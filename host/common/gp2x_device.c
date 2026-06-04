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
    double      aud_t0;
    int         aud_on;
    uint32_t    last_hb;          /* last seen viewer heartbeat */
    double      last_hb_t;        /* host time it last changed */
    int         debug;            /* ME_GP2X_DEBUG: log regions + present decisions */
    uint32_t    last_oadr;        /* last MLC scanout address (flip detection) */
    uint32_t    flips;            /* count of real game flips (== actual frame rate) */
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
    d->aud_freq = 44100; d->aud_ch = 2; d->aud_bits = 16;
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
/* The GP2X MMSP2 system timer (TCOUNT @ 0x0a00) runs at 7.3728 MHz, not 1 MHz.
   Advancing it at 1 MHz makes the game read time passing ~7.4x too slowly -> the
   simulation runs in slow motion and the in-game clock barely moves. Match the
   real frequency. ME_GP2X_TIMESCALE overrides the multiplier for experiments. */
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

/* sparse FNV hash of a 320x240 RGB565 surface (change detection). */
static uint32_t surf_hash(const void *host) {
    if (!host) return 0;
    const uint8_t *p = host; uint32_t h = 2166136261u;
    for (int y = 0; y < 240; y += 12)
        for (int x = 0; x < 320 * 2; x += 5)
            h = (h ^ p[(size_t)y * 640 + x]) * 16777619u;
    return h;
}
static int surf_nonblank(const void *host) {
    if (!host) return -1;
    const uint8_t *p = host; int nz = 0;
    for (int y = 8; y < 240; y += 24)
        for (int x = 0; x < 320 * 2; x++)
            if (p[(size_t)y * 640 + x]) { nz++; break; }
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
    uint32_t n0 = surf_hash(a), n1 = surf_hash(b);
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
    if (c1 && b)      gp2x_present_rgb565(d, b, 320, 240);
    else if (c0 && a) gp2x_present_rgb565(d, a, 320, 240);
    else gp2x_present_rgb565(d, surf_nonblank(b) > surf_nonblank(a) ? b : a, 320, 240);
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

/* ---- /dev/dsp (OSS) audio ring ---- */
static void aud_drain(gp2x_dev_t *d) {
    if (!d->shm) return;
    double now = host_now();
    /* If a viewer is attached it consumes the ring via real playback and owns
       a_read; advancing a_read here too would double-drain it (underruns/stutter).
       Detect the viewer by its heartbeat and yield a_read to it. */
    uint32_t hb = d->shm->viewer_heartbeat;
    if (hb != d->last_hb) { d->last_hb = hb; d->last_hb_t = now; }
    if (now - d->last_hb_t < 0.5) { d->aud_on = 0; return; }  /* viewer alive */
    /* headless: advance a_read as if played in real time so the ring drains and
       the game keeps producing at the right rate. */
    if (!d->aud_on) { d->aud_t0 = now; d->aud_on = 1; }
    uint32_t bps = d->aud_freq * d->aud_ch * (d->aud_bits / 8);
    if (!bps) return;
    uint64_t consumed = (uint64_t)((now - d->aud_t0) * bps);
    if (consumed > d->shm->a_write) consumed = d->shm->a_write;
    if (consumed > d->shm->a_read) d->shm->a_read = (uint32_t)consumed;
}
static uint32_t aud_free(gp2x_dev_t *d) {
    aud_drain(d);
    if (!d->shm) return 0;
    uint32_t used = d->shm->a_write - d->shm->a_read;
    return used < GP2XSHM_ARING ? GP2XSHM_ARING - used : 0;
}

uint32_t gp2x_dsp_write(gp2x_dev_t *d, const void *pcm, uint32_t n) {
    if (!d || !d->shm) return n;
    uint32_t fr = aud_free(d);
    if (n > fr) n = fr;
    if (n) {
        uint32_t w = d->shm->a_write % GP2XSHM_ARING, first = GP2XSHM_ARING - w;
        if (first > n) first = n;
        memcpy(d->shm->aring + w, pcm, first);
        if (n > first) memcpy(d->shm->aring, (const uint8_t *)pcm + first, n - first);
        d->shm->a_write += n;
    }
    return n;
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
    case GP2X_DSP_SETFMT:   d->aud_bits = (v == 8 /*AFMT_U8*/) ? 8 : 16; break;
    case GP2X_DSP_GETBLKSIZE: v = 4096; break;
    case GP2X_DSP_SETFRAGMENT: break;                 /* accept as-is */
    case GP2X_DSP_GETFMTS:  v = 0x18; break;           /* S16_LE | U8 */
    case GP2X_DSP_GETCAPS:  v = 0; break;
    case GP2X_DSP_GETODELAY:
        aud_drain(d);
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
    /* publish the negotiated format so the viewer opens the right audio device */
    if (d->shm) {
        d->shm->audio_freq = d->aud_freq;
        d->shm->audio_format = (d->aud_bits == 8) ? 0x0008u : 0x8010u; /* U8/S16LSB */
        d->shm->audio_channels = d->aud_ch;
        d->shm->audio_active = 1;
    }
    if (arg) memcpy(arg, &v, 4);
    return 0;
}
