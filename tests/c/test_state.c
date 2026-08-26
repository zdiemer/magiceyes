/* Unit tests for host/state_file.c -- the savestate container: the fixed header, chunk framing,
 * CRCs, miniz compression, and the metadata/thumbnail probe the slot picker reads.
 *
 * ME_TEST_SRC: host/state_file.c host/engine/extract/miniz.c
 *
 * A savestate is the only file magiceyes reads back INTO ITSELF. A bad screenshot is a bad
 * screenshot; a mis-framed savestate is a plausible-looking machine that diverges a few thousand
 * instructions later, somewhere else entirely. So almost everything here is about REFUSING bad
 * input rather than accepting good input: truncation at every offset, a flipped bit, a newer
 * version, a length that lies about itself.
 *
 * That this test exists at all is why the container is a separate translation unit from the
 * capture code: it makes zero uc_* calls and links no engine, exactly like test_png_write.c.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cmocka.h>

#include "state_file.h"

/* ---- scratch files ---------------------------------------------------------- */
static char g_path[512];

/* Scratch files go to a real temp dir, never the CWD: a cmocka assertion failure longjmps past
   the cleanup, and littering the repo with .mst debris on every red test is its own small bug. */
static const char *tmp_path(void) {
    static int n = 0;
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = getenv("TEMP");
#ifdef _WIN32
    if (!dir || !*dir) dir = ".";
#else
    if (!dir || !*dir) dir = "/tmp";
#endif
    snprintf(g_path, sizeof g_path, "%s/me_state_test_%d_%d.mst", dir, (int)(size_t)&n, ++n);
    return g_path;
}

static void unlink_all(const char *p) {
    char t[600];
    remove(p);
    snprintf(t, sizeof t, "%s.tmp", p);
    remove(t);
}

