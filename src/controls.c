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

/* ---- item lists (LISTBOX, COMBOBOX) -------------------------------------- */

typedef struct {
    char **item;
    int *edge; /* status-bar part right edges, in client coordinates */
    int count, cap, cursel, top;
} ween_items;

static ween_items *items_of(HWND w)
{
    if (!w->ctl) {
        w->ctl = calloc(1, sizeof(ween_items));
        if (w->ctl)
            ((ween_items *)w->ctl)->cursel = -1;
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

void ween_controls_free(HWND w)
{
    ween_items *it = w->ctl;
    if (!it)
        return;
    for (int i = 0; i < it->count; i++)
        free(it->item[i]);
    free(it->item);
    free(it->edge);
    free(it);
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

static LRESULT combo_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_items *it;
    switch (msg) {
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
    case WM_CREATE: {
        /* the strip sizes itself to the bottom of the parent's client area */
        const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
        RECT pc;
        wnd->h = (f ? f->ascent - f->descent : 13) + 5;
        if (wnd->parent && GetClientRect(wnd->parent, &pc)) {
            wnd->w = pc.right;
            wnd->x = 0;
            wnd->y = pc.bottom - wnd->h;
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        status_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
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
}

