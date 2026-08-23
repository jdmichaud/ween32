//! The colour box: the two current colours on the left, then twenty-eight
//! swatches in two rows of fourteen.
//!
//! Measured off a Windows 2000 Paint. The whole strip starts seven pixels
//! down from the top of the bar. The indicator is 31 by 32; each swatch is a
//! 16 by 16 cell — shadow and black outside a 12 by 12 colour, highlight
//! below and right — and they sit on a 16 pixel pitch starting at x 31.

const w = @import("ween32");
const A = @import("app.zig");
const app = &A.app;

pub const class_name = "PaintColorBox";

const top = 7; // where the strip starts inside the bar
const ind_w = 31;
const ind_h = 32;
const grid_x = 31;
const cell = 16;
const cols = 14;

const face = w.RGB(212, 208, 200);
const white = w.RGB(255, 255, 255);
const shadow = w.RGB(128, 128, 128);
const black = w.RGB(0, 0, 0);

fn fill(dc: w.HDC, x: i32, y: i32, cx: i32, cy: i32, color: w.COLORREF) void {
    const r = w.RECT{ .left = x, .top = y, .right = x + cx, .bottom = y + cy };
    const brush = w.CreateSolidBrush(color).?;
    _ = w.FillRect(dc, &r, brush);
    _ = w.DeleteObject(brush);
}

/// A swatch, or the box behind the two current colours: they are the same
/// sunken cell at two sizes, so they are drawn by the same code.
fn sunkenCell(dc: w.HDC, x: i32, y: i32, cx: i32, cy: i32) void {
    fill(dc, x, y, cx - 1, 1, shadow);
    fill(dc, x, y, 1, cy - 1, shadow);
    fill(dc, x + 1, y + 1, cx - 3, 1, black);
    fill(dc, x + 1, y + 1, 1, cy - 3, black);
    fill(dc, x + 1, y + cy - 2, cx - 2, 1, face);
    fill(dc, x + cx - 2, y + 1, 1, cy - 2, face);
    fill(dc, x, y + cy - 1, cx, 1, white);
    fill(dc, x + cx - 1, y, 1, cy, white);
}

/// One of the two current colours: a raised 15x15 box with the colour in the
/// middle of it. The background one is drawn first and the foreground one
/// over its corner, which is what makes them look stacked.
fn colorBox(dc: w.HDC, x: i32, y: i32, color: w.COLORREF) void {
    fill(dc, x, y, 15, 1, white);
    fill(dc, x, y, 1, 15, white);
    fill(dc, x + 1, y + 14, 14, 1, shadow);
    fill(dc, x + 14, y + 1, 1, 14, shadow);
    fill(dc, x + 1, y + 1, 13, 13, face);
    fill(dc, x + 2, y + 2, 11, 11, color);
}

fn paint(hwnd: w.HWND) void {
    var ps: w.PAINTSTRUCT = undefined;
    const dc = w.BeginPaint(hwnd, &ps).?;
    var cr: w.RECT = undefined;
    _ = w.GetClientRect(hwnd, &cr);

    // the current colours, on their checkered ground
    sunkenCell(dc, 0, top, ind_w, ind_h);
    var y: i32 = top + 2;
    while (y < top + ind_h - 2) : (y += 1) {
        var x: i32 = 2;
        while (x < ind_w - 2) : (x += 1)
            _ = w.SetPixel(dc, x, y, if (@mod(x + y, 2) == 0) white else face);
    }
    colorBox(dc, 11, top + 12, app.bg);
    colorBox(dc, 4, top + 5, app.fg);

    // the palette
    for (A.palette, 0..) |color, i| {
        const cx = grid_x + @as(i32, @intCast(i % cols)) * cell;
        const cy = top + @as(i32, @intCast(i / cols)) * cell;
        sunkenCell(dc, cx, cy, cell, cell);
        fill(dc, cx + 2, cy + 2, 12, 12, color);
    }

    // the line along the bottom, where the status bar begins
    fill(dc, 0, cr.bottom - 2, cr.right, 1, shadow);
    fill(dc, 0, cr.bottom - 1, cr.right, 1, white);
    _ = w.EndPaint(hwnd, &ps);
}

/// Which swatch a point is in, or null.
fn swatchAt(x: i32, y: i32) ?usize {
    if (x < grid_x or y < top) return null;
    const col = @divTrunc(x - grid_x, cell);
    const row = @divTrunc(y - top, cell);
    if (col < 0 or col >= cols or row < 0 or row >= 2) return null;
    const i: usize = @intCast(row * cols + col);
    return if (i < A.palette.len) i else null;
}

fn proc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(w.winapi_cc) w.LRESULT {
    switch (msg) {
        w.WM_PAINT => {
            paint(hwnd);
            return 0;
        },
        // The left button picks the drawing colour, the right one the colour
        // behind it — which is the whole of the box's behaviour.
        w.WM_LBUTTONDOWN, w.WM_RBUTTONDOWN => {
            if (swatchAt(w.GET_X_LPARAM(lp), w.GET_Y_LPARAM(lp))) |i| {
                if (msg == w.WM_LBUTTONDOWN) app.fg = A.palette[i] else app.bg = A.palette[i];
                _ = w.InvalidateRect(hwnd, null, w.FALSE);
            }
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
