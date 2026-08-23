//! A DLGTEMPLATE built in memory from a table of controls.
//!
//! The well-known win32 idiom for creating a dialog at run time without a
//! compiled .rc: the bytes are a standard dialog template, handed to
//! CreateDialogIndirectParam or DialogBoxIndirectParam, so it works the same
//! over ween32 and over the real dialog manager.
//!
//! Rectangles are in *dialog units*, which is the whole point — the dialog
//! manager maps them to pixels through the font, and the application does no
//! pixel arithmetic. Paint's own dialogs are laid out in the units its
//! resources use, and they come out on the pixels the machine puts them on.

const w = @import("ween32");

pub const atom_button = 0x0080;
pub const atom_edit = 0x0081;
pub const atom_static = 0x0082;

pub const Item = struct {
    style: u32,
    ex_style: u32 = 0,
    x: i16,
    y: i16,
    cx: i16,
    cy: i16,
    id: u16,
    class: u16,
    text: []const u8 = "",
};

pub const Builder = struct {
    buf: [8192]u8 align(4) = undefined,
    len: usize = 0,

    fn word(self: *Builder, v: u16) void {
        self.buf[self.len] = @truncate(v);
        self.buf[self.len + 1] = @truncate(v >> 8);
        self.len += 2;
    }

    fn dword(self: *Builder, v: u32) void {
        self.word(@truncate(v));
        self.word(@truncate(v >> 16));
    }

    /// A null-terminated UTF-16 string, ASCII widened, as templates hold them.
    fn wsz(self: *Builder, s: []const u8) void {
        for (s) |c| self.word(c);
        self.word(0);
    }

    fn alignDword(self: *Builder) void {
        while (self.len % 4 != 0) {
            self.buf[self.len] = 0;
            self.len += 1;
        }
    }

    pub fn build(self: *Builder, style: u32, cx: i16, cy: i16, title: []const u8, items: []const Item) *const w.DLGTEMPLATE {
        self.len = 0;
        self.dword(style);
        self.dword(0);
        self.word(@intCast(items.len));
        self.word(0); // x
        self.word(0); // y
        self.word(@bitCast(cx));
        self.word(@bitCast(cy));
        self.word(0); // no menu
        self.word(0); // the default dialog class
        self.wsz(title);
        for (items) |it| {
            self.alignDword();
            self.dword(it.style);
            self.dword(it.ex_style);
            self.word(@bitCast(it.x));
            self.word(@bitCast(it.y));
            self.word(@bitCast(it.cx));
            self.word(@bitCast(it.cy));
            self.word(it.id);
            self.word(0xFFFF); // the class is an ordinal atom...
            self.word(it.class); // ...this one
            self.wsz(it.text);
            self.word(0); // no creation data
        }
        return @ptrCast(&self.buf);
    }
};
