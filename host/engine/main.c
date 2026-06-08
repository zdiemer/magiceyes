/* magiceyes Unicorn engine — main(), the main guest thread, and the host helper thread.
 * Native-threads model: the main thread runs its uc to completion; cloned guest threads
 * run on their own host threads (threads.c); a helper thread presents the framebuffer.
 * g_uc/g_self/g_th/g_biglock live in threads.c. */
#include "engine.h"
#include <stdarg.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#define ME_CHDIR(p) _chdir(p)
#else
#define ME_CHDIR(p) chdir(p)
#endif

FILE *g_log = NULL;   /* ME_LOGFILE diagnostic sink (see engine.h DIAG) */

/* ME_FWLOG: opt-in firmware/viewer debug logging that goes to DIAG (the ME_LOGFILE), so it is
   visible from the -mwindows bundle (which has no stderr). Callable from viewer.c too. */
int g_fwlog = 0;
void me_log(const char *fmt, ...) {
    if (!g_fwlog) return;
    va_list ap; va_start(ap, fmt); vfprintf(DIAG, fmt, ap); va_end(ap);
    fflush(DIAG);
}

/* chdir into the directory holding `path` so the game finds its Data/ (relative opens). */
static void chdir_to_dir_of(const char *path) {
    char d[PATH_MAX]; snprintf(d, sizeof d, "%s", path);
    char *s1 = strrchr(d, '/'), *s2 = strrchr(d, '\\');
    char *s = s1 > s2 ? s1 : s2;
    if (s && s != d) { *s = 0; if (ME_CHDIR(d) != 0 && g_trace) fprintf(stderr, "  [chdir %s failed]\n", d); }
}

uint32_t g_brk, g_brk_start;
uint32_t g_mmap_next = MMAP_BASE;
int g_exit = 0, g_exit_code = 0;
int g_shutdown = 0;   /* real quit (ends helper+viewer); g_exit is the transient per-run CPU bail */
int g_reloading = 0;  /* a reset/reload is in flight -> the helper thread skips present */
int g_reload_chdir = 0;   /* File->Open: chdir to the new game's dir; GPEComp re-exec: keep cwd */
char g_reload_path[PATH_MAX] = {0};   /* non-empty -> the main loop resets + loads this binary */
int g_trace = 0;
int g_scret = 0;   /* ME_SCRET: log every syscall + return value per thread (divergence diff) */
int g_eabi  = 0;   /* current syscall ABI: 1 = EABI (svc #0), 0 = legacy OABI (swi #0x9000xx) */
char g_exe_dir[PATH_MAX] = {0};   /* dir of our own executable (default rootfs search) */
__thread int g_setpc = 0;   /* a syscall set PC (signal entry / sigreturn): skip R0 write */

/* Firmware boot: we ran the device's gp2xmenu launcher (not a single game). On a game's exit
   we re-enter the menu instead of going idle, and gp2xmenu's game-launch execve chain-loads.
   These survive reloads (NOT cleared by engine_reset_globals) so they define the whole session. */
int g_firmware_mode = 0;
char g_firmware_menu[PATH_MAX] = {0};   /* path to gp2xmenu to return to between games */

/* synchronous fork state (system()/popen child; see syscalls.c) */
uc_context *g_fork_ctx = NULL;
struct snap g_snap[2048];
int g_nsnap = 0, g_forked = 0;
uint32_t g_child_pid = 0x1234;
/* The synchronous-fork child runs inline in the parent's engine, so host-side engine
   state it mutates (NOT covered by the guest-memory/uc-context snapshot) leaks into the
   parent. The big one: a fork child resets signal handlers to SIG_DFL before exec, which
   wiped the parent's LinuxThreads restart-signal (32) handler -> every later pthread
   restart was dropped -> deadlock. Save/restore the signal dispositions + the running
   thread's mask across the fork. */
struct sigact g_sigact_fork[65];
uint64_t g_fork_sigblocked;
struct thread *g_fork_thread;   /* the thread running the inline fork child (its sigaction
                                   resets must not leak into the shared signal table) */

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

/* viewer options (set by the CLI parser; the menu may change scale/fullscreen/audio at runtime).
   Defined unconditionally so the parser is the same in both builds; only the bundle's viewer
   thread consumes them (the two-process viewer.exe parses its own argv). */
