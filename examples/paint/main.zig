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
const dialogs = @import("dialogs.zig");
const undo = @import("undo.zig");
const tools = @import("tools.zig");
const bmp = @import("file.zig");
const selection = @import("selection.zig");
const viewbitmap = @import("viewbitmap.zig");
const thumbnail = @import("thumbnail.zig");
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

fn frameProc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(w.winapi_cc) w.LRESULT {
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
        w.WM_MENUSELECT => {
            // the line the status bar shows while a menu item is under the
            // pointer, which is where a Windows program explains itself
            setHelpText(helpFor(w.LOWORD(wp)));
            return 0;
        },
        w.WM_CLOSE => {
            if (!askToSave()) return 0;
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

/// What the status bar says about each command. These are the machine's own
/// strings — the ones its resources carry, checked against it item by item.
fn helpFor(id: u16) [*:0]const u8 {
    return switch (id) {
        ID.file_new => "Creates a new document.",
        ID.file_open => "Opens an existing document.",
        ID.file_save => "Saves the active document.",
        ID.file_save_as => "Saves the active document with a new name.",
        ID.file_print_preview => "Displays full pages.",
        ID.file_page_setup => "Changes the printing options.",
        ID.file_print => "Prints the active document.",
        ID.file_send => "Sends the active document through electronic mail.",
        ID.file_wallpaper_tiled => "Sets the current bitmap as the desktop wallpaper in tiled format.",
        ID.file_wallpaper_centered => "Sets the current bitmap as the desktop wallpaper in centered format.",
        ID.file_exit => "Quits Paint.",

        ID.edit_undo => "Undoes the last action.",
        ID.edit_repeat => "Redoes the previously undone action.",
        ID.edit_cut => "Cuts the selection and puts it on the Clipboard.",
        ID.edit_copy => "Copies the selection and puts it on the Clipboard.",
        ID.edit_paste => "Inserts the Clipboard contents.",
        ID.edit_clear => "Deletes the selection.",
        ID.edit_select_all => "Selects everything.",
        ID.edit_copy_to => "Copies the selection to a file.",
        ID.edit_paste_from => "Pastes a file into the selection.",

        ID.view_tool_box => "Shows or hides the tool box.",
        ID.view_color_box => "Shows or hides the color box.",
        ID.view_status_bar => "Shows or hides the status bar.",
        ID.view_text_toolbar => "Shows or hides the text toolbar.",
        ID.view_zoom_normal => "Zooms the picture to 100%.",
        ID.view_zoom_large => "Zooms the picture to 400%.",
        ID.view_zoom_custom => "Zooms the picture.",
        ID.view_show_grid => "Shows or hides the grid.",
        ID.view_show_thumbnail => "Shows or hides the thumbnail.",
        ID.view_bitmap => "Displays the picture.",

        ID.image_flip_rotate => "Flips or rotates the picture or a selection.",
        ID.image_stretch_skew => "Stretches or skews the picture or a selection.",
        ID.image_invert => "Inverts the colors of the picture or a selection.",
        ID.image_attributes => "Changes the attributes of the picture.",
        ID.image_clear => "Clears the picture or selection.",
        ID.image_draw_opaque => "Makes the current selection either opaque or transparent.",

        ID.colors_edit => "Creates a new color.",
        ID.help_topics => "Lists Help topics.",
        ID.help_about => "Displays program information, version number, and copyright.",
        else => "For Help, click Help Topics on the Help Menu.",
    };
}

pub fn setHelpText(text: [*:0]const u8) void {
    _ = w.SendMessageA(app.status, w.SB_SETTEXTA, 0, @bitCast(@intFromPtr(text)));
}

fn command(id: u16) void {
    switch (id) {
        ID.file_new => {
            if (!askToSave()) return;
            app.pic.resize(512, 384, w.RGB(255, 255, 255));
            app.pic.clear();
            undo.forget();
            setPath("");
            refresh();
        },
        ID.file_save => save(hasPath()),
        ID.file_save_as => save(false),
        ID.file_recent...ID.file_recent + recent_max - 1 => {
            // one of the four at the foot of the File menu
            const i = id - ID.file_recent;
            if (i >= recent_n) return;
            if (!askToSave()) return;
            var keep: [300]u8 = @splat(0);
            const path = recentPath(i);
            @memcpy(keep[0..path.len], path);
            bmp.open(keep[0..path.len]) catch {
                _ = w.MessageBoxA(app.frame, "The picture could not be opened.", "Paint", w.MB_OK | w.MB_ICONERROR);
                return;
            };
            undo.forget();
            setPath(keep[0..path.len]);
            refresh();
        },
        ID.file_open => {
            if (!askToSave()) return;
            var buf: [300]u8 = @splat(0);
            if (!askForFile(&buf, false)) return;
            const path = zlen(&buf);
            bmp.open(path) catch {
                _ = w.MessageBoxA(app.frame, "The picture could not be opened.", "Paint", w.MB_OK | w.MB_ICONERROR);
                return;
            };
            undo.forget();
            setPath(path);
            refresh();
        },
        ID.edit_paste_from => {
            var buf: [300]u8 = @splat(0);
            if (!askForFile(&buf, false)) return;
            selection.pasteFrom(zlen(&buf));
            refresh();
        },
        ID.edit_copy_to => {
            var buf: [300]u8 = @splat(0);
            if (!askForFile(&buf, true)) return;
            selection.copyTo(zlen(&buf));
        },
        ID.colors_edit => {
            // The sixteen the box was last left with: the machine keeps them
            // for as long as the program runs, so the colour mixed on one
            // visit is still there on the next.
            const S = struct {
                var custom: [16]w.COLORREF = @splat(w.RGB(255, 255, 255));
            };
            var cc = w.CHOOSECOLORA{
                .hwndOwner = app.frame,
                .rgbResult = app.fg,
                .lpCustColors = &S.custom,
                .Flags = w.CC_RGBINIT | w.CC_ENABLEHOOK,
                .lpfnHook = colorHook,
            };
            if (w.ChooseColorA(&cc) != 0) {
                app.fg = cc.rgbResult;
                _ = w.InvalidateRect(app.colorbox, null, w.FALSE);
            }
        },
        ID.file_exit => _ = w.SendMessageA(app.frame, w.WM_CLOSE, 0, 0),

        ID.edit_undo => {
            undo.undo();
            refresh();
        },
        ID.edit_repeat => {
            undo.repeat();
            refresh();
        },
        ID.edit_select_all => {
            selection.selectAll();
            refresh();
        },
        ID.edit_cut => {
            selection.cut();
            refresh();
        },
        ID.edit_copy => selection.copy(),
        ID.edit_paste => {
            selection.paste();
            refresh();
        },
        ID.edit_clear => {
            selection.clear();
            refresh();
        },

        ID.view_tool_box => toggleBar(&app.show_toolbox, app.toolbox, ID.view_tool_box),
        ID.view_color_box => toggleBar(&app.show_colorbox, app.colorbox, ID.view_color_box),
        ID.view_status_bar => toggleBar(&app.show_status, app.status, ID.view_status_bar),
        ID.view_bitmap => viewbitmap.show(),
        ID.view_show_thumbnail => {
            app.thumbnail = !app.thumbnail;
            _ = w.CheckMenuItem(w.GetMenu(app.frame), ID.view_show_thumbnail, if (app.thumbnail) w.MF_CHECKED else w.MF_UNCHECKED);
            thumbnail.toggle(app.thumbnail);
        },
        ID.view_zoom_normal => setZoom(1),
        ID.view_zoom_large => setZoom(4),
        ID.view_zoom_custom => {
            const z = dialogs.customZoom(app.frame);
            if (z != 0) setZoom(z);
        },
        ID.view_show_grid => {
            app.grid = !app.grid;
            _ = w.CheckMenuItem(w.GetMenu(app.frame), ID.view_show_grid, if (app.grid) w.MF_CHECKED else w.MF_UNCHECKED);
            refresh();
        },

        ID.image_flip_rotate => dialogs.flipRotate(app.frame),
        ID.image_stretch_skew => dialogs.stretchSkew(app.frame),
        ID.image_attributes => dialogs.attributes(app.frame),
        ID.image_invert => {
            undo.take();
            // every pixel to its opposite, which is one blit
            _ = w.PatBlt(app.pic.dc, 0, 0, app.pic.width, app.pic.height, w.DSTINVERT);
            refresh();
        },
        ID.image_clear => {
            undo.take();
            const brush = w.CreateSolidBrush(app.bg).?;
            const r = w.RECT{ .left = 0, .top = 0, .right = app.pic.width, .bottom = app.pic.height };
            _ = w.FillRect(app.pic.dc, &r, brush);
            _ = w.DeleteObject(brush);
            refresh();
        },
        ID.image_draw_opaque => {
            app.draw_opaque = !app.draw_opaque;
            _ = w.CheckMenuItem(w.GetMenu(app.frame), ID.image_draw_opaque, if (app.draw_opaque) w.MF_CHECKED else w.MF_UNCHECKED);
        },

        ID.help_about => dialogs.about(app.frame),
        else => {},
    }
}

/// Everything that shows the picture, after something has changed it.
pub fn refresh() void {
    canvas.updateScroll(app.view);
    _ = w.InvalidateRect(app.view, null, w.TRUE);
    _ = w.InvalidateRect(app.colorbox, null, w.FALSE);
    updateMenus();
}

/// The colour box belongs to the system, and its title is "Color". Paint's
/// says "Edit Colors", which is what the hook a program hands over with
/// CC_ENABLEHOOK is for.
fn colorHook(dlg: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(w.winapi_cc) w.INT_PTR {
    _ = wp;
    _ = lp;
    if (msg == w.WM_INITDIALOG) _ = w.SetWindowTextA(dlg, "Edit Colors");
    return 0; // and the dialog carries on with it
}

fn setZoom(z: i32) void {
    canvas.setZoom(z, .{ .x = @divTrunc(app.pic.width, 2), .y = @divTrunc(app.pic.height, 2) });
    const menu = w.GetSubMenu(w.GetSubMenu(w.GetMenu(app.frame), 2), 5);
    _ = w.CheckMenuRadioItem(menu, ID.view_zoom_normal, ID.view_zoom_large, if (z == 1) ID.view_zoom_normal else ID.view_zoom_large, w.MF_BYCOMMAND);
    _ = w.EnableMenuItem(w.GetMenu(app.frame), ID.view_show_grid, if (z >= 4) w.MF_ENABLED else w.MF_GRAYED);
}

/// The menu items that are only sometimes available.
pub fn updateMenus() void {
    const m = w.GetMenu(app.frame);
    const on = w.MF_ENABLED;
    const off = w.MF_GRAYED;
    _ = w.EnableMenuItem(m, ID.edit_undo, if (undo.canUndo()) on else off);
    _ = w.EnableMenuItem(m, ID.edit_repeat, if (undo.canRedo()) on else off);
    const sel = selection.active();
    _ = w.EnableMenuItem(m, ID.edit_cut, if (sel) on else off);
    _ = w.EnableMenuItem(m, ID.edit_copy, if (sel) on else off);
    _ = w.EnableMenuItem(m, ID.edit_clear, if (sel) on else off);
    _ = w.EnableMenuItem(m, ID.edit_paste, if (w.IsClipboardFormatAvailable(w.CF_BITMAP) != 0) on else off);
}

/// Before anything that would lose the picture: save it, throw it away, or
/// change your mind, which is the three-button question every Windows
/// program asks.
fn askToSave() bool {
    if (!app.dirty) return true;
    var ask: [340]u8 = undefined;
    const q = std.fmt.bufPrintSentinel(&ask, "Save changes to {s}?", .{displayName(savePathOrNone())}, 0) catch return true;
    const answer = w.MessageBoxA(app.frame, q.ptr, "Paint", w.MB_YESNOCANCEL | w.MB_ICONQUESTION);
    if (answer == w.IDCANCEL) return false;
    if (answer == w.IDYES) save(hasPath());
    return true;
}

/// The file dialogs, which are the system's rather than ours.
fn askForFile(buf: *[300]u8, saving: bool) bool {
    var ofn = w.OPENFILENAMEA{
        .hwndOwner = app.frame,
        .lpstrFilter = "Bitmap Files (*.bmp)\x00*.bmp\x00All Files (*.*)\x00*.*\x00",
        .lpstrFile = @ptrCast(buf),
        .nMaxFile = buf.len,
        .lpstrDefExt = "bmp",
        .Flags = if (saving) w.OFN_OVERWRITEPROMPT else w.OFN_FILEMUSTEXIST,
        .lpstrTitle = if (saving) "Save As" else "Open",
    };
    // start where the last one was, if there was one
    if (hasPath()) @memcpy(buf[0..savePath().len], savePath());
    return (if (saving) w.GetSaveFileNameA(&ofn) else w.GetOpenFileNameA(&ofn)) != 0;
}

fn zlen(buf: []const u8) []const u8 {
    var i: usize = 0;
    while (i < buf.len and buf[i] != 0) : (i += 1) {}
    return buf[0..i];
}

fn hasPath() bool {
    return app.path[0] != 0;
}

/// Save, asking for a name unless we already have one.
fn save(straight: bool) void {
    var buf: [300]u8 = @splat(0);
    var path: []const u8 = undefined;
    if (straight) {
        path = savePath();
    } else {
        if (!askForFile(&buf, true)) return;
        path = zlen(&buf);
    }
    bmp.save(path) catch {
        _ = w.MessageBoxA(app.frame, "The picture could not be saved.", "Paint", w.MB_OK | w.MB_ICONERROR);
        return;
    };
    setPath(path);
    app.dirty = false;
}

/// What a picture is called: the last part of the path with the extension
/// taken off, which is what the machine puts in its caption, in its question
/// before losing changes, and in its list of recent files.
fn displayName(path: []const u8) []const u8 {
    if (path.len == 0) return "untitled";
    var i = path.len;
    while (i > 0) : (i -= 1) {
        if (path[i - 1] == '/' or path[i - 1] == '\\') break;
    }
    const name = path[i..];
    var dot = name.len;
    while (dot > 0) : (dot -= 1) {
        if (name[dot - 1] == '.') return name[0 .. dot - 1];
    }
    return name;
}

/// The first argument, if there is one. win32 hands the whole command line
/// over as one string, the program name first, and an argument with a space
/// in it is quoted.
fn openArgument() void {
    const line = std.mem.span(w.GetCommandLineA());
    var i: usize = 0;
    if (i < line.len and line[i] == '"') { // step over the program name
        i += 1;
        while (i < line.len and line[i] != '"') i += 1;
        if (i < line.len) i += 1;
    } else {
        while (i < line.len and line[i] != ' ') i += 1;
    }
    while (i < line.len and line[i] == ' ') i += 1;
    if (i >= line.len) return;
    var arg: []const u8 = line[i..];
    if (arg.len > 1 and arg[0] == '"') {
        var end: usize = 1;
        while (end < arg.len and arg[end] != '"') end += 1;
        arg = arg[1..end];
    }
    if (arg.len == 0) return;
    bmp.open(arg) catch {
        _ = w.MessageBoxA(app.frame, "The picture could not be opened.", "Paint", w.MB_OK | w.MB_ICONERROR);
        return;
    };
    undo.forget();
    setPath(arg);
    canvas.updateScroll(app.view);
    _ = w.InvalidateRect(app.view, null, w.TRUE);
}

/// The window's title: the picture's name, or "untitled".
fn setPath(path: []const u8) void {
    @memset(&app.path, 0);
    if (path.len > 0 and path.len < app.path.len)
        @memcpy(app.path[0..path.len], path);
    var title: [300]u8 = undefined;
    const t = std.fmt.bufPrintSentinel(&title, "{s} - Paint", .{displayName(path)}, 0) catch return;
    _ = w.SetWindowTextA(app.frame, t.ptr);
    if (path.len > 0) addRecent(path);
}

/// The four files at the foot of the File menu. The machine keeps them in
/// the registry between runs; there is none here, so they last as long as
/// the program does.
const recent_max = 4;
var recent: [recent_max][300]u8 = @splat(@splat(0));
var recent_n: usize = 0;

fn recentPath(i: usize) []const u8 {
    var n: usize = 0;
    while (n < recent[i].len and recent[i][n] != 0) : (n += 1) {}
    return recent[i][0..n];
}

fn addRecent(path: []const u8) void {
    if (path.len == 0 or path.len >= recent[0].len) return;
    // already there: it moves to the top rather than appearing twice
    var at: usize = recent_n;
    for (0..recent_n) |i| {
        if (std.mem.eql(u8, recentPath(i), path)) at = i;
    }
    if (at == recent_n and recent_n < recent_max) recent_n += 1;
    var i = @min(at, recent_max - 1);
    while (i > 0) : (i -= 1) recent[i] = recent[i - 1];
    @memset(&recent[0], 0);
    @memcpy(recent[0][0..path.len], path);
    updateRecentMenu();
}

/// Where the list sits in the File menu: after the wallpaper items and the
/// separator that follows them, before the separator above Exit.
const recent_pos = 14;

fn updateRecentMenu() void {
    const file = w.GetSubMenu(w.GetMenu(app.frame), 0) orelse return;
    // out with the placeholder or the list that is there — every item that
    // carries one of the four commands — and in with the list as it stands
    var id: u32 = ID.file_recent;
    while (id < ID.file_recent + recent_max) : (id += 1)
        _ = w.DeleteMenu(file, id, w.MF_BYCOMMAND);
    var buf: [320]u8 = undefined;
    for (0..recent_n) |i| {
        const text = std.fmt.bufPrintSentinel(&buf, "&{d} {s}", .{ i + 1, displayName(recentPath(i)) }, 0) catch continue;
        _ = w.InsertMenuA(file, @intCast(recent_pos + i), w.MF_BYPOSITION | w.MF_STRING, ID.file_recent + i, text.ptr);
    }
}

/// Where the picture is saved: what was opened, or what the command line
/// named, or a file beside the program.
/// The path as it stands, or nothing at all — which is "untitled".
fn savePathOrNone() []const u8 {
    var i: usize = 0;
    while (i < app.path.len and app.path[i] != 0) : (i += 1) {}
    return app.path[0..i];
}

fn savePath() []const u8 {
    var i: usize = 0;
    while (i < app.path.len and app.path[i] != 0) : (i += 1) {}
    if (i > 0) return app.path[0..i];
    return "untitled.bmp";
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

/// Paint's accelerators, the ones its menu advertises.
fn accelerators() w.HACCEL {
    const v = w.FVIRTKEY | w.FCONTROL;
    const S = struct {
        var table: [20]w.ACCEL = undefined;
    };
    S.table = .{
        .{ .fVirt = v, .key = 'N', .cmd = ID.file_new },
        .{ .fVirt = v, .key = 'O', .cmd = ID.file_open },
        .{ .fVirt = v, .key = 'S', .cmd = ID.file_save },
        .{ .fVirt = v, .key = 'P', .cmd = ID.file_print },
        .{ .fVirt = v, .key = 'Z', .cmd = ID.edit_undo },
        .{ .fVirt = v, .key = 'Y', .cmd = ID.edit_repeat },
        .{ .fVirt = v, .key = 'X', .cmd = ID.edit_cut },
        .{ .fVirt = v, .key = 'C', .cmd = ID.edit_copy },
        .{ .fVirt = v, .key = 'V', .cmd = ID.edit_paste },
        .{ .fVirt = v, .key = 'A', .cmd = ID.edit_select_all },
        .{ .fVirt = w.FVIRTKEY, .key = w.VK_DELETE, .cmd = ID.edit_clear },
        .{ .fVirt = v, .key = 'T', .cmd = ID.view_tool_box },
        .{ .fVirt = v, .key = 'L', .cmd = ID.view_color_box },
        .{ .fVirt = v, .key = 'F', .cmd = ID.view_bitmap },
        .{ .fVirt = v, .key = 'G', .cmd = ID.view_show_grid },
        .{ .fVirt = v, .key = 'R', .cmd = ID.image_flip_rotate },
        .{ .fVirt = v, .key = 'W', .cmd = ID.image_stretch_skew },
        .{ .fVirt = v, .key = 'I', .cmd = ID.image_invert },
        .{ .fVirt = v, .key = 'E', .cmd = ID.image_attributes },
        .{ .fVirt = v | w.FSHIFT, .key = 'N', .cmd = ID.image_clear },
    };
    return w.CreateAcceleratorTableA(&S.table, S.table.len);
}

/// What a failed check does.
///
/// Zig's own handler leaves through ntdll's RtlExitUserProcess, which is not
/// on a machine this old, and a program that so much as mentions it will not
/// load there. This says what happened in a box and goes through the door
/// win32 has always had.
pub const panic = std.debug.FullPanic(paintPanic);

fn paintPanic(msg: []const u8, _: ?usize) noreturn {
    var buf: [512]u8 = undefined;
    const text = std.fmt.bufPrintSentinel(&buf, "{s}", .{msg}, 0) catch "Paint stopped.";
    _ = w.MessageBoxA(null, text, "Paint", w.MB_OK | w.MB_ICONERROR);
    w.ExitProcess(3);
}

/// Where Windows starts it.
///
/// Zig's own start-up leaves through ntdll's RtlExitUserProcess, which
/// Windows 2000 does not have — the program will not load at all, before a
/// line of it runs. Entering here instead means the only calls it makes are
/// ones that machine has: it runs there, which is the whole point of being
/// able to build it as win32 at all.
fn startup() callconv(w.winapi_cc) noreturn {
    main();
    w.ExitProcess(0);
}

/// The one call Zig's library makes that Windows 2000 has not got.
///
/// It is how a Zig program leaves — the start-up's own exit and the abort a
/// failed check ends in both go through it — and a program that names a
/// procedure ntdll does not export is refused by the loader before any of it
/// runs, whatever it would have called. Defining it here answers the name
/// ourselves, so nothing is ever looked up, and does what it says through the
/// call that has been there since Windows had windows.
fn exitUserProcess(code: u32) callconv(w.winapi_cc) noreturn {
    w.ExitProcess(code);
}

comptime {
    if (@import("builtin").target.os.tag == .windows) {
        @export(&startup, .{ .name = "paintStartup" });
        @export(&exitUserProcess, .{ .name = "RtlExitUserProcess" });
    }
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
    viewbitmap.register();
    thumbnail.register();

    const frame = w.CreateWindowExA(0, "MSPaintApp", "untitled - Paint", w.WS_OVERLAPPEDWINDOW | w.WS_CLIPCHILDREN | w.WS_CLIPSIBLINGS, w.CW_USEDEFAULT, w.CW_USEDEFAULT, 275, 400, null, buildMenu(), null, null).?;
    _ = w.ShowWindow(frame, w.SW_SHOWNORMAL);

    const accel = accelerators();
    updateMenus();
    // A picture named on the command line, which is how a shell opens one.
    openArgument();

    var msg: w.MSG = undefined;
    while (w.GetMessageA(&msg, null, 0, 0) != 0) {
        if (w.TranslateAcceleratorA(frame, accel, &msg) != 0) continue;
        // Alt and the letters after it: the menu bar is reached from the
        // keyboard the same way a dialog's controls are, which is what the
        // framework does for the real one.
        if (w.IsDialogMessageA(frame, &msg) != 0) continue;
        _ = w.TranslateMessage(&msg);
        _ = w.DispatchMessageA(&msg);
    }
}
