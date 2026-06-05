/* magiceyes — engine-agnostic GP2X (MMSP2) hardware model. See gp2x_device.h.
   Ported from the device logic proven in host/unicorn/me_unicorn.c, rewritten to
   operate on host pointers so the qemu-user backend can drive it with zero-copy
   g2h() mappings (and the Unicorn backend via its Layer-1 primitives). */
#include "gp2x_device.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <stdio.h>

struct memregion { uint32_t phys, len; void *host; };

struct gp2x_dev {
    gp2x_shm_t *shm;
    int         shm_fd;

    /* MMSP2 + framebuffers (host pointers into guest RAM) */
    void       *mmsp2;            /* 0xC0000000 register block */
    uint32_t    mmsp2_len;
    void       *fb[2];            /* /dev/fb0, /dev/fb1 surfaces */
    uint32_t    fb_len[2];
    struct memregion reg[64];     /* phys->host, for OADR scanout lookup */
    int         nreg;

    /* timing */
    double      t0;               /* monotonic epoch for the us counter */

    /* present cap + change detection */
    double      last_present;
    uint32_t    hash[2];

    /* audio (OSS /dev/dsp) */
    uint32_t    aud_freq, aud_ch, aud_bits;
    uint32_t    aud_fmt;          /* SDL audio format word for the viewer */
    double      prod_t0;          /* wall-clock epoch for producer real-time pacing */
    uint64_t    prod_bytes;       /* bytes the game has written since prod_t0 */
    int         prod_on;
    uint32_t    last_hb;          /* last seen viewer heartbeat */
    double      last_hb_t;        /* host time it last changed */
    int         debug;            /* ME_GP2X_DEBUG: log regions + present decisions */
    uint32_t    last_oadr;        /* last MLC scanout address (flip detection) */
    uint32_t    flips;            /* count of real game flips (== actual frame rate) */

    /* MLC 8-bit palette, reconstructed from trapped PALLT_A/PALLT_D writes
       (write-only hardware port). Each entry is RGB888; present converts to 565. */
    uint8_t     pal[256][3];      /* [i] = {R,G,B} */
    uint16_t    pal_idx;          /* current auto-incrementing entry index */
    uint8_t     pal_phase;        /* 0 = expect GB halfword, 1 = expect R halfword */
    uint16_t    pal_gb;           /* the GB halfword pending its R partner */
    int         pal_have;         /* a palette has been uploaded (else fall back to RGB332) */
};

