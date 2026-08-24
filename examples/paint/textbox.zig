//! The text tool: a box dragged out on the picture, typed into, and drawn
//! into the picture when you click away from it.
//!
//! Everything here about how it looks and behaves was read off the machine.
//! While the box is open it wears a dashed border — four pixels of the
//! selection's navy, four of nothing — with the same eight handles a
//! selection has; the opaque setting fills it with the background colour
//! before the text goes on, as it does for a selection; what is typed wraps
//! at the box's right edge, word by word; and a caret blinks after the last
//! character. Escape throws the lot away, and so does anything that ends the
//! box without there being anything in it.

const w = @import("ween32");
const A = @import("app.zig");
const undo = @import("undo.zig");
const app = &A.app;

/// The border's dash, and the handles that sit on it: the machine draws four
/// pixels on and four off, and the handles are the selection's three-square.
const dash = 4;
const handle = 3;
const navy = w.RGB(10, 36, 106);

pub const State = struct {
    open: bool = false,
    rect: w.RECT = .{ .left = 0, .top = 0, .right = 0, .bottom = 0 },
    text: [1024]u8 = undefined,
    len: usize = 0,
    caret_on: bool = true,
};

pub var box: State = .{};

/// The face the machine's text tool starts in. There is no Arial here, so
/// the library hands back what it has; asking for it is still what a program
/// written for Windows does.
var font: ?w.HFONT = null;

