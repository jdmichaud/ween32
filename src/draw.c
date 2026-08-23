/* The drawing half of GDI: pens, shapes, memory device contexts and blits.
 *
 * gdi.c has the parts the classic chrome needs — a brush, a rectangle, some
 * text. This file has the parts a *drawing program* needs, which is a rather
 * different list: a bitmap you can select into a context and draw on, a pen
 * with a width and a raster operation, lines and ellipses and polygons, a
 * flood fill, and BitBlt to move the result about.
 *
 * Two things are worth knowing before reading it.
 *
 * The raster operations are implemented in general, not case by case. The
 * high byte of a ROP3 code is the truth table of (pattern, source, dest):
 * bit n of it is the result when (P,S,D) spell n. Evaluating that on whole
 * 32-bit words at once gives every one of the 256 codes for the price of
 * eight ANDs, and there is no list of special cases to be incomplete.
 *
 * Coordinates arriving here are the caller's — window-relative for a window
 * DC, bitmap-relative for a memory one — and become surface coordinates by
 * adding the DC's origin. Everything draws through the surface's clip
 * rectangle narrowed to the DC's own box, so a window cannot draw past its
 * client area and a bitmap cannot be drawn past its edge.
 */

#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

static ween_color cr_px(COLORREF c)
{
    return ween_cr_to_px(c);
}

