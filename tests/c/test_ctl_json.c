/* Unit tests for host/engine/ctl_json.c -- the control channel's JSON writer and flat-object
 * parser.
 *
 * This is protocol code with hard, documented limits (JP_MAXKEYS, 24-byte keys, 192-byte values)
 * that truncate SILENTLY. Silent truncation is fine for the protocol as designed, but only as
 * long as it stays deliberate, so the limits are pinned here rather than left to be rediscovered.
 *
 * ctl_json.c includes engine.h, which includes <unicorn/unicorn.h>, so this test needs the
 * unicorn headers on the include path. It links no unicorn library: the file makes zero uc_*
 * calls. We #include the .c to reach take_string and skip_ws indirectly through the public API.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cmocka.h>

#include "ctl_json.c"

/* ---- writer ------------------------------------------------------------------------------ */

static void test_jw_init_is_empty(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    assert_null(w.buf);
    assert_int_equal(w.len, 0);
    assert_int_equal(w.err, 0);
    jw_free(&w);
}

static void test_jw_raw_appends(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_raw(&w, "{");
    jw_raw(&w, "}");
    assert_string_equal(w.buf, "{}");
    assert_int_equal(w.len, 2);
    jw_free(&w);
}

static void test_jw_str_quotes_and_escapes(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_str(&w, "plain");
    assert_string_equal(w.buf, "\"plain\"");
    jw_free(&w);

    jw_init(&w);
    jw_str(&w, "a\"b\\c\nd\te\rf");
    assert_string_equal(w.buf, "\"a\\\"b\\\\c\\nd\\te\\rf\"");
    jw_free(&w);
}

/* Control characters that have no short escape become \u00xx, which is what keeps a raw guest
   string from breaking the framing. */
static void test_jw_str_escapes_control_characters(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_str(&w, "a\x01\x1f");
    assert_string_equal(w.buf, "\"a\\u0001\\u001f\"");
    jw_free(&w);
}

/* A NULL string is the JSON literal null, NOT an empty quoted string. */
static void test_jw_str_null_is_the_json_literal(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_str(&w, NULL);
    assert_string_equal(w.buf, "null");
    jw_free(&w);

    jw_init(&w);
    jw_raw(&w, "{");
    jw_kv_str(&w, "k", NULL);
    jw_raw(&w, "}");
    assert_string_equal(w.buf, "{\"k\":null}");
    jw_free(&w);
}

/* jw_comma is what makes the writer usable without the caller tracking whether a separator is
   due: it suppresses the comma right after an opening brace or bracket. */
static void test_jw_comma_suppressed_after_an_opener(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_raw(&w, "{");
    jw_comma(&w);
    assert_string_equal(w.buf, "{");
    jw_raw(&w, "\"a\":1");
    jw_comma(&w);
    assert_string_equal(w.buf, "{\"a\":1,");
    jw_free(&w);

    jw_init(&w);
    jw_raw(&w, "[");
    jw_comma(&w);
    assert_string_equal(w.buf, "[");
    jw_free(&w);

    /* an empty buffer must not gain a leading comma either */
    jw_init(&w);
    jw_comma(&w);
    assert_int_equal(w.len, 0);
    jw_free(&w);
}

static void test_jw_scalar_kinds(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_raw(&w, "{");
    jw_kv_str(&w, "s", "v");
    jw_kv_i64(&w, "i", -42);
    jw_kv_u32(&w, "u", 0xffffffffu);
    jw_kv_bool(&w, "t", 1);
    jw_kv_bool(&w, "f", 0);
    jw_raw(&w, "}");
    assert_string_equal(w.buf,
        "{\"s\":\"v\",\"i\":-42,\"u\":4294967295,\"t\":true,\"f\":false}");
    jw_free(&w);
}

/* u32 must not sign-extend through the i64 path. */
static void test_jw_kv_u32_is_unsigned(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_raw(&w, "{");
    jw_kv_u32(&w, "a", 0x80000000u);
    jw_raw(&w, "}");
    assert_string_equal(w.buf, "{\"a\":2147483648}");
    jw_free(&w);
}

/* JSON has no NaN or Infinity, so those become null rather than an unparsable token that would
   break the client. */
static void test_jw_kv_dbl_nan_and_inf_become_null(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_raw(&w, "{");
    jw_kv_dbl(&w, "a", 1.5);
    jw_kv_dbl(&w, "n", NAN);
    jw_kv_dbl(&w, "p", INFINITY);
    jw_kv_dbl(&w, "m", -INFINITY);
    jw_raw(&w, "}");
    assert_string_equal(w.buf, "{\"a\":1.5,\"n\":null,\"p\":null,\"m\":null}");
    jw_free(&w);
}

/* The buffer starts unallocated and doubles; a payload well past the initial 512 bytes must
   still come out intact. */
