/* magiceyes Unicorn engine — shared internal contract.
 *
 * The engine is being split out of the old monolithic host/unicorn/me_unicorn.c into
 * focused modules under host/engine/ (main, elf, mem, devices, syscalls, threads, cpu)
 * so the native-threads rewrite lands in a clean threads.c. This header declares the
 * state + functions shared across those modules. (Migration in progress: globals/protos
 * are added here as each module is carved out.)
 */
#ifndef MAGICEYES_ENGINE_H
#define MAGICEYES_ENGINE_H

#include <unicorn/unicorn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>
#include <elf.h>
#include "gp2xshm.h"
#include "report.h"   /* structured run telemetry (me_report / me_report_flush_json) */
#include "ctl.h"      /* debug control channel (ME_CTL); compiled out of release bundles */
#include "dbg.h"      /* pause / step / breakpoints (inert until armed) */
#include "symbols.h"  /* addr <-> symbol, where a symbol table survives */
#include "state.h"    /* savestates: capture/restore of the whole guest machine */
#include "paths.h"    /* portable, user-configurable storage roots (settings/firmware/cache) */

/* ---- guest virtual memory layout ---- */
#define STACK_TOP   0x80000000u
#define STACK_SIZE  (8u * 1024 * 1024)
#define MMAP_BASE   0x40000000u
#define MMAP_END    0x70000000u
#define PAGE        0x1000u
#define PRAM_BASE   0x02000000u           /* GP2X upper physical RAM (shared 920/940 area) */
#define PRAM_SIZE   0x02000000u           /* 32MB: phys 0x02000000 .. 0x04000000 */
/* The GP2X kernel's fbdev memory lives in upper RAM: fb0 at 0x03101000 (also the boot-time MLC
   scanout address), fb1 the page after (0x25800 = 320*240*2). minlib-style titles hard-code these
   and draw via /dev/mem, so fbdev must be backed by pram at the REAL phys (one physical RAM). */
#define GP2X_FB0_PHYS 0x03101000u
#define GP2X_FB1_PHYS (GP2X_FB0_PHYS + 320u * 240u * 2u)
#define ALIGN_DN(x) ((x) & ~(PAGE - 1))
#define ALIGN_UP(x) (((x) + PAGE - 1) & ~(PAGE - 1))

/* ---- core CPU/engine state ---- */
extern __thread uc_engine *g_uc;   /* per host thread: the calling guest thread's uc */
extern uint32_t g_brk, g_brk_start, g_mmap_next;
extern int g_exit, g_exit_code, g_trace, g_scret;
extern int g_shutdown;        /* real quit: ends helper + viewer (g_exit is the per-run CPU bail) */
extern FILE *g_log;           /* ME_LOGFILE diagnostic sink (robust on the -mwindows bundle where
                                 stderr redirection is fragile); NULL -> use stderr. */
#define DIAG (g_log ? g_log : stderr)
extern int g_fwlog;                       /* ME_FWLOG: firmware/viewer debug logging to DIAG */
void me_log(const char *fmt, ...);        /* logs to DIAG when g_fwlog (visible in the bundle log) */
extern int g_reloading;       /* a reset/reload is in flight: the helper skips present */
extern int g_reload_chdir;    /* reload should chdir to the new binary's dir (File->Open: yes;
                                 GPEComp re-exec into the temp: no -- keep the game's cwd) */
extern char g_reload_path[PATH_MAX];   /* non-empty -> the main loop resets + loads this binary */
extern int g_firmware_mode;            /* booted a device gp2xmenu: return to it between games */
extern char g_firmware_menu[PATH_MAX]; /* gp2xmenu path to re-enter after a game exits */
extern __thread int g_setpc;   /* a syscall set PC (signal entry/sigreturn): skip R0 write */
extern unsigned long g_n_rd, g_n_wr, g_n_fault;   /* hook-call profiling */

/* guest mmap() flag bits used by do_mmap */
#define GMAP_FIXED 0x10u
#define GMAP_ANON  0x20u

