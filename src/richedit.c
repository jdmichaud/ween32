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
    int height; /* pixels: the tallest run on the line */
    int ascent; /* where the runs' baselines meet, from the line's top */
};

/* ---- runs ----------------------------------------------------------------
 *
 * The formatting the document carries, kept beside the text rather than in
 * it: a run is a first character and the formatting from there until the
 * next run begins. Every attribute has a value -- there is no mask in a
 * stored run, since a mask is what a *caller* means and not what a character
 * is -- and the first run always starts at nought, so every character has a
 * format without a search failing.
 *
 * Two rules, both read off the machine's own riched20 with
 * tools/vm/ctlprobe.c and written up in docs/testing.md: setting a format
 * over part of a run splits it, and a run left identical to its neighbour is
 * merged with it. Splitting without merging is what turns a document into
 * thousands of runs that all say the same thing. */
typedef struct {
    DWORD effects; /* CFE_BOLD | CFE_ITALIC | ... and CFE_AUTOCOLOR */
    LONG height;   /* twips, as CHARFORMAT states a size */
    LONG offset;   /* yOffset, twips */
    COLORREF color;
    BYTE charset, pitch;
    char face[LF_FACESIZE];
} ween_rfmt;

struct rich_run {
    int start;
    ween_rfmt fmt;
};

/* ---- paragraphs ----------------------------------------------------------
 *
 * Kept the same way the runs are, and maintained by the same two events: a
 * mark put in splits a paragraph, and both halves carry what the whole one
 * carried; a mark taken out joins two, and what survives is the *first*
 * one's. Both are the machine's -- typing a return in a centred paragraph
 * gives two centred ones, and a backspace over the mark between a left and a
 * right paragraph leaves one that is left. */
typedef struct {
    WORD numbering;
    WORD alignment;    /* PFA_LEFT, PFA_RIGHT, PFA_CENTER */
    LONG start_indent; /* twips */
    LONG right_indent;
    LONG offset; /* the first line's, against the rest */
    SHORT tabs;
    LONG tab[MAX_TAB_STOPS];
} ween_pfmt;

struct rich_para {
    int start;
    ween_pfmt fmt;
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
    struct rich_run *run;
    int runs, run_cap;
    struct rich_para *para;
    int paras, para_cap;
    /* What the next character typed will carry. A rich edit takes it from
     * the character *before* the caret -- an X typed at the end of a bold
     * run comes out bold -- unless a program has just set a format on an
     * empty selection, which arms this instead. Measured; see docs. */
    ween_rfmt insert;
    int insert_armed;
    /* The selection as the parent last heard it, so that EN_SELCHANGE is
     * sent when it moves and not every time something asks. */
    int said_from, said_to;
} ween_rich;

static void rich_free(void *p)
{
    ween_rich *e = p;
    if (!e)
        return;
    free(e->text);
    free(e->undo);
    free(e->line);
    free(e->run);
    free(e->para);
    free(e);
}

static void runs_reset(HWND wnd, ween_rich *e);
static void paras_reset(ween_rich *e);

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
        if (e) {
            runs_reset(w, e);
            paras_reset(e);
        }
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

/* ---- what a run is made of ----------------------------------------------- */

/* A face name copied into a fixed field: bounded, and always terminated.
 * win32 spells this lstrcpynA; this library has not got it and the one place
 * that wants it is here. */
static void face_copy(char *out, const char *in)
{
    int i;
    for (i = 0; i < LF_FACESIZE - 1 && in && in[i]; i++)
        out[i] = in[i];
    out[i] = 0;
}

/* Twips are a twentieth of a point; a point is a seventy-second of an inch.
 * What the screen shows is pixels, so a height converts through the dpi --
 * 165 twips is eleven pixels at 96, which is the face a fresh control is
 * lettered in. */
/* Twips to pixels for a distance, which may be nothing or may be negative --
 * unlike a font's height, which is at least one pixel. */
static int rfmt_px_twips(LONG twips)
{
    int dpi = ween_render_dpi();
    return (int)((twips * dpi + (twips < 0 ? -720 : 720)) / 1440);
}

static int rfmt_px(LONG twips)
{
    int px = (int)((twips * ween_render_dpi() + 720) / 1440);
    return px > 0 ? px : 1;
}

static LONG rfmt_twips(int px)
{
    return (LONG)((px * 1440 + ween_render_dpi() / 2) / ween_render_dpi());
}

/* The strike a run is drawn in: its face at its size, and the bold cut when
 * it asks for one. The slant and the rule under are not in the glyphs and go
 * on at drawing time. */
static const ween_strike *rfmt_strike(const ween_rfmt *f)
{
    return ween_font_create(f->face[0] ? f->face : "MS Shell Dlg",
                            -rfmt_px(f->height),
                            (f->effects & CFE_BOLD) ? 700 : 400);
}

static int rfmt_same(const ween_rfmt *a, const ween_rfmt *b)
{
    return a->effects == b->effects && a->height == b->height &&
           a->offset == b->offset && a->color == b->color &&
           a->charset == b->charset && a->pitch == b->pitch &&
           strcmp(a->face, b->face) == 0;
}

/* What a control with nothing said to it is lettered in: the font it was
 * given, at the size that font is, in the window text colour. The machine's
 * fresh control answers Tahoma at 165 twips with CFE_AUTOCOLOR, which is the
 * message font -- so this is that, read from the strike rather than named. */
