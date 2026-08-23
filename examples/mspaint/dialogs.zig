//! Paint's dialogs: Attributes, Flip/Rotate, Stretch/Skew and About.
//!
//! Every rectangle here is the machine's. The probe (tools/vm/probe.c) walks
//! the real dialog and prints each control's class, style, id and position in
//! pixels; those pixels divide exactly by the dialog base units, so what is
//! written here in dialog units maps back onto them.

const std = @import("std");
const w = @import("ween32");
const A = @import("app.zig");
const dlg = @import("dlgtemplate.zig");
const canvas = @import("canvas.zig");
const undo = @import("undo.zig");
const app = &A.app;

const child = w.WS_CHILD | w.WS_VISIBLE;
const group = w.WS_GROUP;
const tab = w.WS_TABSTOP;

var builder: dlg.Builder = .{};
var scratch: [64]u8 = undefined;

fn text(comptime fmt: []const u8, args: anytype) [*:0]const u8 {
    const s = std.fmt.bufPrintSentinel(&scratch, fmt, args, 0) catch return "";
    return s.ptr;
}

// ---- Attributes -----------------------------------------------------------

const attr = struct {
    const width = 264;
    const height = 266;
    const inches = 1028;
    const cm = 1029;
    const pixels = 1030;
    const bw = 1079;
    const colors = 1080;
    const transparent = 1068;
    const select_color = 1069;
    const swatch = 1070;
    const default = 280;
    const last_saved = 1071;
    const on_disk = 1072;
};

/// What the fields are showing, so switching units converts rather than
/// re-reads the picture.
var attr_unit: u8 = 2; // 0 inches, 1 cm, 2 pixels
var attr_w: i32 = 0;
var attr_h: i32 = 0;
var attr_bw: bool = false;

fn attrItems() []const dlg.Item {
    const S = struct {
        var items: [20]dlg.Item = undefined;
    };
    S.items = .{
        .{ .style = child | group | w.SS_LEFT, .x = 13, .y = 7, .cx = 144, .cy = 8, .id = attr.last_saved, .class = dlg.atom_static, .text = "File last saved:   Not Available" },
        .{ .style = child | group | w.SS_LEFT, .x = 13, .y = 17, .cx = 144, .cy = 8, .id = attr.on_disk, .class = dlg.atom_static, .text = "Size on disk:      Not Available" },
        .{ .style = child | group | w.SS_LEFT, .x = 13, .y = 34, .cx = 22, .cy = 10, .id = 1050, .class = dlg.atom_static, .text = "&Width:" },
        .{ .style = child | tab | w.ES_AUTOHSCROLL, .ex_style = w.WS_EX_CLIENTEDGE, .x = 42, .y = 32, .cx = 32, .cy = 12, .id = attr.width, .class = dlg.atom_edit },
        .{ .style = child | group | w.SS_LEFT, .x = 92, .y = 34, .cx = 22, .cy = 10, .id = 1051, .class = dlg.atom_static, .text = "&Height:" },
        .{ .style = child | tab | w.ES_AUTOHSCROLL, .ex_style = w.WS_EX_CLIENTEDGE, .x = 123, .y = 32, .cx = 32, .cy = 12, .id = attr.height, .class = dlg.atom_edit },
        .{ .style = child | group | w.BS_GROUPBOX, .x = 7, .y = 48, .cx = 154, .cy = 28, .id = 65535, .class = dlg.atom_button, .text = "Units" },
        .{ .style = child | tab | w.BS_AUTORADIOBUTTON, .x = 13, .y = 60, .cx = 34, .cy = 10, .id = attr.inches, .class = dlg.atom_button, .text = "&Inches" },
        .{ .style = child | w.BS_AUTORADIOBUTTON, .x = 63, .y = 60, .cx = 27, .cy = 10, .id = attr.cm, .class = dlg.atom_button, .text = "C&m" },
        .{ .style = child | w.BS_AUTORADIOBUTTON, .x = 105, .y = 60, .cx = 30, .cy = 10, .id = attr.pixels, .class = dlg.atom_button, .text = "&Pixels" },
        .{ .style = child | group | w.BS_GROUPBOX, .x = 7, .y = 80, .cx = 154, .cy = 28, .id = 65535, .class = dlg.atom_button, .text = "Colors" },
        .{ .style = child | tab | w.BS_AUTORADIOBUTTON, .x = 13, .y = 92, .cx = 64, .cy = 10, .id = attr.bw, .class = dlg.atom_button, .text = "&Black and white" },
        .{ .style = child | w.BS_AUTORADIOBUTTON, .x = 105, .y = 92, .cx = 34, .cy = 10, .id = attr.colors, .class = dlg.atom_button, .text = "Co&lors" },
        .{ .style = child | group | w.BS_GROUPBOX, .x = 7, .y = 112, .cx = 154, .cy = 48, .id = 65535, .class = dlg.atom_button, .text = "Transparency" },
        .{ .style = child | tab | w.WS_DISABLED | w.BS_AUTOCHECKBOX, .x = 13, .y = 124, .cx = 126, .cy = 10, .id = attr.transparent, .class = dlg.atom_button, .text = "Use &Transparent background color" },
        .{ .style = child | tab | w.WS_DISABLED | w.BS_PUSHBUTTON, .x = 33, .y = 138, .cx = 73, .cy = 14, .id = attr.select_color, .class = dlg.atom_button, .text = "Select &Color..." },
        .{ .style = child | w.SS_LEFT, .ex_style = w.WS_EX_CLIENTEDGE, .x = 119, .y = 138, .cx = 16, .cy = 14, .id = attr.swatch, .class = dlg.atom_static },
        .{ .style = child | tab | group | w.BS_DEFPUSHBUTTON, .x = 176, .y = 7, .cx = 50, .cy = 14, .id = w.IDOK, .class = dlg.atom_button, .text = "OK" },
        .{ .style = child | tab | w.BS_PUSHBUTTON, .x = 176, .y = 24, .cx = 50, .cy = 14, .id = w.IDCANCEL, .class = dlg.atom_button, .text = "Cancel" },
        .{ .style = child | tab | w.BS_PUSHBUTTON, .x = 176, .y = 41, .cx = 50, .cy = 14, .id = attr.default, .class = dlg.atom_button, .text = "&Default" },
    };
    return &S.items;
}

