/* Read-only YAFFS1 / YAFFS2 image reader. Geometry is auto-detected from the image length:
 *   YAFFS2 = 2048-byte page + 64-byte spare (e.g. Caanoo yaffs2_rfs.img)
 *   YAFFS1 =  512-byte page + 16-byte spare (e.g. GP2X gp2xyaffs.img -- the base glibc)
 *
 * Tags (which object/chunk a page belongs to) live in the spare:
 *   YAFFS2: PackedTags2 -- plain u32s: spare[0..3]=seq, [4..7]=objId, [8..11]=chunkId, [12..15]=nBytes.
 *   YAFFS1: yaffs_Spare -- 8 tag bytes at spare[0,1,2,3,6,7,8,9] forming two words:
 *           word0 = chunkId:20 | serial:2 | nBytes:10 ; word1 = objId | ecc (objId is the low 16
 *           bits -- the upper bits are tag ECC, verified empirically: a 16-bit mask makes every
 *           file in gp2xyaffs.img reassemble to its exact header size).
 * A chunk with chunkId==0 is an object header (yaffs_obj_hdr in the page data); chunkId>=1 is file
 * data. The object header layout (natural ARM alignment) puts type@0, parent@4, name@10,
 * file_size@292, equiv_id@296, alias@300 -- confirmed by exact size matches on extraction. */
#include "yaffs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Y_ROOT 1            /* objId of the root directory */
#define NAME_OFF 10
#define MODE_OFF 268
#define SIZE_OFF 292
#define EQUIV_OFF 296
#define ALIAS_OFF 300

static unsigned le32(const unsigned char *p) { return p[0] | p[1]<<8 | p[2]<<16 | (unsigned)p[3]<<24; }

struct chunk { unsigned cid; const unsigned char *data; unsigned nb; };
struct yobj {
    int used, type; unsigned parent, size, equiv, mode;
    char name[256], alias[160];
    struct chunk *ch; int nch, cch;
    char *path;   /* resolved lazily */
};

struct ytab { struct yobj *o; unsigned *id; int n, cap; int *idx; int icap; };  /* id[i]=objId of o[i]; idx=hash */

static int tab_find(struct ytab *t, unsigned objid) {
    unsigned h = objid & (t->icap - 1);
    for (;;) {
        int slot = t->idx[h];
        if (slot < 0) return -1;
        if (t->id[slot] == objid) return slot;
        h = (h + 1) & (t->icap - 1);
    }
}
static int tab_get(struct ytab *t, unsigned objid) {
    int s = tab_find(t, objid);
    if (s >= 0) return s;
    if (t->n >= t->cap) {
        int nc = t->cap ? t->cap * 2 : 1024;
        t->o = realloc(t->o, nc * sizeof *t->o);
        t->id = realloc(t->id, nc * sizeof *t->id);
        memset(t->o + t->cap, 0, (nc - t->cap) * sizeof *t->o);
        t->cap = nc;
    }
    s = t->n++;
    t->id[s] = objid; t->o[s].used = 1;
    unsigned h = objid & (t->icap - 1);
    while (t->idx[h] >= 0) h = (h + 1) & (t->icap - 1);
    t->idx[h] = s;
    return s;
}

/* Build the full path for object index s by walking parents to the root. Cached in o->path. */
static const char *obj_path(struct ytab *t, int s, int depth) {
    struct yobj *o = &t->o[s];
    if (o->path) return o->path;
    if (depth > 64) return NULL;
    char buf[2048];
    if (t->id[s] == Y_ROOT || o->parent == 0 || o->parent == Y_ROOT) {
        snprintf(buf, sizeof buf, "%s", o->name);
    } else {
        int ps = tab_find(t, o->parent);
        const char *pp = ps >= 0 ? obj_path(t, ps, depth + 1) : NULL;
        if (pp && pp[0]) snprintf(buf, sizeof buf, "%s/%s", pp, o->name);
        else snprintf(buf, sizeof buf, "%s", o->name);
    }
    o->path = strdup(buf);
    return o->path;
}

