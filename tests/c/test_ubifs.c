/* Unit tests for host/engine/extract/ubifs.c -- the read-only UBI + UBIFS reader.
 *
 * ME_TEST_SRC: host/engine/extract/ubifs.c host/engine/extract/miniz.c host/engine/extract/minilzo.c
 *
 * This is the reader that turns a Wiz firmware image into the rootfs, so its field offsets were
 * originally established by byte-exact extraction of ld-2.3.6.so against a known-good rootfs.
 * Nothing in the repo can re-run that check: firmware images are neither committed nor
 * redistributable. So the fixtures here are UBI images assembled byte by byte, which is the only
 * way this file is testable at all.
 *
 * UBIFS is log-structured, and that is the part most worth pinning: a node is not an edit in
 * place, it is a new version, and the reader has to keep the highest sqnum for every inode,
 * dentry and data block. Getting that backwards yields a rootfs built from stale nodes, which
 * looks plausible and boots to something subtly wrong.
 *
 * Compression is exercised for real rather than stubbed: the reader links miniz and minilzo to
 * decompress, so the test uses the matching compressors to build the blocks.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "ubifs.h"
#include "untar.h"
#include "miniz.h"
#include "minilzo.h"

/* ---- image geometry ------------------------------------------------------------------------ */

#define PEB       16384u        /* the smallest size the reader probes for */
#define VID_OFF    2048u
#define DATA_OFF   4096u
#define LEB_SIZE  (PEB - DATA_OFF)
#define MAX_PEB    6u

#define UBIFS_MAGIC 0x06101831u
#define N_INO  0
#define N_DATA 1
#define N_DENT 2

/* The reader carries its own S_IF* constants (the guest's, not the host's), so the test must use
   the same literals: MinGW's sys/stat.h disagrees with Linux about several of them. */
#define G_IFDIR  0040000u
#define G_IFREG  0100000u
#define G_IFLNK  0120000u
#define G_IFCHR  0020000u

#define ROOT_INO 1u

struct img {
    unsigned char b[PEB * MAX_PEB];
    size_t npeb;
    size_t used[MAX_PEB];       /* bytes of node payload written into each LEB */
};

static void put_le16(unsigned char *p, uint16_t v) { p[0] = v & 0xff; p[1] = v >> 8; }
static void put_le32(unsigned char *p, uint32_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}
static void put_le64(unsigned char *p, uint64_t v) {
    put_le32(p, (uint32_t)v); put_le32(p + 4, (uint32_t)(v >> 32));
}
static void put_be32(unsigned char *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff; p[1] = (v >> 16) & 0xff; p[2] = (v >> 8) & 0xff; p[3] = v & 0xff;
}

/* One PEB: an EC header at 0 naming where the VID header and the data live, a VID header giving
   the volume and logical block, then the LEB payload. */
static void add_peb(struct img *im, uint32_t vol_id, uint32_t lnum) {
    assert_true(im->npeb < MAX_PEB);
    unsigned char *pb = im->b + im->npeb * PEB;

    memcpy(pb, "UBI#", 4);
    put_be32(pb + 16, VID_OFF);
    put_be32(pb + 20, DATA_OFF);

    unsigned char *vid = pb + VID_OFF;
    memcpy(vid, "UBI!", 4);
    put_be32(vid + 8, vol_id);
    put_be32(vid + 12, lnum);

    im->used[im->npeb] = 0;
    im->npeb++;
}

static void img_init(struct img *im) {
    memset(im, 0, sizeof *im);
}

static size_t img_len(const struct img *im) { return im->npeb * PEB; }

/* Reserve `n` bytes in a LEB, 8-aligned as the node scanner expects. */
static unsigned char *leb_alloc(struct img *im, size_t peb, size_t n) {
    size_t aligned = (n + 7u) & ~7u;
    assert_true(im->used[peb] + aligned <= LEB_SIZE);
    unsigned char *p = im->b + peb * PEB + DATA_OFF + im->used[peb];
    im->used[peb] += aligned;
    return p;
}

