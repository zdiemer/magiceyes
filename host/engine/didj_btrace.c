/* magiceyes -- Didj inactivity boot-guard + button-task tracer.
 *
 * Wall 1 (see didj-lf1000-support memory): the Didj base UI shuts down ~10s into boot, before the
 * home menu. KEY FINDING (corrected via this tracer): the button INPUT PATH WORKS -- libButton's
 * LightningButtonTask reads our injected evdev key events and posts CButtonMessages, which reach
 * CAppManager::Notify (synchronously, NOT via the eventDispatchQueue) and set the inactivity
 * activity flag at CAppManager+0x5c (verified: ~1 Notify+reset per key event). The earlier
 * "input never resets inactivity" belief was WRONG. The real problem is TIMING: our emulated boot
 * only brings the button task alive at ~10s of real wall-clock -- right at the ~10s inactivity
 * threshold -- and boot thread-scheduling is non-deterministic, so the resets are a coin flip.
 *
 * THE GUARD (default ON for Didj; ME_DIDJ_NOINACTGUARD disables): hook CAppManager::UpdateInactivity
 * (0x29a78) entry and, until the guest first consumes an input event (g_input_active), force the
 * activity flag set on the CAppManager `this` (r0, +0x5c) so the inactivity state machine can't
 * advance to shutdown during the input-less boot window; once real input flows the guard backs off
 * and the button task's own events drive it (so the device still auto-sleeps on genuine idle). A
 * 1-byte poke beats distorting the global clock (which stalls boot or doesn't bind -- see the now-
 * opt-in ME_DIDJ_CLOCKSTEP). PARTIAL: it removes the UpdateInactivity shutdown, but ~40% of runs
 * still shut down via a SEPARATE non-deterministic transition into CPowerDownState requested from
 * within CAppManager::Run's state pump (a deeper state-machine/scheduling race, not yet root-caused;
 * the residual likely needs the engine's thread-scheduling robustness addressed).
 *
 * THE TRACER (ME_DIDJ_BTRACE): logs the button task's decision points, Notify/activity-flag, and the
 * shutdown entry points (ShutDown/ExitPopUnloadApp/CPowerDownState::Enter) -- the diagnostics that
 * pinned all of the above. Offsets are libButton.so / libLightningBase.so file vaddrs (first PT_LOAD
 * at vaddr 0, so runtime base == the file-offset-0 mmap address, learned by correlating
 * open()/mmap()). Hooks are per-uc (each guest thread has its own Unicorn) via uc_hook_std(); the
 * relevant threads spawn after both libs load, so the bases are known. */
#include "engine.h"
#include <string.h>

extern int g_input_active;

static int      g_bt_fd     = -1;     /* host fd of libButton.so */
static uint32_t g_bt_base   = 0;      /* runtime load base */
static int      g_lb_fd     = -1;     /* host fd of libLightningBase.so */
static uint32_t g_lb_base   = 0;      /* runtime load base */
static int      g_blt_fd    = -1;     /* host fd of BLT.so (the base UI app) */
static uint32_t g_blt_base  = 0;      /* runtime load base */
static int      g_bt_budget = 1200;   /* tracer output cap */

static uint32_t rr(uc_engine *uc, int reg) { uint32_t v = 0; uc_reg_read(uc, reg, &v); return v; }

/* Wall-2 splash/video tracer: LightningSplashState::Update (BLT 0x2a884) shows the legal screen
 * for 3s then branches to startVideo (0x2a900) -> CVideoPlayback::Start (libLightningBase 0x213bc)
 * which plays the Theora intro; the splash advances to the menu when IsVideoPlaying()==false. */
static void splash_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)size; (void)user;
    if (g_bt_budget <= 0) return;
    uint32_t rel = (uint32_t)addr - g_blt_base;
    if (rel == 0x2a884) {                 /* Update entry: r0 = splash this; mSubState @ this+92 */
        static int n = 0; if (n++ % 60) return;   /* rate-limit (called every frame) */
        uint32_t self = rr(uc, UC_ARM_REG_R0), ss = 0; if (self) uc_mem_read(uc, self + 92, &ss, 4);
        fprintf(stderr, "[splash] Update mSubState=%u\n", ss); g_bt_budget--;
    } else if (rel == 0x2a900) {
        fprintf(stderr, "[splash] *** startVideo branch reached (3s legal elapsed)\n"); g_bt_budget--;
    }
}
static void vidpb_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)uc; (void)size; (void)user;
    uint32_t rel = (uint32_t)addr - g_lb_base;
    fprintf(stderr, "[video] %s tid=%d\n",
            rel == 0x213bc ? "CVideoPlayback::Start CALLED" : "CVideoPlayback::IsVideoPlaying",
            g_self ? g_self->tid : -1);
}

