/* Unit tests for host/engine/paths.c -- the portable Settings/Firmware/Cache roots.
 *
 * These three roots decide where a release writes everything: keybindings, staged firmware, the
 * GPEComp decompress cache and the save overlay. Getting them wrong does not crash anything, it
 * quietly writes to the wrong place, which is exactly the class of bug a unit test catches and a
 * play session does not.
 *
 * The module reads one global, g_exe_dir, in preference to asking the OS, so a test can point the
 * whole thing at a temp dir by setting that single variable. We #include the .c both to reach
 * mkdirs() and, more importantly, to clear s_loaded: it is a one-shot latch with no reset hook,
 * so without this only the first paths.conf in a process would ever be read.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "paths.c"

char g_exe_dir[PATH_MAX];      /* the one engine global paths.c refers to */

static char g_tmp[PATH_MAX];

/* ---- helpers ------------------------------------------------------------------------------ */

/* Windows normalises the resolved path to backslashes so the settings UI never shows a mixed
   separator; expectations have to follow. */
static void expect_sep(char *s) {
#ifdef _WIN32
    for (char *p = s; *p; p++) if (*p == '/') *p = '\\';
#else
    (void)s;
#endif
}

static void join(char *out, size_t cap, const char *dir, const char *leaf) {
    snprintf(out, cap, "%s/%s", dir, leaf);
    expect_sep(out);
}

static int setup(void **st) {
    (void)st;
    static int seq = 0;
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = getenv("TEMP");
    if (!base || !*base) base = ".";
    snprintf(g_tmp, sizeof g_tmp, "%s/me_paths_test_%u_%d",
             base, (unsigned)(uintptr_t)&seq, seq);
    seq++;
    mkdirs(g_tmp);

    snprintf(g_exe_dir, sizeof g_exe_dir, "%s", g_tmp);
    s_loaded = 0;                       /* the latch: without this only the first conf is read */
    memset(s_override, 0, sizeof s_override);
    return 0;
}

static int teardown(void **st) {
    (void)st;
    char p[PATH_MAX];
    join(p, sizeof p, g_tmp, "paths.conf"); remove(p);
    join(p, sizeof p, g_tmp, "config");     rmdir(p);
    join(p, sizeof p, g_tmp, "firmware");   rmdir(p);
    join(p, sizeof p, g_tmp, "cache");      rmdir(p);
    join(p, sizeof p, g_tmp, "elsewhere");  rmdir(p);
    rmdir(g_tmp);
    return 0;
}

static void write_conf(const char *body) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/paths.conf", g_tmp);
    FILE *f = fopen(p, "wb");
    assert_non_null(f);
    fwrite(body, 1, strlen(body), f);
    fclose(f);
}