/// Pixels to whatever unit is showing, and back. Paint reckons an inch at
/// the display's own resolution.
fn toUnit(px: i32, unit: u8) i32 {
    return switch (unit) {
        0 => @divTrunc(px * 100, 96), // hundredths of an inch
        1 => @divTrunc(px * 254, 96), // tenths of a centimetre
        else => px,
    };
}

fn fromUnit(v: i32, unit: u8) i32 {
    return switch (unit) {
        0 => @divTrunc(v * 96, 100),
        1 => @divTrunc(v * 96, 254),
        else => v,
    };
}

fn showAttrSizes(hwnd: w.HWND) void {
    if (attr_unit == 2) {
        _ = w.SetDlgItemTextA(hwnd, attr.width, text("{d}", .{attr_w}));
        _ = w.SetDlgItemTextA(hwnd, attr.height, text("{d}", .{attr_h}));
    } else {
        const uw = toUnit(attr_w, attr_unit);
        const uh = toUnit(attr_h, attr_unit);
        const div: i32 = if (attr_unit == 0) 100 else 10;
        _ = w.SetDlgItemTextA(hwnd, attr.width, text("{d}.{d:0>2}", .{ @divTrunc(uw, div), @mod(uw, div) }));
        _ = w.SetDlgItemTextA(hwnd, attr.height, text("{d}.{d:0>2}", .{ @divTrunc(uh, div), @mod(uh, div) }));
    }
}

fn readAttrSizes(hwnd: w.HWND) void {
    var buf: [32]u8 = undefined;
    inline for (.{ .{ attr.width, &attr_w }, .{ attr.height, &attr_h } }) |pair| {
        const n = w.GetDlgItemTextA(hwnd, pair[0], @ptrCast(&buf), buf.len);
        const s = buf[0..@intCast(n)];
        var whole: i32 = 0;
        var frac: i32 = 0;
        var scale: i32 = 1;
        var seen_dot = false;
        for (s) |c| {
            if (c == '.') {
                seen_dot = true;
            } else if (c >= '0' and c <= '9') {
                if (seen_dot) {
                    frac = frac * 10 + (c - '0');
                    scale *= 10;
                } else whole = whole * 10 + (c - '0');
            }
        }
        const div: i32 = if (attr_unit == 0) 100 else 10;
        const v = if (attr_unit == 2) whole else @divTrunc(whole * div * scale + frac * div, scale);
        pair[1].* = @max(1, fromUnit(v, attr_unit));
    }
}

