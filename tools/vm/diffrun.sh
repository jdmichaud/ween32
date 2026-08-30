#!/bin/sh
# Run every differential scenario that has a machine half, and say so about
# the ones that do not.
#
# **This existed as a shell loop typed by hand, which is why it exists now.**
# Every "0 new, 3 known" reported on the channel tonight came out of a loop
# nobody else could run and no sha could reproduce -- the one instrument whose
# output was being quoted as a result was the one instrument not in the
# repository. verify.sh does not run it either, because the machine half is
# a set of committed files rather than something the tree can produce.
#
# It also owns the build, and that is not tidiness. `make tests/replay_test
# X11=0` after verify.sh has built the library with X11 fails to link, leaves
# no binary, and `>` then writes an empty file -- which reads as a difference
# in every field. That cost about an hour across five occurrences before
# diffdump.py grew its empty-dump refusal, and the refusal is a guard rather
# than a fix: the fix is not to half-build in the first place.
#
#     tools/vm/diffrun.sh          exit 1 if any scenario has a NEW difference
#
# **It exits 1 today, and it is meant to.** 06 breaks at 44 where the machine
# breaks at 40, which is measured to be glyph widths -- ours 245px of text in
# a 246px column, the machine near 253 -- and deliberately has no
# known-differences entry. Entry 6 said that exact thing twice without
# measuring it and was deleted for it, so this reports rather than gates: a
# red line everyone understands beats a green one nobody rechecks. If somebody
# wires it into verify.sh, that is the decision being made.
set -e
cd "$(dirname "$0")/../.."

make clean >/dev/null 2>&1 || true
make libween32.a X11=0 >/dev/null
make tests/replay_test X11=0 >/dev/null

status=0
unpaired=
orphans=

for f in tools/vm/seq/[0-9]*.txt; do
    n=$(basename "$f" .txt)
    m="tools/vm/seq/machine/$n.txt"
    if [ ! -f "$m" ]; then
        unpaired="$unpaired $n"
        continue
    fi
    seq=$(grep -v '^#' "$f" | tr -d '\n ')
    ./tests/replay_test "$seq" > "/tmp/diffrun-$n.txt" 2>/dev/null || true
    printf '%-28s ' "$n"
    # **The exit status is the differ's, not `tail`'s.** Written as
    # `if diffdump | tail -1` first, which reports a pipeline's last command
    # and so passed a run with two NEW differences in it -- a runner that
    # cannot fail, in a file whose whole purpose is to fail.
    if out=$(./tools/vm/diffdump.py "/tmp/diffrun-$n.txt" "$m"); then
        echo "$out" | tail -1
    else
        echo "$out" | tail -1
        status=1
    fi
done

# The reverse gap: a reading taken on a machine that nothing compares against.
for m in tools/vm/seq/machine/[0-9]*.txt; do
    n=$(basename "$m" .txt)
    [ -f "tools/vm/seq/$n.txt" ] || orphans="$orphans $n"
done

if [ -n "$orphans" ]; then
    echo
    echo "measured on a machine and compared against nothing:"
    for n in $orphans; do echo "  $n"; done
fi

if [ -n "$unpaired" ]; then
    echo
    echo "no machine half, so these say what ours does and not whether it agrees:"
    for n in $unpaired; do echo "  $n"; done
    echo "  -- take them with tools/vm/dump.h on the guest, next time one is up"
fi

exit $status