static void rfmt_default(HWND wnd, ween_rfmt *f)
{
    const ween_strike *s = wnd->font ? wnd->font : ween_gui_font();
    memset(f, 0, sizeof *f);
    f->effects = CFE_AUTOCOLOR;
    f->height = rfmt_twips(s ? s->ascent - s->descent : 11);
    f->color = GetSysColor(COLOR_WINDOWTEXT);
    f->charset = 1; /* DEFAULT_CHARSET */
    face_copy(f->face, "MS Shell Dlg");
}

static int runs_reserve(ween_rich *e, int n)
{
    struct rich_run *grown;
    int cap = e->run_cap ? e->run_cap : 8;
    if (n <= e->run_cap)
        return 1;
    while (cap < n)
        cap *= 2;
    grown = realloc(e->run, (size_t)cap * sizeof *grown);
    if (!grown)
        return 0;
    e->run = grown;
    e->run_cap = cap;
    return 1;
}

/* One run over everything, in the control's own face. */
static void runs_reset(HWND wnd, ween_rich *e)
{
    if (!runs_reserve(e, 1))
        return;
    e->runs = 1;
    e->run[0].start = 0;
    rfmt_default(wnd, &e->run[0].fmt);
}

static int run_at(const ween_rich *e, int at)
{
    int lo = 0, hi = e->runs - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (e->run[mid].start <= at)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo < 0 ? 0 : lo;
}

/* A boundary at `at`, made if it is not there. Answers the run that begins
 * there. */
static int runs_split(ween_rich *e, int at)
{
    int i = run_at(e, at);
    if (e->run[i].start == at)
        return i;
    if (!runs_reserve(e, e->runs + 1))
        return i;
    memmove(&e->run[i + 2], &e->run[i + 1],
            (size_t)(e->runs - i - 1) * sizeof *e->run);
    e->run[i + 1] = e->run[i];
    e->run[i + 1].start = at;
    e->runs++;
    return i + 1;
}

/* Neighbours that say the same thing become one, and a run with nothing left
 * in it goes. */
static void runs_coalesce(ween_rich *e)
{
    int i = 1;
    while (i < e->runs) {
        int empty = e->run[i].start <= e->run[i - 1].start;
        if (empty || rfmt_same(&e->run[i].fmt, &e->run[i - 1].fmt)) {
            memmove(&e->run[i], &e->run[i + 1],
                    (size_t)(e->runs - i - 1) * sizeof *e->run);
            e->runs--;
        } else {
            i++;
        }
    }
}

/* Text put in at `at`, carrying `f`. Everything from there moves along --
 * including a run that began exactly there, since what is typed at a
 * boundary belongs to the run before it -- and the new characters become a
 * run of their own, which is then merged back if it says what its
 * neighbour says. */
static void runs_insert(ween_rich *e, int at, int n, const ween_rfmt *f)
{
    int i;
    for (i = 1; i < e->runs; i++)
        if (e->run[i].start >= at)
            e->run[i].start += n;
    i = runs_split(e, at);
    if (at + n < e->len)
        runs_split(e, at + n);
    if (i < e->runs)
        e->run[i].fmt = *f;
    runs_coalesce(e);
}

/* Text taken out: what began inside the hole begins at its start, and what
 * began after it comes back by as much as was removed. A run left with
 * nothing in it goes in the coalesce. */
static void runs_delete(ween_rich *e, int from, int to)
{
    int n = to - from, i;
    for (i = 1; i < e->runs; i++) {
        if (e->run[i].start >= to)
            e->run[i].start -= n;
        else if (e->run[i].start > from)
            e->run[i].start = from;
    }
    runs_coalesce(e);
}

/* Put a caller's CHARFORMAT over a range: only the fields its mask names. */
static void rfmt_apply(ween_rfmt *f, const CHARFORMATA *cf)
{
    DWORD m = cf->dwMask;
    if (m & CFM_BOLD)
        f->effects = (f->effects & ~(DWORD)CFE_BOLD) |
                     (cf->dwEffects & CFE_BOLD);
    if (m & CFM_ITALIC)
        f->effects = (f->effects & ~(DWORD)CFE_ITALIC) |
                     (cf->dwEffects & CFE_ITALIC);
    if (m & CFM_UNDERLINE)
        f->effects = (f->effects & ~(DWORD)CFE_UNDERLINE) |
                     (cf->dwEffects & CFE_UNDERLINE);
    if (m & CFM_STRIKEOUT)
        f->effects = (f->effects & ~(DWORD)CFE_STRIKEOUT) |
                     (cf->dwEffects & CFE_STRIKEOUT);
    if (m & CFM_PROTECTED)
        f->effects = (f->effects & ~(DWORD)CFE_PROTECTED) |
                     (cf->dwEffects & CFE_PROTECTED);
    if (m & CFM_LINK)
        f->effects = (f->effects & ~(DWORD)CFE_LINK) |
                     (cf->dwEffects & CFE_LINK);
    if (m & CFM_SIZE)
        f->height = cf->yHeight;
    if (m & CFM_OFFSET)
        f->offset = cf->yOffset;
    if (m & CFM_COLOR) {
        /* CFE_AUTOCOLOR in the effects means "the window's colour", and the
         * colour field is then not read at all. */
        f->effects = (f->effects & ~(DWORD)CFE_AUTOCOLOR) |
                     (cf->dwEffects & CFE_AUTOCOLOR);
        if (!(cf->dwEffects & CFE_AUTOCOLOR))
            f->color = cf->crTextColor;
    }
    if (m & CFM_CHARSET)
        f->charset = cf->bCharSet;
    if (m & CFM_FACE) {
        face_copy(f->face, cf->szFaceName);
        f->pitch = cf->bPitchAndFamily;
    }
}

