/*----------------------
 | saturn_keyboard.h
 | Description: The Saturn keyboard interface: the decoded key-event types and the
 |   per-frame poll (with edge detection and auto-repeat), plus presence and
 |   any-key-down queries. Implemented in saturn_keyboard.cxx.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef SATURN_KEYBOARD_H
#define SATURN_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SaturnKeyKind
 | Description: The kind of a decoded key event: NONE, a printable CHAR (in .ch),
 |   the editing/navigation keys, TAB (accept completion), CLEAR (Ctrl+C),
 |   CTRL_LEFT/RIGHT (word caret), INSERT (toggle arrow roles), DELETE, and the
 |   mapped function keys F2..F12 (F1/F4/F7/F8 have no role and stay unreported).
 | Author: suinevere
 ----------------------*/
typedef enum {
    SATURN_KEY_NONE = 0,
    SATURN_KEY_CHAR,        /* a printable character is in .ch */
    SATURN_KEY_BACKSPACE,
    SATURN_KEY_ENTER,
    SATURN_KEY_ESCAPE,
    SATURN_KEY_LEFT,
    SATURN_KEY_RIGHT,
    SATURN_KEY_UP,
    SATURN_KEY_DOWN,
    SATURN_KEY_HOME,
    SATURN_KEY_END,
    SATURN_KEY_PAGEUP,
    SATURN_KEY_PAGEDOWN,
    SATURN_KEY_TAB,         /* Tab: accept the typeahead completion */
    SATURN_KEY_CLEAR,       /* Ctrl+C: clear the current input line */
    SATURN_KEY_CTRL_LEFT,   /* Ctrl+Left: move the text caret left */
    SATURN_KEY_CTRL_RIGHT,  /* Ctrl+Right: move the text caret right */
    SATURN_KEY_INSERT,      /* Insert: toggle arrow-key vs Ctrl+arrow roles */
    SATURN_KEY_DELETE,      /* Delete: remove the character at the caret */
    /* Function keys, in the order the game uses them. Only the mapped ones are
       here -- F1/F4/F7/F8 have no role, so they stay unreported. */
    SATURN_KEY_F2,
    SATURN_KEY_F3,
    SATURN_KEY_F5,
    SATURN_KEY_F6,
    SATURN_KEY_F9,
    SATURN_KEY_F10,
    SATURN_KEY_F11,
    SATURN_KEY_F12
} SaturnKeyKind;

/*----------------------
 | SaturnKeyEvent
 | Description: One decoded key event: its kind, plus the character in .ch when
 |   kind == SATURN_KEY_CHAR.
 | Author: suinevere
 ----------------------*/
typedef struct {
    SaturnKeyKind kind;
    char ch;                /* valid when kind == SATURN_KEY_CHAR */
} SaturnKeyEvent;

/*----------------------
 | saturn_keyboard_present / saturn_keyboard_any_down / saturn_keyboard_poll
 | Description: present is 1 if a keyboard is detected on any port; any_down is 1
 |   while any key is physically held (raw MAKE flag, so callers can wait for
 |   release); poll (once per frame) returns the next key event or NONE, with edge
 |   detection and auto-repeat.
 | Author: suinevere
 ----------------------*/
int saturn_keyboard_present(void);
int saturn_keyboard_any_down(void);
SaturnKeyEvent saturn_keyboard_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_KEYBOARD_H */
