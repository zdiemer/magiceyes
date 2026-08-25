/* fake-SDL 1.2 for GP2X/Wiz binaries under qemu-user.
 *
 * Implements the subset of the SDL 1.2 API that Deicide 3 (+ the real GP2X
 * libSDL_image / libSDL_mixer that layer on top of us) require, rendering the
 * screen surface into a /dev/shm framebuffer (RGB565) that a native x86 SDL2
 * viewer presents. Input is read back from the same shm segment. No GP2X
 * hardware (/dev/mem, MMSP2, /dev/fb0) is ever touched.
 *
 * Built with the GPH SDK toolchain (gcc-4.0.2-glibc-2.3.6) against the SDK's
 * own SDL headers so SDL_Surface/SDL_RWops/etc. are ABI-identical to the real
 * SDL_image / SDL_mixer this process also loads.
 *
 * Audio is currently stubbed (silent). Joystick = GP2X buttons via shm.
 */
#include "SDL_video.h"
#include "SDL_events.h"
#include "SDL_audio.h"
#include "SDL_joystick.h"
#include "SDL_timer.h"
#include "SDL_loadso.h"
#include "SDL_mouse.h"
#include "SDL_error.h"
#include "SDL_rwops.h"
#include "SDL_version.h"
#include "SDL_mutex.h"
#include "SDL_thread.h"
#include "SDL_cdrom.h"
#include "SDL_wiz_dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <pthread.h>
#include <errno.h>

#include "gp2xshm.h"
#include "glcmd.h"      /* ME_NR_REPORT: surface unsupported SDL features into the engine's run report */

/* Tell the engine's structured run report about a feature we can't honour, by writing a sentinel
   line to stderr that the engine ingests (see glcmd.h). A line, not a custom syscall, so it works
   under the OABI GPH-SDK toolchain (no `svc`). Gated on ME_DEBUG so a normal run stays quiet. */
static int sdl_rpt_on(void) { static int v = -1; if (v < 0) v = getenv("ME_DEBUG") ? 1 : 0; return v; }
static void sdl_report(long kind, long code, const char *name) {
    if (!sdl_rpt_on()) return;
    char b[128];
    int n = snprintf(b, sizeof b, "\x01MR %ld %ld %s\n", kind, code, name ? name : "");
    if (n > 0) { int w = write(2, b, (unsigned)n); (void)w; }
}

/* ------------------------------------------------------------------ state */
static gp2x_shm_t *g_shm = NULL;
static SDL_Surface *g_screen = NULL;
static char g_err[256];
static unsigned long g_start_ms = 0;
static unsigned int g_prev_buttons = 0;
static int g_inited = 0;

/* Virtual clock (FAKESDL_VTIME=fps): SDL_GetTicks advances by a fixed step per SDL_Flip (and by
   the requested ms per SDL_Delay) instead of by wall-clock, so the game's time-driven logic
   becomes a deterministic function of the frame count -- the basis for reproducible input
   record/replay regression tests (a recording's frame numbers map to the same game state on any
   host, fast or slow). 0 = off (real wall-clock; default; behaviour unchanged). Only correct for
   flip/Delay-driven titles; a pure SDL_GetTicks busy-spin would stall, so we nudge the clock
   forward after a long run of GetTicks calls with no flip (g_vtime_spin). */
static int g_vtime_fps = -1;          /* -1 = not yet read from env; 0 = off */
static unsigned long g_vclock_ms = 0; /* the virtual clock (ms) */
static int g_vtime_spin = 0;          /* GetTicks calls since the clock last advanced */
static int vtime_on(void) {
    if (g_vtime_fps < 0) { const char *e = getenv("FAKESDL_VTIME"); g_vtime_fps = e ? atoi(e) : 0;
                           if (g_vtime_fps < 0) g_vtime_fps = 0; }
    return g_vtime_fps > 0;
}
static void vtime_step_flip(void) { if (vtime_on()) { g_vclock_ms += 1000UL / (unsigned)g_vtime_fps;
                                                      g_vtime_spin = 0; } }

/* When a GLES title is ALSO loaded (Caanoo Propis/Rhythmos use SDL for input/audio/timing but
   EGL+GLES for rendering), the fakegles shim owns the shm framebuffer. Suppress every SDL present
   (SDL_Flip + the continuous-scanout fallback) so they can't overwrite the GL frame. Weak ref:
   resolves to fakegles's `magiceyes_gl_active` when that lib is loaded, else &sym == NULL and
   SDL-only titles are unaffected. */
extern int magiceyes_gl_active __attribute__((weak));
static int gl_owns_fb(void) { return (&magiceyes_gl_active != 0) && magiceyes_gl_active; }

#define EVQ_SIZE 256
static SDL_Event g_evq[EVQ_SIZE];
static int g_evq_head = 0, g_evq_tail = 0;

/* audio: the game registers a callback via SDL_OpenAudio; we "pull" it from the
   frame loop (SDL_Flip/SDL_Delay) since LinuxThreads clone() fails under qemu,
   and push the PCM into the shm ring for the x86 viewer to play. */
static void (*g_audio_cb)(void *userdata, Uint8 *stream, int len) = NULL;
static void *g_audio_ud = NULL;
static Uint8 *g_audio_buf = NULL;
static int g_audio_chunk = 0;            /* bytes per callback (obtained.size) */
static int g_audio_bps = 2;              /* bytes per sample */
static unsigned long g_audio_rate_bps = 0; /* bytes per second */
static int g_audio_opened = 0, g_audio_paused = 1, g_audio_lock = 0;
static unsigned long g_audio_last_ms = 0;
static int g_audio_freq_v = 22050, g_audio_ch_v = 2, g_audio_silence = 0;
static Uint16 g_audio_format = AUDIO_S16SYS;   /* obtained device format (for SDL_MixAudio) */
static long g_test_phase = 0;
static void pump_audio(void);

/* ------------------------------------------------------------------ util */
static unsigned long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)tv.tv_sec * 1000UL + tv.tv_usec / 1000UL;
}

