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

#include "gp2xshm.h"

/* ------------------------------------------------------------------ state */
static gp2x_shm_t *g_shm = NULL;
static SDL_Surface *g_screen = NULL;
static char g_err[256];
static unsigned long g_start_ms = 0;
static unsigned int g_prev_buttons = 0;
static int g_inited = 0;

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
    int fd = shm_open(GP2XSHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { fprintf(stderr, "fakesdl: shm_open failed\n"); return; }
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
static Uint32 rgb_to_raw(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b) {
    if (f->palette) return 0; /* not used for our dst */
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

static void present(SDL_Surface *s) {
    if (!g_shm || !s) return;
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
    SDL_UpperBlit(src, NULL, d, NULL);
    if (src->flags & SDL_SRCCOLORKEY)
        SDL_SetColorKey(d, SDL_SRCCOLORKEY, src->format->colorkey);
    return d;
}
SDL_Surface *SDL_DisplayFormatAlpha(SDL_Surface *src) { return SDL_DisplayFormat(src); }
SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, SDL_PixelFormat *fmt, Uint32 flags) {
    (void)flags;
    SDL_Surface *d = alloc_surface(0, src->w, src->h, fmt->BitsPerPixel,
                                   fmt->Rmask, fmt->Gmask, fmt->Bmask, fmt->Amask);
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
            put_raw(dst, tx, ty, rgb_to_raw(dst->format, r, g, b));
        }
    }
    if (dstrect) { dstrect->w = sw; dstrect->h = sh; }
    return 0;
}
int SDL_LowerBlit(SDL_Surface *a, SDL_Rect *ar, SDL_Surface *b, SDL_Rect *br) {
    return SDL_UpperBlit(a, ar, b, br);
}

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
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode) { return mode; }

/* ---------------------------------------------------------------- events */
static void push_event(const SDL_Event *e) {
    int nt = (g_evq_tail + 1) % EVQ_SIZE;
    if (nt == g_evq_head) return; /* full */
    g_evq[g_evq_tail] = *e; g_evq_tail = nt;
}
static void pump(void) {
    if (!g_shm) return;
    if (g_shm->quit) { SDL_Event e; memset(&e, 0, sizeof(e)); e.type = SDL_QUIT; push_event(&e); }
    unsigned int b = g_shm->buttons, changed = b ^ g_prev_buttons, i;
    for (i = 0; i < GP2X_NBUTTONS; i++) {
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
Uint8 SDL_EventState(Uint8 type, int state) { (void)type; (void)state; return 1; }
Uint8 *SDL_GetKeyState(int *n) { static Uint8 ks[512]; if (n) *n = 512; return ks; }
SDLMod SDL_GetModState(void) { return KMOD_NONE; }

/* --------------------------------------------------------------- joystick */
static int g_dummy_joy;
int SDL_NumJoysticks(void) { return 1; }
const char *SDL_JoystickName(int i) { (void)i; return "gp2x-buttons"; }
SDL_Joystick *SDL_JoystickOpen(int i) { (void)i; return (SDL_Joystick *)&g_dummy_joy; }
void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }
int SDL_JoystickIndex(SDL_Joystick *j) { (void)j; return 0; }
int SDL_JoystickNumButtons(SDL_Joystick *j) { (void)j; return GP2X_NBUTTONS; }
int SDL_JoystickNumAxes(SDL_Joystick *j) { (void)j; return 2; }
int SDL_JoystickNumHats(SDL_Joystick *j) { (void)j; return 0; }
int SDL_JoystickNumBalls(SDL_Joystick *j) { (void)j; return 0; }
void SDL_JoystickUpdate(void) { pump(); }
int SDL_JoystickOpened(int i) { (void)i; return 1; }
Uint8 SDL_JoystickGetButton(SDL_Joystick *j, int btn) {
    (void)j;
    if (!g_shm || btn < 0 || btn >= GP2X_NBUTTONS) return 0;
    return (Uint8)((g_shm->buttons >> btn) & 1);
}
Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int axis) { (void)j; (void)axis; return 0; }
Uint8 SDL_JoystickGetHat(SDL_Joystick *j, int hat) { (void)j; (void)hat; return 0; }

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
Uint8 SDL_GetMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
Uint8 SDL_GetRelativeMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
void SDL_WarpMouse(Uint16 x, Uint16 y) { (void)x; (void)y; }

/* ------------------------------------------------------------------- time */
Uint32 SDL_GetTicks(void) { scanout_maybe(); return (Uint32)(now_ms() - g_start_ms); }
void SDL_Delay(Uint32 ms) {
    pump_audio();
    scanout_maybe();   /* present scanout-mode titles (no SDL_Flip) from their delay loop */
    struct timespec ts; ts.tv_sec = ms / 1000; ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

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
    g_audio_freq_v = sp.freq; g_audio_ch_v = sp.channels;
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
void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume) {
    if (volume <= 0) return;
    if (volume > SDL_MIX_MAXVOLUME) volume = SDL_MIX_MAXVOLUME;
    Uint32 i, n = len / 2;                       /* assume 16-bit signed */
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
    (void)df;
    /* SDL_ConvertAudio (below) ALWAYS emits S16 mono -- 2 bytes per output frame, 1 channel --
       regardless of the nominal dst format. So len_mult/len_ratio must size the buffer for that
       actual output (2 bytes/frame), NOT for (dbps*dc). The old (dbps*dc) basis under-allocated
       whenever the destination was 8-bit: e.g. an 8-bit WAV -> 8-bit device gave len_mult=1, but
       the S16 output is 2x the input -> a heap overflow in ConvertAudio (it surfaced as a glibc
       "malloc(): memory corruption" abort, e.g. Odonata, which opens the device as S8). */
    double out_bytes_per_in_frame = 2.0;   /* S16 mono out */
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
    double ratio = (double)p.dr / (double)p.sr;        /* resample -> dst rate */
    int out_frames = (int)(in_frames * ratio);
    short *out = (short *)cvt->buf;
    int o;
    for (o = 0; o < out_frames; o++) {
        double sp = o / ratio;
        int i0 = (int)sp, i1 = i0 + 1; if (i1 >= in_frames) i1 = in_frames - 1;
        double fr = sp - i0;
        out[o] = (short)(g_cvt_mono[i0] * (1.0 - fr) + g_cvt_mono[i1] * fr);
    }
    cvt->len_cvt = out_frames * 2;                     /* S16 mono */
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
void *SDL_LoadObject(const char *name) { return dlopen(name, RTLD_NOW | RTLD_GLOBAL); }
void *SDL_LoadFunction(void *handle, const char *name) { return dlsym(handle, name); }
void SDL_UnloadObject(void *handle) { if (handle) dlclose(handle); }

/* version */
const SDL_version *SDL_Linked_Version(void) {
    static SDL_version v = { 1, 2, 13 };
    return &v;
}
