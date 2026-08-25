/* magiceyes Unicorn engine — GP2X/Wiz device model: /dev/{fb,mem,gpio,dsp,mixer},
 * MMSP2 registers, the shm framebuffer/audio bridge to the viewer, and present. */
#include "engine.h"

int g_devtype[64], g_devn = 0;
int g_fbnum[64];   /* per-slot framebuffer index (0=/dev/fb0, 1=/dev/fb1) for DEV_FB fds */
int dev_open(const char *path) {
    int t, fbno = 0;
    if (!strncmp(path, "/dev/fb", 7))       { t = DEV_FB; fbno = (path[7] == '1') ? 1 : 0; }
    else if (!strcmp(path, "/dev/mem"))       t = DEV_MEM;
    else if (!strcmp(path, "/dev/gpio") ||
             !strcmp(path, "/dev/GPIO"))      t = DEV_GPIO;   /* paeryn SDL opens "/dev/GPIO" */
    else if (!strncmp(path, "/dev/dsp", 8))   t = DEV_DSP;
    else if (!strcmp(path, "/dev/sound/dsp"))   t = DEV_DSP;   /* Caanoo OSS alias */
    else if (!strncmp(path, "/dev/mixer", 10))t = DEV_MIXER;
    else if (!strcmp(path, "/dev/sound/mixer")) t = DEV_MIXER; /* Caanoo OSS alias (Liar) */
    else if (!strncmp(path, "/dev/tty", 8))   t = DEV_TTY;
    else if (!strcmp(path, "/dev/i2c-0"))     t = DEV_I2C;   /* handset serial (DRM/region check) */
    /* Caanoo (Pollux) device nodes the GLES titles open: the ISA1200 haptics motor and the
       pollux clock-gating control. We have no hardware to drive; a benign stub (open ok,
       ioctl->0, read->0, write discarded) lets the game proceed past the open. */
    else if (!strcmp(path, "/dev/isa1200") ||
             !strcmp(path, "/dev/pollux_clock")) t = DEV_OTHER;
    /* /dev/mmuhack: the GP2X "mmuhack" kernel module that enables write-back caching. Games
       open it purely to install the hack and treat a failed open as fatal (GP2X_Nat2007 prints
       "MMU hack failed" and exits). We have nothing to accelerate, but a benign stub (open ok,
       ioctl->0, writes discarded) lets the game proceed. */
    else if (!strcmp(path, "/dev/mmuhack"))   t = DEV_OTHER;
    /* OSS MIDI sequencer: 22 corpus titles open it for background music and treat a failed
       open as "no music" at best, a fatal at worst. Accept-and-discard (open ok, ioctl->0,
       writes swallowed) gets them past init; real MIDI synthesis is a separate project. */
    else if (!strcmp(path, "/dev/sequencer") ||
             !strcmp(path, "/dev/sequencer2") ||
             !strcmp(path, "/dev/music"))     t = DEV_OTHER;
    else if (!strcmp(path, "/dev/null"))      t = DEV_NULL;  /* freopen sink; must not ENOENT */
    /* GP2X F200 resistive touchscreen (WM9712 codec). Titles read TS_EVENT samples; the viewer's
       mouse->touch plumbing (gp2xshm touch_x/y/down) is the data source. */
    else if (!strcmp(path, "/dev/touchscreen/wm97xx")) t = DEV_TOUCH;
    else if (!strcmp(path, "/dev/shm/gp2x_fb")) t = DEV_SHMFB; /* fake-SDL shim's framebuffer shm */
    else if ((t = input_classify(path))) { /* /dev/input/event*, /dev/input/js*: Linux input subsystem */ }
    else {
        /* A /dev node we don't model: record it (so the harness/agent learns which device a new
           title wants) but don't change behaviour -- fall through to the normal open() path. */
        if (!strncmp(path, "/dev/", 5)) me_report(MR_UNKNOWN_DEV, 0, path, 0);
        return -1;
    }
    int i; for (i = 0; i < 64; i++) if (g_devtype[i] == 0) break;  /* reuse freed slots */
    if (i == 64) return -1;
    g_devtype[i] = t; g_fbnum[i] = fbno; if (i + 1 > g_devn) g_devn = i + 1;
    if (t == DEV_INPUT_EV || t == DEV_INPUT_JS) input_open(DEVFD_BASE + i, t);
    if (g_trace) fprintf(stderr, "  DEV open %s -> fd=%d type=%d\n", path, DEVFD_BASE + i, t);
    return DEVFD_BASE + i;
}
void dev_close(int fd) { int i = fd - DEVFD_BASE; if (i >= 0 && i < 64) g_devtype[i] = 0; }
int dev_type(int fd) {
    int i = fd - DEVFD_BASE;
    return (i >= 0 && i < g_devn) ? g_devtype[i] : 0;
}

/* ---- shm framebuffer bridge to the native viewer (shared w/ the Wiz shim) ---- */
gp2x_shm_t *g_shm = NULL;
void shm_setup(void) {
#ifdef ME_BUNDLED
    /* Single-process bundle: the viewer runs in this same process (a worker thread, see
       main.c), so g_shm is plain in-process memory -- no cross-process named mapping to
       mismatch on launch order/namespace/DACL. This is the intended single-process end state
       (the cross-platform plan); the native-Windows render fixes are in host/win/.
       mmap (not calloc) so the buffer is page-aligned + a whole number of pages -- a dynamic
       SDL game's fake-SDL shim mmaps this exact object and the engine aliases its guest pages
       straight onto it (uc_mem_map_ptr needs both ends page-aligned). */
    size_t sz = ALIGN_UP(sizeof(gp2x_shm_t));
    g_shm = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_shm == MAP_FAILED) { g_shm = NULL; return; }
    g_shm->buttons = 0; g_shm->quit = 0; g_shm->frame_seq = 0;
    g_shm->magic = GP2XSHM_MAGIC;
    return;
