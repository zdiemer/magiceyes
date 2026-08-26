/* Unit tests for host/win/posix_compat.c -- the POSIX calls the engine needs, over Win32.
 *
 * ME_TEST_ONLY: win
 * ME_TEST_LIBS: -lwinmm
 *
 * This file is where the native-Windows black-screen bugs lived: a short-reading pread left a
 * file-backed mapping half-zeroed, which corrupted the guest's .dynsym and surfaced only as
 * "undefined symbol ... version GLIBC_2.0" much later. Every one of these is a thin shim whose
 * failure mode is silent corruption rather than a crash, which is exactly what unit tests are for.
 *
 * We #include the .c to reach win_name and prot_page, both static.
 *
 * Deliberately not covered: me_platform_init (it can freopen the standard streams, which would
 * swallow the test runner's own output) and me_usleep (its sub-millisecond busy-spin is
 * timing-dependent and would flake on a loaded CI runner).
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include <fcntl.h>

#include "../../host/win/posix_compat.c"

/* ---- helpers ------------------------------------------------------------------------------- */

static void tmp_path(char *out, size_t cap, const char *tag) {
    const char *base = getenv("TEMP");
    if (!base || !*base) base = getenv("TMP");
    if (!base || !*base) base = ".";
    snprintf(out, cap, "%s\\me_posix_test_%s", base, tag);
}

static void write_file(const char *path, const void *data, size_t n) {
    FILE *f = fopen(path, "wb");
    assert_non_null(f);
    fwrite(data, 1, n, f);
    fclose(f);
}

/* ---- win_name ------------------------------------------------------------------------------- */

/* A POSIX shm name is "/gp2x_fb"; Win32 object names cannot carry the leading slash, and the
   "Local\" prefix keeps the object in the session namespace so engine and viewer find each other. */
static void test_win_name_maps_posix_shm_names(void **st) {
    (void)st;
    char n[256];
    win_name(n, sizeof n, "/gp2x_fb");
    assert_string_equal(n, "Local\\magiceyes_gp2x_fb");

    win_name(n, sizeof n, "gp2x_fb");            /* already bare */
    assert_string_equal(n, "Local\\magiceyes_gp2x_fb");

    win_name(n, sizeof n, "///gp2x_fb");         /* every leading slash goes */
    assert_string_equal(n, "Local\\magiceyes_gp2x_fb");
}

/* ---- prot_page ------------------------------------------------------------------------------- */

/* Getting this wrong hands the guest a page it cannot execute or write, and the failure shows up
   as an access violation deep inside emulated code rather than here. */
static void test_prot_page_translation(void **st) {
    (void)st;
    assert_int_equal(prot_page(PROT_NONE), PAGE_NOACCESS);
    assert_int_equal(prot_page(PROT_READ), PAGE_READONLY);
    assert_int_equal(prot_page(PROT_WRITE), PAGE_READWRITE);
    assert_int_equal(prot_page(PROT_READ | PROT_WRITE), PAGE_READWRITE);
    assert_int_equal(prot_page(PROT_EXEC), PAGE_EXECUTE_READ);
    assert_int_equal(prot_page(PROT_READ | PROT_EXEC), PAGE_EXECUTE_READ);
    assert_int_equal(prot_page(PROT_READ | PROT_WRITE | PROT_EXEC), PAGE_EXECUTE_READWRITE);
}

/* ---- setenv ---------------------------------------------------------------------------------- */

static void test_setenv_overwrite_semantics(void **st) {
    (void)st;
    assert_int_equal(setenv("ME_TEST_VAR", "first", 1), 0);
    assert_string_equal(getenv("ME_TEST_VAR"), "first");

    assert_int_equal(setenv("ME_TEST_VAR", "second", 0), 0);      /* must not clobber */
    assert_string_equal(getenv("ME_TEST_VAR"), "first");

    assert_int_equal(setenv("ME_TEST_VAR", "third", 1), 0);
    assert_string_equal(getenv("ME_TEST_VAR"), "third");
}

static void test_setenv_null_value_is_empty(void **st) {
    (void)st;
    assert_int_equal(setenv("ME_TEST_NULLVAR", NULL, 1), 0);
    const char *v = getenv("ME_TEST_NULLVAR");
    /* Windows treats "NAME=" as a delete, so the variable reads back as absent or empty; both
       are acceptable, a stale previous value is not. */
    assert_true(v == NULL || v[0] == 0);
}

