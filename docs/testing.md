# Testing ween32

Three layers, cheapest first. The first two are what CI runs and take under a
minute; the third is the one that needs your eyes, and only that one.

## 1. The automated suite

```sh
make clean && make
make test
```

Expect **no `FAIL`, and more `ok` lines than last time**. The count only goes
up — if it has dropped, a test file stopped being built rather than a test
starting to pass.

**There is no number in that sentence any more, and its absence is the
point.** There was one, and it said **1023** while the suite said **1095** —
seventy-two behind, across a day and a good many commits. The paragraph that
carried it *already said* it was written by hand and would be wrong between
the commit that adds a test and the commit that remembers the line: a
disclaimer on a number is not a check on it, and the number was believed
anyway because it was specific. `tools/verify.sh` prints what the suite
actually says, and that is the one to quote.

Then the four things `make test` does not cover:

```sh
# that the same example source still builds against the real windows.h, that
# every constant ween32 declares is the number Windows gives it, and that
# `zig build` still links a program against this library on this machine
make win32
```

That last one is there because `build.zig` keeps its own list of the
library's sources and `make` does not read it. A file added to the Makefile
and not to `build.zig` leaves `make` green and every Zig program that depends
on ween32 -- paint, notepad, WordPad -- failing to link on a symbol it cannot
find. `src/richedit.c` did exactly that, and the line that says so now is
`zig build paint` with no target.

This is the half of the promise the suite cannot see. It has caught the
explorer failing to compile against win32 at all (GET_X_LPARAM lives in
windowsx.h), the explorer creating common controls without asking for them
(nothing would have been created on Windows), a list view left to default to
the icon view instead of the report view, and two constants copied wrong —
`TVN_SELCHANGEDA` sitting on `TVN_SELCHANGING`'s number and
`TB_ISBUTTONCHECKED` on `TB_ISBUTTONHIDDEN`'s. Needs `zig`, for its bundled
mingw-w64 headers; without it the target says so and passes.

```sh
# that drawing is still quick: three thousand mouse moves through the pencil,
# which is a second of somebody scribbling
python3 -c "print('d:70,60 ' + ' '.join('m:%d,%d'%(70+(i%180),60+(i%240))
  for i in range(3000)) + ' u:250,300')" > /tmp/stroke
time WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_SCRIPT="$(cat /tmp/stroke)" \
  ./zig-out/bin/paint                       # expect well under a tenth of a second
```

```sh
# that a rubber band is still quick: three thousand moves of an ellipse's
# corner, which is what a slow preview used to make of a single drag
python3 -c "print('d:20,229 u:20,229 d:100,80 ' + ' '.join('m:%d,%d'%(70+(i%140),
  60+((i*7)%180)) for i in range(3000)) + ' u:200,200')" > /tmp/band
time WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_SCRIPT="$(cat /tmp/band)" \
  ./zig-out/bin/paint                       # expect a few hundredths

# and that resizing is: two hundred window sizes, as a drag of the corner
python3 -c "print(' '.join('r:%d,%d'%(1100+(i%200),800+(i%150))
  for i in range(200)))" > /tmp/rez
time WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_SCRIPT="$(cat /tmp/rez)" \
  ./zig-out/bin/paint                       # expect about a millisecond each
```

Neither of those catches the thing that actually made a resize crawl, which
only a real X server shows: the window falling further behind the pointer the
longer the drag runs, because every motion report the mouse sent was answered
with its own repaint. What it looks like from outside is a drag of a corner
that arrives late — 300 moves of a 1500x1000 window took 323 ms to send and
the window caught up 677 ms after the last of them. The backend now reports
where the pointer *is*, not everywhere it has been, and the same drag lands
on the final size with the last move. Measuring it needs a display and a
program that sends the moves; there is no headless equivalent, because the
script feeds one event at a time and so can never build a backlog.

Three more of a drag's faults are invisible to a script for the same reason —
they need a pointer, a display, and sometimes a window manager that disagrees.
The pointer moves in screen pixels while a window is measured in the pixels it
is drawn from, so at 2x, following it one for one grew the window twice as
fast as the hand moved and the border ran away from the grip. A size counted
up step by step drifts as soon as one of the steps is refused — by the minimum
size, or by a window manager — and never comes back to where it started;
measured from where the drag began, a drag out and back lands on the size it
began at, exactly. And a window manager that hands back a size of its own was
answered by our resizing to what we asked for and then to what it gave, twice
per mouse report: the window flickered between the two for as long as the drag
lasted. Asking, and letting the answer do the resizing, is the whole fix.

A fourth kind of fault the script cannot see at all: the keys the X server
sends. `WEEN32_SCRIPT` injects virtual keys straight, so it exercises every
window in the program without ever going through the table that turns a
keysym into one. That table had no function keys in it, and F2 — rename — and
F5 — refresh — did nothing whatever in a real window while every headless
render of the same thing worked. The table is `ween_x11_keysym_to_vk`, kept
outside the backend's own gate and named rather than static so that
`keys_test` can read it; anything reachable only by a key nobody types a
character with belongs in that test.

Driving that needs XTEST — `XTestFakeMotionEvent` and `XTestFakeButtonEvent`,
so that the press is a real press with X's implicit grab behind it; synthetic
events through `XSendEvent` are not the same thing and will not show any of
this. A window manager to disagree with can be a hundred lines: select
`SubstructureRedirectMask` on the root, clamp every `ConfigureRequest` to a
maximum, and answer with the geometry kept, which is what a tiling one does
all day.

```sh
# the sanitizers, which have caught real bugs the suite passed through
make clean && make X11=0 test CC=gcc \
  CFLAGS="-O1 -g -std=c99 -Iinclude -fsanitize=address,undefined -fno-sanitize-recover=all"

# and that the library still builds with no window system at all
make clean && make X11=0
```

Both should be silent apart from the usual test output.

One trap worth knowing: `make -B` rebuilds the library and the examples but
**not** the tests. After changing library code, either `make clean` or name the
test — `make tests/geometry_test` — or you will be running the old binary
against the new library and drawing conclusions from it.

The same trap once had a worse form: three of the tests were *run* by `make
test` without being named among the things it depends on, so a stale binary
could report a pass for code that no longer existed — and did. There is one
`TESTS` list now, used to build them, run them and clean them, so that
particular drift cannot happen again. If you add a test, add it there and
nowhere else.

## 2. Fidelity against Wine

