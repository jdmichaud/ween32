#!/usr/bin/env python3
"""Paint reads and writes two formats now. Does each one survive the trip?

The .png half is new, and the gates only say that Paint *compiles* -- against
win32 and against ween32 -- which is silent about whether a picture that goes
out comes back. Nothing else here opens a file Paint wrote.

What it asks, and each one can fail on its own:

    a .png saved is a .png          the magic bytes, read by something else
    a .bmp saved is still a .bmp    the format that already worked
    the name decides which          the same picture, two suffixes, two formats
    a .png reopens as itself        Paint's own writer against Paint's reader
    a .png with no suffix opens     the sniffing path file.zig claims to have
    a file that is neither is refused   the control: the checks above can fail

**The last one is the reason to trust the rest.** Every assertion here is of
the form "Paint produced something", and a check of that shape passes for a
program that writes the same file whatever it is asked -- so one case has to
produce nothing, and it is the one where the input is not a picture at all.

The comparison is by size and by format, not pixel for pixel: libpng
composites what was transparent in linear light and a straight blend does
not, so a semi-transparent source is not expected back byte for byte. What is
expected back is a picture of the same shape in the format the name asked
for, reopened by Paint itself without complaint.

    tools/paint/pictures.py
    tools/paint/pictures.py --keep      leave what it wrote in /tmp

Exit status is 1 if any answer is wrong.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PAINT = os.path.join(ROOT, "zig-out/bin/paint")

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
BMP_MAGIC = b"BM"


def run(script, argv_path=None, timeout=90):
    """Drive Paint once, headless. Answers its exit status."""
    env = dict(os.environ)
    env.update(WEEN32_HEADLESS="1", WEEN32_DPI="96", WEEN32_SCRIPT=script)
    cmd = [PAINT] + ([argv_path] if argv_path else [])
    p = subprocess.run(cmd, env=env, capture_output=True, timeout=timeout)
    return p.returncode


def save_as(source, out_path):
    """Open `source`, then File > Save As to `out_path`.

    Alt+F opens the menu, `a` is Save &As's own letter, and the name is typed
    into the box. There is no other way in: the dialog is the only route
    Paint has to a chosen name, which is the point of driving it this way
    rather than calling the writer.
    """
    # **Every underscore in the path is doubled, because `_` is the script's
    # space.** `mkdtemp` puts one in its random name about a third of the
    # time, so a third of these runs typed a *different* path: Paint wrote a
    # real picture, returned 0, and the check then looked for the name it had
    # asked for and said "Paint could not write the seed .bmp" -- a true
    # sentence about the wrong file. I read the intermittency as a race and
    # it was nothing of the kind; longer waits did nothing because there was
    # nothing slow, and frame capture appeared to fix it because the name is
    # drawn afresh each run.
    #
    # **This is the second instrument to be caught by it**, and the escape it
    # needs was added to `headless.c` in the same change as this file.
    script = ("w:900 a:70 w:500 t:a w:600 t:%s w:500 k:13 w:900"
              % out_path.replace("_", "__"))
    return run(script, source)


def magic(path, n=8):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        return f.read(n)


def png_size(path):
    """Width and height out of the IHDR, read here rather than by a library:
    a reader of our own is what makes this an independent check."""
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != PNG_MAGIC or head[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", head[16:24])


def bmp_size(path):
    with open(path, "rb") as f:
        head = f.read(26)
    if head[:2] != BMP_MAGIC:
        return None
    w, h = struct.unpack("<ii", head[18:26])
    return (w, abs(h))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--keep", action="store_true",
                    help="leave what it wrote where it can be looked at")
    ap.add_argument("--paint", default=PAINT)
    args = ap.parse_args()
    if not os.path.exists(args.paint):
        sys.exit("no paint at %s -- build it first (zig build paint)" % args.paint)

    tmp = tempfile.mkdtemp(prefix="pictures-")
    bad = 0

    def say(ok, name, detail=""):
        nonlocal bad
        bad += not ok
        print("  %-4s %-46s %s" % ("ok" if ok else "FAIL", name, detail))

    # A picture of Paint's own making is the source, so the test does not
    # depend on a file that happens to be lying about: a blank canvas saved
    # as .bmp is the one picture Paint can always produce.
    seed = os.path.join(tmp, "seed.bmp")
    save_as(None, seed)
    if magic(seed, 2) != BMP_MAGIC:
        sys.exit("Paint could not write the seed .bmp -- nothing below can mean anything")
    seed_size = bmp_size(seed)
    print("seed: a blank canvas saved as .bmp, %dx%d" % seed_size)
    print()

    print("the name decides the format")
    as_png = os.path.join(tmp, "out.png")
    save_as(seed, as_png)
    say(magic(as_png) == PNG_MAGIC, "a name ending .png is written as a PNG",
        str(magic(as_png)[:4]) if magic(as_png) else "no file")
    as_bmp = os.path.join(tmp, "out.bmp")
    save_as(seed, as_bmp)
    say(magic(as_bmp, 2) == BMP_MAGIC, "a name ending .bmp is still a BMP")

    print()
    print("the picture survives the trip")
    got = png_size(as_png) if magic(as_png) == PNG_MAGIC else None
    say(got == seed_size, "the PNG has the picture's own size",
        "%s against %s" % (got, seed_size))
    again = os.path.join(tmp, "again.png")
    save_as(as_png, again)
    say(png_size(again) == seed_size,
        "and Paint reopens its own PNG and writes it back",
        str(png_size(again)))

    print()
    print("a name that does not say")
    unnamed = os.path.join(tmp, "nosuffix")
    shutil.copyfile(as_png, unnamed)
    sniffed = os.path.join(tmp, "sniffed.png")
    save_as(unnamed, sniffed)
    say(png_size(sniffed) == seed_size,
        "a PNG under a name with no suffix still opens",
        str(png_size(sniffed)))

    print()
    print("the control -- these checks must be able to fail")
    junk = os.path.join(tmp, "junk.png")
    with open(junk, "wb") as f:
        f.write(b"this is not a picture, whatever the name says\n" * 4)
    refused = os.path.join(tmp, "refused.png")
    save_as(junk, refused)
    # **Nothing is written, and that is the whole point of this row.** Paint
    # refuses the junk with "The picture could not be opened", and the box it
    # puts up takes the rest of the script -- so the Save As never runs. The
    # check above it would pass for a program that wrote a PNG whatever it
    # was handed; this one says the pipeline can produce nothing, which is
    # what makes the five above worth reading.
    say(not os.path.exists(refused),
        "a file that is not a picture is refused, and nothing saved",
        "wrote nothing" if not os.path.exists(refused)
        else "wrote %s" % (png_size(refused),))

    if args.keep:
        print("\nleft in %s" % tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    print()
    if bad:
        print("FAILED %d" % bad)
        return 1
    print("ok   both formats go out and come back")
    return 0


if __name__ == "__main__":
    sys.exit(main())
