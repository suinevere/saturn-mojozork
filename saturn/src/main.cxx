#include <srl.hpp>
#include <setjmp.h>

extern "C" {
#include "console.h"
#include "keyboard.h"
#include "display.h"
#include "menu_layout.h"
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

// Display colors (Options > Display). Applied to VDP2 by display_apply();
// persisted in MOJOOPTS alongside the other options.
static DisplayState g_display;

// Online dial number (editable in Options -> Network; persisted).
// Server dial number. 11 digits is the longest we accept (NANP country code
// plus number), and the entry field is capped to match: the on-screen keyboard
// buffer holds 63 characters, which does not fit the 40-column screen and used
// to run over the Network page's right border.
#define DIALNUM_MAX 11
static char g_dialnum[DIALNUM_MAX + 1] = "199403";

// "Load Save Game": a save slot pre-selected from the menu, applied by the first
// in-game "restore" (queued via g_autocmd) instead of the choose_dest prompt.
static int g_restore_device = -1;
static int g_restore_slot   = -1;
static const char *g_autocmd = nullptr;   // command auto-submitted on the next readline
// The slot a save/restore last actually committed to, remembered for the session so
// the quick keys (F5/F6/F9) can skip the pickers. -1 until one commits.
static int g_last_device = -1;
static int g_last_slot   = -1;
// Pre-picked save destination, the mirror of g_restore_* on the save side: set by
// quick-save so saturn_save_blob writes straight to the slot. One-shot.
static int g_save_device = -1;
static int g_save_slot   = -1;

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


#define ZATURN_DIAL_NUMBER "199403"

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

// Console text rows currently available; the input line sits on the next row down.
// Shown: reserve input + KB_ROWS keyboard rows + a hint row. Hidden: just input.
static int console_height(void) {
    int avail = SCREEN_ROWS - TOP_MARGIN;
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
static bool title_bg_show(const char *file);
static void title_bg_show(void);
static void title_bg_hide(void);


// Set the color the SGL font glyphs and the block cursor render in.
//
// The font lives in ASCII palette 0, not palette 1. colorBank's declarator
// initializes it to 1 << 12 (srl_ascii.hpp:23), but Core::Initialize ->
// VDP2::Initialize calls ASCII::SetPalette(0) (srl_vdp2.hpp:1517) before any
// of our code runs, and nothing here calls SetPalette again. NBG3 is
// COL_TYPE_16 (4bpp), so palette 0 is CRAM entries 0-15, bytes 0-31.
//
// Two entries matter, and they are not adjacent:
//
//   entry 1  -- the glyph foreground. VDP2::Initialize seeds it via
//               SetPrintPaletteColor(0, White), which writes 1 + (index << 8)
//               (srl_vdp2.hpp:1489). Its other six calls (index 1..6) land on
//               entries 257, 513, ... which a 4bpp cell cannot reach, so index
//               0 is the only one that colors anything.
//   entry 15 -- the cursor. install_block_glyph() fills its tile with 0xFF,
//               and 4bpp pixel value 15 selects entry 15.
//
// SRL::ASCII::SetColor is not usable for the glyphs: it indexes from
// (colorBank >> 6), which is 0 here, so SetColor(c, i) writes entry i. That
// reaches the cursor at i=15 but never the glyphs, which is why changing Text
// previously appeared to do nothing.
//
// VDP2_COLRAM (sl_def.h:981) is a bare integer address, not a pointer, so the
// cast is required. It reaches this file via <srl.hpp>. The address is in the
// SH-2's uncached mirror, so no flush is needed; the only DMA into CRAM
// (CRAM::Palette::Load) targets bank 1 at entries 256+ and never overlaps.
static void text_set_color(unsigned short rgb555) {
    volatile unsigned short *cram = (volatile unsigned short *) VDP2_COLRAM;
    cram[1]  = rgb555;   // glyph foreground
    cram[15] = rgb555;   // install_block_glyph()'s cursor tile
}

// Push g_display to the hardware. text_set_color writes both the glyph and the
// cursor CRAM entries, so this recolors body text, menus, the on-screen
// keyboard, and the cursor in one call.
// (SRL::Debug::PrintColorSet is not usable here: it sets slCurColor while
// Debug::Print reads ASCII::colorBank.)
// Returns false when the requested background could not be shown, so a caller
// cycling through presets can step over the bad one instead of settling on the
// fallback this installs.
static bool display_apply(void) {
    text_set_color(display_text_rgb(g_display.text));
    // Set the back plane before any image load. It is what shows through the
    // menu frames, whose interiors are transparent NBG3 cells, and it is on
    // screen for the second or two the CD read takes -- so the colour has to be
    // right before the picture arrives, not after. An image preset pairs with
    // black, which is why stepping onto one resets the background and text.
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
    if (display_is_image(&g_display)) {
        if (!title_bg_show(display_image_file(g_display.image))) {
            // Load failed (or the bitmap isn't the 8bpp shape we require): fall
            // back to a color background rather than leaving static on screen.
            //
            // The palette may itself be the image preset that just failed --
            // keeping it would re-select the broken picture. Drop to a color
            // preset in that case.
            int p = g_display.palette;
            if (p >= DISP_PRESET_N || p < 0) p = 12;   // IBM PC (MDA), the startup default
            g_display.palette = p;
            g_display.bg      = display_preset_bg(p);
            g_display.text    = display_preset_text(p);
            g_display.image   = DISP_IMAGE_NONE;
            text_set_color(display_text_rgb(g_display.text));
            title_bg_hide();
            SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
            return false;
        }
    } else {
        title_bg_hide();
    }
    return true;
}

// Which Display Options row a cycle applies to. Named separately from the
// page's own row enum because the stepping logic lives out here, next to
// display_apply.
enum DisplayCycleRow { DCR_PALETTE, DCR_BG, DCR_TEXT };

// Cycle one row and push it to the hardware, stepping over any image that will
// not load.
//
// Only the Palette row can hit that: it is the one carrying pictures now.
// The step matters there because display_apply() installs a colour-preset
// fallback when a load fails, and that rewrites the very index being cycled --
// without restoring it, the next press would resume from the fallback and land
// on the same bad image, making every image past it unreachable.
static void display_cycle_row(DisplayCycleRow which, int dir) {
    if (which != DCR_PALETTE) {
        if (which == DCR_BG) display_cycle_bg(&g_display, dir);
        else                 display_cycle_text(&g_display, dir);
        display_apply();     // colours only; nothing here can fail to load
        return;
    }
    int tries = display_palette_count();
    while (tries-- > 0) {
        display_cycle_palette(&g_display, dir);
        DisplayState want = g_display;
        if (display_apply()) return;   // showing what was asked for
        g_display = want;              // keep our place and step past the bad entry
    }
    // Every candidate failed (a disc whose images are all unreadable): let the
    // fallback stand rather than looping here.
    display_apply();
}

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
        // Advance to the stored string's own NUL rather than to what we copied.
        // A blob written before the 11-digit cap can hold a longer number, and
        // every field below is located relative to that terminator -- stopping
        // at the copy length would misread all of them.
        while (buf[1 + j] && 1 + j < (int) sizeof(buf) - 1) j++;
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
    // Display block follows the sound block. display_decode() checks its own
    // sentinel, range-checks every field, and resolves an image background by
    // name against the disc's current TGA list -- so it must run after
    // display_scan_images(). Hand it everything left in the buffer rather than
    // a fixed width: the older four-byte form is still readable even when a
    // long stored dial number pushes the block too close to the end for the
    // full name-bearing one. buf is zeroed above, so any bytes past what was
    // actually written read as an absent block.
    int dsp = s + 3;
    if (dsp + 4 <= (int) sizeof(buf)) {
        display_decode(buf + dsp, (int) sizeof(buf) - dsp, &g_display);
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
    if (n + DISP_BLOB_BYTES <= 62) n += display_encode(&g_display, buf + n);
    saturn_bup_write(SATURN_BUP_CONSOLE, "MOJOOPTS", "options", buf, (uint32_t) n);
}
static void options_menu(void);   // defined below, near the other menus
static void keyboard_controls_page(void);   // ditto -- reached from the in-game F11 key
static void menu_clear_full(void);
static bool menu_confirm(const char *line1, const char *line2);  // defined below
// Defined below too: it draws a menu box, so it has to follow MenuBacking and
// menu_frame, while its callers (the reboot/quit commands) sit above them.
static bool confirm_return_to_title(const char *question);
static void sound_options_page(void);
static void display_options_page(void);

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

// Set by render_console: true when the view has off-screen text below it (the
// "more v" marker is showing). render_keyboard reads it to repaint the marker in
// real-keyboard mode, where the input line is drawn over the console's last row and
// would otherwise wipe it.
static bool g_more_below = false;

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
    // Edge markers when there's off-screen text above/below the window. "more v" is
    // 6 cells wide and the text layer is 40 cells (0..39), so it starts at column 34
    // to keep its trailing 'v' on screen -- column 35 pushed the 'v' to cell 40 and
    // clipped it. g_more_below lets render_keyboard repaint the marker after the
    // input line (see there).
    if (start > 0 && !top_blank) SRL::Debug::Print(39, TOP_MARGIN, "^");
    g_more_below = (start + rows < total);
    if (g_more_below)            SRL::Debug::Print(34, TOP_MARGIN + rows - 1, "more v");
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
// 0xFF fills every 4bpp pixel with color index 15. That is a different CRAM
// entry than the glyphs use (they are index 1), so text_set_color writes both
// to keep the block the same color as the text.
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
        // The input line shares the console's last row here, so the clear above wiped
        // any "more v" render_console drew on it. Repaint it (same cell as there).
        if (g_more_below) SRL::Debug::Print(34, row, "more v");
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

// True if the first word of `line` is "q" or "quit" (case-insensitive). The local
// game prompt intercepts this rather than letting the interpreter run its own quit,
// which ends the story and drops us out of mojo_run.
//
// This is a safety boundary, not a convenience, so it errs towards catching too
// much: it must match every spelling the story's parser would read as QUIT. A
// Z-machine dictionary declares its own word separators (Zork I: , . ") and the
// tokenizer breaks on them with or without a space, so "quit." is QUIT to the game
// -- splitting on spaces alone would let it through. Anything that isn't a letter
// or digit therefore ends the word, and whatever follows is ignored: "quit now" is
// intercepted too, since a story may well parse that as QUIT, and the worst case
// for over-matching is a confirm prompt the player declines.
static int is_alnum_ch(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
static int is_quit_command(const char *line) {
    while (*line != '\0' && !is_alnum_ch(*line)) line++;   // skip spaces/separators
    char w[6];
    int n = 0;
    for (; is_alnum_ch(line[n]) && n < 5; n++) {           // first word, lowercased
        char c = line[n];
        w[n] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
    }
    w[n] = '\0';
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

// Put `cmd` on the input line and submit it, as if the player had typed it and
// pressed Enter -- so it echoes, enters history, and reaches the interpreter by
// the one path every other command uses. How the F-key shortcuts run the game's
// own save/restore.
static void submit_command(KeyboardState &k, const char *cmd) {
    int n = 0;
    while (cmd[n] != '\0' && n < KB_INPUT_MAX - 1) { k.input[n] = cmd[n]; n++; }
    k.input[n] = '\0';
    k.input_len = n;
    k.cursor = n;
    k.submitted = 1;
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
    // Disarm a quick-save destination that was never spent. F5 arms it and submits
    // in the same breath, so it is always consumed before we are back here -- unless
    // the story's save opcode bailed out early (a failed allocation or story re-read
    // never reaches saturn_save_blob), in which case a stale slot would silently
    // hijack the player's next save. Being at a prompt means it is spent or void.
    // The restore side can't be swept the same way: "Load Save Game" arms
    // g_restore_* before the very readline that submits its "restore".
    g_save_device = -1;
    g_save_slot   = -1;
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

        // Start (gamepad), Esc or F10 (keyboard) opens the Options menu mid-game.
        if ((pad && g_pad->WasPressed(Button::START)) || ke.kind == SATURN_KEY_ESCAPE
            || ke.kind == SATURN_KEY_F10) {
            options_menu();
            ensure_typeahead();                       // rebuild if the difficulty changed
            typeahead_scan_screen(g_typeahead_root);  // re-mark on-screen words on the new trie
            SRL::Core::Synchronize();    // consume the edge that closed the menu
            continue;
        }
        // F11: the controls page for the device in hand. That is always the
        // keyboard -- a gamepad has no F11, and this very keypress just marked the
        // keyboard as the active device above -- so unlike the Options menu's
        // equivalent row there is no pad branch to pick here.
        if (ke.kind == SATURN_KEY_F11) {
            keyboard_controls_page();
            menu_clear_full();
            SRL::Core::Synchronize();    // consume the edge that closed the page
            continue;
        }
        // F12: the Sound Options page directly, unless there's nothing to configure --
        // no CD-DA on the disc and no game .BLB -- the same availability the Options menu
        // uses to show/hide its Sound row. (F10 Options, F11 Controls, F12 Sound all open
        // from here.) The key is consumed either way so it never types.
        if (ke.kind == SATURN_KEY_F12) {
            if (music_cdda_has_audio() || sound_has_audio()) {
                sound_options_page();
                menu_clear_full();
                SRL::Core::Synchronize();    // consume the edge that closed the page
            }
            continue;
        }
        // Save/load function keys. These submit the game's own save/restore command
        // rather than doing the work here: the save blob lives in the interpreter,
        // so there is nothing main.cxx could write by itself. The quick variants
        // pre-pick the last committed slot (the mechanism the "Load Save Game" menu
        // already uses) so the blob hooks skip their pickers; with no slot used yet
        // they fall through to asking, which is all F2/F3 ever do.
        if (ke.kind == SATURN_KEY_F2 || ke.kind == SATURN_KEY_F5) {
            if (ke.kind == SATURN_KEY_F5 && g_last_slot >= 0) {
                g_save_device = g_last_device; g_save_slot = g_last_slot;
            }
            submit_command(k, "save");
            continue;   // k.submitted is set: the loop ends and the core takes it
        }
        if (ke.kind == SATURN_KEY_F3 || ke.kind == SATURN_KEY_F6 || ke.kind == SATURN_KEY_F9) {
            if (ke.kind != SATURN_KEY_F3 && g_last_slot >= 0) {
                g_restore_device = g_last_device; g_restore_slot = g_last_slot;
            }
            submit_command(k, "restore");
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

// ---- opaque backing for menus over an image --------------------------------
// NBG3 (the text layer) treats palette entry 0 as transparent, so a menu frame
// drawn over an image background shows the picture through its interior and the
// text sits directly on the artwork.
//
// A VDP2 window fixes just the box: NBG0 (the image) is switched off inside the
// menu rectangle, the back plane colour shows through there, and NBG3's text
// draws over it as usual. Outside the box the picture is untouched.
//
// SGL exposes no constants for this, so the encoding was read out of the
// library. slScrWindowMode(scrn, mode) is
//
//     mov.l IMM_VDP2_WCTLA, r0    ; 0x060ffd90
//     mov.b r5, @(r0, r4)
//
// -- a byte store of `mode` at 0x060ffd90 + scrn, into SGL's work-RAM shadow of
// WCTLA..WCTLD which it flushes at vblank. The scn constants confirm the
// mapping: NBG1=0, NBG0=1, NBG3=2, NBG2=3, SPR=4, RBG0=5 is exactly those four
// 16-bit registers in big-endian byte order, so `mode` is the raw per-screen
// WCTL byte. slScrWindow0 takes plain pixel coordinates (it shifts X left
// itself for the 320-dot mode's half-dot units) and clears the line-window
// pointer, so a rectangle is all it is.
#define WIN_W0_ENABLE  0x02   // WCTL bit 1: window 0 applies to this screen
#define WIN_W0_INSIDE  0x00   // WCTL bit 0: the rect's inside is the window
#define WIN_W0_OUTSIDE 0x01   // ...its outside is

// The one bit that could not be settled from the library: whether the screen is
// suppressed inside the window or outside it. If the image turns out to be
// hidden *everywhere except* the menu box, swap INSIDE for OUTSIDE here and
// nothing else changes.
#define WIN_NBG0_MENU  (WIN_W0_ENABLE | WIN_W0_INSIDE)

// Point window 0 at a character-cell box. Cells are 8x8 and the display is
// 320x224, so the rectangle is just the box scaled up, clamped to the screen.
static void menu_window_rect(int x0, int y0, int w, int h) {
    int x1 = x0 * 8,             y1 = y0 * 8;
    int x2 = (x0 + w) * 8 - 1,   y2 = (y0 + h) * 8 - 1;
    if (x2 > 319) x2 = 319;
    if (y2 > 223) y2 = 223;
    if (x1 < 0)   x1 = 0;
    if (y1 < 0)   y1 = 0;
    slScrWindow0((uint16_t) x1, (uint16_t) y1, (uint16_t) x2, (uint16_t) y2);
}

// Refcounted, because pages nest: Options opens Display, and the inner page
// closing must not clear the windowing while the outer one is still up.
//
// Scoped rather than paired calls. Every one of the seven pages has several
// exit paths, and "remember to undo this on all of them" is the exact shape of
// bug that has already cost this file a release -- a destructor cannot forget.
static int g_menu_backing_depth = 0;

struct MenuBacking {
    MenuBacking() {
        if (g_menu_backing_depth++ == 0) slScrWindowModeNbg0(WIN_NBG0_MENU);
    }
    ~MenuBacking() {
        if (--g_menu_backing_depth == 0) slScrWindowModeNbg0(0);   // window off
    }
};

// Draw a w x h box of +--+ chrome at (x0, y0) and center `title` on its second
// row. Every menu page uses this so the chrome and title placement stay
// identical; pages differ only in the box they ask for.
//
// The caller owns the interior: content starts at (x0 + 2, y0 + 3) by
// convention -- row y0 + 2 stays blank under the title -- and must stay inside
// x0 + w - 2 so it never overwrites the right border.
static void menu_frame(int x0, int y0, int w, int h, const char *title) {
    // Aim the image-suppressing window at this box. Done on every draw rather
    // than once on open, so a nested page's box takes over while it is up and
    // the outer one's is restored the moment it redraws.
    menu_window_rect(x0, y0, w, h);
    for (int r = 0; r < h; r++) {
        char line[42]; int p = 0;
        for (int c = 0; c < w && p < (int) sizeof(line) - 1; c++)
            line[p++] = (r == 0 || r == h - 1) ? ((c == 0 || c == w - 1) ? '+' : '-')
                      : ((c == 0 || c == w - 1) ? '|' : ' ');
        line[p] = '\0';
        SRL::Debug::Print(x0, y0 + r, "%s", line);
    }
    int len = 0; while (title[len]) len++;
    int tx = x0 + (w - len) / 2;
    if (tx < x0 + 1) tx = x0 + 1;
    SRL::Debug::Print(tx, y0 + 1, "%s", title);
}

// A digit jumps to a row and acts on it in one press: value rows cycle forward
// on a plain digit and backward on the shifted symbol, action rows activate.
// The mapping itself lives in menu_layout.c and is unit-tested; this is only
// the C++ binding, which the layout unit cannot express because it needs
// references to each page's own bool locals.
//
// Returns true if a digit selected a row, leaving the caller to set its own
// activation flag -- pages disagree on whether that is named `ok` or `act`.
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

// Modal Y/N confirm, asking `question`. On Yes, soft-resets to the title screen
// in-process (the same return-to-title as the A+B+C+Start chord); configured
// options in backup RAM are retained. On No, returns false so the caller resumes.
// Shared by the reboot and quit commands -- both discard an unsaved game, so both
// ask first.
static bool confirm_return_to_title(const char *question) {
    MenuBacking backing;        // the box owns its area; no console shrink needed
    int qlen = 0;
    while (question[qlen]) qlen++;

    // Everything drawn inside the box counts toward its width, hints included.
    // The longest row that is not the question is the pad variant of the first
    // hint, "(A) (C) (Start) = yes" (21); the digit row is 15 and the second
    // hint is at most 12. Budgeted unconditionally so the box does not resize
    // when the player switches between the pad and a keyboard mid-prompt.
    int content_w = qlen;
    if (content_w < 21) content_w = 21;
    int x0, y0, w, h;
    menu_box_fit("RETURN TO TITLE", content_w, 5, &x0, &y0, &w, &h);

    SRL::Core::Synchronize();   // drop the submit edge that triggered this
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);   // so the prompt below matches the device in hand
        bool yes = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'y' || ke.ch == 'Y' || ke.ch == '1'))
                 || ke.kind == SATURN_KEY_ENTER
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::START)
                 || g_pad->WasPressed(Button::C);
        bool no  = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'n' || ke.ch == 'N' || ke.ch == '2'))
                 || ke.kind == SATURN_KEY_ESCAPE || g_pad->WasPressed(Button::B);
        if (yes) { soft_reset_to_title(); }         // in-process return to title; never returns
        if (no) { return false; }

        // The console still renders behind at full height; the box sits over it
        // and the VDP2 window keeps any image background out from under it. No
        // menu_clear() on purpose -- the game text staying visible behind the
        // box is the point of this prompt.
        render_console();
        menu_frame(x0, y0, w, h, "RETURN TO TITLE");
        int cx = x0 + 2, cy = y0 + 3;
        SRL::Debug::Print(cx, cy, "%s", question);
        if (!g_kbd_visible) SRL::Debug::Print(cx, cy + 2, "1) Yes    2) No");
        SRL::Debug::Print(cx, cy + 3, "%s", hint("(A) (C) (Start) = yes", "Y / Enter = yes"));
        SRL::Debug::Print(cx, cy + 4, "%s", hint("(B) = no", "N / Esc = no"));
        SRL::Core::Synchronize();
    }
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

