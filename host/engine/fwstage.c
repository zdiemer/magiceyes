/* In-process firmware staging (see fwstage.h). Pipeline for a GP2X full-firmware .zip:
 *   .zip -> (miniz) gp2xfs.tar.gz -> (tinfl) tar -> (untar) write files to <destdir>, with
 *   symlinks dereferenced to real copies so the native Windows engine can follow them.
 * Caanoo (yaffs2) and Wiz (ubifs) extractors are added in later steps; their formats are detected
 * here so the GUI can report them. */
#include "fwstage.h"
#include "extract/miniz.h"
#include "extract/untar.h"
#include "extract/yaffs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define ME_MKDIR(p) _mkdir(p)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#define ME_MKDIR(p) mkdir(p, 0755)
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void prog(fw_progress cb, void *ud, const char *msg, int pct) { if (cb) cb(ud, msg, pct); }

/* case-insensitive substring (avoid relying on the GNU strcasestr extension) */
static const char *strcasestr_ci(const char *h, const char *needle) {
    size_t nl = strlen(needle);
    for (; *h; h++) if (!strncasecmp(h, needle, nl)) return h;
    return NULL;
}

static unsigned char *slurp(const char *path, size_t *out) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    unsigned char *b = malloc(n);
    if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f); if (b && out) *out = (size_t)n; return b;
}

/* gunzip via miniz's raw-deflate decompressor (skip the RFC1952 header). free() the result. */
static unsigned char *gz_inflate(const unsigned char *src, size_t n, size_t *outlen) {
    if (n < 18 || src[0] != 0x1f || src[1] != 0x8b || src[2] != 8) return NULL;
    unsigned flg = src[3]; size_t p = 10;
    if (flg & 4) { if (p + 2 > n) return NULL; unsigned xl = src[p] | (src[p+1] << 8); p += 2 + xl; }
    if (flg & 8)  { while (p < n && src[p]) p++; p++; }
    if (flg & 16) { while (p < n && src[p]) p++; p++; }
    if (flg & 2)  p += 2;
    if (p >= n) return NULL;
    return tinfl_decompress_mem_to_heap(src + p, n - p, outlen, 0);   /* free with mz_free */
}

/* ---- path helpers ---------------------------------------------------------- */
/* Normalise a guest-relative path: strip leading "./" and "/", collapse "//", resolve "."/"..".
   Result has no leading slash and forward slashes. Returns 0 on success. */
static int norm_path(char *out, size_t cap, const char *in) {
    char *parts[256]; int np = 0;
    char tmp[2048]; snprintf(tmp, sizeof tmp, "%s", in);
    for (char *t = strtok(tmp, "/"); t; t = strtok(NULL, "/")) {
        if (!strcmp(t, ".") || !*t) continue;
        if (!strcmp(t, "..")) { if (np > 0) np--; continue; }
        if (np < 256) parts[np++] = t;
    }
    size_t o = 0; out[0] = 0;
    for (int i = 0; i < np; i++) {
        int w = snprintf(out + o, cap - o, "%s%s", i ? "/" : "", parts[i]);
        if (w < 0 || (size_t)w >= cap - o) return -1;
        o += w;
    }
    return 0;
}

static void mkdirs(const char *dir) {   /* mkdir -p (dir is a full host path) */
    char t[PATH_MAX]; snprintf(t, sizeof t, "%s", dir);
    for (char *p = t + 1; *p; p++)
        if (*p == '/' || *p == '\\') { char c = *p; *p = 0; ME_MKDIR(t); *p = c; }
    ME_MKDIR(t);
}
static void mkparent(const char *file) {
    char t[PATH_MAX]; snprintf(t, sizeof t, "%s", file);
    char *s = strrchr(t, '/'); if (s) { *s = 0; mkdirs(t); }
}
static int write_file(const char *path, const unsigned char *data, size_t n) {
    mkparent(path);
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    int ok = (n == 0) || fwrite(data, 1, n, f) == n;
    fclose(f); return ok ? 0 : -1;
}

/* ---- collected entries (own their data: yaffs reassembles into transient buffers, tar points
   into the tar image -- copying lets us free the source buffers + treat both uniformly) ------- */
struct ent { int type; char *path; char *link; unsigned char *data; size_t size; };
struct collect { struct ent *e; int n, cap; int err; };

static int collect_cb(void *ud, const char *path, int type, const char *link,
                      const unsigned char *data, size_t size, unsigned mode) {
    (void)mode;
    struct collect *c = ud;
    char np[2048]; if (norm_path(np, sizeof np, path) != 0 || !np[0]) return 0;
    if (c->n >= c->cap) {
        int nc = c->cap ? c->cap * 2 : 1024;
        struct ent *ne = realloc(c->e, nc * sizeof *ne);
        if (!ne) { c->err = 1; return 1; }
        c->e = ne; c->cap = nc;
    }
    struct ent *e = &c->e[c->n++];
    e->type = type; e->path = strdup(np); e->link = strdup(link ? link : "");
    e->size = size;
    e->data = size ? malloc(size) : NULL;
    if (size && e->data) memcpy(e->data, data, size);
    return 0;
}
static void collect_free(struct collect *c) {
    for (int i = 0; i < c->n; i++) { free(c->e[i].path); free(c->e[i].link); free(c->e[i].data); }
    free(c->e); c->e = NULL; c->n = c->cap = 0;
}