static void runs_set(ween_rich *e, int from, int to, const CHARFORMATA *cf)
{
    int i, last;
    if (from >= to)
        return;
    i = runs_split(e, from);
    if (to < e->len)
        runs_split(e, to);
    last = run_at(e, to > from ? to - 1 : from);
    for (; i <= last && i < e->runs; i++)
        rfmt_apply(&e->run[i].fmt, cf);
    runs_coalesce(e);
}

/* What a range carries, and what the control is sure of.
 *
 * Two halves, and the machine settles both. The *mask* is what is the same
 * throughout: an attribute that differs anywhere in the range has its bit
 * cleared, which is how a format bar knows to show Bold as neither in nor
 * out. The *values* are the character before the range's end -- not the
 * first one, which is what this had until the answers came back. Asked over
 * characters 4 to 6 of a text bold from 5, riched20 answers with the bold
 * bit set in dwEffects and cleared in dwMask; asked over the whole text it
 * answers with it clear in both. Both are "the character before the caret",
 * which is the same rule that decides what the next character typed will
 * carry. */
static void runs_get(ween_rich *e, int from, int to, CHARFORMATA *cf)
{
    int i, first = run_at(e, from), last = run_at(e, to > from ? to - 1 : from);
    const ween_rfmt *a = &e->run[last].fmt;
    DWORD mask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT |
                 CFM_PROTECTED | CFM_LINK | CFM_SIZE | CFM_COLOR | CFM_FACE |
                 CFM_OFFSET | CFM_CHARSET;
    for (i = first; i < last && i + 1 < e->runs; i++) {
        const ween_rfmt *p = &e->run[i].fmt;
        const ween_rfmt *b = &e->run[i + 1].fmt;
        DWORD diff = p->effects ^ b->effects;
        if (diff & CFE_BOLD)
            mask &= ~(DWORD)CFM_BOLD;
        if (diff & CFE_ITALIC)
            mask &= ~(DWORD)CFM_ITALIC;
        if (diff & CFE_UNDERLINE)
            mask &= ~(DWORD)CFM_UNDERLINE;
        if (diff & CFE_STRIKEOUT)
            mask &= ~(DWORD)CFM_STRIKEOUT;
        if (diff & CFE_PROTECTED)
            mask &= ~(DWORD)CFM_PROTECTED;
        if (diff & CFE_LINK)
            mask &= ~(DWORD)CFM_LINK;
        if (p->height != b->height)
            mask &= ~(DWORD)CFM_SIZE;
        if (p->offset != b->offset)
            mask &= ~(DWORD)CFM_OFFSET;
        if (p->color != b->color || (diff & CFE_AUTOCOLOR))
            mask &= ~(DWORD)CFM_COLOR;
        if (strcmp(p->face, b->face) != 0)
            mask &= ~(DWORD)CFM_FACE;
        if (p->charset != b->charset)
            mask &= ~(DWORD)CFM_CHARSET;
    }
    cf->dwMask = mask;
    cf->dwEffects = a->effects;
    cf->yHeight = a->height;
    cf->yOffset = a->offset;
    cf->crTextColor = a->color;
    cf->bCharSet = a->charset;
    cf->bPitchAndFamily = a->pitch;
    face_copy(cf->szFaceName, a->face);
}

/* ---- the paragraphs' own array ------------------------------------------- */

static int paras_reserve(ween_rich *e, int n)
{
    struct rich_para *grown;
    int cap = e->para_cap ? e->para_cap : 8;
    if (n <= e->para_cap)
        return 1;
    while (cap < n)
        cap *= 2;
    grown = realloc(e->para, (size_t)cap * sizeof *grown);
    if (!grown)
        return 0;
    e->para = grown;
    e->para_cap = cap;
    return 1;
}

static void pfmt_default(ween_pfmt *f)
{
    memset(f, 0, sizeof *f);
    f->alignment = PFA_LEFT; /* which is what a fresh control answers */
}

/* One paragraph per mark, all in the default. */
static void paras_reset(ween_rich *e)
{
    int i, n = 1;
    for (i = 0; i < e->len; i++)
        if (e->text[i] == '\r')
            n++;
    if (!paras_reserve(e, n))
        return;
    e->paras = 0;
    pfmt_default(&e->para[0].fmt);
    e->para[0].start = 0;
    e->paras = 1;
    for (i = 0; i < e->len; i++)
        if (e->text[i] == '\r') {
            e->para[e->paras].start = i + 1;
            pfmt_default(&e->para[e->paras].fmt);
            e->paras++;
        }
}

static int para_at(const ween_rich *e, int at)
{
    int lo = 0, hi = e->paras - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (e->para[mid].start <= at)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo < 0 ? 0 : lo;
}

/* Text put in: the paragraphs after it move along, and a mark among what was
 * put in splits the paragraph it landed in -- every piece carrying what that
 * one carried. */
