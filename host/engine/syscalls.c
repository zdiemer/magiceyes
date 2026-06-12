/* magiceyes Unicorn engine — Linux-ARM (EABI/OABI) syscall shim. */

#include "engine.h"
#ifdef _WIN32
#include <direct.h>
#include <io.h>        /* _commit (fsync) */
#define ME_MKDIR(p) _mkdir(p)
#else
#define ME_MKDIR(p) mkdir(p, 0777)
#endif
#include <dirent.h>   /* portable directory enumeration (Linux + MinGW-w64) for getdents */
#include "glcmd.h"     /* GL render-offload syscall numbers + descriptor */

/* Host scratch dir for decompressed GPEComp temps + extracted zips: the configured Cache dir
   (portable default <exe_dir>/cache; user-relocatable via the settings window). me_paths_dir
   mkdir-p's it. */
void me_host_tmpdir(char *out, size_t cap) {
    me_paths_dir(ME_PATH_CACHE, out, cap);
}

/* Redirect guest writes/reads under /mnt/tmp and /tmp into the host scratch dir (the GPEComp
   stub writes its decompressed payload to /mnt/tmp/<name>_tmp). Identity on Linux, where those
   paths exist for real; on Windows there is no /mnt/tmp, so map it to %TEMP%\magiceyes. */
void rewrite_guest_path(const char *in, char *out, size_t cap) {
#ifdef _WIN32
    const char *rest = NULL;
    if (!strncmp(in, "/mnt/tmp/", 9)) rest = in + 9;
    else if (!strncmp(in, "/tmp/", 5)) rest = in + 5;
    if (rest) {
        char base[PATH_MAX]; me_host_tmpdir(base, sizeof base);
        char fixed[PATH_MAX]; size_t j = 0;
        for (size_t i = 0; rest[i] && j < sizeof fixed - 1; i++)
            fixed[j++] = (rest[i] == '/') ? '\\' : rest[i];
        fixed[j] = 0;
        snprintf(out, cap, "%s\\%s", base, fixed);
        return;
    }
#endif
    snprintf(out, cap, "%s", in);
}

/* ---- device rootfs (dynamic-linker path) -----------------------------------
   Dynamically-linked GP2X titles (Odonata, Wind & Water, RetroVirus) name
   /lib/ld-linux.so.2 and link libc/libSDL; the guest ld.so opens those absolute paths.
   Redirect them at a host "rootfs" dir (like qemu-user's -L): a dereferenced copy of the
   device libs + our fake-SDL shim shadowing libSDL (host/win/stage_rootfs.sh). Game assets
   are opened relative to the game's own cwd, so they don't go through here. */
static char g_rootfs[PATH_MAX]; static int g_rootfs_ok = -1;
/* Firmware boot pins the active rootfs to a known device dir, so me_rootfs_select() won't
   re-pick by PT_INTERP (Wiz/F100/F200 all use ld-linux.so.2 and can't be told apart by it).
   The pin persists across reloads (syscalls_reset doesn't touch it) so a game chain-loaded
   from gp2xmenu keeps the same firmware rootfs. me_rootfs_set(NULL) unpins. */
static int g_rootfs_pinned = 0;
void me_rootfs_set(const char *dir) {
    if (!dir || !dir[0]) { g_rootfs_pinned = 0; return; }
    snprintf(g_rootfs, sizeof g_rootfs, "%s", dir);
    g_rootfs_ok = 1; g_rootfs_pinned = 1;
    if (g_trace) fprintf(stderr, "  [rootfs] pinned %s\n", g_rootfs);
}

/* Candidate rootfs dirs. There can be more than one with DIFFERENT ABIs: the firmware
   rootfs is glibc-2.3.6 / ld-linux.so.2 (commercial titles: Deicide 3, Her Knights,
   Odonata...), while CodeSourcery-built homebrew (Patissier/rg_ura) is EABI with
   ld-linux.so.3 + a newer glibc (assets/rootfs-eabi). We can't merge them (conflicting
   libc.so.6), so we keep them side by side and SELECT one per title by its PT_INTERP. */
static char g_cands[24][PATH_MAX]; static int g_ncand = -1;
#define NCAND ((int)(sizeof g_cands / sizeof g_cands[0]))
static int cand_has(int i, const char *suffix) {
    char p[PATH_MAX]; struct stat s;
    snprintf(p, sizeof p, "%s%s", g_cands[i], suffix);
    return stat(p, &s) == 0;
}
static void rootfs_build_cands(void) {
    if (g_ncand >= 0) return;
    g_ncand = 0;
    const char *env  = getenv("ME_GP2X_ROOTFS");
    const char *env3 = getenv("ME_GP2X_ROOTFS_EABI");
    const char *envd = getenv("ME_GP2X_ROOTFS_DIDJ");   /* uClibc + LeapFrog MPI runtime */
    if (env  && g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "%s", env);
    if (env3 && g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "%s", env3);
    if (envd && g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "%s", envd);
    static const char *names[] = { "rootfs", "rootfs-win", "rootfs-eabi", "rootfs-didj" };
    if (g_exe_dir[0]) for (size_t n = 0; n < sizeof names / sizeof names[0]; n++) {
        if (g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "%s/%s", g_exe_dir, names[n]);
        if (g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "%s/assets/%s", g_exe_dir, names[n]);
        if (g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "%s/../assets/%s", g_exe_dir, names[n]);
    }
    if (g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "assets/rootfs-win");
    if (g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "assets/rootfs-eabi");
    if (g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "assets/rootfs-didj");
    if (g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "assets/rootfs/0/rootfs");
    /* Firmware installed in-process (Firmware -> Install firmware) lands in the writable root as a
       whole OABI glibc-2.3.6 rootfs with our shim overlaid -- add it so a dynamic Wiz/GP2X title
       resolves it directly, not just gp2xmenu boot. Only the OABI (ld-linux.so.2) devices: the
       Caanoo firmware is ld-linux.so.3 and must NOT win selection over the FOSS rootfs-eabi for
       Caanoo titles (it only supplies fonts, via the data overlay in me_rootfs_resolve). */
    char wr[PATH_MAX];
    if (me_writable_root(wr, sizeof wr)) {
        const char *dev[] = { "wiz", "f100", "f200" };
        for (int d = 0; d < 3; d++)
            if (g_ncand < NCAND) snprintf(g_cands[g_ncand++], PATH_MAX, "%s/fw/%s", wr, dev[d]);
    }
}
void me_rootfs_init(void) {
    if (g_rootfs_ok >= 0) return;
    rootfs_build_cands();
    g_rootfs_ok = 0;
    /* default active rootfs = first candidate providing any dynamic linker (glibc or uClibc) */
    for (int i = 0; i < g_ncand; i++)
        if (cand_has(i, "/lib/ld-linux.so.2") || cand_has(i, "/lib/ld-linux.so.3") ||
            cand_has(i, "/lib/ld-uClibc.so.0")) {
            snprintf(g_rootfs, sizeof g_rootfs, "%s", g_cands[i]); g_rootfs_ok = 1;
            if (g_trace) fprintf(stderr, "  [rootfs] default %s\n", g_rootfs); return;
        }
    if (g_trace) fprintf(stderr, "  [rootfs] none found (set ME_GP2X_ROOTFS for dynamic titles)\n");
}
/* Choose the rootfs whose /lib holds this program's interpreter (ld-linux.so.2 vs .3),
   so an EABI title gets the newer-glibc rootfs and a firmware title the 2.3.6 one.
   Returns 1 if a matching rootfs was found+selected. Called from load_elf (all entry
   points) before the interpreter/libs are opened. */
/* When several .so.2 candidates match (e.g. fw/wiz AND fw/f100 both installed), the firmware
   rootfs for the WRONG device must not win -- they share ld-linux.so.2 so the interp can't tell
   them apart. Only a POSITIVELY-detected Wiz title (g_device==1, from Pollux/Wiz sonames or an
   explicit MAGICEYES_DEVICE) rejects the GP2X firmware dirs. We must NOT do the reverse: g_device==0
   is also the ambiguous .so.2 default, and the Wiz firmware rootfs (glibc-2.3.6 + our shim overlay)
   runs dynamic GP2X titles too -- so an ambiguous title falls through to candidate order, which
   prefers fw/wiz. Returns 1 if candidate i is a wrong-device fw dir for this title. */
static int cand_device_mismatch(int i) {
    if (g_device != ME_DEV_WIZ) return 0;       /* only a confident Wiz title is picky */
    const char *p = g_cands[i];
    return strstr(p, "/fw/f100") || strstr(p, "/fw/f200");
}
int me_rootfs_select(const char *interp) {
    if (g_rootfs_pinned) return g_rootfs_ok == 1;   /* firmware boot pinned a specific rootfs */
    if (!interp || !interp[0]) return g_rootfs_ok == 1;
    rootfs_build_cands();
    const char *b = strrchr(interp, '/'); b = b ? b + 1 : interp;
    char suffix[PATH_MAX]; snprintf(suffix, sizeof suffix, "/lib/%s", b);
    /* pass 1: a device-appropriate match; pass 2: any match (no device-matching rootfs installed) */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < g_ncand; i++) {
            if (pass == 0 && cand_device_mismatch(i)) continue;
            if (cand_has(i, suffix)) {
                snprintf(g_rootfs, sizeof g_rootfs, "%s", g_cands[i]); g_rootfs_ok = 1;
                if (g_trace) fprintf(stderr, "  [rootfs] selected %s for %s\n", g_rootfs, interp);
                return 1;
            }
        }
    return 0;
}
/* Caanoo system fonts (/usr/gp2x/*.ttf): a Caanoo title links its libs from rootfs-eabi but opens
   the QType4/DGE TrueType fonts by absolute path. The shipped bundle now stages these (freely
   redistributable) fonts into rootfs-eabi/usr/gp2x, so they resolve via g_rootfs with no firmware.
   This overlay additionally prefers an INSTALLED Caanoo firmware's copy when present (e.g. the user
   booted the firmware menu). Strictly prefix-scoped to /usr/gp2x: never /lib or /usr/lib, or a
   Caanoo title would pull the firmware's glibc-2.3.6 under the Debian ld-linux.so.3 it's linked to
   -> ABI breakage. */
static int caanoo_font_overlay(const char *guest, char *out, size_t cap) {
    if (!me_model()->font_overlay || strncmp(guest, "/usr/gp2x/", 10)) return 0;
    char hp[PATH_MAX], wr[PATH_MAX]; struct stat s;
    /* 1. an installed Caanoo firmware (e.g. the user booted the firmware menu) wins. */
    if (me_writable_root(wr, sizeof wr)) {
        snprintf(hp, sizeof hp, "%s/fw/caanoo%s", wr, guest);
        if (stat(hp, &s) == 0) { snprintf(out, cap, "%s", hp); return 1; }
    }
    /* 2. the bundled (freely-redistributable) Caanoo fonts shipped next to the exe. Device-scoped,
       NOT ABI-scoped: a Caanoo title can be OABI (ld-linux.so.2 -> rootfs-win) or EABI (.so.3 ->
       rootfs-eabi), so we resolve the font here independently of g_rootfs. */
    const char *bases[2]; int nb = 0;
    if (g_exe_dir[0]) bases[nb++] = g_exe_dir;
    bases[nb++] = ".";
    const char *pfx[] = { "caanoo-fonts", "assets/caanoo-ref", "../assets/caanoo-ref" };
    for (int i = 0; i < nb; i++)
        for (int p = 0; p < (int)(sizeof pfx / sizeof pfx[0]); p++) {
            snprintf(hp, sizeof hp, "%s/%s%s", bases[i], pfx[p], guest);
            if (stat(hp, &s) == 0) { snprintf(out, cap, "%s", hp); return 1; }
        }
    static int warned = 0;
    if (!warned && strstr(guest, ".ttf")) {
        warned = 1;
        fprintf(stderr, "magiceyes: this Caanoo title is missing its system font (%s); text "
                        "will be blank.\n", guest);
    }
    return 0;
}
int me_rootfs_resolve(const char *guest, char *out, size_t cap) {
    me_rootfs_init();
    if (g_rootfs_ok != 1 || !guest || guest[0] != '/') return 0;
    if (caanoo_font_overlay(guest, out, cap)) return 1;   /* firmware font data, before g_rootfs */
    /* Skip the loader cache/preload so ld.so falls back to the LD_LIBRARY_PATH (/lib:/usr/lib)
       search -- the cache could pin a host-unreadable symlink name; the default search finds
       our deref'd libs (and the shim shadowing libSDL). */
    if (!strcmp(guest, "/etc/ld.so.cache") || !strcmp(guest, "/etc/ld.so.preload")) return 0;
    char hp[PATH_MAX]; snprintf(hp, sizeof hp, "%s%s", g_rootfs, guest);
    struct stat s;
    if (stat(hp, &s) != 0) return 0;
    snprintf(out, cap, "%s", hp);
    return 1;
}
/* Map the device SD/NAND mount points onto a host games directory, so gp2xmenu (and games it
   launches) can list /mnt/sd/game etc. ME_GP2X_SD / ME_GP2X_NAND override (set by the GUI's
   "Set games folder"); NAND defaults to the SD root; the dev default is a "games" dir beside the
   exe. The roots must be ABSOLUTE -- the loader chdir()s into the game dir, so a relative root
   would resolve against the wrong cwd. */
