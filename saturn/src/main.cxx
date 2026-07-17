#include <srl.hpp>
#include <setjmp.h>

extern "C" {
#include "console.h"
#include "keyboard.h"
#include "saturn_keyboard.h"
#include "saturn_backup.h"
#include "saturn_glue.h"
#include "sound.h"
#include "term.h"
#include "net/net_connect.h"
#include "typeahead.h"
#include "typeahead_extract.h"
#include "typeahead_solution.h"
#include "music.h"
#include "game_titles.h"
}

// Global typeahead trie (should be populated by the game backend eventually)
static TrieNode* g_typeahead_root = nullptr;

// Difficulty (Options menu). Easy = full typeahead + winning-path hints; Medium =
// typeahead, grammar weights only; Hard = typeahead off. Defined here because
// ensure_typeahead() below reads it. Persistence/UI live further down.
enum { DIFF_EASY = 0, DIFF_MEDIUM = 1, DIFF_HARD = 2 };
static int g_difficulty = DIFF_EASY;

// Sound (Options menu). On by default; persisted in MOJOOPTS alongside
// difficulty and the dial number. See options_load/options_save below.
static int g_music_level = 7;   // CD-DA music level 0..7 (default full)
static int g_pcm_level   = 4;   // PCM sound-effect level 0..7 (default mid)
static int g_mix_mode  = MIX_DYNAMIC;   // Audio Mix: Dynamic/Override/Sequential/Random
static int g_sel_track = 10;            // selected/override track, also the menu track

// Online dial number (editable in Options -> Configure MojoZork; persisted).
static char g_dialnum[24] = "199403";

// "Load Save Game": a save slot pre-selected from the menu, applied by the first
// in-game "restore" (queued via g_autocmd) instead of the choose_dest prompt.
static int g_restore_device = -1;
static int g_restore_slot   = -1;
static const char *g_autocmd = nullptr;   // command auto-submitted on the next readline

extern "C" void* typeahead_malloc(unsigned int size) {
    return SRL::Memory::HighWorkRam::Malloc(size);
}

extern "C" void typeahead_free(void* ptr) {
    SRL::Memory::Free(ptr);
}

// Build the typeahead from whatever story is currently loaded, decoding its own
// dictionary and grammar on-device. Rebuilds (freeing the old trie) whenever the
// loaded story changes, so switching games picks up the new vocabulary.
static const uint8_t* g_ta_story = nullptr;
static int g_ta_diff = -1;
static void ensure_typeahead() {
    uint32_t len = 0;
    const uint8_t* story = saturn_story_data(&len);
    if (g_typeahead_root && story == g_ta_story && g_ta_diff == g_difficulty) return;
    if (g_typeahead_root) { destroy_typeahead(g_typeahead_root); g_typeahead_root = nullptr; }
    g_typeahead_root = create_trie_node();
    int have_solution = 0;
    if (story != nullptr && len > 0 && g_difficulty != DIFF_HARD) {
        build_typeahead_from_story(g_typeahead_root, story, len);           // grammar layer
        // Apply the winning-path overlay in BOTH Easy and Normal: Easy restricts
        // suggestions to it; Normal uses its links as the "unless in solution"
        // exception to the grammar filter.
        have_solution = apply_solution_overlay(g_typeahead_root, story, len);
        // Last, so the twelve stock abbreviations are in whatever trie the story
        // produced (the extractor drops the bare direction ones). Hard builds no
        // trie at all, so it stays helpless -- the rule is: they exist iff it does.
        typeahead_add_abbreviations(g_typeahead_root);
    }
    typeahead_set_easy(g_difficulty == DIFF_EASY, have_solution);
    g_ta_story = story;
    g_ta_diff = g_difficulty;
}


#define MOJOZORK_DIAL_NUMBER "199403"

// snprintf links from newlib; the SRL dummy <stdio.h> omits its declaration.
extern "C" int snprintf(char *str, size_t size, const char *fmt, ...);

#define SAVE_SLOTS 5

using namespace SRL::Types;
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

// One shared multi-port gamepad, used everywhere input is read.
static MultiPad *g_pad = nullptr;

// Soft reset (the Sega-mandated A+B+C+Start chord, and the typed "reboot" command)
// returns the player to the title screen *in-process* -- NOT a hardware/SMPC reset,
// which reboots the console all the way back to CD load. main() arms this jump
// target just before the title screen; the input loops poll the chord and longjmp
// back to it. Configured options live in backup RAM, so they survive the jump.
static jmp_buf  g_title_jmp;
static bool     g_title_jmp_armed = false;

// The story file currently loaded from CD (set in main after game selection).
// saturn_read_story_file re-reads this for save/restart, so it must track the
// chosen game rather than a hard-coded name.
static const char *g_story_filename = "ZORK1.Z3";

// ---- rendering -------------------------------------------------------------

// The debug layer gives us 28 text rows (0..27). When the on-screen keyboard is
// shown it occupies the bottom rows; when hidden (the player is typing on a real
// keyboard) those rows are handed back to the console for more text.
static const int SCREEN_ROWS = 28;

// TV overscan clips the very top text row on real hardware (the first line shows
// only its bottom half), so we keep row 0 blank and start all console content on
// row 1. Menus already draw from row 1+, so this only affects the console layout.
static const int TOP_MARGIN = 1;

// Whether the on-screen keyboard is drawn. Flipped by the active input device: a
// real-keyboard keypress hides it (more text room); a gamepad press shows it again.
static bool g_kbd_visible = true;

// Inline-edit mode: which arrows move the text caret vs cycle suggestions.
// false (default): Ctrl+Left/Right move the caret, plain Left/Right cycle.
// true:  plain Left/Right move the caret, Ctrl+Left/Right cycle. Toggled by Insert.
static bool g_caret_arrows = false;

// While the reboot confirm modal is up, the console shrinks to leave the bottom
// rows for the prompt, so the console (including the freshly-echoed "reboot") stays
// visible above it instead of being overwritten by the prompt band.
static bool g_reboot_menu = false;
static const int REBOOT_MENU_ROWS = 8;   // bottom rows reserved for the prompt band

// Console text rows currently available; the input line sits on the next row down.
// Shown: reserve input + KB_ROWS keyboard rows + a hint row. Hidden: just input.
static int console_height(void) {
    int avail = SCREEN_ROWS - TOP_MARGIN;
    if (g_reboot_menu) return avail - REBOOT_MENU_ROWS;   // 19 rows; prompt sits below
    return g_kbd_visible ? (avail - (1 + KB_ROWS + 1)) : (avail - 1);
}

// Input-hint text flips to match the active device instead of cramming both
// devices' commands onto one line: gamepad wording while the on-screen keyboard
// is up (the player is on a pad), real-keyboard wording once it's hidden (the
// player is typing on a physical keyboard).
static const char *hint(const char *pad, const char *kbd) {
    return g_kbd_visible ? pad : kbd;
}

// Track which device the player last used so the hints (and the on-screen
// keyboard) follow it. Call once per input frame with that frame's key event.
static void note_input_device(const SaturnKeyEvent &ke) {
    if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;
    else if (g_pad->AnyPressed())   g_kbd_visible = true;
}

// One menu frame: keep looping PCM sounds alive while a modal menu is open (the
// ping-pong hand-off needs sound_service() every frame, or the loop starves and
// goes silent). Menu loops call this in place of a bare Synchronize.
static void menu_sync(void) {
    sound_service();
    music_tick();      // advance one-shot mixes / commit debounced Dynamic switches
    SRL::Core::Synchronize();
}

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
// shoulder triggers L/R, "d" = D-pad Left/Right.
enum { SL_LR, SL_ZUD, SL_ZLRt, SL_ZLRd, SL_YUD, SL_YLRd, SL_YLRt, SL_N };

static int g_face_btn[FA_N]   = { 0, 1, 2 };   // 0=A 1=B 2=C (Accept/Back/Type)
static int g_chord_slot[CA_N] = { SL_LR, SL_ZUD, SL_YLRd, SL_YUD, SL_ZLRt, SL_YLRt };
static const int FACE_DEFAULT[FA_N]  = { 0, 1, 2 };
static const int CHORD_DEFAULT[CA_N] = { SL_LR, SL_ZUD, SL_YLRd, SL_YUD, SL_ZLRt, SL_YLRt };
// The spare directional slot is SL_ZLRd. Caps-toggle rides the fixed L+R combo.

// ---- options / difficulty --------------------------------------------------
// (DIFF_* and g_difficulty are declared up top for ensure_typeahead.) Easy: full
// typeahead + winning-path hints. Medium: typeahead, grammar weights only. Hard:
// off. Defaults to Easy; only written to backup once the player changes it.
// Persisted blob: difficulty(1) + dial number(NUL-terminated) + sound flag(1).
// Older saves (difficulty only, or difficulty+dial with a zeroed tail) load
// fine: the sound byte reads as 0 (absent), which the flag encoding below
// treats as "on" so pre-existing saves aren't silently muted.
static void options_load(void) {
    uint8_t buf[64];
    for (int z = 0; z < (int) sizeof(buf); z++) buf[z] = 0;
    if (!saturn_bup_read(SATURN_BUP_CONSOLE, "MOJOOPTS", buf)) return;
    if (buf[0] <= DIFF_HARD) g_difficulty = (int) buf[0];
    int i = 1;   // tracks the offset of the dial number's terminating NUL
    if (buf[1]) {
        int j;
        for (j = 0; buf[1 + j] && j < (int) sizeof(g_dialnum) - 1; j++) g_dialnum[j] = (char) buf[1 + j];
        g_dialnum[j] = '\0';
        i = 1 + j;
    }
    // Two audio levels follow the dial NUL: [music][pcm], each 0..7. A legacy blob
    // had a single sound flag here (1 = off, else on); map it: off -> pcm 0.
    if (i + 1 < (int) sizeof(buf)) {
        uint8_t a = buf[i + 1], b = (i + 2 < (int) sizeof(buf)) ? buf[i + 2] : 0xFF;
        if (a <= 7 && b <= 7) { g_music_level = a; g_pcm_level = b; }
        else { g_pcm_level = (a == 1) ? 0 : 4; g_music_level = 7; }   // legacy sound flag
    }
    // Controller mapping follows the two level bytes: a format sentinel (2 =
    // current: 3 face + 6 chord bytes) then the bytes. Older/absent blobs keep
    // the compiled defaults.
    int m = i + 3;
    if (m + 1 + FA_N + CA_N <= (int) sizeof(buf) && buf[m] == 2) {
        for (int a = 0; a < FA_N; a++) { int v = buf[m + 1 + a];        if (v < 3)    g_face_btn[a]   = v; }
        for (int a = 0; a < CA_N; a++) { int v = buf[m + 1 + FA_N + a]; if (v < SL_N) g_chord_slot[a] = v; }
    }
    // Sound block follows the controller bytes: sentinel 1, then [mix][track].
    int s = m + 1 + FA_N + CA_N;
    if (s + 2 < (int) sizeof(buf) && buf[s] == 1) {
        if (buf[s + 1] <= MIX_RANDOM) g_mix_mode = buf[s + 1];
        if (buf[s + 2] >= MUSIC_TRACK_MIN && buf[s + 2] <= MUSIC_TRACK_MAX) g_sel_track = buf[s + 2];
    }
}
static void options_save(void) {
    uint8_t buf[64]; int n = 0;
    buf[n++] = (uint8_t) g_difficulty;
    for (int i = 0; g_dialnum[i] && n < 62; i++) buf[n++] = (uint8_t) g_dialnum[i];
    buf[n++] = 0;
    buf[n++] = (uint8_t) g_music_level;           // audio levels: [music][pcm], 0..7
    buf[n++] = (uint8_t) g_pcm_level;
    buf[n++] = 2;                                 // controller-mapping format sentinel
    for (int a = 0; a < FA_N && n < 62; a++) buf[n++] = (uint8_t) g_face_btn[a];
    for (int a = 0; a < CA_N && n < 62; a++) buf[n++] = (uint8_t) g_chord_slot[a];
    buf[n++] = 1;                                 // sound-block sentinel
    buf[n++] = (uint8_t) g_mix_mode;              // 0..3
    buf[n++] = (uint8_t) g_sel_track;             // 2..32
    saturn_bup_write(SATURN_BUP_CONSOLE, "MOJOOPTS", "options", buf, (uint32_t) n);
}
static void options_menu(void);   // defined below, near the other menus
static bool menu_confirm(const char *line1, const char *line2);  // defined below
static void sound_options_page(void);

// ---- scrollback ------------------------------------------------------------

// How many lines the view is scrolled up from the live bottom (0 = latest text).
// render_console clamps this to the available range every frame, so callers can
// bump it freely. Driven by the physical keyboard's nav keys (the on-screen
// keyboard cursor is moved by the gamepad D-pad instead).
static int g_scroll = 0;
static const int SCROLL_PAGE = 16;        // Page Up/Down jump size, in lines
static const int SCROLL_ALL  = 1 << 30;   // Home: clamped down to the oldest line

