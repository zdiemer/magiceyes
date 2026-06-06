/* fake OpenGL ES 1.1 + EGL for Caanoo (Pollux) binaries under qemu-user / the
 * native Unicorn engine.
 *
 * Caanoo commercial/homebrew .gpe (Propis, Rhythmos, ...) render through the
 * Pollux 3D GPU via libGLESv1_CM/libOpenEGL (Khronos names) or libopengles_lite/
 * libglport (the "lite" fixed-point stack). Rather than emulate the Pollux GPU
 * hardware, we REPLACE those libraries with this software rasterizer: it
 * implements the fixed-function GLES1.1 subset the games actually call and
 * rasterizes into the same /dev/shm RGB565 framebuffer the fake-SDL shim uses,
 * so the existing native SDL2 viewer presents it unchanged. No Pollux GPU,
 * /dev/mem, libMesNativeOEM or libDrv is ever touched.
 *
 * Scope = exactly the symbols Propis + Rhythmos import (probed from the .gpe):
 *   EGL:  GetDisplay Initialize ChooseConfig GetConfigs CreateContext
 *         CreateWindowSurface MakeCurrent SwapBuffers DestroySurface
 *         DestroyContext Terminate
 *   GL:   ClearColor Clear Viewport MatrixMode LoadIdentity Orthox Frustumx
 *         Translatef Rotatef Scalef Enable Disable BlendFunc AlphaFunc
 *         ShadeModel Enable/DisableClientState Vertex/Color/TexCoordPointer
 *         DrawArrays Gen/Delete/BindTexture TexImage2D CompressedTexImage2D
 *         TexParameteri TexEnvf TexEnvfv  (+ the MES glTextureDither no-op)
 *
 * Built with a mainline EABI cross toolchain (soft-float, armv5te) and staged
 * into assets/rootfs-eabi by host/win/stage_rootfs_eabi.sh under every GL/EGL
 * soname the games NEED. Self-contained: defines the Khronos types/enums it uses
 * (no GLES SDK headers required); depends on libc + libm + librt only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>

#include "gp2xshm.h"

/* ------------------------------------------------------------ Khronos ABI */
typedef unsigned int   GLenum;
typedef unsigned int   GLbitfield;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned int   GLuint;
typedef unsigned char  GLboolean;
typedef unsigned char  GLubyte;
typedef short          GLshort;
typedef unsigned short GLushort;
typedef float          GLfloat;
typedef float          GLclampf;
typedef int            GLfixed;      /* 16.16 fixed point */
typedef void           GLvoid;

typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef void *EGLNativeWindowType;
typedef void *EGLNativeDisplayType;
typedef unsigned int EGLBoolean;
typedef int  EGLint;

#define EGL_FALSE 0
#define EGL_TRUE  1

/* GL enums (standard values) */
#define GL_POINTS         0x0000
#define GL_LINES          0x0001
#define GL_LINE_STRIP     0x0003
#define GL_TRIANGLES      0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN   0x0006

#define GL_BYTE           0x1400
#define GL_UNSIGNED_BYTE  0x1401
#define GL_SHORT          0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT          0x1406
#define GL_FIXED          0x140C

#define GL_MODELVIEW      0x1700
#define GL_PROJECTION     0x1701
#define GL_TEXTURE        0x1702

#define GL_DEPTH_BUFFER_BIT   0x0100
#define GL_STENCIL_BUFFER_BIT 0x0400
#define GL_COLOR_BUFFER_BIT   0x4000

#define GL_TEXTURE_2D     0x0DE1
#define GL_BLEND          0x0BE2
#define GL_ALPHA_TEST     0x0BC0
#define GL_DEPTH_TEST     0x0B71
#define GL_CULL_FACE      0x0B44
#define GL_DITHER         0x0BD0
#define GL_SCISSOR_TEST   0x0C11
#define GL_LIGHTING       0x0B50
#define GL_FOG            0x0B60

#define GL_VERTEX_ARRAY        0x8074
#define GL_NORMAL_ARRAY        0x8075
#define GL_COLOR_ARRAY         0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078

#define GL_ZERO 0
#define GL_ONE  1
#define GL_SRC_COLOR           0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA           0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR           0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE  0x0308

#define GL_NEVER    0x0200
#define GL_LESS     0x0201
#define GL_EQUAL    0x0202
#define GL_LEQUAL   0x0203
#define GL_GREATER  0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL   0x0206
#define GL_ALWAYS   0x0207

#define GL_ALPHA            0x1906
#define GL_RGB              0x1907
#define GL_RGBA             0x1908
#define GL_LUMINANCE        0x1909
#define GL_LUMINANCE_ALPHA  0x190A
#define GL_BGRA             0x80E1

#define GL_UNSIGNED_SHORT_5_6_5   0x8363
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034

