/* Unit tests for host/engine/hostabi.c -- the host <-> Linux/ARM ABI translation.
 *
 * ME_TEST_SRC: host/engine/hostabi.c
 *
 * Every one of these has already cost a debugging session, and none of them fail loudly:
 *
 *   host_open_flags   missing O_BINARY let msvcrt open GP2X assets in TEXT mode, translating CRLF
 *                     and stopping at the first 0x1A. The assets "loaded", the pixels were
 *                     garbage, the game drew nothing: the native-Windows black screen.
 *   linux_errno       ENOSYS is 38 on Linux and 40 on MinGW, so a failed syscall sent the guest's
 *                     glibc down the wrong branch.
 *   pack_stat64       the OABI struct is 96 bytes, not 104. Writing 104 overflowed the caller's
 *                     frame in _IO_file_doallocate onto its saved r5, nulling a FILE*: the Payback
 *                     load crash.
 *   stat_ino32        a 64-bit inode from a drvfs mount made the guest's 32-bit fstat() return
 *                     EOVERFLOW, which Caanoo QType4 reports as "TTF Font File Open Failed".
 *
 * The layouts are asserted field by field at their documented offsets, so this is a real check of
 * the ABI rather than a round-trip through the same code that writes it.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <cmocka.h>

#include "hostabi.h"

/* The packed fields are not naturally aligned (st_size sits at 44 in the OABI layout), so every
   read goes through memcpy rather than a cast. */
static uint16_t rd16(const uint8_t *b, size_t off) { uint16_t v; memcpy(&v, b + off, 2); return v; }
static uint32_t rd32(const uint8_t *b, size_t off) { uint32_t v; memcpy(&v, b + off, 4); return v; }
static uint64_t rd64(const uint8_t *b, size_t off) { uint64_t v; memcpy(&v, b + off, 8); return v; }

static struct stat sample(void) {
    struct stat s;
    memset(&s, 0, sizeof s);
    s.st_dev   = 0x0801;
    s.st_ino   = 0x1234;
    s.st_mode  = S_IFREG | 0644;
    s.st_nlink = 1;
    s.st_uid   = 1000;
    s.st_gid   = 1000;
    s.st_rdev  = 0;
    s.st_size  = 4096;
    return s;
}

/* ---- host_open_flags --------------------------------------------------------------------- */

static void test_open_flags_preserve_the_access_mode(void **st) {
    (void)st;
    /* 0/1/2 mean the same on both platforms and must survive untouched. */
    assert_int_equal(host_open_flags(0) & 3, 0);
    assert_int_equal(host_open_flags(1) & 3, 1);
    assert_int_equal(host_open_flags(2) & 3, 2);
}

#ifdef _WIN32
/* The one that actually mattered: nothing may reach msvcrt without O_BINARY. */
static void test_open_flags_always_binary_on_windows(void **st) {
    (void)st;
    const int guest_flags[] = {0, 1, 2, 0100, 01000, 02000, 0100 | 01000};
    for (size_t i = 0; i < sizeof guest_flags / sizeof guest_flags[0]; i++)
        assert_true(host_open_flags(guest_flags[i]) & O_BINARY);
}

static void test_open_flags_translate_the_bit_values(void **st) {
    (void)st;
    /* Linux O_CREAT is 0100 octal; MinGW's is a different bit entirely. */
    assert_true(host_open_flags(0100) & O_CREAT);
    assert_true(host_open_flags(0200) & O_EXCL);
    assert_true(host_open_flags(01000) & O_TRUNC);
    assert_true(host_open_flags(02000) & O_APPEND);
}

static void test_open_flags_do_not_invent_creation(void **st) {
    (void)st;
    /* A plain read must not come back as O_CREAT|O_TRUNC, which would truncate a game asset. */
    int hf = host_open_flags(0);
    assert_false(hf & O_CREAT);
    assert_false(hf & O_TRUNC);
    assert_false(hf & O_APPEND);
}
#else
/* On Linux the guest IS the host, so translating at all would be the bug. */
static void test_open_flags_are_identity_on_linux(void **st) {
    (void)st;
    const int guest_flags[] = {0, 1, 2, O_CREAT, O_TRUNC, O_APPEND, O_CREAT | O_WRONLY | O_TRUNC};
    for (size_t i = 0; i < sizeof guest_flags / sizeof guest_flags[0]; i++)
        assert_int_equal(host_open_flags(guest_flags[i]), guest_flags[i]);
}
#endif

/* ---- linux_errno ------------------------------------------------------------------------- */

