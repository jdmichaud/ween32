/* The rich edit control, plain text.
 *
 * A second text control rather than a widened EDIT, because the two differ
 * from the first character: an EDIT keeps one string in the window's own
 * text and draws it in one font, and a rich edit keeps a document -- runs of
 * characters carrying their own formatting, paragraphs carrying theirs --
 * and draws every run in the font it says. Nothing of that is here yet; what
 * is here is the plain-text control WordPad's editor stands on, and the
 * shape it is written in is the shape the runs will need.
 *
 * What that means in practice, and why it is not a copy of controls.c:
 *
 *   - The text is the control's own, not the window's. WM_SETTEXT,
 *     WM_GETTEXT and WM_GETTEXTLENGTH are answered here, which is what
 *     GetWindowTextA goes through, so a program cannot tell the difference.
 *   - There is a line table -- every line's start, length, top and height --
 *     rebuilt in one pass whenever the text changes. An EDIT can multiply a
 *     row by one line height because every line is the same height; a rich
 *     edit cannot, the moment a run carries a size of its own. With one font
 *     the table says exactly what the multiplication would have said, and it
 *     is already the general thing.
 *   - What a line *is* is not decided twice. ween_text_line_* in controls.c
 *     is the one answer to where a line starts and how long it is, CRLF and
 *     a bare LF both counting as one break, and the messages that ask about
 *     a line are answered from it. tests/richedit_test.c holds the table and
 *     those functions against each other on the same text, so the two cannot
 *     drift apart in silence.
 *   - The behaviours are the EDIT's -- the blink, the anchor, shift
 *     extending a selection, a double click taking a word, hiding the
 *     selection when the keyboard leaves unless ES_NOHIDESEL -- and they are
 *     written again rather than shared, because sharing them would mean
 *     handing them a way to ask "how long is the text, what is at this
 *     offset, where does this line begin" for a model that is about to
 *     change. The tests are what keep them the same: richedit_test.c asserts
 *     the same behaviours in the same words as edit_test.c.
 *
 * One thing the two do not share at all, and it is deliberate: a rich edit
 * sends no notification until a program asks for it. An EDIT tells its
 * parent EN_CHANGE whether or not anybody wanted to hear; a rich edit starts
 * with an event mask of ENM_NONE and says nothing until EM_SETEVENTMASK.
 */

#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

/* ---- the control's state -------------------------------------------------
 *
 * The document is a byte buffer for now. When runs arrive they go beside it
 * -- an array of (start, CHARFORMAT) -- rather than inside it, so that the
 * text stays one contiguous thing to search and to hand out. */

struct rich_line {
    int start;  /* offset of the line's first character */
    int len;    /* its length, not counting the break */
    int top;    /* pixels from the top of the document */
    int height; /* pixels, which one font makes all the same */
};

typedef struct {
    char *text;
    int len, cap;
    int caret, anchor; /* equal when nothing is selected */
    int caret_on;      /* whether this half of the blink is showing */
    int modified;
    int limit;    /* the most characters it will hold, 0 for no limit */
    DWORD events; /* which notifications the program asked for */
    char *undo;   /* one step, as an edit control keeps */
    int undo_caret;
    int first_visible; /* the top line drawn */
    int sb_grab;       /* where in the thumb a drag took hold, or -1 */
    /* Where a walk up or down the lines set out from, in pixels along the
     * line. A rich edit remembers it: the machine's, asked with
     * tools/vm/ctlprobe.c, walks down from twelve characters into a long
     * line, through a five-character line, and comes out at the pixel it
     * started at -- and two presses of Up bring it back to the very
     * character it left. An EDIT does not; it takes the pixel from wherever
     * the caret is now, and the two really do differ. Anything that is not
     * a vertical move forgets it. */
    int goal_x, goal_set;
    struct rich_line *line;
    int lines, line_cap;
} ween_rich;

static void rich_free(void *p)
{
    ween_rich *e = p;
    if (!e)
        return;
    free(e->text);
    free(e->undo);
    free(e->line);
    free(e);
}

static ween_rich *rich_state(HWND w)
{
    if (!w->ctl) {
        ween_rich *e = calloc(1, sizeof(ween_rich));
        if (e) {
            e->sb_grab = -1;
            e->text = calloc(1, 1); /* always a string, never NULL */
            e->cap = 1;
        }
        w->ctl = e;
        w->ctl_free = rich_free;
    }
    return w->ctl;
}