#define GL_TEXTURE_ENV       0x2300
#define GL_TEXTURE_ENV_MODE  0x2200
#define GL_MODULATE          0x2100
#define GL_DECAL             0x2101
#define GL_REPLACE           0x1E01
#define GL_ADD               0x0104

#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_NEAREST            0x2600
#define GL_LINEAR             0x2601
#define GL_CLAMP_TO_EDGE      0x812F
#define GL_REPEAT             0x2901

#define GL_FLAT   0x1D00
#define GL_SMOOTH 0x1D01

/* --------------------------------------------------------------- logging */
static int gl_log(void) {
    static int v = -1;
    if (v < 0) v = getenv("FAKEGLES_LOG") ? 1 : 0;
    return v;
}
#define GLOG(...) do { if (gl_log()) fprintf(stderr, "fakegles: " __VA_ARGS__); } while (0)

/* --------------------------------------------------------------- shm/fb */
static gp2x_shm_t *g_shm = NULL;
/* Set once this shim starts presenting; the fake-SDL shim weak-refs this and stops presenting
   its own (empty) screen so a hybrid SDL+GLES title (Propis/Rhythmos) shows the GL frame. */
int magiceyes_gl_active = 0;
static int   g_fbw = 320, g_fbh = 240;
static uint32_t *g_cbuf = NULL;     /* RGBA8888 colour buffer (g_fbw*g_fbh) */
static unsigned long g_start_ms = 0;

