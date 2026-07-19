#include "menu_layout.h"

void menu_box_fit(const char *title, int content_w, int rows,
                  int *x0, int *y0, int *w, int *h) {
    int tlen = 0;
    int bw, bh;

    if (title != 0) while (title[tlen] != '\0') tlen++;

    if (content_w < 0) content_w = 0;
    if (rows < 0)      rows = 0;

    bw = (tlen > content_w ? tlen : content_w) + 4;   /* borders + pads */
    bh = rows + 4;                                    /* borders + title + blank */

    if (bw > MENU_SCREEN_COLS) bw = MENU_SCREEN_COLS;
    if (bh > MENU_SCREEN_ROWS) bh = MENU_SCREEN_ROWS;

    *w  = bw;
    *h  = bh;
    *x0 = (MENU_SCREEN_COLS - bw) / 2;
    *y0 = (MENU_SCREEN_ROWS - bh) / 2;
}
