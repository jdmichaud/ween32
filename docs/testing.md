# Testing ween32

Three layers, cheapest first. The first two are what CI runs and take under a
minute; the third is the one that needs your eyes, and only that one.

## 1. The automated suite

```sh
make clean && make
make test
```

Expect **974 `ok` lines and no `FAIL`**. The count only goes up — if it has
dropped, a test file stopped being built rather than a test starting to pass.

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
tools/refcapture/pxdiff.py                  # expect 15749 / 298596 — 5.3%

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/m.bmp ./examples/menu
magick /tmp/m.bmp /tmp/m.png
PXDIFF_REF=tools/refcapture/menu-reference.png PXDIFF_OUR=/tmp/m.png \
  tools/refcapture/pxdiff.py                # expect 4397 / 39200 — 11.2%
```

Most of both — 3170 of the menu's and 7835 of the sampler's — is one thing:
the caption's gradient. Where it stops is measured off the machine, which
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

The ramp itself is stepped in 16.16 with the step rounded down before it is
accumulated, not divided per pixel. The two are the same everywhere except
where a channel would land exactly on an integer — a quarter, a half and
three quarters along — and there the dropped fraction leaves the machine's a
shade below. All 305 pixels of Column Settings' gradient come out on the
machine's with it, and three of them do not without it.

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
  --sock=/tmp/jslinux-<you>.sock --shm=/dev/shm/jslinux-<you>.fb &
```

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

```sh
# View > Choose Columns — the 330x313 frame is the dialog. Opened by key, so
# the mnemonics are underlined, which is how the capture has them.
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/cc%d.bmp \
  WEEN32_SCRIPT="w:300 k:18 w:200 t:vc w:800" ./examples/explorer /tmp/many

# Tools > Folder Options — the 384x469 frame is the sheet; the tabs are at
# y 42, at x 30 / 75 / 135 / 205
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/fo%d.bmp \
  WEEN32_SCRIPT="w:300 k:18 w:200 t:to w:900" ./examples/explorer /tmp/many
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

```sh
# five downs picks CONFIG.SYS in the fixture's list, Alt+Enter opens it; the
# 367x443 frame is the sheet. Four downs picks boot.
WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
  WEEN32_BMP=/tmp/pp%d.bmp \
  WEEN32_SCRIPT="w:300 k:40 k:40 k:40 k:40 k:40 w:200 a:13 w:900" \
  ./examples/explorer
PXDIFF_REF=tools/refcapture/properties-machine.png PXDIFF_OUR=/tmp/ours.png \
  tools/refcapture/pxdiff.py                  # expect 640 / 162581 — 0.4%
```

| what | differing | what it is |
| --- | --- | --- |
| CONFIG.SYS | 640 | 453 the caption's bold title, 145 the icon quantised, 42 two glyphs |
| boot | 294 | 236 the two icons quantised, 48 the same two glyphs and the title |

Both were counted on the day the captures were taken. The Accessed row is
today's date on either side, so on any other day it differs too — 22 pixels
where the day of the month is.

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

A 100x26 strip around the box, ours against the machine's, differs by
**113 pixels of 2600** — every one of them in the seven columns of the file's
icon at the left of the strip, which is the quantisation the whole fixture
has. The box itself, its border, its white, its blue and its letters are
identical.

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
ending exactly on a paragraph's first character takes that paragraph. Both
want a run of the probe when 4a next boots a machine.

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

### The caption's ramp, on a window wider than the ones we had

WordPad's frame is 768 wide where the explorer's is 654, and at that width
the caption band differs from the machine by **6079 pixels of 14,592** while
the menu band under it and the frame's own top rows differ by **0**. So it is
the gradient alone, and it is not a colour: **340 of 768 columns are out by
one, in one channel**, and every sampled pixel that differs differs by ±1.

Taken apart along a caption row clear of the title and the icon, the two
ramps agree about almost everything:

| | machine | ours |
| --- | --- | --- |
| steps in R / G / B | 157 / 167 / 135 | **the same** |
| the last four R steps | 693, 698, 702, 706 | **the same** |
| the first R step | **23** | 21 |
| the first B step | **24** | 22 |

The span is right, the end is right, the number of steps is right; **ours
starts two pixels early and the two converge**, so the offset runs 2, then 1,
then 0 across the width (38 columns at 2, 78 at 1, 41 already together). A
uniform ramp from the caption's left edge would take its first step at pixel
4 or 5, and the machine takes it at 23 — so whatever it does at the start is
not a straight interpolation, and shifting ours by one or two does not fix it
either: the best whole-pixel shift still leaves 190 columns out.

**The machine's own ramp is now in the repository at three more widths** —
`tools/refcapture/caption-400-machine.png`, `-500-`, `-654-`, each the top 30
rows of a WordPad frame sized with `SetWindowPos` from inside the guest — so
the next attempt needs no machine. What they say:

- the ramp has **157 steps in R, 167 in G and 135 in B at every width**: the
  colour is walked, not the pixels;
- it **ends 55 pixels before the client's right edge** at every width, which
  is the room the three caption buttons take;
- and it **starts about eighteen pixels in**, not at the caption's left edge:
  the first step lands at 21, 21, 22, 23 and 24 for widths 400, 500, 654, 768
  and 900, where a ramp from the edge would put it at 3, 4, 5, 5 and 6.

**And the rule is nearly in hand.** Taken channel by channel rather than all
three at once, the machine's ramp is the plain linear one with **its first
nineteen pixels holding the start colour**:

    value(i) = start + floor((i - 19) * (end - start) / (last - 19))

which matches **627 of R's 706 columns, 622 of G's and 638 of B's** — about
89% each, where the naive ramp from the caption's own left edge matches 8%.
The nineteen is a constant and not a fraction of the width: it predicts the
first step at 21, 22, 23, 24 and 25 for widths 400, 500, 654, 768 and 900,
against the 21, 21, 22, 23 and 24 measured. And it is where the caption's
**icon** ends — 16 pixels of icon from x 6, plus a column — which is a reason
rather than a coincidence.

What is left is the last tenth: the columns where floor is off by one. Try
`MulDiv`'s rounding, or a fixed-point accumulator, against the four captures.

**Whoever takes this should expect it to move every capture that has a
caption in it** — which is most of them — so it wants its own task with every
number re-measured, not a line slipped into another change.

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

Two more, both about what a gate is *shaped* to notice:

- **the binding gate is a spell-checker, not a dictionary.** It checks that
  what `zig/ween32.zig` declares agrees with the header, which is one
  direction. A name the module never mentions is a name it never disagrees
  with, and absences are reported only as a total -- "361 of the header's not
  declared in Zig yet". `STATUSCLASSNAMEA` sat in that total, so a Zig program
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
