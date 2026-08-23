//! Paint, as Windows 2000 shipped it, written to win32 and running on
//! ween32 — the frame, the menu, and the four windows inside it.
//!
//! The layout is not invented: it is what the real program's own windows
//! report. A probe run inside a Windows 2000 machine (tools/vm/probe.c) walks
//! Paint's window tree and prints every rectangle, and the numbers below are
//! those rectangles. The tool box is 57 wide because Paint's is 57 wide; the
//! colour box is 49 tall because Paint's is 49 tall.
//!
//!     MSPaintApp            0,0   275x400   client 267x354
//!       AfxControlBar42u    4,42   57x282   "Tools"
//!       AfxFrameOrView42u  61,42  210x282   the view, with a client edge
//!       AfxControlBar42u    4,324 267x49    "Colors"
//!       msctls_statusbar32  4,373 267x23

const std = @import("std");
const w = @import("ween32");
const A = @import("app.zig");
const toolbox = @import("toolbox.zig");
const colorbox = @import("colorbox.zig");
const canvas = @import("canvas.zig");
const artwork = @import("artwork.zig");
const art_icon = @import("art_icon.zig");
const app = &A.app;

// ---- geometry -------------------------------------------------------------

pub const toolbox_w = 57;
pub const colorbox_h = 49;
pub const status_h = 23;

// ---- command ids ----------------------------------------------------------
//
// The real program's, read out of its menu by the probe. Keeping them means a
// screenshot of one and a dump of the other can be compared item by item.

pub const ID = struct {
    pub const file_new = 57600;
    pub const file_open = 57601;
    pub const file_save = 57603;
    pub const file_save_as = 57604;
    pub const file_print_preview = 57609;
    pub const file_page_setup = 57605;
    pub const file_print = 57607;
    pub const file_send = 37662;
    pub const file_wallpaper_tiled = 57677;
    pub const file_wallpaper_centered = 57675;
    pub const file_recent = 57616;
    pub const file_exit = 57665;

    pub const edit_undo = 57643;
    pub const edit_repeat = 57644;
    pub const edit_cut = 57635;
    pub const edit_copy = 57634;
    pub const edit_paste = 57637;
    pub const edit_clear = 57632;
    pub const edit_select_all = 57642;
    pub const edit_copy_to = 37663;
    pub const edit_paste_from = 37664;

    pub const view_tool_box = 59415;
    pub const view_color_box = 59416;
    pub const view_status_bar = 59393;
    pub const view_text_toolbar = 37678;
    pub const view_zoom_normal = 37670;
    pub const view_zoom_large = 37671;
    pub const view_zoom_custom = 37672;
    pub const view_show_grid = 37677;
    pub const view_show_thumbnail = 37676;
    pub const view_bitmap = 37673;

    pub const image_flip_rotate = 37680;
    pub const image_stretch_skew = 37681;
    pub const image_invert = 37682;
    pub const image_attributes = 37683;
    pub const image_clear = 37684;
    pub const image_draw_opaque = 6868;

    pub const colors_edit = 6869;

    pub const help_topics = 57670;
    pub const help_about = 57664;
};

const ctl_id = struct {
    const toolbox = 59420;
    const colorbox = 59422;
    const view = 59648;
    const status = 59393;
};

// ---- the menu -------------------------------------------------------------

/// One menu item, as the real program spells it: '&' before the mnemonic and
/// a tab before the accelerator, which is what puts it in the right column.
const Item = struct {
    id: u32 = 0,
    text: ?[*:0]const u8 = null, // null: a separator
    flags: u32 = 0,
};

const grayed = w.MF_GRAYED;
const checked = w.MF_CHECKED;

