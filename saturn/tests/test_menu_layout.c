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
       clamp on menu_layout.c:28-29, not just fall through by luck. */
    menu_box_fit("X", -50, -40, &x0, &y0, &w, &h);
    assert(w >= 4);
    assert(h >= 4);
    assert(x0 >= 0 && y0 >= 0);
    assert(x0 + w <= 40);
    assert(y0 + h <= 28);
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

int main(void) {
    test_fit_centers_a_normal_box();
    test_fit_widens_for_a_long_title();
    test_fit_clamps_to_the_screen();
    test_fit_never_goes_negative();
    test_fit_handles_null_title();
    test_fit_clamps_negative_inputs();
    test_fit_survives_int_max_inputs();
    printf("test_menu_layout: OK\n");
    return 0;
}
