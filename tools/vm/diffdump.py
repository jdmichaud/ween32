#!/usr/bin/env python3
"""Compare two document dumps -- ours and the machine's -- field by field.

    tools/vm/diffdump.py ours.txt machine.txt [--sequence '<seq>']

**A difference is a finding, not a bug.** ween32 has two strikes and the
machine has seven faces, so some lines will always differ. The ones we have
measured are in `known-differences.md` and are matched here **by shape** --
and they are *reported*, not hidden:

    same          identical, nothing to say
    KNOWN         differs, and it is on the list, with the entry named
    NEW           differs, and nobody has written down why      <- the finding

**Nothing is suppressed.** alice's rule for the list is that an entry is
added by a measurement and never to make a run green; the same rule applies
here, so a known difference still prints. A run that says `KNOWN x14` is a
run somebody can read; a run that silently says nothing is one where the
fourteen could have become fifteen without anybody noticing.

Exit 1 if anything is NEW, 0 otherwise -- so it can go in a roll call.
"""
import argparse
import re
import sys

# Each entry: a name, and a predicate over (field, ours, theirs). The names
# are the headings in known-differences.md, so a report points at prose that
# says what was measured and where.
def _len_mark(field, a, b):
    """1. The trailing paragraph mark, and therefore `len`."""
    if field == "len":
        try:
            return int(b) == int(a) + 1
        except ValueError:
            return False
    if field.startswith("char ") or field.startswith("para "):
        # **`para` as well as `char`.** The first version matched only the
        # character runs, so every scenario reported its paragraph line as a
        # NEW finding -- the trailing mark showing up a second time under a
        # different name. Seven scenarios, seven false findings, all of them
        # the one difference we already understood.
        ta, tb = _tail_index(a), _tail_index(b)
        if ta is None or tb is None:
            return False
        if tb != ta + 1:
            return False
        # and nothing else on the line may differ
        return a.split(None, 1)[1:] == b.split(None, 1)[1:]
    return False


def _tail_index(line):
    m = re.match(r"^\s*\d+\.\.(\d+)", line)
    return int(m.group(1)) if m else None


def _face(field, a, b):
    """2 and 3. The default face, and which faces exist.

    A face difference carries a size with it -- `Arial 200` against `System
    180` -- and the System font is bold, so the effects can differ too. The
    first version compared token by token and missed exactly that, calling
    the commonest expected difference in the whole comparison NEW.

    So: same *shape* of line, and the face token differs. Everything that
    travels with a face is part of the same known difference; anything else
    is not."""
    if not (field.startswith("char ") or field == "next"):
        return False
    fa, fb = a.split(), b.split()
    if len(fa) < 3 or len(fb) < 3:
        return False
    # a char line begins with its range; `next` does not
    off = 1 if field.startswith("char ") else 0
    face_a = " ".join(fa[off + 1:-1])
    face_b = " ".join(fb[off + 1:-1])
    return face_a != face_b


KNOWN = [
    ("1. the trailing paragraph mark", _len_mark),
    ("2/3. the face", _face),
]


def read(path):
    """A dump as an ordered list of (key, rest). The key is the field name
    plus, for the indexed lines, its range -- so `char 0..2` and `char 0..3`
    are compared as the same field rather than as an insertion."""
    out = []
    for line in open(path):
        line = line.rstrip("\n")
        if not line.strip():
            continue
        head = line.split(None, 1)
        key = head[0]
        rest = head[1] if len(head) > 1 else ""
        if key in ("char", "para", "line"):
            bits = rest.split(None, 1)
            # **Keyed by where the run starts, not by its whole range.** The
            # first version keyed on `0..2` against `0..3`, so a run whose
            # *end* differed looked like one field missing on each side and
            # printed as two NEW lines -- which is the trailing paragraph
            # mark showing up three times instead of once, in the exact
            # shape most likely to make somebody weaken the comparison.
            start = bits[0].split("..")[0]
            key = "%s %s" % (key, start)
            rest = (bits[0] + (" " + bits[1] if len(bits) > 1 else ""))
        out.append((key, rest))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("theirs")
    ap.add_argument("--sequence", default=None,
                    help="printed with the report, so a finding is runnable")
    args = ap.parse_args()

    a, b = read(args.ours), read(args.theirs)

    # **An empty dump is not a document, and it must not read as findings.**
    # A dump has a `len` line unconditionally, so a file without one did not
    # come from the serialiser -- the replay binary failed to build, or the
    # probe never ran, or the redirect went somewhere else.
    #
    # It matters because of what it looks like: every field missing on one
    # side is reported as a difference, so an empty file comes out as **"7
    # new, 0 known"** across every scenario -- a full-looking table of
    # findings, identical in shape to a real one. I produced exactly that
    # three times today, twice from `make` leaving no binary behind after
    # verify.sh had rebuilt the library with X11, and read it as seven
    # divergences each time before checking.
    #
    # This is the same guard `tools/build.py` puts in front of the wordpad
    # instruments -- *refuse rather than warn* -- and the differ was the one
    # instrument in either repository that did not have it.
    for path, dump in ((args.ours, a), (args.theirs, b)):
        if not any(k == "len" for k, _ in dump):
            sys.exit("%s has no `len` line, so it is not a dump: the program "
                     "that should have written it did not run.\n"
                     "Refusing to compare -- an empty file reports as a "
                     "difference in every field, which reads exactly like a "
                     "finding." % path)
    da, db = dict(a), dict(b)
    keys = [k for k, _ in a] + [k for k, _ in b if k not in da]

    known, new, seen = [], [], set()
    for k in keys:
        if k in seen:
            continue
        seen.add(k)
        va, vb = da.get(k), db.get(k)
        if va == vb:
            continue
        why = None
        if va is not None and vb is not None:
            for name, pred in KNOWN:
                try:
                    if pred(k, va, vb):
                        why = name
                        break
                except Exception:
                    pass
        (known if why else new).append((k, va, vb, why))

    if args.sequence:
        print("sequence  %s" % args.sequence)
    print("%-16s %-28s %s" % ("field", "ours", "the machine"))
    for k, va, vb, why in new:
        print("NEW   %-10s %-28s %s" % (k, va, vb))
    for k, va, vb, why in known:
        print("KNOWN %-10s %-28s %-20s  (%s)" % (k, va, vb, why))
    print()
    print("%d new, %d known" % (len(new), len(known)))
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
