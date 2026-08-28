#!/usr/bin/env python3
"""Check the Zig module's constants against the C header's.

The values are the thing that cannot be allowed to drift: a constant copied
wrong compiles, links and runs, and shows up as a message that never arrives.
tools/zigbind/genconsts.py derives the Zig values from the C header, and
`make win32` checks the C header against the real Windows SDK -- so a Zig
constant that agrees with the header agrees with Windows.

Every name the module declares *and* the header declares must agree. A name
the header has and the module has not is a gap rather than a fault: a Zig
program that reaches for it does not compile, which is a missing feature and
not a wrong answer, so it is counted and reported rather than failed on.

    tools/zigbind/checkconsts.py include/ween32.h zig/ween32.zig
"""
import re
import subprocess
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def main(header, module):
    gen = subprocess.run([sys.executable, os.path.join(HERE, "genconsts.py"),
                          header], capture_output=True, text=True, check=True)
    want = dict(re.findall(r"^pub const (\w+) = ([^;]+);", gen.stdout, re.M))
    have = dict(re.findall(r"^pub const (\w+)(?::\s*\w+)? = ([^;]+);",
                           open(module, encoding="utf-8").read(), re.M))
    # Compared as numbers, not as text: 0x00000001 and 1 are the same
    # constant written two ways, and a check that calls those a difference
    # cries wolf until nobody reads it. Anything that will not resolve to a
    # number -- an expression over other names -- falls back to comparing the
    # text, which is strict but never wrong.
    def value(v):
        v = v.strip().rstrip("uUlL")
        try:
            return int(v, 0)
        except ValueError:
            return None

    def same(a, b):
        va, vb = value(a), value(b)
        if va is not None and vb is not None:
            return va == vb
        return a.strip() == b.strip()

    shared = [k for k in want if k in have]
    wrong = [(k, want[k].strip(), have[k].strip())
             for k in shared if not same(want[k], have[k])]
    for k, w, h in wrong:
        print("checkconsts: %s is %s in the C header and %s in the Zig module"
              % (k, w, h), file=sys.stderr)
    if wrong:
        return 1
    print("/* %d constants compared against the C header, %d of the header's "
          "not declared in Zig yet */" % (len(shared), len(want) - len(shared)),
          file=sys.stderr)
    return 0


sys.exit(main(sys.argv[1], sys.argv[2]))
