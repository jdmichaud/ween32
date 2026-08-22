/* Bitmap-strike text (port of telemouse's font.zig).
 *
 * Classic GDI text was drawn from the hand-tuned 1-bit bitmaps embedded in the
 * font (Tahoma at 11 ppem: the EBLC/EBDT tables) — not from rasterised
 * outlines. We read those same bitmaps straight out of the font, so the result
 * is pixel-identical to the reference on every platform: no rasteriser, no
 * FreeType, no antialiasing.
 *
 * Supports the layout Tahoma's strikes use: format-4 cmap, EBLC index format 1,
 * EBDT image format 2 (small metrics, bit-aligned data). */

#include <string.h>

#include "ween_internal.h"

static uint16_t rd16(const unsigned char *p, size_t o)
{
    return (uint16_t)((p[o] << 8) | p[o + 1]);
}

static uint32_t rd32(const unsigned char *p, size_t o)
{
    return ((uint32_t)rd16(p, o) << 16) | rd16(p, o + 2);
}

static int8_t rdi8(const unsigned char *p, size_t o)
{
    return (int8_t)p[o];
}

static size_t find_table(const unsigned char *ttf, const char tag[4])
{
    uint16_t n = rd16(ttf, 4);
    for (size_t i = 0; i < n; i++) {
        size_t o = 12 + i * 16;
        if (memcmp(ttf + o, tag, 4) == 0)
            return rd32(ttf, o + 8);
    }
    return 0;
}

int ween_strike_init(ween_strike *f, const unsigned char *ttf, size_t len, int ppem)
{
    memset(f, 0, sizeof(*f));
    f->ttf = ttf;
    f->len = len;

    size_t cmap = find_table(ttf, "cmap");
    size_t eblc = find_table(ttf, "EBLC");
    f->ebdt = find_table(ttf, "EBDT");
    if (!cmap || !eblc || !f->ebdt)
        return 0;

    /* Pick a format-4 cmap subtable (prefer platform 3 / encoding 1). */
    uint16_t ntab = rd16(ttf, cmap + 2);
    for (size_t i = 0; i < ntab; i++) {
        size_t e = cmap + 4 + i * 8;
        uint16_t pid = rd16(ttf, e);
        uint16_t eid = rd16(ttf, e + 2);
        size_t sub = cmap + rd32(ttf, e + 4);
        if (rd16(ttf, sub) == 4) {
            if (pid == 3 && eid == 1)
                f->cmap4 = sub;
            else if (f->cmap4 == 0)
                f->cmap4 = sub;
        }
    }
    if (!f->cmap4)
        return 0;

    /* Pick the strike nearest the requested ppem. */
    uint32_t num_strikes = rd32(ttf, eblc + 4);
    size_t chosen = eblc + 8;
    int best = 999;
    for (uint32_t s = 0; s < num_strikes; s++) {
        size_t base = eblc + 8 + s * 48;
        int d = (int)ttf[base + 44] - ppem;
        if (d < 0)
            d = -d;
        if (d < best) {
            best = d;
            chosen = base;
        }
    }
    f->isa = eblc + rd32(ttf, chosen);
    f->nidx = rd32(ttf, chosen + 8);
    f->ascent = rdi8(ttf, chosen + 16); /* hori sbitLineMetrics: the baseline */

    /* Two heights are in play, and both are measured from the reference: the
     * strike's own cell (ascender + descender + 1) is what labels are centred
     * within, while multi-line text is laid out on the outline's taller cell.
     *
     * GDI reports the *logical* font's character widths, not the strike's:
     * they come from the outline (hmtx), rounded up. Drawing still uses the
     * strike's own advances, so a measured string comes out a shade wider than
     * it renders — a discrepancy real GDI has too, and one that visibly moves
     * centred labels. */
    f->cell_h = rdi8(ttf, chosen + 16) - rdi8(ttf, chosen + 17) + 1;
    f->ppem = ttf[chosen + 45];
    f->hmtx = find_table(ttf, "hmtx");
    {
        size_t hh = find_table(ttf, "hhea");
        size_t hd = find_table(ttf, "head");
        f->nhmtx = hh ? rd16(ttf, hh + 34) : 0;
        f->upem = hd ? rd16(ttf, hd + 18) : 0;
    }

    /* The cell height (GDI's tmAscent/tmDescent) comes from the scaled hhea
     * metrics, not the strike (whose descender some fonts leave at 0). */
    size_t head = find_table(ttf, "head");
    size_t hhea = find_table(ttf, "hhea");
    int strike_ppem = ttf[chosen + 45]; /* ppemY */
    if (head && hhea && strike_ppem > 0) {
        int upem = rd16(ttf, head + 18);
        int asc = (int16_t)rd16(ttf, hhea + 4);
        int desc = (int16_t)rd16(ttf, hhea + 6); /* negative */
        if (upem > 0) {
            f->ascent = (asc * strike_ppem + upem / 2) / upem;
            f->descent = -((-desc * strike_ppem + upem / 2) / upem);
        }
    }
    return 1;
}