// Draw a centered box with one or two lines of text. Returns at once without
// waiting or synchronizing: the save/load result screens follow it with
// menu_wait(), while the dialing screens redraw it every frame.
//
// Both lines are sized into the box, so a hint passed as line2 is budgeted the
// same as any other row. Where a caller passes hint(), the pad and keyboard
// variants must be the SAME length ("L+R = cancel" / "Esc = cancel" are both
// 12) so the box does not resize when the player switches input device; if a
// pair ever differs, size the box off the longer one.
//
// The caller owns any MenuBacking guard. Screens that are a single blocking
// message declare one; loops that already hold one do not need a second.
static void menu_message(const char *title, const char *line1, const char *line2) {
    int l1 = 0, l2 = 0;
    while (line1 && line1[l1]) l1++;
    while (line2 && line2[l2]) l2++;

    int content_w = (l1 > l2 ? l1 : l2);
    int rows      = (l2 > 0) ? 2 : 1;
    int x0, y0, w, h;
    menu_box_fit(title, content_w, rows, &x0, &y0, &w, &h);

    menu_clear();
    menu_frame(x0, y0, w, h, title);
    if (l1) SRL::Debug::Print(x0 + 2, y0 + 3, "%s", line1);
    if (l2) SRL::Debug::Print(x0 + 2, y0 + 4, "%s", line2);
}

