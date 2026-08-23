//! View > View Bitmap: the picture on its own, with nothing round it.
//!
//! A window with no frame the size of the screen, the picture drawn in its
//! corner; any key or click puts it away again. Paint has had this since
//! before it had anything else.

const w = @import("ween32");
const A = @import("app.zig");
const app = &A.app;

pub const class_name = "PaintViewBitmap";

var window: ?w.HWND = null;

fn proc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(w.winapi_cc) w.LRESULT {
    switch (msg) {
        w.WM_PAINT => {
            var ps: w.PAINTSTRUCT = undefined;
            const dc = w.BeginPaint(hwnd, &ps).?;
            var cr: w.RECT = undefined;
            _ = w.GetClientRect(hwnd, &cr);
            _ = w.FillRect(dc, &cr, w.GetSysColorBrush(w.COLOR_APPWORKSPACE).?);
            _ = w.BitBlt(dc, 0, 0, app.pic.width, app.pic.height, app.pic.dc, 0, 0, w.SRCCOPY);
            _ = w.EndPaint(hwnd, &ps);
            return 0;
        },
        w.WM_LBUTTONDOWN, w.WM_RBUTTONDOWN, w.WM_KEYDOWN, w.WM_KILLFOCUS => {
            close();
            return 0;
        },
        else => return w.DefWindowProcA(hwnd, msg, wp, lp),
    }
}

pub fn register() void {
    var wc = w.WNDCLASSA{
        .lpfnWndProc = proc,
        .hbrBackground = w.GetSysColorBrush(w.COLOR_APPWORKSPACE),
        .hCursor = w.LoadCursorA(null, w.IDC_ARROW),
        .lpszClassName = class_name,
    };
    _ = w.RegisterClassA(&wc);
}

pub fn show() void {
    if (window != null) return;
    const cx = w.GetSystemMetrics(w.SM_CXSCREEN);
    const cy = w.GetSystemMetrics(w.SM_CYSCREEN);
    window = w.CreateWindowExA(0, class_name, "", w.WS_POPUP | w.WS_VISIBLE, 0, 0, cx, cy, null, null, null, null);
    if (window) |win| {
        _ = w.SetFocus(win);
        _ = w.InvalidateRect(win, null, w.TRUE);
    }
}

pub fn close() void {
    if (window) |win| {
        _ = w.DestroyWindow(win);
        window = null;
        _ = w.SetFocus(app.view);
    }
}
