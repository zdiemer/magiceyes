/* In-process firmware staging: turn a selected firmware file (.zip / .img) into a per-device
 * rootfs the engine can boot (run its gp2xmenu). Self-contained -- vendored miniz (zip/deflate),
 * a tar reader, and per-format extractors -- so it works on native Windows with no external tools.
 * See fwstage.c and the plan. */
#ifndef ME_FWSTAGE_H
#define ME_FWSTAGE_H
#include <stddef.h>

typedef struct {
    char device[16];     /* "wiz" | "caanoo" | "f100" | "f200", or "" if unknown */
    char format[16];     /* "zip" | "ubi" | "yaffs2", or "" */
    int  ok;             /* 1 if recognised as installable firmware */
    char detail[160];    /* human-readable note for the GUI/CLI */
} fw_info;

/* progress: pct 0..100, msg a short status line (may be NULL) */
typedef void (*fw_progress)(void *ud, const char *msg, int pct);

/* Sniff a firmware file. Fills *out; returns 1 if recognised, 0 otherwise. */
int fw_detect(const char *file, fw_info *out);

/* Extract + stage `file` into `destdir` as a bootable rootfs. `device` may be "" to use the
   detected guess. Returns 0 on success, negative on error (a message is sent via cb). */
int fw_stage(const char *file, const char *device, const char *destdir, fw_progress cb, void *ud);

#endif
