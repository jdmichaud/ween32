/* Classic Windows chrome on the software surface (port of classic.zig).
 * The 3D edge and the caption gradient reproduce the authentic Win2000 look;
 * the edge algorithm and its colour tables are Wine's DrawEdge
 * (dlls/user32/uitools.c, UITOOLS95_DrawRectEdge), which is what makes the
 * result pixel-identical to the real thing.
 *
 * The two edges that look alike but are not: a window frame is the plain
 * EDGE_RAISED (its outer line is COLOR_3DLIGHT, which is face-coloured and so
 * invisible against the desktop), while a button is EDGE_RAISED | BF_SOFT
 * (outer line COLOR_BTNHIGHLIGHT, white). Getting these the wrong way round
 * puts the white line one pixel out. */

#include "ween_internal.h"

/* Colour slots used by the tables; -1 means "draw nothing". */
#define X (-1)
#define L 0 /* COLOR_3DLIGHT      */
#define H 1 /* COLOR_BTNHIGHLIGHT */
#define S 2 /* COLOR_BTNSHADOW    */
#define D 3 /* COLOR_3DDKSHADOW   */
#define F 4 /* COLOR_BTNFACE      */
#define W 5 /* COLOR_WINDOWFRAME  */
#define N 6 /* COLOR_WINDOW       */

static const ween_color edge_palette[] = {
    WEEN_3DLIGHT, WEEN_WHITE, WEEN_SHADOW, WEEN_DKSHADOW,
    WEEN_FACE,    WEEN_BLACK, WEEN_WINDOWBG,
};

/* Indexed by uType & (BDR_INNER | BDR_OUTER). */
static const signed char LTInnerNormal[] = {
    X, X, X, X, X, H, H, X, X, D, D, X, X, X, X, X,
};
static const signed char LTOuterNormal[] = {
    X, L, S, X, H, L, S, X, D, L, S, X, X, L, S, X,
};
static const signed char RBInnerNormal[] = {
    X, X, X, X, X, S, S, X, X, L, L, X, X, X, X, X,
};
static const signed char RBOuterNormal[] = {
    X, D, H, X, S, D, H, X, L, D, H, X, X, D, H, X,
};
static const signed char LTInnerSoft[] = {
    X, X, X, X, X, L, L, X, X, S, S, X, X, X, X, X,
};
static const signed char LTOuterSoft[] = {
    X, H, D, X, L, H, D, X, S, H, D, X, X, H, D, X,
};
/* Wine: the right/bottom soft tables are the normal ones. */
#define RBInnerSoft RBInnerNormal
#define RBOuterSoft RBOuterNormal

static const signed char LTRBInnerMono[] = {
    X, X, X, X, X, W, W, W, X, W, W, W, X, W, W, W,
};
static const signed char LTRBOuterMono[] = {
    X, W, W, W, N, W, W, W, N, W, W, W, N, W, W, W,
};
static const signed char LTRBInnerFlat[] = {
    X, X, X, X, X, S, S, S, X, S, S, S, X, S, S, S,
};
static const signed char LTRBOuterFlat[] = {
    X, S, S, S, F, S, S, S, F, S, S, S, F, S, S, S,
};

static void fill(ween_surface *s, int l, int t, int r, int b, signed char c)
{
    if (c == X || r <= l || b <= t)
        return;
    ween_surface_fill(s, l, t, r - l, b - t, edge_palette[(int)c]);
}

/* Wine's UITOOLS95_DrawRectEdge with width 1. `type` is the BDR_ or EDGE_
 * combination, `flags` the BF_ set; the interior rect lands in *inner when
 * BF_ADJUST is given and inner is non-NULL. */
