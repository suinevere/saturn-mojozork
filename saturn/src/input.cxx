/*----------------------
 | input.cxx
 | Description: Implements the controller mapping tables, the MultiPad/g_pad
 |   aggregate pad declared in input.h, gamepad auto-repeat and shift-chord
 |   hold-repeat, gamepad- and keyboard-driven console scrollback, and shell-style
 |   command history recall.
 | Author: suinevere
 | Dependencies: input.h
 ----------------------*/

#include "input.h"

MultiPad *g_pad = nullptr;

int g_face_btn[FA_N]   = { 0, 1, 2 };
int g_chord_slot[CA_N] = { SL_LR, SL_ZUD, SL_YLRd, SL_YUD, SL_ZLRt, SL_YLRt };

static const int FACE_DEFAULT[FA_N]  = { 0, 1, 2 };
static const int CHORD_DEFAULT[CA_N] = { SL_LR, SL_ZUD, SL_YLRd, SL_YUD, SL_ZLRt, SL_YLRt };

static const int SCROLL_PAGE = 16;
static const int SCROLL_ALL  = 1 << 30;

/*----------------------
 | scroll_handle_key
 | Description: A switch over the physical-keyboard nav keys, translating each
 |   into a g_scroll delta or absolute value. Left/Right are matched but left a
 |   no-op: they used to move the on-screen keyboard cursor and are now consumed
 |   here so they don't fall through and get typed as text.
 | Author: suinevere
 | Dependencies: app_state.h, saturn_keyboard.h
 | Globals: g_scroll
 | Params: ke -- the keyboard event to test
 | Returns: true if `ke` was a nav key (and thus consumed); false otherwise
 ----------------------*/
bool scroll_handle_key(const SaturnKeyEvent &ke) {
    switch (ke.kind) {
        case SATURN_KEY_UP:       g_scroll += 1;           return true;
        case SATURN_KEY_DOWN:     g_scroll -= 1;           return true;
        case SATURN_KEY_PAGEUP:   g_scroll += SCROLL_PAGE; return true;
        case SATURN_KEY_PAGEDOWN: g_scroll -= SCROLL_PAGE; return true;
        case SATURN_KEY_HOME:     g_scroll  = SCROLL_ALL;  return true;
        case SATURN_KEY_END:      g_scroll  = 0;           return true;
        case SATURN_KEY_LEFT:
        case SATURN_KEY_RIGHT:                             return true;
        default:                                           return false;
    }
}

static const int PAD_SCROLL_DELAY = 30;
static const int PAD_SCROLL_RATE  = 4;

/*----------------------
 | face_button
 | Description: Indexes a fixed {A,B,C} table by the currently-mapped button
 |   number for face-action `action`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: action -- one of FA_ACCEPT/FA_BACK/FA_TYPE
 | Returns: the Button currently assigned to that action
 ----------------------*/
Button face_button(int action) {
    static const Button BTN[3] = { Button::A, Button::B, Button::C };
    return BTN[g_face_btn[action]];
}

/*----------------------
 | face_btn_name
 | Description: Indexes a fixed name table by the currently-mapped button number
 |   for face-action `action`. SRL::Debug::Print has no width flags, so callers
 |   align columns by printing the returned value at a fixed x rather than
 |   padding here.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: action -- one of FA_ACCEPT/FA_BACK/FA_TYPE
 | Returns: "A", "B", or "C"
 ----------------------*/
const char *face_btn_name(int action) {
    static const char *N[3] = { "A", "B", "C" };
    return N[g_face_btn[action]];
}

/*----------------------
 | slot_name
 | Description: Indexes a fixed display-string table by slot.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- one of the SL_* slot constants
 | Returns: the slot's display string
 ----------------------*/
const char *slot_name(int slot) {
    static const char *N[SL_N] = { "L/R", "Z+Up/Dn", "Z+L/R", "Z+Left/Right",
                                   "Y+Up/Dn", "Y+Left/Right", "Y+L/R" };
    return N[slot];
}

