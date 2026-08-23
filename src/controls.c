/* The built-in control classes beyond BUTTON and STATIC.
 *
 * Each one is a port of how the classic control draws itself — Wine's
 * comctl32 for the layout arithmetic, its uitools for the parts — because the
 * point is to land on the same pixels, not merely to look similar. */

#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

/* What a window wears outside its client area, in pixels: the field border
 * WS_EX_CLIENTEDGE draws (two), the status-field border WS_EX_STATICEDGE
 * (one), and WS_BORDER's single line on top of either.
 *
 * WS_BORDER counts on a control as much as on a window of its own — it is one
 * line of COLOR_WINDOWFRAME and one pixel of client area, wherever it is
 * asked for. A captioned window is the exception: WS_CAPTION *is*
 * WS_BORDER | WS_DLGFRAME, and that border is drawn as part of the frame. */
int ween_border_width(const struct ween_wnd *w)
{
    int n = 0;
    if (w->ex_style & WS_EX_CLIENTEDGE)
        n = 2;
    else if (w->ex_style & WS_EX_STATICEDGE)
        n = 1;
    if ((w->style & WS_BORDER) && !ween_has_caption(w))
        n += 1;
    return n;
}

/* Paint it. WS_EX_CLIENTEDGE is the field border (sunken outer and inner);
 * WS_EX_STATICEDGE is the status-field border (sunken outer only); WS_BORDER
 * is the flat line, drawn inside whichever of those came first. */
void ween_paint_border(struct ween_wnd *w)
{
    struct ween_wnd *top = ween_top_level(w);
    int ox, oy, all = ween_border_width(w), field;
    if (!all)
        return;
    ween_client_origin(w, &ox, &oy);
    ox -= all;
    oy -= all;
    if (w->ex_style & WS_EX_CLIENTEDGE)
        ween_classic_edge(&top->surface, ox, oy, w->w, w->h, EDGE_SUNKEN,
                          BF_RECT, NULL);
    else if (w->ex_style & WS_EX_STATICEDGE)
        ween_classic_edge(&top->surface, ox, oy, w->w, w->h, BDR_SUNKENOUTER,
                          BF_RECT, NULL);
    field = (w->ex_style & WS_EX_CLIENTEDGE) ? 2
            : (w->ex_style & WS_EX_STATICEDGE) ? 1
                                               : 0;
    if (all > field) { /* WS_BORDER: one line of COLOR_WINDOWFRAME */
        int x = ox + field, y = oy + field;
        int cw = w->w - 2 * field, ch = w->h - 2 * field;
        ween_surface_fill(&top->surface, x, y, cw, 1, WEEN_BLACK);
        ween_surface_fill(&top->surface, x, y + ch - 1, cw, 1, WEEN_BLACK);
        ween_surface_fill(&top->surface, x, y, 1, ch, WEEN_BLACK);
        ween_surface_fill(&top->surface, x + cw - 1, y, 1, ch, WEEN_BLACK);
    }
}

/* ---- scroll bars ---------------------------------------------------------
 *
 * A vertical bar is an up arrow, a dithered track with the thumb in it, and a
 * down arrow; horizontal is the same lying down. The arrows are disabled —
 * and the thumb absent — when there is nothing to scroll, which is how an
 * EDIT with WS_VSCROLL and one screenful of text comes out. */

int ween_scroll_metric(void)
{
    return ween_ncm(16); /* SM_CXVSCROLL / SM_CYHSCROLL at 96 dpi */
}

/* Whether a corner to drag the window by is standing in this window's own
 * bottom-right — inside it, or beside it and lying over the corner. Anything
 * that scrolls yields the foot of its bar to one: the machine draws 82 pixels
 * of bar in a 98 pixel box, with the sixteen below it the corner.
 *
 * The corner draws no background of its own, so what the bar leaves is what
 * shows behind it. */
int ween_corner_taken(HWND wnd)
{
    int mx, my, ox, oy;
    if (!wnd)
        return 0;
    for (struct ween_wnd *c = wnd->first_child; c; c = c->next_sibling)
        if (c->visible && (c->style & (SBS_SIZEGRIP | SBS_SIZEBOX)))
            return 1;
    if (!wnd->parent)
        return 0;
    ween_client_origin(wnd, &ox, &oy);
    mx = ox + wnd->w - 1;
    my = oy + wnd->h - 1;
    for (struct ween_wnd *c = wnd->parent->first_child; c; c = c->next_sibling) {
        int cx, cy;
        if (c == wnd || !c->visible || !(c->style & (SBS_SIZEGRIP | SBS_SIZEBOX)))
            continue;
        ween_client_origin(c, &cx, &cy);
        if (mx >= cx && mx < cx + c->w && my >= cy && my < cy + c->h)
            return 1;
    }
    return 0;
}

void ween_draw_scrollbar(ween_surface *s, int x, int y, int w, int h, int vert,
                         int enabled, int pos, int page, int min, int max)
{
    int sz = ween_scroll_metric();
    int len = vert ? h : w;
    int track = len - 2 * sz;
    int thumb = sz, tpos = 0;

    if (vert) {
        ween_classic_scroll_arrow(s, x, y, w, sz, 0, !enabled, 0);
        ween_classic_scroll_arrow(s, x, y + h - sz, w, sz, 1, !enabled, 0);
    } else {
        ween_classic_scroll_arrow(s, x, y, sz, h, 2, !enabled, 0);
        ween_classic_scroll_arrow(s, x + w - sz, y, sz, h, 3, !enabled, 0);
    }
    if (track <= 0)
        return;

    if (vert)
        ween_classic_scroll_track(s, x, y + sz, w, track);
    else
        ween_classic_scroll_track(s, x + sz, y, track, h);

    if (!enabled || max <= min)
        return;
    if (page > 0) {
        thumb = MulDiv(page, track, max - min + 1);
        if (thumb < sz)
            thumb = sz;
    }
    if (thumb >= track)
        return; /* no room for a thumb: the track stays bare, as in win32 */
    {   /* the thumb spans the positions that still show a full page, which is
         * nMax - nPage + 1 — the same range a click or a drag works in */
        int span = (page > 0 ? max - page + 1 : max) - min;
        if (span > 0)
            tpos = MulDiv(pos - min, track - thumb, span);
    }
    if (vert)
        ween_classic_edge(s, x, y + sz + tpos, w, thumb, EDGE_RAISED,
                          BF_RECT | BF_MIDDLE, NULL);
    else
        ween_classic_edge(s, x + sz + tpos, y, thumb, h, EDGE_RAISED,
                          BF_RECT | BF_MIDDLE, NULL);
}

/* ---- scroll bars embedded in a view --------------------------------------
 *
 * A list box, tree or list view owns its bars rather than hosting SCROLLBAR
 * children, so it needs the same hit-testing the class does. `at` is the
 * offset along the bar; the result is the new position. */

typedef struct {
    int pos, min, max, page;
    int line; /* what an arrow click scrolls by */
} ween_sbstate;

/* The last position that still shows a full page — win32's nMax - nPage + 1. */
static int sb_maxpos(const ween_sbstate *st)
{
    int m = st->page > 0 ? st->max - st->page + 1 : st->max;
    return m < st->min ? st->min : m;
}

static void sb_thumb(int len, const ween_sbstate *st, int *tpos, int *tsize)
{
    int sz = ween_scroll_metric();
    int track = len - 2 * sz, span = sb_maxpos(st) - st->min;
    *tsize = sz;
    if (st->page > 0 && st->max > st->min) {
        *tsize = MulDiv(st->page, track, st->max - st->min + 1);
        if (*tsize < sz)
            *tsize = sz;
        if (*tsize > track)
            *tsize = track;
    }
    *tpos = span > 0 ? MulDiv(st->pos - st->min, track - *tsize, span) : 0;
}

/* Act on a click. Sets *grab to where within the thumb it landed, or -1. */
/* Wine's scroll.c: one step, then a pause, then a steady repeat. */
#define WEEN_SCROLL_FIRST_DELAY 200
#define WEEN_SCROLL_REPEAT_DELAY 50
#define WEEN_SB_TIMER WEEN_SB_TIMER_ID /* shared with the window's own bars */

static int sb_click(int at, int len, const ween_sbstate *st, int *grab)
{
    int sz = ween_scroll_metric(), tpos, tsize;
    int page = st->page > 0 ? st->page : 1;
    int line = st->line > 0 ? st->line : 1;
    sb_thumb(len, st, &tpos, &tsize);
    *grab = -1;
    if (at < sz)
        return st->pos - line;
    if (at >= len - sz)
        return st->pos + line;
    if (at < sz + tpos)
        return st->pos - page;
    if (at >= sz + tpos + tsize)
        return st->pos + page;
    *grab = at - (sz + tpos);
    return st->pos;
}

/* Where a drag has moved the thumb to. */
static int sb_drag(int at, int len, const ween_sbstate *st, int grab)
{
    int sz = ween_scroll_metric(), tpos, tsize, track;
    sb_thumb(len, st, &tpos, &tsize);
    track = len - 2 * sz - tsize;
    if (track <= 0)
        return st->pos;
    return st->min + MulDiv(at - grab - sz, sb_maxpos(st) - st->min, track);
}

static int sb_clamp(int pos, const ween_sbstate *st)
{
    int max = sb_maxpos(st);
    if (pos < st->min)
        pos = st->min;
    if (pos > max)
        pos = max;
    return pos;
}

/* ---- the bars a window wears itself ---------------------------------------
 *
 * WS_HSCROLL and WS_VSCROLL are not controls: the bar lives in the window's
 * own non-client area, outside the client rectangle, and the window hears
 * WM_HSCROLL/WM_VSCROLL with a null lParam. A view that scrolls its contents
 * — an image bigger than the pane showing it — is written against these,
 * and everything below is the same arithmetic the SCROLLBAR class uses.
 */

static ween_sbstate wnd_sbstate(const struct ween_wnd *w, int vert)
{
    ween_sbstate st;
    st.pos = w->sb[vert].pos;
    st.min = w->sb[vert].min;
    st.max = w->sb[vert].max;
    st.page = w->sb[vert].page;
    st.line = 1;
    return st;
}

int ween_wnd_sb_shown(const struct ween_wnd *w, int vert)
{
    DWORD bit = vert ? WS_VSCROLL : WS_HSCROLL;
    if (w->cls && w->cls->own_scroll)
        return 0;
    return (w->style & bit) && !w->sb[vert].hidden;
}

/* Where the bar sits within the window, in window coordinates. The two meet
 * in a square at the corner, which belongs to neither and is filled with the
 * face colour, as win32 does it. */
static void wnd_sb_rect(const struct ween_wnd *w, int vert, RECT *r)
{
    int sz = ween_scroll_metric();
    int edge = ween_border_width(w);
    int right = w->w - edge, bottom = w->h - edge;
    if (vert) {
        r->left = right - sz;
        r->right = right;
        r->top = edge;
        r->bottom = bottom - (ween_wnd_sb_shown(w, 0) ? sz : 0);
    } else {
        r->left = edge;
        r->right = right - (ween_wnd_sb_shown(w, 1) ? sz : 0);
        r->top = bottom - sz;
        r->bottom = bottom;
    }
}

void ween_wnd_sb_paint(struct ween_wnd *w)
{
    struct ween_wnd *top = ween_top_level(w);
    int ox, oy, edge = ween_border_width(w);
    if (!ween_wnd_sb_shown(w, 0) && !ween_wnd_sb_shown(w, 1))
        return;
    ween_client_origin(w, &ox, &oy);
    ox -= edge; /* the window's own top-left, frame included */
    oy -= edge;
    for (int vert = 0; vert < 2; vert++) {
        RECT r;
        ween_sbstate st;
        if (!ween_wnd_sb_shown(w, vert))
            continue;
        wnd_sb_rect(w, vert, &r);
        st = wnd_sbstate(w, vert);
        ween_draw_scrollbar(&top->surface, ox + r.left, oy + r.top,
                            r.right - r.left, r.bottom - r.top, vert,
                            !w->sb[vert].disabled && sb_maxpos(&st) > st.min,
                            st.pos, st.page, st.min, st.max);
    }
    if (ween_wnd_sb_shown(w, 0) && ween_wnd_sb_shown(w, 1)) {
        int sz = ween_scroll_metric();
        ween_surface_fill(&top->surface, ox + w->w - edge - sz,
                          oy + w->h - edge - sz, sz, sz, WEEN_FACE);
    }
}

/* Which bar a point in window coordinates is in: 0 horizontal, 1 vertical,
 * -1 neither. */
static int wnd_sb_which(const struct ween_wnd *w, int wx, int wy)
{
    for (int vert = 0; vert < 2; vert++) {
        RECT r;
        if (!ween_wnd_sb_shown(w, vert))
            continue;
        wnd_sb_rect(w, vert, &r);
        if (wx >= r.left && wx < r.right && wy >= r.top && wy < r.bottom)
            return vert;
    }
    return -1;
}

int ween_wnd_sb_at(const struct ween_wnd *w, int x, int y)
{
    int ox, oy, edge = ween_border_width(w);
    ween_client_origin((HWND)w, &ox, &oy);
    return wnd_sb_which(w, x - ox + edge, y - oy + edge);
}

static void wnd_sb_notify(struct ween_wnd *w, int vert, int code)
{
    SendMessageA(w, vert ? WM_VSCROLL : WM_HSCROLL,
                 MAKEWPARAM((WORD)code, (WORD)w->sb[vert].pos), 0);
}

static void wnd_sb_set(struct ween_wnd *w, int vert, int pos, int code)
{
    ween_sbstate st = wnd_sbstate(w, vert);
    pos = sb_clamp(pos, &st);
    if (pos != w->sb[vert].pos) {
        w->sb[vert].pos = pos;
        InvalidateRect(w, NULL, FALSE);
    }
    wnd_sb_notify(w, vert, code);
}

/* Which window is mid-gesture, so that the moves and the release after a
 * press in a bar go to the same place. */
static struct ween_wnd *g_sb_track;
static int g_sb_track_vert;

int ween_wnd_sb_mouse(struct ween_wnd *w, UINT msg, int x, int y)
{
    int ox, oy, edge = ween_border_width(w), wx, wy, vert, at, len;
    RECT r;
    ween_sbstate st;

    if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONUP) {
        if (g_sb_track != w)
            return 0;
        vert = g_sb_track_vert;
    } else if (msg == WM_LBUTTONDOWN) {
        vert = -1;
    } else {
        return 0;
    }

    ween_client_origin(w, &ox, &oy);
    wx = x - ox + edge;
    wy = y - oy + edge;
    if (vert < 0) {
        vert = wnd_sb_which(w, wx, wy);
        if (vert < 0)
            return 0;
        if (w->sb[vert].disabled)
            return 1;
    }
    wnd_sb_rect(w, vert, &r);
    at = vert ? wy - r.top : wx - r.left;
    len = vert ? r.bottom - r.top : r.right - r.left;
    st = wnd_sbstate(w, vert);

    switch (msg) {
    case WM_LBUTTONDOWN: {
        int grab, pos = sb_click(at, len, &st, &grab), code;
        g_sb_track = w;
        g_sb_track_vert = vert;
        SetCapture(w);
        if (grab >= 0) {
            w->sb[vert].grab = grab;
            w->sb[vert].repeat = 0;
            return 1;
        }
        w->sb[vert].grab = -1;
        code = pos < st.pos
                   ? (at < ween_scroll_metric() ? SB_LINEUP : SB_PAGEUP)
                   : (at >= len - ween_scroll_metric() ? SB_LINEDOWN
                                                       : SB_PAGEDOWN);
        w->sb[vert].repeat = code;
        wnd_sb_set(w, vert, pos, code);
        SetTimer(w, WEEN_SB_TIMER, WEEN_SCROLL_FIRST_DELAY, NULL);
        return 1;
    }
    case WM_MOUSEMOVE:
        if (w->sb[vert].grab >= 0)
            wnd_sb_set(w, vert, sb_drag(at, len, &st, w->sb[vert].grab),
                       SB_THUMBTRACK);
        return 1;
    case WM_LBUTTONUP:
        g_sb_track = NULL;
        ReleaseCapture();
        if (w->sb[vert].repeat) {
            KillTimer(w, WEEN_SB_TIMER);
            w->sb[vert].repeat = 0;
        } else if (w->sb[vert].grab >= 0) {
            wnd_sb_notify(w, vert, SB_THUMBPOSITION);
        }
        w->sb[vert].grab = -1;
        wnd_sb_notify(w, vert, SB_ENDSCROLL);
        return 1;
    default:
        return 0;
    }
}

/* A held arrow or track keeps going: the same one step, pause, repeat the
 * SCROLLBAR class has. Called from the window's WM_TIMER. */
int ween_wnd_sb_timer(struct ween_wnd *w)
{
    int vert = g_sb_track_vert;
    ween_sbstate st;
    int step, pos;
    if (g_sb_track != w || !w->sb[vert].repeat)
        return 0;
    st = wnd_sbstate(w, vert);
    step = 1;
    if (w->sb[vert].repeat == SB_PAGEUP || w->sb[vert].repeat == SB_PAGEDOWN)
        step = st.page > 0 ? st.page : 1;
    pos = w->sb[vert].pos;
    if (w->sb[vert].repeat == SB_LINEUP || w->sb[vert].repeat == SB_PAGEUP)
        pos -= step;
    else
        pos += step;
    wnd_sb_set(w, vert, pos, w->sb[vert].repeat);
    SetTimer(w, WEEN_SB_TIMER, WEEN_SCROLL_REPEAT_DELAY, NULL);
    return 1;
}

/* ---- SetScrollInfo and the rest ------------------------------------------ */

/* The SCROLLBAR control's own range, page and position, which live in
 * different fields from a window's two bars; defined with the control. */
static ween_sbstate scroll_state(HWND wnd);

static int sb_index(HWND wnd, int bar)
{
    if (bar == SB_VERT)
        return 1;
    if (bar == SB_HORZ)
        return 0;
    if (bar == SB_CTL) /* the control's own four fields, not the window's */
        return -1;
    (void)wnd;
    return -1;
}

int SetScrollInfo(HWND wnd, int bar, const SCROLLINFO *info, BOOL redraw)
{
    int i;
    if (!wnd || !info)
        return 0;
    i = sb_index(wnd, bar);
    if (i < 0) { /* SB_CTL: the SCROLLBAR control's own state */
        ween_sbstate st;
        if (info->fMask & SIF_RANGE) {
            wnd->scroll_min = info->nMin;
            wnd->scroll_max = info->nMax;
        }
        if (info->fMask & SIF_PAGE)
            wnd->scroll_page = (int)info->nPage;
        if (info->fMask & SIF_POS)
            wnd->scroll_pos = info->nPos;
        st = scroll_state(wnd);
        wnd->scroll_pos = sb_clamp(wnd->scroll_pos, &st);
        if (redraw)
            InvalidateRect(wnd, NULL, FALSE);
        return wnd->scroll_pos;
    }
    if (info->fMask & SIF_RANGE) {
        wnd->sb[i].min = info->nMin;
        wnd->sb[i].max = info->nMax;
    }
    if (info->fMask & SIF_PAGE)
        wnd->sb[i].page = (int)info->nPage;
    if (info->fMask & SIF_POS)
        wnd->sb[i].pos = info->nPos;
    {
        ween_sbstate st = wnd_sbstate(wnd, i);
        wnd->sb[i].pos = sb_clamp(wnd->sb[i].pos, &st);
    }
    if (redraw)
        InvalidateRect(wnd, NULL, FALSE);
    return wnd->sb[i].pos;
}

BOOL GetScrollInfo(HWND wnd, int bar, SCROLLINFO *info)
{
    int i;
    if (!wnd || !info)
        return FALSE;
    i = sb_index(wnd, bar);
    if (i < 0) {
        if (info->fMask & SIF_RANGE) {
            info->nMin = wnd->scroll_min;
            info->nMax = wnd->scroll_max;
        }
        if (info->fMask & SIF_PAGE)
            info->nPage = (UINT)wnd->scroll_page;
        if (info->fMask & (SIF_POS | SIF_TRACKPOS))
            info->nPos = info->nTrackPos = wnd->scroll_pos;
        return TRUE;
    }
    if (info->fMask & SIF_RANGE) {
        info->nMin = wnd->sb[i].min;
        info->nMax = wnd->sb[i].max;
    }
    if (info->fMask & SIF_PAGE)
        info->nPage = (UINT)wnd->sb[i].page;
    if (info->fMask & (SIF_POS | SIF_TRACKPOS))
        info->nPos = info->nTrackPos = wnd->sb[i].pos;
    return TRUE;
}

int SetScrollPos(HWND wnd, int bar, int pos, BOOL redraw)
{
    SCROLLINFO si;
    int prev = GetScrollPos(wnd, bar);
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = pos;
    si.nMin = si.nMax = si.nTrackPos = 0;
    si.nPage = 0;
    SetScrollInfo(wnd, bar, &si, redraw);
    return prev;
}

int GetScrollPos(HWND wnd, int bar)
{
    int i;
    if (!wnd)
        return 0;
    i = sb_index(wnd, bar);
    return i < 0 ? wnd->scroll_pos : wnd->sb[i].pos;
}

BOOL SetScrollRange(HWND wnd, int bar, int min, int max, BOOL redraw)
{
    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE;
    si.nMin = min;
    si.nMax = max;
    si.nPos = si.nTrackPos = 0;
    si.nPage = 0;
    SetScrollInfo(wnd, bar, &si, redraw);
    return TRUE;
}

BOOL GetScrollRange(HWND wnd, int bar, int *min, int *max)
{
    int i;
    if (!wnd)
        return FALSE;
    i = sb_index(wnd, bar);
    if (min)
        *min = i < 0 ? wnd->scroll_min : wnd->sb[i].min;
    if (max)
        *max = i < 0 ? wnd->scroll_max : wnd->sb[i].max;
    return TRUE;
}

BOOL ShowScrollBar(HWND wnd, int bar, BOOL show)
{
    if (!wnd)
        return FALSE;
    if (bar == SB_HORZ || bar == SB_BOTH)
        wnd->sb[0].hidden = !show;
    if (bar == SB_VERT || bar == SB_BOTH)
        wnd->sb[1].hidden = !show;
    InvalidateRect(wnd, NULL, TRUE);
    return TRUE;
}

BOOL EnableScrollBar(HWND wnd, UINT bar, UINT flags)
{
    if (!wnd)
        return FALSE;
    if (bar == SB_HORZ || bar == SB_BOTH)
        wnd->sb[0].disabled = (flags == ESB_DISABLE_BOTH);
    if (bar == SB_VERT || bar == SB_BOTH)
        wnd->sb[1].disabled = (flags == ESB_DISABLE_BOTH);
    InvalidateRect(wnd, NULL, FALSE);
    return TRUE;
}

/* ---- the SCROLLBAR class --------------------------------------------------
 *
 * Clicking an arrow scrolls a line, the track a page, and the thumb can be
 * dragged; each tells the parent through WM_HSCROLL/WM_VSCROLL, as win32
 * does — the control itself owns no content. */

/* The bar's state, as the class holds it. */
static ween_sbstate scroll_state(HWND wnd)
{
    ween_sbstate st;
    st.pos = wnd->scroll_pos;
    st.min = wnd->scroll_min;
    st.max = wnd->scroll_max;
    st.page = wnd->scroll_page;
    st.line = 1;
    return st;
}

static void scroll_notify(HWND wnd, int code)
{
    UINT msg = (wnd->style & SBS_VERT) ? WM_VSCROLL : WM_HSCROLL;
    if (wnd->parent)
        SendMessageA(wnd->parent, msg,
                     MAKEWPARAM((WORD)code, (WORD)wnd->scroll_pos),
                     (LPARAM)wnd);
}

static void scroll_set(HWND wnd, int pos, int code);

/* One more step of whatever a held button started. A line moves by one, a
 * page by the page size — and once the thumb reaches the end there is nothing
 * left to repeat, so the timer is dropped rather than firing into a wall. */
static void scroll_repeat(HWND wnd, const ween_sbstate *st)
{
    int step = 1, pos = wnd->scroll_pos;
    if (wnd->sb_repeat == SB_PAGEUP || wnd->sb_repeat == SB_PAGEDOWN)
        step = st->page > 0 ? st->page : 1;
    if (wnd->sb_repeat == SB_LINEUP || wnd->sb_repeat == SB_PAGEUP)
        pos -= step;
    else
        pos += step;
    scroll_set(wnd, pos, wnd->sb_repeat);
    if (wnd->scroll_pos == sb_clamp(pos, st) && pos != wnd->scroll_pos) {
        KillTimer(wnd, WEEN_SB_TIMER);
        wnd->sb_repeat = 0;
    }
}

static void scroll_set(HWND wnd, int pos, int code)
{
    ween_sbstate st = scroll_state(wnd);
    pos = sb_clamp(pos, &st);
    if (pos != wnd->scroll_pos) {
        wnd->scroll_pos = pos;
        InvalidateRect(wnd, NULL, FALSE);
    }
    scroll_notify(wnd, code);
}

static LRESULT scrollbar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int vert = (wnd->style & SBS_VERT) != 0;
    int len = (vert ? wnd->h : wnd->w) -
              (ween_corner_taken(wnd) ? ween_scroll_metric() : 0);
    int at = vert ? GET_Y_LPARAM(lp) : GET_X_LPARAM(lp);
    ween_sbstate st = scroll_state(wnd);

    /* A corner rather than a bar: the hatched square a window is dragged
     * bigger by. It has no range and no thumb — what it does is tell the
     * window it is in that a resize has started, which the window answers by
     * following the pointer. */
    if (wnd->style & (SBS_SIZEGRIP | SBS_SIZEBOX)) {
        switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            struct ween_wnd *top = ween_top_level(wnd);
            RECT r;
            int ox, oy;
            BeginPaint(wnd, &ps);
            GetClientRect(wnd, &r);
            ween_client_origin(wnd, &ox, &oy);
            /* lines only: what is behind shows through, which is white on a
             * window and face at the foot of a bar */
            ween_classic_sizegrip_size(&top->surface, ox + r.right - 1,
                                       oy + r.bottom - 1,
                                       r.right < r.bottom ? r.right : r.bottom,
                                       0);
            EndPaint(wnd, &ps);
            return 0;
        }
        case WM_NCHITTEST:
            return HTBOTTOMRIGHT; /* the corner, so a frame resizes from it */
        case WM_LBUTTONDOWN:
            if (wnd->parent) { /* the window it is in does the following */
                /* where the pointer is on the screen, which is what a window
                 * being sized needs: it is about to follow it, and the
                 * corner it started from is moving underneath it */
                POINT at = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                ClientToScreen(wnd, &at);
                SendMessageA(wnd->parent, WM_SYSCOMMAND,
                             SC_SIZE | WMSZ_BOTTOMRIGHT,
                             MAKELPARAM(at.x, at.y));
            }
            return 0;
        default:
            return DefWindowProcA(wnd, msg, wp, lp);
        }
    }

    switch (msg) {
    case WM_LBUTTONDOWN: {
        int grab, pos, code;
        SetFocus(wnd);
        pos = sb_click(at, len, &st, &grab);
        if (grab >= 0) {
            SetCapture(wnd);
            wnd->drag_offset = grab;
            return 0;
        }
        code = pos < st.pos
                   ? (at < ween_scroll_metric() ? SB_LINEUP : SB_PAGEUP)
                   : (at >= len - ween_scroll_metric() ? SB_LINEDOWN
                                                       : SB_PAGEDOWN);
        scroll_set(wnd, pos, code);
        /* Holding the button keeps it going: one step, a pause, then a
         * repeat — Wine's SCROLL_FIRST_DELAY and SCROLL_REPEAT_DELAY. */
        SetCapture(wnd);
        wnd->sb_repeat = code;
        SetTimer(wnd, WEEN_SB_TIMER, WEEN_SCROLL_FIRST_DELAY, NULL);
        return 0;
    }
    case WM_TIMER:
        if (wp != WEEN_SB_TIMER || !wnd->sb_repeat)
            return 0;
        scroll_repeat(wnd, &st);
        SetTimer(wnd, WEEN_SB_TIMER, WEEN_SCROLL_REPEAT_DELAY, NULL);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == wnd && !wnd->sb_repeat)
            scroll_set(wnd, sb_drag(at, len, &st, wnd->drag_offset),
                       SB_THUMBTRACK);
        return 0;
    case WM_LBUTTONUP:
        if (wnd->sb_repeat) {
            KillTimer(wnd, WEEN_SB_TIMER);
            wnd->sb_repeat = 0;
            ReleaseCapture();
            scroll_notify(wnd, SB_ENDSCROLL);
            return 0;
        }
        if (GetCapture() == wnd) {
            ReleaseCapture();
            scroll_notify(wnd, SB_THUMBPOSITION);
            scroll_notify(wnd, SB_ENDSCROLL);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        struct ween_wnd *top = ween_top_level(wnd);
        int ox, oy;
        {
            /* A corner standing at the foot of it takes the last sixteen: the
             * bar is laid out short of them and they are left plain face for
             * the corner's lines to go over. */
            int vert = (wnd->style & SBS_VERT) != 0;
            int corner = ween_corner_taken(wnd) ? ween_scroll_metric() : 0;
            int bw = wnd->w - (vert ? 0 : corner);
            int bh = wnd->h - (vert ? corner : 0);
            BeginPaint(wnd, &ps);
            ween_client_origin(wnd, &ox, &oy);
            if (corner)
                ween_surface_fill(&top->surface, ox + (vert ? 0 : bw),
                                  oy + (vert ? bh : 0),
                                  vert ? wnd->w : corner,
                                  vert ? corner : wnd->h, WEEN_FACE);
            ween_draw_scrollbar(&top->surface, ox, oy, bw, bh, vert, 1,
                                wnd->scroll_pos, wnd->scroll_page,
                                wnd->scroll_min, wnd->scroll_max);
            EndPaint(wnd, &ps);
        }
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the EDIT class ------------------------------------------------------
 *
 * v1 draws the control: the field border, the text with its margins, and the
 * scroll bars its styles ask for. Typing needs WM_CHAR and a caret, which the
 * core does not have yet — see ROADMAP.md. */

/* The EDIT's own state: the caret, the anchor a selection runs from (the two
 * are equal when nothing is selected), and whether the caret is showing this
 * half of its blink. */
typedef struct {
    int left_margin, right_margin; /* -1 until the app says otherwise */
    int caret, anchor;
    int caret_on;
} ween_edit;

/* Win2000's default caret blink rate, the one Control Panel's slider sits at
 * in the middle of; win32 apps read it with GetCaretBlinkTime. */
#define WEEN_CARET_BLINK_MS 530
#define WEEN_CARET_TIMER 0x57454549 /* an id an app is unlikely to also pick */

static ween_edit *edit_state(HWND w)
{
    if (!w->ctl) {
        ween_edit *e = calloc(1, sizeof(ween_edit));
        if (e) /* no margin of its own until the application says */
            e->left_margin = e->right_margin = -1;
        w->ctl = e;
    }
    return w->ctl;
}

static void edit_range(const ween_edit *e, int *from, int *to)
{
    *from = e->caret < e->anchor ? e->caret : e->anchor;
    *to = e->caret < e->anchor ? e->anchor : e->caret;
}

/* Remove the selected text, if any. Returns 1 if anything went. */
static int edit_delete_selection(HWND wnd, ween_edit *e)
{
    int from, to, len = (int)strlen(wnd->text);
    edit_range(e, &from, &to);
    if (from == to)
        return 0;
    memmove(wnd->text + from, wnd->text + to, (size_t)(len - to) + 1);
    e->caret = e->anchor = from;
    return 1;
}

/* The left inset of an edit's text: the border pixel plus half an average
 * character, as Wine's EDIT_SetRectNP computes it. */
static int edit_margin(HWND wnd)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    static const char alpha[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int sum = 0;
    if (!f)
        return 4;
    for (int i = 0; i < 52; i++)
        sum += ween_strike_char_advance(f, (unsigned char)alpha[i]);
    return (ween_border_width(wnd) ? 1 : 0) + ((sum + 26) / 52) / 2;
}

/* The character index nearest an x offset within the text. */
static int edit_index_at(HWND wnd, int x)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int len = (int)strlen(wnd->text), best = 0, bestd = 1 << 30;
    if (!f)
        return 0;
    for (int i = 0; i <= len; i++) {
        int pen = ween_strike_pen(f, wnd->text, i);
        int d = pen > x ? pen - x : x - pen;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return best;
}

static void edit_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    struct ween_wnd *top = ween_top_level(wnd);
    RECT r = ps->rcPaint;
    int ox, oy, line = f ? f->ascent - f->descent : 13;
    int multi = (wnd->style & ES_MULTILINE) != 0;
    int sb = (wnd->style & WS_VSCROLL) ? ween_scroll_metric() : 0;
    const char *p = wnd->text;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush((wnd->style & WS_DISABLED) ||
                                              (wnd->style & ES_READONLY)
                                          ? COLOR_BTNFACE
                                          : COLOR_WINDOW));
    if (sb)
        ween_draw_scrollbar(&top->surface, ox + r.right - sb, oy, sb,
                            r.bottom - r.top, 1, 0, 0, 0, 0, 0);

    ween_color ink = (wnd->style & WS_DISABLED) ? WEEN_SHADOW : WEEN_BLACK;
    /* Wine's EDIT_SetRectNP: a field-bordered edit gives up one pixel on each
     * side, then the format rect is inset by the margins, which default to
     * half the average character width. */
    int inset = ween_border_width(wnd) ? 1 : 0;
    int margin = 3;
    if (f) {
        static const char alpha[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        int sum = 0;
        for (int i = 0; i < 52; i++)
            sum += ween_strike_char_advance(f, (unsigned char)alpha[i]);
        margin = ((sum + 26) / 52) / 2;
    }
    int rmargin = margin;
    {   /* unless the application said what the margins are */
        ween_edit *m = edit_state(wnd);
        if (m && m->left_margin >= 0)
            margin = m->left_margin;
        if (m && m->right_margin >= 0)
            rmargin = m->right_margin;
    }
    int tx = inset + margin;
    int ty = inset;
    /* ES_RIGHT and ES_CENTER: a single line sits against the far margin or in
     * the middle of what is between the two. A number in a field is usually
     * right-aligned — the machine's reminder-minutes box is. */
    if (!multi && f && (wnd->style & (ES_CENTER | ES_RIGHT))) {
        RECT cr;
        int tw = ween_strike_text_width(f, wnd->text, (int)strlen(wnd->text));
        int room;
        GetClientRect(wnd, &cr);
        room = cr.right - sb - (inset + margin) - (inset + rmargin) - tw;
        if (room > 0)
            tx += (wnd->style & ES_RIGHT) ? room : room / 2;
    }
    {
        ween_edit *e = edit_state(wnd);
        int from = 0, to = 0, focused = ween_focus_get() == wnd;
        if (e && focused)
            edit_range(e, &from, &to);
        while (*p) {
            const char *nl = strchr(p, '\n');
            int n = nl ? (int)(nl - p) : (int)strlen(p);
            if (n && p[n - 1] == '\r')
                n--;
            if (f) {
                /* the selected run sits on a highlight bar, in its colour */
                int off = (int)(p - wnd->text);
                int a = from - off, b = to - off;
                if (a < 0)
                    a = 0;
                if (b > n)
                    b = n;
                if (multi || a >= b) {
                    ween_strike_draw(f, &top->surface, ox + tx, oy + ty, p, n,
                                     ink);
                } else {
                    int xa = ween_strike_pen(f, p, a);
                    int xb = ween_strike_pen(f, p, b);
                    ween_strike_draw(f, &top->surface, ox + tx, oy + ty, p, a,
                                     ink);
                    ween_surface_fill(&top->surface, ox + tx + xa, oy + ty,
                                      xb - xa, line, WEEN_CAP_LEFT);
                    ween_strike_draw(f, &top->surface, ox + tx + xa, oy + ty,
                                     p + a, b - a, WEEN_WHITE);
                    ween_strike_draw(f, &top->surface, ox + tx + xb, oy + ty,
                                     p + b, n - b, ink);
                }
            }
            if (!nl || !multi)
                break;
            p = nl + 1;
            ty += line;
        }
    }

    /* the caret: a one-pixel bar where the next character will go, on for
     * half of each blink period */
    if (f && ween_focus_get() == wnd && !(wnd->style & WS_DISABLED) && !multi) {
        ween_edit *e = edit_state(wnd);
        if (e && e->caret_on) {
            int cx = tx + ween_strike_pen(f, wnd->text, e->caret);
            ween_surface_vline(&top->surface, ox + cx, oy + inset, line,
                               WEEN_BLACK);
        }
    }
}

