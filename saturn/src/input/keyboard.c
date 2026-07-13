/*----------------------
 | keyboard.c
 | Description: The on-screen keyboard model and the input-line editor beneath it:
 |   the two key layouts (normal and shifted), the Caps/Insert/Num toggle state,
 |   picker movement over the grid, and the caret-aware type/insert/delete
 |   operations on the command line. Pure logic -- no rendering (console_view draws
 |   it) and no device polling (input/saturn_keyboard feed it).
 | Author: suinevere
 | Dependencies: keyboard.h (KeyboardState, KB_ROWS/KB_COLS/KB_INPUT_MAX)
 ----------------------*/
#include "keyboard.h"

/*----------------------
 | KB_LAYOUT
 | Description: The base grid: QWERTY with a number row on top (cols 0-9) and a
 |   numpad on the far right (cols 10-12). The numpad digits are identical in both
 |   layers, so CapsLock still gives numbers. Row 3 col 9 is the space key.
 | Author: suinevere
 ----------------------*/
const char KB_LAYOUT[KB_ROWS][KB_COLS + 1] = {
    "1234567890789",
    "qwertyuiop456",
    "asdfghjkl'123",
    "zxcvbnm,. 0.-"
};

/*----------------------
 | KB_LAYOUT_UPPER
 | Description: The shifted layer (CapsLock on): uppercase letters and the shifted
 |   symbols of the number/punctuation keys. The numpad (cols 10-12) stays digits.
 | Author: suinevere
 ----------------------*/
const char KB_LAYOUT_UPPER[KB_ROWS][KB_COLS + 1] = {
    "!@#$%^&*()789",
    "QWERTYUIOP456",
    "ASDFGHJKL\"123",
    "ZXCVBNM<> 0.-"
};

/*----------------------
 | g_caps / g_insert / g_num
 | Description: Keyboard toggle state: CapsLock (selects the shifted layer),
 |   Insert (insert vs overwrite while typing), and NumLock (defaults on, so the
 |   numpad produces digits).
 | Author: suinevere
 ----------------------*/
static int g_caps = 0;
static int g_insert = 0;
static int g_num = 1;

/*----------------------
 | keyboard_set/get_caps / _insert / _num
 | Description: Setters and getters for the three toggle flags; setters normalize
 |   to 0/1.
 | Author: suinevere
 ----------------------*/
void keyboard_set_caps(int on) { g_caps = on ? 1 : 0; }
int  keyboard_get_caps(void)   { return g_caps; }
void keyboard_set_insert(int on) { g_insert = on ? 1 : 0; }
int  keyboard_get_insert(void)   { return g_insert; }
void keyboard_set_num(int on) { g_num = on ? 1 : 0; }
int  keyboard_get_num(void)   { return g_num; }

/*----------------------
 | keyboard_char_at
 | Description: The character at a grid cell in the layer CapsLock currently
 |   selects.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_caps, KB_LAYOUT, KB_LAYOUT_UPPER
 | Params: row, col -- grid coordinates
 | Returns: the key character at that cell
 ----------------------*/
char keyboard_char_at(int row, int col) {
    return (g_caps ? KB_LAYOUT_UPPER : KB_LAYOUT)[row][col];
}

/*----------------------
 | keyboard_reset
 | Description: Clears an input line back to empty: picker at the top-left, no
 |   text, caret at 0, not submitted.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the keyboard/input-line state to clear
 | Returns: N/A
 ----------------------*/
void keyboard_reset(KeyboardState *k) {
    k->cursor_col = 0;
    k->cursor_row = 0;
    k->input_len = 0;
    k->input[0] = '\0';
    k->cursor = 0;
    k->submitted = 0;
}

/*----------------------
 | keyboard_caret_left / _right / _home / _end
 | Description: Move the text caret within the line: one char left/right (clamped
 |   to the ends) or straight to the start/end.
 | Author: suinevere
 ----------------------*/
void keyboard_caret_left(KeyboardState *k)  { if (k->cursor > 0) k->cursor--; }
void keyboard_caret_right(KeyboardState *k) { if (k->cursor < k->input_len) k->cursor++; }
void keyboard_caret_home(KeyboardState *k)  { k->cursor = 0; }
void keyboard_caret_end(KeyboardState *k)   { k->cursor = k->input_len; }

/*----------------------
 | keyboard_delete_forward
 | Description: Deletes the character under the caret (Delete key), shifting the
 |   rest of the line (including its NUL) left; a no-op at end of line.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- input-line state
 | Returns: N/A
 ----------------------*/
void keyboard_delete_forward(KeyboardState *k) {
    if (k->cursor < k->input_len) {
        for (int i = k->cursor; i < k->input_len; i++) k->input[i] = k->input[i + 1];
        k->input_len--;
    }
}

/*----------------------
 | keyboard_move
 | Description: Moves the on-screen picker by (dcol, drow), wrapping around both
 |   axes of the grid.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- keyboard state; dcol/drow -- signed step on each axis
 | Returns: N/A
 ----------------------*/
void keyboard_move(KeyboardState *k, int dcol, int drow) {
    k->cursor_col = (k->cursor_col + dcol + KB_COLS) % KB_COLS;
    k->cursor_row = (k->cursor_row + drow + KB_ROWS) % KB_ROWS;
}

/*----------------------
 | keyboard_current_char
 | Description: The character of the cell the picker is on.
 | Author: suinevere
 ----------------------*/
char keyboard_current_char(const KeyboardState *k) {
    return keyboard_char_at(k->cursor_row, k->cursor_col);
}

/*----------------------
 | keyboard_type_char
 | Description: Inserts (or, in overwrite mode with the caret inside the line,
 |   replaces) one character at the caret and advances it. Insert/append shifts
 |   the tail (including the NUL) right; both paths respect KB_INPUT_MAX.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_insert
 | Params: k -- input-line state; c -- character to add
 | Returns: N/A
 ----------------------*/
void keyboard_type_char(KeyboardState *k, char c) {
    if (k->cursor < k->input_len && !g_insert) {
        k->input[k->cursor++] = c;
    } else if (k->input_len < KB_INPUT_MAX - 1) {
        for (int i = k->input_len; i > k->cursor; i--) k->input[i] = k->input[i - 1];
        k->input[k->cursor] = c;
        k->input_len++;
        k->input[k->input_len] = '\0';
        k->cursor++;
    }
}

/*----------------------
 | keyboard_type
 | Description: Types the character the picker is currently on (the gamepad's
 |   "type" action).
 | Author: suinevere
 ----------------------*/
void keyboard_type(KeyboardState *k) {
    keyboard_type_char(k, keyboard_current_char(k));
}

/*----------------------
 | keyboard_backspace
 | Description: Deletes the character before the caret, shifting the rest
 |   (including the NUL) left; a no-op at the start of the line.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- input-line state
 | Returns: N/A
 ----------------------*/
void keyboard_backspace(KeyboardState *k) {
    if (k->cursor > 0) {
        for (int i = k->cursor - 1; i < k->input_len; i++) k->input[i] = k->input[i + 1];
        k->input_len--;
        k->cursor--;
    }
}

/*----------------------
 | keyboard_submit
 | Description: Marks the line submitted, ending the read loop.
 | Author: suinevere
 ----------------------*/
void keyboard_submit(KeyboardState *k) {
    k->submitted = 1;
}
