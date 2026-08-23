//! The selection: a rectangle of the picture that can be moved, cut, copied
//! and pasted.
//!
//! While a selection is up, the pixels under it have already been lifted out
//! of the picture — Paint replaces them with the background colour the moment
//! the selection is dragged — and what is drawn is the lifted copy floating
//! over the hole. Clicking away drops it back where it lies.

const w = @import("ween32");
const A = @import("app.zig");
const undo = @import("undo.zig");
const picfile = @import("file.zig");
const tools = @import("tools.zig");
const app = &A.app;

pub const Selection = struct {
    live: bool = false,
    /// Where it is now, in picture coordinates.
    rect: w.RECT = .{ .left = 0, .top = 0, .right = 0, .bottom = 0 },
    /// The pixels as they are drawn, kept in a bitmap of their own.
    dc: w.HDC = undefined,
    bmp: w.HBITMAP = undefined,
    /// The same pixels as they were taken, which the drawn copy is made
    /// from again whenever the settings change under it.
    orig: w.HDC = undefined,
    orig_bmp: w.HBITMAP = undefined,
    /// Whether the hole it came from has been filled in yet.
    lifted: bool = false,
    /// Black inside the shape a free-form selection was drawn round and
    /// white outside it. Null for a selection dragged out as a rectangle.
    shape: ?w.HDC = null,
    shape_bmp: w.HBITMAP = undefined,
    /// White everywhere the selection does not show: the shape, and the
    /// holes the transparent setting leaves where the background colour
    /// was. Null when the whole rectangle is solid, which is the ordinary
    /// case and the fast one.
    mask: ?w.HDC = null,
    mask_bmp: w.HBITMAP = undefined,
};

pub var sel: Selection = .{};

pub fn active() bool {
    return sel.live;
}

fn newBitmapDC(width: i32, height: i32, bmp: *w.HBITMAP) w.HDC {
    const screen = w.GetDC(null).?;
    defer _ = w.ReleaseDC(null, screen);
    const dc = w.CreateCompatibleDC(screen).?;
    bmp.* = w.CreateCompatibleBitmap(screen, width, height).?;
    _ = w.SelectObject(dc, bmp.*);
    return dc;
}

fn hold(width: i32, height: i32) void {
    if (sel.live) {
        _ = w.DeleteDC(sel.dc);
        _ = w.DeleteObject(sel.bmp);
        _ = w.DeleteDC(sel.orig);
        _ = w.DeleteObject(sel.orig_bmp);
    }
    dropMask();
    dropShape();
    sel.dc = newBitmapDC(width, height, &sel.bmp);
    sel.orig = newBitmapDC(width, height, &sel.orig_bmp);
    sel.live = true;
    sel.lifted = false;
}

fn dropMask() void {
    if (sel.mask) |m| {
        _ = w.DeleteDC(m);
        _ = w.DeleteObject(sel.mask_bmp);
        sel.mask = null;
    }
}

fn dropShape() void {
    if (sel.shape) |m| {
        _ = w.DeleteDC(m);
        _ = w.DeleteObject(sel.shape_bmp);
        sel.shape = null;
    }
}

/// Whether the selection tools are set to let the background colour
/// through: the second of the two settings under them.
fn transparent() bool {
    return app.option[@intFromEnum(A.Tool.select)] == 1;
}