fn attrProc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.INT_PTR {
    _ = lp;
    switch (msg) {
        w.WM_INITDIALOG => {
            attr_unit = 2;
            attr_w = app.pic.width;
            attr_h = app.pic.height;
            attr_bw = false;
            _ = w.CheckRadioButton(hwnd, attr.inches, attr.pixels, attr.pixels);
            _ = w.CheckRadioButton(hwnd, attr.bw, attr.colors, attr.colors);
            showAttrSizes(hwnd);
            return 1;
        },
        w.WM_COMMAND => {
            const id = w.LOWORD(wp);
            switch (id) {
                attr.inches, attr.cm, attr.pixels => {
                    readAttrSizes(hwnd);
                    attr_unit = @intCast(id - attr.inches);
                    showAttrSizes(hwnd);
                },
                attr.bw, attr.colors => attr_bw = (id == attr.bw),
                attr.default => {
                    attr_w = 512;
                    attr_h = 384;
                    showAttrSizes(hwnd);
                },
                w.IDOK => {
                    readAttrSizes(hwnd);
                    _ = w.EndDialog(hwnd, 1);
                },
                w.IDCANCEL => _ = w.EndDialog(hwnd, 0),
                else => {},
            }
            return 1;
        },
        else => return 0,
    }
}

/// Image > Attributes. Changing the size keeps what is in the top left and
/// fills the rest with the background colour, which is what Paint does.
pub fn attributes(owner: w.HWND) void {
    const t = builder.build(w.WS_POPUP | w.WS_CAPTION | w.WS_SYSMENU | w.DS_MODALFRAME | w.DS_SETFONT | w.DS_3DLOOK, 234, 166, "Attributes", attrItems());
    if (w.DialogBoxIndirectParamA(null, t, owner, attrProc, 0) == 0) return;
    if (attr_w == app.pic.width and attr_h == app.pic.height) return;
    undo.take();
    app.pic.resize(attr_w, attr_h, app.bg);
    canvas.updateScroll(app.view);
    _ = w.InvalidateRect(app.view, null, w.TRUE);
}

// ---- Flip and rotate ------------------------------------------------------

const flip = struct {
    const horizontal = 1077;
    const vertical = 1078;
    const rotate = 1079;
    const by90 = 1081;
    const by180 = 1082;
    const by270 = 1083;
};

var flip_what: u8 = 0; // 0 horizontal, 1 vertical, 2 rotate
var flip_angle: u8 = 0; // 0 90, 1 180, 2 270

fn flipItems() []const dlg.Item {
    const S = struct {
        var items: [8]dlg.Item = undefined;
    };
    S.items = .{
        .{ .style = child | group | w.BS_GROUPBOX, .x = 7, .y = 7, .cx = 120, .cy = 76, .id = 65535, .class = dlg.atom_button, .text = "Flip or Rotate" },
        .{ .style = child | tab | w.BS_AUTORADIOBUTTON, .x = 14, .y = 19, .cx = 90, .cy = 10, .id = flip.horizontal, .class = dlg.atom_button, .text = "Flip &horizontal" },
        .{ .style = child | w.BS_AUTORADIOBUTTON, .x = 14, .y = 34, .cx = 90, .cy = 10, .id = flip.vertical, .class = dlg.atom_button, .text = "Flip &vertical" },
        .{ .style = child | w.BS_AUTORADIOBUTTON, .x = 14, .y = 49, .cx = 90, .cy = 10, .id = flip.rotate, .class = dlg.atom_button, .text = "&Rotate by angle" },
        .{ .style = child | tab | w.BS_AUTORADIOBUTTON, .x = 28, .y = 63, .cx = 30, .cy = 10, .id = flip.by90, .class = dlg.atom_button, .text = "&90°" },
        .{ .style = child | w.BS_AUTORADIOBUTTON, .x = 62, .y = 63, .cx = 34, .cy = 10, .id = flip.by180, .class = dlg.atom_button, .text = "1&80°" },
        .{ .style = child | w.BS_AUTORADIOBUTTON, .x = 96, .y = 63, .cx = 34, .cy = 10, .id = flip.by270, .class = dlg.atom_button, .text = "2&70°" },
        .{ .style = child | tab | group | w.BS_DEFPUSHBUTTON, .x = 140, .y = 7, .cx = 50, .cy = 14, .id = w.IDOK, .class = dlg.atom_button, .text = "OK" },
    };
    return &S.items;
}