int ween_classic_edge(ween_surface *s, int x, int y, int w, int h,
                      unsigned type, unsigned flags, RECT *inner)
{
    const int width = 1;
    int idx = (int)(type & (BDR_INNER | BDR_OUTER));
    signed char lti, lto, rbi, rbo;
    int left = x, top = y, right = x + w, bottom = y + h;
    int lbi_off = 0, lti_off = 0, rti_off = 0, rbi_off = 0;
    int retval = !(((type & BDR_INNER) == BDR_INNER ||
                    (type & BDR_OUTER) == BDR_OUTER) &&
                   !(flags & (BF_FLAT | BF_MONO)));

    if (flags & BF_MONO) {
        lti = rbi = LTRBInnerMono[idx];
        lto = rbo = LTRBOuterMono[idx];
    } else if (flags & BF_FLAT) {
        lti = rbi = LTRBInnerFlat[idx];
        lto = rbo = LTRBOuterFlat[idx];
        if (lti != X)
            lti = rbi = F;
    } else if (flags & BF_SOFT) {
        lti = LTInnerSoft[idx];
        lto = LTOuterSoft[idx];
        rbi = RBInnerSoft[idx];
        rbo = RBOuterSoft[idx];
    } else {
        lti = LTInnerNormal[idx];
        lto = LTOuterNormal[idx];
        rbi = RBInnerNormal[idx];
        rbo = RBOuterNormal[idx];
    }

    if ((flags & BF_BOTTOMLEFT) == BF_BOTTOMLEFT)
        lbi_off = width;
    if ((flags & BF_TOPRIGHT) == BF_TOPRIGHT)
        rti_off = width;
    if ((flags & BF_BOTTOMRIGHT) == BF_BOTTOMRIGHT)
        rbi_off = width;
    if ((flags & BF_TOPLEFT) == BF_TOPLEFT)
        lti_off = width;

    /* outer edge */
    if (flags & BF_TOP)
        fill(s, left, top, right, top + width, lto);
    if (flags & BF_LEFT)
        fill(s, left, top, left + width, bottom, lto);
    if (flags & BF_BOTTOM)
        fill(s, left, bottom - width, right, bottom, rbo);
    if (flags & BF_RIGHT)
        fill(s, right - width, top, right, bottom, rbo);

    /* inner edge */
    if (flags & BF_TOP)
        fill(s, left + lti_off, top + width, right - rti_off, top + 2 * width, lti);
    if (flags & BF_LEFT)
        fill(s, left + width, top + lti_off, left + 2 * width, bottom - lbi_off, lti);
    if (flags & BF_BOTTOM)
        fill(s, left + lbi_off, bottom - 2 * width, right - rbi_off, bottom - width,
             rbi);
    if (flags & BF_RIGHT)
        fill(s, right - 2 * width, top + rti_off, right - width, bottom - rbi_off,
             rbi);

    if (((flags & BF_MIDDLE) && retval) || (flags & BF_ADJUST)) {
        int add = (LTRBInnerMono[idx] != X ? width : 0) +
                  (LTRBOuterMono[idx] != X ? width : 0);
        if (flags & BF_LEFT)
            left += add;
        if (flags & BF_RIGHT)
            right -= add;
        if (flags & BF_TOP)
            top += add;
        if (flags & BF_BOTTOM)
            bottom -= add;
        if ((flags & BF_MIDDLE) && retval)
            fill(s, left, top, right, bottom, (flags & BF_MONO) ? N : F);
        if ((flags & BF_ADJUST) && inner) {
            inner->left = left;
            inner->top = top;
            inner->right = right;
            inner->bottom = bottom;
        }
    }
    return retval;
}

/* The 2-pixel button edge: raised or sunken with BF_SOFT, as
 * DrawFrameControl draws a push button. */
void ween_classic_bevel(ween_surface *s, int x, int y, int w, int h, int sunken)
{
    ween_classic_edge(s, x, y, w, h, sunken ? EDGE_SUNKEN : EDGE_RAISED,
                      BF_RECT | BF_SOFT, NULL);
}

/* The classic caption fill: solid behind the icon, the navy -> light-blue
 * ramp behind the title, solid behind the buttons. Wine feeds GradientFill a
 * four-vertex mesh with exactly those three regions, so a caption is not one
 * ramp across its whole width — miss that and every pixel of it is a shade
 * out. `icon_w` is the strip on the left held at the start colour (0 when the
 * window has no system menu); `buttons_w` the strip on the right held at the
 * end colour. */
void ween_classic_caption(ween_surface *s, int x, int y, int w, int h,
                          int icon_w, int buttons_w)
{
    int ar = (WEEN_CAP_LEFT >> 16) & 0xff, ag = (WEEN_CAP_LEFT >> 8) & 0xff;
    int ab = WEEN_CAP_LEFT & 0xff;
    int br = (WEEN_CAP_RIGHT >> 16) & 0xff, bg = (WEEN_CAP_RIGHT >> 8) & 0xff;
    int bb = WEEN_CAP_RIGHT & 0xff;
    int x1 = x + (icon_w < w ? icon_w : w);
    int x2 = x + w - buttons_w;
    int span;
    if (w <= 0)
        return;
    if (x2 < x1)
        x2 = x1;
    span = x2 - x1;
    for (int i = x; i < x + w; i++) {
        ween_color c;
        if (i < x1 || span <= 0)
            c = WEEN_CAP_LEFT;
        else if (i >= x2)
            c = WEEN_CAP_RIGHT;
        else {
            int d = i - x1;
            unsigned r = (unsigned)(ar + (br - ar) * d / span);
            unsigned g = (unsigned)(ag + (bg - ag) * d / span);
            unsigned b = (unsigned)(ab + (bb - ab) * d / span);
            c = (r << 16) | (g << 8) | b;
        }
        ween_surface_vline(s, i, y, h, c);
    }
}

