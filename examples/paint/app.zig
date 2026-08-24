//! What every part of Paint needs to know: the picture, the tools, the two
//! colours, and the windows that show them.
//!
//! Paint is a small program with a lot of shared state, and the real one keeps
//! it in a document object the views ask. This is the same thing without the
//! framework: one instance, reached by importing this file.

const std = @import("std");
const w = @import("ween32");

pub const Tool = enum(u8) {
    free_select,
    select,
    eraser,
    fill,
    pick,
    magnifier,
    pencil,
    brush,
    airbrush,
    text,
    line,
    curve,
    rect,
    polygon,
    ellipse,
    round_rect,

    /// Where the tool sits in the box: two columns, top to bottom.
    pub fn index(t: Tool) u8 {
        return @intFromEnum(t);
    }

    pub fn fromIndex(i: usize) Tool {
        return @enumFromInt(@as(u8, @intCast(i)));
    }

    /// How many settings the box below the tools offers for this tool.
    pub fn optionCount(t: Tool) u8 {
        return switch (t) {
            .free_select, .select, .text => 2,
            .eraser => 4,
            .brush => 12,
            .airbrush => 3,
            .magnifier => 4,
            .line, .curve => 5,
            .rect, .polygon, .ellipse, .round_rect => 3,
            else => 0,
        };
    }
};