static unsigned long now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (unsigned long)tv.tv_sec * 1000UL + tv.tv_usec / 1000UL;
}
static void shm_init(void) {
    if (g_shm) return;
    /* Open the shm object directly rather than via shm_open(): the device glibc's shm_open
       statfs-checks /dev/shm and returns an empty path on our fake /proc, so it fails. The engine
       (DEV_SHMFB) and qemu both intercept open("/dev/shm/gp2x_fb") directly. */
    char shmpath[64]; snprintf(shmpath, sizeof shmpath, "/dev/shm%s", GP2XSHM_NAME);
    int fd = open(shmpath, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { fprintf(stderr, "fakegles: shm open(%s) failed\n", shmpath); return; }
    if (ftruncate(fd, sizeof(gp2x_shm_t)) != 0) { /* may already be sized */ }
    void *p = mmap(NULL, sizeof(gp2x_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { fprintf(stderr, "fakegles: mmap failed\n"); return; }
    g_shm = (gp2x_shm_t *)p;
    g_shm->buttons = 0; g_shm->quit = 0; g_shm->frame_seq = 0;
    g_shm->magic = GP2XSHM_MAGIC;
    g_start_ms = now_ms();
    fprintf(stderr, "fakegles: shm ready (%lu bytes)\n", (unsigned long)sizeof(gp2x_shm_t));
}
static void fb_resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (w > GP2XSHM_MAXW) w = GP2XSHM_MAXW;
    if (h > GP2XSHM_MAXH) h = GP2XSHM_MAXH;
    if (w == g_fbw && h == g_fbh && g_cbuf) return;
    g_fbw = w; g_fbh = h;
    free(g_cbuf);
    g_cbuf = (uint32_t *)calloc((size_t)g_fbw * g_fbh, 4);
    if (g_shm) { g_shm->width = g_fbw; g_shm->height = g_fbh; }
}
static void ensure_cbuf(void) {
    if (!g_shm) shm_init();
    if (!g_cbuf) fb_resize(g_fbw, g_fbh);
}

/* -------------------------------------------------------------- GL state */
typedef struct { GLboolean enabled; GLint size; GLenum type; GLsizei stride; const void *ptr; } Array;
typedef struct {
    int used, w, h;
    uint32_t *rgba;          /* w*h RGBA8888 */
    GLenum minf, magf, wraps, wrapt;
} Tex;

#define MAXTEX 4096
static Tex g_tex[MAXTEX];
static GLuint g_next_tex = 1;

static float g_mv[16], g_proj[16];   /* column-major, current values (no stack: games don't push/pop) */
static GLenum g_mmode = GL_MODELVIEW;
static GLint  g_vp[4] = { 0, 0, 320, 240 };

static Array g_av, g_ac, g_at;       /* vertex / colour / texcoord arrays */
static float g_cur_color[4] = { 1, 1, 1, 1 };

static int    g_en_tex = 0, g_en_blend = 0, g_en_atest = 0;
static GLenum g_blend_s = GL_ONE, g_blend_d = GL_ZERO;
static GLenum g_atest_func = GL_ALWAYS; static float g_atest_ref = 0;
static GLenum g_texenv = GL_MODULATE;
static GLuint g_bound_tex = 0;
static float  g_clear[4] = { 0, 0, 0, 1 };
static unsigned long g_n_draw = 0, g_n_clear = 0, g_n_tex = 0, g_n_swap = 0;  /* diag counters */

/* ----------------------------------------------------------- matrix math */
static void m_identity(float *m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
static float *cur_matrix(void) {
    return (g_mmode == GL_PROJECTION) ? g_proj : g_mv; /* GL_TEXTURE unused by targets */
}
/* r = a * b  (column-major 4x4) */
static void m_mul(float *r, const float *a, const float *b) {
    float t[16];
    int c, k;
    for (c = 0; c < 4; c++)
        for (k = 0; k < 4; k++)
            t[c*4+k] = a[0*4+k]*b[c*4+0] + a[1*4+k]*b[c*4+1]
                     + a[2*4+k]*b[c*4+2] + a[3*4+k]*b[c*4+3];
    memcpy(r, t, sizeof t);
}
/* out = m * (x,y,z,w) */
static void m_xform(const float *m, float x, float y, float z, float w, float *out) {
    out[0] = m[0]*x + m[4]*y + m[8]*z  + m[12]*w;
    out[1] = m[1]*x + m[5]*y + m[9]*z  + m[13]*w;
    out[2] = m[2]*x + m[6]*y + m[10]*z + m[14]*w;
    out[3] = m[3]*x + m[7]*y + m[11]*z + m[15]*w;
}
/* current = current * a */
static void mult_cur(const float *a) { float *m = cur_matrix(); m_mul(m, m, a); }

/* --------------------------------------------------------- GL: matrices */
void glMatrixMode(GLenum m) { g_mmode = m; }
void glLoadIdentity(void)   { m_identity(cur_matrix()); }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    g_vp[0] = x; g_vp[1] = y; g_vp[2] = w; g_vp[3] = h;
    if (x == 0 && y == 0 && w > 0 && h > 0) fb_resize(w, h);
}
void glOrthox(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) {
    float L=l/65536.f, R=r/65536.f, B=b/65536.f, T=t/65536.f, N=n/65536.f, F=f/65536.f;
    float o[16]; m_identity(o);
    if (R!=L) o[0]  = 2.0f/(R-L);
    if (T!=B) o[5]  = 2.0f/(T-B);
    if (F!=N) o[10] = -2.0f/(F-N);
    o[12] = -(R+L)/(R-L); o[13] = -(T+B)/(T-B); o[14] = -(F+N)/(F-N);
    mult_cur(o);
}
void glFrustumx(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) {
    float L=l/65536.f, R=r/65536.f, B=b/65536.f, T=t/65536.f, N=n/65536.f, F=f/65536.f;
    float m[16]; memset(m, 0, sizeof m);
    if (R!=L) m[0]  = 2*N/(R-L);
    if (T!=B) m[5]  = 2*N/(T-B);
    m[8] = (R+L)/(R-L); m[9] = (T+B)/(T-B);
    if (F!=N) m[10] = -(F+N)/(F-N);
    m[11] = -1;
    if (F!=N) m[14] = -2*F*N/(F-N);
    mult_cur(m);
}
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    float m[16]; m_identity(m); m[12]=x; m[13]=y; m[14]=z; mult_cur(m);
}
void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    float m[16]; m_identity(m); m[0]=x; m[5]=y; m[10]=z; mult_cur(m);
}
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    float rad = angle * 3.14159265358979f / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    float len = sqrtf(x*x + y*y + z*z);
    if (len < 1e-6f) return;
    x/=len; y/=len; z/=len;
    float nc = 1 - c;
    float m[16]; m_identity(m);
    m[0]=x*x*nc+c;   m[4]=x*y*nc-z*s; m[8] =x*z*nc+y*s;
    m[1]=y*x*nc+z*s; m[5]=y*y*nc+c;   m[9] =y*z*nc-x*s;
    m[2]=x*z*nc-y*s; m[6]=y*z*nc+x*s; m[10]=z*z*nc+c;
    mult_cur(m);
}

/* ----------------------------------------------------------- GL: state */
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    g_clear[0]=r; g_clear[1]=g; g_clear[2]=b; g_clear[3]=a;
}
void glEnable(GLenum c)  {
    if (c==GL_TEXTURE_2D) g_en_tex=1; else if (c==GL_BLEND) g_en_blend=1;
    else if (c==GL_ALPHA_TEST) g_en_atest=1;
}
void glDisable(GLenum c) {
    if (c==GL_TEXTURE_2D) g_en_tex=0; else if (c==GL_BLEND) g_en_blend=0;
    else if (c==GL_ALPHA_TEST) g_en_atest=0;
}
void glBlendFunc(GLenum s, GLenum d) { g_blend_s=s; g_blend_d=d; }
void glAlphaFunc(GLenum func, GLclampf ref) { g_atest_func=func; g_atest_ref=ref; }
void glShadeModel(GLenum m) { (void)m; }
void glTextureDither(GLint v) { (void)v; }      /* MES extension: no-op */

