/* magiceyes Unicorn engine — input resolution + ELF classification.
 *
 * The CLI/menu accept a folder, a .zip, or a .gpe directly. resolve_input() reduces any of
 * those to a single runnable binary path (finding the one .gpe in a folder/zip, erroring on
 * 0 or 2+), and classify_elf() decides static-ET_EXEC (run it) vs dynamically-linked (deferred
 * to the Wiz/qemu path). GPEComp .gpe stubs are themselves dynamically linked, so the native
 * engine can't run them to self-extract; finalize() decompresses the appended UCL payload
 * offline (gpecomp.c) to the scratch dir and runs that static binary instead. */
#include "engine.h"
#include "gpecomp.h"
#include <dirent.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#define LMKDIR(p) _mkdir(p)
#define strcasecmp _stricmp
#else
#include <strings.h>
#define LMKDIR(p) mkdir(p, 0777)
#endif

/* Launcher-script arguments to forward to the guest (e.g. BermudaSyndrome's
   "./BS_gp2x.bin --datapath=./DATA"): captured from the script line that names the binary, and
   pushed after argv[0] by engine_load_game. Reset per resolve_input. */
char g_launch_args[8][256];
int  g_launch_nargs = 0;
#define ME_SCRIPT_SEP " \t\r\n;|&\"'`"

/* The dir the launcher script would run the game FROM on device: the script's own dir plus any
   relative `cd` lines before the exec. Pins g_game_root (cwd + save overlay) for script-followed
   titles -- a BennuGD game's bgdi lives in ../bgd-runtime, but game.dcb sits beside the SCRIPT. */
char g_launch_cwd[PATH_MAX] = {0};

/* Host dirs harvested from the script's LD_LIBRARY_PATH=/PATH= assignments (":"-joined). The
   BennuGD runtime dir is outside the default guest lib search; elf.c appends these. */
char g_script_libdirs[512] = {0};

/* If a launcher runs `load940 <firmware>` (the GP2X ARM940 loader), record the firmware path so the
   engine can bring up the second core inline (see me940_load_and_start) before the client game runs.
   Empty when the title doesn't use the 940. */
char g_940_firmware[PATH_MAX] = {0};
char g_launcher_dir[PATH_MAX] = {0};   /* the followed launcher script's dir (see resolve_script) */
static const char *path_base(const char *p);   /* defined below */
static int file_is_elf(const char *path);      /* defined below */
static void scan_940_firmware(const char *gpe, const char *dir) {
    FILE *f = fopen(gpe, "r"); if (!f) return;
    char line[512], *save;
    while (fgets(line, sizeof line, f)) {
        /* Only the COMMAND (first token of the line) counts -- not load940 appearing as an arg to
           `chmod a+x egoboo load940 stop940`. */
        char *t = strtok_r(line, ME_SCRIPT_SEP, &save);
        if (!t) continue;
        const char *b = t; while (*b == '.' || *b == '/' || *b == '\\') b++;
        if (strcasecmp(path_base(b), "load940")) continue;
        char *fw = strtok_r(NULL, ME_SCRIPT_SEP, &save);   /* the firmware arg */
        if (!fw) continue;
        const char *fb = fw; while (*fb == '.' || *fb == '/' || *fb == '\\') fb++;
        char cand[PATH_MAX]; snprintf(cand, sizeof cand, "%s/%s", dir, fb);
        /* the firmware is a RAW 940 blob (not an ELF, not the stop940/egoboo binaries) */
        struct stat st;
        if (!stat(cand, &st) && !file_is_elf(cand)) {
            snprintf(g_940_firmware, sizeof g_940_firmware, "%s", cand);
            fclose(f); return;
        }
    }
    fclose(f);
}

