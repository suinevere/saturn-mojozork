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

/* Map a printable character to a 0-based row index, or -1 if it selects no row.
   A plain digit 1-9 sets *dir to +1; the matching shifted symbol (!@#$%^&*(, US
   layout) sets it to -1. Callers use *dir to decide whether the row's value
   cycles forward or backward; rows that are actions rather than value cyclers
   ignore it and simply activate. SaturnKeyEvent carries no modifier flag, which
   is why the shifted character is what gets matched. */
int menu_row_digit(char ch, int nrows, int *dir);

/* Which absolute list index a digit selects, given a scroll window of `visible`
   rows starting at `top` in a list of `count` items. Only plain digits select --
   a list pick has no backward direction. Returns -1 if the digit names no
   visible row. */
int menu_visible_digit(char ch, int top, int visible, int count);

#endif
