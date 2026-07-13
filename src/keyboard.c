#include "keyboard.h"

const char KB_LAYOUT[KB_ROWS][KB_COLS + 1] = {
    "abcdefghij",
    "klmnopqrst",
    "uvwxyz0123",
    "456789.,' "
};

void keyboard_reset(KeyboardState *k) {
    k->cursor_col = 0;
    k->cursor_row = 0;
    k->input_len = 0;
    k->input[0] = '\0';
    k->submitted = 0;
}

void keyboard_move(KeyboardState *k, int dcol, int drow) {
    k->cursor_col = (k->cursor_col + dcol + KB_COLS) % KB_COLS;
    k->cursor_row = (k->cursor_row + drow + KB_ROWS) % KB_ROWS;
}

char keyboard_current_char(const KeyboardState *k) {
    return KB_LAYOUT[k->cursor_row][k->cursor_col];
}

void keyboard_type_char(KeyboardState *k, char c) {
    if (k->input_len < KB_INPUT_MAX - 1) {
        k->input[k->input_len++] = c;
        k->input[k->input_len] = '\0';
    }
}

void keyboard_type(KeyboardState *k) {
    keyboard_type_char(k, keyboard_current_char(k));
}

void keyboard_backspace(KeyboardState *k) {
    if (k->input_len > 0) {
        k->input[--k->input_len] = '\0';
    }
}

void keyboard_submit(KeyboardState *k) {
    k->submitted = 1;
}