/* ---- time ---- */
static double host_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- lifecycle ---- */
gp2x_dev_t *gp2x_open(void) {
    gp2x_dev_t *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->aud_freq = 44100; d->aud_ch = 2; d->aud_bits = 16; d->aud_fmt = 0x8010; /* S16LSB */
    d->t0 = host_now();
    d->debug = getenv("ME_GP2X_DEBUG") != NULL;

    int fd = shm_open(GP2XSHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { free(d); return NULL; }
    if (ftruncate(fd, sizeof(gp2x_shm_t)) != 0) { /* may pre-exist */ }
    void *p = mmap(NULL, sizeof(gp2x_shm_t), PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { close(fd); free(d); return NULL; }
    d->shm_fd = fd;
    d->shm = p;
    d->shm->buttons = 0; d->shm->quit = 0; d->shm->frame_seq = 0;
    d->shm->width = 320; d->shm->height = 240;
    d->shm->magic = GP2XSHM_MAGIC;
    return d;
}

void gp2x_close(gp2x_dev_t *d) {
    if (!d) return;
    if (d->shm && d->shm != MAP_FAILED) munmap(d->shm, sizeof(gp2x_shm_t));
    if (d->shm_fd > 0) close(d->shm_fd);
    free(d);
}

gp2x_shm_t *gp2x_shm(gp2x_dev_t *d) { return d ? d->shm : NULL; }
int gp2x_quit_requested(gp2x_dev_t *d) { return d && d->shm && d->shm->quit; }

/* ---- region registration ---- */
void gp2x_map_region(gp2x_dev_t *d, uint32_t phys, void *host, uint32_t len) {
    if (!d || !host) return;
    if (d->nreg < (int)(sizeof d->reg / sizeof d->reg[0]))
        d->reg[d->nreg++] = (struct memregion){ phys, len, host };
    if (phys == GP2X_MMSP2_PHYS) { d->mmsp2 = host; d->mmsp2_len = len; }
    if (d->debug)
        fprintf(stderr, "[gp2x] map_region phys=%08x host=%p len=%u%s\n",
                phys, host, len, phys == GP2X_MMSP2_PHYS ? "  <MMSP2>" : "");
}

void gp2x_set_fb(gp2x_dev_t *d, void *host, uint32_t len) {
    if (!d || !host) return;
    if (!d->fb[0])               { d->fb[0] = host; d->fb_len[0] = len; }
    else if (!d->fb[1] && host != d->fb[0]) { d->fb[1] = host; d->fb_len[1] = len; }
    if (d->debug)
        fprintf(stderr, "[gp2x] set_fb host=%p len=%u (fb0=%p fb1=%p)\n",
                host, len, d->fb[0], d->fb[1]);
}

/* phys -> host pointer (for MLC OADR scanout). */
static void *phys_to_host(gp2x_dev_t *d, uint32_t phys) {
    for (int i = 0; i < d->nreg; i++)
        if (phys >= d->reg[i].phys && phys < d->reg[i].phys + d->reg[i].len)
            return (uint8_t *)d->reg[i].host + (phys - d->reg[i].phys);
    return NULL;
}

/* ---- timer ---- */
/* The GP2X MMSP2 system timer (TCOUNT @ 0x0a00) reads at 7.3728 MHz (the
   reference crystal). Payback paces frames by busy-waiting on TCOUNT (245760
   ticks/frame) AND derives its simulation dt from the same counter, so the timer
   rate sets BOTH the frame rate and the game speed together: at 7.3728 MHz it
   runs at its intended speed and ~30fps. We cannot reach 60fps just by doubling
   the rate -- 14.7456 MHz gives 60fps but runs the whole game ~2x too fast
   (operator-confirmed in hands-on play). A genuine 60fps would need decoupling
   the game's dt from its frame pacing (a per-title patch), not a timer change.
   (1 MHz was the old slow-motion bug, ~7.4x too slow. An earlier world-scroll
   measurement wrongly suggested the speed was timer-independent -- it only
   sampled on-foot walking, which is velocity-clamped, so it missed the coupling.)
   ME_GP2X_TIMESCALE=N overrides to N *MHz* (NOT an Nx multiplier) -- so =1 is
   1 MHz, not "1x"; e.g. =9 would run a touch faster than the 7.3728 default. */
#define GP2X_TIMER_HZ 7372800.0
uint32_t gp2x_timer_us(gp2x_dev_t *d) {
    double hz = GP2X_TIMER_HZ;
    const char *s = getenv("ME_GP2X_TIMESCALE");
    if (s) hz = 1e6 * atof(s);
    return (uint32_t)((host_now() - d->t0) * hz);
}

/* ---- GPIO (active-low; matches gp2xshm.h button enum) ---- */
void gp2x_gpio_values(gp2x_dev_t *d, uint16_t *ga, uint16_t *gc, uint16_t *gvol) {
    uint32_t b = (d && d->shm) ? d->shm->buttons : 0;
    if (ga)   *ga   = 0xFF00 | (~b & 0x00FF);              /* stick bits 0..7 */
    if (gc)   *gc   = 0x00FF | ((~(b >> 8) & 0xFF) << 8);  /* buttons -> hi byte */
    if (gvol) *gvol = 0xFF00 | (~(b >> 16) & 0xFF);        /* VOL -> lo byte */
}

/* ---- framebuffer present ---- */
void gp2x_present_rgb565(gp2x_dev_t *d, const void *src, uint32_t w, uint32_t h) {
    if (!d || !d->shm || !src) return;
    if (w == 0 || w > GP2XSHM_MAXW) w = 320;
    if (h == 0 || h > GP2XSHM_MAXH) h = 240;
    const uint8_t *s = src;
    uint8_t *dst = d->shm->pixels;
    for (uint32_t y = 0; y < h; y++)
        memcpy(dst + (size_t)y * GP2XSHM_MAXW * 2, s + (size_t)y * w * 2, w * 2);
    d->shm->width = w; d->shm->height = h;
    d->shm->frame_seq++;
}

/* Reconstruct the MLC 8-bit palette from a trapped write to the write-only palette
   port. The hardware protocol (paeryn SDL / MMSP2 datasheet): write the start entry
   index to PALLT_A, then per entry two halfwords to PALLT_D — first (G<<8)|B, then R
   (low byte) — with the entry index auto-incrementing after the pair. Other offsets
   on the protected page (e.g. the OADR flip registers) are stored to RAM by the
   caller and need no capture here. */
void gp2x_mmsp2_write(gp2x_dev_t *d, uint32_t off, uint32_t val, int size) {
    (void)size;
    if (!d) return;
    if (off == GP2X_REG_PALLT_A) {
        d->pal_idx = (uint16_t)(val & 0xff);
        d->pal_phase = 0;
    } else if (off == GP2X_REG_PALLT_D) {
        if (d->pal_phase == 0) {
            d->pal_gb = (uint16_t)val;
            d->pal_phase = 1;
        } else {
            uint8_t i = (uint8_t)(d->pal_idx & 0xff);
            d->pal[i][0] = (uint8_t)(val & 0xff);          /* R */
            d->pal[i][1] = (uint8_t)((d->pal_gb >> 8) & 0xff); /* G */
            d->pal[i][2] = (uint8_t)(d->pal_gb & 0xff);    /* B */
            d->pal_idx = (uint16_t)((d->pal_idx + 1) & 0xff);
            d->pal_phase = 0;
            d->pal_have = 1;
        }
    }
}

/* 2D blitter register write (the 0xE0020000 window). Implemented in phase 2 —
   currently the registers are stored to RAM by the caller and the run-trigger is a
   no-op (so blitter-only games render nothing rather than crashing). */
void gp2x_blitter_write(gp2x_dev_t *d, uint32_t off, uint32_t val, int size) {
    (void)d; (void)off; (void)val; (void)size;
}

/* Present an 8-bit indexed framebuffer. If the game has uploaded a palette (captured
   via the trapped PALLT_D writes) use its true RGB; otherwise fall back to an RGB332
   approximation so the rendering is at least visible. */
static void gp2x_present_indexed8(gp2x_dev_t *d, const uint8_t *src, uint32_t w, uint32_t h) {
    if (!d || !d->shm || !src) return;
    if (w == 0 || w > GP2XSHM_MAXW) w = 320;
    if (h == 0 || h > GP2XSHM_MAXH) h = 240;
    uint16_t lut[256];
    if (d->pal_have) {
        for (int i = 0; i < 256; i++)
            lut[i] = (uint16_t)(((d->pal[i][0] >> 3) << 11) |
                                ((d->pal[i][1] >> 2) << 5)  |
                                 (d->pal[i][2] >> 3));
    } else {
        for (int i = 0; i < 256; i++) {        /* RGB332 fallback */
            uint16_t r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
            lut[i] = (uint16_t)((r << 13) | (g << 8) | (b << 3));
        }
    }
    uint16_t *dst = (uint16_t *)d->shm->pixels;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *sp = src + (size_t)y * w;
        uint16_t *dp = dst + (size_t)y * GP2XSHM_MAXW;
        for (uint32_t x = 0; x < w; x++)
            dp[x] = lut[sp[x]];
    }
    d->shm->width = w; d->shm->height = h;
    d->shm->frame_seq++;
}

/* sparse FNV hash of a surface (change detection). `len` bounds the reads: the
   game's fb may be smaller than 320x240x2 (e.g. 8-bit palettised = 76800 bytes),
   and reading past it segfaults our helper thread. */
static uint32_t surf_hash(const void *host, uint32_t len) {
    if (!host || !len) return 0;
    const uint8_t *p = host; uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i += 60)        /* sparse sample, in-bounds */
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static int surf_nonblank(const void *host, uint32_t len) {
    if (!host || !len) return -1;
    const uint8_t *p = host; int nz = 0;
    for (size_t i = 0; i < len; i += 997)
        if (p[i]) nz++;
    return nz;
}

/* Present whichever surface the game most recently rendered to. Candidates: the
   MLC OADR scanout target (if the game flips it) plus the fb0/fb1 surfaces (the
   double-buffered case, where OADR stays 0 — Payback). ~60fps capped. */
static void present_active(gp2x_dev_t *d) {
    double now = host_now();
    if (now - d->last_present < 0.016) return;
    d->last_present = now;

    void *a = d->fb[0], *b = d->fb[1];
    /* if the MLC scanout register points at a known region, prefer it */
    uint32_t oadr_phys = 0;
    void *oadr = NULL;
    if (d->mmsp2) {
        uint16_t lo, hi;
        memcpy(&lo, (uint8_t *)d->mmsp2 + GP2X_REG_OADRL, 2);
        memcpy(&hi, (uint8_t *)d->mmsp2 + GP2X_REG_OADRH, 2);
        oadr_phys = ((uint32_t)hi << 16) | lo;
        oadr = oadr_phys ? phys_to_host(d, oadr_phys) : NULL;
    }
    uint32_t n0 = surf_hash(a, d->fb_len[0]), n1 = surf_hash(b, d->fb_len[1]);
    int c0 = (n0 != d->hash[0]), c1 = (n1 != d->hash[1]);
    if (d->debug) {
        static int n = 0;
        if (n++ % 30 == 0)
            fprintf(stderr, "[gp2x] present: OADR=%08x->%p fb0%s fb1%s\n",
                    oadr_phys, oadr, c0 ? "*" : " ", c1 ? "*" : " ");
    }
    d->hash[0] = n0; d->hash[1] = n1;
    if (oadr) { gp2x_present_rgb565(d, oadr, 320, 240); return; }
    if (!a && !b) return;
    /* Present the surface the game most recently changed, choosing the depth from
       the mapped fb length: >=153600B is RGB565, 76800B is 8-bit indexed. */
    void *pick = NULL; uint32_t plen = 0;
    if (c1 && b)      { pick = b; plen = d->fb_len[1]; }
    else if (c0 && a) { pick = a; plen = d->fb_len[0]; }
    else {
        int s_a = surf_nonblank(a, d->fb_len[0]), s_b = surf_nonblank(b, d->fb_len[1]);
        if (s_b > s_a) { pick = b; plen = d->fb_len[1]; }
        else if (a)    { pick = a; plen = d->fb_len[0]; }
    }
    if (!pick) return;
    if (plen >= 320u * 240u * 2)      gp2x_present_rgb565(d, pick, 320, 240);
    else if (plen >= 320u * 240u)     gp2x_present_indexed8(d, pick, 320, 240);
}

/* Count real game frames by watching the MLC scanout address (OADR) flip. Called
   every helper tick (~1kHz) so it catches flips up to ~1000fps; published to shm
   so a monitor can read the ACTUAL frame rate (frame_seq only counts presents). */
static void count_flip(gp2x_dev_t *d) {
    if (!d->mmsp2) return;
    uint16_t lo, hi;
    memcpy(&lo, (uint8_t *)d->mmsp2 + GP2X_REG_OADRL, 2);
    memcpy(&hi, (uint8_t *)d->mmsp2 + GP2X_REG_OADRH, 2);
    uint32_t phys = ((uint32_t)hi << 16) | lo;
    if (phys && phys != d->last_oadr) {
        d->last_oadr = phys;
        d->flips++;
        if (d->shm) d->shm->reserved[0] = d->flips;   /* monitor: real fps source */
    }
}

/* ---- service tick (helper thread) ---- */
void gp2x_tick(gp2x_dev_t *d) {
    if (!d) return;
    count_flip(d);
    if (d->mmsp2) {
        uint32_t us = gp2x_timer_us(d);
        memcpy((uint8_t *)d->mmsp2 + GP2X_REG_TCOUNT, &us, 4);
        uint16_t ga, gc, gvol;
        gp2x_gpio_values(d, &ga, &gc, &gvol);
        memcpy((uint8_t *)d->mmsp2 + GP2X_REG_GPIO_A,   &ga,   2);
        memcpy((uint8_t *)d->mmsp2 + GP2X_REG_GPIO_C,   &gc,   2);
        memcpy((uint8_t *)d->mmsp2 + GP2X_REG_GPIO_VOL, &gvol, 2);
    }
    present_active(d);
}

/* ---- /dev/dsp (OSS) audio ring ----

   Pacing model: the game's audio thread writes PCM to /dev/dsp; on real OSS the
   write blocks until the DAC has played enough, which both paces the game to real
   time AND (critically) only ever stalls it ~one fragment (~90ms). We reproduce
   that with WALL-CLOCK pacing (gp2x_dsp_pace_us, slept by the caller) instead of
   blocking on the viewer's a_read: if we block on the viewer and the audio backend
   wedges for ~1s, the audio thread would hold its mixer mutex that long and freeze
   rendering. gp2x_dsp_write therefore NEVER blocks — if the ring is full (the
   viewer stalled) it drops the oldest audio to make room, keeping the game running
   and the viewer current once it recovers. */
static uint32_t aud_free(gp2x_dev_t *d) {
    if (!d->shm) return 0;
    uint32_t used = d->shm->a_write - d->shm->a_read;
    return used < GP2XSHM_ARING ? GP2XSHM_ARING - used : 0;
}

uint32_t gp2x_dsp_write(gp2x_dev_t *d, const void *pcm, uint32_t n) {
    if (!d || !d->shm) return n;
    if (getenv("ME_GP2X_AUDIO_FREERUN")) { d->shm->a_read = d->shm->a_write; }
    if (n > GP2XSHM_ARING) n = GP2XSHM_ARING;
    uint32_t used = d->shm->a_write - d->shm->a_read;
    uint32_t freeb = used < GP2XSHM_ARING ? GP2XSHM_ARING - used : 0;
    if (n > freeb) {                       /* consumer stalled: drop oldest, never block */
        uint32_t frame = d->aud_ch * (d->aud_bits / 8); if (frame < 1) frame = 1;
        uint32_t drop = ((n - freeb + frame - 1) / frame) * frame;
        if (drop > used) drop = used;
        d->shm->a_read += drop;
    }
    uint32_t w = d->shm->a_write % GP2XSHM_ARING, first = GP2XSHM_ARING - w;
    if (first > n) first = n;
    memcpy(d->shm->aring + w, pcm, first);
    if (n > first) memcpy(d->shm->aring, (const uint8_t *)pcm + first, n - first);
    d->shm->a_write += n;
    d->prod_bytes += n;
    return n;
}

/* Microseconds the caller should sleep so the game's audio output tracks real time
   (the OSS-blocking-write pacing, without ever blocking on the viewer). 0 = on time
   or behind; rebases on a >0.25s gap so banked idle time can't burst. */
uint32_t gp2x_dsp_pace_us(gp2x_dev_t *d) {
    uint32_t bps = d->aud_freq * d->aud_ch * (d->aud_bits / 8);
    if (!bps) return 0;
    double now = host_now();
    if (!d->prod_on) { d->prod_on = 1; d->prod_t0 = now; d->prod_bytes = 0; }
    double allowed = (now - d->prod_t0) * bps;
    if ((double)d->prod_bytes > allowed) {
        return (uint32_t)(((double)d->prod_bytes - allowed) / bps * 1e6);
    }
    if (allowed > (double)d->prod_bytes + bps * 0.25) {   /* long idle: restart clock */
        d->prod_t0 = now; d->prod_bytes = 0;
    }
    return 0;
}

/* ---- /dev/fb0,fb1 screeninfo (Linux fbdev ABI, 32-bit ARM target) ---- */
void gp2x_fill_fscreeninfo(void *buf, uint32_t smem_start) {
    uint8_t *b = buf;
    memset(b, 0, 80);
    memcpy(b + 0, "MagicEyes-MLC", 13);          /* id[16] */
    *(uint32_t *)(b + 16) = smem_start;           /* smem_start (phys base) */
    *(uint32_t *)(b + 20) = GP2X_FB_LEN;          /* smem_len */
    *(uint32_t *)(b + 24) = 0;                     /* type = FB_TYPE_PACKED_PIXELS */
    *(uint32_t *)(b + 32) = 2;                     /* visual = FB_VISUAL_TRUECOLOR */
    *(uint32_t *)(b + 44) = GP2X_FB_STRIDE;        /* line_length = 640 */
}
void gp2x_fill_vscreeninfo(void *buf) {
    uint8_t *b = buf;
    memset(b, 0, 160);
    *(uint32_t *)(b + 0)  = GP2X_FB_W;             /* xres */
    *(uint32_t *)(b + 4)  = GP2X_FB_H;             /* yres */
    *(uint32_t *)(b + 8)  = GP2X_FB_W;             /* xres_virtual */
    *(uint32_t *)(b + 12) = GP2X_FB_H;             /* yres_virtual */
    *(uint32_t *)(b + 24) = GP2X_FB_BPP;           /* bits_per_pixel */
    /* fb_bitfield red@32, green@44, blue@56, transp@68 : {offset,length,msb_right} */
    *(uint32_t *)(b + 32) = 11; *(uint32_t *)(b + 36) = 5;   /* red   */
    *(uint32_t *)(b + 44) = 5;  *(uint32_t *)(b + 48) = 6;   /* green */
    *(uint32_t *)(b + 56) = 0;  *(uint32_t *)(b + 60) = 5;   /* blue  */
}

int gp2x_dsp_ioctl(gp2x_dev_t *d, uint32_t cmd, void *arg, uint32_t *outlen) {
    uint32_t v = 0;
    if (arg) memcpy(&v, arg, 4);
    if (outlen) *outlen = 4;        /* default: write the int back */
    switch (cmd & 0xff) {
    case GP2X_DSP_SPEED:    if (v) d->aud_freq = v; break;
    case GP2X_DSP_STEREO:   d->aud_ch = v ? 2 : 1; break;
    case GP2X_DSP_CHANNELS: if (v) d->aud_ch = v; break;
    case GP2X_DSP_SETFMT:   /* OSS AFMT -> bits + SDL format word for the viewer */
        if (v == 8 /*AFMT_U8*/)        { d->aud_bits = 8;  d->aud_fmt = 0x0008; } /* U8 */
        else if (v == 32 /*S16_BE*/)   { d->aud_bits = 16; d->aud_fmt = 0x9010; } /* S16MSB */
        else /* 16 = AFMT_S16_LE */    { d->aud_bits = 16; d->aud_fmt = 0x8010; } /* S16LSB */
        break;
    case GP2X_DSP_GETBLKSIZE: v = 4096; break;
    case GP2X_DSP_SETFRAGMENT: break;                 /* accept as-is */
    case GP2X_DSP_GETFMTS:  v = 0x18; break;           /* S16_LE | U8 */
    case GP2X_DSP_GETCAPS:  v = 0; break;
    case GP2X_DSP_GETODELAY:
        v = d->shm ? (d->shm->a_write - d->shm->a_read) : 0;
        break;
    case GP2X_DSP_GETOSPACE: {  /* audio_buf_info{fragments,fragstotal,fragsize,bytes} */
        uint32_t freeb = aud_free(d), fsz = 4096;
        uint32_t info[4] = { freeb / fsz, GP2XSHM_ARING / fsz, fsz, freeb };
        if (arg) memcpy(arg, info, 16);
        if (outlen) *outlen = 16;
        return 0;
    }
    case GP2X_DSP_RESET: case GP2X_DSP_SYNC: case GP2X_DSP_POST:
        if (outlen) *outlen = 0;
        return 0;
    default:
        if (outlen) *outlen = 0;
        return 0;
    }
    /* publish the negotiated format so the viewer opens the right audio device.
       Honour the game's SNDCTL_DSP_SETFMT (recorded in aud_fmt) rather than
       guessing: Payback sets AFMT_S16_LE and writes little-endian S16, all in
       frame-aligned 16384-byte chunks. (An earlier "it's big-endian" read was a
       false positive from analysing the ring at a mid-frame a_read offset, before
       the drain was frame-aligned.) */
    if (d->shm) {
        d->shm->audio_freq = d->aud_freq;
        d->shm->audio_format = d->aud_fmt;
        d->shm->audio_channels = d->aud_ch;
        d->shm->audio_active = 1;
    }
    if (arg) memcpy(arg, &v, 4);
    return 0;
}