fn flipProc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.INT_PTR {
    _ = lp;
    switch (msg) {
        w.WM_INITDIALOG => {
            _ = w.CheckRadioButton(hwnd, flip.horizontal, flip.rotate, flip.horizontal + @as(i32, flip_what));
            _ = w.CheckRadioButton(hwnd, flip.by90, flip.by270, flip.by90 + @as(i32, flip_angle));
            return 1;
        },
        w.WM_COMMAND => {
            const id = w.LOWORD(wp);
            switch (id) {
                flip.horizontal, flip.vertical, flip.rotate => flip_what = @intCast(id - flip.horizontal),
                flip.by90, flip.by180, flip.by270 => {
                    flip_angle = @intCast(id - flip.by90);
                    flip_what = 2;
                    _ = w.CheckRadioButton(hwnd, flip.horizontal, flip.rotate, flip.rotate);
                },
                w.IDOK => _ = w.EndDialog(hwnd, 1),
                w.IDCANCEL => _ = w.EndDialog(hwnd, 0),
                else => {},
            }
            return 1;
        },
        else => return 0,
    }
}

pub fn flipRotate(owner: w.HWND) void {
    const t = builder.build(w.WS_POPUP | w.WS_CAPTION | w.WS_SYSMENU | w.DS_MODALFRAME | w.DS_SETFONT | w.DS_3DLOOK, 197, 90, "Flip and Rotate", flipItems());
    if (w.DialogBoxIndirectParamA(null, t, owner, flipProc, 0) == 0) return;
    undo.take();
    switch (flip_what) {
        0 => app.pic.mirror(true),
        1 => app.pic.mirror(false),
        else => app.pic.rotate(switch (flip_angle) {
            0 => 90,
            1 => 180,
            else => 270,
        }),
    }
    canvas.updateScroll(app.view);
    _ = w.InvalidateRect(app.view, null, w.TRUE);
}

// ---- Stretch and skew -----------------------------------------------------

const stretch = struct {
    const horiz = 1084;
    const vert = 1085;
    const skew_h = 1086;
    const skew_v = 1087;
};

var stretch_h: i32 = 100;
var stretch_v: i32 = 100;
var skew_h: i32 = 0;
var skew_v: i32 = 0;

fn stretchItems() []const dlg.Item {
    const S = struct {
        var items: [14]dlg.Item = undefined;
    };
    S.items = .{
        .{ .style = child | group | w.BS_GROUPBOX, .x = 7, .y = 7, .cx = 180, .cy = 56, .id = 65535, .class = dlg.atom_button, .text = "Stretch" },
        .{ .style = child | w.SS_LEFT, .x = 30, .y = 22, .cx = 60, .cy = 10, .id = 65535, .class = dlg.atom_static, .text = "&Horizontal:" },
        .{ .style = child | tab | w.ES_AUTOHSCROLL, .ex_style = w.WS_EX_CLIENTEDGE, .x = 95, .y = 20, .cx = 32, .cy = 12, .id = stretch.horiz, .class = dlg.atom_edit },
        .{ .style = child | w.SS_LEFT, .x = 132, .y = 22, .cx = 20, .cy = 10, .id = 65535, .class = dlg.atom_static, .text = "%" },
        .{ .style = child | w.SS_LEFT, .x = 30, .y = 42, .cx = 60, .cy = 10, .id = 65535, .class = dlg.atom_static, .text = "&Vertical:" },
        .{ .style = child | tab | w.ES_AUTOHSCROLL, .ex_style = w.WS_EX_CLIENTEDGE, .x = 95, .y = 40, .cx = 32, .cy = 12, .id = stretch.vert, .class = dlg.atom_edit },
        .{ .style = child | w.SS_LEFT, .x = 132, .y = 42, .cx = 20, .cy = 10, .id = 65535, .class = dlg.atom_static, .text = "%" },
        .{ .style = child | group | w.BS_GROUPBOX, .x = 7, .y = 70, .cx = 180, .cy = 56, .id = 65535, .class = dlg.atom_button, .text = "Skew" },
        .{ .style = child | w.SS_LEFT, .x = 30, .y = 85, .cx = 60, .cy = 10, .id = 65535, .class = dlg.atom_static, .text = "H&orizontal:" },
        .{ .style = child | tab | w.ES_AUTOHSCROLL, .ex_style = w.WS_EX_CLIENTEDGE, .x = 95, .y = 83, .cx = 32, .cy = 12, .id = stretch.skew_h, .class = dlg.atom_edit },
        .{ .style = child | w.SS_LEFT, .x = 132, .y = 85, .cx = 30, .cy = 10, .id = 65535, .class = dlg.atom_static, .text = "Degrees" },
        .{ .style = child | w.SS_LEFT, .x = 30, .y = 105, .cx = 60, .cy = 10, .id = 65535, .class = dlg.atom_static, .text = "V&ertical:" },
        .{ .style = child | tab | w.ES_AUTOHSCROLL, .ex_style = w.WS_EX_CLIENTEDGE, .x = 95, .y = 103, .cx = 32, .cy = 12, .id = stretch.skew_v, .class = dlg.atom_edit },
        .{ .style = child | tab | group | w.BS_DEFPUSHBUTTON, .x = 200, .y = 7, .cx = 50, .cy = 14, .id = w.IDOK, .class = dlg.atom_button, .text = "OK" },
    };
    return &S.items;
}

