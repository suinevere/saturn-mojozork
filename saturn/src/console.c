#include "console.h"
#include <string.h>

static char lines[CONSOLE_MAX_LINES][CONSOLE_COLS + 1];
static int  head;    /* index of oldest stored line */
static int  count;   /* number of completed lines in the ring */
static char cur[CONSOLE_COLS + 1];
static int  curlen;
static long total_ever;   /* completed lines ever pushed; never decremented by eviction */

static void push_line(const char *s, int len) {
    int slot;
    if (len > CONSOLE_COLS) len = CONSOLE_COLS;
    slot = (head + count) % CONSOLE_MAX_LINES;
    memcpy(lines[slot], s, (size_t) len);
    lines[slot][len] = '\0';
    if (count < CONSOLE_MAX_LINES) count++;
    else head = (head + 1) % CONSOLE_MAX_LINES;   /* overwrite oldest */
    total_ever++;
}

static void flush_cur(void) {
    push_line(cur, curlen);
    curlen = 0;
    cur[0] = '\0';
}

void console_init(void) {
    head = 0; count = 0; curlen = 0; cur[0] = '\0'; total_ever = 0;
}

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
                push_line(cur, sp);                 /* line before the space */
                memmove(cur, cur + sp + 1, (size_t) carry);
                curlen = carry;
                cur[curlen] = '\0';
            } else {
                flush_cur();                        /* no space: hard wrap */
            }
        }
        cur[curlen++] = c;
        cur[curlen] = '\0';
    }
}

int console_line_count(void) {
    return count + (curlen > 0 ? 1 : 0);
}

long console_total_lines(void) {
    return total_ever + (curlen > 0 ? 1 : 0);
}

const char *console_get_line(int index) {
    if (index < count) return lines[(head + index) % CONSOLE_MAX_LINES];
    return cur;   /* in-progress line is the last */
}
