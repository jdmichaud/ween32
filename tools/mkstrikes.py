#!/usr/bin/env python3
"""Add embedded bitmap strikes to an outline font, for ween32's renderer.

jd: *"The size are just incorrect... if I select Tahoma and then select 72, I
don't get a 72 font."*  He is right, and the cause is that ween32 has no
rasteriser: `src/font.c` draws from `EBDT` bitmap strikes and Tahoma ships
eight of them, the largest at 16 ppem -- twelve point at 96 dpi.  Every size
above that draws identically.  Given the choice between writing a rasteriser
and generating the bitmaps ahead of time, jd chose ahead of time.

**The output is the input font with strikes added, not a bitmap font built
from it.**  `ween_strike_char_units` reads `hmtx`, `hhea` and `head` for the
advances GDI reports, so the outline tables have to survive; a strikes-only
file would load, draw, and measure every string wrong.

**The format is not negotiable and not checked.**  `src/font.c` reads uint32
index offsets (EBLC index format 1) and glyph data of `height, width,
bearingX, bearingY, advance` followed by bits with no row padding -- the bit
index is `row * width + col` at font.c:256, which is EBDT image format 2.  It
never reads `indexFormat` or `imageFormat`, so writing the wrong ones fails
silently rather than loudly.

**Why not every ppem.**  `sbitLineMetrics.ascender` and `.descender` are
**int8**.  At 144 ppem this face's ascender is about 130 and does not fit, so
the format itself rules out the largest strikes a 144 dpi size list would
want.  The sixteen ppem of the 96 dpi size list all fit; 120 and 144 fall to
the nearest strike, which is what every face already does at every dpi.

**MS Sans Serif cannot be treated this way and that is correct.**  520 of its
521 glyphs have no outline -- it is a bitmap font, Windows draws it as one,
and there is nothing here to generate from.  Tahoma could be, and does not
need to be: it is not the document face.

    tools/mkstrikes.py IN.ttf OUT.ttf --ppem 11,12,13,... [--family Arial]
"""
import argparse
import struct
import sys

import freetype


def rd16(b, o):
    return struct.unpack(">H", b[o:o + 2])[0]


def rd32(b, o):
    return struct.unpack(">I", b[o:o + 4])[0]


def read_tables(data):
    """tag -> bytes, from an sfnt."""
    n = rd16(data, 4)
    out = {}
    for i in range(n):
        e = 12 + i * 16
        tag = data[e:e + 4].decode("latin-1")
        off, length = rd32(data, e + 8), rd32(data, e + 12)
        out[tag] = data[off:off + length]
    return out


