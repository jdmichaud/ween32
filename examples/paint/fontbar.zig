//! The Fonts bar: the palette that floats over the picture while the text
//! tool has a box open.
//!
//! The machine's is a tool window — a shorter caption with nothing in it but
//! a close box — holding a font box, a size box, a script box and four
//! buttons: bold, italic, underlined, and one for writing down the page.
//! Every rectangle here was read off it with tools/vm/probe.c: the window is
//! 475x51 with a client area of 469x29, the font box is 165x20 at 5,5, the
//! size box 53x21 at 179,5, the script box 120x21 at 241,5, and the buttons
//! sit in the last 104.
//!
//! Two things the machine's bar offers that this one only shows. The fourth
//! button is for text written down the page, which needs an East Asian
//! script to mean anything; and the sizes past about twelve point are drawn
//! at the largest strike the face carries, because the library letters from
//! bitmap strikes and has no rasteriser to make a bigger one.

const w = @import("ween32");
const A = @import("app.zig");
const textbox = @import("textbox.zig");
const artwork = @import("artwork.zig");
const art = @import("art_fonts.zig");
const app = &A.app;

const ID = struct {
    const face = 104; // the numbers the machine's own bar uses
    const size = 103;
    const script = 140;
    const buttons = 59392;
    // what the four buttons on that bar answer to
    const bold = 200;
    const italic = 201;
    const underline = 202;
    const vertical = 203;
};

/// The faces this library has. The machine lists what is installed on it —
/// its text tool starts in Arial — and ween32 carries two, so it offers
/// those two rather than a name it would only substitute for.
const faces = [_][*:0]const u8{ "Tahoma", "MS Sans Serif" };

/// The sizes the machine's box offers, in points.
const sizes = [_][*:0]const u8{
    "8", "9", "10", "11", "12", "14", "16", "18", "20", "22", "24", "26", "28",
    "36", "48", "72",
};

var bar: ?w.HWND = null;
var showing = false;
var glyphs: w.HIMAGELIST = null;
/// The three boxes and the four buttons, kept as they are made: the boxes
/// sit in the panel rather than in the bar, so the bar cannot be asked for
/// them by number.
var face_box: ?w.HWND = null;
var size_box: ?w.HWND = null;
var buttons: ?w.HWND = null;

pub fn visible() bool {
    return showing;
}

/// What the machine letters this bar in: the shell's own dialog face, not
/// the icon font. Its "Western" is eleven pixels of W where Tahoma's is ten,
/// which is how the two tell themselves apart.
var bar_font: ?w.HFONT = null;

fn barFont() w.HFONT {
    if (bar_font == null)
        bar_font = w.CreateFontA(-11, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, "MS Sans Serif");
    return bar_font.?;
}

fn control(parent: w.HWND, class: [*:0]const u8, text: [*:0]const u8, style: w.DWORD, x: i32, y: i32, cx: i32, cy: i32, id: i32) w.HWND {
    const h = w.CreateWindowExA(0, class, text, w.WS_CHILD | w.WS_VISIBLE | style, x, y, cx, cy, parent, @ptrFromInt(@as(usize, @intCast(id))), null, null).?;
    _ = w.SendMessageA(h, w.WM_SETFONT, @bitCast(@intFromPtr(barFont())), 0);
    return h;
}

fn proc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.LRESULT {
    switch (msg) {
        w.WM_COMMAND => {
            const id = w.LOWORD(wp);
            const code = w.HIWORD(wp);
            // a size can be picked from the list or typed into the box, and
            // either way the letters change
            if (code == w.CBN_SELCHANGE or code == w.CBN_EDITCHANGE or
                code == w.BN_CLICKED or id == ID.bold or id == ID.italic or
                id == ID.underline)
            {
                apply();
                _ = w.InvalidateRect(app.view, null, w.FALSE);
            }
            return 0;
        },
        w.WM_CLOSE => {
            hide();
            return 0;
        },
        else => return w.DefWindowProcA(hwnd, msg, wp, lp),
    }
}

/// The panel holding the three boxes passes on what they say. A dialog does
/// this for the controls inside it; this is a window, so it has to be told —
/// and without it a font picked from a list was heard by nobody.
fn panelProc(hwnd: w.HWND, msg: w.UINT, wp: w.WPARAM, lp: w.LPARAM) callconv(.c) w.LRESULT {
    if (msg == w.WM_COMMAND or msg == w.WM_NOTIFY) {
        const parent = w.GetParent(hwnd) orelse return 0;
        return w.SendMessageA(parent, msg, wp, lp);
    }
    return w.DefWindowProcA(hwnd, msg, wp, lp);
}

pub fn register() void {
    var wc = w.WNDCLASSA{
        .lpfnWndProc = proc,
        .hbrBackground = w.GetSysColorBrush(w.COLOR_BTNFACE),
        .hCursor = w.LoadCursorA(null, w.IDC_ARROW),
        .lpszClassName = "PaintFontsBar",
    };
    _ = w.RegisterClassA(&wc);
    var panel = w.WNDCLASSA{
        .lpfnWndProc = panelProc,
        .hbrBackground = w.GetSysColorBrush(w.COLOR_BTNFACE),
        .hCursor = w.LoadCursorA(null, w.IDC_ARROW),
        .lpszClassName = "PaintFontsPanel",
    };
    _ = w.RegisterClassA(&panel);
}

