# How ween32 works

ween32 is a C99 library that lets you write plain old win32 code —
`RegisterClassA`, `CreateWindowExA`, a `WndProc`, `GetMessageA`/
`DispatchMessageA`, `BeginPaint`, `FillRect`, `DrawEdge`, `"BUTTON"` child
windows sending `WM_COMMAND` — and run it outside Windows, with the classic
Windows 2000 look reproduced pixel for pixel.

```c
#include <ween32.h>   /* on Windows this defers to <windows.h>: same code, both worlds */
```

The same program compiles **unchanged** against real `windows.h` on Windows and
against ween32 everywhere else. That dual-compile property is the project's
fidelity contract, and CI enforces it on every example.

The flagship example is the classic Windows calculator, recreated in
`examples/calc.c` as pure win32 code — built the authentic way from a **dialog
template**: the controls are declared once in dialog units and the dialog
manager (`CreateDialogIndirectParam`) instantiates them and maps their DLUs to
pixels. Owner-drawn colored keys (`BS_OWNERDRAW`/`WM_DRAWITEM`), memory keys,
`Enter`=equals via `IsDialogMessage`, and all.

## Why it looks right

- **Software rendering, no 2D library.** Every pixel is drawn by ween32 into a
  plain `uint32_t` buffer. The classic look is crisp aliased 1px lines and
  exact palette colors — antialiasing vector libraries fight that; a ~500-line
  rasterizer nails it.
- **The authentic algorithms.** The 3D bevel is Wine's `DrawEdge`; the system
  colors are the Win2000 `GetSysColor` defaults; the caption is the real
  navy→blue gradient.
- **The authentic text.** Small GDI text was never rasterized from outlines: it
  came from hand-tuned 1-bit bitmaps embedded in the font. ween32 reads the
  embedded strikes (EBLC/EBDT) directly — Tahoma and its bold cut for the
  shell, MS Sans Serif for dialogs, which is what "MS Shell Dlg" resolves to
  on this Windows — the same glyphs GDI showed. Caption glyphs (the close box
  ✕) come from Marlett's outlines, scanline-filled with no antialiasing, as
  `DrawFrameControl` drew them.
- **The authentic layout.** Dialogs position controls in **dialog units** via
  `GetDialogBaseUnits`/`MapDialogRect` (`px = MulDiv(dlu, base, 4 or 8)`), so
  the standard 50×14 DLU button comes out 75×23 px — nothing is hand-placed.

## Architecture

```
 your win32-style C code
 ────────────────────────────────────────────────
 win32 API layer      user.c  gdi.c  draw.c  dialog.c  menu.c
                      controls.c  propsheet.c  comdlg.c  imagelist.c
 software engine      surface.c classic.c font.c fonts.c marlett.c  (zero deps)
                      shellart.c  cursorart.c   the art that is not a font
 presentation         x11.c (XPutImage blit + input)  headless.c (tests)
```

`controls.c` holds the window classes an application asks for by name — the
five USER32 ones and the common controls, including the two views a shell is
built from — and `draw.c` the drawing half of GDI that a paint program needs.

A top-level window owns one native (X11) window and one surface; the library
draws the non-client chrome (caption, close box, drag) in `DefWindowProcA`,
controls are child windows hit-tested down the tree — USER32's shape, scoped
down. The backends only blit the finished buffer and report input; they never
draw. On Windows there is no backend: your code just uses the real thing.

## Build

```sh
make            # libween32.a + the examples (libX11 to run them live)
make test       # headless test suite: engine pixels + full API path, no display needed
make X11=0      # library without the X11 backend
```

Any example also runs without a display: `WEEN32_HEADLESS=1 WEEN32_BMP=shot.bmp
WEEN32_SCRIPT="d:113,169 u:113,169" ./examples/calc` renders to a BMP, driven
by scripted input — that is how the screenshots were made and how CI exercises
real apps.

The dual-compile gate (needs zig or a mingw toolchain):

```sh
zig cc -target x86_64-windows-gnu -std=c99 -Iinclude examples/dialog.c -luser32 -lgdi32
```

There is also a Zig package: `zig build` builds the static library, and
`zig/ween32.zig` exposes the same API as a Zig module — linking the C library
off Windows and the real user32/gdi32 on it.

## DPI

The classic win32 model, faithfully: one system dpi, fonts sized in points
against it, and dialog-unit layout follows the font — so apps scale with zero
code changes. The dpi is detected from the desktop's `Xft.dpi` X resource (what
GNOME/KDE/xrdb set when you scale the UI — the same source GTK/Qt honor;
physical monitor size is deliberately not trusted). `WEEN32_DPI` overrides it,
and headless runs default to 96 for determinism:

- **120 / 144 (125% / 150%)**: the GUI font picks Tahoma's real 13px / 16px
  strikes (`MulDiv(8pt, dpi, 72)`, snapped to shipped strikes), non-client
  metrics scale like the classic `SM_*` system metrics, and
  `GetDialogBaseUnits`/`MapDialogRect` re-lay dialogs automatically.
- **192+ (200%, small hi-res screens)**: renders at 96 dpi and the backend
  pixel-doubles the finished frame — perfectly crisp, authentically chunky.
- `GetDpiForSystem()` (and `AdjustWindowRect`) are available to apps; on real
  Windows declare DPI awareness in the manifest and the same mechanism runs
  in genuine GDI.

## Fonts and licensing

The library code is MIT. The embedded fonts are Wine's redistributable Tahoma,
its bold cut, MS Sans Serif and Marlett replacements (see Wine's `fonts/` —
LGPL); they carry their own license, not MIT. They are build-time embedded byte
arrays in `fonts/*_ttf.h`; swap the files and regenerate (`xxd -i`) to use
different metrics-compatible fonts.