// The hint line drawn at the bottom of the box (see hint() calls below), named
// once so its width feeds both the sizing math and the draw call -- if the
// wording changes, the box width follows automatically instead of drifting
// out of sync with a hardcoded column count.
static const char MENU_SELECT_HINT_PAD[] = "pad picks   C=ok   B=back";
static const char MENU_SELECT_HINT_KBD[] = "num picks   Enter=ok   Esc=back";

// Modal list menu. Returns the chosen index, or -1 if cancelled. Navigable by
// gamepad (D-pad + A/C/Start, B cancels) or keyboard (number keys pick directly,
// Enter picks the highlighted item, Backspace cancels).
static int menu_select(const char *title, const char *const *items, int count) {
    const int VIS = 16;         // max list rows shown at once; longer lists scroll
    MenuBacking backing;        // opaque while the list is up; restored on exit
    int sel = 0;
    int top = 0;                // index of the first visible row
    int i;

    // Width: the longest item, plus the "> " cursor and the reserved digit
    // columns. Reserved unconditionally so the box does not resize when the
    // player switches between the pad and a keyboard mid-menu.
    int content_w = 0;
    for (i = 0; i < count; i++) {
        int len = 0;
        while (items[i][len]) len++;
        if (len > content_w) content_w = len;
    }
    content_w += 2 + MENU_DIGIT_COLS;

    // The hint line shares the box with the list (same cx origin), so its
    // width must be budgeted too -- otherwise it overwrites the right border
    // on menus whose items are shorter than the hint. Budget the LONGER of
    // the two variants unconditionally, same reasoning as MENU_DIGIT_COLS
    // above: the box must not resize when the player flips input devices
    // mid-menu, so both must fit regardless of which one is drawn this frame.
    int hint_w = (int) sizeof(MENU_SELECT_HINT_PAD) - 1;
    int hint_kbd_w = (int) sizeof(MENU_SELECT_HINT_KBD) - 1;
    if (hint_kbd_w > hint_w) hint_w = hint_kbd_w;
    if (hint_w > content_w) content_w = hint_w;

    // Rows: the visible slice, plus the two scroll markers and a blank line and
    // the hint. The markers keep their rows whether or not they are drawn, so
    // the box does not jump as the list scrolls.
    int rows = (count < VIS ? count : VIS) + 4;

    int x0, y0, w, h;
    menu_box_fit(title, content_w, rows, &x0, &y0, &w, &h);

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
        else if (ke.kind == SATURN_KEY_CHAR) {
            // Digits name the visible rows, not absolute indices, so every entry
            // of a long game list stays reachable as the player scrolls.
            int idx = menu_visible_digit(ke.ch, top, VIS, count);
            if (idx >= 0) { sel = idx; pick = true; }
        }
        else if (ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + count) % count;
        else if (ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % count;
        if (cancel) return -1;
        if (pick)   return sel;

        // scroll the window so the selection stays visible
        if (sel < top)             top = sel;
        else if (sel >= top + VIS) top = sel - VIS + 1;
        int last = top + VIS; if (last > count) last = count;

        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand

        menu_clear();
        menu_frame(x0, y0, w, h, title);
        int cx = x0 + 2, cy = y0 + 3;
        SRL::Debug::Print(cx, cy, "%s", top > 0 ? "^ more" : "      ");
        for (i = top; i < last; i++) {
            char mark = (i == sel) ? '>' : ' ';
            int  vis  = i - top;      // 0-based row within the window
            if (nums && vis < 9)
                SRL::Debug::Print(cx, cy + 1 + vis, "%c %d) %s", mark, vis + 1, items[i]);
            else
                SRL::Debug::Print(cx, cy + 1 + vis, "%c    %s", mark, items[i]);
        }
        SRL::Debug::Print(cx, cy + 1 + (last - top), "%s", last < count ? "v more" : "      ");
        SRL::Debug::Print(cx, cy + 3 + (last - top), "%s",
            hint(MENU_SELECT_HINT_PAD, MENU_SELECT_HINT_KBD));
        menu_sync();
    }
}

// Basic validation for the dial number: non-empty, digits only.
static bool valid_dialnum(const char *s) {
    if (!s[0]) return false;
    int i = 0;
    for (; s[i]; i++) if (s[i] < '0' || s[i] > '9') return false;
    return i <= DIALNUM_MAX;   // g_dialnum has no room past this
}