static int find_ent(struct collect *c, const char *norm) {
    for (int i = 0; i < c->n; i++) if (!strcmp(c->e[i].path, norm)) return i;
    return -1;
}

/* Resolve a symlink chain to the final regular file's data. depth-limited. */
static const struct ent *resolve_link(struct collect *c, int idx, int depth) {
    if (idx < 0 || depth > 16) return NULL;
    struct ent *e = &c->e[idx];
    if (e->type == TAR_FILE) return e;
    if (e->type != TAR_SYMLINK && e->type != TAR_HARDLINK) return NULL;
    /* compute the target path relative to the rootfs root */
    char joined[2048];
    if (e->link[0] == '/') snprintf(joined, sizeof joined, "%s", e->link);
    else {
        char dir[2048]; snprintf(dir, sizeof dir, "%s", e->path);
        char *s = strrchr(dir, '/'); if (s) *s = 0; else dir[0] = 0;
        snprintf(joined, sizeof joined, "%s/%s", dir, e->link);
    }
    char norm[2048]; if (norm_path(norm, sizeof norm, joined) != 0) return NULL;
    return resolve_link(c, find_ent(c, norm), depth + 1);
}

/* ---- format detection ------------------------------------------------------ */
static int zip_has(mz_zip_archive *z, const char *name) {
    return mz_zip_reader_locate_file(z, name, NULL, 0) >= 0;
}

int fw_detect(const char *file, fw_info *out) {
    memset(out, 0, sizeof *out);
    size_t n = 0; unsigned char *buf = slurp(file, &n);
    if (!buf) { snprintf(out->detail, sizeof out->detail, "cannot read %s", file); return 0; }
    const char *base = strrchr(file, '/'); const char *b2 = strrchr(file, '\\');
    if (b2 > base) base = b2; base = base ? base + 1 : file;

    int rc = 0;
    if (n >= 4 && buf[0] == 'P' && buf[1] == 'K') {
        mz_zip_archive z; memset(&z, 0, sizeof z);
        if (mz_zip_reader_init_mem(&z, buf, n, 0)) {
            if (zip_has(&z, "gp2xfs.tar.gz")) {
                snprintf(out->format, sizeof out->format, "zip");
                int f200 = strcasestr_ci(base, "f200") || strcasestr_ci(base, "f-200");
                snprintf(out->device, sizeof out->device, "%s", f200 ? "f200" : "f100");
                snprintf(out->detail, sizeof out->detail,
                         "GP2X %s full firmware (gp2xfs.tar.gz)", f200 ? "F200" : "F100");
                out->ok = rc = 1;
            } else if (zip_has(&z, "yaffs2_rfs.img")) {
                snprintf(out->format, sizeof out->format, "zip");
                snprintf(out->device, sizeof out->device, "caanoo");
                snprintf(out->detail, sizeof out->detail, "Caanoo firmware (yaffs2_rfs.img)");
                out->ok = rc = 1;
            } else {
                snprintf(out->detail, sizeof out->detail, "unrecognised zip (no gp2xfs/yaffs2)");
            }
            mz_zip_reader_end(&z);
        } else snprintf(out->detail, sizeof out->detail, "corrupt zip");
    } else if (n >= 4 && !memcmp(buf, "UBI#", 4)) {
        snprintf(out->format, sizeof out->format, "ubi");
        snprintf(out->device, sizeof out->device, "wiz");
        snprintf(out->detail, sizeof out->detail, "Wiz UBI/UBIFS firmware image");
        out->ok = rc = 1;
    } else if (strcasestr_ci(base, "yaffs")) {
        snprintf(out->format, sizeof out->format, "yaffs2");
        snprintf(out->device, sizeof out->device, "caanoo");
        snprintf(out->detail, sizeof out->detail, "Caanoo YAFFS2 rootfs image");
        out->ok = rc = 1;
    } else {
        snprintf(out->detail, sizeof out->detail, "unrecognised firmware format");
    }
    free(buf);
    return rc;
}

/* ---- staging --------------------------------------------------------------- */
/* Write a collected entry set to destdir: dirs+files first, then symlinks/hardlinks deref'd to
   real copies. The collect may merge several sources (e.g. yaffs base + tar overlay). */
