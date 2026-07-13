/*----------------------
 | saturn_keyboard.cxx
 | Description: Reads the Saturn keyboard (an SMPC peripheral) and turns its raw
 |   scancode reports into SaturnKeyEvents: characters (with Shift/Caps applied),
 |   editing/navigation keys, and function keys, plus auto-repeat. Held modifiers
 |   (Ctrl, Shift) and the latched Caps/Num toggles are tracked across reports
 |   because the keyboard delivers one key event at a time. Caps/Num toggle the
 |   SHARED keyboard state so the physical key, the on-screen keyboard layer, and
 |   the Options page stay in sync.
 | Author: suinevere
 | Dependencies: SRL (Input::Management raw peripheral data), keyboard.h (shared
 |   Caps/Num state)
 ----------------------*/
#include <srl.hpp>
#include "saturn_keyboard.h"
extern "C" {
#include "keyboard.h"
}

/*----------------------
 | KBD_* report constants
 | Description: Offsets and codes for reading the keyboard's 24-byte peripheral
 |   report directly (so this file does not depend on the SGL struct being
 |   visible): id at byte 0 (KBD_ID = PER_ID_StnKeyBoard), status flags at byte 8
 |   (MAKE = key-down, BREAK = key-up), scancode at byte 9. The CODE_* values are
 |   the PS/2 set-2 scancodes for the modifier keys handled specially.
 | Author: suinevere
 ----------------------*/
#define KBD_ID          0x34
#define KBD_COND_MAKE   0x08
#define KBD_COND_BREAK  0x01
#define KBD_CODE_LCTRL  0x14
#define KBD_CODE_LSHIFT 0x12
#define KBD_CODE_RSHIFT 0x59
#define KBD_CODE_CAPS   0x58
#define KBD_CODE_NUM    0x77
#define KBD_OFF_ID      0
#define KBD_OFF_COND    8
#define KBD_OFF_CODE    9

/*----------------------
 | KBD_REPEAT_DELAY / KBD_REPEAT_RATE
 | Description: Auto-repeat timing in frames (~60Hz): the initial hold delay, then
 |   the repeat interval.
 | Author: suinevere
 ----------------------*/
#define KBD_REPEAT_DELAY 30
#define KBD_REPEAT_RATE  4

/*----------------------
 | kbd_map
 | Description: Scancode -> base (unshifted, lowercase) ASCII, US layout, adapted
 |   from Jo Engine's table. Enter (90/25) and Backspace (102) are absent here --
 |   they are emitted as key events, not characters.
 | Author: suinevere
 ----------------------*/
static const char kbd_map[128] = {
    /*  0 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 10 */ 0,   0,   0,   0,  '`',  0,   0,   0,   0,   0,
    /* 20 */ 0,  'q', '1',  0,   0,   0,  'z', 's', 'a', 'w',
    /* 30 */ '2', 0,   0,  'c', 'x', 'd', 'e', '4', '3',  0,
    /* 40 */ 0,  ' ', 'v', 'f', 't', 'r', '5',  0,   0,  'n',
    /* 50 */ 'b', 'h', 'g', 'y', '6',  0,   0,   0,  'm', 'j',
    /* 60 */ 'u', '7', '8',  0,   0,  ',', 'k', 'i', 'o', '0',
    /* 70 */ '9', 0,   0,  '.', '/', 'l', ';', 'p', '-',  0,
    /* 80 */ 0,   0,  '\'', 0,  '[', '=',  0,   0,   0,   0,
    /* 90 */ 0,  ']',  0,  '\\', 0,   0,   0,   0,   0,   0,
    /*100 */ 0,   0,   0,   0,   0,  '1',  0,  '4', '7',  0,
    /*110 */ 0,   0,  '0', '.', '2', '5', '6', '8',  0,   0,
    /*120 */ 0,  '+', '3', '-', '*', '9',  0,   0
};

/*----------------------
 | apply_mods
 | Description: Applies Shift/CapsLock to a base ASCII character (US layout).
 |   CapsLock affects letters only; Shift also selects the shifted symbol of the
 |   number/punctuation keys.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: c -- base character; shift -- Shift held; caps -- CapsLock on
 | Returns: the modified character
 ----------------------*/
