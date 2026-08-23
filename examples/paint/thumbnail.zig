//! View > Show Thumbnail: a small window that shows the picture at its own
//! size while the view is magnified.
//!
//! Paint's floats over the top right of the view. This one is a window of
//! its own, which is what it is in the real program too.

const w = @import("ween32");
const A = @import("app.zig");
const app = &A.app;

pub const class_name = "PaintThumbnail";

const width = 120;
const height = 100;

var window: ?w.HWND = null;

fn proc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(w.winapi_cc) w.LRESULT {
    switch (msg) {
        w.WM_PAINT => {
            var ps: w.PAINTSTRUCT = undefined;
            const dc = w.BeginPaint(hwnd, &ps).?;
            var cr: w.RECT = undefined;
            _ = w.GetClientRect(hwnd, &cr);
            _ = w.FillRect(dc, &cr, w.GetSysColorBrush(w.COLOR_BTNFACE).?);
            // as much of the picture as fits, from the top left
            _ = w.BitBlt(dc, 0, 0, @min(cr.right, app.pic.width), @min(cr.bottom, app.pic.height), app.pic.dc, 0, 0, w.SRCCOPY);
            _ = w.EndPaint(hwnd, &ps);
            return 0;
        },
        else => return w.DefWindowProcA(hwnd, msg, wp, lp),
    }
}

pub fn register() void {
    var wc = w.WNDCLASSA{
        .lpfnWndProc = proc,
        .hbrBackground = w.GetSysColorBrush(w.COLOR_BTNFACE),
        .hCursor = w.LoadCursorA(null, w.IDC_ARROW),
        .lpszClassName = class_name,
    };
    _ = w.RegisterClassA(&wc);
}

pub fn toggle(on: bool) void {
    if (on and window == null) {
        var r: w.RECT = undefined;
        _ = w.GetWindowRect(app.view, &r);
        window = w.CreateWindowExA(w.WS_EX_CLIENTEDGE, class_name, "Thumbnail", w.WS_POPUP | w.WS_CAPTION | w.WS_VISIBLE, r.right - width - 8, r.top + 8, width, height, app.frame, null, null, null);
    } else if (!on) {
        if (window) |win| _ = w.DestroyWindow(win);
        window = null;
    }
    refresh();
}

/// The picture changed: the thumbnail follows it.
pub fn refresh() void {
    if (window) |win| _ = w.InvalidateRect(win, null, w.FALSE);
}
