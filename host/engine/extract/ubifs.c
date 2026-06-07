/* Read-only UBI + UBIFS reader (see ubifs.h).
 *
 * UBI layer: the image is a series of PEBs (physical erase blocks). Each PEB holds an EC header
 * ('UBI#') at offset 0 and a VID header ('UBI!') at vid_hdr_offset; the LEB data follows at
 * data_offset. The VID header gives the volume id + logical block number (lnum), so the logical
 * volume is its LEBs ordered by lnum. We take the volume with the most LEBs (the rootfs).
 *
 * UBIFS layer: scan the volume's LEBs for nodes (common header magic 0x06101831; ch is
 * magic@0, crc@4, sqnum@8, len@16, node_type@20). We keep the latest (highest sqnum) version of
 * each inode (type 0), dentry (type 2) and data node (type 1), then rebuild the directory tree
 * and reassemble files (4 KiB logical blocks; per-node compression none/zlib/LZO1X). Field
 * offsets verified by byte-exact extraction of ld-2.3.6.so / libc-2.3.6.so vs the known rootfs. */
#include "ubifs.h"
#include "miniz.h"
#include "minilzo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define UBIFS_MAGIC   0x06101831u
#define BLOCK_SIZE    4096
#define N_INO 0
#define N_DATA 1
#define N_DENT 2
#define S_IFMT_  0170000
#define S_IFDIR_ 0040000
#define S_IFREG_ 0100000
#define S_IFLNK_ 0120000

static uint32_t le32(const unsigned char *p) { return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24; }
static uint16_t le16(const unsigned char *p) { return p[0]|p[1]<<8; }
static uint64_t le64(const unsigned char *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p+4)<<32); }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|p[1]<<16|p[2]<<8|p[3]; }

/* ---- generic u64-keyed open-addressing hash (key 0 reserved) -------------- */
struct hmap { uint64_t *key; int *val; int cap, n; };
static void hm_init(struct hmap *h, int cap) {
    h->cap = 16; while (h->cap < cap * 2) h->cap <<= 1;
    h->key = calloc(h->cap, sizeof(uint64_t)); h->val = malloc(h->cap * sizeof(int)); h->n = 0;
}
static void hm_free(struct hmap *h) { free(h->key); free(h->val); }
static int hm_get(struct hmap *h, uint64_t k) {            /* -1 if absent */
    uint64_t i = (k * 0x9E3779B97F4A7C15ull) & (h->cap - 1);
    for (;;) { if (!h->key[i]) return -1; if (h->key[i] == k) return h->val[i]; i = (i+1) & (h->cap-1); }
}
static void hm_put(struct hmap *h, uint64_t k, int v) {
    uint64_t i = (k * 0x9E3779B97F4A7C15ull) & (h->cap - 1);
    for (;;) { if (!h->key[i] || h->key[i] == k) { h->key[i] = k; h->val[i] = v; return; } i = (i+1) & (h->cap-1); }
}

struct ino_t  { uint64_t sqnum, size; uint32_t mode, dlen; const unsigned char *inl; };
struct dent_t { uint64_t sqnum, inum; uint32_t parent; char name[256]; };
struct data_t { uint64_t sqnum; const unsigned char *p; uint32_t usize, clen, compr; };

struct ubifs {
    struct ino_t *ino; int nino, cino; struct hmap ino_h;          /* ino# -> index */
    struct dent_t *dent; int ndent, cdent;
    struct hmap dent_by_inum;                                       /* inum -> dent index (dirs) */
    struct hmap dedup;                                              /* (parent,name) hash -> dent index */
    struct data_t *data; int ndata, cdata; struct hmap data_h;      /* (ino<<32|block) -> index */
    char **path; int npath, cpath; struct hmap path_for;           /* ino -> resolved dir path */
};

static uint64_t namehash(uint32_t parent, const char *name) {
    uint64_t h = 1469598103934665603ull ^ parent;
    for (const char *s = name; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ull; }
    return h | 1;
}