/* recursive .gpe scan (depth-limited): a folder/zip usually wraps the game in subdirs. */
struct gpelist { char paths[16][PATH_MAX]; int n; };
static void scan_gpe(const char *dir, int depth, struct gpelist *gl) {
    if (depth < 0 || gl->n >= 16) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && gl->n < 16) {
        if (de->d_name[0] == '.') continue;   /* skip ., .., dotfiles */
        char full[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(full, &st)) continue;
        if (S_ISDIR(st.st_mode)) { scan_gpe(full, depth - 1, gl); continue; }
        const char *ext = strrchr(de->d_name, '.');
        if (ext && !strcasecmp(ext, ".gpe"))
            snprintf(gl->paths[gl->n++], PATH_MAX, "%s", full);
    }
    closedir(d);
}

static int file_is_elf(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    unsigned char m[4]; size_t n = fread(m, 1, 4, f); fclose(f);
    return n == 4 && m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
}

/* 1 if `path` is a *runnable* ARM ELF: ET_EXEC or ET_DYN (a static/dynamic game or a GPEComp
   stub). Rejects ET_REL `.o` object files (e.g. a game's bundled cramfs.o/zlib_inflate.o kernel
   modules) so launcher-script following doesn't latch onto the wrong ELF. */
static int file_is_runnable_elf(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    Elf32_Ehdr eh; int ok = 0;
    if (fread(&eh, 1, sizeof eh, f) == sizeof eh &&
        !memcmp(eh.e_ident, ELFMAG, SELFMAG) && eh.e_ident[EI_CLASS] == ELFCLASS32 &&
        eh.e_machine == EM_ARM && (eh.e_type == ET_EXEC || eh.e_type == ET_DYN))
        ok = 1;
    fclose(f);
    return ok;
}

/* basename of a path (after the last '/' or '\\'). */
static const char *path_base(const char *p) {
    const char *s1 = strrchr(p, '/'), *s2 = strrchr(p, '\\');
    const char *s = s1 > s2 ? s1 : s2;
    return s ? s + 1 : p;
}

/* Collect runnable ARM ELF executables under `dir` (depth-limited), skipping shared libraries
   (*.so*) so we don't latch onto a bundled libSDL. Used as the launcher-follow fallback: a .gpe
   launcher script often just `exec`s the one real binary that sits beside it. */
static void scan_runnable_elf(const char *dir, int depth, struct gpelist *gl) {
    if (depth < 0 || gl->n >= 16) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && gl->n < 16) {
        if (de->d_name[0] == '.') continue;
        char full[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(full, &st)) continue;
        if (S_ISDIR(st.st_mode)) { scan_runnable_elf(full, depth - 1, gl); continue; }
        if (strstr(de->d_name, ".so")) continue;          /* a shared library, not the game */
        if (file_is_runnable_elf(full))
            snprintf(gl->paths[gl->n++], PATH_MAX, "%s", full);
    }
    closedir(d);
}

/* Device/tool commands a launcher script runs AROUND the game: never the game itself. Latching
   onto one of these (pollux_dpc_set prints "usage:" and exits) was the whole usage-print failure
   family. bgdc is the BennuGD COMPILER: the shipped .dcb is prebuilt, so the runnable game step
   is the bgdi line that follows. */
static int is_util_cmd(const char *base) {
    static const char *deny[] = {"pollux_dpc_set", "pollux_set", "pollux_fpu", "cpu_speed",
        "cpuspeed", "gp2xmenu", "bgdc", "load940", "stop940", "sync", "mount", "umount",
        "sh", "bash", "echo", "rm", "chmod", "sleep", "killall", 0};
    for (int i = 0; deny[i]; i++) if (!strcasecmp(base, deny[i])) return 1;
    return 0;
}

/* Comment or redirection token: nothing after it on the line is a real argument
   ("./game > log.txt 2>&1", "./penguin-command # > pc.log"). */
static int tok_ends_cmdline(const char *t) {
    return *t == '#' || *t == '>' || *t == '<' ||
           (isdigit((unsigned char)*t) && t[1] == '>');
}

/* "LD_LIBRARY_PATH=../bgd-runtime:$LD_LIBRARY_PATH" -> resolve the concrete relative components
   against the script's cwd; existing dirs are scanned for runnable ELFs (that's where the
   BennuGD bgdi runtime lives) and recorded for the guest LD_LIBRARY_PATH (g_script_libdirs). */