/* 1..34 are the same on both platforms; a translation here would be a regression. */
static void test_common_errnos_pass_through(void **st) {
    (void)st;
    assert_int_equal(linux_errno(ENOENT), ENOENT);
    assert_int_equal(linux_errno(EBADF), EBADF);
    assert_int_equal(linux_errno(EACCES), EACCES);
    assert_int_equal(linux_errno(EINVAL), EINVAL);
    assert_int_equal(linux_errno(0), 0);
}

/* The guest is Linux/ARM, so these are the numbers its glibc compares against, whatever the host
   happens to call them. */
static void test_high_errnos_are_the_linux_numbers(void **st) {
    (void)st;
    assert_int_equal(linux_errno(ENAMETOOLONG), 36);
    assert_int_equal(linux_errno(ENOLCK), 37);
    assert_int_equal(linux_errno(ENOSYS), 38);
    assert_int_equal(linux_errno(ENOTEMPTY), 39);
#ifdef ELOOP
    assert_int_equal(linux_errno(ELOOP), 40);
#endif
    assert_int_equal(linux_errno(EDEADLK), 35);
    assert_int_equal(linux_errno(EILSEQ), 84);
}

/* ---- stat_ino32 -------------------------------------------------------------------------- */

static void test_ino_is_never_zero(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_ino = 0;
    assert_int_equal(stat_ino32(&s), 1);
}

static void test_ino_passes_small_values_through(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_ino = 0x1234;
    assert_int_equal(stat_ino32(&s), 0x1234);
}

#ifndef _WIN32
/* drvfs hands back huge 64-bit inodes; the guest's 32-bit fstat() would return EOVERFLOW on them.
   (MinGW's st_ino is only 16 bits, so there is nothing to truncate there.) */
static void test_ino_truncates_a_huge_inode(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_ino = 0x1234567890ABCDEFULL;
    assert_int_equal(stat_ino32(&s), 0x90ABCDEFu);
}

/* Truncation must not produce a zero either, or the guest sees inode 0. */
static void test_ino_truncating_to_zero_still_yields_one(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_ino = 0x1234567800000000ULL;
    assert_int_equal(stat_ino32(&s), 1);
}
#endif

/* ---- pack_oabi_stat ---------------------------------------------------------------------- */

static void test_oabi_stat_size_and_layout(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_dev = 0x0801; s.st_rdev = 0x1234; s.st_size = 8192;

    uint8_t b[128];
    memset(b, 0xAA, sizeof b);
    assert_int_equal(pack_oabi_stat(b, &s), 88);

    assert_int_equal(rd32(b, 0), 0x0801);                 /* st_dev */
    assert_int_equal(rd32(b, 4), 0x1234);                 /* st_ino (untruncated here) */
    assert_int_equal(rd16(b, 8), (uint16_t)s.st_mode);    /* st_mode */
    assert_int_equal(rd16(b, 10), 1);                     /* st_nlink */
    assert_int_equal(rd32(b, 16), 0x1234);                /* st_rdev */
    assert_int_equal(rd32(b, 20), 8192);                  /* st_size */
    assert_int_equal(rd32(b, 24), 4096);                  /* st_blksize */
    assert_int_equal(rd32(b, 28), 8192 / 512);            /* st_blocks */
}

static void test_oabi_stat_does_not_write_past_88(void **st) {
    (void)st;
    struct stat s = sample();
    uint8_t b[128];
    memset(b, 0xAA, sizeof b);
    pack_oabi_stat(b, &s);
    for (size_t i = 88; i < sizeof b; i++) assert_int_equal(b[i], 0xAA);
}

static void test_oabi_stat_defaults_nlink(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_nlink = 0;
    uint8_t b[128];
    pack_oabi_stat(b, &s);
    assert_int_equal(rd16(b, 10), 1);
}

/* ---- pack_stat64: the OABI (GP2X, glibc 2.3.6) layout -------------------------------------- */

static void test_stat64_oabi_is_96_bytes(void **st) {
    (void)st;
    struct stat s = sample();
    uint8_t b[PACK_STAT64_MAX];
    assert_int_equal(pack_stat64(b, &s, 0), 96);
}

/* THE regression guard. _IO_file_doallocate reserves a 104-byte frame and puts the struct at
   sp+8, so bytes 96..103 are the caller's saved {r4,r5}. Writing them nulled the saved FILE*. */