static void paras_insert(ween_rich *e, int at, int n, const char *text)
{
    int i, marks = 0, k;
    for (i = 0; i < n; i++)
        if (text[i] == '\r')
            marks++;
    for (i = 1; i < e->paras; i++)
        if (e->para[i].start > at)
            e->para[i].start += n;
    if (!marks)
        return;
    if (!paras_reserve(e, e->paras + marks))
        return;
    k = para_at(e, at);
    memmove(&e->para[k + 1 + marks], &e->para[k + 1],
            (size_t)(e->paras - k - 1) * sizeof *e->para);
    e->paras += marks;
    marks = 0;
    for (i = 0; i < n; i++)
        if (text[i] == '\r') {
            marks++;
            e->para[k + marks].fmt = e->para[k].fmt;
            e->para[k + marks].start = at + i + 1;
        }
}

/* Text taken out: a paragraph whose mark went with it is gone, and what is
 * left of the two it joined carries the first one's formatting. */
static void paras_delete(ween_rich *e, int from, int to)
{
    int i, out = 1;
    for (i = 1; i < e->paras; i++) {
        int start = e->para[i].start;
        if (start > from && start <= to)
            continue; /* its mark was inside what went */
        if (start > to)
            start -= to - from;
        e->para[out].fmt = e->para[i].fmt;
        e->para[out].start = start;
        out++;
    }
    e->paras = out;
}

static void pfmt_apply(ween_pfmt *f, const PARAFORMAT *pf)
{
    DWORD m = pf->dwMask;
    if (m & PFM_ALIGNMENT)
        f->alignment = pf->wAlignment;
    if (m & PFM_NUMBERING)
        f->numbering = pf->wNumbering;
    if (m & PFM_STARTINDENT)
        f->start_indent = pf->dxStartIndent;
    if (m & PFM_OFFSETINDENT)
        f->start_indent += pf->dxStartIndent; /* a step, not a place */
    if (m & PFM_RIGHTINDENT)
        f->right_indent = pf->dxRightIndent;
    if (m & PFM_OFFSET)
        f->offset = pf->dxOffset;
    if (m & PFM_TABSTOPS) {
        int i, n = pf->cTabCount;
        if (n > MAX_TAB_STOPS)
            n = MAX_TAB_STOPS;
        if (n < 0)
            n = 0;
        f->tabs = (SHORT)n;
        for (i = 0; i < n; i++)
            f->tab[i] = pf->rgxTabs[i];
    }
}

/* Every paragraph the range touches, whole. A rich edit has no notion of
 * half a paragraph being centred, which the machine shows by centring the
 * whole of one when a single character of it was selected. */
static void paras_set(ween_rich *e, int from, int to, const PARAFORMAT *pf)
{
    int i, first = para_at(e, from), last = para_at(e, to > from ? to - 1 : to);
    for (i = first; i <= last && i < e->paras; i++)
        pfmt_apply(&e->para[i].fmt, pf);
}