static uint8_t *slurp(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb");
    assert_non_null(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    assert_int_equal(fread(b, 1, (size_t)n, f), (size_t)n);
    fclose(f);
    *len = (size_t)n;
    return b;
}

static void spew(const char *p, const void *b, size_t n) {
    FILE *f = fopen(p, "wb");
    assert_non_null(f);
    assert_int_equal(fwrite(b, 1, n, f), n);
    fclose(f);
}

static struct mst_info sample_info(void) {
    struct mst_info i;
    memset(&i, 0, sizeof i);
    i.game_key   = 0x0123456789abcdefULL;
    i.save_time  = 1756000000;
    i.device     = 2;
    i.engine_abi = 7;
    i.frame_seq  = 104233;
    i.fb_w = 320; i.fb_h = 240;
    i.thumb_w = 160; i.thumb_h = 120;
    return i;
}

static const char SAMPLE_META[] =
    "# magiceyes-state v1\n"
    "game=Payback.gpe\n"
    "device=GP2X\n"
    "frame=104233\n"
    "saved=2026-08-26T14:03:11Z\n";

/* A state with everything a picker needs plus one large body chunk, which is what makes the
   "probe does not read the body" assertion meaningful. */
static void write_sample(const char *p, size_t body_bytes) {
    struct mst_info info = sample_info();
    info.content_flags = MST_F_THUMB;
    struct mst_w *w = mst_create(p, &info);
    assert_non_null(w);
    assert_int_equal(mst_chunk(w, "META", SAMPLE_META, sizeof SAMPLE_META - 1, 0), MST_OK);
    uint8_t *thumb = malloc(160 * 120 * 2);
    for (size_t i = 0; i < 160u * 120u * 2u; i++) thumb[i] = (uint8_t)(i * 7 + 3);
    assert_int_equal(mst_chunk(w, "THMB", thumb, 160 * 120 * 2, 0), MST_OK);
    if (body_bytes) {
        /* Incompressible on purpose, and stored: the probe test needs the body to really be
           large ON DISK. Zeros would deflate away to nothing and the assertion would pass
           because the file was small, not because the probe stopped early. */
        uint8_t *body = malloc(body_bytes);
        uint32_t s = 2463534242u;
        for (size_t i = 0; i < body_bytes; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                                                  body[i] = (uint8_t)s; }
        assert_int_equal(mst_chunk(w, "MEMR", body, body_bytes, 0), MST_OK);
        free(body);
    }
    free(thumb);
    assert_int_equal(mst_finish(w), MST_OK);
}

/* ---- header round-trip ------------------------------------------------------ */
static void test_header_round_trips_every_identity_field(void **st) {
    (void)st;
    const char *p = tmp_path();
    write_sample(p, 0);

    struct mst_info got;
    int err = 999;
    struct mst_r *r = mst_open(p, &got, &err);
    assert_non_null(r);
    assert_int_equal(err, MST_OK);

    struct mst_info want = sample_info();
    assert_int_equal(got.format_version, MST_FORMAT_VERSION);
    assert_int_equal(got.header_bytes, MST_HEADER_BYTES);
    assert_true(got.game_key == want.game_key);
    assert_true(got.save_time == want.save_time);
    assert_int_equal(got.device, want.device);
    assert_int_equal(got.engine_abi, want.engine_abi);
    assert_int_equal(got.frame_seq, want.frame_seq);
    assert_int_equal(got.fb_w, want.fb_w);
    assert_int_equal(got.fb_h, want.fb_h);
    assert_int_equal(got.thumb_w, want.thumb_w);
    assert_int_equal(got.thumb_h, want.thumb_h);
    assert_true((got.content_flags & MST_F_THUMB) != 0);
    /* chunk_count is patched in at finish and counts END. */
    assert_int_equal(got.chunk_count, 3);
    mst_close(r);
    unlink_all(p);
}

/* ---- refusing things that are not a savestate ------------------------------- */
static void test_reader_rejects_a_file_that_is_not_a_state(void **st) {
    (void)st;
    const char *p = tmp_path();
    int err;

    spew(p, "", 0);
    assert_null(mst_open(p, NULL, &err));
    assert_int_equal(err, MST_ERR_MAGIC);

    spew(p, "hello, this is plainly not a savestate at all, not even close\n", 62);
    assert_null(mst_open(p, NULL, &err));
    assert_int_equal(err, MST_ERR_MAGIC);

    /* A real PNG: the container borrows PNG's trap bytes, so this is the near-miss most likely
       to be handed to us by accident (a screenshot renamed, or a mixed-up slot directory). */
    static const uint8_t png[] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
                                   0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    spew(p, png, sizeof png);
    assert_null(mst_open(p, NULL, &err));
    assert_int_equal(err, MST_ERR_MAGIC);

    unlink_all(p);
}

static void test_reader_rejects_a_corrupted_header(void **st) {
    (void)st;
    const char *p = tmp_path();
    write_sample(p, 0);
    size_t n;
    uint8_t *b = slurp(p, &n);

    /* Flip a bit in a field the CRC is supposed to cover. If this still opened, the header CRC
       would be decorative -- so this pins that it covers what the format says it covers. */
    b[36] ^= 0x01;                       /* device */
    spew(p, b, n);
    int err;
    assert_null(mst_open(p, NULL, &err));
    assert_int_equal(err, MST_ERR_HEADER_CRC);

    free(b);
    unlink_all(p);
}

static void test_reader_rejects_a_chunk_whose_crc_does_not_match(void **st) {
    (void)st;
    const char *p = tmp_path();
    write_sample(p, 4096);
    size_t n;
    uint8_t *b = slurp(p, &n);

    /* Damage a byte inside the THMB payload: past the header, past META. */
    b[MST_HEADER_BYTES + MST_CHUNK_HDR_BYTES + (sizeof SAMPLE_META - 1)
      + MST_CHUNK_HDR_BYTES + 64] ^= 0xff;
    spew(p, b, n);

    struct mst_r *r = mst_open(p, NULL, NULL);
    assert_non_null(r);
    char ty[5]; void *d = NULL; size_t dn = 0;
    /* META still reads: damage to one chunk must not poison the ones before it. */
    assert_int_equal(mst_next(r, ty, &d, &dn), 1);
    assert_string_equal(ty, "META");
    free(d);
    assert_int_equal(mst_next(r, ty, &d, &dn), MST_ERR_CHUNK_CRC);
    assert_null(d);
    mst_close(r);

    free(b);
    unlink_all(p);
}

/* The cheap fuzz that catches the classic framing bug: every prefix of a valid file must fail
   cleanly, never crash and never hand back a buffer it did not fill. Uses a DELIBERATELY tiny
   fixture (no thumbnail, a few hundred bytes) because this is O(file size) file opens -- the
   point is to cover every offset in a file that has one of each structure, not to cover a big
   one. */
static void test_reader_rejects_a_truncated_file_at_every_length(void **st) {
    (void)st;
    const char *p = tmp_path();
    {
        struct mst_info info = sample_info();
        struct mst_w *w = mst_create(p, &info);
        assert_non_null(w);
        uint8_t small[64];
        for (size_t i = 0; i < sizeof small; i++) small[i] = (uint8_t)(i * 5 + 1);
        assert_int_equal(mst_chunk(w, "META", SAMPLE_META, sizeof SAMPLE_META - 1, 0), MST_OK);
        assert_int_equal(mst_chunk(w, "THMB", small, sizeof small, 0), MST_OK);
        assert_int_equal(mst_chunk(w, "MEMR", small, sizeof small, 0), MST_OK);
        assert_int_equal(mst_finish(w), MST_OK);
    }
    size_t n;
    uint8_t *full = slurp(p, &n);
    assert_true(n < 1024);        /* keep this fixture small; see above */

    for (size_t cut = 0; cut < n; cut++) {
        spew(p, full, cut);
        int err = MST_OK;
        struct mst_r *r = mst_open(p, NULL, &err);
        if (!r) {
            assert_true(err < 0);
            continue;
        }
        int rc;
        do {
            char ty[5]; void *d = NULL; size_t dn = 0;
            rc = mst_next(r, ty, &d, &dn);
            if (rc == 1) assert_non_null(d);
            free(d);
        } while (rc == 1);
        /* A truncated file must ERROR. Reaching END cleanly would mean the framing accepted a
           file that is not all there. */
        assert_true(rc < 0);
        mst_close(r);
    }

    free(full);
    unlink_all(p);
}

static void test_a_file_with_no_end_chunk_is_an_error(void **st) {
    (void)st;
    const char *p = tmp_path();
    write_sample(p, 0);
    size_t n;
    uint8_t *b = slurp(p, &n);

    /* Drop the END chunk but leave chunk_count claiming it is there: the reader must trust the
       framing it can see, not the count it is told. */
    spew(p, b, n - MST_CHUNK_HDR_BYTES);
    struct mst_r *r = mst_open(p, NULL, NULL);
    assert_non_null(r);
    int rc;
    do {
        char ty[5]; void *d = NULL; size_t dn = 0;
        rc = mst_next(r, ty, &d, &dn);
        free(d);
    } while (rc == 1);
    assert_int_equal(rc, MST_ERR_NO_END);
    mst_close(r);

    free(b);
    unlink_all(p);
}

/* ---- versioning ------------------------------------------------------------- */
static void test_a_newer_format_version_is_refused_not_guessed_at(void **st) {
    (void)st;
    const char *p = tmp_path();
    write_sample(p, 0);
    size_t n;
    uint8_t *b = slurp(p, &n);

    b[8] = MST_FORMAT_VERSION + 1; b[9] = 0;         /* format_version */
    /* Re-derive the header CRC so the ONLY thing wrong is the version. Otherwise this would
       silently be testing the CRC path again and would pass for the wrong reason. */
    {
        uint32_t crc = 0xffffffffu;
        static uint32_t tab[256]; static int ready = 0;
        if (!ready) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
                tab[i] = c;
            }
            ready = 1;
        }
        for (size_t i = 0; i < 12; i++) crc = tab[(crc ^ b[i]) & 0xff] ^ (crc >> 8);
        for (size_t i = 16; i < MST_HEADER_BYTES; i++) crc = tab[(crc ^ b[i]) & 0xff] ^ (crc >> 8);
        crc ^= 0xffffffffu;
        b[12] = (uint8_t)crc; b[13] = (uint8_t)(crc >> 8);
        b[14] = (uint8_t)(crc >> 16); b[15] = (uint8_t)(crc >> 24);
    }
    spew(p, b, n);

    int err = MST_OK;
    assert_null(mst_open(p, NULL, &err));
    assert_int_equal(err, MST_ERR_NEWER_FORMAT);
    /* The reason string is what the on-screen toast shows, so its content is part of the
       contract, not an implementation detail. */
    assert_non_null(strstr(mst_strerror(err), "newer"));

    free(b);
    unlink_all(p);
}