// Scrollback line index where the latest output block began. Captured just before
// the interpreter runs a turn (or set to 0 for the initial room) so the pager can
// land on the TOP of a long response instead of its bottom.
static long g_output_start = 0;   // console_total_lines() mark taken before a turn's output

// Route a physical-keyboard nav key to the scrollback. Returns true if the event
// was a nav key (and thus consumed, so it isn't treated as text). Left/Right are
// consumed too: they no longer move the on-screen keyboard cursor.
static bool scroll_handle_key(const SaturnKeyEvent &ke) {
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

static const int PAD_SCROLL_DELAY = 30;   // frames before auto-repeat kicks in
static const int PAD_SCROLL_RATE  = 4;    // frames between repeats while held

// The A/B/C button carrying face-action `action` (FA_ACCEPT/BACK/TYPE).
static Button face_button(int action) {
    static const Button BTN[3] = { Button::A, Button::B, Button::C };
    return BTN[g_face_btn[action]];
}
// Display names for the current mapping (SRL::Debug::Print has no width flags, so
// callers align columns by printing the value at a fixed x).
static const char *face_btn_name(int action) {
    static const char *N[3] = { "A", "B", "C" };
    return N[g_face_btn[action]];
}
static const char *slot_name(int slot) {
    static const char *N[SL_N] = { "L/R", "Z+Up/Dn", "Z+L/R", "Z+Left/Right",
                                   "Y+Up/Dn", "Y+Left/Right", "Y+L/R" };
    return N[slot];
}

// Raw held direction of a chord slot this frame: -1 (neg: L / Up / Left),
// +1 (pos: R / Down / Right), 0 (idle). Trigger slots yield nothing when both L+R
// are held (that combo is the caps toggle), and plain L/R yields nothing under a
// shift so it never clashes with the shifted trigger slots.
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

// The caps-toggle combo: L+R pressed together with no shift held. Rising edge only.
static bool caps_combo_fired(void) {
    static bool was = false;
    bool now = g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R)
             && !g_pad->IsHeld(Button::Z) && !g_pad->IsHeld(Button::Y);
    bool fired = now && !was;
    was = now;
    return fired;
}

// Per-slot edge + hold-repeat, ticked once per input frame by chord_tick().
static struct ChordRep { int dir; int timer; bool fired; } g_chordrep[SL_N];
static void chord_tick(void) {
    for (int s = 0; s < SL_N; s++) {
        int d = slot_raw(s);
        ChordRep &r = g_chordrep[s];
        if (d == 0)              { r.dir = 0; r.timer = 0; r.fired = false; }
        else if (d != r.dir)     { r.dir = d; r.timer = PAD_SCROLL_DELAY; r.fired = true; }
        else if (--r.timer <= 0) { r.timer = PAD_SCROLL_RATE; r.fired = true; }
        else                     { r.fired = false; }
    }
}
// Did chord action `action` fire in direction `dir` (-1/+1) this frame?
static bool chord_fired(int action, int dir) {
    const ChordRep &r = g_chordrep[g_chord_slot[action]];
    return r.fired && r.dir == dir;
}

// Gamepad scrollback via the configurable Line / Home-End / Page chords (defaults
// Y+Up/Down, Y+Left/Right, Y+L/R), all sharing chord_tick's per-slot hold-repeat.
// Requires chord_tick() this frame. Call once per input frame; the on-screen
// keyboard cursor moves on the plain D-pad (no shift) in typeahead_edit.
static void pad_scroll_update(void) {
    if (chord_fired(CA_LINE,    -1)) g_scroll += 1;
    if (chord_fired(CA_LINE,    +1)) g_scroll -= 1;
    if (chord_fired(CA_HOMEEND, -1)) g_scroll  = SCROLL_ALL;
    if (chord_fired(CA_HOMEEND, +1)) g_scroll  = 0;
    if (chord_fired(CA_PAGE,    -1)) g_scroll += SCROLL_PAGE;
    if (chord_fired(CA_PAGE,    +1)) g_scroll -= SCROLL_PAGE;
}

// ---- gamepad auto-repeat ----------------------------------------------------
// The editing buttons (D-pad, C, B, Y, L, R) should repeat while held, like the
// real keyboard. pad_repeat_update() ticks all their timers once per frame;
// pad_fired() then reports the initial press plus each repeat tick.
#define PAD_REPEAT_DELAY 30   // frames before auto-repeat kicks in (~0.5s)
#define PAD_REPEAT_RATE  4    // frames between repeats while held

static struct PadRepeat { Button btn; int timer; bool fired; } g_padrep[] = {
    { Button::Up, 0, false }, { Button::Down, 0, false },
    { Button::Left, 0, false }, { Button::Right, 0, false },
    { Button::L, 0, false }, { Button::R, 0, false },
    { Button::A, 0, false }, { Button::C, 0, false },
    { Button::B, 0, false }, { Button::X, 0, false },
};

static void pad_repeat_update(void) {
    for (auto &r : g_padrep) {
        if (!g_pad->IsHeld(r.btn))      { r.timer = 0; r.fired = false; }
        else if (r.timer == 0)          { r.fired = true;  r.timer = PAD_REPEAT_DELAY; }
        else if (--r.timer <= 0)        { r.fired = true;  r.timer = PAD_REPEAT_RATE; }
        else                            { r.fired = false; }
    }
}

// Initial press or a repeat tick this frame (tracked buttons); plain edge otherwise.
static bool pad_fired(Button b) {
    for (auto &r : g_padrep) if (r.btn == b) return r.fired;
    return g_pad->WasPressed(b);
}

// ---- command history -------------------------------------------------------
// Up/Down recall previously entered commands into the input line (shell-style).
#define HISTORY_MAX 16
static char g_history[HISTORY_MAX][KB_INPUT_MAX];
static int  g_hist_count  = 0;    // entries stored (<= HISTORY_MAX)
static int  g_hist_head   = 0;    // ring buffer: index of the next write slot
static int  g_hist_browse = -1;   // -1 = editing a fresh line; >=0 = steps back from newest

// Remember a submitted command. Skips blanks and consecutive duplicates, and ends
// any in-progress browsing so the next Up starts from the newest entry again.
static void history_push(const char *s) {
    g_hist_browse = -1;
    if (s == nullptr || s[0] == '\0') return;
    if (g_hist_count > 0) {
        int last = (g_hist_head - 1 + HISTORY_MAX) % HISTORY_MAX;
        int i = 0; while (s[i] && g_history[last][i] && s[i] == g_history[last][i]) i++;
        if (s[i] == '\0' && g_history[last][i] == '\0') return;   // same as most recent
    }
    int n = 0; while (s[n] && n < KB_INPUT_MAX - 1) { g_history[g_hist_head][n] = s[n]; n++; }
    g_history[g_hist_head][n] = '\0';
    g_hist_head = (g_hist_head + 1) % HISTORY_MAX;
    if (g_hist_count < HISTORY_MAX) g_hist_count++;
}

// Copy the entry at the current browse offset into the input line.
static void history_load(KeyboardState *k) {
    int idx = (g_hist_head - 1 - g_hist_browse + HISTORY_MAX * 2) % HISTORY_MAX;
    const char *s = g_history[idx];
    int n = 0; while (s[n] && n < KB_INPUT_MAX - 1) { k->input[n] = s[n]; n++; }
    k->input[n] = '\0';
    k->input_len = n;
    k->cursor = n;             // caret to the end of the recalled line
}

// older != 0 -> previous (older) command; older == 0 -> newer, clearing past the
// newest back to an empty line. No-op when there's no history.
static void history_recall(KeyboardState *k, int older) {
    if (g_hist_count == 0) return;
    if (older) {
        if (g_hist_browse < g_hist_count - 1) { g_hist_browse++; history_load(k); }
    } else {
        if (g_hist_browse > 0) { g_hist_browse--; history_load(k); }
        else { g_hist_browse = -1; k->input_len = 0; k->input[0] = '\0'; k->cursor = 0; }
    }
}

static void render_console(void) {
    int rows = console_height();
    int total = console_line_count();
    int maxstart = (total > rows) ? (total - rows) : 0;
    if (g_scroll < 0)            g_scroll = 0;
    if (g_scroll > maxstart + 1) g_scroll = maxstart + 1;   // one blank line past the top
    int top_blank = (g_scroll == maxstart + 1) ? 1 : 0;     // showing the blank-line affordance
    int start = maxstart - (g_scroll - top_blank);
    for (int r = 0; r < rows; r++) {
        SRL::Debug::PrintClearLine(TOP_MARGIN + r);
        int li = start + r - top_blank;                     // shift down by the blank row
        if (li >= 0 && li < total)
            SRL::Debug::Print(0, TOP_MARGIN + r, "%s", console_get_line(li));
    }
    // Edge markers when there's off-screen text above/below the window.
    if (start > 0 && !top_blank) SRL::Debug::Print(39, TOP_MARGIN, "^");
    if (start + rows < total)    SRL::Debug::Print(35, TOP_MARGIN + rows - 1, "more v");
}

// Position the scrollback on the turn's output once it has landed (called at the
// start of the NEXT readline, when the output actually exists). `added` is how many
// lines this turn produced, from the monotonic counter so it stays correct even
// after old lines evict from the 128-line ring. If the turn is taller than the
// window, land on its TOP row (so the player reads from the start and pages down via
// "more v"); otherwise snap to the live bottom.
static void console_scroll_to_output(void) {
    int total = console_line_count(), rows = console_height();
    int maxstart = (total > rows) ? (total - rows) : 0;
    long added = console_total_lines() - g_output_start;   // lines this turn emitted
    if (added > rows) {
        int top = total - (int) added;              // buffer index where the turn began
        if (top < 0) top = 0;                       // turn longer than the ring: oldest survivor
        g_scroll = maxstart - top;                  // lines up from bottom to put `top` at the top row
        if (g_scroll < 0) g_scroll = 0;
    } else {
        g_scroll = 0;                               // fits on screen: live bottom
    }
}

// ---- blinking block cursor -------------------------------------------------
//
// The SGL ASCII font has no solid-block glyph, so we carve one into the
// otherwise-unused DEL (0x7F) slot and print that as the cursor. ASCII::Print
// addresses font 0's char data at VDP2_VRAM_B1 + 0x18000 + charNum*0x20, where
// charNum = char + 640 (see srl_ascii.hpp: fontBank=640, and LoadFontSG's dest
// math). For 0x7F that lands at +0x1DFE0, the last tile LoadFontSG populated.
// 0xFF fills every 4bpp pixel with color index 15 -- the same index the font
// glyphs use -- so the block renders in the current text color.
static const char CURSOR_BLOCK_STR[2] = { (char) 0x7f, '\0' };

static void install_block_glyph(void) {
    volatile uint8_t* tile =
        (volatile uint8_t*)(VDP2_VRAM_B1 + 0x18000 + (0x7f + 640) * 0x20);
    for (int i = 0; i < 32; i++) tile[i] = 0xFF;
}

// Draw "> {input}{ghost}" at (0,row) with a blinking block cursor. The block
// sits on the first predicted (ghost) character -- the next char TAB would
// accept -- or just past the input when there's no prediction. When the block
// is "off" the cell shows the character underneath, so it appears to blink.
static void draw_input_line(int row, const KeyboardState &k,
                            DictionaryWord* prediction, int current_word_len,
                            bool block_on) {
    const char* suffix = "";
    char sbuf[64];
    // The completion ghost only shows when the caret is at the end of the line
    // (a mid-line caret means the player is editing, so typeahead is suppressed).
    if (prediction && k.cursor == k.input_len && k.input_len < KB_INPUT_MAX - 1) {
        const char* g = prediction->text + current_word_len;
        // Match the case the player is typing: if the current word's last typed
        // character is uppercase, show the completion (ghost) in uppercase too.
        bool up = current_word_len > 0 && k.input_len > 0 &&
                  k.input[k.input_len - 1] >= 'A' && k.input[k.input_len - 1] <= 'Z';
        int i = 0;
        for (; g[i] && i < (int) sizeof(sbuf) - 1; i++)
            sbuf[i] = (up && g[i] >= 'a' && g[i] <= 'z') ? (char) (g[i] - 'a' + 'A') : g[i];
        sbuf[i] = '\0';
        suffix = sbuf;
    }
    SRL::Debug::Print(0, row, "> %s%s", k.input, suffix);

    int cursor_col = 2 + k.cursor;   // 2 = width of the "> " prompt; caret within line
    // Char under the caret: the edited char when mid-line, else the ghost head/space.
    char under = (k.cursor < k.input_len) ? k.input[k.cursor]
                                          : (suffix[0] ? suffix[0] : ' ');
    if (block_on) SRL::Debug::Print(cursor_col, row, "%s", CURSOR_BLOCK_STR);
    else          SRL::Debug::Print(cursor_col, row, "%c", under);
}

// Half-period, in frames, of the cursor blink (~0.33s at 60fps -> ~1.5Hz).
#define CURSOR_BLINK_FRAMES 20

