/* Unit tests for host/engine/gpecomp.c -- the offline GPEComp (UCL/NRV2x) decompressor.
 *
 * gpecomp.c is the one file under host/engine/ that includes no engine.h, so this test needs
 * neither unicorn headers nor any stubbed global. We #include the .c to reach its statics
 * (find_header, nrv_decompress, getbit, be32).
 *
 * The fixtures are BUILT, not captured: there is no NRV2x compressor in the tree and real .gpe
 * files are gitignored game assets. `struct bw` below mirrors the decoder's own bit machinery,
 * which is the only way to get a valid stream. UCL interleaves the bit-stream bytes with the
 * literal bytes in a single buffer, refilling the bit buffer from src[ip++] at the exact moment
 * a bit is needed, so the writer has to reserve each bit-byte at the position the decoder will
 * ask for it.
 *
 * Note when adding fixtures: the end-of-block marker alone costs ~7 bytes, so a short hand-built
 * stream is always LARGER than what it decodes to, and the container reader treats
 * out_len >= in_len as a stored block. To exercise the compressed path a fixture must expand,
 * which means it needs a match (see the RLE run below), not just literals.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "gpecomp.c"

/* ---- a writer that mirrors getbit()'s MSB-first, refill-on-demand consumption ---------- */

struct bw { uint8_t b[4096]; size_t n; long slot; int nbits; };

static void bw_init(struct bw *w) { memset(w, 0, sizeof *w); w->slot = -1; w->nbits = 8; }

static void bw_byte(struct bw *w, uint8_t v) {
    assert_true(w->n < sizeof w->b);
    w->b[w->n++] = v;
}

static void bw_bit(struct bw *w, int bit) {
    if (w->nbits >= 8) {                 /* the decoder refills here, so the byte lands here */
        w->slot = (long)w->n;
        bw_byte(w, 0);
        w->nbits = 0;
    }
    if (bit) w->b[w->slot] |= (uint8_t)(1u << (7 - w->nbits));
    w->nbits++;
}

/* NRV2B offset: an implicit leading 1, then each remaining bit followed by a continuation bit
   that is set only on the last one (`do { m_off = m_off*2 + GB(); } while (!GB());`). */
static void bw_off2b(struct bw *w, uint32_t raw) {
    int hi = 31;
    while (hi > 0 && !((raw >> hi) & 1)) hi--;
    for (int i = hi - 1; i >= 0; i--) { bw_bit(w, (raw >> i) & 1); bw_bit(w, i == 0); }
}

/* NRV2B length: a single 1 bit means m_len == 1; a leading 0 selects the long form, which is
   encoded like the offset and then biased by 2. Valid for m_len == 1 or m_len >= 3. */
static void bw_len2b(struct bw *w, uint32_t m_len) {
    if (m_len == 1) { bw_bit(w, 1); return; }
    assert_true(m_len >= 3);
    bw_bit(w, 0);
    uint32_t v = m_len - 2;
    int hi = 31;
    while (hi > 0 && !((v >> hi) & 1)) hi--;
    for (int i = hi - 1; i >= 0; i--) { bw_bit(w, (v >> i) & 1); bw_bit(w, i == 0); }
}

/* End of block: the decoder breaks when (m_off - 3) * 256 + src[ip++] == 0xffffffff. The only
   solution is a trailing 0xff byte with a decoded offset of 0x1000002. */
static void bw_eof(struct bw *w) { bw_off2b(w, 0x1000002u); bw_byte(w, 0xff); }

static void bw_literal(struct bw *w, uint8_t v) { bw_bit(w, 1); bw_byte(w, v); }
static void bw_end_literals(struct bw *w) { bw_bit(w, 0); }

/* A match at offset 1, which overlaps its own output and so expresses a run. */
static void bw_rle(struct bw *w, uint32_t m_len) {
    bw_off2b(w, 3); bw_byte(w, 0x00);    /* (3-3)*256 + 0 = 0, then +1 -> m_off = 1 */
    bw_len2b(w, m_len);                  /* copies m_len + 1 bytes */
}

/* ---- container assembly ---------------------------------------------------------------- */

static const uint8_t MAGIC[8] = {0x00,0xe9,0x55,0x43,0x4c,0xff,0x01,0x1a};

struct cont { uint8_t b[8192]; size_t n; };

static void put_be32(struct cont *c, uint32_t v) {
    c->b[c->n++] = (uint8_t)(v >> 24); c->b[c->n++] = (uint8_t)(v >> 16);
    c->b[c->n++] = (uint8_t)(v >> 8);  c->b[c->n++] = (uint8_t)v;
}

