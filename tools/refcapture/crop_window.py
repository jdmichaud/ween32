#!/usr/bin/env python3
"""Crop the sampler's window out of a wine virtual-desktop screenshot.

Trimming the uniform background is enough only while wine's desktop window is
exactly as big as we asked for. A window manager that resizes it gives it
chrome of its own — a title bar, scroll bars, and padding around the virtual
desktop — and that chrome reaches the edges of the shot, so there is no
uniform border left to trim.

What is always true is that the sampler's window floats: it does not touch any
edge of the desktop it sits on. So take everything that is not the background,
discard whatever is connected to the border, and what is left is the window.
If nothing is left, the shot has chrome of its own: crop to the desktop inside
it — the largest expanse that is not the border colour — and look again.

    crop_window.py desktop.png out.png
"""

import sys
from collections import Counter, deque

from PIL import Image


def edge_colour(px, w, h):
    """Whatever covers most of the border."""
    seen = Counter()
    for x in range(0, w, 4):
        seen[px[x, 0]] += 1
        seen[px[x, h - 1]] += 1
    for y in range(0, h, 4):
        seen[px[0, y]] += 1
        seen[px[w - 1, y]] += 1
    return seen.most_common(1)[0][0]


def near(a, b):
    return all(abs(p - q) <= 6 for p, q in zip(a, b))


def floating_bbox(px, w, h, background):
    """The bbox of everything that is neither background nor reachable from
    the border through non-background pixels. None if there is nothing."""
    outside = bytearray(w * h)
    queue = deque()

    def push(x, y):
        if not outside[y * w + x] and not near(px[x, y], background):
            outside[y * w + x] = 1
            queue.append((x, y))

    for x in range(w):
        push(x, 0)
        push(x, h - 1)
    for y in range(h):
        push(0, y)
        push(w - 1, y)
    while queue:
        x, y = queue.popleft()
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h:
                push(nx, ny)

    left, top, right, bottom = w, h, -1, -1
    for y in range(h):
        row = y * w
        for x in range(w):
            if outside[row + x] or near(px[x, y], background):
                continue
            left = min(left, x)
            right = max(right, x)
            top = min(top, y)
            bottom = max(bottom, y)
    return None if right < 0 else (left, top, right + 1, bottom + 1)


def desktop_bbox(px, w, h, border):
    """The largest expanse that is not the border colour: wine's virtual
    desktop, sitting inside the chrome the window manager forced on it."""
    seen = Counter()
    for x in range(0, w, 3):
        for y in range(0, h, 3):
            c = px[x, y]
            if not near(c, border):
                seen[c] += 1
    if not seen:
        return None
    inner = seen.most_common(1)[0][0]
    left, top, right, bottom = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            if near(px[x, y], inner):
                left = min(left, x)
                right = max(right, x)
                top = min(top, y)
                bottom = max(bottom, y)
    return None if right < 0 else (left, top, right + 1, bottom + 1)


def main(src, dst):
    im = Image.open(src).convert("RGB")
    w, h = im.size
    px = im.load()

    border = edge_colour(px, w, h)
    box = floating_bbox(px, w, h, border)
    if box is None:
        inner = desktop_bbox(px, w, h, border)
        if inner is None:
            raise SystemExit("no desktop found in that shot")
        im = im.crop(inner)
        w, h = im.size
        px = im.load()
        box = floating_bbox(px, w, h, edge_colour(px, w, h))
        if box is None:
            raise SystemExit("no window found on that desktop")

    im.crop(box).save(dst)
    print(f"{dst}: {box[2] - box[0]} x {box[3] - box[1]}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
