/* magiceyes Unicorn engine — locate a staged device firmware for "boot to firmware".
 *
 * Firmware boot runs the device's own gp2xmenu launcher (a normal dynamic ARM ELF) under the
 * engine, with the firmware rootfs. me_firmware_paths() resolves a device name to its staged
 * rootfs dir and the gp2xmenu inside it. The in-process stager (fwstage.c) writes staged
 * firmware to <writable>/fw/<device>; until a device is staged we also accept the pre-extracted
 * rootfs that already ships under assets/ (Wiz). See firmware-boot-support (memory) + the plan. */
#include "engine.h"
#include "fwstage.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static int is_file(const char *p) { struct stat s; return stat(p, &s) == 0 && (s.st_mode & S_IFREG); }
/* A usable shared lib: a real, NON-EMPTY file. A firmware rootfs extracted on Linux has
   symlinked libs (ld-linux.so.2 -> ld-2.x.so); on Windows those read as 0-byte broken links, so
   a rootfs with a present gp2xmenu but a 0-byte linker is unbootable (black screen). stat() on
   Linux follows the symlink (non-zero); on Windows the broken link is size 0 -> rejected. */
static int is_loadable(const char *p) { struct stat s; return stat(p, &s) == 0 && (s.st_mode & S_IFREG) && s.st_size > 0; }

/* Canonicalise to an absolute path. The caller chdir()s into the game's dir before loading,
   and gp2xmenu's ld.so opens /lib/... via the rootfs prefix, so a relative rootfs/menu path
   would break once cwd moves. realpath/_fullpath resolve "." and "..". */
static void abspath(const char *in, char *out, size_t cap) {
#ifdef _WIN32
    if (!_fullpath(out, in, (int)cap)) snprintf(out, cap, "%s", in);
#else
    char *r = realpath(in, NULL);
    if (r) { snprintf(out, cap, "%s", r); free(r); }
    else snprintf(out, cap, "%s", in);
#endif
}

/* Canonical staged-dir name for a device alias ("gp2x" -> "f100"). */
static const char *fw_dir_name(const char *device) {
    if (!device) return NULL;
    if (!strcasecmp(device, "wiz"))    return "wiz";
    if (!strcasecmp(device, "caanoo")) return "caanoo";
    if (!strcasecmp(device, "f200"))   return "f200";
    if (!strcasecmp(device, "f100") || !strcasecmp(device, "gp2x")) return "f100";
    return NULL;
}

/* Writable staging root for firmware: the configured Firmware dir (portable default
   <exe_dir>/firmware; user-relocatable via the settings window). Staged installs land under
   <root>/fw/<device>. Shared with syscalls.c (rootfs candidate search + the Caanoo font
   overlay) and exported via engine.h. me_paths_dir mkdir-p's it, so this always succeeds. */
int me_writable_root(char *out, size_t cap) {
    me_paths_dir(ME_PATH_FIRMWARE, out, cap);
    return 1;
}

/* If <rootfs>/usr/gp2x/gp2xmenu exists, fill rootfs+menu and return 1. */
static int try_rootfs(const char *cand, char *rootfs, char *menu, size_t cap) {
    char m[PATH_MAX];
    snprintf(m, sizeof m, "%s/usr/gp2x/gp2xmenu", cand);
    if (!is_file(m)) return 0;
    /* require a usable dynamic linker -- rejects a Linux-symlinked fallback rootfs on Windows
       (0-byte links) so the GUI grays Boot until a proper in-process install (deref'd) exists. */
    char ld[PATH_MAX];
    snprintf(ld, sizeof ld, "%s/lib/ld-linux.so.2", cand); int ok = is_loadable(ld);
    if (!ok) { snprintf(ld, sizeof ld, "%s/lib/ld-linux.so.3", cand); ok = is_loadable(ld); }
    if (!ok) return 0;
    abspath(cand, rootfs, cap);                          /* absolute: survives the loader's chdir */
    snprintf(menu, cap, "%s/usr/gp2x/gp2xmenu", rootfs);
    return 1;
}

