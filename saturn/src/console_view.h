/*----------------------
 | console_view.h
 | Description: Text-console and on-screen-keyboard rendering: how many console
 |   rows are available given whether the on-screen keyboard is showing, which
 |   input-method-appropriate hint text to print, tracking which input device (real
 |   keyboard vs gamepad) the player last used, painting the scrollback view and
 |   positioning it on new output, and drawing the on-screen keyboard grid with its
 |   blinking block text cursor. Owns no input handling -- callers feed it key
 |   events and read back its device-tracking state.
 | Author: suinevere
 | Dependencies: console.h, keyboard.h, saturn_keyboard.h, typeahead.h
 ----------------------*/

#ifndef CONSOLE_VIEW_H
#define CONSOLE_VIEW_H

#include "console.h"
#include "keyboard.h"
#include "saturn_keyboard.h"
#include "typeahead.h"

// Whether the on-screen keyboard is drawn. Flipped by the active input device: a
// real-keyboard keypress hides it (more text room); a gamepad press shows it again.
extern bool g_kbd_visible;

// Inline-edit mode: which arrows move the text caret vs cycle suggestions. false
// (default): Ctrl+Left/Right move the caret, plain Left/Right cycle. true: plain
// Left/Right move the caret, Ctrl+Left/Right cycle. Toggled by Insert.
extern bool g_caret_arrows;

// console_total_lines() mark taken just before the interpreter runs a turn (or 0
// for the initial room), so console_scroll_to_output can size that turn's output
// even after older lines evict from the scrollback ring.
extern long g_output_start;

/*----------------------
 | console_height
 | Description: How many console text rows are currently available for
 |   scrollback, given whether the on-screen keyboard is showing (it reserves its
 |   own rows plus a hint row when visible).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible
 | Params: N/A
 | Returns: the number of rows the console view may draw into
 ----------------------*/
int console_height(void);

/*----------------------
 | hint
 | Description: Picks the input-hint string matching the last-used device, so
 |   on-screen text always names the device the player actually has in hand
 |   instead of listing both.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible
 | Params: pad -- hint text for gamepad + on-screen keyboard; kbd -- hint text for
 |   a real keyboard
 | Returns: whichever of pad/kbd matches the last-used device
 ----------------------*/
const char *hint(const char *pad, const char *kbd);

/*----------------------
 | note_input_device
 | Description: Updates which input device is considered active from this
 |   frame's key event: a real key hides the on-screen keyboard, a gamepad press
 |   shows it again. Call once per input frame with that frame's event.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_kbd_visible, g_pad
 | Params: ke -- this frame's keyboard event
 | Returns: N/A
 ----------------------*/
void note_input_device(const SaturnKeyEvent &ke);

/*----------------------
 | render_console
 | Description: Draws the current scrollback window into the console's text
 |   rows, clamping the scroll position and showing "^"/"more v" edge markers
 |   when off-screen text remains above/below.
 | Author: suinevere
 | Dependencies: console.c, SRL
 | Globals: g_scroll, g_more_below
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void render_console(void);

/*----------------------
 | console_scroll_to_output
 | Description: Positions the scrollback on the turn's newly-landed output: if
 |   the turn produced more lines than fit on screen, lands on its top row so the
 |   player reads from the start and pages down via "more v"; otherwise snaps to
 |   the live bottom.
 | Author: suinevere
 | Dependencies: console.c
 | Globals: g_output_start, g_scroll
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void console_scroll_to_output(void);

/*----------------------
 | install_block_glyph
 | Description: Carves a solid-block glyph into the unused DEL (0x7F) font slot
 |   so it can be printed as the blinking input-line text cursor.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void install_block_glyph(void);

/*----------------------
 | render_keyboard
 | Description: Draws the input line with its blinking block cursor, and --
 |   when the on-screen keyboard is showing -- the keyboard grid, its cursor
 |   marker, the CapsLock indicator, and the face-button legend below it.
 | Author: suinevere
 | Dependencies: keyboard.c, input.cxx, SRL
 | Globals: g_kbd_visible, g_more_below
 | Params: k -- current keyboard/input-line state; prediction -- the selected
 |   typeahead completion, or null; current_word_len -- length of the word being
 |   completed
 | Returns: N/A
 ----------------------*/
void render_keyboard(const KeyboardState &k, DictionaryWord* prediction, int current_word_len);

#endif /* CONSOLE_VIEW_H */
