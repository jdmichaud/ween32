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
const app = &A.app;

pub const Selection = struct {
    live: bool = false,
    /// Where it is now, in picture coordinates.
    rect: w.RECT = .{ .left = 0, .top = 0, .right = 0, .bottom = 0 },
    /// The pixels, kept in a bitmap of their own.
    dc: w.HDC = undefined,
    bmp: w.HBITMAP = undefined,
    /// Whether the hole it came from has been filled in yet.
    lifted: bool = false,
};

pub var sel: Selection = .{};

pub fn active() bool {
    return sel.live;
}

fn hold(width: i32, height: i32) void {
    const screen = w.GetDC(null).?;
    defer _ = w.ReleaseDC(null, screen);
    if (sel.live) {
        _ = w.DeleteDC(sel.dc);
        _ = w.DeleteObject(sel.bmp);
    }
    sel.dc = w.CreateCompatibleDC(screen).?;
    sel.bmp = w.CreateCompatibleBitmap(screen, width, height).?;
    _ = w.SelectObject(sel.dc, sel.bmp);
    sel.live = true;
    sel.lifted = false;
}

/// Take a rectangle of the picture as the selection.
pub fn take(r: w.RECT) void {
    const width = r.right - r.left;
    const height = r.bottom - r.top;
    if (width <= 0 or height <= 0) {
        drop();
        return;
    }
    hold(width, height);
    _ = w.BitBlt(sel.dc, 0, 0, width, height, app.pic.dc, r.left, r.top, w.SRCCOPY);
    sel.rect = r;
}

pub fn selectAll() void {
    take(.{ .left = 0, .top = 0, .right = app.pic.width, .bottom = app.pic.height });
    app.tool = .select;
    _ = w.InvalidateRect(app.toolbox, null, w.FALSE);
}

/// Put the floating pixels back into the picture and forget the selection.
pub fn drop() void {
    if (sel.live and sel.lifted) {
        undo.take();
        _ = w.BitBlt(app.pic.dc, sel.rect.left, sel.rect.top, sel.rect.right - sel.rect.left, sel.rect.bottom - sel.rect.top, sel.dc, 0, 0, w.SRCCOPY);
    }
    if (sel.live) {
        _ = w.DeleteDC(sel.dc);
        _ = w.DeleteObject(sel.bmp);
    }
    sel.live = false;
    sel.lifted = false;
}

/// The first time a selection is moved its old place is filled with the
/// background colour: from then on it is floating.
pub fn lift() void {
    if (!sel.live or sel.lifted) return;
    undo.take();
    const brush = w.CreateSolidBrush(app.bg).?;
    _ = w.FillRect(app.pic.dc, &sel.rect, brush);
    _ = w.DeleteObject(brush);
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

/// Draw the floating pixels and the dashed rectangle round them.
pub fn draw(dc: w.HDC) void {
    if (!sel.live) return;
    const width = sel.rect.right - sel.rect.left;
    const height = sel.rect.bottom - sel.rect.top;
    if (sel.lifted)
        _ = w.BitBlt(dc, sel.rect.left, sel.rect.top, width, height, sel.dc, 0, 0, w.SRCCOPY);
    var r = sel.rect;
    _ = w.DrawFocusRect(dc, &r);
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
    _ = w.BitBlt(dc, 0, 0, width, height, if (sel.lifted) sel.dc else app.pic.dc, if (sel.lifted) 0 else sel.rect.left, if (sel.lifted) 0 else sel.rect.top, w.SRCCOPY);
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
    _ = w.DeleteDC(sel.dc);
    _ = w.DeleteObject(sel.bmp);
    sel.live = false;
    sel.lifted = false;
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
    _ = w.BitBlt(sel.dc, 0, 0, info.bmWidth, info.bmHeight, src, 0, 0, w.SRCCOPY);
    if (old) |o| _ = w.SelectObject(src, o);
    _ = w.DeleteDC(src);
    sel.rect = .{ .left = 0, .top = 0, .right = info.bmWidth, .bottom = info.bmHeight };
    sel.lifted = true; // it is floating from the start: nothing was lifted
    app.tool = .select;
    _ = w.InvalidateRect(app.toolbox, null, w.FALSE);
}
