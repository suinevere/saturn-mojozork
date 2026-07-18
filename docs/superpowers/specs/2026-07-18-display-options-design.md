# Display Options — design

Date: 2026-07-18
Status: approved, ready for planning

## Goal

Let the player choose the console's background and text color from a new
**Display Options** page, seeded by a list of vintage microcomputer palettes.
The background selector additionally cycles into any bitmap found in the disc's
`TGA` folder, so the game can run over an image instead of a flat color.

## Motivation

Z-ATURN renders every character through `SRL::Debug::Print` onto VDP2 NBG3 in a
single hardcoded color over a hardcoded black back screen. Both are one-line
changes at the hardware level but neither is reachable by the player. The
microcomputer palettes give the choice a theme rather than presenting a bare
color wheel.

## Rendering background

Text lives on NBG3 at `Priority::Layer7` (top). Cells with no glyph are
transparent, so whatever sits below shows through: either the VDP2 back screen
(a flat color) or NBG0 (a bitmap at `Priority::Layer1`).

Two APIs drive everything, both of which take effect immediately — no reload,
no re-render pass:

- **Text color:** `SRL::ASCII::SetColor(rgb555, 15)`. Index 15 is the color
  index the SGL font glyphs use, and the same index `install_block_glyph()`
  (`saturn/src/main.cxx:547`) fills when it carves the block cursor into the
  DEL slot. Setting it therefore recolors body text, menus, the on-screen
  keyboard, and the blinking cursor in one call.
- **Background color:** `SRL::VDP2::SetBackColor(rgb555)`, currently called once
  via `SRL::Core::Initialize(HighColor::Colors::Black)` at `main.cxx:2457`.

`SRL::Debug::PrintColorSet()` is **not** a usable lever here. It calls
`slCurColor()`, but `SRL::Debug::Print` routes through `SRL::ASCII::Print`,
which reads `ASCII::colorBank` instead. The call is inert in this codebase.

Because both setters are instant, the options page previews changes live behind
itself as the player scrolls, and Cancel restores a snapshot.

## State

Three values, all persisted:

| Variable | Range | Meaning |
| --- | --- | --- |
| `g_palette` | `0..14`, or `PALETTE_CUSTOM` | selected microcomputer preset |
| `g_bg_index` | `0..BG_N-1` | background: color, then image |
| `g_text_index` | `0..TEXT_N-1` | text color |

`g_palette` is stored independently rather than reverse-derived from the color
pair, because two preset pairs collide:

- Commodore 64 and Atari 800 are both Blue / Light Blue.
- IBM PC (MDA) and Commodore PET are both Black / Bright Green.

A reverse lookup could not tell them apart, so a reloaded save would show the
wrong machine name. Storing the index makes the name unambiguous, and reduces
"Custom" to a single derived predicate: the current `(g_bg_index, g_text_index)`
no longer matches `PRESETS[g_palette]`.

## Palette table

Fifteen presets, in this display order. `bg` and `text` are indices into the
color tables below.

| # | Machine | Background | Text |
| --- | --- | --- | --- |
| 0 | Toshiba T3100 (Gas-Plasma) | Black | Bright Amber |
| 1 | Monochrome P3 (Inverse) | Glowing Amber | Black |
| 2 | Apple II Plus | Black | Green |
| 3 | Commodore 64 | Blue | Light Blue |
| 4 | ZX Spectrum | Light Gray | Black |
| 5 | VIC-20 | Light Gray | Cyan |
| 6 | TI-99/4A | Bright Cyan | Black |
| 7 | Amstrad CPC 464 | Blue | Bright Yellow |
| 8 | BBC Micro | Black | White |
| 9 | MSX Standard | Blue | White |
| 10 | TRS-80 Color Computer | Green | Black |
| 11 | Atari 800 (BASIC) | Blue | Light Blue |
| 12 | IBM PC (MDA Monitor) | Black | Bright Green |
| 13 | Commodore PET | Black | Bright Green |
| 14 | Apple Macintosh (Classic) | Bright White | Black |

Machine names must fit the selector field. The longest, "Toshiba T3100
(Gas-Plasma)" at 28 characters, exceeds the ~24 columns available at
`x + 16` on a 40-column screen. Names are shortened for display where needed;
the exact strings are an implementation detail, but the machine must stay
recognizable (e.g. "Toshiba T3100", "TRS-80 CoCo", "Mac Classic").

