/* Debugger core: stop-the-world pause, single-step, breakpoints, watchpoints. See dbg.h. */
#include "engine.h"
#include "dbg.h"

#define MAXBP 32
#define MAXWP 8

enum { DTH_TCG = 0, DTH_SYSCALL, DTH_PARKED,
       DTH_WAIT };   /* off-CPU in an indefinite syscall wait: quiesced, but not park-pointed */

volatile int g_dbg_armed = 0;

/* g_dbg_lock sits OUTSIDE g_present_lock/g_biglock/g_reg_lock and is never held while acquiring
   any of them, so it cannot participate in the existing lock order. */
static pthread_mutex_t g_dbg_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_dbg_cv   = PTHREAD_COND_INITIALIZER;   /* parked threads wait here */
static pthread_cond_t  g_dbg_ack  = PTHREAD_COND_INITIALIZER;   /* dbg_pause waits here */

static volatile int g_dbg_stop = 0;        /* stop-the-world requested */
static int g_state[MAXTH];                 /* DTH_* per thread slot */
static int g_step[MAXTH];                  /* pending single-steps per thread slot */
static struct dbg_stop g_last;

struct bp { int id; uint32_t addr; int used; };
struct wp { int id; uint32_t addr, len; int used; };
static struct bp g_bp[MAXBP];
static struct wp g_wp[MAXWP];
static int g_next_id = 1;
static int g_epoch = 0;                    /* bumped when the breakpoint set changes */

/* Per-uc hook handles. uc_hook_std discarded its handles, so nothing could ever be removed; this
   is the registry that makes add/remove possible. Indexed in parallel with g_th[]. */
struct ucslot {
    uc_engine *uc;
    int epoch;
    uc_hook bph[MAXBP];  int bpid[MAXBP];  uint32_t bpaddr[MAXBP];
    uc_hook wph[MAXWP];  int wpid[MAXWP];
};
static struct ucslot g_slot[MAXTH + 2];

static int slot_idx(uc_engine *uc) {
    for (int i = 0; i < (int)(sizeof g_slot / sizeof g_slot[0]); i++)
        if (g_slot[i].uc == uc) return i;
    for (int i = 0; i < (int)(sizeof g_slot / sizeof g_slot[0]); i++)
        if (!g_slot[i].uc) { g_slot[i].uc = uc; g_slot[i].epoch = -1; return i; }
    return -1;
}

static int self_idx(void) {
    if (!g_self) return -1;
    int i = (int)(g_self - g_th);
    return (i >= 0 && i < MAXTH) ? i : -1;
}

int dbg_stop_pending(void) { return g_dbg_stop; }
int dbg_is_paused(void)    { return g_dbg_stop; }

void dbg_enter_tcg(void) { int i = self_idx(); if (i >= 0) g_state[i] = DTH_TCG; }
void dbg_leave_tcg(void) { int i = self_idx(); if (i >= 0) g_state[i] = DTH_SYSCALL; }
void dbg_wait_enter(void) { int i = self_idx(); if (i >= 0) g_state[i] = DTH_WAIT; }
void dbg_wait_exit(void)  { int i = self_idx(); if (i >= 0) g_state[i] = DTH_SYSCALL; }
int  dbg_thread_waiting(int idx) { return idx >= 0 && idx < MAXTH && g_state[idx] == DTH_WAIT; }

static void note_stop(int reason, int id, uint32_t addr, uint32_t value, uint32_t pc) {
    g_last.reason = reason;
    g_last.tid    = g_self ? g_self->tid : -1;
    g_last.pc     = pc;
    g_last.id     = id;
    g_last.addr   = addr;
    g_last.value  = value;
}

/* ---- hook callbacks -------------------------------------------------------- */
static void bp_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)size; (void)user;
    if (!g_dbg_armed) return;
    note_stop(DBG_BP, 0, (uint32_t)addr, 0, (uint32_t)addr);
    for (int i = 0; i < MAXBP; i++)
        if (g_bp[i].used && g_bp[i].addr == (uint32_t)addr) { g_last.id = g_bp[i].id; break; }
    g_dbg_stop = 1;
    uc_emu_stop(uc);     /* emu_run then parks us at the park point */
}