static COLORREF px_cr(ween_color p)
{
    return RGB((p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff);
}

/* ---- clipping ------------------------------------------------------------ */

/* Narrow the surface's clip to this DC's drawing area, and give back what it
 * was so it can be put back. Every entry point in this file brackets its
 * drawing with these two. */
static void clip_push(HDC dc, RECT *saved)
{
    RECT c;
    ween_surface_get_clip(dc->s, saved);
    c.left = dc->org_x;
    c.top = dc->org_y;
    c.right = dc->org_x + dc->clip_w;
    c.bottom = dc->org_y + dc->clip_h;
    if (c.left < saved->left)
        c.left = saved->left;
    if (c.top < saved->top)
        c.top = saved->top;
    if (c.right > saved->right)
        c.right = saved->right;
    if (c.bottom > saved->bottom)
        c.bottom = saved->bottom;
    if (c.right < c.left)
        c.right = c.left;
    if (c.bottom < c.top)
        c.bottom = c.top;
    ween_surface_clip(dc->s, c.left, c.top, c.right - c.left, c.bottom - c.top);
}

static void clip_pop(HDC dc, const RECT *saved)
{
    ween_surface_clip(dc->s, saved->left, saved->top, saved->right - saved->left,
                      saved->bottom - saved->top);
}

/* ---- raster operations --------------------------------------------------- */

/* The binary ones, as SetROP2 selects them: pen against destination. */
static ween_color rop2_apply(int rop, ween_color p, ween_color d)
{
    switch (rop) {
    case R2_BLACK:
        return 0;
    case R2_NOTMERGEPEN:
        return ~(p | d) & 0xffffffu;
    case R2_MASKNOTPEN:
        return (~p & d) & 0xffffffu;
    case R2_NOTCOPYPEN:
        return ~p & 0xffffffu;
    case R2_MASKPENNOT:
        return (p & ~d) & 0xffffffu;
    case R2_NOT:
        return ~d & 0xffffffu;
    case R2_XORPEN:
        return (p ^ d) & 0xffffffu;
    case R2_NOTMASKPEN:
        return ~(p & d) & 0xffffffu;
    case R2_MASKPEN:
        return (p & d) & 0xffffffu;
    case R2_NOTXORPEN:
        return ~(p ^ d) & 0xffffffu;
    case R2_NOP:
        return d;
    case R2_MERGENOTPEN:
        return (~p | d) & 0xffffffu;
    case R2_MERGEPENNOT:
        return (p | ~d) & 0xffffffu;
    case R2_MERGEPEN:
        return (p | d) & 0xffffffu;
    case R2_WHITE:
        return 0xffffffu;
    case R2_COPYPEN:
    default:
        return p;
    }
}

/* The ternary ones, as BitBlt takes them. The truth table is the high byte
 * of the code; every bit of the three inputs is combined independently, so
 * whole pixels go through at once. */
static ween_color rop3_apply(unsigned char rop, ween_color p, ween_color s,
                             ween_color d)
{
    ween_color r = 0;
    if (rop & 0x01)
        r |= ~p & ~s & ~d;
    if (rop & 0x02)
        r |= ~p & ~s & d;
    if (rop & 0x04)
        r |= ~p & s & ~d;
    if (rop & 0x08)
        r |= ~p & s & d;
    if (rop & 0x10)
        r |= p & ~s & ~d;
    if (rop & 0x20)
        r |= p & ~s & d;
    if (rop & 0x40)
        r |= p & s & ~d;
    if (rop & 0x80)
        r |= p & s & d;
    return r & 0xffffffu;
}

/* Whether a ROP3 looks at the source at all — a PATCOPY or a DSTINVERT does
 * not, and must not be refused for want of a source DC. */
static int rop_uses_source(unsigned char rop)
{
    return ((rop >> 2) & 0x33) != (rop & 0x33);
}

/* ---- pixels -------------------------------------------------------------- */

/* One pixel in DC coordinates, through the DC's binary raster operation. */
static void put(HDC dc, int x, int y, ween_color c)
{
    ween_surface *s = dc->s;
    int sx = dc->org_x + x, sy = dc->org_y + y;
    if (sx < s->clip_x || sy < s->clip_y || sx >= s->clip_r || sy >= s->clip_b)
        return;
    if (!dc->rop2 || dc->rop2 == R2_COPYPEN)
        s->px[(long)sy * s->w + sx] = c;
    else
        s->px[(long)sy * s->w + sx] =
            rop2_apply(dc->rop2, c, s->px[(long)sy * s->w + sx]);
}

static ween_color peek(HDC dc, int x, int y)
{
    ween_surface *s = dc->s;
    int sx = dc->org_x + x, sy = dc->org_y + y;
    if (sx < 0 || sy < 0 || sx >= s->w || sy >= s->h)
        return 0;
    return s->px[(long)sy * s->w + sx] & 0xffffffu;
}

/* ---- the objects a DC draws with ----------------------------------------- */

static const ween_gdiobj *dc_pen(HDC dc)
{
    static ween_gdiobj black = { .kind = WEEN_OBJ_PEN, .color = 0,
                                 .pen_style = PS_SOLID, .pen_width = 1,
                                 .is_static = 1 };
    return dc->pen_obj ? dc->pen_obj : &black;
}

static const ween_gdiobj *dc_brush(HDC dc)
{
    static ween_gdiobj white = { .kind = WEEN_OBJ_BRUSH, .color = 0x00ffffffu,
                                 .is_static = 1 };
    return dc->brush_obj ? dc->brush_obj : &white;
}

HPEN CreatePen(int style, int width, COLORREF color)
{
    ween_gdiobj *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->kind = WEEN_OBJ_PEN;
    p->color = cr_px(color);
    p->pen_style = style;
    p->pen_width = width < 1 ? 1 : width;
    p->is_null = (style == PS_NULL);
    return p;
}

int SetROP2(HDC dc, int mode)
{
    int prev;
    if (!dc)
        return 0;
    prev = dc->rop2 ? dc->rop2 : R2_COPYPEN;
    dc->rop2 = mode;
    return prev;
}

int GetROP2(HDC dc)
{
    return dc && dc->rop2 ? dc->rop2 : R2_COPYPEN;
}

COLORREF SetBkColor(HDC dc, COLORREF color)
{
    COLORREF prev;
    if (!dc)
        return 0;
    prev = dc->bk_color;
    dc->bk_color = color;
    return prev;
}

COLORREF GetBkColor(HDC dc)
{
    return dc ? dc->bk_color : 0;
}

int SetStretchBltMode(HDC dc, int mode)
{
    int prev;
    if (!dc)
        return 0;
    prev = dc->stretch_mode ? dc->stretch_mode : COLORONCOLOR;
    dc->stretch_mode = mode;
    return prev;
}

/* ---- memory device contexts ---------------------------------------------- */

HDC CreateCompatibleDC(HDC ref)
{
    struct ween_dc *dc = calloc(1, sizeof(*dc));
    if (!dc)
        return NULL;
    /* GDI hands back a context with a 1x1 bitmap already in it, so that one
     * exists before anything is selected. */
    dc->default_bitmap.kind = WEEN_OBJ_BITMAP;
    dc->default_bitmap.is_static = 1;
    if (!ween_surface_init(&dc->default_bitmap.bitmap, 1, 1)) {
        free(dc);
        return NULL;
    }
    dc->bitmap_obj = &dc->default_bitmap;
    dc->s = &dc->default_bitmap.bitmap;
    dc->clip_w = 1;
    dc->clip_h = 1;
    dc->is_memory = 1;
    dc->text_color = 0;
    dc->bk_color = RGB(255, 255, 255);
    dc->bk_mode = TRANSPARENT;
    ween_dc_set_font(dc, ref && ref->font ? ref->font : ween_gui_font());
    return dc;
}

BOOL DeleteDC(HDC dc)
{
    if (!dc || !dc->is_memory)
        return FALSE;
    ween_surface_free(&dc->default_bitmap.bitmap);
    free(dc);
    return TRUE;
}

HBITMAP CreateCompatibleBitmap(HDC dc, int w, int h)
{
    ween_gdiobj *b;
    (void)dc;
    if (w <= 0 || h <= 0)
        return NULL;
    b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->kind = WEEN_OBJ_BITMAP;
    if (!ween_surface_init(&b->bitmap, w, h)) {
        free(b);
        return NULL;
    }
    return b;
}

int GetObjectA(HGDIOBJ obj, int size, LPVOID out)
{
    if (!obj || !out)
        return 0;
    if (obj->kind == WEEN_OBJ_BITMAP || obj->kind == WEEN_OBJ_ICON) {
        BITMAP bm;
        if (size < (int)sizeof(bm))
            return 0;
        memset(&bm, 0, sizeof(bm));
        bm.bmWidth = obj->bitmap.w;
        bm.bmHeight = obj->bitmap.h;
        bm.bmWidthBytes = obj->bitmap.w * 4;
        bm.bmPlanes = 1;
        bm.bmBitsPixel = 32;
        memcpy(out, &bm, sizeof(bm));
        return (int)sizeof(bm);
    }
    return 0;
}

/* Selecting a bitmap only means anything in a memory DC, and it is what
 * points the context at a different set of pixels. */
HGDIOBJ ween_select_bitmap(HDC dc, HGDIOBJ obj)
{
    HGDIOBJ prev = dc->bitmap_obj;
    if (!dc->is_memory)
        return NULL;
    dc->bitmap_obj = obj;
    dc->s = &obj->bitmap;
    dc->org_x = dc->org_y = 0;
    dc->clip_w = obj->bitmap.w;
    dc->clip_h = obj->bitmap.h;
    ween_surface_clip(dc->s, 0, 0, obj->bitmap.w, obj->bitmap.h);
    return prev;
}

/* ---- lines --------------------------------------------------------------- */

/* A pen wider than one pixel puts down a square of that width at every point
 * of the line, centred as GDI centres it: the extra pixels of an even width
 * go up and left. */
static void pen_dot(HDC dc, const ween_gdiobj *pen, int x, int y)
{
    int w = pen->pen_width;
    if (w <= 1) {
        put(dc, x, y, pen->color);
        return;
    }
    for (int dy = 0; dy < w; dy++)
        for (int dx = 0; dx < w; dx++)
            put(dc, x - (w - 1) / 2 + dx, y - (w - 1) / 2 + dy, pen->color);
}

/* PS_DASH and friends: the pattern GDI walks along a cosmetic pen, in pixels
 * on and pixels off. A pen wider than one unit ignores its style, as GDI's
 * does. */
static int pen_on(const ween_gdiobj *pen, int step)
{
    if (pen->pen_width > 1)
        return 1;
    switch (pen->pen_style) {
    case PS_DASH:
        return (step % 27) < 18;
    case PS_DOT:
        return (step % 6) < 3;
    case PS_DASHDOT:
        return (step % 21) < 9 || ((step % 21) >= 12 && (step % 21) < 15);
    case PS_DASHDOTDOT:
        return (step % 24) < 9 || ((step % 24) >= 12 && (step % 24) < 15) ||
               ((step % 24) >= 18 && (step % 24) < 21);
    default:
        return 1;
    }
}

/* Bresenham, in the shape GDI draws it: the last point belongs to the next
 * segment and is not put down here. `first` says whether the starting point
 * is drawn, which is how a polyline avoids drawing its joints twice. */
static void line_raw(HDC dc, const ween_gdiobj *pen, int x0, int y0, int x1,
                     int y1, int *step, int draw_last)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int x = x0, y = y0;

    if (adx >= ady) {
        int err = adx / 2;
        for (int i = 0; i <= adx; i++) {
            if ((i < adx || draw_last) && pen_on(pen, (*step)++))
                pen_dot(dc, pen, x, y);
            err -= ady;
            if (err < 0) {
                y += sy;
                err += adx;
            }
            x += sx;
        }
    } else {
        int err = ady / 2;
        for (int i = 0; i <= ady; i++) {
            if ((i < ady || draw_last) && pen_on(pen, (*step)++))
                pen_dot(dc, pen, x, y);
            err -= adx;
            if (err < 0) {
                x += sx;
                err += ady;
            }
            y += sy;
        }
    }
}

