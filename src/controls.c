/* The built-in control classes beyond BUTTON and STATIC.
 *
 * Each one is a port of how the classic control draws itself — Wine's
 * comctl32 for the layout arithmetic, its uitools for the parts — because the
 * point is to land on the same pixels, not merely to look similar. */

#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

/* Non-client edge width of a child window: the two field-border styles. */
int ween_ex_edge(const struct ween_wnd *w)
{
    if (w->ex_style & WS_EX_CLIENTEDGE)
        return 2;
    if (w->ex_style & WS_EX_STATICEDGE)
        return 1;
    return 0;
}

/* Paint that edge. WS_EX_CLIENTEDGE is the field border (sunken outer and
 * inner); WS_EX_STATICEDGE is the status-field border (sunken outer only). */
void ween_paint_ex_edge(struct ween_wnd *w)
{
    struct ween_wnd *top = ween_top_level(w);
    int ox, oy;
    if (!ween_ex_edge(w))
        return;
    ween_client_origin(w, &ox, &oy);
    ox -= ween_ex_edge(w);
    oy -= ween_ex_edge(w);
    if (w->ex_style & WS_EX_CLIENTEDGE)
        ween_classic_edge(&top->surface, ox, oy, w->w, w->h, EDGE_SUNKEN,
                          BF_RECT, NULL);
    else
        ween_classic_edge(&top->surface, ox, oy, w->w, w->h, BDR_SUNKENOUTER,
                          BF_RECT, NULL);
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
        thumb = track * page / (max - min + 1);
        if (thumb < sz)
            thumb = sz;
    }
    if (max - min > 0)
        tpos = (track - thumb) * (pos - min) / (max - min);
    if (vert)
        ween_classic_edge(s, x, y + sz + tpos, w, thumb, EDGE_RAISED,
                          BF_RECT | BF_MIDDLE, NULL);
    else
        ween_classic_edge(s, x + sz + tpos, y, thumb, h, EDGE_RAISED,
                          BF_RECT | BF_MIDDLE, NULL);
}

/* ---- the SCROLLBAR class -------------------------------------------------- */

static LRESULT scrollbar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        struct ween_wnd *top = ween_top_level(wnd);
        int ox, oy;
        BeginPaint(wnd, &ps);
        ween_client_origin(wnd, &ox, &oy);
        ween_draw_scrollbar(&top->surface, ox, oy, wnd->w, wnd->h,
                            (wnd->style & SBS_VERT) != 0, 1, wnd->scroll_pos,
                            wnd->scroll_page, wnd->scroll_min, wnd->scroll_max);
        EndPaint(wnd, &ps);
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
    int inset = ween_ex_edge(wnd) ? 1 : 0;
    int margin = 3;
    if (f) {
        static const char alpha[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        int sum = 0;
        for (int i = 0; i < 52; i++)
            sum += ween_strike_char_advance(f, (unsigned char)alpha[i]);
        margin = ((sum + 26) / 52) / 2;
    }
    int tx = inset + margin;
    int ty = inset;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int n = nl ? (int)(nl - p) : (int)strlen(p);
        if (n && p[n - 1] == '\r')
            n--;
        if (f)
            ween_strike_draw_logical(f, &top->surface, ox + tx, oy + ty, p, n, ink);
        if (!nl || !multi)
            break;
        p = nl + 1;
        ty += line;
    }
}

static LRESULT edit_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        edit_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_SETTEXT:
        InvalidateRect(wnd, NULL, FALSE);
        return DefWindowProcA(wnd, msg, wp, lp);
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- registration --------------------------------------------------------- */

void ween_register_controls(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.hbrBackground = NULL; /* every control paints its own background */
    wc.lpfnWndProc = edit_proc;
    wc.lpszClassName = "EDIT";
    RegisterClassA(&wc);
    wc.lpfnWndProc = scrollbar_proc;
    wc.lpszClassName = "SCROLLBAR";
    RegisterClassA(&wc);
}
