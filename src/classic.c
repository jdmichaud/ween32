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

/* The classic navy -> light-blue horizontal caption gradient. Interpolated per
 * channel across the strip, as GradientFill does; quantising through a 0..255
 * ramp first would coarsen it visibly. */
void ween_classic_caption(ween_surface *s, int x, int y, int w, int h)
{
    int ar = (WEEN_CAP_LEFT >> 16) & 0xff, ag = (WEEN_CAP_LEFT >> 8) & 0xff;
    int ab = WEEN_CAP_LEFT & 0xff;
    int br = (WEEN_CAP_RIGHT >> 16) & 0xff, bg = (WEEN_CAP_RIGHT >> 8) & 0xff;
    int bb = WEEN_CAP_RIGHT & 0xff;
    if (w <= 0)
        return;
    for (int i = 0; i < w; i++) {
        int d = w == 1 ? 0 : i;
        int n = w == 1 ? 1 : w - 1;
        unsigned r = (unsigned)(ar + (br - ar) * d / n);
        unsigned g = (unsigned)(ag + (bg - ag) * d / n);
        unsigned b = (unsigned)(ab + (bb - ab) * d / n);
        ween_surface_vline(s, x + i, y, h, (r << 16) | (g << 8) | b);
    }
}