/// The picture being edited: a bitmap and the memory context it is drawn
/// through, which is exactly how the real one holds it.
pub const Picture = struct {
    dc: w.HDC = undefined,
    bmp: w.HBITMAP = undefined,
    width: i32 = 0,
    height: i32 = 0,

    pub fn create(self: *Picture, width: i32, height: i32) void {
        const screen = w.GetDC(null).?;
        self.dc = w.CreateCompatibleDC(screen).?;
        self.bmp = w.CreateCompatibleBitmap(screen, width, height).?;
        _ = w.SelectObject(self.dc, self.bmp);
        _ = w.ReleaseDC(null, screen);
        self.width = width;
        self.height = height;
        self.clear();
    }

    /// A new picture is white, as a sheet of paper is.
    pub fn clear(self: *Picture) void {
        const white = w.GetStockObject(w.WHITE_BRUSH).?;
        const old = w.SelectObject(self.dc, white);
        _ = w.PatBlt(self.dc, 0, 0, self.width, self.height, w.PATCOPY);
        if (old) |o| _ = w.SelectObject(self.dc, o);
    }

    /// Put a differently-shaped bitmap in the picture's place, leaving the
    /// old one for the caller to have blitted whatever it wanted out of.
    fn swapIn(self: *Picture, bmp: w.HBITMAP, width: i32, height: i32) void {
        const old = w.SelectObject(self.dc, bmp);
        if (old) |o| _ = w.DeleteObject(o);
        self.bmp = bmp;
        self.width = width;
        self.height = height;
    }

    fn blank(self: *Picture, width: i32, height: i32, fill: w.COLORREF) w.HDC {
        const screen = w.GetDC(null).?;
        defer _ = w.ReleaseDC(null, screen);
        const bmp = w.CreateCompatibleBitmap(screen, width, height).?;
        const dc = w.CreateCompatibleDC(screen).?;
        _ = w.SelectObject(dc, bmp);
        const brush = w.CreateSolidBrush(fill).?;
        const r = w.RECT{ .left = 0, .top = 0, .right = width, .bottom = height };
        _ = w.FillRect(dc, &r, brush);
        _ = w.DeleteObject(brush);
        _ = self;
        return dc;
    }

    /// Image > Attributes: a new size, with what was there kept in the top
    /// left and the rest the background colour.
    pub fn resize(self: *Picture, width: i32, height: i32, fill: w.COLORREF) void {
        const tmp = self.blank(width, height, fill);
        _ = w.BitBlt(tmp, 0, 0, @min(width, self.width), @min(height, self.height), self.dc, 0, 0, w.SRCCOPY);
        self.adopt(tmp, width, height);
    }

    /// Take the bitmap out of a temporary context and make it the picture.
    fn adopt(self: *Picture, tmp: w.HDC, width: i32, height: i32) void {
        // pull the bitmap back out of the temporary context first: a bitmap
        // can only be in one at a time
        const screen = w.GetDC(null).?;
        const spare = w.CreateCompatibleBitmap(screen, 1, 1).?;
        _ = w.ReleaseDC(null, screen);
        const bmp = w.SelectObject(tmp, spare).?;
        _ = w.DeleteDC(tmp);
        _ = w.DeleteObject(spare);
        self.swapIn(bmp, width, height);
    }

    /// Image > Flip/Rotate, the two flips: a blit onto itself with one extent
    /// negative, which is what mirroring is in GDI.
    pub fn mirror(self: *Picture, horizontal: bool) void {
        const tmp = self.blank(self.width, self.height, 0xFFFFFF);
        // the rectangle runs back from where it starts, so a mirror onto
        // the same size starts one past the far edge
        if (horizontal)
            _ = w.StretchBlt(tmp, self.width, 0, -self.width, self.height, self.dc, 0, 0, self.width, self.height, w.SRCCOPY)
        else
            _ = w.StretchBlt(tmp, 0, self.height, self.width, -self.height, self.dc, 0, 0, self.width, self.height, w.SRCCOPY);
        self.adopt(tmp, self.width, self.height);
    }

    /// The three rotations. Ninety degrees swaps the sides, so it is done a
    /// pixel at a time through the pixels themselves rather than with blits.
    pub fn rotate(self: *Picture, degrees: i32) void {
        if (degrees == 180) {
            const tmp = self.blank(self.width, self.height, 0xFFFFFF);
            _ = w.StretchBlt(tmp, self.width, self.height, -self.width, -self.height, self.dc, 0, 0, self.width, self.height, w.SRCCOPY);
            self.adopt(tmp, self.width, self.height);
            return;
        }
        const alloc = std.heap.page_allocator;
        const src = self.readPixels(alloc) orelse return;
        defer alloc.free(src);
        const nw = self.height;
        const nh = self.width;
        const out = alloc.alloc(u32, @intCast(nw * nh)) catch return;
        defer alloc.free(out);
        var y: i32 = 0;
        while (y < self.height) : (y += 1) {
            var x: i32 = 0;
            while (x < self.width) : (x += 1) {
                const v = src[@intCast(y * self.width + x)];
                const to = if (degrees == 90)
                    (x * nw) + (nw - 1 - y)
                else
                    ((nh - 1 - x) * nw) + y;
                out[@intCast(to)] = v;
            }
        }
        self.writePixels(out, nw, nh);
    }

    /// Image > Stretch/Skew, the stretch: a percentage of each side.
    pub fn stretch(self: *Picture, horizontal: i32, vertical: i32, fill: w.COLORREF) void {
        const nw = @max(1, @divTrunc(self.width * horizontal, 100));
        const nh = @max(1, @divTrunc(self.height * vertical, 100));
        const tmp = self.blank(nw, nh, fill);
        _ = w.StretchBlt(tmp, 0, 0, nw, nh, self.dc, 0, 0, self.width, self.height, w.SRCCOPY);
        self.adopt(tmp, nw, nh);
    }

    /// And the skew: each row slid sideways by its distance from the bottom,
    /// each column down by its distance from the right — one blit per row and
    /// per column, which is how this was always done.
    pub fn skew(self: *Picture, horizontal: i32, vertical: i32, fill: w.COLORREF) void {
        const tan_h = tanOf(horizontal);
        const tan_v = tanOf(vertical);
        const dx: i32 = @intFromFloat(@abs(tan_h) * @as(f64, @floatFromInt(self.height)));
        const dy: i32 = @intFromFloat(@abs(tan_v) * @as(f64, @floatFromInt(self.width)));
        const nw = self.width + dx;
        const nh = self.height + dy;
        const tmp = self.blank(nw, nh, fill);
        var y: i32 = 0;
        while (y < self.height) : (y += 1) {
            const shift: i32 = @intFromFloat(tan_h * @as(f64, @floatFromInt(self.height - y)));
            _ = w.BitBlt(tmp, if (tan_h >= 0) shift else dx + shift, y, self.width, 1, self.dc, 0, y, w.SRCCOPY);
        }
        if (vertical != 0) {
            var x: i32 = 0;
            while (x < nw) : (x += 1) {
                const shift: i32 = @intFromFloat(tan_v * @as(f64, @floatFromInt(nw - x)));
                _ = w.BitBlt(tmp, x, if (tan_v >= 0) shift else dy + shift, 1, self.height, tmp, x, 0, w.SRCCOPY);
            }
        }
        self.adopt(tmp, nw, nh);
    }

    fn tanOf(degrees: i32) f64 {
        const rad = @as(f64, @floatFromInt(degrees)) * std.math.pi / 180.0;
        return @tan(rad);
    }

    /// The picture's pixels, top row first, as 0x00RRGGBB words.
    pub fn readPixels(self: *Picture, alloc: std.mem.Allocator) ?[]u32 {
        const n: usize = @intCast(self.width * self.height);
        const out = alloc.alloc(u32, n) catch return null;
        var info = w.BITMAPINFO{ .bmiHeader = .{
            .biWidth = self.width,
            .biHeight = -self.height, // negative: top row first
            .biBitCount = 32,
        } };
        _ = w.GetDIBits(self.dc, self.bmp, 0, @intCast(self.height), out.ptr, &info, w.DIB_RGB_COLORS);
        return out;
    }

    /// Replace the picture with a buffer of pixels of a possibly new size.
    pub fn writePixels(self: *Picture, px: []const u32, width: i32, height: i32) void {
        const tmp = self.blank(width, height, 0xFFFFFF);
        const bmp = w.CreateCompatibleBitmap(self.dc, width, height).?;
        var info = w.BITMAPINFO{ .bmiHeader = .{
            .biWidth = width,
            .biHeight = -height,
            .biBitCount = 32,
        } };
        _ = w.SetDIBits(self.dc, bmp, 0, @intCast(height), px.ptr, &info, w.DIB_RGB_COLORS);
        const old = w.SelectObject(tmp, bmp);
        if (old) |o| _ = w.DeleteObject(o);
        self.adopt(tmp, width, height);
    }
};