static int g_view_scale = 3, g_fullscreen = 0, g_mute = 0, g_volume = 100;

#ifdef ME_BUNDLED
/* Single-process bundle: the SDL viewer runs in this process on a worker thread, sharing the
   engine's in-process g_shm directly (no cross-process shm bridge -> no Windows black screen).
   viewer_run lives in host/viewer.c; declared here so main.c needn't include SDL.h (which would
   #define main -> SDL_main and rename the engine's entry point). */
int viewer_run(gp2x_shm_t *shm, int scale, int fullscreen, int mute, int volume);
static void *viewer_thread(void *arg) {
    (void)arg;
    viewer_run(g_shm, g_view_scale, g_fullscreen, g_mute, g_volume);  /* returns on window close */
    g_shutdown = 1; g_exit = 1;        /* real quit: stop the engine + helper threads */
    if (g_shm) g_shm->quit = 1;
    exit(g_exit_code);                 /* engine main is blocked in uc_emu_start; force exit */
}
#endif

void die(const char *m, uc_err e) {
    fprintf(stderr, "me_unicorn: %s: %s\n", m, e ? uc_strerror(e) : "");
    exit(1);
}

#ifndef _WIN32
void me_usleep(unsigned us) { usleep(us); }   /* Linux usleep is already high-resolution */
void me_platform_init(void) {}                /* Linux doesn't throttle background processes */
#endif

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
    /* svc #0 (imm==0) = EABI; swi #(0x900000+nr) = legacy OABI. The whole process is one ABI,
       so this global (set every syscall, same value from every thread) tells ABI-sensitive
       syscalls which kernel struct layout to write (e.g. struct stat64: OABI 96B vs EABI 104B). */
    g_eabi = (imm == 0);
    if (g_self) g_self->last_pc = pc;   /* diagnostics: where this thread last syscalled */
    /* gettimeofday/clock_gettime fast path: read-only + thread-safe (host clock + a write to the
       caller's own buffer), and hammered in tight timing loops (Liar busy-polls gettimeofday
       ~850k/s). Serve it WITHOUT the biglock or per-syscall housekeeping -- that overhead both
       tanked fps and starved the game's frame pacing. */
    if (!g_exit && (nr == 78 || nr == 263 || nr == 266)) {
        struct timeval tv; gettimeofday(&tv, NULL);
        if (nr == 78) { uint32_t tvp = gread(UC_ARM_REG_R0);
            if (tvp) { uint32_t t[2] = { (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec }; uc_mem_write(uc, tvp, t, 8); } }
        else { uint32_t tsp = gread(UC_ARM_REG_R1);   /* clock_gettime(clk, timespec) */
            if (tsp) { uint32_t t[2] = { (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec * 1000 }; uc_mem_write(uc, tsp, t, 8); } }
        gwrite(UC_ARM_REG_R0, 0);
        return;
    }
    BIGLOCK_LOCK();
    g_setpc = 0;
    uint32_t a0 = gread(UC_ARM_REG_R0), a1 = gread(UC_ARM_REG_R1), a2 = gread(UC_ARM_REG_R2);
    long r = sys_dispatch(nr, a0, a1, a2,
                          gread(UC_ARM_REG_R3), gread(UC_ARM_REG_R4), gread(UC_ARM_REG_R5));
    if (!g_setpc && !g_exit) gwrite(UC_ARM_REG_R0, (uint32_t)r);
    if (g_scret) {   /* deterministic per-thread syscall+return trace (single fprintf, no interleave) */
        char b[180];
        snprintf(b, sizeof b, "SC %.3f t%d pc=%08x nr=%u(%08x,%08x,%08x)=%08lx\n",
                 host_now(), g_self ? g_self->tid : -1, pc, nr, a0, a1, a2, (unsigned long)(uint32_t)r);
        fputs(b, stderr);
    }
    BIGLOCK_UNLOCK();
    if (g_exit) uc_emu_stop(uc);
}

/* host helper thread: present the framebuffer + prof + thread dump, off the guest CPUs. */
static void *helper_thread(void *arg) {
    (void)arg;
    int prof = getenv("ME_PROF") ? 1 : 0;
    double prof_t = 0, tdp = 0, rpt_t = 0; uint32_t prof_fs = 0;
    /* ME_RUN_SECS=N: a headless test bound. After N seconds, stop the guest CPU and end the run
       CLEANLY (so the JSON report flushes) instead of the harness SIGKILLing us mid-write. */
    double run_secs = getenv("ME_RUN_SECS") ? atof(getenv("ME_RUN_SECS")) : 0;
    double run_t0 = host_now();
    int run_stopped = 0;
    const char *ac = getenv("ME_AUDIOCLEAR");   /* TEMP: simulate audio-DMA completion by
                                                   periodically clearing a guest "DMA busy" flag */
    uint32_t acaddr = ac ? (uint32_t)strtoul(ac, NULL, 0) : 0;
    while (!g_shutdown) {   /* survives reloads (g_exit is the transient per-run bail) */
        usleep(g_oadr_driven ? 2000 : 16000);   /* poll faster once frame-driven (low latency) */
        /* Present off the guest render thread: frame-synced to the game's OADR write
           (g_frame_ready) once it drives present, else an async fallback. Skip while a reload
           is tearing down/rebuilding guest memory (g_fb_guest is being reset). */
        if (!g_reloading && g_fb_guest && (!g_oadr_driven || g_frame_ready)) { g_frame_ready = 0; guarded_present(); }
        if (acaddr) { uint32_t *p = guest_to_host(acaddr); if (p) *p = 0; }
        double now = host_now();
        if (run_secs > 0 && !run_stopped && now - run_t0 >= run_secs) {
            run_stopped = 1;
            fprintf(DIAG, "ME_RUN_SECS=%.0f elapsed -> ending run\n", run_secs);
            g_exit = 1; g_shutdown = 1;
            if (g_shm) g_shm->quit = 1;          /* end the viewer loop (bundle) */
            uc_emu_stop(g_th[0].uc);             /* break the main thread out of guarded_emu_start */
        }
        if (prof && now - prof_t >= 2.0) {
            double dt = now - prof_t; uint32_t fs = g_shm ? g_shm->frame_seq : 0;
            extern unsigned long g_fpa_n, g_fpa_ops;
            fprintf(DIAG, "PROF: %.1f fps  mmsp2_rd=%.0f/s wr=%.0f/s fault=%.0f/s  fpa=%.0f/s(ops=%.0f/s)\n",
                    (fs - prof_fs) / dt, g_n_rd / dt, g_n_wr / dt, g_n_fault / dt, g_fpa_n / dt, g_fpa_ops / dt);
            prof_fs = fs; prof_t = now; g_n_rd = g_n_wr = g_n_fault = 0; g_fpa_n = g_fpa_ops = 0;
        }
        if (g_threaddump && now - tdp >= 2.0) { tdp = now; dump_threads("periodic"); }
        /* Flush the JSON report periodically so even a hard kill leaves a recent snapshot on disk
           (the harness imposes ME_RUN_SECS for a clean exit, but be robust anyway). */
        if (me_report_active() && now - rpt_t >= 3.0) { rpt_t = now; me_report_flush_json(NULL); }
    }
    return NULL;
}

/* TEMP diagnostic harness (ME_TEST_RELOAD): drive engine_request_reload through a ;-list of
   game paths to reproduce the hot-reload chain headlessly (under ASan/valgrind). */
static void *test_reload_thread(void *arg) {
    (void)arg;
    char *list = strdup(getenv("ME_TEST_RELOAD"));
    int secs = getenv("ME_TEST_RELOAD_SECS") ? atoi(getenv("ME_TEST_RELOAD_SECS")) : 6;
    if (secs < 1) secs = 1;
    for (char *p = strtok(list, ";"); p && !g_shutdown; p = strtok(NULL, ";")) {
        for (int i = 0; i < secs * 10 && !g_shutdown; i++) usleep(100000);
        char bin[PATH_MAX]; const char *r = resolve_input(p, bin, sizeof bin);
        if (!r || classify_elf(r) < 0) { fprintf(stderr, "[test-reload] skip '%s'\n", p); continue; }
        fprintf(stderr, "[test-reload] -> %s\n", r);
        engine_request_reload(r);
    }
    free(list);
    return NULL;
}

/* TEMP diagnostic (ME_TEST_FWBOOT=<device>): after a few seconds, drive me_firmware_boot_request
   to reproduce the GUI "Firmware -> Boot" reload path on the console engine (with stderr trace). */
static void *test_fwboot_thread(void *arg) {
    (void)arg;
    const char *dev = getenv("ME_TEST_FWBOOT");
    for (int i = 0; i < 30 && !g_shutdown; i++) usleep(100000);
    fprintf(stderr, "[test-fwboot] -> %s\n", dev);
    if (!me_firmware_boot_request(dev)) fprintf(stderr, "[test-fwboot] device not staged\n");
    return NULL;
}

#ifndef ME_VERSION
#define ME_VERSION "0.2.0-dev"   /* release builds inject the tag via -DME_VERSION (build_bundle_win.sh) */
#endif
static void print_usage(const char *p0) {
    fprintf(stderr,
        "magiceyes - run GP2X/Wiz games on a PC\n"
        "usage: %s [options] [game.gpe | folder | game.zip]\n"
        "       (with no game, the window opens empty -- use File > Open)\n\n"
        "      --firmware DEV boot the device firmware menu (gp2xmenu): wiz|caanoo|f100|f200\n"
        "      --install-firmware F  stage a firmware .zip/.img into the per-device dir, then exit\n"
        "  -s, --scale N      window scale factor (default 3)\n"
        "  -f, --fullscreen   start fullscreen (toggle in-app with F11)\n"
        "      --mute         start muted\n"
        "      --volume N     output volume 0..100 (default 100)\n"
        "      --timescale M  GP2X timer rate in MHz (default 7.3728; sets fps + game speed)\n"
        "      --trace        verbose syscall/device trace (ME_TRACE)\n"
        "      --profile      fps + hook profiling (ME_PROF)\n"
        "      --scret        per-thread syscall+return trace (ME_SCRET)\n"
        "      --threaddump   periodic thread-state dump (ME_THREADDUMP)\n"
        "      --no-smcfreeze disable the SMC-freeze CPU fix (ME_GP2X_NOSMCFREEZE)\n"
        "      --debug        heavy logging: structured run report + fps profiling (ME_DEBUG)\n"
        "      --report P     write the structured run report (JSON) to path P (ME_REPORT)\n"
        "      --run-secs N   run for N seconds then exit cleanly (ME_RUN_SECS; for headless tests)\n"
        "  -h, --help         show this help\n"
        "      --version      show version\n", p0);
}

/* Map + populate the ARMv5 kuser helper page (no HW TLS/atomics). Redone for each fresh uc
   (uc_close drops the mappings, mem_reset frees the backing), so a reload re-installs it. */
static void map_kuser_page(void) {
    uc_engine *u = g_uc;
    map_region(0xffff0000u, PAGE, UC_PROT_READ | UC_PROT_EXEC);
    uint32_t mb = 0xe1a0f00eu;                        /* mov pc,lr (memory_barrier) */
    uc_mem_write(u, 0xffff0fa0u, &mb, 4);
    uint32_t cx[] = {0xef90fff0u, 0xe1a0f00eu};       /* svc #0x90fff0 ; mov pc,lr (cmpxchg) */
    uc_mem_write(u, 0xffff0fc0u, cx, sizeof cx);
    uint32_t gt[] = {0xe59f0008u, 0xe1a0f00eu};       /* get_tls: ldr r0,[pc,#8]; mov pc,lr */
    uc_mem_write(u, 0xffff0fe0u, gt, sizeof gt);
    uint32_t ver = 2; uc_mem_write(u, 0xffff0ffcu, &ver, 4);
    uint32_t tramp[] = {0xe3a070adu, 0xef000000u};    /* SIG_TRAMP: mov r7,#173; svc 0 */
    uc_mem_write(u, 0xffff0f00u, tramp, sizeof tramp);
}

/* Per-game setup: a fresh uc over a clean address space, the kuser page, the ELF, the SysV
   stack (argv[0] = the binary path), and the main guest-thread record. Returns the entry PC.
   Called once at startup and again by engine_reset_and_load on every reload. */
static uint32_t engine_load_game(const char *path) {
    uc_engine *u;
    uc_err e = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &u);
    if (e) die("uc_open", e);
    g_uc = u;
    snprintf(g_cur_game, sizeof g_cur_game, "%s", path);   /* the game the crash guard names */
    map_kuser_page();
    shm_reset_for_new_game();
    uint32_t entry = load_elf(path);
    me_log("[fw] engine_load_game '%s' -> entry=%08x device=%d firmware_mode=%d\n",
           path, entry, g_device, g_firmware_mode);
    if (!entry) { uc_close(u); g_uc = NULL; return 0; }   /* bad/missing binary: caller goes idle */
    if (g_shm) {   /* device for the viewer header; classified from the ELF in load_elf (g_device) */
        g_shm->device  = (uint8_t)g_device;
        g_shm->backend = 0;   /* the shim/engine sets the real backend once it presents a frame */
    }
    char *av[1]; av[0] = (char *)path;
    uint32_t sp = setup_stack(1, av);
    gwrite(UC_ARM_REG_SP, sp);
    memset(&g_th[0], 0, sizeof g_th[0]);
    g_th[0].uc = u; g_th[0].th = 0; g_th[0].tid = g_next_tid++; g_th[0].ppid = 1;
    g_th[0].state = TH_RUN; g_self = &g_th[0]; g_nth = 1;
    uc_hook_std(u);
    return entry;
}

