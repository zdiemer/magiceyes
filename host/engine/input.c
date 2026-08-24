/* magiceyes engine -- Linux input subsystem (evdev /dev/input/event* + joystick /dev/input/js*).
 *
 * Reusable: ANY guest that reads the kernel input devices sees the handheld's analog stick + face
 * buttons, sourced from the shm input state the viewer maintains. The Caanoo firmware menu needs
 * this -- it runs on the REAL firmware libSDL, whose joystick driver opens /dev/input/event0 for the
 * "Analog" stick navigation (our shim titles never hit this path; they read input through the shim).
 *
 * The shm contract (gp2xshm.h) carries a GP2X-style button bitmap + an 8-way direction (no separate
 * analog axis), so the stick is synthesised: a held direction => the axis pinned to its extreme.
 * Mapping (shm bit -> evdev/js) lives in one table so it generalises to other devices.
 */
#include "engine.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/time.h>

/* ---- evdev / joystick wire constants (linux/input.h, linux/joystick.h) ---- */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03
#define ABS_X  0x00
#define ABS_Y  0x01
#define ABS_PRESSURE 0x18
#define BTN_TOUCH    0x14a
#define SYN_REPORT 0x00
#define JS_EVENT_BUTTON 0x01
#define JS_EVENT_AXIS   0x02
#define JS_EVENT_INIT   0x80
#define AXIS_MAX 32767   /* report a full-scale digital stick (-MAX / 0 / +MAX) */

/* A face button: shm bit index (gp2xshm enum) -> evdev key code. The order here is the joystick
   button order SDL derives (ascending key code), so A == button 0 (the menu's "OK"). */
struct btn { int shmbit; int code; };
static const struct btn BTN[] = {
    { GP2X_A,      0x130 },  /* BTN_A     -> js/SDL button 0 */
    { GP2X_B,      0x131 },  /* BTN_B */
    { GP2X_X,      0x133 },  /* BTN_X */
    { GP2X_Y,      0x134 },  /* BTN_Y */
    { GP2X_L,      0x136 },  /* BTN_TL */
    { GP2X_R,      0x137 },  /* BTN_TR */
    { GP2X_SELECT, 0x13a },  /* BTN_SELECT */
    { GP2X_START,  0x13b },  /* BTN_START */
    { GP2X_CLICK,  0x13d },  /* BTN_THUMBL (stick push) */
};
#define NBTN ((int)(sizeof BTN / sizeof BTN[0]))

/* per-open state: the last input snapshot we reported, so a read emits only what changed.
   tq[]: pending touch events for the Wiz touchscreen node -- tslib's input-raw plugin reads
   ONE 16-byte input_event per read(), so a report (BTN_TOUCH, ABS_X/Y/PRESSURE, SYN) must be
   queued and drained across several reads, not crammed into one. */
struct inpst { int used, type; uint32_t last_btns; int last_ax, last_ay; int js_synced;
               uint32_t last_tdown; int last_tx, last_ty; uint64_t last_tus;
               uint8_t tq[16 * 8]; uint32_t tq_len, tq_off; };
static struct inpst g_inp[64];

static int slot(int fd) { int i = fd - DEVFD_BASE; return (i >= 0 && i < 64) ? i : -1; }

/* ---- Wiz touchscreen over evdev ---------------------------------------------
   On the real Wiz /dev/input/event0 IS the resistive touchscreen; the firmware libSDL
   reads it through tslib (module_raw input -> pthres -> variance -> dejitter -> linear),
   and the linear module applies the rootfs /etc/pointercal calibration. Buttons never
   come over evdev on the Wiz (they are the /dev/GPIO word), so for g_device==1 this node
   serves TOUCH: raw ADC coords computed by INVERTING pointercal, so tslib's calibrated
   output lands exactly on the pixel the viewer's mouse points at. */
