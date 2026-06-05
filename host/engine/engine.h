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

/* ---- core CPU/engine state (defined in main.c; moves to cpu.c later) ---- */
extern uc_engine *g_uc;
extern uint32_t g_brk, g_brk_start, g_mmap_next;
extern int g_exit, g_exit_code, g_trace;
extern unsigned long g_n_rd, g_n_wr, g_n_fault;   /* hook-call profiling */

/* guest mmap() flag bits used by do_mmap */
#define GMAP_FIXED 0x10u
#define GMAP_ANON  0x20u

void die(const char *m, uc_err e);
void map_region(uint32_t addr, uint32_t size, uint32_t perms);   /* host-backed (mem.c) */
void uc_map_all(uc_engine *u);   /* map all guest regions into a fresh uc (thread factory) */
uint32_t gread(uint32_t reg);
void gwrite(uint32_t reg, uint32_t v);

/* ---- elf.c ---- */
uint32_t load_elf(const char *path);
uint32_t setup_stack(int argc, char **argv);

/* ---- devices.c: GP2X/Wiz device model + shm bridge ---- */
enum { DEV_FB = 1, DEV_MEM, DEV_GPIO, DEV_DSP, DEV_MIXER, DEV_TTY, DEV_OTHER };
#define DEVFD_BASE 0x10000000   /* far above real host fds (avoid aliasing) */
struct memmap { uint32_t phys, guest, len; };

extern gp2x_shm_t *g_shm;
extern int g_devtype[64], g_devn;
extern struct memmap g_mem[64];
extern int g_nmem;
extern uint32_t g_mmsp2_guest, g_fb_guest, g_fb_guest2;
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
double host_now(void);
void aud_drain(void);
uint32_t aud_free(void);
long dsp_write(uint32_t gbuf, uint32_t n);
long dsp_ioctl(uint32_t cmd, uint32_t arg);
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

struct thread {
    uc_context *ctx;
    int state, block, tid, ppid;
    uint32_t tls;
    uint32_t futex_addr;
    uint32_t ctid;
    uint64_t sig_pending, sig_blocked;
    uint64_t susp_oldmask;
    int susp_active;
    int has_sigsave;
    uint32_t sigsave[17];
    double wake_deadline;
    int enoent_streak;
    uint32_t last_pc;
};
struct sigact { uint32_t handler, flags, restorer; uint64_t mask; };
struct snap { uint64_t begin; uint32_t len; uint8_t *data; };
struct freereg { uint32_t addr, len; };

/* ---- threads.c (cooperative scheduler — to be rewritten for native threads) ---- */
extern struct thread g_th[MAXTH];
extern int g_nth, g_cur, g_next_tid, g_switched;
extern struct sigact g_sigact[65];
extern const int g_sregs[17];
extern int g_threaddump;
void wake_sleepers(void);
int sched_pick(void);
void sched_switch_to(int j);
void block_current(int reason);
void dump_threads(const char *why);
void deliver_signals(void);
long send_sig(int pid, int sig);

/* ---- syscalls.c (syscall shim + synchronous fork + in-engine pipe) ---- */
extern uc_context *g_fork_ctx;
extern struct snap g_snap[2048];
extern int g_nsnap, g_forked;
extern uint32_t g_child_pid;
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
bool mem_invalid_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                    int size, int64_t value, void *user);

/* ---- cpu.c (SVC entry, hooks, preemption timer) ---- */
extern volatile int g_timer_run;
extern unsigned g_slice_us;
void intr_cb(uc_engine *uc, uint32_t intno, void *user);
void *timer_thread(void *arg);

#endif /* MAGICEYES_ENGINE_H */
