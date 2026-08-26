/* Unit tests for host/engine/report.c -- the structured run-report sink.
 *
 * ME_TEST_LIBS: -lpthread
 *
 * This table is what the headless harness reads back to decide WHY a title failed
 * (incompatible vs crashed vs just-a-quirk), so a mistake here silently mis-grades a corpus
 * sweep rather than crashing anything. The dedup key, the fixed limits, and above all the
 * stderr pattern scanner's false-positive guard are pinned here.
 *
 * report.c makes zero uc_* calls but includes engine.h for DIAG, so it needs the unicorn headers
 * and exactly one stubbed global: g_log. We #include the .c so the tests can drive g_mr_active
 * directly; otherwise "inert before init" would be an order-dependent, one-shot test, since
 * me_report_init() can never be undone.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "report.c"

/* The only engine global report.c refers to (through the DIAG macro). */
FILE *g_log = NULL;

/* ---- helpers ------------------------------------------------------------------------------ */

static int setup_active(void **st) {
    (void)st;
    g_mr_active = 1;
    me_report_reset();
    return 0;
}

static int setup_inactive(void **st) {
    (void)st;
    g_mr_active = 0;
    me_report_reset();
    return 0;
}

static long log_pos(void) { fflush(g_log); return ftell(g_log); }

/* Count the newlines DIAG gained between two positions, which is how many events were mirrored
   to the human log. */
static int log_lines_since(long from) {
    fflush(g_log);
    long to = ftell(g_log);
    if (to <= from) return 0;
    char *buf = malloc((size_t)(to - from) + 1);
    assert_non_null(buf);
    fseek(g_log, from, SEEK_SET);
    size_t n = fread(buf, 1, (size_t)(to - from), g_log);
    fseek(g_log, 0, SEEK_END);
    int lines = 0;
    for (size_t i = 0; i < n; i++) if (buf[i] == '\n') lines++;
    free(buf);
    return lines;
}

static struct mr_entry *find_event(int kind, long code) {
    for (int i = 0; i < g_nev; i++)
        if (g_ev[i].kind == kind && g_ev[i].code == code) return &g_ev[i];
    return NULL;
}

static char *json(void) {
    char *b = NULL; size_t n = 0;
    me_report_json_buf(&b, &n);
    assert_non_null(b);
    return b;
}

/* ---- kind strings -------------------------------------------------------------------------- */

/* The harness matches on these exact strings, so the mapping is a contract, not a detail. */
static void test_kind_strings(void **st) {
    (void)st;
    assert_string_equal(me_report_kind_str(MR_HOST_FAULT), "host_fault");
    assert_string_equal(me_report_kind_str(MR_GUEST_FATAL), "guest_fatal");
    assert_string_equal(me_report_kind_str(MR_MISSING_ROOTFS_LIB), "missing_rootfs_lib");
    assert_string_equal(me_report_kind_str(MR_MISSING_SYMBOL), "missing_symbol");
    assert_string_equal(me_report_kind_str(MR_UNIMPL_SYSCALL), "unimpl_syscall");
    assert_string_equal(me_report_kind_str(MR_UNKNOWN_DEV), "unknown_dev");
    assert_string_equal(me_report_kind_str(MR_UNKNOWN_IOCTL), "unknown_ioctl");
    assert_string_equal(me_report_kind_str(MR_UNKNOWN_MMIO), "unknown_mmio");
    assert_string_equal(me_report_kind_str(MR_UNSUPPORTED_BLIT), "unsupported_blit");
    assert_string_equal(me_report_kind_str(MR_UNSUPPORTED_GLES), "unsupported_gles");
    assert_string_equal(me_report_kind_str(MR_UNSUPPORTED_AUDIO), "unsupported_audio");
    assert_string_equal(me_report_kind_str(MR_UNSUPPORTED_SDL), "unsupported_sdl");
}