/// Make the drawn copy from the pixels as taken, and with it the mask that
/// says where it shows: the free-form shape, less anything the transparent
/// setting drops. Called again whenever either could have changed.
pub fn rebuild() void {
    if (!sel.live) return;
    const width = sel.rect.right - sel.rect.left;
    const height = sel.rect.bottom - sel.rect.top;
    if (width <= 0 or height <= 0) return;
    _ = w.BitBlt(sel.dc, 0, 0, width, height, sel.orig, 0, 0, w.SRCCOPY);
    const clear_bg = transparent();
    if (sel.shape == null and !clear_bg) {
        dropMask();
        return;
    }
    dropMask();
    const m = newBitmapDC(width, height, &sel.mask_bmp);
    sel.mask = m;
    if (sel.shape) |shape| {
        _ = w.BitBlt(m, 0, 0, width, height, shape, 0, 0, w.SRCCOPY);
    } else {
        var all = w.RECT{ .left = 0, .top = 0, .right = width, .bottom = height };
        _ = w.FillRect(m, &all, w.GetStockObject(w.BLACK_BRUSH).?);
    }
    if (clear_bg) {
        var y: i32 = 0;
        while (y < height) : (y += 1) {
            var x: i32 = 0;
            while (x < width) : (x += 1) {
                if (w.GetPixel(sel.orig, x, y) == app.bg)
                    _ = w.SetPixel(m, x, y, w.RGB(255, 255, 255));
            }
        }
    }
    // where the mask is white the copy must be black, or the OR that draws
    // it would leave its own pixels showing through
    _ = w.BitBlt(sel.dc, 0, 0, width, height, m, 0, 0, 0x00220326); // DSna
}

/// A selection drawn round rather than dragged out: the lasso's own shape.
pub fn takeFreeForm(pts: []const w.POINT) void {
    if (pts.len < 3) {
        drop();
        return;
    }
    var r = w.RECT{ .left = pts[0].x, .top = pts[0].y, .right = pts[0].x, .bottom = pts[0].y };
    for (pts) |p| {
        r.left = @min(r.left, p.x);
        r.top = @min(r.top, p.y);
        r.right = @max(r.right, p.x);
        r.bottom = @max(r.bottom, p.y);
    }
    // The machine's free-form selection is one pixel wider than the path on
    // every side: the box holds the lasso, not just what it went round.
    r.left = @max(0, r.left - 1);
    r.top = @max(0, r.top - 1);
    r.right = @min(app.pic.width, r.right + 2);
    r.bottom = @min(app.pic.height, r.bottom + 2);
    take(r);
    if (!sel.live) return;

    const width = r.right - r.left;
    const height = r.bottom - r.top;
    const shape = newBitmapDC(width, height, &sel.shape_bmp);
    sel.shape = shape;
    // white outside the shape, black inside it
    var all = w.RECT{ .left = 0, .top = 0, .right = width, .bottom = height };
    _ = w.FillRect(shape, &all, w.GetStockObject(w.WHITE_BRUSH).?);
    var local: [tools.max_lasso]w.POINT = undefined;
    const n = @min(pts.len, tools.max_lasso);
    for (pts[0..n], 0..) |p, i| local[i] = .{ .x = p.x - r.left, .y = p.y - r.top };
    const pen = w.CreatePen(w.PS_SOLID, 1, w.RGB(0, 0, 0)).?;
    const op = w.SelectObject(shape, pen);
    const ob = w.SelectObject(shape, w.GetStockObject(w.BLACK_BRUSH).?);
    _ = w.Polygon(shape, &local, @intCast(n));
    if (op) |o| _ = w.SelectObject(shape, o);
    if (ob) |o| _ = w.SelectObject(shape, o);
    _ = w.DeleteObject(pen);
    rebuild();
}

pub fn take(r: w.RECT) void {
    const width = r.right - r.left;
    const height = r.bottom - r.top;
    if (width <= 0 or height <= 0) {
        drop();
        return;
    }
    hold(width, height);
    _ = w.BitBlt(sel.orig, 0, 0, width, height, app.pic.dc, r.left, r.top, w.SRCCOPY);
    sel.rect = r;
    rebuild();
}

pub fn selectAll() void {
    take(.{ .left = 0, .top = 0, .right = app.pic.width, .bottom = app.pic.height });
    app.tool = .select;
    _ = w.InvalidateRect(app.toolbox, null, w.FALSE);
}

