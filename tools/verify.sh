#!/bin/bash
# Everything a branch has to pass, in one command, from a clean checkout.
#
#     tools/verify.sh              # this working tree, as it stands
#     tools/verify.sh <sha>        # a throwaway worktree at that commit
#     FAST=1 tools/verify.sh       # skip the sanitizer and the win32 gates
#
# It exists because the numbers below were being measured twice: once by
# whoever wrote the change and once by whoever reviewed it, from recipes
# copied out of docs/testing.md by hand each time. Measuring them twice never
# once caught a wrong number; what it caught was people using different
# recipes and getting different answers for the same thing. So the recipe
# lives here, and its output *is* the report.
#
# What it prints and why each line is there:
#
#   assertions        the suite, which must grow and never shrink
#   sanitizer         the suite again under CI's exact flags, address+UB
#   gates             the five lines of `make win32`, all of which must appear
#                     -- a missing line means that gate did not run, not that
#                     it passed
#   captures          every stored reference, by the recipe that took it
#
# A number that moves is not a failure. A number that moves *without being
# named in the report* is.
set -u

repo=$(cd "$(dirname "$0")/.." && pwd)
sha=${1:-}
work=$repo
tmp=$(mktemp -d /tmp/verify.XXXXXX)
trap 'rm -rf "$tmp"' EXIT

if [ -n "$sha" ]; then
    work=/tmp/verify-$sha
    rm -rf "$work"
    git -C "$repo" worktree add -q --detach "$work" "$sha" || exit 1
    trap 'rm -rf "$tmp"; git -C "'"$repo"'" worktree remove --force "'"$work"'" >/dev/null 2>&1' EXIT
    echo "verifying $sha in $work"
    if [ -n "$(git -C "$repo" status --porcelain)" ]; then
        echo "  note: the main checkout is dirty; this run is not affected by it"
    fi
else
    echo "verifying the working tree as it stands"
    if [ -n "$(git -C "$repo" status --porcelain)" ]; then
        echo "  WARNING: uncommitted changes. A number measured here is not a"
        echo "           number anybody else can reproduce from a sha."
    fi
fi
cd "$work" || exit 1
echo "  at $(git log --oneline -1 | cat)"
echo

# ---- build and suite -------------------------------------------------------

echo "== build =="
make clean >/dev/null 2>&1
if ! make > "$tmp/build.log" 2>&1; then
    echo "  FAILED"; grep -E "error:" "$tmp/build.log" | head -5; exit 1
fi
warn=$(grep -cE "warning:" "$tmp/build.log")
echo "  built, $warn compiler warnings"
[ "$warn" != 0 ] && grep -E "warning:" "$tmp/build.log" | head -3

ok=$(make test 2>&1 | grep -cE "^ok")
echo "  assertions   $ok"
echo

# ---- captures --------------------------------------------------------------
#
# Each is the recipe from docs/testing.md, run headless. The explorer's own
# window is not here: its reference is not in the repository and has to be
# grabbed from a machine in the same session, which is written up under
# "The explorer beside the machine".

echo "== captures =="
R=$work/tools/refcapture
# The two wine references are generated rather than committed -- .gitignore
# says so, because ROADMAP has them made under Wine -- so a throwaway worktree
# cannot contain them by construction. Borrowed from the main checkout when
# they are missing here, and said out loud when they are borrowed: a number
# measured against a file from somewhere else is still a number, but nobody
# should have to guess where it came from.
for gen in reference.png menu-reference.png; do
    if [ ! -f "$R/$gen" ] && [ -f "$repo/tools/refcapture/$gen" ]; then
        cp "$repo/tools/refcapture/$gen" "$R/$gen"
        echo "  (borrowed $gen from the main checkout; it is not in the repository)"
    fi
done

count() { # name reference our-png expected
    if [ ! -f "$2" ]; then
        printf "  %-22s MISSING REFERENCE -- not measured\n" "$1"; return
    fi
    if [ ! -f "$3" ]; then printf "  %-22s NOT RENDERED\n" "$1"; return; fi
    n=$(PXDIFF_REF="$2" PXDIFF_OUR="$3" "$R/pxdiff.py" 2>/dev/null |
        sed -n 's/.*differing pixels: \([0-9]*\) .*/\1/p')
    if [ -z "$n" ]; then
        printf "  %-22s COULD NOT COUNT -- not measured\n" "$1"; return
    fi
    printf "  %-22s %8s   was %s\n" "$1" "$n" "$4"
}

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=$tmp/s.bmp ./examples/controls >/dev/null 2>&1
magick "$tmp/s.bmp" "$tmp/s.png" 2>/dev/null
count "wine sampler" "$R/reference.png" "$tmp/s.png" 14877

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=$tmp/m.bmp ./examples/menu >/dev/null 2>&1
magick "$tmp/m.bmp" "$tmp/m.png" 2>/dev/null
count "wine menu" "$R/menu-reference.png" "$tmp/m.png" 4162