static void test_kind_string_out_of_range(void **st) {
    (void)st;
    assert_string_equal(me_report_kind_str(-1), "unknown");
    assert_string_equal(me_report_kind_str(MR_KIND_COUNT), "unknown");
    assert_string_equal(me_report_kind_str(9999), "unknown");
}

/* ---- capture gating ------------------------------------------------------------------------- */

/* Off by default is the point: no capture, no log noise, no cost on the hot paths. */
static void test_inert_until_activated(void **st) {
    (void)st;
    assert_int_equal(me_report_active(), 0);
    long p = log_pos();
    me_report(MR_UNIMPL_SYSCALL, 4242, "nope", 0x1000);
    assert_int_equal(g_nev, 0);
    assert_int_equal(log_lines_since(p), 0);
}

static void test_active_after_init(void **st) {
    (void)st;
    me_report_init(NULL);
    assert_int_equal(me_report_active(), 1);
}

/* ---- recording and dedup --------------------------------------------------------------------- */

static void test_records_one_event(void **st) {
    (void)st;
    me_report(MR_UNIMPL_SYSCALL, 4242, "sys_nope", 0xdeadbeef);
    assert_int_equal(g_nev, 1);
    struct mr_entry *e = find_event(MR_UNIMPL_SYSCALL, 4242);
    assert_non_null(e);
    assert_string_equal(e->name, "sys_nope");
    assert_int_equal(e->count, 1);
    assert_true(e->pc == 0xdeadbeef);
}

/* Repeats bump a counter instead of growing the table -- a spinning title can hit the same
   unimplemented register millions of times. */
static void test_dedup_by_kind_code_name(void **st) {
    (void)st;
    for (int i = 0; i < 5; i++) me_report(MR_UNKNOWN_MMIO, 0x2958, "mlc", 0x100);
    assert_int_equal(g_nev, 1);
    assert_int_equal(find_event(MR_UNKNOWN_MMIO, 0x2958)->count, 5);

    me_report(MR_UNKNOWN_MMIO, 0x295a, "mlc", 0x100);      /* different code */
    me_report(MR_UNKNOWN_IOCTL, 0x2958, "mlc", 0x100);     /* different kind */
    assert_int_equal(g_nev, 3);
}

static void test_dedup_distinguishes_names(void **st) {
    (void)st;
    me_report(MR_UNKNOWN_DEV, 0, "/dev/i2c-0", 0);
    me_report(MR_UNKNOWN_DEV, 0, "/dev/tty", 0);
    assert_int_equal(g_nev, 2);
}

/* A NULL name and an empty name are the same event, which matters because callers pass either. */
static void test_null_and_empty_names_are_the_same_event(void **st) {
    (void)st;
    me_report(MR_HOST_FAULT, 7, NULL, 0);
    me_report(MR_HOST_FAULT, 7, "", 0);
    assert_int_equal(g_nev, 1);
    assert_int_equal(find_event(MR_HOST_FAULT, 7)->count, 2);
}

/* The first sighting wins for pc, so the report points at where a problem started. */
static void test_pc_is_from_the_first_sighting(void **st) {
    (void)st;
    me_report(MR_UNIMPL_SYSCALL, 1, "a", 0x1111);
    me_report(MR_UNIMPL_SYSCALL, 1, "a", 0x2222);
    assert_true(find_event(MR_UNIMPL_SYSCALL, 1)->pc == 0x1111);
}

static void test_name_is_truncated_not_overflowed(void **st) {
    (void)st;
    char big[300];
    memset(big, 'n', sizeof big - 1);
    big[sizeof big - 1] = 0;
    me_report(MR_MISSING_SYMBOL, 0, big, 0);
    assert_int_equal(g_nev, 1);
    assert_int_equal(strlen(g_ev[0].name), sizeof g_ev[0].name - 1);
}