static void test_stat64_oabi_never_touches_bytes_96_to_103(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_size = 0x7FFFFFFF;
    s.st_ino = 0xFFFF;

    uint8_t b[128];
    memset(b, 0xAA, sizeof b);
    assert_int_equal(pack_stat64(b, &s, 0), 96);
    for (size_t i = 96; i < sizeof b; i++)
        assert_int_equal(b[i], 0xAA);
}

static void test_stat64_oabi_layout(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_dev = 0x0801; s.st_mode = S_IFREG | 0755; s.st_nlink = 2;
    s.st_uid = 1000; s.st_gid = 100; s.st_rdev = 0x99; s.st_size = 1048576; s.st_ino = 0xABCD;

    uint8_t b[PACK_STAT64_MAX];
    pack_stat64(b, &s, 0);

    assert_int_equal(rd64(b, 0), 0x0801);                    /* st_dev @0 (8) */
    assert_int_equal(rd32(b, 12), 0xABCD);                   /* __st_ino @12 */
    assert_int_equal(rd32(b, 16), (uint32_t)s.st_mode);      /* st_mode @16 */
    assert_int_equal(rd32(b, 20), 2);                        /* st_nlink @20 */
    assert_int_equal(rd32(b, 24), 1000);                     /* st_uid @24 */
    assert_int_equal(rd32(b, 28), 100);                      /* st_gid @28 */
    assert_int_equal(rd64(b, 32), 0x99);                     /* st_rdev @32 (8) */
    assert_int_equal(rd64(b, 44), 1048576);                  /* st_size @44, 4-byte aligned */
    assert_int_equal(rd32(b, 52), 4096);                     /* st_blksize @52 */
    assert_int_equal(rd64(b, 56), 1048576 / 512);            /* st_blocks @56 */
    assert_int_equal(rd64(b, 88), 0xABCD);                   /* 64-bit st_ino @88 */
}

/* st_size at 44 rather than 48 is the entire reason this layout is 96 bytes. */
static void test_stat64_oabi_size_is_at_44_not_48(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_size = 0x0BADF00D;
    uint8_t b[PACK_STAT64_MAX];
    memset(b, 0, sizeof b);
    pack_stat64(b, &s, 0);
    assert_int_equal(rd64(b, 44), 0x0BADF00D);
    assert_int_not_equal(rd64(b, 48), 0x0BADF00D);
}

/* ---- pack_stat64: the EABI layout ----------------------------------------------------------- */

static void test_stat64_eabi_is_104_bytes(void **st) {
    (void)st;
    struct stat s = sample();
    uint8_t b[PACK_STAT64_MAX];
    assert_int_equal(pack_stat64(b, &s, 1), 104);
}

static void test_stat64_eabi_layout(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_dev = 0x0801; s.st_mode = S_IFREG | 0755; s.st_nlink = 3;
    s.st_uid = 7; s.st_gid = 9; s.st_rdev = 0x55; s.st_size = 2048; s.st_ino = 0x4321;

    uint8_t b[PACK_STAT64_MAX];
    pack_stat64(b, &s, 1);

    assert_int_equal(rd64(b, 0), 0x0801);                    /* st_dev @0 */
    assert_int_equal(rd32(b, 12), 0x4321);                   /* __st_ino @12 */
    assert_int_equal(rd32(b, 16), (uint32_t)s.st_mode);      /* st_mode @16 */
    assert_int_equal(rd32(b, 20), 3);                        /* st_nlink @20 */
    assert_int_equal(rd32(b, 24), 7);                        /* st_uid @24 */
    assert_int_equal(rd32(b, 28), 9);                        /* st_gid @28 */
    assert_int_equal(rd64(b, 32), 0x55);                     /* st_rdev @32 */
    assert_int_equal(rd64(b, 48), 2048);                     /* st_size @48, 8-byte aligned */
    assert_int_equal(rd32(b, 56), 4096);                     /* st_blksize @56 */
    assert_int_equal(rd64(b, 64), 2048 / 512);               /* st_blocks @64 */
    assert_int_equal(rd64(b, 96), 0x4321);                   /* 64-bit st_ino @96 */
}

/* The two layouts must genuinely differ. Serving the OABI 96-byte shape to an EABI ld.so gives it
   a garbage st_size, so it refuses to mmap libc.so.6 and fails relocation with
   "version GLIBC_2.4 not defined". */
