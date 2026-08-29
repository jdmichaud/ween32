//! Zig bindings for the win32-compatible API.
//!
//! On non-Windows targets these symbols resolve to libween32 (link the
//! `ween32` artifact); on Windows they are the real USER32/GDI32/COMCTL32 —
//! same names, signatures and constants, so application code is identical on
//! both. This mirrors include/ween32.h; keep the two in sync.
//!
//! The constants at the end are *generated* from that header by
//! `tools/zigbind/genconsts.py --merge`, because a constant copied wrong is a
//! bug that compiles, links and runs — and `make win32` already checks the C
//! header's values against the real Windows SDK, so the Zig side inherits
//! that check instead of needing a second one. Everything above them is
//! written by hand.

const std = @import("std");


// ---- fundamental types (LLP64-faithful) ---------------------------------

pub const BOOL = c_int;
pub const BYTE = u8;
pub const WORD = u16;
pub const SHORT = i16;
pub const DWORD = u32;
pub const LONG = i32;
pub const UINT = c_uint;
pub const WPARAM = usize;
pub const LONG_PTR = isize;
pub const LPARAM = isize;
pub const LRESULT = isize;
pub const INT_PTR = isize;
pub const UINT_PTR = usize;
pub const SIZE_T = usize;
pub const ATOM = u16;
pub const COLORREF = u32; // 0x00BBGGRR
pub const LPCSTR = [*:0]const u8;
pub const LPSTR = [*:0]u8;

pub const TRUE: BOOL = 1;
pub const FALSE: BOOL = 0;

pub const HWND = *opaque {};
pub const HDC = *opaque {};
pub const HGDIOBJ = *opaque {};
pub const HBRUSH = HGDIOBJ;
pub const HFONT = HGDIOBJ;
pub const HPEN = HGDIOBJ;
pub const HBITMAP = HGDIOBJ;
pub const HINSTANCE = ?*anyopaque;
pub const HICON = ?*anyopaque;
pub const HCURSOR = ?*anyopaque;
pub const HMENU = ?*anyopaque;
pub const HANDLE = ?*anyopaque;
pub const HACCEL = ?*anyopaque;
pub const HIMAGELIST = ?*anyopaque;

pub const POINT = extern struct { x: LONG, y: LONG };
pub const RECT = extern struct { left: LONG, top: LONG, right: LONG, bottom: LONG };
pub const SIZE = extern struct { cx: LONG, cy: LONG };

pub const MSG = extern struct {
    hwnd: ?HWND,
    message: UINT,
    wParam: WPARAM,
    lParam: LPARAM,
    time: DWORD,
    pt: POINT,
};

pub const WNDPROC = *const fn (HWND, UINT, WPARAM, LPARAM) callconv(.c) LRESULT;
pub const DLGPROC = *const fn (HWND, UINT, WPARAM, LPARAM) callconv(.c) INT_PTR;
pub const TIMERPROC = *const fn (HWND, UINT, UINT_PTR, DWORD) callconv(.c) void;

pub const WNDCLASSA = extern struct {
    style: UINT = 0,
    lpfnWndProc: WNDPROC,
    cbClsExtra: c_int = 0,
    cbWndExtra: c_int = 0,
    hInstance: HINSTANCE = null,
    hIcon: HICON = null,
    hCursor: HCURSOR = null,
    hbrBackground: ?HBRUSH = null,
    lpszMenuName: ?LPCSTR = null,
    lpszClassName: LPCSTR,
};

pub const PAINTSTRUCT = extern struct {
    hdc: HDC,
    fErase: BOOL,
    rcPaint: RECT,
    fRestore: BOOL,
    fIncUpdate: BOOL,
    rgbReserved: [32]BYTE,
};

pub const CREATESTRUCTA = extern struct {
    lpCreateParams: ?*anyopaque,
    hInstance: HINSTANCE,
    hMenu: HMENU,
    hwndParent: ?HWND,
    cy: c_int,
    cx: c_int,
    y: c_int,
    x: c_int,
    style: LONG,
    lpszName: ?LPCSTR,
    lpszClass: ?LPCSTR,
    dwExStyle: DWORD,
};

pub const DRAWITEMSTRUCT = extern struct {
    CtlType: UINT,
    CtlID: UINT,
    itemID: UINT,
    itemAction: UINT,
    itemState: UINT,
    hwndItem: HWND,
    hDC: HDC,
    rcItem: RECT,
    itemData: UINT_PTR,
};

pub const NMHDR = extern struct {
    hwndFrom: HWND,
    idFrom: UINT_PTR,
    code: UINT,
};

/// What a tooltip asks its owner for, through WM_NOTIFY. Point `lpszText` at
/// your own string or fill `szText`; the control reads whichever you set.
pub const NMTTDISPINFOA = extern struct {
    hdr: NMHDR,
    lpszText: ?[*:0]u8,
    szText: [80]u8,
    hinst: HINSTANCE,
    uFlags: UINT,
    lParam: LPARAM,
};

pub const TRACKMOUSEEVENT = extern struct {
    cbSize: DWORD,
    dwFlags: DWORD,
    hwndTrack: ?HWND,
    dwHoverTime: DWORD,
};

pub const INITCOMMONCONTROLSEX = extern struct {
    dwSize: DWORD,
    dwICC: DWORD,
};

pub const ACCEL = extern struct {
    fVirt: BYTE,
    key: WORD,
    cmd: WORD,
};

pub const SCROLLINFO = extern struct {
    cbSize: UINT = @sizeOf(SCROLLINFO),
    fMask: UINT = 0,
    nMin: c_int = 0,
    nMax: c_int = 0,
    nPage: UINT = 0,
    nPos: c_int = 0,
    nTrackPos: c_int = 0,
};

pub const BITMAP = extern struct {
    bmType: LONG = 0,
    bmWidth: LONG = 0,
    bmHeight: LONG = 0,
    bmWidthBytes: LONG = 0,
    bmPlanes: WORD = 0,
    bmBitsPixel: WORD = 0,
    bmBits: ?*anyopaque = null,
};

pub const RGBQUAD = extern struct {
    rgbBlue: BYTE = 0,
    rgbGreen: BYTE = 0,
    rgbRed: BYTE = 0,
    rgbReserved: BYTE = 0,
};

pub const BITMAPINFOHEADER = extern struct {
    biSize: DWORD = @sizeOf(BITMAPINFOHEADER),
    biWidth: LONG = 0,
    biHeight: LONG = 0,
    biPlanes: WORD = 1,
    biBitCount: WORD = 24,
    biCompression: DWORD = 0,
    biSizeImage: DWORD = 0,
    biXPelsPerMeter: LONG = 0,
    biYPelsPerMeter: LONG = 0,
    biClrUsed: DWORD = 0,
    biClrImportant: DWORD = 0,
};

pub const BITMAPINFO = extern struct {
    bmiHeader: BITMAPINFOHEADER = .{},
    bmiColors: [1]RGBQUAD = .{.{}},
};

pub const BITMAPFILEHEADER = extern struct {
    bfType: WORD align(1) = 0x4D42, // "BM"
    bfSize: DWORD align(1) = 0,
    bfReserved1: WORD align(1) = 0,
    bfReserved2: WORD align(1) = 0,
    bfOffBits: DWORD align(1) = 0,
};

// A dialog template, as CreateDialogIndirectParam takes one: the header, then
// each control, every one of them padded to a 4-byte boundary. The strings
// are UTF-16 in a real template; ween32 and win32 both read them that way.
// align(2), not align(4): the C header wraps these two in `#pragma pack(2)`,
// as win32 does, so DLGTEMPLATE is eighteen bytes and not twenty. A dialog
// template is a run of bytes -- the header, then the menu, class and title,
// then the items -- so two bytes of tail padding here put everything after it
// out of place. Caught by tools/zigbind/genstructs.py.
pub const DLGTEMPLATE = extern struct {
    style: DWORD align(2),
    dwExtendedStyle: DWORD align(2),
    cdit: WORD,
    x: i16,
    y: i16,
    cx: i16,
    cy: i16,
};

pub const DLGITEMTEMPLATE = extern struct {
    style: DWORD align(2),
    dwExtendedStyle: DWORD align(2),
    x: i16,
    y: i16,
    cx: i16,
    cy: i16,
    id: WORD,
};

// ---- macros --------------------------------------------------------------

inline fn asUsize(v: anytype) usize {
    return switch (@typeInfo(@TypeOf(v))) {
        .int => |i| if (i.signedness == .signed)
            @bitCast(@as(isize, v))
        else
            @as(usize, v),
        else => v, // comptime_int
    };
}

pub inline fn LOWORD(l: anytype) WORD {
    return @truncate(asUsize(l));
}
pub inline fn HIWORD(l: anytype) WORD {
    return @truncate(asUsize(l) >> 16);
}
pub inline fn MAKELPARAM(a: WORD, b: WORD) LPARAM {
    return @bitCast(@as(usize, a) | (@as(usize, b) << 16));
}
pub inline fn MAKEWPARAM(a: WORD, b: WORD) WPARAM {
    return @as(usize, a) | (@as(usize, b) << 16);
}
pub inline fn GET_X_LPARAM(lp: LPARAM) i32 {
    return @as(i16, @bitCast(LOWORD(lp)));
}
pub inline fn GET_Y_LPARAM(lp: LPARAM) i32 {
    return @as(i16, @bitCast(HIWORD(lp)));
}
pub inline fn RGB(r: u8, g: u8, b: u8) COLORREF {
    return @as(u32, r) | (@as(u32, g) << 8) | (@as(u32, b) << 16);
}
pub inline fn GetRValue(c: COLORREF) u8 {
    return @truncate(c);
}
pub inline fn GetGValue(c: COLORREF) u8 {
    return @truncate(c >> 8);
}
pub inline fn GetBValue(c: COLORREF) u8 {
    return @truncate(c >> 16);
}
pub inline fn MAKEINTRESOURCE(i: u16) LPCSTR {
    return @ptrFromInt(@as(usize, i));
}

pub const CW_USEDEFAULT: c_int = @bitCast(@as(u32, 0x80000000));

// The stock cursors, which are numbers pretending to be strings: the C
// header spells them as casts, so they are written out here rather than
// generated with the rest.
pub const IDC_ARROW = MAKEINTRESOURCE(32512);
pub const IDC_IBEAM = MAKEINTRESOURCE(32513);
pub const IDC_WAIT = MAKEINTRESOURCE(32514);
pub const IDC_CROSS = MAKEINTRESOURCE(32515);
pub const IDC_SIZENWSE = MAKEINTRESOURCE(32642);
pub const IDC_SIZENESW = MAKEINTRESOURCE(32643);
pub const IDC_SIZEWE = MAKEINTRESOURCE(32644);
pub const IDC_SIZENS = MAKEINTRESOURCE(32645);
pub const IDC_SIZEALL = MAKEINTRESOURCE(32646);
pub const IDC_HAND = MAKEINTRESOURCE(32649);

// ---- USER32: windows and messages ---------------------------------------