static int trace_on(void)  { return getenv("ME_DIDJ_BTRACE") != NULL; }
static int guard_on(void)  { return g_device == ME_DEV_DIDJ && !getenv("ME_DIDJ_NOINACTGUARD"); }
static int track_on(void)  { return trace_on() || guard_on(); }

/* UpdateInactivity entry (libLightningBase 0x29a78): r0 = CAppManager*. Until input is live, set
 * its activity flag (+0x5c) so the timer resets every poll and can't shut us down during boot. */
static void inactguard_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)addr; (void)size; (void)user;
    static int n = 0;
    if (g_input_active) return;                 /* real input now drives the timer */
    uint32_t self = rr(uc, UC_ARM_REG_R0);
    if (self) { uint8_t one = 1; uc_mem_write(uc, self + 0x5c, &one, 1); }
    if (trace_on() && n++ < 8)
        fprintf(stderr, "[guard] UpdateInactivity: forced activity on CAppManager=%08x tid=%d\n",
                self, g_self ? g_self->tid : -1);
}

/* button task decision points (libButton) */
static void bt_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)size; (void)user;
    if (g_bt_budget <= 0) return;
    uint32_t rel = (uint32_t)addr - g_bt_base;
    int tid = g_self ? g_self->tid : -1;
    switch (rel) {
    case 0x53f4:    /* jump-table dispatch: r3 = keycode - 19 */
        fprintf(stderr, "[bt] dispatch key=%u tid=%d\n", rr(uc, UC_ARM_REG_R3) + 19, tid); g_bt_budget--; break;
    case 0x557c:    /* press path: r1 = button bit, r2 = pressed-state */
        fprintf(stderr, "[bt] PRESS bit=%x state=%x tid=%d\n",
                rr(uc, UC_ARM_REG_R1), rr(uc, UC_ARM_REG_R2), tid); g_bt_budget--; break;
    case 0x55cc: {  /* post test: r3 = transition accumulator (r7[4]) */
        fprintf(stderr, "[bt] POST? accum=%x -> %s tid=%d\n",
                rr(uc, UC_ARM_REG_R3), rr(uc, UC_ARM_REG_R3) ? "POST" : "skip", tid); g_bt_budget--; break; }
    case 0x55e0:    /* -> PostEvent(CButtonMessage) */
        fprintf(stderr, "[bt] >>> PostEvent(CButtonMessage)\n"); g_bt_budget--; break;
    }
}

/* CAppManager::ShutDown (0x25614) / ExitPopUnloadApp (0x267e0): dump LR (caller) + the inactivity
 * state, to see which timer/path tore the app down. */
static void shut_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)size; (void)user;
    uint32_t rel = (uint32_t)addr - g_lb_base;
    const char *nm = rel == 0x25614 ? "ShutDown()" : rel == 0x267e0 ? "ExitPopUnloadApp()"
                   : rel == 0x204a0 ? "CPowerDownState::Enter (ENTERING SHUTDOWN)" : "?";
    fprintf(stderr, "[lb] %s lr=%08x(rel %05x) tid=%d g_input_active=%d\n", nm, rr(uc, UC_ARM_REG_LR),
            rr(uc, UC_ARM_REG_LR) - g_lb_base, g_self ? g_self->tid : -1, g_input_active);
}

/* CAppManager::Notify (libLightningBase 0x25748) + activity-flag store (0x25848) */
static void lb_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)size; (void)user;
    if (g_bt_budget <= 0) return;
    uint32_t rel = (uint32_t)addr - g_lb_base;
    int tid = g_self ? g_self->tid : -1;
    if (rel == 0x25748) {
        uint32_t ev = rr(uc, UC_ARM_REG_R1), ty = 0; if (ev) uc_mem_read(uc, ev + 4, &ty, 4);
        fprintf(stderr, "[lb] Notify event=%x type=%x tid=%d\n", ev, ty, tid); g_bt_budget--;
    } else if (rel == 0x25848) {
        fprintf(stderr, "[lb] *** activity flag SET tid=%d\n", tid); g_bt_budget--;
    }
}

