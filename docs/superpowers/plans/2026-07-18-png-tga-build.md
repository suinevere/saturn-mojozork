# PNG → TGA Asset Conversion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert every staged PNG background into a disc-ready 8bpp paletted TGA automatically during `compile.bat`, bootstrapping a Python venv on demand and degrading to a warning when Python is absent.

**Architecture:** A batch converter (`tools/make_tga.py`) replaces the single-file `tools/make_house_tga.py`. A POSIX sh wrapper (`tools/convert-backgrounds.sh`) finds an interpreter, provisions `tools/.venv`, and runs the converter. `saturn/pre.makefile` hooks that wrapper into the SDK's existing `pre_build` extension point, so the SaturnRingLib submodule is never edited. Source PNGs move out of `saturn/cd/data/` so they stop being burned onto the ISO.

**Tech Stack:** Python 3.9+, Pillow, POSIX sh (MSYS2 on Windows), GNU make.

## Global Constraints

- **Output must be 8bpp paletted TGA, never truecolor.** RGB555 doubles SRL's VDP2 bitmap container to 256 KB, spanning the A0/A1 VRAM bank boundary, which renders as static. 8bpp yields exactly 128 KB and fits one bank.
- **Palette index 0 must never appear in pixel data.** VDP2 renders index 0 on a scroll screen as transparent. Quantize to 255 colors and shift every index up by one.
- **Never save the TGA through PIL.** PIL re-optimizes the palette on save, silently undoing the index-0 reservation. Pack the bytes with `struct`.
- **Source dimensions must be exactly 320x224.** Anything else is skipped with a warning, never resized and never fatal.
- **Generated TGA filenames:** uppercased PNG stem + `.TGA`. Stems longer than 8 characters are skipped — the build passes `--norock` to `xorrisofs`, so ISO9660 8.3 applies.
- **Every recipe runs under MSYS2 `sh`,** not cmd or PowerShell. Shell code must be POSIX sh.
- **Missing Python, missing Pillow, no network, or a broken venv must print an actionable warning and exit 0.** Only a genuine converter crash may exit nonzero.
- **Generated TGAs stay committed to git.** They are the fallback for contributors without Python.
- **The selector holds at most 8 images** (`DISP_IMAGE_MAX` in `saturn/src/display.h`). This plan produces 7 (`HOUSE` plus six new); a resized `ANCIENT` would make exactly 8. A ninth background needs that constant raised first.

---

### Task 1: Batch converter with tests

**Files:**
- Create: `tools/make_tga.py`
- Create: `tools/tests/test_make_tga.py`
- Delete: `tools/make_house_tga.py`

**Interfaces:**
- Consumes: nothing (first task).
- Produces:
  - `encode_tga(im: PIL.Image.Image) -> bytes` — packs a 320x224 RGB image into a complete TGA file image.
  - `convert_one(src: pathlib.Path, dst: pathlib.Path) -> tuple[str, str]` — returns `(status, message)` where status is one of `"wrote"`, `"skip"`.
  - `batch(srcdir: pathlib.Path, dstdir: pathlib.Path) -> int` — converts a directory, prints per-file lines, returns the number of files written.
  - Module is importable without side effects; CLI lives under `if __name__ == "__main__":`.

Pillow is needed to run these tests. If `tools/.venv` does not exist yet, create it by hand for this task only — Task 3 automates it:
`python -m venv tools/.venv && tools/.venv/Scripts/python.exe -m pip install Pillow`
(use `tools/.venv/bin/python` on Linux/macOS). Every command below is written as `PY=tools/.venv/Scripts/python.exe`; substitute the POSIX path if you are not on Windows.

- [ ] **Step 1: Write the failing test**

Create `tools/tests/test_make_tga.py`:

```python
#!/usr/bin/env python3
"""Host tests for tools/make_tga.py. Run: python tools/tests/test_make_tga.py"""
import os
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PIL import Image

import make_tga

FAILURES = []


def check(cond, label):
    print(("  ok   " if cond else "  FAIL ") + label)
    if not cond:
        FAILURES.append(label)


def gradient(w=make_tga.WIDTH, h=make_tga.HEIGHT):
    """A deterministic multi-hue gradient — quantizing a flat color is a weak test."""
    im = Image.new("RGB", (w, h))
    im.putdata([((x * 7) % 256, (y * 5) % 256, ((x + y) * 3) % 256)
                for y in range(h) for x in range(w)])
    return im


def make_png(path, w=make_tga.WIDTH, h=make_tga.HEIGHT):
    gradient(w, h).save(path)


def test_encode_tga_structure():
    print("test_encode_tga_structure")
    blob = make_tga.encode_tga(gradient())

    idlen, cmaptype, imgtype = blob[0], blob[1], blob[2]
    cmaplen = blob[5] | (blob[6] << 8)
    cmapdepth = blob[7]
    width = blob[12] | (blob[13] << 8)
    height = blob[14] | (blob[15] << 8)
    bpp, desc = blob[16], blob[17]

    check(idlen == 0, "no image ID field")
    check(cmaptype == 1, "colormap present")
    check(imgtype == 1, "uncompressed paletted")
    check(cmapdepth == 24, "24-bit colormap entries")
    check(width == 320 and height == 224, "320x224")
    check(bpp == 8, "8bpp indices")
    check(desc == 0x00, "bottom-left origin")
    check(cmaplen <= 256, "colormap fits 256 entries")

    body = blob[18 + cmaplen * 3:]
    check(len(body) == 320 * 224, "body is exactly one byte per pixel")
    check(len(blob) == 18 + cmaplen * 3 + 320 * 224, "no trailing bytes")
    check(0 not in body, "index 0 never appears in pixel data")
    check(max(body) < cmaplen, "every index is inside the colormap")


def test_batch_naming_and_case_insensitivity():
    print("test_batch_naming_and_case_insensitivity")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        make_png(src / "HOUSE.PNG")
        make_png(src / "cmplab.png")
        written = make_tga.batch(src, dst)

        check(written == 2, "both sources converted")
        check((dst / "HOUSE.TGA").exists(), "uppercase source -> HOUSE.TGA")
        check((dst / "CMPLAB.TGA").exists(), "lowercase source -> CMPLAB.TGA")


def test_batch_skips_offsize_but_continues():
    print("test_batch_skips_offsize_but_continues")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        make_png(src / "ANCIENT.PNG", w=640, h=480)
        make_png(src / "CLIFF.PNG")
        written = make_tga.batch(src, dst)

        check(written == 1, "only the correctly-sized image converted")
        check(not (dst / "ANCIENT.TGA").exists(), "off-size image produced no TGA")
        check((dst / "CLIFF.TGA").exists(), "a bad file does not block later files")


def test_batch_skips_long_stems():
    print("test_batch_skips_long_stems")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        make_png(src / "TYPEWRTR.png")          # 8 chars - allowed
        make_png(src / "TOOLONGNAME.png")       # 11 chars - rejected
        written = make_tga.batch(src, dst)

        check(written == 1, "only the 8.3-safe name converted")
        check((dst / "TYPEWRTR.TGA").exists(), "exactly 8 characters is allowed")
        check(not (dst / "TOOLONGNAME.TGA").exists(), "over 8 characters is skipped")


def test_batch_is_incremental():
    print("test_batch_is_incremental")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        png = src / "HOUSE.PNG"
        make_png(png)
        make_tga.batch(src, dst)
        tga = dst / "HOUSE.TGA"

        # TGA newer than PNG -> untouched.
        os.utime(png, (1000, 1000))
        os.utime(tga, (2000, 2000))
        before = tga.stat().st_mtime_ns
        written = make_tga.batch(src, dst)
        check(written == 0, "up-to-date TGA reported as not written")
        check(tga.stat().st_mtime_ns == before, "up-to-date TGA left untouched")

        # PNG newer than TGA -> rebuilt.
        os.utime(png, (3000, 3000))
        written = make_tga.batch(src, dst)
        check(written == 1, "stale TGA is regenerated")
        check(tga.stat().st_mtime_ns != before, "regenerated TGA has a new mtime")


def main():
    for t in (test_encode_tga_structure,
              test_batch_naming_and_case_insensitivity,
              test_batch_skips_offsize_but_continues,
              test_batch_skips_long_stems,
              test_batch_is_incremental):
        t()
    print()
    if FAILURES:
        print(f"FAILED {len(FAILURES)} check(s):")
        for f in FAILURES:
            print("  - " + f)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `tools/.venv/Scripts/python.exe tools/tests/test_make_tga.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'make_tga'`

- [ ] **Step 3: Write the implementation**

Create `tools/make_tga.py`:

```python
#!/usr/bin/env python3
"""Convert PNG backgrounds into the 8bpp paletted TGAs the Saturn disc expects.

Usage:
    python tools/make_tga.py <src-dir> <dst-dir>    # batch (what the build runs)
    python tools/make_tga.py <src.png> <dst.tga>    # single file

Two constraints make this script necessary instead of a plain image-editor
export:

1. **8bpp paletted, never truecolor.** SRL's VRAM::AutoAllocateBmp doubles the
   VDP2 bitmap container size for RGB555, so the 512x256 container becomes 256KB
   and spans the A0/A1 VRAM bank boundary. Bank-spanning bitmaps render as
   static: slBitMapNbg0 never reserves the second bank in VDP2_RAMCTL, and SRL's
   allocator tracks banks only in software (see srl_vdp2.hpp:11-15). At 8bpp the
   container is exactly 128KB and fits one bank.

2. **Palette index 0 must be unused.** VDP2 treats index 0 on a scroll screen as
   transparent, which would punch back-color holes through the image. We
   quantize to 255 colors and shift every index up by one.

The TGA is written by hand because PIL re-optimizes the palette on save, which
silently undoes constraint 2.

Sources that are not exactly 320x224, or whose name would not survive ISO9660
8.3 truncation, are reported and skipped rather than aborting the run -- the
build calls this on every compile and one bad file must not stop the rest.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

WIDTH, HEIGHT = 320, 224
MAX_STEM = 8  # ISO9660 8.3; the build passes --norock to xorrisofs


def encode_tga(im):
    """Pack a 320x224 RGB image into a complete 8bpp paletted TGA file image."""
    w, h = im.size
    q = im.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    idx = q.tobytes()
    ncolors = max(idx) + 1

    flat = q.getpalette()[: ncolors * 3]
    rgb = [tuple(flat[i * 3 : i * 3 + 3]) for i in range(ncolors)]

    # Reserve index 0: shift colors up one slot, pixels follow.
    palette = [(0, 0, 0)] + rgb
    pixels = bytes(b + 1 for b in idx)
    assert 0 not in pixels, "index 0 must stay unused (VDP2 reads it as transparent)"
    assert max(pixels) < len(palette) <= 256

    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,              # no image ID
        1,              # colormap present
        1,              # uncompressed paletted
        0,              # colormap start
        len(palette),   # colormap length
        24,             # colormap entry depth
        0, 0,           # origin x/y
        w, h,
        8,              # 8bpp indices
        0x00,           # bottom-left origin, no alpha bits
    )
    cmap = b"".join(bytes((b, g, r)) for (r, g, b) in palette)  # TGA colormaps are BGR
    rows = [pixels[y * w : (y + 1) * w] for y in range(h)]
    body = b"".join(reversed(rows))  # bottom-left origin: rows bottom-to-top
    return header + cmap + body


def convert_one(src, dst):
    """Convert one PNG. Returns (status, message) with status 'wrote' or 'skip'."""
    im = Image.open(src).convert("RGB")
    w, h = im.size
    if (w, h) != (WIDTH, HEIGHT):
        return ("skip", f"{src.name}: expected {WIDTH}x{HEIGHT}, got {w}x{h}")

    blob = encode_tga(im)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(blob)
    return ("wrote", f"{dst.name}: {w}x{h} 8bpp, index 0 reserved, {len(blob)} bytes")


def batch(srcdir, dstdir):
    """Convert every PNG in srcdir into dstdir. Returns the number written."""
    srcdir, dstdir = Path(srcdir), Path(dstdir)
    if not srcdir.is_dir():
        print(f"  skip  {srcdir} does not exist -- nothing to convert")
        return 0

    sources = sorted(p for p in srcdir.iterdir()
                     if p.is_file() and p.suffix.lower() == ".png")
    if not sources:
        print(f"  skip  no PNG files in {srcdir}")
        return 0

    written = 0
    for src in sources:
        stem = src.stem.upper()
        if len(stem) > MAX_STEM:
            print(f"  skip  {src.name}: name over {MAX_STEM} characters "
                  f"(ISO9660 8.3); rename it")
            continue

        dst = dstdir / (stem + ".TGA")
        if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
            print(f"  ok    {dst.name} up to date")
            continue

        status, message = convert_one(src, dst)
        print(f"  {'wrote' if status == 'wrote' else 'skip '} {message}")
        if status == "wrote":
            written += 1

    return written


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2

    src, dst = Path(argv[1]), Path(argv[2])
    if src.is_dir() or dst.suffix.lower() != ".tga":
        batch(src, dst)
        return 0

    status, message = convert_one(src, dst)
    print(f"  {'wrote' if status == 'wrote' else 'skip '} {message}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `tools/.venv/Scripts/python.exe tools/tests/test_make_tga.py`
Expected: every line prefixed `ok`, final line `all checks passed`, exit code 0.

- [ ] **Step 5: Confirm it reproduces the known-good HOUSE.TGA**

The committed `saturn/cd/data/TGA/HOUSE.TGA` was produced by the old script. Regenerate it into a scratch path and compare structure (quantization is deterministic for a fixed input, so the bytes should match too):

```bash
tools/.venv/Scripts/python.exe tools/make_tga.py saturn/cd/data/PNG/HOUSE.PNG /tmp/HOUSE.TGA
cmp /tmp/HOUSE.TGA saturn/cd/data/TGA/HOUSE.TGA && echo IDENTICAL
```

Expected: `IDENTICAL`. If the bytes differ, verify the header fields and body length match (18 + 768 + 71680 = 72205 bytes) before proceeding — a size mismatch is a real bug, byte differences with an identical size warrant checking the Pillow version.

- [ ] **Step 6: Delete the superseded script**

```bash
git rm tools/make_house_tga.py
```

- [ ] **Step 7: Commit**

```bash
git add tools/make_tga.py tools/tests/test_make_tga.py
git commit -m "Add batch PNG->TGA converter with host tests

Replaces the single-file make_house_tga.py. Converts a whole directory,
skips off-size and non-8.3 sources with a warning instead of aborting,
and re-converts only when the PNG is newer than its TGA."
```

---

### Task 2: Relocate source PNGs out of the disc tree

**Files:**
- Move: `saturn/cd/data/PNG/*` → `tools/assets/png/`
- Modify: `.gitignore`
- Create: `saturn/cd/data/TGA/*.TGA` (six generated files)

**Interfaces:**
- Consumes: `tools/make_tga.py` from Task 1.
- Produces: `tools/assets/png/` as the canonical source-art location and `saturn/cd/data/TGA/` as the generated-output location. Task 3's shell script hardcodes both paths.

Rationale: `xorrisofs` includes `cd/data/` recursively (`SaturnRingLib/saturnringlib/shared.mk:267-270`), so everything under `PNG/` — including the 9.4 MB `ANCIENT.PNG` — is currently burned onto the ISO. Nothing at runtime reads that directory; `display_scan_images` (`saturn/src/main.cxx:2107-2191`) scans only `TGA/`.

- [ ] **Step 1: Move the sources**

```bash
mkdir -p tools/assets/png
git mv saturn/cd/data/PNG/AMIGA.PNG    tools/assets/png/AMIGA.PNG
git mv saturn/cd/data/PNG/ANCIENT.PNG  tools/assets/png/ANCIENT.PNG
git mv saturn/cd/data/PNG/CASTLE.PNG   tools/assets/png/CASTLE.PNG
git mv saturn/cd/data/PNG/CLIFF.PNG    tools/assets/png/CLIFF.PNG
git mv saturn/cd/data/PNG/CMPLAB.png   tools/assets/png/CMPLAB.png
git mv saturn/cd/data/PNG/FOREST.PNG   tools/assets/png/FOREST.PNG
git mv saturn/cd/data/PNG/HOUSE.PNG    tools/assets/png/HOUSE.PNG
git mv saturn/cd/data/PNG/TYPEWRTR.png tools/assets/png/TYPEWRTR.png
```

If any file is untracked, `git mv` fails on it — use plain `mv` for that file. Confirm `saturn/cd/data/PNG/` is empty afterwards and remove it:

```bash
rmdir saturn/cd/data/PNG
```

- [ ] **Step 2: Verify the disc tree no longer holds source art**

Run: `ls saturn/cd/data/`
Expected: `0.bin ABS.TXT BIB.TXT BOOTSND.MAP CPY.TXT SDDRVS.DAT SDDRVS.TSK TGA Z3` — no `PNG`.

- [ ] **Step 3: Add the Python section to `.gitignore`**

Append to `.gitignore`:

```gitignore
# --- Asset-tool Python environment -------------------------------------------
# Provisioned on demand by tools/convert-backgrounds.sh; never committed.
tools/.venv/
**/__pycache__/
*.pyc
```

- [ ] **Step 4: Generate the backgrounds**

```bash
tools/.venv/Scripts/python.exe tools/make_tga.py tools/assets/png saturn/cd/data/TGA
```

Expected output — six `wrote` lines (`AMIGA`, `CASTLE`, `CLIFF`, `CMPLAB`, `FOREST`, `TYPEWRTR`), one `skip` line for `ANCIENT.PNG` reading `expected 320x224, got 3105x4658`, and one `ok ... up to date` line for `HOUSE.TGA` (its committed TGA is newer than `HOUSE.PNG`).

- [ ] **Step 5: Verify incremental behavior**

Run the exact same command again.
Expected: seven `ok ... up to date` lines and the same single `ANCIENT.PNG` skip. Zero `wrote` lines.

- [ ] **Step 6: Verify every generated file is well-formed**

```bash
tools/.venv/Scripts/python.exe -c "
from pathlib import Path
for p in sorted(Path('saturn/cd/data/TGA').glob('*.TGA')):
    d = p.read_bytes()
    n = d[5] | (d[6] << 8)
    body = d[18 + n*3:]
    print(p.name, d[2], d[16], d[12]|d[13]<<8, d[14]|d[15]<<8, len(body), 0 in body)
"
```

Expected: every line reads `<NAME>.TGA 1 8 320 224 71680 False` — imgtype 1, 8bpp, 320x224, exactly 71680 body bytes, and `False` for "contains index 0".

- [ ] **Step 7: Commit**

```bash
git add -A tools/assets/png saturn/cd/data/TGA .gitignore
git commit -m "Move background PNG sources out of the disc tree, generate TGAs

xorrisofs includes cd/data/ recursively, so ~10MB of source PNGs were
being burned onto the ISO. Sources now live in tools/assets/png/ and the
generated TGAs remain committed as the no-Python fallback.

ANCIENT.PNG is 3105x4658 and is skipped until it is resized to 320x224."
```

---

### Task 3: Venv bootstrap wrapper

**Files:**
- Create: `tools/requirements.txt`
- Create: `tools/convert-backgrounds.sh`

**Interfaces:**
- Consumes: `tools/make_tga.py` (Task 1); the directory layout fixed in Task 2.
- Produces: `tools/convert-backgrounds.sh`, invoked by Task 4's `pre_build` recipe as `sh ../tools/convert-backgrounds.sh`. It locates the repo root from its own path, so the caller's working directory does not matter.

- [ ] **Step 1: Write the requirements file**

Create `tools/requirements.txt`:

```
# Dependencies for the asset tools (tools/make_tga.py).
# Installed into tools/.venv on demand by tools/convert-backgrounds.sh.
Pillow>=10,<13
```

- [ ] **Step 2: Write the bootstrap script**

Create `tools/convert-backgrounds.sh`:

```sh
#!/bin/sh
# Convert the staged PNG backgrounds into 8bpp paletted TGAs for the Saturn disc.
#
# Invoked by saturn/pre.makefile on every build, and runnable by hand from any
# directory. Provisions tools/.venv on first run and does no network access
# thereafter.
#
# A missing interpreter, a missing dependency, or no network prints an
# actionable warning and exits 0 -- the build then uses the TGAs already
# committed under saturn/cd/data/TGA/. Only a genuine converter crash fails.

set -u

REPO="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VENV="$REPO/tools/.venv"
REQ="$REPO/tools/requirements.txt"
SRC="$REPO/tools/assets/png"
DST="$REPO/saturn/cd/data/TGA"

warn() {
    echo ""
    echo "  *** background conversion skipped ***"
    echo "  $1"
    echo "  The build continues using the TGAs already in saturn/cd/data/TGA/."
    echo ""
}

# Echo the first interpreter that is Python 3.9 or newer.
find_python() {
    for candidate in "py -3" python3 python; do
        # Intentionally unquoted: "py -3" must split into command plus argument.
        if $candidate -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)' 2>/dev/null; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

# Echo the venv interpreter path, whichever layout this platform uses.
venv_python() {
    if [ -x "$VENV/Scripts/python.exe" ]; then
        echo "$VENV/Scripts/python.exe"
    elif [ -x "$VENV/bin/python" ]; then
        echo "$VENV/bin/python"
    else
        return 1
    fi
}

PY=$(venv_python) || {
    BOOT=$(find_python) || {
        warn "No Python 3.9+ on PATH. Install it from https://www.python.org/downloads/ and rebuild."
        exit 0
    }
    echo "  creating asset-tool virtualenv in tools/.venv ..."
    # Intentionally unquoted, same reason as above.
    $BOOT -m venv "$VENV" || {
        warn "Could not create a virtualenv. On Debian/Ubuntu try: apt install python3-venv"
        exit 0
    }
    PY=$(venv_python) || {
        warn "Virtualenv was created but contains no interpreter. Delete tools/.venv and retry."
        exit 0
    }
}

if ! "$PY" -c 'import PIL' 2>/dev/null; then
    echo "  installing asset-tool dependencies ..."
    "$PY" -m pip install --quiet --disable-pip-version-check -r "$REQ" || {
        warn "Could not install Pillow (offline?). Run: $PY -m pip install -r tools/requirements.txt"
        exit 0
    }
fi

"$PY" "$REPO/tools/make_tga.py" "$SRC" "$DST"
```

- [ ] **Step 3: Verify the happy path from a cold start**

Delete the hand-made venv so the script has to build one:

```bash
rm -rf tools/.venv
sh tools/convert-backgrounds.sh
```

Expected: `creating asset-tool virtualenv ...`, then `installing asset-tool dependencies ...`, then seven `ok ... up to date` lines and the `ANCIENT.PNG` skip. Exit code 0 (`echo $?`).

- [ ] **Step 4: Verify the warm path does no network access**

Run: `sh tools/convert-backgrounds.sh`
Expected: no `creating` or `installing` lines — straight to the file list. This is what every incremental build will print.

- [ ] **Step 5: Verify it works from another working directory**

The `pre_build` recipe runs with `saturn/` as the working directory:

```bash
cd saturn && sh ../tools/convert-backgrounds.sh; cd ..
```

Expected: identical output to Step 4. Any "no such file" error means the `REPO` resolution is wrong.

- [ ] **Step 6: Verify the no-Python fallback**

Simulate a machine with no interpreter by hiding both the venv and `PATH`:

```bash
rm -rf tools/.venv
env PATH=/usr/bin:/bin sh tools/convert-backgrounds.sh; echo "exit=$?"
```

Expected: the `*** background conversion skipped ***` banner naming python.org, and `exit=0`. If your `/usr/bin` happens to contain a Python 3.9+, this check is inconclusive — instead confirm by temporarily renaming the interpreters, or accept the Task 4 end-to-end build check as the evidence.

Then restore the venv: `sh tools/convert-backgrounds.sh`

- [ ] **Step 7: Commit**

```bash
git add tools/requirements.txt tools/convert-backgrounds.sh
git commit -m "Add self-provisioning venv wrapper for background conversion

Finds a Python 3.9+ interpreter, builds tools/.venv on first run, and
installs Pillow only when the import fails. Any environment problem warns
and exits 0 so a contributor without Python can still build."
```

---

### Task 4: Wire into the build and document it

**Files:**
- Create: `saturn/pre.makefile`
- Modify: `README.md:46-52` (Prerequisites), `README.md:209` (new section), `README.md:211,244,263` (renumber)

**Interfaces:**
- Consumes: `tools/convert-backgrounds.sh` (Task 3).
- Produces: nothing consumed by later tasks. This is the final task.

`SaturnRingLib/saturnringlib/shared.mk:215-229` includes `./pre.makefile` when it exists and otherwise defines a no-op `pre_build`. Creating the file is the whole integration — the submodule is not touched.

- [ ] **Step 1: Create the pre-build hook**

Create `saturn/pre.makefile`:

```make
# Included automatically by SaturnRingLib's shared.mk when this file exists
# (shared.mk:215-229). Recipes run under MSYS2 sh with saturn/ as the working
# directory.
pre_build:
	$(info ****** Converting PNG backgrounds to TGA ******)
	@sh ../tools/convert-backgrounds.sh
```

- [ ] **Step 2: Build and confirm the step runs**

Ask the user to run the build — do not run `compile.bat` yourself:

> Please run `cd saturn && ./compile.bat debug` and paste the output.

Expected in the log: the `****** Converting PNG backgrounds to TGA ******` banner followed by seven `ok ... up to date` lines and the `ANCIENT.PNG` skip, then the normal compile, then `mojozork.iso` produced without error.

- [ ] **Step 3: Confirm the PNGs are gone from the ISO**

```bash
tools/.venv/Scripts/python.exe -c "
d = open('saturn/BuildDrop/mojozork.iso','rb').read()
print('PNG dir on disc:', b'ANCIENT.PNG' in d)
print('TGA dir on disc:', b'CASTLE.TGA' in d)
"
```

Expected: `PNG dir on disc: False` and `TGA dir on disc: True`.

- [ ] **Step 4: Update the README prerequisites**

In `README.md`, replace the Prerequisites list (currently lines 48-52) with:

```markdown
Builds on Windows, Linux, or macOS:

- Git for Windows (**Git Bash**) or a POSIX shell.
- The SaturnRingLib SH-2 cross-compiler, fetched in Step 2 below (≈ needs `curl`/`unzip`).
- **Python 3.9+** *(optional)* — converts the background art in `tools/assets/png/`
  into disc-ready TGAs during the build, provisioning its own virtualenv on first
  run. Without it the build still succeeds using the TGAs already committed under
  `saturn/cd/data/TGA/`.
- An emulator for testing (e.g. **Mednafen** with Saturn BIOS), or real hardware.
```

- [ ] **Step 5: Add the new README section**

Insert this immediately after the `---` that ends section 5 (currently `README.md:209`), before `## 6. Playing online from a real Saturn`:

```markdown
## 6. Adding a background image

The Display Options page lists every `*.TGA` it finds in `saturn/cd/data/TGA/`.
Those files are generated from the PNGs in `tools/assets/png/` on every build —
you do not create them by hand.

1. Save your artwork as a **320x224** PNG in `tools/assets/png/`, with a name of
   **8 characters or fewer**, e.g. `tools/assets/png/CAVE.PNG`.
2. Rebuild: `cd saturn && ./compile.bat debug`.
3. The new background appears in **Display Options**.

The size and name limits are enforced, not advisory: the Saturn reads these as
ISO9660 8.3 names, and the converter skips anything that is not exactly 320x224
rather than guessing at a crop. Both cases print a warning naming the file and
never fail the build.

Commit the generated `saturn/cd/data/TGA/*.TGA` alongside your PNG — they are
what lets someone without Python build a complete disc.

Conversion is handled by `tools/make_tga.py`, which quantizes to 255 colors and
reserves palette index 0 (VDP2 renders index 0 as transparent) and emits 8bpp
paletted output (an RGB555 bitmap would span two VRAM banks and render as
static). Run `python tools/tests/test_make_tga.py` to exercise it directly.

---
```

- [ ] **Step 6: Renumber the following sections**

The three headings after the insertion point shift by one:

- `## 6. Playing online from a real Saturn` → `## 7. Playing online from a real Saturn`
- `## 7. Hosting the multizork server yourself` → `## 8. Hosting the multizork server yourself`
- `## 8. Releases (prebuilt disc)` → `## 9. Releases (prebuilt disc)`

Verify with `grep -n "^## " README.md` that the numbered sections read 1 through 9 with no duplicates or gaps. The repo has no in-document anchor links, so nothing else needs updating.

- [ ] **Step 7: Commit**

```bash
git add saturn/pre.makefile README.md
git commit -m "Run background conversion as a pre-build step

Hooks tools/convert-backgrounds.sh into SaturnRingLib's pre.makefile
extension point, so adding a background is now: drop a 320x224 PNG in
tools/assets/png/ and rebuild. Documents the workflow in the README."
```

---

## Verification

End-to-end, after all four tasks:

1. `tools/.venv/Scripts/python.exe tools/tests/test_make_tga.py` → `all checks passed`.
2. `rm -rf tools/.venv && cd saturn && ./compile.bat debug` → the venv is provisioned mid-build and the ISO is produced. *(User runs this; do not run `compile.bat` yourself.)*
3. Second `./compile.bat debug` → conversion reports everything up to date and adds no measurable time.
4. `git status` after a build shows no modified `saturn/cd/data/TGA/*.TGA` — the build rewrites no background when sources are unchanged. (Other build outputs such as `cd/data/0.bin` may legitimately show as modified; only the TGAs are being checked here.)
5. Boot `saturn/BuildDrop/mojozork.iso` in Mednafen, open **Display Options**, and confirm the selector now offers `AMIGA`, `CASTLE`, `CLIFF`, `CMPLAB`, `FOREST`, `HOUSE`, and `TYPEWRTR`, each rendering without static or transparent holes.

Known follow-up, deliberately out of scope: `ANCIENT.PNG` (3105x4658) will keep warning on every build until someone resizes it to 320x224 or deletes it.
