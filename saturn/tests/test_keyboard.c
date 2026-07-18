#include "../src/keyboard.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

int main(void) {
    KeyboardState k;

    /* The character literals below track KB_LAYOUT's first row; if the layout
       changes, this guard fails first and points at what to update. */
    assert(KB_LAYOUT[0][0] == '1' && KB_LAYOUT[0][2] == '3');

    keyboard_reset(&k);
    assert(k.cursor_col == 0 && k.cursor_row == 0);
    assert(keyboard_current_char(&k) == '1');   /* KB_LAYOUT[0][0] */

    /* type '1' */
    keyboard_type(&k);
    assert(k.input_len == 1 && strcmp(k.input, "1") == 0);

    /* move right twice -> '3', type it */
    keyboard_move(&k, 1, 0);
    keyboard_move(&k, 1, 0);
    assert(keyboard_current_char(&k) == '3');
    keyboard_type(&k);
    assert(strcmp(k.input, "13") == 0);

    /* backspace */
    keyboard_backspace(&k);
    assert(strcmp(k.input, "1") == 0);

    /* left wraps from col 0 to col KB_COLS-1 */
    keyboard_reset(&k);
    keyboard_move(&k, -1, 0);
    assert(k.cursor_col == KB_COLS - 1);

    /* up wraps from row 0 to row KB_ROWS-1 */
    keyboard_reset(&k);
    keyboard_move(&k, 0, -1);
    assert(k.cursor_row == KB_ROWS - 1);

    /* backspace on an empty buffer is a no-op (guard exercised) */
    keyboard_reset(&k);
    keyboard_backspace(&k);
    assert(k.input_len == 0 && k.input[0] == '\0');

    /* typing past the buffer cap stops at KB_INPUT_MAX-1, no overflow (guard exercised) */
    keyboard_reset(&k);
    for (int i = 0; i < KB_INPUT_MAX + 10; i++) keyboard_type(&k);
    assert(k.input_len == KB_INPUT_MAX - 1);
    assert(k.input[k.input_len] == '\0');

    /* submit */
    keyboard_submit(&k);
    assert(k.submitted == 1);

    printf("test_keyboard: OK\n");
    return 0;
}