/* The UBIFS common header. The CRC at +4 is deliberately left zero: this reader does not check
   it, and pretending otherwise in the fixture would hide that. */
static void put_ch(unsigned char *n, uint64_t sqnum, uint32_t len, uint8_t type) {
    put_le32(n, UBIFS_MAGIC);
    put_le64(n + 8, sqnum);
    put_le32(n + 16, len);
    n[20] = type;
}

static void wr_ino(struct img *im, size_t peb, uint32_t inum, uint32_t mode, uint64_t size,
                   uint64_t sqnum, const void *inl, uint32_t dlen) {
    uint32_t len = 160 + dlen;
    unsigned char *n = leb_alloc(im, peb, len);
    memset(n, 0, (len + 7u) & ~7u);
    put_ch(n, sqnum, len, N_INO);
    put_le32(n + 24, inum);
    put_le64(n + 48, size);
    put_le32(n + 104, mode);
    put_le32(n + 112, dlen);
    if (inl && dlen) memcpy(n + 160, inl, dlen);
}

static void wr_dent(struct img *im, size_t peb, uint32_t parent, uint64_t inum,
                    const char *name, uint64_t sqnum) {
    uint16_t nlen = (uint16_t)strlen(name);
    uint32_t len = 56u + nlen;
    unsigned char *n = leb_alloc(im, peb, len);
    memset(n, 0, (len + 7u) & ~7u);
    put_ch(n, sqnum, len, N_DENT);
    put_le32(n + 24, parent);
    put_le64(n + 40, inum);
    put_le16(n + 50, nlen);
    memcpy(n + 56, name, nlen);
}

static void wr_data(struct img *im, size_t peb, uint32_t ino, uint32_t block,
                    const void *payload, uint32_t clen, uint32_t usize, uint16_t compr,
                    uint64_t sqnum) {
    uint32_t len = 48u + clen;
    unsigned char *n = leb_alloc(im, peb, len);
    memset(n, 0, (len + 7u) & ~7u);
    put_ch(n, sqnum, len, N_DATA);
    put_le32(n + 24, ino);
    put_le32(n + 28, block);
    put_le32(n + 40, usize);
    put_le16(n + 44, compr);
    if (payload && clen) memcpy(n + 48, payload, clen);
}

/* Shorthand: an uncompressed data block. */
static void wr_data_raw(struct img *im, size_t peb, uint32_t ino, uint32_t block,
                        const void *data, uint32_t n, uint64_t sqnum) {
    wr_data(im, peb, ino, block, data, n, n, 0, sqnum);
}

/* ---- collecting callback -------------------------------------------------------------------- */

struct entry {
    char          path[512];
    int           type;
    char          link[512];
    unsigned char data[8192];
    size_t        size;
    unsigned      mode;
};

struct collect {
    struct entry e[32];
    int n;
    int abort_after;
    int abort_rc;
};

static int collect_cb(void *ud, const char *path, int type, const char *link,
                      const unsigned char *data, size_t size, unsigned mode) {
    struct collect *c = (struct collect *)ud;
    if (c->n < (int)(sizeof c->e / sizeof c->e[0])) {
        struct entry *e = &c->e[c->n];
        snprintf(e->path, sizeof e->path, "%s", path);
        snprintf(e->link, sizeof e->link, "%s", link ? link : "");
        e->type = type;
        e->size = size;
        e->mode = mode;
        size_t k = size < sizeof e->data ? size : sizeof e->data;
        if (data && k) memcpy(e->data, data, k);
        c->n++;
    }
    if (c->abort_after && c->n >= c->abort_after) return c->abort_rc;
    return 0;
}

static struct entry *by_path(struct collect *c, const char *path) {
    for (int i = 0; i < c->n; i++) if (!strcmp(c->e[i].path, path)) return &c->e[i];
    return NULL;
}

/* A one-volume image with a second, empty LEB so the reader can detect the PEB size (it needs a
   following block that also starts with UBI#). */
static void two_leb_volume(struct img *im) {
    img_init(im);
    add_peb(im, 0, 0);
    add_peb(im, 0, 1);
}

/* ---- rejection ------------------------------------------------------------------------------ */