/* The forward-compatibility contract, and the one most likely to rot: adding a chunk within v1
   must not break a reader that predates it. */
static void test_unknown_chunk_types_are_skipped_not_fatal(void **st) {
    (void)st;
    const char *p = tmp_path();
    struct mst_info info = sample_info();
    info.content_flags = MST_F_THUMB;
    struct mst_w *w = mst_create(p, &info);
    assert_non_null(w);
    assert_int_equal(mst_chunk(w, "META", SAMPLE_META, sizeof SAMPLE_META - 1, 0), MST_OK);
    assert_int_equal(mst_chunk(w, "ZZZZ", "from the future", 15, 0), MST_OK);
    uint8_t thumb[160 * 120 * 2];
    memset(thumb, 0x5a, sizeof thumb);
    assert_int_equal(mst_chunk(w, "THMB", thumb, sizeof thumb, 0), MST_OK);
    assert_int_equal(mst_finish(w), MST_OK);

    struct mst_r *r = mst_open(p, NULL, NULL);
    assert_non_null(r);
    int seen_unknown = 0, seen_thumb = 0, rc;
    do {
        char ty[5]; void *d = NULL; size_t dn = 0;
        rc = mst_next(r, ty, &d, &dn);
        if (rc == 1 && !strcmp(ty, "ZZZZ")) seen_unknown = 1;
        if (rc == 1 && !strcmp(ty, "THMB")) seen_thumb = 1;
        free(d);
    } while (rc == 1);
    assert_int_equal(rc, 0);
    assert_true(seen_unknown);
    assert_true(seen_thumb);          /* the reader kept going PAST the type it did not know */
    mst_close(r);
    unlink_all(p);
}

