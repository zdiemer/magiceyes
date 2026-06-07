/* GL render-offload command contract between the guest fake-GLES shim (emulated ARM) and the
 * native engine rasterizer. The shim no longer rasterizes in the emulated CPU (the dominant cost
 * on low-spec hosts); it records each draw's state into a struct gl_draw in guest memory and
 * issues a custom syscall. The engine reads the descriptor + the guest vertex arrays + the guest
 * (already-decoded RGBA) texture via guest_to_host and rasterizes NATIVELY. Keep this header
 * dependency-free (stdint only); both ends are little-endian so it maps 1:1. */
#ifndef GLCMD_H
#define GLCMD_H
#include <stdint.h>

/* custom syscall numbers (well above any Linux/ARM syscall; r7 on the EABI svc 0). */
#define ME_NR_GL_RESIZE   0x000E0001u   /* r0=w r1=h : (re)size the native colour buffer */
#define ME_NR_GL_CLEAR    0x000E0002u   /* r0=packed RGBA8 (a<<24|b<<16|g<<8|r) : clear cbuf */
#define ME_NR_GL_DRAW     0x000E0003u   /* r0=guest ptr to struct gl_draw : rasterize one draw */
#define ME_NR_GL_PRESENT  0x000E0004u   /* present cbuf -> shm RGB565 (frame_seq++) */

/* Telemetry: the guest shims surface an unsupported feature into the engine's structured run
   report (host/engine/report.h) by writing a sentinel line to stderr -- "\x01MR <kind> <code>
   <name>\n" -- which the engine ingests in its write() handler (me_report_ingest_guest). A line,
   not a custom syscall, because the OABI GPH-SDK shim toolchain has no `svc`. The kind numbers
   MUST match enum me_report_kind in report.h. Emitted only when ME_DEBUG is in the guest env. */
#define ME_RPT_GLES       9    /* MR_UNSUPPORTED_GLES */
#define ME_RPT_AUDIO      10   /* MR_UNSUPPORTED_AUDIO */
#define ME_RPT_SDL        11   /* MR_UNSUPPORTED_SDL */

struct gl_array {       /* a client vertex-attribute array (guest pointers; engine derefs them) */
    uint32_t ptr;       /* guest address of the array base (0 / en=0 if disabled) */
    int32_t  size;      /* components per element */
    uint32_t type;      /* GL type enum (FLOAT/FIXED/BYTE/UBYTE/SHORT/USHORT) */
    int32_t  stride;    /* byte stride (0 = tightly packed) */
    int32_t  en;        /* 1 if enabled */
};

struct gl_draw {        /* one glDrawArrays, fully self-describing */
    uint32_t mode, first, count;
    float    mv[16], proj[16];           /* modelview, projection (column-major) */
    int32_t  vp[4];                      /* viewport x,y,w,h */
    int32_t  en_tex, en_blend, en_atest;
    uint32_t blend_s, blend_d, atest_func, texenv;
    float    atest_ref;
    struct gl_array av, ac, at;          /* vertex / colour / texcoord arrays */
    float    cur_color[4];               /* current colour when the colour array is disabled */
    uint32_t tex_rgba;                   /* guest addr of the bound texture's decoded RGBA (0=none) */
    int32_t  tex_w, tex_h;
    uint32_t tex_wraps, tex_wrapt;
};

#endif /* GLCMD_H */