// Network: edit the server dial number with the on-screen / real keyboard.
// A/Enter accept (after validation); Start/Esc cancel. Both return to the
// Options menu.
static void config_page(void) {
    MenuBacking backing;   // opaque while this page is up; restored on every exit
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
    MenuBacking backing;   // opaque while this page is up; restored on every exit
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
        // Only the 9 assignable rows are numbered: there are 12 rows here and
        // just 9 digits, so Reset/OK/Cancel keep their unnumbered padding and
        // stay reachable by Up/Down only.
        if (up)   sel = (sel - 1 + R_CANCEL + 1) % (R_CANCEL + 1);
        if (down) sel = (sel + 1) % (R_CANCEL + 1);
        // After Up/Down, so a digit wins a same-frame tie against the pad --
        // the order the other five option pages use. Resolving it first would
        // let a simultaneous Up/Down move sel while left/right/act stayed set
        // from the digit, cycling whichever row the pad landed on instead.
        if (menu_digit_row(ke, NASSIGN, sel, left, right)) act = true;
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
        const int fx = 0, fy = 3, fw = 40, fh = 22;
        menu_frame(fx, fy, fw, fh, "CONFIGURE CONTROLS");
        int x = fx + 2, y = fy + 3;
        // Shortened from "Left/Right change ...": the old strings were 38 chars
        // and ran into the frame's right border at column 39.
        SRL::Debug::Print(x, y++, "%s", hint("L/R change  A/Start=OK B=Cancel",
                                             "L/R change  Enter=OK Esc=Cancel"));
        y++;
        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand
        // The value column carries MENU_DIGIT_COLS in BOTH modes so it does not
        // move when the player switches device. x + 20 + 3 = column 25; the
        // widest value is "Z+Left/Right" (12), ending at 36, and the box's
        // right border is at column 39.
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
        // Fixed, unselectable row: indented by the reserved digit columns in
        // both modes so it stays aligned with the numbered labels above.
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

// Controller page: shows the live mapping and offers Configure (remap editor),
// the on-screen keyboard's CapsLock toggle, and Done.
static void controls_page(void) {
    MenuBacking backing;   // opaque while this page is up; restored on every exit
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
        // Rows 1 and 3 are actions and 2 is a toggle driven by left||right||act,
        // so direction is irrelevant on every row of this page.
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
        // Only these three rows are selectable; the mapping table above is a
        // read-only display and keeps its own columns. No value column moves
        // here -- each selectable row prints its value inline.
        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand
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

// Physical-keyboard settings page (shown in Options while a real keyboard is the
// device in hand). Toggles: which arrows move the caret vs cycle suggestions (the
// same flag the Insert key flips), insert-vs-overwrite typing, and CapsLock.
static void keyboard_controls_page(void) {
    MenuBacking backing;   // opaque while this page is up; restored on every exit
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
        // Rows 0-3 are two-state toggles, so direction does not matter; rows 4
        // and 5 are OK/Cancel actions. Placed before `toggle` so a digit feeds
        // straight into it.
        if (menu_digit_row(ke, N, sel, left, right)) act = true;
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
        const int fx = 1, fy = 5, fw = 38, fh = 18;
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        int x = fx + 2, y = fy + 3;
        SRL::Debug::Print(x, y++, "Insert key also flips Arrows;");
        SRL::Debug::Print(x, y++, "Ctrl+Left/Right always move caret.");
        y++;
        // The value column stays at x + 18 in BOTH modes -- it must NOT take the
        // usual MENU_DIGIT_COLS shift. At x + 21 = column 24 the widest value,
        // "Off (overwrite)" (15), would end on column 38, which is this box's
        // right border (fx=1, fw=38). It does not need to move: the longest
        // numbered label, "N) Insert mode", ends at column 18, still two
        // columns clear of the value at 21.
        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand
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

// Diagnostic dump of the raw SRL::Cd::TableOfContents, reached from Sound Options. The
// TOC is static for the disc's life, so we snapshot it once on entry. Up top we print
// the FirstTrack / LastTrack / Session records in full (Control, Address, Number,
// Sector/Frame, session frame-address); below is a scrollable per-track table showing,
// for every Tracks[] slot, the raw 4-bit Control nibble, the Number, the decoded type,
// and the 24-bit FrameAddress. This is here to hunt the "empty / no audio track" signal:
// an absent track reads Control=15 (0x0f) with type Unk and/or FrameAddress=16777215
// (0x00FFFFFF), while a real audio track reads type Aud with a plausible frame address.
// Up/Down scroll one row, Left/Right page; A/B/C/Start (or Enter/Esc) returns.
static void toc_dump_page(void) {
    SRL::Cd::TableOfContents toc = SRL::Cd::TableOfContents::GetTable();
    const int total    = SRL::Cd::MaxTrackCount;   // length of Tracks[]
    const int rows_per = 19;                        // data rows that fit under the header
    int top = 0;
    SRL::Core::Synchronize();   // consume the edge that opened this
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        bool up    = g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP;
        bool down  = g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN;
        bool left  = g_pad->WasPressed(Button::Left) || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right)|| ke.kind == SATURN_KEY_RIGHT;
        bool done  = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B)
                   || g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)
                   || ke.kind == SATURN_KEY_ENTER || ke.kind == SATURN_KEY_ESCAPE
                   || ke.kind == SATURN_KEY_BACKSPACE;
        if (done) break;
        if (up)    top--;
        if (down)  top++;
        if (left)  top -= rows_per;
        if (right) top += rows_per;
        if (top > total - rows_per) top = total - rows_per;
        if (top < 0) top = 0;

        menu_clear();
        int x = 1, y = 0;
        SRL::Debug::Print(x, y++, "CD TOC DUMP");
        SRL::Debug::Print(x, y++, "First C=%d A=%d N=%d S=%d F=%d",
            toc.FirstTrack.Control, toc.FirstTrack.Address, toc.FirstTrack.Number,
            toc.FirstTrack.LocationBody.LocationData.Sector,
            toc.FirstTrack.LocationBody.LocationData.Frame);
        SRL::Debug::Print(x, y++, "Last  C=%d A=%d N=%d S=%d F=%d",
            toc.LastTrack.Control, toc.LastTrack.Address, toc.LastTrack.Number,
            toc.LastTrack.LocationBody.LocationData.Sector,
            toc.LastTrack.LocationBody.LocationData.Frame);
        SRL::Debug::Print(x, y++, "Sess  C=%d A=%d fad=%d",
            toc.Session.Control, toc.Session.Address, toc.Session.fad);
        y++;   // blank separator
        SRL::Debug::Print(x,     y, "Trk");
        SRL::Debug::Print(x + 6,  y, "Ct");
        SRL::Debug::Print(x + 10, y, "Nm");
        SRL::Debug::Print(x + 14, y, "Type");
        SRL::Debug::Print(x + 22, y, "Frame");
        y++;
        for (int i = 0; i < rows_per; i++) {
            int t = top + i;
            if (t >= total) break;
            const char *tn;
            switch (toc.Tracks[t].GetType()) {
                case SRL::Cd::TableOfContents::TrackType::Audio:    tn = "Aud"; break;
                case SRL::Cd::TableOfContents::TrackType::Audio4Ch: tn = "A4c"; break;
                case SRL::Cd::TableOfContents::TrackType::Data:     tn = "Dat"; break;
                default:                                            tn = "Unk"; break;
            }
            SRL::Debug::Print(x,     y, "%d", t);
            SRL::Debug::Print(x + 6,  y, "%d", toc.Tracks[t].Control);
            SRL::Debug::Print(x + 10, y, "%d", toc.Tracks[t].Number);
            SRL::Debug::Print(x + 14, y, "%s", tn);
            SRL::Debug::Print(x + 22, y, "%d", (int) toc.Tracks[t].FrameAddress);
            y++;
        }
        SRL::Debug::Print(x, 27, "%s", hint("Up/Dn scroll  <> page  B=back",
                                            "Up/Dn scroll  <> page  Esc=back"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}

// Sound Options (full-screen, OK/Cancel). Which rows appear depends on what is actually
// available: Audio Mix / Track / Music level need CD-DA on the disc (surfaced here as
// an>0 from music_cdda_audio_tracks); PCM level needs the loaded game's .BLB
// (sound_has_audio). OK/Cancel always show. Start/A = OK (commit+save+apply), Esc/B =
// Cancel (restore snapshot incl. live audio). Previews play live while open.
static void sound_options_page(void) {
    MenuBacking backing;   // opaque while this page is up; restored on every exit
    static const char *const MIX[] = { "Dynamic", "Repeat", "Sequential", "Random" };
    enum { SR_MIX, SR_TRACK, SR_MUSIC, SR_PCM, SR_TOC, SR_OK, SR_CANCEL };
    const unsigned char* atracks; int an = music_cdda_audio_tracks(&atracks);
    bool has_cd  = (an > 0);
    bool has_blb = (sound_has_audio() != 0);

    // Visible rows in display order: the CD trio only when the disc has audio, PCM only
    // when the game shipped sound. sel indexes this list, not a fixed row number.
    int rows[7], nrows = 0;
    if (has_cd)  { rows[nrows++] = SR_MIX; rows[nrows++] = SR_TRACK; rows[nrows++] = SR_MUSIC; }
    if (has_blb) rows[nrows++] = SR_PCM;
    rows[nrows++] = SR_TOC;          // diagnostic: always reachable from here
    rows[nrows++] = SR_OK;
    rows[nrows++] = SR_CANCEL;

    int sel = 0;
    // Snapshot for Cancel.
    int s_mix = g_mix_mode, s_trk = g_sel_track, s_mus = g_music_level, s_pcm = g_pcm_level;
    // A live track demo interrupts whatever was streaming, so exit must re-assert the
    // real track. Absent any demo (and any mix/track change), we leave the stream alone
    // so opening and closing this page in-game doesn't restart the current track.
    bool previewed = false;
    int aidx = 0;                     // current index into the audio-track list
    for (int i = 0; i < an; i++) if (atracks[i] == g_sel_track) { aidx = i; break; }
    if (an > 0 && atracks[aidx] != g_sel_track) g_sel_track = atracks[aidx];   // no match: snap to first
    SRL::Core::Synchronize();   // consume the edge that opened this
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
        // A digit picks a visible row and acts on it: the three level/mix rows
        // ignore `ok`, SR_TRACK already treats left/right/ok alike, and SR_TOC,
        // SR_OK and SR_CANCEL are action rows that should activate.
        if (menu_digit_row(ke, nrows, sel, left, right)) ok = true;
        int row = rows[sel];

        if (cancel || (ok && row == SR_CANCEL)) {   // revert everything, incl. live audio
            g_mix_mode = s_mix; g_sel_track = s_trk; g_music_level = s_mus; g_pcm_level = s_pcm;
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track);
            if (previewed) music_refresh();   // only if a demo interrupted the stream
            break;
        }
        if (row == SR_MIX) { if (left && g_mix_mode > 0) g_mix_mode--; if (right && g_mix_mode < MIX_RANDOM) g_mix_mode++; }
        else if (row == SR_TRACK) {   // only present when has_cd, so an>0/atracks valid
            if (left  && aidx > 0)      aidx--;
            if (right && aidx < an - 1) aidx++;
            g_sel_track = atracks[aidx];
            if (left || right || ok) { music_cdda_play(g_sel_track); previewed = true; }   // demo/preview
        }
        else if (row == SR_MUSIC) { if (left && g_music_level > 0) g_music_level--; if (right && g_music_level < 7) g_music_level++;
                                    if (left || right) music_set_volume(g_music_level); }
        else if (row == SR_PCM)   { if (left && g_pcm_level > 0) g_pcm_level--; if (right && g_pcm_level < 7) g_pcm_level++;
                                    if (left || right) sound_set_level(g_pcm_level); }
        else if (ok && row == SR_TOC) { toc_dump_page(); menu_clear_full(); }
        else if (ok && row == SR_OK) {   // OK
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track);
            // Only (re)assert playback if a demo interrupted it or the mix/track actually
            // changed; otherwise leave the current stream running so closing the page is
            // seamless in-game.
            if (previewed || g_mix_mode != s_mix || g_sel_track != s_trk) {
                if (g_mix_mode == MIX_DYNAMIC) music_refresh();   // hand back to the room engine
                else music_start();                               // start the chosen track now
            }
            options_save();
            break;
        }

        menu_clear();
        const int fx = 1, fy = 6, fw = 38, fh = 16;
        menu_frame(fx, fy, fw, fh, "SOUND");
        int x = fx + 2, y = fy + 3;
        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand
        // The value column carries MENU_DIGIT_COLS in BOTH modes so it does not
        // move when the player switches device. x + 14 + 3 = column 20; the
        // widest value is "< Sequential >" (14), ending at 33, and the box's
        // right border is at column 38.
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
                    SRL::Debug::Print(vx, y++, "%d  (A=demo)", aidx + 1);   // 1-based
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
                case SR_TOC:
                    if (nums) SRL::Debug::Print(x, y++, "%c %d) View CD TOC", cur, i + 1);
                    else      SRL::Debug::Print(x, y++, "%c    View CD TOC", cur);
                    break;
                case SR_OK:
                    y++;   // blank separator before the actions
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

// Display Options (full-screen, OK/Cancel). Unlike Sound Options every row is
// always present -- there is no hardware dependency. Left/Right applies live so
// the result is visible behind the menu; Cancel restores the snapshot.
static void display_options_page(void) {
    MenuBacking backing;   // opaque while this page is up; restored on every exit
    enum { DR_PALETTE, DR_BG, DR_TEXT, DR_OK, DR_CANCEL };
    static const int rows[] = { DR_PALETTE, DR_BG, DR_TEXT, DR_OK, DR_CANCEL };
    const int nrows = (int)(sizeof(rows) / sizeof(rows[0]));

    int sel = 0;
    DisplayState snapshot = g_display;   // for Cancel
    SRL::Core::Synchronize();            // consume the edge that opened this
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
        // A digit picks a row and acts on it. The three cycler rows ignore `ok`
        // (they read `dir` from left/right below); DR_OK and DR_CANCEL are
        // action rows that should activate.
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
        // Full 40 columns rather than the 38 the other pages use. Values print
        // at x + 17, leaving 20 columns before the border, so "< %s >" fits a
        // name of at most 16. Two sources feed these rows and both must stay
        // under that: PRESETS in display.c (widest "Amstrad CPC 464", 15) and
        // the disc image names, capped at GFS_FNAME_LEN = 12 by ISO9660 8.3.
        // The margin is one column -- a longer preset name, or image names no
        // longer bounded by 8.3, needs the value column moved, not just a
        // wider box. At 38 the palette row already lands on the border.
        //
        // This is the one page where the value column CANNOT take the usual
        // MENU_DIGIT_COLS shift: at x + 20 the widest value ("< Amstrad CPC
        // 464 >", 19) would run to column 40 and overwrite the border, and the
        // box is already the full screen width so it cannot grow. The 3 columns
        // come out of the LABEL side instead -- "System Palette" was shortened
        // to "Palette" so that the longest numbered label ("N) Background",
        // ending at column 17) still clears the value column at 19.
        const int fx = 0, fy = 7, fw = 40, fh = 14;
        menu_frame(fx, fy, fw, fh, "DISPLAY");
        int x = fx + 2, y = fy + 3;
        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand
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
                    y++;   // blank separator before the actions
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

// Options menu (centered box): a difficulty slider plus actions (Network,
// Controls, Display, Sound, Return to Title, Done). Up/Down select a row; on
// Difficulty, Left/Right change it; A/Enter activate other rows; B/Esc close.
// Sound only appears when there is audio to configure -- CD-DA on the disc
// or the game's .BLB; with neither, the row is hidden. Audio settings live on that
// page. Difficulty is written to backup only if the player actually changed it.
static void options_menu(void) {
    MenuBacking backing;   // opaque while this page is up; restored on every exit
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
    items[nitems++] = OI_DISPLAY;   // always available: no hardware dependency
    if (sound_available) items[nitems++] = OI_SOUND;
    items[nitems++] = OI_RETURN;
    items[nitems++] = OI_DONE;

    int diff = g_difficulty, sel = 0;
    SRL::Core::Synchronize();   // consume the edge that opened this
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nitems) % nitems;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nitems;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        // Digit handling has to run before `item` is read, since it can move
        // `sel`. Every row but OI_DIFF is a sub-page or an action, so direction
        // only matters on the difficulty slider; the rest ignore left/right and
        // OI_DIFF ignores the activation.
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
            else if (item == OI_CONTROLS) { if (g_kbd_visible) controls_page(); else keyboard_controls_page(); menu_clear_full(); }
            else if (item == OI_DISPLAY) { display_options_page(); menu_clear_full(); }
            else if (item == OI_SOUND) { sound_options_page(); menu_clear_full(); }
            else if (item == OI_RETURN) {   // Return to Title (soft reset; never returns on Yes)
                if (menu_confirm("Return to the title screen?", "Are you sure?")) {
                    if (diff != g_difficulty) { g_difficulty = diff; options_save(); }
                    soft_reset_to_title();
                }
            }
            else if (item == OI_DONE) break;   // (OI_DIFF: activate is a no-op)
        }

        // Wipe NBG3 before redrawing the box, like every other page's loop.
        // MenuBacking only windows the image out of the box interior; it does
        // nothing to leftover text OUTSIDE the box. Without this, the menu that
        // opened Options (e.g. the Single/Multiplayer list, which is wider than
        // this box) shows through around it. The image background stays -- only
        // the text layer is cleared.
        menu_clear();
        menu_frame(x0, y0, w, h, "OPTIONS");
        // Content runs from column 7 to the last drawable column 33 (border at
        // 34). The numbered difficulty row is the widest at 27 characters
        // ("> 1) Difficulty: < Medium >"), landing exactly on 33. The DESC line
        // below it is already 27 wide and is NOT indented further for that
        // reason -- it is a sub-caption, not a selectable row.
        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand
        char dmark = item == OI_DIFF ? '>' : ' ';
        // OI_DIFF is always items[0], so its number is always 1.
        if (nums) SRL::Debug::Print(x0 + 2, y0 + 3, "%c 1) Difficulty: %s %s %s", dmark,
                          diff > DIFF_EASY ? "<" : " ", NAMES[diff], diff < DIFF_HARD ? ">" : " ");
        else      SRL::Debug::Print(x0 + 2, y0 + 3, "%c    Difficulty: %s %s %s", dmark,
                          diff > DIFF_EASY ? "<" : " ", NAMES[diff], diff < DIFF_HARD ? ">" : " ");
        SRL::Debug::Print(x0 + 2, y0 + 4, "    %s", DESC[diff]);
        int ay = y0 + 6;   // action rows follow the difficulty block; Sound may be absent
        for (int i = 0; i < nitems; i++) {
            char cur = (i == sel) ? '>' : ' ';
            const char *label = 0;
            switch (items[i]) {
                case OI_DIFF: continue;   // drawn above
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
    MenuBacking backing;        // the box owns its area while the picker is up

    // Both hint variants of both states. Everything drawn inside a box counts
    // toward its width, hints included, and the LONGER variant is budgeted
    // unconditionally so the box does not resize when the player switches
    // between the pad and a keyboard mid-menu.
    static const char PICK_HINT_PAD[] = "pad picks   C=edit   B=back";
    static const char PICK_HINT_KBD[] = "num picks   Enter=edit   Esc=back";
    static const char EDIT_HINT_PAD[] = "C=type X=space  B=back  A=OK";
    static const char EDIT_HINT_KBD[] = "type name  Esc=back  Enter=OK";

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
            else if (ke.kind == SATURN_KEY_CHAR) {
                // Slot list never scrolls, so the window is the whole list.
                int idx = menu_visible_digit(ke.ch, 0, SAVE_SLOTS, SAVE_SLOTS);
                if (idx >= 0) { sel = idx; pick = true; }
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
        // Two shapes: a short slot list, or a taller box with the on-screen
        // keyboard under it once a name is being typed. Sized per frame rather
        // than once, because the box changes shape when `editing` flips.
        const char *btitle = editing ? "NAME THIS SAVE" : "SAVE - PICK A SLOT";

        // A slot row is a cursor mark, a space, the reserved "N) " columns
        // (reserved whether or not drawn) and the label. saturn_bup_info caps a
        // save comment at 10 characters, and "(empty)" is 7, so 10 is the
        // ceiling. The edited row is the same chrome plus `maxchars` and the
        // caret; callers pass 8, well under the row width above.
        int row_w = 2 + MENU_DIGIT_COLS + 10;                 // 15
        int edit_w = 2 + MENU_DIGIT_COLS + maxchars + 1;      // 14 at maxchars = 8
        if (edit_w > row_w) row_w = edit_w;
        int content_w;
        int rows;
        if (editing) {
            int kb_w = KB_COLS * 2;                            // 26
            int hint_w = (int) sizeof(EDIT_HINT_KBD) - 1;      // 29, the longer variant
            if ((int) sizeof(EDIT_HINT_PAD) - 1 > hint_w) hint_w = (int) sizeof(EDIT_HINT_PAD) - 1;
            content_w = row_w;
            if (kb_w > content_w)   content_w = kb_w;
            if (hint_w > content_w) content_w = hint_w;        // 29
            // slots, blank, keyboard, blank, hint
            rows = SAVE_SLOTS + 2 + KB_ROWS + 1;               // 12 -> h 16, fits 28
        } else {
            int hint_w = (int) sizeof(PICK_HINT_KBD) - 1;      // 33, the longer variant
            if ((int) sizeof(PICK_HINT_PAD) - 1 > hint_w) hint_w = (int) sizeof(PICK_HINT_PAD) - 1;
            content_w = row_w;
            if (hint_w > content_w) content_w = hint_w;        // 33
            rows = SAVE_SLOTS + 2;                             // slots, blank, hint
        }
        int x0, y0, w, h;
        menu_box_fit(btitle, content_w, rows, &x0, &y0, &w, &h);

        bool nums = !g_kbd_visible && !editing;   // digits are literal text while editing

        menu_clear();
        menu_frame(x0, y0, w, h, btitle);
        int cx = x0 + 2, cy = y0 + 3;
        for (int i = 0; i < SAVE_SLOTS; i++) {
            char mark = (i == sel) ? '>' : ' ';
            if (editing && i == sel) {
                SRL::Debug::Print(cx, cy + i, "%c    %s_", mark, k.input);
            } else {
                const char *label = slotname[i][0] ? slotname[i] : "(empty)";
                if (nums) SRL::Debug::Print(cx, cy + i, "%c %d) %s", mark, i + 1, label);
                else      SRL::Debug::Print(cx, cy + i, "%c    %s", mark, label);
            }
        }
        if (!editing) {
            SRL::Debug::Print(cx, cy + SAVE_SLOTS + 1, "%s", hint(PICK_HINT_PAD, PICK_HINT_KBD));
        } else {
            for (int r = 0; r < KB_ROWS; r++) {
                char rowbuf[KB_COLS * 2 + 1];
                int p = 0;
                for (int c = 0; c < KB_COLS; c++) {
                    rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
                    rowbuf[p++] = KB_LAYOUT[r][c];
                }
                rowbuf[p] = '\0';
                SRL::Debug::Print(cx, cy + SAVE_SLOTS + 1 + r, "%s", rowbuf);
            }
            SRL::Debug::Print(cx, cy + SAVE_SLOTS + 2 + KB_ROWS, "%s",
                hint(EDIT_HINT_PAD, EDIT_HINT_KBD));
        }
        SRL::Core::Synchronize();
    }
}

// Yes/no confirmation. Returns true if confirmed (C / A / Start / Enter / Y),
// false if declined (B / N).
static bool menu_confirm(const char *line1, const char *line2) {
    MenuBacking backing;        // opaque behind the box while the prompt is up
    int l1 = 0, l2 = 0;
    while (line1 && line1[l1]) l1++;
    while (line2 && line2[l2]) l2++;

    // Everything drawn inside the box counts toward its width, hints included.
    // The widest non-message row is the keyboard hint, "Enter = Yes     Esc = No"
    // (24); the digit row is 15. Budgeted unconditionally -- pad wording is
    // shorter, but sizing to it would make the box grow the moment the player
    // switched to a keyboard.
    //
    // Deliberately does NOT add MENU_DIGIT_COLS the way menu_select does. There
    // the digits are a per-row prefix that shifts every item's text rightward,
    // so the columns have to be added to the item width. Here they are a
    // standalone row that prefixes nothing, and the 24-column floor above
    // already covers it -- unconditionally, so the box still cannot resize when
    // the input device changes, which is the invariant that constant protects.
    int content_w = (l1 > l2 ? l1 : l2);
    if (content_w < 24) content_w = 24;
    int x0, y0, w, h;
    menu_box_fit("CONFIRM", content_w, (l2 > 0 ? 5 : 4), &x0, &y0, &w, &h);

    SRL::Core::Synchronize();   // consume the edge that got us here
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (ke.kind == SATURN_KEY_ENTER) return true;
        if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) return false;
        if (ke.kind == SATURN_KEY_CHAR) {
            if (ke.ch == 'y' || ke.ch == 'Y' || ke.ch == '1') return true;
            if (ke.ch == 'n' || ke.ch == 'N' || ke.ch == '2') return false;
        } else {
            if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START))
                return true;
            if (g_pad->WasPressed(Button::B)) return false;
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "CONFIRM");
        int cx = x0 + 2, cy = y0 + 3;
        if (l1) SRL::Debug::Print(cx, cy, "%s", line1);
        if (l2) SRL::Debug::Print(cx, cy + 1, "%s", line2);
        int hy = cy + (l2 > 0 ? 3 : 2);
        if (!g_kbd_visible) SRL::Debug::Print(cx, hy, "1) Yes    2) No");
        SRL::Debug::Print(cx, hy + 1, "%s",
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
    int device, slot;
    char comment[12];
    char name[12];
    if (g_save_slot >= 0) {
        // Quick-save (F5): straight to the last slot -- no device, slot, or
        // overwrite prompt, which is the entire point of it. Keep whatever name
        // the slot already carries so the picker still reads sensibly.
        device = g_save_device; slot = g_save_slot;
        g_save_device = -1; g_save_slot = -1;              // one-shot
        make_slot_name(name, slot);
        if (!saturn_bup_info(device, name, comment))
            snprintf(comment, sizeof(comment), "Save %d", slot + 1);
    } else {
        device = choose_device("SAVE - device?");
        if (device < 0) return 0;

        // Pick a slot and edit its name in place (cursor stays on the chosen line).
        if (!pick_slot_and_name(device, &slot, comment, 8)) return 0;
        if (comment[0] == 0) snprintf(comment, sizeof(comment), "Save %d", slot + 1);

        // Confirm before overwriting an existing save in that slot.
        make_slot_name(name, slot);
        char existing[12];
        if (saturn_bup_info(device, name, existing)) {
            char q[40];
            snprintf(q, sizeof(q), "Overwrite \"%s\"?", existing);
            if (!menu_confirm(q, "Are you sure?")) return 0;
        }
    }

    int ok = saturn_bup_write(device, name, comment, data, len);
    if (ok) { g_last_device = device; g_last_slot = slot; }   // what the quick keys reuse
    {
        MenuBacking backing;   // opaque over an image background while this is up
        menu_message("SAVE", ok ? "Saved." : "Save FAILED (no space?).",
                     "(press any key/button)");
        menu_wait();
    }
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
    if (ok) { g_last_device = device; g_last_slot = slot; }   // what the quick keys reuse
    if (!ok) {
        MenuBacking backing;
        menu_message("RESTORE", "No save in that slot.", "(press any key/button)");
        menu_wait();
    }
    return ok;
}