void die(const char *m, uc_err e);
void me_usleep(unsigned us);   /* sleep, low CPU (Windows usleep is ~15ms-granular) */
void me_platform_init(void);   /* one-time host setup (Windows: timer res + no EcoQoS throttling) */

/* ---- reset/reload (shared by GPEComp re-exec + File->Open hot reload) ---- */
void engine_request_reload(const char *host_path);  /* viewer thread: stop+reset+load (threads.c) */
void engine_reload_in_syscall(const char *host_path);  /* execve chain-load (g_biglock held) */
void engine_stop_all_threads(void);                 /* join every worker, close its uc (threads.c) */
void mem_reset(void);                               /* free all guest-RAM host backing (mem.c) */
void devices_reset(void);                           /* zero device/fb/audio/MMSP2 state (devices.c) */
void shm_reset_for_new_game(void);                  /* reset per-game shm cursors (devices.c) */
void syscalls_reset(void);                          /* close host fds, free memfds (syscalls.c) */

/* ---- loader.c: resolve folder/.zip/.gpe -> a runnable binary, classify static/dynamic ---- */
const char *resolve_input(const char *in, char *out, size_t cap);  /* NULL + message on error */
extern char g_launch_args[8][256];  /* launcher-script args to forward to the guest argv */
extern int  g_launch_nargs;
/* Content identity (loader.c). g_game_key/g_game_key_hex are the running title's 64-bit FNV-1a,
   set for EVERY title on each resolve_input (0 if the binary could not be re-read). Savestates
   use it to refuse loading into a different game; the GPEComp cache uses the same hash to key a
   decompressed payload by content rather than by filename. */
uint64_t me_content_key64(const uint8_t *buf, size_t len);
extern uint64_t g_game_key;
extern char     g_game_key_hex[17];
int classify_elf(const char *path);   /* 0 = static ET_EXEC ok, 1 = dynamic (deferred), -1 = error */
int read_elf_interp(const char *path, char *out, size_t cap);  /* PT_INTERP string; 1 if found */

/* ---- path redirect: /mnt/tmp,/tmp -> host temp on Windows (identity on Linux), syscalls.c ---- */
void rewrite_guest_path(const char *in, char *out, size_t cap);
void me_host_tmpdir(char *out, size_t cap);   /* host scratch dir (created on first use) */
void map_region(uint32_t addr, uint32_t size, uint32_t perms);   /* host-backed (mem.c) */
void ensure_mapped(uc_engine *u, uint32_t addr, uint32_t size, int perms);
void *guest_to_host(uint32_t gaddr);   /* host ptr backing a guest addr (host-atomic ops) */
int  mem_nreg(void);                   /* region-registry occupancy (diag) */
int read_guest(void *dst, uint32_t gaddr, uint32_t len); /* copy guest bytes (spans regions); 0 ok, -1 unmapped */
void uc_map_all(uc_engine *u);   /* map all guest regions into a fresh uc (thread/940 factory) */
void kuser_populate(uc_engine *u);   /* write the kuser helper trampoline code into u's page */
/* shared GP2X physical RAM (upper memory): one host backing, /dev/mem mmaps are windows into it */
extern uint8_t *g_pram;
int   phys_in_pram(uint32_t phys, uint32_t len);
long  pram_map(uint32_t phys, uint32_t len, uint32_t hint);   /* map a window; returns guest addr */
void *pram_host(uint32_t phys);                               /* host ptr for a phys addr, or NULL */
void *pram_ensure(void);                                      /* allocate the pool (no uc mapping) */

/* ---- me940.c: ARM940T second core (gpu940 etc.) ---- */
void me940_reg_write(uint32_t off, uint32_t val);  /* trap of the 940 control regs (0x904/0x3b4x) */
void me940_start(int bank);
void me940_stop(void);                             /* MUST run before mem_reset frees g_pram */
int  me940_active(void);
int  me940_load_and_start(const char *fw);         /* inline load940: firmware -> shared RAM, start */
void me940_selftest(const char *fw);               /* ME_940_SELFTEST standalone core check */
void me940_scan_fb(void);                           /* ME_940_SCANFB: locate gpu940's video buffer */
extern char g_940_firmware[];                      /* set by the loader if a title runs load940 */
extern char g_launcher_dir[];                      /* followed launcher script's dir (lib search) */
extern char g_launch_cwd[];                        /* followed script's effective run dir */
extern char g_script_libdirs[];                    /* runtime dirs from script LD_LIBRARY_PATH= */
uint32_t gread(uint32_t reg);
void gwrite(uint32_t reg, uint32_t v);

