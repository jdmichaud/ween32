//! What each tool does with the mouse.
//!
//! Two kinds of tool. The pencil, the brush, the rubber and the airbrush put
//! paint down as the pointer moves and there is nothing to undo mid-stroke;
//! the shapes — line, curve, rectangle, ellipse, polygon — show what they
//! would draw until the button comes up, and only then is it in the picture.
//! Both go through the same code: `draw` is handed a device context, and it
//! is the window's while the button is down and the picture's afterwards.

const std = @import("std");
const w = @import("ween32");
const A = @import("app.zig");
const app = &A.app;

pub const max_points = 64;

pub const Drag = struct {
    active: bool = false,
    tool: A.Tool = .pencil,
    /// The right button draws with the two colours the other way round,
    /// which is the whole of what it means in Paint.
    right: bool = false,
    start: w.POINT = .{ .x = 0, .y = 0 },
    last: w.POINT = .{ .x = 0, .y = 0 },
    cur: w.POINT = .{ .x = 0, .y = 0 },
    /// The curve and the polygon collect points across several presses.
    points: [max_points]w.POINT = undefined,
    count: usize = 0,
    /// The curve's four stages: the line, then a bend, then another.
    stage: u8 = 0,
    shift: bool = false,
};

pub var drag: Drag = .{};

/// The free-form select's path, which is not a shape made of a few presses
/// but every point the pointer went through while the button was down.
pub const max_lasso = 1024;
pub var lasso: [max_lasso]w.POINT = undefined;
pub var lasso_n: usize = 0;

pub fn lassoAdd(p: w.POINT) void {
    if (lasso_n > 0 and lasso[lasso_n - 1].x == p.x and lasso[lasso_n - 1].y == p.y) return;
    if (lasso_n == max_lasso) {
        // out of room: thin what is there and carry on, so a long drag
        // loses detail rather than its end
        var i: usize = 0;
        while (i * 2 + 1 < lasso_n) : (i += 1) lasso[i] = lasso[i * 2];
        lasso_n = i;
    }
    lasso[lasso_n] = p;
    lasso_n += 1;
}

/// The colour the outline is drawn in, and the one the inside is filled
/// with: the foreground and the background, swapped for the right button.
pub fn penColor() w.COLORREF {
    return if (drag.right) app.bg else app.fg;
}

pub fn fillColor() w.COLORREF {
    return if (drag.right) app.fg else app.bg;
}

/// Whether the tool holds its drawing back until the button comes up.
pub fn isShape(t: A.Tool) bool {
    return switch (t) {
        .line, .curve, .rect, .ellipse, .round_rect, .polygon, .select, .free_select => true,
        else => false,
    };
}

// ---- the marks the freehand tools make ------------------------------------

/// The twelve brushes, exactly as the machine stamps them: a click with
/// each in turn, read back off the screen. Three sizes of round, of square,
/// and of the two diagonals; the stamp sits with its top-left corner half
/// its size above and left of the pointer.
const brushes = [12][]const []const u8{
    // round: seven, four and one across
    &.{ "  ###  ", " ##### ", "#######", "#######", "#######", " ##### ", "  ###  " },
    &.{ " ## ", "####", "####", " ## " },
    &.{"#"},
    // square: eight, five and two
    &.{ "########", "########", "########", "########", "########", "########", "########", "########" },
    &.{ "#####", "#####", "#####", "#####", "#####" },
    &.{ "##", "##" },
    // one diagonal
    &.{ "       #", "      # ", "     #  ", "    #   ", "   #    ", "  #     ", " #      ", "#       " },
    &.{ "    #", "   # ", "  #  ", " #   ", "#    " },
    &.{ " #", "# " },
    // and the other
    &.{ "#       ", " #      ", "  #     ", "   #    ", "    #   ", "     #  ", "      # ", "       #" },
    &.{ "#    ", " #   ", "  #  ", "   # ", "    #" },
    &.{ "# ", " #" },
};

