/* magiceyes Unicorn engine — native host threads.
 *
 * Each guest thread runs on its own host thread with its own uc_engine over the shared
 * (host-backed, uc_mem_map_ptr) guest RAM. A big-engine lock serialises the syscall +
 * device layer; CPU execution (uc_emu_start) runs in parallel. Pre-ARMv6 GP2X has no
 * ldrex/strex, so the one atomic primitive is the kuser cmpxchg, done host-atomically
 * (see the 0xfff0 case in syscalls.c). This replaces the old cooperative scheduler. */
#include "engine.h"

pthread_mutex_t g_biglock = PTHREAD_MUTEX_INITIALIZER;   /* syscall + device layer lock */
__thread int g_holds_biglock = 0;    /* this thread holds g_biglock (for the crash guard) */
__thread uc_engine *g_uc;            /* the calling host thread's uc */
__thread struct thread *g_self;      /* the calling host thread's guest-thread record */

struct thread g_th[MAXTH];
int g_nth = 0, g_next_tid = 100;
int g_threaddump = 0;

/* defined below; forward-declared so thread_entry's crash path can wake a sigsuspended thread */
static pthread_mutex_t g_sigm;
static pthread_cond_t  g_sigc;

/* process-wide signal disposition (shared across threads) */
struct sigact g_sigact[65];
const int g_sregs[17] = {
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR };

/* ---- temporary: watch writes to up to 4 guest addresses (ME_WATCH=0xA,0xB,..) ---- */
static uint32_t g_watch[4]; static int g_nwatch = 0;
static void watch_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                     int64_t value, void *user) {
    (void)uc; (void)type; (void)size; (void)user;
    fprintf(stderr, "WATCH %08x <- %08llx tid=%d pc=%08x\n", (uint32_t)addr,
            (unsigned long long)value, g_self ? g_self->tid : -1, gread(UC_ARM_REG_PC));
}

/* ---- temporary: log when any thread first executes ME_PCHOOK=0xADDR ---- */
static uint32_t g_pchook = 0;
static void pchook_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)uc; (void)size; (void)user;
    static unsigned long n = 0;
    /* ME_PCHOOK_EQ: only fire when r0==r4 (the Lock() assert-fail condition); logs the
       caller + which thread owns the lock. Lets us catch just the failing re-entry. */
    if (getenv("ME_PCHOOK_EQ") && gread(UC_ARM_REG_R0) != gread(UC_ARM_REG_R4)) return;
    if (++n <= 12) {
        static const int rr[13] = {UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
            UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,
            UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12};
        char buf[256]; int o = snprintf(buf, sizeof buf, "PCHOOK %08x #%lu tid=%d lr=%08x sp=%08x",
            (uint32_t)addr, n, g_self ? g_self->tid : -1, gread(UC_ARM_REG_LR), gread(UC_ARM_REG_SP));
        for (int i = 0; i < 13; i++) o += snprintf(buf+o, sizeof buf-o, " r%d=%08x", i, gread(rr[i]));
        fprintf(stderr, "%s\n", buf);   /* single fprintf so threads don't interleave */
    }
}

/* ---- ME_LOOPPC: histogram of executed block PCs once frame_seq freezes, to pin a hang's loop body. */
#define NLOOPPC 512
static struct { uint32_t pc; unsigned long n; } g_looppc[NLOOPPC];
static int g_looppc_on = 0;
static uint32_t g_looppc_from = 0;
void looppc_dump(void) {
    if (!g_looppc_on) return;
    /* simple selection of the top 16 by count */
    fprintf(stderr, "== LOOPPC histogram (top block PCs while frozen) ==\n");
    for (int top = 0; top < 40; top++) {
        int best = -1; unsigned long bn = 0;
        for (int i = 0; i < NLOOPPC; i++) if (g_looppc[i].n > bn) { bn = g_looppc[i].n; best = i; }
        if (best < 0) break;
        fprintf(stderr, "  %08x  %lu\n", g_looppc[best].pc, g_looppc[best].n);
        g_looppc[best].n = 0;   /* consume so the next pass finds the next-highest */
    }
}
static void looppc_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)uc; (void)size; (void)user;
    extern gp2x_shm_t *g_shm;
    if (!g_shm || g_shm->frame_seq < g_looppc_from) return;
    uint32_t pc = (uint32_t)addr;
    int h = (pc >> 2) & (NLOOPPC - 1);
    for (int i = 0; i < NLOOPPC; i++) {
        int j = (h + i) & (NLOOPPC - 1);
        if (g_looppc[j].n == 0 || g_looppc[j].pc == pc) { g_looppc[j].pc = pc; g_looppc[j].n++; return; }
    }
}

