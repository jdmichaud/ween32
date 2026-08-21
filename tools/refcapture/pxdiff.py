#!/usr/bin/env python3
"""Compare a ween32 render against the win32 reference, pixel by pixel.

    tools/refcapture/pxdiff.py                 # whole window: where do we differ?
    tools/refcapture/pxdiff.py 12 12 75 23     # one region, as an ASCII map

The maps print one character per pixel from a shared legend, so a bevel that is
one row out, or a colour that is one shade off, is obvious. Coordinates are
window coordinates (0,0 = top-left of the window, frame included).

Renders come from:
    tools/refcapture/capture.sh                              -> reference.png
    WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=ours.bmp ./examples/controls
"""
import sys
from PIL import Image

import os

# The control sampler by default; PXDIFF_REF/PXDIFF_OUR point it at another
# pair, such as the menu sampler.
REF = os.environ.get("PXDIFF_REF", "tools/refcapture/reference.png")
OUR = os.environ.get("PXDIFF_OUR", "/tmp/ours.png")

# The classic palette, so the maps read as colours rather than numbers.
LEGEND = {
    (212, 208, 200): ".",  # face
    (255, 255, 255): "W",  # button highlight / window
    (128, 128, 128): "s",  # button shadow
    (64, 64, 64): "D",  # dark shadow
    (0, 0, 0): "#",  # black / text
    (10, 36, 106): "N",  # active caption, highlight
    (166, 202, 240): "L",  # caption gradient end
}


def glyph(p, extra):
    if p in LEGEND:
        return LEGEND[p]
    if p not in extra:
        extra[p] = "abcdefghijklmnopqrtuvwxyz"[len(extra) % 25]
    return extra[p]


def load(path):
    im = Image.open(path).convert("RGB")
    return im, im.load(), im.size


def region_map(x0, y0, w, h):
    (ri, rp, rs), (oi, op, os) = load(REF), load(OUR)
    extra = {}
    print(f"reference {rs}   ween32 {os}   region {x0},{y0} {w}x{h}\n")
    head = " " * 5 + "".join(str(x % 10) for x in range(x0, x0 + w))
    print(head + "   " + head[5:])
    diff = 0
    for y in range(y0, y0 + h):
        a = b = ""
        for x in range(x0, x0 + w):
            pr = rp[x, y] if x < rs[0] and y < rs[1] else None
            po = op[x, y] if x < os[0] and y < os[1] else None
            a += glyph(pr, extra) if pr else " "
            b += glyph(po, extra) if po else " "
            if pr != po:
                diff += 1
        print(f"{y:4d} {a}   {b}")
    print(f"\ndiffering pixels in region: {diff}/{w * h}")
    if extra:
        print("other colours: " + ", ".join(f"{c}={rgb}" for rgb, c in extra.items()))


def overview():
    (ri, rp, rs), (oi, op, os) = load(REF), load(OUR)
    print(f"reference {rs}   ween32 {os}")
    if rs != os:
        print("!! sizes differ — the non-client metrics do not match yet")
    w, h = min(rs[0], os[0]), min(rs[1], os[1])
    rows = []
    total = 0
    for y in range(h):
        n = sum(1 for x in range(w) if rp[x, y] != op[x, y])
        total += n
        rows.append(n)
    print(f"differing pixels: {total} of {w * h} ({100.0 * total / (w * h):.1f}%)")
    print("\nby row band (32px):")
    for y in range(0, h, 32):
        n = sum(rows[y : y + 32])
        bar = "#" * min(60, n // 40)
        print(f"  y {y:4d}-{min(y + 31, h - 1):4d}  {n:6d}  {bar}")


if __name__ == "__main__":
    if len(sys.argv) == 5:
        region_map(*(int(a) for a in sys.argv[1:5]))
    else:
        overview()