static void render_keyboard(const KeyboardState &k, DictionaryWord* prediction, int current_word_len) {
    // One-time: install the solid-block glyph the cursor prints. Done here (not
    // at boot) so VDP2/the font are guaranteed up by the first render.
    static bool glyph_ready = false;
    if (!glyph_ready) { install_block_glyph(); glyph_ready = true; }

    // Advance the blink phase once per render (one render == one frame).
    static uint32_t blink = 0;
    bool block_on = ((blink++ / CURSOR_BLINK_FRAMES) & 1) != 0;

    // Keyboard hidden (real-keyboard user): the console's last line is already the
    // ">" prompt, so draw the input over it instead of on a separate row below --
    // otherwise the prompt shows twice. Clear the now-unused row underneath.
    int base = TOP_MARGIN + console_height();   // first row below the console
    if (!g_kbd_visible) {
        int row = base - 1;                     // over the console's last (prompt) line
        SRL::Debug::PrintClearLine(base);
        SRL::Debug::PrintClearLine(row);
        draw_input_line(row, k, prediction, current_word_len, block_on);
        return;
    }
    int row = base;   // input line sits directly below the console
    SRL::Debug::PrintClearLine(row);
    draw_input_line(row, k, prediction, current_word_len, block_on);
    for (int r = 0; r < KB_ROWS; r++) {
        char rowbuf[KB_COLS * 2 + 1];
        int p = 0;
        for (int c = 0; c < KB_COLS; c++) {
            rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
            rowbuf[p++] = keyboard_char_at(r, c);
        }
        rowbuf[p] = '\0';
        SRL::Debug::PrintClearLine(row + 1 + r);
        SRL::Debug::Print(2, row + 1 + r, "%s", rowbuf);
    }
    // CapsLock indicator, to the right of the (now wider) keyboard grid.
    if (keyboard_get_caps()) SRL::Debug::Print(30, row + 1, "CAPS");
    // Reflect the (remappable) face buttons; full mapping lives in Options>Controller.
    SRL::Debug::Print(0, row + 1 + KB_ROWS, "%s=type %s=accept %s=del  X=space",
                      face_btn_name(FA_TYPE), face_btn_name(FA_ACCEPT), face_btn_name(FA_BACK));
}

// ---- global reboot / quit commands -----------------------------------------

// True if `line` is exactly "reboot" (case-insensitive). The reboot command is
// global -- available from both the local game prompt and the online terminal.
static int is_reboot_command(const char *line) {
    static const char cmd[] = "reboot";
    int i;
    for (i = 0; cmd[i]; i++) {
        char c = line[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c != cmd[i]) return 0;
    }
    return line[i] == '\0';
}

// True if `line` is a bare "q" or "quit" (case-insensitive). Only the local game
// prompt intercepts this -- the interpreter's quit opcode tears down and crashes
// on Saturn, so the command must never reach it. That makes the match a safety
// boundary rather than a convenience: it has to catch every spelling the game's
// own parser would accept, hence the leading/trailing space handling that
// is_reboot_command (whose miss is harmless) can do without.
static int is_quit_command(const char *line) {
    while (*line == ' ') line++;                          // the parser skips these; so must we
    char w[6];
    int n = 0;
    for (; line[n] && line[n] != ' ' && n < 5; n++) {     // first word, lowercased
        char c = line[n];
        w[n] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
    }
    w[n] = '\0';
    const char *rest = line + n;
    while (*rest == ' ') rest++;
    if (*rest != '\0') return 0;                          // "quit now" is the game's to answer
    // Short-circuits at the terminator, so no read runs past it.
    return w[0] == 'q' && (w[1] == '\0' ||
           (w[1] == 'u' && w[2] == 'i' && w[3] == 't' && w[4] == '\0'));
}

// Perform the Sega software reset: drop any live connection, release the story
// image, and longjmp back to the title screen (armed in main). This is an
// in-process restart, not an SMPC reset -- the console never re-reads the CD. If
// the jump target isn't armed yet (can't happen once main is running), fall back
// to the SMPC reset-button NMI.
static void soft_reset_to_title(void) {
    net_connect_close();          // idempotent: drops the modem link if online
    sound_stop_all();             // stop any playing/looping sound before we bail out
    if (g_title_jmp_armed) longjmp(g_title_jmp, 1);
    slNMIRequest();               // fallback only
    while (1) {}
}

// Frames the A+B+C+Start reset chord must be held before it fires. A debounce, not
// a feature: it rejects the garbage peripheral read on the very first frame (before
// the first vsync has polled real input), which otherwise reads as "all held" and
// resets instantly. ~0.5s is still well below any real four-button hold.
static const int SOFT_RESET_HOLD = 30;

// True when the software-reset chord (A+B+C+Start) is physically held this frame.
static bool soft_reset_chord_held(void) {
    return g_pad->IsHeld(Button::A) && g_pad->IsHeld(Button::B) &&
           g_pad->IsHeld(Button::C) && g_pad->IsHeld(Button::START);
}

// Poll the software-reset chord. Call once per frame from the input loops; it never
// returns once the chord has been held long enough (it soft-resets to the title).
static void check_soft_reset(void) {
    static int hold = 0;
    hold = soft_reset_chord_held() ? (hold + 1) : 0;
    if (hold >= SOFT_RESET_HOLD) soft_reset_to_title();
}

// Modal Y/N confirm, asking `question`. On Yes, soft-resets to the title screen
// in-process (the same return-to-title as the A+B+C+Start chord); configured
// options in backup RAM are retained. On No, returns false so the caller resumes.
// Shared by the reboot and quit commands -- both discard an unsaved game, so both
// ask first.
static bool confirm_return_to_title(const char *question) {
    g_reboot_menu = true;       // shrink the console so it renders above the prompt band
    SRL::Core::Synchronize();   // drop the submit edge that triggered this
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);   // so the prompt below matches the device in hand
        bool yes = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'y' || ke.ch == 'Y'))
                 || ke.kind == SATURN_KEY_ENTER
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::START)
                 || g_pad->WasPressed(Button::C);
        bool no  = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'n' || ke.ch == 'N'))
                 || ke.kind == SATURN_KEY_ESCAPE || g_pad->WasPressed(Button::B);
        if (yes) { soft_reset_to_title(); }         // in-process return to title; never returns
        if (no) { g_reboot_menu = false; return false; }

        // Console (shrunk to rows above REBOOT_MENU_ROWS) still shows the game text
        // and the just-echoed command; the prompt band below is fully cleared each
        // frame so nothing bleeds through in either keyboard or controller mode.
        render_console();
        for (int r = SCREEN_ROWS - REBOOT_MENU_ROWS; r <= 28; r++) SRL::Debug::PrintClearLine(r);
        SRL::Debug::Print(2, 22, "%s", question);
        SRL::Debug::Print(1, 24, "%s", hint("(A) (C) (Start) = yes", "Y / Enter = yes"));
        SRL::Debug::Print(1, 26, "%s", hint("(B) = no", "N / Esc = no"));
        SRL::Core::Synchronize();
    }
}

// ---- hooks (extern "C" so the C core can call them) ------------------------

extern "C" void saturn_writestr(const char *str, size_t slen) {
    console_write(str, (unsigned int) slen);
    music_note_output(str, (unsigned int) slen);   // feed the room-music classifier
}

// One frame of on-screen input editing with typeahead, shared by the local game
// prompt and the online (multizork) terminal so both behave identically. Handles
// the gamepad (with auto-repeat) and real keyboard: moves the picker, types/
// deletes, cycles suggestions (L/R), accepts (A/Tab) or accepts+space (Y), and
// recalls history (X + Up/Down). May set k.submitted. sug_index/sug_last persist
// the cycle position across frames. Outputs the selected suggestion + typed-word
// length for the caller to render. Caller polls `ke`/`pad` and ticks
// pad_repeat_update() before calling.
static void typeahead_edit(KeyboardState &k, TrieNode *root,
                           int &sug_index, char *sug_last,
                           SaturnKeyEvent &ke, bool pad,
                           DictionaryWord *&selected_out, int &cw_len_out) {
    // On-screen keyboard editing (gamepad): move the picker, type, delete.
    if (pad) {
        if (caps_combo_fired()) keyboard_set_caps(!keyboard_get_caps());  // L+R toggles Caps
        // Recall + text-caret move ride their configurable chords (defaults
        // Z+Up/Down and Z+L/R).
        if (chord_fired(CA_RECALL, -1)) history_recall(&k, 1);   // older
        if (chord_fired(CA_RECALL, +1)) history_recall(&k, 0);   // newer
        if (chord_fired(CA_CURSOR, -1)) keyboard_caret_left(&k);
        if (chord_fired(CA_CURSOR, +1)) keyboard_caret_right(&k);
        // Plain D-pad (no Z/Y shift held) moves the on-screen keyboard picker.
        if (!g_pad->IsHeld(Button::Z) && !g_pad->IsHeld(Button::Y)) {
            if (pad_fired(Button::Up))    keyboard_move(&k, 0, -1);
            if (pad_fired(Button::Down))  keyboard_move(&k, 0,  1);
            if (pad_fired(Button::Left))  keyboard_move(&k, -1, 0);
            if (pad_fired(Button::Right)) keyboard_move(&k,  1, 0);
        }
        if (pad_fired(face_button(FA_TYPE))) keyboard_type(&k);       // type letter
        if (pad_fired(face_button(FA_BACK))) keyboard_backspace(&k);  // backspace
    }

    char current_word[256]; int cw_len; DictionaryWord *prev_word;
    DictionaryWord *cands[24]; int ncand; DictionaryWord *selected;
    auto refresh = [&]() {
        int ws = 0;
        for (int i = k.input_len - 1; i >= 0; i--) if (k.input[i] == ' ') { ws = i + 1; break; }
        cw_len = k.input_len - ws;
        if (cw_len > 255) cw_len = 255;
        for (int i = 0; i < cw_len; i++) current_word[i] = k.input[ws + i];
        current_word[cw_len] = '\0';
        prev_word = nullptr;
        if (ws > 1) {
            int ps = 0;
            for (int i = ws - 2; i >= 0; i--) if (k.input[i] == ' ') { ps = i + 1; break; }
            char pw[256]; int pl = (ws - 1) - ps; if (pl > 255) pl = 255;
            for (int i = 0; i < pl; i++) pw[i] = k.input[ps + i];
            pw[pl] = '\0';
            prev_word = find_exact_word(root, pw);
        }
        ncand = predict_candidates(root, prev_word, current_word, cands, 24, ws == 0);
        bool same = true;
        for (int i = 0; i <= cw_len; i++) if (current_word[i] != sug_last[i]) { same = false; break; }
        if (!same) { sug_index = 0; for (int i = 0; i <= cw_len; i++) sug_last[i] = current_word[i]; }
        if (ncand == 0) sug_index = 0; else if (sug_index >= ncand) sug_index %= ncand;
        selected = ncand > 0 ? cands[sug_index] : nullptr;
    };
    refresh();

    auto ghost_len = [&]() -> int {
        if (!selected) return 0;
        int n = 0; while (selected->text[n]) n++;
        return n > cw_len ? n - cw_len : 0;
    };
    auto accept = [&](bool add_space) {
        // Match the case the player is typing (see draw_input_line): uppercase the
        // completed suffix when the current word's last typed char is uppercase.
        bool up = cw_len > 0 && k.input_len > 0 &&
                  k.input[k.input_len - 1] >= 'A' && k.input[k.input_len - 1] <= 'Z';
        if (ghost_len() > 0)
            for (int i = cw_len; selected->text[i] && k.input_len < KB_INPUT_MAX - 1; i++) {
                char c = selected->text[i];
                if (up && c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
                keyboard_type_char(&k, c);
            }
        if (add_space && k.input_len < KB_INPUT_MAX - 1) keyboard_type_char(&k, ' ');
        sug_index = 0;
    };

    bool at_end = (k.cursor == k.input_len);   // typeahead is active only at line end

    // Insert toggles which arrows move the caret vs cycle suggestions.
    if (ke.kind == SATURN_KEY_INSERT) { g_caret_arrows = !g_caret_arrows; ke.kind = SATURN_KEY_NONE; }

    // Text-caret movement: Ctrl+Left/Right by default, plain Left/Right when toggled.
    bool caret_l = g_caret_arrows ? (ke.kind == SATURN_KEY_LEFT)  : (ke.kind == SATURN_KEY_CTRL_LEFT);
    bool caret_r = g_caret_arrows ? (ke.kind == SATURN_KEY_RIGHT) : (ke.kind == SATURN_KEY_CTRL_RIGHT);
    if (caret_l) keyboard_caret_left(&k);
    if (caret_r) keyboard_caret_right(&k);

    // Suggestion cycling: the Autocomplete chord (default L/R), or whichever arrows
    // aren't moving the caret. Only when the caret is at the end of the line.
    bool kb_prev = g_caret_arrows ? (ke.kind == SATURN_KEY_CTRL_LEFT)  : (ke.kind == SATURN_KEY_LEFT);
    bool kb_next = g_caret_arrows ? (ke.kind == SATURN_KEY_CTRL_RIGHT) : (ke.kind == SATURN_KEY_RIGHT);
    bool cyc_prev = (pad && chord_fired(CA_AUTO, -1)) || kb_prev;
    bool cyc_next = (pad && chord_fired(CA_AUTO, +1)) || kb_next;
    if (at_end && ncand > 0 && cyc_prev) sug_index = (sug_index - 1 + ncand) % ncand;
    if (at_end && ncand > 0 && cyc_next) sug_index = (sug_index + 1) % ncand;
    selected = (at_end && ncand > 0) ? cands[sug_index] : nullptr;
    if (ke.kind == SATURN_KEY_LEFT || ke.kind == SATURN_KEY_RIGHT ||
        ke.kind == SATURN_KEY_CTRL_LEFT || ke.kind == SATURN_KEY_CTRL_RIGHT) ke.kind = SATURN_KEY_NONE;

    // Accept (A): commit the ghost with NO trailing space; with no ghost, submit the
    // line unless it already ends in a space (so a just-typed separator doesn't fire
    // the command). X commits the ghost + a space, or -- no ghost -- types a space to
    // begin the next word. Keyboard Tab keeps the classic accept + space.
    bool a_press   = pad && g_pad->WasPressed(face_button(FA_ACCEPT));
    bool x_press   = pad && pad_fired(Button::X);
    bool has_ghost = selected && ghost_len() > 0;
    if (a_press) {
        if (has_ghost) accept(false);
        else if (k.input_len == 0 || k.input[k.input_len - 1] != ' ') keyboard_submit(&k);
    }
    if (ke.kind == SATURN_KEY_TAB) {
        // Tab completes the suggestion with NO trailing space; if there's nothing
        // left to complete (already at the end of a word), it adds a space.
        if (has_ghost) accept(false);
        else if (at_end && k.input_len > 0 && k.input[k.input_len - 1] != ' ')
            keyboard_type_char(&k, ' ');
        ke.kind = SATURN_KEY_NONE;
    }
    if (x_press) {
        if (has_ghost) accept(true);
        else           keyboard_type_char(&k, ' ');
    }

    // Remaining keyboard events (typing a letter extends/overwrites at the caret).
    if      (ke.kind == SATURN_KEY_CHAR)      keyboard_type_char(&k, ke.ch);
    else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
    else if (ke.kind == SATURN_KEY_DELETE)    keyboard_delete_forward(&k);
    else if (ke.kind == SATURN_KEY_ENTER)     keyboard_submit(&k);
    else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }  // Ctrl+C
    else if (ke.kind == SATURN_KEY_UP)        history_recall(&k, 1);
    else if (ke.kind == SATURN_KEY_DOWN)      history_recall(&k, 0);
    else                                      scroll_handle_key(ke);   // PgUp/Dn/Home/End

    refresh();             // reflect any edit before drawing
    if (k.cursor != k.input_len) selected = nullptr;   // no typeahead unless caret at end
    selected_out = selected;
    cw_len_out = cw_len;
}

