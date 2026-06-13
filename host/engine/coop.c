/* magiceyes Unicorn engine — cooperative scheduler (single-core run token).
 *
 * Background: every guest thread runs on its own host thread with its own uc_engine over the
 * shared guest RAM, and (by default) executes guest CODE in TRUE PARALLEL -- only the syscall +
 * device layer is serialised (g_biglock). That parallelism is correct and fast for the GP2X
 * family, but some guests assume a SINGLE CORE and are not actually thread-safe under real
 * parallelism. The LeapFrog Didj (uClibc 0.9.29 + the Brio/MPI stack) is one: its malloc lock is
 * a swp+futex spinlock that LIVELOCKS when two workers (the Tremor audio decoder + module/button
 * init) malloc at the same wall-clock instant -- impossible on the real single-core device, where
 * the kernel runs exactly one thread at a time and a timer interrupt preempts it. The livelock
 * deadlocked ~half of Didj boots, and even once a futex backoff broke the hard deadlock the
 * residual lock CONTENTION (both workers spinning) throttled the app loop to ~1 Hz.
 *
 * This module restores single-core semantics for such guests with a global RUN TOKEN (a GIL): a
 * thread must hold it to execute guest code, so exactly one guest thread runs at a time. It is
 * released at every blocking point (futex/sleep/mqueue/sigwait -- the COOP_BLOCK_* macros) so a
 * blocked thread doesn't freeze the others, and on a periodic TIMESLICE (UC_HOOK_BLOCK) so a
 * compute- or spin-bound thread can't starve the rest -- the software analogue of the device's
 * timer-interrupt preemption. With execution serialised, two threads can never be inside malloc
 * concurrently, so the contention/livelock simply cannot arise.
 *
 * Gated on g_coop (set for ME_DEV_DIDJ in load_elf; ME_COOP=0/1 overrides). When off, runtok_*()
 * are no-ops and no per-block hook is installed -- the GP2X parallel path is byte-for-byte
 * unchanged and pays zero cost. */
#include "engine.h"
#include <sched.h>

int g_coop = 0;                                  /* 1 = single-core cooperative scheduling */
static pthread_mutex_t g_runtok = PTHREAD_MUTEX_INITIALIZER;
__thread int g_holds_runtok = 0;                 /* this host thread holds the run token */
static int g_slice = 0;                          /* timeslice length in basic blocks (0 until init) */

/* Acquire the run token (idempotent). Blocks until no other guest thread is executing. */
void runtok_acquire(void) {
    if (!g_coop || g_holds_runtok) return;
    pthread_mutex_lock(&g_runtok);
    g_holds_runtok = 1;
}
/* Release the run token (idempotent) so another guest thread can run. */
void runtok_release(void) {
    if (!g_coop || !g_holds_runtok) return;
    g_holds_runtok = 0;
    pthread_mutex_unlock(&g_runtok);
}
/* Crash-guard hook (guard.c): drop the token on a host fault so the survivors don't deadlock. */
void guard_release_runtok(void) {
    if (g_holds_runtok) { g_holds_runtok = 0; pthread_mutex_unlock(&g_runtok); }
}
/* EAGAIN / spin yield: hand the token to a waiter so the lock holder can make progress, then
   take it back. Used by futex_wait's mismatch path under cooperative scheduling. */
void runtok_yield(void) {
    if (!g_coop || !g_holds_runtok) return;
    runtok_release();
    sched_yield();
    runtok_acquire();
}

/* Timeslice: every g_slice basic blocks, yield the token so a waiting thread runs -- the
   cooperative stand-in for the device's preemptive timer tick. Installed only when g_coop
   (uc_hook_std), so threads that compute or spin without ever syscalling can't hog the core. */
void coop_block_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)uc; (void)addr; (void)size; (void)user;
    if (!g_self) return;
    if (++g_self->blkctr < g_slice) return;
    g_self->blkctr = 0;
    runtok_yield();
}

/* Called once from load_elf after g_device is known. */
void coop_init(int is_didj) {
    const char *e = getenv("ME_COOP");
    g_coop = e ? atoi(e) != 0 : is_didj;
    const char *s = getenv("ME_COOP_SLICE");
    g_slice = s ? atoi(s) : 20000;               /* ~one yield per ~150k guest insns */
    if (g_slice < 1) g_slice = 1;
    if (g_coop && getenv("ME_DEBUG"))
        fprintf(stderr, "[coop] single-core run token ON (slice=%d blocks)\n", g_slice);
}
