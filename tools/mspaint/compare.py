#!/usr/bin/env python3
"""Draw the same thing on the machine and in ours, and compare the pixels.

The machine is driven by a synthetic mouse whose pointer is *relative*: ask
for (120,160) and the guest's pointer may end up at (120,159), consistently
and for reasons of its own. That one pixel is not a difference in what Paint
draws, but it moves every pixel of the shape, so a straight comparison of two
renders driven from the same numbers is worthless.

So the numbers are calibrated first. With the pencil, a click at each point
of the gesture leaves one dot; where the dots land is where the pointer
really goes, and *those* coordinates are what our own copy is driven with.
After that the two are drawing the same shape from the same corners, and any
difference left is ours.

    tools/mspaint/compare.py "tool 12" "drag 76,57 126,87"
    tools/mspaint/compare.py --keep "tool 10" "drag 80,120 120,159"

Steps: `tool N` picks the Nth tool button, `option N` the Nth setting,
`click X,Y`, `drag X,Y X,Y ...`, `key NAME`.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DRIVE = os.path.join(ROOT, "tools/vm/drive.py")
PAINT = os.path.join(ROOT, "zig-out/bin/mspaint")

# The window is at (0,0) on the machine's screen, so window coordinates and
# screen coordinates are the same thing.
# The paper itself, not the view: the three pixels of grey above and
# left of it, and the handles in them, are not ink.
VIEW = (66, 47, 187, 259)


def tool_point(n):
    return (8 + (n % 2) * 25 + 12, 42 + (n // 2) * 25 + 12)


# Which settings each tool offers, and where they sit — read out of the
# generated art so the two cannot drift apart.
GROUP_OF = {
    0: "select", 1: "select", 9: "select",
    2: "eraser", 5: "zoom", 7: "brush", 8: "airbrush",
    10: "line", 11: "line",
    12: "shape", 13: "shape", 14: "shape", 15: "shape",
}


def option_rects():
    import re

    text = open(os.path.join(ROOT, "examples/mspaint/art_options.zig")).read()
    out, name = {}, None
    for line in text.splitlines():
        m = re.match(r"pub const (\w+) = \[_\]Option\{", line)
        if m:
            name = m.group(1)
            out[name] = []
        m = re.match(r"\s*\.x = (-?\d+), \.y = (-?\d+), \.w = (\d+), \.h = (\d+),", line)
        if m and name:
            out[name].append(tuple(int(v) for v in m.groups()))
    return out


def option_point(tool, index):
    """The middle of that setting's rectangle, in window coordinates: the
    settings box sits at (12,245) and both copies put it there."""
    group = GROUP_OF.get(tool)
    if not group:
        return None
    rects = option_rects().get(group, [])
    if index >= len(rects):
        return None
    x, y, w_, h_ = rects[index]
    return (12 + x + w_ // 2, 245 + y + h_ // 2)


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)


def drive(*args):
    return run([sys.executable, DRIVE] + list(args))


def grab(path):
    drive("park", "sleep", "400", "shot", path, "0,0,275,400")
    from PIL import Image

    return Image.open(path).convert("RGB")


def ink(im, bg=(255, 255, 255)):
    """Every pixel of the canvas that is not the paper."""
    x0, y0, w, h = VIEW
    p = im.load()
    return {(x, y): p[x, y] for y in range(y0, y0 + h) for x in range(x0, x0 + w)
            if p[x, y] != bg}


def clear_machine():
    # the caption, not the canvas: a click on the canvas would draw
    drive("click", "150,12", "sleep", "300", "key", "KeyN:ControlLeft+ShiftLeft",
          "sleep", "600")


def calibrate(points):
    """Where the pointer really lands for each of these, found by leaving a
    pencil dot at each and reading them back."""
    clear_machine()
    args = ["click", "%d,%d" % tool_point(6), "sleep", "400"]  # the pencil
    for x, y in points:
        args += ["click", "%d,%d" % (x, y), "sleep", "250"]
    drive(*args)
    im = grab("/tmp/paint-calib.png")
    dots = sorted(ink(im).keys(), key=lambda p: (p[1], p[0]))
    if len(dots) != len(points):
        print("calibration: asked for %d dots, found %d" % (len(points), len(dots)))
        return points
    # match each dot to the point it is nearest to
    out = []
    for p in points:
        best = min(dots, key=lambda d: (d[0] - p[0]) ** 2 + (d[1] - p[1]) ** 2)
        out.append(best)
    return out


def parse(steps):
    """The gesture as a list of (kind, points)."""
    out = []
    for s in steps:
        parts = s.split()
        kind = parts[0]
        if kind in ("tool", "option"):
            out.append((kind, [int(parts[1])]))
        elif kind == "key":
            out.append((kind, parts[1:]))
        else:
            out.append((kind, [tuple(int(v) for v in p.split(",")) for p in parts[1:]]))
    return out


def main(argv):
    keep = "--keep" in argv
    steps = parse([a for a in argv if not a.startswith("--")])

    # every point the gesture touches, so they can all be calibrated at once
    pts = []
    for kind, args in steps:
        if kind in ("click", "drag"):
            pts += args
    tool_now = 6
    fixed = calibrate(pts) if pts else []
    real = dict(zip(pts, fixed))
    if pts:
        moved = [(a, b) for a, b in zip(pts, fixed) if a != b]
        if moved:
            print("calibrated: " + ", ".join("%s->%s" % m for m in moved))

    # the machine
    clear_machine()
    args = []
    for kind, a in steps:
        if kind == "tool":
            tool_now = a[0]
            args += ["click", "%d,%d" % tool_point(a[0]), "sleep", "400"]
        elif kind == "option":
            p = option_point(tool_now, a[0])
            if p:
                args += ["click", "%d,%d" % p, "sleep", "300"]
        elif kind == "click":
            args += ["click", "%d,%d" % a[0], "sleep", "300"]
        elif kind == "drag":
            args += ["drag"] + ["%d,%d" % p for p in a] + ["sleep", "400"]
        elif kind == "key":
            args += ["key", a[0], "sleep", "400"]
    drive(*args)
    theirs = grab("/tmp/paint-machine.png")

    # ours, driven with where the pointer really went
    script = []
    tool_now = 6
    for kind, a in steps:
        if kind == "tool":
            tool_now = a[0]
            x, y = tool_point(a[0])
            script += ["d:%d,%d" % (x, y), "u:%d,%d" % (x, y), "w:50"]
        elif kind == "option":
            p = option_point(tool_now, a[0])
            if p:
                script += ["d:%d,%d" % p, "u:%d,%d" % p, "w:50"]
        elif kind == "click":
            x, y = real.get(a[0], a[0])
            script += ["d:%d,%d" % (x, y), "u:%d,%d" % (x, y), "w:50"]
        elif kind == "drag":
            ps = [real.get(p, p) for p in a]
            script.append("d:%d,%d" % ps[0])
            for p in ps[1:]:
                script.append("m:%d,%d" % p)
            script += ["u:%d,%d" % ps[-1], "w:50"]
        elif kind == "key":
            script.append("k:%s" % a[0])
    env = dict(os.environ, WEEN32_HEADLESS="1", WEEN32_DPI="96",
               WEEN32_BMP="/tmp/paint-ours.bmp", WEEN32_SCRIPT=" ".join(script))
    subprocess.run([PAINT], env=env, cwd=ROOT)

    from PIL import Image

    ours = Image.open("/tmp/paint-ours.bmp").convert("RGB")
    a, b = theirs.load(), ours.load()
    x0, y0, ww, hh = VIEW
    diff = [(x, y) for y in range(y0, y0 + hh) for x in range(x0, x0 + ww)
            if a[x, y] != b[x, y]]
    print("canvas: %d differing pixels of %d" % (len(diff), ww * hh))
    if diff:
        xs = [p[0] for p in diff]
        ys = [p[1] for p in diff]
        print("        between %d,%d and %d,%d" % (min(xs), min(ys), max(xs), max(ys)))
        print("        first few: " + ", ".join(
            "%s ref=%s ours=%s" % (p, a[p[0], p[1]], b[p[0], p[1]]) for p in diff[:4]))
    if keep:
        theirs.crop((61, 42, 271, 324)).save("/tmp/cmp-machine.png")
        ours.crop((61, 42, 271, 324)).save("/tmp/cmp-ours.png")
        print("        wrote /tmp/cmp-machine.png and /tmp/cmp-ours.png")
    return 0 if not diff else 1


sys.exit(main(sys.argv[1:]))
