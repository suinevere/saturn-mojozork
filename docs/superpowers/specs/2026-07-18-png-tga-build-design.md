# PNG → TGA Asset Conversion in the Build

**Date:** 2026-07-18
**Status:** Approved for planning

## Problem

The Display Options feature scans `saturn/cd/data/TGA/` at runtime and offers every
`*.TGA` it finds as a selectable background. Seven source PNGs are staged in
`saturn/cd/data/PNG/`, but only one — `HOUSE.TGA` — has been converted, by hand,
by running `tools/make_house_tga.py` with explicit arguments.

That script converts exactly one file per invocation, hard-exits on any image that
is not 320x224, and defaults to a source path (`saturn/cd/data/house.png`) that no
longer exists. Adding a background is therefore a manual step that is easy to
forget and easy to get wrong.

Separately, `xorrisofs` includes `cd/data/` recursively, so the ~10 MB of source
PNGs (9.4 MB of which is a single oversized file) are currently burned onto the
disc as dead weight.

## Goals

Converting backgrounds becomes part of `compile.bat`, requiring no manual step and
no pre-existing Python environment beyond an interpreter on `PATH`. A contributor
who follows README.md from a clean clone gets working backgrounds. A contributor
with no Python at all still gets a working build.

## Non-Goals

- Changing the TGA format, the quantizer, or the VDP2 constraints. These are
  settled; see "Format constraints" below.
- Cleaning generated TGAs on `make clean`. They are committed artifacts.
- Patching the SaturnRingLib submodule. The `pre.makefile` hook makes this
  unnecessary.

## Format constraints (carried forward, do not regress)

Two constraints make a hand-written TGA necessary instead of an image-editor export.
Both are documented in `tools/make_house_tga.py`'s docstring and must survive into
the new script verbatim.

1. **8bpp paletted, never truecolor.** SRL's `VRAM::AutoAllocateBmp` doubles the
   VDP2 bitmap container size for RGB555, so a 512x256 container becomes 256 KB and
   spans the A0/A1 VRAM bank boundary. Bank-spanning bitmaps render as static:
   `slBitMapNbg0` never reserves the second bank in `VDP2_RAMCTL`, and SRL's
   allocator tracks banks only in software. At 8bpp the container is exactly 128 KB
   and fits one bank.
2. **Palette index 0 must be unused.** VDP2 treats index 0 on a scroll screen as
   transparent, punching back-color holes through the image. Quantize to 255 colors
   and shift every index up by one.

PIL re-optimizes the palette on save, which silently undoes constraint 2. The file
is written byte-by-byte with `struct`.

## Design

### 1. Asset relocation

`saturn/cd/data/PNG/*` moves to `tools/assets/png/` via `git mv`. Source art lives
outside the disc tree and can no longer reach the ISO.

Nothing at runtime references the `PNG/` directory — `display_scan_images`
(`saturn/src/main.cxx:2107-2191`) scans only `TGA/` — so this is inert with respect
to the Saturn binary.

Output remains `saturn/cd/data/TGA/`.

### 2. `tools/make_tga.py`

Replaces `tools/make_house_tga.py`. Retains its quantize → shift → hand-pack
pipeline and its docstring unchanged. New behavior:

| Concern | Behavior |
|---|---|
| Input | A directory pair (batch) or a single `src.png dst.tga` pair, as today |
| Discovery | Case-insensitive `*.png` glob — `CMPLAB.png` and `TYPEWRTR.png` need no rename |
| Output naming | Uppercased stem + `.TGA` |
| Long names | Stems over 8 characters are skipped with a warning (ISO9660 8.3; the build passes `--norock`) |
| Incremental | A PNG is skipped when its TGA exists and is newer |
| Off-size input | Warns and skips instead of `sys.exit`, so one bad file cannot block the rest |
| Exit code | 0 when the only problems were reported skips; nonzero only on a genuine crash |

Every skip prints the filename and the reason. Silence means everything converted.

### 3. `tools/convert-backgrounds.sh`

POSIX sh — every makefile recipe runs under MSYS2 `sh` (`SaturnRingLib/Compiler/msys2/usr/bin`),
not cmd or PowerShell.

Sequence:

1. Probe for an interpreter: `py -3`, then `python3`, then `python`. Require >= 3.9.
2. Create `tools/.venv` if absent.
3. Check whether Pillow imports; install from `tools/requirements.txt` only if it
   does not. Steady-state runs perform no network access.
4. Invoke `tools/make_tga.py tools/assets/png saturn/cd/data/TGA`.

Venv interpreter path differs by platform: `Scripts/python.exe` on Windows,
`bin/python` elsewhere. Probe both.

**Failure is never fatal.** No interpreter, no network, a pip error, a corrupt venv —
each prints an actionable message naming the fix ("install Python 3.9+ from
python.org, then rebuild") and exits 0. The build continues against whatever TGAs
are already committed.

### 4. `tools/requirements.txt`

Pillow, version-pinned to a compatible range. This is the entire dependency set.

### 5. `saturn/pre.makefile`

`shared.mk:215-229` includes `./pre.makefile` when it exists and otherwise defines a
no-op `pre_build`. Defining the file gives us the hook with no submodule edit:

```make
pre_build:
	$(info ****** Converting PNG backgrounds to TGA ******)
	sh ../tools/convert-backgrounds.sh
```

Recipes execute with `saturn/` as the working directory, hence `../tools/`.

Ordering (`build : pre_build build_bin_cue post_build`) is guaranteed only by
prerequisite order and is not parallel-safe under `-j`. `compile.bat` invokes make
serially, so this holds in practice.

### 6. Housekeeping

- `.gitignore` gains `tools/.venv/`, `__pycache__/`, `*.pyc`. The repo currently has
  no Python section at all.
- **Generated TGAs stay committed.** This is what makes the warn-and-continue path
  meaningful: a contributor without Python still gets every background.
- README.md gains Python 3.9+ as an *optional* prerequisite and an "adding a
  background image" section mirroring the existing "adding a game" section.

## Verification

1. Run the converter against the real folder. Expect six new TGAs — `ANCIENT` is
   skipped as off-size, and `HOUSE.TGA` is already newer than `HOUSE.PNG` so
   incremental mode leaves it alone. Confirm each output has an 18-byte header, a
   768-byte colormap, 71680 pixels of body, and no pixel with index 0.
2. Regenerate `HOUSE.TGA` explicitly (delete it first, so incremental mode does not
   skip it) and confirm it matches the committed one structurally.
3. Run again with everything present — confirm every file reports as up to date and
   nothing is rewritten.
4. Confirm `ANCIENT.PNG` (3105x4658) warns, skips, and does not abort the run.
5. Delete `tools/.venv`, remove Python from `PATH`, and confirm the build still
   completes with a visible warning.
6. Build and confirm the new backgrounds appear in the Display Options selector on
   hardware or in Mednafen.
