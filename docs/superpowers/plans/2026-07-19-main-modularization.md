# main.cxx Modularization + Codebase Documentation Convention — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `saturn/src/main.cxx` (3,595 lines) into a thin orchestrator by relocating its self-contained clusters into topical modules, and apply one uniform documentation convention (file + function box headers, no inline comments, dead code removed) across every in-scope C/C++ and Python source.

**Architecture:** Physical relocation only — SRL/Saturn calls stay inline, no new interfaces or unit tests. Cross-cutting globals become an `extern` seam in `app_state.h`; cluster-local state moves with its functions as `static`. The makefile globs `src/*.c`/`*.cxx`, so new files need no build edits. Correctness is confirmed by compiling with `saturn/compile.bat` at each phase boundary and keeping the host tests green.

**Tech Stack:** C/C++ (SaturnRingLib / SGL), Python 3 (asset + table generators), GNU make, host unit tests (`saturn/tests/*.c` via `CMakeLists.txt`).

## Global Constraints

- **Build command:** `saturn/compile.bat` (never invoke it yourself — the user compiles; make the edits and hand off at each compile checkpoint). Host tests build via the top-level `CMakeLists.txt` / existing test runners.
- **No behavior change.** Every screen, menu, key, save slot, and the online mode must behave identically before and after. This is mechanical.
- **No logic refactor.** Only relocation, the comment convention, and removal of code proven dead. No renaming, no control-flow changes.
- **No build-system edits.** `saturn/makefile` auto-globs `src/`. `CMakeLists.txt` is touched only if a moved file breaks a host-test include path (fix the include, add nothing new).
- **Box format (C/C++):** exactly —
  ```
  /*----------------------
   | Function Name
   | Description:
   | Author: suinevere
   | Dependencies: other-file deps required for this function
   | Globals: globals interacted with
   | Params: inputs
   | Returns: output
   ----------------------*/
  ```
- **Box format (Python):** same fields/order, `#` delimiters (see Recipe).
- **File-level box** at the top of every file: `File Name` / `Description` / `Author: suinevere` / `Dependencies`.
- **`.h` box = what; `.c`/`.cxx` box = how.** Python has one box per `def` (what, plus a how line when non-obvious). `static` C functions (no declaration) get their sole box above the definition, carrying the *how*.
- **`Author: suinevere`** on every box. Every empty field is `N/A`, never blank.
- **Remove all inline comments** (in-body + trailing). Fold any non-obvious *why* into the box Description at whatever length it needs — no sentence cap. File/section banner comments (`---- rendering ----`) may stay as structure.
- **Dead code** (defined but never referenced in the in-scope tree) is surfaced as a candidate list per file and **confirmed with the user before deletion** — never deleted blind.
- **Scope:** `saturn/src/*.{c,cxx,h}` (incl. `net/`) and `tools/*.py` (excl. `tools/.venv/`). Out of scope: `mojozork.c`, `multizorkd.c`, `mojozork-libretro.c`, `mojozork-sdl3.c`; generated *data* tables inside generated `.c` files.

---

## The Box-Writing Recipe (shared by every documentation task)

Every documentation step below means: **apply this recipe to each function in the file.** It is deterministic — the fields are read off the code, not invented.

For a function, fill the box fields by inspecting its body:

1. **Function Name** — the identifier.
2. **Description** — `.h`: what a caller gets (behavior/contract). `.c`/`.cxx`: how it works (the mechanism). Fold in any *why* the deleted inline comments carried that isn't recoverable from the code.
3. **Author** — `suinevere`.
4. **Dependencies** — the other **files/modules** this function calls into: scan the body for calls resolved by another header (`display.h`→"display.c", `menu_layout.h`→"menu_layout.c", SRL types/`SRL::…`→"SRL", `GFS_*`→"SRL/GFS", `net/…`→"net_connect.c"). List those files. `N/A` if it calls only its own translation unit + the C/C++ standard library.
5. **Globals** — `app_state` externs and file-scope statics the body reads or writes (e.g. `g_display`, `g_pad`, `g_scroll`). `N/A` if none.
6. **Params** — each parameter and its meaning; `N/A` for `(void)`/no args.
7. **Returns** — the return value's meaning; `N/A` for `void`.

Then **delete every inline comment** in the body, moving any surviving rationale into the Description.

### Worked example A — C function with `.h`/`.cxx` split

Source today (`main.cxx`), an internal helper with rich rationale:

```cpp
// Push g_display to the hardware. text_set_color writes both the glyph and the
// cursor CRAM entries, so this recolors body text, menus, the on-screen
// keyboard, and the cursor in one call.
// (SRL::Debug::PrintColorSet is not usable here: it sets slCurColor while
// Debug::Print reads ASCII::colorBank.)
// Returns false when the requested background could not be shown ...
static bool display_apply(void) {
    text_set_color(display_text_rgb(g_display.text));
    // Set the back plane before any image load. It is what shows through ...
    SRL::VDP2::SetBackColor(...);
    if (display_is_image(&g_display)) { ... title_bg_show(...) ... }
    ...
}
```