## Color tables

Derived from the distinct ANSI codes in the source table. Dark Blue, Deep Blue,
and Medium Blue all emit `\033[44m` and collapse to one entry; White and
White / Light Gray both emit `\033[47m` and likewise collapse. Each selector
offers only the colors appearing in its own column.

Values are 8-bit RGB, passed to `SRL::Types::HighColor(r, g, b)` and cast to
`uint16_t` for `ASCII::SetColor`.

**Background (7):**

| # | Name | ANSI | RGB |
| --- | --- | --- | --- |
| 0 | Black | `40` | `#000000` |
| 1 | Glowing Amber | `43` | `#FFB000` |
| 2 | Blue | `44` | `#0000AA` |
| 3 | Light Gray | `47` | `#AAAAAA` |
| 4 | Bright Cyan | `106` | `#55FFFF` |
| 5 | Green | `42` | `#00AA00` |
| 6 | Bright White | `107` | `#FFFFFF` |

**Text (8):**

| # | Name | ANSI | RGB |
| --- | --- | --- | --- |
| 0 | Bright Amber | `38;5;214` | `#FFAF00` |
| 1 | Black | `30` | `#000000` |
| 2 | Green | `32` | `#00AA00` |
| 3 | Light Blue | `94` | `#5555FF` |
| 4 | Cyan | `36` | `#00AAAA` |
| 5 | Bright Yellow | `93` | `#FFFF55` |
| 6 | White | `37` | `#AAAAAA` |
| 7 | Bright Green | `92` | `#55FF55` |

`White` is `#AAAAAA`, faithful to ANSI 37 rather than true white; `Bright White`
(`#FFFFFF`) is available as a background. This keeps the BBC Micro and MSX
presets looking like the machines they name.

## Background images

Background indices `>= BG_COLOR_N` address bitmaps rather than colors.

**Discovery.** At boot, `SRL::Cd::ChangeDir("TGA")` returns the file count and
populates `SRL::Cd::GfsDirectoryNames[]`. The names are cached into a fixed
table (cap: 8 images) and the previous working directory is restored
immediately, since the app relies on being in `Z3` for story-file access. The
list is read once; the disc cannot change under a running Saturn.

**Selection.** Choosing an image calls the existing `title_bg_show()` machinery
(`main.cxx:1989`): load the TGA onto NBG0, set `Priority::Layer1`, enable the
scroll. Choosing a color instead calls `title_bg_hide()` so the back screen
shows through. The teardown at `main.cxx:2504` — which currently disables NBG0
unconditionally after the title — becomes conditional on the saved setting.

**Validation.** Every bitmap must be 8bpp paletted and fit within one VRAM
bank, or SRL's allocator renders it as static (see
`docs/superpowers/specs/2026-07-17-title-screen-background-image-design.md`;
`tools/make_house_tga.py` is the conversion path). The loader checks the TGA
header before committing: on a format it cannot render, it falls back to the
color background and leaves the selection unchanged, rather than displaying
static. A file that fails validation is skipped when the list is built, so an
unrenderable image is never reachable from the selector.

Selecting an image sets the palette name to `Custom`, since no preset specifies
an image.

## Menu

New `display_options_page()`, modeled on `sound_options_page()`
(`main.cxx:1519`): same OK/Cancel loop, same snapshot-and-revert, same
`<` / `>` selector rows, same pad-and-keyboard input handling.

```
DISPLAY OPTIONS

> System Palette   < Commodore 64 >
  Background       < Blue >
  Text             < Light Blue >

  OK
  Cancel

<> change  A/Start=OK  B=Cancel
```

Behavior:

- **System Palette** cycles the 15 presets and snaps both sliders to that
  machine's combo. When the current combo matches no preset it displays
  `Custom`; cycling from `Custom` moves to preset 0.
- **Background** and **Text** cycle their tables. Any change that breaks the
  match with `PRESETS[g_palette]` flips the displayed name to `Custom`. A
  change that happens to restore a match to the stored palette restores that
  name.