static int rich_reserve(ween_rich *e, int len)
{
    int cap;
    char *grown;
    if (len + 1 <= e->cap)
        return 1;
    cap = e->cap ? e->cap : 32;
    while (cap < len + 1)
        cap *= 2;
    grown = realloc(e->text, (size_t)cap);
    if (!grown)
        return 0; /* the old text is still there and still valid */
    e->text = grown;
    e->cap = cap;
    return 1;
}

/* ---- the line table ------------------------------------------------------
 *
 * One pass over the text. A break is CRLF or a bare LF and counts as one
 * either way, and a text ending in a break has an empty line after it --
 * both of which ween_text_line_count says too, and the test says so. */

static int rich_line_height(HWND wnd)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    return f ? f->ascent - f->descent : 13;
}

static void rich_relines(HWND wnd, ween_rich *e)
{
    int height = rich_line_height(wnd);
    int at = 0, top = 0, n = 0;
    if (!e)
        return;
    for (;;) {
        int start = at, len;
        while (at < e->len && e->text[at] != '\n')
            at++;
        len = at - start;
        if (len && e->text[start + len - 1] == '\r')
            len--; /* the break is not part of the line */
        if (n >= e->line_cap) {
            int cap = e->line_cap ? e->line_cap * 2 : 32;
            struct rich_line *grown =
                realloc(e->line, (size_t)cap * sizeof *grown);
            if (!grown)
                break; /* out of memory: what is measured so far stands */
            e->line = grown;
            e->line_cap = cap;
        }
        e->line[n].start = start;
        e->line[n].len = len;
        e->line[n].top = top;
        e->line[n].height = height;
        top += height;
        n++;
        if (at >= e->len)
            break;
        at++; /* past the '\n' */
    }
    e->lines = n;
}

