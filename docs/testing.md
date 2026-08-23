# Testing ween32

Three layers, cheapest first. The first two are what CI runs and take under a
minute; the third is the one that needs your eyes, and only that one.

## 1. The automated suite

```sh
make clean && make
make test
```

Expect **437 `ok` lines and no `FAIL`**. The count only goes up — if it has
dropped, a test file stopped being built rather than a test starting to pass.

Then the four things `make test` does not cover:

```sh
# that the same example source still builds against the real windows.h, and
# that every constant ween32 declares is the number Windows gives it
make win32
```

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
tools/refcapture/pxdiff.py                  # expect 15017 / 298596 — 5.0%

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/m.bmp ./examples/menu
magick /tmp/m.bmp /tmp/m.png
PXDIFF_REF=tools/refcapture/menu-reference.png PXDIFF_OUR=/tmp/m.png \
  tools/refcapture/pxdiff.py                # expect 3931 / 39200 — 10.0%
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
General — and the machine is followed.

Fourteen of the rest are two mnemonic underlines a control draws only once Alt
has been pressed, which wine draws always — the same rule that keeps a menu's
underlines out of sight, applied to the controls in a dialog.

Roughly 240 of what is left is the menu bar, and is *deliberate*. A bar item
is its label plus twelve pixels of padding, half each side, which is what
Paint's own bar measures on the machine — the gap between one label's ink and
the next is a constant thirteen across File, Edit, View, Image and Colors.
It was sixteen here for a while, off the explorer's bar; but the shell's bar
is a *toolbar* of drop-down buttons in its rebar, not a menu bar at all, and a
toolbar button's padding is not a menu item's. The rest is the caption's bold
title, which ween32 synthesises. Do not "fix" the bar by eye — measure it
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

**Column Settings** differs by **1148 of 103290**, 1.1%. Its scroll bar is
718 of that and is content, not drawing: the machine's shell offers some
fifty columns where this example has eight, so its thumb is a fifteenth of
the trough and ours five sixths of it. Another 252 is the caption's bold
title. What is left is 58 pixels where wine's MS Sans Serif draws a `k` or a
`v` a pixel off the machine's, and 77 where a bordered edit box starts its
text: the machine's begins two further in with that font, and the rule that
gives both that and Tahoma's three — wine's, half the average character
width — has not been found.

Each of Folder Options' four pages is counted against its own capture. The
tabs are at y 38, at x 30 / 80 / 135 / 205; the 386x468 frame is the sheet:

| page | differing | of 180648 | what is left |
| --- | --- | --- | --- |
| General | 1228 | 0.7% | the machine's own mouse pointer in the shot, the caption's bold title |
| View | 1411 | 0.8% | the same two, the folder a heading wears, the scroll bar |
| File Types | 9719 | 5.4% | the list's contents |
| Offline Files | 1041 | 0.6% | the same two, and the arrows' bevel |

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

Two things are in every one of these counts and in the Column Settings one:
the caption's bold title, which ween32 synthesises, is about 300, and the
machine's mouse pointer, which is in the screenshot and not in ours, another
300 where it happens to sit.

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
    -luser32 -lgdi32 -lcomctl32 -o examples/ween-explorer.exe
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
that is chasing wine.
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