static void test_jw_grows_past_the_initial_capacity(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    char big[2000];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    jw_raw(&w, "{");
    jw_kv_str(&w, "k", big);
    jw_raw(&w, "}");
    assert_int_equal(w.err, 0);
    assert_int_equal(w.len, 1 + 4 + 1 + (sizeof big - 1) + 1 + 1);
    assert_non_null(strstr(w.buf, big));
    jw_free(&w);
}

static void test_jw_free_resets(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_raw(&w, "abc");
    jw_free(&w);
    assert_null(w.buf);
    assert_int_equal(w.len, 0);
    assert_int_equal(w.cap, 0);
}

/* ---- parser ------------------------------------------------------------------------------- */

static int parse(struct jp *p, const char *s) { return jp_parse(p, s, strlen(s)); }

static void test_jp_parses_a_flat_object(void **st) {
    (void)st;
    struct jp p;
    assert_int_equal(parse(&p, "{\"cmd\":\"mem.read\",\"addr\":100,\"ok\":true}"), 0);
    assert_int_equal(p.n, 3);
    assert_string_equal(jp_get(&p, "cmd"), "mem.read");
    assert_string_equal(jp_get(&p, "addr"), "100");
    assert_string_equal(jp_get(&p, "ok"), "true");
    assert_null(jp_get(&p, "missing"));
}

static void test_jp_empty_object(void **st) {
    (void)st;
    struct jp p;
    assert_int_equal(parse(&p, "{}"), 0);
    assert_int_equal(p.n, 0);
    assert_null(jp_get(&p, "anything"));
}

static void test_jp_tolerates_whitespace(void **st) {
    (void)st;
    struct jp p;
    assert_int_equal(parse(&p, "  {\n  \"a\" : \"b\" ,\t\"c\" : 7 \n}  "), 0);
    assert_int_equal(p.n, 2);
    assert_string_equal(jp_get(&p, "a"), "b");
    assert_string_equal(jp_get(&p, "c"), "7");
}

static void test_jp_unescapes_the_escapes_the_writer_emits(void **st) {
    (void)st;
    struct jp p;
    assert_int_equal(parse(&p, "{\"k\":\"a\\nb\\tc\\rd\\\"e\\\\f\"}"), 0);
    assert_string_equal(jp_get(&p, "k"), "a\nb\tc\rd\"e\\f");
}

/* \u escapes are consumed and dropped: the protocol never needs the codepoint, but the four hex
   digits must not leak into the value. */
static void test_jp_drops_unicode_escapes(void **st) {
    (void)st;
    struct jp p;
    assert_int_equal(parse(&p, "{\"k\":\"a\\u0041b\"}"), 0);
    assert_string_equal(jp_get(&p, "k"), "ab");
}

static void test_jp_rejects_malformed_input(void **st) {
    (void)st;
    struct jp p;
    assert_int_equal(parse(&p, ""), -1);                      /* empty */
    assert_int_equal(parse(&p, "[]"), -1);                    /* not an object */
    assert_int_equal(parse(&p, "{\"a\"}"), -1);               /* no colon */
    assert_int_equal(parse(&p, "{\"a\":1"), -1);              /* unterminated object */
    assert_int_equal(parse(&p, "{\"a\":\"unterminated}"), -1);/* unterminated string */
    assert_int_equal(parse(&p, "{a:1}"), -1);                 /* unquoted key */
    assert_int_equal(parse(&p, "{\"a\":1 \"b\":2}"), -1);     /* missing comma */
}

/* The 25th key is refused rather than silently dropped, so a client cannot quietly lose a
   field it thought it sent. */
static void test_jp_key_limit(void **st) {
    (void)st;
    char buf[4096];
    struct jp p;
    size_t o = 0;
    o += (size_t)snprintf(buf + o, sizeof buf - o, "{");
    for (int i = 0; i < JP_MAXKEYS; i++)
        o += (size_t)snprintf(buf + o, sizeof buf - o, "%s\"k%d\":%d", i ? "," : "", i, i);
    snprintf(buf + o, sizeof buf - o, "}");
    assert_int_equal(parse(&p, buf), 0);
    assert_int_equal(p.n, JP_MAXKEYS);

    o = 0;
    o += (size_t)snprintf(buf + o, sizeof buf - o, "{");
    for (int i = 0; i < JP_MAXKEYS + 1; i++)
        o += (size_t)snprintf(buf + o, sizeof buf - o, "%s\"k%d\":%d", i ? "," : "", i, i);
    snprintf(buf + o, sizeof buf - o, "}");
    assert_int_equal(parse(&p, buf), -1);
}

/* Keys and values are fixed-size slots and overlong ones are truncated without an error. A key
   truncated this way stops matching its own full name, which is the practical consequence. */
