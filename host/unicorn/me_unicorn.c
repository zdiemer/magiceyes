/* magiceyes Unicorn backend (WIP) — a portable ARM-Linux userland engine.
 *
 * Loads a (static, for now) ARM ELF into a Unicorn CPU, sets up the SysV stack
 * (argv/envp/auxv), and emulates the Linux-ARM (EABI) syscall ABI by trapping
 * SVC. Unknown syscalls are logged so we can grow the table by running real
 * binaries. Later increments add: mmap-backed file maps, the GP2X/Wiz hardware
 * devices (/dev/fb0, /dev/mem, gpio, dsp -> shm+viewer), in-process un-GPEComp,
 * and a dynamic-linker path. Cross-platform: only depends on Unicorn + libc.
 *
 * Build: host/unicorn/build.sh   Run: me_unicorn <static-arm.elf> [args...]
 */
#include <unicorn/unicorn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <elf.h>
#include "gp2xshm.h"

/* ---- guest virtual memory layout ---- */
#define STACK_TOP   0x80000000u
#define STACK_SIZE  (8u * 1024 * 1024)
#define MMAP_BASE   0x40000000u
#define MMAP_END    0x70000000u
#define PAGE        0x1000u
#define ALIGN_DN(x) ((x) & ~(PAGE - 1))
#define ALIGN_UP(x) (((x) + PAGE - 1) & ~(PAGE - 1))

static uc_engine *g_uc;
static uint32_t g_brk, g_brk_start;
static uint32_t g_mmap_next = MMAP_BASE;
static int g_exit = 0, g_exit_code = 0;
static int g_trace = 0;

/* Synchronous fork: snapshot the process, let the child run in-line until it
   exits (its pipe writes are captured along the way), then restore the parent
   and resume it with fork()==child_pid. Handles the common "fork a loader, pipe
   results back to the parent" pattern (e.g. Payback) without nested emulation:
   the child's exit_group restores the saved CPU context (PC -> parent's post-fork
   site) and execution simply continues as the parent. */
static uc_context *g_fork_ctx = NULL;
static struct snap { uint64_t begin; uint32_t len; uint8_t *data; } g_snap[2048];
static int g_nsnap = 0, g_forked = 0;
static uint32_t g_child_pid = 0x1234;

/* in-engine pipe (parent <-> forked child); one pair is enough for the loaders
   seen so far. Backed by a growable host buffer that survives the fork restore. */
/* far above any real host fd so they never alias a file descriptor */
#define PIPEFD_R 0x10000100
#define PIPEFD_W 0x10000101
static uint8_t *g_pipebuf = NULL;
static uint32_t g_pipe_cap = 0, g_pipe_w = 0, g_pipe_r = 0;
static void pipe_put(const uint8_t *p, uint32_t n) {
    if (g_pipe_w + n > g_pipe_cap) {
        g_pipe_cap = (g_pipe_w + n) * 2 + 4096;
        g_pipebuf = realloc(g_pipebuf, g_pipe_cap);
    }
    memcpy(g_pipebuf + g_pipe_w, p, n); g_pipe_w += n;
}

/* ---- cooperative thread scheduler ----
   clone(CLONE_VM) threads share this single Unicorn address space; only CPU
   state differs, so a context switch = uc_context_save(cur)+uc_context_restore
   (next). Threads run until they block (futex/sigsuspend) or yield; a blocking
   syscall pre-sets its own wake-time R0, then switches away. intr_cb skips its
   R0 write when a switch happened (the new thread's R0 is already correct). */
enum { TH_FREE = 0, TH_RUN, TH_BLOCKED, TH_SLEEPING, TH_DEAD };
enum { BLK_NONE = 0, BLK_FUTEX, BLK_SIG };
#define MAXTH 32
struct thread {
    uc_context *ctx;        /* saved CPU state when not the running thread */
    int state, block, tid, ppid;
    uint32_t tls;           /* per-thread TLS, mirrored at 0xffff0ff0 on switch */
    uint32_t futex_addr;    /* BLK_FUTEX: address waited on */
    uint32_t ctid;          /* CLONE_CHILD_CLEARTID: clear+futex-wake on exit */
    uint64_t sig_pending, sig_blocked;  /* signals 1..64 (bit s-1) */
    uint64_t susp_oldmask;  /* sig_blocked to restore when a sigsuspend returns */
    int susp_active;
    int has_sigsave;        /* a signal handler is running; sigsave holds the resume regs */
    uint32_t sigsave[17];   /* r0..r15 + cpsr, restored by (rt_)sigreturn */
    double wake_deadline;   /* TH_SLEEPING: host time at which to wake (nanosleep) */
    int enoent_streak;      /* consecutive failed opens -> back off (music worker) */
};
static struct thread g_th[MAXTH];
static int g_nth = 0, g_cur = 0, g_next_tid = 100;  /* main != pid 1 (LinuxThreads orphan check) */
static int g_switched = 0;   /* a syscall changed the running thread this trap */

/* CLONE_* flags we care about */
#define ME_CLONE_VM            0x00000100
#define ME_CLONE_PARENT_SETTID 0x00100000
#define ME_CLONE_CHILD_CLEARTID 0x00200000
#define ME_CLONE_CHILD_SETTID  0x01000000
#define ME_CLONE_SETTLS        0x00080000

static void die(const char *m, uc_err e) {
    fprintf(stderr, "me_unicorn: %s: %s\n", m, e ? uc_strerror(e) : "");
    exit(1);
}

/* map [addr,addr+size) page-aligned (idempotent-ish; ignores already-mapped). */
static void map_region(uint32_t addr, uint32_t size, uint32_t perms) {
    uint32_t a = ALIGN_DN(addr), end = ALIGN_UP(addr + size);
    uc_err e = uc_mem_map(g_uc, a, end - a, perms);
    if (e && e != UC_ERR_MAP) die("uc_mem_map", e);
}

/* ---- ELF loader (static EXEC) ---- */
static uint32_t load_elf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("open elf"); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) { perror("read elf"); exit(1); }
    fclose(f);

    Elf32_Ehdr *eh = (Elf32_Ehdr *)buf;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32)
        { fprintf(stderr, "not a 32-bit ELF\n"); exit(1); }
    if (eh->e_machine != EM_ARM) { fprintf(stderr, "not ARM\n"); exit(1); }
    if (eh->e_type != ET_EXEC) { fprintf(stderr, "only static ET_EXEC for now\n"); exit(1); }

    uint32_t max_end = 0;
    Elf32_Phdr *ph = (Elf32_Phdr *)(buf + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint32_t va = ph[i].p_vaddr, fsz = ph[i].p_filesz, msz = ph[i].p_memsz;
        uint32_t perms = UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC; /* relax for now */
        map_region(va, msz, perms);
        if (fsz) {
            uc_err e = uc_mem_write(g_uc, va, buf + ph[i].p_offset, fsz);
            if (e) die("uc_mem_write seg", e);
        }
        if (va + msz > max_end) max_end = va + msz;
        if (g_trace) fprintf(stderr, "  PT_LOAD va=%08x filesz=%u memsz=%u\n", va, fsz, msz);
    }
    g_brk_start = g_brk = ALIGN_UP(max_end);
    map_region(g_brk, PAGE, UC_PROT_READ | UC_PROT_WRITE); /* initial brk page */
    uint32_t entry = eh->e_entry;
    free(buf);
    return entry;
}