BOOL MoveToEx(HDC dc, int x, int y, POINT *prev)
{
    if (!dc)
        return FALSE;
    if (prev) {
        prev->x = dc->cur_x;
        prev->y = dc->cur_y;
    }
    dc->cur_x = x;
    dc->cur_y = y;
    return TRUE;
}

BOOL LineTo(HDC dc, int x, int y)
{
    const ween_gdiobj *pen;
    RECT saved;
    int step = 0;
    if (!dc)
        return FALSE;
    pen = dc_pen(dc);
    if (!pen->is_null) {
        clip_push(dc, &saved);
        line_raw(dc, pen, dc->cur_x, dc->cur_y, x, y, &step, 0);
        clip_pop(dc, &saved);
    }
    dc->cur_x = x;
    dc->cur_y = y;
    return TRUE;
}

BOOL Polyline(HDC dc, const POINT *pts, int count)
{
    const ween_gdiobj *pen;
    RECT saved;
    int step = 0;
    if (!dc || !pts || count < 2)
        return FALSE;
    pen = dc_pen(dc);
    if (pen->is_null)
        return TRUE;
    clip_push(dc, &saved);
    for (int i = 0; i + 1 < count; i++)
        line_raw(dc, pen, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, &step,
                 0);
    clip_pop(dc, &saved);
    return TRUE;
}