/* Zero/free every accumulated per-game global so a reload starts clean. Runs AFTER all ucs are
   closed and guest RAM is freed. (Inventory: main.c fork/snapshot/pipe + brk/mmap; threads.c
   table + signal dispositions; devices.c + syscalls.c via their own reset hooks.) */
static void engine_reset_globals(void) {
    if (g_fork_ctx) { uc_context_free(g_fork_ctx); g_fork_ctx = NULL; }
    for (int i = 0; i < g_nsnap; i++) free(g_snap[i].data);
    g_nsnap = 0; g_forked = 0;
    free(g_pipebuf); g_pipebuf = NULL; g_pipe_cap = g_pipe_w = g_pipe_r = 0;
    g_fork_thread = NULL; g_fork_sigblocked = 0;
    memset(g_sigact_fork, 0, sizeof g_sigact_fork);
    g_n_rd = g_n_wr = g_n_fault = 0;
    g_brk = g_brk_start = 0; g_mmap_next = MMAP_BASE;
    memset(g_th, 0, sizeof g_th); g_nth = 0; g_next_tid = 100;
    memset(g_sigact, 0, sizeof g_sigact);
    devices_reset();
    syscalls_reset();
}

/* The load-bearing primitive: tear down all guest state and (re)load a fresh ELF, keeping the
   process (and the viewer/helper threads + shm) alive. Returns the new entry PC, or 0 if the
   load failed. Used by both the GPEComp re-exec (case 11) and File->Open hot reload. */