void glEnableClientState(GLenum a) {
    if (a==GL_VERTEX_ARRAY) g_av.enabled=1;
    else if (a==GL_COLOR_ARRAY) g_ac.enabled=1;
    else if (a==GL_TEXTURE_COORD_ARRAY) g_at.enabled=1;
}
void glDisableClientState(GLenum a) {
    if (a==GL_VERTEX_ARRAY) g_av.enabled=0;
    else if (a==GL_COLOR_ARRAY) g_ac.enabled=0;
    else if (a==GL_TEXTURE_COORD_ARRAY) g_at.enabled=0;
}
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    g_av.size=size; g_av.type=type; g_av.stride=stride; g_av.ptr=p;
}
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    g_ac.size=size; g_ac.type=type; g_ac.stride=stride; g_ac.ptr=p;
}
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    g_at.size=size; g_at.type=type; g_at.stride=stride; g_at.ptr=p;
}
void glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
    (void)target; if (pname==GL_TEXTURE_ENV_MODE) g_texenv=(GLenum)param;
}
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params) {
    (void)target; if (pname==GL_TEXTURE_ENV_MODE && params) g_texenv=(GLenum)params[0];
}

/* ---------------------------------------------------------- GL: textures */
void glGenTextures(GLsizei n, GLuint *t) {
    int i;
    for (i = 0; i < n; i++) {
        GLuint id = g_next_tex++;
        if (id >= MAXTEX) { t[i] = 0; continue; }
        memset(&g_tex[id], 0, sizeof(Tex));
        g_tex[id].used = 1;
        g_tex[id].minf = g_tex[id].magf = GL_LINEAR;
        g_tex[id].wraps = g_tex[id].wrapt = GL_REPEAT;
        t[i] = id;
    }
}
void glDeleteTextures(GLsizei n, const GLuint *t) {
    int i;
    for (i = 0; i < n; i++) {
        GLuint id = t[i];
        if (id && id < MAXTEX && g_tex[id].used) {
            free(g_tex[id].rgba); memset(&g_tex[id], 0, sizeof(Tex));
            if (g_bound_tex == id) g_bound_tex = 0;
        }
    }
}
void glBindTexture(GLenum target, GLuint t) { (void)target; g_bound_tex = t; }
void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    (void)target;
    if (!g_bound_tex || g_bound_tex >= MAXTEX) return;
    Tex *tx = &g_tex[g_bound_tex];
    if (pname==GL_TEXTURE_MIN_FILTER) tx->minf=(GLenum)param;
    else if (pname==GL_TEXTURE_MAG_FILTER) tx->magf=(GLenum)param;
    else if (pname==GL_TEXTURE_WRAP_S) tx->wraps=(GLenum)param;
    else if (pname==GL_TEXTURE_WRAP_T) tx->wrapt=(GLenum)param;
}
/* decode one source texel at linear index i into RGBA8 */
static uint32_t decode_texel(const uint8_t *src, GLenum format, GLenum type, int i) {
    uint8_t r=0,g=0,b=0,a=255;
    if (type==GL_UNSIGNED_BYTE) {
        if (format==GL_RGBA)            { const uint8_t*p=src+i*4; r=p[0];g=p[1];b=p[2];a=p[3]; }
        else if (format==GL_RGB)        { const uint8_t*p=src+i*3; r=p[0];g=p[1];b=p[2];a=255; }
        else if (format==GL_BGRA)       { const uint8_t*p=src+i*4; b=p[0];g=p[1];r=p[2];a=p[3]; }
        else if (format==GL_LUMINANCE)  { r=g=b=src[i]; a=255; }
        else if (format==GL_LUMINANCE_ALPHA){ const uint8_t*p=src+i*2; r=g=b=p[0]; a=p[1]; }
        else if (format==GL_ALPHA)      { r=g=b=255; a=src[i]; }
    } else if (type==GL_UNSIGNED_SHORT_5_6_5) {
        uint16_t v=((const uint16_t*)src)[i];
        r=(uint8_t)(((v>>11)&0x1f)*255/31); g=(uint8_t)(((v>>5)&0x3f)*255/63);
        b=(uint8_t)((v&0x1f)*255/31); a=255;
    } else if (type==GL_UNSIGNED_SHORT_4_4_4_4) {
        uint16_t v=((const uint16_t*)src)[i];
        r=(uint8_t)(((v>>12)&0xf)*17); g=(uint8_t)(((v>>8)&0xf)*17);
        b=(uint8_t)(((v>>4)&0xf)*17); a=(uint8_t)((v&0xf)*17);
    } else if (type==GL_UNSIGNED_SHORT_5_5_5_1) {
        uint16_t v=((const uint16_t*)src)[i];
        r=(uint8_t)(((v>>11)&0x1f)*255/31); g=(uint8_t)(((v>>6)&0x1f)*255/31);
        b=(uint8_t)(((v>>1)&0x1f)*255/31); a=(v&1)?255:0;
    }
    return ((uint32_t)a<<24)|((uint32_t)b<<16)|((uint32_t)g<<8)|r;
}
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)internalformat; (void)border;
    if (level != 0 || !g_bound_tex || g_bound_tex >= MAXTEX || w <= 0 || h <= 0) return;
    Tex *tx = &g_tex[g_bound_tex];
    free(tx->rgba);
    tx->w = w; tx->h = h;
    tx->rgba = (uint32_t *)malloc((size_t)w * h * 4);
    if (!tx->rgba) { tx->w = tx->h = 0; return; }
    g_n_tex++;
    if (pixels) {
        int i, n = w * h;
        for (i = 0; i < n; i++) tx->rgba[i] = decode_texel((const uint8_t *)pixels, format, type, i);
    } else {
        memset(tx->rgba, 0, (size_t)w * h * 4);
    }
    GLOG("glTexImage2D id=%u %dx%d fmt=%x type=%x\n", g_bound_tex, w, h, format, type);
}
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei w, GLsizei h,
                            GLint border, GLsizei imageSize, const GLvoid *data) {
    (void)target; (void)border; (void)data;
    /* The Pollux/MES compressed-texture format is undocumented; capture the enum so a
       decoder can be added once identified. Placeholder = opaque mid-grey so geometry
       still shows where a compressed texture would be. */
    if (level != 0 || !g_bound_tex || g_bound_tex >= MAXTEX || w <= 0 || h <= 0) return;
    fprintf(stderr, "fakegles: glCompressedTexImage2D UNHANDLED format=0x%x %dx%d size=%d "
                    "(placeholder; needs a decoder)\n", internalformat, w, h, imageSize);
    Tex *tx = &g_tex[g_bound_tex];
    free(tx->rgba);
    tx->w = w; tx->h = h;
    tx->rgba = (uint32_t *)malloc((size_t)w * h * 4);
    if (!tx->rgba) { tx->w = tx->h = 0; return; }
    int i, n = w * h;
    for (i = 0; i < n; i++) tx->rgba[i] = 0xFF808080u;
}