/* ---- DrawFrameControl's button glyphs (Wine's UITOOLS95_DFC_Button*) ----- */

/* Wine's UITOOLS_MakeSquareRect: the largest centred square of a rect. */
static int make_square(int *x, int *y, int w, int h)
{
    int d = w > h ? h : w;
    if (w < h)
        *y += (h - d) / 2;
    else if (w > h)
        *x += (w - d) / 2;
    return d;
}

/* Even-odd scanline fill of an integer polygon, testing pixel centres — GDI's
 * ALTERNATE mode, which is what Polygon() uses by default. */
static void fill_polygon(ween_surface *s, const POINT *pt, int n, ween_color c)
{
    int ymin = pt[0].y, ymax = pt[0].y, i, j;
    for (i = 1; i < n; i++) {
        if (pt[i].y < ymin)
            ymin = pt[i].y;
        if (pt[i].y > ymax)
            ymax = pt[i].y;
    }
    for (int y = ymin; y < ymax; y++) {
        double cy = y + 0.5, xs[16];
        int nx = 0;
        for (i = 0, j = n - 1; i < n; j = i++) {
            double y0 = pt[j].y, y1 = pt[i].y;
            if ((y0 <= cy) == (y1 <= cy))
                continue;
            if (nx < 16)
                xs[nx++] = pt[j].x + (cy - y0) * (pt[i].x - pt[j].x) / (y1 - y0);
        }
        for (i = 1; i < nx; i++) { /* insertion sort: at most a few crossings */
            double v = xs[i];
            for (j = i - 1; j >= 0 && xs[j] > v; j--)
                xs[j + 1] = xs[j];
            xs[j + 1] = v;
        }
        for (i = 0; i + 1 < nx; i += 2)
            for (int x = (int)(xs[i] + 0.5); x < (int)(xs[i + 1] + 0.5); x++)
                ween_surface_pixel(s, x, y, c);
    }
}

/* A filled ellipse inscribed in the w x h box at (x,y). `half` picks one of
 * the two halves Wine's DFC_ButtonRadio draws with Pie(): -1 the top-left, +1
 * the bottom-right, 0 the whole disc. The split is not the diagonal — the
 * radials run to (left-1, bottom) and (right+1, top), which is what puts the
 * odd pixel of the rim where win32 puts it.
 *
 * The membership test is a shade tighter than "centre within the radius":
 * GDI's integer rasteriser drops the pixels that only just qualify at the 45
 * degree points, and so must we. */
static void fill_ellipse(ween_surface *s, int x, int y, int w, int h, int half,
                         ween_color c)
{
    double rx = w / 2.0, ry = h / 2.0, cx = x + rx, cy = y + ry;
    double d1x = (x - 1) - cx, d1y = (y + h - 1) - cy;
    double d2x = (x + w) - cx, d2y = y - cy;
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            double ux = (px + 0.5 - cx) / rx, uy = (py + 0.5 - cy) / ry;
            double vx = px + 0.5 - cx, vy = py + 0.5 - cy;
            double c1, c2;
            int br;
            if (ux * ux + uy * uy > 0.95)
                continue;
            c1 = d1x * vy - d1y * vx;
            c2 = d2x * vy - d2y * vx;
            br = c1 < 0 || c2 > 0;
            if ((half < 0 && br) || (half > 0 && !br))
                continue;
            ween_surface_pixel(s, px, py, c);
        }
    }
}

/* DFCS_BUTTONCHECK / DFCS_BUTTON3STATE: a sunken field box with a tick. */
/* The bare tick a menu puts beside a checked item — Wine's six-point mark,
 * with no box around it. A menu is not a check box; drawing the frame too was
 * the difference between "checked" and a control sitting in the gutter. */
/* The arrow a column header wears when the view is sorted by it: eight by
 * seven, an engraved triangle — shadow down its left, white down its right
 * and along the edge it points away from. Pointing down is the same shape
 * with its rows the other way up, which is what the machine draws. */
