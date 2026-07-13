/*----------------------
 | term.h
 | Description: A minimal telnet terminal over a cui_transport: drain received
 |   bytes to the console and send a typed line with its newline. Implemented in
 |   term.c.
 | Author: suinevere
 | Dependencies: cui_transport.h (the link), keyboard.h (the input line)
 ----------------------*/
#ifndef TERM_H
#define TERM_H
#include "net/cui_transport.h"
#include "keyboard.h"

/*----------------------
 | ZATURN_RX_BUDGET
 | Description: Max bytes term_service drains per call, so a flood cannot starve
 |   the rest of the frame loop.
 | Author: suinevere
 ----------------------*/
#define ZATURN_RX_BUDGET 512

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | TermState
 | Description: Terminal state -- the count of bytes still to swallow after a
 |   telnet IAC command.
 | Author: suinevere
 ----------------------*/
typedef struct { int iac_skip; } TermState;

/*----------------------
 | term_init / term_service / term_submit_line
 | Description: init clears the state; service drains up to max_bytes to the
 |   console (returning the count consumed); submit_line echoes and sends the
 |   keyboard's line with a newline, then resets it.
 | Author: suinevere
 ----------------------*/
void term_init(TermState *t);
int  term_service(TermState *t, const cui_transport_t *tr, int max_bytes);
void term_submit_line(const cui_transport_t *tr, KeyboardState *k);

#ifdef __cplusplus
}
#endif
#endif /* TERM_H */