/* Which line an offset is on, from the table. */
static int rich_line_of(const ween_rich *e, int at)
{
    int lo = 0, hi = e->lines - 1;
    if (!e->lines)
        return 0;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (e->line[mid].start <= at)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

/* ---- the text ------------------------------------------------------------ */

static void rich_range(const ween_rich *e, int *from, int *to)
{
    *from = e->caret < e->anchor ? e->caret : e->anchor;
    *to = e->caret < e->anchor ? e->anchor : e->caret;
}

/* Keep what is there, so that the change about to be made can be taken back.
 * One step is what a text control keeps, and undoing swaps the two. */
static void rich_remember(ween_rich *e)
{
    char *copy = malloc((size_t)e->len + 1);
    if (!copy)
        return; /* out of memory loses the undo, not the edit */
    memcpy(copy, e->text, (size_t)e->len + 1);
    free(e->undo);
    e->undo = copy;
    e->undo_caret = e->caret;
}

/* The parent hears, if it asked to. */
static void rich_notify(HWND wnd, UINT code, DWORD mask)
{
    ween_rich *e = rich_state(wnd);
    if (!wnd->parent || !e || !(e->events & mask))
        return;
    SendMessageA(wnd->parent, WM_COMMAND, MAKEWPARAM((WORD)wnd->id, code),
                 (LPARAM)wnd);
}

static void rich_changed(HWND wnd, ween_rich *e)
{
    e->modified = 1;
    rich_relines(wnd, e);
    /* EN_UPDATE is before the drawing and EN_CHANGE after it, as win32
     * orders them; both wait on the mask. */
    rich_notify(wnd, EN_UPDATE, ENM_UPDATE);
    rich_notify(wnd, EN_CHANGE, ENM_CHANGE);
}

static int rich_delete_range(HWND wnd, ween_rich *e, int from, int to)
{
    if (from >= to)
        return 0;
    memmove(e->text + from, e->text + to, (size_t)(e->len - to) + 1);
    e->len -= to - from;
    e->caret = e->anchor = from;
    rich_relines(wnd, e);
    return 1;
}

static int rich_delete_selection(HWND wnd, ween_rich *e)
{
    int from, to;
    rich_range(e, &from, &to);
    return rich_delete_range(wnd, e, from, to);
}

static void rich_insert(HWND wnd, ween_rich *e, const char *text)
{
    int n = (int)strlen(text);
    if (!n)
        return;
    if (e->limit && e->len + n > e->limit) {
        n = e->limit - e->len;
        if (n <= 0) {
            rich_notify(wnd, EN_MAXTEXT, ENM_CHANGE);
            return;
        }
    }
    if (!rich_reserve(e, e->len + n))
        return;
    memmove(e->text + e->caret + n, e->text + e->caret,
            (size_t)(e->len - e->caret) + 1);
    memcpy(e->text + e->caret, text, (size_t)n);
    e->len += n;
    e->caret += n;
    e->anchor = e->caret;
    rich_relines(wnd, e);
}

static int rich_set_text(HWND wnd, ween_rich *e, const char *text)
{
    int n = text ? (int)strlen(text) : 0;
    if (!rich_reserve(e, n))
        return 0;
    if (n)
        memcpy(e->text, text, (size_t)n);
    e->text[n] = 0;
    e->len = n;
    e->caret = e->anchor = 0;
    e->first_visible = 0;
    rich_relines(wnd, e);
    return 1;
}

static char *rich_selected_text(ween_rich *e)
{
    int from, to;
    char *out;
    rich_range(e, &from, &to);
    if (from >= to)
        return NULL;
    out = malloc((size_t)(to - from) + 1);
    if (!out)
        return NULL;
    memcpy(out, e->text + from, (size_t)(to - from));
    out[to - from] = 0;
    return out;
}

static int rich_is_word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* A double click takes the word under it, and the run of spaces if it landed
 * on one -- which is what the EDIT does and what the machine does. */
static void rich_select_word(ween_rich *e)
{
    int from = e->caret, to = e->caret;
    if (!e->len)
        return;
    if (from >= e->len)
        from = e->len - 1;
    if (rich_is_word_char(e->text[from])) {
        while (from > 0 && rich_is_word_char(e->text[from - 1]))
            from--;
        while (to < e->len && rich_is_word_char(e->text[to]))
            to++;
    } else {
        while (from > 0 && !rich_is_word_char(e->text[from - 1]) &&
               e->text[from - 1] != '\n' && e->text[from - 1] != '\r')
            from--;
        while (to < e->len && !rich_is_word_char(e->text[to]) &&
               e->text[to] != '\n' && e->text[to] != '\r')
            to++;
    }
    e->anchor = from;
    e->caret = to;
}

/* ---- geometry ------------------------------------------------------------ */

/* A rich edit's text starts one pixel inside its border, as an edit's does
 * in a strike font; see edit_margin in controls.c for the measurement. */
static int rich_inset(HWND wnd)
{
    return ween_border_width(wnd) ? 1 : 0;
}

static int rich_bar(HWND wnd)
{
    return (wnd->style & WS_VSCROLL) ? ween_scroll_metric() : 0;
}

static int rich_visible_lines(HWND wnd)
{
    RECT r;
    int line = rich_line_height(wnd), inset = rich_inset(wnd), rows;
    GetClientRect(wnd, &r);
    rows = line > 0 ? (r.bottom - r.top - 2 * inset) / line : 0;
    return rows > 0 ? rows : 1;
}

static ween_sbstate rich_sbstate(HWND wnd)
{
    ween_rich *e = rich_state(wnd);
    ween_sbstate st;
    st.pos = e ? e->first_visible : 0;
    st.min = 0;
    st.max = e && e->lines ? e->lines - 1 : 0;
    st.page = rich_visible_lines(wnd);
    st.line = 1;
    return st;
}

/* The offset a point lands on. x is client, already past the margin. */
static int rich_index_at_point(HWND wnd, ween_rich *e, int x, int y)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int line = rich_line_height(wnd), inset = rich_inset(wnd);
    int row, i, best = 0, bestd = -1;
    if (!e || !e->lines)
        return 0;
    row = (y - inset) / (line > 0 ? line : 13) + e->first_visible;
    if (row < 0)
        row = 0;
    if (row >= e->lines)
        row = e->lines - 1;
    if (!f)
        return e->line[row].start;
    for (i = 0; i <= e->line[row].len; i++) {
        int px = ween_strike_pen(f, e->text + e->line[row].start, i);
        int d = px - x;
        if (d < 0)
            d = -d;
        if (bestd < 0 || d < bestd) {
            bestd = d;
            best = e->line[row].start + i;
        }
    }
    return best;
}

/* How far along its line the caret stands, in pixels. */
static int rich_caret_x(HWND wnd, ween_rich *e)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int row = rich_line_of(e, e->caret);
    if (!f)
        return e->caret - e->line[row].start;
    return ween_strike_pen(f, e->text + e->line[row].start,
                           e->caret - e->line[row].start);
}

/* The character on a line that stands nearest a pixel. */
static int rich_index_at_x(HWND wnd, ween_rich *e, int row, int x)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int i, best = 0, bestd = 1 << 30;
    if (!f)
        return e->line[row].start +
               (x > e->line[row].len ? e->line[row].len : x);
    for (i = 0; i <= e->line[row].len; i++) {
        int pen = ween_strike_pen(f, e->text + e->line[row].start, i);
        int d = pen > x ? pen - x : x - pen;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return e->line[row].start + best;
}