static void wp_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                  int64_t value, void *user) {
    (void)type; (void)size; (void)user;
    if (!g_dbg_armed) return;
    uint32_t pc = 0;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    note_stop(DBG_WP, 0, (uint32_t)addr, (uint32_t)value, pc);
    for (int i = 0; i < MAXWP; i++)
        if (g_wp[i].used && (uint32_t)addr >= g_wp[i].addr &&
            (uint32_t)addr < g_wp[i].addr + g_wp[i].len) { g_last.id = g_wp[i].id; break; }
    g_dbg_stop = 1;
    uc_emu_stop(uc);
}

/* Apply the current breakpoint/watchpoint set to one uc. The caller must guarantee this uc is not
   executing: either it is brand new (uc_hook_std) or its thread is parked. */
static void sync_slot(struct ucslot *s) {
    /* Invalidating the containing translation block is REQUIRED, not an optimisation. Unicorn
       emits per-instruction hook calls at translation time, so a breakpoint added to code that is
       already translated is simply not in the generated block and never fires -- which is exactly
       how this first behaved. (The in-tree code hooks, ME_PCHOOK and oabi_libm, are installed at
       uc creation before anything is translated, so they never hit this.) The same applies on
       removal, or the block keeps calling a hook that is gone. */
    for (int i = 0; i < MAXBP; i++)
        if (s->bph[i]) {
            uc_hook_del(s->uc, s->bph[i]);
            uc_ctl_remove_cache(s->uc, s->bpaddr[i], s->bpaddr[i] + 4);
            s->bph[i] = 0; s->bpid[i] = 0; s->bpaddr[i] = 0;
        }
    for (int i = 0; i < MAXWP; i++)
        if (s->wph[i]) { uc_hook_del(s->uc, s->wph[i]); s->wph[i] = 0; s->wpid[i] = 0; }
    for (int i = 0; i < MAXBP; i++)
        if (g_bp[i].used) {
            /* BOUNDED code hook: begin == end, so only the containing TB loses chaining. */
            uc_hook_add(s->uc, &s->bph[i], UC_HOOK_CODE, (void *)bp_cb, NULL,
                        g_bp[i].addr, g_bp[i].addr);
            s->bpid[i] = g_bp[i].id;
            s->bpaddr[i] = g_bp[i].addr;
            uc_ctl_remove_cache(s->uc, g_bp[i].addr, g_bp[i].addr + 4);
        }
    for (int i = 0; i < MAXWP; i++)
        if (g_wp[i].used) {
            uc_hook_add(s->uc, &s->wph[i], UC_HOOK_MEM_WRITE, (void *)wp_cb, NULL,
                        g_wp[i].addr, g_wp[i].addr + g_wp[i].len - 1);
            s->wpid[i] = g_wp[i].id;
        }
    s->epoch = g_epoch;
}

void dbg_hooks_install(void *uc) {
    if (!uc) return;
    pthread_mutex_lock(&g_dbg_lock);
    int any = 0;
    for (int i = 0; i < MAXBP; i++) if (g_bp[i].used) any = 1;
    for (int i = 0; i < MAXWP; i++) if (g_wp[i].used) any = 1;
    if (any) {                      /* a thread created while breakpoints are set inherits them */
        int si = slot_idx((uc_engine *)uc);
        if (si >= 0) sync_slot(&g_slot[si]);
    } else {
        slot_idx((uc_engine *)uc);  /* just register it */
    }
    pthread_mutex_unlock(&g_dbg_lock);
}

