#!/bin/bash
# Everything a branch has to pass, in one command, from a clean checkout.
#
#     tools/verify.sh              # this working tree, as it stands
#     tools/verify.sh <sha>        # a throwaway worktree at that commit
#     FAST=1 tools/verify.sh       # skip the sanitizer, the gates and the pictures
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

# ---- the base --------------------------------------------------------------
#
# **First, because a stale base means none of the numbers below mean
# anything.** They would all be true, and true about a tree nobody is going to
# merge: a branch that forked before somebody else's work reverts it silently
# when merged, and the report says nothing, because measuring the branch is
# exactly what it did.
#
# This happened four times in one day. Three were branches sitting behind
# master; the fourth was master moving under a branch that had just rebased,
# which this cannot see -- it looks backwards from here and master moves
# afterwards. `git merge --ff-only` is what catches that one, on the way in.
#
# Exits non-zero, because a report that should not be read should not be
# reported.

echo "== base =="
behind=$(git rev-list --count HEAD..master 2>/dev/null || echo 0)
if [ "$behind" = 0 ]; then
    echo "  up to date with master"
else
    echo "  STALE: $behind commits behind master"
    echo "         base $(git log --oneline -1 "$(git merge-base HEAD master)" | cat)"
    echo "         rebase before reporting -- nothing measured here describes"
    echo "         the tree that would result from merging this."
    exit 1
fi
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

# **The status, and not just the count.** This was
#
#     ok=$(make test 2>&1 | grep -cE "^ok")
#
# which counts `ok` lines and never looks at whether `make test` ran at all.
# **The tests are a second compile** -- the library built ten lines above says
# nothing about them -- so a test file that stops compiling ends the run
# before any binary is executed.
#
# Measured rather than described, by appending `this is not c;` to
# `tests/propsheet_test.c`:
#
#     the line as it was    assertions 0, and the run carried on
#     the line as it is     FAILED to build or run the tests, plus gcc's
#                           first errors, exit 1
#
# **Zero is not silence and it is not nothing** -- somebody would very likely
# notice a suite that had gone from 1123 to 0. What it is not is *the reason*,
# and a number cannot be told from another number: `assertions 0` reads as a
# suite that ran and passed nothing, which is a different and much more
# alarming thing than a file that would not compile. The captures below it
# would meanwhile be measured perfectly well, because `make` succeeded -- so
# the report would be nine right numbers and one wrong one, which is the
# hardest shape to read.
#
# This is §9's *a build that did not happen looks exactly like a measurement
# that did*, in the file that exists to catch it, found by writing the entry.
if ! make test > "$tmp/test.log" 2>&1; then
    echo "  FAILED to build or run the tests"
    grep -E "error:|FAIL" "$tmp/test.log" | head -5
    exit 1
fi
ok=$(grep -cE "^ok" "$tmp/test.log")
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
#
# **The main checkout is asked of git, not derived from this script's path.**
# It used to be `$repo`, which is where *this file* lives -- fine when the
# script is the main checkout's, and wrong for everybody running it from
# their own worktree, where `$repo` is the worktree that by construction has
# neither file. So the borrow could not fire for the three people it was
# written for, and their runs said `MISSING REFERENCE -- not measured` for
# these two all day. `git worktree list` names the main worktree first from
# anywhere inside the repository.
main=$(git -C "$work" worktree list --porcelain 2>/dev/null |
       sed -n '1s/^worktree //p')
[ -n "${main:-}" ] || main=$repo
for gen in reference.png menu-reference.png; do
    if [ ! -f "$R/$gen" ] && [ -f "$main/tools/refcapture/$gen" ]; then
        cp "$main/tools/refcapture/$gen" "$R/$gen"
        echo "  (borrowed $gen from $main; it is not in the repository)"
    fi
done

# **Every capture is counted, measured or not.** `MISSING REFERENCE` is an
# honest line and it was far too quiet: it appeared in every run three people
# posted today and nobody, including me, noticed that two of the nine were
# never being measured at all. A line you can skim past is not a report. The
# tally at the end has to read nine.
captures=0
measured=0
count() { # name reference our-png expected
    captures=$((captures + 1))
    if [ ! -f "$2" ]; then
        printf "  %-22s MISSING REFERENCE -- not measured\n" "$1"; return
    fi
    if [ ! -f "$3" ]; then printf "  %-22s NOT RENDERED\n" "$1"; return; fi
    n=$(PXDIFF_REF="$2" PXDIFF_OUR="$3" "$R/pxdiff.py" 2>/dev/null |
        sed -n 's/.*differing pixels: \([0-9]*\) .*/\1/p')
    if [ -z "$n" ]; then
        printf "  %-22s COULD NOT COUNT -- not measured\n" "$1"; return
    fi
    measured=$((measured + 1))
    printf "  %-22s %8s   was %s\n" "$1" "$n" "$4"
}

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=$tmp/s.bmp ./examples/controls >/dev/null 2>&1
magick "$tmp/s.bmp" "$tmp/s.png" 2>/dev/null
# 14877 until 2026-08-29, when the property sheet's frame landed. `gb_paint`
# and the tab row are not the property sheet's alone -- examples/controls goes
# through both -- so a change whose predictions covered nine captures moved a
# tenth thing nobody had listed. It moved 334 pixels *towards* the machine.
count "wine sampler" "$R/reference.png" "$tmp/s.png" 14543

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=$tmp/m.bmp ./examples/menu >/dev/null 2>&1
magick "$tmp/m.bmp" "$tmp/m.png" 2>/dev/null
count "wine menu" "$R/menu-reference.png" "$tmp/m.png" 4162