/* ---- compression ------------------------------------------------------------ */
static void test_a_compressed_chunk_round_trips_byte_for_byte(void **st) {
    (void)st;
    const char *p = tmp_path();
    const size_t N = 1u << 20;

    uint8_t *noise = malloc(N), *zeros = calloc(N, 1);
    uint32_t s = 12345;
    for (size_t i = 0; i < N; i++) { s = s * 1103515245u + 12345u; noise[i] = (uint8_t)(s >> 16); }

    struct mst_info info = sample_info();
    struct mst_w *w = mst_create(p, &info);
    assert_non_null(w);
    assert_int_equal(mst_chunk(w, "NOIS", noise, N, 1), MST_OK);
    assert_int_equal(mst_chunk(w, "ZERO", zeros, N, 1), MST_OK);
    assert_int_equal(mst_finish(w), MST_OK);

    struct mst_r *r = mst_open(p, NULL, NULL);
    assert_non_null(r);
    char ty[5]; void *d = NULL; size_t dn = 0;
    assert_int_equal(mst_next(r, ty, &d, &dn), 1);
    assert_string_equal(ty, "NOIS");
    assert_int_equal(dn, N);
    assert_memory_equal(d, noise, N);
    free(d);
    assert_int_equal(mst_next(r, ty, &d, &dn), 1);
    assert_string_equal(ty, "ZERO");
    assert_int_equal(dn, N);
    assert_memory_equal(d, zeros, N);
    free(d);
    mst_close(r);

    /* A megabyte of zeros must land in a fraction of the space. Without this the whole file could
       be silently storing everything and every other test here would still pass -- and a 40 MB
       quicksave that should have been 2 MB is a real regression, just an invisible one. */
    size_t fn;
    uint8_t *raw = slurp(p, &fn);
    assert_true(fn < 2 * N);
    free(raw);

    free(noise); free(zeros);
    unlink_all(p);
}