static void cont_header(struct cont *c, int method, uint32_t block_size) {
    memset(c, 0, sizeof *c);
    memcpy(c->b, MAGIC, 8); c->n = 8;
    put_be32(c, 0);                       /* flags: ignored by the reader */
    c->b[c->n++] = (uint8_t)method;
    c->b[c->n++] = 0;                     /* level: informational */
    put_be32(c, block_size);
}

/* in_len is the UNCOMPRESSED size and out_len the on-disk size; a block is "stored" when
   out_len >= in_len, which is how the reader tells the two apart. */
static void cont_block(struct cont *c, uint32_t in_len, const uint8_t *data, uint32_t out_len) {
    put_be32(c, in_len); put_be32(c, out_len);
    memcpy(c->b + c->n, data, out_len); c->n += out_len;
}

static void cont_eof(struct cont *c) { put_be32(c, 0); }

/* ---- be32 / getbit ---------------------------------------------------------------------- */

static void test_be32(void **st) {
    (void)st;
    const uint8_t a[4] = {0x12,0x34,0x56,0x78};
    const uint8_t b[4] = {0xff,0xff,0xff,0xff};
    assert_int_equal(be32(a), 0x12345678u);
    assert_int_equal(be32(b), 0xffffffffu);
}

/* Bits come out MSB-first, exactly 8 per source byte. */
static void test_getbit_msb_first(void **st) {
    (void)st;
    const uint8_t src[2] = {0xA5, 0x0F};        /* 1010 0101  0000 1111 */
    const int want[16] = {1,0,1,0,0,1,0,1, 0,0,0,0,1,1,1,1};
    uint32_t bb = 0; size_t ip = 0;
    for (int i = 0; i < 16; i++)
        assert_int_equal(getbit(&bb, src, &ip, sizeof src), want[i]);
    assert_int_equal(ip, 2);                    /* both bytes consumed, and no more */
}

/* Past the end getbit must not read out of bounds; it latches to a 0 bit. */
static void test_getbit_past_end(void **st) {
    (void)st;
    const uint8_t src[1] = {0x00};
    uint32_t bb = 0; size_t ip = 0;
    for (int i = 0; i < 8; i++) (void)getbit(&bb, src, &ip, sizeof src);
    assert_int_equal(ip, 1);
    assert_int_equal(getbit(&bb, src, &ip, sizeof src), 0);
    assert_int_equal(ip, 1);
}

/* ---- nrv_decompress --------------------------------------------------------------------- */

static void test_nrv2b_literals_only(void **st) {
    (void)st;
    struct bw w; bw_init(&w);
    const char *msg = "magiceyes";
    for (const char *p = msg; *p; p++) bw_literal(&w, (uint8_t)*p);
    bw_end_literals(&w);
    bw_eof(&w);

    uint8_t out[64]; memset(out, 0, sizeof out);
    long n = nrv_decompress(0x2b, w.b, w.n, out, sizeof out);
    assert_int_equal(n, (long)strlen(msg));
    assert_memory_equal(out, msg, strlen(msg));
}

/* A match at offset 1 overlaps its own output, which is how NRV2x expresses a run. */
static void test_nrv2b_overlapping_match_is_rle(void **st) {
    (void)st;
    struct bw w; bw_init(&w);
    bw_literal(&w, 'A');
    bw_end_literals(&w);
    bw_rle(&w, 1);                        /* copies 2 bytes from op - 1 */
    bw_end_literals(&w);
    bw_eof(&w);

    uint8_t out[64]; memset(out, 0, sizeof out);
    long n = nrv_decompress(0x2b, w.b, w.n, out, sizeof out);
    assert_int_equal(n, 3);
    assert_memory_equal(out, "AAA", 3);
}

/* The long-form length encoding, which is a different code path from the single-bit form. */
static void test_nrv2b_long_match_length(void **st) {
    (void)st;
    struct bw w; bw_init(&w);
    bw_literal(&w, 'Z');
    bw_end_literals(&w);
    bw_rle(&w, 30);                       /* copies 31 bytes */
    bw_end_literals(&w);
    bw_eof(&w);

    uint8_t out[128]; memset(out, 0, sizeof out);
    long n = nrv_decompress(0x2b, w.b, w.n, out, sizeof out);
    assert_int_equal(n, 32);
    for (int i = 0; i < 32; i++) assert_int_equal(out[i], 'Z');
}

