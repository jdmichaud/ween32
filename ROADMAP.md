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

Everything that was on this list has been built. What is left is the tail of
each piece — the part that needs machinery the library does not have yet, or
that no application has asked for:

- [ ] **The clipboard between applications.** Cut, copy and paste work within
  one; sharing with other X clients needs selection ownership and the round
  trip that goes with it.

- [ ] **Does the list box page by a screenful or one less?** The edit and the
  tree view page by a screenful *less one row* and the list view by a whole
  one — all three measured on the machine, and written up in
  [docs/testing.md](docs/testing.md#which-controls-page-by-a-whole-screenful-and-which-by-one-less)
  with the method, which matters because two plausible ways of counting are
  both off by one. The list box and the combo box's dropped list are the two
  nobody has measured; they page by a whole screenful today because that is
  what the shared helper does, not because anyone checked.

- [ ] **Auto-repeat in the views' own scroll bars.** The `SCROLLBAR` control
  repeats a held arrow; the list box, tree view, list view and edit draw and
  handle their bars inline and do not.

- [ ] **Horizontal scrolling in an edit**, so text that outruns the field
  scrolls rather than being clipped. The vertical half is there — the view
  follows the caret and the bar drives it — and what is missing is the
  sideways offset that would go with it, `WS_HSCROLL`, and `EM_LINESCROLL`'s
  first argument, which is taken and ignored today.


- [ ] **Where a property sheet puts its page.** The machine's puts it at
  (13, 51) from the sheet's window origin and makes it 360x374; ween32's is
  at (10, 50) and 365x377, and its tab control is at (8, 29) where the
  machine's is at (9, 29) -- both sheets being 386x468 with a 380x443
  client. Every page in `examples/explorer` carries the difference in its
  own table of pixels, which is why the pages match while the structure does
  not. `TCM_ADJUSTRECT` and `PS_TAB_X` are where it lives; fixing it means
  moving all four tables back the same day. Measured with
  `tools/vm/probe.c`, in docs/testing.md.

- [ ] **The strike's diagonals, and the caption's bold `F`.** Find and
  Replace are within **83 pixels of 45,360** and **57 of 62,478** of the
  machine's own boxes, and every one of those pixels is a letter: our `M`, `w`, `N`, `A` and `R` step a diagonal
  on a different row from the machine's bitmap face, and the caption's bold
  `F` is one column wider, which shifts the whole title. Both are the fonts
  rather than the drawing -- the last thing between these two dialogs and
  zero, and every other dialog in the library carries the same letters.

- [ ] **Printing.** `PrintDlgA`, `PageSetupDlgA` and `StartDoc`/`StartPage`/
  `EndPage`/`EndDoc` link and fail honestly. What is missing is a device
  context whose pixels go somewhere other than a window -- PostScript or PDF
  is the shape of it -- and something to send the result to. A program draws
  a page with the same GDI calls it draws a window with, so the drawing half
  is already here.

- [ ] **Files dropped on a window.** `DragAcceptFiles` remembers that a
  window would take them and `DragQueryFileA` answers with none, because the
  drag protocol belongs to the desktop -- XDND on X11 -- and the backend does
  not speak it. A gap in the backend rather than in any program: the same
  source dropped on by Windows works there.

- [ ] **One source list, not two.** The Makefile and build.zig each name the
  library's .c files, and the pair went stale within an hour of the second
  one being added -- a build that links a library missing five objects fails
  at the last step with undefined symbols rather than at the first with a
  name. A check that compares the two lists is a few lines and would have
  said so at once.

- [ ] **Multi-row tabs** (`TCS_MULTILINE`) and tab images.

- [ ] **GDI's ellipse, exactly.** ween32's is the mathematically inscribed
  one. GDI's is the boundary of a filled *region* whose spans are tighter
  than that at some rows — measured on the machine, and written down in
  [docs/paint.md](docs/paint.md), which is where a next attempt should
  start. A wide line at a shallow angle differs at its ends for the same
  kind of reason: GDI sweeps the pen as a region rather than stamping it
  along a walk.

- [ ] **A handful of calls no application here has asked for yet**:
  `PeekMessageA` (so a message loop could run more than once in a process),
  `DrawState`, `WM_ERASEBKGND`, `WM_CTLCOLORSTATIC`/`WM_CTLCOLOREDIT`, and
  `WM_NCACTIVATE` — a window already draws the inactive caption when the
  keyboard leaves it, but it does not tell the application.

- [ ] **The other two halves of `WM_PARENTNOTIFY`**. A parent is told when a
  child of its is destroyed, because a control that holds a child by handle —
  a rebar's band is the first, and will not be the last — is otherwise left
  pointing at freed memory. win32 sends the same message when a child is
  *created* and when one is *pressed*; neither is here, because nothing has
  needed them. `WS_EX_NOPARENTNOTIFY` is honoured for the half that exists.

- [ ] **Fields that are declared, stored and not acted on.** The offset gate
  keeps every struct an application fills in the shape win32 gives it, which
  means some fields exist to be named rather than to be read: `LVITEMA`'s
  `iIndent` and its three group fields (ween32 has no list-view groups),
  `HDITEMA`'s `type` and `pvFilter` (no filter headers), `OPENFILENAMEA`'s
  three reserved fields, `NMTOOLBAR`'s `tbButton`, `cchText`, `pszText` and
  `rcButton` (a drop-down notification fills in the item alone), and
  `PROPSHEETHEADERA`'s watermark and header bitmaps. A field that cannot be
  named is intolerable; one that is named, kept and listed here is unfinished.

- [ ] **Four fields of `REBARBANDINFOA` that are taken and not read.** The
  struct is win32's shape to the end of the classic definition, and every
  field is stored and handed back by `RB_GETBANDINFOA`, but `cyChild`,
  `cyMaxChild`, `cyIntegral` and `hbmBack` are only kept: a band's height is
  still `cyMinChild`, and a band has no background bitmap. A field that cannot
  be named is intolerable; one that is named, kept and listed here is merely
  unfinished.

- [ ] **ween32's list box pages by one row too many.** Measured on the machine,
  a list box moves a screenful *less one* on a track click — 6 of 7 whole rows,
  twice, with the client an exact multiple of the row pitch so there is no
  partial-row ambiguity. ween32's moves a whole screenful: `sb_click` is given
  `page = visible`, and a probe confirms it, top 0 to 8 with 8 rows showing.
  The combo's dropped list wants the same reading taken of *our* side before
  either is changed — its keyboard PageDown is a whole screenful by
  construction, and a track click is a different path. Changing this moves any
  capture with a scrolled list in it, so it wants the before-and-after numbers
  rather than a quiet fix. Measurements in [docs/testing.md](docs/testing.md).

- [ ] **A popup drawn on somebody else's pixels.** The chevron's menu draws a
  clipped, greyed `Ct` at the right edge of one row. It is not accelerator
  text of the menu's own — those items carry no tab — and it is not the Edit
  menu's `&Undo\tCtrl+Z`: changing that string does not move it. It looks
  like a popup window being handed a surface with something else's pixels
  still on it, which would be the library's rather than that menu's. Whoever
  takes it starts from those two things being ruled out.

- [x] **What comes out of a chevron.** Done: the band draws one when it is
  narrower than `cxIdeal` and reserves the room for it, sends
  `RBN_CHEVRONPUSHED` with the rectangle, and the explorer answers with the
  buttons that do not fit. Measured off the machine; docs/testing.md has the
  glyph, the placement and what the popup contains.

- [ ] **The chevron's staircase.** A band that can hide what does not fit has
  one floor here — its handle, its name and the chevron — where the machine
  has steps, because the machine's toolbar sheds buttons into the chevron a
  button at a time and the band follows it down. Measured in
  [docs/testing.md](docs/testing.md); it needs the toolbar to learn to hide
  clipped buttons (`TBSTYLE_EX_HIDECLIPPEDBUTTONS`) before the band can step.

- [ ] **A fixed-width band beside one that stretches** — `RBBIM_SIZE`,
  `RBBS_FIXEDSIZE`. Bands share a row, but the last band on a row takes
  whatever width is left, unconditionally, so a fixed width at the *right end*
  of a row cannot be said at all — which is exactly where the explorer's brand
  box sits, and why the application places that one itself instead of giving
  it to a band.

### What the explorer still writes for itself

`examples/explorer.c` was read once against the question "would an application
written to win32 have had to write this?", and what it turned up is mostly
built: the menu band is a toolbar of drop-down buttons rather than a private
side channel into ween32's menu engine, the rebar passes its children's
messages on, the sort arrow goes through `LVM_GETHEADER` and `HDM_SETITEM`,
and the class cursors the app had switched off with a gate that named nothing
are back. What is still open:

- [ ] **The header draws its own band.** `LVM_GETHEADER` hands back a real
  header and `HDM_SETITEM`/`HDM_GETITEM` answer against the list's columns,
  which is how the sort arrow is asked for — but the list still paints the
  headings, so the header is a place and a store rather than a control. Doing
  it properly also lifts the four-column cap and gives `HDM_LAYOUT` and
  `HDM_HITTEST` something to answer with.
- [ ] **A folder picker**, so Edit > Copy To Folder and Move To Folder can ask
  where rather than leaving the paste to the folder you go to.
- [x] **Property sheets** — `PROPSHEETHEADER` and `PropertySheetA`, in
  `src/propsheet.c`. Folder Options is a sheet of four pages and Properties
  one of a single page; Choose Columns is a plain dialog, as the machine's
  is. The sheet is as big as its largest page, is written in the same face as
  its pages, and leaves the keyboard where a page put it.
- [ ] **A flat toolbar button that hot-tracks and takes the keyboard**, so the
  ✕ on the explorer's Folders bar can be one instead of seven `FillRect` pairs
  with a frame drawn by hand. (A toolbar takes the keyboard now — the menu band
  is one — but a lone button still wants the focus rectangle and the hover.)
- [ ] **The rest of the calls the explorer works around**: `TVIF_PARAM` and
  `LVIF_PARAM` with `LVM_GETITEMA` (so an item can carry what it stands for
  rather than having its path rebuilt from its text), `LVM_SORTITEMS`,
  `ImageList_LoadImageA` and `TB_ADDBITMAP` (so toolbar art is a bitmap in
  `assets/` rather than 730 lines of ASCII in the application), `GetCursorPos`.
- [ ] **`explorer.c` with no `#if` in it.** Once the above are in, the feature
  gates go, the pixel art moves to `assets/`, and the fixture that fills both
  panes with the machine's content moves behind the `fs.h` seam as a second
  namespace provider — which is where "where do the entries come from" already
  lives.

One thing is deliberately *not* on this list. Marquee progress: `PBS_MARQUEE`
arrived with comctl32 6.0, which is Windows XP. A Windows 2000 progress bar
has no marquee mode, so building one would be a period mistake rather than a
missing feature.

## Implemented

**Windowing** — any number of top-level windows, each with its own surface and
native window; `RegisterClassA`, `CreateWindowExA`, `DestroyWindow`,
`ShowWindow`, `MoveWindow`, `SetWindowPos` (with `SWP_NOMOVE`/`SWP_NOSIZE`,
which is how a window resizes itself without having to ask where it is
first), `GetClientRect`, `AdjustWindowRect(Ex)`,
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
the dotted focus rectangle. Accelerator tables
(`CreateAcceleratorTableA`/`TranslateAcceleratorA`) and `WM_NEXTDLGCTL`.

**The clipboard** — `OpenClipboard`/`EmptyClipboard`/`SetClipboardData`/
`GetClipboardData`/`CloseClipboard` over `CF_TEXT`, with an EDIT taking
Ctrl+X/C/V/A and `WM_CUT`/`WM_COPY`/`WM_PASTE`, and a double click selecting
the word under it. Within one process: sharing with other X clients still
needs selection ownership.

**Fonts a program describes** — `LOGFONTA` and `CreateFontIndirectA`, which
is `CreateFontA` with the structure read out of it; `TEXTMETRICA` and
`GetTextMetricsA`, worked out from the strike selected into the DC and the
widths of the characters themselves; and `GetDeviceCaps`, whose `LOGPIXELSX`
and `LOGPIXELSY` are the dots per inch the whole library scales by, so a
program working a point size into a height lands where the library would.

**KERNEL32's smaller corners** — `GlobalAlloc`/`LocalAlloc` and their locks,
where a block is its own handle, as it is for fixed memory on Windows;
`GetLocalTime` with `GetDateFormatA`/`GetTimeFormatA` over the C library's
locale, taking win32's flags or a picture (`yyyy-MM-dd`, `h:mm tt`);
`MultiByteToWideChar`/`WideCharToMultiByte` for CP_UTF8 and CP_ACP, the
latter being Latin-1 here, surrogate pairs included. And the entry point: a
win32 program has `WinMain` rather than `main`, and the library supplies the
`main` that calls it — weakly, so a program with one of its own keeps it.

**An icon out of a resource script** — `LoadIconA` walks the `RT_GROUP_ICON`
directory, picks the row nearest the size a caption wears and with the most
colours in it, and reads that `RT_ICON` with the same decoder a `.ico` file
goes through. `DrawIconEx` draws the size it is asked for, so the one 32x32
image a script usually carries becomes the sixteen a caption wants.

**The rich edit, in plain text** — `RICHEDIT_CLASS`, a second text control
rather than a widened EDIT, because the two differ from the first character:
an EDIT keeps one string in the window's text and draws it in one font, and a
rich edit keeps a document. What is there is the plain-text half WordPad's
editor stands on: the class (both the riched20 name and the one Rich Edit 1.0
answered to), its own storage behind WM_SETTEXT/WM_GETTEXT, a line table
rebuilt in one pass, typing, the arrows (which move by the pixel the caret
stands at and remember the one a walk set out from, as riched20 does and an
EDIT does not), Home and End and the pages, the
selection in the EDIT's terms and in a `CHARRANGE`, `EM_GETSELTEXT` and
`EM_GETTEXTRANGE`, one step of undo, the modified flag, `EM_EXLIMITTEXT`, the
vertical bar with the machine's own paging rule, ES_MULTILINE, ES_WANTRETURN
and ES_NOHIDESEL. The event mask is the one place it is meant to differ from
the EDIT: a rich edit says nothing to its parent until `EM_SETEVENTMASK` asks
it to, where an EDIT sends EN_CHANGE whether or not anybody wanted it.

**The runs are there too**, which is the second piece: a run is a first
character and the formatting from there on, kept beside the text rather than
in it. `EM_SETCHARFORMAT` splits the run it lands inside and merges what it
leaves identical to its neighbour; `EM_GETCHARFORMAT` clears the mask bit of
anything that differs across the range and answers with the character before
its end; a character typed takes the formatting of the character before the
caret, and a set on an empty selection arms the next one. Every one of those
is riched20's own behaviour, asked of it with `tools/vm/ctlprobe.c`. Bold,
italic, underline, strikeout, size, face and colour are drawn, each run in
its own, on the line's own baseline; `EM_POSFROMCHAR` says where a character
landed, and `EN_SELCHANGE` tells a format bar the caret has moved.

**And the paragraphs**, which is the third piece: a paragraph mark is a
single carriage return, as Rich Edit 2.0 keeps one, so every offset the
control states counts it once while `WM_GETTEXT` hands the text back with the
CRLF a program expects. `EM_SETPARAFORMAT` takes whole paragraphs -- every
one the selection touches -- and `EM_GETPARAFORMAT` clears the mask bit of
whatever differs across them; a paragraph split in two leaves both halves
carrying what the whole one carried, and a join keeps the first one's.
Alignment and the indents are drawn.

**And it wraps and streams**, which is the fourth piece: a line breaks at the
last space that fits with the space staying on the line that broke, a word
too long breaks at the character, and `EM_SETTARGETDEVICE` with a width and
no device stops the breaking as WordPad's No Wrap asks. `EM_STREAMIN` and
`EM_STREAMOUT` do `SF_TEXT` and `SF_RTF`, the RTF written in the shape
riched20 writes it -- header, font table, colour table, paragraphs, a run
stating only what changed -- and read back with the two traps a round trip
finds: `\deff` names a face the font table has not offered yet, and the
`\par` before the closing brace is a terminator rather than a mark.

What it has not got is tab stops in the drawing, `EM_FINDTEXT`, and the
object side of a rich edit -- pictures and OLE, which WordPad's plan does not
reach. `tests/richedit_test.c` asks it the same questions
`tests/edit_test.c` asks the EDIT, in the same words, so that the two cannot
come to disagree about a behaviour they share.

**ChooseFont** — the common Font dialog, every rectangle the machine's own
out of `reference/probe/font.txt`: the three combos, the Effects group with
its two ticks and its colour, the Sample, the note, and OK, Cancel, Apply and
Help down the right, at the ids a program that hooks the box addresses them
by. `CF_INITTOLOGFONTSTRUCT` selects what the program handed in -- including
the size its `lfHeight` works out to -- `CF_EFFECTS` decides whether the
effects are there at all, `CF_APPLY` whether Apply is, and `CF_ENABLEHOOK`
lets a program see the messages first, which is also how the test drives a
modal box.

What the lists hold is this library's rather than the machine's, and said out
loud rather than implied: there is no rasteriser here, so the box offers the
faces it can actually draw and the sizes their strikes carry -- the way the
machine's own box offers a bitmap face's own sizes rather than every number.
A box offering a face it would then draw in another one would be worse than
none.

**Find and Replace** — `FindTextA` and `ReplaceTextA`, modeless as win32 has
them: the call puts the box up and answers with its window, and every press
reaches the owner as `RegisterWindowMessage(FINDMSGSTRING)` with the
`FINDREPLACE` the program handed over — `FR_FINDNEXT`, `FR_REPLACE`,
`FR_REPLACEALL`, `FR_DIALOGTERM` on the way out, and `FR_DOWN`/`FR_MATCHCASE`
read off the controls rather than remembered. What was typed goes back into
the program's own buffers. The searching is the program's: that is why a text
editor and a hex editor can wear the same box. Laid out from the machine's
own rectangles -- every one of them measured off a capture of the machine's
own Notepad -- read off the guest with GetWindowRect rather than fitted --
and both boxes agree with it in everything but five letters and the bold `F`
of a title.

**The registry** — `RegCreateKeyExA`/`RegOpenKeyExA`/`RegCloseKey`,
`RegQueryValueExA`/`RegSetValueExA`/`RegDeleteValueA`, which is how a Windows
program remembers its font, its window and its ticked boxes between runs.
There is no registry here, so it is a file: `registry.reg` under the user's
config directory (`$XDG_CONFIG_HOME/ween32`, else `~/.config/ween32`), in the
REGEDIT4 format regedit itself exports, which a person can read and edit.
`WEEN32_REGISTRY` names the file outright, which is what the tests use.
REG_SZ is written as text and a four-byte REG_DWORD as `dword:` hex;
everything else keeps its type and its bytes as `hex(n):`. The file is read
once and written whole after every change, through a temporary and a rename
so that a program stopped mid-save keeps the settings it had.

**A static that is not text** — the low five bits of a `STATIC`'s style are a
type: `SS_BITMAP` and `SS_ICON` draw the picture hung on it with
`STM_SETIMAGE`, `SS_ETCHEDHORZ`/`VERT`/`FRAME` the rules a dialog divides
itself with, `SS_BLACKRECT`/`GRAYRECT`/`WHITERECT` a block of a system
colour, and `SS_NOPREFIX` says an ampersand is an ampersand.

**Images and icons** — `CreateBitmap` from pixels in memory, `LoadImageA` for
a `.bmp` or a `.ico` on disk, image lists (`ImageList_Create`/`Add`/
`AddMasked`/`AddIcon`/`Draw`/`Destroy`) with one-bit transparency, and
`DrawIconEx`. The tree and list views draw the image an item names.

**A cursor of the application's own** — `CreateCursor` takes the two masks
win32 has taken since sixteen-bit Windows and `DestroyCursor` gives it back;
a class or a `WM_SETCURSOR` can name one, and the X11 backend turns it into a
pixmap cursor with its hot spot. `WM_SETCURSOR` carries the hit-test code and
the message that asked, so an application that answers only for `HTCLIENT` —
which is what one writes — is heard. Paint has one drawing per tool.

**A shell's controls** — a `ToolbarWindow32` with flat, hot-tracked, checkable
and drop-down buttons, in a `ReBarWindow32` of stacked bands with grippers and
labels; a list view whose columns resize by dragging a header divider; cursors (`LoadCursorA`/`SetCursor`/`WM_SETCURSOR`, a
class cursor, and the shapes a splitter needs); minimize and maximize beside
the close box, with `WM_SYSCOMMAND` behind them; and both views able to be
emptied, walked and scrolled rather than only filled.

**GDI** — `BeginPaint`/`EndPaint`, `FillRect`, `FrameRect`, `DrawEdge` (Wine's
tables, every `BDR_`/`EDGE_` type and `BF_` flag), `DrawFrameControl`
(`DFC_CAPTION` and `DFC_BUTTON`), `TextOutA`, `DrawTextA`,
`GetTextExtentPoint32A`, `SetTextColor`, `SetBkMode`, `GetSysColor(Brush)`,
`CreateSolidBrush`, `CreateFontA`, `DeleteObject`,
`GetStockObject` (the GUI font and the stock brushes), `SelectObject`
(returning what was really selected).

**The drawing half of GDI** — what a paint program is made of, in `draw.c`:
memory device contexts (`CreateCompatibleDC`/`DeleteDC`/
`CreateCompatibleBitmap`/`SelectObject`/`GetObjectA`), pens
(`CreatePen`, `SetROP2`, `MoveToEx`, `LineTo`, `Polyline`, `PolyBezier`),
shapes (`Rectangle`, `Ellipse`, `RoundRect`, `Polygon` filled with the
selected brush), `SetPixel`/`GetPixel`, `ExtFloodFill`, `BitBlt`/
`StretchBlt`/`PatBlt` with the general ROP3 (the high byte is the truth
table of pattern, source and destination, evaluated on whole words),
`InvertRect`, `DrawFocusRect`, `SetViewportOrgEx`, and `GetDIBits`/
`SetDIBits` so a program that has to write a `.bmp` can reach the pixels.

A wide pen puts down the disc Windows puts down, measured off a machine: two
across is a full square, three a plus, four and five squares with the corners
off, seven a proper circle. The far end of a wide line is part of it; a
one-pixel line still leaves its last point to whatever comes next.

**A window's own scroll bars** — `WS_HSCROLL`/`WS_VSCROLL` put a bar in the
non-client area, so `GetClientRect` shrinks; `SetScrollInfo`/`GetScrollInfo`/
`SetScrollPos`/`GetScrollPos`/`SetScrollRange`/`ShowScrollBar`/
`EnableScrollBar` address them, and the window hears `WM_HSCROLL`/
`WM_VSCROLL` with a null lParam. The controls that draw their bars inside
their own client area — an edit, a list box, the two views — say so at
registration so they do not wear both.

**The common dialogs** — `GetOpenFileNameA`, `GetSaveFileNameA` and
`ChooseColorA`, in `comdlg.c`: the dialogs that belong to the system rather
than to the application, built from templates out of the controls ween32
already has. The file one lists directories and files against the caller's
filter; the colour one is the machine's own, on its own dialog units — the
forty-eight basic colours, the custom row, and a definition half that is
there from the start and outside the window until "Define Custom Colors"
widens it. Its field is banded as the machine bands it, sixty steps of hue by
thirty of saturation, and comes out pixel for pixel. `CC_ENABLEHOOK` lets the
application see every message first, which is how a program gives the box a
title of its own.

**Dialogs** — `CreateDialogIndirectParamA` (+ `CreateDialogIndirectA`) builds a
dialog from a `DLGTEMPLATE`/`DLGITEMTEMPLATE`, instantiating each control and
mapping its dialog units to pixels — the position and the size each mapped on
their own, as Windows maps them, and not as a pair of edges; `DLGPROC`
(`WM_INITDIALOG`), `DefDlgProcA`, `EndDialog`, `IsDialogMessageA` (Tab /
Enter→default / Esc), `DM_SETDEFID`, `GetDlgCtrlID`, `GetDialogBaseUnits`,
`MapDialogRect`, `MulDiv`. A template that says `DS_CONTEXTHELP` gets the `?`
beside its close box, and pressing it sends `SC_CONTEXTHELP`.

**What the machine has** — `GlobalMemoryStatus`, from the page count, capped
at four gigabytes in its thirty-two-bit fields exactly as Windows caps it.
An About box is the one place a program says something about the machine.

**Only what changed is painted** — every window keeps the rectangle of its
surface that has to be drawn again, `InvalidateRect`'s rectangle adds to it,
the paint pass clips to it, `BeginPaint` reports it in `rcPaint`, and the
backend puts only that much of the frame on the screen. Drawing a stroke
costs the pixels under the pen rather than the whole window.

**DPI** — one system dpi detected from the desktop's `Xft.dpi` (overridable
with `WEEN32_DPI`); point-sized font strikes, scaled non-client metrics, and
pixel doubling at 200%. `GetDpiForSystem`.

