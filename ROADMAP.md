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
`NCPAINT`, and the built-in `BUTTON` (`BS_PUSHBUTTON`, `BS_DEFPUSHBUTTON`,
`BS_OWNERDRAW`) and `STATIC` classes.

**GDI** — `BeginPaint`/`EndPaint`, `FillRect`, `FrameRect`, `DrawEdge`,
`DrawFrameControl` (`DFC_CAPTION`), `TextOutA`, `DrawTextA`,
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

Grouped by what each needs from the core, because that dictates the order.
Every entry names the real win32 class, styles and messages — the API surface
is not ours to invent.

`examples/controls.c` is the acceptance test for this whole list. It is one
sampler holding every control below, compiled two ways: against real
`<windows.h>` it renders the genuine controls (that is the reference capture),
and against ween32 it renders the ones we have. A control **lands** when its
block draws the same pixels in both. Mechanically that means: implement the
class, then add `#define WEEN32_HAS_<NAME> 1` to `include/ween32.h` — the
sampler tests those defines with `#if HAVE(<NAME>)`, so the block lights up on
the ween32 side and the diff against the reference shrinks. Today the sampler
renders the caption, the push buttons and the labels; everything else is a
hole shaped like this list.

### Stage 1 — drawing only, no new core machinery

- [ ] **Checkbox** — `BUTTON` + `BS_CHECKBOX`/`BS_AUTOCHECKBOX` (and
      `BS_3STATE`/`BS_AUTO3STATE`); `BM_SETCHECK`/`BM_GETCHECK`,
      `BST_CHECKED`/`BST_UNCHECKED`/`BST_INDETERMINATE`, `CheckDlgButton`/
      `IsDlgButtonChecked`. A 13×13 sunken field-bordered box with the check
      glyph, label to its right, whole label clickable, Space toggles.
- [ ] **OptionButton (radio)** — `BUTTON` + `BS_RADIOBUTTON`/
      `BS_AUTORADIOBUTTON`; group semantics via `WS_GROUP`, `CheckRadioButton`,
      arrow keys move the selection within a group. Round sunken indicator with
      a centre dot.
- [ ] **GroupBox** — `BUTTON` + `BS_GROUPBOX`: an etched frame
      (`EDGE_ETCHED` = `BDR_SUNKENOUTER | BDR_RAISEDINNER`) with the label
      punched out of the top-left of the frame line.
- [ ] **Button states** — disabled (`EnableWindow`/`WS_DISABLED`/`WM_ENABLE`,
      the embossed grey caption, `COLOR_GRAYTEXT`) and the dotted focus
      rectangle (`DrawFocusRect`). Pushed and default already work.
- [ ] **Field borders** — the two classic border styles as first-class
      surfaces: the *field border* (sunken outer + sunken inner, white
      interior; face interior when read-only or disabled) =
      `WS_EX_CLIENTEDGE`, and the *status field border* (sunken outer only) =
      `WS_EX_STATICEDGE`. Needs `DrawEdge` to accept partial `BDR_*`/`BF_*`
      combinations, which it currently rejects.
- [ ] **Progress indicator** — `msctls_progress32`; `PBM_SETRANGE`,
      `PBM_SETPOS`, `PBM_SETSTEP`, `PBM_STEPIT`. Segmented is the classic
      default; `PBS_SMOOTH` is the solid bar.
- [ ] **Status bar** — `msctls_statusbar32`; `SB_SETPARTS`, `SB_SETTEXT`,
      `SBARS_SIZEGRIP`. Each part is a status-field border.
- [ ] **Title bar, complete** — minimise/maximise/restore/help buttons
      (`WS_MINIMIZEBOX`, `WS_MAXIMIZEBOX`, `WS_EX_CONTEXTHELP`, and
      `DFCS_CAPTIONMIN`/`MAX`/`RESTORE`/`HELP`, all of which
      `DrawFrameControl` already renders), plus the **inactive** caption:
      `WM_NCACTIVATE`, `COLOR_INACTIVECAPTION`,
      `COLOR_GRADIENTINACTIVECAPTION`, `COLOR_INACTIVECAPTIONTEXT`. The window
      icon and its system menu belong here too.

### Stage 2 — needs scrolling, text input and notifications

- [ ] **Scroll bars** — the `SCROLLBAR` class plus non-client `WS_VSCROLL`/
      `WS_HSCROLL` bars; `SetScrollInfo`/`GetScrollInfo`/`SetScrollPos`,
      `WM_VSCROLL`/`WM_HSCROLL` with `SB_LINEUP`/`SB_PAGEUP`/`SB_THUMBTRACK`/
      `SB_THUMBPOSITION`, proportional thumb, auto-repeat on the arrows. Every
      control below scrolls, so this comes first.
- [ ] **TextBox** — `EDIT`; `ES_LEFT`/`CENTER`/`RIGHT`, `ES_MULTILINE`,
      `ES_AUTOHSCROLL`/`ES_AUTOVSCROLL`, `ES_READONLY`, `ES_PASSWORD`;
      `EM_SETSEL`/`EM_GETSEL`/`EM_REPLACESEL`/`EM_LINEFROMCHAR`, `EN_CHANGE`/
      `EN_UPDATE`. Needs a blinking caret (`CreateCaret`/`ShowCaret`/
      `SetCaretPos` + timers), selection painting in `COLOR_HIGHLIGHT`, and
      clipboard (`WM_CUT`/`WM_COPY`/`WM_PASTE`).