static char *read_conf(void) {
    static char buf[2048];
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/paths.conf", g_tmp);
    FILE *f = fopen(p, "rb");
    if (!f) { buf[0] = 0; return buf; }
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static int is_dir(const char *p) {
    struct stat s;
    return stat(p, &s) == 0 && (s.st_mode & S_IFDIR);
}

/* ---- labels ------------------------------------------------------------------------------- */

static void test_labels(void **st) {
    (void)st;
    assert_string_equal(me_paths_label(ME_PATH_SETTINGS), "Settings");
    assert_string_equal(me_paths_label(ME_PATH_FIRMWARE), "Firmware");
    assert_string_equal(me_paths_label(ME_PATH_CACHE), "Cache");
    assert_string_equal(me_paths_label(ME_PATH_NKINDS), "?");
    assert_string_equal(me_paths_label((me_path_kind)-1), "?");
}

/* ---- defaults ------------------------------------------------------------------------------ */

/* The whole point of the design: every writable root sits beside the executable, so the bundle
   is portable and nothing lands under %APPDATA% or %TEMP%. */
static void test_defaults_sit_beside_the_executable(void **st) {
    (void)st;
    char got[PATH_MAX], want[PATH_MAX];

    me_paths_default(ME_PATH_SETTINGS, got, sizeof got);
    snprintf(want, sizeof want, "%s/config", g_tmp);
    assert_string_equal(got, want);

    me_paths_default(ME_PATH_FIRMWARE, got, sizeof got);
    snprintf(want, sizeof want, "%s/firmware", g_tmp);
    assert_string_equal(got, want);

    me_paths_default(ME_PATH_CACHE, got, sizeof got);
    snprintf(want, sizeof want, "%s/cache", g_tmp);
    assert_string_equal(got, want);
}

/* me_paths_default reports the portable default even when an override is in force, which is what
   the settings window shows as the "reset to" value. */
static void test_default_ignores_an_override(void **st) {
    (void)st;
    char other[PATH_MAX], got[PATH_MAX], want[PATH_MAX];
    join(other, sizeof other, g_tmp, "elsewhere");
    assert_int_equal(me_paths_set(ME_PATH_CACHE, other), 0);

    me_paths_default(ME_PATH_CACHE, got, sizeof got);
    snprintf(want, sizeof want, "%s/cache", g_tmp);
    assert_string_equal(got, want);
}

/* ---- resolution ----------------------------------------------------------------------------- */

static void test_dir_uses_the_default_and_creates_it(void **st) {
    (void)st;
    char got[PATH_MAX], want[PATH_MAX];
    me_paths_dir(ME_PATH_SETTINGS, got, sizeof got);
    join(want, sizeof want, g_tmp, "config");
    assert_string_equal(got, want);
    assert_true(is_dir(got));
}

static void test_dir_rejects_an_out_of_range_kind(void **st) {
    (void)st;
    char got[PATH_MAX];
    me_paths_dir(ME_PATH_NKINDS, got, sizeof got);
    assert_string_equal(got, ".");
    me_paths_dir((me_path_kind)-1, got, sizeof got);
    assert_string_equal(got, ".");
}

#ifdef _WIN32
/* Defaults join the exe dir (backslashes) with "/sub", so without normalisation the UI would
   show a mixed separator. */
static void test_dir_normalises_separators_on_windows(void **st) {
    (void)st;
    char got[PATH_MAX];
    me_paths_dir(ME_PATH_CACHE, got, sizeof got);
    assert_null(strchr(got, '/'));
}
#endif

/* ---- overrides -------------------------------------------------------------------------------- */

static void test_override_from_an_existing_conf(void **st) {
    (void)st;
    char other[PATH_MAX], got[PATH_MAX], line[PATH_MAX + 32];
    join(other, sizeof other, g_tmp, "elsewhere");
    snprintf(line, sizeof line, "cache=%s\n", other);
    write_conf(line);

    me_paths_dir(ME_PATH_CACHE, got, sizeof got);
    assert_string_equal(got, other);
    assert_true(is_dir(got));

    /* the other two are untouched and still portable */
    char want[PATH_MAX];
    me_paths_dir(ME_PATH_SETTINGS, got, sizeof got);
    join(want, sizeof want, g_tmp, "config");
    assert_string_equal(got, want);
}

static void test_set_persists_and_takes_effect(void **st) {
    (void)st;
    char other[PATH_MAX], got[PATH_MAX];
    join(other, sizeof other, g_tmp, "elsewhere");

    assert_int_equal(me_paths_set(ME_PATH_FIRMWARE, other), 0);
    me_paths_dir(ME_PATH_FIRMWARE, got, sizeof got);
    assert_string_equal(got, other);

    assert_non_null(strstr(read_conf(), "firmware="));

    /* and it survives a fresh load, which is what a restart does */
    s_loaded = 0;
    memset(s_override, 0, sizeof s_override);
    me_paths_dir(ME_PATH_FIRMWARE, got, sizeof got);
    assert_string_equal(got, other);
}

/* An empty (or NULL) directory clears the override rather than setting an empty path. */
static void test_set_empty_clears_the_override(void **st) {
    (void)st;
    char other[PATH_MAX], got[PATH_MAX], want[PATH_MAX];
    join(other, sizeof other, g_tmp, "elsewhere");
    me_paths_set(ME_PATH_CACHE, other);

    assert_int_equal(me_paths_set(ME_PATH_CACHE, ""), 0);
    me_paths_dir(ME_PATH_CACHE, got, sizeof got);
    join(want, sizeof want, g_tmp, "cache");
    assert_string_equal(got, want);

    me_paths_set(ME_PATH_CACHE, other);
    assert_int_equal(me_paths_set(ME_PATH_CACHE, NULL), 0);
    me_paths_dir(ME_PATH_CACHE, got, sizeof got);
    assert_string_equal(got, want);
}

static void test_set_rejects_an_out_of_range_kind(void **st) {
    (void)st;
    assert_int_equal(me_paths_set(ME_PATH_NKINDS, "/tmp"), -1);
    assert_int_equal(me_paths_set((me_path_kind)-1, "/tmp"), -1);
}

static void test_reset_restores_every_default(void **st) {
    (void)st;
    char other[PATH_MAX], got[PATH_MAX], want[PATH_MAX];
    join(other, sizeof other, g_tmp, "elsewhere");
    me_paths_set(ME_PATH_SETTINGS, other);
    me_paths_set(ME_PATH_FIRMWARE, other);
    me_paths_set(ME_PATH_CACHE, other);

    me_paths_reset();

    static const char *leaf[] = {"config", "firmware", "cache"};
    for (int k = 0; k < ME_PATH_NKINDS; k++) {
        me_paths_dir((me_path_kind)k, got, sizeof got);
        join(want, sizeof want, g_tmp, leaf[k]);
        assert_string_equal(got, want);
    }
    /* nothing left to persist, so the file carries no keys */
    assert_null(strstr(read_conf(), "="));
}

/* ---- conf parsing ------------------------------------------------------------------------------ */

/* Comments, blank lines, junk lines, unknown keys and CRLF all have to survive without turning
   into a bogus path -- a conf saved by a Windows editor is the common case. */
static void test_conf_parsing_skips_noise_and_strips_line_endings(void **st) {
    (void)st;
    char other[PATH_MAX], got[PATH_MAX], body[PATH_MAX * 2];
    join(other, sizeof other, g_tmp, "elsewhere");
    snprintf(body, sizeof body,
             "# a comment\r\n"
             "\r\n"
             "a line with no equals sign\r\n"
             "unknown_key=/should/be/ignored\r\n"
             "cache=%s   \r\n",           /* trailing spaces and CR are stripped */
             other);
    write_conf(body);

    me_paths_dir(ME_PATH_CACHE, got, sizeof got);
    assert_string_equal(got, other);
}

/* load_once rstrips both halves, so only TRAILING whitespace is forgiven. A key with a space
   before the '=' works; a key with a leading indent silently does not, and the failure is
   invisible because the value simply stays at its default. Both halves pinned here so the
   asymmetry is a documented contract rather than a surprise. */
static void test_conf_whitespace_handling_is_trailing_only(void **st) {
    (void)st;
    char other[PATH_MAX], got[PATH_MAX], want[PATH_MAX], body[PATH_MAX * 2];
    join(other, sizeof other, g_tmp, "elsewhere");
    join(want, sizeof want, g_tmp, "cache");

    snprintf(body, sizeof body, "cache \t=%s\n", other);      /* trailing: tolerated */
    write_conf(body);
    me_paths_dir(ME_PATH_CACHE, got, sizeof got);
    assert_string_equal(got, other);

    s_loaded = 0;
    memset(s_override, 0, sizeof s_override);
    snprintf(body, sizeof body, "   cache=%s\n", other);      /* leading: silently ignored */
    write_conf(body);
    me_paths_dir(ME_PATH_CACHE, got, sizeof got);
    assert_string_equal(got, want);
}

/* A key whose value is empty leaves the portable default in place rather than resolving to "". */
static void test_conf_empty_value_falls_back_to_the_default(void **st) {
    (void)st;
    char got[PATH_MAX], want[PATH_MAX];
    write_conf("cache=\n");
    me_paths_dir(ME_PATH_CACHE, got, sizeof got);
    join(want, sizeof want, g_tmp, "cache");
    assert_string_equal(got, want);
}

/* A missing conf is the normal first-run case and must not be an error. */
static void test_missing_conf_is_fine(void **st) {
    (void)st;
    char got[PATH_MAX], want[PATH_MAX];
    me_paths_dir(ME_PATH_SETTINGS, got, sizeof got);
    join(want, sizeof want, g_tmp, "config");
    assert_string_equal(got, want);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_labels, setup, teardown),
        cmocka_unit_test_setup_teardown(test_defaults_sit_beside_the_executable, setup, teardown),
        cmocka_unit_test_setup_teardown(test_default_ignores_an_override, setup, teardown),
        cmocka_unit_test_setup_teardown(test_dir_uses_the_default_and_creates_it, setup, teardown),
        cmocka_unit_test_setup_teardown(test_dir_rejects_an_out_of_range_kind, setup, teardown),
#ifdef _WIN32
        cmocka_unit_test_setup_teardown(test_dir_normalises_separators_on_windows, setup, teardown),
#endif
        cmocka_unit_test_setup_teardown(test_override_from_an_existing_conf, setup, teardown),
        cmocka_unit_test_setup_teardown(test_set_persists_and_takes_effect, setup, teardown),
        cmocka_unit_test_setup_teardown(test_set_empty_clears_the_override, setup, teardown),
        cmocka_unit_test_setup_teardown(test_set_rejects_an_out_of_range_kind, setup, teardown),
        cmocka_unit_test_setup_teardown(test_reset_restores_every_default, setup, teardown),
        cmocka_unit_test_setup_teardown(test_conf_parsing_skips_noise_and_strips_line_endings, setup, teardown),
        cmocka_unit_test_setup_teardown(test_conf_whitespace_handling_is_trailing_only, setup, teardown),
        cmocka_unit_test_setup_teardown(test_conf_empty_value_falls_back_to_the_default, setup, teardown),
        cmocka_unit_test_setup_teardown(test_missing_conf_is_fine, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
