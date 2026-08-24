# Patching wine's Tahoma so it measures what it draws

`examples/explorer.c` compiles against the real win32 headers, and running
what that produces beside the ween32 build is how the places ween32 is more
permissive than comctl32 get found —
[docs/testing.md](../../docs/testing.md#the-same-source-built-as-win32) has
the recipe. One thing has to be dealt with first, and it lives here.

## Why the font is patched

Tahoma carries embedded bitmap strikes and GDI draws the 11-pixel one at eight
points. So does ween32. So does wine — and then wine measures something else:
it scales and hints the outline, and answers `GetTextExtentPoint32` a tenth
wider than what it just drew. Every width an application works out from a
measurement inherits that, so the win32 build lays itself out on numbers the
machine never had.

`gdi_advances.py` writes a Tahoma whose advances are the ones wine's own
scaling has to arrive at, so that it measures what it draws.
`tahoma11_units.txt` is the table it writes them from, found character by
character by binary search against wine itself until every one measured what
the strike draws.

    tools/winecmp/gdi_advances.py fonts/tahoma.ttf /tmp/tahoma.ttf \
        tools/winecmp/tahoma11_units.txt 11
    cp /tmp/tahoma.ttf "$WINEPREFIX/drive_c/windows/Fonts/tahoma.ttf"

Use a prefix of its own: the stored wine references in `tools/refcapture` were
captured with the stock font.

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
