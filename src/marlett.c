/* Marlett caption glyphs, rendered from the font's own `glyf` outlines with
 * FreeType-equivalent monochrome scan conversion.
 *
 * Wine's DrawFrameControl (dlls/user32/uitools.c) selects Marlett at the
 * button size and TextOuts the glyph; on X11 desktops that lands in
 * FreeType's mono rasteriser. Marlett's caption glyphs are straight-line
 * polygons with no hinting instructions (verified: no fpgm/prep, empty
 * per-glyph programs), so FreeType's output is plain scan conversion of the
 * scaled outline — reproduced here exactly: coordinates scaled to the 26.6
 * grid (round-to-nearest 1/64 per point, like FT_MulFix), the ink box
 * grid-fitted (floor/ceil), nonzero-winding fill sampled at pixel centres.
 * Verified bit-identical to FreeType (and to the win2k_popup_wine reference
 * capture) at the caption sizes. */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

static uint16_t rd16(const unsigned char *p, size_t o)
{
    return (uint16_t)((p[o] << 8) | p[o + 1]);
}

static int16_t rdi16(const unsigned char *p, size_t o)
{
    return (int16_t)rd16(p, o);
}

static uint32_t rd32(const unsigned char *p, size_t o)
{
    return ((uint32_t)rd16(p, o) << 16) | rd16(p, o + 2);
}

static size_t find_table(const unsigned char *ttf, const char tag[4])
{
    uint16_t n = rd16(ttf, 4);
    for (size_t i = 0; i < n; i++) {
        size_t rec = 12 + i * 16;
        if (memcmp(ttf + rec, tag, 4) == 0)
            return rd32(ttf, rec + 8);
    }
    return 0;
}

int ween_marlett_init(ween_marlett *m, const unsigned char *ttf, size_t len)
{
    m->ttf = ttf;
    m->len = len;
    return find_table(ttf, "glyf") != 0 && find_table(ttf, "cmap") != 0;
}


