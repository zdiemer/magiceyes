/* Unit tests for host/engine/armfp.c -- ARM condition codes and the OABI double marshalling.
 *
 * ME_TEST_SRC: host/engine/armfp.c
 * ME_TEST_LIBS: -lm
 *
 * fpa_words_to_double is the reason this file exists. Getting the register word order backwards
 * produces a perfectly finite, plausible-looking wrong number, so it does not crash: it makes a
 * projectile's sqrt come out -inf and the turrets deal no damage (github #2, fixed in 95f99c4),
 * or a rotation constant land somewhere absurd (e93a525). A test that pins the byte order against
 * a known constant is the only cheap way to notice.
 *
 * arm_cond_pass is pinned exhaustively: all 16 codes against all 16 NZCV states, checked against
 * the ARM ARM definitions rather than against the implementation's own shape.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <cmocka.h>

#include "armfp.h"

#define COND(c) ((uint32_t)(c) << 28)

static uint32_t cpsr_of(int n, int z, int c, int v) {
    return ((uint32_t)n << 31) | ((uint32_t)z << 30) | ((uint32_t)c << 29) | ((uint32_t)v << 28);
}

/* ---- arm_cond_pass ------------------------------------------------------------------------ */

/* The ARM ARM definitions, written out independently of how armfp.c happens to implement them. */
static int expected(int cond, int N, int Z, int C, int V) {
    switch (cond) {
    case 0x0: return Z;                       /* EQ */
    case 0x1: return !Z;                      /* NE */
    case 0x2: return C;                       /* CS/HS */
    case 0x3: return !C;                      /* CC/LO */
    case 0x4: return N;                       /* MI */
    case 0x5: return !N;                      /* PL */
    case 0x6: return V;                       /* VS */
    case 0x7: return !V;                      /* VC */
    case 0x8: return C && !Z;                 /* HI */
    case 0x9: return !C || Z;                 /* LS */
    case 0xa: return N == V;                  /* GE */
    case 0xb: return N != V;                  /* LT */
    case 0xc: return !Z && N == V;            /* GT */
    case 0xd: return Z || N != V;             /* LE */
    default:  return 1;                       /* AL / unconditional */
    }
}

static void test_every_condition_against_every_flag_state(void **st) {
    (void)st;
    for (int cond = 0; cond <= 0xf; cond++)
        for (int flags = 0; flags < 16; flags++) {
            int N = (flags >> 3) & 1, Z = (flags >> 2) & 1, C = (flags >> 1) & 1, V = flags & 1;
            int got = arm_cond_pass(COND(cond), cpsr_of(N, Z, C, V)) ? 1 : 0;
            int want = expected(cond, N, Z, C, V) ? 1 : 0;
            if (got != want)
                fail_msg("cond 0x%x with N=%d Z=%d C=%d V=%d: got %d, want %d",
                         cond, N, Z, C, V, got, want);
        }
}

static void test_al_and_unconditional_always_execute(void **st) {
    (void)st;
    for (int flags = 0; flags < 16; flags++) {
        uint32_t cpsr = cpsr_of((flags >> 3) & 1, (flags >> 2) & 1, (flags >> 1) & 1, flags & 1);
        assert_true(arm_cond_pass(COND(0xe), cpsr));   /* AL */
        assert_true(arm_cond_pass(COND(0xf), cpsr));   /* the unconditional space */
    }
}

/* Only bits[31:28] of the instruction select the condition; the rest is the opcode. */
static void test_only_the_top_nibble_selects_the_condition(void **st) {
    (void)st;
    uint32_t cpsr = cpsr_of(0, 1, 0, 0);               /* Z set */
    assert_true(arm_cond_pass(COND(0x0) | 0x0FFFFFFF, cpsr));    /* EQ */
    assert_false(arm_cond_pass(COND(0x1) | 0x0FFFFFFF, cpsr));   /* NE */
}

/* Only bits[31:28] of CPSR matter; the mode and interrupt bits must not leak in. */
static void test_lower_cpsr_bits_are_ignored(void **st) {
    (void)st;
    uint32_t z_set = cpsr_of(0, 1, 0, 0);
    assert_true(arm_cond_pass(COND(0x0), z_set));
    assert_true(arm_cond_pass(COND(0x0), z_set | 0x0FFFFFFF));   /* mode/IRQ bits set too */
}

