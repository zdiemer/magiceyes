/* magiceyes Unicorn engine — GP2X/Wiz device model: /dev/{fb,mem,gpio,dsp,mixer},
 * MMSP2 registers, the shm framebuffer/audio bridge to the viewer, and present. */
#include "engine.h"

int g_devtype[64], g_devn = 0;
int dev_open(const char *path) {
    int t;
    if (!strncmp(path, "/dev/fb", 7))         t = DEV_FB;
    else if (!strcmp(path, "/dev/mem"))       t = DEV_MEM;
    else if (!strcmp(path, "/dev/gpio"))      t = DEV_GPIO;
    else if (!strncmp(path, "/dev/dsp", 8))   t = DEV_DSP;
    else if (!strncmp(path, "/dev/mixer", 10))t = DEV_MIXER;
    else if (!strncmp(path, "/dev/tty", 8))   t = DEV_TTY;
    else return -1;
    int i; for (i = 0; i < 64; i++) if (g_devtype[i] == 0) break;  /* reuse freed slots */
    if (i == 64) return -1;
    g_devtype[i] = t; if (i + 1 > g_devn) g_devn = i + 1;
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
    int fd = shm_open(GP2XSHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) return;
    if (ftruncate(fd, sizeof(gp2x_shm_t)) != 0) { /* may pre-exist */ }
    void *p = mmap(NULL, sizeof(gp2x_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return;
    g_shm = p; g_shm->buttons = 0; g_shm->quit = 0; g_shm->frame_seq = 0;
    g_shm->magic = GP2XSHM_MAGIC;
}

/* /dev/mem mmap tracking so MMSP2 framebuffer phys addresses resolve to guest. */
struct memmap g_mem[64]; int g_nmem = 0;
uint32_t g_mmsp2_guest = 0;   /* guest addr of the 0xC0000000 reg block */
uint32_t g_fb_guest = 0;      /* guest addr of the /dev/fb0 framebuffer */
uint32_t g_fb_guest2 = 0;     /* /dev/fb1 (double-buffering: present the active one) */
void record_memmap(uint32_t phys, uint32_t guest, uint32_t len) {
    if (g_nmem < 64) { g_mem[g_nmem] = (struct memmap){phys, guest, len}; g_nmem++; }
}
int phys_to_guest(uint32_t phys, uint32_t *out) {
    for (int i = 0; i < g_nmem; i++)
        if (phys >= g_mem[i].phys && phys < g_mem[i].phys + g_mem[i].len)
            { *out = g_mem[i].guest + (phys - g_mem[i].phys); return 1; }
    return 0;
}

/* GP2X native screen = 320x240 RGB565. Present framebuffer at `phys` to shm. */
void present_guest(uint32_t g) {
    if (!g_shm || !g) return;
    uint8_t row[320 * 2];
    for (int y = 0; y < 240; y++) {
        { uint8_t *src = guest_to_host(g + (uint32_t)y * 640); if (!src) break;
          memcpy(row, src, sizeof row); }
        memcpy(g_shm->pixels + (size_t)y * GP2XSHM_MAXW * 2, row, sizeof row);
    }
    g_shm->width = 320; g_shm->height = 240; g_shm->frame_seq++;
}
/* Once the game page-flips to a real fb address (double-buffering), lock the display to that
   front buffer: the change-heuristic in present_active() otherwise shows the half-drawn back
   buffer between flips -> sprites/text flicker. Updated on each OADR flip. */
int g_flip_active = 0; uint32_t g_flip_guest = 0;
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
    static double last = 0;            /* cap to ~60fps: the game's tiny nanosleeps call
                                          this ~2000/s, and hashing+copying every time
                                          (300MB/s) was choking the emulator. */
    double now = host_now();
    if (now - last < 0.008) return;  /* dedupe the OADRL+OADRH pair; allow up to ~100fps */
    last = now;
    if (g_flip_active) {              /* double-buffered: show the flipped-to front buffer only */
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

/* ---- /dev/dsp (OSS) audio -> shm audio ring (consumed by the viewer) ---- */
uint32_t g_aud_freq = 44100, g_aud_ch = 2, g_aud_bits = 16;
double g_aud_t0 = 0; int g_aud_on = 0;
double host_now(void) {
    struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec * 1e-6;
}
/* advance the read cursor as if played in real time, so the ring drains and the
   game keeps producing at the right rate even with no viewer attached. */
void aud_drain(void) {
    if (!g_shm) return;
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
    uint32_t w = g_shm->a_write % GP2XSHM_ARING, first = GP2XSHM_ARING - w;
    if (first > n) first = n;
    memcpy(g_shm->aring + w, tmp, first);
    if (n > first) memcpy(g_shm->aring, tmp + first, n - first);
    g_shm->a_write += n; g_prod_bytes += n; free(tmp);
    return n;
}
/* Microseconds the DSP-write caller should sleep so audio tracks real time (the OSS
   blocking-write pacing). 0 = on time/behind; rebases on a long idle gap. */
uint32_t dsp_pace_us(void) {
    uint32_t bps = g_aud_freq * g_aud_ch * (g_aud_bits / 8);
    if (!bps) return 0;
    double now = host_now();
    if (!g_prod_on) { g_prod_on = 1; g_prod_t0 = now; g_prod_bytes = 0; }
    double allowed = (now - g_prod_t0) * bps;
    if ((double)g_prod_bytes > allowed)
        return (uint32_t)(((double)g_prod_bytes - allowed) / bps * 1e6);
    if (allowed > (double)g_prod_bytes + bps * 0.25) { g_prod_t0 = now; g_prod_bytes = 0; }
    return 0;
}
/* OSS dsp ioctl (type 'P' == 0x50); arg usually points to an int (in/out). */
long dsp_ioctl(uint32_t cmd, uint32_t arg) {
    uint32_t v = 0; if (arg) uc_mem_read(g_uc, arg, &v, 4);
    switch (cmd & 0xff) {
    case 0x02: /* SPEED   */ if (v) g_aud_freq = v; break;
    case 0x03: /* STEREO  */ g_aud_ch = v ? 2 : 1; break;
    case 0x06: /* CHANNELS*/ if (v) g_aud_ch = v; break;
    case 0x05: /* SETFMT  */ g_aud_bits = (v == 8 /*AFMT_U8*/) ? 8 : 16; break;
    case 0x04: /* GETBLKSIZE */ v = 4096; break;
    case 0x0a: /* SETFRAGMENT: accept the request as-is */ break;
    case 0x0b: /* GETFMTS  */ v = 0x18; /* AFMT_S16_LE|AFMT_U8 */ break;
    case 0x0f: /* GETCAPS  */ v = 0; break;
    case 0x17: /* GETODELAY*/ aud_drain(); v = g_shm ? (g_shm->a_write - g_shm->a_read) : 0; break;
    case 0x0c: /* GETOSPACE -> audio_buf_info{fragments,fragstotal,fragsize,bytes} */ {
        uint32_t freeb = aud_free(), fsz = 4096;
        uint32_t info[4] = { freeb / fsz, GP2XSHM_ARING / fsz, fsz, freeb };
        if (arg) uc_mem_write(g_uc, arg, info, 16);
        return 0;
    }
    case 0x00: /* RESET */ case 0x01: /* SYNC */ case 0x08: /* POST */ return 0;
    default: return 0;
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

/* MMSP2 framebuffer-address registers (byte offsets in the 0xC0000000 block) */
#define MMSP2_OADRL 0x290e
#define MMSP2_OADRH 0x2910
void mmsp2_write_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                           int size, int64_t value, void *user) {
    (void)type; (void)user;
    if (!g_mmsp2_guest) return;
    g_n_wr++;
    uint32_t off = (uint32_t)addr - g_mmsp2_guest;
    if (g_trace) { static int n = 0; if (n++ < 400)
        fprintf(stderr, "  MMSP2 wr %04x sz%d=%08x\n", off, size, (uint32_t)value); }
    if (off != MMSP2_OADRL && off != MMSP2_OADRH) return;
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
/* Serve MMSP2 register reads. The free-running microsecond timer (TCOUNT @ 0x0a00)
   must advance or the game's timing/frame loops spin forever. */
void mmsp2_read_cb(uc_engine *uc, uc_mem_type type, uint64_t addr,
                          int size, int64_t value, void *user) {
    (void)type; (void)size; (void)value; (void)user;
    if (!g_mmsp2_guest) return;
    g_n_rd++;
    uint32_t off = (uint32_t)addr - g_mmsp2_guest;
    if (off == 0x0a00) {           /* TCOUNT: free-running counter, 7.3728 MHz reference crystal */
        /* The game derives BOTH frame pacing AND simulation dt from TCOUNT, so the tick rate
           sets frame rate and game speed together. The GP2X reference crystal is 7.3728 MHz
           (fps ~= 4.15*MHz -> ~30fps at intended speed); 1 MHz was the slow-motion bug.
           ME_GP2X_TIMESCALE = N sets the rate to N MHz (matches the qemu backend's knob). */
        static double t0 = 0, hz = 0;
        if (hz == 0) { const char *e = getenv("ME_GP2X_TIMESCALE");
                       double mhz = e ? atof(e) : 7.3728; if (mhz <= 0) mhz = 7.3728;
                       hz = mhz * 1e6; }
        struct timeval tv; gettimeofday(&tv, NULL);
        double now = tv.tv_sec + tv.tv_usec * 1e-6;
        if (t0 == 0) t0 = now;
        uint32_t us = (uint32_t)((now - t0) * hz);
        uc_mem_write(uc, g_mmsp2_guest + 0x0a00, &us, 4);
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
        uc_mem_write(uc, g_mmsp2_guest + off, &v, 2);
        return;
    }
    if (g_trace) { static int n = 0; if (n++ < 200) fprintf(stderr, "  MMSP2 RD %04x\n", off); }
}

/* mmap free-list: recycle freed regions instead of uc_mem_unmap, which flushes
   Unicorn's JIT translation cache (the game churns same-size anon maps ~150/s, which
   otherwise re-translates everything -> ~21 MIPS / single-digit fps). */
