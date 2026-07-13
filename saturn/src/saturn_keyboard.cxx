#include <srl.hpp>
#include "saturn_keyboard.h"
extern "C" {
#include "keyboard.h"   // shared CapsLock / NumLock state (synced with on-screen kbd + Options)
}

// The Saturn keyboard is an SMPC peripheral. SRL copies its raw report into the
// per-port peripheral data (same 24-byte slot as a digital pad). We read the
// bytes we need directly so we don't depend on the SGL struct being visible:
//   byte 0 = peripheral id, byte 8 = cond (status flags), byte 9 = code (scancode)
#define KBD_ID          0x34   // PER_ID_StnKeyBoard
#define KBD_COND_MAKE   0x08   // PER_KBD_MK (key-down this report)
#define KBD_COND_BREAK  0x01   // PER_KBD_BR (key-up this report)
#define KBD_CODE_LCTRL  0x14   // Left Ctrl scancode (unmapped in kbd_map)
#define KBD_CODE_LSHIFT 0x12   // Left Shift  (PS/2 set 2)
#define KBD_CODE_RSHIFT 0x59   // Right Shift
#define KBD_CODE_CAPS   0x58   // CapsLock
#define KBD_CODE_NUM    0x77   // NumLock (PS/2 set 2)
#define KBD_OFF_ID      0
#define KBD_OFF_COND    8
#define KBD_OFF_CODE    9

// Auto-repeat timing, in frames (~60Hz): initial hold delay, then repeat interval.
#define KBD_REPEAT_DELAY 30
#define KBD_REPEAT_RATE  4

// Scancode -> ASCII (Saturn keyboard, US layout). Adapted from Jo Engine's table.
// Enter (90/25) and Backspace (102) are handled separately as key events.
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
    /*120 */ 0,  '+', '3', '-', '*', '9',  0,   0   // 121/123/124 = numpad + - *
};

// Apply Shift/CapsLock to a base (unshifted, lowercase) ASCII character, US
// layout. CapsLock affects letters only; Shift also selects the shifted symbol
// of the number/punctuation keys.
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

// PS/2 set-2 scancodes for the numpad DIGIT keys (and '.'), gated by NumLock.
static int is_numpad_digit(uint8_t code) {
    switch (code) {
        case 105: case 107: case 108: case 112: case 113:
        case 114: case 115: case 116: case 117: case 122: case 125:
            return 1;
        default: return 0;
    }
}

static int find_keyboard_port(void) {
    for (int p = 0; p < 12; p++) {
        const uint8_t *raw = (const uint8_t *) SRL::Input::Management::GetRawData(p);
        if (raw != nullptr && raw[KBD_OFF_ID] == KBD_ID) {
            return p;
        }
    }
    return -1;
}

extern "C" int saturn_keyboard_present(void) {
    return find_keyboard_port() >= 0 ? 1 : 0;
}

extern "C" int saturn_keyboard_any_down(void) {
    int port = find_keyboard_port();
    if (port < 0) return 0;
    const uint8_t *raw = (const uint8_t *) SRL::Input::Management::GetRawData(port);
    return (raw[KBD_OFF_COND] & KBD_COND_MAKE) != 0 && raw[KBD_OFF_CODE] != 0;
}