static void test_rejects_non_ubi_input(void **st) {
    (void)st;
    struct collect c = {{{{0}}}, 0, 0, 0};
    static unsigned char junk[PEB * 2];
    memset(junk, 0x41, sizeof junk);
    assert_int_equal(ubifs_mem(junk, sizeof junk, collect_cb, &c), -1);
    assert_int_equal(c.n, 0);
}

static void test_rejects_a_short_image(void **st) {
    (void)st;
    struct collect c = {{{{0}}}, 0, 0, 0};
    unsigned char small[1024];
    memset(small, 0, sizeof small);
    memcpy(small, "UBI#", 4);
    assert_int_equal(ubifs_mem(small, sizeof small, collect_cb, &c), -1);
}

/* An image whose PEBs carry no VID header has no volumes to choose from. */
static void test_rejects_an_image_with_no_volume(void **st) {
    (void)st;
    struct img im;
    img_init(&im);
    add_peb(&im, 0, 0);
    add_peb(&im, 0, 1);
    memset(im.b + VID_OFF, 0, 4);              /* clear both VID magics */
    memset(im.b + PEB + VID_OFF, 0, 4);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(ubifs_mem(im.b, img_len(&im), collect_cb, &c), -1);
}

/* ---- the basics ------------------------------------------------------------------------------ */

static void test_a_file_in_the_root(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    const char *body = "export PATH=/bin\n";
    uint32_t n = (uint32_t)strlen(body);
    wr_dent(&im, 0, ROOT_INO, 2, "profile", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, n, 11, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, body, n, 12);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(ubifs_mem(im.b, img_len(&im), collect_cb, &c), 0);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "profile");
    assert_int_equal(c.e[0].type, TAR_FILE);
    assert_int_equal(c.e[0].size, n);
    assert_memory_equal(c.e[0].data, body, n);
    assert_int_equal(c.e[0].mode, G_IFREG | 0644);
}

/* Paths are rebuilt by walking the parent chain up to inode 1, which is the root and contributes
   no component of its own. */
static void test_nested_directories(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "usr", 10);
    wr_ino(&im, 0, 2, G_IFDIR | 0755, 0, 11, NULL, 0);
    wr_dent(&im, 0, 2, 3, "lib", 12);
    wr_ino(&im, 0, 3, G_IFDIR | 0755, 0, 13, NULL, 0);
    wr_dent(&im, 0, 3, 4, "libc.so.6", 14);
    wr_ino(&im, 0, 4, G_IFREG | 0755, 4, 15, NULL, 0);
    wr_data_raw(&im, 0, 4, 0, "ELF\x01", 4, 16);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(ubifs_mem(im.b, img_len(&im), collect_cb, &c), 0);
    assert_int_equal(c.n, 3);
    assert_int_equal(by_path(&c, "usr")->type, TAR_DIR);
    assert_int_equal(by_path(&c, "usr/lib")->type, TAR_DIR);

    struct entry *f = by_path(&c, "usr/lib/libc.so.6");
    assert_non_null(f);
    assert_int_equal(f->type, TAR_FILE);
    assert_memory_equal(f->data, "ELF\x01", 4);
}

static void test_symlink_target_comes_from_the_inline_data(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    const char *target = "libSDL-1.2.so.0";
    wr_dent(&im, 0, ROOT_INO, 2, "libSDL.so", 10);
    wr_ino(&im, 0, 2, G_IFLNK | 0777, strlen(target), 11, target, (uint32_t)strlen(target));

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(ubifs_mem(im.b, img_len(&im), collect_cb, &c), 0);
    assert_int_equal(c.n, 1);
    assert_int_equal(c.e[0].type, TAR_SYMLINK);
    assert_string_equal(c.e[0].link, target);
}

/* Device and fifo nodes carry no content worth staging. */
static void test_special_nodes_are_skipped(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "null", 10);
    wr_ino(&im, 0, 2, G_IFCHR | 0666, 0, 11, NULL, 0);
    wr_dent(&im, 0, ROOT_INO, 3, "real", 12);
    wr_ino(&im, 0, 3, G_IFREG | 0644, 1, 13, NULL, 0);
    wr_data_raw(&im, 0, 3, 0, "x", 1, 14);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "real");
}