def checksum(b):
    b = b + b"\0" * ((4 - len(b) % 4) % 4)
    return sum(struct.unpack(">%dI" % (len(b) // 4), b)) & 0xFFFFFFFF


def build_sfnt(tables):
    """An sfnt from tag -> bytes, with the directory and checksums right."""
    tags = sorted(tables)
    n = len(tags)
    sr = 1
    while sr * 2 <= n:
        sr *= 2
    search = sr * 16
    entry = sr.bit_length() - 1
    header = struct.pack(">IHHHH", 0x00010000, n, search, entry,
                         n * 16 - search)
    off = 12 + n * 16
    dirent, body, head_at = b"", b"", None
    for t in tags:
        d = tables[t]
        if t == "head":                     # zero checkSumAdjustment first
            d = d[:8] + b"\0\0\0\0" + d[12:]
            head_at = off + len(body)
        dirent += struct.pack(">4sIII", t.encode("latin-1"), checksum(d),
                              off + len(body), len(d))
        body += d + b"\0" * ((4 - len(d) % 4) % 4)
    font = header + dirent + body
    if head_at is not None:
        adj = (0xB1B0AFBA - checksum(font)) & 0xFFFFFFFF
        font = (font[:head_at + 8] + struct.pack(">I", adj)
                + font[head_at + 12:])
    return font


def render(face, ppem, gids):
    """Every glyph at one ppem: small metrics and bit-aligned rows.

    FreeType hands back rows padded to a byte (`pitch`); EBDT format 2 is one
    continuous bit stream with no padding, so the rows are re-packed here.
    Getting that wrong draws a sheared glyph rather than an error.
    """
    face.set_pixel_sizes(0, ppem)
    out = {}
    for g in gids:
        face.load_glyph(g, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        bm, sl = face.glyph.bitmap, face.glyph
        w, h, pitch = bm.width, bm.rows, bm.pitch
        bits = bytearray()
        acc = cur = 0
        for row in range(h):
            for col in range(w):
                on = (bm.buffer[row * pitch + (col >> 3)] >> (7 - (col & 7))) & 1
                cur = (cur << 1) | on
                acc += 1
                if acc == 8:
                    bits.append(cur)
                    acc = cur = 0
        if acc:
            bits.append(cur << (8 - acc))
        adv = sl.advance.x >> 6
        out[g] = (h, w, sl.bitmap_left, sl.bitmap_top, adv, bytes(bits))
    return out


def s8(v):
    """int8, and say so rather than wrapping: the format cannot hold more."""
    if not -128 <= v <= 127:
        raise SystemExit("mkstrikes: %d does not fit in the int8 this field is;"
                         " that ppem cannot be represented" % v)
    return struct.pack(">b", v)


def runs(gids):
    """Contiguous glyph-id runs, one index subtable each."""
    out, start, prev = [], None, None
    for g in gids:
        if start is None:
            start = prev = g
        elif g == prev + 1:
            prev = g
        else:
            out.append((start, prev))
            start = prev = g
    if start is not None:
        out.append((start, prev))
    return out


def existing(tables):
    """The strikes a font already has: (ppem, bitmapSizeTable, index blob).

    **These are copied out byte for byte and never regenerated**, which is the
    whole reason this tool merges instead of replacing. Tahoma ships eight
    hand-made strikes and nine committed captures agree with them pixel for
    pixel; rendering those sizes again from the outlines would move every one.
    The argument for generating ahead of time instead of writing a rasteriser
    was precisely that the small sizes cannot move -- a tool that regenerated
    them would have thrown that away while appearing to do the job.

    EBDT offsets are relative to the table's own start, so keeping the original
    EBDT as a prefix and appending after it leaves every existing offset valid.
    """
    eblc, ebdt = tables.get("EBLC"), tables.get("EBDT")
    if not eblc or not ebdt:
        return [], bytearray(struct.pack(">I", 0x00020000))
    out = []
    for i in range(rd32(eblc, 4)):
        bst = 8 + i * 48
        isa, isz = rd32(eblc, bst), rd32(eblc, bst + 4)
        out.append((eblc[bst + 44], eblc[bst:bst + 48], eblc[isa:isa + isz]))
    return out, bytearray(ebdt)


def line_metrics(tables, px):
    """A strike's ascender and descender, from the outline at *px* pixels.

    **Not from FreeType's scaled metrics, and not at the integer ppem.**
    Measured on Windows 2000: Arial 10 at 96 dpi gives a caret sixteen rows
    tall (tests/richedit_test.c). The candidates, against this face's
    `OS/2.winAscent` 1854 and `winDescent` 434 per 2048:

        integer ppem 13, round     ascent 12 descent 3 -> 15
        integer ppem 13, ceil      ascent 12 descent 3 -> 15
        unrounded 13.333, ceil     ascent 13 descent 3 -> 16   <- the machine

    Ten point at 96 dpi is 13.333 pixels; the strike is stored at ppem 13
    because EBLC keys on an integer, but the metrics belong to the size it
    stands for. Scaling by the rounded ppem loses exactly the third of a pixel
    that carries the ascender over.

    **These are written and `src/font.c` currently ignores them.** It takes
    the cell from `hhea` scaled by the *integer* strike ppem instead, on the
    stated grounds that some fonts leave the strike descender at zero -- and
    that is why Arial 10 comes out fifteen rows here against the machine's
    sixteen. Honouring the strike's own numbers would fix that and **move
    every one of Tahoma's eight existing strikes**, whose ascenders differ
    from the hhea-scaled ones at every size (6 against 8 at ppem 8, 13 against
    16 at ppem 16), so it is not a change that can be made for this alone.
    They are written correctly here regardless: the file should say what the
    strike is even while the reader prefers another source.
    """
    os2, head = tables.get("OS/2"), tables.get("head")
    if not os2 or not head:
        return None
    upem = rd16(head, 18)
    wa, wd = rd16(os2, 74), rd16(os2, 76)
    up = lambda v: -(-(v * px) // upem)          # ceil, in integers
    return int(up(wa)), -int(up(wd))


def build(face, ppems, gids, keep=(), ebdt0=None, real=None, tables=None):
    """EBLC and EBDT for the given ppem set, after any kept strikes."""
    ebdt = ebdt0 if ebdt0 is not None else bytearray(struct.pack(">I", 0x00020000))
    sizes = []
    for ppem in ppems:
        glyphs = render(face, ppem, gids)
        face.set_pixel_sizes(0, ppem)
        asc, desc = face.size.ascender >> 6, face.size.descender >> 6
        if real and tables is not None:
            lm = line_metrics(tables, real.get(ppem, ppem))
            if lm:
                asc, desc = lm
        widest = max((g[1] for g in glyphs.values()), default=0)
        data_at = len(ebdt)
        offsets = {}
        for g in gids:
            h, w, bx, by, adv, bits = glyphs[g]
            offsets[g] = len(ebdt) - data_at
            if bits:
                ebdt += bytes([h, w]) + s8(bx) + s8(by) + bytes([adv]) + bits
        offsets["end"] = len(ebdt) - data_at
        sizes.append((ppem, asc, desc, widest, data_at, offsets, glyphs))

    # EBLC: header, one bitmapSizeTable per strike, then each strike's
    # indexSubTableArray and its subtables.
    nsz = len(sizes) + len(keep)
    tail = b""
    tables = []
    for ppem, asc, desc, widest, data_at, offsets, glyphs in sizes:
        rr = runs(gids)
        arr = b""
        subs = b""
        for first, last in rr:
            arr += struct.pack(">HHI", first, last, len(rr) * 8 + len(subs))
            body = struct.pack(">HHI", 1, 2, data_at)
            for g in range(first, last + 1):
                body += struct.pack(">I", offsets[g])
            nxt = [x for x in gids if x > last]
            body += struct.pack(">I", offsets[nxt[0]] if nxt else offsets["end"])
            subs += body
        tables.append((ppem, asc, desc, widest, len(rr), arr + subs))

    hdr_len = 8 + nsz * 48
    eblc = bytearray(struct.pack(">II", 0x00020000, nsz))
    at = hdr_len
    # the kept strikes first, their bitmapSizeTable copied whole and only the
    # array offset moved, since the table has grown by the new strikes
    for _ppem, bst, blob in keep:
        eblc += struct.pack(">I", at) + bst[4:]
        at += len(blob)
    for ppem, asc, desc, widest, nidx, blob in tables:
        eblc += struct.pack(">III", at, len(blob), nidx)
        eblc += struct.pack(">I", 0)                       # colorRef
        for _ in range(2):                                 # hori then vert
            eblc += s8(asc) + s8(desc) + bytes([min(widest, 255)])
            eblc += s8(0) + s8(0) + s8(0) + s8(0) + s8(0) + s8(0) + s8(0)
            eblc += b"\0\0"
        eblc += struct.pack(">HH", min(gids), max(gids))
        eblc += bytes([ppem, ppem, 1]) + s8(1)
        at += len(blob)
    for _ppem, _bst, blob in keep:
        eblc += blob
    for _, _, _, _, _, blob in tables:
        eblc += blob
    return bytes(eblc), bytes(ebdt)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--ppem", required=True,
                    help="comma-separated pixel sizes")
    ap.add_argument("--chars", default="32-255",
                    help="character ranges to cover, e.g. 32-255")
    ap.add_argument("--dpi", type=int, default=0,
                    help="with --sizes, take line metrics from the unrounded"
                         " pixel size rather than the integer ppem")
    ap.add_argument("--sizes", default="",
                    help="the point sizes the ppem list came from")
    ap.add_argument("--keep-outlines", action="store_true",
                    help="keep glyf/loca and the layout tables (see below)")
    args = ap.parse_args()

    ppems = sorted({int(x) for x in args.ppem.split(",") if x.strip()})
    face = freetype.Face(args.src)

    wanted = set()
    for part in args.chars.split(","):
        lo, hi = (part.split("-") + [part])[:2]
        for c in range(int(lo), int(hi) + 1):
            g = face.get_char_index(c)
            if g:
                wanted.add(g)
    gids = sorted(wanted)

    tables = read_tables(open(args.src, "rb").read())
    keep, ebdt0 = existing(tables)
    have = {p for p, _, _ in keep}
    todo = [p for p in ppems if p not in have]
    if have:
        print("  keeping %d existing strikes %s untouched"
              % (len(have), sorted(have)))
    real = {}
    if args.dpi:
        for pt in [float(x) for x in args.sizes.split(",")] if args.sizes else []:
            px = pt * args.dpi / 72.0
            real[int((pt * 20 * args.dpi + 720) // 1440)] = px
    eblc, ebdt = build(face, todo, gids, keep, ebdt0, real, tables)
    tables["EBLC"], tables["EBDT"] = eblc, ebdt

    if not args.keep_outlines:
        # **The outlines are dropped, and the result is a bitmap-only font.**
        #
        # src/font.c reads exactly seven tables -- cmap, head, hhea, hmtx,
        # maxp, EBLC, EBDT -- and never touches `glyf`. Measured on the first
        # full build: 210K of what it reads against 389K of what it does not,
        # with `glyf` alone 269K for 2620 outlines where 191 glyphs are
        # covered.
        #
        # `name` and `OS/2` are kept although nothing reads them, because a
        # substitute face should carry its own provenance and licence rather
        # than rely on a comment in a source file the binary does not ship.
        #
        # This is what MS Sans Serif already is -- a bitmap font whose `glyf`
        # is a stub -- so it is a shape this renderer already handles, not a
        # new kind of file. Pass --keep-outlines to get a font a rasteriser
        # could later use; nothing here needs it, and the source font is a
        # command line away.
        for t in list(tables):
            if t not in ("cmap", "head", "hhea", "hmtx", "maxp",
                         "EBLC", "EBDT", "name", "OS/2"):
                del tables[t]
    open(args.out, "wb").write(build_sfnt(tables))
    # **The count is the strikes in the file, not the ppem asked for.** This
    # printed len(ppems) and said "16 strikes" of a Tahoma that had nineteen:
    # eight kept and eleven added, with five of the requested sixteen already
    # present. A tool that reports its input as its output is the shape this
    # repository keeps catching, and it was in the line meant to report it.
    total = sorted(have | set(todo))
    print("  %s: %d strikes %d..%d, %d glyphs, EBLC %d + EBDT %d bytes"
          % (args.out, len(total), total[0], total[-1],
             len(gids), len(eblc), len(ebdt)))


if __name__ == "__main__":
    main()