static uint32_t engine_reset_and_load(const char *path) {
    g_reloading = 1;
    engine_stop_all_threads();                          /* join workers, close their ucs */
    if (g_th[0].uc) { uc_close(g_th[0].uc); g_th[0].uc = NULL; }
    mem_reset();                                        /* free guest RAM (every uc now closed) */
    engine_reset_globals();
    g_exit = 0; g_exit_code = 0;
    if (g_reload_chdir) { chdir_to_dir_of(path); g_reload_chdir = 0; }  /* File->Open: into the new game's dir */
    uint32_t entry = engine_load_game(path);
    g_reloading = 0;
    if (g_trace) fprintf(stderr, "  [reload] -> %s entry=%08x\n", path, entry);
    return entry;
}

int main(int argc, char **argv) {
    const char *logf = getenv("ME_LOGFILE");   /* divert diagnostics to a file -- the only way to
                                                  see logs from the -mwindows bundle (no console) */
    if (logf && *logf) {
        g_log = fopen(logf, "w");               /* a dedicated FILE* the rest of the engine never
                                                   reopens (stderr redirection is fragile in the
                                                   GUI-subsystem bundle); held for the whole run. */
        if (g_log) { setvbuf(g_log, NULL, _IONBF, 0); fputs("== magiceyes log start ==\n", g_log); }
    }
    setvbuf(stderr, NULL, _IONBF, 0);   /* diagnostics must survive a kill (msvcrt fully buffers
                                           a redirected stderr otherwise -> lost logs on Windows) */
    me_platform_init();   /* Windows: 1ms timer + opt out of EcoQoS throttling (else a backgrounded
                             window is CPU/timer-throttled -> ~4x slower load; Linux never throttles) */
    guard_init();         /* install the host-fault guard so a bad game can't crash the GUI */
    if (argc > 0 && argv[0][0]) {   /* remember the executable's dir (default device-rootfs search) */
        snprintf(g_exe_dir, sizeof g_exe_dir, "%s", argv[0]);
        char *s1 = strrchr(g_exe_dir, '/'), *s2 = strrchr(g_exe_dir, '\\'), *s = s1 > s2 ? s1 : s2;
        if (s) *s = 0; else g_exe_dir[0] = 0;
        if (g_exe_dir[0]) {   /* absolutise: it seeds rootfs/firmware/games search, and the loader
                                 chdir()s into the game dir, so a relative base would later break */
            char abs[PATH_MAX];
#ifdef _WIN32
            if (_fullpath(abs, g_exe_dir, sizeof abs)) snprintf(g_exe_dir, sizeof g_exe_dir, "%s", abs);
#else
            if (realpath(g_exe_dir, abs)) snprintf(g_exe_dir, sizeof g_exe_dir, "%s", abs);
#endif
        }
    }
    me_rootfs_init();     /* locate the device rootfs (dynamic-linked titles); env ME_GP2X_ROOTFS */
    /* ---- CLI: parse flags; the first non-flag positional is the game ---- */
    const char *input = NULL;
    const char *fw_device = NULL;   /* --firmware <device>: boot that device's gp2xmenu */
    const char *fw_install = NULL;  /* --install-firmware <file>: stage a firmware then exit */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == 0) { input = a; break; }   /* a bare "-" is not a flag */
        if      (!strcmp(a, "-h") || !strcmp(a, "--help"))       { print_usage(argv[0]); return 0; }
        else if (!strcmp(a, "--version"))                        { printf("magiceyes %s\n", ME_VERSION); return 0; }
        else if (!strcmp(a, "-f") || !strcmp(a, "--fullscreen")) g_fullscreen = 1;
        else if (!strcmp(a, "--mute"))                           g_mute = 1;
        else if (!strcmp(a, "-s") || !strcmp(a, "--scale"))      { if (++i < argc) g_view_scale = atoi(argv[i]); }
        else if (!strcmp(a, "--volume"))                         { if (++i < argc) g_volume = atoi(argv[i]); }
        else if (!strcmp(a, "--timescale"))                      { if (++i < argc) setenv("ME_GP2X_TIMESCALE", argv[i], 1); }
        else if (!strcmp(a, "--trace"))                          setenv("ME_TRACE", "1", 1);
        else if (!strcmp(a, "--profile"))                        setenv("ME_PROF", "1", 1);
        else if (!strcmp(a, "--scret"))                          setenv("ME_SCRET", "1", 1);
        else if (!strcmp(a, "--threaddump"))                     setenv("ME_THREADDUMP", "1", 1);
        else if (!strcmp(a, "--no-smcfreeze"))                   setenv("ME_GP2X_NOSMCFREEZE", "1", 1);
        else if (!strcmp(a, "--debug"))                          { setenv("ME_DEBUG", "1", 1); setenv("ME_PROF", "1", 1); }
        else if (!strcmp(a, "--firmware"))                       { if (++i < argc) fw_device = argv[i]; }
        else if (!strcmp(a, "--install-firmware"))               { if (++i < argc) fw_install = argv[i]; }
        else if (!strcmp(a, "--report"))                         { if (++i < argc) setenv("ME_REPORT", argv[i], 1); }
        else if (!strcmp(a, "--run-secs"))                       { if (++i < argc) setenv("ME_RUN_SECS", argv[i], 1); }
        else { fprintf(stderr, "magiceyes: unknown option '%s'\n", a); print_usage(argv[0]); return 2; }
    }
    if (g_view_scale < 1) g_view_scale = 1;
    if (g_volume < 0) g_volume = 0; else if (g_volume > 100) g_volume = 100;

    /* --install-firmware <file>: stage a .zip/.img into the per-device dir, then exit. */
    if (fw_install) return me_firmware_install(fw_install, fw_device);

    /* --firmware <device>: boot the device's gp2xmenu launcher from its staged rootfs. We pin
       the rootfs (Wiz/F100/F200 share ld-linux.so.2, so PT_INTERP can't disambiguate) and force
       MAGICEYES_DEVICE (the menu auto-detects as GP2X -> wrong input map for Wiz/Caanoo). The
       menu then loads via the normal dynamic-ELF path below. */
    if (fw_device) {
        char fwroot[PATH_MAX], fwmenu[PATH_MAX];
        if (!me_firmware_paths(fw_device, fwroot, fwmenu, sizeof fwroot)) {
            fprintf(stderr, "magiceyes: no staged firmware for device '%s'.\n"
                            "  Install one first (Firmware -> Install firmware..., or stage a rootfs).\n",
                            fw_device);
            return 3;
        }
        setenv("MAGICEYES_DEVICE", fw_device, 1);
        me_rootfs_set(fwroot);
        g_firmware_mode = 1;
        snprintf(g_firmware_menu, sizeof g_firmware_menu, "%s", fwmenu);
        input = g_firmware_menu;
        fprintf(DIAG, "magiceyes: firmware boot %s -> %s\n", fw_device, fwmenu);
    }

    /* ---- resolve folder/.zip/.gpe -> a runnable binary; reject dynamic-linked titles. With no
       game given, the bundle opens an empty window (File->Open loads one); the standalone engine
       still requires a game on the command line. ---- */
    char binbuf[PATH_MAX]; const char *bin = NULL;
    if (input) {
        bin = resolve_input(input, binbuf, sizeof binbuf);
        if (!bin) return 2;
        int cls = classify_elf(bin);
        if (cls < 0) return 2;
        if (cls == 1) {   /* dynamically-linked title (Odonata, W&W, RetroVirus, Patissier): load the
                             guest ld.so + NEEDED libs from the matching device rootfs. Pick it by the
                             binary's PT_INTERP -- ld-linux.so.2 (firmware glibc-2.3.6) vs .so.3 (EABI). */
            char interp[256];
            if (!read_elf_interp(bin, interp, sizeof interp))
                snprintf(interp, sizeof interp, "/lib/ld-linux.so.2");
            if (!me_rootfs_select(interp)) {
                fprintf(stderr, "magiceyes: '%s' needs interpreter '%s' but no device rootfs provides it.\n"
                                "  Build one with host/win/stage_rootfs.sh (firmware, ld-linux.so.2) or the\n"
                                "  EABI rootfs (ld-linux.so.3), and set ME_GP2X_ROOTFS / place it at <exe>/rootfs.\n",
                                bin, interp);
                return 3;
            }
            if (g_trace) fprintf(stderr, "magiceyes: '%s' dynamically linked (%s)\n", bin, interp);
        }
    } else {
#ifndef ME_BUNDLED
        fprintf(stderr, "magiceyes: no game specified\n"); print_usage(argv[0]); return 2;
#endif
    }

    if (getenv("ME_TRACE")) g_trace = 1;
