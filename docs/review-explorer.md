# Review: `examples/explorer.c` against ween32

Scope: read-only review of `examples/explorer.c` (2,636 lines), `examples/fs.h`,
`include/ween32.h`, and the parts of `src/` the app leans on (`user.c`,
`menu.c`, `controls.c`). Line numbers are as of commit `ce6f2a0`.

**What has been acted on since.** A2 — a rebar now passes `WM_COMMAND` and
`WM_NOTIFY` to its parent, with a test whose control is the rebar's own child;
every button in the window was dead and now works. A3 — `LVM_GETHEADER` hands
back a real header and `HDM_SETITEM` is how the sort arrow is asked for;
`LVCOLUMN.fmt` no longer carries `HDF_*`. A6 — `WEEN32_HAS_CURSORS` was never
defined, so the explorer had no class cursors at all; the flag is declared,
`-Wundef` is in `CFLAGS`, and all eleven `HAVE()` gates are gone from the app.
A5 — `InitCommonControlsEx`, `lstrcmpiA`/`lstrcmpA`/`lstrlenA`, and
`commctrl.h`/`windowsx.h` reached through `ween32.h` on Windows, which took two
`#ifdef` blocks out of the file. D3 — the splitter goes through
`ScreenToClient`, and `GetWindowRect` and `ClientToScreen` now measure from the
same corner (they did not, which is a library bug this found). D4, D5, D6, D7,
D9 — all fixed. D8 — Back and Forward walk a history.

A1 — the menu band is a toolbar of drop-down buttons on both sides now, the
application answers `TBN_DROPDOWN` with `TrackPopupMenu` and `SC_KEYMENU` by
handing the bar the keyboard, and `ween_menu_band_*`, the `SetMenu` side
channel and 175 lines of the application drawing a bar for ween32 to drive are
all gone. The only `#ifdef _WIN32` left in the file are a path separator and a
console to drop.

What is left is the rest of A3 (the header drawing its own band), A4 (the
pane's ✕ as a flat toolbar button), the rest of A5 (`TVIF_PARAM`,
`LVIF_PARAM`, `LVM_SORTITEMS`, `ImageList_LoadImageA`), A7, B1 and B2. Those
are on the ROADMAP's Next list.

## Summary

About half of `explorer.c` is the file browser. The other half is four things
that don't belong in an application written against a win32 library:

| What | Lines (≈) | Where it belongs |
|---|---|---|
| Pixel art for 13 toolbar glyphs, 4 Send To icons, My Computer, plus the palette→bitmap→icon converters | 730 (28%) | `assets/*.bmp`/`.ico`, loaded with `LoadImageA` + `ImageList_AddMasked` (or `ImageList_LoadImageA`) |
| A menu-bar engine written twice: one path against a ween32-private API, a stub for Windows | 175 | one path through win32-shaped API; the engine in ween32 |
| A pixel-comparison fixture (`WEEN32_EXPLORER_FIXTURE`) threaded through the UI code | 220 | behind the `fs.h` seam, or its own header |
| Hand-made windows standing in for rebar/toolbar features ween32 lacks (brand box, pane-header close glyph, layout that pokes at rebar insets) | 150 | ween32's rebar and toolbar |

Plus a set of "this win32 call doesn't exist in ween32, so the app re-does it"
spots, and three real defects found on the way: the class cursors are never
registered in the ween32 build (`HAVE(CURSORS)` is silently 0), nothing inside
the rebar can notify the frame in the ween32 build (toolbar buttons, Go,
address selection), and the sort arrow rides on a ween32-private meaning of
`LVCOLUMN.fmt`.

The dual-compile contract holds only in the narrow sense that CI cross-compiles
the file: it contains 5 `#ifndef _WIN32`, 4 `#ifdef _WIN32` and 11
`#if HAVE(...)` blocks, and the Windows side of those forks has, as far as the
repo shows, never been run.

---

## A. Code in explorer.c that is really ween32's job

### A1. The menu band — implemented twice, against a ween32-only API (most important)

