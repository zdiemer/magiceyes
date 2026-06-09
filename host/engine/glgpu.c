/* Host-GPU OpenGL backend for the GL render-offload (glcmd.h). Replays each guest `gl_draw` on a
 * REAL host OpenGL (compatibility profile) context via SDL2, instead of the software rasterizer
 * (glraster.c). gl_draw is fixed-function GLES1.1 state, which maps ~1:1 onto desktop GL's
 * fixed-function pipeline (glLoadMatrixf / gl*Pointer / glEnable / glDrawArrays).
 *
 * Bundle-only (needs SDL2 + a GL context); the standalone/headless engine keeps software. This is
 * the DEFAULT backend for the bundled viewer (opt out with ME_GL_BACKEND=sw). The public glr_* below
 * dispatch to the GPU path when it inits OK, else to the software glsw_*. The GL context is created lazily on
 * the first call's thread and kept current there (GLES titles render from one thread). */
#include "engine.h"
#include "glcmd.h"
#include <string.h>
#include <stdlib.h>

/* software backend (glraster.c, renamed) */
void glsw_resize(int w, int h);
void glsw_clear(uint32_t packed);
void glsw_draw(uint32_t desc_ptr);
void glsw_present(void);

/* The host-GPU backend is the DEFAULT for GL-offload (GLES) titles (validated on Propis OABI+EABI
   and Rhythmos). It still falls back to software automatically when a GL context can't be created
   (see use_gpu()), so headless/no-GL hosts are unaffected. Opt out explicitly with ME_GL_BACKEND=sw
   (software|cpu|soft|0|off also accepted); ME_GL_BACKEND=gpu forces it on. */
static int gpu_wanted(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("ME_GL_BACKEND");
        v = (e && (!strcmp(e, "sw") || !strcmp(e, "software") || !strcmp(e, "cpu") ||
                   !strcmp(e, "soft") || !strcmp(e, "0") || !strcmp(e, "off"))) ? 0 : 1;
    }
    return v;
}

#ifdef ME_BUNDLED
#include <SDL2/SDL.h>

#ifdef _WIN32
#define GLAPI_ __stdcall
#else
#define GLAPI_
#endif
typedef unsigned GLenum; typedef int GLint; typedef int GLsizei; typedef unsigned GLuint;
typedef float GLfloat; typedef float GLclampf; typedef unsigned char GLboolean; typedef unsigned GLbitfield;

/* enums (subset; values are GL_* spec constants, shared with the guest fakegles / glraster.c) */
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_TEXTURE_2D 0x0DE1
#define GL_BLEND 0x0BE2
#define GL_ALPHA_TEST 0x0BC0
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_UNPACK_ALIGNMENT 0x0CF5

#define GLFNS \
  X(void,Viewport,(GLint,GLint,GLsizei,GLsizei)) \
  X(void,MatrixMode,(GLenum)) X(void,LoadMatrixf,(const GLfloat*)) X(void,LoadIdentity,(void)) \
  X(void,Enable,(GLenum)) X(void,Disable,(GLenum)) \
  X(void,BlendFunc,(GLenum,GLenum)) X(void,AlphaFunc,(GLenum,GLfloat)) \
  X(void,ClearColor,(GLclampf,GLclampf,GLclampf,GLclampf)) X(void,Clear,(GLbitfield)) \
  X(void,EnableClientState,(GLenum)) X(void,DisableClientState,(GLenum)) \
  X(void,VertexPointer,(GLint,GLenum,GLsizei,const void*)) \
  X(void,ColorPointer,(GLint,GLenum,GLsizei,const void*)) \
  X(void,TexCoordPointer,(GLint,GLenum,GLsizei,const void*)) \
  X(void,DrawArrays,(GLenum,GLint,GLsizei)) \
  X(void,GenTextures,(GLsizei,GLuint*)) X(void,BindTexture,(GLenum,GLuint)) \
  X(void,TexImage2D,(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*)) \
  X(void,TexParameteri,(GLenum,GLenum,GLint)) X(void,TexEnvi,(GLenum,GLenum,GLint)) \
  X(void,PixelStorei,(GLenum,GLint)) \
  X(void,ReadPixels,(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*))

