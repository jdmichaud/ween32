//! The view: the picture, scrolled, with the tools drawing into it.
//!
//! The picture lives in a memory device context and is blitted into the
//! window; nothing is drawn twice, and every tool works on the bitmap rather
//! than on the screen — which is why what is drawn survives a scroll.

const w = @import("ween32");
const A = @import("app.zig");
const app = &A.app;
const tools = @import("tools.zig");
const undo = @import("undo.zig");

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
    // What the drag is making, drawn over the picture but not into it: the
    // origin moves to the picture's, so the tools work in its coordinates
    // whichever context they are handed.
    _ = w.SetViewportOrgEx(dc, o.x, o.y, null);
    tools.drawDrag(dc);
    _ = w.SetViewportOrgEx(dc, 0, 0, null);
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

/// A point in the window, in the picture's coordinates.
fn toImage(lp: w.LPARAM) w.POINT {
    const o = pageOrigin();
    return .{ .x = w.GET_X_LPARAM(lp) - o.x, .y = w.GET_Y_LPARAM(lp) - o.y };
}

fn buttonDown(hwnd: w.HWND, right: bool, wp: w.WPARAM, lp: w.LPARAM) void {
    const p = toImage(lp);
    const d = &tools.drag;
    if (d.active) return;
    // The curve and the polygon are still going from the press before this
    // one; everything else starts afresh.
    const continuing = (app.tool == .curve or app.tool == .polygon) and
        (d.stage > 0 or d.count > 0) and d.tool == app.tool;
    if (!continuing) {
        d.* = .{};
        d.tool = app.tool;
        d.right = right;
        d.start = p;
        d.points[0] = p;
        d.count = 1;
    }
    d.active = true;
    d.last = p;
    d.cur = p;
    d.shift = (wp & w.MK_SHIFT) != 0;
    _ = w.SetCapture(hwnd);

    switch (app.tool) {
        .pencil, .brush, .eraser, .airbrush => {
            undo.take();
            tools.stroke(app.pic.dc, p, p);
            _ = w.InvalidateRect(hwnd, null, w.FALSE);
        },
        .fill => {
            undo.take();
            const brush = w.CreateSolidBrush(tools.penColor()).?;
            const old = w.SelectObject(app.pic.dc, brush);
            _ = w.ExtFloodFill(app.pic.dc, p.x, p.y, w.GetPixel(app.pic.dc, p.x, p.y), w.FLOODFILLSURFACE);
            if (old) |o| _ = w.SelectObject(app.pic.dc, o);
            _ = w.DeleteObject(brush);
            _ = w.InvalidateRect(hwnd, null, w.FALSE);
        },
        .pick => {
            // takes the colour under the pointer and goes back to the tool
            // that was in use before, which is what Paint does
            const c = w.GetPixel(app.pic.dc, p.x, p.y);
            if (c != 0xFFFFFFFF) {
                if (right) app.bg = c else app.fg = c;
            }
            _ = w.InvalidateRect(app.colorbox, null, w.FALSE);
            d.active = false;
            _ = w.ReleaseCapture();
        },
        else => {},
    }
}

fn mouseMove(hwnd: w.HWND, wp: w.WPARAM, lp: w.LPARAM) void {
    const d = &tools.drag;
    const p = toImage(lp);
    if (!d.active) {
        // a curve or a polygon in the middle of being made follows the
        // pointer even with the button up
        if (d.stage > 0 or (d.tool == .polygon and d.count > 0)) {
            d.cur = p;
            _ = w.InvalidateRect(hwnd, null, w.FALSE);
        }
        return;
    }
    d.shift = (wp & w.MK_SHIFT) != 0;
    d.cur = p;
    switch (d.tool) {
        .pencil, .brush, .eraser, .airbrush => {
            tools.stroke(app.pic.dc, d.last, p);
            d.last = p;
            _ = w.InvalidateRect(hwnd, null, w.FALSE);
        },
        else => _ = w.InvalidateRect(hwnd, null, w.FALSE),
    }
}

fn buttonUp(hwnd: w.HWND, lp: w.LPARAM) void {
    const d = &tools.drag;
    if (!d.active) return;
    d.cur = toImage(lp);
    d.active = false;
    _ = w.ReleaseCapture();
    switch (d.tool) {
        .pencil, .brush, .eraser, .airbrush, .fill, .pick => {},
        .curve => {
            // press one: the line and its two ends. Presses two and three
            // pull it about; after the third it is finished.
            if (d.stage == 0) {
                d.points[1] = d.cur;
                d.stage = 1;
            } else if (d.stage == 1) {
                d.points[2] = d.cur;
                d.stage = 2;
            } else {
                d.points[3] = d.cur;
                d.stage = 3;
                commit();
            }
        },
        .polygon => {
            if (d.count < tools.max_points - 1) {
                d.points[d.count] = d.cur;
                d.count += 1;
            }
            // back where it started, or a double click: close it
            const first = d.points[0];
            if (d.count > 2 and @abs(d.cur.x - first.x) < 4 and @abs(d.cur.y - first.y) < 4)
                commit();
        },
        .magnifier => {
            d.count = 0;
        },
        else => commit(),
    }
    _ = w.InvalidateRect(hwnd, null, w.FALSE);
}

/// The shape goes into the picture and the drag is over.
fn commit() void {
    const d = &tools.drag;
    undo.take();
    tools.drawDrag(app.pic.dc);
    d.* = .{};
    _ = w.InvalidateRect(app.view, null, w.FALSE);
}

fn proc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.LRESULT {
    switch (msg) {
        w.WM_LBUTTONDOWN => {
            buttonDown(hwnd, false, wp, lp);
            return 0;
        },
        w.WM_RBUTTONDOWN => {
            buttonDown(hwnd, true, wp, lp);
            return 0;
        },
        w.WM_MOUSEMOVE => {
            mouseMove(hwnd, wp, lp);
            return 0;
        },
        w.WM_LBUTTONUP, w.WM_RBUTTONUP => {
            buttonUp(hwnd, lp);
            return 0;
        },
        w.WM_LBUTTONDBLCLK => {
            // a double click ends a polygon
            if (tools.drag.tool == .polygon and tools.drag.count > 2) commit();
            return 0;
        },
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
