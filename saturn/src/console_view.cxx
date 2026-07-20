/*----------------------
 | console_view.cxx
 | Description: Implements console-scrollback and on-screen-keyboard rendering,
 |   input-device hint tracking, and the blinking block text cursor (including
 |   its one-time DEL-slot glyph and the input-line drawing it shares between the
 |   real-keyboard and on-screen-keyboard layouts).
 | Author: suinevere
 | Dependencies: console_view.h, app_state.h, input.h, console.c, keyboard.c, SRL
 ----------------------*/

#include <srl.hpp>
#include "console_view.h"
#include "app_state.h"
#include "input.h"

// ---- rendering -------------------------------------------------------------

// The debug layer gives us 28 text rows (0..27). When the on-screen keyboard is
// shown it occupies the bottom rows; when hidden (the player is typing on a real
// keyboard) those rows are handed back to the console for more text.
static const int SCREEN_ROWS = 28;

// TV overscan clips the very top text row on real hardware (the first line shows
// only its bottom half), so we keep row 0 blank and start all console content on
// row 1. Menus already draw from row 1+, so this only affects the console layout.
static const int TOP_MARGIN = 1;

bool g_kbd_visible = true;
bool g_caret_arrows = false;

/*----------------------
 | console_height
 | Description: Subtracts TOP_MARGIN from SCREEN_ROWS for the available rows,
 |   then further reserves 1 input row + KB_ROWS keyboard rows + 1 hint row when
 |   the on-screen keyboard is showing, or just the 1 input row when it is hidden.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible
 | Params: N/A
 | Returns: the number of rows the console view may draw into
 ----------------------*/
int console_height(void) {
    int avail = SCREEN_ROWS - TOP_MARGIN;
    return g_kbd_visible ? (avail - (1 + KB_ROWS + 1)) : (avail - 1);
}

/*----------------------
 | hint
 | Description: Returns `pad` while the on-screen keyboard is showing (gamepad in
 |   hand) or `kbd` once it is hidden (real keyboard in hand), so the same call
 |   site's on-screen text always names the device the player is actually using.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible
 | Params: pad -- hint text for gamepad + on-screen keyboard; kbd -- hint text for
 |   a real keyboard
 | Returns: whichever of pad/kbd matches the last-used device
 ----------------------*/
const char *hint(const char *pad, const char *kbd) {
    return g_kbd_visible ? pad : kbd;
}

/*----------------------
 | note_input_device
 | Description: A real-keyboard key event clears g_kbd_visible (hides the
 |   on-screen keyboard); otherwise, any gamepad button edge this frame sets it
 |   back. Call once per input frame with that frame's key event.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible, g_pad
 | Params: ke -- this frame's keyboard event
 | Returns: N/A
 ----------------------*/
void note_input_device(const SaturnKeyEvent &ke) {
    if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;
    else if (g_pad->AnyPressed())   g_kbd_visible = true;
}

// ---- scrollback ------------------------------------------------------------

// console_total_lines() mark taken before a turn's output (set to 0 for the
// initial room) so console_scroll_to_output can land on the TOP of a long
// response instead of its bottom.
long g_output_start = 0;

// Set by render_console: true when the view has off-screen text below it (the
// "more v" marker is showing). render_keyboard reads it to repaint the marker in
// real-keyboard mode, where the input line is drawn over the console's last row
// and would otherwise wipe it.
static bool g_more_below = false;

/*----------------------
 | render_console
 | Description: Clamps g_scroll to [0, maxstart+1] (the +1 allows one blank line
 |   past the top as a scroll-limit affordance), then prints `rows` console lines
 |   starting from the computed `start` index. Prints a "^" at column 39 of the
 |   top row when older text is scrolled off above, and sets g_more_below (and
 |   prints "more v" at column 34 -- chosen so the 6-wide marker's trailing 'v'
 |   lands inside the 40-cell text layer instead of clipping at column 35) when
 |   newer text remains below the window.
 | Author: suinevere
 | Dependencies: console.c, SRL
 | Globals: g_scroll, g_more_below
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void render_console(void) {
    int rows = console_height();
    int total = console_line_count();
    int maxstart = (total > rows) ? (total - rows) : 0;
    if (g_scroll < 0)            g_scroll = 0;
    if (g_scroll > maxstart + 1) g_scroll = maxstart + 1;
    int top_blank = (g_scroll == maxstart + 1) ? 1 : 0;
    int start = maxstart - (g_scroll - top_blank);
    for (int r = 0; r < rows; r++) {
        SRL::Debug::PrintClearLine(TOP_MARGIN + r);
        int li = start + r - top_blank;
        if (li >= 0 && li < total)
            SRL::Debug::Print(0, TOP_MARGIN + r, "%s", console_get_line(li));
    }
    if (start > 0 && !top_blank) SRL::Debug::Print(39, TOP_MARGIN, "^");
    g_more_below = (start + rows < total);
    if (g_more_below)            SRL::Debug::Print(34, TOP_MARGIN + rows - 1, "more v");
}

/*----------------------
 | console_scroll_to_output
 | Description: Computes how many lines the turn just emitted from the delta
 |   against g_output_start (using the monotonic total-lines counter so this
 |   stays correct even after old lines evict from the 128-line ring). If that
 |   exceeds the visible rows, sets g_scroll so the turn's first row lands at the
 |   top of the window (clamped to the oldest surviving line if the turn is
 |   longer than the ring); otherwise sets g_scroll to 0 (the live bottom).
 | Author: suinevere
 | Dependencies: console.c
 | Globals: g_output_start, g_scroll
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void console_scroll_to_output(void) {
    int total = console_line_count(), rows = console_height();
    int maxstart = (total > rows) ? (total - rows) : 0;
    long added = console_total_lines() - g_output_start;
    if (added > rows) {
        int top = total - (int) added;
        if (top < 0) top = 0;
        g_scroll = maxstart - top;
        if (g_scroll < 0) g_scroll = 0;
    } else {
        g_scroll = 0;
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

/*----------------------
 | install_block_glyph
 | Description: Fills the DEL (0x7F) font tile at VDP2_VRAM_B1 + 0x18000 +
 |   (0x7f + 640)*0x20 with 0xFF (every 4bpp pixel set to color index 15), so
 |   printing CURSOR_BLOCK_STR renders a solid block instead of a character.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void install_block_glyph(void) {
    volatile uint8_t* tile =
        (volatile uint8_t*)(VDP2_VRAM_B1 + 0x18000 + (0x7f + 640) * 0x20);
    for (int i = 0; i < 32; i++) tile[i] = 0xFF;
}

/*----------------------
 | draw_input_line
 | Description: Prints "> {input}{ghost}" at (0,row). The ghost (the typeahead
 |   completion's remaining characters, case-matched to whatever the player was
 |   typing) is only appended when the caret sits at the end of the line -- a
 |   mid-line caret means the player is editing, so the completion is suppressed.
 |   The blinking block cursor overprints whichever cell it currently sits on:
 |   the character under the caret when mid-line, else the ghost's next character
 |   or a space; when the block is "off" that cell's real character prints
 |   instead, so it appears to blink. Only called from render_keyboard, so it
 |   stays file-local.
 | Author: suinevere
 | Dependencies: keyboard.c, SRL
 | Globals: N/A
 | Params: row -- console row to draw on; k -- current keyboard/input-line state;
 |   prediction -- the selected typeahead completion, or null; current_word_len --
 |   length of the word being completed; block_on -- whether the cursor block is
 |   in its "on" blink phase
 | Returns: N/A
 ----------------------*/