// Mark the words on the currently visible console as on-screen, so objects the
// game just described lead their suggestions. Must be re-run after any trie
// rebuild (e.g. a mid-game difficulty change), since the marks live on the words.
static void typeahead_scan_screen(TrieNode *root) {
    char scr[1024]; int sp = 0;
    int total = console_line_count(), rows = console_height();
    int startln = (total > rows) ? (total - rows) : 0;
    for (int li = startln; li < total && sp < (int) sizeof(scr) - 1; li++) {
        const char* ln = console_get_line(li);
        for (int j = 0; ln[j] && sp < (int) sizeof(scr) - 1; j++) scr[sp++] = ln[j];
        if (sp < (int) sizeof(scr) - 1) scr[sp++] = ' ';
    }
    scr[sp] = '\0';
    typeahead_set_screen(root, scr);
}

extern "C" void saturn_readline(char *buf, int maxlen) {
    if (maxlen < 2) { if (maxlen > 0) buf[0] = '\0'; return; }
    // "Load Save Game" queues a one-shot "restore" so the first turn applies the
    // pre-picked save (see g_restore_slot).
    if (g_autocmd != nullptr) {
        const char *c = g_autocmd; g_autocmd = nullptr;
        int n = 0;
        while (c[n] && n < maxlen - 2) { buf[n] = c[n]; n++; }
        buf[n] = '\n'; buf[n + 1] = '\0';
        return;
    }
    ensure_typeahead();                       // decode the game's dictionary/grammar
    typeahead_scan_screen(g_typeahead_root);  // on-screen objects lead suggestions

    // Keep the keyboard state (and cursor position) across prompts, so the picker
    // stays where the player left it instead of jumping back to 'a' every command.
    static KeyboardState k;
    static int kbd_inited = 0;
    if (!kbd_inited) { keyboard_reset(&k); kbd_inited = 1; }
    // Start a fresh input line but preserve cursor_row/cursor_col.
    k.input_len = 0;
    k.input[0] = '\0';
    k.cursor = 0;
    k.submitted = 0;
    // The interpreter runs between reads without refreshing input, so a button
    // still held from dismissing the title (or the previous command's submit) can
    // read as a fresh press here and instantly submit/type. Refresh once to turn
    // any held button into "held", not "just pressed", before we poll.
    SRL::Core::Synchronize();
    int sug_index = 0;           // which suggestion the player has cycled to
    char sug_last[256] = "";     // the typed word the cycle position belongs to
    // The turn's output has now been printed by the interpreter (or is the first
    // room). Position the view at the TOP of that output before the first draw, so a
    // long response shows from its start with "more v" and the player pages down --
    // rather than being dumped at the bottom.
    console_scroll_to_output();
    for (;;) {
      while (!k.submitted) {
        check_soft_reset();   // A+B+C+Start -> back to the title screen
        // Prefer the keyboard: only read the gamepad on frames with no keyboard
        // event, so a keyboard keypress (which also bleeds into pad button bits)
        // doesn't double-trigger. The gamepad still works whenever the keyboard is
        // idle, so on-screen navigation never gets stuck.
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;   // real keyboard in use: hide the on-screen one
        bool pad = (ke.kind == SATURN_KEY_NONE);
        if (pad && g_pad->AnyPressed()) g_kbd_visible = true;    // gamepad in use: show the on-screen keyboard
        pad_repeat_update();   // tick held-button auto-repeat (D-pad, A, C, B, Y, L, R)
        chord_tick();          // tick the configurable shift-chord repeat

        // Start (gamepad) or Esc (keyboard) opens the Options menu mid-game.
        if ((pad && g_pad->WasPressed(Button::START)) || ke.kind == SATURN_KEY_ESCAPE) {
            options_menu();
            ensure_typeahead();                       // rebuild if the difficulty changed
            typeahead_scan_screen(g_typeahead_root);  // re-mark on-screen words on the new trie
            SRL::Core::Synchronize();    // consume the edge that closed the menu
            continue;
        }

        DictionaryWord* selected; int cw_len;
        typeahead_edit(k, g_typeahead_root, sug_index, sug_last, ke, pad, selected, cw_len);

        pad_scroll_update();   // Z+L/R page, Z+D-pad line/Home/End -- with hold repeat
        render_console();
        render_keyboard(k, selected, cw_len);
        SRL::Core::Synchronize();
        sound_service();   // re-trigger looping sounds / reap finished one-shots
        music_tick();      // advance one-shot mixes / commits debounced switches (startup seek-window behavior tracked for branch review)
      }
      // Autocomplete-accept appends a trailing space; strip trailing spaces so the
      // parser, the reboot check, history, and the echo all see the clean command.
      while (k.input_len > 0 && k.input[k.input_len - 1] == ' ') k.input[--k.input_len] = '\0';
      g_scroll = 0;   // snap to the live bottom so the echoed command is visible
      history_push(k.input);   // remember the command for Up/Down recall
      // Echo the entered command onto the game's "> " prompt line so it stays in the
      // scrollback -- there's no OS echo here, and the input widget vanishes on submit.
      console_write(k.input, (unsigned int) k.input_len);
      console_write("\n", 1);
      g_output_start = console_total_lines();   // mark where the next turn's output begins
      render_console();
      if (is_reboot_command(k.input)) {
          confirm_return_to_title("reboot back to the title screen?");   // resets on Yes
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
          SRL::Core::Synchronize();
          continue;   // declined reboot is not passed to the game
      }
      // Quit is intercepted here and never handed to the interpreter, whose quit
      // opcode crashes on Saturn once accepted. Confirming keeps the one "are you
      // sure" step the game itself would have asked -- worth keeping now that "q"
      // is a first-word suggestion a stray accept could pick.
      if (is_quit_command(k.input)) {
          confirm_return_to_title("quit back to the title screen?");     // resets on Yes
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
          SRL::Core::Synchronize();
          continue;   // declined quit must not reach the game either
      }
      break;
    }
    int n = k.input_len;
    if (n > maxlen - 2) n = maxlen - 2;
    for (int i = 0; i < n; i++) buf[i] = k.input[i];
    buf[n]     = '\n';   // opcode_read strips this, matching fgets contract
    buf[n + 1] = '\0';
}

// Re-read the story image from CD (used by opcode_restart). The GFS size read can
// come back garbage, so retry the stat until it reports the expected size, then read
// the whole file. Returns 1 on success, 0 on failure.
extern "C" int saturn_read_story_file(uint8_t *buf, uint32_t len) {
    for (int attempt = 0; attempt < 300; attempt++) {
        SRL::Cd::File f(g_story_filename);
        if (f.Size.SectorSize == 2048 && (uint32_t) f.Size.Bytes == len) {
            if (f.Open()) {
                int32_t got = f.Read((int32_t) len, buf);
                f.Close();
                if (got == (int32_t) len) { return 1; }
            }
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    return 0;
}

extern "C" void saturn_die(const char *fmt, ...) {
    (void) fmt;
    console_write("\n*** interpreter halted ***\n", 28);
    while (1) { render_console(); SRL::Core::Synchronize(); }
}

// ---- save / restore menu ---------------------------------------------------

static void menu_clear_full(void) { for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r); }

static void menu_clear(void) {
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
}

// Wait for any button/key (used for "press any key" prompts).
static void menu_wait(void) {
    SRL::Core::Synchronize();
    for (;;) {
        if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return;
        if (saturn_keyboard_poll().kind != SATURN_KEY_NONE) return;
        SRL::Core::Synchronize();
    }
}

// Modal list menu. Returns the chosen index, or -1 if cancelled. Navigable by
// gamepad (D-pad + A/C/Start, B cancels) or keyboard (number keys pick directly,
// Enter picks the highlighted item, Backspace cancels).
static int menu_select(const char *title, const char *const *items, int count) {
    const int VIS = 20;         // max list rows shown at once; longer lists scroll
    int sel = 0;
    int top = 0;                // index of the first visible row
    SRL::Core::Synchronize();   // consume any stale button/key edge
    for (;;) {
        check_soft_reset();   // A+B+C+Start -> back to the title screen
        if (g_pad->WasPressed(Button::Up))    sel = (sel - 1 + count) % count;
        if (g_pad->WasPressed(Button::Down))  sel = (sel + 1) % count;
        bool pick = g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START);
        bool cancel = g_pad->WasPressed(Button::B);
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (ke.kind == SATURN_KEY_ENTER) pick = true;
        else if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) cancel = true;
        else if (ke.kind == SATURN_KEY_CHAR && ke.ch >= '1' && ke.ch <= '9') {
            int idx = (int) (ke.ch - '1');
            if (idx < count) { sel = idx; pick = true; }
        }
        else if (ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + count) % count;
        else if (ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % count;
        if (cancel) return -1;
        if (pick)   return sel;

        // scroll the window so the selection stays visible
        if (sel < top)             top = sel;
        else if (sel >= top + VIS) top = sel - VIS + 1;
        int last = top + VIS; if (last > count) last = count;

        menu_clear();
        SRL::Debug::Print(2, 2, "%s", title);
        if (top > 0)       SRL::Debug::Print(1, 3, "^ more");
        for (int i = top; i < last; i++)
            SRL::Debug::Print(1, 4 + (i - top), "%c %d) %s", (i == sel) ? '>' : ' ', i + 1, items[i]);
        if (last < count)  SRL::Debug::Print(1, 4 + VIS, "v more");
        SRL::Debug::Print(1, 6 + VIS, "%s",
            hint("pad picks   C=ok   B=back", "dir picks   Enter=ok   Esc=back"));
        SRL::Core::Synchronize();
    }
}

