/* magiceyes Unicorn engine — cooperative thread scheduler + signals.
 * REWRITE TARGET for native host threads (see NATIVE_THREADS.md). */

#include "engine.h"

struct thread g_th[MAXTH];
int g_nth = 0, g_cur = 0, g_next_tid = 100;  /* main != pid 1 (LinuxThreads orphan check) */
int g_switched = 0;   /* a syscall changed the running thread this trap */
void wake_sleepers(void) {
    double now = host_now();
    for (int i = 0; i < g_nth; i++)
        if (g_th[i].state == TH_SLEEPING && now >= g_th[i].wake_deadline)
            g_th[i].state = TH_RUN;
}
/* pick the next runnable thread (round-robin from g_cur), or -1 if none. When only
   sleeping threads remain, really sleep until the earliest deadline + wake it — this
   paces the game to real time (frame cap). */
int sched_pick(void) {
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
void sched_switch_to(int j) {
    if (j == g_cur) return;
    g_th[g_cur].last_pc = gread(UC_ARM_REG_PC);
    uc_context_save(g_uc, g_th[g_cur].ctx);
    uc_mem_read(g_uc, 0xffff0ff0u, &g_th[g_cur].tls, 4);
    g_cur = j;
    uc_mem_write(g_uc, 0xffff0ff0u, &g_th[g_cur].tls, 4);
    uc_context_restore(g_uc, g_th[g_cur].ctx);
    g_switched = 1;
    deliver_signals();   /* if the now-current thread has a pending signal, enter its handler */
}
/* block the current thread (caller already set its wake-time R0) and run next. */
void block_current(int reason) {
    if (g_trace) { static int n = 0; if (n++ < 400)
        fprintf(stderr, "  [block tid=%d reason=%d]\n", g_th[g_cur].tid, reason); }
    g_th[g_cur].state = TH_BLOCKED; g_th[g_cur].block = reason;
    g_th[g_cur].last_pc = gread(UC_ARM_REG_PC);
    int j = sched_pick();
    if (j < 0) {
        fprintf(stderr, "me_unicorn: all %d threads blocked (deadlock)\n", g_nth);
        g_exit = 1; uc_emu_stop(g_uc); return;
    }
    sched_switch_to(j);
}

/* Diagnostics: dump every thread's state/block/PC (ME_THREADDUMP). */
int g_threaddump = 0;
void dump_threads(const char *why) {
    static const char *sn[] = {"FREE","RUN","BLOCKED","SLEEPING","DEAD"};
    static const char *bn[] = {"-","FUTEX","SIG"};
    double now = host_now();
    fprintf(stderr, "== threads (%s) cur=%d nth=%d ==\n", why, g_cur, g_nth);
    for (int i = 0; i < g_nth; i++) {
        struct thread *t = &g_th[i];
        uint32_t pc = (i == g_cur) ? gread(UC_ARM_REG_PC) : t->last_pc;
        fprintf(stderr, "  [%d] tid=%d %-8s blk=%-5s pc=%08x futex=%08x sigP=%llx sigB=%llx wake=%+.2f\n",
                i, t->tid, sn[t->state & 7], bn[t->block % 3], pc, t->futex_addr,
                (unsigned long long)t->sig_pending, (unsigned long long)t->sig_blocked,
                t->state == TH_SLEEPING ? t->wake_deadline - now : 0.0);
    }
}

/* ---- signals (enough for the glibc LinuxThreads restart/cancel handshake) ---- */
struct sigact g_sigact[65];
/* r0..r12, sp, lr, pc, cpsr — the set saved/restored across a handler */
const int g_sregs[17] = {
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR };

/* If the current thread has a pending, unblocked signal with a handler, enter it.
   The pre-handler register state is saved so (rt_)sigreturn can resume it. */
void deliver_signals(void) {
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
long send_sig(int pid, int sig) {
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
