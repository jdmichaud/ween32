//! The tool box: sixteen buttons in two columns, and the box of settings
//! under them that changes with the tool.
//!
//! Every number here was measured off a Windows 2000 Paint. The buttons are
//! 25 by 25 on a 25-pixel pitch starting four pixels in; the bar draws an
//! etched line across its top and its bottom; the settings box is a sunken
//! rectangle 41 by 66 at (8, 203). The button's own bevel is not DrawEdge's:
//! its outer bottom-right is black rather than the dark grey a window frame
//! uses, which is how Paint drew a toolbar button and why it looks harder
//! than the rest of the shell.

const w = @import("ween32");
const A = @import("app.zig");
const art = @import("art_tools.zig");
const artwork = @import("artwork.zig");
const app = &A.app;

pub const class_name = "PaintToolBox";

const btn = 25; // the pitch and the size of a tool button
const grid_x = 4; // where the two columns start within the bar
const opt_x = 8; // the settings box
const opt_y = 203;
const opt_w = 41;
const opt_h = 66;

const face = w.RGB(212, 208, 200);
const white = w.RGB(255, 255, 255);
const shadow = w.RGB(128, 128, 128);
const black = w.RGB(0, 0, 0);
const navy = w.RGB(10, 36, 106);

var glyphs: w.HIMAGELIST = null;

// ---- painting -------------------------------------------------------------

fn fill(dc: w.HDC, x: i32, y: i32, cx: i32, cy: i32, color: w.COLORREF) void {
    const r = w.RECT{ .left = x, .top = y, .right = x + cx, .bottom = y + cy };
    const brush = w.CreateSolidBrush(color).?;
    _ = w.FillRect(dc, &r, brush);
    _ = w.DeleteObject(brush);
}

/// The line a docked bar draws along an edge: shadow, then highlight.
fn etched(dc: w.HDC, x: i32, y: i32, cx: i32) void {
    fill(dc, x, y, cx, 1, shadow);
    fill(dc, x, y + 1, cx, 1, white);
}

/// One tool button. Paint's own bevel, not the shell's: raised is white on
/// the top and left with black — not dark grey — outside the bottom and
/// right; pressed turns it inside out and fills the middle with the 50%
/// checker a checked toolbar button wears.
fn drawButton(dc: w.HDC, bx: i32, by: i32, index: usize, pressed: bool) void {
    if (pressed) {
        fill(dc, bx, by, 24, 1, black);
        fill(dc, bx, by, 1, 24, black);
        fill(dc, bx + 1, by + 1, 22, 1, shadow);
        fill(dc, bx + 1, by + 1, 1, 22, shadow);
        fill(dc, bx, by + 24, 25, 1, white);
        fill(dc, bx + 24, by, 1, 25, white);
        // the checker, twenty-one squares each way, in the window's
        // coordinates so neighbouring buttons line up as one pattern
        var y: i32 = by + 2;
        while (y <= by + 22) : (y += 1) {
            var x: i32 = bx + 2;
            while (x <= bx + 22) : (x += 1)
                _ = w.SetPixel(dc, x, y, if (@mod(x + y, 2) == 1) white else face);
        }
    } else {
        fill(dc, bx, by, 24, 1, white);
        fill(dc, bx, by, 1, 24, white);
        fill(dc, bx + 1, by + 23, 23, 1, shadow);
        fill(dc, bx + 23, by + 1, 1, 23, shadow);
        fill(dc, bx, by + 24, 25, 1, black);
        fill(dc, bx + 24, by, 1, 25, black);
    }
    const off: i32 = if (pressed) 5 else 4;
    _ = w.ImageList_Draw(glyphs, @intCast(index), dc, bx + off, by + off, w.ILD_TRANSPARENT);
}