void ween_classic_sort_arrow(ween_surface *s, int x, int y, int up)
{
    static const unsigned char dark[7] = { 0x08, 0x0c, 0x04, 0x06,
                                           0x02, 0x03, 0x00 };
    static const unsigned char lit[7] = { 0x10, 0x30, 0x20, 0x60,
                                          0x40, 0xc0, 0xff };
    for (int r = 0; r < 7; r++) {
        int row = up ? r : 6 - r;
        for (int i = 0; i < 8; i++) {
            if (dark[row] & (1u << i))
                ween_surface_pixel(s, x + i, y + r, WEEN_SHADOW);
            if (lit[row] & (1u << i))
                ween_surface_pixel(s, x + i, y + r, WEEN_WHITE);
        }
    }
}

/* The tick a menu puts beside an item that is on: seven by seven, taken a
 * pixel at a time off a Windows 2000 column menu. A check box's mark is a
 * different glyph and a different size, which is why this one is its own. */
void ween_classic_menu_check(ween_surface *s, int x, int y, ween_color c)
{
    static const unsigned char rows[7] = { 0x40, 0x60, 0x71, 0x3b, 0x1f, 0x0e,
                                           0x04 };
    for (int r = 0; r < 7; r++)
        for (int i = 0; i < 7; i++)
            if (rows[r] & (1u << i))
                ween_surface_pixel(s, x + i, y + r, c);
}

/* The dot a menu puts beside the one of a set it is on: six by six with its
 * corners off, which is what a Windows 2000 View menu has beside Details. */
void ween_classic_menu_bullet(ween_surface *s, int x, int y, ween_color c)
{
    ween_surface_hline(s, x + 1, y, 4, c);
    for (int i = 1; i < 5; i++)
        ween_surface_hline(s, x, y + i, 6, c);
    ween_surface_hline(s, x + 1, y + 5, 4, c);
}

void ween_classic_checkmark(ween_surface *s, int x, int y, int w, int h,
                            ween_color c)
{
    RECT in;
    int d = make_square(&x, &y, w, h);
    int t3;
    POINT pt[6];
    in.left = x + 1;
    in.top = y + 1;
    in.right = x + d - 1;
    in.bottom = y + d - 1;
    t3 = (in.bottom - in.top) / 3;
    pt[0].x = in.right - 1;
    pt[0].y = in.top;
    pt[1].x = pt[0].x;
    pt[1].y = pt[0].y + t3;
    pt[2].x = in.left + (in.right - in.left) / 3;
    pt[2].y = in.bottom - 1;
    pt[3].x = in.left + 1;
    pt[3].y = pt[2].y - (pt[2].x - pt[3].x);
    pt[4].x = pt[3].x;
    pt[4].y = pt[3].y - t3;
    pt[5].x = pt[2].x;
    pt[5].y = pt[2].y - t3;
    fill_polygon(s, pt, 6, c);
}

void ween_classic_check(ween_surface *s, int x, int y, int w, int h, unsigned flags)
{
    RECT in;
    int d = make_square(&x, &y, w, h);
    unsigned bf = BF_RECT | BF_ADJUST;
    if (flags & DFCS_FLAT)
        bf |= BF_FLAT;
    else if (flags & DFCS_MONO)
        bf |= BF_MONO;
    ween_classic_edge(s, x, y, d, d, EDGE_SUNKEN, bf, &in);

    ween_surface_fill(s, in.left, in.top, in.right - in.left, in.bottom - in.top,
                      (flags & (DFCS_INACTIVE | DFCS_PUSHED)) ? WEEN_FACE
                                                             : WEEN_WINDOWBG);
    if (flags & DFCS_CHECKED) {
        /* Wine's six-point tick, in the adjusted interior. */
        int t3 = (in.bottom - in.top) / 3;
        POINT pt[6];
        pt[0].x = in.right - 1;
        pt[0].y = in.top;
        pt[1].x = pt[0].x;
        pt[1].y = pt[0].y + t3;
        pt[2].x = in.left + (in.right - in.left) / 3;
        pt[2].y = in.bottom - 1;
        pt[3].x = in.left + 1;
        pt[3].y = pt[2].y - (pt[2].x - pt[3].x);
        pt[4].x = pt[3].x;
        pt[4].y = pt[3].y - t3;
        pt[5].x = pt[2].x;
        pt[5].y = pt[2].y - t3;
        fill_polygon(s, pt, 6,
                     (flags & DFCS_INACTIVE) ? WEEN_SHADOW : WEEN_BLACK);
    }
}