/* ---- OABI DRM-gate overlay ----
   We no longer overlay our fake-SDL shim onto firmware: the device's own real libSDL renders on the
   engine's emulated MMSP2/Pollux hardware (and exports the GPH/SDL globals gp2xmenu COPY-imports),
   which the hand-written shim only approximated. We DO overlay the Inka "NED" DRM stubs on Wiz,
   because the real libinkadrm reads a handset serial from /dev/i2c-0 and bails back to gp2xmenu
   without the device. (GP2X/Caanoo gp2xmenu don't link Inka DRM.) The stub dir is the same
   guest/bin location find_overlay_oabi() locates. */
static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb"); if (!in) return -1;
    FILE *out = fopen(dst, "wb"); if (!out) { fclose(in); return -1; }
    char buf[1 << 16]; size_t n; int rc = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    fclose(in); if (fclose(out) != 0) rc = -1;
    return rc;
}
/* Locate the OABI shim dir (exe-relative, then cwd), like me_firmware_paths' search. The shipped
   bundle puts it in overlay-oabi/ next to the exe; a dev tree keeps it in bin/guest/ (where
   guest/build_guest.sh writes it and stage_rootfs.sh reads it) -- accept both. */
static int find_overlay_oabi(char *out, size_t cap) {
    const char *bases[2]; int nb = 0;
    if (g_exe_dir[0]) bases[nb++] = g_exe_dir;
    bases[nb++] = ".";
    const char *pfx[] = { "overlay-oabi", "assets/overlay-oabi", "../assets/overlay-oabi",
                          "guest", "bin/guest", "../bin/guest" };
    char cand[PATH_MAX], probe[PATH_MAX];
    for (int i = 0; i < nb; i++)
        for (int p = 0; p < (int)(sizeof pfx / sizeof pfx[0]); p++) {
            snprintf(cand, sizeof cand, "%s/%s", bases[i], pfx[p]);
            snprintf(probe, sizeof probe, "%s/libSDL-1.2.so.0", cand);
            if (is_file(probe)) { snprintf(out, cap, "%s", cand); return 1; }
        }
    return 0;
}
static void fw_overlay_drm(const char *dest) {
    char ov[PATH_MAX];
    if (!find_overlay_oabi(ov, sizeof ov)) {
        fprintf(stderr, "magiceyes: note: no DRM-stub dir found; Wiz Inka DRM gate may bail to "
                        "gp2xmenu.\n");
        return;
    }
    char src[PATH_MAX], dst[PATH_MAX];
    /* DRM gate stubs over every soname a title's DT_NEEDED may ask for (overwrite if present;
       always ensure the canonical .so.0). libSDL itself is left untouched -- the real one renders. */
    const char *drm[] = { "libinkadrm", "libdrmcode" };
    for (int b = 0; b < 2; b++) {
        snprintf(src, sizeof src, "%s/%s.so.0", ov, drm[b]);
        if (!is_file(src)) continue;
        const char *sfx[] = { ".so", ".so.0", ".so.0.0.0" };
        for (int s = 0; s < 3; s++) {
            snprintf(dst, sizeof dst, "%s/lib/%s%s", dest, drm[b], sfx[s]);
            if (s == 1 || is_file(dst)) copy_file(src, dst);   /* .so.0 always; others if present */
        }
    }
    fprintf(stderr, "magiceyes: overlaid Inka DRM gate stubs (%s)\n", ov);
}
static long file_size(const char *p) { struct stat s; return stat(p, &s) == 0 ? (long)s.st_size : -1; }
/* Reconcile already-installed firmware at startup, so the move to "run the real libSDL" never needs
   a manual re-install. Two jobs:
   1. EVERY OABI device (Wiz/F100/F200): UNDO any libSDL shim an older magiceyes overlaid. Each must
      run on its OWN real libSDL -- it renders on the emulated MMSP2/Pollux and exports the GPH/SDL
      globals gp2xmenu COPY-imports (e.g. SDL_videofd, SDL threads). The real lib survives the old
      overlay as the deref'd libSDL.so, so restore libSDL-1.2.so.0 / .0.11.2 from it (for Wiz .0.11.2
      is the real soname; for GP2X it's a bogus name the old overlay created -- either way we want
      the real bytes).
   2. Wiz only: refresh the Inka DRM-gate stubs when the bundled stub changed (libSDL is never
      overlaid now), so an upgraded DRM stub reaches an existing install. */