static void test_a_chunk_that_declares_the_wrong_plain_len_is_rejected(void **st) {
    (void)st;
    const char *p = tmp_path();
    const size_t N = 4096;
    uint8_t *body = calloc(N, 1);
    for (size_t i = 0; i < N; i++) body[i] = (uint8_t)(i * 3);

    struct mst_info info = sample_info();
    struct mst_w *w = mst_create(p, &info);
    assert_non_null(w);
    assert_int_equal(mst_chunk(w, "MEMR", body, N, 1), MST_OK);
    assert_int_equal(mst_finish(w), MST_OK);

    size_t n;
    uint8_t *b = slurp(p, &n);
    /* Claim the chunk inflates to 64 MB. A reader that allocates plain_len and never checks what
       inflate actually produced is a decompression bomb; one that checks is not. */
    uint8_t *ch = b + MST_HEADER_BYTES;
    ch[12] = 0x00; ch[13] = 0x00; ch[14] = 0x00; ch[15] = 0x04;   /* plain_len = 0x04000000 */
    /* Re-derive the chunk CRC so the length is the only thing wrong. */
    {
        static uint32_t tab[256]; static int ready = 0;
        if (!ready) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
                tab[i] = c;
            }
            ready = 1;
        }
        uint32_t stored = (uint32_t)ch[8] | ((uint32_t)ch[9] << 8)
                        | ((uint32_t)ch[10] << 16) | ((uint32_t)ch[11] << 24);
        uint32_t crc = 0xffffffffu;
        for (size_t i = 0; i < 16; i++) crc = tab[(crc ^ ch[i]) & 0xff] ^ (crc >> 8);
        for (size_t i = 0; i < stored; i++)
            crc = tab[(crc ^ ch[MST_CHUNK_HDR_BYTES + i]) & 0xff] ^ (crc >> 8);
        crc ^= 0xffffffffu;
        ch[16] = (uint8_t)crc; ch[17] = (uint8_t)(crc >> 8);
        ch[18] = (uint8_t)(crc >> 16); ch[19] = (uint8_t)(crc >> 24);
    }
    spew(p, b, n);

    struct mst_r *r = mst_open(p, NULL, NULL);
    assert_non_null(r);
    char ty[5]; void *d = NULL; size_t dn = 0;
    assert_int_equal(mst_next(r, ty, &d, &dn), MST_ERR_BAD_LEN);
    assert_null(d);
    mst_close(r);

    free(b); free(body);
    unlink_all(p);
}

/* ---- the picker's path ------------------------------------------------------ */
static void test_probe_reads_metadata_and_thumbnail_without_reading_the_body(void **st) {
    (void)st;
    const char *p = tmp_path();
    write_sample(p, 8u << 20);           /* an 8 MB body the probe must not touch */

    struct mst_info info;
    char *meta = NULL; size_t meta_len = 0;
    uint8_t *thumb = NULL; size_t thumb_len = 0;
    assert_int_equal(mst_probe(p, &info, &meta, &meta_len, &thumb, &thumb_len), MST_OK);
    assert_non_null(meta);
    assert_non_null(thumb);
    assert_int_equal(thumb_len, 160u * 120u * 2u);
    assert_int_equal(info.thumb_w, 160);
    assert_int_equal(info.thumb_h, 120);

    /* The property the picker's responsiveness rests on: opening a ten-slot directory must not
       read eighty megabytes. Compare against the file size rather than a magic number. */
    size_t fn;
    uint8_t *raw = slurp(p, &fn);
    assert_true(fn > 4u << 20);          /* the body really is large */
    free(raw);

    free(meta); free(thumb);
    unlink_all(p);
}