/* Copying from before the start of the output is the classic corrupt-stream case. */
static void test_nrv2b_rejects_offset_before_output(void **st) {
    (void)st;
    struct bw w; bw_init(&w);
    bw_end_literals(&w);                  /* no literals, so op is still 0 */
    bw_rle(&w, 1);                        /* m_off = 1 > op = 0 */
    bw_end_literals(&w);
    bw_eof(&w);

    uint8_t out[64];
    assert_int_equal(nrv_decompress(0x2b, w.b, w.n, out, sizeof out), -1);
}

static void test_nrv2b_rejects_output_overflow(void **st) {
    (void)st;
    struct bw w; bw_init(&w);
    for (int i = 0; i < 8; i++) bw_literal(&w, (uint8_t)('a' + i));
    bw_end_literals(&w);
    bw_eof(&w);

    uint8_t out[4];
    assert_int_equal(nrv_decompress(0x2b, w.b, w.n, out, sizeof out), -1);
}

/* A match that would run off the end of the destination must be refused too, not just a
   literal run (they are separate bounds checks). */
static void test_nrv2b_rejects_match_overflow(void **st) {
    (void)st;
    struct bw w; bw_init(&w);
    bw_literal(&w, 'A');
    bw_end_literals(&w);
    bw_rle(&w, 30);                       /* 31 bytes, but the destination holds 8 */
    bw_end_literals(&w);
    bw_eof(&w);

    uint8_t out[8];
    assert_int_equal(nrv_decompress(0x2b, w.b, w.n, out, sizeof out), -1);
}

/* Truncation mid-stream must be refused rather than read past src_len. */
static void test_nrv2b_rejects_truncated_input(void **st) {
    (void)st;
    struct bw w; bw_init(&w);
    for (int i = 0; i < 8; i++) bw_literal(&w, (uint8_t)('a' + i));
    bw_end_literals(&w);
    bw_eof(&w);

    uint8_t out[64];
    assert_int_equal(nrv_decompress(0x2b, w.b, 3, out, sizeof out), -1);
}

/* ---- find_header / gpecomp_detect --------------------------------------------------------- */

static void test_detect_finds_plain_payload(void **st) {
    (void)st;
    struct cont c; cont_header(&c, 0x2b, 4096); cont_eof(&c);
    assert_true(gpecomp_detect(c.b, c.n));
    assert_int_equal(find_header(c.b, c.n), 0);
}

static void test_detect_rejects_absent_and_null(void **st) {
    (void)st;
    uint8_t junk[256]; memset(junk, 0x41, sizeof junk);
    assert_false(gpecomp_detect(junk, sizeof junk));
    assert_false(gpecomp_detect(NULL, 100));
    assert_true(find_header(junk, sizeof junk) == (size_t)-1);
}

/* The magic alone is not enough: the method byte at +12 gates it, which is what stops a random
   run of bytes (or the stub's own embedded copy of the magic) from being taken as a payload. */
static void test_detect_requires_a_valid_method_byte(void **st) {
    (void)st;
    struct cont c;
    cont_header(&c, 0x99, 4096); cont_eof(&c);
    assert_false(gpecomp_detect(c.b, c.n));

    cont_header(&c, 0x2b, 4096); cont_eof(&c);
    assert_true(gpecomp_detect(c.b, c.n));
    cont_header(&c, 0x2d, 4096); cont_eof(&c);
    assert_true(gpecomp_detect(c.b, c.n));
    cont_header(&c, 0x2e, 4096); cont_eof(&c);
    assert_true(gpecomp_detect(c.b, c.n));
}

/* The GPEComp stub embeds a copy of the magic in its own code. find_header must skip past the
   ELF image (to the end of the section-header table) so it finds the appended payload rather
   than the decoy. This is the entire reason the ELF heuristic exists. */
static void test_find_header_skips_the_decoy_inside_the_elf_image(void **st) {
    (void)st;
    static uint8_t buf[1024];
    memset(buf, 0, sizeof buf);
    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';

    const uint32_t shoff = 256, shentsize = 40, shnum = 4;   /* image ends at 256 + 160 = 416 */
    buf[0x20] = (uint8_t)shoff; buf[0x21] = (uint8_t)(shoff >> 8);
    buf[0x22] = (uint8_t)(shoff >> 16); buf[0x23] = (uint8_t)(shoff >> 24);
    buf[0x2e] = (uint8_t)shentsize; buf[0x2f] = (uint8_t)(shentsize >> 8);
    buf[0x30] = (uint8_t)shnum;     buf[0x31] = (uint8_t)(shnum >> 8);

    memcpy(buf + 128, MAGIC, 8);          /* decoy inside the ELF image */
    buf[128 + 12] = 0x2b;

    struct cont c; cont_header(&c, 0x2e, 4096); cont_eof(&c);
    memcpy(buf + 512, c.b, c.n);          /* the real payload, appended after the SHT */

    assert_int_equal(find_header(buf, sizeof buf), 512);
}