- **Left/Right applies immediately** so the result is visible behind the menu.
- **Cancel** restores the snapshot of all three values and re-applies them.
- **OK** commits and calls `options_save()`.

Rows do not appear or disappear conditionally — unlike Sound Options, Display
Options has no hardware dependency and always shows all five rows.

**Legibility guard.** Both tables contain Black and both contain Green, so free
cycling could land on background == text and blank the screen, including the
menu itself. When cycling either selector, an entry whose color equals the
other selector's current color is skipped. Presets never collide this way, so
the guard only constrains manual cycling. When the background is an image the
guard is inactive: there is no single background color to compare against, and
every text color stays reachable.

**Entry point.** A new `OI_DISPLAY` item in `options_menu()` (`main.cxx:1647`),
placed between Controls and Sound.

No direct function-key hotkey in this pass. F9 is already bound to `restore`
(`main.cxx:1024`, alongside F3 and F6), and the only unbound keys — F1, F4, F7,
F8 — are deliberately unreported by `saturn_keyboard.cxx`, which maps raw Saturn
scancodes that are not derivable from the existing table (F9 is `0x01`, F10
`0x09`, F11 `0x78`, F12 `0x07`). Adding one means confirming its scancode on
hardware first. Deferred as a follow-up.

## Title screen

The title screen keeps HOUSE.TGA and forces text to white, independent of the
saved setting. The player's colors are applied when the title exits — the same
point where `title_bg_hide()` is called today. This keeps the title art looking
as designed regardless of the configured palette.

## Persistence

The setting joins the existing `MOJOOPTS` backup-RAM blob
(`options_load` / `options_save`, `main.cxx:241` and `main.cxx:275`), following
that file's established convention: a sentinel byte, then the payload, appended
after the previous block.

Layout appended after the sound block's `[sentinel=1][mix][track]`:

```
[sentinel=1][palette][bg_index][text_index]
```

`palette` always stores a real preset index, `0..14` — never a `Custom`
sentinel. `Custom` is a *derived* display state (the stored `bg` / `text` no
longer match `PRESETS[palette]`), and since `bg` and `text` are saved
alongside, it reconstructs itself on load. Writing a sentinel instead would
discard the machine identity and reintroduce exactly the collision ambiguity
this field exists to prevent. On load, a `palette` byte outside `0..14` is
treated as absent and falls back to the default.

Loading tolerates absence: a blob written before this feature has no such block,
and the defaults stand. Each field is range-checked on load, exactly as the
existing blocks check theirs; an out-of-range value falls back to the default
rather than being trusted. If `bg_index` names an image slot that no longer
exists on the current disc, it falls back to the default color background.

**Defaults** (also the values used when the block is absent): palette 12,
IBM PC (MDA Monitor) — Black background, Bright Green text. This is closest to
the current hardcoded appearance while making the new feature visible.

Budget: the blob is 64 bytes with `n < 62` guards already in place. Four more
bytes fit.

## Testing

Host-side tests cover the logic that does not touch VDP2:

- Preset table is well-formed: 15 entries, every `bg` / `text` index in range.
- The two known collisions (C64 / Atari 800, IBM PC / PET) both round-trip to
  the correct machine name through save and load — the regression the stored
  palette index exists to prevent.
- Applying a preset then changing one slider yields `Custom`; restoring the
  slider restores the preset name.
- The legibility guard never yields background == text, from any starting
  position, cycling in either direction.
- `options_save` / `options_load` round-trip all three values, and a blob
  written without the display block loads at the defaults.
- Out-of-range bytes in the display block fall back to defaults.

On-hardware checks, which cannot be automated:

- Text, menus, on-screen keyboard, and block cursor all recolor together.
- Live preview updates behind the open menu; Cancel reverts it.
- An image background survives the transition out of the title screen.
- A deliberately malformed TGA is skipped rather than rendered as static.

## Out of scope

- Dimming or tinting an image background to improve text contrast over
  photographic art. Game text over HOUSE.TGA is expected to be hard to read;
  whether that needs addressing is a judgment to make after seeing it on
  hardware, not before.
- Per-element colors (distinct colors for prompt, status hints, keyboard). The
  on-screen keyboard matches body text, as decided during design.
- Changing the title screen's own appearance.
- Custom RGB entry beyond the fixed tables.
