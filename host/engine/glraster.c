/* Native GL rasterizer (render offload). The guest fake-GLES shim records each draw into a
 * struct gl_draw (glcmd.h) and issues a syscall; this runs the SAME fixed-function rasterizer the
 * shim used to run -- but NATIVELY, reading the guest vertex arrays + decoded-RGBA textures via
 * guest_to_host (zero-copy). That moves the per-pixel work off the emulated CPU (the dominant cost
 * for GLES titles on low-spec hosts). Writes a native RGBA colour buffer, presented to the shm
 * RGB565 framebuffer on GL_PRESENT. */
#include "engine.h"
#include "glcmd.h"
#include <math.h>

/* GL type/enum subset (matches guest fakegles). */
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_FIXED 0x140C
#define GL_REPEAT 0x2901
#define GL_REPLACE 0x1E01
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206

static uint32_t *g_glcbuf = NULL;     /* native RGBA8888 colour buffer */
static int g_glw = 320, g_glh = 240;
static int glr_log(void) { static int v = -1; if (v < 0) v = getenv("ME_GLR_LOG") ? 1 : 0; return v; }

void glr_resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (w > GP2XSHM_MAXW) w = GP2XSHM_MAXW;
    if (h > GP2XSHM_MAXH) h = GP2XSHM_MAXH;
    if (w == g_glw && h == g_glh && g_glcbuf) return;
    g_glw = w; g_glh = h;
    free(g_glcbuf); g_glcbuf = calloc((size_t)g_glw * g_glh, 4);
}
static void ensure_cbuf(void) { if (!g_glcbuf) g_glcbuf = calloc((size_t)g_glw * g_glh, 4); }

void glr_clear(uint32_t packed) {
    ensure_cbuf(); if (!g_glcbuf) return;
    int n = g_glw * g_glh;
    for (int i = 0; i < n; i++) g_glcbuf[i] = packed;
}

void glr_present(void) {
    ensure_cbuf();
    if (!g_shm || !g_glcbuf) return;
    uint16_t *dst = (uint16_t *)g_shm->pixels;
    for (int y = 0; y < g_glh; y++)
        for (int x = 0; x < g_glw; x++) {
            uint32_t p = g_glcbuf[(size_t)y * g_glw + x];
            uint8_t r = p & 0xff, g = (p>>8)&0xff, b = (p>>16)&0xff;
            dst[(size_t)y * GP2XSHM_MAXW + x] = (uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3));
        }
    g_shm->width = g_glw; g_shm->height = g_glh; g_shm->frame_seq++;
    if (glr_log()) { static unsigned f=0; unsigned step = getenv("ME_GLR_EVERYFRAME")?1:60;
        if ((f++ % step)==0) {
        int nz=0,n=g_glw*g_glh; for(int i=0;i<n;i++) if(g_glcbuf[i]&0x00FFFFFF){nz++;}
        fprintf(DIAG,"glr_present #%u cbuf_nonblack=%d/%d ==== SWAP ====\n",f,nz,n); } }
}

/* ---- the rasterizer (ported from guest fakegles; reads guest mem via guest_to_host) ---- */
typedef struct { float x, y, w, u, v, r, g, b, a; } Vtx;

static void m_xform(const float *m, float x, float y, float z, float w, float *o) {
    o[0]=m[0]*x+m[4]*y+m[8]*z+m[12]*w;  o[1]=m[1]*x+m[5]*y+m[9]*z+m[13]*w;
    o[2]=m[2]*x+m[6]*y+m[10]*z+m[14]*w; o[3]=m[3]*x+m[7]*y+m[11]*z+m[15]*w;
}
static int type_size(uint32_t t) {
    switch (t) { case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
                 case GL_SHORT: case GL_UNSIGNED_SHORT: return 2; default: return 4; }
}
static float read_comp(const uint8_t *b, uint32_t type, int c) {
    switch (type) {
    case GL_FLOAT:          return ((const float *)b)[c];
    case GL_FIXED:          return ((const int32_t *)b)[c] / 65536.0f;
    case GL_BYTE:           return ((const int8_t *)b)[c];
    case GL_UNSIGNED_BYTE:  return ((const uint8_t *)b)[c];
    case GL_SHORT:          return ((const int16_t *)b)[c];
    case GL_UNSIGNED_SHORT: return ((const uint16_t *)b)[c];
    default:                return 0;
    }
}
/* Copy array element idx into buf (the bytes are contiguous in GUEST space but their host
   backing may span non-contiguous region mmaps, so read_guest stitches it). 1 on success. */
