#!/usr/bin/env python3
"""Turn a probe of a real dialog into the dialog units that reproduce it.

`tools/vm/probe.c` prints every control of a running dialog in screen
pixels. The dialog manager got those pixels from a template written in
dialog units, through MulDiv(units, base, 4) across and MulDiv(units, base,
8) down; this runs that backwards, and checks the answer by mapping it
forward again.

    tools/paint/dlu.py ~/paintshare/attr.txt

Prints one line per control, ready to be read into a table of
dlgtemplate.Item, and says so if any pixel cannot be reached from a whole
number of units -- which would mean the base units are not 6 and 13.
"""
import re
import sys

BX, BY = 6, 13


def muldiv(n, num, den):
    """Round half away from zero, as win32's MulDiv does."""
    v = n * num
    return (v + den // 2) // den if v >= 0 else -((-v + den // 2) // den)


def to_units(px, base, den):
    """The whole number of units that maps to exactly this many pixels."""
    guess = round(px * den / base)
    for d in (guess, guess - 1, guess + 1, guess - 2, guess + 2):
        if muldiv(d, base, den) == px:
            return d
    return None


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    text = open(argv[1], errors="replace").read()
    top = re.search(r"#32770\s+(-?\d+),(-?\d+)\s+(\d+)x(\d+)\s+style=(\w+)", text)
    if not top:
        print("no dialog in that probe")
        return 1
    wx, wy, ww, wh = (int(top.group(i)) for i in range(1, 5))
    style = int(top.group(5), 16)
    m = re.search(r"client (\d+)x(\d+)", text)
    cw, ch = int(m.group(1)), int(m.group(2))
    # the frame is what is left over, and the caption the rest of the height
    frame = (ww - cw) // 2
    ox, oy = wx + frame, wy + (wh - ch) - frame
    print("dialog %dx%d px = %s x %s du   style=%08X" %
          (cw, ch, to_units(cw, BX, 4), to_units(ch, BY, 8), style))
    bad = 0
    for line in text.splitlines():
        m = re.match(r"\s+\w+ (\S+)\s+(-?\d+),(-?\d+)\s+(\d+)x(\d+)\s+"
                     r"style=(\w+) ex=(\w+) id=(-?\d+) \"(.*)\"", line)
        if not m:
            continue
        cls, x, y, w, h = m.group(1), *(int(m.group(i)) for i in range(2, 6))
        st, ex, cid, txt = int(m.group(6), 16), int(m.group(7), 16), int(m.group(8)), m.group(9)
        u = [to_units(x - ox, BX, 4), to_units(y - oy, BY, 8),
             to_units(w, BX, 4), to_units(h, BY, 8)]
        if None in u:
            bad += 1
        print("  %-8s id=%-6d .x = %s, .y = %s, .cx = %s, .cy = %s,"
              "  style=%08X ex=%04X  %r" %
              (cls, cid, *u, st, ex, txt))
    if bad:
        print("%d control(s) do not land on whole dialog units" % bad)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