/// Draw the floating pixels into a context, through the mask if there is
/// one: the picture underneath shows wherever the mask is white.
fn stamp(dc: w.HDC, x: i32, y: i32) void {
    const width = sel.rect.right - sel.rect.left;
    const height = sel.rect.bottom - sel.rect.top;
    if (sel.mask) |m| {
        _ = w.BitBlt(dc, x, y, width, height, m, 0, 0, w.SRCAND);
        _ = w.BitBlt(dc, x, y, width, height, sel.dc, 0, 0, w.SRCPAINT);
    } else {
        _ = w.BitBlt(dc, x, y, width, height, sel.dc, 0, 0, w.SRCCOPY);
    }
}

/// Put the floating pixels back into the picture and forget the selection.
pub fn drop() void {
    if (sel.live and sel.lifted) {
        undo.take();
        stamp(app.pic.dc, sel.rect.left, sel.rect.top);
    }
    if (sel.live) free();
    sel.live = false;
    sel.lifted = false;
}

fn free() void {
    _ = w.DeleteDC(sel.dc);
    _ = w.DeleteObject(sel.bmp);
    _ = w.DeleteDC(sel.orig);
    _ = w.DeleteObject(sel.orig_bmp);
    dropMask();
    dropShape();
}

/// The first time a selection is moved its old place is filled with the
/// background colour: from then on it is floating. Held Control leaves the
/// old place alone, which is how a selection is copied.
pub fn lift() void {
    if (!sel.live or sel.lifted) return;
    undo.take();
    if (w.GetKeyState(w.VK_CONTROL) >= 0) { // the high bit is "held"
        const brush = w.CreateSolidBrush(app.bg).?;
        if (sel.shape) |shape| {
            // a free-form selection leaves a hole its own shape: the
            // background goes down through the mask, not over the rectangle
            const width = sel.rect.right - sel.rect.left;
            const height = sel.rect.bottom - sel.rect.top;
            const old = w.SelectObject(app.pic.dc, brush);
            // black inside the shape: PATCOPY there, dst everywhere else
            _ = w.BitBlt(app.pic.dc, sel.rect.left, sel.rect.top, width, height, shape, 0, 0, 0x00B8074A); // PSDPxax
            if (old) |o| _ = w.SelectObject(app.pic.dc, o);
        } else {
            _ = w.FillRect(app.pic.dc, &sel.rect, brush);
        }
        _ = w.DeleteObject(brush);
    }
    sel.lifted = true;
}

pub fn moveBy(dx: i32, dy: i32) void {
    if (!sel.live) return;
    lift();
    sel.rect.left += dx;
    sel.rect.right += dx;
    sel.rect.top += dy;
    sel.rect.bottom += dy;
}

// ---- the border round it --------------------------------------------------

/// How far outside the selection the dashed rectangle sits, measured off the
/// machine: two pixels of picture between the selection and the line.
pub const border_gap = 2;

/// The eight handles are three pixels square.
pub const handle = 3;

const highlight = w.RGB(10, 36, 106);

/// The rectangle the border is drawn on, in picture coordinates and
/// inclusive: the line itself, not what it encloses.
pub fn borderRect() w.RECT {
    return .{
        .left = sel.rect.left - border_gap,
        .top = sel.rect.top - border_gap,
        .right = sel.rect.right - 1 + border_gap,
        .bottom = sel.rect.bottom - 1 + border_gap,
    };
}

/// Where each of the eight handles sits: the four corners, on the line, and
/// the four edge middles, whose top-left corner is the middle of the span.
pub fn handleRect(i: usize) w.RECT {
    const b = borderRect();
    const mx = @divTrunc(b.left + b.right, 2);
    const my = @divTrunc(b.top + b.bottom, 2);
    const x = switch (i % 3) {
        0 => b.left - 1,
        1 => mx,
        else => b.right - 1,
    };
    const y = switch (i / 3) {
        0 => b.top - 1,
        1 => my,
        else => b.bottom - 1,
    };
    return .{ .left = x, .top = y, .right = x + handle, .bottom = y + handle };
}