// ---- boot ------------------------------------------------------------------

// Title screen: wait for any face button; use elapsed frames as an RNG seed.
// Draw the title art (no prompt). Shown on its own during the catalog load, then
// again by title_and_seed with the "Press any button" prompt on the same screen.
static void title_draw_art(void) {
    SRL::Debug::Print(13, 12, "Z - A T U R N");
    SRL::Debug::Print(4, 15, "Saturn port (c) 2026 by Suinevere");
}

// ---- background images discovered on the disc (TGA folder) -----------------
// Names of the bitmaps found in the disc's TGA folder, scanned once at boot.
// display.c borrows these pointers (via display_set_images), so the storage
// must outlive the scan -- hence static rather than stack-local.
static char        g_image_name[DISP_IMAGE_MAX][16];
static const char *g_image_ptr[DISP_IMAGE_MAX];

// ISO9660 entries can arrive as "NAME.TGA;1" (version suffix already stripped
// by the time this is called -- see display_scan_images). Accept only .TGA
// files; the deeper 8bpp/one-bank check cannot be done from the name and
// happens at load time in title_bg_show, where a failure falls back to a
// color background.
static bool tga_name_is_usable(const char *name) {
    if (!name || !name[0] || name[0] == '.') return false;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.' && name[i+1] == 'T' && name[i+2] == 'G' && name[i+3] == 'A'
            && name[i+4] == '\0') return true;
    }
    return false;
}

