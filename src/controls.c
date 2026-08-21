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

/* ---- the tree view --------------------------------------------------------
 *
 * Items on 16-pixel rows, each level indented 19; a 9x9 button with a plus or
 * minus where an item has children, and dotted lines — every other pixel —
 * joining a parent to its children. */

typedef struct ween_tvitem {
    char *text;
    struct ween_tvitem *parent, *child, *next;
    int expanded;
} ween_tvitem;

typedef struct {
    ween_tvitem *root;
} ween_tree;

#define WEEN_TV_ITEM_H 16
#define WEEN_TV_INDENT 19
#define WEEN_TV_BUTTON 9

static ween_tree *tree_of(HWND w)
{
    if (!w->ctl)
        w->ctl = calloc(1, sizeof(ween_tree));
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

static void dotted_v(ween_surface *s, int x, int y0, int y1, ween_color c)
{
    for (int y = y0; y < y1; y++)
        if (!((x + y) & 1))
            ween_surface_pixel(s, x, y, c);
}

static void dotted_h(ween_surface *s, int y, int x0, int x1, ween_color c)
{
    for (int x = x0; x < x1; x++)
        if (!((x + y) & 1))
            ween_surface_pixel(s, x, y, c);
}

/* Draw one level of the tree; returns the row after the last one drawn. */
static int tree_draw(ween_surface *s, const ween_strike *f, ween_tvitem *first,
                     int ox, int oy, int depth, int row, int lines)
{
    int th = f ? f->ascent - f->descent : 13;
    for (ween_tvitem *it = first; it; it = it->next) {
        int y = oy + row * WEEN_TV_ITEM_H;
        int bx = ox + 5 + depth * WEEN_TV_INDENT;
        int cx = bx + WEEN_TV_BUTTON / 2;
        int cy = y + WEEN_TV_ITEM_H / 2 - 1;
        int tx = bx + WEEN_TV_BUTTON + 7;

        if (lines) {
            /* the stub joining this item to its parent's line, and the run
             * down to the sibling below */
            dotted_h(s, cy, cx, tx - 3, WEEN_SHADOW);
            if (it->next)
                dotted_v(s, cx, y, y + WEEN_TV_ITEM_H, WEEN_SHADOW);
            else
                dotted_v(s, cx, y, cy + 1, WEEN_SHADOW);
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
        if (f && it->text)
            ween_strike_draw(f, s, tx, y + (WEEN_TV_ITEM_H - th) / 2 - 1,
                             it->text, (int)strlen(it->text), WEEN_BLACK);
        row++;
        if (it->expanded && it->child) {
            int start = row;
            row = tree_draw(s, f, it->child, ox, oy, depth + 1, row, lines);
            if (lines) /* the parent's line down past its children */
                dotted_v(s, cx, y + WEEN_TV_ITEM_H,
                         oy + (start + 0) * WEEN_TV_ITEM_H, WEEN_SHADOW);
        }
    }
    return row;
}

static void treeview_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    ween_tree *t = tree_of(wnd);
    struct ween_wnd *top = ween_top_level(wnd);
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT r = ps->rcPaint;
    int ox, oy;

    ween_client_origin(wnd, &ox, &oy);
    FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
    if (t && t->root)
        tree_draw(&top->surface, f, t->root, ox, oy, 0, 0,
                  (wnd->style & TVS_HASLINES) != 0);
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
    case TVM_INSERTITEMA: {
        const TVINSERTSTRUCTA *is = (const TVINSERTSTRUCTA *)lp;
        ween_tvitem *item, **link;
        size_t n;
        if (!is || !(t = tree_of(wnd)))
            return 0;
        item = calloc(1, sizeof(*item));
        if (!item)
            return 0;
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
    case TVM_EXPAND: {
        ween_tvitem *item = (ween_tvitem *)lp;
        if (item)
            item->expanded = (wp & TVE_EXPAND) != 0;
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case WM_DESTROY:
        t = wnd->ctl;
        if (t) {
            tree_free(t->root);
            free(t);
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
} ween_lvrow;

typedef struct {
    char *col[4];
    int width[4], ncol;
    ween_lvrow *row;
    int nrow, caprow;
} ween_list;

static ween_list *list_of(HWND w)
{
    if (!w->ctl)
        w->ctl = calloc(1, sizeof(ween_list));
    return w->ctl;
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
        ween_classic_edge(&top->surface, ox + x, oy, l->width[c],
                          WEEN_LV_HEADER_H, EDGE_RAISED,
                          BF_RECT | BF_SOFT | BF_MIDDLE, NULL);
        if (f && l->col[c])
            ween_strike_draw(f, &top->surface, ox + x + 7,
                             oy + (WEEN_LV_HEADER_H - th) / 2, l->col[c],
                             (int)strlen(l->col[c]), WEEN_BLACK);
        x += l->width[c];
    }

    for (int i = 0; i < l->nrow; i++) {
        int y = oy + WEEN_LV_HEADER_H + i * WEEN_LV_ITEM_H;
        x = 0;
        for (int c = 0; c < l->ncol; c++) {
            if (f && l->row[i].text[c])
                ween_strike_draw(f, &top->surface, ox + x + 7,
                                 y + (WEEN_LV_ITEM_H - th) / 2, l->row[i].text[c],
                                 (int)strlen(l->row[i].text[c]), WEEN_BLACK);
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
        l->row[l->nrow].text[0] = dup_str(item->pszText);
        InvalidateRect(wnd, NULL, FALSE);
        return l->nrow++;
    }
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
    case LVM_SETITEMSTATE:
        return TRUE; /* the selection only shows when the control has focus */
    case WM_DESTROY:
        l = wnd->ctl;
        if (l) {
            for (int c = 0; c < 4; c++)
                free(l->col[c]);
            for (int i = 0; i < l->nrow; i++)
                for (int c = 0; c < 4; c++)
                    free(l->row[i].text[c]);
            free(l->row);
            free(l);
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

static void trackbar_thumb(ween_surface *s, int x, int y, int vert)
{
    int w = WEEN_TB_THUMB_W, body = WEEN_TB_THUMB_H - 5;
    if (!vert) {
        /* body */
        ween_surface_hline(s, x, y, w - 1, WEEN_WHITE);
        ween_surface_vline(s, x, y, body, WEEN_WHITE);
        ween_surface_fill(s, x + 1, y + 1, w - 3, body - 1, WEEN_FACE);
        ween_surface_vline(s, x + w - 2, y + 1, body - 1, WEEN_SHADOW);
        ween_surface_vline(s, x + w - 1, y, body + 1, WEEN_DKSHADOW);
        /* the point */
        for (int i = 0; i < 5; i++) {
            int l = x + 1 + i, r = x + w - 2 - i;
            int py = y + body + i;
            if (l > r)
                break;
            ween_surface_pixel(s, l, py, WEEN_WHITE);
            ween_surface_fill(s, l + 1, py, r - l - 1, 1, WEEN_FACE);
            ween_surface_pixel(s, r, py, WEEN_SHADOW);
            ween_surface_pixel(s, r + 1, py, WEEN_DKSHADOW);
        }
    } else {
        ween_surface_vline(s, x, y, w - 1, WEEN_WHITE);
        ween_surface_hline(s, x, y, body, WEEN_WHITE);
        ween_surface_fill(s, x + 1, y + 1, body - 1, w - 3, WEEN_FACE);
        ween_surface_hline(s, x + 1, y + w - 2, body - 1, WEEN_SHADOW);
        ween_surface_hline(s, x, y + w - 1, body + 1, WEEN_DKSHADOW);
        for (int i = 0; i < 5; i++) {
            int t = y + 1 + i, b = y + w - 2 - i;
            int pxx = x + body + i;
            if (t > b)
                break;
            ween_surface_pixel(s, pxx, t, WEEN_WHITE);
            ween_surface_fill(s, pxx, t + 1, 1, b - t - 1, WEEN_FACE);
            ween_surface_pixel(s, pxx, b, WEEN_SHADOW);
            ween_surface_pixel(s, pxx, b + 1, WEEN_DKSHADOW);
        }
    }
}

static void trackbar_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    struct ween_wnd *top = ween_top_level(wnd);
    RECT r = ps->rcPaint;
    int vert = (wnd->style & TBS_VERT) != 0;
    int ox, oy, half = WEEN_TB_THUMB_W / 2;
    int min = wnd->scroll_min, max = wnd->scroll_max, pos = wnd->scroll_pos;
    int len = vert ? r.bottom - r.top : r.right - r.left;
    int chan0 = 8, chan1 = len - 8; /* the channel's ends */
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
                int h = (i == min || i == max) ? 4 : 3;
                ween_surface_vline(&top->surface, tx, oy + 25, h, WEEN_DKSHADOW);
            }
        trackbar_thumb(&top->surface, ox + at - half, oy + 2, 0);
    } else {
        ween_classic_edge(&top->surface, ox + 9, oy + chan0, WEEN_TB_CHANNEL,
                          chan1 - chan0, EDGE_SUNKEN, BF_RECT, NULL);
        if (!(wnd->style & TBS_NOTICKS))
            for (int i = min; i <= max; i++) {
                int ty = oy + chan0 + half + span * (i - min) / (max - min);
                int h = (i == min || i == max) ? 4 : 3;
                ween_surface_hline(&top->surface, ox + 25, ty, h, WEEN_DKSHADOW);
            }
        trackbar_thumb(&top->surface, ox + 2, oy + at - half, 1);
    }
}

static LRESULT trackbar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
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
}