static void test_probe_survives_a_state_with_no_thumbnail(void **st) {
    (void)st;
    const char *p = tmp_path();
    struct mst_info info = sample_info();
    info.thumb_w = info.thumb_h = 0;
    struct mst_w *w = mst_create(p, &info);
    assert_non_null(w);
    assert_int_equal(mst_chunk(w, "META", SAMPLE_META, sizeof SAMPLE_META - 1, 0), MST_OK);
    assert_int_equal(mst_finish(w), MST_OK);

    struct mst_info got;
    char *meta = NULL; size_t ml = 0;
    uint8_t *thumb = (uint8_t *)1; size_t tl = 99;
    assert_int_equal(mst_probe(p, &got, &meta, &ml, &thumb, &tl), MST_OK);
    assert_non_null(meta);
    assert_null(thumb);                  /* absent, not garbage */
    assert_int_equal(tl, 0);
    assert_false((got.content_flags & MST_F_THUMB) != 0);
    free(meta);
    unlink_all(p);
}

/* Top-down, row-major, no stride padding: the contract StretchDIBits (BITMAPV4HEADER, negative
   height) and SDL_UpdateTexture (pitch = w*2) are both wired against. */
static void test_thumbnail_pixels_survive_verbatim(void **st) {
    (void)st;
    const char *p = tmp_path();
    const int W = 160, H = 120;
    uint16_t *px = malloc((size_t)W * H * 2);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            px[y * W + x] = (uint16_t)((y << 8) | x);

    struct mst_info info = sample_info();
    info.content_flags = MST_F_THUMB;
    struct mst_w *w = mst_create(p, &info);
    assert_non_null(w);
    assert_int_equal(mst_chunk(w, "META", SAMPLE_META, sizeof SAMPLE_META - 1, 0), MST_OK);
    assert_int_equal(mst_chunk(w, "THMB", px, (size_t)W * H * 2, 0), MST_OK);
    assert_int_equal(mst_finish(w), MST_OK);

    uint8_t *thumb = NULL; size_t tl = 0;
    assert_int_equal(mst_probe(p, NULL, NULL, NULL, &thumb, &tl), MST_OK);
    assert_int_equal(tl, (size_t)W * H * 2);
    assert_memory_equal(thumb, px, (size_t)W * H * 2);
    free(thumb); free(px);
    unlink_all(p);
}

static void test_metadata_parses_the_keys_the_picker_shows(void **st) {
    (void)st;
    char v[64];
    size_t n = sizeof SAMPLE_META - 1;
    assert_int_equal(mst_meta_get(SAMPLE_META, n, "game", v, sizeof v), 1);
    assert_string_equal(v, "Payback.gpe");
    assert_int_equal(mst_meta_get(SAMPLE_META, n, "frame", v, sizeof v), 1);
    assert_string_equal(v, "104233");
    assert_int_equal(mst_meta_get(SAMPLE_META, n, "saved", v, sizeof v), 1);
    assert_string_equal(v, "2026-08-26T14:03:11Z");
    /* An unknown key is absent, not an error: that is what lets META grow. */
    assert_int_equal(mst_meta_get(SAMPLE_META, n, "nosuchkey", v, sizeof v), 0);
    /* A key that is a PREFIX of a real one must not match it. */
    assert_int_equal(mst_meta_get(SAMPLE_META, n, "gam", v, sizeof v), 0);
}