// Basic validation for the dial number: non-empty, digits only.
static bool valid_dialnum(const char *s) {
    if (!s[0]) return false;
    for (int i = 0; s[i]; i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

// Configure MojoZork: edit the server dial number with the on-screen / real
// keyboard. A/Enter accept (after validation); Start/Esc cancel. Both return to
// the Options menu.
static void config_page(void) {
    KeyboardState k; keyboard_reset(&k);
    for (int i = 0; g_dialnum[i] && k.input_len < KB_INPUT_MAX - 1; i++) keyboard_type_char(&k, g_dialnum[i]);
    const char *err = "";
    SRL::Core::Synchronize();
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        bool accept = false, cancel = false;
        if      (ke.kind == SATURN_KEY_CHAR)      keyboard_type_char(&k, ke.ch);
        else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
        else if (ke.kind == SATURN_KEY_ENTER)     accept = true;
        else if (ke.kind == SATURN_KEY_ESCAPE)    cancel = true;
        else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
        else {
            if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
            if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
            if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
            if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
            if (g_pad->WasPressed(Button::C))     keyboard_type(&k);
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
        SRL::Debug::Print(2, 1, "Configure MojoZork");
        SRL::Debug::Print(2, 3, "Server dial number:");
        SRL::Debug::Print(2, 4, "> %s_", k.input);
        for (int r = 0; r < KB_ROWS; r++) {
            char rowbuf[KB_COLS * 2 + 1]; int p = 0;
            for (int c = 0; c < KB_COLS; c++) {
                rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
                rowbuf[p++] = KB_LAYOUT[r][c];
            }
            rowbuf[p] = '\0';
            SRL::Debug::Print(4, 6 + r, "%s", rowbuf);
        }
        if (err[0]) SRL::Debug::Print(2, 11, "%s", err);
        SRL::Debug::Print(2, 13, "%s",
            hint("C=type B=del  A=OK  Start=Cancel", "type number  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
}

// Group 1 tie: give face action `a` button `b`, swapping with whoever held it so
// the three actions always stay a permutation of {A,B,C}.
static void face_assign(int a, int b) {
    for (int o = 0; o < FA_N; o++) if (o != a && g_face_btn[o] == b) g_face_btn[o] = g_face_btn[a];
    g_face_btn[a] = b;
}
// Group 2 tie: give chord action `a` slot `s`; swap if another action holds it,
// otherwise (the free spare) just move.
static void chord_assign(int a, int s) {
    for (int o = 0; o < CA_N; o++) if (o != a && g_chord_slot[o] == s) g_chord_slot[o] = g_chord_slot[a];
    g_chord_slot[a] = s;
}

static const char *const FACE_LABEL[FA_N]  = { "Accept", "Backspace/Cancel", "Type Letter" };
static const char *const CHORD_LABEL[CA_N] = { "Autocomplete", "Recall", "Home/End",
                                               "Line Up/Down", "Cursor Move", "Page Up/Down" };

static void mapping_reset_defaults(void) {
    for (int a = 0; a < FA_N; a++) g_face_btn[a]   = FACE_DEFAULT[a];
    for (int a = 0; a < CA_N; a++) g_chord_slot[a] = CHORD_DEFAULT[a];
}

// Live remap editor: 3 face rows + 6 chord rows, then Reset, OK and Cancel. Up/Down
// pick a row, Left/Right cycle its button/slot (applying the tie rules). OK saves;
// Cancel (and B/Esc) restores the snapshot taken on entry.
static void configure_controls_page(void) {
    SRL::Core::Synchronize();   // consume the edge that opened this
    int s_face[FA_N], s_chord[CA_N];   // snapshot for Cancel
    for (int a = 0; a < FA_N; a++) s_face[a]  = g_face_btn[a];
    for (int a = 0; a < CA_N; a++) s_chord[a] = g_chord_slot[a];
    const int NASSIGN  = FA_N + CA_N;  // assignable rows [0..NASSIGN)
    const int R_RESET  = NASSIGN;      // Reset to Defaults row
    const int R_DONE   = NASSIGN + 1;  // OK row
    const int R_CANCEL = NASSIGN + 2;  // Cancel row
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
        if (back) {   // B/Esc == Cancel: restore snapshot
            for (int a = 0; a < FA_N; a++) g_face_btn[a]   = s_face[a];
            for (int a = 0; a < CA_N; a++) g_chord_slot[a] = s_chord[a];
            break;
        }
        if (up)   sel = (sel - 1 + R_CANCEL + 1) % (R_CANCEL + 1);
        if (down) sel = (sel + 1) % (R_CANCEL + 1);
        if (sel == R_DONE)  { if (act) { options_save(); break; } }         // OK
        else if (sel == R_CANCEL) { if (act) {                             // Cancel
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
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "CONFIGURE CONTROLS"); y += 2;
        SRL::Debug::Print(x, y++, "%s", hint("Left/Right change  A/Start=OK B=Cancel",
                                             "Left/Right change  Enter=OK Esc=Cancel"));
        y++;
        for (int a = 0; a < FA_N; a++) {
            SRL::Debug::Print(x, y, "%c %s", sel == a ? '>' : ' ', FACE_LABEL[a]);
            SRL::Debug::Print(x + 20, y++, "%s", face_btn_name(a));
        }
        for (int a = 0; a < CA_N; a++) {
            SRL::Debug::Print(x, y, "%c %s", sel == FA_N + a ? '>' : ' ', CHORD_LABEL[a]);
            SRL::Debug::Print(x + 20, y++, "%s", slot_name(g_chord_slot[a]));
        }
        SRL::Debug::Print(x + 2, y, "Caps Toggle");
        SRL::Debug::Print(x + 20, y++, "L+R (fixed)");
        y++;
        SRL::Debug::Print(x, y++, "%c Reset to Defaults", sel == R_RESET ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c OK", sel == R_DONE ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c Cancel", sel == R_CANCEL ? '>' : ' ');
        menu_sync();
    }
    SRL::Core::Synchronize();
}

// Controller page: shows the live mapping and offers Configure (remap editor),
// the on-screen keyboard's CapsLock toggle, and Done.
static void controls_page(void) {
    SRL::Core::Synchronize();   // consume the edge that opened this
    int sel = 0;                // 0 = Configure, 1 = Caps, 2 = Done
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
        if (sel == 0 && act) configure_controls_page();
        else if (sel == 1 && (left || right || act)) keyboard_set_caps(!keyboard_get_caps());
        else if (sel == 2 && act) break;

        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "GAMEPAD CONTROLS"); y += 2;
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
        SRL::Debug::Print(x, y++, "%c Configure Mapping", sel == 0 ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c Keyboard Caps: %s", sel == 1 ? '>' : ' ',
                          keyboard_get_caps() ? "On" : "Off");
        SRL::Debug::Print(x, y++, "%c Done", sel == 2 ? '>' : ' ');
        menu_sync();
    }
    SRL::Core::Synchronize();
}

// Physical-keyboard settings page (shown in Options while a real keyboard is the
// device in hand). Toggles: which arrows move the caret vs cycle suggestions (the
// same flag the Insert key flips), insert-vs-overwrite typing, and CapsLock.
static void keyboard_controls_page(void) {
    SRL::Core::Synchronize();   // consume the edge that opened this
    int s_arrows = g_caret_arrows, s_ins = keyboard_get_insert(),   // snapshot for Cancel
        s_caps = keyboard_get_caps(), s_num = keyboard_get_num();
    const int N = 6;            // 0 Arrows, 1 Insert, 2 Caps, 3 Num, 4 OK, 5 Cancel
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
        if (back) {   // B/Esc == Cancel: restore snapshot
            g_caret_arrows = s_arrows; keyboard_set_insert(s_ins);
            keyboard_set_caps(s_caps); keyboard_set_num(s_num);
            break;
        }
        bool toggle = left || right || act;
        if      (sel == 0 && toggle) g_caret_arrows = !g_caret_arrows;
        else if (sel == 1 && toggle) keyboard_set_insert(!keyboard_get_insert());
        else if (sel == 2 && toggle) keyboard_set_caps(!keyboard_get_caps());
        else if (sel == 3 && toggle) keyboard_set_num(!keyboard_get_num());
        else if (sel == 4 && act) { options_save(); break; }   // OK
        else if (sel == 5 && act) {                            // Cancel
            g_caret_arrows = s_arrows; keyboard_set_insert(s_ins);
            keyboard_set_caps(s_caps); keyboard_set_num(s_num); break; }

        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "KEYBOARD CONTROLS"); y += 2;
        SRL::Debug::Print(x, y++, "Insert key also flips Arrows;");
        SRL::Debug::Print(x, y++, "Ctrl+Left/Right always move caret.");
        y++;
        SRL::Debug::Print(x, y, "%c Arrows move", sel == 0 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", g_caret_arrows ? "Caret" : "Suggestions");
        SRL::Debug::Print(x, y, "%c Insert mode", sel == 1 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_insert() ? "On (insert)" : "Off (overwrite)");
        SRL::Debug::Print(x, y, "%c Caps Lock", sel == 2 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_caps() ? "On" : "Off");
        SRL::Debug::Print(x, y, "%c Num Lock", sel == 3 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_num() ? "On" : "Off");
        y++;
        SRL::Debug::Print(x, y++, "%c OK", sel == 4 ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c Cancel", sel == 5 ? '>' : ' ');
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("A/Start=OK  B=Cancel", "Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}

// Sound Options (full-screen, OK/Cancel). Rows: Audio Mix, Track,
// Music level, PCM level, OK, Cancel. Start/A = OK (commit+save+apply), Esc/B =
// Cancel (restore snapshot incl. live audio). Previews play live while open.
static void sound_options_page(void) {
    static const char *const MIX[] = { "Dynamic", "Repeat", "Sequential", "Random" };
    const int N = 6;   // 0 Mix, 1 Track, 2 Music, 3 PCM, 4 OK, 5 Cancel
    int sel = 0;
    // Snapshot for Cancel.
    int s_mix = g_mix_mode, s_trk = g_sel_track, s_mus = g_music_level, s_pcm = g_pcm_level;
    const unsigned char* atracks; int an = music_cdda_audio_tracks(&atracks);
    int aidx = 0;                     // current index into the audio-track list
    for (int i = 0; i < an; i++) if (atracks[i] == g_sel_track) { aidx = i; break; }
    if (an > 0 && atracks[aidx] != g_sel_track) g_sel_track = atracks[aidx];   // no match: snap to first
    SRL::Core::Synchronize();   // consume the edge that opened this
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + N) % N;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % N;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool ok   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                  || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool cancel = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                    || ke.kind == SATURN_KEY_BACKSPACE;

        if (cancel) {   // revert everything, incl. live audio
            g_mix_mode = s_mix; g_sel_track = s_trk; g_music_level = s_mus; g_pcm_level = s_pcm;
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track);
            music_refresh();
            break;
        }
        if (sel == 0) { if (left && g_mix_mode > 0) g_mix_mode--; if (right && g_mix_mode < MIX_RANDOM) g_mix_mode++; }
        else if (sel == 1) {
            if (an > 0) {
                if (left  && aidx > 0)      aidx--;
                if (right && aidx < an - 1) aidx++;
                g_sel_track = atracks[aidx];
                if (left || right || ok) music_cdda_play(g_sel_track);   // demo/preview
            }
        }
        else if (sel == 2) { if (left && g_music_level > 0) g_music_level--; if (right && g_music_level < 7) g_music_level++;
                             if (left || right) music_set_volume(g_music_level); }
        else if (sel == 3) { if (left && g_pcm_level > 0) g_pcm_level--; if (right && g_pcm_level < 7) g_pcm_level++;
                             if (left || right) sound_set_level(g_pcm_level); }
        else if (ok && sel == 4) {   // OK
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track);
            if (g_mix_mode == MIX_DYNAMIC) music_refresh();   // hand back to the room engine
            else music_start();                               // start the chosen track now
            options_save();
            break;
        }
        else if (ok && sel == 5) {   // Cancel row == B
            g_mix_mode = s_mix; g_sel_track = s_trk; g_music_level = s_mus; g_pcm_level = s_pcm;
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track); music_refresh();
            break;
        }

        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "SOUND OPTIONS"); y += 2;
        SRL::Debug::Print(x, y, "%c Audio Mix", sel == 0 ? '>' : ' ');
        SRL::Debug::Print(x + 14, y++, "%s %s %s", g_mix_mode > 0 ? "<" : " ", MIX[g_mix_mode], g_mix_mode < MIX_RANDOM ? ">" : " ");
        SRL::Debug::Print(x, y, "%c Track", sel == 1 ? '>' : ' ');
        if (an > 0) SRL::Debug::Print(x + 14, y++, "%d  (A=demo)", aidx + 1);   // 1-based
        else        SRL::Debug::Print(x + 14, y++, "no audio on disc");
        SRL::Debug::Print(x, y, "%c Music", sel == 2 ? '>' : ' ');
        SRL::Debug::Print(x + 14, y++, "%d", g_music_level);
        SRL::Debug::Print(x, y, "%c PCM", sel == 3 ? '>' : ' ');
        SRL::Debug::Print(x + 14, y++, "%d", g_pcm_level);
        y++;
        SRL::Debug::Print(x, y++, "%c OK", sel == 4 ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c Cancel", sel == 5 ? '>' : ' ');
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("<> change  A/Start=OK  B=Cancel", "<> change  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}

// Options menu (centered box): a difficulty slider plus actions (Configure,
// Controls, Sound Options, Return to Title, Done). Up/Down select a row; on
// Difficulty, Left/Right change it; A/Enter activate other rows; B/Esc close.
// Audio settings now live on the dedicated Sound Options page. Difficulty is
// written to backup only if the player actually changed it.
static void options_menu(void) {
    static const char *const NAMES[] = { "Easy", "Medium", "Hard" };
    static const char *const DESC[]  = { "Walkthrough steps only",
                                         "Valid-command typeahead",
                                         "Typeahead off" };
    const int x0 = 5, y0 = 8, w = 30, h = 15;
    const int NITEMS = 6;   // 0=Diff 1=Configure 2=Controls 3=Sound 4=Return 5=Done
    int diff = g_difficulty, sel = 0;
    SRL::Core::Synchronize();   // consume the edge that opened this
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + NITEMS) % NITEMS;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % NITEMS;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        if (sel == 0) { if (left && diff > DIFF_EASY) diff--; if (right && diff < DIFF_HARD) diff++; }
        bool act = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                  || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) break;
        if (act) {
            if (sel == 1) { config_page(); }
            else if (sel == 2) { if (g_kbd_visible) controls_page(); else keyboard_controls_page(); menu_clear_full(); }
            else if (sel == 3) { sound_options_page(); menu_clear_full(); }
            else if (sel == 4) {   // Return to Title Screen (soft reset; never returns on Yes)
                if (menu_confirm("Return to the title screen?", "Are you sure?")) {
                    if (diff != g_difficulty) { g_difficulty = diff; options_save(); }
                    soft_reset_to_title();
                }
            }
            else if (sel == 5) break;   // Done  (sel==0 Difficulty: activate is a no-op)
        }

        for (int r = 0; r < h; r++) {
            char line[40]; int p = 0;
            for (int c = 0; c < w; c++)
                line[p++] = (r == 0 || r == h - 1) ? ((c == 0 || c == w - 1) ? '+' : '-')
                          : ((c == 0 || c == w - 1) ? '|' : ' ');
            line[p] = '\0';
            SRL::Debug::Print(x0, y0 + r, "%s", line);
        }
        SRL::Debug::Print(x0 + 11, y0 + 1, "OPTIONS");
        SRL::Debug::Print(x0 + 2, y0 + 3, "%c Difficulty: %s %s %s", sel == 0 ? '>' : ' ',
                          diff > DIFF_EASY ? "<" : " ", NAMES[diff], diff < DIFF_HARD ? ">" : " ");
        SRL::Debug::Print(x0 + 2, y0 + 4, "    %s", DESC[diff]);
        SRL::Debug::Print(x0 + 2, y0 + 6, "%c Configure MojoZork", sel == 1 ? '>' : ' ');
        SRL::Debug::Print(x0 + 2, y0 + 7, "%c %s", sel == 2 ? '>' : ' ',
                          g_kbd_visible ? "Gamepad Controls" : "Keyboard Controls");
        SRL::Debug::Print(x0 + 2, y0 + 8,  "%c Sound Options", sel == 3 ? '>' : ' ');
        SRL::Debug::Print(x0 + 2, y0 + 9,  "%c Return to Title Screen", sel == 4 ? '>' : ' ');
        SRL::Debug::Print(x0 + 2, y0 + 10, "%c Done", sel == 5 ? '>' : ' ');
        SRL::Debug::Print(x0 + 2, y0 + 13, "%s", hint("Up/Dn A=pick  <>=diff", "Up/Dn Enter  B=back"));
        menu_sync();
    }
    bool diff_changed = (diff != g_difficulty);
    g_difficulty = diff;
    if (diff_changed) options_save();
    // Wait for the closing button to be released so it doesn't leak into the
    // editor (B would backspace, A/C/Start would submit) the moment we return.
    while (g_pad->IsHeld(Button::B) || g_pad->IsHeld(Button::A) ||
           g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::START))
        menu_sync();
}

