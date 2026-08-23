//! Undo, as Paint has it: the last three states of the picture, kept whole.
//!
//! A drawing program's undo is not a list of operations — a flood fill has no
//! inverse worth writing — so what is kept is the picture itself, copied
//! before anything changes it. Three of them, which is what Paint keeps.

const w = @import("ween32");
const A = @import("app.zig");
const app = &A.app;

const depth = 3;

const Snapshot = struct {
    dc: w.HDC = undefined,
    bmp: w.HBITMAP = undefined,
    width: i32 = 0,
    height: i32 = 0,
    used: bool = false,
};

var stack: [depth]Snapshot = @splat(.{});
var count: usize = 0;
/// What Undo took off, so Repeat can put it back.
var redo: Snapshot = .{};

fn capture(into: *Snapshot) void {
    const screen = w.GetDC(null).?;
    defer _ = w.ReleaseDC(null, screen);
    if (into.used and (into.width != app.pic.width or into.height != app.pic.height)) {
        _ = w.DeleteDC(into.dc);
        _ = w.DeleteObject(into.bmp);
        into.used = false;
    }
    if (!into.used) {
        into.dc = w.CreateCompatibleDC(screen).?;
        into.bmp = w.CreateCompatibleBitmap(screen, app.pic.width, app.pic.height).?;
        _ = w.SelectObject(into.dc, into.bmp);
        into.width = app.pic.width;
        into.height = app.pic.height;
        into.used = true;
    }
    _ = w.BitBlt(into.dc, 0, 0, app.pic.width, app.pic.height, app.pic.dc, 0, 0, w.SRCCOPY);
}

/// Called before anything changes the picture.
pub fn take() void {
    if (count == depth) {
        // the oldest goes; the rest shuffle down
        const oldest = stack[0];
        var i: usize = 0;
        while (i + 1 < depth) : (i += 1) stack[i] = stack[i + 1];
        stack[depth - 1] = oldest;
        count -= 1;
    }
    capture(&stack[count]);
    count += 1;
}

pub fn canUndo() bool {
    return count > 0;
}

pub fn canRedo() bool {
    return redo.used;
}

pub fn undo() void {
    if (count == 0) return;
    capture(&redo);
    count -= 1;
    const s = &stack[count];
    _ = w.BitBlt(app.pic.dc, 0, 0, s.width, s.height, s.dc, 0, 0, w.SRCCOPY);
}

pub fn repeat() void {
    if (!redo.used) return;
    take();
    _ = w.BitBlt(app.pic.dc, 0, 0, redo.width, redo.height, redo.dc, 0, 0, w.SRCCOPY);
}

/// A new picture has nothing behind it.
pub fn forget() void {
    count = 0;
    redo.used = false;
}
