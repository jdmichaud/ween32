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
tools/refcapture/pxdiff.py                  # expect 13318 / 298596 — 4.5%

WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/m.bmp ./examples/menu
magick /tmp/m.bmp /tmp/m.png
PXDIFF_REF=tools/refcapture/menu-reference.png PXDIFF_OUR=/tmp/m.png \
  tools/refcapture/pxdiff.py                # expect 695 / 39200 — 1.8%
```

Roughly 242 of that 1.8% is the menu bar, and is *deliberate*: wine spaces bar
items by twelve pixels and Windows by sixteen, and ween32 follows Windows. The
rest is the caption's bold title, which ween32 synthesises. Do not "fix" the
bar back toward wine — check it against a screenshot of Windows instead.

About 8000 of the controls sampler's 4.5% is its caption: the gradient now
stops three pixels before the leftmost caption button rather than one button's
width from the right edge, which is where the machine's stops. The sampler's
window has one button and the machine's three, so the two cannot be checked
against each other; the machine is followed and every column of the sampler's
gradient lands a shade off wine's. About 3100 more is the tree and the list
view, and
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
The tree pane is window-relative x 4..203, y 100..519. As of the last pass
everything in it matches but the icons: every differing pixel is inside a
16-pixel icon column, and all but two of those icons are ours quantised to
5-5-5, which is the machine drawing them through a sixteen bit image list.
The two that are not are My Computer and My Documents, whose icons are not
in `assets/icons`.

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
window coordinates, `k:` a virtual-key code, `t:` characters to type, `w:`
milliseconds of timer time to let pass:

```sh
WEEN32_HEADLESS=1 WEEN32_BMP=/tmp/shot.bmp \
  WEEN32_SCRIPT="d:8,28 u:8,28" ./examples/menu     # opens the File menu
```

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
