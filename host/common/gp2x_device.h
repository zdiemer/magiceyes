/* magiceyes — engine-agnostic GP2X (MMSP2) hardware model.
 *
 * This is the GP2X device contract — the shm framebuffer bridge, the MMSP2
 * register semantics (free-running microsecond timer, GPIO buttons, MLC OADR
 * framebuffer scanout), and the /dev/dsp OSS audio ring — factored out of the
 * Unicorn backend so BOTH backends share one implementation:
 *
 *   - the forked qemu-user backend (host/qemu/gp2x.c) maps the guest's device
 *     mmaps to real host RAM via g2h() and hands those host pointers here; a
 *     helper thread calls gp2x_tick() to advance the timer, inject GPIO, and
 *     present the framebuffer. No per-access hook — qemu touches the regs as
 *     plain memory.
 *   - the Unicorn backend has no host pointer into guest RAM, so it bounces
 *     through uc_mem_read/write and calls the Layer-1 primitives directly
 *     (gp2x_timer_us / gp2x_gpio_values / gp2x_dsp_* / gp2x_present_rgb565).
 *
 * Everything here is plain C (stdint + a few POSIX calls for shm/time); no
 * dependency on qemu or Unicorn. Little-endian host assumed (matches gp2xshm.h).
 */
#ifndef GP2X_DEVICE_H
#define GP2X_DEVICE_H

#include <stdint.h>
#include "gp2xshm.h"

/* MMSP2 register byte offsets within the 0xC0000000 block (paeryn SDL map). */
#define GP2X_MMSP2_PHYS   0xC0000000u
#define GP2X_REG_TCOUNT   0x0a00   /* free-running 1us counter (u32) */
#define GP2X_REG_GPIO_A   0x1198   /* lo byte: 8-way stick (active low) */
#define GP2X_REG_GPIO_C   0x1184   /* hi byte: START/SEL/L/R/A/B/X/Y */
#define GP2X_REG_GPIO_VOL 0x1186   /* lo byte: VOL up/down */
#define GP2X_REG_OADRL    0x290e   /* MLC output addr low  (fb phys lo16) */
#define GP2X_REG_OADRH    0x2910   /* MLC output addr high (fb phys hi16) */
#define GP2X_REG_PALLT_A  0x2958   /* MLC STL palette index (write-only port) */
#define GP2X_REG_PALLT_D  0x295a   /* MLC STL palette data  (2 hw/entry: GB then R) */

/* MMSP2 2D "MESG" blitter (graphics engine) — a separate /dev/mem window the game
   mmaps at this phys. Games like Vektar draw entirely through it (the CPU never
   touches fb0/fb1), so we emulate the blit on the run-trigger write. Offsets are
   halfword indices in the paeryn map; byte offsets here. */
#define GP2X_BLIT_PHYS    0xe0020000u
#define GP2X_BLIT_LEN     0x100

/* GP2X framebuffer geometry + the physical addresses we advertise for /dev/fb0
   and /dev/fb1 (via FBIOGET_FSCREENINFO.smem_start). The game writes one of these
   to the MLC OADR register to flip; we resolve it back to the right surface. They
   sit just above the 0x02000000+32MB /dev/mem window so phys lookups are unambiguous. */
#define GP2X_FB_W       320
#define GP2X_FB_H       240
#define GP2X_FB_BPP     16
#define GP2X_FB_LEN     (GP2X_FB_W * GP2X_FB_H * 2)   /* 153600 */
#define GP2X_FB_STRIDE  (GP2X_FB_W * 2)               /* 640 */
#define GP2X_FB0_PHYS   0x04000000u
#define GP2X_FB1_PHYS   0x04040000u

/* Linux fbdev ioctls (GP2X /dev/fb0,fb1). */
#define GP2X_FBIOGET_VSCREENINFO 0x4600
#define GP2X_FBIOPUT_VSCREENINFO 0x4601
#define GP2X_FBIOGET_FSCREENINFO 0x4602
#define GP2X_FBIOPAN_DISPLAY     0x4606

/* OSS /dev/dsp ioctl low bytes (type 'P'); see gp2x_dsp_ioctl. */
#define GP2X_DSP_RESET     0x00
#define GP2X_DSP_SYNC      0x01
#define GP2X_DSP_SPEED     0x02
#define GP2X_DSP_STEREO    0x03
#define GP2X_DSP_GETBLKSIZE 0x04
#define GP2X_DSP_SETFMT    0x05
#define GP2X_DSP_CHANNELS  0x06
#define GP2X_DSP_POST      0x08
#define GP2X_DSP_SETFRAGMENT 0x0a
#define GP2X_DSP_GETFMTS   0x0b
#define GP2X_DSP_GETOSPACE 0x0c
#define GP2X_DSP_GETCAPS   0x0f
#define GP2X_DSP_GETODELAY 0x17