/*----------------------
 | slot_raw
 | Description: Reads g_pad directly for the four modifier/direction pairs (Z, Y,
 |   L/R, D-pad) and switches on `slot` to return its raw held direction this
 |   frame. Trigger slots (the "t" suffix) return 0 when both L+R are held, since
 |   that combo is reserved for the caps toggle; the plain L/R slot returns 0
 |   under any shift (Z or Y) so it never collides with the shifted trigger
 |   slots. Only called from chord_tick, so it stays file-local.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: slot -- one of the SL_* slot constants
 | Returns: -1 (L/Up/Left), +1 (R/Down/Right), or 0 (idle / shift mismatch)
 ----------------------*/
static int slot_raw(int slot) {
    bool z = g_pad->IsHeld(Button::Z), y = g_pad->IsHeld(Button::Y);
    bool l = g_pad->IsHeld(Button::L), r = g_pad->IsHeld(Button::R);
    bool up = g_pad->IsHeld(Button::Up),   dn = g_pad->IsHeld(Button::Down);
    bool lt = g_pad->IsHeld(Button::Left), rt = g_pad->IsHeld(Button::Right);
    switch (slot) {
        case SL_LR:   if (z || y || (l && r)) return 0; return l ? -1 : r ? 1 : 0;
        case SL_ZUD:  if (!z) return 0;                 return up ? -1 : dn ? 1 : 0;
        case SL_ZLRt: if (!z || (l && r)) return 0;     return l ? -1 : r ? 1 : 0;
        case SL_ZLRd: if (!z) return 0;                 return lt ? -1 : rt ? 1 : 0;
        case SL_YUD:  if (!y) return 0;                 return up ? -1 : dn ? 1 : 0;
        case SL_YLRd: if (!y) return 0;                 return lt ? -1 : rt ? 1 : 0;
        case SL_YLRt: if (!y || (l && r)) return 0;     return l ? -1 : r ? 1 : 0;
    }
    return 0;
}

/*----------------------
 | caps_combo_fired
 | Description: Latches held state across calls in a static `was`, comparing it
 |   to the current L+R-without-shift read so only the rising edge reports true.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: true on the frame the combo first becomes held
 ----------------------*/
bool caps_combo_fired(void) {
    static bool was = false;
    bool now = g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R)
             && !g_pad->IsHeld(Button::Z) && !g_pad->IsHeld(Button::Y);
    bool fired = now && !was;
    was = now;
    return fired;
}

// Per-slot edge + hold-repeat state, ticked once per input frame by chord_tick.
struct ChordRep { int dir; int timer; bool fired; };
static ChordRep g_chordrep[SL_N];

/*----------------------
 | chord_tick
 | Description: For each of the SL_N slots, reads slot_raw and compares it to the
 |   slot's stored direction: idle resets the timer and clears fired; a changed
 |   direction fires immediately and arms the PAD_SCROLL_DELAY timer; an
 |   unchanged held direction fires again each time the timer counts down to 0,
 |   then rearms at the faster PAD_SCROLL_RATE.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void chord_tick(void) {
    for (int s = 0; s < SL_N; s++) {
        int d = slot_raw(s);
        ChordRep &r = g_chordrep[s];
        if (d == 0)              { r.dir = 0; r.timer = 0; r.fired = false; }
        else if (d != r.dir)     { r.dir = d; r.timer = PAD_SCROLL_DELAY; r.fired = true; }
        else if (--r.timer <= 0) { r.timer = PAD_SCROLL_RATE; r.fired = true; }
        else                     { r.fired = false; }
    }
}

/*----------------------
 | chord_fired
 | Description: Looks up the slot currently mapped to `action` and checks
 |   chord_tick's per-slot fired flag and direction against `dir`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_chord_slot
 | Params: action -- one of the CA_* constants; dir -- -1 or +1
 | Returns: true if that action's slot fired in that direction this frame
 ----------------------*/