static void paras_get(ween_rich *e, int from, int to, PARAFORMAT *pf)
{
    int i, first = para_at(e, from), last = para_at(e, to > from ? to - 1 : to);
    const ween_pfmt *a = &e->para[last].fmt;
    DWORD mask = PFM_STARTINDENT | PFM_RIGHTINDENT | PFM_OFFSET |
                 PFM_ALIGNMENT | PFM_TABSTOPS | PFM_NUMBERING;
    for (i = first; i < last && i + 1 < e->paras; i++) {
        const ween_pfmt *p = &e->para[i].fmt;
        const ween_pfmt *b = &e->para[i + 1].fmt;
        if (p->alignment != b->alignment)
            mask &= ~(DWORD)PFM_ALIGNMENT;
        if (p->numbering != b->numbering)
            mask &= ~(DWORD)PFM_NUMBERING;
        if (p->start_indent != b->start_indent)
            mask &= ~(DWORD)PFM_STARTINDENT;
        if (p->right_indent != b->right_indent)
            mask &= ~(DWORD)PFM_RIGHTINDENT;
        if (p->offset != b->offset)
            mask &= ~(DWORD)PFM_OFFSET;
        if (p->tabs != b->tabs ||
            memcmp(p->tab, b->tab, sizeof p->tab) != 0)
            mask &= ~(DWORD)PFM_TABSTOPS;
    }
    /* The machine answers a fresh control with these two set as well, and a
     * program that reads the mask rather than the fields sees the same
     * thing either way. */
    pf->dwMask = mask | PFM_OFFSETINDENT | 0x00010000 /* PFM_RTLPARA */;
    pf->wNumbering = a->numbering;
    pf->wAlignment = a->alignment;
    pf->dxStartIndent = a->start_indent;
    pf->dxRightIndent = a->right_indent;
    pf->dxOffset = a->offset;
    pf->cTabCount = a->tabs;
    memcpy(pf->rgxTabs, a->tab, sizeof pf->rgxTabs);
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

/* How tall a line is: the tallest run on it, since a run may carry a size of
 * its own. With one size throughout -- which is every line until a program
 * says otherwise -- it is the font's own height, and the table then says
 * exactly what multiplying a row by one line height would have said.
 *
 * Where the runs sit *within* that height, when they differ, is not measured
 * yet: the baselines are lined up here, which is what every text engine
 * does, and 4a will have to ask the machine when it wraps. */
static int rich_line_extent(HWND wnd, ween_rich *e, int start, int len,
                            int *ascent)
{
    int i = run_at(e, start), tall = 0, asc = 0;
    for (;;) {
        const ween_strike *f = rfmt_strike(&e->run[i].fmt);
        int h = f ? f->ascent - f->descent : rich_line_height(wnd);
        int a = f ? f->ascent : h;
        if (h > tall)
            tall = h;
        if (a > asc)
            asc = a;
        if (i + 1 >= e->runs || e->run[i + 1].start >= start + len)
            break;
        i++;
    }
    if (!tall)
        tall = rich_line_height(wnd);
    if (ascent)
        *ascent = asc;
    return tall;
}

static void rich_relines(HWND wnd, ween_rich *e)
{
    int at = 0, top = 0, n = 0;
    if (!e)
        return;
    for (;;) {
        int start = at, len, height, ascent = 0;
        while (at < e->len && e->text[at] != '\r')
            at++;
        len = at - start; /* the mark is not part of the line */
        height = rich_line_extent(wnd, e, start, len, &ascent);
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
        e->line[n].ascent = ascent;
        top += height;
        n++;
        if (at >= e->len)
            break;
        at++; /* past the mark */
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

/* EN_SELCHANGE, which is a WM_NOTIFY and not a WM_COMMAND: it carries where
 * the selection is now and what kind of thing is in it, and it is what keeps
 * a format bar's buttons in step with the caret. Like every other
 * notification a rich edit sends, it waits on the mask. */
static void rich_selchange(HWND wnd, ween_rich *e)
{
    SELCHANGE sc;
    int from, to;
    rich_range(e, &from, &to);
    if (from == e->said_from && to == e->said_to)
        return;
    e->said_from = from;
    e->said_to = to;
    if (!wnd->parent || !(e->events & ENM_SELCHANGE))
        return;
    memset(&sc, 0, sizeof sc);
    sc.nmhdr.hwndFrom = wnd;
    sc.nmhdr.idFrom = wnd->id;
    sc.nmhdr.code = EN_SELCHANGE;
    sc.chrg.cpMin = from;
    sc.chrg.cpMax = to;
    sc.seltyp = (WORD)(from == to ? SEL_EMPTY
                                  : (to - from > 1 ? SEL_TEXT | SEL_MULTICHAR
                                                   : SEL_TEXT));
    SendMessageA(wnd->parent, WM_NOTIFY, (WPARAM)wnd->id, (LPARAM)&sc);
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
    runs_delete(e, from, to);
    paras_delete(e, from, to);
    rich_relines(wnd, e);
    return 1;
}

static int rich_delete_selection(HWND wnd, ween_rich *e)
{
    int from, to;
    rich_range(e, &from, &to);
    return rich_delete_range(wnd, e, from, to);
}

/* ---- what a paragraph mark is -------------------------------------------
 *
 * One carriage return, and not the two characters that were handed in. Rich
 * Edit 2.0 keeps a CRLF as a single CR and every offset it states -- a
 * selection, EM_LINEINDEX, EM_GETSELTEXT -- is in that numbering, while
 * WM_GETTEXT hands the text back with the CRLF a program expects. The
 * machine says so plainly: set "one\r\ntwo" and WM_GETTEXT answers eight
 * bytes ending "6f 6e 65 0d 0a 74 77 6f", but EM_GETSELTEXT of 2..6 answers
 * four -- "65 0d 74 77", an "e", one carriage return, and "tw". And
 * "one\r\ntwo\r\n" is three lines whose last begins at 8, which only counts
 * if each break is one character.
 *
 * It matters more than it looks: a program that finds an offset in the text
 * it read and hands it back to the control has to mean the same character on
 * both sides, or the same source behaves differently on Windows and here.
 * That is the whole promise.
 */
static int rich_copy_in(char *out, const char *in, int n)
{
    int i = 0, o = 0;
    while (i < n) {
        if (in[i] == '\r' && i + 1 < n && in[i + 1] == '\n') {
            out[o++] = '\r';
            i += 2;
        } else if (in[i] == '\n') {
            out[o++] = '\r'; /* a bare line feed is a paragraph mark too */
            i++;
        } else {
            out[o++] = in[i++];
        }
    }
    out[o] = 0;
    return o;
}

/* What the next characters will carry: what a program armed with a set on an
 * empty selection, or else what the character *before* the caret carries --
 * an X typed at the end of a bold run comes out bold, which is riched20's
 * own rule and is measured. */
static void rich_insert_fmt(ween_rich *e, ween_rfmt *out)
{
    if (e->insert_armed) {
        *out = e->insert;
        return;
    }
    *out = e->run[run_at(e, e->caret > 0 ? e->caret - 1 : 0)].fmt;
}

static void rich_insert(HWND wnd, ween_rich *e, const char *text)
{
    int n = (int)strlen(text), at = e->caret;
    ween_rfmt f;
    char *converted = NULL;
    if (!n)
        return;
    /* Whatever comes in -- typed, pasted, or handed over by EM_REPLACESEL --
     * is stored with one carriage return per paragraph mark. */
    if (strchr(text, '\n')) {
        converted = malloc((size_t)n + 1);
        if (!converted)
            return;
        n = rich_copy_in(converted, text, n);
        text = converted;
    }
    if (e->limit && e->len + n > e->limit) {
        n = e->limit - e->len;
        if (n <= 0) {
            rich_notify(wnd, EN_MAXTEXT, ENM_CHANGE);
            return;
        }
    }
    if (!rich_reserve(e, e->len + n))
        return;
    rich_insert_fmt(e, &f);
    memmove(e->text + e->caret + n, e->text + e->caret,
            (size_t)(e->len - e->caret) + 1);
    memcpy(e->text + e->caret, text, (size_t)n);
    e->len += n;
    e->caret += n;
    e->anchor = e->caret;
    runs_insert(e, at, n, &f);
    paras_insert(e, at, n, text);
    e->insert_armed = 0; /* it armed one insertion, not every one after it */
    rich_relines(wnd, e);
    free(converted);
}

static int rich_set_text(HWND wnd, ween_rich *e, const char *text)
{
    int n = text ? (int)strlen(text) : 0;
    if (!rich_reserve(e, n))
        return 0;
    if (n)
        n = rich_copy_in(e->text, text, n);
    e->text[n] = 0;
    e->len = n;
    e->caret = e->anchor = 0;
    e->first_visible = 0;
    /* Text given whole is text without formatting: one run again, in the
     * control's own face, and its paragraphs in the default. RTF arrives
     * through EM_STREAMIN and not here. */
    runs_reset(wnd, e);
    paras_reset(e);
    e->insert_armed = 0;
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

/* How far along a line a column stands, in pixels, measured run by run --
 * since two runs of one line may be in different faces and sizes. */
static int rich_x_of(ween_rich *e, int row, int col)
{
    int start = e->line[row].start, end = start + col;
    int at = start, i = run_at(e, start), x = 0;
    while (at < end) {
        int next = (i + 1 < e->runs && e->run[i + 1].start < end)
                       ? e->run[i + 1].start
                       : end;
        const ween_strike *f = rfmt_strike(&e->run[i].fmt);
        x += f ? ween_strike_pen(f, e->text + at, next - at) : next - at;
        at = next;
        i++;
    }
    return x;
}

/* Where a line's text begins, in client pixels: its paragraph's indents, and
 * then whatever its alignment does with what is left. A twip is a twentieth
 * of a point, so an indent converts through the dpi the way a size does.
 *
 * The first line of a paragraph gets the offset as well as the indent, which
 * is what makes a hanging indent hang. With no wrapping yet every line is a
 * first line; when 4a wraps them this is where the difference goes. */
static int rich_line_left(HWND wnd, ween_rich *e, int row)
{
    int inset = rich_inset(wnd), sb = rich_bar(wnd);
    const ween_pfmt *pf = &e->para[para_at(e, e->line[row].start)].fmt;
    int left = inset + rfmt_px_twips(pf->start_indent) +
               rfmt_px_twips(pf->offset);
    RECT cr;
    int right, width;
    if (pf->alignment == PFA_LEFT || !pf->alignment)
        return left;
    GetClientRect(wnd, &cr);
    right = cr.right - sb - inset - rfmt_px_twips(pf->right_indent);
    width = rich_x_of(e, row, e->line[row].len);
    if (pf->alignment == PFA_RIGHT)
        return right - width;
    if (pf->alignment == PFA_CENTER)
        return left + (right - left - width) / 2;
    return left;
}

/* How far along its line the caret stands. */
static int rich_caret_x(HWND wnd, ween_rich *e)
{
    int row = rich_line_of(e, e->caret);
    (void)wnd;
    return rich_x_of(e, row, e->caret - e->line[row].start);
}

/* The character on a line that stands nearest a pixel. */
static int rich_index_at_x(HWND wnd, ween_rich *e, int row, int x)
{
    int i, best = 0, bestd = 1 << 30;
    (void)wnd;
    for (i = 0; i <= e->line[row].len; i++) {
        int pen = rich_x_of(e, row, i);
        int d = pen > x ? pen - x : x - pen;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return e->line[row].start + best;
}

/* Which character a point lands on: the row from the line table, since the
 * lines are no longer all one height, and then the nearest column on it. */

static int rich_index_at_point(HWND wnd, ween_rich *e, int x, int y)
{
    int inset = rich_inset(wnd), row, top;
    if (!e || !e->lines)
        return 0;
    top = e->line[e->first_visible].top;
    for (row = e->first_visible; row < e->lines; row++) {
        int at = inset + e->line[row].top - top;
        if (y < at + e->line[row].height || row == e->lines - 1)
            break;
    }
    if (row < e->first_visible)
        row = e->first_visible;
    if (row >= e->lines)
        row = e->lines - 1;
    return rich_index_at_x(wnd, e, row, x - rich_line_left(wnd, e, row) +
                                            rich_inset(wnd));
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
    struct ween_wnd *top = ween_top_level(wnd);
    ween_rich *e = rich_state(wnd);
    RECT r = ps->rcPaint, cr;
    int ox, oy, inset = rich_inset(wnd), sb = rich_bar(wnd);
    int from = 0, to = 0, row;
    ween_color ink = (wnd->style & WS_DISABLED) ? WEEN_SHADOW : WEEN_BLACK;

    ween_client_origin(wnd, &ox, &oy);
    GetClientRect(wnd, &cr);
    FillRect(dc, &r, GetSysColorBrush((wnd->style & WS_DISABLED) ||
                                              (wnd->style & ES_READONLY)
                                          ? COLOR_BTNFACE
                                          : COLOR_WINDOW));
    if (!e || !e->lines)
        return;

    /* A control hides its selection when the keyboard leaves it, unless it
     * was made with ES_NOHIDESEL -- which WordPad's editor is, so that what
     * its Find box has just found stays visible while the box has the
     * keyboard. */
    if (ween_focus_get() == wnd || (wnd->style & ES_NOHIDESEL))
        rich_range(e, &from, &to);

    for (row = e->first_visible; row < e->lines; row++) {
        int y = inset + e->line[row].top - e->line[e->first_visible].top;
        int x = rich_line_left(wnd, e, row);
        int at = e->line[row].start, end = at + e->line[row].len;
        int i = run_at(e, at);
        if ((wnd->style & ES_MULTILINE) &&
            y + e->line[row].height > cr.bottom - inset)
            break;
        /* A line is drawn run by run, and a run in two or three pieces where
         * the selection crosses it. Each piece is in its own face, size,
         * slant, rule and colour; they sit on one baseline, the tallest
         * run's, so a big letter and a small one on the same line stand on
         * the same line rather than each in the middle of its own box. */
        while (at < end) {
            int rend = (i + 1 < e->runs && e->run[i + 1].start < end)
                           ? e->run[i + 1].start
                           : end;
            const ween_rfmt *fmt = &e->run[i].fmt;
            const ween_strike *sf = rfmt_strike(fmt);
            int seg = rend, selected, w, by;
            ween_color col;
            if (at < from && from < seg)
                seg = from;
            else if (at >= from && at < to && to < seg)
                seg = to;
            selected = at >= from && at < to;
            w = sf ? ween_strike_pen(sf, e->text + at, seg - at) : seg - at;
            by = y + e->line[row].ascent - (sf ? sf->ascent : 0);
            col = selected ? WEEN_WHITE
                  : (fmt->effects & CFE_AUTOCOLOR)
                      ? ink
                      : ween_cr_to_px(fmt->color);
            if (selected)
                ween_surface_fill(&top->surface, ox + x, oy + y, w,
                                  e->line[row].height, WEEN_CAP_LEFT);
            if (sf)
                ween_strike_draw_styled(sf, &top->surface, ox + x, oy + by,
                                        e->text + at, seg - at, col,
                                        (fmt->effects & CFE_ITALIC) != 0,
                                        (fmt->effects & CFE_UNDERLINE) != 0);
            /* A line through the middle, which no strike carries and GDI
             * draws for itself. Where exactly it sits is not measured; half
             * the cell is where it lands here. */
            if ((fmt->effects & CFE_STRIKEOUT) && sf)
                ween_surface_fill(&top->surface, ox + x,
                                  oy + by + (sf->ascent - sf->descent) / 2, w,
                                  1, col);
            x += w;
            at = seg;
            if (at >= rend)
                i++;
        }
        if (!(wnd->style & ES_MULTILINE))
            break;
    }

    /* The caret: as tall as the line it is on, where the run it stands in
     * puts it. */
    if (ween_focus_get() == wnd && !(wnd->style & WS_DISABLED) && e->caret_on) {
        int crow = rich_line_of(e, e->caret);
        int cy = inset + e->line[crow].top - e->line[e->first_visible].top;
        int cx = rich_line_left(wnd, e, crow) +
                 rich_x_of(e, crow, e->caret - e->line[crow].start);
        if (cy >= inset && cy + e->line[crow].height <= cr.bottom - inset)
            ween_surface_vline(&top->surface, ox + cx, oy + cy,
                               e->line[crow].height, WEEN_BLACK);
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
        /* Out the way a program expects it: a carriage return and a line
         * feed for every paragraph mark, however it is stored. */
        char *out = (char *)lp;
        int room = (int)wp, i, o = 0;
        if (!out || room <= 0)
            return 0;
        for (i = 0; i < e->len && o < room - 1; i++) {
            if (e->text[i] == '\r') {
                if (o + 2 > room - 1)
                    break;
                out[o++] = '\r';
                out[o++] = '\n';
            } else {
                out[o++] = e->text[i];
            }
        }
        out[o] = 0;
        return o;
    }
    case WM_GETTEXTLENGTH: {
        int i, n = e->len;
        for (i = 0; i < e->len; i++)
            if (e->text[i] == '\r')
                n++; /* the line feed that comes back with it */
        return n;
    }

    /* ---- the selection, in both the EDIT's terms and the rich edit's ---- */
    case EM_SETSEL: {
        int to = (int)lp < 0 ? e->len : (int)lp;
        e->anchor = (int)wp < 0 ? e->len : (int)wp;
        if (e->anchor > e->len)
            e->anchor = e->len;
        e->caret = to > e->len ? e->len : to;
        e->goal_set = 0;
        rich_selchange(wnd, e);
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
        rich_selchange(wnd, e);
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
        rich_selchange(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }

    /* ---- what a program asks about lines ---- */
    /* ---- what a program asks about lines ----
     *
     * From the control's own table, not from the shared ween_text_line_*:
     * those count a CRLF as one break and this control has not got a CRLF to
     * count. Sharing them was right while the two controls kept their text
     * the same way; the machine's rich edit does not, so the answers come
     * from where the lines actually are. */
    case EM_GETLINECOUNT:
        return e->lines;
    case EM_LINEINDEX: {
        int row = (int)wp < 0 ? rich_line_of(e, e->caret) : (int)wp;
        if (row < 0 || row >= e->lines)
            return -1;
        return e->line[row].start;
    }
    case EM_LINEFROMCHAR:
        return rich_line_of(e, (int)wp < 0 ? e->caret : (int)wp);
    case EM_LINELENGTH: {
        int at = (int)wp < 0 ? e->caret : (int)wp;
        return e->line[rich_line_of(e, at)].len;
    }
    case EM_GETLINE: {
        char *out = (char *)lp;
        int row = (int)wp, n, room;
        if (!out || row < 0 || row >= e->lines)
            return 0;
        room = (int)*(WORD *)out;
        n = e->line[row].len;
        if (n > room)
            n = room;
        memcpy(out, e->text + e->line[row].start, (size_t)n);
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
    case EM_POSFROMCHAR: {
        /* A rich edit fills in a POINTL the caller passes and takes the
         * index in lParam, where an EDIT takes the index in wParam and packs
         * the answer into its return. Same message, two conventions; this is
         * the rich edit's. */
        POINTL *pt = (POINTL *)wp;
        int at = (int)lp, row;
        if (!pt || !e->lines)
            return 0;
        if (at < 0)
            at = 0;
        if (at > e->len)
            at = e->len;
        row = rich_line_of(e, at);
        pt->x = rich_line_left(wnd, e, row) +
                rich_x_of(e, row, at - e->line[row].start);
        pt->y = rich_inset(wnd) + e->line[row].top -
                e->line[e->first_visible].top;
        return 0;
    }

    /* ---- the formatting a run carries ---- */
    case EM_SETCHARFORMAT: {
        const CHARFORMATA *cf = (const CHARFORMATA *)lp;
        int from, to;
        if (!cf)
            return FALSE;
        if (wp & SCF_ALL) {
            from = 0;
            to = e->len;
        } else {
            rich_range(e, &from, &to);
        }
        if (from == to) {
            /* Nothing selected: what is set is what the next character
             * typed will carry, and the text does not change. That is how a
             * format bar's Bold works with an empty selection, and it is
             * measured -- a Z typed after one comes out bold. */
            if (!e->insert_armed) {
                rich_insert_fmt(e, &e->insert);
                e->insert_armed = 1;
            }
            rfmt_apply(&e->insert, cf);
            return TRUE;
        }
        rich_remember(e);
        runs_set(e, from, to, cf);
        e->insert_armed = 0;
        rich_relines(wnd, e);
        rich_notify(wnd, EN_CHANGE, ENM_CHANGE);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case EM_GETCHARFORMAT: {
        CHARFORMATA *cf = (CHARFORMATA *)lp;
        int from, to;
        if (!cf)
            return 0;
        if (wp & SCF_SELECTION) {
            rich_range(e, &from, &to);
            /* With nothing selected the answer is what the next character
             * would be typed in, which is the run before the caret unless a
             * program has armed something else. */
            if (from == to) {
                ween_rfmt f;
                if (e->insert_armed)
                    f = e->insert;
                else
                    rich_insert_fmt(e, &f);
                cf->dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE |
                             CFM_STRIKEOUT | CFM_PROTECTED | CFM_LINK |
                             CFM_SIZE | CFM_COLOR | CFM_FACE | CFM_OFFSET |
                             CFM_CHARSET;
                cf->dwEffects = f.effects;
                cf->yHeight = f.height;
                cf->yOffset = f.offset;
                cf->crTextColor = f.color;
                cf->bCharSet = f.charset;
                cf->bPitchAndFamily = f.pitch;
                face_copy(cf->szFaceName, f.face);
                return (LRESULT)cf->dwEffects;
            }
        } else {
            from = 0;
            to = e->len;
        }
        runs_get(e, from, to, cf);
        return (LRESULT)cf->dwEffects;
    }

    case EM_SETPARAFORMAT: {
        const PARAFORMAT *pf = (const PARAFORMAT *)lp;
        int from, to;
        if (!pf)
            return FALSE;
        rich_range(e, &from, &to);
        paras_set(e, from, to, pf);
        rich_relines(wnd, e);
        rich_notify(wnd, EN_CHANGE, ENM_CHANGE);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case EM_GETPARAFORMAT: {
        PARAFORMAT *pf = (PARAFORMAT *)lp;
        int from, to;
        if (!pf)
            return 0;
        rich_range(e, &from, &to);
        paras_get(e, from, to, pf);
        return (LRESULT)pf->dwMask;
    }

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
        rich_selchange(wnd, e);
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
                rich_selchange(wnd, e);
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
            rich_selchange(wnd, e);
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
            rich_insert(wnd, e, "\r"); /* one character, as it is stored */
        } else if (ch == '\b') {
            rich_remember(e);
            if (!rich_delete_selection(wnd, e) && e->caret > 0)
                rich_delete_range(wnd, e, e->caret - 1, e->caret);
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
        rich_selchange(wnd, e);
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
                rich_selchange(wnd, e);
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
            /* One place, since a paragraph mark is one character here. */
            if (e->caret > 0)
                e->caret--;
            break;
        case VK_RIGHT:
            if (e->caret < e->len)
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
            if (wnd->style & (WS_DISABLED | ES_READONLY))
                return 0;
            rich_remember(e);
            if (!rich_delete_selection(wnd, e) && e->caret < e->len)
                rich_delete_range(wnd, e, e->caret, e->caret + 1);
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
        rich_selchange(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* For the test: see ween_internal.h. */
int ween_rich_run_count(HWND w)
{
    ween_rich *e = w ? rich_state(w) : NULL;
    return e ? e->runs : 0;
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