static void mount_root(const char *which, char *out, size_t cap) {
    const char *env = getenv(which);
    if (env && env[0]) { snprintf(out, cap, "%s", env); return; }
    if (!strcmp(which, "ME_GP2X_NAND")) {
        const char *sd = getenv("ME_GP2X_SD");
        if (sd && sd[0]) { snprintf(out, cap, "%s", sd); return; }
    }
    struct stat s;
    if (g_exe_dir[0]) {   /* g_exe_dir is absolutised at startup (main) */
        snprintf(out, cap, "%s/games", g_exe_dir);
        if (stat(out, &s) == 0) return;
        snprintf(out, cap, "%s/assets/games", g_exe_dir);
        if (stat(out, &s) == 0) return;
        snprintf(out, cap, "%s/games", g_exe_dir);   /* fall back to the first (may not exist yet) */
        return;
    }
    snprintf(out, cap, "%s", "games");
}
static int me_mount_resolve(const char *guest, char *out, size_t cap) {
    if (!g_firmware_mode) return 0;   /* only the firmware menu + games it launches use /mnt/sd|nand */
    const char *rest = NULL, *which = NULL;
    if (!strncmp(guest, "/mnt/sd", 7) && (guest[7] == 0 || guest[7] == '/')) {
        rest = guest + 7; which = "ME_GP2X_SD";
    } else if (!strncmp(guest, "/mnt/nand", 9) && (guest[9] == 0 || guest[9] == '/')) {
        rest = guest + 9; which = "ME_GP2X_NAND";
    }
    if (!rest) return 0;
    char root[PATH_MAX]; mount_root(which, root, sizeof root);
    char tail[PATH_MAX]; snprintf(tail, sizeof tail, "%s", rest);
#ifdef _WIN32
    for (char *p = tail; *p; p++) if (*p == '/') *p = '\\';   /* match rewrite_guest_path */
#endif
    snprintf(out, cap, "%s%s", root, tail);
    return 1;
}

/* Map a guest path to the host path to actually open/stat: SD/NAND games mount first, then rootfs
   (dynamic libs), then the /mnt/tmp redirect (GPEComp temps on Windows), else identity. */
static void resolve_path(const char *guest, char *out, size_t cap) {
    /* Some titles use DOS-style backslash separators (BermudaSyndrome opens "..\bermuda.ovr").
       Backslash isn't a separator on the host, so normalise it to '/' before resolving. */
    char norm[PATH_MAX];
    if (strchr(guest, '\\')) {
        size_t i = 0; for (; guest[i] && i < sizeof norm - 1; i++)
            norm[i] = (guest[i] == '\\') ? '/' : guest[i];
        norm[i] = 0; guest = norm;
    }
    if (me_mount_resolve(guest, out, cap)) return;
    if (me_rootfs_resolve(guest, out, cap)) return;
    rewrite_guest_path(guest, out, cap);
}

/* ---- persistent per-game save overlay ("set up saving properly") -----------------
   Games save into their own dir (Payback: Data/Config/Slot1.ini), but that dir is often
   read-only -- a ROM folder, or the %TEMP% spot a GPEComp payload decompressed to -- so the
   profile never persists and the game re-prompts every launch. We redirect game-data WRITES
   into a portable per-game folder, <exe_dir>/saves/<gamekey>/ (g_save_root), and read them
   back from there; reads of files NOT in the overlay fall through to the original assets.
   Directories are never claimed for reads, so asset enumeration still sees the real folder. */

/* Derive g_game_root (the .gpe/ELF's own dir, absolutised) + g_save_root from the title path. */
void me_save_set_game(const char *elf_path) {
    if (!elf_path || !elf_path[0]) { g_game_root[0] = 0; g_save_root[0] = 0; return; }
    char dir[PATH_MAX]; snprintf(dir, sizeof dir, "%s", elf_path);
    char *s1 = strrchr(dir, '/'), *s2 = strrchr(dir, '\\'), *s = s1 > s2 ? s1 : s2;
    const char *name = s ? s + 1 : elf_path;
    char stem[PATH_MAX]; snprintf(stem, sizeof stem, "%s", name);
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    if (s) *s = 0; else snprintf(dir, sizeof dir, ".");
    /* absolutise the asset dir: the engine chdir()s into it, and the overlay matches absolute
       guest paths (built from getcwd) against it. */
    char abs[PATH_MAX];
#ifdef _WIN32
    if (_fullpath(abs, dir, sizeof abs)) snprintf(g_game_root, sizeof g_game_root, "%s", abs);
#else
    if (realpath(dir, abs)) snprintf(g_game_root, sizeof g_game_root, "%s", abs);
#endif
    else snprintf(g_game_root, sizeof g_game_root, "%s", dir);
    /* sanitise the stem into a filesystem-safe gamekey */
    char key[PATH_MAX]; int k = 0;
    for (const char *p = stem; *p && k < (int)sizeof key - 1; p++) {
        char c = *p;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        key[k++] = ok ? c : '_';
    }
    key[k] = 0; if (!key[0]) snprintf(key, sizeof key, "game");
    const char *base = g_exe_dir[0] ? g_exe_dir : ".";
    snprintf(g_save_root, sizeof g_save_root, "%s/saves/%s", base, key);
    if (g_trace) fprintf(stderr, "  [saves] game_root=%s save_root=%s\n", g_game_root, g_save_root);
}

/* mkdir -p of host path `hp`'s parent dirs (not the leaf -- it's the file being created). */
static void mkdirs_for(const char *hp) {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s", hp);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/' || *p == '\\') { char c = *p; *p = 0; ME_MKDIR(tmp); *p = c; }
}

/* Best-effort byte copy src -> dst (regular files only, so a copy-up of a directory path can't
   create a bogus file; no-op if src is missing/not a regular file / dst unwritable). */
static void copy_file(const char *src, const char *dst) {
    struct stat ss; if (stat(src, &ss) != 0 || !S_ISREG(ss.st_mode)) return;
    FILE *in = fopen(src, "rb"); if (!in) return;
    FILE *o = fopen(dst, "wb"); if (!o) { fclose(in); return; }
    char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, n, o) != n) break;
    fclose(o); fclose(in);
}

/* Copy `in` into `out` with DOS-style '\\' separators normalised to '/'. */
static void norm_slashes(const char *in, char *out, size_t cap) {
    size_t i = 0; for (; in[i] && i < cap - 1; i++) out[i] = in[i] == '\\' ? '/' : in[i];
    out[i] = 0;
}
/* If normalised `path` is `root` or under it, return the tail relative to root ("" if equal);
   else NULL. Case-insensitive on Windows. */
static const char *path_under(const char *path, const char *root) {
    size_t rl = strlen(root); while (rl && root[rl - 1] == '/') rl--;
    if (!rl) return NULL;
#ifdef _WIN32
    if (strncasecmp(path, root, rl)) return NULL;
#else
    if (strncmp(path, root, rl)) return NULL;
#endif
    if (path[rl] == '/') return path + rl + 1;
    if (path[rl] == 0)   return path + rl;
    return NULL;
}

/* Map a guest path into the per-game save-overlay namespace WITHOUT any existence/type check.
   Returns 1 + fills `out` (g_save_root/<rel>) iff `guest` names data under the game's own dir.
   RELATIVE paths are ALWAYS captured (they resolve against cwd, which is the game dir) -- so a
   save can never leak back into a read-only ROM dir; when the game chdir()'d into a subdir of
   its own dir we anchor at the live cwd to keep the overlay path asset-relative, else we use
   the literal relative path. ABSOLUTE paths are captured only when under g_game_root. */
static int save_overlay_path(const char *guest, char *out, size_t cap) {
    if (!g_save_root[0] || !g_game_root[0] || getenv("ME_NOSAVES")) return 0;
    char norm[PATH_MAX]; norm_slashes(guest, norm, sizeof norm);
    char gr[PATH_MAX]; norm_slashes(g_game_root, gr, sizeof gr);
    int drive = (((norm[0] >= 'a' && norm[0] <= 'z') || (norm[0] >= 'A' && norm[0] <= 'Z')) && norm[1] == ':');
    char relbuf[PATH_MAX]; const char *rel = NULL;
    if (norm[0] != '/' && !drive) {                  /* relative -> always game data */
        rel = norm;
        char cwd[PATH_MAX], cn[PATH_MAX];
        if (getcwd(cwd, sizeof cwd)) {               /* refine: prefix cwd's offset within game dir */
            norm_slashes(cwd, cn, sizeof cn);
            const char *sub = path_under(cn, gr);
            if (sub && sub[0]) { snprintf(relbuf, sizeof relbuf, "%s/%s", sub, norm); rel = relbuf; }
        }
    } else {                                         /* absolute -> only if under the game dir */
        rel = path_under(norm, gr);
    }
    if (!rel || !rel[0] || rel[0] == '/' || strstr(rel, "..")) return 0;
    snprintf(out, cap, "%s/%s", g_save_root, rel);
    return 1;
}

/* If `guest` is game-save data, fill `out` with its overlay host path and return 1:
   - write_intent: always claim (creating parent dirs, seeding the overlay from the shipped
     file so a read-modify-write keeps the shipped content -- Payback rewrites Slot1.ini).
   - read: claim only if a regular file already exists in the overlay (so saved data is read
     back); else return 0 to fall through to the real (read-only) assets. */
static int save_overlay_resolve(const char *guest, int write_intent, char *out, size_t cap) {
    if (!save_overlay_path(guest, out, cap)) return 0;
    if (write_intent) {
        mkdirs_for(out);
        struct stat os;
        if (stat(out, &os) != 0) {
            char orig[PATH_MAX]; resolve_path(guest, orig, sizeof orig);
            copy_file(orig, out);
        }
        return 1;
    }
    struct stat s; return (stat(out, &s) == 0 && S_ISREG(s.st_mode)) ? 1 : 0;
}

/* Resolve a path-based file op to a host path, honouring the save overlay first. */
static void resolve_io(const char *guest, int write_intent, char *out, size_t cap) {
    if (save_overlay_resolve(guest, write_intent, out, cap)) return;
    resolve_path(guest, out, cap);
}

/* Guest (Linux/ARM) open() flag bits are the same numeric values on every guest ABI. A write
   intent = anything but a pure O_RDONLY open (so the target lands in the writable overlay). */
static int open_is_write(int gf) {
    enum { GO_WRONLY = 01, GO_RDWR = 02, GO_CREAT = 0100, GO_TRUNC = 01000, GO_APPEND = 02000 };
    return (gf & 03) || (gf & GO_CREAT) || (gf & GO_TRUNC) || (gf & GO_APPEND);
}

void read_cstr(uint32_t gaddr, char *out, size_t cap) {
    size_t i;
    if (cap == 0) return;
    for (i = 0; i < cap - 1; i++) {
        uint8_t c;
        if (uc_mem_read(g_uc, gaddr + i, &c, 1) != UC_ERR_OK) break;
        out[i] = (char)c;
        if (c == 0) return;
    }
    out[i] = 0;
}

/* The firmware menu gates its SD games scan on the SD partition's block device existing
   (it stat()s /dev/mmcblk0p1, else shows "Insert the SD card"). We map /mnt/sd to a host games
   dir, so report the node as a present block device to get past the dialog. Firmware-mode only,
   so a normal single-game run is unaffected. */
static int sd_fake_node(const char *p, struct stat *s) {
    if (!g_firmware_mode) return 0;
    if (strcmp(p, "/dev/mmcblk0p1") && strcmp(p, "/dev/mmcblk0")) return 0;
    memset(s, 0, sizeof *s);
    s->st_mode = S_IFBLK | 0660;
    s->st_rdev = (179 << 8) | 1;   /* MMC block major 179 */
    s->st_ino  = 0x6d6d63;         /* 'mmc' */
    s->st_dev  = 1;
    return 1;
}

/* The OSS audio nodes (/dev/dsp*, /dev/mixer*, /dev/sound/*) are intercepted at open() (dev_open
   -> DEV_DSP/DEV_MIXER) but they don't exist on the host, so a stat() falls through and fails.
   PortAudio's OSS backend (Didj AudioMPI) probes the devices by stat()ing them first and finds
   none -> "Failed to initialize audio output". Report them as present character devices so the
   probe proceeds to open() (which we serve) + the SNDCTL ioctls (dsp_ioctl). */
static int oss_fake_node(const char *p, struct stat *s) {
    int dsp = !strncmp(p, "/dev/dsp", 8) || !strcmp(p, "/dev/sound/dsp") || !strcmp(p, "/dev/audio");
    int mix = !strncmp(p, "/dev/mixer", 10) || !strcmp(p, "/dev/sound/mixer");
    if (!dsp && !mix) return 0;
    memset(s, 0, sizeof *s);
    s->st_mode = S_IFCHR | 0666;
    s->st_rdev = (14 << 8) | (dsp ? 3 : 0);   /* OSS major 14: dsp=minor 3, mixer=minor 0 */
    s->st_ino  = 0x6473 + (dsp ? 0 : 1);      /* 'ds' */
    s->st_dev  = 1;
    return 1;
}

void fill_oabi_stat(uint32_t gbuf, struct stat *hs) {
    /* `struct stat` for the plain stat/lstat/fstat syscalls (nr 106/107/108). Field OFFSETS are
       the same for OABI and EABI, but the total SIZE differs: the GP2X OABI glibc-2.3.6 struct is
       88 bytes, while the ARM **EABI** newstat struct is only **64** (st_dev@0 st_ino@4 st_mode@8
       st_nlink@10 st_uid@12 st_gid@14 st_rdev@16 st_size@20 st_blksize@24 st_blocks@28
       atime/mtime/ctime + 2 unused, ending at 64). uClibc's ld.so (Didj) uses nr 106 with a
       64-byte buffer on its stack; writing 88 overran it onto _dl_map_cache's saved sl (the GOT
       pointer), zeroing it -> a deref through sl=0 right after the call. Size it by ABI, exactly
       like fill_stat64's 96-vs-104 split. */
    uint8_t b[88]; memset(b, 0, sizeof b);
    *(uint32_t *)(b + 0)  = (uint32_t)hs->st_dev;
    *(uint32_t *)(b + 4)  = (uint32_t)hs->st_ino;
    *(uint16_t *)(b + 8)  = (uint16_t)hs->st_mode;
    *(uint16_t *)(b + 10) = (uint16_t)(hs->st_nlink ? hs->st_nlink : 1);
    *(uint32_t *)(b + 16) = (uint32_t)hs->st_rdev;
    *(uint32_t *)(b + 20) = (uint32_t)hs->st_size;
    *(uint32_t *)(b + 24) = 4096;
    *(uint32_t *)(b + 28) = (uint32_t)((hs->st_size + 511) / 512);
    uc_mem_write(g_uc, gbuf, b, g_eabi ? 64 : sizeof b);
}