bool chord_fired(int action, int dir) {
    const ChordRep &r = g_chordrep[g_chord_slot[action]];
    return r.fired && r.dir == dir;
}

/*----------------------
 | pad_scroll_update
 | Description: Six chord_fired checks (Line +/-, Home/End, Page +/-) against the
 |   CA_LINE/CA_HOMEEND/CA_PAGE actions, each adjusting g_scroll. Depends on
 |   chord_tick having run this frame; the on-screen keyboard cursor moves on the
 |   plain D-pad (no shift) elsewhere, in the typeahead-editing input handler.
 | Author: suinevere
 | Dependencies: app_state.h
 | Globals: g_scroll
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void pad_scroll_update(void) {
    if (chord_fired(CA_LINE,    -1)) g_scroll += 1;
    if (chord_fired(CA_LINE,    +1)) g_scroll -= 1;
    if (chord_fired(CA_HOMEEND, -1)) g_scroll  = SCROLL_ALL;
    if (chord_fired(CA_HOMEEND, +1)) g_scroll  = 0;
    if (chord_fired(CA_PAGE,    -1)) g_scroll += SCROLL_PAGE;
    if (chord_fired(CA_PAGE,    +1)) g_scroll -= SCROLL_PAGE;
}

// The editing buttons (D-pad, C, B, Y, L, R) repeat while held, like a real
// keyboard. pad_repeat_update ticks all their timers once per frame; pad_fired
// then reports the initial press plus each repeat tick.
#define PAD_REPEAT_DELAY 30
#define PAD_REPEAT_RATE  4

struct PadRepeat { Button btn; int timer; bool fired; };
static PadRepeat g_padrep[] = {
    { Button::Up, 0, false }, { Button::Down, 0, false },
    { Button::Left, 0, false }, { Button::Right, 0, false },
    { Button::L, 0, false }, { Button::R, 0, false },
    { Button::A, 0, false }, { Button::C, 0, false },
    { Button::B, 0, false }, { Button::X, 0, false },
};

/*----------------------
 | pad_repeat_update
 | Description: For each tracked button: released resets its timer and clears
 |   fired; first held frame fires immediately and arms PAD_REPEAT_DELAY; held
 |   frames after that fire again each time the timer counts down to 0, then
 |   rearm at the faster PAD_REPEAT_RATE.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void pad_repeat_update(void) {
    for (auto &r : g_padrep) {
        if (!g_pad->IsHeld(r.btn))      { r.timer = 0; r.fired = false; }
        else if (r.timer == 0)          { r.fired = true;  r.timer = PAD_REPEAT_DELAY; }
        else if (--r.timer <= 0)        { r.fired = true;  r.timer = PAD_REPEAT_RATE; }
        else                            { r.fired = false; }
    }
}

/*----------------------
 | pad_fired
 | Description: Looks `b` up in g_padrep; if tracked, returns its repeat-aware
 |   fired flag, otherwise falls back to a plain WasPressed edge.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: b -- the button to check
 | Returns: true if it fired (pressed or repeated) this frame
 ----------------------*/
bool pad_fired(Button b) {
    for (auto &r : g_padrep) if (r.btn == b) return r.fired;
    return g_pad->WasPressed(b);
}

// Up/Down recall previously entered commands into the input line (shell-style).
#define HISTORY_MAX 16
static char g_history[HISTORY_MAX][KB_INPUT_MAX];
static int  g_hist_count  = 0;
static int  g_hist_head   = 0;
static int  g_hist_browse = -1;

/*----------------------
 | history_push
 | Description: Resets browsing to -1 first (so the next Up starts from the
 |   newest entry). Returns early on a blank line, or on a line that matches the
 |   most-recently-stored entry character-for-character (dedupes consecutive
 |   repeats). Otherwise copies it into the ring buffer at g_hist_head and
 |   advances the head, growing g_hist_count up to HISTORY_MAX.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- the command line just submitted
 | Returns: N/A
 ----------------------*/