`examples/explorer.c:1413-1586`, `:1864-1870`, `:2196-2200`.

- On ween32 the band calls `ween_menu_band_set/hot/open/track`
  (`include/ween32.h:1105-1111`) — a four-function API that exists for this
  one caller (`git log -S ween_menu_band_set` → one commit, `aab353c`). On
  Windows it falls back to `g_menu_open` + a per-item `TrackPopupMenu`
  (`explorer.c:1567-1578`) with no hover-switching between drop-downs and no
  Alt/F10 at all, because `SetMenu` is only called on ween32 (`:1864-1868`).
- To make Alt work, the ween32 build *does* call `SetMenu`, and the library
  then hides the frame's bar through a global side channel:
  `ween_menu_bar_height()` returns 0 when a band is registered
  (`src/user.c:262-268`). `SetMenu` no longer means what it means in win32 —
  it draws a bar or not depending on whether `ween_menu_band_set` was called
  somewhere else. The band state is also process-global
  (`src/menu.c:780-782`, "one band per process").
- The geometry handshake is backwards: the app measures its items inside
  `WM_PAINT` and pushes the rectangles to the library on every paint
  (`menubar_measure`, `:1455-1489`); a hit-test before the first paint sees
  zero items.
- `ween32.h`'s `_WIN32` branch is just `#include <windows.h>`, so any
  extension the library offers forces an `#ifndef _WIN32` fork into every app
  that uses it. That is a header design flaw on its own.

What the shape should be: on a real Windows 2000 the menu band *is* a
`ToolbarWindow32` in the rebar (browseui drives it). The app should create a
flat `TBSTYLE_LIST` toolbar with one drop-down button per top-level menu and
answer `TBN_DROPDOWN` with `TrackPopupMenu` — one code path that compiles
**and runs** on both sides. The behaviour that is genuinely beyond plain
comctl32 (sliding between drop-downs while one is open, Alt arming,
hidden-until-Alt underlines) can stay in ween32, but reached through standard
means the toolbar already has or should have (`TB_SETHOTITEM`,
`TBN_HOTITEMCHANGE`, `WM_SYSCOMMAND`/`SC_KEYMENU`, `WM_QUERYUISTATE`), with
ween32's tracker treating a toolbar of drop-down buttons as "the bar" — it
already knows the button rectangles, so `ween_menu_band_set` is unnecessary.
If a ween32-specific call is unavoidable, `ween32.h` must define it away on
`_WIN32` so the app source stays single-path.

### A2. The rebar drops everything its children say — and the app papers over what the rebar can't do

`src/controls.c:4280-4345` (`rebar_proc`): no `WM_COMMAND`/`WM_NOTIFY` case,
`default` → `DefWindowProcA` → `return 0` (`src/user.c:2260`). Every control
sends only to `wnd->parent` (`controls.c:4129,4136`). In the explorer the
toolbar's parent is `g_rebar` (`explorer.c:2072`), the Go toolbar's and the
combo's is `g_addrband` → `g_rebar` (`:2162-2170`, `address_proc` forwards to
`GetParent`, `:1631-1645`).

Consequence, by reading (not executed): in the ween32 build a toolbar button's
`WM_COMMAND`, `TBN_DROPDOWN`, Go, and the address combo's `CBN_SELCHANGE`
never reach `explorer_proc`. Folders still toggles from the menu and the pane's
✕ because those don't go through the rebar. `tests/toolbar_test.c:83-86,192-206`
doesn't catch it: its toolbar is created as the *frame's* child and merely
moved into the band. comctl32's rebar forwards `WM_COMMAND`/`WM_NOTIFY` to its
parent, which is why the same code is fine on Windows. To confirm:

```sh
WEEN32_HEADLESS=1 WEEN32_DPI=96 WEEN32_BMP=/tmp/f.bmp \
  WEEN32_SCRIPT="d:240,60 u:240,60" ./examples/explorer
```

— the tree pane should close and, by the code path above, will not.