/* Fill the GP2X OABI glibc-2.3.6 `struct stat64` -- sizeof **96**, NOT 104. This glibc
   was built OABI, where `long long` is 4-byte aligned (no EABI 8-byte alignment), so the
   struct is packed: st_size lands at 44 (not 48) and st_blksize at 52 (not 56). Proven
   from `_IO_file_doallocate` (0x17c168): it reserves a 104-byte frame, puts `struct stat64`
   at sp+8, and reads st_blksize at [sp,#60] = struct+52 -> the struct is exactly the 96
   bytes sp+8..sp+104. Writing 104 bytes overflowed past sp+104 onto the function's saved
   {r4,r5} (pushed before the frame), zeroing the saved FILE* in r5 -> the documented
   "null mntent stream" crash at load. Kernel layout:
     st_dev@0(8) __st_ino@12(4) st_mode@16 st_nlink@20 st_uid@24 st_gid@28
     st_rdev@32(8) st_size@44(8,packed) st_blksize@52 st_blocks@56(8) st_ino@88(8). */
void fill_stat64(uint32_t gbuf, struct stat *hs) {
    uint64_t sz = (uint64_t)hs->st_size, blk = (uint64_t)((hs->st_size + 511) / 512);
    /* Truncate st_ino to 32 bits. The guest's 32-bit fstat() (glibc __fxstat) does the fstat64
       syscall then converts to `struct stat`, returning EOVERFLOW if the 64-bit st_ino doesn't
       fit in 32 bits -- and a Windows drive mounted in WSL (drvfs, where assets/ lives) hands
       back HUGE 64-bit inodes. That EOVERFLOW makes the guest think fstat failed: Caanoo QType4's
       tt_font_init treats it as "TTF Font File Open Failed" and bails to gp2xmenu. A 32-bit-safe
       synthetic inode keeps the conversion lossless. */
    uint32_t ino = (uint32_t)hs->st_ino; if (!ino) ino = 1;
    if (getenv("ME_STATLOG")) fprintf(stderr, "  STAT64 mode=%07o size=%llu ino=%u blksz=4096 eabi=%d -> buf=%08x\n",
                                      (unsigned)hs->st_mode, (unsigned long long)sz, ino, g_eabi, gbuf);
    if (g_eabi) {
        /* Mainline ARM **EABI** `struct stat64` -- sizeof **104**, `long long` 8-byte aligned:
           st_dev@0(8) __st_ino@12 st_mode@16 st_nlink@20 st_uid@24 st_gid@28 st_rdev@32(8)
           st_size@48(8) st_blksize@56 st_blocks@64(8) ... st_ino@96(8). Used by CodeSourcery
           homebrew (Patissier) on the EABI rootfs. Writing the OABI 96B layout here gives the
           EABI ld.so a garbage st_size -> it refuses to mmap libc.so.6 -> "version GLIBC_2.4
           not defined" at relocation. */
        uint8_t b[104]; memset(b, 0, sizeof b);
        *(uint64_t *)(b + 0)  = (uint64_t)hs->st_dev;
        *(uint32_t *)(b + 12) = ino;
        *(uint32_t *)(b + 16) = (uint32_t)hs->st_mode;
        *(uint32_t *)(b + 20) = (uint32_t)(hs->st_nlink ? hs->st_nlink : 1);
        *(uint32_t *)(b + 24) = (uint32_t)hs->st_uid;
        *(uint32_t *)(b + 28) = (uint32_t)hs->st_gid;
        *(uint64_t *)(b + 32) = (uint64_t)hs->st_rdev;
        *(uint64_t *)(b + 48) = sz;                        /* st_size @48 (8-byte aligned) */
        *(uint32_t *)(b + 56) = 4096;                      /* st_blksize @56 */
        *(uint64_t *)(b + 64) = blk;                       /* st_blocks @64 */
        *(uint64_t *)(b + 96) = (uint64_t)ino;             /* 64-bit st_ino @96 (32-bit-safe) */
        uc_mem_write(g_uc, gbuf, b, sizeof b);
        return;
    }
    /* GP2X OABI glibc-2.3.6 `struct stat64` -- sizeof **96** (long long 4-byte aligned): see
       the _IO_file_doallocate proof in the header above; st_size@44, st_blksize@52, st_ino@88. */
    uint8_t b[96]; memset(b, 0, sizeof b);
    *(uint64_t *)(b + 0)  = (uint64_t)hs->st_dev;
    *(uint32_t *)(b + 12) = ino;                           /* legacy 32-bit __st_ino */
    *(uint32_t *)(b + 16) = (uint32_t)hs->st_mode;
    *(uint32_t *)(b + 20) = (uint32_t)(hs->st_nlink ? hs->st_nlink : 1);
    *(uint32_t *)(b + 24) = (uint32_t)hs->st_uid;
    *(uint32_t *)(b + 28) = (uint32_t)hs->st_gid;
    *(uint64_t *)(b + 32) = (uint64_t)hs->st_rdev;
    memcpy(b + 44, &sz, 8);                                /* st_size @44 (4-byte aligned) */
    *(uint32_t *)(b + 52) = 4096;                          /* st_blksize @52 */
    *(uint64_t *)(b + 56) = blk;                           /* st_blocks @56 */
    *(uint64_t *)(b + 88) = (uint64_t)ino;                 /* 64-bit st_ino @88 (32-bit-safe) */
    uc_mem_write(g_uc, gbuf, b, sizeof b);
}

/* Translate guest (Linux/ARM) open() flags to the host's. On Linux the guest IS the host, so
   it's identity. On Windows (MinGW) the flag BIT VALUES differ (e.g. Linux O_CREAT=0100 vs
   MinGW 0x100) AND a file MUST be opened O_BINARY or msvcrt opens it in text mode — translating
   CRLF and ending binary reads at the first 0x1A — which silently corrupts GP2X binary assets.
   That was the native-Windows black screen: assets "load" but the pixel data is garbage, so the
   game draws nothing into the framebuffer while its loop runs on. */
static int host_open_flags(int gf) {
#ifdef _WIN32
    enum { LO_WRONLY = 01, LO_RDWR = 02, LO_CREAT = 0100, LO_EXCL = 0200,
           LO_NOCTTY = 0400, LO_TRUNC = 01000, LO_APPEND = 02000 };
    int hf = gf & 03;                       /* access mode (0/1/2) is the same on both */
    if (gf & LO_CREAT)  hf |= O_CREAT;
    if (gf & LO_EXCL)   hf |= O_EXCL;
    if (gf & LO_TRUNC)  hf |= O_TRUNC;
    if (gf & LO_APPEND) hf |= O_APPEND;
    return hf | O_BINARY;                   /* GP2X files are all binary */
#else
    return gf;
#endif
}

/* Map a host errno to the Linux/ARM errno the guest expects. Values 1..34 are identical on
   Linux and MinGW; the higher ones differ (e.g. ENOSYS is 38 on Linux but 40 on MinGW), so a
   failed syscall returns the wrong code to the guest's glibc on Windows -> wrong control flow
   (e.g. it gives up on a file instead of reading it). Identity on Linux. */
static int linux_errno(int e) {
#ifdef _WIN32
    switch (e) {
    case EDEADLK:      return 35;
    case ENAMETOOLONG: return 36;
    case ENOLCK:       return 37;
    case ENOSYS:       return 38;
    case ENOTEMPTY:    return 39;
#ifdef ELOOP
    case ELOOP:        return 40;
#endif
    case EILSEQ:       return 84;
    default:           return e;   /* 1..34 + the common file errnos already match */
    }
#else
    return e;
#endif
}
#define LERR(e) (-(long)linux_errno(e))

/* In-memory fake files for the Linux /proc and /etc entries the guest's glibc reads but the
   host may not provide. On WSL these resolve to the real Linux host by accident; on Windows
   there is no /proc, so the open fails (-ENOENT) and the guest's init diverges/hangs. Serve
   canned content from a malloc buffer via a fake fd, host-independently (the old /proc/mounts
   path used mkstemp("/tmp/..."), which also fails on Windows). */
#define MEMFD_BASE 0x20000000
#define MEMFD_MAX  16
struct memfile { int used; char *data; uint32_t len, pos; };
static struct memfile g_memf[MEMFD_MAX];
static struct memfile *memfd_get(int fd) {
    int i = fd - MEMFD_BASE;
    return (i >= 0 && i < MEMFD_MAX && g_memf[i].used) ? &g_memf[i] : NULL;
}
static int memfd_make_bin(const void *s, uint32_t len) {
    for (int i = 0; i < MEMFD_MAX; i++) if (!g_memf[i].used) {
        g_memf[i].used = 1; g_memf[i].data = malloc(len ? len : 1);
        memcpy(g_memf[i].data, s, len); g_memf[i].len = len; g_memf[i].pos = 0;
        return MEMFD_BASE + i;
    }
    return -1;
}
static int memfd_make(const char *s) { return memfd_make_bin(s, (uint32_t)strlen(s)); }

/* ---- directory handles (getdents) -----------------------------------------
   A guest open() of a directory becomes a DIRFD: opendir() + a snapshot of all entries (name +
   type). getdents/getdents64 serve from the snapshot with a cursor (no telldir/seekdir, which
   MinGW lacks). Used by Caanoo Rhythmos scanning ./package/ for songs. */
#define DIRFD_BASE 0x30000000
#define DIRFD_MAX  16
struct dirhandle { int used, n, pos; char **name; unsigned char *type; };
static struct dirhandle g_dirf[DIRFD_MAX];
static struct dirhandle *dirfd_get(int fd) {
    int i = fd - DIRFD_BASE;
    return (i >= 0 && i < DIRFD_MAX && g_dirf[i].used) ? &g_dirf[i] : NULL;
}
/* Read `dir`'s entries into the handle, skipping names already present (so an earlier-scanned
   source shadows a later one). Returns 1 if the dir was opened, 0 otherwise. */
static int dir_scan_into(struct dirhandle *h, int *cap, const char *dir) {
    DIR *d = opendir(dir); if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        int dup = 0;
        for (int i = 0; i < h->n; i++) if (!strcmp(h->name[i], de->d_name)) { dup = 1; break; }
        if (dup) continue;
        if (h->n >= *cap) { *cap = *cap ? *cap * 2 : 32;
            h->name = realloc(h->name, *cap * sizeof *h->name);
            h->type = realloc(h->type, *cap * sizeof *h->type); }
        h->name[h->n] = strdup(de->d_name);
        unsigned char t = 0;            /* DT_UNKNOWN */
#ifdef DT_DIR
        t = de->d_type;
#endif
        if (t == 0) {                   /* d_type unavailable: stat the entry */
            char ep[PATH_MAX]; struct stat es;
            snprintf(ep, sizeof ep, "%s/%s", dir, de->d_name);
            if (stat(ep, &es) == 0) t = S_ISDIR(es.st_mode) ? 4 /*DT_DIR*/ : 8 /*DT_REG*/;
        }
        h->type[h->n] = t; h->n++;
    }
    closedir(d);
    return 1;
}
/* A directory fd that merges the per-game save overlay over the original assets: `overlay`
   entries are listed first and shadow same-named `orig` entries, so a game enumerating a save
   dir (rather than opening files by name) sees the files it saved. Either arg may be NULL. */
static int dirfd_make(const char *orig, const char *overlay) {
    int slot = -1;
    for (int i = 0; i < DIRFD_MAX; i++) if (!g_dirf[i].used) { slot = i; break; }
    if (slot < 0) return -1;
    struct dirhandle *h = &g_dirf[slot]; memset(h, 0, sizeof *h);
    int cap = 0, ok = 0;
    if (overlay) ok |= dir_scan_into(h, &cap, overlay);   /* overlay first -> it wins on dup names */
    if (orig)    ok |= dir_scan_into(h, &cap, orig);
    if (!ok) { for (int i = 0; i < h->n; i++) free(h->name[i]);
               free(h->name); free(h->type); memset(h, 0, sizeof *h); return -1; }
    h->used = 1; h->pos = 0;
    return DIRFD_BASE + slot;
}
static void dirfd_close(int fd) {
    struct dirhandle *h = dirfd_get(fd); if (!h) return;
    for (int i = 0; i < h->n; i++) free(h->name[i]);
    free(h->name); free(h->type); memset(h, 0, sizeof *h);
}
/* pack snapshot entries into the guest buffer; wide=1 -> linux_dirent64, else linux_dirent.
   returns bytes written (0 = end of directory). */
static long dir_getdents(int fd, uint32_t gbuf, uint32_t count, int wide) {
    struct dirhandle *h = dirfd_get(fd); if (!h) return -9 /*EBADF*/;
    uint32_t off = 0;
    while (h->pos < h->n) {
        const char *nm = h->name[h->pos]; int nlen = (int)strlen(nm);
        int reclen = wide ? ((19 + nlen + 1 + 7) & ~7)     /* d64: ino8 off8 reclen2 type1 name.. */
                          : ((10 + nlen + 1 + 1 + 3) & ~3); /* d:   ino4 off4 reclen2 name.. pad type@end */
        if (off + (uint32_t)reclen > count) break;          /* full: leave for the next call */
        uint8_t rec[320]; if (reclen > (int)sizeof rec) { h->pos++; continue; }
        memset(rec, 0, reclen);
        if (wide) {
            *(uint64_t *)(rec + 0)  = (uint64_t)(h->pos + 1);   /* d_ino */
            *(uint64_t *)(rec + 8)  = (uint64_t)(off + reclen); /* d_off */
            *(uint16_t *)(rec + 16) = (uint16_t)reclen;
            rec[18] = h->type[h->pos];                          /* d_type */
            memcpy(rec + 19, nm, nlen + 1);
        } else {
            *(uint32_t *)(rec + 0) = (uint32_t)(h->pos + 1);    /* d_ino */
            *(uint32_t *)(rec + 4) = (uint32_t)(off + reclen);  /* d_off */
            *(uint16_t *)(rec + 8) = (uint16_t)reclen;
            memcpy(rec + 10, nm, nlen + 1);
            rec[reclen - 1] = h->type[h->pos];                  /* d_type at record end */
        }
        uc_mem_write(g_uc, gbuf + off, rec, reclen);
        off += reclen; h->pos++;
    }
    return (long)off;
}