/* ME_MUTEXWATCH=0xADDR: trace pthread_mutex_init/lock/unlock of one mutex + caller, to find a
   missing-unlock / double-lock. Addresses are the EABI libpthread's (base 0x40020000). */
static uint32_t g_mutexwatch = 0;
static void mutex_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)size; (void)user;
    if (gread(UC_ARM_REG_R0) != g_mutexwatch) return;
    uint32_t pc = (uint32_t)addr, sp = gread(UC_ARM_REG_SP);
    const char *op = pc == 0x40028624u ? "LOCK" : pc == 0x4002a1f4u ? "UNLOCK"
                   : pc == 0x40028930u ? "TRYLOCK" : pc == 0x40027e24u ? "INIT" : "?";
    char bt[160]; int o = 0;
    for (int k = 0; k < 120 && o < 130; k++) {   /* bl-validated guest return addresses */
        uint32_t w = 0; uc_mem_read(uc, sp + k * 4, &w, 4);
        if (!(w & 3) && w >= 0x8000 && w < 0x19bc00) {
            uint32_t ins = 0;
            if (uc_mem_read(uc, w - 4, &ins, 4) == UC_ERR_OK && (ins >> 24) == 0xeb)
                o += snprintf(bt + o, sizeof bt - o, " %08x", w);
        }
    }
    fprintf(stderr, "MUTEX %-6s %08x tid=%d lr=%08x bt:%s\n",
            op, g_mutexwatch, g_self ? g_self->tid : -1, gread(UC_ARM_REG_LR), bt);
}

/* ---- per-uc hooks + factory ------------------------------------------------ */
void uc_hook_std(uc_engine *u) {
    uc_hook h;
    uc_hook_add(u, &h, UC_HOOK_INTR, intr_cb, NULL, 1, 0);
    if (!g_mutexwatch) { const char *e = getenv("ME_MUTEXWATCH"); if (e) g_mutexwatch = strtoul(e, NULL, 0); }
    if (g_mutexwatch) for (uint32_t a = 0; a < 4; a++) {
        uint32_t pcs[] = {0x40027e24u, 0x40028624u, 0x40028930u, 0x4002a1f4u};
        uc_hook_add(u, &h, UC_HOOK_CODE, mutex_cb, NULL, pcs[a], pcs[a]);
    }
    uc_hook_add(u, &h, UC_HOOK_INSN_INVALID, fpa_invalid_cb, NULL, 1, 0);  /* FPA float emulation */
    if (!g_pchook) { const char *e = getenv("ME_PCHOOK"); if (e) g_pchook = strtoul(e, NULL, 0); }
    if (g_pchook) uc_hook_add(u, &h, UC_HOOK_CODE, pchook_cb, NULL, g_pchook, g_pchook);
    if (!g_looppc_on) { const char *e = getenv("ME_LOOPPC"); if (e) { g_looppc_on = 1; g_looppc_from = strtoul(e, NULL, 0); } }
    if (g_looppc_on) uc_hook_add(u, &h, UC_HOOK_BLOCK, looppc_cb, NULL, 1, 0);
    if (g_coop) uc_hook_add(u, &h, UC_HOOK_BLOCK, coop_block_cb, NULL, 1, 0);  /* single-core timeslice */
    uc_hook_add(u, &h, UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED
                | UC_HOOK_MEM_FETCH_UNMAPPED, mem_invalid_cb, NULL, 1, 0);
    if (!g_nwatch) { const char *e = getenv("ME_WATCH");
        for (char *s = e ? strdup(e) : NULL, *p = s ? strtok(s, ",") : NULL;
             p && g_nwatch < 4; p = strtok(NULL, ",")) g_watch[g_nwatch++] = strtoul(p, NULL, 0); }
    for (int i = 0; i < g_nwatch; i++)
        uc_hook_add(u, &h, UC_HOOK_MEM_WRITE, watch_cb, NULL, g_watch[i], g_watch[i] + 3);
    me_btrace_hook(u);   /* ME_DIDJ_BTRACE: trace libButton's button task on this uc (Wall 1) */
}