#ifdef ME_DEV
    g_fwlog = 1;   /* the magiceyes-dev build logs the firmware/viewer diagnostics by default */
#endif
    if (getenv("ME_FWLOG")) g_fwlog = 1;
    if (getenv("ME_SCRET")) g_scret = 1;
    if (getenv("ME_THREADDUMP")) g_threaddump = 1;
    /* Structured run telemetry (host/engine/report.c). ME_REPORT=<path> writes the JSON the
       headless harness reads back; ME_DEBUG turns capture on with a default path. Off otherwise:
       me_report() is a no-op, so zero impact on a normal play session. */
    { const char *rpt = getenv("ME_REPORT");
      if (rpt && *rpt)            me_report_init(rpt);
      else if (getenv("ME_DEBUG")) me_report_init("me_report.json"); }
    threads_init();
    shm_setup();

    uint32_t entry = 0;
    if (bin) {
        chdir_to_dir_of(bin);                 /* run from the game's dir so its Data/ resolves */
        entry = engine_load_game(bin);
        if (g_trace) fprintf(stderr, "entry=%08x brk=%08x\n", entry, g_brk);
        if (!entry) {
            fprintf(stderr, "magiceyes: failed to load '%s'\n", bin);
#ifndef ME_BUNDLED
            return 1;                         /* standalone: nothing to run */
#endif
        }                                     /* bundle: fall through to an idle window */
    }

    /* helper + viewer threads are created ONCE and outlive every reload / idle period */
    pthread_t helper; pthread_create(&helper, NULL, helper_thread, NULL);
    /* TEMP diagnostic: ME_TEST_RELOAD="pathA;pathB" auto-hot-reloads through the list (every
       ME_TEST_RELOAD_SECS, default 6) — reproduces the File->Open reload chain headlessly so it
       can run under ASan/valgrind. Reuses the exact reload path the viewer's File->Open uses. */
    pthread_t treload; int test_reload = getenv("ME_TEST_RELOAD") != NULL;
    if (test_reload) pthread_create(&treload, NULL, test_reload_thread, NULL);
    pthread_t tfw; if (getenv("ME_TEST_FWBOOT")) pthread_create(&tfw, NULL, test_fwboot_thread, NULL);
