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
