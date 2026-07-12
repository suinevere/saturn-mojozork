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
static int g_sound_on = 1;

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
    if (story != nullptr && len > 0 && g_difficulty != DIFF_HARD) {
        build_typeahead_from_story(g_typeahead_root, story, len);           // grammar layer
        if (g_difficulty == DIFF_EASY)
            apply_solution_overlay(g_typeahead_root, story, len);          // winning-path boosts
    }
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
    // Byte just past the dial number's NUL is the sound flag: 1 = off; 0
    // (absent, old blob) or 2 = on.
    if (i + 1 < (int) sizeof(buf)) g_sound_on = (buf[i + 1] == 1) ? 0 : 1;
}
static void options_save(void) {
    uint8_t buf[64]; int n = 0;
    buf[n++] = (uint8_t) g_difficulty;
    for (int i = 0; g_dialnum[i] && n < 62; i++) buf[n++] = (uint8_t) g_dialnum[i];
    buf[n++] = 0;
    buf[n++] = (uint8_t) (g_sound_on ? 2 : 1);   // 0 = absent (old blob) -> on
    saturn_bup_write(SATURN_BUP_CONSOLE, "MOJOOPTS", "options", buf, (uint32_t) n);
}
static void options_menu(void);   // defined below, near the other menus

// ---- scrollback ------------------------------------------------------------

// How many lines the view is scrolled up from the live bottom (0 = latest text).
// render_console clamps this to the available range every frame, so callers can
// bump it freely. Driven by the physical keyboard's nav keys (the on-screen
// keyboard cursor is moved by the gamepad D-pad instead).
static int g_scroll = 0;
static const int SCROLL_PAGE = 16;        // Page Up/Down jump size, in lines
static const int SCROLL_ALL  = 1 << 30;   // Home: clamped down to the oldest line

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

// Gamepad scrollback, mirroring the keyboard's nav keys with press-and-hold
// auto-repeat. The L/R triggers page up/down. Z acts as a shift so the D-pad
// controls the history instead of the on-screen keyboard cursor: Z+Up/Down move
// one line, Z+Left/Right jump to the oldest/newest line (Home/End). Call once per
// input frame. The on-screen keyboard cursor moves on the plain D-pad (no Z).
static const int PAD_SCROLL_DELAY = 30;   // frames before auto-repeat kicks in
static const int PAD_SCROLL_RATE  = 4;    // frames between repeats while held
static void pad_scroll_update(void) {
    static int last_action = 0;
    static int timer = 0;

    // All scrolling lives under the Z shift now, so plain L/R is free to cycle
    // typeahead suggestions. Z+L/R page, Z+Up/Down line, Z+Left/Right home/end.
    int action = 0;
    if (g_pad->IsHeld(Button::Z)) {
        if      (g_pad->IsHeld(Button::L))     action = 1;  // page up  (older)
        else if (g_pad->IsHeld(Button::R))     action = 2;  // page down (newer)
        else if (g_pad->IsHeld(Button::Up))    action = 3;  // line up
        else if (g_pad->IsHeld(Button::Down))  action = 4;  // line down
        else if (g_pad->IsHeld(Button::Left))  action = 5;  // home (oldest)
        else if (g_pad->IsHeld(Button::Right)) action = 6;  // end  (newest)
    }
    if (action == 0) { last_action = 0; timer = 0; return; }

    bool emit;
    if (action != last_action) { last_action = action; timer = PAD_SCROLL_DELAY; emit = true; }
    else if (--timer <= 0)     { timer = PAD_SCROLL_RATE; emit = true; }
    else                       { emit = false; }
    if (!emit) return;

    switch (action) {
        case 1: g_scroll += SCROLL_PAGE; break;
        case 2: g_scroll -= SCROLL_PAGE; break;
        case 3: g_scroll += 1;           break;
        case 4: g_scroll -= 1;           break;
        case 5: g_scroll  = SCROLL_ALL;  break;
        case 6: g_scroll  = 0;           break;
    }
}