static void shm_init(void) {
    if (g_shm) return;
    /* Open the shm object directly rather than via shm_open(): the device glibc's shm_open
       statfs-checks /dev/shm and returns an empty path on our fake /proc, so it fails (Caanoo
       EABI rootfs). The engine (DEV_SHMFB) and qemu both intercept open("/dev/shm/gp2x_fb"). */
    char shmpath[64]; snprintf(shmpath, sizeof shmpath, "/dev/shm%s", GP2XSHM_NAME);
    int fd = open(shmpath, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { fprintf(stderr, "fakesdl: shm open(%s) failed\n", shmpath); return; }
    if (ftruncate(fd, sizeof(gp2x_shm_t)) != 0) { /* may already be sized */ }
    void *p = mmap(NULL, sizeof(gp2x_shm_t), PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { fprintf(stderr, "fakesdl: mmap failed\n"); return; }
    g_shm = (gp2x_shm_t *)p;
    g_shm->buttons = 0;
    g_shm->quit = 0;
    g_shm->frame_seq = 0;
    g_shm->magic = GP2XSHM_MAGIC;
    fprintf(stderr, "fakesdl: shm ready (%lu bytes)\n",
            (unsigned long)sizeof(gp2x_shm_t));
}

/* ----------------------------------------------------------- pixel format */
static void calc_shift_loss(Uint32 mask, Uint8 *shift, Uint8 *loss) {
    Uint8 s = 0, bits = 0;
    if (mask == 0) { *shift = 0; *loss = 8; return; }
    while (!((mask >> s) & 1)) s++;
    { Uint32 m = mask >> s; while (m & 1) { bits++; m >>= 1; } }
    *shift = s;
    *loss = (Uint8)(8 - bits);
}

static SDL_PixelFormat *make_format(int depth, Uint32 R, Uint32 G, Uint32 B, Uint32 A) {
    SDL_PixelFormat *f = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    f->BitsPerPixel = (Uint8)depth;
    f->BytesPerPixel = (Uint8)((depth + 7) / 8);
    if (depth == 8) {
        f->palette = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
        f->palette->ncolors = 256;
        f->palette->colors = (SDL_Color *)calloc(256, sizeof(SDL_Color));
    }
    f->Rmask = R; f->Gmask = G; f->Bmask = B; f->Amask = A;
    calc_shift_loss(R, &f->Rshift, &f->Rloss);
    calc_shift_loss(G, &f->Gshift, &f->Gloss);
    calc_shift_loss(B, &f->Bshift, &f->Bloss);
    calc_shift_loss(A, &f->Ashift, &f->Aloss);
    f->alpha = 255;
    return f;
}

static SDL_Surface *alloc_surface(Uint32 flags, int w, int h, int depth,
                                  Uint32 R, Uint32 G, Uint32 B, Uint32 A) {
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    s->flags = flags & ~(Uint32)(SDL_HWSURFACE | SDL_DOUBLEBUF); /* we are sw */
    s->format = make_format(depth, R, G, B, A);
    s->w = w; s->h = h;
    s->pitch = (Uint16)(((w * s->format->BytesPerPixel) + 3) & ~3);
    s->pixels = calloc(1, (size_t)s->pitch * h);
    s->clip_rect.x = 0; s->clip_rect.y = 0; s->clip_rect.w = w; s->clip_rect.h = h;
    s->refcount = 1;
    return s;
}

/* read a raw pixel from a surface */
static Uint32 get_raw(SDL_Surface *s, int x, int y) {
    Uint8 *p = (Uint8 *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
    switch (s->format->BytesPerPixel) {
    case 1: return *p;
    case 2: return *(Uint16 *)p;
    case 3: return p[0] | (p[1] << 8) | (p[2] << 16);
    default: return *(Uint32 *)p;
    }
}
static void put_raw(SDL_Surface *s, int x, int y, Uint32 v) {
    Uint8 *p = (Uint8 *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
    switch (s->format->BytesPerPixel) {
    case 1: *p = (Uint8)v; break;
    case 2: *(Uint16 *)p = (Uint16)v; break;
    case 3: p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; break;
    default: *(Uint32 *)p = v; break;
    }
}
static void raw_to_rgb(const SDL_PixelFormat *f, Uint32 px, Uint8 *r, Uint8 *g, Uint8 *b) {
    if (f->palette) {
        SDL_Color c = f->palette->colors[px & 0xff];
        *r = c.r; *g = c.g; *b = c.b; return;
    }
    *r = (Uint8)(((px & f->Rmask) >> f->Rshift) << f->Rloss);
    *g = (Uint8)(((px & f->Gmask) >> f->Gshift) << f->Gloss);
    *b = (Uint8)(((px & f->Bmask) >> f->Bshift) << f->Bloss);
}
/* Nearest palette entry by squared RGB distance. A palette dst needs a real index from
   MapRGB and from format-converting blits; returning 0 here blanked every surface a game
   ConvertSurface'd to 8bpp (noiz2sa's whole sprite set -> solid white screen). */
static Uint32 pal_nearest(const SDL_Palette *p, Uint8 r, Uint8 g, Uint8 b) {
    int best = 0, i, n = p->ncolors > 256 ? 256 : p->ncolors;
    long bestd = 0x7fffffff;
    for (i = 0; i < n; i++) {
        int dr = p->colors[i].r - r, dg = p->colors[i].g - g, db = p->colors[i].b - b;
        long d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (d < bestd) { bestd = d; best = i; if (d == 0) break; }
    }
    return (Uint32)best;
}
static Uint32 rgb_to_raw(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b) {
    if (f->palette) return pal_nearest(f->palette, r, g, b);
    return ((Uint32)(r >> f->Rloss) << f->Rshift)
         | ((Uint32)(g >> f->Gloss) << f->Gshift)
         | ((Uint32)(b >> f->Bloss) << f->Bshift);
}

/* ----------------------------------------------------------------- init */
int SDL_Init(Uint32 flags) {
    (void)flags;
    if (!g_inited) { shm_init(); g_start_ms = now_ms(); g_inited = 1; }
    return 0;
}
int SDL_InitSubSystem(Uint32 flags) { return SDL_Init(flags); }
void SDL_QuitSubSystem(Uint32 flags) { (void)flags; }
Uint32 SDL_WasInit(Uint32 flags) { (void)flags; return g_inited ? flags : 0; }
void SDL_Quit(void) { if (g_shm) g_shm->quit = 1; }

char *SDL_GetError(void) { return g_err; }
void SDL_ClearError(void) { g_err[0] = 0; }
void SDL_SetError(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(g_err, sizeof(g_err), fmt, ap); va_end(ap);
}
void SDL_Error(SDL_errorcode code) { SDL_SetError("SDL error %d", (int)code); }

/* ---------------------------------------------------------------- video */
SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags) {
    if (!g_inited) SDL_Init(0);
    if (bpp <= 0) bpp = 16;
    if (w <= 0) w = 320; if (h <= 0) h = 240;
    if (w > GP2XSHM_MAXW) w = GP2XSHM_MAXW;
    if (h > GP2XSHM_MAXH) h = GP2XSHM_MAXH;
    if (g_screen) { /* leak previous on re-set; fine for our use */ }
    if (bpp == 16)
        g_screen = alloc_surface(flags, w, h, 16, 0xF800, 0x07E0, 0x001F, 0);
    else if (bpp == 8)
        g_screen = alloc_surface(flags, w, h, 8, 0, 0, 0, 0);
    else if (bpp == 24)
        g_screen = alloc_surface(flags, w, h, 24, 0xFF0000, 0x00FF00, 0x0000FF, 0);
    else
        g_screen = alloc_surface(flags, w, h, 32, 0xFF0000, 0x00FF00, 0x0000FF, 0);
    if (g_shm) { g_shm->width = w; g_shm->height = h; }
    fprintf(stderr, "fakesdl: SetVideoMode %dx%d %dbpp\n", w, h, bpp);
    return g_screen;
}
SDL_Surface *SDL_GetVideoSurface(void) { return g_screen; }

/* defined in the FB-overlay section; exported so the fakegles shim composites the same movie layer
   under GL-rendered titles (Rhythmos draws its gameplay via GLES, not SDL). */
void magiceyes_video_composite(uint16_t *dst, int w, int h);

static void present(SDL_Surface *s) {
    if (!g_shm || !s) return;
    if (gl_owns_fb()) return;          /* GLES title: the fakegles shim presents the GL frame */
    g_shm->backend = 1;                /* SDL 2D (viewer header) */
    int w = s->w, h = s->h, x, y;
    if (w > GP2XSHM_MAXW) w = GP2XSHM_MAXW;
    if (h > GP2XSHM_MAXH) h = GP2XSHM_MAXH;
    Uint16 *dst = (Uint16 *)g_shm->pixels;
    if (s->format->BytesPerPixel == 2 && s->format->Rmask == 0xF800) {
        for (y = 0; y < h; y++)
            memcpy(dst + y * GP2XSHM_MAXW,
                   (Uint8 *)s->pixels + y * s->pitch, (size_t)w * 2);
    } else {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                Uint8 r, g, b;
                raw_to_rgb(s->format, get_raw(s, x, y), &r, &g, &b);
                dst[y * GP2XSHM_MAXW + x] =
                    (Uint16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            }
        }
    }
    magiceyes_video_composite(dst, w, h);   /* show the YUV movie layer through colour-keyed UI pixels */
    g_shm->width = w; g_shm->height = h;
    g_shm->frame_seq++;
}
/* Continuous-scanout fallback. Real GP2X SDL gives the game a HARDWARE screen surface whose
   memory the MMSP2 scans out continuously, so some titles (RetroVirus's intro logos) draw to
   the screen and NEVER call SDL_Flip/SDL_UpdateRect -- on our shim they'd be invisible. We
   present the screen from the game's poll/delay loop, but ONLY when it hasn't flipped recently,
   so flip-driven titles (Wind & Water) are untouched (they refresh g_last_flip_ms every flip). */
static unsigned long g_last_flip_ms = 0, g_last_scan_ms = 0;
static void scanout_maybe(void) {
    if (!g_screen) return;
    if (gl_owns_fb()) return;          /* GLES title owns the framebuffer; don't scan out SDL's */
    unsigned long t = now_ms();
    if (t - g_last_flip_ms < 200) return;   /* the game is actively flipping: leave it alone */
    if (t - g_last_scan_ms < 16) return;    /* else emulate ~60Hz scanout */
    g_last_scan_ms = t;
    present(g_screen);
}
/* GP2X SDL_Flip blocks for vsync (~60Hz); ours is instant, so games run at
   thousands of fps and their per-frame timing (incl. music/sfx pacing) breaks.
   Emulate vsync by capping flip rate (FAKESDL_FPS, default 60). */
int SDL_Flip(SDL_Surface *s) {
    present(s ? s : g_screen);
    g_last_flip_ms = now_ms();   /* explicit flip: suppress the scanout fallback */
    vtime_step_flip();           /* advance the virtual clock one frame (FAKESDL_VTIME) */
    pump_audio();
    static unsigned long last = 0;
    static long frame_ms = -1;
    if (frame_ms < 0) {
        const char *e = getenv("FAKESDL_FPS");
        int fps = e ? atoi(e) : 60; if (fps <= 0) fps = 60;
        frame_ms = 1000 / fps;
    }
    if (last) {
        long el = (long)(now_ms() - last);
        while (el < frame_ms) {
            struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (frame_ms - el) * 1000000L;
            nanosleep(&ts, NULL);
            pump_audio();
            el = (long)(now_ms() - last);
        }
    }
    last = now_ms();
    return 0;
}
void SDL_UpdateRect(SDL_Surface *s, Sint32 x, Sint32 y, Uint32 w, Uint32 h) {
    (void)x;(void)y;(void)w;(void)h; present(s ? s : g_screen); g_last_flip_ms = now_ms();
}
void SDL_UpdateRects(SDL_Surface *s, int n, SDL_Rect *r){ (void)n;(void)r; present(s); g_last_flip_ms = now_ms();}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
                                  Uint32 R, Uint32 G, Uint32 B, Uint32 A) {
    return alloc_surface(flags, w, h, depth, R, G, B, A);
}
SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int w, int h, int depth,
                                      int pitch, Uint32 R, Uint32 G, Uint32 B, Uint32 A) {
    SDL_Surface *s = alloc_surface(0, w, h, depth, R, G, B, A);
    free(s->pixels);
    s->pixels = pixels;
    s->pitch = (Uint16)pitch;
    s->flags |= SDL_PREALLOC;
    return s;
}
void SDL_FreeSurface(SDL_Surface *s) {
    if (!s) return;
    /* Real SDL 1.2 silently refuses to free the video surface (SDL_VideoSurface check in
       SDL_video.c), and games rely on it: freeing the screen (shutdown paths, mode-change
       cleanup) is a no-op on hardware. Our shim freed it for real; the surface's memory got
       recycled and the game's NEXT FreeSurface/use hit freed chunks -- the "glibc detected:
       double free or corruption" cluster (Abbaye, patissier/rotate, supertux-caanoo,
       OpenBOR, pacmame, scummvm, Wiztern: watchpoints showed FreeSurface's refcount
       decrement going 0 -> -1 on already-freed memory). */
    if (s == g_screen) return;
    if (--s->refcount > 0) return;
    if (s->format) {
        if (s->format->palette) {
            free(s->format->palette->colors);
            free(s->format->palette);
        }
        free(s->format);
    }
    if (!(s->flags & SDL_PREALLOC)) free(s->pixels);
    free(s);
}
SDL_Surface *SDL_DisplayFormat(SDL_Surface *src) {
    { static int dl = -1, dn = 0; if (dl < 0) dl = getenv("FAKESDL_BLIT_LOG") ? 1 : 0;
      if (dl && dn++ < 40) {
        SDL_PixelFormat *f = src->format;
        if (f->palette)
          fprintf(stderr, "DISPFMT src=%dx%d@8bpp ncolors=%d pal[0..3]=(%d,%d,%d)(%d,%d,%d)(%d,%d,%d)(%d,%d,%d)\n",
                  src->w, src->h, f->palette->ncolors,
                  f->palette->colors[0].r,f->palette->colors[0].g,f->palette->colors[0].b,
                  f->palette->colors[1].r,f->palette->colors[1].g,f->palette->colors[1].b,
                  f->palette->colors[2].r,f->palette->colors[2].g,f->palette->colors[2].b,
                  f->palette->colors[3].r,f->palette->colors[3].g,f->palette->colors[3].b);
        else
          fprintf(stderr, "DISPFMT src=%dx%d@%dbpp (no palette) Rmask=%x\n",
                  src->w, src->h, f->BitsPerPixel, f->Rmask);
      } }
    SDL_PixelFormat *df = g_screen ? g_screen->format : NULL;
    int depth = df ? df->BitsPerPixel : 16;
    Uint32 R = df ? df->Rmask : 0xF800, G = df ? df->Gmask : 0x07E0,
           B = df ? df->Bmask : 0x001F;
    SDL_Surface *d = alloc_surface(0, src->w, src->h, depth, R, G, B, 0);
    if (df && df->palette && d->format->palette)     /* 8bpp screen: convert to ITS palette */
        memcpy(d->format->palette->colors, df->palette->colors,
               256 * sizeof(SDL_Color));
    SDL_UpperBlit(src, NULL, d, NULL);
    if (src->flags & SDL_SRCCOLORKEY) {   /* re-map the key into the dst format's raw units */
        Uint8 kr, kg, kb;
        raw_to_rgb(src->format, src->format->colorkey, &kr, &kg, &kb);
        SDL_SetColorKey(d, SDL_SRCCOLORKEY, rgb_to_raw(d->format, kr, kg, kb));
    }
    return d;
}
SDL_Surface *SDL_DisplayFormatAlpha(SDL_Surface *src) { return SDL_DisplayFormat(src); }
SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, SDL_PixelFormat *fmt, Uint32 flags) {
    (void)flags;
    SDL_Surface *d = alloc_surface(0, src->w, src->h, fmt->BitsPerPixel,
                                   fmt->Rmask, fmt->Gmask, fmt->Bmask, fmt->Amask);
    /* converting TO a palette format maps against the CALLER's palette, so it must be in
       place on the dst before the blit (noiz2sa converts its BMP sprites to its 8bpp
       greyscale-ramp video format) */
    if (fmt->palette && d->format->palette) {
        int n = fmt->palette->ncolors > 256 ? 256 : fmt->palette->ncolors;
        memcpy(d->format->palette->colors, fmt->palette->colors,
               (size_t)n * sizeof(SDL_Color));
    }
    SDL_UpperBlit(src, NULL, d, NULL);
    return d;
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b) {
    return rgb_to_raw(f, r, g, b);
}
Uint32 SDL_MapRGBA(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    (void)a; return rgb_to_raw(f, r, g, b);
}
void SDL_GetRGB(Uint32 px, SDL_PixelFormat *f, Uint8 *r, Uint8 *g, Uint8 *b) {
    raw_to_rgb(f, px, r, g, b);
}
void SDL_GetRGBA(Uint32 px, SDL_PixelFormat *f, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) {
    raw_to_rgb(f, px, r, g, b); *a = 255;
}

int SDL_SetColorKey(SDL_Surface *s, Uint32 flag, Uint32 key) {
    if (flag & SDL_SRCCOLORKEY) { s->flags |= SDL_SRCCOLORKEY; s->format->colorkey = key; }
    else s->flags &= ~(Uint32)SDL_SRCCOLORKEY;
    return 0;
}
int SDL_SetAlpha(SDL_Surface *s, Uint32 flag, Uint8 alpha) {
    if (flag & SDL_SRCALPHA) { s->flags |= SDL_SRCALPHA; s->format->alpha = alpha; }
    else s->flags &= ~(Uint32)SDL_SRCALPHA;
    return 0;
}
SDL_bool SDL_SetClipRect(SDL_Surface *s, const SDL_Rect *r) {
    if (!r) { s->clip_rect.x = 0; s->clip_rect.y = 0; s->clip_rect.w = s->w; s->clip_rect.h = s->h; }
    else s->clip_rect = *r;
    return SDL_TRUE;
}
void SDL_GetClipRect(SDL_Surface *s, SDL_Rect *r) { *r = s->clip_rect; }
int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }

int SDL_FillRect(SDL_Surface *s, SDL_Rect *rect, Uint32 color) {
    int x0 = 0, y0 = 0, x1 = s->w, y1 = s->h, x, y;
    if (rect) { x0 = rect->x; y0 = rect->y; x1 = x0 + rect->w; y1 = y0 + rect->h; }
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w; if (y1 > s->h) y1 = s->h;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            put_raw(s, x, y, color);
    return 0;
}