/* DFCS_BUTTONRADIO: two half-discs for the rim, a white face, and the dot. */
void ween_classic_radio(ween_surface *s, int x, int y, int w, int h, unsigned flags)
{
    int d = make_square(&x, &y, w, h);
    int shrink = d / 16 < 1 ? 1 : d / 16;
    int xc = x + d - d / 2, yc = y + d - d / 2;
    int i = 14 * d / 16;
    int l = xc - i + i / 2, t = yc - i + i / 2, sz = i + 1;

    fill_ellipse(s, l, t, sz, sz, +1, WEEN_WHITE);
    fill_ellipse(s, l, t, sz, sz, -1, WEEN_SHADOW);
    fill_ellipse(s, l + shrink, t + shrink, sz - 2 * shrink, sz - 2 * shrink, +1,
                 WEEN_3DLIGHT);
    fill_ellipse(s, l + shrink, t + shrink, sz - 2 * shrink, sz - 2 * shrink, -1,
                 WEEN_DKSHADOW);

    i = 10 * d / 16;
    fill_ellipse(s, xc - i + i / 2, yc - i + i / 2, i, i, 0,
                 (flags & (DFCS_INACTIVE | DFCS_PUSHED)) ? WEEN_FACE
                                                         : WEEN_WINDOWBG);
    if (flags & DFCS_CHECKED) {
        i = 6 * d / 16;
        if (i < 1)
            i = 1;
        fill_ellipse(s, xc - i + i / 2, yc - i + i / 2, i, i, 0,
                     (flags & DFCS_INACTIVE) ? WEEN_SHADOW : WEEN_BLACK);
    }
}

/* ---- scroll bars (Wine's UITOOLS95_DrawFrameScroll) ---------------------- */

/* The 55AA dither the scroll-bar track is painted with: face and window
 * white on alternating pixels. */
void ween_classic_scroll_track(ween_surface *s, int x, int y, int w, int h)
{
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            ween_surface_pixel(s, px, py,
                               ((px + py) & 1) ? WEEN_FACE : WEEN_WHITE);
}

/* The same weave the other way up. A toolbar button that is on is dithered
 * like a scroll bar's track but on the opposite parity, so the two cannot
 * share one routine: which pixels are white is fixed by where they are on
 * the surface, not by where the patch starts. */
void ween_classic_check_dither(ween_surface *s, int x, int y, int w, int h)
{
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            ween_surface_pixel(s, px, py,
                               ((px + py) & 1) ? WEEN_WHITE : WEEN_FACE);
}

/* A polygon drawn the way GDI does with a pen and brush of one colour: the
 * interior plus the outline, which is a pixel wider than the fill alone. */
static void draw_polygon(ween_surface *s, const POINT *pt, int n, ween_color c)
{
    fill_polygon(s, pt, n, c);
    for (int i = 0; i < n; i++) {
        POINT a = pt[i], b = pt[(i + 1) % n];
        int dx = b.x - a.x, dy = b.y - a.y;
        int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                        ? (dx < 0 ? -dx : dx)
                        : (dy < 0 ? -dy : dy);
        if (!steps) {
            ween_surface_pixel(s, a.x, a.y, c);
            continue;
        }
        for (int k = 0; k <= steps; k++)
            ween_surface_pixel(s, a.x + dx * k / steps, a.y + dy * k / steps, c);
    }
}

/* One scroll-bar arrow button. `dir`: 0 up, 1 down, 2 left, 3 right. */
/* The triangle at the right of an item that opens a submenu. Windows draws a
 * small solid one pointing the way the cascade will open — no bevel, no
 * button, just the mark. */
/* The small triangle beside a drop-down toolbar button: four wide, pointing
 * down, which is the shape the classic shell used. */
/* A solid triangle pointing down: `w` wide at the top, narrowing by two each
 * row, so an odd width comes to a point. Widths differ by where it is drawn —
 * a toolbar's drop-down mark is five wide, which is what the Windows 2000
 * screenshot has beside "Back". Draw a seven there and its last column falls
 * under the button's own border. */
void ween_classic_arrow_down(ween_surface *s, int x, int y, int w, ween_color c)
{
    for (int i = 0; w - 2 * i > 0; i++)
        ween_surface_hline(s, x + i, y + i, w - 2 * i, c);
}

