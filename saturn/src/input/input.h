/*----------------------
 | input.h
 | Description: Controller input for the game loop and the mapping-editor menu
 |   pages: the MultiPad multi-port/multi-family gamepad aggregate and the single
 |   shared g_pad instance every input read goes through; the remappable face-
 |   button (Accept/Backspace/Type) and shift-chord (Autocomplete/Recall/Home-End/
 |   Line/Cursor/Page) mapping tables and their tie-swap assign helpers; gamepad
 |   auto-repeat for the editing buttons and hold-repeat for the shift chords;
 |   gamepad-driven console scrollback and its physical-keyboard counterpart; and
 |   shell-style Up/Down command history recall. Owns no rendering -- callers draw
 |   what these report.
 | Author: suinevere
 | Dependencies: app_state.h, keyboard.h, saturn_keyboard.h, SRL
 ----------------------*/

#ifndef INPUT_H
#define INPUT_H

#include <srl.hpp>
#include "app_state.h"
#include "keyboard.h"
#include "saturn_keyboard.h"

using Button = SRL::Input::Digital::Button;

// Aggregates both hardware controller ports and both pad families (digital and
// analog / 3D control pad) so a controller in port 1 OR port 2 works in any
// configuration. Keeps the WasPressed/IsHeld interface so every call site is
// unchanged. The keyboard is polled separately (saturn_keyboard_poll already
// scans all ports), so a controller in one port + keyboard in the other works.
struct MultiPad {
    SRL::Input::Digital d0, d1;
    SRL::Input::Analog  a0, a1;
    MultiPad() : d0(0), d1(1), a0(0), a1(1) {}
    bool WasPressed(Button b) const {
        return (d0.IsConnected() && d0.WasPressed(b)) ||
               (d1.IsConnected() && d1.WasPressed(b)) ||
               (a0.IsConnected() && a0.WasPressed(b)) ||
               (a1.IsConnected() && a1.WasPressed(b));
    }
    bool IsHeld(Button b) const {
        return (d0.IsConnected() && d0.IsHeld(b)) ||
               (d1.IsConnected() && d1.IsHeld(b)) ||
               (a0.IsConnected() && a0.IsHeld(b)) ||
               (a1.IsConnected() && a1.IsHeld(b));
    }
    // True on the frame any nav/action button edges down (edge state is not
    // consumed, so this is safe to call alongside the per-button WasPressed calls).
    bool AnyPressed() const {
        return WasPressed(Button::Up)   || WasPressed(Button::Down)  ||
               WasPressed(Button::Left) || WasPressed(Button::Right) ||
               WasPressed(Button::A)    || WasPressed(Button::B)     ||
               WasPressed(Button::C)    || WasPressed(Button::X)     ||
               WasPressed(Button::Y)    || WasPressed(Button::Z)     ||
               WasPressed(Button::L)    || WasPressed(Button::R)     ||
               WasPressed(Button::START);
    }
};

// One shared multi-port gamepad, used everywhere input is read. main() owns the
// backing storage (a function-static MultiPad) and points this here before the
// game loop starts; every other module treats it as always-valid thereafter.
extern MultiPad *g_pad;

// ---- configurable controller mapping ---------------------------------------
// Two tied groups the player can remap (Options > Controller > Configure):
//   Group 1 (face buttons): Accept, Backspace/Cancel, Type-letter -- always a
//     permutation of {A,B,C}; reassigning one swaps with whoever held that button
//     ("alternate when changed").
//   Group 2 (shift chords): Autocomplete, Recall, Home/End, Line, Cursor-move and
//     Page -- each in one of seven slots {L/R, Z+Up/Dn, Z+L/R, Z+Left/Right,
//     Y+Up/Dn, Y+Left/Right, Y+L/R}; reassigning to a used slot swaps, to the free
//     spare just moves ("alternate iff already used").
//   Fixed: A accept, X space/accept+space, C type, B backspace, L+R caps toggle.
// Everything reads through face_button()/chord_fired() so both editors honor it.
enum { FA_ACCEPT, FA_BACK, FA_TYPE, FA_N };
enum { CA_AUTO, CA_RECALL, CA_HOMEEND, CA_LINE, CA_CURSOR, CA_PAGE, CA_N };
// Directional chord slots. Y is the shift for line/home-end/page (it took over the
// old X shift; X is now a normal button); Z carries recall/cursor. Suffix: "t" =
// shoulder triggers L/R, "d" = D-pad Left/Right. The spare directional slot is
// SL_ZLRd; caps-toggle rides the fixed L+R combo instead of a slot.
enum { SL_LR, SL_ZUD, SL_ZLRt, SL_ZLRd, SL_YUD, SL_YLRd, SL_YLRt, SL_N };

// Current face-button mapping: g_face_btn[FA_*] holds which of {0=A,1=B,2=C} that
// action fires on. Persisted by main.cxx's options_load/options_save.
extern int g_face_btn[FA_N];

// Current shift-chord mapping: g_chord_slot[CA_*] holds which SL_* slot that
// action fires on. Persisted by main.cxx's options_load/options_save.
extern int g_chord_slot[CA_N];