/// One stamp of the brush at a point, in the given colour.
fn brushAt(dc: w.HDC, x: i32, y: i32, color: w.COLORREF) void {
    const b = brushes[@min(A.option(), brushes.len - 1)];
    const h: i32 = @intCast(b.len);
    const wd: i32 = @intCast(b[0].len);
    const x0 = x - @divTrunc(wd, 2);
    const y0 = y - @divTrunc(h, 2);
    for (b, 0..) |row, ry| {
        var run: i32 = -1;
        for (row, 0..) |c, rx| {
            const on = c == '#';
            if (on and run < 0) run = @intCast(rx);
            if (!on and run >= 0) {
                fillRun(dc, x0 + run, y0 + @as(i32, @intCast(ry)), @as(i32, @intCast(rx)) - run, color);
                run = -1;
            }
        }
        if (run >= 0)
            fillRun(dc, x0 + run, y0 + @as(i32, @intCast(ry)), wd - run, color);
    }
}

fn fillRun(dc: w.HDC, x: i32, y: i32, len: i32, color: w.COLORREF) void {
    const r = w.RECT{ .left = x, .top = y, .right = x + len, .bottom = y + 1 };
    const brush = w.CreateSolidBrush(color).?;
    _ = w.FillRect(dc, &r, brush);
    _ = w.DeleteObject(brush);
}

/// The rubber: a square of the background colour, four sizes.
fn eraseAt(dc: w.HDC, x: i32, y: i32) void {
    const size: i32 = 4 + @as(i32, @intCast(A.option())) * 2;
    const r = w.RECT{ .left = x - @divTrunc(size, 2), .top = y - @divTrunc(size, 2), .right = x - @divTrunc(size, 2) + size, .bottom = y - @divTrunc(size, 2) + size };
    if (drag.right) {
        // The colour eraser: dragged with the right button it changes only
        // the pixels that are the foreground colour, and changes them to the
        // background one. Everything else it passes over untouched.
        var py = r.top;
        while (py < r.bottom) : (py += 1) {
            var px = r.left;
            while (px < r.right) : (px += 1) {
                if (w.GetPixel(dc, px, py) == app.fg)
                    _ = w.SetPixel(dc, px, py, app.bg);
            }
        }
        return;
    }
    const brush = w.CreateSolidBrush(fillColor()).?;
    _ = w.FillRect(dc, &r, brush);
    _ = w.DeleteObject(brush);
}

/// The airbrush: a scatter of dots in a circle, thicker in the middle, and a
/// new scatter every time the pointer moves or the timer ticks.
var spray_seed: u32 = 0x1234567;

fn sprayAt(dc: w.HDC, x: i32, y: i32, color: w.COLORREF) void {
    const radius: i32 = switch (@min(A.option(), 2)) {
        0 => 4,
        1 => 7,
        else => 11,
    };
    const dots: u32 = @intCast(@divTrunc(radius * radius, 2));
    var i: u32 = 0;
    while (i < dots) : (i += 1) {
        // a small xorshift: the same scatter every run, which is what makes
        // a headless render of it reproducible
        spray_seed ^= spray_seed << 13;
        spray_seed ^= spray_seed >> 17;
        spray_seed ^= spray_seed << 5;
        const a = @as(i32, @intCast(spray_seed % @as(u32, @intCast(radius * 2 + 1)))) - radius;
        spray_seed ^= spray_seed << 13;
        spray_seed ^= spray_seed >> 17;
        spray_seed ^= spray_seed << 5;
        const b = @as(i32, @intCast(spray_seed % @as(u32, @intCast(radius * 2 + 1)))) - radius;
        if (a * a + b * b <= radius * radius)
            _ = w.SetPixel(dc, x + a, y + b, color);
    }
}