int SDL_UpperBlit(SDL_Surface *src, SDL_Rect *srcrect,
                  SDL_Surface *dst, SDL_Rect *dstrect) {
    int sx0 = 0, sy0 = 0, sw = src->w, sh = src->h, dx = 0, dy = 0, x, y;
    { static int blog = -1, bn = 0;
      if (blog < 0) blog = getenv("FAKESDL_BLIT_LOG") ? 1 : 0;
      if (blog && bn++ < 200)
        fprintf(stderr, "BLIT #%d src=%dx%d@%dbpp%s -> dst=%dx%d %s pos=(%d,%d) srcr=%s\n",
                bn, src->w, src->h, src->format->BitsPerPixel,
                (src->flags & SDL_SRCCOLORKEY) ? "CK" : "",
                dst->w, dst->h, (dst == g_screen) ? "SCREEN" : "offscr",
                dstrect ? dstrect->x : 0, dstrect ? dstrect->y : 0,
                srcrect ? "y" : "n"); }
    if (srcrect) { sx0 = srcrect->x; sy0 = srcrect->y; sw = srcrect->w; sh = srcrect->h; }
    if (dstrect) { dx = dstrect->x; dy = dstrect->y; }
    /* clip to dst clip_rect */
    int cx0 = dst->clip_rect.x, cy0 = dst->clip_rect.y;
    int cx1 = cx0 + dst->clip_rect.w, cy1 = cy0 + dst->clip_rect.h;
    int usecolorkey = (src->flags & SDL_SRCCOLORKEY) ? 1 : 0;
    { static int nock = -1; if (nock < 0) nock = getenv("FAKESDL_NO_COLORKEY") ? 1 : 0;
      if (nock) usecolorkey = 0; }
    { static int dumped = 0; const char *dp = getenv("FAKESDL_SRC_DUMP");
      if (dp && !dumped && dst != g_screen && src->w >= 256 && src->h >= 200 && !srcrect) {
        dumped = 1;
        FILE *fp = fopen(dp, "wb");
        if (fp) {
          int w = src->w, h = src->h, xx, yy; Uint8 r, g, b;
          fwrite(&w, 4, 1, fp); fwrite(&h, 4, 1, fp);
          for (yy = 0; yy < h; yy++) for (xx = 0; xx < w; xx++) {
            raw_to_rgb(src->format, get_raw(src, xx, yy), &r, &g, &b);
            fputc(r, fp); fputc(g, fp); fputc(b, fp);
          }
          fclose(fp);
          fprintf(stderr, "SRC_DUMP wrote %dx%d (CK=%d key=%u)\n",
                  w, h, usecolorkey, src->format->colorkey);
        }
      } }
    int usealpha = (src->flags & SDL_SRCALPHA) ? 1 : 0;
    Uint8 sa = src->format->alpha;
    /* Fast path: identical pixel format + no alpha blend -> copy raw pixels directly (no
       per-pixel RGB round-trip). This is the common case (sprites DisplayFormat'd to the 16-bit
       screen, blitted to it) and the dominant per-frame cost under ARM emulation; the slow
       general path below stays for format-converting / alpha-blended blits. */
    /* Two 8bpp surfaces with the same palette content blit as a raw index copy (colorkey is
       an index there). Without this, per-frame 8->8 blits (noiz2sa's layer composition) take
       the RGB round-trip below, which costs a nearest-palette search per pixel. */
    int pal_same = 0;
    if (src->format->palette && dst->format->palette &&
        src->format->BytesPerPixel == 1 && dst->format->BytesPerPixel == 1) {
        SDL_Palette *pa = src->format->palette, *pb = dst->format->palette;
        pal_same = (pa == pb) || (pa->ncolors == pb->ncolors &&
                   !memcmp(pa->colors, pb->colors, (size_t)pa->ncolors * sizeof(SDL_Color)));
    }
    if (!(usealpha && sa < 255) &&
        ((!src->format->palette && !dst->format->palette &&
          src->format->BytesPerPixel == dst->format->BytesPerPixel &&
          src->format->Rmask == dst->format->Rmask &&
          src->format->Gmask == dst->format->Gmask &&
          src->format->Bmask == dst->format->Bmask) || pal_same)) {
        int bpp = src->format->BytesPerPixel;
        Uint32 ck = src->format->colorkey;
        for (y = 0; y < sh; y++) {
            int ty = dy + y, syy = sy0 + y;
            if (ty < cy0 || ty >= cy1 || syy < 0 || syy >= src->h) continue;
            int xs = 0, xe = sw;                 /* clip x to dst clip rect + src bounds */
            if (xs < cx0 - dx) xs = cx0 - dx;
            if (xs < -sx0)     xs = -sx0;
            if (xe > cx1 - dx) xe = cx1 - dx;
            if (xe > src->w - sx0) xe = src->w - sx0;
            if (xe <= xs) continue;
            Uint8 *sp = (Uint8 *)src->pixels + (size_t)syy * src->pitch + (size_t)(sx0 + xs) * bpp;
            Uint8 *dp = (Uint8 *)dst->pixels + (size_t)ty  * dst->pitch + (size_t)(dx  + xs) * bpp;
            int n = xe - xs;
            if (!usecolorkey) {
                memcpy(dp, sp, (size_t)n * bpp);
            } else if (bpp == 2) {
                Uint16 k = (Uint16)ck, *s16 = (Uint16 *)sp, *d16 = (Uint16 *)dp;
                for (x = 0; x < n; x++) { Uint16 p = s16[x]; if (p != k) d16[x] = p; }
            } else if (bpp == 4) {
                Uint32 *s32 = (Uint32 *)sp, *d32 = (Uint32 *)dp;
                for (x = 0; x < n; x++) { Uint32 p = s32[x]; if (p != ck) d32[x] = p; }
            } else {
                for (x = 0; x < n; x++) { Uint32 p = get_raw(src, sx0 + xs + x, syy);
                    if (p != ck) put_raw(dst, dx + xs + x, ty, p); }
            }
        }
        if (dstrect) { dstrect->w = sw; dstrect->h = sh; }
        return 0;
    }
    /* tiny recent-colour memo: a palette dst pays a 256-entry nearest search per new colour,
       and sprite art repeats a handful of colours, so this makes converting blits cheap */
    Uint32 mkey[8], mval[8]; int mn = 0;
    for (y = 0; y < sh; y++) {
        int ty = dy + y, syy = sy0 + y;
        if (ty < cy0 || ty >= cy1 || syy < 0 || syy >= src->h) continue;
        for (x = 0; x < sw; x++) {
            int tx = dx + x, sxx = sx0 + x;
            if (tx < cx0 || tx >= cx1 || sxx < 0 || sxx >= src->w) continue;
            Uint32 rawp = get_raw(src, sxx, syy);
            if (usecolorkey && rawp == src->format->colorkey) continue;
            Uint8 r, g, b;
            raw_to_rgb(src->format, rawp, &r, &g, &b);
            if (usealpha && sa < 255) {
                Uint8 dr, dg, db;
                raw_to_rgb(dst->format, get_raw(dst, tx, ty), &dr, &dg, &db);
                r = (Uint8)((r * sa + dr * (255 - sa)) / 255);
                g = (Uint8)((g * sa + dg * (255 - sa)) / 255);
                b = (Uint8)((b * sa + db * (255 - sa)) / 255);
            }
            if (dst->format->palette) {
                Uint32 key = ((Uint32)r << 16) | ((Uint32)g << 8) | b, v;
                int mi, hit = -1;
                for (mi = 0; mi < mn; mi++) if (mkey[mi] == key) { hit = mi; break; }
                v = (hit >= 0) ? mval[hit] : pal_nearest(dst->format->palette, r, g, b);
                if (hit < 0) { int slot = (mn < 8) ? mn++ : (int)(key & 7);
                               mkey[slot] = key; mval[slot] = v; }
                put_raw(dst, tx, ty, v);
            } else {
                put_raw(dst, tx, ty, rgb_to_raw(dst->format, r, g, b));
            }
        }
    }
    if (dstrect) { dstrect->w = sw; dstrect->h = sh; }
    return 0;
}
int SDL_LowerBlit(SDL_Surface *a, SDL_Rect *ar, SDL_Surface *b, SDL_Rect *br) {
    return SDL_UpperBlit(a, ar, b, br);
}
/* Nearest-neighbour stretch between same-format surfaces (what SDL 1.2's software path does
   for the common case; games use it to scale a low-res canvas to 320x240). C89-style decls:
   this file also builds with the GPH SDK's gcc-4.0.2 for the OABI shim. */
int SDL_SoftStretch(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
    SDL_Rect sr, dr;
    int bpp, x, y;
    if (!src || !dst || !src->pixels || !dst->pixels) return -1;
    if (srcrect) sr = *srcrect;
    else { sr.x = 0; sr.y = 0; sr.w = (Uint16)src->w; sr.h = (Uint16)src->h; }
    if (dstrect) dr = *dstrect;
    else { dr.x = 0; dr.y = 0; dr.w = (Uint16)dst->w; dr.h = (Uint16)dst->h; }
    bpp = src->format->BytesPerPixel;
    if (bpp != dst->format->BytesPerPixel || !sr.w || !sr.h || !dr.w || !dr.h) return -1;
    for (y = 0; y < dr.h; y++) {
        int sy = sr.y + (int)((long)y * sr.h / dr.h);
        Uint8 *srow, *drow;
        if (sy < 0 || sy >= src->h || dr.y + y < 0 || dr.y + y >= dst->h) continue;
        srow = (Uint8 *)src->pixels + (long)sy * src->pitch;
        drow = (Uint8 *)dst->pixels + (long)(dr.y + y) * dst->pitch;
        for (x = 0; x < dr.w; x++) {
            int sx = sr.x + (int)((long)x * sr.w / dr.w);
            if (sx < 0 || sx >= src->w || dr.x + x < 0 || dr.x + x >= dst->w) continue;
            memcpy(drow + (long)(dr.x + x) * bpp, srow + (long)sx * bpp, bpp);
        }
    }
    return 0;
}
/* App is always active, visible, and focused (no window manager on a handheld). */
Uint8 SDL_GetAppState(void) { return 0x01 | 0x02 | 0x04; }  /* MOUSEFOCUS|INPUTFOCUS|ACTIVE */
int SDL_SetGamma(float r, float g, float b) { (void)r; (void)g; (void)b; return 0; }
int SDL_SetGammaRamp(const Uint16 *r, const Uint16 *g, const Uint16 *b) { (void)r; (void)g; (void)b; return 0; }
int SDL_GetGammaRamp(Uint16 *r, Uint16 *g, Uint16 *b) { (void)r; (void)g; (void)b; return 0; }

/* libgcc integer-division helpers: a few OABI-era binaries import these dynamically (the
   firmware libs exported their statically-linked copies). Weak, because static libgcc may be
   pulled into this link with its own strong copies (same semantics; either def serves).
   Plain C compiles to the __aeabi_* forms here, so these wrappers don't recurse. */
__attribute__((weak)) unsigned __udivsi3(unsigned n, unsigned d) { return d ? n / d : 0; }
__attribute__((weak)) unsigned __umodsi3(unsigned n, unsigned d) { return d ? n % d : 0; }
__attribute__((weak)) int      __divsi3(int n, int d)            { return d ? n / d : 0; }
__attribute__((weak)) int      __modsi3(int n, int d)            { return d ? n % d : 0; }

int SDL_SetColors(SDL_Surface *s, SDL_Color *colors, int first, int n) {
    int i;
    if (!s->format->palette) return 0;
    for (i = 0; i < n; i++) s->format->palette->colors[first + i] = colors[i];
    return 1;
}
int SDL_SetPalette(SDL_Surface *s, int flags, SDL_Color *colors, int first, int n) {
    (void)flags; return SDL_SetColors(s, colors, first, n);
}

char *SDL_VideoDriverName(char *buf, int n) { strncpy(buf, "gp2xshm", n); return buf; }
void SDL_WM_SetCaption(const char *t, const char *i) { (void)t; (void)i; }
void SDL_WM_SetIcon(SDL_Surface *s, Uint8 *m) { (void)s; (void)m; }
int SDL_ShowCursor(int toggle) { return toggle; }
/* No hardware cursor on the handhelds -- keep a current-cursor pointer so Create/Set/Get
   round-trip (games build cursors at init and crash on a NULL return; nothing is drawn). */
static SDL_Cursor *g_cursor;
SDL_Cursor *SDL_CreateCursor(Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y) {
    SDL_Cursor *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    int n = (w / 8) * h; if (n < 0) n = 0;
    c->area.w = (Uint16)w; c->area.h = (Uint16)h;
    c->hot_x = (Sint16)hot_x; c->hot_y = (Sint16)hot_y;
    c->data = malloc(n ? n : 1); c->mask = malloc(n ? n : 1);
    if (c->data && data) memcpy(c->data, data, n);
    if (c->mask && mask) memcpy(c->mask, mask, n);
    return c;
}
void SDL_FreeCursor(SDL_Cursor *cursor) {
    if (!cursor) return;
    if (g_cursor == cursor) g_cursor = NULL;
    free(cursor->data); free(cursor->mask); free(cursor);
}
void SDL_SetCursor(SDL_Cursor *cursor) { if (cursor) g_cursor = cursor; }
SDL_Cursor *SDL_GetCursor(void) { return g_cursor; }
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode) { return mode; }

/* ---------------------------------------------------------------- events */
/* SDL 1.2 event filter: called as each event is queued; returning 0 drops it. */
static SDL_EventFilter g_evfilter;
static void push_event(const SDL_Event *e) {
    if (g_evfilter && g_evfilter((SDL_Event *)e) == 0) return;
    int nt = (g_evq_tail + 1) % EVQ_SIZE;
    if (nt == g_evq_head) return; /* full */
    g_evq[g_evq_tail] = *e; g_evq_tail = nt;
}
void SDL_SetEventFilter(SDL_EventFilter filter) { g_evfilter = filter; }
SDL_EventFilter SDL_GetEventFilter(void) { return g_evfilter; }

/* ---- per-device joystick mapping --------------------------------------------
   GP2X + Wiz games see the GP2X 19-"button" layout (gp2xshm.h order, button index == shm bit;
   the d-pad is buttons 0..7). The default. CAANOO games are built against the Caanoo SDL, which
   reports the analog stick as AXES 0/1 and the face buttons in the Caanoo NATIVE order
   (A,X,B,Y,L,R,START,HOLD,I,II,TAT == joystick buttons 0..10) -- so feeding them the GP2X order
   makes the d-pad register as face buttons (the reported symptom). Select Caanoo with
   MAGICEYES_DEVICE=caanoo. TODO: separate GP2X/Wiz/Caanoo profiles + user-remappable bindings. */
static int joymap_caanoo(void) {
    static int v = -1;
    if (v < 0) { const char *d = getenv("MAGICEYES_DEVICE"); v = (d && !strcmp(d, "caanoo")) ? 1 : 0; }
    return v;
}
/* Caanoo SDL joystick button index -> our shm button bit (gp2xshm.h). */
static const int g_caanoo_btn[] = {
    GP2X_A, GP2X_X, GP2X_B, GP2X_Y, GP2X_L, GP2X_R,
    GP2X_START, GP2X_SELECT, GP2X_VOLUP, GP2X_VOLDOWN, GP2X_CLICK
};
#define CAANOO_NBTN ((int)(sizeof g_caanoo_btn / sizeof g_caanoo_btn[0]))
/* the analog stick, derived from the d-pad bits (axis 0 = X: left/right, axis 1 = Y: up/down) */
static Sint16 caanoo_axis(int axis) {
    if (!g_shm) return 0;
    unsigned b = g_shm->buttons;
    if (axis == 0) return (b & (1u << GP2X_RIGHT)) ? 32767 : (b & (1u << GP2X_LEFT)) ? -32768 : 0;
    if (axis == 1) return (b & (1u << GP2X_DOWN))  ? 32767 : (b & (1u << GP2X_UP))   ? -32768 : 0;
    return 0;
}