static void test_the_two_layouts_are_not_the_same(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_size = 0x11223344;

    uint8_t oabi[PACK_STAT64_MAX], eabi[PACK_STAT64_MAX];
    memset(oabi, 0, sizeof oabi);
    memset(eabi, 0, sizeof eabi);
    size_t n_oabi = pack_stat64(oabi, &s, 0);
    size_t n_eabi = pack_stat64(eabi, &s, 1);

    assert_int_not_equal(n_oabi, n_eabi);
    assert_int_equal(rd64(oabi, 44), 0x11223344);
    assert_int_equal(rd64(eabi, 48), 0x11223344);
    assert_memory_not_equal(oabi, eabi, 96);
}

/* ---- shared behaviour across both layouts ---------------------------------------------------- */

static void test_stat64_defaults_nlink_and_ino(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_nlink = 0;
    s.st_ino = 0;

    uint8_t b[PACK_STAT64_MAX];
    pack_stat64(b, &s, 0);
    assert_int_equal(rd32(b, 20), 1);
    assert_int_equal(rd32(b, 12), 1);

    pack_stat64(b, &s, 1);
    assert_int_equal(rd32(b, 20), 1);
    assert_int_equal(rd32(b, 12), 1);
}

static void test_stat64_block_count_rounds_up(void **st) {
    (void)st;
    struct stat s = sample();
    uint8_t b[PACK_STAT64_MAX];

    s.st_size = 0;   pack_stat64(b, &s, 0); assert_int_equal(rd64(b, 56), 0);
    s.st_size = 1;   pack_stat64(b, &s, 0); assert_int_equal(rd64(b, 56), 1);
    s.st_size = 512; pack_stat64(b, &s, 0); assert_int_equal(rd64(b, 56), 1);
    s.st_size = 513; pack_stat64(b, &s, 0); assert_int_equal(rd64(b, 56), 2);
}

static void test_stat64_reports_a_directory_mode(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_mode = S_IFDIR | 0755;
    uint8_t b[PACK_STAT64_MAX];
    pack_stat64(b, &s, 0);
    assert_true(rd32(b, 16) & S_IFDIR);
}

#ifndef _WIN32
/* A file larger than 4GB has to survive as 64 bits; MinGW's off_t is only 32, so this is a Linux
   check of the field width. */
static void test_stat64_handles_a_large_file(void **st) {
    (void)st;
    struct stat s = sample();
    s.st_size = 0x1FFFFFFFFLL;                     /* 8GB - 1 */
    uint8_t b[PACK_STAT64_MAX];
    pack_stat64(b, &s, 0);
    assert_true(rd64(b, 44) == 0x1FFFFFFFFULL);
    pack_stat64(b, &s, 1);
    assert_true(rd64(b, 48) == 0x1FFFFFFFFULL);
}
#endif

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_open_flags_preserve_the_access_mode),
#ifdef _WIN32
        cmocka_unit_test(test_open_flags_always_binary_on_windows),
        cmocka_unit_test(test_open_flags_translate_the_bit_values),
        cmocka_unit_test(test_open_flags_do_not_invent_creation),
#else
        cmocka_unit_test(test_open_flags_are_identity_on_linux),
#endif
        cmocka_unit_test(test_common_errnos_pass_through),
        cmocka_unit_test(test_high_errnos_are_the_linux_numbers),
        cmocka_unit_test(test_ino_is_never_zero),
        cmocka_unit_test(test_ino_passes_small_values_through),
#ifndef _WIN32
        cmocka_unit_test(test_ino_truncates_a_huge_inode),
        cmocka_unit_test(test_ino_truncating_to_zero_still_yields_one),
#endif
        cmocka_unit_test(test_oabi_stat_size_and_layout),
        cmocka_unit_test(test_oabi_stat_does_not_write_past_88),
        cmocka_unit_test(test_oabi_stat_defaults_nlink),
        cmocka_unit_test(test_stat64_oabi_is_96_bytes),
        cmocka_unit_test(test_stat64_oabi_never_touches_bytes_96_to_103),
        cmocka_unit_test(test_stat64_oabi_layout),
        cmocka_unit_test(test_stat64_oabi_size_is_at_44_not_48),
        cmocka_unit_test(test_stat64_eabi_is_104_bytes),
        cmocka_unit_test(test_stat64_eabi_layout),
        cmocka_unit_test(test_the_two_layouts_are_not_the_same),
        cmocka_unit_test(test_stat64_defaults_nlink_and_ino),
        cmocka_unit_test(test_stat64_block_count_rounds_up),
        cmocka_unit_test(test_stat64_reports_a_directory_mode),
#ifndef _WIN32
        cmocka_unit_test(test_stat64_handles_a_large_file),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