static void note_libdir_assign(const char *tok, const char *cwd, struct gpelist *elf) {
    const char *val;
    if (!strncmp(tok, "LD_LIBRARY_PATH=", 16)) val = tok + 16;
    else if (!strncmp(tok, "PATH=", 5)) val = tok + 5;
    else return;
    char comps[256]; snprintf(comps, sizeof comps, "%s", val);
    char *save = NULL;
    for (char *c = strtok_r(comps, ":", &save); c; c = strtok_r(NULL, ":", &save)) {
        if (!*c || strchr(c, '$') || *c == '/') continue;   /* $VAR refs + device-absolute dirs */
        char cand[PATH_MAX]; snprintf(cand, sizeof cand, "%s/%s", cwd, c);
        struct stat st;
        if (stat(cand, &st) || !S_ISDIR(st.st_mode)) continue;
        scan_runnable_elf(cand, 0, elf);
        size_t used = strlen(g_script_libdirs);
        if (!strstr(g_script_libdirs, cand) &&
            used + strlen(cand) + 2 < sizeof g_script_libdirs)
            snprintf(g_script_libdirs + used, sizeof g_script_libdirs - used,
                     "%s%s", used ? ":" : "", cand);
    }
}

/* If the matched binary is BennuGD's bgdi and the script's args were all shell variables we
   can't expand ("for prg in *.prg; do bgdi $name"), point it at the one prebuilt .dcb beside
   the script. */
static void bgdi_default_dcb(const char *cwd) {
    DIR *d = opendir(cwd); if (!d) return;
    struct dirent *de; char found[256] = {0}; int n = 0;
    while ((de = readdir(d))) {
        const char *ext = strrchr(de->d_name, '.');
        if (ext && !strcasecmp(ext, ".dcb")) { n++; snprintf(found, sizeof found, "%s", de->d_name); }
    }
    closedir(d);
    if (n == 1) snprintf(g_launch_args[g_launch_nargs++], sizeof g_launch_args[0], "%s", found);
}

/* One pass over one script file: track relative `cd`s, harvest lib-dir assignments, and follow
   the first referenced token that resolves to a runnable ARM ELF (exact path against the current
   cwd, else by basename in the collected candidate set). Recurses one level into a referenced
   sub-script ("./run.gpe" -> "./run-wiz"). Returns out or NULL. cwd is updated in place so a
   parent script's later lines continue from where the sub-script left the state. */
static const char *scan_script_file(const char *gpe, char *cwd, struct gpelist *elf,
                                    int depth, char *out, size_t cap) {
    FILE *f = fopen(gpe, "r");
    if (!f) return NULL;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *save = NULL;
        int was_cd = 0;
        for (char *tok = strtok_r(line, ME_SCRIPT_SEP, &save); tok;
             tok = strtok_r(NULL, ME_SCRIPT_SEP, &save)) {
            if (*tok == '#') break;                    /* comment: rest of line is dead */
            if (was_cd) {                              /* the dir operand of a `cd` */
                was_cd = 0;
                if (*tok != '/' && !strchr(tok, '$')) {  /* relative only; device paths stay */
                    char cand[PATH_MAX]; snprintf(cand, sizeof cand, "%s/%s", cwd, tok);
                    struct stat st;
                    if (!stat(cand, &st) && S_ISDIR(st.st_mode))
                        snprintf(cwd, PATH_MAX, "%s", cand);
                }
                continue;
            }
            if (!strcmp(tok, "cd")) { was_cd = 1; continue; }
            note_libdir_assign(tok, cwd, elf);
            if (strchr(tok, '$')) continue;   /* unexpandable shell variable ($prg, $HOSTNAME) */
            const char *name = tok;
            while (*name == '.' || *name == '/' || *name == '\\') name++;   /* strip ./ ../ */
            if (!*name) continue;
            if (is_util_cmd(path_base(name))) continue;
            char cand[PATH_MAX]; snprintf(cand, sizeof cand, "%s/%s", cwd, name);
            int hit = file_is_runnable_elf(cand);
            if (hit) snprintf(out, cap, "%s", cand);
            else {
                const char *tb = path_base(name);
                for (int i = 0; i < elf->n; i++)
                    if (!strcasecmp(path_base(elf->paths[i]), tb)) {
                        snprintf(out, cap, "%s", elf->paths[i]); hit = 1; break;
                    }
            }
            if (!hit && depth > 0) {
                /* a referenced sibling that is itself a "#!" script: follow it (run.gpe ->
                   run-wiz). The sub-script starts from OUR cwd, matching sh semantics. */
                FILE *sf = fopen(cand, "rb");
                if (sf) {
                    char shebang[2] = {0, 0};
                    size_t got = fread(shebang, 1, 2, sf); fclose(sf);
                    if (got == 2 && shebang[0] == '#' && shebang[1] == '!') {
                        const char *sub = scan_script_file(cand, cwd, elf, depth - 1, out, cap);
                        if (sub) { fclose(f); return sub; }
                    }
                }
            }
            if (hit) {   /* forward the binary's own args from this line (--datapath=./DATA ...) */
                for (char *a = strtok_r(NULL, ME_SCRIPT_SEP, &save); a && g_launch_nargs < 8;
                     a = strtok_r(NULL, ME_SCRIPT_SEP, &save)) {
                    if (tok_ends_cmdline(a)) break;    /* redirection/comment: args are over */
                    if (strchr(a, '$')) continue;      /* unexpandable shell variable */
                    snprintf(g_launch_args[g_launch_nargs++], sizeof g_launch_args[0], "%s", a);
                }
                if (g_launch_nargs == 0 && !strcasecmp(path_base(out), "bgdi"))
                    bgdi_default_dcb(cwd);
                snprintf(g_launch_cwd, PATH_MAX, "%s", cwd);
                fclose(f);
                return out;
            }
        }
    }
    fclose(f);
    return NULL;
}