static void pump(void) {
    if (!g_shm) return;
    if (g_shm->quit) { SDL_Event e; memset(&e, 0, sizeof(e)); e.type = SDL_QUIT; push_event(&e); }
    /* touchscreen -> SDL mouse events (Caanoo games read the resistive panel as the SDL mouse).
       The viewer writes touch_x/y (guest pixels) + touch_down. */
    { static int px = -1, py = -1, pd = 0;
      int tx = g_shm->touch_x, ty = g_shm->touch_y, td = g_shm->touch_down ? 1 : 0;
      if (tx != px || ty != py) {
          SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_MOUSEMOTION;
          e.motion.x = (Uint16)tx; e.motion.y = (Uint16)ty;
          e.motion.xrel = (Sint16)(px < 0 ? 0 : tx - px); e.motion.yrel = (Sint16)(py < 0 ? 0 : ty - py);
          e.motion.state = (Uint8)(td ? SDL_BUTTON_LMASK : 0);
          push_event(&e); px = tx; py = ty;
      }
      if (td != pd) {
          SDL_Event e; memset(&e, 0, sizeof e);
          e.type = td ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
          e.button.button = SDL_BUTTON_LEFT; e.button.state = (Uint8)(td ? SDL_PRESSED : SDL_RELEASED);
          e.button.x = (Uint16)tx; e.button.y = (Uint16)ty;
          push_event(&e); pd = td;
      }
    }
    unsigned int b = g_shm->buttons, changed = b ^ g_prev_buttons, i;
    if (joymap_caanoo()) {
        /* analog-stick motion events from the d-pad bits */
        static Sint16 ax = 0, ay = 0;
        Sint16 nx = caanoo_axis(0), ny = caanoo_axis(1);
        if (nx != ax) { SDL_Event e; memset(&e,0,sizeof e); e.type=SDL_JOYAXISMOTION;
                        e.jaxis.which=0; e.jaxis.axis=0; e.jaxis.value=nx; push_event(&e); ax=nx; }
        if (ny != ay) { SDL_Event e; memset(&e,0,sizeof e); e.type=SDL_JOYAXISMOTION;
                        e.jaxis.which=0; e.jaxis.axis=1; e.jaxis.value=ny; push_event(&e); ay=ny; }
        /* face buttons in the Caanoo native order (button index i -> shm bit g_caanoo_btn[i]) */
        for (i = 0; i < (unsigned)CAANOO_NBTN; i++) {
            int bit = g_caanoo_btn[i];
            if (changed & (1u << bit)) {
                SDL_Event e; memset(&e, 0, sizeof(e));
                int down = (b >> bit) & 1;
                e.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
                e.jbutton.which = 0; e.jbutton.button = (Uint8)i;
                e.jbutton.state = (Uint8)(down ? SDL_PRESSED : SDL_RELEASED);
                push_event(&e);
            }
        }
        g_prev_buttons = b;
        return;
    }
    for (i = 0; i < GP2X_NBUTTONS; i++) {       /* default GP2X/Wiz: button index == shm bit */
        if (changed & (1u << i)) {
            SDL_Event e; memset(&e, 0, sizeof(e));
            int down = (b >> i) & 1;
            e.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
            e.jbutton.which = 0;
            e.jbutton.button = (Uint8)i;
            e.jbutton.state = (Uint8)(down ? SDL_PRESSED : SDL_RELEASED);
            push_event(&e);
        }
    }
    g_prev_buttons = b;
}
void SDL_PumpEvents(void) { pump(); scanout_maybe(); }
int SDL_PollEvent(SDL_Event *event) {
    pump(); scanout_maybe();
    if (g_evq_head == g_evq_tail) return 0;
    if (event) *event = g_evq[g_evq_head];
    g_evq_head = (g_evq_head + 1) % EVQ_SIZE;
    return 1;
}
int SDL_WaitEvent(SDL_Event *event) {
    for (;;) { if (SDL_PollEvent(event)) return 1; usleep(2000); }
}
int SDL_PushEvent(SDL_Event *event) { push_event(event); return 0; }
/* BennuGD's libsdlhandler drives the whole event loop through SDL_PeepEvents; without this
   export every mod_*.so dlopen fails ("undefined symbol: SDL_PeepEvents") and the game dies
   before main. Mask is the SDL_EVENTMASK(type) bitmap. */
int SDL_PeepEvents(SDL_Event *events, int numevents, SDL_eventaction action, Uint32 mask) {
    int n = 0, idx;
    if (action == SDL_ADDEVENT) {
        for (n = 0; n < numevents; n++) push_event(&events[n]);
        return numevents;
    }
    pump(); scanout_maybe();
    idx = g_evq_head;
    while (idx != g_evq_tail && n < numevents) {
        SDL_Event *e = &g_evq[idx];
        if (mask & SDL_EVENTMASK(e->type)) {
            if (events) events[n] = *e;
            n++;
            if (action == SDL_GETEVENT) {
                /* remove this slot: compact the tail end of the ring over it */
                int j = idx, k = (idx + 1) % EVQ_SIZE;
                while (k != g_evq_tail) { g_evq[j] = g_evq[k]; j = k; k = (k + 1) % EVQ_SIZE; }
                g_evq_tail = j;
                continue;   /* idx now holds the next (shifted) event */
            }
        }
        idx = (idx + 1) % EVQ_SIZE;
    }
    return n;
}
Uint8 SDL_EventState(Uint8 type, int state) { (void)type; (void)state; return 1; }
Uint8 *SDL_GetKeyState(int *n) { static Uint8 ks[512]; if (n) *n = 512; return ks; }
SDLMod SDL_GetModState(void) { return KMOD_NONE; }

/* --------------------------------------------------------------- joystick */
static int g_dummy_joy;
int SDL_NumJoysticks(void) { return 1; }
const char *SDL_JoystickName(int i) { (void)i; return "gp2x-buttons"; }
SDL_Joystick *SDL_JoystickOpen(int i) {
    (void)i;
    fprintf(stderr, "fakesdl: joystick opened, map=%s\n", joymap_caanoo() ? "caanoo" : "gp2x");
    return (SDL_Joystick *)&g_dummy_joy;
}
void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }
int SDL_JoystickIndex(SDL_Joystick *j) { (void)j; return 0; }
int SDL_JoystickNumButtons(SDL_Joystick *j) { (void)j; return joymap_caanoo() ? CAANOO_NBTN : GP2X_NBUTTONS; }
int SDL_JoystickNumAxes(SDL_Joystick *j) { (void)j; return 2; }
int SDL_JoystickNumHats(SDL_Joystick *j) { (void)j; return 0; }
int SDL_JoystickNumBalls(SDL_Joystick *j) { (void)j; return 0; }
void SDL_JoystickUpdate(void) { pump(); }
int SDL_JoystickOpened(int i) { (void)i; return 1; }
Uint8 SDL_JoystickGetButton(SDL_Joystick *j, int btn) {
    (void)j;
    { static int log = -1, seen[64] = {0};
      if (log < 0) log = getenv("FAKESDL_JOYLOG") ? 1 : 0;
      if (log && btn >= 0 && btn < 64 && !seen[btn]) { seen[btn] = 1;
        fprintf(stderr, "JOY GetButton(%d) polled\n", btn); } }
    if (!g_shm) return 0;
    if (joymap_caanoo()) {
        if (btn < 0 || btn >= CAANOO_NBTN) return 0;
        return (Uint8)((g_shm->buttons >> g_caanoo_btn[btn]) & 1);
    }
    if (btn < 0 || btn >= GP2X_NBUTTONS) return 0;
    return (Uint8)((g_shm->buttons >> btn) & 1);
}
Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int axis) {
    (void)j;
    { static int log = -1, seen[8] = {0};
      if (log < 0) log = getenv("FAKESDL_JOYLOG") ? 1 : 0;
      if (log && axis >= 0 && axis < 8 && !seen[axis]) { seen[axis] = 1;
        fprintf(stderr, "JOY GetAxis(%d) polled\n", axis); } }
    return joymap_caanoo() ? caanoo_axis(axis) : 0;
}
Uint8 SDL_JoystickGetHat(SDL_Joystick *j, int hat) { (void)j; (void)hat; return 0; }
int SDL_JoystickGetBall(SDL_Joystick *j, int ball, int *dx, int *dy) {
    (void)j; (void)ball;
    if (dx) *dx = 0;
    if (dy) *dy = 0;
    return 0;
}

/* SDL 1.2 CD-ROM API: BennuGD's mod_cd.so links the whole family (its dlopen fails on any
   missing symbol, killing the game before main). There is no drive: zero drives, opens fail. */
int SDL_CDNumDrives(void) { return 0; }
const char *SDL_CDName(int drive) { (void)drive; return NULL; }
SDL_CD *SDL_CDOpen(int drive) { (void)drive; return NULL; }
CDstatus SDL_CDStatus(SDL_CD *cdrom) { (void)cdrom; return CD_ERROR; }
int SDL_CDPlayTracks(SDL_CD *c, int st, int sf, int nt, int nf) {
    (void)c; (void)st; (void)sf; (void)nt; (void)nf; return -1;
}
int SDL_CDPause(SDL_CD *c) { (void)c; return -1; }
int SDL_CDResume(SDL_CD *c) { (void)c; return -1; }
int SDL_CDStop(SDL_CD *c) { (void)c; return -1; }
int SDL_CDEject(SDL_CD *c) { (void)c; return -1; }
void SDL_CDClose(SDL_CD *c) { (void)c; }

int SDL_WM_IconifyWindow(void) { return 0; }   /* mod_wm.so: nothing to iconify */

/* ----------------------------------------------- GP2X/Wiz SDL extensions
 * The GPH fork of SDL 1.2 adds device-specific entry points (LCD update mode,
 * TV-out, the handset's RTC/USB/battery info hung off the joystick). Commercial
 * Wiz titles link these directly (e.g. Her Knights calls SDL_SetLcdMode +
 * SDL_SYS_JoystickGp2xInfo), so the shim must export them or ld.so aborts with
 * "undefined symbol". On a PC there's no LCD controller / RTC / battery to talk
 * to, so these are benign success stubs that report a healthy, idle handset. */
void SDL_SetLcdMode(int mode) { (void)mode; }              /* fast/quality LCD refresh select */
int  SDL_GetLcdMode(void) { return 0; }
int  SDL_SetLcdChange(unsigned int subCmd, unsigned int value) { (void)subCmd; (void)value; return 0; }
int  SDL_TvConfig(FB_TVCONF *tv_cfg) { (void)tv_cfg; return 0; } /* no TV-out -> no-op */
/* Battery/charge info buffer. Real Wiz fills the first bytes with the gauge
 * reading; report "full, not charging" so titles don't pop a low-battery warning.
 * Keep the touched range tiny -- the buffer size isn't in the ABI. */
void SDL_SYS_JoystickGp2xInfo(SDL_Joystick *j, unsigned char *pInfo) {
    (void)j; if (pInfo) { pInfo[0] = 4; pInfo[1] = 0; pInfo[2] = 0; pInfo[3] = 0; }
}
int SDL_SYS_JoystickGetExtRtc(SDL_Joystick *j, void *dt) { (void)j; (void)dt; return 0; }
int SDL_SYS_JoystickSetExtRtc(SDL_Joystick *j, void *dt) { (void)j; (void)dt; return 0; }
int SDL_SYS_JoystickSetPowerOff(SDL_Joystick *j) { (void)j; return 0; }
int SDL_SYS_JoystickUsbConCheck(SDL_Joystick *j) { (void)j; return 0; }   /* USB not connected */
int SDL_SYS_JoystickUsbDisconnect(SDL_Joystick *j) { (void)j; return 0; }

/* ------------------------------------------------------------------ mouse */
Uint8 SDL_GetMouseState(int *x, int *y) {
    if (g_shm) { if (x) *x = g_shm->touch_x; if (y) *y = g_shm->touch_y;
                 return (Uint8)(g_shm->touch_down ? SDL_BUTTON_LMASK : 0); }
    if (x) *x = 0; if (y) *y = 0; return 0;
}
Uint8 SDL_GetRelativeMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
void SDL_WarpMouse(Uint16 x, Uint16 y) { (void)x; (void)y; }