/// A stroke from one point to the next, stamping whatever the tool puts down
/// at every pixel along the way.
pub fn stroke(dc: w.HDC, from: w.POINT, to: w.POINT) void {
    const color = penColor();
    var x = from.x;
    var y = from.y;
    const dx = @abs(to.x - from.x);
    const dy = @abs(to.y - from.y);
    const sx: i32 = if (to.x > from.x) 1 else -1;
    const sy: i32 = if (to.y > from.y) 1 else -1;
    var err: i32 = @as(i32, @intCast(dx)) - @as(i32, @intCast(dy));
    while (true) {
        switch (drag.tool) {
            .pencil => _ = w.SetPixel(dc, x, y, color),
            .brush => brushAt(dc, x, y, color),
            .eraser => eraseAt(dc, x, y),
            .airbrush => sprayAt(dc, x, y, color),
            else => {},
        }
        if (x == to.x and y == to.y) break;
        const e2 = 2 * err;
        if (e2 > -@as(i32, @intCast(dy))) {
            err -= @intCast(dy);
            x += sx;
        }
        if (e2 < @as(i32, @intCast(dx))) {
            err += @intCast(dx);
            y += sy;
        }
    }
}

// ---- the shapes ------------------------------------------------------------

/// Held Shift squares a rectangle, circles an ellipse and puts a line on one
/// of the eight compass points, as it does in Paint.
/// Where the drag ends once Shift has had its say, for the caller that has
/// to keep that point rather than draw with it.
pub fn constrainedEnd() w.POINT {
    return constrained();
}

fn constrained() w.POINT {
    var p = drag.cur;
    if (!drag.shift) return p;
    const dx = p.x - drag.start.x;
    const dy = p.y - drag.start.y;
    switch (drag.tool) {
        // A curve starts as a line, and Shift puts that line on a compass
        // point the same way; the two bends after it are free.
        .line, .curve => {
            const ax = @abs(dx);
            const ay = @abs(dy);
            if (ax > ay * 2) {
                p.y = drag.start.y;
            } else if (ay > ax * 2) {
                p.x = drag.start.x;
            } else {
                const d = @min(ax, ay);
                p.x = drag.start.x + (if (dx < 0) -@as(i32, @intCast(d)) else @as(i32, @intCast(d)));
                p.y = drag.start.y + (if (dy < 0) -@as(i32, @intCast(d)) else @as(i32, @intCast(d)));
            }
        },
        .rect, .ellipse, .round_rect, .select => {
            const d = @min(@abs(dx), @abs(dy));
            p.x = drag.start.x + (if (dx < 0) -@as(i32, @intCast(d)) else @as(i32, @intCast(d)));
            p.y = drag.start.y + (if (dy < 0) -@as(i32, @intCast(d)) else @as(i32, @intCast(d)));
        },
        else => {},
    }
    return p;
}

fn lineWidth() i32 {
    return @as(i32, @intCast(A.option())) + 1;
}

/// How wide the mark a shape leaves is, which is how far past the shape a
/// repaint has to reach.
pub fn penWidth() i32 {
    return switch (app.tool) {
        .rect, .ellipse, .round_rect, .polygon => 1,
        .line, .curve => lineWidth(),
        else => 1,
    };
}

/// The pen and brush a shape is drawn with: the outline in the pen colour at
/// the chosen width, and the inside according to the fill style — outline
/// only, filled with the background, or filled with the foreground.
fn withShapeObjects(dc: w.HDC, body: *const fn (w.HDC) void) void {
    const style = A.option();
    const width: i32 = switch (app.tool) {
        .rect, .ellipse, .round_rect, .polygon => 1,
        else => lineWidth(),
    };
    const pen = w.CreatePen(w.PS_SOLID, width, penColor()).?;
    const brush: w.HBRUSH = switch (style) {
        1 => w.CreateSolidBrush(fillColor()).?,
        2 => w.CreateSolidBrush(penColor()).?,
        else => w.GetStockObject(w.NULL_BRUSH).?,
    };
    const op = w.SelectObject(dc, pen);
    const ob = w.SelectObject(dc, brush);
    body(dc);
    if (op) |o| _ = w.SelectObject(dc, o);
    if (ob) |o| _ = w.SelectObject(dc, o);
    _ = w.DeleteObject(pen);
    if (style == 1 or style == 2) _ = w.DeleteObject(brush);
}

/// The rectangle a drag spans, in the form GDI's Rectangle takes it: the
/// far corner is the one the pointer is on and is *not* drawn, which is
/// what Paint does — a drag of fifty pixels leaves a rectangle fifty wide
/// counting from the corner it started at.
fn dragRect() w.RECT {
    const p = constrained();
    return .{
        .left = @min(drag.start.x, p.x),
        .top = @min(drag.start.y, p.y),
        .right = @max(drag.start.x, p.x),
        .bottom = @max(drag.start.y, p.y),
    };
}