/* ---- pread ------------------------------------------------------------------------------------ */

static void test_pread_reads_at_an_offset(void **st) {
    (void)st;
    char path[512];
    tmp_path(path, sizeof path, "pread.bin");
    static unsigned char data[4096];
    for (size_t i = 0; i < sizeof data; i++) data[i] = (unsigned char)(i * 31 + 7);
    write_file(path, data, sizeof data);

    int fd = _open(path, _O_RDONLY | _O_BINARY);
    assert_true(fd >= 0);

    unsigned char buf[256];
    assert_int_equal(pread(fd, buf, sizeof buf, 1024), (ssize_t)sizeof buf);
    assert_memory_equal(buf, data + 1024, sizeof buf);

    /* reading the same range again must yield the same bytes: the offset is explicit, not
       inherited from wherever the previous read left the file pointer */
    memset(buf, 0, sizeof buf);
    assert_int_equal(pread(fd, buf, sizeof buf, 1024), (ssize_t)sizeof buf);
    assert_memory_equal(buf, data + 1024, sizeof buf);

    _close(fd);
    remove(path);
}

/* A read that runs into the end of the file returns what was actually there. A short read that
   silently reported success is what left a mapped library segment half-zeroed. */
static void test_pread_short_read_at_end_of_file(void **st) {
    (void)st;
    char path[512];
    tmp_path(path, sizeof path, "preadshort.bin");
    unsigned char data[100];
    for (int i = 0; i < 100; i++) data[i] = (unsigned char)i;
    write_file(path, data, sizeof data);

    int fd = _open(path, _O_RDONLY | _O_BINARY);
    assert_true(fd >= 0);

    unsigned char buf[256];
    assert_int_equal(pread(fd, buf, sizeof buf, 60), 40);     /* only 40 bytes remain */
    assert_memory_equal(buf, data + 60, 40);

    assert_int_equal(pread(fd, buf, sizeof buf, 100), 0);     /* exactly at EOF */
    assert_int_equal(pread(fd, buf, sizeof buf, 5000), 0);    /* past EOF */

    _close(fd);
    remove(path);
}

static void test_pread_on_a_bad_descriptor(void **st) {
    (void)st;
    unsigned char buf[16];
    assert_int_equal(pread(-1, buf, sizeof buf, 0), -1);
}

/* ---- lstat ------------------------------------------------------------------------------------- */

/* GP2X assets carry no symlinks, so lstat is stat; what matters is that it reports a real size. */
static void test_lstat_reports_the_file(void **st) {
    (void)st;
    char path[512];
    tmp_path(path, sizeof path, "lstat.bin");
    write_file(path, "0123456789", 10);

    struct stat s;
    memset(&s, 0, sizeof s);
    assert_int_equal(lstat(path, &s), 0);
    assert_int_equal(s.st_size, 10);
    assert_true(s.st_mode & S_IFREG);

    assert_int_equal(lstat("no_such_file_at_all.bin", &s), -1);
    remove(path);
}

/* ---- anonymous mmap ------------------------------------------------------------------------------ */

static void test_anonymous_mapping_round_trip(void **st) {
    (void)st;
    size_t len = 64 * 1024;
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert_true(p != MAP_FAILED);

    memset(p, 0xa5, len);
    assert_int_equal(((unsigned char *)p)[0], 0xa5);
    assert_int_equal(((unsigned char *)p)[len - 1], 0xa5);

    assert_int_equal(munmap(p, len), 0);
}

/* Anonymous pages come back zeroed, which guest code relies on for .bss. */
static void test_anonymous_mapping_is_zeroed(void **st) {
    (void)st;
    size_t len = 16 * 1024;
    unsigned char *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert_true((void *)p != MAP_FAILED);
    for (size_t i = 0; i < len; i += 512) assert_int_equal(p[i], 0);
    assert_int_equal(munmap(p, len), 0);
}

/* An executable mapping is what the guest's in-heap code allocators need. */
static void test_executable_mapping(void **st) {
    (void)st;
    size_t len = 4096;
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert_true(p != MAP_FAILED);
    memset(p, 0x90, len);
    assert_int_equal(munmap(p, len), 0);
}