# The explorer's dialogs, each driven from the keyboard by the script that
# opens it. The shot wanted is the last one of the reference's own size --
# every one of these leaves several behind.
# The frame a run meant, out of the several it left behind. This was ten lines
# of python inside this function, which meant every recipe in docs/testing.md
# either duplicated it or could not be run -- and one of them could not be run
# and nobody noticed for as long as it had existed. It is a script now and the
# recipes call the same one this does.
shot() { # glob reference out
    "$R/pickshot.py" "$1" "$2" "$3" >/dev/null 2>&1
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
if [ "$measured" = "$captures" ]; then
    echo "  all $captures captures measured"
else
    echo "  MEASURED $measured OF $captures -- the rest are lines, not numbers"
    bad_captures=1
fi
echo "  Properties carries the day of the month in it and moves on its own."
echo "  The explorer's own window needs a machine: docs/testing.md."
echo

# ---- the rename box, which is relations and not a reference -----------------
#
# **Every check above counts pixels against a stored picture, and this one
# cannot.** The rename box is not a window of its own, so `pickshot.py` has
# nothing to pick -- every frame the run leaves is the explorer's own 654x544
# -- and a whole-frame count would drown seven pixels of box in a window's
# worth of everything else. That is why docs/testing.md carried it in prose
# for weeks and called it the one thing in its section that was *believed
# rather than checked*.
#
# So it is checked as **relations**: two pixels left, twelve wider, as tall as
# the row, and four more, none of which mentions a coordinate. The same
# instrument reads them off the machine's captures and off ours, at whatever
# size either window happens to be -- which is the comparison the prose
# described and nothing could re-make.
#
# **The machine's own captures are run through it too.** They are the thing
# the numbers came from, so if one of them is ever replaced by a capture taken
# in a different state, this says so rather than quietly moving the target.
renamebox() { # name row-capture box-capture
    local out
    if [ ! -f "$2" ] || [ ! -f "$3" ]; then
        printf "  %-22s MISSING CAPTURE -- not measured\n" "$1"; return
    fi
    out=$("$R/renamebox.py" "$2" "$3" 2>&1)
    if [ $? -eq 0 ]; then
        printf "  %-22s %s\n" "$1" "$(printf '%s\n' "$out" | tail -1)"
    else
        printf "  %-22s FAILED: %s\n" "$1" \
            "$(printf '%s\n' "$out" | grep -m1 'NO \|^no \|^the two')"
    fi
}
echo "== the rename box =="
renamebox "machine CONFIG.SYS" "$R/rename-config-row-machine.png" \
    "$R/rename-config-box-machine.png"
renamebox "machine Program Files" "$R/rename-pf-row-machine.png" \
    "$R/rename-pf-box-machine.png"
# Five downs picks CONFIG.SYS in the fixture's list; 113 is F2. The first run
# stops with the row picked, so its highlight *is* the label rect.
rm -f "$tmp"/rb*.bmp
for phase in before after; do
    key=""; [ "$phase" = after ] && key="k:113 "
    WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
        WEEN32_BMP="$tmp/rb-$phase-%d.bmp" \
        WEEN32_SCRIPT="w:300 k:40 k:40 k:40 k:40 k:40 w:300 ${key}w:600" \
        ./examples/explorer >/dev/null 2>&1
    last=$(ls "$tmp"/rb-$phase-*.bmp 2>/dev/null |
           sed 's/.*-\([0-9]*\)\.bmp/\1 &/' | sort -n | tail -1 | cut -d' ' -f2)
    [ -n "$last" ] && magick "$last" "$tmp/rb-$phase.png" 2>/dev/null
done
renamebox "ours" "$tmp/rb-before.png" "$tmp/rb-after.png"
echo

if [ "${FAST:-0}" = 1 ]; then
    echo "== FAST: the sanitizer, the gates and the pictures were skipped =="
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

# ---- the pictures, which are the only thing that opens what paint wrote -----
#
# The gates say Paint *compiles*, against win32 and against ween32, and are
# silent about whether a picture that goes out comes back. `pictures.py` asks
# that -- and until now nothing ran it, so it could neither pass nor fail.
#
# **Three outcomes, not two.** Paint is built by `zig build paint`, which
# nothing else here does: without zig there is no binary, and a check with no
# binary must say so rather than report a verdict about Paint. That is the
# same rule as the gate line above -- a missing line means it did not run.
#
# **`--cache-dir` is not optional and is the Makefile's own.** This checkout
# is on an sshfs mount, and Zig extracts a fetched package into the build root
# before renaming it into place -- a rename this mount refuses, with a bare
# `error: PermissionDenied` and nothing to say which path it meant. The
# Makefile has always passed `$(ZIG_CACHE)`, which is on /tmp, and that is why
# `make win32` builds Paint on this machine while a plain `zig build paint`
# cannot. Written out because the failure names neither the mount nor the
# cache, and the next person will lose the same hour I did.
echo
echo "== pictures =="
if ! command -v zig > /dev/null 2>&1; then
    echo "  SKIPPED: no zig, so Paint was not built and nothing was asked"
elif ! zig build paint --cache-dir "${ZIG_CACHE:-/tmp/ween32-zig-cache}" \
        > "$tmp/paintbuild.log" 2>&1; then
    echo "  NOT FIT TO REPORT: Paint did not build"
    grep -E "error" "$tmp/paintbuild.log" | head -3 | sed 's/^/    /'
elif python3 tools/paint/pictures.py > "$tmp/pictures.log" 2>&1; then
    printf "  ok      %s\n" "$(tail -1 "$tmp/pictures.log")"
else
    echo "  FAILED"
    grep -E "FAIL|could not|refused" "$tmp/pictures.log" | head -4 | sed 's/^/    /'
fi