/* ---- elf.c ---- */
uint32_t load_elf(const char *path);
uint32_t setup_stack(int argc, char **argv);
extern uint32_t g_at_base;   /* AT_BASE: interpreter (ld.so) load base; 0 = static binary */
extern int g_is_dynamic;     /* 1 once load_elf has loaded a dynamically-linked title */
extern int g_eabi;           /* current syscall ABI: 1 = EABI (svc #0), 0 = legacy OABI */
extern int g_caanoo_dev;     /* 1 if the loaded binary is a Caanoo title (Pollux/DGE sonames) */
extern int g_device;         /* viewer-header device: 0=GP2X 1=GP2X Wiz 2=GP2X Caanoo (set in load_elf) */

/* ---- GL render offload: glgpu.c dispatches glr_* to the host-GPU (OpenGL) or software (glraster.c)
   backend; the shim forwards draws via the ME_NR_GL_* syscalls ---- */
void glr_resize(int w, int h);
void glr_clear(uint32_t packed_rgba);
void glr_draw(uint32_t desc_guest_ptr);   /* struct gl_draw in guest memory */
void glr_present(void);
int  gl_owns_screen(void);                /* a GLES title is presenting -> gate the 2D fb present */

/* ---- syscalls.c: device rootfs for the dynamic-linker path ---- */
void me_rootfs_init(void);   /* pick the rootfs (ME_GP2X_ROOTFS or a default); idempotent */
int  me_rootfs_resolve(const char *guest, char *out, size_t cap);  /* 1 = host path in out */
int  me_rootfs_select(const char *interp);  /* pick the rootfs holding this PT_INTERP (so.2 vs .3) */
void me_rootfs_set(const char *dir);        /* firmware boot: pin a specific rootfs; NULL = unpin */
const char *me_rootfs_current(int *pinned); /* the active rootfs, for savestate identity */

/* ---- firmware.c: locate / install a device firmware ---- */
int  me_writable_root(char *out, size_t cap);  /* configured Firmware dir (me_paths, portable default); 1=ok */
int  me_firmware_paths(const char *device, char *rootfs, char *menu, size_t cap);  /* 1 if found */
int  me_firmware_install(const char *file, const char *device);  /* stage a .zip/.img; 0 = ok */
void me_firmware_sync_overlays(void);  /* heal shim overlay on already-installed OABI firmware */
int  me_firmware_boot_request(const char *device);  /* GUI: pin rootfs + reload its gp2xmenu; 1=ok */

/* ---- devices.c: GP2X/Wiz device model + shm bridge ---- */
enum { DEV_FB = 1, DEV_MEM, DEV_GPIO, DEV_DSP, DEV_MIXER, DEV_TTY, DEV_I2C, DEV_SHMFB, DEV_OTHER,
       DEV_INPUT_EV, DEV_INPUT_JS,     /* Linux input subsystem: /dev/input/event* and /dev/input/js* */
       DEV_NULL,                       /* /dev/null: read EOF, write discard */
       DEV_TOUCH };                    /* /dev/touchscreen/wm97xx: F200 resistive panel */
long ts_read(uint32_t buf, uint32_t len);   /* F200 touchscreen: TS_EVENT samples from shm touch state */
int  ts_pending(void);
/* Synthetic guest fds MUST stay below FD_SETSIZE (1024): games put device fds in fd_sets
   (glibc's FD_SET writes bits[fd/32] with no bounds check — an fd like 0x10000002 smashed
   memory 8M words past the stack fd_set, and select() on a device could never be modelled).
   Real GP2X fds are tiny. Kept well above any realistic host fd (guest files pass through
   host fds; HOSTFD_MAX is 512) so the ranges cannot alias. */