// True while the Z shift is held: the D-pad is driving scrollback, so callers
// should not also move the on-screen keyboard cursor with it.
static bool pad_scroll_shift(void) { return g_pad->IsHeld(Button::Z); }

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
    { Button::C, 0, false }, { Button::B, 0, false }, { Button::Y, 0, false },
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
}

// older != 0 -> previous (older) command; older == 0 -> newer, clearing past the
// newest back to an empty line. No-op when there's no history.
static void history_recall(KeyboardState *k, int older) {
    if (g_hist_count == 0) return;
    if (older) {
        if (g_hist_browse < g_hist_count - 1) { g_hist_browse++; history_load(k); }
    } else {
        if (g_hist_browse > 0) { g_hist_browse--; history_load(k); }
        else { g_hist_browse = -1; k->input_len = 0; k->input[0] = '\0'; }
    }
}

static void render_console(void) {
    int rows = console_height();
    int total = console_line_count();
    int maxstart = (total > rows) ? (total - rows) : 0;
    if (g_scroll < 0)        g_scroll = 0;
    if (g_scroll > maxstart) g_scroll = maxstart;   // clamp to oldest
    int start = maxstart - g_scroll;
    for (int r = 0; r < rows; r++) {
        SRL::Debug::PrintClearLine(TOP_MARGIN + r);
        int li = start + r;
        if (li < total) {
            SRL::Debug::Print(0, TOP_MARGIN + r, "%s", console_get_line(li));
        }
    }
    // Edge markers when there's off-screen text above/below the window.
    if (start > 0)            SRL::Debug::Print(39, TOP_MARGIN, "^");
    if (start + rows < total) SRL::Debug::Print(39, TOP_MARGIN + rows - 1, "v");
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
    if (prediction && k.input_len < KB_INPUT_MAX - 1)
        suffix = prediction->text + current_word_len;
    SRL::Debug::Print(0, row, "> %s%s", k.input, suffix);

    int cursor_col = 2 + k.input_len;   // 2 = width of the "> " prompt
    char under = suffix[0] ? suffix[0] : ' ';
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
            rowbuf[p++] = KB_LAYOUT[r][c];
        }
        rowbuf[p] = '\0';
        SRL::Debug::PrintClearLine(row + 1 + r);
        SRL::Debug::Print(2, row + 1 + r, "%s", rowbuf);
    }
    SRL::Debug::Print(0, row + 1 + KB_ROWS, "C=type A=accept Y=space L/R=cycle Start=go");
}

// ---- global reboot command -------------------------------------------------

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

// Modal Y/N confirm. On Yes, soft-resets to the title screen in-process (the same
// return-to-title as the A+B+C+Start chord); configured options in backup RAM are
// retained. On No, returns false so the caller resumes.
static bool reboot_confirm_and_maybe_reset(void) {
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
        // and the just-echoed "reboot"; the prompt band below is fully cleared each
        // frame so nothing bleeds through in either keyboard or controller mode.
        render_console();
        for (int r = SCREEN_ROWS - REBOOT_MENU_ROWS; r <= 28; r++) SRL::Debug::PrintClearLine(r);
        SRL::Debug::Print(2, 22, "reboot back to the title screen?");
        SRL::Debug::Print(1, 24, "%s", hint("(A) (C) (Start) = yes", "Y / Enter = yes"));
        SRL::Debug::Print(1, 26, "%s", hint("(B) = no", "N / Esc = no"));
        SRL::Core::Synchronize();
    }
}

// ---- hooks (extern "C" so the C core can call them) ------------------------

