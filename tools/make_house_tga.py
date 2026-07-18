#!/usr/bin/env python3
"""Generate saturn/cd/data/HOUSE.TGA (title-screen background) from house.png.

Usage:  python tools/make_house_tga.py [src.png] [dst.tga]

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
"""
import struct
import sys

from PIL import Image

SRC = sys.argv[1] if len(sys.argv) > 1 else "saturn/cd/data/house.png"
DST = sys.argv[2] if len(sys.argv) > 2 else "saturn/cd/data/HOUSE.TGA"

im = Image.open(SRC).convert("RGB")
W, H = im.size
if (W, H) != (320, 224):
    sys.exit(f"expected a 320x224 source image, got {W}x{H}")

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
    W, H,
    8,              # 8bpp indices
    0x00,           # bottom-left origin, no alpha bits
)

cmap = b"".join(bytes((b, g, r)) for (r, g, b) in palette)  # TGA colormaps are BGR
rows = [pixels[y * W : (y + 1) * W] for y in range(H)]
body = b"".join(reversed(rows))  # bottom-left origin: rows bottom-to-top

with open(DST, "wb") as f:
    f.write(header + cmap + body)

print(f"wrote {DST}: {W}x{H} 8bpp, {len(palette)} colors, index 0 reserved, "
      f"{len(header) + len(cmap) + len(body)} bytes")