fn readNumber(hwnd: w.HWND, id: i32, into: *i32) void {
    var buf: [32]u8 = undefined;
    const n = w.GetDlgItemTextA(hwnd, id, @ptrCast(&buf), buf.len);
    var v: i32 = 0;
    var neg = false;
    for (buf[0..@intCast(n)]) |c| {
        if (c == '-') neg = true else if (c >= '0' and c <= '9') v = v * 10 + (c - '0');
    }
    into.* = if (neg) -v else v;
}

fn stretchProc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.INT_PTR {
    _ = lp;
    switch (msg) {
        w.WM_INITDIALOG => {
            _ = w.SetDlgItemTextA(hwnd, stretch.horiz, "100");
            _ = w.SetDlgItemTextA(hwnd, stretch.vert, "100");
            _ = w.SetDlgItemTextA(hwnd, stretch.skew_h, "0");
            _ = w.SetDlgItemTextA(hwnd, stretch.skew_v, "0");
            return 1;
        },
        w.WM_COMMAND => {
            switch (w.LOWORD(wp)) {
                w.IDOK => {
                    readNumber(hwnd, stretch.horiz, &stretch_h);
                    readNumber(hwnd, stretch.vert, &stretch_v);
                    readNumber(hwnd, stretch.skew_h, &skew_h);
                    readNumber(hwnd, stretch.skew_v, &skew_v);
                    _ = w.EndDialog(hwnd, 1);
                },
                w.IDCANCEL => _ = w.EndDialog(hwnd, 0),
                else => {},
            }
            return 1;
        },
        else => return 0,
    }
}

pub fn stretchSkew(owner: w.HWND) void {
    const t = builder.build(w.WS_POPUP | w.WS_CAPTION | w.WS_SYSMENU | w.DS_MODALFRAME | w.DS_SETFONT | w.DS_3DLOOK, 257, 133, "Stretch and Skew", stretchItems());
    if (w.DialogBoxIndirectParamA(null, t, owner, stretchProc, 0) == 0) return;
    undo.take();
    if (stretch_h != 100 or stretch_v != 100)
        app.pic.stretch(stretch_h, stretch_v, app.bg);
    if (skew_h != 0 or skew_v != 0)
        app.pic.skew(skew_h, skew_v, app.bg);
    canvas.updateScroll(app.view);
    _ = w.InvalidateRect(app.view, null, w.TRUE);
}

// ---- About ----------------------------------------------------------------

fn aboutItems() []const dlg.Item {
    const S = struct {
        var items: [3]dlg.Item = undefined;
    };
    S.items = .{
        .{ .style = child | w.SS_LEFT, .x = 10, .y = 10, .cx = 160, .cy = 10, .id = 65535, .class = dlg.atom_static, .text = "Paint" },
        .{ .style = child | w.SS_LEFT, .x = 10, .y = 24, .cx = 160, .cy = 20, .id = 65535, .class = dlg.atom_static, .text = "The classic one, on ween32." },
        .{ .style = child | tab | group | w.BS_DEFPUSHBUTTON, .x = 120, .y = 46, .cx = 50, .cy = 14, .id = w.IDOK, .class = dlg.atom_button, .text = "OK" },
    };
    return &S.items;
}

fn aboutProc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.INT_PTR {
    _ = lp;
    if (msg == w.WM_COMMAND) {
        const id = w.LOWORD(wp);
        if (id == w.IDOK or id == w.IDCANCEL) _ = w.EndDialog(hwnd, 1);
        return 1;
    }
    return 0;
}

pub fn about(owner: w.HWND) void {
    const t = builder.build(w.WS_POPUP | w.WS_CAPTION | w.WS_SYSMENU | w.DS_MODALFRAME | w.DS_SETFONT | w.DS_3DLOOK, 180, 66, "About Paint", aboutItems());
    _ = w.DialogBoxIndirectParamA(null, t, owner, aboutProc, 0);
}
