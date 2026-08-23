//! What every part of Paint needs to know: the picture, the tools, the two
//! colours, and the windows that show them.
//!
//! Paint is a small program with a lot of shared state, and the real one keeps
//! it in a document object the views ask. This is the same thing without the
//! framework: one instance, reached by importing this file.

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
}
