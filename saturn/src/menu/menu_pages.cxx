/*----------------------
 | menu_pages.cxx
 | Description: Implements the Options menu and its sub-pages: the Network
 |   dial-number editor, the live control-remap editor and its two read-only
 |   Controls views (gamepad and keyboard), Sound Options, and Display
 |   Options. Every page constructs a
 |   MenuBacking on entry (menu.h) so an image background stays suppressed
 |   for the page's lifetime, and drops the input edge that opened it with an
 |   initial SRL::Core::Synchronize() before entering its poll loop, so the
 |   same button press that opened the page cannot also act inside it. Pages
 |   that offer OK/Cancel snapshot the state they edit on entry and restore
 |   it verbatim on Cancel. menu_digit_row is the shared C++ binding from a
 |   polled key-char to a page's own (row, direction) locals, backing
 |   menu_layout.c's unit-tested digit-to-row mapping -- the layout unit
 |   itself cannot reference a page's local bool state.
 | Author: suinevere
 | Dependencies: menu.h, menu_layout.c, input.h (g_pad/g_face_btn/g_chord_slot/
 |   face_assign/chord_assign/face_btn_name/slot_name/pad_repeat_update/
 |   mapping_reset_defaults), console_view.h (note_input_device/hint/
 |   g_kbd_visible/g_caret_arrows), options.h (options_save/display_apply/
 |   display_cycle_row/valid_dialnum), app_state.h (g_difficulty/g_dialnum/
 |   g_display/g_mix_mode/g_sel_track/g_music_level/g_pcm_level), keyboard.h,
 |   saturn_keyboard.h, soft_reset.h, display.h, sound.h, music.h, SRL
 ----------------------*/

#include <srl.hpp>

#include "menu_pages.h"
#include "menu.h"
#include "app_state.h"
#include "console_view.h"
#include "input.h"
#include "options.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"

extern "C" {
#include "keyboard.h"
#include "menu_layout.h"
#include "display.h"
#include "sound.h"
#include "music.h"
}

/*----------------------
 | menu_digit_row
 | Description: Reads one polled key-char event and, via menu_row_digit
 |   (menu_layout.c, unit-tested standalone), maps the character to a row
 |   index in [0, nrows) plus a direction -- a plain digit selects forward,
 |   the shifted symbol above it selects backward. On a match, writes `sel`
 |   and sets `left` or `right` per the direction, leaving the page's own
 |   activation flag for the caller to set afterward, since call sites
 |   disagree on whether that local is named `ok` or `act`.
 | Author: suinevere
 | Dependencies: menu_layout.c
 | Globals: N/A
 | Params: ke -- the polled key event; nrows -- number of selectable rows on
 |   the page; sel -- (out) row selected on a digit-row match; left, right --
 |   (out) direction flags set on a match
 | Returns: true if `ke` selected a row (sel/left/right updated); false
 |   otherwise
 ----------------------*/
static bool menu_digit_row(const SaturnKeyEvent &ke, int nrows,
                           int &sel, bool &left, bool &right) {
    if (ke.kind != SATURN_KEY_CHAR) return false;
    int ddir = 0;
    int drow = menu_row_digit(ke.ch, nrows, &ddir);
    if (drow < 0) return false;
    sel = drow;
    if (ddir > 0) right = true; else left = true;
    return true;
}