#define DEVFD_BASE 900          /* 64 device slots: 900..963 */
struct memmap { uint32_t phys, guest, len; };

extern gp2x_shm_t *g_shm;
extern int g_devtype[64], g_devn;
extern int g_fbnum[64];   /* per-slot fb index for DEV_FB fds */
extern struct memmap g_mem[64];
extern int g_nmem;
extern uint32_t g_mmsp2_guest, g_fb_guest, g_fb_guest2;
extern int g_fb_from_devmem;   /* g_fb_guest is the /dev/mem window over the default scanout
                                  (GP2X_FB0_PHYS), not an fbdev mmap -- an fbdev mmap replaces it */
extern int g_flip_active; extern uint32_t g_flip_guest;   /* present lock (set by 940 MLC scanout too) */
extern uint32_t g_fb_stride;   /* present row stride in bytes (gpu940 video buffers are pow2-wide) */
extern int g_fb_bpp;           /* present source depth: 16 RGB565 (default) or 32 XRGB (gpu940) */
extern uint32_t g_fb_xoff;     /* x pixel offset into each present row (gpu940 centering) */
extern uint32_t g_blit_guest;   /* guest base of the 0xe0020000 blitter window */
extern int g_caanoo_bpp; extern uint32_t g_caanoo_pitch;   /* Pollux MLC layer depth (bytes/px) + pitch */
extern int g_oadr_driven;   /* game drives present via OADR writes -> async present off */
extern int g_frame_ready;   /* OADR write -> helper thread presents this frame (off-render-thread) */
extern int g_present_stale; /* helper: flip-synced present starved >250ms -> present_active may
                               auto-pan the flip-lock to the fb page the game actually draws */
extern uint32_t g_aud_freq, g_aud_ch, g_aud_bits;
extern double g_aud_t0;
extern int g_aud_on;

int dev_open(const char *path);
void dev_close(int fd);
int dev_type(int fd);
void shm_setup(void);
void record_memmap(uint32_t phys, uint32_t guest, uint32_t len);
int phys_to_guest(uint32_t phys, uint32_t *out);
void present_guest(uint32_t g);
void present_fb(uint32_t phys);
int buf_score(uint32_t g);
uint32_t buf_hash(uint32_t g);
void present_active(void);
void gp2x_cacheflush(uint32_t guest);   /* cacheflush(r3=fb base) -> present that buffer */
double host_now(void);
/* The GUEST's clock: host time plus a constant skew, 0 except after a savestate restore. Every
   time value a game can observe (gettimeofday, clock_gettime, time, times, alarm, TCOUNT, the
   audio/DSP pacing epochs) must read through these, so a restored state resumes at the instant
   it was saved instead of jumping forward. Host pacing stays on host_now(). See devices.c. */
extern double g_clock_skew;
double guest_now(void);
void   guest_timeofday(struct timeval *tv);
void aud_drain(void);
uint32_t aud_free(void);
long dsp_write(uint32_t gbuf, uint32_t n);
uint32_t dsp_pace_us(void);
long dsp_ioctl(uint32_t cmd, uint32_t arg);
long fb_ioctl(int fd, uint32_t cmd, uint32_t arg);   /* FBIOGET_*SCREENINFO / PAN_DISPLAY */
long gpio_read(uint32_t gbuf, uint32_t n);           /* /dev/GPIO joystick button word (read) */
long gpio_ioctl(uint32_t cmd, uint32_t arg);         /* /dev/GPIO GPH SDL_OpenGPIO ioctl button query */
/* input.c: reusable Linux input subsystem (evdev + joystick) fed from shm -> the analog stick +
   buttons for titles that read /dev/input/event* or /dev/input/js* (e.g. the Caanoo firmware menu). */
