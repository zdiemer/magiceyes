/* Unit tests for host/engine/padmap.c -- the GP2X button bitmap to Wiz pad word mapping.
 *
 * ME_TEST_SRC: host/engine/padmap.c
 *
 * The viewer always writes the canonical GP2X order; the Wiz's hardware reports the Pollux pad
 * layout, and the guest reads the hardware. A wrong bit here does not crash anything, it just
 * means a button does the wrong thing (or nothing) in every Wiz title at once.
 *
 * The diagonals are the part worth pinning: they set TWO bits, because the Pollux pad has no
 * diagonal of its own and expresses one as both of its components held together.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "padmap.h"
#include "gp2xshm.h"

/* Pollux pad bit positions, from the unstripped fxi runtime's wizJoystickRead. */
enum {
    W_R = 6, W_L = 7, W_SELECT = 8, W_MENU = 9, W_VOLUP = 10, W_VOLDOWN = 11,
    W_LEFT = 16, W_RIGHT = 17, W_UP = 18, W_DOWN = 19,
    W_A = 20, W_B = 21, W_X = 22, W_Y = 23, W_CLICK = 27,
};

#define BIT(n) (1u << (n))

static uint32_t map(int gp2x_bit) { return wiz_button_word(BIT(gp2x_bit)); }

/* ---- nothing in, nothing out ------------------------------------------------------------- */

static void test_nothing_pressed(void **st) {
    (void)st;
    assert_int_equal(wiz_button_word(0), 0);
}

/* ---- the four cardinal directions ---------------------------------------------------------- */

static void test_cardinal_directions(void **st) {
    (void)st;
    assert_int_equal(map(GP2X_UP), BIT(W_UP));
    assert_int_equal(map(GP2X_DOWN), BIT(W_DOWN));
    assert_int_equal(map(GP2X_LEFT), BIT(W_LEFT));
    assert_int_equal(map(GP2X_RIGHT), BIT(W_RIGHT));
}

/* ---- the diagonals: two bits each ------------------------------------------------------------ */

/* The Pollux pad has no diagonal bit, so a diagonal is both components held at once. Emitting
   only one of them is the failure mode: the stick appears to work but never goes diagonally. */
static void test_diagonals_set_both_components(void **st) {
    (void)st;
    assert_int_equal(map(GP2X_UPLEFT), BIT(W_LEFT) | BIT(W_UP));
    assert_int_equal(map(GP2X_UPRIGHT), BIT(W_RIGHT) | BIT(W_UP));
    assert_int_equal(map(GP2X_DOWNLEFT), BIT(W_LEFT) | BIT(W_DOWN));
    assert_int_equal(map(GP2X_DOWNRIGHT), BIT(W_RIGHT) | BIT(W_DOWN));
}

static void test_every_diagonal_has_exactly_two_bits(void **st) {
    (void)st;
    const int diagonals[] = {GP2X_UPLEFT, GP2X_UPRIGHT, GP2X_DOWNLEFT, GP2X_DOWNRIGHT};
    for (size_t i = 0; i < sizeof diagonals / sizeof diagonals[0]; i++) {
        uint32_t w = map(diagonals[i]);
        assert_int_equal(__builtin_popcount(w), 2);
    }
}

/* A diagonal must be the union of its own two cardinals, not some other pair. */
static void test_diagonals_agree_with_their_cardinals(void **st) {
    (void)st;
    assert_int_equal(map(GP2X_UPLEFT), map(GP2X_UP) | map(GP2X_LEFT));
    assert_int_equal(map(GP2X_UPRIGHT), map(GP2X_UP) | map(GP2X_RIGHT));
    assert_int_equal(map(GP2X_DOWNLEFT), map(GP2X_DOWN) | map(GP2X_LEFT));
    assert_int_equal(map(GP2X_DOWNRIGHT), map(GP2X_DOWN) | map(GP2X_RIGHT));
}

/* ---- face and shoulder buttons ---------------------------------------------------------------- */

static void test_face_buttons(void **st) {
    (void)st;
    assert_int_equal(map(GP2X_A), BIT(W_A));
    assert_int_equal(map(GP2X_B), BIT(W_B));
    assert_int_equal(map(GP2X_X), BIT(W_X));
    assert_int_equal(map(GP2X_Y), BIT(W_Y));
}

static void test_shoulder_buttons(void **st) {
    (void)st;
    assert_int_equal(map(GP2X_L), BIT(W_L));
    assert_int_equal(map(GP2X_R), BIT(W_R));
}

/* START is the Wiz MENU button. */
static void test_start_is_menu_and_select_is_select(void **st) {
    (void)st;
    assert_int_equal(map(GP2X_START), BIT(W_MENU));
    assert_int_equal(map(GP2X_SELECT), BIT(W_SELECT));
}