/// Which handle is under a point, if any: 0..7 clockwise from the top left,
/// with the middle of the nine skipped.
pub fn handleAt(p: w.POINT) ?usize {
    if (!sel.live) return null;
    for (0..9) |i| {
        if (i == 4) continue;
        const r = handleRect(i);
        if (p.x >= r.left and p.x < r.right and p.y >= r.top and p.y < r.bottom)
            return i;
    }
    return null;
}

/// The marching ants: a four-on four-off dashed line in the highlight colour
/// and white. It is a four-by-four chequer anchored to the view's own
/// corner, not to the selection — which is why the dashes stay put when the
/// selection is dragged over them, and how the machine draws it.
fn ant(x: i32, y: i32, origin: w.POINT) w.COLORREF {
    const cx = x + origin.x;
    const cy = y + origin.y;
    const on = (@divFloor(cx, 4) + @divFloor(cy, 4)) & 1 != 0;
    return if (on) highlight else w.RGB(255, 255, 255);
}

/// Draw the floating pixels and the border round them. The origin is where
/// the picture sits in the view, which the pattern is aligned to.
pub fn draw(dc: w.HDC, origin: w.POINT) void {
    if (!sel.live) return;
    if (sel.lifted) stamp(dc, sel.rect.left, sel.rect.top);
    const b = borderRect();
    var x = b.left;
    while (x <= b.right) : (x += 1) {
        _ = w.SetPixel(dc, x, b.top, ant(x, b.top, origin));
        _ = w.SetPixel(dc, x, b.bottom, ant(x, b.bottom, origin));
    }
    var y = b.top;
    while (y <= b.bottom) : (y += 1) {
        _ = w.SetPixel(dc, b.left, y, ant(b.left, y, origin));
        _ = w.SetPixel(dc, b.right, y, ant(b.right, y, origin));
    }
    const brush = w.CreateSolidBrush(highlight).?;
    for (0..9) |i| {
        if (i == 4) continue;
        var r = handleRect(i);
        _ = w.FillRect(dc, &r, brush);
    }
    _ = w.DeleteObject(brush);
}

/// Drag a handle: the floating pixels are stretched into the new rectangle,
/// which is what the handles are for.
pub fn resizeTo(r: w.RECT) void {
    if (!sel.live) return;
    const width = r.right - r.left;
    const height = r.bottom - r.top;
    if (width <= 0 or height <= 0) return;
    lift();
    const screen = w.GetDC(null).?;
    defer _ = w.ReleaseDC(null, screen);
    var bmp: w.HBITMAP = undefined;
    const dst = newBitmapDC(width, height, &bmp);
    _ = w.StretchBlt(dst, 0, 0, width, height, sel.orig, 0, 0,
        sel.rect.right - sel.rect.left, sel.rect.bottom - sel.rect.top, w.SRCCOPY);
    // the shape, if there is one, stretches with the pixels
    if (sel.shape) |shape| {
        var sbmp: w.HBITMAP = undefined;
        const sdc = newBitmapDC(width, height, &sbmp);
        _ = w.StretchBlt(sdc, 0, 0, width, height, shape, 0, 0,
            sel.rect.right - sel.rect.left, sel.rect.bottom - sel.rect.top, w.SRCCOPY);
        dropShape();
        sel.shape = sdc;
        sel.shape_bmp = sbmp;
    }
    _ = w.DeleteDC(sel.orig);
    _ = w.DeleteObject(sel.orig_bmp);
    sel.orig = dst;
    sel.orig_bmp = bmp;
    _ = w.DeleteDC(sel.dc);
    _ = w.DeleteObject(sel.bmp);
    sel.dc = newBitmapDC(width, height, &sel.bmp);
    sel.rect = r;
    rebuild();
}


// ---- the clipboard --------------------------------------------------------