// Per-game backup filename for a slot: the story's base name (uppercased, up to
// 9 chars, extension dropped) + the slot digit. Gives each game its own 5 slots.
static void make_slot_name(char *out, int slot) {
    int i = 0;
    for (const char *p = g_story_filename; *p && *p != '.' && i < 9; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        out[i++] = c;
    }
    out[i++] = (char) ('0' + slot);
    out[i] = 0;
}

// Pick a backup device (cartridge only when inserted). Returns the device id, or
// -1 if cancelled.
static int choose_device(const char *title) {
    const char *dev_items[2];
    int dev_ids[2];
    int ndev = 0;
    dev_items[ndev] = "Console (internal)"; dev_ids[ndev] = SATURN_BUP_CONSOLE; ndev++;
    if (saturn_bup_present(SATURN_BUP_CARTRIDGE)) {
        dev_items[ndev] = "Cartridge"; dev_ids[ndev] = SATURN_BUP_CARTRIDGE; ndev++;
    }
    int d = menu_select(title, dev_items, ndev);
    return (d < 0) ? -1 : dev_ids[d];
}

// Combined save-slot picker + in-place name editor. Shows all slots for the
// device; the player picks one (C/Enter/number), then edits THAT slot's name
// right in the list -- the cursor stays on the line they picked. Backspace/B
// while editing goes back to slot selection; A/Enter confirms. Returns 1 with
// *out_slot/out_name set, or 0 if cancelled. out_name is empty if left blank.
static int pick_slot_and_name(int device, int *out_slot, char *out_name, int maxchars) {
    char slotname[SAVE_SLOTS][12];
    for (int i = 0; i < SAVE_SLOTS; i++) {
        char fn[12];
        make_slot_name(fn, i);
        if (!saturn_bup_info(device, fn, slotname[i])) slotname[i][0] = '\0';
    }

    int sel = 0;
    int editing = 0;
    KeyboardState k;
    keyboard_reset(&k);
    SRL::Core::Synchronize();   // consume the device-pick edge

    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);

        if (!editing) {
            bool pick = false, cancel = false;
            if (ke.kind == SATURN_KEY_ENTER) pick = true;
            else if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) cancel = true;
            else if (ke.kind == SATURN_KEY_CHAR && ke.ch >= '1' && ke.ch < (char) ('1' + SAVE_SLOTS)) {
                sel = (int) (ke.ch - '1'); pick = true;
            } else {
                if (g_pad->WasPressed(Button::Up))   sel = (sel - 1 + SAVE_SLOTS) % SAVE_SLOTS;
                if (g_pad->WasPressed(Button::Down)) sel = (sel + 1) % SAVE_SLOTS;
                if (g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) pick = true;
                if (g_pad->WasPressed(Button::B)) cancel = true;
            }
            if (cancel) return 0;
            if (pick) {
                keyboard_reset(&k);
                for (int i = 0; slotname[sel][i] && k.input_len < maxchars; i++)
                    keyboard_type_char(&k, slotname[sel][i]);
                if (k.input_len > 0) {   // picker on the last char of the pre-filled name
                    char last = k.input[k.input_len - 1];
                    for (int r = 0; r < KB_ROWS; r++)
                        for (int c = 0; c < KB_COLS; c++)
                            if (KB_LAYOUT[r][c] == last) { k.cursor_row = r; k.cursor_col = c; }
                }
                editing = 1;
                SRL::Core::Synchronize();
                continue;
            }
        } else {
            bool submit = false;
            if (ke.kind == SATURN_KEY_ENTER) submit = true;
            else if (ke.kind == SATURN_KEY_ESCAPE) { editing = 0; SRL::Core::Synchronize(); continue; }
            else if (ke.kind == SATURN_KEY_CLEAR) { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }  // Ctrl+C
            else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
            else if (ke.kind == SATURN_KEY_CHAR) { if (k.input_len < maxchars) keyboard_type_char(&k, ke.ch); }
            else {
                if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
                if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
                if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
                if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
                if (g_pad->WasPressed(Button::C))     { if (k.input_len < maxchars) keyboard_type(&k); }
                if (g_pad->WasPressed(Button::X))     { if (k.input_len < maxchars) keyboard_type_char(&k, ' '); }
                if (g_pad->WasPressed(Button::B))     { editing = 0; SRL::Core::Synchronize(); continue; }
                if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::START)) submit = true;
            }
            if (submit) {
                int n = k.input_len;
                if (n > maxchars) n = maxchars;
                for (int i = 0; i < n; i++) out_name[i] = k.input[i];
                out_name[n] = '\0';
                *out_slot = sel;
                return 1;
            }
        }

        // Same slot menu either way; the picked line becomes editable in place.
        menu_clear();
        SRL::Debug::Print(2, 1, editing ? "Name this save:" : "Save - pick a slot:");
        for (int i = 0; i < SAVE_SLOTS; i++) {
            char mark = (i == sel) ? '>' : ' ';
            if (editing && i == sel) {
                SRL::Debug::Print(2, 3 + i, "%c %d) %s_", mark, i + 1, k.input);
            } else {
                const char *label = slotname[i][0] ? slotname[i] : "(empty)";
                SRL::Debug::Print(2, 3 + i, "%c %d) %s", mark, i + 1, label);
            }
        }
        if (!editing) {
            SRL::Debug::Print(2, 4 + SAVE_SLOTS, "%s",
                hint("pad picks   C=edit   B=back", "num picks   Enter=edit   Esc=back"));
        } else {
            for (int r = 0; r < KB_ROWS; r++) {
                char rowbuf[KB_COLS * 2 + 1];
                int p = 0;
                for (int c = 0; c < KB_COLS; c++) {
                    rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
                    rowbuf[p++] = KB_LAYOUT[r][c];
                }
                rowbuf[p] = '\0';
                SRL::Debug::Print(4, 4 + SAVE_SLOTS + r, "%s", rowbuf);
            }
            SRL::Debug::Print(2, 5 + SAVE_SLOTS + KB_ROWS, "%s",
                hint("C=type X=space  B=back  A=OK", "type name  Esc=back  Enter=OK"));
        }
        SRL::Core::Synchronize();
    }
}

// Yes/no confirmation. Returns true if confirmed (C / A / Start / Enter / Y),
// false if declined (B / N).
static bool menu_confirm(const char *line1, const char *line2) {
    SRL::Core::Synchronize();   // consume the edge that got us here
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (ke.kind == SATURN_KEY_ENTER) return true;
        if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) return false;
        if (ke.kind == SATURN_KEY_CHAR) {
            if (ke.ch == 'y' || ke.ch == 'Y') return true;
            if (ke.ch == 'n' || ke.ch == 'N') return false;
        } else {
            if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START))
                return true;
            if (g_pad->WasPressed(Button::B)) return false;
        }
        menu_clear();
        SRL::Debug::Print(2, 3, "%s", line1);
        if (line2 && line2[0]) SRL::Debug::Print(2, 4, "%s", line2);
        SRL::Debug::Print(2, 6, "%s",
            hint("A / C = Yes     B = No", "Enter = Yes     Esc = No"));
        menu_sync();
    }
}

// Pick a backup device (cartridge only if inserted) then a slot (showing each
// slot's existing save comment). Returns 1 with *out_device/*out_slot set, or 0.
static int choose_dest(const char *title_dev, const char *title_slot,
                       int *out_device, int *out_slot) {
    int device = choose_device(title_dev);
    if (device < 0) return 0;

    static char labels[SAVE_SLOTS][40];
    const char *slot_items[SAVE_SLOTS];
    for (int i = 0; i < SAVE_SLOTS; i++) {
        char name[12];
        make_slot_name(name, i);
        char comment[12];
        if (saturn_bup_info(device, name, comment)) snprintf(labels[i], sizeof(labels[i]), "%s", comment);
        else                                        snprintf(labels[i], sizeof(labels[i]), "(empty)");
        slot_items[i] = labels[i];
    }
    int slot = menu_select(title_slot, slot_items, SAVE_SLOTS);
    if (slot < 0) return 0;

    *out_device = device;
    *out_slot = slot;
    return 1;
}

extern "C" int saturn_save_blob(const uint8_t *data, uint32_t len) {
    int device = choose_device("SAVE - device?");
    if (device < 0) return 0;

    // Pick a slot and edit its name in place (cursor stays on the chosen line).
    int slot;
    char comment[12];
    if (!pick_slot_and_name(device, &slot, comment, 8)) return 0;
    if (comment[0] == 0) snprintf(comment, sizeof(comment), "Save %d", slot + 1);

    // Confirm before overwriting an existing save in that slot.
    char name[12];
    make_slot_name(name, slot);
    char existing[12];
    if (saturn_bup_info(device, name, existing)) {
        char q[40];
        snprintf(q, sizeof(q), "Overwrite \"%s\"?", existing);
        if (!menu_confirm(q, "Are you sure?")) return 0;
    }

    int ok = saturn_bup_write(device, name, comment, data, len);
    menu_clear();
    SRL::Debug::Print(2, 4, "%s", ok ? "Saved." : "Save FAILED (no space?).");
    SRL::Debug::Print(2, 6, "(press any key/button)");
    menu_wait();
    return ok;
}

extern "C" int saturn_load_blob(uint8_t *buf, uint32_t maxlen) {
    (void) maxlen;
    int device, slot;
    if (g_restore_slot >= 0) {                 // slot pre-picked via "Load Save Game"
        device = g_restore_device; slot = g_restore_slot;
        g_restore_device = -1; g_restore_slot = -1;   // one-shot
    } else if (!choose_dest("RESTORE - device?", "RESTORE - slot?", &device, &slot)) {
        return 0;
    }
    char name[12];
    make_slot_name(name, slot);
    int ok = saturn_bup_read(device, name, buf);
    if (!ok) {
        menu_clear();
        SRL::Debug::Print(2, 4, "No save in that slot.");
        SRL::Debug::Print(2, 6, "(press any key/button)");
        menu_wait();
    }
    return ok;
}