/* ----------------------------------------------------------- GL: clear */
void glClear(GLbitfield mask) {
    g_n_clear++;
    ensure_cbuf();
    if (!(mask & GL_COLOR_BUFFER_BIT) || !g_cbuf) return;
    uint8_t r=(uint8_t)(g_clear[0]*255+0.5f), g=(uint8_t)(g_clear[1]*255+0.5f),
            b=(uint8_t)(g_clear[2]*255+0.5f), a=(uint8_t)(g_clear[3]*255+0.5f);
    uint32_t c = ((uint32_t)a<<24)|((uint32_t)b<<16)|((uint32_t)g<<8)|r;
    int i, n = g_fbw * g_fbh;
    for (i = 0; i < n; i++) g_cbuf[i] = c;
}

/* ------------------------------------------------------ vertex fetch */
static int type_size(GLenum t) {
    switch (t) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_FLOAT: case GL_FIXED: return 4;
    default: return 4;
    }
}
static float read_comp(const uint8_t *base, GLenum type, int c) {
    switch (type) {
    case GL_FLOAT:          return ((const float *)base)[c];
    case GL_FIXED:          return ((const int32_t *)base)[c] / 65536.0f;
    case GL_BYTE:           return ((const int8_t *)base)[c];
    case GL_UNSIGNED_BYTE:  return ((const uint8_t *)base)[c];
    case GL_SHORT:          return ((const int16_t *)base)[c];
    case GL_UNSIGNED_SHORT: return ((const uint16_t *)base)[c];
    default:                return 0;
    }
}
static const uint8_t *elem_ptr(const Array *a, int idx) {
    int stride = a->stride ? a->stride : a->size * type_size(a->type);
    return (const uint8_t *)a->ptr + (size_t)idx * stride;
}

/* a transformed, ready-to-raster vertex */
typedef struct { float x, y, w; float u, v; float r, g, b, a; } Vtx;