static void add_ino(struct ubifs *u, uint32_t num, const unsigned char *node, uint32_t len) {
    uint64_t sq = le64(node + 8);
    int idx = hm_get(&u->ino_h, num + 1);
    if (idx >= 0 && u->ino[idx].sqnum >= sq) return;
    if (idx < 0) {
        if (u->nino >= u->cino) { u->cino = u->cino ? u->cino*2 : 1024; u->ino = realloc(u->ino, u->cino*sizeof*u->ino); }
        idx = u->nino++; hm_put(&u->ino_h, num + 1, idx);
    }
    struct ino_t *o = &u->ino[idx];
    o->sqnum = sq; o->size = le64(node + 48); o->mode = le32(node + 104); o->dlen = le32(node + 112);
    o->inl = (len >= 160 + o->dlen) ? node + 160 : NULL;
}
static void add_dent(struct ubifs *u, const unsigned char *node, uint32_t len) {
    uint64_t sq = le64(node + 8);
    uint32_t parent = le32(node + 24); uint64_t inum = le64(node + 40);
    uint16_t nlen = le16(node + 50);
    if (nlen > 255 || 56 + nlen > len) return;
    char name[256]; memcpy(name, node + 56, nlen); name[nlen] = 0;    /* @56: cookie(4) precedes name */
    if (!name[0]) return;
    uint64_t dk = namehash(parent, name);
    int ex = hm_get(&u->dedup, dk);
    if (ex >= 0 && u->dent[ex].sqnum >= sq) return;
    int idx;
    if (ex >= 0) idx = ex;
    else { if (u->ndent >= u->cdent) { u->cdent = u->cdent ? u->cdent*2 : 1024; u->dent = realloc(u->dent, u->cdent*sizeof*u->dent); }
           idx = u->ndent++; hm_put(&u->dedup, dk, idx); }
    struct dent_t *de = &u->dent[idx];
    de->sqnum = sq; de->inum = inum; de->parent = parent; memcpy(de->name, name, nlen + 1);
    int bi = hm_get(&u->dent_by_inum, inum + 1);                      /* track one dentry per inum (dirs) */
    if (bi < 0 || u->dent[bi].sqnum < sq) hm_put(&u->dent_by_inum, inum + 1, idx);
}
static void add_data(struct ubifs *u, const unsigned char *node, uint32_t len) {
    uint64_t sq = le64(node + 8);
    uint32_t ino = le32(node + 24), block = le32(node + 28) & 0x1FFFFFFFu;
    uint64_t k = ((uint64_t)ino << 32) | block;
    int idx = hm_get(&u->data_h, k + 1);
    if (idx >= 0 && u->data[idx].sqnum >= sq) return;
    if (idx < 0) {
        if (u->ndata >= u->cdata) { u->cdata = u->cdata ? u->cdata*2 : 4096; u->data = realloc(u->data, u->cdata*sizeof*u->data); }
        idx = u->ndata++; hm_put(&u->data_h, k + 1, idx);
    }
    struct data_t *dn = &u->data[idx];
    dn->sqnum = sq; dn->usize = le32(node + 40); dn->compr = le16(node + 44);
    dn->p = node + 48; dn->clen = len - 48;
}

/* decompress one data block into out (cap bytes); returns bytes produced */
static uint32_t decomp(const struct data_t *dn, unsigned char *out, uint32_t cap) {
    uint32_t want = dn->usize < cap ? dn->usize : cap;
    if (dn->compr == 0) { memcpy(out, dn->p, want); return want; }       /* none */
    if (dn->compr == 2) {                                                /* zlib */
        size_t got = tinfl_decompress_mem_to_mem(out, cap, dn->p, dn->clen, TINFL_FLAG_PARSE_ZLIB_HEADER);
        return got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ? 0 : (uint32_t)got;
    }
    if (dn->compr == 1) {                                                /* LZO1X */
        lzo_uint outlen = cap;
        if (lzo1x_decompress_safe(dn->p, dn->clen, out, &outlen, NULL) == LZO_E_OK) return (uint32_t)outlen;
        return 0;
    }
    return 0;
}