extern "C" void saturn_writestr(const char *str, size_t slen) {
    console_write(str, (unsigned int) slen);
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
        if (g_pad->IsHeld(Button::X)) {                 // X + Up/Down recalls history
            if (pad_fired(Button::Up))    history_recall(&k, 1);
            if (pad_fired(Button::Down))  history_recall(&k, 0);
        } else if (!pad_scroll_shift()) {               // plain D-pad moves the picker
            if (pad_fired(Button::Up))    keyboard_move(&k, 0, -1);
            if (pad_fired(Button::Down))  keyboard_move(&k, 0,  1);
            if (pad_fired(Button::Left))  keyboard_move(&k, -1, 0);
            if (pad_fired(Button::Right)) keyboard_move(&k,  1, 0);
        }
        if (pad_fired(Button::C)) keyboard_type(&k);       // C = type letter
        if (pad_fired(Button::B)) keyboard_backspace(&k);  // B = delete
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
        if (ghost_len() > 0)
            for (int i = cw_len; selected->text[i] && k.input_len < KB_INPUT_MAX - 1; i++)
                keyboard_type_char(&k, selected->text[i]);
        if (add_space && k.input_len < KB_INPUT_MAX - 1) keyboard_type_char(&k, ' ');
        sug_index = 0;
    };

    // L/R (shoulders) or keyboard Left/Right cycle through the suggestions.
    bool cyc_prev = (pad && !pad_scroll_shift() && pad_fired(Button::L)) || ke.kind == SATURN_KEY_LEFT;
    bool cyc_next = (pad && !pad_scroll_shift() && pad_fired(Button::R)) || ke.kind == SATURN_KEY_RIGHT;
    if (ncand > 0 && cyc_prev) sug_index = (sug_index - 1 + ncand) % ncand;
    if (ncand > 0 && cyc_next) sug_index = (sug_index + 1) % ncand;
    selected = ncand > 0 ? cands[sug_index] : nullptr;
    if (ke.kind == SATURN_KEY_LEFT || ke.kind == SATURN_KEY_RIGHT) ke.kind = SATURN_KEY_NONE;

    // Accept: A or Tab commit the ghost (Tab only on a keyboard; space is literal
    // there). No ghost -> A submits. Gamepad Y accepts then appends a space.
    bool a_press = pad && g_pad->WasPressed(Button::A);
    if (a_press || ke.kind == SATURN_KEY_TAB) {
        if (ghost_len() > 0) accept(false);
        else if (a_press)    keyboard_submit(&k);
        ke.kind = SATURN_KEY_NONE;
    } else if (pad && pad_fired(Button::Y)) {
        accept(true);
    }

    // Remaining keyboard events (typing a letter extends the prefix).
    if      (ke.kind == SATURN_KEY_CHAR)      keyboard_type_char(&k, ke.ch);
    else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
    else if (ke.kind == SATURN_KEY_ENTER)     keyboard_submit(&k);
    else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; }  // Ctrl+C
    else if (ke.kind == SATURN_KEY_UP)        history_recall(&k, 1);
    else if (ke.kind == SATURN_KEY_DOWN)      history_recall(&k, 0);
    else                                      scroll_handle_key(ke);   // PgUp/Dn/Home/End

    refresh();             // reflect any edit before drawing
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
    k.submitted = 0;
    // The interpreter runs between reads without refreshing input, so a button
    // still held from dismissing the title (or the previous command's submit) can
    // read as a fresh press here and instantly submit/type. Refresh once to turn
    // any held button into "held", not "just pressed", before we poll.
    SRL::Core::Synchronize();
    int sug_index = 0;           // which suggestion the player has cycled to
    char sug_last[256] = "";     // the typed word the cycle position belongs to
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
        pad_repeat_update();   // tick held-button auto-repeat (D-pad, C, B, Y, L, R)

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
      }
      g_scroll = 0;   // a submitted line returns the view to the live bottom
      history_push(k.input);   // remember the command for Up/Down recall
      // Echo the entered command onto the game's "> " prompt line so it stays in the
      // scrollback -- there's no OS echo here, and the input widget vanishes on submit.
      console_write(k.input, (unsigned int) k.input_len);
      console_write("\n", 1);
      render_console();
      if (is_reboot_command(k.input)) {
          reboot_confirm_and_maybe_reset();   // soft-resets on Yes; returns on No
          k.input_len = 0; k.input[0] = '\0'; k.submitted = 0;
          SRL::Core::Synchronize();
          continue;   // declined reboot is not passed to the game
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
        else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; }
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
            hint("C=type B=del  A=save  Start=cancel", "type number  Enter=save  Esc=cancel"));
        SRL::Core::Synchronize();
    }
}