static void fetch_vertex(int idx, Vtx *o) {
    /* position */
    float px=0, py=0, pz=0, pw=1;
    if (g_av.enabled && g_av.ptr) {
        const uint8_t *p = elem_ptr(&g_av, idx);
        px = read_comp(p, g_av.type, 0);
        if (g_av.size >= 2) py = read_comp(p, g_av.type, 1);
        if (g_av.size >= 3) pz = read_comp(p, g_av.type, 2);
        if (g_av.size >= 4) pw = read_comp(p, g_av.type, 3);
    }
    float eye[4], clip[4];
    m_xform(g_mv, px, py, pz, pw, eye);
    m_xform(g_proj, eye[0], eye[1], eye[2], eye[3], clip);
    float cw = clip[3]; if (cw == 0) cw = 1e-6f;
    float ndx = clip[0]/cw, ndy = clip[1]/cw;
    /* window coords; flip Y so row 0 is the top of our top-down buffer */
    o->x = g_vp[0] + (ndx*0.5f + 0.5f) * g_vp[2];
    o->y = (float)g_fbh - (g_vp[1] + (ndy*0.5f + 0.5f) * g_vp[3]);
    o->w = cw;
    /* texcoord */
    o->u = o->v = 0;
    if (g_at.enabled && g_at.ptr) {
        const uint8_t *p = elem_ptr(&g_at, idx);
        o->u = read_comp(p, g_at.type, 0);
        if (g_at.size >= 2) o->v = read_comp(p, g_at.type, 1);
    }
    /* colour */
    if (g_ac.enabled && g_ac.ptr) {
        const uint8_t *p = elem_ptr(&g_ac, idx);
        float sc = (g_ac.type==GL_UNSIGNED_BYTE || g_ac.type==GL_BYTE) ? 1.0f/255.0f : 1.0f;
        o->r = read_comp(p, g_ac.type, 0) * sc;
        o->g = read_comp(p, g_ac.type, 1) * sc;
        o->b = read_comp(p, g_ac.type, 2) * sc;
        o->a = (g_ac.size >= 4) ? read_comp(p, g_ac.type, 3) * sc : 1.0f;
    } else {
        o->r=g_cur_color[0]; o->g=g_cur_color[1]; o->b=g_cur_color[2]; o->a=g_cur_color[3];
    }
}

