/*----------------------
 | keyboard.h
 | Description: The on-screen keyboard model and the input-line editor: the grid
 |   layouts, the Caps/Insert/Num toggle state, picker movement, and caret-aware
 |   type/insert/delete. Pure logic (no rendering, no device polling); implemented
 |   in keyboard.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef KEYBOARD_H
#define KEYBOARD_H

/*----------------------
 | KB_COLS / KB_ROWS / KB_INPUT_MAX
 | Description: The on-screen keyboard grid dimensions and the maximum input-line
 |   length (including its NUL).
 | Author: suinevere
 ----------------------*/
#define KB_COLS 13
#define KB_ROWS 4
#define KB_INPUT_MAX 64

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | KeyboardState
 | Description: The on-screen keyboard picker position (cursor_col/row), the input
 |   line and its length, the text caret within it (0..input_len), and whether the
 |   line has been submitted.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int  cursor_col;    // on-screen keyboard grid column
    int  cursor_row;    // on-screen keyboard grid row
    char input[KB_INPUT_MAX];
    int  input_len;
    int  cursor;        // text caret within input (0..input_len)
    int  submitted;
} KeyboardState;

/*----------------------
 | KB_LAYOUT / KB_LAYOUT_UPPER
 | Description: The base (lowercase/unshifted) and shifted (uppercase/symbols) grid
 |   layouts, indexed [row][col].
 | Author: suinevere
 ----------------------*/
extern const char KB_LAYOUT[KB_ROWS][KB_COLS + 1];
extern const char KB_LAYOUT_UPPER[KB_ROWS][KB_COLS + 1];

/*----------------------
 | keyboard_reset / keyboard_move
 | Description: reset clears the line and picker; move steps the on-screen picker
 |   by (dcol, drow), wrapping both axes.
 | Author: suinevere
 ----------------------*/
void keyboard_reset(KeyboardState *k);
void keyboard_move(KeyboardState *k, int dcol, int drow);
/*----------------------
 | caret + query (keyboard_caret_* / delete_forward / current_char / char_at)
 | Description: caret_left/right/home/end move the text caret within the line;
 |   delete_forward removes the character under it (caret stays); current_char is
 |   the character the picker is on; char_at is the character at (row,col) in the
 |   active (caps-aware) layer.
 | Author: suinevere
 ----------------------*/
void keyboard_caret_left(KeyboardState *k);
void keyboard_caret_right(KeyboardState *k);
void keyboard_caret_home(KeyboardState *k);
void keyboard_caret_end(KeyboardState *k);
void keyboard_delete_forward(KeyboardState *k);
char keyboard_current_char(const KeyboardState *k);
char keyboard_char_at(int row, int col);

/*----------------------
 | toggles (keyboard_{set,get}_{caps,insert,num})
 | Description: CapsLock selects the KB_LAYOUT_UPPER layer; Insert makes mid-line
 |   typing insert (shift the tail right) rather than overwrite (append is the same
 |   either way); NumLock (defaults on, shared with the physical key and the
 |   Keyboard Controls page) suppresses the physical numpad digits when off.
 | Author: suinevere
 ----------------------*/
void keyboard_set_caps(int on);
int  keyboard_get_caps(void);
void keyboard_set_insert(int on);
int  keyboard_get_insert(void);
void keyboard_set_num(int on);
int  keyboard_get_num(void);

/*----------------------
 | edit (keyboard_type / type_char / backspace / submit)
 | Description: type types the character under the picker; type_char inserts/
 |   overwrites a given character; backspace deletes before the caret; submit marks
 |   the line entered.
 | Author: suinevere
 ----------------------*/
void keyboard_type(KeyboardState *k);
void keyboard_type_char(KeyboardState *k, char c);
void keyboard_backspace(KeyboardState *k);
void keyboard_submit(KeyboardState *k);

#ifdef __cplusplus
}
#endif
#endif /* KEYBOARD_H */