pub extern fn RegisterClassA(wc: *const WNDCLASSA) callconv(.c) ATOM;
pub extern fn CreateWindowExA(ex_style: DWORD, class_name: LPCSTR, window_name: ?LPCSTR, style: DWORD, x: c_int, y: c_int, w: c_int, h: c_int, parent: ?HWND, menu: HMENU, inst: HINSTANCE, param: ?*anyopaque) callconv(.c) ?HWND;
pub fn CreateWindowA(class_name: LPCSTR, window_name: ?LPCSTR, style: DWORD, x: c_int, y: c_int, w: c_int, h: c_int, parent: ?HWND, menu: HMENU, inst: HINSTANCE, param: ?*anyopaque) ?HWND {
    return CreateWindowExA(0, class_name, window_name, style, x, y, w, h, parent, menu, inst, param);
}
pub extern fn DestroyWindow(wnd: HWND) callconv(.c) BOOL;
pub extern fn ShowWindow(wnd: HWND, cmd: c_int) callconv(.c) BOOL;
pub extern fn SetWindowTextA(wnd: HWND, text: LPCSTR) callconv(.c) BOOL;
pub extern fn GetWindowTextA(wnd: HWND, out: LPSTR, max: c_int) callconv(.c) c_int;
pub extern fn GetWindowTextLengthA(wnd: HWND) callconv(.c) c_int;
pub extern fn GetClientRect(wnd: HWND, rect: *RECT) callconv(.c) BOOL;
pub extern fn GetWindowRect(wnd: HWND, rect: *RECT) callconv(.c) BOOL;
pub extern fn AdjustWindowRect(rect: *RECT, style: DWORD, menu: BOOL) callconv(.c) BOOL;
pub extern fn AdjustWindowRectEx(rect: *RECT, style: DWORD, menu: BOOL, ex_style: DWORD) callconv(.c) BOOL;
pub extern fn GetDpiForSystem() callconv(.c) UINT;
pub extern fn MoveWindow(wnd: HWND, x: c_int, y: c_int, w: c_int, h: c_int, repaint: BOOL) callconv(.c) BOOL;
pub extern fn InvalidateRect(wnd: HWND, rect: ?*const RECT, erase: BOOL) callconv(.c) BOOL;
pub extern fn UpdateWindow(wnd: HWND) callconv(.c) BOOL;
pub extern fn GetDlgItem(dlg: HWND, id: c_int) callconv(.c) ?HWND;
pub extern fn GetDlgCtrlID(wnd: HWND) callconv(.c) c_int;
pub extern fn GetDlgItemTextA(dlg: HWND, id: c_int, out: LPSTR, max: c_int) callconv(.c) UINT;
pub extern fn SetDlgItemTextA(dlg: HWND, id: c_int, text: LPCSTR) callconv(.c) BOOL;
pub extern fn SetFocus(wnd: ?HWND) callconv(.c) ?HWND;
pub extern fn GetFocus() callconv(.c) ?HWND;
pub extern fn SetCapture(wnd: HWND) callconv(.c) ?HWND;
pub extern fn ReleaseCapture() callconv(.c) BOOL;
pub extern fn GetCapture() callconv(.c) ?HWND;
pub extern fn GetParent(wnd: HWND) callconv(.c) ?HWND;
pub extern fn EnableWindow(wnd: HWND, enable: BOOL) callconv(.c) BOOL;
pub extern fn IsWindowEnabled(wnd: HWND) callconv(.c) BOOL;
pub extern fn CheckDlgButton(dlg: HWND, id: c_int, check: UINT) callconv(.c) BOOL;
pub extern fn IsDlgButtonChecked(dlg: HWND, id: c_int) callconv(.c) UINT;
pub extern fn CheckRadioButton(dlg: HWND, first: c_int, last: c_int, check: c_int) callconv(.c) BOOL;
pub extern fn GetSystemMetrics(index: c_int) callconv(.c) c_int;
pub extern fn GetCommandLineA() callconv(.c) LPSTR;

/// The six byte counts are SIZE_T -- as wide as a pointer -- and only the
/// length and the load are DWORD. Declaring them all DWORD makes this struct
/// half the size win32 fills in, and GlobalMemoryStatus writes the whole of
/// its own over the top of it: the fields read wrong and the stack takes the
/// rest. That is true of the real USER32 as much as of ween32, so this was
/// wrong on Windows too.
pub const MEMORYSTATUS = extern struct {
    dwLength: DWORD = @sizeOf(MEMORYSTATUS),
    dwMemoryLoad: DWORD = 0,
    dwTotalPhys: SIZE_T = 0,
    dwAvailPhys: SIZE_T = 0,
    dwTotalPageFile: SIZE_T = 0,
    dwAvailPageFile: SIZE_T = 0,
    dwTotalVirtual: SIZE_T = 0,
    dwAvailVirtual: SIZE_T = 0,
};
pub extern fn GlobalMemoryStatus(status: *MEMORYSTATUS) callconv(.c) void;
pub extern fn ClientToScreen(wnd: HWND, pt: *POINT) callconv(.c) BOOL;
pub extern fn ScreenToClient(wnd: HWND, pt: *POINT) callconv(.c) BOOL;
pub extern fn GetCursorPos(pt: *POINT) callconv(.c) BOOL;

// ---- files ----------------------------------------------------------------
// What a win32 program reads and writes a file with. Going through these
// rather than the C library's is also what lets the Windows build carry no C
// runtime: the one a modern toolchain links is the UCRT, which a machine of
// this vintage cannot even start a program that asks for.
pub const INVALID_HANDLE_VALUE: HANDLE = @ptrFromInt(std.math.maxInt(usize));
pub const GENERIC_READ: u32 = 0x80000000;
pub const GENERIC_WRITE: u32 = 0x40000000;
pub const FILE_SHARE_READ: u32 = 1;
pub const FILE_SHARE_WRITE: u32 = 2;
pub const CREATE_ALWAYS: u32 = 2;
pub const OPEN_EXISTING: u32 = 3;
pub const FILE_ATTRIBUTE_NORMAL: u32 = 0x80;
pub const FILE_BEGIN: u32 = 0;
pub const FILE_CURRENT: u32 = 1;
pub const FILE_END: u32 = 2;
pub const INVALID_SET_FILE_POINTER: u32 = 0xFFFFFFFF;
pub const INVALID_FILE_SIZE: u32 = 0xFFFFFFFF;
pub extern fn CreateFileA(name: [*:0]const u8, access: u32, share: u32, security: ?*anyopaque, disposition: u32, flags: u32, template: HANDLE) callconv(.c) HANDLE;
pub extern fn ReadFile(file: HANDLE, buf: [*]u8, to_read: u32, read: *u32, ovl: ?*anyopaque) callconv(.c) BOOL;
pub extern fn WriteFile(file: HANDLE, buf: [*]const u8, to_write: u32, written: *u32, ovl: ?*anyopaque) callconv(.c) BOOL;
pub extern fn SetFilePointer(file: HANDLE, distance: i32, high: ?*i32, method: u32) callconv(.c) u32;
pub extern fn GetFileSize(file: HANDLE, high: ?*u32) callconv(.c) u32;
pub extern fn CloseHandle(h: HANDLE) callconv(.c) BOOL;
pub extern fn IsWindowVisible(wnd: HWND) callconv(.c) BOOL;
pub extern fn GetWindowLongA(wnd: HWND, index: c_int) callconv(.c) LONG;
pub extern fn SetWindowLongA(wnd: HWND, index: c_int, value: LONG) callconv(.c) LONG;
pub extern fn GetWindowLongPtrA(wnd: HWND, index: c_int) callconv(.c) LONG_PTR;
pub extern fn SetWindowLongPtrA(wnd: HWND, index: c_int, value: LONG_PTR) callconv(.c) LONG_PTR;
/// Subclassing: keep what SetWindowLongPtrA(GWLP_WNDPROC) handed back and
/// call it for everything the new procedure does not want.
pub extern fn CallWindowProcA(proc: WNDPROC, wnd: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) callconv(.c) LRESULT;
pub extern fn TrackMouseEvent(track: *TRACKMOUSEEVENT) callconv(.c) BOOL;
pub extern fn GetKeyState(vk: c_int) callconv(.c) SHORT;
pub extern fn SetTimer(wnd: HWND, id: UINT_PTR, ms: UINT, proc: ?TIMERPROC) callconv(.c) UINT_PTR;
pub extern fn KillTimer(wnd: HWND, id: UINT_PTR) callconv(.c) BOOL;
pub extern fn LoadCursorA(inst: HINSTANCE, name: LPCSTR) callconv(.c) HCURSOR;
pub extern fn SetCursor(cursor: HCURSOR) callconv(.c) HCURSOR;
pub extern fn CreateCursor(inst: HINSTANCE, xhot: c_int, yhot: c_int, width: c_int, height: c_int, and_plane: *const anyopaque, xor_plane: *const anyopaque) callconv(.c) HCURSOR;
pub extern fn DestroyCursor(cursor: HCURSOR) callconv(.c) BOOL;
pub extern fn LoadImageA(inst: HINSTANCE, name: LPCSTR, kind: UINT, cx: c_int, cy: c_int, flags: UINT) callconv(.c) HANDLE;
/// The menu and the accelerator table a program keeps in its resources.
/// WordPad's frame is built from both, which is what put them here: the C
/// header has had them since the .res reader landed and the Zig module had
/// not, and a missing declaration here is a compile error in the application
/// rather than a wrong answer -- which is the right way round.
pub extern fn LoadMenuA(instance: HINSTANCE, name: LPCSTR) callconv(.c) HMENU;
pub extern fn LoadAcceleratorsA(instance: HINSTANCE, name: LPCSTR) callconv(.c) HACCEL;
pub extern fn MessageBoxA(owner: ?HWND, text: LPCSTR, caption: LPCSTR, kind: UINT) callconv(.c) c_int;

pub extern fn GetMessageA(msg: *MSG, wnd: ?HWND, min: UINT, max: UINT) callconv(.c) BOOL;
pub extern fn TranslateMessage(msg: *const MSG) callconv(.c) BOOL;
pub extern fn DispatchMessageA(msg: *const MSG) callconv(.c) LRESULT;
pub extern fn PostQuitMessage(code: c_int) callconv(.c) void;
pub extern fn SendMessageA(wnd: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) callconv(.c) LRESULT;
pub extern fn PostMessageA(wnd: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) callconv(.c) BOOL;
pub extern fn DefWindowProcA(wnd: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) callconv(.c) LRESULT;

// ---- USER32: menus -------------------------------------------------------

