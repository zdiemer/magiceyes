/* magiceyes savestate container (.mst): the byte layer. See state_file.h for the format and for
 * why this is its own translation unit (the slot picker in bin/viewer must read a state's
 * thumbnail without linking the engine).
 *
 * Style follows host/png_write.c, which is the repo's other hand-rolled binary writer: explicit
 * per-byte serialisation rather than fwrite of a struct, one framing primitive every chunk goes
 * through, a lazily-built CRC table, and every read/write return-checked with a 0/-1 API. The
 * CRC table is duplicated here rather than shared with png_write.c on purpose: both link into the
 * same binaries, png_write.c's table is static, and a savestate must stay readable in builds that
 * have no PNG writer at all.
 *
 * The one thing worth stating plainly: this reader's job is to REFUSE bad input, not to accept
 * good input. A savestate is the only file magiceyes reads back into itself, so a framing bug
 * does not produce a bad screenshot, it produces a plausible-looking machine that diverges. Every
 * length is validated against the actual file size before it is used to allocate or read.
 */
#include "state_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MST_NO_COMPRESS
#include "miniz.h"
#endif

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static const uint8_t MST_MAGIC[8] = { 0x4D, 0x45, 0x53, 0x54, 0x1A, 0x0A, 0x0D, 0x0A };

const char *mst_strerror(int err) {
    switch (err) {
    case MST_OK:               return "ok";
    case MST_ERR_IO:           return "file could not be read or written";
    case MST_ERR_MAGIC:        return "not a magiceyes savestate";
    case MST_ERR_HEADER_CRC:   return "savestate header is damaged";
    case MST_ERR_NEWER_FORMAT: return "savestate was written by a newer magiceyes";
    case MST_ERR_TRUNCATED:    return "savestate is truncated";
    case MST_ERR_CHUNK_CRC:    return "savestate contents are damaged";
    case MST_ERR_COMPRESSED:   return "savestate is compressed and this build cannot decompress";
    case MST_ERR_BAD_LEN:      return "savestate declares a length its contents do not match";
    case MST_ERR_NO_END:       return "savestate is incomplete (no end marker)";
    case MST_ERR_MEM:          return "out of memory";
    case MST_ERR_RANGE:        return "slot number out of range";
    default:                   return "unknown savestate error";
    }
}