/* Cubic Béziers, in groups of three points after the first — the curve tool
 * of every drawing program, and the shape GDI takes them in. */
static void bezier(HDC dc, const ween_gdiobj *pen, POINT a, POINT b, POINT c,
                   POINT d, int *step)
{
    /* Enough steps that consecutive points are never more than a pixel or so
     * apart: the control net's length is a safe over-estimate of the arc. */
    long len = labs(b.x - a.x) + labs(b.y - a.y) + labs(c.x - b.x) +
               labs(c.y - b.y) + labs(d.x - c.x) + labs(d.y - c.y);
    int n = (int)(len / 2 + 4);
    int px = a.x, py = a.y;
    if (n > 4096)
        n = 4096;
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, u = 1.0 - t;
        double x = u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x +
                   t * t * t * d.x;
        double y = u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y +
                   t * t * t * d.y;
        int nx = (int)(x + 0.5), ny = (int)(y + 0.5);
        if (nx != px || ny != py) {
            line_raw(dc, pen, px, py, nx, ny, step, 1);
            px = nx;
            py = ny;
        }
    }
}

BOOL PolyBezier(HDC dc, const POINT *pts, DWORD count)
{
    const ween_gdiobj *pen;
    RECT saved;
    int step = 0;
    if (!dc || !pts || count < 4)
        return FALSE;
    pen = dc_pen(dc);
    if (pen->is_null)
        return TRUE;
    clip_push(dc, &saved);
    for (DWORD i = 0; i + 3 < count; i += 3)
        bezier(dc, pen, pts[i], pts[i + 1], pts[i + 2], pts[i + 3], &step);
    clip_pop(dc, &saved);
    dc->cur_x = pts[count - 1].x;
    dc->cur_y = pts[count - 1].y;
    return TRUE;
}

/* ---- filled shapes ------------------------------------------------------- */

/* A shape's interior, drawn with the DC's brush; NULL_BRUSH leaves it. */
static void fill_span(HDC dc, const ween_gdiobj *brush, int x0, int x1, int y)
{
    if (brush->is_null)
        return;
    for (int x = x0; x < x1; x++)
        put(dc, x, y, brush->color);
}

BOOL Rectangle(HDC dc, int left, int top, int right, int bottom)
{
    const ween_gdiobj *pen, *brush;
    RECT saved;
    int step = 0, w;
    if (!dc)
        return FALSE;
    pen = dc_pen(dc);
    brush = dc_brush(dc);
    /* GDI's rectangle stops one short of its right and bottom. */
    right--;
    bottom--;
    if (right < left || bottom < top)
        return TRUE;
    w = pen->is_null ? 0 : pen->pen_width;
    clip_push(dc, &saved);
    for (int y = top + w; y <= bottom - w; y++)
        fill_span(dc, brush, left + w, right - w + 1, y);
    if (!pen->is_null) {
        line_raw(dc, pen, left, top, right, top, &step, 1);
        line_raw(dc, pen, right, top, right, bottom, &step, 1);
        line_raw(dc, pen, right, bottom, left, bottom, &step, 1);
        line_raw(dc, pen, left, bottom, left, top, &step, 1);
    }
    clip_pop(dc, &saved);
    return TRUE;
}