static const char *path_of(struct ubifs *u, uint32_t ino, int depth) {
    if (ino == 1) return "";                                             /* root */
    int pi = hm_get(&u->path_for, ino + 1);
    if (pi >= 0) return u->path[pi];
    if (depth > 64) return NULL;
    int di = hm_get(&u->dent_by_inum, ino + 1);
    if (di < 0) return NULL;
    struct dent_t *de = &u->dent[di];
    const char *pp = path_of(u, de->parent, depth + 1);
    if (!pp) return NULL;
    char buf[2048];
    if (pp[0]) snprintf(buf, sizeof buf, "%s/%s", pp, de->name);
    else snprintf(buf, sizeof buf, "%s", de->name);
    if (u->npath >= u->cpath) { u->cpath = u->cpath ? u->cpath*2 : 1024; u->path = realloc(u->path, u->cpath*sizeof(char*)); }
    int slot = u->npath++;
    u->path[slot] = strdup(buf);
    hm_put(&u->path_for, ino + 1, slot);
    return u->path[slot];
}

int ubifs_mem(const unsigned char *buf, size_t len, tar_cb cb, void *ud) {
    if (len < 4096 || memcmp(buf, "UBI#", 4) != 0) return -1;
    /* detect PEB size: smallest power-of-two-ish block where the next block also starts with UBI# */
    size_t peb = 0;
    size_t cands[] = { 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152 };
    for (unsigned i = 0; i < sizeof cands/sizeof cands[0]; i++)
        if (len % cands[i] == 0 && len > cands[i] && !memcmp(buf + cands[i], "UBI#", 4)) { peb = cands[i]; break; }
    if (!peb) { for (unsigned i = 0; i < sizeof cands/sizeof cands[0]; i++) if (len % cands[i] == 0) { peb = cands[i]; break; } }
    if (!peb) return -1;
    uint32_t vid_off = be32(buf + 16), data_off = be32(buf + 20);
    if (vid_off < 64 || data_off <= vid_off || data_off >= peb) { vid_off = 2048; data_off = 4096; }
    size_t leb_size = peb - data_off, npeb = len / peb;

    /* collect LEBs per volume */
    struct leb { uint32_t lnum; const unsigned char *p; };
    struct vol { uint32_t vol_id; struct leb *l; int n, c; };
    struct vol *vols = NULL; int nvol = 0, cvol = 0;
    for (size_t p = 0; p < npeb; p++) {
        const unsigned char *pb = buf + p * peb;
        if (memcmp(pb, "UBI#", 4)) continue;
        const unsigned char *vid = pb + vid_off;
        if (memcmp(vid, "UBI!", 4)) continue;
        uint32_t vol_id = be32(vid + 8), lnum = be32(vid + 12);
        int vi = -1; for (int i = 0; i < nvol; i++) if (vols[i].vol_id == vol_id) { vi = i; break; }
        if (vi < 0) { if (nvol >= cvol) { cvol = cvol ? cvol*2 : 8; vols = realloc(vols, cvol*sizeof*vols); }
                      vi = nvol++; vols[vi].vol_id = vol_id; vols[vi].l = NULL; vols[vi].n = vols[vi].c = 0; }
        struct vol *v = &vols[vi];
        if (v->n >= v->c) { v->c = v->c ? v->c*2 : 64; v->l = realloc(v->l, v->c*sizeof*v->l); }
        v->l[v->n].lnum = lnum; v->l[v->n].p = pb + data_off; v->n++;
    }
    int best = -1; for (int i = 0; i < nvol; i++) if (best < 0 || vols[i].n > vols[best].n) best = i;
    if (best < 0) { free(vols); return -1; }

    struct ubifs u; memset(&u, 0, sizeof u);
    hm_init(&u.ino_h, 4096); hm_init(&u.dent_by_inum, 4096); hm_init(&u.dedup, 4096);
    hm_init(&u.data_h, 1 << 16); hm_init(&u.path_for, 4096);
    lzo_init();

    /* scan each LEB of the chosen volume for nodes */
    for (int i = 0; i < vols[best].n; i++) {
        const unsigned char *leb = vols[best].l[i].p;
        size_t off = 0;
        while (off + 24 <= leb_size) {
            if (le32(leb + off) != UBIFS_MAGIC) { off += 8; continue; }
            uint32_t nlen = le32(leb + off + 16); uint8_t ntype = leb[off + 20];
            if (nlen < 24 || off + nlen > leb_size) { off += 8; continue; }
            const unsigned char *node = leb + off;
            if (ntype == N_INO && nlen >= 160) {
                uint32_t num = le32(node + 24); add_ino(&u, num, node, nlen);
            } else if (ntype == N_DENT && nlen >= 56) {
                add_dent(&u, node, nlen);
            } else if (ntype == N_DATA && nlen >= 48) {
                add_data(&u, node, nlen);
            }
            off += (nlen + 7) & ~7u;
        }
    }

    /* emit every dentry: dir / symlink / regular file (hardlinks emit the file at each path) */
    unsigned char *fb = NULL; size_t fbcap = 0;
    int rc = 0;
    for (int i = 0; i < u.ndent && rc == 0; i++) {
        struct dent_t *de = &u.dent[i];
        int ii = hm_get(&u.ino_h, de->inum + 1);
        if (ii < 0) continue;
        struct ino_t *o = &u.ino[ii];
        const char *dir = path_of(&u, de->parent, 0);
        if (!dir) continue;
        char full[2048];
        if (dir[0]) snprintf(full, sizeof full, "%s/%s", dir, de->name);
        else snprintf(full, sizeof full, "%s", de->name);
        uint32_t fmt = o->mode & S_IFMT_;
        if (fmt == S_IFDIR_) {
            rc = cb(ud, full, TAR_DIR, "", NULL, 0, o->mode);
        } else if (fmt == S_IFLNK_) {
            char tgt[1024]; uint32_t tl = o->dlen < sizeof tgt - 1 ? o->dlen : sizeof tgt - 1;
            if (o->inl) { memcpy(tgt, o->inl, tl); tgt[tl] = 0; } else tgt[0] = 0;
            rc = cb(ud, full, TAR_SYMLINK, tgt, NULL, 0, o->mode);
        } else if (fmt == S_IFREG_) {
            uint32_t sz = (uint32_t)o->size;
            if (sz > fbcap) { fbcap = sz; fb = realloc(fb, fbcap ? fbcap : 1); }
            if (sz) memset(fb, 0, sz);
            uint32_t nblk = (sz + BLOCK_SIZE - 1) / BLOCK_SIZE;
            for (uint32_t b = 0; b < nblk; b++) {
                int dgi = hm_get(&u.data_h, (((uint64_t)de->inum << 32) | b) + 1);
                if (dgi < 0) continue;
                uint32_t avail = sz - b * BLOCK_SIZE; if (avail > BLOCK_SIZE) avail = BLOCK_SIZE;
                decomp(&u.data[dgi], fb + b * BLOCK_SIZE, avail);
            }
            rc = cb(ud, full, TAR_FILE, "", sz ? fb : (const unsigned char *)"", sz, o->mode);
        }
        /* device/fifo nodes skipped */
    }

    free(fb);
    for (int i = 0; i < u.npath; i++) free(u.path[i]); free(u.path);
    free(u.ino); free(u.dent); free(u.data);
    hm_free(&u.ino_h); hm_free(&u.dent_by_inum); hm_free(&u.dedup); hm_free(&u.data_h); hm_free(&u.path_for);
    for (int i = 0; i < nvol; i++) free(vols[i].l);
    free(vols);
    return rc;
}