void me_btrace_hook(uc_engine *u) {
    uc_hook h;
    if (g_lb_base && guard_on())
        uc_hook_add(u, &h, UC_HOOK_CODE, inactguard_cb, NULL,
                    (uint64_t)g_lb_base + 0x29a78, (uint64_t)g_lb_base + 0x29a78);
    if (!trace_on()) return;
    if (g_bt_base) uc_hook_add(u, &h, UC_HOOK_CODE, bt_cb, NULL,
                               (uint64_t)g_bt_base + 0x53d0, (uint64_t)g_bt_base + 0x56c0);
    if (g_lb_base) {
        uc_hook_add(u, &h, UC_HOOK_CODE, lb_cb, NULL,
                    (uint64_t)g_lb_base + 0x25748, (uint64_t)g_lb_base + 0x2584c);
        uc_hook_add(u, &h, UC_HOOK_CODE, shut_cb, NULL,
                    (uint64_t)g_lb_base + 0x25614, (uint64_t)g_lb_base + 0x25614);
        uc_hook_add(u, &h, UC_HOOK_CODE, shut_cb, NULL,
                    (uint64_t)g_lb_base + 0x267e0, (uint64_t)g_lb_base + 0x267e0);
        uc_hook_add(u, &h, UC_HOOK_CODE, shut_cb, NULL,   /* CPowerDownState::Enter: who transitions to shutdown */
                    (uint64_t)g_lb_base + 0x204a0, (uint64_t)g_lb_base + 0x204a0);
        uc_hook_add(u, &h, UC_HOOK_CODE, vidpb_cb, NULL,  /* CVideoPlayback::Start (Wall 2) */
                    (uint64_t)g_lb_base + 0x213bc, (uint64_t)g_lb_base + 0x213bc);
    }
    if (g_blt_base) {
        uc_hook_add(u, &h, UC_HOOK_CODE, splash_cb, NULL, (uint64_t)g_blt_base + 0x2a884, (uint64_t)g_blt_base + 0x2a884);
        uc_hook_add(u, &h, UC_HOOK_CODE, splash_cb, NULL, (uint64_t)g_blt_base + 0x2a900, (uint64_t)g_blt_base + 0x2a900);
    }
}

/* ME_LIBMAP: log the load base of every distinct .so, to map a stalled PC to lib+offset. The
 * dynamic loader re-opens libc/libgcc 100+ times, so dedup by basename + keep a fresh fd->name. */
static char g_libname[128][40]; static int g_libfd[128]; static int g_liblogged[128]; static int g_nlib = 0;
void me_btrace_note_open(const char *guest_path, int fd) {
    if (fd < 0) return;
    if (getenv("ME_DIDJ_OPENLOG")) { static int n = 0;
        if (n++ < 200 && strstr(guest_path, ".so")) fprintf(stderr, "[opn] fd=%d %s\n", fd, guest_path); }
    if (getenv("ME_LIBMAP") && strstr(guest_path, ".so")) {
        const char *b = strrchr(guest_path, '/'); b = b ? b + 1 : guest_path;
        int idx = -1;
        for (int i = 0; i < g_nlib; i++) if (!strcmp(g_libname[i], b)) { idx = i; break; }
        if (idx < 0 && g_nlib < 128) { idx = g_nlib++; snprintf(g_libname[idx], sizeof g_libname[0], "%s", b); }
        if (idx >= 0 && !g_liblogged[idx]) g_libfd[idx] = fd;   /* track until its base is logged */
    }
    if (!track_on()) return;
    if (g_bt_fd < 0 && strstr(guest_path, "libButton.so")) g_bt_fd = fd;
    else if (g_lb_fd < 0 && strstr(guest_path, "libLightningBase.so")) g_lb_fd = fd;
    else if (g_blt_fd < 0 && strstr(guest_path, "BLT.so")) g_blt_fd = fd;  /* base UI app */
}

void me_btrace_note_mmap(int fd, uint32_t at, uint64_t off, uint32_t len) {
    if (getenv("ME_LIBMAP") && off == 0 && len >= 0x40000)   /* big TEXT mmap = a real lib load; NO dedup */
        fprintf(stderr, "[bigmap] base=%08x len=%06x end=%08x fd=%d\n", at, len, at + len, fd);
    if (getenv("ME_LIBMAP") && off == 0 && len >= 0x1000)
        for (int i = 0; i < g_nlib; i++) if (!g_liblogged[i] && g_libfd[i] == fd) {
            fprintf(stderr, "[libmap] %-28s base=%08x len=%06x end=%08x\n", g_libname[i], at, len, at + len);
            g_liblogged[i] = 1; break; }
    if (off != 0 || len < 0x5000 || !track_on()) return;
    if (!g_bt_base && g_bt_fd >= 0 && fd == g_bt_fd) {
        g_bt_base = at;
        if (trace_on()) fprintf(stderr, "[bt] libButton base=%08x\n", at);
        me_btrace_hook(g_uc);
    } else if (!g_lb_base && g_lb_fd >= 0 && fd == g_lb_fd) {
        g_lb_base = at;
        if (trace_on()) fprintf(stderr, "[lb] libLightningBase base=%08x\n", at);
        me_btrace_hook(g_uc);
    } else if (!g_blt_base && g_blt_fd >= 0 && fd == g_blt_fd) {
        g_blt_base = at;
        if (trace_on()) fprintf(stderr, "[blt] BLT base=%08x\n", at);
        me_btrace_hook(g_uc);
    }
}
