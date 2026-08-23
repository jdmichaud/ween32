//! Turning the generated pixel tables into the things win32 draws with.
//!
//! `tools/mspaint/genart.py` writes each picture as a palette and one
//! character per pixel. This makes those into a bitmap, an image list or an
//! icon — the three shapes GDI wants them in — at compile time, so nothing is
//! parsed at run time and nothing is read off disk.

const w = @import("ween32");

pub const transparent = w.RGB(212, 208, 200);

/// The pixels of a generated picture, as CreateBitmap takes them: one
/// 0x00RRGGBB word per pixel, top row first.
pub fn pixels(comptime art: type) [art.cell_w * art.count * art.cell_h]u32 {
    var out: [art.cell_w * art.count * art.cell_h]u32 = undefined;
    const stride = art.cell_w * art.count;
    for (art.rows, 0..) |row, y| {
        for (row, 0..) |c, x| {
            var rgb: u32 = 0xD4D0C8;
            for (art.palette) |p| {
                if (p.ch == c) {
                    rgb = p.rgb;
                    break;
                }
            }
            out[y * stride + x] = rgb;
        }
    }
    return out;
}

/// The strip as an image list: one image per cell, the face colour taken as
/// the transparent one.
pub fn imageList(comptime art: type) w.HIMAGELIST {
    const px = pixels(art);
    const bmp = w.CreateBitmap(art.cell_w * art.count, art.cell_h, 1, 32, &px).?;
    const il = w.ImageList_Create(art.cell_w, art.cell_h, w.ILC_COLOR | w.ILC_MASK, art.count, 0);
    _ = w.ImageList_AddMasked(il, bmp, transparent);
    _ = w.DeleteObject(bmp);
    return il;
}

/// A single-cell picture as an icon, which is what a window's class wants for
/// the one in its caption: the colours, plus the one-bit mask that says which
/// of them are really there.
pub fn icon(comptime art: type) w.HICON {
    const px = pixels(art);
    const stride = (art.cell_w + 15) / 16 * 2;
    var mask: [stride * art.cell_h]u8 = @splat(0);
    for (art.rows, 0..) |row, y| {
        for (row, 0..) |c, x| {
            // a set bit in the AND mask is a pixel that is *not* drawn
            if (c == ' ')
                mask[y * stride + x / 8] |= @as(u8, 0x80) >> @intCast(x % 8);
        }
    }
    return w.CreateIcon(null, art.cell_w, art.cell_h, 1, 32, &mask, @ptrCast(&px));
}
