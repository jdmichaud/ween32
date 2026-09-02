//! Which format a path is: Paint reads and writes .bmp and .png, and this
//! is where one becomes the other. The .bmp it writes itself; the .png is
//! libpng's. Which one a path means is decided by its extension — and, on
//! the way in, by what the file turns out to be, because a name is not
//! always telling the truth: a png picked out through "All Files" under a
//! name with no suffix still opens, recognised by what it is.

const std = @import("std");
const bmp = @import("bmp.zig");
const png = @import("png.zig");

/// Read a picture in, by its name if the name says, and by the file itself
/// if it does not: a file the .bmp reader rejects for not being a bitmap is
/// offered to the .png reader before the failure is admitted.
pub fn open(path: []const u8) !void {
    if (isPngName(path)) return png.open(path);
    return bmp.open(path) catch |err| switch (err) {
        error.NotABitmap => png.open(path),
        else => err,
    };
}

/// Write a picture out, by its name alone: a path that says nothing is a
/// .bmp, which is what Paint has always meant by one.
pub fn save(path: []const u8) !void {
    if (isPngName(path)) return png.save(path);
    return bmp.save(path);
}

/// A name ending in .png, in either case a path is typed in.
fn isPngName(path: []const u8) bool {
    return path.len >= 4 and std.ascii.eqlIgnoreCase(path[path.len - 4 ..], ".png");
}