/*----------------------
 | face_button
 | Description: The A/B/C button currently carrying face-action `action`
 |   (FA_ACCEPT/FA_BACK/FA_TYPE), per the player's remapping.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: action -- one of FA_ACCEPT/FA_BACK/FA_TYPE
 | Returns: the Button (A, B, or C) currently assigned to that action
 ----------------------*/
Button face_button(int action);

/*----------------------
 | face_btn_name
 | Description: Display name ("A"/"B"/"C") of the button currently carrying
 |   face-action `action`, for the mapping-editor rows and in-game hints.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: action -- one of FA_ACCEPT/FA_BACK/FA_TYPE
 | Returns: "A", "B", or "C"
 ----------------------*/
const char *face_btn_name(int action);

/*----------------------
 | slot_name
 | Description: Display name of a chord slot ("L/R", "Z+Up/Dn", ...), for the
 |   mapping-editor rows.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- one of the SL_* slot constants
 | Returns: the slot's display string
 ----------------------*/
const char *slot_name(int slot);

/*----------------------
 | caps_combo_fired
 | Description: Reports the rising edge of the fixed L+R (no shift) caps-toggle
 |   combo -- fires once per press, not once per held frame.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: true on the frame the combo first becomes held
 ----------------------*/
bool caps_combo_fired(void);

/*----------------------
 | chord_tick
 | Description: Advances the per-slot edge/hold-repeat state for every shift-
 |   chord slot; must be called once per input frame before chord_fired.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void chord_tick(void);

/*----------------------
 | chord_fired
 | Description: Whether chord action `action` (one of the CA_* constants) fired
 |   in direction `dir` (-1 or +1) on the frame chord_tick was last called.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_chord_slot
 | Params: action -- one of the CA_* constants; dir -- -1 or +1
 | Returns: true if that action's slot fired in that direction this frame
 ----------------------*/
bool chord_fired(int action, int dir);

/*----------------------
 | pad_scroll_update
 | Description: Applies the configurable Line/Home-End/Page scrollback chords
 |   (default shift Y) to the console scroll position. Call once per input frame,
 |   after chord_tick.
 | Author: suinevere
 | Dependencies: app_state.h
 | Globals: g_scroll
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void pad_scroll_update(void);

/*----------------------
 | pad_repeat_update
 | Description: Advances the auto-repeat timers for the editing buttons (D-pad,
 |   A, C, B, X, L, R) so pad_fired can report both the initial press and each
 |   repeat tick while held. Call once per input frame.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void pad_repeat_update(void);

/*----------------------
 | pad_fired
 | Description: Whether button `b` fired this frame -- the initial press or an
 |   auto-repeat tick for the tracked editing buttons, a plain edge otherwise.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: b -- the button to check
 | Returns: true if it fired (pressed or repeated) this frame
 ----------------------*/
bool pad_fired(Button b);

/*----------------------
 | scroll_handle_key
 | Description: Routes a physical-keyboard navigation key (arrows, Page Up/Down,
 |   Home/End) to the console scroll position.
 | Author: suinevere
 | Dependencies: app_state.h, saturn_keyboard.h
 | Globals: g_scroll
 | Params: ke -- the keyboard event to test
 | Returns: true if `ke` was a nav key and was consumed; false otherwise
 ----------------------*/
bool scroll_handle_key(const SaturnKeyEvent &ke);

/*----------------------
 | history_push
 | Description: Remembers a submitted command for Up/Down recall. Skips blank
 |   lines and a line identical to the most recently stored one, and ends any
 |   in-progress browsing so the next Up starts from the newest entry again.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- the command line just submitted
 | Returns: N/A
 ----------------------*/
void history_push(const char *s);

/*----------------------
 | history_load
 | Description: Copies the history entry at the current browse offset into the
 |   keyboard input line, with the caret placed at its end.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard state to overwrite
 | Returns: N/A
 ----------------------*/
void history_load(KeyboardState *k);

/*----------------------
 | history_recall
 | Description: Steps the history browse position and loads the resulting entry
 |   into the input line -- older (`older` != 0) moves toward earlier commands,
 |   newer (`older` == 0) moves back toward the freshest, clearing to an empty
 |   line once it steps past the newest.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard state to update; older -- nonzero for older, zero for newer
 | Returns: N/A
 ----------------------*/
void history_recall(KeyboardState *k, int older);

/*----------------------
 | face_assign
 | Description: Assigns face-action `a` to button `b`. If another action already
 |   holds `b`, that action takes over whatever button `a` previously had (a
 |   swap), keeping the three face actions a permutation of {A,B,C}.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: a -- the face action being reassigned; b -- the button (0=A,1=B,2=C) to give it
 | Returns: N/A
 ----------------------*/
void face_assign(int a, int b);

/*----------------------
 | chord_assign
 | Description: Assigns chord action `a` to slot `s`. If another chord action
 |   already holds `s`, that action takes over `a`'s previous slot (a swap);
 |   otherwise (the slot was the free spare) `a` simply moves.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_chord_slot
 | Params: a -- the chord action being reassigned; s -- the SL_* slot to give it
 | Returns: N/A
 ----------------------*/
void chord_assign(int a, int s);

/*----------------------
 | mapping_reset_defaults
 | Description: Restores both the face-button and shift-chord mappings to their
 |   compiled defaults.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn, g_chord_slot
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void mapping_reset_defaults(void);

#endif /* INPUT_H */
