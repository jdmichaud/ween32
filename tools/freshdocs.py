#!/usr/bin/env python3
"""Check every number a document quotes against the thing that produced it.

Four times in one day a sentence in `docs/testing.md` was wrong while every
number in it had been right when written: a band table that said 555 after it
had become 201, an attribution copied from a channel report that then aged,
three instrument counts copied from tools that then grew, and a count of
`ok` lines in ween32's own file that is now seventy-two behind. **None of
those was ever a guess.** A stale truth reads exactly like a live one, and
neither a suite nor a rendering instrument looks at prose.

So: **a document may say what, a tool may say how many** -- and where a
document must give a number, it quotes the tool verbatim and this re-runs the
tool and compares.

    tools/freshdocs.py                  # check
    tools/freshdocs.py --fix            # rewrite the stale blocks in place
    tools/freshdocs.py docs/testing.md ../ween32/docs/testing.md

**The convention is one line.** A fenced block whose language is `console`
and whose first line is `$ <command>` is owned by that command:

    ```console
    $ tools/bands.py
    band               row..row     differ       of
    ...
    ```

Everything after the `$` line is the command's own stdout and is compared with
it, character for character. A block with no `$` line is prose and is left
alone, so this can be added to a file one block at a time.

**What it cannot do**, and the reason it is not the whole answer: it checks
numbers a tool owns. §8.6's thirteen pixels were called "the mnemonic
underline under the w" for hours -- the count was right and the *noun* was
invented, and nothing mechanical will ever catch that. This closes three of
the four holes found today and names the fourth as the one still needing eyes.
"""
import argparse
import os
import re
import subprocess
import sys

# The command is the `$ ` line and any lines it continues onto with a
# trailing backslash, which is how every recipe in these documents is already
# written -- a one-line-only reader would have meant rewriting them all to be
# checkable, and a document that has to be reshaped to be checked will not be.
BLOCK = re.compile(
    r"^```console\n\$ ((?:[^\n]*\\\n)*[^\n]*)\n(.*?)^```$", re.M | re.S)
# A document that explains this convention contains an example of it, and an
# example is not a quote. Markdown's way of showing a fence is to wrap it in a
# longer one, so anything inside a ````-fenced region is left alone -- without
# this, §11's own illustration is checked as though it were a claim, which is
# exactly what happened the first time this was run.
EXAMPLE = re.compile(r"^````.*?^````$", re.M | re.S)


def run(cmd, cwd):
    """The command's stdout, with a trailing newline, whatever it exits."""
    p = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
    out = p.stdout
    if not out.endswith("\n"):
        out += "\n"
    return out, p.returncode


def check(path, fix):
    # A document's commands are relative to its own repository, not to wherever
    # this was invoked: `docs/testing.md` says `tools/bands.py` and means the
    # one beside it.
    root = os.path.dirname(os.path.dirname(os.path.abspath(path)))
    text = open(path, encoding="utf-8").read()
    # Blank out the illustrations, keeping the offsets so the rewrite below
    # still lines up with the real file.
    scan = EXAMPLE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    stale, checked = [], 0
    out = []
    at = 0
    for m in BLOCK.finditer(scan):
        cmd, quoted = m.group(1), m.group(2)
        checked += 1
        fresh, code = run(cmd, root)
        out.append(text[at:m.start()])
        if fresh == quoted:
            out.append(m.group(0))
        else:
            stale.append((cmd, quoted, fresh, code))
            out.append("```console\n$ %s\n%s```" % (cmd, fresh) if fix
                       else m.group(0))
        at = m.end()
    out.append(text[at:])

    print("%s: %d quoted block%s" % (path, checked, "" if checked == 1 else "s"))
    for cmd, quoted, fresh, code in stale:
        print("  STALE  $ %s%s" % (cmd.split("\n")[0],
                                   "   (exit %d)" % code if code else ""))
        q, f = quoted.rstrip("\n").split("\n"), fresh.rstrip("\n").split("\n")
        for i in range(max(len(q), len(f))):
            a = q[i] if i < len(q) else "(nothing)"
            b = f[i] if i < len(f) else "(nothing)"
            if a != b:
                print("           the document says  %s" % a)
                print("           the command says   %s" % b)
    if fix and stale:
        open(path, "w", encoding="utf-8").write("".join(out))
        print("  rewritten")
    return len(stale)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("docs", nargs="*", default=["docs/testing.md"])
    ap.add_argument("--fix", action="store_true",
                    help="rewrite each stale block with what the command says")
    args = ap.parse_args()
    bad = sum(check(d, args.fix) for d in args.docs if os.path.exists(d))
    if bad and not args.fix:
        print("\n%d block%s stale. `--fix` rewrites them; read the diff first, "
              "because a number that moved is a thing to explain and not only "
              "a thing to update." % (bad, "" if bad == 1 else "s"))
    return 1 if bad and not args.fix else 0


if __name__ == "__main__":
    sys.exit(main())
