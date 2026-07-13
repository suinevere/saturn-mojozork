/*----------------------
 | console_view.cxx
 | Description: Implements console-scrollback and on-screen-keyboard rendering,
 |   input-device hint tracking, and the blinking block text cursor (including
 |   its one-time DEL-slot glyph and the input-line drawing it shares between the
 |   real-keyboard and on-screen-keyboard layouts), plus typeahead_edit -- the
 |   one-frame input-editing pass with typeahead that the local prompt and the
 |   online terminal share.
 | Author: suinevere
 | Dependencies: console_view.h, app_state.h, input.h, console.c, keyboard.c,
 |   typeahead.c, SRL
 ----------------------*/

#include <srl.hpp>
#include "console_view.h"
#include "app_state.h"
#include "input.h"

// ---- rendering -------------------------------------------------------------

/*----------------------
 | SCREEN_ROWS
 | Description: The 28 text rows (0..27) the debug layer provides. The on-screen
 |   keyboard occupies the bottom rows when shown; when hidden (real keyboard in
 |   hand) those rows go back to the console for more text.
 | Author: suinevere
 ----------------------*/
static const int SCREEN_ROWS = 28;

/*----------------------
 | TOP_MARGIN
 | Description: One blank row kept at the top because TV overscan clips the first
 |   text row on real hardware. Console content starts on row 1; menus already
 |   draw from row 1+, so this only affects the console layout.
 | Author: suinevere
 ----------------------*/
static const int TOP_MARGIN = 1;

/*----------------------
 | g_kbd_visible / g_caret_arrows
 | Description: g_kbd_visible tracks whether the on-screen keyboard is showing
 |   (gamepad in hand) vs hidden (real keyboard). g_caret_arrows swaps whether
 |   plain or Ctrl arrows move the text caret vs cycle suggestions.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | g_output_start
 | Description: The console_total_lines() mark taken before a turn's output (0 for
 |   the initial room), so console_scroll_to_output can land on the TOP of a long
 |   response instead of its bottom.
 | Author: suinevere
 ----------------------*/
long g_output_start = 0;

/*----------------------
 | g_more_below
 | Description: Set by render_console: true when off-screen text remains below the
 |   view (the "more v" marker is showing). render_keyboard reads it to repaint the
 |   marker in real-keyboard mode, where the input line is drawn over the console's
 |   last row and would otherwise wipe it.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | CURSOR_BLOCK_STR
 | Description: The one-character string printed as the text cursor. The SGL ASCII
 |   font has no solid-block glyph, so one is carved into the otherwise-unused DEL
 |   (0x7F) slot (see install_block_glyph). ASCII::Print addresses font 0's char
 |   data at VDP2_VRAM_B1 + 0x18000 + (char+640)*0x20; for 0x7F that is +0x1DFE0,
 |   the last tile LoadFontSG populated. The fill uses color index 15, a different
 |   CRAM entry than the glyphs (index 1), so text_set_color writes both to keep
 |   the block the same color as the text.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | CURSOR_BLINK_FRAMES
 | Description: Half-period of the cursor blink in frames (~0.33s at 60fps, ~1.5Hz).
 | Author: suinevere
 ----------------------*/
#define CURSOR_BLINK_FRAMES 20

/*----------------------
 | typeahead_edit
 | Description: Gamepad editing runs first (Caps toggle on the L+R combo; history
 |   recall and text-caret moves on their configurable chords; plain D-pad moves
 |   the on-screen picker; the mapped face buttons type and backspace). Then a
 |   refresh lambda re-derives the current word (text after the last space), the
 |   previous word (looked up in the trie so grammar can filter), and the
 |   candidate list, resetting the cycle index whenever the current word changes.
 |   accept commits the ghost suffix, matching the case the player is typing --
 |   uppercasing the completion when the last typed char is uppercase (see
 |   draw_input_line). Typeahead is live only when the caret sits at the end of
 |   the line. Insert toggles whether plain or Ctrl arrows move the caret vs cycle
 |   suggestions. Accept (mapped face button / Tab) commits the ghost with no
 |   trailing space, or -- with no ghost -- submits, unless the line already ends
 |   in a space so a just-typed separator does not fire the command; X commits the
 |   ghost plus a space, or types a space to open the next word. Remaining key
 |   events type/erase/submit/recall or fall through to scroll handling.
 | Author: suinevere
 | Dependencies: keyboard.c, input.cxx, typeahead.c
 | Globals: g_pad, g_caret_arrows
 | Params: k -- keyboard/input-line state, edited in place; root -- typeahead
 |   trie; sug_index -- suggestion-cycle index (in/out); sug_last -- word the
 |   cycle index belongs to (in/out); ke -- decoded key event, consumed as
 |   handled; pad -- gamepad is the active device; selected_out -- chosen
 |   suggestion or null; cw_len_out -- current word length
 | Returns: N/A
 ----------------------*/