static void test_jp_truncates_overlong_keys_and_values(void **st) {
    (void)st;
    struct jp p;
    char buf[1024], key[64], val[512];

    memset(key, 'k', sizeof key - 1); key[sizeof key - 1] = 0;
    snprintf(buf, sizeof buf, "{\"%s\":1}", key);
    assert_int_equal(parse(&p, buf), 0);
    assert_int_equal(strlen(p.key[0]), sizeof p.key[0] - 1);
    assert_null(jp_get(&p, key));                    /* the full name no longer matches */
    assert_non_null(jp_get(&p, p.key[0]));

    memset(val, 'v', sizeof val - 1); val[sizeof val - 1] = 0;
    snprintf(buf, sizeof buf, "{\"a\":\"%s\"}", val);
    assert_int_equal(parse(&p, buf), 0);
    assert_int_equal(strlen(jp_get(&p, "a")), sizeof p.val[0] - 1);
}

/* ---- jp_int -------------------------------------------------------------------------------- */

static void test_jp_int_decimal_and_hex(void **st) {
    (void)st;
    struct jp p;
    assert_int_equal(parse(&p, "{\"d\":1234,\"h\":\"0xC0000000\",\"n\":-7}"), 0);
    assert_int_equal(jp_int(&p, "d", -1), 1234);
    assert_true(jp_int(&p, "h", 0) == 0xC0000000LL);   /* addresses arrive as 0x-hex */
    assert_int_equal(jp_int(&p, "n", 0), -7);
}

static void test_jp_int_falls_back_to_the_default(void **st) {
    (void)st;
    struct jp p;
    assert_int_equal(parse(&p, "{\"t\":true,\"e\":\"\",\"s\":\"abc\"}"), 0);
    assert_int_equal(jp_int(&p, "absent", 99), 99);    /* key not present */
    assert_int_equal(jp_int(&p, "e", 99), 99);         /* empty value */
    assert_int_equal(jp_int(&p, "s", 99), 99);         /* not a number at all */
    assert_int_equal(jp_int(&p, "t", 99), 99);         /* a bool is not a number */
}

/* ---- round trip ---------------------------------------------------------------------------- */

/* What the writer emits, the parser must read back. This is the property that actually matters
   for the control channel, since both ends of it live in this file. */
static void test_writer_output_parses_back(void **st) {
    (void)st;
    struct jw w; jw_init(&w);
    jw_raw(&w, "{");
    jw_kv_str(&w, "cmd", "mem.write");
    jw_kv_str(&w, "note", "a \"quoted\"\nline\twith\\escapes");
    jw_kv_i64(&w, "addr", 3221225472LL);
    jw_kv_bool(&w, "force", 1);
    jw_raw(&w, "}");

    struct jp p;
    assert_int_equal(jp_parse(&p, w.buf, w.len), 0);
    assert_int_equal(p.n, 4);
    assert_string_equal(jp_get(&p, "cmd"), "mem.write");
    assert_string_equal(jp_get(&p, "note"), "a \"quoted\"\nline\twith\\escapes");
    assert_int_equal(jp_int(&p, "addr", 0), 3221225472LL);
    assert_string_equal(jp_get(&p, "force"), "true");
    jw_free(&w);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_jw_init_is_empty),
        cmocka_unit_test(test_jw_raw_appends),
        cmocka_unit_test(test_jw_str_quotes_and_escapes),
        cmocka_unit_test(test_jw_str_escapes_control_characters),
        cmocka_unit_test(test_jw_str_null_is_the_json_literal),
        cmocka_unit_test(test_jw_comma_suppressed_after_an_opener),
        cmocka_unit_test(test_jw_scalar_kinds),
        cmocka_unit_test(test_jw_kv_u32_is_unsigned),
        cmocka_unit_test(test_jw_kv_dbl_nan_and_inf_become_null),
        cmocka_unit_test(test_jw_grows_past_the_initial_capacity),
        cmocka_unit_test(test_jw_free_resets),
        cmocka_unit_test(test_jp_parses_a_flat_object),
        cmocka_unit_test(test_jp_empty_object),
        cmocka_unit_test(test_jp_tolerates_whitespace),
        cmocka_unit_test(test_jp_unescapes_the_escapes_the_writer_emits),
        cmocka_unit_test(test_jp_drops_unicode_escapes),
        cmocka_unit_test(test_jp_rejects_malformed_input),
        cmocka_unit_test(test_jp_key_limit),
        cmocka_unit_test(test_jp_truncates_overlong_keys_and_values),
        cmocka_unit_test(test_jp_int_decimal_and_hex),
        cmocka_unit_test(test_jp_int_falls_back_to_the_default),
        cmocka_unit_test(test_writer_output_parses_back),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