pub fn copy() void {
    if (!sel.live) return;
    const width = sel.rect.right - sel.rect.left;
    const height = sel.rect.bottom - sel.rect.top;
    const screen = w.GetDC(null).?;
    defer _ = w.ReleaseDC(null, screen);
    const bmp = w.CreateCompatibleBitmap(screen, width, height).?;
    const dc = w.CreateCompatibleDC(screen).?;
    _ = w.SelectObject(dc, bmp);
    _ = w.BitBlt(dc, 0, 0, width, height, if (sel.lifted) sel.orig else app.pic.dc, if (sel.lifted) 0 else sel.rect.left, if (sel.lifted) 0 else sel.rect.top, w.SRCCOPY);
    _ = w.DeleteDC(dc);
    if (w.OpenClipboard(app.frame) != 0) {
        _ = w.EmptyClipboard();
        _ = w.SetClipboardData(w.CF_BITMAP, bmp);
        _ = w.CloseClipboard();
    }
}

pub fn cut() void {
    if (!sel.live) return;
    copy();
    clear();
}

/// Delete: the selection goes and its place fills with the background.
pub fn clear() void {
    if (!sel.live) return;
    lift();
    free();
    sel.live = false;
    sel.lifted = false;
}

/// Edit > Paste From: a picture off the disk arrives as a floating
/// selection, which is what Paste does with the clipboard.
pub fn pasteFrom(path: []const u8) void {
    var side = A.Picture{};
    side.create(1, 1);
    const keep = app.pic;
    app.pic = side;
    picfile.open(path) catch {
        app.pic = keep;
        return;
    };
    side = app.pic;
    app.pic = keep;
    drop();
    hold(side.width, side.height);
    _ = w.BitBlt(sel.orig, 0, 0, side.width, side.height, side.dc, 0, 0, w.SRCCOPY);
    sel.rect = .{ .left = 0, .top = 0, .right = side.width, .bottom = side.height };
    sel.lifted = true;
    rebuild();
    app.tool = .select;
    _ = w.InvalidateRect(app.toolbox, null, w.FALSE);
}

/// Edit > Copy To: the selection, written out as its own picture.
pub fn copyTo(path: []const u8) void {
    if (!sel.live) return;
    const width = sel.rect.right - sel.rect.left;
    const height = sel.rect.bottom - sel.rect.top;
    const keep = app.pic;
    var side = A.Picture{};
    side.create(width, height);
    _ = w.BitBlt(side.dc, 0, 0, width, height, if (sel.lifted) sel.orig else app.pic.dc, if (sel.lifted) 0 else sel.rect.left, if (sel.lifted) 0 else sel.rect.top, w.SRCCOPY);
    app.pic = side;
    picfile.save(path) catch {};
    app.pic = keep;
    _ = w.DeleteDC(side.dc);
    _ = w.DeleteObject(side.bmp);
}

/// Paste: whatever is on the clipboard arrives as a selection in the corner.
pub fn paste() void {
    if (w.IsClipboardFormatAvailable(w.CF_BITMAP) == 0) return;
    if (w.OpenClipboard(app.frame) == 0) return;
    defer _ = w.CloseClipboard();
    const bmp = w.GetClipboardData(w.CF_BITMAP) orelse return;
    var info: w.BITMAP = .{};
    if (w.GetObjectA(@ptrCast(bmp), @sizeOf(w.BITMAP), &info) == 0) return;
    drop();
    hold(info.bmWidth, info.bmHeight);
    const screen = w.GetDC(null).?;
    defer _ = w.ReleaseDC(null, screen);
    const src = w.CreateCompatibleDC(screen).?;
    const old = w.SelectObject(src, @ptrCast(bmp));
    _ = w.BitBlt(sel.orig, 0, 0, info.bmWidth, info.bmHeight, src, 0, 0, w.SRCCOPY);
    if (old) |o| _ = w.SelectObject(src, o);
    _ = w.DeleteDC(src);
    sel.rect = .{ .left = 0, .top = 0, .right = info.bmWidth, .bottom = info.bmHeight };
    sel.lifted = true; // it is floating from the start: nothing was lifted
    rebuild();
    app.tool = .select;
    _ = w.InvalidateRect(app.toolbox, null, w.FALSE);
}