Other rebar gaps the app compensates for in its own body:

- **The brand box** (`brand_proc`, `:1595-1619`) is a stray child of the rebar
  outside any band, positioned by the *app* at `rr.right - 2 - BRAND_W`
  (`:532`, the rebar's own 2-px edge), and it paints a piece of the rebar's
  separator rule itself (`:1614`). On the machine it is a second band on the
  menu row with a fixed width — exactly the "bands side by side" gap
  ROADMAP.md lists. The hack belongs in the roadmap as a rebar feature, not in
  the app.
- `RBBIM_SIZE`/`cx` is declared but ignored by `RB_INSERTBANDA`;
  `RBBS_FIXEDSIZE`/`RBBS_BREAK` don't exist.
- The address container (`address_proc`) is legitimate win32 practice (the
  real address band is a window holding a ComboBoxEx and a Go toolbar) — but
  on ween32 it's dead code because of the forwarding gap above.

### A3. The sort arrow: a ween32-private convention on a win32 message

`mark_sorted_column` (`explorer.c:812-824`) puts `HDF_SORTUP`/`HDF_SORTDOWN`
into `LVCOLUMN.fmt` via `LVM_SETCOLUMNA`; ween32's list view masks exactly
those bits in (`src/controls.c:2990-3006`, `:3096-3108`) and draws the
triangle (`:2556-2561`). On Windows those are header-item bits, set through
`LVM_GETHEADER` + `HDM_SETITEM`; they are not defined values of `LVCFMT_*`, so
the same source draws no arrow there. ROADMAP.md explicitly said it would not
"fake it with a message win32 does not have" — and then `2fefa87` did. The
machine does draw the arrow (`shot-machine-window.png` shows it), so the
feature is right; the mechanism needs a header control (`LVM_GETHEADER`,
`HDM_SETITEMA`, `HDI_FORMAT`) — which would also retire the "four columns"
cap and the header hit-test trick in `WM_CONTEXTMENU` (`:2375`).

### A4. The pane header's close box is a flat toolbar button drawn by hand

`panehead_proc` (`:1648-1716`): the ✕ is seven `FillRect` pairs, the frame
appears only when `GetFocus() == w` (`:1675`), and the comment describes the
real behaviour — "a bare glyph… a frame once the pointer is over it… inflated
six across and five down, raised by one" — which is precisely a flat
`ToolbarWindow32` button's hot state, something ween32 already renders
pixel-exactly for the Back button. The hand-made version implements half of it
(no hover at all: no `WM_MOUSEMOVE`/`TrackMouseEvent`) and re-implements
keyboard activation (`:1706-1710`). It should be a one-button flat toolbar
beside a static label.

### A5. win32 calls ween32 lacks, so the app reinvents them

| Missing in `ween32.h` | What the app does instead |
|---|---|
| `TVIF_PARAM` (the `lParam` field exists, no flag) | `path_of_item` (`:991-1022`) rebuilds a path by reading item *text* up the parent chain |
| `LVIF_PARAM`, `LVM_GETITEMA`/`LVM_GETITEMTEXTA` | a parallel `g_entry[]` array indexed by row, and a second one for the fixture (`WM_CONTEXTMENU`, `:2380-2388`) |
| `LVM_SORTITEMS` | `qsort` + delete-all + refill (`fill_list`, `:826-854`) |
| `lstrcmpiA`/`CompareStringA` | `name_cmp` (`:608-619`) |
| `InitCommonControlsEx`, `INITCOMMONCONTROLSEX`, `ICC_*` | `#ifdef _WIN32` block in `main` (`:2553-2567`) — a no-op stub in ween32 removes the fork |
| `GetCursorPos` | splitter derives pointer position from `GetWindowRect` arithmetic (`:1729-1730`, see D3) |
| `ImageList_LoadImageA`, `TB_ADDBITMAP` | the whole art pipeline in B1 |
| `TB_SETHOTITEM`, `TBN_HOTITEMCHANGE`, `SC_KEYMENU` handling | the private band API in A1 |
| `<commctrl.h>`/`<windowsx.h>` on the `_WIN32` side of `ween32.h` | `#ifdef _WIN32` includes at `:15-17` |

### A6. The `HAVE()` feature gates are dead scaffolding — and one is a live bug

`HAVE(MENU|IMAGELIST|TOOLBAR|MESSAGEBOX|CURSORS)` at `:1755, 1941, 2059, 2187,
2269, 2294-2301, 2516, 2580, 2613`. Every `WEEN32_HAS_*` flag is 1 now, so
these only make the file harder to read — except `WEEN32_HAS_CURSORS`, which
**has never been defined** (`include/ween32.h` has no such line;
`git log -S WEEN32_HAS_CURSORS` finds only the explorer's first commit).
`cc -std=c99 -Iinclude -Wundef -fsyntax-only examples/explorer.c` confirms
both `#if HAVE(CURSORS)` evaluate to 0, so in the ween32 build `wc.hCursor` is
never set, the frame has no arrow class cursor and the splitter never shows
`IDC_SIZEWE` (`src/user.c:237` falls back to the arrow). The feature ROADMAP.md
credits to the explorer ("the shapes a splitter needs") is switched off in the
app that needs it. The gates belong in `controls.c` (the sampler), not here;
`-Wundef` in `CFLAGS` would have caught this.

### A7. Pixel constants the controls own, hard-coded in the app

`STATUS_H 20` (`:488`) while the status bar sizes itself from the font on
`WM_SIZE` (`src/controls.c`, `status_proc`), `PANE_HEAD_H 20`, `BRAND_H 23`,
`SPLIT_W 4`, the status parts at `cr.right - 233/-155` (`:577`),
`g_split_x = 203`. The design doc's DPI story ("apps scale with zero code
changes") is false for this app: at 120/144 dpi the status bar and the layout
disagree. The win32 idiom is to send the status bar `WM_SIZE` and read its
rect back; `rebar_height()` already does it right for the rebar.

---

## B. Content that isn't application code at all

### B1. ≈730 lines of pixel art and converters

`:85-450` (glyph struct, 13 ASCII bitmaps with palettes, `GLYPHS`/`GLYPHS_HOT`),
`:1071-1233` (Send To ×4, My Computer), `:452-463` `glyph_colour`,
`:1236-1285` `glyph_icon`/`menu_bitmap`, `:1944-2058`
`add_glyph`/`add_blank`/`build_images`/`load_icons`. The repo already ships
111 `.ico` files in `assets/icons` and `LoadImageA` reads `.bmp`; the toolbar
strip, hot strip, Send To icons and the My Computer icon should be files there
too, loaded the same way. `add_blank`'s index-hole bookkeeping disappears with
a strip. If the ASCII form is valuable for review, keep the encoder as a
`tools/` script that generates the `.bmp`.

### B2. The fixture (≈220 lines) is test scaffolding inside the UI

`g_fixture` (`:704`) is tested in `status_for_directory`,
`status_for_selection`, `fill_fixture_list`/`fill_fixture_tree`,
`show_directory` (`:892-911`), `build_images` (the shell icon list with a
`NULL` hole for the hand-painted My Computer, `:2018-2042`), `WM_CONTEXTMENU`,
`WM_CREATE`, `TVN_ITEMEXPANDING`. It carries literal machine output
("6 object(s) (Disk free space: 499 MB)", "203 bytes",
`SetWindowTextA(g_main, "Local Disk (C:)")`), and the My Computer glyph is
hand-quantised to the VM's 16-bit display (`:1184-1190` comment). `fs.h`
already is the seam for "where the entries come from": a namespace provider
(root items, children, listing, status line) with a file-system implementation
and a fixture implementation, chosen once in `main`, removes every
`if (g_fixture)` from the UI. Even outside the fixture, `show_directory`
writes "My Computer" into the status bar on a Linux file system (`:936`).

### B3. The six menus, item for item (≈250 lines)

`build_menu` (`:1756-1880`), context menus (`:1286-1412`). Legitimate app
content for this example, but ~80% of the items are inert (`id 0`) and
`IDM_CTX_REFRESH` is appended and never handled. A table-driven builder would
be a third of the size; lower priority than everything above.

---

## C. Where ween32 has been shaped around this one app (the reverse leak)

- `ween_menu_band_*` as public API, process-global state, and
  `SetMenu`/`ween_menu_bar_height` changing meaning on a side channel (A1).
- `HDF_SORTUP`/`HDF_SORTDOWN` accepted inside `LVCOLUMN.fmt` (A3).
- No `_WIN32`-side neutralisation of extensions in `ween32.h` (A1).
- `tests/toolbar_test.c` tests a toolbar-in-rebar arrangement the explorer
  doesn't use (A2).

---

## D. Defects noticed while reading (beyond A2/A6)

- **D3** `splitter_proc` (`:1729-1730`): `GetWindowRect(g_main)` is the
  *outer* rect (`src/user.c:298-318`), so `g_split_x` is offset by the sizing
  frame; the splitter jumps 4 px on the first drag. `ScreenToClient(g_main, …)`
  is the idiom.
- **D4** `menubar_proc` uses `ps.rcPaint` as the client rect (`:1505`); a
  partial repaint (routine on Windows when a drop-down closes) misplaces the
  labels.
- **D5** `WM_NOTIFY` never checks `hwndFrom`: `NM_DBLCLK` from the tree runs
  the list's open code (`:2445`).
- **D6** `IDM_UP` edits `g_path` in place, then `show_directory(g_path)` does
  `strncpy(g_path, path…)` with `path == g_path` (`:938`) — overlapping,
  undefined.
- **D7** Context menu over the header of an *empty* list gives the background
  menu (`LVM_GETITEMRECT` on item 0 fails, `:2375`).
- **D8** No handler for `IDM_BACK/FORWARD/HISTORY/SEARCH/VIEWS/GO/DELETE/UNDO/
  MOVETO/COPYTO`, `TBN_DROPDOWN`, or the address combo — Back/Forward have no
  history model; the drop-down arrows do nothing even once A2 is fixed.
- **D9** `g_mb_x[12]` vs `items[16]` caps in the same function (`:1454, 1478`).

---

## E. Suggested order of work

1. **Rebar forwards `WM_COMMAND`/`WM_NOTIFY`** to its parent, with a test whose
   toolbar is created as the rebar's child. Unblocks every button in the
   window.
2. **Header control** (`LVM_GETHEADER`, `HDM_SETITEMA`/`HDI_FORMAT`,
   `HDF_SORTUP`); remove the `fmt` smuggling; lift the 4-column cap.
3. **Menu band as a toolbar** on both sides (`TBN_DROPDOWN` →
   `TrackPopupMenu`), ween32's tracker recognising a drop-down toolbar as a
   bar; `TB_SETHOTITEM`/`TBN_HOTITEMCHANGE`/`SC_KEYMENU`; delete
   `ween_menu_band_*` or hide it behind `ween32.h` on `_WIN32`.
4. **Rebar bands with a fixed width on one row** (`RBBIM_SIZE`,
   `RBBS_FIXEDSIZE`) so the brand is a band and `layout()` stops reaching into
   the rebar.
5. Pane-header close = one-button flat toolbar.
6. Add `TVIF_PARAM`, `LVIF_PARAM`, `LVM_SORTITEMS`, `LVM_GETITEMA`,
   `lstrcmpiA`, `InitCommonControlsEx`, `GetCursorPos`,
   `ImageList_LoadImageA`; include `commctrl.h`/`windowsx.h` from `ween32.h`
   on Windows; define `WEEN32_HAS_CURSORS` or drop the gates and add
   `-Wundef`.
7. Art to `assets/`, fixture behind the `fs.h` seam.

Done in that order, `explorer.c` should come out around 900–1,000 lines with
no `#if` in it — which is the property the README is promising.