/* An ellipse inscribed in a rectangle, and the rounded rectangle that is the
 * same arithmetic applied to four corners. Both are drawn a scan line at a
 * time: for a row `dy` from the centre, the widest point on the ellipse is
 * the largest x with x*x*b*b + dy*dy*a*a <= a*a*b*b, which is an integer
 * question and needs no square root. The interior of the row is then one
 * span, and the outline is the run between this row's edge and the last
 * one's — which is what keeps the flat top and bottom of the curve joined
 * up instead of dotted.
 */
static int ell_xmax(long dy, long a, long b)
{
    long x, a2 = a * a, b2 = b * b, lim = a2 * b2 - dy * dy * a2;
    if (lim < 0)
        return -1;
    for (x = a; x > 0; x--)
        if (x * x * b2 <= lim)
            break;
    return (int)x;
}

/* The shared body: `rows` holds the left and right edge of every scan line,
 * and this fills and outlines them. */
static void spans_draw(HDC dc, const ween_gdiobj *pen, const ween_gdiobj *brush,
                       const int *x0s, const int *x1s, int top, int rows)
{
    int w = pen->is_null ? 0 : pen->pen_width;
    for (int i = 0; i < rows; i++) {
        int y = top + i, x0 = x0s[i], x1 = x1s[i];
        if (x1 < x0)
            continue;
        if (!brush->is_null) {
            int f0 = x0 + w, f1 = x1 - w;
            /* a row that the outline covers entirely has no interior */
            if (i >= w && i < rows - w)
                for (int x = f0; x <= f1; x++)
                    put(dc, x, y, brush->color);
        }
        if (pen->is_null)
            continue;
        {
            /* how far this row's edges moved from the last: the outline has
             * to bridge the gap, or a shallow arc comes out as dots */
            int p0 = i > 0 ? x0s[i - 1] : x0, p1 = i > 0 ? x1s[i - 1] : x1;
            int l0 = x0, l1 = (i > 0 && p0 - 1 > x0) ? p0 - 1 : x0;
            int r0 = (i > 0 && p1 + 1 < x1) ? p1 + 1 : x1, r1 = x1;
            if (i == 0 || i == rows - 1) {
                l1 = x1;
                r0 = x0;
            }
            for (int x = l0; x <= l1 && x <= x1; x++)
                pen_dot(dc, pen, x, y);
            for (int x = r0 < x0 ? x0 : r0; x <= r1; x++)
                pen_dot(dc, pen, x, y);
        }
    }
}

BOOL Ellipse(HDC dc, int left, int top, int right, int bottom)
{
    const ween_gdiobj *pen, *brush;
    RECT saved;
    int rows, *x0s, *x1s;
    long a, b;

    if (!dc)
        return FALSE;
    /* GDI's ellipse stops one short of the right and bottom edge. */
    right--;
    bottom--;
    if (right < left || bottom < top)
        return TRUE;
    pen = dc_pen(dc);
    brush = dc_brush(dc);
    rows = bottom - top + 1;
    a = (right - left);
    b = (bottom - top);
    x0s = (int *)malloc(sizeof(int) * (size_t)rows * 2);
    if (!x0s)
        return FALSE;
    x1s = x0s + rows;
    for (int i = 0; i < rows; i++) {
        /* doubled coordinates, so an even-sized box has no centre pixel and
         * an odd-sized one has exactly one */
        long dy2 = 2 * i - b;
        int x = ell_xmax(dy2 < 0 ? -dy2 : dy2, a, b);
        if (x < 0) {
            x0s[i] = 1;
            x1s[i] = 0;
        } else {
            x0s[i] = left + (int)((a - x) / 2);
            x1s[i] = right - (int)((a - x) / 2);
        }
    }
    clip_push(dc, &saved);
    spans_draw(dc, pen, brush, x0s, x1s, top, rows);
    clip_pop(dc, &saved);
    free(x0s);
    return TRUE;
}