uc_engine *uc_new_thread(void) {
    uc_engine *u;
    if (uc_open(UC_ARCH_ARM, UC_MODE_ARM, &u)) die("uc_open(thread)", UC_ERR_OK);
    uc_map_all(u);          /* share all guest RAM (same host backing) */
    uc_hook_std(u);
    return u;
}

/* host thread body: run the guest thread to completion. */
void *thread_entry(void *arg) {
    struct thread *t = arg;
    g_self = t;
    g_uc = t->uc;
    uc_mem_write(g_uc, 0xffff0ff0u, &t->tls, 4);   /* this thread's kuser TLS slot */
    struct me_fault flt = {0};
    guarded_emu_start(g_uc, t->entry_pc, &flt);    /* run to completion (or catch a host fault) */
    if (flt.faulted) {       /* a worker host fault: flag the crash + tear the whole game down */
        fprintf(stderr, "magiceyes: GAME CRASHED (worker host fault) pc=%p addr=%p in %s\n",
                (void *)flt.pc, (void *)flt.addr, g_cur_game[0] ? g_cur_game : "(unknown)");
        g_fault_addr = flt.addr; g_fault_pending = 1;   /* viewer pops a MessageBox */
        /* We can't engine_stop_all_threads() here (it would join ourselves). Instead kick the
           main thread out of uc_emu_start so the main loop runs the standard teardown -> idle. */
        BIGLOCK_LOCK();
        g_exit = 1;
        if (g_th[0].uc) uc_emu_stop(g_th[0].uc);
        BIGLOCK_UNLOCK();
        futex_wake_all();
        pthread_mutex_lock(&g_sigm); pthread_cond_broadcast(&g_sigc); pthread_mutex_unlock(&g_sigm);
    }
    BIGLOCK_LOCK();
    t->state = TH_DEAD;
    if (t->ctid) {                                  /* CLONE_CHILD_CLEARTID: clear + wake */
        uint32_t z = 0; uc_mem_write(g_uc, t->ctid, &z, 4);
        futex_wake(t->ctid, INT_MAX);
    }
    BIGLOCK_UNLOCK();
    return NULL;
}

/* ---- futex: hashed table of wait queues ----------------------------------- */
#define NFXQ 128
static struct fxq { pthread_mutex_t m; pthread_cond_t c; } g_fxq[NFXQ];
static void fxq_init(void) {
    for (int i = 0; i < NFXQ; i++) {
        pthread_mutex_init(&g_fxq[i].m, NULL);
        pthread_cond_init(&g_fxq[i].c, NULL);
    }
}
static struct fxq *fxq_for(uint32_t a) { return &g_fxq[(a >> 2) % NFXQ]; }

/* FUTEX_WAIT: block iff *uaddr == val. Called holding g_biglock; releases it while
   blocked so other threads run, re-acquires on wake. `abstime` (CLOCK_REALTIME absolute, NULL =
   infinite) is the guest's timeout -- HONOURING it is essential: glibc's timed waits
   (pthread_cond_timedwait, adaptive/timed mutex) call FUTEX_WAIT[_BITSET] with a deadline and
   expect to wake and re-poll when it elapses. Ignoring it (always pthread_cond_wait) left a
   single-threaded guest blocked indefinitely on a timed wait -- it only progressed on stray
   hash-collision wakes, which is what made Rhythmos's GPAC movie init crawl for minutes.
   Returns 0 woken, -ETIMEDOUT on timeout, -EAGAIN if value mismatched. */