/// The 28 colours of the palette, in the order the box shows them: the top
/// row left to right, then the bottom row.
pub const palette = [28]w.COLORREF{
    rgb(0, 0, 0),       rgb(128, 128, 128), rgb(128, 0, 0),    rgb(128, 128, 0),
    rgb(0, 128, 0),     rgb(0, 128, 128),   rgb(0, 0, 128),    rgb(128, 0, 128),
    rgb(128, 128, 64),  rgb(0, 64, 64),     rgb(0, 128, 255),  rgb(0, 64, 128),
    rgb(64, 0, 255),    rgb(128, 64, 0),    rgb(255, 255, 255), rgb(192, 192, 192),
    rgb(255, 0, 0),     rgb(255, 255, 0),   rgb(0, 255, 0),    rgb(0, 255, 255),
    rgb(0, 0, 255),     rgb(255, 0, 255),   rgb(255, 255, 128), rgb(0, 255, 128),
    rgb(128, 255, 255), rgb(128, 128, 255), rgb(255, 0, 128),  rgb(255, 128, 64),
};

fn rgb(r: u8, g: u8, b: u8) w.COLORREF {
    return w.RGB(r, g, b);
}

pub const State = struct {
    frame: w.HWND = undefined,
    toolbox: w.HWND = undefined,
    colorbox: w.HWND = undefined,
    view: w.HWND = undefined,
    status: w.HWND = undefined,

    tool: Tool = .pencil,
    /// The setting picked in the box under the tools, one per tool. The
    /// starting values are the ones Paint starts with: the middle rubber,
    /// the middle round brush, everything else the first.
    option: [16]u8 = blk: {
        var o: [16]u8 = @splat(0);
        o[@intFromEnum(Tool.eraser)] = 2;
        o[@intFromEnum(Tool.brush)] = 1;
        break :blk o;
    },

    fg: w.COLORREF = 0x000000,
    bg: w.COLORREF = 0xFFFFFF,

    pic: Picture = .{},

    /// Where the view is scrolled to, in picture pixels.
    scroll_x: i32 = 0,
    scroll_y: i32 = 0,
    zoom: i32 = 1,

    grid: bool = false,
    thumbnail: bool = false,
    draw_opaque: bool = true,
    /// Where the file came from, empty until it is saved or opened.
    path: [260]u8 = @splat(0),
    dirty: bool = false,

    show_toolbox: bool = true,
    show_colorbox: bool = true,
    show_status: bool = true,
};

pub var app: State = .{};

/// The option this tool is set to right now.
pub fn option() u8 {
    return app.option[app.tool.index()];
}

pub fn setOption(v: u8) void {
    app.option[app.tool.index()] = v;
    // The two selection tools share one setting: whether the background
    // colour shows through is a property of the selection, not of how it
    // was drawn round.
    if (app.tool == .select) app.option[Tool.free_select.index()] = v;
    if (app.tool == .free_select) app.option[Tool.select.index()] = v;
}

/// The two panes on the right: where the pointer is in the picture, and how
/// big what is being dragged out is. Both are the machine's, and both are
/// empty when there is nothing to say — the pointer off the picture, or
/// nothing being dragged.
var pos_text: [32:0]u8 = @splat(0);
var size_text: [32:0]u8 = @splat(0);

fn say(part: i32, into: *[32:0]u8, text: []const u8) void {
    var i: usize = 0;
    while (i < text.len and i + 1 < into.len) : (i += 1) into[i] = text[i];
    into[i] = 0;
    const p: [*:0]const u8 = @ptrCast(into);
    _ = w.SendMessageA(app.status, w.SB_SETTEXTA, @intCast(part), @bitCast(@intFromPtr(p)));
}

pub fn showPos(x: i32, y: i32) void {
    var buf: [32]u8 = undefined;
    say(1, &pos_text, std.fmt.bufPrint(&buf, "{d},{d}", .{ x, y }) catch return);
}

pub fn showSize(cx: i32, cy: i32) void {
    var buf: [32]u8 = undefined;
    say(2, &size_text, std.fmt.bufPrint(&buf, "{d}x{d}", .{ cx, cy }) catch return);
}

pub fn clearPos() void {
    say(1, &pos_text, "");
}

pub fn clearSize() void {
    say(2, &size_text, "");
}