#define X(ret,name,args) static ret (GLAPI_ *p_gl##name) args;
GLFNS
#undef X

static SDL_Window  *g_win = NULL;
static SDL_GLContext g_ctx = NULL;
static int g_ok = -1, g_w = 320, g_h = 240;
static GLuint g_tex = 0;
static uint8_t *g_rb = NULL;   /* readback scratch (g_w*g_h*4) */

static int gl_load(void) {
#define X(ret,name,args) p_gl##name = (ret(GLAPI_*)args)SDL_GL_GetProcAddress("gl" #name); if (!p_gl##name) return 0;
    GLFNS
#undef X
    return 1;
}
static int gpu_init(void) {
    if (g_ok >= 0) return g_ok;
    g_ok = 0;
    if (!SDL_WasInit(SDL_INIT_VIDEO)) return 0;          /* viewer hasn't SDL_Init'd yet */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);   /* keep dest alpha = GLES coverage, for compositing
                                                    the GL layer over the title's 2D SDL background */
    g_win = SDL_CreateWindow("magiceyes-gl", 0, 0, g_w, g_h, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (!g_win) { fprintf(DIAG, "glgpu: SDL_CreateWindow failed: %s\n", SDL_GetError()); return 0; }
    g_ctx = SDL_GL_CreateContext(g_win);
    if (!g_ctx) { fprintf(DIAG, "glgpu: GL context failed: %s\n", SDL_GetError());
                  SDL_DestroyWindow(g_win); g_win = NULL; return 0; }
    if (!gl_load()) { fprintf(DIAG, "glgpu: GL func load failed\n"); return 0; }
    p_glGenTextures(1, &g_tex);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g_rb = malloc((size_t)g_w * g_h * 4);
    fprintf(DIAG, "glgpu: host OpenGL backend ready (%dx%d)\n", g_w, g_h);
    g_ok = 1;
    return 1;
}