int futex_wait(uint32_t uaddr, uint32_t val, const struct timespec *abstime) {
    struct fxq *q = fxq_for(uaddr);
    pthread_mutex_lock(&q->m);
    uint32_t cur = 0; uc_mem_read(g_uc, uaddr, &cur, 4);
    if (cur != val) {                           /* EAGAIN: the value already changed, don't block */
        pthread_mutex_unlock(&q->m);
        /* Livelock backoff. uClibc's swp+futex mutex (the malloc lock) under TRUE-PARALLEL threading
           can livelock: two worker threads each swp the lock to 2, FUTEX_WAIT(val=2), the OTHER's
           swp/release flips the value so each sees cur!=2 -> EAGAIN -> the guest re-spins the swp
           instead of ever blocking, and neither wins. On a single-core device this can't happen
           (one thread runs at a time). Under cooperative scheduling (g_coop) we ARE single-core, so
           just hand the run token to the lock holder so it can finish + release. Otherwise (parallel
           path) fall back to the address-streak yield: after a run of consecutive EAGAINs on the
           same address (the livelock signature), drop g_biglock and sleep so the holder progresses. */
        if (g_coop) { runtok_yield(); return -11; }
        static __thread uint32_t last_addr; static __thread int streak;
        if (uaddr == last_addr) { if (++streak >= 48) { streak = 0;
            BIGLOCK_UNLOCK(); me_usleep(60); BIGLOCK_LOCK(); } }
        else { last_addr = uaddr; streak = 0; }
        return -11;
    }
    COOP_BLOCK_UNLOCK();                         /* let others run while we block (drop biglock+token) */
    int r = abstime ? pthread_cond_timedwait(&q->c, &q->m, abstime)
                    : pthread_cond_wait(&q->c, &q->m);   /* atomically releases q->m */
    pthread_mutex_unlock(&q->m);
    COOP_BLOCK_LOCK();
    return (r == ETIMEDOUT) ? -110 /* -ETIMEDOUT: guest re-checks + re-polls */ : 0;
}
int futex_wake(uint32_t uaddr, int n) {
    struct fxq *q = fxq_for(uaddr);
    pthread_mutex_lock(&q->m);
    /* ALWAYS broadcast, even for n==1. The wait queues are HASHED (NFXQ buckets), so several
       distinct futex addresses share one condvar. A cond_signal could wake a waiter on a DIFFERENT
       address (same bucket); it re-checks its own *uaddr, mismatches, and re-waits -- consuming the
       signal and leaving the intended waiter blocked forever (a lost wakeup). This bit the uClibc
       malloc lock: a mutex unlock's FUTEX_WAKE(n=1) sometimes failed to wake the real waiter, so two
       threads mallocing concurrently (audio decoder + module/button init) deadlocked ~50% of Didj
       boots. Broadcast wakes all waiters in the bucket; the wrong ones re-block, the right one
       proceeds. Spurious wakeups are permitted by the futex contract. */
    pthread_cond_broadcast(&q->c);
    pthread_mutex_unlock(&q->m);
    return n;
}
/* Broadcast every wait-queue: a teardown (reset/reload) must free threads blocked in
   futex_wait so they return, re-check g_exit, and fall out of uc_emu_start. */
void futex_wake_all(void) {
    for (int i = 0; i < NFXQ; i++) {
        pthread_mutex_lock(&g_fxq[i].m);
        pthread_cond_broadcast(&g_fxq[i].c);
        pthread_mutex_unlock(&g_fxq[i].m);
    }
}

/* ---- signals: per-thread sigsuspend wait + restart wake ------------------- */
static pthread_mutex_t g_sigm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_sigc = PTHREAD_COND_INITIALIZER;

/* deliver `sig` to the thread whose tid==pid; wake it if it's sigsuspended. */
static int g_siglog = -1;
long send_sig(int pid, int sig) {
    if (g_siglog < 0) g_siglog = getenv("ME_SIGLOG") ? 1 : 0;
    if (sig <= 0 || sig > 64) return 0;
    for (int i = 0; i < g_nth; i++) {
        if (g_th[i].tid != pid || g_th[i].state == TH_DEAD) continue;
        g_th[i].sig_pending |= (1ULL << (sig - 1));
        if (g_siglog) fprintf(stderr, "SIG send sig=%d -> tid=%d (from t%d) state=%d "
            "pend=%llx blk=%llx deliverable=%d\n", sig, pid, g_self ? g_self->tid : -1,
            g_th[i].state, (unsigned long long)g_th[i].sig_pending,
            (unsigned long long)g_th[i].sig_blocked,
            (int)!!(g_th[i].sig_pending & ~g_th[i].sig_blocked & (1ULL << (sig - 1))));
        pthread_mutex_lock(&g_sigm);
        pthread_cond_broadcast(&g_sigc);    /* wake any sigsuspended thread to re-check */
        pthread_mutex_unlock(&g_sigm);
        return 0;
    }
    if (g_siglog) fprintf(stderr, "SIG send sig=%d -> tid=%d (from t%d): NO MATCH/dead\n",
                          sig, pid, g_self ? g_self->tid : -1);
    return 0;
}