/* The parent hears about every edit the same way. */
static void edit_changed(HWND wnd)
{
    if (wnd->parent)
        SendMessageA(wnd->parent, WM_COMMAND,
                     MAKEWPARAM((WORD)wnd->id, EN_CHANGE), (LPARAM)wnd);
}

/* A word, for double-click selection: a run of letters and digits, or a run
 * of anything else. Windows takes the trailing space with the word; the edit
 * control's own rule is the same one Notepad uses. */
static int is_word_char(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '_' ||
           (unsigned char)c >= 0x80;
}

static void edit_select_word(HWND wnd, ween_edit *e)
{
    const char *t = wnd->text;
    int len = (int)strlen(t), at = e->caret, from, to;
    if (!len)
        return;
    if (at >= len)
        at = len - 1;
    if (is_word_char(t[at])) {
        for (from = at; from > 0 && is_word_char(t[from - 1]); from--)
            ;
        for (to = at; to < len && is_word_char(t[to]); to++)
            ;
        while (to < len && t[to] == ' ') /* the trailing space goes with it */
            to++;
    } else { /* a run of whatever this is instead */
        for (from = at; from > 0 && !is_word_char(t[from - 1]) &&
                        t[from - 1] != ' ';
             from--)
            ;
        for (to = at; to < len && !is_word_char(t[to]) && t[to] != ' '; to++)
            ;
    }
    e->anchor = from;
    e->caret = to;
}

/* The selection, as a string the caller owns. NULL if nothing is selected. */
static char *edit_selected_text(HWND wnd, ween_edit *e)
{
    int from, to;
    edit_range(e, &from, &to);
    if (from == to)
        return NULL;
    char *copy = malloc((size_t)(to - from) + 1);
    if (!copy)
        return NULL;
    memcpy(copy, wnd->text + from, (size_t)(to - from));
    copy[to - from] = 0;
    return copy;
}

static void edit_insert(HWND wnd, ween_edit *e, const char *text)
{
    int len, add = (int)strlen(text);
    edit_delete_selection(wnd, e);
    len = (int)strlen(wnd->text);
    if (!add || !ween_wnd_reserve_text(wnd, len + add))
        return;
    memmove(wnd->text + e->caret + add, wnd->text + e->caret,
            (size_t)(len - e->caret) + 1);
    memcpy(wnd->text + e->caret, text, (size_t)add);
    e->caret += add;
    e->anchor = e->caret;
}

/* Typing or moving the caret makes it solid again and restarts the blink, so
 * it never vanishes just as you are looking for it — what win32 does. */
static void edit_show_caret(HWND wnd, ween_edit *e)
{
    if (!e || ween_focus_get() != wnd)
        return;
    e->caret_on = 1;
    SetTimer(wnd, WEEN_CARET_TIMER, WEEN_CARET_BLINK_MS, NULL);
}

