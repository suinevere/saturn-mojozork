#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

/* Pure layout arithmetic for the menu system. Deliberately free of any SRL or
   Saturn dependency so it can be unit-tested on the host; main.cxx owns all the
   drawing and input handling and calls in here for the geometry. */

#define MENU_SCREEN_COLS 40
#define MENU_SCREEN_ROWS 28

/* Columns reserved for a "N) " row-number prefix. Reserved unconditionally,
   whether or not the digits are currently drawn, so a box does not resize when
   the player switches between the pad and a real keyboard mid-menu. */
#define MENU_DIGIT_COLS  3

/* Fit a centered box around `content_w` columns and `rows` rows of content.
   Width is the wider of the content and the title, plus two border columns and
   one pad column each side; height is the content plus a top border, a title
   row, a blank row, and a bottom border. Both are clamped to the screen, and
   the result is always fully on-screen. */
void menu_box_fit(const char *title, int content_w, int rows,
                  int *x0, int *y0, int *w, int *h);

#endif
