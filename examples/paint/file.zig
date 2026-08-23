//! Reading and writing the one format Paint is really about: a .bmp.
//!
//! GDI has no file format of its own — a program that saves a picture writes
//! the header itself and asks for the pixels with GetDIBits, which is what
//! this does. Twenty-four bits, bottom-up, rows padded to four bytes: the
//! plainest BI_RGB there is, and what Paint writes.

const std = @import("std");
const w = @import("ween32");
const A = @import("app.zig");
const app = &A.app;

/// The C runtime's file calls. A win32 program written in C would use these,
/// and they are the same on both sides of the port; Zig's own file API is
/// mid-rewrite and would need a different spelling on each.
const c = struct {
    const FILE = opaque {};
    extern fn fopen(path: [*:0]const u8, mode: [*:0]const u8) ?*FILE;
    extern fn fclose(f: *FILE) c_int;
    extern fn fread(ptr: [*]u8, size: usize, n: usize, f: *FILE) usize;
    extern fn fwrite(ptr: [*]const u8, size: usize, n: usize, f: *FILE) usize;
    extern fn fseek(f: *FILE, off: c_long, whence: c_int) c_int;
    extern fn ftell(f: *FILE) c_long;
};

var path_buf: [300]u8 = undefined;

fn zpath(path: []const u8) [*:0]const u8 {
    @memcpy(path_buf[0..path.len], path);
    path_buf[path.len] = 0;
    return @ptrCast(&path_buf);
}

const file_header = 14;
const info_header = 40;

pub fn save(path: []const u8) !void {
    const width = app.pic.width;
    const height = app.pic.height;
    const stride: usize = @intCast(@divTrunc(width * 24 + 31, 32) * 4);
    const bytes = stride * @as(usize, @intCast(height));

    const alloc = std.heap.page_allocator;
    const bits = try alloc.alloc(u8, bytes);
    defer alloc.free(bits);

    var info = w.BITMAPINFO{ .bmiHeader = .{
        .biWidth = width,
        .biHeight = height, // positive: bottom row first, as a file has it
        .biBitCount = 24,
        .biSizeImage = @intCast(bytes),
    } };
    _ = w.GetDIBits(app.pic.dc, app.pic.bmp, 0, @intCast(height), bits.ptr, &info, w.DIB_RGB_COLORS);

    const f = c.fopen(zpath(path), "wb") orelse return error.CannotCreate;
    defer _ = c.fclose(f);
    var head: [file_header + info_header]u8 = undefined;
    @memset(&head, 0);
    head[0] = 'B';
    head[1] = 'M';
    std.mem.writeInt(u32, head[2..6], @intCast(head.len + bytes), .little);
    std.mem.writeInt(u32, head[10..14], head.len, .little);
    std.mem.writeInt(u32, head[14..18], info_header, .little);
    std.mem.writeInt(i32, head[18..22], width, .little);
    std.mem.writeInt(i32, head[22..26], height, .little);
    std.mem.writeInt(u16, head[26..28], 1, .little);
    std.mem.writeInt(u16, head[28..30], 24, .little);
    std.mem.writeInt(u32, head[34..38], @intCast(bytes), .little);
    if (c.fwrite(&head, 1, head.len, f) != head.len) return error.WriteFailed;
    if (c.fwrite(bits.ptr, 1, bytes, f) != bytes) return error.WriteFailed;
}

/// Read a .bmp into the picture, which takes its size.
pub fn open(path: []const u8) !void {
    const alloc = std.heap.page_allocator;
    const f = c.fopen(zpath(path), "rb") orelse return error.CannotOpen;
    defer _ = c.fclose(f);
    _ = c.fseek(f, 0, 2); // SEEK_END
    const size: usize = @intCast(c.ftell(f));
    _ = c.fseek(f, 0, 0);
    const data = try alloc.alloc(u8, size);
    defer alloc.free(data);
    if (c.fread(data.ptr, 1, size, f) != size) return error.Truncated;
    if (data.len < file_header + info_header or data[0] != 'B' or data[1] != 'M')
        return error.NotABitmap;
    const offset = std.mem.readInt(u32, data[10..14], .little);
    const width = std.mem.readInt(i32, data[18..22], .little);
    const raw_height = std.mem.readInt(i32, data[22..26], .little);
    const bpp = std.mem.readInt(u16, data[28..30], .little);
    if (bpp != 24 and bpp != 32) return error.UnsupportedDepth;
    const height = if (raw_height < 0) -raw_height else raw_height;
    const top_down = raw_height < 0;
    const stride: usize = @intCast(@divTrunc(width * @as(i32, bpp) + 31, 32) * 4);
    if (offset + stride * @as(usize, @intCast(height)) > data.len) return error.Truncated;

    const px = try alloc.alloc(u32, @intCast(width * height));
    defer alloc.free(px);
    var y: i32 = 0;
    while (y < height) : (y += 1) {
        const row = if (top_down) y else height - 1 - y;
        const in = data[offset + stride * @as(usize, @intCast(row)) ..];
        var x: i32 = 0;
        while (x < width) : (x += 1) {
            const at = @as(usize, @intCast(x)) * (bpp / 8);
            px[@intCast(y * width + x)] = (@as(u32, in[at + 2]) << 16) |
                (@as(u32, in[at + 1]) << 8) | in[at];
        }
    }
    app.pic.writePixels(px, width, height);
}