void me_firmware_sync_overlays(void) {
    char wr[PATH_MAX], ov[PATH_MAX];
    if (!me_writable_root(wr, sizeof wr)) return;

    /* (1) un-overlay libSDL on every installed OABI firmware: restore the real lib from libSDL.so. */
    const char *dev[] = { "wiz", "f100", "f200" };
    for (int d = 0; d < 3; d++) {
        char real[PATH_MAX], so0[PATH_MAX], so11[PATH_MAX];
        snprintf(real, sizeof real, "%s/fw/%s/lib/libSDL.so",            wr, dev[d]);
        snprintf(so0,  sizeof so0,  "%s/fw/%s/lib/libSDL-1.2.so.0",      wr, dev[d]);
        snprintf(so11, sizeof so11, "%s/fw/%s/lib/libSDL-1.2.so.0.11.2", wr, dev[d]);
        long rs = file_size(real), cs = file_size(so0);
        if (rs <= 0 || cs <= 0 || rs == cs) continue;  /* not installed, or already real (sizes match) */
        fprintf(stderr, "magiceyes: restoring real libSDL on installed '%s' firmware (un-overlaying)\n", dev[d]);
        copy_file(real, so0);
        if (file_size(so11) > 0) copy_file(real, so11);
    }

    /* (2) refresh the Wiz DRM-gate stubs if the bundled stub changed. */
    if (!find_overlay_oabi(ov, sizeof ov)) return;     /* nothing to overlay from (dev tree, no bundle) */
    char wantdrm[PATH_MAX], dest[PATH_MAX], havedrm[PATH_MAX];
    snprintf(wantdrm, sizeof wantdrm, "%s/libinkadrm.so.0", ov);
    snprintf(dest,    sizeof dest,    "%s/fw/wiz", wr);
    snprintf(havedrm, sizeof havedrm, "%s/lib/libinkadrm.so.0", dest);
    long hdrm = file_size(havedrm);
    if (hdrm < 0) return;                              /* wiz not installed */
    long wdrm = file_size(wantdrm);
    if (wdrm > 0 && hdrm != wdrm) {
        fprintf(stderr, "magiceyes: refreshing Inka DRM gate stubs on installed 'wiz' firmware\n");
        fw_overlay_drm(dest);
    }
}

