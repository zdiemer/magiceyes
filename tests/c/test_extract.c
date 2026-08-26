/* Unit tests for host/engine/extract/{untar,yaffs}.c -- the in-memory firmware image walkers.
 *
 * These are pure buffer-in, callback-out readers with no globals and no I/O, so the fixtures are
 * archives built byte by byte in the test. That matters here more than usual: the real inputs are
 * device firmware images, which are neither in the repo nor redistributable, so without built
 * fixtures this code could not be tested at all.
 *
 * untar.c deliberately does NOT verify the header checksum, so nothing here computes one.
 *
 * ME_TEST_SRC: host/engine/extract/untar.c host/engine/extract/yaffs.c
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "untar.h"
#include "yaffs.h"

/* ---- collecting callback -------------------------------------------------------------------- */

struct entry {
    char          path[512];
    int           type;
    char          link[256];
    unsigned char data[512];
    size_t        size;
    unsigned      mode;
};

struct collect {
    struct entry e[32];
    int          n;
    int          abort_after;   /* 0 = never abort */
    int          abort_rc;
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
        size_t n = size < sizeof e->data ? size : sizeof e->data;
        if (data && n) memcpy(e->data, data, n);
        c->n++;
    }
    if (c->abort_after && c->n >= c->abort_after) return c->abort_rc;
    return 0;
}

static struct entry *by_path(struct collect *c, const char *path) {
    for (int i = 0; i < c->n; i++) if (!strcmp(c->e[i].path, path)) return &c->e[i];
    return NULL;
}

/* ---- tar builder ------------------------------------------------------------------------------ */

struct tarbuf { unsigned char b[65536]; size_t n; };

/* ustar numeric fields are octal, NUL-terminated within their width. */
static void oct(char *dst, int width, unsigned long v) {
    memset(dst, '0', (size_t)width - 1);
    dst[width - 1] = 0;
    for (int i = width - 2; i >= 0 && v; i--) { dst[i] = (char)('0' + (v & 7)); v >>= 3; }
}

static unsigned char *tar_hdr(struct tarbuf *t, const char *name, unsigned long size,
                              char type, const char *link, unsigned mode, const char *prefix) {
    unsigned char *h = t->b + t->n;
    memset(h, 0, 512);
    snprintf((char *)h + 0, 100, "%s", name);
    oct((char *)h + 100, 8, mode);
    oct((char *)h + 124, 12, size);
    h[156] = (unsigned char)type;
    if (link) snprintf((char *)h + 157, 100, "%s", link);
    memcpy(h + 257, "ustar", 5);
    if (prefix) snprintf((char *)h + 345, 155, "%s", prefix);
    t->n += 512;
    return h;
}

static void tar_data(struct tarbuf *t, const void *data, size_t size) {
    size_t blocks = (size + 511) / 512;
    memset(t->b + t->n, 0, blocks * 512);
    if (data && size) memcpy(t->b + t->n, data, size);
    t->n += blocks * 512;
}

static void tar_file(struct tarbuf *t, const char *name, const char *content, unsigned mode) {
    tar_hdr(t, name, strlen(content), '0', "", mode, NULL);
    tar_data(t, content, strlen(content));
}

static void tar_end(struct tarbuf *t) {
    memset(t->b + t->n, 0, 1024);
    t->n += 1024;
}

/* ---- untar ------------------------------------------------------------------------------------- */

static void test_untar_regular_file(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_file(&t, "etc/profile", "export PATH=/bin\n", 0644);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(untar_mem(t.b, t.n, collect_cb, &c), 0);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "etc/profile");
    assert_int_equal(c.e[0].type, TAR_FILE);
    assert_int_equal(c.e[0].size, strlen("export PATH=/bin\n"));
    assert_memory_equal(c.e[0].data, "export PATH=/bin\n", c.e[0].size);
    assert_int_equal(c.e[0].mode, 0644);
}

static void test_untar_entry_kinds(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_hdr(&t, "usr/lib", 0, '5', "", 0755, NULL);
    tar_hdr(&t, "usr/lib/libSDL.so", 0, '2', "libSDL-1.2.so.0", 0777, NULL);
    tar_hdr(&t, "usr/bin/alias", 0, '1', "usr/bin/real", 0755, NULL);
    tar_file(&t, "usr/bin/real", "ELF", 0755);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(untar_mem(t.b, t.n, collect_cb, &c), 0);
    assert_int_equal(c.n, 4);
    assert_int_equal(by_path(&c, "usr/lib")->type, TAR_DIR);

    struct entry *sl = by_path(&c, "usr/lib/libSDL.so");
    assert_int_equal(sl->type, TAR_SYMLINK);
    assert_string_equal(sl->link, "libSDL-1.2.so.0");

    struct entry *hl = by_path(&c, "usr/bin/alias");
    assert_int_equal(hl->type, TAR_HARDLINK);
    assert_string_equal(hl->link, "usr/bin/real");
}

