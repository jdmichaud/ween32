//! The other format a picture is asked for by name: a .png, which Paint
//! came to on Windows through the filters a later one installed, and which
//! every hand now expects of it.
//!
//! There is no writing this one out by hand the way the .bmp is written:
//! behind it are deflate and crcs and thirteen kinds of chunk, and libpng is
//! the library that knows them. Only its simplified API is used — a
//! png_image told the size and handed plain RGB, in either direction —
//! because that is all a picture with no alpha and no palette needs. The
//! file itself still goes in and out through CreateFile, ReadFile and
//! WriteFile like the .bmp does; only the deflate ever reaches a C library.

const std = @import("std");
const w = @import("ween32");
const png = @import("libpng");
const A = @import("app.zig");
const app = &A.app;

var path_buf: [300]u8 = undefined;

/// The path as win32 wants it: nul-terminated, in a buffer of its own.
///
/// **It answers rather than assuming it fits.** A path is not bounded by the
/// dialog that usually supplies one -- `openArgument` hands over whatever is
/// on the command line -- and this copied in without looking, so a long
/// enough name wrote past the buffer. Reachable, and silent in a release
/// build.
fn zpath(path: []const u8) ![*:0]const u8 {
    if (path.len >= path_buf.len) return error.PathTooLong;
    @memcpy(path_buf[0..path.len], path);
    path_buf[path.len] = 0;
    return @ptrCast(&path_buf);
}

/// Read a .png into the picture, which takes its size. Anything the file is
/// that the picture is not — a palette, sixteen bits, an alpha channel,
/// interlacing — the library turns into plain eight-bit RGB on the way in,
/// with whatever was transparent laid onto white: the colour of the paper,
/// which is what the machine lays an opaque paste onto.
pub fn open(path: []const u8) !void {
    const alloc = std.heap.page_allocator;
    const f = w.CreateFileA(try zpath(path), w.GENERIC_READ, w.FILE_SHARE_READ, null, w.OPEN_EXISTING, w.FILE_ATTRIBUTE_NORMAL, null);
    if (f == w.INVALID_HANDLE_VALUE) return error.CannotOpen;
    defer _ = w.CloseHandle(f);
    const size32 = w.GetFileSize(f, null);
    if (size32 == w.INVALID_FILE_SIZE) return error.CannotOpen;
    const size: usize = size32;
    const data = try alloc.alloc(u8, size);
    defer alloc.free(data);
    var got: u32 = 0;
    if (w.ReadFile(f, data.ptr, size32, &got, null) == 0 or got != size32) return error.Truncated;

    var image = png.png_image{};
    image.version = png.PNG_IMAGE_VERSION;
    // zero is its "no": the file is not one, or is broken past reading
    if (png.png_image_begin_read_from_memory(&image, data.ptr, size) == 0)
        return error.NotAPng;
    // **A begin_read that answered yes owns memory until something frees
    // it.** Every way out of here from this point -- the allocation below
    // failing as much as the read itself -- has to give it back, and one of
    // them did not. `png_image_free` is a no-op once the image is finished,
    // so the success path costs nothing for being covered by the same line.
    defer png.png_image_free(&image);
    image.format = png.PNG_FORMAT_RGB;
    const width: i32 = @intCast(image.width);
    const height: i32 = @intCast(image.height);
    const pixels: usize = @as(usize, image.width) * @as(usize, image.height);

    const rgb = try alloc.alloc(u8, pixels * 3);
    defer alloc.free(rgb);
    const paper = png.png_color{ .red = 255, .green = 255, .blue = 255 };
    if (png.png_image_finish_read(&image, &paper, rgb.ptr, 0, null) == 0)
        return error.Corrupt;

    // to the 0x00RRGGBB words the picture is made of, top row first
    const px = try alloc.alloc(u32, pixels);
    defer alloc.free(px);
    var i: usize = 0;
    while (i < pixels) : (i += 1) {
        px[i] = (@as(u32, rgb[i * 3]) << 16) |
            (@as(u32, rgb[i * 3 + 1]) << 8) | rgb[i * 3 + 2];
    }
    app.pic.writePixels(px, width, height);
}

/// The library's idea of the picture: RGB, eight bits, no alpha — the depth
/// the picture is. A png_image is consumed by the call it is handed to,
/// which frees it, so it is made afresh each time.
fn rgbImage(width: usize, height: usize) png.png_image {
    var image = png.png_image{};
    image.version = png.PNG_IMAGE_VERSION;
    image.width = @intCast(width);
    image.height = @intCast(height);
    image.format = png.PNG_FORMAT_RGB;
    return image;
}

/// Write the picture as a .png: its pixels as plain RGB, deflated by the
/// library, and the bytes it makes written out with WriteFile.
pub fn save(path: []const u8) !void {
    const alloc = std.heap.page_allocator;
    const px = app.pic.readPixels(alloc) orelse return error.CannotRead;
    defer alloc.free(px);
    const width: usize = @intCast(app.pic.width);
    const height: usize = @intCast(app.pic.height);

    const rgb = try alloc.alloc(u8, width * height * 3);
    defer alloc.free(rgb);
    var i: usize = 0;
    while (i < px.len) : (i += 1) {
        rgb[i * 3] = @truncate(px[i] >> 16);
        rgb[i * 3 + 1] = @truncate(px[i] >> 8);
        rgb[i * 3 + 2] = @truncate(px[i]);
    }

    // The library does not make the room itself: asked with no buffer, it
    // counts the bytes the picture deflates to, and is asked again with one
    // that size. The deflating runs twice with it, which a save is rare
    // enough to afford and the plain way is the better one for.
    var bytes: usize = 0;
    var probe = rgbImage(width, height);
    if (png.png_image_write_to_memory(&probe, null, &bytes, 0, rgb.ptr, 0, null) == 0)
        return error.EncodeFailed;
    const out = try alloc.alloc(u8, bytes);
    defer alloc.free(out);
    var image = rgbImage(width, height);
    if (png.png_image_write_to_memory(&image, out.ptr, &bytes, 0, rgb.ptr, 0, null) == 0)
        return error.EncodeFailed;

    const f = w.CreateFileA(try zpath(path), w.GENERIC_WRITE, 0, null, w.CREATE_ALWAYS, w.FILE_ATTRIBUTE_NORMAL, null);
    if (f == w.INVALID_HANDLE_VALUE) return error.CannotCreate;
    defer _ = w.CloseHandle(f);
    var done: u32 = 0;
    if (w.WriteFile(f, out.ptr, @intCast(bytes), &done, null) == 0 or done != bytes) return error.WriteFailed;
}