/* ------------------------------------------------------------------- time */
Uint32 SDL_GetTicks(void) {
    scanout_maybe();
    if (vtime_on()) {
        /* anti-stall: a title that polls GetTicks WITHOUT flipping/delaying would freeze the
           virtual clock; after a long spin, nudge it so such loops still progress (rare). */
        if (++g_vtime_spin > 100000) { g_vclock_ms += 1; g_vtime_spin = 0; }
        return (Uint32)g_vclock_ms;
    }
    return (Uint32)(now_ms() - g_start_ms);
}
void SDL_Delay(Uint32 ms) {
    pump_audio();
    scanout_maybe();   /* present scanout-mode titles (no SDL_Flip) from their delay loop */
    if (vtime_on()) { g_vclock_ms += ms; g_vtime_spin = 0; }   /* delay advances the virtual clock */
    struct timespec ts; ts.tv_sec = ms / 1000; ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* --------------------------------------------------------- threads & timers
   The Wiz firmware gp2xmenu (unlike the standalone titles we run under qemu) drives its UI/audio
   from SDL threads + timer callbacks, so it imports SDL's threading API. Firmware boot runs only on
   the native engine, where guest LinuxThreads clone() works (the forked Unicorn relaxes do_fork --
   the same path Payback's native threads use), so back these with real pthreads. Standalone titles
   never import these symbols, so the qemu backend (where clone() fails -- see the audio note above)
   is unaffected; the new libpthread DT_NEEDED only matters once a thread is actually created. */
struct SDL_mutex { pthread_mutex_t m; };
SDL_mutex *SDL_CreateMutex(void) {
    SDL_mutex *x = malloc(sizeof *x);
    if (x) {
        /* RECURSIVE, matching real SDL 1.2 (src/thread/pthread/SDL_sysmutex.c). SDL mutexes are
           documented to be re-entrant by the same thread, and callers rely on it -- e.g. Rhythmos's
           embedded GPAC wraps its node-registry lock in SDL_mutexP via LockEnter, and Register()
           holds it while FindClass() re-locks it. A plain (NULL-attr / non-recursive) mutex here
           self-deadlocked that re-entry -> the song-launch "hang". */
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&x->m, &attr);
        pthread_mutexattr_destroy(&attr);
    }
    return x;
}
int  SDL_mutexP(SDL_mutex *x) { return x ? pthread_mutex_lock(&x->m)   : -1; }  /* SDL_LockMutex   */
int  SDL_mutexV(SDL_mutex *x) { return x ? pthread_mutex_unlock(&x->m) : -1; }  /* SDL_UnlockMutex */
void SDL_DestroyMutex(SDL_mutex *x) { if (x) { pthread_mutex_destroy(&x->m); free(x); } }

struct SDL_cond { pthread_cond_t c; };
SDL_cond *SDL_CreateCond(void) {
    SDL_cond *x = malloc(sizeof *x);
    if (x) pthread_cond_init(&x->c, NULL);
    return x;
}
void SDL_DestroyCond(SDL_cond *x) { if (x) { pthread_cond_destroy(&x->c); free(x); } }

/* Condition-variable ops. GPAC's MPEG-4 systems runtime (compiled into Rhythmos)
   uses these for its event/thread signalling; without them lazy symbol resolution
   aborts the game ("undefined symbol: SDL_CondBroadcast") the moment a song loads. */
int SDL_CondSignal(SDL_cond *x)    { return x ? pthread_cond_signal(&x->c)    : -1; }
int SDL_CondBroadcast(SDL_cond *x) { return x ? pthread_cond_broadcast(&x->c) : -1; }
int SDL_CondWait(SDL_cond *x, SDL_mutex *m) {
    return (x && m) ? pthread_cond_wait(&x->c, &m->m) : -1;
}
/* SDL 1.2: returns 0 if signalled, SDL_MUTEX_TIMEDOUT(1) on timeout. Default cond
   attr uses CLOCK_REALTIME, so build the deadline from that clock. */
int SDL_CondWaitTimeout(SDL_cond *x, SDL_mutex *m, Uint32 ms) {
    if (!x || !m) return -1;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return pthread_cond_timedwait(&x->c, &m->m, &ts) == ETIMEDOUT ? 1 : 0;
}

struct SDL_Thread { pthread_t t; int (*fn)(void *); void *data; };
static void *sdl_thread_trampoline(void *p) {
    struct SDL_Thread *th = p;
    th->fn(th->data);
    return NULL;
}
SDL_Thread *SDL_CreateThread(int (*fn)(void *), void *data) {
    struct SDL_Thread *th = malloc(sizeof *th);
    if (!th) return NULL;
    th->fn = fn; th->data = data;
    if (pthread_create(&th->t, NULL, sdl_thread_trampoline, th) != 0) { free(th); return NULL; }
    return th;
}
void SDL_WaitThread(SDL_Thread *th, int *status) {
    if (!th) return;
    pthread_join(th->t, NULL);
    if (status) *status = 0;
    free(th);
}
/* Counting semaphore on the mutex+cond pair above (glibc 2.3.6-era sem_t layouts differ
   between our build sysroots, so avoid <semaphore.h>). */
struct SDL_semaphore { pthread_mutex_t m; pthread_cond_t c; Uint32 v; };
SDL_sem *SDL_CreateSemaphore(Uint32 initial) {
    struct SDL_semaphore *s = malloc(sizeof *s);
    if (!s) return NULL;
    pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->c, NULL); s->v = initial;
    return (SDL_sem *)s;
}
void SDL_DestroySemaphore(SDL_sem *sem) {
    struct SDL_semaphore *s = (struct SDL_semaphore *)sem;
    if (!s) return;
    pthread_mutex_destroy(&s->m); pthread_cond_destroy(&s->c); free(s);
}
int SDL_SemWait(SDL_sem *sem) {
    struct SDL_semaphore *s = (struct SDL_semaphore *)sem;
    if (!s) return -1;
    pthread_mutex_lock(&s->m);
    while (s->v == 0) pthread_cond_wait(&s->c, &s->m);
    s->v--;
    pthread_mutex_unlock(&s->m);
    return 0;
}
int SDL_SemTryWait(SDL_sem *sem) {
    struct SDL_semaphore *s = (struct SDL_semaphore *)sem;
    if (!s) return -1;
    int r = SDL_MUTEX_TIMEDOUT;
    pthread_mutex_lock(&s->m);
    if (s->v > 0) { s->v--; r = 0; }
    pthread_mutex_unlock(&s->m);
    return r;
}
int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 ms) {
    struct SDL_semaphore *s = (struct SDL_semaphore *)sem;
    if (!s) return -1;
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    int r = 0;
    pthread_mutex_lock(&s->m);
    while (s->v == 0 && r != ETIMEDOUT) r = pthread_cond_timedwait(&s->c, &s->m, &ts);
    if (s->v > 0) { s->v--; r = 0; } else r = SDL_MUTEX_TIMEDOUT;
    pthread_mutex_unlock(&s->m);
    return r;
}
int SDL_SemPost(SDL_sem *sem) {
    struct SDL_semaphore *s = (struct SDL_semaphore *)sem;
    if (!s) return -1;
    pthread_mutex_lock(&s->m);
    s->v++;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
    return 0;
}
Uint32 SDL_SemValue(SDL_sem *sem) {
    struct SDL_semaphore *s = (struct SDL_semaphore *)sem;
    return s ? s->v : 0;
}
/* Calling thread's id. GPAC compares these to detect re-entrancy, so only
   per-thread consistency matters, not the exact value. */
Uint32 SDL_ThreadID(void) {
    Uint32 id = (Uint32)(uintptr_t)pthread_self();
    if (getenv("FAKESDL_TIDLOG")) { static int n = 0;
        if (n++ < 80) fprintf(stderr, "SDL_ThreadID -> %08x\n", id); }
    return id;
}

/* --- GPH Pollux hardware video-overlay extensions ----------------------------
   On a real Caanoo the MLC has a dedicated YUV video layer beneath the RGB
   framebuffer. Rhythmos decodes its MPEG-4 background (via the GPAC systems lib
   compiled into the .gpe) into planar Y/Cb/Cr in video memory, hands the layer
   the planes' physical addresses (FB_VMEMINFO), positions it (FB_VIDEO_CONF),
   and draws its UI on the RGB layer leaving the video region as the colour-key
   so the movie shows through. We have no hardware layer, so we capture the layer
   state here and composite it in present() (see g_fbvid_*). Layout mirrors
   the game passes real pointers. FB_VMEMINFO / FB_VIDEO_CONF / FB_RGBSET come
   from the SDK's pollux_fb_cfg.h (pulled in via SDL_video.h). */

/* Captured overlay state, consumed by present(). */
static int          g_fbvid_on = 0;
static FB_VMEMINFO  g_fbvid_mem;
static FB_VIDEO_CONF g_fbvid_conf;
static int          g_fbvid_have_conf = 0;

static int fblog(void) {
    static int v = -1;
    if (v < 0) v = getenv("FAKESDL_FBLOG") ? 1 : 0;
    return v;
}

/* --- software video-overlay compositor --------------------------------------
   The Pollux MLC scans a YUV plane out beneath the RGB framebuffer. We have no such
   layer, so we read the game's decoded planes out of video memory, convert to
   RGB565, and composite them in present() wherever the RGB (SDL) layer is the
   colour-key -- giving the movie-background-with-UI-on-top look in software. */
#define VID_W 320
#define VID_H 240
static uint16_t g_videobuf[VID_W * VID_H];
static volatile int g_video_have = 0;      /* a frame is ready in g_videobuf */
static uint16_t g_video_key = 0;           /* RGB565 colour-key (UI pixels == key show video) */

/* mmap window onto the Pollux video memory (physical). The engine aliases repeat /dev/mem maps of
   an already-mapped phys (mem.c dev_mmap), so this sees exactly the bytes the game decoded. */
static uint8_t *g_vmem = NULL;
static uint32_t g_vmem_base = 0, g_vmem_len = 0;
static uint8_t *vmem_ptr(uint32_t phys, uint32_t need) {
    if (g_vmem && phys >= g_vmem_base && phys + need <= g_vmem_base + g_vmem_len)
        return g_vmem + (phys - g_vmem_base);
    if (g_vmem) { munmap(g_vmem, g_vmem_len); g_vmem = NULL; }
    uint32_t base = phys & ~0xFFFFFu, len = 0x600000;   /* 1MB-aligned base, 6MB window */
    int fd = open("/dev/mem", O_RDWR);
    if (fd < 0) return NULL;
    void *m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
    close(fd);
    if (m == MAP_FAILED) return NULL;
    g_vmem = m; g_vmem_base = base; g_vmem_len = len;
    return (phys + need <= base + len) ? g_vmem + (phys - base) : NULL;
}

