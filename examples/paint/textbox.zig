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
const fontbar = @import("fontbar.zig");
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
    /// Where the next letter goes, counted in characters from the start —
    /// the machine's box is a field you can walk back into, not a tape.
    caret: usize = 0,
    /// The other end of what is picked out. Equal to the caret when nothing
    /// is: a run is what lies between the two, either way round.
    anchor: usize = 0,
    caret_on: bool = true,
};

pub var box: State = .{};

/// The font the letters are drawn in, made once and thrown away whenever the
/// Fonts bar changes what it should be.
var font: ?w.HFONT = null;
/// What the Fonts bar last asked for. The machine starts its box in Arial at
/// eight point; there is no Arial here, so the box starts in the first face
/// the bar offers and the bar says so.
var face: [64]u8 = blk: {
    var f: [64]u8 = @splat(0);
    for ("Tahoma", 0..) |c, i| f[i] = c;
    break :blk f;
};
var points: i32 = 8;
var bold = false;
var italic = false;
var underlined = false;

fn textFont() w.HFONT {
    if (font == null) {
        // points are what the bar talks in; the library wants pixels, and at
        // ninety-six to the inch a point is four thirds of one
        const height = -@divTrunc(points * 96, 72);
        const name: [*:0]const u8 = @ptrCast(&face);
        font = w.CreateFontA(height, 0, 0, 0, if (bold) 700 else 400, if (italic) 1 else 0, if (underlined) 1 else 0, 0, 0, 0, 0, 0, 0, name);
    }
    return font.?;
}

/// The Fonts bar has been used: the box is lettered in what it says from now
/// on, and what is already typed is redrawn in it.
pub fn setFont(name: []const u8, size: i32, b: bool, i: bool, u: bool) void {
    var n: usize = 0;
    while (n < name.len and n + 1 < face.len and name[n] != 0) : (n += 1) face[n] = name[n];
    face[n] = 0;
    points = if (size > 0) size else 8;
    bold = b;
    italic = i;
    underlined = u;
    if (font) |f| {
        _ = w.DeleteObject(f);
        font = null;
    }
}

pub fn active() bool {
    return box.open;
}

pub fn start(r: w.RECT) void {
    box = .{ .open = true, .rect = r };
    box.len = 0;
    box.caret = 0;
    box.anchor = 0;
    box.caret_on = true;
    // the machine floats its Fonts bar the moment there is a box to letter
    fontbar.show();
}

/// One character typed, at the caret. Backspace rubs out what is behind it;
/// Enter starts a line.
pub fn typed(ch: u8) void {
    if (!box.open) return;
    if (ch == 8) {
        if (hasSelection()) {
            removeSelection();
        } else if (box.caret > 0) {
            remove(box.caret - 1);
            box.caret -= 1;
        }
        box.caret_on = true;
        return;
    }
    if (ch == 27) { // Escape: throw the box away
        box.open = false;
        fontbar.hide();
        return;
    }
    // Anything else that is not a letter is not typing: a control character
    // reaching here would come out as a hollow box, which is what a menu
    // shortcut used to leave behind.
    if (ch < ' ' and ch != '\r' and ch != '\n') return;
    // what is picked out is what the letter replaces
    if (hasSelection()) removeSelection();
    if (box.len + 1 < box.text.len) {
        var i = box.len;
        while (i > box.caret) : (i -= 1) box.text[i] = box.text[i - 1];
        box.text[box.caret] = ch;
        box.len += 1;
        box.caret += 1;
        box.anchor = box.caret;
    }
    box.caret_on = true;
}

/// What is picked out, low end first; the two are equal when nothing is.
pub fn selection() struct { from: usize, to: usize } {
    const a = @min(box.anchor, box.caret);
    const b = @max(box.anchor, box.caret);
    return .{ .from = a, .to = b };
}

pub fn hasSelection() bool {
    return box.anchor != box.caret;
}

/// Take out the run that is picked, and leave the caret where it was.
fn removeSelection() void {
    const sel = selection();
    var i = sel.from;
    while (i < sel.to) : (i += 1) remove(sel.from);
    box.caret = sel.from;
    box.anchor = sel.from;
}

/// Take out the character at `at`, closing the gap after it.
fn remove(at: usize) void {
    if (at >= box.len) return;
    var i = at;
    while (i + 1 < box.len) : (i += 1) box.text[i] = box.text[i + 1];
    box.len -= 1;
}