/* ---- CRC-32 (same polynomial and lazy table as png_write.c) ---------------- */
static uint32_t g_crc_tab[256];
static int g_crc_ready = 0;
static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : (c >> 1);
        g_crc_tab[n] = c;
    }
    g_crc_ready = 1;
}
static uint32_t crc_upd(uint32_t crc, const void *buf, size_t len) {
    const uint8_t *p = buf;
    if (!g_crc_ready) crc_init();
    for (size_t i = 0; i < len; i++) crc = g_crc_tab[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc;
}
static uint32_t crc_of2(const void *a, size_t na, const void *b, size_t nb) {
    uint32_t c = 0xffffffffu;
    c = crc_upd(c, a, na);
    if (nb) c = crc_upd(c, b, nb);
    return c ^ 0xffffffffu;
}

/* ---- little-endian field access -------------------------------------------- */
static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void put_le64(uint8_t *p, uint64_t v) { put_le32(p, (uint32_t)v); put_le32(p + 4, (uint32_t)(v >> 32)); }
static uint16_t get_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get_le64(const uint8_t *p) { return (uint64_t)get_le32(p) | ((uint64_t)get_le32(p + 4) << 32); }

static void hdr_pack(uint8_t *h, const struct mst_info *in) {
    memset(h, 0, MST_HEADER_BYTES);
    memcpy(h, MST_MAGIC, 8);
    put_le16(h + 8,  MST_FORMAT_VERSION);
    put_le16(h + 10, MST_HEADER_BYTES);
    /* h+12 is the header CRC, filled last (and excluded from its own input) */
    put_le32(h + 16, in->content_flags);
    put_le64(h + 20, in->game_key);
    put_le64(h + 28, (uint64_t)in->save_time);
    put_le32(h + 36, in->device);
    put_le32(h + 40, in->engine_abi);
    put_le32(h + 44, in->frame_seq);
    put_le16(h + 48, in->fb_w);
    put_le16(h + 50, in->fb_h);
    put_le16(h + 52, in->thumb_w);
    put_le16(h + 54, in->thumb_h);
    put_le32(h + 56, in->chunk_count);
    put_le32(h + 12, crc_of2(h, 12, h + 16, MST_HEADER_BYTES - 16));
}

static void hdr_unpack(const uint8_t *h, struct mst_info *out) {
    out->format_version = get_le16(h + 8);
    out->header_bytes   = get_le16(h + 10);
    out->content_flags  = get_le32(h + 16);
    out->game_key       = get_le64(h + 20);
    out->save_time      = (int64_t)get_le64(h + 28);
    out->device         = get_le32(h + 36);
    out->engine_abi     = get_le32(h + 40);
    out->frame_seq      = get_le32(h + 44);
    out->fb_w           = get_le16(h + 48);
    out->fb_h           = get_le16(h + 50);
    out->thumb_w        = get_le16(h + 52);
    out->thumb_h        = get_le16(h + 54);
    out->chunk_count    = get_le32(h + 56);
}

/* ---- writer ----------------------------------------------------------------- */
struct mst_w {
    FILE *f;
    char *path;      /* final destination */
    char *tmp;       /* what we are actually writing */
    uint32_t nchunk;
    uint32_t flags;  /* accumulated content_flags (COMPRESSED is set as it happens) */
    struct mst_info info;
    int failed;
};

struct mst_w *mst_create(const char *path, const struct mst_info *info) {
    if (!path || !info) return NULL;
    struct mst_w *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    size_t n = strlen(path);
    w->path = malloc(n + 1);
    w->tmp  = malloc(n + 5);
    if (!w->path || !w->tmp) { free(w->path); free(w->tmp); free(w); return NULL; }
    memcpy(w->path, path, n + 1);
    memcpy(w->tmp, path, n);
    memcpy(w->tmp + n, ".tmp", 5);
    w->info = *info;
    w->flags = info->content_flags;
    w->f = fopen(w->tmp, "wb");
    if (!w->f) { free(w->path); free(w->tmp); free(w); return NULL; }
    uint8_t h[MST_HEADER_BYTES];
    hdr_pack(h, &w->info);                      /* rewritten by mst_finish with the real counts */
    if (fwrite(h, 1, sizeof h, w->f) != sizeof h) w->failed = 1;
    return w;
}

int mst_chunk(struct mst_w *w, const char *type, const void *data, size_t len, int compress) {
    if (!w || w->failed || !type || strlen(type) != 4) return MST_ERR_IO;
    if (len > 0xffffffffu) return MST_ERR_IO;

    const uint8_t *payload = data;
    uint8_t *deflated = NULL;
    size_t stored = len;
    int enc = MST_ENC_STORED;

#ifndef MST_NO_COMPRESS
    /* Compress only when it actually pays. Guest RAM and pram are mostly zeros and shrink
       enormously; a thumbnail or a short META does not, and paying a deflate pass to grow a
       38 KB buffer would be silly. A chunk that fails to shrink is stored, which keeps the
       reader's job identical either way. */
    if (compress && len > 512) {
        mz_ulong cap = mz_compressBound((mz_ulong)len);
        deflated = malloc(cap ? cap : 1);
        if (deflated) {
            mz_ulong got = cap;
            if (mz_compress2(deflated, &got, payload, (mz_ulong)len, MZ_DEFAULT_COMPRESSION) == MZ_OK
                && got < len) {
                payload = deflated; stored = got; enc = MST_ENC_DEFLATE;
                w->flags |= MST_F_COMPRESSED;
            } else { free(deflated); deflated = NULL; }
        }
    }
#else
    (void)compress;
#endif

    uint8_t ch[MST_CHUNK_HDR_BYTES];
    memset(ch, 0, sizeof ch);
    memcpy(ch, type, 4);
    ch[4] = (uint8_t)enc;
    put_le32(ch + 8,  (uint32_t)stored);
    put_le32(ch + 12, (uint32_t)len);
    put_le32(ch + 16, crc_of2(ch, 16, payload, stored));

    int ok = fwrite(ch, 1, sizeof ch, w->f) == sizeof ch
          && (stored == 0 || fwrite(payload, 1, stored, w->f) == stored);
    free(deflated);
    if (!ok) { w->failed = 1; return MST_ERR_IO; }
    w->nchunk++;
    return MST_OK;
}

int mst_finish(struct mst_w *w) {
    if (!w) return MST_ERR_IO;
    int rc = MST_OK;
    if (w->failed) rc = MST_ERR_IO;

    if (rc == MST_OK && mst_chunk(w, "END ", NULL, 0, 0) != MST_OK) rc = MST_ERR_IO;

    if (rc == MST_OK) {
        /* Patch the real chunk count and accumulated flags into the header now that both are
           known, then re-derive the header CRC over the final bytes. */
        w->info.chunk_count   = w->nchunk;
        w->info.content_flags = w->flags;
        uint8_t h[MST_HEADER_BYTES];
        hdr_pack(h, &w->info);
        if (fseek(w->f, 0, SEEK_SET) != 0 || fwrite(h, 1, sizeof h, w->f) != sizeof h)
            rc = MST_ERR_IO;
    }
    if (rc == MST_OK && fflush(w->f) != 0) rc = MST_ERR_IO;
#ifndef _WIN32
    if (rc == MST_OK) fsync(fileno(w->f));      /* best effort: survive a power loss, not a bug */
#else
    if (rc == MST_OK) _commit(_fileno(w->f));
#endif
    if (fclose(w->f) != 0) rc = MST_ERR_IO;
    w->f = NULL;

    if (rc == MST_OK) {
        /* remove() first: MinGW's rename() fails when the destination exists, unlike POSIX.
           This is the only moment the previous state is gone, and it is immediately followed by
           the rename of a file already fully written and flushed. */
        remove(w->path);
        if (rename(w->tmp, w->path) != 0) rc = MST_ERR_IO;
    }
    if (rc != MST_OK) remove(w->tmp);
    free(w->path); free(w->tmp); free(w);
    return rc;
}

void mst_abort(struct mst_w *w) {
    if (!w) return;
    if (w->f) fclose(w->f);
    remove(w->tmp);
    free(w->path); free(w->tmp); free(w);
}

/* ---- reader ----------------------------------------------------------------- */
struct mst_r {
    FILE *f;
    long  size;      /* the real file length: every declared length is checked against it */
    int   saw_end;
};

static long file_size(FILE *f) {
    long cur = ftell(f);
    if (cur < 0 || fseek(f, 0, SEEK_END) != 0) return -1;
    long n = ftell(f);
    if (fseek(f, cur, SEEK_SET) != 0) return -1;
    return n;
}

struct mst_r *mst_open(const char *path, struct mst_info *info, int *err) {
    int e = MST_OK;
    struct mst_r *r = NULL;
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) { e = MST_ERR_IO; goto out; }

    uint8_t h[MST_HEADER_BYTES];
    if (fread(h, 1, sizeof h, f) != sizeof h) { e = MST_ERR_MAGIC; goto out; }
    if (memcmp(h, MST_MAGIC, 8) != 0)         { e = MST_ERR_MAGIC; goto out; }
    if (crc_of2(h, 12, h + 16, MST_HEADER_BYTES - 16) != get_le32(h + 12)) {
        e = MST_ERR_HEADER_CRC; goto out;
    }
    struct mst_info tmp;
    memset(&tmp, 0, sizeof tmp);
    hdr_unpack(h, &tmp);
    /* A NEWER container is refused rather than guessed at. Reading it with this version's rules
       would produce a machine that looks restored and is not. */
    if (tmp.format_version > MST_FORMAT_VERSION) { e = MST_ERR_NEWER_FORMAT; goto out; }
    if (tmp.header_bytes < MST_HEADER_BYTES)     { e = MST_ERR_TRUNCATED;    goto out; }
    /* A future version may have grown the header; skip whatever we do not understand. */
    if (tmp.header_bytes > MST_HEADER_BYTES
        && fseek(f, (long)tmp.header_bytes, SEEK_SET) != 0) { e = MST_ERR_TRUNCATED; goto out; }

    r = calloc(1, sizeof *r);
    if (!r) { e = MST_ERR_MEM; goto out; }
    r->f = f;
    r->size = file_size(f);
    if (r->size < 0) { free(r); r = NULL; e = MST_ERR_IO; goto out; }
    f = NULL;                                   /* owned by r now */
    if (info) *info = tmp;

out:
    if (f) fclose(f);
    if (err) *err = e;
    return r;
}