The renders are compared pixel by pixel with a real win32 render of the same
source. You need the reference captures first — see [Reference
captures](../ROADMAP.md#reference-captures); they are gitignored because they
are generated.

```sh
make clean && make

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/ours.bmp ./examples/controls
magick /tmp/ours.bmp /tmp/ours.png
tools/refcapture/pxdiff.py                  # expect 14877 / 298596 — 5.0%

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/m.bmp ./examples/menu
magick /tmp/m.bmp /tmp/m.png
PXDIFF_REF=tools/refcapture/menu-reference.png PXDIFF_OUR=/tmp/m.png \
  tools/refcapture/pxdiff.py                # expect 4162 / 39200 — 10.6%
```

Most of both — 3170 of the menu's and about 6300 of the sampler's — is one
thing: the caption's gradient. Where it stops is measured off the machine, which
holds its end colour two pixels before the leftmost caption button; wine
stops one pixel before it. A window with nothing but a close box is where the
two disagree, and every pixel of the ramp shifts by a step when its span
changes by two, so the whole caption strip counts as different. The machine's
own Column Settings comes out on the gradient exactly, which is what settled
it — see the note on the ramp below.

Another 1213 of the sampler's is where a tick box puts its label: the machine
keeps one column more between a check box and its text than an option button
gets, and wine gives the two the same. That was measured on two Folder
Options pages at once — the boxes on Offline Files and the option buttons on
General — and the machine is followed. What that column really is was
settled later by asking Windows for its own rectangles: measured from the
control both labels start in the same place, and it is the option button's
*circle* that sits one column in. A hundred more of the sampler's pixels are
that; see "The option button's column, settled by asking Windows" below.

Fourteen of the rest are two mnemonic underlines a control draws only once Alt
has been pressed, which wine draws always — the same rule that keeps a menu's
underlines out of sight, applied to the controls in a dialog.

About 820 more are where a label sits inside a button, and are deliberate in
the same way: the machine centres one by the height of a *line* — the ascent,
the descent and the row between two of them, fourteen at this size — where
wine centres it by the strike's own cell, which is twelve for Tahoma; and it
places a centred label by the width the glyphs draw at, where wine measures
Tahoma off its outline and gets three pixels more across "Cancel". Both were
measured on the machine's own Properties page, whose buttons and tick boxes
come out on its pixels because of them — see [Properties](#properties).

Roughly 240 of what is left is the menu bar, and is *deliberate*. A bar item
is its label plus twelve pixels of padding, half each side, which is what
Paint's own bar measures on the machine — the gap between one label's ink and
the next is a constant thirteen across File, Edit, View, Image and Colors.
It was sixteen here for a while, off the explorer's bar; but the shell's bar
is a *toolbar* of drop-down buttons in its rebar, not a menu bar at all, and a
toolbar button's padding is not a menu item's. The rest is the caption's bold
title, whose strike is wine's Tahoma Bold and not the machine's. Do not "fix"
the bar by eye — measure it
against a program that has a menu bar.

The ramp itself is one division per pixel, a shade below wherever it lands
exactly on a whole number — `start + (d * (end - start) - 1) / span` in whole
numbers, with the start colour held across the icon's room and the two
columns past it. That is the machine's, column for column at three widths;
see "The caption's ramp, measured to the column" below, and
`tests/caption_test.c`, which holds it against the captures. It replaced a
step worked out once in 16.16 and added up, which agreed at 400 and 500 wide
and drifted a column at 654 — the error grows with the span, and the
sampler's caption is wider than any of the three.

Thirty-six of the sampler's are the same thing in miniature: a scroll
bar's up arrow. Wine draws it a row higher than the machine does, and the
machine's is what ween32 draws — see the box under the address bar below,
which comes out pixel for pixel because of it. Another 1705 are the tab
control: its strip begins one pixel further in on the machine than in wine,
and its smallest tab is sized from the average character width the font
reports rather than from the alphabet. Both were measured off Folder
Options, whose four tabs land on the machine's pixels because of them.

About 3100 of the controls sampler's 1.9% is the tree and the list view, and
is deliberate in the same way. The tree's indent, the column its buttons sit
in and the pixel of white above its first row are measured against a Windows
2000 shell tree; the list's two rows of white between its header and its first
item, and its header's text sitting six in rather than eight, against the same
machine's list. See the ROADMAP for what was measured.

### Where the checkouts live, and what that costs

`/home/jd/ween32/ween32` and `/home/jd/ween32/notepad` are **sshfs mounts** --
`jedi@10.0.2.2:/home/jedi/projects/...` -- and every worktree beside them is
ext4. `stat -f -c %T <path>` says `fuse` for the first two and `ext2/ext3`
for the rest, and `mount | grep ween32` shows the pair. Nobody knew this for
most of a day, and it explains a fault nobody could otherwise have found.

**And the emulator is one too.** `/home/jd/jslinux` is
`sshfs -p 2995 jedi@10.0.2.2:/home/jedi/tmp/jslinux-2019-12-21`, which is why
it reads as an **empty directory** when the mount is not up rather than as a
missing one.

**Three of us reported "the machine is gone" in one evening and none of us
said which kind of gone it was**, because `ls` cannot tell them apart:

```
a deleted tree     somebody has to restore it
a dropped mount    one command, and the tree was never touched
```

**So the check is `mount | grep jslinux`, not `ls`** -- the same sentence as
the checkouts above, one directory along, and it cost an evening's worth of
people concluding the harder of the two.

**Observed, and reproducible.** A copy step whose *source* is on the mount
produces an **empty result, with no error**: `installHeadersDirectory` over
one hands a consumer an `-I` at a directory with nothing in it, so a C
program built against the package fails on `'windows.h' file not found`
while every listing shows the header exactly where it should be. The
controlled pair, same Zig, same build.zig, same relative-path form, no
symlink either side:

| dependency at | filesystem | headers staged | host build |
| --- | --- | --- | --- |
| `../ween32` | sshfs | **0 files** | fails |
| `../ween32-localcopy` (`cp -r` of it) | ext4 | 10 files | succeeds |

To run it again: point notepad's `build.zig.zon` at each in turn, take
`addHeaders` back out of its build.zig so the build depends on the copy,
`rm -rf .zig-cache`, and read the staged directory out of the compile line --
`zig build --verbose | grep -o '\-I \.zig-cache[^ ]*'`, then `ls` what it
names.

**What is *not* the mechanism**, so nobody spends the hour I nearly did:
`readdir` over this mount is fine. A C program calling it gets `d_type=8`
(`DT_REG`) for all eight headers, exactly as on ext4, so this is not the
usual `DT_UNKNOWN` fuse story. What goes wrong is inside the copy; the
symptom is what is written down here, not a cause.

**Reading is fine, and that is what makes it dangerous.** The library's own C
sources compile out of the mount without complaint, and `zig rc` reads a
script from it and writes its `.res` into the cache -- alice built both
targets in jd's own checkout, which is itself on the mount. So most of a
build works, and the one part that fails is silent.

**Suspected on the same mechanism, and not tested**: `b.installArtifact`
copying into a `zig-out` that lives on the mount; `addWriteFiles` and
`addCopyDirectory` with a source on it; anything else that stages a
*directory* rather than naming a path. None of these has been observed
failing -- they are here because they are the same shape, and if you check
one, please write down which way it went.

**Two other things this probably explains**, both reported by alice in the
checkout on the mount and neither yet tied to it by experiment: `make`
printing *"Clock skew detected"* on nearly every run, and `zig build` in the
same tree twice giving `error: PermissionDenied` unless `.zig-cache` is
cleared between them -- the trap already written down for paint. A network
filesystem with its own idea of timestamps would produce the first, and a
cache directory it does not lock the way a local one does could produce the
second. Both are guesses until somebody runs the pair test on them.

**What to do about it**: prefer pointing at a path over copying out of one.
`ween32.addHeaders(dep, exe)` exists for exactly this reason -- a path is
read where it stands, and reading over the mount was never the problem.

### A machine of your own

The Windows 2000 the captures come from is a program with a socket, not a
singular thing: boot one whenever you need to measure something, and shut it
down when you are done. Sharing one means queueing, and a VM nobody is
watching is 192 MB and a core.

```sh
cd /home/jd/jslinux
setsid node jslinux-node.js --cpu=x86 --emu=. --url=win2000/win2k.cfg \
  --mem=192 --graphic --w=1024 --h=768 --net --drive=vm-daemon.js \
  --share=/home/jd/ween32/share-<you> \
  --sock=/tmp/jslinux-<you>.sock --shm=/dev/shm/jslinux-<you>.fb &
```

**`--share` is not optional and `Z:` does not follow from it.** Every probe
in `tools/vm` is run as `Z:\thing.exe` and writes its answer to `Z:\thing.txt`,
and a fresh guest has no Z: at all -- the drive letter is a *mapping*, the
disk does not keep one across a boot (see the note on persistence below), and
the failure is a dialog saying **"Z:\ is not accessible. This folder was moved
or removed"**, which reads like a broken share rather than a missing letter.
So, once, on every boot, through Start > Run:

```
net use Z: \\10.0.2.2\share
```

`10.0.2.2` is the host as the guest sees it and `share` is the name
`--share=DIR` is served under; the console flashes and closes, and nothing
says it worked except Z: starting to work.

About a minute to the desktop. Then everything below, pointed at it:

```sh
export JSLINUX_SOCK=/tmp/jslinux-<you>.sock JSLINUX_SHM=/dev/shm/jslinux-<you>.fb
tools/vm/drive.py click 300,200 type hello key Enter park shot /tmp/s.png
node /home/jd/jslinux/vmctl.js --sock=/tmp/jslinux-<you>.sock quit
```

`quit` answers `bye` and takes the socket and the shared memory with it. A VM
that *dies* leaves both behind, and the next thing to connect gets
`Connection refused` on a socket file that is still there -- so a socket
existing is not a machine running. If one goes quiet in the middle of a
measurement, suspect the machine before your method, and throw away anything
that straddles the death: the frame counter in the shared memory stops with
it, which is the quickest way to tell.

**A fresh VM is a fresh Windows.** Before any explorer capture: Tools >
Folder Options > General > "Use Windows classic folders", and the four column
widths from [the explorer's section](#the-explorer-beside-the-machine). With
the web view on, the shell puts a third panel between the tree and the list
and nothing lines up.

Two key names that work in `drive.py`: `key KeyF:ControlLeft` for Ctrl+F, and
`key Home:ControlLeft` for Ctrl+Home. One that does not is `MetaLeft` -- the
daemon answered an empty line and stopped listening, taking the machine with
it, so a name the daemon does not know is not something to try on a machine
somebody else is using.

**Do not `park` between opening a menu and clicking an item in it.** The
pointer is *walked* rather than jumped, so a walk that starts from the parked
corner crosses the menu bar on its way, and crossing another top-level title
while a menu is open switches which menu is down. The click then lands in a
different menu at the same coordinates -- twice in a row for me, on a
disabled item, which looks exactly like a click that missed. Drive the whole
gesture in one invocation, or reach the item by keyboard: `key KeyV:AltLeft`
then `key KeyO` is View > Options and has none of this.

**And a batch file is worth it past about three probes.** Each Start > Run
trip is four synthetic events and a guess at where the menu item is; a
`.bat` on the share is one trip for any number of runs. `start /wait "" Z:\p.exe`
rather than `Z:\p.exe` -- these probes are `--subsystem,windows`, so `cmd`
does not wait for them and without it the whole file runs at once.

### Paint beside the machine

`examples/paint` is held up against a Windows 2000 running the real
program, which is where the numbers above came from. The window differs by
six pixels of 110,000, and every tool but two draws what the machine draws to
the pixel. See [paint.md](paint.md) for how to take the captures, how the
comparison calibrates the guest's relative mouse, and what the two are.

```sh
zig build paint
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/paint-ours.bmp ./zig-out/bin/paint
magick /tmp/paint-ours.bmp /tmp/paint-ours.png
tools/refcapture/paintdiff.py               # expect 6 of 110000
```

What the tools draw is checked the same way, by drawing it on both and
counting. These four are the ones to run after touching a tool, a selection
or the rasteriser — each should print zero:

```sh
tools/paint/compare.py "tool 10" "option 0" "tool 12" "drag 76,57 126,87"
tools/paint/compare.py "tool 1" "drag 81,61 141,101" "drag 100,80 130,110"
tools/paint/compare.py "tool 0" "drag 90,70 130,70 130,110 90,110 90,70"
tools/paint/compare.py "color 16" "tool 7" "option 6" "click 90,100" \
    "color 20" "click 120,100" "color 16" "tool 2" "option 0" \
    "rclick 90,100" "rclick 120,100"
```

Name the settings a gesture depends on. The machine keeps a tool's setting
between runs, so a rectangle drawn without saying `"tool 10" "option 0"`
first comes out in whatever pen width the last test left behind, and the
difference looks like a bug in the shape.

### The explorer beside the machine

`WEEN32_EXPLORER_FIXTURE=1` fills both panes with what a Windows 2000
explorer shows sitting on Local Disk (C:) — the same eleven items in the
tree, the same six in the list — so the window can be put beside a
screenshot of that machine and counted:

```sh
WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
  WEEN32_BMP=/tmp/fx%d.bmp ./examples/explorer
```

The window is at (132,132) on the machine's 1024x768 screen and 654x544, so
a pixel at window-relative (x,y) is at (x+132,y+132) in a screenshot of it.
Set the machine to Tools > Folder Options > Web View > "Use Windows classic
folders" first: with the web view on, the shell puts a third panel between
the tree and the list and nothing lines up.

The machine's columns have to be the ones the fixture uses — Name 120, Size
96, Type 120, Modified 120. A divider double-clicked or dragged changes them
and the shell remembers it, which puts every column after the first one out
by the difference and makes the whole list look wrong. View > Choose Columns
sets each one back by number; a width there is one more than the width here,
so Name is 119 in that dialog.

With a tree item picked — click "Local Disk (C:)" in the tree on both sides,
which is the state the shell leaves after the Folders pane is opened — the
window differs by 3221 of 355776 pixels, 0.9%:

```sh
WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
  WEEN32_BMP=/tmp/ts%d.bmp WEEN32_SCRIPT="w:200 d:140,179 u:140,179 w:400" \
  ./examples/explorer
tools/vm/grab.py /tmp/machine.png 132 132 654 544
```

Most of those 3221 are ours quantised to 5-5-5 — the machine draws every icon
through a sixteen bit image list, so its pixel is ours with the low three bits
of each channel dropped and the top bits shifted back in:

| band | differing | what it is |
| --- | --- | --- |
| left pane | 1813 | the tree's icons, quantised |
| list pane | 947 | the same, and two file icons whose art has a near-white where ours has white, and six pixels of a date |
| caption | 176 | the bold title — wine's Tahoma Bold is not the machine's |
| menu band | 170 | the animation the shell plays at its right |
| address band | 114 | quantisation |
| status bar | 1 | one pixel of a letter |
| toolbar band | 0 | |

The tree pane is window-relative x 4..203, y 100..519. Everything in it
matches but the icons, and every icon difference but the picked one is the
quantisation.

#### The box under the address bar

The suggestions the address bar offers are their own window, so they can be
counted on their own — and they come out **0 of 50000**. The fixture knows
what the machine has in `C:\Program Files`, read off the machine's own
suggestion box, since that is the only way to be sure of a name its list view
truncates.

```sh
WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
  WEEN32_BMP=/tmp/pf%d.bmp \
  WEEN32_SCRIPT='w:300 d:300,84 u:300,84 w:200 t:C:\Program_Files\ w:800' \
  ./examples/explorer
# the 500x100 frame is the box; the machine's is tools/refcapture/suggest-machine.png
PXDIFF_REF=tools/refcapture/suggest-machine.png PXDIFF_OUR=/tmp/ours.png \
  tools/refcapture/pxdiff.py                  # expect 0 / 50000
```

Where it *lands* has to be checked too, and separately: the box is its own
window, so a comparison of its contents says nothing about where it hangs.
`tools/refcapture/suggest-window-machine.png` is the machine's whole window
with the box up. Paste the box into the window frame at (81, 92) — the corner
it goes to — and the 500x100 it covers differs by 0. The address bar above it
differs by 26, which is the machine's mouse pointer sitting in the field; the
panes differ throughout, because the machine is showing Program Files while
the fixture shows Local Disk (C:).

The box's top-left is the bottom-left of the combo box's `rcItem` — the band
the text lives on, which is not the client area and not the edit control
either. Taking it from the client put the box a pixel high.

Its height is `min(14n + 15, 100)` for n names, measured on the machine at
29, 43 and 57 for one, two and three, and 100 once there are more than it
will show. `tools/refcapture/suggest-one-machine.png` is the one-name box,
which is the interesting end: it has a corner with no bar above it, so it is
the capture that says the corner draws no background of its own.

Getting there settled five things that no other capture could have:

- a scroll bar's up arrow sits a row lower than wine draws it (the sampler
  moved by 36 for this, and the machine is the reference);
- the corner a window is dragged by is one square in a scroll bar and
  another in a status bar — comctl32 draws its own rather than asking user32
  for one, and the two are anchored differently;
- the thumb is sized by the rows that show *whole*, not by the height
  divided by the row;
- the box is as wide as the combo box's field *area*, which is a pixel wider
  than the box the text is typed in — `GetComboBoxInfo` is how to ask;
- and the shell's list is not a stock one: rows of fourteen, inset four
  columns and three rows, and `LBS_NOINTEGRALHEIGHT` so the last row is cut
  by the bottom rather than dropped.

The one-name box settled two more:

- a corner draws its lines and no background, so what is behind it shows —
  white on a window, face at the foot of a bar, which is where the face in
  the seven-name box comes from;
- and a hatch line is four pixels, the last of them face. A status bar's
  corner fills its square first and so never needed the fourth.

#### The two dialogs

`tools/refcapture/columns-machine.png` is the machine's Column Settings and
`folderopts-machine.png` / `folderopts-view-machine.png` its Folder Options.
Every rectangle in both is measured off those, so a change that moves one
shows up beside them:

**`folderopts-machine.png` has a machine's settings inside it, and one of them
is worth writing down before it costs somebody an afternoon.** A fresh capture
of the same sheet, taken on 2026-08-29, is identical to it except for **183
pixels**, all in the Web View group:

```
the stored reference    "Use Windows classic folders"   selected
that machine            "Enable Web content in folders" selected
```

**That is a setting and not a rendering** — and it is not two pixels of radio
dot, because the group's 32x32 illustration changes with the choice, which is
comfortably large enough to read as a layout bug. The reference is not wrong;
it simply records a machine as it was configured, and **a capture of a
configurable dialog is a capture of somebody's configuration**. When a Folder
Options count moves by something in the low hundreds, check the radio buttons
against the reference before looking at the code.

**Column Settings** differs by **1106 of 103290**, 1.1%. Its scroll bar is
718 of that and is content, not drawing: the machine's shell offers some
fifty columns where this example has eight, so its thumb is a fifteenth of
the trough and ours five sixths of it. Another 252 is the caption's bold
title. What is left is 58 pixels where wine's MS Sans Serif draws a `k` or a
`v` a pixel off the machine's, and 77 where a bordered edit box starts its
text: the machine's begins two further in with that font, and the rule that
gives both that and Tahoma's three — wine's, half the average character
width — has not been found.

One behaviour in that dialog is deliberately not the machine's. Click a check
box there twice quickly and the machine turns it over **once**: the second
press of a pair arrives as a double click, and its list drops it — watched on
the machine twice, on "Attributes" and on "Size", each going from clear to
ticked and staying there. ween32 turns the box over on that press too, so a
box clicked fast flips every time and two clicks leave it as it started. The
row is not picked either way, which the machine agrees with.

Each of Folder Options' four pages is counted against its own capture. The
tabs are at y 38, at x 30 / 80 / 135 / 205; the 386x468 frame is the sheet:

| page | differing | of 180648 | what is left |
| --- | --- | --- | --- |
| General | 656 | 0.4% | the caption's bold title and icon, and two disabled circles' insides |
| View | 1403 | 0.8% | the title, the machine's pointer, the folder a heading wears, the scroll bar |
| File Types | 9424 | 5.2% | the list's contents |
| Offline Files | 735 | 0.4% | the title, and the arrows' bevel |

**Three of those four captures were retaken without the pointer in them.**
A screenshot has the mouse in it wherever the mouse was, and on General it
was sitting over "Use Windows classic desktop", on File Types and Offline
Files over the tab that had just been clicked. Each retake was checked
against the capture it replaced before it replaced it: **285, 296 and 302
differing pixels, every one of them inside a 17x25 box** — the arrow itself
and nothing else. So the three counts fall by that much and no fact about
either side changed. View keeps its pointer: that machine's Advanced settings
are not this machine's, and a retake would have moved the page as well as the
arrow.

The pointer is worth a moment because it is the same trap as the caret. A
capture is a photograph of a machine doing something, and what the machine
was *being asked* to do is in the picture too: the pointer, a hot button
under it, a caret half way through its blink, a tip that had time to come up.
Park the pointer somewhere harmless, wait for the blink, and take the picture
of the program rather than of the session.

Only **File Types** cannot close: the machine's list is its own registry — a
hundred extensions this example has never heard of — while ours is what
`type_of` knows. The page's frame, its columns, its buttons, its wording and
the seventeen-pixel pitch of its rows all match; the rows themselves never
can, and about 7000 of that 9719 is them. Every kind of file wears the
picture registered for it — a batch file is not drawn like a bitmap — so the
rows differ by their art as well as their words.

**View**'s Advanced settings is a tree, as the machine's is: rows indented by
level, a folder on a heading, and a tick box or an option button in the state
column before the label. What is left of it is the folder icon — ours is the
one the rest of this shell uses, and the machine's is its own — and the
scroll bar, which is shorter here because the machine offers more settings
below the ones it shows.

One thing is in every one of these counts and in the Column Settings one: the
caption's bold title — wine's Tahoma Bold is not the machine's, and its
letters are a pixel wider — which is about 300, and its icon another 93. The
pointer used to be a second such tax and is now only in View's.

What General had left past the caption was worth naming, and one of the two
is now fixed. **142 pixels were a focus rectangle with no top and no bottom.**
The rectangle goes round the label — the text's height with a pixel of margin,
sixteen rows — and an option button in that page is thirteen tall, so the two
rows it asked for outside itself were clipped away and what reached the page
was two upright sides with nothing joining them. The machine draws that
rectangle as the control's thirteen rows exactly, and Find's round "Down" as
sixteen inside a control of twenty: the rule that gives both is the label's
rectangle **clipped to the control**, which is what `bt_paint` does now, with
both halves asserted in `tests/dlg_test.c`. General went 786 to **656** and no
other capture moved a pixel.

What is left there is **32 pixels in the two disabled option buttons'
insides**, where the machine leaves the circle unfilled and draws its
highlight and ours does not.

View > Choose Columns. Opened by key, so the mnemonics are underlined, which
is how the capture has them. `pickshot.py` takes the frame whose size is the
reference's own, so neither of these has to name an index:

```console
$ WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/cc%d.bmp \
  WEEN32_SCRIPT="w:300 k:18 w:200 t:vc w:800" ./examples/explorer /tmp/many \
    >/dev/null 2>&1; \
  tools/refcapture/pickshot.py '/tmp/cc*.bmp' \
    tools/refcapture/columns-machine.png /tmp/cc.png && \
  PXDIFF_REF=tools/refcapture/columns-machine.png PXDIFF_OUR=/tmp/cc.png \
  tools/refcapture/pxdiff.py | head -2
reference (330, 313)   ween32 (330, 313)
differing pixels: 1106 of 103290 (1.1%)
```

Tools > Folder Options; its tabs are at y 42, at x 30 / 75 / 135 / 205. **The
sheet is 386x468** — this said 384x469 until 2026-08-29, wrong in both
directions, and nothing noticed because no recipe here diffed anything:

```console
$ WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/fo%d.bmp \
  WEEN32_SCRIPT="w:300 k:18 w:200 t:to w:900" ./examples/explorer /tmp/many \
    >/dev/null 2>&1; \
  tools/refcapture/pickshot.py '/tmp/fo*.bmp' \
    tools/refcapture/folderopts-machine.png /tmp/fo.png && \
  PXDIFF_REF=tools/refcapture/folderopts-machine.png PXDIFF_OUR=/tmp/fo.png \
  tools/refcapture/pxdiff.py | head -2
reference (386, 468)   ween32 (386, 468)
differing pixels: 656 of 180648 (0.4%)
```

Both are driven from the keyboard in a script: the menus by their mnemonics,
the lists by the arrows, a box by Space, and OK by Enter — which only works
because the sheet's tab ring goes tabs, page, buttons, so it is worth
checking that a Tab from the tab control lands on the page.

#### Properties

`tools/refcapture/properties-machine.png` is the machine's Properties for
`CONFIG.SYS` and `properties-boot-machine.png` is the same page for `boot.ini`
— one file with no program registered against it and one with, one hidden and
one not, one empty and one 203 bytes. Both frames are 367x443.

**The recipe below is a `console` block, which means `tools/freshdocs.py`
re-runs it and compares the output** — and it is written out in full because
the version that stood here until 2026-08-29 **could not be run**. It rendered
to `/tmp/pp%d.bmp` and then diffed `/tmp/ours.png`, a file no step in it
created; whether it failed or silently compared the machine's Properties
against whatever the *sampler* recipe had left in `/tmp` depended on what you
had done first. Nobody had run it since it was written.

```console
$ WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
  WEEN32_BMP=/tmp/pp%d.bmp \
  WEEN32_SCRIPT="w:300 k:40 k:40 k:40 k:40 k:40 w:200 a:13 w:900" \
  ./examples/explorer >/dev/null 2>&1; \
  magick /tmp/pp9.bmp /tmp/props.png; \
  PXDIFF_REF=tools/refcapture/properties-machine.png PXDIFF_OUR=/tmp/props.png \
  tools/refcapture/pxdiff.py | head -2
reference (367, 443)   ween32 (367, 443)
differing pixels: 652 of 162581 (0.4%)
```

Five downs picks CONFIG.SYS in the fixture's list and Alt+Enter opens it; four
downs picks boot. **`pp9` is the ninth thing that painted, and it is the sheet
because it is the only 367x443 one** — every one of these scripts leaves
several frames behind, and `tools/verify.sh` picks by size rather than by
index for that reason. The index is written here because a document should be
runnable by pasting; if it stops being the ninth, this block goes stale and
says so, which is the whole point of it being a `console` block.

| what | differing | what it is |
| --- | --- | --- |
| CONFIG.SYS | 640 | 453 the caption's bold title, 145 the icon quantised, 42 two glyphs |
| boot | 294 | 236 the two icons quantised, 48 the same two glyphs and the title |

**Re-checked on 2026-08-29 by the method that caught a wrong attribution in
WordPad's own file, and CONFIG.SYS's holds exactly** — every figure, and the
row band each of them lives in:

| rows | pixels | what |
| --- | --- | --- |
| 8..17 | 453 | the caption's bold title |
| 62..92 | 145 | the icon, quantised |
| 275..284, 301..310 | 21 + 21 = 42 | the two glyphs |
| 329..334 | 12 | **the day of the month** |

453 + 145 + 42 is the table's 640, and the twelve in rows 329..334 is the
whole of the drift `verify.sh` sees. **The number that moves is six rows
wide and nothing else in the sheet moves at all**, which is a better thing to
know than either total: anybody who sees this count change by something other
than a dozen in those six rows has found something real.

**Both were counted on the day the captures were taken, and neither is what
you will get.** The Accessed row is today's date on either side, so the count
moves with the calendar — up to 22 pixels where the day of the month is.
`tools/verify.sh` stores **652** and **306**, and both are exactly **twelve
more** than the two numbers above: the same drift, in both, from the same
cause.

So there are two authorities here saying different things and **both are
right, for different days** — which is worth naming because it is the shape
that wastes an afternoon. **`verify.sh` is the one to quote**; the numbers in
this table are kept for their third column, which does not move, and its own
output says *"Properties carries the day of the month in it and moves on its
own."*

Nothing else in either differs. The icons are the machine's own quantisation
— it draws them through a sixteen bit image list, so its pixel is ours with
the low three bits of each channel dropped — and the two glyphs are `J` and
`1`, which wine's Tahoma draws differently from the machine's: the `J` in
"July" descends below the baseline here and does not there.

The page is worth reading for what it taught, because most of it was not
about this dialog at all:

- **It is set in another face.** The template asks for "MS Shell Dlg 2",
  which resolves to Tahoma, where Folder Options asks for "MS Shell Dlg" and
  gets MS Sans Serif. A sheet takes its face from its page, so the tab and the
  three buttons along the bottom are in it too.
- **A line of text is not the strike's cell.** Both faces are eleven up and
  two down, but MS Sans Serif's cell is fourteen rows and Tahoma's twelve.
  The machine centres a button's label and a tick box's by fourteen in both,
  which is the ascent and the descent and the row between two lines.
- **Text is placed by what it draws.** A bitmap face measures as it draws, but
  Tahoma is measured off its outline and comes out wider — three pixels wider
  across "Cancel". The machine centres that label by the drawn width, and
  sizes a tab and a focus rectangle by it too: "General" in a Properties
  sheet's tab is 49 wide, its drawn width and the twelve either side. A list
  row's name box is the same rule with MS Sans Serif, where the extent runs
  wider still — six pixels across "CONFIG.SYS": the six names in the
  machine's C: window come to 30, 42, 61, 69, 72 and 102 pixels of blue, and
  each is the drawn width and eight, two before the name and six after.
- **A focus rectangle's dots start at the control's corner** — for a button.
  win32 hangs the pattern on the brush origin, and a tick box's is its own:
  two at different places dot theirs the same way, and hanging them off the
  window's corner instead inverts one of the two, which is what the machine
  showed against ours (Folder Options goes 1224 / 1407 / 1037 to 1250 / 1645
  / 1249 that way). A view's do not. A list's and a tree's keep the chequer
  the whole window shares, which Column Settings settles: the picked "Name"
  row's dots invert when they are hung on the list's corner, 1147 to 1229.
  Two rules, both measured, and nothing here says which a control follows.
- **An etched line is a frame two pixels thick**, which is what puts the
  highlight round its far end.
- **A tab control draws its own frame**, and lays the sides down in an order
  no other frame uses: the shadowed ones first, so the white top line keeps
  the corner at the top right and the white left one keeps the corner at the
  bottom left. Every other frame in the window is the other way round, and
  moving them all was worth 4 pixels here and 50 the wrong way elsewhere.
- **Alt with any key brings the underlines out.** Alt+Enter opens this sheet
  without going near a menu, and the machine's has them.

Two of those move pixels in the wine samplers, deliberately: see below.

#### The tips

Rest the pointer on a toolbar button that is a picture and nothing else and the
machine names it in a little pale-yellow window; rest it on a name the columns
have cut short and the whole name appears in the place it was drawn. Both are
the same box, and both are measured off the machine:

| what | the machine's | how it was read |
| --- | --- | --- |
| the box | 1px black line, `COLOR_INFOBK` inside, 17 tall | a capture of "Delete" and one of "Copy To" |
| its width | the words as they draw, and six | 31 → 37 and 40 → 46 |
| the words | three in from the line, two down | the same two |
| a button's tip | at the pointer, 21 pixels down | the pointer's own position in the shot |
| against the right edge | pushed left, one column of screen kept clear | a button at x 1013 whose tip starts at 986 |
| a name's tip | over the name, so the full one lands where the short one was | the list unfolding "Documents and S..." |
| how long | about half a second to show | four shots at 0.5, 0.8, 1.2 and 2.0 seconds |

A button that wears its own label gets none: hovering Search or Folders on the
machine shows nothing where hovering Delete or Views shows a tip.

Counted against the machine's own capture, our "Delete" tip differs by **one
pixel of 629** — and that one is a colour the machine quantised.

The tips are driven from a script the same way anything else is:

```sh
# rest on the Delete button; the 37x17 frame is the tip
WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
  WEEN32_BMP=/tmp/tp%d.bmp \
  WEEN32_SCRIPT="w:400 m:300,60 w:200 m:395,60 w:900" ./examples/explorer
```

#### Renaming in place

F2 over the list, or a click on the name of the file already picked, opens a
box over that name to type a new one in. The machine's box was measured on
its C: window and ours is the same to the pixel:

- it is **two pixels further left** than the name's own blue box, **twelve
  wider**, and exactly as tall as the row — 81x17 over a 69-wide box for
  `CONFIG.SYS`, 84 over 72 for `Program Files`;
- one pixel of black around it, white inside, and the name in it picked out
  in the selection colours — a band thirteen rows tall, two below the top of
  the box;
- the name lands **one pixel right** of where the row drew it, which is the
  margin the edit control keeps anyway;
- and the row keeps its icon and its other columns, with no highlight left
  under the box.

```sh
# five downs picks CONFIG.SYS in the fixture's list; F2 is 113
WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
  WEEN32_BMP=/tmp/ed%d.bmp \
  WEEN32_SCRIPT="w:300 k:40 k:40 k:40 k:40 k:40 w:300 k:113 w:600" \
  ./examples/explorer
```

**This one was prose for weeks, and the reason is worth keeping even though it
has been fixed: there was nothing to diff it against.** The rename box is not
a window of its own — the run leaves eleven frames and every one of them is
the explorer's own 654x544, so `pickshot.py` has nothing to pick — and a
whole-frame count would drown seven pixels of box in a window's worth of
everything else. The list above is a set of *relations*, and the entry here
used to say that relations of that kind could not be checked from this
repository at all: *"ours is the same to the pixel" is a claim about a
comparison somebody made once, on a machine, and nothing here can re-make it.*

**What it wanted was not a picture but a way to make the comparison again.**
Both now exist. `tools/refcapture/renamebox.py` takes two captures of the same
rectangle — the row picked, then `F2` pressed — and reads the seven relations
off them, none of which mentions a coordinate; so **the same instrument
measures the machine's captures and ours**, at whatever size either window
happens to be. `verify.sh` runs it on all three pairs on every branch:

```
== the rename box ==
  machine CONFIG.SYS     all 7
  machine Program Files  all 7
  ours                   all 7
```

The machine's captures, taken on Windows 2000 on 2026-08-29, are
`rename-config-{row,box}-machine.png` and `rename-pf-{row,box}-machine.png`.
Both agree with the numbers written above them — an 81x17 box over a 69-wide
label for `CONFIG.SYS`, 84 over 72 for `Program Files` — and **ours reads
69x17, 81x17 and a 61x13 band, which is the machine's, exactly.**

**The reference captures are run through the instrument too.** They are where
the numbers came from, so if one is ever replaced by a capture taken in some
other state the run says so, instead of quietly moving the target — which is
what a stored PNG on its own would have let happen.

**Finding the box has to come before finding the label**, which is the reverse
of how the list above describes them. The natural order needs somewhere to
start looking and the only landmark is the label, whose distance from the box
is the very thing being measured. So the box is identified without it: a
rectangle whose top and bottom edges are the same horizontal run, one row
tall, **with a selection inside it**. The last clause is what makes it work on
a whole frame — a button or a sunken pane is also a matched pair of runs, and
neither has a highlight in it.

An earlier note here recorded that a 100x26 strip around the box differed by
**113 pixels of 2600**, all of them in the seven columns of the file's icon,
which is the quantisation the whole fixture has. That count is still true and
is now the less useful of the two numbers: it is dominated by an icon nobody
is measuring, where the seven relations are about the box.

The click that opens it is not the first one: the view waits out the
double-click time after a press on the name already picked, because a second
press within it is a double click and opening what it is on comes first. The
machine's box appears between 450 and 550 ms after that click, timed by
taking the screen 50 ms at a time.

#### The bars that can be put away

View > Toolbars turns each band off and on. With the address bar off the
toolbar's band ends and the panes begin straight under it — no strip of what
was there before:

```sh
WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
  WEEN32_BMP=/tmp/ab%d.bmp WEEN32_SCRIPT="w:300 k:18 w:150 t:vta w:800" \
  ./examples/explorer
```

Beside the machine doing the same — its own View > Toolbars > Address Bar —
the two bands agree everywhere but the menu's underlines: ours keeps them out
after the menu closes and the machine puts them away again, while a dialog
opened the same way keeps them on both. The state win32 keeps per window is
one flag here; see the roadmap.

#### Carrying a band by its gripper

Watched on the machine, in My Computer's rebar — the menu band, the toolbar
and the address bar. `tools/vm/drive.py` again, because none of this can be
seen without holding the button down and taking a picture in the middle of
it:

```sh
JSLINUX_SOCK=/tmp/jslinux-mcp.sock JSLINUX_SHM=/dev/shm/jslinux-mcp.fb \
  tools/vm/drive.py press 141,191 sleep 150 \
  holdmove 300,167 sleep 200 shot /tmp/mid.png 134,158,650,74 \
  release sleep 400 park shot /tmp/after.png 134,158,650,74
```

With that window at 133,136 the three grippers are one white column at x=140,
and the bands run 158..179, 182..203 and 206..227.

**A gripper is one gesture with two axes.** Five things measured:

1. **The pointer over a gripper is the column-divider cursor** — a vertical
   bar with an arrow either side, the same shape a list view's header divider
   wears. Not a move cursor and not a hand. It is the first thing that says
   this is a resize as much as a move.
2. **It follows, and it is not a ghost.** The real layout reflows while the
   button is down: bands resize, rows appear and close. The shot taken
   mid-drag and the shot after the release are the same picture. This is the
   opposite of the column drag above, which *does* carry a ghost.
3. **Along a row it resizes**, taking width off the band to its left. With the
   menu and toolbar sharing a row, carrying the toolbar's gripper left walked
   the menu down its chevron states — `File Edit View Favorites Tools Help` →
   `File Edit View Favo »` → `File Edit »` → `File »` — and carrying it right
   gave the width back and squeezed the toolbar to `←Back ▾ »`.
4. **Up and down it moves.** Carried onto the row above, the band goes
   *beside* the one already there and the row it left closes up — three rows
   became two. Carried below the last row it gets a row of its own, and the
   order changes with it.
5. **Past the end of a row it clamps**, and **a band left alone on a row takes
   the whole width back**. Pointer at 775 and pointer at 900 — well past the
   window's right edge at 783 — gave identical layouts: the band shrinks to
   its smallest and stops, rather than wrapping or vanishing.

**The known difference.** The boundary does not track the pointer on the
machine. Pressing a gripper whose edge is at 296 with the pointer at 297 is a
grab offset of one pixel, but dragging to 240 left the edge at 262 and
dragging to 180 left it at 196 — out by 22 and by 16. The reading that fits
those four measurements is that the edge follows the pointer but is clamped by
the left neighbour's current smallest width, and that smallest falls in steps
as that neighbour's toolbar sheds buttons into its chevron. ween32 has no
chevron, so a band here has one floor where the machine has a staircase: the
band's own handle and name, and the edge. Everything else about the gesture is
the machine's; this one part is a straight line where the machine has steps,
and it is the thing to fix when a toolbar learns to chevron.

#### Moving a column

A heading dragged sideways carries its column, cells and all — the shell's
Details view does it and so does this. What the machine draws while it is
being carried was captured mid-drag, with `tools/vm/drive.py`, which is the
only way to hold a button down and take a picture at the same time:

```sh
JSLINUX_SOCK=/tmp/jslinux-mcp.sock JSLINUX_SHM=/dev/shm/jslinux-mcp.fb \
  tools/vm/drive.py press 600,241 holdmove 480,241 holdmove 420,241 \
  shot /tmp/drag.png release
```

The heading follows the pointer as a ghost — every one of its colours half
way to COLOR_3DSHADOW, which is what makes (212,208,200) come out
(173,173,165) in a 5-5-5 shot — and a two-pixel bar in (64,64,191) stands at
the boundary it would go to, drawn over the ghost. Headless, the same drag is

```sh
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/hd%d.bmp \
  WEEN32_SCRIPT="w:300 d:500,105 m:460,105 m:380,105 m:300,105 u:300,105 w:400" \
  ./examples/explorer /tmp/somefolder
```

### The edit's scroll bar, beside the machine's Notepad

The library's own Notepad clone put a real win32 program on top of ween32, and
the machine runs the same program's ancestor -- so its edit control is
measurable the same way the explorer's panes are. Start Notepad on the
machine, fill it with numbered lines, and read the bar off the screen:

```sh
JSLINUX_SOCK=/tmp/jslinux-mcp.sock JSLINUX_SHM=/dev/shm/jslinux-mcp.fb \
  tools/vm/drive.py click 300,200 type "$(seq -f 'line %02g' 0 79)" \
  wait 1200 park shot /tmp/w0.png
```

The window as it opens has a client area of **740 x 472** with a thirteen
pixel line, so **36 lines show**. With 83 lines in it, four things:

1. **An arrow is one line.** The top line read `line 44`; one click on the up
   arrow and it read `line 43`.
2. **The thumb is `MulDiv(page, track, lines)`** -- the formula the shared
   scroll helper already used, guessed from the views and unchecked until
   now. Bar 131..603 with sixteen-pixel arrows is a track of 441, and
   36 x 441 / 83 = 191.3; the thumb measured 191 pixels.
3. **A click in the track is a screenful less one line**, not a screenful:
   `line 43` to `line 08` is 35 with 36 showing. The line that was at the
   bottom is the line at the top, so the eye keeps its place. ween32 moved a
   whole screenful until this was measured.
4. **The view follows the caret to the line.** Typing past the bottom left
   the top line at exactly lines minus visible, which is what
   `edit_scroll_into_view` computes -- a rule that came free with the other
   three.

**The caption, with an icon in it.** Notepad is the first program here to
have one, and the layout can be counted against the machine's the same way.
With both windows at their own origin, the caption's gradient begins to
change at column 27 on both -- so the room kept for the icon is the same to
the pixel -- and the title's glyphs occupy exactly the same columns, 24 and
25 solid, 26 to 28 one pixel each, 29 and 30 solid. The pictures differ,
this clone's icon not being the machine's, and nothing else does.

The wheel is three lines a notch, measured with `drive.py`'s `wheel` verb --
the daemon had the command all along and the driver had no word for it. One
notch up took the top line from 44 to 41, a second to 38, and two notches
down took it back to 44, so it is three a notch and a notch count multiplies.

Point 3 above is the one that had to be re-measured, twice: see [Which
controls page by a whole screenful, and which by one
less](#which-controls-page-by-a-whole-screenful-and-which-by-one-less).

### Which controls page by a whole screenful, and which by one less

A click in a scroll bar's track moves a page. *How much a page is* turned out
to differ between controls, and the answer took three of us and two wrong
methods to get right, so the method matters as much as the numbers.

**What does not work.** Dividing a client height by a row height and calling
the answer "rows visible" is off by one whenever the last row is clipped, and
it usually is. Reading the row names off the picture and judging by eye which
is the last *whole* one is off by one too: a row's ink sits in the top of its
box, so a row can have every pixel of its letters drawn and still be cut.
Both mistakes were made here, in that order.

**What works** is a comparison with no judgement in it. Before the click, and
after it, crop the strip of the row at the top afterwards and find which row
before the click it is *pixel-identical* to. Then measure the client's last
pixel and the row grid, and say whether that row was whole. Nothing is
divided, nothing is eyeballed.

    A = before, B = after; rows are `pitch` tall from `origin`
    for k in range(...):
        if B.crop(x0, origin, x1, origin+pitch) == A.crop(x0, origin+pitch*k, ...):
            that is the row that moved to the top

Measured that way on Windows 2000:

| control | client | rows | whole | moved | rule |
| --- | --- | --- | --- | --- | --- |
| Notepad's **edit** | 132..603 | 13 from 133 | 36 | **35** | a screenful less one |
| explorer's **tree** | 208..490 | 16 from 208 | 17 | **16** | a screenful less one |
| explorer's **list view** | 204..439 | 17 from 209 | 13 | **13** | a whole screenful |
| a **list box** (Notepad's Font) | 289..379 | 13 from 289 | 7 | **6** | a screenful less one |
| a **combo's dropped list** (Time Zone) | 212..406 | 13 from 212 | 15 | **14** | a screenful less one |

The last two are the easiest of the five to trust, because **their clients are an
exact multiple of the row pitch** — 91 = 7x13 and 195 = 15x13 — so there is no
partial row and the question the other three had to be careful about does not
arise. Each was measured twice, a second page after the first, and the matching
strip differed by **0** pixels where every other row differed by hundreds.

**A hazard peculiar to a combo's dropped list: the highlight follows the
pointer.** A strip can therefore differ from its own earlier self for a reason
that has nothing to do with scrolling. It is visible in the numbers — matching
the top strip against each row, the highlighted row came back **4,312** pixels
different where the ordinary mismatches were 270 to 500. It did not reach the
answer here, the match being elsewhere and exact, but a run where the highlight
lands on the row being matched gives a false negative that looks exactly like a
whole screenful. Keep the pointer on the scroll bar, which a track click does
anyway.

The list view's is Dan's, measured twice at two window heights; the other four
are on the strip test above. So the edit and the tree overlap a row and the
list view does not — two rules in the same window, both read off the same
machine. ween32 follows each control's own.

The clamp is the other trap: a control cannot move a page it has not got. A
tree of 38 items showing 17 can only move 21, so a "page" measured from
halfway down measures the end of the range instead. Page from the top, with
more than two screenfuls below.

### Notepad's Find and Replace, as the machine draws them

`tools/refcapture/find-machine.png` (360x126) and `replace-machine.png`
(351x178) are the two boxes cut out of the machine's own Notepad, whole
windows including their frames. Ours are held against them pixel for pixel:

| box | differing | of | what is left |
| --- | --- | --- | --- |
| Find | **83** | 45360 | 64 the bold `F` of the title, 19 four letters |
| Replace | **57** | 62478 | the same letters, in this box's words |

Every rectangle agrees, and every one of them is Windows' own: the probe was
run against both boxes inside the guest, so the units below are read rather
than fitted. What differs is the strike — our `M`, `w`, `N`, `A` and `R` put
a diagonal's step on a different row from the machine's, and the caption's
bold `F` is a column wider, which shifts the rest of the title.

**Taking the captures.** The box has to be caught in the state a program's
own call puts it in, because four things about it are state and each one
costs pixels:

```sh
export JSLINUX_SOCK=/tmp/jslinux-bob.sock JSLINUX_SHM=/dev/shm/jslinux-bob.fb
tools/vm/drive.py click 300,200 wait 400 key KeyF:ControlLeft wait 1500 park
tools/vm/drive.py key AltLeft wait 700 park          # the underlines on
tools/vm/drive.py shot /tmp/find.png 45,157,360,126  # until the caret is in
```

- **Never let it lose the keyboard.** A box that is deactivated and made
  active again by a click comes back *without the black ring round its
  default button*, and does not get it back until the focus moves — Tab and
  Shift+Tab both restore it. A fresh box has the ring even though Find Next
  is greyed out, which is what ween32 draws. Moving Notepad out from under
  the box costs the ring; maximising Notepad and taking the box over its own
  white client costs nothing, because the crop is found by structure.
- **Do not click the box to activate it** — the blank face between the tick
  box and the group looks safe and is not: the click that lands on `Match
  case` ticks it and leaves a focus rectangle round its label.
- **The mnemonic underlines are their own state.** Ctrl+F after a run of
  mouse work opens a box with no underlines at all; a press of Alt turns them
  on. 41 pixels, and they are the ones a reader will call a font bug.
- **The caret blinks.** Shoot until column 77 (Find) or 87 (Replace) is
  black; ween32's render always has it.

**Finding the window in the screenshot.** Not by trimming to white — that is
what cost the last attempt two columns, because the frame's outermost pixel
is the face colour and the window behind it is white. The dialog's own
corners say where it is: the top-left pixel of the window rect is
`COLOR_3DFACE` and the bottom-right is `COLOR_3DDKSHADOW`. Over a maximized
Notepad the box is the only thing that is not white, so its bounding box is
the window rect, and the two corners check it.

**The rectangles, as the template says them.** At 96 dpi this dialog's base
units are 6 across and 13 down, so `MulDiv(u, 6, 4)` and `MulDiv(v, 13, 8)`
map them, position and size separately, and the client's origin in the window
is (3, 22). Find is 236 x 62 units and Replace 230 x 94 — both exact: 354 and
345 pixels of client width, 101 and 153 of height.

| Find | x, y | cx, cy | the pixel it lands on |
| --- | --- | --- | --- |
| `Fi&nd what:` | 4, 8 | 42, 8 | ink from 10, 37 |
| field | 47, 7 | 128, 12 | 74..265, 33..52 |
| `Match &case` | 4, 42 | 64, 12 | box at 9, 93; label ink 28 |
| `Direction` group | 107, 26 | 68, 28 | 164..265, frame top 70, bottom 109 |
| `&Up` | 111, 38 | 25, 12 | circle at 171, 87; label rect 188 |
| `&Down` | 138, 38 | 35, 12 | circle at 211, 87; label rect 228 |
| `&Find Next` | 182, 5 | 50, 14 | 276..350, 30..52 |
| `Cancel` | 182, 23 | 50, 14 | 276..350, 59..81 |

| Replace | x, y | cx, cy | the pixel it lands on |
| --- | --- | --- | --- |
| `Fi&nd what:` | 4, 9 | 48, 8 | ink from 10, 39 |
| field | 54, 7 | 114, 12 | 84..254, 33..52 |
| `Re&place with:` | 4, 26 | 48, 8 | ink from 10, 66 |
| field | 54, 24 | 114, 12 | 84..254, 61..80 |
| `Match &case` | 5, 62 | 59, 12 | box at 11, 126; label ink 30 |
| `&Find Next` | 174, 4 | 50, 14 | 264..338, 29..51 |
| `&Replace` | 174, 21 | 50, 14 | 264..338, 56..78 |
| `Replace &All` | 174, 38 | 50, 14 | 264..338, 84..106 |
| `Cancel` | 174, 55 | 50, 14 | 264..338, 111..133 |

The mnemonic in the first label is the **n**, not the d: the machine
underlines `Fi_n_d what:` in both boxes.

**A field's own margin comes from its font, and a bitmap face gets none.**
The caret of an empty Find field stands in column 77, one pixel inside a
client that begins at 76, and Replace's two fields put theirs the same one
pixel in. Half an average character — what wine's edit control uses, and what
this library used — would have put it at 80. But the shell's Properties page,
drawn in Tahoma, *does* have half a character: `CONFIG.SYS` starts three
pixels inside its field. Win32 sets these default margins from the font when
the control is made and gives them only to a scalable face; the strike flag
`bitmap_only` is the same question, so that is what ween32 asks. The two
dialogs disagree because their fonts do, and both now agree with ours. A
blanket margin of nothing was tried first and put Properties at 878 where it
had been 640.

The rounding is confirmed twice over on the way: `MulDiv` rounds a half away
from zero, and the fields prove it inside these very boxes — Find's at unit
47 lands on 71 rather than 70, Replace's tick box at unit 5 on 8 rather than
7.

### What a Rich Edit 2.0 does, asked of riched20 itself

`tools/vm/ctlprobe.c` creates the controls in its own process, so the
questions that decide a *text model* — where a run of formatting begins and
ends, what a character typed at a boundary takes its formatting from — can be
put to riched20 directly and the answers written down. None of them can be
read off a picture. Built and run the way its header says, with a `richedit`
section that prints what follows.

**A run split, and identical neighbours merged.** The RTF the control streams
out is the run structure written down. Bolding characters 5..10 of twenty:

```
\pard\f0\fs17 abcde\b fghij\b0 klmnopqrst\par
```

then bolding 10..15 as well:

```
\pard\f0\fs17 abcde\b fghijklmno\b0 pqrst\par
```

— one group, not two, so **riched20 merges a run with an identical
neighbour**. And taking bold away from the middle of that stretch, 8..12,
gives five runs back:

```
\pard\f0\fs17 abcde\b fgh\b0 ijkl\b mno\b0 pqrst\par
```

So a formatting command splits the runs it lands inside and coalesces what it
leaves the same. A model that only ever splits will drift into thousands of
runs saying the same thing.

**What `EM_GETCHARFORMAT` says over a selection that spans two runs.** Over
one run, `dwMask` is `f800003f` — every attribute it knows. Across a
boundary between bold and plain it is `f800003e`: **`CFM_BOLD` is cleared**,
and `dwEffects` still carries the first run's value. So a format bar reads
the *mask* to decide whether Bold is in, out, or neither, and reading
`dwEffects` alone would show it in.

**What a character typed at a boundary takes.** The caret between a bold run
and a plain one, and an `X` typed:

```
\pard\b\f0\fs17 abcdeX\b0 fghij\par
```

— bold. **The formatting comes from the character before the caret**, not the
one after. And `EM_SETCHARFORMAT` with `SCF_SELECTION` on an *empty*
selection sets the format the next character will be typed in, which is how a
format bar's Bold button works with nothing selected.

**A fresh control's own format** is the message font: face `Tahoma`, `yHeight`
165 twips, `dwEffects` `40000000` — `CFE_AUTOCOLOR`.

**And a rich edit keeps no margin of its own.** With the same Tahoma in both,
`EM_POSFROMCHAR` puts the rich edit's caret three pixels left of the EDIT's
at the same offset: the EDIT has half an average character and the rich edit
has none. Which is what ween32 does — see `edit_default_margin`.

### What a paragraph mark is, and why every offset turns on it

**Rich Edit 2.0 keeps a paragraph mark as a single carriage return.** Every
offset it states -- a selection, `EM_LINEINDEX`, `EM_GETSELTEXT` -- is in that
numbering, while `WM_GETTEXT` hands the text back with the CRLF a program
handed in. `ctlprobe.c` shows both halves at once:

```
set "one\r\ntwo":  WM_GETTEXTLENGTH 8, WM_GETTEXT 8 bytes
                    6f 6e 65 0d 0a 74 77 6f          "one" CR LF "two"
EM_GETSELTEXT of 2..6 answers 4 bytes
                    65 0d 74 77                      "e"   CR    "tw"
"one\r\ntwo\r\n" is three lines and the last begins at 8
```

Eight bytes out, four bytes for the range 2..6, and a third line beginning at
8 -- which only counts if each mark is one character. It matters more than it
looks: a program that finds an offset in the text it read and hands it back
to the control has to mean the same character on both sides, or the same
source behaves differently on Windows and here, which is the whole promise.

One consequence inside ween32: the rich edit stopped answering line questions
from the shared `ween_text_line_*`. Those count a CRLF as one break and this
control has not got a CRLF to count, so `EM_LINEINDEX` and its neighbours
come from the control's own line table. Sharing them was right while both
controls kept their text the same way; it stopped being right the moment the
machine said otherwise.

### What a paragraph carries, and what happens at its edges

Four rules, all `ctlprobe.c`'s answers:

- **A command takes whole paragraphs.** Centring one character of the middle
  paragraph of three centres the whole of it and leaves its neighbours alone;
  a selection reaching one character into the next paragraph takes that one
  whole as well.
- **Read across paragraphs that differ, the mask bit is cleared** -- the same
  rule a `CHARFORMAT` follows. A fresh control answers mask `8001003f`, left,
  no indents, no tab stops; across three that disagree it answers `80010037`.
- **A paragraph split in two leaves both halves carrying what the whole one
  carried.** A return typed in a centred paragraph gives two centred ones.
- **A join keeps the first one's.** A backspace over the mark between a
  left-ranged paragraph and a right-ranged one leaves a paragraph that is
  ranged left.

The last two are what say a paragraph's formatting lives with the mark rather
than with the text around it, and they are why ween32 keeps a paragraph array
beside the run array and maintains it with exactly those two events.

Two things about paragraphs the probe has *not* been asked, marked so that
nobody reads them as measured: where the values come from when
`EM_GETPARAFORMAT` is read across paragraphs that differ (ween32 answers with
the last one's, as it does for a character format), and whether a selection
ending exactly on a paragraph's first character takes that paragraph.

**The second of those has now had a run of the probe that did not answer
it**, which is worth writing down because the reason is a trap anybody can
fall into here. The probe set `0..5` on "one\r\ntwo\r\nthree" and reported
that the second paragraph took the command -- but a mark is stored as a
*single* CR, so "two" begins at 4 and `0..5` already had a character of the
second paragraph in it. That is the case ween32 was already built for and
`tests/richedit_test.c` already asks. The question is `0..4`, and the probe
now asks that as well as the old one; until it is run, ween32's rule stands
where it was, `paras_set` taking the paragraph of `to - 1`.

### Where a tab puts the text

Every number here is `EM_POSFROMCHAR`'s, out of `ctlprobe.c`'s `tabs` block,
in a control whose client is 556 wide and whose text begins at 1. Because
the texts are tabs and nothing else, none of it depends on the strike.

```
nine tabs, no stops of their own   1 49 97 145 193 241 289 337 385 433
stops at 300, 1000, 2137 twips     1 21 68 143 145 193
one stop at 500 twips              1 34 49 97 145 193
cleared again                      1 49 97 145
"ab<tab>cd<tab>ef<tab>gh"          1 7 13 49 54 60 97 103 107 145 151
seven w's, then a tab              ... 57 97   (with or without a stop at 300)
a stop of 300 on the first of two  1 21 25 | 1 49 53
```

- **The default is half an inch**, 48 pixels at 96 dpi, measured from the
  text's own left edge: hence 1, 49, 97 in a control whose text begins at 1.
- **A paragraph's own stops come first**, in twips, and the pixel is
  `rfmt_px_twips`'s: 300 -> 20, 1000 -> **67**, 2137 -> **142**. Not the
  floor -- 1000 twips is 66 and two thirds and the control puts it at 67 --
  and not the ceiling either, since 2137 is 142 and a half and it puts it at
  142. MulDiv's rounding, which is the same rounding a dialog unit takes.
- **Past the last stop of its own the grid takes over again**, measured from
  the same left edge and not from that stop: one stop at 500 twips gives 34,
  and then 49, 97, 145.
- **A stop the pen has already passed is skipped.** Seven w's reach 57 and
  the tab after them goes to 97 whether the paragraph's only stop is at 300
  twips or it has none.
- **Stops belong to the paragraph.** A stop set on the first of two leaves
  the second on the default grid, and the RTF riched20 writes says the same:
  `\pard\tx300\f0\fs17\tab .\par \pard\tab .\par`.
- **A tab that would land past the edge takes the line with it.** In a
  control 116 wide, four tabs and a stop are two lines, `[0,2]` and `[2,3]`:
  the third tab would have gone to 145, so it begins the second line instead
  -- at 1, and advancing to 49 from there.
- **A click inside a tab's stretch follows the rule every character
  follows**: the nearer of the two ends, and the middle itself goes left.
  Swept a pixel at a time on "a<tab>b", whose 'a' spans 1..7 and whose tab
  spans 7..49, the caret turns at 5 and at 29 -- one past each middle. The
  same sweep of "abcdef" turns at 5, 11, 16, 22, 28, 33 against characters
  at 1, 7, 13, 18, 24, 30, which is that rule and not any other.
  `EM_CHARFROMPOS` answers the same numbers as a click.

What is *not* measured, and is left as it stands: whether a tab is a place a
line may break the way a space is (ween32 breaks at the last space, and a
tab only breaks the line when its own stop is past the edge), and what the
top byte of a stop -- its alignment and leader in Rich Edit 2.0's
documentation -- does, since nothing here has set one. The position is taken
and the rest dropped.

### The selection bar, and jd's seven pixels

**Driving the program by hand found what four instruments could not**: the
text in WordPad's editor does not line up with the ruler above it. What it
turned out to be is not a margin and not an indent but a **style bit**, and
it was sitting in the style word this repository has quoted all day.

```
EM_GETMARGINS on the machine's own WordPad editor    left 0  right 0
Format > Paragraph                                   Left 0"  Right 0"  First 0"
the editor's style word, read by probe.c             550081C4
                                                     ^ 0x01000000 = ES_SELECTIONBAR
```

`ES_SELECTIONBAR` is the strip down the left where the pointer becomes an
arrow and a click takes a whole line. Asked of riched20 with the same
control either way and nothing but the bit different:

```
plain RichEdit20A                                    first character at x=1
with ES_SELECTIONBAR                                 first character at x=9
WordPad's exact style word, ex 210, Arial 10, 760 wide             x=9
  the same without ES_SELECTIONBAR                                 x=1
  the same without 0x04000000, and without ES_SAVESEL              x=9
with a selection bar and Arial 10 rather than the message font     x=9
```

**Eight pixels, and not a function of the font.** `EM_GETMARGINS` still
answers nought with the bar on, so it is not a margin under another name.
ween32 scales the eight through `ween_ncm` the way it scales every other
measured metric; what the bar is at another dpi has not been asked.

**Five pixels are still unexplained and belong to the frame, not the
control.** WordPad's own editor puts its first character at client **x=14**
-- measured off the ink at screen 175 with the ruler's marker apex at 174 --
where a control built with its exact style word puts it at 9. Not a margin,
not an indent, and not the wrap mode: that WordPad's Rich Text page is on
*Wrap to window* was read off the box. The only standard mechanism left is
`EM_SETRECT`, and `EM_GETRECT` takes a pointer, so it cannot be asked across
a process. **Eight of jd's pixels were this library's and five are
WordPad's.**

### What the machine's WordPad does that no capture shows

Four answers from the same boot, none of them a pixel:

- **Find wraps.** With the caret at the end of "cat dog cat", Ctrl+F, "cat",
  Find Next selects the **first** one.
- **A search that fails** puts up a message box titled `WordPad`, with an
  information icon and one OK button: **"WordPad has finished searching the
  document."** -- not "cannot find", and the same words whether or not
  anything was ever found.
- **Find Next is greyed until a search has been made**, where Find and
  Replace need only a document with text in it -- and the flag **outlives
  File > New**, since what the program remembers is the search string rather
  than the document.
- **A ruler marker dragged off the ruler** stops: the left indent at zero
  going left and at the right indent going right; the right indent at **7.25
  inches**, which is 8.5" of paper less its 1.25" left margin -- the paper's
  own right edge, not the ruler's end.

### The Font box, counted

`tools/refcapture/font-machine.png` is WordPad's own Font dialog on the
machine, **437x344**, the whole window including its frame. Ours is rendered
by driving WordPad headlessly and keeping the frame the dialog appears in:

```sh
cd wordpad && zig build
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/fb%d.bmp \
  WEEN32_SCRIPT='w:200 a:79 w:200 k:70 w:600' ./zig-out/bin/wordpad
# a %d in the path writes one file per frame, which is the only way to catch
# a modal window; the 437x344 one is the dialog.
```

```
before CBS_SIMPLE  32,050 of 150,328   21.3%
  the three lists  28,416   ween32 had no CBS_SIMPLE
  the note            954   the static is there and its text is not
  everything else   2,680   of which 60 are the caption's bold title

after              9,614 of 150,328    6.4%
  the three lists   5,623   what is left is the *contents*: our two faces
                            against the machine's seven, our sizes against
                            its, and the OpenType marks beside its names
  the note            954   unchanged, and still ours to fix
  everything else   3,037

with the note written 9,635 of 150,328   6.4%
  the note            975   **up 21, and this is the right direction**
  everything else   8,660   unchanged to the pixel
```

**The 28,416 were one missing style bit, and it is implemented now.** The machine's Font, Font style and
Size controls are `ComboBox` with **`CBS_SIMPLE`** -- `probe/font.txt` reads
`50010B51`, `50010241`, `50010B51`, and `0x0001` is `CBS_SIMPLE` -- which
draws an edit with its list **always open below it**. ween32's combo box
treated every combo as a dropdown, so where the machine showed three lists of
fonts, styles and sizes, ours showed three closed fields and dialog face. The
Color and Script combos are `50010253`, `CBS_DROPDOWNLIST`, and those two ours
already drew correctly.

**A simple combo is a different control rather than a differently-drawn one**,
which is what the implementation had to follow: it keeps the height it was
made with instead of shrinking to its field, it has no button, it wears two
sunken frames of its own rather than one client edge round everything, and
its list is part of the box instead of a window that appears over other
things. Every number in it is the machine's: `147x116` for the box, `141x15`
at `(3,3)` for the field -- the whole width less six, the field's band less
six -- which `tests/comdlg_test.c` asserts against a dropdown beside it, so
the difference is the style and not the class.

**The 954 were the note** at the bottom, which is static 1093 in the
template, and ween32 created the static and never gave it text.

**It is written now, and the count went up rather than down.** That is not a
regression and it is worth the space, because a number moving the wrong way
is normally the thing this file exists to catch.

The note is **a rule and not a string**, read verbatim off the machine with
`probe.exe` at three selections:

```
Arial           "This is an OpenType font. This same font will be used on
                 both your printer and your screen."
MS Sans Serif   "This is a screen font. The closest matching printer font
                 will be used for printing."
Courier         the same string as MS Sans Serif, exactly
```

So it is keyed on the font's **kind**, not on the font. The control is
present, and in the same place, in all three dumps -- `334,552 329x33 style
50000080` -- which is how *"the static is hidden"* was ruled out: an empty
string and an absent control look identical in a screenshot and are two lines
apart in a probe.

**And that is why the count rose.** `font-machine.png` was taken with **Arial**
selected, so the reference carries the OpenType string; ours opens on MS Sans
Serif 8 and now correctly carries the screen-font one. Blank-against-text was
954 differing pixels; text-against-different-text is 975. **The note has moved
out of "missing" and into the state difference named below** -- it cannot
reach zero until the two boxes open on the same font, and then it should reach
it exactly.

**Two limits, because the rule is two samples wide.** One OpenType face and
two bitmap ones were read; TrueType-but-not-OpenType was not looked at and may
be a third string. Whether the wording changes with a printer installed was
not looked at either, and both strings are about printing. **The OpenType
branch is also unreachable in ween32 today**, since both faces it offers are
bitmap strikes -- it is written as the rule so a scalable face carries the
note with it, but nothing that opens the box exercises it.

**And 415 of the rest are state rather than drawing**: WordPad's box opens on
Arial 10 and ours on MS Sans Serif 8, so the three fields hold different
strings. That is the frame's call and the control's `CF_INITTOLOGFONTSTRUCT`
handling, not the dialog's geometry.

**The capture was wrong when it was first taken, and the way it was wrong is
written in this file two sections up.** It was cropped by walking out from
inside the dialog until the pixels stopped being the dialog's -- and **a
dialog's frame has a white highlight in it**, so the walk stopped on the
frame's own second row and cut two columns and two rows off the top and left.
The corner check passed anyway: the truncated rectangle's corners are also
`COLOR_3DFACE` and `COLOR_3DDKSHADOW`. What settles it is the *outside*: at
any row through the dialog's middle the pixels left of it are the white of
WordPad's client, and the dialog begins at the first `COLOR_3DFACE` after
them. Corrected, it is 437x344 -- **exactly the size ween32 draws**, so the
outer geometry agrees and the first version of this section would have
reported a two-pixel disagreement that was mine.

### What a search does

`EM_FINDTEXTEX`'s own answers, out of `ctlprobe.c`'s `finding` block, against
"one cat two Cat three catalog cat." -- lower `cat` at 4, upper `Cat` at 12,
`catalog` at 22, `cat.` at 30 -- and against
"cat cat. cat-o cat9 cat_ (cat)":

```
flags 0, 0..-1, "cat"                    -> -1, range -1..-1
0..-1 with neither flag nor order, "one" -> -1
FR_DOWN, 0..-1                           ->  4, range 4..7
FR_DOWN|FR_MATCHCASE, "Cat"              -> 12, range 12..15
FR_DOWN|FR_WHOLEWORD, "cat"              ->  4   (not the one in "catalog")
FR_DOWN, 4..-1                           ->  4   cpMin's own match counts
FR_DOWN, 5..-1                           -> 12
FR_DOWN, 0..6 (the match is 4..7)        -> -1   cpMax bounds the whole match
FR_DOWN, 0..7                            ->  4
34..0, no FR_DOWN                        -> 30
34..0 with FR_DOWN set as well           -> -1
20..0, backwards                         -> 12   the nearest behind, not the first
backwards 20..13                         -> -1   the far end bounds it too
backwards 20..12                         -> 12
backwards 15..0                          -> 12   a match ending where it starts counts
backwards 14..0                          ->  4
forwards 30..-1                          -> 30
forwards 31..-1                          -> -1
"zebra" -> -1        the empty string -> -1        the selection is untouched
whole word from 1, 5, 10, 16, 21         ->  4, 9, 20, 20, 26
```

- **The direction is `FR_DOWN`, not the order of the range.** A forward range
  without the flag finds nothing at all -- not even a word at 0 -- and a
  backward range with it finds nothing either.
- **The whole match has to lie inside the range**, at both ends and in both
  directions, and the end the search starts from counts as part of it.
- **Backwards answers the nearest match behind**, which is what a Find box
  pressing "Find Next" upwards needs.
- **Case is ignored unless `FR_MATCHCASE`.**
- **A word, for `FR_WHOLEWORD`, is letters and digits.** `cat9` is passed by
  and `cat_` is taken, so an underscore is not part of a word and a digit is.
  What a byte above 127 is has not been asked; ween32 takes it as not part of
  one.
- **A find moves nothing.** The selection is where it was afterwards, which
  is why the frame can search and then decide what to select.
- **The storage is what is searched**: `e\rt` is found across the break in
  "one\r\ntwo" and `e\r\nt` is not, since a mark is one CR.

Not asked, and taken the way the forward cases read: a `cpMin` of -1 going
backwards, and a `cpMax` of -1 going backwards.

**`FR_WHOLEWORD`'s boundaries are not the same as a double click's**, and the
two live a few lines apart in `src/richedit.c`, so the difference is worth
stating rather than tidying away. `rich_wholeword_char` counts letters and
digits, which is measured above. `rich_is_word_char` -- what a double click
takes -- counts an underscore in as well, which is **inherited from the
EDIT's `is_word_char` and not measured of riched20**. `ctlprobe.c` now asks
it, on "cat_dog cat9 don't (cat)"; until it answers, nobody should make the
two agree on the grounds that they look alike.

### Where a line breaks, and what a document looks like written down

**Wrapping**, measured in a control two hundred pixels wide:

```
"the quick brown fox jumps over the lazy dog"   2 lines: [0,40] [40,3]
   line 0 = "the quick brown fox jumps over the lazy "
forty-eight a's                                 2 lines: [0,32] [32,16]
a paragraph of ninety-one characters            3 lines: [0,40] [40,42] [82,9]
   after EM_SETTARGETDEVICE(0, 1440)            1 line:  [0,91]
   and after EM_SETTARGETDEVICE(0, 0)           3 lines again
```

A line breaks at the last space that fits and **the space stays on the line
that broke** -- the next line begins on the "d" of "dog". A word too long for
a line of its own breaks at the character that fits. And
`EM_SETTARGETDEVICE` with a width and no device stops the breaking
altogether, whatever the width: 1440 twips is an inch and would have broken
that paragraph five times if it were a width to break to. That is what
WordPad's No Wrap sends, and nought brings the window's own width back.

**The RTF riched20 writes**, which is what ween32's writer is shaped to:

```
{\rtf1\ansi\ansicpg1252\deff0\deflang1033{\fonttbl{\f0\fnil\fcharset0 Tahoma;}{\f1\fnil\fcharset0 Courier New;}}
{\colortbl ;\red255\green0\blue0;}
\viewkind4\uc1\pard\fi360\li360\ri360\qc\tx1440\tx2880\f0\fs17 plain and \cf1\ul\b\i\strike\f1\fs24 formatted\cf0\ulnone\b0\i0\strike0\f0\fs17\par
}
```

Four things to read off it, each of which the writer had to be told:

- A size is in **half-points**: `\fs24` for 240 twips, `\fs17` for the 165 a
  fresh control is lettered in.
- The colour table's **first entry is empty** and is the automatic colour, so
  `\cf0` is "no colour of its own" and `\cf1` is the first real one. Reading
  it as a list of colours puts every index one out.
- `\li` is the paragraph's left indent and `\fi` the **first line's, against
  it**; a PARAFORMAT states the first line's indent and the offset of the
  rest. The pair that came out of `dxStartIndent 720, dxOffset -360` was
  `\li360\fi360`.
- A run states **only what changed**, and the paragraph's end puts everything
  back.

**And what it reads.** Handed a document written by hand -- `\deff0`, a font
table naming Arial, a colour table with one blue in it, `\qc\li720\fs28` and
a `\b` in the middle -- the machine answers with the text "centred bold
blue", Arial at 280 twips throughout, the middle word bold, the last one
blue and not automatic, and a paragraph centred with an indent of 720. The
same document is in `tests/richedit_test.c` with the same expectations.

Two details of the reader that only a round trip finds: **`\deff` names a
face that the font table has not been read yet**, so it is applied when the
table closes; and **the `\par` before the closing brace is a terminator, not
a mark** -- riched20's own documents end with one and the text it reads back
has no empty paragraph after it, so a mark is only written when something
follows it.

### The runs, and what a test can see of them

`EM_SETCHARFORMAT` and `EM_GETCHARFORMAT` are checked in
`tests/richedit_test.c` against the answers riched20 gave the probe, and
three of those checks need something a message cannot show:

- **How many runs the document is in.** Merging a run with an identical
  neighbour has no outward sign -- a document that splits and never merges
  draws exactly the same and grows without bound -- so the library carries
  `ween_rich_run_count`, which nothing but the test calls. Bolding the middle
  of a run gives three; bolding what follows the bold run gives three again
  and not four; taking bold off the middle of that stretch gives five.
- **Where a character landed.** `EM_POSFROMCHAR` is the rich edit's own
  answer to that, and the test uses it to see a bigger run take more room
  along its line and make the line taller. Note the two conventions: an EDIT
  takes the index in wParam and packs the point into what it returns, a rich
  edit fills in a `POINTL` the caller passes and takes the index in lParam.
- **That nothing is said until it is asked for.** `EN_SELCHANGE` arrives as a
  `WM_NOTIFY` carrying a `SELCHANGE`, and only when `ENM_SELCHANGE` is in the
  mask; the test sets the mask without it, moves the selection, and expects
  silence.

`SELCHANGE` is one of the structs the win32 gate earns its keep on: the whole
of `<richedit.h>` is inside `pshpack4.h`, so a `WORD` after a `CHARRANGE`
leaves two bytes of padding and not six, and the struct is thirty-six bytes
rather than forty. Nothing in a compile would have said so.

### What a text control does with the column, when the line below is short

Both controls move the caret up and down by the **pixel it is standing at**,
not by the character it is counted at. The probe asks each of them the same
walk — twelve characters into "long line here", down through "short", down
into "long line again" — and prints `EM_POSFROMCHAR` beside every offset:

```
RichEdit20A  at the start     caret 12  line 0  column 12  x 55
RichEdit20A  after one Down   caret 20  line 1  column 5   x 26
RichEdit20A  after two Downs  caret 33  line 2  column 12  x 55
RichEdit20A  and back up twice caret 12 line 0  column 12  x 55

EDIT         at the start     caret 12  line 0  column 12  x 58
EDIT         after one Down   caret 21  line 1  column 5   x 29
EDIT         after two Downs  caret 29  line 2  column 6   x 29
EDIT         and back up twice caret 6  line 0  column 6   x 29
```

**The rich edit remembers the pixel the walk set out from** — 55 at every
step, and two presses of Up put the caret back on the character it left. **An
EDIT takes the pixel from wherever the caret is now** — 29 after the short
line, and it never comes back. The two really do differ, and ween32 now
differs the same way; anything that moves the caret another way forgets the
remembered pixel, or the next Down would set out from a place the caret had
already left.

Counting columns instead — which is what this library did until the probe
asked — gets the first step right by luck and the second wrong: it puts the
EDIT's caret on 28 where the machine puts it on 29, and never moves a caret
to the character *nearest* a pixel in a proportional face at all.

### The bar a rich edit puts up, and when

A rich edit shows its vertical bar **only when there is something to
scroll**, where an EDIT with WS_VSCROLL always has one and greys it. The
machine's WordPad has no bar at all on an empty document: columns 749..761
of `wordpad/reference/shots/win.png` are white where ween32 had a dithered
track, which was about 830 pixels of that band. `ES_DISABLENOSCROLL` is what
asks for one that is always there.

It costs two passes over the lines, because the bar and the wrapping each
depend on the other: measure without a bar, and again with one if the text
turned out not to fit. Adding the bar can only take width away, which can
only add lines, so the second answer stands.

### The Font box, whose rectangles are the machine's and whose lists are ours

`wordpad/reference/probe/font.txt` is the probe's walk of a running Font
dialog, so every control in ween32's is at the unit the machine has it at:
the client is 431x319, which is **287 x 196** dialog units, and the three
combos, the Effects group, the Sample and the four buttons follow from it.
The ids are win32's — 1136 the face, 1137 the style, 1138 the size, 1139 the
colour, 1040 and 1041 the effects — because a program that hooks the box
addresses them by number.

**The lists are the other half, and they are ours.** This library has no
rasteriser: a face carries a handful of bitmap strikes and a request lands on
the nearest of them. So the box offers the two faces it can actually draw and
the sizes those strikes hold — six to twelve points for Tahoma, and MS Sans
Serif's own six — which is what the machine's box does for a bitmap face
anyway. Offering a face it would then draw in another one would be worse than
none, and enumerating the host's fonts would make a render depend on what is
installed, which is the one thing every capture here has been protected from.

There is no capture of the box yet, so what is checked is behaviour rather
than pixels: `tests/comdlg_test.c` drives it through the **hook** a program
would install, which is the only way into a modal dialog and is itself worth
checking. It asserts that the face, size, style and effects the program hands
in are the ones selected, that OK writes them back with `iPointSize` in
tenths of a point, that Cancel writes nothing, and that choosing the other
face refills the size list with that face's own sizes.

### The two text controls, held against each other

`tests/richedit_test.c` asks the rich edit the questions `tests/edit_test.c`
asks the EDIT, in the same words. That is not duplication for its own sake:
the two controls keep their text in different places on purpose — an EDIT in
the window's own text, a rich edit in a document of its own that will grow
runs and paragraphs — and everything above the text is meant to be
identical. A backspace over a line break taking both characters, Home being
the line's start and Control-Home the document's, Down stopping at the end
of a shorter line, the anchor that Shift extends from and a plain arrow
drops: each is written twice because only a test can keep two
implementations agreeing.

Three things it checks that the EDIT's cannot:

- **The line table against the shared line functions.** The rich edit draws
  from a table of its own — every line's start, length, top and height, built
  in one pass — because a line's height stops being one number the moment a
  run carries a size. `ween_text_line_*` in controls.c is what answers the
  messages. On a text with both kinds of break, an empty line in the middle
  and one at the end, walking down it with the arrow has to land on the same
  line starts the functions give.
- **The event mask.** A rich edit tells its parent nothing until
  `EM_SETEVENTMASK`, where an EDIT sends EN_CHANGE whether or not anybody
  asked. Both halves are checked: silence before, EN_UPDATE and EN_CHANGE
  after.
- **Where the two disagree and nobody has measured which is right.** When
  the line below is too short for the caret's column, Windows is said to
  remember the column and come back to it; neither control here does. The
  test asserts that the *two agree with each other* rather than what they
  agree on, so the day it is measured on the machine — a long line, a short
  one, a long one, two presses of Down in Notepad — both change together or
  the test fails. It is on the ROADMAP as its own item.

### The option button's column, settled by asking Windows

**An option button's circle is drawn one column inside its control; a tick
box's box is drawn on the control's own corner.** It cost a wrong answer and
a held branch to get here, and the thing that settled it was not a better
argument but a different instrument.

The arguments were these, and they contradicted each other. Notepad's Find
box has two circles forty pixels apart, and `MulDiv(u, 6, 4)` skips every
third pixel — 0, 2, 3, 5, 6, 8, 9, 11, 12 and never a pixel ≡ 1 (mod 3) —
so if a circle began at its control's edge no pair of whole units could put
those two where they are. But moving every circle one column in cost Folder
Options General **7,577 pixels** against its own capture, and no whole unit
could hand that page back the pixel it lost either.

**`tools/vm/probe.c` ends it in one run.** Built for the guest and run
inside it, it asks Windows for `GetWindowRect` of every control:

```
Folder Options   Button  245,268  278x13  "&Use Windows classic desktop"
                 circle's leftmost pixel: 246
Find             Button  307,333   38x20  "&Up"      circle: 308
                 Button  347,333   53x20  "&Down"    circle: 348
                 Button  146,339   96x20  "Match &case"   box: 146
```

The circle is one in and the tick box's is not, in both dialogs, with no
inference between the measurement and the answer. And the two Find buttons
are at dialog units **111 and 138** — the pair the arithmetic said had to
exist.

**Why Folder Options seemed to say otherwise**: `examples/explorer` places
that page's controls from a table of pixels rather than from its template,
and the table had been fitted while the library drew the circle at the
control's edge — its comment said so in as many words. The fitted rectangles
were a pixel right and a pixel up of Windows' own, and sixteen tall where
Windows has thirteen. With the library drawing the circle where the machine
draws it, the table is now the probe's own rectangles (moved by the three
across and one down below), and the page counts **1069** where it counted
1224 before any of this.

**One difference of ours the probe found on the way**, unfixed and written
down here rather than left in the pixels: the machine's property sheet puts
a page at **(13, 51)** from the sheet's window origin and makes it 360x374;
ween32's is at (10, 50) and 365x377. Both sheets are 386x468 with a 380x443
client, and the tab control is at (9, 29) on the machine and (8, 29) here.
Every page in `examples/explorer` carries that difference in its table of
pixels, which is why the pages match while the structure does not. Worth
fixing once, in `TCM_ADJUSTRECT` and `PS_TAB_X`, with all four tables moved
back the same day.

**What the machine's own templates turned out to be**, since the probe gives
the rectangles and the mapping is known: Find's are in the table above and
every one of them is what ween32 now carries. Two the probe corrected:
`Fi&nd what:` is 42 units wide rather than 44 and `Match &case` 64 rather
than 60 — neither moves a pixel, both are the real numbers. It also shows
what Notepad hides: comdlg32's template carries a `Match &whole word only`
tick box at unit 4,26 and a `&Help` button at 182,45, both created without
WS_VISIBLE.

**What it costs against wine**: the sampler goes from 15649 to **15749**,
all hundred of them the option buttons in rows 70..136, because wine draws
the circle at the control's edge. The machine is the reference that matters.

**Rendering ours to compare.** The box draws on its own window surface:

```c
ween_kbd_used = ween_menu_cues = ween_ui_focus_cues = 1;  /* opened by Ctrl+F */
FINDREPLACEA fr = { .lStructSize = sizeof fr, .hwndOwner = host,
                    .lpstrFindWhat = buf, .wFindWhatLen = sizeof buf,
                    .Flags = FR_DOWN };
HWND dlg = FindTextA(&fr);
InvalidateRect(dlg, NULL, TRUE); ween_flush_paint();
ween_surface_write_bmp(&((struct ween_wnd *)dlg)->surface, "/tmp/find-ours.bmp");
```

`tests/comdlg_test.c` keeps two of the capture's pixels as assertions — the
circle's column and the field's first ink — so that neither can drift back
without a test saying so.

### The caption's ramp, measured to the column

**Solved.** The machine's ramp, at 400, 500 and 654 pixels wide, is:

- the start colour held across **the icon's room and the two columns past
  it** — eighteen at 96 dpi, which is why a dialog, with no system-menu icon
  at all, starts its ramp at the caption's own left edge;
- then one division per pixel to **fifty-five before the client's right
  edge**, which is the room the three caption buttons take;
- and each channel, d columns along a span, is

```
start + (d * (end - start) - 1) / span        in whole numbers
```

That is **every one of 1,371 columns across the three widths and all three
channels**. The subtracted one is the whole finding: it changes nothing
except where the division comes out exactly whole, and there the machine is a
shade below. At 500 wide that is every 35th column, since 156 and 420 share
twelve. The plain floor of the same division misses exactly those; a step
worked out once in 16.16 and added up — which is what this library did —
misses them too and then drifts, agreeing at 400 and 500 and losing a column
by 654. The error grows with the span, which is why the sampler, whose
caption is wider than any of the three, gained **1,571 pixels** from the
change: 15,749 against its wine render before it and 14,178 after.

`tests/caption_test.c` holds the ramp against all three captures, column for
column, out of `tests/caption_rows.h` — which
`tools/refcapture/caption_rows.py` writes from the PNGs, since the suite has
no decoder and is not going to grow one for this.

**What moved and what did not.** Every comparison against the machine was
re-measured: Properties **652**, Folder Options General **656**, View 1403,
File Types 9711, Offline Files 1037, Column Settings **1106**, Find **83**,
Replace **57** — all unchanged, because those windows' ramps were already on
the machine's pixels at their widths. The two that moved are the wine
renders, and toward the machine rather than away: the sampler 15749 →
**14178**, the menu unchanged at 4397.

**A fourth width confirms it.** `wordpad/reference/shots/win.png` is the
machine's own WordPad at 768, and the same rule with the same hold of
eighteen is **every one of its 760 columns**. All four widths are in
`tests/caption_rows.h` now, 2,078 columns in all.

What that capture also settles is what looked at first like a disagreement:
against it, ween32's WordPad frame holds sixteen where the machine holds
eighteen. That is not the ramp — it is the *window*. The machine's WordPad
has an icon in its caption and ours has not been given one yet, and the two
columns past the icon's room are what an icon earns.

**The fifth width settles the window that has no icon of its own.**
`caption-460-noicon-machine.png` is `tools/vm/ctlprobe.c`'s own window on the
machine — `WS_OVERLAPPEDWINDOW`, 460 wide, and a class whose `hIcon` is NULL,
since the whole `WNDCLASS` is memset and nothing puts one there. Its ramp is
the same rule with the same hold of **eighteen**, every one of its 452
columns. So the hold does not depend on the window having an icon, and
`icon_w` no longer asks: a caption that keeps the icon's room keeps the two
columns past it as well.

**Why it holds eighteen is worth knowing, because it is a job not done.** The
room is not empty on the machine. Subtract the ramp from that capture and 99
pixels are left in it — a small dithered thing with the four colours of the
Windows flag in its right half — and the title starts at the caption's left
plus twenty, not hard against the frame. That is Windows 2000 drawing *its*
default icon for a window whose class has none. This library draws nothing
there and puts the title hard left, so a system-menu window without an icon
comes out with a gap where the machine has a picture. The machine's pixels
for it are kept as `tools/refcapture/caption-default-icon-machine.png` — 16x16,
transparent where the ramp showed through, lifted off a single capture. They
are evidence, not a resource: what to do with them is draw the icon and move
`title_x` with it, and then every capture of a window in that state moves.

**What the hold cost against wine, deliberately.** Wine holds sixteen for
such a window, so the controls sampler goes 14,178 → **14,877**, all 699 of
them in rows 4..21 and nothing else in the window touched; the menu sampler
goes the other way, 4,397 → **4,162**, which says wine's two samplers do not
agree with each other about this. Every comparison against the machine is
unchanged. When wine and five machine captures disagree, this repository
follows the machine.


### The toolbar, asked rather than looked at

`tools/vm/ctlprobe.c` creates controls in its own process, so the messages
that take a pointer are legal and a toolbar can be asked what it did instead
of being photographed doing it. Build line in its header; run it as
`Z:\ctlprobe.exe Z:\ctl.txt` on a machine of your own. What it has settled so
far about a toolbar:

| question | win32's answer |
| --- | --- |
| `TB_SETBUTTONSIZE cx` | the button's **whole** rectangle. Told 23, buttons are 23x22 at a pitch of 23 |
| a separator's width | `iBitmap` at add time, `TBIF_SIZE`'s `cx` afterwards, and **8** for saying nothing |
| ...and what it reports | `TB_GETBUTTON` gives back **8**, not the 0 it was added with: the default is stored, not applied |
| ...and the two fields | separate. `cx` set afterwards wins the layout and leaves `iBitmap` alone |
| the button's top in the bar | **flat 0, classic 2** — at every bar height, divider or not. Not a centring |
| `TB_SETHOTITEM` on a classic bar | refused; `TB_GETHOTITEM` goes on saying -1 |

The last two are worth a warning each.

**A classic bar has no hot item**, and that is not an omission: every button
on one already wears its raised edge, so there is nowhere for hot to show.

**ween32 centres its buttons, and win32 does not.** `tb_button_y` returns
`(h - btn_h) / 2`, which agrees with the fixed inset only where the bar is
exactly a button tall — and every toolbar in this tree was, so the wrong rule
and the right one gave the same pixel everywhere anybody had looked. Do not
just change it: putting the measured rule in moves **402 pixels in explorer's
menu band**, and that band is checked against the machine at 170 differing
pixels, all of them the shell's animation. Asked for a menu band's own
configuration — flat, `TBSTYLE_LIST`, `TB_SETPADDING(16,0)`,
`TB_SETBUTTONSIZE(0,19)`, bar 22 tall — real comctl32 gives its button
**y=0 h=16** where ween32 gives **y=1 h=19**. Two disagreements that come out
right together, and until somebody knows which of the two numbers is the
wrong one, changing either alone makes the picture worse.

`tools/refcapture/tbstates-machine.png` is the four states on both bars,
comctl32's own standard art, 96x67 taken at the probe's client origin:

    crop y  2   the flat bar     ordinary, hot, checked, disabled at x 0,23,46,69
    crop y 39   the classic bar  the same four, its buttons two down in their bar

A checked button is a sunken edge over a 50% dither of face and white; a
disabled one keeps whatever edge its style gives it and draws the image
embossed. Nothing in ween32 draws either yet.

### The explorer's commands

The menus and the toolbar do what they say, against the file system the
example is browsing — so trying them means giving it somewhere safe to work:

```sh
mkdir -p /tmp/scratch/sub && printf x > /tmp/scratch/alpha.txt
./examples/explorer /tmp/scratch
```

Headless, the same through a script. New Folder, a name typed over it, and the
folder that results:

```sh
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/f%d.bmp \
  WEEN32_SCRIPT="w:200 k:18 w:150 t:f w:200 t:w w:200 t:f w:400 t:Photos w:200 k:13 w:400" \
  ./examples/explorer /tmp/scratch && ls /tmp/scratch
```

Delete puts what it takes into a folder beside the system's temporary
directory — `ween32-recycled` — which is what makes Edit > Undo Delete able to
put it back. Nothing is removed outright.

### The same source, built as win32

`examples/explorer.c` compiles against the real win32 headers, and it is worth
*running* what that produces: everywhere ween32 is more permissive than
comctl32, the application leans on ween32 without knowing it and the win32
build comes out wrong. Every one of these was found that way — bands that had
never asked to break, toolbars that had never said where they go, a heading
that lost its name when the sort arrow was set, status-bar text put in parts
that did not exist yet, a menu title that was not a whole-button drop-down.

```sh
Xvfb :99 -screen 0 1024x768x24 &          # no window manager needed
export DISPLAY=:99 WINEPREFIX=${XDG_CACHE_HOME:-$HOME/.cache}/ween32-refcapture
zig cc -target x86_64-windows-gnu -std=c99 -Iinclude examples/explorer.c \
    -luser32 -lgdi32 -lcomctl32 -lshell32 -o examples/ween-explorer.exe
WEEN32_EXPLORER_FIXTURE=1 wine explorer /desktop=ween32test,760x600 \
    'Z:\path\to\ween32\examples\ween-explorer.exe' &
xwininfo -root -tree | grep "Local Disk"   # the window id, to grab or click
```

The fixture works on both sides, so the two windows hold the same content and
can be put side by side. Everything that is *placed* now lands in the same
pixel on both: the column dividers at 328, 424, 544 and 652, the pane's edge
at 202, the status bar's top at 516, every band and every button. Getting
there meant saying outright what a library would otherwise work out for
itself — each toolbar button's width and each menu title's, since comctl32's
padding is not the machine's.

A screenshot is the wrong instrument for the last of it. Wine measures text one
way and draws it another: `GetTextExtentPoint32`, `GetCharWidth32` and
`GetCharABCWidths` all say "Documents and Settings" is 128 pixels — the outline
scaled — while what it puts on the screen is the 11-pixel embedded strike, the
same 116 pixels ween32 draws. Every layout decision made from a measurement
inherits the 10%: the application's own button widths, the list view's
truncation, the row height comctl32 derives.

Real Windows does not do that, and the machine says so. With Alt down, its menu
band's six titles have their text starting at 10, 42, 76, 114, 175 and 216
pixels. ween32 puts them at 10, 42, 76, 114, 175 and 216. Wine puts them at 10,
45, 81, 122, 185 and 228. The same source laid out on the machine would land
where ween32 lands; it is wine in between that cannot agree with itself.

So the two builds will not come out pixel for pixel under wine, and chasing
that is chasing wine — unless wine is given a Tahoma whose advances are what
its own scaling arrives at, which is what `tools/winecmp/` is for and the one
way to get the two pictures comparable.
Ask for the geometry instead: paste a block into `layout()` that dumps every
control's rectangle — `GetWindowRect` relative to the client origin, plus
`TB_GETITEMRECT` for each button and `LVM_GETCOLUMNWIDTH` for each column —
compile that one file both ways, and diff the two dumps. Every line that
differs is a question with an answer, and most of them turned out to be
something the application had left to the library rather than saying outright.
What still differs there is four numbers: the widths that follow from the font,
the gripper a band leaves before its child (nine against ten), the rule a rebar
puts under its last band, and the height a ComboBoxEx takes. ween32 has the
machine's number for three of those.

Three things are left over in the picture, and all three are the environment
rather than the code. The font and what is measured from it. Tahoma carries embedded
bitmap strikes — 8, 9, 10, 11, 12, 13, 15 and 16 pixels — and at eight points
GDI draws the 11-pixel strike, which is what ween32 rasterises and why the
machine matches to the pixel. Wine scales the outline instead: "Documents and
Settings" comes to 128 pixels there against 116 here, near what the outline
gives with each advance rounded up (130) and nowhere near the strike. So every
run of text is wider, and comctl32 sizes a list's rows from it — 16 pixels
against the 17 the machine has, which walks the rows apart down the list.
Font smoothing on or off makes no difference; it is not the rasterising, it is
which glyphs are being measured. The caption: with no window manager nothing takes the X
focus, and wine paints an inactive caption over a window that reports itself
active — `WM_ACTIVATE` arrives with `WA_ACTIVE` and `GetForegroundWindow`
agrees, and a forced repaint keeps the grey. And the underlines under the menu
titles: whether they show before Alt is a system setting, hidden on the machine
and shown by wine's default, so the win32 build wears them from the start.

None of it is ween32's to fix, and the machine, not wine, is the yardstick for
how any of it should look.

**Wine is not the reference for drop-downs.** It renders a menu's border as a
flat grey line and a separator as a single line; Windows draws a raised edge
and an etched pair. Everywhere else in ween32 wine agrees with Windows, and is
the reference because it can be re-rendered on demand. For menus, measure
against a screenshot of Windows itself — see [Reference
captures](../ROADMAP.md#reference-captures).

A wine drop-down can still be captured, if only to see the difference:

```sh
CLICK_AT=55,80 tools/refcapture/capture.sh menu.c /dev/null
# then crop the drop-down out of tools/refcapture/desktop.png
```

`CLICK_AT` is in the coordinates of wine's desktop window, so it depends on
where the window manager put things — find the window's corner in the shot
first rather than trusting a remembered number.

Those numbers are the ones to watch. **Going up is a regression** even if
every test still passes — the suite asserts behaviour, the diff asserts
appearance. When it moves, find out where before doing anything else:

```sh
tools/refcapture/pxdiff.py 15 34 75 23      # x y w h: one control, pixel by pixel
```

The ASCII map prints the reference beside ours with one character per colour,
so a one-pixel shift is visible. What the standing difference consists of is
tabulated in ROADMAP.md — if your total matches those totals, nothing has
moved.

#### A band's chevron

A band too narrow for what is in it wears a `»` at its right edge. Measured on
My Computer's rebar, with the window dragged narrow enough that both the menu
band and the toolbar band wore one:

```sh
JSLINUX_SOCK=/tmp/jslinux-mcp.sock JSLINUX_SHM=/dev/shm/jslinux-mcp.fb \
  tools/vm/drive.py press 782,400 holdmove 430,400 release \
  sleep 600 park shot /tmp/narrow.png
```

**The glyph** is two arrowheads, each two pixels thick, stepping out for three
columns and back — **eight wide by five tall**, counted three times on three
different bands:

```
##..##..
.##..##.
..##..##
.##..##.
##..##..
```

**Where it sits.** Three pixels in from the band's right edge, and its top
**four pixels below the band's top** — *not* centred in the band, which is
what it looks like and what ween32 did until this was measured. Two band
positions agree: a band starting at y 158 has its chevron top at 162, one
starting at 182 has it at 186.

**What comes out of it is a popup menu**, not a floating strip of buttons: the
hidden buttons with their icons at the left and their labels as text, disabled
ones still greyed, a drop-down button keeping its submenu arrow, then a
separator and **Customize…** — which is the toolbar's own item rather than a
button that did not fit. It hangs directly off the band's bottom edge with its
left two pixels left of the chevron's, which is what an application gets by
taking the rectangle out of `NMREBARCHEVRON` and calling `TrackPopupMenu` at
its bottom-left. ween32 sends that notification and draws that chevron; what
goes in the menu is the application's, because only the application knows what
is in the band.

#### How far a track click pages

Clicking the scroll bar's track below the thumb, in the explorer's list:

| list client height | rows that fit whole | rows the click moved |
| --- | --- | --- |
| 383 px | 22 | 22 — the top went `addins` → `system32` |
| 270 px | 15 | 15 — the top went `addins` → `mww32` |

So a list view pages by **exactly the number of fully visible rows**, and the
row that was partly visible at the bottom becomes the new top. There is no
line of overlap — a page is not a screenful less one here.

Two heights, because one number is not a rule. And a warning for whoever
measures the tree: it has to have **more than twice as many rows as fit**, or
the page clamps at the bottom of its range and what you measure is the clamp.
A first attempt read 7 rows moved with 25 visible, which is the tree running
out of scroll and not the tree's page size.

### What the gates cannot see

The constants gate compares every `#define` against the real headers and every
alias against what it stands for, and a name win32 has not got fails it. That
is a strong instrument and it has a hole worth naming, because naming it is
what makes the next person think to look:

**A struct with the right name and half the fields passes both gates.**
`REBARBANDINFOA` was declared here down to `cx` and stopped, eight fields short
of win32's — no `cxIdeal`, no `wID`, no `lParam`, no `cxHeader`. Every constant
around it agreed; every alias resolved; the examples compiled against real
win32 because no example used those fields. A program that sets `cxIdeal`
compiles on Windows and not here, which is the one thing this library promises
it will not do, and nothing in the suite or either gate said a word.

**That hole is closed.** I argued a struct-shape checker was impossible, and
that argument was about the wrong set: a checker that asserts something about
*every* type ween32 declares would drown in exceptions, but the set worth
checking is the types an application fills in and hands over, and that is a
list. `tools/win32check/genstructs.py` is that gate. Nothing in it is written
by hand, because a hand-written offset drifts from the header it describes:
it reads ween32.h for the structs and their fields in declaration order, emits
a C program that includes **ween32.h** so the host compiler supplies our real
offsets, and that program prints `_Static_assert`s which are compiled against
the **real** windows.h. Three faults fail it, all three tried on purpose:

| what was done to LVITEMA | what the gate said |
| --- | --- |
| a field removed | `offsetof(LVITEMA, iGroupId) == 48` |
| `LPARAM lParam` made `int` | `offsetof(LVITEMA, iIndent) == 44` |
| two fields swapped | `offsetof(LVITEMA, iImage) == 32` |

The middle one is the reason the gate exists: a field of the wrong width reads
perfectly, compiles on both sides, and moves every field after it.

Offsets are asserted always; **sizes are asserted only where win32's own
definition does not grow a tail behind a version guard**. Eight structs stop
before such a tail deliberately — REBARBANDINFOA before the Vista chevron
pair, LVITEMA before `piColFmt`, and so on — and each is named in the
generator with the reason. That excuses the size and nothing else: a field in
the wrong place still fails.

It is the same shape as three other things that have caught us, all of them
the instrument rather than the code:

- **a test binary that was not rebuilt** — `make` builds the library and the
  examples, and *not* the tests, so `make && ./tests/foo` can run yesterday's
  binary against today's library and pass;
- **a number read out of a worktree somebody was still typing in** — the gate
  reported 951 constants for a commit that had 932, because it was run in a
  tree with uncommitted work in it. Measure at the sha, in a worktree of its
  own: `git worktree add --detach /tmp/review-<sha> <sha>`;
- **a grep that matched a test's name** — `grep -c ERROR` over a sanitizer log
  finds `ok   a buffer too small is ERROR_MORE_DATA`. Grep for the
  sanitizer's own words, `AddressSanitizer|LeakSanitizer|runtime error`.

**The two struct gates read the same header with the same regex, and one of
them used to stop at a word the other allowed.** The C header writes a hook as
`INT_PTR(CALLBACK *lpfnHook)(HWND, ...)`:

```
win32check looked for   \(\s*\*\s*(\w+)\s*\)         -- (*name) only
zigbind looks for       \(\s*\w*\s*\*\s*(\w+)\s*\)  -- (CALLBACK *name) too
```

A member it could not read made the **whole struct** unreadable, and it said
so honestly — `5 not read by the generator, so not compared` — which is easy
to read past when it sits under a line saying sixty-two were. The five were
CHOOSECOLORA, CHOOSEFONTA, FINDREPLACEA, PAGESETUPDLGA and PRINTDLGA:
**exactly the five comdlg32 structs that carry a hook, every one of them for
that reason and none for a reason of its own.** Three were covered by the Zig
gate, which had always allowed the calling convention; **PAGESETUPDLGA and
PRINTDLGA are declared in no Zig, so nothing checked them from either
direction.**

`win32check`'s regex now allows it too. **62 structs compared became 67, and
all five agree with real `windows.h`** — 69 field assertions and five sizes,
including all seven hook members. Nothing was wrong; the point is that nobody
could have known, and two of the five had been checked by nothing at all for
as long as they had existed.

Both of the previously-unchecked two were broken on purpose to prove the new
assertions are not vacuous: `PRINTDLGA.nCopies` widened from WORD, and
`PAGESETUPDLGA.Flags` narrowed from DWORD.

```
_Static_assert(sizeof(((PRINTDLGA *)0)->nCopies) == 4, "PRINTDLGA.nCopies width");
_Static_assert(sizeof(((PAGESETUPDLGA *)0)->Flags) == 2, "PAGESETUPDLGA.Flags width");
```

**The lesson is not the regex.** It is that a gate which reports what it could
not do is only as good as somebody reading that line — and a count of what
*was* checked, printed beside it, is exactly the thing that stops anybody
reading it. If a gate can name what it skipped, it can be made to fail on
what it skipped instead.

Two more, both about what a gate is *shaped* to notice:

- **the binding gate is a spell-checker, not a dictionary.** It checks that
  what `zig/ween32.zig` declares agrees with the header, which is one
  direction. A name the module never mentions is a name it never disagrees
  with, and absences are reported only as a **total in the hundreds** --
  the gate's own last line. (It said 361 when this was written and says fewer
  now; the figure is deliberately not repeated here, because a number in a
  document beside a tool that prints one is a number with nothing checking
  it.) `STATUSCLASSNAMEA` sat in that total, so a Zig program
  could create every common control except a status bar and every gate stayed
  green. One missing entry inside a count of hundreds is not a signal anybody
  can act on;
- **a declaration nobody calls is a declaration nobody has checked.**
  `LoadMenuA` went in with two optionals on types that were already optional,
  which is a compile error the moment anything calls it -- and nothing did, so
  it was merged. The fix existed on disk and was never committed. `git status`
  before saying a branch is ready, and write the caller in the same commit as
  the declaration.

And the rule under all six: **a test that passes is worth nothing until it has
been made to fail.** Two tests written the same day passed with the fix and
without it — one scanned for a gripper starting on the control's own white edge
and so compared two buttons rather than two grippers; the other read freed
memory that happened to survive, and only the sanitizer build could tell.
Break the library on purpose, watch the assertion go red, put it back.

### Reproduce the state the capture was taken in

§8.6's Word page counted **104** differing pixels the first time it could be
counted at all, and **77 of them were a focus rectangle** — the machine's
dotted ring around `Wrap to r&uler`, which its capture has because somebody
took it by *clicking* the tab, and which our render did not have because the
script arrived by arrow. A click leaves the keyboard in the page; an arrow
leaves it on the tabs. One Tab at the end of the script puts it where the
click would have left it and the page reads **27**.

Every one of those 77 pixels was real, present in the reference, and absent
from ours. **The count was right and the comparison was not**, because the two
pictures were of the same controls in two different states.

This is the caret and the mouse pointer from the other end. There the rule is
to keep the session *out* of a reference — a blinking caret, a hot control, a
tip that had time to come up. Here it is to put the session *back into* the
render: **a capture is of a moment, and the moment includes where the keyboard
was.** Before counting anything against a reference, ask how the reference was
made and whether the script arrives the same way — and if the reference does
not say, that is the thing to write down about it.

### A capture made by clicking has the pointer in it

The first machine capture of the rename box had its top border broken by seven
pixels — `black black black white white white black white white white black`,
which reads exactly like a gap in the art and was one sentence away from being
written down as one.

**It was the mouse.** The row had been picked by clicking it, the I-beam's top
serif sits eight rows above the hotspot, and eight rows above the click is the
row the box's border is drawn on. Moved to a corner and retaken, the border is
one unbroken run from 15 to 95.

This is the section above from the other end, one input device over. There the
reference had a focus rectangle in it *because* it had been made by clicking;
here it had a pointer in it for the same reason. **A pointer is not part of
the program and it is not part of the render either**, so a capture with one
in it cannot be compared with anything — and unlike a focus ring, which at
least belongs to a control, this one lands wherever the last click was, which
is to say wherever the interesting thing is.

**It has now done this three times in one day, and each time it looked like
something else.** That is the part worth carrying, because the first version of
this entry described only the middle row and it did not fire on the third:

```
a focus ring, 77 pixels          read as    our render missing a rectangle
an I-beam's serif, 7 pixels      read as    a gap in the box's top border
an arrow's drop shadow, 178 px   read as    the capture had been resampled
```

**The third was found by somebody else**, on a capture taken by the person who
wrote this entry, four hours after writing it. The shadow is alpha-blended, so
it does not add a pointer-shaped hole — it turns a five-colour ruler band into
a **seventy-nine-colour** one, 0.73% of the pixels, in a patch sixteen wide.
Nothing about that resembles "the pointer is in it", and the count was
diagnosed as a lossy step in the capture path. It was the mouse, parked where
the drag had released it.

So the rule is not *look out for a broken border*. **A pointer in a capture
is not one symptom, and filing it under the symptom you first met is what
makes it invisible the second time.** What the three have in common is only
the cause, so that is what to check: **before believing any anomaly in a
capture, ask where the pointer was.**

**And it should not be a rule at all, because the tool can do it.**
`tools/vm/drive.py` has a `park` verb, so a capture routed through it moves the
pointer out of the way by construction:

```sh
tools/vm/drive.py drag 174,262 460,262 wait 400 park wait 300 shot /tmp/x.png
```

The same run that produced the seventy-nine colours produced **five** with
`park` in it. A rule that everybody must remember before every capture is a
rule that will be forgotten; a verb in the command cannot be.

### Counting a picture with a script is still counting the picture

§8.6 says the five format pages of WordPad's Options sheet are one template.
A count of the checkboxes on each said **four, four, three** — the Embedded
page appearing to lack `Status bar` — and it was reported as §8.6 being wrong,
with the words *"counted programmatically rather than by eye"* attached to it
as though that settled what kind of claim it was.

**It did not, because what the script counted was the capture.** Probing the
live sheet found the fourth checkbox on the Embedded page at the same id, the
same rect and the same dialog units as on every other page, differing by one
bit:

```
"&Status bar"  1031  230,102  102x16  style 50010003   the other four pages
"&Status bar"  1031  230,102  102x16  style 40010003   Embedded: no WS_VISIBLE
```

§8.6 was right. **A control that is not drawn still exists**, which is the
same sentence as `PSH_NOAPPLYNOW`'s two buttons twenty lines up in
`propsheet_test.c` — *the picture loses a button and the program does not* —
and it was missed by somebody who had written that comment the same day.

So: **"counted programmatically" says how carefully you looked, not what you
looked at.** It is worth *less* than counting by eye when the two count the
same thing, because it carries an authority it has not earned. The question
that separates them is not *did a script do it* but **could this answer have
come out differently if the thing were invisible rather than absent** — and
if the answer is no, the instrument is the picture wearing a script's clothes.
Ask the program: `GetDlgItem`, a probe, a window walk. Knowing the rule is not
protection; reaching for the other instrument is.

### A build that did not happen looks exactly like a measurement that did

**Three times in one evening a build failed and the harness that ran it
reported a result anyway.** Each time the number came from the binary that was
already there.

```
two sabotages did not compile under -Werror and printed nothing
gb_paint failed on -Werror unused parameter 'ps'; the render came from the stale binary
```

The mechanism is the same each time and it is not subtle once you have seen
it: a script builds, does not look at the exit status, and then measures
whatever is on disk. **There is always something on disk.** The measurement
succeeds, prints a plausible figure, and the figure is about a version of the
program that no longer exists in the source.

**No instrument here can tell that number from a real one**, and that is the
whole reason this is dangerous rather than annoying. A wrong pixel count looks
wrong. A count of the *previous* build looks exactly like a count — right
shape, right magnitude, often right value, because most of the time the change
you are making does not move it much.

**So: a script that builds and then measures must check the build's status and
refuse to measure.** Not warn — *refuse*, and print what the compiler said.
Warning is not enough because a warning scrolls past above a number, and the
number is what gets quoted into a report.

The way it was caught in the end is worth copying: **print the compiler's
output rather than counting its lines.** A build step that captures output and
prints nothing on success prints nothing on failure either, unless somebody
wrote the branch that says so.

**And a fourth, found in this file's own `verify.sh` while writing this entry**
— which is the most useful kind of instance, because nobody was hunting it.
The suite's line was

```sh
ok=$(make test 2>&1 | grep -cE "^ok")
```

counting `ok` lines and never asking whether `make test` ran. **The tests are a
second compile**, so a test file that stops compiling ends the run before any
binary executes. Sabotaged by appending `this is not c;` to
`tests/propsheet_test.c`:

```
as it was    assertions 0, and the run carried on
as it is     FAILED to build or run the tests, gcc's errors, exit 1
```

**Zero is not silence**, and somebody would very likely notice a suite that had
gone from 1123 to 0. What it is not is *the reason*: `assertions 0` reads as a
suite that ran and passed nothing, which is a different and far more alarming
thing than a file that would not compile. Meanwhile every capture below it is
measured perfectly well, because the *library* built — so the report is nine
right numbers and one wrong one, which is the hardest shape there is to read.

**And a fifth of a different kind, which belongs beside these and is not one of
them.** `wordpad`'s `verify.sh` reported `0 warnings` where a cold build
reports 2, because zig prints a warning when it *compiles* the thing that has
it and says nothing on a warm cache. Nothing failed and nothing was stale: the
build was real and the count was of what was rebuilt, which was nothing. That
is not a stale binary, it is **a number whose meaning depends on the cache**,
and the fix is a different one — `rm -rf` the build cache before counting, and
spend the three seconds.

**Not a fourth instance**, though it was nearly written down as one: two
templates that measured identically turned out to be a file nothing read —
`fo_layout` overwrites every control from a table of pixels at `WM_INITDIALOG`,
so the dialog units being edited were dead. It *presented* the same way, two
different inputs and one output, and "the build did not happen" was the cause
already to hand. **A wrong cause with a confident sentence on it is the thing
this section exists to stop**, and it does not stop being that when the
sentence is one of this section's own.

### An instrument that depends on a flag depends on a version

**A check passed on one machine and failed on another, and the difference was
neither the code, the tree, nor the cache.** It was which `zig` was first on
somebody's PATH.

```
== verify ==
  FAILED  a consumer fetches the package and builds against the host
  error: unrecognized argument: --global-cache-dir
```

The run that failed was jd's, on a release; the run that passed was of the
same commit, minutes earlier. **And it is not even consistent within one
binary**: his `zig` accepted `--global-cache-dir` on `zig fetch` and rejected
it on `zig build`, and the Makefile's `--cache-dir` worked throughout — so a
fix narrowed to the line that failed would have left two more waiting.

`ZIG_LOCAL_CACHE_DIR` and `ZIG_GLOBAL_CACHE_DIR` say the same thing and **have
no argument list to be rejected from.** Both `tools/package.sh` here and
`wordpad`'s `verify.sh` were changed to them; the second had not fired yet and
would have, on whoever ran it next.

**The general form:** an instrument that depends on a command-line flag
depends on a version, and a version is a property of the person running it
rather than of the thing being measured. Where an environment variable will do
the same job it is the more portable statement.

**And this is the one failure in this section that no amount of care here can
catch**, which is why it is written down rather than guarded. Every other entry
is about a number that was wrong for a reason present in this repository; this
one is about a difference between two shells, and no script can police what is
on somebody else's PATH. What it *can* do is not depend on it.

**When a variable replaces a flag, check that the variable is honoured.** A
flag that is rejected says so loudly; a variable that is ignored is silent and
falls back to the default — which for a cache directory means it lands in
`.zig-cache` on the checkout, the exact thing the flag existed to prevent. The
check is to remove both directories, run, and look at where they come back:

```
both cache directories removed, then a full run
  /tmp/wordpad-zig-cache and /tmp/wordpad-zig-gcache created
  no .zig-cache on the checkout
```

### A sabotage that does not compile looks exactly like one that failed

This is the entry above at the worst possible moment: **while you are proving
a test can fail.**

The ritual is right and this file asks for it — *break the library on purpose,
watch the assertion go red, put it back.* The trap is that a sabotage is
usually a small edit made quickly, in a tree built with `-Werror`, and the
edits that come to hand are exactly the ones that stop compiling: change a
constant and its old value becomes an unused variable; take a parameter out of
a calculation and it becomes an unused parameter. **The build fails, the old
binary runs, the test goes red, and red is what you were hoping for.**

You then write down that the test catches the bug. It may not catch anything.

**So a sabotage has to print the failure it caused, not merely fail.** Not
`FAILED` but the two numbers, in the test's own words:

```
index 0: EM_POSFROMCHAR says 21, the caret is drawn at 19
FAILED  planting src/.package-invariant-243211.o moved the hash:
          without it  ween32-0.1.0-jgasIMLBSACaby9G1B66j7U7UE4kV7mCqpXMLnvn8Q22
          with it     ween32-0.1.0-jgasIOTBSAAAd7jg3MfNCbSlbb1aU9gwq6m2gW19GuOh
```

**A stale binary cannot produce that text**, because the text is about the
change you just made and the stale binary has never seen it. A bare `FAILED`
is produced identically by a working check and by a build that did not happen;
a failure that describes its own cause is produced only by the first.

**And check the exit status separately from the text.** One of these was found
because `| tail` had been swallowing it — a pipeline reports the status of its
last command, so `make 2>&1 | tail` is always 0 and a build failure reads as
a build.

### Cleanup belongs on the path a failure takes

**Two people, two languages, one evening, and both of them in commits that were
about cleanup.**

```
python   the instruments' mkdtemp was removed after the last print
shell    a scratch file's trap was dropped when a second file was added
```

Both are the same shape: the cleanup sits at the end of the success path, so a
run that finishes tidies up and **a run that dies leaves everything behind**.
And the run that dies is the run somebody repeats — five times, while working
out what is wrong — so the leak is not merely biased towards failure, it is
*multiplied* by it.

It ended with 2168 leaked directories and 15GB in `/tmp`, and the next build
died of `ENOSPC`. **A tool that measures the program should not be the thing
that stops it building.**

The rule is not "remember to clean up", which everybody already intends. It is
**put the cleanup where a failure goes**: a `trap` in shell, `try/finally` or
an `except` that re-raises in Python, `defer` in Zig. And then, because this is
this section: make it fail on purpose and look at the directory.

**The general form, which is worth more than the three instances:** a check is
only as good as its worst path, and **the worst path is the one nobody runs on
purpose.** Every entry in this section is an instance of it — the un-compiled
sabotage is the failure path of a test, the stale binary is the failure path of
a build, and this is the failure path of a tool.

### What a guess is reasoned from

Everything above is about numbers that are wrong. This is about numbers that
are *absent* — the places where nothing has been measured and something has to
ship anyway. Three shipped in WordPad's §8.8 on 2026-08-29, all three labelled
as guesses in the same words and the same tone. **Two were right and one was
wrong**, and the difference was visible before any of them was measured:

| the guess | reasoned from | result |
| --- | --- | --- |
| Find wraps at the end of the document | the box has **no wrap checkbox** where Notepad's has one, so there is no choice to offer | right |
| Find Next is greyed until a search has been made | the command has **nothing to repeat**, which is the program's own state | right |
| a failed search says `Cannot find "x"` | **Notepad says something like that** | wrong |

The two that survived were reasoned from **this program's own structure** — a
control that is absent, a state that cannot exist. The one that failed was
reasoned from **a sibling program**, and a sibling is seductive evidence: same
authors, same era, same job, right next door. It is still not evidence.

**And the wrong one was the only one nobody would ever have questioned.** The
machine says `WordPad has finished searching the document.` — it does not name
what was searched for, and it is the same message whether or not anything was
found, which is the only sentence a *wrapping* search can honestly say.
`Cannot find "x"` is not a worse wording of that; **it is a different claim**,
it names the thing you searched for, and it therefore looks *more* helpful
than the truth. Nobody files a bug against a message that reads better than
the right one.

So, before shipping a guess: **ask what it is reasoned from.** From this
program's own structure, ship it labelled. From another program, do not ship
it — go and measure, or leave the behaviour out, because when that kind of
guess is wrong it is wrong in a way that reads as correct.

### Window geometry, without a window system

The headless backend is a fake window system, not just a hole where one should
be: `ween_headless_set_window_size()` makes it hand every window a size of its
choosing, the way a tiling window manager does, and injected pointer
coordinates come back through the same mapping the X11 backend uses.

That is what `tests/geometry_test` is for. The class of bug it covers — what is
drawn and what is clicked disagreeing — is invisible to every other test,
because everywhere else the window and the buffer are the same size. If you
change anything about `ween_letterbox`, check that test still bites by
breaking it on purpose.

## 2a. The editor's invariants — what a monkey may assert

jd: *"There are many bugs around the editor. Adding text, filling up the
editor, removing text, selecting text. Doing those actions **in combination**
brings many bugs."*

A monkey test needs an oracle, and **an invariant nobody measured is a bug
report waiting to be filed against the wrong program.** This is the list of
what has actually been established, and — more usefully — which
plausible-sounding invariants are **false**.

Every entry says where it came from. `machine` means Windows 2000 through
`tools/vm/probe.c` or `ctlprobe.c`; `ours` means a test in this repository.

### Measured, and safe to assert

- **`EM_EXLIMITTEXT` truncates and tells the parent.** With a limit of 4,
  typing `abcdef` leaves `abcd` and the parent receives `EN_MAXTEXT`.
  *(ours, `richedit_test.c`)*
- **A `cpMax` of -1 in `EM_EXSETSEL` means the end of the document**, and the
  control reports back the resolved value, not the -1. *(ours)*
- **The control raises and lowers `WS_VSCROLL` on itself.** On the machine's
  `RichEdit20W`, three states in one document: empty `550081C4`, overflowing
  `552081C4`, emptied again `550081C4`. **It is not a latch.** *(machine)*
- **A vertical bar takes room from the client and does not overlay.** The
  editor's white ran to screen x 915 without the bar and x 900 with it,
  against `SM_CXVSCROLL` 16. *(machine)*
- **But the wrap width does not change with it.** Text appended until the bar
  came up, and the first line's band compared over the columns that are text
  in *both* states:

  ```
  line 1's ink ends     no bar  x 877      bar up  x 877
  differing pixels      16 of 13230, and they are the caret
  ink under the bar     none; the longest line reaches 894, inside 899
  ```

  **So wrap points of existing text ARE stable under append**, even across the
  bar appearing. WordPad's wrap width already allows for the bar, or is not
  the client width at all — which of those is unmeasured, and only the
  behaviour is asserted here. *(machine, `captures-sam/wrap-line1-*.png`)*

### Measured to be **false** — do not assert these

- **"Distinct indices have distinct positions."** In a rich edit, indices
  **7 and 8** of `"abc\r\ndef"` both answer `x 21 y 16` — the last character
  and the end of the text share a place. *(machine)*

- **"After an insert of n the length rises by exactly n."** True until the
  text limit, where the insert is truncated and `EN_MAXTEXT` fires. The
  invariant needs *unless `EN_MAXTEXT` fired since the last check*. *(ours)*

### The two that were on this list as *false* and are not

**Two entries here were wrong, both of them mine, and both were found by
measuring what the entry asserted.** They are kept rather than deleted because
the way each was wrong is different and worth having.

**1. `EM_POSFROMCHAR` does agree with where the caret is drawn — in a rich
edit.**

```
EDIT         empty, index 0         -> -1
             "abc\r\ndef", index 8   -> -1          (index == length)
RICHEDIT20W  empty, index 0         -> x 1  y 0      written
             "abc\r\ndef", index 8   -> x 21 y 16    written
             index 9, past the end  -> x 21 y 16    written, clamped
```

**riched20 answers a real position at both places the EDIT refuses**,
including the index the caret occupies after every append. *(machine, both
controls in one run — `captures-sam/re1.txt`)*

**The mistake was not the reading, it was carrying it across.** `ctlprobe`
asked an **EDIT**; that answer was written into an oracle for a **rich edit**
with a note saying riched20 was *unmeasured*. The note was true and the shape
was wrong, because the two are **different calls sharing one name**:

```
EDIT      wParam = index          the point is the return value, or -1
RICHEDIT  wParam = POINTL *out    lParam = index; the result is always 0
```

**A reading of one is not weak evidence about the other; it is none.** A
signature that differs is the tell, and it is the same shape as reading
`BringWindowToTop` off its name.

They also break lines differently: for `"abc\r\ndef"` the EDIT puts indices 3
and 4 both at the end of line 1, and riched20 puts index 3 at the end of line
1 and index 4 at the **start of line 2**.

**2. The wrap points of existing text are stable under append** — the entry
above under *measured and safe*. This section said the opposite: that raising
the scrollbar re-wraps the document. **The client narrowing was measured and
the re-wrap was inferred from it**, and the inference was written in the same
voice as the reading.

**The first was a reading carried between two controls; the second was a
reading carried one step further than it went.** Neither was a wrong number.
Both were a true measurement asserted about something it was not of — which
is the failure this whole section exists to catch, committed twice in the
file that catches it.

### Not measured — assert only as our own design, and say so

- `0 <= sel.start <= sel.end <= length`. **Whether the control normalises a
  backwards range is unmeasured**; only the -1 case has been read.
- The caret's index lies within the text.
- `EM_LINEFROMCHAR(length) == EM_GETLINECOUNT - 1`. Related and measured: a
  document ending in a break has an empty last line that counts.
- First visible line <= line count.
- **Whether the bar appears at exactly-fits or only at exceeds.** Three states
  were read — empty, well over, empty again — and the boundary was not one of
  them, so *iff* is stronger than the reading.

### And the rule this list exists to enforce

**Six of today's defects were a true sentence restated one level stronger than
its source** — a function's name read as its behaviour, a picture read as a
program, a colour count read as a cause, a user's *should not* read as a
measurement. **A monkey applies its oracle thousands of times**, so an
invariant that is wrong at one boundary is not a small error; it is a failure
report per run, in the wrong file, drowning the real ones.

**When the monkey fails, the first question is which of these it used.**

## 3. By hand, on screen

```sh
./examples/controls      # every control
./examples/menu          # menus, message box, modal dialog
./examples/dialog        # the classic frame and dialog-unit layout
./examples/calc
```

**Before you start, know your window manager.** On a tiling one (i3 and
friends) a window is given its tile whatever it asked for. ween32 handles the
two cases differently, on purpose:

- `examples/controls` has a sizing border, so it *follows* the tile and lays
  itself out at that size;
- `examples/menu` and `examples/dialog` do not, so they stay the size they
  asked for and sit centred with a dark border around them.

Both are correct. Float the window (`$mod+Shift+space` on i3) to see either at
its natural size, and to drag its caption or its resize grip at all.

### What to try, and what should happen

**Menus** (`examples/menu`)

- Click **File**. The drop-down opens and File stays highlighted.
- Slide sideways onto **Edit** with the button still down — the drop-down
  changes as you go.
- Hover **Recent files**. The submenu opens *beside* it and the File menu
  stays up with the item highlighted. Move back onto **Open...** and the
  submenu closes.
- **Escape** inside the submenu leaves the submenu, not the whole menu. A
  second Escape closes it.
- Press **Alt**: the bar opens on File's first item. **Right** walks to Edit,
  **Left** back. **Down**/**Up** move within a drop-down and skip separators
  and the greyed **Paste**.
- Press a letter: **x** picks Exit, **o** picks Open.
- **Alt+F** opens File directly.
- **Ctrl+N** and **Ctrl+O** work with no menu open — those are accelerators.
- **Paste** is grey and cannot be chosen. **Word wrap** has a tick that
  toggles.

**Dialogs** (`examples/menu`)

- **Message box** opens one sized to its two lines of text, with a default OK.
  Enter, Escape and clicking OK all close it, and the status line below says
  so afterwards.
- **Modal dialog** opens one you cannot get behind: the sampler ignores clicks
  while it is up. OK and Cancel report differently in the status line.

**Keyboard** (`examples/controls`)

- **Tab** walks the controls; the focused button shows a dotted rectangle.
  **Shift+Tab** goes back. Disabled controls are skipped.
- **Space** presses the focused button or ticks the focused check box.
- With an option button focused, **Down**/**Up** move the selection within the
  group and wrap.
- Click into **Occupation** and watch the caret blink; type and it stays solid
  while you type.
- **Double-click** a word in it — the word is selected, with its trailing
  space. **Ctrl+C**, then **Home**, then **Ctrl+V** inserts it. **Ctrl+A**
  then **Ctrl+X** empties the field.
  (Copying leaves the word selected, so pasting *immediately* after replaces
  it with itself and looks like nothing happened. That is what win32 does.)
- Hold a **scroll-bar arrow**: one step, a pause, then it repeats until you
  let go.
- Click a control inside the **Select one** group box — the group box must not
  swallow it.

## 4. Headless, when there is no display

Any example runs with no window system, and can be driven by a script.

```sh
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/shot.bmp ./examples/controls
```

`WEEN32_SCRIPT` feeds it input — `d:`/`u:`/`m:` are mouse down/up/move at
window coordinates, `D:`/`U:` the right button, `k:` a virtual-key code, `K:`
the same with Shift held, `t:` characters to type, `w:` milliseconds of timer
time to let pass:

```sh
WEEN32_HEADLESS=1 WEEN32_BMP=/tmp/shot.bmp \
  WEEN32_SCRIPT="d:8,28 u:8,28" ./examples/menu     # opens the File menu
WEEN32_HEADLESS=1 WEEN32_BMP="/tmp/f%d.bmp" \
  WEEN32_SCRIPT="k:18 k:39 k:40" ./examples/menu    # Alt, right, down: Edit
WEEN32_HEADLESS=1 WEEN32_BMP="/tmp/f%d.bmp" \
  WEEN32_SCRIPT="K:9" ./examples/explorer           # Shift+Tab
```

Two down/up pairs with no `w:` between them are a double click, which is how
a column divider is asked to fit its column.

A path with `%d` in it writes **one file per frame** instead of one per run.
That is the only way to see a modal window — a message box or a drop-down is
gone again by the time the run ends, so a single shot at the end never
contains one:

```sh
WEEN32_HEADLESS=1 WEEN32_BMP="/tmp/frame%d.bmp" \
  WEEN32_SCRIPT="d:30,75 u:30,75" ./examples/menu
# /tmp/frame2.bmp is the message box
```

Frames come out at the size of whichever window was presented, so a frame
smaller than the main window is the popup or dialog.

## 5. Other display scales

The tests pin `WEEN32_DPI=96`, so nothing else is covered by them:

```sh
for d in 96 120 144 192; do
  WEEN32_HEADLESS=1 WEEN32_DPI=$d WEEN32_BMP=/tmp/dpi$d.bmp ./examples/controls
done
```

96 and 192 should be identical bar the doubling; 120 and 144 pick a nearer
font strike and are worth a look rather than a diff.
