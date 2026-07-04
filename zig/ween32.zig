//! Zig bindings for the win32-compatible API.
//!
//! On non-Windows targets these symbols resolve to libween32 (link the
//! `ween32` artifact); on Windows they are the real USER32/GDI32 — same
//! names, signatures and constants, so application code is identical on
//! both. This mirrors include/ween32.h; keep the two in sync.

const std = @import("std");

// ---- fundamental types (LLP64-faithful) ---------------------------------

pub const BOOL = c_int;
pub const BYTE = u8;
pub const WORD = u16;
pub const DWORD = u32;
pub const LONG = i32;
pub const UINT = c_uint;
pub const WPARAM = usize;
pub const LPARAM = isize;
pub const LRESULT = isize;
pub const INT_PTR = isize;
pub const UINT_PTR = usize;
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
pub const HINSTANCE = ?*anyopaque;
pub const HICON = ?*anyopaque;
pub const HCURSOR = ?*anyopaque;
pub const HMENU = ?*anyopaque;

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

pub const CW_USEDEFAULT: c_int = @bitCast(@as(u32, 0x80000000));

// ---- window messages -------------------------------------------------------

pub const WM_NULL = 0x0000;
pub const WM_CREATE = 0x0001;
pub const WM_DESTROY = 0x0002;
pub const WM_MOVE = 0x0003;
pub const WM_SETFOCUS = 0x0007;
pub const WM_KILLFOCUS = 0x0008;
pub const WM_SETTEXT = 0x000C;
pub const WM_PAINT = 0x000F;
pub const WM_CLOSE = 0x0010;
pub const WM_QUIT = 0x0012;
pub const WM_SETFONT = 0x0030;
pub const WM_GETFONT = 0x0031;
pub const WM_DRAWITEM = 0x002B;
pub const WM_NCHITTEST = 0x0084;
pub const WM_NCPAINT = 0x0085;
pub const WM_NCMOUSEMOVE = 0x00A0;
pub const WM_NCLBUTTONDOWN = 0x00A1;
pub const WM_NCLBUTTONUP = 0x00A2;
pub const WM_KEYDOWN = 0x0100;
pub const WM_KEYUP = 0x0101;
pub const WM_CHAR = 0x0102;
pub const WM_INITDIALOG = 0x0110;
pub const WM_COMMAND = 0x0111;
pub const WM_MOUSEMOVE = 0x0200;
pub const WM_LBUTTONDOWN = 0x0201;
pub const WM_LBUTTONUP = 0x0202;
pub const WM_USER = 0x0400;
pub const DM_GETDEFID = WM_USER + 0;
pub const DM_SETDEFID = WM_USER + 1;
pub const DC_HASDEFID = 0x534B;

// ---- window styles ----------------------------------------------------------

pub const WS_OVERLAPPED: DWORD = 0x00000000;
pub const WS_POPUP: DWORD = 0x80000000;
pub const WS_CHILD: DWORD = 0x40000000;
pub const WS_VISIBLE: DWORD = 0x10000000;
pub const WS_CAPTION: DWORD = 0x00C00000;
pub const WS_BORDER: DWORD = 0x00800000;
pub const WS_DLGFRAME: DWORD = 0x00400000;
pub const WS_SYSMENU: DWORD = 0x00080000;
pub const WS_GROUP: DWORD = 0x00020000;
pub const WS_TABSTOP: DWORD = 0x00010000;

pub const BS_PUSHBUTTON: DWORD = 0x00000000;
pub const BS_DEFPUSHBUTTON: DWORD = 0x00000001;
pub const BS_OWNERDRAW: DWORD = 0x0000000B;
pub const SS_LEFT: DWORD = 0x00000000;
pub const SS_CENTER: DWORD = 0x00000001;
pub const SS_RIGHT: DWORD = 0x00000002;

pub const BN_CLICKED = 0;

pub const DS_3DLOOK: DWORD = 0x0004;
pub const DS_SETFONT: DWORD = 0x40;
pub const DS_MODALFRAME: DWORD = 0x80;
pub const DS_CENTER: DWORD = 0x0800;