/* A dentry whose inode never appeared cannot be emitted, and must not derail the rest. */
static void test_a_dentry_without_an_inode_is_skipped(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "orphan", 10);       /* no inode 2 anywhere */
    wr_dent(&im, 0, ROOT_INO, 3, "real", 11);
    wr_ino(&im, 0, 3, G_IFREG | 0644, 1, 12, NULL, 0);
    wr_data_raw(&im, 0, 3, 0, "x", 1, 13);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "real");
}

/* ---- the log-structured semantics ------------------------------------------------------------- */

/* UBIFS never edits in place: a change is a new node, and the reader must keep the highest
   sqnum. Written newest-first here on purpose, so passing cannot be an artefact of order. */
static void test_the_highest_sqnum_inode_wins(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "f", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0600, 4, 99, NULL, 0);    /* newer, written FIRST */
    wr_ino(&im, 0, 2, G_IFREG | 0644, 2, 20, NULL, 0);    /* older */
    wr_data_raw(&im, 0, 2, 0, "abcd", 4, 30);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_int_equal(c.e[0].mode, G_IFREG | 0600);        /* the newer mode */
    assert_int_equal(c.e[0].size, 4);                     /* and the newer size */
}

static void test_the_highest_sqnum_data_block_wins(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "f", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, 3, 11, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, "old", 3, 20);
    wr_data_raw(&im, 0, 2, 0, "new", 3, 21);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_memory_equal(c.e[0].data, "new", 3);
}

/* A rename or replace writes a new dentry for the same (parent, name). */
static void test_the_highest_sqnum_dentry_wins(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "f", 20);       /* older: points at inode 2 */
    wr_dent(&im, 0, ROOT_INO, 3, "f", 21);       /* newer: now points at inode 3 */
    wr_ino(&im, 0, 2, G_IFREG | 0644, 3, 11, NULL, 0);
    wr_ino(&im, 0, 3, G_IFREG | 0644, 3, 12, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, "old", 3, 30);
    wr_data_raw(&im, 0, 3, 0, "new", 3, 31);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_memory_equal(c.e[0].data, "new", 3);
}

/* Same name under different parents is a different file, not a duplicate. */
static void test_the_same_name_in_two_directories(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "a", 10);
    wr_ino(&im, 0, 2, G_IFDIR | 0755, 0, 11, NULL, 0);
    wr_dent(&im, 0, ROOT_INO, 3, "b", 12);
    wr_ino(&im, 0, 3, G_IFDIR | 0755, 0, 13, NULL, 0);

    wr_dent(&im, 0, 2, 4, "same", 14);
    wr_ino(&im, 0, 4, G_IFREG | 0644, 1, 15, NULL, 0);
    wr_data_raw(&im, 0, 4, 0, "A", 1, 16);
    wr_dent(&im, 0, 3, 5, "same", 17);
    wr_ino(&im, 0, 5, G_IFREG | 0644, 1, 18, NULL, 0);
    wr_data_raw(&im, 0, 5, 0, "B", 1, 19);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 4);
    assert_memory_equal(by_path(&c, "a/same")->data, "A", 1);
    assert_memory_equal(by_path(&c, "b/same")->data, "B", 1);
}

/* Two names for one inode: the file is emitted at each path. */
static void test_a_hardlink_is_emitted_at_every_path(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "one", 10);
    wr_dent(&im, 0, ROOT_INO, 2, "two", 11);
    wr_ino(&im, 0, 2, G_IFREG | 0644, 4, 12, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, "same", 4, 13);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 2);
    assert_memory_equal(by_path(&c, "one")->data, "same", 4);
    assert_memory_equal(by_path(&c, "two")->data, "same", 4);
}

/* ---- block reassembly --------------------------------------------------------------------------- */