#else
    /* ME_SHM_NAME lets several headless engines run in parallel without colliding on one shm
       object (the corpus harness reads /dev/shm/<name>). The in-guest shim still opens the
       virtual "/dev/shm/gp2x_fb", which the engine intercepts (DEV_SHMFB) and aliases onto this
       g_shm regardless of the real object's name -- so no shim change is needed. */
    const char *shmname = getenv("ME_SHM_NAME"); if (!shmname || !*shmname) shmname = GP2XSHM_NAME;
    int fd = shm_open(shmname, O_CREAT | O_RDWR, 0666);
    if (fd < 0) return;
    if (ftruncate(fd, sizeof(gp2x_shm_t)) != 0) { /* may pre-exist */ }
    void *p = mmap(NULL, sizeof(gp2x_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return;
    g_shm = p; g_shm->buttons = 0; g_shm->quit = 0; g_shm->frame_seq = 0;
    g_shm->magic = GP2XSHM_MAGIC;
#endif
}

/* The fake-SDL shim does shm_open("/gp2x_fb") -> open("/dev/shm/gp2x_fb"), ftruncate, then
   mmap(sizeof(gp2x_shm_t)). Alias those guest pages directly onto the engine's g_shm so the
   shim renders into the very buffer the viewer shows (the in-process version of the qemu
   /dev/shm bridge). Fixed guest base above the interpreter (0x71000000) and below the stack. */
#define SHMFB_BASE 0x72000000u
long shmfb_mmap(uint32_t len) {
    if (!g_shm) return -12 /*ENOMEM*/;
    uint32_t l = ALIGN_UP(len ? len : sizeof(gp2x_shm_t));
    if (l > ALIGN_UP(sizeof(gp2x_shm_t))) l = ALIGN_UP(sizeof(gp2x_shm_t));
    mem_register_external(SHMFB_BASE, l, g_shm);
    if (g_trace) fprintf(stderr, "  [shmfb] mmap len=%u -> guest=%08x (g_shm)\n", len, SHMFB_BASE);
    return SHMFB_BASE;
}

/* /dev/mem mmap tracking so MMSP2 framebuffer phys addresses resolve to guest. */
struct memmap g_mem[64]; int g_nmem = 0;
uint32_t g_mmsp2_guest = 0;   /* guest addr of the 0xC0000000 reg block */
uint32_t g_fb_guest = 0;      /* guest addr of the /dev/fb0 framebuffer */
uint32_t g_fb_guest2 = 0;     /* /dev/fb1 (double-buffering: present the active one) */
int g_fb_from_devmem = 0;     /* g_fb_guest came from a /dev/mem map of the default scanout */
uint32_t g_fb_stride = 640;   /* present row stride in bytes (320*2; gpu940 sets its pow2 width) */
int      g_fb_bpp = 16;       /* present source depth: 16 = RGB565, 32 = XRGB8888 (gpu940 output) */
uint32_t g_fb_xoff = 0;       /* x pixel offset into each row (gpu940 centers 320 in a pow2 buffer) */
/* Caanoo (Pollux MLC): the firmware menu + some titles drive the display directly via the MLC
   registers in the 0xC0000000 block, choosing a pixel depth our default RGB565 present doesn't
   match (the menu uses 24bpp RGB888, pitch 960 -> presented as 16bpp it shears + mis-colours).
   Capture HSTRIDE (bytes/pixel) + VSTRIDE (pitch) from the MLC layer so present can convert. */
int g_caanoo_bpp = 0;         /* MLCHSTRIDE (bytes/pixel): 0 = unset (use RGB565), 2/3/4 */
uint32_t g_caanoo_pitch = 0;  /* MLCVSTRIDE (bytes/row) */
int g_fb8_mode = 0;           /* title showed 8bpp evidence (MLC HSTRIDE=1 or PUT_VSCREENINFO bpp=8):
                                 gates FBIOPUTCMAP flipping present to the 8-bit indexed path */
/* fbdev virtual mode from PUT_VSCREENINFO. The shim/firmware SDL honours whatever geometry the
   title asks for (EEEEK/Metal Slug Zombies run 640x480, Skull 320x200, openjazz-wiz 640x480 via
   the Wiz hardware scaler) but present used to assume 320x240 -- shear/duplication. Track the
   accepted mode; GET_VSCREENINFO/GET_FSCREENINFO reflect it and present_guest reads + publishes
   frames at this size (the shm contract carries width/height up to 1024x768; the viewer scales). */
int g_fbv_w = 320, g_fbv_h = 240;
/* MLC STL scaler registers as last written (HSC/VSCL/VSCH/HW): the hardware-scaled-surface
   source geometry is derived from these in mmsp2_write_cb. Reset with the fb mode. */
static uint32_t g_mlc_hsc = 1024, g_mlc_vscl = 0, g_mlc_vsch = 0, g_mlc_hw = 0;
/* Pollux PORTRAIT scanout (the Wiz LCD is physically 240x320 rotated): the open2x-wiz SDL
   programs MLCSCREENSIZE as 240x320 and rotate-blits the app's landscape frame into the portrait
   buffer. Detected from the SCREENSIZE write (h > w); present un-rotates. */
int g_mlc_rot = 0;            /* portrait SCREENSIZE seen (rotation candidate) */
uint32_t g_mlc_rot_w = 240, g_mlc_rot_h = 320;   /* portrait buffer geometry */
uint32_t g_mlc_rot_pitch = 0;                    /* bytes/row of the portrait buffer (captured) */
uint32_t g_mlc_rot_bypp = 2;                     /* bytes/pixel (MLCHSTRIDE) */
void record_memmap(uint32_t phys, uint32_t guest, uint32_t len) {
    if (g_nmem < 64) { g_mem[g_nmem] = (struct memmap){phys, guest, len}; g_nmem++; }
}
int phys_to_guest(uint32_t phys, uint32_t *out) {
    for (int i = 0; i < g_nmem; i++)
        if (phys >= g_mem[i].phys && phys < g_mem[i].phys + g_mem[i].len)
            { *out = g_mem[i].guest + (phys - g_mem[i].phys); return 1; }
    return 0;
}

/* MLC 8-bit palette (defined in the palette section below; used by present_guest). */
extern uint8_t g_pal[256][3];
extern int g_pal_have;
extern int g_stl_bpp;

/* GP2X native screen = 320x240. Present the framebuffer at guest addr `g` to shm.
   Depth is inferred from g_pal_have: an 8-bit MLC framebuffer (Odonata, paeryn-SDL
   titles like Knight Lore) uploads a palette via the write-only PALLT port — which only
   8-bit modes ever touch — so a captured palette means the live surface is 8-bit indexed
   (320 B/row, LUT'd to RGB565). No palette => native RGB565 (640 B/row). See gp2x_mmio_palette. */
void present_guest(uint32_t g) {
    if (!g_shm || !g) return;
    int nz = 0; long nzc = 0;   /* nzc: # of non-zero indices this frame (accumulation diag) */
    if (g_device == 2 && g_caanoo_bpp >= 3) {  /* Caanoo 24/32bpp (firmware menu) -> RGB565.
            Pixels are B,G,R[,X] (SDL Rmask=0xff0000). The firmware menu draws 24bpp pixels into the
            640-byte-pitch fbdev (only ~213 px/row) and the real Pollux MLC upscales ~1.5x to the
            320px panel -- so derive the source width from pitch and nearest-neighbour-scale it to 320
            (srcw==320 for titles that use a full-width 24/32bpp surface => identity). */
        int bpp = g_caanoo_bpp;
        uint32_t pitch = g_caanoo_pitch ? g_caanoo_pitch : (uint32_t)320 * bpp;
        int srcw = (int)(pitch / (uint32_t)bpp); if (srcw < 1) srcw = 320;
        uint16_t *dst = (uint16_t *)g_shm->pixels;
        for (int y = 0; y < 240; y++) {
            uint8_t *src = guest_to_host(g + (uint32_t)y * pitch); if (!src) break;
            uint16_t *dp = dst + (size_t)y * GP2XSHM_MAXW;
            for (int x = 0; x < 320; x++) {
                int sx = (x * srcw) / 320;
                uint8_t b = src[sx*bpp+0], gg = src[sx*bpp+1], r = src[sx*bpp+2];
                dp[x] = (uint16_t)(((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3));
                if (b | gg | r) { nz = 1; nzc++; }
            }
        }
        g_shm->width = 320; g_shm->height = 240; g_shm->frame_seq++;
        return;
    }
    if (g_mlc_rot && g_mlc_rot_pitch &&
        g_mlc_rot_pitch == g_mlc_rot_w * g_mlc_rot_bypp) {  /* Pollux portrait scanout -> un-rotate.
            Portrait buffer: g_mlc_rot_w x g_mlc_rot_h (240x320), pitch g_mlc_rot_pitch. Landscape
            frame: w = portrait h, h = portrait w. Each portrait row becomes one landscape column
            (x = prt_h-1-yp, y = xp). 16bpp direct; 8bpp through the captured palette. */
        uint32_t pw = g_mlc_rot_w, ph = g_mlc_rot_h, pitch = g_mlc_rot_pitch;
        if (ph > GP2XSHM_MAXW) ph = GP2XSHM_MAXW;
        if (pw > GP2XSHM_MAXH) pw = GP2XSHM_MAXH;
        uint16_t *dst = (uint16_t *)g_shm->pixels;
        uint16_t lut[256];
        if (g_mlc_rot_bypp == 1) {
            for (int i = 0; i < 256; i++)
                lut[i] = g_pal_have ? (uint16_t)(((g_pal[i][0] >> 3) << 11) |
                                                 ((g_pal[i][1] >> 2) << 5) | (g_pal[i][2] >> 3))
                                    : (uint16_t)((((i >> 5) & 7) << 13) | (((i >> 2) & 7) << 8) |
                                                 ((i & 3) << 3));
        }
        for (uint32_t yp = 0; yp < ph; yp++) {
            uint8_t *src = guest_to_host(g + yp * pitch); if (!src) break;
            uint32_t x = ph - 1 - yp;                    /* landscape column */
            if (g_mlc_rot_bypp == 1) {
                for (uint32_t xp = 0; xp < pw; xp++) {
                    dst[(size_t)xp * GP2XSHM_MAXW + x] = lut[src[xp]];
                    if (src[xp]) { nz = 1; nzc++; }
                }
            } else {
                const uint16_t *s16 = (const uint16_t *)src;
                for (uint32_t xp = 0; xp < pw; xp++) {
                    dst[(size_t)xp * GP2XSHM_MAXW + x] = s16[xp];
                    if (s16[xp]) { nz = 1; nzc++; }
                }
            }
        }
        g_shm->width = (int)ph; g_shm->height = (int)pw; g_shm->frame_seq++;
        goto presented;
    }
    if (g_pal_have && g_stl_bpp <= 8) {     /* 8-bit indexed -> RGB565 via the captured palette
                                               (unless STL_CNTL explicitly declared 16/24bpp) */
        uint16_t lut[256];
        for (int i = 0; i < 256; i++)
            lut[i] = (uint16_t)(((g_pal[i][0] >> 3) << 11) |
                                ((g_pal[i][1] >> 2) << 5)  | (g_pal[i][2] >> 3));
        uint16_t *dst = (uint16_t *)g_shm->pixels;
        for (int y = 0; y < g_fbv_h; y++) {
            uint8_t *src = guest_to_host(g + (uint32_t)y * (uint32_t)g_fbv_w); if (!src) break;
            uint16_t *dp = dst + (size_t)y * GP2XSHM_MAXW;
            for (int x = 0; x < g_fbv_w; x++) { dp[x] = lut[src[x]]; if (src[x]) { nz = 1; nzc++; } }
        }
        g_shm->width = g_fbv_w; g_shm->height = g_fbv_h; g_shm->frame_seq++;
        goto presented;
    } else if (g_fb_bpp == 32) {            /* gpu940 output: XRGB8888, g_fb_stride bytes/row */
        for (int y = 0; y < 240; y++) {
            uint32_t *src = (uint32_t *)guest_to_host(g + (uint32_t)y * g_fb_stride);
            if (!src) break;
            uint16_t *dp = (uint16_t *)(g_shm->pixels + (size_t)y * GP2XSHM_MAXW * 2);
            src += g_fb_xoff;                 /* gpu940 centers the 320px screen in a wider buffer */
            for (int x = 0; x < 320; x++) {
                uint32_t p = src[x];
                dp[x] = (uint16_t)(((p >> 8) & 0xf800) | ((p >> 5) & 0x07e0) | ((p >> 3) & 0x001f));
                if (p & 0xffffff) nz = 1;
            }
        }
    } else {                                /* native RGB565 (g_fb_stride bytes/row; 640 normally,
                                               but gpu940 video buffers are power-of-2 wide and a
                                               PUT_VSCREENINFO mode sets its own width) */
        uint32_t rowb = (uint32_t)g_fbv_w * 2;
        /* MLC-scaled mode (stride > native, HSC > 1:1): the surface holds wider-than-panel
           lines and the hardware downsamples horizontally by HSC/1024 source pixels per
           output pixel, exactly paeryn's scale_x = 1024*width/phys_width. Verified on
           Volleyball (640-wide Game&Watch court: both scores + GAME A/B side by side) and
           para3 (640-wide intro text): without the subsample each showed a plausible-looking
           but WRONG left-half crop. */
        int hsub = (g_fb_stride > rowb && g_mlc_hsc > 1024);
        uint8_t row[GP2XSHM_MAXW * 2];
        for (int y = 0; y < g_fbv_h; y++) {
            uint8_t *src = guest_to_host(g + (uint32_t)y * g_fb_stride); if (!src) break;
            if (hsub) {
                const uint16_t *s16 = (const uint16_t *)src;
                uint16_t *r16 = (uint16_t *)row;
                uint32_t last = g_fb_stride / 2 - 1;
                for (int x = 0; x < g_fbv_w; x++) {
                    uint32_t sx = ((uint32_t)x * g_mlc_hsc) >> 10;
                    r16[x] = s16[sx > last ? last : sx];
                }
            } else {
                memcpy(row, src, rowb);
            }
            memcpy(g_shm->pixels + (size_t)y * GP2XSHM_MAXW * 2, row, rowb);
            if (!nz) for (uint32_t i = 0; i < rowb; i++) if (row[i]) { nz = 1; break; }
        }
        g_shm->width = g_fbv_w; g_shm->height = g_fbv_h; g_shm->frame_seq++;
        goto presented;
    }
    g_shm->width = 320; g_shm->height = 240; g_shm->frame_seq++;
presented:;
    if (getenv("ME_GP2X_PRESENTLOG")) {   /* diagnose black-screen: black frames vs viewer issue */
        static int n = 0, nb = 0; n++; if (!nz) nb++;
        if (n % 60 == 0) fprintf(stderr, "PRESENT %d frames (%d black) guest=%08x seq=%u nz_px=%ld\n",
                                 n, nb, g, g_shm->frame_seq, nzc);
    }
}
/* Once the game page-flips to a real fb address (double-buffering), lock the display to that
   front buffer: the change-heuristic in present_active() otherwise shows the half-drawn back
   buffer between flips -> sprites/text flicker. Updated on each OADR flip. */
int g_flip_active = 0; uint32_t g_flip_guest = 0;
/* Set by the helper thread while the flip-synced present has been starved >250ms (no flip
   signal). Lets present_active() reconsider WHICH page the flip-lock pins: a title can pan
   once at init (locking us to fb page 0) and then draw every frame into page 1 with no
   further pan/flip -- the Wiz GLBasic family (Wiz_Blox, freecell2x, xcom, ~20 titles) does
   exactly this through the firmware SDL's surface pixels, and the old fallback re-presented
   the pinned page forever: full-speed game, audio playing, permanently black screen. */
int g_present_stale = 0;
/* The game signals each frame boundary by writing the MLC OADR register (Payback writes 0
   every frame). Once it does, present in lockstep with that write (one complete frame, synced)
   and stop the async 16ms helper present -- that async copy was catching mid-draw frames
   (tearing) at a rate decoupled from the game's (stutter). */
int g_oadr_driven = 0;
int g_frame_ready = 0;   /* OADR write sets this; the helper thread does the actual present
                            (keeping the heavy copy OFF the guest render thread) */
void present_fb(uint32_t phys) {
    uint32_t g;
    if (phys_to_guest(phys, &g)) {
        if (!getenv("ME_GP2X_NOFLIPLOCK")) { g_flip_active = 1; g_flip_guest = g; }
        present_guest(g);
    }
}
/* the game double-buffers across fb0/fb1; present whichever currently has content. */
int buf_score(uint32_t g) {
    if (!g) return -1;
    uint8_t row[320 * 2]; int nz = 0;
    for (int y = 8; y < 240; y += 24) {
        { uint8_t *src = guest_to_host(g + (uint32_t)y * 640); if (!src) break;
          memcpy(row, src, sizeof row); }
        for (unsigned i = 0; i < sizeof row; i++) if (row[i]) { nz++; break; }
    }
    return nz;
}
uint32_t buf_hash(uint32_t g) {
    if (!g) return 0;
    uint8_t row[320 * 2]; uint32_t h = 2166136261u;
    for (int y = 0; y < 240; y += 12) {
        { uint8_t *src = guest_to_host(g + (uint32_t)y * 640); if (!src) break;
          memcpy(row, src, sizeof row); }
        for (unsigned i = 0; i < sizeof row; i += 5) h = (h ^ row[i]) * 16777619u;
    }
    return h;
}
/* present whichever fb the game just rendered to (its content changed since last
   frame); fall back to the non-blank one for a fully static screen. */
void present_active(void) {
    if (gl_owns_screen()) return;   /* a GLES title (offload) owns shm; don't let the hybrid title's
                                       real-libSDL 2D framebuffer present alternate with it -> flicker */
    if (me940_active()) {   /* gpu940 client: present follows the 940's rendered framebuffer */
        static int n = 0; if (++n % 12 == 0) me940_scan_fb();
    }
    static double last = 0;            /* cap to ~60fps: the game's tiny nanosleeps call
                                          this ~2000/s, and hashing+copying every time
                                          (300MB/s) was choking the emulator. */
    double now = host_now();
    if (now - last < 0.008) return;  /* dedupe the OADRL+OADRH pair; allow up to ~100fps */
    last = now;
    if (g_flip_active) {              /* double-buffered: show the flipped-to front buffer only */
        /* Staleness rescue: with no flip signal for >250ms (g_present_stale), check whether the
           pinned page is frozen while the OTHER fb page is the live one, and auto-pan to it.
           Switch only on evidence -- pinned page static across consecutive samples AND (the
           alternate page animating, or pinned blank while the alternate has content) -- so a
           genuinely double-buffered title mid-flip is never preempted (it flips within 250ms
           and never runs stale). Sticky: g_flip_guest moves, so a later real pan overrides. */
        if (g_present_stale && g_fb_guest && g_flip_guest) {
            uint32_t page = (uint32_t)g_fbv_h * g_fb_stride;
            uint32_t alt = (g_flip_guest == g_fb_guest) ? g_fb_guest + page : g_fb_guest;
            static uint32_t hf = 0, ha = 0; static int front_frozen = 0;
            uint32_t nf = buf_hash(g_flip_guest), na = buf_hash(alt);
            int alt_changed = (na != ha);
            front_frozen = (nf == hf) ? front_frozen + 1 : 0;
            hf = nf; ha = na;
            if (front_frozen >= 3 &&
                (alt_changed || (buf_score(g_flip_guest) == 0 && buf_score(alt) > 0))) {
                fprintf(DIAG, "present: flip-locked page %08x stale, auto-pan to %08x\n",
                        g_flip_guest, alt);
                g_flip_guest = alt;
                front_frozen = 0; hf = ha = 0;
            }
        }
        if (g_flip_guest) present_guest(g_flip_guest);
        return;
    }
    static uint32_t h0 = 0, h1 = 0;
    uint32_t n0 = buf_hash(g_fb_guest), n1 = buf_hash(g_fb_guest2);
    int c0 = (n0 != h0), c1 = (n1 != h1);
    h0 = n0; h1 = n1;
    if (c1 && g_fb_guest2) present_guest(g_fb_guest2);
    else if (c0) present_guest(g_fb_guest);
    else present_guest(buf_score(g_fb_guest2) > buf_score(g_fb_guest) ? g_fb_guest2 : g_fb_guest);
}

/* Payback (a double-buffered title) renders to fb0/fb1 alternately and "flips" by issuing
   __ARM_NR_cacheflush on the buffer it just finished, with that buffer's base in r3 -- it leaves
   the MLC OADR register at 0 the whole time, so the OADR write-hook can't tell which buffer is
   live. The cacheflushed base IS the just-rendered front buffer, so it's the correct present
   signal: lock the display to it and let the helper thread do the copy (frame-synced). */
void gp2x_cacheflush(uint32_t guest) {
    if (guest != g_fb_guest && guest != g_fb_guest2) return;   /* only an fb flush is a flip */
    g_oadr_driven = 1;                /* stop the async fallback present; we drive it per frame */
    g_flip_active = 1; g_flip_guest = guest;
    g_frame_ready = 1;
}

/* ---- /dev/fb0,fb1 (Linux fbdev) -------------------------------------------
   Framebuffer-direct GP2X games (Blazar, Quartz2, minlib titles) query the fb
   geometry via FBIOGET_VSCREENINFO/FSCREENINFO before mmap'ing it; if the engine
   returns a zeroed struct they decide the framebuffer is unusable and bail back to
   `gp2xmenu` (execve) without ever drawing. We advertise a 320x240 RGB565 fbdev,
   with a yres_virtual of 480 so a game can double-buffer via FBIOPAN_DISPLAY.
   On GP2X the fb is pram-backed at its REAL phys (GP2X_FB0/1_PHYS, engine.h) and that
   is what smem_start reports -- titles mix fbdev with /dev/mem views of the same RAM.
   Other devices keep a synthetic phys (FB0_PHYS/FB1_PHYS); either way the phys is
   recorded so an MLC OADR flip to it resolves back to the surface. */
#define FB0_PHYS 0x04000000u
#define FB1_PHYS 0x04040000u
#define FB_LEN_  (320 * 240 * 2)
static void fill_vscreeninfo(uint32_t gbuf) {
    uint8_t b[160]; memset(b, 0, sizeof b);
    int c = (g_device == 2 && g_is_dynamic);   /* Caanoo menu: 24bpp BGR888 (static = GP2X-era 16bpp) */
    uint32_t w = (uint32_t)g_fbv_w, h = (uint32_t)g_fbv_h;
    *(uint32_t *)(b + 0)  = w;   *(uint32_t *)(b + 4)  = h;     /* xres / yres */
    *(uint32_t *)(b + 8)  = w;   *(uint32_t *)(b + 12) = h * 2; /* xres_v / yres_v (2 pages) */
    if (g_fb8_mode) {
        /* The title established an 8bpp screen (PUT_VSCREENINFO bpp=8 or MLC HSTRIDE=1). Report it
           back: SDL's fbcon grants SDL_HWPALETTE from this read-back, and without the flag it drops
           SDL_SetColors before FBIOPUTCMAP is ever called (Sopwith stayed in init-ramp colours). */
        *(uint32_t *)(b + 24) = 8;                              /* bits_per_pixel */
        *(uint32_t *)(b + 36) = 8; *(uint32_t *)(b + 48) = 8;   /* r/g/b len 8, offsets 0 (indexed) */
        *(uint32_t *)(b + 60) = 8;
    } else if (c) {  /* BGR888: blue in the low byte, red in the high byte (present_guest B,G,R) */
        *(uint32_t *)(b + 24) = 24;
        *(uint32_t *)(b + 32) = 16; *(uint32_t *)(b + 36) = 8;  /* red   offset/len */
        *(uint32_t *)(b + 44) = 8;  *(uint32_t *)(b + 48) = 8;  /* green offset/len */
        *(uint32_t *)(b + 56) = 0;  *(uint32_t *)(b + 60) = 8;  /* blue  offset/len */
    } else {  /* RGB565 */
        *(uint32_t *)(b + 24) = 16;
        *(uint32_t *)(b + 32) = 11; *(uint32_t *)(b + 36) = 5;
        *(uint32_t *)(b + 44) = 5;  *(uint32_t *)(b + 48) = 6;
        *(uint32_t *)(b + 56) = 0;  *(uint32_t *)(b + 60) = 5;
    }
    uc_mem_write(g_uc, gbuf, b, sizeof b);
}
static void fill_fscreeninfo(uint32_t gbuf, uint32_t smem_start) {
    uint8_t b[80]; memset(b, 0, sizeof b);
    uint32_t bypp = g_fb8_mode ? 1 : (g_device == 2 && g_is_dynamic) ? 3 : 2;  /* Caanoo menu 24bpp;
                                                             GP2X (and any static build) 16bpp */
    memcpy(b + 0, "MagicEyes-MLC", 13);            /* id[16] */
    *(uint32_t *)(b + 16) = smem_start;            /* smem_start (phys base) */
    *(uint32_t *)(b + 20) = (uint32_t)g_fbv_w * (uint32_t)g_fbv_h * bypp * 2; /* smem_len (2 pages) */
    *(uint32_t *)(b + 24) = 0;                      /* FB_TYPE_PACKED_PIXELS */
    *(uint32_t *)(b + 32) = g_fb8_mode ? 3 : 2;     /* FB_VISUAL_PSEUDOCOLOR : TRUECOLOR */
    *(uint32_t *)(b + 44) = (uint32_t)g_fbv_w * bypp; /* line_length */
    uc_mem_write(g_uc, gbuf, b, sizeof b);
}
/* FBIOPAN_DISPLAY: select the visible page by yoffset. Reuse the OADR flip-lock path
   so present() shows exactly the panned-to region (one complete frame, frame-synced). */
static void fb_pan(uint32_t yoffset) {
    if (!g_fb_guest) return;
    g_oadr_driven = 1; g_flip_active = 1;
    g_flip_guest = g_fb_guest + yoffset * g_fb_stride;   /* page 1 = yoffset of one screen */
    g_frame_ready = 1;
}
int dev_fbno(int fd) { int i = fd - DEVFD_BASE; return (i >= 0 && i < 64) ? g_fbnum[i] : 0; }
long fb_ioctl(int fd, uint32_t cmd, uint32_t arg) {
    int fbno = dev_fbno(fd);
    switch (cmd) {
    case 0x4600: if (arg) fill_vscreeninfo(arg); return 0;                 /* GET_VSCREENINFO */
    case 0x4602: if (arg) fill_fscreeninfo(arg, (g_device == 0 || !g_is_dynamic)
                                                    ? (fbno ? GP2X_FB1_PHYS : GP2X_FB0_PHYS)
                                                    : (fbno ? FB1_PHYS : FB0_PHYS));
                 return 0;                          /* GET_FSCREENINFO (static = GP2X-era phys) */
    case 0x4601: {                                                          /* PUT_VSCREENINFO: accept
            but NEVER track: SDL fbcon's FB_CheckMode probes every standard mode (1600x1200 down,
            8bpp) with plain PUTs -- old GPH builds don't even set FB_ACTIVATE_TEST -- so a PUT
            carries no signal about the live mode (trusting it broke Deicide 3 two different ways).
            The live mode is inferred from the MLC writes (HSTRIDE/VSTRIDE/PALETTE) instead. */
        if (arg && getenv("ME_GP2X_FLIPLOG")) {
            uint32_t v[9]; memset(v, 0, sizeof v); uc_mem_read(g_uc, arg, v, sizeof v);
            fprintf(stderr, "PUT_VSCREENINFO xres=%u yres=%u xv=%u yv=%u xoff=%u yoff=%u bpp=%u\n",
                    v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
        }
        return 0; }
    /* FBIOPUT/GETCMAP: the fbdev palette. The Wiz firmware libSDL's fbcon driver delivers
       SDL_SetColors here (its MLC writes at init only load an all-black + RGB332 ramp), so
       dropping the cmap leaves 8bpp titles (Sopwith) in wrong colours. struct fb_cmap:
       u32 start, len; ptr red, green, blue, transp -- 16-bit intensity arrays. Feed the shared
       MLC palette; only mark it live (g_pal_have -> 8-bit present) when the title has shown
       8bpp evidence (MLC HSTRIDE=1 or a PUT_VSCREENINFO bpp=8): fbcon also PUTCMAPs a
       console-restore palette on 16bpp titles, which must not flip the present path. */
    case 0x4605: {                                                          /* FBIOPUTCMAP */
        if (!arg) return 0;
        uint32_t c[6]; memset(c, 0, sizeof c); uc_mem_read(g_uc, arg, c, sizeof c);
        uint32_t start = c[0], len = c[1];
        if (start > 255) return 0;
        if (len > 256 - start) len = 256 - start;
        for (uint32_t i = 0; i < len; i++) {
            uint16_t r = 0, gg = 0, b = 0;
            if (c[2]) uc_mem_read(g_uc, c[2] + i * 2, &r, 2);
            if (c[3]) uc_mem_read(g_uc, c[3] + i * 2, &gg, 2);
            if (c[4]) uc_mem_read(g_uc, c[4] + i * 2, &b, 2);
            g_pal[start + i][0] = (uint8_t)(r >> 8);
            g_pal[start + i][1] = (uint8_t)(gg >> 8);
            g_pal[start + i][2] = (uint8_t)(b >> 8);
        }
        if (len && (g_fb8_mode || g_pal_have)) g_pal_have = 1;
        return 0; }
    case 0x4604: {                                                          /* FBIOGETCMAP */
        if (!arg) return 0;
        uint32_t c[6]; memset(c, 0, sizeof c); uc_mem_read(g_uc, arg, c, sizeof c);
        uint32_t start = c[0], len = c[1];
        if (start > 255) return 0;
        if (len > 256 - start) len = 256 - start;
        for (uint32_t i = 0; i < len; i++) {
            uint16_t r = (uint16_t)(g_pal[start + i][0] << 8);
            uint16_t gg = (uint16_t)(g_pal[start + i][1] << 8);
            uint16_t b = (uint16_t)(g_pal[start + i][2] << 8);
            if (c[2]) uc_mem_write(g_uc, c[2] + i * 2, &r, 2);
            if (c[3]) uc_mem_write(g_uc, c[3] + i * 2, &gg, 2);
            if (c[4]) uc_mem_write(g_uc, c[4] + i * 2, &b, 2);
        }
        return 0; }
    case 0x4606: { uint32_t yoff = 0; if (arg) uc_mem_read(g_uc, arg + 20, &yoff, 4);
                   fb_pan(yoff); return 0; }                                /* PAN_DISPLAY (yoffset@20) */
    case 0x4008445a: {                       /* FBIO_LCD_CHANGE_CONTROL (_IOW('D',90,u32[2])).
            Wiz GLBasic (SimOniZ/DuoWIZ_Pong family) bypasses SDL for present: it mmaps
            /dev/fb0 itself (one 0x25800 view = exactly one 240x320x16 PORTRAIT frame, the
            panel-native geometry) and issues this ioctl to point the LCD controller at it.
            Adopt the existing Pollux portrait un-rotate present for the fb. */
        uint32_t v[2] = {0, 0}; if (arg) uc_mem_read(g_uc, arg, v, sizeof v);
        fprintf(DIAG, "FBIO_LCD_CHANGE_CONTROL cmd=%08x val=%08x\n", v[0], v[1]);
        if (g_device == 1) {
            g_mlc_rot = 1; g_mlc_rot_w = 240; g_mlc_rot_h = 320;
            g_mlc_rot_pitch = 480; g_mlc_rot_bypp = 2;
        }
        return 0; }
    case 0x4611: return 0;                                                  /* FBIOBLANK: accept */
    default:     me_report(MR_UNKNOWN_IOCTL, (long)cmd, "fb", 0); return 0;  /* accept unknown fb ioctl */
    }
}

/* ---- /dev/i2c-0 handset serial (paeryn/Inka serial read) ------------------
   Some titles (Vektar) read the GP2X handset serial over i2c for a region/DRM
   check, printing "got serial number: ...". The serial is read via the I2C_RDWR
   ioctl (a register-select write msg + a read msg), in a retry loop that only exits
   once a serial is returned — so the read MUST hand back bytes (returning success
   with an unchanged buffer made Vektar spin forever). We supply a fixed plausible
   serial; the games tolerate the value. */
static const uint8_t I2C_SERIAL[16] = "MAGICEYES0000001";
long i2c_read(uint32_t gbuf, uint32_t n) {
    uint32_t k = n < sizeof I2C_SERIAL ? n : (uint32_t)sizeof I2C_SERIAL;
    if (gbuf && k) uc_mem_write(g_uc, gbuf, I2C_SERIAL, k);
    return (long)k;
}
/* i2c ioctl. I2C_SLAVE/RETRIES/TIMEOUT etc.: accept. I2C_RDWR (0x0707): walk the
   i2c_rdwr_ioctl_data {u32 msgs; u32 nmsgs;}; for each read msg (i2c_msg{u16 addr;
   u16 flags; u16 len; u8 *buf} == 12 bytes, flags&I2C_M_RD) fill buf with the serial. */
long i2c_ioctl(uint32_t cmd, uint32_t arg) {
    if (cmd != 0x0707 || !arg) return 0;
    uint32_t msgs = 0, nmsgs = 0;
    uc_mem_read(g_uc, arg, &msgs, 4);
    uc_mem_read(g_uc, arg + 4, &nmsgs, 4);
    if (nmsgs > 16) nmsgs = 16;
    for (uint32_t i = 0; i < nmsgs; i++) {
        uint16_t flags = 0, len = 0; uint32_t buf = 0;
        uc_mem_read(g_uc, msgs + i * 12 + 2, &flags, 2);
        uc_mem_read(g_uc, msgs + i * 12 + 4, &len, 2);
        uc_mem_read(g_uc, msgs + i * 12 + 8, &buf, 4);
        if ((flags & 0x0001) && buf && len) {            /* I2C_M_RD */
            uint8_t s[256]; for (int k = 0; k < 256; k++) s[k] = I2C_SERIAL[k % 16];
            uint16_t k = len < 256 ? len : 256;
            uc_mem_write(g_uc, buf, s, k);
        }
    }
    return (long)nmsgs;
}

/* ---- /dev/GPIO joystick ----------------------------------------------------
   GP2X games read a 32-bit ACTIVE-HIGH button word from /dev/GPIO (read returns 4 bytes).
   The bit layout is the standard GP2X button word — UP=0x1, UP_LEFT=0x2, LEFT=0x4,
   DOWN_LEFT=0x8, DOWN=0x10, DOWN_RIGHT=0x20, RIGHT=0x40, UP_RIGHT=0x80, START=0x100, …,
   A=0x1000, B=0x2000, X=0x4000, Y=0x8000, VOL_UP/DOWN, STICK_PUSH — which is exactly our
   `gp2xshm.h`/shm->buttons order (the same order the mmio GPIO path uses), so just hand back
   shm->buttons directly. (Knight Lore reads /dev/GPIO raw, NOT via paeryn's SDL_Joystick; an
   earlier PEPC_VK remap rotated DOWN/LEFT/RIGHT -> the "funky d-pad".) */
/* The Caanoo firmware libSDL polls /dev/GPIO via ioctl() for DEVICE STATUS, not buttons --
   buttons arrive over the evdev joystick (input.c). Each call is ioctl(fd, cmd, &out) and the
   kernel writes a status word to *out (arg). cmd map (from libSDL disasm, GPH SDL extensions):
     0x05 SDL_SYS_JoystickUsbConCheck  0x09 SDL_SDStatus      0x0b SDL_HoldStatus
     0x10 SDL_USBWLANStatus  0x11 SDL_USBSETStatus  0x12 SDL_USBGETStatus  0x13 SDL_USBENABLEStatus
   Returning the button word here is what produced the alternating "USB Connected"/"Insert SD Card"
   overlays. Report a clean handheld: no USB/WLAN, hold off, SD card inserted (so the menu shows the
   SD game list). ME_CAANOO_NOSD forces no card. */
long gpio_ioctl(uint32_t cmd, uint32_t arg) {
    uint32_t v = 0;
    switch (cmd) {
        case 0x09: v = getenv("ME_CAANOO_NOSD") ? 0 : 1; break; /* SD card inserted */
        default:   v = 0; break;                                /* USB/WLAN off, hold off */
    }
    if (getenv("ME_INPUTLOG")) { static int n = 0; if (n++ < 60)
        fprintf(stderr, "[gpioctl] cmd=%02x arg=%08x -> status=%u\n", cmd, arg, v); }
    if (arg) uc_mem_write(g_uc, arg, &v, 4);
    return 0;
}
/* Wiz hardware button word (the Pollux pad layout, ACTIVE-HIGH pressed bits): bit
   positions recovered from the unstripped fxi runtime (wizJoystickRead), shared by the
   MMIO GPIOB/C pad model and the Wiz /dev/GPIO read. */
uint32_t wiz_button_word(uint32_t b) {
    uint32_t w = 0;
    if (b & 0x0006) w |= 1u << 16;                    /* LEFT | UPLEFT */
    if (b & 0x0008) w |= (1u << 16) | (1u << 19);     /* DOWNLEFT */
    if (b & 0x0040) w |= 1u << 17;                    /* RIGHT */
    if (b & 0x0080) w |= (1u << 17) | (1u << 18);     /* UPRIGHT */
    if (b & 0x0020) w |= (1u << 17) | (1u << 19);     /* DOWNRIGHT */
    if (b & 0x0003) w |= 1u << 18;                    /* UP | UPLEFT */
    if (b & 0x0010) w |= 1u << 19;                    /* DOWN */
    if (b & (1u << GP2X_A))       w |= 1u << 20;
    if (b & (1u << GP2X_B))       w |= 1u << 21;
    if (b & (1u << GP2X_X))       w |= 1u << 22;
    if (b & (1u << GP2X_Y))       w |= 1u << 23;
    if (b & (1u << GP2X_SELECT))  w |= 1u << 8;
    if (b & (1u << GP2X_START))   w |= 1u << 9;       /* Wiz MENU */
    if (b & (1u << GP2X_L))       w |= 1u << 7;
    if (b & (1u << GP2X_R))       w |= 1u << 6;
    if (b & (1u << GP2X_VOLUP))   w |= 1u << 10;
    if (b & (1u << GP2X_VOLDOWN)) w |= 1u << 11;
    if (b & (1u << GP2X_CLICK))   w |= 1u << 27;
    return w;
}
long gpio_read(uint32_t gbuf, uint32_t n) {
    uint32_t v = g_shm ? g_shm->buttons : 0;   /* active-high; gp2xshm.h == GP2X button word.
       The WIZ firmware libSDL's joystick driver parses this same GP2X order (its update fn
       bit-tests 0..18 and posts joystick button i for bit i -- disasm @0x3a460), so the GP2X
       word is correct for Wiz too; do NOT serve the Pollux pad layout here. */
    if (getenv("ME_INPUTLOG") && v) { static int g = 0; if (g++ < 30) fprintf(stderr, "[gpio] read btns=%08x\n", v); }
    uint32_t k = n < 4 ? n : 4;
    if (gbuf && k) uc_mem_write(g_uc, gbuf, &v, k);
    return (long)k;
}

/* ---- /dev/touchscreen/wm97xx (GP2X F200) ----------------------------------
   The GPH F200 kernel driver hands out 16-byte samples:
     struct TS_EVENT { u16 pressure; u16 x; u16 y; u16 pad; struct timeval stamp; }
   with raw ADC coordinates (the community-standard calibration maps x 200..3750 ->
   0..319 and y 3860..230 -> 0..239, i.e. raw y grows UPWARD). We synthesise raw
   coords from the shm touch state through that same mapping so titles using the
   stock GPH sample transform land on the pixel the viewer's mouse points at.
   Sample pacing mirrors the real driver: an event whenever the state changes, plus
   a ~100Hz stream at ALL times — the real WM9712 is a free-running ADC, so reads
   deliver current-state samples (pressure 0 when idle) continuously. GLBasic's
   startup gates on exactly that: it loops its touch poll until it SEES a pen-up
   sample (a double flag init'd 1.0, cleared only by a sample), so an
   only-on-change model parked GP2X_Nat2007 (and kin) on "Loading..." forever.
   The 10ms pace keeps it burst-bounded, so a title draining the queue in its
   frame loop still always runs dry. */
static struct { int16_t x, y; uint32_t down; uint64_t last_us; } g_ts_last;
static uint64_t ts_now_us(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
int ts_pending(void) {
    if (!g_shm) return 0;
    if (g_shm->touch_down != g_ts_last.down ||
        (g_shm->touch_down && (g_shm->touch_x != g_ts_last.x || g_shm->touch_y != g_ts_last.y)))
        return 1;
    return ts_now_us() - g_ts_last.last_us >= 10000;  /* ~100Hz free-running sample stream */
}
long ts_read(uint32_t gbuf, uint32_t n) {
    if (!g_shm || n < 16 || !ts_pending()) return 0;   /* no sample: read returns 0 (poll gates) */
    int16_t px = g_shm->touch_x, py = g_shm->touch_y;
    uint32_t down = g_shm->touch_down;
    if (px < 0) px = 0;
    if (px > 319) px = 319;
    if (py < 0) py = 0;
    if (py > 239) py = 239;
    uint16_t ev[4];
    ev[0] = down ? 1000 : 0;                            /* pressure */
    ev[1] = (uint16_t)(200 + (uint32_t)px * (3750 - 200) / 319);
    ev[2] = (uint16_t)(3860 - (uint32_t)py * (3860 - 230) / 239);
    ev[3] = 0;
    uint64_t now = ts_now_us();
    uint32_t stamp[2] = { (uint32_t)(now / 1000000), (uint32_t)(now % 1000000) };
    uc_mem_write(g_uc, gbuf, ev, 8);
    uc_mem_write(g_uc, gbuf + 8, stamp, 8);
    g_ts_last.x = g_shm->touch_x; g_ts_last.y = g_shm->touch_y;
    g_ts_last.down = down; g_ts_last.last_us = now;
    return 16;
}

/* ---- MLC 8-bit palette (PALLT_A index @0x2958, PALLT_D data @0x295a) -------
   The palette is a write-only hardware port — the value never survives in RAM —
   so present() can't read it back. Capture it from the MMSP2-page write hook: write
   the start index to PALLT_A, then 2 halfwords/entry to PALLT_D (first (G<<8)|B,
   then R), the index auto-incrementing. Reconstructs the 256-entry RGB888 palette so
   8-bit framebuffers present in true colour (else an RGB332 approximation). */
#define MMSP2_PALLT_A 0x2958
#define MMSP2_PALLT_D 0x295a
uint8_t g_pal[256][3]; int g_pal_have = 0;
int g_stl_bpp = 0;   /* MLC_STL_CNTL-declared depth (0 = never declared) */
static uint16_t g_pal_idx; static uint8_t g_pal_phase; static uint16_t g_pal_gb;
void gp2x_mmio_palette(uint32_t off, uint32_t val) {
    if (off == MMSP2_PALLT_A) { g_pal_idx = (uint16_t)(val & 0xff); g_pal_phase = 0; }
    else if (off == MMSP2_PALLT_D) {
        if (g_pal_phase == 0) { g_pal_gb = (uint16_t)val; g_pal_phase = 1; }
        else {
            uint8_t e = (uint8_t)(g_pal_idx & 0xff);
            g_pal[e][0] = (uint8_t)(val & 0xff);            /* R */
            g_pal[e][1] = (uint8_t)((g_pal_gb >> 8) & 0xff);/* G */
            g_pal[e][2] = (uint8_t)(g_pal_gb & 0xff);       /* B */
            g_pal_idx = (uint16_t)((g_pal_idx + 1) & 0xff);
            g_pal_phase = 0; g_pal_have = 1;
        }
    }
}

/* ---- MMSP2 2D "MESG" blitter (0xE0020000) ---------------------------------
   minlib-style games (Vektar) draw entirely through the 2D blitter — the CPU never
   touches fb0/fb1, so they'd stay black. We shadow the blitter registers from a write
   hook on the mapped window and run the blit when MESGSTATUS is written BUSY (the hw
   run-trigger). Handles solid fill (forecolor) + video->video copy with colour-key
   transparency, 8/16bpp. FIFO (system-mem) sources, 1-bpp expand, and blend ROPs are
   skipped (logged). Ported from host/common/gp2x_device.c. Reg map: paeryn mmsp2_regs.h. */
#define MESG_DSTCTRL 0x00
#define MESG_DSTADDR 0x04
#define MESG_DSTSTRIDE 0x08
#define MESG_SRCCTRL 0x0c
#define MESG_SRCADDR 0x10
#define MESG_SRCSTRIDE 0x14
#define MESG_PATCTRL 0x20
#define MESG_FORCOLOR 0x24
#define MESG_BACKCOLOR 0x28
#define MESG_SIZE 0x2c
#define MESG_CTRL 0x30
#define MESG_STATUS 0x34
#define MESG_B_DSTBPP16 (1u << 5)
#define MESG_B_SRCENB   (1u << 7)
#define MESG_B_INVIDEO  (1u << 8)
#define MESG_B_SRCBPP16 (1u << 5)
#define MESG_B_SRCBPP1  (1u << 6)
#define MESG_B_TRANSPEN (1u << 11)
#define MESG_B_TRANSP_SHIFT 16
#define MESG_BUSY (1u << 0)
static struct {
    uint32_t dstctrl, dstaddr, dststride, srcctrl, srcaddr, srcstride;
    uint32_t patctrl, forcolor, backcolor, size, ctrl;
} g_blt;
/* phys -> host pointer (regions are one contiguous host mmap, so base+offset is valid). */
static void *phys_to_hostptr(uint32_t phys) {
    uint32_t g; if (!phys_to_guest(phys, &g)) return NULL; return guest_to_host(g);
}
static void blit_exec(void) {
    uint32_t w = g_blt.size & 0xffff, h = (g_blt.size >> 16) & 0xffff;
    if (!w || !h) return;
    uint32_t ctrl = g_blt.ctrl;
    int transp = (ctrl & MESG_B_TRANSPEN) != 0;
    uint32_t transpc = ctrl >> MESG_B_TRANSP_SHIFT;
    int dbpp = (g_blt.dstctrl & MESG_B_DSTBPP16) ? 16 : 8, dbytes = dbpp / 8;
    uint32_t dpixoff = (dbpp == 16) ? ((g_blt.dstctrl >> 4) & 1) : ((g_blt.dstctrl >> 3) & 3);
    uint8_t *dst = phys_to_hostptr(g_blt.dstaddr & ~3u);
    uint32_t dstride = g_blt.dststride;
    const char *why = "ok";
    if (!dst) { why = "dst-unmapped"; goto done; }
    dst += dpixoff * dbytes;
    if (g_blt.srcctrl & MESG_B_SRCENB) {                 /* copy blit */
        if (!(g_blt.srcctrl & MESG_B_INVIDEO)) { why = "fifo-src"; goto done; }
        int sbpp = (g_blt.srcctrl & MESG_B_SRCBPP16) ? 16
                 : (g_blt.srcctrl & MESG_B_SRCBPP1)  ? 1 : 8;
        if (sbpp != dbpp) { why = "bpp-mismatch"; goto done; }
        uint32_t spixoff = (sbpp == 16) ? ((g_blt.srcctrl >> 4) & 1) : ((g_blt.srcctrl >> 3) & 3);
        uint8_t *src = phys_to_hostptr(g_blt.srcaddr & ~3u);
        if (!src) { why = "src-unmapped"; goto done; }
        src += spixoff * dbytes;
        uint32_t sstride = g_blt.srcstride;
        for (uint32_t y = 0; y < h; y++) {
            uint8_t *drow = dst + (size_t)y * dstride;
            const uint8_t *srow = src + (size_t)y * sstride;
            if (dbpp == 16) {
                uint16_t *dp = (uint16_t *)drow; const uint16_t *sp = (const uint16_t *)srow;
                for (uint32_t x = 0; x < w; x++) { uint16_t p = sp[x];
                    if (!(transp && p == (uint16_t)transpc)) dp[x] = p; }
            } else {
                for (uint32_t x = 0; x < w; x++) { uint8_t p = srow[x];
                    if (!(transp && p == (uint8_t)transpc)) drow[x] = p; }
            }
        }
    } else {                                             /* solid fill (forcolor) */
        uint32_t fc = g_blt.forcolor;
        for (uint32_t y = 0; y < h; y++) {
            uint8_t *drow = dst + (size_t)y * dstride;
            if (dbpp == 16) { uint16_t *dp = (uint16_t *)drow;
                              for (uint32_t x = 0; x < w; x++) dp[x] = (uint16_t)fc; }
            else            memset(drow, (int)(fc & 0xff), w);
        }
    }
done:
    if (why[0] != 'o' /* != "ok" */)   /* a blit op we couldn't execute (fifo-src, bpp-mismatch, ...) */
        me_report(MR_UNSUPPORTED_BLIT, 0, why, 0);
    if (getenv("ME_GP2X_BLITLOG"))
        fprintf(stderr, "  BLIT %s: dst=%08x(+%u) %ux%u dbpp=%d src=%08x sctrl=%08x ctrl=%08x fc=%08x\n",
                why, g_blt.dstaddr, dpixoff, w, h, dbpp, g_blt.srcaddr, g_blt.srcctrl, ctrl, g_blt.forcolor);
}
void gp2x_blitter_write(uint32_t off, uint32_t val, int size) {
    (void)size;
    switch (off) {
    case MESG_DSTCTRL:   g_blt.dstctrl   = val; break;
    case MESG_DSTADDR:   g_blt.dstaddr   = val; break;
    case MESG_DSTSTRIDE: g_blt.dststride = val; break;
    case MESG_SRCCTRL:   g_blt.srcctrl   = val; break;
    case MESG_SRCADDR:   g_blt.srcaddr   = val; break;
    case MESG_SRCSTRIDE: g_blt.srcstride = val; break;
    case MESG_PATCTRL:   g_blt.patctrl   = val; break;
    case MESG_FORCOLOR:  g_blt.forcolor  = val; break;
    case MESG_BACKCOLOR: g_blt.backcolor = val; break;
    case MESG_SIZE:      g_blt.size      = val; break;
    case MESG_CTRL:      g_blt.ctrl      = val; break;
    case MESG_STATUS:    if (val & MESG_BUSY) blit_exec(); break;
    default: break;
    }
}
uint32_t g_blit_guest = 0;   /* guest base of the 0xe0020000 blitter window */
void blitter_write_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                      int size, int64_t value, void *user) {
    (void)uc; (void)type; (void)user;
    if (!g_blit_guest) return;
    gp2x_blitter_write((uint32_t)addr - g_blit_guest, (uint32_t)value, size);
}
/* The game triggers a blit by storing BUSY to MESGSTATUS, then spins `while(STATUS & BUSY)`.
   Our blits run synchronously, so the engine is never busy. We can't clear BUSY from the WRITE
   hook (Unicorn fires it before the CPU store, which then writes BUSY back), so serve every
   STATUS read as 0 (idle) from a READ hook — the poll exits immediately. */
void blitter_read_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                     int size, int64_t value, void *user) {
    (void)type; (void)value; (void)user;
    if (!g_blit_guest) return;
    if ((uint32_t)addr - g_blit_guest == MESG_STATUS) {
        uint32_t z = 0; uc_mem_write(uc, g_blit_guest + MESG_STATUS, &z, size < 4 ? size : 4);
    }
}

/* ---- /dev/dsp (OSS) audio -> shm audio ring (consumed by the viewer) ---- */
uint32_t g_aud_freq = 44100, g_aud_ch = 2, g_aud_bits = 16;
double g_aud_t0 = 0; int g_aud_on = 0;
/* Promoted out of a function-static so a reload can reset it (else the new game inherits a
   stale TCOUNT epoch -> a huge first dt). */
double g_tcount_t0 = 0;            /* TCOUNT free-running-timer epoch */
double host_now(void) {
    struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec * 1e-6;
}
/* Advance the read cursor as if played in real time, so the ring drains and the game keeps
   producing at the right rate even with NO viewer attached. When a viewer IS attached it owns
   a_read (it advances it as it feeds SDL); draining here too makes BOTH move a_read and they
   fight -- if our wall-clock estimate races ahead of the viewer it jumps a_read past audio the
   viewer hasn't read yet, so the viewer skips chunks -> clicks/dropouts ("radio static"). This
   was masked while the viewer kept a deep (silence-padded) queue ahead of wall-clock. Detect an
   attached viewer via its heartbeat and leave a_read entirely to it. */
double g_last_hb_t = 0; uint32_t g_last_hb = 0;
int viewer_attached(void) {
    if (!g_shm) return 0;
    uint32_t hb = g_shm->viewer_heartbeat;
    double now = host_now();
    if (hb != g_last_hb) { g_last_hb = hb; g_last_hb_t = now; }
    return (g_last_hb_t != 0) && (now - g_last_hb_t < 0.5);   /* beat within 500ms = live viewer */
}
void aud_drain(void) {
    if (!g_shm) return;
    if (viewer_attached()) { g_aud_on = 0; return; }   /* viewer owns a_read; rebase our clock */
    if (!g_aud_on) { g_aud_t0 = host_now(); g_aud_on = 1; }
    uint32_t bps = g_aud_freq * g_aud_ch * (g_aud_bits / 8);
    if (!bps) return;
    uint64_t consumed = (uint64_t)((host_now() - g_aud_t0) * bps);
    if (consumed > g_shm->a_write) consumed = g_shm->a_write;
    if (consumed > g_shm->a_read) g_shm->a_read = (uint32_t)consumed;  /* viewer may be ahead */
}
uint32_t aud_free(void) {
    aud_drain();
    if (!g_shm) return 0;
    uint32_t used = g_shm->a_write - g_shm->a_read;
    return used < GP2XSHM_ARING ? GP2XSHM_ARING - used : 0;
}
/* writer-pacing state: like a real OSS blocking write, the game's audio thread must be
   slowed to real time, else it free-runs (~1000x) spinning on a full ring while holding
   its mixer mutex -> starves other threads waiting on that lock. */
int g_prod_on = 0; double g_prod_t0 = 0; uint64_t g_prod_bytes = 0;

/* ME_AUDIO_DUMP=<path>: append every PCM buffer the game hands /dev/dsp, captured BEFORE the ring's
   drop-oldest policy below and independent of a_read. An external consumer polling g_shm->aring
   cannot do this: dsp_write never blocks, so when the reader falls behind it silently discards the
   oldest samples -- which makes "is the BGM actually static, or is my capture just lossy?"
   unanswerable (exactly the question Her Knights' radio-static BGM poses). Raw PCM, matching the
   FAKESDL_AUDIO_DUMP / ME_AUDIODUMP convention; <path>.meta records the format so the dump is still
   decodable after the engine (and its shm header) is gone. One file per engine PROCESS: it is
   deliberately left open across reloads (devices_reset does not touch it) rather than reopened,
   since "wb" would truncate the outgoing title's audio at every GPEComp re-exec / File-Open. */
static FILE *g_adump = NULL;
static int   g_adump_tried = 0;
static void adump_write(const uint8_t *pcm, uint32_t n) {
    if (!g_adump_tried) {
        g_adump_tried = 1;
        const char *p = getenv("ME_AUDIO_DUMP");
        if (p && *p && (g_adump = fopen(p, "wb"))) {
            char mp[PATH_MAX]; FILE *m;
            snprintf(mp, sizeof mp, "%s.meta", p);
            if ((m = fopen(mp, "w"))) {   /* format is settled by dsp_ioctl before the first write */
                fprintf(m, "freq %u\nchannels %u\nbits %u\n", g_aud_freq, g_aud_ch, g_aud_bits);
                fclose(m);
            }
        }
    }
    if (g_adump) { fwrite(pcm, 1, n, g_adump); fflush(g_adump); }
}

long dsp_write(uint32_t gbuf, uint32_t n) {
    if (!g_shm) return n;
    aud_drain();                                   /* advance a_read by wall clock */
    if (n > GP2XSHM_ARING) n = GP2XSHM_ARING;
    uint32_t used = g_shm->a_write - g_shm->a_read;
    uint32_t freeb = used < GP2XSHM_ARING ? GP2XSHM_ARING - used : 0;
    if (n > freeb) {                               /* consumer stalled: drop oldest, never block */
        uint32_t frame = g_aud_ch * (g_aud_bits / 8); if (frame < 1) frame = 1;
        uint32_t drop = ((n - freeb + frame - 1) / frame) * frame;
        if (drop > used) drop = used;
        g_shm->a_read += drop;
    }
    uint8_t *tmp = malloc(n); uc_mem_read(g_uc, gbuf, tmp, n);
    adump_write(tmp, n);                           /* lossless tap: what the GAME produced */
    uint32_t w = g_shm->a_write % GP2XSHM_ARING, first = GP2XSHM_ARING - w;
    if (first > n) first = n;
    memcpy(g_shm->aring + w, tmp, first);
    if (n > first) memcpy(g_shm->aring, tmp + first, n - first);
    g_shm->a_write += n; g_prod_bytes += n; free(tmp);
    return n;
}
/* ---- virtual OSS output buffer (the game-visible DSP queue) ----
   Real hardware has a SMALL output buffer (fragments); games pace two ways: blocking writes
   (Payback — write stalls once the buffer is full) or a GETOSPACE fill loop ("while free >=
   chunk: mix+write", THEN render a frame — falldown-class). Reporting free space from our big
   transport ring made that fill loop infinite: the ring never looked full, GETOSPACE always
   said "plenty free", and the game's main thread stayed in its audio loop forever — black
   screen at full speed with perfect audio. Model the queue the game actually sees: writes are
   instant while the virtual buffer has room (the producer may run ahead by the buffer depth,
   which also replaces the old flat 80ms streaming cushion), the syscall layer sleeps only once
   it is full, and GETOSPACE/GETODELAY answer from the same model. Depth = SETFRAGMENT when the
   game sets one, else 8 x 4096 (~186ms at 44.1k stereo). */
uint32_t g_dsp_fragsz = 4096, g_dsp_frags = 8;
static uint32_t dsp_vbuf(void) { return g_dsp_fragsz * g_dsp_frags; }
static uint32_t dsp_vqueued(void) {
    uint32_t bps = g_aud_freq * g_aud_ch * (g_aud_bits / 8);
    if (!bps) return 0;
    double now = host_now();
    if (!g_prod_on) { g_prod_on = 1; g_prod_t0 = now; g_prod_bytes = 0; }
    double played = (now - g_prod_t0) * bps;
    if (played >= (double)g_prod_bytes) {
        /* idle gap: rebase so a fresh burst isn't instantly "late" */
        if (played > (double)g_prod_bytes + bps * 0.25) { g_prod_t0 = now; g_prod_bytes = 0; }
        return 0;
    }
    return (uint32_t)((double)g_prod_bytes - played);
}
/* Microseconds the DSP-write caller should sleep so audio tracks real time (the OSS
   blocking-write pacing): sleep only for the part of the queue that overflows the buffer. */
uint32_t dsp_pace_us(void) {
    uint32_t bps = g_aud_freq * g_aud_ch * (g_aud_bits / 8);
    if (!bps) return 0;
    uint32_t q = dsp_vqueued(), vb = dsp_vbuf();
    if (q <= vb) return 0;
    return (uint32_t)((double)(q - vb) / bps * 1e6);
}
/* OSS dsp ioctl (type 'P' == 0x50); arg usually points to an int (in/out). */
long dsp_ioctl(uint32_t cmd, uint32_t arg) {
    uint32_t v = 0; if (arg) uc_mem_read(g_uc, arg, &v, 4);
    switch (cmd & 0xff) {
    case 0x02: /* SPEED   */ if (v) g_aud_freq = v; break;
    case 0x03: /* STEREO  */ g_aud_ch = v ? 2 : 1; break;
    case 0x06: /* CHANNELS*/ if (v) g_aud_ch = v; break;
    case 0x05: /* SETFMT  */ g_aud_bits = (v == 8 /*AFMT_U8*/) ? 8 : 16; break;
    case 0x04: /* GETBLKSIZE */ v = g_dsp_fragsz; break;
    case 0x0a: { /* SETFRAGMENT: 0xCCCCSSSS = count frags of 1<<S bytes -> size the virtual buffer */
        uint32_t e = v & 0xffff, ct = (v >> 16) & 0x7fff;
        if (e >= 8 && e <= 16) g_dsp_fragsz = 1u << e;
        if (ct >= 2 && ct <= 64) g_dsp_frags = ct;
        break; }
    case 0x0b: /* GETFMTS  */ v = 0x18; /* AFMT_S16_LE|AFMT_U8 */ break;
    case 0x0f: /* GETCAPS  */ v = 0; break;
    case 0x17: /* GETODELAY*/ v = dsp_vqueued(); break;
    case 0x0c: /* GETOSPACE -> audio_buf_info{fragments,fragstotal,fragsize,bytes}. Answers from
                  the virtual buffer: once the game has written ~a buffer's depth ahead of real
                  time, free space reads 0 and its fill loop exits (falldown-class main loops). */ {
        uint32_t vb = dsp_vbuf(), q = dsp_vqueued();
        uint32_t freeb = q < vb ? vb - q : 0;
        uint32_t info[4] = { freeb / g_dsp_fragsz, g_dsp_frags, g_dsp_fragsz, freeb };
        if (arg) uc_mem_write(g_uc, arg, info, 16);
        return 0;
    }
    case 0x00: /* RESET */ case 0x01: /* SYNC */ case 0x08: /* POST */ return 0;
    default: me_report(MR_UNKNOWN_IOCTL, (long)(cmd & 0xff), "dsp", 0); return 0;
    }
    /* publish the negotiated format so a viewer can open the right audio device */
    if (g_shm) {
        g_shm->audio_freq = g_aud_freq;
        g_shm->audio_format = (g_aud_bits == 8) ? 0x0008u : 0x8010u; /* U8 / S16LSB */
        g_shm->audio_channels = g_aud_ch;
        g_shm->audio_active = 1;
    }
    if (arg) uc_mem_write(g_uc, arg, &v, 4);   /* write-back the (possibly clamped) value */
    return 0;
}

/* MMSP2 MLC framebuffer-address registers (byte offsets in the 0xC0000000 block).
   The MLC has TWO scanout-address registers per layer: EADR (even field / primary) and OADR
   (odd field). Double-buffered titles page-flip via OADR (Payback uses cacheflush instead);
   single-buffered ones (paeryn-SDL: Knight Lore) set EADR once to the scanout base and draw
   in place. We watch both. */
#define MMSP2_OADRL 0x290e
#define MMSP2_OADRH 0x2910
#define MMSP2_EADRL 0x2912
#define MMSP2_EADRH 0x2914
/* The MMSP2 DPC/MLC display-controller register file (0x2800..0x295f) is the hardware display
   PIPELINE -- layer bpp, region rectangles, scaling, the gamma/colour LUT, scanout addresses. We
   bypass all of it: we present the RGB "STL" framebuffer directly at native 320x240 (intercepting
   only OADR/EADR/PALLT above). Tracing a known-good title (Payback) through ME_GP2X_MLCLOG shows it
   programs this whole block as a plain full-screen RGB setup -- regions at 0..319/0..239, an
   IDENTITY gamma ramp at 0x295e, 16bpp at MLC_STL_CNTL (0x28da=0x04ab) -- all of which our direct
   present already matches, so none of it needs emulating. Flagging each write as unknown_mmio just
   made working titles look broken. So treat the whole file as known/expected (return 1 -> the
   caller suppresses the generic unknown_mmio); ME_GP2X_MLCLOG dumps the values when a specific
   title's display pipeline actually needs investigating (e.g. a real hardware-scaled or
   YUV-overlay title -- 0x2880 alone can't be reliably decoded as "overlay" without the datasheet,
   so we don't guess). Offsets: paeryn mmsp2_regs.h. */
static int mlc_config_write(uint32_t off, uint32_t val) {
    if (off < 0x2800 || off > 0x295f) return 0;             /* not the display register file */
    if (getenv("ME_GP2X_MLCLOG"))                           /* on-demand trace of the pipeline setup */
        fprintf(stderr, "  MLC cfg %04x = %08x\n", off, val);
    return 1;                                               /* known display config, not "unknown" */
}

/* Pollux (Wiz + Caanoo) MLC layer registers in the 0xC0000000 block (polluxregs.h): capture the
   framebuffer geometry + scanout so present_guest shows the right buffer at the right depth.
   layer0: HSTRIDE0=0x4028 VSTRIDE0=0x402c ADDRESS0=0x4038 PALETTE0=0x403c; layer1: HSTRIDE1=0x405c
   VSTRIDE1=0x4060 ADDRESS1=0x406c PALETTE1=0x4070 (the Caanoo firmware menu uses layer 1, 24bpp
   RGB888; Wiz 8bpp titles like Sopwith run layer 1 in P8 palette mode, CONTROL1=0x443AD030). */
static void pollux_mlc_write(uint32_t off, uint32_t val) {
    if (getenv("ME_MLCLOG")) {   /* trace MLC layer-register programming to read off the real format */
        const char *nm = off==0x4004?"SCREENSIZE": off==0x4024?"CONTROL0": off==0x4028?"HSTRIDE0":
            off==0x402c?"VSTRIDE0": off==0x4038?"ADDRESS0": off==0x400c?"LEFTRIGHT0": off==0x4010?"TOPBOTTOM0":
            off==0x4058?"CONTROL1": off==0x405c?"HSTRIDE1": off==0x4060?"VSTRIDE1": off==0x406c?"ADDRESS1":
            off==0x4040?"LEFTRIGHT1": off==0x4044?"TOPBOTTOM1": off==0x4000?"CONTROLT":
            off==0x403c?"PALETTE0": off==0x4070?"PALETTE1": "";
        if (*nm || off==0x4028||off==0x402c||off==0x4038||off==0x405c||off==0x4060||off==0x406c)
            fprintf(stderr, "[mlc] %04x %-11s = %08x (%u)\n", off, nm, val, val);
    }
    switch (off) {
    /* MLCSCREENSIZE = ((h-1)<<16)|(w-1). A PORTRAIT setting (h > w, the 240x320 Wiz panel) means
       the title rotate-blits into the scanout buffer; present must un-rotate. */
    case 0x4004: {
        /* NOTE: on the Wiz SCREENSIZE is ALWAYS the physical 240x320 panel -- even the GPH SDL
           whose layer content is landscape (Deicide 3) writes it. A portrait SCREENSIZE is only a
           rotation CANDIDATE; present rotates only when the captured layer pitch also matches a
           portrait row (pitch == prt_w * bypp, e.g. 480 = 240*2), which landscape-content titles
           never program (their pitch is 640 or absent). */
        uint32_t w = (val & 0xffff) + 1, h = (val >> 16) + 1;
        if (w >= 64 && h >= 64 && w <= 1024 && h <= 1024) {
            g_mlc_rot = (h > w);
            if (g_mlc_rot) { g_mlc_rot_w = w; g_mlc_rot_h = h; }
        }
        break; }
    /* MLCHSTRIDE/VSTRIDE: capture only LAYER 0 (the primary RGB surface). Layer 1 (0x405c/0x4060) is
       an OVERLAY -- the firmware menu programs it as a 24bpp video plane while its UI lives on the
       16bpp /dev/fb0 buffer; letting layer-1's format drive present_guest read the 16bpp UI as
       24bpp/960 -> tripled/washed garbage. (ME_MLCL1 re-enables layer-1 capture for diagnosis.)
       In portrait mode either layer's stride describes the rotated scanout buffer. */
    case 0x4028: g_caanoo_bpp = (int)val; if (val == 1) g_fb8_mode = 1;   /* MLCHSTRIDE0 */
        if (g_mlc_rot && val >= 1 && val <= 4) g_mlc_rot_bypp = val;
        break;
    case 0x402c: g_caanoo_pitch = val;                            /* MLCVSTRIDE0 = pitch (bytes/row) */
        if (g_mlc_rot && val) g_mlc_rot_pitch = val;
        break;
    case 0x405c:                                                  /* MLCHSTRIDE1 */
        if (val == 1) g_fb8_mode = 1;                             /* 8bpp layer (Sopwith layer 1) */
        if (g_mlc_rot && val >= 1 && val <= 4) g_mlc_rot_bypp = val;
        if (getenv("ME_MLCL1")) g_caanoo_bpp = (int)val;
        break;
    case 0x4060:                                                  /* MLCVSTRIDE1 */
        if (g_mlc_rot && val) g_mlc_rot_pitch = val;
        if (getenv("ME_MLCL1")) g_caanoo_pitch = val;
        break;
    /* MLCPALETTE0/1: one 32-bit write per entry, index in [31:24], R5G6B5 colour in [15:0]
       (LF1000 databook). Feed the shared MLC palette: a captured palette is what flips
       present_guest to the 8-bit indexed path (same contract as the MMSP2 PALLT capture). */
    case 0x403c: case 0x4070: {
        uint8_t e = (uint8_t)(val >> 24);
        uint16_t c = (uint16_t)(val & 0xffff);
        uint8_t r5 = (c >> 11) & 0x1f, g6 = (c >> 5) & 0x3f, b5 = c & 0x1f;
        g_pal[e][0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        g_pal[e][1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        g_pal[e][2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        g_pal_have = 1;
        break; }
    /* MLC layer scanout base (flip). 0x4038/0x406c are MLCADDRESS0/1; the Caanoo firmware menu writes
       the base to its layer's 0x4058 register instead (0x4058 is CONTROL1 -- keep that quirk
       Caanoo-only: Wiz titles write real control values there, e.g. Sopwith's 0x443AD030, which the
       >=0x40000000 guard alone can't tell from a guest address). The value may be a PHYSICAL address
       (titles that poke /dev/mem) OR a GUEST address (the menu's mmap'd 24bpp surface, e.g.
       0x4653d020) -- try both. */
    case 0x4058: if (g_device != 2) break; /* fall through on Caanoo only */
        /* FALLTHRU */
    case 0x4038: case 0x406c: {
        uint32_t g = 0, gg;
        if (val >= 0x40000000u && val < 0x80000000u && guest_to_host(val)) g = val;   /* already guest */
        else if (val && phys_to_guest(val, &gg)) g = gg;                               /* physical */
        if (g) { g_fb_guest = g; g_flip_active = 1; g_flip_guest = g; }
        break; }
    }
}

/* Register-block offset for a guest address. A title may mmap /dev/mem @0xC0000000 MORE THAN
   ONCE (malvado's Fenix runtime maps it twice); every window is hooked, but g_mmsp2_guest only
   records the last, so offsets computed against it were garbage for the earlier windows. Resolve
   through the recorded /dev/mem maps instead. */
static uint32_t mmsp2_off(uint32_t addr) {
    for (int i = 0; i < g_nmem; i++)
        if (addr >= g_mem[i].guest && addr < g_mem[i].guest + g_mem[i].len &&
            g_mem[i].phys >= 0xC0000000u && g_mem[i].phys < 0xC0100000u)
            return g_mem[i].phys - 0xC0000000u + (addr - g_mem[i].guest);
    return addr - g_mmsp2_guest;
}
void mmsp2_write_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                           int size, int64_t value, void *user) {
    (void)type; (void)user;
    if (!g_mmsp2_guest) return;
    g_n_wr++;
    uint32_t off = mmsp2_off((uint32_t)addr);
    if (g_trace) { static int n = 0; if (n++ < 400)
        fprintf(stderr, "  MMSP2 wr %04x sz%d=%08x\n", off, size, (uint32_t)value); }
    /* Pollux MLC range. Wiz + Caanoo silicon, but also any DYNAMIC title regardless of the badge:
       every OABI dynamic title runs against the staged Wiz rootfs libSDL, whose fbcon driver pokes
       the Pollux MLC (a "GP2X"-classified port like sopwith_camel still programs an 8bpp layer +
       palette here). Static GP2X titles never touch this range (they use the 0x2800 MLC + PALLT),
       so they keep the plain unknown-register path. */
    if ((g_device != 0 || g_is_dynamic) && off >= 0x4000 && off <= 0x44b8) {
        pollux_mlc_write(off, (uint32_t)value); return;
    }
    /* ARM940 second-core control (clock 0x904, DualCPU ctrl/int 0x3b40-0x3b48). The value is also
       stored in the mapped MMSP2 RAM (so the game can read it back); here we just act on it. */
    if (off == 0x904 || off == 0x3b40 || off == 0x3b42 || off == 0x3b48) {
        me940_reg_write(off, (uint32_t)value); return;
    }
    if (off == MMSP2_PALLT_A || off == MMSP2_PALLT_D) { gp2x_mmio_palette(off, (uint32_t)value); return; }
    /* MLC_STL_CNTL bpp field (paeryn: 0x02aa=8bpp, 0x04aa=16bpp, bits 9-10). A 16bpp title
       can still upload a PALLT palette at init (JUMPNRUN), which used to flip present to the
       8-bit indexed path and shred the picture into a byte-wise LUT weave. An EXPLICIT
       16/24bpp declaration here overrides the palette inference; titles that never write
       STL_CNTL (Odonata, Knight Lore) keep the palette-implies-8bpp behaviour. */
    if (off == 0x28da) {
        int f = ((uint32_t)value >> 9) & 3;
        g_stl_bpp = (f == 0) ? 4 : (f == 1) ? 8 : (f == 2) ? 16 : 24;
        if (f == 1) g_fb8_mode = 1;
    }
    /* MLC STL hardware scaler (paeryn "hardware-scaled surfaces": the game renders a LARGER
       surface and the MLC downscales it to the 320x240 panel). paeryn programs
       HSC @0x2906 = 1024*src_w/phys_w, VSCL/VSCH @0x2908/0x290a = src_h*pitch/phys_h, and
       HW @0x290c = pitch in bytes. Present used to ignore all three and fold e.g. a
       640x480/1280-byte-pitch surface into 640-byte rows: the interleaved/sheared class
       (Volleyball, UQM). Derive the SOURCE geometry and present the surface at its real
       size (the shm contract carries up to 1024x768; the viewer scales). */
    if (off >= 0x2906 && off <= 0x290c && !(off & 1)) {
        if (getenv("ME_GP2X_MLCLOG"))
            fprintf(stderr, "  MLC scaler raw %04x sz%d = %08x\n", off, size, (uint32_t)value);
        uint32_t lo = (uint32_t)value & 0xffff;
        switch (off) {
        case 0x2906: g_mlc_hsc = lo; break;
        case 0x2908: g_mlc_vscl = lo;
                     if (size == 4) g_mlc_vsch = ((uint32_t)value >> 16) & 0xffff; break;
        case 0x290a: g_mlc_vsch = lo; break;
        case 0x290c: g_mlc_hw = lo; break;
        }
        /* What matters for present is the LINE STRIDE the panel scans at: MLC_STL_HW. A
           title in a "scaled" mode (Volleyball: HSC=2048 VS=2560 HW=1280) writes each
           scene line TWICE at the natural 640-byte pitch, and the hardware shows every
           other line by striding HW bytes per panel line. Measured on Volleyball: the
           memory holds 320 unique pixels per 640-byte line, lines duplicated, so panel
           geometry (320x240) with row stride HW reproduces the hardware picture; deriving
           a bigger virtual size from HSC/VS presented the raw doubled buffer (sheared,
           two copies side by side). g_fb_stride carries the stride; g_fbv_* stay 320x240. */
        uint32_t hw = g_mlc_hw;
        int bypp = g_fb8_mode ? 1 : 2;
        uint32_t native = 320u * (uint32_t)bypp;
        if (hw >= native && hw <= native * 4 && (hw % native) == 0 &&
            g_fb_stride != hw && g_fbv_w == 320) {
            fprintf(DIAG, "MLC scaler: panel line stride %u (HSC=%u VS=%u)\n",
                    hw, g_mlc_hsc, (g_mlc_vsch << 16) | g_mlc_vscl);
            g_fb_stride = hw;
        }
        return;   /* known display config (also covered by mlc_config_write's range) */
    }
    if (off == MMSP2_EADRL || off == MMSP2_EADRH) {   /* MLC primary scanout base (single-buffer) */
        uint16_t lo = 0, hi = 0;
        uc_mem_read(uc, g_mmsp2_guest + MMSP2_EADRL, &lo, 2);
        uc_mem_read(uc, g_mmsp2_guest + MMSP2_EADRH, &hi, 2);
        if (off == MMSP2_EADRL) { lo = value & 0xffff; if (size == 4) hi = (value >> 16) & 0xffff; }
        else hi = value & 0xffff;
        uint32_t phys = ((uint32_t)hi << 16) | lo, g;
        /* Route through the async present path (helper + present_active): the game draws in place
           and never re-writes EADR, so don't gate present on a per-frame flip (g_oadr_driven). */
        if (phys && phys_to_guest(phys, &g)) g_fb_guest = g;
        return;
    }
    if (off != MMSP2_OADRL && off != MMSP2_OADRH) {
        /* a register we don't act on (the game still sees its own stored value). Known display-
           controller config is expected (mlc_config_write surfaces only the meaningful overlay
           signal); anything else is genuinely undecoded -> record the distinct offset so a new
           title's use of an unknown block shows up. Deduped; gated so it's free when off. */
        if (me_report_active() && !mlc_config_write(off, (uint32_t)value))
            me_report(MR_UNKNOWN_MMIO, (long)off, NULL, 0);
        return;
    }
    uint16_t lo = 0, hi = 0;
    uc_mem_read(uc, g_mmsp2_guest + MMSP2_OADRL, &lo, 2);
    uc_mem_read(uc, g_mmsp2_guest + MMSP2_OADRH, &hi, 2);
    if (off == MMSP2_OADRL) { lo = value & 0xffff; if (size == 4) hi = (value >> 16) & 0xffff; }
    else hi = value & 0xffff;
    uint32_t phys = ((uint32_t)hi << 16) | lo;
    if (getenv("ME_GP2X_FLIPLOG")) {
        uint32_t g = 0; int ok = phys_to_guest(phys, &g);
        fprintf(stderr, "FLIP off=%04x phys=%08x -> guest=%08x(%s)  fb0=%08x fb1=%08x flipactive=%d\n",
                off, phys, g, ok ? "ok" : "UNRESOLVED", g_fb_guest, g_fb_guest2, g_flip_active);
    } else { static int n = 0; if (n++ < 8) fprintf(stderr, "  MMSP2 flip -> phys=%08x\n", phys); }
    /* Don't present here: this runs in the guest render thread's write hook; doing the heavy
       fb copy on it raced (crash entering a level, masked by ME_THREADDUMP's timing). Just
       record the frame boundary + flip target and let the helper thread present, frame-synced. */
    g_oadr_driven = 1;
    uint32_t g; if (phys_to_guest(phys, &g)) { g_flip_active = 1; g_flip_guest = g; }
    g_frame_ready = 1;
}
/* Pacing-register spin throttle. Fenix/BennuGD (and some minlib) titles frame-limit by
   busy-polling a timer or the vsync line millions of times a second (EpicFreeFall measured
   ~5.6M latch-write+read pairs/s on TIMER0). On hardware that just warms an otherwise idle
   CPU; under emulation every poll is a full MMIO hook, so a title that is merely WAITING
   burns a whole host core -- and a parallel sweep starves every job with it (the 27-title
   playable->ingame demotion family of the fourth 08-13 sweep). When the same thread polls
   with sub-5us gaps hundreds of times in a row, sleep briefly: the wall-clock the game
   reads keeps advancing while it sleeps, so pacing stays correct and granularity (~0.5ms)
   remains far below a frame. Genuine once-per-frame or per-tick reads never reach the
   streak threshold. ME_NO_SPINSLEEP opts out. */
static void spin_throttle(double now) {
    static int disabled = -1;
    if (disabled < 0) disabled = getenv("ME_NO_SPINSLEEP") != NULL;
    if (disabled) return;
    static __thread double last = 0;
    static __thread int streak = 0;
    if (now - last < 5e-6) {
        if (++streak >= 100) { me_usleep(500); streak = 0;
                               struct timeval tv; gettimeofday(&tv, NULL);
                               last = tv.tv_sec + tv.tv_usec * 1e-6; return; }
    } else streak = 0;
    last = now;
}
/* Serve MMSP2 register reads. The free-running microsecond timer (TCOUNT @ 0x0a00)
   must advance or the game's timing/frame loops spin forever. */
void mmsp2_read_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                          int size, int64_t value, void *user) {
    (void)type; (void)size; (void)value; (void)user;
    if (!g_mmsp2_guest) return;
    g_n_rd++;
    uint32_t off = mmsp2_off((uint32_t)addr);
    if (off == 0x0a00) {           /* TCOUNT: free-running counter, 7.3728 MHz reference crystal */
        /* The game derives BOTH frame pacing AND simulation dt from TCOUNT, so the tick rate
           sets frame rate and game speed together. The GP2X reference crystal is 7.3728 MHz
           (fps ~= 4.15*MHz -> ~30fps at intended speed); 1 MHz was the slow-motion bug.
           ME_GP2X_TIMESCALE = N sets the rate to N MHz (matches the qemu backend's knob). */
        static double hz = 0;
        if (hz == 0) { const char *e = getenv("ME_GP2X_TIMESCALE");
                       double mhz = e ? atof(e) : 7.3728; if (mhz <= 0) mhz = 7.3728;
                       hz = mhz * 1e6; }
        struct timeval tv; gettimeofday(&tv, NULL);
        double now = tv.tv_sec + tv.tv_usec * 1e-6;
        spin_throttle(now);
        if (g_tcount_t0 == 0) g_tcount_t0 = now;
        uint32_t us = (uint32_t)((now - g_tcount_t0) * hz);
        uc_mem_write(uc, (uint32_t)addr, &us, 4);   /* serve into the window actually read
                                                       (there may be several 0xC0000000 maps) */
        return;
    }
    /* Pollux DPC0/DPC1 control @0x308c/0x348c (Wiz/Caanoo). Two consumers of one register:
       open2x-wiz's GP2XWIZ_VideoInit reads bit 15 once as "display engine enabled" (the
       preset in mem.c keeps it SET, and so do we), while raw-hardware titles busy-wait on
       bit 10 -- the VSYNC interrupt-pending flag -- as their only frame pacing, then ack it
       by writing it back (wiz-car burned 1.4 BILLION reads at `ldrh; tst #0x400; beq` on an
       edge that never came). Model bit 10 as a 60Hz vsync: high for ~1ms of each 16.7ms
       period; bit 15 stays high always. */
    if ((off == 0x308c || off == 0x348c) && g_device != 0) {
        struct timeval tv; gettimeofday(&tv, NULL);
        double now = tv.tv_sec + tv.tv_usec * 1e-6;
        spin_throttle(now);
        uint16_t v = 0; uc_mem_read(uc, (uint32_t)addr & ~1u, &v, 2);
        uint64_t us = (uint64_t)(now * 1e6);
        if (us % 16667u < 1000u) v |= 0x0400; else v &= (uint16_t)~0x0400;
        v |= 0x8000;
        uc_mem_write(uc, (uint32_t)addr & ~1u, &v, 2);
        return;
    }
    /* Pollux TIMER0 count @0x1980 (Wiz/Caanoo): homebrew "ptimer" code latches the count by
       writing 0x4B to TMRCONTROL @0x1988, then reads a free-running microsecond counter here
       (cgenius WIZ_ptimer_get_ticks_ms; malvado's Fenix runtime). Zeros froze their pacing
       loops (cgenius load stuck at 0.0% with the resource loader polling ticks forever). Model
       a 1 MHz tick, the rate wiz homebrew configures. */
    if (off == 0x1980 && (g_device != 0 || g_is_dynamic) && !getenv("ME_NO_PTIMER")) {
        static double pt0 = 0;
        struct timeval tv; gettimeofday(&tv, NULL);
        double now = tv.tv_sec + tv.tv_usec * 1e-6;
        spin_throttle(now);
        if (pt0 == 0) pt0 = now;
        uint32_t us = (uint32_t)((now - pt0) * 1e6);
        uc_mem_write(uc, (uint32_t)addr, &us, 4);
        return;
    }
    /* Pollux GPIO pads @0xA000 (Wiz/Caanoo): raw-hardware titles read the button pads
       directly -- GPIOB PAD @0xA058, GPIOC PAD @0xA098 (malvado's Fenix wizJoystickRead does
       ldrh from both, combines (C<<16)|B and inverts). The pads are ACTIVE-LOW, so serving
       the mapped-RAM zeros meant EVERY button read as pressed: malvado blew through its title
       screen ("Until key(_enter)") straight into "If key(_space) exit(0)" and self-exited
       black. Bit layout recovered from fxi's key-equivalence jump table (fxi_key):
       word = ~((GPIOC<<16)|GPIOB): LEFT=16 RIGHT=17 UP=18 DOWN=19 A=20 B=21 X=22 Y=23
       SELECT=8 MENU(START)=9 L=7 R=6 VOL+=10 VOL-=11 (touch/click 27). Idle pins read HIGH. */
    if ((g_device != 0 || g_is_dynamic) &&
        (off == 0xa018 || off == 0xa058 || off == 0xa098)) {
        uint32_t w = wiz_button_word(g_shm ? g_shm->buttons : 0);
        uint32_t v = 0xffffffffu;                          /* GPIOA (0xa018): nothing active */
        if (off == 0xa058) v = ~(w & 0xffffu) ;
        if (off == 0xa098) v = ~(w >> 16);
        uc_mem_write(uc, (uint32_t)addr, &v, size == 4 ? 4 : 2);
        return;
    }
    /* GPIOB pin-level @ 0x1182: bit 4 is the LCD VSYNC line, bit 5 HSYNC. rlyeh-minlib
       titles busy-wait for a rising edge of bit 4 as their only frame pacing, so the bit
       must genuinely toggle or they spin forever (the corpus "mmio-spin" cluster: the
       0x90a/0x910 writes in those reports are just minlib's one-shot clock init). Model
       vsync high for ~1ms of each 60Hz period -- the edge cadence is what paces the game,
       the duty cycle only has to be plausibly short. HSYNC toggles fast (~15.6kHz); any
       spin loop samples far faster than it flips. Other bits keep their stored value. */
    if (off == 0x1182) {
        struct timeval tv; gettimeofday(&tv, NULL);
        uint64_t us = (uint64_t)tv.tv_sec * 1000000ull + tv.tv_usec;
        spin_throttle(us * 1e-6);
        uint16_t v = 0; uc_mem_read(uc, (uint32_t)addr, &v, 2);
        v &= (uint16_t)~0x30;
        if (us % 16667u < 1000u) v |= 0x10;   /* in vertical blank */
        if (us % 64u < 8u)       v |= 0x20;   /* in horizontal blank */
        uc_mem_write(uc, (uint32_t)addr, &v, 2);
        return;
    }
    /* GPIO button registers (active low; canonical GP2X layout, matches our shm enum):
       0x1198 lo = 8-way stick, 0x1184 hi = START/SELECT/L/R/A/B/X/Y, 0x1186 lo = VOL. */
    if (off == 0x1198 || off == 0x1184 || off == 0x1186) {
        if (g_trace) { static int n = 0; if (n++ < 12)
            fprintf(stderr, "  GPIO RD %04x pc=%08x\n", off, gread(UC_ARM_REG_PC)); }
        uint32_t b = g_shm ? g_shm->buttons : 0;
        uint16_t v;
        if (off == 0x1198)      v = 0xFF00 | (~b & 0x00FF);            /* stick (bits 0..7) */
        else if (off == 0x1184) v = 0x00FF | ((~(b >> 8) & 0xFF) << 8); /* buttons -> hi byte */
        else                    v = 0xFF00 | (~(b >> 16) & 0xFF);      /* VOL -> lo byte */
        uc_mem_write(uc, (uint32_t)addr, &v, 2);
        return;
    }
    if (g_trace) { static int n = 0; if (n++ < 200) fprintf(stderr, "  MMSP2 RD %04x\n", off); }
}

/* Reset the per-game shm cursors so a reloaded game starts from a clean audio ring and the
   viewer renegotiates the audio format on its first ioctl. frame_seq stays MONOTONIC (the
   viewer compares against its own last_seq; resetting it to 0 could make it skip the first
   new frame). buttons/quit/magic/viewer_heartbeat are preserved (the viewer owns them). */
void shm_reset_for_new_game(void) {
    if (!g_shm) return;
    g_shm->a_write = 0; g_shm->a_read = 0;
    g_shm->audio_active = 0;
}

/* Zero all per-game device/framebuffer/audio/MMSP2 state between games (engine_reset_globals).
   The shm allocation itself is preserved (the viewer thread holds it) -- see shm_reset_for_new_game. */
void devices_reset(void) {
    memset(g_devtype, 0, sizeof g_devtype); g_devn = 0;
    memset(g_fbnum, 0, sizeof g_fbnum);
    memset(g_mem, 0, sizeof g_mem); g_nmem = 0;
    g_blit_guest = 0; memset(&g_blt, 0, sizeof g_blt);
    g_pal_have = 0; g_pal_idx = 0; g_pal_phase = 0; g_stl_bpp = 0;
    g_mmsp2_guest = 0; g_fb_guest = 0; g_fb_guest2 = 0; g_fb_from_devmem = 0;
    g_caanoo_bpp = 0; g_caanoo_pitch = 0; g_fb8_mode = 0; g_fbv_w = 320; g_fbv_h = 240;
    g_mlc_hsc = 1024; g_mlc_vscl = 0; g_mlc_vsch = 0; g_mlc_hw = 0;
    g_fb_stride = 640; g_fb_bpp = 16; g_fb_xoff = 0;
    g_mlc_rot = 0; g_mlc_rot_w = 240; g_mlc_rot_h = 320; g_mlc_rot_pitch = 0; g_mlc_rot_bypp = 2;
    g_flip_active = 0; g_flip_guest = 0;
    g_oadr_driven = 0; g_frame_ready = 0;
    g_aud_freq = 44100; g_aud_ch = 2; g_aud_bits = 16;
    g_aud_t0 = 0; g_aud_on = 0;
    g_prod_on = 0; g_prod_t0 = 0; g_prod_bytes = 0;
    g_dsp_fragsz = 4096; g_dsp_frags = 8;
    g_tcount_t0 = 0;
}

/* mmap free-list: recycle freed regions instead of uc_mem_unmap, which flushes
   Unicorn's JIT translation cache (the game churns same-size anon maps ~150/s, which
   otherwise re-translates everything -> ~21 MIPS / single-digit fps). */