/* Read one chunk. Returns 1 on a chunk, 0 at END, <0 on error. */
int mst_next(struct mst_r *r, char type_out[5], void **data, size_t *len) {
    if (!r) return MST_ERR_IO;
    if (data) *data = NULL;
    if (len) *len = 0;
    if (r->saw_end) return 0;

    uint8_t ch[MST_CHUNK_HDR_BYTES];
    size_t got = fread(ch, 1, sizeof ch, r->f);
    if (got == 0) return MST_ERR_NO_END;        /* ran out with no END: interrupted write */
    if (got != sizeof ch) return MST_ERR_TRUNCATED;

    uint32_t stored = get_le32(ch + 8), plain = get_le32(ch + 12), want = get_le32(ch + 16);
    int enc = ch[4];
    if (type_out) { memcpy(type_out, ch, 4); type_out[4] = 0; }

    /* Validate the declared length against the file BEFORE allocating: a corrupt or hostile
       stored_len must not turn into a multi-gigabyte malloc or a read past the end. */
    long pos = ftell(r->f);
    if (pos < 0 || (long)stored > r->size - pos) return MST_ERR_TRUNCATED;

    if (!memcmp(ch, "END ", 4)) {
        if (stored != 0) return MST_ERR_TRUNCATED;
        if (crc_of2(ch, 16, NULL, 0) != want) return MST_ERR_CHUNK_CRC;
        r->saw_end = 1;
        return 0;
    }

    uint8_t *raw = malloc(stored ? stored : 1);
    if (!raw) return MST_ERR_MEM;
    if (stored && fread(raw, 1, stored, r->f) != stored) { free(raw); return MST_ERR_TRUNCATED; }
    if (crc_of2(ch, 16, raw, stored) != want) { free(raw); return MST_ERR_CHUNK_CRC; }

    if (enc == MST_ENC_DEFLATE) {
#ifdef MST_NO_COMPRESS
        free(raw);
        return MST_ERR_COMPRESSED;
#else
        uint8_t *out = malloc(plain ? plain : 1);
        if (!out) { free(raw); return MST_ERR_MEM; }
        mz_ulong n = plain;
        int zr = mz_uncompress(out, &n, raw, (mz_ulong)stored);
        free(raw);
        /* Insist the inflated size is EXACTLY what the header promised. Trusting plain_len for
           the allocation and then not checking it is the classic decompression-bomb hole, and it
           would also let a truncated payload restore as a half-filled buffer. */
        if (zr != MZ_OK || n != plain) { free(out); return MST_ERR_BAD_LEN; }
        raw = out;
#endif
    } else if (plain != stored) {
        free(raw);
        return MST_ERR_BAD_LEN;
    }

    if (data) *data = raw; else free(raw);
    if (len) *len = plain;
    return 1;
}