/* ---- the park point (called from emu_run, holding NO engine lock) ---------- */
int dbg_park(void *ucv, uint32_t *pc) {
    uc_engine *uc = ucv;
    int i = self_idx();
    if (i < 0) return 1;

    pthread_mutex_lock(&g_dbg_lock);
    g_state[i] = DTH_PARKED;
    /* Pick up any breakpoint changes made while we were elsewhere. Doing it here -- on our own
       thread, provably outside uc_emu_start -- is what makes hook mutation safe. */
    int si = slot_idx(uc);
    if (si >= 0 && g_slot[si].epoch != g_epoch) sync_slot(&g_slot[si]);
    pthread_cond_broadcast(&g_dbg_ack);

    while (g_dbg_stop && !g_exit && !g_shutdown) {
        if (g_step[i] > 0) {
            g_step[i]--;
            pthread_mutex_unlock(&g_dbg_lock);
            /* One instruction. The count limit costs TB chaining for this call only.
               If that instruction is an SVC the whole syscall runs (step-over semantics), and
               intr_cb's dbg_leave_tcg/dbg_enter_tcg will have rewritten our execution state --
               so the park must reassert ownership of it below, or the next step is rejected as
               "not parked" and stepping silently stops working after the first syscall. */
            uc_emu_start(uc, *pc, 0, 0, 1);
            uc_reg_read(uc, UC_ARM_REG_PC, pc);
            pthread_mutex_lock(&g_dbg_lock);
            g_state[i] = DTH_PARKED;
            if (g_last.reason != DBG_BP) {
                g_last.reason = DBG_STEP; g_last.pc = *pc;
                g_last.tid = g_self ? g_self->tid : -1;
            }
            pthread_cond_broadcast(&g_dbg_ack);
            continue;
        }
        pthread_cond_wait(&g_dbg_cv, &g_dbg_lock);
    }
    g_state[i] = DTH_TCG;
    pthread_mutex_unlock(&g_dbg_lock);
    return !(g_exit || g_shutdown);
}

/* ---- control --------------------------------------------------------------- */
/* Shared body of dbg_pause and dbg_quiesce. `all_parked` picks the stop condition: a debugger
   only needs nothing to be RUNNING, a savestate needs everything actually PARKED (see dbg.h). */
static int dbg_stop_world(int timeout_ms, int all_parked,
                          int *parked, int *blocked, int *running) {
    /* Deadlock guard: a thread that needs g_biglock to reach the park point can never get there
       if the pauser is holding it. */
    if (g_holds_biglock) return -2;   /* caller error, distinct from a timeout */

    g_dbg_armed = 1;
    pthread_mutex_lock(&g_dbg_lock);
    /* Do NOT clobber an existing stop reason: pausing something already stopped at a breakpoint
       must not rewrite why it stopped, or a client that polls status and then pauses loses the
       one piece of information it was after. */
    if (!g_dbg_stop) g_last.reason = DBG_PAUSE;
    g_dbg_stop = 1;
    pthread_mutex_unlock(&g_dbg_lock);

    double deadline = host_now() + (timeout_ms > 0 ? timeout_ms : 2000) / 1000.0;
    double next_kick = 0;
    for (;;) {
        /* Kick every CPU out of TCG, then release anything blocked so it can reach a park point.
           REPEATED, not done once: a thread freed from futex_wait re-checks its predicate and can
           go straight back to sleep before it ever reaches the park point, so a single broadcast
           loses the race. Rate-limited so this stays cheap while we spin. */
        if (host_now() >= next_kick) {
            next_kick = host_now() + 0.01;
            for (int i = 0; i < g_nth; i++)
                if (g_th[i].uc && g_th[i].state != TH_DEAD) uc_emu_stop(g_th[i].uc);
            futex_wake_all();
            engine_wake_sigwaiters();
        }
        int p = 0, b = 0, r = 0;
        pthread_mutex_lock(&g_dbg_lock);
        for (int i = 0; i < g_nth; i++) {
            if (!g_th[i].uc || g_th[i].state == TH_DEAD) continue;
            /* DTH_WAIT counts with the parked: the thread is off-CPU in an indefinite wait and
               its registers are stable, which is all a stop-the-world needs. */
            if      (g_state[i] == DTH_PARKED || g_state[i] == DTH_WAIT) p++;
            else if (g_state[i] == DTH_SYSCALL) b++;
            else                                r++;
        }
        pthread_mutex_unlock(&g_dbg_lock);
        int done = all_parked ? (!r && !b) : !r;
        if (done || host_now() >= deadline) {
            if (parked) *parked = p;
            if (blocked) *blocked = b;
            if (running) *running = r;
            return done ? 0 : (all_parked ? -1 : 0);
        }
        me_usleep(2000);
    }
}