BOOL RoundRect(HDC dc, int left, int top, int right, int bottom, int ew, int eh)
{
    const ween_gdiobj *pen, *brush;
    RECT saved;
    int rows, *x0s, *x1s;
    long a, b;

    if (!dc)
        return FALSE;
    right--;
    bottom--;
    if (right < left || bottom < top)
        return TRUE;
    pen = dc_pen(dc);
    brush = dc_brush(dc);
    a = ew / 2;
    b = eh / 2;
    if (a > (right - left) / 2)
        a = (right - left) / 2;
    if (b > (bottom - top) / 2)
        b = (bottom - top) / 2;
    rows = bottom - top + 1;
    x0s = (int *)malloc(sizeof(int) * (size_t)rows * 2);
    if (!x0s)
        return FALSE;
    x1s = x0s + rows;
    for (int i = 0; i < rows; i++) {
        int y = top + i, inset = 0;
        long dy = 0;
        if (y < top + b)
            dy = top + b - y;
        else if (y > bottom - b)
            dy = y - (bottom - b);
        if (dy && b) {
            int x = ell_xmax(dy, a, b);
            inset = x < 0 ? (int)a : (int)a - x;
        }
        x0s[i] = left + inset;
        x1s[i] = right - inset;
    }
    clip_push(dc, &saved);
    spans_draw(dc, pen, brush, x0s, x1s, top, rows);
    clip_pop(dc, &saved);
    free(x0s);
    return TRUE;
}

/* A polygon, filled by the even-odd rule and outlined with the pen — which
 * is what GDI's default ALTERNATE fill mode does. */
BOOL Polygon(HDC dc, const POINT *pts, int count)
{
    const ween_gdiobj *pen, *brush;
    RECT saved;
    int step = 0, ymin, ymax;
    int *xs;
    if (!dc || !pts || count < 2)
        return FALSE;
    pen = dc_pen(dc);
    brush = dc_brush(dc);
    ymin = ymax = pts[0].y;
    for (int i = 1; i < count; i++) {
        if (pts[i].y < ymin)
            ymin = pts[i].y;
        if (pts[i].y > ymax)
            ymax = pts[i].y;
    }
    clip_push(dc, &saved);
    xs = (int *)malloc(sizeof(int) * (size_t)(count + 1));
    if (xs && !brush->is_null) {
        for (int y = ymin; y <= ymax; y++) {
            int n = 0;
            for (int i = 0; i < count; i++) {
                POINT a = pts[i], b = pts[(i + 1) % count];
                if (a.y == b.y)
                    continue;
                if ((y >= a.y && y < b.y) || (y >= b.y && y < a.y)) {
                    long dx = b.x - a.x, dy = b.y - a.y;
                    xs[n++] = (int)(a.x + (dx * (y - a.y) + (dy > 0 ? dy / 2
                                                                   : -dy / 2)) /
                                               dy);
                }
            }
            for (int i = 1; i < n; i++) { /* insertion sort: n is tiny */
                int v = xs[i], j = i - 1;
                while (j >= 0 && xs[j] > v) {
                    xs[j + 1] = xs[j];
                    j--;
                }
                xs[j + 1] = v;
            }
            for (int i = 0; i + 1 < n; i += 2)
                for (int x = xs[i]; x <= xs[i + 1]; x++)
                    put(dc, x, y, brush->color);
        }
    }
    free(xs);
    if (!pen->is_null) {
        for (int i = 0; i < count; i++)
            line_raw(dc, pen, pts[i].x, pts[i].y, pts[(i + 1) % count].x,
                     pts[(i + 1) % count].y, &step, 1);
    }
    clip_pop(dc, &saved);
    return TRUE;
}

/* ---- pixels and fills ---------------------------------------------------- */

COLORREF SetPixel(HDC dc, int x, int y, COLORREF color)
{
    RECT saved;
    if (!dc)
        return 0;
    clip_push(dc, &saved);
    put(dc, x, y, cr_px(color));
    clip_pop(dc, &saved);
    return color;
}

COLORREF GetPixel(HDC dc, int x, int y)
{
    if (!dc || x < 0 || y < 0 || x >= dc->clip_w || y >= dc->clip_h)
        return 0xFFFFFFFFu; /* CLR_INVALID */
    return px_cr(peek(dc, x, y));
}

/* Scan-line flood fill: each span found pushes the rows above and below it
 * onto a stack, which is what keeps it out of the C stack for a big area. */
