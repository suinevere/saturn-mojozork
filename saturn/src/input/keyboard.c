#include "keyboard.h"

/* QWERTY with a number row on top (cols 0-9) and a numpad on the far right
   (cols 10-12). The numpad digits are identical in both layers, so CapsLock still
   gives numbers. Row 3 col 9 is the space key. */
const char KB_LAYOUT[KB_ROWS][KB_COLS + 1] = {
    "1234567890789",
    "qwertyuiop456",
    "asdfghjkl'123",
    "zxcvbnm,. 0.-"
};

/* Shifted layer (CapsLock on): uppercase letters and the shifted symbols of the
   number/punctuation keys. The numpad (cols 10-12) stays digits. */
const char KB_LAYOUT_UPPER[KB_ROWS][KB_COLS + 1] = {
    "!@#$%^&*()789",
    "QWERTYUIOP456",
    "ASDFGHJKL\"123",
    "ZXCVBNM<> 0.-"
};

static int g_caps = 0;
static int g_insert = 0;
static int g_num = 1;   /* NumLock defaults on (numpad produces digits) */

void keyboard_set_caps(int on) { g_caps = on ? 1 : 0; }
int  keyboard_get_caps(void)   { return g_caps; }
void keyboard_set_insert(int on) { g_insert = on ? 1 : 0; }
int  keyboard_get_insert(void)   { return g_insert; }
void keyboard_set_num(int on) { g_num = on ? 1 : 0; }
int  keyboard_get_num(void)   { return g_num; }

char keyboard_char_at(int row, int col) {
    return (g_caps ? KB_LAYOUT_UPPER : KB_LAYOUT)[row][col];
}

void keyboard_reset(KeyboardState *k) {
    k->cursor_col = 0;
    k->cursor_row = 0;
    k->input_len = 0;
    k->input[0] = '\0';
    k->cursor = 0;
    k->submitted = 0;
}

void keyboard_caret_left(KeyboardState *k)  { if (k->cursor > 0) k->cursor--; }
void keyboard_caret_right(KeyboardState *k) { if (k->cursor < k->input_len) k->cursor++; }
void keyboard_caret_home(KeyboardState *k)  { k->cursor = 0; }
void keyboard_caret_end(KeyboardState *k)   { k->cursor = k->input_len; }

void keyboard_delete_forward(KeyboardState *k) {
    if (k->cursor < k->input_len) {
        for (int i = k->cursor; i < k->input_len; i++) k->input[i] = k->input[i + 1];
        k->input_len--;
    }
}

void keyboard_move(KeyboardState *k, int dcol, int drow) {
    k->cursor_col = (k->cursor_col + dcol + KB_COLS) % KB_COLS;
    k->cursor_row = (k->cursor_row + drow + KB_ROWS) % KB_ROWS;
}

char keyboard_current_char(const KeyboardState *k) {
    return keyboard_char_at(k->cursor_row, k->cursor_col);
}

void keyboard_type_char(KeyboardState *k, char c) {
    if (k->cursor < k->input_len && !g_insert) {
        // Caret inside the line, overwrite mode: replace the char under it, advance.
        k->input[k->cursor++] = c;
    } else if (k->input_len < KB_INPUT_MAX - 1) {
        // Insert at the caret (or append at the end): shift the tail (incl. NUL) right.
        for (int i = k->input_len; i > k->cursor; i--) k->input[i] = k->input[i - 1];
        k->input[k->cursor] = c;
        k->input_len++;
        k->input[k->input_len] = '\0';
        k->cursor++;
    }
}

void keyboard_type(KeyboardState *k) {
    keyboard_type_char(k, keyboard_current_char(k));
}

void keyboard_backspace(KeyboardState *k) {
    if (k->cursor > 0) {
        // Delete the character before the caret, shifting the rest (incl. NUL) left.
        for (int i = k->cursor - 1; i < k->input_len; i++) k->input[i] = k->input[i + 1];
        k->input_len--;
        k->cursor--;
    }
}

void keyboard_submit(KeyboardState *k) {
    k->submitted = 1;
}
