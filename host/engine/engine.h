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
void map_region(uint32_t addr, uint32_t size, uint32_t perms);
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

#endif /* MAGICEYES_ENGINE_H */