/* A GP2X .gpe is often a tiny shell-script launcher ("#!/bin/sh\n./Game\ncd /usr/gp2x\n...")
   rather than the binary itself. Follow it to the real ARM executable:
     1. scan the script for a referenced filename that resolves to a runnable ARM ELF beside it
        (same dir or an immediate subdir, e.g. "cd runtime; ./fxi"), skipping device utilities
        and tracking relative `cd`s + LD_LIBRARY_PATH runtime dirs;
     2. if the script names nothing usable (e.g. it `exec`s a name that isn't an ELF, or just
        re-invokes itself), fall back to the runnable executable(s) sitting beside it -- picking
        the one whose name matches the folder, else the largest.
   Returns out, or NULL if no runnable binary was found. */
static const char *resolve_script(const char *gpe, char *out, size_t cap) {
    char dir[PATH_MAX]; snprintf(dir, sizeof dir, "%s", gpe);
    char *s1 = strrchr(dir, '/'), *s2 = strrchr(dir, '\\'), *s = s1 > s2 ? s1 : s2;
    if (s) *s = 0; else snprintf(dir, sizeof dir, ".");

    /* Remember the launcher's own dir: on device the script cd's here and sets
       LD_LIBRARY_PATH=. before exec'ing a possibly-nested binary, so titles ship their
       satellite libs HERE (CDogs: CDogs/libmikmod.so.2 next to CDogs.gpe, binary three dirs
       down). elf.c appends this to the guest LD_LIBRARY_PATH. */
    snprintf(g_launcher_dir, sizeof g_launcher_dir, "%s", dir);
    snprintf(g_launch_cwd, PATH_MAX, "%s", dir);   /* default: the game runs from the script dir */

    scan_940_firmware(gpe, dir);   /* note an ARM940 (load940 gpu940) launch for the engine */

    /* the runnable executables that live beside the launcher (immediate subdirs included) */
    struct gpelist elf; elf.n = 0;
    scan_runnable_elf(dir, 1, &elf);

    /* (1) follow the script (cwd tracked; may recurse one level into a sub-script). */
    char cwd[PATH_MAX]; snprintf(cwd, sizeof cwd, "%s", dir);
    if (scan_script_file(gpe, cwd, &elf, 1, out, cap)) return out;

    /* (2) fallback: pick the best runnable executable beside the launcher. */
    snprintf(g_launch_cwd, PATH_MAX, "%s", dir);   /* the wandering cwd doesn't apply */
    if (elf.n == 0) return NULL;
    char base[PATH_MAX]; snprintf(base, sizeof base, "%s", path_base(dir));
    char *dot = strrchr(base, '.'); if (dot) *dot = 0;
    int best = 0; long best_sz = -1;
    for (int i = 0; i < elf.n; i++) {
        char stem[PATH_MAX]; snprintf(stem, sizeof stem, "%s", path_base(elf.paths[i]));
        char *d = strrchr(stem, '.'); if (d) *d = 0;
        if (!strcasecmp(stem, base)) { best = i; break; }   /* name matches folder -> the game */
        struct stat st; long sz = stat(elf.paths[i], &st) ? 0 : (long)st.st_size;
        if (sz > best_sz) { best_sz = sz; best = i; }
    }
    if (g_trace && elf.n > 1)
        fprintf(stderr, "  [loader] launcher '%s' -> %s (%d candidates)\n", gpe, elf.paths[best], elf.n);
    snprintf(out, cap, "%s", elf.paths[best]);
    return out;
}