void glgpu_resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == g_w && h == g_h)) return;
    if (w > GP2XSHM_MAXW) w = GP2XSHM_MAXW; if (h > GP2XSHM_MAXH) h = GP2XSHM_MAXH;
    g_w = w; g_h = h;
    if (g_ok == 1) { SDL_SetWindowSize(g_win, g_w, g_h); free(g_rb); g_rb = malloc((size_t)g_w * g_h * 4); }
}
void glgpu_clear(uint32_t packed) {
    if (!gpu_init()) return;
    SDL_GL_MakeCurrent(g_win, g_ctx);
    float r = (packed & 0xff) / 255.f, g = ((packed >> 8) & 0xff) / 255.f,
          b = ((packed >> 16) & 0xff) / 255.f, a = ((packed >> 24) & 0xff) / 255.f;
    p_glViewport(0, 0, g_w, g_h);
    p_glClearColor(r, g, b, a);
    p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* ---- gl_draw -> host GL replay ---- */
static int comp_size(uint32_t t) { return (t == 0x1400 || t == 0x1401) ? 1 : (t == 0x1402 || t == 0x1403) ? 2 : 4; }
static float rd_comp(const uint8_t *b, uint32_t type, int c) {
    switch (type) {
    case 0x1406: return ((const float *)b)[c];               /* FLOAT */
    case 0x140C: return ((const int32_t *)b)[c] / 65536.0f;  /* FIXED */
    case 0x1400: return ((const int8_t *)b)[c];              /* BYTE */
    case 0x1401: return ((const uint8_t *)b)[c];             /* UBYTE */
    case 0x1402: return ((const int16_t *)b)[c];             /* SHORT */
    case 0x1403: return ((const uint16_t *)b)[c];            /* USHORT */
    default:     return 0;
    }
}
static int rd_elem(const struct gl_array *a, int idx, uint8_t *buf, int bufsz) {
    if (!a->en || !a->ptr || a->size <= 0) return 0;
    int esz = a->size * comp_size(a->type);
    if (esz <= 0 || esz > bufsz) return 0;
    int stride = a->stride ? a->stride : esz;
    return read_guest(buf, a->ptr + (uint32_t)idx * stride, (uint32_t)esz) == 0;
}

void glgpu_draw(uint32_t desc_ptr) {
    if (!gpu_init()) return;
    struct gl_draw d;
    if (read_guest(&d, desc_ptr, sizeof d) != 0) return;
    int n = (int)d.count;
    if (n <= 0 || !d.av.en) return;
    int vs = d.av.size >= 2 ? d.av.size : 2; if (vs > 4) vs = 4;
    if (getenv("ME_GL_DBG")) { static unsigned fs = 0; static int dc = 0;
        if (fs != g_shm->frame_seq) { if (dc) fprintf(DIAG, "glgpu FRAME draws=%d\n", dc); fs = g_shm->frame_seq; dc = 0; }
        if (++dc <= 8) {
            /* (1) geometry: transform each vertex mv*proj/w*viewport -> screen-space bbox */
            float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
            for (int i = 0; i < n && i < 256; i++) {
                uint8_t b[64]; float px = 0, py = 0, pz = 0, pw = 1;
                if (rd_elem(&d.av, (int)d.first + i, b, sizeof b))
                    for (int c = 0; c < vs; c++) { float v = rd_comp(b, d.av.type, c);
                        if (c==0) px=v; else if (c==1) py=v; else if (c==2) pz=v; else pw=v; }
                float e[4], cl[4];
                for (int r = 0; r < 4; r++) e[r]  = d.mv[r]*px + d.mv[4+r]*py + d.mv[8+r]*pz + d.mv[12+r]*pw;
                for (int r = 0; r < 4; r++) cl[r] = d.proj[r]*e[0] + d.proj[4+r]*e[1] + d.proj[8+r]*e[2] + d.proj[12+r]*e[3];
                float w = cl[3] ? cl[3] : 1e-6f;
                float sx = d.vp[0] + (cl[0]/w*0.5f+0.5f)*d.vp[2], sy = d.vp[1] + (cl[1]/w*0.5f+0.5f)*d.vp[3];
                if (sx<minx) minx=sx; if (sx>maxx) maxx=sx; if (sy<miny) miny=sy; if (sy>maxy) maxy=sy;
            }
            /* (2) texture: center sample of the decoded RGBA */
            uint32_t txc = 0;
            if (d.tex_rgba && d.tex_w > 0 && d.tex_h > 0)
                read_guest(&txc, d.tex_rgba + (uint32_t)((d.tex_h/2*d.tex_w + d.tex_w/2)*4), 4);
            fprintf(DIAG, "  draw[%d] tex=%dx%d screen=[%.0f,%.0f..%.0f,%.0f] texC=%08x atest=%x/%.2f blend=%d/%d entex=%d\n",
                dc, d.tex_w, d.tex_h, minx, miny, maxx, maxy, txc,
                d.atest_func, d.atest_ref, d.blend_s, d.blend_d, d.en_tex);
        } }

    static float *pos = NULL, *col = NULL, *tc = NULL; static int cap = 0;
    if (n > cap) { cap = n; pos = realloc(pos, (size_t)cap * 4 * sizeof(float));
                   col = realloc(col, (size_t)cap * 4 * sizeof(float));
                   tc  = realloc(tc,  (size_t)cap * 2 * sizeof(float)); }
    int have_col = d.ac.en, have_tc = d.at.en && d.en_tex && d.tex_rgba;
    uint8_t buf[64];
    for (int i = 0; i < n; i++) {
        int idx = (int)d.first + i;
        float *pp = pos + (size_t)i * 4; pp[0] = pp[1] = pp[2] = 0; pp[3] = 1;
        if (rd_elem(&d.av, idx, buf, sizeof buf))
            for (int c = 0; c < vs; c++) pp[c] = rd_comp(buf, d.av.type, c);
        float *cp = col + (size_t)i * 4;
        if (have_col && rd_elem(&d.ac, idx, buf, sizeof buf)) {
            float sc = (d.ac.type == 0x1401 || d.ac.type == 0x1400) ? 1 / 255.f : 1.f;
            cp[0] = rd_comp(buf, d.ac.type, 0) * sc; cp[1] = rd_comp(buf, d.ac.type, 1) * sc;
            cp[2] = rd_comp(buf, d.ac.type, 2) * sc; cp[3] = d.ac.size >= 4 ? rd_comp(buf, d.ac.type, 3) * sc : 1.f;
        } else { cp[0] = d.cur_color[0]; cp[1] = d.cur_color[1]; cp[2] = d.cur_color[2]; cp[3] = d.cur_color[3]; }
        float *tp = tc + (size_t)i * 2; tp[0] = tp[1] = 0;
        if (have_tc && rd_elem(&d.at, idx, buf, sizeof buf)) {
            tp[0] = rd_comp(buf, d.at.type, 0); if (d.at.size >= 2) tp[1] = rd_comp(buf, d.at.type, 1); }
    }

    SDL_GL_MakeCurrent(g_win, g_ctx);
    p_glMatrixMode(GL_PROJECTION); p_glLoadMatrixf(d.proj);
    p_glMatrixMode(GL_MODELVIEW);  p_glLoadMatrixf(d.mv);
    p_glViewport(d.vp[0], d.vp[1], d.vp[2], d.vp[3]);
    p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_CULL_FACE);
    if (d.en_blend) { p_glEnable(GL_BLEND); p_glBlendFunc(d.blend_s, d.blend_d); } else p_glDisable(GL_BLEND);
    if (d.en_atest) { p_glEnable(GL_ALPHA_TEST); p_glAlphaFunc(d.atest_func, d.atest_ref); } else p_glDisable(GL_ALPHA_TEST);

    if (have_tc) {
        static uint8_t *txbuf = NULL; static int txcap = 0;
        int tn = d.tex_w * d.tex_h * 4;
        if (tn > 0 && tn <= 4096 * 4096 * 4) {
            if (tn > txcap) { txcap = tn; txbuf = realloc(txbuf, txcap); }
            if (read_guest(txbuf, d.tex_rgba, (uint32_t)tn) == 0) {
                p_glEnable(GL_TEXTURE_2D); p_glBindTexture(GL_TEXTURE_2D, g_tex);
                p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, d.tex_w, d.tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, txbuf);
                p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)d.tex_wraps);
                p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)d.tex_wrapt);
                p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                p_glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, d.texenv ? (GLint)d.texenv : GL_MODULATE);
                p_glEnableClientState(GL_TEXTURE_COORD_ARRAY); p_glTexCoordPointer(2, GL_FLOAT, 0, tc);
            } else have_tc = 0;
        } else have_tc = 0;
    }
    if (!have_tc) { p_glDisable(GL_TEXTURE_2D); p_glDisableClientState(GL_TEXTURE_COORD_ARRAY); }
    /* pos holds 4 floats/vertex (x,y,z,w with z=0,w=1 defaults), so always size=4, tightly packed. */
    p_glEnableClientState(GL_VERTEX_ARRAY); p_glVertexPointer(4, GL_FLOAT, 0, pos);
    p_glEnableClientState(GL_COLOR_ARRAY);  p_glColorPointer(4, GL_FLOAT, 0, col);
    p_glDrawArrays(d.mode, 0, n);
}