// Scan the disc's "TGA" directory and register any *.TGA files found with
// display.c so the background selector can cycle into them. Modeled on
// scan_z3_folder() below: a private GfsDirTbl (not SRL's shared Cd table),
// and a return to the root directory both before and after so the scan is
// idempotent across soft resets and leaves the CD where the boot sequence
// expects it. The loaded table is kept (g_tga_tbl) so title_bg_show() can make
// /TGA current for each load, which is the only directory where a bare
// "*.TGA" name resolves.
//
// Two earlier comments here were wrong and between them cost this feature a
// release: first that bare names resolve regardless of current directory, then
// that the fix was to open them at the root. Both are false. The bitmaps are in
// /TGA and GFS only ever searches the current directory, so the loader has to
// go there.
// ---- CD current-directory discipline ---------------------------------------
// scan_z3_folder() calls GFS_SetDir() to make Z3 the current CD directory, and
// that persists for the rest of the session. Anything opened by bare file name
// afterwards resolves inside Z3 -- which is why background bitmaps silently
// failed to load once the game catalog had been scanned. Bitmap loads bracket
// themselves with these two helpers instead of assuming a directory.
static GfsDirName g_z3_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_z3_tbl;
static bool       g_z3_dir_valid = false;   // set once scan_z3_folder has run

// Root is captured the same way Z3 is, rather than asking SRL to walk back up.
// SRL::Cd::ChangeDir(nullptr) ("go to root") cannot be used: its loop reads
//
//     if ((err = Cd::ChangeDir("..")) != ErrorCode::ErrorOk) return err;
//
// (srl_cd.hpp:679) but ChangeDir(name) returns GFS_SetDir()'s value, which is
// the *file count* on success, not GFS_ERR_OK (== 0, sega_gfs.h:59). So any
// successful step up a non-empty directory is read as an error and the function
// returns before its root-detection loop runs -- after having already called
// GFS_SetDir on SRL's shared table. It does not reach root; it leaves the
// current directory somewhere unintended. That is why background loads failed
// even at the title screen, where we were already at root before the call.
static GfsDirName g_root_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_root_tbl;
static bool       g_root_dir_valid = false;

// Snapshot the root directory record. Must be called while root is current --
// i.e. straight after GFS_Reset(), before any GFS_SetDir. fid 0 is "." (the
// same self-entry SRL reads via GFS_GetDirInfo(0, ...)), so loading it while at
// root captures root's own contents.
static void cd_capture_root(void) {
    GFS_DIRTBL_TYPE(&g_root_tbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&g_root_tbl) = g_root_dirnames;
    GFS_DIRTBL_NDIR(&g_root_tbl)    = SRL_MAX_CD_FILES;
    g_root_dir_valid = GFS_LoadDir(0, &g_root_tbl) >= 0;
}

// No-op if the snapshot failed: staying put beats moving somewhere unintended.
static void cd_enter_root(void) {
    if (g_root_dir_valid) GFS_SetDir(&g_root_tbl);
}

// The bitmaps live in /TGA, not at the root -- cd/data is the ISO root
// (shared.mk ASSETS_DIR), so cd/data/TGA lands as /TGA. GFS resolves a bare
// name against the current directory only, so opening "HOUSE.TGA" anywhere but
// inside /TGA finds nothing: at the root that name is not present, only the
// directory holding it. display_scan_images() captures this table when it lists
// the folder, and title_bg_show() enters it around each load.
static GfsDirName g_tga_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_tga_tbl;
static bool       g_tga_dir_valid = false;

static void cd_enter_tga(void) {
    if (g_tga_dir_valid) GFS_SetDir(&g_tga_tbl);
}

// Re-point the CD at Z3 so story-file opens keep working. No-op before the
// catalog scan has populated g_z3_tbl.
static void cd_restore_z3(void) {
    if (g_z3_dir_valid) GFS_SetDir(&g_z3_tbl);
}

// Undo everything a bitmap load disturbed: the current CD directory (story-file
// opens resolve inside Z3) and CD-DA playback. One helper rather than two calls
// because title_bg_show has six exit paths, and a fix in this file has already
// been lost once to an exit path that forgot its cleanup.
static void bitmap_read_end(void) {
    cd_restore_z3();
}