/* 64-bit FNV-1a over the whole .gpe image -> 16 hex chars. Keys the GPEComp decompress cache by
   CONTENT (not filename), so a renamed/moved .gpe maps to the same entry and is reused. Same idiom
   as syscalls.c path_ino, widened to 64 bits to make dir-name collisions negligible. */
static void content_key(const uint8_t *buf, size_t len, char out[17]) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) { h ^= buf[i]; h *= 1099511628211ULL; }
    snprintf(out, 17, "%016llx", (unsigned long long)h);
}

/* If `elf` is a GPEComp self-extractor, decompress its UCL payload into the portable Cache dir
   (<cache>/gpecomp/<content-hash>/<stem>_tmp) and return that path in out (1); else 0. The .gpe
   stub is dynamically linked (unrunnable natively), but the payload it carries is the static game.
   Caching there (a) never litters the (often read-only ROM) game folder, (b) survives a .gpe
   rename, and (c) is reused on relaunch. The decompressed location is decoupled from asset
   resolution: the engine chdir()s into g_game_root (the real .gpe dir, pinned by me_save_set_game
   before this runs), so the game's relative Data/ still resolves. */
static int gpecomp_to_tmp(const char *elf, char *out, size_t cap) {
    FILE *f = fopen(elf, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 0; }
    fclose(f);
    if (!gpecomp_detect(buf, (size_t)sz)) { free(buf); return 0; }

    /* <stem> from the .gpe name, kept in the cached filename for readable logs/diagnostics. */
    char stem[PATH_MAX]; snprintf(stem, sizeof stem, "%s", elf);
    char *s1 = strrchr(stem, '/'), *s2 = strrchr(stem, '\\'), *s = s1 > s2 ? s1 : s2;
    if (s) memmove(stem, s + 1, strlen(s + 1) + 1);
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    if (!stem[0]) snprintf(stem, sizeof stem, "game");

    char key[17]; content_key(buf, (size_t)sz, key);
    char base[PATH_MAX]; me_host_tmpdir(base, sizeof base);   /* <cache>, mkdir-p'd */
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/gpecomp", base);      LMKDIR(dir);
    snprintf(dir, sizeof dir, "%s/gpecomp/%s", base, key); LMKDIR(dir);
    snprintf(out, cap, "%s/%s_tmp", dir, stem);

    /* reuse a previous decompression of the same content (skip the decompress + write). */
    struct stat st;
    if (stat(out, &st) == 0 && st.st_size > 0) {
        free(buf);
        if (g_trace) fprintf(stderr, "  [gpecomp] %s -> %s (cached)\n", elf, out);
        return 1;
    }

    uint8_t *dec = NULL; size_t dlen = 0;
    if (gpecomp_decompress(buf, (size_t)sz, &dec, &dlen) != 0) {
        free(buf);
        fprintf(stderr, "magiceyes: '%s' is GPEComp but decompression failed (corrupt/unsupported)\n", elf);
        return 0;
    }
    free(buf);
    FILE *o = fopen(out, "wb");
    if (!o) { fprintf(stderr, "magiceyes: cannot write decompressed '%s': %s\n", out, strerror(errno)); free(dec); return 0; }
    size_t w = fwrite(dec, 1, dlen, o); fclose(o); free(dec);
    if (w != dlen) { fprintf(stderr, "magiceyes: short write to '%s'\n", out); return 0; }
#ifndef _WIN32
    chmod(out, 0755);
#endif
    if (g_trace) fprintf(stderr, "  [gpecomp] %s -> %s (%zu bytes)\n", elf, out, dlen);
    return 1;
}