int  input_classify(const char *path);               /* -> DEV_INPUT_EV / DEV_INPUT_JS / 0 */
struct stat;
int  input_fake_node(const char *path, struct stat *s); /* stat() event0/js0 as char devs (SDL scan) */
void input_open(int fd, int type);                   /* seed per-fd state at open */
long input_read(int fd, uint32_t gbuf, uint32_t n);  /* emit evdev/js events for state changes */
long input_ioctl(int fd, uint32_t cmd, uint32_t arg);/* EVIOCG / JSIOCG capability queries */
int  input_pending(int fd);                          /* poll()/select(): is an input event ready? */
long i2c_read(uint32_t gbuf, uint32_t n);            /* /dev/i2c-0 handset serial (read) */
long i2c_ioctl(uint32_t cmd, uint32_t arg);          /* /dev/i2c-0 I2C_RDWR serial */
void gp2x_mmio_palette(uint32_t off, uint32_t val);  /* MLC palette port capture (MMSP2 page) */
void gp2x_blitter_write(uint32_t off, uint32_t val, int size);   /* MESG 2D blitter (0xe0020000) */
void blitter_write_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                      int size, int64_t value, void *user);
void blitter_read_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                     int size, int64_t value, void *user);
void mmsp2_write_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                    int size, int64_t value, void *user);
void mmsp2_read_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                   int size, int64_t value, void *user);

/* ---- shared types/macros: threads, signals, fork, mem ---- */
#define MAXTH 32
enum { TH_FREE = 0, TH_RUN, TH_BLOCKED, TH_SLEEPING, TH_DEAD };
enum { BLK_NONE = 0, BLK_FUTEX, BLK_SIG };
#define ME_CLONE_VM            0x00000100
#define ME_CLONE_PARENT_SETTID 0x00100000
#define ME_CLONE_CHILD_CLEARTID 0x00200000
#define ME_CLONE_CHILD_SETTID  0x01000000
#define ME_CLONE_SETTLS        0x00080000
#define SIG_TRAMP 0xffff0f00u   /* restorer trampoline in the kuser page */
#define PIPEFD_R 964
#define PIPEFD_W 965
#define FAKESOCK_FD 1000         /* glibc syslog's AF_UNIX socket (datagrams discarded).
                                    (Also un-aliases it from the dirfd range: it used to be
                                    0x30000001 = DIRFD_BASE+1.) */

struct thread {
    uc_engine *uc;          /* this guest thread's CPU */
    pthread_t th;           /* host thread (0 for the main thread) */
    int state, tid, ppid;
    uint32_t entry_pc, sp, tls, ctid;
    uint64_t sig_pending, sig_blocked, susp_oldmask;
    int susp_active, has_sigsave;
    uint32_t sigsave[17];   /* r0..r15 + cpsr, restored by (rt_)sigreturn */
    int enoent_streak;      /* consecutive failed opens -> back off (music worker) */
    uint32_t last_pc;       /* diagnostics */
    /* Syscall-in-flight record, filled on every SVC entry (intr_cb) and cleared on return. Two
       users: the thread dumps, where "blocked in nr=240" beats a bare PC when triaging a hang;
       and the savestate quiesce, which must know whether a thread still off-CPU at the pause
       deadline sits in a RESTARTABLE wait (futex/sigsuspend/nanosleep -- rewind PC to the SVC and
       re-execute it after the restore) or in something that already committed side effects (in
       which case the save is refused, naming the syscall, rather than writing a torn file). */
    uint32_t sc_nr, sc_arg[6], sc_pc;   /* sc_pc = PC AFTER the SVC instruction */
    int      sc_active;                 /* 1 between SVC entry and the R0 write */
    double   fpa[8];        /* FPA f0-f7 register file (fpa.c) -- see the note there on why this
                               lives here rather than in __thread storage */
    uint32_t fpsr;          /* FPA status register (WFS/RFS) */
};
struct sigact { uint32_t handler, flags, restorer; uint64_t mask; };
struct snap { uint64_t begin; uint32_t len; uint8_t *data; };
struct freereg { uint32_t addr, len; };

