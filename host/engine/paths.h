/* magiceyes -- portable, user-configurable storage paths.
 *
 * Three writable roots -- Settings (bindings/recent/games configs), Firmware (staged device
 * installs), Cache (GPEComp decompress temps + extracted-zip scratch) -- default to dirs
 * ALONGSIDE the executable, so a release is fully portable (nothing under %APPDATA%/%TEMP%).
 * Users relocate any of them via the native settings window (host/paths_win.c); overrides
 * persist to <exe_dir>/paths.conf. me_writable_root() (firmware.c) and me_host_tmpdir()
 * (syscalls.c) resolve through here. The per-game save overlay stays fixed at
 * <exe_dir>/saves/<stem> (syscalls.c, human-readable) and is NOT configured here.
 *
 * No engine dependencies (only stddef), so the host-side settings window + viewer can include
 * it directly. */
#ifndef MAGICEYES_PATHS_H
#define MAGICEYES_PATHS_H
#include <stddef.h>

typedef enum { ME_PATH_SETTINGS, ME_PATH_FIRMWARE, ME_PATH_CACHE, ME_PATH_NKINDS } me_path_kind;

void        me_paths_dir(me_path_kind k, char *out, size_t cap);     /* resolved (override|default), mkdir-p'd */
void        me_paths_default(me_path_kind k, char *out, size_t cap); /* the portable default, ignoring overrides */
int         me_paths_set(me_path_kind k, const char *dir);           /* set override ("" clears) + persist; 0 = ok */
void        me_paths_reset(void);                                    /* all kinds back to portable default + persist */
const char *me_paths_label(me_path_kind k);                          /* "Settings"/"Firmware"/"Cache" (UI label) */

/* mkdir -p. Exported for the savestate layer, which needs <exe_dir>/states/<gamekey>/ created
   the same way the configured roots are. */
void me_mkdirs(const char *dir);

#endif /* MAGICEYES_PATHS_H */