static void test_invalid_kinds_are_ignored(void **st) {
    (void)st;
    me_report(-1, 0, "x", 0);
    me_report(MR_KIND_COUNT, 0, "x", 0);
    me_report(12345, 0, "x", 0);
    assert_int_equal(g_nev, 0);
}

/* The table is bounded; past the cap new distinct events are dropped rather than overrunning it,
   and the ones already recorded keep counting. */
static void test_table_is_capped(void **st) {
    (void)st;
    for (int i = 0; i < MR_MAX + 20; i++) me_report(MR_UNKNOWN_MMIO, i, NULL, 0);
    assert_int_equal(g_nev, MR_MAX);
    me_report(MR_UNKNOWN_MMIO, 0, NULL, 0);         /* an existing one still counts up */
    assert_int_equal(find_event(MR_UNKNOWN_MMIO, 0)->count, 2);
}

static void test_reset_clears(void **st) {
    (void)st;
    me_report(MR_UNIMPL_SYSCALL, 1, "a", 0);
    assert_int_equal(g_nev, 1);
    me_report_reset();
    assert_int_equal(g_nev, 0);
}

/* The human log gets one line the first time an event is seen and nothing on repeats. */
static void test_diag_mirrors_once_per_event(void **st) {
    (void)st;
    long p = log_pos();
    for (int i = 0; i < 10; i++) me_report(MR_UNIMPL_SYSCALL, 99, "spam", 0);
    assert_int_equal(log_lines_since(p), 1);

    p = log_pos();
    me_report(MR_UNIMPL_SYSCALL, 100, "other", 0);
    assert_int_equal(log_lines_since(p), 1);
}

/* ---- JSON ------------------------------------------------------------------------------------ */

static void test_json_shape(void **st) {
    (void)st;
    me_report(MR_UNIMPL_SYSCALL, 4242, "sys_nope", 0x1234);
    char *j = json();
    assert_non_null(strstr(j, "\"events\":["));
    assert_non_null(strstr(j, "\"kind\":\"unimpl_syscall\""));
    assert_non_null(strstr(j, "\"code\":4242"));
    assert_non_null(strstr(j, "\"name\":\"sys_nope\""));
    assert_non_null(strstr(j, "\"count\":1"));
    assert_non_null(strstr(j, "\"pc\":\"0x00001234\""));
    assert_non_null(strstr(j, "\"counts\":{\"unimpl_syscall\":1}"));
    free(j);
}

static void test_json_empty_table(void **st) {
    (void)st;
    char *j = json();
    assert_string_equal(j, "{\"events\":[],\"counts\":{}}");
    free(j);
}

/* counts aggregates hit counts per kind, not distinct events. */
static void test_json_counts_sum_hits_per_kind(void **st) {
    (void)st;
    me_report(MR_UNKNOWN_MMIO, 1, NULL, 0);
    me_report(MR_UNKNOWN_MMIO, 1, NULL, 0);
    me_report(MR_UNKNOWN_MMIO, 2, NULL, 0);
    me_report(MR_HOST_FAULT, 0, NULL, 0);
    char *j = json();
    assert_non_null(strstr(j, "\"unknown_mmio\":3"));
    assert_non_null(strstr(j, "\"host_fault\":1"));
    free(j);
}

/* A guest string reaches this table verbatim, so it has to be escaped or it breaks the harness's
   JSON parse. */
static void test_json_escapes_names(void **st) {
    (void)st;
    me_report(MR_GUEST_FATAL, 0, "a\"b\\c\nd\te\x01", 0);
    char *j = json();
    assert_non_null(strstr(j, "\\\"b"));
    assert_non_null(strstr(j, "\\\\c"));
    assert_non_null(strstr(j, "\\nd"));
    assert_non_null(strstr(j, "\\te"));
    assert_non_null(strstr(j, "\\u0001"));
    free(j);
}