/* Track the real host fds we hand back to the guest from open()/openat(). The guest closes
   most of them, but a game that exits/reloads mid-load leaks the rest; over many hot reloads
   that exhausts the msvcrt/posix fd table. syscalls_reset closes any still open. */
#define HOSTFD_MAX 512
static int g_hostfd[HOSTFD_MAX]; static uint32_t g_hostfd_ino[HOSTFD_MAX]; static int g_nhostfd = 0;
/* A stable, file-unique synthetic inode from the host path (FNV-1a, forced nonzero). On Windows
   MinGW's fstat/stat report st_ino==0 for EVERY file; the guest ld.so dedups shared objects by
   (st_dev,st_ino), so identical inodes make it treat libc/libm/... as duplicates of the first lib
   and never map them -> "undefined symbol __ctype_tolower" at relocation. A path hash gives each
   distinct file a distinct inode (and the same file the same one). Identity-irrelevant on Linux,
   where the real inode is already unique -- we only substitute when st_ino==0. */
static uint32_t path_ino(const char *hp) {
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)hp; *p; p++) { h ^= *p; h *= 16777619u; }
    return h ? h : 1u;
}
static void hostfd_track(int fd, uint32_t ino) {
    if (fd < 0) return;
    for (int i = 0; i < g_nhostfd; i++) if (g_hostfd[i] < 0) { g_hostfd[i] = fd; g_hostfd_ino[i] = ino; return; }
    if (g_nhostfd < HOSTFD_MAX) { g_hostfd[g_nhostfd] = fd; g_hostfd_ino[g_nhostfd] = ino; g_nhostfd++; }
}
static uint32_t hostfd_ino(int fd) {
    for (int i = 0; i < g_nhostfd; i++) if (g_hostfd[i] == fd) return g_hostfd_ino[i];
    return 0;
}
static void hostfd_untrack(int fd) {
    for (int i = 0; i < g_nhostfd; i++) if (g_hostfd[i] == fd) { g_hostfd[i] = -1; return; }
}
/* Flush every tracked host file to disk (called on quit, and by sync()). Cannot reach buffers
   the guest's own glibc still holds in guest RAM -- only data already write()-en to the host. */
void syscalls_flush_all(void) {
    for (int i = 0; i < g_nhostfd; i++) if (g_hostfd[i] >= 0)
#ifdef _WIN32
        _commit(g_hostfd[i]);
#else
        fsync(g_hostfd[i]);
#endif
}

/* Between games: close leaked host fds + free the in-memory /proc-/etc fake files. */
void syscalls_reset(void) {
    for (int i = 0; i < g_nhostfd; i++) if (g_hostfd[i] >= 0) close(g_hostfd[i]);
    g_nhostfd = 0;
    for (int i = 0; i < MEMFD_MAX; i++)
        if (g_memf[i].used) { free(g_memf[i].data); g_memf[i].used = 0; g_memf[i].data = NULL; }
    mqueue_reset();
    netsock_reset();
    lf1000_reset();
}

/* Minimal TZif (v1) for UTC — the guest's glibc opens /etc/localtime; a host without it (no
   /proc/etc on Windows) returns ENOENT and the game's init gets stuck re-polling. */
static const unsigned char TZ_UTC[] = {
    'T','Z','i','f', 0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   /* magic, ver 1, 15 reserved */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,                  /* isutcnt isstdcnt leapcnt timecnt = 0 */
    0,0,0,1, 0,0,0,4,                                    /* typecnt=1 charcnt=4 */
    0,0,0,0, 0, 0,                                       /* ttinfo: utoff=0 isdst=0 abbrind=0 */
    'U','T','C', 0 };
/* Return a fake fd for a known Linux system path, or 0 if not one we fake. */
static int sysfile_open(const char *p) {
    /* Didj cartridge-detect sysfs: CButtonModule's LightningButtonTask reads this each poll and
       asserts ("cart read failed") if the read fails. "0" = no cartridge inserted (base UI). */
    if (!strcmp(p, "/sys/devices/platform/lf1000-nand/cartridge"))
        return memfd_make("0\n");
    if (!strcmp(p, "/proc/sys/kernel/version"))
        return memfd_make("#1 PREEMPT Mon Jan 1 00:00:00 UTC 2008\n");
    if (!strcmp(p, "/proc/sys/kernel/osrelease") || !strcmp(p, "/proc/version"))
        return memfd_make("2.6.32\n");
    if (!strcmp(p, "/proc/mounts") || !strcmp(p, "/etc/mtab")) {
        /* Include a /dev/shm tmpfs: glibc's shm_open() parses this for the POSIX shm directory;
           without it __shm_directory() returns "" and shm_open(open '') fails -> the fake-SDL /
           fake-GLES shims can't map the /dev/shm framebuffer (no rendering).
           Show /mnt/sd mounted so the firmware menu lists games instead of "Insert SD Card" -- it
           greps /proc/mounts by SD DEVICE NAME, which differs per device: Caanoo = /dev/mmcblk0p1,
           GP2X/Wiz = /dev/mmcsd/disc0/part1. /mnt/sd maps to the host games dir (me_mount_resolve). */
        static char mb[512];
        snprintf(mb, sizeof mb,
                 "/dev/root / ext2 rw 0 0\nnone /proc proc rw 0 0\n"
                 "none /tmp tmpfs rw 0 0\nnone /dev/shm tmpfs rw 0 0\n"
                 "%s /mnt/sd vfat rw 0 0\n",
                 me_model()->sd_devnode);
        return memfd_make(mb);
    }
    if (!strcmp(p, "/etc/localtime"))
        return memfd_make_bin(TZ_UTC, sizeof TZ_UTC);
    return 0;
}

