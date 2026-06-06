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
#ifdef _WIN32
#include <direct.h>
#define LMKDIR(p) _mkdir(p)
#define strcasecmp _stricmp
#else
#include <strings.h>
#define LMKDIR(p) mkdir(p, 0777)
#endif

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

/* A GP2X .gpe is often a tiny shell-script launcher ("#!/bin/sh\n./Game\ncd /usr/gp2x\n...")
   rather than the binary itself. Scan it for a referenced filename that exists beside it and is
   an ELF, and use that. Returns out, or NULL if none found. */
static const char *resolve_script(const char *gpe, char *out, size_t cap) {
    char dir[PATH_MAX]; snprintf(dir, sizeof dir, "%s", gpe);
    char *s1 = strrchr(dir, '/'), *s2 = strrchr(dir, '\\'), *s = s1 > s2 ? s1 : s2;
    if (s) *s = 0; else snprintf(dir, sizeof dir, ".");
    FILE *f = fopen(gpe, "r"); if (!f) return NULL;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        for (char *tok = strtok(line, " \t\r\n;|&"); tok; tok = strtok(NULL, " \t\r\n;|&")) {
            const char *name = tok;
            while (*name == '.' || *name == '/' || *name == '\\') name++;   /* strip ./ */
            if (!*name) continue;
            char cand[PATH_MAX]; snprintf(cand, sizeof cand, "%s/%s", dir, name);
            struct stat st;
            if (!stat(cand, &st) && S_ISREG(st.st_mode) && file_is_elf(cand)) {
                fclose(f); snprintf(out, cap, "%s", cand); return out;
            }
        }
    }
    fclose(f);
    return NULL;
}

/* If `elf` is a GPEComp self-extractor, decompress its UCL payload to the host scratch dir
   (<tmpdir>/<stem>_tmp) and return that path in out (1); else 0. The .gpe stub is dynamically
   linked (unrunnable natively), but the payload it carries is the static game. */
static int gpecomp_to_tmp(const char *elf, char *out, size_t cap) {
    FILE *f = fopen(elf, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 0; }
    fclose(f);
    if (!gpecomp_detect(buf, (size_t)sz)) { free(buf); return 0; }
    uint8_t *dec = NULL; size_t dlen = 0;
    if (gpecomp_decompress(buf, (size_t)sz, &dec, &dlen) != 0) {
        free(buf);
        fprintf(stderr, "magiceyes: '%s' is GPEComp but decompression failed (corrupt/unsupported)\n", elf);
        return 0;
    }
    free(buf);
    /* derive <gamedir> and <stem> from the .gpe path */
    char dir[PATH_MAX]; snprintf(dir, sizeof dir, "%s", elf);
    char *d1 = strrchr(dir, '/'), *d2 = strrchr(dir, '\\'), *d = d1 > d2 ? d1 : d2;
    const char *name = d ? d + 1 : elf;
    char stem[PATH_MAX]; snprintf(stem, sizeof stem, "%s", name);
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    if (d) *d = 0; else snprintf(dir, sizeof dir, ".");
    /* Prefer writing the decompressed game BESIDE the .gpe so the engine's chdir lands in the
       game's own dir and its relative Data/ resolves. Fall back to the host scratch dir if the
       game's location is read-only (the game may then not find its assets -- it's the best we can do). */
    snprintf(out, cap, "%s/%s_tmp", dir, stem);
    FILE *o = fopen(out, "wb");
    if (!o) {
        char base[PATH_MAX]; me_host_tmpdir(base, sizeof base);
        snprintf(out, cap, "%s/%s_tmp", base, stem);
        o = fopen(out, "wb");
        if (!o) { fprintf(stderr, "magiceyes: cannot write decompressed '%s': %s\n", out, strerror(errno)); free(dec); return 0; }
        fprintf(stderr, "magiceyes: note: '%s' is read-only; decompressed to %s (the game may not find its Data/)\n", dir, out);
    }
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
    if (file_is_elf(path)) { snprintf(elfbuf, sizeof elfbuf, "%s", path); elf = elfbuf; }
    else {
        elf = resolve_script(path, elfbuf, sizeof elfbuf);
        if (!elf) {
            fprintf(stderr, "magiceyes: '%s' is not an ARM ELF and no runnable binary was found beside it\n", path);
            return NULL;
        }
    }
    if (gpecomp_to_tmp(elf, out, cap)) return out;   /* GPEComp stub -> decompressed static game */
    snprintf(out, cap, "%s", elf);
    return out;
}

/* Reduce a directory tree to its single .gpe (following a launcher script to the real binary);
   error (with a listing) on 0 or 2+. */
static const char *resolve_dir(const char *dir, char *out, size_t cap) {
    struct gpelist gl; gl.n = 0;
    scan_gpe(dir, 4, &gl);
    if (gl.n == 0) { fprintf(stderr, "magiceyes: no .gpe found under '%s'\n", dir); return NULL; }
    if (gl.n > 1) {
        fprintf(stderr, "magiceyes: %d .gpe files under '%s' (ambiguous -- pass one directly):\n",
                gl.n, dir);
        for (int i = 0; i < gl.n; i++) fprintf(stderr, "    %s\n", gl.paths[i]);
        return NULL;
    }
    return finalize(gl.paths[0], out, cap);
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