static char apply_mods(char c, int shift, int caps) {
    if (c >= 'a' && c <= 'z') {
        return (shift ^ caps) ? (char)(c - 'a' + 'A') : c;
    }
    if (shift) {
        switch (c) {
            case '1': return '!'; case '2': return '@'; case '3': return '#';
            case '4': return '$'; case '5': return '%'; case '6': return '^';
            case '7': return '&'; case '8': return '*'; case '9': return '(';
            case '0': return ')'; case '`': return '~'; case '-': return '_';
            case '=': return '+'; case '[': return '{'; case ']': return '}';
            case '\\': return '|'; case ';': return ':'; case '\'': return '"';
            case ',': return '<'; case '.': return '>'; case '/': return '?';
        }
    }
    return c;
}

/*----------------------
 | is_numpad_digit
 | Description: True for the PS/2 set-2 scancodes of the numpad digit keys (and
 |   '.'), which are suppressed when NumLock is off.
 | Author: suinevere
 ----------------------*/
static int is_numpad_digit(uint8_t code) {
    switch (code) {
        case 105: case 107: case 108: case 112: case 113:
        case 114: case 115: case 116: case 117: case 122: case 125:
            return 1;
        default: return 0;
    }
}

/*----------------------
 | find_keyboard_port
 | Description: Scans the peripheral ports for one whose report id marks it a
 |   Saturn keyboard.
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: N/A
 | Params: N/A
 | Returns: the port index, or -1 if no keyboard is attached
 ----------------------*/
static int find_keyboard_port(void) {
    for (int p = 0; p < 12; p++) {
        const uint8_t *raw = (const uint8_t *) SRL::Input::Management::GetRawData(p);
        if (raw != nullptr && raw[KBD_OFF_ID] == KBD_ID) {
            return p;
        }
    }
    return -1;
}

/*----------------------
 | saturn_keyboard_present
 | Description: True when a Saturn keyboard is attached.
 | Author: suinevere
 ----------------------*/
extern "C" int saturn_keyboard_present(void) {
    return find_keyboard_port() >= 0 ? 1 : 0;
}

/*----------------------
 | saturn_keyboard_any_down
 | Description: True when the keyboard currently reports a key held (a make report
 |   with a nonzero scancode). Used by the online settle to detect residual input.
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: N/A
 | Params: N/A
 | Returns: 1 if a key is down, 0 otherwise
 ----------------------*/
extern "C" int saturn_keyboard_any_down(void) {
    int port = find_keyboard_port();
    if (port < 0) return 0;
    const uint8_t *raw = (const uint8_t *) SRL::Input::Management::GetRawData(port);
    return (raw[KBD_OFF_COND] & KBD_COND_MAKE) != 0 && raw[KBD_OFF_CODE] != 0;
}

/*----------------------
 | saturn_keyboard_poll
 | Description: Reads this frame's keyboard report and returns one SaturnKeyEvent.
 |   Modifier bookkeeping runs first (before any early-out) so break reports are
 |   seen: Ctrl and Shift are held-state flags tracked from their own make/break;
 |   Caps and Num are latched toggles that flip the shared keyboard state on their
 |   rising edge only (debounced). With no key down it clears the held-key latch.
 |   Otherwise it gates emission through auto-repeat (fresh press, or a repeat
 |   tick) and maps the scancode to an event: Enter/Tab/Backspace/Esc, the arrow
 |   keys (Ctrl+Left/Right distinguished for word motion), Insert/Delete, the
 |   nav-cluster and Page keys, the function keys F2..F12, Ctrl+C to clear the
 |   line, and finally a character (numpad digits suppressed when NumLock is off,
 |   Shift/Caps applied).
 | Author: suinevere
 | Dependencies: SRL (Input::Management), keyboard.h (shared Caps/Num state)
 | Globals: N/A (all persistent state is function-static)
 | Params: N/A
 | Returns: the decoded SaturnKeyEvent (SATURN_KEY_NONE when nothing to emit)
 ----------------------*/
