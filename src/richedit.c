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
    int first;  /* whether it is the first line of its paragraph, which is
                 * the one the start indent applies to on its own */
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
    /* **The *rest's*, against the first line** -- `dxOffset`, and
     * `rich_line_left` adds it to every line that is not the first, which is
     * what MSDN says and what riched20's RTF shows. The comment here said
     * the opposite for months.
     *
     * It cost a real detour: handing over the bullet indent I gave the field
     * values as `dxStartIndent 0, dxOffset 720`, Dan checked them against
     * this line, and it told him I had them backwards. He trusted the
     * comment over the measurement and over the header, wrote the ruler's
     * two markers into each other's fields, and found it only by going to
     * measure something else. **A comment that is wrong about its own field
     * is worse than none**, because it is exactly where somebody looks to
     * check themselves. */
    LONG offset;
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
    /* **A stack of transactions, and a transaction is the whole document.**
     *
     * It began as one string that EM_UNDO swapped -- right for the EDIT in
     * src/controls.c, wrong here -- and became a stack of strings, which
     * fixed jd's first report and not his second: *"if you change the style
     * and type something, only the style is undone."* A style alters runs
     * and not text, so a record made of text cannot hold it.
     *
     * Sam read riched20 for the shape rather than any of us guessing it
     * (tools/vm/undoprobe.txt):
     *
     *     "hello" typed a character at a time   -> 1 undo, empty
     *     "ab cd" typed a character at a time   -> 1 undo, empty
     *     EM_GETUNDONAME for that step          -> UID_TYPING
     *     120 formatting changes                -> 100 undos, then no more
     *     bold 0..3, type XY, undo, undo        -> the typing, then the style
     *
     * So: a step holds text **and** runs; a typed run is one step however
     * many characters it is; a formatting change is a step of its own; and
     * the stack is a hundred deep. */
    struct rich_step {
        char *text;
        struct rich_run *run;
        int runs;
        int caret;
        int typing; /* still absorbing characters, per UID_TYPING */
    } *undo;
    int undos, undo_cap;
    int first_visible; /* the top line drawn */
    int sb_grab;       /* where in the thumb a drag took hold, or -1 */
    int hsb_grab;      /* the same for the horizontal bar */
    /* The last double click, so that a press close to it in time and place
     * can be told from a first one: win32 has no triple-click message, so a
     * control that wants the third press counts them itself. */
    unsigned long dbl_ms;
    int dbl_x, dbl_y;
    /* Whether dbl_ms means anything yet. A flag rather than a zero
     * timestamp, because under the headless clock zero is a real time --
     * the same trap the backend's own click pairing documents, and the one
     * that made the third press here read as a first. */
    int dbl_seen;
    /* The display line a drag from the selection bar started on, or -1 when
     * no such drag is running. */
    int bar_anchor_row;
    /* Auto word selection: a drag inside one word takes characters, and the
     * moment it leaves that word it takes whole words and goes on doing so.
     * These are the word the press landed in, and whether the drag has left
     * it yet. */
    int drag_word_from, drag_word_to, drag_snapped;
    /* A press that landed inside the selection: it may become a drag of the
     * text rather than a new selection, and until the pointer moves it is
     * not yet either. */
    int dnd_pending, dnd_active, dnd_x, dnd_y;
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
    /* **The format text arrives in when nothing has said otherwise**, which
     * is neither the selection's nor the insertion point's and outlives both.
     * `EM_SETCHARFORMAT` with `SCF_DEFAULT` sets it and `WM_SETTEXT` lays new
     * text out in it -- measured on riched20, tools/vm/deffmt.txt:
     *
     *   SCF_DEFAULT   Arial 10, then SetWindowTextA -> the text is Arial
     *   SCF_SELECTION Arial 10, then SetWindowTextA -> the control's own face
     *
     * Without it WordPad loses a document's face the moment one is opened:
     * Arial before, the control's own after. **`SCF_DEFAULT` is `0x0000`**,
     * so it cannot be tested for with `&` -- a default call is one with
     * neither SCF_SELECTION nor SCF_ALL, which is why it used to fall through
     * to the selection branch and land in `insert`, where the next
     * `WM_SETTEXT` threw it away. */
    ween_rfmt def;
    int def_set;
    /* Whether the text is broken to the window's width. EM_SETTARGETDEVICE
     * with a width -- any width, with no device to measure it against --
     * turns it off, which is what WordPad's No Wrap sends; with nought it
     * comes back. Measured: a paragraph of ninety-one characters is three
     * lines in a control 200 wide and one line after
     * EM_SETTARGETDEVICE(0, 1440). */
    int nowrap;
    /* Whether the vertical bar is showing. A rich edit puts one up only
     * when there is something to scroll -- the machine's WordPad has none at
     * all on an empty document, where an EDIT with WS_VSCROLL always has one
     * -- so this is worked out while the lines are, and the lines are worked
     * out again when it changes, since the bar takes width from them. */
    int bar_on;
    int bar_ours; /* WS_VSCROLL is up because this control put it up */
    /* **Whether this control may ever show a vertical bar**, taken from
     * WS_VSCROLL as the window was created and then removed from the style.
     * Sam measured that the bit is a permission rather than a request
     * (tools/vm/barwhy.c): a control created without it never gets a bar
     * however much it overflows, and one created with it has the bit taken
     * off again whenever the document fits. */
    int bar_allowed;
    /* The horizontal axis, in pixels rather than lines. `scroll_x` is how far
     * the view has moved right, and it is what `rich_line_left` takes off --
     * one subtraction there reaches the painter, the caret, the hit test and
     * EM_POSFROMCHAR, because all four already go through that function.
     *
     * The three flags mirror the vertical ones exactly, on purpose: the same
     * permission model, measured the same way (tools/vm/barwhy.c for the
     * mechanism, wpscroll.txt for WordPad raising WS_HSCROLL in No wrap and
     * dropping it again when the line fits).
     *
     * **This axis was a symmetry when it landed and is a reading now.** The
     * retirement condition written here -- a control made without the bit
     * showing a bar on a line that does not fit -- was aimed at it on the
     * channel, correctly, since a whole program's behaviour rests on it. So
     * it was run, and it did not fire (barwhy.txt, section 5, No wrap and
     * 500 w's in a 756px control):
     *
     *     created WITHOUT WS_HSCROLL   550081c4   no bar, one line
     *     created WITH    WS_HSCROLL   551081c4   bar
     *
     * The same four states as the vertical pair, on the axis they were
     * assumed for. The condition stands and is now the thing that would
     * retire a measurement rather than a guess. */
    int scroll_x;
    int hbar_on;
    int hbar_ours;  /* WS_HSCROLL is up because this control put it up */
    int hbar_allowed;
    /* The selection as the parent last heard it, so that EN_SELCHANGE is
     * sent when it moves and not every time something asks. */
    int said_from, said_to;
    /* The formatting rectangle -- EM_SETRECT -- and whether one was given.
     * Without one the text is laid out in the client rectangle less the
     * border, which is what this control did before the message existed. */
    RECT fmt;
    int fmt_set;
} ween_rich;

static void rich_undo_clear(ween_rich *e);