static LRESULT edit_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_edit *e = edit_state(wnd);
    int len = (int)strlen(wnd->text);

    switch (msg) {
    case EM_SETMARGINS:
        if (e) {
            if (wp & EC_LEFTMARGIN)
                e->left_margin = (int)(short)LOWORD(lp);
            if (wp & EC_RIGHTMARGIN)
                e->right_margin = (int)(short)HIWORD(lp);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case EM_SETSEL:
        if (e) {
            int to = (int)lp < 0 ? len : (int)lp;
            e->anchor = (int)wp < 0 ? len : (int)wp;
            e->caret = to > len ? len : to;
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        edit_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
        int had = ween_focus_get() == wnd;
        if (wnd->style & WS_DISABLED)
            return 0;
        SetFocus(wnd);
        if (f && e) {
            e->caret = edit_index_at(wnd, GET_X_LPARAM(lp) - edit_margin(wnd));
            e->anchor = e->caret; /* a fresh click starts a new selection */
            edit_show_caret(wnd, e);
            SetCapture(wnd);
        }
        /* The parent hears about the click that brought the keyboard here,
         * after the caret has been placed rather than before — so a combo box
         * that takes everything in its field on the way in is not undone by
         * the very click that arrived. */
        if (!had && wnd->parent)
            SendMessageA(wnd->parent, WM_COMMAND,
                         MAKEWPARAM((WORD)wnd->id, EN_SETFOCUS), (LPARAM)wnd);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == wnd && e) {
            int at = edit_index_at(wnd, GET_X_LPARAM(lp) - edit_margin(wnd));
            if (at != e->caret) {
                e->caret = at; /* drag extends from the anchor */
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == wnd)
            ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK:
        if (e && !(wnd->style & WS_DISABLED)) {
            e->caret = edit_index_at(wnd, GET_X_LPARAM(lp) - edit_margin(wnd));
            edit_select_word(wnd, e);
            edit_show_caret(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;

    case WM_COPY:
    case WM_CUT: {
        char *sel = e ? edit_selected_text(wnd, e) : NULL;
        if (!sel)
            return 0;
        if (OpenClipboard(wnd)) {
            EmptyClipboard();
            SetClipboardData(CF_TEXT, sel); /* the clipboard owns it now */
            CloseClipboard();
        } else {
            free(sel);
            return 0;
        }
        if (msg == WM_CUT && !(wnd->style & (WS_DISABLED | ES_READONLY))) {
            edit_delete_selection(wnd, e);
            edit_changed(wnd);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_PASTE: {
        if (!e || (wnd->style & (WS_DISABLED | ES_READONLY)))
            return 0;
        if (!OpenClipboard(wnd))
            return 0;
        const char *text = (const char *)GetClipboardData(CF_TEXT);
        if (text)
            edit_insert(wnd, e, text);
        CloseClipboard();
        if (text) {
            edit_changed(wnd);
            edit_show_caret(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_SETFOCUS:
        if (e) {
            e->caret_on = 1; /* a caret appears the moment it is placed */
            SetTimer(wnd, WEEN_CARET_TIMER, WEEN_CARET_BLINK_MS, NULL);
        }
        /* the parent hears it too, which is how a combo box knows to take
         * everything in its field when the keyboard arrives */
        if (wnd->parent)
            SendMessageA(wnd->parent, WM_COMMAND,
                         MAKEWPARAM((WORD)wnd->id, EN_SETFOCUS), (LPARAM)wnd);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        KillTimer(wnd, WEEN_CARET_TIMER);
        if (e)
            e->caret_on = 0;
        if (wnd->parent)
            SendMessageA(wnd->parent, WM_COMMAND,
                         MAKEWPARAM((WORD)wnd->id, EN_KILLFOCUS), (LPARAM)wnd);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_TIMER:
        if (wp == WEEN_CARET_TIMER && e) {
            e->caret_on = !e->caret_on;
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_CHAR: {
        char ch = (char)wp;
        if (wnd->style & (WS_DISABLED | ES_READONLY))
            return 0;
        if (!e)
            return 0;
        if (ch == '\b') { /* backspace takes the selection, or one character */
            if (!edit_delete_selection(wnd, e) && e->caret > 0) {
                memmove(wnd->text + e->caret - 1, wnd->text + e->caret,
                        (size_t)(len - e->caret) + 1);
                e->caret--;
                e->anchor = e->caret;
            }
        } else if ((unsigned char)ch >= ' ') {
            edit_delete_selection(wnd, e);
            len = (int)strlen(wnd->text);
            if (!ween_wnd_reserve_text(wnd, len + 1))
                return 0; /* out of memory is the only way a character is lost */
            memmove(wnd->text + e->caret + 1, wnd->text + e->caret,
                    (size_t)(len - e->caret) + 1);
            wnd->text[e->caret] = ch;
            e->caret++;
            e->anchor = e->caret;
        } else {
            return 0;
        }
        edit_show_caret(wnd, e);
        edit_changed(wnd);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        int shift = (lp & 1) != 0;      /* the backend puts Shift in bit 0 */
        int ctrl = (lp & (1L << 28)) != 0; /* and Ctrl in bit 28 */
        int moved = 1;
        if (!e)
            return 0;
        if (ctrl) { /* the clipboard shortcuts, and select-all */
            switch (wp) {
            case 'C':
                SendMessageA(wnd, WM_COPY, 0, 0);
                return 0;
            case 'X':
                SendMessageA(wnd, WM_CUT, 0, 0);
                return 0;
            case 'V':
                SendMessageA(wnd, WM_PASTE, 0, 0);
                return 0;
            case 'A':
                e->anchor = 0;
                e->caret = len;
                InvalidateRect(wnd, NULL, FALSE);
                return 0;
            default:
                break;
            }
        }
        switch (wp) {
        case VK_LEFT:
            if (e->caret > 0)
                e->caret--;
            break;
        case VK_RIGHT:
            if (e->caret < len)
                e->caret++;
            break;
        case VK_HOME:
            e->caret = 0;
            break;
        case VK_END:
            e->caret = len;
            break;
        case VK_DELETE:
            if (wnd->style & (WS_DISABLED | ES_READONLY))
                return 0;
            if (!edit_delete_selection(wnd, e) && e->caret < len)
                memmove(wnd->text + e->caret, wnd->text + e->caret + 1,
                        (size_t)(len - e->caret));
            e->anchor = e->caret;
            moved = 0;
            break;
        default:
            return DefWindowProcA(wnd, msg, wp, lp);
        }
        if (moved && !shift) /* moving without Shift drops the selection */
            e->anchor = e->caret;
        edit_show_caret(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_SETTEXT:
        if (e)
            e->caret = e->anchor = 0;
        InvalidateRect(wnd, NULL, FALSE);
        return DefWindowProcA(wnd, msg, wp, lp);
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* The one control showing a drop-down list, if any. Only a combo box does
 * this today, and only one can be open at a time — as on Windows. */
static HWND g_dropped;

/* Tell the parent something happened, the way a common control does. */
/* A notification that carries a row and its text — which is what the two ends
 * of a label edit are. Returns what the application said. */
static LRESULT notify_disp(HWND wnd, UINT code, int row, char *text)
{
    NMLVDISPINFOA nm;
    if (!wnd->parent)
        return 0;
    memset(&nm, 0, sizeof(nm));
    nm.hdr.hwndFrom = wnd;
    nm.hdr.idFrom = wnd->id;
    nm.hdr.code = code;
    nm.item.mask = LVIF_TEXT;
    nm.item.iItem = row;
    nm.item.pszText = text;
    return SendMessageA(wnd->parent, WM_NOTIFY, (WPARAM)wnd->id, (LPARAM)&nm);
}

static void notify_parent(HWND wnd, UINT code)
{
    NMHDR nm;
    if (!wnd->parent)
        return;
    nm.hwndFrom = wnd;
    nm.idFrom = wnd->id;
    nm.code = code;
    SendMessageA(wnd->parent, WM_NOTIFY, (WPARAM)wnd->id, (LPARAM)&nm);
}

/* ---- item lists (LISTBOX, COMBOBOX) -------------------------------------- */

static void items_free(void *p); /* defined with ween_controls_free */

/* How many rows a dropped list shows before it needs a bar, and how far it
 * can be dragged. The machine's shows seven and can be pulled taller. */
#define WEEN_CB_ROWS 8
/* How far down its client a combo box puts the row of text — the field when
 * it has one, and where the box under it hangs from. */
#define WEEN_COMBO_TEXT_Y 3
#define WEEN_CB_MIN_ROWS 2

typedef struct {
    char **item;
    int *edge; /* status-bar part right edges, in client coordinates */
    HICON icon[8]; /* status bar: an icon before a part's text, or NULL */
    int *image;  /* ComboBoxEx: the image each item names, -1 for none */
    int *indent; /* ComboBoxEx: how many steps in it is drawn */
    HIMAGELIST images; /* ComboBoxEx: where those images come from */
    int count, cap, cursel, top;
    int track;  /* combo box: the item the pointer is over, -1 for none */
    int opened; /* combo box: this press is the one that opened the list */
    HWND edit;  /* combo box: the field, when its style says it can be typed
                 * in — which is what an address bar is */
    int quiet;  /* the control is writing in its own field: what comes back
                 * is not someone typing, and the owner is not told */
    int rows;   /* how many the dropped list shows, once it has been dragged
                 * taller or shorter; 0 until then */
    int drop_h; /* the height it was created with, which is the height it has
                 * with its list down — where the row count comes from */
    int item_h; /* a row's height when the program set one, 0 for the font's */
    int sizing; /* the grip is being dragged, and this is where from */
    int size_y0, size_rows0;
    char was[260]; /* and what was in it when the typing started */
} ween_items;

static ween_items *items_of(HWND w)
{
    if (!w->ctl) {
        w->ctl = calloc(1, sizeof(ween_items));
        w->ctl_free = items_free;
        if (w->ctl) {
            ((ween_items *)w->ctl)->cursel = -1;
            ((ween_items *)w->ctl)->track = -1;
        }
    }
    return w->ctl;
}

static int items_add(HWND w, const char *text)
{
    ween_items *it = items_of(w);
    if (!it)
        return -1;
    if (it->count == it->cap) {
        int cap = it->cap ? it->cap * 2 : 8;
        char **p = realloc(it->item, (size_t)cap * sizeof(*p));
        int *img = realloc(it->image, (size_t)cap * sizeof(*img));
        int *ind = realloc(it->indent, (size_t)cap * sizeof(*ind));
        if (!p || !img || !ind)
            return -1;
        it->item = p;
        it->image = img;
        it->indent = ind;
        it->cap = cap;
    }
    it->image[it->count] = -1;
    it->indent[it->count] = 0;
    {   /* strdup is not C99 */
        const char *src = text ? text : "";
        size_t n = strlen(src) + 1;
        char *copy = malloc(n);
        if (!copy)
            return -1;
        memcpy(copy, src, n);
        it->item[it->count] = copy;
    }
    return it->count++;
}

/* Per-class state is not all one shape: an EDIT holds a caret, a list box
 * holds strings it owns, a tree holds a node graph. Each says how it is freed
 * when it allocates itself, and reading one as another walked off the end of
 * the allocation. */
static void items_free(void *p)
{
    ween_items *it = p;
    for (int i = 0; i < it->count; i++)
        free(it->item[i]);
    free(it->item);
    free(it->edge);
    free(it->image);
    free(it->indent);
    free(it);
}

void ween_controls_free(HWND w)
{
    if (!w->ctl)
        return;
    if (w->ctl_free)
        w->ctl_free(w->ctl);
    else
        free(w->ctl); /* a flat struct owning nothing else */
    w->ctl = NULL;
}

/* A ComboBoxEx draws sixteen-pixel images, so its rows are sixteen tall
 * whatever the font would have asked for. */
#define WEEN_CBEX_IMAGE 16
#define WEEN_CBEX_INDENT 10 /* how far in each step of an item's indent is */
#define WEEN_CBEX_GAP 5     /* between the image and the label */

static int item_height(HWND w)
{
    const ween_strike *f = w->font ? w->font : ween_gui_font();
    const ween_items *it = w->ctl;
    int h;
    if (it && it->item_h > 0) /* the program said, and that settles it */
        return it->item_h;
    h = f ? f->ascent - f->descent : 13;
    if (it && it->images && h < WEEN_CBEX_IMAGE)
        h = WEEN_CBEX_IMAGE;
    return h;
}

/* One row of a list: the selection bar, then the text. */
static void draw_item(HWND w, HDC dc, const char *text, int x, int y, int width,
                      int h, int selected)
{
    RECT r;
    r.left = x;
    r.top = y;
    r.right = x + width;
    r.bottom = y + h;
    if (selected)
        FillRect(dc, &r, GetSysColorBrush(COLOR_HIGHLIGHT));
    SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    TextOutA(dc, x + 1, y, text, -1);
    (void)w;
}

/* ---- the LISTBOX class ---------------------------------------------------- */

/* Whether the bar is there at all: a list box with WS_VSCROLL shows one only
 * when it has more in it than it can show, unless it was told to keep it. */
static int lb_has_bar(HWND wnd, int client_h)
{
    const ween_items *it = wnd->ctl;
    int ih = item_height(wnd);
    if (!(wnd->style & WS_VSCROLL))
        return 0;
    if (wnd->style & LBS_DISABLENOSCROLL)
        return 1;
    return it && ih && it->count > client_h / ih;
}

static int lb_bar_h(HWND wnd, int client_h)
{
    return ween_corner_taken(wnd) ? client_h - ween_scroll_metric() : client_h;
}

static void listbox_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_items *it = items_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    RECT r = ps->rcPaint;
    int ih = item_height(wnd), ox, oy;
    int sb = lb_has_bar(wnd, r.bottom - r.top) ? ween_scroll_metric() : 0;
    int width = r.right - sb;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
    for (int i = 0; it && i < it->count; i++) {
        int y = (i - it->top) * ih;
        if (y + ih <= 0 || y >= r.bottom)
            continue;
        draw_item(wnd, dc, it->item[i], 0, y, width, ih, i == it->cursel);
    }
    if (sb) {
        int visible = (r.bottom - r.top) / ih;
        int maxpos = it && it->count > visible ? it->count - visible : 0;
        ween_draw_scrollbar(&top->surface, ox + width, oy, sb,
                            lb_bar_h(wnd, r.bottom - r.top), 1, 1,
                            it ? it->top : 0, visible, 0,
                            maxpos ? maxpos : (it ? it->count - 1 : 0));
    }
}

/* A list box shows whole items only: it trims its height by whatever is left
 * over, so it never ends in half a name. */
static void lb_whole_items(HWND wnd)
{
    int ih = item_height(wnd);
    if (wnd->style & LBS_NOINTEGRALHEIGHT)
        return; /* it was told to take the height it was given */
    int client = wnd->h - 2 * ween_border_width(wnd);
    int rem = ih ? client % ih : 0;
    if (wnd->h > ih && rem)
        wnd->h -= rem;
}

/* Being made is the one time it trims twice: once against the height it was
 * asked for and once against the client area left after the field border
 * comes off. That is what wine settles on, so a 62px box with 13px items ends
 * up 43, showing three of them and scrolling for the fourth. Trimming is
 * idempotent afterwards — a box already showing whole names keeps its size —
 * which is why only this one place does it twice. */
static void lb_whole_items_at_birth(HWND wnd)
{
    int ih = item_height(wnd);
    int rem;
    if (wnd->style & LBS_NOINTEGRALHEIGHT)
        return;
    rem = ih ? wnd->h % ih : 0;
    if (wnd->h > ih && rem)
        wnd->h -= rem;
    lb_whole_items(wnd);
}

static LRESULT listbox_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_items *it;
    switch (msg) {
    case WM_CREATE:
        lb_whole_items_at_birth(wnd);
        return 0;
    case WM_SIZE:
        /* and it goes on showing whole ones when it is made a different size:
         * a box dragged taller snaps to the row below */
        lb_whole_items(wnd);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        listbox_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int ih = item_height(wnd), i;
        RECT cr;
        it = items_of(wnd);
        GetClientRect(wnd, &cr);
        SetFocus(wnd);
        if (lb_has_bar(wnd, cr.bottom) && it &&
            GET_X_LPARAM(lp) >= cr.right - ween_scroll_metric()) {
            int visible = cr.bottom / (ih ? ih : 1);
            int bar = lb_bar_h(wnd, cr.bottom);
            ween_sbstate st = { it->top, 0, it->count - 1, visible, 1 };
            int grab, pos = sb_click(GET_Y_LPARAM(lp), bar, &st, &grab);
            if (grab >= 0) {
                SetCapture(wnd);
                wnd->drag_offset = grab;
            }
            it->top = sb_clamp(pos, &st);
            InvalidateRect(wnd, NULL, FALSE);
            return 0;
        }
        i = (it ? it->top : 0) + GET_Y_LPARAM(lp) / (ih ? ih : 1);
        if (it && i >= 0 && i < it->count && GET_X_LPARAM(lp) < cr.right) {
            it->cursel = i;
            InvalidateRect(wnd, NULL, FALSE);
            if (wnd->parent)
                SendMessageA(wnd->parent, WM_COMMAND,
                             MAKEWPARAM((WORD)wnd->id, LBN_SELCHANGE),
                             (LPARAM)wnd);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        RECT cr;
        int ih = item_height(wnd);
        int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        it = items_of(wnd);
        GetClientRect(wnd, &cr);
        if (it) {
            int visible = cr.bottom / (ih ? ih : 1);
            ween_sbstate st = { it->top, 0, it->count - 1, visible, 1 };
            it->top = sb_clamp(it->top - delta * 3, &st);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == wnd) {
            RECT cr;
            int ih = item_height(wnd);
            it = items_of(wnd);
            GetClientRect(wnd, &cr);
            if (it) {
                int visible = cr.bottom / (ih ? ih : 1);
                ween_sbstate st = { it->top, 0, it->count - 1, visible, 1 };
                it->top = sb_clamp(sb_drag(GET_Y_LPARAM(lp),
                                           lb_bar_h(wnd, cr.bottom), &st,
                                           wnd->drag_offset),
                                   &st);
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == wnd)
            ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        it = items_of(wnd);
        if (it && (wp == VK_UP || wp == VK_DOWN)) {
            int next = it->cursel + (wp == VK_DOWN ? 1 : -1);
            if (next >= 0 && next < it->count) {
                RECT cr;
                int ih = item_height(wnd), visible;
                GetClientRect(wnd, &cr);
                visible = cr.bottom / (ih ? ih : 1);
                it->cursel = next;
                if (next < it->top) /* scroll to keep it in view */
                    it->top = next;
                else if (next >= it->top + visible)
                    it->top = next - visible + 1;
                InvalidateRect(wnd, NULL, FALSE);
                if (wnd->parent)
                    SendMessageA(wnd->parent, WM_COMMAND,
                                 MAKEWPARAM((WORD)wnd->id, LBN_SELCHANGE),
                                 (LPARAM)wnd);
            }
            return 0;
        }
        return DefWindowProcA(wnd, msg, wp, lp);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case LB_ADDSTRING:
        return items_add(wnd, (const char *)lp);
    case LB_SETCURSEL:
        it = items_of(wnd);
        if (it)
            it->cursel = (int)wp;
        InvalidateRect(wnd, NULL, FALSE);
        return (LRESULT)wp;
    case LB_GETCURSEL:
        it = items_of(wnd);
        return it ? it->cursel : -1;
    case LB_GETTEXT: {
        /* One item's text, into the caller's buffer, returning its length —
         * the only way a program reads back what it put in. */
        int i = (int)wp;
        char *out = (char *)lp;
        it = items_of(wnd);
        if (!out || !it || i < 0 || i >= it->count) {
            if (out)
                out[0] = 0;
            return LB_ERR;
        }
        {
            size_t n = strlen(it->item[i]);
            memcpy(out, it->item[i], n + 1);
            return (LRESULT)n;
        }
    }
    case LB_GETTEXTLEN:
        it = items_of(wnd);
        return it && (int)wp >= 0 && (int)wp < it->count
                   ? (LRESULT)strlen(it->item[(int)wp])
                   : LB_ERR;
    case LB_GETITEMHEIGHT:
        /* How tall one row is, which is what a program sizing a list box to
         * whole rows has to ask: the height comes from the font it was
         * given unless the program said otherwise, so only the box knows. */
        return item_height(wnd);
    case LB_SETITEMHEIGHT:
        it = items_of(wnd);
        if (!it || (int)lp <= 0)
            return LB_ERR;
        it->item_h = (int)lp;
        lb_whole_items(wnd);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case LB_GETCOUNT:
        it = items_of(wnd);
        return it ? it->count : 0;
    case LB_GETTOPINDEX:
        it = items_of(wnd);
        return it ? it->top : 0;
    case LB_SETTOPINDEX:
        it = items_of(wnd);
        if (it)
            it->top = (int)wp;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case LB_RESETCONTENT:
        /* The items go and nothing else does: a row's height is the list
         * box's own, not the list's, so emptying it to refill it must not
         * put the rows back to the font's. */
        {
            int keep = wnd->ctl ? ((ween_items *)wnd->ctl)->item_h : 0;
            ween_controls_free(wnd);
            it = items_of(wnd);
            if (it)
                it->item_h = keep;
        }
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the COMBOBOX class ---------------------------------------------------
 *
 * The closed control only: a field with the current item and a drop-down
 * button. Opening the list needs a popup window, which the core cannot do
 * yet — see ROADMAP.md. */

static int combo_text_is_sel(ween_items *it);

static void combo_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_items *it = items_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    RECT r = ps->rcPaint;
    int btn = ween_scroll_metric(), ox, oy;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
    /* the drop-down button, and its arrow: the same glyph a scroll bar's
     * down arrow uses */
    ween_classic_scroll_arrow(&top->surface, ox + r.right - btn, oy, btn,
                              r.bottom - r.top, 1, 0, 0);
    if (it && it->edit) {
        /* a field that can be typed in draws its own text: what is left for
         * the combo is the picture that stands beside it, and only while what
         * is in the field is still that item */
        if (it->images && combo_text_is_sel(it) &&
            it->image[it->cursel] >= 0)
            ween_imagelist_draw(it->images, it->image[it->cursel],
                                &top->surface, ox + 1,
                                oy + (r.bottom - r.top - WEEN_CBEX_IMAGE) / 2);
        return;
    }
    if (it && it->cursel >= 0 && it->cursel < it->count) {
        int tx = 2;
        /* The field shows the item's image but not its indent: it is what
         * you are looking at, not where it sits in the tree. One in, with the
         * label four past it and three down, which is where the machine's
         * address bar has them. */
        if (it->images && it->image[it->cursel] >= 0) {
            ween_imagelist_draw(it->images, it->image[it->cursel],
                                &top->surface, ox + 1,
                                oy + (r.bottom - r.top - WEEN_CBEX_IMAGE) / 2);
            tx = 1 + WEEN_CBEX_IMAGE + 4;
        }
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        TextOutA(dc, tx, 3, it->item[it->cursel], -1);
    }
}

/* How many rows the list shows: what it was dragged to, or eight, and never
 * more than it has to show. */
/* How many rows the list drops. A combo box is created as tall as it is with
 * its list *down* — that is what the height in CreateWindowEx means for this
 * class — so the height the application asked for is the answer, once the
 * closed control and the border come off it. Worked out each time rather than
 * kept, because the row height changes under it: a ComboBoxEx given an image
 * list afterwards has taller rows than the one that was created.
 *
 * Dragging the corner overrides it, and eight is what is left when the
 * application asked for a height with no room to drop into. */
static int combo_list_rows(HWND wnd)
{
    ween_items *it = wnd->ctl;
    int n = it ? it->count : 0;
    int ih = item_height(wnd);
    int want = WEEN_CB_ROWS;
    if (it && it->rows)
        want = it->rows;
    else if (it && ih) {
        /* what is left of the asked-for height once the closed control and
         * the list's border come off it — measured against the height the
         * control has now, since a bar or a dialog lays it out afterwards */
        int room = it->drop_h - wnd->h - 2;
        if (room >= ih)
            want = room / ih;
    }
    if (n < want)
        want = n;
    return want < 1 ? 1 : want;
}

/* Whether it has more than it can show, and so wants a bar down its side. */
static int combo_list_scrolls(HWND wnd)
{
    ween_items *it = wnd->ctl;
    return it && it->count > combo_list_rows(wnd);
}

static int combo_list_height(HWND wnd)
{
    /* the rows and the border, and nothing else: the machine's is 162 pixels
     * for ten sixteen-pixel ones, which is 10 * 16 + 2. The corner it can be
     * dragged by does not add a strip underneath — it stands at the foot of
     * the bar's own column, which is why the bar is short of the bottom. */
    return combo_list_rows(wnd) * item_height(wnd) + 2;
}

/* How tall the bar down the side is: the rows, less the corner standing at
 * the foot of it. The machine's is 82 pixels in a 98 pixel list, and the
 * sixteen below it are the grip. */
static int combo_bar_h(HWND wnd)
{
    return combo_list_rows(wnd) * item_height(wnd) - ween_scroll_metric();
}

/* Where the dropped list sits, in surface coordinates: directly under the
 * control, its own width. */
static void combo_list_rect(HWND wnd, int *x, int *y, int *w, int *h)
{
    int ox, oy;
    ween_client_origin(wnd, &ox, &oy);
    *x = ox - ween_border_width(wnd);
    *y = oy - ween_border_width(wnd) + wnd->h;
    *w = wnd->w;
    *h = combo_list_height(wnd);
}

void ween_popup_paint(void)
{
    struct ween_wnd *top;
    ween_items *it;
    const ween_strike *f;
    int x, y, w, h, ih;
    if (!g_dropped)
        return;
    top = ween_top_level(g_dropped);
    it = g_dropped->ctl;
    f = g_dropped->font ? g_dropped->font : ween_gui_font();
    ih = item_height(g_dropped);
    combo_list_rect(g_dropped, &x, &y, &w, &h);

    /* A combo's list is a list box with WS_BORDER, and a classic window
     * border is one pixel of COLOR_WINDOWFRAME — flat black. It was drawn as
     * a sunken edge instead, which over the window beneath is a grey line on
     * two sides and a white one on the other two: no border to speak of. */
    ween_surface_fill(&top->surface, x, y, w, h, WEEN_BLACK);
    ween_surface_fill(&top->surface, x + 1, y + 1, w - 2, h - 2, WEEN_WINDOWBG);
    /* The rows that fit, from wherever the list is scrolled to; a bar down
     * the side when there are more than that, and the corner it can be
     * dragged bigger by underneath it. */
    if (combo_list_scrolls(g_dropped)) {
        /* Only a list with more in it than it can show has either: the
         * machine's, with everything in it, is white right up to the border
         * in the corner. */
        int sb = ween_scroll_metric();
        int rows = combo_list_rows(g_dropped);
        ween_draw_scrollbar(&top->surface, x + w - 1 - sb, y + 1, sb,
                            combo_bar_h(g_dropped), 1, 1, it ? it->top : 0,
                            rows, 0, (it ? it->count : 1) - 1);
        ween_classic_sizegrip(&top->surface, x + w - 2, y + h - 2);
    }
    for (int i = it ? it->top : 0; it && i < it->count; i++) {
        int iy = y + 1 + (i - it->top) * ih;
        int selected = i == (it->track >= 0 ? it->track : it->cursel);
        int tx = x + 2, th = f ? f->ascent - f->descent : ih;
        if (i - it->top >= combo_list_rows(g_dropped))
            break;
        if (it->images) {
            /* image and indent: each step of the indent moves it in, and the
             * label follows the image. The bar behind a chosen item is the
             * width of its label, as a tree's is — not the whole row. */
            int ix = x + 4 + it->indent[i] * WEEN_CBEX_INDENT;
            if (it->image[i] >= 0)
                ween_imagelist_draw(it->images, it->image[i], &top->surface, ix,
                                    iy + (ih - WEEN_CBEX_IMAGE) / 2);
            tx = ix + WEEN_CBEX_IMAGE + WEEN_CBEX_GAP;
        }
        if (selected) {
            int sbw = combo_list_scrolls(g_dropped) ? ween_scroll_metric() : 0;
            int bar = w - 2 - sbw - (tx - x - 1);
            int by = iy + (ih - th) / 2, bh = th;
            if (it->images && f) {
                bar = ween_strike_text_width(f, it->item[i],
                                             (int)strlen(it->item[i])) + 3;
                by = iy + 1; /* nearly the whole row, as the shot has it */
                bh = ih - 1;
            }
            ween_surface_fill(&top->surface, tx - 1, by, bar, bh,
                              WEEN_CAP_LEFT);
        }
        if (f)
            ween_strike_draw(f, &top->surface, tx, iy + (ih - th) / 2,
                             it->item[i], (int)strlen(it->item[i]),
                             selected ? WEEN_WHITE : WEEN_BLACK);
    }
}

void ween_combo_list_rect(HWND combo, RECT *out)
{
    int x, y, w, h;
    if (!combo || !out)
        return;
    combo_list_rect(combo, &x, &y, &w, &h);
    out->left = x;
    out->top = y;
    out->right = x + w;
    out->bottom = y + h;
}

HWND ween_popup_hit(int x, int y)
{
    int px, py, pw, ph;
    if (!g_dropped)
        return NULL;
    combo_list_rect(g_dropped, &px, &py, &pw, &ph);
    if (x >= px && x < px + pw && y >= py && y < py + ph)
        return g_dropped;
    return NULL;
}

/* Whether this combo's field can be typed in. CBS_DROPDOWNLIST is
 * CBS_DROPDOWN with another bit, so the test is for the whole value. */
static int combo_editable(const struct ween_wnd *wnd)
{
    return (wnd->style & CBS_DROPDOWNLIST) == CBS_DROPDOWN;
}

/* Whether the field still shows the item that is picked. While it does, the
 * item's picture stands beside it; type something else and the picture goes,
 * because what is in the field is no longer that item. */
static int combo_text_is_sel(ween_items *it)
{
    char now[260];
    if (!it || !it->edit || it->cursel < 0 || it->cursel >= it->count)
        return 0;
    GetWindowTextA(it->edit, now, (int)sizeof(now));
    return strcmp(now, it->item[it->cursel]) == 0;
}

/* Where the field starts: past the picture when there is one to stand
 * beside. */
static int combo_edit_x(HWND wnd, ween_items *it)
{
    /* Where the text goes, which with no margin is where the field goes: four
     * past the picture, or two in when the control has no pictures at all.
     * It does not move when the picture stops being drawn — type over an
     * address bar on the machine and the folder's icon goes, but the space it
     * stood in stays and the text keeps its inset. */
    (void)wnd;
    return it && it->images ? 1 + WEEN_CBEX_IMAGE + 4 : 2;
}

/* What a combo box is made of. A program that wants to put something under
 * the field — a box of suggestions — needs to know where the field ends and
 * the arrow begins, and only the control knows that. */
BOOL GetComboBoxInfo(HWND combo, COMBOBOXINFO *info)
{
    RECT cr;
    int btn = ween_scroll_metric(), edge;
    ween_items *it;
    if (!combo || !info)
        return FALSE;
    it = combo->ctl;
    edge = ween_border_width(combo);
    GetClientRect(combo, &cr);
    memset(&info->rcItem, 0, sizeof(info->rcItem));
    /* The band the text lives in: the rows the field is put on, and as wide
     * as what the button leaves — a pixel more than the field itself, which
     * stops one short of the button. */
    info->rcItem.left = combo_edit_x(combo, it);
    info->rcItem.top = WEEN_COMBO_TEXT_Y;
    info->rcItem.right = cr.right - btn;
    info->rcItem.bottom = cr.bottom - 1;
    (void)edge;
    info->rcButton.left = cr.right - btn;
    info->rcButton.top = 0;
    info->rcButton.right = cr.right;
    info->rcButton.bottom = cr.bottom;
    info->stateButton = 0;
    info->hwndCombo = combo;
    info->hwndItem = it ? it->edit : NULL;
    info->hwndList = NULL; /* the dropped list is drawn, not a window */
    return TRUE;
}


/* The field of a combo box has the combo's own procedure in front of it: the
 * keys that walk the list, and the two that end the typing, are the combo's
 * and never reach the box. Subclassing, as comctl32 does it. */
static WNDPROC g_combo_edit_proc;
static void combo_end_edit(HWND wnd, ween_items *it, int why, int sel);
static void combo_show_sel(HWND wnd, ween_items *it);
static void combo_commit(HWND wnd, ween_items *it);

static LRESULT CALLBACK combo_field_proc(HWND box, UINT msg, WPARAM wp,
                                         LPARAM lp)
{
    HWND cb = box->parent;
    ween_items *it = cb ? items_of(cb) : NULL;
    if (msg == WM_KEYDOWN && it && it->edit == box) {
        switch (wp) {
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
            /* a box one line tall has no use for these; the combo does */
            return SendMessageA(cb, WM_KEYDOWN, wp, lp);
        case VK_RETURN:
            if (g_dropped == cb && it->track >= 0 && it->track < it->count)
                combo_commit(cb, it); /* the list is open on something */
            else
                combo_end_edit(cb, it, CBENF_RETURN, -1);
            return 0;
        case VK_ESCAPE:
            if (g_dropped == cb) { /* the list goes; the text stays */
                g_dropped = NULL;
                it->track = -1;
                ween_damage_all(cb);
            } else {
                combo_show_sel(cb, it); /* back to what it was */
                combo_end_edit(cb, it, CBENF_ESCAPE, -1);
            }
            return 0;
        default:
            break;
        }
    }
    return CallWindowProcA(g_combo_edit_proc, box, msg, wp, lp);
}

/* Take what the list is open on, and say so. */
static void combo_commit(HWND wnd, ween_items *it)
{
    it->cursel = it->track;
    it->track = -1;
    g_dropped = NULL;
    combo_show_sel(wnd, it);
    ween_damage_all(wnd);
    if (wnd->parent)
        SendMessageA(wnd->parent, WM_COMMAND,
                     MAKEWPARAM((WORD)wnd->id, CBN_SELCHANGE), (LPARAM)wnd);
}

/* The field of an editable combo: an EDIT over everything but the button and
 * whatever picture stands before it. */
static HWND combo_edit(HWND wnd)
{
    ween_items *it = items_of(wnd);
    RECT cr;
    int btn = ween_scroll_metric(), x;
    if (!it || !combo_editable(wnd))
        return NULL;
    GetClientRect(wnd, &cr);
    if (!it->edit) {
        it->edit = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE, 2, 1,
                                   cr.right - btn - 3, cr.bottom - 2, wnd,
                                   NULL, NULL, NULL);
        if (it->edit) {
            SendMessageA(it->edit, WM_SETFONT, (WPARAM)0, FALSE);
            /* no margin of its own: where the field is put is where the text
             * starts, which is what lines it up with the picture beside it */
            SendMessageA(it->edit, EM_SETMARGINS,
                         EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
            g_combo_edit_proc = (WNDPROC)SetWindowLongPtrA(
                it->edit, GWLP_WNDPROC, (LONG_PTR)combo_field_proc);
        }
    }
    /* One row down and one shorter than the field: what the box draws then
     * lands exactly where the combo's own painting used to put it, which is
     * where the machine has it. */
    x = combo_edit_x(wnd, it);
    if (it->edit)
        MoveWindow(it->edit, x, WEEN_COMBO_TEXT_Y, cr.right - btn - x - 1,
                   cr.bottom - 1 - WEEN_COMBO_TEXT_Y, FALSE);
    return it->edit;
}

/* Say the typing is over, and why. An application answers this by going
 * where the text says — which is what an address bar is for. */
static void combo_end_edit(HWND wnd, ween_items *it, int why, int sel)
{
    NMCBEENDEDITA nm;
    if (!it || !it->edit || !wnd->parent)
        return;
    memset(&nm, 0, sizeof(nm));
    nm.hdr.hwndFrom = wnd;
    nm.hdr.idFrom = wnd->id;
    nm.hdr.code = CBEN_ENDEDITA;
    GetWindowTextA(it->edit, nm.szText, (int)sizeof(nm.szText));
    nm.fChanged = strcmp(nm.szText, it->was) != 0;
    nm.iNewSelection = sel;
    nm.iWhy = why;
    SendMessageA(wnd->parent, WM_NOTIFY, (WPARAM)wnd->id, (LPARAM)&nm);
}

/* What the field shows: the item that is picked, as text. */
static HWND combo_edit(HWND wnd);

static void combo_show_sel(HWND wnd, ween_items *it)
{
    const char *t;
    if (!it || !it->edit)
        return;
    t = it->cursel >= 0 && it->cursel < it->count ? it->item[it->cursel] : "";
    SetWindowTextA(it->edit, t);
    strncpy(it->was, t, sizeof(it->was) - 1);
    it->was[sizeof(it->was) - 1] = 0;
    (void)wnd;
}

static LRESULT combo_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_items *it;
    switch (msg) {
    case WM_SIZE:
        combo_edit(wnd); /* made, or moved to fit the new size */
        return 0;
    case CBEM_GETEDITCONTROL:
        return (LRESULT)(INT_PTR)combo_edit(wnd);
    case WM_COMMAND:
        /* what the field says about itself: the keyboard arriving, going, and
         * the text changing. Enter and Escape never get here — the combo's
         * own procedure took them in front of the box. */
        it = items_of(wnd);
        if (it && it->edit && (HWND)lp == it->edit) {
            if (HIWORD(wp) == EN_SETFOCUS) {
                /* everything in it, ready to be typed over — which is what
                 * clicking into an address bar does */
                SendMessageA(it->edit, EM_SETSEL, 0, -1);
                GetWindowTextA(it->edit, it->was, (int)sizeof(it->was));
            } else if (HIWORD(wp) == EN_KILLFOCUS) {
                combo_end_edit(wnd, it, CBENF_KILLFOCUS, -1);
            } else if (HIWORD(wp) == EN_CHANGE && !it->quiet) {
                /* the picture goes when the text is no longer that item's,
                 * and comes back when it is — the field stays where it is */
                InvalidateRect(wnd, NULL, FALSE);
                if (wnd->parent)
                    SendMessageA(wnd->parent, WM_COMMAND,
                                 MAKEWPARAM((WORD)wnd->id, CBN_EDITCHANGE),
                                 (LPARAM)wnd);
            }
            return 0;
        }
        return DefWindowProcA(wnd, msg, wp, lp);
    case WM_MOUSEWHEEL:
        it = items_of(wnd);
        if (g_dropped == wnd && it && combo_list_scrolls(wnd)) {
            int rows = combo_list_rows(wnd);
            it->top -= 3 * (GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
            if (it->top > it->count - rows)
                it->top = it->count - rows;
            if (it->top < 0)
                it->top = 0;
            ween_damage_all(wnd);
        }
        return 0;
    case WM_KEYDOWN: {
        /* Walking the list from the keyboard, whether the combo has a field
         * or is the list-only kind: Down opens it and moves through it, Up
         * comes back, Enter takes what is under the highlight and Escape
         * leaves the list as it was. */
        int ih, per;
        RECT cr;
        it = items_of(wnd);
        if (!it || !it->count)
            return 0;
        ih = item_height(wnd);
        GetClientRect(wnd, &cr);
        per = combo_list_rows(wnd); /* what a page is, is what it shows */
        switch (wp) {
        case VK_DOWN:
        case VK_NEXT:
            if (g_dropped != wnd) {
                g_dropped = wnd; /* the first Down opens it */
                it->track = it->cursel >= 0 ? it->cursel : 0;
            } else {
                int step = wp == VK_NEXT ? per : 1;
                it->track = (it->track < 0 ? -1 : it->track) + step;
                if (it->track >= it->count)
                    it->track = it->count - 1;
            }
            break;
        case VK_UP:
        case VK_PRIOR:
            if (g_dropped != wnd)
                return 0;
            {
                int step = wp == VK_PRIOR ? per : 1;
                it->track = (it->track < 0 ? it->count : it->track) - step;
                if (it->track < 0)
                    it->track = 0;
            }
            break;
        case VK_ESCAPE:
            if (g_dropped != wnd)
                return 0;
            g_dropped = NULL;
            it->track = -1;
            combo_show_sel(wnd, it); /* back to what was picked before */
            ween_damage_all(wnd);
            return 0;
        default:
            return 0;
        }
        /* the field shows what the highlight is on, as win32's does — and
         * without telling the application the text changed, since it is the
         * control moving through its own list rather than someone typing */
        if (it->edit && it->track >= 0 && it->track < it->count) {
            it->quiet = 1;
            SetWindowTextA(it->edit, it->item[it->track]);
            SendMessageA(it->edit, EM_SETSEL, 0, -1);
            it->quiet = 0;
        }
        if (it->track >= 0) { /* keep the highlight in view */
            int rows = combo_list_rows(wnd);
            if (it->track < it->top)
                it->top = it->track;
            else if (it->track >= it->top + rows)
                it->top = it->track - rows + 1;
        }
        ween_damage_all(wnd);
        (void)ih;
        (void)cr;
        return 0;
    }
    case WM_SETFOCUS:
        /* the keyboard reaching the combo reaches its field, and everything
         * in it is picked — as it is when you click into an address bar */
        it = items_of(wnd);
        if (it && combo_edit(wnd)) {
            SetFocus(it->edit);
            SendMessageA(it->edit, EM_SETSEL, 0, -1);
        }
        return 0;
    /* The item the pointer is over, or -1. Points arrive relative to our own
     * client area; the list hangs below it. */
    case WM_LBUTTONDOWN: {
        int ox, oy, px, py, pw, ph;
        int sy, ih = item_height(wnd);
        it = items_of(wnd);
        if (it && it->edit && g_dropped != wnd) {
            /* Only the button drops the list: a press in the field is the
             * field's, and it takes everything in it — which is what clicking
             * into an address bar does. */
            RECT cr;
            GetClientRect(wnd, &cr);
            if (GET_X_LPARAM(lp) < cr.right - ween_scroll_metric()) {
                SetFocus(it->edit);
                SendMessageA(it->edit, EM_SETSEL, 0, -1);
                return 0;
            }
        }
        /* A press in the list the combo itself put up leaves the keyboard
         * where it is: the field keeps it, as it does on Windows, and the
         * owner is not told the typing ended by a click on a suggestion. */
        if (!(it && it->edit && g_dropped == wnd))
            SetFocus(wnd);
        ween_client_origin(wnd, &ox, &oy);
        combo_list_rect(wnd, &px, &py, &pw, &ph);
        sy = oy + GET_Y_LPARAM(lp) - py - 1;
        if (g_dropped == wnd && sy >= 0 && sy < ph - 2) {
            int sb = ween_scroll_metric();
            int sx = oy; /* the press in the list's own coordinates */
            int rows = combo_list_rows(wnd);
            sx = ox + GET_X_LPARAM(lp) - px;
            if (it && combo_list_scrolls(wnd) && sx >= pw - 1 - sb) {
                if (sy >= combo_bar_h(wnd)) {
                    /* the corner at the foot of the bar: the list is being
                     * dragged taller or shorter */
                    it->sizing = 1;
                    it->size_y0 = oy + GET_Y_LPARAM(lp);
                    it->size_rows0 = rows;
                    SetCapture(wnd);
                    return 0;
                }
                /* the bar itself, which works the way every other one does */
                {
                    ween_sbstate st = { it->top, 0, it->count - 1, rows, 1 };
                    int grab;
                    int pos = sb_click(sy, combo_bar_h(wnd), &st, &grab);
                    it->top = sb_clamp(pos, &st);
                    ween_damage_all(wnd);
                    SetCapture(wnd);
                }
                return 0;
            }
            /* pressing in the open list starts tracking it */
            if (it)
                it->track = it->top + sy / (ih ? ih : 1);
            SetCapture(wnd);
        } else if (g_dropped == wnd) {
            g_dropped = NULL; /* a second click on the control closes it */
            if (it)
                it->track = -1;
        } else {
            g_dropped = wnd;
            if (it) {
                it->track = it->cursel;
                it->opened = 1;
            }
            SetCapture(wnd);
        }
        ween_damage_all(wnd);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int ox, oy, px, py, pw, ph, sy, ih = item_height(wnd);
        /* An open list follows the pointer whether or not the button is still
         * down: click to open, let go, and moving over the list still lights
         * up the item under it. Requiring capture meant it only tracked while
         * you dragged, and a plain click-then-move left the highlight stuck
         * on whatever was selected. Moves reach here past everything else
         * because the list is what ween_popup_hit answers with. */
        if (g_dropped != wnd)
            return 0;
        it = items_of(wnd);
        ween_client_origin(wnd, &ox, &oy);
        combo_list_rect(wnd, &px, &py, &pw, &ph);
        sy = oy + GET_Y_LPARAM(lp) - py - 1;
        if (it && it->sizing) {
            /* following the corner: the list grows and shrinks by rows */
            int moved = oy + GET_Y_LPARAM(lp) - it->size_y0;
            int rows = it->size_rows0 + moved / (ih ? ih : 1);
            if (rows < WEEN_CB_MIN_ROWS)
                rows = WEEN_CB_MIN_ROWS;
            if (rows > it->count)
                rows = it->count;
            if (rows != it->rows) {
                it->rows = rows;
                ween_damage_all(wnd);
            }
            return 0;
        }
        if (it) {
            int at = sy >= 0 && sy < ph - 2 ? it->top + sy / (ih ? ih : 1) : -1;
            if (at >= it->count)
                at = -1;
            if (at != it->track) {
                it->track = at;
                it->opened = 0; /* a drag into the list commits on release */
                ween_damage_all(wnd);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int ox, oy, px, py, pw, ph, sy, ih = item_height(wnd);
        if (GetCapture() == wnd)
            ReleaseCapture();
        if (g_dropped != wnd)
            return 0;
        it = items_of(wnd);
        if (it && it->sizing) { /* the drag is over; the size stays */
            it->sizing = 0;
            return 0;
        }
        ween_client_origin(wnd, &ox, &oy);
        combo_list_rect(wnd, &px, &py, &pw, &ph);
        sy = oy + GET_Y_LPARAM(lp) - py - 1;
        if (sy >= 0 && sy < ph - 2 && it) {
            /* releasing over an item picks it and closes the list */
            int at = it->top + sy / (ih ? ih : 1);
            if (at >= 0 && at < it->count) {
                it->cursel = at;
                combo_show_sel(wnd, it); /* the field shows what was picked */
                if (wnd->parent)
                    SendMessageA(wnd->parent, WM_COMMAND,
                                 MAKEWPARAM((WORD)wnd->id, CBN_SELCHANGE),
                                 (LPARAM)wnd);
            }
            it->track = -1;
            g_dropped = NULL;
        } else if (it && !it->opened) {
            g_dropped = NULL; /* released off the list without opening it */
            it->track = -1;
        }
        if (it)
            it->opened = 0;
        ween_damage_all(wnd);
        return 0;
    }
    case WM_KILLFOCUS:
        if (g_dropped == wnd) {
            it = wnd->ctl;
            if (it)
                it->track = -1;
            g_dropped = NULL;
            ween_damage_all(wnd);
        }
        return 0;
    case WM_DESTROY:
        if (g_dropped == wnd)
            g_dropped = NULL;
        return 0;
    case WM_CREATE:
        /* The combo box wears the field border itself, whatever ex-style it
         * was created with, and a closed drop-down list is one item tall
         * however high the app asked for. */
        wnd->ex_style |= WS_EX_CLIENTEDGE;
        it = items_of(wnd);
        if (it)
            it->drop_h = wnd->h; /* what it will be when the list is down */
        wnd->h = item_height(wnd) + 4 + 2 * ween_border_width(wnd);
        combo_edit(wnd); /* a field, when the style says it can be typed in */
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        combo_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case CB_ADDSTRING:
        return items_add(wnd, (const char *)lp);
    case CB_GETITEMHEIGHT:
        /* how tall one row of the dropped list is, which is what anyone
         * sizing a combo box to a number of rows has to ask */
        return item_height(wnd);
    case CBEM_SETIMAGELIST: {
        HIMAGELIST was;
        it = items_of(wnd);
        if (!it)
            return 0;
        was = it->images;
        it->images = (HIMAGELIST)lp;
        /* the rows grow to fit the images, and so does the closed control */
        wnd->h = item_height(wnd) + 4 + 2 * ween_border_width(wnd);
        InvalidateRect(wnd, NULL, FALSE);
        return (LRESULT)(INT_PTR)was;
    }
    case CBEM_INSERTITEMA: {
        const COMBOBOXEXITEMA *ci = (const COMBOBOXEXITEMA *)lp;
        int at;
        if (!ci)
            return -1;
        at = items_add(wnd, (ci->mask & CBEIF_TEXT) ? ci->pszText : "");
        if (at < 0)
            return -1;
        it = items_of(wnd);
        if (ci->mask & CBEIF_IMAGE)
            it->image[at] = ci->iImage;
        if (ci->mask & CBEIF_INDENT)
            it->indent[at] = ci->iIndent;
        InvalidateRect(wnd, NULL, FALSE);
        return at;
    }
    case CB_SHOWDROPDOWN:
        it = items_of(wnd);
        if (wp && it && it->count) {
            g_dropped = wnd;
            it->track = -1;
            it->opened = 1;
        } else if (g_dropped == wnd) {
            g_dropped = NULL;
            if (it)
                it->track = -1;
        }
        ween_damage_all(wnd);
        return 0;
    case CB_GETDROPPEDSTATE:
        return g_dropped == wnd;
    case CB_RESETCONTENT:
        /* Emptying it takes the selection with it, so the field goes blank
         * rather than keeping the item that was there. An app that refills a
         * combo — an address bar, say — otherwise piles new entries behind
         * the first one and goes on showing that one for ever. */
        {   /* The items go and nothing else does. The image list, the field,
             * the height the list drops to and how far the corner has been
             * dragged all belong to the control rather than to what is in it:
             * emptying an address bar to refill it must not take away the box
             * the path is typed in, nor make its list a different size. */
            ween_items keep;
            memset(&keep, 0, sizeof(keep));
            if (wnd->ctl)
                keep = *(ween_items *)wnd->ctl;
            ween_controls_free(wnd);
            it = items_of(wnd);
            if (it) {
                it->images = keep.images;
                it->edit = keep.edit;
                it->drop_h = keep.drop_h;
                it->rows = keep.rows;
            }
        }
        if (g_dropped == wnd)
            g_dropped = NULL;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case CB_SETCURSEL:
        it = items_of(wnd);
        if (it) {
            it->cursel = (int)wp;
            combo_show_sel(wnd, it);
        }
        InvalidateRect(wnd, NULL, FALSE);
        return (LRESULT)wp;
    case CB_GETCURSEL:
        it = items_of(wnd);
        return it ? it->cursel : -1;
    case CB_GETCOUNT:
        it = items_of(wnd);
        return it ? it->count : 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the progress bar ------------------------------------------------------
 *
 * Wine's progress.c: the control swaps WS_EX_CLIENTEDGE for WS_EX_STATICEDGE
 * when unthemed, insets its client by one more pixel, and fills it either
 * solid (PBS_SMOOTH) or in chunks two thirds as wide as the bar is tall with a
 * two-pixel gap. */

#define WEEN_LED_GAP 2

static void progress_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    RECT r = ps->rcPaint, bar;
    int pos = wnd->scroll_pos, min = wnd->scroll_min, max = wnd->scroll_max;
    int span, filled;

    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    bar = r;
    bar.left++;
    bar.top++;
    bar.right--;
    bar.bottom--;
    span = bar.right - bar.left;
    if (span <= 0 || max <= min)
        return;
    filled = MulDiv(pos - min, span, max - min);

    if (wnd->style & PBS_SMOOTH) {
        RECT f = bar;
        f.right = f.left + filled;
        FillRect(dc, &f, GetSysColorBrush(COLOR_HIGHLIGHT));
        return;
    }
    /* the filled length is rounded up to whole chunks, as Wine does */
    int led = MulDiv(bar.bottom - bar.top, 2, 3);
    int step = led + WEEN_LED_GAP;
    RECT chunk = bar;
    filled = (filled + step - 1) / step * step;
    if (filled > span)
        filled = span;
    chunk.left = bar.left;
    while (chunk.left < bar.left + filled) {
        chunk.right = chunk.left + led;
        if (chunk.right > bar.left + filled)
            chunk.right = bar.left + filled;
        FillRect(dc, &chunk, GetSysColorBrush(COLOR_HIGHLIGHT));
        chunk.left = chunk.right + WEEN_LED_GAP;
    }
}

static LRESULT progress_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        /* the classic control asks for the status-field border, not the
         * field border it was created with */
        wnd->ex_style &= ~(DWORD)WS_EX_CLIENTEDGE;
        wnd->ex_style |= WS_EX_STATICEDGE;
        wnd->scroll_min = 0;
        wnd->scroll_max = 100;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        progress_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case PBM_SETPOS: {
        int old = wnd->scroll_pos;
        wnd->scroll_pos = (int)wp;
        InvalidateRect(wnd, NULL, FALSE);
        return old;
    }
    case PBM_SETRANGE:
        wnd->scroll_min = (int)LOWORD(lp);
        wnd->scroll_max = (int)HIWORD(lp);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case PBM_SETRANGE32:
        wnd->scroll_min = (int)wp;
        wnd->scroll_max = (int)lp;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the tree view --------------------------------------------------------
 *
 * Items on 16-pixel rows, each level indented 19; a 9x9 button with a plus or
 * minus where an item has children, and dotted lines — every other pixel —
 * joining a parent to its children. */

typedef struct ween_tvitem {
    char *text;
    struct ween_tvitem *parent, *child, *next;
    int expanded;
    int state_img; /* the picture before it: 1-based into the state list, 0 none */
    int image;     /* index into the view's image list, -1 for none */
    int sel_image; /* the one it wears while selected, -1 to keep `image` */
    int cchildren; /* said to have children before it has any: an item that
                    * fills itself when opened still needs the box to open */
} ween_tvitem;

typedef struct {
    HIMAGELIST images;       /* the icons items name by index */
    HIMAGELIST state_images; /* the tick boxes and option buttons before them */
    ween_tvitem *root;
    ween_tvitem *sel;
    int scroll_x, content_w; /* horizontal scroll, and what there is to scroll */
    int scroll_row, rows;    /* vertical, counted in items */
} ween_tree;

#define WEEN_TV_ITEM_H 16
/* The row of white a tree keeps above its first item. The shell's has one and
 * it puts every line and every dot a pixel further down, which is enough to
 * put the dotted runs out of phase with the machine's. */
#define WEEN_TV_TOP_MARGIN 1
#define WEEN_TV_INDENT 19
#define WEEN_TV_BUTTON 9

static void tree_ctl_free(void *p);

static ween_tree *tree_of(HWND w)
{
    if (!w->ctl) {
        w->ctl = calloc(1, sizeof(ween_tree));
        w->ctl_free = tree_ctl_free;
    }
    return w->ctl;
}

static void tree_free(ween_tvitem *it)
{
    while (it) {
        ween_tvitem *next = it->next;
        tree_free(it->child);
        free(it->text);
        free(it);
        it = next;
    }
}

/* The whole node graph hangs off the root. */
static void tree_ctl_free(void *p)
{
    ween_tree *t = p;
    tree_free(t->root);
    free(t);
}

/* Every other pixel, counted from where the run starts. Every run in a tree
 * begins on a row edge or a row centre, and rows are an even number of pixels
 * apart, so they all come out on the same parity and a line drawn in two goes
 * still reads as one. */
static void dotted_v(ween_surface *s, int x, int y0, int y1, ween_color c)
{
    for (int y = y0; y < y1; y += 2)
        ween_surface_pixel(s, x, y, c);
}

static void dotted_h(ween_surface *s, int y, int x0, int x1, ween_color c)
{
    for (int x = x0; x < x1; x += 2)
        ween_surface_pixel(s, x, y, c);
}

/* Draw one level of the tree; returns the row after the last one drawn. */
/* Where an item's button goes, measured from the tree's own origin. The
 * picture and the hit test both come through here: worked out twice they
 * drift apart, and then the box on the screen does not answer the mouse. */
static int tv_button_col(int depth, int at_root)
{
    return (depth + at_root) * WEEN_TV_INDENT - WEEN_TV_INDENT + 4;
}

static int tree_button_x(const struct ween_wnd *wnd, int depth, int scroll_x)
{
    return tv_button_col(depth, (wnd->style & TVS_LINESATROOT) != 0) - scroll_x;
}

/* and whether it has one at all: a root item is bare unless the style
 * carries the lines out to it */
static int tree_has_button(const struct ween_wnd *wnd, const ween_tvitem *it,
                           int depth)
{
    int at_root = (wnd->style & TVS_LINESATROOT) != 0;
    return (it->child || it->cchildren) && (depth > 0 || at_root);
}

/* sel_state says how the selected item is drawn: 2 when the tree has the
 * focus — the highlight and white text — 1 when it has not but the style says
 * to show the selection anyway, which is the grey box a list keeps, and 0
 * when it shows nothing at all. A shell's tree is the last of those: click
 * away from it and the item it had stops being marked. */
static int tree_draw(ween_surface *s, const ween_strike *f, ween_tvitem *first,
                     int ox, int oy, int depth, int row, int lines, int at_root,
                     const ween_tvitem *sel, int sel_state, HIMAGELIST images,
                     HIMAGELIST state_images, int buttons)
{
    int th = f ? f->ascent - f->descent : 13;
    int icon_w = 0, icon_h = 0, st_w = 0, st_h = 0;
    if (images)
        ImageList_GetIconSize(images, &icon_w, &icon_h);
    if (state_images)
        ImageList_GetIconSize(state_images, &st_w, &st_h);
    for (ween_tvitem *it = first; it; it = it->next) {
        int y = oy + WEEN_TV_TOP_MARGIN + row * WEEN_TV_ITEM_H;
        /* The column this item's own picture starts in. TVS_LINESATROOT
         * pushes the whole tree right by one level, because a root that has a
         * button needs a column to put it in; without the style a root item
         * starts hard against the left edge, which is where the shell's
         * Desktop is. The button goes in the column before the picture. */
        int col = ox + (depth + (at_root ? 1 : 0)) * WEEN_TV_INDENT;
        int bx = ox + tv_button_col(depth, at_root ? 1 : 0);
        int cx = bx + WEEN_TV_BUTTON / 2;
        int cy = y + WEEN_TV_ITEM_H / 2;
        int tx = col;

        /* TVS_LINESATROOT is what carries the lines and the boxes out to the
         * top level. Without it the roots sit bare and only what is under
         * them is joined up, which is what win32 does with the style off. */
        int marked = depth > 0 || at_root;
        if (lines && marked) {
            /* the stub out to the text, the run up to the sibling above and
             * the one down to the sibling below */
            /* right up to the picture, not three short of it */
            dotted_h(s, cy, cx, tx, WEEN_SHADOW);
            if (it != first || depth > 0) /* up to the sibling or the parent */
                dotted_v(s, cx, y, cy, WEEN_SHADOW);
        }
        if ((it->child || it->cchildren) && marked && buttons) {
            /* the button: a grey box with a plus or minus in it */
            ween_surface_rect(s, bx, cy - 4, WEEN_TV_BUTTON, WEEN_TV_BUTTON,
                              WEEN_SHADOW);
            ween_surface_fill(s, bx + 1, cy - 3, WEEN_TV_BUTTON - 2,
                              WEEN_TV_BUTTON - 2, WEEN_WHITE);
            ween_surface_hline(s, bx + 2, cy, WEEN_TV_BUTTON - 4, WEEN_BLACK);
            if (!it->expanded)
                ween_surface_vline(s, cx, cy - 2, WEEN_TV_BUTTON - 4, WEEN_BLACK);
        }
        /* The state column comes before everything the item draws in: the
         * tick box or option button a page of settings puts against each of
         * its rows. It is reserved for every item once the list is set, so
         * the ones that draw nothing there still start where the others do. */
        if (state_images) {
            if (it->state_img > 0)
                ween_imagelist_draw(state_images, it->state_img - 1, s, tx,
                                    y + (WEEN_TV_ITEM_H - st_h) / 2);
            tx += st_w + 5;
        }
        if (images && it->image >= 0) {
            /* the icon goes between the button and the label, and the label
             * moves over to make room for it. A selected item may wear a
             * different one — an open folder, which is what a shell does. */
            /* A picked item's picture is drawn as it is, over the highlight
             * — the machine washes nothing into it, in a tree or in a list. */
            int img = (it == sel && it->sel_image >= 0) ? it->sel_image
                                                        : it->image;
            ween_imagelist_draw(images, img, s, tx,
                                y + (WEEN_TV_ITEM_H - icon_h) / 2);
            tx += icon_w + 5;
        }
        if (f && it->text) {
            /* The label sits on the row's own baseline: the strike's cell
             * centred in what the row keeps for it, which is two less than
             * the row. Both faces are measured — the shell's folder tree is
             * set in Tahoma and this one, in a dialog, in MS Sans Serif, and
             * the two cells differ by two rows. */
            int cell = f->cell_h ? f->cell_h : th;
            int ty = y + (WEEN_TV_ITEM_H - 2 - cell) / 2;
            int selected = it == sel && sel_state;
            if (selected) {
                /* The bar is the whole row's height, not the text's, and it
                 * starts two before the pen: sixteen deep and the label plus
                 * four across, which is what the machine draws. */
                int tw = ween_strike_text_width(f, it->text,
                                                (int)strlen(it->text));
                ween_surface_fill(s, tx - 2, y, tw + 4, WEEN_TV_ITEM_H,
                                  sel_state == 2 ? WEEN_CAP_LEFT : WEEN_FACE);
                /* and the caret over it, once the keyboard has been used —
                 * the same rule that brings out a menu's underlines */
                if (sel_state == 2 && ween_ui_focus_cues)
                    ween_surface_focus_rect(s, tx - 2, y, tw + 4,
                                            WEEN_TV_ITEM_H);
            }
            ween_strike_draw(f, s, tx, ty, it->text, (int)strlen(it->text),
                             sel_state == 2 && selected ? WEEN_WHITE
                                                        : WEEN_BLACK);
        }
        row++;
        if (it->expanded && it->child)
            row = tree_draw(s, f, it->child, ox, oy, depth + 1, row, lines,
                            at_root, sel, sel_state, images, state_images,
                            buttons);
        /* Down to the next sibling — past this item's children, when it has
         * any open. Drawn after them so the run is one length rather than
         * one per row, and so an item with a subtree under it still has the
         * line running down its own level beside that subtree. */
        if (lines && marked && it->next) {
            /* From below the button rather than from the middle of it: the
             * dots are every other row from the item's top, so starting six
             * past the centre clears the box and stays in step. */
            int from = ((it->child || it->cchildren) && marked) ? cy + 6 : cy;
            dotted_v(s, cx, from,
                     oy + WEEN_TV_TOP_MARGIN + row * WEEN_TV_ITEM_H,
                     WEEN_SHADOW);
        }
    }
    return row;
}

/* A tree's notifications name the item they are about, which is what an app
 * filling a level lazily needs to know. */
static void notify_tree(HWND wnd, UINT code, UINT action, ween_tvitem *item)
{
    NMTREEVIEWA nm;
    if (!wnd->parent)
        return;
    memset(&nm, 0, sizeof(nm));
    nm.hdr.hwndFrom = wnd;
    nm.hdr.idFrom = wnd->id;
    nm.hdr.code = code;
    nm.action = action;
    nm.itemNew.mask = TVIF_HANDLE;
    nm.itemNew.hItem = item;
    SendMessageA(wnd->parent, WM_NOTIFY, (WPARAM)wnd->id, (LPARAM)&nm);
}

/* Open or close an item, telling the app before and after. Before is where a
 * tree that fills a level only when it is opened puts the children, so that
 * what is drawn straight after already has them. */
static void tree_expand(HWND wnd, ween_tvitem *item, int expand)
{
    if (!item || item->expanded == expand)
        return;
    notify_tree(wnd, TVN_ITEMEXPANDINGA, expand ? TVE_EXPAND : TVE_COLLAPSE,
                item);
    item->expanded = expand;
    notify_tree(wnd, TVN_ITEMEXPANDEDA, expand ? TVE_EXPAND : TVE_COLLAPSE,
                item);
    InvalidateRect(wnd, NULL, FALSE);
}

/* How many rows the tree shows when fully walked. */
static int tree_rows(ween_tvitem *first)
{
    int n = 0;
    for (ween_tvitem *it = first; it; it = it->next) {
        n++;
        if (it->expanded && it->child)
            n += tree_rows(it->child);
    }
    return n;
}

/* The item on a given visible row, and how deep it sits. */
/* The visible row an item is on, counting from the first root, or -1. The
 * keyboard walks the tree by row: what Down does is take the next one. */
static int tree_row_of(ween_tvitem *first, const ween_tvitem *want, int *row)
{
    for (ween_tvitem *it = first; it; it = it->next) {
        if (it == want)
            return *row;
        (*row)++;
        if (it->expanded && it->child) {
            int r = tree_row_of(it->child, want, row);
            if (r >= 0)
                return r;
        }
    }
    return -1;
}

static ween_tvitem *tree_at_row(ween_tvitem *first, int depth, int want,
                                int *row, int *depth_out)
{
    for (ween_tvitem *it = first; it; it = it->next) {
        if (*row == want) {
            *depth_out = depth;
            return it;
        }
        (*row)++;
        if (it->expanded && it->child) {
            ween_tvitem *hit =
                tree_at_row(it->child, depth + 1, want, row, depth_out);
            if (hit)
                return hit;
        }
    }
    return NULL;
}

/* How far right the widest item reaches — what the horizontal scroll bar
 * measures itself against. */
/* How far right the widest item reaches — the same arithmetic the drawing
 * uses, because a bar that comes up when the text in fact fits is worse than
 * no bar at all: the shell's tree has none here and ours had one. */
static int tree_extent(const ween_strike *f, ween_tvitem *first, int depth,
                       int at_root, int icon_w)
{
    int max = 0;
    for (ween_tvitem *it = first; it; it = it->next) {
        int w = (depth + (at_root ? 1 : 0)) * WEEN_TV_INDENT;
        if (icon_w && it->image >= 0)
            w += icon_w + 5;
        if (f && it->text)
            w += ween_strike_text_width(f, it->text, (int)strlen(it->text));
        if (w > max)
            max = w;
        if (it->expanded && it->child) {
            int c = tree_extent(f, it->child, depth + 1, at_root, icon_w);
            if (c > max)
                max = c;
        }
    }
    return max;
}

static void treeview_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_tree *t = tree_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT r = ps->rcPaint, clip;
    int ox, oy, sb = ween_scroll_metric();
    int hbar, vbar, view_w, view_h, visible;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
    if (!t || !t->root)
        return;

    /* each bar takes a strip, and taking one can bring the other on */
    {
        int iw = 0, ih = 0;
        if (t->images)
            ImageList_GetIconSize(t->images, &iw, &ih);
        t->content_w =
            tree_extent(f, t->root, 0, (wnd->style & TVS_LINESATROOT) != 0, iw);
    }
    t->rows = tree_rows(t->root);
    hbar = t->content_w > r.right;
    vbar = t->rows * WEEN_TV_ITEM_H > r.bottom - (hbar ? sb : 0);
    hbar = t->content_w > r.right - (vbar ? sb : 0);
    view_w = r.right - (vbar ? sb : 0);
    view_h = r.bottom - (hbar ? sb : 0);
    visible = view_h / WEEN_TV_ITEM_H;

    if (!hbar)
        t->scroll_x = 0;
    if (!vbar)
        t->scroll_row = 0;
    else if (t->scroll_row > t->rows - visible)
        t->scroll_row = t->rows - visible;

    ween_surface_get_clip(&top->surface, &clip);
    ween_surface_clip(&top->surface, clip.left, clip.top,
                      (ox + view_w < clip.right ? ox + view_w : clip.right) -
                          clip.left,
                      (oy + view_h < clip.bottom ? oy + view_h : clip.bottom) -
                          clip.top);
    tree_draw(&top->surface, f, t->root, ox - t->scroll_x,
              oy - t->scroll_row * WEEN_TV_ITEM_H, 0, 0,
              (wnd->style & TVS_HASLINES) != 0,
              (wnd->style & TVS_LINESATROOT) != 0, t->sel,
              ween_focus_get() == wnd  ? 2
              : (wnd->style & TVS_SHOWSELALWAYS) ? 1
                                                 : 0,
              t->images, t->state_images,
              (wnd->style & TVS_HASBUTTONS) != 0);
    ween_surface_clip(&top->surface, clip.left, clip.top,
                      clip.right - clip.left, clip.bottom - clip.top);

    if (hbar)
        ween_draw_scrollbar(&top->surface, ox, oy + r.bottom - sb, view_w, sb, 0,
                            1, t->scroll_x, view_w, 0, t->content_w - 1);
    if (vbar)
        ween_draw_scrollbar(&top->surface, ox + r.right - sb, oy, sb, view_h, 1,
                            1, t->scroll_row, visible, 0, t->rows - 1);
    if (hbar && vbar) /* the dead square where they meet */
        ween_surface_fill(&top->surface, ox + r.right - sb, oy + r.bottom - sb,
                          sb, sb, WEEN_FACE);
}

static LRESULT treeview_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_tree *t;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        treeview_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int row = 0, depth = 0;
        int want = (GET_Y_LPARAM(lp) - WEEN_TV_TOP_MARGIN) / WEEN_TV_ITEM_H;
        ween_tvitem *hit;
        RECT cr;
        int sb = ween_scroll_metric();
        t = tree_of(wnd);
        SetFocus(wnd);
        if (!t || !t->root)
            return 0;
        GetClientRect(wnd, &cr);
        {
            int hbar = t->content_w > cr.right;
            int vbar = t->rows * WEEN_TV_ITEM_H > cr.bottom - (hbar ? sb : 0);
            int view_w = cr.right - (vbar ? sb : 0);
            int view_h = cr.bottom - (hbar ? sb : 0);
            int visible = view_h / WEEN_TV_ITEM_H;
            int grab, pos;
            hbar = t->content_w > view_w;
            if (hbar && GET_Y_LPARAM(lp) >= cr.bottom - sb) {
                ween_sbstate st = { t->scroll_x, 0, t->content_w - 1, view_w, 8 };
                pos = sb_click(GET_X_LPARAM(lp), view_w, &st, &grab);
                if (grab >= 0) {
                    SetCapture(wnd);
                    wnd->drag_offset = grab;
                    wnd->drag_vertical = 0;
                }
                t->scroll_x = sb_clamp(pos, &st);
                InvalidateRect(wnd, NULL, FALSE);
                return 0;
            }
            if (vbar && GET_X_LPARAM(lp) >= cr.right - sb) {
                ween_sbstate st = { t->scroll_row, 0, t->rows - 1, visible, 1 };
                pos = sb_click(GET_Y_LPARAM(lp), view_h, &st, &grab);
                if (grab >= 0) {
                    SetCapture(wnd);
                    wnd->drag_offset = grab;
                    wnd->drag_vertical = 1;
                }
                t->scroll_row = sb_clamp(pos, &st);
                InvalidateRect(wnd, NULL, FALSE);
                return 0;
            }
            want += t->scroll_row;
        }
        hit = tree_at_row(t->root, 0, want, &row, &depth);
        if (!hit)
            return 0;
        /* the pointer has taken over: the caret goes until a key asks for it,
         * which is the rule the list keeps too */
        if (ween_ui_focus_cues) {
            ween_ui_focus_cues = 0;
            InvalidateRect(wnd, NULL, FALSE);
        }
        {   /* the button toggles, anywhere else selects */
            int bx = tree_button_x(wnd, depth, t->scroll_x);
            int x = GET_X_LPARAM(lp);
            if (tree_has_button(wnd, hit, depth) && x >= bx &&
                x < bx + WEEN_TV_BUTTON) {
                tree_expand(wnd, hit, !hit->expanded);
            } else {
                t->sel = hit;
                notify_parent(wnd, TVN_SELCHANGEDA);
            }
        }
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        RECT cr;
        int sb2 = ween_scroll_metric(), lines = 3;
        int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        t = tree_of(wnd);
        GetClientRect(wnd, &cr);
        if (t) {
            int view_h = cr.bottom - (t->content_w > cr.right ? sb2 : 0);
            int visible = view_h / WEEN_TV_ITEM_H;
            ween_sbstate st = { t->scroll_row, 0, t->rows - 1, visible, 1 };
            t->scroll_row = sb_clamp(t->scroll_row - delta * lines, &st);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == wnd) {
            RECT cr;
            int sb2 = ween_scroll_metric();
            t = tree_of(wnd);
            GetClientRect(wnd, &cr);
            if (t && wnd->drag_vertical) {
                int view_h = cr.bottom - (t->content_w > cr.right ? sb2 : 0);
                int visible = view_h / WEEN_TV_ITEM_H;
                ween_sbstate st = { t->scroll_row, 0, t->rows - 1, visible, 1 };
                t->scroll_row = sb_clamp(
                    sb_drag(GET_Y_LPARAM(lp), view_h, &st, wnd->drag_offset), &st);
                InvalidateRect(wnd, NULL, FALSE);
            } else if (t) {
                int view_w = cr.right -
                             (t->rows * WEEN_TV_ITEM_H > cr.bottom ? sb2 : 0);
                ween_sbstate st = { t->scroll_x, 0, t->content_w - 1, view_w, 8 };
                t->scroll_x = sb_clamp(
                    sb_drag(GET_X_LPARAM(lp), view_w, &st, wnd->drag_offset), &st);
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == wnd)
            ReleaseCapture();
        return 0;
    case TVM_SELECTITEM:
        t = tree_of(wnd);
        if (t)
            t->sel = (ween_tvitem *)lp;
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    case TVM_INSERTITEMA: {
        const TVINSERTSTRUCTA *is = (const TVINSERTSTRUCTA *)lp;
        ween_tvitem *item, **link;
        size_t n;
        if (!is || !(t = tree_of(wnd)))
            return 0;
        item = calloc(1, sizeof(*item));
        if (!item)
            return 0;
        item->image = (is->item.mask & TVIF_IMAGE) ? is->item.iImage : -1;
        item->state_img = (is->item.mask & TVIF_STATE)
                              ? (int)((is->item.state & TVIS_STATEIMAGEMASK) >>
                                      12)
                              : 0;
        item->sel_image = (is->item.mask & TVIF_SELECTEDIMAGE)
                              ? is->item.iSelectedImage
                              : -1;
        item->cchildren = (is->item.mask & TVIF_CHILDREN) ? is->item.cChildren
                                                          : 0;
        n = strlen(is->item.pszText ? is->item.pszText : "") + 1;
        item->text = malloc(n);
        if (item->text)
            memcpy(item->text, is->item.pszText ? is->item.pszText : "", n);
        if (is->hParent && is->hParent != TVI_ROOT) {
            item->parent = is->hParent;
            link = &is->hParent->child;
        } else {
            link = &t->root;
        }
        while (*link)
            link = &(*link)->next;
        *link = item;
        InvalidateRect(wnd, NULL, FALSE);
        return (LRESULT)(UINT_PTR)item;
    }
    case TVM_DELETEITEM: {
        /* TVI_ROOT empties the whole tree, which is what a refill needs. */
        ween_tvitem *it = (ween_tvitem *)lp;
        t = tree_of(wnd);
        if (!t)
            return FALSE;
        if (!it || it == (ween_tvitem *)TVI_ROOT) {
            tree_free(t->root);
            t->root = NULL;
            t->sel = NULL;
            t->scroll_row = 0;
            t->scroll_x = 0;
        } else {
            /* unlink it from wherever it hangs, then free it and its children */
            ween_tvitem **link = it->parent ? &it->parent->child : &t->root;
            while (*link && *link != it)
                link = &(*link)->next;
            if (*link)
                *link = it->next;
            it->next = NULL;
            if (t->sel == it)
                t->sel = NULL;
            tree_free(it);
        }
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case TVM_GETNEXTITEM: {
        /* The three an app actually walks with: the root, a child, the one
         * that is selected. */
        ween_tvitem *it = (ween_tvitem *)lp;
        t = tree_of(wnd);
        if (!t)
            return 0;
        switch (wp) {
        case TVGN_ROOT:
            return (LRESULT)(UINT_PTR)t->root;
        case TVGN_CARET:
            return (LRESULT)(UINT_PTR)t->sel;
        case TVGN_CHILD:
            return (LRESULT)(UINT_PTR)(it ? it->child : t->root);
        case TVGN_NEXT:
            return (LRESULT)(UINT_PTR)(it ? it->next : NULL);
        case TVGN_PARENT:
            return (LRESULT)(UINT_PTR)(it ? it->parent : NULL);
        default:
            return 0;
        }
    }
    case TVM_GETITEMA: {
        /* An app that walks the tree needs the text back out of it. */
        TVITEMA *item = (TVITEMA *)lp;
        ween_tvitem *it = item ? (ween_tvitem *)item->hItem : NULL;
        if (!it)
            return FALSE;
        if ((item->mask & TVIF_TEXT) && item->pszText && item->cchTextMax > 0) {
            int n = (int)strlen(it->text ? it->text : "");
            if (n > item->cchTextMax - 1)
                n = item->cchTextMax - 1;
            memcpy(item->pszText, it->text ? it->text : "", (size_t)n);
            item->pszText[n] = 0;
        }
        if (item->mask & TVIF_IMAGE)
            item->iImage = it->image;
        if (item->mask & TVIF_STATE)
            item->state = (UINT)(it->state_img << 12);
        return TRUE;
    }
    case TVM_SETITEMA: {
        TVITEMA *item = (TVITEMA *)lp;
        ween_tvitem *it = item ? item->hItem : NULL;
        if (!it)
            return FALSE;
        if ((item->mask & TVIF_STATE) &&
            (item->stateMask & TVIS_STATEIMAGEMASK))
            it->state_img =
                (int)((item->state & TVIS_STATEIMAGEMASK) >> 12);
        if ((item->mask & TVIF_IMAGE))
            it->image = item->iImage;
        if ((item->mask & TVIF_TEXT) && item->pszText) {
            size_t n = strlen(item->pszText) + 1;
            char *copy = malloc(n);
            if (copy) {
                memcpy(copy, item->pszText, n);
                free(it->text);
                it->text = copy;
            }
        }
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case TVM_SETIMAGELIST:
        if ((t = tree_of(wnd))) {
            HIMAGELIST *slot = wp == TVSIL_STATE ? &t->state_images
                                                 : &t->images;
            HIMAGELIST was = *slot;
            *slot = (HIMAGELIST)lp;
            InvalidateRect(wnd, NULL, FALSE);
            return (LRESULT)(UINT_PTR)was;
        }
        return 0;
    case TVM_HITTEST: {
        TVHITTESTINFO *hi = (TVHITTESTINFO *)lp;
        RECT cr;
        int row, depth = 0, want, sb = ween_scroll_metric();
        ween_tvitem *hit;
        t = tree_of(wnd);
        if (!hi || !t)
            return 0;
        hi->hItem = NULL;
        hi->flags = 0;
        GetClientRect(wnd, &cr);
        if (hi->pt.x >= cr.right - sb && t->rows * WEEN_TV_ITEM_H > cr.bottom)
            return 0; /* the bar down the side is not an item */
        want = (hi->pt.y - WEEN_TV_TOP_MARGIN) / WEEN_TV_ITEM_H + t->scroll_row;
        row = 0;
        hit = tree_at_row(t->root, 0, want, &row, &depth);
        if (!hit)
            return 0;
        hi->hItem = hit;
        {   /* which part of it: the box that opens it, or the item itself —
             * the same column the button is drawn in */
            int bx = tree_button_x(wnd, depth, t->scroll_x);
            int x = hi->pt.x;
            hi->flags = (tree_has_button(wnd, hit, depth) && x >= bx &&
                         x < bx + WEEN_TV_BUTTON)
                            ? TVHT_ONITEMBUTTON
                            : TVHT_ONITEMLABEL;
        }
        return (LRESULT)(INT_PTR)hit;
    }
    case WM_LBUTTONDBLCLK: {
        /* The first click of the pair picked the item; the second opens it,
         * which for a tree means the branch under it — the same thing its
         * button does. The application hears about it as well, as it does
         * from a list. */
        TVHITTESTINFO hi;
        t = tree_of(wnd);
        memset(&hi, 0, sizeof(hi));
        hi.pt.x = GET_X_LPARAM(lp);
        hi.pt.y = GET_Y_LPARAM(lp);
        if (t && SendMessageA(wnd, TVM_HITTEST, 0, (LPARAM)&hi) && hi.hItem) {
            ween_tvitem *it = (ween_tvitem *)hi.hItem;
            if (!(hi.flags & TVHT_ONITEMBUTTON) && (it->child || it->cchildren))
                tree_expand(wnd, it, !it->expanded);
            InvalidateRect(wnd, NULL, FALSE);
        }
        notify_parent(wnd, NM_DBLCLK);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        /* A press of the right button picks the item under it, the way the
         * shell does, so the menu that follows is about that item. */
        TVHITTESTINFO hi;
        t = tree_of(wnd);
        SetFocus(wnd);
        memset(&hi, 0, sizeof(hi));
        hi.pt.x = GET_X_LPARAM(lp);
        hi.pt.y = GET_Y_LPARAM(lp);
        if (t && SendMessageA(wnd, TVM_HITTEST, 0, (LPARAM)&hi) && hi.hItem &&
            t->sel != (ween_tvitem *)hi.hItem) {
            t->sel = (ween_tvitem *)hi.hItem;
            InvalidateRect(wnd, NULL, FALSE);
            notify_parent(wnd, TVN_SELCHANGEDA);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        /* The arrows walk the visible rows; left and right close and open a
         * branch, which is what a tree does everywhere in Windows. */
        int row = 0, depth = 0, at, rows;
        ween_tvitem *next = NULL;
        t = tree_of(wnd);
        if (!t || !t->root)
            return 0;
        rows = tree_rows(t->root);
        at = t->sel ? tree_row_of(t->root, t->sel, &row) : -1;
        switch (wp) {
        case VK_DOWN:
            row = 0;
            next = tree_at_row(t->root, 0, at + 1 < rows ? at + 1 : at, &row,
                               &depth);
            break;
        case VK_UP:
            row = 0;
            next = tree_at_row(t->root, 0, at > 0 ? at - 1 : 0, &row, &depth);
            break;
        case VK_HOME:
            row = 0;
            next = tree_at_row(t->root, 0, 0, &row, &depth);
            break;
        case VK_END:
            row = 0;
            next = tree_at_row(t->root, 0, rows - 1, &row, &depth);
            break;
        case VK_RIGHT:
            if (t->sel && !t->sel->expanded && (t->sel->child ||
                                                t->sel->cchildren))
                tree_expand(wnd, t->sel, 1);
            else if (t->sel && t->sel->child)
                next = t->sel->child;
            break;
        case VK_LEFT:
            if (t->sel && t->sel->expanded)
                tree_expand(wnd, t->sel, 0);
            else if (t->sel && t->sel->parent)
                next = t->sel->parent;
            break;
        default:
            return DefWindowProcA(wnd, msg, wp, lp);
        }
        ween_ui_focus_cues = 1; /* the keyboard has been used, so it shows */
        if (next && next != t->sel) {
            t->sel = next;
            notify_parent(wnd, TVN_SELCHANGEDA);
        }
        /* keep it in view, which is the point of moving it */
        {
            RECT cr;
            int r2 = 0, visible;
            GetClientRect(wnd, &cr);
            visible = cr.bottom / WEEN_TV_ITEM_H;
            at = t->sel ? tree_row_of(t->root, t->sel, &r2) : 0;
            if (at < t->scroll_row)
                t->scroll_row = at;
            else if (at >= t->scroll_row + visible)
                t->scroll_row = at - visible + 1;
        }
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case TVM_EXPAND:
        tree_expand(wnd, (ween_tvitem *)lp, (wp & TVE_EXPAND) != 0);
        return TRUE;
    case WM_DESTROY:
        if (wnd->ctl) {
            tree_ctl_free(wnd->ctl);
            wnd->ctl = NULL;
        }
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the list view, in report mode ---------------------------------------
 *
 * A header of raised buttons over rows of text, one column per header item. */

#define WEEN_LV_HEADER_H 17

/* How tall the heading strip is on this view: none, when it was told to do
 * without one. */
static int lv_header_h(HWND wnd)
{
    return (wnd->style & LVS_NOCOLUMNHEADER) ? 0 : WEEN_LV_HEADER_H;
}
/* The two rows of window a list keeps between its header and its first item.
 * Windows leaves them; without them every row sits two pixels high. */
#define WEEN_LV_ROW_TOP 2

/* How many columns a list view will hold. Windows has no such cap; this is
 * one, and it is here so that raising it is one number. */
#define WEEN_LV_COLS 8

typedef struct {
    char *text[WEEN_LV_COLS];
    int image;    /* index into the view's image list, -1 for none */
    int cut;      /* LVIS_CUT: drawn ghosted, which is how a hidden file looks */
    int selected; /* LVIS_SELECTED, per row: a list without LVS_SINGLESEL can
                   * have any number of them, which is what Select All is for */
    int state_img; /* the row's state picture: 0 none, 1 a box, 2 a ticked box.
                    * A row with none is a heading, which is how a list of
                    * settings groups them. */
} ween_lvrow;

typedef struct {
    HIMAGELIST images; /* the small ones, which every view but one draws */
    HIMAGELIST big;    /* and the big ones the icon view draws */
    char *col[WEEN_LV_COLS];
    int width[WEEN_LV_COLS], fmt[WEEN_LV_COLS], ncol;
    ween_lvrow *row;
    int nrow, caprow;
    int sel;      /* 1-based: the row last picked, which a shell shows in its
                   * status bar and which Shift extends from. What is drawn
                   * selected is each row's own flag. */
    int focus;    /* 1-based: the row an arrow key moves from, which outlives
                   * the selection — clicking a file's size clears the one and
                   * leaves the other where it was */
    int top;      /* the first row drawn: a file list has to scroll */
    int pressed;  /* the header column being held down, -1 for none */
    int drag_col; /* the heading being carried to another place, -1 for none */
    int drag_x0;  /* where that press landed, to tell a drag from a click */
    int drag_x;   /* where the pointer is now, so the ghost follows it */
    int drag_grab; /* how far into the heading it was taken hold of */
    int drop_at;  /* the place it would go: the column it would sit before */
    int icon_col; /* the column the item's own picture and name are in: the
                   * first one until a heading is carried somewhere else, and
                   * then wherever that one went — the machine's icons travel
                   * with the Name column */
    int sizing;   /* the divider being dragged, -1 for none */
    int size_x0;  /* where the drag started, and the width it started at */
    int size_w0;
    int scroll_x; /* how far the columns are scrolled left, in pixels */
    HWND header;  /* the header control, once something has asked for it */
    HWND edit;    /* the box a label is being typed over in, or NULL */
    int editing;  /* which row that is */
    DWORD exstyle; /* LVS_EX_*: what it was asked for after it was made */
} ween_list;

/* The box drawn before a row when the view has check boxes, and the room it
 * takes. Thirteen is what a check box is everywhere else. */
#define WEEN_LV_CHECK 13
#define WEEN_LV_CHECK_W 16

static int lv_check_w(const ween_list *l)
{
    return l && (l->exstyle & LVS_EX_CHECKBOXES) ? WEEN_LV_CHECK_W : 0;
}

/* How far a heading must be carried before it is a drag rather than a press:
 * win32's SM_CXDRAG, which is four. */
#define WEEN_LV_DRAG 4

/* How near a header divider counts as being on it. Windows uses about this,
 * and it has to be wide enough to hit without being so wide that clicking the
 * column to sort it becomes hard. */
#define WEEN_LV_DIVIDER 4

static void list_ctl_free(void *p);

static ween_list *list_of(HWND w)
{
    if (!w->ctl) {
        w->ctl = calloc(1, sizeof(ween_list));
        w->ctl_free = list_ctl_free;
        if (w->ctl) {
            ((ween_list *)w->ctl)->pressed = -1;
            ((ween_list *)w->ctl)->sizing = -1;
            ((ween_list *)w->ctl)->drag_col = -1;
        }
    }
    return w->ctl;
}

/* ---- the four views -------------------------------------------------------
 *
 * A folder is shown one of four ways, and the low two bits of the style say
 * which. Details is rows with columns; List is a column of names that wraps
 * into another column when it runs out of height; Small Icons is the same
 * items flowing across instead of down; and Icons is a grid of big pictures
 * with the name under each. All the metrics here are measured off the
 * machine.
 */
#define WEEN_LV_FLOW_H 18     /* a row of List or Small Icons */
#define WEEN_LV_FLOW_PAD 10   /* and what follows the widest name in a cell */
#define WEEN_LV_ICON_W 75     /* an Icons cell, and the picture in it */
#define WEEN_LV_ICON_SIZE 32
#define WEEN_LV_ICON_TOP 4    /* the picture sits this far down its cell */
#define WEEN_LV_ICON_GAP 3    /* and the name this far under the picture */

static int lv_mode(const struct ween_wnd *wnd)
{
    return (int)(wnd->style & LVS_TYPEMASK);
}

/* A row is as tall as the tallest thing in it and one more: sixteen for a
 * small icon makes seventeen, which is what the shell's list has, and a list
 * with no images falls back to the font's own cell. Windows sizes a report
 * row this way rather than fixing it, which is why a list of files and a list
 * of plain strings are not the same height. */
static int lv_item_h(HWND wnd, const ween_list *l)
{
    const ween_strike *f = wnd && wnd->font ? wnd->font : ween_gui_font();
    int th = f ? f->ascent - f->descent : 13;
    int icon_w = 0, icon_h = 0;
    if (l && l->images)
        ImageList_GetIconSize(l->images, &icon_w, &icon_h);
    /* A view with boxes keeps a sixteen-square cell for the state picture,
     * the way comctl32's state image list is sixteen square with the
     * thirteen-pixel box in the middle of it. That is what makes the
     * machine's rows seventeen where a row of plain text would be fifteen. */
    if (l && (l->exstyle & LVS_EX_CHECKBOXES) && icon_h < WEEN_LV_CHECK_W)
        icon_h = WEEN_LV_CHECK_W;
    return (icon_h > th ? icon_h : th) + 1;
}

/* Selecting rows. A list without LVS_SINGLESEL can have any number picked at
 * once — Ctrl adds one, Shift takes the run from the anchor, and everything
 * else picks one and drops the rest. */
static void lv_select_none(ween_list *l)
{
    for (int i = 0; i < l->nrow; i++)
        l->row[i].selected = 0;
}

static void lv_select_one(ween_list *l, int row)
{
    lv_select_none(l);
    if (row >= 0 && row < l->nrow)
        l->row[row].selected = 1;
    l->sel = row + 1;
}

static void lv_select_range(ween_list *l, int from, int to)
{
    int a = from < to ? from : to, b = from < to ? to : from;
    lv_select_none(l);
    for (int i = a; i <= b; i++)
        if (i >= 0 && i < l->nrow)
            l->row[i].selected = 1;
}

/* How many are picked, and the first of them. */
static int lv_selected_count(const ween_list *l)
{
    int n = 0;
    for (int i = 0; i < l->nrow; i++)
        n += l->row[i].selected != 0;
    return n;
}

/* Columns, and every row's cells: the list view owns all of those strings. */
static void list_ctl_free(void *p)
{
    ween_list *l = p;
    for (int c = 0; c < WEEN_LV_COLS; c++)
        free(l->col[c]);
    for (int i = 0; i < l->nrow; i++)
        for (int c = 0; c < WEEN_LV_COLS; c++)
            free(l->row[i].text[c]);
    free(l->row);
    free(l);
}

/* How many rows fit under the header, and how far down the list can go. */
/* How wide the columns come to, which is what there is to scroll sideways. */
/* The widest name in the list, which is what a flowing view's cell is sized
 * to: every cell the same, as Windows makes them. */
static int lv_widest_label(HWND wnd, const ween_list *l)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int w = 0;
    for (int i = 0; f && i < l->nrow; i++) {
        const char *t = l->row[i].text[0];
        int tw = t ? ween_strike_text_width(f, t, (int)strlen(t)) : 0;
        if (tw > w)
            w = tw;
    }
    return w;
}

/* One cell of List or Small Icons: the picture, a gap, the widest name and
 * what follows it. */
static int lv_flow_cell_w(HWND wnd, const ween_list *l)
{
    int icon_w = 0, icon_h = 0;
    if (l->images)
        ImageList_GetIconSize(l->images, &icon_w, &icon_h);
    return icon_w + 2 + lv_widest_label(wnd, l) + WEEN_LV_FLOW_PAD;
}

/* How tall a cell of the Icons view is: the picture, the gap under it, and
 * two lines for the name, which is what a long one wraps to. */
static int lv_icon_cell_h(HWND wnd)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int cell = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 13;
    return WEEN_LV_ICON_TOP + WEEN_LV_ICON_SIZE + WEEN_LV_ICON_GAP + 2 * cell +
           WEEN_LV_ICON_TOP;
}

static int lv_content_w(const ween_list *l);

/* Where a row sits, in the view's own coordinates before scrolling: every
 * view works out from this, so what is drawn and what is clicked agree. */
static void lv_cell_rect(HWND wnd, const ween_list *l, int row, RECT *out)
{
    int mode = lv_mode(wnd);
    RECT cr;
    GetClientRect(wnd, &cr);
    if (mode == LVS_REPORT) {
        int ih = lv_item_h(wnd, l);
        out->left = 0;
        out->top = lv_header_h(wnd) + WEEN_LV_ROW_TOP + row * ih;
        out->right = lv_content_w(l);
        out->bottom = out->top + ih;
        return;
    }
    if (mode == LVS_ICON) {
        int cw = WEEN_LV_ICON_W, ch = lv_icon_cell_h(wnd);
        int per = cr.right / cw;
        if (per < 1)
            per = 1;
        out->left = (row % per) * cw;
        out->top = (row / per) * ch;
        out->right = out->left + cw;
        out->bottom = out->top + ch;
        return;
    }
    {   /* List goes down then across; Small Icons across then down */
        int cw = lv_flow_cell_w(wnd, l), ch = WEEN_LV_FLOW_H;
        int down = cr.bottom / ch, across = cr.right / cw;
        if (down < 1)
            down = 1;
        if (across < 1)
            across = 1;
        if (mode == LVS_LIST) {
            out->left = (row / down) * cw;
            out->top = (row % down) * ch;
        } else {
            out->left = (row % across) * cw;
            out->top = (row / across) * ch;
        }
        out->right = out->left + cw;
        out->bottom = out->top + ch;
    }
}

/* How wide everything is, taken together — what the horizontal bar scrolls. */
static int lv_content_w(const ween_list *l)
{
    int w = 0;
    for (int c = 0; l && c < l->ncol; c++)
        w += l->width[c];
    return w;
}

/* Which bars the view needs and what is left for the rows once they are
 * taken. Each bar takes a strip off the view, and taking one can bring the
 * other on — a list that just fits until its own scroll bar appears. */
typedef struct {
    int hbar, vbar;     /* whether each is shown */
    int view_w, view_h; /* what is left for the header and rows */
    int visible;        /* rows that fit in view_h */
} ween_lv_layout;

static ween_lv_layout lv_layout(HWND wnd, const ween_list *l)
{
    ween_lv_layout g;
    RECT cr;
    int sb = ween_scroll_metric(), content = lv_content_w(l);
    int ih = lv_item_h(wnd, l);
    int rows_h = (l ? l->nrow : 0) * ih + lv_header_h(wnd) + WEEN_LV_ROW_TOP;

    GetClientRect(wnd, &cr);
    g.hbar = content > cr.right;
    g.vbar = rows_h > cr.bottom - (g.hbar ? sb : 0);
    g.hbar = content > cr.right - (g.vbar ? sb : 0);
    g.view_w = cr.right - (g.vbar ? sb : 0);
    g.view_h = cr.bottom - (g.hbar ? sb : 0);
    g.visible = (g.view_h - lv_header_h(wnd) - WEEN_LV_ROW_TOP) / ih;
    if (g.visible < 1)
        g.visible = 1;
    return g;
}

static int lv_visible(HWND wnd)
{
    return lv_layout(wnd, list_of(wnd)).visible;
}

static int lv_max_top(HWND wnd, const ween_list *l)
{
    int over = l->nrow - lv_visible(wnd);
    return over > 0 ? over : 0;
}

/* The bar down the right, in the terms win32 states a scroll bar in: nMax is
 * the last item, not the last item you can scroll to. The two differ by a
 * page, and everything downstream — the thumb's size, the range a drag works
 * in — is derived from nMax - nPage + 1. Hand it the scrollable range instead
 * and it subtracts a page twice: the thumb comes out a page too long and, on
 * a short list, the range collapses to nothing and the bar will not move. */
static ween_sbstate lv_sbstate(HWND wnd, const ween_list *l)
{
    ween_sbstate st;
    st.pos = l->top;
    st.min = 0;
    st.max = l->nrow - 1;
    st.page = lv_visible(wnd);
    st.line = 1;
    return st;
}

static void lv_scroll_to(HWND wnd, ween_list *l, int top)
{
    int max = lv_max_top(wnd, l);
    if (top > max)
        top = max;
    if (top < 0)
        top = 0;
    if (top == l->top)
        return;
    l->top = top;
    InvalidateRect(wnd, NULL, FALSE);
}

/* The column whose right-hand divider is under x, or -1. */
static int lv_divider_at(const ween_list *l, int x, int y)
{
    int edge = 0;
    (void)y; /* the caller has already said this is in the heading strip */
    for (int c = 0; c < l->ncol; c++) {
        edge += l->width[c];
        if (x >= edge - WEEN_LV_DIVIDER && x <= edge + WEEN_LV_DIVIDER)
            return c;
    }
    return -1;
}

/* What a cell needs before its text: the first column's is the icon and the
 * two pixels its label box starts before the text, the rest sit six in. This
 * is the measure a column is sized by, not the pen the text is drawn with —
 * the pen is two further along in the first column. */
static int lv_cell_lead(int col, int icon_w)
{
    if (col)
        return 6;
    return icon_w ? icon_w + 2 : 7;
}

/* What LVSCW_AUTOSIZE comes to: the widest cell in the column, what comes
 * before it, and six after. Double-clicking a divider asks for this, which is
 * how a shell's column comes to fit the longest name in it. Measured against
 * the machine, whose C: window gives 140 for Name, 33 for Size and 104 for
 * Modified — the numbers this reproduces. */
#define WEEN_LV_AUTOSIZE_TRAIL 6
static int lv_autosize_width(HWND wnd, const ween_list *l, int col,
                             int use_header)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int icon_w = 0, icon_h = 0, best = 0;
    if (!f || col < 0 || col >= l->ncol)
        return 0;
    if (col == 0 && l->images)
        ImageList_GetIconSize(l->images, &icon_w, &icon_h);
    /* The drawn width, not the measured one: a column is sized to the pixels
     * the text puts on the screen, and the extent rounds every character up. */
    for (int i = 0; i < l->nrow; i++) {
        const char *t = l->row[i].text[col];
        int w = t ? ween_strike_text_width(f, t, (int)strlen(t)) : 0;
        if (w > best)
            best = w;
    }
    if (use_header && l->col[col]) {
        /* USEHEADER counts the heading as one more thing that has to fit */
        int w = ween_strike_text_width(f, l->col[col],
                                       (int)strlen(l->col[col]));
        if (w + 12 - lv_cell_lead(col, icon_w) > best)
            best = w + 12 - lv_cell_lead(col, icon_w);
    }
    if (!best)
        return 0;
    return lv_cell_lead(col, icon_w) + best + WEEN_LV_AUTOSIZE_TRAIL;
}

/* The width of a row's label box: the text, clamped to what the name column
 * leaves, with five pixels each side. The same number the painting uses, so
 * what can be clicked is exactly what is drawn highlighted. */
static int lv_label_w(HWND wnd, const ween_list *l, int row, int indent)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int tw;
    if (!f || !l->row[row].text[l->icon_col])
        return 0;
    tw = ween_strike_text_extent(f, l->row[row].text[l->icon_col],
                                 (int)strlen(l->row[row].text[l->icon_col]));
    if (tw > l->width[l->icon_col] - indent - 8)
        tw = l->width[l->icon_col] - indent - 8;
    /* Two before the text and four after, which is the box the machine
     * highlights: "WINNT" picked comes to forty-two pixels of blue. A list
     * with no images has no icon column to start after and keeps the wider
     * box, which is what wine's classic list draws. */
    return tw + (indent ? 6 : 10);
}

/* Where a row's label box starts: at the icon's right edge when there is one,
 * two in when there is not. */
static int lv_label_x(int indent)
{
    return indent ? indent : 2;
}

/* How far in the column the item's picture and name are drawn in starts. */
static int lv_icon_col_x(const ween_list *l)
{
    int x = 0;
    for (int c = 0; c < l->icon_col && c < l->ncol; c++)
        x += l->width[c];
    return x;
}

/* Where a heading being carried would land: the column it would sit before,
 * counted in the order they are in now. A point past the last one lands after
 * it, which is what dragging a column to the far right means. */
static int lv_drop_at(const ween_list *l, int x)
{
    int edge = 0;
    for (int c = 0; c < l->ncol; c++) {
        if (x < edge + l->width[c] / 2)
            return c;
        edge += l->width[c];
    }
    return l->ncol;
}

/* Move a column, and every row's cell with it: the order the headings are in
 * is the order the cells are in, so one cannot move without the other. */
static void lv_move_column(ween_list *l, int from, int to)
{
    char *col;
    int width, fmt;
    if (from < 0 || from >= l->ncol || to < 0 || to > l->ncol)
        return;
    if (to > from)
        to--; /* it is coming out of the run first, so what follows shifts */
    if (to == from)
        return;
    col = l->col[from];
    width = l->width[from];
    fmt = l->fmt[from];
    if (to < from)
        for (int c = from; c > to; c--) {
            l->col[c] = l->col[c - 1];
            l->width[c] = l->width[c - 1];
            l->fmt[c] = l->fmt[c - 1];
        }
    else
        for (int c = from; c < to; c++) {
            l->col[c] = l->col[c + 1];
            l->width[c] = l->width[c + 1];
            l->fmt[c] = l->fmt[c + 1];
        }
    l->col[to] = col;
    l->width[to] = width;
    l->fmt[to] = fmt;
    if (l->icon_col == from)
        l->icon_col = to;
    else if (from < l->icon_col && to >= l->icon_col)
        l->icon_col--;
    else if (from > l->icon_col && to <= l->icon_col)
        l->icon_col++;
    for (int r = 0; r < l->nrow; r++) {
        char *cell = l->row[r].text[from];
        if (to < from)
            for (int c = from; c > to; c--)
                l->row[r].text[c] = l->row[r].text[c - 1];
        else
            for (int c = from; c < to; c++)
                l->row[r].text[c] = l->row[r].text[c + 1];
        l->row[r].text[to] = cell;
    }
}

static const char *fit_text(const ween_strike *f, const char *text, int avail,
                            char *buf, size_t cap, int *out_len);

/* The bar that says where a carried heading would land: a dark blue, drawn
 * over everything including the heading being carried — which is where the
 * machine's shows it, and in this colour. */
#define WEEN_LV_DROPMARK WEEN_RGBX(64, 64, 191)

/* One heading, in its own place or wherever it is being carried to. */
static void lv_draw_heading(ween_surface *s, const ween_strike *f,
                            const ween_list *l, int c, int cx, int oy,
                            int down)
{
    char buf[256];
    ween_classic_edge(s, cx, oy, l->width[c], WEEN_LV_HEADER_H,
                      down ? EDGE_SUNKEN : EDGE_RAISED,
                      BF_RECT | BF_SOFT | BF_MIDDLE, NULL);
    if (f && l->col[c]) {
        int len, th = f->ascent - f->descent;
        const char *t = fit_text(f, l->col[c], l->width[c] - 12, buf,
                                 sizeof(buf), &len);
        int tx = cx + 6;
        if (l->fmt[c] & LVCFMT_RIGHT) /* the heading follows its cells */
            tx = cx + l->width[c] - 6 - ween_strike_text_width(f, t, len);
        ween_strike_draw(f, s, tx + (down ? 1 : 0),
                         oy + (WEEN_LV_HEADER_H - th) / 2 + (down ? 1 : 0), t,
                         len, WEEN_BLACK);
        /* and the sort arrow ten past the end of it */
        if (l->fmt[c] & (HDF_SORTUP | HDF_SORTDOWN))
            ween_classic_sort_arrow(
                s, tx + ween_strike_text_width(f, t, len) + 9 + (down ? 1 : 0),
                oy + 5 + (down ? 1 : 0), (l->fmt[c] & HDF_SORTUP) != 0);
    }
}

/* Half way into the shadow colour, pixel by pixel: what a carried heading
 * is drawn as, and what its every colour on the machine comes out as. */
static void lv_shade_rect(ween_surface *s, int x, int y, int w, int h)
{
    for (int iy = y; iy < y + h; iy++)
        for (int ix = x; ix < x + w; ix++) {
            ween_color p, out = 0;
            if (ix < s->clip_x || ix >= s->clip_r || iy < s->clip_y ||
                iy >= s->clip_b)
                continue;
            p = s->px[(size_t)iy * s->w + ix];
            for (int sh = 0; sh < 24; sh += 8)
                out |= ((((p >> sh) & 0xff) + 128) / 2) << sh;
            ween_surface_pixel(s, ix, iy, out);
        }
}

/* Which row a point picks, and what part of it. A report-view row is only its
 * icon and its label: the cells to the right of the name are background, which
 * is why clicking a file's size clears the selection rather than picking the
 * file, and why a right click there brings up the folder's menu instead of the
 * file's. Returns -1 for a point on nothing. */
static int lv_item_hit(HWND wnd, ween_list *l, int x, int y, UINT *flags)
{
    ween_lv_layout g;
    int icon_w = 0, icon_h = 0, indent, row;
    if (flags)
        *flags = LVHT_NOWHERE;
    if (lv_mode(wnd) != LVS_REPORT) {
        /* the cell a point is in, and then whether it is on what is drawn in
         * it rather than on the space around it */
        for (int i = 0; i < l->nrow; i++) {
            RECT c;
            lv_cell_rect(wnd, l, i, &c);
            if (x < c.left || x >= c.right || y < c.top || y >= c.bottom)
                continue;
            if (flags)
                *flags = LVHT_ONITEMLABEL;
            return i;
        }
        return -1;
    }
    g = lv_layout(wnd, l);
    if (y < lv_header_h(wnd) || (g.vbar && x >= g.view_w) ||
        (g.hbar && y >= g.view_h))
        return -1;
    row = (y - lv_header_h(wnd) - WEEN_LV_ROW_TOP) / lv_item_h(wnd, l) +
          l->top;
    if (row < 0 || row >= l->nrow)
        return -1;
    if (l->images)
        ImageList_GetIconSize(l->images, &icon_w, &icon_h);
    indent = (l->images && l->row[row].image >= 0) ? icon_w + 2 : 0;
    x += l->scroll_x;
    /* the item's picture and name are in whichever column they were left in,
     * so everything about them is measured from where that column starts */
    x -= lv_icon_col_x(l);
    if (x < 2)
        return (l->exstyle & LVS_EX_FULLROWSELECT) ? row : -1;
    if (lv_check_w(l)) { /* the box before everything else */
        /* the whole state column answers for the box in it, not just the
         * thirteen pixels the box itself is drawn on */
        if (x < 2 + lv_check_w(l)) {
            if (flags)
                *flags = LVHT_ONITEMSTATEICON;
            return row;
        }
        /* and the label starts where the paint puts it: two past the box
         * when the box is all that comes before it */
        indent += lv_check_w(l) + (indent ? 0 : 2);
    }
    if (l->images && l->row[row].image >= 0 &&
        x < 2 + lv_check_w(l) + icon_w) {
        if (flags)
            *flags = LVHT_ONITEMICON;
        return row;
    }
    if (x >= lv_label_x(indent) &&
        x < lv_label_x(indent) + lv_label_w(wnd, l, row, indent)) {
        if (flags)
            *flags = LVHT_ONITEMLABEL;
        return row;
    }
    /* LVS_EX_FULLROWSELECT: the row is what is picked, so the whole width of
     * it answers — a press on a cell to the right of the name picks the row
     * it is in rather than falling through to the background. */
    if ((l->exstyle & LVS_EX_FULLROWSELECT) &&
        x + lv_icon_col_x(l) < lv_content_w(l)) {
        if (flags)
            *flags = LVHT_ONITEMLABEL;
        return row;
    }
    return -1;
}

/* As much of `text` as fits in `avail` pixels, with an ellipsis on the end
 * when it had to be cut — win32's DT_END_ELLIPSIS, which is what a list view
 * does to a name too long for its column. Without it a long name simply runs
 * on and writes over whatever the next column had to say. */
static const char *fit_text(const ween_strike *f, const char *text, int avail,
                            char *buf, size_t bufsz, int *len)
{
    int n = (int)strlen(text), dots;
    *len = n;
    if (!f || avail <= 0 || ween_strike_text_width(f, text, n) <= avail)
        return text;
    dots = ween_strike_text_width(f, "...", 3);
    while (n > 0 && ween_strike_text_width(f, text, n) + dots > avail)
        n--;
    if (n > (int)bufsz - 4)
        n = (int)bufsz - 4;
    if (n < 0)
        n = 0;
    memcpy(buf, text, (size_t)n);
    memcpy(buf + n, "...", 4);
    *len = n + 3;
    return buf;
}

/* The three views that are not Details: each item is a cell, and what is in
 * the cell is a picture and a name — beside each other, or the name under the
 * picture when the pictures are the big ones. */
static void lv_paint_flow(HWND wnd, ween_list *l, HDC dc)
{
    struct ween_wnd *top = ween_top_level(wnd);
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int mode = lv_mode(wnd);
    int icons = mode == LVS_ICON;
    HIMAGELIST images = icons && l->big ? l->big : l->images;
    int cell = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 13;
    int th = f ? f->ascent - f->descent : 13;
    int icon_w = 0, icon_h = 0, ox, oy;
    int focused = ween_focus_get() == wnd;
    int sel_state = focused                            ? 2
                    : (wnd->style & LVS_SHOWSELALWAYS) ? 1
                                                       : 0;
    RECT clip;
    ween_client_origin(wnd, &ox, &oy);
    if (images)
        ImageList_GetIconSize(images, &icon_w, &icon_h);
    ween_surface_get_clip(&top->surface, &clip);
    for (int i = 0; i < l->nrow; i++) {
        RECT c;
        const char *t = l->row[i].text[0] ? l->row[i].text[0] : "";
        int len = (int)strlen(t);
        int tw = f ? ween_strike_text_width(f, t, len) : 0;
        int selected = l->row[i].selected && sel_state;
        ween_color bar = sel_state == 2 ? WEEN_CAP_LEFT : WEEN_FACE;
        lv_cell_rect(wnd, l, i, &c);
        if (icons) {
            /* the picture centred over the name, and the name centred under
             * it — wrapped to a second line when it will not fit */
            int ix = ox + c.left + (WEEN_LV_ICON_W - icon_w) / 2;
            int iy = oy + c.top + WEEN_LV_ICON_TOP;
            int ty = iy + icon_h + WEEN_LV_ICON_GAP;
            int room = WEEN_LV_ICON_W - 8, used = 0, line = 0;
            if (images && l->row[i].image >= 0)
                ween_imagelist_draw(images, l->row[i].image, &top->surface, ix,
                                    iy);
            while (used < len && line < 2 && f) {
                int fit = len - used, w;
                while (fit > 0 &&
                       (w = ween_strike_text_width(f, t + used, fit)) > room)
                    fit--;
                if (fit <= 0)
                    break;
                if (line == 0 && used + fit < len) {
                    int back = fit; /* break at a space where there is one */
                    while (back > 0 && t[used + back] != ' ')
                        back--;
                    if (back > 0)
                        fit = back;
                }
                w = ween_strike_text_width(f, t + used, fit);
                {
                    int lx = ox + c.left + (WEEN_LV_ICON_W - w) / 2;
                    if (selected)
                        ween_surface_fill(&top->surface, lx - 1, ty, w + 2,
                                          cell, bar);
                    ween_strike_draw(f, &top->surface, lx, ty, t + used, fit,
                                     selected && sel_state == 2 ? WEEN_WHITE
                                                                : WEEN_BLACK);
                }
                used += fit;
                while (used < len && t[used] == ' ')
                    used++;
                ty += cell;
                line++;
            }
        } else {
            /* the picture, then the name beside it */
            int ix = ox + c.left, iy = oy + c.top + (WEEN_LV_FLOW_H - icon_h) / 2;
            int tx = ix + icon_w + 2;
            int ty = oy + c.top + (WEEN_LV_FLOW_H - th) / 2;
            if (images && l->row[i].image >= 0)
                ween_imagelist_draw(images, l->row[i].image, &top->surface, ix,
                                    iy);
            if (selected)
                ween_surface_fill(&top->surface, tx - 1, ty, tw + 3, th, bar);
            if (f)
                ween_strike_draw(f, &top->surface, tx, ty, t, len,
                                 selected && sel_state == 2 ? WEEN_WHITE
                                                            : WEEN_BLACK);
        }
        if (ween_ui_focus_cues && focused && i == l->focus - 1) {
            if (icons)
                ween_surface_focus_rect(&top->surface, ox + c.left,
                                        oy + c.top, WEEN_LV_ICON_W,
                                        c.bottom - c.top);
            else
                ween_surface_focus_rect(&top->surface,
                                        ox + c.left + icon_w + 1,
                                        oy + c.top +
                                            (WEEN_LV_FLOW_H - th) / 2,
                                        tw + 3, th);
        }
    }
    (void)dc;
}

static void listview_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_list *l = list_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT r = ps->rcPaint, clip;
    int ox, oy, x, sx;
    int icon_w = 0, icon_h = 0, ih;
    ween_lv_layout g;
    char buf[260];

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
    if (!l)
        return;
    if (lv_mode(wnd) != LVS_REPORT) { /* one of the three that flow */
        lv_paint_flow(wnd, l, dc);
        return; /* and none of them has a header: that band is Details' own */
    }
    ih = lv_item_h(wnd, l);

    g = lv_layout(wnd, l);
    if (!g.hbar)
        l->scroll_x = 0;
    else if (l->scroll_x > lv_content_w(l) - g.view_w)
        l->scroll_x = lv_content_w(l) - g.view_w;
    if (l->scroll_x < 0)
        l->scroll_x = 0;
    sx = l->scroll_x;
    if (l->images)
        ImageList_GetIconSize(l->images, &icon_w, &icon_h);

    /* Everything that scrolls is drawn through a clip the width and height of
     * what is left once the bars have taken their strips, so a wide column or
     * a long name stops at the edge instead of running over them. */
    ween_surface_get_clip(&top->surface, &clip);
    ween_surface_clip(&top->surface, clip.left, clip.top,
                      (ox + g.view_w < clip.right ? ox + g.view_w : clip.right) -
                          clip.left,
                      (oy + g.view_h < clip.bottom ? oy + g.view_h
                                                   : clip.bottom) -
                          clip.top);

    x = 0;
    for (int c = 0; lv_header_h(wnd) && c < l->ncol; c++) {
        lv_draw_heading(&top->surface, f, l, c, ox + x - sx, oy,
                        c == l->pressed);
        x += l->width[c];
    }
    /* A heading being carried to another place: where it would land, and the
     * heading itself under the pointer. Both are the machine's — a two-pixel
     * blue bar at the boundary it would go to, and the heading drawn again
     * half way into the shadow colour, which is what makes it a ghost. */
    if (lv_header_h(wnd) && l->drag_col >= 0) {
        int edge = 0, gx = ox + l->drag_x - sx - l->drag_grab;
        for (int c = 0; c < l->drop_at && c < l->ncol; c++)
            edge += l->width[c];
        lv_draw_heading(&top->surface, f, l, l->drag_col, gx, oy, 0);
        lv_shade_rect(&top->surface, gx, oy, l->width[l->drag_col],
                      WEEN_LV_HEADER_H);
        ween_surface_fill(&top->surface, ox + edge - sx + 1, oy, 2,
                          WEEN_LV_HEADER_H, WEEN_LV_DROPMARK);
    }

    /* A view that has lost the focus keeps its selection, in grey rather than
     * in the highlight, when the style says to show it always — and drops it
     * altogether when the style does not. The caret goes with the focus in
     * either case: an unfocused list has no caret to move. */
    int focused = ween_focus_get() == wnd;
    int fullrow = (l->exstyle & LVS_EX_FULLROWSELECT) != 0;
    int sel_state = focused                            ? 2
                    : (wnd->style & LVS_SHOWSELALWAYS) ? 1
                                                       : 0;
    /* One past what fits, when what fits leaves a gap: a view whose rows do
     * not divide its height draws the next one cut off at the bottom, which
     * is what says there is more below. Only paint counts it — a page still
     * moves by whole rows. */
    int painted = g.visible +
                  ((g.view_h - lv_header_h(wnd) - WEEN_LV_ROW_TOP) % ih ? 1
                                                                        : 0);
    for (int i = l->top; i < l->nrow && i < l->top + painted; i++) {
        int y = oy + lv_header_h(wnd) + WEEN_LV_ROW_TOP + (i - l->top) * ih;
        int selected = l->row[i].selected && sel_state;
        /* The caret is drawn on the row the arrows would move from, selected
         * or not — but only once the keyboard has been used, which is the same
         * rule that keeps a menu's underlines hidden until then. */
        int caret = ween_ui_focus_cues && focused && i == l->focus - 1;
        int box = lv_check_w(l);
        int has_image = l->images && l->row[i].image >= 0;
        int icx = lv_icon_col_x(l); /* where the item's own column starts */
        /* Where the label column starts. A row with a tick box and no
         * picture starts two past the box, the same two an icon leaves
         * between itself and the name — the box stands in for the icon. */
        int indent = has_image ? box + icon_w + 2 : box ? box + 2 : 0;
        x = 0;
        if (box && l->row[i].state_img) {
            int by = y + (ih - WEEN_LV_CHECK) / 2;
            ween_classic_check_flat(&top->surface, ox - sx + icx + 4, by,
                                    l->row[i].state_img == 2);
        }
        /* LVS_EX_FULLROWSELECT: the row is picked, not the name in it, so
         * the highlight runs the whole width of the columns and every cell's
         * text goes with it. The machine's file-type list is one of these. */
        if (fullrow && (selected || caret) && f) {
            int rw = lv_content_w(l);
            if (selected)
                ween_surface_fill(&top->surface, ox - sx, y, rw, ih,
                                  sel_state == 2 ? WEEN_CAP_LEFT : WEEN_FACE);
            if (caret)
                ween_surface_focus_rect(&top->surface, ox - sx, y, rw, ih);
        } else if ((selected || caret) && f && l->row[i].text[l->icon_col]) {
            /* the label box: the text inflated five pixels each side */
            int lw = lv_label_w(wnd, l, i, indent);
            int lx = ox - sx + icx + lv_label_x(indent);
            if (selected)
                ween_surface_fill(&top->surface, lx, y, lw, ih,
                                  sel_state == 2 ? WEEN_CAP_LEFT : WEEN_FACE);
            if (caret)
                ween_surface_focus_rect(&top->surface, lx, y, lw, ih);
        }
        if (has_image) {
            /* A picked row's picture is drawn as it is — the machine washes
             * nothing into it. A cut one — which is how the shell shows a
             * hidden file — goes half way into the window's own colour. */
            if (l->row[i].cut)
                ween_imagelist_draw_blend(l->images, l->row[i].image,
                                          &top->surface, ox - sx + icx + 2 + box,
                                          y + (ih - icon_h) / 2, WEEN_WHITE);
            else
                ween_imagelist_draw(l->images, l->row[i].image, &top->surface,
                                    ox - sx + icx + 2 + box,
                                    y + (ih - icon_h) / 2);
        }
        for (int c = 0; c < l->ncol; c++) {
            /* the first column leaves room for an icon; the rest sit closer */
            /* the name sits two past its box, which starts where the icon
             * ends; the other cells sit closer to their own column */
            int lead = c != l->icon_col
                           ? 6
                           : (has_image || box ? indent + 2 : indent + 7);
            if (f && l->row[i].text[c]) {
                int len;
                const char *t = fit_text(f, l->row[i].text[c],
                                         l->width[c] - lead - 3, buf,
                                         sizeof(buf), &len);
                if (l->fmt[c] & LVCFMT_RIGHT) /* six short of its own edge */
                    lead = l->width[c] - 6 - ween_strike_text_width(f, t, len);
                /* two below the row's top, not one: the same lopsided
                 * centring the rest of the shell's text has */
                ween_strike_draw(f, &top->surface, ox + x - sx + lead, y + 2,
                                 t, len,
                                 selected && sel_state == 2 &&
                                         (!c || fullrow)
                                     ? WEEN_WHITE
                                     : WEEN_BLACK);
            }
            x += l->width[c];
        }
    }

    ween_surface_clip(&top->surface, clip.left, clip.top,
                      clip.right - clip.left, clip.bottom - clip.top);

    if (g.vbar) {
        ween_sbstate st = lv_sbstate(wnd, l);
        ween_draw_scrollbar(&top->surface, ox + g.view_w, oy,
                            ween_scroll_metric(), g.view_h, 1, 1, st.pos,
                            st.page, st.min, st.max);
    }
    if (g.hbar)
        ween_draw_scrollbar(&top->surface, ox, oy + g.view_h, g.view_w,
                            ween_scroll_metric(), 0, 1, l->scroll_x, g.view_w,
                            0, lv_content_w(l) - 1);
    if (g.hbar && g.vbar) /* the dead square where they meet */
        ween_surface_fill(&top->surface, ox + g.view_w, oy + g.view_h,
                          ween_scroll_metric(), ween_scroll_metric(),
                          WEEN_FACE);
}

static char *dup_str(const char *src)
{
    size_t n = strlen(src ? src : "") + 1;
    char *copy = malloc(n);
    if (copy)
        memcpy(copy, src ? src : "", n);
    return copy;
}

void ween_listview_view(HWND w, ween_lv_view *out)
{
    ween_list *l = w ? list_of(w) : NULL;
    memset(out, 0, sizeof(*out));
    if (!l)
        return;
    out->top = l->top;
    out->sel = l->sel;
    out->count = l->nrow;
    out->visible = lv_visible(w);
    out->max_top = lv_max_top(w, l);
}

/* ---- typing over a label --------------------------------------------------
 *
 * A row's label is edited in place: an EDIT is put exactly where the label is
 * drawn, with the name in it and selected. Enter takes it, Escape drops it,
 * and clicking away takes it as well. The application hears LVN_ENDLABELEDIT
 * and can refuse.
 */
/* The box a label is typed over in is an EDIT with the view's own procedure
 * in front of it: Enter and Escape are the view's answers, not the box's, and
 * everything else is the box's as usual. This is how comctl32 does it, and
 * what SetWindowLongPtr(GWLP_WNDPROC) is for. */
static WNDPROC g_lv_edit_proc;

static void lv_end_edit(HWND wnd, ween_list *l, int keep);

static LRESULT CALLBACK lv_edit_proc(HWND box, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        HWND view = box->parent;
        ween_list *l = view ? list_of(view) : NULL;
        if (l && l->edit == box) {
            lv_end_edit(view, l, wp == VK_RETURN);
            return 0;
        }
    }
    return CallWindowProcA(g_lv_edit_proc, box, msg, wp, lp);
}

static void lv_end_edit(HWND wnd, ween_list *l, int keep)
{
    char text[260];
    HWND box = l->edit;
    int row = l->editing;
    if (!box)
        return;
    l->edit = NULL; /* cleared first: destroying the box comes back through */
    l->editing = -1;
    GetWindowTextA(box, text, (int)sizeof(text));
    DestroyWindow(box);
    if (keep && text[0] && row >= 0 && row < l->nrow) {
        if (notify_disp(wnd, LVN_ENDLABELEDITA, row, text)) {
            free(l->row[row].text[0]);
            l->row[row].text[0] = dup_str(text);
        }
    } else {
        notify_disp(wnd, LVN_ENDLABELEDITA, row, NULL); /* abandoned */
    }
    InvalidateRect(wnd, NULL, FALSE);
    SetFocus(wnd);
}

static HWND lv_begin_edit(HWND wnd, ween_list *l, int row)
{
    RECT r;
    int icon_w = 0, icon_h = 0, indent;
    if (row < 0 || row >= l->nrow)
        return NULL;
    lv_end_edit(wnd, l, 1); /* one at a time */
    if (notify_disp(wnd, LVN_BEGINLABELEDITA, row, l->row[row].text[0]))
        return NULL; /* the application said no */
    if (l->images)
        ImageList_GetIconSize(l->images, &icon_w, &icon_h);
    indent = (l->images && l->row[row].image >= 0) ? icon_w + 2 : 0;
    r.left = lv_label_x(indent) - l->scroll_x - 2;
    r.top = lv_header_h(wnd) + WEEN_LV_ROW_TOP +
            (row - l->top) * lv_item_h(wnd, l) - 1;
    r.right = r.left + lv_label_w(wnd, l, row, indent) + 24;
    r.bottom = r.top + lv_item_h(wnd, l) + 2;
    l->edit = CreateWindowExA(0, "EDIT", l->row[row].text[0] ? l->row[row].text[0] : "",
                              WS_CHILD | WS_VISIBLE | WS_BORDER, r.left, r.top,
                              r.right - r.left, r.bottom - r.top, wnd, NULL,
                              NULL, NULL);
    if (!l->edit)
        return NULL;
    /* in front of the box: Enter and Escape are the view's */
    g_lv_edit_proc = (WNDPROC)SetWindowLongPtrA(l->edit, GWLP_WNDPROC,
                                                (LONG_PTR)lv_edit_proc);
    l->editing = row;
    SendMessageA(l->edit, WM_SETFONT, (WPARAM)0, FALSE);
    SendMessageA(l->edit, EM_SETSEL, 0, -1); /* all of it, ready to type over */
    SetFocus(l->edit);
    return l->edit;
}

static LRESULT listview_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_list *l;
    switch (msg) {
    case LVM_EDITLABELA:
        l = list_of(wnd);
        return l ? (LRESULT)(INT_PTR)lv_begin_edit(wnd, l, (int)wp) : 0;
    case LVM_GETEDITCONTROL:
        l = list_of(wnd);
        return l ? (LRESULT)(INT_PTR)l->edit : 0;

    case WM_SIZE:
        /* the header stands across the band, so it follows the width */
        l = list_of(wnd);
        if (l && l->header) {
            RECT cr;
            GetClientRect(wnd, &cr);
            MoveWindow(l->header, 0, 0, cr.right, WEEN_LV_HEADER_H, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        listview_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        l = list_of(wnd);
        if (l) /* three rows a notch, as every list does */
            lv_scroll_to(wnd, l,
                         l->top - 3 * (GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA));
        return 0;
    }
    case WM_KEYDOWN:
        l = list_of(wnd);
        if (!l)
            return 0;
        /* An arrow starts from the caret rather than from the selection: a
         * click on a row's size cell drops the selection but leaves the caret
         * on that row, and the next arrow moves from there. */
        if (wp == VK_SPACE && (l->exstyle & LVS_EX_CHECKBOXES) && l->focus &&
            l->row[l->focus - 1].state_img) {
            /* Space turns the box on the row the caret is on, which is how a
             * list of things to tick is worked without the mouse. */
            int i = l->focus - 1;
            l->row[i].state_img = l->row[i].state_img == 2 ? 1 : 2;
            InvalidateRect(wnd, NULL, FALSE);
            notify_parent(wnd, LVN_ITEMCHANGED);
            return 0;
        }
        {
        int anchor = l->sel ? l->sel : l->focus;
        l->sel = l->focus;
        switch (wp) {
        case VK_DOWN:
            if (l->sel < l->nrow)
                l->sel++;
            break;
        case VK_UP:
            if (l->sel > 1)
                l->sel--;
            break;
        case VK_HOME:
            l->sel = l->nrow ? 1 : 0;
            break;
        case VK_END:
            l->sel = l->nrow;
            break;
        case VK_NEXT:
            l->sel += lv_visible(wnd);
            if (l->sel > l->nrow)
                l->sel = l->nrow;
            break;
        case VK_PRIOR:
            l->sel -= lv_visible(wnd);
            if (l->sel < 1)
                l->sel = l->nrow ? 1 : 0;
            break;
        default:
            return DefWindowProcA(wnd, msg, wp, lp);
        }
        l->focus = l->sel;
        /* the arrow moves the one that is picked, unless Shift is held, which
         * takes the run from where it started instead */
        if (!(wnd->style & LVS_SINGLESEL) && (lp & 1))
            lv_select_range(l, anchor - 1, l->sel - 1);
        else
            lv_select_one(l, l->sel - 1);
        ween_ui_focus_cues = 1; /* the keyboard has been used, so it shows */
        /* keep the selection in view, which is the whole point of moving it */
        if (l->sel) {
            if (l->sel - 1 < l->top)
                lv_scroll_to(wnd, l, l->sel - 1);
            else if (l->sel - 1 >= l->top + lv_visible(wnd))
                lv_scroll_to(wnd, l, l->sel - lv_visible(wnd));
        }
        InvalidateRect(wnd, NULL, FALSE);
        notify_parent(wnd, LVN_ITEMCHANGED);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int i, mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        ween_lv_layout g;
        l = list_of(wnd);
        if (l && l->edit)
            lv_end_edit(wnd, l, 1); /* clicking away takes the name */
        SetFocus(wnd);
        if (!l)
            return 0;
        g = lv_layout(wnd, l);
        /* the bar down the right, when there is one */
        if (g.vbar && mx >= g.view_w) {
            int grab;
            ween_sbstate st = lv_sbstate(wnd, l);
            int pos = sb_click(my, g.view_h, &st, &grab);
            if (grab >= 0) {
                SetCapture(wnd);
                wnd->drag_offset = grab;
                wnd->drag_vertical = 1;
            }
            lv_scroll_to(wnd, l, pos);
            return 0;
        }
        /* and the one along the bottom, which scrolls the columns */
        if (g.hbar && my >= g.view_h) {
            int grab;
            ween_sbstate st = { l->scroll_x, 0, lv_content_w(l) - 1, g.view_w,
                                lv_item_h(wnd, l) };
            int pos = sb_click(mx, g.view_w, &st, &grab);
            if (grab >= 0) {
                SetCapture(wnd);
                wnd->drag_offset = grab;
                wnd->drag_vertical = 0;
            }
            l->scroll_x = sb_clamp(pos, &st);
            InvalidateRect(wnd, NULL, FALSE);
            return 0;
        }
        /* everything below scrolls with the columns, so it is asked about in
         * the coordinates the columns are laid out in */
        mx += l->scroll_x;
        /* a press on a header divider drags the column's width instead of
         * pressing the column, which is what a divider is for */
        if (my < lv_header_h(wnd)) {
            int d = lv_divider_at(l, mx, my);
            if (d >= 0) {
                l->sizing = d;
                l->size_x0 = mx;
                l->size_w0 = l->width[d];
                SetCapture(wnd);
                return 0;
            }
        }
        /* a press on the header: it goes down, and the app hears about it on
         * the release, which is how a column is sorted */
        if (my < lv_header_h(wnd)) {
            int x = 0;
            for (int c = 0; c < l->ncol; c++) {
                if (mx >= x && mx < x + l->width[c]) {
                    l->pressed = c;
                    l->drag_col = -1; /* until it is carried far enough */
                    l->drag_x0 = mx;
                    SetCapture(wnd);
                    InvalidateRect(wnd, NULL, FALSE);
                    break;
                }
                x += l->width[c];
            }
            return 0;
        }
        /* mx had the scroll added for the header; the hit test adds its own */
        /* the pointer has taken over: the caret goes until a key asks for it */
        if (ween_ui_focus_cues) {
            ween_ui_focus_cues = 0;
            InvalidateRect(wnd, NULL, FALSE);
        }
        {   /* a press on the box before a row turns it over, and does
             * nothing else: it is not a way of picking the row */
            UINT where = 0;
            int on = lv_item_hit(wnd, l, mx - l->scroll_x, my, &where);
            if (on >= 0 && (where & LVHT_ONITEMSTATEICON) &&
                l->row[on].state_img) {
                l->row[on].state_img = l->row[on].state_img == 2 ? 1 : 2;
                InvalidateRect(wnd, NULL, FALSE);
                notify_parent(wnd, LVN_ITEMCHANGED);
                return 0;
            }
        }
        i = lv_item_hit(wnd, l, mx - l->scroll_x, my, NULL);
        if (i >= 0) {
            int single = (wnd->style & LVS_SINGLESEL) != 0;
            int ctrl = (wp & MK_CONTROL) != 0, shift = (wp & MK_SHIFT) != 0;
            if (!single && ctrl) { /* add this one, keep the rest */
                l->row[i].selected = !l->row[i].selected;
                l->sel = i + 1;
            } else if (!single && shift && l->sel) {
                lv_select_range(l, l->sel - 1, i); /* the run from the anchor */
            } else {
                lv_select_one(l, i);
            }
            l->focus = i + 1;
            InvalidateRect(wnd, NULL, FALSE);
            notify_parent(wnd, LVN_ITEMCHANGED);
            notify_parent(wnd, NM_CLICK);
        } else if (lv_selected_count(l)) {
            /* a press on a row's other cells, or under the last row, drops
             * the selection and keeps the caret where it was */
            lv_select_none(l);
            l->sel = 0;
            InvalidateRect(wnd, NULL, FALSE);
            notify_parent(wnd, LVN_ITEMCHANGED);
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        /* The first click of the pair already selected the row; this says
         * the app should act on it, which for a shell means opening it. */
        ween_lv_layout g;
        int my = GET_Y_LPARAM(lp), i;
        l = list_of(wnd);
        if (!l)
            return 0;
        g = lv_layout(wnd, l);
        if (my < lv_header_h(wnd)) {
            /* A divider double-clicked sizes the column to the left of it to
             * fit what is in it — the header's own gesture, which comctl32
             * turns into an autosize on the view. */
            int d = lv_divider_at(l, GET_X_LPARAM(lp) + l->scroll_x, my);
            if (d >= 0)
                SendMessageA(wnd, LVM_SETCOLUMNWIDTH, (WPARAM)d,
                             MAKELPARAM(LVSCW_AUTOSIZE, 0));
            return 0;
        }
        if ((g.vbar && GET_X_LPARAM(lp) >= g.view_w) ||
            (g.hbar && my >= g.view_h))
            return 0; /* the bars are not items */
        i = lv_item_hit(wnd, l, GET_X_LPARAM(lp), my, NULL);
        if (i >= 0) {
            lv_select_one(l, i);
            l->focus = i + 1;
            InvalidateRect(wnd, NULL, FALSE);
            notify_parent(wnd, NM_DBLCLK);
        }
        return 0;
    }
    case LVM_GETITEMRECT: {
        RECT *out = (RECT *)lp;
        int i = (int)wp, code;
        l = list_of(wnd);
        if (!out || !l || i < 0 || i >= l->nrow)
            return FALSE;
        code = (int)out->left; /* win32 passes the code in on left */
        out->top = lv_header_h(wnd) + WEEN_LV_ROW_TOP +
                   (i - l->top) * lv_item_h(wnd, l);
        out->bottom = out->top + lv_item_h(wnd, l);
        if (code == LVIR_BOUNDS) {
            out->left = -l->scroll_x;
            out->right = lv_content_w(l) - l->scroll_x;
        } else {
            int icon_w = 0, icon_h = 0, indent;
            if (l->images)
                ImageList_GetIconSize(l->images, &icon_w, &icon_h);
            indent = (l->images && l->row[i].image >= 0) ? icon_w + 2 : 0;
            if (code == LVIR_ICON) {
                out->left = 2 - l->scroll_x;
                out->right = out->left + icon_w;
            } else { /* the label, and the selection is the same box */
                out->left = lv_label_x(indent) - l->scroll_x;
                out->right = out->left + lv_label_w(wnd, l, i, indent);
            }
        }
        return TRUE;
    }
    case LVM_HITTEST: {
        LVHITTESTINFO *hi = (LVHITTESTINFO *)lp;
        int i;
        l = list_of(wnd);
        if (!hi || !l)
            return -1;
        hi->iSubItem = 0;
        i = lv_item_hit(wnd, l, hi->pt.x, hi->pt.y, &hi->flags);
        hi->iItem = i;
        return i;
    }
    case WM_RBUTTONDOWN: {
        /* A press of the right button picks the row under it, the way win32
         * does, so that whatever menu follows is about that row. */
        LVHITTESTINFO hi;
        l = list_of(wnd);
        memset(&hi, 0, sizeof(hi));
        hi.pt.x = GET_X_LPARAM(lp);
        hi.pt.y = GET_Y_LPARAM(lp);
        SetFocus(wnd);
        if (ween_ui_focus_cues) {
            ween_ui_focus_cues = 0;
            InvalidateRect(wnd, NULL, FALSE);
        }
        if (l && SendMessageA(wnd, LVM_HITTEST, 0, (LPARAM)&hi) >= 0) {
            if (!l->row[hi.iItem].selected) {
                lv_select_one(l, hi.iItem);
                l->focus = hi.iItem + 1;
                InvalidateRect(wnd, NULL, FALSE);
                notify_parent(wnd, LVN_ITEMCHANGED);
            }
        } else if (l && lv_selected_count(l)) {
            /* off every label: the selection goes, as it does for the left
             * button, and the menu that follows is the folder's own */
            l->sel = 0;
            InvalidateRect(wnd, NULL, FALSE);
            notify_parent(wnd, LVN_ITEMCHANGED);
        }
        return 0;
    }
    case WM_SETCURSOR: {
        /* A resize arrow over a divider, an ordinary one everywhere else —
         * over it, not merely while one is being dragged: the shape is how a
         * person is told the column can be pulled wider at all. WM_SETCURSOR
         * says only that the pointer moved, so where it is comes from the
         * pointer itself. */
        POINT at;
        l = list_of(wnd);
        if (l && l->sizing >= 0) {
            SetCursor(LoadCursorA(NULL, IDC_SIZEWE));
            return TRUE;
        }
        if (l && lv_header_h(wnd) && GetCursorPos(&at) &&
            ScreenToClient(wnd, &at) && at.y >= 0 &&
            at.y < lv_header_h(wnd) &&
            lv_divider_at(l, at.x + l->scroll_x, at.y) >= 0) {
            SetCursor(LoadCursorA(NULL, IDC_SIZEWE));
            return TRUE;
        }
        return DefWindowProcA(wnd, msg, wp, lp);
    }

    case WM_MOUSEMOVE:
        l = list_of(wnd);
        if (l && l->sizing >= 0 && GetCapture() == wnd) {
            int w = l->size_w0 + GET_X_LPARAM(lp) + l->scroll_x - l->size_x0;
            if (w < WEEN_LV_DIVIDER * 2) /* never smaller than its divider */
                w = WEEN_LV_DIVIDER * 2;
            if (w != l->width[l->sizing]) {
                l->width[l->sizing] = w;
                InvalidateRect(wnd, NULL, FALSE);
            }
            return 0;
        }
        /* A heading held and moved sideways is being carried to another
         * place: past a few pixels it stops being a press on the column and
         * becomes a drag, and the view shows where it would land. */
        if (l && GetCapture() == wnd && l->pressed >= 0) {
            int mx = GET_X_LPARAM(lp) + l->scroll_x;
            if (l->drag_col < 0 &&
                (mx - l->drag_x0 > WEEN_LV_DRAG || l->drag_x0 - mx > WEEN_LV_DRAG)) {
                int edge = 0;
                for (int c = 0; c < l->pressed; c++)
                    edge += l->width[c];
                l->drag_col = l->pressed;
                l->drag_grab = l->drag_x0 - edge;
            }
            if (l->drag_col >= 0) {
                l->drag_x = mx;
                l->drop_at = lv_drop_at(l, mx);
                InvalidateRect(wnd, NULL, FALSE);
            }
            return 0;
        }
        if (l && GetCapture() == wnd && l->pressed < 0) {
            ween_lv_layout g = lv_layout(wnd, l);
            if (wnd->drag_vertical) {
                ween_sbstate st = lv_sbstate(wnd, l);
                lv_scroll_to(wnd, l, sb_drag(GET_Y_LPARAM(lp), g.view_h, &st,
                                             wnd->drag_offset));
            } else if (g.hbar) {
                ween_sbstate st = { l->scroll_x, 0, lv_content_w(l) - 1,
                                    g.view_w, lv_item_h(wnd, l) };
                int pos = sb_drag(GET_X_LPARAM(lp), g.view_w, &st,
                                  wnd->drag_offset);
                pos = sb_clamp(pos, &st);
                if (pos != l->scroll_x) {
                    l->scroll_x = pos;
                    InvalidateRect(wnd, NULL, FALSE);
                }
            }
        }
        return 0;
    case WM_LBUTTONUP:
        l = list_of(wnd);
        if (l && l->sizing >= 0) {
            l->sizing = -1;
            ReleaseCapture();
            return 0;
        }
        if (l && GetCapture() == wnd) {
            ReleaseCapture();
            if (l->drag_col >= 0) {
                /* it was carried somewhere: the column moves, and the view
                 * says so afterwards the way comctl32's header does */
                NMHEADERA nm;
                int from = l->drag_col, to = l->drop_at;
                l->drag_col = -1;
                l->pressed = -1;
                lv_move_column(l, from, to);
                InvalidateRect(wnd, NULL, FALSE);
                memset(&nm, 0, sizeof(nm));
                nm.hdr.hwndFrom = wnd;
                nm.hdr.idFrom = (UINT_PTR)wnd->id;
                nm.hdr.code = HDN_ENDDRAG;
                nm.iItem = from;
                nm.iButton = to > from ? to - 1 : to;
                if (wnd->parent)
                    SendMessageA(wnd->parent, WM_NOTIFY, (WPARAM)wnd->id,
                                 (LPARAM)&nm);
                return 0;
            }
            if (l->pressed >= 0) {
                /* the column was clicked: the app sorts and says so by
                 * refilling the list */
                NMLISTVIEW nm;
                int col = l->pressed;
                l->pressed = -1;
                InvalidateRect(wnd, NULL, FALSE);
                memset(&nm, 0, sizeof(nm));
                nm.hdr.hwndFrom = wnd;
                nm.hdr.idFrom = (UINT_PTR)wnd->id;
                nm.hdr.code = LVN_COLUMNCLICK;
                nm.iItem = -1;
                nm.iSubItem = col;
                if (wnd->parent)
                    SendMessageA(wnd->parent, WM_NOTIFY, (WPARAM)wnd->id,
                                 (LPARAM)&nm);
            }
        }
        return 0;
    case LVM_INSERTCOLUMNA: {
        const LVCOLUMNA *col = (const LVCOLUMNA *)lp;
        int i = (int)wp;
        l = list_of(wnd);
        if (!l || !col || i < 0 || i >= WEEN_LV_COLS)
            return -1;
        l->col[i] = dup_str(col->pszText);
        l->width[i] = (col->mask & LVCF_WIDTH) ? col->cx : 50;
        l->fmt[i] = (col->mask & LVCF_FMT) ? (col->fmt & LVCFMT_RIGHT) : 0;
        /* a heading with a name says so in its format, which is what a header
         * item means by HDF_STRING — and what an application must put back
         * when it changes the format for the sort arrow */
        if (col->mask & LVCF_TEXT)
            l->fmt[i] |= HDF_STRING;
        if (i >= l->ncol)
            l->ncol = i + 1;
        InvalidateRect(wnd, NULL, FALSE);
        return i;
    }
    case LVM_DELETECOLUMN: {
        /* Its number is its place, so everything past it shifts down. The
         * cells shift with it: a row's third column is the third one there
         * is, not the third one there ever was. */
        int i = (int)wp;
        l = list_of(wnd);
        if (!l || i < 0 || i >= l->ncol)
            return FALSE;
        free(l->col[i]);
        for (int c = i; c + 1 < l->ncol; c++) {
            l->col[c] = l->col[c + 1];
            l->width[c] = l->width[c + 1];
            l->fmt[c] = l->fmt[c + 1];
        }
        l->col[l->ncol - 1] = NULL;
        for (int r = 0; r < l->nrow; r++) {
            free(l->row[r].text[i]);
            for (int c = i; c + 1 < l->ncol; c++)
                l->row[r].text[c] = l->row[r].text[c + 1];
            l->row[r].text[l->ncol - 1] = NULL;
        }
        l->ncol--;
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case LVM_INSERTITEMA: {
        const LVITEMA *item = (const LVITEMA *)lp;
        ween_lvrow *rows;
        l = list_of(wnd);
        if (!l || !item)
            return -1;
        rows = realloc(l->row, (size_t)(l->nrow + 1) * sizeof(*rows));
        if (!rows)
            return -1;
        l->row = rows;
        memset(&l->row[l->nrow], 0, sizeof(*rows));
        l->row[l->nrow].image = (item->mask & LVIF_IMAGE) ? item->iImage : -1;
        /* a new row in a view with boxes gets one, unticked, which a program
         * can then take away to make the row a heading */
        if (l->exstyle & LVS_EX_CHECKBOXES)
            l->row[l->nrow].state_img = 1;
        l->row[l->nrow].text[0] = dup_str(item->pszText);
        InvalidateRect(wnd, NULL, FALSE);
        return l->nrow++;
    }
    case LVM_SETIMAGELIST:
        /* two sets: the big pictures the Icons view draws, and the small ones
         * every other view draws */
        l = list_of(wnd);
        if (l) {
            HIMAGELIST *slot = (int)wp == LVSIL_NORMAL ? &l->big : &l->images;
            HIMAGELIST was = *slot;
            *slot = (HIMAGELIST)lp;
            InvalidateRect(wnd, NULL, FALSE);
            return (LRESULT)(UINT_PTR)was;
        }
        return 0;
    case LVM_SETITEMTEXTA: {
        const LVITEMA *item = (const LVITEMA *)lp;
        int i = (int)wp;
        l = list_of(wnd);
        if (!l || !item || i < 0 || i >= l->nrow || item->iSubItem >= WEEN_LV_COLS)
            return FALSE;
        free(l->row[i].text[item->iSubItem]);
        l->row[i].text[item->iSubItem] = dup_str(item->pszText);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case LVM_SETEXTENDEDLISTVIEWSTYLE: {
        /* wp names which bits are being spoken about; zero means all of them,
         * which is how the message is nearly always sent. */
        DWORD mask = wp ? (DWORD)wp : 0xFFFFFFFFu;
        l = list_of(wnd);
        if (l) {
            DWORD was = l->exstyle;
            l->exstyle = (l->exstyle & ~mask) | ((DWORD)lp & mask);
            InvalidateRect(wnd, NULL, FALSE);
            return (LRESULT)was;
        }
        return 0;
    }
    case LVM_GETEXTENDEDLISTVIEWSTYLE:
        l = list_of(wnd);
        return l ? (LRESULT)l->exstyle : 0;
    case LVM_GETITEMSTATE: {
        int i = (int)wp;
        UINT want = (UINT)lp, state = 0;
        l = list_of(wnd);
        if (!l || i < 0 || i >= l->nrow)
            return 0;
        if (l->row[i].selected)
            state |= LVIS_SELECTED;
        if (l->row[i].cut)
            state |= LVIS_CUT;
        if (l->focus == i + 1)
            state |= LVIS_FOCUSED;
        if (l->exstyle & LVS_EX_CHECKBOXES)
            state |= INDEXTOSTATEIMAGEMASK(l->row[i].state_img);
        return state & want;
    }
    case LVM_SETITEMSTATE: {
        /* -1 means every row, which is how Select All is asked for and how a
         * selection is dropped wholesale. */
        const LVITEMA *item = (const LVITEMA *)lp;
        int at = (int)wp, from, to;
        l = list_of(wnd);
        if (!l || !item)
            return FALSE;
        from = at < 0 ? 0 : at;
        to = at < 0 ? l->nrow - 1 : at;
        if (item->stateMask & LVIS_SELECTED) {
            int on = (item->state & LVIS_SELECTED) != 0;
            for (int i = from; i <= to && i < l->nrow; i++)
                if (i >= 0)
                    l->row[i].selected = on;
            if (on && at >= 0)
                l->sel = at + 1;
            else if (!on)
                l->sel = at < 0 ? 0 : l->sel;
            InvalidateRect(wnd, NULL, FALSE);
        }
        if (item->stateMask & LVIS_FOCUSED) {
            if (item->state & LVIS_FOCUSED)
                l->focus = at + 1;
            InvalidateRect(wnd, NULL, FALSE);
        }
        if (item->stateMask & LVIS_CUT) {
            for (int i = from; i <= to && i < l->nrow; i++)
                if (i >= 0)
                    l->row[i].cut = (item->state & LVIS_CUT) != 0;
            InvalidateRect(wnd, NULL, FALSE);
        }
        if (item->stateMask & LVIS_STATEIMAGEMASK) {
            /* the state picture by number: none, a box, a ticked box */
            int img = (int)((item->state & LVIS_STATEIMAGEMASK) >> 12);
            for (int i = from; i <= to && i < l->nrow; i++)
                if (i >= 0)
                    l->row[i].state_img = img;
            InvalidateRect(wnd, NULL, FALSE);
        }
        return TRUE;
    }
    case LVM_DELETEALLITEMS:
        /* Every navigation empties the list, so this is not optional. */
        l = list_of(wnd);
        if (l) {
            for (int i = 0; i < l->nrow; i++)
                for (int c = 0; c < WEEN_LV_COLS; c++) {
                    free(l->row[i].text[c]);
                    l->row[i].text[c] = NULL;
                }
            l->nrow = 0;
            l->sel = 0;
            l->focus = 0;
            l->top = 0;
            InvalidateRect(wnd, NULL, FALSE);
        }
        return TRUE;
    case LVM_GETSELECTEDCOUNT:
        l = list_of(wnd);
        return l ? lv_selected_count(l) : 0;
    case LVM_GETITEMCOUNT:
        l = list_of(wnd);
        return l ? l->nrow : 0;
    case LVM_GETNEXTITEM:
        /* The next row after wp with what lParam asks for — which is how a
         * list with more than one selected is walked: start at -1 and keep
         * asking until it says -1 back. */
        l = list_of(wnd);
        if (!l)
            return -1;
        if (lp & LVNI_SELECTED) {
            for (int i = (int)wp + 1; i < l->nrow; i++)
                if (l->row[i].selected)
                    return i;
            return -1;
        }
        if (lp & LVNI_FOCUSED)
            return (l->focus && (int)wp < l->focus - 1) ? l->focus - 1 : -1;
        return (int)wp + 1 < l->nrow ? (int)wp + 1 : -1;
    case LVM_SETCOLUMNA: {
        const LVCOLUMNA *col = (const LVCOLUMNA *)lp;
        int i = (int)wp;
        l = list_of(wnd);
        if (!l || !col || i < 0 || i >= l->ncol)
            return FALSE;
        if (col->mask & LVCF_FMT) /* alignment; the arrows are the header's */
            l->fmt[i] = (l->fmt[i] & (HDF_SORTUP | HDF_SORTDOWN)) |
                        (col->fmt & LVCFMT_RIGHT);
        if (col->mask & LVCF_WIDTH)
            l->width[i] = col->cx;
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case LVM_GETHEADER: {
        /* Made when it is first asked for, and kept: an application holds on
         * to the handle. It draws nothing — the list draws the band — so it
         * is not shown. */
        l = list_of(wnd);
        if (!l)
            return 0;
        if (!l->header) {
            RECT cr;
            GetClientRect(wnd, &cr);
            /* It stands where the band is drawn, so a window can ask where
             * the headings are — the point of a control that is not shown is
             * still to have a place. It is not visible, so it takes no
             * mouse: the list answers for its own band. */
            l->header = CreateWindowA(WC_HEADERA, "", WS_CHILD, 0, 0, cr.right,
                                      WEEN_LV_HEADER_H, wnd, NULL, NULL, NULL);
        }
        return (LRESULT)(INT_PTR)l->header;
    }
    case LVM_GETCOLUMNWIDTH:
        l = list_of(wnd);
        if (!l || (int)wp < 0 || (int)wp >= l->ncol)
            return 0;
        return l->width[(int)wp];
    case LVM_SETCOLUMNWIDTH: {
        int cx;
        l = list_of(wnd);
        if (!l || (int)wp < 0 || (int)wp >= l->ncol)
            return FALSE;
        cx = (int)(short)LOWORD(lp);
        if (cx == LVSCW_AUTOSIZE || cx == LVSCW_AUTOSIZE_USEHEADER) {
            int fit = lv_autosize_width(wnd, l, (int)wp,
                                        cx == LVSCW_AUTOSIZE_USEHEADER);
            if (!fit)
                return TRUE; /* nothing in it to fit to */
            cx = fit;
        }
        l->width[(int)wp] = cx;
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case LVM_ENSUREVISIBLE:
        l = list_of(wnd);
        if (l && (int)wp >= 0 && (int)wp < l->nrow) {
            if ((int)wp < l->top)
                lv_scroll_to(wnd, l, (int)wp);
            else if ((int)wp >= l->top + lv_visible(wnd))
                lv_scroll_to(wnd, l, (int)wp - lv_visible(wnd) + 1);
        }
        return TRUE;
    case WM_DESTROY:
        if (wnd->ctl) {
            list_ctl_free(wnd->ctl);
            wnd->ctl = NULL;
        }
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the up-down ----------------------------------------------------------
 *
 * Two buttons stacked in one control, each half the height, with a small
 * arrow in the middle of it: what the machine puts beside the number of
 * minutes on the Offline Files page. Given a buddy it keeps the value in that
 * window's text, which is what UDS_SETBUDDYINT asks for.
 */
typedef struct {
    HWND buddy;
    int min, max, pos;
    int pressed; /* 0 none, 1 up, 2 down */
} ween_updown;

static void updown_free(void *p) { free(p); }

static ween_updown *updown_of(HWND w)
{
    if (!w->ctl) {
        ween_updown *u = calloc(1, sizeof(*u));
        if (u) {
            u->max = 100;
            w->ctl = u;
            w->ctl_free = updown_free;
        }
    }
    return w->ctl;
}

/* The arrow: three pixels over one, the way the machine draws it. */
static void updown_arrow(ween_surface *s, int cx, int cy, int up)
{
    ween_surface_fill(s, cx - 1, cy + (up ? 1 : 0), 3, 1, WEEN_BLACK);
    ween_surface_fill(s, cx, cy + (up ? 0 : 1), 1, 1, WEEN_BLACK);
}

static void updown_show(HWND wnd, ween_updown *u)
{
    char text[32];
    int v = u->pos, n = 0, i;
    if (!u->buddy)
        return;
    if (v < 0) {
        text[n++] = '-';
        v = -v;
    }
    {   /* the digits, without stdio in a control */
        char rev[16];
        int m = 0;
        do {
            rev[m++] = (char)('0' + v % 10);
            v /= 10;
        } while (v && m < (int)sizeof(rev));
        for (i = m - 1; i >= 0; i--)
            text[n++] = rev[i];
    }
    text[n] = 0;
    SetWindowTextA(u->buddy, text);
    (void)wnd;
}

static void updown_step(HWND wnd, ween_updown *u, int by)
{
    int was = u->pos;
    u->pos += by;
    if (u->pos < u->min)
        u->pos = (wnd->style & UDS_WRAP) ? u->max : u->min;
    if (u->pos > u->max)
        u->pos = (wnd->style & UDS_WRAP) ? u->min : u->max;
    if (u->pos != was)
        updown_show(wnd, u);
}

static LRESULT updown_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_updown *u = updown_of(wnd);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        struct ween_wnd *top = ween_top_level(wnd);
        RECT cr;
        int ox, oy, half;
        BeginPaint(wnd, &ps);
        GetClientRect(wnd, &cr);
        ween_client_origin(wnd, &ox, &oy);
        /* Two buttons of the same height with a row of face between them,
         * and a column of it against the field they stand in — which is what
         * the machine's pair has. */
        half = (cr.bottom - 1) / 2;
        ween_surface_fill(&top->surface, ox, oy, cr.right, cr.bottom,
                          WEEN_FACE);
        for (int i = 0; i < 2; i++) {
            int y = i ? cr.bottom - half : 0, h = half;
            int down = u && u->pressed == i + 1;
            ween_classic_bevel(&top->surface, ox + 1, oy + y, cr.right - 1, h,
                               down);
            updown_arrow(&top->surface,
                         ox + 1 + (cr.right - 1) / 2 + (down ? 1 : 0),
                         oy + y + h / 2 - 1 + (down ? 1 : 0), !i);
        }
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RECT cr;
        int at = GET_Y_LPARAM(lp);
        GetClientRect(wnd, &cr);
        if (!u)
            return 0;
        u->pressed = at < cr.bottom / 2 ? 1 : 2;
        updown_step(wnd, u, u->pressed == 1 ? 1 : -1);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
        if (u && u->pressed) {
            u->pressed = 0;
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case UDM_SETRANGE:
        if (u) {
            u->max = (short)LOWORD(lp);
            u->min = (short)HIWORD(lp);
        }
        return 0;
    case UDM_GETRANGE:
        return u ? (LRESULT)(((unsigned)u->min << 16) |
                             ((unsigned)u->max & 0xffff))
                 : 0;
    case UDM_SETPOS: {
        int was = u ? u->pos : 0;
        if (u) {
            u->pos = (short)LOWORD(lp);
            updown_show(wnd, u);
        }
        return was;
    }
    case UDM_GETPOS:
        return u ? u->pos : 0;
    case UDM_SETBUDDY: {
        HWND was = u ? u->buddy : NULL;
        if (u)
            u->buddy = (HWND)wp;
        /* UDS_ALIGNRIGHT: the arrows stand inside the field's own border, so
         * the field keeps that much of its right side clear — which is what
         * puts the machine's "60" against them rather than under them. */
        if (u && u->buddy && (wnd->style & UDS_ALIGNRIGHT)) {
            RECT r;
            GetWindowRect(wnd, &r);
            SendMessageA(u->buddy, EM_SETMARGINS, EC_RIGHTMARGIN,
                         MAKELPARAM(0, r.right - r.left + 3));
        }
        return (LRESULT)(UINT_PTR)was;
    }
    case UDM_GETBUDDY:
        return (LRESULT)(UINT_PTR)(u ? u->buddy : NULL);
    default:
        break;
    }
    return DefWindowProcA(wnd, msg, wp, lp);
}

/* ---- the trackbar ---------------------------------------------------------
 *
 * A sunken channel with a pointed thumb riding it and a row of ticks below.
 * The thumb's centre travels between half a thumb in from each end of the
 * channel, and the ticks mark the same span. */

#define WEEN_TB_THUMB_W 11
#define WEEN_TB_THUMB_H 21
#define WEEN_TB_CHANNEL 4

/* The pointed thumb of a plain trackbar, or the box a TBS_BOTH one uses. */
static void trackbar_thumb(ween_surface *s, int x, int y, int w, int h, int point)
{
    if (!point) {
        ween_classic_edge(s, x, y, w, h, EDGE_RAISED, BF_RECT | BF_SOFT | BF_MIDDLE,
                          NULL);
        return;
    }
    int body = h - 5;
    ween_surface_hline(s, x, y, w - 1, WEEN_WHITE);
    ween_surface_vline(s, x, y, body, WEEN_WHITE);
    ween_surface_fill(s, x + 1, y + 1, w - 3, body - 1, WEEN_FACE);
    ween_surface_vline(s, x + w - 2, y + 1, body - 1, WEEN_SHADOW);
    ween_surface_vline(s, x + w - 1, y, body + 1, WEEN_DKSHADOW);
    for (int i = 0; i < 5; i++) {
        int l = x + 1 + i, r = x + w - 2 - i, py = y + body + i;
        if (l > r)
            break;
        ween_surface_pixel(s, l, py, WEEN_WHITE);
        ween_surface_fill(s, l + 1, py, r - l - 1, WEEN_FACE ? 1 : 1, WEEN_FACE);
        ween_surface_pixel(s, r, py, WEEN_SHADOW);
        ween_surface_pixel(s, r + 1, py, WEEN_DKSHADOW);
    }
}

static void trackbar_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    struct ween_wnd *top = ween_top_level(wnd);
    RECT r = ps->rcPaint;
    int vert = (wnd->style & TBS_VERT) != 0;
    /* A box thumb is what TBS_BOTH asks for — ticks on both sides leave no
     * room for a point. TBS_NOTICKS only takes the ticks away; the thumb
     * keeps its point, which is what the machine's has. */
    int box = (wnd->style & TBS_BOTH) != 0;
    int ox, oy;
    int min = wnd->scroll_min, max = wnd->scroll_max, pos = wnd->scroll_pos;
    int cw = r.right - r.left, ch = r.bottom - r.top;
    /* the thumb: pointed across the channel, or a box when TBS_BOTH asks */
    int tw = vert ? (box ? 20 : WEEN_TB_THUMB_H) : WEEN_TB_THUMB_W;
    int thh = vert ? (box ? 11 : WEEN_TB_THUMB_W) : WEEN_TB_THUMB_H;
    int travel = vert ? thh : tw;
    int chan0 = 8, chan1 = (vert ? ch : cw) - (vert ? 9 : 8);
    int half = travel / 2;
    int span = chan1 - chan0 - 2 * half;
    int at = max > min ? chan0 + half + span * (pos - min) / (max - min)
                       : chan0 + half;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));

    if (!vert) {
        ween_classic_edge(&top->surface, ox + chan0, oy + 9, chan1 - chan0,
                          WEEN_TB_CHANNEL, EDGE_SUNKEN, BF_RECT, NULL);
        if (!(wnd->style & TBS_NOTICKS))
            for (int i = min; i <= max; i++) {
                int tx = ox + chan0 + half + span * (i - min) / (max - min);
                ween_surface_vline(&top->surface, tx, oy + 25,
                                   (i == min || i == max) ? 4 : 3, WEEN_DKSHADOW);
            }
        trackbar_thumb(&top->surface, ox + at - half, oy + 2, tw, thh, !box);
    } else {
        int chan_x = cw - 12, thumb_x = cw - tw;
        ween_classic_edge(&top->surface, ox + chan_x, oy + chan0,
                          WEEN_TB_CHANNEL, chan1 - chan0, EDGE_SUNKEN, BF_RECT,
                          NULL);
        if (!(wnd->style & TBS_NOTICKS))
            for (int i = min; i <= max; i++) {
                int ty = oy + chan0 + half + span * (i - min) / (max - min);
                ween_surface_hline(&top->surface, ox + 5, ty,
                                   (i == min || i == max) ? 4 : 3, WEEN_DKSHADOW);
            }
        trackbar_thumb(&top->surface, ox + thumb_x, oy + at - half, tw, thh, 0);
    }
}

/* The position a point along the bar corresponds to. */
static int trackbar_pos_at(HWND wnd, int at)
{
    int vert = (wnd->style & TBS_VERT) != 0;
    int box = (wnd->style & TBS_BOTH) != 0;
    int travel = vert ? (box ? 11 : WEEN_TB_THUMB_W) : WEEN_TB_THUMB_W;
    int half = travel / 2;
    int chan0 = 8, chan1 = (vert ? wnd->h : wnd->w) - (vert ? 9 : 8);
    int span = chan1 - chan0 - 2 * half;
    int rel = at - chan0 - half;
    if (span <= 0)
        return wnd->scroll_min;
    if (rel < 0)
        rel = 0;
    if (rel > span)
        rel = span;
    return wnd->scroll_min +
           MulDiv(rel, wnd->scroll_max - wnd->scroll_min, span);
}

static LRESULT trackbar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int vert = (wnd->style & TBS_VERT) != 0;
    int at = vert ? GET_Y_LPARAM(lp) : GET_X_LPARAM(lp);

    switch (msg) {
    case WM_LBUTTONDOWN:
        SetFocus(wnd);
        SetCapture(wnd);
        wnd->scroll_pos = trackbar_pos_at(wnd, at);
        InvalidateRect(wnd, NULL, FALSE);
        if (wnd->parent)
            SendMessageA(wnd->parent, vert ? WM_VSCROLL : WM_HSCROLL,
                         MAKEWPARAM(SB_THUMBTRACK, (WORD)wnd->scroll_pos),
                         (LPARAM)wnd);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == wnd) {
            int pos = trackbar_pos_at(wnd, at);
            if (pos != wnd->scroll_pos) {
                wnd->scroll_pos = pos;
                InvalidateRect(wnd, NULL, FALSE);
                if (wnd->parent)
                    SendMessageA(wnd->parent, vert ? WM_VSCROLL : WM_HSCROLL,
                                 MAKEWPARAM(SB_THUMBTRACK, (WORD)pos),
                                 (LPARAM)wnd);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == wnd) {
            ReleaseCapture();
            if (wnd->parent)
                SendMessageA(wnd->parent, vert ? WM_VSCROLL : WM_HSCROLL,
                             MAKEWPARAM(SB_THUMBPOSITION, (WORD)wnd->scroll_pos),
                             (LPARAM)wnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_LEFT || wp == VK_DOWN || wp == VK_RIGHT || wp == VK_UP) {
            int d = (wp == VK_LEFT || wp == VK_UP) ? -1 : 1;
            int pos = wnd->scroll_pos + d;
            if (pos >= wnd->scroll_min && pos <= wnd->scroll_max) {
                wnd->scroll_pos = pos;
                InvalidateRect(wnd, NULL, FALSE);
            }
            return 0;
        }
        return DefWindowProcA(wnd, msg, wp, lp);
    case WM_CREATE:
        wnd->scroll_min = 0;
        wnd->scroll_max = 100;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        trackbar_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case TBM_SETPOS:
        wnd->scroll_pos = (int)lp;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case TBM_GETPOS:
        return wnd->scroll_pos;
    case TBM_SETRANGE:
        wnd->scroll_min = (int)(short)LOWORD(lp);
        wnd->scroll_max = (int)(short)HIWORD(lp);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the tab control -----------------------------------------------------
 *
 * Tabs sit in a strip along the top: the selected one two pixels taller and
 * two wider on each side, its bottom merged into the body below. Each tab is
 * a white top and left with a shadow/dark right, corners stepped. Widths come
 * from Wine's tab.c: the reported text width plus twice the six-pixel padding,
 * never less than six average characters plus the same padding. */

static int tab_min_width(const ween_strike *f)
{
    static const char alpha[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int sum = 0;
    if (!f)
        return 54;
    /* The average character width the font reports. A bitmap face carries it
     * in its header and it is the 'x' advance; a scalable one is averaged
     * across the alphabet, which is how wine arrives at Tahoma's seven. */
    if (f->bitmap_only)
        return ween_strike_char_advance(f, 'x') * 6 + 12;
    for (int i = 0; i < 52; i++)
        sum += ween_strike_char_extent(f, (unsigned char)alpha[i]);
    return ((sum + 26) / 52) * 6 + 12;
}

static int tab_width(const ween_strike *f, const char *text, int min)
{
    int w = f ? ween_strike_text_extent(f, text, (int)strlen(text)) + 12 : min;
    return w < min ? min : w;
}

static void tab_shape(ween_surface *s, int x, int y, int l, int r, int bottom)
{
    /* white top and left, stepped into the corner */
    ween_surface_hline(s, l + 2, y, r - 2 - (l + 2), WEEN_WHITE);
    ween_surface_pixel(s, l + 1, y + 1, WEEN_WHITE);
    ween_surface_vline(s, l, y + 2, bottom - (y + 2), WEEN_WHITE);
    /* shadow and dark right, its top pixel dark alone */
    ween_surface_pixel(s, r - 2, y + 1, WEEN_DKSHADOW);
    ween_surface_vline(s, r - 2, y + 2, bottom - (y + 2), WEEN_SHADOW);
    ween_surface_vline(s, r - 1, y + 2, bottom - (y + 2), WEEN_DKSHADOW);
    (void)x;
}

static void tab_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_items *it = items_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT r = ps->rcPaint;
    int ox, oy, th = f ? f->ascent - f->descent : 13;
    /* The strip the tabs stand in is four more than the text, and the body
     * starts two below it — nineteen for the eleven-pixel faces, which is
     * what the machine's property sheets have. */
    int tabh = th + 4, body = 2 + tabh;
    int min = tab_min_width(f);
    int sel = it ? it->cursel : 0;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));

    /* The page below the tabs, in the plain raised edge a window frame wears
     * — outer line COLOR_3DLIGHT, which is face, and the white one inside it.
     * The soft edge a button wears would put the white a pixel out, which is
     * a pixel the machine's tab control does not have. */
    ween_classic_edge(&top->surface, ox, oy + body, r.right, r.bottom - body,
                      EDGE_RAISED, BF_RECT, NULL);

    /* right to left, so each tab's dark edge covers the next one's white.
     * The first tab starts three in, so the selected one — which is drawn two
     * wider on each side — begins one pixel inside the control, which is
     * where the machine's does. */
    for (int pass = 0; pass < 2; pass++) {
        int l = 3;
        for (int i = 0; it && i < it->count; i++) {
            int w = tab_width(f, it->item[i], min);
            int selected = i == sel;
            if ((pass == 1) == selected) {
                int tl = selected ? l - 2 : l;
                int tr = selected ? l + w + 2 : l + w;
                int ty = selected ? 0 : 2;
                /* Every tab reaches the frame below it, and the one in front
                 * covers the frame's own top line — which is what makes it
                 * look joined to the page. The machine's leaves no white
                 * under the tab it has picked and none under the others
                 * either. */
                int tb = body + (selected ? 2 : 1);
                ween_surface_fill(&top->surface, ox + tl, oy + ty, tr - tl,
                                  tb - ty, WEEN_FACE);
                tab_shape(&top->surface, ox, oy + ty, ox + tl, ox + tr,
                          oy + tb);
                if (f) {
                    /* The label keeps its place in the tab whether or not the
                     * tab is the one in front: the two extra pixels a picked
                     * tab is given go below its text, not around it. */
                    int visible = tabh;
                    ween_strike_draw(f, &top->surface, ox + l + 6,
                                     oy + ty + (visible - th) / 2 + 1,
                                     it->item[i],
                                     (int)strlen(it->item[i]), WEEN_BLACK);
                }
            }
            l += w;
        }
    }
}

static LRESULT tab_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_items *it;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        tab_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
        int min = tab_min_width(f), l = 2, x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp), tabh = (f ? f->ascent - f->descent : 13) + 4;
        it = items_of(wnd);
        SetFocus(wnd);
        if (y > tabh + 2)
            return 0;
        for (int i = 0; it && i < it->count; i++) {
            int w = tab_width(f, it->item[i], min);
            if (x >= l && x < l + w) {
                if (it->cursel != i) {
                    it->cursel = i;
                    InvalidateRect(wnd, NULL, FALSE);
                    notify_parent(wnd, TCN_SELCHANGE);
                }
                break;
            }
            l += w;
        }
        return 0;
    }
    case TCM_INSERTITEMA: {
        const TCITEMA *ti = (const TCITEMA *)lp;
        it = items_of(wnd);
        if (it && it->cursel < 0)
            it->cursel = 0;
        return items_add(wnd, ti && (ti->mask & TCIF_TEXT) ? ti->pszText : "");
    }
    case TCM_SETCURSEL:
        it = items_of(wnd);
        if (it) {
            int old = it->cursel;
            it->cursel = (int)wp;
            InvalidateRect(wnd, NULL, FALSE);
            return old;
        }
        return -1;
    case TCM_GETCURSEL:
        it = items_of(wnd);
        return it ? it->cursel : -1;
    case TCM_ADJUSTRECT: {
        /* Between the whole control and the page inside it: wp says which way
         * to go. The strip the tabs stand in comes off the top, and the body's
         * own border off the rest. */
        RECT *r = (RECT *)lp;
        const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
        /* the strip the tabs stand in, the two below it the body's edge
         * begins at, and the body's own raised edge */
        int th = (f ? f->ascent - f->descent : 13) + 4 + 2 + 2, edge = 2;
        if (!r)
            return 0;
        if (wp) { /* a page's rectangle -> the control that must hold it */
            r->left -= edge;
            r->top -= th;
            r->right += edge;
            r->bottom += edge;
        } else { /* the control -> where its page goes */
            r->left += edge;
            r->top += th;
            r->right -= edge;
            r->bottom -= edge;
        }
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the status bar -------------------------------------------------------
 *
 * A strip along the bottom of its parent's client area, divided into parts,
 * each in a status-field border, with the size grip in the corner. */

/* The border above a status bar's parts. Windows leaves two rows of face
 * between the top of the bar and the first part's sunken edge, whatever the
 * bar's height: the parts grow downward with the window, not upward. */
#define WEEN_SB_VBORDER 2

static void status_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_items *it = items_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    RECT r = ps->rcPaint;
    int ox, oy, left = 0, grip = 0;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    if (wnd->style & SBARS_SIZEGRIP)
        grip = 15; /* the corner square the grip is drawn in */

    for (int i = 0; it && i < it->count; i++) {
        /* A bar whose parts were never set has one, the width of the window:
         * SB_SETTEXTA alone is enough to make a status bar, and win32 gives
         * that text the whole strip. */
        int right = it->edge ? it->edge[i] : -1;
        RECT part;
        if (right < 0 || right > r.right)
            right = r.right;
        part.left = left;
        part.top = r.top + WEEN_SB_VBORDER;
        part.right = right;
        part.bottom = r.bottom;
        ween_classic_edge(&top->surface, ox + part.left, oy + part.top,
                          part.right - part.left, part.bottom - part.top,
                          BDR_SUNKENOUTER, BF_RECT, NULL);
        if (it->icon[i]) { /* before the text, two in and one down */
            struct ween_dc idc;
            memset(&idc, 0, sizeof(idc));
            idc.s = &top->surface;
            idc.clip_w = top->surface.w;
            idc.clip_h = top->surface.h;
            DrawIconEx(&idc, ox + part.left + 2, oy + part.top + 1, it->icon[i],
                       16, 16, 0, NULL, DI_NORMAL);
        }
        /* Two in and one above the middle, which is where the machine puts
         * it — the same lopsided centring a menu item and a pane's bar have.
         */
        {
            const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
            int cell = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 12;
            part.left += it->icon[i] ? 22 : 2;
            /* Centred with the odd pixel above, then one higher again. Both
             * halves are measured: the explorer's 18-pixel part and Paint's
             * 21-pixel one put their text one row apart, and only rounding
             * this division up puts each of them where the machine does. */
            part.top += (part.bottom - part.top - cell + 1) / 2 - 1;
        }
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        DrawTextA(dc, it->item[i], -1, &part, DT_LEFT | DT_SINGLELINE);
        left = right + 2; /* the gap between parts */
    }
    if (grip) /* over the last part, which runs on under it */
        ween_classic_sizegrip(&top->surface, ox + r.right - 2,
                              oy + r.bottom - 2);
}

static LRESULT status_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_items *it;
    switch (msg) {
    case WM_CREATE:
    case WM_SIZE: {
        /* the strip sits along the bottom of the parent's client area, and
         * follows it when the window is resized — the app forwards WM_SIZE,
         * as a win32 app does. Its height is its font's, not the caller's:
         * that is what a status bar does with the rectangle it is given.
         *
         * Unless it was asked not to. CCS_NORESIZE means the application
         * places the bar itself, and an application that wants a taller one
         * than the font implies — Paint's is 23 — has no other way to say
         * so. */
        const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
        RECT pc;
        if (wnd->style & CCS_NORESIZE) {
            InvalidateRect(wnd, NULL, FALSE);
            return 0;
        }
        /* The parts do not start at the very top: two rows of the bar are a
         * border above them, and the height accounts for it. */
        wnd->h = (f ? f->ascent - f->descent : 13) + 5 + WEEN_SB_VBORDER;
        if (wnd->parent && GetClientRect(wnd->parent, &pc)) {
            wnd->w = pc.right;
            wnd->x = 0;
            wnd->y = pc.bottom - wnd->h;
        }
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        status_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_NCHITTEST: {
        /* the grip in the corner resizes the window, as it does in win32 */
        RECT cr;
        int ox, oy, x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (!(wnd->style & SBARS_SIZEGRIP))
            return HTCLIENT;
        GetClientRect(wnd, &cr);
        ween_client_origin(wnd, &ox, &oy);
        if (x - ox >= cr.right - 15 && y - oy >= cr.bottom - 15)
            return HTBOTTOMRIGHT;
        return HTCLIENT;
    }
    case SB_SETICON: {
        int i = (int)wp;
        it = items_of(wnd);
        if (!it || i < 0 || i >= 8)
            return FALSE;
        it->icon[i] = (HICON)lp;
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case SB_SETPARTS: {
        const int *edges = (const int *)lp;
        int n = (int)wp;
        it = items_of(wnd);
        if (!it || !edges)
            return FALSE;
        free(it->edge);
        it->edge = calloc((size_t)n, sizeof(int));
        for (int i = 0; i < n; i++) {
            if (it->edge)
                it->edge[i] = edges[i];
            if (i >= it->count)
                items_add(wnd, "");
        }
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case SB_SETTEXTA: {
        int i = (int)(wp & 0xff);
        it = items_of(wnd);
        while (it && it->count <= i)
            items_add(wnd, "");
        if (it && i < it->count) {
            const char *src = (const char *)lp;
            size_t n = strlen(src ? src : "") + 1;
            char *copy = malloc(n);
            if (copy) {
                memcpy(copy, src ? src : "", n);
                free(it->item[i]);
                it->item[i] = copy;
            }
        }
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- registration --------------------------------------------------------- */

static LRESULT toolbar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT rebar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

/* ---- the header inside a list view ----------------------------------------
 *
 * A report-view list keeps its columns in a header control, and that is where
 * an application sets the arrow that says which column the view is sorted by.
 * The list still draws the band — see the ROADMAP — so this is where the
 * columns are *said* to be rather than where they are drawn: it holds no
 * state of its own and passes everything to the list it belongs to.
 */
static LRESULT header_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND list = wnd->parent;
    ween_list *l = list ? list_of(list) : NULL;
    HDITEMA *hd = (HDITEMA *)lp;
    int i = (int)wp;
    switch (msg) {
    case HDM_GETITEMCOUNT:
        return l ? l->ncol : 0;
    case HDM_SETITEMA:
        if (!l || !hd || i < 0 || i >= l->ncol)
            return FALSE;
        if (hd->mask & HDI_FORMAT) /* HDF_STRING and the rest ride along */
            l->fmt[i] = hd->fmt;
        if (hd->mask & HDI_WIDTH)
            l->width[i] = hd->cxy;
        InvalidateRect(list, NULL, FALSE);
        return TRUE;
    case HDM_GETITEMA:
        if (!l || !hd || i < 0 || i >= l->ncol)
            return FALSE;
        if (hd->mask & HDI_FORMAT)
            hd->fmt = l->fmt[i];
        if (hd->mask & HDI_WIDTH)
            hd->cxy = l->width[i];
        if ((hd->mask & HDI_TEXT) && hd->pszText && hd->cchTextMax > 0) {
            const char *t = l->col[i] ? l->col[i] : "";
            int n = (int)strlen(t);
            if (n > hd->cchTextMax - 1)
                n = hd->cchTextMax - 1;
            memcpy(hd->pszText, t, (size_t)n);
            hd->pszText[n] = 0;
        }
        return TRUE;
    }
    return DefWindowProcA(wnd, msg, wp, lp);
}

/* win32 has an application register the common control classes before it uses
 * one; ween32 registers them itself, so this only has to agree. Saying so
 * rather than leaving it out is what lets an application call it on both
 * sides without an #ifdef. */
BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX *icc)
{
    (void)icc;
    ween_register_controls();
    return TRUE;
}

void InitCommonControls(void)
{
    ween_register_controls();
}

void ween_register_controls(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.hbrBackground = NULL; /* every control paints its own background */
    /* EDIT is the one control here that acts on a double click: it takes the
     * word under it. Everything else is left to receive ordinary presses, so
     * clicking one quickly never drops every other click. */
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = edit_proc;
    wc.lpszClassName = "EDIT";
    RegisterClassA(&wc);
    wc.style = 0;
    wc.lpfnWndProc = scrollbar_proc;
    wc.lpszClassName = "SCROLLBAR";
    RegisterClassA(&wc);
    wc.lpfnWndProc = listbox_proc;
    wc.lpszClassName = "LISTBOX";
    RegisterClassA(&wc);
    wc.lpfnWndProc = combo_proc;
    wc.lpszClassName = "COMBOBOX";
    RegisterClassA(&wc);
    /* The same control, told about images and indents. win32 makes this one
     * host the other; here it is the one class answering to both names. */
    wc.lpszClassName = WC_COMBOBOXEXA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = updown_proc;
    wc.lpszClassName = UPDOWN_CLASSA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = progress_proc;
    wc.lpszClassName = PROGRESS_CLASSA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = status_proc;
    wc.lpszClassName = STATUSCLASSNAMEA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = tab_proc;
    wc.lpszClassName = WC_TABCONTROLA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = header_proc;
    wc.lpszClassName = WC_HEADERA;
    RegisterClassA(&wc);
    /* A tree acts on a double click too: it opens the branch under it. */
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = treeview_proc;
    wc.lpszClassName = WC_TREEVIEWA;
    RegisterClassA(&wc);
    /* A list view acts on a double click — it is how a shell opens what you
     * are pointing at — so it asks for one. */
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = listview_proc;
    wc.lpszClassName = WC_LISTVIEWA;
    RegisterClassA(&wc);
    wc.style = 0;
    wc.lpfnWndProc = trackbar_proc;
    wc.lpszClassName = TRACKBAR_CLASSA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = toolbar_proc;
    wc.lpszClassName = TOOLBARCLASSNAMEA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = rebar_proc;
    wc.lpszClassName = REBARCLASSNAMEA;
    RegisterClassA(&wc);

    /* The controls that draw their own bars inside their client area. A
     * window's non-client bars must stay off them, or an edit with
     * WS_VSCROLL would come out with two. */
    ween_class_owns_scroll("EDIT");
    ween_class_owns_scroll("LISTBOX");
    ween_class_owns_scroll("COMBOBOX");
    ween_class_owns_scroll(WC_COMBOBOXEXA);
    ween_class_owns_scroll(WC_TREEVIEWA);
    ween_class_owns_scroll(WC_LISTVIEWA);
}


/* ---- the toolbar ----------------------------------------------------------
 *
 * A row of flat buttons. Flat means no edge at rest: the button only shows one
 * when the pointer is over it (raised) or it is held or checked (sunken, over
 * a dither of white and face). That dither is the same one a scroll-bar track
 * is made of, and it is what says "this is on" without a colour.
 *
 * The metrics are measured off a Windows 2000 shell toolbar: the band is
 * twenty-two tall, an icon sits six pixels in and vertically centred, and the
 * text starts twenty-four in — six, then the sixteen-pixel icon, then two.
 */

#define WEEN_TB_HEIGHT 22
#define WEEN_TB_ICON_X 6
#define WEEN_TB_TEXT_X 24
#define WEEN_TB_PAD_RIGHT 7
#define WEEN_TB_PAD_DROP 5 /* when an arrow half follows the label */
#define WEEN_TB_SEP_W 6
/* The arrow half a drop-down button reserves: thirteen after a label,
 * twelve after an image on its own. Measured on both. */
#define WEEN_TB_DROP_W 13
#define WEEN_TB_DROP_W_ICON 12
#define WEEN_TB_DROP_HOT_W 13 /* and the part of it that comes up when hot */
#define WEEN_TB_DROP_ARROW_W 5 /* and the mark drawn in it */

typedef struct {
    int id;
    int image;
    char *text;
    UINT style;
    UINT state;
    int x, w;  /* filled in by the layout */
    int fixed; /* a width the app set itself, 0 for none */
} ween_tbbutton;

typedef struct {
    HIMAGELIST images;
    HIMAGELIST hot_images; /* the set used for the button under the pointer */
    ween_tbbutton *btn;
    int count, cap;
    int hot;     /* the button under the pointer, -1 for none */
    int pressed; /* the button being held, -1 for none */
    int drop;    /* the held button's arrow half, not its body */
    DWORD ex;    /* TBSTYLE_EX_*: whether a drop-down shows its arrow half */
    int pad_x;   /* what surrounds a label, when the app has said, else 0 */
    int indent;  /* how far in the first button starts */
    int btn_h;   /* how tall a button is, when the app has said, else 0 */
    int keyed;   /* the hot item was put there by the keyboard, not the mouse */
    HWND unfocus; /* what had the focus when the keyboard reached this bar */
} ween_toolbar;

static void toolbar_free(void *p)
{
    ween_toolbar *tb = p;
    for (int i = 0; i < tb->count; i++)
        free(tb->btn[i].text);
    free(tb->btn);
    free(tb);
}

static ween_toolbar *toolbar_of(HWND w)
{
    if (!w->ctl) {
        w->ctl = calloc(1, sizeof(ween_toolbar));
        w->ctl_free = toolbar_free;
        if (w->ctl) {
            ((ween_toolbar *)w->ctl)->hot = -1;
            ((ween_toolbar *)w->ctl)->pressed = -1;
        }
    }
    return w->ctl;
}

/* A label's width without the marker that says which letter is its mnemonic:
 * the '&' is not drawn, so it is not measured either. */
static int tb_text_width(const ween_strike *f, const char *text)
{
    int w = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '&' && p[1]) {
            if (p[1] != '&')
                continue;
            p++;
        }
        w += ween_strike_char_advance(f, (unsigned char)*p);
    }
    return w;
}

/* How tall a button is drawn, and where in the control it sits: a bar told a
 * size uses it and centres it, which is how a menu band's titles come to be
 * a row shorter than the band they are in. */
static int tb_button_h(HWND wnd, const ween_toolbar *tb)
{
    (void)wnd;
    return tb->btn_h ? tb->btn_h : ween_ncm(WEEN_TB_HEIGHT);
}

static int tb_button_y(HWND wnd, const ween_toolbar *tb)
{
    RECT cr;
    if (!tb->btn_h)
        return 0;
    GetClientRect(wnd, &cr);
    return cr.bottom > tb->btn_h ? (cr.bottom - tb->btn_h) / 2 : 0;
}

/* The arrow half a drop-down button reserves — none at all unless the bar
 * has been told to draw the arrows, which is what makes a menu title's whole
 * button the drop-down. */
static int tb_drop_w(const ween_toolbar *tb, const ween_tbbutton *b)
{
    if (!(b->style & TBSTYLE_DROPDOWN) || (b->style & BTNS_WHOLEDROPDOWN) ||
        !(tb->ex & TBSTYLE_EX_DRAWDDARROWS))
        return 0;
    return ween_ncm(b->text ? WEEN_TB_DROP_W : WEEN_TB_DROP_W_ICON);
}

/* Lay the row out left to right; each button keeps its own rectangle so the
 * drawing and the hit-testing cannot disagree. */
static void toolbar_layout(HWND wnd, ween_toolbar *tb)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int x = ween_ncm(tb->indent);
    for (int i = 0; i < tb->count; i++) {
        ween_tbbutton *b = &tb->btn[i];
        b->x = x;
        if (b->style & TBSTYLE_SEP) {
            b->w = ween_ncm(WEEN_TB_SEP_W);
        } else {
            int text = b->text ? tb_text_width(f, b->text) : 0;
            int drop = tb_drop_w(tb, b);
            if (b->fixed) { /* the app said how wide, so that is how wide */
                b->w = b->fixed;
                x += b->w;
                continue;
            }
            if (tb->pad_x && text && b->image < 0) {
                /* Told what surrounds a label, a button is that and no more:
                 * a menu band's titles are their text and sixteen. */
                b->w = text + ween_ncm(tb->pad_x);
            } else if (text) {
                /* A label is followed by seven pixels, or by four when an
                 * arrow half comes after it and takes the rest. */
                b->w = ween_ncm(WEEN_TB_TEXT_X) + text +
                       ween_ncm(drop ? WEEN_TB_PAD_DROP : WEEN_TB_PAD_RIGHT);
            } else {
                /* A button with nothing but an image is not symmetric: the
                 * image keeps its left inset and only two pixels follow it. */
                b->w = ween_ncm(WEEN_TB_ICON_X) + 16 + ween_ncm(2);
            }
            b->w += drop;
        }
        x += b->w;
    }
}

static int toolbar_hit(HWND wnd, ween_toolbar *tb, int x, int y, int *on_arrow)
{
    int top = tb_button_y(wnd, tb), h = tb_button_h(wnd, tb);
    if (on_arrow)
        *on_arrow = 0;
    if (y < top || y >= top + h)
        return -1;
    for (int i = 0; i < tb->count; i++) {
        ween_tbbutton *b = &tb->btn[i];
        int aw = tb_drop_w(tb, b);
        if (b->style & TBSTYLE_SEP)
            continue;
        if (x < b->x || x >= b->x + b->w)
            continue;
        if (aw && on_arrow && x >= b->x + b->w - aw)
            *on_arrow = 1;
        return i;
    }
    return -1;
}

static void toolbar_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_toolbar *tb = toolbar_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int th = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 12;
    int h = tb_button_h(wnd, toolbar_of(wnd));
    int row = tb_button_y(wnd, toolbar_of(wnd));
    UINT ui = (UINT)SendMessageA(wnd, WM_QUERYUISTATE, 0, 0);
    RECT r = ps->rcPaint;
    int ox, oy;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    if (!tb)
        return;
    toolbar_layout(wnd, tb);

    for (int i = 0; i < tb->count; i++) {
        ween_tbbutton *b = &tb->btn[i];
        int bx = ox + b->x, by = oy + row;
        int aw = tb_drop_w(tb, b);
        int enabled = (b->state & TBSTATE_ENABLED) != 0;
        int checked = (b->state & TBSTATE_CHECKED) != 0;
        int held = tb->pressed == i && tb->hot == i;

        if (b->style & TBSTYLE_SEP) {
            /* an etched line three pixels in, which is the middle of the
             * six it takes */
            int sx = bx + 3;
            ween_surface_vline(&top->surface, sx, by + 2, h - 4, WEEN_SHADOW);
            ween_surface_vline(&top->surface, sx + 1, by + 2, h - 4, WEEN_WHITE);
            continue;
        }

        if (checked || held) {
            /* the dither is what says "on"; the edge says which way. Like the
             * hot edge it starts a pixel in, so two buttons side by side
             * share the boundary rather than doubling it. */
            if (checked && !held)
                ween_classic_check_dither(&top->surface, bx + 3, by + 2,
                                          b->w - 4, h - 4);
            /* A pixel wider than the hot edge: this one closes on the far
             * side of the boundary it shares with the next button, where the
             * hot edge stops short of it. Both are measured. */
            ween_classic_edge(&top->surface, bx + 1, by, b->w, h,
                              BDR_SUNKENOUTER, BF_RECT, NULL);
        } else if (tb->hot == i && enabled) {
            /* A hot button in a flat toolbar wears one pixel of edge, not the
             * two a raised border has: white along the top and left, shadow
             * along the bottom and right. And it starts a pixel in from the
             * button, so that two of them side by side share the boundary
             * rather than doubling it.
             *
             * A drop-down button comes up as two of these, the body and the
             * arrow half, meeting in the middle. Drawn as one rect instead,
             * the border runs down through the arrow itself. */
            int hx = bx + 1, hw = b->w - 1;
            ween_classic_edge(&top->surface, hx, by, hw - aw, h,
                              BDR_RAISEDINNER, BF_RECT, NULL);
            if (aw)
                ween_classic_edge(&top->surface, hx + hw - aw, by, aw, h,
                                  BDR_RAISEDINNER, BF_RECT, NULL);
        }

        /* A button that is on moves its content in by one, the same as one
         * being held down does. */
        int shift = (held || checked) ? 1 : 0;
        {   /* the hot set, for the one the pointer is on */
            /* The second set is for the button under the pointer and for
             * one that is on: Windows 2000 drew these grey and swapped to
             * the coloured ones for both. */
            HIMAGELIST from = ((tb->hot == i || checked) && enabled &&
                               tb->hot_images)
                                  ? tb->hot_images
                                  : tb->images;
            /* Three pixels down from the top of the button, and the label a
             * pixel above the middle: neither is centred, and both are where
             * Windows 2000 puts them. */
            /* A button with no label carries its image a pixel further
             * left than one with, though both reserve the same inset when
             * their width is worked out. Measured, like the rest of this. */
            int ix = bx + ween_ncm(WEEN_TB_ICON_X) + (b->text ? 0 : -1) +
                     shift;
            int iy = by + (h - 16) / 2 + shift;
            if (from && b->image >= 0) {
                if (enabled) {
                    ween_imagelist_draw(from, b->image, &top->surface, ix, iy);
                } else {
                    /* Embossed, the same as a dead arrow: the silhouette in
                     * white a pixel down and to the right, then again in
                     * shadow on the spot. Which is why it reaches a pixel
                     * past the image's own cell. */
                    ween_imagelist_draw_mono(from, b->image, &top->surface,
                                             ix + 1, iy + 1, WEEN_WHITE);
                    ween_imagelist_draw_mono(from, b->image, &top->surface, ix,
                                             iy, WEEN_SHADOW);
                }
            }
        }
        if (f && b->text && tb->pad_x && b->image < 0) {
            /* A menu band's title: centred in what surrounds it, and drawn
             * through DrawText so the letter its '&' marks is underlined —
             * or not, which is the window's to say and not the bar's. */
            RECT lr;
            lr.left = b->x + ween_ncm(tb->pad_x) / 2 + shift;
            lr.top = row + shift;
            lr.right = b->x + b->w;
            lr.bottom = row + h;
            SetTextColor(dc, enabled ? GetSysColor(COLOR_BTNTEXT)
                                     : GetSysColor(COLOR_GRAYTEXT));
            DrawTextA(dc, b->text, -1, &lr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                          ((ui & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0));
        } else if (f && b->text) {
            ween_strike_draw(f, &top->surface,
                             bx + ween_ncm(WEEN_TB_TEXT_X) + shift,
                             by + (h - th) / 2 - 1 + shift, b->text,
                             (int)strlen(b->text),
                             enabled ? WEEN_BLACK : WEEN_SHADOW);
        }
        if (aw) {
            /* the arrow half, with a line marking it off from the body */
            int ax = bx + b->w - aw;
            int gx = ax + 4, gy = by + h / 2 - 1;
            /* A dead one is embossed rather than merely greyed: the shape in
             * shadow with a white copy of it a pixel down and to the right,
             * which is how win32 greys any glyph. */
            if (!enabled)
                ween_classic_arrow_down(&top->surface, gx + 1, gy + 1,
                                        WEEN_TB_DROP_ARROW_W, WEEN_WHITE);
            ween_classic_arrow_down(&top->surface, gx, gy,
                                    WEEN_TB_DROP_ARROW_W,
                                    enabled ? WEEN_BLACK : WEEN_SHADOW);
        }
    }
}

static void toolbar_set_hot(HWND wnd, ween_toolbar *tb, int hot)
{
    if (tb->hot == hot)
        return;
    tb->hot = hot;
    InvalidateRect(wnd, NULL, FALSE);
}

/* ---- menu mode ------------------------------------------------------------
 *
 * A toolbar whose drop-down buttons wear no arrow is a menu bar: the whole
 * button is the drop-down, it opens on the press rather than the release, and
 * while one is open the pointer moving onto another title -- or the arrows
 * walking to it -- closes that one and opens this. The menu tracker asks
 * these, which is how it knows what to switch to; the application only sees
 * TBN_DROPDOWN, once per title, and answers it with TrackPopupMenu.
 */
static HWND g_tb_menu;       /* the bar whose drop-down is up, or NULL */
static int g_tb_menu_item;   /* which of its buttons */
static int g_tb_menu_switch; /* the one to open when this one returns */
static int g_tb_menu_keyed;  /* it was opened by the keyboard */

HWND ween_toolbar_menu_bar(void)
{
    return g_tb_menu;
}

int ween_toolbar_menu_item(void)
{
    return g_tb_menu_item;
}

int ween_toolbar_menu_keyed(void)
{
    return g_tb_menu_keyed;
}

void ween_toolbar_menu_switch(int index)
{
    g_tb_menu_switch = index;
}

/* Which drop-down button is at a point in the bar's own coordinates. */
int ween_toolbar_menu_hit(HWND bar, int x, int y)
{
    ween_toolbar *tb = bar ? toolbar_of(bar) : NULL;
    int i;
    if (!tb)
        return -1;
    toolbar_layout(bar, tb);
    i = toolbar_hit(bar, tb, x, y, NULL);
    if (i < 0 || !(tb->btn[i].style & TBSTYLE_DROPDOWN) ||
        !(tb->btn[i].state & TBSTATE_ENABLED))
        return -1;
    return i;
}

/* The next drop-down button along, wrapping — what an arrow key walks. */
int ween_toolbar_menu_step(HWND bar, int from, int dir)
{
    ween_toolbar *tb = bar ? toolbar_of(bar) : NULL;
    int at = from;
    if (!tb || tb->count <= 0)
        return -1;
    for (int n = 0; n < tb->count; n++) {
        at = (at + dir + tb->count) % tb->count;
        if ((tb->btn[at].style & TBSTYLE_DROPDOWN) &&
            (tb->btn[at].state & TBSTATE_ENABLED))
            return at;
    }
    return -1;
}

static void toolbar_notify_dropdown(HWND wnd, ween_toolbar *tb, int i)
{
    NMTOOLBAR nm;
    memset(&nm, 0, sizeof(nm));
    nm.hdr.hwndFrom = wnd;
    nm.hdr.idFrom = (UINT_PTR)wnd->id;
    nm.hdr.code = TBN_DROPDOWN;
    nm.iItem = tb->btn[i].id;
    if (wnd->parent)
        SendMessageA(wnd->parent, WM_NOTIFY, (WPARAM)wnd->id, (LPARAM)&nm);
}

/* Open a title's drop-down and keep opening whichever the tracker moves to,
 * which is what makes the pointer slide from one to the next. */
static void toolbar_dropdown(HWND wnd, ween_toolbar *tb, int i, int keyed)
{
    while (i >= 0 && i < tb->count) {
        tb->pressed = i;
        tb->hot = i;
        tb->drop = 0;
        InvalidateRect(wnd, NULL, FALSE);
        ween_flush_paint(); /* pushed in before the menu covers it */
        g_tb_menu = wnd;
        g_tb_menu_item = i;
        g_tb_menu_switch = -1;
        g_tb_menu_keyed = keyed;
        toolbar_notify_dropdown(wnd, tb, i);
        g_tb_menu = NULL;
        g_tb_menu_keyed = 0;
        i = g_tb_menu_switch;
        keyed = 0;
    }
    tb->pressed = -1;
    InvalidateRect(wnd, NULL, FALSE);
}

static LRESULT toolbar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_toolbar *tb;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        toolbar_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case TB_BUTTONSTRUCTSIZE:
        return 0; /* nothing here depends on the app's struct size */
    case TB_SETBUTTONINFOA: {
        const TBBUTTONINFOA *bi = (const TBBUTTONINFOA *)lp;
        int by_index = bi && (bi->dwMask & TBIF_BYINDEX);
        tb = toolbar_of(wnd);
        for (int i = 0; tb && bi && i < tb->count; i++) {
            /* by command, or by place when the caller says so — which is the
             * only way to name a separator, since it carries no command */
            if (by_index ? i != (int)wp : tb->btn[i].id != (int)wp)
                continue;
            if (bi->dwMask & TBIF_SIZE)
                tb->btn[i].fixed = bi->cx;
            if (bi->dwMask & TBIF_IMAGE)
                tb->btn[i].image = bi->iImage;
            if (bi->dwMask & TBIF_STYLE)
                tb->btn[i].style = bi->fsStyle;
            InvalidateRect(wnd, NULL, FALSE);
            return TRUE;
        }
        return FALSE;
    }
    case TB_SETHOTIMAGELIST:
        tb = toolbar_of(wnd);
        if (tb)
            tb->hot_images = (HIMAGELIST)lp;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case TB_SETEXTENDEDSTYLE: {
        DWORD was;
        tb = toolbar_of(wnd);
        if (!tb)
            return 0;
        was = tb->ex;
        tb->ex = (DWORD)lp;
        InvalidateRect(wnd, NULL, FALSE);
        return (LRESULT)was;
    }
    case TB_GETEXTENDEDSTYLE:
        tb = toolbar_of(wnd);
        return tb ? (LRESULT)tb->ex : 0;
    case TB_SETPADDING: {
        LRESULT was;
        tb = toolbar_of(wnd);
        if (!tb)
            return 0;
        was = MAKELPARAM(tb->pad_x, 0);
        tb->pad_x = (int)(short)LOWORD(lp);
        InvalidateRect(wnd, NULL, FALSE);
        return was;
    }
    case TB_GETPADDING:
        tb = toolbar_of(wnd);
        return tb ? MAKELPARAM(tb->pad_x, 0) : 0;
    case TB_SETBITMAPSIZE:
        /* ween32 takes an image's size from the image list, so this only has
         * to be accepted: what it is for is telling a bar that has no images
         * not to reserve room for them, and a bar with no image list here
         * reserves none anyway. */
        return TRUE;
    case TB_SETINDENT:
        tb = toolbar_of(wnd);
        if (!tb)
            return FALSE;
        tb->indent = (int)wp;
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    case TB_SETBUTTONSIZE:
        tb = toolbar_of(wnd);
        if (!tb)
            return FALSE;
        tb->btn_h = (int)(short)HIWORD(lp);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    case TB_GETHOTITEM:
        tb = toolbar_of(wnd);
        return tb ? tb->hot : -1;
    case TB_SETHOTITEM: {
        int was;
        tb = toolbar_of(wnd);
        if (!tb)
            return -1;
        was = tb->hot;
        tb->keyed = 1; /* asked for, so it is the keyboard's, not the mouse's */
        toolbar_set_hot(wnd, tb, (int)wp);
        return was;
    }
    case TB_MAPACCELERATORA: {
        /* Which button's label marks that letter, if any: what Alt+F has to
         * ask before it can open a menu band's File. */
        int *out = (int *)lp;
        int ch = (int)wp;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        tb = toolbar_of(wnd);
        for (int i = 0; tb && i < tb->count; i++) {
            const char *t = tb->btn[i].text;
            for (const char *p = t; p && *p; p++) {
                int c;
                if (*p != '&' || !p[1])
                    continue;
                if (p[1] == '&') {
                    p++;
                    continue;
                }
                c = p[1] >= 'A' && p[1] <= 'Z' ? p[1] + 32 : p[1];
                if (c == ch) {
                    if (out)
                        *out = i;
                    return TRUE;
                }
                break;
            }
        }
        return FALSE;
    }
    case TB_SETIMAGELIST:
        tb = toolbar_of(wnd);
        if (tb) {
            HIMAGELIST was = tb->images;
            tb->images = (HIMAGELIST)lp;
            InvalidateRect(wnd, NULL, FALSE);
            return (LRESULT)(UINT_PTR)was;
        }
        return 0;
    case TB_ADDBUTTONSA: {
        const TBBUTTON *src = (const TBBUTTON *)lp;
        int n = (int)wp;
        tb = toolbar_of(wnd);
        if (!tb || !src || n <= 0)
            return FALSE;
        if (tb->count + n > tb->cap) {
            int cap = tb->cap ? tb->cap : 8;
            while (cap < tb->count + n)
                cap *= 2;
            ween_tbbutton *grown = realloc(tb->btn, (size_t)cap * sizeof *grown);
            if (!grown)
                return FALSE;
            tb->btn = grown;
            tb->cap = cap;
        }
        for (int i = 0; i < n; i++) {
            ween_tbbutton *b = &tb->btn[tb->count + i];
            memset(b, 0, sizeof(*b));
            b->id = src[i].idCommand;
            b->image = src[i].iBitmap;
            b->style = src[i].fsStyle;
            b->state = src[i].fsState;
            b->text = src[i].iString ? dup_str((const char *)src[i].iString)
                                     : NULL;
        }
        tb->count += n;
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case TB_BUTTONCOUNT:
        tb = toolbar_of(wnd);
        return tb ? tb->count : 0;
    case TB_CHECKBUTTON:
    case TB_ENABLEBUTTON: {
        UINT bit = msg == TB_CHECKBUTTON ? TBSTATE_CHECKED : TBSTATE_ENABLED;
        tb = toolbar_of(wnd);
        for (int i = 0; tb && i < tb->count; i++) {
            if (tb->btn[i].id != (int)wp)
                continue;
            if (LOWORD(lp))
                tb->btn[i].state |= bit;
            else
                tb->btn[i].state &= ~bit;
            InvalidateRect(wnd, NULL, FALSE);
            return TRUE;
        }
        return FALSE;
    }
    case TB_ISBUTTONCHECKED:
    case TB_ISBUTTONENABLED: {
        UINT bit = msg == TB_ISBUTTONCHECKED ? TBSTATE_CHECKED : TBSTATE_ENABLED;
        tb = toolbar_of(wnd);
        for (int i = 0; tb && i < tb->count; i++)
            if (tb->btn[i].id == (int)wp)
                return (tb->btn[i].state & bit) != 0;
        return FALSE;
    }
    case TB_COMMANDTOINDEX:
        tb = toolbar_of(wnd);
        for (int i = 0; tb && i < tb->count; i++)
            if (tb->btn[i].id == (int)wp)
                return i;
        return -1;
    case TB_GETITEMRECT: {
        RECT *out = (RECT *)lp;
        tb = toolbar_of(wnd);
        if (!tb || !out || (int)wp < 0 || (int)wp >= tb->count)
            return FALSE;
        toolbar_layout(wnd, tb);
        out->left = tb->btn[(int)wp].x;
        out->top = tb_button_y(wnd, tb);
        out->right = out->left + tb->btn[(int)wp].w;
        out->bottom = out->top + tb_button_h(wnd, tb);
        return TRUE;
    }
    case TB_AUTOSIZE:
        tb = toolbar_of(wnd);
        if (tb) {
            toolbar_layout(wnd, tb);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme;
        tb = toolbar_of(wnd);
        if (!tb)
            return 0;
        toolbar_layout(wnd, tb);
        toolbar_set_hot(wnd, tb,
                        toolbar_hit(wnd, tb, GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
                                    NULL));
        /* ask to hear when the pointer goes, so the hot button can let go */
        memset(&tme, 0, sizeof(tme));
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = wnd;
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        tb = toolbar_of(wnd);
        if (tb && !tb->keyed)
            toolbar_set_hot(wnd, tb, -1);
        return 0;

    case WM_SETFOCUS:
        /* what had the keyboard before this bar took it, so Escape can put it
         * back where it was */
        tb = toolbar_of(wnd);
        if (tb)
            tb->unfocus = (HWND)wp;
        return 0;
    case WM_KILLFOCUS:
        tb = toolbar_of(wnd);
        if (tb && tb->keyed) { /* the bar is no longer being walked */
            tb->keyed = 0;
            toolbar_set_hot(wnd, tb, -1);
        }
        return 0;
    case WM_KEYDOWN: {
        /* A bar with the keyboard on it is walked: the arrows move along it,
         * Down or Enter opens the title they are on, a letter opens the one
         * it marks, and Escape gives the keyboard back. This is what a menu
         * bar does, and a toolbar is where win32 keeps it. */
        unsigned ch = (unsigned)(lp >> 16) & 0xff;
        int at, to;
        tb = toolbar_of(wnd);
        if (!tb || tb->count <= 0)
            return 0;
        at = tb->hot >= 0 ? tb->hot : 0;
        switch (wp) {
        case VK_LEFT:
        case VK_RIGHT:
            to = ween_toolbar_menu_step(wnd, at, wp == VK_RIGHT ? 1 : -1);
            if (to >= 0) {
                tb->keyed = 1;
                toolbar_set_hot(wnd, tb, to);
            }
            return 0;
        case VK_DOWN:
        case VK_RETURN:
        case VK_SPACE:
            if (tb->hot >= 0 && (tb->btn[at].style & TBSTYLE_DROPDOWN))
                toolbar_dropdown(wnd, tb, at, 1);
            else if (tb->hot >= 0 && wnd->parent)
                SendMessageA(wnd->parent, WM_COMMAND,
                             MAKEWPARAM((WORD)tb->btn[at].id, 0), (LPARAM)wnd);
            return 0;
        case VK_ESCAPE:
        case VK_MENU:
        case VK_F10:
            /* Escape leaves the bar, and so does a second Alt: the underlines
             * go with it and the keyboard goes back where it came from. */
            tb->keyed = 0;
            toolbar_set_hot(wnd, tb, -1);
            SendMessageA(wnd, WM_CHANGEUISTATE,
                         MAKEWPARAM(UIS_SET, UISF_HIDEACCEL), 0);
            if (tb->unfocus)
                SetFocus(tb->unfocus);
            return 0;
        default:
            break;
        }
        if (ch) {
            int hit = -1;
            if (SendMessageA(wnd, TB_MAPACCELERATORA, ch, (LPARAM)&hit) &&
                hit >= 0) {
                tb->keyed = 1;
                toolbar_dropdown(wnd, tb, hit, 1);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int arrow = 0, i;
        tb = toolbar_of(wnd);
        if (!tb)
            return 0;
        toolbar_layout(wnd, tb);
        i = toolbar_hit(wnd, tb, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &arrow);
        if (i < 0 || !(tb->btn[i].state & TBSTATE_ENABLED))
            return 0;
        if ((tb->btn[i].style & TBSTYLE_DROPDOWN) &&
            ((tb->btn[i].style & BTNS_WHOLEDROPDOWN) ||
             !(tb->ex & TBSTYLE_EX_DRAWDDARROWS))) {
            /* a menu title: the whole button is the drop-down, and it opens
             * on the press, not on the release */
            SetFocus(wnd);
            toolbar_dropdown(wnd, tb, i, 0);
            return 0;
        }
        tb->pressed = i;
        tb->hot = i;
        tb->drop = arrow;
        SetCapture(wnd);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        tb = toolbar_of(wnd);
        if (!tb || tb->pressed < 0)
            return 0;
        ReleaseCapture();
        int i = tb->pressed, arrow = tb->drop;
        int still = toolbar_hit(wnd, tb, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), NULL);
        tb->pressed = -1;
        InvalidateRect(wnd, NULL, FALSE);
        if (still != i)
            return 0;
        if (arrow) {
            /* the arrow asks the app for a menu rather than doing the
             * button's job */
            toolbar_notify_dropdown(wnd, tb, i);
            return 0;
        }
        if (tb->btn[i].style & TBSTYLE_CHECK)
            tb->btn[i].state ^= TBSTATE_CHECKED;
        if (wnd->parent)
            SendMessageA(wnd->parent, WM_COMMAND,
                         MAKEWPARAM((WORD)tb->btn[i].id, 0), (LPARAM)wnd);
        return 0;
    }
    case WM_DESTROY:
        if (wnd->ctl) {
            toolbar_free(wnd->ctl);
            wnd->ctl = NULL;
        }
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the rebar ------------------------------------------------------------
 *
 * The bands a shell's toolbars sit in. Each band is a row: an etched line
 * across the top of it, then a gripper — a three-pixel raised bar, inset two
 * from the band's left edge and two from its top and bottom — then an optional
 * label, then the control filling what is left.
 *
 * Measured off a Windows 2000 shell: the etched line is a shadow row with a
 * white one under it, and the content starts ten pixels in from the band's
 * left, which is two, the three of the gripper, then five.
 */

#define WEEN_RB_GRIPPER_W 3
#define WEEN_RB_GRIPPER_X 4     /* from the rebar's left, past its own edge */
#define WEEN_RB_GRIPPER_INSET 2 /* and from the top and bottom of its band */
#define WEEN_RB_CONTENT_X 10
#define WEEN_RB_EDGE_H 2 /* the etched line above each band, and around all */
#define WEEN_RB_LABEL_X 11  /* a band's label, a pixel past its content */
#define WEEN_RB_LABEL_GAP 4 /* and what follows it starts four past that */

typedef struct {
    HWND child;
    char *text;
    UINT style;
    int cx;     /* the width the band asked for, 0 for none */
    int min_h;  /* what the band asked for, or the child's height when it went
                 * in — never re-read from the child, because the layout
                 * resizes the child and the two would chase each other down */
    int x, w;   /* filled in by the layout */
    int y, h;
} ween_rbband;

typedef struct {
    ween_rbband *band;
    int count, cap;
} ween_rebar;

static void rebar_free(void *p)
{
    ween_rebar *rb = p;
    for (int i = 0; i < rb->count; i++)
        free(rb->band[i].text);
    free(rb->band);
    free(rb);
}

static ween_rebar *rebar_of(HWND w)
{
    if (!w->ctl) {
        w->ctl = calloc(1, sizeof(ween_rebar));
        w->ctl_free = rebar_free;
    }
    return w->ctl;
}

/* Stack the bands and put each child where its band says. */
/* Bands share a row until one asks to start a new one — RBBS_BREAK, which is
 * what a shell sets on each of its bars to get the stack it shows. Without it
 * they sit side by side, which is the arrangement a rebar is named for and
 * which ween32 used to ignore: every band went on a row of its own, so an
 * application that never asked for the break looked right here and came out
 * with all its bars on one row on Windows. */
static void rebar_layout(HWND wnd, ween_rebar *rb)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT cr;
    int y = 0, i = 0;
    int edge = (wnd->style & RBS_BANDBORDERS) ? ween_ncm(WEEN_RB_EDGE_H) : 0;
    GetClientRect(wnd, &cr);
    while (i < rb->count) {
        int row_h = 0, n = 0, x = 0, share;
        /* how many bands this row holds, and how tall it is */
        for (int j = i; j < rb->count; j++) {
            if (j > i && (rb->band[j].style & RBBS_BREAK))
                break;
            if (!(rb->band[j].style & RBBS_HIDDEN) &&
                rb->band[j].min_h > row_h)
                row_h = rb->band[j].min_h;
            n++;
        }
        share = n > 1 ? cr.right / n : cr.right;
        for (int j = i; j < i + n; j++) {
            ween_rbband *b = &rb->band[j];
            int content = ween_ncm(WEEN_RB_CONTENT_X);
            b->y = y;
            b->h = (b->style & RBBS_HIDDEN) ? 0 : edge + row_h;
            b->x = x;
            /* the last on the row takes whatever is left of the width */
            b->w = (j == i + n - 1) ? cr.right - x : (b->cx ? b->cx : share);
            x += b->w;
            if (b->text && f)
                content = ween_ncm(WEEN_RB_LABEL_X) +
                          ween_strike_text_width(f, b->text,
                                                 (int)strlen(b->text)) +
                          ween_ncm(WEEN_RB_LABEL_GAP);
            if (b->child)
                MoveWindow(b->child, b->x + content, y + edge,
                           b->w - content - edge, b->h - edge, TRUE);
        }
        y += row_h ? edge + row_h : 0; /* a row of hidden bands takes none */
        i += n;
    }
}

static int rebar_height(HWND wnd, ween_rebar *rb)
{
    /* Each row carries the edge above it; the one under the last is the
     * bottom of the control, so it is counted here rather than by a band. */
    int h = (wnd->style & RBS_BANDBORDERS) ? ween_ncm(WEEN_RB_EDGE_H) : 0;
    rebar_layout(wnd, rb);
    for (int i = 0; i < rb->count; i++)
        if ((i == 0 || (rb->band[i].style & RBBS_BREAK)) &&
            !(rb->band[i].style & RBBS_HIDDEN))
            h += rb->band[i].h;
    return h;
}

static void rebar_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_rebar *rb = rebar_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int th = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 12;
    RECT r = ps->rcPaint;
    int ox, oy;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    if (!rb)
        return;
    rebar_layout(wnd, rb);

    /* The whole control is etched round: shadow then white on the top and
     * left, the other way about on the bottom and right. The bands are then
     * ruled off from each other with the same two lines, so the last one has
     * an edge under it as well as over it. */
    ween_classic_edge(&top->surface, ox, oy, r.right, r.bottom, EDGE_ETCHED,
                      BF_RECT, NULL);

    for (int i = 0; i < rb->count; i++) {
        ween_rbband *b = &rb->band[i];
        int by = oy + b->y;
        int inner = b->h - ween_ncm(WEEN_RB_EDGE_H);
        if (i) { /* the rebar's own top edge is the first band's */
            int e = ween_ncm(WEEN_RB_EDGE_H);
            ween_surface_hline(&top->surface, ox + e, by, r.right - 2 * e,
                               WEEN_SHADOW);
            ween_surface_hline(&top->surface, ox + e, by + 1, r.right - 2 * e,
                               WEEN_WHITE);
        }
        by += ween_ncm(WEEN_RB_EDGE_H);

        if (!(b->style & RBBS_NOGRIPPER)) {
            /* one pixel of raised edge, not two: white down the left and
             * along the top, shadow down the right and along the bottom */
            int gi = ween_ncm(WEEN_RB_GRIPPER_INSET);
            ween_classic_edge(&top->surface, ox + ween_ncm(WEEN_RB_GRIPPER_X),
                              by + gi, ween_ncm(WEEN_RB_GRIPPER_W),
                              inner - 2 * gi, BDR_RAISEDINNER, BF_RECT, NULL);
        }
        if (b->text && f)
            ween_strike_draw(f, &top->surface, ox + ween_ncm(WEEN_RB_LABEL_X),
                             by + (inner - th) / 2 - 1, b->text,
                             (int)strlen(b->text), WEEN_BLACK);
    }
}

static LRESULT rebar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_rebar *rb;
    switch (msg) {
    case WM_COMMAND:
    case WM_NOTIFY:
        /* A rebar is a place to put controls, not something that answers for
         * them: what its bands say goes on to the window that owns the rebar.
         * Without this a toolbar in a band is dead — its buttons send their
         * command to their parent, which is the rebar, and it stops there. */
        if (wnd->parent)
            return SendMessageA(wnd->parent, msg, wp, lp);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        rebar_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case RB_INSERTBANDA: {
        const REBARBANDINFOA *info = (const REBARBANDINFOA *)lp;
        rb = rebar_of(wnd);
        if (!rb || !info)
            return FALSE;
        if (rb->count == rb->cap) {
            int cap = rb->cap ? rb->cap * 2 : 4;
            ween_rbband *grown = realloc(rb->band, (size_t)cap * sizeof *grown);
            if (!grown)
                return FALSE;
            rb->band = grown;
            rb->cap = cap;
        }
        ween_rbband *b = &rb->band[rb->count];
        memset(b, 0, sizeof(*b));
        if (info->fMask & RBBIM_CHILD)
            b->child = info->hwndChild;
        if (info->fMask & RBBIM_STYLE)
            b->style = info->fStyle;
        if ((info->fMask & RBBIM_TEXT) && info->lpText)
            b->text = dup_str(info->lpText);
        /* the height the band keeps: what it asked for, else what the child
         * was when it went in, else a toolbar's worth */
        if (info->fMask & RBBIM_SIZE)
            b->cx = (int)info->cx;
        b->min_h = (info->fMask & RBBIM_CHILDSIZE) ? (int)info->cyMinChild : 0;
        if (!b->min_h && b->child) {
            RECT chr;
            GetClientRect(b->child, &chr);
            b->min_h = chr.bottom;
        }
        if (!b->min_h)
            b->min_h = ween_ncm(WEEN_TB_HEIGHT);
        rb->count++;
        rebar_layout(wnd, rb);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case RB_SHOWBAND: {
        /* a band that is not shown keeps its place but takes no room, which
         * is what makes hiding a toolbar close the gap it was in */
        int i = (int)wp;
        rb = rebar_of(wnd);
        if (!rb || i < 0 || i >= rb->count)
            return FALSE;
        if (lp)
            rb->band[i].style &= ~RBBS_HIDDEN;
        else
            rb->band[i].style |= RBBS_HIDDEN;
        if (rb->band[i].child)
            ShowWindow(rb->band[i].child, lp ? SW_SHOW : SW_HIDE);
        rebar_layout(wnd, rb);
        InvalidateRect(wnd, NULL, TRUE);
        if (wnd->parent) /* the frame lays out again around the new height */
            SendMessageA(wnd->parent, WM_SIZE, 0, 0);
        return TRUE;
    }
    case RB_GETBANDCOUNT:
        rb = rebar_of(wnd);
        return rb ? rb->count : 0;
    case RB_GETBARHEIGHT:
        rb = rebar_of(wnd);
        return rb ? rebar_height(wnd, rb) : 0;
    case WM_SIZE:
        rb = rebar_of(wnd);
        if (rb)
            rebar_layout(wnd, rb);
        return 0;
    case WM_DESTROY:
        if (wnd->ctl) {
            rebar_free(wnd->ctl);
            wnd->ctl = NULL;
        }
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}