/* Normalise a chosen path to a runnable ARM ELF: use it directly if it's an ELF, else treat it
   as a launcher script and follow it to the binary beside it; then transparently decompress a
   GPEComp self-extractor to its static payload. */
static const char *finalize(const char *path, char *out, size_t cap) {
    char elfbuf[PATH_MAX]; const char *elf;
    int followed = 0;
    if (file_is_elf(path)) { snprintf(elfbuf, sizeof elfbuf, "%s", path); elf = elfbuf; }
    else {
        elf = resolve_script(path, elfbuf, sizeof elfbuf);
        if (!elf) {
            fprintf(stderr, "magiceyes: '%s' is not an ARM ELF and no runnable binary was found beside it\n", path);
            return NULL;
        }
        followed = 1;
    }
    /* Pin the title's REAL asset dir + per-game save overlay, BEFORE GPEComp decompression
       (which may write the runnable payload to %TEMP%, away from Data/). For a followed
       launcher script that's the dir the SCRIPT would run the game from (script dir + its
       relative cd's) keyed by the script's stem -- a BennuGD bgdi lives in ../bgd-runtime,
       but game.dcb and the save dir belong beside the script. */
    if (followed && g_launch_cwd[0]) {
        char pin[PATH_MAX];
        snprintf(pin, sizeof pin, "%s/%s", g_launch_cwd, path_base(path));
        me_save_set_game(pin);
        /* The resolved binary may live in a runtime dir that is neither the run cwd nor the
           launcher dir (BennuGD's bgdi in bgd-runtime/, with libbgdrtm.so + module .so
           satellites beside it). Put that dir on the guest lib path too. */
        char bindir[PATH_MAX]; snprintf(bindir, sizeof bindir, "%s", elf);
        char *b1 = strrchr(bindir, '/'), *b2 = strrchr(bindir, '\\'), *b = b1 > b2 ? b1 : b2;
        if (b) {
            *b = 0;
            if (strcmp(bindir, g_launcher_dir) && strcmp(bindir, g_launch_cwd) &&
                !strstr(g_script_libdirs, bindir)) {
                size_t used = strlen(g_script_libdirs);
                if (used + strlen(bindir) + 2 < sizeof g_script_libdirs)
                    snprintf(g_script_libdirs + used, sizeof g_script_libdirs - used, "%s%s",
                             used ? ":" : "", bindir);
            }
        }
    } else
        me_save_set_game(elf);
    if (gpecomp_to_tmp(elf, out, cap)) return out;   /* GPEComp stub -> decompressed static game */
    snprintf(out, cap, "%s", elf);
    return out;
}

/* Score a .gpe candidate so resolve_dir can implicitly pick the game when a folder ships several
   (the game alongside helper stubs like cpu_speed/reset, a start.gpe launcher, and a "_Pollux"
   Caanoo build). Higher = more likely the real GP2X game. `base` is the folder's basename. */