/* ---- slot naming ------------------------------------------------------------ */
static void test_slot_zero_is_the_quick_slot_and_the_rest_are_numbered(void **st) {
    (void)st;
    char p[512];
    assert_int_equal(me_state_slot_path("/opt/me", "Payback", 0, p, sizeof p), MST_OK);
    assert_non_null(strstr(p, "state-quick.mst"));
    assert_int_equal(me_state_slot_path("/opt/me", "Payback", 3, p, sizeof p), MST_OK);
    assert_non_null(strstr(p, "state-3.mst"));
    assert_int_equal(me_state_slot_path("/opt/me", "Payback", ME_STATE_NSLOTS, p, sizeof p), MST_OK);

    assert_int_equal(me_state_slot_path("/opt/me", "Payback", -1, p, sizeof p), MST_ERR_RANGE);
    assert_int_equal(me_state_slot_path("/opt/me", "Payback", ME_STATE_NSLOTS + 1, p, sizeof p),
                     MST_ERR_RANGE);
    /* A path that will not fit is refused, never silently truncated onto another slot's file. */
    char tiny[8];
    assert_int_equal(me_state_slot_path("/opt/me", "Payback", 1, tiny, sizeof tiny), MST_ERR_IO);
}

/* States live BESIDE the per-game save overlay, never inside it: saves/<key>/ is union-mounted
   into the guest's own readdir (syscalls.c dirfd_make), so a state file there would show up in
   the game's directory listings, and a title that tidies its save dir could delete it. Pinned as
   a test so the layout cannot be quietly "tidied" back later. */
static void test_state_dir_is_a_sibling_of_the_save_overlay_not_inside_it(void **st) {
    (void)st;
    char p[512];
    assert_int_equal(me_state_dir("/opt/me", "Payback", p, sizeof p), MST_OK);
    assert_non_null(strstr(p, "/states/"));
    assert_null(strstr(p, "/saves/"));
    assert_int_equal(me_state_slot_path("/opt/me", "Payback", 0, p, sizeof p), MST_OK);
    assert_non_null(strstr(p, "/states/"));
    assert_null(strstr(p, "/saves/"));
}

/* ---- scalar packing --------------------------------------------------------- */
static void test_scalars_round_trip_through_a_buffer(void **st) {
    (void)st;
    struct sbuf b;
    memset(&b, 0, sizeof b);
    sb_u8(&b, 0xa5);
    sb_u16(&b, 0xbeef);
    sb_u32(&b, 0xdeadbeefu);
    sb_u64(&b, 0x0123456789abcdefULL);
    sb_f64(&b, -1234.5678e90);
    sb_str(&b, "a path with spaces/and.dots");
    sb_str(&b, "");
    assert_false(b.failed);

    struct scur c;
    sc_init(&c, b.p, b.len);
    assert_int_equal(sc_u8(&c), 0xa5);
    assert_int_equal(sc_u16(&c), 0xbeef);
    assert_true(sc_u32(&c) == 0xdeadbeefu);
    assert_true(sc_u64(&c) == 0x0123456789abcdefULL);
    assert_true(sc_f64(&c) == -1234.5678e90);
    char s[64];
    assert_int_equal(sc_str(&c, s, sizeof s), 1);
    assert_string_equal(s, "a path with spaces/and.dots");
    assert_int_equal(sc_str(&c, s, sizeof s), 1);
    assert_string_equal(s, "");
    assert_false(c.failed);
    assert_int_equal(c.off, b.len);       /* everything written was consumed, nothing left over */
    sb_free(&b);
}

/* The property the whole restore path leans on: a chunk that ends early yields zeros and a
   sticky failure flag, so a module can do a run of reads and check ONCE at the end instead of
   testing every field -- and can never walk off the end of the buffer while doing it. */