static void display_scan_images(void) {
    GfsDirName *dirnames = g_tga_dirnames;   // kept at file scope: cd_enter_tga()
    GfsDirTbl  *tblp     = &g_tga_tbl;       // re-applies this table on every load
    int found = 0;

    g_tga_dir_valid = false;

    // Same idempotence dance as scan_z3_folder: get back to the root before
    // resolving "TGA", or after a soft reset it would resolve relative to
    // whatever directory we were left in.
    cd_enter_root();

    int32_t fid = GFS_NameToId((int8_t *) "TGA");
    if (fid >= 0) {
        GFS_DIRTBL_TYPE(tblp)    = GFS_DIR_NAME;
        GFS_DIRTBL_DIRNAME(tblp) = dirnames;
        GFS_DIRTBL_NDIR(tblp)    = SRL_MAX_CD_FILES;
        int32_t count = GFS_LoadDir(fid, tblp);
        // Records loaded: cd_enter_tga() can now make this the current
        // directory, which is the only place a bare "*.TGA" open resolves.
        g_tga_dir_valid = count >= 0;
        // Entry names live in dirnames[i].fname (int8_t[GFS_FNAME_LEN], not a
        // bare string) -- same layout scan_z3_folder reads below. Extract into
        // a NUL-terminated buffer, stripping any ";n" version suffix, before
        // testing the extension or copying into the registered name.
        for (int32_t i = 0; count > 0 && i < count && found < DISP_IMAGE_MAX; i++) {
            char nm[16];
            int j = 0;
            for (; j < GFS_FNAME_LEN && j < (int) sizeof(nm) - 1; j++) {
                char c = (char) dirnames[i].fname[j];
                if (c == '\0' || c == ';') break;
                nm[j] = c;
            }
            nm[j] = '\0';
            if (!tga_name_is_usable(nm)) continue;
            int k = 0;
            for (; nm[k] && k < (int) sizeof(g_image_name[0]) - 1; k++) g_image_name[found][k] = nm[k];
            g_image_name[found][k] = '\0';
            g_image_ptr[found] = g_image_name[found];
            found++;
        }
    }

    // Leave the CD where we found it: at the root, which is where the existing
    // boot sequence expects to be before scan_z3_folder runs.
    cd_enter_root();
    display_set_images(found > 0 ? g_image_ptr : NULL, found);
}

// ---- minimal 8bpp TGA loader ------------------------------------------------
// Replaces SRL::Bitmap::TGA for backgrounds. Its DecodePaletted
// (srl_tga.hpp:376-394) has two defects that made cycling backgrounds trip an
// SH-2 exception after three or four loads -- the frozen save state showed
// PC inside the VBR handler at 0x06000956 with every interrupt masked:
//
//   * `this->imageData = autonew uint8_t[pixels];` is never null-checked before
//     it is written through.
//   * the destination is indexed by a uint32_t computed from a *signed*,
//     descending row counter, so any early decrement wraps the index and writes
//     before the buffer.
//
// Either is a silent out-of-bounds write per load, which is why the failure
// took several loads to appear and did not depend on which image was chosen.
//
// This reads only what it needs: one sector for the header and palette, then
// the pixel data straight into a single w*h buffer. Peak is about half SRL's,
// which read the whole file into a temp buffer and decoded into a second one.
//
// Accepts only uncompressed 8bpp colour-mapped TGA (type 1) with a 24- or
// 32-bit palette -- exactly what tools/make_house_tga.py emits. Anything else
// is rejected rather than guessed at, so a stray file falls back to a colour
// background instead of decoding into nonsense.
struct RawBitmap final : SRL::Bitmap::IBitmap {
    uint8_t                *Pixels;
    SRL::Bitmap::Palette   *Pal;
    uint16_t                W, H;
    RawBitmap() : Pixels(nullptr), Pal(nullptr), W(0), H(0) {}
    ~RawBitmap() override {
        if (Pal != nullptr)    delete Pal;       // ~Palette frees its Colors
        if (Pixels != nullptr) delete Pixels;
    }
    uint8_t *GetData() override { return Pixels; }
    SRL::Bitmap::BitmapInfo GetInfo() const override {
        return SRL::Bitmap::BitmapInfo(W, H, Pal);
    }
};

// Load `file` from the current CD directory into NBG0. Returns false without
// touching VDP2 if anything about the file is not the shape described above.
static bool tga_load_nbg0(const char *file) {
    SRL::Cd::File f(file);
    if (!f.Exists()) return false;

    // Header and palette both live in the first sector for every image we ship
    // (18 + 256*4 = 1042 at worst). Bail rather than handle a split palette.
    //
    // GFS DMAs sector data straight into the destination, so every buffer handed
    // to LoadBytes must be 4-byte aligned. Declared as uint32_t for that reason:
    // a bare uint8_t array carries no alignment guarantee.
    static uint32_t hdrbuf[512];               // 2048 bytes, 4-byte aligned
    uint8_t *const hdr = (uint8_t *) hdrbuf;
    const int32_t ss = (f.Size.SectorSize > 0) ? f.Size.SectorSize : 2048;
    if (ss > (int32_t) sizeof(hdrbuf)) return false;
    if (f.LoadBytes(0, ss, hdr) <= 0) return false;

    const int idlen    = hdr[0];
    const int cmaptype = hdr[1];
    const int imgtype  = hdr[2];
    const int cmaplen  = hdr[5] | (hdr[6] << 8);
    const int cmapbits = hdr[7];
    const int w        = hdr[12] | (hdr[13] << 8);
    const int h        = hdr[14] | (hdr[15] << 8);
    const int bpp      = hdr[16];
    const int topdown  = (hdr[17] >> 5) & 1;   // descriptor bit 5: 1 = top origin

    if (cmaptype != 1 || imgtype != 1 || bpp != 8)      return false;
    if (cmaplen <= 0 || cmaplen > 256)                  return false;
    if (cmapbits != 24 && cmapbits != 32)               return false;
    if (w <= 0 || h <= 0 || w > 1024 || h > 512)        return false;

    const int      cmapbytes = cmaplen * (cmapbits / 8);
    const int      pixoff    = 18 + idlen + cmapbytes;
    const uint32_t npix      = (uint32_t) w * (uint32_t) h;
    if (pixoff > ss)                                    return false;
    if ((uint32_t) pixoff + npix > (uint32_t) f.Size.Bytes) return false;
    if (SRL::Memory::HighWorkRam::GetFreeSpace() < npix + 4096) return false;

    // Palette always 256 entries so BitmapInfo picks Paletted256 and the VRAM
    // container stays 128KB -- one bank. A shorter palette would select a 4bpp
    // mode and a different layout. TGA stores map entries as B,G,R.
    SRL::Types::HighColor *colors = new SRL::Types::HighColor[256];
    if (colors == nullptr) return false;
    for (int i = 0; i < 256; i++) {
        SRL::Types::HighColor c;
        c.Opaque = (i == 0) ? 0 : 1;   // index 0 reads as transparent on a scroll screen
        if (i < cmaplen) {
            const uint8_t *e = hdr + 18 + idlen + i * (cmapbits / 8);
            c.Blue = e[0] >> 3; c.Green = e[1] >> 3; c.Red = e[2] >> 3;
        } else {
            c.Blue = 0; c.Green = 0; c.Red = 0;
        }
        colors[i] = c;
    }

    // Read from the sector the pixels start in, into the *base* of the
    // allocation, then slide them down over the leading partial sector.
    //
    // The destination of a LoadBytes must be 4-byte aligned, because GFS DMAs
    // the sector data into it. Reading the tail to `pix + got` -- an arbitrary
    // byte offset -- is what broke the title load: HOUSE.TGA's 169-entry palette
    // puts pixel data at byte 525, leaving an odd destination, and the SH-2
    // raised an address error. The frozen save state showed PR inside
    // gftr_execTrn and R4 = 0x060660b3, an odd heap address.
    //
    // Sliding afterwards also keeps the pixels at the allocation base, which
    // matters a second time: SRL's Bmp2VRAM DMAs each scanline from this
    // pointer, so it must not sit at a byte offset either.
    const uint32_t skip = (uint32_t) (pixoff % ss);   // partial sector before the pixels
    const uint32_t span = skip + npix;                 // bytes to read from that sector on
    if (SRL::Memory::HighWorkRam::GetFreeSpace() < span + 4096) { delete colors; return false; }

    uint8_t *pix = new uint8_t[span];
    if (pix == nullptr) { delete colors; return false; }
    if (((unsigned int) pix & 3) != 0) {   // allocator should 4-align; refuse if not
        delete pix; delete colors; return false;
    }
    if (f.LoadBytes((size_t) (pixoff / ss), (int32_t) span, pix) <= 0) {
        delete pix; delete colors; return false;
    }
    if (skip > 0) for (uint32_t i = 0; i < npix; i++) pix[i] = pix[skip + i];

    // TGA's default origin is bottom-left; VDP2 wants the top row first.
    if (!topdown) {
        static uint8_t rowbuf[1024];
        for (int y = 0; y < h / 2; y++) {
            uint8_t *a = pix + (uint32_t) y * (uint32_t) w;
            uint8_t *b = pix + (uint32_t) (h - 1 - y) * (uint32_t) w;
            for (int i = 0; i < w; i++) rowbuf[i] = a[i];
            for (int i = 0; i < w; i++) a[i]      = b[i];
            for (int i = 0; i < w; i++) b[i]      = rowbuf[i];
        }
    }

    RawBitmap bmp;
    bmp.Pixels = pix;
    bmp.W      = (uint16_t) w;
    bmp.H      = (uint16_t) h;
    bmp.Pal    = new SRL::Bitmap::Palette(colors, 256);
    if (bmp.Pal == nullptr) { delete pix; delete colors; return false; }

    SRL::VDP2::NBG0::LoadBitmap(&bmp);
    return true;   // ~RawBitmap frees the pixels and the palette
}

