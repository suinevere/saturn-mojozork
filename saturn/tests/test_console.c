#include "../src/video/console.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

static void write_str(const char *s) { console_write(s, (unsigned int) strlen(s)); }

int main(void) {
    /* newline flushes a line */
    console_init();
    write_str("hello\n");
    assert(console_line_count() == 1);
    assert(strcmp(console_get_line(0), "hello") == 0);

    /* text without newline is the current partial line */
    console_init();
    write_str("west of house");
    assert(console_line_count() == 1);
    assert(strcmp(console_get_line(0), "west of house") == 0);

    /* hard-wrap a single long word at CONSOLE_COLS */
    console_init();
    { char buf[60]; memset(buf, 'a', 45); buf[45] = '\0'; write_str(buf); }
    assert(console_line_count() == 2);
    assert((int) strlen(console_get_line(0)) == CONSOLE_COLS);
    assert(strcmp(console_get_line(1), "aaaaa") == 0);   /* 45 - 40 */

    /* word-wrap at a space boundary, dropping the wrapping space */
    console_init();
    write_str("aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd eeee");
    /* 40-col width: "aaaaaaaaaa bbbbbbbbbb cccccccccc" is 32 chars, next word
       "dddddddddd" would reach 43 -> wrap before it. */
    assert(console_line_count() == 2);
    assert(strcmp(console_get_line(0), "aaaaaaaaaa bbbbbbbbbb cccccccccc") == 0);
    assert(strcmp(console_get_line(1), "dddddddddd eeee") == 0);

    /* ring-buffer eviction: write more than CONSOLE_MAX_LINES */
    console_init();
    {
        char buf[20];
        for (int i = 0; i < 200; i++) {
            snprintf(buf, sizeof(buf), "line%d\n", i);
            write_str(buf);
        }
    }
    assert(console_line_count() == CONSOLE_MAX_LINES);
    assert(strcmp(console_get_line(0), "line72") == 0);      /* oldest: 200 - 128 */
    assert(strcmp(console_get_line(127), "line199") == 0);   /* newest */

    printf("test_console: OK\n");
    return 0;
}
