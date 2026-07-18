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
