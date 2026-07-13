# main.cxx Modularization + Codebase Documentation Convention — Design

**Date:** 2026-07-19
**Status:** Approved (design), pending spec review
**Scope:** Saturn client sources (`saturn/src/`) and the project build/asset
scripts (`tools/*.py`, excluding the `.venv/`). No behavior changes.

## Goal

Two interwoven efforts across `saturn/src/`:

1. **Modularize `main.cxx`** — turn the 3,595-line god file into a thin
   orchestrator by relocating its self-contained clusters into topical `.cxx`/`.h`
   modules that follow the pattern already set by `display.c`, `menu_layout.c`,
   etc. End state: `main.cxx` holds `main()`, the core game loop, and soft-reset
   wiring — nothing else.
2. **Apply a uniform documentation convention to the whole tree** — every source
   file (`saturn/src/*.{c,cxx,h}` and `tools/*.py`) gets a file-level box header
   and a box header on every function, all inline comments are removed (their
   essential rationale folded into the header), and dead code is removed. See
   **Code Conventions** below.

## Non-Goals

- **No test-seam split.** We are physically relocating code, not extracting pure
  host-testable cores behind new interfaces. SRL/Saturn calls stay inline in the
  moved functions. No new unit tests are written for relocated code.
- **No behavior change.** This is a mechanical refactor plus documentation. Every
  screen, menu, key, and save slot must behave identically before and after.
- **No logic refactor.** Naming, control-flow, and algorithm changes are out of
  scope. The only substantive edits are relocation, the comment convention, and
  removal of code proven dead. (Dead-code removal *is* in scope; see below.)
- **No build-system change.** The makefile already globs `src/*.c` + `src/*.cxx`,
  so new files are compiled automatically. `CMakeLists.txt` (host tests) only
  needs entries if we add tests — we are not.

## Architectural Review (current state)

The project is already ~90% module-driven. Clean, cohesive, and in several cases
host-unit-tested modules exist: `display.c` (color/palette model + save blob
encode/decode), `menu_layout.c` (pure box/geometry arithmetic), `music`, `sound`,
`typeahead`, `keyboard`, `game_titles`, `saturn_backup`, `console`, `term`.

The single anomaly is **`main.cxx`**. It accreted every responsibility the pure
modules do not cover:

- the menu-drawing framework (frames, boxes, selection, confirm dialogs)
- every menu page (options, sound, display, controls, config, TOC)
- title screen + background art + TGA bitmap loading + CD directory juggling
- CD game scanning and the game-select screen
- the online/network play mode
- input handling: controller mapping, pad repeat, scroll, command history
- the game loop: command submission, soft reset, typeahead scanning

Crucially, most of the file-scope globals in `main.cxx` are **cluster-local** —
declared immediately next to the functions that use them (scroll/pad/history state
by the input functions; CD dir tables by the title/catalog functions; the online
trie by the online mode). Only a small core is genuinely cross-cutting. That makes
a low-ceremony relocation viable: cluster-local state moves with its functions as
`static`; the cross-cutting core becomes a shared `extern` seam.

## Code Conventions (applies to every source file in scope)

Scope: `saturn/src/*.{c,cxx,h}` (hand-authored client code, including `net/`) and
the project scripts `tools/*.py`. Every file gets a file-level header box; every
function gets a function header box.

**Scope boundary.** Upstream/vendored interpreter sources outside `src/`
(`mojozork.c`, `multizorkd.c`, `mojozork-libretro.c`, `mojozork-sdl3.c`) and the
`tools/.venv/` third-party packages are out of scope.

### File-level header box

At the top of every file:

```
/*----------------------
 | File Name
 | Description: what this file/module provides
 | Author: suinevere
 | Dependencies: other files/modules this file relies on
 ----------------------*/
```

In `.py` scripts the same box is written with `#` delimiters (see Python
adaptation below). `Author: suinevere`; `Dependencies: N/A` when the file is
self-contained.

**Generated source files.** Several `saturn/src/*.c` files are emitted by Python
generators in `tools/`, so hand-editing the `.c` is futile — regeneration would
overwrite it. For these, **the generator's C template is the source of truth**:
the generator is updated to emit boxed headers (with `Author: suinevere`) for the
functions it writes and to follow the comment rules, so its output conforms on the
next regeneration. Generators and their targets:

| Generator | Emits |
|---|---|
| `tools/typeahead/gen_solution.py` | `saturn/src/typeahead_solution.c` |
| `tools/typeahead/gen_typeahead.py` | `saturn/src/typeahead_zork.c` |
| `tools/gametitles/gen_titles.py` | `saturn/src/game_titles.c` |

Only the functions the templates emit get boxes; the bulk data tables themselves
(trie/word arrays) are left as-is. After updating a generator, it is re-run and the
regenerated file is committed so the tree matches the template.

### Boxed comment header

Every function gets a header block immediately above it, in this exact format:

```
/*----------------------
 | Function Name
 | Description:
 | Author: suinevere
 | Dependencies: other-file deps this function requires
 | Globals: globals read or written
 | Params: inputs
 | Returns: output
 ----------------------*/
```

- The **box lives in both files**: above the declaration in the `.h` and above the
  definition in the `.c`/`.cxx`.
- **Description differs by file:** the `.h` box says **what** the function does
  (the caller's view); the `.c`/`.cxx` box says **how** it does it (the
  implementer's view). Keep descriptions concise — typically a sentence or two —
  but there is **no hard sentence cap**; expand when needed to preserve a
  non-obvious caveat.
- The metadata fields are the same in both places:
  - **Dependencies** — other source files this function calls into (e.g.
    `display.c`, `menu_layout.c`, SRL). `N/A` if it calls nothing outside its own
    translation unit and the standard library.
  - **Globals** — file-scope/`app_state` globals it reads or writes. `N/A` if none.
  - **Params** — each parameter, or `N/A` for `void`.
  - **Returns** — the return value's meaning, or `N/A` for `void`.
- **`Author: suinevere`** on every box.
- Any field with nothing to say is `N/A` (never left blank).
- `static` file-local functions get the box above the definition in the `.c`/`.cxx`;
  they have no `.h` declaration, so their box carries the **how** description.

### Comment rules

- **Remove all inline comments** (in-body and trailing). Their content is either
  obvious from the code (drop it) or a non-obvious *why* (fold it into the header
  box's Description).
- **Rationale-preservation rule:** where a comment records a caveat that cannot be
  recovered from the code — TV-overscan row handling, MenuBacking refcount cleanup
  across `longjmp`, save-blob sentinel/back-compat formats, buffer-cap reasons —
  the *why* is preserved in the box Description, at whatever length that takes.
- Section-banner comments that organize a file (e.g. `---- rendering ----`) may
  remain as file/section structure; they are not function-inline comments.

### Python adaptation (`tools/*.py`)

- The boxes are written with `#` line delimiters instead of `/* */`, same fields
  and same `Author: suinevere`:

  ```
  #----------------------
  # File Name
  # Description: ...
  # Author: suinevere
  # Dependencies: ...
  #----------------------
  ```

- A Python file has no header/implementation split, so each function gets **one**
  box (above the `def`) whose Description covers what it does; add a short *how*
  line when the implementation is non-obvious. Fields: `Dependencies` (imported
  modules / other scripts it shells out to), `Globals` (module-level state it
  reads/writes), `Params`, `Returns`. `N/A` where unused.
- The box goes above the `def`, not inside it (not a docstring), to match the C
  convention visually. Inline `#` comments are removed under the same rule; the
  argparse `--help`/usage strings are user-facing output, not comments, and stay.
- These scripts include the three C generators; updating a generator means
  updating both the script itself (Python boxes) **and** the C template it emits
  (C boxes), per **Generated source files** above.

### Dead code

- During each file's pass, functions and file-scope statics (or module-level
  Python defs) that are **defined but never referenced anywhere in the in-scope
  tree** are candidates for removal.
- Removal is **surfaced as a finding and confirmed before deletion** — never
  removed blind, because a symbol may be reached only via a callback, an
  `extern "C"` seam, asm, or the SRL/libretro entry points. The plan produces the
  candidate list; deletion happens only on confirmation.

## Design

### The shared-state seam: `app_state.h` / `app_state.cxx`

A new header declares the small cross-cutting core as `extern`; `app_state.cxx`
provides the single definition. This is the minimal-churn choice — these are
already file-scope globals, so making them `extern` touches only their definition
site, not their ~200 read/write sites throughout the moved code. No struct
threading, no accessor functions.

Cross-cutting globals moved to `app_state`:

- **Options:** `g_difficulty`, `g_mix_mode`, `g_sel_track`, `g_display`,
  `g_dialnum`, `g_sound`
- **Save/restore session state:** `g_restore_device`, `g_restore_slot`,
  `g_save_device`, `g_save_slot`, `g_last_device`, `g_last_slot`, `g_autocmd`,
  `g_story_filename`
- **Input handle:** `g_pad`
- **Soft reset:** `g_title_jmp`, `g_title_jmp_armed`

Related constants/enums those globals depend on (`DIFF_*`, `MIX_*`, `DisplayState`,
`DIALNUM_MAX`) are declared alongside or included from their existing home
(`display.h` already owns `DisplayState`).

### Module map

Each row is a new `.cxx` + `.h` carved out of `main.cxx`. Cluster-local state
listed in the last column moves into the module as `static`.

| Module | Functions relocated | Local state moved in |
|---|---|---|
| `app_state` + `options` | `options_load`, `options_save`, `display_apply`, `display_cycle_row`, `valid_dialnum` | the cross-cutting core (above) |
| `console_view` | `render_console`, `render_keyboard`, `install_block_glyph`, `console_height`, `console_scroll_to_output`, `text_set_color`, `hint`, `note_input_device` | `g_kbd_visible`, `g_caret_arrows`, `g_more_below`, `g_scroll`, screen-layout consts |
| `input` | `face_button`, `face_btn_name`, `slot_name`, `slot_raw`, `caps_combo_fired`, `chord_tick`, `chord_fired`, `pad_scroll_update`, `pad_repeat_update`, `pad_fired`, `scroll_handle_key`, `history_push/load/recall`, `face_assign`, `chord_assign`, `mapping_reset_defaults` | `g_chord_slot`, `g_chordrep`, `g_padrep`, `g_history`, `g_hist_*`, `FACE_DEFAULT`, `CHORD_DEFAULT`, pad/scroll consts |
| `menu` | `menu_clear`, `menu_window_rect`, `menu_frame`, `menu_wait`, `menu_message`, `menu_select`, `menu_confirm`, `menu_sync` | `g_menu_backing_depth`, `MENU_SELECT_HINT_*` |
| `menu_pages` | `config_page`, `configure_controls_page`, `controls_page`, `keyboard_controls_page`, `toc_dump_page`, `sound_options_page`, `display_options_page`, `options_menu` | page-local label tables (`FACE_LABEL`, `CHORD_LABEL`) |
| `save_ui` | `make_slot_name`, `choose_device`, `pick_slot_and_name` | — |
| `title` | `title_draw_art`, `tga_name_is_usable`, `cd_capture_root`, `cd_enter_root`, `cd_enter_tga`, `cd_restore_z3`, `bitmap_read_end`, `display_scan_images`, `tga_load_nbg0`, `title_bg_show`, `title_bg_hide`, `title_and_seed` | `g_image_name`, `g_image_ptr`, `g_root_dir*`, `g_tga_dir*` |
| `game_catalog` | `has_z3_ext`, `scan_z3_folder`, `read_game_info`, `label_cmp`, `label_year`, `preload_game_catalog`, `game_select` | `g_z3_dir*`, `names`, `cats`, `g_catalog_*`, `CAT_NAMES`, `MAX_GAMES` |
| `online` | `online_cancel_requested`, `online_wait_any`, `online_settle_input`, `ensure_online_typeahead`, `online_mode` | `g_online_ta`, `g_online_diff` |
| **stays in `main.cxx`** | `main()`, `submit_command`, `is_reboot_command`, `is_alnum_ch`, `is_quit_command`, `soft_reset_to_title`, `soft_reset_chord_held`, `check_soft_reset`, `ensure_typeahead`, `typeahead_scan_screen` | `g_typeahead_root`, `g_ta_story`, `g_ta_diff` |

The final `main.cxx` is `main()` + the game loop + soft-reset wiring: a thin
orchestrator that includes the new module headers and calls into them.

### Dependency direction

Modules depend downward on the substrate (`app_state`, `console_view`, `menu`,
`menu_layout`, `display`) and never upward on `main.cxx`. `menu_pages`, `save_ui`,
`title`, `game_catalog`, and `online` are leaves that call the substrate. Any
function currently defined in `main.cxx` and *called by* a relocated function
either moves with it or, if shared, lands in `app_state`/`console_view`/`menu`.
The plan phase resolves each such edge explicitly.

## Verification

Because relocated code is not host-testable and the assistant does not build
(the user compiles via `saturn/compile.bat`), correctness is confirmed by
**compiling after each batch** and requiring a clean link before proceeding.
Existing host tests (`test_menu_layout`, `test_display`, `test_console`,
`test_keyboard`, `test_term`) must still pass after every batch — the comment
convention and dead-code pass must not change the behavior they cover.

Each batch does both jobs for the files it touches: relocation (where
applicable) **and** the documentation convention (box headers, inline-comment
removal, dead-code candidates surfaced). Batches are ordered leaf-first so each
one links against already-moved substrate:

1. **Substrate:** `app_state`/`options`, `console_view`, `input`. → compile.
2. **Menus:** `menu`, `menu_pages`, `save_ui`. → compile.
3. **Boot/content:** `title`, `game_catalog`. → compile.
4. **Modes + trim:** `online`, then `main.cxx` reduced to loop + wiring. → compile.
5. **Pre-existing modules doc pass:** apply the convention + dead-code pass to the
   already-clean hand-authored files (`display`, `menu_layout`, `music`, `sound`,
   `typeahead`, `typeahead_extract`, `keyboard`, `saturn_keyboard`,
   `saturn_backup`, `saturn_compat`, `console`, `term`, `music_cdda`,
   `music_data`, `sound_blorb`, `mojozork_saturn`, and the `net/` sources). No
   relocation — headers and comments only. This batch may subdivide by file group
   for compile checkpoints. → compile.
6. **Python tooling:** apply the convention (file box, per-`def` boxes, inline-
   comment removal, dead-code pass) to every `tools/*.py` script —
   `gen_solution.py`, `gen_typeahead.py`, `gen_titles.py`, `make_tga.py`,
   `filter_win.py`, `tests/test_make_tga.py`. For the three generators, also update
   the C template each emits to carry the C boxes, then re-run and commit the
   regenerated `typeahead_solution.c` / `typeahead_zork.c` / `game_titles.c`.
   Verified by running the Python tests (`test_make_tga.py`) and, for generators,
   confirming the regenerated `.c` still compiles in the Saturn build.

A batch is complete only when `compile.bat` links clean, host tests pass, and the
game boots to the title screen with menus, save/load, options, and online mode all
reachable. The dead-code candidate list produced across batches is confirmed with
the user before any deletion (see Code Conventions › Dead code).

## Risks & Mitigations

- **Hidden coupling / call edges from main-resident code into moved clusters.**
  Mitigation: leaf-first batching; resolve each cross-edge in the plan by moving
  the callee or promoting it to the substrate.
- **`static` linkage collisions** when two clusters had identically named local
  helpers. Mitigation: they were all `static` in one TU before; in separate TUs
  `static` keeps them file-local, so no collision — verified at compile.
- **C vs C++ linkage** across the `extern "C"` boundary (`app_state.h` is included
  by both `.c` and `.cxx` TUs). Mitigation: wrap C-visible declarations in the
  `#ifdef __cplusplus extern "C"` guard, matching `display.h`.
- **Include-order / forward-declaration churn.** Mitigation: each module header is
  self-contained (includes what it needs); `main.cxx` includes all module headers.

## Out of Scope / Future

- Extracting pure host-testable cores from the relocated SRL code (the deferred
  "full test-seam split").
- Consolidating the three near-identical CD-directory scan blocks (`root`/`tga`/
  `z3`) into one helper — tempting during the `title`/`game_catalog` moves, but
  deferred to keep this refactor mechanical.