static int elem(const struct gl_array *a, int idx, uint8_t *buf, int bufsz) {
    if (!a->en || !a->ptr || a->size <= 0) return 0;
    int esz = a->size * type_size(a->type);
    if (esz <= 0 || esz > bufsz) return 0;
    int stride = a->stride ? a->stride : esz;
    return read_guest(buf, a->ptr + (uint32_t)idx * stride, (uint32_t)esz) == 0;
}
static void fetch(const struct gl_draw *d, int idx, Vtx *o) {
    uint8_t buf[64];
    float px=0,py=0,pz=0,pw=1;
    if (elem(&d->av, idx, buf, sizeof buf)) {
             px=read_comp(buf,d->av.type,0);
             if (d->av.size>=2) py=read_comp(buf,d->av.type,1);
             if (d->av.size>=3) pz=read_comp(buf,d->av.type,2);
             if (d->av.size>=4) pw=read_comp(buf,d->av.type,3); }
    float eye[4], clip[4];
    m_xform(d->mv, px,py,pz,pw, eye);
    m_xform(d->proj, eye[0],eye[1],eye[2],eye[3], clip);
    float cw = clip[3]; if (cw==0) cw=1e-6f;
    float ndx=clip[0]/cw, ndy=clip[1]/cw;
    o->x = d->vp[0] + (ndx*0.5f+0.5f)*d->vp[2];
    o->y = (float)g_glh - (d->vp[1] + (ndy*0.5f+0.5f)*d->vp[3]);
    o->w = cw;
    o->u=o->v=0;
    if (elem(&d->at, idx, buf, sizeof buf)) {
        o->u=read_comp(buf,d->at.type,0); if (d->at.size>=2) o->v=read_comp(buf,d->at.type,1); }
    if (elem(&d->ac, idx, buf, sizeof buf)) {
             float sc=(d->ac.type==GL_UNSIGNED_BYTE||d->ac.type==GL_BYTE)?1/255.f:1.f;
             o->r=read_comp(buf,d->ac.type,0)*sc; o->g=read_comp(buf,d->ac.type,1)*sc;
             o->b=read_comp(buf,d->ac.type,2)*sc; o->a=(d->ac.size>=4)?read_comp(buf,d->ac.type,3)*sc:1.f; }
    else { o->r=d->cur_color[0]; o->g=d->cur_color[1]; o->b=d->cur_color[2]; o->a=d->cur_color[3]; }
}