fn textFont() w.HFONT {
    if (font == null)
        font = w.CreateFontA(-11, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");
    return font.?;
}

pub fn active() bool {
    return box.open;
}

pub fn start(r: w.RECT) void {
    box = .{ .open = true, .rect = r };
    box.len = 0;
    box.caret_on = true;
}

/// One character typed. Backspace rubs out; Enter starts a line.
pub fn typed(ch: u8) void {
    if (!box.open) return;
    if (ch == 8) {
        if (box.len > 0) box.len -= 1;
        box.caret_on = true;
        return;
    }
    if (ch == 27) { // Escape: throw the box away
        box.open = false;
        return;
    }
    if (box.len + 1 < box.text.len) {
        box.text[box.len] = ch;
        box.len += 1;
    }
    box.caret_on = true;
}

/// The box's inside: two pixels in from the border on every side, which is
/// where the machine puts both the fill and the first letter.
fn inside() w.RECT {
    return .{
        .left = box.rect.left + 2,
        .top = box.rect.top + 2,
        .right = box.rect.right - 2,
        .bottom = box.rect.bottom - 2,
    };
}

/// What one line of text is worth, in pixels.
fn lineHeight(dc: w.HDC) i32 {
    var size: w.SIZE = undefined;
    _ = w.GetTextExtentPoint32A(dc, "Ay", 2, &size);
    return size.cy;
}

fn widthOf(dc: w.HDC, s: []const u8) i32 {
    if (s.len == 0) return 0;
    var size: w.SIZE = undefined;
    _ = w.GetTextExtentPoint32A(dc, s.ptr, @intCast(s.len), &size);
    return size.cx;
}

/// Where each line of the typed text begins and ends once it has been broken
/// to the box's width. Returns how many lines there are, and hands each to
/// the caller — a return breaks a line, and so does running out of room,
/// which happens at the last space that still fits.
const Line = struct { from: usize, to: usize };

fn layout(dc: w.HDC, room: i32, out: *[64]Line) usize {
    var lines: usize = 0;
    var at: usize = 0;
    while (at <= box.len and lines < out.len) {
        var end = at;
        var take = at;
        var last_space: ?usize = null;
        while (end < box.len) : (end += 1) {
            const ch = box.text[end];
            if (ch == '\r' or ch == '\n') break;
            if (widthOf(dc, box.text[at .. end + 1]) > room and take > at) {
                // too wide: back up to the last space that fitted
                end = last_space orelse take;
                break;
            }
            if (ch == ' ') last_space = end + 1;
            take = end + 1;
        }
        out[lines] = .{ .from = at, .to = end };
        lines += 1;
        if (end >= box.len) break;
        at = if (box.text[end] == '\r' or box.text[end] == '\n') end + 1 else end;
        // a break at a space swallows the space, as the machine's does
        while (at < box.len and box.text[at] == ' ') at += 1;
    }
    return lines;
}

/// Draw the text — into the window while the box is open, into the picture
/// when it is closed. `with_border` is what tells the two apart.
pub fn draw(dc: w.HDC, with_border: bool) void {
    if (!box.open) return;
    const in = inside();
    const old_font = w.SelectObject(dc, textFont());
    defer if (old_font) |o| {
        _ = w.SelectObject(dc, o);
    };

    // Opaque puts the background colour down first, as it does for a
    // selection; transparent leaves what is under it alone.
    if (A.option() == 0) {
        const brush = w.CreateSolidBrush(app.bg).?;
        var fill = in;
        _ = w.FillRect(dc, &fill, brush);
        _ = w.DeleteObject(brush);
    }

    const height = lineHeight(dc);
    var lines: [64]Line = undefined;
    const n = layout(dc, in.right - in.left, &lines);
    const prev = w.SetTextColor(dc, app.fg);
    _ = w.SetBkMode(dc, w.TRANSPARENT);
    var i: usize = 0;
    while (i < n) : (i += 1) {
        const line = box.text[lines[i].from..lines[i].to];
        if (line.len > 0)
            _ = w.TextOutA(dc, in.left, in.top + @as(i32, @intCast(i)) * height, line.ptr, @intCast(line.len));
    }
    _ = w.SetTextColor(dc, prev);

    if (!with_border) return;

    // The caret, after the last character of the last line.
    if (box.caret_on and n > 0) {
        const last = box.text[lines[n - 1].from..lines[n - 1].to];
        const x = in.left + widthOf(dc, last);
        const y = in.top + @as(i32, @intCast(n - 1)) * height;
        const brush = w.CreateSolidBrush(app.fg).?;
        var caret = w.RECT{ .left = x, .top = y, .right = x + 1, .bottom = y + height };
        _ = w.FillRect(dc, &caret, brush);
        _ = w.DeleteObject(brush);
    }

    drawFrame(dc);
}

/// The dashed border and its eight handles.
fn drawFrame(dc: w.HDC) void {
    const r = box.rect;
    const brush = w.CreateSolidBrush(navy).?;
    defer _ = w.DeleteObject(brush);
    // four on, four off, all the way round
    var x = r.left;
    while (x <= r.right) : (x += 1) {
        if (@rem(x - r.left, dash * 2) < dash) {
            _ = w.SetPixel(dc, x, r.top, navy);
            _ = w.SetPixel(dc, x, r.bottom, navy);
        }
    }
    var y = r.top;
    while (y <= r.bottom) : (y += 1) {
        if (@rem(y - r.top, dash * 2) < dash) {
            _ = w.SetPixel(dc, r.left, y, navy);
            _ = w.SetPixel(dc, r.right, y, navy);
        }
    }
    // and the eight, centred on the border as a selection's are
    const xs = [3]i32{ r.left, @divTrunc(r.left + r.right, 2), r.right };
    const ys = [3]i32{ r.top, @divTrunc(r.top + r.bottom, 2), r.bottom };
    for (ys, 0..) |hy, row| {
        for (xs, 0..) |hx, col| {
            if (row == 1 and col == 1) continue;
            var h = w.RECT{
                .left = hx - @divTrunc(handle, 2),
                .top = hy - @divTrunc(handle, 2),
                .right = hx - @divTrunc(handle, 2) + handle,
                .bottom = hy - @divTrunc(handle, 2) + handle,
            };
            _ = w.FillRect(dc, &h, brush);
        }
    }
}

/// Click away, change tool, or press Escape: what was typed goes into the
/// picture and the box closes.
pub fn commit() void {
    if (!box.open) return;
    if (box.len > 0) {
        undo.take();
        const on = box.caret_on;
        box.caret_on = false; // the caret is not part of the picture
        draw(app.pic.dc, false);
        box.caret_on = on;
    }
    box.open = false;
}
