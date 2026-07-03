/* Marlett caption glyphs (port of telemouse's marlett.zig), rendered from the
 * font's own `glyf` outlines. The caption symbols (close/min/max/restore) are
 * simple straight-line polygons, so an even-odd scanline fill reproduces them
 * pixel-for-pixel — no curve flattening or hinting. These are the same glyphs
 * Windows drew for DrawFrameControl(DFC_CAPTION, ...). */

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
    /* Y coordinates, flipped so downward is positive. */
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
        o->ys[i] = o->upem - (float)y;
    }
    return 1;
}

static int cmp_float(const void *a, const void *b)
{
    float fa = *(const float *)a, fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

/* Draw glyph `code` filling a size x size box at (ox, oy): 1-bit even-odd
 * scanline fill — crisp, no antialiasing. */
void ween_marlett_draw(const ween_marlett *m, ween_surface *s, int code,
                       int x_org, int y_org, int size, ween_color color)
{
    outline_t o;
    if (!parse_outline(m->ttf, (uint32_t)code, &o))
        return;
    float scale = (float)size / o.upem;

    for (int row = 0; row < size; row++) {
        float yc = (float)row + 0.5f;
        float xs[MAX_POINTS];
        size_t nx = 0;
        size_t start = 0;
        for (size_t c = 0; c < o.ncontours; c++) {
            size_t end = o.ends[c];
            for (size_t i = start; i < end; i++) {
                size_t j = i + 1 < end ? i + 1 : start;
                float y0 = o.ys[i] * scale;
                float y1 = o.ys[j] * scale;
                if ((y0 <= yc && yc < y1) || (y1 <= yc && yc < y0)) {
                    float t = (yc - y0) / (y1 - y0);
                    xs[nx++] = (o.xs[i] + t * (o.xs[j] - o.xs[i])) * scale;
                }
            }
            start = end;
        }
        if (nx < 2)
            continue;
        qsort(xs, nx, sizeof(float), cmp_float);
        for (size_t k = 0; k + 1 < nx; k += 2) {
            /* GDI executed Marlett's hinting, which snaps every stroke to a
             * uniform whole-pixel width; emulate that by quantizing the span
             * to an integer width (min 1) centred on the true span, so the
             * diagonals of the caption glyphs come out even, not ragged. */
            float width = xs[k + 1] - xs[k];
            int wpx = (int)(width + 0.5f);
            if (wpx < 1)
                wpx = 1;
            float centre = (xs[k] + xs[k + 1]) * 0.5f;
            int xa = (int)(centre - (float)wpx * 0.5f + 0.5f);
            for (int px = xa; px < xa + wpx; px++)
                ween_surface_pixel(s, x_org + px, y_org + row, color);
        }
    }
}