int yaffs_mem(const unsigned char *buf, size_t len, tar_cb cb, void *ud) {
    int page, spare, yaffs2;
    if (len % 2112 == 0)      { page = 2048; spare = 64; yaffs2 = 1; }
    else if (len % 528 == 0)  { page = 512;  spare = 16; yaffs2 = 0; }
    else return -1;
    size_t step = page + spare, n = len / step;

    struct ytab t; memset(&t, 0, sizeof t);
    t.icap = 1024; while ((size_t)t.icap < n * 2) t.icap <<= 1;
    t.idx = malloc(t.icap * sizeof(int));
    for (int i = 0; i < t.icap; i++) t.idx[i] = -1;

    for (size_t c = 0; c < n; c++) {
        const unsigned char *d = buf + c * step;
        const unsigned char *sp = d + page;
        unsigned cid, objid, nb;
        if (yaffs2) {
            objid = le32(sp + 4); cid = le32(sp + 8); nb = le32(sp + 12);
            if (objid == 0 || objid == 0xffffffffu) continue;
        } else {
            unsigned w0 = sp[0] | sp[1]<<8 | sp[2]<<16 | (unsigned)sp[3]<<24;   /* chunkId|serial|nBytes */
            unsigned w1 = sp[6] | sp[7]<<8 | sp[8]<<16 | (unsigned)sp[9]<<24;   /* objId|ecc */
            if (w0 == 0 && w1 == 0) continue;
            if (sp[4] == 0x00) continue;                 /* pageStatus 0 = deleted */
            cid = w0 & 0xfffff; nb = (w0 >> 22) & 0x3ff;
            objid = w1 & 0xffff;                         /* low 16 bits; upper bits are tag ECC */
            if (objid == 0) continue;
        }
        if (nb > (unsigned)page) nb = page;
        int s = tab_get(&t, objid);
        struct yobj *o = &t.o[s];
        if (cid == 0) {                                  /* object header */
            o->type = (int)le32(d + 0);
            o->parent = le32(d + 4);
            memcpy(o->name, d + NAME_OFF, 255); o->name[255] = 0;
            o->mode = le32(d + MODE_OFF);
            o->size = le32(d + SIZE_OFF);
            o->equiv = le32(d + EQUIV_OFF);
            memcpy(o->alias, d + ALIAS_OFF, 159); o->alias[159] = 0;
        } else {                                         /* data chunk: latest write of (obj,cid) wins */
            int found = -1;
            for (int i = o->nch - 1; i >= 0; i--) if (o->ch[i].cid == cid) { found = i; break; }
            if (found < 0) {
                if (o->nch >= o->cch) { o->cch = o->cch ? o->cch * 2 : 16;
                                        o->ch = realloc(o->ch, o->cch * sizeof *o->ch); }
                found = o->nch++;
            }
            o->ch[found].cid = cid; o->ch[found].data = d; o->ch[found].nb = nb;
        }
    }

    /* sort each object's chunks by id (insertion sort -- runs are short / nearly ordered) */
    for (int s = 0; s < t.n; s++) {
        struct yobj *o = &t.o[s];
        for (int i = 1; i < o->nch; i++) {
            struct chunk k = o->ch[i]; int j = i - 1;
            while (j >= 0 && o->ch[j].cid > k.cid) { o->ch[j+1] = o->ch[j]; j--; }
            o->ch[j+1] = k;
        }
    }

    int rc = 0;
    for (int s = 0; s < t.n && rc == 0; s++) {
        struct yobj *o = &t.o[s];
        if (!o->used || t.id[s] == Y_ROOT) continue;
        const char *path = obj_path(&t, s, 0);
        if (!path || !path[0]) continue;
        if (o->type == 3) {                              /* directory */
            rc = cb(ud, path, TAR_DIR, "", NULL, 0, o->mode);
        } else if (o->type == 2) {                       /* symlink */
            rc = cb(ud, path, TAR_SYMLINK, o->alias, NULL, 0, o->mode);
        } else if (o->type == 4) {                       /* hardlink -> resolve equiv obj to a path */
            int es = tab_find(&t, o->equiv);
            const char *tp = es >= 0 ? obj_path(&t, es, 0) : NULL;
            if (tp) { char abs[2048]; snprintf(abs, sizeof abs, "/%s", tp);
                      rc = cb(ud, path, TAR_HARDLINK, abs, NULL, 0, o->mode); }
        } else if (o->type == 1) {                       /* regular file -> reassemble */
            unsigned sz = o->size;
            unsigned char *blob = sz ? malloc(sz) : NULL;
            unsigned pos = 0;
            for (int i = 0; i < o->nch && pos < sz; i++) {
                unsigned want = sz - pos; if (want > o->ch[i].nb) want = o->ch[i].nb;
                if (blob) memcpy(blob + pos, o->ch[i].data, want);
                pos += want;
            }
            rc = cb(ud, path, TAR_FILE, "", blob ? blob : (const unsigned char *)"", pos, o->mode);
            free(blob);
        }
        /* type 5 (special/device nodes) skipped */
    }

    for (int s = 0; s < t.n; s++) { free(t.o[s].ch); free(t.o[s].path); }
    free(t.o); free(t.id); free(t.idx);
    return rc;
}
