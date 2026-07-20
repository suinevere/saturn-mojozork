# main.cxx Modularization — Design

**Date:** 2026-07-19
**Status:** Approved (design), pending spec review
**Scope:** Saturn client only (`saturn/src/`). No behavior changes.

## Goal

Turn `saturn/src/main.cxx` from a 3,595-line god file into a thin orchestrator by
relocating its self-contained clusters into topical `.cxx`/`.h` modules that follow
the pattern already established by `display.c`, `menu_layout.c`, `music`, `sound`,
etc. The end state: `main.cxx` holds `main()`, the core game loop, and soft-reset
wiring — nothing else.

## Non-Goals

- **No test-seam split.** We are physically relocating code, not extracting pure
  host-testable cores behind new interfaces. SRL/Saturn calls stay inline in the
  moved functions. No new unit tests are written for relocated code.
- **No behavior change.** This is a mechanical refactor. Every screen, menu, key,
  and save slot must behave identically before and after.
- **No unrelated cleanup.** Naming, dead code, and micro-refactors are out of
  scope except where a move mechanically requires it (e.g. a forward declaration).
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
`test_keyboard`, `test_term`) must still pass, since the pure modules they cover
are untouched.

Batches, ordered leaf-first so each one links against already-moved substrate:

1. **Substrate:** `app_state`/`options`, `console_view`, `input`. → compile.
2. **Menus:** `menu`, `menu_pages`, `save_ui`. → compile.
3. **Boot/content:** `title`, `game_catalog`. → compile.
4. **Modes + trim:** `online`, then `main.cxx` reduced to loop + wiring. → compile.

A batch is complete only when `compile.bat` links clean and the game boots to the
title screen with menus, save/load, options, and online mode all reachable.

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