// Options menu (centered box): a difficulty slider, a sound toggle, plus
// actions. Up/Down select a row; on Difficulty or Sound, Left/Right change it
// (Sound also toggles on A/Enter); A/Enter activate other rows; B/Esc close.
// Difficulty/sound are written to backup only if the player changed them.
static void options_menu(void) {
    static const char *const NAMES[] = { "Easy", "Medium", "Hard" };
    static const char *const DESC[]  = { "Full typeahead + hints",
                                         "Typeahead, no hints",
                                         "Typeahead off" };
    const int x0 = 5, y0 = 8, w = 30, h = 12;
    const int NITEMS = 5;   // 0=Difficulty 1=Configure 2=Return to Title 3=Sound 4=Done
    int diff = g_difficulty, sel = 0;
    int sound0 = g_sound_on;   // to detect a change at close, like diff below
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
        if (sel == 3 && (left || right || act)) {
            g_sound_on = !g_sound_on;   // Sound row: Left/Right/A/Enter all toggle
        } else if (act) {
            if (sel == 1) { config_page(); }
            else if (sel == 2) {   // Return to Title Screen (never returns)
                if (diff != g_difficulty) { g_difficulty = diff; options_save(); }
                soft_reset_to_title();
            }
            else if (sel == 4) break;   // Done  (sel==0 Difficulty: activate is a no-op)
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
        SRL::Debug::Print(x0 + 2, y0 + 7, "%c Return to Title Screen", sel == 2 ? '>' : ' ');
        SRL::Debug::Print(x0 + 2, y0 + 8, "%c Sound: %s", sel == 3 ? '>' : ' ', g_sound_on ? "On" : "Off");
        SRL::Debug::Print(x0 + 2, y0 + 9, "%c Done", sel == 4 ? '>' : ' ');
        SRL::Debug::Print(x0 + 2, y0 + 10, "%s", hint("Up/Dn A=pick  <>=diff", "Up/Dn Enter  B=back"));
        SRL::Core::Synchronize();
    }
    bool diff_changed = (diff != g_difficulty);
    g_difficulty = diff;
    if (diff_changed || g_sound_on != sound0) options_save();
    sound_set_enabled(g_sound_on);
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
            else if (ke.kind == SATURN_KEY_CLEAR) { k.input_len = 0; k.input[0] = '\0'; }  // Ctrl+C
            else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
            else if (ke.kind == SATURN_KEY_CHAR) { if (k.input_len < maxchars) keyboard_type_char(&k, ke.ch); }
            else {
                if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
                if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
                if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
                if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
                if (g_pad->WasPressed(Button::C))     { if (k.input_len < maxchars) keyboard_type(&k); }
                if (g_pad->WasPressed(Button::Y))     { if (k.input_len < maxchars) keyboard_type_char(&k, ' '); }
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
                hint("C=type Y=space  B=back  A=OK", "type name  Esc=back  Enter=OK"));
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
            if (g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return true;
            if (g_pad->WasPressed(Button::B)) return false;
        }
        menu_clear();
        SRL::Debug::Print(2, 3, "%s", line1);
        if (line2 && line2[0]) SRL::Debug::Print(2, 4, "%s", line2);
        SRL::Debug::Print(2, 6, "%s",
            hint("A / C = Yes     B = No", "Enter = Yes     Esc = No"));
        SRL::Core::Synchronize();
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
        SRL::Debug::Print(12, 12, "M O J O Z O R K");
        SRL::Debug::Print(4, 15, "Saturn port (c) 2026 by Suinevere");
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
    static uint8_t hdr[2048];
    *cat = GAME_CAT_OTHER;
    for (int attempt = 0; attempt < 8; attempt++) {
        SRL::Cd::File f(filename);
        int32_t bytes = f.Size.Bytes, ssz = f.Size.SectorSize;
        if (ssz == 2048 && bytes >= 0x1a) {
            if (f.Open()) {
                int32_t got = f.Read(2048, hdr);
                f.Close();
                if (got >= 0x1a && hdr[0] == 3) {
                    unsigned short rel = (unsigned short)((hdr[2] << 8) | hdr[3]);
                    const char* serial = (const char*)(hdr + 0x12);
                    *cat = game_category(rel, serial);
                    return game_title(rel, serial);
                }
                return nullptr;
            }
        }
        for (int i = 0; i < 4; i++) SRL::Core::Synchronize();
    }
    return nullptr;
}