/// The keys that are not letters: the arrows walk the caret, Home and End go
/// to the ends of the line it is on, Delete takes out what is in front of it.
/// Held with Shift, the walking keys drag the far end of a picked-out run
/// behind them instead of collapsing it, which is how a field works.
pub fn key(vk: i32, shift: bool, dc: w.HDC) bool {
    if (!box.open) return false;
    var lines: [64]Line = undefined;
    const n = measure(dc, &lines);
    const row = lineOf(lines[0..n], box.caret);
    switch (vk) {
        w.VK_LEFT => if (hasSelection() and !shift) {
            box.caret = selection().from;
        } else if (box.caret > 0) {
            box.caret -= 1;
        },
        w.VK_RIGHT => if (hasSelection() and !shift) {
            box.caret = selection().to;
        } else if (box.caret < box.len) {
            box.caret += 1;
        },
        w.VK_HOME => box.caret = lines[row].from,
        w.VK_END => box.caret = lines[row].to,
        w.VK_DELETE => if (hasSelection()) {
            removeSelection();
        } else {
            remove(box.caret);
        },
        w.VK_UP, w.VK_DOWN => {
            // the same many characters along, on the line above or below
            const want = box.caret - lines[row].from;
            const to = if (vk == w.VK_UP) row else row + 1;
            if (vk == w.VK_UP and row == 0) return true;
            if (to >= n) return true;
            const line = lines[if (vk == w.VK_UP) row - 1 else row + 1];
            box.caret = @min(line.from + want, line.to);
        },
        else => return false,
    }
    // Shift leaves the other end where it was; anything else brings it along,
    // which is what makes the run go away when you simply walk off it.
    if (!shift) box.anchor = box.caret;
    box.caret_on = true;
    return true;
}

/// Which of the laid-out lines a character sits on.
fn lineOf(lines: []const Line, at: usize) usize {
    var i: usize = 0;
    while (i < lines.len) : (i += 1) {
        if (at <= lines[i].to) return i;
    }
    return if (lines.len > 0) lines.len - 1 else 0;
}

/// Lay the text out with the box's own font selected, which is what every
/// question about where a character is has to be asked through.
fn measure(dc: w.HDC, out: *[64]Line) usize {
    const old_font = w.SelectObject(dc, textFont());
    defer if (old_font) |o| {
        _ = w.SelectObject(dc, o);
    };
    const in = inside();
    return layout(dc, in.right - in.left, out);
}

