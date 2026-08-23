#!/usr/bin/env python3
"""Compare a Paint render with the machine's, and say where it differs.

    tools/refcapture/paintdiff.py                    # totals, by region
    tools/refcapture/paintdiff.py 4 42 57 40         # x y w h: an ASCII map
    tools/refcapture/paintdiff.py --image /tmp/d.png # the two, and a mask

The regions are Paint's own windows, so a total is attributable: "the tool
box is right and the status bar is not" rather than "5.7% of the window".
"""
import os
import sys
from PIL import Image

REF = os.environ.get("PAINT_REF", "tools/refcapture/paint/00-default.png")
OUR = os.environ.get("PAINT_OUR", "/tmp/paint-ours.png")

# x, y, w, h in window coordinates, from the probe's window list.
REGIONS = [
    ("caption", 0, 0, 275, 23),
    ("menu bar", 4, 23, 267, 19),
    ("tool box", 4, 42, 57, 282),
    ("view", 61, 42, 210, 282),
    ("colour box", 4, 324, 267, 49),
    ("status bar", 4, 373, 267, 23),
    ("frame", 0, 0, 275, 400),  # counted whole, so the rest shows up
]

LEGEND = {
    (212, 208, 200): ".",
    (255, 255, 255): "W",
    (128, 128, 128): "s",
    (64, 64, 64): "D",
    (0, 0, 0): "#",
    (10, 36, 106): "N",
    (166, 202, 240): "L",
    (192, 192, 192): "g",
}


def load(path):
    im = Image.open(path).convert("RGB")
    return im, im.load()


def glyph(p, extra):
    if p in LEGEND:
        return LEGEND[p]
    if p not in extra:
        extra[p] = "abcdefhijklmnopqrtuvxyz"[len(extra) % 23]
    return extra[p]


def count(rp, op, x0, y0, w, h, size):
    n = 0
    for y in range(y0, min(y0 + h, size[1])):
        for x in range(x0, min(x0 + w, size[0])):
            if rp[x, y] != op[x, y]:
                n += 1
    return n


def main():
    ri, rp = load(REF)
    oi, op = load(OUR)
    if ri.size != oi.size:
        print("!! sizes differ: reference %s, ours %s" % (ri.size, oi.size))
    size = (min(ri.size[0], oi.size[0]), min(ri.size[1], oi.size[1]))

    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--image" in sys.argv:
        out = args[0] if args else "/tmp/paint-diff.png"
        w, h = size
        canvas = Image.new("RGB", (w * 3 + 8, h), (255, 0, 255))
        canvas.paste(ri, (0, 0))
        canvas.paste(oi, (w + 4, 0))
        mask = Image.new("RGB", (w, h), (255, 255, 255))
        mp = mask.load()
        for y in range(h):
            for x in range(w):
                if rp[x, y] != op[x, y]:
                    mp[x, y] = (255, 0, 0)
        canvas.paste(mask, (2 * w + 8, 0))
        canvas.save(out)
        print("wrote %s (reference | ours | where they differ)" % out)
        return

    if len(args) == 4:
        x0, y0, w, h = (int(a) for a in args)
        extra = {}
        head = " " * 5 + "".join(str(x % 10) for x in range(x0, x0 + w))
        print("reference%sours" % (" " * (w - 4)))
        print(head + "   " + head[5:])
        for y in range(y0, y0 + h):
            a = b = ""
            for x in range(x0, x0 + w):
                a += glyph(rp[x, y], extra)
                b += glyph(op[x, y], extra)
            mark = "|" if a != b else " "
            print("%4d %s %s %s" % (y, a, mark, b))
        print("\ndiffering: %d/%d" % (count(rp, op, x0, y0, w, h, size), w * h))
        if extra:
            print("other colours: " + ", ".join("%s=%s" % (c, rgb) for rgb, c in extra.items()))
        return

    total = count(rp, op, 0, 0, size[0], size[1], size)
    print("differing pixels: %d of %d (%.1f%%)\n" % (total, size[0] * size[1],
                                                     100.0 * total / (size[0] * size[1])))
    for name, x, y, w, h in REGIONS:
        n = count(rp, op, x, y, w, h, size)
        print("  %-12s %4d,%-4d %3dx%-3d  %6d  %s" % (name, x, y, w, h, n,
                                                      "#" * min(50, n // 20)))


main()