static void test_a_multi_block_file_reassembles_in_order(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    enum { SZ = 5000 };
    static unsigned char body[SZ];
    for (int i = 0; i < SZ; i++) body[i] = (unsigned char)(i * 7 + 3);

    wr_dent(&im, 0, ROOT_INO, 2, "big", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, SZ, 11, NULL, 0);
    /* written out of order: the block index, not the position in the log, decides placement */
    wr_data_raw(&im, 0, 2, 1, body + 4096, SZ - 4096, 21);
    wr_data_raw(&im, 0, 2, 0, body, 4096, 20);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_int_equal(c.e[0].size, SZ);
    assert_memory_equal(c.e[0].data, body, SZ);
}

/* A hole reads as zeros rather than as stale heap. */
static void test_a_missing_block_reads_as_zeros(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    enum { SZ = 5000 };
    static unsigned char first[4096];
    memset(first, 0xCD, sizeof first);

    wr_dent(&im, 0, ROOT_INO, 2, "sparse", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, SZ, 11, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, first, sizeof first, 12);   /* block 1 absent */

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_int_equal(c.e[0].size, SZ);
    for (int i = 4096; i < SZ; i++) assert_int_equal(c.e[0].data[i], 0);
}

static void test_an_empty_file(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "empty", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, 0, 11, NULL, 0);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_int_equal(c.e[0].size, 0);
    assert_int_equal(c.e[0].type, TAR_FILE);
}

/* ---- compression ---------------------------------------------------------------------------------- */

/* Real zlib, produced by the same miniz the reader decompresses with. */
static void test_a_zlib_compressed_block(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    char body[2048];
    for (size_t i = 0; i < sizeof body; i++) body[i] = (char)('a' + (i % 26));

    unsigned char comp[4096];
    size_t clen = tdefl_compress_mem_to_mem(comp, sizeof comp, body, sizeof body,
                                            TDEFL_DEFAULT_MAX_PROBES | TDEFL_WRITE_ZLIB_HEADER);
    assert_true(clen > 0);
    assert_true(clen < sizeof body);            /* it really did compress */

    wr_dent(&im, 0, ROOT_INO, 2, "z", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, sizeof body, 11, NULL, 0);
    wr_data(&im, 0, 2, 0, comp, (uint32_t)clen, (uint32_t)sizeof body, 2, 12);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_int_equal(c.e[0].size, sizeof body);
    assert_memory_equal(c.e[0].data, body, sizeof body);
}

/* Real LZO1X, produced by the same minilzo the reader decompresses with. */
static void test_an_lzo_compressed_block(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    assert_int_equal(lzo_init(), LZO_E_OK);

    unsigned char body[2048];
    for (size_t i = 0; i < sizeof body; i++) body[i] = (unsigned char)('A' + (i % 5));

    static unsigned char comp[4096];
    static unsigned char work[LZO1X_1_MEM_COMPRESS];
    lzo_uint clen = sizeof comp;
    assert_int_equal(lzo1x_1_compress(body, sizeof body, comp, &clen, work), LZO_E_OK);
    assert_true(clen < sizeof body);

    wr_dent(&im, 0, ROOT_INO, 2, "l", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, sizeof body, 11, NULL, 0);
    wr_data(&im, 0, 2, 0, comp, (uint32_t)clen, (uint32_t)sizeof body, 1, 12);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_int_equal(c.e[0].size, sizeof body);
    assert_memory_equal(c.e[0].data, body, sizeof body);
}

/* An unknown compression id produces nothing rather than raw compressed bytes: a file of visible
   garbage is worse than a file of zeros, because it looks like it worked. */
static void test_an_unknown_compression_id_yields_no_data(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "weird", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, 8, 11, NULL, 0);
    wr_data(&im, 0, 2, 0, "XXXXXXXX", 8, 8, 99, 12);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    for (int i = 0; i < 8; i++) assert_int_equal(c.e[0].data[i], 0);
}

/* ---- the UBI layer ----------------------------------------------------------------------------------- */