extern "C" SaturnKeyEvent saturn_keyboard_poll(void) {
    static uint8_t last_code = 0;
    static int repeat_timer = 0;
    static int ctrl_down = 0;
    static int shift_down = 0;
    static int caps_held = 0;
    static int num_held  = 0;

    SaturnKeyEvent ev;
    ev.kind = SATURN_KEY_NONE;
    ev.ch = 0;

    int port = find_keyboard_port();
    if (port < 0) { last_code = 0; ctrl_down = 0; return ev; }

    const uint8_t *raw = (const uint8_t *) SRL::Input::Management::GetRawData(port);
    uint8_t cond = raw[KBD_OFF_COND];
    uint8_t code = raw[KBD_OFF_CODE];

    if (code == KBD_CODE_LCTRL) {
        if (cond & KBD_COND_MAKE)  ctrl_down = 1;
        if (cond & KBD_COND_BREAK) ctrl_down = 0;
    }
    if (code == KBD_CODE_LSHIFT || code == KBD_CODE_RSHIFT) {
        if (cond & KBD_COND_MAKE)  shift_down = 1;
        if (cond & KBD_COND_BREAK) shift_down = 0;
    }
    if (code == KBD_CODE_CAPS) {
        if (cond & KBD_COND_MAKE)  { if (!caps_held) { keyboard_set_caps(!keyboard_get_caps()); caps_held = 1; } }
        if (cond & KBD_COND_BREAK) { caps_held = 0; }
    }
    if (code == KBD_CODE_NUM) {
        if (cond & KBD_COND_MAKE)  { if (!num_held) { keyboard_set_num(!keyboard_get_num()); num_held = 1; } }
        if (cond & KBD_COND_BREAK) { num_held = 0; }
    }

    if ((cond & KBD_COND_MAKE) == 0 || code == 0) {
        last_code = 0;
        return ev;
    }

    int emit = 0;
    if (code != last_code) {
        last_code = code;
        repeat_timer = KBD_REPEAT_DELAY;
        emit = 1;
    } else if (--repeat_timer <= 0) {
        repeat_timer = KBD_REPEAT_RATE;
        emit = 1;
    }
    if (!emit) return ev;

    if (code == 90 || code == 25) { ev.kind = SATURN_KEY_ENTER; return ev; }
    if (code == 13)               { ev.kind = SATURN_KEY_TAB; return ev; }
    if (code == 102)              { ev.kind = SATURN_KEY_BACKSPACE; return ev; }
    if (code == 118)              { ev.kind = SATURN_KEY_ESCAPE; return ev; }
    if (code == 134)              { ev.kind = ctrl_down ? SATURN_KEY_CTRL_LEFT  : SATURN_KEY_LEFT;  return ev; }
    if (code == 141)              { ev.kind = ctrl_down ? SATURN_KEY_CTRL_RIGHT : SATURN_KEY_RIGHT; return ev; }
    if (code == 137)              { ev.kind = SATURN_KEY_UP;    return ev; }
    if (code == 138)              { ev.kind = SATURN_KEY_DOWN;  return ev; }
    if (code == 129)              { ev.kind = SATURN_KEY_INSERT;   return ev; }
    if (code == 133)              { ev.kind = SATURN_KEY_DELETE;   return ev; }
    if (code == 128)              { ev.kind = SATURN_KEY_CHAR; ev.ch = '/'; return ev; }
    if (code == 135)              { ev.kind = SATURN_KEY_HOME;     return ev; }
    if (code == 136)              { ev.kind = SATURN_KEY_END;      return ev; }
    if (code == 139)              { ev.kind = SATURN_KEY_PAGEUP;   return ev; }
    if (code == 140)              { ev.kind = SATURN_KEY_PAGEDOWN; return ev; }
    if (code == 6)                { ev.kind = SATURN_KEY_F2;  return ev; }
    if (code == 4)                { ev.kind = SATURN_KEY_F3;  return ev; }
    if (code == 3)                { ev.kind = SATURN_KEY_F5;  return ev; }
    if (code == 11)               { ev.kind = SATURN_KEY_F6;  return ev; }
    if (code == 1)                { ev.kind = SATURN_KEY_F9;  return ev; }
    if (code == 9)                { ev.kind = SATURN_KEY_F10; return ev; }
    if (code == 120)              { ev.kind = SATURN_KEY_F11; return ev; }
    if (code == 7)                { ev.kind = SATURN_KEY_F12; return ev; }
    if (ctrl_down && code < 128 && kbd_map[code] == 'c') { ev.kind = SATURN_KEY_CLEAR; return ev; }
    if (is_numpad_digit(code) && !keyboard_get_num()) return ev;
    if (code < 128 && kbd_map[code] != 0) {
        ev.kind = SATURN_KEY_CHAR;
        ev.ch = apply_mods(kbd_map[code], shift_down, keyboard_get_caps());
    }
    return ev;
}