pub extern fn CreateMenu() callconv(.c) HMENU;
pub extern fn CreatePopupMenu() callconv(.c) HMENU;
pub extern fn DestroyMenu(menu: HMENU) callconv(.c) BOOL;
pub extern fn AppendMenuA(menu: HMENU, flags: UINT, id: UINT_PTR, text: ?LPCSTR) callconv(.c) BOOL;
pub extern fn SetMenu(wnd: HWND, menu: HMENU) callconv(.c) BOOL;
pub extern fn GetMenu(wnd: HWND) callconv(.c) HMENU;
pub extern fn GetSubMenu(menu: HMENU, pos: c_int) callconv(.c) HMENU;
pub extern fn GetMenuItemCount(menu: HMENU) callconv(.c) c_int;
pub extern fn InsertMenuA(menu: HMENU, before: UINT, flags: UINT, id: usize, text: ?LPCSTR) callconv(.c) BOOL;
pub extern fn DeleteMenu(menu: HMENU, item: UINT, flags: UINT) callconv(.c) BOOL;
pub extern fn CheckMenuItem(menu: HMENU, id: UINT, check: UINT) callconv(.c) DWORD;
pub extern fn CheckMenuRadioItem(menu: HMENU, first: UINT, last: UINT, check: UINT, flags: UINT) callconv(.c) BOOL;
pub extern fn EnableMenuItem(menu: HMENU, id: UINT, enable: UINT) callconv(.c) BOOL;
pub extern fn ModifyMenuA(menu: HMENU, item: UINT, flags: UINT, id: UINT_PTR, text: ?LPCSTR) callconv(.c) BOOL;
pub extern fn TrackPopupMenu(menu: HMENU, flags: UINT, x: c_int, y: c_int, reserved: c_int, owner: HWND, rect: ?*const RECT) callconv(.c) BOOL;

// ---- USER32: accelerators, clipboard, dialogs ----------------------------

pub extern fn CreateAcceleratorTableA(accels: [*]const ACCEL, count: c_int) callconv(.c) HACCEL;
pub extern fn DestroyAcceleratorTable(table: HACCEL) callconv(.c) BOOL;
pub extern fn TranslateAcceleratorA(wnd: HWND, table: HACCEL, msg: *MSG) callconv(.c) c_int;

pub extern fn OpenClipboard(owner: ?HWND) callconv(.c) BOOL;
pub extern fn CloseClipboard() callconv(.c) BOOL;
pub extern fn EmptyClipboard() callconv(.c) BOOL;
pub extern fn SetClipboardData(format: UINT, data: HANDLE) callconv(.c) HANDLE;
pub extern fn GetClipboardData(format: UINT) callconv(.c) HANDLE;
pub extern fn IsClipboardFormatAvailable(format: UINT) callconv(.c) BOOL;

pub extern fn GetDialogBaseUnits() callconv(.c) LONG;
pub extern fn MapDialogRect(dlg: ?HWND, rect: *RECT) callconv(.c) BOOL;
pub extern fn MulDiv(number: c_int, numerator: c_int, denominator: c_int) callconv(.c) c_int;
pub extern fn IsDialogMessageA(dlg: HWND, msg: *MSG) callconv(.c) BOOL;
pub extern fn DefDlgProcA(dlg: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) callconv(.c) LRESULT;
pub extern fn EndDialog(dlg: HWND, result: INT_PTR) callconv(.c) BOOL;
pub extern fn CreateDialogIndirectParamA(inst: HINSTANCE, tmpl: *const DLGTEMPLATE, parent: ?HWND, proc: DLGPROC, param: LPARAM) callconv(.c) ?HWND;
pub extern fn DialogBoxIndirectParamA(inst: HINSTANCE, tmpl: *const DLGTEMPLATE, owner: ?HWND, proc: DLGPROC, param: LPARAM) callconv(.c) INT_PTR;
/// The same, from a DIALOG in the script rather than a template in memory --
/// which is how a program that has a resource script writes it.
pub extern fn DialogBoxParamA(inst: HINSTANCE, name: LPCSTR, owner: ?HWND, proc: DLGPROC, param: LPARAM) callconv(.c) INT_PTR;

// ---- USER32: a window's own scroll bars ---------------------------------

pub extern fn SetScrollInfo(wnd: HWND, bar: c_int, info: *const SCROLLINFO, redraw: BOOL) callconv(.c) c_int;
pub extern fn GetScrollInfo(wnd: HWND, bar: c_int, info: *SCROLLINFO) callconv(.c) BOOL;
pub extern fn SetScrollPos(wnd: HWND, bar: c_int, pos: c_int, redraw: BOOL) callconv(.c) c_int;
pub extern fn GetScrollPos(wnd: HWND, bar: c_int) callconv(.c) c_int;
pub extern fn SetScrollRange(wnd: HWND, bar: c_int, min: c_int, max: c_int, redraw: BOOL) callconv(.c) BOOL;
pub extern fn GetScrollRange(wnd: HWND, bar: c_int, min: *c_int, max: *c_int) callconv(.c) BOOL;
pub extern fn ShowScrollBar(wnd: HWND, bar: c_int, show: BOOL) callconv(.c) BOOL;
pub extern fn EnableScrollBar(wnd: HWND, bar: UINT, flags: UINT) callconv(.c) BOOL;

// ---- COMDLG32 ------------------------------------------------------------

pub const OPENFILENAMEA = extern struct {
    lStructSize: DWORD = @sizeOf(OPENFILENAMEA),
    hwndOwner: ?HWND = null,
    hInstance: HINSTANCE = null,
    lpstrFilter: ?LPCSTR = null,
    lpstrCustomFilter: ?LPSTR = null,
    nMaxCustFilter: DWORD = 0,
    nFilterIndex: DWORD = 1,
    lpstrFile: LPSTR,
    nMaxFile: DWORD,
    lpstrFileTitle: ?LPSTR = null,
    nMaxFileTitle: DWORD = 0,
    lpstrInitialDir: ?LPCSTR = null,
    lpstrTitle: ?LPCSTR = null,
    Flags: DWORD = 0,
    nFileOffset: WORD = 0,
    nFileExtension: WORD = 0,
    lpstrDefExt: ?LPCSTR = null,
    lCustData: LPARAM = 0,
    /// Offered every message before the dialog sees it, when the flags say
    /// CC_ENABLEHOOK: what an application gives the system's colour box a
    /// title of its own with.
    lpfnHook: ?*const fn (HWND, UINT, WPARAM, LPARAM) callconv(.c) INT_PTR = null,
    lpTemplateName: ?LPCSTR = null,
    /// win32's three, which nothing here reads. They are declared because the
    /// struct an application fills in has to be the struct it is filling --
    /// the C header grew them for the same reason.
    pvReserved: ?*anyopaque = null,
    dwReserved: DWORD = 0,
    FlagsEx: DWORD = 0,
};

pub const CHOOSECOLORA = extern struct {
    lStructSize: DWORD = @sizeOf(CHOOSECOLORA),
    hwndOwner: ?HWND = null,
    hInstance: ?HWND = null,
    rgbResult: COLORREF = 0,
    lpCustColors: [*]COLORREF,
    Flags: DWORD = 0,
    lCustData: LPARAM = 0,
    /// Offered every message before the dialog sees it, when the flags say
    /// CC_ENABLEHOOK: what an application gives the system's colour box a
    /// title of its own with.
    lpfnHook: ?*const fn (HWND, UINT, WPARAM, LPARAM) callconv(.c) INT_PTR = null,
    lpTemplateName: ?LPCSTR = null,
};

pub extern fn GetOpenFileNameA(ofn: *OPENFILENAMEA) callconv(.c) BOOL;
pub extern fn GetSaveFileNameA(ofn: *OPENFILENAMEA) callconv(.c) BOOL;
pub extern fn ChooseColorA(cc: *CHOOSECOLORA) callconv(.c) BOOL;

// ---- COMCTL32 ------------------------------------------------------------

pub extern fn InitCommonControlsEx(icc: *const INITCOMMONCONTROLSEX) callconv(.c) BOOL;
pub extern fn InitCommonControls() callconv(.c) void;
/// A BITMAP a program keeps in its script: a strip of toolbar art is the
/// usual reason to want one.
/// Returns null when the script has not got it, which HBITMAP -- an
/// unadorned `*opaque{}` here -- cannot say on its own.
pub extern fn LoadBitmapA(inst: HINSTANCE, name: LPCSTR) callconv(.c) ?HBITMAP;
pub extern fn ImageList_Create(cx: c_int, cy: c_int, flags: UINT, initial: c_int, grow: c_int) callconv(.c) HIMAGELIST;
pub extern fn ImageList_Destroy(il: HIMAGELIST) callconv(.c) BOOL;
pub extern fn ImageList_Add(il: HIMAGELIST, image: HBITMAP, mask: ?HBITMAP) callconv(.c) c_int;
pub extern fn ImageList_AddMasked(il: HIMAGELIST, image: HBITMAP, transparent: COLORREF) callconv(.c) c_int;
pub extern fn ImageList_Draw(il: HIMAGELIST, index: c_int, dc: HDC, x: c_int, y: c_int, style: UINT) callconv(.c) BOOL;

// ---- GDI: contexts, objects and text ------------------------------------

pub extern fn BeginPaint(wnd: HWND, ps: *PAINTSTRUCT) callconv(.c) ?HDC;
pub extern fn EndPaint(wnd: HWND, ps: *const PAINTSTRUCT) callconv(.c) BOOL;
pub extern fn GetDC(wnd: ?HWND) callconv(.c) ?HDC;
pub extern fn ReleaseDC(wnd: ?HWND, dc: HDC) callconv(.c) c_int;
pub extern fn FillRect(dc: HDC, rect: *const RECT, brush: HBRUSH) callconv(.c) BOOL;
pub extern fn FrameRect(dc: HDC, rect: *const RECT, brush: HBRUSH) callconv(.c) c_int;
pub extern fn DrawEdge(dc: HDC, rect: *RECT, edge: UINT, flags: UINT) callconv(.c) BOOL;
pub extern fn DrawFrameControl(dc: HDC, rect: *RECT, kind: UINT, state: UINT) callconv(.c) BOOL;
pub extern fn TextOutA(dc: HDC, x: c_int, y: c_int, text: [*]const u8, len: c_int) callconv(.c) BOOL;
pub extern fn DrawTextA(dc: HDC, text: [*]const u8, len: c_int, rect: *RECT, format: UINT) callconv(.c) c_int;
pub extern fn GetTextExtentPoint32A(dc: HDC, text: [*]const u8, len: c_int, size: *SIZE) callconv(.c) BOOL;
pub extern fn SetTextColor(dc: HDC, color: COLORREF) callconv(.c) COLORREF;
pub extern fn SetBkMode(dc: HDC, mode: c_int) callconv(.c) c_int;
pub extern fn SetBkColor(dc: HDC, color: COLORREF) callconv(.c) COLORREF;
pub extern fn GetBkColor(dc: HDC) callconv(.c) COLORREF;
pub extern fn GetSysColor(index: c_int) callconv(.c) DWORD;
pub extern fn GetSysColorBrush(index: c_int) callconv(.c) ?HBRUSH;
pub extern fn CreateSolidBrush(color: COLORREF) callconv(.c) ?HBRUSH;
pub extern fn CreateFontA(height: c_int, width: c_int, escapement: c_int, orientation: c_int, weight: c_int, italic: DWORD, underline: DWORD, strike_out: DWORD, charset: DWORD, out_precision: DWORD, clip_precision: DWORD, quality: DWORD, pitch_and_family: DWORD, face_name: LPCSTR) callconv(.c) ?HFONT;
pub extern fn DeleteObject(obj: HGDIOBJ) callconv(.c) BOOL;
pub extern fn GetStockObject(what: c_int) callconv(.c) ?HGDIOBJ;
pub extern fn SelectObject(dc: HDC, obj: HGDIOBJ) callconv(.c) ?HGDIOBJ;
pub extern fn CreateBitmap(w: c_int, h: c_int, planes: UINT, bpp: UINT, bits: ?*const anyopaque) callconv(.c) ?HBITMAP;
pub extern fn CreateIcon(inst: HINSTANCE, w_: c_int, h_: c_int, planes: BYTE, bpp: BYTE, and_bits: [*]const BYTE, xor_bits: [*]const BYTE) callconv(.c) HICON;
pub extern fn DestroyIcon(icon: HICON) callconv(.c) void;
pub extern fn DrawIconEx(dc: HDC, x: c_int, y: c_int, icon: HICON, cx: c_int, cy: c_int, frame: UINT, flicker: ?HBRUSH, flags: UINT) callconv(.c) BOOL;