/* --------------------------------------------------------- rasterizer */
static uint32_t tex_sample(const Tex *t, float u, float v) {
    if (!t || !t->rgba || t->w <= 0 || t->h <= 0) return 0xFFFFFFFFu;
    int x, y;
    if (t->wraps == GL_REPEAT) { u -= floorf(u); x = (int)(u * t->w); }
    else { if (u < 0) u = 0; if (u > 1) u = 1; x = (int)(u * (t->w - 1) + 0.5f); }
    if (t->wrapt == GL_REPEAT) { v -= floorf(v); y = (int)(v * t->h); }
    else { if (v < 0) v = 0; if (v > 1) v = 1; y = (int)(v * (t->h - 1) + 0.5f); }
    if (x < 0) x = 0; if (x >= t->w) x = t->w - 1;
    if (y < 0) y = 0; if (y >= t->h) y = t->h - 1;
    return t->rgba[y * t->w + x];
}
static int alpha_pass(float a) {
    int v = (int)(a * 255 + 0.5f), ref = (int)(g_atest_ref * 255 + 0.5f);
    switch (g_atest_func) {
    case GL_NEVER:   return 0;
    case GL_LESS:    return v <  ref;
    case GL_EQUAL:   return v == ref;
    case GL_LEQUAL:  return v <= ref;
    case GL_GREATER: return v >  ref;
    case GL_NOTEQUAL:return v != ref;
    case GL_GEQUAL:  return v >= ref;
    default:         return 1;       /* GL_ALWAYS */
    }
}
static float blend_factor(GLenum f, float sa, float da, float sc, float dc) {
    switch (f) {
    case GL_ZERO: return 0; case GL_ONE: return 1;
    case GL_SRC_COLOR: return sc; case GL_ONE_MINUS_SRC_COLOR: return 1-sc;
    case GL_DST_COLOR: return dc; case GL_ONE_MINUS_DST_COLOR: return 1-dc;
    case GL_SRC_ALPHA: return sa; case GL_ONE_MINUS_SRC_ALPHA: return 1-sa;
    case GL_DST_ALPHA: return da; case GL_ONE_MINUS_DST_ALPHA: return 1-da;
    case GL_SRC_ALPHA_SATURATE: { float m = sa < 1-da ? sa : 1-da; return m; }
    default: return 1;
    }
}
static void put_frag(int x, int y, float fr, float fg, float fb, float fa) {
    if (x < 0 || y < 0 || x >= g_fbw || y >= g_fbh) return;
    if (g_en_atest && !alpha_pass(fa)) return;
    uint32_t *d = &g_cbuf[y * g_fbw + x];
    if (g_en_blend) {
        uint32_t dv = *d;
        float dr=(dv&0xff)/255.f, dg=((dv>>8)&0xff)/255.f, db=((dv>>16)&0xff)/255.f, da=((dv>>24)&0xff)/255.f;
        float sf, df;
        sf = blend_factor(g_blend_s, fa, da, fr, dr); df = blend_factor(g_blend_d, fa, da, fr, dr);
        float or_ = fr*sf + dr*df;
        sf = blend_factor(g_blend_s, fa, da, fg, dg); df = blend_factor(g_blend_d, fa, da, fg, dg);
        float og = fg*sf + dg*df;
        sf = blend_factor(g_blend_s, fa, da, fb, db); df = blend_factor(g_blend_d, fa, da, fb, db);
        float ob = fb*sf + db*df;
        sf = blend_factor(g_blend_s, fa, da, fa, da); df = blend_factor(g_blend_d, fa, da, fa, da);
        float oa = fa*sf + da*df;
        fr=or_; fg=og; fb=ob; fa=oa;
    }
    if (fr<0)fr=0; if (fr>1)fr=1; if (fg<0)fg=0; if (fg>1)fg=1;
    if (fb<0)fb=0; if (fb>1)fb=1; if (fa<0)fa=0; if (fa>1)fa=1;
    *d = ((uint32_t)(fa*255+0.5f)<<24)|((uint32_t)(fb*255+0.5f)<<16)
       | ((uint32_t)(fg*255+0.5f)<<8)|(uint32_t)(fr*255+0.5f);
}
static float edge(const Vtx *a, const Vtx *b, float px, float py) {
    return (px - a->x) * (b->y - a->y) - (py - a->y) * (b->x - a->x);
}
static void raster_tri(const Vtx *v0, const Vtx *v1, const Vtx *v2) {
    float minx = v0->x, maxx = v0->x, miny = v0->y, maxy = v0->y;
    if (v1->x<minx)minx=v1->x; if (v1->x>maxx)maxx=v1->x;
    if (v2->x<minx)minx=v2->x; if (v2->x>maxx)maxx=v2->x;
    if (v1->y<miny)miny=v1->y; if (v1->y>maxy)maxy=v1->y;
    if (v2->y<miny)miny=v2->y; if (v2->y>maxy)maxy=v2->y;
    int x0=(int)floorf(minx), x1=(int)ceilf(maxx), y0=(int)floorf(miny), y1=(int)ceilf(maxy);
    if (x0<0)x0=0; if (y0<0)y0=0; if (x1>g_fbw)x1=g_fbw; if (y1>g_fbh)y1=g_fbh;
    float area = edge(v0, v1, v2->x, v2->y);
    if (fabsf(area) < 1e-6f) return;
    const Tex *tx = (g_en_tex && g_bound_tex && g_bound_tex < MAXTEX && g_tex[g_bound_tex].used)
                  ? &g_tex[g_bound_tex] : NULL;
    int x, y;
    for (y = y0; y < y1; y++) {
        float cy = y + 0.5f;
        for (x = x0; x < x1; x++) {
            float cx = x + 0.5f;
            float w0 = edge(v1, v2, cx, cy), w1 = edge(v2, v0, cx, cy), w2 = edge(v0, v1, cx, cy);
            /* inside if all same sign as area (covers both windings) */
            if (area > 0) { if (w0 < 0 || w1 < 0 || w2 < 0) continue; }
            else          { if (w0 > 0 || w1 > 0 || w2 > 0) continue; }
            float l0 = w0/area, l1 = w1/area, l2 = w2/area;
            float fr = l0*v0->r + l1*v1->r + l2*v2->r;
            float fg = l0*v0->g + l1*v1->g + l2*v2->g;
            float fb = l0*v0->b + l1*v1->b + l2*v2->b;
            float fa = l0*v0->a + l1*v1->a + l2*v2->a;
            if (tx) {
                float u = l0*v0->u + l1*v1->u + l2*v2->u;
                float vv = l0*v0->v + l1*v1->v + l2*v2->v;
                uint32_t t = tex_sample(tx, u, vv);
                float tr=(t&0xff)/255.f, tg=((t>>8)&0xff)/255.f, tb=((t>>16)&0xff)/255.f, ta=((t>>24)&0xff)/255.f;
                if (g_texenv == GL_REPLACE) { fr=tr; fg=tg; fb=tb; fa=ta; }
                else { fr*=tr; fg*=tg; fb*=tb; fa*=ta; }     /* MODULATE (default) */
            }
            put_frag(x, y, fr, fg, fb, fa);
        }
    }
}
void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    g_n_draw++;
    ensure_cbuf();
    if (!g_cbuf || count <= 0 || !g_av.enabled) return;
    Vtx *v = (Vtx *)malloc((size_t)count * sizeof(Vtx));
    if (!v) return;
    int i;
    for (i = 0; i < count; i++) fetch_vertex(first + i, &v[i]);
    if (mode == GL_TRIANGLES) {
        for (i = 0; i + 2 < count; i += 3) raster_tri(&v[i], &v[i+1], &v[i+2]);
    } else if (mode == GL_TRIANGLE_STRIP) {
        for (i = 0; i + 2 < count; i++) raster_tri(&v[i], &v[i+1], &v[i+2]);
    } else if (mode == GL_TRIANGLE_FAN) {
        for (i = 1; i + 1 < count; i++) raster_tri(&v[0], &v[i], &v[i+1]);
    } else {
        GLOG("glDrawArrays unhandled mode=0x%x count=%d\n", mode, count);
    }
    free(v);
}

/* ----------------------------------------------------------------- EGL */
/* One display/config/context/surface; we render to the shm fb regardless. */
static int g_egl_dpy, g_egl_cfg, g_egl_ctx, g_egl_surf;  /* addresses used as opaque handles */
static unsigned long g_last_swap_ms = 0;