void mst_close(struct mst_r *r) {
    if (!r) return;
    if (r->f) fclose(r->f);
    free(r);
}

int mst_probe(const char *path, struct mst_info *info,
              char **meta, size_t *meta_len, uint8_t **thumb, size_t *thumb_len) {
    if (meta) *meta = NULL;
    if (meta_len) *meta_len = 0;
    if (thumb) *thumb = NULL;
    if (thumb_len) *thumb_len = 0;

    int err = MST_OK;
    struct mst_r *r = mst_open(path, info, &err);
    if (!r) return err;

    int have_meta = 0, have_thumb = 0, rc = MST_OK;
    for (;;) {
        char ty[5];
        void *d = NULL; size_t n = 0;
        int k = mst_next(r, ty, &d, &n);
        if (k < 0) { rc = k; break; }
        if (k == 0) break;                      /* END: a state with no body is still valid */
        if (!strcmp(ty, "META") && !have_meta) {
            have_meta = 1;
            if (meta) { *meta = d; if (meta_len) *meta_len = n; d = NULL; }
        } else if (!strcmp(ty, "THMB") && !have_thumb) {
            have_thumb = 1;
            if (thumb) { *thumb = d; if (thumb_len) *thumb_len = n; d = NULL; }
        }
        free(d);
        /* META and THMB are written first, in that order, so once both are in hand (or the first
           chunk that is neither has appeared) there is nothing else here worth reading. Stopping
           is the point: a picker drawing ten slots must not read ten multi-megabyte bodies. */
        if ((have_meta && have_thumb) || (strcmp(ty, "META") && strcmp(ty, "THMB"))) break;
    }
    mst_close(r);
    return rc;
}