// ---- boot ------------------------------------------------------------------

// Title screen: wait for any face button; use elapsed frames as an RNG seed.
// Draw the title art (no prompt). Shown on its own during the catalog load, then
// again by title_and_seed with the "Press any button" prompt on the same screen.
static void title_draw_art(void) {
    SRL::Debug::Print(12, 12, "M O J O Z O R K");
    SRL::Debug::Print(4, 15, "Saturn port (c) 2026 by Suinevere");
}

static int title_and_seed(void) {
    int frames = 0;
    int reset_hold = 0;
    // Wipe the debug layer: after a soft reset the previous game's text is still on
    // screen and would show through the (non-clearing) title draw below.
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
    SRL::Core::Synchronize();   // poll real input once before reading it (avoid boot garbage)
    for (;;) {
        // Software reset while already on the title screen: per the Sega spec, send
        // the player out to the system (BIOS / CD player) via the SMPC reset. This
        // is the one place a hardware reset is correct -- in-game it returns here.
        // Debounced like check_soft_reset so a boot-time garbage read can't fire it.
        reset_hold = soft_reset_chord_held() ? (reset_hold + 1) : 0;
        if (reset_hold >= SOFT_RESET_HOLD) {
            slNMIRequest();
            while (1) {}
        }
        // Advance on any gamepad face button or any keyboard key. Polling the
        // keyboard here (rather than a raw "key down" check) also records the
        // held key so it doesn't immediately type into the first prompt.
        bool advance =
            g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START) ||
            (saturn_keyboard_poll().kind != SATURN_KEY_NONE);
        if (advance) break;
        title_draw_art();
        SRL::Debug::Print(8, 18, "Press any button to begin");
        SRL::Core::Synchronize();
        frames++;
    }
    return frames | 1;   // avoid a zero seed
}

// ---- game selection: scan the CD "Z3" folder for *.Z3 story files ----------

// True if `s` ends in ".z3" / ".Z3".
static int has_z3_ext(const char *s) {
    int len = 0; while (s[len]) len++;
    if (len < 3) return 0;
    const char *e = s + len - 3;
    return e[0] == '.' && (e[1] == 'z' || e[1] == 'Z') && e[2] == '3';
}

// Scan the CD "Z3" directory and collect up to `max` *.Z3 filenames into `out`.
// Also makes Z3 the current CD directory so later SRL::Cd::File() opens resolve
// there. Returns the number of matches (0 if none), or -1 if the Z3 folder is
// absent. Saturn/ISO9660: names are uppercase 8.3 (GFS_FNAME_LEN = 12), possibly
// with a ";1" version suffix, which is stripped. Directory records "." / ".."
// carry non-".Z3" names and are naturally filtered out.
static int scan_z3_folder(char out[][16], int max) {
    static GfsDirName dirnames[SRL_MAX_CD_FILES];
    static GfsDirTbl  tbl;

    // GFS_SetDir below makes Z3 the current CD directory, and that persists. After a
    // soft reset we re-enter here still inside Z3, so "Z3" would resolve relative to
    // Z3 and fail. Return to the root directory first so the scan is idempotent.
    SRL::Cd::ChangeDir((char *) nullptr);

    int32_t fid = GFS_NameToId((int8_t *) "Z3");
    if (fid < 0) return -1;

    GFS_DIRTBL_TYPE(&tbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&tbl) = dirnames;
    GFS_DIRTBL_NDIR(&tbl)    = SRL_MAX_CD_FILES;

    int32_t count = GFS_LoadDir(fid, &tbl);
    if (count < 0) return -1;
    GFS_SetDir(&tbl);   // subsequent File() opens resolve inside Z3

    int n = 0;
    for (int i = 0; i < count && n < max; i++) {
        char nm[16];
        int j = 0;
        for (; j < GFS_FNAME_LEN && j < 15; j++) {
            char c = (char) dirnames[i].fname[j];
            if (c == '\0' || c == ';') break;   // stop at NUL or version suffix
            nm[j] = c;
        }
        nm[j] = '\0';
        if (has_z3_ext(nm)) {
            int k = 0;
            for (; nm[k] && k < 15; k++) out[n][k] = nm[k];
            out[n][k] = '\0';
            n++;
        }
    }
    return n;
}

// Read a game's header (release 0x02 + serial 0x12) from the CD. Returns its
// display title (NULL if unknown/unreadable) and sets *cat to its category
// (GAME_CAT_OTHER if unknown). Reads one sector.
static const char* read_game_info(const char* filename, int* cat) {
    static uint8_t hdr[64];
    *cat = GAME_CAT_OTHER;
    // One sector-addressed LoadBytes instead of Size-stat + Open + Read + Close:
    // fewer GFS calls per file (faster over ~25 games) and it sidesteps the
    // flaky first-access GFS_GetFileSize the old Size check had to retry around.
    for (int attempt = 0; attempt < 8; attempt++) {
        SRL::Cd::File f(filename);
        int32_t got = f.LoadBytes(0, (int32_t) sizeof(hdr), hdr);   // header lives in sector 0
        if (got >= 0x1a) {
            if (hdr[0] != 3) return nullptr;                        // read ok, not a v3 story
            unsigned short rel = (unsigned short)((hdr[2] << 8) | hdr[3]);
            const char* serial = (const char*)(hdr + 0x12);
            *cat = game_category(rel, serial);
            return game_title(rel, serial);
        }
        for (int i = 0; i < 4; i++) SRL::Core::Synchronize();
    }
    return nullptr;
}

static const char *const CAT_NAMES[GAME_CAT_COUNT] = {
    "The Zork Universe", "The Planetfall Series", "The Mystery Series",
    "Tales of Adventure & Fantasy", "Sci-Fi & Horror", "Comedy", "Other",
};

// Release year parsed from a label's trailing "(YYYY)" (e.g. "Zork I (1980)").
// Returns 9999 when there's no 4-digit year, so undated games sort after dated
// ones (then alphabetically among themselves) per the menu's sort order.
// Lexicographic compare, <0 / 0 / >0 like strcmp (avoids a <string.h> dependency).
static int label_cmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int label_year(const char* s) {
    int n = 0; while (s[n]) n++;
    if (n >= 6 && s[n-6] == '(' && s[n-1] == ')') {
        int y = 0;
        for (int k = n-5; k <= n-2; k++) {
            if (s[k] < '0' || s[k] > '9') return 9999;
            y = y * 10 + (s[k] - '0');
        }
        return y;
    }
    return 9999;
}

// Game catalog cache: filled once by preload_game_catalog() at title time.
// game_select's browse loop reads these directly and does no CD I/O.
static const int MAX_GAMES = 32;       // headroom for the full Infocom Z3 catalogue
static char names[MAX_GAMES][16];      // static so the returned pointer stays valid
static char labels[MAX_GAMES][40];     // display titles (or filename fallback)
static int  cats[MAX_GAMES];
static int  g_catalog_count = 0;
static bool g_catalog_ready = false;

// Read the Z3 folder + each game's header ONCE (these CD reads stop CD-DA); cache
// the result so game_select does no CD I/O and the menu track plays uninterrupted.
static void preload_game_catalog(void) {
    if (g_catalog_ready) return;
    g_catalog_count = scan_z3_folder(names, MAX_GAMES);
    if (g_catalog_count > 0) {
        for (int i = 0; i < g_catalog_count; i++) {
            const char* title = read_game_info(names[i], &cats[i]);
            int j = 0; const char* src = title ? title : names[i];
            for (; src[j] && j < 39; j++) labels[i][j] = src[j];
            labels[i][j] = '\0';
        }
    }
    g_catalog_ready = true;
}

const char* game_select() {
    const char *items[MAX_GAMES];

    preload_game_catalog();   // idempotent: the CD reads happen once, at the title
    int count = g_catalog_count;

    if (count <= 0) {
        menu_clear();
        SRL::Debug::Print(2, 2, "%s", (count < 0)
            ? "No Z3 folder found on the CD."
            : "No .Z3 games found in the Z3 folder.");
        SRL::Debug::Print(2, 4, "(press any key/button to go back)");
        menu_wait();
        return nullptr;   // back to the single/multiplayer select menu
    }

    // Category page -> game page. Back from the game page returns to categories.
    for (;;) {
        int catmap[GAME_CAT_COUNT], ncat = 0;   // menu index -> category id
        for (int c = 0; c < GAME_CAT_COUNT; c++) {
            int any = 0;
            for (int i = 0; i < count && !any; i++) if (cats[i] == c) any = 1;
            if (any) { items[ncat] = CAT_NAMES[c]; catmap[ncat] = c; ncat++; }
        }
        int cs = (ncat == 1) ? 0 : menu_select("Choose a category:", items, ncat);
        if (cs < 0) return nullptr;   // B/Esc at categories: back to the mode menu

        int gmap[MAX_GAMES], ng = 0;   // menu index -> game index
        for (int i = 0; i < count; i++) if (cats[i] == catmap[cs]) gmap[ng++] = i;
        // Sort within the category by release year (from the label's "(YYYY)"),
        // then alphabetically by title (also breaks ties among undated games).
        for (int a = 1; a < ng; a++) {
            int key = gmap[a], ya = label_year(labels[key]);
            int b = a - 1;
            while (b >= 0) {
                int yb = label_year(labels[gmap[b]]);
                if (yb < ya || (yb == ya && label_cmp(labels[gmap[b]], labels[key]) <= 0)) break;
                gmap[b+1] = gmap[b]; b--;
            }
            gmap[b+1] = key;
        }
        for (int i = 0; i < ng; i++) items[i] = labels[gmap[i]];
        int gs = menu_select(CAT_NAMES[catmap[cs]], items, ng);
        if (gs < 0) { if (ncat == 1) return nullptr; else continue; }   // back to categories
        return names[gmap[gs]];       // the caller loads by filename, not the label
    }
}

// ---- online mode (multizork telnet terminal) -------------------------------


#define ONLINE_DIAL_ATTEMPTS 3   // auto-redial count (modem carrier training is flaky)

// True if the player wants to abort: Esc on the Saturn keyboard, or the L+R
// trigger chord on the gamepad (both triggers unused for typing).
static bool online_cancel_requested(void) {
    if (saturn_keyboard_poll().kind == SATURN_KEY_ESCAPE) return true;
    return g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R);
}

// Wait for any button/key, used on terminal error screens.
static void online_wait_any(void) {
    for (;;) {
        if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return;
        if (saturn_keyboard_poll().kind != SATURN_KEY_NONE) return;
        SRL::Core::Synchronize();
    }
}

// Wait until input has released AND settled before the terminal starts reading it,
// so nothing spurious is submitted to the server on connect. Powering the NetLink
// modem over the SMPC reboots the controllers/keyboard (they re-init from EEPROM),
// so for a short window after connect their peripheral reports can be stale or
// garbage; reading input during that window decodes the garbage as phantom
// keypresses that get typed and submitted as a scrambled line. Guard against it by
// requiring a sustained fully-idle streak -- covering both the raw held-state and
// the *decoded* key event the loop actually consumes -- after a minimum settle,
// while draining the keyboard decoder each frame so no stale repeat leaks through.
static void online_settle_input(void) {
    const int MIN_FRAMES  = 45;    // ~0.75s: let the rebooted peripherals stabilize
    const int IDLE_NEEDED = 10;    // consecutive fully-idle frames required to accept
    const int MAX_FRAMES  = 300;   // ~5s cap so we never hang if input never quiets
    int frames = 0, idle = 0;
    while ((frames < MIN_FRAMES || idle < IDLE_NEEDED) && frames < MAX_FRAMES) {
        SRL::Core::Synchronize();
        frames++;
        bool busy =
            saturn_keyboard_poll().kind != SATURN_KEY_NONE   // also drains decoder state
            || saturn_keyboard_any_down() != 0
            || g_pad->IsHeld(Button::A) || g_pad->IsHeld(Button::B)
            || g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::X)
            || g_pad->IsHeld(Button::START) || g_pad->IsHeld(Button::Up)
            || g_pad->IsHeld(Button::Down) || g_pad->IsHeld(Button::Left)
            || g_pad->IsHeld(Button::Right);
        idle = busy ? 0 : (idle + 1);
    }
}

