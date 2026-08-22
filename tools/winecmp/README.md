# The explorer, built as win32, beside the same source on ween32

`examples/explorer.c` compiles against the real win32 headers, and running what
that produces is how the places ween32 is more permissive than comctl32 get
found: an application leans on the difference without knowing, and the win32
build comes out wrong. Every one of these came from doing it — bands that had
never asked to break, toolbars that had never said where they go, a heading
that lost its name when the sort arrow was set, status-bar text put in parts
that did not exist yet, a menu title that was not a drop-down.

    Xvfb :99 -screen 0 1024x768x24 &
    export DISPLAY=:99 WINEPREFIX=$HOME/.cache/ween32-winecmp
    zig cc -target x86_64-windows-gnu -std=c99 -Iinclude examples/explorer.c \
        -luser32 -lgdi32 -lcomctl32 -o examples/ween-explorer.exe
    wine regedit tools/refcapture/win2000.reg
    cp fonts/*.ttf "$WINEPREFIX/drive_c/windows/Fonts/"
    tools/winecmp/gdi_advances.py fonts/tahoma.ttf /tmp/tahoma.ttf \
        tools/winecmp/tahoma11_units.txt 11
    cp /tmp/tahoma.ttf "$WINEPREFIX/drive_c/windows/Fonts/tahoma.ttf"
    WEEN32_EXPLORER_FIXTURE=1 wine explorer /desktop=cmp,760x600 \
        'Z:\...\examples\ween-explorer.exe' &

A prefix of its own, because the stored wine references in `tools/refcapture`
were captured with the stock font.

## Why the font is patched

Tahoma carries embedded bitmap strikes and GDI draws the 11-pixel one at eight
points. So does ween32. So does wine — and then wine measures something else:
it scales and hints the outline, and answers `GetTextExtentPoint32` a tenth
wider than what it just drew. Every width an application works out from a
measurement inherits that. `gdi_advances.py` writes the advances wine's own
scaling has to arrive at, so that it measures what it draws; `fit.py` is how
those numbers were found, by binary search against wine itself, and
`tahoma11_units.txt` is the answer.

## What still differs, and why

Two things, and both are wine against Windows 2000 rather than ween32 against
win32:

- **A list's rows.** ween32 makes a row `max(icon, text) + 1` — 17 pixels for
  a 16-pixel icon and Tahoma, which is what the machine has, measured off it.
  Wine's comctl32 makes it 16, so the rows walk apart down the list. It cannot
  be talked into 17: a 16-pixel image list gives 16 and a 17-pixel one gives
  18, and telling the font to report a taller height gets the rows right at the
  cost of the caption and the address bar, which are sized from the same
  number. Both were tried; neither is worth having.
- **The caption.** A window manager does not help either — wine paints the
  inactive caption over a window it agrees is active.
Compare the geometry rather than the picture when you want an answer you can
act on: paste a block into `layout()` that dumps every control's rectangle,
compile that one file both ways, and diff the two dumps.