static void test_flush_json_writes_a_file(void **st) {
    (void)st;
    me_report(MR_UNIMPL_SYSCALL, 4242, "sys_nope", 0);

    char path[512];
    snprintf(path, sizeof path, "%s/me_report_test_%d.json",
             getenv("TMPDIR") ? getenv("TMPDIR") : ".", (int)getpid());
    me_report_flush_json(path);

    FILE *f = fopen(path, "r");
    assert_non_null(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    remove(path);
    assert_non_null(strstr(buf, "\"code\":4242"));
}

/* ---- the guest-shim stderr sentinel ------------------------------------------------------------ */

static void test_ingest_guest_sentinel(void **st) {
    (void)st;
    const char *line = "\x01MR 11 42 JPEG\n";
    assert_int_equal(me_report_ingest_guest(line, strlen(line)), 1);
    assert_int_equal(g_nev, 1);
    struct mr_entry *e = find_event(MR_UNSUPPORTED_SDL, 42);
    assert_non_null(e);
    assert_string_equal(e->name, "JPEG");
}

static void test_ingest_guest_ignores_ordinary_output(void **st) {
    (void)st;
    const char *line = "just some game output\n";
    assert_int_equal(me_report_ingest_guest(line, strlen(line)), 0);
    assert_int_equal(me_report_ingest_guest("\x01M", 2), 0);      /* too short to be a sentinel */
    assert_int_equal(g_nev, 0);
}

/* Even with capture off the sentinel is swallowed, so it never leaks to the user's console. */
static void test_ingest_guest_swallows_when_inactive(void **st) {
    (void)st;
    const char *line = "\x01MR 11 42 JPEG\n";
    assert_int_equal(me_report_ingest_guest(line, strlen(line)), 1);
    assert_int_equal(g_nev, 0);
}

static void test_ingest_guest_without_a_name(void **st) {
    (void)st;
    const char *line = "\x01MR 4 99 \n";
    assert_int_equal(me_report_ingest_guest(line, strlen(line)), 1);
    struct mr_entry *e = find_event(MR_UNIMPL_SYSCALL, 99);
    assert_non_null(e);
    assert_string_equal(e->name, "");
}

/* ---- the guest stderr scanner ------------------------------------------------------------------- */

static void scan(const char *s) { me_report_scan_write(2, s, strlen(s)); }

static void test_scan_detects_missing_symbol(void **st) {
    (void)st;
    scan("./game.gpe: symbol lookup error: ./game.gpe: undefined symbol: SDL_GetKeyState\n");
    assert_int_equal(g_nev, 1);
    assert_int_equal(g_ev[0].kind, MR_MISSING_SYMBOL);
}

static void test_scan_detects_missing_library(void **st) {
    (void)st;
    scan("error while loading shared libraries: libpng12.so.0: cannot open shared object file\n");
    assert_int_equal(g_nev, 1);
    assert_int_equal(g_ev[0].kind, MR_MISSING_ROOTFS_LIB);
}

static void test_scan_detects_fatal_aborts(void **st) {
    (void)st;
    scan("game: main.c:42: foo: assertion 'x != NULL' failed\n");
    assert_int_equal(g_ev[0].kind, MR_GUEST_FATAL);

    me_report_reset();
    scan("*** stack smashing detected ***: terminated\n");
    assert_int_equal(g_ev[0].kind, MR_GUEST_FATAL);

    me_report_reset();
    scan("*** glibc detected *** ./game: free(): invalid pointer\n");
    assert_int_equal(g_ev[0].kind, MR_GUEST_FATAL);
}

/* Games decorate stderr with banners. A bare "*** " must NOT be read as a fatal abort: one false
   guest_fatal demotes a perfectly fine title to `incompatible` in the tracker. */
static void test_scan_does_not_flag_decorative_banners(void **st) {
    (void)st;
    scan("*** INIT SOUND ***\n");
    scan("*** Welcome to the game ***\n");
    scan("loading assets, please wait\n");
    scan("assertion of quality\n");                /* "assertion" without "failed" */
    assert_int_equal(g_nev, 0);
}

/* Only the guest's own stdout/stderr are scanned. */
static void test_scan_ignores_other_fds_and_empty_input(void **st) {
    (void)st;
    const char *fatal = "symbol lookup error: nope\n";
    me_report_scan_write(3, fatal, strlen(fatal));
    me_report_scan_write(0, fatal, strlen(fatal));
    me_report_scan_write(2, NULL, 10);
    me_report_scan_write(2, fatal, 0);
    assert_int_equal(g_nev, 0);

    me_report_scan_write(1, fatal, strlen(fatal));   /* stdout counts */
    assert_int_equal(g_nev, 1);
}

/* The recorded name is a single trimmed line, so a multi-line dump does not smear into it. */
static void test_scan_records_only_the_first_line(void **st) {
    (void)st;
    scan("  symbol lookup error: boom\nsecond line\nthird line\n");
    assert_int_equal(g_nev, 1);
    assert_null(strchr(g_ev[0].name, '\n'));
    assert_string_equal(g_ev[0].name, "symbol lookup error: boom");
}

int main(void) {
    g_log = tmpfile();
    if (!g_log) { fprintf(stderr, "could not open a temporary log\n"); return 1; }

    const struct CMUnitTest tests[] = {
        /* must come before anything calls me_report_init(): activation is one-way */
        cmocka_unit_test_setup(test_inert_until_activated, setup_inactive),
        cmocka_unit_test_setup(test_ingest_guest_swallows_when_inactive, setup_inactive),

        cmocka_unit_test(test_kind_strings),
        cmocka_unit_test(test_kind_string_out_of_range),
        cmocka_unit_test_setup(test_active_after_init, setup_active),

        cmocka_unit_test_setup(test_records_one_event, setup_active),
        cmocka_unit_test_setup(test_dedup_by_kind_code_name, setup_active),
        cmocka_unit_test_setup(test_dedup_distinguishes_names, setup_active),
        cmocka_unit_test_setup(test_null_and_empty_names_are_the_same_event, setup_active),
        cmocka_unit_test_setup(test_pc_is_from_the_first_sighting, setup_active),
        cmocka_unit_test_setup(test_name_is_truncated_not_overflowed, setup_active),
        cmocka_unit_test_setup(test_invalid_kinds_are_ignored, setup_active),
        cmocka_unit_test_setup(test_table_is_capped, setup_active),
        cmocka_unit_test_setup(test_reset_clears, setup_active),
        cmocka_unit_test_setup(test_diag_mirrors_once_per_event, setup_active),

        cmocka_unit_test_setup(test_json_shape, setup_active),
        cmocka_unit_test_setup(test_json_empty_table, setup_active),
        cmocka_unit_test_setup(test_json_counts_sum_hits_per_kind, setup_active),
        cmocka_unit_test_setup(test_json_escapes_names, setup_active),
        cmocka_unit_test_setup(test_flush_json_writes_a_file, setup_active),

        cmocka_unit_test_setup(test_ingest_guest_sentinel, setup_active),
        cmocka_unit_test_setup(test_ingest_guest_ignores_ordinary_output, setup_active),
        cmocka_unit_test_setup(test_ingest_guest_without_a_name, setup_active),

        cmocka_unit_test_setup(test_scan_detects_missing_symbol, setup_active),
        cmocka_unit_test_setup(test_scan_detects_missing_library, setup_active),
        cmocka_unit_test_setup(test_scan_detects_fatal_aborts, setup_active),
        cmocka_unit_test_setup(test_scan_does_not_flag_decorative_banners, setup_active),
        cmocka_unit_test_setup(test_scan_ignores_other_fds_and_empty_input, setup_active),
        cmocka_unit_test_setup(test_scan_records_only_the_first_line, setup_active),
    };
    int rc = cmocka_run_group_tests(tests, NULL, NULL);
    fclose(g_log);
    return rc;
}