long sys_dispatch(uint32_t nr, uint32_t a0, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a5;
    /* ME_TRACE_AFTER: enable the syscall trace only AFTER the guest has rendered (frame_seq), to
       capture the input loop without the slow asset-load init drowning it. ~20k syscalls then off. */
    if (getenv("ME_TRACE_AFTER")) { static long lc = -1;
        if (lc < 0 && g_shm && g_shm->frame_seq > 60) lc = 0;
        if (lc >= 0) { g_trace = (lc < 20000); lc++; } }
    /* ME_TRACE_FROMFRAME=N: turn the (uncapped) syscall trace on once frame_seq >= N -- to capture a
       late-running busy loop (e.g. a freeze) without the boot syscalls exhausting a budget first. */
    if (getenv("ME_TRACE_FROMFRAME")) { static int armed = -1;
        if (armed < 0) armed = atoi(getenv("ME_TRACE_FROMFRAME"));
        if (g_shm && (int)g_shm->frame_seq >= armed) g_trace = 1; }
    if (g_trace)
        fprintf(stderr, "  [t%d] sc %u pc=%08x (%08x,%08x,%08x,%08x)\n",
                g_self ? g_self->tid : -1, nr, g_self ? g_self->last_pc : 0, a0, a1, a2, a3);
    /* refresh fb periodically — only until the game drives present via OADR (frame-synced) */
    { static unsigned c = 0; if (g_fb_guest && !g_oadr_driven && (++c & 63) == 0) present_active(); }
    switch (nr) {
    case 1:    /* exit */
    case 248:  /* exit_group */
        if (g_forked) {  /* the synchronous fork child is done -> restore parent */
            for (int i = 0; i < g_nsnap; i++) {
                uc_mem_write(g_uc, g_snap[i].begin, g_snap[i].data, g_snap[i].len);
                free(g_snap[i].data);
            }
            g_nsnap = 0;
            uc_context_restore(g_uc, g_fork_ctx);
            uc_context_free(g_fork_ctx); g_fork_ctx = NULL;
            g_forked = 0;
            memcpy(g_sigact, g_sigact_fork, sizeof g_sigact);   /* undo the child's handler resets */
            g_self->sig_blocked = g_fork_sigblocked;
            if (g_trace) fprintf(stderr, "  [fork] child exited(%u) -> resume parent\n", a0);
            return g_child_pid;  /* parent's fork() now returns the child pid */
        }
        /* A NON-main thread terminating (exit, or glibc 2.3.6's exit_group-first _exit)
           ends only that host thread — on real GP2X each LinuxThreads thread is its own
           group, so a worker's _exit (e.g. the AMA audio worker finishing a song) must not
           kill the game. uc_emu_stop returns from uc_emu_start -> thread_entry tears down
           (clears ctid, futex-wakes joiners). The MAIN thread's exit/exit_group quits. */
        if (g_self != &g_th[0]) {
            if (g_trace) fprintf(stderr, "  [thread %d exit]\n", g_self->tid);
            g_self->state = TH_DEAD;
            uc_emu_stop(g_uc);
            g_setpc = 1;
            return 0;
        }
        if (g_trace) fprintf(stderr, "  [REAL EXIT] code=%u nr=%u\n", a0, nr);
        g_exit = 1; g_exit_code = a0; uc_emu_stop(g_uc); return 0;
    case 4: {  /* write(fd, buf, count) */
        if ((int)a0 == FAKESOCK_FD) return a2;   /* syslog write to its socket: discard */
        uint8_t *tmp = malloc(a2 ? a2 : 1);
        uc_mem_read(g_uc, a1, tmp, a2);
        if ((int)a0 == PIPEFD_W) { pipe_put(tmp, a2); free(tmp); return a2; }
        if (dev_type((int)a0) == DEV_DSP) { free(tmp);
            int nb = dev_nonblock((int)a0);
            long r = dsp_write(a1, a2, nb);
            if (r == -11) return r;        /* EAGAIN on a full non-blocking ring: don't pace */
            uint32_t us = dsp_pace_us();   /* pace like a blocking OSS write (frees the mixer
                                              mutex + CPU; else the audio thread free-runs) */
            if (us) { if (us > 100000) us = 100000;
                      BIGLOCK_UNLOCK(); usleep(us); BIGLOCK_LOCK(); }
            return r; }
        if (dev_type((int)a0)) { free(tmp); return a2; }  /* other devices: accept + discard */
        /* route guest stdout/stderr through the C streams so they follow ME_LOGFILE's freopen
           (on the -mwindows bundle a raw write(2,..) doesn't reach the redirected stderr, so
           guest FAKEGLES_LOG/printf output was being lost). */
        if ((int)a0 == 1 || (int)a0 == 2) {
            if (me_report_ingest_guest((const char *)tmp, a2)) { free(tmp); return a2; }  /* shim
                                                  report line: recorded, never echoed to the log */
            me_report_scan_write((int)a0, (const char *)tmp, a2);  /* catch ld.so symbol/lib
                                                                      errors + glibc aborts */
            FILE *s = g_log ? g_log : ((int)a0 == 1 ? stdout : stderr);
            if (a2) { fwrite(tmp, 1, a2, s); fflush(s); }
            free(tmp);
            return (long)a2;   /* always claim the whole write landed: if the sink is a dead handle
                                  (no console on a double-click, no ME_LOGFILE) a short/0 return made
                                  the guest's glibc stdio spin retrying -> menu hung -> black screen */
        }
        long r = write((int)a0, tmp, a2); free(tmp);
        return r < 0 ? -errno : r;
    }
    case 3: {  /* read(fd, buf, count) */
        if ((int)a0 == PIPEFD_R) {  /* drain the forked child's pipe output */
            uint32_t avail = g_pipe_w - g_pipe_r, n = a2 < avail ? a2 : avail;
            if (n) uc_mem_write(g_uc, a1, g_pipebuf + g_pipe_r, n);
            g_pipe_r += n; return n;   /* 0 == EOF (child finished) */
        }
        struct memfile *mf = memfd_get((int)a0);
        if (mf) { uint32_t n = mf->len - mf->pos; if (n > a2) n = a2;
                  if (n) uc_mem_write(g_uc, a1, mf->data + mf->pos, n);
                  mf->pos += n; return n; }
        if (dev_type((int)a0) == DEV_I2C)  return i2c_read(a1, a2);  /* handset serial */
        if (dev_type((int)a0) == DEV_GPIO) return gpio_read(a1, a2); /* joystick buttons */
        if (dev_type((int)a0) == DEV_INPUT_EV || dev_type((int)a0) == DEV_INPUT_JS) {
            long r = input_read((int)a0, a1, a2);                   /* evdev/js stick + buttons */
            if (r != -11 || dev_nonblock((int)a0)) return r;        /* got events / error / non-blocking EAGAIN */
            /* Blocking evdev read (input_read returned -EAGAIN = no events): a real input device
               blocks until an event. The Didj button task (LightningButtonTask) does a bare blocking
               read and asserts "button read failed" on EAGAIN, so wait (releasing g_biglock) for the
               next button state change. */
            while (!g_shutdown && !g_exit) {
                BIGLOCK_UNLOCK(); me_usleep(8000); BIGLOCK_LOCK();
                if (input_pending((int)a0)) { r = input_read((int)a0, a1, a2); if (r != -11) return r; }
            }
            return -4 /*EINTR*/;
        }
        if (dev_type((int)a0)) return 0;   /* other stub devices: EOF (never host-read a fake fd) */
        uint8_t *tmp = malloc(a2 ? a2 : 1);
        long r = read((int)a0, tmp, a2);
        if (r > 0) uc_mem_write(g_uc, a1, tmp, r);
        free(tmp); return r < 0 ? -errno : r;
    }
    case 45: { /* brk(addr) */
        if (a0 == 0) return g_brk;
        uint32_t na = ALIGN_UP(a0);
        /* RWX, like do_mmap: the brk heap is EXECUTABLE on the Caanoo (ARMv5 Linux, no XN). GPAC's
           in-heap code allocator (CodeAlloc/CodeUnlock in Rhythmos) malloc's a small block, writes a
           code thunk into it, and blx's it; a R/W-only heap page faulted the fetch (a protection
           fault mem_invalid_cb doesn't catch -> the decoder thread died). */
        if (na > ALIGN_UP(g_brk))
            map_region(ALIGN_UP(g_brk), na - ALIGN_UP(g_brk),
                       UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC);
        g_brk = a0;
        return g_brk;
    }
    case 90: { /* old_mmap(ptr->{addr,len,prot,flags,fd,offset_bytes}) */
        uint32_t m[6]; uc_mem_read(g_uc, a0, m, sizeof m);
        int fd = (m[4] == 0xffffffffu) ? -1 : (int)m[4], t = dev_type(fd);
        if (t == DEV_SHMFB) return shmfb_mmap(m[1]);
        if (t) { long r = dev_mmap(t, m[0], m[1], m[3], m[5]);
                 if (t == DEV_LF1000_LAYER && r > 0) lf1000_track_mmap(fd, (uint32_t)r, m[1]);
                 return r; }
        return do_mmap(m[0], m[1], m[3], fd, m[5]);
    }
    case 192: { /* mmap2: a4=fd, a5=pgoff (4096 units) */
        int fd = (a4 == 0xffffffffu) ? -1 : (int)a4, t = dev_type(fd);
        if (t == DEV_SHMFB) return shmfb_mmap(a1);
        if (t) { long r = dev_mmap(t, a0, a1, a3, (uint32_t)(a5 * 4096));
                 if (t == DEV_LF1000_LAYER && r > 0) lf1000_track_mmap(fd, (uint32_t)r, a1);
                 return r; }
        long r = do_mmap(a0, a1, a3, fd, (uint64_t)a5 * 4096);
        if (fd >= 0 && getenv("ME_MMAPLOG")) { /* file-backed map: print base+len+fd(+path) for the lib layout */
            char lp[256] = "?";
#ifndef _WIN32                                 /* /proc/self/fd readlink is a Linux-only triage aid */
            char pf[64]; snprintf(pf, sizeof pf, "/proc/self/fd/%d", fd);
            ssize_t ln = readlink(pf, lp, sizeof lp - 1); if (ln >= 0) lp[ln] = 0; else lp[0] = '?', lp[1] = 0;
#endif
            fprintf(stderr, "  [mmap] fd=%d off=%llx len=%x prot=%x -> %08lx  %s\n",
                    fd, (unsigned long long)a5 * 4096, a1, a3, r, lp);
        }
        return r;
    }
    case 91: { /* munmap(addr, len) — recycle via the free-list rather than uc_mem_unmap,
                  which flushes the JIT cache. Real-unmap only if the list overflows. */
        uint32_t a = ALIGN_DN(a0), l = ALIGN_UP(a1);
        if (l) { if (g_nmfree < 256) g_mfree[g_nmfree++] = (struct freereg){a, l};
                 else { extern unsigned long g_uc_unmap; g_uc_unmap++; uc_mem_unmap(g_uc, a, l); } }
        return 0;
    }
    case 2: { /* fork.
        Default: DON'T run the child inline. The inline child shares the engine with the
        still-running parent threads, so snapshotting guest memory at fork time and restoring
        it on child exit clobbers a FILE/lock a parent thread initialised in that window ->
        a zeroed FILE in glibc stdio -> crash entering a level (the documented symptom). The
        only forks here are system("sh ...") device-setup that no-ops on PC, so just return a
        child pid: the guest takes the parent path, the child never executes, and waitpid reaps
        it (status 0). This also makes the old g_sigact-leak machinery moot (no child = no
        pre-exec handler reset). ME_GP2X_FORKCHILD restores the old inline-child behaviour. */
        if (!getenv("ME_GP2X_FORKCHILD")) {
            if (g_trace) fprintf(stderr, "  [fork] no inline child -> return pid %u\n", g_child_pid);
            return g_child_pid;
        }
        if (uc_context_alloc(g_uc, &g_fork_ctx) != UC_ERR_OK) return -ENOMEM;
        uc_context_save(g_uc, g_fork_ctx);
        uc_mem_region *regs = NULL; uint32_t cnt = 0; g_nsnap = 0;
        /* ME_GP2X_FORKNOMEM: skip the guest-memory snapshot/restore. The full-memory restore
           clobbers writes that PARENT threads made while the inline child ran (they keep
           executing) -> can wipe a mutex/wait-queue a parent thread acquired -> main waits
           forever. The child only execs our exit-stub, so its memory changes are ~negligible. */
        if (!getenv("ME_GP2X_FORKNOMEM") && uc_mem_regions(g_uc, &regs, &cnt) == UC_ERR_OK) {
            for (uint32_t i = 0; i < cnt && g_nsnap < 2048; i++) {
                uint64_t b = regs[i].begin;
                uint32_t l = (uint32_t)(regs[i].end - regs[i].begin + 1);
                uint8_t *d = malloc(l);
                if (!d || uc_mem_read(g_uc, b, d, l) != UC_ERR_OK) { free(d); continue; }
                g_snap[g_nsnap].begin = b; g_snap[g_nsnap].len = l;
                g_snap[g_nsnap].data = d; g_nsnap++;
            }
            uc_free(regs);
        }
        memcpy(g_sigact_fork, g_sigact, sizeof g_sigact);   /* child resets handlers pre-exec */
        g_fork_sigblocked = g_self->sig_blocked;
        g_fork_thread = g_self;
        g_forked = 1;
        if (g_trace) fprintf(stderr, "  [fork] snapshot %d regions; child runs first\n", g_nsnap);
        return 0;  /* child sees fork()==0 */
    }
    case 42: { /* pipe(fds[2]) -> our in-engine pipe */
        g_pipe_r = g_pipe_w = 0;
        uint32_t fds[2] = { PIPEFD_R, PIPEFD_W };
        uc_mem_write(g_uc, a0, fds, 8); return 0;
    }
    case 7: case 114: /* waitpid/wait4: the synchronous child already exited */
        if (a1) { uint32_t z = 0; uc_mem_write(g_uc, a1, &z, 4); }
        return g_child_pid;
    case 11: { /* execve(path, argv, envp). Matches the qemu backend's gp2x_execve_noop:
                  GP2X games shell out (/bin/sh) for best-effort device tweaks and insmod
                  kernel modules that don't exist on PC. Letting the exec fail (-ENOSYS) ran
                  glibc's exec-failed cleanup + _exit(127) inside our snapshot/restore fork
                  and left the parent inconsistent -> a later null-deref. So a forked child
                  exec'ing sh/insmod just exits(0) cleanly (system() then returns 0). Real
                  ELF chain-loads are unsupported here. */
        char ep[1024]; read_cstr(a0, ep, sizeof ep);
        const char *base = strrchr(ep, '/'); base = base ? base + 1 : ep;
        if (g_forked && (!strcmp(base, "sh") || !strcmp(base, "insmod"))) {
            for (int i = 0; i < g_nsnap; i++) {
                uc_mem_write(g_uc, g_snap[i].begin, g_snap[i].data, g_snap[i].len);
                free(g_snap[i].data);
            }
            g_nsnap = 0;
            uc_context_restore(g_uc, g_fork_ctx);
            uc_context_free(g_fork_ctx); g_fork_ctx = NULL;
            g_forked = 0;
            memcpy(g_sigact, g_sigact_fork, sizeof g_sigact);   /* undo the child's handler resets */
            g_self->sig_blocked = g_fork_sigblocked;
            if (g_trace) fprintf(stderr, "  [fork] child execve %s -> exit(0)\n", base);
            return g_child_pid;
        }
        /* A non-fork execve chain-loads a new binary into our single process. Two cases:
           (1) the GPEComp self-extractor exec'ing its decompressed temp (/mnt/tmp -> host scratch);
           (2) firmware mode: gp2xmenu exec'ing a selected game (/mnt/sd/.../foo.gpe) or a game
               exec'ing /usr/gp2x/gp2xmenu to return to the launcher. resolve_path() handles all of
               them (SD/NAND mount, rootfs, /mnt/tmp redirect). The main loop runs the target
               through resolve_input() (GPEComp decompress / launcher-script follow) on reload. */
        if (!g_forked) {
            char rp[PATH_MAX]; resolve_path(ep, rp, sizeof rp);
            struct stat es;
            if (stat(rp, &es) != 0) {   /* garbage/missing target (e.g. a game relaunching itself
                                           with a path built from stubbed getcwd/readlink): fail the
                                           execve so the game keeps running -- don't tear down to idle. */
                if (g_trace) fprintf(stderr, "  [execve] target '%s' (guest '%s') missing -> ENOENT\n", rp, ep);
                return LERR(ENOENT);
            }
            /* Run the target through the same resolver the CLI + File->Open use, so a GPEComp
               .gpe launched from gp2xmenu decompresses (and a launcher script is followed).
               Idempotent on a plain ELF / an already-decompressed temp. */
            char fin[PATH_MAX]; const char *r = resolve_input(rp, fin, sizeof fin);
            if (!r) return LERR(ENOENT);
            if (g_trace) fprintf(stderr, "  [execve] reload -> %s (guest '%s')\n", fin, ep);
            /* stop EVERY thread (gp2xmenu may exec the game off a worker thread), record the
               target, and let the main loop run the reset+load. */
            g_setpc = 1;
            engine_reload_in_syscall(fin);
            return 0;
        }
        return LERR(ENOSYS);
    }
    case 15:   /* chmod */
    case 94:   /* fchmod: the GPEComp stub +x's its temp; we load it via fopen, so just accept */
        return 0;
    case 140: { /* _llseek(fd, off_hi, off_lo, result64*, whence) */
        int64_t off = ((int64_t)(uint32_t)a1 << 32) | (uint32_t)a2;
        struct memfile *mf = memfd_get((int)a0);
        if (mf) { uint32_t base = ((int)a4 == 1) ? mf->pos : ((int)a4 == 2) ? mf->len : 0;
                  mf->pos = base + (uint32_t)off; if (mf->pos > mf->len) mf->pos = mf->len;
                  uint64_t ru = mf->pos; if (a3) uc_mem_write(g_uc, a3, &ru, 8); return 0; }
        off_t r = lseek((int)a0, (off_t)off, (int)a4);
        if (r == (off_t)-1) return LERR(errno);
        uint64_t ru = (uint64_t)r;
        if (a3) uc_mem_write(g_uc, a3, &ru, 8);
        return 0;
    }
    case 13: { /* time(t) */
        uint32_t t = (uint32_t)time(NULL);
        if (a0) uc_mem_write(g_uc, a0, &t, 4); return t;
    }
    case 99: case 100: { /* statfs/fstatfs: report a roomy tmpfs */
        uint8_t b[64]; memset(b, 0, sizeof b);
        *(uint32_t *)(b + 0)  = 0x01021994;  /* f_type = TMPFS_MAGIC -- glibc shm_open statfs's
                                                /dev/shm and rejects it unless it's tmpfs/ramfs;
                                                without this the fake-SDL shim's shm_open fails
                                                -> no shm framebuffer/audio/input. */
        *(uint32_t *)(b + 4)  = 4096;        /* f_bsize   */
        *(uint32_t *)(b + 8)  = 0x00100000;  /* f_blocks  */
        *(uint32_t *)(b + 12) = 0x00080000;  /* f_bfree   */
        *(uint32_t *)(b + 16) = 0x00080000;  /* f_bavail  */
        *(uint32_t *)(b + 36) = 255;         /* f_namelen */
        if (a1) uc_mem_write(g_uc, a1, b, sizeof b); return 0;
    }
    case 24: case 47: case 49: case 50:       /* getuid/getgid/geteuid/getegid */
    case 199: case 200: case 201: case 202:   /* ...32 variants */
        return 0;
    case 75:   return 0;        /* setrlimit */
    /* Credential setters: GP2X firmware runs everything as root, so a game that drops/restores
       privileges succeeds trivially. Without this, angband/kq/rogue abort at start with
       "setegid(): cannot drop permissions correctly!". Whole set*id family -> 0. */
    case 23:  case 46:  case 70:  case 71:  case 138: case 139: case 164: case 170:  /* set*id    */
    case 203: case 204: case 208: case 210: case 213: case 214: case 215: case 216:  /* ...32     */
    case 81:  case 206:         /* setgroups / setgroups32 */
    case 16:  case 95:  case 182: case 198: case 207: case 212:  /* (l/f)chown + ...32: as root, ok */
    case 60:   return 0;        /* umask (report 0 as the prior mask -- harmless) */
    case 39: { /* mkdir(path, mode): game save/config dirs land in the per-game overlay */
        char p[1024]; read_cstr(a0, p, sizeof p);
        char hp[PATH_MAX]; resolve_io(p, 1, hp, sizeof hp);
        int r = ME_MKDIR(hp); (void)a1;
        return (r == 0 || errno == EEXIST) ? 0 : LERR(errno);
    }
    case 40: { /* rmdir(path) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        char hp[PATH_MAX]; resolve_io(p, 1, hp, sizeof hp);
        return rmdir(hp) == 0 ? 0 : LERR(errno);
    }
    case 10: { /* unlink(path): games delete stale temp/lock/save files before recreating them */
        char p[1024]; read_cstr(a0, p, sizeof p);
        if (!strncmp(p, "/dev/", 5)) return 0;
        char hp[PATH_MAX]; resolve_io(p, 1, hp, sizeof hp);
        return remove(hp) == 0 ? 0 : LERR(errno);
    }
    case 38: { /* rename(oldpath, newpath): atomic-save pattern (write tmp, rename over final) */
        char op[1024], np[1024]; read_cstr(a0, op, sizeof op); read_cstr(a1, np, sizeof np);
        char ohp[PATH_MAX], nhp[PATH_MAX];
        resolve_io(op, 1, ohp, sizeof ohp);
        resolve_io(np, 1, nhp, sizeof nhp);
        return rename(ohp, nhp) == 0 ? 0 : LERR(errno);
    }
    case 41: { int r = dup((int)a0);            return r < 0 ? LERR(errno) : r; }  /* dup  */
    case 63: { int r = dup2((int)a0, (int)a1);  return r < 0 ? LERR(errno) : r; }  /* dup2 */
    /* benign no-ops: nothing the engine caches to a guest FS / nothing to schedule. Returning
       success keeps these off the UNIMPLEMENTED log (Vektar calls sync; others appear in titles
       that otherwise spam ENOSYS) without changing behaviour. */
    /* Durability: flush real host fds for real (skip device/memfd/dirfd fds -- not host files).
       A normal quit already persists written bytes via the OS page cache; this honours a game's
       explicit fsync so the data is also safe against a hard host-kill. */
    case 118:  /* fsync */
    case 148:  /* fdatasync */
    case 314:  /* sync_file_range(fd, ...): treat as an fsync of the fd */
        if (dev_type((int)a0) || (int)a0 >= MEMFD_BASE || dirfd_get((int)a0)) return 0;
#ifdef _WIN32
        return _commit((int)a0) == 0 ? 0 : LERR(errno);
#else
        return (nr == 148 ? fdatasync((int)a0) : fsync((int)a0)) == 0 ? 0 : LERR(errno);
#endif
    case 36:   syscalls_flush_all(); return 0;   /* sync: flush every host file we track */
    case 34:   return 0;        /* nice */
    case 55:                    /* fcntl  (F_GETFL/F_SETFL/F_SETFD on device/normal fds) */
        /* Record O_NONBLOCK on a device fd: PortAudio's OSS backend (Didj) sets it on /dev/dsp
           and primes the output buffer by writing until EAGAIN; honouring it in dsp_write ends
           the prime loop (else the audio thread spins forever and its parent deadlocks). */
        if (a1 == 4 /*F_SETFL*/ && dev_type((int)a0))
            dev_set_nonblock((int)a0, (a2 & 0x800u /*O_NONBLOCK*/) ? 1 : 0);
        return 0;
    case 221:  return 0;        /* fcntl64 — accept (we don't honour O_NONBLOCK; harmless here) */
    case 141:  return dir_getdents((int)a0, a1, a2, 0);  /* getdents   (linux_dirent)   */
    case 217:  return dir_getdents((int)a0, a1, a2, 1);  /* getdents64 (linux_dirent64) */
    case 156:  return 0;        /* sched_setscheduler (GP2X games bump their audio thread's prio) */
    case 12: {  /* chdir(path) -- some games cd before opening assets (Blazar); gp2xmenu cd's into
                   the game dir under /mnt/sd before exec'ing it */
        char p[1024]; read_cstr(a0, p, sizeof p);
        char rp[PATH_MAX]; resolve_path(p, rp, sizeof rp);
        return chdir(rp) == 0 ? 0 : LERR(errno);
    }
    case 183: {  /* getcwd(buf, size) -> bytes incl NUL, or -ERANGE */
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) return LERR(errno);
        size_t l = strlen(cwd) + 1;
        if (l > a1) return LERR(ERANGE);
        uc_mem_write(g_uc, a0, cwd, (uint32_t)l);
        return (long)l;
    }
    case 149:  return LERR(ENOSYS);  /* _sysctl (glibc tolerates) */
    case 122: { /* uname -> minimal Linux/armv5tel 2.6.32 (>= eglibc 2.11 min-kernel for EABI titles) */
        char u[6 * 65]; memset(u, 0, sizeof u);
        strcpy(u + 0 * 65, "Linux"); strcpy(u + 2 * 65, "2.6.32");
        strcpy(u + 3 * 65, "#1"); strcpy(u + 4 * 65, "armv5tel");
        uc_mem_write(g_uc, a0, u, sizeof u); return 0;
    }
    case 54:   /* ioctl */ {
        int t = dev_type((int)a0);
        if (t == DEV_DSP) return dsp_ioctl(a1, a2);
        if (t == DEV_FB)  return fb_ioctl((int)a0, a1, a2);
        if (t == DEV_GPIO) return gpio_ioctl(a1, a2);   /* GPH SDL_OpenGPIO button-query ioctls */
        if (t == DEV_INPUT_EV || t == DEV_INPUT_JS) return input_ioctl((int)a0, a1, a2);
        if (t == DEV_I2C) return i2c_ioctl(a1, a2);
        if (t == DEV_LF1000_DPC || t == DEV_LF1000_MLC || t == DEV_LF1000_LAYER || t == DEV_LF1000_GA3D)
            return lf1000_ioctl((int)a0, a1, a2);
        return 0;
    }
    case 0xf0005: { /* __ARM_NR_set_tls -> kuser TLS slot */
        uc_mem_write(g_uc, 0xffff0ff0u, &a0, 4); return 0;
    }
    case 0xf0002: /* __ARM_NR_cacheflush(start, end, flags); r3 = base of the buffer the game
                     just rendered. Double-buffered titles (Payback) flip via this, not OADR. */
        gp2x_cacheflush(a3);
        return 0;
    /* GL render offload: the fake-GLES shim forwards draws here; the engine rasterizes natively. */
    case ME_NR_GL_RESIZE:  glr_resize((int)a0, (int)a1); return 0;
    case ME_NR_GL_CLEAR:   glr_clear(a0);  return 0;
    case ME_NR_GL_DRAW:    glr_draw(a0);   return 0;
    case ME_NR_GL_PRESENT: glr_present();  return 0;
    case 5: {  /* open(path, flags, mode) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        int d = dev_open(p); if (d >= 0) return d;
        if (g_trace) fprintf(stderr, "  open '%s' flags=%x\n", p, (int)a1);
        /* Never expose the host's ld.so.cache/preload: me_rootfs_resolve already skips them to
           force a clean LD_LIBRARY_PATH search, but resolve_io would otherwise fall back to the
           HOST file. Leaking the host's glibc cache here makes uClibc's _dl_map_cache (Didj)
           mmap+munmap a corrupt cache, and that path clobbered ld.so's GOT pointer (sl) on return
           -> a fault before any NEEDED lib was opened. ENOENT makes the linker skip the cache. */
        if (!strcmp(p, "/etc/ld.so.cache") || !strcmp(p, "/etc/ld.so.preload")) return -ENOENT;
        if (getenv("ME_NOMOUNTS") && (!strcmp(p, "/proc/mounts") || !strcmp(p, "/etc/mtab")))
            return -ENOENT;   /* test: make setmntent() fail cleanly instead of feeding getmntent */
        /* Linux /proc + /etc files glibc reads: serve canned content host-independently. The
           game's getmntent() also can't take the HOST mount table (WSL/drvfs has dozens of long
           entries that overrun its parser -> null-deref); the GP2X-like table in sysfile_open
           replaces it. */
        { int mf = sysfile_open(p); if (mf) { if (g_trace) fprintf(stderr, "  [fake %s]\n", p);
                                              return mf; } }
        char hp[PATH_MAX]; resolve_io(p, open_is_write((int)a1), hp, sizeof hp);
        /* Directory -> a DIRFD (portable opendir; a host open() of a dir works on Linux but not
           on MinGW, and we serve getdents from it), merging the save overlay over the assets. */
        { char ov[PATH_MAX]; struct stat ds, os;
          int has_ov  = save_overlay_path(p, ov, sizeof ov);
          int orig_d  = (stat(hp, &ds) == 0 && S_ISDIR(ds.st_mode));
          int ov_d    = has_ov && (stat(ov, &os) == 0 && S_ISDIR(os.st_mode));
          if (orig_d || ov_d) {
              int dfd = dirfd_make(orig_d ? hp : NULL, ov_d ? ov : NULL);
              if (g_trace) fprintf(stderr, "  open dir '%s' -> %d (overlay=%d)\n", p, dfd, ov_d);
              if (dfd >= 0) return dfd; } }
        long r = open(hp, host_open_flags((int)a1), a2); int e2 = errno;
        if (getenv("ME_OPENLOG")) { char b[1300]; snprintf(b, sizeof b,
            "OPEN '%s' [%s] flags=%x -> %ld%s\n", p, hp, (int)a1, r, r < 0 ? " FAIL" : ""); fputs(b, stderr); }
        if (r >= 0) { g_self->enoent_streak = 0; hostfd_track((int)r, path_ino(hp)); return r; }
        /* a worker tight-looping on missing files (the music worker on absent *.ama):
           back it off with a real sleep so it doesn't spin hot. */
        if (e2 == ENOENT && ++g_self->enoent_streak > 3) {
            g_self->enoent_streak = 0;
            BIGLOCK_UNLOCK();
            usleep(50000);
            BIGLOCK_LOCK();
        }
        return LERR(e2);
    }
    case 322: { /* openat(dirfd, path, flags, mode) */
        char p[1024]; read_cstr(a1, p, sizeof p);
        int d = dev_open(p); if (d >= 0) return d;
        int mf = sysfile_open(p); if (mf) return mf;
        char hp[PATH_MAX]; resolve_io(p, open_is_write((int)a2), hp, sizeof hp);
        { char ov[PATH_MAX]; struct stat ds, os;
          int has_ov = save_overlay_path(p, ov, sizeof ov);
          int orig_d = (stat(hp, &ds) == 0 && S_ISDIR(ds.st_mode));
          int ov_d   = has_ov && (stat(ov, &os) == 0 && S_ISDIR(os.st_mode));
          if (orig_d || ov_d) {
              int dfd = dirfd_make(orig_d ? hp : NULL, ov_d ? ov : NULL);
              if (dfd >= 0) return dfd; } }
        long r = open(hp, host_open_flags((int)a2), a3);
        if (r >= 0) { hostfd_track((int)r, path_ino(hp)); return r; }
        return LERR(errno);
    }
    case 6:    /* close */
        if ((int)a0 == PIPEFD_R || (int)a0 == PIPEFD_W || (int)a0 == FAKESOCK_FD) return 0;
        if (mq_is_fd((int)a0)) return 0;   /* mq descriptor: queue persists until mq_unlink */
        if (sock_is_fake((int)a0)) { sock_close_fake((int)a0); return 0; }
        if (dirfd_get((int)a0)) { dirfd_close((int)a0); return 0; }
        { struct memfile *mf = memfd_get((int)a0);
          if (mf) { free(mf->data); mf->used = 0; mf->data = NULL; return 0; } }
        if (dev_type((int)a0)) { dev_close((int)a0); return 0; }  /* free the device slot */
        hostfd_untrack((int)a0);
        return close((int)a0) < 0 ? LERR(errno) : 0;
    case 19: { /* lseek */
        struct dirhandle *dh = dirfd_get((int)a0);
        if (dh) { if ((int)a2 == 0 && a1 == 0) dh->pos = 0; return 0; }   /* rewinddir */
        struct memfile *mf = memfd_get((int)a0);
        if (mf) { uint32_t base = ((int)a2 == 1) ? mf->pos : ((int)a2 == 2) ? mf->len : 0;
                  mf->pos = base + a1; if (mf->pos > mf->len) mf->pos = mf->len; return mf->pos; }
        long r = lseek((int)a0, (off_t)a1, (int)a2); return r < 0 ? LERR(errno) : r; }
    case 93:   /* ftruncate(fd, len): the shim ftruncates the gp2x_fb shm (a device fd) -> accept;
                  a real host fd is truncated for real. */
        if (dev_type((int)a0) || (int)a0 >= MEMFD_BASE) return 0;
        return ftruncate((int)a0, (off_t)a1) == 0 ? 0 : LERR(errno);
    case 194:  /* ftruncate64(fd, len_lo, len_hi): EABI shim sizing the shm; high word unused here. */
        if (dev_type((int)a0) || (int)a0 >= MEMFD_BASE) return 0;
        return ftruncate((int)a0, (off_t)a1) == 0 ? 0 : LERR(errno);
    case 33: {  /* access(path, mode): exists? (ld.so/glibc probe libs + locale dirs) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        if (!strncmp(p, "/dev/", 5)) return 0;          /* devices always "exist" */
        char hp[PATH_MAX]; resolve_io(p, 0, hp, sizeof hp);
        struct stat s; return stat(hp, &s) == 0 ? 0 : LERR(ENOENT);
    }
    case 85: {  /* readlink(path, buf, bufsiz): we don't expose host symlinks; report "not a
                   symlink" so glibc path-canonicalisation falls back to the literal path. */
        (void)a1; (void)a2; return LERR(EINVAL);
    }
    case 263:   /* clock_gettime(clk, ts) */
    case 266: { /* clock_gettime64 */
        struct timeval tv; gettimeofday(&tv, NULL);
        uint32_t ts[2] = { (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec * 1000 };
        if (a1) uc_mem_write(g_uc, a1, ts, 8);
        return 0;
    }
    case 186:  return 0;  /* sigaltstack (libpthread sets one; we run handlers on the guest stack) */
    case 125:  return 0;  /* mprotect (we map RWX) */
    case 20:   return g_self->tid;  /* getpid (LinuxThreads: 1 pid per thread) */
    case 224:  return g_self->tid;  /* gettid */
    case 64:   return g_self->ppid;  /* getppid (LinuxThreads orphan check) */
    case 256:  return g_self->tid;  /* set_tid_address: returns the caller's TID. MUST match gettid(224)
                                       -- glibc caches this return as THREAD_SELF->tid, and a recursive/
                                       owner-checked mutex compares __owner (set from that) against gettid.
                                       Returning a constant 1 here while gettid returns g_self->tid made
                                       the check never match -> the thread deadlocked re-locking a mutex
                                       it already owned (Rhythmos GPAC movie init). */
    case 338:  return 0;  /* set_robust_list */
    case 174: { /* rt_sigaction(signum, act, oldact, sigsetsize) */
        int sig = (int)a0;
        if (sig > 0 && sig <= 64) {
            if (a2) { uint32_t o[3] = {g_sigact[sig].handler, g_sigact[sig].flags,
                                       g_sigact[sig].restorer};
                      uc_mem_write(g_uc, a2, o, 12);
                      uc_mem_write(g_uc, a2 + 12, &g_sigact[sig].mask, 8); }
            /* The inline fork child resets handlers to SIG_DFL pre-exec; since it shares the
               process-wide table with the still-running parent threads, applying that would
               (transiently) wipe e.g. the LinuxThreads restart handler and drop a concurrent
               thread's restart. The child only execs our exit-stub, so ignore its changes. */
            if (g_forked && g_self == g_fork_thread) return 0;
            if (a1) { uint32_t h[3]; uc_mem_read(g_uc, a1, h, 12);
                      uint64_t m = 0; uc_mem_read(g_uc, a1 + 12, &m, 8);
                      g_sigact[sig].handler = h[0]; g_sigact[sig].flags = h[1];
                      g_sigact[sig].restorer = h[2]; g_sigact[sig].mask = m;
                      if (getenv("ME_SIGLOG") && sig >= 32 && sig <= 34)
                          fprintf(stderr, "SIG t%d sigaction(%d) handler=%08x flags=%08x\n",
                                  g_self ? g_self->tid : -1, sig, h[0], h[1]); }
        }
        return 0;
    }
    case 175: { /* rt_sigprocmask(how, set, oldset, size) */
        struct thread *t = g_self;
        if (a2) uc_mem_write(g_uc, a2, &t->sig_blocked, 8);
        if (a1) { uint64_t set = 0; uc_mem_read(g_uc, a1, &set, 8);
                  if (a0 == 0) t->sig_blocked |= set;
                  else if (a0 == 1) t->sig_blocked &= ~set;
                  else if (a0 == 2) t->sig_blocked = set; }
        return 0;
    }
    case 37:   return send_sig((int)a0, (int)a1);  /* kill(pid, sig) */
    case 238:  return send_sig((int)a0, (int)a1);  /* tkill(tid, sig) */
    case 268:  return send_sig((int)a1, (int)a2);  /* tgkill(tgid, tid, sig) */
    case 119:  /* sigreturn */
    case 173: { /* rt_sigreturn: restore the pre-handler register state */
        struct thread *t = g_self;
        if (t->has_sigsave) { for (int i = 0; i < 17; i++) gwrite(g_sregs[i], t->sigsave[i]);
                              t->has_sigsave = 0; }
        if (t->susp_active) { t->sig_blocked = t->susp_oldmask; t->susp_active = 0; }
        g_setpc = 1;   /* PC/regs restored; don't let intr_cb clobber R0 */
        return 0;
    }
    case 142: { /* _newselect(n, readfds, writefds, exceptfds, timeout). GP2X games use
                   select(0,NULL,NULL,NULL,&tv) as a portable sub-second sleep (Knight Lore's
                   audio/timing worker spins on it). Report any requested writefds as ready
                   (devices/files are always writable), clear readfds/exceptfds (no input
                   pending), and otherwise sleep the timeout — lock-free — so the caller paces
                   to real time instead of busy-spinning. */
        uint32_t nfds = a0, rd = a1, wr = a2, ex = a3, tmo = a4;
        int words = (int)((nfds + 31) / 32); if (words > 32) words = 32; if (words < 0) words = 0;
        int ready = 0;
        if (wr) for (int i = 0; i < words; i++) {
            uint32_t w = 0; uc_mem_read(g_uc, wr + i * 4, &w, 4);
            ready += __builtin_popcount(w);
        }
        if (rd) { uint32_t z = 0; for (int i = 0; i < words; i++) uc_mem_write(g_uc, rd + i * 4, &z, 4); }
        if (ex) { uint32_t z = 0; for (int i = 0; i < words; i++) uc_mem_write(g_uc, ex + i * 4, &z, 4); }
        if (ready) return ready;
        double dur = 0.02;                 /* no timeout (parked thread): park ~20ms, then re-select */
        if (tmo) { uint32_t tv[2] = {0, 0}; uc_mem_read(g_uc, tmo, tv, 8);
                   dur = (double)tv[0] + (double)tv[1] * 1e-6; }
        if (dur > 0.1) dur = 0.1;
        if (dur <= 0) dur = 0.001;          /* zero-timeout poll: a 1ms yield avoids pinning a core */
        BIGLOCK_UNLOCK(); me_usleep((unsigned)(dur * 1e6)); BIGLOCK_LOCK();
        return 0;
    }
    case 168: { /* poll(fds, nfds, timeout): pipe check, else a real (lock-free) sleep */
        int ready = 0;
        for (uint32_t i = 0; i < a1; i++) {
            uint32_t fd = 0; uint16_t ev = 0, rev = 0;
            uc_mem_read(g_uc, a0 + i * 8, &fd, 4);
            uc_mem_read(g_uc, a0 + i * 8 + 4, &ev, 2);
            int dt = dev_type((int)fd);
            if ((int)fd == PIPEFD_R) { if (g_pipe_w > g_pipe_r) rev |= 1; }  /* POLLIN */
            else if (dt == DEV_GPIO) { if (ev & 1) rev |= 1; }               /* button device: state always ready */
            else if ((dt == DEV_INPUT_EV || dt == DEV_INPUT_JS) && (ev & 1) && input_pending((int)fd)) rev |= 1;
            if (ev & 4) rev |= 4;                                            /* devices/files always writable */
            uc_mem_write(g_uc, a0 + i * 8 + 6, &rev, 2);
            if (rev) ready++;
        }
        if (ready) return ready;
        int tmo = (int)a2;
        if (tmo == 0) return 0;            /* non-blocking */
        double dur = (tmo < 0 || tmo > 100) ? 0.1 : (double)tmo / 1000.0;
        BIGLOCK_UNLOCK();
        me_usleep((unsigned)(dur * 1e6));
        BIGLOCK_LOCK();
        return 0;
    }
    case 240: { /* futex(uaddr, op, val, timeout, ...) — mask off PRIVATE_FLAG(0x80)+CLOCK_REALTIME(0x100) */
        int op = (int)(a1 & 0x7f);
        /* WAIT_BITSET(9)/WAKE_BITSET(10) behave like WAIT/WAKE for our purposes (we ignore the
           bitset). We DO honour the timeout (a3): FUTEX_WAIT's is a RELATIVE timespec, WAIT_BITSET's
           is ABSOLUTE (CLOCK_REALTIME if flag 0x100, else CLOCK_MONOTONIC). Compute a CLOCK_REALTIME
           absolute deadline; NULL a3 = infinite. (Ignoring it stalled single-threaded timed waits.) */
        if (op == 0 || op == 9) {
            struct timespec abst; int have = 0;
            if (a3) {
                uint32_t gt[2] = {0, 0}; uc_mem_read(g_uc, a3, gt, 8);   /* 32-bit tv_sec, tv_nsec */
                long long rel_ns;
                if (op == 0) rel_ns = (long long)gt[0] * 1000000000LL + gt[1];   /* relative */
                else {                                                            /* absolute */
                    struct timespec now;
                    clock_gettime((a1 & 0x100) ? CLOCK_REALTIME : CLOCK_MONOTONIC, &now);
                    rel_ns = ((long long)gt[0] - now.tv_sec) * 1000000000LL + ((long long)gt[1] - now.tv_nsec);
                }
                if (rel_ns < 0) rel_ns = 0;
                struct timespec now; clock_gettime(CLOCK_REALTIME, &now);
                long long t = (long long)now.tv_sec * 1000000000LL + now.tv_nsec + rel_ns;
                abst.tv_sec = (time_t)(t / 1000000000LL); abst.tv_nsec = (long)(t % 1000000000LL);
                have = 1;
            }
            { const char *fw = getenv("ME_FUTEXWATCH");
              uint32_t wa = fw ? (uint32_t)strtoul(fw, NULL, 16) : 0;
              static int n=0;
              if ((getenv("ME_FUTEXLOG") && n++<40) || (wa && a0 == wa)) {
                uint32_t m[5]={0}; uc_mem_read(g_uc, a0, m, 20);
                fprintf(stderr,"FUTEX wait op=%d addr=%08x val=%u tmo=%d | w0=%u w4=%u w8=%u w12=%u w16=%u | tid=%d pc=%08x\n",
                        op,a0,a2,have,m[0],m[1],m[2],m[3],m[4],g_self?g_self->tid:-1,g_self?g_self->last_pc:0);} }
            return futex_wait(a0, a2, have ? &abst : NULL);          /* FUTEX_WAIT[_BITSET] */
        }
        if (op == 1 || op == 10) {
            const char *fw = getenv("ME_FUTEXWATCH");
            uint32_t wa = fw ? (uint32_t)strtoul(fw, NULL, 16) : 0;
            static int w=0;
            if ((getenv("ME_FUTEXLOG") && w++<40) || (wa && a0 == wa))
                fprintf(stderr,"FUTEX wake addr=%08x n=%d tid=%d pc=%08x\n", a0, (int)a2,
                        g_self?g_self->tid:-1, g_self?g_self->last_pc:0);
            return futex_wake(a0, (int)a2); }                        /* FUTEX_WAKE[_BITSET] */
        /* REQUEUE(3)/CMP_REQUEUE(4): glibc pthread_cond_broadcast moves cond waiters to the mutex
           futex instead of waking them all (anti-thundering-herd). We don't model a per-futex wait
           queue we can splice, so we WAKE every waiter on the cond futex (a0); each re-tests its
           predicate and re-blocks on the target (a4, the mutex) naturally. Spurious wakeups are
           permitted by the futex contract, so this is correct, just less optimal. Without it cond
           broadcasts were dropped and every waiter hung (Rhythmos/GPAC song-load deadlock). */
        if (op == 3 || op == 4) {
            if (op == 4) { uint32_t cur = 0; uc_mem_read(g_uc, a0, &cur, 4);
                if (cur != a5) return -11 /*EAGAIN*/; }             /* CMP_REQUEUE val3 check */
            if (getenv("ME_FUTEXLOG")) { static int r=0; if(r++<40)
                fprintf(stderr,"FUTEX requeue op=%d addr=%08x -> %08x (wake-all)\n", op, a0, a4); }
            return futex_wake(a0, INT_MAX);
        }
        /* WAKE_OP(5): atomically *uaddr2 = (*uaddr2 OP oparg); wake a2 on uaddr; if (oldval CMP
           cmparg) wake a3 on uaddr2. Encoded in a5 per the FUTEX_OP macro. */
        if (op == 5) {
            uint32_t enc = a5;
            int oper = (enc >> 28) & 0xf, cmp = (enc >> 24) & 0xf;
            int oparg = (enc >> 12) & 0xfff, cmparg = enc & 0xfff;
            if (oper & 8) { oparg = 1 << (oparg & 0x1f); oper &= 7; }   /* FUTEX_OP_OPARG_SHIFT */
            uint32_t oldv = 0; uc_mem_read(g_uc, a4, &oldv, 4);
            uint32_t newv = oldv;
            switch (oper) { case 0: newv = (uint32_t)oparg; break; case 1: newv = oldv + oparg; break;
                case 2: newv = oldv | oparg; break; case 3: newv = oldv & ~(uint32_t)oparg; break;
                case 4: newv = oldv ^ (uint32_t)oparg; break; }
            uc_mem_write(g_uc, a4, &newv, 4);
            long n = futex_wake(a0, (int)a2);
            int w2; switch (cmp) { case 0: w2 = (oldv == (uint32_t)cmparg); break;
                case 1: w2 = (oldv != (uint32_t)cmparg); break; case 2: w2 = ((int)oldv < cmparg); break;
                case 3: w2 = ((int)oldv <= cmparg); break; case 4: w2 = ((int)oldv > cmparg); break;
                case 5: w2 = ((int)oldv >= cmparg); break; default: w2 = 0; break; }
            if (w2) n += futex_wake(a4, (int)a3);
            return n;
        }
        return 0;
    }
    case 120: { /* clone(flags, child_stack, ptid, tls, ctid) -> a native host thread */
        if (g_exit) return -11 /*EAGAIN*/;   /* teardown in progress: don't spawn new workers */
        /* clone WITHOUT CLONE_VM is fork(), not a thread: glibc fork() issues
           clone(SIGCHLD|CHILD_SETTID|CHILD_CLEARTID, child_stack=0). Spawning a memory-sharing
           host thread for it gives the child sp=0 (no stack) -> instant null-deref (Liar hit this
           in init). We run a SINGLE process, so fork() picks one branch: like the OABI fork(2)
           case 2 default, take the PARENT branch (return the child pid) so the game continues --
           the only forks seen are glibc system()/posix_spawn whose child just execs /bin/sh
           (a no-op here); waitpid (case 7/114) reaps it. ME_GP2X_CLONEFORK_CHILD forces the CHILD
           branch (return 0) for a launcher-style title whose child is the real game (honours
           CLONE_CHILD_SETTID so glibc's __libc_fork "self->tid != ppid" assert passes). */
        if (!(a0 & ME_CLONE_VM)) {
            int as_child = getenv("ME_GP2X_CLONEFORK_CHILD") != NULL;
            if (g_trace) fprintf(stderr, "  [clone=fork] flags=%08x stack=%08x -> %s\n",
                                 a0, a1, as_child ? "child (0)" : "parent (pid)");
            if (!as_child) return (long)g_child_pid;
            if ((a0 & ME_CLONE_CHILD_SETTID) && a4) { uint32_t t = g_child_pid; uc_mem_write(g_uc, a4, &t, 4); }
            return 0;
        }
        int slot = thread_alloc();
        if (slot < 0) return -11 /*EAGAIN*/;
        struct thread *c = &g_th[slot];
        memset(c, 0, sizeof *c);
        c->tid = g_next_tid++;
        c->ppid = g_self->tid;
        c->state = TH_RUN;
        c->tls = (a0 & ME_CLONE_SETTLS) ? a3 : g_self->tls;
        c->ctid = (a0 & ME_CLONE_CHILD_CLEARTID) ? a4 : 0;
        c->sig_blocked = g_self->sig_blocked;
        c->sp = a1;
        c->entry_pc = gread(UC_ARM_REG_PC);     /* child resumes after the svc, like the parent */
        c->uc = uc_new_thread();
        for (int i = 0; i < 15; i++) {          /* seed child regs = parent's (R0..R12,SP,LR) */
            uint32_t v; uc_reg_read(g_uc, g_sregs[i], &v); uc_reg_write(c->uc, g_sregs[i], &v);
        }
        uint32_t cpsr; uc_reg_read(g_uc, UC_ARM_REG_CPSR, &cpsr);
        uc_reg_write(c->uc, UC_ARM_REG_CPSR, &cpsr);
        uc_reg_write(c->uc, UC_ARM_REG_SP, &c->sp);
        uint32_t zero = 0; uc_reg_write(c->uc, UC_ARM_REG_R0, &zero);   /* child fork()==0 */
        if ((a0 & ME_CLONE_PARENT_SETTID) && a2) { uint32_t t = c->tid; uc_mem_write(g_uc, a2, &t, 4); }
        if ((a0 & ME_CLONE_CHILD_SETTID) && a4) { uint32_t t = c->tid; uc_mem_write(g_uc, a4, &t, 4); }
        if (g_trace) fprintf(stderr, "  [clone] tid=%d stack=%08x flags=%08x (nth=%d)\n",
                             c->tid, a1, a0, g_nth);
        pthread_create(&c->th, NULL, thread_entry, c);
        return c->tid;     /* parent gets the new tid */
    }
    case 158:   /* sched_yield */
        BIGLOCK_UNLOCK(); sched_yield(); BIGLOCK_LOCK();
        return 0;
    case 29:    /* pause */
    case 72:    /* sigsuspend (old) */
    case 179: { /* rt_sigsuspend(mask, size) — block until a deliverable signal arrives */
        struct thread *t = g_self;
        t->susp_oldmask = t->sig_blocked; t->susp_active = 1;
        if (nr == 179 && a0) { uint64_t m = 0; uc_mem_read(g_uc, a0, &m, 8); t->sig_blocked = m; }
        else if (nr == 72) t->sig_blocked = a0;
        if (!(t->sig_pending & ~t->sig_blocked)) sigsuspend_wait();
        gwrite(UC_ARM_REG_R0, (uint32_t)-4 /*EINTR*/);
        deliver_signals(); g_setpc = 1;
        return 0;
    }
    case 162: { /* nanosleep(req, rem): a real sleep, releasing the engine lock */
        if (a1) { uint32_t z[2] = {0, 0}; uc_mem_write(g_uc, a1, z, 8); }
        uint32_t ts[2] = {0, 0}; if (a0) uc_mem_read(g_uc, a0, ts, 8);
        double dur = (double)ts[0] + (double)ts[1] * 1e-9;
        if (dur > 0.1) dur = 0.1;
        if (dur > 0) { BIGLOCK_UNLOCK();
                       me_usleep((unsigned)(dur * 1e6));
                       BIGLOCK_LOCK(); }
        return 0;
    }
    case 78: {  /* gettimeofday(tv, tz): real wall-clock — games drive loading/animation
                   timing off this; returning 0 froze the elapsed-time delta (stuck screens). */
        if (a0) { struct timeval tv; gettimeofday(&tv, NULL);
                  uint32_t t[2] = { (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec };
                  uc_mem_write(g_uc, a0, t, 8); }
        return 0;
    }
    case 191: { /* ugetrlimit(res, rlim) -> cur=8MB max=inf */
        uint32_t rl[2] = {0x00800000u, 0xffffffffu};
        if (a1) uc_mem_write(g_uc, a1, rl, 8); return 0;
    }
    case 369:  return 0;  /* prlimit64 */
    /* POSIX message queues (Didj Brio EventManager). In-engine impl (mqueue.c). */
    case 274:  return mq_open_sys(a0, a1, a2, a3);                   /* mq_open(name,oflag,mode,attr) */
    case 275:  return mq_unlink_sys(a0);                            /* mq_unlink(name) */
    case 276:  return mq_timedsend_sys((int)a0, a1, a2, a3, a4);     /* mq_timedsend */
    case 277:  return mq_timedreceive_sys((int)a0, a1, a2, a3, a4);  /* mq_timedreceive */
    case 278:  return 0;                                            /* mq_notify: events go via send/recv */
    case 279:  return mq_getsetattr_sys((int)a0, a1, a2);            /* mq_getsetattr */
    /* Scheduling: we don't honour real-time policies, but pthread_create with sched attrs (Didj
       Brio tasks) needs these to succeed or it fails with EINVAL. Report POSIX-standard priority
       ranges and accept set/get as no-ops. policy: 0=SCHED_OTHER, 1=FIFO, 2=RR. */
    case 159:  return (a0 == 0) ? 0 : 99;   /* sched_get_priority_max */
    case 160:  return (a0 == 0) ? 0 : 1;    /* sched_get_priority_min */
    case 154:  return 0;                    /* sched_setparam */
    case 155:  if (a1) { uint32_t z = 0; uc_mem_write(g_uc, a1, &z, 4); } return 0;  /* sched_getparam */
    case 157:  return 0;                    /* sched_getscheduler -> SCHED_OTHER */
    /* Direct socket syscalls (EABI; uClibc/Didj). No real networking: a Lightning task opens an
       AF_UNIX listening socket for an IPC control channel (socket/bind/listen ok, accept parks),
       and socketpair() backs an internal self-pipe with a real host pair. See netsock.c. */
    case 281:  return sock_socket(a0, a1, a2);            /* socket */
    case 282:  return 0;                                 /* bind */
    case 283:  return sock_is_fake((int)a0) ? -111 : 0;  /* connect -> ECONNREFUSED on a fake sock */
    case 284:  return 0;                                 /* listen */
    case 285:  return sock_accept((int)a0, 0);           /* accept */
    case 366:  return sock_accept((int)a0, a3);          /* accept4 */
    case 286:  case 287:  return 0;                      /* getsockname / getpeername */
    case 288:  return sock_socketpair(a0, a1, a2, a3);   /* socketpair */
    case 289:  case 290:  case 296:  return (long)a2;    /* send/sendto/sendmsg: claim sent (a2=len) */
    case 291:  case 292:  case 297:                      /* recv/recvfrom/recvmsg: nothing to read */
        return sock_is_fake((int)a0) ? -11 /*EAGAIN*/ : 0;
    case 293:  return 0;                                 /* shutdown */
    case 294:  return 0;                                 /* setsockopt */
    case 295:  if (a3 && a4) { uint32_t z = 0; uc_mem_write(g_uc, a3, &z, 4);
                              uint32_t l = 4; uc_mem_write(g_uc, a4, &l, 4); } return 0;  /* getsockopt */
    case 106: { /* stat(path, buf) */
        char p[1024]; read_cstr(a0, p, sizeof p);
        struct stat s;
        if (sd_fake_node(p, &s)) { fill_oabi_stat(a1, &s); return 0; }
        if (input_fake_node(p, &s)) { fill_oabi_stat(a1, &s); return 0; }
        if (oss_fake_node(p, &s)) { fill_oabi_stat(a1, &s); return 0; }
        char hp[PATH_MAX]; resolve_io(p, 0, hp, sizeof hp);
        if (stat(hp, &s)) return LERR(errno);
        if (s.st_ino == 0) s.st_ino = path_ino(hp);   /* Win: unique inode (ld.so dedups by it) */
        fill_oabi_stat(a1, &s); return 0;
    }
    case 108: { /* fstat(fd, buf) */
        /* A directory handle (dirfd_make) is synthetic, not a host fd -- recognise it like the
           fstat64 path does (case 197), or uClibc's opendir() fstat's the fd, sees the host
           fstat fail, and reports the directory as empty (Didj RealAppManager: "No modules
           found in /Didj/Base/Brio/Module/" -> BOOTFAIL). */
        if (dirfd_get((int)a0)) { struct stat ds; memset(&ds, 0, sizeof ds);
            ds.st_mode = S_IFDIR | 0755; ds.st_ino = 2; fill_oabi_stat(a1, &ds); return 0; }
        struct memfile *mf = memfd_get((int)a0);
        if (mf) { struct stat ms; memset(&ms, 0, sizeof ms); ms.st_mode = S_IFREG | 0644;
                  ms.st_size = mf->len; fill_oabi_stat(a1, &ms); return 0; }
        struct stat s; if (fstat((int)a0, &s)) return LERR(errno);
        /* Windows: MinGW's fstat returns st_ino==0 for every file, but the guest ld.so dedups
           shared objects by (st_dev,st_ino) -- so every NEEDED lib looks like a duplicate of the
           first and is skipped (uClibc/Didj: opens libc.so.0, fstat's it, then closes it without
           mapping -> "can't resolve symbol __uClibc_main"). Synthesise a path-unique inode, like
           the fstat64 path (case 197). (This is why glibc titles, which use fstat64, already
           worked on Windows but uClibc, which uses fstat(108), did not.) */
        if (s.st_ino == 0) s.st_ino = hostfd_ino((int)a0);
        fill_oabi_stat(a1, &s); return 0;
    }
    case 195: case 196: case 197: { /* stat64 / lstat64 / fstat64 */
        struct stat s; int ok; char p[1024] = {0};
        if (nr == 197) {
            if (dirfd_get((int)a0)) { struct stat ds; memset(&ds, 0, sizeof ds);
                ds.st_mode = S_IFDIR | 0755; ds.st_ino = 2; fill_stat64(a1, &ds); return 0; }
            struct memfile *mf = memfd_get((int)a0);
            if (mf) { struct stat ms; memset(&ms, 0, sizeof ms); ms.st_mode = S_IFREG | 0644;
                      ms.st_size = mf->len; fill_stat64(a1, &ms); return 0; }
            ok = fstat((int)a0, &s);
            if (!ok && s.st_ino == 0) s.st_ino = hostfd_ino((int)a0);   /* Win: synth unique inode */
        }
        else { read_cstr(a0, p, sizeof p);
               if (sd_fake_node(p, &s)) { fill_stat64(a1, &s); return 0; }   /* SD present (firmware) */
               if (input_fake_node(p, &s)) { fill_stat64(a1, &s); return 0; } /* event0/js0 (SDL joy scan) */
               if (oss_fake_node(p, &s)) { fill_stat64(a1, &s); return 0; }   /* /dev/dsp*,/dev/mixer (PortAudio) */
               int mf = sysfile_open(p);   /* a faked path: report it as a regular file */
               if (mf) { struct memfile *m = memfd_get(mf); struct stat ms; memset(&ms, 0, sizeof ms);
                         ms.st_mode = S_IFREG | 0644; ms.st_size = m->len; free(m->data); m->used = 0;
                         fill_stat64(a1, &ms); return 0; }
               char hp[PATH_MAX]; resolve_io(p, 0, hp, sizeof hp);
               ok = (nr == 196) ? lstat(hp, &s) : stat(hp, &s);
               if (!ok && s.st_ino == 0) s.st_ino = path_ino(hp);       /* Win: synth unique inode */
        }
        if (g_trace && nr != 197) fprintf(stderr, "  stat64 '%s' -> %s\n", p, ok ? "FAIL" : "ok");
        if (ok) return LERR(errno);
        if (s.st_dev == 0) s.st_dev = 1;                                /* ld.so keys on (dev,ino) */
        fill_stat64(a1, &s); return 0;
    }
    case 146: { /* writev(fd, iov, cnt) */
        long tot = 0;
        for (uint32_t i = 0; i < a2; i++) {
            uint32_t io[2]; uc_mem_read(g_uc, a1 + i * 8, io, 8);
            if (!io[1]) continue;
            uint8_t *t = malloc(io[1]); uc_mem_read(g_uc, io[0], t, io[1]);
            long w = write((int)a0, t, io[1]); free(t);
            if (w > 0) tot += w;
        }
        return tot;
    }
    case 0xfff0: {  /* kuser_cmpxchg, host-atomic (r0=oldval, r1=newval, r2=ptr). Fallback path only:
                       the forked Unicorn now emits this helper as an in-TB host-atomic CAS at
                       translation time (fork-patches/kuser_cmpxchg.py), so the `svc #0x90fff0` form
                       never actually executes. Kept correct in case a build runs without that patch.
                       Atomic vs other threads' raw guest stores (same host backing). Success: r0=0+C. */
        uint32_t *hp = (uint32_t *)guest_to_host(a2);
        uint32_t expected = a0;
        int ok = hp && __atomic_compare_exchange_n(hp, &expected, a1, 0,
                                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        uint32_t cpsr = gread(UC_ARM_REG_CPSR);
        if (ok) cpsr |= 0x20000000u; else cpsr &= ~0x20000000u;
        gwrite(UC_ARM_REG_CPSR, cpsr);
        return ok ? 0 : ~0u;
    }
    case 102: { /* socketcall(call, args[]). The only socket use seen is glibc syslog() opening an
                   AF_UNIX/SOCK_DGRAM socket to /dev/log. We give it a fake socket and SWALLOW the
                   datagrams (printing them under ME_SYSLOG) so syslog succeeds instead of failing
                   -- a failed socket() made the guest abort. No real networking is emulated. */
        uint32_t args[6] = {0}; if (a1) uc_mem_read(g_uc, a1, args, 24);
        switch (a0) {
        case 1:  return FAKESOCK_FD;                       /* SYS_SOCKET */
        case 2: case 3: case 4: case 14: return 0;         /* bind/connect/listen/setsockopt: ok */
        case 9: case 11: case 16: {                        /* send/sendto/sendmsg: discard */
            if (getenv("ME_SYSLOG")) {
                if (a0 != 16) { uint32_t buf = args[1], len = args[2];
                    if (len > 512) len = 512; char m[513];
                    if (buf && len) { uc_mem_read(g_uc, buf, m, len); m[len] = 0;
                                      fprintf(stderr, "  [syslog] %s\n", m); } }
            }
            return (long)args[2];                          /* claim we sent it all */
        }
        case 10: case 12: case 17: return 0;               /* recv*: nothing to read */
        default: return 0;
        }
    }
    default:
        me_report(MR_UNIMPL_SYSCALL, (long)nr, NULL, g_self ? g_self->last_pc : 0);
        if (g_trace)
            fprintf(stderr, "me_unicorn: UNIMPLEMENTED syscall %u (r0=%08x r1=%08x r2=%08x)\n",
                    nr, a0, a1, a2);
        return LERR(ENOSYS);
    }
}

