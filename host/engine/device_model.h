/* magiceyes — per-device capability model.
 *
 * The engine emulates a small family of MagicEyes-SoC handhelds that differ in only a
 * handful of ways (display depth, whether they have the Pollux MLC block, firmware-font
 * overlay, the SD block-node name the firmware menu greps for, the device tag handed to
 * the guest shim). Historically these were ~15 scattered `if (g_device == 2)` checks.
 *
 * This collapses them into a single data table of capability flags keyed by `enum
 * me_device`, reached through me_model(). Adding a new system (e.g. the LeapFrog Didj /
 * LF1000, a Pollux derivative) is a table row plus its genuinely-new hardware code — not
 * another branch sprinkled across devices.c/syscalls.c/elf.c/mem.c.
 *
 * Enum values are an ABI: the shm `device` byte (gp2x_shm_t) and viewer.c key off 0/1/2.
 * Keep GP2X=0/WIZ=1/CAANOO=2 stable; new devices are strictly additive.
 */
#ifndef MAGICEYES_DEVICE_MODEL_H
#define MAGICEYES_DEVICE_MODEL_H

enum me_device {
    ME_DEV_GP2X   = 0,   /* MMSP2 */
    ME_DEV_WIZ    = 1,   /* Pollux */
    ME_DEV_CAANOO = 2,   /* Pollux */
    ME_DEV_DIDJ   = 3,   /* LeapFrog LF1000 (Pollux derivative) */
    ME_DEV_COUNT
};

struct me_device_model {
    const char *name;          /* short label for engine traces / shm header */
    const char *guest_env;     /* MAGICEYES_DEVICE value handed to the guest shim; NULL = don't set */
    unsigned    pollux_mlc : 1;/* has the 0xC0004000 MLC block -> pollux_mlc_write (Caanoo, Didj) */
    unsigned    bgr_present : 1;/* present_guest 24/32bpp B,G,R[,X] -> RGB565 path */
    int         fb_bpp;        /* advertised /dev/fb0 depth: 16 (RGB565) or 24 (BGR888) */
    unsigned    font_overlay : 1;/* firmware TrueType-font overlay under /usr/gp2x (Caanoo) */
    const char *sd_devnode;    /* SD block node reported in /proc/mounts for the firmware menu */
};

/* The model for the currently-loaded device (g_device). Always valid (defaults to GP2X). */
const struct me_device_model *me_model(void);
const struct me_device_model *me_model_for(enum me_device d);

#endif /* MAGICEYES_DEVICE_MODEL_H */