static int32_t g_cal[7]; static int g_cal_ok = -1;   /* /etc/pointercal, lazily loaded */
static void cal_load(void) {
    if (g_cal_ok >= 0) return;
    g_cal_ok = 0;
    char host[PATH_MAX];
    if (me_rootfs_resolve("/etc/pointercal", host, sizeof host)) {
        FILE *f = fopen(host, "r");
        if (f) {
            if (fscanf(f, "%d %d %d %d %d %d %d", &g_cal[0], &g_cal[1], &g_cal[2],
                       &g_cal[3], &g_cal[4], &g_cal[5], &g_cal[6]) == 7 && g_cal[6] &&
                ((int64_t)g_cal[0] * g_cal[4] - (int64_t)g_cal[1] * g_cal[3]) != 0)
                g_cal_ok = 1;
            fclose(f);
        }
    }
}
/* screen (sx,sy) -> raw coords: invert  s = (A*raw + c) / a6  (identity without a usable cal). */
static void cal_raw(int sx, int sy, int *rx, int *ry) {
    cal_load();
    if (g_cal_ok != 1) { *rx = sx; *ry = sy; return; }
    int64_t u = (int64_t)sx * g_cal[6] - g_cal[2];
    int64_t v = (int64_t)sy * g_cal[6] - g_cal[5];
    int64_t det = (int64_t)g_cal[0] * g_cal[4] - (int64_t)g_cal[1] * g_cal[3];
    *rx = (int)(((int64_t)g_cal[4] * u - (int64_t)g_cal[1] * v) / det);
    *ry = (int)(((int64_t)g_cal[0] * v - (int64_t)g_cal[3] * u) / det);
}
static int wiz_touch_node(int type) { return g_device == 1 && type == DEV_INPUT_EV; }
static uint64_t inp_now_us(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
/* current touch state + whether it needs reporting on this slot (change, or ~100Hz while down) */
static int touch_pending(int s, uint32_t *down, int *sx, int *sy) {
    *down = g_shm ? g_shm->touch_down : 0;
    *sx = g_shm ? g_shm->touch_x : 0; *sy = g_shm ? g_shm->touch_y : 0;
    if (*down != g_inp[s].last_tdown ||
        (*down && (*sx != g_inp[s].last_tx || *sy != g_inp[s].last_ty))) return 1;
    return *down && inp_now_us() - g_inp[s].last_tus >= 10000;
}

/* current input snapshot from shm: the button bitmap + the synthesised stick axes (-MAX/0/+MAX). */
static void snapshot(uint32_t *btns, int *ax, int *ay) {
    uint32_t b = g_shm ? g_shm->buttons : 0;
    *btns = b;
    int up = (b >> GP2X_UP & 1) | (b >> GP2X_UPLEFT & 1) | (b >> GP2X_UPRIGHT & 1);
    int dn = (b >> GP2X_DOWN & 1) | (b >> GP2X_DOWNLEFT & 1) | (b >> GP2X_DOWNRIGHT & 1);
    int lf = (b >> GP2X_LEFT & 1) | (b >> GP2X_UPLEFT & 1) | (b >> GP2X_DOWNLEFT & 1);
    int rt = (b >> GP2X_RIGHT & 1) | (b >> GP2X_UPRIGHT & 1) | (b >> GP2X_DOWNRIGHT & 1);
    /* Standard joystick convention (and verified against the Caanoo FW menu wheel): stick-up = -Y,
       stick-down = +Y, so the highlight moves up for up and down for down. */
    *ax = rt ? AXIS_MAX : lf ? -AXIS_MAX : 0;
    *ay = dn ? AXIS_MAX : up ? -AXIS_MAX : 0;
}

int input_classify(const char *path) {
    if (!strncmp(path, "/dev/input/event", 16)) return DEV_INPUT_EV;
    if (!strncmp(path, "/dev/input/js", 13) || !strncmp(path, "/dev/js", 7)) return DEV_INPUT_JS;
    return 0;
}

/* Advertise exactly ONE evdev node + ONE joystick node as character devices, so a guest that
   ENUMERATES the input devices by stat()ing /dev/input/event%d (the real SDL 1.2 joystick scan does
   exactly this -- __xstat() each candidate, skip the open if it's absent) finds the handheld and
   stops. Only event0/js0 exist; event1.. would otherwise be opened+probed as duplicate joysticks.
   Returns 1 (and fills a char-device stat) for those nodes, else 0. */
int input_fake_node(const char *path, struct stat *s) {
    int js = (!strcmp(path, "/dev/input/js0") || !strcmp(path, "/dev/js0"));
    if (!js && strcmp(path, "/dev/input/event0")) return 0;
    memset(s, 0, sizeof *s);
    s->st_mode = S_IFCHR | 0660;
    s->st_rdev = js ? ((13 << 8) | 0) : ((13 << 8) | 64);  /* input major 13, js minor 0 / evdev 64 */
    s->st_ino  = js ? 0xE0E1 : 0xE0E0;
    s->st_nlink = 1;
    return 1;
}

/* is there an unreported input change on this fd? (poll()/select() POLLIN) */
int input_pending(int fd) {
    int s = slot(fd); if (s < 0 || !g_inp[s].used) return 0;
    if (wiz_touch_node(g_inp[s].type)) {
        if (g_inp[s].tq_off < g_inp[s].tq_len) return 1;   /* queued events not yet drained */
        uint32_t d; int x, y; return touch_pending(s, &d, &x, &y);
    }
    if (g_inp[s].type == DEV_INPUT_JS && !g_inp[s].js_synced) return 1;   /* js initial state */
    uint32_t btns; int ax, ay; snapshot(&btns, &ax, &ay);
    return btns != g_inp[s].last_btns || ax != g_inp[s].last_ax || ay != g_inp[s].last_ay;
}

void input_open(int fd, int type) {
    int s = slot(fd); if (s < 0) return;
    g_inp[s].used = 1; g_inp[s].type = type; g_inp[s].js_synced = 0;
    snapshot(&g_inp[s].last_btns, &g_inp[s].last_ax, &g_inp[s].last_ay);  /* no spurious initial events */
    g_inp[s].last_tdown = g_shm ? g_shm->touch_down : 0;
    g_inp[s].last_tx = g_shm ? g_shm->touch_x : 0;
    g_inp[s].last_ty = g_shm ? g_shm->touch_y : 0;
    g_inp[s].last_tus = inp_now_us();
}

/* ---- evdev: pack input_event {tv_sec, tv_usec, u16 type, u16 code, s32 value} = 16 bytes ---- */
static int ev_pack(uint8_t *p, int type, int code, int val) {
    memset(p, 0, 16);
    *(uint16_t *)(p + 8) = (uint16_t)type; *(uint16_t *)(p + 10) = (uint16_t)code;
    *(int32_t *)(p + 12) = val; return 16;
}
/* ---- joystick: pack js_event {u32 time, s16 value, u8 type, u8 number} = 8 bytes ---- */
static int js_pack(uint8_t *p, int type, int number, int val) {
    memset(p, 0, 8); *(int16_t *)(p + 4) = (int16_t)val; p[6] = (uint8_t)type; p[7] = (uint8_t)number; return 8;
}

long input_read(int fd, uint32_t gbuf, uint32_t n) {
    int s = slot(fd); if (s < 0 || !g_inp[s].used) return 0;
    if (wiz_touch_node(g_inp[s].type)) {          /* Wiz: this evdev node is the touchscreen */
        if (g_inp[s].tq_off >= g_inp[s].tq_len) {              /* queue empty: build a report */
            uint32_t down; int sx, sy;
            if (!touch_pending(s, &down, &sx, &sy)) return -11;   /* EAGAIN */
            uint8_t *q = g_inp[s].tq; uint32_t off = 0;
            int rx, ry; cal_raw(sx, sy, &rx, &ry);
            if (down != g_inp[s].last_tdown)
                off += ev_pack(q + off, EV_KEY, BTN_TOUCH, down ? 1 : 0);
            off += ev_pack(q + off, EV_ABS, ABS_X, rx);
            off += ev_pack(q + off, EV_ABS, ABS_Y, ry);
            off += ev_pack(q + off, EV_ABS, ABS_PRESSURE, down ? 255 : 0);
            off += ev_pack(q + off, EV_SYN, SYN_REPORT, 0);
            g_inp[s].tq_len = off; g_inp[s].tq_off = 0;
            g_inp[s].last_tdown = down; g_inp[s].last_tx = sx; g_inp[s].last_ty = sy;
            g_inp[s].last_tus = inp_now_us();
            if (getenv("ME_INPUTLOG")) { static int nt = 0; if (nt++ < 40)
                fprintf(stderr, "[wts] report down=%u screen=%d,%d raw=%d,%d bytes=%u\n",
                        down, sx, sy, rx, ry, off); }
        }
        uint32_t left = g_inp[s].tq_len - g_inp[s].tq_off;
        uint32_t give = (n / 16) * 16; if (give > left) give = left;
        if (!give) return -11;                                  /* n < one event */
        uc_mem_write(g_uc, gbuf, g_inp[s].tq + g_inp[s].tq_off, give);
        g_inp[s].tq_off += give;
        return (long)give;
    }
    uint32_t btns; int ax, ay; snapshot(&btns, &ax, &ay);
    if (getenv("ME_INPUTLOG") && btns) { static int nr = 0;
        if (nr++ < 40) fprintf(stderr, "[inp] read fd=%x btns=%08x ax=%d ay=%d\n", fd, btns, ax, ay); }
    uint8_t buf[16 * 64]; uint32_t off = 0;
    int isjs = (g_inp[s].type == DEV_INPUT_JS);
    int esz = isjs ? 8 : 16;

    if (isjs && !g_inp[s].js_synced) {       /* js: kernel emits the initial state with JS_EVENT_INIT */
        g_inp[s].js_synced = 1;
        for (int i = 0; i < NBTN && off + esz <= n && off + esz <= sizeof buf; i++)
            off += js_pack(buf + off, JS_EVENT_BUTTON | JS_EVENT_INIT, i, (btns >> BTN[i].shmbit) & 1);
        if (off + esz <= n) off += js_pack(buf + off, JS_EVENT_AXIS | JS_EVENT_INIT, 0, ax);
        if (off + esz <= n) off += js_pack(buf + off, JS_EVENT_AXIS | JS_EVENT_INIT, 1, ay);
        g_inp[s].last_btns = btns; g_inp[s].last_ax = ax; g_inp[s].last_ay = ay;
        if (off) { uc_mem_write(g_uc, gbuf, buf, off); return (long)off; }
    }

    uint32_t changed = btns ^ g_inp[s].last_btns;
    for (int i = 0; i < NBTN && off + esz <= n && off + esz + 16 <= sizeof buf; i++)
        if (changed >> BTN[i].shmbit & 1)
            off += isjs ? js_pack(buf + off, JS_EVENT_BUTTON, i, (btns >> BTN[i].shmbit) & 1)
                        : ev_pack(buf + off, EV_KEY, BTN[i].code, (btns >> BTN[i].shmbit) & 1);
    if (ax != g_inp[s].last_ax && off + esz <= n)
        off += isjs ? js_pack(buf + off, JS_EVENT_AXIS, 0, ax) : ev_pack(buf + off, EV_ABS, ABS_X, ax);
    if (ay != g_inp[s].last_ay && off + esz <= n)
        off += isjs ? js_pack(buf + off, JS_EVENT_AXIS, 1, ay) : ev_pack(buf + off, EV_ABS, ABS_Y, ay);
    if (!isjs && off && off + 16 <= n)        /* evdev frames end with a SYN_REPORT */
        off += ev_pack(buf + off, EV_SYN, SYN_REPORT, 0);

    g_inp[s].last_btns = btns; g_inp[s].last_ax = ax; g_inp[s].last_ay = ay;
    if (!off) return -11;                     /* O_NONBLOCK: nothing pending (EAGAIN) */
    uc_mem_write(g_uc, gbuf, buf, off); return (long)off;
}

/* set bit `b` in a guest bitmap of `len` bytes at `arg` */
static void set_bit(uint32_t arg, uint32_t len, int b) {
    uint32_t byte = b / 8; if (byte >= len) return;
    uint8_t v = 0; uc_mem_read(g_uc, arg + byte, &v, 1); v |= (uint8_t)(1u << (b & 7));
    uc_mem_write(g_uc, arg + byte, &v, 1);
}
static void wr(uint32_t arg, const void *p, uint32_t n) { uc_mem_write(g_uc, arg, p, n); }

long input_ioctl(int fd, uint32_t cmd, uint32_t arg) {
    int s = slot(fd); int isjs = (s >= 0 && g_inp[s].type == DEV_INPUT_JS);
    uint32_t nr = cmd & 0xff, size = (cmd >> 16) & 0x3fff;
    if (getenv("ME_INPUTLOG")) { static int n = 0; if (n++ < 60)
        fprintf(stderr, "[inp] ioctl fd=%x cmd=%08x nr=%02x size=%u %s\n", fd, cmd, nr, size, isjs?"js":"ev"); }
    if (!arg) return 0;
    uint32_t btns; int ax, ay; snapshot(&btns, &ax, &ay);

    if (isjs) {                                /* /dev/input/js* (legacy joystick API), type 'j'=0x6a */
        switch (nr) {
        case 0x01: { uint32_t v = 0x020000; wr(arg, &v, 4); return 0; }     /* JSIOCGVERSION */
        case 0x11: { uint8_t a = 2;  wr(arg, &a, 1); return 0; }            /* JSIOCGAXES */
        case 0x12: { uint8_t b = NBTN; wr(arg, &b, 1); return 0; }          /* JSIOCGBUTTONS */
        case 0x13: { const char *nm = "pollux-analog";                     /* JSIOCGNAME(len) */
                     uint32_t l = (uint32_t)strlen(nm) + 1; if (l > size) l = size; wr(arg, nm, l); return (long)l; }
        default: return 0;
        }
    }

    /* /dev/input/event* (evdev), type 'E'=0x45 */
    switch (nr) {
    case 0x01: { uint32_t v = 0x010001; wr(arg, &v, 4); return 0; }        /* EVIOCGVERSION */
    case 0x02: { uint16_t id[4] = { 0x0003, 0x1f00, 0x0001, 0x0100 }; wr(arg, id, 8); return 0; } /* EVIOCGID */
    case 0x06: { const char *nm = "pollux-analog";                        /* EVIOCGNAME(len) */
                 uint32_t l = (uint32_t)strlen(nm) + 1; if (l > size) l = size; wr(arg, nm, l); return (long)l; }
    case 0x18: {  /* EVIOCGKEY(len): current key state bitmap */
        uint8_t z[96]; memset(z, 0, sizeof z); uint32_t l = size < sizeof z ? size : sizeof z;
        wr(arg, z, l); for (int i = 0; i < NBTN; i++) if (btns >> BTN[i].shmbit & 1) set_bit(arg, l, BTN[i].code);
        return (long)l; }
    case 0x20: { uint8_t z[4] = {0}; uint32_t l = size < 4 ? size : 4; wr(arg, z, l);   /* EVIOCGBIT(0): ev types */
                 set_bit(arg, l, EV_SYN); set_bit(arg, l, EV_KEY); set_bit(arg, l, EV_ABS); return (long)l; }
    case 0x21: { uint8_t z[96]; memset(z, 0, sizeof z); uint32_t l = size < sizeof z ? size : sizeof z;  /* EVIOCGBIT(EV_KEY) */
                 wr(arg, z, l);
                 if (wiz_touch_node(s >= 0 ? g_inp[s].type : 0)) set_bit(arg, l, BTN_TOUCH);
                 else for (int i = 0; i < NBTN; i++) set_bit(arg, l, BTN[i].code);
                 return (long)l; }
    case 0x23: { uint8_t z[8] = {0}; uint32_t l = size < 8 ? size : 8; wr(arg, z, l);   /* EVIOCGBIT(EV_ABS) */
                 set_bit(arg, l, ABS_X); set_bit(arg, l, ABS_Y);
                 if (wiz_touch_node(s >= 0 ? g_inp[s].type : 0)) set_bit(arg, l, ABS_PRESSURE);
                 return (long)l; }
    case 0x40: case 0x41: case 0x58: {  /* EVIOCGABS(ABS_X/ABS_Y/ABS_PRESSURE): input_absinfo */
        if (wiz_touch_node(s >= 0 ? g_inp[s].type : 0)) {
            uint32_t d; int sx, sy, rx, ry;
            d = g_shm ? g_shm->touch_down : 0;
            sx = g_shm ? g_shm->touch_x : 0; sy = g_shm ? g_shm->touch_y : 0;
            cal_raw(sx, sy, &rx, &ry);
            int32_t ai[6] = { nr == 0x40 ? rx : nr == 0x41 ? ry : (d ? 255 : 0),
                              0, nr == 0x58 ? 255 : 1023, 0, 0, 0 };
            wr(arg, ai, 24); return 0;
        }
        int32_t ai[6] = { nr == 0x40 ? ax : ay, -AXIS_MAX, AXIS_MAX, 0, 0, 0 }; wr(arg, ai, 24); return 0; }
    default:
        if (nr >= 0x20 && nr <= 0x3f) { uint8_t z[96] = {0}; uint32_t l = size < sizeof z ? size : sizeof z;
                                        wr(arg, z, l); return (long)l; }   /* other EVIOCGBIT(ev): empty */
        return 0;                                                         /* GRAB/REP/etc: accept */
    }
}
