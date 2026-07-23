/*----------------------
 | menu_layout.h
 | Description: Pure layout arithmetic and digit-key mapping for the menu system,
 |   deliberately free of any SRL/Saturn dependency so it unit-tests on the host.
 |   menu.cxx / menu_pages.cxx own the drawing and input and call in here for the
 |   geometry. Implemented in menu_layout.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

/*----------------------
 | MENU_SCREEN_COLS / MENU_SCREEN_ROWS / MENU_DIGIT_COLS / MENU_ROW_TEXT_MAX
 | Description: The screen size in text cells; the columns reserved for a "N) "
 |   row-number prefix (reserved unconditionally so a box does not resize when the
 |   player switches pad<->keyboard mid-menu); and the longest text a single menu
 |   row can draw without touching the box border (31 = 32 ceiling minus one margin
 |   column, after 5 columns of chrome). Callers building rows from external data
 |   clamp to MENU_ROW_TEXT_MAX, since a clamped box truncates silently.
 | Author: suinevere
 ----------------------*/
#define MENU_SCREEN_COLS 40
#define MENU_SCREEN_ROWS 28
#define MENU_DIGIT_COLS  3
#define MENU_ROW_TEXT_MAX 31

/*----------------------
 | menu_box_fit
 | Description: Fits a centered box around `content_w` columns and `rows` rows:
 |   width is the wider of content and title plus border+pad each side, height is
 |   the content plus top border, title row, blank row, and bottom border; both
 |   clamped so the result is always fully on-screen.
 | Author: suinevere
 ----------------------*/
void menu_box_fit(const char *title, int content_w, int rows,
                  int *x0, int *y0, int *w, int *h);

/*----------------------
 | menu_row_digit
 | Description: Maps a character to a 0-based row index (or -1). A plain digit 1-9
 |   sets *dir to +1; its shifted symbol (!@#$%^&*(, US layout) sets -1, so value
 |   rows cycle forward/backward and action rows just activate. The shifted
 |   character is matched because SaturnKeyEvent carries no modifier flag.
 | Author: suinevere
 ----------------------*/
int menu_row_digit(char ch, int nrows, int *dir);

/*----------------------
 | menu_visible_digit
 | Description: Which absolute list index a plain digit selects through a scroll
 |   window of `visible` rows starting at `top` in a list of `count`; -1 if the
 |   digit names no visible row.
 | Author: suinevere
 ----------------------*/
int menu_visible_digit(char ch, int top, int visible, int count);

#endif