`display_apply` has no `.h` declaration today (it is `static`). Under this plan it moves into `options.cxx` and, because `menu_pages.cxx` and `main.cxx` call it, gains a declaration in `options.h`. Result:

`options.h` (what):
```c
/*----------------------
 | display_apply
 | Description: Pushes the current display settings (g_display) to VDP2 so body
 |   text, menus, the on-screen keyboard, and the cursor all take the new colors,
 |   loading the selected background image when one is chosen. Returns false when
 |   the chosen background could not be shown, so a caller cycling presets can
 |   step over a bad one instead of settling on the fallback it installs.
 | Author: suinevere
 | Dependencies: display.c, title.c (title_bg_show/title_bg_hide), SRL
 | Globals: g_display
 | Params: N/A
 | Returns: true if the requested display was applied; false if it fell back
 ----------------------*/
bool display_apply(void);
```

`options.cxx` (how — preserving the caveats the inline comments carried):
```c
/*----------------------
 | display_apply
 | Description: Recolors via text_set_color (writes both the glyph CRAM entry
 |   and install_block_glyph's cursor entry in one call; SRL::Debug::PrintColorSet
 |   is unusable here because it sets slCurColor while Debug::Print reads
 |   ASCII::colorBank). Sets the back plane BEFORE any image load, because it is
 |   what shows through the transparent menu frames and is on screen during the
 |   1-2s CD read. On image-load failure it drops to a color preset -- and if the
 |   failed palette WAS the image preset, rewrites it to preset 12 (IBM PC/MDA)
 |   so the broken picture is not re-selected.
 | Author: suinevere
 | Dependencies: display.c, title.c, SRL
 | Globals: g_display
 | Params: N/A
 | Returns: true if applied; false if a load failed and the fallback was installed
 ----------------------*/
bool display_apply(void) { ... /* body, inline comments removed */ }
```

### Worked example B — Python `def`

Before (`tools/gametitles/gen_titles.py`, illustrative):
```python
def load_rows(paths):
    # read each story file and pull its serial/release
    rows = []
    for f in paths:
        d = open(f, "rb").read()   # raw z-machine header
        rows.append(parse_header(d))
    return rows
```

After:
```python
#----------------------
# load_rows
# Description: Reads each story file and extracts its (release, serial) header
#   pair, returning one row per path in input order.
# Author: suinevere
# Dependencies: parse_header (same module)
# Globals: N/A
# Params: paths -- list of story-file paths to read
# Returns: list of (release, serial) rows, one per path
#----------------------
def load_rows(paths):
    rows = []
    for f in paths:
        d = open(f, "rb").read()
        rows.append(parse_header(d))
    return rows
```

### File-level box example
```c
/*----------------------
 | options.cxx
 | Description: Load/save of the persisted MOJOOPTS blob and the runtime apply of
 |   display settings to VDP2. Owns no menu UI -- the option pages call in here.
 | Author: suinevere
 | Dependencies: app_state.h, display.h, saturn_backup.h, title.h, SRL
 ----------------------*/
```

---

## File Structure (target)

New modules carved from `main.cxx` (each `.h` + matching `.c`/`.cxx`):

| Module | Responsibility |
|---|---|
| `app_state` | The cross-cutting `extern` globals + game-option enums (`DIFF_*`, `MIX_*`). Definitions in `app_state.cxx`. |
| `options` | `options_load/save`, `display_apply`, `display_cycle_row`, `valid_dialnum`. |
| `console_view` | Text-screen + on-screen-keyboard rendering and input-device hinting. |
| `input` | Controller mapping, pad repeat/scroll, command history, mapping editors' assign helpers. |
| `menu` | Menu-drawing framework (frames, boxes, select, confirm, message, sync). |
| `menu_pages` | Every option page + `options_menu`. |
| `save_ui` | Save/restore device + slot pickers. |
| `title` | Title screen, background art, TGA loading, CD directory juggling, seed. |
| `game_catalog` | CD game scan, catalog build, `game_select`. |
| `online` | Network play mode. |
| `main.cxx` (trimmed) | `main()`, game loop, `submit_command`, soft-reset, typeahead glue. |

`app_state.cxx` cross-cutting globals (moved out of `main.cxx`, defined once here, declared `extern` in `app_state.h`). **`app_state.h` must stay C-includable** (POD/`extern "C"` only), so no C++-typed globals live here:

`g_difficulty`, `g_music_level`, `g_pcm_level`, `g_mix_mode`, `g_sel_track`, `g_display` (POD `DisplayState`), `g_dialnum`, `g_restore_device`, `g_restore_slot`, `g_save_device`, `g_save_slot`, `g_last_device`, `g_last_slot`, `g_autocmd`, `g_story_filename`, `g_title_jmp` (`jmp_buf`), `g_title_jmp_armed`. Plus the `DIFF_*`, `MIX_*`, `DIALNUM_MAX` definitions.

**NOT in app_state:** `g_pad` and its `MultiPad` struct (a C++ type with `SRL::Input` members and methods) belong in the `input` module (Task 4), which is C++-only. There is no `g_sound` global — audio on/off is expressed through `g_music_level`/`g_pcm_level` (0 = off).

**Cross-edge already identified:** `display_apply` (options) calls `title_bg_show`/`title_bg_hide` (title). Because `title` is extracted later (Phase 3) than `options` (Phase 1), `options.cxx` must see a declaration of those two before `title.h` exists. Resolution: add a two-line forward declaration in `options.cxx` in Phase 1, and replace it with `#include "title.h"` in Phase 3 (Task 8's final step). The plan calls this out at both ends.

**Every task's move follows the same mechanical shape** (stated once here, referenced per task):

> **Standard module-extraction procedure (SMEP):**
> 1. Create `NAME.h`: file-box, include guard + `extern "C"` guard matching `display.h`, and a declaration (with `.h` *what*-box) for each function other translation units call. Purely file-local helpers get **no** declaration.
> 2. Create `NAME.cxx`: file-box, include `NAME.h` + the module headers its bodies need. Move the listed functions here verbatim, then apply the **Box-Writing Recipe** (`.cxx` *how*-boxes) and strip inline comments. Move the listed cluster-local `static` globals here too.
> 3. Edit `main.cxx`: delete the moved functions/globals; add `#include "NAME.h"`.
> 4. Note any function the file references that still lives in `main.cxx` (or a not-yet-extracted module) and ensure a declaration is visible (via that module's header if it exists, else a temporary forward declaration resolved when that module is extracted).
> 5. Run the per-file dead-code check; record candidates (do not delete yet).

---

## Phase 1 — Substrate (compile checkpoint at end)

### Task 1: `app_state` seam

**Files:**
- Create: `saturn/src/app_state.h`, `saturn/src/app_state.cxx`
- Modify: `saturn/src/main.cxx` (remove the cross-cutting global definitions, add include)

**Interfaces:**
- Produces: `extern` declarations for the 17 cross-cutting globals listed in File Structure (the C-safe set — `g_pad` is excluded, it goes to Task 4), plus `enum { DIFF_EASY, DIFF_MEDIUM, DIFF_HARD }`, `enum { MIX_DYNAMIC, MIX_OVERRIDE, MIX_SEQUENTIAL, MIX_RANDOM }`, and `#define DIALNUM_MAX 11` (copy exact values from `main.cxx`).

**Exact definitions to move** (from `main.cxx`, preserve verbatim including initializers):
```
static int g_difficulty = DIFF_EASY;
static int g_music_level = 7;
static int g_pcm_level   = 4;
static int g_mix_mode  = MIX_DYNAMIC;
static int g_sel_track = 10;
static DisplayState g_display;
static char g_dialnum[DIALNUM_MAX + 1] = "199403";
static int g_restore_device = -1;
static int g_restore_slot   = -1;
static const char *g_autocmd = nullptr;
static int g_last_device = -1;
static int g_last_slot   = -1;
static int g_save_device = -1;
static int g_save_slot   = -1;
static jmp_buf  g_title_jmp;
static bool     g_title_jmp_armed = false;
static const char *g_story_filename = "ZORK1.Z3";
static int g_scroll = 0;
```
`g_scroll` (currently `main.cxx:439`) is included here deliberately: it is the console scroll position, **written by the input module** (`scroll_handle_key`, `pad_scroll_update`) and **read by console_view** (`render_console`). Housing it in the neutral C-safe seam breaks what would otherwise be a mutual `input`↔`console_view` header cycle. Its companion consts `SCROLL_PAGE`/`SCROLL_ALL` are input-only and stay with the input module (Task 3).

Drop the `static` when defining them in `app_state.cxx` (they gain external linkage). `#define DIALNUM_MAX 11` currently sits at `main.cxx:47`; the `DIFF_*` and `MIX_*` enums are near the top comments — grep for their definitions (`grep -n "DIFF_EASY\|MIX_DYNAMIC" main.cxx`) and move the enum definitions too. In `app_state.cxx`, `nullptr` is valid (it is a `.cxx`); keep it.

- [x] **Step 1: Create `app_state.h`** — file-box; `#ifndef APP_STATE_H`; `#include <setjmp.h>` and `#include "display.h"` (for `DisplayState`); `extern "C"` guard; the enums/`DIALNUM_MAX`; one `extern` line per global with a short `.h` *what*-box each (e.g. `g_display` → "Current display colors/background, applied to VDP2 by display_apply and persisted in MOJOOPTS.").
- [x] **Step 2: Create `app_state.cxx`** — file-box; `#include "app_state.h"`; the single **definition** of each global, moved verbatim from `main.cxx` (preserve initializers exactly: `g_dialnum = "199403"`, `g_story_filename = "ZORK1.Z3"`, `g_difficulty = DIFF_EASY`, etc.).
- [x] **Step 3: Edit `main.cxx`** — delete those 17 definitions and the `DIFF_*`/`MIX_*`/`DIALNUM_MAX` definitions; add `#include "app_state.h"` beneath the existing includes.
- [x] **Step 4: Commit**
```bash
git add saturn/src/app_state.h saturn/src/app_state.cxx saturn/src/main.cxx
git commit -m "Extract cross-cutting app_state seam from main.cxx"
```

### Task 2: `options` module

**Files:**
- Create: `saturn/src/options.h`, `saturn/src/options.cxx`
- Modify: `saturn/src/main.cxx`

**Interfaces:**
- Consumes: `app_state.h` globals; `display.h`; `saturn_backup.h`.
- Produces: `void options_load(void); void options_save(void); bool display_apply(void); void display_cycle_row(DisplayCycleRow which, int dir); bool valid_dialnum(const char *s);` and `enum DisplayCycleRow { DCR_PALETTE, DCR_BG, DCR_TEXT };` (move the enum into `options.h`).

- [x] **Step 1: Apply SMEP** for functions `options_load`, `options_save`, `display_apply`, `display_cycle_row`, `valid_dialnum` and the `DisplayCycleRow` enum. `text_set_color` is used only by `display_apply` and `console_view` — keep a copy decision: it belongs to display hardware, so move it into `options.cxx` as `static` **and** expose it via `options.h` if `console_view` needs it; grep first (`grep -n text_set_color main.cxx`) and if `console_view`'s functions use it, declare it in `options.h`.
- [x] **Step 2: Forward-declare title hooks** — at the top of `options.cxx` add `bool title_bg_show(const char *file); void title_bg_hide(void);` with a one-line comment box noting these are resolved by `#include "title.h"` in Phase 3. (Recorded cross-edge.)
- [x] **Step 3: Dead-code check** — `for f in options_load options_save display_apply display_cycle_row valid_dialnum; do echo "$f:"; grep -rn "\\b$f\\b" saturn/src | grep -v "options\." ; done` — confirm each has a caller outside its own file; record any with none.
- [x] **Step 4: Commit**
```bash
git add saturn/src/options.h saturn/src/options.cxx saturn/src/main.cxx
git commit -m "Extract options persistence/apply into options module"
```

### Task 3: `input` module

*(Ordered before `console_view` on purpose: `console_view` calls `g_pad` and so includes `input.h`; `input` now depends only on `app_state` — the one-directional edge. Do Task 3 before Task 4.)*

**Files:**
- Create: `saturn/src/input.h`, `saturn/src/input.cxx`
- Modify: `saturn/src/main.cxx`

**Functions to move:** `scroll_handle_key`, `face_button`, `face_btn_name`, `slot_name`, `slot_raw`, `caps_combo_fired`, `chord_tick`, `chord_fired`, `pad_scroll_update`, `pad_repeat_update`, `pad_fired`, `history_push`, `history_load`, `history_recall`, `face_assign`, `chord_assign`, `mapping_reset_defaults`.
**Cluster-local statics to move:** `g_chord_slot`, `g_chordrep`, `g_padrep`, `g_history`, `g_hist_head`, `g_hist_browse`, `FACE_DEFAULT`, `CHORD_DEFAULT`, `PAD_SCROLL_DELAY`, `PAD_SCROLL_RATE`, `SCROLL_PAGE`, `SCROLL_ALL`, the `SL_*`/`FA_*`/`CA_*` enums and slot tables, plus the `ChordRep`/`PadRepeat` structs. (`g_scroll` itself now lives in `app_state` — Task 1; the input functions write it via `app_state.h`.)
**Also move here (from `main.cxx`, deferred from Task 1):** the `MultiPad` struct definition (`main.cxx` lines ~116-144, a C++ type with `SRL::Input::Digital/Analog` members and `WasPressed`/`IsHeld`/`AnyPressed` methods) and `static MultiPad *g_pad = nullptr;`. Put the `MultiPad` struct in `input.h` and declare `extern MultiPad *g_pad;` there; define `g_pad` in `input.cxx`. `main.cxx` allocates the backing `static MultiPad pads;` in `main()` and sets `g_pad = &pads;` — leave that line in `main()`, it now writes the `input.h`-declared global.

**Interfaces:**
- Consumes: `app_state.h` (`g_scroll`, and the option globals if any), `saturn_keyboard.h`, `keyboard.h`, SRL `Button`/`SRL::Input`. **No `console_view` dependency.**
- Produces (input.h, C++-only): the `MultiPad` struct and `extern MultiPad *g_pad;`, consumed by `console_view`, `menu`, `menu_pages`, `save_ui`, `online`, and `main.cxx`.
- Produces: declarations for what the loop and menu pages call: `face_button`, `face_btn_name`, `slot_name`, `slot_raw`, `caps_combo_fired`, `chord_tick`, `chord_fired`, `pad_scroll_update`, `pad_repeat_update`, `pad_fired`, `scroll_handle_key`, `history_push`, `history_load`, `history_recall`, `face_assign`, `chord_assign`, `mapping_reset_defaults`, and the `FA_*`/`CA_*`/`SL_*` enums (move enums into `input.h` since `menu_pages` reads them).

- [x] **Step 1: Move the mapping enums/labels** — `FA_*`, `CA_*`, `SL_*` and their `*_DEFAULT` tables into `input.h`/`input.cxx`. `FACE_LABEL`/`CHORD_LABEL` are used only by menu pages → leave them for Task 6, but if `face_btn_name`/`slot_name` reference them, they move here; grep to decide.
- [x] **Step 2: Apply SMEP** for the listed functions and statics, including `MultiPad`/`g_pad`.
- [x] **Step 3: Dead-code check** over the moved names.
- [x] **Step 4: Commit**
```bash
git add saturn/src/input.h saturn/src/input.cxx saturn/src/main.cxx
git commit -m "Extract controller mapping / pad / history into input module"
```

### Task 4: `console_view` module

*(Runs after Task 3. `console_view.cxx` includes `input.h` for `g_pad` (used by `note_input_device`) and `app_state.h` for `g_scroll`.)*

**Files:**
- Create: `saturn/src/console_view.h`, `saturn/src/console_view.cxx`
- Modify: `saturn/src/main.cxx`

**Functions to move:** `console_height`, `hint`, `note_input_device`, `text_set_color` (if not already owned by options — reconcile with Task 2), `render_console`, `console_scroll_to_output`, `install_block_glyph`, `render_keyboard`.
**Cluster-local statics to move:** `g_kbd_visible`, `g_caret_arrows`, `g_more_below`, `SCREEN_ROWS`, `TOP_MARGIN`, `KB_ROWS` and related layout consts declared beside these functions. (`g_scroll` is NOT here — it moved to `app_state` in Task 1.)

**Interfaces:**
- Consumes: `app_state.h` (`g_scroll`), `input.h` (`g_pad`), `console.h`, `keyboard.h`, `saturn_keyboard.h`, `typeahead.h`, SRL.
- Produces: declarations for the functions `main.cxx`'s loop calls (`render_console`, `render_keyboard`, `console_height`, `console_scroll_to_output`, `install_block_glyph`, `note_input_device`, `hint`) with `.h` *what*-boxes. `g_kbd_visible`/`g_caret_arrows`/`g_more_below` are also written by `main.cxx` (e.g. `main.cxx:1082`, `main.cxx:3389`), so declare them `extern` in `console_view.h` rather than leaving them file-`static` (grep to confirm which; any read/written outside `console_view` gets the `extern` treatment).

- [x] **Step 1: Grep the rendering statics** — `for g in g_kbd_visible g_caret_arrows g_more_below; do echo "$g:"; grep -rn "\\b$g\\b" saturn/src | grep -v console_view; done`. Any with external references get `extern` in `console_view.h`; purely local ones stay `static` in `console_view.cxx`. Record the decision in the commit message.
- [x] **Step 2: Apply SMEP** for the listed functions and resolved statics. `console_view.cxx` includes `input.h` (for `g_pad`) and `app_state.h` (for `g_scroll`).
- [x] **Step 3: Dead-code check** (same pattern as Task 2 Step 3 over the moved names).
- [x] **Step 4: Commit**
```bash
git add saturn/src/console_view.h saturn/src/console_view.cxx saturn/src/main.cxx saturn/src/app_state.h saturn/src/app_state.cxx
git commit -m "Extract text/keyboard rendering into console_view module"
```

- [x] **Phase 1 compile checkpoint** — hand off: user runs `saturn/compile.bat` and the host tests. Proceed only on a clean link + green tests. Fix any unresolved symbol by adding the missing declaration to the owning module's header (or a temporary forward declaration per SMEP step 4).

---

## Phase 2 — Menu system (compile checkpoint at end)

### Task 5: `menu` framework module

**Files:** Create `saturn/src/menu.h`, `saturn/src/menu.cxx`; Modify `main.cxx`.
**Functions to move:** `menu_sync`, `menu_clear`, `menu_window_rect`, `menu_frame`, `menu_wait`, `menu_message`, `menu_select`, `menu_confirm`.
**Statics to move:** `g_menu_backing_depth`, `MENU_SELECT_HINT_PAD`, `MENU_SELECT_HINT_KBD`.

**Interfaces:**
- Consumes: `menu_layout.h`, `app_state.h`, `console_view.h`, `input.h`, `display.h`, `sound.h` (`menu_sync` services sound), SRL.
- Produces: `void menu_sync(void); void menu_clear(void); void menu_window_rect(int,int,int,int); void menu_frame(int,int,int,int,const char*); void menu_wait(void); void menu_message(const char*,const char*,const char*); int menu_select(const char*,const char*const*,int); bool menu_confirm(const char*,const char*);` with `.h` *what*-boxes.

- [x] **Step 1: Apply SMEP** for the listed functions/statics.
- [x] **Step 2: Dead-code check.**
- [x] **Step 3: Commit**
```bash
git add saturn/src/menu.h saturn/src/menu.cxx saturn/src/main.cxx
git commit -m "Extract menu-drawing framework into menu module"
```

### Task 6: `menu_pages` module

**Files:** Create `saturn/src/menu_pages.h`, `saturn/src/menu_pages.cxx`; Modify `main.cxx`.
**Functions to move:** `config_page`, `configure_controls_page`, `controls_page`, `keyboard_controls_page`, `toc_dump_page`, `sound_options_page`, `display_options_page`, `options_menu`.
**Statics to move:** `FACE_LABEL`, `CHORD_LABEL` (unless Task 4 already claimed them).

**Interfaces:**
- Consumes: `menu.h`, `input.h`, `options.h`, `display.h`, `music.h`, `sound.h`, `app_state.h`, `typeahead.h`.
- Produces: `void options_menu(void);` (the only entry the loop calls) with `.h` *what*-box; the six page functions may stay file-local (`static`) if only `options_menu` calls them — grep; declare in `.h` only those with external callers.

- [x] **Step 1: Apply SMEP.** Verify each page's external calls resolve to already-extracted headers (`menu.h`, `input.h`, `options.h`, `music.h`, `sound.h`).
- [x] **Step 2: Dead-code check.**
- [x] **Step 3: Commit**
```bash
git add saturn/src/menu_pages.h saturn/src/menu_pages.cxx saturn/src/main.cxx
git commit -m "Extract option pages into menu_pages module"
```

### Task 7: `save_ui` module

**Files:** Create `saturn/src/save_ui.h`, `saturn/src/save_ui.cxx`; Modify `main.cxx`.
**Functions to move:** `make_slot_name`, `choose_device`, `pick_slot_and_name`.

**Interfaces:**
- Consumes: `menu.h`, `saturn_backup.h`, `app_state.h`, `saturn_keyboard.h`.
- Produces: `void make_slot_name(char *out, int slot); int choose_device(const char *title); int pick_slot_and_name(int device, int *out_slot, char *out_name, int maxchars);` with `.h` *what*-boxes.

- [x] **Step 1: Apply SMEP.**
- [x] **Step 2: Dead-code check.**
- [x] **Step 3: Commit**
```bash
git add saturn/src/save_ui.h saturn/src/save_ui.cxx saturn/src/main.cxx
git commit -m "Extract save/restore pickers into save_ui module"
```

- [x] **Phase 2 compile checkpoint** — user runs `saturn/compile.bat` + host tests. Proceed only on clean link.

---

## Phase 3 — Boot / content (compile checkpoint at end)

### Task 8: `title` module

**Files:** Create `saturn/src/title.h`, `saturn/src/title.cxx`; Modify `main.cxx`, `saturn/src/options.cxx`.
**Functions to move:** `title_draw_art`, `tga_name_is_usable`, `cd_capture_root`, `cd_enter_root`, `cd_enter_tga`, `cd_restore_z3`, `bitmap_read_end`, `display_scan_images`, `tga_load_nbg0`, `title_bg_show`, `title_bg_hide`, `title_and_seed`.
**Statics to move:** `g_image_name`, `g_image_ptr`, `g_root_dirnames`, `g_root_tbl`, `g_root_dir_valid`, `g_tga_dirnames`, `g_tga_tbl`, `g_tga_dir_valid`.

**Interfaces:**
- Consumes: `display.h` (`display_set_images`, `DISP_*`), `app_state.h`, `menu.h`, SRL/GFS.
- Produces: `bool title_bg_show(const char *file); void title_bg_hide(void); int title_and_seed(void); void display_scan_images(void);` (and any other function `main.cxx`/`options` call) with `.h` *what*-boxes.

- [x] **Step 1: Apply SMEP** for the listed functions/statics.
- [x] **Step 2: Resolve the recorded cross-edge** — in `options.cxx`, delete the Phase-1 forward declarations of `title_bg_show`/`title_bg_hide` and add `#include "title.h"`.
- [x] **Step 3: Dead-code check.** Note the three near-identical CD-scan blocks (`root`/`tga`/`z3`) — do **not** consolidate (out of scope); just box each.
- [x] **Step 4: Commit**
```bash
g it add saturn/src/title.h saturn/src/title.cxx saturn/src/main.cxx saturn/src/options.cxx
git commit -m "Extract title screen / background art / TGA loading into title module"
```

### Task 9: `game_catalog` module

**Files:** Create `saturn/src/game_catalog.h`, `saturn/src/game_catalog.cxx`; Modify `main.cxx`.
**Functions to move:** `has_z3_ext`, `scan_z3_folder`, `read_game_info`, `label_cmp`, `label_year`, `preload_game_catalog`, `game_select`.
**Statics to move:** `g_z3_dirnames`, `g_z3_tbl`, `g_z3_dir_valid`, `CAT_NAMES`, `MAX_GAMES`, `names`, `cats`, `g_catalog_count`, `g_catalog_ready`.

**Interfaces:**
- Consumes: `menu.h`, `game_titles.h`, `app_state.h`, `title.h` (if `game_select` draws over the title bg), SRL/GFS.
- Produces: `const char* game_select(void); void preload_game_catalog(void);` with `.h` *what*-boxes. `game_select` currently has external (non-`static`) linkage — keep it `extern` and declare in `game_catalog.h`.

- [x] **Step 1: Apply SMEP.**
- [x] **Step 2: Dead-code check.**
- [x] **Step 3: Commit**
```bash
git add saturn/src/game_catalog.h saturn/src/game_catalog.cxx saturn/src/main.cxx
git commit -m "Extract CD game scan / catalog / select into game_catalog module"
```

- [x] **Phase 3 compile checkpoint** — user runs `saturn/compile.bat` + host tests.

---

## Phase 4 — Modes + trim (compile checkpoint at end)

### Task 10: `online` module

**Files:** Create `saturn/src/online.h`, `saturn/src/online.cxx`; Modify `main.cxx`.
**Functions to move:** `online_cancel_requested`, `online_wait_any`, `online_settle_input`, `ensure_online_typeahead`, `online_mode`.
**Statics to move:** `g_online_ta`, `g_online_diff`.

**Interfaces:**
- Consumes: `net/net_connect.h`, `app_state.h`, `console_view.h`, `input.h`, `typeahead.h`, `menu.h`.
- Produces: `void online_mode(void);` (the only entry `main.cxx` calls) with `.h` *what*-box; the helpers stay `static` unless grep shows external use.

- [ ] **Step 1: Apply SMEP.**
- [ ] **Step 2: Dead-code check.**
- [ ] **Step 3: Commit**
```bash
git add saturn/src/online.h saturn/src/online.cxx saturn/src/main.cxx
git commit -m "Extract network play into online module"
```

### Task 11: Document the trimmed `main.cxx`

**Files:** Modify `saturn/src/main.cxx`.
**Remaining functions:** `main`, `submit_command`, `is_reboot_command`, `is_alnum_ch`, `is_quit_command`, `soft_reset_to_title`, `soft_reset_chord_held`, `check_soft_reset`, `ensure_typeahead`, `typeahead_scan_screen` (+ their statics `g_typeahead_root`, `g_ta_story`, `g_ta_diff`).

- [ ] **Step 1: Add the file-level box** to `main.cxx` describing it as the entry point + game loop + soft-reset + typeahead glue, listing its module dependencies.
- [ ] **Step 2: Apply the Box-Writing Recipe** to the ten remaining functions (`.cxx` *how*-boxes; `main` and the `is_*` helpers included), stripping their inline comments and folding the soft-reset/overscan/refcount rationale into the boxes.
- [ ] **Step 3: Verify main.cxx now includes** every module header it calls into and defines nothing that a module owns. `grep -cE '^\s*(static\s+)?[A-Za-z].*\)\s*\{' main.cxx` should be ~10.
- [ ] **Step 4: Dead-code check** over the remaining names.
- [ ] **Step 5: Commit**
```bash
git add saturn/src/main.cxx
git commit -m "Trim main.cxx to orchestrator; box remaining functions"
```

- [ ] **Phase 4 compile checkpoint** — user runs `saturn/compile.bat` + host tests + a manual smoke (boot to title, open Options, save/load, enter online mode). Proceed only when all reachable.

---

## Phase 5 — Documentation pass over pre-existing modules (compile checkpoint at end)

No relocation — file-box + function-boxes + inline-comment removal + dead-code check per file, applying the Box-Writing Recipe. Group into compile-safe sub-batches. For each file: add the file-box, box each function (declarations in the `.h` get *what*, definitions in the `.c`/`.cxx` get *how*), strip inline comments preserving rationale, record dead-code candidates.

### Task 12: display + menu_layout
**Files:** `display.h/.c`, `menu_layout.h/.c`. These already carry rich header comments — convert them into the box format (their existing prose becomes the Description) rather than rewriting. Commit: `git commit -m "Apply doc convention to display and menu_layout"`.

### Task 13: audio
**Files:** `music.h/.c`, `music_cdda.cxx`, `music_data.c`, `sound.h/.cxx`, `sound_blorb.h/.c`. Commit: `"Apply doc convention to audio modules"`.

### Task 14: input/text glue
**Files:** `keyboard.h/.c`, `saturn_keyboard.h/.cxx`, `console.h/.c`, `term.h/.c`, `typeahead.h/.c`, `typeahead_extract.h/.c`. (`typeahead_solution.c` handled in Phase 6 via its generator.) Commit: `"Apply doc convention to input/text modules"`.

### Task 15: platform glue
**Files:** `saturn_backup.h/.cxx`, `saturn_compat.h/.cxx`, `saturn_filestub.c`, `mojozork_saturn.c`, `saturn_glue.h`. Commit: `"Apply doc convention to platform-glue modules"`.

### Task 16: net
**Files:** `net/net_connect.h/.c`, `net/transport_uart.h/.c`, `net/cui_transport.h`, `net/modem.h`, `net/saturn_uart16550.h`. Commit: `"Apply doc convention to net modules"`.

- [ ] Each task: (1) apply recipe to every function + file-box; (2) dead-code check; (3) commit with the message above.
- [ ] **Phase 5 compile checkpoint** — user runs `saturn/compile.bat` + host tests (these tasks touch host-tested modules, so `test_display`, `test_menu_layout`, `test_console`, `test_keyboard`, `test_term` are the guardrail — they must stay green).

---

## Phase 6 — Python tooling (verify via Python tests + regen compile)

Apply the **Python** box convention to every `tools/*.py` script: file-box, one box per `def`, strip inline `#` comments (argparse help/usage strings stay), dead-code check. For the three generators, also update the emitted C template so the generated `.c` carries C boxes, then re-run and commit the regenerated file.

### Task 17: non-generator scripts
**Files:** `tools/make_tga.py`, `tools/typeahead/filter_win.py`, `tools/tests/test_make_tga.py`.
- [ ] Apply Python box recipe to each `def` + file-box; strip inline comments; dead-code check.
- [ ] Run `python -m pytest tools/tests/test_make_tga.py` (or the project's invocation) — expect PASS unchanged.
- [ ] Commit: `git commit -m "Apply doc convention to non-generator Python tools"`.

### Task 18: generators + their C templates
**Files:** `tools/typeahead/gen_solution.py` (→`typeahead_solution.c`), `tools/typeahead/gen_typeahead.py` (→`typeahead_zork.c`), `tools/gametitles/gen_titles.py` (→`game_titles.c`).
- [ ] **Step 1:** Apply the Python box recipe to each generator script (file-box + per-`def` boxes + strip).
- [ ] **Step 2:** In each generator, edit the emitted-C string so the output file starts with a C file-box and each emitted function gets a C function-box (the generator writes the box literally; the *data tables* it emits stay unboxed).
- [ ] **Step 3:** Re-run each generator with the `--out` path from its usage string (`../../saturn/src/…`), regenerating `typeahead_solution.c`, `typeahead_zork.c`, `game_titles.c`.
- [ ] **Step 4:** Diff the regenerated files — only the new box comments should differ from the prior committed output (plus the intended data). Confirm no table data changed.
- [ ] **Step 5:** Commit script + regenerated output together: `git commit -m "Emit doc-convention boxes from Python generators; regenerate C tables"`.
- [ ] **Phase 6 compile checkpoint** — user runs `saturn/compile.bat` (regenerated `.c` files must still compile) + host tests.

---

## Self-Review notes (for the executor)
- If any compile checkpoint fails on an **undeclared identifier**, the fix is always: add the missing declaration to the owning module's `.h` (or a temporary forward declaration per SMEP step 4) — never move logic to "make it compile."
- If a **dead-code candidate** appears, stop and confirm with the user before deleting; a symbol may be reached via `extern "C"`, a callback, or an SRL/libretro entry point.
- The `.h` *what* vs `.cxx` *how* split is the most common slip: a `.h` box that describes the mechanism, or a `.cxx` box that only restates the contract, is wrong — re-derive from the Recipe.