/* A NUL typeflag is the pre-ustar spelling of "regular file", and '7' is contiguous; both are
   ordinary files as far as staging is concerned. */
static void test_untar_legacy_regular_typeflags(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_hdr(&t, "old", 2, '\0', "", 0644, NULL);
    tar_data(&t, "ab", 2);
    tar_hdr(&t, "contig", 2, '7', "", 0644, NULL);
    tar_data(&t, "cd", 2);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 0, 0};
    untar_mem(t.b, t.n, collect_cb, &c);
    assert_int_equal(c.n, 2);
    assert_int_equal(by_path(&c, "old")->type, TAR_FILE);
    assert_int_equal(by_path(&c, "contig")->type, TAR_FILE);
}

/* Device, fifo and other special nodes are skipped rather than staged as empty files. */
static void test_untar_skips_special_nodes(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_hdr(&t, "dev/null", 0, '3', "", 0666, NULL);      /* character device */
    tar_hdr(&t, "dev/sda", 0, '4', "", 0660, NULL);       /* block device */
    tar_hdr(&t, "dev/pipe", 0, '6', "", 0666, NULL);      /* fifo */
    tar_file(&t, "real", "x", 0644);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 0, 0};
    untar_mem(t.b, t.n, collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "real");
}

/* ustar splits long paths across prefix and name; they must be rejoined with a slash. */
static void test_untar_joins_the_ustar_prefix(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_hdr(&t, "libSDL-1.2.so.0", 3, '0', "", 0755, "usr/lib/deeply/nested");
    tar_data(&t, "abc", 3);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 0, 0};
    untar_mem(t.b, t.n, collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "usr/lib/deeply/nested/libSDL-1.2.so.0");
}

/* A GNU 'L' entry carries the real name for the NEXT entry, and must not itself be reported. */
static void test_untar_gnu_long_name(void **st) {
    (void)st;
    char longname[300];
    memset(longname, 'a', sizeof longname - 1);
    longname[sizeof longname - 1] = 0;

    struct tarbuf t = {{0}, 0};
    tar_hdr(&t, "././@LongLink", strlen(longname) + 1, 'L', "", 0644, NULL);
    tar_data(&t, longname, strlen(longname) + 1);
    tar_hdr(&t, "truncated-name", 2, '0', "", 0644, NULL);
    tar_data(&t, "hi", 2);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 0, 0};
    untar_mem(t.b, t.n, collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, longname);
}

/* The pending long name applies to one entry only. */
static void test_untar_long_name_does_not_leak_to_the_next_entry(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_hdr(&t, "././@LongLink", 6, 'L', "", 0644, NULL);
    tar_data(&t, "first", 6);
    tar_file(&t, "ignored", "a", 0644);
    tar_file(&t, "second", "b", 0644);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 0, 0};
    untar_mem(t.b, t.n, collect_cb, &c);
    assert_int_equal(c.n, 2);
    assert_string_equal(c.e[0].path, "first");
    assert_string_equal(c.e[1].path, "second");
}

static void test_untar_gnu_long_link(void **st) {
    (void)st;
    char target[200];
    memset(target, 'b', sizeof target - 1);
    target[sizeof target - 1] = 0;

    struct tarbuf t = {{0}, 0};
    tar_hdr(&t, "././@LongLink", strlen(target) + 1, 'K', "", 0644, NULL);
    tar_data(&t, target, strlen(target) + 1);
    tar_hdr(&t, "link", 0, '2', "short", 0777, NULL);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 0, 0};
    untar_mem(t.b, t.n, collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].link, target);       /* the long target wins over the 100-byte one */
}

/* A file whose size is not a multiple of 512 still leaves the next header block-aligned. */
static void test_untar_pads_to_the_block_size(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_file(&t, "a", "1", 0644);          /* 1 byte -> one padded block */
    tar_file(&t, "b", "22", 0644);
    tar_file(&t, "c", "333", 0644);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 0, 0};
    untar_mem(t.b, t.n, collect_cb, &c);
    assert_int_equal(c.n, 3);
    assert_string_equal(c.e[2].path, "c");
    assert_int_equal(c.e[2].size, 3);
}