typedef struct gp2x_dev gp2x_dev_t;

/* ---- lifecycle ---- */
gp2x_dev_t *gp2x_open(void);          /* create + attach the shm bridge (NULL on fail) */
void        gp2x_close(gp2x_dev_t *d);
gp2x_shm_t *gp2x_shm(gp2x_dev_t *d);
int         gp2x_quit_requested(gp2x_dev_t *d);  /* viewer asked to close */

/* ---- region registration (the host-pointer / qemu path) ----
   Call once per guest device mmap. `phys` is the mmap offset (== GP2X physical
   address). `host` is the host pointer backing that guest mapping (g2h()).
   The 0xC0000000 region is recognised as the MMSP2 register block. */
void gp2x_map_region(gp2x_dev_t *d, uint32_t phys, void *host, uint32_t len);
/* Register an explicit framebuffer surface (a /dev/fb0 or /dev/fb1 mmap). The
   first becomes fb0, the second fb1 (double-buffer present). */
void gp2x_set_fb(gp2x_dev_t *d, void *host, uint32_t len);

/* ---- service tick (helper-thread cadence, ~1kHz) ----
   Refresh TCOUNT + GPIO in the MMSP2 block and present the active framebuffer.
   Safe to call from a dedicated host thread; does its own ~60fps present cap. */
void gp2x_tick(gp2x_dev_t *d);

/* ---- Layer 1: pure primitives (also used directly by the Unicorn backend) ---- */
/* Current free-running microsecond counter value (for TCOUNT). */
uint32_t gp2x_timer_us(gp2x_dev_t *d);
/* Active-low GPIO register values computed from the shm button bitmap. */
void gp2x_gpio_values(gp2x_dev_t *d, uint16_t *gpio_a, uint16_t *gpio_c,
                      uint16_t *gpio_vol);
/* Present a 320x240 RGB565 image (host pointer, 640-byte stride) to the viewer. */
void gp2x_present_rgb565(gp2x_dev_t *d, const void *src, uint32_t w, uint32_t h);

/* ---- trapped MMSP2 register writes (the write-on-fault path) ----
   The MMSP2 palette is a write-only port (PALLT_D streams 2 halfwords/entry and
   only the last value survives in RAM), so the qemu backend protects that register
   page and forwards each store here to reconstruct the 256-entry palette. `off` is
   the byte offset within the 0xC0000000 block; `val`/`size` are the stored value. */
void gp2x_mmsp2_write(gp2x_dev_t *d, uint32_t off, uint32_t val, int size);
/* Same, for the 2D blitter window (off within the 0xE0020000 block). A write to the
   run/trigger register executes the pending blit into guest framebuffer memory. */
void gp2x_blitter_write(gp2x_dev_t *d, uint32_t off, uint32_t val, int size);

/* OSS /dev/dsp ioctl. `arg` points to a >=16-byte scratch holding the guest's
   argument on entry; on return it holds the reply and *outlen is the number of
   bytes (0/4/16) to copy back to the guest. Returns 0 or -errno. */
int  gp2x_dsp_ioctl(gp2x_dev_t *d, uint32_t cmd, void *arg, uint32_t *outlen);
/* Queue PCM into the audio ring (never blocks; drops oldest if the ring is full). */
uint32_t gp2x_dsp_write(gp2x_dev_t *d, const void *pcm, uint32_t n);
/* Microseconds the caller should sleep so the game's audio output tracks real time
   (reproduces OSS blocking-write pacing without blocking on the viewer). */
uint32_t gp2x_dsp_pace_us(gp2x_dev_t *d);

/* Fill a Linux fb_fix_screeninfo (>=80 bytes) for a 320x240x16 GP2X framebuffer
   whose physical base is `smem_start` (GP2X_FB0_PHYS / GP2X_FB1_PHYS). The game
   reads smem_start and writes it to the MLC OADR to flip. */
void gp2x_fill_fscreeninfo(void *buf, uint32_t smem_start);
/* Fill a Linux fb_var_screeninfo (>=160 bytes): 320x240, RGB565. */
void gp2x_fill_vscreeninfo(void *buf);

#endif /* GP2X_DEVICE_H */
