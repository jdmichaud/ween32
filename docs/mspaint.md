# Paint

`examples/mspaint` is Windows 2000's Paint, written to win32 and running on
ween32. It is in Zig — through the `ween32` module the package already
exposed — and the same source compiles for Windows, where the module's
declarations are the real USER32, GDI32, COMCTL32 and COMDLG32.

![Paint, on ween32](paint.png)

The window is the machine's, not an impression of it. Held up against a
Windows 2000 running the real program, it differs by **six pixels of
110,000**: three where ween32 synthesises the caption's bold title, and three
where Paint's MFC status bar draws two empty panes as zero-height boxes
beside the size grip. The tool box, the settings box under it, the colour
box, the menu bar and the view are pixel for pixel, in every one of the
sixteen tool states.

## Where the numbers came from

Nothing about the layout is invented, and nothing was eyeballed off a
screenshot where the machine could be asked instead.

**The windows.** `tools/vm/probe.c` runs inside the machine, walks Paint's
own window tree and prints every rectangle, style and control id:

```
MSPaintApp            0,0   275x400   client 267x354
  AfxControlBar42u    4,42   57x282   "Tools"
  AfxFrameOrView42u  61,42  210x282   the view, with a client edge
  AfxControlBar42u    4,324 267x49    "Colors"
  msctls_statusbar32  4,373 267x23
  ToolChild           8,42   25x25    ...sixteen of them
```

The tool box is 57 wide because Paint's is 57 wide. The status bar is 23
because MFC makes Paint's 23 — taller than the font implies, which is why it
is created with `CCS_NORESIZE` and placed by the application.

The probe is built with no C runtime at all (`zig cc -nostdlib`): mingw's
imports the `api-ms-win-crt-*` stubs, which Windows 2000 has never heard of,
and its PE header has to say NT 4.0 or the machine calls it "not a valid
Win32 application" (`tools/vm/pe2k.py`).

**The menu** is the menu the probe read out of it, item ids, accelerator
text and greyed states included, and the line each item puts in the status
bar is the machine's own — checked against it by hovering and reading the
bar back.

**The dialogs.** The probe dumps a dialog's controls the same way. Every
pixel it reports divides exactly by the dialog base units (6 and 13), so
`examples/mspaint/dialogs.zig` writes them in dialog units and the dialog
manager puts them back on the same pixels.

**The pictures.** The sixteen tool glyphs and the icon in the caption are cut
out of screenshots of the machine and generated into Zig by
`tools/mspaint/genart.py`. The settings box is generated too, by
`tools/mspaint/genoptions.py`, from one screenshot per setting: the pixels
that change when a setting is chosen are that setting's rectangle — which is
also where a click on it lands — and that rectangle out of its own
screenshot and out of any other are the two pictures to draw. Nothing is
interpreted, so a colour picture and a one-bit glyph need no distinction.

**The tools.** The twelve brushes are twelve shapes, not sizes of a formula:
a click with each on the machine, read back off the screen. The disc a wide
pen puts down is the machine's too — two across is a full square, three a
plus, four and five squares with the corners off.

## Checking it

```sh
zig build mspaint
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/paint.bmp ./zig-out/bin/mspaint
```

`tools/refcapture/paintdiff.py` compares that render with the machine's,
totalled by region — the tool box, the view, the colour box — so a total is
attributable rather than a percentage of a window:

```sh
tools/refcapture/paintdiff.py               # totals, by region
tools/refcapture/paintdiff.py 4 42 57 40    # one region, as an ASCII map
```

It wants `tools/refcapture/paint/00-default.png`, a screenshot of the
machine's Paint window taken with `tools/vm/drive.py`. Those captures are
gitignored: they are pictures of someone else's program, and they can be
taken again.

`tools/mspaint/compare.py` draws the *same thing* on both and counts the
difference:

```sh
tools/mspaint/compare.py "tool 12" "drag 76,57 126,87"     # a rectangle
tools/mspaint/compare.py "tool 7" "option 5" "click 120,100"  # one brush
```

It calibrates first. The guest's mouse is relative, and asking for (120,160)
can leave the pointer at (120,159) — consistently, and for reasons of its
own. That one pixel is not a difference in what Paint draws, but it moves
every pixel of a shape, so the numbers are calibrated by leaving a pencil dot
at each point of the gesture and reading back where they landed; our copy is
then driven with *those* coordinates.

What matches exactly: a pen dot at every width, horizontal and vertical lines
at every width, one-pixel lines at any angle, all twelve brushes, the four
rubber sizes, the flood fill, and a rectangle.

What does not:

| | how far off | why |
| --- | --- | --- |
| a wide line at a shallow angle | ~60 px of 400 | GDI sweeps the pen as a region; ween32 stamps it along a four-connected walk, and the two round the ends of that region differently |
| an ellipse | ~86 px of an 80x60 outline | GDI's ellipse comes out flatter across the top than the mathematically inscribed one ween32 draws |
| a rounded rectangle | the same, in its corners | the same arithmetic |
| the dialogs' text | every glyph | Windows dialogs are set in MS Sans Serif; ween32 has only Tahoma |

The first three are ween32's rasteriser, not Paint's drawing code.

## The same source, as win32

```sh
zig build mspaint -Dtarget=x86_64-windows-gnu
```

That links the real user32/gdi32/comctl32/comdlg32 and produces
`zig-out/bin/mspaint.exe`, which runs under wine:

```sh
Xvfb :98 -screen 0 1024x768x24 &
DISPLAY=:98 wine zig-out/bin/mspaint.exe
```

It comes up with its tool box, its colour box, its menu and its status bar,
and it draws. It will not be pixel for pixel there — wine measures text one
way and draws it another, and with no window manager nothing takes the focus
— but it is the same source, and every call it makes is a call Windows
answers.

## What is not there

- **The text toolbar.** It would offer a choice of faces and sizes that
  ween32 has no rasteriser to honour, so the menu item stays greyed, as it
  is in the real one until the text tool is picked.
- **Printing**, print preview and page setup: there is no print spooler
  behind them.
- **Set As Wallpaper**, which writes a registry key on a machine that has
  one. Both items are greyed until the picture is saved, which is where they
  start.
- **Paint's own cursors.** Each tool has a drawing of its own — a pencil, a
  bucket, a brush — and those are cursor resources; ween32 has no way yet to
  make a cursor out of a bitmap, so the nearest stock shape is used.
- **Custom zoom** and the free-form select's lasso: the first takes the
  fixed magnifications, the second takes a rectangle.