void history_push(const char *s) {
    g_hist_browse = -1;
    if (s == nullptr || s[0] == '\0') return;
    if (g_hist_count > 0) {
        int last = (g_hist_head - 1 + HISTORY_MAX) % HISTORY_MAX;
        int i = 0; while (s[i] && g_history[last][i] && s[i] == g_history[last][i]) i++;
        if (s[i] == '\0' && g_history[last][i] == '\0') return;
    }
    int n = 0; while (s[n] && n < KB_INPUT_MAX - 1) { g_history[g_hist_head][n] = s[n]; n++; }
    g_history[g_hist_head][n] = '\0';
    g_hist_head = (g_hist_head + 1) % HISTORY_MAX;
    if (g_hist_count < HISTORY_MAX) g_hist_count++;
}

/*----------------------
 | history_load
 | Description: Computes the ring-buffer index g_hist_browse steps back from the
 |   newest entry (mod-wrapped through HISTORY_MAX*2 to stay positive) and copies
 |   that string into k->input, placing the caret at its end.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard state to overwrite
 | Returns: N/A
 ----------------------*/
void history_load(KeyboardState *k) {
    int idx = (g_hist_head - 1 - g_hist_browse + HISTORY_MAX * 2) % HISTORY_MAX;
    const char *s = g_history[idx];
    int n = 0; while (s[n] && n < KB_INPUT_MAX - 1) { k->input[n] = s[n]; n++; }
    k->input[n] = '\0';
    k->input_len = n;
    k->cursor = n;
}

/*----------------------
 | history_recall
 | Description: older != 0 steps the browse offset one further back (toward
 |   older commands) if more remain, then loads it. older == 0 steps one closer
 |   to the newest; once browsing hits -1 (nothing older than the newest is
 |   selected), it clears the input line instead of loading anything. No-op when
 |   history is empty.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard state to update; older -- nonzero for older, zero for newer
 | Returns: N/A
 ----------------------*/
void history_recall(KeyboardState *k, int older) {
    if (g_hist_count == 0) return;
    if (older) {
        if (g_hist_browse < g_hist_count - 1) { g_hist_browse++; history_load(k); }
    } else {
        if (g_hist_browse > 0) { g_hist_browse--; history_load(k); }
        else { g_hist_browse = -1; k->input_len = 0; k->input[0] = '\0'; k->cursor = 0; }
    }
}

/*----------------------
 | face_assign
 | Description: Scans the other two face actions for one that currently holds
 |   button `b`; if found, gives it `a`'s previous button (the swap), then sets
 |   `a` to `b`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: a -- the face action being reassigned; b -- the button (0=A,1=B,2=C) to give it
 | Returns: N/A
 ----------------------*/
void face_assign(int a, int b) {
    for (int o = 0; o < FA_N; o++) if (o != a && g_face_btn[o] == b) g_face_btn[o] = g_face_btn[a];
    g_face_btn[a] = b;
}

/*----------------------
 | chord_assign
 | Description: Scans the other chord actions for one that currently holds slot
 |   `s`; if found, gives it `a`'s previous slot (the swap; the free spare slot
 |   has no owner, so this is skipped and `a` simply moves), then sets `a` to `s`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_chord_slot
 | Params: a -- the chord action being reassigned; s -- the SL_* slot to give it
 | Returns: N/A
 ----------------------*/
void chord_assign(int a, int s) {
    for (int o = 0; o < CA_N; o++) if (o != a && g_chord_slot[o] == s) g_chord_slot[o] = g_chord_slot[a];
    g_chord_slot[a] = s;
}

/*----------------------
 | mapping_reset_defaults
 | Description: Copies FACE_DEFAULT into g_face_btn and CHORD_DEFAULT into
 |   g_chord_slot.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn, g_chord_slot
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void mapping_reset_defaults(void) {
    for (int a = 0; a < FA_N; a++) g_face_btn[a]   = FACE_DEFAULT[a];
    for (int a = 0; a < CA_N; a++) g_chord_slot[a] = CHORD_DEFAULT[a];
}