/// The settings box, and whatever the current tool puts in it.
///
/// The border is a one-pixel sunken edge, but not DrawEdge's: the machine
/// leaves the top-right and bottom-left corners alone, where DrawEdge's
/// later line would take them.
fn drawOptions(dc: w.HDC) void {
    fill(dc, opt_x, opt_y, opt_w - 1, 1, shadow);
    fill(dc, opt_x, opt_y, 1, opt_h - 1, shadow);
    fill(dc, opt_x + opt_w - 1, opt_y + 1, 1, opt_h - 1, white);
    fill(dc, opt_x + 1, opt_y + opt_h - 1, opt_w - 1, 1, white);

    const chosen = A.option();
    switch (app.tool) {
        .eraser => {
            // four square rubbers, 4 6 8 and 10 pixels across, each in a
            // 14x14 cell on a 16-pixel pitch
            var i: i32 = 0;
            while (i < 4) : (i += 1) {
                const cx = opt_x + 10;
                const cy = opt_y + 2 + i * 16;
                const on = i == chosen;
                if (on) fill(dc, cx, cy, 14, 14, navy);
                const size = 4 + i * 2;
                fill(dc, cx + @divTrunc(14 - size, 2), cy + @divTrunc(14 - size, 2), size, size, if (on) white else black);
            }
        },
        else => {},
    }
}

fn paint(hwnd: w.HWND) void {
    var ps: w.PAINTSTRUCT = undefined;
    const dc = w.BeginPaint(hwnd, &ps).?;
    var cr: w.RECT = undefined;
    _ = w.GetClientRect(hwnd, &cr);

    var i: usize = 0;
    while (i < 16) : (i += 1) {
        const bx = grid_x + @as(i32, @intCast(i % 2)) * btn;
        const by = @as(i32, @intCast(i / 2)) * btn;
        drawButton(dc, bx, by, i, A.Tool.fromIndex(i) == app.tool);
    }
    // over the top row of buttons, which is what the real one does: the bar's
    // own edge wins where they meet
    etched(dc, 0, 0, cr.right);
    etched(dc, 0, cr.bottom - 2, cr.right);
    drawOptions(dc);
    _ = w.EndPaint(hwnd, &ps);
}

// ---- input ----------------------------------------------------------------

/// The tool whose button covers a point, or null.
fn toolAt(x: i32, y: i32) ?A.Tool {
    if (x < grid_x or x >= grid_x + 2 * btn or y < 0 or y >= 8 * btn) return null;
    const col = @divTrunc(x - grid_x, btn);
    const row = @divTrunc(y, btn);
    return A.Tool.fromIndex(@intCast(row * 2 + col));
}

fn optionAt(x: i32, y: i32) ?u8 {
    const count = app.tool.optionCount();
    if (count == 0) return null;
    switch (app.tool) {
        .eraser => {
            const i = @divTrunc(y - (opt_y + 2), 16);
            if (x < opt_x + 10 or x >= opt_x + 24) return null;
            if (i < 0 or i >= count) return null;
            return @intCast(i);
        },
        else => return null,
    }
}

fn proc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.LRESULT {
    switch (msg) {
        w.WM_PAINT => {
            paint(hwnd);
            return 0;
        },
        w.WM_LBUTTONDOWN => {
            const x = w.GET_X_LPARAM(lp);
            const y = w.GET_Y_LPARAM(lp);
            if (toolAt(x, y)) |t| {
                app.tool = t;
                _ = w.InvalidateRect(hwnd, null, w.FALSE);
                _ = w.InvalidateRect(app.view, null, w.FALSE);
            } else if (optionAt(x, y)) |o| {
                A.setOption(o);
                _ = w.InvalidateRect(hwnd, null, w.FALSE);
            }
            return 0;
        },
        else => return w.DefWindowProcA(hwnd, msg, wp, lp),
    }
}

pub fn register() void {
    buildGlyphs();
    var wc = w.WNDCLASSA{
        .lpfnWndProc = proc,
        .hbrBackground = w.GetSysColorBrush(w.COLOR_BTNFACE),
        .hCursor = w.LoadCursorA(null, w.IDC_ARROW),
        .lpszClassName = class_name,
    };
    _ = w.RegisterClassA(&wc);
}

/// The sixteen glyphs in an image list — the face colour is the transparent
/// one, so a pressed button's checker shows through around the picture.
fn buildGlyphs() void {
    glyphs = artwork.imageList(art);
}