/* ---- install: stage a firmware file into the writable per-device dir ---- */
static void cli_progress(void *ud, const char *msg, int pct) {
    (void)ud; if (msg) fprintf(stderr, "  [install %3d%%] %s\n", pct, msg);
}
int me_firmware_install(const char *file, const char *device) {
    fw_info info;
    if (!fw_detect(file, &info)) {
        fprintf(stderr, "magiceyes: '%s': %s\n", file, info.detail); return -1;
    }
    const char *dev = (device && device[0]) ? device : info.device;
    const char *name = fw_dir_name(dev);
    if (!name) { fprintf(stderr, "magiceyes: unknown firmware device '%s'\n", dev ? dev : "?"); return -1; }
    char wr[PATH_MAX], dest[PATH_MAX];
    if (!me_writable_root(wr, sizeof wr)) { fprintf(stderr, "magiceyes: no writable dir (set APPDATA/HOME)\n"); return -1; }
    snprintf(dest, sizeof dest, "%s/fw/%s", wr, name);
    fprintf(stderr, "magiceyes: installing %s -> %s\n", info.detail, dest);
    int rc = fw_stage(file, dev, dest, cli_progress, NULL);
    if (rc == 0) {
        /* Run each device's OWN real libSDL (renders on the emulated MMSP2/Pollux + exports the
           GPH/SDL globals gp2xmenu COPY-imports) -- we no longer overlay our fake-SDL shim. Only Wiz
           gets the Inka DRM-gate stubs overlaid: its real libinkadrm bails to gp2xmenu without a
           /dev/i2c-0 handset serial. GP2X (F100/F200) and Caanoo gp2xmenu don't link Inka DRM. */
        if (!strcmp(name, "wiz")) fw_overlay_drm(dest);
        fprintf(stderr, "magiceyes: '%s' firmware installed. Boot it with: --firmware %s\n", name, name);
    }
    else if (rc == -2) fprintf(stderr, "magiceyes: staging for this format isn't implemented yet.\n");
    else fprintf(stderr, "magiceyes: firmware install failed (%d).\n", rc);
    return rc;
}

/* Boot a device's firmware menu (called from the GUI). Pins the rootfs + device, sets firmware
   mode, and requests the in-process reload of gp2xmenu. Returns 1 if the device is staged. */
int me_firmware_boot_request(const char *device) {
    char rootfs[PATH_MAX], menu[PATH_MAX];
    if (!me_firmware_paths(device, rootfs, menu, sizeof rootfs)) return 0;
    setenv("MAGICEYES_DEVICE", device, 1);
    me_rootfs_set(rootfs);
    g_firmware_mode = 1;
    snprintf(g_firmware_menu, sizeof g_firmware_menu, "%s", menu);
    fprintf(DIAG, "magiceyes: firmware boot (GUI) %s -> %s (rootfs %s)\n", device, menu, rootfs);
    engine_request_reload(menu);
    return 1;
}

int me_firmware_paths(const char *device, char *rootfs, char *menu, size_t cap) {
    const char *name = fw_dir_name(device);
    if (!name) return 0;

    char cand[PATH_MAX], wr[PATH_MAX];
    /* 1. staged firmware in the writable root (the normal install target) */
    if (me_writable_root(wr, sizeof wr)) {
        snprintf(cand, sizeof cand, "%s/fw/%s", wr, name);
        if (try_rootfs(cand, rootfs, menu, cap)) return 1;
    }
    /* 2. staged firmware beside the exe / under assets */
    const char *bases[3]; int nb = 0;
    if (g_exe_dir[0]) { bases[nb++] = g_exe_dir; }
    bases[nb++] = ".";
    for (int i = 0; i < nb; i++) {
        const char *pfx[] = { "fw", "assets/fw", "../assets/fw" };
        for (int p = 0; p < 3; p++) {
            snprintf(cand, sizeof cand, "%s/%s/%s", bases[i], pfx[p], name);
            if (try_rootfs(cand, rootfs, menu, cap)) return 1;
        }
    }
    /* 3. pre-extracted rootfs that already ships (Wiz: assets/rootfs/0/rootfs) — lets firmware
       boot work before the in-process stager exists. */
    if (!strcmp(name, "wiz")) {
        const char *fallback[] = { "rootfs/0/rootfs", "assets/rootfs/0/rootfs",
                                   "../assets/rootfs/0/rootfs", "rootfs", "assets/rootfs-win" };
        for (int i = 0; i < nb; i++)
            for (size_t f = 0; f < sizeof fallback / sizeof fallback[0]; f++) {
                snprintf(cand, sizeof cand, "%s/%s", bases[i], fallback[f]);
                if (try_rootfs(cand, rootfs, menu, cap)) return 1;
            }
    }
    return 0;
}
