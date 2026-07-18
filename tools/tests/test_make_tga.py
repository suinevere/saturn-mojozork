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


def four_quadrants(w=make_tga.WIDTH, h=make_tga.HEIGHT):
    """Four exactly-distinguishable quadrant colors -- quantize losslessly so
    decoded pixels can be checked for exact RGB equality, no tolerance."""
    im = Image.new("RGB", (w, h))
    red, green, blue, white = (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)

    def color_at(x, y):
        left = x < w // 2
        top = y < h // 2
        if top and left:
            return red
        if top and not left:
            return green
        if not top and left:
            return blue
        return white

    im.putdata([color_at(x, y) for y in range(h) for x in range(w)])
    return im


def test_encode_tga_pixel_roundtrip():
    print("test_encode_tga_pixel_roundtrip")
    w, h = make_tga.WIDTH, make_tga.HEIGHT
    blob = make_tga.encode_tga(four_quadrants(w, h))

    cmaplen = blob[5] | (blob[6] << 8)
    cmap_start = 18
    body_start = cmap_start + cmaplen * 3

    def colormap_entry(index):
        off = cmap_start + index * 3
        b, g, r = blob[off], blob[off + 1], blob[off + 2]
        return (r, g, b)

    def pixel_at(x, y):
        # Body is bottom-to-top: body row r holds image row (h - 1 - r).
        row = h - 1 - y
        idx = blob[body_start + row * w + x]
        return colormap_entry(idx)

    check(pixel_at(80, 56) == (255, 0, 0), "top-left quadrant decodes to pure red")
    check(pixel_at(240, 56) == (0, 255, 0), "top-right quadrant decodes to pure green")
    check(pixel_at(80, 168) == (0, 0, 255), "bottom-left quadrant decodes to pure blue")
    check(pixel_at(240, 168) == (255, 255, 255), "bottom-right quadrant decodes to pure white")


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
              test_encode_tga_pixel_roundtrip,
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
