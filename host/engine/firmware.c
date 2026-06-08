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

/* Writable staging root (matches where fwstage.c installs + the viewer keeps recent.txt):
   %APPDATA%\magiceyes on Windows, $HOME/.magiceyes on Linux. Shared with syscalls.c
   (rootfs candidate search + the Caanoo font overlay), so it's exported via engine.h. */
int me_writable_root(char *out, size_t cap) {
#ifdef _WIN32
    const char *base = getenv("APPDATA");
#else
    const char *base = getenv("HOME");
#endif
    if (!base || !base[0]) return 0;
#ifdef _WIN32
    snprintf(out, cap, "%s\\magiceyes", base);
#else
    snprintf(out, cap, "%s/.magiceyes", base);
#endif
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

/* ---- OABI shim overlay: replace the firmware's libSDL + DRM libs with ours ----
   The shipped bundle carries the GPH-SDK-built OABI shim (overlay-oabi/, ABI-identical to the
   firmware glibc-2.3.6 -- see guest/build_guest.sh). After staging a Wiz/F100/F200 firmware we
   copy it over the staged rootfs's /lib so dynamic titles render into the shm framebuffer and the
   Inka DRM gate is satisfied. This mirrors host/win/stage_rootfs.sh:30-52 done in-process. */
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
static void fw_overlay_oabi(const char *dest) {
    char ov[PATH_MAX];
    if (!find_overlay_oabi(ov, sizeof ov)) {
        fprintf(stderr, "magiceyes: note: no overlay-oabi/ found; firmware libSDL won't render "
                        "(dynamic titles need the bundled shim).\n");
        return;
    }
    char src[PATH_MAX], dst[PATH_MAX];
    /* fake-SDL shim over both firmware sonames */
    snprintf(src, sizeof src, "%s/libSDL-1.2.so.0", ov);
    const char *sdl[] = { "libSDL-1.2.so.0", "libSDL-1.2.so.0.11.2" };
    for (int i = 0; i < 2; i++) {
        snprintf(dst, sizeof dst, "%s/lib/%s", dest, sdl[i]); copy_file(src, dst);
    }
    /* DRM gate stubs over every soname a title's DT_NEEDED may ask for (overwrite if present;
       always ensure the canonical .so.0). */
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
    fprintf(stderr, "magiceyes: overlaid fake-SDL shim + DRM stubs (%s)\n", ov);
}
static long file_size(const char *p) { struct stat s; return stat(p, &s) == 0 ? (long)s.st_size : -1; }
/* Re-apply the shim overlay to any already-installed OABI firmware whose libSDL isn't ours yet.
   Makes upgrades seamless: firmware staged by an older magiceyes (before fw_overlay_oabi, or with
   an older shim) is healed in place at startup, so the user never has to re-install to get rendering
   + the DRM gate. Idempotent: a dir already carrying the current shim (same size) is skipped. */
void me_firmware_sync_overlays(void) {
    char wr[PATH_MAX], ov[PATH_MAX];
    if (!me_writable_root(wr, sizeof wr)) return;
    if (!find_overlay_oabi(ov, sizeof ov)) return;     /* nothing to overlay from (dev tree, no bundle) */
    char want[PATH_MAX]; snprintf(want, sizeof want, "%s/libSDL-1.2.so.0", ov);
    long wantsz = file_size(want);
    const char *dev[] = { "wiz", "f100", "f200" };
    for (int d = 0; d < 3; d++) {
        char dest[PATH_MAX], sdl[PATH_MAX];
        snprintf(dest, sizeof dest, "%s/fw/%s", wr, dev[d]);
        snprintf(sdl, sizeof sdl, "%s/lib/libSDL-1.2.so.0", dest);
        long have = file_size(sdl);
        if (have < 0) continue;                        /* device not installed */
        if (have == wantsz) continue;                  /* already our shim */
        fprintf(stderr, "magiceyes: updating shim overlay on installed '%s' firmware\n", dev[d]);
        fw_overlay_oabi(dest);
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
        /* OABI devices (Wiz/F100/F200) run their dynamic titles + gp2xmenu through the firmware
           glibc-2.3.6 rootfs we just staged, but the firmware's real libSDL pokes the MMSP2/Pollux
           hardware directly (no shm framebuffer) and the real libinkadrm DRM gate bails to gp2xmenu.
           Overlay our shim + DRM stubs on top so the staged rootfs renders + boots. Caanoo titles
           link the FOSS rootfs-eabi instead, so they need no overlay (only the firmware fonts). */
        if (strcmp(name, "caanoo")) fw_overlay_oabi(dest);
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
