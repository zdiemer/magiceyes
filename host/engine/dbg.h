/* magiceyes debugger core: stop-the-world pause, single-step, breakpoints, watchpoints.
 *
 * Design constraints that shaped this (each is a real property of this engine):
 *
 *  - Every guest thread has its OWN uc_engine (threads.c), so "the CPU" is plural. A pause that
 *    only stops one thread yields a torn snapshot with the others still running.
 *  - There is no preemption timer. The only safe cross-thread control is uc_emu_stop, which the
 *    teardown paths already rely on.
 *  - uc_emu_stop is a TERMINATION signal everywhere else in the tree (it is always paired with
 *    g_exit). A pause must therefore stop the CPU and then RE-ENTER it, which is exactly the shape
 *    emu_run (guard.c) already uses to resume after an emulated FPA instruction. The park point
 *    lives there for that reason, and because it is the one place that holds no lock.
 *  - TH_BLOCKED / TH_SLEEPING (engine.h) are declared but never assigned anywhere, so t->state
 *    cannot distinguish "running" from "blocked in a futex". This module tracks its own per-thread
 *    execution state.
 *  - Hooks are registered once per uc and their handles were previously discarded; a breakpoint
 *    needs add AND remove, so this module owns a per-uc handle registry.
 *
 * Cost when idle: g_dbg_armed is 0 until something is actually armed, so the checks are a single
 * predictable branch per syscall and per uc_emu_start return. Breakpoints use BOUNDED
 * UC_HOOK_CODE (begin == end == addr), the same form oabi_libm.c already uses in production, so
 * they do not disable TB chaining globally -- only the containing block runs unchained.
 */
#ifndef MAGICEYES_DBG_H
#define MAGICEYES_DBG_H

#include <stdint.h>

/* Non-zero once any debug feature is armed. Hot paths test this before doing anything. */
extern volatile int g_dbg_armed;

/* Why the world last stopped. */
enum { DBG_NONE = 0, DBG_PAUSE, DBG_BP, DBG_WP, DBG_STEP, DBG_ENTRY };

struct dbg_stop {
    int reason;
    int tid;
    uint32_t pc;
    int id;            /* breakpoint / watchpoint id, or 0 */
    uint32_t addr;     /* watchpoint address, or 0 */
    uint32_t value;    /* watchpoint value written */
};

/* ---- called from the engine's hot paths ---- */
int  dbg_stop_pending(void);              /* cheap: is a stop requested? */
void dbg_enter_tcg(void);                 /* intr_cb: returning to guest code */
void dbg_leave_tcg(void);                 /* intr_cb: entered a syscall */
/* A guest thread sitting in an INDEFINITE wait inside a syscall marks itself around the wait.
   In practice that means sigsuspend and only sigsuspend: every other blocker in this engine is
   either bounded (nanosleep, select, poll, the dsp pacing) or released by a broadcast (futex).
   sigsuspend is neither, so without this a stop-the-world could never converge on any title with
   a LinuxThreads worker parked in __pthread_wait_for_restart_signal.

   Such a thread is off-CPU and its registers are stable, so dbg_quiesce counts it as quiesced.
   It is NOT parked at a park point, so it cannot be single-stepped, and a savestate has to treat
   it as mid-syscall: see the restart handling in state.c. */
void dbg_wait_enter(void);
void dbg_wait_exit(void);
int  dbg_thread_waiting(int idx);   /* 1 if g_th[idx] is in such a wait */
/* The park point, called from emu_run (guard.c) when a stop is pending. Blocks until resumed or
   stepped; updates *pc to where execution should re-enter. Returns 1 to continue running, 0 if
   the run should end (exit/shutdown). */
int  dbg_park(void *uc, uint32_t *pc);
void dbg_hooks_install(void *uc);         /* uc_hook_std: give a new uc the current breakpoints */

/* ---- control (called from the control channel) ---- */
/* Stop every guest thread and wait for them to quiesce. Fills the three counts with how many
   threads ended up parked / blocked in a syscall / still running at the timeout. MUST NOT be
   called while holding g_biglock (a thread that needs it to reach the park point would deadlock).
   Returns 0 on success. */
int  dbg_pause(int timeout_ms, int *parked, int *blocked, int *running);
/* Like dbg_pause, but waits for every live thread to actually reach the PARK POINT, not merely
   to stop executing TCG. The difference matters only for a savestate. dbg_pause returns as soon
   as nothing is RUNNING, which is the right answer for a debugger -- a thread sitting off-CPU
   inside a syscall has perfectly readable registers, so you can inspect it. A savestate cannot
   use that: such a thread is inside sys_dispatch, possibly mid-mutation of engine state, and its
   uc is suspended inside a hook inside uc_emu_start, which is not a state a fresh uc can be
   restored into. So this keeps re-broadcasting the wait queues until `blocked` reaches zero too.
   Same contract as dbg_pause otherwise (do NOT call holding g_biglock). Returns 0 if fully
   quiesced, -1 if it timed out with threads still blocked or running (the counts say which),
   -2 if called from a thread holding g_biglock. */
int  dbg_quiesce(int timeout_ms, int *parked, int *blocked, int *running);
void dbg_resume(void);
/* Unconditional release: clears the stop, all step requests, and wakes every parked thread.
   Called by the teardown/reload paths and by the control channel when its last client goes away,
   so a debugger that dies mid-pause can never wedge the emulator. */
void dbg_force_resume(void);
int  dbg_is_paused(void);
int  dbg_step(int tid, int n);            /* step a parked thread; 0 on success */

int  dbg_bp_add(uint32_t addr);           /* returns id, or -1 */
int  dbg_bp_del(int id);
int  dbg_bp_list(uint32_t *addrs, int *ids, int cap);
void dbg_bp_clear(void);
int  dbg_wp_add(uint32_t addr, uint32_t len);
int  dbg_wp_del(int id);
int  dbg_wp_list(uint32_t *addrs, uint32_t *lens, int *ids, int cap);

void dbg_last_stop(struct dbg_stop *out);
void dbg_reset(void);                     /* per reload: drop all state */

#endif /* MAGICEYES_DBG_H */