static long score_gpe(const char *path, const char *base) {
    char stem[PATH_MAX]; snprintf(stem, sizeof stem, "%s", path_base(path));
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    /* normalise (lowercase, drop non-alnum) for a forgiving folder-name match */
    char ns[PATH_MAX]; int k = 0;
    for (const char *p = stem; *p && k < (int)sizeof ns - 1; p++)
        if (isalnum((unsigned char)*p)) ns[k++] = (char)tolower((unsigned char)*p);
    ns[k] = 0;
    char nb[PATH_MAX]; k = 0;
    for (const char *p = base; *p && k < (int)sizeof nb - 1; p++)
        if (isalnum((unsigned char)*p)) nb[k++] = (char)tolower((unsigned char)*p);
    nb[k] = 0;

    long s = 0;
    if (ns[0] && !strcmp(ns, nb)) s += 1000;            /* stem == folder name -> the game */
    static const char *deny[] = {"cpu_speed","cpuspeed","reset","select","selector","menu",
                                 "gp2xmenu","install","uninstall","setup","update",0};
    for (int i = 0; deny[i]; i++) if (!strcasecmp(stem, deny[i])) { s -= 1000; break; }
    if (strstr(ns, "pollux")) s -= 200;                  /* Caanoo build; prefer GP2X for default */
    struct stat st; if (!stat(path, &st)) s += (long)(st.st_size / 4096);   /* tie-break: bigger */
    return s;
}

/* Reduce a directory tree to the .gpe to run (following a launcher script to the real binary).
   When a folder holds several .gpe, rank them (score_gpe) and implicitly pick the best rather
   than bailing -- the corpus is full of folders bundling a game with helper/launcher stubs. */
static const char *resolve_dir(const char *dir, char *out, size_t cap) {
    struct gpelist gl; gl.n = 0;
    scan_gpe(dir, 4, &gl);
    if (gl.n == 0) { fprintf(stderr, "magiceyes: no .gpe found under '%s'\n", dir); return NULL; }
    int pick = 0;
    if (gl.n > 1) {
        const char *b = path_base(dir);
        long best = score_gpe(gl.paths[0], b);
        for (int i = 1; i < gl.n; i++) {
            long sc = score_gpe(gl.paths[i], b);
            if (sc > best) { best = sc; pick = i; }
        }
        if (g_trace) {
            fprintf(stderr, "magiceyes: %d .gpe under '%s', picking %s:\n", gl.n, dir, gl.paths[pick]);
            for (int i = 0; i < gl.n; i++)
                fprintf(stderr, "    [%c] %s\n", i == pick ? '*' : ' ', gl.paths[i]);
        }
    }
    return finalize(gl.paths[pick], out, cap);
}

/* Extract a .zip into the host scratch dir (cached: skip if it already holds a .gpe). Uses the
   OS unzip (bsdtar `tar` ships with Windows 10/11; `unzip` on Linux) -- no extra DLL dependency. */
static int extract_zip(const char *zip, char *destbuf, size_t cap) {
    char base[PATH_MAX]; me_host_tmpdir(base, sizeof base);
    const char *s1 = strrchr(zip, '/'), *s2 = strrchr(zip, '\\');
    const char *name = s1 > s2 ? s1 + 1 : (s2 ? s2 + 1 : zip);
    char stem[PATH_MAX]; snprintf(stem, sizeof stem, "%s", name);
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    snprintf(destbuf, cap, "%s/%s", base, stem);

    struct stat ds;
    if (!stat(destbuf, &ds) && S_ISDIR(ds.st_mode)) {   /* cached extraction */
        struct gpelist gl; gl.n = 0; scan_gpe(destbuf, 4, &gl);
        if (gl.n >= 1) return 0;
    }
    LMKDIR(destbuf);
    char cmd[PATH_MAX * 3];
#ifdef _WIN32
    snprintf(cmd, sizeof cmd, "tar -xf \"%s\" -C \"%s\"", zip, destbuf);
#else
    snprintf(cmd, sizeof cmd, "unzip -oq \"%s\" -d \"%s\"", zip, destbuf);
#endif
    int rc = system(cmd);
    if (rc != 0) { fprintf(stderr, "magiceyes: failed to extract '%s' (exit %d)\n", zip, rc); return -1; }
    return 0;
}

