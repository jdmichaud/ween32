#!/usr/bin/env python3
"""Read Paint's own pointers off the machine.

A cursor is not in the window, so the probe cannot ask for it and a
screenshot of the window does not hold it -- but the emulator draws the
pointer into its frame buffer, so a screenshot of the *screen* does.

Each tool is picked in turn and the pointer parked over a page flooded with
the palette's grey, which is what tells the four kinds of pixel apart in one
shot: grey is the page showing through, black and white are the cursor's
own, and the inverse of grey is a pixel that inverts what is under it.

    tools/mspaint/grabcursors.py                 # writes assets/paint-cursors.png
    tools/mspaint/grabcursors.py 3 6 8           # only those tools
    tools/mspaint/grabcursors.py --reuse         # from the shots already taken

The hot spot is where the pointer really was, which is measured first by
leaving a pencil dot at the same place: the ink lands on the hot spot, and
the cursor is drawn round it.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DRIVE = os.path.join(ROOT, "tools/vm/drive.py")

# where the pointer is parked, and how far round it to look
AT = (160, 200)
PAD = 24

# the page it is parked over, and what an inverting pixel comes out as
PAGE = (192, 192, 192)
INVERSE = (255 - PAGE[0], 255 - PAGE[1], 255 - PAGE[2])
GREY_SWATCH = 15  # the palette square that colour sits in

TOOLS = list(range(16))


def drive(*args):
    subprocess.run([sys.executable, DRIVE] + [str(a) for a in args],
                   check=False, capture_output=True)


def tool_point(n):
    return (8 + (n % 2) * 25 + 12, 42 + (n // 2) * 25 + 12)


def color_point(n):
    return (35 + 16 * (n % 14) + 8, 331 + 16 * (n // 14) + 8)


def shot(path):
    drive("shot", path, "0,0,275,400")
    from PIL import Image
    return Image.open(path).convert("RGB")


def clear(grey=False):
    """A fresh page, white or flooded with the grey."""
    drive("click", "150,12", "sleep", "300",
          "click", "%d,%d" % tool_point(6), "sleep", "250",
          "key", "KeyN:ControlLeft+ShiftLeft", "sleep", "600")
    if grey:
        drive("click", "%d,%d" % color_point(GREY_SWATCH), "sleep", "200",
              "click", "%d,%d" % tool_point(3), "sleep", "250",
              "click", "%d,%d" % AT, "sleep", "500")


def hotspot():
    """Where the pointer really lands, from the dot the pencil leaves."""
    clear()
    drive("click", "%d,%d" % tool_point(6), "sleep", "250",
          "click", "%d,%d" % AT, "sleep", "300",
          "move", "150,385", "sleep", "200", "park", "sleep", "300")
    im = shot("/tmp/cursor-calib.png")
    p = im.load()
    dots = [(x, y)
            for y in range(AT[1] - PAD, AT[1] + PAD)
            for x in range(AT[0] - PAD, AT[0] + PAD)
            if p[x, y] != (255, 255, 255)]
    if len(dots) != 1:
        print("calibration: %d dots, expected one" % len(dots))
        return AT
    return dots[0]


def capture(tool, reuse=False):
    path = "/tmp/cursor-%d.png" % tool
    if reuse and os.path.exists(path):
        from PIL import Image
        return Image.open(path).convert("RGB")
    clear(True)
    drive("click", "%d,%d" % tool_point(tool), "sleep", "300",
          "move", "%d,%d" % AT, "sleep", "500")
    return shot(path)


def main(argv):
    from PIL import Image
    reuse = "--reuse" in argv
    wanted = [int(a) for a in argv[1:] if not a.startswith("--")] or TOOLS
    hot = hotspot()
    print("the pointer lands at %d,%d" % hot)
    art = {}
    for t in wanted:
        p = capture(t, reuse).load()
        px, odd = {}, 0
        for y in range(AT[1] - PAD, AT[1] + PAD):
            for x in range(AT[0] - PAD, AT[0] + PAD):
                c = p[x, y]
                if c == PAGE:
                    continue           # the page showing through
                elif c == INVERSE:
                    c = (0, 0, 0)      # inverts what is under it: drawn black
                elif c not in ((0, 0, 0), (255, 255, 255)):
                    odd += 1
                    continue
                px[(x - hot[0], y - hot[1])] = c
        art[t] = px
        xs = [k[0] for k in px] or [0]
        ys = [k[1] for k in px] or [0]
        print("tool %-2d %4d pixels, %d,%d to %d,%d%s" %
              (t, len(px), min(xs), min(ys), max(xs), max(ys),
               ", %d not of the four colours" % odd if odd else ""))

    # one cell per tool, all the same size, each with its hot spot in the
    # same place so the generated art needs only one number
    left = min(min((k[0] for k in a), default=0) for a in art.values())
    top = min(min((k[1] for k in a), default=0) for a in art.values())
    right = max(max((k[0] for k in a), default=0) for a in art.values())
    bottom = max(max((k[1] for k in a), default=0) for a in art.values())
    cw, ch = right - left + 1, bottom - top + 1
    # A cursor is thirty-two square on Windows and CreateCursor is entitled
    # to insist on it, so the cell is padded out to that with nothing in it.
    px_left = max(0, (32 - cw + 1) // 2)
    px_top = max(0, (32 - ch + 1) // 2)
    cw, ch = max(cw, 32), max(ch, 32)
    hx, hy = -left + px_left, -top + px_top
    print("cell %dx%d, hot spot at %d,%d" % (cw, ch, hx, hy))
    out = Image.new("RGBA", (cw * len(wanted), ch), (0, 0, 0, 0))
    for i, t in enumerate(wanted):
        for (dx, dy), c in art[t].items():
            out.putpixel((i * cw + dx + hx, dy + hy), c + (255,))
    path = os.path.join(ROOT, "assets/paint-cursors.png")
    out.save(path)
    print("wrote %s (%d cells)" % (path, len(wanted)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
