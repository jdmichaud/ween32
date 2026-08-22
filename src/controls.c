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
#define WEEN_SB_TIMER 0x5343524C /* an id an app is unlikely to also pick */

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
    int len = vert ? wnd->h : wnd->w;
    int at = vert ? GET_Y_LPARAM(lp) : GET_X_LPARAM(lp);
    ween_sbstate st = scroll_state(wnd);

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

/* The EDIT's own state: the caret, the anchor a selection runs from (the two
 * are equal when nothing is selected), and whether the caret is showing this
 * half of its blink. */
typedef struct {
    int caret, anchor;
    int caret_on;
} ween_edit;

/* Win2000's default caret blink rate, the one Control Panel's slider sits at
 * in the middle of; win32 apps read it with GetCaretBlinkTime. */
#define WEEN_CARET_BLINK_MS 530
#define WEEN_CARET_TIMER 0x57454549 /* an id an app is unlikely to also pick */

static ween_edit *edit_state(HWND w)
{
    if (!w->ctl)
        w->ctl = calloc(1, sizeof(ween_edit));
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
    return (ween_ex_edge(wnd) ? 1 : 0) + ((sum + 26) / 52) / 2;
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
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        edit_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
        if (wnd->style & WS_DISABLED)
            return 0;
        SetFocus(wnd);
        if (f && e) {
            e->caret = edit_index_at(wnd, GET_X_LPARAM(lp) - edit_margin(wnd));
            e->anchor = e->caret; /* a fresh click starts a new selection */
            edit_show_caret(wnd, e);
            SetCapture(wnd);
        }
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
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        KillTimer(wnd, WEEN_CARET_TIMER);
        if (e)
            e->caret_on = 0;
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

typedef struct {
    char **item;
    int *edge; /* status-bar part right edges, in client coordinates */
    int count, cap, cursel, top;
    int track;  /* combo box: the item the pointer is over, -1 for none */
    int opened; /* combo box: this press is the one that opened the list */
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
        if (!p)
            return -1;
        it->item = p;
        it->cap = cap;
    }
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

static int item_height(HWND w)
{
    const ween_strike *f = w->font ? w->font : ween_gui_font();
    return f ? f->ascent - f->descent : 13;
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

static void listbox_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_items *it = items_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    RECT r = ps->rcPaint;
    int ih = item_height(wnd), ox, oy;
    int sb = (wnd->style & WS_VSCROLL) ? ween_scroll_metric() : 0;
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
        ween_draw_scrollbar(&top->surface, ox + width, oy, sb, r.bottom - r.top, 1,
                            1, it ? it->top : 0, visible, 0,
                            maxpos ? maxpos : (it ? it->count - 1 : 0));
    }
}

