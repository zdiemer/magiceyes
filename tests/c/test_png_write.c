/* Unit tests for host/png_write.c -- the dependency-free PNG screenshot writer.
 *
 * ME_TEST_SRC: host/png_write.c
 *
 * png_write.c hand-rolls a PNG: chunk framing, CRC32, and a zlib stream built from DEFLATE
 * "stored" blocks so no zlib dependency is needed. Every one of those is a place to be subtly
 * wrong in a way that still produces a plausible-looking file, so this test does not merely check
 * that bytes were written -- it parses the result back, verifies every chunk CRC and the adler32,
 * and reconstructs the pixels.
 *
 * That matters beyond screenshots: tools/test/compat_visual.load_luma decodes exactly this
 * writer's output, and the whole visual-grading tier is built on it.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "png_write.h"

/* ---- a small PNG reader, used only to check the writer -------------------------------------- */

static uint32_t crc32_of(const uint8_t *p, size_t n) {
    static uint32_t tab[256];
    static int ready = 0;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
        ready = 1;
    }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) c = tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static uint32_t adler32_of(const uint8_t *p, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) { a = (a + p[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

struct png {
    uint8_t *file;   size_t flen;
    uint8_t *idat;   size_t idat_len;     /* concatenated IDAT chunk payloads */
    uint8_t *raw;    size_t raw_len;      /* the filtered scanlines, after inflating */
    int      w, h, depth, ctype;
    int      nchunks, stored_blocks;
    char     first_chunk[5], last_chunk[5];
};

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    *len = got;
    return b;
}

/* Walk the file, verifying framing and every CRC, then inflate the stored-block zlib stream. */
static void png_parse(struct png *p, const char *path) {
    memset(p, 0, sizeof *p);
    p->file = slurp(path, &p->flen);
    assert_non_null(p->file);

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    assert_true(p->flen > 8);
    assert_memory_equal(p->file, sig, 8);

    p->idat = malloc(p->flen);
    size_t off = 8;
    while (off + 12 <= p->flen) {
        uint32_t len = rd_be32(p->file + off);
        const uint8_t *type = p->file + off + 4;
        const uint8_t *data = type + 4;
        assert_true(off + 12 + len <= p->flen);

        /* the CRC covers the type bytes and the data as one running value */
        uint32_t want = rd_be32(data + len);
        assert_int_equal(crc32_of(type, 4 + len), want);

        if (p->nchunks == 0) memcpy(p->first_chunk, type, 4);
        memcpy(p->last_chunk, type, 4);
        p->nchunks++;

        if (!memcmp(type, "IHDR", 4)) {
            assert_int_equal(len, 13);
            p->w = (int)rd_be32(data);
            p->h = (int)rd_be32(data + 4);
            p->depth = data[8];
            p->ctype = data[9];
            assert_int_equal(data[10], 0);      /* compression: deflate */
            assert_int_equal(data[11], 0);      /* filter method */
            assert_int_equal(data[12], 0);      /* interlace: none */
        } else if (!memcmp(type, "IDAT", 4)) {
            memcpy(p->idat + p->idat_len, data, len);
            p->idat_len += len;
        }
        off += 12 + len;
    }
    assert_int_equal(off, p->flen);            /* no trailing junk */

    /* zlib: 2-byte header, a run of stored DEFLATE blocks, 4-byte adler32 */
    assert_true(p->idat_len > 6);
    assert_int_equal(p->idat[0], 0x78);
    assert_int_equal(p->idat[1], 0x01);
    assert_int_equal((p->idat[0] * 256 + p->idat[1]) % 31, 0);   /* zlib header check value */

    p->raw = malloc(p->idat_len);
    size_t i = 2;
    int final = 0;
    while (!final) {
        assert_true(i + 5 <= p->idat_len);
        uint8_t flags = p->idat[i];
        final = flags & 1;
        assert_int_equal((flags >> 1) & 3, 0);                   /* BTYPE 00 = stored */
        uint16_t l  = (uint16_t)(p->idat[i+1] | (p->idat[i+2] << 8));
        uint16_t nl = (uint16_t)(p->idat[i+3] | (p->idat[i+4] << 8));
        assert_int_equal((uint16_t)~l, nl);                      /* NLEN is the complement */
        i += 5;
        assert_true(i + l <= p->idat_len);
        memcpy(p->raw + p->raw_len, p->idat + i, l);
        p->raw_len += l;
        i += l;
        p->stored_blocks++;
    }
    assert_int_equal(i + 4, p->idat_len);
    assert_int_equal(rd_be32(p->idat + i), adler32_of(p->raw, p->raw_len));
}

static void png_free(struct png *p) { free(p->file); free(p->idat); free(p->raw); }

static void tmp_png(char *out, size_t cap, const char *tag) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = getenv("TEMP");
    if (!base || !*base) base = ".";
    snprintf(out, cap, "%s/me_png_test_%s.png", base, tag);
}

/* ---- tests ------------------------------------------------------------------------------------ */

static void test_rejects_bad_arguments(void **st) {
    (void)st;
    uint8_t px[3] = {1, 2, 3};
    char path[512];
    tmp_png(path, sizeof path, "bad");
    assert_int_equal(png_write_rgb(NULL, px, 1, 1), -1);
    assert_int_equal(png_write_rgb(path, NULL, 1, 1), -1);
    assert_int_equal(png_write_rgb(path, px, 0, 1), -1);
    assert_int_equal(png_write_rgb(path, px, 1, 0), -1);
    assert_int_equal(png_write_rgb(path, px, -4, 4), -1);
}

static void test_unwritable_path_fails_cleanly(void **st) {
    (void)st;
    uint8_t px[3] = {1, 2, 3};
    assert_int_equal(png_write_rgb("no_such_dir_here/nope.png", px, 1, 1), -1);
}

static void test_single_pixel(void **st) {
    (void)st;
    char path[512];
    tmp_png(path, sizeof path, "1x1");
    const uint8_t px[3] = {0x12, 0x34, 0x56};
    assert_int_equal(png_write_rgb(path, px, 1, 1), 0);

    struct png p;
    png_parse(&p, path);
    assert_int_equal(p.w, 1);
    assert_int_equal(p.h, 1);
    assert_int_equal(p.depth, 8);
    assert_int_equal(p.ctype, 2);              /* truecolour RGB */
    assert_int_equal(p.raw_len, 4);            /* one filter byte + 3 colour bytes */
    assert_int_equal(p.raw[0], 0);             /* filter: none */
    assert_memory_equal(p.raw + 1, px, 3);
    png_free(&p);
    remove(path);
}

/* Every scanline carries its own filter byte, and the pixels must survive verbatim. */
static void test_pixels_round_trip(void **st) {
    (void)st;
    enum { W = 7, H = 5 };
    uint8_t px[W * H * 3];
    for (int i = 0; i < W * H * 3; i++) px[i] = (uint8_t)(i * 7 + 11);

    char path[512];
    tmp_png(path, sizeof path, "roundtrip");
    assert_int_equal(png_write_rgb(path, px, W, H), 0);

    struct png p;
    png_parse(&p, path);
    assert_int_equal(p.w, W);
    assert_int_equal(p.h, H);
    assert_int_equal(p.raw_len, (size_t)(W * 3 + 1) * H);
    for (int y = 0; y < H; y++) {
        const uint8_t *row = p.raw + (size_t)y * (W * 3 + 1);
        assert_int_equal(row[0], 0);
        assert_memory_equal(row + 1, px + (size_t)y * W * 3, W * 3);
    }
    png_free(&p);
    remove(path);
}

/* IHDR must come first and IEND last, or a decoder is entitled to reject the file. */
static void test_chunk_order(void **st) {
    (void)st;
    char path[512];
    tmp_png(path, sizeof path, "order");
    uint8_t px[3 * 4] = {0};
    assert_int_equal(png_write_rgb(path, px, 2, 2), 0);

    struct png p;
    png_parse(&p, path);
    assert_memory_equal(p.first_chunk, "IHDR", 4);
    assert_memory_equal(p.last_chunk, "IEND", 4);
    assert_int_equal(p.nchunks, 3);            /* IHDR, IDAT, IEND */
    png_free(&p);
    remove(path);
}

/* A stored DEFLATE block tops out at 65535 bytes, so a screenshot-sized image has to span
   several of them with only the last marked final. */
static void test_large_image_spans_several_stored_blocks(void **st) {
    (void)st;
    enum { W = 320, H = 240 };                 /* the native GP2X framebuffer */
    size_t n = (size_t)W * H * 3;
    uint8_t *px = malloc(n);
    for (size_t i = 0; i < n; i++) px[i] = (uint8_t)(i & 0xff);

    char path[512];
    tmp_png(path, sizeof path, "320x240");
    assert_int_equal(png_write_rgb(path, px, W, H), 0);

    struct png p;
    png_parse(&p, path);
    assert_int_equal(p.w, W);
    assert_int_equal(p.h, H);
    assert_int_equal(p.raw_len, (size_t)(W * 3 + 1) * H);
    assert_true(p.raw_len > 65535);
    assert_true(p.stored_blocks > 1);
    for (int y = 0; y < H; y++)
        assert_memory_equal(p.raw + (size_t)y * (W * 3 + 1) + 1, px + (size_t)y * W * 3, W * 3);

    png_free(&p);
    free(px);
    remove(path);
}

/* A single row that is itself longer than one stored block. */
static void test_row_longer_than_a_stored_block(void **st) {
    (void)st;
    enum { W = 30000, H = 1 };                 /* 90000 bytes of pixels in one scanline */
    size_t n = (size_t)W * H * 3;
    uint8_t *px = malloc(n);
    for (size_t i = 0; i < n; i++) px[i] = (uint8_t)(i * 3);

    char path[512];
    tmp_png(path, sizeof path, "widerow");
    assert_int_equal(png_write_rgb(path, px, W, H), 0);

    struct png p;
    png_parse(&p, path);
    assert_int_equal(p.raw_len, (size_t)W * 3 + 1);
    assert_true(p.stored_blocks > 1);
    assert_memory_equal(p.raw + 1, px, (size_t)W * 3);

    png_free(&p);
    free(px);
    remove(path);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rejects_bad_arguments),
        cmocka_unit_test(test_unwritable_path_fails_cleanly),
        cmocka_unit_test(test_single_pixel),
        cmocka_unit_test(test_pixels_round_trip),
        cmocka_unit_test(test_chunk_order),
        cmocka_unit_test(test_large_image_spans_several_stored_blocks),
        cmocka_unit_test(test_row_longer_than_a_stored_block),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