/* planar YUV 4:2:0 -> RGB565 (BT.601, fixed-point). */
static void yuv420_to_rgb565(const uint8_t *Y, int ys, const uint8_t *U, int us,
                             const uint8_t *V, int vs) {
    for (int y = 0; y < VID_H; y++) {
        const uint8_t *yr = Y + y * ys, *ur = U + (y >> 1) * us, *vr = V + (y >> 1) * vs;
        uint16_t *o = g_videobuf + y * VID_W;
        for (int x = 0; x < VID_W; x++) {
            int yy = yr[x], uu = ur[x >> 1] - 128, vv = vr[x >> 1] - 128;
            int r = yy + ((91881 * vv) >> 16);
            int g = yy - ((22554 * uu + 46802 * vv) >> 16);
            int b = yy + ((116130 * uu) >> 16);
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            o[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
}

/* Composite the current video frame UNDER the SDL/RGB layer: a destination pixel equal to the
   colour-key is "transparent" on the RGB layer, so the movie shows through there. Called from
   present() after the SDL surface has been copied into dst. */
void magiceyes_video_composite(uint16_t *dst, int w, int h) {
    if (!g_video_have) return;
    static int full = -1; if (full < 0) full = getenv("FAKESDL_VIDEO_FULL") ? 1 : 0;
    int vw = w < VID_W ? w : VID_W, vh = h < VID_H ? h : VID_H;
    long matched = 0;
    for (int y = 0; y < vh; y++) {
        Uint16 *o = dst + y * GP2XSHM_MAXW;
        const uint16_t *v = g_videobuf + y * VID_W;
        for (int x = 0; x < vw; x++)
            if (full || o[x] == g_video_key) { o[x] = v[x]; matched++; }
    }
    if (fblog()) { static int n = 0; if ((n++ % 120) == 0)
        fprintf(stderr, "fakesdl: composite matched=%ld/%d key=%04x dst[c]=%04x\n",
                matched, vw * vh, g_video_key, dst[(vh/2)*GP2XSHM_MAXW + vw/2]); }
}

int SDL_FBVideoStart(FB_VMEMINFO *m) {
    if (m) { g_fbvid_mem = *m; g_fbvid_on = 1; }
    if (fblog() && m)
        fprintf(stderr, "fakesdl: FBVideoStart fourcc=%08x %dx%d Lu=%08x/%u Cb=%08x/%u Cr=%08x/%u dev=%u\n",
                m->FourCC, m->Width, m->Height, m->LuAddr, m->LuStride,
                m->CbAddr, m->CbStride, m->CrAddr, m->CrStride, m->VideoDev);
    return 0;
}
int SDL_FBVideoMemoryUpdate(FB_VMEMINFO *m) {
    if (m) { g_fbvid_mem = *m; g_fbvid_on = 1; }
    if (fblog() && m)
        fprintf(stderr, "fakesdl: FBVideoMemUpdate Addr=%08x %dx%d Lu=%08x/%u Cb=%08x/%u Cr=%08x/%u "
                "LuOff=%08x CbOff=%08x CrOff=%08x\n",
                m->Address, m->Width, m->Height, m->LuAddr, m->LuStride, m->CbAddr, m->CbStride,
                m->CrAddr, m->CrStride, m->LuOffset, m->CbOffset, m->CrOffset);
    /* The plane PHYSICAL addresses live in Lu/Cb/Cr-Offset (the *Addr fields are 0). Pull the YV12
       planes out of video memory and convert to RGB565 for present() to composite. Strides come
       from the FBVideoStart vmem; default to a contiguous 320x240 4:2:0 layout if unset. */
    if (m && m->LuOffset) {
        uint32_t lus = g_fbvid_mem.LuStride ? g_fbvid_mem.LuStride : VID_W;
        uint32_t cbs = g_fbvid_mem.CbStride ? g_fbvid_mem.CbStride : VID_W / 2;
        uint32_t crs = g_fbvid_mem.CrStride ? g_fbvid_mem.CrStride : VID_W / 2;
        uint32_t lo = m->LuOffset < m->CbOffset ? m->LuOffset : m->CbOffset;
        if (m->CrOffset < lo) lo = m->CrOffset;
        uint8_t *Y = vmem_ptr(m->LuOffset, lus * VID_H);
        uint8_t *U = vmem_ptr(m->CbOffset, cbs * (VID_H / 2));
        uint8_t *V = vmem_ptr(m->CrOffset, crs * (VID_H / 2));
        (void)lo;
        if (Y && U && V) {
            yuv420_to_rgb565(Y, lus, U, cbs, V, crs);
            g_video_key = (uint16_t)(g_fbvid_have_conf ? (g_fbvid_conf.ColorKey & 0xFFFF) : 0);
            g_video_have = 1;
            if (fblog()) { static int n = 0; if ((n++ % 120) == 0)
                fprintf(stderr, "fakesdl: video frame Y[c]=%u Ymid=%u rgb[c]=%04x key=%04x vmem=%p\n",
                        Y[0], Y[lus * (VID_H/2) + VID_W/2], g_videobuf[VID_H/2*VID_W + VID_W/2],
                        g_video_key, (void*)g_vmem); }
        } else if (fblog()) { static int w = 0; if (w++ < 3)
            fprintf(stderr, "fakesdl: video planes NULL Y=%p U=%p V=%p (vmem map failed)\n",
                    (void*)Y, (void*)U, (void*)V); }
    }
    return 0;
}
int SDL_FBVideoUpdate(FB_VIDEO_CONF *c) {
    if (c) { g_fbvid_conf = *c; g_fbvid_have_conf = 1; }
    if (fblog() && c)
        fprintf(stderr, "fakesdl: FBVideoUpdate fourcc=%08x key=%08x src=%dx%d dst=%dx%d rect=(%d,%d,%d,%d)\n",
                c->FourCC, c->ColorKey, c->SrcWidth, c->SrcHeight, c->DstWidth, c->DstHeight,
                c->Left, c->Top, c->Right, c->Bottom);
    return 0;
}
int SDL_FBVideoStop(unsigned int *v) { (void)v; g_fbvid_on = 0; g_video_have = 0;
    if (fblog()) fprintf(stderr, "fakesdl: FBVideoStop\n"); return 0; }
int SDL_FBDeviceEnable(unsigned int *v) {
    if (fblog()) fprintf(stderr, "fakesdl: FBDeviceEnable=%u\n", v ? *v : 0); return 0; }
int SDL_FBRGBControl(FB_RGBSET *s)   { (void)s; return 0; }
int SDL_FBVideoPriority(unsigned int *v) { (void)v; return 0; }
int SDL_FBLayerTPColor(unsigned int *v)  { (void)v; return 0; }
int SDL_FBLayerAlphaBLD(unsigned int *v) { (void)v; return 0; }

/* SDL 1.2 timer: fire `cb` every `interval` ms; cb returns 0 to stop or the next interval. The menu
   uses only a couple, so one helper thread per timer is fine. */
struct sdl_timer { pthread_t t; volatile int run; Uint32 iv; SDL_NewTimerCallback cb; void *param; };
static void *sdl_timer_thread(void *p) {
    struct sdl_timer *tm = p;
    while (tm->run) {
        struct timespec ts; ts.tv_sec = tm->iv / 1000; ts.tv_nsec = (long)(tm->iv % 1000) * 1000000L;
        nanosleep(&ts, NULL);
        if (!tm->run) break;
        Uint32 nx = tm->cb(tm->iv, tm->param);
        if (nx == 0) break;
        tm->iv = nx;
    }
    return NULL;
}
SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_NewTimerCallback cb, void *param) {
    if (!cb || interval == 0) return NULL;
    struct sdl_timer *tm = malloc(sizeof *tm);
    if (!tm) return NULL;
    tm->run = 1; tm->iv = interval; tm->cb = cb; tm->param = param;
    if (pthread_create(&tm->t, NULL, sdl_timer_thread, tm) != 0) { free(tm); return NULL; }
    return (SDL_TimerID)tm;
}
SDL_bool SDL_RemoveTimer(SDL_TimerID id) {
    struct sdl_timer *tm = (struct sdl_timer *)id;
    if (!tm) return SDL_FALSE;
    tm->run = 0;
    pthread_join(tm->t, NULL);
    free(tm);
    return SDL_TRUE;
}
/* Legacy single-timer API (SDL_SetTimer(0,NULL) cancels). The old callback takes only the
   interval; adapt it onto the SDL_AddTimer machinery above. */
static SDL_TimerID g_legacy_timer;
static SDL_TimerCallback g_legacy_cb;
static Uint32 legacy_timer_tramp(Uint32 iv, void *param) { (void)param; return g_legacy_cb ? g_legacy_cb(iv) : 0; }
int SDL_SetTimer(Uint32 interval, SDL_TimerCallback callback) {
    if (g_legacy_timer) { SDL_RemoveTimer(g_legacy_timer); g_legacy_timer = NULL; }
    g_legacy_cb = callback;
    if (!interval || !callback) return 0;
    g_legacy_timer = SDL_AddTimer(interval, legacy_timer_tramp, NULL);
    return g_legacy_timer ? 0 : -1;
}

/* GPH Wiz device extensions the menu probes (battery / board id / device-iface lifecycle). No
   hardware here -> report a healthy battery + success so the menu neither warns nor auto-powers-off.
   (Battery scale is unknown; a high reading is safe against any low-battery threshold.) */
int SDL_WizDevIfInit(Uint32 init) { (void)init; return 0; }
int SDL_WizDevIfQuit(void) { return 0; }
int SDL_GetBattryCheck(Uint16 *battry_status) { if (battry_status) *battry_status = 0x0fff; return 0; }
int SDL_GetGphBoard(unsigned char *board_num) { if (board_num) *board_num = 0; return 0; }

/* ------------------------------------------------------------------ audio */
/* Closed-loop: produce only enough to keep ~CUSHION chunks ahead of what the
   viewer has consumed (a_read). This self-paces to the host audio clock and
   eliminates the under-run crackle that wall-clock pacing caused. */
#define AUDIO_CUSHION_CHUNKS 6
static void pump_audio(void) {
    if (!g_audio_opened || g_audio_paused || g_audio_lock || !g_audio_cb || !g_shm)
        return;
    if (g_audio_chunk <= 0) return;
    static FILE *adump = (FILE *)1;   /* sentinel = not yet checked */
    if (adump == (FILE *)1) {
        const char *p = getenv("FAKESDL_AUDIO_DUMP");
        adump = p ? fopen(p, "wb") : NULL;
    }
    static int testtone = -1;
    if (testtone < 0) testtone = getenv("FAKESDL_AUDIO_TEST") ? 1 : 0;
    unsigned int target = (unsigned int)g_audio_chunk * AUDIO_CUSHION_CHUNKS;
    int guard = 0, i;
    while ((g_shm->a_write - g_shm->a_read) < target && guard++ < 16) {
        if (testtone) {                       /* 440Hz sawtooth, our infra only */
            int ch = g_audio_ch_v ? g_audio_ch_v : 1;
            int frames = g_audio_chunk / (2 * ch);
            int period = g_audio_freq_v / 440; if (period < 2) period = 2;
            short *o = (short *)g_audio_buf;
            int f, c;
            for (f = 0; f < frames; f++) {
                int ph = (int)(g_test_phase % period);
                short v = (short)((ph * 16000 / period) - 8000);
                for (c = 0; c < ch; c++) o[f * ch + c] = v;
                g_test_phase++;
            }
        } else {
            /* SDL 1.2 pre-silences the callback buffer; games using SDL_MixAudio
               rely on mixing onto silence, not stale data. */
            memset(g_audio_buf, g_audio_silence, g_audio_chunk);
            g_audio_cb(g_audio_ud, g_audio_buf, g_audio_chunk);
        }
        if (adump) fwrite(g_audio_buf, 1, g_audio_chunk, adump);
        unsigned int wpos = g_shm->a_write;
        for (i = 0; i < g_audio_chunk; i++)
            g_shm->aring[(wpos + i) % GP2XSHM_ARING] = g_audio_buf[i];
        g_shm->a_write = wpos + (unsigned)g_audio_chunk;
    }
}

char *SDL_AudioDriverName(char *namebuf, int maxlen) {
    if (!namebuf || maxlen <= 0) return NULL;
    snprintf(namebuf, (size_t)maxlen, "dsp");   /* what the real GP2X/Wiz SDL reports */
    return namebuf;
}
int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    SDL_AudioSpec sp; memset(&sp, 0, sizeof(sp));
    if (desired)
        fprintf(stderr, "fakesdl: OpenAudio REQUESTED freq=%d fmt=%04x ch=%d samples=%d size=%u\n",
                desired->freq, desired->format, desired->channels, desired->samples,
                (unsigned)desired->size);
    if (desired) sp = *desired;
    if (sp.freq == 0) sp.freq = 22050;
    if (sp.format == 0) sp.format = AUDIO_S16SYS;
    if (sp.channels == 0) sp.channels = 2;
    if (sp.samples == 0) sp.samples = 1024;
    g_audio_bps = (sp.format & 0xFF) / 8; if (g_audio_bps < 1) g_audio_bps = 2;
    g_audio_chunk = sp.samples * sp.channels * g_audio_bps;
    sp.size = g_audio_chunk;
    g_audio_rate_bps = (unsigned long)sp.freq * sp.channels * g_audio_bps;
    g_audio_freq_v = sp.freq; g_audio_ch_v = sp.channels; g_audio_format = sp.format;
    g_audio_silence = (sp.format == AUDIO_U8) ? 0x80 : 0;  /* SDL silence byte */
    sp.silence = (Uint8)g_audio_silence;
    g_audio_cb = sp.callback; g_audio_ud = sp.userdata;
    free(g_audio_buf); g_audio_buf = (Uint8 *)malloc(g_audio_chunk);
    g_audio_opened = 1; g_audio_paused = 1; g_audio_last_ms = 0; g_audio_lock = 0;
    if (g_shm) {
        g_shm->audio_freq = sp.freq; g_shm->audio_format = sp.format;
        g_shm->audio_channels = sp.channels;
        g_shm->a_write = 0; g_shm->a_read = 0; g_shm->audio_active = 0;
    }
    if (obtained) *obtained = sp;
    fprintf(stderr, "fakesdl: OpenAudio %dHz fmt=%04x ch=%d samp=%d\n",
            sp.freq, sp.format, sp.channels, sp.samples);
    return 0;
}
void SDL_CloseAudio(void) { g_audio_opened = 0; g_audio_paused = 1; if (g_shm) g_shm->audio_active = 0; }
void SDL_PauseAudio(int p) {
    g_audio_paused = p ? 1 : 0;
    if (!p) { g_audio_last_ms = 0; if (g_shm) g_shm->audio_active = 1; }
}
void SDL_LockAudio(void) { g_audio_lock++; }
void SDL_UnlockAudio(void) { if (g_audio_lock > 0) g_audio_lock--; }
/* Mix src onto dst at the OPENED device format. The real SDL_MixAudio honours the device's
 * sample format; hardcoding 16-bit corrupts an 8-bit device (e.g. Her Knights opens AUDIO_S8),
 * which is exactly the "radio static" symptom -- it reinterprets pairs of S8 samples as one
 * S16, doubling pitch and injecting noise. Handle S8/U8/S16 per g_audio_format. */
void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume) {
    if (volume <= 0) return;
    if (volume > SDL_MIX_MAXVOLUME) volume = SDL_MIX_MAXVOLUME;
    Uint32 i;
    if ((g_audio_format & 0xFF) == 8) {
        if (g_audio_format == AUDIO_U8) {                 /* unsigned 8-bit, centred at 128 */
            for (i = 0; i < len; i++) {
                int v = (int)dst[i] - 128 + (((int)src[i] - 128) * volume) / SDL_MIX_MAXVOLUME;
                if (v > 127) v = 127; else if (v < -128) v = -128;
                dst[i] = (Uint8)(v + 128);
            }
        } else {                                          /* signed 8-bit, centred at 0 */
            Sint8 *d = (Sint8 *)dst; const Sint8 *s = (const Sint8 *)src;
            for (i = 0; i < len; i++) {
                int v = d[i] + (s[i] * volume) / SDL_MIX_MAXVOLUME;
                if (v > 127) v = 127; else if (v < -128) v = -128;
                d[i] = (Sint8)v;
            }
        }
        return;
    }
    Uint32 n = len / 2;                          /* 16-bit signed */
    Sint16 *d = (Sint16 *)dst; const Sint16 *s = (const Sint16 *)src;
    for (i = 0; i < n; i++) {
        int v = d[i] + (s[i] * volume) / SDL_MIX_MAXVOLUME;
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        d[i] = (Sint16)v;
    }
}
SDL_audiostatus SDL_GetAudioStatus(void) {
    return g_audio_paused ? SDL_AUDIO_PAUSED : SDL_AUDIO_PLAYING;
}
/* Audio conversion: SDL's SDL_AudioCVT exposes no channel fields, so we stash
   the full src/dst params in a small side table keyed by cvt->filter_index
   (we own the whole Build->Convert flow). Output is always decoded->mono->
   resampled->S16 (the only dst these titles request). */