extern "C" SaturnKeyEvent saturn_keyboard_poll(void) {
    static uint8_t last_code = 0;
    static int repeat_timer = 0;
    static int ctrl_down = 0;   // Ctrl is a held modifier tracked across reports
    static int shift_down = 0;  // Shift is held; tracked from its own make/break
    static int caps_held = 0;   // debounce: only toggle Caps on its rising edge
    static int num_held  = 0;   // debounce: only toggle Num on its rising edge

    SaturnKeyEvent ev;
    ev.kind = SATURN_KEY_NONE;
    ev.ch = 0;

    int port = find_keyboard_port();
    if (port < 0) { last_code = 0; ctrl_down = 0; return ev; }

    const uint8_t *raw = (const uint8_t *) SRL::Input::Management::GetRawData(port);
    uint8_t cond = raw[KBD_OFF_COND];
    uint8_t code = raw[KBD_OFF_CODE];

    // The keyboard delivers one key event at a time, so a Ctrl+key chord can't be
    // read in a single report. Instead track Ctrl's held state from its own make/
    // break events (which arrive as distinct reports) and consult it when another
    // key is pressed. Done before the early-out below so we still catch Ctrl's
    // break report (which has MAKE clear).
    if (code == KBD_CODE_LCTRL) {
        if (cond & KBD_COND_MAKE)  ctrl_down = 1;
        if (cond & KBD_COND_BREAK) ctrl_down = 0;
    }
    // Shift: same held-modifier tracking as Ctrl (both handled before the early
    // out so we still see the break report, which has MAKE clear).
    if (code == KBD_CODE_LSHIFT || code == KBD_CODE_RSHIFT) {
        if (cond & KBD_COND_MAKE)  shift_down = 1;
        if (cond & KBD_COND_BREAK) shift_down = 0;
    }
    // CapsLock: latched toggle on the SHARED keyboard state, so the physical key,
    // the on-screen keyboard layer, and the Options page all stay in sync.
    if (code == KBD_CODE_CAPS) {
        if (cond & KBD_COND_MAKE)  { if (!caps_held) { keyboard_set_caps(!keyboard_get_caps()); caps_held = 1; } }
        if (cond & KBD_COND_BREAK) { caps_held = 0; }
    }
    // NumLock: latched toggle on the shared numpad state (same sync).
    if (code == KBD_CODE_NUM) {
        if (cond & KBD_COND_MAKE)  { if (!num_held) { keyboard_set_num(!keyboard_get_num()); num_held = 1; } }
        if (cond & KBD_COND_BREAK) { num_held = 0; }
    }

    // No key currently down: clear the held-key state.
    if ((cond & KBD_COND_MAKE) == 0 || code == 0) {
        last_code = 0;
        return ev;
    }

    // Decide whether to emit this frame: on a fresh press, or on a repeat tick.
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
    if (code == 13)               { ev.kind = SATURN_KEY_TAB; return ev; }       // Tab (PS/2 set 2)
    if (code == 102)              { ev.kind = SATURN_KEY_BACKSPACE; return ev; }
    if (code == 118)              { ev.kind = SATURN_KEY_ESCAPE; return ev; }  // Esc
    if (code == 134)              { ev.kind = ctrl_down ? SATURN_KEY_CTRL_LEFT  : SATURN_KEY_LEFT;  return ev; }
    if (code == 141)              { ev.kind = ctrl_down ? SATURN_KEY_CTRL_RIGHT : SATURN_KEY_RIGHT; return ev; }
    if (code == 137)              { ev.kind = SATURN_KEY_UP;    return ev; }
    if (code == 138)              { ev.kind = SATURN_KEY_DOWN;  return ev; }
    if (code == 129)              { ev.kind = SATURN_KEY_INSERT;   return ev; }  // Insert
    if (code == 133)              { ev.kind = SATURN_KEY_DELETE;   return ev; }  // Delete
    if (code == 128)              { ev.kind = SATURN_KEY_CHAR; ev.ch = '/'; return ev; }  // numpad /
    if (code == 135)              { ev.kind = SATURN_KEY_HOME;     return ev; }  // nav cluster
    if (code == 136)              { ev.kind = SATURN_KEY_END;      return ev; }
    if (code == 139)              { ev.kind = SATURN_KEY_PAGEUP;   return ev; }
    if (code == 140)              { ev.kind = SATURN_KEY_PAGEDOWN; return ev; }
    // Ctrl+C: clear the input line (checked before the char map so it doesn't type 'c').
    if (ctrl_down && code < 128 && kbd_map[code] == 'c') { ev.kind = SATURN_KEY_CLEAR; return ev; }
    if (is_numpad_digit(code) && !keyboard_get_num()) return ev;   // NumLock off: suppress numpad digits
    if (code < 128 && kbd_map[code] != 0) {
        ev.kind = SATURN_KEY_CHAR;
        ev.ch = apply_mods(kbd_map[code], shift_down, keyboard_get_caps());
    }
    return ev;
}