// ---- GDI: the drawing half ----------------------------------------------

pub extern fn CreateCompatibleDC(dc: ?HDC) callconv(.c) ?HDC;
pub extern fn DeleteDC(dc: HDC) callconv(.c) BOOL;
pub extern fn CreateCompatibleBitmap(dc: HDC, w: c_int, h: c_int) callconv(.c) ?HBITMAP;
pub extern fn GetObjectA(obj: HGDIOBJ, size: c_int, out: ?*anyopaque) callconv(.c) c_int;
pub extern fn CreatePen(style: c_int, width: c_int, color: COLORREF) callconv(.c) ?HPEN;
pub extern fn SetROP2(dc: HDC, mode: c_int) callconv(.c) c_int;
pub extern fn GetROP2(dc: HDC) callconv(.c) c_int;
pub extern fn SetStretchBltMode(dc: HDC, mode: c_int) callconv(.c) c_int;
pub extern fn SetViewportOrgEx(dc: HDC, x: c_int, y: c_int, prev: ?*POINT) callconv(.c) BOOL;
pub extern fn GetViewportOrgEx(dc: HDC, pt: *POINT) callconv(.c) BOOL;
pub extern fn MoveToEx(dc: HDC, x: c_int, y: c_int, prev: ?*POINT) callconv(.c) BOOL;
pub extern fn LineTo(dc: HDC, x: c_int, y: c_int) callconv(.c) BOOL;
pub extern fn Polyline(dc: HDC, pts: [*]const POINT, count: c_int) callconv(.c) BOOL;
pub extern fn PolyBezier(dc: HDC, pts: [*]const POINT, count: DWORD) callconv(.c) BOOL;
pub extern fn Polygon(dc: HDC, pts: [*]const POINT, count: c_int) callconv(.c) BOOL;
pub extern fn Rectangle(dc: HDC, left: c_int, top: c_int, right: c_int, bottom: c_int) callconv(.c) BOOL;
pub extern fn Ellipse(dc: HDC, left: c_int, top: c_int, right: c_int, bottom: c_int) callconv(.c) BOOL;
pub extern fn RoundRect(dc: HDC, left: c_int, top: c_int, right: c_int, bottom: c_int, ew: c_int, eh: c_int) callconv(.c) BOOL;
pub extern fn SetPixel(dc: HDC, x: c_int, y: c_int, color: COLORREF) callconv(.c) COLORREF;
pub extern fn GetPixel(dc: HDC, x: c_int, y: c_int) callconv(.c) COLORREF;
pub extern fn ExtFloodFill(dc: HDC, x: c_int, y: c_int, color: COLORREF, kind: UINT) callconv(.c) BOOL;
pub extern fn BitBlt(dst: HDC, x: c_int, y: c_int, w: c_int, h: c_int, src: ?HDC, sx: c_int, sy: c_int, rop: DWORD) callconv(.c) BOOL;
pub extern fn StretchBlt(dst: HDC, x: c_int, y: c_int, w: c_int, h: c_int, src: ?HDC, sx: c_int, sy: c_int, sw: c_int, sh: c_int, rop: DWORD) callconv(.c) BOOL;
pub extern fn PatBlt(dc: HDC, x: c_int, y: c_int, w: c_int, h: c_int, rop: DWORD) callconv(.c) BOOL;
pub extern fn InvertRect(dc: HDC, rect: *const RECT) callconv(.c) BOOL;
pub extern fn DrawFocusRect(dc: HDC, rect: *const RECT) callconv(.c) BOOL;
pub extern fn IntersectClipRect(dc: HDC, left: i32, top: i32, right: i32, bottom: i32) callconv(.c) i32;
pub extern fn GetDIBits(dc: ?HDC, bmp: HBITMAP, start: UINT, lines: UINT, bits: ?*anyopaque, info: *BITMAPINFO, usage: UINT) callconv(.c) c_int;
pub extern fn SetDIBits(dc: ?HDC, bmp: HBITMAP, start: UINT, lines: UINT, bits: *const anyopaque, info: *const BITMAPINFO, usage: UINT) callconv(.c) c_int;

// ---- constants (generated: see the note at the top) ----------------------
// >>> genconsts
// Generated by tools/zigbind/genconsts.py from include/ween32.h.
// Every value comes from the C header, which `make win32` checks
// against the real Windows SDK -- so these agree with Windows too.

pub const WM_NULL = 0x0000;
pub const WM_CREATE = 0x0001;
pub const WM_DESTROY = 0x0002;
pub const WM_MOVE = 0x0003;
pub const WM_SIZE = 0x0005;
pub const SIZE_RESTORED = 0;
pub const WM_SETFOCUS = 0x0007;
pub const WM_KILLFOCUS = 0x0008;
pub const WM_SETTEXT = 0x000C;
pub const WM_GETTEXT = 0x000D;
pub const WM_PAINT = 0x000F;
pub const WM_CLOSE = 0x0010;
pub const WM_QUIT = 0x0012;
pub const WM_ENABLE = 0x000A;
pub const WM_SETFONT = 0x0030;
pub const WM_GETFONT = 0x0031;
pub const WM_SETICON = 0x0080;
pub const WM_GETICON = 0x007F;
pub const ICON_SMALL = 0;
pub const ICON_BIG = 1;
pub const WM_NCHITTEST = 0x0084;
pub const WM_NCPAINT = 0x0085;
pub const WM_NCMOUSEMOVE = 0x00A0;
pub const WM_NCLBUTTONDOWN = 0x00A1;
pub const WM_NCLBUTTONUP = 0x00A2;
pub const WM_DRAWITEM = 0x002B;
pub const WM_INITDIALOG = 0x0110;
pub const WM_USER = 0x0400;
pub const DM_GETDEFID = (WM_USER + 0);
pub const DS_ABSALIGN = 0x0001;
pub const DM_SETDEFID = (WM_USER + 1);
pub const DC_HASDEFID = 0x534B;
pub const WM_KEYDOWN = 0x0100;
pub const WM_KEYUP = 0x0101;
pub const WM_CHAR = 0x0102;
pub const WM_COMMAND = 0x0111;
pub const WM_TIMER = 0x0113;
pub const WM_SYSCOMMAND = 0x0112;
pub const WM_INITMENU = 0x0116;
pub const WM_INITMENUPOPUP = 0x0117;
pub const WM_MENUSELECT = 0x011F;
pub const SC_KEYMENU = 0xF100;
pub const WM_NOTIFY = 0x004E;
pub const WM_VSCROLL = 0x0115;
pub const WM_HSCROLL = 0x0114;
pub const WM_MOUSEMOVE = 0x0200;
pub const WM_LBUTTONDOWN = 0x0201;
pub const WM_LBUTTONUP = 0x0202;
pub const WM_LBUTTONDBLCLK = 0x0203;
pub const WM_RBUTTONDOWN = 0x0204;
pub const WM_RBUTTONUP = 0x0205;
pub const WM_CONTEXTMENU = 0x007B;
pub const MK_LBUTTON = 0x0001;
pub const MK_RBUTTON = 0x0002;
pub const MK_SHIFT = 0x0004;
pub const MK_CONTROL = 0x0008;
pub const CS_DBLCLKS = 0x0008;
pub const WM_MOUSEWHEEL = 0x020A;
pub const WM_MOUSELEAVE = 0x02A3;
pub const WM_CHANGEUISTATE = 0x0127;
pub const WM_UPDATEUISTATE = 0x0128;
pub const WM_QUERYUISTATE = 0x0129;
pub const UIS_SET = 1;
pub const UIS_CLEAR = 2;
pub const UIS_INITIALIZE = 3;
pub const UISF_HIDEFOCUS = 0x1;
pub const UISF_HIDEACCEL = 0x2;
pub const UISF_ACTIVE = 0x4;
pub const TME_LEAVE = 0x00000002;
pub const TME_CANCEL = 0x80000000;
pub const WM_SETCURSOR = 0x0020;
pub const WHEEL_DELTA = 120;
pub const WS_OVERLAPPED = 0x00000000;
pub const WS_POPUP = 0x80000000;
pub const WS_CHILD = 0x40000000;
pub const WS_VISIBLE = 0x10000000;
pub const WS_CAPTION = 0x00C00000;
pub const WS_BORDER = 0x00800000;
pub const WS_DLGFRAME = 0x00400000;
pub const WS_SYSMENU = 0x00080000;
pub const WS_DISABLED = 0x08000000;
pub const WS_THICKFRAME = 0x00040000;
pub const WS_SIZEBOX = WS_THICKFRAME;
pub const WS_MINIMIZEBOX = 0x00020000;
pub const WS_MAXIMIZEBOX = 0x00010000;
pub const WS_VSCROLL = 0x00200000;
pub const WS_HSCROLL = 0x00100000;
pub const WS_GROUP = 0x00020000;
pub const WS_TABSTOP = 0x00010000;
pub const WS_CLIPSIBLINGS = 0x04000000;
pub const WS_CLIPCHILDREN = 0x02000000;
pub const WS_OVERLAPPEDWINDOW = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
pub const WS_EX_DLGMODALFRAME = 0x00000001;
pub const WS_EX_TOOLWINDOW = 0x00000080;
pub const WS_EX_CLIENTEDGE = 0x00000200;
pub const WS_EX_CONTROLPARENT = 0x00010000;
pub const WS_EX_CONTEXTHELP = 0x00000400;
pub const WS_EX_NOACTIVATE = 0x08000000;
pub const WS_EX_STATICEDGE = 0x00020000;
/// A window that takes files dropped on it. Nothing here acts on it yet;
/// a program that accepts files still says so when it creates its window.
pub const WS_EX_ACCEPTFILES = 0x00000010;
pub const ES_LEFT = 0x0000;
pub const ES_CENTER = 0x0001;
pub const ES_RIGHT = 0x0002;
pub const ES_MULTILINE = 0x0004;
pub const ES_AUTOVSCROLL = 0x0040;
pub const ES_NOHIDESEL = 0x0100;
pub const ES_AUTOHSCROLL = 0x0080;
pub const ES_READONLY = 0x0800;
pub const LBS_NOTIFY = 0x0001;
pub const LBS_SORT = 0x0002;
pub const LB_ADDSTRING = 0x0180;
pub const LB_INSERTSTRING = 0x0181;
pub const LB_DELETESTRING = 0x0182;
pub const LB_RESETCONTENT = 0x0184;
pub const LB_SETCURSEL = 0x0186;
pub const LB_GETCURSEL = 0x0188;
pub const LB_GETTEXT = 0x0189;
pub const LB_GETCOUNT = 0x018B;
pub const LB_GETTOPINDEX = 0x018E;
pub const LB_SETTOPINDEX = 0x0197;
pub const LBN_SELCHANGE = 1;
pub const LBN_DBLCLK = 2;
pub const CBS_SIMPLE = 0x0001;
pub const CBS_DROPDOWN = 0x0002;
pub const CBS_DROPDOWNLIST = 0x0003;
pub const CB_ADDSTRING = 0x0143;
pub const CB_DELETESTRING = 0x0144;
pub const CB_GETCOUNT = 0x0146;
pub const CB_GETCURSEL = 0x0147;
pub const CB_GETLBTEXT = 0x0148;
pub const CB_INSERTSTRING = 0x014A;
pub const CB_RESETCONTENT = 0x014B;
pub const CBEM_INSERTITEMA = (WM_USER + 1);
pub const CB_SETITEMHEIGHT = 0x0153;
pub const CB_GETITEMHEIGHT = 0x0154;
pub const CBEM_SETIMAGELIST = (WM_USER + 2);
pub const CBEIF_TEXT = 0x0001;
pub const CBEIF_IMAGE = 0x0002;
pub const CBEIF_SELECTEDIMAGE = 0x0004;
pub const CBEIF_INDENT = 0x0010;
pub const CB_SETCURSEL = 0x014E;
pub const CB_SHOWDROPDOWN = 0x014F;
pub const CB_GETDROPPEDSTATE = 0x0157;
pub const CBN_SELCHANGE = 1;
pub const CBN_EDITCHANGE = 5;
pub const CBN_DROPDOWN = 7;
pub const CBEN_FIRST = (0 - 800);
pub const CBEN_ENDEDITA = (CBEN_FIRST - 5);
pub const CBENF_KILLFOCUS = 1;
pub const CBENF_RETURN = 2;
pub const CBENF_ESCAPE = 3;
pub const CBENF_DROPDOWN = 4;
pub const CBEMAXSTRLEN = 260;
pub const CBEM_GETEDITCONTROL = (WM_USER + 7);
pub const EN_SETFOCUS = 0x0100;
pub const EN_KILLFOCUS = 0x0200;
pub const EN_CHANGE = 0x0300;
pub const EN_UPDATE = 0x0400;
pub const EN_ENTER = 0x1300;
pub const EN_ESCAPE = 0x1301;
pub const EM_GETMODIFY = 0x00B8;
pub const EM_SETMODIFY = 0x00B9;
pub const EM_SETSEL = 0x00B1;
pub const EM_GETSEL = 0x00B0;
pub const EM_SETMARGINS = 0x00D3;
pub const EC_LEFTMARGIN = 0x0001;
pub const EC_RIGHTMARGIN = 0x0002;