int dbg_pause(int timeout_ms, int *parked, int *blocked, int *running) {
    /* Keeps its historical contract: <0 means only "called from the wrong thread", never
       "did not settle" -- dbg_pause is satisfied by nothing running. */
    return dbg_stop_world(timeout_ms, 0, parked, blocked, running) == -2 ? -1 : 0;
}

int dbg_quiesce(int timeout_ms, int *parked, int *blocked, int *running) {
    return dbg_stop_world(timeout_ms, 1, parked, blocked, running);
}

void dbg_resume(void) {
    pthread_mutex_lock(&g_dbg_lock);
    g_dbg_stop = 0;
    g_last.reason = DBG_NONE;
    pthread_cond_broadcast(&g_dbg_cv);
    pthread_mutex_unlock(&g_dbg_lock);
}

void dbg_force_resume(void) {
    pthread_mutex_lock(&g_dbg_lock);
    g_dbg_stop = 0;
    for (int i = 0; i < MAXTH; i++) g_step[i] = 0;
    pthread_cond_broadcast(&g_dbg_cv);
    pthread_mutex_unlock(&g_dbg_lock);
}

int dbg_step(int tid, int n) {
    if (n < 1) n = 1;
    if (!g_dbg_stop) return -1;             /* stepping only makes sense while paused */
    int idx = -1;
    for (int i = 0; i < g_nth; i++) if (g_th[i].tid == tid) { idx = i; break; }
    if (idx < 0) return -2;

    pthread_mutex_lock(&g_dbg_lock);
    if (g_state[idx] != DTH_PARKED) { pthread_mutex_unlock(&g_dbg_lock); return -3; }
    g_step[idx] = n;
    g_last.reason = DBG_NONE;
    pthread_cond_broadcast(&g_dbg_cv);
    /* Wait for the steps to be consumed so the caller sees the post-step state. */
    double deadline = host_now() + 2.0;
    while (g_step[idx] > 0 && host_now() < deadline) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&g_dbg_ack, &g_dbg_lock, &ts);
    }
    int left = g_step[idx];
    g_step[idx] = 0;
    pthread_mutex_unlock(&g_dbg_lock);
    return left ? -4 : 0;
}

/* Apply the current breakpoint/watchpoint set to every PARKED thread's uc. Caller holds g_dbg_lock.
   Threads that are merely blocked in a syscall pick the change up at their next park (epoch). */
static void apply_to_parked(void) {
    for (int i = 0; i < g_nth; i++) {
        if (!g_th[i].uc || g_th[i].state == TH_DEAD) continue;
        if (g_state[i] != DTH_PARKED) continue;
        int si = slot_idx(g_th[i].uc);
        if (si >= 0) sync_slot(&g_slot[si]);
    }
}

/* Publish a breakpoint/watchpoint change to the running world.
 *
 * Hooks may only be mutated on a uc that is not executing, so if the guest is running we take a
 * TRANSIENT stop-the-world, install, and resume. Without this, setting a breakpoint on a running
 * guest silently did nothing at all: apply_to_parked() found no parked threads, and a running
 * thread only re-syncs inside dbg_park -- which is never reached unless a stop is already pending.
 * Must be called with g_dbg_lock NOT held (dbg_pause takes it). */
static void publish_change(void) {
    pthread_mutex_lock(&g_dbg_lock);
    g_epoch++;
    int was_paused = g_dbg_stop;
    if (was_paused) apply_to_parked();
    pthread_mutex_unlock(&g_dbg_lock);
    if (was_paused) return;

    dbg_pause(2000, NULL, NULL, NULL);
    pthread_mutex_lock(&g_dbg_lock);
    apply_to_parked();
    /* Installing during a transient stop is not itself a debugger stop -- don't leave a bogus
       "paused" reason behind for the next status query. */
    g_last.reason = DBG_NONE;
    pthread_mutex_unlock(&g_dbg_lock);
    dbg_resume();
}