/* The rootfs is the volume with the most LEBs; a small companion volume must not win. */
static void test_the_largest_volume_is_chosen(void **st) {
    (void)st;
    struct img im;
    img_init(&im);
    add_peb(&im, 7, 0);          /* a small volume: one LEB */
    add_peb(&im, 3, 0);          /* the rootfs: three LEBs */
    add_peb(&im, 3, 1);
    add_peb(&im, 3, 2);

    /* the decoy volume holds a file that must NOT be extracted */
    wr_dent(&im, 0, ROOT_INO, 2, "decoy", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, 1, 11, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, "D", 1, 12);

    /* the real volume */
    wr_dent(&im, 1, ROOT_INO, 2, "real", 20);
    wr_ino(&im, 1, 2, G_IFREG | 0644, 1, 21, NULL, 0);
    wr_data_raw(&im, 1, 2, 0, "R", 1, 22);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(ubifs_mem(im.b, img_len(&im), collect_cb, &c), 0);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "real");
}

/* A volume's nodes are spread across its LEBs, and all of them have to be scanned. */
static void test_nodes_are_found_across_several_lebs(void **st) {
    (void)st;
    struct img im;
    img_init(&im);
    add_peb(&im, 0, 0);
    add_peb(&im, 0, 1);
    add_peb(&im, 0, 2);

    wr_dent(&im, 0, ROOT_INO, 2, "split", 10);       /* dentry in LEB 0 */
    wr_ino(&im, 1, 2, G_IFREG | 0644, 4, 11, NULL, 0);   /* inode in LEB 1 */
    wr_data_raw(&im, 2, 2, 0, "abcd", 4, 12);            /* data in LEB 2 */

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_memory_equal(c.e[0].data, "abcd", 4);
}

/* A PEB with no VID header belongs to no volume and is simply not part of one. */
static void test_a_peb_without_a_vid_header_is_ignored(void **st) {
    (void)st;
    struct img im;
    img_init(&im);
    add_peb(&im, 0, 0);
    add_peb(&im, 0, 1);
    add_peb(&im, 0, 2);

    wr_dent(&im, 0, ROOT_INO, 2, "kept", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, 1, 11, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, "K", 1, 12);

    wr_dent(&im, 2, ROOT_INO, 3, "dropped", 20);
    wr_ino(&im, 2, 3, G_IFREG | 0644, 1, 21, NULL, 0);
    wr_data_raw(&im, 2, 3, 0, "X", 1, 22);
    memset(im.b + 2 * PEB + VID_OFF, 0, 4);            /* orphan that last PEB */

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "kept");
}

/* ---- robustness of the node scan -------------------------------------------------------------------- */

/* The scan steps 8 bytes at a time looking for the magic, so arbitrary bytes between nodes are
   normal (a LEB is a log with free space and stale remnants in it). */
static void test_garbage_between_nodes_is_stepped_over(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    unsigned char *junk = leb_alloc(&im, 0, 64);
    memset(junk, 0xA5, 64);

    wr_dent(&im, 0, ROOT_INO, 2, "after", 10);
    unsigned char *junk2 = leb_alloc(&im, 0, 32);
    memset(junk2, 0x5A, 32);
    wr_ino(&im, 0, 2, G_IFREG | 0644, 1, 11, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, "Z", 1, 12);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "after");
}

/* A node whose declared length runs past the end of its LEB is refused rather than read. */
static void test_a_node_longer_than_its_leb_is_refused(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    wr_dent(&im, 0, ROOT_INO, 2, "ok", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, 1, 11, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, "K", 1, 12);

    unsigned char *bad = leb_alloc(&im, 0, 64);
    memset(bad, 0, 64);
    put_ch(bad, 999, LEB_SIZE * 2, N_DENT);            /* absurd length */

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(ubifs_mem(im.b, img_len(&im), collect_cb, &c), 0);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "ok");
}

/* An unrecognised node type is skipped by its own declared length, so the scan stays in step. */
static void test_an_unknown_node_type_is_skipped(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    unsigned char *odd = leb_alloc(&im, 0, 80);
    memset(odd, 0, 80);
    put_ch(odd, 5, 80, 9);                             /* some other UBIFS node kind */

    wr_dent(&im, 0, ROOT_INO, 2, "still-here", 10);
    wr_ino(&im, 0, 2, G_IFREG | 0644, 1, 11, NULL, 0);
    wr_data_raw(&im, 0, 2, 0, "S", 1, 12);

    struct collect c = {{{{0}}}, 0, 0, 0};
    ubifs_mem(im.b, img_len(&im), collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "still-here");
}

