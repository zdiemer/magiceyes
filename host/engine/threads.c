/* magiceyes Unicorn engine — native host threads.
 *
 * Each guest thread runs on its own host thread with its own uc_engine over the shared
 * (host-backed, uc_mem_map_ptr) guest RAM. A big-engine lock serialises the syscall +
 * device layer; CPU execution (uc_emu_start) runs in parallel. Pre-ARMv6 GP2X has no
 * ldrex/strex, so the one atomic primitive is the kuser cmpxchg, done host-atomically
 * (see the 0xfff0 case in syscalls.c). This replaces the old cooperative scheduler. */
#include "engine.h"

pthread_mutex_t g_biglock = PTHREAD_MUTEX_INITIALIZER;   /* syscall + device layer lock */
__thread uc_engine *g_uc;            /* the calling host thread's uc */
__thread struct thread *g_self;      /* the calling host thread's guest-thread record */

struct thread g_th[MAXTH];
int g_nth = 0, g_next_tid = 100;
int g_threaddump = 0;

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

/* ---- per-uc hooks + factory ------------------------------------------------ */
void uc_hook_std(uc_engine *u) {
    uc_hook h;
    uc_hook_add(u, &h, UC_HOOK_INTR, intr_cb, NULL, 1, 0);
    uc_hook_add(u, &h, UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED
                | UC_HOOK_MEM_FETCH_UNMAPPED, mem_invalid_cb, NULL, 1, 0);
    if (!g_nwatch) { const char *e = getenv("ME_WATCH");
        for (char *s = e ? strdup(e) : NULL, *p = s ? strtok(s, ",") : NULL;
             p && g_nwatch < 4; p = strtok(NULL, ",")) g_watch[g_nwatch++] = strtoul(p, NULL, 0); }
    for (int i = 0; i < g_nwatch; i++)
        uc_hook_add(u, &h, UC_HOOK_MEM_WRITE, watch_cb, NULL, g_watch[i], g_watch[i] + 3);
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
    uc_emu_start(g_uc, t->entry_pc, 0, 0, 0);
    pthread_mutex_lock(&g_biglock);
    t->state = TH_DEAD;
    if (t->ctid) {                                  /* CLONE_CHILD_CLEARTID: clear + wake */
        uint32_t z = 0; uc_mem_write(g_uc, t->ctid, &z, 4);
        futex_wake(t->ctid, INT_MAX);
    }
    pthread_mutex_unlock(&g_biglock);
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
   blocked so other threads run, re-acquires on wake. Returns 0 woken, -EAGAIN if value
   mismatched. */
int futex_wait(uint32_t uaddr, uint32_t val) {
    struct fxq *q = fxq_for(uaddr);
    pthread_mutex_lock(&q->m);
    uint32_t cur = 0; uc_mem_read(g_uc, uaddr, &cur, 4);
    if (cur != val) { pthread_mutex_unlock(&q->m); return -11; /* EAGAIN */ }
    pthread_mutex_unlock(&g_biglock);          /* let others run while we block */
    pthread_cond_wait(&q->c, &q->m);           /* atomically releases q->m */
    pthread_mutex_unlock(&q->m);
    pthread_mutex_lock(&g_biglock);
    return 0;
}
int futex_wake(uint32_t uaddr, int n) {
    struct fxq *q = fxq_for(uaddr);
    pthread_mutex_lock(&q->m);
    if (n <= 1) pthread_cond_signal(&q->c); else pthread_cond_broadcast(&q->c);
    pthread_mutex_unlock(&q->m);
    return n;
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

/* sigsuspend/pause: block (releasing g_biglock) until a deliverable signal arrives. */
void sigsuspend_wait(void) {
    if (g_siglog < 0) g_siglog = getenv("ME_SIGLOG") ? 1 : 0;
    if (g_siglog) fprintf(stderr, "SIG t%d sigsuspend_wait ENTER pend=%llx blk=%llx\n",
        g_self->tid, (unsigned long long)g_self->sig_pending,
        (unsigned long long)g_self->sig_blocked);
    pthread_mutex_lock(&g_sigm);
    pthread_mutex_unlock(&g_biglock);
    while (!(g_self->sig_pending & ~g_self->sig_blocked))
        pthread_cond_wait(&g_sigc, &g_sigm);
    pthread_mutex_unlock(&g_sigm);
    pthread_mutex_lock(&g_biglock);
    if (g_siglog) fprintf(stderr, "SIG t%d sigsuspend_wait WAKE pend=%llx\n",
        g_self->tid, (unsigned long long)g_self->sig_pending);
}

void threads_init(void) { fxq_init(); }

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
        fprintf(stderr, "\n");
    }
}