fn build() void {
    if (bar != null) return;
    // The window the machine has, to the pixel: 475x51 outside, 469x29 in,
    // and fifteen in from the corner of the window it belongs to -- which is
    // where the machine's own sits, its Paint being at the corner of the
    // screen.
    var fr: w.RECT = undefined;
    _ = w.GetWindowRect(app.frame, &fr);
    // Made out of sight and shown afterwards without being activated: the
    // caret stays in the picture while the bar floats over it, which is how
    // the machine's behaves — its caption is grey while you type.
    const h = w.CreateWindowExA(w.WS_EX_TOOLWINDOW, "PaintFontsBar", "Fonts", w.WS_POPUP | w.WS_CAPTION | w.WS_SYSMENU, fr.left + 15, fr.top + 15, 475, 51, app.frame, null, null, null).?;
    bar = h;

    // The machine keeps the three boxes in a dialog of its own inside the
    // bar — 354 by 23 at 5,3 — and the last of them is two pixels wider than
    // that dialog, so its right edge is clipped away. A panel here does the
    // same job, and the same two pixels go.
    const panel = w.CreateWindowExA(0, "PaintFontsPanel", "", w.WS_CHILD | w.WS_VISIBLE | w.WS_CLIPCHILDREN, 5, 3, 354, 23, h, null, null, null).?;

    face_box = control(panel, "COMBOBOX", "", w.WS_TABSTOP | w.CBS_DROPDOWNLIST, 0, 2, 165, 20, ID.face);
    for (faces) |f| _ = w.SendMessageA(face_box.?, w.CB_ADDSTRING, 0, @bitCast(@intFromPtr(f)));
    _ = w.SendMessageA(face_box.?, w.CB_SETCURSEL, 0, 0);

    size_box = control(panel, "COMBOBOX", "", w.WS_TABSTOP | w.CBS_DROPDOWN, 174, 2, 53, 21, ID.size);
    for (sizes) |s| _ = w.SendMessageA(size_box.?, w.CB_ADDSTRING, 0, @bitCast(@intFromPtr(s)));
    _ = w.SendMessageA(size_box.?, w.CB_SETCURSEL, 0, 0);

    const script_box = control(panel, "COMBOBOX", "", w.WS_TABSTOP | w.CBS_DROPDOWNLIST, 236, 2, 120, 21, ID.script);
    _ = w.SendMessageA(script_box, w.CB_ADDSTRING, 0, @bitCast(@intFromPtr("Western")));
    _ = w.SendMessageA(script_box, w.CB_SETCURSEL, 0, 0);

    // Bold, italic, underlined and the one for writing down the page:
    // four buttons that stay down, on a bar that is not flat — every button
    // wears its edge whether the pointer is on it or not, which is what the
    // machine's Fonts bar has.
    buttons = w.CreateWindowExA(0, w.TOOLBARCLASSNAMEA, "", w.WS_CHILD | w.WS_VISIBLE | w.CCS_NORESIZE | w.CCS_NODIVIDER | w.CCS_NOPARENTALIGN, 364, 0, 104, 28, h, @ptrFromInt(@as(usize, ID.buttons)), null, null).?;
    _ = w.SendMessageA(buttons.?, w.TB_BUTTONSTRUCTSIZE, @sizeOf(w.TBBUTTON), 0);
    glyphs = artwork.imageList(art);
    _ = w.SendMessageA(buttons.?, w.TB_SETIMAGELIST, 0, @bitCast(@intFromPtr(glyphs)));
    // sixteen-pixel pictures make a button seven wider and six taller, and
    // the first of them starts six in
    _ = w.SendMessageA(buttons.?, w.TB_SETBUTTONSIZE, 0, w.MAKELPARAM(23, 22));
    _ = w.SendMessageA(buttons.?, w.TB_SETINDENT, 6, 0);
    var tb = [_]w.TBBUTTON{
        .{ .iBitmap = 0, .idCommand = ID.bold, .fsState = w.TBSTATE_ENABLED, .fsStyle = w.TBSTYLE_CHECK },
        .{ .iBitmap = 1, .idCommand = ID.italic, .fsState = w.TBSTATE_ENABLED, .fsStyle = w.TBSTYLE_CHECK },
        .{ .iBitmap = 2, .idCommand = ID.underline, .fsState = w.TBSTATE_ENABLED, .fsStyle = w.TBSTYLE_CHECK },
        .{ .iBitmap = 3, .idCommand = ID.vertical, .fsState = w.TBSTATE_ENABLED, .fsStyle = w.TBSTYLE_CHECK },
    };
    _ = w.SendMessageA(buttons.?, w.TB_ADDBUTTONSA, tb.len, @bitCast(@intFromPtr(&tb)));
}

/// What the boxes say, into the text box's font.
fn apply() void {
    if (bar == null) return;
    var name: [64:0]u8 = undefined;
    var text: [16:0]u8 = undefined;
    _ = w.GetWindowTextA(face_box.?, &name, name.len);
    _ = w.GetWindowTextA(size_box.?, &text, text.len);
    var points: i32 = 8;
    var n: i32 = 0;
    for (text) |c| {
        if (c < '0' or c > '9') break;
        n = n * 10 + @as(i32, c - '0');
    }
    if (n > 0) points = n;
    textbox.setFont(&name, points, down(ID.bold), down(ID.italic), down(ID.underline));
}

/// Whether one of the four buttons is on.
fn down(id: i32) bool {
    const b = buttons orelse return false;
    return w.SendMessageA(b, w.TB_ISBUTTONCHECKED, @intCast(id), 0) != 0;
}

pub fn show() void {
    build();
    if (bar) |h| {
        _ = w.ShowWindow(h, w.SW_SHOWNA);
        showing = true;
    }
}

pub fn hide() void {
    if (bar) |h| _ = w.ShowWindow(h, w.SW_HIDE);
    showing = false;
}

pub fn toggle() void {
    if (showing) hide() else show();
}
