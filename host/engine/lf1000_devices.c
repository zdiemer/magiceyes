/* magiceyes — LF1000 (LeapFrog Didj) display driver device nodes.
 *
 * The Didj DisplayMPI (libDisplay.so) drives the screen through the LF1000 kernel display
 * drivers, NOT raw register pokes: it opens /dev/dpc + /dev/mlc + /dev/layer0..2 (+ /dev/gpio,
 * /dev/ga3d for OpenGL), queries the screen geometry by ioctl, mmap()s a layer framebuffer, and
 * flips by ioctl'ing the layer scanout address / marking it dirty. We emulate just that contract:
 * back the mmap'd layer-0 RGB framebuffer with guest memory and present it to the shm viewer on
 * each flip, reusing the engine's 32bpp present path (present_guest, g_mlc_bpp/pitch).
 *
 * MLC ioctls (include/linux/lf1000/mlc_ioctl.h): magic 'm', _IO(m,nr). The ones DisplayMPI uses:
 *   7  GSCREENSIZE (_IOR ptr {w,h})   8 LAYEREN     9  ADDRESS (flip)   10 HSTRIDE (bytes/px)
 *   11 VSTRIDE (pitch)   15 FORMAT    16 QFORMAT    17 DIRTY (present)   25 QADDRESS   29 QFBSIZE
 */
#include "engine.h"

#define LF_SCREEN_W 320
#define LF_SCREEN_H 240
#define LF_BYTESPP  4                       /* LF1000 RGB layer 0 = 32bpp (ARGB8888) */
/* Each layer has a fixed framebuffer physical base on real hardware (fbaddr[] in the driver).
   MLC_IOCQADDRESS must report a non-zero one (DisplayModule::InitOpenGL fails otherwise), and
   DisplayModule then mmap()s that phys. Synthetic bases, above the GP2X PRAM window (0x02-0x04M)
   and the 0x04M fbdev region so they don't alias. */
#define LF_FB_PHYS(layer)  (0x05000000u + (uint32_t)(layer) * 0x00400000u)

/* per-fd node state (small table keyed by guest fd). */
struct lf_fd { int fd, type, layer; uint32_t fb_guest, fb_len; };
static struct lf_fd g_lf[16];
static struct lf_fd *lf_get(int fd) {
    for (int i = 0; i < 16; i++) if (g_lf[i].type && g_lf[i].fd == fd) return &g_lf[i];
    return NULL;
}

int lf1000_classify(const char *path) {
    if (!strcmp(path, "/dev/dpc"))  return DEV_LF1000_DPC;
    if (!strcmp(path, "/dev/mlc"))  return DEV_LF1000_MLC;
    if (!strncmp(path, "/dev/layer", 10) && path[10] >= '0' && path[10] <= '2') return DEV_LF1000_LAYER;
    if (!strcmp(path, "/dev/ga3d")) return DEV_LF1000_GA3D;
    return 0;
}

void lf1000_open(int fd, int type, const char *path) {
    for (int i = 0; i < 16; i++) if (!g_lf[i].type) {
        g_lf[i].fd = fd; g_lf[i].type = type;
        g_lf[i].layer = (type == DEV_LF1000_LAYER) ? (path[10] - '0') : -1;
        g_lf[i].fb_guest = 0; g_lf[i].fb_len = 0;
        return;
    }
}
void lf1000_close(int fd) { struct lf_fd *f = lf_get(fd); if (f) f->type = 0; }

/* dev_mmap calls this after allocating a guest region for a layer fd's framebuffer. */
void lf1000_track_mmap(int fd, uint32_t guest, uint32_t len) {
    struct lf_fd *f = lf_get(fd); if (!f) return;
    f->fb_guest = guest; f->fb_len = len;
    /* Make the layer's synthetic phys resolve to this guest buffer (so a flip that names the phys
       finds it, and a second mapper aliases it). */
    if (f->layer >= 0) record_memmap(LF_FB_PHYS(f->layer), guest, len);
    /* Layer 0 is the primary RGB UI surface. Point the engine's present source at it. */
    if (f->layer == 0) { g_fb_guest = guest; g_mlc_bpp = LF_BYTESPP; g_mlc_pitch = LF_SCREEN_W * LF_BYTESPP; }
}

/* Flip: show layer 0's framebuffer. Mirrors the Caanoo MLC scanout path (devices.c) -- set the
   flip target + frame-ready and let the helper thread present, frame-synced. */
static void lf1000_present_layer0(void) {
    if (!g_fb_guest) return;
    g_mlc_bpp = LF_BYTESPP; g_mlc_pitch = LF_SCREEN_W * LF_BYTESPP;
    g_flip_active = 1; g_flip_guest = g_fb_guest; g_frame_ready = 1;
}

long lf1000_ioctl(int fd, uint32_t cmd, uint32_t arg) {
    struct lf_fd *f = lf_get(fd);
    int layer = f ? f->layer : -1;
    unsigned type = (cmd >> 8) & 0xff, nr = cmd & 0xff;
    if (getenv("ME_LF1000LOG"))
        fprintf(stderr, "  [lf1000] ioctl fd=%x type=%c nr=%u arg=%08x (layer=%d)\n",
                fd, type ? (char)type : '?', nr, arg, layer);
    if (type != 'm') return 0;   /* MLC magic; DPC/other ioctls -> benign success */
    switch (nr) {
    case 7: {  /* MLC_IOCGSCREENSIZE: write {width,height} to the user struct */
        uint32_t wh[2] = { LF_SCREEN_W, LF_SCREEN_H };
        if (arg) uc_mem_write(g_uc, arg, wh, 8);
        return 0;
    }
    case 29: return LF_SCREEN_W * LF_SCREEN_H * LF_BYTESPP;   /* MLC_IOCQFBSIZE */
    case 16: return 0;                                        /* MLC_IOCQFORMAT */
    case 25: return (layer >= 0) ? (long)LF_FB_PHYS(layer) : 0;  /* MLC_IOCQADDRESS: the layer fb phys */
    case 9:                                                   /* MLC_IOCTADDRESS: flip */
        if (layer == 0) lf1000_present_layer0();
        return 0;
    case 17:                                                  /* MLC_IOCTDIRTY: present on redraw */
        if (layer == 0) lf1000_present_layer0();
        return 0;
    case 8: case 10: case 11: case 15:   /* LAYEREN / HSTRIDE / VSTRIDE / FORMAT: accept (we fix 320x240x4) */
    default:
        return 0;
    }
}

void lf1000_reset(void) { memset(g_lf, 0, sizeof g_lf); }