/* If the current thread has a pending, unblocked signal with a handler, enter it.
   Saves the pre-handler registers so (rt_)sigreturn can resume. */
void deliver_signals(void) {
    struct thread *t = g_self;
    if (g_siglog < 0) g_siglog = getenv("ME_SIGLOG") ? 1 : 0;
    if (g_siglog && t) fprintf(stderr, "SIG t%d deliver: has_sigsave=%d pend=%llx blk=%llx deliv=%llx\n",
        t->tid, t->has_sigsave, (unsigned long long)t->sig_pending,
        (unsigned long long)t->sig_blocked,
        (unsigned long long)(t->sig_pending & ~t->sig_blocked));
    if (!t || t->has_sigsave) return;
    uint64_t deliv = t->sig_pending & ~t->sig_blocked;
    if (!deliv) return;
    int sig = 0;
    for (int s = 1; s <= 64; s++) if (deliv & (1ULL << (s - 1))) { sig = s; break; }
    t->sig_pending &= ~(1ULL << (sig - 1));
    uint32_t h = g_sigact[sig].handler;
    if (g_siglog) fprintf(stderr, "SIG t%d deliver sig=%d handler=%08x%s\n",
        t->tid, sig, h, (h == 0 || h == 1) ? " -> DROP (SIG_DFL/IGN)" : "");
    if (h == 0 || h == 1) return;               /* SIG_DFL / SIG_IGN: drop */
    for (int i = 0; i < 17; i++) t->sigsave[i] = gread(g_sregs[i]);
    t->has_sigsave = 1;
    gwrite(UC_ARM_REG_R0, (uint32_t)sig);
    gwrite(UC_ARM_REG_LR, SIG_TRAMP);
    gwrite(UC_ARM_REG_PC, h);
    if (g_trace) fprintf(stderr, "  [signal %d -> handler %08x in tid %d]\n", sig, h, t->tid);
}

/* A blocking syscall handler (accept/read parking on a fake device) calls this each wait
   iteration: if a deliverable signal is now pending -- e.g. LinuxThreads' cancellation signal (32),
   sent by pthread_cancel via tgkill to interrupt a blocked thread -- enter its handler and report
   that the syscall was interrupted. Mirrors the kernel making a blocking call return EINTR with a
   signal pending: the guest's cancellation testpoint then unwinds + exits the thread, so a
   pthread_cancel+pthread_join task teardown (CKernelMPI) can't deadlock on a thread parked forever
   in our fake blocking call. Returns 1 if it dispatched a signal (caller must return immediately,
   leaving R0=EINTR saved + PC at the handler); 0 to keep waiting. */
int deliver_pending_signal(void) {
    struct thread *t = g_self;
    if (!t || !(t->sig_pending & ~t->sig_blocked) || t->has_sigsave) return 0;
    gwrite(UC_ARM_REG_R0, (uint32_t)-4 /*EINTR*/);
    deliver_signals();
    g_setpc = 1;
    return 1;
}

/* sigsuspend/pause: block (releasing g_biglock) until a deliverable signal arrives. */
void sigsuspend_wait(void) {
    if (g_siglog < 0) g_siglog = getenv("ME_SIGLOG") ? 1 : 0;
    if (g_siglog) fprintf(stderr, "SIG t%d sigsuspend_wait ENTER pend=%llx blk=%llx\n",
        g_self->tid, (unsigned long long)g_self->sig_pending,
        (unsigned long long)g_self->sig_blocked);
    pthread_mutex_lock(&g_sigm);
    COOP_BLOCK_UNLOCK();
    while (!(g_self->sig_pending & ~g_self->sig_blocked) && !g_exit)
        pthread_cond_wait(&g_sigc, &g_sigm);   /* g_exit: a teardown woke us to bail out */
    pthread_mutex_unlock(&g_sigm);
    COOP_BLOCK_LOCK();
    if (g_siglog) fprintf(stderr, "SIG t%d sigsuspend_wait WAKE pend=%llx\n",
        g_self->tid, (unsigned long long)g_self->sig_pending);
}