/* ---- threads.c (native host threads) ---- */
extern pthread_mutex_t g_biglock;   /* serialises the syscall + device layer */
/* Per-thread "I hold g_biglock" flag so the crash guard (guard.c) can release the lock if a
   host fault hits while a guest thread is mid-syscall (else every other thread deadlocks).
   EVERY g_biglock lock/unlock MUST go through these macros to keep the flag accurate. */
extern __thread int g_holds_biglock;
#define BIGLOCK_LOCK()   do { pthread_mutex_lock(&g_biglock);   g_holds_biglock = 1; } while (0)
#define BIGLOCK_UNLOCK() do { g_holds_biglock = 0; pthread_mutex_unlock(&g_biglock); } while (0)
extern __thread struct thread *g_self;   /* the calling host thread's guest-thread record */
extern struct thread g_th[MAXTH];
extern int g_nth, g_next_tid;
extern struct sigact g_sigact[65];
extern const int g_sregs[17];
extern int g_threaddump;
void uc_hook_std(uc_engine *u);
uc_engine *uc_new_thread(void);
void *thread_entry(void *arg);
int futex_wait(uint32_t uaddr, uint32_t val, const struct timespec *abstime);
int futex_wake(uint32_t uaddr, int n);
void futex_wake_all(void);
void engine_wake_sigwaiters(void);   /* broadcast the sigsuspend queue (teardown + debugger pause) */   /* broadcast every wait-queue (teardown: free blocked threads) */
void sigsuspend_wait(void);
void threads_init(void);
int thread_alloc(void);
void dump_threads(const char *why);
/* Machine-readable twin of dump_threads (ME_THREADDUMP_JSON), + the shared bl/blx-validated
   stack-scan backtrace both of them use. */
void dump_threads_json(const char *path, const char *why);
int  th_backtrace(struct thread *t, uint32_t *out, int cap);

/* Snapshot of one guest memory region (mem.c owns the registry; see mem_regions). */
struct me_region { uint32_t addr, len; int perms, external; };
int mem_regions(struct me_region *out, int cap);
int write_guest(const void *src, uint32_t gaddr, uint32_t len);

/* Outermost lock: serialises the helper thread's present against reload teardown, and is taken by
   the control channel around any guest-memory read for the same reason (defined in main.c). */
extern pthread_mutex_t g_present_lock;

/* MLC palette reconstructed from the write-only hardware port (gp2x_device.c). The only place an
   8-bit title's palette is observable -- it never survives in guest RAM. */
extern uint8_t g_pal[256][3];
extern int g_pal_have;

/* Run fn(arg) with the host-fault guard armed on THIS thread, releasing g_reg_lock if it faults.
   Same shape as guarded_present; used by the control channel, which touches guest memory from a
   non-guest thread and must not take the process down on a stale pointer. */
int guarded_ctl(void (*fn)(void *), void *arg);
void deliver_signals(void);
long send_sig(int pid, int sig);

/* ---- syscalls.c (syscall shim + synchronous fork + in-engine pipe) ---- */
extern uc_context *g_fork_ctx;
extern struct snap g_snap[2048];
extern int g_nsnap, g_forked;
extern uint32_t g_child_pid;
extern struct sigact g_sigact_fork[65];   /* sig dispositions saved across a synchronous fork */
extern uint64_t g_fork_sigblocked;
extern struct thread *g_fork_thread;      /* thread running the inline fork child */
extern uint8_t *g_pipebuf;
extern uint32_t g_pipe_cap, g_pipe_w, g_pipe_r;
void pipe_put(const uint8_t *p, uint32_t n);
void read_cstr(uint32_t gaddr, char *out, size_t cap);
void fill_oabi_stat(uint32_t gbuf, struct stat *hs);
void fill_stat64(uint32_t gbuf, struct stat *hs);
long sys_dispatch(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2,
                  uint32_t a3, uint32_t a4, uint32_t a5);

