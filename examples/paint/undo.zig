//! Undo: the last eight states of the picture, kept whole.
//!
//! A drawing program's undo is not a list of operations — a flood fill has no
//! inverse worth writing — so what is kept is the picture itself, copied
//! before anything changes it.
//!
//! The machine keeps three, and three is what everyone who has ever run out
//! of them remembers about it. This keeps eight, which is a deliberate
//! parting from it: the one number in this program chosen because it is
//! better rather than because it is what Paint did. Each one costs a copy of
//! the picture, so a large one costs eight of those — 512x384 is six
//! megabytes for the lot, and nothing is copied until something is drawn.

const w = @import("ween32");
const A = @import("app.zig");
const app = &A.app;

const depth = 8;

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

/// Called before anything changes the picture — which is also the moment
/// the picture stops matching the file it came from.
pub fn take() void {
    app.dirty = true;
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

/// Put a picture back, size and all.
///
/// The size matters as much as the pixels: Attributes, Stretch/Skew and a
/// drag of the page's corner all change it, and an undo that put the old
/// pixels into a picture still the new size left the page the wrong shape
/// with its old contents in the corner.
fn restore(s: *const Snapshot) void {
    if (s.width != app.pic.width or s.height != app.pic.height)
        app.pic.resize(s.width, s.height, app.bg);
    _ = w.BitBlt(app.pic.dc, 0, 0, s.width, s.height, s.dc, 0, 0, w.SRCCOPY);
}

pub fn undo() void {
    if (count == 0) return;
    capture(&redo);
    count -= 1;
    restore(&stack[count]);
}

pub fn repeat() void {
    if (!redo.used) return;
    take();
    restore(&redo);
}

/// A new picture has nothing behind it.
pub fn forget() void {
    count = 0;
    redo.used = false;
}