void threads_init(void) { fxq_init(); }

/* Stop + join every guest WORKER thread (i>=1). Called on the engine main thread while NOT
   holding g_biglock, by the reset/reload primitive. CPU-bound workers are forced out of
   uc_emu_start by uc_emu_stop; blocked ones are freed by the futex/sig broadcasts and then
   bail on g_exit. The main thread (g_th[0]) is the caller and has already left its own
   uc_emu_start, so it is neither stopped nor joined here. */
void engine_stop_all_threads(void) {
    BIGLOCK_LOCK();
    g_exit = 1;
    for (int i = 1; i < g_nth; i++)
        if (g_th[i].uc && g_th[i].state != TH_DEAD) uc_emu_stop(g_th[i].uc);
    BIGLOCK_UNLOCK();
    futex_wake_all();
    pthread_mutex_lock(&g_sigm); pthread_cond_broadcast(&g_sigc); pthread_mutex_unlock(&g_sigm);
    /* TWO passes, deliberately: (1) JOIN every worker, THEN (2) close every uc. uc_close mutates
       process-global qemu state (address-space teardown, etc.); doing it interleaved -- close
       worker i while workers i+1.. are STILL EXECUTING TCG (not yet joined) -- raced that global
       state against their memory accesses and corrupted a uc's internal pointers, which then
       crashed in a later free() inside arm_release. After pass 1 NO guest thread is running, so
       pass 2's closes touch no live state. (The multi-reload hard-crash: gp2x-static-titles-and-
       reload-crash.) */
    for (int i = 1; i < g_nth; i++)
        if (g_th[i].th) { pthread_join(g_th[i].th, NULL); g_th[i].th = 0; }
    /* Close each DISTINCT worker uc exactly once. If a uc pointer is aliased across slots (two
       g_th[] entries, or a worker slot equal to the main g_th[0].uc), closing it twice double-frees
       its TCGContext -- a later uc_close/arm_release then walks freed memory and faults in
       qht_destroy (the multi-reload hard-crash, gp2x-static-titles-and-reload-crash; confirmed
       under PageHeap+cdb as a UAF of the malloc'd tcg_ctx). Dedup here, and clear g_th[0].uc too
       if it aliases a worker so the main-uc close skips it.
       Close UNDER g_biglock: engine_request_reload (viewer/File->Open) scans g_th[].uc calling
       uc_emu_stop under g_biglock, so a reload request mid-teardown must not see a uc being closed. */
    uc_engine *closed[MAXTH]; int nclosed = 0;
    for (int i = 1; i < g_nth; i++) {
        uc_engine *u = g_th[i].uc;
        if (!u) continue;
        g_th[i].uc = NULL;
        if (g_th[0].uc == u) g_th[0].uc = NULL;          /* main aliases this worker -> skip later */
        int dup = 0; for (int j = 0; j < nclosed; j++) if (closed[j] == u) { dup = 1; break; }
        if (dup) continue;                                /* already closed this round */
        closed[nclosed++] = u;
        BIGLOCK_LOCK();
        uc_close(u);
        BIGLOCK_UNLOCK();
    }
}

/* Viewer thread (File->Open): request an in-process hot reload of host_path. Records the
   target, flags the bail, and kicks EVERY uc (incl. the main thread's) out of uc_emu_start so
   the main loop picks up g_reload_path and runs the reset+load. uc_emu_stop is thread-safe;
   g_biglock serialises against a syscall in flight. */
void engine_request_reload(const char *host_path) {
    if (!host_path || !*host_path) return;
    snprintf(g_reload_path, sizeof g_reload_path, "%s", host_path);
    g_reload_chdir = 1;   /* a new game from the picker -> run from its directory */
    BIGLOCK_LOCK();
    g_exit = 1;
    for (int i = 0; i < g_nth; i++) if (g_th[i].uc) uc_emu_stop(g_th[i].uc);
    BIGLOCK_UNLOCK();
    futex_wake_all();
    pthread_mutex_lock(&g_sigm); pthread_cond_broadcast(&g_sigc); pthread_mutex_unlock(&g_sigm);
}

