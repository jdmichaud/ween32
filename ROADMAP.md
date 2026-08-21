# Roadmap

ween32 is in progress. This is what the library does today, what it does not do
yet, and the shortcuts taken to get here.

The control inventory below is the component list from
[98.css](https://jdan.github.io/98.css/), which works through the same
*Microsoft Windows User Experience* guide the classic shell was built from. We
take that **list**, not that **look**: ween32 targets the Windows 2000 classic
theme, and every control is checked against a real win32 render (see
[Reference captures](#reference-captures) below), never against the 98 styling.

## Implemented

**Windowing** — `RegisterClassA`, `CreateWindowExA`, `DestroyWindow`,
`ShowWindow`, `MoveWindow`, `GetClientRect`, `AdjustWindowRect(Ex)`,
`SetWindowTextA`/`GetWindowTextA`, `GetDlgItem`, `SetFocus`,
`SetCapture`/`ReleaseCapture`/`GetCapture`, `InvalidateRect`, `UpdateWindow`,
the message loop (`GetMessageA`/`TranslateMessage`/`DispatchMessageA`,
`SendMessageA`, `PostQuitMessage`, `DefWindowProcA`), `WM_CREATE`/`PAINT`/
`COMMAND`/`CLOSE`/`DESTROY`/`KEYDOWN`/`LBUTTON*`/`MOUSEMOVE`/`NCHITTEST`/
`NCPAINT`, `WM_ENABLE`, `EnableWindow`, and the built-in control classes:
`BUTTON` (push, default, owner-draw, check box, option button, group box, and
the disabled state), `STATIC`, `EDIT`, `LISTBOX`, `COMBOBOX`, `SCROLLBAR`, and
the common controls `msctls_progress32`, `msctls_trackbar32`,
`msctls_statusbar32`, `SysTabControl32`, `SysTreeView32` and `SysListView32`.

**GDI** — `BeginPaint`/`EndPaint`, `FillRect`, `FrameRect`, `DrawEdge` (Wine's
tables, every `BDR_`/`EDGE_` type and `BF_` flag), `DrawFrameControl`
(`DFC_CAPTION` and `DFC_BUTTON`), `TextOutA`, `DrawTextA`,
`GetTextExtentPoint32A`, `SetTextColor`, `SetBkMode`, `GetSysColor(Brush)`,
`CreateSolidBrush`, `CreateFontA`, `DeleteObject`,
`GetStockObject(DEFAULT_GUI_FONT)`, `SelectObject`.

**Dialogs** — `CreateDialogIndirectParamA` (+ `CreateDialogIndirectA`) builds a
dialog from a `DLGTEMPLATE`/`DLGITEMTEMPLATE`, instantiating each control and
mapping its dialog units to pixels; `DLGPROC` (`WM_INITDIALOG`), `DefDlgProcA`,
`EndDialog`, `IsDialogMessageA` (Tab / Enter→default / Esc), `DM_SETDEFID`,
`GetDlgCtrlID`, `GetDialogBaseUnits`, `MapDialogRect`, `MulDiv`.

**DPI** — one system dpi detected from the desktop's `Xft.dpi` (overridable
with `WEEN32_DPI`); point-sized font strikes, scaled non-client metrics, and
pixel doubling at 200%. `GetDpiForSystem`.

## Controls

Every control on the 98.css list now draws, and `examples/controls.c` renders
0.8% differently from the reference — see [Reference captures](#reference-captures)
for how that is measured, and the table there for where the difference is.

`examples/controls.c` is the acceptance test for this list. It is one sampler
holding every control, compiled two ways: against real `<windows.h>` it renders
the genuine controls (that is the reference capture), and against ween32 it
renders ours. A control **lands** when its block draws the same pixels in both:
implement the class, then add `#define WEEN32_HAS_<NAME> 1` to
`include/ween32.h`, which is what the sampler switches on.

| Control | win32 class | Drawn | Differs |
| --- | --- | --- | --- |
| Button, default, disabled | `BUTTON` | yes | 1px on one centred label |
| Checkbox, 3-state | `BUTTON` + `BS_AUTOCHECKBOX` | yes | 4px |
| OptionButton | `BUTTON` + `BS_AUTORADIOBUTTON` | yes | 2px per button |
| GroupBox | `BUTTON` + `BS_GROUPBOX` | yes | — |
| TextBox | `EDIT` | yes | glyph spacing |
| ListBox | `LISTBOX` | yes | — |
| Dropdown (closed) | `COMBOBOX` | yes | — |
| Slider | `msctls_trackbar32` | yes | 96px |
| Progress, both kinds | `msctls_progress32` | yes | — |
| Scroll bars | `SCROLLBAR` | yes | — |
| Tabs | `SysTabControl32` | yes | one tab's width |
| TreeView | `SysTreeView32` | yes | 46px |
| TableView | `SysListView32` (report) | yes | 1px |
| Status bar, size grip | `msctls_statusbar32` | yes | 17px |
| Field borders | `WS_EX_CLIENTEDGE`/`STATICEDGE` | yes | — |
| Title bar | close box, gradient | yes | Wine antialiases the glyph |

### What they do not do yet

Drawing is not behaving. What remains is interaction, and the core machinery
it needs:

- [ ] **Typing** — `EDIT` has no caret and no `WM_CHAR` to feed it, so its
      text can be set but not edited. Needs `TranslateMessage`, modifier state
      from the backend, timers for the caret blink, selection and clipboard.
- [ ] **The drop-down list** — a combo box's list is a popup window that
      escapes the parent's client area, which the single-top-level model
      cannot express (`src/user.c:303`).
- [ ] **Scrolling and selection** — the scroll bars, list box, tree and list
      views draw their state but do not respond to the mouse or keyboard:
      no thumb dragging, no click-to-select, no expand/collapse, no `WM_NOTIFY`
      to report any of it.
- [ ] **Multi-row tabs** (`TCS_MULTILINE`), item images from an image list,
      and column resizing in the list view.

## Core machinery these need

- **A second top-level window.** Only one exists today (`src/user.c:303`).
  Blocks the combo drop-down, and later menus and modal dialogs.
- **`WM_NOTIFY`/`NMHDR`.** Every common control (tree, tabs, list view, status
  bar) reports through it; ween32 has no notification path at all.
- **`WM_CHAR` and modifier state.** `TranslateMessage` is a no-op
  (`src/user.c:626`) and the X11 backend takes the unshifted keysym only
  (`src/x11.c:317`), so there is no typed text and no Shift+Tab.
- **Timers** — `SetTimer`/`KillTimer`/`WM_TIMER`: caret blink, scroll-bar
  auto-repeat, marquee progress.
- **Mouse routing into nested children**, plus hover tracking
  (`WM_MOUSELEAVE`) for the states 98.css shows on interactive rows. Today
  only direct children of the top-level window are hit-tested
  (`src/user.c:644`).
- **Drawing primitives still missing** — `DrawFocusRect`, `DrawState` as a
  public call, `WM_ERASEBKGND`, `WM_CTLCOLORSTATIC`/`WM_CTLCOLOREDIT`, and the
  inactive-caption colours (`COLOR_INACTIVECAPTION(TEXT)`,
  `COLOR_GRADIENTINACTIVECAPTION`) with `WM_NCACTIVATE` behind them.
- **Image lists and icons** — `ImageList_Create`/`Add`/`Draw`, `LoadImage`,
  `DrawIconEx` for tree and list-view items.
- **Keyboard conventions** — mnemonics (`&Text` + Alt), accelerators,
  `WM_NEXTDLGCTL`, arrow-key navigation inside radio groups and lists.

## Beyond the 98.css list

Not on that page, but a real application needs them: menus (`HMENU`,
`WM_INITMENU`, popup tracking), accelerator tables, toolbars, tooltips, modal
`DialogBox`/`MessageBoxA`, and resizable windows (`WM_SIZE`; `MoveWindow`
currently cannot resize the top-level surface, `src/user.c:427`).

## Known limits

Shortcuts the current code takes deliberately; each is a candidate task.

- **`SelectObject` returns the stock GUI font as the previous object**
  (`src/gdi.c:141`), so the usual save/restore idiom loses a bold selection.
- **`CreateFontA` honours only `weight`**; `height` and `face_name` are
  accepted and ignored.
- **`SetBkMode(OPAQUE)` is accepted but unimplemented** (`src/gdi.c:322`):
  text is always drawn transparent.
- **No subclassing** (`src/ween_internal.h:131`); no `SetWindowLongPtr`,
  `PostMessageA` or `GetDC`/`ReleaseDC`.
- **Fixed caps, silent when exceeded**: 32 window classes (`RegisterClassA`
  then returns 0), 128-byte window text, a 64-message queue, 64 tab stops per
  dialog, four columns in a list view.
- **The dpi is latched on first use** — the font strikes are static
  singletons, so `WEEN32_DPI` must be set before the first API call.
- **Text is measured differently from how it is drawn.** GDI reports character
  widths from the outline, rounded up, while the strike draws with its own
  advances; ween32 follows that, but not exactly — three of the sampler's
  strings come out a few pixels wider than Wine measures them, which is most of
  the difference that remains. Labels centred in a control, and the tab widths
  derived from them, move with it.
- **Marlett glyphs are straight-line only** (`src/marlett.c:129`): a curved
  glyph would need Bézier flattening. The scroll-bar arrows are straight-line,
  so they are reachable; check before assuming for others.

## Reference captures

The fidelity target is a real win32 render, not a memory of one.
`tools/refcapture/capture.sh` renders `examples/controls.c` — the same file the
ween32 example is built from — under Wine, against the real win32 controls:

```sh
tools/refcapture/capture.sh          # -> tools/refcapture/reference.png
make && WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=ours.bmp ./examples/controls
```

The script cross-compiles the sampler with `zig cc`, then runs it under Wine in
a prefix of its own where theming is off and the Windows 2000 colour scheme is
restored (`win2000.reg`) — Wine 9+ defaults to a light modern theme and palette,
which is emphatically not the target. It runs inside a Wine virtual desktop, so
Wine draws the caption and frame itself instead of handing the window to the
host window manager, and the finished window is read back with `import` and
cropped. Only Wine's own window is ever read.

To check the sampler against its reference, render it headless and diff:

```sh
make && WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=ours.bmp ./examples/controls
tools/refcapture/pxdiff.py                 # summary, or a region as ASCII maps
tools/refcapture/pxdiff.py 15 34 75 23     # one control, pixel by pixel
```

As of the last pass, 2435 of 296370 pixels differ — 0.8% — and every one of
them is in one of four places:

| Where | Pixels | Why |
| --- | --- | --- |
| `EDIT` text | 660 | glyph spacing: Wine steps its edit text by advances we reproduce to within a pixel per character |
| one tab | 651 | Wine measures one of the four tab strings three pixels narrower than we do, and the tabs after it shift |
| the close box | 509 | Wine antialiases the Marlett glyph; classic Windows drew it aliased, and so do we |
| a disabled label | ~400 | the same measuring difference, on a centred push-button label |

The rest — check boxes, option buttons, group box, list box, combo box, both
progress bars, the scroll bar, the tree view, the list view, the status bar and
the trackbars — is within a handful of pixels or exact.

## Testing

`make test` covers the engine pixels, the API path and the dialog manager, all
headless. Gaps worth closing:

- the tests pin `WEEN32_DPI=96`; only the `Xft.dpi` *parser* is covered, so
  the 120/144 strike snapping and the 192 pixel-doubling have no assertions;
- the headless screenshots are only checked for size in CI, not compared
  against the reference captures above.