BOOL ExtFloodFill(HDC dc, int x, int y, COLORREF color, UINT type)
{
    ween_color target, fill;
    ween_surface *s;
    RECT saved;
    struct { int x, y; } *stack;
    int top = 0, cap = 4096;
    int w, h;

    if (!dc)
        return FALSE;
    w = dc->clip_w;
    h = dc->clip_h;
    if (x < 0 || y < 0 || x >= w || y >= h)
        return FALSE;
    s = dc->s;
    target = peek(dc, x, y);
    fill = dc_brush(dc)->color;
    if (type == FLOODFILLSURFACE && target == fill)
        return TRUE;
    if (type == FLOODFILLBORDER && target == cr_px(color))
        return TRUE;

    stack = malloc(sizeof(*stack) * (size_t)cap);
    if (!stack)
        return FALSE;
    clip_push(dc, &saved);
    stack[top].x = x;
    stack[top].y = y;
    top++;

#define MATCH(px)                                                              \
    (type == FLOODFILLBORDER ? ((px) != cr_px(color) && (px) != fill)          \
                             : (px) == target)

    while (top > 0) {
        int px, py, x0, x1;
        top--;
        px = stack[top].x;
        py = stack[top].y;
        if (py < 0 || py >= h)
            continue;
        if (!MATCH(peek(dc, px, py)))
            continue;
        x0 = px;
        while (x0 > 0 && MATCH(peek(dc, x0 - 1, py)))
            x0--;
        x1 = px;
        while (x1 + 1 < w && MATCH(peek(dc, x1 + 1, py)))
            x1++;
        for (int i = x0; i <= x1; i++)
            put(dc, i, py, fill);
        for (int dy = -1; dy <= 1; dy += 2) {
            int ny = py + dy, run = 0;
            if (ny < 0 || ny >= h)
                continue;
            for (int i = x0; i <= x1; i++) {
                if (MATCH(peek(dc, i, ny))) {
                    if (!run) {
                        if (top == cap) {
                            void *bigger =
                                realloc(stack, sizeof(*stack) * (size_t)cap * 2);
                            if (!bigger)
                                goto done;
                            stack = bigger;
                            cap *= 2;
                        }
                        stack[top].x = i;
                        stack[top].y = ny;
                        top++;
                        run = 1;
                    }
                } else {
                    run = 0;
                }
            }
        }
    }
done:
#undef MATCH
    clip_pop(dc, &saved);
    free(stack);
    (void)s;
    return TRUE;
}

/* ---- blits --------------------------------------------------------------- */

/* Read one source pixel, with the source DC's own origin and bounds; outside
 * it the source reads as black, as a blit off the edge of a bitmap does. */
static ween_color src_px(HDC src, int x, int y)
{
    ween_surface *s;
    int sx, sy;
    if (!src)
        return 0;
    s = src->s;
    sx = src->org_x + x;
    sy = src->org_y + y;
    if (x < 0 || y < 0 || x >= src->clip_w || y >= src->clip_h)
        return 0;
    if (sx < 0 || sy < 0 || sx >= s->w || sy >= s->h)
        return 0;
    return s->px[(long)sy * s->w + sx] & 0xffffffu;
}

static BOOL blt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy,
                int sw, int sh, DWORD rop)
{
    unsigned char code = (unsigned char)(rop >> 16);
    ween_color pat;
    RECT saved;
    int flip_x = 0, flip_y = 0;

    if (!dst)
        return FALSE;
    if (rop_uses_source(code) && !src)
        return FALSE;
    if (w < 0) {
        w = -w;
        x -= w;
        flip_x = !flip_x;
    }
    if (h < 0) {
        h = -h;
        y -= h;
        flip_y = !flip_y;
    }
    if (sw < 0) {
        sw = -sw;
        sx -= sw;
        flip_x = !flip_x;
    }
    if (sh < 0) {
        sh = -sh;
        sy -= sh;
        flip_y = !flip_y;
    }
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0)
        return TRUE;

    pat = dc_brush(dst)->color;
    clip_push(dst, &saved);
    for (int j = 0; j < h; j++) {
        int syy = sh == h ? sy + (flip_y ? h - 1 - j : j)
                          : sy + (int)(((long)(flip_y ? h - 1 - j : j) * sh) / h);
        for (int i = 0; i < w; i++) {
            int sxx = sw == w
                          ? sx + (flip_x ? w - 1 - i : i)
                          : sx + (int)(((long)(flip_x ? w - 1 - i : i) * sw) / w);
            ween_color s = src ? src_px(src, sxx, syy) : 0;
            ween_color d = peek(dst, x + i, y + j);
            ween_color r = rop3_apply(code, pat, s, d);
            ween_surface_pixel(dst->s, dst->org_x + x + i, dst->org_y + y + j, r);
        }
    }
    clip_pop(dst, &saved);
    return TRUE;
}

BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy,
            DWORD rop)
{
    return blt(dst, x, y, w, h, src, sx, sy, w, h, rop);
}

BOOL StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy,
                int sw, int sh, DWORD rop)
{
    return blt(dst, x, y, w, h, src, sx, sy, sw, sh, rop);
}