/* Like engine_request_reload, but called from WITHIN a guest syscall (g_biglock already held, so
   it must NOT re-lock it). Kicks EVERY uc out -- not just the caller's -- because a chain-load
   (execve) can be issued from a worker thread (gp2xmenu launches the selected game off its main
   thread); stopping only the caller left the main thread spinning and the reload never ran. */
void engine_reload_in_syscall(const char *host_path) {
    if (!host_path || !*host_path) return;
    snprintf(g_reload_path, sizeof g_reload_path, "%s", host_path);
    g_reload_chdir = 1;
    g_exit = 1;
    for (int i = 0; i < g_nth; i++) if (g_th[i].uc) uc_emu_stop(g_th[i].uc);
    futex_wake_all();
    pthread_mutex_lock(&g_sigm); pthread_cond_broadcast(&g_sigc); pthread_mutex_unlock(&g_sigm);
}

/* allocate a thread slot; caller fills it + pthread_create. Returns index or -1. */
int thread_alloc(void) {
    for (int i = 0; i < g_nth; i++) if (g_th[i].state == TH_DEAD && !g_th[i].th) return i;
    if (g_nth >= MAXTH) return -1;
    return g_nth++;
}

/* ---- diagnostics ---------------------------------------------------------- */
void dump_threads(const char *why) {
    static const char *sn[] = {"FREE","RUN","BLOCKED","SLEEPING","DEAD"};
    fprintf(stderr, "== threads (%s) nth=%d ==\n", why, g_nth);
    for (int i = 0; i < g_nth; i++) {
        struct thread *t = &g_th[i];
        uint32_t pc = 0, lr = 0, sp = 0;
        uint32_t r[5] = {0,0,0,0,0};
        if (t->uc) {
            uc_reg_read(t->uc, UC_ARM_REG_PC, &pc); uc_reg_read(t->uc, UC_ARM_REG_LR, &lr);
            uc_reg_read(t->uc, UC_ARM_REG_SP, &sp);
            uc_reg_read(t->uc, UC_ARM_REG_R0, &r[0]); uc_reg_read(t->uc, UC_ARM_REG_R1, &r[1]);
            uc_reg_read(t->uc, UC_ARM_REG_R2, &r[2]); uc_reg_read(t->uc, UC_ARM_REG_R3, &r[3]);
            uc_reg_read(t->uc, UC_ARM_REG_R4, &r[4]);
        }
        fprintf(stderr, "  [%d] tid=%d %-8s livePC=%08x lr=%08x lastSC=%08x sigP=%llx sigB=%llx\n"
                        "      r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x sp=%08x\n      bt:",
                i, t->tid, sn[t->state & 7], pc, lr, t->last_pc,
                (unsigned long long)t->sig_pending, (unsigned long long)t->sig_blocked,
                r[0], r[1], r[2], r[3], r[4], sp);
        for (int k = 0; t->uc && k < 160; k++) {           /* scan stack for .text ret-addrs */
            uint32_t w = 0; uc_mem_read(t->uc, sp + k * 4, &w, 4);
            if (w >= 0x8100 && w < 0x19bc00) fprintf(stderr, " %08x", w);
        }
        /* PRECISE call chain: a stack word W is a real ARM return address only if the instruction
           at W-4 is a bl (0xeb......) or blx-imm (0xfa/0xfb......). Filters out data that merely
           looks like a .text address. Scans both the main binary and the loaded libs. */
        fprintf(stderr, "\n      ra:");
        for (int k = 0; t->uc && k < 256; k++) {
            uint32_t w = 0; uc_mem_read(t->uc, sp + k * 4, &w, 4);
            if (!(w & 3) && w >= 0x8000 && w < 0x71000000) {   /* word-aligned, in code arena */
                uint32_t insn = 0;
                if (uc_mem_read(t->uc, w - 4, &insn, 4) == UC_ERR_OK) {
                    uint32_t top = insn >> 24;
                    if (top == 0xeb || top == 0xfa || top == 0xfb)   /* bl / blx imm */
                        fprintf(stderr, " %08x", w);
                }
            }
        }
        fprintf(stderr, "\n");
    }
    looppc_dump();   /* ME_LOOPPC: hot block PCs since the last dump (the frozen loop body) */
}