static LRESULT listbox_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_items *it;
    switch (msg) {
    case WM_CREATE: {
        /* A list box shows whole items only: it trims its height by whatever
         * is left over. Wine settles on this after two passes — the first
         * before the field border comes off the client area, the second
         * after — so a 62px box with 13px items ends up 43, showing three of
         * them and scrolling for the fourth. */
        int ih = item_height(wnd), edge = 2 * ween_ex_edge(wnd);
        for (int pass = 0; pass < 2; pass++) {
            int client = pass ? wnd->h - edge : wnd->h;
            int rem = ih ? client % ih : 0;
            if (wnd->h > ih && rem)
                wnd->h -= rem;
        }
        return 0;
    }
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
        if ((wnd->style & WS_VSCROLL) && it &&
            GET_X_LPARAM(lp) >= cr.right - ween_scroll_metric()) {
            int visible = cr.bottom / (ih ? ih : 1);
            ween_sbstate st = { it->top, 0, it->count - 1, visible, 1 };
            int grab, pos = sb_click(GET_Y_LPARAM(lp), cr.bottom, &st, &grab);
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
                it->top = sb_clamp(
                    sb_drag(GET_Y_LPARAM(lp), cr.bottom, &st, wnd->drag_offset),
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
        ween_controls_free(wnd);
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
    if (it && it->cursel >= 0 && it->cursel < it->count) {
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        TextOutA(dc, 2, 2, it->item[it->cursel], -1);
    }
}

static int combo_list_height(HWND wnd)
{
    ween_items *it = wnd->ctl;
    int n = it ? it->count : 0;
    if (n > 8)
        n = 8;
    return n * item_height(wnd) + 2;
}

/* Where the dropped list sits, in surface coordinates: directly under the
 * control, its own width. */
static void combo_list_rect(HWND wnd, int *x, int *y, int *w, int *h)
{
    int ox, oy;
    ween_client_origin(wnd, &ox, &oy);
    *x = ox - ween_ex_edge(wnd);
    *y = oy - ween_ex_edge(wnd) + wnd->h;
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

    ween_classic_edge(&top->surface, x, y, w, h, BDR_SUNKENOUTER, BF_RECT, NULL);
    ween_surface_fill(&top->surface, x + 1, y + 1, w - 2, h - 2, WEEN_WINDOWBG);
    for (int i = 0; it && i < it->count; i++) {
        int iy = y + 1 + i * ih;
        int selected = i == (it->track >= 0 ? it->track : it->cursel);
        if (iy + ih > y + h - 1)
            break;
        if (selected)
            ween_surface_fill(&top->surface, x + 1, iy, w - 2, ih, WEEN_CAP_LEFT);
        if (f)
            ween_strike_draw(f, &top->surface, x + 2, iy, it->item[i],
                             (int)strlen(it->item[i]),
                             selected ? WEEN_WHITE : WEEN_BLACK);
    }
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

static LRESULT combo_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_items *it;
    switch (msg) {
    /* The item the pointer is over, or -1. Points arrive relative to our own
     * client area; the list hangs below it. */
    case WM_LBUTTONDOWN: {
        int ox, oy, px, py, pw, ph;
        int sy, ih = item_height(wnd);
        it = items_of(wnd);
        SetFocus(wnd);
        ween_client_origin(wnd, &ox, &oy);
        combo_list_rect(wnd, &px, &py, &pw, &ph);
        sy = oy + GET_Y_LPARAM(lp) - py - 1;
        if (g_dropped == wnd && sy >= 0 && sy < ph - 2) {
            /* pressing in the open list starts tracking it */
            if (it)
                it->track = sy / (ih ? ih : 1);
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
        ween_top_level(wnd)->dirty = 1;
        return 0;
    }
    case WM_MOUSEMOVE: {
        int ox, oy, px, py, pw, ph, sy, ih = item_height(wnd);
        if (GetCapture() != wnd || g_dropped != wnd)
            return 0;
        it = items_of(wnd);
        ween_client_origin(wnd, &ox, &oy);
        combo_list_rect(wnd, &px, &py, &pw, &ph);
        sy = oy + GET_Y_LPARAM(lp) - py - 1;
        if (it) {
            int at = sy >= 0 && sy < ph - 2 ? sy / (ih ? ih : 1) : -1;
            if (at >= it->count)
                at = -1;
            if (at != it->track) {
                it->track = at;
                it->opened = 0; /* a drag into the list commits on release */
                ween_top_level(wnd)->dirty = 1;
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
        ween_client_origin(wnd, &ox, &oy);
        combo_list_rect(wnd, &px, &py, &pw, &ph);
        sy = oy + GET_Y_LPARAM(lp) - py - 1;
        if (sy >= 0 && sy < ph - 2 && it) {
            /* releasing over an item picks it and closes the list */
            int at = sy / (ih ? ih : 1);
            if (at >= 0 && at < it->count) {
                it->cursel = at;
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
        ween_top_level(wnd)->dirty = 1;
        return 0;
    }
    case WM_KILLFOCUS:
        if (g_dropped == wnd) {
            it = wnd->ctl;
            if (it)
                it->track = -1;
            g_dropped = NULL;
            ween_top_level(wnd)->dirty = 1;
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
        wnd->h = item_height(wnd) + 4 + 2 * ween_ex_edge(wnd);
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
    case CB_SETCURSEL:
        it = items_of(wnd);
        if (it)
            it->cursel = (int)wp;
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
    int image; /* index into the view's image list, -1 for none */
} ween_tvitem;

typedef struct {
    HIMAGELIST images; /* the icons items name by index */
    ween_tvitem *root;
    ween_tvitem *sel;
    int scroll_x, content_w; /* horizontal scroll, and what there is to scroll */
    int scroll_row, rows;    /* vertical, counted in items */
} ween_tree;

#define WEEN_TV_ITEM_H 16
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

static void dotted_v(ween_surface *s, int x, int y0, int y1, ween_color c)
{
    /* every other pixel, counted from where the line starts */
    for (int y = y0; y < y1; y += 2)
        ween_surface_pixel(s, x, y, c);
}

static void dotted_h(ween_surface *s, int y, int x0, int x1, ween_color c)
{
    for (int x = x0; x < x1; x += 2)
        ween_surface_pixel(s, x, y, c);
}

/* Draw one level of the tree; returns the row after the last one drawn. */
static int tree_draw(ween_surface *s, const ween_strike *f, ween_tvitem *first,
                     int ox, int oy, int depth, int row, int lines,
                     const ween_tvitem *sel, HIMAGELIST images)
{
    int th = f ? f->ascent - f->descent : 13;
    int icon_w = 0, icon_h = 0;
    if (images)
        ImageList_GetIconSize(images, &icon_w, &icon_h);
    for (ween_tvitem *it = first; it; it = it->next) {
        int y = oy + row * WEEN_TV_ITEM_H;
        int bx = ox + 5 + depth * WEEN_TV_INDENT;
        int cx = bx + WEEN_TV_BUTTON / 2;
        int cy = y + WEEN_TV_ITEM_H / 2;
        int tx = bx + WEEN_TV_BUTTON + 7;

        if (lines) {
            /* the stub out to the text, the run up to the sibling above and
             * the one down to the sibling below */
            dotted_h(s, cy, cx, tx - 3, WEEN_SHADOW);
            if (it != first || depth > 0) /* up to the sibling or the parent */
                dotted_v(s, cx, y, cy, WEEN_SHADOW);
            if (it->next)
                dotted_v(s, cx, cy, y + WEEN_TV_ITEM_H, WEEN_SHADOW);
        }
        if (it->child) {
            /* the button: a grey box with a plus or minus in it */
            ween_surface_rect(s, bx, cy - 4, WEEN_TV_BUTTON, WEEN_TV_BUTTON,
                              WEEN_SHADOW);
            ween_surface_fill(s, bx + 1, cy - 3, WEEN_TV_BUTTON - 2,
                              WEEN_TV_BUTTON - 2, WEEN_WHITE);
            ween_surface_hline(s, bx + 2, cy, WEEN_TV_BUTTON - 4, WEEN_BLACK);
            if (!it->expanded)
                ween_surface_vline(s, cx, cy - 2, WEEN_TV_BUTTON - 4, WEEN_BLACK);
        }
        if (images && it->image >= 0) {
            /* the icon goes between the button and the label, and the label
             * moves over to make room for it */
            ween_imagelist_draw(images, it->image, s, tx,
                                y + (WEEN_TV_ITEM_H - icon_h) / 2);
            tx += icon_w + 2;
        }
        if (f && it->text) {
            int ty = y + (WEEN_TV_ITEM_H - th) / 2;
            int selected = it == sel;
            if (selected) {
                int tw = ween_strike_text_width(f, it->text,
                                                (int)strlen(it->text));
                ween_surface_fill(s, tx - 1, ty, tw + 3, th, WEEN_CAP_LEFT);
            }
            ween_strike_draw(f, s, tx, ty, it->text, (int)strlen(it->text),
                             selected ? WEEN_WHITE : WEEN_BLACK);
        }
        row++;
        if (it->expanded && it->child) {
            int start = row;
            row = tree_draw(s, f, it->child, ox, oy, depth + 1, row, lines, sel,
                            images);
            if (lines) /* the parent's line down past its children */
                dotted_v(s, cx, y + WEEN_TV_ITEM_H,
                         oy + (start + 0) * WEEN_TV_ITEM_H, WEEN_SHADOW);
        }
    }
    return row;
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
static int tree_extent(const ween_strike *f, ween_tvitem *first, int depth)
{
    int max = 0;
    for (ween_tvitem *it = first; it; it = it->next) {
        int w = 5 + depth * WEEN_TV_INDENT + WEEN_TV_BUTTON + 7;
        if (f && it->text)
            w += ween_strike_text_width(f, it->text, (int)strlen(it->text));
        if (w > max)
            max = w;
        if (it->expanded && it->child) {
            int c = tree_extent(f, it->child, depth + 1);
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
    t->content_w = tree_extent(f, t->root, 0) + 8;
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
              (wnd->style & TVS_HASLINES) != 0, t->sel, t->images);
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
        int want = GET_Y_LPARAM(lp) / WEEN_TV_ITEM_H;
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
        {   /* the button toggles, anywhere else selects */
            int bx = 5 + depth * WEEN_TV_INDENT, x = GET_X_LPARAM(lp);
            if (hit->child && x >= bx && x < bx + WEEN_TV_BUTTON) {
                hit->expanded = !hit->expanded;
                notify_parent(wnd, TVN_ITEMEXPANDEDA);
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
        return TRUE;
    }
    case TVM_SETIMAGELIST:
        if ((t = tree_of(wnd))) {
            HIMAGELIST was = t->images;
            t->images = (HIMAGELIST)lp;
            InvalidateRect(wnd, NULL, FALSE);
            return (LRESULT)(UINT_PTR)was;
        }
        return 0;
    case TVM_EXPAND: {
        ween_tvitem *item = (ween_tvitem *)lp;
        if (item)
            item->expanded = (wp & TVE_EXPAND) != 0;
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
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
#define WEEN_LV_ITEM_H 14

typedef struct {
    char *text[4];
    int image; /* index into the view's image list, -1 for none */
} ween_lvrow;

typedef struct {
    HIMAGELIST images;
    char *col[4];
    int width[4], ncol;
    ween_lvrow *row;
    int nrow, caprow, sel;
    int top;      /* the first row drawn: a file list has to scroll */
    int pressed;  /* the header column being held down, -1 for none */
    int sizing;   /* the divider being dragged, -1 for none */
    int size_x0;  /* where the drag started, and the width it started at */
    int size_w0;
} ween_list;

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
        }
    }
    return w->ctl;
}

/* Columns, and every row's cells: the list view owns all of those strings. */
static void list_ctl_free(void *p)
{
    ween_list *l = p;
    for (int c = 0; c < 4; c++)
        free(l->col[c]);
    for (int i = 0; i < l->nrow; i++)
        for (int c = 0; c < 4; c++)
            free(l->row[i].text[c]);
    free(l->row);
    free(l);
}

/* How many rows fit under the header, and how far down the list can go. */
static int lv_visible(HWND wnd)
{
    RECT cr;
    GetClientRect(wnd, &cr);
    int rows = (cr.bottom - WEEN_LV_HEADER_H) / WEEN_LV_ITEM_H;
    return rows > 0 ? rows : 1;
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
    if (y >= WEEN_LV_HEADER_H)
        return -1;
    for (int c = 0; c < l->ncol; c++) {
        edge += l->width[c];
        if (x >= edge - WEEN_LV_DIVIDER && x <= edge + WEEN_LV_DIVIDER)
            return c;
    }
    return -1;
}

static void listview_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_list *l = list_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT r = ps->rcPaint;
    int ox, oy, th = f ? f->ascent - f->descent : 13, x;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
    if (!l)
        return;

    x = 0;
    for (int c = 0; c < l->ncol; c++) {
        int down = c == l->pressed;
        ween_classic_edge(&top->surface, ox + x, oy, l->width[c],
                          WEEN_LV_HEADER_H, down ? EDGE_SUNKEN : EDGE_RAISED,
                          BF_RECT | BF_SOFT | BF_MIDDLE, NULL);
        if (f && l->col[c])
            ween_strike_draw(f, &top->surface, ox + x + 8 + (down ? 1 : 0),
                             oy + (WEEN_LV_HEADER_H - th) / 2 + (down ? 1 : 0),
                             l->col[c], (int)strlen(l->col[c]), WEEN_BLACK);
        x += l->width[c];
    }

    int icon_w = 0, icon_h = 0;
    if (l->images)
        ImageList_GetIconSize(l->images, &icon_w, &icon_h);

    int visible = lv_visible(wnd);
    if (l->nrow > visible) { /* a file list nearly always has more than fits */
        RECT cr;
        int sb = ween_scroll_metric();
        GetClientRect(wnd, &cr);
        ween_sbstate st = lv_sbstate(wnd, l);
        ween_draw_scrollbar(&top->surface, ox + cr.right - sb, oy, sb,
                            cr.bottom, 1, 1, st.pos, st.page, st.min, st.max);
    }
    for (int i = l->top; i < l->nrow && i < l->top + visible; i++) {
        int y = oy + WEEN_LV_HEADER_H + (i - l->top) * WEEN_LV_ITEM_H;
        int selected = i == l->sel - 1; /* sel is 1-based, 0 for none */
        int indent = (l->images && l->row[i].image >= 0) ? icon_w + 2 : 0;
        x = 0;
        if (selected && f && l->row[i].text[0]) {
            /* the label rect: the text inflated five pixels each side, with
             * the focus rectangle drawn over it */
            int tw = ween_strike_text_extent(f, l->row[i].text[0],
                                             (int)strlen(l->row[i].text[0]));
            ween_surface_fill(&top->surface, ox + 2 + indent, y, tw + 10,
                              WEEN_LV_ITEM_H, WEEN_CAP_LEFT);
            ween_surface_focus_rect(&top->surface, ox + 2 + indent, y, tw + 10,
                                    WEEN_LV_ITEM_H);
        }
        if (indent)
            ween_imagelist_draw(l->images, l->row[i].image, &top->surface,
                                ox + 2, y + (WEEN_LV_ITEM_H - icon_h) / 2);
        for (int c = 0; c < l->ncol; c++) {
            /* the first column leaves room for an icon; the rest sit closer */
            if (f && l->row[i].text[c])
                ween_strike_draw(f, &top->surface,
                                 ox + x + (c ? 5 : 7) + (c ? 0 : indent), y + 1,
                                 l->row[i].text[c],
                                 (int)strlen(l->row[i].text[c]),
                                 selected && !c ? WEEN_WHITE : WEEN_BLACK);
            x += l->width[c];
        }
    }
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

static LRESULT listview_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_list *l;
    switch (msg) {
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
        /* keep the selection in view, which is the whole point of moving it */
        if (l->sel) {
            if (l->sel - 1 < l->top)
                lv_scroll_to(wnd, l, l->sel - 1);
            else if (l->sel - 1 >= l->top + lv_visible(wnd))
                lv_scroll_to(wnd, l, l->sel - lv_visible(wnd));
        }
        InvalidateRect(wnd, NULL, FALSE);
        notify_parent(wnd, LVN_ITEMCHANGED);
        return 0;

    case WM_LBUTTONDOWN: {
        int i;
        RECT cr;
        l = list_of(wnd);
        SetFocus(wnd);
        GetClientRect(wnd, &cr);
        if (!l)
            return 0;
        /* the scroll bar down the right, when there is one */
        if (l->nrow > lv_visible(wnd) &&
            GET_X_LPARAM(lp) >= cr.right - ween_scroll_metric()) {
            int grab;
            ween_sbstate st = lv_sbstate(wnd, l);
            int pos = sb_click(GET_Y_LPARAM(lp), cr.bottom, &st, &grab);
            if (grab >= 0) {
                SetCapture(wnd);
                wnd->drag_offset = grab;
            }
            lv_scroll_to(wnd, l, pos);
            return 0;
        }
        /* a press on a header divider drags the column's width instead of
         * pressing the column, which is what a divider is for */
        if (GET_Y_LPARAM(lp) < WEEN_LV_HEADER_H) {
            int d = lv_divider_at(l, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (d >= 0) {
                l->sizing = d;
                l->size_x0 = GET_X_LPARAM(lp);
                l->size_w0 = l->width[d];
                SetCapture(wnd);
                return 0;
            }
        }
        /* a press on the header: it goes down, and the app hears about it on
         * the release, which is how a column is sorted */
        if (GET_Y_LPARAM(lp) < WEEN_LV_HEADER_H) {
            int x = 0;
            for (int c = 0; c < l->ncol; c++) {
                if (GET_X_LPARAM(lp) >= x &&
                    GET_X_LPARAM(lp) < x + l->width[c]) {
                    l->pressed = c;
                    SetCapture(wnd);
                    InvalidateRect(wnd, NULL, FALSE);
                    break;
                }
                x += l->width[c];
            }
            return 0;
        }
        i = (GET_Y_LPARAM(lp) - WEEN_LV_HEADER_H) / WEEN_LV_ITEM_H + l->top;
        if (l && GET_Y_LPARAM(lp) >= WEEN_LV_HEADER_H && i >= 0 && i < l->nrow) {
            l->sel = i + 1;
            InvalidateRect(wnd, NULL, FALSE);
            notify_parent(wnd, LVN_ITEMCHANGED);
        }
        return 0;
    }
    case WM_SETCURSOR:
        /* a resize arrow over a divider, an ordinary one everywhere else */
        l = list_of(wnd);
        if (l && l->sizing >= 0) {
            SetCursor(LoadCursorA(NULL, IDC_SIZEWE));
            return TRUE;
        }
        return DefWindowProcA(wnd, msg, wp, lp);

    case WM_MOUSEMOVE:
        l = list_of(wnd);
        if (l && l->sizing >= 0 && GetCapture() == wnd) {
            int w = l->size_w0 + GET_X_LPARAM(lp) - l->size_x0;
            if (w < WEEN_LV_DIVIDER * 2) /* never smaller than its divider */
                w = WEEN_LV_DIVIDER * 2;
            if (w != l->width[l->sizing]) {
                l->width[l->sizing] = w;
                InvalidateRect(wnd, NULL, FALSE);
            }
            return 0;
        }
        if (l && GetCapture() == wnd && l->pressed < 0) {
            RECT cr;
            ween_sbstate st = lv_sbstate(wnd, l);
            GetClientRect(wnd, &cr);
            lv_scroll_to(wnd, l,
                         sb_drag(GET_Y_LPARAM(lp), cr.bottom, &st,
                                 wnd->drag_offset));
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
        if (!l || !col || i < 0 || i >= 4)
            return -1;
        l->col[i] = dup_str(col->pszText);
        l->width[i] = (col->mask & LVCF_WIDTH) ? col->cx : 50;
        if (i >= l->ncol)
            l->ncol = i + 1;
        InvalidateRect(wnd, NULL, FALSE);
        return i;
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
        l->row[l->nrow].text[0] = dup_str(item->pszText);
        InvalidateRect(wnd, NULL, FALSE);
        return l->nrow++;
    }
    case LVM_SETIMAGELIST:
        l = list_of(wnd);
        if (l) {
            HIMAGELIST was = l->images;
            l->images = (HIMAGELIST)lp;
            InvalidateRect(wnd, NULL, FALSE);
            return (LRESULT)(UINT_PTR)was;
        }
        return 0;
    case LVM_SETITEMTEXTA: {
        const LVITEMA *item = (const LVITEMA *)lp;
        int i = (int)wp;
        l = list_of(wnd);
        if (!l || !item || i < 0 || i >= l->nrow || item->iSubItem >= 4)
            return FALSE;
        free(l->row[i].text[item->iSubItem]);
        l->row[i].text[item->iSubItem] = dup_str(item->pszText);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case LVM_SETITEMSTATE: {
        const LVITEMA *item = (const LVITEMA *)lp;
        l = list_of(wnd);
        /* the selection only shows once the control has been clicked, as an
         * unfocused list view does not paint one */
        if (l && item && (item->state & LVIS_SELECTED) &&
            ween_focus_get() == wnd)
            l->sel = (int)wp + 1;
        return TRUE;
    }
    case LVM_DELETEALLITEMS:
        /* Every navigation empties the list, so this is not optional. */
        l = list_of(wnd);
        if (l) {
            for (int i = 0; i < l->nrow; i++)
                for (int c = 0; c < 4; c++) {
                    free(l->row[i].text[c]);
                    l->row[i].text[c] = NULL;
                }
            l->nrow = 0;
            l->sel = 0;
            l->top = 0;
            InvalidateRect(wnd, NULL, FALSE);
        }
        return TRUE;
    case LVM_GETITEMCOUNT:
        l = list_of(wnd);
        return l ? l->nrow : 0;
    case LVM_GETNEXTITEM:
        /* only the one an app actually asks for: the selected row */
        l = list_of(wnd);
        if (!l || !(lp & LVNI_SELECTED))
            return -1;
        return l->sel ? l->sel - 1 : -1;
    case LVM_SETCOLUMNWIDTH:
        l = list_of(wnd);
        if (!l || (int)wp < 0 || (int)wp >= l->ncol)
            return FALSE;
        l->width[(int)wp] = (int)(short)LOWORD(lp);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
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
    int box = (wnd->style & (TBS_BOTH | TBS_NOTICKS)) != 0;
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
    int box = (wnd->style & (TBS_BOTH | TBS_NOTICKS)) != 0;
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
    int tabh = th + 5, body = 2 + tabh;
    int min = tab_min_width(f);
    int sel = it ? it->cursel : 0;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));

    /* the page below the tabs */
    ween_classic_edge(&top->surface, ox, oy + body, r.right, r.bottom - body,
                      EDGE_RAISED, BF_RECT | BF_SOFT, NULL);

    /* right to left, so each tab's dark edge covers the next one's white */
    for (int pass = 0; pass < 2; pass++) {
        int l = 2;
        for (int i = 0; it && i < it->count; i++) {
            int w = tab_width(f, it->item[i], min);
            int selected = i == sel;
            if ((pass == 1) == selected) {
                int tl = selected ? l - 2 : l;
                int tr = selected ? l + w + 2 : l + w;
                int ty = selected ? 0 : 2;
                int tb = body + (selected ? 1 : 0);
                ween_surface_fill(&top->surface, ox + tl, oy + ty, tr - tl,
                                  tb - ty, WEEN_FACE);
                tab_shape(&top->surface, ox, oy + ty, ox + tl, ox + tr,
                          oy + tb);
                if (f) {
                    int visible = selected ? tabh + 2 : tabh;
                    ween_strike_draw(f, &top->surface, ox + l + 6,
                                     oy + ty + (visible - th) / 2, it->item[i],
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
        int y = GET_Y_LPARAM(lp), tabh = (f ? f->ascent - f->descent : 13) + 5;
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
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the status bar -------------------------------------------------------
 *
 * A strip along the bottom of its parent's client area, divided into parts,
 * each in a status-field border, with the size grip in the corner. */

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
        int right = it->edge[i];
        RECT part;
        if (right < 0 || right > r.right)
            right = r.right;
        part.left = left;
        part.top = r.top;
        part.right = right;
        part.bottom = r.bottom;
        ween_classic_edge(&top->surface, ox + part.left, oy + part.top,
                          part.right - part.left, part.bottom - part.top,
                          BDR_SUNKENOUTER, BF_RECT, NULL);
        part.left += 4; /* the text inset a status bar uses */
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        DrawTextA(dc, it->item[i], -1, &part,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        left = right + 2; /* the gap between parts */
    }
    if (grip) /* drawn over the last part, in the corner */
        ween_classic_sizegrip(&top->surface, ox + r.right - grip, oy + r.top,
                              grip, r.bottom - r.top);
}

static LRESULT status_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_items *it;
    switch (msg) {
    case WM_CREATE:
    case WM_SIZE: {
        /* the strip sits along the bottom of the parent's client area, and
         * follows it when the window is resized — the app forwards WM_SIZE,
         * as a win32 app does */
        const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
        RECT pc;
        wnd->h = (f ? f->ascent - f->descent : 13) + 5;
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
    wc.lpfnWndProc = progress_proc;
    wc.lpszClassName = PROGRESS_CLASSA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = status_proc;
    wc.lpszClassName = STATUSCLASSNAMEA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = tab_proc;
    wc.lpszClassName = WC_TABCONTROLA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = treeview_proc;
    wc.lpszClassName = WC_TREEVIEWA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = listview_proc;
    wc.lpszClassName = WC_LISTVIEWA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = trackbar_proc;
    wc.lpszClassName = TRACKBAR_CLASSA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = toolbar_proc;
    wc.lpszClassName = TOOLBARCLASSNAMEA;
    RegisterClassA(&wc);
    wc.lpfnWndProc = rebar_proc;
    wc.lpszClassName = REBARCLASSNAMEA;
    RegisterClassA(&wc);

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
#define WEEN_TB_SEP_W 8
#define WEEN_TB_DROP_W 11 /* the arrow half of a drop-down button */

typedef struct {
    int id;
    int image;
    char *text;
    UINT style;
    UINT state;
    int x, w; /* filled in by the layout */
} ween_tbbutton;

typedef struct {
    HIMAGELIST images;
    ween_tbbutton *btn;
    int count, cap;
    int hot;     /* the button under the pointer, -1 for none */
    int pressed; /* the button being held, -1 for none */
    int drop;    /* the held button's arrow half, not its body */
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

/* Lay the row out left to right; each button keeps its own rectangle so the
 * drawing and the hit-testing cannot disagree. */
static void toolbar_layout(HWND wnd, ween_toolbar *tb)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int x = 0;
    for (int i = 0; i < tb->count; i++) {
        ween_tbbutton *b = &tb->btn[i];
        b->x = x;
        if (b->style & TBSTYLE_SEP) {
            b->w = ween_ncm(WEEN_TB_SEP_W);
        } else {
            int text = b->text ? ween_strike_text_width(f, b->text,
                                                        (int)strlen(b->text))
                               : 0;
            /* A button with nothing but an image is not symmetric: the
             * image keeps its left inset and only two pixels follow it. */
            b->w = text ? ween_ncm(WEEN_TB_TEXT_X) + text +
                              ween_ncm(WEEN_TB_PAD_RIGHT)
                        : ween_ncm(WEEN_TB_ICON_X) + 16 + ween_ncm(2);
            if (b->style & TBSTYLE_DROPDOWN)
                b->w += ween_ncm(WEEN_TB_DROP_W);
        }
        x += b->w;
    }
}

static int toolbar_hit(ween_toolbar *tb, int x, int y, int *on_arrow)
{
    if (on_arrow)
        *on_arrow = 0;
    if (y < 0 || y >= ween_ncm(WEEN_TB_HEIGHT))
        return -1;
    for (int i = 0; i < tb->count; i++) {
        ween_tbbutton *b = &tb->btn[i];
        if (b->style & TBSTYLE_SEP)
            continue;
        if (x < b->x || x >= b->x + b->w)
            continue;
        if ((b->style & TBSTYLE_DROPDOWN) && on_arrow &&
            x >= b->x + b->w - ween_ncm(WEEN_TB_DROP_W))
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
    int h = ween_ncm(WEEN_TB_HEIGHT);
    RECT r = ps->rcPaint;
    int ox, oy;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    if (!tb)
        return;
    toolbar_layout(wnd, tb);

    for (int i = 0; i < tb->count; i++) {
        ween_tbbutton *b = &tb->btn[i];
        int bx = ox + b->x, by = oy;
        int enabled = (b->state & TBSTATE_ENABLED) != 0;
        int checked = (b->state & TBSTATE_CHECKED) != 0;
        int held = tb->pressed == i && tb->hot == i;

        if (b->style & TBSTYLE_SEP) {
            /* an etched line down the middle of its gap */
            int sx = bx + b->w / 2;
            ween_surface_vline(&top->surface, sx, by + 2, h - 4, WEEN_SHADOW);
            ween_surface_vline(&top->surface, sx + 1, by + 2, h - 4, WEEN_WHITE);
            continue;
        }

        if (checked || held) {
            /* the dither is what says "on"; the edge says which way */
            if (checked && !held)
                ween_classic_scroll_track(&top->surface, bx + 1, by + 1,
                                          b->w - 2, h - 2);
            ween_classic_edge(&top->surface, bx, by, b->w, h, EDGE_SUNKEN,
                              BF_RECT, NULL);
        } else if (tb->hot == i && enabled) {
            ween_classic_edge(&top->surface, bx, by, b->w, h, EDGE_RAISED,
                              BF_RECT, NULL);
        }

        int shift = held ? 1 : 0;
        if (tb->images && b->image >= 0)
            ween_imagelist_draw(tb->images, b->image, &top->surface,
                                bx + ween_ncm(WEEN_TB_ICON_X) + shift,
                                by + (h - 16) / 2 + shift);
        if (f && b->text)
            ween_strike_draw(f, &top->surface,
                             bx + ween_ncm(WEEN_TB_TEXT_X) + shift,
                             by + (h - th) / 2 + shift, b->text,
                             (int)strlen(b->text),
                             enabled ? WEEN_BLACK : WEEN_SHADOW);
        if (b->style & TBSTYLE_DROPDOWN) {
            /* the arrow half, with a line marking it off from the body */
            int ax = bx + b->w - ween_ncm(WEEN_TB_DROP_W);
            ween_classic_menu_arrow_down(&top->surface, ax + 3,
                                         by + h / 2 - 1,
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
    case TB_GETITEMRECT: {
        RECT *out = (RECT *)lp;
        tb = toolbar_of(wnd);
        if (!tb || !out || (int)wp < 0 || (int)wp >= tb->count)
            return FALSE;
        toolbar_layout(wnd, tb);
        out->left = tb->btn[(int)wp].x;
        out->top = 0;
        out->right = out->left + tb->btn[(int)wp].w;
        out->bottom = ween_ncm(WEEN_TB_HEIGHT);
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
                        toolbar_hit(tb, GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
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
        if (tb)
            toolbar_set_hot(wnd, tb, -1);
        return 0;

    case WM_LBUTTONDOWN: {
        int arrow = 0, i;
        tb = toolbar_of(wnd);
        if (!tb)
            return 0;
        toolbar_layout(wnd, tb);
        i = toolbar_hit(tb, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &arrow);
        if (i < 0 || !(tb->btn[i].state & TBSTATE_ENABLED))
            return 0;
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
        int still = toolbar_hit(tb, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), NULL);
        tb->pressed = -1;
        InvalidateRect(wnd, NULL, FALSE);
        if (still != i)
            return 0;
        if (arrow) {
            /* the arrow asks the app for a menu rather than doing the
             * button's job */
            NMTOOLBAR nm;
            memset(&nm, 0, sizeof(nm));
            nm.hdr.hwndFrom = wnd;
            nm.hdr.idFrom = (UINT_PTR)wnd->id;
            nm.hdr.code = TBN_DROPDOWN;
            nm.iItem = tb->btn[i].id;
            if (wnd->parent)
                SendMessageA(wnd->parent, WM_NOTIFY, (WPARAM)wnd->id,
                             (LPARAM)&nm);
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
#define WEEN_RB_GRIPPER_INSET 2
#define WEEN_RB_CONTENT_X 10
#define WEEN_RB_EDGE_H 2 /* the etched line above each band */
#define WEEN_RB_LABEL_GAP 6

typedef struct {
    HWND child;
    char *text;
    UINT style;
    int min_h;  /* what the band asked for, or the child's height when it went
                 * in — never re-read from the child, because the layout
                 * resizes the child and the two would chase each other down */
    int y, h;   /* filled in by the layout */
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
static void rebar_layout(HWND wnd, ween_rebar *rb)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT cr;
    int y = 0;
    GetClientRect(wnd, &cr);
    for (int i = 0; i < rb->count; i++) {
        ween_rbband *b = &rb->band[i];
        RECT chr;
        int content = ween_ncm(WEEN_RB_CONTENT_X);
        (void)chr;
        b->y = y;
        b->h = ween_ncm(WEEN_RB_EDGE_H) + b->min_h;
        if (b->text && f)
            content += ween_strike_text_width(f, b->text,
                                              (int)strlen(b->text)) +
                       ween_ncm(WEEN_RB_LABEL_GAP);
        if (b->child)
            MoveWindow(b->child, content, y + ween_ncm(WEEN_RB_EDGE_H),
                       cr.right - content, b->h - ween_ncm(WEEN_RB_EDGE_H),
                       TRUE);
        y += b->h;
    }
}

static int rebar_height(HWND wnd, ween_rebar *rb)
{
    int h = 0;
    rebar_layout(wnd, rb);
    for (int i = 0; i < rb->count; i++)
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

    for (int i = 0; i < rb->count; i++) {
        ween_rbband *b = &rb->band[i];
        int by = oy + b->y;
        int inner = b->h - ween_ncm(WEEN_RB_EDGE_H);
        /* the etched line across the top of the band */
        ween_surface_hline(&top->surface, ox, by, r.right, WEEN_SHADOW);
        ween_surface_hline(&top->surface, ox, by + 1, r.right, WEEN_WHITE);
        by += ween_ncm(WEEN_RB_EDGE_H);

        if (!(b->style & RBBS_NOGRIPPER)) {
            int gi = ween_ncm(WEEN_RB_GRIPPER_INSET);
            ween_classic_edge(&top->surface, ox + gi, by + gi,
                              ween_ncm(WEEN_RB_GRIPPER_W), inner - 2 * gi,
                              EDGE_RAISED, BF_RECT, NULL);
        }
        if (b->text && f)
            ween_strike_draw(f, &top->surface, ox + ween_ncm(WEEN_RB_CONTENT_X),
                             by + (inner - th) / 2, b->text,
                             (int)strlen(b->text), WEEN_BLACK);
    }
}

static LRESULT rebar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_rebar *rb;
    switch (msg) {
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