// The rich edit. Its class comes from riched20.dll on Windows and from this
// library everywhere else, and the name is the ANSI one either way: WordPad
// asks for RICHEDIT_CLASS, which is the W name only in a UNICODE build.
pub const RICHEDIT_CLASSA = "RichEdit20A";
pub const RICHEDIT_CLASS10A = "RICHEDIT";
pub const EM_CANPASTE = (WM_USER + 50);
pub const EM_EXGETSEL = (WM_USER + 52);
pub const EM_EXLIMITTEXT = (WM_USER + 53);
pub const EM_EXSETSEL = (WM_USER + 55);
pub const EM_GETEVENTMASK = (WM_USER + 59);
pub const EM_GETSELTEXT = (WM_USER + 62);
pub const EM_SETEVENTMASK = (WM_USER + 69);
pub const EM_GETTEXTRANGE = (WM_USER + 75);
pub const EM_SETUNDOLIMIT = (WM_USER + 82);
pub const ENM_NONE = 0x00000000;
pub const ENM_CHANGE = 0x00000001;
pub const ENM_UPDATE = 0x00000002;
pub const ENM_SCROLL = 0x00000004;
pub const ENM_SELCHANGE = 0x00080000;
pub const EN_SELCHANGE = 0x0702;
pub const CHARRANGE = extern struct {
    cpMin: LONG = 0,
    cpMax: LONG = 0,
};
pub const TEXTRANGEA = extern struct {
    chrg: CHARRANGE = .{},
    lpstrText: ?LPSTR = null,
};
pub const LF_FACESIZE = 32;
pub const EM_GETCHARFORMAT = (WM_USER + 58);
pub const EM_SETCHARFORMAT = (WM_USER + 68);
pub const EM_POSFROMCHAR = 0x00D6;
pub const EM_CHARFROMPOS = 0x00D7;
pub const SCF_DEFAULT = 0x0000;
pub const SCF_SELECTION = 0x0001;
pub const SCF_WORD = 0x0002;
pub const SCF_ALL = 0x0004;
pub const CFM_BOLD = 0x00000001;
pub const CFM_ITALIC = 0x00000002;
pub const CFM_UNDERLINE = 0x00000004;
pub const CFM_STRIKEOUT = 0x00000008;
pub const CFM_PROTECTED = 0x00000010;
pub const CFM_LINK = 0x00000020;
pub const CFM_SIZE = 0x80000000;
pub const CFM_COLOR = 0x40000000;
pub const CFM_FACE = 0x20000000;
pub const CFM_OFFSET = 0x10000000;
pub const CFM_CHARSET = 0x08000000;
pub const CFE_BOLD = 0x00000001;
pub const CFE_ITALIC = 0x00000002;
pub const CFE_UNDERLINE = 0x00000004;
pub const CFE_STRIKEOUT = 0x00000008;
pub const CFE_PROTECTED = 0x00000010;
pub const CFE_LINK = 0x00000020;
pub const CFE_AUTOCOLOR = 0x40000000;
pub const SEL_EMPTY = 0x0000;
pub const SEL_TEXT = 0x0001;
pub const SEL_OBJECT = 0x0002;
pub const SEL_MULTICHAR = 0x0004;
pub const SEL_MULTIOBJECT = 0x0008;
pub const CHARFORMATA = extern struct {
    cbSize: UINT = @sizeOf(CHARFORMATA),
    dwMask: DWORD = 0,
    dwEffects: DWORD = 0,
    yHeight: LONG = 0,
    yOffset: LONG = 0,
    crTextColor: COLORREF = 0,
    bCharSet: BYTE = 0,
    bPitchAndFamily: BYTE = 0,
    szFaceName: [LF_FACESIZE]u8 = std.mem.zeroes([LF_FACESIZE]u8),
};
pub const POINTL = extern struct {
    x: LONG = 0,
    y: LONG = 0,
};
pub const EM_GETPARAFORMAT = (WM_USER + 61);
pub const EM_SETPARAFORMAT = (WM_USER + 71);
pub const PFM_STARTINDENT = 0x00000001;
pub const PFM_RIGHTINDENT = 0x00000002;
pub const PFM_OFFSET = 0x00000004;
pub const PFM_ALIGNMENT = 0x00000008;
pub const PFM_TABSTOPS = 0x00000010;
pub const PFM_NUMBERING = 0x00000020;
pub const PFM_OFFSETINDENT = 0x80000000;
pub const PFA_LEFT = 1;
pub const PFA_RIGHT = 2;
pub const PFA_CENTER = 3;
pub const MAX_TAB_STOPS = 32;
pub const EM_SETTARGETDEVICE = (WM_USER + 72);
pub const EM_STREAMIN = (WM_USER + 73);
pub const EM_STREAMOUT = (WM_USER + 74);
pub const SF_TEXT = 0x0001;
pub const SF_RTF = 0x0002;
pub const SFF_SELECTION = 0x8000;
pub const EDITSTREAMCALLBACK = *const fn (usize, [*]u8, LONG, *LONG) callconv(.c) DWORD;
// Packed to four the way <richedit.h> is, which puts the callback at twelve
// rather than sixteen: an eight-byte pointer straight after a DWORD.
pub const EDITSTREAM = extern struct {
    dwCookie: usize align(4) = 0,
    dwError: DWORD = 0,
    pfnCallback: ?EDITSTREAMCALLBACK align(4) = null,
};
pub const PARAFORMAT = extern struct {
    cbSize: UINT = @sizeOf(PARAFORMAT),
    dwMask: DWORD = 0,
    wNumbering: WORD = 0,
    wReserved: WORD = 0,
    dxStartIndent: LONG = 0,
    dxRightIndent: LONG = 0,
    dxOffset: LONG = 0,
    wAlignment: WORD = 0,
    cTabCount: SHORT = 0,
    rgxTabs: [MAX_TAB_STOPS]LONG = std.mem.zeroes([MAX_TAB_STOPS]LONG),
};
pub const TBS_AUTOTICKS = 0x0001;
pub const TBS_VERT = 0x0002;
pub const TBS_HORZ = 0x0000;
pub const TBS_BOTH = 0x0008;
pub const TBS_NOTICKS = 0x0010;
pub const TBM_GETPOS = (WM_USER);
pub const TBM_SETPOS = (WM_USER + 5);
pub const TBM_SETRANGE = (WM_USER + 6);
pub const TBM_SETRANGEMIN = (WM_USER + 7);
pub const TBM_SETRANGEMAX = (WM_USER + 8);
pub const TBM_SETTICFREQ = (WM_USER + 20);
pub const TVS_HASBUTTONS = 0x0001;
pub const TVS_HASLINES = 0x0002;
pub const TVS_LINESATROOT = 0x0004;
pub const TVS_SHOWSELALWAYS = 0x0020;
pub const TVIF_TEXT = 0x0001;
pub const TVIF_IMAGE = 0x0002;
pub const TVIF_HANDLE = 0x0010;
pub const TVIF_SELECTEDIMAGE = 0x0020;
pub const TVIF_CHILDREN = 0x0040;
pub const TVSIL_NORMAL = 0;
pub const TVE_COLLAPSE = 0x0001;
pub const TVE_EXPAND = 0x0002;
pub const TV_FIRST = 0x1100;
pub const TVM_INSERTITEMA = (TV_FIRST + 0);
pub const TVM_SETIMAGELIST = (TV_FIRST + 9);
pub const TVM_DELETEITEM = (TV_FIRST + 1);
pub const TVM_GETNEXTITEM = (TV_FIRST + 10);
pub const TVM_GETITEMA = (TV_FIRST + 12);
pub const TVGN_ROOT = 0x0000;
pub const TVGN_NEXT = 0x0001;
pub const TVGN_PARENT = 0x0003;
pub const TVGN_CHILD = 0x0004;
pub const TVGN_CARET = 0x0009;
pub const TVM_EXPAND = (TV_FIRST + 2);
pub const TVM_SELECTITEM = (TV_FIRST + 11);
pub const TVM_HITTEST = (TV_FIRST + 17);
pub const TVHT_ONITEMICON = 0x0002;
pub const TVHT_ONITEMLABEL = 0x0004;
pub const TVHT_ONITEMBUTTON = 0x0010;
pub const TVHT_ONITEMRIGHT = 0x0020;
pub const TVHT_ONITEMSTATEICON = 0x0040;
pub const TVHT_ONITEM = (TVHT_ONITEMICON | TVHT_ONITEMLABEL | TVHT_ONITEMSTATEICON);
pub const LVS_ICON = 0x0000;
pub const LVS_REPORT = 0x0001;
pub const LVS_SMALLICON = 0x0002;
pub const LVS_LIST = 0x0003;
pub const LVS_TYPEMASK = 0x0003;
pub const LVS_SINGLESEL = 0x0004;
pub const LVS_SHOWSELALWAYS = 0x0008;
pub const LVS_EDITLABELS = 0x0200;
pub const LVIF_TEXT = 0x0001;
pub const LVIF_IMAGE = 0x0002;
pub const LVIF_STATE = 0x0008;
pub const LVSIL_NORMAL = 0;
pub const LVSIL_SMALL = 1;
pub const LVIS_FOCUSED = 0x0001;
pub const LVIS_CUT = 0x0004;
pub const LVIS_SELECTED = 0x0002;
pub const LVCF_FMT = 0x0001;
pub const LVCF_WIDTH = 0x0002;
pub const LVCFMT_LEFT = 0x0000;
pub const LVCFMT_RIGHT = 0x0001;
pub const HDM_FIRST = 0x1200;
pub const HDM_GETITEMCOUNT = (HDM_FIRST + 0);
pub const HDM_GETITEMA = (HDM_FIRST + 3);
pub const HDM_SETITEMA = (HDM_FIRST + 4);
pub const HDI_WIDTH = 0x0001;
pub const HDI_HEIGHT = 0x0001;
pub const HDI_TEXT = 0x0002;
pub const HDI_FORMAT = 0x0004;
pub const HDI_LPARAM = 0x0008;
pub const HDI_BITMAP = 0x0010;
pub const HDI_IMAGE = 0x0020;
pub const HDI_ORDER = 0x0080;
pub const HDF_STRING = 0x4000;
pub const HDF_LEFT = 0x0000;
pub const HDF_RIGHT = 0x0001;
pub const HDF_CENTER = 0x0002;
pub const HDF_SORTDOWN = 0x0200;
pub const HDF_SORTUP = 0x0400;
pub const LVCF_TEXT = 0x0004;
pub const LVM_FIRST = 0x1000;
pub const LVM_INSERTCOLUMNA = (LVM_FIRST + 27);
pub const LVM_INSERTITEMA = (LVM_FIRST + 7);
pub const LVM_SETIMAGELIST = (LVM_FIRST + 3);
pub const LVM_DELETEALLITEMS = (LVM_FIRST + 9);
pub const LVM_GETITEMCOUNT = (LVM_FIRST + 4);
pub const LVM_GETSELECTEDCOUNT = (LVM_FIRST + 50);
pub const LVM_EDITLABELA = (LVM_FIRST + 23);
pub const LVM_GETEDITCONTROL = (LVM_FIRST + 24);
pub const LVM_GETNEXTITEM = (LVM_FIRST + 12);
pub const LVM_SETCOLUMNA = (LVM_FIRST + 26);
pub const LVM_GETHEADER = (LVM_FIRST + 31);
pub const LVM_GETCOLUMNWIDTH = (LVM_FIRST + 29);
pub const LVM_SETCOLUMNWIDTH = (LVM_FIRST + 30);
pub const LVSCW_AUTOSIZE = (-1);
pub const LVSCW_AUTOSIZE_USEHEADER = (-2);
pub const LVM_ENSUREVISIBLE = (LVM_FIRST + 19);
pub const LVNI_SELECTED = 0x0002;
pub const LVNI_FOCUSED = 0x0001;
pub const LVM_HITTEST = (LVM_FIRST + 18);
pub const LVM_GETITEMRECT = (LVM_FIRST + 14);
pub const LVIR_BOUNDS = 0;
pub const LVIR_ICON = 1;
pub const LVIR_LABEL = 2;
pub const LVIR_SELECTBOUNDS = 3;
pub const LVHT_NOWHERE = 0x0001;
pub const LVHT_ONITEMICON = 0x0002;
pub const LVHT_ONITEMLABEL = 0x0004;
pub const LVHT_ONITEMSTATEICON = 0x0008;
pub const LVHT_ONITEM = (LVHT_ONITEMICON | LVHT_ONITEMLABEL | LVHT_ONITEMSTATEICON);
pub const LVM_SETITEMTEXTA = (LVM_FIRST + 46);
pub const LVM_SETITEMSTATE = (LVM_FIRST + 43);
pub const TCS_MULTILINE = 0x0200;
pub const TCM_FIRST = 0x1300;
pub const TCM_INSERTITEMA = (TCM_FIRST + 7);
pub const TCM_SETCURSEL = (TCM_FIRST + 12);
pub const TCM_GETCURSEL = (TCM_FIRST + 11);
pub const TCM_ADJUSTRECT = (TCM_FIRST + 40);
pub const TCIF_TEXT = 0x0001;
pub const TCIF_IMAGE = 0x0002;
pub const TCN_SELCHANGE = (0 - 551);
pub const TVN_SELCHANGEDA = (0 - 402);
pub const TVN_ITEMEXPANDINGA = (0 - 405);
pub const TVN_ITEMEXPANDEDA = (0 - 406);
pub const NM_DBLCLK = (0 - 3);
pub const LVN_ITEMCHANGED = (0 - 101);
pub const LVN_COLUMNCLICK = (0 - 108);
pub const LVN_BEGINLABELEDITA = (0 - 105);
pub const LVN_ENDLABELEDITA = (0 - 106);
pub const SB_LINEUP = 0;
pub const SB_LINELEFT = 0;
pub const SB_LINEDOWN = 1;
pub const SB_LINERIGHT = 1;
pub const SB_PAGEUP = 2;
pub const SB_PAGELEFT = 2;
pub const SB_PAGEDOWN = 3;
pub const SB_PAGERIGHT = 3;
pub const SB_THUMBPOSITION = 4;
pub const SB_THUMBTRACK = 5;
pub const SB_ENDSCROLL = 8;
pub const SBARS_SIZEGRIP = 0x0100;
pub const SB_SETTEXTA = (WM_USER + 1);
pub const SB_GETTEXTA = (WM_USER + 2);
pub const SB_SETICON = (WM_USER + 15);
pub const SB_SETPARTS = (WM_USER + 4);
pub const SB_GETRECT = (WM_USER + 10);
pub const SBT_NOBORDERS = 0x0100;
pub const SBT_POPOUT = 0x0200;
pub const SBT_RTLREADING = 0x0400;
pub const SBT_OWNERDRAW = 0x1000;
pub const SB_GETPARTS = (WM_USER + 6);
pub const SB_SIMPLE = (WM_USER + 9);
pub const PBS_SMOOTH = 0x01;
pub const PBS_VERTICAL = 0x04;
pub const PBM_SETRANGE = (WM_USER + 1);
pub const PBM_SETPOS = (WM_USER + 2);
pub const PBM_DELTAPOS = (WM_USER + 3);
pub const PBM_SETSTEP = (WM_USER + 4);
pub const PBM_STEPIT = (WM_USER + 5);
pub const PBM_SETRANGE32 = (WM_USER + 6);
pub const SBS_HORZ = 0x0000;
pub const SBS_VERT = 0x0001;
pub const DS_3DLOOK = 0x0004;
pub const DS_SETFONT = 0x40;
pub const DS_MODALFRAME = 0x80;
pub const DS_CENTER = 0x0800;
pub const DS_CONTEXTHELP = 0x2000;
pub const IDOK = 1;
pub const IDCANCEL = 2;
pub const BS_PUSHBUTTON = 0x00000000;
pub const BS_DEFPUSHBUTTON = 0x00000001;
pub const BS_CHECKBOX = 0x00000002;
pub const BS_AUTOCHECKBOX = 0x00000003;
pub const BS_RADIOBUTTON = 0x00000004;
pub const BS_3STATE = 0x00000005;
pub const BS_AUTO3STATE = 0x00000006;
pub const BS_GROUPBOX = 0x00000007;
pub const BS_AUTORADIOBUTTON = 0x00000009;
pub const BS_OWNERDRAW = 0x0000000B;
pub const BS_TYPEMASK = 0x0000000F;
pub const BS_LEFTTEXT = 0x00000020;
pub const SS_LEFT = 0x00000000;
pub const SS_CENTER = 0x00000001;
pub const SS_RIGHT = 0x00000002;
pub const SS_ICON = 0x00000003;
pub const SS_BLACKRECT = 0x00000004;
pub const SS_GRAYRECT = 0x00000005;
pub const SS_WHITERECT = 0x00000006;
pub const SS_BLACKFRAME = 0x00000007;
pub const SS_GRAYFRAME = 0x00000008;
pub const SS_WHITEFRAME = 0x00000009;
pub const SS_SIMPLE = 0x0000000B;
pub const SS_LEFTNOWORDWRAP = 0x0000000C;
pub const SS_OWNERDRAW = 0x0000000D;
pub const SS_BITMAP = 0x0000000E;
pub const SS_ETCHEDHORZ = 0x00000010;
pub const SS_ETCHEDVERT = 0x00000011;
pub const SS_ETCHEDFRAME = 0x00000012;
pub const SS_TYPEMASK = 0x0000001F;
pub const SS_NOPREFIX = 0x00000080;
pub const SS_CENTERIMAGE = 0x00000200;
pub const SS_SUNKEN = 0x00001000;
pub const STM_SETIMAGE = 0x0172;
pub const STM_GETIMAGE = 0x0173;
pub const BN_CLICKED = 0;
pub const BM_GETCHECK = 0x00F0;
pub const BM_CLICK = 0x00F5;
pub const BM_SETCHECK = 0x00F1;
pub const BM_GETSTATE = 0x00F2;
pub const BM_SETSTATE = 0x00F3;
pub const BST_UNCHECKED = 0x0000;
pub const BST_CHECKED = 0x0001;
pub const BST_INDETERMINATE = 0x0002;
pub const BST_PUSHED = 0x0004;
pub const ODT_BUTTON = 4;
pub const ODA_DRAWENTIRE = 0x0001;
pub const ODA_SELECT = 0x0002;
pub const ODS_SELECTED = 0x0001;
pub const HTNOWHERE = 0;
pub const HTCLIENT = 1;
pub const HTCAPTION = 2;
pub const HTMENU = 5;
pub const HTLEFT = 10;
pub const HTRIGHT = 11;
pub const HTTOP = 12;
pub const HTTOPLEFT = 13;
pub const HTTOPRIGHT = 14;
pub const HTBOTTOM = 15;
pub const HTBOTTOMLEFT = 16;
pub const HTBOTTOMRIGHT = 17;
pub const HTCLOSE = 20;
pub const HTHELP = 21;
pub const HTMINBUTTON = 8;
pub const HTMAXBUTTON = 9;
pub const SC_SIZE = 0xF000;
pub const SC_MOVE = 0xF010;
pub const SC_MINIMIZE = 0xF020;
pub const SC_MAXIMIZE = 0xF030;
pub const SC_CLOSE = 0xF060;
pub const SC_CONTEXTHELP = 0xF180;
pub const SC_RESTORE = 0xF120;
pub const SW_HIDE = 0;
pub const SW_SHOWNORMAL = 1;
pub const SW_SHOWNOACTIVATE = 4;
pub const SW_SHOW = 5;
pub const SW_SHOWNA = 8;
pub const VK_BACK = 0x08;
pub const VK_TAB = 0x09;
pub const VK_RETURN = 0x0D;
pub const VK_ESCAPE = 0x1B;
pub const VK_SHIFT = 0x10;
pub const VK_CONTROL = 0x11;
pub const VK_MENU = 0x12;
pub const VK_PRIOR = 0x21;
pub const VK_NEXT = 0x22;
pub const VK_F1 = 0x70;
pub const VK_F2 = 0x71;
pub const VK_F3 = 0x72;
pub const VK_F4 = 0x73;
pub const VK_F5 = 0x74;
pub const VK_F6 = 0x75;
pub const VK_F10 = 0x79;
pub const VK_SPACE = 0x20;
pub const VK_END = 0x23;
pub const VK_HOME = 0x24;
pub const VK_LEFT = 0x25;
pub const VK_UP = 0x26;
pub const VK_RIGHT = 0x27;
pub const VK_DOWN = 0x28;
pub const VK_DELETE = 0x2E;
pub const COLOR_ACTIVECAPTION = 2;
pub const COLOR_WINDOWFRAME = 6;
pub const COLOR_APPWORKSPACE = 12;
pub const COLOR_WINDOW = 5;
pub const COLOR_MENU = 4;
pub const COLOR_MENUTEXT = 7;
pub const COLOR_WINDOWTEXT = 8;
pub const COLOR_CAPTIONTEXT = 9;
pub const COLOR_BTNFACE = 15;
pub const COLOR_BTNSHADOW = 16;
pub const COLOR_HIGHLIGHT = 13;
pub const COLOR_HIGHLIGHTTEXT = 14;
pub const COLOR_GRAYTEXT = 17;
pub const COLOR_BTNTEXT = 18;
pub const COLOR_BTNHIGHLIGHT = 20;
pub const COLOR_3DDKSHADOW = 21;
pub const COLOR_3DLIGHT = 22;
pub const COLOR_GRADIENTACTIVECAPTION = 27;
pub const BDR_RAISEDOUTER = 0x0001;
pub const BDR_SUNKENOUTER = 0x0002;
pub const BDR_RAISEDINNER = 0x0004;
pub const BDR_SUNKENINNER = 0x0008;
pub const EDGE_RAISED = (BDR_RAISEDOUTER | BDR_RAISEDINNER);
pub const EDGE_SUNKEN = (BDR_SUNKENOUTER | BDR_SUNKENINNER);
pub const BDR_OUTER = (BDR_RAISEDOUTER | BDR_SUNKENOUTER);
pub const BDR_INNER = (BDR_RAISEDINNER | BDR_SUNKENINNER);
pub const EDGE_ETCHED = (BDR_SUNKENOUTER | BDR_RAISEDINNER);
pub const EDGE_BUMP = (BDR_RAISEDOUTER | BDR_SUNKENINNER);
pub const BF_LEFT = 0x0001;
pub const BF_TOP = 0x0002;
pub const BF_RIGHT = 0x0004;
pub const BF_BOTTOM = 0x0008;
pub const BF_TOPLEFT = (BF_TOP | BF_LEFT);
pub const BF_TOPRIGHT = (BF_TOP | BF_RIGHT);
pub const BF_BOTTOMLEFT = (BF_BOTTOM | BF_LEFT);
pub const BF_BOTTOMRIGHT = (BF_BOTTOM | BF_RIGHT);
pub const BF_RECT = (BF_LEFT | BF_TOP | BF_RIGHT | BF_BOTTOM);
pub const BF_MIDDLE = 0x0800;
pub const BF_SOFT = 0x1000;
pub const BF_ADJUST = 0x2000;
pub const BF_FLAT = 0x4000;
pub const BF_MONO = 0x8000;
pub const DFC_CAPTION = 1;
pub const DFC_BUTTON = 4;
pub const DFCS_CAPTIONCLOSE = 0x0000;
pub const DFCS_CAPTIONMIN = 0x0001;
pub const DFCS_CAPTIONMAX = 0x0002;
pub const DFCS_CAPTIONRESTORE = 0x0003;
pub const DFCS_CAPTIONHELP = 0x0004;
pub const DFCS_PUSHED = 0x0200;
pub const DFCS_BUTTONCHECK = 0x0000;
pub const DFCS_BUTTONRADIOIMAGE = 0x0001;
pub const DFCS_BUTTONRADIOMASK = 0x0002;
pub const DFCS_BUTTONRADIO = 0x0004;
pub const DFCS_BUTTON3STATE = 0x0008;
pub const DFCS_BUTTONPUSH = 0x0010;
pub const DFCS_INACTIVE = 0x0100;
pub const DFCS_CHECKED = 0x0400;
pub const DFCS_FLAT = 0x4000;
pub const DFCS_MONO = 0x8000;
pub const DT_LEFT = 0x00000000;
pub const DT_CENTER = 0x00000001;
pub const DT_RIGHT = 0x00000002;
pub const DT_VCENTER = 0x00000004;
pub const DT_SINGLELINE = 0x00000020;
pub const DT_NOCLIP = 0x00000100;
pub const DT_NOPREFIX = 0x00000800;
pub const DT_HIDEPREFIX = 0x00100000;
pub const TRANSPARENT = 1;
pub const OPAQUE = 2;
pub const WHITE_BRUSH = 0;
pub const LTGRAY_BRUSH = 1;
pub const GRAY_BRUSH = 2;
pub const DKGRAY_BRUSH = 3;
pub const BLACK_BRUSH = 4;
pub const SYSTEM_FONT = 13;
pub const DEFAULT_GUI_FONT = 17;
pub const FW_NORMAL = 400;
pub const FW_BOLD = 700;
pub const ANSI_CHARSET = 0;
pub const DEFAULT_CHARSET = 1;
pub const SYMBOL_CHARSET = 2;
pub const OUT_DEFAULT_PRECIS = 0;
pub const CLIP_DEFAULT_PRECIS = 0;
pub const DEFAULT_QUALITY = 0;
pub const DEFAULT_PITCH = 0;
pub const FIXED_PITCH = 1;
pub const FF_DONTCARE = (0 << 4);
pub const SB_HORZ = 0;
pub const SB_VERT = 1;
pub const SB_CTL = 2;
pub const SB_BOTH = 3;
pub const SIF_RANGE = 0x0001;
pub const SIF_PAGE = 0x0002;
pub const SIF_POS = 0x0004;
pub const SIF_DISABLENOSCROLL = 0x0008;
pub const SIF_TRACKPOS = 0x0010;
pub const SIF_ALL = (SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS);
pub const ESB_ENABLE_BOTH = 0x0000;
pub const ESB_DISABLE_BOTH = 0x0003;
pub const NULL_BRUSH = 5;
pub const HOLLOW_BRUSH = NULL_BRUSH;
pub const WHITE_PEN = 6;
pub const BLACK_PEN = 7;
pub const NULL_PEN = 8;
pub const PS_SOLID = 0;
pub const PS_DASH = 1;
pub const PS_DOT = 2;
pub const PS_DASHDOT = 3;
pub const PS_DASHDOTDOT = 4;
pub const PS_NULL = 5;
pub const PS_INSIDEFRAME = 6;
pub const R2_BLACK = 1;
pub const R2_NOTMERGEPEN = 2;
pub const R2_MASKNOTPEN = 3;
pub const R2_NOTCOPYPEN = 4;
pub const R2_MASKPENNOT = 5;
pub const R2_NOT = 6;
pub const R2_XORPEN = 7;
pub const R2_NOTMASKPEN = 8;
pub const R2_MASKPEN = 9;
pub const R2_NOTXORPEN = 10;
pub const R2_NOP = 11;
pub const R2_MERGENOTPEN = 12;
pub const R2_COPYPEN = 13;
pub const R2_MERGEPENNOT = 14;
pub const R2_MERGEPEN = 15;
pub const R2_WHITE = 16;
pub const BLACKNESS = 0x00000042;
pub const NOTSRCERASE = 0x001100A6;
pub const NOTSRCCOPY = 0x00330008;
pub const SRCERASE = 0x00440328;
pub const DSTINVERT = 0x00550009;
pub const PATINVERT = 0x005A0049;
pub const SRCINVERT = 0x00660046;
pub const SRCAND = 0x008800C6;
pub const MERGEPAINT = 0x00BB0226;
pub const MERGECOPY = 0x00C000CA;
pub const SRCCOPY = 0x00CC0020;
pub const SRCPAINT = 0x00EE0086;
pub const PATCOPY = 0x00F00021;
pub const PATPAINT = 0x00FB0A09;
pub const WHITENESS = 0x00FF0062;
pub const BLACKONWHITE = 1;
pub const WHITEONBLACK = 2;
pub const COLORONCOLOR = 3;
pub const HALFTONE = 4;
pub const STRETCH_ANDSCANS = BLACKONWHITE;
pub const STRETCH_ORSCANS = WHITEONBLACK;
pub const STRETCH_DELETESCANS = COLORONCOLOR;
pub const STRETCH_HALFTONE = HALFTONE;
pub const FLOODFILLBORDER = 0;
pub const FLOODFILLSURFACE = 1;
pub const OBJ_PEN = 1;
pub const OBJ_BRUSH = 2;
pub const OBJ_DC = 3;
pub const OBJ_BITMAP = 7;
pub const OBJ_FONT = 6;
pub const BI_RGB = 0;
pub const DIB_RGB_COLORS = 0;
pub const OFN_READONLY = 0x00000001;
pub const OFN_OVERWRITEPROMPT = 0x00000002;
pub const OFN_HIDEREADONLY = 0x00000004;
pub const OFN_PATHMUSTEXIST = 0x00000800;
pub const OFN_FILEMUSTEXIST = 0x00001000;
pub const OFN_CREATEPROMPT = 0x00002000;
pub const OFN_EXPLORER = 0x00080000;
pub const CC_RGBINIT = 0x00000001;
pub const CC_FULLOPEN = 0x00000002;
pub const CC_PREVENTFULLOPEN = 0x00000004;
pub const CC_ANYCOLOR = 0x00000100;
pub const CC_ENABLEHOOK = 0x00000010;
pub const WEEN32_HAS_DISABLED = 1;
pub const WEEN32_HAS_CHECKBOX = 1;
pub const WEEN32_HAS_RADIO = 1;
pub const WEEN32_HAS_GROUPBOX = 1;
pub const WEEN32_HAS_EDIT = 1;
pub const WEEN32_HAS_SCROLLBAR = 1;
pub const WEEN32_HAS_LISTBOX = 1;
pub const WEEN32_HAS_COMBOBOX = 1;
pub const WEEN32_HAS_PROGRESS = 1;
pub const WEEN32_HAS_STATUSBAR = 1;
pub const WEEN32_HAS_TABS = 1;
pub const WEEN32_HAS_TREEVIEW = 1;
pub const WEEN32_HAS_LISTVIEW = 1;
pub const WEEN32_HAS_TRACKBAR = 1;
pub const TOOLBARCLASSNAMEA = "ToolbarWindow32";
pub const TTN_FIRST = (0 - 520);
pub const TTN_GETDISPINFOA = (TTN_FIRST - 0);
pub const TBSTYLE_TOOLTIPS = 0x0100;
pub const TB_GETTOOLTIPS = (WM_USER + 35);
pub const STATUSCLASSNAMEA = "msctls_statusbar32";

