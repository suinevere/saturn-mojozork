#include "menu_layout.h"

void menu_box_fit(const char *title, int content_w, int rows,
                  int *x0, int *y0, int *w, int *h) {
    int tlen = 0;
    int bw, bh;

    /* Stop counting once tlen can no longer matter: it only ever competes
       with content_w for "widest", and both get clamped to the screen width
       below, so counting past that is wasted work on pathological input. */
    if (title != 0) while (tlen < MENU_SCREEN_COLS && title[tlen] != '\0') tlen++;

    /* Only rows needs a negative guard. A negative content_w cannot reach the
       width: tlen is never negative and the two compete through a max() below,
       so the max already neutralizes it. rows has no such shield -- bh is rows
       plus a constant -- so without this a negative rows yields a negative
       height. */
    if (rows < 0) rows = 0;

    /* Clamp inputs to the screen bound BEFORE the "+ 4" below. Doing the
       clamp only after the addition (as before) lets a huge content_w/rows
       overflow signed int in the addition, producing a negative result that
       slips past the ">" clamp check entirely and escapes the screen. */
    if (content_w > MENU_SCREEN_COLS) content_w = MENU_SCREEN_COLS;
    if (rows > MENU_SCREEN_ROWS)      rows = MENU_SCREEN_ROWS;

    bw = (tlen > content_w ? tlen : content_w) + 4;   /* borders + pads */
    bh = rows + 4;                                    /* borders + title + blank */

    if (bw > MENU_SCREEN_COLS) bw = MENU_SCREEN_COLS;
    if (bh > MENU_SCREEN_ROWS) bh = MENU_SCREEN_ROWS;

    *w  = bw;
    *h  = bh;
    *x0 = (MENU_SCREEN_COLS - bw) / 2;
    *y0 = (MENU_SCREEN_ROWS - bh) / 2;
}

/* Shifted digits on a US layout, index 0 == Shift+1. */
static const char MENU_SHIFTED_DIGITS[] = "!@#$%^&*(";

/* 0-based row for a plain digit, or -1. */
static int menu_plain_digit_row(char ch) {
    if (ch >= '1' && ch <= '9') return (int) (ch - '1');
    return -1;
}

/* 0-based row for a shifted digit, or -1. */
static int menu_shifted_digit_row(char ch) {
    int i;
    for (i = 0; i < 9; i++) if (MENU_SHIFTED_DIGITS[i] == ch) return i;
    return -1;
}

int menu_row_digit(char ch, int nrows, int *dir) {
    int row = menu_plain_digit_row(ch);
    int d   = 1;

    if (row < 0) {
        row = menu_shifted_digit_row(ch);
        d   = -1;
    }
    if (row < 0 || row >= nrows) return -1;

    if (dir != 0) *dir = d;
    return row;
}

int menu_visible_digit(char ch, int top, int visible, int count) {
    int row = menu_plain_digit_row(ch);
    int idx;

    if (row < 0 || row >= visible) return -1;
    idx = top + row;
    if (idx < 0 || idx >= count) return -1;
    return idx;
}