static int stage_collect(struct collect *c, const char *destdir, fw_progress cb, void *ud) {
    if (c->err) return -1;
    prog(cb, ud, "writing files", 65);
    int files = 0, links = 0, fail = 0;
    char path[PATH_MAX];
    for (int i = 0; i < c->n; i++) {
        struct ent *e = &c->e[i];
        snprintf(path, sizeof path, "%s/%s", destdir, e->path);
        if (e->type == TAR_DIR) mkdirs(path);
        else if (e->type == TAR_FILE) { if (write_file(path, e->data, e->size) == 0) files++; else fail++; }
        if ((i & 1023) == 0) prog(cb, ud, "writing files", 65 + (i * 25) / (c->n ? c->n : 1));
    }
    prog(cb, ud, "resolving links", 92);
    for (int i = 0; i < c->n; i++) {
        struct ent *e = &c->e[i];
        if (e->type != TAR_SYMLINK && e->type != TAR_HARDLINK) continue;
        const struct ent *t = resolve_link(c, i, 0);
        if (!t) continue;   /* dangling link (e.g. into /dev or /proc) -- skip */
        snprintf(path, sizeof path, "%s/%s", destdir, e->path);
        if (write_file(path, t->data, t->size) == 0) links++;
    }
    char msg[128]; snprintf(msg, sizeof msg, "staged %d files + %d links", files, links);
    prog(cb, ud, msg, 100);
    return fail > files ? -1 : 0;
}

int fw_stage(const char *file, const char *device, const char *destdir, fw_progress cb, void *ud) {
    fw_info info;
    if (!fw_detect(file, &info)) { prog(cb, ud, "unrecognised firmware", 0); return -1; }
    if (device && device[0] && strcasecmp(device, info.device) != 0)
        snprintf(info.device, sizeof info.device, "%s", device);   /* caller override */

    prog(cb, ud, "reading firmware", 5);
    size_t n = 0; unsigned char *buf = slurp(file, &n);
    if (!buf) { prog(cb, ud, "cannot read file", 0); return -1; }
    mkdirs(destdir);

    struct collect c; memset(&c, 0, sizeof c);
    int rc = -2;   /* default: format not yet supported */
    if (!strcmp(info.format, "zip")) {
        mz_zip_archive z; memset(&z, 0, sizeof z);
        if (!mz_zip_reader_init_mem(&z, buf, n, 0)) { free(buf); prog(cb, ud, "corrupt zip", 0); return -1; }
        if (zip_has(&z, "gp2xfs.tar.gz")) {
            /* GP2X full firmware: base glibc/ld.so in gp2xyaffs.img (YAFFS), then the lib/app
               overlay in gp2xfs.tar.gz on top (later entries win on disk). */
            size_t yl = 0;
            unsigned char *yimg = mz_zip_reader_extract_file_to_heap(&z, "gp2xyaffs.img", &yl, 0);
            if (yimg) { prog(cb, ud, "staging base (yaffs)", 25); yaffs_mem(yimg, yl, collect_cb, &c); mz_free(yimg); }
            prog(cb, ud, "extracting gp2xfs.tar.gz", 40);
            size_t gzlen = 0;
            unsigned char *gz = mz_zip_reader_extract_file_to_heap(&z, "gp2xfs.tar.gz", &gzlen, 0);
            mz_zip_reader_end(&z);
            if (gz) {
                size_t tarlen = 0; unsigned char *tar = gz_inflate(gz, gzlen, &tarlen);
                mz_free(gz);
                if (tar) { prog(cb, ud, "staging overlay (tar)", 55); untar_mem(tar, tarlen, collect_cb, &c); mz_free(tar); }
            }
            rc = stage_collect(&c, destdir, cb, ud);
        } else if (zip_has(&z, "yaffs2_rfs.img")) {
            prog(cb, ud, "extracting yaffs2_rfs.img", 20);
            size_t yl = 0;
            unsigned char *yimg = mz_zip_reader_extract_file_to_heap(&z, "yaffs2_rfs.img", &yl, 0);
            mz_zip_reader_end(&z);
            if (yimg) { prog(cb, ud, "staging rootfs (yaffs2)", 40);
                        if (yaffs_mem(yimg, yl, collect_cb, &c) == 0) rc = stage_collect(&c, destdir, cb, ud);
                        mz_free(yimg); }
            else { prog(cb, ud, "extract failed", 0); rc = -1; }
        } else { mz_zip_reader_end(&z); prog(cb, ud, "no rootfs in zip", 0); rc = -1; }
    } else if (!strcmp(info.format, "yaffs2")) {     /* a raw yaffs .img selected directly */
        prog(cb, ud, "staging rootfs (yaffs)", 30);
        if (yaffs_mem(buf, n, collect_cb, &c) == 0) rc = stage_collect(&c, destdir, cb, ud);
        else prog(cb, ud, "not a yaffs image", 0);
    } else if (!strcmp(info.format, "ubi")) {
        prog(cb, ud, "Wiz (ubifs) staging not implemented yet", 0);            /* step 6 */
    }
    collect_free(&c);
    free(buf);
    return rc;
}