/* A file-backed mapping of an ordinary descriptor is not supported and must say so rather than
   hand back a plausible pointer. */
static void test_file_backed_mapping_is_refused(void **st) {
    (void)st;
    assert_true(mmap(NULL, 4096, PROT_READ, MAP_SHARED, 3, 0) == MAP_FAILED);
}

/* ---- shared memory -------------------------------------------------------------------------- */

/* The engine/viewer bridge: two independent maps of the same name are the same memory. */
static void test_shared_mapping_is_shared_by_name(void **st) {
    (void)st;
    const size_t len = 8192;

    int fd1 = shm_open("/me_test_shm", 0, 0600);
    assert_true(fd1 >= 0);
    unsigned char *a = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
    assert_true((void *)a != MAP_FAILED);

    int fd2 = shm_open("/me_test_shm", 0, 0600);
    assert_true(fd2 >= 0);
    unsigned char *b = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    assert_true((void *)b != MAP_FAILED);
    assert_true(a != b);                          /* different views ... */

    memcpy(a, "framebuffer", 12);
    assert_string_equal((char *)b, "framebuffer");  /* ... of the same object */

    b[0] = 'F';
    assert_int_equal(a[0], 'F');

    assert_int_equal(munmap(a, len), 0);
    assert_int_equal(munmap(b, len), 0);
    assert_int_equal(shm_unlink("/me_test_shm"), 0);
}

/* Distinct names must not alias. */
static void test_distinct_shm_names_are_distinct(void **st) {
    (void)st;
    const size_t len = 4096;
    int fd1 = shm_open("/me_test_one", 0, 0600);
    int fd2 = shm_open("/me_test_two", 0, 0600);
    unsigned char *a = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
    unsigned char *b = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    assert_true((void *)a != MAP_FAILED);
    assert_true((void *)b != MAP_FAILED);

    memset(a, 0x11, len);
    memset(b, 0x22, len);
    assert_int_equal(a[0], 0x11);

    munmap(a, len);
    munmap(b, len);
}

/* munmap has to tell a mapped view from a VirtualAlloc block; unmapping a view twice must not
   claim success the second time. */
static void test_munmap_distinguishes_views_from_allocations(void **st) {
    (void)st;
    const size_t len = 4096;
    int fd = shm_open("/me_test_unmap", 0, 0600);
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert_true(p != MAP_FAILED);
    assert_int_equal(munmap(p, len), 0);
    assert_int_equal(munmap(p, len), -1);         /* no longer a known view, not a valid block */
}

/* The descriptor table is small and fixed; running it out returns an error rather than
   overrunning the array. Runs last because it consumes the table. */
static void test_shm_descriptor_table_is_bounded(void **st) {
    (void)st;
    int last = 0;
    for (int i = 0; i < 32; i++) {
        char name[64];
        snprintf(name, sizeof name, "/me_test_bulk_%d", i);
        last = shm_open(name, 0, 0600);
        if (last < 0) break;
    }
    assert_int_equal(last, -1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_win_name_maps_posix_shm_names),
        cmocka_unit_test(test_prot_page_translation),
        cmocka_unit_test(test_setenv_overwrite_semantics),
        cmocka_unit_test(test_setenv_null_value_is_empty),
        cmocka_unit_test(test_pread_reads_at_an_offset),
        cmocka_unit_test(test_pread_short_read_at_end_of_file),
        cmocka_unit_test(test_pread_on_a_bad_descriptor),
        cmocka_unit_test(test_lstat_reports_the_file),
        cmocka_unit_test(test_anonymous_mapping_round_trip),
        cmocka_unit_test(test_anonymous_mapping_is_zeroed),
        cmocka_unit_test(test_executable_mapping),
        cmocka_unit_test(test_file_backed_mapping_is_refused),
        cmocka_unit_test(test_shared_mapping_is_shared_by_name),
        cmocka_unit_test(test_distinct_shm_names_are_distinct),
        cmocka_unit_test(test_munmap_distinguishes_views_from_allocations),
        cmocka_unit_test(test_shm_descriptor_table_is_bounded),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
