/* magiceyes Unicorn engine — host-fault guard.
 *
 * On native Windows the engine installs no SEH/signal handler, so a genuine HOST access
 * violation (a bad guest->host pointer deref in a device/syscall handler, or inside Unicorn's
 * JIT) on ANY thread instantly kills the whole process -- taking the GUI down with it. This
 * module wraps the two places guest code can fault us natively -- uc_emu_start (the CPU) and
 * present_active (the off-CPU framebuffer present) -- so a fault is caught, the game is torn
 * down, and the window survives.
 *
 * This is a net for HOST faults only. Guest unmapped accesses flow through Unicorn's TLB into
 * mem_invalid_cb (threads.c) and never reach a host signal, so normal emulation is untouched.
 *
 * Windows: __try/__except over the faulting structured exceptions. Linux: a sigaction for
 * SIGSEGV/SIGBUS that siglongjmps back to a per-thread arm point. Either way, on a fault we
 * release g_biglock if this thread held it (g_holds_biglock) so other threads don't deadlock,
 * and return -1 to the caller, which then runs the existing game-teardown path. */
#include "engine.h"

/* Recovery handoff (see engine.h). Defined here; read by the viewer thread in the bundle. */
volatile int g_fault_pending = 0;
volatile uintptr_t g_fault_addr = 0;
char g_cur_game[PATH_MAX] = {0};

void guard_release_biglock(void) {
    if (g_holds_biglock) { g_holds_biglock = 0; pthread_mutex_unlock(&g_biglock); }
}

/* Run uc from `entry`, restarting after each FPA instruction the invalid-insn hook emulates.
   Unicorn stops emulation when an invalid-instruction hook reports "handled" (it can't resume
   in place), so the hook advances PC + sets g_fpa_resume and we re-enter from the new PC. A
   real return (game exit, uc_emu_stop, or a genuine invalid insn) leaves g_fpa_resume clear. */
static int emu_run(uc_engine *uc, uint32_t entry) {
    uint32_t pc = entry;
    int e;
    do {
        g_fpa_resume = 0;
        e = (int)uc_emu_start(uc, pc, 0, 0, 0);
        if (g_fpa_resume) uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    } while (g_fpa_resume && e == UC_ERR_OK && !g_exit && !g_shutdown);
    return e;
}

#ifdef _WIN32
#include <windows.h>
/* NB: MinGW GCC does NOT support the MSVC __try/__except statement, so we can't use lexical SEH.
   The GCC-compatible equivalent is a process-wide Vectored Exception Handler that, on a genuine
   fault inside a guarded (armed) thread, restores a CONTEXT captured at the guard's arm point --
   the Win32 analog of sigsetjmp/siglongjmp. Restoring a full CONTEXT (RtlRestoreContext) just
   slams the registers back, so it needs no unwind info for the discarded JIT/callback frames. */

static __thread CONTEXT g_ctx;             /* captured arm point */
static __thread volatile int g_armed = 0;
static __thread volatile int g_returned = 0;  /* set by the handler -> "we faulted" on resume */
static __thread volatile uintptr_t g_faddr = 0, g_fpc = 0;

static int is_fault_code(DWORD c) {
    return c == EXCEPTION_ACCESS_VIOLATION || c == EXCEPTION_IN_PAGE_ERROR ||
           c == EXCEPTION_ILLEGAL_INSTRUCTION || c == EXCEPTION_PRIV_INSTRUCTION ||
           c == EXCEPTION_DATATYPE_MISALIGNMENT || c == EXCEPTION_ARRAY_BOUNDS_EXCEEDED;
}

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (getenv("ME_FAULTLOG") && is_fault_code(c)) {   /* diag: log every host fault, armed or not */
        uintptr_t fa = ep->ExceptionRecord->NumberParameters >= 2
                     ? (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1] : 0;
        fprintf(stderr, "FAULT code=%08lx pc=%p addr=%p armed=%d\n",
                (unsigned long)c, ep->ExceptionRecord->ExceptionAddress, (void *)fa, g_armed);
    }
    if (!g_armed || !is_fault_code(c))
        return EXCEPTION_CONTINUE_SEARCH;   /* not ours (incl. STACK_OVERFLOW): crash as before */
    g_armed = 0;
    g_fpc = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    g_faddr = ep->ExceptionRecord->NumberParameters >= 2
              ? (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1] : 0;
    g_returned = 1;
    RtlRestoreContext(&g_ctx, NULL);        /* resume just after RtlCaptureContext; never returns */
    return EXCEPTION_CONTINUE_SEARCH;       /* unreachable */
}

int guarded_emu_start(uc_engine *uc, uint32_t entry, struct me_fault *f) {
    g_returned = 0;
    RtlCaptureContext(&g_ctx);              /* arm point; the handler resumes us right here */
    if (g_returned) {                       /* came back via the handler -> a host fault */
        if (f) { f->faulted = 1; f->addr = g_faddr; f->pc = g_fpc; }
        guard_release_biglock();
        return -1;
    }
    g_armed = 1;
    int e = emu_run(uc, entry);
    g_armed = 0;
    return e;
}

int guarded_present(void) {
    g_returned = 0;
    RtlCaptureContext(&g_ctx);
    if (g_returned) return -1;              /* stale/unmapped fb during teardown: skip the frame */
    g_armed = 1;
    present_active();
    g_armed = 0;
    return 0;
}

void guard_init(void) { AddVectoredExceptionHandler(1, veh); }

#else  /* ---- Linux: sigaction + per-thread siglongjmp ---- */
#include <signal.h>
#include <setjmp.h>

static __thread sigjmp_buf g_fjmp;
static __thread volatile int g_armed = 0;
static __thread volatile uintptr_t g_faddr = 0;
static struct sigaction g_old_segv, g_old_bus;

static void fault_handler(int sig, siginfo_t *si, void *u) {
    (void)u;
    if (g_armed) {
        g_faddr = (uintptr_t)(si ? si->si_addr : 0);
        siglongjmp(g_fjmp, sig);
    }
    /* not in a guarded region -> a real engine bug: restore + re-raise so it crashes loudly */
    sigaction(sig, sig == SIGBUS ? &g_old_bus : &g_old_segv, NULL);
    raise(sig);
}

void guard_init(void) {
    /* One process-wide disposition covers every thread (SIGSEGV/SIGBUS are delivered to the
       faulting thread). We don't recover stack overflows, so no sigaltstack is needed -- a
       wild-pointer fault leaves the thread's normal stack usable for the handler + longjmp. */
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_old_segv);
    sigaction(SIGBUS, &sa, &g_old_bus);
}

int guarded_emu_start(uc_engine *uc, uint32_t entry, struct me_fault *f) {
    if (sigsetjmp(g_fjmp, 1)) {
        g_armed = 0;
        if (f) { f->faulted = 1; f->addr = g_faddr; f->pc = 0; }
        guard_release_biglock();
        return -1;
    }
    g_armed = 1;
    int e = emu_run(uc, entry);
    g_armed = 0;
    return e;
}

int guarded_present(void) {
    if (sigsetjmp(g_fjmp, 1)) { g_armed = 0; return -1; }
    g_armed = 1;
    present_active();
    g_armed = 0;
    return 0;
}
#endif
