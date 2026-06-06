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

/* ---- guest virtual memory layout ---- */
#define STACK_TOP   0x80000000u
#define STACK_SIZE  (8u * 1024 * 1024)
#define MMAP_BASE   0x40000000u
#define MMAP_END    0x70000000u
#define PAGE        0x1000u
#define ALIGN_DN(x) ((x) & ~(PAGE - 1))
#define ALIGN_UP(x) (((x) + PAGE - 1) & ~(PAGE - 1))

/* ---- core CPU/engine state ---- */
extern __thread uc_engine *g_uc;   /* per host thread: the calling guest thread's uc */
extern uint32_t g_brk, g_brk_start, g_mmap_next;
extern int g_exit, g_exit_code, g_trace, g_scret;
extern int g_shutdown;        /* real quit: ends helper + viewer (g_exit is the per-run CPU bail) */
extern int g_reloading;       /* a reset/reload is in flight: the helper skips present */
extern int g_reload_chdir;    /* reload should chdir to the new binary's dir (File->Open: yes;
                                 GPEComp re-exec into the temp: no -- keep the game's cwd) */
extern char g_reload_path[PATH_MAX];   /* non-empty -> the main loop resets + loads this binary */
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
void engine_stop_all_threads(void);                 /* join every worker, close its uc (threads.c) */
void mem_reset(void);                               /* free all guest-RAM host backing (mem.c) */
void devices_reset(void);                           /* zero device/fb/audio/MMSP2 state (devices.c) */
void shm_reset_for_new_game(void);                  /* reset per-game shm cursors (devices.c) */
void syscalls_reset(void);                          /* close host fds, free memfds (syscalls.c) */

/* ---- loader.c: resolve folder/.zip/.gpe -> a runnable binary, classify static/dynamic ---- */
const char *resolve_input(const char *in, char *out, size_t cap);  /* NULL + message on error */
int classify_elf(const char *path);   /* 0 = static ET_EXEC ok, 1 = dynamic (deferred), -1 = error */

/* ---- path redirect: /mnt/tmp,/tmp -> host temp on Windows (identity on Linux), syscalls.c ---- */
void rewrite_guest_path(const char *in, char *out, size_t cap);
void me_host_tmpdir(char *out, size_t cap);   /* host scratch dir (created on first use) */
void map_region(uint32_t addr, uint32_t size, uint32_t perms);   /* host-backed (mem.c) */
void ensure_mapped(uc_engine *u, uint32_t addr, uint32_t size, int perms);
void *guest_to_host(uint32_t gaddr);   /* host ptr backing a guest addr (host-atomic ops) */
void uc_map_all(uc_engine *u);   /* map all guest regions into a fresh uc (thread factory) */
uint32_t gread(uint32_t reg);
void gwrite(uint32_t reg, uint32_t v);

/* ---- elf.c ---- */
uint32_t load_elf(const char *path);
uint32_t setup_stack(int argc, char **argv);
extern uint32_t g_at_base;   /* AT_BASE: interpreter (ld.so) load base; 0 = static binary */
extern int g_is_dynamic;     /* 1 once load_elf has loaded a dynamically-linked title */

/* ---- syscalls.c: device rootfs for the dynamic-linker path ---- */
void me_rootfs_init(void);   /* pick the rootfs (ME_GP2X_ROOTFS or a default); idempotent */
int  me_rootfs_resolve(const char *guest, char *out, size_t cap);  /* 1 = host path in out */

/* ---- devices.c: GP2X/Wiz device model + shm bridge ---- */
enum { DEV_FB = 1, DEV_MEM, DEV_GPIO, DEV_DSP, DEV_MIXER, DEV_TTY, DEV_I2C, DEV_SHMFB, DEV_OTHER };
#define DEVFD_BASE 0x10000000   /* far above real host fds (avoid aliasing) */
struct memmap { uint32_t phys, guest, len; };

extern gp2x_shm_t *g_shm;
extern int g_devtype[64], g_devn;
extern int g_fbnum[64];   /* per-slot fb index for DEV_FB fds */
extern struct memmap g_mem[64];
extern int g_nmem;
extern uint32_t g_mmsp2_guest, g_fb_guest, g_fb_guest2;
extern uint32_t g_blit_guest;   /* guest base of the 0xe0020000 blitter window */
extern int g_oadr_driven;   /* game drives present via OADR writes -> async present off */
extern int g_frame_ready;   /* OADR write -> helper thread presents this frame (off-render-thread) */
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
void aud_drain(void);
uint32_t aud_free(void);
long dsp_write(uint32_t gbuf, uint32_t n);
uint32_t dsp_pace_us(void);
long dsp_ioctl(uint32_t cmd, uint32_t arg);
long fb_ioctl(int fd, uint32_t cmd, uint32_t arg);   /* FBIOGET_*SCREENINFO / PAN_DISPLAY */
long gpio_read(uint32_t gbuf, uint32_t n);           /* /dev/GPIO joystick button word */
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
#define PIPEFD_R 0x10000100
#define PIPEFD_W 0x10000101
#define FAKESOCK_FD 0x30000001   /* glibc syslog's AF_UNIX socket (datagrams discarded) */

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
int futex_wait(uint32_t uaddr, uint32_t val);
int futex_wake(uint32_t uaddr, int n);
void futex_wake_all(void);   /* broadcast every wait-queue (teardown: free blocked threads) */
void sigsuspend_wait(void);
void threads_init(void);
int thread_alloc(void);
void dump_threads(const char *why);
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
long dev_mmap(int type, uint32_t addr, uint32_t len, uint32_t flags, uint32_t phys);
long shmfb_mmap(uint32_t len);   /* alias the shim's gp2x_fb mmap onto the engine's g_shm */
void mem_register_external(uint32_t guest, uint32_t len, void *host);
bool mem_invalid_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                    int size, int64_t value, void *user);

/* ---- fpa.c (FPA float-unit emulation: device libstdc++/libm use legacy FPA insns) ---- */
extern __thread int g_fpa_resume;   /* set by the invalid-insn hook -> guarded_emu_start restarts */
bool fpa_invalid_cb(uc_engine *uc, void *user);   /* UC_HOOK_INSN_INVALID */
void fpa_reset(void);

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
/* Recovery handoff to the viewer (bundle only): the engine flags a crashed game; the viewer
   thread polls and pops a MessageBox. (Not in gp2x_shm_t -- that ABI is shared with the guest.) */
extern volatile int g_fault_pending;
extern volatile uintptr_t g_fault_addr;
extern char g_cur_game[PATH_MAX];      /* the binary the engine is actually running */
extern char g_exe_dir[PATH_MAX];       /* dir of our own executable (rootfs default search) */

#endif /* MAGICEYES_ENGINE_H */
