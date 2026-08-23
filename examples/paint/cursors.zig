//! Paint's own pointers, one per tool.
//!
//! They are cursor resources in the real program. Here they are read off the
//! machine by `tools/paint/grabcursors.py` — the emulator draws the pointer
//! into its frame buffer, so a screenshot of the screen holds it, and the same
//! pointer over a white page and over a black one says which of its pixels are
//! its own and which are the page showing through. `CreateCursor` takes the
//! two masks that come out of that.

const w = @import("ween32");
const A = @import("app.zig");
const artwork = @import("artwork.zig");
const art = @import("art_cursors.zig");

/// Made on first use and kept: a cursor is a server resource.
var made: [art.count]?w.HCURSOR = @splat(null);

fn cell(comptime i: usize) w.HCURSOR {
    if (made[i] == null)
        made[i] = artwork.cursor(art, i, art.hot_x, art.hot_y);
    return made[i].?;
}

/// The pointer over the picture, for the tool in hand.
pub fn forTool(t: A.Tool) w.HCURSOR {
    // The rubber's is the size it rubs out, so it is drawn here rather than
    // read off the machine: a square outline with the pointer in the middle,
    // which is what the captured one is at its smallest size.
    if (t == .eraser) return eraser(4 + @as(i32, A.option()) * 2);
    return switch (t) {
        inline else => |tag| cell(@intFromEnum(tag)),
    };
}

/// One thirty-two square cursor per rubber size, made once each.
var erasers: [4]?w.HCURSOR = @splat(null);

fn eraser(size: i32) w.HCURSOR {
    const i: usize = @intCast(@divTrunc(size - 4, 2));
    if (i >= erasers.len) return cell(@intFromEnum(A.Tool.eraser));
    if (erasers[i]) |c| return c;
    const side = 32;
    var and_bits: [4 * side]u8 = @splat(0xFF); // all transparent
    var xor_bits: [4 * side]u8 = @splat(0);
    const at = @divTrunc(side, 2) - @divTrunc(size, 2);
    var y: i32 = 0;
    while (y < size) : (y += 1) {
        var x: i32 = 0;
        while (x < size) : (x += 1) {
            const px: usize = @intCast(at + x);
            const py: usize = @intCast(at + y);
            const bit: u8 = @as(u8, 0x80) >> @intCast(px % 8);
            and_bits[py * 4 + px / 8] &= ~bit;
            // the edge is black and what it encloses white
            const edge = x == 0 or y == 0 or x == size - 1 or y == size - 1;
            if (!edge) xor_bits[py * 4 + px / 8] |= bit;
        }
    }
    erasers[i] = w.CreateCursor(null, 16, 16, side, side, &and_bits, &xor_bits).?;
    return erasers[i].?;
}