/*----------------------
 | config_page
 | Description: Network dial-number editor, driven by either a real keyboard
 |   or the on-screen keyboard grid under pad control. Seeds the edit buffer
 |   from g_dialnum, then loops: a real-keyboard char/backspace/enter/escape/
 |   clear edits or accepts/cancels directly; otherwise the pad moves the
 |   on-screen highlight (Up/Down/Left/Right), C types the highlighted key, B
 |   backspaces, A accepts, Start cancels. Accept validates the buffer with
 |   valid_dialnum before committing it into g_dialnum and calling
 |   options_save(); an invalid buffer keeps the page open with an inline
 |   error instead of closing. Cancel returns without saving. Redraws the
 |   NETWORK box, the current input line, and the on-screen keyboard grid
 |   every frame.
 | Author: suinevere
 | Dependencies: keyboard.c, saturn_keyboard.h, soft_reset.h, options.c
 |   (valid_dialnum, options_save), menu.c, console_view.c
 | Globals: g_dialnum
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void config_page(void) {
    MenuBacking backing;
    KeyboardState k; keyboard_reset(&k);
    for (int i = 0; g_dialnum[i] && k.input_len < DIALNUM_MAX; i++) keyboard_type_char(&k, g_dialnum[i]);
    const char *err = "";
    SRL::Core::Synchronize();
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        bool accept = false, cancel = false;
        if      (ke.kind == SATURN_KEY_CHAR)      { if (k.input_len < DIALNUM_MAX) keyboard_type_char(&k, ke.ch); }
        else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
        else if (ke.kind == SATURN_KEY_ENTER)     accept = true;
        else if (ke.kind == SATURN_KEY_ESCAPE)    cancel = true;
        else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
        else {
            if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
            if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
            if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
            if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
            if (g_pad->WasPressed(Button::C) && k.input_len < DIALNUM_MAX) keyboard_type(&k);
            if (g_pad->WasPressed(Button::B))     keyboard_backspace(&k);
            if (g_pad->WasPressed(Button::A))     accept = true;
            if (g_pad->WasPressed(Button::START)) cancel = true;
        }
        if (cancel) return;
        if (accept) {
            if (!valid_dialnum(k.input)) err = "Invalid number (digits only).";
            else {
                int j;
                for (j = 0; k.input[j] && j < (int) sizeof(g_dialnum) - 1; j++) g_dialnum[j] = k.input[j];
                g_dialnum[j] = '\0';
                options_save();
                return;
            }
        }
        menu_clear();
        const int fx = 1, fy = 6, fw = 38, fh = 16;
        menu_frame(fx, fy, fw, fh, "NETWORK");
        SRL::Debug::Print(fx + 2, fy + 3, "Server dial number:");
        SRL::Debug::Print(fx + 2, fy + 4, "> %s_", k.input);
        for (int r = 0; r < KB_ROWS; r++) {
            char rowbuf[KB_COLS * 2 + 1]; int p = 0;
            for (int c = 0; c < KB_COLS; c++) {
                rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
                rowbuf[p++] = KB_LAYOUT[r][c];
            }
            rowbuf[p] = '\0';
            SRL::Debug::Print(fx + 4, fy + 6 + r, "%s", rowbuf);
        }
        if (err[0]) SRL::Debug::Print(fx + 2, fy + 11, "%s", err);
        SRL::Debug::Print(fx + 2, fy + 13, "%s",
            hint("C=type B=del  A=OK  Start=Cancel", "type number  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
}

/*----------------------
 | FACE_LABEL / CHORD_LABEL
 | Description: Display names for the remappable face actions (FA_*) and chord
 |   actions (CA_*), shown as the row labels in the control-remap editor.
 | Author: suinevere
 ----------------------*/
static const char *const FACE_LABEL[FA_N]  = { "Accept", "Backspace/Cancel", "Type Letter" };
static const char *const CHORD_LABEL[CA_N] = { "Autocomplete", "Recall", "Home/End",
                                               "Line Up/Down", "Cursor Move", "Page Up/Down" };