/* Two zero blocks end the archive; anything after them is not part of it. */
static void test_untar_stops_at_the_end_marker(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_file(&t, "before", "x", 0644);
    tar_end(&t);
    tar_file(&t, "after", "y", 0644);

    struct collect c = {{{{0}}}, 0, 0, 0};
    untar_mem(t.b, t.n, collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "before");
}

/* A truncated archive must not hand the callback data that runs off the end of the buffer. */
static void test_untar_ignores_an_entry_past_the_end(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_file(&t, "ok", "x", 0644);
    tar_hdr(&t, "truncated", 4096, '0', "", 0644, NULL);   /* claims 4096 bytes that are absent */

    struct collect c = {{{{0}}}, 0, 0, 0};
    untar_mem(t.b, t.n, collect_cb, &c);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "ok");
}

/* A nonzero callback return aborts the walk and is passed back to the caller. */
static void test_untar_callback_can_abort(void **st) {
    (void)st;
    struct tarbuf t = {{0}, 0};
    tar_file(&t, "a", "1", 0644);
    tar_file(&t, "b", "2", 0644);
    tar_file(&t, "c", "3", 0644);
    tar_end(&t);

    struct collect c = {{{{0}}}, 0, 2, -7};
    assert_int_equal(untar_mem(t.b, t.n, collect_cb, &c), -7);
    assert_int_equal(c.n, 2);
}

static void test_untar_empty_and_tiny_input(void **st) {
    (void)st;
    struct collect c = {{{{0}}}, 0, 0, 0};
    unsigned char tiny[16] = {0};
    assert_int_equal(untar_mem(tiny, 0, collect_cb, &c), 0);
    assert_int_equal(untar_mem(tiny, sizeof tiny, collect_cb, &c), 0);
    assert_int_equal(c.n, 0);
}

/* ---- yaffs ------------------------------------------------------------------------------------- */

#define Y_PAGE  2048
#define Y_SPARE 64
#define Y_STEP  (Y_PAGE + Y_SPARE)

struct yaffsbuf { unsigned char b[Y_STEP * 8]; size_t n; };

static void put_le32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* One YAFFS2 chunk: 2048 bytes of page followed by a 64-byte spare carrying PackedTags2. */
static unsigned char *y_chunk(struct yaffsbuf *y, unsigned objid, unsigned cid, unsigned nbytes) {
    unsigned char *page = y->b + y->n;
    memset(page, 0, Y_STEP);
    unsigned char *sp = page + Y_PAGE;
    put_le32(sp + 0, 1);          /* sequence */
    put_le32(sp + 4, objid);
    put_le32(sp + 8, cid);
    put_le32(sp + 12, nbytes);
    y->n += Y_STEP;
    return page;
}

/* An object header lives in the page of chunk 0: type@0, parent@4, name@10, mode@268, size@292. */
static void y_objhdr(struct yaffsbuf *y, unsigned objid, unsigned type, unsigned parent,
                     const char *name, unsigned size, unsigned mode) {
    unsigned char *p = y_chunk(y, objid, 0, 0);
    put_le32(p + 0, type);
    put_le32(p + 4, parent);
    snprintf((char *)p + 10, 255, "%s", name);
    put_le32(p + 268, mode);
    put_le32(p + 292, size);
}

/* Geometry is inferred purely from the image length, so a length that matches neither page size
   is refused rather than guessed at. */
static void test_yaffs_geometry_detection(void **st) {
    (void)st;
    struct collect c = {{{{0}}}, 0, 0, 0};
    static unsigned char blank[Y_STEP * 2];
    memset(blank, 0, sizeof blank);

    assert_int_equal(yaffs_mem(blank, Y_STEP, collect_cb, &c), 0);        /* yaffs2: 2112 */
    assert_int_equal(yaffs_mem(blank, 528, collect_cb, &c), 0);           /* yaffs1: 528 */
    assert_int_equal(yaffs_mem(blank, 100, collect_cb, &c), -1);          /* neither */
    assert_int_equal(yaffs_mem(blank, 1000, collect_cb, &c), -1);
    assert_int_equal(c.n, 0);
}

