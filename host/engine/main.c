/* magiceyes Unicorn engine — main(), run loop, and core CPU/engine state.
 * Split into focused modules under host/engine/ (elf, mem, devices, syscalls,
 * threads, cpu); the shared contract is engine.h. */
#include "engine.h"

uc_engine *g_uc;
uint32_t g_brk, g_brk_start;
uint32_t g_mmap_next = MMAP_BASE;
int g_exit = 0, g_exit_code = 0;
int g_trace = 0;

/* Synchronous fork: snapshot the process, let the child run in-line until it
   exits (its pipe writes are captured along the way), then restore the parent
   and resume it with fork()==child_pid. Handles the common "fork a loader, pipe
   results back to the parent" pattern (e.g. Payback) without nested emulation:
   the child's exit_group restores the saved CPU context (PC -> parent's post-fork
   site) and execution simply continues as the parent. */
uc_context *g_fork_ctx = NULL;
struct snap g_snap[2048];
int g_nsnap = 0, g_forked = 0;
uint32_t g_child_pid = 0x1234;

/* in-engine pipe (parent <-> forked child); one pair is enough for the loaders
   seen so far. Backed by a growable host buffer that survives the fork restore. */
/* far above any real host fd so they never alias a file descriptor */
uint8_t *g_pipebuf = NULL;
uint32_t g_pipe_cap = 0, g_pipe_w = 0, g_pipe_r = 0;
void pipe_put(const uint8_t *p, uint32_t n) {
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
unsigned long g_n_rd = 0, g_n_wr = 0, g_n_fault = 0;  /* hook-call profiling */

/* CLONE_* flags we care about */

void die(const char *m, uc_err e) {
    fprintf(stderr, "me_unicorn: %s: %s\n", m, e ? uc_strerror(e) : "");
    exit(1);
}

/* map [addr,addr+size) page-aligned (idempotent-ish; ignores already-mapped). */
void map_region(uint32_t addr, uint32_t size, uint32_t perms) {
    uint32_t a = ALIGN_DN(addr), end = ALIGN_UP(addr + size);
    uc_err e = uc_mem_map(g_uc, a, end - a, perms);
    if (e && e != UC_ERR_MAP) die("uc_mem_map", e);
}

/* ---- syscalls (ARM EABI numbers) ---- */
uint32_t gread(uint32_t reg) { uint32_t v; uc_reg_read(g_uc, reg, &v); return v; }
void gwrite(uint32_t reg, uint32_t v) { uc_reg_write(g_uc, reg, &v); }

void deliver_signals(void);  /* defined below; runs a pending handler */

/* wake any TH_SLEEPING thread whose nanosleep deadline has passed. */
void intr_cb(uc_engine *uc, uint32_t intno, void *user) {
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
volatile int g_timer_run = 1;
unsigned g_slice_us = 6000;
void *timer_thread(void *arg) {
    (void)arg;
    while (g_timer_run) { usleep(g_slice_us); if (!g_exit) uc_emu_stop(g_uc); }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: me_unicorn <static-arm.elf> [args]\n"); return 1; }
    if (getenv("ME_TRACE")) g_trace = 1;
    if (getenv("ME_THREADDUMP")) g_threaddump = 1;

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
    /* Time-slice via a host timer (uc_emu_stop) so block chaining stays enabled.
       Blocking syscalls switch threads inline; on each slice we round-robin. The loop
       only exits on real process exit, so a stray uc_emu_start return can't freeze it. */
    if (getenv("ME_SLICE_US")) g_slice_us = (unsigned)strtoul(getenv("ME_SLICE_US"), 0, 0);
    uint32_t pc = entry;
    unsigned slice = 0; int errs = 0;
    /* count-based time-slice. A host-timer/timeout slice would keep Unicorn's block
       chaining (faster) but cross-thread uc_emu_stop crashes this Unicorn and the
       timeout slice starves the menu; the count slice is the stable choice (~21 MIPS,
       ~6fps for this heavy animated menu — Unicorn speed is the ceiling here). */
    const uint64_t SLICE = getenv("ME_SLICE") ? strtoull(getenv("ME_SLICE"), 0, 0) : 2000000;
    const uint64_t TMO = 0; (void)g_slice_us; (void)timer_thread;
    int prof = getenv("ME_PROF") ? 1 : 0; double prof_t = 0; unsigned prof_s = 0; uint32_t prof_fs = 0;
    while (!g_exit) {
        e = uc_emu_start(g_uc, pc, 0, TMO, SLICE);
        if (g_exit) break;
        if (prof) { double now = host_now(); if (!prof_t) prof_t = now; prof_s++;
            if (now - prof_t >= 2.0) { double dt = now - prof_t;
                uint32_t fs = g_shm ? g_shm->frame_seq : 0;
                fprintf(stderr, "PROF: %.1f fps  %.0f slices/s  mmsp2_rd=%.0f/s wr=%.0f/s fault=%.0f/s\n",
                    (fs - prof_fs) / dt, prof_s / dt, g_n_rd / dt, g_n_wr / dt, g_n_fault / dt);
                prof_fs = fs;
                prof_t = now; prof_s = 0; g_n_rd = g_n_wr = g_n_fault = 0; } }
        if (g_threaddump) { static double tdp = 0; double tn = host_now();
            if (tn - tdp >= 2.0) { tdp = tn; dump_threads("periodic"); } }
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
