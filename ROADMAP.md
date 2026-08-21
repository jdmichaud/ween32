# Roadmap

ween32 is in progress. This is what the library does today, what it does not do
yet, and the shortcuts taken to get here.

The control inventory below is the component list from
[98.css](https://jdan.github.io/98.css/), which works through the same
*Microsoft Windows User Experience* guide the classic shell was built from. We
take that **list**, not that **look**: ween32 targets the Windows 2000 classic
theme, and every control is checked against a real win32 render (see
[Reference captures](#reference-captures) below), never against the 98 styling.

## Next

The five tasks that stood between the library and an application that is not
this one are done — a second top-level window, text and tables that grow
instead of truncating, `SelectObject`'s contract, timers, and the keyboard
conventions. What is left, in the order it is worth doing:

- [ ] **Menu keyboard access**: Alt opens the bar, the arrows walk between
  drop-downs, and a letter picks an item. Inside an open drop-down the arrows,
  Enter and Escape already work; getting *into* one from the keyboard does not.
- [ ] **Cascading submenus stay up**: a submenu opens beside its parent, but
  the parent closes behind it rather than remaining tracked.
- [ ] **Mouse routing into nested children**, plus hover tracking
  (`WM_MOUSELEAVE`) for the states 98.css shows on interactive rows. Today only
  direct children of the top-level window are hit-tested.
- [ ] **Auto-repeat in the views' own scroll bars.** The `SCROLLBAR` control
  repeats a held arrow; the list box, tree view and list view draw and handle
  their bars inline and do not.

Marquee progress was on this list and has been struck off rather than built:
`PBS_MARQUEE` arrived with comctl32 6.0, which is Windows XP. A Windows 2000
progress bar has no marquee mode, so adding one would be a period mistake, not
a missing feature.
- [ ] **The clipboard, and double-click to select a word.**
- [ ] **Image lists and icons** — `ImageList_Create`/`Add`/`Draw`,
  `LoadImage`, `DrawIconEx` for tree and list-view items.
- [ ] **Accelerator tables** and `WM_NEXTDLGCTL`, the two keyboard pieces not
  covered by IsDialogMessageA.

Arbitrary font sizes and faces are deliberately *not* on this list.
`CreateFontA` honours `weight` and picks between the regular and bold Tahoma
strikes, which is what classic dialogs actually used; anything else needs a
rasteriser, and no application here has asked for one yet.

## Implemented

**Windowing** — any number of top-level windows, each with its own surface and
native window; `RegisterClassA`, `CreateWindowExA`, `DestroyWindow`,
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

**Menus and dialogs** — `CreateMenu`/`CreatePopupMenu`/`AppendMenuA`/`SetMenu`
/`GetMenu`/`GetSubMenu`/`CheckMenuItem`/`EnableMenuItem`/`DestroyMenu`, a menu
bar drawn above the client area that opens its drop-downs on a click, and
`TrackPopupMenu` for a popup anywhere on screen — separators, grey items,
ticks, accelerator text and submenu arrows. Modal `DialogBoxIndirectParamA`
(the owner disabled while it is up) and `MessageBoxA`, sized to its message.

**Timers and the keyboard** — `SetTimer`/`KillTimer`/`WM_TIMER` (with the
`TIMERPROC` called from `DispatchMessage`), a caret that blinks on one, and
`IsDialogMessageA` carrying Tab/Shift+Tab, Space, the arrows within a group of
option buttons, `&`-mnemonics under Alt, Enter and Esc. Focused buttons draw
the dotted focus rectangle.

**GDI** — `BeginPaint`/`EndPaint`, `FillRect`, `FrameRect`, `DrawEdge` (Wine's
tables, every `BDR_`/`EDGE_` type and `BF_` flag), `DrawFrameControl`
(`DFC_CAPTION` and `DFC_BUTTON`), `TextOutA`, `DrawTextA`,
`GetTextExtentPoint32A`, `SetTextColor`, `SetBkMode`, `GetSysColor(Brush)`,
`CreateSolidBrush`, `CreateFontA`, `DeleteObject`,
`GetStockObject` (the GUI font and the stock brushes), `SelectObject`
(returning what was really selected).

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

### What they do

Every control responds to the mouse and keyboard, and reports through the
message its win32 counterpart uses:

- **EDIT** — click to place the caret, drag or Shift+arrows to select, type
  over a selection, backspace, delete, arrows and Home/End; `EN_CHANGE` to the
  parent, a caret when focused, and the selection on a highlight bar.
- **BUTTON** — press and release tracking, auto check boxes and radio groups,
  `BN_CLICKED`.
- **LISTBOX** — click or arrow keys to select, `LBN_SELCHANGE`; its scroll bar
  scrolls, by arrow, page, thumb or wheel, and the arrows keep the selection in
  view.
- **COMBOBOX** — press to drop the list; while the button is held the item
  under the pointer highlights and only the release picks it, which is how the
  control has always behaved. `CBN_SELCHANGE`. The list is painted over
  everything else and gets first refusal on the mouse, which is how it escapes
  its own client area without a second top-level window.
- **SCROLLBAR** — arrows scroll a line, the track a page, and the thumb drags;
  `WM_HSCROLL`/`WM_VSCROLL` with `SB_LINEUP`, `SB_PAGEUP`, `SB_THUMBTRACK`,
  `SB_THUMBPOSITION` and `SB_ENDSCROLL`.
- **Trackbar** — click or drag to a position, arrow keys to step.
- **Tabs** — click to switch, `TCN_SELCHANGE` through `WM_NOTIFY`.
- **TreeView** — click the button to expand or collapse, click an item to
  select it; `TVN_SELCHANGED` and `TVN_ITEMEXPANDED`. Both scroll bars work,
  and appear only when there is something to scroll — taking one strip can
  bring the other on, as in win32.
- **ListView** — click a row to select it, `LVN_ITEMCHANGED`. The selection is
  the label rect inflated five pixels with a dotted focus rectangle over it,
  which is pixel-identical to Wine's.
- **The wheel** goes to the focused window, as win32 sends it — a view scrolls
  once it has been clicked, and never selects.

`tests/input_test.c` drives all of this through the headless backend and
asserts where each control ends up, so CI covers it.

### What they still do not do

- [ ] **The clipboard, and double-click to select a word** — the edit selects
      by dragging and by Shift+arrows, but there is no cut, copy or paste, and
      no horizontal scroll when the text outruns the field: it is clipped.
- [ ] **Scrolling the list view** — its rows do not scroll yet; the list box
      and tree view do.
- [ ] **Auto-repeat** on a held scroll-bar arrow, and hot-tracking states.
- [ ] **A focus rectangle on the tree view's focused item** — buttons and the
      list view draw one; the tree view does not, and `DrawFocusRect` is still
      not a public call.
- [ ] **Multi-row tabs** (`TCS_MULTILINE`), item images from an image list,
      and column resizing in the list view.

## Core machinery these need

- **Timers** are done — `SetTimer`/`KillTimer`/`WM_TIMER`, with the caret
  blinking on one. Scroll-bar auto-repeat and marquee progress still want
  writing, but the machinery under them exists.
- **More than one top-level window** is done, each with its own surface and
  backend window. The combo box still paints its list over the parent; a list
  taller than the window is now fixable rather than blocked.
- **Ctrl shortcuts and shift-click selection.** Shift and Alt reach the app
  (Shift+Tab and mnemonics use them); Ctrl does not yet.
- **Mouse routing into nested children**, plus hover tracking
  (`WM_MOUSELEAVE`) for the states 98.css shows on interactive rows. Today
  only direct children of the top-level window are hit-tested
  (`src/user.c:644`).
- **Drawing primitives still missing** — `DrawFocusRect` and `DrawState` as
  public calls (a focused button draws its own rectangle, but the API is not
  exposed), `WM_ERASEBKGND`, `WM_CTLCOLORSTATIC`/`WM_CTLCOLOREDIT`, and the
  inactive-caption colours (`COLOR_INACTIVECAPTION(TEXT)`,
  `COLOR_GRADIENTINACTIVECAPTION`) with `WM_NCACTIVATE` behind them.
- **Image lists and icons** — `ImageList_Create`/`Add`/`Draw`, `LoadImage`,
  `DrawIconEx` for tree and list-view items.
- **Keyboard conventions** — mnemonics, Tab/Shift+Tab, Space and the arrows
  inside a group are done, through `IsDialogMessageA`. Accelerator tables,
  `WM_NEXTDLGCTL` and arrow navigation inside a list are not.

## Beyond the 98.css list

Not on that page, but a real application needs them: menus (`HMENU`,
`WM_INITMENU`, popup tracking), accelerator tables, toolbars, tooltips, and
modal `DialogBox`/`MessageBoxA`.

Resizing is done: `WS_THICKFRAME` gives a window a sizing border, its corners
and edges drag, the status bar's grip claims `HTBOTTOMRIGHT` as it does in
win32, `MoveWindow` really resizes, the surface follows and the app hears
`WM_SIZE`. A window without that style stays fixed — and tells the window
manager so, which is why one could not be resized before.

## Known limits

Shortcuts the current code takes deliberately; each is a candidate task.

- **`CreateFontA` honours only `weight`**; `height` and `face_name` are
  accepted and ignored.
- **`SetBkMode(OPAQUE)` is accepted but unimplemented** (`src/gdi.c:322`):
  text is always drawn transparent.
- **No subclassing** (`src/ween_internal.h:131`); no `SetWindowLongPtr`,
  `PostMessageA`, `PeekMessageA` or `GetDC`/`ReleaseDC`. Without `PeekMessage`
  a message loop can only be run once per process, since `WM_QUIT` is final.
- **Fixed caps still left**: four columns in a list view, and 64 buttons in one
  group of option buttons. Window text, the class table and the message queue
  grow; a window may have any number of tab stops.
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
tools/refcapture/capture.sh                        # -> reference.png
tools/refcapture/capture.sh menu.c menu-reference.png   # the other sampler
make && WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=ours.bmp ./examples/controls
```

The script cross-compiles the sampler with `zig cc`, then runs it under Wine in
a prefix of its own where theming is off and the Windows 2000 colour scheme is
restored (`win2000.reg`) — Wine 9+ defaults to a light modern theme and palette,
which is emphatically not the target. It runs inside a Wine virtual desktop, so
Wine draws the caption and frame itself instead of handing the window to the
host window manager, and the finished window is read back with `import` and
cropped. Only Wine's own window is ever read.

Cropping is `crop_window.py`, not a trim: a window manager that resizes Wine's
desktop window gives it chrome of its own, which reaches the edges of the shot
and leaves no uniform border to trim away. What is always true instead is that
the sampler's window floats, touching no edge, so everything connected to the
border is dropped and what remains is the window.

To check the sampler against its reference, render it headless and diff:

```sh
make && WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=ours.bmp ./examples/controls
tools/refcapture/pxdiff.py                 # summary, or a region as ASCII maps
tools/refcapture/pxdiff.py 15 34 75 23     # one control, pixel by pixel

PXDIFF_REF=tools/refcapture/menu-reference.png \
PXDIFF_OUR=/tmp/menu_ween.png tools/refcapture/pxdiff.py   # the menu sampler
```

As of the last pass, 2435 of 296370 pixels differ — 0.8% — and every one of
them is in one of four places:

| Where | Pixels | Why |
| --- | --- | --- |
| `EDIT` text | ~900 | deliberate: Wine spaces an edit's characters out to the width GDI *reports*, which is wider and reads as uneven, and shifts the text as you type. We lay it out on the strike's advances, like every other control, which is what Windows looked like |
| one tab | 651 | Wine measures one of the four tab strings three pixels narrower than we do, and the tabs after it shift |
| the close box | 509 | Wine antialiases the Marlett glyph; classic Windows drew it aliased, and so do we |
| a disabled label | ~400 | the same measuring difference, on a centred push-button label |

`examples/menu.c` is the second sampler, and its reference is a window with a
menu bar. 532 of its 39200 pixels differ — 1.4% — and nearly all of that is the
caption's bold title, which ween32 synthesises by overstriking the regular
strike where Wine has the real Tahoma Bold. That difference is not new; it
shows up here because this window's title has different letters in it. The menu
bar's own geometry lands on Wine's pixels, and where it did not, the reference
said so by a pixel and was followed.

The rest — check boxes, option buttons, group box, list box, combo box, both
progress bars, the scroll bar, the tree view, the list view, the status bar and
the trackbars — is within a handful of pixels or exact.

## Testing

`make test` covers the engine pixels, the API path, the dialog manager, input
and resizing, two windows at once, timers, the keyboard conventions, menus and
modal dialogs — 130 assertions, all headless, so CI runs the lot. Gaps worth
closing:

- the tests pin `WEEN32_DPI=96`; only the `Xft.dpi` *parser* is covered, so
  the 120/144 strike snapping and the 192 pixel-doubling have no assertions;
- the headless screenshots are only checked for size in CI, not compared
  against the reference captures above;
- a message box and a modal dialog have no reference capture of their own. The
  capture needs the window to take the input focus, and the display this was
  built on would not give it one; `WEEN32_BMP=/tmp/frame%d.bmp` is how their
  ween32 side gets looked at meanwhile.