/* Bring the caret into view, scrolling by lines the way win32 does. */
static int rich_scroll_into_view(HWND wnd, ween_rich *e)
{
    int rows = rich_visible_lines(wnd), row, top = e->first_visible;
    if (!(wnd->style & ES_MULTILINE) || !e->lines)
        return 0;
    row = rich_line_of(e, e->caret);
    if (row < top)
        top = row;
    else if (row >= top + rows)
        top = row - rows + 1;
    if (top < 0)
        top = 0;
    if (top == e->first_visible)
        return 0;
    e->first_visible = top;
    return 1;
}

static void rich_show_caret(HWND wnd, ween_rich *e)
{
    e->caret_on = 1; /* a caret that has just moved is not blinking off */
    rich_scroll_into_view(wnd, e);
}

/* ---- drawing ------------------------------------------------------------- */

static void rich_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    struct ween_wnd *top = ween_top_level(wnd);
    ween_rich *e = rich_state(wnd);
    RECT r = ps->rcPaint, cr;
    int ox, oy, inset = rich_inset(wnd), sb = rich_bar(wnd);
    int line = rich_line_height(wnd);
    int from = 0, to = 0, row;
    ween_color ink = (wnd->style & WS_DISABLED) ? WEEN_SHADOW : WEEN_BLACK;

    ween_client_origin(wnd, &ox, &oy);
    GetClientRect(wnd, &cr);
    FillRect(dc, &r, GetSysColorBrush((wnd->style & WS_DISABLED) ||
                                              (wnd->style & ES_READONLY)
                                          ? COLOR_BTNFACE
                                          : COLOR_WINDOW));
    if (!e || !f)
        return;

    /* A control hides its selection when the keyboard leaves it, unless it
     * was made with ES_NOHIDESEL -- which WordPad's editor is, so that what
     * its Find box has just found stays visible while the box has the
     * keyboard. */
    if (ween_focus_get() == wnd || (wnd->style & ES_NOHIDESEL))
        rich_range(e, &from, &to);

    for (row = e->first_visible; row < e->lines; row++) {
        int y = inset + (row - e->first_visible) * line;
        int x = inset;
        const char *p = e->text + e->line[row].start;
        int n = e->line[row].len;
        int a = from - e->line[row].start, b = to - e->line[row].start;
        if (y + line > cr.bottom - inset && (wnd->style & ES_MULTILINE))
            break;
        if (a < 0)
            a = 0;
        if (b > n)
            b = n;
        if (a >= b) {
            ween_strike_draw(f, &top->surface, ox + x, oy + y, p, n, ink);
        } else {
            /* the selected run sits on a highlight bar, in its colour, on
             * every line it crosses */
            int xa = ween_strike_pen(f, p, a);
            int xb = ween_strike_pen(f, p, b);
            ween_strike_draw(f, &top->surface, ox + x, oy + y, p, a, ink);
            ween_surface_fill(&top->surface, ox + x + xa, oy + y, xb - xa,
                              line, WEEN_CAP_LEFT);
            ween_strike_draw(f, &top->surface, ox + x + xa, oy + y, p + a,
                             b - a, WEEN_WHITE);
            ween_strike_draw(f, &top->surface, ox + x + xb, oy + y, p + b,
                             n - b, ink);
        }
        if (!(wnd->style & ES_MULTILINE))
            break;
    }

    if (ween_focus_get() == wnd && !(wnd->style & WS_DISABLED) && e->caret_on) {
        int crow = rich_line_of(e, e->caret);
        int cy = inset + (crow - e->first_visible) * line;
        int cx = inset + ween_strike_pen(f, e->text + e->line[crow].start,
                                         e->caret - e->line[crow].start);
        if (cy >= inset && cy + line <= cr.bottom - inset)
            ween_surface_vline(&top->surface, ox + cx, oy + cy, line,
                               WEEN_BLACK);
    }

    /* The bar last, so a line long enough to reach it is covered by it. */
    if (sb) {
        ween_sbstate st = rich_sbstate(wnd);
        ween_draw_scrollbar(&top->surface, ox + cr.right - sb, oy, sb,
                            cr.bottom - cr.top, 1,
                            ween_sb_maxpos(&st) > st.min, st.pos, st.page,
                            st.min, st.max);
    }
}

/* ---- the class ----------------------------------------------------------- */