static uint32_t tex_sample(const uint32_t *tx, int tw, int th, uint32_t ws, uint32_t wt, float u, float v) {
    if (!tx || tw<=0 || th<=0) return 0xFFFFFFFFu;
    int x, y;
    if (ws==GL_REPEAT) { u-=floorf(u); x=(int)(u*tw); } else { if(u<0)u=0; if(u>1)u=1; x=(int)(u*(tw-1)+0.5f); }
    if (wt==GL_REPEAT) { v-=floorf(v); y=(int)(v*th); } else { if(v<0)v=0; if(v>1)v=1; y=(int)(v*(th-1)+0.5f); }
    if(x<0)x=0; if(x>=tw)x=tw-1; if(y<0)y=0; if(y>=th)y=th-1;
    return tx[(size_t)y*tw+x];
}
static int alpha_pass(uint32_t func, float ref, float a) {
    int v=(int)(a*255+0.5f), r=(int)(ref*255+0.5f);
    switch(func){case GL_NEVER:return 0;case GL_LESS:return v<r;case GL_EQUAL:return v==r;
        case GL_LEQUAL:return v<=r;case GL_GREATER:return v>r;case GL_NOTEQUAL:return v!=r;
        case GL_GEQUAL:return v>=r;default:return 1;}
}
static float bfac(uint32_t f, float sa, float da, float sc, float dc) {
    switch(f){case GL_ZERO:return 0;case GL_ONE:return 1;
        case GL_SRC_COLOR:return sc;case GL_ONE_MINUS_SRC_COLOR:return 1-sc;
        case GL_DST_COLOR:return dc;case GL_ONE_MINUS_DST_COLOR:return 1-dc;
        case GL_SRC_ALPHA:return sa;case GL_ONE_MINUS_SRC_ALPHA:return 1-sa;
        case GL_DST_ALPHA:return da;case GL_ONE_MINUS_DST_ALPHA:return 1-da;
        case GL_SRC_ALPHA_SATURATE:{float m=sa<1-da?sa:1-da;return m;}default:return 1;}
}
static long g_glr_frags = 0;   /* fragments actually written (diagnostic) */
static void put_frag(const struct gl_draw *d, int x, int y, float fr, float fg, float fb, float fa) {
    if (x<0||y<0||x>=g_glw||y>=g_glh) return;
    if (d->en_atest && !alpha_pass(d->atest_func, d->atest_ref, fa)) return;
    g_glr_frags++;
    uint32_t *dp = &g_glcbuf[(size_t)y*g_glw+x];
    if (d->en_blend) {
        uint32_t dv=*dp; float dr=(dv&0xff)/255.f,dg=((dv>>8)&0xff)/255.f,db=((dv>>16)&0xff)/255.f,da=((dv>>24)&0xff)/255.f;
        float s,t;
        s=bfac(d->blend_s,fa,da,fr,dr); t=bfac(d->blend_d,fa,da,fr,dr); float or_=fr*s+dr*t;
        s=bfac(d->blend_s,fa,da,fg,dg); t=bfac(d->blend_d,fa,da,fg,dg); float og=fg*s+dg*t;
        s=bfac(d->blend_s,fa,da,fb,db); t=bfac(d->blend_d,fa,da,fb,db); float ob=fb*s+db*t;
        s=bfac(d->blend_s,fa,da,fa,da); t=bfac(d->blend_d,fa,da,fa,da); float oa=fa*s+da*t;
        fr=or_;fg=og;fb=ob;fa=oa;
    }
    if(fr<0)fr=0;if(fr>1)fr=1;if(fg<0)fg=0;if(fg>1)fg=1;if(fb<0)fb=0;if(fb>1)fb=1;if(fa<0)fa=0;if(fa>1)fa=1;
    *dp=((uint32_t)(fa*255+0.5f)<<24)|((uint32_t)(fb*255+0.5f)<<16)|((uint32_t)(fg*255+0.5f)<<8)|(uint32_t)(fr*255+0.5f);
}
static float edge(const Vtx *a, const Vtx *b, float px, float py) {
    return (px-a->x)*(b->y-a->y) - (py-a->y)*(b->x-a->x);
}
static void raster_tri(const struct gl_draw *d, const uint32_t *tx, const Vtx *v0, const Vtx *v1, const Vtx *v2) {
    float minx=v0->x,maxx=v0->x,miny=v0->y,maxy=v0->y;
    if(v1->x<minx)minx=v1->x;if(v1->x>maxx)maxx=v1->x;if(v2->x<minx)minx=v2->x;if(v2->x>maxx)maxx=v2->x;
    if(v1->y<miny)miny=v1->y;if(v1->y>maxy)maxy=v1->y;if(v2->y<miny)miny=v2->y;if(v2->y>maxy)maxy=v2->y;
    int x0=(int)floorf(minx),x1=(int)ceilf(maxx),y0=(int)floorf(miny),y1=(int)ceilf(maxy);
    if(x0<0)x0=0;if(y0<0)y0=0;if(x1>g_glw)x1=g_glw;if(y1>g_glh)y1=g_glh;
    if(x1<=x0||y1<=y0)return;
    float area=edge(v0,v1,v2->x,v2->y); if(fabsf(area)<1e-6f)return;
    float inv=1.0f/area;
    float dx0=v2->y-v1->y,dy0=v1->x-v2->x, dx1=v0->y-v2->y,dy1=v2->x-v0->x, dx2=v1->y-v0->y,dy2=v0->x-v1->x;
    float cx0=x0+0.5f,cy0=y0+0.5f;
    float w0r=edge(v1,v2,cx0,cy0),w1r=edge(v2,v0,cx0,cy0),w2r=edge(v0,v1,cx0,cy0);
    int pos=area>0;
    int flat=(v0->r==v1->r&&v1->r==v2->r&&v0->g==v1->g&&v1->g==v2->g&&v0->b==v1->b&&v1->b==v2->b&&v0->a==v1->a&&v1->a==v2->a);
    for(int y=y0;y<y1;y++){
        float w0=w0r,w1=w1r,w2=w2r;
        for(int x=x0;x<x1;x++,w0+=dx0,w1+=dx1,w2+=dx2){
            if(pos){if(w0<0||w1<0||w2<0)continue;}else{if(w0>0||w1>0||w2>0)continue;}
            float fr,fg,fb,fa;
            if(flat){fr=v0->r;fg=v0->g;fb=v0->b;fa=v0->a;}
            else{float l0=w0*inv,l1=w1*inv,l2=w2*inv;
                 fr=l0*v0->r+l1*v1->r+l2*v2->r;fg=l0*v0->g+l1*v1->g+l2*v2->g;
                 fb=l0*v0->b+l1*v1->b+l2*v2->b;fa=l0*v0->a+l1*v1->a+l2*v2->a;}
            if(tx){float l0=w0*inv,l1=w1*inv,l2=w2*inv;
                 float u=l0*v0->u+l1*v1->u+l2*v2->u, vv=l0*v0->v+l1*v1->v+l2*v2->v;
                 uint32_t t=tex_sample(tx,d->tex_w,d->tex_h,d->tex_wraps,d->tex_wrapt,u,vv);
                 float tr=(t&0xff)/255.f,tg=((t>>8)&0xff)/255.f,tb=((t>>16)&0xff)/255.f,ta=((t>>24)&0xff)/255.f;
                 if(d->texenv==GL_REPLACE){fr=tr;fg=tg;fb=tb;fa=ta;}else{fr*=tr;fg*=tg;fb*=tb;fa*=ta;}}
            put_frag(d,x,y,fr,fg,fb,fa);
        }
        w0r+=dy0;w1r+=dy1;w2r+=dy2;
    }
}
static int fast_quad(const struct gl_draw *d, const uint32_t *tx, const Vtx *v, int n) {
    if (n!=4 || (d->mode!=GL_TRIANGLE_STRIP && d->mode!=GL_TRIANGLE_FAN)) return 0;
    float xL=v[0].x,xR=v[0].x,yT=v[0].y,yB=v[0].y;
    for(int i=1;i<4;i++){if(v[i].x<xL)xL=v[i].x;if(v[i].x>xR)xR=v[i].x;if(v[i].y<yT)yT=v[i].y;if(v[i].y>yB)yB=v[i].y;}
    const float E=0.02f;
    for(int i=0;i<4;i++){int ox=fabsf(v[i].x-xL)<E||fabsf(v[i].x-xR)<E,oy=fabsf(v[i].y-yT)<E||fabsf(v[i].y-yB)<E; if(!ox||!oy)return 0;}
    if(xR-xL<0.5f||yB-yT<0.5f)return 1;
    int flat=(v[0].r==v[1].r&&v[1].r==v[2].r&&v[2].r==v[3].r&&v[0].g==v[1].g&&v[1].g==v[2].g&&v[2].g==v[3].g&&
              v[0].b==v[1].b&&v[1].b==v[2].b&&v[2].b==v[3].b&&v[0].a==v[1].a&&v[1].a==v[2].a&&v[2].a==v[3].a);
    if(!flat&&!tx)return 0;
    int rx0=(int)floorf(xL),rx1=(int)ceilf(xR),ry0=(int)floorf(yT),ry1=(int)ceilf(yB);
    if(rx0<0)rx0=0;if(ry0<0)ry0=0;if(rx1>g_glw)rx1=g_glw;if(ry1>g_glh)ry1=g_glh;
    if(rx1<=rx0||ry1<=ry0)return 1;
    float cr=v[0].r,cg=v[0].g,cb=v[0].b,ca=v[0].a;
    if(tx){
        float uL=0,uR=0,vT=0,vB=0;
        for(int i=0;i<4;i++){if(fabsf(v[i].x-xL)<E)uL=v[i].u;else uR=v[i].u; if(fabsf(v[i].y-yT)<E)vT=v[i].v;else vB=v[i].v;}
        float du=(uR-uL)/(xR-xL),dv=(vB-vT)/(yB-yT);
        for(int y=ry0;y<ry1;y++){
            float fvv=vT+dv*((y+0.5f)-yT), fu=uL+du*((rx0+0.5f)-xL);
            for(int x=rx0;x<rx1;x++,fu+=du){
                uint32_t t=tex_sample(tx,d->tex_w,d->tex_h,d->tex_wraps,d->tex_wrapt,fu,fvv);
                float tr=(t&0xff)/255.f,tg=((t>>8)&0xff)/255.f,tb=((t>>16)&0xff)/255.f,ta=((t>>24)&0xff)/255.f;
                if(d->texenv==GL_REPLACE)put_frag(d,x,y,tr,tg,tb,ta);
                else if(flat)put_frag(d,x,y,tr*cr,tg*cg,tb*cb,ta*ca);
                else put_frag(d,x,y,tr,tg,tb,ta);
            }
        }
    } else if(!d->en_blend&&!d->en_atest){
        float r=cr,g=cg,b=cb,a=ca; if(r<0)r=0;if(r>1)r=1;if(g<0)g=0;if(g>1)g=1;if(b<0)b=0;if(b>1)b=1;if(a<0)a=0;if(a>1)a=1;
        uint32_t pc=((uint32_t)(a*255+0.5f)<<24)|((uint32_t)(b*255+0.5f)<<16)|((uint32_t)(g*255+0.5f)<<8)|(uint32_t)(r*255+0.5f);
        for(int y=ry0;y<ry1;y++){uint32_t*row=&g_glcbuf[(size_t)y*g_glw];for(int x=rx0;x<rx1;x++)row[x]=pc;}
    } else {
        for(int y=ry0;y<ry1;y++)for(int x=rx0;x<rx1;x++)put_frag(d,x,y,cr,cg,cb,ca);
    }
    return 1;
}