- [ ] **ListBox** — `LISTBOX`; `LBS_NOTIFY`, `LBS_SORT`, `LB_ADDSTRING`/
      `LB_INSERTSTRING`/`LB_SETCURSEL`/`LB_GETCURSEL`/`LB_GETTEXT`,
      `LBN_SELCHANGE`/`LBN_DBLCLK`, selection bar, keyboard navigation.
- [ ] **Slider** — `msctls_trackbar32`; `TBS_HORZ`/`TBS_VERT`, `TBS_AUTOTICKS`,
      the box indicator (`TBS_BOTH`/`TBS_NOTICKS`) versus the pointer thumb;
      `TBM_SETRANGE`/`TBM_SETPOS`/`TBM_GETPOS`/`TBM_SETTICFREQ`, drag and
      keyboard, notifying through `WM_HSCROLL`/`WM_VSCROLL`.
- [ ] **Tabs** — `SysTabControl32`; `TCM_INSERTITEM`, `TCM_SETCURSEL`,
      `TCM_ADJUSTRECT`, `TCN_SELCHANGE`, and `TCS_MULTILINE` for the multi-row
      variant. The selected tab is drawn taller and overlaps its neighbours;
      the body is a raised page frame joined to the tab row.
- [ ] **TreeView** — `SysTreeView32`; `TVS_HASLINES`/`TVS_HASBUTTONS`/
      `TVS_LINESATROOT`/`TVS_SHOWSELALWAYS`, `TVM_INSERTITEM`/`TVM_EXPAND`/
      `TVM_SELECTITEM`/`TVM_GETNEXTITEM`, `TVN_SELCHANGED`/`TVN_ITEMEXPANDING`.
      Dotted connector lines, ⊞/⊟ buttons, indentation, per-item icons from an
      image list, scrolling.
- [ ] **TableView** — `SysListView32` in `LVS_REPORT` mode over a
      `SysHeader32` header; `LVM_INSERTCOLUMN`/`LVM_INSERTITEM`/
      `LVM_SETITEMTEXT`/`LVM_SETITEMSTATE`, `LVN_ITEMCHANGED`, `LVS_SINGLESEL`,
      row highlight in `COLOR_HIGHLIGHT`/`COLOR_HIGHLIGHTTEXT`, draggable
      column dividers, both scroll bars.

### Stage 3 — needs a second top-level window

- [ ] **Dropdown** — `COMBOBOX` with `CBS_DROPDOWNLIST` (and `CBS_DROPDOWN`
      for the editable form); `CB_ADDSTRING`/`CB_SETCURSEL`/`CB_GETCURSEL`/
      `CB_GETLBTEXT`, `CBN_SELCHANGE`/`CBN_DROPDOWN`. The closed control is a
      field border plus a raised drop arrow; the open list is a *popup window*
      that escapes the parent's client area, which the current single-window
      model cannot express.

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
- **Drawing primitives** — `DrawEdge` with partial `BDR_*`/`BF_*` flags and
  `EDGE_ETCHED` (`src/gdi.c:201` rejects everything but `BF_RECT`),
  `DrawFocusRect`, `DrawState` (the embossed disabled label), `WM_ENABLE`,
  `WM_ERASEBKGND`, `WM_CTLCOLORSTATIC`/`WM_CTLCOLOREDIT`.
- **More system colours** — `COLOR_HIGHLIGHT`, `COLOR_HIGHLIGHTTEXT`,
  `COLOR_GRAYTEXT`, `COLOR_SCROLLBAR`, `COLOR_INACTIVECAPTION(TEXT)`,
  `COLOR_GRADIENTINACTIVECAPTION`.
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
- **Fixed caps, silent when exceeded**: 16 window classes (`RegisterClassA`
  then returns 0), 128-byte window text, a 64-message queue, 64 tab stops per
  dialog (`src/user.c:458`).
- **The dpi is latched on first use** — the font strikes are static
  singletons, so `WEEN32_DPI` must be set before the first API call.
- **The non-client metrics are ~1px off real win32.** Measured with the
  harness below: for `WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU` at 96 dpi, a
  660x420 client comes out 666x446 here against 666x445 under Wine, and the
  caption gradient band is 20px against 18px. Widths agree exactly. The
  reference still runs with Wine's own caption font rather than Tahoma Bold
  8pt, so pin `CaptionFont` in `win2000.reg` before treating the remainder as
  a ween32 bug.
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

To check a control against its reference, render the same widget through the
headless backend and diff the two crops:

```sh
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=ours.bmp WEEN32_SCRIPT="..." ./your_app
magick compare -metric AE ours.bmp theirs.png diff.png   # 0 == pixel identical
```

## Testing

`make test` covers the engine pixels, the API path and the dialog manager, all
headless. Gaps worth closing:

- the tests pin `WEEN32_DPI=96`; only the `Xft.dpi` *parser* is covered, so
  the 120/144 strike snapping and the 192 pixel-doubling have no assertions;
- the headless screenshots are only checked for size in CI, not compared
  against the reference captures above.