static void test_volume_and_stick_click(void **st) {
    (void)st;
    assert_int_equal(map(GP2X_VOLUP), BIT(W_VOLUP));
    assert_int_equal(map(GP2X_VOLDOWN), BIT(W_VOLDOWN));
    assert_int_equal(map(GP2X_CLICK), BIT(W_CLICK));
}

/* ---- combinations ------------------------------------------------------------------------------ */

/* Holding a direction and a face button at once is the normal case in any game. */
static void test_combinations_are_the_union(void **st) {
    (void)st;
    uint32_t w = wiz_button_word(BIT(GP2X_RIGHT) | BIT(GP2X_A));
    assert_int_equal(w, BIT(W_RIGHT) | BIT(W_A));

    w = wiz_button_word(BIT(GP2X_UPLEFT) | BIT(GP2X_B) | BIT(GP2X_L));
    assert_int_equal(w, BIT(W_LEFT) | BIT(W_UP) | BIT(W_B) | BIT(W_L));
}

static void test_every_button_at_once(void **st) {
    (void)st;
    const int all[] = {GP2X_UP, GP2X_DOWN, GP2X_LEFT, GP2X_RIGHT, GP2X_START, GP2X_SELECT,
                       GP2X_L, GP2X_R, GP2X_A, GP2X_B, GP2X_X, GP2X_Y,
                       GP2X_VOLUP, GP2X_VOLDOWN, GP2X_CLICK};
    uint32_t in = 0, want = 0;
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        in |= BIT(all[i]);
        want |= map(all[i]);
    }
    assert_int_equal(wiz_button_word(in), want);
}

/* ---- properties that must hold across the whole map ---------------------------------------------- */

/* No canonical button may map to nothing: a hole here is a button that silently does not work. */
static void test_every_canonical_button_maps_somewhere(void **st) {
    (void)st;
    for (int b = GP2X_UP; b <= GP2X_CLICK; b++)
        assert_int_not_equal(map(b), 0);
}

/* Nothing may land above bit 27 or below bit 6: the pad word has no meaning outside that. */
static void test_no_stray_bits(void **st) {
    (void)st;
    const uint32_t legal =
        BIT(W_R) | BIT(W_L) | BIT(W_SELECT) | BIT(W_MENU) | BIT(W_VOLUP) | BIT(W_VOLDOWN) |
        BIT(W_LEFT) | BIT(W_RIGHT) | BIT(W_UP) | BIT(W_DOWN) |
        BIT(W_A) | BIT(W_B) | BIT(W_X) | BIT(W_Y) | BIT(W_CLICK);
    for (int b = GP2X_UP; b <= GP2X_CLICK; b++)
        assert_int_equal(map(b) & ~legal, 0);
}

/* Two different non-diagonal buttons must not collide onto the same pad bit. */
static void test_distinct_buttons_do_not_collide(void **st) {
    (void)st;
    const int singles[] = {GP2X_UP, GP2X_DOWN, GP2X_LEFT, GP2X_RIGHT, GP2X_START, GP2X_SELECT,
                           GP2X_L, GP2X_R, GP2X_A, GP2X_B, GP2X_X, GP2X_Y,
                           GP2X_VOLUP, GP2X_VOLDOWN, GP2X_CLICK};
    size_t n = sizeof singles / sizeof singles[0];
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            assert_int_equal(map(singles[i]) & map(singles[j]), 0);
}

/* Bits the canonical word does not define must not produce pad output. */
static void test_undefined_input_bits_are_ignored(void **st) {
    (void)st;
    assert_int_equal(wiz_button_word(1u << 28), 0);
    assert_int_equal(wiz_button_word(1u << 31), 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_nothing_pressed),
        cmocka_unit_test(test_cardinal_directions),
        cmocka_unit_test(test_diagonals_set_both_components),
        cmocka_unit_test(test_every_diagonal_has_exactly_two_bits),
        cmocka_unit_test(test_diagonals_agree_with_their_cardinals),
        cmocka_unit_test(test_face_buttons),
        cmocka_unit_test(test_shoulder_buttons),
        cmocka_unit_test(test_start_is_menu_and_select_is_select),
        cmocka_unit_test(test_volume_and_stick_click),
        cmocka_unit_test(test_combinations_are_the_union),
        cmocka_unit_test(test_every_button_at_once),
        cmocka_unit_test(test_every_canonical_button_maps_somewhere),
        cmocka_unit_test(test_no_stray_bits),
        cmocka_unit_test(test_distinct_buttons_do_not_collide),
        cmocka_unit_test(test_undefined_input_bits_are_ignored),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