/*----------------------
 | configure_controls_page
 | Description: Live remap editor: 3 face-button rows + 6 shift-chord rows,
 |   then Reset to Defaults, OK, and Cancel -- 12 rows total, but only the
 |   first 9 are numbered (there are just 9 digit keys, so Reset/OK/Cancel
 |   stay reachable only by Up/Down). Snapshots g_face_btn/g_chord_slot on
 |   entry so Cancel (or B/Esc) can restore them verbatim. Up/Down move the
 |   row cursor with wraparound, resolved before the digit-row jump so a
 |   same-frame digit press wins the tie against the pad -- the order the
 |   other five option pages use; resolving Up/Down first would let a
 |   simultaneous press move `sel` while left/right/act stayed set from the
 |   digit, cycling whichever row the pad happened to land on instead.
 |   Left/Right cycle the selected row's assignment via face_assign/
 |   chord_assign (applying their own tie-breaking rules), or activate
 |   Reset/OK/Cancel. The value column is drawn at a fixed offset of x + 20 +
 |   MENU_DIGIT_COLS, reserved unconditionally so it does not shift when the
 |   player switches between gamepad and keyboard mid-page; the widest value
 |   string is "Z+Left/Right" (12 chars), still clearing the box's right
 |   border at column 39. The Caps Toggle row is fixed and unselectable, so
 |   its label is indented by the same reserved digit columns to stay
 |   aligned with the numbered rows above it.
 | Author: suinevere
 | Dependencies: input.c (g_face_btn/g_chord_slot/face_assign/chord_assign/
 |   mapping_reset_defaults/face_btn_name/slot_name), console_view.c
 |   (note_input_device/hint/g_kbd_visible), menu.c, menu_layout.c
 |   (MENU_DIGIT_COLS), options.c (options_save), saturn_keyboard.h
 | Globals: g_face_btn, g_chord_slot, g_kbd_visible
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void configure_controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
    int s_face[FA_N], s_chord[CA_N];
    for (int a = 0; a < FA_N; a++) s_face[a]  = g_face_btn[a];
    for (int a = 0; a < CA_N; a++) s_chord[a] = g_chord_slot[a];
    const int NASSIGN  = FA_N + CA_N;
    const int R_RESET  = NASSIGN;
    const int R_DONE   = NASSIGN + 1;
    const int R_CANCEL = NASSIGN + 2;
    int sel = 0;
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        bool up    = g_pad->WasPressed(Button::Up)    || ke.kind == SATURN_KEY_UP;
        bool down  = g_pad->WasPressed(Button::Down)  || ke.kind == SATURN_KEY_DOWN;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool act   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                   || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool back  = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                   || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) {
            for (int a = 0; a < FA_N; a++) g_face_btn[a]   = s_face[a];
            for (int a = 0; a < CA_N; a++) g_chord_slot[a] = s_chord[a];
            break;
        }
        if (up)   sel = (sel - 1 + R_CANCEL + 1) % (R_CANCEL + 1);
        if (down) sel = (sel + 1) % (R_CANCEL + 1);
        if (menu_digit_row(ke, NASSIGN, sel, left, right)) act = true;
        if (sel == R_DONE)  { if (act) { options_save(); break; } }
        else if (sel == R_CANCEL) { if (act) {
            for (int a = 0; a < FA_N; a++) g_face_btn[a]   = s_face[a];
            for (int a = 0; a < CA_N; a++) g_chord_slot[a] = s_chord[a];
            break; } }
        else if (sel == R_RESET) { if (act) mapping_reset_defaults(); }
        else if (left || right) {
            if (sel < FA_N) {
                int n = right ? (g_face_btn[sel] + 1) % 3 : (g_face_btn[sel] + 2) % 3;
                face_assign(sel, n);
            } else {
                int a = sel - FA_N;
                int n = right ? (g_chord_slot[a] + 1) % SL_N : (g_chord_slot[a] + SL_N - 1) % SL_N;
                chord_assign(a, n);
            }
        }

        menu_clear();
        const int fx = 0, fy = 3, fw = 40, fh = 22;
        menu_frame(fx, fy, fw, fh, "CONFIGURE CONTROLS");
        int x = fx + 2, y = fy + 3;
        SRL::Debug::Print(x, y++, "%s", hint("L/R change  A/Start=OK B=Cancel",
                                             "L/R change  Enter=OK Esc=Cancel"));
        y++;
        bool nums = !g_kbd_visible;
        const int vx = x + 20 + MENU_DIGIT_COLS;
        for (int a = 0; a < FA_N; a++) {
            char cur = sel == a ? '>' : ' ';
            if (nums) SRL::Debug::Print(x, y, "%c %d) %s", cur, a + 1, FACE_LABEL[a]);
            else      SRL::Debug::Print(x, y, "%c    %s", cur, FACE_LABEL[a]);
            SRL::Debug::Print(vx, y++, "%s", face_btn_name(a));
        }
        for (int a = 0; a < CA_N; a++) {
            char cur = sel == FA_N + a ? '>' : ' ';
            if (nums) SRL::Debug::Print(x, y, "%c %d) %s", cur, FA_N + a + 1, CHORD_LABEL[a]);
            else      SRL::Debug::Print(x, y, "%c    %s", cur, CHORD_LABEL[a]);
            SRL::Debug::Print(vx, y++, "%s", slot_name(g_chord_slot[a]));
        }
        SRL::Debug::Print(x + 2 + MENU_DIGIT_COLS, y, "Caps Toggle");
        SRL::Debug::Print(vx, y++, "L+R (fixed)");
        y++;
        SRL::Debug::Print(x, y++, "%c    Reset to Defaults", sel == R_RESET ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c    OK", sel == R_DONE ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c    Cancel", sel == R_CANCEL ? '>' : ' ');
        menu_sync();
    }
    SRL::Core::Synchronize();
}

/*----------------------
 | controls_page
 | Description: Gamepad-in-hand Controls page: shows the live face-button and
 |   shift-chord mapping read-only, plus three selectable rows -- Configure
 |   Mapping (opens configure_controls_page), Keyboard Caps toggle (any of
 |   Left/Right/act flips it), and Done. Up/Down move the row cursor with
 |   wraparound over exactly these 3 rows; a digit-row match feeds directly
 |   into `act`, since only Configure and Done are true actions and the Caps
 |   row treats direction and activation alike. The read-only mapping table
 |   keeps its own column layout and is not part of the 3 selectable rows or
 |   their digit numbering.
 | Author: suinevere
 | Dependencies: input.c (g_chord_slot/face_btn_name/slot_name/
 |   pad_repeat_update), keyboard.c (keyboard_get_caps/keyboard_set_caps),
 |   console_view.c (note_input_device/hint), menu.c
 | Globals: g_chord_slot
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
    int sel = 0;
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel + 2) % 3;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % 3;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool act = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                  || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) break;
        if (menu_digit_row(ke, 3, sel, left, right)) act = true;
        if (sel == 0 && act) configure_controls_page();
        else if (sel == 1 && (left || right || act)) keyboard_set_caps(!keyboard_get_caps());
        else if (sel == 2 && act) break;

        menu_clear();
        const int fx = 1, fy = 3, fw = 38, fh = 22;
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        int x = fx + 2, y = fy + 3;
        for (int a = 0; a < FA_N; a++) {
            SRL::Debug::Print(x, y, "%s", FACE_LABEL[a]);
            SRL::Debug::Print(x + 18, y++, "%s", face_btn_name(a));
        }
        for (int a = 0; a < CA_N; a++) {
            SRL::Debug::Print(x, y, "%s", CHORD_LABEL[a]);
            SRL::Debug::Print(x + 18, y++, "%s", slot_name(g_chord_slot[a]));
        }
        SRL::Debug::Print(x, y, "Space / Accept+Sp");
        SRL::Debug::Print(x + 18, y++, "X");
        SRL::Debug::Print(x, y, "Caps Toggle");
        SRL::Debug::Print(x + 18, y++, "L+R");
        y++;
        bool nums = !g_kbd_visible;
        if (nums) SRL::Debug::Print(x, y++, "%c 1) Configure Mapping", sel == 0 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Configure Mapping", sel == 0 ? '>' : ' ');
        if (nums) SRL::Debug::Print(x, y++, "%c 2) Keyboard Caps: %s", sel == 1 ? '>' : ' ',
                                    keyboard_get_caps() ? "On" : "Off");
        else      SRL::Debug::Print(x, y++, "%c    Keyboard Caps: %s", sel == 1 ? '>' : ' ',
                                    keyboard_get_caps() ? "On" : "Off");
        if (nums) SRL::Debug::Print(x, y++, "%c 3) Done", sel == 2 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Done", sel == 2 ? '>' : ' ');
        menu_sync();
    }
    SRL::Core::Synchronize();
}

/*----------------------
 | keyboard_controls_page
 | Description: Physical-keyboard settings page: 6 rows -- Arrows-move-caret-
 |   vs-suggestions, Insert mode, Caps Lock, Num Lock (all two-state toggles,
 |   so direction and activation are treated alike), then OK and Cancel.
 |   Snapshots g_caret_arrows and the keyboard.c getters on entry so Cancel
 |   (B/Esc, or the Cancel row) can restore them. OK commits and calls
 |   options_save(). The value column is fixed at x + 18 in BOTH digit and
 |   no-digit modes -- unlike the other pages it must NOT take the usual
 |   MENU_DIGIT_COLS shift: at x + 21 the widest value, "Off (overwrite)" (15
 |   chars), would end on column 38, this box's right border (fx=1, fw=38).
 |   It does not need the shift regardless: the longest numbered label,
 |   "N) Insert mode", ends at column 18, still two columns clear of the
 |   value at 21.
 | Author: suinevere
 | Dependencies: keyboard.c (keyboard_get_insert/keyboard_set_insert/
 |   keyboard_get_caps/keyboard_set_caps/keyboard_get_num/keyboard_set_num),
 |   console_view.c (g_caret_arrows/note_input_device/hint/g_kbd_visible),
 |   input.c (pad_repeat_update), menu.c, options.c (options_save)
 | Globals: g_caret_arrows
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void keyboard_controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
    int s_arrows = g_caret_arrows, s_ins = keyboard_get_insert(),
        s_caps = keyboard_get_caps(), s_num = keyboard_get_num();
    const int N = 6;
    int sel = 0;
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + N) % N;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % N;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool act = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                  || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) {
            g_caret_arrows = s_arrows; keyboard_set_insert(s_ins);
            keyboard_set_caps(s_caps); keyboard_set_num(s_num);
            break;
        }
        if (menu_digit_row(ke, N, sel, left, right)) act = true;
        bool toggle = left || right || act;
        if      (sel == 0 && toggle) g_caret_arrows = !g_caret_arrows;
        else if (sel == 1 && toggle) keyboard_set_insert(!keyboard_get_insert());
        else if (sel == 2 && toggle) keyboard_set_caps(!keyboard_get_caps());
        else if (sel == 3 && toggle) keyboard_set_num(!keyboard_get_num());
        else if (sel == 4 && act) { options_save(); break; }
        else if (sel == 5 && act) {
            g_caret_arrows = s_arrows; keyboard_set_insert(s_ins);
            keyboard_set_caps(s_caps); keyboard_set_num(s_num); break; }

        menu_clear();
        const int fx = 1, fy = 5, fw = 38, fh = 18;
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        int x = fx + 2, y = fy + 3;
        SRL::Debug::Print(x, y++, "Insert key also flips Arrows;");
        SRL::Debug::Print(x, y++, "Ctrl+Left/Right always move caret.");
        y++;
        bool nums = !g_kbd_visible;
        if (nums) SRL::Debug::Print(x, y, "%c 1) Arrows move", sel == 0 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Arrows move", sel == 0 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", g_caret_arrows ? "Caret" : "Suggestions");
        if (nums) SRL::Debug::Print(x, y, "%c 2) Insert mode", sel == 1 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Insert mode", sel == 1 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_insert() ? "On (insert)" : "Off (overwrite)");
        if (nums) SRL::Debug::Print(x, y, "%c 3) Caps Lock", sel == 2 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Caps Lock", sel == 2 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_caps() ? "On" : "Off");
        if (nums) SRL::Debug::Print(x, y, "%c 4) Num Lock", sel == 3 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Num Lock", sel == 3 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_num() ? "On" : "Off");
        y++;
        if (nums) SRL::Debug::Print(x, y++, "%c 5) OK", sel == 4 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    OK", sel == 4 ? '>' : ' ');
        if (nums) SRL::Debug::Print(x, y++, "%c 6) Cancel", sel == 5 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Cancel", sel == 5 ? '>' : ' ');
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("A/Start=OK  B=Cancel", "Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}

/*----------------------
 | sound_options_page
 | Description: Sound Options (full-screen, OK/Cancel). Which rows appear
 |   depends on what is actually available: Audio Mix / Track / Music level
 |   need CD-DA on the disc (has_cd, from music_cdda_audio_tracks() > 0);
 |   PCM level needs the loaded game's .BLB (has_blb, from
 |   sound_has_audio()); OK/Cancel always show.
 |   `sel` indexes the resulting visible-row list, not a fixed row number.
 |   Snapshots g_mix_mode/g_sel_track/g_music_level/g_pcm_level for Cancel.
 |   `previewed` tracks whether a live demo (Track row Left/Right, which
 |   calls music_cdda_play) interrupted whatever was streaming, so exit only
 |   re-asserts playback -- music_refresh() for Dynamic mix, else
 |   music_start() -- when a preview fired or the mix/track actually
 |   changed; absent both, opening and closing this page in-game leaves the
 |   current track running uninterrupted. Cancel restores the snapshot
 |   (including live audio via music_set_level/sound_set_level/
 |   music_set_mix) and, if a preview fired, calls music_refresh() to put
 |   back what was playing. The value column is fixed at x + 14 +
 |   MENU_DIGIT_COLS in both digit and no-digit modes, reserved
 |   unconditionally so it does not move when the player switches device;
 |   the widest value is "< Sequential >" (14 chars), ending at column 33,
 |   clear of the box's right border at column 38.
 | Author: suinevere
 | Dependencies: music.c (music_cdda_audio_tracks/music_cdda_play/
 |   music_set_volume/music_set_level/music_set_mix/music_refresh/
 |   music_start/music_cdda_has_audio/MIX_*), sound.c (sound_has_audio/
 |   sound_set_level), console_view.c
 |   (note_input_device/hint/g_kbd_visible), input.c (pad_repeat_update),
 |   menu.c, menu_layout.c (MENU_DIGIT_COLS), options.c (options_save),
 |   soft_reset.h (check_soft_reset)
 | Globals: g_mix_mode, g_sel_track, g_music_level, g_pcm_level
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sound_options_page(void) {
    MenuBacking backing;
    static const char *const MIX[] = { "Dynamic", "Repeat", "Sequential", "Random" };
    enum { SR_MIX, SR_TRACK, SR_MUSIC, SR_PCM, SR_OK, SR_CANCEL };
    const unsigned char* atracks; int an = music_cdda_audio_tracks(&atracks);
    bool has_cd  = (an > 0);
    bool has_blb = (sound_has_audio() != 0);

    int rows[6], nrows = 0;
    if (has_cd)  { rows[nrows++] = SR_MIX; rows[nrows++] = SR_TRACK; rows[nrows++] = SR_MUSIC; }
    if (has_blb) rows[nrows++] = SR_PCM;
    rows[nrows++] = SR_OK;
    rows[nrows++] = SR_CANCEL;

    int sel = 0;
    int s_mix = g_mix_mode, s_trk = g_sel_track, s_mus = g_music_level, s_pcm = g_pcm_level;
    bool previewed = false;
    // Open the Track row on whatever is actually sounding, falling back to the saved
    // selection and then to the first audio track. Deliberately does NOT write
    // g_sel_track: the page must be able to open and close without changing anything,
    // or the OK handler below sees g_sel_track != s_trk and restarts the music the
    // player came in listening to.
    int aidx = -1;
    int cur = music_cdda_current_track();
    if (cur > 0) for (int i = 0; i < an; i++) if (atracks[i] == cur)         { aidx = i; break; }
    if (aidx < 0)   for (int i = 0; i < an; i++) if (atracks[i] == g_sel_track) { aidx = i; break; }
    if (aidx < 0) aidx = 0;
    SRL::Core::Synchronize();
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nrows) % nrows;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nrows;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool ok   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                  || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool cancel = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                    || ke.kind == SATURN_KEY_BACKSPACE;
        if (menu_digit_row(ke, nrows, sel, left, right)) ok = true;
        int row = rows[sel];

        if (cancel || (ok && row == SR_CANCEL)) {
            g_mix_mode = s_mix; g_sel_track = s_trk; g_music_level = s_mus; g_pcm_level = s_pcm;
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track);
            if (previewed) music_refresh();
            break;
        }
        if (row == SR_MIX) { if (left && g_mix_mode > 0) g_mix_mode--; if (right && g_mix_mode < MIX_RANDOM) g_mix_mode++; }
        else if (row == SR_TRACK) {
            if (left  && aidx > 0)      aidx--;
            if (right && aidx < an - 1) aidx++;
            // Only an actual interaction commits a new selection, so that merely
            // visiting this row leaves g_sel_track (and the music) alone.
            if ((left || right || ok) && an > 0) {
                g_sel_track = atracks[aidx];
                music_cdda_play(g_sel_track);
                previewed = true;
            }
        }
        else if (row == SR_MUSIC) { if (left && g_music_level > 0) g_music_level--; if (right && g_music_level < 7) g_music_level++;
                                    if (left || right) music_set_volume(g_music_level); }
        else if (row == SR_PCM)   { if (left && g_pcm_level > 0) g_pcm_level--; if (right && g_pcm_level < 7) g_pcm_level++;
                                    if (left || right) sound_set_level(g_pcm_level); }
        else if (ok && row == SR_OK) {
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track);
            if (previewed || g_mix_mode != s_mix || g_sel_track != s_trk) {
                if (g_mix_mode == MIX_DYNAMIC) music_refresh();
                else music_start();
            }
            options_save();
            break;
        }

        menu_clear();
        const int fx = 1, fy = 6, fw = 38, fh = 16;
        menu_frame(fx, fy, fw, fh, "SOUND");
        int x = fx + 2, y = fy + 3;
        bool nums = !g_kbd_visible;
        const int vx = x + 14 + MENU_DIGIT_COLS;
        for (int i = 0; i < nrows; i++) {
            char cur = (i == sel) ? '>' : ' ';
            switch (rows[i]) {
                case SR_MIX:
                    if (nums) SRL::Debug::Print(x, y, "%c %d) Audio Mix", cur, i + 1);
                    else      SRL::Debug::Print(x, y, "%c    Audio Mix", cur);
                    SRL::Debug::Print(vx, y++, "%s %s %s", g_mix_mode > 0 ? "<" : " ", MIX[g_mix_mode], g_mix_mode < MIX_RANDOM ? ">" : " ");
                    break;
                case SR_TRACK:
                    if (nums) SRL::Debug::Print(x, y, "%c %d) Track", cur, i + 1);
                    else      SRL::Debug::Print(x, y, "%c    Track", cur);
                    SRL::Debug::Print(vx, y++, "%d", aidx + 1);
                    break;
                case SR_MUSIC:
                    if (nums) SRL::Debug::Print(x, y, "%c %d) Music", cur, i + 1);
                    else      SRL::Debug::Print(x, y, "%c    Music", cur);
                    SRL::Debug::Print(vx, y++, "%d", g_music_level);
                    break;
                case SR_PCM:
                    if (nums) SRL::Debug::Print(x, y, "%c %d) PCM", cur, i + 1);
                    else      SRL::Debug::Print(x, y, "%c    PCM", cur);
                    SRL::Debug::Print(vx, y++, "%d", g_pcm_level);
                    break;
                case SR_OK:
                    y++;
                    if (nums) SRL::Debug::Print(x, y++, "%c %d) OK", cur, i + 1);
                    else      SRL::Debug::Print(x, y++, "%c    OK", cur);
                    break;
                case SR_CANCEL:
                    if (nums) SRL::Debug::Print(x, y++, "%c %d) Cancel", cur, i + 1);
                    else      SRL::Debug::Print(x, y++, "%c    Cancel", cur);
                    break;
            }
        }
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("<> change  A/Start=OK  B=Cancel", "<> change  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}

/*----------------------
 | display_options_page
 | Description: Display Options (full-screen, OK/Cancel). Unlike Sound
 |   Options every row is always present -- there is no hardware dependency.
 |   Left/Right applies each cycler row live (via display_cycle_row) so the
 |   result is visible behind the menu immediately; Cancel restores the
 |   g_display snapshot taken on entry and re-applies it with
 |   display_apply(); OK persists it with options_save(). Uses the full 40
 |   columns rather than the 38 the other pages use. Values print at x + 17,
 |   leaving 20 columns before the border, so "< %s >" fits a name of at most
 |   16 characters. Two sources feed these rows and both must stay under
 |   that: PRESETS in display.c (widest "Amstrad CPC 464", 15 chars) and the
 |   disc image names, capped at GFS_FNAME_LEN = 12 by ISO9660 8.3 -- a
 |   one-column margin that a longer preset name, or image names no longer
 |   bounded by 8.3, would need the value column moved for, not just a wider
 |   box (at 38 the Palette row already lands on the border). This is the
 |   one page where the value column CANNOT take the usual MENU_DIGIT_COLS
 |   shift: at x + 20 the widest value ("< Amstrad CPC 464 >", 19 chars)
 |   would run to column 40 and overwrite the border, and the box is already
 |   the full screen width so it cannot grow. The 3 columns instead come out
 |   of the LABEL side -- "System Palette" was shortened to "Palette" so the
 |   longest numbered label ("N) Background", ending at column 17) still
 |   clears the value column at 19.
 | Author: suinevere
 | Dependencies: display.c (DisplayState/display_palette_name/
 |   display_bg_name/display_text_name), options.c (display_apply/
 |   display_cycle_row/DCR_* / options_save), console_view.c
 |   (note_input_device/hint/g_kbd_visible), input.c (pad_repeat_update),
 |   menu.c, menu_layout.c (MENU_DIGIT_COLS), soft_reset.h
 |   (check_soft_reset)
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void display_options_page(void) {
    MenuBacking backing;
    enum { DR_PALETTE, DR_BG, DR_TEXT, DR_OK, DR_CANCEL };
    static const int rows[] = { DR_PALETTE, DR_BG, DR_TEXT, DR_OK, DR_CANCEL };
    const int nrows = (int)(sizeof(rows) / sizeof(rows[0]));

    int sel = 0;
    DisplayState snapshot = g_display;
    SRL::Core::Synchronize();
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nrows) % nrows;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nrows;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool ok   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                  || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool cancel = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                    || ke.kind == SATURN_KEY_BACKSPACE;
        if (menu_digit_row(ke, nrows, sel, left, right)) ok = true;
        int row = rows[sel];

        if (cancel || (ok && row == DR_CANCEL)) {
            g_display = snapshot;
            display_apply();
            break;
        }
        int dir = right ? 1 : (left ? -1 : 0);
        if (dir != 0) {
            if      (row == DR_PALETTE) display_cycle_row(DCR_PALETTE, dir);
            else if (row == DR_BG)      display_cycle_row(DCR_BG,      dir);
            else if (row == DR_TEXT)    display_cycle_row(DCR_TEXT,    dir);
        }
        if (ok && row == DR_OK) { options_save(); break; }

        menu_clear();
        const int fx = 0, fy = 7, fw = 40, fh = 14;
        menu_frame(fx, fy, fw, fh, "DISPLAY");
        int x = fx + 2, y = fy + 3;
        bool nums = !g_kbd_visible;
        for (int i = 0; i < nrows; i++) {
            char cur = (i == sel) ? '>' : ' ';
            switch (rows[i]) {
                case DR_PALETTE:
                    if (nums) SRL::Debug::Print(x, y, "%c %d) Palette", cur, i + 1);
                    else      SRL::Debug::Print(x, y, "%c    Palette", cur);
                    SRL::Debug::Print(x + 17, y++, "< %s >", display_palette_name(&g_display));
                    break;
                case DR_BG:
                    if (nums) SRL::Debug::Print(x, y, "%c %d) Background", cur, i + 1);
                    else      SRL::Debug::Print(x, y, "%c    Background", cur);
                    SRL::Debug::Print(x + 17, y++, "< %s >", display_bg_name(&g_display));
                    break;
                case DR_TEXT:
                    if (nums) SRL::Debug::Print(x, y, "%c %d) Text", cur, i + 1);
                    else      SRL::Debug::Print(x, y, "%c    Text", cur);
                    SRL::Debug::Print(x + 17, y++, "< %s >", display_text_name(g_display.text));
                    break;
                case DR_OK:
                    y++;
                    if (nums) SRL::Debug::Print(x, y++, "%c %d) OK", cur, i + 1);
                    else      SRL::Debug::Print(x, y++, "%c    OK", cur);
                    break;
                case DR_CANCEL:
                    if (nums) SRL::Debug::Print(x, y++, "%c %d) Cancel", cur, i + 1);
                    else      SRL::Debug::Print(x, y++, "%c    Cancel", cur);
                    break;
            }
        }
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("<> change  A/Start=OK  B=Cancel",
                                             "<> change  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}

/*----------------------
 | options_menu
 | Description: Options menu (centered box): a difficulty slider plus
 |   actions (Network, Controls, Display, Sound, Return to Title, Done).
 |   Builds a dynamic item list -- Difficulty is always items[0]; Network,
 |   Controls, and Display are always present (Display has no hardware
 |   dependency); Sound appears only when there is audio to configure (CD-DA
 |   on the disc or the game's .BLB); Return to Title and Done always
 |   follow. Up/Down select a row with wraparound; on Difficulty, Left/Right
 |   adjust a local `diff` copy (committed to g_difficulty and saved only on
 |   exit, and only if it actually changed) while every other row ignores
 |   direction. A digit-row match is resolved before `item` is read (it can
 |   move `sel`) and OR'd into the activation flag; direction only matters
 |   on the difficulty slider, so every other row ignores left/right and
 |   Difficulty ignores activation. Activating dispatches to the matching
 |   sub-page (config_page; controls_page or keyboard_controls_page
 |   depending on g_kbd_visible; display_options_page; sound_options_page),
 |   or for Return to Title, confirms via menu_confirm and, on yes, commits
 |   any difficulty change before calling soft_reset_to_title() (which never
 |   returns). Redraws with an unconditional menu_clear() before
 |   menu_frame() every frame -- MenuBacking only suppresses the image
 |   inside its own box rectangle, and does nothing to leftover text OUTSIDE
 |   it, so without this the wider menu that opened Options (e.g. the
 |   Single/Multiplayer list) would show through around this box. On exit
 |   (Done or B/Esc), commits any difficulty change and options_save()s it,
 |   then blocks on menu_sync() until B/A/C/Start are all released, so the
 |   button that closed this menu cannot leak into whatever reads input
 |   next.
 | Author: suinevere
 | Dependencies: options.c (options_save), music.c (music_cdda_has_audio),
 |   sound.c (sound_has_audio), menu.c (menu_confirm), soft_reset.h
 |   (soft_reset_to_title, check_soft_reset), console_view.c
 |   (note_input_device/hint/g_kbd_visible), menu_pages.cxx (config_page/
 |   controls_page/keyboard_controls_page/display_options_page/
 |   sound_options_page)
 | Globals: g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_menu(void) {
    MenuBacking backing;
    static const char *const NAMES[] = { "Easy", "Medium", "Hard" };
    static const char *const DESC[]  = { "Walkthrough steps only",
                                         "Valid-command typeahead",
                                         "Typeahead off" };
    const int x0 = 5, y0 = 8, w = 30, h = 15;
    enum { OI_DIFF, OI_CONFIG, OI_CONTROLS, OI_DISPLAY, OI_SOUND, OI_RETURN, OI_DONE };
    bool sound_available = (music_cdda_has_audio() != 0) || (sound_has_audio() != 0);
    int items[7], nitems = 0;
    items[nitems++] = OI_DIFF;
    items[nitems++] = OI_CONFIG;
    items[nitems++] = OI_CONTROLS;
    items[nitems++] = OI_DISPLAY;
    if (sound_available) items[nitems++] = OI_SOUND;
    items[nitems++] = OI_RETURN;
    items[nitems++] = OI_DONE;

    int diff = g_difficulty, sel = 0;
    SRL::Core::Synchronize();
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nitems) % nitems;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nitems;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool digit = menu_digit_row(ke, nitems, sel, left, right);
        int item = items[sel];
        if (item == OI_DIFF) { if (left && diff > DIFF_EASY) diff--; if (right && diff < DIFF_HARD) diff++; }
        bool act = digit
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                  || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) break;
        if (act) {
            if (item == OI_CONFIG) { config_page(); }
            else if (item == OI_CONTROLS) { if (g_kbd_visible) controls_page(); else keyboard_controls_page(); menu_clear(); }
            else if (item == OI_DISPLAY) { display_options_page(); menu_clear(); }
            else if (item == OI_SOUND) { sound_options_page(); menu_clear(); }
            else if (item == OI_RETURN) {
                if (menu_confirm("Return to the title screen?", "Are you sure?")) {
                    if (diff != g_difficulty) { g_difficulty = diff; options_save(); }
                    soft_reset_to_title();
                }
            }
            else if (item == OI_DONE) break;
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "OPTIONS");
        bool nums = !g_kbd_visible;
        char dmark = item == OI_DIFF ? '>' : ' ';
        if (nums) SRL::Debug::Print(x0 + 2, y0 + 3, "%c 1) Difficulty: %s %s %s", dmark,
                          diff > DIFF_EASY ? "<" : " ", NAMES[diff], diff < DIFF_HARD ? ">" : " ");
        else      SRL::Debug::Print(x0 + 2, y0 + 3, "%c    Difficulty: %s %s %s", dmark,
                          diff > DIFF_EASY ? "<" : " ", NAMES[diff], diff < DIFF_HARD ? ">" : " ");
        SRL::Debug::Print(x0 + 2, y0 + 4, "    %s", DESC[diff]);
        int ay = y0 + 6;
        for (int i = 0; i < nitems; i++) {
            char cur = (i == sel) ? '>' : ' ';
            const char *label = 0;
            switch (items[i]) {
                case OI_DIFF: continue;
                case OI_CONFIG:   label = "Network";         break;
                case OI_CONTROLS: label = "Controls";        break;
                case OI_DISPLAY:  label = "Display";         break;
                case OI_SOUND:    label = "Sound";           break;
                case OI_RETURN:   label = "Return to Title"; break;
                case OI_DONE:     label = "Done";            break;
            }
            if (nums) SRL::Debug::Print(x0 + 2, ay++, "%c %d) %s", cur, i + 1, label);
            else      SRL::Debug::Print(x0 + 2, ay++, "%c    %s", cur, label);
        }
        SRL::Debug::Print(x0 + 2, y0 + 13, "%s", hint("Up/Dn A=pick  <>=diff", "Up/Dn Enter  B=back"));
        menu_sync();
    }
    bool diff_changed = (diff != g_difficulty);
    g_difficulty = diff;
    if (diff_changed) options_save();
    while (g_pad->IsHeld(Button::B) || g_pad->IsHeld(Button::A) ||
           g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::START))
        menu_sync();
}
