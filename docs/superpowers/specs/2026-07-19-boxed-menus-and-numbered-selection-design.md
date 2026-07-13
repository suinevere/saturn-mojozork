# Boxed centered menus and keyboard-numbered selection

Date: 2026-07-19
Branch: sound-options
Status: approved, ready for planning

## Problem

Seven option pages already draw themselves as centered boxes via `menu_frame`
(`saturn/src/main.cxx:1319`), with a VDP2 window suppressing the background image
inside the box. Every other menu in the game still renders as bare text on a
cleared screen: the title mode menu, category and game select, device select,
save and restore slot select, the overwrite confirmation, the return-to-title
confirmation, the save-slot naming page, the online dialing sequence, and the
various one-line result screens.

Separately, digit selection is inconsistent. `menu_select` always prints `1)`,
`2)` prefixes and accepts `'1'`-`'9'`, whether or not a keyboard is in hand,
while the option pages offer no digit selection at all.

## Goals

1. Every menu the player can reach is a centered box with the same chrome.
2. Digits appear only when a keyboard is the active input device, and they work
   on every menu, including the option pages.

## Non-goals

- `toc_dump_page` (`main.cxx:1682`) stays unboxed. It is a debug table that
  already fills all 40 columns with dense TOC data; chrome would force
  truncation for no benefit.
- No change to the VDP2 windowing mechanism itself. `MenuBacking` and
  `menu_window_rect` are reused as they stand.
- No new input-driver capability. See the Shift constraint below.

## Design

### Sizing helper

```c
static void menu_box_fit(const char *title, int content_w, int rows,
                         int *x0, int *y0, int *w, int *h);
```

- `*w = max(content_w, strlen(title)) + 4` — two border columns plus one pad
  column on each side — clamped to 40.
- `*h = rows + 4` — top border, title row, blank row, content rows, bottom
  border — clamped to 28.
- Centered: `*x0 = (40 - *w) / 2`, `*y0 = (28 - *h) / 2`.

`menu_frame` is unchanged. Content origin stays its documented `(x0 + 2, y0 + 3)`
convention.

### Digit rendering rule

Digits render when `!g_kbd_visible` — the existing flag maintained by
`note_input_device` (`main.cxx:205`) and already used by `hint()`. No new state.

The `N) ` prefix costs three columns. Those three columns are reserved
unconditionally in every box's width calculation, whether or not digits are
currently drawn, so a box does not visibly resize when the player switches
between pad and keyboard mid-menu.

### `menu_select` (`main.cxx:1352`)

Boxed, with:

- `VIS` reduced from 20 to 16. A maxed-out list box is then 22 of 28 rows,
  leaving visible margin.
- Width sized to the longest item. Long game titles will push the box near full
  width; that is expected and accepted.
- **Digits map to visible rows, not absolute indices.** Pressing `3` selects the
  third row of the current scroll window. This keeps every entry of a long game
  list digit-reachable as the player scrolls. Only the first nine visible rows
  carry a digit.
- `^ more` / `v more` markers move inside the box.
- A `MenuBacking` guard is added; `menu_select` currently has none despite
  rendering over the image background.

This single change covers the title mode menu, category select, game select,
device select, and save/restore slot select.

### Option pages

```c
static int menu_row_digit(const SaturnKeyEvent &ke, int nrows, int *dir);
```

Returns a row index, or -1 for no match. Sets `*dir` to +1 for a plain digit and
-1 for the corresponding shifted symbol.

Each option page moves `sel` to the returned row and then applies the action that
Right (for `*dir == +1`) or Left (for `*dir == -1`) would apply to it. On rows
that are actions rather than value cyclers — OK, Cancel, and sub-page entries —
direction is ignored and the digit activates the row as Enter would.

Affected pages: Sound, Display, Controls, Configure Controls, Keyboard Controls,
Options.

**Digit selection is suppressed wherever a text field is being edited.** The
Network page (`config_page`, `main.cxx:1404`) is a dial-number entry screen and
`pick_slot_and_name` becomes one once `editing` is set: on those, a digit is
literal input and must reach the field. Network therefore gets the box but no
numbered rows at all; `pick_slot_and_name` numbers its slot rows only while
`editing` is clear.

**Shift constraint.** `SaturnKeyEvent` is `{kind, ch}` (`saturn_keyboard.h:42`)
and carries no modifier flag. Shift+digit is therefore detected by the shifted
character the keyboard emits: `!@#$%^&*(` for 1-9. This assumes a US layout.
Changing the driver to report modifiers is out of scope for this work.

### Newly boxed screens

| Screen | Location | Note |
|---|---|---|
| `menu_confirm` | `main.cxx:2184` | small box; `1) Yes  2) No` when a keyboard is active |
| `confirm_return_to_title` | `main.cxx:857` | boxed; see below |
| `pick_slot_and_name` | `main.cxx:~2060` | box grows when `editing` flips, to fit the on-screen keyboard; digits number the slot rows only while not editing |
| Result screens | `main.cxx:2266`, `2287`, `2847` | "Saved.", "Save FAILED (no space?).", "No save in that slot.", "No games found" |
| Online dialing | `online_mode`, `main.cxx:2981` | "Dialing...", "No carrier. Retrying...", "Connection failed.", "NetLink modem not found." |

A shared helper backs the last two groups:

```c
static void menu_message(const char *title, const char *line1, const char *line2);
```

It draws a fitted box and leaves the caller to decide whether to wait
(`menu_wait`) or keep redrawing per frame, which the dialing screens need.

**`confirm_return_to_title` drops `g_reboot_menu`.** That flag exists to shrink
the console so the prompt band below it can be cleared without erasing game text.
Once the prompt is a box that owns its own area and suppresses the background
inside it, the shrink is redundant. `g_reboot_menu` and `REBOOT_MENU_ROWS` are
removed along with the console-height branch at `main.cxx:191`.

## Verification

All of this is `main.cxx` — Saturn-only UI code with no host test harness. The
existing unit tests cover `display.c` and friends and will not exercise any of
it. Verification is therefore:

1. `saturn/compile.bat` builds clean (the user runs this; see project memory).
2. The existing unit suite still passes, confirming nothing shared was broken.
3. The user confirms behavior on hardware or emulator.

No claim stronger than "it builds and the existing tests still pass" can be made
from this side.

## Risks

- **Box height on long lists.** A 16-row list box is 22 of 28 rows. If any menu
  needs more visible rows than that, `VIS` must drop further rather than the box
  overflowing.
- **Long game titles.** Titles wider than 36 columns will make the box full-width
  and the centering visually meaningless. Acceptable; truncation was explicitly
  rejected.
- **Shifted-symbol detection is layout-dependent.** On a non-US keyboard,
  Shift+digit will not cycle backward. Left/Right still works everywhere.
