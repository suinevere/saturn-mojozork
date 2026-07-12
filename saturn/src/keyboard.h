#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KB_COLS 10
#define KB_ROWS 4
#define KB_INPUT_MAX 64

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  cursor_col;
    int  cursor_row;
    char input[KB_INPUT_MAX];
    int  input_len;
    int  submitted;
} KeyboardState;

extern const char KB_LAYOUT[KB_ROWS][KB_COLS + 1];        /* lowercase / unshifted */
extern const char KB_LAYOUT_UPPER[KB_ROWS][KB_COLS + 1];  /* uppercase / shifted symbols */

void keyboard_reset(KeyboardState *k);
void keyboard_move(KeyboardState *k, int dcol, int drow);
char keyboard_current_char(const KeyboardState *k);
/* Character at (row,col) in the active layer (caps-aware). */
char keyboard_char_at(int row, int col);
/* CapsLock: when on, keys produce the KB_LAYOUT_UPPER layer. */
void keyboard_set_caps(int on);
int  keyboard_get_caps(void);
void keyboard_type(KeyboardState *k);
void keyboard_type_char(KeyboardState *k, char c);
void keyboard_backspace(KeyboardState *k);
void keyboard_submit(KeyboardState *k);

#ifdef __cplusplus
}
#endif
#endif /* KEYBOARD_H */