static LRESULT CALLBACK rich_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ween_rich *e = rich_state(wnd);
    int multi = (wnd->style & ES_MULTILINE) != 0;

    if (!e)
        return DefWindowProcA(wnd, msg, wp, lp);

    switch (msg) {
    /* ---- the text itself ---- */
    case WM_SETTEXT:
        if (!rich_set_text(wnd, e, (const char *)lp))
            return FALSE;
        /* Setting the text is not the user's change: win32 clears the
         * modified flag here, which is how a program that has just loaded a
         * file is not offered a save. */
        e->modified = 0;
        rich_notify(wnd, EN_UPDATE, ENM_UPDATE);
        rich_notify(wnd, EN_CHANGE, ENM_CHANGE);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    case WM_GETTEXT: {
        char *out = (char *)lp;
        int room = (int)wp;
        int n = e->len;
        if (!out || room <= 0)
            return 0;
        if (n > room - 1)
            n = room - 1;
        memcpy(out, e->text, (size_t)n);
        out[n] = 0;
        return n;
    }
    case WM_GETTEXTLENGTH:
        return e->len;

    /* ---- the selection, in both the EDIT's terms and the rich edit's ---- */
    case EM_SETSEL: {
        int to = (int)lp < 0 ? e->len : (int)lp;
        e->anchor = (int)wp < 0 ? e->len : (int)wp;
        if (e->anchor > e->len)
            e->anchor = e->len;
        e->caret = to > e->len ? e->len : to;
        e->goal_set = 0;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case EM_GETSEL: {
        int from, to;
        rich_range(e, &from, &to);
        if (wp)
            *(DWORD *)wp = (DWORD)from;
        if (lp)
            *(DWORD *)lp = (DWORD)to;
        return (LRESULT)MAKELONG(from, to);
    }
    case EM_EXSETSEL: {
        /* The same thing said in a CHARRANGE, which is how a rich edit is
         * asked once a document is longer than a packed pair can say. A
         * cpMax of -1 means the end. */
        CHARRANGE *cr = (CHARRANGE *)lp;
        if (!cr)
            return 0;
        e->anchor = cr->cpMin < 0 ? e->len : (int)cr->cpMin;
        e->caret = cr->cpMax < 0 ? e->len : (int)cr->cpMax;
        if (e->anchor > e->len)
            e->anchor = e->len;
        if (e->caret > e->len)
            e->caret = e->len;
        rich_show_caret(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return e->caret;
    }
    case EM_EXGETSEL: {
        CHARRANGE *cr = (CHARRANGE *)lp;
        int from, to;
        if (!cr)
            return 0;
        rich_range(e, &from, &to);
        cr->cpMin = from;
        cr->cpMax = to;
        return 0;
    }
    case EM_GETSELTEXT: {
        char *out = (char *)lp;
        int from, to;
        rich_range(e, &from, &to);
        if (!out)
            return 0;
        memcpy(out, e->text + from, (size_t)(to - from));
        out[to - from] = 0;
        return to - from;
    }
    case EM_GETTEXTRANGE: {
        TEXTRANGEA *tr = (TEXTRANGEA *)lp;
        int from, to;
        if (!tr || !tr->lpstrText)
            return 0;
        from = tr->chrg.cpMin < 0 ? 0 : (int)tr->chrg.cpMin;
        to = tr->chrg.cpMax < 0 ? e->len : (int)tr->chrg.cpMax;
        if (from > e->len)
            from = e->len;
        if (to > e->len)
            to = e->len;
        if (to < from)
            to = from;
        memcpy(tr->lpstrText, e->text + from, (size_t)(to - from));
        tr->lpstrText[to - from] = 0;
        return to - from;
    }
    case EM_REPLACESEL: {
        const char *text = (const char *)lp;
        if (!text || (wnd->style & (WS_DISABLED | ES_READONLY)))
            return 0;
        if (wp) /* the program says whether this is undoable */
            rich_remember(e);
        rich_delete_selection(wnd, e);
        rich_insert(wnd, e, text);
        rich_changed(wnd, e);
        rich_show_caret(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }

    /* ---- what a program asks about lines ---- */
    case EM_GETLINECOUNT:
        return ween_text_line_count(e->text);
    case EM_LINEINDEX:
        return ween_text_line_start(
            e->text,
            (int)wp < 0 ? ween_text_line_from_char(e->text, e->caret) : (int)wp);
    case EM_LINEFROMCHAR:
        return ween_text_line_from_char(e->text,
                                        (int)wp < 0 ? e->caret : (int)wp);
    case EM_LINELENGTH: {
        int at = (int)wp < 0 ? e->caret : (int)wp;
        return ween_text_line_length(
            e->text,
            ween_text_line_start(e->text,
                                 ween_text_line_from_char(e->text, at)));
    }
    case EM_GETLINE: {
        char *out = (char *)lp;
        int start, n, room;
        if (!out)
            return 0;
        room = (int)*(WORD *)out;
        start = ween_text_line_start(e->text, (int)wp);
        n = ween_text_line_length(e->text, start);
        if (n > room)
            n = room;
        memcpy(out, e->text + start, (size_t)n);
        return n;
    }
    case EM_GETFIRSTVISIBLELINE:
        return e->first_visible;
    case EM_LINESCROLL: {
        int top;
        if (!multi)
            return FALSE;
        top = e->first_visible + (int)lp;
        if (top > e->lines - 1)
            top = e->lines - 1;
        if (top < 0)
            top = 0;
        if (top != e->first_visible) {
            e->first_visible = top;
            InvalidateRect(wnd, NULL, FALSE);
        }
        return TRUE;
    }
    case EM_SCROLLCARET:
        if (rich_scroll_into_view(wnd, e))
            InvalidateRect(wnd, NULL, FALSE);
        return 0;

    /* ---- the smaller state a program keeps ---- */
    case EM_GETMODIFY:
        return e->modified;
    case EM_SETMODIFY:
        e->modified = wp != 0;
        return 0;
    case EM_LIMITTEXT:
    case EM_EXLIMITTEXT:
        /* EM_EXLIMITTEXT takes its number in lParam, since a rich edit's
         * documents outgrew a WPARAM. Zero means the default, which here is
         * no limit at all. */
        e->limit = msg == EM_EXLIMITTEXT ? (int)lp : (int)wp;
        return 0;
    case EM_SETEVENTMASK: {
        DWORD was = e->events;
        e->events = (DWORD)lp;
        return (LRESULT)was;
    }
    case EM_GETEVENTMASK:
        return (LRESULT)e->events;
    case EM_CANUNDO:
        return e->undo ? TRUE : FALSE;
    case EM_EMPTYUNDOBUFFER:
        free(e->undo);
        e->undo = NULL;
        return 0;
    case EM_UNDO: {
        char *was = e->undo;
        int caret = e->undo_caret;
        if (!was)
            return FALSE;
        e->undo = NULL;
        rich_remember(e); /* the swap: undoing is itself undoable */
        if (!rich_set_text(wnd, e, was)) {
            free(was);
            return FALSE;
        }
        free(was);
        e->caret = e->anchor = caret > e->len ? e->len : caret;
        rich_changed(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case EM_CANPASTE:
        return TRUE;

    case WM_GETDLGCODE:
        /* Everything typed, including the arrows; and Return too when the
         * control was made with ES_WANTRETURN, which is what a multi-line
         * editor in a dialog asks for so that Enter makes a line rather than
         * pressing the default button. */
        return DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_HASSETSEL |
               ((wnd->style & ES_WANTRETURN) ? DLGC_WANTMESSAGE : 0);

    /* ---- drawing and the caret ---- */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        rich_paint(wnd, dc, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_SETFOCUS:
        e->caret_on = 1;
        SetTimer(wnd, WEEN_CARET_TIMER, WEEN_CARET_BLINK_MS, NULL);
        rich_notify(wnd, EN_SETFOCUS, ENM_CHANGE);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        KillTimer(wnd, WEEN_CARET_TIMER);
        e->caret_on = 0;
        rich_notify(wnd, EN_KILLFOCUS, ENM_CHANGE);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_TIMER:
        if (wp == WEEN_CARET_TIMER) {
            e->caret_on = !e->caret_on;
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_SETFONT: {
        /* The window's own handler takes the face; the table has to be
         * measured again after it, because a line's height is the font's. */
        LRESULT r = DefWindowProcA(wnd, msg, wp, lp);
        rich_relines(wnd, e);
        return r;
    }
    case WM_SIZE:
        rich_relines(wnd, e);
        return DefWindowProcA(wnd, msg, wp, lp);

    /* ---- the mouse ---- */
    case WM_LBUTTONDOWN: {
        int had = ween_focus_get() == wnd;
        if (wnd->style & WS_DISABLED)
            return 0;
        if ((wnd->style & WS_VSCROLL)) {
            RECT cr;
            GetClientRect(wnd, &cr);
            if (GET_X_LPARAM(lp) >= cr.right - ween_scroll_metric()) {
                ween_sbstate st = rich_sbstate(wnd);
                int grab, pos;
                SetFocus(wnd);
                pos = ween_sb_click(GET_Y_LPARAM(lp), cr.bottom - cr.top, &st,
                                    &grab);
                /* A click in the track moves a screenful less one line, so
                 * the line that was at the bottom is at the top afterwards
                 * -- measured on the machine's own Notepad, and the same
                 * rule its edit control follows. */
                if (grab < 0 && st.page > 1) {
                    if (pos == st.pos - st.page)
                        pos = st.pos - (st.page - 1);
                    else if (pos == st.pos + st.page)
                        pos = st.pos + (st.page - 1);
                }
                if (grab >= 0) {
                    SetCapture(wnd);
                    e->sb_grab = grab;
                }
                e->first_visible = ween_sb_clamp(pos, &st);
                rich_notify(wnd, EN_VSCROLL, ENM_SCROLL);
                InvalidateRect(wnd, NULL, FALSE);
                return 0;
            }
        }
        SetFocus(wnd);
        e->caret = rich_index_at_point(wnd, e, GET_X_LPARAM(lp) - rich_inset(wnd),
                                       GET_Y_LPARAM(lp));
        e->anchor = e->caret; /* a fresh click starts a new selection */
        e->goal_set = 0;
        rich_show_caret(wnd, e);
        SetCapture(wnd);
        if (!had)
            rich_notify(wnd, EN_SETFOCUS, ENM_CHANGE);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == wnd && e->sb_grab >= 0) {
            RECT cr;
            ween_sbstate st = rich_sbstate(wnd);
            int top;
            GetClientRect(wnd, &cr);
            top = ween_sb_clamp(ween_sb_drag(GET_Y_LPARAM(lp),
                                             cr.bottom - cr.top, &st,
                                             e->sb_grab),
                                &st);
            if (top != e->first_visible) {
                e->first_visible = top;
                rich_notify(wnd, EN_VSCROLL, ENM_SCROLL);
                InvalidateRect(wnd, NULL, FALSE);
            }
        } else if (GetCapture() == wnd) {
            int at = rich_index_at_point(wnd, e,
                                         GET_X_LPARAM(lp) - rich_inset(wnd),
                                         GET_Y_LPARAM(lp));
            if (at != e->caret) {
                e->caret = at; /* a drag extends from the anchor */
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        e->sb_grab = -1;
        if (GetCapture() == wnd)
            ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK:
        if (!(wnd->style & WS_DISABLED)) {
            e->caret = rich_index_at_point(wnd, e,
                                           GET_X_LPARAM(lp) - rich_inset(wnd),
                                           GET_Y_LPARAM(lp));
            rich_select_word(e);
            rich_show_caret(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (multi) {
            ween_sbstate st = rich_sbstate(wnd);
            int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            int top = ween_sb_clamp(e->first_visible - delta * 3, &st);
            if (top != e->first_visible) {
                e->first_visible = top;
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        return 0;

    /* ---- the clipboard ---- */
    case WM_COPY:
    case WM_CUT: {
        char *sel = rich_selected_text(e);
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
            rich_remember(e);
            rich_delete_selection(wnd, e);
            rich_changed(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_PASTE: {
        const char *text;
        if (wnd->style & (WS_DISABLED | ES_READONLY))
            return 0;
        if (!OpenClipboard(wnd))
            return 0;
        text = (const char *)GetClipboardData(CF_TEXT);
        if (text) {
            rich_remember(e);
            rich_delete_selection(wnd, e);
            rich_insert(wnd, e, text);
        }
        CloseClipboard();
        if (text) {
            rich_changed(wnd, e);
            rich_show_caret(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_CLEAR:
        if (!(wnd->style & (WS_DISABLED | ES_READONLY))) {
            rich_remember(e);
            if (rich_delete_selection(wnd, e)) {
                rich_changed(wnd, e);
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        return 0;

    /* ---- typing ---- */
    case WM_CHAR: {
        char ch = (char)wp;
        if (wnd->style & (WS_DISABLED | ES_READONLY))
            return 0;
        if (ch == '\r' || ch == '\n') {
            /* A line of its own, written the way Windows writes one: a
             * carriage return and a line feed, so a program reading the text
             * back finds what it expects. A single-line control is not
             * typing at all -- the dialog's default button has already had
             * its chance at the key. */
            if (!multi)
                return 0;
            rich_remember(e);
            rich_delete_selection(wnd, e);
            rich_insert(wnd, e, "\r\n");
        } else if (ch == '\b') {
            rich_remember(e);
            if (!rich_delete_selection(wnd, e) && e->caret > 0) {
                /* a line break is two characters and goes as one */
                int back = (e->caret >= 2 && e->text[e->caret - 1] == '\n' &&
                            e->text[e->caret - 2] == '\r')
                               ? 2
                               : 1;
                rich_delete_range(wnd, e, e->caret - back, e->caret);
            }
        } else if ((unsigned char)ch >= ' ') {
            char one[2];
            int from, to;
            rich_range(e, &from, &to);
            if (e->limit && e->len - (to - from) + 1 > e->limit) {
                rich_notify(wnd, EN_MAXTEXT, ENM_CHANGE);
                return 0;
            }
            one[0] = ch;
            one[1] = 0;
            rich_remember(e);
            rich_delete_selection(wnd, e);
            rich_insert(wnd, e, one);
        } else {
            return 0;
        }
        rich_show_caret(wnd, e);
        rich_changed(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        int shift = (lp & 1) != 0;         /* the backend puts Shift in bit 0 */
        int ctrl = (lp & (1L << 28)) != 0; /* and Ctrl in bit 28 */
        int moved = 1, keeps_goal = 0;
        if (ctrl) {
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
                e->caret = e->len;
                InvalidateRect(wnd, NULL, FALSE);
                return 0;
            case 'Z':
                SendMessageA(wnd, EM_UNDO, 0, 0);
                return 0;
            default:
                break;
            }
        }
        switch (wp) {
        case VK_LEFT:
            if (e->caret >= 2 && e->text[e->caret - 1] == '\n' &&
                e->text[e->caret - 2] == '\r')
                e->caret -= 2; /* a line break is one place, not two */
            else if (e->caret > 0)
                e->caret--;
            break;
        case VK_RIGHT:
            if (e->caret + 1 < e->len && e->text[e->caret] == '\r' &&
                e->text[e->caret + 1] == '\n')
                e->caret += 2;
            else if (e->caret < e->len)
                e->caret++;
            break;
        case VK_UP:
        case VK_DOWN: {
            int row, to;
            if (!multi)
                return DefWindowProcA(wnd, msg, wp, lp);
            row = rich_line_of(e, e->caret);
            to = wp == VK_UP ? row - 1 : row + 1;
            if (to < 0 || to >= e->lines)
                break;
            if (!e->goal_set) {
                e->goal_x = rich_caret_x(wnd, e);
                e->goal_set = 1;
            }
            e->caret = rich_index_at_x(wnd, e, to, e->goal_x);
            keeps_goal = 1;
            break;
        }
        case VK_PRIOR:
        case VK_NEXT: {
            /* A page is what the window shows, and the caret keeps the
             * place along the line it set out from -- the same rule the
             * arrows follow. */
            int rows = rich_visible_lines(wnd);
            int row, to;
            if (!multi)
                return DefWindowProcA(wnd, msg, wp, lp);
            row = rich_line_of(e, e->caret);
            to = wp == VK_PRIOR ? row - rows : row + rows;
            if (to < 0)
                to = 0;
            if (to >= e->lines)
                to = e->lines - 1;
            if (!e->goal_set) {
                e->goal_x = rich_caret_x(wnd, e);
                e->goal_set = 1;
            }
            e->caret = rich_index_at_x(wnd, e, to, e->goal_x);
            keeps_goal = 1;
            break;
        }
        case VK_HOME:
            e->caret = multi && !ctrl ? e->line[rich_line_of(e, e->caret)].start
                                      : 0;
            break;
        case VK_END:
            if (multi && !ctrl) {
                int row = rich_line_of(e, e->caret);
                e->caret = e->line[row].start + e->line[row].len;
            } else {
                e->caret = e->len;
            }
            break;
        case VK_DELETE: {
            int back;
            if (wnd->style & (WS_DISABLED | ES_READONLY))
                return 0;
            rich_remember(e);
            back = (e->caret + 1 < e->len && e->text[e->caret] == '\r' &&
                    e->text[e->caret + 1] == '\n')
                       ? 2
                       : 1;
            if (!rich_delete_selection(wnd, e) && e->caret < e->len)
                rich_delete_range(wnd, e, e->caret, e->caret + back);
            rich_changed(wnd, e);
            moved = 0;
            break;
        }
        default:
            return DefWindowProcA(wnd, msg, wp, lp);
        }
        if (moved && !shift) /* moving without Shift drops the selection */
            e->anchor = e->caret;
        if (!keeps_goal) /* anything but a vertical move forgets where it set out from */
            e->goal_set = 0;
        rich_show_caret(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

void ween_register_richedit(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.hbrBackground = NULL; /* it paints its own */
    wc.style = CS_DBLCLKS;   /* a double click takes the word under it */
    wc.lpfnWndProc = rich_proc;
    wc.lpszClassName = RICHEDIT_CLASSA;
    RegisterClassA(&wc);
    /* Rich Edit 1.0's name answers to the same control. A program built
     * against riched32.dll asks for this one, and the difference between the
     * two versions is not in the plain text. */
    wc.lpszClassName = RICHEDIT_CLASS10A;
    RegisterClassA(&wc);
}