static void rich_free(void *p)
{
    ween_rich *e = p;
    if (!e)
        return;
    free(e->text);
    rich_undo_clear(e);
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
            e->hsb_grab = -1;
            e->bar_anchor_row = -1;
            /* **`WS_VSCROLL` at creation is a permission, not a request**, so
             * it is remembered here and taken straight back off the window:
             * an empty document has nothing to scroll and riched20 shows no
             * bar on one. See rich_relines. */
            e->bar_allowed = (w->style & WS_VSCROLL) != 0;
            w->style &= ~(DWORD)WS_VSCROLL;
            e->hbar_allowed = (w->style & WS_HSCROLL) != 0;
            w->style &= ~(DWORD)WS_HSCROLL;
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

/* One run over everything, in the default face: the one a program set with
 * `SCF_DEFAULT` if it did, and the control's own if it did not. **This is the
 * line that makes an opened document keep its font**, because `rich_set_text`
 * comes through here for every `WM_SETTEXT`. */
static void runs_reset(HWND wnd, ween_rich *e)
{
    if (!runs_reserve(e, 1))
        return;
    e->runs = 1;
    e->run[0].start = 0;
    if (e->def_set)
        e->run[0].fmt = e->def;
    else
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
static void rich_insert_fmt(ween_rich *e, ween_rfmt *out);

static int rich_line_extent(HWND wnd, ween_rich *e, int start, int len,
                            int *ascent)
{
    int i = run_at(e, start), tall = 0, asc = 0;
    /* **An empty line is as tall as what would be typed on it**, which is
     * not the same as the run it sits in.
     *
     * jd: *"the cursor seems smaller than the original"*. A new document is
     * empty, and WordPad sets Arial 10 on it with EM_SETCHARFORMAT before a
     * character exists -- so the set lands on nothing selected, which arms
     * the insert format and leaves every run alone. The line had no
     * characters to take a height from and fell back to the control's own
     * face, so the caret came out the GUI font's 13 where the machine's is
     * 16. Measured both ways, empty document at 96 dpi:
     *
     *     machine, captures-sam/caret-machine.png    rows 11..26   16 tall
     *     ours, before                               rows 135..147 13 tall
     *
     * Type one character and ours was already right -- the run existed by
     * then. That is why it survived: **every reading anybody took of the
     * caret had text under it.**
     *
     * rich_insert_fmt is the control's own answer to "what will the next
     * character carry", armed format or the one before the caret, so this
     * asks the question that is already asked when a character actually
     * arrives rather than inventing a second rule.
     */
    if (len == 0 && start == e->caret) {
        ween_rfmt next;
        const ween_strike *f;
        rich_insert_fmt(e, &next);
        f = rfmt_strike(&next);
        if (f) {
            if (ascent)
                *ascent = f->ascent;
            return f->ascent - f->descent;
        }
    }
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

static int rich_inset(HWND wnd);
static int rich_selbar(HWND wnd);
static void rich_fmt(HWND wnd, RECT *r);
static int rich_bar(HWND wnd);
static int rich_visible_lines(HWND wnd);
static int rich_overflows(HWND wnd, ween_rich *e);
static int rich_hoverflows(HWND wnd, ween_rich *e);
static void rich_hrelines(HWND wnd, ween_rich *e);
static int rich_hbar(HWND wnd);
static int rich_text_width(HWND wnd, ween_rich *e);
static int rich_hpage(HWND wnd);
static int rich_rows_from(HWND wnd, ween_rich *e, int start);
static int rich_next_tab_stop(ween_rich *e, int at, int x);

/* How wide the text may be before it has to break, in pixels of client. */
/* **The right indent comes off the width the wrap is computed against**,
 * which is what riched20 does and what ours did not.
 *
 * jd: *"the right ruler's cursor does nothing on the text"*. It did nothing
 * because `dxRightIndent` was stored, reported, and never reached this line
 * -- three instruments read it back all evening and no character moved.
 *
 * Sam measured the rule rather than us inferring one, and the arithmetic
 * closes to within the width of the character that did not fit:
 *
 *     indent   breaks at   last x on line 1   client width - indent
 *          0          64                379                     408
 *        720          58                345                     360
 *       1440          52                309                     312
 *       2880          35                209                     216
 *
 * **It is per paragraph**, so the width is computed where the paragraph is
 * known rather than once for the control -- which is why this takes a
 * paragraph now and `rich_relines_once` asks per line. */
static int rich_wrap_width(HWND wnd, ween_rich *e, const ween_pfmt *pf,
                           int first)
{
    RECT r;
    int w;
    if (e->nowrap)
        return 0; /* nothing to break to */
    rich_fmt(wnd, &r);
    w = r.right - r.left - rich_bar(wnd) - rich_selbar(wnd);
    if (pf) {
        /* **Every term `rich_line_left` adds, this takes off**, or a line
         * starts indented and wraps at the full width -- overflowing by
         * exactly the indent.
         *
         * The right indent was jd's report. The *left* ones were found by
         * the differential test on the first run where both controls were
         * the same width, which is the run that could see them:
         *
         *     indent:720:-720, then more text
         *     ours     line 0 len 44, line 1 len 39        2 lines
         *     riched20 line 0 len 35, line 1 len 43, 5     3 lines
         *
         * The machine wraps the first line nine characters earlier -- the
         * start indent -- and the rest later, which is the negative offset.
         * **Ours applied both to where a line began and neither to where it
         * had to break.**
         *
         * `first` because the offset is the continuation lines', exactly as
         * in `rich_line_left`; the two now read as one rule stated twice
         * rather than two rules that happen to share a field. */
        w -= rfmt_px_twips(pf->right_indent);
        w -= rfmt_px_twips(pf->start_indent);
        if (!first)
            w -= rfmt_px_twips(pf->offset);
        /* **The bullet's own indent is deliberately NOT taken off here, and
         * that is a gap rather than a decision.**
         *
         * By the rule above -- every term `rich_line_left` adds -- it should
         * be: a bulleted first line begins eleven pixels in and wraps at the
         * full width, overflowing by that much. I wrote the term, and then
         * could not make any text demonstrate it: a break every three
         * characters, an eleven-pixel indent, and the line broke in the same
         * place with and without.
         *
         * **So the term went in unverified, and it came back out.** What is
         * measured is where a bulleted line *begins* -- Sam's four rows and
         * the eleven-pixel floor. **Whether riched20 also narrows the wrap
         * for it is a different question and nobody has asked it.** One
         * sequence answers it once there is a machine: a bulleted paragraph
         * with dense break opportunities, and where its first line breaks.
         *
         * Adding it would have been swapping one unmeasured rule for a
         * tidier unmeasured rule, which is the thing this file keeps
         * catching. */
    }
    return w > 0 ? w : 0;
}

/* Where a paragraph's next display line begins, and how long this one is.
 *
 * The machine's rule, read off riched20 in a control two hundred pixels
 * wide: a line breaks at the last space that fits, and **the space stays on
 * the line that broke** -- "the quick brown fox jumps over the lazy " is
 * forty characters and the next line begins at 40, on the "d" of "dog". A
 * word too long for a line of its own breaks at the character that fits:
 * forty-eight a's in that control are 32 and then 16. */
static int rich_wrap_len(HWND wnd, ween_rich *e, int start, int para_len,
                         int width)
{
    int i, x = 0, last_break = -1, run = run_at(e, start);
    const ween_strike *f;
    (void)wnd;
    if (width <= 0)
        return para_len;
    for (i = 0; i < para_len; i++) {
        int at = start + i;
        while (run + 1 < e->runs && e->run[run + 1].start <= at)
            run++;
        f = rfmt_strike(&e->run[run].fmt);
        /* A tab is a place the line may break, and it breaks *before* the
         * tab where a space breaks after it -- so the tab begins the new
         * line and the space stays on the old one. Both measured, in a
         * control whose client is 116 wide:
         *
         *   "a b<tab><tab><tab>c"   [0,5] [5,2]
         *   "xx<tab>yyyyyyyy..."    [0,2] [2,11] [13,14]
         *
         * The first breaks at the overflowing tab and not back at the space
         * before it; the second breaks at the tab even though what overflows
         * is a letter ten characters later. One rule fits both: the last
         * opportunity at or before the character that does not fit. */
        if (e->text[at] == '\t') {
            last_break = i;
            x = rich_next_tab_stop(e, at, x);
        } else {
            x += f ? ween_strike_char_advance(f, (unsigned char)e->text[at])
                   : 6;
        }
        if (e->text[at] == ' ')
            last_break = i + 1; /* the space goes with this line */
        if (x > width) {
            if (last_break > 0)
                return last_break;
            return i > 0 ? i : 1; /* a word too long breaks anyway */
        }
    }
    return para_len;
}

static void rich_relines_once(HWND wnd, ween_rich *e)
{
    int at = 0, top = 0, n = 0;
    if (!e)
        return;
    for (;;) {
        int start = at, len, height, ascent = 0, para_len, width;
        while (at < e->len && e->text[at] != '\r')
            at++;
        para_len = at - start; /* the mark is not part of the line */
        /* Per line, not per paragraph: the offset applies to continuation
         * lines only, so the width a line breaks at depends on which line
         * it is -- the same distinction `rich_line_left` makes. */
        width = rich_wrap_width(wnd, e, &e->para[para_at(e, start)].fmt,
                                start == e->para[para_at(e, start)].start);
        len = rich_wrap_len(wnd, e, start, para_len, width);
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
        e->line[n].first = start == e->para[para_at(e, start)].start;
        top += height;
        n++;
        if (len < para_len) {
            /* the rest of the paragraph, on lines of its own */
            at = start + len;
            continue;
        }
        if (at >= e->len)
            break;
        at++; /* past the mark */
    }
    e->lines = n;
}

/* The lines, and the bar that depends on them and that they depend on. It
 * settles in two passes: measured without a bar, and again with one if the
 * text turned out not to fit. Adding the bar can only take width away, which
 * can only add lines, so the second answer stands. */
/* **A re-wrap can shorten the document under a scroll position**, and the
 * position is not part of the line table that gets rebuilt. Found by
 * tests/monkey_test.c, which shrank it to three steps:
 *
 *     resize small, paste 60 characters, resize large
 *     -> first visible line 3, of a document that is now 2 lines long
 *
 * jd's own sentence for it is *"the editor does not seem synchronised with
 * the window size"*, and this is a third thing under that heading, after
 * wordpad's formatting rectangle and this control's scrollbar style.
 *
 * **The sanitizer does not see it.** `e->line` is grown and never shrunk, so
 * `e->line[3]` of a two-line document is stale data inside a live
 * allocation -- a wrong number rather than a bad read. It is the shape alice
 * described when she asked for an oracle: a monkey that only catches
 * crashes would have run this sequence and reported nothing. */
static void rich_clamp_scroll(HWND wnd, ween_rich *e)
{
    RECT r;
    int room, bottom, top;
    if (e->first_visible >= e->lines)
        e->first_visible = e->lines - 1;
    if (e->first_visible < 0)
        e->first_visible = 0;
    if (!e->lines)
        return;
    /* **And not scrolled past the point where the last line sits at the
     * bottom**, which this did not do at all.
     *
     * alice drove a window down and back up: thirty-one lines, forty rows of
     * room, and `first_visible` still 22 -- nine lines of text above thirty
     * rows of blank. Growing a window has to pull the view back down, because
     * there is nothing below the last line to show.
     *
     * The furthest scroll is **the smallest top whose remaining lines still
     * fit** -- the last line resting on the bottom, with up to one line's
     * worth of slack because scrolling moves in whole lines.
     *
     * There is a second rule that sounds like the same one and is not: the
     * *largest* top whose lines still fill the room. I wrote this, then
     * talked myself into that, and it is wrong by a line -- 32 lines of 13px
     * in 330px of room gives 7 against 6, and at 6 the last line is cut off
     * twelve pixels short of its bottom. Never leaving blank space and never
     * clipping the last line are different goals, and win32 takes the second:
     * you can scroll until the last line is fully visible and no further.
     *
     * Both readings fix alice's case, which is why the probe agreed with each
     * of them in turn and only the partial grow told them apart.
     *
     * Walked rather than divided, because a row count is wrong the moment two
     * lines are different heights. */
    rich_fmt(wnd, &r);
    room = r.bottom - r.top;
    bottom = e->line[e->lines - 1].top + e->line[e->lines - 1].height;
    top = e->lines - 1;
    while (top > 0 && bottom - e->line[top - 1].top <= room)
        top--;
    if (e->first_visible > top)
        e->first_visible = top;

    /* And the same, sideways. The furthest right the view goes is what the
     * machine's own bar allows, which is the standard scrollbar arithmetic
     * over the range measured in `rich_text_width` and `rich_hpage`:
     *
     *     nMax 5510, nPage 778, SB_RIGHT -> pos 4733 = max - page + 1
     *
     * Read off hscroll.txt rather than derived, and it agrees with the
     * ordinary rule, which is the only reason to use the ordinary rule. */
    {
        int max = rich_text_width(wnd, e) + 1 - rich_hpage(wnd) + 1;
        if (max < 0)
            max = 0;
        if (e->scroll_x > max)
            e->scroll_x = max;
        if (e->scroll_x < 0)
            e->scroll_x = 0;
    }
}

static void rich_relines(HWND wnd, ween_rich *e)
{
    int was;
    if (!e)
        return;
    e->bar_on = 0;
    rich_relines_once(wnd, e);
    /* **Before the vertical block, because that block returns four different
     * ways** -- disabled bars, no permission, nothing to scroll, nothing to
     * scroll any more -- and a horizontal update hung off the end of it would
     * be skipped on all four. A document one line tall and a mile wide takes
     * every one of those paths.
     *
     * The two axes depend on each other: a vertical bar narrows the room the
     * horizontal one measures against, and the reverse. It is settled by
     * running this twice, here and after the vertical bar has moved, rather
     * than by solving it -- the same thing riched20 must do, since its own
     * client shrinks on both axes when both bars come up. */
    rich_hrelines(wnd, e);
    if (wnd->style & ES_DISABLENOSCROLL) {
        rich_clamp_scroll(wnd, e);
        return;
    }
    /* **The control puts WS_VSCROLL up itself when the text overflows**, and
     * that is measured rather than inferred. Two readings of the machine's
     * editor looked for a while like they contradicted each other:
     *
     *     reference/probe/window.txt   550081C4   an empty document
     *     Sam, the same box full       552081C4
     *     the difference               0x00200000, WS_VSCROLL, and nothing
     *                                  else -- WS_HSCROLL is set in neither
     *
     * They do not contradict, and **the reading that reconciled them was
     * wrong for a year of comment-lines below this one**: it said WordPad
     * creates the editor *without* the style and riched20 adds it. It cannot.
     * `550081C4` is a style word read off a **running, idle** WordPad --
     * riched20 had already taken the bit down off an empty document -- so it
     * is a resting state and never was a creation style. See the permission
     * reading further down, which is what this paragraph now defers to.
     *
     * Without this the control scrolled perfectly well and drew no bar: jd
     * reported it as *"no scrollbar appears and you cannot write any more"*,
     * and the second half is what a caret below the last visible row looks
     * like from outside -- the characters were landing.
     *
     * **And it comes back off**, which is the third state and was measured
     * too rather than assumed from the symmetry:
     *
     *     empty                      550081C4
     *     overflowing                552081C4   WS_VSCROLL added
     *     overflowed, then emptied   550081C4   WS_VSCROLL removed
     *
     * **This said the opposite and it was the guess, not a reading.** It read
     * *"only the bit this control raised is cleared; a program that asked for
     * WS_VSCROLL at CreateWindow keeps it"*, and closed by naming the fourth
     * state as one nobody had looked at. Sam looked (`barwhy.c` row 2): a
     * control created **with** the style reads `550081C4` while empty, so
     * riched20 takes down a bit its creator asked for too. **What survives a
     * document that fits is the permission, not the bar.**
     *
     * **That gap is resolved and this is how it was found.** The raise was
     * once broader than its evidence -- every reading was of *WordPad's*
     * editor, and a bare `RichEdit20W` driven twenty-one lines into an
     * eighty-pixel control never set the bit:
     *
     *     machine, bare control, overflowing     WS_VSCROLL not set
     *     machine, WordPad's editor, overflowing WS_VSCROLL set
     *     ours, bare control, overflowing        WS_VSCROLL set   <- was here
     *
     * Two controls disagreeing about the same overflow is what sent somebody
     * looking for the bit that differed, and the answer is below.
     *
     * **Somebody read which bit does it, so the condition is here now.** This
     * said the rule was measured of WordPad's editor and generalised to every
     * rich edit, that `ES_AUTOVSCROLL` and `EM_SETRECT` were both candidates,
     * and that it was left broad on purpose until someone measured. Sam did
     * (`tools/vm/barwhy.c`):
     *
     *     created WITHOUT WS_VSCROLL  empty 550081C4  61 lines 550081C4  never
     *     created WITH    WS_VSCROLL  empty 550081C4  61 lines 552081C4  raised
     *
     * **`WS_VSCROLL` at creation is a permission, not a request.** Neither
     * candidate: the plain window bit, which is why nobody found it by
     * reasoning about names. A control created without it never shows a bar
     * however far it overflows; one created with it has the bit taken off
     * again whenever the document fits, which is why `550081C4` read off a
     * running WordPad is a *resting state* and not the style it was made
     * with.
     *
     * The guess this replaces was wrong in the direction it admitted to: we
     * put a bar on a bare control, which the machine never does.
     *
     * **What would retire this, since alice asked before it landed and the
     * next person to find WordPad barless will reach for this code first:**
     * a machine reading where a control created *with* `WS_VSCROLL` shows a
     * bar on a document that fits, or one created *without* it shows a bar on
     * a document that does not. Either kills the permission model outright.
     * It rests on `barwhy.c`'s two rows and nothing else -- four states of
     * two controls -- so it is a strong claim on a small sample, and the
     * sample is the whole of the evidence.
     *
     * **It is measured of a resting state, which is the subtlety.** Every
     * style word this project quotes was read off a running control; none is
     * a creation style. That is exactly how the reading it replaces went
     * wrong, and it applies to this one too. */
    if (!e->bar_allowed) {
        rich_hrelines(wnd, e);
        rich_clamp_scroll(wnd, e);
        return;
    }
    if (!(wnd->style & WS_VSCROLL)) {
        if (!rich_overflows(wnd, e)) {
            rich_hrelines(wnd, e);
            rich_clamp_scroll(wnd, e);
            return;
        }
        wnd->style |= WS_VSCROLL;
        e->bar_ours = 1;
    } else if (!rich_overflows(wnd, e)) {
        wnd->style &= ~(DWORD)WS_VSCROLL;
        e->bar_ours = 0;
        rich_hrelines(wnd, e);
        rich_clamp_scroll(wnd, e);
        return;
    }
    was = e->bar_on;
    e->bar_on = rich_overflows(wnd, e);
    if (e->bar_on != was)
        rich_relines_once(wnd, e);
    rich_hrelines(wnd, e);
    rich_clamp_scroll(wnd, e);
}

/* The horizontal bar, raised and lowered on the same terms as the vertical
 * one. Measured on the machine's own WordPad rather than assumed from the
 * symmetry (wpscroll.txt), because the note this replaces got the vertical
 * rule right and its reason wrong:
 *
 *     No wrap, a line past the edge      551081c4   WS_HSCROLL up
 *     the window widened so it fits      550081c4   down again
 *
 * The same document both times, only the width changing -- so it is not a
 * latch, and it is not the text being edited that moves it.
 *
 * **Wrapping is what keeps this quiet in the ordinary case**: a wrapped line
 * is never wider than its room, so `rich_hoverflows` is false and no bar
 * appears. That agrees with the two readings of the machine's wrapping
 * editor which found WS_HSCROLL clear -- and which is all `richedit.c` used
 * to have, generalised into "riched20 never puts one up". */
static void rich_hrelines(HWND wnd, ween_rich *e)
{
    if (!e->hbar_allowed) {
        e->hbar_on = 0;
        return;
    }
    if (!(wnd->style & WS_HSCROLL)) {
        if (!rich_hoverflows(wnd, e)) {
            e->hbar_on = 0;
            return;
        }
        wnd->style |= WS_HSCROLL;
        e->hbar_ours = 1;
    } else if (!rich_hoverflows(wnd, e)) {
        wnd->style &= ~(DWORD)WS_HSCROLL;
        e->hbar_ours = 0;
        e->hbar_on = 0;
        e->scroll_x = 0;
        return;
    }
    e->hbar_on = rich_hoverflows(wnd, e);
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
static void rich_step_free(struct rich_step *st)
{
    free(st->text);
    free(st->run);
    st->text = NULL;
    st->run = NULL;
}

static void rich_undo_clear(ween_rich *e)
{
    while (e->undos > 0)
        rich_step_free(&e->undo[--e->undos]);
    free(e->undo);
    e->undo = NULL;
    e->undo_cap = 0;
}

/* **A hundred, which is riched20's and not a round number somebody liked.**
 * Sam filled the stack with formatting changes to measure it -- typed
 * characters cannot, because they group, and his first attempt asked for 120
 * of them and got one undo, which is the grouping answer restated. */
#define RICH_UNDO_MAX 100

/* `typing` says this edit is a character going in, which may join the step
 * before it rather than making one of its own -- riched20's `UID_TYPING`.
 *
 * **A paragraph break is not grouped, and that is unmeasured rather than
 * decided.** Sam read that a *space* does not break a typed run; a newline
 * was not asked. Enter therefore starts a step here, which is the
 * conservative half: too many undos is a nuisance and too few loses work.
 * The reading that would settle it is one line -- type a word, Enter, type
 * another, and count. */
static void rich_remember_as(ween_rich *e, int typing)
{
    struct rich_step st;
    if (typing && e->undos > 0 && e->undo[e->undos - 1].typing)
        return; /* the run is already recorded, from before it began */
    if (e->undos >= e->undo_cap) {
        int cap = e->undo_cap ? e->undo_cap * 2 : 16;
        struct rich_step *g;
        if (cap > RICH_UNDO_MAX)
            cap = RICH_UNDO_MAX;
        g = realloc(e->undo, (size_t)cap * sizeof *g);
        if (!g)
            return; /* out of memory loses the undo, not the edit */
        e->undo = g;
        e->undo_cap = cap;
    }
    if (e->undos >= RICH_UNDO_MAX) {
        /* The oldest goes, which is what a hundred-deep stack means. */
        rich_step_free(&e->undo[0]);
        memmove(e->undo, e->undo + 1,
                (size_t)(e->undos - 1) * sizeof *e->undo);
        e->undos--;
    }
    memset(&st, 0, sizeof st);
    st.text = malloc((size_t)e->len + 1);
    if (!st.text)
        return;
    st.run = malloc((size_t)(e->runs > 0 ? e->runs : 1) * sizeof *st.run);
    if (!st.run) {
        free(st.text);
        return;
    }
    memcpy(st.text, e->text, (size_t)e->len + 1);
    memcpy(st.run, e->run, (size_t)e->runs * sizeof *st.run);
    st.runs = e->runs;
    st.caret = e->caret;
    st.typing = typing;
    /* **The depth is ours and is not measured.** riched20 keeps many steps
     * and this keeps them all; what nobody has read off the machine is how
     * many it keeps before it forgets, so no limit is invented here.
     *
     * **Nor is grouping.** Each edit is one step, so typing six characters
     * takes six undos. Real WordPad may take a typed run back in one -- that
     * is the obvious next question and it wants a reading rather than a
     * guess: type a word into the machine's editor and count the EM_UNDOs
     * back to empty. */
    e->undo[e->undos++] = st;
}

/* Everything that is not a character going in -- and any of them closes a
 * typed run, so the next character starts a step of its own rather than
 * joining one from before the interruption. */
static void rich_remember(ween_rich *e)
{
    if (e->undos > 0)
        e->undo[e->undos - 1].typing = 0;
    rich_remember_as(e, 0);
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

/* §5's drag and drop: the selected run moves to where it is dropped, stays
 * selected there, and Ctrl+Z puts it back -- which is why the whole move is
 * one undo step, taken before anything changes. Dropping inside the
 * selection changes nothing; the caller checks that before asking.
 *
 * **The formatting goes with the text.** The span is put back run by run,
 * each piece armed with the format it had, rather than taking the format of
 * wherever it landed -- which is what a rich edit moving bold text has to
 * do, and what the same insertion path already offers through insert_armed.
 */
static void rich_move_selection(HWND wnd, ween_rich *e, int dst)
{
    int from, to, n, i, at, count = 0, k, off = 0;
    char *span, *tmp;
    struct rich_piece {
        int len;
        ween_rfmt fmt;
    } *piece;
    rich_range(e, &from, &to);
    n = to - from;
    if (n <= 0 || (dst >= from && dst <= to))
        return;
    for (i = run_at(e, from); i < e->runs && e->run[i].start < to; i++)
        count++;
    if (count <= 0)
        return;
    span = malloc((size_t)n + 1);
    tmp = malloc((size_t)n + 1);
    piece = malloc((size_t)count * sizeof *piece);
    if (!span || !tmp || !piece) {
        free(span);
        free(tmp);
        free(piece);
        return;
    }
    memcpy(span, e->text + from, (size_t)n);
    span[n] = 0;
    count = 0;
    for (i = run_at(e, from); i < e->runs && e->run[i].start < to; i++) {
        int s = e->run[i].start < from ? from : e->run[i].start;
        int en = (i + 1 < e->runs && e->run[i + 1].start < to)
                     ? e->run[i + 1].start
                     : to;
        piece[count].len = en - s;
        piece[count].fmt = e->run[i].fmt;
        count++;
    }
    rich_remember(e);
    rich_delete_range(wnd, e, from, to);
    /* Dropping past the selection means dropping into text that has just
     * lost it, so the index moves back by what was taken out. */
    at = dst > to ? dst - n : dst;
    e->caret = e->anchor = at;
    for (k = 0; k < count; k++) {
        if (piece[k].len <= 0)
            continue;
        memcpy(tmp, span + off, (size_t)piece[k].len);
        tmp[piece[k].len] = 0;
        e->insert = piece[k].fmt;
        e->insert_armed = 1;
        rich_insert(wnd, e, tmp);
        off += piece[k].len;
    }
    e->insert_armed = 0;
    e->anchor = at;
    e->caret = at + n; /* it stays selected where it lands */
    free(span);
    free(tmp);
    free(piece);
    rich_changed(wnd, e);
}

static int rich_set_text(HWND wnd, ween_rich *e, const char *text)
{
    int n = text ? (int)strlen(text) : 0;
    /* **A typed run ends here even though nothing is recorded.** Text set
     * whole is not the user's change -- EM_GETMODIFY stays 0 and no undo
     * step is pushed -- but leaving the run *open* means the next character
     * typed joins a step from before the text was replaced, and undoing it
     * jumps back past the replacement.
     *
     * Found by a test that had been green for months: set "start", type "!",
     * set "before", type "!", undo -- and the undo went back to "start"
     * instead of "before". The coalescing was right and its boundary was
     * missing. */
    if (e->undos > 0)
        e->undo[e->undos - 1].typing = 0;
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

/* What a double click takes, measured of riched20 on
 * "cat_dog cat9 don't (cat)": clicking in "cat_dog" takes "cat", in "cat9"
 * takes "cat9 ", in "don't" takes "don't " and in "(cat)" takes "cat". So a
 * digit and an apostrophe are part of a word and **an underscore is not** --
 * which is the opposite of what this counted before, inherited from the EDIT
 * and never asked of the machine. The EDIT's own rule is different again and
 * is measured now too; see is_word_char in controls.c.
 *
 * A byte above 127 is not measured either way, and stays outside a word here
 * because that is what it did before. */
static int rich_is_word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '\'';
}

/* Where the selection bar's columns are: from the control's own inset to the
 * text's left edge, which is the eight pixels ES_SELECTIONBAR adds. A click
 * inside them selects rather than places the caret -- §5, measured on the
 * machine: one display line for a click, the paragraph for a double click,
 * and line by line for a drag. */
static int rich_in_selbar(HWND wnd, int x)
{
    RECT fmt;
    int bar = rich_selbar(wnd);
    if (bar <= 0)
        return 0;
    rich_fmt(wnd, &fmt);
    return x >= 0 && x < fmt.left + bar;
}

/* The whole of one display line -- a wrapped row, not a paragraph. */
static void rich_select_row(ween_rich *e, int row)
{
    if (row < 0 || row >= e->lines)
        return;
    e->anchor = e->line[row].start;
    e->caret = row + 1 < e->lines ? e->line[row + 1].start
                                  : e->line[row].start + e->line[row].len;
}

/* The whole paragraph an index is in, wrapped lines and all.
 *
 * **Whether the mark at the end goes with it is not measured**: §5 says a
 * triple click takes "the whole paragraph, wrapped lines and all" and says
 * nothing about the break. It is taken here, on the same reading as a double
 * click taking the space after a word, and because a paragraph selected in
 * WordPad highlights past its last character. ctlprobe.c asks the machine
 * for the range, and this is what it will correct. */
static void rich_select_para(ween_rich *e, int at)
{
    int p = para_at(e, at);
    int start = e->para[p].start;
    int end = p + 1 < e->paras ? e->para[p + 1].start : e->len;
    e->anchor = start;
    e->caret = end;
}

/* A double click takes the word under it, and the run of spaces if it landed
 * on one -- which is what the EDIT does and what the machine does. */
/* The word an index is in: what a double click takes, and what a drag snaps
 * to once it has left the word it started in. One function because both ask
 * and the two must not drift apart. */
/* ---- where Ctrl and an arrow put the caret --------------------------------
 *
 * jd: *"Ctrl+Right/Left (Ctrl+Shift+Right/Left) do not work as expected."*
 * They moved one character, because the `if (ctrl)` switch below had cases
 * for C, X, V, A and Z and no arrows, so they fell through to plain arrow
 * handling.
 *
 * **Every rule here is Sam's reading of riched20 on Windows 2000**
 * (tools/vm/wordkey.c), and the readings are not what a guess would have
 * produced:
 *
 *     "alpha beta gamma delta"    0 -> 6 -> 11 -> 17 -> 22, then stops
 *     from 2, inside "alpha"      -> 6         the NEXT word's start,
 *     from 8, inside "beta"       -> 11        not the current word's end
 *
 * WordPad and Notepad famously differ on that last point and the rival rule
 * gives 16 where this gives 17.
 *
 * **Punctuation is a word of its own; runs of spaces are not.** This is why
 * his second sample exists: every rival rule agrees on evenly spaced
 * lowercase words, so the first sample *could not have failed*.
 *
 *     "one two  three, four\r\nfive six"
 *     0 -> 4 -> 9 -> 14 -> 16 -> 20 -> 21 -> 26 -> 29
 *               the comma ^        ^ the paragraph mark
 *
 * **A break you can see and a break you cannot behave oppositely**, which is
 * the finding rather than a detail:
 *
 *     hard paragraph mark   16 -> 20 (the mark itself) -> 21    STOPS on it
 *     soft wrap             44 -> 48 -> 52, 48 being line 1     DOES NOT
 *
 * Either sample alone would have given a confident wrong rule. Ctrl+Left is
 * the current word's start, or the previous word's if the caret is already
 * there: 22 -> 17 -> 11 -> 6 -> 0, and from 8 -> 6.
 *
 * **The leftward walk is the exact mirror, and that is measured too** -- it
 * was flagged here as an assumption first, which is the only reason anybody
 * went and read it:
 *
 *     29 -> 26 -> 21 -> 20 -> 16 -> 14 -> 9 -> 4 -> 0
 *
 * and three singles say which rule produced it rather than which sequence:
 * 21 -> 20 (onto the mark), 20 -> 16 (the last word before it), 22 -> 21.
 * **The mark is a stop from both directions and is stepped off, not over.**
 * Being right about the answer and having read it were two different things
 * until Sam took the reading.
 *
 * Ctrl+Shift needs nothing of its own. Sam: *"moves the active end and
 * shrinks. It does not flip"* -- anchor 11 and caret 6 collapsing to 11..11
 * rather than turning round to 17 -- which is what leaving `e->anchor` alone
 * and moving `e->caret` already does. */
enum { RW_SPACE, RW_BREAK, RW_WORD, RW_PUNCT };

static int rich_wclass(char c)
{
    if (c == '\r' || c == '\n')
        return RW_BREAK;
    if (c == ' ' || c == '\t')
        return RW_SPACE;
    return rich_is_word_char(c) ? RW_WORD : RW_PUNCT;
}

static int rich_word_right(const ween_rich *e, int at)
{
    int c;
    if (at >= e->len)
        return e->len;
    c = rich_wclass(e->text[at]);
    if (c == RW_BREAK) /* the mark is a stop of its own; step off it */
        return at + 1;
    while (at < e->len && rich_wclass(e->text[at]) == c)
        at++;
    /* spaces only: a mark ahead is the next stop, not something to skip */
    while (at < e->len && rich_wclass(e->text[at]) == RW_SPACE)
        at++;
    return at;
}

static int rich_word_left(const ween_rich *e, int at)
{
    int c;
    if (at <= 0)
        return 0;
    at--;
    while (at > 0 && rich_wclass(e->text[at]) == RW_SPACE)
        at--;
    c = rich_wclass(e->text[at]);
    if (c == RW_BREAK)
        return at;
    while (at > 0 && rich_wclass(e->text[at - 1]) == c)
        at--;
    return at;
}

static void rich_word_bounds(ween_rich *e, int at, int *pfrom, int *pto)
{
    int from = at, to = at;
    *pfrom = *pto = at;
    if (!e->len)
        return;
    if (from >= e->len)
        from = e->len - 1;
    to = from;
    if (rich_is_word_char(e->text[from])) {
        while (from > 0 && rich_is_word_char(e->text[from - 1]))
            from--;
        while (to < e->len && rich_is_word_char(e->text[to]))
            to++;
        /* The trailing space goes with the word, as it does in an EDIT: the
         * machine takes 8..13 for "cat9 " and 13..19 for "don't ", and
         * 20..23 for the "cat" inside "(cat)", where a bracket follows and
         * there is no space to take. Only one space stood after either word
         * that was measured; a run of them is this control's own reading. */
        while (to < e->len && e->text[to] == ' ')
            to++;
    } else {
        while (from > 0 && !rich_is_word_char(e->text[from - 1]) &&
               e->text[from - 1] != '\n' && e->text[from - 1] != '\r')
            from--;
        while (to < e->len && !rich_is_word_char(e->text[to]) &&
               e->text[to] != '\n' && e->text[to] != '\r')
            to++;
    }
    *pfrom = from;
    *pto = to;
}

static void rich_select_word(ween_rich *e)
{
    int from, to;
    rich_word_bounds(e, e->caret, &from, &to);
    e->anchor = from;
    e->caret = to;
}

/* ---- geometry ------------------------------------------------------------ */

/* A rich edit's text starts one pixel inside its border, as an edit's does
 * in a strike font; see edit_margin in controls.c for the measurement. */
/* ---- finding -------------------------------------------------------------
 *
 * All of this is riched20's, measured through EM_FINDTEXTEX in
 * tools/vm/ctlprobe.c against "one cat two Cat three catalog cat." and
 * "cat cat. cat-o cat9 cat_ (cat)":
 *
 *   - **The direction is FR_DOWN and not the order of the range.** 0..-1
 *     without it answers -1 even for a word at 0; 34..0 *with* it answers -1
 *     as well, since it goes forward from 34 in a document 34 long.
 *   - **The whole match has to lie inside the range.** Forwards, 0..6 for a
 *     match at 4..7 is -1 and 0..7 is 4. Backwards, 20..13 for a match at
 *     12..15 is -1 and 20..12 is 12.
 *   - **The end the search starts from counts.** Forwards from 30 finds the
 *     match at 30 and from 31 does not; backwards from 15 finds the one that
 *     ends at 15 and from 14 does not.
 *   - **Backwards answers the nearest match behind**, not the first in the
 *     document: 20..0 is 12, not 4.
 *   - **Case is ignored unless FR_MATCHCASE.** Searching "cat" finds "Cat".
 *   - **A word, for FR_WHOLEWORD, is letters and digits.** "cat" whole-word
 *     skips "cat9" and takes "cat_", "cat.", "cat-o" and "(cat)" -- so a
 *     digit is part of a word and an underscore is not. What a byte above
 *     127 is has not been asked, and is taken here as not part of one.
 *   - **The empty string is never found**, and -1 comes with chrgText set to
 *     -1..-1 rather than left alone.
 *   - **A find moves nothing**: the selection is where it was afterwards.
 *   - And the document's own storage is what is searched, so a paragraph
 *     mark is the single CR it is stored as: "e\rt" is found across the
 *     break in "one\r\ntwo" and "e\r\nt" is not.
 *
 * Two readings of -1 going backwards are *not* measured, and are taken here
 * the way the forward ones are: a cpMin of -1 starts at the end, and a
 * cpMax of -1 stops at the start. Every backwards case that was asked had
 * both ends written out.
 */
/* Note that this is *not* rich_is_word_char above, which is what a double
 * click takes and which counts an underscore in. The two differ in exactly
 * that character, and the difference is measured on this side and inherited
 * from the EDIT on the other: whether a double click in a rich edit takes
 * "cat_dog" whole has not been asked. ctlprobe.c now asks it. */
static int rich_wholeword_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

static int rich_same(const char *a, const char *b, int n, int cased)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (!cased) {
            if (x >= 'A' && x <= 'Z')
                x = (unsigned char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z')
                y = (unsigned char)(y - 'A' + 'a');
        }
        if (x != y)
            return 0;
    }
    return 1;
}

static int rich_match_at(ween_rich *e, int at, const char *what, int n,
                         DWORD flags)
{
    if (!rich_same(e->text + at, what, n, (flags & FR_MATCHCASE) != 0))
        return 0;
    if (flags & FR_WHOLEWORD) {
        if (at > 0 && rich_wholeword_char((unsigned char)e->text[at - 1]))
            return 0;
        if (at + n < e->len && rich_wholeword_char((unsigned char)e->text[at + n]))
            return 0;
    }
    return 1;
}

static LRESULT rich_find(ween_rich *e, DWORD flags, const CHARRANGE *chrg,
                         const char *what, CHARRANGE *found)
{
    int n = what ? (int)strlen(what) : 0, at;
    int lo, hi;
    if (found) {
        found->cpMin = -1;
        found->cpMax = -1;
    }
    if (!e || !n || !chrg)
        return -1;
    if (flags & FR_DOWN) {
        lo = chrg->cpMin < 0 ? 0 : (int)chrg->cpMin;
        hi = chrg->cpMax < 0 ? e->len : (int)chrg->cpMax;
        if (hi > e->len)
            hi = e->len;
        for (at = lo; at >= 0 && at + n <= hi; at++)
            if (rich_match_at(e, at, what, n, flags))
                break;
        if (at < 0 || at + n > hi)
            return -1;
    } else {
        hi = chrg->cpMin < 0 ? e->len : (int)chrg->cpMin;
        lo = chrg->cpMax < 0 ? 0 : (int)chrg->cpMax;
        if (hi > e->len)
            hi = e->len;
        if (lo < 0)
            lo = 0;
        for (at = hi - n; at >= lo; at--)
            if (rich_match_at(e, at, what, n, flags))
                break;
        if (at < lo)
            return -1;
    }
    if (found) {
        found->cpMin = at;
        found->cpMax = at + n;
    }
    return at;
}

static int rich_inset(HWND wnd)
{
    return ween_border_width(wnd) ? 1 : 0;
}

/* The selection bar: the strip down the left where the pointer becomes an
 * arrow and a click takes a whole line. It is where WordPad's text sits
 * further in than a plain control's, and it is a style bit rather than a
 * margin -- which is what took a boot to find, since EM_GETMARGINS answers
 * nought either way and the paragraph's indents are nought too.
 *
 * Measured on the machine with the same control either way and nothing but
 * the bit different: the first character stands at x=1 without it and x=9
 * with it. Eight pixels, and eight with the message font and with Arial 10
 * alike, so it does not follow the font. WordPad's own editor is at 14 --
 * five pixels this does not explain and which are the frame's, not the
 * control's: see docs/testing.md.
 *
 * Eight is the machine's number at 96 dpi, scaled here the way every other
 * measured metric in this library is. What the bar is at another dpi has not
 * been asked. */
static int rich_selbar(HWND wnd)
{
    /* **No selection bar inside an explicit formatting rectangle.**
     *
     * `rich_fmt`'s comment used to offer two readings of WordPad's character
     * 0 sitting at editor-client 14 -- a rect of 14 with the bar inside it,
     * or a rect of 6 with the bar on top -- and said a measurement would
     * settle it. Sam measured it (tools/vm/hpage.txt): it is neither. Once a
     * rectangle exists riched20 drops the bar entirely.
     *
     * Ours put character 0 at `rect + 8` where the machine puts it just
     * before the rect, and the two agree exactly until the message is sent
     * -- so nothing that never sends `EM_SETRECT` could have seen it, and
     * wordpad sends one. */
    ween_rich *e = rich_state(wnd);
    if (e && e->fmt_set)
        return 0;
    return (wnd->style & ES_SELECTIONBAR) ? ween_ncm(8) : 0;
}

/* The box the text is laid out in.
 *
 * Without EM_SETRECT this is the client rectangle less the border, which is
 * exactly what every one of these places used to work out for itself -- so a
 * control nobody has sent the message to lays out to the pixel it did before.
 *
 * **The selection bar sits inside it**, so a line begins at `left` plus the
 * bar and not at `left`. That is the choice this file makes and nobody has
 * measured which the machine makes: WordPad's first character is at
 * editor-client 14 either way, and 14 could be a rect of 14 with the bar
 * inside it or a rect of 6 with the bar added on top. It is written this way
 * because it is what the client rectangle already did -- the client holds the
 * bar and then the text -- so the message replaces the box and changes
 * nothing else. A measurement will settle it; this note is what makes that
 * cheap.
 *
 * The vertical scroll bar is *not* inside it and is still taken off the wrap
 * width separately, for the same reason: that is what happened before. */
static void rich_fmt(HWND wnd, RECT *r)
{
    ween_rich *e = rich_state(wnd);
    int inset;
    if (e && e->fmt_set) {
        /* **A rectangle given to `EM_SETRECT` lays text out one pixel wider
         * on each side than it asks for**, which is measured rather than
         * reasoned. Sam's eight rows, a 756px control, the rect inset by N
         * on every side (tools/vm/hpage.txt):
         *
         *     inset   0   1   2   4  10  20  40  80
         *     x0      0   0   1   3   9  19  39  79
         *     page  756 756 754 750 738 718 678 598
         *
         * **79 at an inset of 80, not 72 and not 78** -- an offset rather
         * than a scaling, over two orders of magnitude rather than over the
         * four values it was first read off. He had written "about N-1" over
         * the first four and went back for these when I would not build on an
         * approximation.
         *
         * He recorded them as two rules -- `x0 = max(0, inset - 1)` and
         * `page = client - 2 * x0` -- and deliberately did not tidy them into
         * one, on the grounds that the version that gets withdrawn is always
         * the one tidied first. **Inflating the rectangle by a pixel
         * reproduces both, exactly, on all eight rows**, so that is what this
         * does: any implementation has to choose some expression, and two
         * special cases that happen to agree would be less honest than the
         * one that explains them.
         *
         * **That was a hypothesis when it was written and the experiment has
         * since been run.** Every rectangle in those eight rows was inset
         * equally on all sides, so `2 * x0` and "a pixel on each side" are
         * the same number in all of them; a *non-symmetric* rectangle tells
         * them apart, and wordpad's is asymmetric -- `alignEditorToRuler`
         * moves the left and leaves the right. Sam went and took it:
         *
         *     left 20, right inset  0    x0 19   page 737
         *     left 20, right inset 40    x0 19   page 698
         *
         * **Same origin, thirty-nine pixels apart**, where `2 * x0` predicts
         * 718 for both. Inflating each side independently -- which is what
         * the two clamped lines below do -- gives 737 and 698, because a
         * pixel off the left and a pixel onto the right is exactly
         * `client - f(left) - f(right)` with `f(k) = max(0, k - 1)`. The same
         * `f` as the origin's, which is why it is one rule and not two that
         * agree.
         *
         * (Measured here through `EM_POSFROMCHAR` and a right-aligned
         * paragraph, which carries a constant four-pixel bias -- the no-rect
         * control reads 742 where the rule says 746 -- so what is established
         * from this side is the *difference* of 39 and the origin, not the
         * absolute width.)
         *
         * **Horizontal only, because that is all that was measured.** `x0`
         * and `page` are both horizontal quantities; whether the top and
         * bottom inflate too is unasked, and inflating them here would be
         * exactly the unmeasured symmetry that the WS_HSCROLL permission
         * nearly became. */
        RECT cr;
        GetClientRect(wnd, &cr);
        *r = e->fmt;
        r->left -= 1;
        r->right += 1;
        if (r->left < cr.left)
            r->left = cr.left;
        if (r->right > cr.right)
            r->right = cr.right;
        return;
    }
    GetClientRect(wnd, r);
    inset = rich_inset(wnd);
    r->left += inset;
    r->top += inset;
    r->right -= inset;
    r->bottom -= inset;
}

static int rich_bar(HWND wnd)
{
    ween_rich *e = rich_state(wnd);
    if (!(wnd->style & WS_VSCROLL))
        return 0;
    if (wnd->style & ES_DISABLENOSCROLL)
        return ween_scroll_metric(); /* always there, disabled when idle */
    return (e && e->bar_on) ? ween_scroll_metric() : 0;
}

/* The horizontal bar's height, and it takes room the same way the vertical
 * one takes width. Measured on the machine's WordPad, which is the only
 * reason to believe it rather than assume the symmetry:
 *
 *     window 760x386   no bar   client 756x382
 *                      bar up   client 756x366     16 off the height
 *     window 1024x597  both     client 740x529     and 16 off the width
 */
static int rich_hbar(HWND wnd)
{
    ween_rich *e = rich_state(wnd);
    if (!(wnd->style & WS_HSCROLL))
        return 0;
    if (wnd->style & ES_DISABLENOSCROLL)
        return ween_scroll_metric();
    return (e && e->hbar_on) ? ween_scroll_metric() : 0;
}

/* Whether there is anything to scroll: the document taller than the room.
 *
 * **A height, not a line count.** `e->lines > rich_visible_lines(wnd)` was
 * the old test and it asks a different question once the capacity is counted
 * from `first_visible` -- scrolled down, a short tail fits, and the bar would
 * take itself away underneath the scroll. It also could not be right for
 * mixed sizes in either direction: twelve small lines and one huge one is a
 * count of thirteen either way.
 *
 * The threshold is `>`: content equal to the room does not raise it.
 * Measured on riched20, and the direction matters -- it is the difference
 * between a bar on an exactly-full control and no bar. **What the machine
 * measures the room *against* -- the client or the inset formatting
 * rectangle -- is a separate open question**, and this keeps the rectangle
 * the old test used rather than settling it by choosing. The dump records
 * `vscroll` but nobody has asked the machine where the edge is. */
static int rich_overflows(HWND wnd, ween_rich *e)
{
    return e && e->lines > rich_rows_from(wnd, e, 0);
}

/* How many lines fit, counted from the first visible one.
 *
 * **This used to be `(r.bottom - r.top) / rich_line_height(wnd)`, and that
 * was jd's "the scrollbar appears too late".** alice found the cause:
 * `rich_line_height` reads `wnd->font`, the `WM_SETFONT` font -- and WordPad
 * never sends `WM_SETFONT`, it sets Arial 10 with `EM_SETCHARFORMAT
 * SCF_DEFAULT`. So the capacity was measured in the shell font while every
 * line was laid out in the document's.
 *
 * Line *layout* was already format-aware (`rich_line_extent` goes through
 * `rfmt_strike`), which is why the pitch was right and only the scrollbar was
 * wrong -- this was the one call site still asking the window. Measured
 * before the change, client 156 tall, Arial 24, a line 19px so eight fit:
 *
 *     9 lines    last line spans 153..172, 16px past the bottom   no bar
 *    11 lines    last line top 191, 35px past                     no bar
 *    13 lines    13 * 13px of shell font finally exceeds 156      bar
 *
 * Five lines late. The table knows each line's real height, so it is asked
 * instead: the answer is exact for mixed sizes rather than approximately
 * right for uniform ones, and it needs no font at all.
 *
 * Counted from `first_visible` because with variable heights "how many fit"
 * depends on where you start. */
static int rich_rows_from(HWND wnd, ween_rich *e, int start)
{
    RECT r;
    int room, top, rows = 0, i;
    rich_fmt(wnd, &r);
    /* The horizontal bar takes sixteen pixels off the bottom, so a document
     * that fits without one may not fit with it -- measured on the machine's
     * WordPad, whose client goes 756x382 to 756x366 when the bar comes up. */
    room = r.bottom - r.top - rich_hbar(wnd);
    if (!e || !e->lines) {
        int line = rich_line_height(wnd);
        rows = line > 0 ? room / line : 0;
        return rows > 0 ? rows : 1;
    }
    i = start;
    if (i < 0 || i >= e->lines)
        i = 0;
    top = e->line[i].top;
    for (; i < e->lines; i++) {
        if (e->line[i].top + e->line[i].height - top > room)
            break;
        rows++;
    }
    /* **Never nought**, so a control too short for a single line still has a
     * page of one and a one-line document never raises the bar.
     *
     * That floor is inherited from the arithmetic this replaced -- it fell
     * out of `room / line` being clamped -- and it is **not measured**. What
     * riched20 does with a line taller than its client is a question nobody
     * has asked; monkey_test.c asserts the bar is down on a one-line
     * document, which is this floor read back rather than an independent
     * reading. Kept because dropping it would change an untested behaviour
     * while fixing a measured one. */
    return rows > 0 ? rows : 1;
}

static int rich_visible_lines(HWND wnd)
{
    ween_rich *e = rich_state(wnd);
    return rich_rows_from(wnd, e, e ? e->first_visible : 0);
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

/* The horizontal bar's state, in **pixels** where the vertical one is in
 * lines. Every field is a measurement rather than a convention:
 *
 *     max   the longest line's width + 1     nMax 5501 for 5500px of text
 *     page  client - 14                      742 at 756, 1006 at 1020
 *     line  7                                the arrow, and it is a constant:
 *                                            the same 7 at 8, 9 and 11 pixels
 *                                            per character, which is what
 *                                            separates it from "one average
 *                                            character width"
 *     pos   pixels, 1:1 with the view        pos 7 per arrow, 778 per page
 */
static ween_sbstate rich_hsbstate(HWND wnd)
{
    ween_rich *e = rich_state(wnd);
    ween_sbstate st;
    st.pos = e ? e->scroll_x : 0;
    st.min = 0;
    st.max = e ? rich_text_width(wnd, e) + 1 : 0;
    st.page = rich_hpage(wnd);
    st.line = 7;
    return st;
}

/* Where a tab takes the pen, in pixels from the text's own left edge.
 *
 * Measured on the machine with EM_POSFROMCHAR, in a rich edit whose client
 * is 556 wide and whose font is the message font. With no stops of its own
 * a tab goes to the next multiple of 48 -- half an inch at 96 dpi -- so
 * nine of them stand at 49, 97, 145 and so on in a control whose text
 * begins at 1. With stops at 300, 1000 and 2137 twips the same nine stand
 * at 21, 68 and 143, which is rfmt_px_twips of each: 20, 67 and 142. Not
 * the floor of any -- 1000 twips is 66 and two thirds and the control puts
 * it at 67 -- and not the ceiling either, since 2137 is 142 and a half and
 * the control puts it at 142.
 *
 * Past the last stop of its own the half-inch grid takes over again,
 * measured from the same left edge and not from that stop: one stop at 500
 * twips gives 34, then 49, 97, 145 -- 33 and then the ordinary 48s. And a
 * stop the pen has already passed is skipped: seven w's reach 57 and the
 * tab after them goes to 97, whether the paragraph's only stop is at 300
 * twips or it has none at all.
 *
 * Stops belong to the paragraph, which the same measurement says twice: a
 * stop set on the first of two paragraphs leaves the second on the default
 * grid, and the RTF riched20 writes for it is "\pard\tx300 ... \pard",
 * the second paragraph's \pard clearing what the first had.
 *
 * The top byte of a stop is its alignment and its leader in Rich Edit 2.0's
 * documentation. Nothing here has measured what the control does with one,
 * so the position is taken and the rest dropped. */
static int rich_next_tab_stop(ween_rich *e, int at, int x)
{
    const ween_pfmt *pf = &e->para[para_at(e, at)].fmt;
    int i, step;
    for (i = 0; i < pf->tabs && i < MAX_TAB_STOPS; i++) {
        int stop = rfmt_px_twips(pf->tab[i] & 0xffffff);
        if (stop > x)
            return stop;
    }
    step = rfmt_px_twips(720);
    if (step < 1)
        step = 1;
    return (x / step + 1) * step;
}

/* How far a span of one run's text carries the pen, from an x that is
 * already measured from the line's left edge -- which a tab needs and a
 * letter does not. */
static int rich_run_pen(ween_rich *e, const ween_strike *f, int at, int n,
                        int x)
{
    int i, run = 0;
    for (i = 0; i < n; i++) {
        if (e->text[at + i] == '\t') {
            if (run)
                x += f ? ween_strike_pen(f, e->text + at + i - run, run)
                       : run;
            run = 0;
            x = rich_next_tab_stop(e, at + i, x);
            continue;
        }
        run++;
    }
    if (run)
        x += f ? ween_strike_pen(f, e->text + at + n - run, run) : run;
    return x;
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
        x = rich_run_pen(e, f, at, next - at, x);
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
/* **What a bullet takes from the first line of its paragraph.**
 *
 * Measured on the machine, tools/vm/bulletprobe.txt -- a bare RichEdit20W
 * with `PFN_BULLET` and nothing else set:
 *
 *     the first character   x 1 -> 12      the text moves eleven pixels
 *     the wrapped line      back to x 1    and the rest do NOT
 *     dxStartIndent         0, unchanged   the paragraph format says
 *     dxOffset              0, unchanged   nothing about any of it
 *     line pitch            16 either way
 *
 * **The indent is the control's, and it is not reported through the
 * paragraph format.** A program that reads `dxStartIndent` to find where a
 * bulleted paragraph begins gets zero and is not being lied to -- it is
 * asking the wrong thing.
 *
 * **And there is no hang here.** WordPad's bulleted paragraphs do hang --
 * its Format > Paragraph reads Left 0.5", First line -0.5" -- but that is
 * WordPad setting a paragraph format, which this already honours. The two
 * readings disagreed about the wrapped line for that reason and both were
 * right; only the message-level pass could tell them apart, and it is the
 * reason to have asked for it before the pixels.
 *
 * The eleven scales with dpi as the selection bar's eight does. **Not
 * measured**: the glyph's size at another dpi, and what a bullet does to a
 * centred or right-aligned paragraph -- the reading is of a left-aligned
 * one. */
static int rich_bullet_indent(ween_rich *e, int row)
{
    const ween_pfmt *pf = &e->para[para_at(e, e->line[row].start)].fmt;
    if (pf->numbering != PFN_BULLET || !e->line[row].first)
        return 0;
    /* **The eleven is a floor on the first line, not a fallback.** A hanging
     * indent moves the text past the bullet; an indent *smaller* than the
     * bullet does not pull the text back into it.
     *
     *     riched20, start 0    offset 60tw (4px)   first line 11
     *                          offset 120tw (8px)  first line 11
     *                          offset 165tw (11px) first line 11
     *                          offset 240tw (16px) first line 16
     *
     * **This was `offset > 0 ? start + offset : start + 11` and gave 4, 8,
     * 10, 16.** That rule fitted every pair anyone had measured, because the
     * two agree for any offset above eleven pixels and nobody had gone
     * below -- WordPad's own half inch is forty-eight. Dan proposed the
     * `max`, I said the two were not the same rule and named the band where
     * they part, and Sam read that band. **The measurement exists because
     * the disagreement was stated precisely enough to be checkable.**
     *
     * A floor is also what the drawing needs, which is how Dan got to it:
     * the glyph is five pixels wide, so text placed four in was drawn
     * through the dot. That was the argument for it and not the evidence --
     * the evidence is the four rows above. */
    {
        int floor = ween_ncm(11);
        int body = rfmt_px_twips(pf->offset);
        return body > floor ? body : floor;
    }
}

/* Where a line begins if the view has not been scrolled sideways. Split out
 * from `rich_line_left` because the horizontal machinery needs the *layout*
 * position -- how wide the text is, where the caret stands within it -- and
 * would chase its own tail if it asked the scrolled one. */
static int rich_line_left_unscrolled(HWND wnd, ween_rich *e, int row)
{
    int sb = rich_bar(wnd);
    const ween_pfmt *pf = &e->para[para_at(e, e->line[row].start)].fmt;
    /* dxStartIndent is where the *first* line begins and dxOffset is where
     * the rest do, against it -- which is what riched20's own RTF says: a
     * paragraph set to 720 and -360 comes out "\li360\fi360", a left indent
     * of 360 with the first line 360 further in. */
    RECT cr;
    int left, right, width;
    rich_fmt(wnd, &cr);
    left = cr.left + rich_selbar(wnd) + rfmt_px_twips(pf->start_indent) +
           (e->line[row].first ? 0 : rfmt_px_twips(pf->offset)) +
           rich_bullet_indent(e, row);
    if (pf->alignment == PFA_LEFT || !pf->alignment)
        return left;
    right = cr.right - sb - rfmt_px_twips(pf->right_indent);
    width = rich_x_of(e, row, e->line[row].len);
    if (pf->alignment == PFA_RIGHT)
        return right - width;
    if (pf->alignment == PFA_CENTER)
        return left + (right - left - width) / 2;
    return left;
}

/* **The one subtraction that scrolls the view sideways.** Four callers go
 * through here -- the painter, the caret, the hit test and EM_POSFROMCHAR --
 * so they all move together and none can be forgotten. That the machine's
 * EM_POSFROMCHAR reports scrolled coordinates is measured rather than
 * assumed: with 500 w's and the view at the end, character 0 answers x
 * -4723 (hscroll.txt), so it is the same convention. */
static int rich_line_left(HWND wnd, ween_rich *e, int row)
{
    return rich_line_left_unscrolled(wnd, e, row) - e->scroll_x;
}

/* How wide the widest line is, from the text's own left edge.
 *
 * This is what the bar's range is made of. On the machine `nMax` is the
 * longest line's width plus one, with no residue over two fonts and two
 * lengths (hscroll.txt):
 *
 *     500 w's at 11px   nMax 5501       501 of them   nMax 5510
 *     500 at 8px        nMax 4001       501           nMax 4009
 *
 * The indent is included and the selection bar is not, because character 0
 * sits at x 9 in a control whose nMax is 5501 for 5500 pixels of text. */
static int rich_text_width(HWND wnd, ween_rich *e)
{
    int row, widest = 0;
    if (!e || !e->lines)
        return 0;
    for (row = 0; row < e->lines; row++) {
        int w = rich_line_left_unscrolled(wnd, e, row) +
                rich_x_of(e, row, e->line[row].len);
        if (w > widest)
            widest = w;
    }
    widest -= rich_selbar(wnd);
    return widest > 0 ? widest : 0;
}

/* The track step, and the bar's page: **the room to the right of the first
 * character, less one.**
 *
 * This was `client - 14` for a while, which is WordPad's number, and it was
 * wrong -- fourteen is not a constant, it is *where WordPad's text begins*.
 * Four measurements, three different text origins and two client widths:
 *
 *     control                     client  page   client - page - 1
 *     bare, ES_SELECTIONBAR         756    746          9   <- and x0 is 9
 *     bare, no selection bar        756    754          1   <- and x0 is 1
 *     WordPad                       756    742         13
 *     WordPad, maximised           1020   1006         13
 *
 * **`client - page - 1` is the x of character 0 in every row where that x is
 * known**, and it is the same 13 for WordPad at two widths. A flat fourteen
 * fits neither bare row -- it would give a control without a selection bar a
 * page twelve pixels short -- and the sample contains exactly the case that
 * separates them, which a single width could not have.
 *
 * **So the page follows the text origin, and the four pixels this file used
 * to call unexplained are the *same* four already written down over
 * `rich_fmt`: WordPad's first character sits at editor-client 13 or 14 and a
 * bare riched20 given WordPad's own style word puts it at 9.** One anomaly
 * seen from two sides, not two. Whoever closes that note closes this, and
 * until then ours is short by exactly the amount our text origin is.
 *
 * Written as the library's own origin rather than as WordPad's number, so it
 * cannot be a pair of errors cancelling: tune the constant to 14 and the page
 * is right only for the one control shape that hides the origin bug. */
/* **And a control with a formatting rectangle uses a different rule, which
 * wordpad's editor has.** `hpage.txt`'s rect rows, thirteen of them:
 *
 *     page = client - f(left) - f(right),   f(k) = max(0, k - 1)
 *
 * with `left` and `right` the rectangle's insets from the client edges. Zero
 * residue over all thirteen -- and `f` is the *same function* as the text
 * origin's, `x0 = max(0, left - 1)`, so the page is the client less the
 * origin at each end rather than a second rule that happens to agree.
 *
 * **The first eight rows said something else and it was wrong.** They were
 * all *symmetric* insets, where `2 * x0` and `f(left) + f(right)` are the
 * same number, and `2 * x0` is what they fit. wordpad's own rectangle is
 * asymmetric -- `alignEditorToRuler` sets `want.left = r.left + dx` and
 * leaves the right alone -- so the tidy rule read off those rows was fitted
 * to the one shape the program being copied does not use. Five asymmetric
 * rows kill it outright:
 *
 *     left 20 right  0   x0 19   page 737
 *     left 20 right 40   x0 19   page 698     same x0, and 39 pixels apart
 *
 * `2 * x0` predicts 718 for both. **The separating case was worth the boot
 * it cost**, and it is the fourth time today a rule that fitted every row
 * anybody had was fitted to a sample that could not disagree.
 *
 * This does **not** depend on the origin change: it is a function of the
 * rectangle the caller set, not of where we then put the text. */
static int rich_hpage(HWND wnd)
{
    ween_rich *e = rich_state(wnd);
    RECT cr;
    int page;
    GetClientRect(wnd, &cr);
    if (e && e->fmt_set) {
        int l = e->fmt.left - cr.left, r = cr.right - e->fmt.right;
        page = (cr.right - cr.left) - rich_bar(wnd) - (l > 1 ? l - 1 : 0) -
               (r > 1 ? r - 1 : 0);
    } else {
        page = (cr.right - cr.left) - rich_bar(wnd) - rich_inset(wnd) -
               rich_selbar(wnd) - 1;
    }
    return page > 1 ? page : 1;
}

/* Whether a line runs past the right edge.
 *
 * **The exact threshold is not measured.** What is: a 901px line in a 1020px
 * client shows no bar, and the same document widened past it does
 * (wpscroll.txt). That brackets the boundary and does not pin it, so this
 * takes the plain reading -- the text is wider than the room for it -- and
 * says so rather than dressing the bracket up as a rule. */
static int rich_hoverflows(HWND wnd, ween_rich *e)
{
    RECT cr;
    int room;
    if (!e || !e->lines)
        return 0;
    GetClientRect(wnd, &cr);
    room = (cr.right - cr.left) - rich_bar(wnd) - rich_selbar(wnd);
    return rich_text_width(wnd, e) > room;
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

/* Which display line a y falls on. One function because two things ask: the
 * hit test below, and a drag from the selection bar, which takes whole lines
 * and so wants the row rather than the character. */
static int rich_row_at_y(HWND wnd, ween_rich *e, int y)
{
    RECT fmt;
    int row, top;
    if (!e || !e->lines)
        return 0;
    rich_fmt(wnd, &fmt);
    top = e->line[e->first_visible].top;
    for (row = e->first_visible; row < e->lines; row++) {
        int at = fmt.top + e->line[row].top - top;
        if (y < at + e->line[row].height || row == e->lines - 1)
            break;
    }
    if (row < e->first_visible)
        row = e->first_visible;
    if (row >= e->lines)
        row = e->lines - 1;
    return row;
}

static int rich_index_at_point(HWND wnd, ween_rich *e, int x, int y)
{
    int row;
    if (!e || !e->lines)
        return 0;
    row = rich_row_at_y(wnd, e, y);
    if (row >= e->lines)
        row = e->lines - 1;
    /* `x` is the client x as it arrived. It used to have the border taken
     * off by each caller and added back here -- a compensating pair that
     * cancelled exactly, and both halves are gone. */
    return rich_index_at_x(wnd, e, row, x - rich_line_left(wnd, e, row));
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

/* The view follows the caret sideways, which is the half of jd's report that
 * adding a scrollbar does not fix.
 *
 * Measured: in No wrap, typing one character past the right edge moves
 * character 0 from x 9 to x -4723 -- the machine scrolls to bring the caret
 * back into the window (hscroll.txt). Ours did not move at all, so the caret
 * simply left sideways, which is the same shape as leaving the bottom and an
 * unrelated cause.
 *
 * **What is measured is that it follows and roughly where it lands; the exact
 * resting offset is not.** The machine put the caret about a dozen pixels
 * short of the right edge, and whether that is a margin, a rounding or the
 * next character's width has not been asked. This scrolls the least that
 * makes the caret visible, which agrees with the measurement to within that
 * dozen and is the behaviour a reader would expect. When somebody measures
 * the offset, it goes here. */
static int rich_hscroll_into_view(HWND wnd, ween_rich *e)
{
    int row, x, left, right, room, was = e->scroll_x;
    RECT cr;
    if (!e->lines || !e->hbar_allowed)
        return 0;
    GetClientRect(wnd, &cr);
    room = (cr.right - cr.left) - rich_bar(wnd) - rich_selbar(wnd);
    if (room <= 0)
        return 0;
    row = rich_line_of(e, e->caret);
    /* Where the caret stands in the text, before the view is taken off. */
    x = rich_line_left_unscrolled(wnd, e, row) - rich_selbar(wnd) +
        rich_x_of(e, row, e->caret - e->line[row].start);
    left = e->scroll_x;
    right = left + room;
    if (x < left)
        e->scroll_x = x;
    else if (x > right)
        e->scroll_x = x - room;
    if (e->scroll_x < 0)
        e->scroll_x = 0;
    return e->scroll_x != was;
}

static void rich_show_caret(HWND wnd, ween_rich *e)
{
    e->caret_on = 1; /* a caret that has just moved is not blinking off */
    rich_scroll_into_view(wnd, e);
    rich_hscroll_into_view(wnd, e);
}

/* ---- drawing ------------------------------------------------------------- */

static void rich_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    struct ween_wnd *top = ween_top_level(wnd);
    ween_rich *e = rich_state(wnd);
    RECT r = ps->rcPaint, cr;
    int ox, oy, sb = rich_bar(wnd), hb = rich_hbar(wnd);
    int from = 0, to = 0, row;
    ween_color ink = (wnd->style & WS_DISABLED) ? WEEN_SHADOW : WEEN_BLACK;

    ween_client_origin(wnd, &ox, &oy);
    rich_fmt(wnd, &cr);
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
        int y = cr.top + e->line[row].top - e->line[e->first_visible].top;
        int left = rich_line_left(wnd, e, row);
        int x = left;
        int at = e->line[row].start, end = at + e->line[row].len;
        int i = run_at(e, at);
        if ((wnd->style & ES_MULTILINE) && y + e->line[row].height > cr.bottom)
            break;
        /* **The bullet itself: a five-by-five dot with its corners off.**
         * Measured at 96 dpi (tools/vm/bullet-machine.png), on WordPad where
         * plain text starts at screen x 175:
         *
         *     the glyph   x 175..179, y+6 .. y+10 of a sixteen-pixel line
         *
         * So it sits at the paragraph's *un-indented* left edge -- the text
         * moves right past it and the dot stays where the text would have
         * been. `left` already carries the eleven, so the dot is drawn back
         * at `left - the indent`.
         *
         *     .###.
         *     #####
         *     #####
         *     #####
         *     .###.
         */
        {
            int ind = rich_bullet_indent(e, row);
            if (ind > 0) {
                static const unsigned char dot[5] = { 0x0E, 0x1F, 0x1F,
                                                      0x1F, 0x0E };
                int bx = left - ind, by = y + 6, r, c;
                for (r = 0; r < 5; r++)
                    for (c = 0; c < 5; c++)
                        if (dot[r] & (1 << (4 - c)))
                            ween_surface_fill(&top->surface, ox + bx + c,
                                              oy + by + r, 1, 1, WEEN_BLACK);
            }
        }
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
            int seg = rend, selected, w, by, k, tab;
            ween_color col;
            if (at < from && from < seg)
                seg = from;
            else if (at >= from && at < to && to < seg)
                seg = to;
            /* A tab is drawn as the room it makes and nothing else, so it
             * is a piece of its own: either this piece is one tab, or it
             * stops at the next one. Where the room ends is rich_next_tab_stop's
             * to say, measured from the line's own left edge -- a stop is
             * the paragraph's, not the client's. */
            tab = e->text[at] == '\t';
            if (tab) {
                seg = at + 1;
            } else {
                for (k = at; k < seg; k++)
                    if (e->text[k] == '\t') {
                        seg = k;
                        break;
                    }
            }
            selected = at >= from && at < to;
            w = tab ? left + rich_next_tab_stop(e, at, x - left) - x
                    : sf ? ween_strike_pen(sf, e->text + at, seg - at)
                         : seg - at;
            by = y + e->line[row].ascent - (sf ? sf->ascent : 0);
            col = selected ? WEEN_WHITE
                  : (fmt->effects & CFE_AUTOCOLOR)
                      ? ink
                      : ween_cr_to_px(fmt->color);
            /* Selected, the tab's room is filled like any other -- which
             * is what every text control does and what this one has always
             * done to the space beside it, though no capture here has
             * measured a selected tab. */
            if (selected)
                ween_surface_fill(&top->surface, ox + x, oy + y, w,
                                  e->line[row].height, WEEN_CAP_LEFT);
            if (sf && !tab)
                ween_strike_draw_styled(sf, &top->surface, ox + x, oy + by,
                                        e->text + at, seg - at, col,
                                        (fmt->effects & CFE_ITALIC) != 0,
                                        (fmt->effects & CFE_UNDERLINE) != 0);
            /* A line through the middle, which no strike carries and GDI
             * draws for itself. Where exactly it sits is not measured; half
             * the cell is where it lands here. */
            if ((fmt->effects & CFE_STRIKEOUT) && sf && !tab)
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
        int cy = cr.top + e->line[crow].top - e->line[e->first_visible].top;
        int cx = rich_line_left(wnd, e, crow) +
                 rich_x_of(e, crow, e->caret - e->line[crow].start);
        if (cy >= cr.top && cy + e->line[crow].height <= cr.bottom)
            ween_surface_vline(&top->surface, ox + cx, oy + cy,
                               e->line[crow].height, WEEN_BLACK);
    }

    /* The bars last, so a line long enough to reach one is covered by it.
     * They stop short of each other rather than meeting in the corner: the
     * vertical one ends where the horizontal one starts, which is the room
     * each has already taken off the other's client. */
    if (sb) {
        ween_sbstate st = rich_sbstate(wnd);
        ween_draw_scrollbar(&top->surface, ox + cr.right - sb, oy, sb,
                            cr.bottom - cr.top - hb, 1,
                            ween_sb_maxpos(&st) > st.min, st.pos, st.page,
                            st.min, st.max);
    }
    if (hb) {
        ween_sbstate st = rich_hsbstate(wnd);
        ween_draw_scrollbar(&top->surface, ox + cr.left, oy + cr.bottom - hb,
                            cr.right - cr.left - sb, hb, 0,
                            ween_sb_maxpos(&st) > st.min, st.pos, st.page,
                            st.min, st.max);
    }
}

/* ---- RTF, in and out ------------------------------------------------------
 *
 * A document's formats are the control's business and not the
 * application's: WordPad hands a file to EM_STREAMIN and asks for one back
 * with EM_STREAMOUT, and everything between the braces is here.
 *
 * What is written is the shape riched20 writes, read off the machine with
 * tools/vm/ctlprobe.c -- header, font table, colour table, then the
 * paragraphs:
 *
 *   {\rtf1\ansi\ansicpg1252\deff0\deflang1033{\fonttbl{\f0\fnil\fcharset0 Tahoma;}}
 *   {\colortbl ;\red255\green0\blue0;}
 *   \viewkind4\uc1\pard\fi360\li360\ri360\qc\tx1440\tx2880\f0\fs17 plain and
 *   \cf1\ul\b\i\strike\f1\fs24 formatted\cf0\ulnone\b0\i0\strike0\f0\fs17\par
 *   }
 *
 * A size is in half-points -- \fs17 for the 165 twips a fresh control is
 * lettered in -- an indent is in twips, and a run states only what changed
 * since the one before it, everything being put back at the paragraph's end.
 */

typedef struct {
    char *buf;
    int len, cap;
    int failed;
} rtf_out;

static void rtf_put(rtf_out *o, const char *text, int n)
{
    if (o->failed)
        return;
    if (o->len + n + 1 > o->cap) {
        int cap = o->cap ? o->cap : 256;
        char *grown;
        while (cap < o->len + n + 1)
            cap *= 2;
        grown = realloc(o->buf, (size_t)cap);
        if (!grown) {
            o->failed = 1;
            return;
        }
        o->buf = grown;
        o->cap = cap;
    }
    memcpy(o->buf + o->len, text, (size_t)n);
    o->len += n;
    o->buf[o->len] = 0;
}

static void rtf_puts(rtf_out *o, const char *text)
{
    rtf_put(o, text, (int)strlen(text));
}

static void rtf_word(rtf_out *o, const char *word, long n)
{
    char num[32];
    int i = 0, neg = n < 0;
    unsigned long v = neg ? (unsigned long)-n : (unsigned long)n;
    rtf_puts(o, word);
    if (neg)
        rtf_puts(o, "-");
    do {
        num[i++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && i < 30);
    while (i--)
        rtf_put(o, &num[i], 1);
}

/* The tables: every face and every colour the document uses, once each. */
static int rtf_face_index(char faces[][LF_FACESIZE], int *n, const char *face)
{
    int i;
    for (i = 0; i < *n; i++)
        if (strcmp(faces[i], face) == 0)
            return i;
    if (*n < 32) {
        face_copy(faces[*n], face);
        return (*n)++;
    }
    return 0;
}

static int rtf_color_index(COLORREF *cols, int *n, COLORREF c)
{
    int i;
    for (i = 0; i < *n; i++)
        if (cols[i] == c)
            return i + 1; /* index 0 is the automatic colour */
    if (*n < 32) {
        cols[*n] = c;
        return ++(*n);
    }
    return 0;
}

static void rtf_text(rtf_out *o, const char *text, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\\' || c == '{' || c == '}') {
            rtf_puts(o, "\\");
            rtf_put(o, (const char *)&c, 1);
        } else if (c == '\t') {
            rtf_puts(o, "\\tab ");
        } else if (c >= 0x80) {
            static const char hex[] = "0123456789abcdef";
            char esc[5];
            esc[0] = '\\';
            esc[1] = '\'';
            esc[2] = hex[c >> 4];
            esc[3] = hex[c & 15];
            esc[4] = 0;
            rtf_puts(o, esc);
        } else {
            rtf_put(o, (const char *)&c, 1);
        }
    }
}

static void rtf_write(HWND wnd, ween_rich *e, rtf_out *o, int from, int to)
{
    static char faces[32][LF_FACESIZE];
    COLORREF cols[32];
    int nfaces = 0, ncols = 0, i, para, at;
    ween_rfmt cur;
    int have_cur = 0;
    (void)wnd;

    /* the tables first, since they are written before the text */
    for (i = 0; i < e->runs; i++) {
        rtf_face_index(faces, &nfaces, e->run[i].fmt.face);
        if (!(e->run[i].fmt.effects & CFE_AUTOCOLOR))
            rtf_color_index(cols, &ncols, e->run[i].fmt.color);
    }
    if (!nfaces)
        rtf_face_index(faces, &nfaces, "MS Shell Dlg");

    rtf_puts(o, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\deflang1033{\\fonttbl");
    for (i = 0; i < nfaces; i++) {
        rtf_word(o, "{\\f", i);
        rtf_puts(o, "\\fnil\\fcharset0 ");
        rtf_puts(o, faces[i]);
        rtf_puts(o, ";}");
    }
    rtf_puts(o, "}\r\n{\\colortbl ;");
    for (i = 0; i < ncols; i++) {
        rtf_word(o, "\\red", GetRValue(cols[i]));
        rtf_word(o, "\\green", GetGValue(cols[i]));
        rtf_word(o, "\\blue", GetBValue(cols[i]));
        rtf_puts(o, ";");
    }
    rtf_puts(o, "}\r\n\\viewkind4\\uc1");

    for (para = 0; para < e->paras; para++) {
        const ween_pfmt *pf = &e->para[para].fmt;
        int pstart = e->para[para].start;
        int pend = para + 1 < e->paras ? e->para[para + 1].start - 1 : e->len;
        if (pend < from || pstart > to)
            continue;
        if (pstart < from)
            pstart = from;
        if (pend > to)
            pend = to;
        rtf_puts(o, "\\pard");
        if (pf->offset)
            rtf_word(o, "\\fi", -pf->offset);
        if (pf->start_indent + pf->offset)
            rtf_word(o, "\\li", pf->start_indent + pf->offset);
        if (pf->right_indent)
            rtf_word(o, "\\ri", pf->right_indent);
        if (pf->alignment == PFA_CENTER)
            rtf_puts(o, "\\qc");
        else if (pf->alignment == PFA_RIGHT)
            rtf_puts(o, "\\qr");
        for (i = 0; i < pf->tabs; i++)
            rtf_word(o, "\\tx", pf->tab[i]);
        have_cur = 0;
        at = pstart;
        while (at <= pend) {
            int r = run_at(e, at);
            const ween_rfmt *f = &e->run[r].fmt;
            int next = (r + 1 < e->runs && e->run[r + 1].start <= pend)
                           ? e->run[r + 1].start
                           : pend;
            /* what changed since the run before, and everything on the
             * first run of a paragraph */
            if (!have_cur || (cur.effects & CFE_AUTOCOLOR) !=
                                 (f->effects & CFE_AUTOCOLOR) ||
                cur.color != f->color) {
                if (f->effects & CFE_AUTOCOLOR)
                    rtf_puts(o, "\\cf0");
                else
                    rtf_word(o, "\\cf",
                             rtf_color_index(cols, &ncols, f->color));
            }
            if (!have_cur || (cur.effects & CFE_UNDERLINE) !=
                                 (f->effects & CFE_UNDERLINE))
                rtf_puts(o, (f->effects & CFE_UNDERLINE) ? "\\ul"
                                                         : "\\ulnone");
            if (!have_cur ||
                (cur.effects & CFE_BOLD) != (f->effects & CFE_BOLD))
                rtf_puts(o, (f->effects & CFE_BOLD) ? "\\b" : "\\b0");
            if (!have_cur ||
                (cur.effects & CFE_ITALIC) != (f->effects & CFE_ITALIC))
                rtf_puts(o, (f->effects & CFE_ITALIC) ? "\\i" : "\\i0");
            if (!have_cur ||
                (cur.effects & CFE_STRIKEOUT) != (f->effects & CFE_STRIKEOUT))
                rtf_puts(o, (f->effects & CFE_STRIKEOUT) ? "\\strike"
                                                         : "\\strike0");
            if (!have_cur || strcmp(cur.face, f->face) != 0)
                rtf_word(o, "\\f", rtf_face_index(faces, &nfaces, f->face));
            if (!have_cur || cur.height != f->height)
                rtf_word(o, "\\fs", (f->height + 5) / 10); /* half-points */
            rtf_puts(o, " ");
            cur = *f;
            have_cur = 1;
            if (next > at)
                rtf_text(o, e->text + at, next - at);
            if (next == pend)
                break;
            at = next;
        }
        rtf_puts(o, "\\par\r\n");
    }
    rtf_puts(o, "}\r\n");
}

/* ---- reading one ---------------------------------------------------------
 *
 * Enough of RTF to read back what is written above, and what WordPad will
 * put in a file: the two tables, the paragraph words, the character words,
 * and the escapes. A group it does not know -- {\*\generator ...} and its
 * kind -- is skipped whole, which is what the specification says to do with
 * anything after \*. */

/* No colour at all, which is what the first entry of a colour table is. */
#define WEEN_RTF_AUTOCOLOR 0xFFFFFFFFu

typedef struct {
    char faces[32][LF_FACESIZE];
    int nfaces;
    COLORREF cols[32];
    int ncols;
} rtf_tables;

static int rtf_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* One control word, its number if it has one, and where the text goes on. */
static const char *rtf_control(const char *p, const char *end, char *word,
                               int *has_num, long *num)
{
    int n = 0;
    *has_num = 0;
    *num = 0;
    while (p < end && rtf_is_alpha(*p) && n < 31)
        word[n++] = *p++;
    word[n] = 0;
    if (p < end && (*p == '-' || (*p >= '0' && *p <= '9'))) {
        int neg = *p == '-';
        long v = 0;
        if (neg)
            p++;
        while (p < end && *p >= '0' && *p <= '9')
            v = v * 10 + (*p++ - '0');
        *has_num = 1;
        *num = neg ? -v : v;
    }
    if (p < end && *p == ' ')
        p++; /* one space after a word is the word's own, not text */
    return p;
}

/* The two tables, read out of the header. */
static const char *rtf_read_fonttbl(const char *p, const char *end,
                                    rtf_tables *t)
{
    int depth = 1;
    while (p < end && depth) {
        if (*p == '{') {
            int index = 0, n = 0;
            char face[LF_FACESIZE];
            p++;
            face[0] = 0;
            while (p < end && *p != '}' && *p != ';') {
                if (*p == '\\') {
                    char word[32];
                    int has;
                    long num;
                    p = rtf_control(p + 1, end, word, &has, &num);
                    if (!strcmp(word, "f") && has)
                        index = (int)num;
                } else {
                    if (n < LF_FACESIZE - 1)
                        face[n++] = *p;
                    p++;
                }
            }
            face[n] = 0;
            while (n > 0 && face[n - 1] == ' ')
                face[--n] = 0;
            if (index >= 0 && index < 32) {
                face_copy(t->faces[index], face);
                if (index >= t->nfaces)
                    t->nfaces = index + 1;
            }
            while (p < end && *p != '}')
                p++;
            if (p < end)
                p++;
        } else if (*p == '}') {
            depth--;
            p++;
        } else {
            p++;
        }
    }
    return p;
}

static const char *rtf_read_colortbl(const char *p, const char *end,
                                     rtf_tables *t)
{
    int r = 0, g = 0, b = 0, any = 0;
    while (p < end && *p != '}') {
        if (*p == '\\') {
            char word[32];
            int has;
            long num;
            p = rtf_control(p + 1, end, word, &has, &num);
            if (!strcmp(word, "red"))
                r = (int)num, any = 1;
            else if (!strcmp(word, "green"))
                g = (int)num, any = 1;
            else if (!strcmp(word, "blue"))
                b = (int)num, any = 1;
        } else if (*p == ';') {
            /* Every entry in the order it comes, the empty one included:
             * \cf0 is the first of them and it is the automatic colour, so
             * the numbering only works if it is kept. */
            if (t->ncols < 32)
                t->cols[t->ncols++] = any ? RGB(r, g, b) : WEEN_RTF_AUTOCOLOR;
            r = g = b = 0;
            any = 0;
            p++;
        } else {
            p++;
        }
    }
    if (p < end)
        p++;
    return p;
}

/* Read a document in, replacing everything. */
static int rtf_read(HWND wnd, ween_rich *e, const char *text, int len)
{
    static rtf_tables t;
    const char *p = text, *end = text + len;
    ween_rfmt cf;
    ween_pfmt pf;
    int skip_depth = 0, depth = 0, pending_mark = 0, deff = -1;
    char *out = malloc((size_t)len + 1);
    int n = 0;

    if (!out)
        return 0;
    memset(&t, 0, sizeof t);
    rfmt_default(wnd, &cf);
    pfmt_default(&pf);

    /* The text is built first and the formats recorded against it, then the
     * whole lot is put in at once -- a run and a paragraph array cannot be
     * grown character by character without quadratic work. */
    rich_set_text(wnd, e, "");
    e->runs = 0;
    e->paras = 0;
    while (p < end) {
        if (*p == '{') {
            depth++;
            p++;
            if (p + 1 < end && *p == '\\' && p[1] == '*')
                skip_depth = depth; /* a group nobody has to understand */
            continue;
        }
        if (*p == '}') {
            if (skip_depth && depth == skip_depth)
                skip_depth = 0;
            depth--;
            p++;
            continue;
        }
        if (skip_depth) {
            p++;
            continue;
        }
        if (*p == '\\') {
            char word[32];
            int has;
            long num;
            p++;
            if (p < end && (*p == '\\' || *p == '{' || *p == '}')) {
                if (n <= len)
                    out[n++] = *p;
                p++;
                continue;
            }
            if (p < end && *p == '\'') {
                int v = 0, i;
                p++;
                for (i = 0; i < 2 && p < end; i++, p++) {
                    char c = *p;
                    v = v * 16 + (c >= '0' && c <= '9'   ? c - '0'
                                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                         : 0);
                }
                if (n <= len)
                    out[n++] = (char)v;
                continue;
            }
            p = rtf_control(p, end, word, &has, &num);
            if (!strcmp(word, "deff") && has)
                deff = (int)num; /* which of the table's faces is the default */
            else if (!strcmp(word, "fonttbl")) {
                p = rtf_read_fonttbl(p, end, &t);
                /* The default face is named before the table that has it,
                 * so it is applied once the table is read -- which is how
                 * riched20 letters a document whose text names no font: the
                 * machine reads "{\deff0{\fonttbl{\f0 Arial;}}...}" as
                 * Arial throughout. */
                if (deff >= 0 && deff < t.nfaces && t.faces[deff][0])
                    face_copy(cf.face, t.faces[deff]);
            }
            else if (!strcmp(word, "colortbl"))
                p = rtf_read_colortbl(p, end, &t);
            else if (!strcmp(word, "par") || !strcmp(word, "line")) {
                /* The paragraph that ends here, with what it carried. The
                 * mark itself waits: riched20's own documents end with a
                 * \par before the closing brace and the text it reads back
                 * has no empty paragraph after it, so a mark is only written
                 * when something follows it. */
                if (paras_reserve(e, e->paras + 1)) {
                    e->para[e->paras].start = 0;
                    e->para[e->paras].fmt = pf;
                    e->paras++;
                }
                if (pending_mark && n <= len)
                    out[n++] = '\r';
                pending_mark = 1;
            } else if (!strcmp(word, "tab")) {
                if (n <= len)
                    out[n++] = '\t';
            } else if (!strcmp(word, "pard")) {
                pfmt_default(&pf);
            } else if (!strcmp(word, "ql"))
                pf.alignment = PFA_LEFT;
            else if (!strcmp(word, "qc"))
                pf.alignment = PFA_CENTER;
            else if (!strcmp(word, "qr"))
                pf.alignment = PFA_RIGHT;
            else if (!strcmp(word, "li") && has)
                pf.start_indent = num;
            else if (!strcmp(word, "fi") && has) {
                /* \li is the paragraph's left and \fi the first line's, from
                 * it; a PARAFORMAT states the first line's and the offset of
                 * the rest. */
                pf.start_indent += num;
                pf.offset = -num;
            } else if (!strcmp(word, "ri") && has)
                pf.right_indent = num;
            else if (!strcmp(word, "tx") && has) {
                if (pf.tabs < MAX_TAB_STOPS)
                    pf.tab[pf.tabs++] = num;
            } else if (!strcmp(word, "b"))
                cf.effects = (cf.effects & ~(DWORD)CFE_BOLD) |
                             (has && !num ? 0 : CFE_BOLD);
            else if (!strcmp(word, "i"))
                cf.effects = (cf.effects & ~(DWORD)CFE_ITALIC) |
                             (has && !num ? 0 : CFE_ITALIC);
            else if (!strcmp(word, "strike"))
                cf.effects = (cf.effects & ~(DWORD)CFE_STRIKEOUT) |
                             (has && !num ? 0 : CFE_STRIKEOUT);
            else if (!strcmp(word, "ul"))
                cf.effects |= CFE_UNDERLINE;
            else if (!strcmp(word, "ulnone"))
                cf.effects &= ~(DWORD)CFE_UNDERLINE;
            else if (!strcmp(word, "fs") && has)
                cf.height = num * 10; /* half-points to twips */
            else if (!strcmp(word, "f") && has) {
                if (num >= 0 && num < t.nfaces && t.faces[num][0])
                    face_copy(cf.face, t.faces[num]);
            } else if (!strcmp(word, "cf") && has) {
                if (num < 0 || num >= t.ncols ||
                    t.cols[num] == WEEN_RTF_AUTOCOLOR) {
                    cf.effects |= CFE_AUTOCOLOR;
                } else {
                    cf.effects &= ~(DWORD)CFE_AUTOCOLOR;
                    cf.color = t.cols[num];
                }
            }
            continue;
        }
        if (*p == '\r' || *p == '\n') {
            p++; /* the line breaks in the file are not the document's */
            continue;
        }
        /* an ordinary character, in the formatting in force */
        if (pending_mark) {
            if (n <= len)
                out[n++] = '\r';
            pending_mark = 0;
        }
        if (n <= len)
            out[n++] = *p;
        if (!e->runs || !rfmt_same(&e->run[e->runs - 1].fmt, &cf)) {
            if (runs_reserve(e, e->runs + 1)) {
                e->run[e->runs].start = n - 1;
                e->run[e->runs].fmt = cf;
                e->runs++;
            }
        }
        p++;
    }
    out[n] = 0;

    if (rich_reserve(e, n)) {
        memcpy(e->text, out, (size_t)n + 1);
        e->len = n;
    }
    free(out);
    if (!e->runs) {
        runs_reset(wnd, e);
    } else {
        e->run[0].start = 0;
        runs_coalesce(e);
    }
    /* the paragraphs, in the order their marks came, and one for the tail */
    {
        int i, at = 0, k = 0;
        struct rich_para *got = e->para;
        int ngot = e->paras;
        e->para = NULL;
        e->paras = e->para_cap = 0;
        paras_reset(e);
        for (i = 0; i < e->paras && k < ngot; i++, k++)
            e->para[i].fmt = got[k].fmt;
        if (e->paras && ngot && e->paras > ngot)
            e->para[e->paras - 1].fmt = got[ngot - 1].fmt;
        free(got);
        (void)at;
    }
    e->caret = e->anchor = 0;
    e->first_visible = 0;
    rich_relines(wnd, e);
    return 1;
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
    case EM_FINDTEXT: {
        FINDTEXTA *ft = (FINDTEXTA *)lp;
        if (!ft)
            return -1;
        return rich_find(e, (DWORD)wp, &ft->chrg, ft->lpstrText, NULL);
    }
    case EM_FINDTEXTEX: {
        FINDTEXTEXA *ft = (FINDTEXTEXA *)lp;
        if (!ft)
            return -1;
        return rich_find(e, (DWORD)wp, &ft->chrg, ft->lpstrText,
                         &ft->chrgText);
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
        return e->undos > 0 ? TRUE : FALSE;
    case EM_EMPTYUNDOBUFFER:
        rich_undo_clear(e);
        return 0;
    case EM_UNDO: {
        struct rich_step st;
        char *was;
        int caret;
        if (e->undos <= 0)
            return FALSE;
        st = e->undo[--e->undos];
        was = st.text;
        caret = st.caret;
        /* **Undoing is no longer itself undoable**, which is what made the
         * second undo redo the first. Putting the current text back on the
         * stack here is a redo, and riched20 has a separate EM_REDO for
         * that -- one message doing both is the toggle jd reported. */
        if (!rich_set_text(wnd, e, was)) {
            rich_step_free(&st);
            return FALSE;
        }
        /* **And the runs come back with it**, which is jd's second report:
         * `rich_set_text` puts every run back to the control's own face, so
         * restoring the characters alone loses the formatting they were in.
         * On the machine the two unwind separately and most-recent-first --
         * bold 0..3, type XY, undo, undo gives back the typing and then the
         * style -- and a step here holds both because it holds the
         * document. */
        if (st.runs > 0 && runs_reserve(e, st.runs)) {
            memcpy(e->run, st.run, (size_t)st.runs * sizeof *e->run);
            e->runs = st.runs;
            rich_relines(wnd, e);
        }
        rich_step_free(&st);
        e->caret = e->anchor = caret > e->len ? e->len : caret;
        rich_changed(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    }
    case EM_CANPASTE:
        /* Whether there is anything to paste, which is what win32 answers:
         * with an empty clipboard it is FALSE, and a program that greys its
         * Paste button by asking gets a grey one. This said TRUE always, so
         * WordPad's Paste lit up on an empty clipboard the moment anything
         * started asking. wp is a format to test, or 0 for "any this control
         * takes", which here is text. */
        return IsClipboardFormatAvailable(wp ? (UINT)wp : CF_TEXT);
    case EM_STREAMOUT: {
        /* The document handed out in pieces, the way win32 hands one out:
         * the program's callback is called until there is nothing left. */
        EDITSTREAM *es = (EDITSTREAM *)lp;
        rtf_out o;
        int from = 0, to = e->len, at = 0;
        if (!es || !es->pfnCallback)
            return 0;
        memset(&o, 0, sizeof o);
        if (wp & SFF_SELECTION)
            rich_range(e, &from, &to);
        if ((wp & 0x0f) == SF_RTF) {
            rtf_write(wnd, e, &o, from, to);
        } else {
            /* SF_TEXT: the marks go out as a program expects them. */
            int i;
            for (i = from; i < to; i++) {
                if (e->text[i] == '\r')
                    rtf_puts(&o, "\r\n");
                else
                    rtf_put(&o, e->text + i, 1);
            }
        }
        if (o.failed) {
            free(o.buf);
            es->dwError = 1;
            return 0;
        }
        es->dwError = 0;
        while (at < o.len) {
            LONG done = 0;
            LONG want = o.len - at > 4096 ? 4096 : o.len - at;
            DWORD r = es->pfnCallback(es->dwCookie, (LPBYTE)o.buf + at, want,
                                      &done);
            if (r || done <= 0) {
                es->dwError = r;
                break;
            }
            at += done;
        }
        free(o.buf);
        return at;
    }
    case EM_STREAMIN: {
        EDITSTREAM *es = (EDITSTREAM *)lp;
        char *got = NULL;
        int len = 0, cap = 0;
        if (!es || !es->pfnCallback)
            return 0;
        for (;;) {
            LONG done = 0;
            DWORD r;
            if (len + 4096 + 1 > cap) {
                int want = cap ? cap * 2 : 8192;
                char *grown = realloc(got, (size_t)want);
                if (!grown)
                    break;
                got = grown;
                cap = want;
            }
            r = es->pfnCallback(es->dwCookie, (LPBYTE)got + len, 4096, &done);
            if (r) {
                es->dwError = r;
                break;
            }
            if (done <= 0)
                break;
            len += done;
        }
        if (got)
            got[len] = 0;
        es->dwError = 0;
        if ((wp & 0x0f) == SF_RTF) {
            if (got)
                rtf_read(wnd, e, got, len);
        } else {
            if (got)
                rich_set_text(wnd, e, got);
        }
        free(got);
        e->modified = 0;
        rich_changed(wnd, e);
        InvalidateRect(wnd, NULL, TRUE);
        return len;
    }
    case EM_SETTARGETDEVICE:
        /* With a device this says what to break the text to; with none, a
         * width at all means do not break it, which is what WordPad's No
         * Wrap sends and what the machine does with it. */
        e->nowrap = lp != 0;
        rich_relines(wnd, e);
        InvalidateRect(wnd, NULL, FALSE);
        return TRUE;
    case EM_SETRECT:
    case EM_SETRECTNP: {
        /* The box the text is laid out in. A NULL rectangle puts the control
         * back on its client rectangle less the border, which is win32's own
         * meaning for it and what makes the message reversible.
         *
         * EM_SETRECTNP is the same thing without the repaint -- NP for "no
         * paint" -- and the only difference here is the InvalidateRect. */
        const RECT *r = (const RECT *)lp;
        if (!r) {
            e->fmt_set = 0;
        } else {
            e->fmt = *r;
            e->fmt_set = 1;
        }
        rich_relines(wnd, e);
        if (msg == EM_SETRECT)
            InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case EM_GETRECT: {
        /* **What was set, not what is laid out in.** `rich_fmt` inflates an
         * explicit rectangle by a pixel on each side because that is what
         * riched20 lays out in; a program asking for its rectangle back must
         * get the one it gave. wordpad reads this and adds a delta to it
         * (`alignEditorToRuler`), so handing back the inflated one would grow
         * the rectangle by two pixels on every call -- the accumulation Dan
         * traced item 3 to, in a second field. */
        RECT *r = (RECT *)lp;
        ween_rich *e = rich_state(wnd);
        if (!r)
            return 0;
        if (e && e->fmt_set)
            *r = e->fmt;
        else
            rich_fmt(wnd, r);
        return 0;
    }
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
        {
            RECT fmt;
            rich_fmt(wnd, &fmt);
            pt->y = fmt.top + e->line[row].top -
                    e->line[e->first_visible].top;
        }
        return 0;
    }

    /* ---- the formatting a run carries ---- */
    case EM_SETCHARFORMAT: {
        const CHARFORMATA *cf = (const CHARFORMATA *)lp;
        int from, to;
        if (!cf)
            return FALSE;
        /* **`SCF_DEFAULT` is `0x0000`**, so it is the *absence* of the other
         * two rather than a bit to test, and that is the whole reason this
         * was missing: a default call looked like "not SCF_ALL" and fell into
         * the selection branch below.
         *
         * What is set here outlives the text. It does not touch the runs that
         * exist -- riched20's default is the format text *arrives* in, and
         * `runs_reset` reads it on every `WM_SETTEXT` -- **except when the
         * control is empty**, where the machine reports the new default back
         * through `EM_GETCHARFORMAT(SCF_SELECTION)` straight away:
         *
         *   before anything, selection   face System
         *   after the set, selection     face Arial
         *
         * so the one run an empty control has is re-based to it. That case is
         * the one WordPad is in when it sets Arial 10 before a character
         * exists. **Setting a default on a control that already has text is
         * not measured** and is left alone here rather than guessed at. */
        if (!(wp & (SCF_SELECTION | SCF_ALL))) {
            if (!e->def_set) {
                rfmt_default(wnd, &e->def);
                e->def_set = 1;
            }
            rfmt_apply(&e->def, cf);
            if (e->len == 0)
                runs_reset(wnd, e);
            /* The caret on an empty line takes its height from what would be
             * typed on it, which is what was just set. */
            rich_relines(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
            return TRUE;
        }
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
            /* **And it closes an open typed run, though it records
             * nothing.** Arming pushes no undo step -- measured, Sam's
             * `undoprobe.txt`: bold with nothing selected, then type, and one
             * undo takes back the typing and leaves the text. But leaving the
             * run *open* means the character typed next joins the step before
             * the arming, so that one undo took back everything typed since:
             *
             *     machine   type abc, arm bold, type XY, undo  -> "abc"
             *     ours      the same                           -> ""
             *
             * **This is the second boundary the coalescing was missing**, the
             * first being WM_SETTEXT. A rule that groups has to say what
             * ungroups, and the answer keeps being "anything the user did on
             * purpose", whether or not it left a record. */
            if (e->undos > 0)
                e->undo[e->undos - 1].typing = 0;
            /* **And the line the caret is on may have just changed height.**
             * The text does not change, so this branch used to return here --
             * but an empty line takes its height from what would be typed on
             * it, which is what was just set. A new document is the whole of
             * that case: WordPad sets Arial 10 before a character exists, and
             * without this the caret stayed the control's own 13 against the
             * machine's 16. Re-lining an unchanged text is the same walk the
             * set would have done had anything been selected. */
            rich_relines(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
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
    case WM_SIZE: {
        /* A narrower window breaks the text in more places. */
        LRESULT r = DefWindowProcA(wnd, msg, wp, lp);
        rich_relines(wnd, e);
        return r;
    }

    /* ---- the mouse ---- */
    case WM_LBUTTONDOWN: {
        int had = ween_focus_get() == wnd;
        if (wnd->style & WS_DISABLED)
            return 0;
        /* **The horizontal bar is tested first**, because the vertical
         * block below asks only about x and would take a click in the
         * bottom-right corner -- which belongs to neither bar and to the
         * vertical one least of all, since the vertical bar stops short of
         * it. */
        if ((wnd->style & WS_HSCROLL) && rich_hbar(wnd)) {
            RECT cr;
            GetClientRect(wnd, &cr);
            if (GET_Y_LPARAM(lp) >= cr.bottom - rich_hbar(wnd)) {
                ween_sbstate st = rich_hsbstate(wnd);
                int grab, pos;
                SetFocus(wnd);
                pos = ween_sb_click(GET_X_LPARAM(lp),
                                    cr.right - cr.left - rich_bar(wnd), &st,
                                    &grab);
                if (grab >= 0) {
                    SetCapture(wnd);
                    e->hsb_grab = grab;
                }
                e->scroll_x = ween_sb_clamp(pos, &st);
                rich_notify(wnd, EN_HSCROLL, ENM_SCROLL);
                InvalidateRect(wnd, NULL, FALSE);
                return 0;
            }
        }
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
        /* A press in the selection bar takes the display line beside it --
         * one wrapped row, not the paragraph -- and a drag from there takes
         * them line by line. §5, measured on the machine. */
        if (rich_in_selbar(wnd, GET_X_LPARAM(lp))) {
            int row = rich_row_at_y(wnd, e, GET_Y_LPARAM(lp));
            rich_select_row(e, row);
            e->bar_anchor_row = row;
            e->goal_set = 0;
            rich_selchange(wnd, e);
            rich_show_caret(wnd, e);
            SetCapture(wnd);
            if (!had)
                rich_notify(wnd, EN_SETFOCUS, ENM_CHANGE);
            InvalidateRect(wnd, NULL, FALSE);
            return 0;
        }
        /* The third press of a run: win32 sends no triple-click message, so
         * a press close to the last double click in time and place is one.
         * The fourth press arrives as a double click again -- the backend
         * pairs presses rather than running them together -- which is what
         * §5's "the count starts over" describes. */
        if (e->dbl_seen &&
            ween_now_ms() - e->dbl_ms <= (unsigned long)GetDoubleClickTime() &&
            GET_X_LPARAM(lp) - e->dbl_x <= 4 &&
            e->dbl_x - GET_X_LPARAM(lp) <= 4 &&
            GET_Y_LPARAM(lp) - e->dbl_y <= 4 &&
            e->dbl_y - GET_Y_LPARAM(lp) <= 4) {
            rich_select_para(e, rich_index_at_point(wnd, e, GET_X_LPARAM(lp),
                                                    GET_Y_LPARAM(lp)));
            e->dbl_seen = 0; /* the count starts over */
            e->goal_set = 0;
            rich_selchange(wnd, e);
            rich_show_caret(wnd, e);
            SetCapture(wnd);
            if (!had)
                rich_notify(wnd, EN_SETFOCUS, ENM_CHANGE);
            InvalidateRect(wnd, NULL, FALSE);
            return 0;
        }
        /* **A press inside the selection may be the start of a drag of the
         * text rather than a new selection** -- §5. Which it is cannot be
         * known until the pointer moves or the button comes up, so nothing
         * changes here: the selection stands, and WM_LBUTTONUP decides.
         *
         * After the triple-click test on purpose: the third press of a run
         * lands inside the word the double click selected, and it is a
         * triple click rather than the beginning of a drag.
         * A press in the selection bar is not this, since the bar is not in
         * the text. */
        {
            int selfrom, selto, at;
            rich_range(e, &selfrom, &selto);
            at = rich_index_at_point(wnd, e, GET_X_LPARAM(lp),
                                     GET_Y_LPARAM(lp));
            if (selfrom != selto && at > selfrom && at < selto &&
                !rich_in_selbar(wnd, GET_X_LPARAM(lp))) {
                e->dnd_pending = 1;
                e->dnd_active = 0;
                e->dnd_x = GET_X_LPARAM(lp);
                e->dnd_y = GET_Y_LPARAM(lp);
                SetCapture(wnd);
                if (!had)
                    rich_notify(wnd, EN_SETFOCUS, ENM_CHANGE);
                return 0;
            }
        }
        e->caret = rich_index_at_point(wnd, e, GET_X_LPARAM(lp),
                                       GET_Y_LPARAM(lp));
        e->anchor = e->caret; /* a fresh click starts a new selection */
        /* The word this press landed in, for the drag that may follow: while
         * a drag stays inside it the selection is character by character,
         * and the moment it leaves the selection snaps to whole words. */
        rich_word_bounds(e, e->caret, &e->drag_word_from, &e->drag_word_to);
        e->drag_snapped = 0;
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
        if (GetCapture() == wnd && e->hsb_grab >= 0) {
            RECT cr;
            ween_sbstate st = rich_hsbstate(wnd);
            int pos;
            GetClientRect(wnd, &cr);
            pos = ween_sb_clamp(ween_sb_drag(GET_X_LPARAM(lp),
                                             cr.right - cr.left -
                                                 rich_bar(wnd),
                                             &st, e->hsb_grab),
                                &st);
            if (pos != e->scroll_x) {
                e->scroll_x = pos;
                rich_notify(wnd, EN_HSCROLL, ENM_SCROLL);
                InvalidateRect(wnd, NULL, FALSE);
            }
        } else if (GetCapture() == wnd && e->sb_grab >= 0) {
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
        } else if (GetCapture() == wnd && e->dnd_pending) {
            /* The pointer has moved with the button down and the press was
             * inside the selection: it is a drag of the text. Nothing is
             * drawn for it yet -- the drop is what changes anything. */
            if (GET_X_LPARAM(lp) != e->dnd_x || GET_Y_LPARAM(lp) != e->dnd_y)
                e->dnd_active = 1;
        } else if (GetCapture() == wnd && e->bar_anchor_row >= 0) {
            /* A drag that began in the selection bar takes whole display
             * lines, forwards or backwards from the one it started on. */
            int row = rich_row_at_y(wnd, e, GET_Y_LPARAM(lp));
            int a = e->bar_anchor_row, b = row, from, to;
            if (b < a) {
                int t = a;
                a = b;
                b = t;
            }
            from = e->line[a].start;
            to = b + 1 < e->lines ? e->line[b + 1].start
                                  : e->line[b].start + e->line[b].len;
            if (row < e->bar_anchor_row) {
                e->anchor = to;
                e->caret = from;
            } else {
                e->anchor = from;
                e->caret = to;
            }
            rich_selchange(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
        } else if (GetCapture() == wnd) {
            int at = rich_index_at_point(wnd, e,
                                         GET_X_LPARAM(lp),
                                         GET_Y_LPARAM(lp));
            int anchor = e->anchor;
            /* **Auto word selection**, §5: a drag that stays inside the word
             * it began in takes characters; the moment it crosses out of
             * that word it takes whole words, and it keeps snapping even if
             * it comes back. Which is why this is a latch rather than a test
             * of where the pointer is now. */
            /* **The latched word is from a press that may not have been
             * this one.** `drag_word_from`/`drag_word_to` are set by the
             * plain-click branch of WM_LBUTTONDOWN only; a double click, a
             * triple click, a press in the selection bar and a press that
             * begins a drag-and-drop all return before reaching it, so they
             * leave whatever the previous drag left. The text can have
             * changed since -- been emptied, even -- and then this snaps the
             * selection to a boundary that no longer exists.
             *
             * Found by tests/monkey_test.c, shrunk to ten steps: a drag in a
             * document that had been cleared produced `selection 0..2` with
             * `len 0`, the 2 being a word boundary from an earlier drag on
             * longer text. The line table was correct and the hit test was
             * correct; only the latch was stale.
             *
             * Clamped where it is used rather than reset where it is set,
             * because **the invariant is the selection's, not the latch's**:
             * whichever press filled these in, a selection may not leave the
             * text. */
            if (e->drag_word_from > e->len)
                e->drag_word_from = e->len;
            if (e->drag_word_to > e->len)
                e->drag_word_to = e->len;
            if (!e->drag_snapped &&
                (at < e->drag_word_from || at > e->drag_word_to))
                e->drag_snapped = 1;
            if (e->drag_snapped) {
                int wf, wt;
                rich_word_bounds(e, at < e->len ? at : (e->len ? e->len - 1 : 0),
                                 &wf, &wt);
                if (at >= e->drag_word_to) {
                    anchor = e->drag_word_from;
                    at = wt > at ? wt : at;
                } else {
                    anchor = e->drag_word_to;
                    at = wf;
                }
            }
            if (at != e->caret || anchor != e->anchor) {
                e->anchor = anchor;
                e->caret = at; /* a drag extends from the anchor */
                rich_selchange(wnd, e);
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        e->sb_grab = -1;
        e->hsb_grab = -1;
        e->bar_anchor_row = -1;
        if (e->dnd_pending) {
            int at = rich_index_at_point(wnd, e, GET_X_LPARAM(lp),
                                         GET_Y_LPARAM(lp));
            int selfrom, selto;
            rich_range(e, &selfrom, &selto);
            if (e->dnd_active) {
                /* Dropped outside: the run moves and stays selected where it
                 * lands. Dropped inside itself: nothing changes, which is
                 * §5 and is what rich_move_selection refuses. */
                rich_move_selection(wnd, e, at);
            } else {
                /* No drag: a press and release inside a selection puts the
                 * caret where it landed, which is what makes a click inside
                 * a selection clear it. */
                e->caret = e->anchor = at;
            }
            e->dnd_pending = e->dnd_active = 0;
            rich_selchange(wnd, e);
            rich_show_caret(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
        }
        if (GetCapture() == wnd)
            ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK:
        if (!(wnd->style & WS_DISABLED)) {
            /* Remembered so the press after it can be told from a first one;
             * see WM_LBUTTONDOWN. */
            e->dbl_ms = ween_now_ms();
            e->dbl_seen = 1;
            e->dbl_x = GET_X_LPARAM(lp);
            e->dbl_y = GET_Y_LPARAM(lp);
            e->caret = rich_index_at_point(wnd, e,
                                           GET_X_LPARAM(lp),
                                           GET_Y_LPARAM(lp));
            /* In the selection bar it takes the paragraph rather than the
             * word -- §5, measured. */
            if (rich_in_selbar(wnd, GET_X_LPARAM(lp)))
                rich_select_para(e, e->caret);
            else
                rich_select_word(e);
            rich_show_caret(wnd, e);
            rich_selchange(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    /* **WM_HSCROLL, which this control used to ignore outright.** alice
     * measured it: 500 w's in No wrap, the message sent, and character 499
     * still at x 3993 -- four fifths of the line unreachable by any means.
     *
     * Every step here is read off the machine (`tools/vm/hscroll.txt`) rather
     * than taken from the vertical side, because the two axes do not agree:
     * the vertical page is a screenful less one line and this one is not a
     * screenful less one character.
     *
     *     SB_LINELEFT/RIGHT   7 px      constant across three fonts
     *     SB_PAGELEFT/RIGHT   the page  client - 14
     *     SB_LEFT / SB_RIGHT  the ends
     */
    case WM_HSCROLL: {
        ween_sbstate st = rich_hsbstate(wnd);
        int pos = e->scroll_x;
        switch (LOWORD(wp)) {
        case SB_LINELEFT:  pos -= st.line; break;
        case SB_LINERIGHT: pos += st.line; break;
        case SB_PAGELEFT:  pos -= st.page; break;
        case SB_PAGERIGHT: pos += st.page; break;
        case SB_LEFT:      pos = st.min; break;
        case SB_RIGHT:     pos = ween_sb_maxpos(&st); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: pos = HIWORD(wp); break;
        default: return 0;
        }
        pos = ween_sb_clamp(pos, &st);
        if (pos != e->scroll_x) {
            e->scroll_x = pos;
            rich_notify(wnd, EN_HSCROLL, ENM_SCROLL);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    }

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
            /* **Nothing. riched20 ignores this, and it is measured.**
             *
             *     riched20 (Windows 2000)   ween32, before this
             *     WM_CHAR CR   nothing      inserted a break
             *     WM_KEYDOWN VK_RETURN
             *                  inserts      did nothing
             *
             * Exactly opposite, on both forms. A program sending `WM_CHAR`
             * CR to a rich edit got a paragraph from us and nothing from
             * Windows; one sending the keydown got the reverse. The break is
             * made in WM_KEYDOWN now, where the machine makes it.
             *
             * **A keyboard is unaffected either way**, because a keystroke
             * delivers both messages -- WM_KEYDOWN, then WM_CHAR through
             * TranslateMessage -- so the control acts once whichever of the
             * two it listens to. That is why this was invisible: everything
             * in tree types through the backend, which sends the pair. */
            return 0;
        } else if (ch == '\b') {
            rich_remember(e);
            if (!rich_delete_selection(wnd, e) && e->caret > 0)
                rich_delete_range(wnd, e, e->caret - 1, e->caret);
        } else if ((unsigned char)ch >= ' ' || ch == '\t') {
            /* **A tab is a character here and nothing more.** It was filtered
             * out by `>= ' '` alone, which is why jd's Tab key did nothing --
             * the layout half has always worked (`rich_next_tab_stop`, and
             * paragraph tab stops are honoured), so only the character was
             * missing.
             *
             * Sam measured riched20 on Windows 2000 and against WordPad
             * itself (tools/vm/wordkey.c): **Tab always inserts an `09` and
             * never indents** -- not at a paragraph start, not on a bulleted
             * paragraph, not on an indented one, with `wNumbering` staying 1
             * and `dxStartIndent` staying 720 while the tab goes into the
             * text. **Shift+Tab inserts one too; it is not an outdent.** All
             * three were live possibilities and none of them is guessed here.
             *
             * *Which message* riched20 acts on is not measured and cannot be
             * from that reading: `keybd_event` delivers the keydown and the
             * character both. It is done here, where every other ordinary
             * character is done, rather than in WM_KEYDOWN where the
             * paragraph break is -- the break is there because riched20 was
             * measured ignoring `WM_CHAR` CR, and no such reading exists for
             * a tab. */
            char one[2];
            int from, to;
            rich_range(e, &from, &to);
            if (e->limit && e->len - (to - from) + 1 > e->limit) {
                rich_notify(wnd, EN_MAXTEXT, ENM_CHANGE);
                return 0;
            }
            one[0] = ch;
            one[1] = 0;
            /* **A typed run is one step, however many characters it is** --
             * riched20 calls it UID_TYPING and Sam measured "hello" and
             * "ab cd" each coming back in a single undo. This joins the step
             * before it when that one is also typing; anything else closes
             * the run. */
            rich_remember_as(e, 1);
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
        int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
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
            if (ctrl)
                e->caret = rich_word_left(e, e->caret);
            else if (e->caret > 0)
                e->caret--;
            break;
        case VK_RIGHT:
            if (ctrl)
                e->caret = rich_word_right(e, e->caret);
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
        case VK_RETURN:
            /* **The paragraph break is made here, which is where riched20
             * makes it.** Measured on Windows 2000: `WM_KEYDOWN VK_RETURN`
             * inserts and `WM_CHAR` CR does nothing, and ours was exactly
             * opposite until this.
             *
             * A line of its own is written the way Windows writes one -- a
             * carriage return and a line feed when the text is read back --
             * though it is *one* character in index space, which is what
             * `line 1 at 2` says for `type, enter, type`.
             *
             * A single-line control is not typing at all: the dialog's
             * default button has already had its chance at the key. */
            if (wnd->style & (WS_DISABLED | ES_READONLY))
                return 0;
            if (!(wnd->style & ES_MULTILINE))
                return 0;
            rich_remember(e);
            rich_delete_selection(wnd, e);
            rich_insert(wnd, e, "\r"); /* one character, as it is stored */
            rich_show_caret(wnd, e);
            rich_changed(wnd, e);
            InvalidateRect(wnd, NULL, FALSE);
            return 0;
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