/* ---- mem.c (guest mmap/brk allocator + lazy fault map) ---- */
extern struct freereg g_mfree[256];
extern int g_nmfree;
long do_mmap(uint32_t addr, uint32_t len, uint32_t flags, int fd, uint64_t off);
long do_mremap(uint32_t olda, uint32_t oldl, uint32_t newl, uint32_t flags);
long dev_mmap(int type, uint32_t addr, uint32_t len, uint32_t flags, uint32_t phys, int fbno);
int  dev_fbno(int fd);   /* fb device number (0=/dev/fb0, 1=/dev/fb1) for a DEV_FB fd */
long shmfb_mmap(uint32_t len);   /* alias the shim's gp2x_fb mmap onto the engine's g_shm */
void mem_register_external(uint32_t guest, uint32_t len, void *host);
bool mem_invalid_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                    int size, int64_t value, void *user);

/* ---- fpa.c (FPA float-unit emulation: device libstdc++/libm use legacy FPA insns) ---- */
extern __thread int g_fpa_resume;   /* set by the invalid-insn hook -> guarded_emu_start restarts */
bool fpa_invalid_cb(uc_engine *uc, void *user);   /* UC_HOOK_INSN_INVALID */
void fpa_reset(void);
void   fpa_write(int r, double v);
double fpa_read(int r);

/* OABI libm float-return shim (oabi_libm.c). */
void oabi_libm_scan(const uint8_t *buf, long sz);   /* parse game ELF, record double-libm PLT stubs */
void oabi_libm_register(uc_engine *u);              /* patch stubs -> bx lr (once) + hook them per uc */

/* ---- cpu.c (SVC entry, hooks, preemption timer) ---- */
extern volatile int g_timer_run;
extern unsigned g_slice_us;
void intr_cb(uc_engine *uc, uint32_t intno, void *user);
void *timer_thread(void *arg);

/* ---- guard.c: catch genuine HOST faults (Win SEH / Linux SIGSEGV+SIGBUS) so a bad game
   never takes the whole GUI process down. (Guest unmapped accesses are handled by Unicorn's
   mem_invalid_cb and never reach here.) ---- */
struct me_fault { int faulted; uintptr_t pc; uintptr_t addr; };
void guard_init(void);                 /* install handlers (Linux); Windows: timer/no-op */
int  guarded_emu_start(uc_engine *uc, uint32_t entry, struct me_fault *f);  /* -1 on fault */
int  guarded_present(void);            /* 0 ok, -1 if present faulted (frame skipped) */
void guard_release_biglock(void);      /* unlock g_biglock iff the faulting thread held it */
void heap_check(const char *tag);      /* ME_HEAPCHECK: HeapValidate the process heap (Win) */
void guard_release_reglock(void);      /* unlock g_reg_lock iff the faulting thread held it */
/* Recovery handoff to the viewer (bundle only): the engine flags a crashed game; the viewer
   thread polls and pops a MessageBox. (Not in gp2x_shm_t -- that ABI is shared with the guest.) */
extern volatile int g_fault_pending;
extern volatile uintptr_t g_fault_addr;
extern char g_cur_game[PATH_MAX];      /* the binary the engine is actually running */
extern char g_exe_dir[PATH_MAX];       /* dir of our own executable (rootfs default search) */

/* Persistent per-game saving. g_game_root = the title's REAL asset dir (the .gpe's folder,
   which may differ from where a GPEComp payload was decompressed); g_save_root =
   <exe_dir>/saves/<gamekey>/, a portable writable overlay games' writes are redirected into.
   Set by me_save_set_game() (loader); survive reloads (overwritten on each resolve_input). */
extern char g_game_root[PATH_MAX];
extern char g_save_root[PATH_MAX];
void me_save_set_game(const char *elf_path);   /* derive g_game_root + g_save_root from the .gpe/ELF */
void syscalls_flush_all(void);                 /* fsync every tracked host fd (flush-on-quit) */
/* Park an engine-owned host fd above the guest fd range, so a savestate restore can put every
   guest fd back on its original number without having to evict us. Best effort: returns the new
   fd, or the original one unchanged if it could not be moved. See syscalls.c. */
int  me_fd_reserve_high(int fd);
FILE *me_fopen_reserved(const char *path, const char *mode);   /* fopen + me_fd_reserve_high */

#endif /* MAGICEYES_ENGINE_H */