pub const IDOK = 1;
pub const IDCANCEL = 2;

// ---- hit tests / ShowWindow / keys --------------------------------------------

pub const HTNOWHERE = 0;
pub const HTCLIENT = 1;
pub const HTCAPTION = 2;
pub const HTCLOSE = 20;

pub const SW_HIDE = 0;
pub const SW_SHOWNORMAL = 1;
pub const SW_SHOW = 5;

pub const VK_BACK = 0x08;
pub const VK_TAB = 0x09;
pub const VK_RETURN = 0x0D;
pub const VK_ESCAPE = 0x1B;

// ---- system colors --------------------------------------------------------------

pub const COLOR_ACTIVECAPTION = 2;
pub const COLOR_WINDOW = 5;
pub const COLOR_WINDOWTEXT = 8;
pub const COLOR_CAPTIONTEXT = 9;
pub const COLOR_BTNFACE = 15;
pub const COLOR_BTNSHADOW = 16;
pub const COLOR_BTNTEXT = 18;
pub const COLOR_BTNHIGHLIGHT = 20;
pub const COLOR_3DDKSHADOW = 21;
pub const COLOR_3DLIGHT = 22;
pub const COLOR_GRADIENTACTIVECAPTION = 27;

// ---- DrawEdge / DrawFrameControl / DrawText ---------------------------------------

pub const BDR_RAISEDOUTER = 0x0001;
pub const BDR_SUNKENOUTER = 0x0002;
pub const BDR_RAISEDINNER = 0x0004;
pub const BDR_SUNKENINNER = 0x0008;
pub const EDGE_RAISED = BDR_RAISEDOUTER | BDR_RAISEDINNER;
pub const EDGE_SUNKEN = BDR_SUNKENOUTER | BDR_SUNKENINNER;
pub const BF_LEFT = 0x0001;
pub const BF_TOP = 0x0002;
pub const BF_RIGHT = 0x0004;
pub const BF_BOTTOM = 0x0008;
pub const BF_RECT = BF_LEFT | BF_TOP | BF_RIGHT | BF_BOTTOM;

pub const DFC_CAPTION = 1;
pub const DFCS_CAPTIONCLOSE = 0x0000;
pub const DFCS_CAPTIONMIN = 0x0001;
pub const DFCS_CAPTIONMAX = 0x0002;
pub const DFCS_CAPTIONRESTORE = 0x0003;
pub const DFCS_PUSHED = 0x0200;

pub const DT_LEFT = 0x00000000;
pub const DT_CENTER = 0x00000001;
pub const DT_RIGHT = 0x00000002;
pub const DT_VCENTER = 0x00000004;
pub const DT_SINGLELINE = 0x00000020;

pub const TRANSPARENT = 1;
pub const OPAQUE = 2;
pub const SYSTEM_FONT = 13;
pub const DEFAULT_GUI_FONT = 17;

pub const ODT_BUTTON = 4;
pub const ODA_DRAWENTIRE = 0x0001;
pub const ODA_SELECT = 0x0002;
pub const ODS_SELECTED = 0x0001;

// ---- USER32 --------------------------------------------------------------------------