// Typeahead for the online terminal. multizork is Zork I, so build the trie from
// the local ZORK1.Z3 on the CD (same dictionary + grammar + solution overlay).
// Built once, before dialing (so the CD isn't touched mid-connection); the story
// bytes are freed afterward since the trie is self-contained.
static TrieNode* g_online_ta = nullptr;
static int g_online_diff = -1;
static void ensure_online_typeahead(void) {
    if (g_online_ta != nullptr && g_online_diff == g_difficulty) return;
    if (g_online_ta) { destroy_typeahead(g_online_ta); g_online_ta = nullptr; }
    g_online_ta = create_trie_node();
    g_online_diff = g_difficulty;
    if (g_difficulty == DIFF_HARD) return;      // typeahead off
    char names[1][16];
    if (scan_z3_folder(names, 1) < 0) return;   // no Z3 folder -> empty trie (no suggestions)
    uint8_t* story = nullptr; uint32_t len = 0;
    for (int attempt = 0; attempt < 40 && story == nullptr; attempt++) {
        SRL::Cd::File f("ZORK1.Z3");
        int32_t bytes = f.Size.Bytes, ssz = f.Size.SectorSize;
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t* buf = (uint8_t*) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf != nullptr && f.Open()) {
                int32_t got = f.Read(bytes, buf); f.Close();
                if (got == bytes) { story = buf; len = (uint32_t) bytes; break; }
            }
            if (buf != nullptr) SRL::Memory::HighWorkRam::Free(buf);
        }
        music_cdda_play(g_sel_track);
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();;
    }
    if (story != nullptr) {
        build_typeahead_from_story(g_online_ta, story, len);
        int have_solution = (g_difficulty != DIFF_HARD)
                          ? apply_solution_overlay(g_online_ta, story, len) : 0;
        typeahead_add_abbreviations(g_online_ta);
        typeahead_set_easy(g_difficulty == DIFF_EASY, have_solution);
        SRL::Memory::HighWorkRam::Free(story);
    }
}

// Connect to the multizork server and run the telnet terminal until the link
// drops or the player quits, then return to the mode menu. Auto-redials a few
// times because the NetLink<->DreamPi carrier handshake is probabilistic.
static void online_mode(void) {
    ensure_online_typeahead();   // load the Zork I vocabulary before the modem is up
    // The vocab load's CD reads (first online session) stop CD-DA; start a fresh track
    // so the dialer and the whole online session play music instead of going silent.
    music_cdda_play(g_sel_track);
    const char *number = g_dialnum;   // change it in Options -> Configure MojoZork

    // ---- connect, with auto-redial on carrier-training failure ----
    net_connect_result_t rc = NET_DIAL_FAIL;
    for (int attempt = 1; attempt <= ONLINE_DIAL_ATTEMPTS; attempt++) {
        for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
        SRL::Debug::Print(2, 4, "Dialing %s ... (attempt %d/%d)",
                          number, attempt, ONLINE_DIAL_ATTEMPTS);
        SRL::Debug::Print(2, 6, "%s", hint("L+R = cancel", "Esc = cancel"));
        SRL::Core::Synchronize();

        rc = net_connect_open(number);        // blocking (~35s timeout on failure)
        if (rc == NET_OK) break;
        if (rc == NET_NO_MODEM) break;        // hardware missing; redial won't help

        // NET_DIAL_FAIL: brief pause (lets the DreamPi return to idle), then retry.
        if (attempt < ONLINE_DIAL_ATTEMPTS) {
            for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
            SRL::Debug::Print(2, 4, "No carrier. Retrying...");
            SRL::Debug::Print(2, 6, "%s", hint("L+R = cancel", "Esc = cancel"));
            bool cancelled = false;
            for (int f = 0; f < 180; f++) {   // ~3s at 60Hz
                if (online_cancel_requested()) { cancelled = true; break; }
                SRL::Core::Synchronize();
            }
            if (cancelled) { net_connect_close(); return; }
        }
    }

    if (rc != NET_OK) {
        for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
        SRL::Debug::Print(2, 4, "%s",
            rc == NET_NO_MODEM ? "NetLink modem not found." : "Connection failed.");
        SRL::Debug::Print(2, 6, "(press any button)");
        online_wait_any();
        return;
    }

    const cui_transport_t *tr = net_connect_transport();
    TermState ts; term_init(&ts);
    KeyboardState k; keyboard_reset(&k);
    console_init();
    online_settle_input();      // wait for input to release + settle after the
    keyboard_reset(&k);         // modem's SMPC power-on, then clear any residue

    // ---- terminal loop ----
    // L and R now page the scrollback, so switching page direction briefly holds
    // both. Require the L+R disconnect chord to be held deliberately (~0.75s) so a
    // quick direction change doesn't drop the connection.
    const int LR_DISCONNECT_HOLD = 45;
    int lr_hold = 0;
    int sug_index = 0;           // suggestion cycle position
    char sug_last[256] = "";     // the typed word the cycle position belongs to
    int last_scan_lines = -1;    // rescan on-screen words only when output grows
    for (;;) {
        term_service(&ts, tr, MOJOZORK_RX_BUDGET);   // RX -> console

        // Mark on-screen objects when the server prints new lines (room text etc.).
        int lc = console_line_count();
        if (lc != last_scan_lines) {
            last_scan_lines = lc;
            char scr[1024]; int sp = 0;
            int rows = console_height();
            int startln = (lc > rows) ? (lc - rows) : 0;
            for (int li = startln; li < lc && sp < (int) sizeof(scr) - 1; li++) {
                const char* ln = console_get_line(li);
                for (int j = 0; ln[j] && sp < (int) sizeof(scr) - 1; j++) scr[sp++] = ln[j];
                if (sp < (int) sizeof(scr) - 1) scr[sp++] = ' ';
            }
            scr[sp] = '\0';
            typeahead_set_screen(g_online_ta, scr);
        }

        check_soft_reset();   // A+B+C+Start -> back to the title screen
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;   // real keyboard in use: hide the on-screen one
        bool pad = (ke.kind == SATURN_KEY_NONE);
        if (pad && g_pad->AnyPressed()) g_kbd_visible = true;    // gamepad in use: show the on-screen keyboard
        pad_repeat_update();   // held-button auto-repeat (D-pad, A, C, B, Y, L, R)
        chord_tick();          // tick the configurable shift-chord repeat

        // Esc (keyboard) or a sustained L+R hold (gamepad) disconnects.
        bool lr = g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R);
        lr_hold = lr ? (lr_hold + 1) : 0;
        if (ke.kind == SATURN_KEY_ESCAPE || lr_hold >= LR_DISCONNECT_HOLD) {
            console_write("\n*** disconnected ***\n", 22);
            render_console();
            SRL::Core::Synchronize();
            break;
        }

        DictionaryWord* selected; int cw_len;
        typeahead_edit(k, g_online_ta, sug_index, sug_last, ke, pad, selected, cw_len);
        pad_scroll_update();   // Z+L/R page, Z+D-pad line/Home/End -- with hold repeat

        bool did_submit = k.submitted;
        if (k.submitted) {
            g_scroll = 0;   // streaming terminal: sending a line returns to the live bottom
            history_push(k.input);   // remember the command for Up/Down recall
            if (is_reboot_command(k.input)) {
                confirm_return_to_title("reboot back to the title screen?");  // resets on Yes
                keyboard_reset(&k);                // declined: don't send to server
                online_settle_input();
            } else {
                term_submit_line(tr, &k);          // echo + send line; resets keyboard
            }
        }

        if (!cui_transport_is_connected(tr)) {
            console_write("\n*** connection lost ***\n", 25);
            render_console();
            SRL::Core::Synchronize();
            break;
        }

        render_console();
        render_keyboard(k, did_submit ? nullptr : selected, did_submit ? 0 : cw_len);
        SRL::Debug::Print(0, 28, "%s", hint("L/R=cycle  hold L+R=disconnect", "Esc=disconnect"));
        SRL::Core::Synchronize();
    }
    net_connect_close();
}

int main(void) {
    SRL::Core::Initialize(HighColor::Colors::Black);
    saturn_bup_init();
    options_load();   // restore saved difficulty (defaults to Easy)

    static MultiPad pads;
    g_pad = &pads;

    // Soft reset (A+B+C+Start, or the typed "reboot") longjmps back here to restart
    // at the title screen. The longjmp skips the normal unwinding, so the CD file
    // system can be left holding open handles / stale state from the previous run;
    // GFS_Reset() returns it to its clean post-init state (all handles closed,
    // current directory back to root) so every restart reloads from a known-good
    // state. The story image itself is owned by the Z-machine (initStory frees the
    // previous GState->story on the next boot), so we must NOT free it here -- doing
    // so double-frees it and corrupts the heap.
    setjmp(g_title_jmp);
    g_title_jmp_armed = true;
    g_reboot_menu = false;
    GFS_Reset();
    console_init();

    // Clear engine statics before the menu track. A soft reset longjmps here with
    // stale state (g_active_track > 0, backend still registered); zeroing it stops
    // a menu-frame music_tick() from firing the loop-end branch and leaking a stale
    // game track into the menu. The reset's internal stop is overridden on the next
    // line, and this is safe on first boot too (no backend registered yet).
    music_reset();                       // clear stale engine state (also on soft-reset re-entry)

    // Show the title art first (no prompt), then load the game catalog while it is on
    // screen, then start the menu music and let title_and_seed add the prompt on the
    // same screen. The catalog's CD reads (Z3 folder scan + each game's header) stop
    // CD-DA -- the Saturn's single drive head cannot play audio while reading data --
    // so the load window is briefly silent by necessity. Doing all CD reads here means
    // no menu screen from here on (title, categories, games) ever touches the CD again,
    // so the menu track then plays uninterrupted. On soft-reset re-entry the preload is
    // a no-op (already cached), so no read happens and the music starts cleanly.
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
    title_draw_art();
    SRL::Core::Synchronize();

    preload_game_catalog();              // CD reads happen once, here

    music_set_level(g_music_level);      // honor the saved music level for menu audio
    music_cdda_play(g_sel_track);        // start the menu track; no CD reads remain in the menu flow

    int seed = title_and_seed();         // redraws the same art + "Press any button", waits

    // Top-level mode choice. "Play Online" runs the multizork telnet terminal
    // and returns here on disconnect; "Play Local" falls through to the offline
    // Z-machine flow below.
    static const char *modes[] = { "Play Local (single player)", "Play Online (multizork)",
                                   "Load Save Game", "Options" };
    const char* game_file = nullptr;

    for (;;) {
        int mode = menu_select("MojoZork", modes, 4);
        if (mode < 0) continue;   // Back at the root menu: nowhere to go up, so stay here
        if (mode == 3) { options_menu(); continue; }
        if (mode == 1) { online_mode(); continue; }
        if (mode == 2) {   // Load Save Game: pick a game, then one of its save slots.
            game_file = game_select();
            if (game_file == nullptr) continue;
            g_story_filename = game_file;   // so the slot names resolve to this game
            int device, slot;
            if (!choose_dest("LOAD - device?", "LOAD - slot?", &device, &slot)) continue;
            g_restore_device = device; g_restore_slot = slot;
            g_autocmd = "restore";          // the first turn applies the picked save
            break;
        }
        // Play Local (or B at this top menu): pick a game. Pressing B on the game
        // menu returns nullptr, so we loop back to this mode menu instead of trying
        // to load a bogus story file.
        game_file = game_select();
        if (game_file == nullptr) continue;
        break;
    }
    g_story_filename = game_file;   // save/restart must re-read the selected game

    // Load *.Z3 from CD. GFS_GetFileSize (used by SRL::Cd::File's ctor) can
    // return an uninitialized size on first access, and File::Read relies on that
    // size for both its work-buffer and its read bound. So retry the stat until it
    // reports a sane size (2048-byte data sectors, plausible length), then allocate
    // exactly that and read the whole file. A wrong size corrupts the story buffer
    // and makes the interpreter crash / report bogus "Out of memory".
    uint8_t *story = nullptr;
    uint32_t len = 0;
    for (int attempt = 0; attempt < 300 && story == nullptr; attempt++) {
        SRL::Cd::File f(game_file);
        int32_t bytes = f.Size.Bytes;
        int32_t ssz   = f.Size.SectorSize;
        SRL::Debug::Print(1, 26, "loading %s...           ", game_file);
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t *buf = (uint8_t *) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf != nullptr && f.Open()) {
                int32_t got = f.Read(bytes, buf);
                f.Close();
                if (got == bytes) { story = buf; len = (uint32_t) bytes; break; }
            }
            if (buf != nullptr) { SRL::Memory::HighWorkRam::Free(buf); }
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    if (story == nullptr) { saturn_die("Could not load %s from CD",game_file); }

    mojo_boot(story, len, seed);   // initStory takes ownership; it frees this on the next boot

    {   // enable sound if the game ships a sibling <base>.BLB
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
        blb[i] = '.'; blb[i+1] = 'B'; blb[i+2] = 'L'; blb[i+3] = 'B'; blb[i+4] = '\0';
        sound_init(blb);
        sound_set_level(g_pcm_level);     // honor the saved PCM level
        music_set_level(g_music_level);   // and the saved music level
        music_set_backend(music_cdda_play_mode);
        music_set_isplaying(music_cdda_is_playing);
        music_set_isshort(music_cdda_is_short);
        music_set_game((unsigned int)((story[2] << 8) | story[3]), (const char*) (story + 0x12));
        music_seed((unsigned int) seed);
        music_reset();                         // clear room cache for the new game
        music_set_mix(g_mix_mode, g_sel_track);
        music_start();                         // non-Dynamic starts now; Dynamic waits for first room
    }

    g_output_start = console_total_lines();   // mark before the first room; readline positions on it
    mojo_run();

    // Game ended: keep the final screen up.
    while (1) { render_console(); SRL::Core::Synchronize(); }
    return 0;
}