/* The title's 2D SDL background (real libSDL -> /dev/fb0), RGB565, for compositing under the GL
   layer. devices.c owns these; a hybrid Caanoo title (menu) draws its background here and the menu
   items via GLES on a transparent clear. */
extern uint32_t g_fb_guest;
extern uint32_t g_fb_stride;

void glgpu_present(void) {
    if (g_ok != 1 || !g_shm || !g_rb) return;
    SDL_GL_MakeCurrent(g_win, g_ctx);
    p_glReadPixels(0, 0, g_w, g_h, GL_RGBA, GL_UNSIGNED_BYTE, g_rb);
    const uint16_t *bg = g_fb_guest ? (const uint16_t *)guest_to_host(g_fb_guest) : NULL;
    int bgpx = (int)(g_fb_stride ? g_fb_stride / 2 : 320);   /* SDL fb px per row */
    if (getenv("ME_GL_DBG")) { static int n = 0; if (n++ % 60 == 0) {
        int cx = g_w / 2, cy = g_h / 2; const uint8_t *c = g_rb + ((size_t)(g_h-1-cy)*g_w + cx)*4;
        uint16_t sb = (bg && cy < 240) ? bg[(size_t)cy*bgpx + cx] : 0xDEAD;
        fprintf(DIAG, "glgpu center: GL rgba=%d,%d,%d,%d  SDLbg565=%04x  fb_guest=%08x stride=%u\n",
                c[0], c[1], c[2], c[3], sb, g_fb_guest, g_fb_stride); } }
    uint16_t *dst = (uint16_t *)g_shm->pixels;
    for (int y = 0; y < g_h; y++) {
        const uint8_t *src = g_rb + (size_t)(g_h - 1 - y) * g_w * 4;   /* GL origin is bottom-left: flip */
        uint16_t *dp = dst + (size_t)y * GP2XSHM_MAXW;
        const uint16_t *bgrow = (bg && y < 240) ? bg + (size_t)y * bgpx : NULL;
        for (int x = 0; x < g_w; x++) {
            uint8_t a = src[x*4+3];
            if (a >= 24 || !bgrow || x >= bgpx) {            /* GL covered this pixel -> show GL */
                uint8_t r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                dp[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            } else {                                          /* transparent GL -> SDL 2D background */
                dp[x] = bgrow[x];
            }
        }
    }
    g_shm->width = g_w; g_shm->height = g_h; g_shm->frame_seq++;
}
#else  /* !ME_BUNDLED: no SDL/GL -> always software */
static int gpu_init(void) { return 0; }
void glgpu_resize(int w, int h) { (void)w; (void)h; }
void glgpu_clear(uint32_t p) { (void)p; }
void glgpu_draw(uint32_t d) { (void)d; }
void glgpu_present(void) {}
#endif

/* ---- public dispatch (called from the ME_NR_GL_* syscalls) ---- */
static int g_use_gpu = -1;
static int use_gpu(void) {
    if (g_use_gpu < 0) g_use_gpu = (gpu_wanted() && gpu_init()) ? 1 : 0;
    return g_use_gpu;
}
void glr_resize(int w, int h)   { glgpu_resize(w, h); glsw_resize(w, h); }   /* keep both sized */
void glr_clear(uint32_t packed) { if (use_gpu()) glgpu_clear(packed);   else glsw_clear(packed); }
void glr_draw(uint32_t desc)    { if (use_gpu()) glgpu_draw(desc);      else glsw_draw(desc); }

/* When a GLES title (fakegles offload) is presenting, it owns the shm framebuffer. A hybrid Caanoo
   title ALSO drives the real libSDL, whose 2D framebuffer present (present_active) would otherwise
   alternate with the GL output -> flicker. Gate present_active on this while GL is recently active. */
static double g_gl_last_present = 0;
int gl_owns_screen(void) { return g_gl_last_present != 0 && (host_now() - g_gl_last_present) < 0.3; }
void glr_present(void) {
    if (use_gpu()) glgpu_present(); else glsw_present();
    g_gl_last_present = host_now();
}
