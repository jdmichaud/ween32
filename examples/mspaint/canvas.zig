//! The view: the picture, scrolled, with the tools drawing into it.
//!
//! The picture lives in a memory device context and is blitted into the
//! window; nothing is drawn twice, and every tool works on the bitmap rather
//! than on the screen — which is why what is drawn survives a scroll.

const w = @import("ween32");
const A = @import("app.zig");
const app = &A.app;

pub const class_name = "PaintView";

/// The grey the page sits on, which shows when the picture is smaller than
/// the window.
const workspace = w.COLOR_APPWORKSPACE;

fn clientSize(hwnd: w.HWND) w.RECT {
    var cr: w.RECT = undefined;
    _ = w.GetClientRect(hwnd, &cr);
    return cr;
}

/// The bars follow the picture: their range is its size and their page is
/// what fits, which is what makes the thumb the right length.
pub fn updateScroll(hwnd: w.HWND) void {
    const cr = clientSize(hwnd);
    // The page a bar shows is the window less the margin the picture is
    // inset by: scrolled to the end, the far edge of the picture lands on
    // the far edge of the window, and the thumb is the length the machine's
    // is.
    var si = w.SCROLLINFO{
        .fMask = w.SIF_RANGE | w.SIF_PAGE | w.SIF_POS,
        .nMin = 0,
        .nMax = app.pic.width - 1,
        .nPage = @intCast(@max(0, cr.right - margin)),
        .nPos = app.scroll_x,
    };
    _ = w.SetScrollInfo(hwnd, w.SB_HORZ, &si, w.TRUE);
    si.nMax = app.pic.height - 1;
    si.nPage = @intCast(@max(0, cr.bottom - margin));
    si.nPos = app.scroll_y;
    _ = w.SetScrollInfo(hwnd, w.SB_VERT, &si, w.TRUE);
    app.scroll_x = w.GetScrollPos(hwnd, w.SB_HORZ);
    app.scroll_y = w.GetScrollPos(hwnd, w.SB_VERT);
}

/// Where the picture is drawn in the window. It sits three pixels in from
/// the top left — the room the sizing handles need — and moves with the
/// scroll bars.
pub fn pageOrigin() w.POINT {
    return .{ .x = margin - app.scroll_x, .y = margin - app.scroll_y };
}

pub const margin = 3;
const handle = 3;
const highlight = w.RGB(10, 36, 106);

/// The eight handles around the picture. The three that can resize it — the
/// right edge, the bottom edge and the corner between them — are solid; the
/// other five are hollow, which is how Paint says they do nothing.
fn drawHandles(dc: w.HDC) void {
    const o = pageOrigin();
    const xs = [3]i32{ o.x - handle, o.x + @divTrunc(app.pic.width, 2) - 1, o.x + app.pic.width };
    const ys = [3]i32{ o.y - handle, o.y + @divTrunc(app.pic.height, 2) - 1, o.y + app.pic.height };
    for (ys, 0..) |hy, row| {
        for (xs, 0..) |hx, col| {
            if (row == 1 and col == 1) continue; // the middle is the picture
            const live = (row == 2 and col >= 1) or (col == 2 and row >= 1);
            var r = w.RECT{ .left = hx, .top = hy, .right = hx + handle, .bottom = hy + handle };
            const brush = w.CreateSolidBrush(highlight).?;
            _ = w.FillRect(dc, &r, brush);
            _ = w.DeleteObject(brush);
            if (!live) {
                r = .{ .left = hx + 1, .top = hy + 1, .right = hx + handle, .bottom = hy + handle };
                _ = w.FillRect(dc, &r, w.GetStockObject(w.WHITE_BRUSH).?);
            }
        }
    }
}

fn paint(hwnd: w.HWND) void {
    var ps: w.PAINTSTRUCT = undefined;
    const dc = w.BeginPaint(hwnd, &ps).?;
    const cr = clientSize(hwnd);
    const o = pageOrigin();

    // the grey the page sits on, everywhere the page is not
    const grey = w.GetSysColorBrush(workspace).?;
    var r = w.RECT{ .left = 0, .top = 0, .right = cr.right, .bottom = o.y };
    _ = w.FillRect(dc, &r, grey);
    r = .{ .left = 0, .top = o.y, .right = o.x, .bottom = cr.bottom };
    _ = w.FillRect(dc, &r, grey);
    r = .{ .left = o.x + app.pic.width, .top = o.y, .right = cr.right, .bottom = cr.bottom };
    _ = w.FillRect(dc, &r, grey);
    r = .{ .left = o.x, .top = o.y + app.pic.height, .right = o.x + app.pic.width, .bottom = cr.bottom };
    _ = w.FillRect(dc, &r, grey);

    _ = w.BitBlt(dc, o.x, o.y, app.pic.width, app.pic.height, app.pic.dc, 0, 0, w.SRCCOPY);
    drawHandles(dc);
    _ = w.EndPaint(hwnd, &ps);
}

/// One scroll message, for either bar: the same arithmetic with a different
/// axis, so it is written once.
fn scroll(hwnd: w.HWND, bar: i32, code: u16, pos: *i32) void {
    const cr = clientSize(hwnd);
    const page = if (bar == w.SB_HORZ) cr.right else cr.bottom;
    var si = w.SCROLLINFO{ .fMask = w.SIF_ALL };
    _ = w.GetScrollInfo(hwnd, bar, &si);
    var p = si.nPos;
    switch (code) {
        w.SB_LINEUP => p -= 1,
        w.SB_LINEDOWN => p += 1,
        w.SB_PAGEUP => p -= page,
        w.SB_PAGEDOWN => p += page,
        w.SB_THUMBTRACK, w.SB_THUMBPOSITION => p = si.nTrackPos,
        else => {},
    }
    _ = w.SetScrollPos(hwnd, bar, p, w.TRUE);
    pos.* = w.GetScrollPos(hwnd, bar);
    _ = w.InvalidateRect(hwnd, null, w.FALSE);
}

fn proc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.LRESULT {
    switch (msg) {
        w.WM_PAINT => {
            paint(hwnd);
            return 0;
        },
        w.WM_SIZE => {
            updateScroll(hwnd);
            return 0;
        },
        w.WM_HSCROLL => {
            scroll(hwnd, w.SB_HORZ, w.LOWORD(wp), &app.scroll_x);
            return 0;
        },
        w.WM_VSCROLL => {
            scroll(hwnd, w.SB_VERT, w.LOWORD(wp), &app.scroll_y);
            return 0;
        },
        else => return w.DefWindowProcA(hwnd, msg, wp, lp),
    }
}

pub fn register() void {
    var wc = w.WNDCLASSA{
        .lpfnWndProc = proc,
        .hbrBackground = w.GetSysColorBrush(workspace),
        .hCursor = w.LoadCursorA(null, w.IDC_ARROW),
        .lpszClassName = class_name,
    };
    _ = w.RegisterClassA(&wc);
}