/* The signed comparisons are where a naive implementation goes wrong. */
static void test_signed_comparisons(void **st) {
    (void)st;
    /* GE is N == V, so it holds both when neither is set and when both are. */
    assert_true(arm_cond_pass(COND(0xa), cpsr_of(0, 0, 0, 0)));
    assert_true(arm_cond_pass(COND(0xa), cpsr_of(1, 0, 0, 1)));
    assert_false(arm_cond_pass(COND(0xa), cpsr_of(1, 0, 0, 0)));

    /* LT is its negation. */
    assert_false(arm_cond_pass(COND(0xb), cpsr_of(0, 0, 0, 0)));
    assert_true(arm_cond_pass(COND(0xb), cpsr_of(1, 0, 0, 0)));

    /* GT additionally requires Z clear; LE is its negation. */
    assert_true(arm_cond_pass(COND(0xc), cpsr_of(0, 0, 0, 0)));
    assert_false(arm_cond_pass(COND(0xc), cpsr_of(0, 1, 0, 0)));
    assert_true(arm_cond_pass(COND(0xd), cpsr_of(0, 1, 0, 0)));
}

/* HI and LS are the unsigned pair, and are each other's negation. */
static void test_unsigned_comparisons_are_complementary(void **st) {
    (void)st;
    for (int flags = 0; flags < 16; flags++) {
        uint32_t cpsr = cpsr_of((flags >> 3) & 1, (flags >> 2) & 1, (flags >> 1) & 1, flags & 1);
        int hi = arm_cond_pass(COND(0x8), cpsr) ? 1 : 0;
        int ls = arm_cond_pass(COND(0x9), cpsr) ? 1 : 0;
        assert_int_not_equal(hi, ls);
    }
}

/* Every code except AL/unconditional has an inverse in the next slot. */
static void test_condition_pairs_are_inverses(void **st) {
    (void)st;
    for (int cond = 0; cond <= 0xd; cond += 2)
        for (int flags = 0; flags < 16; flags++) {
            uint32_t cpsr = cpsr_of((flags >> 3) & 1, (flags >> 2) & 1,
                                    (flags >> 1) & 1, flags & 1);
            int a = arm_cond_pass(COND(cond), cpsr) ? 1 : 0;
            int b = arm_cond_pass(COND(cond + 1), cpsr) ? 1 : 0;
            assert_int_not_equal(a, b);
        }
}

/* ---- fpa_words_to_double ------------------------------------------------------------------- */

/* The live capture recorded in the source: 0x3faacee9_f37c4b99 is pi/60. Getting the word order
   backwards yields a finite, plausible number rather than an obvious error, which is exactly why
   the original bug survived so long. */
static void test_the_captured_constant(void **st) {
    (void)st;
    double d = fpa_words_to_double(0x3faacee9u, 0xf37c4b99u);
    assert_true(fabs(d - (3.14159265358979323846 / 60.0)) < 1e-12);
}

static void test_word_order_is_high_then_low(void **st) {
    (void)st;
    /* 1.0 is 0x3FF0000000000000: all the information is in the high word. */
    assert_true(fpa_words_to_double(0x3FF00000u, 0x00000000u) == 1.0);
    /* Swapped, it must NOT come out as 1.0. */
    assert_false(fpa_words_to_double(0x00000000u, 0x3FF00000u) == 1.0);
}

static void test_simple_values(void **st) {
    (void)st;
    assert_true(fpa_words_to_double(0x00000000u, 0x00000000u) == 0.0);
    assert_true(fpa_words_to_double(0x40000000u, 0x00000000u) == 2.0);
    assert_true(fpa_words_to_double(0xBFF00000u, 0x00000000u) == -1.0);
    assert_true(fpa_words_to_double(0x3FE00000u, 0x00000000u) == 0.5);
}

/* The low word must not be dropped: a value whose mantissa lives entirely down there would
   otherwise silently round to something tidy. */
static void test_the_low_word_is_not_discarded(void **st) {
    (void)st;
    double a = fpa_words_to_double(0x3FF00000u, 0x00000001u);   /* 1.0 + 1ulp */
    assert_true(a > 1.0);
    assert_true(a < 1.0000001);
}

static void test_negative_zero_and_infinity(void **st) {
    (void)st;
    double nz = fpa_words_to_double(0x80000000u, 0x00000000u);
    assert_true(nz == 0.0);
    assert_true(signbit(nz));

    assert_true(isinf(fpa_words_to_double(0x7FF00000u, 0x00000000u)));
    assert_true(isnan(fpa_words_to_double(0x7FF80000u, 0x00000000u)));
}

/* Round-tripping any double through the split must be lossless. */
static void test_round_trip(void **st) {
    (void)st;
    const double vals[] = {0.0, 1.0, -1.0, 0.5, 3.14159265358979, 1e-300, 1e300, -12345.6789};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        uint64_t u;
        memcpy(&u, &vals[i], 8);
        double back = fpa_words_to_double((uint32_t)(u >> 32), (uint32_t)u);
        assert_true(back == vals[i]);
    }
}

