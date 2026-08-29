#!/usr/bin/env python3
"""Pick the frame a headless run meant, out of the several it left behind.

Every `WEEN32_SCRIPT` run writes a file per paint -- a menu, a tooltip and a
modal dialog are windows of their own -- so a recipe that says
`WEEN32_BMP=/tmp/x%d.bmp` and then diffs one file has to say *which*. Picking
by index is how that gets written and it is wrong the moment anything else
paints; picking by **size** is what `tools/verify.sh` has always done, and
this is that, out where a recipe can use it.

    tools/refcapture/pickshot.py '/tmp/pp*.bmp' <reference.png> <out.png>

The last frame whose size is the reference's own, converted to PNG. **The
last** and not the first: a dialog paints when it opens and again when
something in it changes, and the finished one is what a reference is of.

Exits 1 and says so when there is no such frame, rather than leaving whatever
was there before -- which is the fault this was written for. A recipe in
`docs/testing.md` rendered to `/tmp/pp%d.bmp` and diffed `/tmp/ours.png`, a
file nothing in it created, so it compared against whatever an earlier recipe
had left in `/tmp` and produced a plausible number.
"""
import glob
import re
import sys

if len(sys.argv) != 4:
    sys.exit(__doc__.strip().split("\n\n")[2].strip())

from PIL import Image

pattern, reference, out = sys.argv[1], sys.argv[2], sys.argv[3]
want = Image.open(reference).size


def index(path):
    m = re.search(r"(\d+)\.bmp$", path)
    return int(m.group(1)) if m else -1


frames = sorted(glob.glob(pattern), key=index)
if not frames:
    sys.exit("pickshot: nothing matched %s -- did the program run?" % pattern)
same = [f for f in frames if Image.open(f).size == want]
if not same:
    sizes = sorted({Image.open(f).size for f in frames})
    sys.exit("pickshot: no %dx%d frame among %d; sizes were %s"
             % (want[0], want[1], len(frames),
                ", ".join("%dx%d" % s for s in sizes)))
Image.open(same[-1]).convert("RGB").save(out)
