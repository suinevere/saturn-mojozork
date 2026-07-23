/*----------------------
 | console.h
 | Description: The text-console model: a wrapped scrollback ring plus an
 |   in-progress line, with a monotonic total-lines counter that survives eviction.
 |   Implemented in console.c; rendered by console_view.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef CONSOLE_H
#define CONSOLE_H

/*----------------------
 | CONSOLE_COLS / CONSOLE_MAX_LINES
 | Description: The console width in columns (the word-wrap limit) and the
 |   scrollback ring capacity.
 | Author: suinevere
 ----------------------*/
#define CONSOLE_COLS 40
#define CONSOLE_MAX_LINES 128

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | console_init / console_write / console_line_count / console_total_lines /
 | console_get_line
 | Description: init clears the console; write appends text (honoring \n, dropping
 |   \r, word-wrapping at CONSOLE_COLS); line_count is the currently retained line
 |   count; total_lines is the monotonic count of lines ever produced (never wraps
 |   with the ring, so the delta between two calls sizes a turn's output even after
 |   eviction); get_line returns a retained line by index (0 = oldest).
 | Author: suinevere
 ----------------------*/
void console_init(void);
void console_write(const char *str, unsigned int len);
int  console_line_count(void);
long console_total_lines(void);
const char *console_get_line(int index);

#ifdef __cplusplus
}
#endif
#endif /* CONSOLE_H */