struct cvtp { Uint16 sf, df; Uint8 sc, dc; int sr, dr; };
static struct cvtp g_cvtp[64];
static int g_cvtp_n = 0;
static short *g_cvt_mono = NULL;
static int g_cvt_monocap = 0;

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, Uint16 sf, Uint8 sc, int sr,
                      Uint16 df, Uint8 dc, int dr) {
    if (!cvt) return -1;
    /* The converter below assumes little-endian samples; a big-endian (0x1000) source/dst would
       be byte-swapped garbage. Surface it rather than silently mis-converting. */
    if ((sf & 0x1000u) || (df & 0x1000u)) sdl_report(ME_RPT_AUDIO, (sf & 0x1000u) ? sf : df, "audio_bigendian");
    if (sc == 0) sc = 1; if (dc == 0) dc = 1;
    if (sr == 0) sr = 22050; if (dr == 0) dr = 22050;
    memset(cvt, 0, sizeof(*cvt));
    int slot = (g_cvtp_n++) & 63;
    g_cvtp[slot].sf = sf; g_cvtp[slot].sc = sc; g_cvtp[slot].sr = sr;
    g_cvtp[slot].df = df; g_cvtp[slot].dc = dc; g_cvtp[slot].dr = dr;
    cvt->filter_index = slot;
    cvt->src_format = sf; cvt->dst_format = df;
    cvt->rate_incr = (double)dr / (double)sr;
    int sbps = (sf & 0xFF) / 8; if (sbps < 1) sbps = 1;
    int dbps = (df & 0xFF) / 8; if (dbps < 1) dbps = 1;
    /* SDL_ConvertAudio (below) emits the requested dst format/channels. Size the buffer for that
       output (dbps*dc bytes/frame, at the dst rate). Getting this wrong both ways has bitten us:
       the buffer must be big enough for an enlarging conversion (8-bit->16-bit, upsample) or
       ConvertAudio overruns it (a glibc "malloc(): memory corruption" abort, e.g. Odonata). */
    double out_bytes_per_in_frame = (double)dbps * dc;
    double ratio = (out_bytes_per_in_frame / ((double)sbps * sc)) * ((double)dr / sr);
    int mult = 1; while ((double)mult < ratio) mult++;
    cvt->len_mult = mult;
    cvt->len_ratio = ratio;
    cvt->needed = !(sf == df && sc == dc && sr == dr);
    return cvt->needed;
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt) {
    if (!cvt) return -1;
    if (!cvt->needed) { cvt->len_cvt = cvt->len; return 0; }
    struct cvtp p = g_cvtp[cvt->filter_index & 63];
    int sbps = (p.sf & 0xFF) / 8; if (sbps < 1) sbps = 1;
    int s_signed = (p.sf & 0x8000) ? 1 : 0;
    int sc = p.sc ? p.sc : 1;
    int in_frames = cvt->len / (sbps * sc);
    if (in_frames <= 0) { cvt->len_cvt = 0; return 0; }
    if (in_frames > g_cvt_monocap) {
        g_cvt_monocap = in_frames;
        g_cvt_mono = (short *)realloc(g_cvt_mono, (size_t)g_cvt_monocap * sizeof(short));
    }
    Uint8 *in = cvt->buf;
    int f, c;
    for (f = 0; f < in_frames; f++) {                 /* decode -> mono S16 */
        int acc = 0;
        for (c = 0; c < sc; c++) {
            int idx = f * sc + c, v;
            if (sbps == 1) v = s_signed ? (((signed char)in[idx]) << 8)
                                        : (((int)in[idx] - 128) << 8);
            else { short s = (short)(in[idx*2] | (in[idx*2+1] << 8)); v = s; }
            acc += v;
        }
        g_cvt_mono[f] = (short)(acc / sc);
    }
    /* Resample the mono S16 intermediate to the dst rate, then emit in the REQUESTED dst format
       and channel count. Emitting S16 unconditionally was the "radio static" bug: SDL_mixer asks
       for an S8 device (e.g. Her Knights: S16 11025 -> S8 22050), got S16 bytes, and played them
       as S8 -> noise. dc>1 duplicates the mono signal across channels. */
    int dbps = (p.df & 0xFF) / 8; if (dbps < 1) dbps = 1;
    int d_signed = (p.df & 0x8000) ? 1 : 0;
    int dc = p.dc ? p.dc : 1;
    /* Resample with 16.16 FIXED-POINT, never soft-float. The guest ARM has no FPU, so the old
       per-sample `double` math (o/ratio, the lerp) compiled to __aeabi_dmul/__adddf3/__fixdfsi
       calls -- ~150 guest instructions per output sample. A song's full background-music track is
       ~3.3M frames (U8 stereo, ~2.5 min); converting it that way takes minutes under the engine's
       interpreter and looked like a hang on song launch (Rhythmos). Integer math is far faster and
       ratio==1 (same rate, the common case here) degenerates to a straight copy. */
    long long out_ll = p.sr ? (long long)in_frames * (long long)p.dr / (long long)p.sr : in_frames;
    int out_frames = out_ll > 0 ? (int)out_ll : 0;
    unsigned step = p.dr ? (unsigned)(((unsigned long long)p.sr << 16) / (unsigned)p.dr) : (1u << 16);
    Uint8 *ob = cvt->buf;
    unsigned long long pos = 0;                         /* 16.16 sample position (64-bit: a long
                                                           track * 65536 overflows 32 bits) */
    int o;
    for (o = 0; o < out_frames; o++) {
        int i0 = (int)(pos >> 16), i1 = i0 + 1; if (i1 >= in_frames) i1 = in_frames - 1;
        int s16 = g_cvt_mono[i0];
        unsigned fr = (unsigned)pos & 0xffff;
        if (fr) s16 += (int)(((long long)(g_cvt_mono[i1] - g_cvt_mono[i0]) * (int)fr) >> 16);
        for (c = 0; c < dc; c++) {
            int idx = o * dc + c;
            if (dbps == 2) {                            /* S16 (assume signed little-endian) */
                ob[idx * 2] = (Uint8)(s16 & 0xff); ob[idx * 2 + 1] = (Uint8)((s16 >> 8) & 0xff);
            } else {                                    /* 8-bit: S16 -> high byte */
                int v8 = s16 >> 8;
                ob[idx] = d_signed ? (Uint8)(signed char)v8 : (Uint8)(v8 + 128);
            }
        }
        pos += step;
    }
    cvt->len_cvt = out_frames * dc * dbps;
    return 0;
}

/* ------------------------------------------------------------------ rwops */
static int mem_seek(SDL_RWops *c, int off, int whence) {
    Uint8 *np;
    if (whence == RW_SEEK_SET) np = c->hidden.mem.base + off;
    else if (whence == RW_SEEK_CUR) np = c->hidden.mem.here + off;
    else np = c->hidden.mem.stop + off;
    if (np < c->hidden.mem.base) np = c->hidden.mem.base;
    if (np > c->hidden.mem.stop) np = c->hidden.mem.stop;
    c->hidden.mem.here = np;
    return (int)(c->hidden.mem.here - c->hidden.mem.base);
}
static int mem_read(SDL_RWops *c, void *ptr, int size, int maxnum) {
    int avail = (int)(c->hidden.mem.stop - c->hidden.mem.here);
    int total = size * maxnum, n;
    if (total > avail) total = (avail / (size ? size : 1)) * size;
    memcpy(ptr, c->hidden.mem.here, total);
    c->hidden.mem.here += total;
    n = size ? total / size : 0;
    return n;
}
static int mem_write(SDL_RWops *c, const void *ptr, int size, int num) {
    int total = size * num;
    memcpy(c->hidden.mem.here, ptr, total);
    c->hidden.mem.here += total;
    return num;
}
static int mem_close(SDL_RWops *c) { free(c); return 0; }

SDL_RWops *SDL_AllocRW(void) { return (SDL_RWops *)calloc(1, sizeof(SDL_RWops)); }
void SDL_FreeRW(SDL_RWops *a) { free(a); }
SDL_RWops *SDL_RWFromMem(void *mem, int size) {
    SDL_RWops *r = SDL_AllocRW();
    r->hidden.mem.base = (Uint8 *)mem;
    r->hidden.mem.here = (Uint8 *)mem;
    r->hidden.mem.stop = (Uint8 *)mem + size;
    r->seek = mem_seek; r->read = mem_read; r->write = mem_write; r->close = mem_close;
    r->type = 2;
    return r;
}
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size) {
    return SDL_RWFromMem((void *)mem, size);
}
static int fp_seek(SDL_RWops *c, int off, int whence) {
    FILE *fp = (FILE *)c->hidden.stdio.fp;
    if (fseek(fp, off, whence) == 0) return (int)ftell(fp);
    return -1;
}
static int fp_read(SDL_RWops *c, void *ptr, int size, int maxnum) {
    return (int)fread(ptr, size, maxnum, (FILE *)c->hidden.stdio.fp);
}
static int fp_write(SDL_RWops *c, const void *ptr, int size, int num) {
    return (int)fwrite(ptr, size, num, (FILE *)c->hidden.stdio.fp);
}
static int fp_close(SDL_RWops *c) {
    if (c->hidden.stdio.autoclose) fclose((FILE *)c->hidden.stdio.fp);
    free(c); return 0;
}
SDL_RWops *SDL_RWFromFP(FILE *fp, int autoclose) {
    SDL_RWops *r = SDL_AllocRW();
    r->hidden.stdio.fp = fp; r->hidden.stdio.autoclose = autoclose;
    r->seek = fp_seek; r->read = fp_read; r->write = fp_write; r->close = fp_close;
    r->type = 1;
    return r;
}
SDL_RWops *SDL_RWFromFile(const char *file, const char *mode) {
    FILE *fp = fopen(file, mode);
    if (!fp) { SDL_SetError("Couldn't open %s", file); return NULL; }
    return SDL_RWFromFP(fp, 1);
}
Uint16 SDL_ReadLE16(SDL_RWops *s) { Uint8 b[2]; s->read(s, b, 1, 2); return (Uint16)(b[0] | (b[1] << 8)); }
Uint16 SDL_ReadBE16(SDL_RWops *s) { Uint8 b[2]; s->read(s, b, 1, 2); return (Uint16)((b[0] << 8) | b[1]); }
Uint32 SDL_ReadLE32(SDL_RWops *s) { Uint8 b[4]; s->read(s, b, 1, 4); return b[0]|(b[1]<<8)|(b[2]<<16)|((Uint32)b[3]<<24); }
Uint32 SDL_ReadBE32(SDL_RWops *s) { Uint8 b[4]; s->read(s, b, 1, 4); return ((Uint32)b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3]; }
int SDL_WriteLE16(SDL_RWops *s, Uint16 v){Uint8 b[2]={v&0xff,v>>8};return s->write(s,b,1,2);}
int SDL_WriteBE16(SDL_RWops *s, Uint16 v){Uint8 b[2]={v>>8,v&0xff};return s->write(s,b,1,2);}
int SDL_WriteLE32(SDL_RWops *s, Uint32 v){Uint8 b[4]={v&0xff,(v>>8)&0xff,(v>>16)&0xff,v>>24};return s->write(s,b,1,4);}
int SDL_WriteBE32(SDL_RWops *s, Uint32 v){Uint8 b[4]={v>>24,(v>>16)&0xff,(v>>8)&0xff,v&0xff};return s->write(s,b,1,4);}

/* WAV loader: parse RIFF/WAVE PCM (8/16-bit). */
SDL_AudioSpec *SDL_LoadWAV_RW(SDL_RWops *src, int freesrc, SDL_AudioSpec *spec,
                              Uint8 **audio_buf, Uint32 *audio_len) {
    if (audio_buf) *audio_buf = NULL;
    if (audio_len) *audio_len = 0;
    if (!src || !spec) { if (freesrc && src) src->close(src); return NULL; }
    char id[4];
    if (src->read(src, id, 1, 4) != 4 || memcmp(id, "RIFF", 4) != 0) {
        if (freesrc) src->close(src); return NULL;
    }
    SDL_ReadLE32(src);                              /* riff size */
    src->read(src, id, 1, 4);                       /* "WAVE" */
    if (memcmp(id, "WAVE", 4) != 0) { if (freesrc) src->close(src); return NULL; }
    Uint16 ch = 2, bits = 16; Uint32 rate = 22050;
    Uint8 *data = NULL; Uint32 datalen = 0;
    while (data == NULL) {
        if (src->read(src, id, 1, 4) != 4) break;
        Uint32 csz = SDL_ReadLE32(src);
        if (memcmp(id, "fmt ", 4) == 0) {
            SDL_ReadLE16(src);                      /* audio format */
            ch = SDL_ReadLE16(src);
            rate = SDL_ReadLE32(src);
            SDL_ReadLE32(src);                      /* byte rate */
            SDL_ReadLE16(src);                      /* block align */
            bits = SDL_ReadLE16(src);
            int rest = (int)csz - 16;
            if (rest > 0) src->seek(src, rest, RW_SEEK_CUR);
        } else if (memcmp(id, "data", 4) == 0) {
            datalen = csz;
            data = (Uint8 *)malloc(csz ? csz : 1);
            src->read(src, data, 1, csz);
        } else {
            src->seek(src, (int)(csz + (csz & 1)), RW_SEEK_CUR);
        }
    }
    if (!data) { if (freesrc) src->close(src); return NULL; }
    memset(spec, 0, sizeof(*spec));
    spec->freq = (int)rate;
    spec->channels = (Uint8)ch;
    spec->format = (bits == 8) ? AUDIO_U8 : AUDIO_S16LSB;
    spec->samples = 4096;
    spec->size = datalen;
    if (audio_buf) *audio_buf = data; else free(data);
    if (audio_len) *audio_len = datalen;
    if (freesrc) src->close(src);
    return spec;
}
void SDL_FreeWAV(Uint8 *buf) { free(buf); }