/* When the ELF heuristic misses (a bogus shoff pushes the start past the payload), the second
   pass rescans the whole file rather than giving up. */
static void test_find_header_falls_back_to_a_whole_file_scan(void **st) {
    (void)st;
    static uint8_t buf[1024];
    memset(buf, 0, sizeof buf);
    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[0x20] = 0x00; buf[0x21] = 0x03;   /* shoff = 768, past the payload placed below */
    buf[0x2e] = 0; buf[0x2f] = 0;
    buf[0x30] = 0; buf[0x31] = 0;

    struct cont c; cont_header(&c, 0x2b, 4096); cont_eof(&c);
    memcpy(buf + 200, c.b, c.n);

    assert_int_equal(find_header(buf, sizeof buf), 200);
}

/* ---- gpecomp_decompress ------------------------------------------------------------------- */

static void test_decompress_stored_block(void **st) {
    (void)st;
    const char *payload = "an incompressible block is stored verbatim";
    uint32_t n = (uint32_t)strlen(payload);

    struct cont c; cont_header(&c, 0x2b, 4096);
    cont_block(&c, n, (const uint8_t *)payload, n);   /* out_len == in_len -> stored */
    cont_eof(&c);

    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(c.b, c.n, &out, &outlen), 0);
    assert_int_equal(outlen, n);
    assert_memory_equal(out, payload, n);
    free(out);
}

/* The compressed path needs a fixture that actually expands, so this one is an RLE run. */
static void test_decompress_compressed_block(void **st) {
    (void)st;
    struct bw w; bw_init(&w);
    bw_literal(&w, 'Q');
    bw_end_literals(&w);
    bw_rle(&w, 30);                       /* 1 literal + 31 copied = 32 bytes out */
    bw_end_literals(&w);
    bw_eof(&w);
    assert_true(w.n < 32);                /* otherwise this would take the stored branch */

    struct cont c; cont_header(&c, 0x2b, 4096);
    cont_block(&c, 32, w.b, (uint32_t)w.n);
    cont_eof(&c);

    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(c.b, c.n, &out, &outlen), 0);
    assert_int_equal(outlen, 32);
    for (int i = 0; i < 32; i++) assert_int_equal(out[i], 'Q');
    free(out);
}

/* Multiple blocks concatenate in order. */
static void test_decompress_multiple_blocks(void **st) {
    (void)st;
    struct cont c; cont_header(&c, 0x2b, 4096);
    cont_block(&c, 4, (const uint8_t *)"AAAA", 4);
    cont_block(&c, 3, (const uint8_t *)"BBB", 3);
    cont_eof(&c);

    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(c.b, c.n, &out, &outlen), 0);
    assert_int_equal(outlen, 7);
    assert_memory_equal(out, "AAAABBB", 7);
    free(out);
}

static void test_decompress_rejects_null_arguments(void **st) {
    (void)st;
    uint8_t *out = NULL; size_t outlen = 0;
    struct cont c; cont_header(&c, 0x2b, 4096);
    cont_block(&c, 1, (const uint8_t *)"A", 1); cont_eof(&c);
    assert_int_equal(gpecomp_decompress(NULL, 10, &out, &outlen), -1);
    assert_int_equal(gpecomp_decompress(c.b, c.n, NULL, &outlen), -1);
    assert_int_equal(gpecomp_decompress(c.b, c.n, &out, NULL), -1);
}

static void test_decompress_rejects_missing_header(void **st) {
    (void)st;
    uint8_t junk[64]; memset(junk, 0x5a, sizeof junk);
    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(junk, sizeof junk, &out, &outlen), -1);
}

/* A header cut off before block_size has been read. */
static void test_decompress_rejects_truncated_header(void **st) {
    (void)st;
    struct cont c; cont_header(&c, 0x2b, 4096); cont_eof(&c);
    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(c.b, 12, &out, &outlen), -1);
}

static void test_decompress_rejects_bad_block_size(void **st) {
    (void)st;
    uint8_t *out = NULL; size_t outlen = 0;
    struct cont c;

    cont_header(&c, 0x2b, 0);
    cont_block(&c, 1, (const uint8_t *)"A", 1); cont_eof(&c);
    assert_int_equal(gpecomp_decompress(c.b, c.n, &out, &outlen), -1);

    cont_header(&c, 0x2b, 65u * 1024 * 1024);
    cont_block(&c, 1, (const uint8_t *)"A", 1); cont_eof(&c);
    assert_int_equal(gpecomp_decompress(c.b, c.n, &out, &outlen), -1);
}