/* MagicEyes/Pollux native window creation (normally in libDrv). Caanoo titles call it to make
   the native window handed to eglCreateWindowSurface. We render to the shm framebuffer and ignore
   the window, so return a non-NULL dummy handle. K&R parens accept whatever args the game passes. */
void *OS_CreateWindow() { ensure_cbuf(); return (void *)&g_egl_surf; }

EGLDisplay eglGetDisplay(EGLNativeDisplayType d) { (void)d; return (EGLDisplay)&g_egl_dpy; }
EGLBoolean eglInitialize(EGLDisplay d, EGLint *major, EGLint *minor) {
    (void)d; shm_init(); if (major)*major=1; if (minor)*minor=1;
    GLOG("eglInitialize\n");
    return EGL_TRUE;
}
EGLBoolean eglTerminate(EGLDisplay d) { (void)d; return EGL_TRUE; }
EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint *attrib, EGLConfig *configs,
                           EGLint config_size, EGLint *num_config) {
    (void)d; (void)attrib;
    if (configs && config_size > 0) configs[0] = (EGLConfig)&g_egl_cfg;
    if (num_config) *num_config = (config_size > 0) ? 1 : 0;
    return EGL_TRUE;
}
EGLBoolean eglGetConfigs(EGLDisplay d, EGLConfig *configs, EGLint config_size, EGLint *num_config) {
    (void)d;
    if (configs && config_size > 0) configs[0] = (EGLConfig)&g_egl_cfg;
    if (num_config) *num_config = (config_size > 0) ? 1 : 0;
    return EGL_TRUE;
}
EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *attrib) {
    (void)d; (void)c; (void)share; (void)attrib; return (EGLContext)&g_egl_ctx;
}
EGLBoolean eglDestroyContext(EGLDisplay d, EGLContext c) { (void)d; (void)c; return EGL_TRUE; }
EGLSurface eglCreateWindowSurface(EGLDisplay d, EGLConfig c, EGLNativeWindowType win,
                                  const EGLint *attrib) {
    (void)d; (void)c; (void)win; (void)attrib;
    ensure_cbuf();
    magiceyes_gl_active = 1;       /* GL owns the shm framebuffer now (suppress fake-SDL present) */
    return (EGLSurface)&g_egl_surf;
}
EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface s) { (void)d; (void)s; return EGL_TRUE; }
EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext c) {
    (void)d; (void)draw; (void)read; (void)c;
    m_identity(g_mv); m_identity(g_proj);
    ensure_cbuf();
    magiceyes_gl_active = 1;
    GLOG("eglMakeCurrent (GL now owns the framebuffer)\n");
    return EGL_TRUE;
}
/* Present the colour buffer to the shm RGB565 framebuffer + frame-cap like SDL_Flip. */
EGLBoolean eglSwapBuffers(EGLDisplay d, EGLSurface s) {
    (void)d; (void)s;
    if (!g_shm || !g_cbuf) return EGL_TRUE;
    uint16_t *dst = (uint16_t *)g_shm->pixels;
    int x, y;
    for (y = 0; y < g_fbh; y++) {
        for (x = 0; x < g_fbw; x++) {
            uint32_t p = g_cbuf[y * g_fbw + x];
            uint8_t r = p & 0xff, g = (p>>8) & 0xff, b = (p>>16) & 0xff;
            dst[y * GP2XSHM_MAXW + x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
    g_shm->width = g_fbw; g_shm->height = g_fbh; g_shm->frame_seq++;
    if (gl_log() && (++g_n_swap % 120) == 1) {
        int nz = 0; for (int i = 0; i < g_fbw * g_fbh; i++) if (g_cbuf[i] & 0x00FFFFFF) { nz = 1; break; }
        fprintf(stderr, "fakegles: swap #%lu draws=%lu clears=%lu texs=%lu tex_bound=%u en_tex=%d nonblack=%d\n",
                g_n_swap, g_n_draw, g_n_clear, g_n_tex, g_bound_tex, g_en_tex, nz);
    }
    /* frame cap (FAKESDL_FPS, default 60) — GP2X eglSwapBuffers blocks on vsync */
    static long frame_ms = -1;
    if (frame_ms < 0) {
        const char *e = getenv("FAKESDL_FPS"); int fps = e ? atoi(e) : 60;
        if (fps <= 0) fps = 60; frame_ms = 1000 / fps;
    }
    if (g_last_swap_ms) {
        long el = (long)(now_ms() - g_last_swap_ms);
        while (el < frame_ms) {
            struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (frame_ms - el) * 1000000L;
            nanosleep(&ts, NULL);
            el = (long)(now_ms() - g_last_swap_ms);
        }
    }
    g_last_swap_ms = now_ms();
    return EGL_TRUE;
}