pub extern fn RegisterClassA(wc: *const WNDCLASSA) ATOM;
pub extern fn CreateWindowExA(ex_style: DWORD, class_name: LPCSTR, window_name: ?LPCSTR, style: DWORD, x: c_int, y: c_int, w: c_int, h: c_int, parent: ?HWND, menu: HMENU, inst: HINSTANCE, param: ?*anyopaque) ?HWND;
pub fn CreateWindowA(class_name: LPCSTR, window_name: ?LPCSTR, style: DWORD, x: c_int, y: c_int, w: c_int, h: c_int, parent: ?HWND, menu: HMENU, inst: HINSTANCE, param: ?*anyopaque) ?HWND {
    return CreateWindowExA(0, class_name, window_name, style, x, y, w, h, parent, menu, inst, param);
}
pub extern fn DestroyWindow(wnd: HWND) BOOL;
pub extern fn ShowWindow(wnd: HWND, cmd: c_int) BOOL;
pub extern fn SetWindowTextA(wnd: HWND, text: LPCSTR) BOOL;
pub extern fn GetWindowTextA(wnd: HWND, out: LPSTR, max: c_int) c_int;
pub extern fn GetClientRect(wnd: HWND, rect: *RECT) BOOL;
pub extern fn MoveWindow(wnd: HWND, x: c_int, y: c_int, w: c_int, h: c_int, repaint: BOOL) BOOL;
pub extern fn InvalidateRect(wnd: HWND, rect: ?*const RECT, erase: BOOL) BOOL;
pub extern fn UpdateWindow(wnd: HWND) BOOL;
pub extern fn GetDlgItem(dlg: HWND, id: c_int) ?HWND;
pub extern fn GetDlgCtrlID(wnd: HWND) c_int;
pub extern fn SetFocus(wnd: ?HWND) ?HWND;
pub extern fn SetCapture(wnd: HWND) ?HWND;
pub extern fn ReleaseCapture() BOOL;
pub extern fn GetCapture() ?HWND;

pub extern fn GetMessageA(msg: *MSG, wnd: ?HWND, min: UINT, max: UINT) BOOL;
pub extern fn TranslateMessage(msg: *const MSG) BOOL;
pub extern fn DispatchMessageA(msg: *const MSG) LRESULT;
pub extern fn PostQuitMessage(code: c_int) void;
pub extern fn SendMessageA(wnd: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) LRESULT;
pub extern fn DefWindowProcA(wnd: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) LRESULT;

pub extern fn GetDialogBaseUnits() LONG;
pub extern fn MapDialogRect(dlg: ?HWND, rect: *RECT) BOOL;
pub extern fn MulDiv(number: c_int, numerator: c_int, denominator: c_int) c_int;
pub extern fn IsDialogMessageA(dlg: HWND, msg: *MSG) BOOL;
pub extern fn DefDlgProcA(dlg: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) LRESULT;
pub extern fn EndDialog(dlg: HWND, result: INT_PTR) BOOL;

// ---- GDI ---------------------------------------------------------------------------------

pub extern fn BeginPaint(wnd: HWND, ps: *PAINTSTRUCT) ?HDC;
pub extern fn EndPaint(wnd: HWND, ps: *const PAINTSTRUCT) BOOL;
pub extern fn FillRect(dc: HDC, rect: *const RECT, brush: HBRUSH) BOOL;
pub extern fn FrameRect(dc: HDC, rect: *const RECT, brush: HBRUSH) c_int;
pub extern fn DrawEdge(dc: HDC, rect: *RECT, edge: UINT, flags: UINT) BOOL;
pub extern fn DrawFrameControl(dc: HDC, rect: *RECT, kind: UINT, state: UINT) BOOL;
pub extern fn TextOutA(dc: HDC, x: c_int, y: c_int, text: [*]const u8, len: c_int) BOOL;
pub extern fn DrawTextA(dc: HDC, text: [*]const u8, len: c_int, rect: *RECT, format: UINT) c_int;
pub extern fn GetTextExtentPoint32A(dc: HDC, text: [*]const u8, len: c_int, size: *SIZE) BOOL;
pub extern fn SetTextColor(dc: HDC, color: COLORREF) COLORREF;
pub extern fn SetBkMode(dc: HDC, mode: c_int) c_int;
pub extern fn GetSysColor(index: c_int) DWORD;
pub extern fn GetSysColorBrush(index: c_int) ?HBRUSH;
pub extern fn CreateSolidBrush(color: COLORREF) ?HBRUSH;
pub extern fn DeleteObject(obj: HGDIOBJ) BOOL;
pub extern fn GetStockObject(what: c_int) ?HGDIOBJ;
pub extern fn SelectObject(dc: HDC, obj: HGDIOBJ) ?HGDIOBJ;