## Controls

Every control on the 98.css list now draws. `examples/controls.c` renders
5.2% differently from the wine reference, most of it deliberate — see
[Reference captures](#reference-captures) for how that is measured and
[docs/testing.md](docs/testing.md) for what every pixel of it is.

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
  parent, a caret when focused, and the selection on a highlight bar. A
  multiline one makes lines with Return, keeps its column between them, and
  answers the messages a text editor asks it — `EM_GETSEL`, `EM_REPLACESEL`,
  the line arithmetic (`EM_GETLINECOUNT`, `EM_LINEINDEX`, `EM_LINEFROMCHAR`,
  `EM_LINELENGTH`, `EM_GETLINE`), `EM_GETMODIFY`, `EM_LIMITTEXT` with
  `EN_MAXTEXT`, and one step of undo. It scrolls to keep the caret in view,
  by `EM_LINESCROLL` and `EM_SCROLLCARET`, and its `WS_VSCROLL` bar is live:
  arrows, track, thumb and wheel, with `EN_VSCROLL` to the parent. Three of
  those four numbers are the machine's own, read off its Notepad — an arrow
  is a line, the thumb is `MulDiv(page, track, lines)`, and a click in the
  track is a screenful *less one line*.
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
- **ListView** — click a row to select it, `LVN_ITEMCHANGED`; a run with
  Shift, one more with Control, a rectangle dragged over the view, Enter to
  open what is picked and F2 to rename it in place. The blue box a name wears
  is what the name draws and eight, measured on the machine.
- **The wheel** goes to the focused window, as win32 sends it — a view scrolls
  once it has been clicked, and never selects.

`tests/input_test.c` drives all of this through the headless backend and
asserts where each control ends up, so CI covers it.

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

- **The registry is a settings file, not a registry.** Values under a key,
  and that is all: no enumerating keys or values, no deleting a key or its
  tree, no security, no remote hives, and the classes root is just another
  name a path can start with. A program needing those is asking for a
  registry rather than for somewhere to put its settings, and a missing
  symbol tells it so more honestly than a call that pretends.
- **`SetBkMode(OPAQUE)` is accepted but unimplemented** (`src/gdi.c:322`):
  text is always drawn transparent.
- **No `PeekMessageA`**, so a message loop can only be run once per process:
  `WM_QUIT` is final. (`SetWindowLongPtrA`/`CallWindowProcA`, `PostMessageA`
  and `GetDC`/`ReleaseDC` are all there now, and the explorer subclasses both
  a list view's label editor and a combo box's field.)
- **The keyboard cues are one flag for the process, not one per window.**
  Win32 keeps UISF_HIDEACCEL per window and passes it down the tree, so a
  menu bar can put its underlines away when its menu closes while a dialog
  opened during that keyboard navigation keeps them. The machine does exactly
  that; ween32's single flag cannot, so the explorer's menu bar keeps its
  underlines after a menu closes — about 130 pixels of that band.
- **The View page's tree is the machine's rows, not the machine's list.** The
  twelve it shows are the ones the capture shows, in that order, and the
  settings this example has no field for are remembered and nothing else. The
  machine has more below them, which is why its scroll bar's thumb is shorter
  than ours.
- **Folder Options has no pictures beside its group boxes.** The machine puts
  a 32-pixel one against each of Active Desktop, Web View, Browse Folders,
  Click items and Folder views; none of the five is among the icons that were
  extracted, and matching the machine's against every one of them by pixel
  finds nothing nearer than half the square. The dialogs are laid out with
  the space they take.
- **A list box's rows are a pixel short of the machine's.** `item_height`
  gives 13 for the 11-pixel Tahoma; the machine's address-bar suggestion box
  puts its rows at 6, 20, 34, 48, 62, 76 and 90, a pitch of 14, and fits
  exactly seven of them in a 98-pixel client. Wine says 13, which is what the
  controls sampler is pinned to, so this is another place where wine and the
  machine disagree and the machine is right. Changing it moves every list
  box, combo box and status bar, so it wants its own pass with a sampler
  captured from the machine rather than from wine.
- **A bordered edit starts its text two pixels short of the machine's** when
  it is set in MS Sans Serif. Ours is wine's rule — half the average
  character width, three for both faces ween32 has — and it puts Tahoma's
  text exactly where wine does; the machine's Column Settings puts MS Sans
  Serif's five in. No metric ween32 can compute tells the two faces apart
  (both average six and top out at eleven), so the rule that yields both is
  still unknown. 77 pixels of that dialog.
- **Choose Columns offers eight columns where the machine's shell offers some
  fifty.** The first eight are the machine's own, in its order, but the scroll
  bar says how much is below them: 718 pixels of the dialog, and all of its
  remaining difference that is not font or caption.
- **Fixed caps still left**: eight columns in a list view, and 64 buttons in
  one group of option buttons. Window text, the class table and the message queue
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

The two samplers differ from their wine renders by 14178 of 298596 pixels and
4397 of 39200, and most of both is *deliberate*: where wine and a Windows 2000
disagree, ween32 follows the machine. The newest hundred are the option
buttons: wine draws the circle at its control's left edge and Windows draws
it one column in, which the guest's own GetWindowRect settles in two dialogs
at once. [docs/testing.md](docs/testing.md) keeps
the current numbers and what every band of them is; the section below is why
the deliberate ones are there.

### Where wine is not the reference

Wine's classic rendering matches Windows 2000 everywhere ween32 has checked it
— except menus, where it is wrong in three ways. A wine drop-down has a flat
one-pixel `COLOR_3DSHADOW` border and single-line separators; a real Windows
2000 menu has a two-pixel raised edge and etched separators. And it draws a
menu bar item's label a row lower than Windows does.

The menu metrics here are measured against Windows itself instead: the
popups are measured off a running machine, item by item.

A menu bar item is its label plus **twelve**, which is what a Windows 2000
Paint's bar measures — its six titles sit thirteen pixels of ink apart and
the first starts six in. It was sixteen here for a while, measured off the
explorer's bar; but that bar is not a menu bar at all. The shell's is a
*toolbar* of drop-down buttons inside its rebar, and a toolbar button's
padding is not a menu item's. Measure a menu bar against a program that has
one.

Two more places where wine and the machine disagree, both settled the same
way and both worth about fifty pixels of the sampler totals: the caption
gradient steps just *after* the exact point rather than on it, and a scroll
bar's up arrow sits in the same four rows of its button as the down arrow.
[docs/paint.md](docs/paint.md) has the measurements.

A shell context menu on a folder is 121 x 238 there and 121 x 238 here, with
every label's ink in the same column and every separator between the same two
rows. What that took, over what a single screenshot had settled: the label
sits one pixel above the middle of its item, the cascade arrow is four wide
and seven tall in a column of twelve with nine before it, the etched
separator stops four pixels from each edge, and a cascade opens six pixels
back over its parent, lifted by the popup's inset so its first item lines up
with the item that opened it.

A menu with pictures in it is laid out wider all round — gutter thirty-one,
items twenty-two tall, four more on the right — which is what makes the
shell's Send To 183 x 94 with its four icons where the same four labels alone
make 168 x 74.

This means `menu-reference.png`, which is a wine render, is now something
ween32 deliberately differs from in the menu-bar band. That difference is
about 242 pixels and is not a regression.

The tree view is the second place. Measured against the machine's own folder
tree, an item's picture starts at nineteen times its depth from the left edge,
its label twenty-one past that, the button that opens it sits in the column
before it, and the whole tree is one pixel down from the top of its window.
Wine's classic tree puts the button one pixel further right and the label two,
and starts at the very top. Neither can be checked against the other — the
sampler's tree has lines at the root and no images, the shell's has images and
no lines at the root — so the machine is followed and about 1270 pixels of
`reference.png`, all of them in the tree's band, are now a deliberate
difference.

The tick box before a label is another: the machine keeps one column more
between a check box and its text than it does between an option button and
its, with the same font and the same thirteen-pixel glyph. Wine gives the two
the same offset, so the sampler's check boxes now differ from it by about
1213 pixels — measured on Folder Options, whose boxes and option buttons sit
on the same page. What that column really is came out later, when the probe
asked Windows for its own rectangles: the labels are level measured from the
control, and it is the circle that sits one column in. docs/testing.md, "The
option button's column, settled by asking Windows".

The list view is the third. Against the machine, a report row is as tall as
the tallest thing in it and one more — seventeen with small icons, fourteen
with none — there are two rows of white between the header and the first item,
the header's text sits six pixels in, a row's label box begins where its icon
ends with the text two past that, and the text is two below the row's top.
Wine's classic list has no gap under its header and its header text eight in.
As with the tree the two configurations do not overlap, so the machine is
followed.

And the caption gradient is the fourth: it holds its start colour behind the
icon, runs from there to two pixels before the leftmost caption button, and
holds its end colour behind the buttons — measured first on a window with
three buttons, whose gradient reaches its end at x=596 of 654, and then on the
machine's Column Settings, which has only a close box and ends its ramp two
pixels before it just the same. Wine ends it one pixel before, so both
sampler windows — each with a single close box — now differ from their
reference render across the whole caption strip, which is most of what each
of them counts, and not a regression.

The ramp is stepped in 16.16 from a step rounded down once, rather than
divided per pixel. The two agree except where a channel would land exactly on
an integer, and there the machine is a shade below; with it, all 305 pixels of
that caption's gradient are the machine's.

The caption's buttons are the fifth: their bevel is a soft edge — white on
the outside of the top and left rather than a pixel in — and the three glyphs
are drawn as measured rather than through Marlett, whose outlines at this size
do not land on the same pixels. Each sits in its own place in the sixteen by
fourteen button: the minimise bar six wide at (4,9), the box nine square at
(3,2), the cross eight by seven at (4,3). The title above them is a pixel
higher than centred, and the icon likewise.

The lesson is narrower than "wine is unreliable". It is that wine is a
reimplementation too, and where it has guessed, following it means inheriting
the guess. A photograph of the real thing settles it.

`examples/menu.c` is the second sampler, and its reference is a window with a
menu bar. What differs there is the caption — its gradient, and its bold title,
which is set in wine's redistributable Tahoma Bold whose eleven-pixel strike is
not the machine's and draws a letter a pixel wider — and the menu bar's own
padding, which is the machine's twelve rather than wine's. The bar's geometry
otherwise lands on wine's pixels, and where it did not, the reference said so
by a pixel and was followed.

The rest — check boxes, option buttons, group box, list box, combo box, both
progress bars, the scroll bar, the tree view, the list view, the status bar and
the trackbars — is within a handful of pixels or exact.

### The explorer, against the machine itself

There is a Windows 2000 VM to compare against now, driven over MCP, and it
mirrors its frame buffer into shared memory — so the comparison is against
the real thing's pixels rather than a photograph of them. `tools/vm/grab.py`
reads that buffer; its explorer window is 654 by 544, which is where the
screenshots came from.

Measured that way, and identical to it pixel for pixel:

- the **rebar**, all of it — the etched rectangle round the control, the
  rules between its bands, the grippers
- the **menu band**, every pixel of it bar the animated flag at its right
- the **Back button**, in both states, cold and under the pointer
- the toolbar's **geometry** — every button boundary, every label, the first
  separator, the checked button — and the images of Back, Forward, Up,
  Search, Folders and History

What that turned up, each of which was wrong before it was measured: a
toolbar image sits three pixels down and not four; a separator is seven
wide with its line four in; a button that is on is dithered on the opposite
parity to a scroll bar, wears one pixel of edge a pixel in from its left,
moves its content in by one, and draws from the toolbar's *second* set of
images just as a hot one does; `GetTextExtentPoint32A` must report what the
glyphs will take rather than a per-character ceiling; and a rebar is ruled
off all the way round rather than only above each band.

Since then every band has come in. What that took, all of it measured by
hovering buttons on the machine and reading their edges off: a separator is
six wide with its line three in; a drop-down button's arrow half is thirteen
after a label and twelve after an image on its own, with five after the
label rather than seven; a button's image sits three down and, on one with
no label, a pixel further left; a button that is on is dithered from three
in and two down to four short of its right and bottom, wears one pixel of
sunken edge a pixel in, closes that edge a pixel wider than a hot one does,
moves its content in by one, and draws from the toolbar's second set of
images; a dead drop-down arrow is embossed rather than greyed, though its
image is not; a rebar is ruled off all the way round, its bands separated by
the same two lines, its grippers one pixel of raised edge four in; a band's
label sits eleven in and a row above centre with four after it; and
`MoveWindow` on a child has to send `WM_SIZE`.

Measured against the machine, band by band:

| band | differing |
| --- | --- |
| the rebar's own edge | **0** |
| menu band | **0**, outside the animation the shell plays at its right |
| address band | **0**, outside what is written in the combo box |
| toolbar band | **0** |
| the context menu on a folder, from either panel | **0** |
| the context menu on a file | **0** |
| the "Send To" submenu | **4**, inside the fraction in "3 1/2 Floppy (A)" |
| the menu on the background | **0** |
| the tree pane, frame and bar | **0** |
| the tree itself | **0** outside the 16-pixel icon columns |

Nothing is left in it. What differs in the window is the animation playing
in the brand box and the path written in the address combo — the machine is
looking at Local Disk (C:) and this is looking at a file system — and
neither of those is a rendering. The four in the submenu are our Tahoma
strike drawing the small one of the fraction with a foot where Windows
draws it without.

The menus were measured with the machine's own menu open beside ours: the
count is over everything the screenshot is not drawing something else over,
which is the row the pointer was hovering, the submenu overlapping its
parent's border, and the mouse pointer itself, which the frame buffer has
drawn into it. The menu a right click brings up in the tree is the same one
it brings up in the list, on the machine as here — 121 x 238 with the same
eleven items — and the one on a file is 121 x 212.

The last three took the longest and were all one mistake, made twice: a
dead image *is* the live one embossed, but the silhouette leaves white out.
A shell's images are lit from the top left and the highlight is pure white;
leave it in and the shape fills solid and loses the detail the real dead
image keeps. Take it out and the emboss lands pixel for pixel — and that is
where the seventeenth column comes from, the emboss reaching a pixel past
the cell the image fills. Which of the four buttons are dead follows from
it too: Move To and Copy To are embossed on the machine and Delete and Undo
are not.

### The explorer, against its screenshot

`examples/explorer` is laid out against screenshots of the real thing, and
the parts that are geometry now land on them: the four column dividers, both
edges of the checked toolbar button, the first separator, the splitter, the
caption icon and title, and the status bar's two divisions. Three toolbar
metrics were corrected to get there and are pinned in `toolbar_test`.

The **Back button is exact** — every pixel of it, in both states, against a
shot with the pointer on it and a shot without. Getting there turned up four
things: a hot button in a flat toolbar wears one pixel of edge and starts one
pixel in; the arrow half that comes up is thirteen wide where the layout
reserves eleven; the image sits four pixels down from the top and the label
one above the middle, neither centred; and the arrow is blue under the
pointer, which is `TB_SETHOTIMAGELIST`.

What still differs, and why. The sort arrow, the Go button beside the address
bar and the menu bar as a rebar band were all on this list and are all here
now — the arrow through `LVM_GETHEADER` and `HDM_SETITEM`, as in win32. What
is left:

- The tree lists the **file system** rather than the shell namespace, so it
  starts at a directory instead of at Desktop, My Documents and My Computer.

The underlines under a menu's accelerators looked like a difference and were
a missing behaviour: Windows 2000 hides them until the keyboard has been used
to reach a menu — Alt, F10, or an arrow key inside one — and shows them
everywhere from then on. Which is why one screenshot in hand has none and
another has them on the bar and in the drop-down both. ween32 now keeps the
same flag, so a menu opened and picked from with the mouse has none.

## Testing

`make test` covers the engine pixels, the API path, the dialog manager, input
and resizing, two windows at once, timers, the keyboard conventions, menus,
modal dialogs, the views and the shell's own controls — 546 assertions, all
headless, so CI runs the lot. [docs/testing.md](docs/testing.md) is how each
part of it is run and what it is counted against. Gaps worth closing:

- the tests pin `WEEN32_DPI=96`; only the `Xft.dpi` *parser* is covered, so
  the 120/144 strike snapping and the 192 pixel-doubling have no assertions;
- the headless screenshots are only checked for size in CI, not compared
  against the reference captures above;
- a message box and a modal dialog have no reference capture of their own. The
  capture needs the window to take the input focus, and the display this was
  built on would not give it one; `WEEN32_BMP=/tmp/frame%d.bmp` is how their
  ween32 side gets looked at meanwhile.