static const char *const CAT_NAMES[GAME_CAT_COUNT] = {
    "The Zork Universe", "The Planetfall Series", "The Mystery Series",
    "Tales of Adventure & Fantasy", "Sci-Fi & Horror", "Comedy", "Other",
};

const char* game_select() {
    const int MAX_GAMES = 32;       // headroom for the full Infocom Z3 catalogue
    static char names[MAX_GAMES][16];   // static so the returned pointer stays valid
    static char labels[MAX_GAMES][40];  // display titles (or filename fallback)
    static int  cats[MAX_GAMES];
    const char *items[MAX_GAMES];
    int count = scan_z3_folder(names, MAX_GAMES);

    if (count <= 0) {
        menu_clear();
        SRL::Debug::Print(2, 2, "%s", (count < 0)
            ? "No Z3 folder found on the CD."
            : "No .Z3 games found in the Z3 folder.");
        SRL::Debug::Print(2, 4, "(press any key/button to go back)");
        menu_wait();
        return nullptr;   // back to the single/multiplayer select menu
    }

    // Classify each game: title (or filename fallback) + category.
    for (int i = 0; i < count; i++) {
        const char* title = read_game_info(names[i], &cats[i]);
        int j = 0;
        const char* src = title ? title : names[i];
        for (; src[j] && j < 39; j++) labels[i][j] = src[j];
        labels[i][j] = '\0';
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
        for (int i = 0; i < count; i++) if (cats[i] == catmap[cs]) { items[ng] = labels[i]; gmap[ng] = i; ng++; }
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
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    if (story != nullptr) {
        build_typeahead_from_story(g_online_ta, story, len);
        if (g_difficulty == DIFF_EASY) apply_solution_overlay(g_online_ta, story, len);
        SRL::Memory::HighWorkRam::Free(story);
    }
}

// Connect to the multizork server and run the telnet terminal until the link
// drops or the player quits, then return to the mode menu. Auto-redials a few
// times because the NetLink<->DreamPi carrier handshake is probabilistic.
static void online_mode(void) {
    ensure_online_typeahead();   // load the Zork I vocabulary before the modem is up
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
        pad_repeat_update();   // held-button auto-repeat (D-pad, C, B, Y, L, R)

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
            g_scroll = 0;   // sending a line returns the view to the live bottom
            history_push(k.input);   // remember the command for Up/Down recall
            if (is_reboot_command(k.input)) {
                reboot_confirm_and_maybe_reset();  // soft-resets on Yes; returns on No
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

    int seed = title_and_seed();

    // Top-level mode choice. "Play Online" runs the multizork telnet terminal
    // and returns here on disconnect; "Play Local" falls through to the offline
    // Z-machine flow below.
    static const char *modes[] = { "Play Local (single player)", "Play Online (multizork)",
                                   "Load Save Game", "Options" };
    const char* game_file = nullptr;

    for (;;) {
        int mode = menu_select("MojoZork", modes, 4);
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
        SRL::Debug::Print(2, 26, "loading %s...           ", game_file);
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
        sound_set_enabled(g_sound_on);   // honor a saved "off" from the first prompt
    }

    mojo_run();

    // Game ended: keep the final screen up.
    while (1) { render_console(); SRL::Core::Synchronize(); }
    return 0;
}