static void draw_input_line(int row, const KeyboardState &k,
                            DictionaryWord* prediction, int current_word_len,
                            bool block_on) {
    const char* suffix = "";
    char sbuf[64];
    if (prediction && k.cursor == k.input_len && k.input_len < KB_INPUT_MAX - 1) {
        const char* g = prediction->text + current_word_len;
        bool up = current_word_len > 0 && k.input_len > 0 &&
                  k.input[k.input_len - 1] >= 'A' && k.input[k.input_len - 1] <= 'Z';
        int i = 0;
        for (; g[i] && i < (int) sizeof(sbuf) - 1; i++)
            sbuf[i] = (up && g[i] >= 'a' && g[i] <= 'z') ? (char) (g[i] - 'a' + 'A') : g[i];
        sbuf[i] = '\0';
        suffix = sbuf;
    }
    SRL::Debug::Print(0, row, "> %s%s", k.input, suffix);

    int cursor_col = 2 + k.cursor;
    char under = (k.cursor < k.input_len) ? k.input[k.cursor]
                                          : (suffix[0] ? suffix[0] : ' ');
    if (block_on) SRL::Debug::Print(cursor_col, row, "%s", CURSOR_BLOCK_STR);
    else          SRL::Debug::Print(cursor_col, row, "%c", under);
}

// Half-period, in frames, of the cursor blink (~0.33s at 60fps -> ~1.5Hz).
#define CURSOR_BLINK_FRAMES 20

/*----------------------
 | render_keyboard
 | Description: Installs the solid-block cursor glyph on first use (deferred
 |   here, not at boot, so VDP2/the font are guaranteed up by the first render),
 |   then advances the blink phase and draws the input line via draw_input_line.
 |   When the on-screen keyboard is hidden (a real keyboard is in hand), the
 |   console's last row is already the ">" prompt, so the input line is drawn
 |   over it (clearing that row and the one below first) instead of on a
 |   separate row -- otherwise the prompt would show twice -- and any "more v"
 |   marker render_console drew on that shared row is repainted since the clear
 |   wiped it. When the on-screen keyboard is showing, the input line goes on its
 |   own row below the console, followed by the KB_ROWS keyboard grid (marking
 |   the picker cell with '['), the CapsLock indicator, and the remappable
 |   face-button legend.
 | Author: suinevere
 | Dependencies: keyboard.c, input.cxx, SRL
 | Globals: g_kbd_visible, g_more_below
 | Params: k -- current keyboard/input-line state; prediction -- the selected
 |   typeahead completion, or null; current_word_len -- length of the word being
 |   completed
 | Returns: N/A
 ----------------------*/
void render_keyboard(const KeyboardState &k, DictionaryWord* prediction, int current_word_len) {
    static bool glyph_ready = false;
    if (!glyph_ready) { install_block_glyph(); glyph_ready = true; }

    static uint32_t blink = 0;
    bool block_on = ((blink++ / CURSOR_BLINK_FRAMES) & 1) != 0;

    int base = TOP_MARGIN + console_height();
    if (!g_kbd_visible) {
        int row = base - 1;
        SRL::Debug::PrintClearLine(base);
        SRL::Debug::PrintClearLine(row);
        draw_input_line(row, k, prediction, current_word_len, block_on);
        if (g_more_below) SRL::Debug::Print(34, row, "more v");
        return;
    }
    int row = base;
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
    if (keyboard_get_caps()) SRL::Debug::Print(30, row + 1, "CAPS");
    SRL::Debug::Print(0, row + 1 + KB_ROWS, "%s=type %s=accept %s=del  X=space",
                      face_btn_name(FA_TYPE), face_btn_name(FA_ACCEPT), face_btn_name(FA_BACK));
}