/* -------------------------------------------------------------------- BMP */
SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc) {
    if (!src) return NULL;
    int start = src->seek(src, 0, RW_SEEK_CUR);
    Uint8 magic[2];
    if (src->read(src, magic, 1, 2) != 2 || magic[0] != 'B' || magic[1] != 'M') {
        if (freesrc) src->close(src); return NULL;
    }
    SDL_ReadLE32(src);                 /* file size */
    SDL_ReadLE16(src); SDL_ReadLE16(src);
    Uint32 dataoff = SDL_ReadLE32(src);
    Uint32 hdrsize = SDL_ReadLE32(src);
    int w = (int)SDL_ReadLE32(src);
    int h = (int)SDL_ReadLE32(src);
    int topdown = 0; if (h < 0) { h = -h; topdown = 1; }
    SDL_ReadLE16(src);                  /* planes */
    int bpp = SDL_ReadLE16(src);
    Uint32 compression = SDL_ReadLE32(src);
    if (compression != 0) { if (freesrc) src->close(src); return NULL; }

    SDL_Color pal[256]; int ncols = 0, i, x, row;
    if (bpp <= 8) {
        ncols = (int)((dataoff - (14 + hdrsize)) / 4);
        if (ncols <= 0 || ncols > 256) ncols = 1 << bpp;
        src->seek(src, start + 14 + hdrsize, RW_SEEK_SET);
        for (i = 0; i < ncols; i++) {
            Uint8 c[4]; src->read(src, c, 1, 4);
            pal[i].b = c[0]; pal[i].g = c[1]; pal[i].r = c[2]; pal[i].unused = 0;
        }
    }
    SDL_Surface *s;
    if (bpp <= 8) { s = alloc_surface(0, w, h, 8, 0, 0, 0, 0); SDL_SetColors(s, pal, 0, ncols); }
    else          { s = alloc_surface(0, w, h, 32, 0xFF0000, 0x00FF00, 0x0000FF, 0); }
    int srcrow = ((w * bpp + 31) / 32) * 4;   /* rows padded to 4 bytes */
    Uint8 *rb = (Uint8 *)malloc(srcrow);
    src->seek(src, start + dataoff, RW_SEEK_SET);
    for (row = 0; row < h; row++) {
        int y = topdown ? row : (h - 1 - row);
        if (src->read(src, rb, 1, srcrow) != srcrow) break;
        for (x = 0; x < w; x++) {
            if (bpp == 1)       put_raw(s, x, y, (rb[x >> 3] >> (7 - (x & 7))) & 1);
            else if (bpp == 4)  put_raw(s, x, y, (x & 1) ? (rb[x >> 1] & 0x0F) : (rb[x >> 1] >> 4));
            else if (bpp == 8)  put_raw(s, x, y, rb[x]);
            else if (bpp == 24) put_raw(s, x, y, ((Uint32)rb[x*3+2]<<16)|((Uint32)rb[x*3+1]<<8)|rb[x*3]);
            else                put_raw(s, x, y, ((Uint32)rb[x*4+2]<<16)|((Uint32)rb[x*4+1]<<8)|rb[x*4]);
        }
    }
    free(rb);
    if (freesrc) src->close(src);
    return s;
}
int SDL_SaveBMP_RW(SDL_Surface *s, SDL_RWops *dst, int freedst) {
    (void)s; if (freedst && dst) dst->close(dst); return 0;
}

/* -------------------------------------------------------------- SDL_image (PNG)
 * Caanoo GLES titles (Propis/Rhythmos) load textures with IMG_Load. Pulling the real
 * SDL_image drags in a libjpeg/libtiff/libwebp chain; instead we decode PNG ourselves with
 * libpng12 -- which these titles already NEED (it's loaded in the process). We bind the libpng
 * entrypoints as WEAK externs: when libpng12 is present they resolve to it (no dlopen, so no
 * __dlopen ref -- the device glibc's libdl doesn't export that), and when it's absent (a
 * firmware-rootfs SDL title) they're NULL and IMG_Load returns NULL gracefully. IMG_Load
 * resolves from this shim; libSDL_image-1.2.so.0 stays a load-only stub. Output is a 32-bit
 * RGBA surface (byte order R,G,B,A) ready for glTexImage2D(GL_RGBA). */
typedef void (*png_rw_t)(void *, unsigned char *, unsigned long);
typedef void (*png_err_t)(void *, const char *);
#define WK __attribute__((weak))
extern void *png_create_read_struct(const char *, void *, png_err_t, png_err_t) WK;
extern void *png_create_info_struct(void *) WK;
extern void  png_destroy_read_struct(void **, void **, void **) WK;
extern void  png_set_read_fn(void *, void *, png_rw_t) WK;
extern void  png_read_info(void *, void *) WK;
extern void  png_read_image(void *, unsigned char **) WK;
extern void  png_read_update_info(void *, void *) WK;
extern void  png_set_strip_16(void *) WK;
extern void  png_set_expand(void *) WK;
extern void  png_set_gray_to_rgb(void *) WK;
extern void  png_set_tRNS_to_alpha(void *) WK;
extern void  png_set_filler(void *, unsigned int, int) WK;
extern unsigned int  png_get_image_width(void *, void *) WK;
extern unsigned int  png_get_image_height(void *, void *) WK;
extern unsigned char png_get_bit_depth(void *, void *) WK;
extern unsigned char png_get_color_type(void *, void *) WK;
extern const char *png_get_libpng_ver(void *) WK;
extern void *png_get_error_ptr(void *) WK;
extern void *png_get_io_ptr(void *) WK;
#undef WK
static void png_cb_read(void *png, unsigned char *data, unsigned long len) {
    SDL_RWops *rw = (SDL_RWops *)png_get_io_ptr(png);
    if (rw) rw->read(rw, data, 1, (int)len);
}
static void png_cb_err(void *png, const char *msg) {
    jmp_buf *jb = (jmp_buf *)png_get_error_ptr(png);
    fprintf(stderr, "fakesdl: IMG_Load: libpng error: %s\n", msg ? msg : "?");
    if (jb) longjmp(*jb, 1);
}
static void png_cb_warn(void *png, const char *msg) { (void)png; (void)msg; }

SDL_Surface *IMG_LoadPNG_RW(SDL_RWops *src);   /* forward: alias below IMG_Load_RW */
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc) {
    if (!src) return NULL;
    /* Dispatch by magic: BMP ("BM") -> our SDL_LoadBMP_RW; PNG -> libpng below. Caanoo titles
       vary: Propis ships PNG, Liar ships BMP (its liar.dat holds char/0.bmp ...). */
    int start = src->seek(src, 0, RW_SEEK_CUR);
    unsigned char magic[8] = {0};
    int got = src->read(src, magic, 1, 8);
    src->seek(src, start, RW_SEEK_SET);
    if (got >= 2 && magic[0] == 'B' && magic[1] == 'M')
        return SDL_LoadBMP_RW(src, freesrc);
    if (!(got >= 4 && magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G')) {
        fprintf(stderr, "fakesdl: IMG_Load: unsupported image (magic %02x %02x %02x %02x)\n",
                magic[0], magic[1], magic[2], magic[3]);
        sdl_report(ME_RPT_SDL, ((long)magic[0] << 8) | magic[1], "IMG_Load_unsupported");
        if (freesrc) src->close(src); return NULL;
    }
    if (!png_create_read_struct) {             /* libpng not in the process (weak -> NULL) */
        fprintf(stderr, "fakesdl: IMG_Load: libpng12 not available\n");
        if (freesrc) src->close(src); return NULL;
    }
    jmp_buf jb;
    void *png = png_create_read_struct(png_get_libpng_ver ? png_get_libpng_ver(NULL) : "1.2.49",
                                       &jb, png_cb_err, png_cb_warn);
    if (!png) { if (freesrc) src->close(src); return NULL; }
    void *info = png_create_info_struct(png);
    SDL_Surface * volatile surf = NULL;
    unsigned char ** volatile rows = NULL;
    if (setjmp(jb)) goto done;                 /* libpng error path */
    png_set_read_fn(png, src, png_cb_read);
    png_read_info(png, info);
    unsigned int w = png_get_image_width(png, info), h = png_get_image_height(png, info), y;
    int bd = png_get_bit_depth(png, info), ct = png_get_color_type(png, info);
    if (bd == 16) png_set_strip_16(png);
    png_set_expand(png);                       /* palette->RGB, sub-8-bit gray->8, tRNS->alpha */
    if (!(ct & 2)) png_set_gray_to_rgb(png);   /* PNG_COLOR_MASK_COLOR == 2 */
    png_set_tRNS_to_alpha(png);
    png_set_filler(png, 0xFF, 1);              /* PNG_FILLER_AFTER: force RGB -> RGBA */
    png_read_update_info(png, info);
    surf = alloc_surface(0, (int)w, (int)h, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    rows = (unsigned char **)malloc((size_t)h * sizeof(unsigned char *));
    for (y = 0; y < h; y++)
        rows[y] = (unsigned char *)surf->pixels + (size_t)y * surf->pitch;
    png_read_image(png, rows);
    { static int log = -1; if (log < 0) log = getenv("FAKESDL_BLIT_LOG") ? 1 : 0;
      if (log) fprintf(stderr, "IMG_Load %ux%u (bd=%d ct=%d) -> RGBA32\n", w, h, bd, ct); }
done:
    free((void *)rows);
    { void *pp = png, *ii = info; png_destroy_read_struct(&pp, &ii, NULL); }
    if (freesrc) src->close(src);
    return (SDL_Surface *)surf;
}
SDL_Surface *IMG_Load(const char *file) {
    SDL_RWops *rw = SDL_RWFromFile(file, "rb");
    if (!rw) { SDL_SetError("IMG_Load: can't open %s", file); return NULL; }
    return IMG_Load_RW(rw, 1);
}
int IMG_isPNG(SDL_RWops *src) { (void)src; return 1; }
/* sdl-instead probes the format before loading; answer from the real magic bytes (seek back so
   the subsequent IMG_Load_RW starts where it did). */
int IMG_isBMP(SDL_RWops *src) {
    if (!src) return 0;
    int pos = src->seek(src, 0, SEEK_CUR);
    char m[2] = {0, 0};
    int ok = src->read(src, m, 1, 2) == 2 && m[0] == 'B' && m[1] == 'M';
    src->seek(src, pos, SEEK_SET);
    return ok;
}
int IMG_isJPG(SDL_RWops *src) {
    if (!src) return 0;
    int pos = src->seek(src, 0, SEEK_CUR);
    unsigned char m[2] = {0, 0};
    int ok = src->read(src, m, 1, 2) == 2 && m[0] == 0xFF && m[1] == 0xD8;
    src->seek(src, pos, SEEK_SET);
    return ok;
}
int IMG_isGIF(SDL_RWops *src) {
    if (!src) return 0;
    int pos = src->seek(src, 0, SEEK_CUR);
    char m[3] = {0, 0, 0};
    int ok = src->read(src, m, 1, 3) == 3 && m[0] == 'G' && m[1] == 'I' && m[2] == 'F';
    src->seek(src, pos, SEEK_SET);
    return ok;
}
SDL_Surface *IMG_LoadPNG_RW(SDL_RWops *src) { return IMG_Load_RW(src, 0); }
const SDL_version *IMG_Linked_Version(void) {
    static SDL_version v = { 1, 2, 12 }; return &v;
}

/* ---------------------------------------------------- extra video helpers */
static SDL_PixelFormat *screen_fmt(void) {
    static SDL_PixelFormat *f = NULL;
    if (!f) f = make_format(16, 0xF800, 0x07E0, 0x001F, 0);
    return f;
}
const SDL_VideoInfo *SDL_GetVideoInfo(void) {
    static SDL_VideoInfo vi;
    vi.vfmt = g_screen ? g_screen->format : screen_fmt();
    vi.current_w = g_screen ? g_screen->w : 320;
    vi.current_h = g_screen ? g_screen->h : 240;
    vi.hw_available = 0; vi.wm_available = 1; vi.video_mem = 0x10000;
    return &vi;
}
int SDL_VideoModeOK(int w, int h, int bpp, Uint32 flags) { (void)w;(void)h;(void)flags; return bpp ? bpp : 16; }
SDL_Rect **SDL_ListModes(SDL_PixelFormat *fmt, Uint32 flags) { (void)fmt;(void)flags; return (SDL_Rect **)-1; }
int SDL_VideoInit(const char *drv, Uint32 flags) { (void)drv;(void)flags; return SDL_Init(0); }
void SDL_VideoQuit(void) {}
int SDL_EnableKeyRepeat(int delay, int interval) { (void)delay;(void)interval; return 0; }
int SDL_EnableUNICODE(int enable) { (void)enable; return enable; }
int SDL_JoystickEventState(int state) { return state; }
int SDL_WM_ToggleFullScreen(SDL_Surface *s) { (void)s; return 1; }
char *SDL_GetKeyName(SDLKey key) { (void)key; return (char *)""; }

/* ----------------------------------------------------------------- loadso */
/* Runtime plugin loading via libdl. No GP2X/Wiz title we target uses SDL_LoadObject,
 * and pulling in libdl forces a glibc-version-specific dlopen reference (e.g.
 * __dlopen / dlopen@GLIBC_2.34 with a modern EABI cross toolchain) that won't resolve
 * against an older device glibc. Stub them out so the shim depends on libc alone. */
void *SDL_LoadObject(const char *name) { sdl_report(ME_RPT_SDL, 0, "SDL_LoadObject");
    (void)name; SDL_SetError("SDL_LoadObject unsupported"); return 0; }
void *SDL_LoadFunction(void *handle, const char *name) { (void)handle; (void)name; return 0; }
void SDL_UnloadObject(void *handle) { (void)handle; }

/* version */
const SDL_version *SDL_Linked_Version(void) {
    static SDL_version v = { 1, 2, 13 };
    return &v;
}
