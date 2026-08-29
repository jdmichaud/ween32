#!/usr/bin/env python3
"""The list view's rename box, measured as a set of relations rather than a
picture.

`docs/testing.md` described this box in prose for weeks and said so plainly:
*"there is nothing to diff it against ... it is the one thing in this section
that is believed rather than checked."* The box is not a window of its own, so
`pickshot.py` has nothing to pick, and every frame the run leaves is the
explorer's own size. A stored PNG would not have fixed that on its own --
**what was missing was not a reference but a way to make the comparison
again**, which is this file.

It takes two captures of the same rectangle:

    before   the row picked, so the highlight *is* the label rect
    after    F2 pressed, so the rename box is up

and reads seven relations off them. None of the seven mentions a screen
coordinate, so **the machine's captures and ours can be measured by the same
instrument at whatever size either window happens to be**:

    tools/refcapture/renamebox.py before.png after.png

The seven, and where they came from:

    the box is two pixels left of the label
    and twelve wider
    and exactly as tall as the row
    its top sits on the label's top
    the selection band inside it is thirteen rows tall
    two below the top of the box
    and the name lands one pixel right of where the row drew it

Measured on Windows 2000 on 2026-08-29 for `CONFIG.SYS` (an 81x17 box over a
69-wide label) and `Program Files` (84 over 72). All seven hold on both, which
is what makes them relations and not two coincidences: **a rule that has been
seen once is a number with a story attached.**

**Take the capture with the pointer somewhere else.** The first machine
capture had the box's top border broken by seven pixels -- `black black black
white white white black white white white black`, which reads exactly like a
gap in the art. It was the mouse I-beam: the row had been picked by clicking
it, the I-beam's top serif sits eight rows above the hotspot, and eight rows
above the click is the row the border is drawn on. Moved to the corner, the
border is one unbroken run.
"""
import sys

FACE = (212, 208, 200)
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
HIGHLIGHT = (10, 36, 106)  # COLOR_HIGHLIGHT, which is what a selection is


def load(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    return im.load(), im.size


def bbox(px, size, colour, x0=0, y0=0, x1=None, y1=None):
    w, h = size
    x1 = w - 1 if x1 is None else min(x1, w - 1)
    y1 = h - 1 if y1 is None else min(y1, h - 1)
    pts = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
           if px[x, y] == colour]
    if not pts:
        return None
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def black_runs(px, size, least):
    """Horizontal runs of black at least `least` long, by row."""
    w, h = size
    out = {}
    for y in range(h):
        runs, start = [], None
        for x in range(w + 1):
            on = x < w and px[x, y] == BLACK
            if on and start is None:
                start = x
            elif not on and start is not None:
                if x - start >= least:
                    runs.append((start, x - 1))
                start = None
        if runs:
            out[y] = runs
    return out


def find_box(px, size):
    """The box's rectangle, found by its own borders and its own contents.

    **The box is looked for first and the label second**, which is the other
    way round from how a person describes them. Doing it the natural way needs
    somewhere to start looking, and the only landmark available is the label
    -- whose distance from the box is the thing being measured. So the box has
    to be found without it.

    What identifies it: a rectangle whose top and bottom edges are *the same
    horizontal run*, one row tall enough to hold a name, **with a selection
    inside it**. An icon, a letter or a column rule does not come in matched
    pairs like that, and the other rectangles that do -- a button, a sunken
    pane -- have no highlight in them. That last clause is what lets this run
    on a whole 654x544 frame instead of a hand-cut strip, and a whole frame is
    what our own renders come as.
    """
    runs = black_runs(px, size, 20)
    best = None
    for top, top_runs in runs.items():
        for r in top_runs:
            for bottom, bottom_runs in runs.items():
                if not 10 <= bottom - top <= 30 or r not in bottom_runs:
                    continue
                inside = bbox(px, size, HIGHLIGHT,
                              r[0] + 1, top + 1, r[1] - 1, bottom - 1)
                if inside is None:
                    continue
                cand = (r[0], top, r[1], bottom)
                if best is None or (cand[2] - cand[0]) > (best[2] - best[0]):
                    best = cand
    return best


def glyph_left(px, rect):
    """The first column of a letter: white, the text being drawn reversed."""
    for x in range(rect[0], rect[2] + 1):
        for y in range(rect[1], rect[3] + 1):
            if px[x, y] == WHITE:
                return x
    return None


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[0] + "\n\n"
                 "    renamebox.py <row-picked.png> <box-open.png>")
    before, bsize = load(sys.argv[1])
    after, asize = load(sys.argv[2])
    if bsize != asize:
        sys.exit("the two captures are %dx%d and %dx%d -- they must be the "
                 "same rectangle, or none of the relations mean anything"
                 % (bsize + asize))

    box = find_box(after, asize)
    if box is None:
        sys.exit("no rename box in %s: expected a black rectangle, one row "
                 "tall, with the name picked out inside it" % sys.argv[2])
    band = bbox(after, asize, HIGHLIGHT,
                box[0] + 1, box[1] + 1, box[2] - 1, box[3] - 1)
    # **The label is looked for on the box's own rows**, so that a highlight
    # elsewhere in the frame -- the tree pane's picked folder, a selected
    # address -- cannot be mistaken for it. On a whole frame the unrestricted
    # bounding box came out 293 wide and swallowed half the window.
    label = bbox(before, bsize, HIGHLIGHT, 0, box[1], bsize[0] - 1, box[3])
    if label is None:
        sys.exit("no highlight on rows %d..%d of %s: the row has to be picked "
                 "in the first capture, so that the highlight *is* the label "
                 "rect" % (box[1], box[3], sys.argv[1]))

    def w(r):
        return r[2] - r[0] + 1

    def h(r):
        return r[3] - r[1] + 1

    print("  label rect   x %d..%d y %d..%d   %dx%d"
          % (label[0], label[2], label[1], label[3], w(label), h(label)))
    print("  rename box   x %d..%d y %d..%d   %dx%d"
          % (box[0], box[2], box[1], box[3], w(box), h(box)))
    print("  the band     x %d..%d y %d..%d   %dx%d"
          % (band[0], band[2], band[1], band[3], w(band), h(band)))
    print()

    checks = [
        ("the box is two pixels left of the label", label[0] - box[0], 2),
        ("and twelve wider", w(box) - w(label), 12),
        ("and exactly as tall as the row", h(box), h(label)),
        ("its top sits on the label's top", box[1] - label[1], 0),
        ("the band is thirteen rows tall", h(band), 13),
        ("two below the top of the box", band[1] - box[1], 2),
        ("the name lands one pixel right",
         glyph_left(after, band) - glyph_left(before, label), 1),
    ]
    bad = 0
    for what, got, want in checks:
        ok = got == want
        bad += not ok
        print("  %-42s %-3s (%s)" % (what, "yes" if ok else "NO", got))
    print()
    print("%s" % ("all %d" % len(checks) if not bad
                  else "%d of %d failed" % (bad, len(checks))))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