int dbg_bp_add(uint32_t addr) {
    pthread_mutex_lock(&g_dbg_lock);
    int slot = -1;
    for (int i = 0; i < MAXBP; i++) if (!g_bp[i].used) { slot = i; break; }
    if (slot < 0) { pthread_mutex_unlock(&g_dbg_lock); return -1; }
    g_bp[slot].used = 1; g_bp[slot].addr = addr; g_bp[slot].id = g_next_id++;
    int id = g_bp[slot].id;
    g_dbg_armed = 1;   /* must be set before publish_change stops the world */
    pthread_mutex_unlock(&g_dbg_lock);
    publish_change();
    return id;
}

int dbg_bp_del(int id) {
    pthread_mutex_lock(&g_dbg_lock);
    int found = 0;
    for (int i = 0; i < MAXBP; i++)
        if (g_bp[i].used && g_bp[i].id == id) { g_bp[i].used = 0; found = 1; }
    pthread_mutex_unlock(&g_dbg_lock);
    if (found) publish_change();
    return found ? 0 : -1;
}

void dbg_bp_clear(void) {
    pthread_mutex_lock(&g_dbg_lock);
    for (int i = 0; i < MAXBP; i++) g_bp[i].used = 0;
    pthread_mutex_unlock(&g_dbg_lock);
    publish_change();
}

int dbg_bp_list(uint32_t *addrs, int *ids, int cap) {
    pthread_mutex_lock(&g_dbg_lock);
    int n = 0;
    for (int i = 0; i < MAXBP && n < cap; i++)
        if (g_bp[i].used) { addrs[n] = g_bp[i].addr; ids[n] = g_bp[i].id; n++; }
    pthread_mutex_unlock(&g_dbg_lock);
    return n;
}

int dbg_wp_add(uint32_t addr, uint32_t len) {
    if (!len) len = 4;
    pthread_mutex_lock(&g_dbg_lock);
    int slot = -1;
    for (int i = 0; i < MAXWP; i++) if (!g_wp[i].used) { slot = i; break; }
    if (slot < 0) { pthread_mutex_unlock(&g_dbg_lock); return -1; }
    g_wp[slot].used = 1; g_wp[slot].addr = addr; g_wp[slot].len = len; g_wp[slot].id = g_next_id++;
    int id = g_wp[slot].id;
    g_dbg_armed = 1;
    pthread_mutex_unlock(&g_dbg_lock);
    publish_change();
    return id;
}

int dbg_wp_del(int id) {
    pthread_mutex_lock(&g_dbg_lock);
    int found = 0;
    for (int i = 0; i < MAXWP; i++)
        if (g_wp[i].used && g_wp[i].id == id) { g_wp[i].used = 0; found = 1; }
    pthread_mutex_unlock(&g_dbg_lock);
    if (found) publish_change();
    return found ? 0 : -1;
}

int dbg_wp_list(uint32_t *addrs, uint32_t *lens, int *ids, int cap) {
    pthread_mutex_lock(&g_dbg_lock);
    int n = 0;
    for (int i = 0; i < MAXWP && n < cap; i++)
        if (g_wp[i].used) { addrs[n] = g_wp[i].addr; lens[n] = g_wp[i].len; ids[n] = g_wp[i].id; n++; }
    pthread_mutex_unlock(&g_dbg_lock);
    return n;
}

void dbg_last_stop(struct dbg_stop *out) {
    pthread_mutex_lock(&g_dbg_lock);
    *out = g_last;
    pthread_mutex_unlock(&g_dbg_lock);
}

void dbg_reset(void) {
    pthread_mutex_lock(&g_dbg_lock);
    g_dbg_stop = 0;
    g_dbg_armed = 0;
    memset(g_bp, 0, sizeof g_bp);
    memset(g_wp, 0, sizeof g_wp);
    memset(g_state, 0, sizeof g_state);
    memset(g_step, 0, sizeof g_step);
    memset(g_slot, 0, sizeof g_slot);   /* the ucs themselves are already closed by the caller */
    memset(&g_last, 0, sizeof g_last);
    g_epoch = 0;
    pthread_cond_broadcast(&g_dbg_cv);
    pthread_mutex_unlock(&g_dbg_lock);
}