int mst_meta_get(const char *meta, size_t len, const char *key, char *out, size_t cap) {
    if (!meta || !key || !out || !cap) return 0;
    size_t klen = strlen(key);
    const char *p = meta, *end = meta + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *lineend = nl ? nl : end;
        if ((size_t)(lineend - p) > klen && !memcmp(p, key, klen) && p[klen] == '=') {
            const char *v = p + klen + 1;
            size_t n = (size_t)(lineend - v);
            if (n >= cap) n = cap - 1;
            memcpy(out, v, n);
            out[n] = 0;
            return 1;
        }
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

/* ---- slot paths -------------------------------------------------------------- */
const char *me_state_slot_name(int slot) {
    static const char *names[1 + ME_STATE_NSLOTS] = {
        "quick", "1", "2", "3", "4", "5", "6", "7", "8", "9"
    };
    if (slot < 0 || slot > ME_STATE_NSLOTS) return NULL;
    return names[slot];
}

int me_state_dir(const char *root, const char *gamekey, char *out, size_t cap) {
    if (!root || !gamekey || !out) return MST_ERR_IO;
    int n = snprintf(out, cap, "%s/states/%s", root, gamekey);
    if (n < 0 || (size_t)n >= cap) return MST_ERR_IO;
    return MST_OK;
}

int me_state_slot_path(const char *root, const char *gamekey, int slot, char *out, size_t cap) {
    const char *name = me_state_slot_name(slot);
    if (!name) return MST_ERR_RANGE;
    if (!root || !gamekey || !out) return MST_ERR_IO;
    int n = snprintf(out, cap, "%s/states/%s/state-%s" MST_EXT, root, gamekey, name);
    if (n < 0 || (size_t)n >= cap) return MST_ERR_IO;
    return MST_OK;
}

/* ---- scalar packing (see state_file.h) --------------------------------------- */
void sb_free(struct sbuf *b) { if (b) { free(b->p); b->p = NULL; b->len = b->cap = 0; } }

static int sb_room(struct sbuf *b, size_t n) {
    if (b->failed) return 0;
    if (b->len + n <= b->cap) return 1;
    size_t want = b->cap ? b->cap * 2 : 256;
    while (want < b->len + n) want *= 2;
    uint8_t *p = realloc(b->p, want);
    if (!p) { b->failed = 1; return 0; }
    b->p = p; b->cap = want;
    return 1;
}
void sb_bytes(struct sbuf *b, const void *p, size_t n) {
    if (!sb_room(b, n)) return;
    if (n) memcpy(b->p + b->len, p, n);
    b->len += n;
}
void sb_u8 (struct sbuf *b, uint8_t v)  { sb_bytes(b, &v, 1); }
void sb_u16(struct sbuf *b, uint16_t v) { uint8_t t[2]; put_le16(t, v); sb_bytes(b, t, 2); }
void sb_u32(struct sbuf *b, uint32_t v) { uint8_t t[4]; put_le32(t, v); sb_bytes(b, t, 4); }
void sb_u64(struct sbuf *b, uint64_t v) { uint8_t t[8]; put_le64(t, v); sb_bytes(b, t, 8); }
/* Doubles go over as their IEEE-754 bit pattern rather than as text: these are clock epochs and
   an FPA register file, where a decimal round-trip would quietly lose the low bits. Both ends of
   a savestate are the same build on the same machine, so the representation is not in question. */
void sb_f64(struct sbuf *b, double v) { uint64_t u; memcpy(&u, &v, 8); sb_u64(b, u); }
void sb_str(struct sbuf *b, const char *s) {
    size_t n = s ? strlen(s) : 0;
    sb_u32(b, (uint32_t)n);
    sb_bytes(b, s, n);
}

void sc_init(struct scur *c, const void *p, size_t len) {
    c->p = p; c->len = len; c->off = 0; c->failed = 0;
}
int sc_bytes(struct scur *c, void *dst, size_t n) {
    if (c->failed || c->off + n > c->len) { c->failed = 1; if (dst && n) memset(dst, 0, n); return 0; }
    if (dst && n) memcpy(dst, c->p + c->off, n);
    c->off += n;
    return 1;
}
uint8_t  sc_u8 (struct scur *c) { uint8_t t = 0;  sc_bytes(c, &t, 1); return t; }
uint16_t sc_u16(struct scur *c) { uint8_t t[2] = {0}; sc_bytes(c, t, 2); return get_le16(t); }
uint32_t sc_u32(struct scur *c) { uint8_t t[4] = {0}; sc_bytes(c, t, 4); return get_le32(t); }
uint64_t sc_u64(struct scur *c) { uint8_t t[8] = {0}; sc_bytes(c, t, 8); return get_le64(t); }
double   sc_f64(struct scur *c) { uint64_t u = sc_u64(c); double v; memcpy(&v, &u, 8); return v; }
int sc_skip(struct scur *c, size_t n) { return sc_bytes(c, NULL, n); }
int sc_str(struct scur *c, char *dst, size_t cap) {
    if (!dst || !cap) return 0;
    dst[0] = 0;
    uint32_t n = sc_u32(c);
    if (c->failed || n > c->len - c->off) { c->failed = 1; return 0; }
    size_t take = n < cap - 1 ? n : cap - 1;
    memcpy(dst, c->p + c->off, take);
    dst[take] = 0;
    c->off += n;                     /* consume the WHOLE field even when we truncate the copy */
    return 1;
}