/* A block claiming more output than the declared block size is malformed. */
static void test_decompress_rejects_oversized_block(void **st) {
    (void)st;
    struct cont c; cont_header(&c, 0x2b, 8);
    cont_block(&c, 64, (const uint8_t *)"AAAA", 4);
    cont_eof(&c);
    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(c.b, c.n, &out, &outlen), -1);
}

/* A block whose data runs off the end of the file. */
static void test_decompress_rejects_block_past_end_of_file(void **st) {
    (void)st;
    struct cont c; cont_header(&c, 0x2b, 4096);
    cont_block(&c, 4, (const uint8_t *)"AAAA", 4);
    cont_eof(&c);
    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(c.b, c.n - 6, &out, &outlen), -1);
}

/* A compressed block that decodes to a different length than declared is corrupt and must not
   be handed back as a short buffer. */
static void test_decompress_rejects_length_mismatch(void **st) {
    (void)st;
    struct bw w; bw_init(&w);
    bw_literal(&w, 'x');
    bw_end_literals(&w);
    bw_eof(&w);

    struct cont c; cont_header(&c, 0x2b, 4096);
    cont_block(&c, 100, w.b, (uint32_t)w.n);   /* claims 100 bytes, the stream yields 1 */
    cont_eof(&c);

    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(c.b, c.n, &out, &outlen), -1);
}

/* A container with no blocks yields nothing, which is a failure rather than an empty success
   (the caller would otherwise get a NULL buffer and a zero length). */
static void test_decompress_rejects_empty_container(void **st) {
    (void)st;
    struct cont c; cont_header(&c, 0x2b, 4096); cont_eof(&c);
    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(c.b, c.n, &out, &outlen), -1);
}

/* A payload appended behind a real ELF stub is the shape every actual .gpe has. */
static void test_decompress_payload_appended_to_an_elf_stub(void **st) {
    (void)st;
    static uint8_t buf[2048];
    memset(buf, 0, sizeof buf);
    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[0x20] = 0x00; buf[0x21] = 0x02;   /* shoff = 512 */
    buf[0x2e] = 40; buf[0x30] = 2;        /* + 2 * 40 -> the image ends at 592 */

    struct cont c; cont_header(&c, 0x2b, 4096);
    cont_block(&c, 11, (const uint8_t *)"hello world", 11);
    cont_eof(&c);
    memcpy(buf + 1024, c.b, c.n);

    uint8_t *out = NULL; size_t outlen = 0;
    assert_int_equal(gpecomp_decompress(buf, sizeof buf, &out, &outlen), 0);
    assert_int_equal(outlen, 11);
    assert_memory_equal(out, "hello world", 11);
    free(out);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_be32),
        cmocka_unit_test(test_getbit_msb_first),
        cmocka_unit_test(test_getbit_past_end),
        cmocka_unit_test(test_nrv2b_literals_only),
        cmocka_unit_test(test_nrv2b_overlapping_match_is_rle),
        cmocka_unit_test(test_nrv2b_long_match_length),
        cmocka_unit_test(test_nrv2b_rejects_offset_before_output),
        cmocka_unit_test(test_nrv2b_rejects_output_overflow),
        cmocka_unit_test(test_nrv2b_rejects_match_overflow),
        cmocka_unit_test(test_nrv2b_rejects_truncated_input),
        cmocka_unit_test(test_detect_finds_plain_payload),
        cmocka_unit_test(test_detect_rejects_absent_and_null),
        cmocka_unit_test(test_detect_requires_a_valid_method_byte),
        cmocka_unit_test(test_find_header_skips_the_decoy_inside_the_elf_image),
        cmocka_unit_test(test_find_header_falls_back_to_a_whole_file_scan),
        cmocka_unit_test(test_decompress_stored_block),
        cmocka_unit_test(test_decompress_compressed_block),
        cmocka_unit_test(test_decompress_multiple_blocks),
        cmocka_unit_test(test_decompress_rejects_null_arguments),
        cmocka_unit_test(test_decompress_rejects_missing_header),
        cmocka_unit_test(test_decompress_rejects_truncated_header),
        cmocka_unit_test(test_decompress_rejects_bad_block_size),
        cmocka_unit_test(test_decompress_rejects_oversized_block),
        cmocka_unit_test(test_decompress_rejects_block_past_end_of_file),
        cmocka_unit_test(test_decompress_rejects_length_mismatch),
        cmocka_unit_test(test_decompress_rejects_empty_container),
        cmocka_unit_test(test_decompress_payload_appended_to_an_elf_stub),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