fn buildMenu() w.HMENU {
    const bar = w.CreateMenu();

    const file = w.CreatePopupMenu();
    append(file, &.{
        .{ .id = ID.file_new, .text = "&New\tCtrl+N" },
        .{ .id = ID.file_open, .text = "&Open...\tCtrl+O" },
        .{ .id = ID.file_save, .text = "&Save\tCtrl+S" },
        .{ .id = ID.file_save_as, .text = "Save &As..." },
        .{},
        .{ .id = ID.file_print_preview, .text = "Print Pre&view" },
        .{ .id = ID.file_page_setup, .text = "Page Set&up..." },
        .{ .id = ID.file_print, .text = "&Print...\tCtrl+P" },
        .{},
        .{ .id = ID.file_send, .text = "S&end..." },
        .{},
        .{ .id = ID.file_wallpaper_tiled, .text = "Set As &Wallpaper (Tiled)", .flags = grayed },
        .{ .id = ID.file_wallpaper_centered, .text = "Set As Wa&llpaper (Centered)", .flags = grayed },
        .{},
        .{ .id = ID.file_recent, .text = "Recent File", .flags = grayed },
        .{},
        .{ .id = ID.file_exit, .text = "E&xit\tAlt+F4" },
    });

    const edit = w.CreatePopupMenu();
    append(edit, &.{
        .{ .id = ID.edit_undo, .text = "&Undo\tCtrl+Z", .flags = grayed },
        .{ .id = ID.edit_repeat, .text = "&Repeat\tCtrl+Y", .flags = grayed },
        .{},
        .{ .id = ID.edit_cut, .text = "Cu&t\tCtrl+X", .flags = grayed },
        .{ .id = ID.edit_copy, .text = "&Copy\tCtrl+C", .flags = grayed },
        .{ .id = ID.edit_paste, .text = "&Paste\tCtrl+V", .flags = grayed },
        .{ .id = ID.edit_clear, .text = "C&lear Selection\tDel", .flags = grayed },
        .{ .id = ID.edit_select_all, .text = "Select &All\tCtrl+A" },
        .{},
        .{ .id = ID.edit_copy_to, .text = "C&opy To...", .flags = grayed },
        .{ .id = ID.edit_paste_from, .text = "Paste &From..." },
    });

    const zoom = w.CreatePopupMenu();
    append(zoom, &.{
        .{ .id = ID.view_zoom_normal, .text = "&Normal Size\tCtrl+PgUp", .flags = checked },
        .{ .id = ID.view_zoom_large, .text = "&Large Size\tCtrl+PgDn" },
        .{ .id = ID.view_zoom_custom, .text = "C&ustom..." },
        .{},
        .{ .id = ID.view_show_grid, .text = "Show &Grid\tCtrl+G", .flags = grayed },
        .{ .id = ID.view_show_thumbnail, .text = "Show T&humbnail" },
    });

    const view = w.CreatePopupMenu();
    append(view, &.{
        .{ .id = ID.view_tool_box, .text = "&Tool Box\tCtrl+T", .flags = checked },
        .{ .id = ID.view_color_box, .text = "&Color Box\tCtrl+L", .flags = checked },
        .{ .id = ID.view_status_bar, .text = "&Status Bar", .flags = checked },
        .{ .id = ID.view_text_toolbar, .text = "T&ext Toolbar", .flags = grayed },
        .{},
    });
    _ = w.AppendMenuA(view, w.MF_POPUP, @intFromPtr(zoom), "&Zoom");
    append(view, &.{
        .{ .id = ID.view_bitmap, .text = "&View Bitmap\tCtrl+F" },
    });

    const image = w.CreatePopupMenu();
    append(image, &.{
        .{ .id = ID.image_flip_rotate, .text = "&Flip/Rotate...\tCtrl+R" },
        .{ .id = ID.image_stretch_skew, .text = "&Stretch/Skew...\tCtrl+W" },
        .{ .id = ID.image_invert, .text = "&Invert Colors\tCtrl+I" },
        .{ .id = ID.image_attributes, .text = "&Attributes...\tCtrl+E" },
        .{ .id = ID.image_clear, .text = "&Clear Image\tCtrl+Shft+N" },
        .{ .id = ID.image_draw_opaque, .text = "&Draw Opaque", .flags = checked },
    });

    const colors = w.CreatePopupMenu();
    append(colors, &.{
        .{ .id = ID.colors_edit, .text = "&Edit Colors..." },
    });

    const help = w.CreatePopupMenu();
    append(help, &.{
        .{ .id = ID.help_topics, .text = "&Help Topics" },
        .{},
        .{ .id = ID.help_about, .text = "&About Paint" },
    });

    _ = w.AppendMenuA(bar, w.MF_POPUP, @intFromPtr(file), "&File");
    _ = w.AppendMenuA(bar, w.MF_POPUP, @intFromPtr(edit), "&Edit");
    _ = w.AppendMenuA(bar, w.MF_POPUP, @intFromPtr(view), "&View");
    _ = w.AppendMenuA(bar, w.MF_POPUP, @intFromPtr(image), "&Image");
    _ = w.AppendMenuA(bar, w.MF_POPUP, @intFromPtr(colors), "&Colors");
    _ = w.AppendMenuA(bar, w.MF_POPUP, @intFromPtr(help), "&Help");
    return bar;
}

fn append(menu: w.HMENU, items: []const Item) void {
    for (items) |it| {
        if (it.text) |t| {
            _ = w.AppendMenuA(menu, w.MF_STRING | it.flags, it.id, t);
        } else {
            _ = w.AppendMenuA(menu, w.MF_SEPARATOR, 0, null);
        }
    }
}

// ---- the frame ------------------------------------------------------------

fn layout(cw: i32, ch: i32) void {
    var top = ch;
    if (app.show_status) {
        top -= status_h;
        _ = w.MoveWindow(app.status, 0, top, cw, status_h, w.TRUE);
        // One part, the width of the bar. Paint's own has three — the two
        // spare ones are what its MFC status bar keeps for panes it never
        // writes in — but with no text they collapse to nothing, and what
        // they leave on the screen is three pixels of hairline beside the
        // size grip that no ordinary status bar would draw. Not worth
        // reproducing, and the only three pixels of this window that are
        // not the machine's.
    }
    if (app.show_colorbox) {
        top -= colorbox_h;
        _ = w.MoveWindow(app.colorbox, 0, top, cw, colorbox_h, w.TRUE);
    }
    var left: i32 = 0;
    if (app.show_toolbox) {
        _ = w.MoveWindow(app.toolbox, 0, 0, toolbox_w, top, w.TRUE);
        left = toolbox_w;
    }
    _ = w.MoveWindow(app.view, left, 0, cw - left, top, w.TRUE);
}