const char *resolve_input(const char *in, char *out, size_t cap) {
    g_launch_nargs = 0;   /* fresh per load; populated if we follow a launcher script with args */
    g_940_firmware[0] = 0;   /* fresh per load; set if the launcher runs load940 */
    g_launcher_dir[0] = 0;   /* fresh per load; set if we follow a launcher script */
    g_launch_cwd[0] = 0;     /* fresh per load; the followed script's effective run dir */
    g_script_libdirs[0] = 0; /* fresh per load; runtime dirs from script LD_LIBRARY_PATH lines */
    struct stat st;
    if (stat(in, &st)) { fprintf(stderr, "magiceyes: '%s': %s\n", in, strerror(errno)); return NULL; }
    if (S_ISDIR(st.st_mode)) return resolve_dir(in, out, cap);
    const char *ext = strrchr(in, '.');
    if (ext && !strcasecmp(ext, ".zip")) {
        char dest[PATH_MAX];
        if (extract_zip(in, dest, sizeof dest) != 0) return NULL;
        return resolve_dir(dest, out, cap);
    }
    /* a .gpe or a plain decompressed ELF passed directly: follow a launcher script to the binary */
    return finalize(in, out, cap);
}

/* 0 = static ET_EXEC ARM (run it; may be a GPEComp stub), 1 = dynamically linked (deferred),
   -1 = not a usable ARM ELF (message printed). */
int classify_elf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "magiceyes: cannot open '%s': %s\n", path, strerror(errno)); return -1; }
    Elf32_Ehdr eh;
    if (fread(&eh, 1, sizeof eh, f) != sizeof eh) {
        fclose(f); fprintf(stderr, "magiceyes: '%s' is too small to be an ELF\n", path); return -1;
    }
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) || eh.e_ident[EI_CLASS] != ELFCLASS32) {
        fclose(f); fprintf(stderr, "magiceyes: '%s' is not a 32-bit ELF\n", path); return -1;
    }
    if (eh.e_machine != EM_ARM) {
        fclose(f); fprintf(stderr, "magiceyes: '%s' is not an ARM binary\n", path); return -1;
    }
    if (eh.e_type == ET_DYN) { fclose(f); return 1; }   /* PIE / shared object: dynamic */
    int dynamic = 0;                                    /* a dynamically-linked ET_EXEC has PT_INTERP */
    if (eh.e_phoff && eh.e_phnum && eh.e_phentsize >= sizeof(Elf32_Phdr)) {
        for (int i = 0; i < eh.e_phnum; i++) {
            Elf32_Phdr ph;
            if (fseek(f, (long)eh.e_phoff + (long)i * eh.e_phentsize, SEEK_SET)) break;
            if (fread(&ph, 1, sizeof ph, f) != sizeof ph) break;
            if (ph.p_type == PT_INTERP) { dynamic = 1; break; }
        }
    }
    fclose(f);
    if (dynamic) return 1;
    if (eh.e_type != ET_EXEC) {
        fprintf(stderr, "magiceyes: '%s' is not an executable ELF (e_type=%u)\n", path, eh.e_type);
        return -1;
    }
    return 0;
}

/* Read a dynamic ELF's PT_INTERP string (e.g. "/lib/ld-linux.so.3") into out.
   Returns 1 if found, 0 otherwise. Used to pick the right device rootfs up front. */
int read_elf_interp(const char *path, char *out, size_t cap) {
    if (cap) out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    Elf32_Ehdr eh;
    int ok = 0;
    if (fread(&eh, 1, sizeof eh, f) == sizeof eh && !memcmp(eh.e_ident, ELFMAG, SELFMAG) &&
        eh.e_phoff && eh.e_phnum && eh.e_phentsize >= sizeof(Elf32_Phdr)) {
        for (int i = 0; i < eh.e_phnum; i++) {
            Elf32_Phdr ph;
            if (fseek(f, (long)eh.e_phoff + (long)i * eh.e_phentsize, SEEK_SET)) break;
            if (fread(&ph, 1, sizeof ph, f) != sizeof ph) break;
            if (ph.p_type == PT_INTERP && ph.p_filesz && ph.p_filesz < cap) {
                if (!fseek(f, (long)ph.p_offset, SEEK_SET) &&
                    fread(out, 1, ph.p_filesz, f) == ph.p_filesz) { out[ph.p_filesz - 1] = 0; ok = 1; }
                break;
            }
        }
    }
    fclose(f);
    return ok;
}