/* ---- oabi_libm_compute ---------------------------------------------------------------------- */

static void test_one_argument_functions(void **st) {
    (void)st;
    assert_true(fabs(oabi_libm_compute(0, 0.0, 0.0) - 1.0) < 1e-12);        /* cos */
    assert_true(fabs(oabi_libm_compute(1, 0.0, 0.0) - 0.0) < 1e-12);        /* sin */
    assert_true(fabs(oabi_libm_compute(2, 0.0, 0.0) - 0.0) < 1e-12);        /* tan */
    assert_true(oabi_libm_compute(3, 2.7, 0.0) == 2.0);                     /* floor */
    assert_true(oabi_libm_compute(4, 2.1, 0.0) == 3.0);                     /* ceil */
    assert_true(oabi_libm_compute(5, 16.0, 0.0) == 4.0);                    /* sqrt */
    assert_true(oabi_libm_compute(6, -3.5, 0.0) == 3.5);                    /* fabs */
    assert_true(fabs(oabi_libm_compute(7, 0.0, 0.0) - 1.0) < 1e-12);        /* exp */
    assert_true(fabs(oabi_libm_compute(8, 1.0, 0.0) - 0.0) < 1e-12);        /* log */
    assert_true(fabs(oabi_libm_compute(9, 100.0, 0.0) - 2.0) < 1e-12);      /* log10 */
    assert_true(fabs(oabi_libm_compute(10, 0.0, 0.0) - 0.0) < 1e-12);       /* asin */
    assert_true(fabs(oabi_libm_compute(11, 1.0, 0.0) - 0.0) < 1e-12);       /* acos */
    assert_true(fabs(oabi_libm_compute(12, 0.0, 0.0) - 0.0) < 1e-12);       /* atan */
}

/* The two-argument ones are where an argument-order mistake hides. */
static void test_two_argument_functions_use_both_in_the_right_order(void **st) {
    (void)st;
    assert_true(fabs(oabi_libm_compute(13, 1.0, 0.0) - (M_PI / 2)) < 1e-12);  /* atan2(y=1,x=0) */
    assert_true(oabi_libm_compute(14, 2.0, 10.0) == 1024.0);                  /* pow(2,10) */
    assert_true(oabi_libm_compute(15, 7.0, 3.0) == 1.0);                      /* fmod(7,3) */
    assert_true(fabs(oabi_libm_compute(16, 3.0, 4.0) - 5.0) < 1e-12);         /* hypot(3,4) */

    /* pow is not commutative, so a swap would be caught here. */
    assert_true(oabi_libm_compute(14, 2.0, 10.0) != oabi_libm_compute(14, 10.0, 2.0));
}

/* An index outside the table returns its first argument rather than reading past the end. */
static void test_an_unknown_function_passes_the_argument_through(void **st) {
    (void)st;
    assert_true(oabi_libm_compute(-1, 42.0, 0.0) == 42.0);
    assert_true(oabi_libm_compute(17, 42.0, 0.0) == 42.0);
    assert_true(oabi_libm_compute(9999, 42.0, 0.0) == 42.0);
}

/* sqrt of a negative is the shape the turret bug took: the emulator must not turn it into
   something finite behind the game's back. */
static void test_sqrt_of_a_negative_is_nan(void **st) {
    (void)st;
    assert_true(isnan(oabi_libm_compute(5, -1.0, 0.0)));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_every_condition_against_every_flag_state),
        cmocka_unit_test(test_al_and_unconditional_always_execute),
        cmocka_unit_test(test_only_the_top_nibble_selects_the_condition),
        cmocka_unit_test(test_lower_cpsr_bits_are_ignored),
        cmocka_unit_test(test_signed_comparisons),
        cmocka_unit_test(test_unsigned_comparisons_are_complementary),
        cmocka_unit_test(test_condition_pairs_are_inverses),
        cmocka_unit_test(test_the_captured_constant),
        cmocka_unit_test(test_word_order_is_high_then_low),
        cmocka_unit_test(test_simple_values),
        cmocka_unit_test(test_the_low_word_is_not_discarded),
        cmocka_unit_test(test_negative_zero_and_infinity),
        cmocka_unit_test(test_round_trip),
        cmocka_unit_test(test_one_argument_functions),
        cmocka_unit_test(test_two_argument_functions_use_both_in_the_right_order),
        cmocka_unit_test(test_an_unknown_function_passes_the_argument_through),
        cmocka_unit_test(test_sqrt_of_a_negative_is_nan),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
