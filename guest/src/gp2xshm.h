/* Shared-memory contract between the ARM fake-SDL shim (inside qemu-user)
   and the native x86 SDL2 viewer. Both processes mmap the same /dev/shm object.
   Both ends are little-endian (ARM guest on x86 host + x86 host), so the
   struct maps 1:1. Keep this header dependency-free (stdint only). */
#ifndef GP2XSHM_H
#define GP2XSHM_H

#include <stdint.h>

#define GP2XSHM_NAME   "/gp2x_fb"        /* shm_open name */
#define GP2XSHM_MAGIC  0x32585032u       /* "2XP2" little-endian tag */
#define GP2XSHM_MAXW   1024
#define GP2XSHM_MAXH   768
/* framebuffer is always presented to the viewer as RGB565 (converted on Flip) */
#define GP2XSHM_FBBYTES (GP2XSHM_MAXW * GP2XSHM_MAXH * 2)

/* GP2X/Wiz joystick button indices (the GP2X SDL button numbering). */
enum {
    GP2X_UP = 0, GP2X_UPLEFT, GP2X_LEFT, GP2X_DOWNLEFT,
    GP2X_DOWN, GP2X_DOWNRIGHT, GP2X_RIGHT, GP2X_UPRIGHT,
    GP2X_START, GP2X_SELECT, GP2X_L, GP2X_R,
    GP2X_A, GP2X_B, GP2X_X, GP2X_Y,
    GP2X_VOLUP, GP2X_VOLDOWN, GP2X_CLICK,
    GP2X_NBUTTONS
};

#define GP2XSHM_ARING (1u << 19)   /* 512 KB audio ring (~3s @ 44k stereo s16) */

typedef struct {
    uint32_t magic;        /* GP2XSHM_MAGIC once the shim has initialised */
    uint32_t width;        /* logical screen width  (set by shim) */
    uint32_t height;       /* logical screen height (set by shim) */
    uint32_t frame_seq;    /* bumped by shim on every SDL_Flip */
    volatile uint32_t buttons;  /* GP2X button bitmap (set by viewer) */
    volatile uint32_t quit;     /* viewer requests shutdown */
    /* audio (set by shim on SDL_OpenAudio) */
    uint32_t audio_freq;
    uint32_t audio_format;     /* SDL 1.2 audio format word (e.g. 0x8010 = S16LSB) */
    uint32_t audio_channels;
    uint32_t audio_active;     /* 1 once audio is opened+unpaused */
    volatile uint32_t a_write; /* shim byte write cursor into aring */
    volatile uint32_t a_read;  /* viewer byte read cursor into aring */
    volatile uint32_t viewer_heartbeat; /* viewer bumps each loop; lets the producer
                                           tell "viewer attached" (it owns a_read) from
                                           "headless" (producer must drain a_read itself) */
    uint32_t reserved[3];
    uint8_t  pixels[GP2XSHM_FBBYTES]; /* RGB565, width*height valid */
    uint8_t  aring[GP2XSHM_ARING];    /* PCM ring buffer (shim->viewer) */
} gp2x_shm_t;

#endif /* GP2XSHM_H */
