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
#define PIPEFD_R 2000
#define PIPEFD_W 2001
static uint8_t *g_pipebuf = NULL;
static uint32_t g_pipe_cap = 0, g_pipe_w = 0, g_pipe_r = 0;
static void pipe_put(const uint8_t *p, uint32_t n) {
    if (g_pipe_w + n > g_pipe_cap) {
        g_pipe_cap = (g_pipe_w + n) * 2 + 4096;
        g_pipebuf = realloc(g_pipebuf, g_pipe_cap);
    }
    memcpy(g_pipebuf + g_pipe_w, p, n); g_pipe_w += n;
}

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

/* ---- emulated GP2X/Wiz devices (fake fds, not passed to the host) ---- */
enum { DEV_FB = 1, DEV_MEM, DEV_GPIO, DEV_DSP, DEV_MIXER, DEV_TTY, DEV_OTHER };
#define DEVFD_BASE 1000
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
    if (g_devn >= 64) return -1;
    int fd = DEVFD_BASE + g_devn; g_devtype[g_devn++] = t;
    fprintf(stderr, "  DEV open %s -> fd=%d type=%d\n", path, fd, t);
    return fd;
}
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

#define GMAP_FIXED 0x10u
#define GMAP_ANON  0x20u
/* unified mmap for old_mmap(90) and mmap2(192); file-backed reads via pread. */
static long do_mmap(uint32_t addr, uint32_t len, uint32_t flags, int fd, uint64_t off) {
    uint32_t l = ALIGN_UP(len ? len : 1);
    uint32_t at;
    if ((flags & GMAP_FIXED) && addr) at = ALIGN_DN(addr);
    else { at = g_mmap_next; g_mmap_next += l; }
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
    if (type == DEV_FB && !g_fb_guest) g_fb_guest = at;   /* /dev/fb0 framebuffer */
    if (type == DEV_MEM) {
        record_memmap(phys, at, len);
        if (phys == 0xC0000000u) {
            g_mmsp2_guest = at;
            static uc_hook hh;
            uc_hook_add(g_uc, &hh, UC_HOOK_MEM_WRITE, mmsp2_write_cb, NULL,
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

static long sys_dispatch(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a3; (void)a4; (void)a5;
    if (g_trace)
        fprintf(stderr, "  sc %u (%08x,%08x,%08x,%08x)\n", nr, a0, a1, a2, a3);
    /* single-buffered titles never "flip"; refresh the live fb0 periodically */
    { static unsigned c = 0; if (g_fb_guest && (++c & 63) == 0) present_guest(g_fb_guest); }
    switch (nr) {
    case 1:    /* exit */
    case 248:  /* exit_group */
        if (g_forked) {  /* the synchronous child is done -> restore the parent */
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
        g_exit = 1; g_exit_code = a0; uc_emu_stop(g_uc); return 0;
    case 4: {  /* write(fd, buf, count) */
        uint8_t *tmp = malloc(a2 ? a2 : 1);
        uc_mem_read(g_uc, a1, tmp, a2);
        if ((int)a0 == PIPEFD_W) { pipe_put(tmp, a2); free(tmp); return a2; }
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
    case 91:   /* munmap */ return 0;
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
        if (t) fprintf(stderr, "  DEV ioctl fd=%d type=%d cmd=%08x arg=%08x\n",
                       (int)a0, t, a1, a2);
        return 0;
    }
    case 0xf0005: { /* __ARM_NR_set_tls -> kuser TLS slot */
        uc_mem_write(g_uc, 0xffff0ff0u, &a0, 4); return 0;
    }
    case 0xf0002: return 0; /* __ARM_NR_cacheflush */
    case 5: {  /* open(path, flags, mode) */
        char p[1024]; uc_mem_read(g_uc, a0, p, sizeof p); p[sizeof p - 1] = 0;
        int d = dev_open(p); if (d >= 0) return d;
        long r = open(p, (int)a1, a2); return r < 0 ? -errno : r;
    }
    case 322: { /* openat(dirfd, path, flags, mode) */
        char p[1024]; uc_mem_read(g_uc, a1, p, sizeof p); p[sizeof p - 1] = 0;
        int d = dev_open(p); if (d >= 0) return d;
        long r = open(p, (int)a2, a3); return r < 0 ? -errno : r;
    }
    case 6:    /* close */
        if ((int)a0 == PIPEFD_R || (int)a0 == PIPEFD_W) return 0;
        if (dev_type((int)a0)) return 0;
        return close((int)a0) < 0 ? -errno : 0;
    case 19: { long r = lseek((int)a0, (off_t)a1, (int)a2); return r < 0 ? -errno : r; } /* lseek */
    case 125:  return 0;  /* mprotect (we map RWX) */
    case 20:   return 1;  /* getpid */
    case 224:  return 1;  /* gettid */
    case 256:  return 1;  /* set_tid_address */
    case 338:  return 0;  /* set_robust_list */
    case 174:  return 0;  /* rt_sigaction */
    case 175:  return 0;  /* rt_sigprocmask */
    case 240:  return 0;  /* futex (single-thread: pretend ok) */
    case 162: { /* nanosleep(req, rem) — sleep a little so we don't busy-spin */
        uint32_t ts[2] = {0, 0}; if (a0) uc_mem_read(g_uc, a0, ts, 8);
        unsigned us = ts[0] * 1000000u + ts[1] / 1000u;
        if (us > 10000) us = 10000;        /* cap to stay responsive */
        if (us) usleep(us);
        if (a1) { uint32_t z[2] = {0, 0}; uc_mem_write(g_uc, a1, z, 8); }
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
    case 195: case 197: { /* stat64/fstat64: reuse OABI layout (good enough for size) */
        struct stat s; int ok;
        if (nr == 197) ok = fstat((int)a0, &s);
        else { char p[1024]; uc_mem_read(g_uc, a0, p, sizeof p); p[1023] = 0; ok = stat(p, &s); }
        if (ok) return -errno; fill_oabi_stat(a1, &s); return 0;
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
    if (n++ < 40)
        fprintf(stderr, "  mem-fault type=%d @ %08x size=%d\n", type, a, size);
    uc_mem_map(uc, ALIGN_DN(a), PAGE, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
    return true;  /* retry the faulting access */
}

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
      uint32_t ver = 2; uc_mem_write(g_uc, 0xffff0ffcu, &ver, 4); }

    shm_setup();   /* framebuffer bridge to the viewer */

    uint32_t entry = load_elf(argv[1]);
    uint32_t sp = setup_stack(argc - 1, argv + 1);   /* guest argv = elf + its args */
    gwrite(UC_ARM_REG_SP, sp);

    uc_hook h, hm;
    uc_hook_add(g_uc, &h, UC_HOOK_INTR, intr_cb, NULL, 1, 0);
    uc_hook_add(g_uc, &hm, UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED
                | UC_HOOK_MEM_FETCH_UNMAPPED, mem_invalid_cb, NULL, 1, 0);

    if (g_trace) fprintf(stderr, "entry=%08x sp=%08x brk=%08x\n", entry, sp, g_brk);
    e = uc_emu_start(g_uc, entry, 0, 0, 0);
    if (e && !g_exit)
        fprintf(stderr, "me_unicorn: emu stopped: %s (pc=%08x)\n",
                uc_strerror(e), gread(UC_ARM_REG_PC));
    uc_close(g_uc);
    return g_exit_code;
}