BOOL PatBlt(HDC dc, int x, int y, int w, int h, DWORD rop)
{
    return blt(dc, x, y, w, h, NULL, 0, 0, w, h, rop);
}

BOOL InvertRect(HDC dc, const RECT *rect)
{
    if (!dc || !rect)
        return FALSE;
    return blt(dc, rect->left, rect->top, rect->right - rect->left,
               rect->bottom - rect->top, NULL, 0, 0, 1, 1, DSTINVERT);
}

BOOL DrawFocusRect(HDC dc, const RECT *rect)
{
    RECT saved;
    if (!dc || !rect)
        return FALSE;
    clip_push(dc, &saved);
    ween_surface_focus_rect(dc->s, dc->org_x + rect->left, dc->org_y + rect->top,
                            rect->right - rect->left, rect->bottom - rect->top);
    clip_pop(dc, &saved);
    return TRUE;
}

/* ---- device-independent bitmaps ------------------------------------------ */

/* The pixels, the way a .bmp file holds them: bottom-up unless the height is
 * negative, rows padded to four bytes, blue first. */
int GetDIBits(HDC dc, HBITMAP bmp, UINT start, UINT lines, LPVOID bits,
              LPBITMAPINFO info, UINT usage)
{
    ween_surface *s;
    int bpp, stride, top_down;
    (void)dc;
    (void)usage;
    if (!bmp || !info || bmp->kind != WEEN_OBJ_BITMAP)
        return 0;
    s = &bmp->bitmap;
    if (!bits) { /* the query form: fill in the header and say how big */
        info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info->bmiHeader.biWidth = s->w;
        info->bmiHeader.biHeight = s->h;
        info->bmiHeader.biPlanes = 1;
        if (!info->bmiHeader.biBitCount)
            info->bmiHeader.biBitCount = 24;
        info->bmiHeader.biCompression = BI_RGB;
        bpp = info->bmiHeader.biBitCount;
        stride = ((s->w * bpp + 31) / 32) * 4;
        info->bmiHeader.biSizeImage = (DWORD)stride * (DWORD)s->h;
        return s->h;
    }
    bpp = info->bmiHeader.biBitCount ? info->bmiHeader.biBitCount : 24;
    if (bpp != 24 && bpp != 32)
        return 0;
    top_down = info->bmiHeader.biHeight < 0;
    stride = ((s->w * bpp + 31) / 32) * 4;
    if (lines > (UINT)s->h - start)
        lines = (UINT)s->h - start;
    for (UINT i = 0; i < lines; i++) {
        /* row `start + i` counted from the bottom of the picture */
        int sy = top_down ? (int)(start + i) : s->h - 1 - (int)(start + i);
        unsigned char *out = (unsigned char *)bits + (size_t)i * (size_t)stride;
        const ween_color *row = s->px + (long)sy * s->w;
        for (int x = 0; x < s->w; x++) {
            out[0] = (unsigned char)(row[x] & 0xff);
            out[1] = (unsigned char)((row[x] >> 8) & 0xff);
            out[2] = (unsigned char)((row[x] >> 16) & 0xff);
            if (bpp == 32)
                out[3] = 0;
            out += bpp / 8;
        }
    }
    return (int)lines;
}

int SetDIBits(HDC dc, HBITMAP bmp, UINT start, UINT lines, const void *bits,
              const BITMAPINFO *info, UINT usage)
{
    ween_surface *s;
    int bpp, stride, top_down, w;
    (void)dc;
    (void)usage;
    if (!bmp || !info || !bits || bmp->kind != WEEN_OBJ_BITMAP)
        return 0;
    s = &bmp->bitmap;
    bpp = info->bmiHeader.biBitCount;
    if (bpp != 24 && bpp != 32)
        return 0;
    w = info->bmiHeader.biWidth;
    if (w > s->w)
        w = s->w;
    top_down = info->bmiHeader.biHeight < 0;
    stride = ((info->bmiHeader.biWidth * bpp + 31) / 32) * 4;
    if (lines > (UINT)s->h - start)
        lines = (UINT)s->h - start;
    for (UINT i = 0; i < lines; i++) {
        int dy = top_down ? (int)(start + i) : s->h - 1 - (int)(start + i);
        const unsigned char *in =
            (const unsigned char *)bits + (size_t)i * (size_t)stride;
        ween_color *row = s->px + (long)dy * s->w;
        for (int x = 0; x < w; x++) {
            row[x] = WEEN_RGBX(in[2], in[1], in[0]);
            in += bpp / 8;
        }
    }
    return (int)lines;
}