var draw_dc: w.HDC = undefined;

fn shapeBody(dc: w.HDC) void {
    const r = dragRect();
    switch (drag.tool) {
        .rect => _ = w.Rectangle(dc, r.left, r.top, r.right, r.bottom),
        .ellipse => _ = w.Ellipse(dc, r.left, r.top, r.right, r.bottom),
        .round_rect => _ = w.RoundRect(dc, r.left, r.top, r.right, r.bottom, 16, 16),
        .line => {
            const p = constrained();
            _ = w.MoveToEx(dc, drag.start.x, drag.start.y, null);
            _ = w.LineTo(dc, p.x, p.y);
            // GDI leaves the last point of a line to whatever comes next;
            // a line tool has nothing coming next, so it puts it down.
            _ = w.SetPixel(dc, p.x, p.y, penColor());
        },
        .curve => drawCurve(dc),
        .polygon => drawPolygon(dc),
        else => {},
    }
}

/// The curve tool: the first drag is a straight line, and the two after it
/// pull it into a Bézier, one control point each.
fn drawCurve(dc: w.HDC) void {
    var pts: [4]w.POINT = undefined;
    pts[0] = drag.points[0];
    pts[3] = if (drag.stage == 0) constrained() else drag.points[1];
    if (drag.stage == 1) {
        pts[1] = drag.cur;
        pts[2] = drag.cur;
    } else {
        pts[1] = drag.points[2];
        pts[2] = if (drag.stage >= 3) drag.points[3] else drag.cur;
    }
    if (drag.stage == 0) {
        _ = w.MoveToEx(dc, pts[0].x, pts[0].y, null);
        _ = w.LineTo(dc, pts[3].x, pts[3].y);
        _ = w.SetPixel(dc, pts[3].x, pts[3].y, penColor());
    } else {
        _ = w.PolyBezier(dc, &pts, 4);
    }
}

fn drawPolygon(dc: w.HDC) void {
    var pts: [max_points]w.POINT = undefined;
    var n: usize = 0;
    while (n < drag.count) : (n += 1) pts[n] = drag.points[n];
    pts[n] = drag.cur;
    n += 1;
    if (n < 2) return;
    if (n == 2) {
        _ = w.MoveToEx(dc, pts[0].x, pts[0].y, null);
        _ = w.LineTo(dc, pts[1].x, pts[1].y);
        _ = w.SetPixel(dc, pts[1].x, pts[1].y, penColor());
    } else {
        _ = w.Polygon(dc, &pts, @intCast(n));
    }
}

/// Draw whatever the drag is making, into whichever context is handed over —
/// the window's while it is in progress, the picture's when it is done.
pub fn drawDrag(dc: w.HDC) void {
    if (!drag.active and drag.stage == 0 and drag.count == 0) return;
    switch (drag.tool) {
        .free_select => {
            // the lasso itself, inverted onto the picture, closed back to
            // where it started as Paint closes it
            if (lasso_n < 2) return;
            const pen = w.CreatePen(w.PS_SOLID, 1, w.RGB(255, 255, 255)) orelse return;
            const old = w.SelectObject(dc, pen);
            const rop = w.SetROP2(dc, w.R2_NOT);
            _ = w.Polyline(dc, &lasso, @intCast(lasso_n));
            _ = w.MoveToEx(dc, lasso[lasso_n - 1].x, lasso[lasso_n - 1].y, null);
            _ = w.LineTo(dc, lasso[0].x, lasso[0].y);
            _ = w.SetROP2(dc, rop);
            if (old) |o| _ = w.SelectObject(dc, o);
            _ = w.DeleteObject(pen);
        },
        .select => {
            const r = dragRect();
            // the marching rectangle, drawn by inverting what is under it
            _ = w.DrawFocusRect(dc, &r);
        },
        else => withShapeObjects(dc, &shapeBody),
    }
}
