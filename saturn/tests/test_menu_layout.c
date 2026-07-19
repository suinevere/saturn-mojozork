#include "../src/menu_layout.h"
#include <limits.h>
#include <stdio.h>
#include <assert.h>

static void test_fit_centers_a_normal_box(void) {
    int x0, y0, w, h;
    /* title 5 cols, content 20 cols, 5 rows -> w = 20+4, h = 5+4 */
    menu_box_fit("SOUND", 20, 5, &x0, &y0, &w, &h);
    assert(w == 24);
    assert(h == 9);
    assert(x0 == (40 - 24) / 2);   /* 8 */
    assert(y0 == (28 - 9) / 2);    /* 9 */
}

static void test_fit_widens_for_a_long_title(void) {
    int x0, y0, w, h;
    /* title is 22 cols and beats the 4-col content */
    menu_box_fit("A VERY LONG TITLE HERE", 4, 2, &x0, &y0, &w, &h);
    assert(w == 26);
    assert(h == 6);
    assert(x0 == 7);
    assert(y0 == 11);
}

static void test_fit_clamps_to_the_screen(void) {
    int x0, y0, w, h;
    menu_box_fit("X", 50, 40, &x0, &y0, &w, &h);
    assert(w == 40);            /* clamped to MENU_SCREEN_COLS */
    assert(h == 28);            /* clamped to MENU_SCREEN_ROWS */
    assert(x0 == 0);
    assert(y0 == 0);
}

static void test_fit_never_goes_negative(void) {
    int x0, y0, w, h;
    menu_box_fit("", 0, 0, &x0, &y0, &w, &h);
    assert(w >= 4);             /* two borders + two pads */
    assert(h >= 4);
    assert(x0 >= 0 && y0 >= 0);
    assert(x0 + w <= 40);
    assert(y0 + h <= 28);
}

static void test_fit_handles_null_title(void) {
    int x0, y0, w, h;
    /* title guard: a NULL title must not be dereferenced, and content_w
       alone should drive the width. */
    menu_box_fit(0, 10, 3, &x0, &y0, &w, &h);
    assert(w == 14);
    assert(h == 7);
    assert(x0 >= 0 && y0 >= 0);
    assert(x0 + w <= 40);
    assert(y0 + h <= 28);
}

static void test_fit_clamps_negative_inputs(void) {
    int x0, y0, w, h;
    /* content_w and rows below zero (not just zero) must hit the negative
       clamp on menu_layout.c:13-14, not just fall through by luck.
       With empty title: tlen=0, content_w clamped to 0, rows clamped to 0,
       so bw = max(0, 0) + 4 = 4, bh = 0 + 4 = 4,
       x0 = (40-4)/2 = 18, y0 = (28-4)/2 = 12. */
    menu_box_fit("", -50, -40, &x0, &y0, &w, &h);
    assert(w == 4);
    assert(h == 4);
    assert(x0 == 18);
    assert(y0 == 12);
}

static void test_fit_survives_int_max_inputs(void) {
    int x0, y0, w, h;
    /* Adversarial: content_w/rows near INT_MAX must not overflow the
       "+ 4" before the screen clamp is applied. A pre-fix build produces
       a negative bw/bh that slips past the clamp and lands off-screen. */
    menu_box_fit("X", INT_MAX, 5, &x0, &y0, &w, &h);
    assert(w == 40);
    assert(x0 >= 0 && y0 >= 0);
    assert(x0 + w <= 40);
    assert(y0 + h <= 28);

    menu_box_fit("X", 5, INT_MAX, &x0, &y0, &w, &h);
    assert(h == 28);
    assert(x0 >= 0 && y0 >= 0);
    assert(x0 + w <= 40);
    assert(y0 + h <= 28);
}

static void test_plain_digit_picks_a_row_forward(void) {
    int dir = 0;
    assert(menu_row_digit('3', 5, &dir) == 2);
    assert(dir == 1);
    assert(menu_row_digit('1', 5, &dir) == 0);
    assert(dir == 1);
}

static void test_shifted_digit_picks_a_row_backward(void) {
    int dir = 0;
    assert(menu_row_digit('#', 5, &dir) == 2);   /* Shift+3 */
    assert(dir == -1);
    assert(menu_row_digit('!', 5, &dir) == 0);   /* Shift+1 */
    assert(dir == -1);
    assert(menu_row_digit('(', 9, &dir) == 8);   /* Shift+9 */
    assert(dir == -1);
}

static void test_digit_past_the_row_count_is_rejected(void) {
    int dir = 0;
    assert(menu_row_digit('7', 5, &dir) == -1);
    assert(menu_row_digit('&', 5, &dir) == -1);  /* Shift+7 */
    assert(menu_row_digit('6', 5, &dir) == -1);   /* row 5 == nrows: the exact boundary */
}

static void test_non_selecting_characters_are_rejected(void) {
    int dir = 0;
    assert(menu_row_digit('0', 5, &dir) == -1);  /* rows are 1-9, never 0 */
    assert(menu_row_digit('a', 5, &dir) == -1);
    assert(menu_row_digit(' ', 5, &dir) == -1);
}

static void test_visible_digit_maps_through_the_scroll_window(void) {
    /* 30 games, window of 16 starting at 10: "3" is the third visible row. */
    assert(menu_visible_digit('3', 10, 16, 30) == 12);
    assert(menu_visible_digit('1', 25, 16, 30) == 25);
}

static void test_visible_digit_rejects_rows_past_the_end(void) {
    /* only 5 items, so the 9th visible row does not exist */
    assert(menu_visible_digit('9', 0, 16, 5) == -1);
    assert(menu_visible_digit('6', 0, 16, 5) == -1);
}

static void test_visible_digit_ignores_shift(void) {
    /* a list pick has no "backward"; only plain digits select */
    assert(menu_visible_digit('#', 0, 16, 30) == -1);
}

static void test_visible_digit_rejects_rows_past_the_window(void) {
    /* 5 rows on screen out of 30 total: row 8 exists in the list but is not
       currently visible, so only the window guard can reject it -- the count
       check cannot, since 8 < 30. */
    assert(menu_visible_digit('9', 0, 5, 30) == -1);
}

int main(void) {
    test_fit_centers_a_normal_box();
    test_fit_widens_for_a_long_title();
    test_fit_clamps_to_the_screen();
    test_fit_never_goes_negative();
    test_fit_handles_null_title();
    test_fit_clamps_negative_inputs();
    test_fit_survives_int_max_inputs();
    test_plain_digit_picks_a_row_forward();
    test_shifted_digit_picks_a_row_backward();
    test_digit_past_the_row_count_is_rejected();
    test_non_selecting_characters_are_rejected();
    test_visible_digit_maps_through_the_scroll_window();
    test_visible_digit_rejects_rows_past_the_end();
    test_visible_digit_ignores_shift();
    test_visible_digit_rejects_rows_past_the_window();
    printf("test_menu_layout: OK\n");
    return 0;
}