/* Map an ASCII/Mac code to a glyph id via a format-0 or format-4 subtable. */
static uint32_t glyph_id(const unsigned char *ttf, uint32_t code)
{
    size_t cmap = find_table(ttf, "cmap");
    if (!cmap)
        return 0;
    uint16_t ntab = rd16(ttf, cmap + 2);
    for (size_t t = 0; t < ntab; t++) {
        size_t sub = cmap + rd32(ttf, cmap + 4 + t * 8 + 4);
        switch (rd16(ttf, sub)) {
        case 0:
            if (code < 256) {
                unsigned char g = ttf[sub + 6 + code];
                if (g)
                    return g;
            }
            break;
        case 4: {
            size_t segc = rd16(ttf, sub + 6) / 2;
            size_t end_o = sub + 14;
            size_t start_o = end_o + segc * 2 + 2;
            size_t delta_o = start_o + segc * 2;
            size_t range_o = delta_o + segc * 2;
            for (size_t s = 0; s < segc; s++) {
                if (code <= rd16(ttf, end_o + s * 2)) {
                    uint16_t start = rd16(ttf, start_o + s * 2);
                    if (code < start)
                        break;
                    uint16_t delta = rd16(ttf, delta_o + s * 2);
                    uint16_t ro = rd16(ttf, range_o + s * 2);
                    if (ro == 0)
                        return (uint16_t)(code + delta);
                    uint16_t gi = rd16(ttf, range_o + s * 2 + ro + (code - start) * 2);
                    return gi == 0 ? 0 : (uint16_t)(gi + delta);
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return 0;
}

static int glyf_location(const unsigned char *ttf, uint32_t gid, size_t *off, size_t *len)
{
    size_t head = find_table(ttf, "head");
    size_t loca = find_table(ttf, "loca");
    size_t glyf = find_table(ttf, "glyf");
    if (!head || !loca || !glyf)
        return 0;
    int long_loca = rdi16(ttf, head + 50) != 0;
    uint32_t a = long_loca ? rd32(ttf, loca + gid * 4) : (uint32_t)rd16(ttf, loca + gid * 2) * 2;
    uint32_t b = long_loca ? rd32(ttf, loca + gid * 4 + 4) : (uint32_t)rd16(ttf, loca + gid * 2 + 2) * 2;
    if (b <= a)
        return 0;
    *off = glyf + a;
    *len = b - a;
    return 1;
}

#define MAX_POINTS 128
#define MAX_CONTOURS 16

typedef struct {
    float xs[MAX_POINTS];
    float ys[MAX_POINTS]; /* flipped: +y downward */
    size_t ends[MAX_CONTOURS];
    size_t ncontours;
    size_t npoints;
    float upem;
} outline_t;

/* Parse a simple glyph's contour points (straight-line caption glyphs only —
 * off-curve control points are treated as vertices, exact for Marlett). */
static int parse_outline(const unsigned char *ttf, uint32_t code, outline_t *o)
{
    size_t head = find_table(ttf, "head");
    if (!head)
        return 0;
    memset(o, 0, sizeof(*o));
    o->upem = (float)rd16(ttf, head + 18);

    uint32_t gid = glyph_id(ttf, code);
    size_t off, glen;
    if (!glyf_location(ttf, gid, &off, &glen))
        return 0;
    size_t p = off;
    int nc = rdi16(ttf, p);
    if (nc <= 0 || nc > MAX_CONTOURS)
        return 0;
    o->ncontours = (size_t)nc;
    p += 10; /* numberOfContours + bbox */

    size_t npts = 0;
    for (size_t c = 0; c < o->ncontours; c++) {
        npts = (size_t)rd16(ttf, p) + 1;
        o->ends[c] = npts;
        p += 2;
    }
    if (npts > MAX_POINTS)
        return 0;
    o->npoints = npts;

    p += 2 + rd16(ttf, p); /* instructions */

    /* Flags (with repeat). */
    unsigned char flags[MAX_POINTS];
    for (size_t i = 0; i < npts;) {
        unsigned char fl = ttf[p++];
        flags[i++] = fl;
        if (fl & 8) {
            unsigned char r = ttf[p++];
            while (r-- > 0 && i < npts)
                flags[i++] = fl;
        }
    }

    /* X coordinates (delta-encoded). */
    int32_t x = 0;
    for (size_t i = 0; i < npts; i++) {
        unsigned char fl = flags[i];
        if (fl & 2) {
            int32_t dx = ttf[p++];
            x += (fl & 16) ? dx : -dx;
        } else if (!(fl & 16)) {
            x += rdi16(ttf, p);
            p += 2;
        }
        o->xs[i] = (float)x;
    }
    /* Y coordinates (font units, +y upward as in the font). */
    int32_t y = 0;
    for (size_t i = 0; i < npts; i++) {
        unsigned char fl = flags[i];
        if (fl & 4) {
            int32_t dy = ttf[p++];
            y += (fl & 32) ? dy : -dy;
        } else if (!(fl & 32)) {
            y += rdi16(ttf, p);
            p += 2;
        }
        o->ys[i] = (float)y;
    }
    return 1;
}

typedef struct {
    double x;
    int dir; /* +1 upward edge, -1 downward (nonzero winding) */
} crossing_t;

static int cmp_crossing(const void *a, const void *b)
{
    double xa = ((const crossing_t *)a)->x, xb = ((const crossing_t *)b)->x;
    return (xa > xb) - (xa < xb);
}

/* Draw glyph `code` at `size` ppem, its ink centred in the size x size box at
 * (ox, oy) — the placement of the validated reference (which centres the ink
 * extents, XGlyphInfo-style, on the caption button).
 *
 * Rasterisation mirrors FreeType's unhinted monochrome pass: every point is
 * scaled to the 26.6 grid with round-to-nearest (FT_MulFix), the ink box is
 * grid-fitted with floor/ceil, and a pixel is set when its centre lies inside
 * the outline under the nonzero winding rule. */
void ween_marlett_draw(const ween_marlett *m, ween_surface *s, int code,
                       int x_org, int y_org, int size, ween_color color)
{
    outline_t o;
    if (size <= 0 || !parse_outline(m->ttf, (uint32_t)code, &o))
        return;

    /* Scale to the 26.6 grid: round each coordinate to the nearest 1/64th of
     * a pixel (FT_MulFix), in integers. 1/64 multiples are exact in doubles,
     * so the span comparisons below behave like FreeType's fixed point. */
    double k = (double)size * 64.0 / (double)o.upem;
    long X26[MAX_POINTS], Y26[MAX_POINTS];
    long xmin26 = LONG_MAX, xmax26 = LONG_MIN;
    long ymin26 = LONG_MAX, ymax26 = LONG_MIN;
    for (size_t i = 0; i < o.npoints; i++) {
        double fx = (double)o.xs[i] * k;
        double fy = (double)o.ys[i] * k;
        X26[i] = fx >= 0 ? (long)(fx + 0.5) : -(long)(-fx + 0.5);
        Y26[i] = fy >= 0 ? (long)(fy + 0.5) : -(long)(-fy + 0.5);
        if (X26[i] < xmin26)
            xmin26 = X26[i];
        if (X26[i] > xmax26)
            xmax26 = X26[i];
        if (Y26[i] < ymin26)
            ymin26 = Y26[i];
        if (Y26[i] > ymax26)
            ymax26 = Y26[i];
    }

    /* Grid-fitted ink box, ftobjs.c ft_glyphslot_preset_bitmap (MONO):
     * "bbox values get rounded; we do asymmetric rounding so that the center
     * of a pixel gets always included" — min edges round half DOWN (+31),
     * max edges half UP (+32). */
    int gx0 = (int)((xmin26 + 31) >> 6), gx1 = (int)((xmax26 + 32) >> 6);
    int gy0 = (int)((ymin26 + 31) >> 6), gy1 = (int)((ymax26 + 32) >> 6);
    int w = gx1 - gx0, h = gy1 - gy0;
    if (w <= 0 || h <= 0)
        return;
    int bx = x_org + size / 2 - w / 2;
    int by = y_org + size / 2 - h / 2;

    for (int row = 0; row < h; row++) {
        /* pixel-centre scanline, top row first (+y is up in font space) */
        double yc = (double)gy1 - (double)row - 0.5;
        crossing_t cr[MAX_POINTS];
        size_t nc = 0;
        size_t start = 0;
        for (size_t c = 0; c < o.ncontours; c++) {
            size_t end = o.ends[c];
            for (size_t i = start; i < end; i++) {
                size_t j = i + 1 < end ? i + 1 : start;
                double y0 = (double)Y26[i] / 64.0, y1 = (double)Y26[j] / 64.0;
                if (y0 == y1)
                    continue; /* horizontal edge: no crossing */
                /* ftraster's profile coverage: an ascending edge covers
                 * (start, end], a descending one likewise excludes its
                 * start scanline — EXCEPT when the start vertex joins a
                 * horizontal edge (a flat), which ftraster's profile-join
                 * handling extends to cover the flat's scanline. */
                int up = y1 > y0;
                if (up ? !(y0 < yc && yc <= y1) : !(y1 <= yc && yc < y0)) {
                    size_t prev = i > start ? i - 1 : end - 1;
                    if (!(yc == y0 && Y26[prev] == Y26[i]))
                        continue;
                }
                double x0 = (double)X26[i] / 64.0, x1 = (double)X26[j] / 64.0;
                double t = (yc - y0) / (y1 - y0);
                cr[nc].x = x0 + t * (x1 - x0);
                cr[nc].dir = up ? 1 : -1;
                nc++;
            }
            start = end;
        }
        if (nc < 2)
            continue;
        qsort(cr, nc, sizeof(cr[0]), cmp_crossing);

        /* Nonzero-winding sweep; unbalanced tails are dropped. Fill pixels
         * whose centre gx0+px+0.5 lies in [x_start, x_end] — BOTH ends
         * inclusive, as in ftraster's Vertical_Sweep_Span (e1 = CEILING(x1),
         * e2 = FLOOR(x2), filled inclusively on the shifted grid). */
        int acc = 0;
        double span_x = 0;
        for (size_t i = 0; i < nc; i++) {
            int was = acc;
            acc += cr[i].dir;
            if (was == 0 && acc != 0) {
                span_x = cr[i].x;
            } else if (was != 0 && acc == 0) {
                double v0 = span_x - (double)gx0 - 0.5;
                double v1 = cr[i].x - (double)gx0 - 0.5;
                int p0 = (int)v0;
                if ((double)p0 < v0)
                    p0++; /* ceil */
                int p1 = (int)v1;
                if ((double)p1 > v1)
                    p1--; /* floor, inclusive */
                if (p0 < 0)
                    p0 = 0;
                for (int px = p0; px <= p1 && px < w; px++)
                    ween_surface_pixel(s, bx + px, by + row, color);
            }
        }
    }

    /* ftraster's second (horizontal) sweep fix: "the vertical sweep
     * mishandles horizontal lines through pixel centers". Any horizontal
     * edge lying exactly on a pixel-centre scanline gets its pixels set
     * directly (Horizontal_Sweep_Span's aligned-edge case). */
    {
        size_t start = 0;
        for (size_t c = 0; c < o.ncontours; c++) {
            size_t end = o.ends[c];
            for (size_t i = start; i < end; i++) {
                size_t j = i + 1 < end ? i + 1 : start;
                if (Y26[i] != Y26[j])
                    continue; /* not horizontal */
                if (((Y26[i] - 32) & 63) != 0)
                    continue; /* not on a pixel-centre scanline */
                double ye = (double)Y26[i] / 64.0;
                int row = gy1 - (int)(ye + 0.5);
                if (row < 0 || row >= h)
                    continue;
                double xa = (double)(X26[i] < X26[j] ? X26[i] : X26[j]) / 64.0;
                double xb = (double)(X26[i] < X26[j] ? X26[j] : X26[i]) / 64.0;
                double v0 = xa - (double)gx0 - 0.5;
                double v1 = xb - (double)gx0 - 0.5;
                int p0 = (int)v0;
                if ((double)p0 < v0)
                    p0++;
                int p1 = (int)v1;
                if ((double)p1 > v1)
                    p1--;
                if (p0 < 0)
                    p0 = 0;
                for (int px = p0; px <= p1 && px < w; px++)
                    ween_surface_pixel(s, bx + px, by + row, color);
            }
            start = end;
        }
    }
}