/* ---- stack: argc, argv[], NULL, envp[], NULL, auxv[], NULL ---- */
static uint32_t setup_stack(int argc, char **argv) {
    map_region(STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    uint32_t sp = STACK_TOP;

    /* push strings, collect guest pointers */
    uint32_t argp[64]; int n = argc < 63 ? argc : 63;
    for (int i = n - 1; i >= 0; i--) {
        size_t l = strlen(argv[i]) + 1;
        sp -= l; sp &= ~3u;
        uc_mem_write(g_uc, sp, argv[i], l);
        argp[i] = sp;
    }
    /* 16 random bytes for AT_RANDOM */
    sp -= 16; sp &= ~15u; uint32_t at_random = sp;
    uint8_t rnd[16] = {0x4d,0x61,0x67,0x69,0x63,0x45,0x79,0x65,0x73,1,2,3,4,5,6,7};
    uc_mem_write(g_uc, sp, rnd, 16);

    /* build the initial stack block; align so final sp is 8-aligned */
    uint32_t aux[][2] = {
        {6 /*AT_PAGESZ*/, PAGE},
        {25/*AT_RANDOM*/, at_random},
        {0 /*AT_NULL*/, 0},
    };
    int naux = sizeof(aux) / sizeof(aux[0]);
    /* total words: argc(1) + argv(n) + null(1) + envp null(1) + aux(2*naux) */
    int words = 1 + n + 1 + 1 + 2 * naux;
    uint32_t block = sp - words * 4;
    block &= ~7u;
    uint32_t p = block;
    uint32_t w;
    w = n;        uc_mem_write(g_uc, p, &w, 4); p += 4;
    for (int i = 0; i < n; i++) { uc_mem_write(g_uc, p, &argp[i], 4); p += 4; }
    w = 0;        uc_mem_write(g_uc, p, &w, 4); p += 4;   /* argv NULL */
    w = 0;        uc_mem_write(g_uc, p, &w, 4); p += 4;   /* envp NULL */
    for (int i = 0; i < naux; i++) {
        uc_mem_write(g_uc, p, &aux[i][0], 4); p += 4;
        uc_mem_write(g_uc, p, &aux[i][1], 4); p += 4;
    }
    return block;
}

/* ---- syscalls (ARM EABI numbers) ---- */
static uint32_t gread(uint32_t reg) { uint32_t v; uc_reg_read(g_uc, reg, &v); return v; }
static void gwrite(uint32_t reg, uint32_t v) { uc_reg_write(g_uc, reg, &v); }

static void deliver_signals(void);  /* defined below; runs a pending handler */

static double host_now(void);   /* defined below; wall-clock seconds */
/* wake any TH_SLEEPING thread whose nanosleep deadline has passed. */
static void wake_sleepers(void) {
    double now = host_now();
    for (int i = 0; i < g_nth; i++)
        if (g_th[i].state == TH_SLEEPING && now >= g_th[i].wake_deadline)
            g_th[i].state = TH_RUN;
}
/* pick the next runnable thread (round-robin from g_cur), or -1 if none. When only
   sleeping threads remain, really sleep until the earliest deadline + wake it — this
   paces the game to real time (frame cap). */
static int sched_pick(void) {
    if (g_forked) return g_th[g_cur].state == TH_RUN ? g_cur : -1;  /* child is alone */
    wake_sleepers();
    for (int i = 1; i <= g_nth; i++) {
        int j = (g_cur + i) % g_nth;
        if (g_th[j].state == TH_RUN) return j;
    }
    if (g_th[g_cur].state == TH_RUN) return g_cur;
    int best = -1; double bestt = 0;
    for (int i = 0; i < g_nth; i++)
        if (g_th[i].state == TH_SLEEPING && (best < 0 || g_th[i].wake_deadline < bestt))
            { best = i; bestt = g_th[i].wake_deadline; }
    if (best >= 0) {
        double dt = bestt - host_now();
        if (dt > 0) { if (dt > 0.1) dt = 0.1; usleep((useconds_t)(dt * 1e6)); }
        g_th[best].state = TH_RUN;
        return best;
    }
    return -1;
}
/* switch the live CPU to thread j (saving the current one). Sets g_switched. */
static void sched_switch_to(int j) {
    if (j == g_cur) return;
    uc_context_save(g_uc, g_th[g_cur].ctx);
    uc_mem_read(g_uc, 0xffff0ff0u, &g_th[g_cur].tls, 4);
    g_cur = j;
    uc_mem_write(g_uc, 0xffff0ff0u, &g_th[g_cur].tls, 4);
    uc_context_restore(g_uc, g_th[g_cur].ctx);
    g_switched = 1;
    deliver_signals();   /* if the now-current thread has a pending signal, enter its handler */
}
/* block the current thread (caller already set its wake-time R0) and run next. */
static void block_current(int reason) {
    if (g_trace) { static int n = 0; if (n++ < 400)
        fprintf(stderr, "  [block tid=%d reason=%d]\n", g_th[g_cur].tid, reason); }
    g_th[g_cur].state = TH_BLOCKED; g_th[g_cur].block = reason;
    int j = sched_pick();
    if (j < 0) {
        fprintf(stderr, "me_unicorn: all %d threads blocked (deadlock)\n", g_nth);
        g_exit = 1; uc_emu_stop(g_uc); return;
    }
    sched_switch_to(j);
}

/* ---- signals (enough for the glibc LinuxThreads restart/cancel handshake) ---- */
#define SIG_TRAMP 0xffff0f00u   /* restorer trampoline in the kuser page */
struct sigact { uint32_t handler, flags, restorer; uint64_t mask; };
static struct sigact g_sigact[65];
/* r0..r12, sp, lr, pc, cpsr — the set saved/restored across a handler */
static const int g_sregs[17] = {
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR };

/* If the current thread has a pending, unblocked signal with a handler, enter it.
   The pre-handler register state is saved so (rt_)sigreturn can resume it. */
static void deliver_signals(void) {
    struct thread *t = &g_th[g_cur];
    if (t->has_sigsave) return;                 /* one level deep is enough here */
    uint64_t deliv = t->sig_pending & ~t->sig_blocked;
    if (!deliv) return;
    int sig = 0;
    for (int s = 1; s <= 64; s++) if (deliv & (1ULL << (s - 1))) { sig = s; break; }
    t->sig_pending &= ~(1ULL << (sig - 1));
    uint32_t h = g_sigact[sig].handler;
    if (h == 0 || h == 1) return;               /* SIG_DFL / SIG_IGN: drop */
    for (int i = 0; i < 17; i++) t->sigsave[i] = gread(g_sregs[i]);
    t->has_sigsave = 1;
    gwrite(UC_ARM_REG_R0, (uint32_t)sig);
    gwrite(UC_ARM_REG_LR, SIG_TRAMP);
    gwrite(UC_ARM_REG_PC, h);
    if (g_trace) fprintf(stderr, "  [signal %d -> handler %08x in tid %d]\n", sig, h, t->tid);
}

/* deliver signal `sig` to the thread whose tid == pid (LinuxThreads = 1 pid/thread). */
static long send_sig(int pid, int sig) {
    if (sig <= 0 || sig > 64) return 0;
    for (int i = 0; i < g_nth; i++) {
        if (g_th[i].tid != pid || g_th[i].state == TH_DEAD) continue;
        g_th[i].sig_pending |= (1ULL << (sig - 1));
        if (g_th[i].state == TH_BLOCKED && g_th[i].block == BLK_SIG
            && !(g_th[i].sig_blocked & (1ULL << (sig - 1))))
            g_th[i].state = TH_RUN;
        return 0;
    }
    return 0;   /* unknown pid: drop */
}

/* ---- emulated GP2X/Wiz devices (fake fds, not passed to the host) ---- */
enum { DEV_FB = 1, DEV_MEM, DEV_GPIO, DEV_DSP, DEV_MIXER, DEV_TTY, DEV_OTHER };
#define DEVFD_BASE 0x10000000   /* far above real host fds (avoid aliasing) */
static int g_devtype[64], g_devn = 0;
static int dev_open(const char *path) {
    int t;
    if (!strncmp(path, "/dev/fb", 7))         t = DEV_FB;
    else if (!strcmp(path, "/dev/mem"))       t = DEV_MEM;
    else if (!strcmp(path, "/dev/gpio"))      t = DEV_GPIO;
    else if (!strncmp(path, "/dev/dsp", 8))   t = DEV_DSP;
    else if (!strncmp(path, "/dev/mixer", 10))t = DEV_MIXER;
    else if (!strncmp(path, "/dev/tty", 8))   t = DEV_TTY;
    else return -1;
    int i; for (i = 0; i < 64; i++) if (g_devtype[i] == 0) break;  /* reuse freed slots */
    if (i == 64) return -1;
    g_devtype[i] = t; if (i + 1 > g_devn) g_devn = i + 1;
    if (g_trace) fprintf(stderr, "  DEV open %s -> fd=%d type=%d\n", path, DEVFD_BASE + i, t);
    return DEVFD_BASE + i;
}
static void dev_close(int fd) { int i = fd - DEVFD_BASE; if (i >= 0 && i < 64) g_devtype[i] = 0; }
static int dev_type(int fd) {
    int i = fd - DEVFD_BASE;
    return (i >= 0 && i < g_devn) ? g_devtype[i] : 0;
}

/* ---- shm framebuffer bridge to the native viewer (shared w/ the Wiz shim) ---- */
static gp2x_shm_t *g_shm = NULL;
static void shm_setup(void) {
    int fd = shm_open(GP2XSHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) return;
    if (ftruncate(fd, sizeof(gp2x_shm_t)) != 0) { /* may pre-exist */ }
    void *p = mmap(NULL, sizeof(gp2x_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return;
    g_shm = p; g_shm->buttons = 0; g_shm->quit = 0; g_shm->frame_seq = 0;
    g_shm->magic = GP2XSHM_MAGIC;
}

/* /dev/mem mmap tracking so MMSP2 framebuffer phys addresses resolve to guest. */
struct memmap { uint32_t phys, guest, len; };
static struct memmap g_mem[64]; static int g_nmem = 0;
static uint32_t g_mmsp2_guest = 0;   /* guest addr of the 0xC0000000 reg block */
static uint32_t g_fb_guest = 0;      /* guest addr of the /dev/fb0 framebuffer */
static uint32_t g_fb_guest2 = 0;     /* /dev/fb1 (double-buffering: present the active one) */
static void record_memmap(uint32_t phys, uint32_t guest, uint32_t len) {
    if (g_nmem < 64) { g_mem[g_nmem] = (struct memmap){phys, guest, len}; g_nmem++; }
}
static int phys_to_guest(uint32_t phys, uint32_t *out) {
    for (int i = 0; i < g_nmem; i++)
        if (phys >= g_mem[i].phys && phys < g_mem[i].phys + g_mem[i].len)
            { *out = g_mem[i].guest + (phys - g_mem[i].phys); return 1; }
    return 0;
}

/* GP2X native screen = 320x240 RGB565. Present framebuffer at `phys` to shm. */
static void present_guest(uint32_t g) {
    if (!g_shm || !g) return;
    uint8_t row[320 * 2];
    for (int y = 0; y < 240; y++) {
        if (uc_mem_read(g_uc, g + (uint32_t)y * 640, row, sizeof row)) break;
        memcpy(g_shm->pixels + (size_t)y * GP2XSHM_MAXW * 2, row, sizeof row);
    }
    g_shm->width = 320; g_shm->height = 240; g_shm->frame_seq++;
}
static void present_fb(uint32_t phys) {
    uint32_t g; if (phys_to_guest(phys, &g)) present_guest(g);
}
/* the game double-buffers across fb0/fb1; present whichever currently has content. */
static int buf_score(uint32_t g) {
    if (!g) return -1;
    uint8_t row[320 * 2]; int nz = 0;
    for (int y = 8; y < 240; y += 24) {
        if (uc_mem_read(g_uc, g + (uint32_t)y * 640, row, sizeof row)) break;
        for (unsigned i = 0; i < sizeof row; i++) if (row[i]) { nz++; break; }
    }
    return nz;
}
static uint32_t buf_hash(uint32_t g) {
    if (!g) return 0;
    uint8_t row[320 * 2]; uint32_t h = 2166136261u;
    for (int y = 0; y < 240; y += 12) {
        if (uc_mem_read(g_uc, g + (uint32_t)y * 640, row, sizeof row)) break;
        for (unsigned i = 0; i < sizeof row; i += 5) h = (h ^ row[i]) * 16777619u;
    }
    return h;
}
/* present whichever fb the game just rendered to (its content changed since last
   frame); fall back to the non-blank one for a fully static screen. */
static void present_active(void) {
    static double last = 0;            /* cap to ~60fps: the game's tiny nanosleeps call
                                          this ~2000/s, and hashing+copying every time
                                          (300MB/s) was choking the emulator. */
    double now = host_now();
    if (now - last < 0.016) return;
    last = now;
    static uint32_t h0 = 0, h1 = 0;
    uint32_t n0 = buf_hash(g_fb_guest), n1 = buf_hash(g_fb_guest2);
    int c0 = (n0 != h0), c1 = (n1 != h1);
    h0 = n0; h1 = n1;
    if (c1 && g_fb_guest2) present_guest(g_fb_guest2);
    else if (c0) present_guest(g_fb_guest);
    else present_guest(buf_score(g_fb_guest2) > buf_score(g_fb_guest) ? g_fb_guest2 : g_fb_guest);
}

/* ---- /dev/dsp (OSS) audio -> shm audio ring (consumed by the viewer) ---- */
static uint32_t g_aud_freq = 44100, g_aud_ch = 2, g_aud_bits = 16;
static double g_aud_t0 = 0; static int g_aud_on = 0;
static double host_now(void) {
    struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec * 1e-6;
}
/* advance the read cursor as if played in real time, so the ring drains and the
   game keeps producing at the right rate even with no viewer attached. */
static void aud_drain(void) {
    if (!g_shm) return;
    if (!g_aud_on) { g_aud_t0 = host_now(); g_aud_on = 1; }
    uint32_t bps = g_aud_freq * g_aud_ch * (g_aud_bits / 8);
    if (!bps) return;
    uint64_t consumed = (uint64_t)((host_now() - g_aud_t0) * bps);
    if (consumed > g_shm->a_write) consumed = g_shm->a_write;
    if (consumed > g_shm->a_read) g_shm->a_read = (uint32_t)consumed;  /* viewer may be ahead */
}
static uint32_t aud_free(void) {
    aud_drain();
    if (!g_shm) return 0;
    uint32_t used = g_shm->a_write - g_shm->a_read;
    return used < GP2XSHM_ARING ? GP2XSHM_ARING - used : 0;
}
static long dsp_write(uint32_t gbuf, uint32_t n) {
    if (!g_shm) return n;
    uint32_t fr = aud_free();
    if (n > fr) n = fr;
    if (n) {
        uint8_t *tmp = malloc(n); uc_mem_read(g_uc, gbuf, tmp, n);
        uint32_t w = g_shm->a_write % GP2XSHM_ARING, first = GP2XSHM_ARING - w;
        if (first > n) first = n;
        memcpy(g_shm->aring + w, tmp, first);
        if (n > first) memcpy(g_shm->aring, tmp + first, n - first);
        g_shm->a_write += n; free(tmp);
    }
    return n;
}
/* OSS dsp ioctl (type 'P' == 0x50); arg usually points to an int (in/out). */
static long dsp_ioctl(uint32_t cmd, uint32_t arg) {
    uint32_t v = 0; if (arg) uc_mem_read(g_uc, arg, &v, 4);
    switch (cmd & 0xff) {
    case 0x02: /* SPEED   */ if (v) g_aud_freq = v; break;
    case 0x03: /* STEREO  */ g_aud_ch = v ? 2 : 1; break;
    case 0x06: /* CHANNELS*/ if (v) g_aud_ch = v; break;
    case 0x05: /* SETFMT  */ g_aud_bits = (v == 8 /*AFMT_U8*/) ? 8 : 16; break;
    case 0x04: /* GETBLKSIZE */ v = 4096; break;
    case 0x0a: /* SETFRAGMENT: accept the request as-is */ break;
    case 0x0b: /* GETFMTS  */ v = 0x18; /* AFMT_S16_LE|AFMT_U8 */ break;
    case 0x0f: /* GETCAPS  */ v = 0; break;
    case 0x17: /* GETODELAY*/ aud_drain(); v = g_shm ? (g_shm->a_write - g_shm->a_read) : 0; break;
    case 0x0c: /* GETOSPACE -> audio_buf_info{fragments,fragstotal,fragsize,bytes} */ {
        uint32_t freeb = aud_free(), fsz = 4096;
        uint32_t info[4] = { freeb / fsz, GP2XSHM_ARING / fsz, fsz, freeb };
        if (arg) uc_mem_write(g_uc, arg, info, 16);
        return 0;
    }
    case 0x00: /* RESET */ case 0x01: /* SYNC */ case 0x08: /* POST */ return 0;
    default: return 0;
    }
    /* publish the negotiated format so a viewer can open the right audio device */
    if (g_shm) {
        g_shm->audio_freq = g_aud_freq;
        g_shm->audio_format = (g_aud_bits == 8) ? 0x0008u : 0x8010u; /* U8 / S16LSB */
        g_shm->audio_channels = g_aud_ch;
        g_shm->audio_active = 1;
    }
    if (arg) uc_mem_write(g_uc, arg, &v, 4);   /* write-back the (possibly clamped) value */
    return 0;
}

/* MMSP2 framebuffer-address registers (byte offsets in the 0xC0000000 block) */
#define MMSP2_OADRL 0x290e
#define MMSP2_OADRH 0x2910
static void mmsp2_write_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                           int size, int64_t value, void *user) {
    (void)type; (void)user;
    if (!g_mmsp2_guest) return;
    uint32_t off = (uint32_t)addr - g_mmsp2_guest;
    if (g_trace) { static int n = 0; if (n++ < 400)
        fprintf(stderr, "  MMSP2 wr %04x sz%d=%08x\n", off, size, (uint32_t)value); }
    if (off != MMSP2_OADRL && off != MMSP2_OADRH) return;
    uint16_t lo = 0, hi = 0;
    uc_mem_read(uc, g_mmsp2_guest + MMSP2_OADRL, &lo, 2);
    uc_mem_read(uc, g_mmsp2_guest + MMSP2_OADRH, &hi, 2);
    if (off == MMSP2_OADRL) { lo = value & 0xffff; if (size == 4) hi = (value >> 16) & 0xffff; }
    else hi = value & 0xffff;
    uint32_t phys = ((uint32_t)hi << 16) | lo;
    { static int n = 0; if (n++ < 8)
        fprintf(stderr, "  MMSP2 flip -> phys=%08x\n", phys); }
    present_fb(phys);
}
/* Serve MMSP2 register reads. The free-running microsecond timer (TCOUNT @ 0x0a00)
   must advance or the game's timing/frame loops spin forever. */
static void mmsp2_read_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                          int size, int64_t value, void *user) {
    (void)type; (void)size; (void)value; (void)user;
    if (!g_mmsp2_guest) return;
    uint32_t off = (uint32_t)addr - g_mmsp2_guest;
    if (off == 0x0a00) {           /* TCOUNT: 1us free-running counter */
        static double t0 = 0; struct timeval tv; gettimeofday(&tv, NULL);
        double now = tv.tv_sec + tv.tv_usec * 1e-6;
        if (t0 == 0) t0 = now;
        uint32_t us = (uint32_t)((now - t0) * 1e6);
        uc_mem_write(uc, g_mmsp2_guest + 0x0a00, &us, 4);
        return;
    }
    /* GPIO button registers (active low; canonical GP2X layout, matches our shm enum):
       0x1198 lo = 8-way stick, 0x1184 hi = START/SELECT/L/R/A/B/X/Y, 0x1186 lo = VOL. */
    if (off == 0x1198 || off == 0x1184 || off == 0x1186) {
        if (g_trace) { static int n = 0; if (n++ < 12)
            fprintf(stderr, "  GPIO RD %04x pc=%08x\n", off, gread(UC_ARM_REG_PC)); }
        uint32_t b = g_shm ? g_shm->buttons : 0;
        uint16_t v;
        if (off == 0x1198)      v = 0xFF00 | (~b & 0x00FF);            /* stick (bits 0..7) */
        else if (off == 0x1184) v = 0x00FF | ((~(b >> 8) & 0xFF) << 8); /* buttons -> hi byte */
        else                    v = 0xFF00 | (~(b >> 16) & 0xFF);      /* VOL -> lo byte */
        uc_mem_write(uc, g_mmsp2_guest + off, &v, 2);
        return;
    }
    if (g_trace) { static int n = 0; if (n++ < 200) fprintf(stderr, "  MMSP2 RD %04x\n", off); }
}

#define GMAP_FIXED 0x10u
#define GMAP_ANON  0x20u
/* unified mmap for old_mmap(90) and mmap2(192); file-backed reads via pread. */
static long do_mmap(uint32_t addr, uint32_t len, uint32_t flags, int fd, uint64_t off) {
    uint32_t l = ALIGN_UP(len ? len : 1);
    uint32_t at;
    if ((flags & GMAP_FIXED) && addr) at = ALIGN_DN(addr);
    else {
        /* align to the allocation size when it's a power of two (capped at 2MB):
           LinuxThreads needs its STACK_SIZE-sized thread stacks STACK_SIZE-aligned
           so sp&~(size-1) locates the thread descriptor, else it retries forever. */
        uint32_t align = PAGE;
        if (l > PAGE && (l & (l - 1)) == 0) align = l > 0x200000u ? 0x200000u : l;
        g_mmap_next = (g_mmap_next + align - 1) & ~(align - 1);
        at = g_mmap_next; g_mmap_next += l;
    }
    map_region(at, l, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
    if (!(flags & GMAP_ANON) && fd >= 0 && len) {
        uint8_t *tmp = malloc(len);
        ssize_t n = pread(fd, tmp, len, (off_t)off);
        if (n > 0) uc_mem_write(g_uc, at, tmp, n);
        free(tmp);
    }
    return at;
}

/* device mmap: give RAM, track phys->guest, and hook MMSP2 reg writes for flips. */
static long dev_mmap(int type, uint32_t addr, uint32_t len, uint32_t flags, uint32_t phys) {
    uint32_t at = do_mmap(addr, len, flags | GMAP_ANON, -1, 0);
    fprintf(stderr, "  DEV mmap type=%d phys=%08x -> guest=%08x len=%08x\n",
            type, phys, at, len);
    if (type == DEV_FB) {                                 /* track up to 2 fb buffers */
        if (!g_fb_guest) g_fb_guest = at;
        else if (!g_fb_guest2 && at != g_fb_guest) g_fb_guest2 = at;
    }
    if (type == DEV_MEM) {
        record_memmap(phys, at, len);
        if (phys == 0xC0000000u) {
            g_mmsp2_guest = at;
            static uc_hook hh, hr;
            uc_hook_add(g_uc, &hh, UC_HOOK_MEM_WRITE, mmsp2_write_cb, NULL,
                        at, at + len - 1);
            uc_hook_add(g_uc, &hr, UC_HOOK_MEM_READ, mmsp2_read_cb, NULL,
                        at, at + len - 1);
        }
    }
    return at;
}

/* fill an OABI `struct stat` (ARM: st_mode@8, st_size@20) — enough for size. */
static void fill_oabi_stat(uint32_t gbuf, struct stat *hs) {
    uint8_t b[88]; memset(b, 0, sizeof b);
    *(uint32_t *)(b + 0)  = (uint32_t)hs->st_dev;
    *(uint32_t *)(b + 4)  = (uint32_t)hs->st_ino;
    *(uint16_t *)(b + 8)  = (uint16_t)hs->st_mode;
    *(uint16_t *)(b + 10) = (uint16_t)(hs->st_nlink ? hs->st_nlink : 1);
    *(uint32_t *)(b + 16) = (uint32_t)hs->st_rdev;
    *(uint32_t *)(b + 20) = (uint32_t)hs->st_size;
    *(uint32_t *)(b + 24) = 4096;
    *(uint32_t *)(b + 28) = (uint32_t)((hs->st_size + 511) / 512);
    uc_mem_write(g_uc, gbuf, b, sizeof b);
}

/* fill the ARM EABI `struct stat64` — offsets verified empirically (sizeof 112):
   st_dev@0 st_ino@8 st_mode@16 st_nlink@20 st_uid@24 st_gid@28 st_rdev@32
   st_size@40 st_blksize@48 st_blocks@56. (Payback is EABI structs + OABI syscalls;
   my earlier packed @44 guess gave a garbage size and crashed the game.) */
static void fill_stat64(uint32_t gbuf, struct stat *hs) {
    uint8_t b[112]; memset(b, 0, sizeof b);
    *(uint64_t *)(b + 0)  = (uint64_t)hs->st_dev;
    *(uint64_t *)(b + 8)  = (uint64_t)hs->st_ino;
    *(uint32_t *)(b + 16) = (uint32_t)hs->st_mode;
    *(uint32_t *)(b + 20) = (uint32_t)(hs->st_nlink ? hs->st_nlink : 1);
    *(uint32_t *)(b + 24) = (uint32_t)hs->st_uid;
    *(uint32_t *)(b + 28) = (uint32_t)hs->st_gid;
    *(uint64_t *)(b + 32) = (uint64_t)hs->st_rdev;
    *(uint64_t *)(b + 40) = (uint64_t)hs->st_size;
    *(uint32_t *)(b + 48) = 4096;                          /* st_blksize */
    *(uint64_t *)(b + 56) = (uint64_t)((hs->st_size + 511) / 512); /* st_blocks */
    uc_mem_write(g_uc, gbuf, b, sizeof b);
}

static long sys_dispatch(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;
    if (g_trace)
        fprintf(stderr, "  sc %u (%08x,%08x,%08x,%08x)\n", nr, a0, a1, a2, a3);
    /* single-buffered titles never "flip"; refresh the live fb0 periodically */
    { static unsigned c = 0; if (g_fb_guest && (++c & 63) == 0) present_active(); }
    switch (nr) {
    case 1:    /* exit */
    case 248:  /* exit_group */
        if (g_forked) {  /* the synchronous fork child is done -> restore parent */
            for (int i = 0; i < g_nsnap; i++) {
                uc_mem_write(g_uc, g_snap[i].begin, g_snap[i].data, g_snap[i].len);
                free(g_snap[i].data);
            }
            g_nsnap = 0;
            uc_context_restore(g_uc, g_fork_ctx);
            uc_context_free(g_fork_ctx); g_fork_ctx = NULL;
            g_forked = 0;
            if (g_trace) fprintf(stderr, "  [fork] child exited(%u) -> resume parent\n", a0);
            return g_child_pid;  /* parent's fork() now returns the child pid */
        }
        if (nr == 1) {   /* exit(): this thread only — keep the process alive */
            if (g_th[g_cur].ctid) {     /* CLONE_CHILD_CLEARTID: clear + futex-wake */
                uint32_t z = 0; uc_mem_write(g_uc, g_th[g_cur].ctid, &z, 4);
                for (int i = 0; i < g_nth; i++)
                    if (g_th[i].state == TH_BLOCKED && g_th[i].block == BLK_FUTEX
                        && g_th[i].futex_addr == g_th[g_cur].ctid)
                        g_th[i].state = TH_RUN;
            }
            g_th[g_cur].state = TH_DEAD;
            int j = sched_pick();
            if (g_trace) fprintf(stderr, "  [thread %d exit] -> next slot %d\n", g_th[g_cur].tid, j);
            if (j >= 0) { sched_switch_to(j); return 0; }
        }
        fprintf(stderr, "  [REAL EXIT] code=%u nr=%u tid=%d forked=%d nth=%d nsnap=%d\n",
                a0, nr, g_th[g_cur].tid, g_forked, g_nth, g_nsnap);
        g_exit = 1; g_exit_code = a0; uc_emu_stop(g_uc); return 0;
    case 4: {  /* write(fd, buf, count) */
        uint8_t *tmp = malloc(a2 ? a2 : 1);
        uc_mem_read(g_uc, a1, tmp, a2);
        if ((int)a0 == PIPEFD_W) { pipe_put(tmp, a2); free(tmp); return a2; }
        if (dev_type((int)a0) == DEV_DSP) { free(tmp); return dsp_write(a1, a2); }
        if (dev_type((int)a0)) { free(tmp); return a2; }  /* other devices: accept + discard */
        long r = write((int)a0, tmp, a2); free(tmp);
        return r < 0 ? -errno : r;
    }
    case 3: {  /* read(fd, buf, count) */
        if ((int)a0 == PIPEFD_R) {  /* drain the forked child's pipe output */
            uint32_t avail = g_pipe_w - g_pipe_r, n = a2 < avail ? a2 : avail;
            if (n) uc_mem_write(g_uc, a1, g_pipebuf + g_pipe_r, n);
            g_pipe_r += n; return n;   /* 0 == EOF (child finished) */
        }
        uint8_t *tmp = malloc(a2 ? a2 : 1);
        long r = read((int)a0, tmp, a2);
        if (r > 0) uc_mem_write(g_uc, a1, tmp, r);
        free(tmp); return r < 0 ? -errno : r;
    }
    case 45: { /* brk(addr) */
        if (a0 == 0) return g_brk;
        uint32_t na = ALIGN_UP(a0);
        if (na > ALIGN_UP(g_brk))
            map_region(ALIGN_UP(g_brk), na - ALIGN_UP(g_brk), UC_PROT_READ | UC_PROT_WRITE);
        g_brk = a0;
        return g_brk;
    }
    case 90: { /* old_mmap(ptr->{addr,len,prot,flags,fd,offset_bytes}) */
        uint32_t m[6]; uc_mem_read(g_uc, a0, m, sizeof m);
        int fd = (m[4] == 0xffffffffu) ? -1 : (int)m[4], t = dev_type(fd);
        if (t) return dev_mmap(t, m[0], m[1], m[3], m[5]);
        return do_mmap(m[0], m[1], m[3], fd, m[5]);
    }
    case 192: { /* mmap2: a4=fd, a5=pgoff (4096 units) */
        int fd = (a4 == 0xffffffffu) ? -1 : (int)a4, t = dev_type(fd);
        if (t) return dev_mmap(t, a0, a1, a3, (uint32_t)(a5 * 4096));
        return do_mmap(a0, a1, a3, fd, (uint64_t)a5 * 4096);
    }
    case 91: { /* munmap(addr, len) — actually free it so Unicorn's region table
                  doesn't overflow (mmap/munmap churn would otherwise leak regions) */
        uint32_t a = ALIGN_DN(a0), l = ALIGN_UP(a1);
        if (l) uc_mem_unmap(g_uc, a, l);   /* ignore errors: may be unaligned/partial */
        return 0;
    }
    case 2: { /* fork: snapshot, run the child in-line, restore parent on its exit */
        if (uc_context_alloc(g_uc, &g_fork_ctx) != UC_ERR_OK) return -ENOMEM;
        uc_context_save(g_uc, g_fork_ctx);
        uc_mem_region *regs = NULL; uint32_t cnt = 0; g_nsnap = 0;
        if (uc_mem_regions(g_uc, &regs, &cnt) == UC_ERR_OK) {
            for (uint32_t i = 0; i < cnt && g_nsnap < 2048; i++) {
                uint64_t b = regs[i].begin;
                uint32_t l = (uint32_t)(regs[i].end - regs[i].begin + 1);
                uint8_t *d = malloc(l);
                if (!d || uc_mem_read(g_uc, b, d, l) != UC_ERR_OK) { free(d); continue; }
                g_snap[g_nsnap].begin = b; g_snap[g_nsnap].len = l;
                g_snap[g_nsnap].data = d; g_nsnap++;
            }
            uc_free(regs);
        }
        g_forked = 1;
        if (g_trace) fprintf(stderr, "  [fork] snapshot %d regions; child runs first\n", g_nsnap);
        return 0;  /* child sees fork()==0 */
    }
    case 42: { /* pipe(fds[2]) -> our in-engine pipe */
        g_pipe_r = g_pipe_w = 0;
        uint32_t fds[2] = { PIPEFD_R, PIPEFD_W };
        uc_mem_write(g_uc, a0, fds, 8); return 0;
    }
    case 7: case 114: /* waitpid/wait4: the synchronous child already exited */
        if (a1) { uint32_t z = 0; uc_mem_write(g_uc, a1, &z, 4); }
        return g_child_pid;
    case 13: { /* time(t) */
        uint32_t t = (uint32_t)time(NULL);
        if (a0) uc_mem_write(g_uc, a0, &t, 4); return t;
    }
    case 99: case 100: { /* statfs/fstatfs: report a roomy filesystem */
        uint8_t b[64]; memset(b, 0, sizeof b);
        *(uint32_t *)(b + 4)  = 4096;        /* f_bsize   */
        *(uint32_t *)(b + 8)  = 0x00100000;  /* f_blocks  */
        *(uint32_t *)(b + 12) = 0x00080000;  /* f_bfree   */
        *(uint32_t *)(b + 16) = 0x00080000;  /* f_bavail  */
        *(uint32_t *)(b + 36) = 255;         /* f_namelen */
        if (a1) uc_mem_write(g_uc, a1, b, sizeof b); return 0;
    }
    case 24: case 47: case 49: case 50:       /* getuid/getgid/geteuid/getegid */
    case 199: case 200: case 201: case 202:   /* ...32 variants */
        return 0;
    case 75:   return 0;        /* setrlimit */
    case 149:  return -ENOSYS;  /* _sysctl (glibc tolerates) */
    case 122: { /* uname -> minimal Linux/armv5tel 2.6.24 */
        char u[6 * 65]; memset(u, 0, sizeof u);
        strcpy(u + 0 * 65, "Linux"); strcpy(u + 2 * 65, "2.6.24");
        strcpy(u + 3 * 65, "#1"); strcpy(u + 4 * 65, "armv5tel");
        uc_mem_write(g_uc, a0, u, sizeof u); return 0;
    }
    case 54:   /* ioctl */ {
        int t = dev_type((int)a0);
        if (t == DEV_DSP) return dsp_ioctl(a1, a2);
        return 0;
    }
    case 0xf0005: { /* __ARM_NR_set_tls -> kuser TLS slot */
        uc_mem_write(g_uc, 0xffff0ff0u, &a0, 4); return 0;
    }
    case 0xf0002: return 0; /* __ARM_NR_cacheflush */
    case 5: {  /* open(path, flags, mode) */
        char p[1024]; uc_mem_read(g_uc, a0, p, sizeof p); p[sizeof p - 1] = 0;
        int d = dev_open(p); if (d >= 0) return d;
        long r = open(p, (int)a1, a2); int e2 = errno;
        if (r >= 0) { g_th[g_cur].enoent_streak = 0; return r; }
        /* a thread tight-looping on missing files (the music worker on the absent
           Data/Music/*.ama) burns the single-threaded emulator's throughput and starves
           the game; back it off after a streak of failures so the menu/game gets CPU. */
        if (e2 == ENOENT && ++g_th[g_cur].enoent_streak > 8) {
            g_th[g_cur].enoent_streak = 0;
            gwrite(UC_ARM_REG_R0, (uint32_t)-e2);
            g_th[g_cur].wake_deadline = host_now() + 0.05;
            g_th[g_cur].state = TH_SLEEPING;
            int j = sched_pick();
            if (j >= 0 && j != g_cur) sched_switch_to(j);
        }
        return -e2;
    }
    case 322: { /* openat(dirfd, path, flags, mode) */
        char p[1024]; uc_mem_read(g_uc, a1, p, sizeof p); p[sizeof p - 1] = 0;
        int d = dev_open(p); if (d >= 0) return d;
        long r = open(p, (int)a2, a3); return r < 0 ? -errno : r;
    }
    case 6:    /* close */
        if ((int)a0 == PIPEFD_R || (int)a0 == PIPEFD_W) return 0;
        if (dev_type((int)a0)) { dev_close((int)a0); return 0; }  /* free the device slot */
        return close((int)a0) < 0 ? -errno : 0;
    case 19: { long r = lseek((int)a0, (off_t)a1, (int)a2); return r < 0 ? -errno : r; } /* lseek */
    case 125:  return 0;  /* mprotect (we map RWX) */
    case 20:   return g_th[g_cur].tid;  /* getpid (LinuxThreads: 1 pid per thread) */
    case 224:  return g_th[g_cur].tid;  /* gettid */
    case 64:   return g_th[g_cur].ppid;  /* getppid (LinuxThreads orphan check) */
    case 256:  return 1;  /* set_tid_address */
    case 338:  return 0;  /* set_robust_list */
    case 174: { /* rt_sigaction(signum, act, oldact, sigsetsize) */
        int sig = (int)a0;
        if (sig > 0 && sig <= 64) {
            if (a2) { uint32_t o[3] = {g_sigact[sig].handler, g_sigact[sig].flags,
                                       g_sigact[sig].restorer};
                      uc_mem_write(g_uc, a2, o, 12);
                      uc_mem_write(g_uc, a2 + 12, &g_sigact[sig].mask, 8); }
            if (a1) { uint32_t h[3]; uc_mem_read(g_uc, a1, h, 12);
                      uint64_t m = 0; uc_mem_read(g_uc, a1 + 12, &m, 8);
                      g_sigact[sig].handler = h[0]; g_sigact[sig].flags = h[1];
                      g_sigact[sig].restorer = h[2]; g_sigact[sig].mask = m; }
        }
        return 0;
    }
    case 175: { /* rt_sigprocmask(how, set, oldset, size) */
        struct thread *t = &g_th[g_cur];
        if (a2) uc_mem_write(g_uc, a2, &t->sig_blocked, 8);
        if (a1) { uint64_t set = 0; uc_mem_read(g_uc, a1, &set, 8);
                  if (a0 == 0) t->sig_blocked |= set;
                  else if (a0 == 1) t->sig_blocked &= ~set;
                  else if (a0 == 2) t->sig_blocked = set; }
        return 0;
    }
    case 37:   return send_sig((int)a0, (int)a1);  /* kill(pid, sig) */
    case 238:  return send_sig((int)a0, (int)a1);  /* tkill(tid, sig) */
    case 268:  return send_sig((int)a1, (int)a2);  /* tgkill(tgid, tid, sig) */
    case 119:  /* sigreturn */
    case 173: { /* rt_sigreturn: restore the pre-handler register state */
        struct thread *t = &g_th[g_cur];
        if (t->has_sigsave) { for (int i = 0; i < 17; i++) gwrite(g_sregs[i], t->sigsave[i]);
                              t->has_sigsave = 0; }
        if (t->susp_active) { t->sig_blocked = t->susp_oldmask; t->susp_active = 0; }
        g_switched = 1;   /* PC/regs restored; don't let intr_cb clobber R0 */
        return 0;
    }
    case 168: { /* poll(fds, nfds, timeout) */
        int ready = 0;
        for (uint32_t i = 0; i < a1; i++) {
            uint32_t fd = 0; uint16_t ev = 0, rev = 0;
            uc_mem_read(g_uc, a0 + i * 8, &fd, 4);
            uc_mem_read(g_uc, a0 + i * 8 + 4, &ev, 2);
            if ((int)fd == PIPEFD_R) { if (g_pipe_w > g_pipe_r) rev |= 1; }  /* POLLIN */
            else if (ev & 4) rev |= 4;                                       /* POLLOUT */
            uc_mem_write(g_uc, a0 + i * 8 + 6, &rev, 2);
            if (rev) ready++;
        }
        if (ready) return ready;
        int tmo = (int)a2;
        if (tmo == 0) return 0;            /* non-blocking poll */
        /* nothing ready: sleep on the timeout (capped) so a polling idle thread (e.g.
           the LinuxThreads manager's 2s poll) doesn't spin and starve everyone. */
        double dur = (tmo < 0) ? 0.1 : (double)tmo / 1000.0;
        if (dur > 0.1) dur = 0.1;
        gwrite(UC_ARM_REG_R0, 0);
        g_th[g_cur].wake_deadline = host_now() + dur;
        g_th[g_cur].state = TH_SLEEPING;
        int j = sched_pick();
        if (j >= 0 && j != g_cur) sched_switch_to(j);
        return 0;
    }
    case 240: { /* futex(uaddr, op, val, ...) */
        int op = (int)(a1 & 0x7f);
        if (op == 0) {            /* FUTEX_WAIT: block iff *uaddr == val */
            uint32_t cur; uc_mem_read(g_uc, a0, &cur, 4);
            if (cur != a2) return -11 /*EAGAIN*/;
            gwrite(UC_ARM_REG_R0, 0);          /* wake returns 0 */
            g_th[g_cur].futex_addr = a0;
            block_current(BLK_FUTEX);
            return 0;                          /* ignored (g_switched) */
        }
        if (op == 1) {            /* FUTEX_WAKE: wake up to `val` waiters */
            int woke = 0;
            for (int i = 0; i < g_nth; i++)
                if (g_th[i].state == TH_BLOCKED && g_th[i].block == BLK_FUTEX
                    && g_th[i].futex_addr == a0 && woke < (int)a2) {
                    g_th[i].state = TH_RUN; woke++;
                }
            return woke;
        }
        return 0;
    }
    case 120: { /* clone(flags, child_stack, ptid, tls, ctid) — CLONE_VM thread */
        if (g_nth >= MAXTH) return -11 /*EAGAIN*/;
        int slot = g_nth++;
        if (!g_th[slot].ctx) uc_context_alloc(g_uc, &g_th[slot].ctx);
        g_th[slot].tid = g_next_tid++;
        g_th[slot].ppid = g_th[g_cur].tid;
        g_th[slot].state = TH_RUN; g_th[slot].block = BLK_NONE;
        g_th[slot].tls = (a0 & ME_CLONE_SETTLS) ? a3 : g_th[g_cur].tls;
        g_th[slot].ctid = (a0 & ME_CLONE_CHILD_CLEARTID) ? a4 : 0;
        g_th[slot].sig_blocked = g_th[g_cur].sig_blocked; g_th[slot].sig_pending = 0;
        /* child ctx = parent's regs, but sp=child_stack, r0=0, same PC (post-svc) */
        uint32_t s_sp = gread(UC_ARM_REG_SP), s_r0 = gread(UC_ARM_REG_R0);
        gwrite(UC_ARM_REG_SP, a1); gwrite(UC_ARM_REG_R0, 0);
        uc_context_save(g_uc, g_th[slot].ctx);
        gwrite(UC_ARM_REG_SP, s_sp); gwrite(UC_ARM_REG_R0, s_r0);
        if ((a0 & ME_CLONE_PARENT_SETTID) && a2) { uint32_t t = g_th[slot].tid; uc_mem_write(g_uc, a2, &t, 4); }
        if ((a0 & ME_CLONE_CHILD_SETTID) && a4) { uint32_t t = g_th[slot].tid; uc_mem_write(g_uc, a4, &t, 4); }
        if (g_trace) fprintf(stderr, "  [clone] tid=%d stack=%08x flags=%08x (nth=%d)\n",
                             g_th[slot].tid, a1, a0, g_nth);
        return g_th[slot].tid;     /* parent gets the new tid */
    }
    case 158: { /* sched_yield */
        int j = sched_pick();
        if (j != g_cur && j >= 0) { gwrite(UC_ARM_REG_R0, 0); sched_switch_to(j); }
        return 0;
    }
    case 29:    /* pause */
    case 72:    /* sigsuspend (old) */
    case 179: { /* rt_sigsuspend(mask, size) — block until a signal arrives (-> EINTR) */
        struct thread *t = &g_th[g_cur];
        t->susp_oldmask = t->sig_blocked; t->susp_active = 1;
        if (nr == 179 && a0) { uint64_t m = 0; uc_mem_read(g_uc, a0, &m, 8); t->sig_blocked = m; }
        else if (nr == 72) t->sig_blocked = a0;   /* old ABI: mask passed by value */
        /* if a deliverable signal is already pending, take it without blocking */
        gwrite(UC_ARM_REG_R0, (uint32_t)-4 /*EINTR*/);
        if (t->sig_pending & ~t->sig_blocked) { deliver_signals(); g_switched = 1; return 0; }
        block_current(BLK_SIG);
        return 0;
    }
    case 162: { /* nanosleep(req, rem) — yield to other threads (else a sleeping
                  main starves a loader worker); only really sleep if alone */
        if (g_fb_guest) present_active();  /* frame boundary: refresh screen */
        if (a1) { uint32_t z[2] = {0, 0}; uc_mem_write(g_uc, a1, z, 8); }
        gwrite(UC_ARM_REG_R0, 0);
        uint32_t ts[2] = {0, 0}; if (a0) uc_mem_read(g_uc, a0, ts, 8);
        double dur = (double)ts[0] + (double)ts[1] * 1e-9;
        if (dur > 0.1) dur = 0.1;          /* cap a single sleep */
        g_th[g_cur].wake_deadline = host_now() + dur;
        g_th[g_cur].state = TH_SLEEPING;   /* sched_pick runs others / real-sleeps to deadline */
        int j = sched_pick();
        if (j >= 0 && j != g_cur) sched_switch_to(j);
        return 0;
    }
    case 78: {  /* gettimeofday(tv, tz) */
        if (a0) { uint32_t z[2] = {0, 0}; uc_mem_write(g_uc, a0, z, 8); } return 0;
    }
    case 191: { /* ugetrlimit(res, rlim) -> cur=8MB max=inf */
        uint32_t rl[2] = {0x00800000u, 0xffffffffu};
        if (a1) uc_mem_write(g_uc, a1, rl, 8); return 0;
    }
    case 369:  return 0;  /* prlimit64 */
    case 106: { /* stat(path, buf) */
        char p[1024]; uc_mem_read(g_uc, a0, p, sizeof p); p[1023] = 0;
        struct stat s; if (stat(p, &s)) return -errno; fill_oabi_stat(a1, &s); return 0;
    }
    case 108: { /* fstat(fd, buf) */
        struct stat s; if (fstat((int)a0, &s)) return -errno; fill_oabi_stat(a1, &s); return 0;
    }
    case 195: case 196: case 197: { /* stat64 / lstat64 / fstat64 */
        struct stat s; int ok;
        if (nr == 197) ok = fstat((int)a0, &s);
        else { char p[1024]; uc_mem_read(g_uc, a0, p, sizeof p); p[1023] = 0;
               ok = (nr == 196) ? lstat(p, &s) : stat(p, &s); }
        if (ok) return -errno; fill_oabi_stat(a1, &s); return 0;  /* 88B: fits the game's buf */
    }
    case 146: { /* writev(fd, iov, cnt) */
        long tot = 0;
        for (uint32_t i = 0; i < a2; i++) {
            uint32_t io[2]; uc_mem_read(g_uc, a1 + i * 8, io, 8);
            if (!io[1]) continue;
            uint8_t *t = malloc(io[1]); uc_mem_read(g_uc, io[0], t, io[1]);
            long w = write((int)a0, t, io[1]); free(t);
            if (w > 0) tot += w;
        }
        return tot;
    }
    default:
        fprintf(stderr, "me_unicorn: UNIMPLEMENTED syscall %u (r0=%08x r1=%08x r2=%08x)\n",
                nr, a0, a1, a2);
        return -ENOSYS;
    }
}

static void intr_cb(uc_engine *uc, uint32_t intno, void *user) {
    (void)user;
    if (intno != 2) { /* not SWI */
        fprintf(stderr, "me_unicorn: intr %u (not SWI), stopping\n", intno);
        g_exit = 1; uc_emu_stop(uc); return;
    }
    /* EABI: `svc 0`, syscall nr in r7. OABI (legacy): `svc #(0x900000+nr)`,
       nr encoded in the instruction immediate. Detect via the SVC immediate. */
    uint32_t pc = gread(UC_ARM_REG_PC), insn = 0;
    uc_mem_read(g_uc, pc - 4, &insn, 4);
    uint32_t imm = insn & 0x00ffffffu;
    uint32_t nr = (imm == 0) ? gread(UC_ARM_REG_R7)
                : (imm >= 0x900000u) ? (imm - 0x900000u) : imm;
    long r = sys_dispatch(nr, gread(UC_ARM_REG_R0), gread(UC_ARM_REG_R1),
                          gread(UC_ARM_REG_R2), gread(UC_ARM_REG_R3),
                          gread(UC_ARM_REG_R4), gread(UC_ARM_REG_R5));
    if (g_switched) { g_switched = 0; return; } /* now in another thread; its R0 is set */
    if (!g_exit) gwrite(UC_ARM_REG_R0, (uint32_t)r);
}

/* On an unmapped access, log it and lazily map a page so we can see how far the
   binary gets (and what regions it expects). Real device regions get handled
   properly later; this is a diagnostic/forgiving fallback. */
static bool mem_invalid_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                           int size, int64_t value, void *user) {
    (void)value; (void)user;
    static int n = 0;
    uint32_t a = (uint32_t)addr;
    if (n++ < 60)
        fprintf(stderr, "  mem-fault type=%d @ %08x size=%d pc=%08x lr=%08x sp=%08x tid=%d\n",
                type, a, size, gread(UC_ARM_REG_PC), gread(UC_ARM_REG_LR),
                gread(UC_ARM_REG_SP), g_th[g_cur].tid);
    if (a < 0x10000 && n < 62) {   /* likely a bad pointer: dump regs to find its source */
        static const int rr[13] = {UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
            UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,
            UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12};
        fprintf(stderr, "   regs:");
        for (int i = 0; i < 13; i++) fprintf(stderr, " r%d=%08x", i, gread(rr[i]));
        fprintf(stderr, "\n");
    }
    uc_mem_map(uc, ALIGN_DN(a), PAGE, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
    return true;  /* retry the faulting access */
}

/* Preemption is driven by uc_emu_start's instruction-count limit in the main loop
   (a per-basic-block UC_HOOK_BLOCK callback was far too much overhead -> choppy). */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: me_unicorn <static-arm.elf> [args]\n"); return 1; }
    if (getenv("ME_TRACE")) g_trace = 1;

    uc_err e = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &g_uc);
    if (e) die("uc_open", e);

    /* kuser helper page: ARMv5 glibc reads TLS / does cmpxchg via fixed stubs at
       0xffff0f** (the kernel-provided user-helper page). */
    map_region(0xffff0000u, PAGE, UC_PROT_READ | UC_PROT_EXEC);
    { uint32_t mb = 0xe1a0f00eu;                       /* mov pc,lr */
      uc_mem_write(g_uc, 0xffff0fa0u, &mb, 4);         /* __kuser_memory_barrier */
      uint32_t cx[] = {0xe5923000u, 0xe0533000u, 0x05821000u, 0xe2730000u, 0xe1a0f00eu};
      uc_mem_write(g_uc, 0xffff0fc0u, cx, sizeof cx);  /* __kuser_cmpxchg */
      uint32_t gt[] = {0xe59f0008u, 0xe1a0f00eu};      /* __kuser_get_tls: ldr r0,[pc,#8]; mov pc,lr */
      uc_mem_write(g_uc, 0xffff0fe0u, gt, sizeof gt);
      uint32_t ver = 2; uc_mem_write(g_uc, 0xffff0ffcu, &ver, 4);
      /* signal restorer trampoline at SIG_TRAMP: mov r7,#173; svc 0 (rt_sigreturn) */
      uint32_t tramp[] = {0xe3a070adu, 0xef000000u};
      uc_mem_write(g_uc, 0xffff0f00u, tramp, sizeof tramp); }

    shm_setup();   /* framebuffer bridge to the viewer */

    uint32_t entry = load_elf(argv[1]);
    uint32_t sp = setup_stack(argc - 1, argv + 1);   /* guest argv = elf + its args */
    gwrite(UC_ARM_REG_SP, sp);

    /* register the main thread (slot 0, tid 1) */
    g_nth = 1; g_cur = 0;
    uc_context_alloc(g_uc, &g_th[0].ctx);
    g_th[0].tid = g_next_tid++;
    g_th[0].ppid = 1;
    g_th[0].state = TH_RUN;

    uc_hook h, hm;
    uc_hook_add(g_uc, &h, UC_HOOK_INTR, intr_cb, NULL, 1, 0);
    uc_hook_add(g_uc, &hm, UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED
                | UC_HOOK_MEM_FETCH_UNMAPPED, mem_invalid_cb, NULL, 1, 0);

    if (g_trace) fprintf(stderr, "entry=%08x sp=%08x brk=%08x\n", entry, sp, g_brk);
    /* Run in instruction-count time-slices. Blocking syscalls switch threads inline
       (g_switched); when a slice's count expires we round-robin to another runnable
       thread. The loop only exits on real process exit, so a stray uc_emu_start
       return can't freeze the engine. */
    uint32_t pc = entry;
    unsigned slice = 0; int errs = 0;
    const uint64_t SLICE = getenv("ME_SLICE") ? strtoull(getenv("ME_SLICE"), 0, 0) : 700000;
    while (!g_exit) {
        e = uc_emu_start(g_uc, pc, 0, 0, SLICE);
        if (g_exit) break;
        if (e != UC_ERR_OK) {
            if (errs < 30) fprintf(stderr, "me_unicorn: emu err %s pc=%08x\n",
                                   uc_strerror(e), gread(UC_ARM_REG_PC));
            if (++errs > 200) { fprintf(stderr, "me_unicorn: too many errors, stopping\n"); break; }
        } else if (errs) errs = 0;
        int j = sched_pick();                 /* time-slice / wake sleepers */
        if (j >= 0 && j != g_cur) sched_switch_to(j);
        g_switched = 0;
        if ((++slice & 3) == 0 && g_fb_guest) present_active();
        uint32_t cpsr = gread(UC_ARM_REG_CPSR);
        pc = gread(UC_ARM_REG_PC) | ((cpsr & 0x20) ? 1u : 0u);   /* keep Thumb bit */
    }
    uc_close(g_uc);
    return g_exit_code;
}
