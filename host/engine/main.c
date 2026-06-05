/* magiceyes Unicorn engine — main(), the main guest thread, and the host helper thread.
 * Native-threads model: the main thread runs its uc to completion; cloned guest threads
 * run on their own host threads (threads.c); a helper thread presents the framebuffer.
 * g_uc/g_self/g_th/g_biglock live in threads.c. */
#include "engine.h"

uint32_t g_brk, g_brk_start;
uint32_t g_mmap_next = MMAP_BASE;
int g_exit = 0, g_exit_code = 0;
int g_trace = 0;
__thread int g_setpc = 0;   /* a syscall set PC (signal entry / sigreturn): skip R0 write */

/* synchronous fork state (system()/popen child; see syscalls.c) */
uc_context *g_fork_ctx = NULL;
struct snap g_snap[2048];
int g_nsnap = 0, g_forked = 0;
uint32_t g_child_pid = 0x1234;

/* in-engine pipe (parent <-> forked child) */
uint8_t *g_pipebuf = NULL;
uint32_t g_pipe_cap = 0, g_pipe_w = 0, g_pipe_r = 0;
void pipe_put(const uint8_t *p, uint32_t n) {
    if (g_pipe_w + n > g_pipe_cap) {
        g_pipe_cap = (g_pipe_w + n) * 2 + 4096;
        g_pipebuf = realloc(g_pipebuf, g_pipe_cap);
    }
    memcpy(g_pipebuf + g_pipe_w, p, n); g_pipe_w += n;
}

unsigned long g_n_rd = 0, g_n_wr = 0, g_n_fault = 0;  /* hook-call profiling */

void die(const char *m, uc_err e) {
    fprintf(stderr, "me_unicorn: %s: %s\n", m, e ? uc_strerror(e) : "");
    exit(1);
}

uint32_t gread(uint32_t reg) { uint32_t v; uc_reg_read(g_uc, reg, &v); return v; }
void gwrite(uint32_t reg, uint32_t v) { uc_reg_write(g_uc, reg, &v); }

/* SVC handler: serialise the syscall/device layer under the big-engine lock. */
void intr_cb(uc_engine *uc, uint32_t intno, void *user) {
    (void)user;
    if (intno != 2) { fprintf(stderr, "me_unicorn: intr %u (not SWI)\n", intno); return; }
    uint32_t pc = gread(UC_ARM_REG_PC), insn = 0;
    uc_mem_read(uc, pc - 4, &insn, 4);
    uint32_t imm = insn & 0x00ffffffu;
    uint32_t nr = (imm == 0) ? gread(UC_ARM_REG_R7)
                : (imm >= 0x900000u) ? (imm - 0x900000u) : imm;
    if (g_self) g_self->last_pc = pc;   /* diagnostics: where this thread last syscalled */
    pthread_mutex_lock(&g_biglock);
    g_setpc = 0;
    long r = sys_dispatch(nr, gread(UC_ARM_REG_R0), gread(UC_ARM_REG_R1),
                          gread(UC_ARM_REG_R2), gread(UC_ARM_REG_R3),
                          gread(UC_ARM_REG_R4), gread(UC_ARM_REG_R5));
    if (!g_setpc && !g_exit) gwrite(UC_ARM_REG_R0, (uint32_t)r);
    pthread_mutex_unlock(&g_biglock);
    if (g_exit) uc_emu_stop(uc);
}

/* host helper thread: present the framebuffer + prof + thread dump, off the guest CPUs. */
static void *helper_thread(void *arg) {
    (void)arg;
    int prof = getenv("ME_PROF") ? 1 : 0;
    double prof_t = 0, tdp = 0; uint32_t prof_fs = 0;
    const char *ac = getenv("ME_AUDIOCLEAR");   /* TEMP: simulate audio-DMA completion by
                                                   periodically clearing a guest "DMA busy" flag */
    uint32_t acaddr = ac ? (uint32_t)strtoul(ac, NULL, 0) : 0;
    while (!g_exit) {
        usleep(16000);
        if (g_fb_guest) present_active();    /* reads the fb via host backing (guest_to_host) */
        if (acaddr) { uint32_t *p = guest_to_host(acaddr); if (p) *p = 0; }
        double now = host_now();
        if (prof && now - prof_t >= 2.0) {
            double dt = now - prof_t; uint32_t fs = g_shm ? g_shm->frame_seq : 0;
            fprintf(stderr, "PROF: %.1f fps  mmsp2_rd=%.0f/s wr=%.0f/s fault=%.0f/s\n",
                    (fs - prof_fs) / dt, g_n_rd / dt, g_n_wr / dt, g_n_fault / dt);
            prof_fs = fs; prof_t = now; g_n_rd = g_n_wr = g_n_fault = 0;
        }
        if (g_threaddump && now - tdp >= 2.0) { tdp = now; dump_threads("periodic"); }
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: me_unicorn <static-arm.elf> [args]\n"); return 1; }
    if (getenv("ME_TRACE")) g_trace = 1;
    if (getenv("ME_THREADDUMP")) g_threaddump = 1;
    threads_init();

    uc_engine *u;
    uc_err e = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &u);
    if (e) die("uc_open", e);
    g_uc = u;   /* this (main) host thread's uc */

    /* kuser helper page (ARMv5 has no HW TLS/atomics). cmpxchg traps to a magic SVC so
       it can be done host-atomically across native threads (see syscalls.c case 0xfff0). */
    map_region(0xffff0000u, PAGE, UC_PROT_READ | UC_PROT_EXEC);
    { uint32_t mb = 0xe1a0f00eu;                       /* mov pc,lr (memory_barrier) */
      uc_mem_write(u, 0xffff0fa0u, &mb, 4);
      uint32_t cx[] = {0xef90fff0u, 0xe1a0f00eu};      /* svc #0x90fff0 ; mov pc,lr */
      uc_mem_write(u, 0xffff0fc0u, cx, sizeof cx);
      uint32_t gt[] = {0xe59f0008u, 0xe1a0f00eu};      /* get_tls: ldr r0,[pc,#8]; mov pc,lr */
      uc_mem_write(u, 0xffff0fe0u, gt, sizeof gt);
      uint32_t ver = 2; uc_mem_write(u, 0xffff0ffcu, &ver, 4);
      uint32_t tramp[] = {0xe3a070adu, 0xef000000u};   /* SIG_TRAMP: mov r7,#173; svc 0 */
      uc_mem_write(u, 0xffff0f00u, tramp, sizeof tramp); }

    shm_setup();
    uint32_t entry = load_elf(argv[1]);
    uint32_t sp = setup_stack(argc - 1, argv + 1);
    gwrite(UC_ARM_REG_SP, sp);

    g_th[0].uc = u; g_th[0].th = 0; g_th[0].tid = g_next_tid++; g_th[0].ppid = 1;
    g_th[0].state = TH_RUN; g_self = &g_th[0]; g_nth = 1;
    uc_hook_std(u);

    if (g_trace) fprintf(stderr, "entry=%08x sp=%08x brk=%08x\n", entry, sp, g_brk);
    pthread_t helper; pthread_create(&helper, NULL, helper_thread, NULL);

    e = uc_emu_start(u, entry, 0, 0, 0);   /* run the main guest thread to completion */
    if (e != UC_ERR_OK && !g_exit)
        fprintf(stderr, "me_unicorn: main emu err %s pc=%08x\n", uc_strerror(e), gread(UC_ARM_REG_PC));
    g_exit = 1;
    return g_exit_code;
}