fn frameProc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.LRESULT {
    switch (msg) {
        w.WM_CREATE => {
            app.frame = hwnd;
            app.pic.create(512, 384);

            app.toolbox = w.CreateWindowExA(0, toolbox.class_name, null, w.WS_CHILD | w.WS_VISIBLE, 0, 0, toolbox_w, 100, hwnd, @ptrFromInt(ctl_id.toolbox), null, null).?;
            app.view = w.CreateWindowExA(w.WS_EX_CLIENTEDGE, canvas.class_name, null, w.WS_CHILD | w.WS_VISIBLE | w.WS_HSCROLL | w.WS_VSCROLL, 0, 0, 100, 100, hwnd, @ptrFromInt(ctl_id.view), null, null).?;
            app.colorbox = w.CreateWindowExA(0, colorbox.class_name, null, w.WS_CHILD | w.WS_VISIBLE, 0, 0, 100, colorbox_h, hwnd, @ptrFromInt(ctl_id.colorbox), null, null).?;
            // CCS_NORESIZE: the bar is 23 tall because Paint's is 23 tall, which is
            // taller than the font alone would make it, so the application has to
            // place it rather than leave that to the control.
            app.status = w.CreateWindowExA(0, "msctls_statusbar32", null, w.WS_CHILD | w.WS_VISIBLE | w.SBARS_SIZEGRIP | w.CCS_NORESIZE, 0, 0, 100, status_h, hwnd, @ptrFromInt(ctl_id.status), null, null).?;
            setHelpText("For Help, click Help Topics on the Help Menu.");
            return 0;
        },
        w.WM_SIZE => {
            layout(w.GET_X_LPARAM(lp), w.GET_Y_LPARAM(lp));
            return 0;
        },
        w.WM_COMMAND => {
            command(w.LOWORD(wp));
            return 0;
        },
        w.WM_CLOSE => {
            _ = w.DestroyWindow(hwnd);
            return 0;
        },
        w.WM_DESTROY => {
            w.PostQuitMessage(0);
            return 0;
        },
        else => return w.DefWindowProcA(hwnd, msg, wp, lp),
    }
}

pub fn setHelpText(text: [*:0]const u8) void {
    _ = w.SendMessageA(app.status, w.SB_SETTEXTA, 0, @bitCast(@intFromPtr(text)));
}

fn command(id: u16) void {
    switch (id) {
        ID.file_exit => _ = w.SendMessageA(app.frame, w.WM_CLOSE, 0, 0),
        ID.view_tool_box => toggleBar(&app.show_toolbox, app.toolbox, ID.view_tool_box),
        ID.view_color_box => toggleBar(&app.show_colorbox, app.colorbox, ID.view_color_box),
        ID.view_status_bar => toggleBar(&app.show_status, app.status, ID.view_status_bar),
        ID.image_clear => {
            app.pic.clear();
            _ = w.InvalidateRect(app.view, null, w.FALSE);
        },
        else => {},
    }
}

fn toggleBar(flag: *bool, hwnd: w.HWND, id: u32) void {
    flag.* = !flag.*;
    _ = w.ShowWindow(hwnd, if (flag.*) w.SW_SHOW else w.SW_HIDE);
    _ = w.CheckMenuItem(w.GetMenu(app.frame), id, if (flag.*) w.MF_CHECKED else w.MF_UNCHECKED);
    var cr: w.RECT = undefined;
    _ = w.GetClientRect(app.frame, &cr);
    layout(cr.right, cr.bottom);
    _ = w.InvalidateRect(app.frame, null, w.TRUE);
}

pub fn main() void {
    var icc = w.INITCOMMONCONTROLSEX{
        .dwSize = @sizeOf(w.INITCOMMONCONTROLSEX),
        .dwICC = w.ICC_BAR_CLASSES,
    };
    _ = w.InitCommonControlsEx(&icc);

    var wc = w.WNDCLASSA{
        .lpfnWndProc = frameProc,
        .hbrBackground = w.GetSysColorBrush(w.COLOR_BTNFACE),
        .hCursor = w.LoadCursorA(null, w.IDC_ARROW),
        .hIcon = artwork.icon(art_icon),
        .lpszClassName = "MSPaintApp",
    };
    _ = w.RegisterClassA(&wc);
    toolbox.register();
    colorbox.register();
    canvas.register();

    const frame = w.CreateWindowExA(0, "MSPaintApp", "untitled - Paint", w.WS_OVERLAPPEDWINDOW | w.WS_CLIPCHILDREN | w.WS_CLIPSIBLINGS, w.CW_USEDEFAULT, w.CW_USEDEFAULT, 275, 400, null, buildMenu(), null, null).?;
    _ = w.ShowWindow(frame, w.SW_SHOWNORMAL);

    var msg: w.MSG = undefined;
    while (w.GetMessageA(&msg, null, 0, 0) != 0) {
        _ = w.TranslateMessage(&msg);
        _ = w.DispatchMessageA(&msg);
    }
}