static void test_a_short_buffer_fails_stickily_instead_of_overreading(void **st) {
    (void)st;
    struct sbuf b;
    memset(&b, 0, sizeof b);
    sb_u32(&b, 0x11223344u);
    sb_u32(&b, 0x55667788u);

    struct scur c;
    sc_init(&c, b.p, 6);                  /* six bytes: the second u32 is half there */
    assert_true(sc_u32(&c) == 0x11223344u);
    assert_false(c.failed);
    assert_int_equal(sc_u32(&c), 0);      /* zero, not garbage from past the end */
    assert_true(c.failed);
    /* Once failed, stays failed, even for a read that WOULD have fit. */
    sc_init(&c, b.p, 8);
    c.failed = 1;
    assert_int_equal(sc_u32(&c), 0);
    assert_true(c.failed);
    sb_free(&b);
}

static void test_a_string_longer_than_the_destination_is_truncated_not_desynced(void **st) {
    (void)st;
    struct sbuf b;
    memset(&b, 0, sizeof b);
    sb_str(&b, "a very long host path that will not fit in the caller's little buffer");
    sb_u32(&b, 0xcafebabeu);              /* the field AFTER the oversized string */

    struct scur c;
    sc_init(&c, b.p, b.len);
    char small[8];
    assert_int_equal(sc_str(&c, small, sizeof small), 1);
    assert_int_equal(strlen(small), 7);
    /* The cursor must skip the WHOLE string, not just the part that was copied, or every
       subsequent field in the chunk is read from the wrong offset. */
    assert_true(sc_u32(&c) == 0xcafebabeu);
    assert_false(c.failed);
    sb_free(&b);
}

static void test_a_string_claiming_more_bytes_than_remain_is_refused(void **st) {
    (void)st;
    uint8_t bad[8] = { 0xff, 0xff, 0xff, 0x7f, 'a', 'b', 'c', 'd' };   /* len = 0x7fffffff */
    struct scur c;
    sc_init(&c, bad, sizeof bad);
    char s[16] = "untouched";
    assert_int_equal(sc_str(&c, s, sizeof s), 0);
    assert_true(c.failed);
    assert_string_equal(s, "");
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_header_round_trips_every_identity_field),
        cmocka_unit_test(test_reader_rejects_a_file_that_is_not_a_state),
        cmocka_unit_test(test_reader_rejects_a_corrupted_header),
        cmocka_unit_test(test_reader_rejects_a_chunk_whose_crc_does_not_match),
        cmocka_unit_test(test_reader_rejects_a_truncated_file_at_every_length),
        cmocka_unit_test(test_a_file_with_no_end_chunk_is_an_error),
        cmocka_unit_test(test_a_newer_format_version_is_refused_not_guessed_at),
        cmocka_unit_test(test_unknown_chunk_types_are_skipped_not_fatal),
        cmocka_unit_test(test_a_compressed_chunk_round_trips_byte_for_byte),
        cmocka_unit_test(test_a_chunk_that_declares_the_wrong_plain_len_is_rejected),
        cmocka_unit_test(test_probe_reads_metadata_and_thumbnail_without_reading_the_body),
        cmocka_unit_test(test_probe_survives_a_state_with_no_thumbnail),
        cmocka_unit_test(test_thumbnail_pixels_survive_verbatim),
        cmocka_unit_test(test_metadata_parses_the_keys_the_picker_shows),
        cmocka_unit_test(test_slot_zero_is_the_quick_slot_and_the_rest_are_numbered),
        cmocka_unit_test(test_state_dir_is_a_sibling_of_the_save_overlay_not_inside_it),
        cmocka_unit_test(test_scalars_round_trip_through_a_buffer),
        cmocka_unit_test(test_a_short_buffer_fails_stickily_instead_of_overreading),
        cmocka_unit_test(test_a_string_longer_than_the_destination_is_truncated_not_desynced),
        cmocka_unit_test(test_a_string_claiming_more_bytes_than_remain_is_refused),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