void typeahead_edit(KeyboardState &k, TrieNode *root,
                    int &sug_index, char *sug_last,
                    SaturnKeyEvent &ke, bool pad,
                    DictionaryWord *&selected_out, int &cw_len_out) {
    if (pad) {
        if (caps_combo_fired()) keyboard_set_caps(!keyboard_get_caps());
        if (chord_fired(CA_RECALL, -1)) history_recall(&k, 1);
        if (chord_fired(CA_RECALL, +1)) history_recall(&k, 0);
        if (chord_fired(CA_CURSOR, -1)) keyboard_caret_left(&k);
        if (chord_fired(CA_CURSOR, +1)) keyboard_caret_right(&k);
        if (!g_pad->IsHeld(Button::Z) && !g_pad->IsHeld(Button::Y)) {
            if (pad_fired(Button::Up))    keyboard_move(&k, 0, -1);
            if (pad_fired(Button::Down))  keyboard_move(&k, 0,  1);
            if (pad_fired(Button::Left))  keyboard_move(&k, -1, 0);
            if (pad_fired(Button::Right)) keyboard_move(&k,  1, 0);
        }
        if (pad_fired(face_button(FA_TYPE))) keyboard_type(&k);
        if (pad_fired(face_button(FA_BACK))) keyboard_backspace(&k);
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

    bool at_end = (k.cursor == k.input_len);

    if (ke.kind == SATURN_KEY_INSERT) { g_caret_arrows = !g_caret_arrows; ke.kind = SATURN_KEY_NONE; }

    bool caret_l = g_caret_arrows ? (ke.kind == SATURN_KEY_LEFT)  : (ke.kind == SATURN_KEY_CTRL_LEFT);
    bool caret_r = g_caret_arrows ? (ke.kind == SATURN_KEY_RIGHT) : (ke.kind == SATURN_KEY_CTRL_RIGHT);
    if (caret_l) keyboard_caret_left(&k);
    if (caret_r) keyboard_caret_right(&k);

    bool kb_prev = g_caret_arrows ? (ke.kind == SATURN_KEY_CTRL_LEFT)  : (ke.kind == SATURN_KEY_LEFT);
    bool kb_next = g_caret_arrows ? (ke.kind == SATURN_KEY_CTRL_RIGHT) : (ke.kind == SATURN_KEY_RIGHT);
    bool cyc_prev = (pad && chord_fired(CA_AUTO, -1)) || kb_prev;
    bool cyc_next = (pad && chord_fired(CA_AUTO, +1)) || kb_next;
    if (at_end && ncand > 0 && cyc_prev) sug_index = (sug_index - 1 + ncand) % ncand;
    if (at_end && ncand > 0 && cyc_next) sug_index = (sug_index + 1) % ncand;
    selected = (at_end && ncand > 0) ? cands[sug_index] : nullptr;
    if (ke.kind == SATURN_KEY_LEFT || ke.kind == SATURN_KEY_RIGHT ||
        ke.kind == SATURN_KEY_CTRL_LEFT || ke.kind == SATURN_KEY_CTRL_RIGHT) ke.kind = SATURN_KEY_NONE;

    bool a_press   = pad && g_pad->WasPressed(face_button(FA_ACCEPT));
    bool x_press   = pad && pad_fired(Button::X);
    bool has_ghost = selected && ghost_len() > 0;
    if (a_press) {
        if (has_ghost) accept(false);
        else if (k.input_len == 0 || k.input[k.input_len - 1] != ' ') keyboard_submit(&k);
    }
    if (ke.kind == SATURN_KEY_TAB) {
        if (has_ghost) accept(false);
        else if (at_end && k.input_len > 0 && k.input[k.input_len - 1] != ' ')
            keyboard_type_char(&k, ' ');
        ke.kind = SATURN_KEY_NONE;
    }
    if (x_press) {
        if (has_ghost) accept(true);
        else           keyboard_type_char(&k, ' ');
    }

    if      (ke.kind == SATURN_KEY_CHAR)      keyboard_type_char(&k, ke.ch);
    else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
    else if (ke.kind == SATURN_KEY_DELETE)    keyboard_delete_forward(&k);
    else if (ke.kind == SATURN_KEY_ENTER)     keyboard_submit(&k);
    else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
    else if (ke.kind == SATURN_KEY_UP)        history_recall(&k, 1);
    else if (ke.kind == SATURN_KEY_DOWN)      history_recall(&k, 0);
    else                                      scroll_handle_key(ke);

    refresh();
    if (k.cursor != k.input_len) selected = nullptr;
    selected_out = selected;
    cw_len_out = cw_len;
}

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