void ween_classic_menu_arrow(ween_surface *s, int x, int y, int h,
                             ween_color c)
{
    /* Four wide and seven tall, which is what a Windows 2000 shell menu
     * draws beside "Send To": the rows are 1,2,3,4,3,2,1 pixels. Drawn as its
     * own columns rather than through fill_polygon, which stops one short of
     * the far edge and left the point blunt. */
    int half = h / 4;
    int cy = y + h / 2;
    if (half < 2)
        half = 2;
    for (int i = 0; i <= half; i++)
        ween_surface_vline(s, x + i, cy - (half - i), 2 * (half - i) + 1, c);
}

void ween_classic_scroll_arrow(ween_surface *s, int x, int y, int w, int h,
                               int dir, int inactive, int pushed)
{
    int sx = x, sy = y;
    int d = make_square(&sx, &sy, w, h);
    int small = d - 2;
    int tri = 290 * small / 1000 - 1;
    int left = sx, top = sy, right = sx + d, bottom = sy + d;
    POINT ln[3];

    if (tri < 2)
        tri = 2;

    /* An enabled arrow button gets the plain raised edge; a disabled or
     * pushed one goes through the push-button path, which is soft. */
    if (!inactive && !pushed)
        ween_classic_edge(s, x, y, w, h, EDGE_RAISED, BF_RECT | BF_MIDDLE, NULL);
    else
        ween_classic_edge(s, x, y, w, h, pushed ? EDGE_SUNKEN : EDGE_RAISED,
                          BF_RECT | BF_SOFT | BF_MIDDLE, NULL);

    switch (dir) {
    case 1: /* down */
        ln[2].x = left + 470 * small / 1000 + 2;
        ln[2].y = top + 687 * small / 1000 + 1;
        ln[0].x = ln[2].x - tri;
        ln[1].x = ln[2].x + tri;
        ln[0].y = ln[1].y = ln[2].y - tri;
        break;
    case 2: /* left */
        ln[2].x = right - (687 * small / 1000 + 1);
        ln[2].y = top + 470 * small / 1000 + 2;
        ln[0].y = ln[2].y - tri;
        ln[1].y = ln[2].y + tri;
        ln[0].x = ln[1].x = ln[2].x + tri;
        break;
    case 3: /* right */
        ln[2].x = left + 687 * small / 1000 + 1;
        ln[2].y = top + 470 * small / 1000 + 2;
        ln[0].y = ln[2].y - tri;
        ln[1].y = ln[2].y + tri;
        ln[0].x = ln[1].x = ln[2].x - tri;
        break;
    default: /* up */
        ln[2].x = left + 470 * small / 1000 + 2;
        ln[2].y = bottom - (687 * small / 1000 + 1);
        ln[0].x = ln[2].x - tri;
        ln[1].x = ln[2].x + tri;
        ln[0].y = ln[1].y = ln[2].y + tri;
        break;
    }

    if (inactive) /* the white emboss goes first, at the unshifted position */
        draw_polygon(s, ln, 3, WEEN_WHITE);
    if (inactive || !pushed)
        for (int i = 0; i < 3; i++) {
            ln[i].x--;
            ln[i].y--;
        }
    draw_polygon(s, ln, 3, inactive ? WEEN_SHADOW : WEEN_BLACK);
}

/* The size grip in a status bar's corner: three diagonal bands, each a white
 * quad with a shadow one behind it (Wine's DFCS_SCROLLSIZEGRIP). */
/* The grip in a status bar's corner: three diagonals of one white pixel and
 * two of shadow, four apart, in the twelve pixels above and left of the
 * corner given. Nothing is filled behind them — the machine runs the last
 * part of the bar on underneath, and a filled box cut it short. */
void ween_classic_sizegrip(ween_surface *s, int x1, int y1)
{
    int x0 = x1 - 11, y0 = y1 - 11;
    /* The thirteen square it sits in is face: the machine clears that much
     * and no more, so the part's top edge runs on above it and only its
     * right and bottom edges are cut. */
    ween_surface_fill(s, x0, y0, 13, 13, WEEN_FACE);
    for (int i = 0; i < 3; i++) {
        int line = x1 + y1 - 11 + 4 * i;
        for (int y = y0; y <= y1; y++) {
            int x = line - y;
            if (x < x0 || x > x1)
                continue;
            ween_surface_pixel(s, x, y, WEEN_WHITE);
            if (x + 1 <= x1)
                ween_surface_pixel(s, x + 1, y, WEEN_SHADOW);
            if (x + 2 <= x1)
                ween_surface_pixel(s, x + 2, y, WEEN_SHADOW);
        }
    }
}