static void test_yaffs_reads_a_file_in_the_root(void **st) {
    (void)st;
    struct yaffsbuf y = {{0}, 0};
    y_objhdr(&y, 2, 1, 1, "readme.txt", 5, 0644);       /* type 1 = regular file, parent = root */
    unsigned char *data = y_chunk(&y, 2, 1, 5);
    memcpy(data, "hello", 5);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(yaffs_mem(y.b, y.n, collect_cb, &c), 0);
    assert_int_equal(c.n, 1);
    assert_string_equal(c.e[0].path, "readme.txt");
    assert_int_equal(c.e[0].type, TAR_FILE);
    assert_int_equal(c.e[0].size, 5);
    assert_memory_equal(c.e[0].data, "hello", 5);
}

/* Paths are rebuilt by walking the parent chain, and the root object itself is not emitted. */
static void test_yaffs_rebuilds_nested_paths(void **st) {
    (void)st;
    struct yaffsbuf y = {{0}, 0};
    y_objhdr(&y, 2, 3, 1, "usr", 0, 0755);              /* type 3 = directory */
    y_objhdr(&y, 3, 3, 2, "gp2x", 0, 0755);
    y_objhdr(&y, 4, 1, 3, "font.ttf", 3, 0644);
    unsigned char *data = y_chunk(&y, 4, 1, 3);
    memcpy(data, "TTF", 3);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(yaffs_mem(y.b, y.n, collect_cb, &c), 0);
    assert_int_equal(c.n, 3);
    assert_non_null(by_path(&c, "usr"));
    assert_non_null(by_path(&c, "usr/gp2x"));
    struct entry *f = by_path(&c, "usr/gp2x/font.ttf");
    assert_non_null(f);
    assert_memory_equal(f->data, "TTF", 3);
}

static void test_yaffs_symlink(void **st) {
    (void)st;
    struct yaffsbuf y = {{0}, 0};
    unsigned char *p = y_chunk(&y, 2, 0, 0);
    put_le32(p + 0, 2);                                  /* type 2 = symlink */
    put_le32(p + 4, 1);
    snprintf((char *)p + 10, 255, "%s", "libSDL.so");
    snprintf((char *)p + 300, 159, "%s", "libSDL-1.2.so.0");   /* alias */

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(yaffs_mem(y.b, y.n, collect_cb, &c), 0);
    assert_int_equal(c.n, 1);
    assert_int_equal(c.e[0].type, TAR_SYMLINK);
    assert_string_equal(c.e[0].link, "libSDL-1.2.so.0");
}

/* A file reassembles from its data chunks in chunk-id order regardless of the order they appear
   in the image, and is clipped to the size the header declares. */
static void test_yaffs_reassembles_chunks_in_order(void **st) {
    (void)st;
    struct yaffsbuf y = {{0}, 0};
    y_objhdr(&y, 2, 1, 1, "split", 6, 0644);
    unsigned char *second = y_chunk(&y, 2, 2, 3);        /* written out of order on purpose */
    memcpy(second, "DEF", 3);
    unsigned char *first = y_chunk(&y, 2, 1, 3);
    memcpy(first, "ABC", 3);

    struct collect c = {{{{0}}}, 0, 0, 0};
    assert_int_equal(yaffs_mem(y.b, y.n, collect_cb, &c), 0);
    assert_int_equal(c.n, 1);
    assert_int_equal(c.e[0].size, 6);
    assert_memory_equal(c.e[0].data, "ABCDEF", 6);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_untar_regular_file),
        cmocka_unit_test(test_untar_entry_kinds),
        cmocka_unit_test(test_untar_legacy_regular_typeflags),
        cmocka_unit_test(test_untar_skips_special_nodes),
        cmocka_unit_test(test_untar_joins_the_ustar_prefix),
        cmocka_unit_test(test_untar_gnu_long_name),
        cmocka_unit_test(test_untar_long_name_does_not_leak_to_the_next_entry),
        cmocka_unit_test(test_untar_gnu_long_link),
        cmocka_unit_test(test_untar_pads_to_the_block_size),
        cmocka_unit_test(test_untar_stops_at_the_end_marker),
        cmocka_unit_test(test_untar_ignores_an_entry_past_the_end),
        cmocka_unit_test(test_untar_callback_can_abort),
        cmocka_unit_test(test_untar_empty_and_tiny_input),
        cmocka_unit_test(test_yaffs_geometry_detection),
        cmocka_unit_test(test_yaffs_reads_a_file_in_the_root),
        cmocka_unit_test(test_yaffs_rebuilds_nested_paths),
        cmocka_unit_test(test_yaffs_symlink),
        cmocka_unit_test(test_yaffs_reassembles_chunks_in_order),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
