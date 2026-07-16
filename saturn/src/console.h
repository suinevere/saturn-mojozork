#ifndef CONSOLE_H
#define CONSOLE_H

#define CONSOLE_COLS 40
#define CONSOLE_MAX_LINES 128

#ifdef __cplusplus
extern "C" {
#endif

void console_init(void);
void console_write(const char *str, unsigned int len);
int  console_line_count(void);
/* Monotonic count of lines ever produced (never wraps with the ring). Use the
   delta between two calls to size a turn's output even after old lines evict. */
long console_total_lines(void);
const char *console_get_line(int index);

#ifdef __cplusplus
}
#endif
#endif /* CONSOLE_H */
