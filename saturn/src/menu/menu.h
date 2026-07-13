/*----------------------
 | menu.h
 | Description: The menu-drawing framework -- box chrome, the image-suppressing
 |   VDP2 window that keeps a background picture from showing through a menu's
 |   interior, the opaque-backing RAII guard, and the modal wait/message/list/
 |   confirm primitives every menu page (Options, Controls, Sound, Display,
 |   Network, save/load, online) is built from. Pages own their own content and
 |   input handling; this module owns only the chrome and the three interaction
 |   primitives.
 | Author: suinevere
 | Dependencies: menu_layout.c (box-fit/digit-mapping geometry), console_view.cxx
 |   (hint/note_input_device/render_console/g_kbd_visible), input.h (g_pad,
 |   Button), saturn_keyboard.h (SaturnKeyEvent/SATURN_KEY_*), soft_reset.h
 |   (check_soft_reset), sound.c (sound_service), music.c (music_tick), SRL
 ----------------------*/

#ifndef MENU_H
#define MENU_H

// Refcounted RAII guard that switches the image-suppressing VDP2 window on
// while at least one menu page is open, and off again once the last one
// closes. Refcounted rather than paired on/off calls because pages nest
// (Options opens Display, and the inner page closing must not clear the
// windowing while the outer one is still up); scoped rather than paired calls
// because every page has several exit paths, and "remember to undo this on
// all of them" is the exact shape of bug that has already cost this project a
// release -- a destructor cannot forget. Construct one at the top of any menu
// page that draws over a possible image background; the box owns its area for
// as long as the guard is alive.
struct MenuBacking {
    MenuBacking();
    ~MenuBacking();
};

// Backs MenuBacking's refcount. Also force-reset to 0 by main()'s soft-reset
// recovery path: the soft reset longjmps out of a possibly-nested menu, which
// skips the destructors that would normally decrement it back to 0.
extern int g_menu_backing_depth;

/*----------------------
 | menu_sync
 | Description: Advances one frame for a modal menu loop -- services looping
 |   PCM sound and the debounced Dynamic-mix music switch before synchronizing
 |   to vblank, so a menu wait does not let looping sound starve into silence
 |   or miss a pending music transition. Menu loops call this in place of a
 |   bare SRL::Core::Synchronize().
 | Author: suinevere
 | Dependencies: sound.c, music.c, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_sync(void);

/*----------------------
 | menu_clear
 | Description: Blanks every console text row, so a menu page can redraw its
 |   box chrome cleanly this frame.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_clear(void);

/*----------------------
 | menu_frame
 | Description: Draws a w x h box of +--+ chrome at (x0, y0) with `title`
 |   centered on its second row, and aims the image-suppressing VDP2 window at
 |   the same rectangle. Every menu page uses this so the chrome and title
 |   placement stay identical; pages differ only in the box they ask for. The
 |   caller owns the interior: content starts at (x0 + 2, y0 + 3) by convention
 |   (row y0 + 2 stays blank under the title) and must stay inside x0 + w - 2
 |   so it never overwrites the right border.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: x0, y0 -- top-left corner of the box, in text cells; w, h -- box
 |   width/height in cells; title -- text centered on the box's second row
 | Returns: N/A
 ----------------------*/
void menu_frame(int x0, int y0, int w, int h, const char *title);

/*----------------------
 | menu_wait
 | Description: Blocks until any button or key is pressed. Used for "press any
 |   key" prompts.
 | Author: suinevere
 | Dependencies: input.h, saturn_keyboard.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_wait(void);

/*----------------------
 | menu_message
 | Description: Draws a centered box titled `title` with one or two lines of
 |   text, sized to fit both lines (including any hint text passed as line2)
 |   without resizing when the player switches input device. Returns at once
 |   without waiting or synchronizing: callers that want a blocking prompt
 |   follow it with menu_wait(); callers that redraw every frame (the dialing
 |   screens) do not.
 | Author: suinevere
 | Dependencies: menu_layout.c, SRL
 | Globals: N/A
 | Params: title -- box title; line1 -- first line of text; line2 -- second
 |   line of text, or NULL for a single-line box
 | Returns: N/A
 ----------------------*/
void menu_message(const char *title, const char *line1, const char *line2);

/*----------------------
 | menu_select
 | Description: Modal, scrollable list menu titled `title` over `count` items
 |   in `items`. Navigable by gamepad (D-pad to move, A/C/Start to pick, B to
 |   cancel) or keyboard (number keys pick a visible row directly, Enter picks
 |   the highlighted item, Backspace/Esc cancels). Polls the soft-reset chord
 |   every loop.
 | Author: suinevere
 | Dependencies: menu_layout.c, console_view.cxx, input.h, saturn_keyboard.h,
 |   soft_reset.h, SRL
 | Globals: g_pad, g_kbd_visible
 | Params: title -- box title; items -- array of item strings; count -- number
 |   of items
 | Returns: the chosen item's 0-based index, or -1 if cancelled
 ----------------------*/
int menu_select(const char *title, const char *const *items, int count);

/*----------------------
 | menu_confirm
 | Description: Modal Yes/No confirmation box showing `line1` and an optional
 |   `line2`. Accepts C/A/Start/Enter/Y as yes and B/N/Esc/Backspace as no.
 |   Unlike confirm_return_to_title, only reports the answer -- it does not act
 |   on it.
 | Author: suinevere
 | Dependencies: menu_layout.c, console_view.cxx, input.h, saturn_keyboard.h,
 |   SRL
 | Globals: g_pad, g_kbd_visible
 | Params: line1 -- first line of the question; line2 -- second line, or NULL
 | Returns: true if confirmed, false if declined
 ----------------------*/
bool menu_confirm(const char *line1, const char *line2);

#endif /* MENU_H */
