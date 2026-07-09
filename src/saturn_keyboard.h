#ifndef SATURN_KEYBOARD_H
#define SATURN_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SATURN_KEY_NONE = 0,
    SATURN_KEY_CHAR,        /* a printable character is in .ch */
    SATURN_KEY_BACKSPACE,
    SATURN_KEY_ENTER,
    SATURN_KEY_ESCAPE,
    SATURN_KEY_LEFT,
    SATURN_KEY_RIGHT,
    SATURN_KEY_UP,
    SATURN_KEY_DOWN
} SaturnKeyKind;

typedef struct {
    SaturnKeyKind kind;
    char ch;                /* valid when kind == SATURN_KEY_CHAR */
} SaturnKeyEvent;

/* 1 if a Saturn keyboard peripheral is currently detected on any port. */
int saturn_keyboard_present(void);

/* 1 if any keyboard key is physically held down right now (raw MAKE flag).
   Lets callers wait for the player to release keys before reading input. */
int saturn_keyboard_any_down(void);

/* Call once per frame. Returns a key event (or NONE), with edge detection and
   auto-repeat: a key emits immediately when pressed, pauses, then repeats while
   held. */
SaturnKeyEvent saturn_keyboard_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_KEYBOARD_H */
