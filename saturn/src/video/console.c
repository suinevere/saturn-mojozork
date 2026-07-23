/*----------------------
 | console.c
 | Description: The text-console model behind the game view: a fixed-size ring of
 |   completed lines plus one in-progress line, with word-wrap at the column limit
 |   and a monotonic total-lines counter that survives eviction (so the scrollback
 |   view can measure a turn's output even after old lines roll off). Pure model --
 |   console_view.cxx renders it.
 | Author: suinevere
 | Dependencies: console.h (CONSOLE_MAX_LINES/CONSOLE_COLS), string.h
 ----------------------*/
#include "console.h"
#include <string.h>

/*----------------------
 | lines / head / count / cur / curlen / total_ever
 | Description: The scrollback ring: `lines` holds completed rows with `head` the
 |   oldest and `count` how many are stored; `cur`/`curlen` accumulate the
 |   in-progress line; `total_ever` counts every completed line ever pushed and is
 |   never decremented by eviction (the scroll math keys off it).
 | Author: suinevere
 ----------------------*/
static char lines[CONSOLE_MAX_LINES][CONSOLE_COLS + 1];
static int  head;
static int  count;
static char cur[CONSOLE_COLS + 1];
static int  curlen;
static long total_ever;

/*----------------------
 | push_line
 | Description: Appends a completed line to the ring, truncating to the column
 |   limit, overwriting the oldest entry once the ring is full, and bumping
 |   total_ever.
 | Author: suinevere
 | Dependencies: string.h
 | Globals: lines, head, count, total_ever
 | Params: s -- the line text; len -- its length
 | Returns: N/A
 ----------------------*/
static void push_line(const char *s, int len) {
    int slot;
    if (len > CONSOLE_COLS) len = CONSOLE_COLS;
    slot = (head + count) % CONSOLE_MAX_LINES;
    memcpy(lines[slot], s, (size_t) len);
    lines[slot][len] = '\0';
    if (count < CONSOLE_MAX_LINES) count++;
    else head = (head + 1) % CONSOLE_MAX_LINES;
    total_ever++;
}

/*----------------------
 | flush_cur
 | Description: Commits the in-progress line to the ring and clears it (a newline
 |   or a wrap).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: cur, curlen
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void flush_cur(void) {
    push_line(cur, curlen);
    curlen = 0;
    cur[0] = '\0';
}

/*----------------------
 | console_init
 | Description: Resets the console to empty.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: head, count, curlen, cur, total_ever
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void console_init(void) {
    head = 0; count = 0; curlen = 0; cur[0] = '\0'; total_ever = 0;
}

/*----------------------
 | console_write
 | Description: Appends text, honoring \n (flush) and dropping \r, and word-wraps
 |   at CONSOLE_COLS: a space at the limit flushes; a non-space at the limit breaks
 |   at the last space (carrying the trailing word onto the next line) or hard-wraps
 |   when the word is longer than a line.
 | Author: suinevere
 | Dependencies: string.h
 | Globals: cur, curlen
 | Params: str -- text to append; len -- its length
 | Returns: N/A
 ----------------------*/
void console_write(const char *str, unsigned int len) {
    unsigned int i;
    for (i = 0; i < len; i++) {
        char c = str[i];
        if (c == '\r') continue;
        if (c == '\n') { flush_cur(); continue; }
        if (c == ' ' && curlen >= CONSOLE_COLS) { flush_cur(); continue; }
        if (c != ' ' && curlen >= CONSOLE_COLS) {
            int sp = -1, j;
            for (j = curlen - 1; j >= 0; j--) { if (cur[j] == ' ') { sp = j; break; } }
            if (sp > 0) {
                int carry = curlen - (sp + 1);
                push_line(cur, sp);
                memmove(cur, cur + sp + 1, (size_t) carry);
                curlen = carry;
                cur[curlen] = '\0';
            } else {
                flush_cur();
            }
        }
        cur[curlen++] = c;
        cur[curlen] = '\0';
    }
}

/*----------------------
 | console_line_count
 | Description: The number of currently retained lines, including the in-progress
 |   line if non-empty.
 | Author: suinevere
 ----------------------*/
int console_line_count(void) {
    return count + (curlen > 0 ? 1 : 0);
}

/*----------------------
 | console_total_lines
 | Description: The monotonic count of lines ever produced (plus the in-progress
 |   line), used by the scroll view to measure a turn's output across eviction.
 | Author: suinevere
 ----------------------*/
long console_total_lines(void) {
    return total_ever + (curlen > 0 ? 1 : 0);
}

/*----------------------
 | console_get_line
 | Description: Returns a retained line by index (0 = oldest); an index at count
 |   returns the in-progress line, which is always last.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: lines, head, count, cur
 | Params: index -- 0-based line index
 | Returns: a pointer to the line's text
 ----------------------*/
const char *console_get_line(int index) {
    if (index < count) return lines[(head + index) % CONSOLE_MAX_LINES];
    return cur;
}