static uint16_t glyph_index(const ween_strike *f, uint16_t cp)
{
    const unsigned char *ttf = f->ttf;
    size_t c = f->cmap4;
    uint16_t seg_x2 = rd16(ttf, c + 6);
    size_t segc = seg_x2 / 2;
    size_t end_o = c + 14;
    size_t start_o = end_o + seg_x2 + 2;
    size_t delta_o = start_o + seg_x2;
    size_t range_o = delta_o + seg_x2;
    for (size_t s = 0; s < segc; s++) {
        uint16_t end = rd16(ttf, end_o + s * 2);
        if (cp <= end) {
            uint16_t start = rd16(ttf, start_o + s * 2);
            if (cp < start)
                return 0;
            uint16_t delta = rd16(ttf, delta_o + s * 2);
            uint16_t ro = rd16(ttf, range_o + s * 2);
            if (ro == 0)
                return (uint16_t)(cp + delta);
            uint16_t gi = rd16(ttf, range_o + s * 2 + ro + (size_t)(cp - start) * 2);
            return gi == 0 ? 0 : (uint16_t)(gi + delta);
        }
    }
    return 0;
}

typedef struct {
    int w, h, bx, by, adv;
    size_t data;
} ween_glyph;

/* Locate glyph g's small-metrics bitmap (EBLC index format 1 / EBDT format 2). */
static int glyph_bitmap(const ween_strike *f, uint16_t g, ween_glyph *out)
{
    const unsigned char *ttf = f->ttf;
    for (size_t k = 0; k < f->nidx; k++) {
        size_t e = f->isa + k * 8;
        uint16_t fg = rd16(ttf, e);
        uint16_t lg = rd16(ttf, e + 2);
        if (g < fg || g > lg)
            continue;
        size_t ist = f->isa + rd32(ttf, e + 4);
        uint32_t image_data_off = rd32(ttf, ist + 4);
        size_t offs = ist + 8;
        uint32_t o0 = rd32(ttf, offs + (size_t)(g - fg) * 4);
        uint32_t o1 = rd32(ttf, offs + (size_t)(g - fg + 1) * 4);
        if (o0 == o1)
            return 0; /* blank glyph (e.g. space) */
        size_t base = f->ebdt + image_data_off + o0;
        out->h = ttf[base];
        out->w = ttf[base + 1];
        out->bx = rdi8(ttf, base + 2);
        out->by = rdi8(ttf, base + 3);
        out->adv = ttf[base + 4];
        out->data = base + 5;
        return 1;
    }
    return 0;
}

static int blank_advance(void)
{
    return 3; /* space and unknown glyphs */
}

/* Where the shipped Tahoma's own eleven-pixel strike disagrees with the one
 * Windows 2000 has.
 *
 * The advance comes out of the font file — these faces carry a hand-tuned
 * bitmap for this size, advances and all — so this is a difference between
 * two Tahomas rather than a rounding of ours. It shows up as a word being a
 * pixel wide: "History" on a toolbar comes to 34 here and 33 there, and
 * everything laid out after it is a pixel out.
 *
 * Measured against a running Windows 2000: 'y' is five there and six here.
 * The letter is otherwise identical — the ink matches, only the step to the
 * next glyph does not. Kept as a list rather than a rounding rule because
 * these are hinted values, and there is no rule that reproduces them.
 */