#ifdef ME_BUNDLED
    pthread_t vth; pthread_create(&vth, NULL, viewer_thread, NULL);
#endif

    while (!g_shutdown) {
        if (entry) {
            struct me_fault flt = {0};
            uc_err e = guarded_emu_start(g_th[0].uc, entry, &flt);   /* run the main guest thread */
            entry = 0;
            if (flt.faulted) {                 /* a HOST fault (access violation) -- recover, don't die */
                fprintf(stderr, "magiceyes: GAME CRASHED (host fault) pc=%p addr=%p in %s\n",
                        (void *)flt.pc, (void *)flt.addr, g_cur_game[0] ? g_cur_game : "(unknown)");
                g_fault_addr = flt.addr; g_fault_pending = 1;   /* viewer pops a MessageBox */
                me_report(MR_HOST_FAULT, (long)flt.addr, g_cur_game[0] ? g_cur_game : NULL,
                          (uint32_t)flt.pc);
#ifndef ME_BUNDLED
                g_exit_code = 70;              /* a distinct "crashed" code so the harness tells a
                                                 crash from a clean exit (standalone exits here) */
#endif
            }
            if (!g_reload_path[0]) {           /* game ended on its own (exit/return/error) */
                if (e != UC_ERR_OK && !g_exit && !flt.faulted) {
                    uint32_t pc = gread(UC_ARM_REG_PC), insn = 0, cpsr = gread(UC_ARM_REG_CPSR);
                    uc_mem_read(g_th[0].uc, pc, &insn, 4);
                    fprintf(stderr, "me_unicorn: main emu err %s pc=%08x insn=%08x cpsr=%08x(T=%d) lr=%08x\n",
                            uc_strerror(e), pc, insn, cpsr, (cpsr >> 5) & 1, gread(UC_ARM_REG_LR));
                }
                /* Firmware mode: a game launched from gp2xmenu exited -> return to the menu (real
                   hardware re-execs gp2xmenu on game exit). Only when it was a GAME that ended, not
                   the menu itself (else a menu that quits would loop). */
                if (g_firmware_mode && g_firmware_menu[0] && strcmp(g_cur_game, g_firmware_menu) != 0) {
                    engine_stop_all_threads();
                    snprintf(g_reload_path, sizeof g_reload_path, "%s", g_firmware_menu);
                    g_reload_chdir = 1; g_exit = 0; g_exit_code = 0;
                } else {
#ifdef ME_BUNDLED
                    engine_stop_all_threads();     /* halt lingering workers; keep last frame + window */
                    g_exit = 0; g_exit_code = 0;   /* return to an idle window (menu still open) */
#else
                    break;                         /* standalone: a finished game ends the process */
#endif
                }
            }
        }
        if (g_reload_path[0]) {                /* GPEComp re-exec or File->Open */
            char path[PATH_MAX]; snprintf(path, sizeof path, "%s", g_reload_path); g_reload_path[0] = 0;
            entry = engine_reset_and_load(path);
            if (!entry) fprintf(stderr, "magiceyes: reload of '%s' failed\n", path);
            continue;
        }
        if (!entry) me_usleep(16000);          /* idle: wait for File->Open or the window to close */
    }
    g_shutdown = 1; g_exit = 1;
    if (me_report_active()) me_report_flush_json(NULL);   /* final structured run report */
#ifdef ME_BUNDLED
    if (g_shm) g_shm->quit = 1;            /* engine done -> end the viewer loop, then it exit()s */
    pthread_join(vth, NULL);
#endif
    return g_exit_code;
}