/* A dentry claiming a name longer than it carries must not read past the node. */
static void test_an_overlong_dentry_name_is_refused(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    unsigned char *n = leb_alloc(&im, 0, 64);
    memset(n, 0, 64);
    put_ch(n, 10, 64, N_DENT);
    put_le32(n + 24, ROOT_INO);
    put_le64(n + 40, 2);
    put_le16(n + 50, 500);                             /* 56 + 500 is well past the node */

    wr_dent(&im, 0, ROOT_INO, 3, "fine", 11);
    wr_ino(&im, 0, 3, G_IFREG | 0644, 1, 12, NULL, 0);
    wr_data_raw(&im, 0, 3, 0, "F", 1, 13);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(ubifs_mem(im.b, img_len(&im), collect_cb, &c), 0);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "fine");
}

/* ---- the callback contract ---------------------------------------------------------------------------- */

static void test_a_nonzero_callback_return_aborts(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    for (uint32_t i = 0; i < 4; i++) {
        char name[16];
        snprintf(name, sizeof name, "f%u", i);
        wr_dent(&im, 0, ROOT_INO, 2 + i, name, 10 + i);
        wr_ino(&im, 0, 2 + i, G_IFREG | 0644, 1, 30 + i, NULL, 0);
        wr_data_raw(&im, 0, 2 + i, 0, "x", 1, 50 + i);
    }

    struct collect c = {{{{0}}}, 0, 2, -7};
    assert_int_equal(ubifs_mem(im.b, img_len(&im), collect_cb, &c), -7);
    assert_int_equal(c.n, 2);
}

static void test_an_empty_volume_emits_nothing(void **st) {
    (void)st;
    struct img im;
    two_leb_volume(&im);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(ubifs_mem(im.b, img_len(&im), collect_cb, &c), 0);
    assert_int_equal(c.n, 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rejects_non_ubi_input),
        cmocka_unit_test(test_rejects_a_short_image),
        cmocka_unit_test(test_rejects_an_image_with_no_volume),

        cmocka_unit_test(test_a_file_in_the_root),
        cmocka_unit_test(test_nested_directories),
        cmocka_unit_test(test_symlink_target_comes_from_the_inline_data),
        cmocka_unit_test(test_special_nodes_are_skipped),
        cmocka_unit_test(test_a_dentry_without_an_inode_is_skipped),

        cmocka_unit_test(test_the_highest_sqnum_inode_wins),
        cmocka_unit_test(test_the_highest_sqnum_data_block_wins),
        cmocka_unit_test(test_the_highest_sqnum_dentry_wins),
        cmocka_unit_test(test_the_same_name_in_two_directories),
        cmocka_unit_test(test_a_hardlink_is_emitted_at_every_path),

        cmocka_unit_test(test_a_multi_block_file_reassembles_in_order),
        cmocka_unit_test(test_a_missing_block_reads_as_zeros),
        cmocka_unit_test(test_an_empty_file),

        cmocka_unit_test(test_a_zlib_compressed_block),
        cmocka_unit_test(test_an_lzo_compressed_block),
        cmocka_unit_test(test_an_unknown_compression_id_yields_no_data),

        cmocka_unit_test(test_the_largest_volume_is_chosen),
        cmocka_unit_test(test_nodes_are_found_across_several_lebs),
        cmocka_unit_test(test_a_peb_without_a_vid_header_is_ignored),

        cmocka_unit_test(test_garbage_between_nodes_is_stepped_over),
        cmocka_unit_test(test_a_node_longer_than_its_leb_is_refused),
        cmocka_unit_test(test_an_unknown_node_type_is_skipped),
        cmocka_unit_test(test_an_overlong_dentry_name_is_refused),

        cmocka_unit_test(test_a_nonzero_callback_return_aborts),
        cmocka_unit_test(test_an_empty_volume_emits_nothing),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
