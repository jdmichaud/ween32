# Testing ween32

Three layers, cheapest first. The first two are what CI runs and take under a
minute; the third is the one that needs your eyes, and only that one.

## 1. The automated suite

```sh
make clean && make
make test
```

Expect **291 `ok` lines and no `FAIL`**. The count only goes up — if it has
dropped, a test file stopped being built rather than a test starting to pass.

Then the three things `make test` does not cover:

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
tools/refcapture/pxdiff.py                  # expect 6038 / 298596 — 2.0%

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/m.bmp ./examples/menu
magick /tmp/m.bmp /tmp/m.png
PXDIFF_REF=tools/refcapture/menu-reference.png PXDIFF_OUR=/tmp/m.png \
  tools/refcapture/pxdiff.py                # expect 741 / 39200 — 1.9%
```

Roughly 242 of that 1.8% is the menu bar, and is *deliberate*: wine spaces bar
items by twelve pixels and Windows by sixteen, and ween32 follows Windows. The
rest is the caption's bold title, which ween32 synthesises. Do not "fix" the
bar back toward wine — check it against a screenshot of Windows instead.

About 3100 of the controls sampler's 1.9% is the tree and the list view, and
is deliberate in the same way. The tree's indent, the column its buttons sit
in and the pixel of white above its first row are measured against a Windows
2000 shell tree; the list's two rows of white between its header and its first
item, and its header's text sitting six in rather than eight, against the same
machine's list. See the ROADMAP for what was measured.

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
window differs by 3243 of 355776 pixels, 0.9%:

```sh
WEEN32_EXPLORER_FIXTURE=1 WEEN32_HEADLESS=1 WEEN32_DPI=96 \
  WEEN32_BMP=/tmp/ts%d.bmp WEEN32_SCRIPT="w:200 d:140,179 u:140,179 w:400" \
  ./examples/explorer
tools/vm/grab.py /tmp/machine.png 132 132 654 544
```

2576 of those 3243 are ours quantised to 5-5-5 — the machine draws every icon
through a sixteen bit image list, so its pixel is ours with the low three bits
of each channel dropped and the top bits shifted back in. What is left over is
667:

| band | differing | left over | what it is |
| --- | --- | --- | --- |
| menu band | 228 | 228 | the animation the shell plays at its right |
| list pane | 947 | 183 | two file icons whose art has a near-white where ours has white, and six pixels of a date |
| caption | 118 | 118 | the bold title — wine's Tahoma Bold is not the machine's |
| left pane | 1835 | 137 | the picked drive's icon: ween32 blends a selected icon halfway into the highlight and the machine does not |
| status bar | 1 | 1 | one pixel of a letter |
| toolbar band | 0 | 0 | |
| address band | 114 | 0 | |

The tree pane is window-relative x 4..203, y 100..519. Everything in it
matches but the icons, and every icon difference but the picked one is the
quantisation.

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
