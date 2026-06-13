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
    int tid = g_self ? g_self->tid : -1;
    if (rel == 0x2a5d8) { fprintf(stderr, "[splash] Enter tid=%d this=%08x lr=%08x(blt %05x)\n", tid,
        rr(uc, UC_ARM_REG_R0), rr(uc, UC_ARM_REG_LR), rr(uc, UC_ARM_REG_LR) - g_blt_base); g_bt_budget--; }
    else if (rel == 0x293dc) { fprintf(stderr, "[splash] Exit tid=%d lr=%08x(blt %05x)\n", tid,
        rr(uc, UC_ARM_REG_LR), rr(uc, UC_ARM_REG_LR) - g_blt_base); g_bt_budget--; }
    else if (rel == 0x2a884) {            /* Update entry: r0 = splash this; mSubState @ this+92 */
        static int n = 0; if (n++ % 30) return;   /* rate-limit (called every frame) */
        uint32_t self = rr(uc, UC_ARM_REG_R0), ss = 0; if (self) uc_mem_read(uc, self + 92, &ss, 4);
        fprintf(stderr, "[splash] Update mSubState=%u tid=%d\n", ss, tid); g_bt_budget--;
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
    /* ME_DIDJ_LOOPRATE: UpdateInactivity is called once per CAppManager::Run iteration, so its
       call count is the app-loop rate -- the metric for whether cooperative scheduling unthrottled
       the loop. Print a running total every 200 calls (cheap; no host clock needed). */
    if (getenv("ME_DIDJ_LOOPRATE")) { static unsigned long c = 0; if (++c % 200 == 0)
        fprintf(stderr, "[looprate] CAppManager::Run iterations=%lu\n", c); }
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

/* CGameStateHandler::Update (libLightningBase 0x11fb4): per-frame state pump. It updates the
 * current state's vtable Update only if handler[41]!=0 && handler[24]!=handler[8]; else it skips. */
static void gsh_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user) {
    (void)size; (void)user;
    if (g_bt_budget <= 0) return;
    uint32_t rel = (uint32_t)addr - g_lb_base;
    if (rel == 0x11fb4) {
        if (getenv("ME_DIDJ_LOOPRATE")) { static unsigned long c = 0; if (++c % 200 == 0)
            fprintf(stderr, "[looprate] CGameStateHandler::Update calls=%lu\n", c); }
        /* ME_DIDJ_GSHALL: log EVERY handler instance pumped (no dedup) to expose A (splash) vs B
           (empty). Otherwise dedup the first few distinct handler pointers. */
        uint32_t h = rr(uc, UC_ARM_REG_R0), f = 0, s24 = 0, s8 = 0;
        uc_mem_read(uc, h + 41, &f, 1); uc_mem_read(uc, h + 24, &s24, 4); uc_mem_read(uc, h + 8, &s8, 4);
        static uint32_t seen[16]; static int nseen = 0; int known = 0;
        for (int i = 0; i < nseen; i++) if (seen[i] == h) { known = 1; break; }
        if (!getenv("ME_DIDJ_GSHALL")) { if (known) return; if (nseen < 16) seen[nseen++] = h; }
        fprintf(stderr, "[gsh] h=%08x flag41=%x cur24=%08x base8=%08x depth=%d %s\n", h, f & 0xff, s24, s8,
                (int)((s24 - s8) / 4), ((f & 0xff) && s24 != s8) ? "UPDATES" : "SKIPS"); g_bt_budget--;
    } else if (rel == 0x11e90) {     /* DoRequest: handler this = r0; pending state @+48, transition @+44 */
        uint32_t h = rr(uc, UC_ARM_REG_R0), pend = 0, tr = 0, s24 = 0, s8 = 0;
        uc_mem_read(uc, h + 48, &pend, 4); uc_mem_read(uc, h + 44, &tr, 4);
        uc_mem_read(uc, h + 24, &s24, 4); uc_mem_read(uc, h + 8, &s8, 4);
        fprintf(stderr, "[gsh] DoRequest h=%08x pending=%08x trans=%u depth=%d\n", h, pend, tr,
                (int)((s24 - s8) / 4)); g_bt_budget--;
    } else if (rel == 0x12028) {     /* about to call state->Update; r0 = state, [r0] = vtable */
        uint32_t st = rr(uc, UC_ARM_REG_R0), vt = 0; if (st) uc_mem_read(uc, st, &vt, 4);
        fprintf(stderr, "[gsh] *** UPDATES state=%08x vtable_rel(BLT)=%x\n", st, g_blt_base ? vt - g_blt_base : vt);
        g_bt_budget--;
    }
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

/* Install the BLT splash hooks on a uc. BLT.so dlopen's AFTER the guest threads exist, so the
 * note_mmap path only reaches the loader thread's uc -- but the splash Update runs on the main
 * thread's uc. So when BLT's base becomes known we install these on EVERY existing uc. */
static void hook_blt_on(uc_engine *u) {
    if (!g_blt_base || !trace_on()) return;
    uc_hook h;
    uc_hook_add(u, &h, UC_HOOK_CODE, splash_cb, NULL, (uint64_t)g_blt_base + 0x2a5d8, (uint64_t)g_blt_base + 0x2a5d8);
    uc_hook_add(u, &h, UC_HOOK_CODE, splash_cb, NULL, (uint64_t)g_blt_base + 0x293dc, (uint64_t)g_blt_base + 0x293dc);
    uc_hook_add(u, &h, UC_HOOK_CODE, splash_cb, NULL, (uint64_t)g_blt_base + 0x2a884, (uint64_t)g_blt_base + 0x2a884);
    uc_hook_add(u, &h, UC_HOOK_CODE, splash_cb, NULL, (uint64_t)g_blt_base + 0x2a900, (uint64_t)g_blt_base + 0x2a900);
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
        uc_hook_add(u, &h, UC_HOOK_CODE, gsh_cb, NULL,    /* CGameStateHandler::DoRequest (push/pop) */
                    (uint64_t)g_lb_base + 0x11e90, (uint64_t)g_lb_base + 0x11e90);
        uc_hook_add(u, &h, UC_HOOK_CODE, gsh_cb, NULL,    /* CGameStateHandler::Update entry */
                    (uint64_t)g_lb_base + 0x11fb4, (uint64_t)g_lb_base + 0x11fb4);
        uc_hook_add(u, &h, UC_HOOK_CODE, gsh_cb, NULL,    /* the state->Update vtable call */
                    (uint64_t)g_lb_base + 0x12028, (uint64_t)g_lb_base + 0x12028);
    }
    hook_blt_on(u);
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
    if (getenv("ME_DIDJ_MMAPLOG"))   /* every mmap: addr range + fd + off, to identify a region (e.g. 0x71xxxxxx) */
        fprintf(stderr, "[mmap] at=%08x end=%08x len=%06x off=%llx fd=%d\n",
                at, at + len, len, (unsigned long long)off, fd);
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
        for (int i = 0; i < g_nth; i++) if (g_th[i].uc) hook_blt_on(g_th[i].uc);  /* all existing ucs */
    }
}