# The explorer's dialogs, each driven from the keyboard by the script that
# opens it. The shot wanted is the last one of the reference's own size --
# every one of these leaves several behind.
shot() { # glob reference out
    python3 - "$1" "$2" "$3" <<'PY'
import glob, re, sys
from PIL import Image
want = Image.open(sys.argv[2]).size
fs = sorted(glob.glob(sys.argv[1]),
            key=lambda s: int(re.search(r'(\d+)\.bmp$', s).group(1)))
d = [f for f in fs if Image.open(f).size == want]
if d:
    Image.open(d[-1]).convert('RGB').save(sys.argv[3])
PY
}

explorer() { # name reference expected script [env...]
    local name=$1 ref=$2 exp=$3 script=$4; shift 4
    rm -f "$tmp"/e*.bmp
    env "$@" WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP="$tmp/e%d.bmp" \
        WEEN32_SCRIPT="$script" ./examples/explorer /tmp/many >/dev/null 2>&1
    shot "$tmp/e*.bmp" "$ref" "$tmp/e.png"
    count "$name" "$ref" "$tmp/e.png" "$exp"
    rm -f "$tmp/e.png"
}

explorer "Folder Options General" "$R/folderopts-machine.png" 656 \
    "w:300 k:18 w:200 t:to w:900"
explorer "Folder Options View" "$R/folderopts-view-machine.png" 1403 \
    "w:300 k:18 w:200 t:to w:600 d:80,38 u:80,38 w:600"
explorer "Folder Options Types" "$R/folderopts-types-machine.png" 9424 \
    "w:300 k:18 w:200 t:to w:600 d:135,38 u:135,38 w:600"
explorer "Folder Options Offline" "$R/folderopts-offline-machine.png" 735 \
    "w:300 k:18 w:200 t:to w:600 d:205,38 u:205,38 w:600"
explorer "Column Settings" "$R/columns-machine.png" 1106 \
    "w:300 k:18 w:200 t:vc w:800"
explorer "Properties CONFIG.SYS" "$R/properties-machine.png" 652 \
    "w:300 k:40 k:40 k:40 k:40 k:40 w:200 a:13 w:900" WEEN32_EXPLORER_FIXTURE=1
explorer "Properties boot.ini" "$R/properties-boot-machine.png" 306 \
    "w:300 k:40 k:40 k:40 k:40 w:200 a:13 w:900" WEEN32_EXPLORER_FIXTURE=1
echo
echo "  Properties carries the day of the month in it and moves on its own."
echo "  The explorer's own window needs a machine: docs/testing.md."
echo

if [ "${FAST:-0}" = 1 ]; then
    echo "== FAST: the sanitizer and the gates were skipped =="
    exit 0
fi

# ---- the sanitizer, under CI's exact flags ---------------------------------

echo "== sanitizer =="
make clean >/dev/null 2>&1
if make X11=0 test CC=gcc \
     CFLAGS="-O1 -g -std=c99 -Iinclude -fsanitize=address,undefined -fno-sanitize-recover=all" \
     > "$tmp/asan.log" 2>&1; then
    n=$(grep -cE "^ok" "$tmp/asan.log")
    bad=$(grep -icE "LeakSanitizer:|AddressSanitizer:|runtime error:" "$tmp/asan.log")
    echo "  assertions   $n, complaints $bad"
    [ "$bad" != 0 ] && grep -iE "LeakSanitizer:|AddressSanitizer:|runtime error:" "$tmp/asan.log" | head -5
else
    echo "  FAILED"; tail -5 "$tmp/asan.log"
fi
echo

# ---- the four gates, and the fifth line ------------------------------------

echo "== gates =="
make clean >/dev/null 2>&1
make >/dev/null 2>&1
make win32 > "$tmp/win32.log" 2>&1
for line in "win32 constants agree" "win32 structs agree" \
            "zig binding agrees with the header" \
            "win32 examples/paint (zig)" "ween32 examples/paint (zig)"; do
    if grep -q "$line" "$tmp/win32.log"; then printf "  ok      %s\n" "$line"
    else printf "  MISSING %s\n" "$line"; fi
done
echo
echo "A missing gate line means that gate did not run, not that it passed."
