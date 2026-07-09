#ifndef TERM_H
#define TERM_H
#include "net/cui_transport.h"
#include "keyboard.h"

#define MOJOZORK_RX_BUDGET 512

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int iac_skip; } TermState;

void term_init(TermState *t);
int  term_service(TermState *t, const cui_transport_t *tr, int max_bytes);
void term_submit_line(const cui_transport_t *tr, KeyboardState *k);

#ifdef __cplusplus
}
#endif
#endif /* TERM_H */