// ---- title-screen background image (VDP2 NBG0) ------------------------------
// Shown behind the title text only; menus and gameplay stay on solid black.
// The debug text console lives on NBG3 at priority Layer7 (top), and NBG0 is
// disabled by default, so loading the image on NBG0 at a lower priority puts it
// safely behind all text with no other VDP2 changes.
//
// Returns false when the bitmap could not be loaded (or isn't the shape we
// require), so the caller can fall back to a color background rather than
// leaving static on screen.
static bool title_bg_show(const char *file) {
    static char loaded[16] = "";       // file currently resident in VDP2 VRAM
    bool same = true;
    for (int i = 0; i < (int) sizeof(loaded); i++) {
        if (loaded[i] != file[i]) { same = false; break; }
        if (loaded[i] == '\0') break;   // matched up to and including the NUL
    }
    if (!same) {
        // CD read + VRAM upload. We open by bare file name, and GFS resolves
        // that against the current directory only -- so we must be *inside*
        // /TGA, where the bitmaps live. Neither the root nor Z3 (left current by
        // scan_z3_folder) contains them. Must run before menu CD-DA starts: the
        // single CD head can't read data while playing audio.
        // Every image must be 256-color paletted, not truecolor: SRL's VDP2
        // bitmap allocator doubles the container size for RGB555, pushing a
        // 512x256 bitmap to 256KB and across the A0/A1 VRAM bank boundary.
        // Bank-spanning bitmaps render as static (slBitMapNbg0 never reserves
        // the second bank in VDP2_RAMCTL -- see the note at the top of
        // srl_vdp2.hpp). At 8bpp the container is exactly 128KB and fits one
        // bank.
        // Every exit path below must call bitmap_read_end(), which restores both
        // the CD directory and playback.
        //
        // This read stops CD-DA -- one drive head -- so the menu track goes
        // silent for its duration. Pausing and resuming around it was tried and
        // removed: SRL's Cdda::Resume() sets an absolute start
        // (CDC_PLY_SFAD = LastLocation) against a *span* end
        // (CDC_PLY_EFAS = whole track length) and repeats that range forever,
        // which left the CD block looping a ~3 second fragment. Resuming
        // mid-track needs that worked out first; the standing alternative is to
        // load on OK rather than live, so a menu visit costs one read.
        cd_enter_tga();
        // tga_load_nbg0 validates the header itself and refuses anything that is
        // not uncompressed 8bpp colour-mapped, so a missing or odd file falls
        // back to a colour background rather than reaching a decoder. It also
        // never hands a name to SRL::Bitmap::TGA, whose constructor raises
        // Debug::Assert on a failed open (srl_tga.hpp:820) -- and in a DEBUG
        // build Assert clears the screen, forces the back plane red and sits in
        // an animation loop (srl_debug.hpp:200-220).
        bool ok = tga_load_nbg0(file);   // not `loaded`: that is the cached name above
        bitmap_read_end();
        if (!ok) {
            return false;
        }
        SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);  // below text (Layer7)
        // LoadBitmap leaves stray debug prints on rows 20-21 ("4bpp" / "Pal: N")
        // from SRL itself (srl_vdp2.hpp:869,888). Patching the library would not
        // survive a fresh submodule checkout, so wipe the rows here instead.
        SRL::Debug::PrintClearLine(20);
        SRL::Debug::PrintClearLine(21);
        int k = 0;
        for (; file[k] && k < (int) sizeof(loaded) - 1; k++) loaded[k] = file[k];
        loaded[k] = '\0';
    }
    SRL::VDP2::NBG0::ScrollEnable();
    return true;
}

static void title_bg_show(void) { (void) title_bg_show("HOUSE.TGA"); }

static void title_bg_hide(void) {
    SRL::VDP2::NBG0::ScrollDisable();
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
    // GFS_SetDir below makes Z3 the current CD directory, and that persists. After a
    // soft reset we re-enter here still inside Z3, so "Z3" would resolve relative to
    // Z3 and fail. Return to the root directory first so the scan is idempotent.
    cd_enter_root();

    int32_t fid = GFS_NameToId((int8_t *) "Z3");
    if (fid < 0) return -1;

    GFS_DIRTBL_TYPE(&g_z3_tbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&g_z3_tbl) = g_z3_dirnames;
    GFS_DIRTBL_NDIR(&g_z3_tbl)    = SRL_MAX_CD_FILES;

    int32_t count = GFS_LoadDir(fid, &g_z3_tbl);
    if (count < 0) return -1;
    GFS_SetDir(&g_z3_tbl);   // subsequent File() opens resolve inside Z3
    g_z3_dir_valid = true;   // cd_restore_z3() may now re-apply this directory

    int n = 0;
    for (int i = 0; i < count && n < max; i++) {
        char nm[16];
        int j = 0;
        for (; j < GFS_FNAME_LEN && j < 15; j++) {
            char c = (char) g_z3_dirnames[i].fname[j];
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
            // Cap at the width a menu row can actually draw, not at the buffer
            // size. A row is "> N) " (5 cols) plus the title, and a full-width
            // box leaves 37 columns from the content origin to the border, so
            // anything past 32 chars would overwrite the border. 31 keeps a
            // column of margin. Every real title fits; this only guards the
            // filename fallback and any future long entry. Once the deferred
            // marquee lands, long titles can scroll instead of being clipped.
            int j = 0; const char* src = title ? title : names[i];
            for (; src[j] && j < MENU_ROW_TEXT_MAX; j++) labels[i][j] = src[j];
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
        MenuBacking backing;
        menu_message("NO GAMES", (count < 0)
            ? "No Z3 folder found on the CD."
            : "No .Z3 games found in the Z3 folder.",
            "(press any key/button to go back)");
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
    // The first online session's vocab load does CD reads that stop CD-DA, and going
    // straight here on boot can arrive before the menu track has begun -- either way,
    // make sure something is playing. But when the menu track is already looping (the
    // common case: it never stopped), leave it be so it stays seamless rather than
    // restarting from the top.
    if (!music_cdda_is_playing()) music_cdda_play(g_sel_track);
    const char *number = g_dialnum;   // change it in Options -> Network

    // ---- connect, with auto-redial on carrier-training failure ----
    // Scoped so the image-suppressing window covers the whole dialing sequence
    // but is dropped again before the terminal session takes the screen over.
    {
    MenuBacking backing;
    net_connect_result_t rc = NET_DIAL_FAIL;
    for (int attempt = 1; attempt <= ONLINE_DIAL_ATTEMPTS; attempt++) {
        {
            // Widest this can get is 37 columns, which is exactly what a
            // full-width 40-column box can draw -- no margin left. That fit
            // depends on BOTH DIALNUM_MAX (11) and ONLINE_DIAL_ATTEMPTS
            // staying a single digit; raising either pushes this past 37.
            // menu_box_fit clamps width to 40 silently, so an overflow here
            // does not report an error, it just overwrites the right border.
            char dial[40];
            snprintf(dial, sizeof(dial), "Dialing %s ... (attempt %d/%d)",
                     number, attempt, ONLINE_DIAL_ATTEMPTS);
            menu_message("ONLINE", dial,
                         hint("L+R = cancel", "Esc = cancel"));
            SRL::Core::Synchronize();
        }

        rc = net_connect_open(number);        // blocking (~35s timeout on failure)
        if (rc == NET_OK) break;
        if (rc == NET_NO_MODEM) break;        // hardware missing; redial won't help

        // NET_DIAL_FAIL: brief pause (lets the DreamPi return to idle), then retry.
        if (attempt < ONLINE_DIAL_ATTEMPTS) {
            menu_message("ONLINE", "No carrier. Retrying...",
                         hint("L+R = cancel", "Esc = cancel"));
            bool cancelled = false;
            for (int f = 0; f < 180; f++) {   // ~3s at 60Hz
                if (online_cancel_requested()) { cancelled = true; break; }
                // Redrawn every frame so the hint follows the input device the
                // player is actually using during the wait.
                menu_message("ONLINE", "No carrier. Retrying...",
                             hint("L+R = cancel", "Esc = cancel"));
                SRL::Core::Synchronize();
            }
            if (cancelled) { net_connect_close(); return; }
        }
    }

    if (rc != NET_OK) {
        menu_message("ONLINE",
            rc == NET_NO_MODEM ? "NetLink modem not found." : "Connection failed.",
            "(press any button)");
        online_wait_any();
        return;
    }
    }   // dialing box down: the terminal owns the screen from here

    menu_clear_full();   // wipe the box chrome before the console draws over it

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
        term_service(&ts, tr, ZATURN_RX_BUDGET);   // RX -> console

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
    cd_capture_root();              // must precede any GFS_SetDir: cd_enter_root() needs it
    display_scan_images();          // must precede options_load: display_decode()
    display_defaults(&g_display);   // validates image indices against this list
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
    int cd_reentry = setjmp(g_title_jmp);
    g_title_jmp_armed = true;
    GFS_Reset();
    cd_capture_root();   // GFS_Reset returns us to root; re-snapshot it there
    g_z3_dir_valid = false;   // the pre-reset Z3 table is stale until re-scanned
    // The soft reset longjmps here, which skips destructors -- so a MenuBacking
    // held by a page we jumped out of never ran. Clear it by hand, or NBG3
    // stays opaque and the title image is hidden for the rest of the session.
    g_menu_backing_depth = 0;
    slScrWindowModeNbg0(0);
    // Re-list /TGA for the same reason. Z3 is re-scanned lazily by
    // preload_game_catalog(), but nothing else refreshes the bitmap table, and
    // title_bg_show() needs it a few lines below. Only on re-entry: the first
    // pass already scanned above, and this is a CD read.
    if (cd_reentry) display_scan_images();
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
    text_set_color(DISP_RGB555(0xFF, 0xFF, 0xFF));
    title_bg_show();
    title_draw_art();
    SRL::Core::Synchronize();

    preload_game_catalog();              // CD reads happen once, here

    music_set_level(g_music_level);      // honor the saved music level for menu audio
    music_cdda_play(g_sel_track);        // start the menu track; no CD reads remain in the menu flow

    int seed = title_and_seed();         // redraws the same art + "Press any button", waits
    display_apply();   // colors (or an image background) take over from the title

    // Top-level mode choice. "Play Online" runs the multizork telnet terminal
    // and returns here on disconnect; "Play Local" falls through to the offline
    // Z-machine flow below.
    static const char *modes[] = { "Play Local (single player)", "Play Online (multizork)",
                                   "Load Save Game", "Options" };
    const char* game_file = nullptr;

    for (;;) {
        int mode = menu_select("Z-ATURN", modes, 4);
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

    // The story has ended: mojo_run only returns once it sets quit, which the
    // prompt's own q/quit interception normally prevents -- but a death or victory
    // screen offers QUIT too, and that answer comes back here. Keeping the final
    // screen up forever (what this used to do) is the hang that made in-game Quit
    // look like a crash. Hold it until the player has read it, then return to the
    // title with menu music, the same place every other exit lands.
    render_console();
    SRL::Debug::Print(1, 27, "(press any key/button for the title screen)");
    menu_wait();
    soft_reset_to_title();   // never returns
    return 0;
}
