//! The text tool: a box dragged out on the picture, typed into, and drawn
//! into the picture when you click away from it.
//!
//! While it is open the text is drawn over the picture rather than into it,
//! with a dashed border round the box and a caret at the end — so it can be
//! backspaced and retyped. Enter starts a new line; Escape throws it away.

const w = @import("ween32");
const A = @import("app.zig");
const undo = @import("undo.zig");
const app = &A.app;

pub const State = struct {
    open: bool = false,
    rect: w.RECT = .{ .left = 0, .top = 0, .right = 0, .bottom = 0 },
    text: [1024]u8 = undefined,
    len: usize = 0,
    caret_on: bool = true,
};

pub var box: State = .{};

pub fn active() bool {
    return box.open;
}

pub fn start(r: w.RECT) void {
    box = .{ .open = true, .rect = r };
    box.len = 0;
}

/// One character typed. Backspace rubs out; Enter starts a line.
pub fn typed(ch: u8) void {
    if (!box.open) return;
    if (ch == 8) {
        if (box.len > 0) box.len -= 1;
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
}

/// The lines of what has been typed, split on the returns.
fn eachLine(comptime f: fn (line: []const u8, index: usize, ctx: *anyopaque) void, ctx: *anyopaque) void {
    var start_at: usize = 0;
    var index: usize = 0;
    var i: usize = 0;
    while (i <= box.len) : (i += 1) {
        if (i == box.len or box.text[i] == '\r' or box.text[i] == '\n') {
            f(box.text[start_at..i], index, ctx);
            index += 1;
            start_at = i + 1;
        }
    }
}

const DrawCtx = struct { dc: w.HDC, height: i32 };

fn drawLine(line: []const u8, index: usize, ctx: *anyopaque) void {
    const c: *DrawCtx = @ptrCast(@alignCast(ctx));
    if (line.len == 0) return;
    _ = w.TextOutA(c.dc, box.rect.left + 1, box.rect.top + 1 + @as(i32, @intCast(index)) * c.height, line.ptr, @intCast(line.len));
}

/// Draw the text — into the window while the box is open, into the picture
/// when it is closed.
pub fn draw(dc: w.HDC, with_border: bool) void {
    if (!box.open) return;
    var size: w.SIZE = undefined;
    _ = w.GetTextExtentPoint32A(dc, "Ay", 2, &size);
    const prev = w.SetTextColor(dc, app.fg);
    _ = w.SetBkMode(dc, w.TRANSPARENT);
    var ctx = DrawCtx{ .dc = dc, .height = size.cy };
    eachLine(drawLine, &ctx);
    _ = w.SetTextColor(dc, prev);
    if (with_border) {
        var r = box.rect;
        _ = w.DrawFocusRect(dc, &r);
    }
}

/// Click away, change tool, or press Escape: what was typed goes into the
/// picture and the box closes.
pub fn commit() void {
    if (!box.open) return;
    if (box.len > 0) {
        undo.take();
        draw(app.pic.dc, false);
    }
    box.open = false;
}