/* Rasterize one draw. desc_ptr = guest address of a struct gl_draw. */
/* contiguous scratch for the bound texture (a guest texture may span non-contiguous host
   regions; copy it flat so the rasterizer can index it). Serialized by the syscall biglock. */
static uint32_t *g_texbuf = NULL; static size_t g_texbuf_cap = 0;

void glr_draw(uint32_t desc_ptr) {
    ensure_cbuf(); if (!g_glcbuf) return;
    struct gl_draw dd;
    if (read_guest(&dd, desc_ptr, sizeof dd) != 0) return;   /* descriptor unmapped */
    const struct gl_draw *d = &dd;
    if (glr_log()) { static int n = 0; if (n++ < 8)
        fprintf(DIAG, "glr_draw ENTER desc=%08x count=%d av.en=%d av.ptr=%08x\n",
                desc_ptr, (int)d->count, d->av.en, d->av.ptr); }
    if ((int)d->count <= 0 || !d->av.en) return;
    /* copy the texture into contiguous scratch (handles a texture that spans host regions, and
       can't fault on a wild address since read_guest validates every region it crosses). */
    const uint32_t *tx = NULL;
    if (d->en_tex && d->tex_rgba && d->tex_w > 0 && d->tex_h > 0 &&
        (long)d->tex_w * d->tex_h <= 4096 * 4096) {
        size_t need = (size_t)d->tex_w * d->tex_h;
        if (need > g_texbuf_cap) { free(g_texbuf); g_texbuf = malloc(need * 4); g_texbuf_cap = g_texbuf ? need : 0; }
        if (g_texbuf && read_guest(g_texbuf, d->tex_rgba, (uint32_t)(need * 4u)) == 0) tx = g_texbuf;
    }
    int count = (int)d->count, first = (int)d->first;
    if (count > 100000) return;
    Vtx *v = malloc((size_t)count * sizeof(Vtx)); if (!v) return;
    for (int i = 0; i < count; i++) fetch(d, first + i, &v[i]);
    /* Per-draw trace: arm once a glyph run (count>6) is seen, then log EVERY draw in order so we
       can see whether the opaque dialogue box overdraws the text, and where the text lands. */
    if (glr_log()) {
        static int armed = 0, logged = 0;
        if (count > 6) armed = 1;
        if (armed && logged < 160) {
            logged++;
            float minx=v[0].x,maxx=v[0].x,miny=v[0].y,maxy=v[0].y;
            for (int i=1;i<count;i++){ if(v[i].x<minx)minx=v[i].x; if(v[i].x>maxx)maxx=v[i].x;
                                       if(v[i].y<miny)miny=v[i].y; if(v[i].y>maxy)maxy=v[i].y; }
            uint32_t gtex=0; if(tx){int tot=d->tex_w*d->tex_h; for(int i=0;i<tot;i++) if(tx[i]&0xFF000000){gtex=tx[i];break;}}
            fprintf(DIAG, "D mode=%u cnt=%u tex=%08x %dx%d bbox=[%.0f,%.0f %.0f,%.0f] entex=%d texenv=%x "
                    "blend=%d(%x,%x) atest=%d(%x,%.2f) col=(%.2f,%.2f,%.2f,%.2f) glyph=%08x\n",
                    d->mode, count, d->tex_rgba, d->tex_w, d->tex_h, minx, miny, maxx, maxy,
                    d->en_tex, d->texenv, d->en_blend, d->blend_s, d->blend_d,
                    d->en_atest, d->atest_func, d->atest_ref, v[0].r, v[0].g, v[0].b, v[0].a, gtex);
        }
    }
    long frags0 = g_glr_frags;
    int handled_quad = fast_quad(d, tx, v, count);
    if (!handled_quad) {
        if (d->mode == GL_TRIANGLES)        for (int i=0;i+2<count;i+=3) raster_tri(d,tx,&v[i],&v[i+1],&v[i+2]);
        else if (d->mode == GL_TRIANGLE_STRIP) for (int i=0;i+2<count;i++)  raster_tri(d,tx,&v[i],&v[i+1],&v[i+2]);
        else if (d->mode == GL_TRIANGLE_FAN)   for (int i=1;i+1<count;i++)  raster_tri(d,tx,&v[0],&v[i],&v[i+1]);
    }
    if (glr_log() && count > 6) { static int n=0; if (n++ < 30)
        fprintf(DIAG, "   ^text frags_written=%ld (quadpath=%d)\n", g_glr_frags - frags0, handled_quad); }
    free(v);
}
