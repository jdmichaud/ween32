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
const selection = @import("selection.zig");
const cursors = @import("cursors.zig");
const textbox = @import("textbox.zig");

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
/// The picture's size on the screen, which is its own size times the zoom.
pub fn shownWidth() i32 {
    return app.pic.width * app.zoom;
}

pub fn shownHeight() i32 {
    return app.pic.height * app.zoom;
}

pub fn updateScroll(hwnd: w.HWND) void {
    const cr = clientSize(hwnd);
    // The page a bar shows is the window less the margin the picture is
    // inset by: scrolled to the end, the far edge of the picture lands on
    // the far edge of the window, and the thumb is the length the machine's
    // is.
    var si = w.SCROLLINFO{
        .fMask = w.SIF_RANGE | w.SIF_PAGE | w.SIF_POS,
        .nMin = 0,
        .nMax = shownWidth() - 1,
        .nPage = @intCast(@max(0, cr.right - margin)),
        .nPos = app.scroll_x,
    };
    _ = w.SetScrollInfo(hwnd, w.SB_HORZ, &si, w.TRUE);
    si.nMax = shownHeight() - 1;
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
    const xs = [3]i32{ o.x - handle, o.x + @divTrunc(shownWidth(), 2) - 1, o.x + shownWidth() };
    const ys = [3]i32{ o.y - handle, o.y + @divTrunc(shownHeight(), 2) - 1, o.y + shownHeight() };
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

/// Dragging one of the three live handles round the page: which one, and the
/// size the picture would become. The picture itself does not change until
/// the button comes up — until then it is a dotted rectangle, which is what
/// the machine shows.
var page_sizing: ?u8 = null;
/// A drag through the letters of an open text box, which picks out the run
/// it passes over; or the box being carried by its border or stretched by
/// one of its handles, which is how the machine's is moved and resized.
var box_pick = false;
var box_handle: ?usize = null;
var box_carry: ?w.POINT = null;
var page_size: w.POINT = .{ .x = 0, .y = 0 };

const page_handle = struct {
    const right = 0; // the width follows the pointer
    const bottom = 1; // the height does
    const corner = 2; // both
};

/// Which live handle is under a point in client coordinates, if any. The
/// five hollow ones are not answers: Paint draws them to say they do nothing.
fn liveHandleAt(pt: w.POINT) ?u8 {
    const o = pageOrigin();
    const rx = o.x + shownWidth();
    const by = o.y + shownHeight();
    const mx = o.x + @divTrunc(shownWidth(), 2) - 1;
    const my = o.y + @divTrunc(shownHeight(), 2) - 1;
    const at = struct {
        fn in(q: w.POINT, hx: i32, hy: i32) bool {
            return q.x >= hx and q.x < hx + handle and q.y >= hy and q.y < hy + handle;
        }
    };
    if (at.in(pt, rx, by)) return page_handle.corner;
    if (at.in(pt, rx, my)) return page_handle.right;
    if (at.in(pt, mx, by)) return page_handle.bottom;
    return null;
}

/// The rubber band, in client coordinates: the page's corner to where the
/// pointer has taken the size.
fn pagePreviewRect() w.RECT {
    const o = pageOrigin();
    return .{
        .left = o.x,
        .top = o.y,
        .right = o.x + page_size.x * app.zoom,
        .bottom = o.y + page_size.y * app.zoom,
    };
}

/// What a drag of that handle makes of the picture, in its own pixels: the
/// pointer says where the edges it owns are, and one pixel is the least a
/// picture can be.
fn pageSizeFor(h: u8, pt: w.POINT) w.POINT {
    const o = pageOrigin();
    var size = w.POINT{ .x = app.pic.width, .y = app.pic.height };
    if (h != page_handle.bottom)
        size.x = @max(1, @divTrunc(pt.x - o.x, app.zoom));
    if (h != page_handle.right)
        size.y = @max(1, @divTrunc(pt.y - o.y, app.zoom));
    return size;
}

/// The four edges of a rectangle in client coordinates, and nothing in the
/// middle: a rubber band round the whole page is a frame, not a page.
fn invalidateBand(hwnd: w.HWND, r: w.RECT) void {
    const edges = [_]w.RECT{
        .{ .left = r.left - 1, .top = r.top - 1, .right = r.right + 1, .bottom = r.top + 1 },
        .{ .left = r.left - 1, .top = r.bottom - 1, .right = r.right + 1, .bottom = r.bottom + 1 },
        .{ .left = r.left - 1, .top = r.top - 1, .right = r.left + 1, .bottom = r.bottom + 1 },
        .{ .left = r.right - 1, .top = r.top - 1, .right = r.right + 1, .bottom = r.bottom + 1 },
    };
    for (edges) |e| {
        var q = e;
        _ = w.InvalidateRect(hwnd, &q, w.FALSE);
    }
}

/// Where an open text box is on the window, so the caret can blink without
/// repainting the picture behind it.
fn invalidateBox(hwnd: w.HWND) void {
    const o = pageOrigin();
    var r = textbox.box.rect;
    r.left = o.x + r.left * app.zoom - 2;
    r.top = o.y + r.top * app.zoom - 2;
    r.right = o.x + r.right * app.zoom + 2;
    r.bottom = o.y + r.bottom * app.zoom + 2;
    _ = w.InvalidateRect(hwnd, &r, w.FALSE);
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
    r = .{ .left = o.x + shownWidth(), .top = o.y, .right = cr.right, .bottom = cr.bottom };
    _ = w.FillRect(dc, &r, grey);
    r = .{ .left = o.x, .top = o.y + shownHeight(), .right = o.x + shownWidth(), .bottom = cr.bottom };
    _ = w.FillRect(dc, &r, grey);

    if (app.zoom == 1) {
        _ = w.BitBlt(dc, o.x, o.y, app.pic.width, app.pic.height, app.pic.dc, 0, 0, w.SRCCOPY);
    } else {
        _ = w.StretchBlt(dc, o.x, o.y, shownWidth(), shownHeight(), app.pic.dc, 0, 0, app.pic.width, app.pic.height, w.SRCCOPY);
        if (app.grid and app.zoom >= 4) drawGrid(dc, o);
    }

    // Everything outside the picture goes down first, because what comes
    // after is clipped to the picture and could not draw it: the handles
    // round the page, and the rubber band of a drag of one of them, which is
    // the one thing here that belongs outside.
    drawHandles(dc);
    if (page_sizing != null) {
        var pr = pagePreviewRect();
        _ = w.DrawFocusRect(dc, &pr);
    }

    // A drag that leaves the picture is clipped at its edge, as it is on the
    // machine: without this a rectangle dragged out to the left is drawn
    // across the tool box, and stays there until something repaints it.
    _ = w.IntersectClipRect(dc, o.x, o.y, o.x + shownWidth(), o.y + shownHeight());
    if (app.zoom == 1) {
        // What the drag is making, drawn over the picture but not into it:
        // the origin moves to the picture's, so the tools work in its
        // coordinates whichever context they are handed.
        _ = w.SetViewportOrgEx(dc, o.x, o.y, null);
        selection.draw(dc, o);
        if (sizing != null) {
            var sr = size_rect;
            _ = w.DrawFocusRect(dc, &sr);
        }
        // The box lengthens under what has been typed; if it just has, what
        // is under it has to be painted again.
        if (textbox.reflow(dc)) {
            _ = w.SetViewportOrgEx(dc, 0, 0, null);
            _ = w.EndPaint(hwnd, &ps);
            invalidateBox(hwnd);
            return;
        }
        textbox.draw(dc, true);
        tools.drawDrag(dc);
        _ = w.SetViewportOrgEx(dc, 0, 0, null);
    }
    _ = w.EndPaint(hwnd, &ps);
}

/// The grid a magnified view can show: one line between each pair of
/// picture pixels, which Paint offers from four times up.
fn drawGrid(dc: w.HDC, o: w.POINT) void {
    const grey = w.CreateSolidBrush(w.RGB(192, 192, 192)).?;
    defer _ = w.DeleteObject(grey);
    var i: i32 = 0;
    while (i <= app.pic.width) : (i += 1) {
        const r = w.RECT{ .left = o.x + i * app.zoom, .top = o.y, .right = o.x + i * app.zoom + 1, .bottom = o.y + shownHeight() };
        _ = w.FillRect(dc, &r, grey);
    }
    i = 0;
    while (i <= app.pic.height) : (i += 1) {
        const r = w.RECT{ .left = o.x, .top = o.y + i * app.zoom, .right = o.x + shownWidth(), .bottom = o.y + i * app.zoom + 1 };
        _ = w.FillRect(dc, &r, grey);
    }
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
fn clientPoint(lp: w.LPARAM) w.POINT {
    return .{ .x = w.GET_X_LPARAM(lp), .y = w.GET_Y_LPARAM(lp) };
}

fn toImage(lp: w.LPARAM) w.POINT {
    const o = pageOrigin();
    return .{
        .x = @divFloor(w.GET_X_LPARAM(lp) - o.x, app.zoom),
        .y = @divFloor(w.GET_Y_LPARAM(lp) - o.y, app.zoom),
    };
}

fn buttonDown(hwnd: w.HWND, right: bool, wp: w.WPARAM, lp: w.LPARAM) void {
    const p = toImage(lp);
    const d = &tools.drag;
    if (d.active) return;
    // The live handles sit outside the picture, so a press on one is not a
    // press with a tool: it starts sizing the picture itself.
    if (!right and page_sizing == null) {
        if (liveHandleAt(clientPoint(lp))) |h| {
            if (textbox.active()) textbox.commit();
            page_sizing = h;
            page_size = .{ .x = app.pic.width, .y = app.pic.height };
            _ = w.SetCapture(hwnd);
            _ = w.SetFocus(hwnd);
            _ = w.InvalidateRect(hwnd, null, w.FALSE);
            return;
        }
    }
    // An open text box has the press before any tool does, and none of what
    // follows belongs to it: a handle stretches the box, the border between
    // them carries it, and a press in the letters puts the caret there and
    // starts picking them out. Further out the box is finished and the tool
    // has the drag. Set the tool's drag up first and these gestures leave it
    // half-begun, which swallowed the press after each of them.
    if (!right and textbox.active()) {
        if (textbox.handleAt(p)) |h| {
            box_handle = h;
            _ = w.SetCapture(hwnd);
            _ = w.SetFocus(hwnd);
            return;
        }
        if (textbox.onBorder(p)) {
            box_carry = p;
            _ = w.SetCapture(hwnd);
            _ = w.SetFocus(hwnd);
            return;
        }
        if (textbox.holds(p)) {
            // Shift keeps the far end where it is, so a press picks out
            // everything between there and here -- as it does in a field.
            const dc = w.GetDC(hwnd).?;
            textbox.place(p, dc, (wp & w.MK_SHIFT) != 0);
            _ = w.ReleaseDC(hwnd, dc);
            box_pick = true;
            _ = w.SetCapture(hwnd);
            _ = w.SetFocus(hwnd);
            invalidateBox(hwnd);
            return;
        }
        textbox.commit();
        _ = w.InvalidateRect(hwnd, null, w.FALSE);
    }

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
    _ = w.SetFocus(hwnd); // the view takes the keyboard: the text tool needs it

    // A press inside a selection picks it up and moves it; one outside puts
    // it down where it lies and starts a new one.
    if (app.tool == .select or app.tool == .free_select) {
        const s2 = &selection.sel;
        // a press on one of the eight handles stretches it instead
        if (selection.handleAt(p)) |h| {
            sizing = h;
            size_rect = s2.rect;
            d.count = 0;
            return;
        }
        if (s2.live and p.x >= s2.rect.left and p.x < s2.rect.right and
            p.y >= s2.rect.top and p.y < s2.rect.bottom)
        {
            moving = true;
            d.count = 0; // nothing to rubber-band: this is a move
            return;
        }
        selection.drop();
        moving = false;
        if (app.tool == .free_select) {
            tools.lasso_n = 0;
            tools.lassoAdd(p);
        }
    }

    switch (app.tool) {
        .pencil, .brush, .eraser, .airbrush => {
            undo.take();
            tools.stroke(app.pic.dc, p, p);
            if (app.tool == .airbrush) _ = w.SetTimer(hwnd, spray_timer, 100, null);
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

/// Whether the drag under way is moving a selection rather than making one.
var moving = false;

/// Which handle is being dragged, and what the selection would become.
var sizing: ?usize = null;
var size_rect: w.RECT = .{ .left = 0, .top = 0, .right = 0, .bottom = 0 };

fn mouseMove(hwnd: w.HWND, wp: w.WPARAM, lp: w.LPARAM) void {
    const d = &tools.drag;
    const p = toImage(lp);
    if (box_handle) |h| {
        invalidateBox(hwnd); // where the box was
        textbox.dragHandle(h, p);
        invalidateBox(hwnd); // and where it is now
        return;
    }
    if (box_carry) |from| {
        invalidateBox(hwnd);
        textbox.moveBy(p.x - from.x, p.y - from.y);
        box_carry = p;
        invalidateBox(hwnd);
        return;
    }
    if (box_pick) {
        const dc = w.GetDC(hwnd).?;
        textbox.place(p, dc, true);
        _ = w.ReleaseDC(hwnd, dc);
        invalidateBox(hwnd);
        return;
    }
    if (page_sizing) |h| {
        const size = pageSizeFor(h, clientPoint(lp));
        if (size.x == page_size.x and size.y == page_size.y) return;
        invalidateBand(hwnd, pagePreviewRect()); // where the band was
        page_size = size;
        invalidateBand(hwnd, pagePreviewRect()); // and where it is now
        return;
    }
    if (sizing) |h| {
        if (!d.active) return;
        var r = selection.sel.rect;
        // the edges the handle owns follow the pointer; the others stay
        if (h % 3 == 0) r.left = @min(p.x, r.right - 1);
        if (h % 3 == 2) r.right = @max(p.x + 1, r.left + 1);
        if (h / 3 == 0) r.top = @min(p.y, r.bottom - 1);
        if (h / 3 == 2) r.bottom = @max(p.y + 1, r.top + 1);
        size_rect = r;
        d.cur = p;
        invalidatePreview(hwnd);
        return;
    }
    if (moving and d.active) {
        invalidatePreview(hwnd); // where it was
        selection.moveBy(p.x - d.last.x, p.y - d.last.y);
        d.last = p;
        d.cur = p;
        invalidatePreview(hwnd); // and where it is now
        return;
    }
    if (!d.active) {
        // a curve or a polygon in the middle of being made follows the
        // pointer even with the button up
        if (d.stage > 0 or (d.tool == .polygon and d.count > 0)) {
            d.cur = p;
            invalidatePreview(hwnd);
        }
        return;
    }
    d.shift = (wp & w.MK_SHIFT) != 0;
    d.cur = p;
    if (d.tool == .free_select) tools.lassoAdd(p);
    switch (d.tool) {
        .pencil, .brush, .eraser, .airbrush => {
            const from = d.last;
            tools.stroke(app.pic.dc, d.last, p);
            d.last = p;
            invalidateStroke(hwnd, from, p, toolReach());
        },
        else => invalidatePreview(hwnd),
    }
}

/// The rectangle the drag's preview can cover, in picture coordinates: the
/// points it has collected so far, wherever the pointer is now, and — for a
/// selection being moved or stretched — the selection itself.
fn dragBounds() w.RECT {
    const d = &tools.drag;
    var r = w.RECT{
        .left = @min(d.start.x, d.cur.x),
        .top = @min(d.start.y, d.cur.y),
        .right = @max(d.start.x, d.cur.x),
        .bottom = @max(d.start.y, d.cur.y),
    };
    for (d.points[0..@min(d.count + 1, tools.max_points)]) |q| {
        r.left = @min(r.left, q.x);
        r.top = @min(r.top, q.y);
        r.right = @max(r.right, q.x);
        r.bottom = @max(r.bottom, q.y);
    }
    if (d.tool == .free_select) {
        for (tools.lasso[0..tools.lasso_n]) |q| {
            r.left = @min(r.left, q.x);
            r.top = @min(r.top, q.y);
            r.right = @max(r.right, q.x);
            r.bottom = @max(r.bottom, q.y);
        }
    }
    if (selection.active()) {
        const b = selection.borderRect();
        r.left = @min(r.left, b.left - 2);
        r.top = @min(r.top, b.top - 2);
        r.right = @max(r.right, b.right + 2);
        r.bottom = @max(r.bottom, b.bottom + 2);
    }
    return r;
}

/// What the last preview covered, so that the next one can rub it out
/// without repainting the whole view.
var last_preview: ?w.RECT = null;

/// Ask for a repaint of what the preview covered and what it covers now.
/// A rubber-banded shape follows the pointer over a few hundred pixels; the
/// tool box, the colour box and the rest of the picture have nothing to say.
fn invalidatePreview(hwnd: w.HWND) void {
    const o = pageOrigin();
    const z = app.zoom;
    const pad = (tools.penWidth() + 4);
    const b = dragBounds();
    var r = w.RECT{
        .left = o.x + (b.left - pad) * z,
        .top = o.y + (b.top - pad) * z,
        .right = o.x + (b.right + pad + 1) * z,
        .bottom = o.y + (b.bottom + pad + 1) * z,
    };
    if (last_preview) |old| {
        r.left = @min(r.left, old.left);
        r.top = @min(r.top, old.top);
        r.right = @max(r.right, old.right);
        r.bottom = @max(r.bottom, old.bottom);
    }
    last_preview = .{
        .left = o.x + (b.left - pad) * z,
        .top = o.y + (b.top - pad) * z,
        .right = o.x + (b.right + pad + 1) * z,
        .bottom = o.y + (b.bottom + pad + 1) * z,
    };
    _ = w.InvalidateRect(hwnd, &r, w.FALSE);
}

/// Ask for a repaint of the picture between two points and no more of it.
/// A stroke damages the pixels under it and the pen's width around them; the
/// rest of the view — and with it the tool box, the colour box and the rest
/// of the window — has nothing to say and need not be painted at all.
fn invalidateStroke(hwnd: w.HWND, a: w.POINT, b: w.POINT, pad: i32) void {
    const o = pageOrigin();
    const z = app.zoom;
    var r = w.RECT{
        .left = o.x + @min(a.x, b.x) * z - pad * z,
        .top = o.y + @min(a.y, b.y) * z - pad * z,
        .right = o.x + (@max(a.x, b.x) + 1) * z + pad * z,
        .bottom = o.y + (@max(a.y, b.y) + 1) * z + pad * z,
    };
    _ = w.InvalidateRect(hwnd, &r, w.FALSE);
}

/// How far from the point a tool's mark reaches, in picture pixels.
fn toolReach() i32 {
    return switch (app.tool) {
        .brush => 6,
        .eraser => 4 + @as(i32, A.option()) * 2,
        .airbrush => 12,
        else => 2,
    };
}

const spray_timer = 1;
/// The caret in an open text box goes on and off twice a second, as the
/// machine's does.
const caret_timer = 2;
const caret_ms = 500;

fn buttonUp(hwnd: w.HWND, lp: w.LPARAM) void {
    const d = &tools.drag;
    if (box_pick or box_handle != null or box_carry != null) {
        box_pick = false;
        box_handle = null;
        box_carry = null;
        _ = w.ReleaseCapture();
        invalidateBox(hwnd);
        return;
    }
    if (page_sizing) |h| {
        page_size = pageSizeFor(h, clientPoint(lp));
        page_sizing = null;
        _ = w.ReleaseCapture();
        if (page_size.x != app.pic.width or page_size.y != app.pic.height) {
            undo.take();
            // what was there stays in the top left and the rest is the
            // background colour, exactly as Image > Attributes does it
            app.pic.resize(page_size.x, page_size.y, app.bg);
            updateScroll(hwnd);
        }
        _ = w.InvalidateRect(hwnd, null, w.FALSE);
        return;
    }
    if (!d.active) return;
    if (d.tool == .airbrush) _ = w.KillTimer(hwnd, spray_timer);
    d.cur = toImage(lp);
    d.active = false;
    _ = w.ReleaseCapture();
    if (sizing != null) {
        sizing = null;
        selection.resizeTo(size_rect);
        d.* = .{};
        _ = w.InvalidateRect(hwnd, null, w.FALSE);
        return;
    }
    if (moving) {
        moving = false;
        d.* = .{};
        _ = w.InvalidateRect(hwnd, null, w.FALSE);
        return;
    }
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
        .select => {
            // A drag that went nowhere is a click, and clears the selection.
            // Both ends are inside: dragging from 70 to 132 takes 63 columns,
            // which is what the machine lifts.
            const r = w.RECT{
                .left = @max(0, @min(d.start.x, d.cur.x)),
                .top = @max(0, @min(d.start.y, d.cur.y)),
                .right = @min(app.pic.width, @max(d.start.x, d.cur.x) + 1),
                .bottom = @min(app.pic.height, @max(d.start.y, d.cur.y) + 1),
            };
            selection.take(r);
            d.* = .{};
        },
        .free_select => {
            tools.lassoAdd(d.cur);
            selection.takeFreeForm(tools.lasso[0..tools.lasso_n]);
            tools.lasso_n = 0;
            d.* = .{};
        },
        .text => {
            const r = w.RECT{
                .left = @min(d.start.x, d.cur.x),
                .top = @min(d.start.y, d.cur.y),
                .right = @max(d.start.x, d.cur.x),
                .bottom = @max(d.start.y, d.cur.y),
            };
            if (r.right - r.left > 4 and r.bottom - r.top > 4) {
                textbox.start(r);
                _ = w.SetTimer(hwnd, caret_timer, caret_ms, null);
            }
            d.* = .{};
        },
        .magnifier => {
            // click the picture with the magnifier and it goes to the zoom
            // the settings box is showing, centred on where you clicked
            setZoom(zoomFor(A.option()), d.cur);
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

/// The pointer over the picture: Paint's own drawing for the tool in hand,
/// read off the machine. Over one of the three handles that size the picture
/// — or through a drag of one — it is the arrows that edge is dragged with,
/// which is the machine's answer there too.
/// Which pair of arrows a handle wears: the edges it owns say which way it
/// stretches, the same as a selection's do.
fn sizeCursor(i: usize) [*:0]const u8 {
    return switch (i) {
        0, 8 => w.IDC_SIZENWSE,
        2, 6 => w.IDC_SIZENESW,
        1, 7 => w.IDC_SIZENS,
        else => w.IDC_SIZEWE,
    };
}

fn setCursor(hwnd: w.HWND) void {
    var h = page_sizing;
    if (h == null) {
        var pt: w.POINT = undefined;
        if (w.GetCursorPos(&pt) != 0 and w.ScreenToClient(hwnd, &pt) != 0)
            h = liveHandleAt(pt);
    }
    // Over an open text box: the letters are chosen with the one for
    // choosing letters, a handle stretches the box and says so with the
    // arrows of the edge it owns, and the border carries it. Further out it
    // is the tool's own, since a press there starts another box.
    if (textbox.active()) {
        var pt: w.POINT = undefined;
        if (w.GetCursorPos(&pt) != 0 and w.ScreenToClient(hwnd, &pt) != 0) {
            const o = pageOrigin();
            const ip = w.POINT{
                .x = @divFloor(pt.x - o.x, app.zoom),
                .y = @divFloor(pt.y - o.y, app.zoom),
            };
            if (textbox.handleAt(ip)) |i| {
                _ = w.SetCursor(w.LoadCursorA(null, sizeCursor(i)));
                return;
            }
            if (textbox.onBorder(ip)) {
                _ = w.SetCursor(w.LoadCursorA(null, w.IDC_ARROW));
                return;
            }
            if (textbox.holds(ip)) {
                _ = w.SetCursor(w.LoadCursorA(null, w.IDC_IBEAM));
                return;
            }
        }
    }
    if (h) |which| {
        const shape = switch (which) {
            page_handle.right => w.IDC_SIZEWE,
            page_handle.bottom => w.IDC_SIZENS,
            else => w.IDC_SIZENWSE,
        };
        _ = w.SetCursor(w.LoadCursorA(null, shape));
        return;
    }
    _ = w.SetCursor(cursors.forTool(app.tool));
}

fn proc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.LRESULT {
    switch (msg) {
        w.WM_SETCURSOR => {
            if (w.LOWORD(lp) == w.HTCLIENT) {
                setCursor(hwnd);
                return 1;
            }
            return w.DefWindowProcA(hwnd, msg, wp, lp);
        },
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
        w.WM_CHAR => {
            if (textbox.active()) {
                textbox.typed(@intCast(wp & 0xff));
                _ = w.InvalidateRect(hwnd, null, w.FALSE);
                return 0;
            }
            return 0;
        },
        w.WM_KEYDOWN => {
            // In a text box the arrows walk the caret through what has been
            // typed, and Delete takes out the character in front of it.
            if (textbox.active()) {
                const dc = w.GetDC(hwnd).?;
                const shift = w.GetKeyState(w.VK_SHIFT) < 0;
                const took = textbox.key(@intCast(wp), shift, dc);
                _ = w.ReleaseDC(hwnd, dc);
                if (took) {
                    invalidateBox(hwnd);
                    return 0;
                }
                return w.DefWindowProcA(hwnd, msg, wp, lp);
            }
            // the arrows nudge a selection, as they do in Paint
            if (selection.active() and !textbox.active()) {
                const step: i32 = 1;
                switch (wp) {
                    w.VK_LEFT => selection.moveBy(-step, 0),
                    w.VK_RIGHT => selection.moveBy(step, 0),
                    w.VK_UP => selection.moveBy(0, -step),
                    w.VK_DOWN => selection.moveBy(0, step),
                    else => return w.DefWindowProcA(hwnd, msg, wp, lp),
                }
                _ = w.InvalidateRect(hwnd, null, w.FALSE);
                return 0;
            }
            return w.DefWindowProcA(hwnd, msg, wp, lp);
        },
        w.WM_LBUTTONDBLCLK => {
            // in a text box, a double press picks out the word under it
            if (textbox.active()) {
                const p = toImage(lp);
                if (textbox.holds(p)) {
                    const dc = w.GetDC(hwnd).?;
                    textbox.selectWord(p, dc);
                    _ = w.ReleaseDC(hwnd, dc);
                    invalidateBox(hwnd);
                    return 0;
                }
            }
            // A double click ends a polygon, wherever it lands: the click
            // before it has already put its point down, so what is left is
            // to close the shape and draw it.
            if (tools.drag.tool == .polygon and tools.drag.count >= 2) {
                tools.drag.cur = toImage(lp);
                commit();
                _ = w.InvalidateRect(hwnd, null, w.FALSE);
                return 0;
            }
            return w.DefWindowProcA(hwnd, msg, wp, lp);
        },
        w.WM_PAINT => {
            paint(hwnd);
            return 0;
        },
        w.WM_SIZE => {
            updateScroll(hwnd);
            return 0;
        },
        w.WM_TIMER => {
            // the airbrush goes on spraying where it is while the button is
            // down, which is what makes it an airbrush and not a pen
            if (wp == spray_timer and tools.drag.active and tools.drag.tool == .airbrush) {
                tools.stroke(app.pic.dc, tools.drag.cur, tools.drag.cur);
                _ = w.InvalidateRect(hwnd, null, w.FALSE);
                return 0;
            }
            if (wp == caret_timer) {
                if (!textbox.active()) {
                    _ = w.KillTimer(hwnd, caret_timer);
                    return 0;
                }
                textbox.box.caret_on = !textbox.box.caret_on;
                invalidateBox(hwnd);
                return 0;
            }
            return w.DefWindowProcA(hwnd, msg, wp, lp);
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

/// The four magnifications the settings box offers.
pub fn zoomFor(option: u8) i32 {
    return switch (option) {
        0 => 1,
        1 => 2,
        2 => 6,
        else => 8,
    };
}

/// Change the magnification, keeping `at` (a point in the picture) in view.
pub fn setZoom(z: i32, at: w.POINT) void {
    if (z == app.zoom) return;
    app.zoom = z;
    const cr = clientSize(app.view);
    app.scroll_x = @max(0, at.x * z - @divTrunc(cr.right, 2));
    app.scroll_y = @max(0, at.y * z - @divTrunc(cr.bottom, 2));
    updateScroll(app.view);
    _ = w.InvalidateRect(app.view, null, w.TRUE);
}

pub fn register() void {
    var wc = w.WNDCLASSA{
        // The view hears about double clicks: one closes a polygon, which is
        // how the shape is finished without going back to where it started.
        .style = w.CS_DBLCLKS,
        .lpfnWndProc = proc,
        .hbrBackground = w.GetSysColorBrush(workspace),
        .hCursor = w.LoadCursorA(null, w.IDC_ARROW),
        .lpszClassName = class_name,
    };
    _ = w.RegisterClassA(&wc);
}
