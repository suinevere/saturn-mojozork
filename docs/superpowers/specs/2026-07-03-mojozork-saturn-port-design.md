# MojoZork → Sega Saturn Port — Design Spec

**Date:** 2026-07-03
**Status:** Approved (design), pending spec review
**Target engine:** SaturnRingLib (SRL)

## Goal

Port [MojoZork](https://github.com/icculus/mojozork) — a single-file C
implementation of Infocom's Z-Machine (v3) — to run on the Sega Saturn as a
**fully playable Zork I**: story loaded from CD, scrolling text output on
screen, and interactive input via an on-screen keyboard driven by the gamepad.

## Engine choice: SaturnRingLib (not Jo Engine)

Both engines have a `Projects/mojozork` scaffold set up (copied from their
respective gamepad samples). We target **SRL** because it is a materially
better fit for a text adventure:

- **SRL's `main()` owns an explicit `while(1){ … SRL::Core::Synchronize(); }`
  loop.** There is no callback scheduler, so mojozork's synchronous
  "run-until-input-then-block" structure maps directly. When the `read` opcode
  needs a line, we run a nested keyboard loop that calls `Synchronize()` each
  frame. Blocking is idiomatic and never freezes a frame. (Jo Engine's
  `jo_core_run()` callback model would force refactoring the `read` opcode into
  an event-driven yield state machine.)
- `SRL::Debug::Print(col, row, fmt, …)` gives full printf-style text output
  (including `%s`), matching mojozork's heavy formatted-text usage.
- `SRL::Input::Digital` exposes `WasPressed()` edge-detection — ideal for
  moving an on-screen keyboard cursor.
- `SRL::Cd::File` maps directly onto story loading.
- The SRL makefile globs both `*.c` and `*.cxx` in `src/`, so `mojozork.c`
  compiles as C and links to a C++ `main.cxx` via `extern "C"`.
- `SRL::Memory::HighWorkRam` (1 MB) comfortably holds the 92 KB story.

## Architecture

A thin SRL C++ frontend wraps the (nearly) unmodified Z-machine core:

| File | Language | Responsibility |
|------|----------|----------------|
| `src/mojozork.c` | C | Z-machine core (opcodes, text decode, parser). Three tiny edits only. |
| `src/main.cxx` | C++ | Saturn frontend: boot, load story, wire hooks, run interpreter loop. |
| `src/saturn_glue.h` | C/C++ | `extern "C"` declarations of the hooks `main.cxx` implements. |
| `cd/data/ZORK1.DAT` | data | The 92 KB story file, read from CD. |

### The three edits to `mojozork.c`

The core already abstracts output and errors behind function pointers
(`GState->writestr`, `GState->die`) and guards its stdio `main`/`die`/
`writestr_stdio` behind `#if !defined(MULTIZORK) && !defined(MOJOZORK_LIBRETRO)`
(line ~2094) — the same mechanism the libretro frontend uses. We follow that
pattern:

1. Add `MOJOZORK_SATURN` to the `#if !defined(...)` guard so the stdio
   `main`/`die`/`writestr_stdio` are excluded from the Saturn build.
2. Add a `readline` function-pointer field to `ZMachineState` (mirroring the
   existing `writestr`/`die` pointers).
3. In `opcode_read`, replace the `fgets((char*)input, inputlen, stdin)` branch
   (line ~1304) with `GState->readline((char*)input, inputlen)`.

No opcode, text-decode, or parser logic changes. The host build of mojozork
continues to compile and validate the interpreter unchanged.

## Components

Each is independently understandable and testable.

### Text console (`console_write` / `console_render`)
A static ring buffer of word-wrapped lines (~40 columns, ~200 lines).
- `console_write(const char* s, len)` — fed by the `writestr` hook. Performs
  word-wrap at column width and on `\n`, pushing completed lines into the ring.
- `console_render()` — draws the visible tail (top ~22 rows) via
  `SRL::Debug::Print`. Scrollback (Up/Down) is a nice-to-have; MVP shows the
  tail.

### On-screen keyboard (`keyboard_run`) — this *is* `saturn_readline`
A character grid (a–z, space, digits, minimal punctuation) plus Backspace and
Submit, in the bottom ~6 rows of the screen.
- Per frame: render console tail (top) + current input line + keyboard grid
  with cursor highlighted; read pad (`WasPressed` for one-step cursor moves);
  A types the highlighted char, B backspaces, Start submits; `Synchronize()`.
- Returns the typed string with a trailing `\n` (so `opcode_read`'s existing
  newline handling works unchanged).

### Glue hooks (`extern "C"`, implemented in `main.cxx`)
- `saturn_writestr(const char* s, uintptr n)` → `console_write`.
- `saturn_readline(char* buf, int maxlen)` → `keyboard_run`.
- `saturn_die(const char* fmt, …)` → render message + halt (infinite
  `Synchronize()` loop).

## Data flow & control flow

```
CD ZORK1.DAT
  └─ SRL::Cd::File → buffer in SRL::Memory::HighWorkRam
       └─ initStory(buffer, len)
            └─ while (!GState->quit) runInstruction();
                 ├─ writestr hook → console_write → (rendered on next read)
                 └─ read opcode → saturn_readline → keyboard_run
                        └─ per-frame: console_render + keyboard render + pad + Synchronize()
```

The **only** place `SRL::Core::Synchronize()` is called is inside
`keyboard_run`'s nested loop. Output accumulates into the console during
instruction bursts (which are fast); it renders when the next `read` begins.
The initial intro text (before the first `read`) renders when the first
keyboard prompt appears. Game-over text (no further `read`) is handled by a
final render + `Synchronize()` loop in the quit path.

Screen layout (~40×28 cells): top ~22 rows console, bottom ~6 rows keyboard.

## Memory

- Story buffer allocated by the frontend via `SRL::Memory::HighWorkRam::Malloc`
  and passed into `initStory`, **bypassing `loadStory`'s `malloc`**. The C core
  therefore needs no heap (`GState` is `static`; the console ring is `static`,
  ~8 KB). Comfortable within 1 MB high work RAM.

## Deferred / stubbed (per approved MVP scope)

- **Save/restore**: the `opcode_save`/`opcode_restore` paths are stubbed to
  report failure; the Z-machine branches gracefully ("saves not supported yet").
  Saturn backup RAM is a future enhancement.
- **RNG seed**: replace `random_seed = time(NULL)` with a read from an SRL
  timer / SMPC clock.
- **Status bar**: optional row-0 render of room/score; deferred if fiddly.

## Build & test

- `makefile`: set `CD_NAME = mojozork`; `SOURCES` already globs `src/*.c` and
  `src/*.cxx`. Place `mojozork.c`, `main.cxx`, and `saturn_glue.h` in `src/`;
  `ZORK1.DAT` in `cd/data/`. Build via `compile.bat`; run in Mednafen via
  `run_with_mednafen.bat`.
- **Testing strategy** (no on-target unit tests):
  1. The host build still exercises the untouched interpreter logic.
  2. An optional **canned-script** debug toggle auto-drives input (reusing
     mojozork's existing script mechanism) to smoke-test rendering in the
     emulator without typing.
  3. Manual keyboard playthrough of the intro in Mednafen.

## Risks & mitigations

- **C/C++ linkage** (name mangling) — mitigated by the `extern "C"` glue header.
- **Saturn newlib libc coverage** — the core only needs `snprintf`,
  `vsnprintf`, `memmove`, `memcpy`, `strlen` (all present). `vfprintf`/`fopen`
  calls live only in the excluded stdio `main`/`die`.
- **Keyboard UX in ~40×28 cells** — addressed by the split-screen layout.
- **Frame pacing** — a long instruction burst without `Synchronize()` is
  acceptable because Zork turns are short; revisit only if a burst is visibly
  long.

## Out of scope (this milestone)

Z-Machine v4/v5/v6 games, sound, save/restore to backup RAM, multiplayer
(multizork), and any graphics beyond the text console.
