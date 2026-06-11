/* magiceyes — per-device capability table. See device_model.h.
 *
 * Values transcribed verbatim from the device-specific branches they replace, so the
 * refactor is behavior-preserving for GP2X/Wiz/Caanoo (locked by tools/test/baselines).
 */
#include <stddef.h>
#include "device_model.h"

extern int g_device;   /* set by load_elf (elf.c); holds an enum me_device value */

static const struct me_device_model g_models[ME_DEV_COUNT] = {
    [ME_DEV_GP2X] = {
        .name = "GP2X", .guest_env = NULL,
        .fb_bpp = 16, .sd_devnode = "/dev/mmcsd/disc0/part1",
    },
    [ME_DEV_WIZ] = {
        .name = "Wiz", .guest_env = NULL,
        .fb_bpp = 16, .sd_devnode = "/dev/mmcsd/disc0/part1",
    },
    [ME_DEV_CAANOO] = {
        .name = "Caanoo", .guest_env = "caanoo",
        .pollux_mlc = 1, .bgr_present = 1, .fb_bpp = 24,
        .font_overlay = 1, .sd_devnode = "/dev/mmcblk0p1",
    },
    [ME_DEV_DIDJ] = {
        .name = "Didj", .guest_env = "didj",
        .pollux_mlc = 1,        /* LF1000 MLC == Pollux MLC (byte-identical regs) */
        .bgr_present = 1,       /* provisional: tuned against a real frame in Phase D */
        .fb_bpp = 16,           /* provisional */
        .sd_devnode = "/dev/mmcsd/disc0/part1",
    },
};

const struct me_device_model *me_model_for(enum me_device d) {
    if ((unsigned)d >= ME_DEV_COUNT) d = ME_DEV_GP2X;
    return &g_models[d];
}

const struct me_device_model *me_model(void) {
    return me_model_for((enum me_device)g_device);
}