/// One button on a toolbar, as TB_ADDBUTTONS takes them.
pub const TBBUTTON = extern struct {
    iBitmap: c_int = 0,
    idCommand: c_int = 0,
    fsState: u8 = 0,
    fsStyle: u8 = 0,
    /// Six on a 64-bit build, which is what this is; win32 declares [2] on a
    /// 32-bit one. Every offset and the struct's size come out the same
    /// either way, so only a comparison of the field's own width finds it --
    /// which is what tools/zigbind/genstructs.py does.
    bReserved: [6]u8 = .{ 0, 0, 0, 0, 0, 0 },
    dwData: usize = 0,
    iString: isize = 0,
};

pub const TBSTYLE_BUTTON = 0x0000;
pub const TBSTYLE_SEP = 0x0001;
pub const TBSTYLE_CHECK = 0x0002;
pub const TBSTYLE_DROPDOWN = 0x0008;
pub const BTNS_BUTTON = 0x0000;
pub const BTNS_SEP = 0x0001;
pub const BTNS_CHECK = 0x0002;
pub const BTNS_GROUP = 0x0004;
pub const BTNS_DROPDOWN = 0x0008;
pub const BTNS_AUTOSIZE = 0x0010;
pub const BTNS_NOPREFIX = 0x0020;
pub const BTNS_SHOWTEXT = 0x0040;
pub const BTNS_WHOLEDROPDOWN = 0x0080;
pub const TBSTYLE_FLAT = 0x0800;
pub const TBSTYLE_LIST = 0x1000;
pub const TBSTATE_CHECKED = 0x01;
pub const TBSTATE_PRESSED = 0x02;
pub const TBSTATE_ENABLED = 0x04;
pub const TBSTATE_HIDDEN = 0x08;
pub const TB_ENABLEBUTTON = (WM_USER + 1);
pub const TB_CHECKBUTTON = (WM_USER + 2);
pub const TB_ISBUTTONCHECKED = (WM_USER + 10);
pub const TB_ISBUTTONENABLED = (WM_USER + 9);
pub const TB_ADDBUTTONSA = (WM_USER + 20);
pub const TB_BUTTONSTRUCTSIZE = (WM_USER + 30);
pub const TB_SETBUTTONINFOA = (WM_USER + 66);
pub const TBIF_IMAGE = 0x0001;
pub const TBIF_STYLE = 0x0008;
pub const TBIF_SIZE = 0x0040;
pub const TBIF_BYINDEX = 0x80000000;
pub const TB_SETBUTTONSIZE = (WM_USER + 31);
pub const TB_SETINDENT = (WM_USER + 47);
pub const TB_SETBITMAPSIZE = (WM_USER + 32);
pub const TB_SETPADDING = (WM_USER + 87);
pub const TB_GETPADDING = (WM_USER + 86);
pub const TB_SETHOTITEM = (WM_USER + 72);
pub const TB_GETHOTITEM = (WM_USER + 71);
pub const TB_MAPACCELERATORA = (WM_USER + 78);
pub const TB_SETEXTENDEDSTYLE = (WM_USER + 84);
pub const TB_GETEXTENDEDSTYLE = (WM_USER + 85);
pub const TBSTYLE_EX_DRAWDDARROWS = 0x00000001;
pub const I_IMAGENONE = (-2);
pub const TB_SETIMAGELIST = (WM_USER + 48);
pub const TB_SETHOTIMAGELIST = (WM_USER + 52);
pub const TB_GETITEMRECT = (WM_USER + 29);
pub const TB_COMMANDTOINDEX = (WM_USER + 25);
pub const TB_BUTTONCOUNT = (WM_USER + 24);
pub const TB_AUTOSIZE = (WM_USER + 33);
pub const TBN_DROPDOWN = (0 - 710);
pub const RBS_TOOLTIPS = 0x0100;
pub const RBS_VARHEIGHT = 0x0200;
pub const RBS_BANDBORDERS = 0x0400;
pub const RBS_FIXEDORDER = 0x0800;
pub const RBBIM_STYLE = 0x00000001;
pub const RBBIM_TEXT = 0x00000004;
pub const RBBIM_CHILD = 0x00000010;
pub const RBBIM_CHILDSIZE = 0x00000020;
pub const RBBIM_SIZE = 0x00000040;
pub const CCS_TOP = 0x00000001;
pub const CCS_NOMOVEY = 0x00000002;
pub const CCS_BOTTOM = 0x00000003;
pub const CCS_NORESIZE = 0x00000004;
pub const CCS_NOPARENTALIGN = 0x00000008;
pub const CCS_ADJUSTABLE = 0x00000020;
pub const CCS_NODIVIDER = 0x00000040;
pub const CCS_VERT = 0x00000080;
pub const RBBS_BREAK = 0x00000001;
pub const RBBS_FIXEDSIZE = 0x00000002;
pub const RBBS_HIDDEN = 0x00000008;
pub const RBBS_GRIPPERALWAYS = 0x00000080;
pub const RBBS_NOGRIPPER = 0x00000100;
pub const RB_INSERTBANDA = (WM_USER + 1);
pub const RB_SETBANDINFOA = (WM_USER + 6);
pub const RB_GETBANDCOUNT = (WM_USER + 12);
pub const RB_GETBARHEIGHT = (WM_USER + 27);
pub const RB_SHOWBAND = (WM_USER + 35);
pub const ICC_LISTVIEW_CLASSES = 0x00000001;
pub const ICC_TREEVIEW_CLASSES = 0x00000002;
pub const ICC_BAR_CLASSES = 0x00000004;
pub const ICC_TAB_CLASSES = 0x00000008;
pub const ICC_UPDOWN_CLASS = 0x00000010;
pub const ICC_PROGRESS_CLASS = 0x00000020;
pub const ICC_HOTKEY_CLASS = 0x00000040;
pub const ICC_ANIMATE_CLASS = 0x00000080;
pub const ICC_WIN95_CLASSES = 0x000000FF;
pub const ICC_DATE_CLASSES = 0x00000100;
pub const ICC_USEREX_CLASSES = 0x00000200;
pub const ICC_COOL_CLASSES = 0x00000400;
pub const ICC_INTERNET_CLASSES = 0x00000800;
pub const ICC_PAGESCROLLER_CLASS = 0x00001000;
pub const ICC_NATIVEFNTCTL_CLASS = 0x00002000;
pub const ICC_STANDARD_CLASSES = 0x00004000;
pub const ICC_LINK_CLASS = 0x00008000;
pub const WEEN32_HAS_REBAR = 1;
pub const WEEN32_HAS_TOOLBAR = 1;
pub const WEEN32_HAS_MENU = 1;
pub const WEEN32_HAS_MESSAGEBOX = 1;
pub const WEEN32_HAS_DIALOGBOX = 1;
pub const WEEN32_HAS_ACCELERATORS = 1;
pub const WEEN32_HAS_IMAGELIST = 1;
pub const WEEN32_HAS_CLIPBOARD = 1;
pub const WEEN32_HAS_CURSORS = 1;
pub const GWL_WNDPROC = (-4);
pub const GWLP_WNDPROC = (-4);
pub const GWL_STYLE = (-16);
pub const GWL_EXSTYLE = (-20);
pub const GWL_ID = (-12);
pub const WM_STYLECHANGED = 0x007D;
pub const MF_STRING = 0x0000;
pub const MF_ENABLED = 0x0000;
pub const MF_UNCHECKED = 0x0000;
pub const MF_BYCOMMAND = 0x0000;
pub const MF_GRAYED = 0x0001;
pub const MF_DISABLED = 0x0002;
pub const MF_CHECKED = 0x0008;
pub const MF_POPUP = 0x0010;
pub const MF_BYPOSITION = 0x0400;
pub const MF_SEPARATOR = 0x0800;
pub const MF_DEFAULT = 0x1000;
pub const TPM_LEFTALIGN = 0x0000;
pub const TPM_RIGHTBUTTON = 0x0002;
pub const TPM_RETURNCMD = 0x0100;
pub const SM_CXSCREEN = 0;
pub const SM_CYSCREEN = 1;
pub const SM_CYCAPTION = 4;
pub const SM_CXBORDER = 5;
pub const SM_CYBORDER = 6;
pub const SM_CYMENU = 15;
pub const SM_CXVSCROLL = 2;
pub const SM_CYHSCROLL = 3;
pub const SM_CXMENUCHECK = 71;
pub const SM_CYMENUCHECK = 72;
pub const CF_TEXT = 1;
pub const CF_BITMAP = 2;
pub const WM_CUT = 0x0300;
pub const WM_COPY = 0x0301;
pub const WM_PASTE = 0x0302;
pub const WM_CLEAR = 0x0303;
pub const IMAGE_BITMAP = 0;
pub const IMAGE_ICON = 1;
pub const LR_LOADFROMFILE = 0x0010;
pub const CLR_NONE = 0xFFFFFFFF;
pub const ILC_COLOR = 0x0000;
pub const ILC_MASK = 0x0001;
pub const ILD_NORMAL = 0x0000;
pub const ILD_TRANSPARENT = 0x0001;
pub const DI_NORMAL = 0x0003;
pub const FVIRTKEY = 0x01;
pub const FSHIFT = 0x04;
pub const FCONTROL = 0x08;
pub const FALT = 0x10;
pub const WM_NEXTDLGCTL = 0x0028;
pub const MB_OK = 0x00000000;
pub const MB_OKCANCEL = 0x00000001;
pub const MB_YESNOCANCEL = 0x00000003;
pub const MB_YESNO = 0x00000004;
pub const MB_TYPEMASK = 0x0000000F;
pub const MB_ICONHAND = 0x00000010;
pub const MB_ICONQUESTION = 0x00000020;
pub const MB_ICONEXCLAMATION = 0x00000030;
pub const MB_ICONASTERISK = 0x00000040;
pub const MB_ICONERROR = MB_ICONHAND;
pub const MB_ICONSTOP = MB_ICONHAND;
pub const MB_ICONWARNING = MB_ICONEXCLAMATION;
pub const MB_ICONINFORMATION = MB_ICONASTERISK;
pub const MB_ICONMASK = 0x000000f0;
pub const MB_DEFBUTTON1 = 0x00000000;
pub const MB_DEFBUTTON2 = 0x00000100;
pub const IDYES = 6;
pub const IDNO = 7;
// <<< genconsts