/// A press inside the box puts the caret where it landed: the line under the
/// pointer, and the character its middle is nearest. Dragging moves only the
/// caret, so what lies between it and where the press was is picked out.
pub fn place(p: w.POINT, dc: w.HDC, dragging: bool) void {
    if (!box.open) return;
    const old_font = w.SelectObject(dc, textFont());
    defer if (old_font) |o| {
        _ = w.SelectObject(dc, o);
    };
    const in = inside();
    var lines: [64]Line = undefined;
    const n = layout(dc, in.right - in.left, &lines);
    if (n == 0) {
        box.caret = 0;
        if (!dragging) box.anchor = 0;
        return;
    }
    const height = lineHeight(dc);
    var row: usize = 0;
    if (p.y > in.top) {
        const r = @divTrunc(p.y - in.top, height);
        row = @min(@as(usize, @intCast(@max(r, 0))), n - 1);
    }
    const line = lines[row];
    var at = line.from;
    while (at < line.to) : (at += 1) {
        const upto = widthOf(dc, box.text[line.from .. at + 1]);
        const before = widthOf(dc, box.text[line.from..at]);
        if (in.left + @divTrunc(before + upto, 2) > p.x) break;
    }
    box.caret = at;
    if (!dragging) box.anchor = at;
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

/// A double press picks out the word under it, as it does in a field: the
/// run of letters and digits the caret landed in, or the run of anything
/// else when it landed between words.
pub fn selectWord(p: w.POINT, dc: w.HDC) void {
    if (!box.open) return;
    place(p, dc, false);
    if (box.len == 0) return;
    const at = @min(box.caret, box.len - 1);
    const word = isWord(box.text[at]);
    var from = at;
    while (from > 0 and isWord(box.text[from - 1]) == word) from -= 1;
    var to = at;
    while (to < box.len and isWord(box.text[to]) == word) to += 1;
    box.anchor = from;
    box.caret = to;
    box.caret_on = true;
}

fn isWord(c: u8) bool {
    return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z') or
        (c >= '0' and c <= '9') or c == '_';
}

/// The box grows down to hold what has been typed: the machine's never hides
/// a line, it lengthens the box under it instead. Says whether it changed,
/// so that what has to be painted again can be.
pub fn reflow(dc: w.HDC) bool {
    if (!box.open) return false;
    const old_font = w.SelectObject(dc, textFont());
    defer if (old_font) |o| {
        _ = w.SelectObject(dc, o);
    };
    const in = inside();
    var lines: [64]Line = undefined;
    const n = layout(dc, in.right - in.left, &lines);
    const want = in.top + @as(i32, @intCast(n)) * lineHeight(dc) + 2;
    if (want <= box.rect.bottom) return false;
    box.rect.bottom = want;
    return true;
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
    const sel = selection();
    var i: usize = 0;
    while (i < n) : (i += 1) {
        const from = lines[i].from;
        const to = lines[i].to;
        const y = in.top + @as(i32, @intCast(i)) * height;
        const line = box.text[from..to];
        // The run that is picked out is lettered white on the highlight
        // navy, the way a field shows one -- and only while the box is open,
        // since what goes into the picture is the text alone.
        if (with_border and sel.to > sel.from and sel.to > from and sel.from < to) {
            const a = @max(sel.from, from);
            const b = @min(sel.to, to);
            const x0 = in.left + widthOf(dc, box.text[from..a]);
            const x1 = in.left + widthOf(dc, box.text[from..b]);
            const brush = w.CreateSolidBrush(navy).?;
            var bar = w.RECT{ .left = x0, .top = y, .right = x1, .bottom = y + height };
            _ = w.FillRect(dc, &bar, brush);
            _ = w.DeleteObject(brush);
            if (a > from)
                _ = w.TextOutA(dc, in.left, y, box.text[from..a].ptr, @intCast(a - from));
            _ = w.SetTextColor(dc, w.RGB(255, 255, 255));
            if (b > a)
                _ = w.TextOutA(dc, x0, y, box.text[a..b].ptr, @intCast(b - a));
            _ = w.SetTextColor(dc, app.fg);
            if (to > b)
                _ = w.TextOutA(dc, x1, y, box.text[b..to].ptr, @intCast(to - b));
            continue;
        }
        if (line.len > 0)
            _ = w.TextOutA(dc, in.left, y, line.ptr, @intCast(line.len));
    }
    _ = w.SetTextColor(dc, prev);

    if (!with_border) return;

    // The caret, wherever in the text it has been walked to.
    if (box.caret_on and n > 0) {
        const row = lineOf(lines[0..n], box.caret);
        const upto = box.text[lines[row].from..box.caret];
        const x = in.left + widthOf(dc, upto);
        const y = in.top + @as(i32, @intCast(row)) * height;
        const brush = w.CreateSolidBrush(app.fg).?;
        var caret = w.RECT{ .left = x, .top = y, .right = x + 1, .bottom = y + height };
        _ = w.FillRect(dc, &caret, brush);
        _ = w.DeleteObject(brush);
    }

    drawFrame(dc);
}

/// Where each of the eight handles sits: three pixels square, centred on the
/// border at the corners and the middles of the sides — the same nine as a
/// selection's, with the middle one left out.
pub fn handleRect(i: usize) w.RECT {
    const r = box.rect;
    const xs = [3]i32{ r.left, @divTrunc(r.left + r.right, 2), r.right };
    const ys = [3]i32{ r.top, @divTrunc(r.top + r.bottom, 2), r.bottom };
    const hx = xs[i % 3] - @divTrunc(handle, 2);
    const hy = ys[i / 3] - @divTrunc(handle, 2);
    return .{ .left = hx, .top = hy, .right = hx + handle, .bottom = hy + handle };
}

/// Which handle is under a point, if any.
pub fn handleAt(p: w.POINT) ?usize {
    if (!box.open) return null;
    for (0..9) |i| {
        if (i == 4) continue;
        const h = handleRect(i);
        if (p.x >= h.left and p.x < h.right and p.y >= h.top and p.y < h.bottom)
            return i;
    }
    return null;
}

/// Whether a point is on the border itself, which is what the box is carried
/// by: within two pixels of the dashed line, inside or out.
pub fn onBorder(p: w.POINT) bool {
    if (!box.open) return false;
    const r = box.rect;
    if (p.x < r.left - 2 or p.x > r.right + 2 or p.y < r.top - 2 or p.y > r.bottom + 2)
        return false;
    return p.x <= r.left + 2 or p.x >= r.right - 2 or p.y <= r.top + 2 or
        p.y >= r.bottom - 2;
}

/// Drag a handle: the edges it owns follow the pointer, and the box never
/// closes past the room one line of letters needs.
pub fn dragHandle(i: usize, p: w.POINT) void {
    if (!box.open) return;
    const least = 8;
    var r = box.rect;
    if (i % 3 == 0) r.left = @min(p.x, r.right - least);
    if (i % 3 == 2) r.right = @max(p.x, r.left + least);
    if (i / 3 == 0) r.top = @min(p.y, r.bottom - least);
    if (i / 3 == 2) r.bottom = @max(p.y, r.top + least);
    box.rect = r;
}

/// Carry the whole box, border and letters together.
pub fn moveBy(dx: i32, dy: i32) void {
    if (!box.open) return;
    box.rect.left += dx;
    box.rect.right += dx;
    box.rect.top += dy;
    box.rect.bottom += dy;
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
    for (0..9) |i| {
        if (i == 4) continue;
        var h = handleRect(i);
        _ = w.FillRect(dc, &h, brush);
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
    fontbar.hide();
}