static int tahoma_11_fix(const ween_strike *f, unsigned char c, int adv)
{
    if (f->ppem != 11 || f->embolden)
        return adv;
    if (c == 'y')
        return adv - 1;
    return adv;
}

int ween_strike_char_advance(const ween_strike *f, unsigned char c)
{
    ween_glyph g;
    if (!glyph_bitmap(f, glyph_index(f, c), &g))
        return blank_advance();
    return tahoma_11_fix(f, c, g.adv + f->embolden);
}

/* The advance GDI would report for a character: the outline's, scaled to the
 * strike's ppem and rounded up. */
/* A character's advance in the font's own design units, before any rounding
 * to pixels. Measuring a whole string means summing these and rounding once;
 * rounding each one first is what made long strings drift wide. */
uint32_t ween_strike_char_units(const ween_strike *f, unsigned char c)
{
    uint16_t g;
    if (!f->hmtx || !f->upem || !f->nhmtx)
        return 0;
    g = glyph_index(f, c);
    if (g >= f->nhmtx)
        g = (uint16_t)(f->nhmtx - 1);
    return rd16(f->ttf, f->hmtx + 4u * g);
}

int ween_strike_char_extent(const ween_strike *f, unsigned char c)
{
    uint32_t units;
    if (!f->hmtx || !f->upem || !f->nhmtx)
        return ween_strike_char_advance(f, c);
    units = ween_strike_char_units(f, c);
    return (int)((units * (uint32_t)f->ppem + f->upem - 1) / f->upem);
}

int ween_strike_text_extent(const ween_strike *f, const char *s, int len)
{
    int w = 0;
    for (int i = 0; i < len; i++)
        w += ween_strike_char_extent(f, (unsigned char)s[i]);
    return w;
}

int ween_strike_text_width(const ween_strike *f, const char *s, int len)
{
    int w = 0;
    for (int i = 0; i < len; i++)
        w += ween_strike_char_advance(f, (unsigned char)s[i]);
    return w;
}

static int draw_char(const ween_strike *f, ween_surface *s, int pen_x,
                     int baseline, unsigned char c, ween_color color)
{
    ween_glyph g;
    if (!glyph_bitmap(f, glyph_index(f, c), &g))
        return blank_advance();
    int top = baseline - g.by;
    int left = pen_x + g.bx;
    for (int row = 0; row < g.h; row++) {
        for (int col = 0; col < g.w; col++) {
            size_t bi = (size_t)(row * g.w + col);
            if ((f->ttf[g.data + bi / 8] >> (7 - bi % 8)) & 1) {
                ween_surface_pixel(s, left + col, top + row, color);
                if (f->embolden) /* synthetic bold: 1px overstrike */
                    ween_surface_pixel(s, left + col + 1, top + row, color);
            }
        }
    }
    return g.adv + f->embolden;
}

void ween_strike_draw(const ween_strike *f, ween_surface *s, int x, int y,
                      const char *text, int len, ween_color color)
{
    int baseline = y + f->ascent;
    int pen = x;
    for (int i = 0; i < len; i++)
        pen += draw_char(f, s, pen, baseline, (unsigned char)text[i], color);
}

/* Where the caret sits before character `index`.
 *
 * Text in an edit is laid out on the strike's own advances, the same as a
 * label or a list item. Wine spreads an edit's characters out to the width
 * GDI *reports* instead, which is wider — the letters come out unevenly
 * spaced, and inserting one character shifts the ones after it by a pixel or
 * two. Windows did not look like that, and neither do we. */
int ween_strike_pen(const ween_strike *f, const char *text, int index)
{
    int pen = 0;
    for (int i = 0; i < index; i++)
        pen += ween_strike_char_advance(f, (unsigned char)text[i]);
    return pen;
}
