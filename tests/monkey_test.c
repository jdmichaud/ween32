/* A monkey on the rich edit: many gestures in a row, on whatever the last
 * one left behind, with the oracle checked after **every** step.
 *
 * jd: *"There are many bugs around the editor. Adding text, filling up the
 * editor, removing text, selecting text. Doing those actions **in
 * combination** brings many bugs. Please do some monkey tests on this area."*
 *
 * **The combination is the whole point.** Every other test in this
 * repository performs one gesture on a state it built itself; jd performs
 * five in a row on the wreckage of the previous four. Nothing we own does
 * that, which is why his eight findings were all in states no capture is in.
 *
 * ## What it may assert, and what it may not
 *
 * The oracle is docs/testing.md §2a and **not** the list anybody would write
 * from memory -- four of those are measurably false, two of them in ways that
 * would fire on every empty document. The ones this file must not use are
 * named where they would naturally have gone, so that the next person adding
 * an assertion here reads why it is missing before adding it back.
 *
 * ## Seeded, printed, shrunk
 *
 * A failure that cannot be re-run is an anecdote. The seed is printed on
 * every run and taken as `argv[1]`; on a failure the sequence is **shrunk**
 * -- steps dropped one at a time while the failure survives -- and the
 * shortest surviving sequence is printed as the reproduction. A four-step
 * reproduction is a fix; a four-thousand-step one is a mystery.
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../src/ween_internal.h"
#include <ween32.h>

static int g_failures;
static int g_checks;
#define CHECK(cond, what)                                                      \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            printf("FAIL %s\n", what);                                         \
            g_failures++;                                                      \
        } else                                                                \
            printf("ok   %s\n", what);                                         \
    } while (0)
static int g_verbose;

/* ---- the operations ------------------------------------------------------
 *
 * Chosen to be jd's four verbs and nothing clever: add, fill, remove, select.
 * Each is one message the way an application sends it. */
enum {
    OP_TYPE,     /* a character, as WM_CHAR does it */
    OP_ENTER,    /* a paragraph break */
    OP_PASTE,    /* a run of text at once, which is how an editor fills */
    OP_BACK,     /* backspace */
    OP_DELETE,   /* forward delete */
    OP_SELECT,   /* a range, sometimes backwards on purpose */
    OP_SELALL,
    OP_HOME,
    OP_END,
    OP_UP,
    OP_DOWN,
    OP_LEFT,
    OP_RIGHT,
    OP_REPLACE,  /* EM_REPLACESEL over whatever is selected */
    OP_CLEAR,    /* select all and delete: the emptying jd names */
    OP_CLICK,    /* a press and release somewhere in the client */
    OP_DRAG,     /* press, two moves, release: a selection by pointer */
    OP_RESIZE,   /* jd's "not synchronised with the window size" */
    OP_BOLD,     /* jd: "if you change the style and type something" */
    OP_ITALIC,
    OP_SIZE,     /* a character size, which is a run change and not a text one */
    /* **The monkey could not undo.** Two of jd's reports were undo bugs and
     * this instrument had no way to reach one: `EM_UNDO` was exercised by
     * the formatting invariant, one step at a time, and never as an
     * operation the sequence could contain. So "type, style, type, undo,
     * type" -- a shape a person produces constantly -- was unreachable. */
    OP_UNDO,
    OP_N
};

static const char *op_name(int op)
{
    static const char *n[] = { "type",  "enter", "paste", "back", "delete",
                               "select", "selall", "home", "end", "up",
                               "down",  "left",  "right", "replace", "clear",
                               "click", "drag", "resize",
                               "bold", "italic", "size", "undo" };
    return n[op];
}

/* xorshift, so a seed reproduces the same run on any machine and in any
 * libc -- rand() does not, and a reproduction that depends on the C library
 * is not a reproduction. */
static unsigned g_rng;
static unsigned nextr(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
static int upto(int n) { return n > 0 ? (int)(nextr() % (unsigned)n) : 0; }

struct step {
    int op;
    int a, b;
};

/* A gesture goes in whole and is then pumped, because a drain between two
 * injections is a drain that finishes the gesture -- richdrag_test's header
 * says the same. */
static void inject(int kind, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = kind;
    ev.button = 1;
    ev.x = x;
    ev.y = y;
    ween_headless_inject(ev);
}

static void pump(void)
{
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0))
        DispatchMessageA(&m);
}

static void do_step(HWND re, const struct step *s)
{
    switch (s->op) {
    case OP_TYPE: SendMessageA(re, WM_CHAR, (WPARAM)('a' + s->a % 26), 1); break;
    case OP_ENTER: SendMessageA(re, WM_CHAR, '\r', 1); break;
    case OP_PASTE: {
        char buf[128];
        int i, n = 1 + s->a % 60;
        for (i = 0; i < n; i++)
            buf[i] = (char)("abcdefghij klmnopqrst "[(s->b + i) % 22]);
        buf[n] = 0;
        SendMessageA(re, EM_REPLACESEL, TRUE, (LPARAM)buf);
        break;
    }
    /* **lParam 0 for a key, not 1.** The backend puts Shift in bit 0 of a
     * WM_KEYDOWN's lParam where win32 keeps the repeat count
     * (src/richedit.c:3766), so the 1 that WM_CHAR wants makes every arrow a
     * *shift*-arrow. These operations were written with 1 and have been
     * holding Shift down ever since -- `home`, `end` and the four arrows
     * extended the selection instead of moving the caret.
     *
     * **Nothing it reported was wrong**, because the invariants are about
     * the document rather than about which key was pressed. What was wrong
     * is what it was exploring: a monkey whose navigation always extends
     * cannot reach the states a person reaches by pressing an arrow to
     * deselect, which is one of the commonest gestures there is. Found by
     * tests/replay_test.c on its first run. */
    case OP_BACK: SendMessageA(re, WM_KEYDOWN, VK_BACK, 0); break;
    case OP_DELETE: SendMessageA(re, WM_KEYDOWN, VK_DELETE, 0); break;
    case OP_SELECT: {
        int len = (int)SendMessageA(re, WM_GETTEXTLENGTH, 0, 0);
        /* Sometimes backwards on purpose: whether the control normalises a
         * reversed range is listed unmeasured, so this exercises it without
         * asserting which way it should come back. */
        SendMessageA(re, EM_SETSEL, (WPARAM)(len ? s->a % (len + 1) : 0),
                     (LPARAM)(len ? s->b % (len + 1) : 0));
        break;
    }
    case OP_SELALL: SendMessageA(re, EM_SETSEL, 0, -1); break;
    case OP_HOME: SendMessageA(re, WM_KEYDOWN, VK_HOME, 0); break;
    case OP_END: SendMessageA(re, WM_KEYDOWN, VK_END, 0); break;
    case OP_UP: SendMessageA(re, WM_KEYDOWN, VK_UP, 0); break;
    case OP_DOWN: SendMessageA(re, WM_KEYDOWN, VK_DOWN, 0); break;
    case OP_LEFT: SendMessageA(re, WM_KEYDOWN, VK_LEFT, 0); break;
    case OP_RIGHT: SendMessageA(re, WM_KEYDOWN, VK_RIGHT, 0); break;
    case OP_REPLACE:
        SendMessageA(re, EM_REPLACESEL, TRUE, (LPARAM) "XY");
        break;
    case OP_CLEAR:
        SendMessageA(re, EM_SETSEL, 0, -1);
        SendMessageA(re, WM_KEYDOWN, VK_DELETE, 0);
        break;
    /* **The pointer goes in as injected events, not as messages**, so this
     * exercises the route and not only the handler at the end of it -- my own
     * §5 distinction, and the reason tests/richdrag_test.c exists.
     *
     * It could not, until an hour ago. An injected gesture reached a control
     * exactly once per program: `hl_next_event` answered `WEEN_EV_END` on an
     * empty queue, that set `g_quit`, and nothing cleared it. Measured then
     * and again after Dan's fix, three identical drags in one program:
     *
     *     before   0..12, then 0..0, then 0..0
     *     after    0..12, then 0..6, then 0..20
     *
     * **`ev.button` is not optional.** The first version of that probe left
     * it zero and every drag selected nothing -- a press with no button, which
     * looks exactly like a harness that does not work. */
    case OP_CLICK: {
        RECT c;
        int x, y, ox, oy;
        GetClientRect(re, &c);
        ween_client_origin(re, &ox, &oy);
        x = c.right ? s->a % c.right : 0;
        y = c.bottom ? s->b % c.bottom : 0;
        inject(WEEN_EV_MOUSE_DOWN, ox + x, oy + y);
        inject(WEEN_EV_MOUSE_UP, ox + x, oy + y);
        pump();
        break;
    }
    case OP_DRAG: {
        RECT c;
        int x0, y0, x1, y1, ox, oy;
        GetClientRect(re, &c);
        ween_client_origin(re, &ox, &oy);
        x0 = c.right ? s->a % c.right : 0;
        y0 = c.bottom ? s->b % c.bottom : 0;
        x1 = c.right ? (s->b * 7) % c.right : 0;
        y1 = c.bottom ? (s->a * 3) % c.bottom : 0;
        inject(WEEN_EV_MOUSE_DOWN, ox + x0, oy + y0);
        inject(WEEN_EV_MOUSE_MOVE, ox + (x0 + x1) / 2, oy + (y0 + y1) / 2);
        inject(WEEN_EV_MOUSE_MOVE, ox + x1, oy + y1);
        inject(WEEN_EV_MOUSE_UP, ox + x1, oy + y1);
        pump();
        break;
    }
    /* **Formatting changes runs and not text**, which is the whole of jd's
     * second Undo report: *"if you change the style and type something, only
     * the style is undone."* A monkey that never sets a style cannot see
     * it -- and this one could not, all evening. */
    case OP_BOLD:
    case OP_ITALIC: {
        CHARFORMATA cf;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = s->op == OP_BOLD ? CFM_BOLD : CFM_ITALIC;
        cf.dwEffects = (s->a & 1)
                           ? (s->op == OP_BOLD ? CFE_BOLD : CFE_ITALIC)
                           : 0;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        break;
    }
    case OP_SIZE: {
        CHARFORMATA cf;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_SIZE;
        cf.yHeight = (LONG)(8 + s->a % 20) * 20; /* points to twips */
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        break;
    }
    case OP_UNDO: SendMessageA(re, EM_UNDO, 0, 0); break;
    case OP_RESIZE:
        /* jd: "the editor does not seem synchronised with the window size."
         * A resize between two edits is the combination he describes, and
         * the scrollbar's width is coupled to the wrap, so this is where a
         * re-wrap bug would surface. */
        SetWindowPos(re, NULL, 0, 0, 60 + s->a % 200, 24 + s->b % 100,
                     SWP_NOMOVE | SWP_NOZORDER);
        break;
    }
}

/* ---- the oracle ----------------------------------------------------------
 *
 * docs/testing.md §2a, and each entry says which shelf it came off. The
 * `design` ones are our own rules rather than the machine's, which is
 * recorded here as it is there: a monkey failure has to be readable as
 * either "the program broke a measured rule" or "the program broke a rule we
 * invented", and those are different bugs.
 *
 * **Four plausible invariants are deliberately absent**, with the reason at
 * the point where each would have gone. */
static const char *check_all(HWND re)
{
    CHARRANGE cr;
    int len = (int)SendMessageA(re, WM_GETTEXTLENGTH, 0, 0);
    int lines = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0);
    int first = (int)SendMessageA(re, EM_GETFIRSTVISIBLELINE, 0, 0);
    int lastline;

    /* MEASURED (machine, RichEdit20W): **EM_POSFROMCHAR always answers a
     * real position**, including the two places a monkey stands in most --
     * index 0 of an empty control, and index == length after every append.
     * Past the end it clamps rather than refusing.
     *
     *     empty, index 0          x 1  y 0
     *     "abc\r\ndef", index 8    x 21 y 16      (index == length)
     *     index 9, past the end   x 21 y 16      clamped
     *
     * **This invariant was dropped from the oracle and then put back**, and
     * the reason is worth carrying: the -1 belongs to the *EDIT*, which is a
     * different call wearing the same name --
     *
     *     EDIT      wParam = index           the point is the return value
     *     RICHEDIT  wParam = POINTL *out     lParam = index
     *
     * -- so an EDIT's answer is not weak evidence about riched20's, it is
     * none. Asserting the EDIT's rule here would have failed on every empty
     * document; dropping it, as the first oracle said to, would have lost the
     * one invariant that ties the model to the pixels. */
    {
        /* **What is asserted is that it *answers*, not that the answer is
         * positive.** The first version of this checked `x >= 0 && y >= 0`
         * and the monkey broke it in 123 steps with
         *
         *     index 0 -> 1,-12   (len 68, 6 lines, first visible 1)
         *
         * which is **correct**: the document is scrolled by a line, so
         * character zero really is twelve pixels above the client. Sam's
         * readings are all of an unscrolled control, and I turned "it
         * answers a real position" into "the position is non-negative" --
         * the evening's own failure, a true sentence restated one level
         * stronger than its source, committed twenty minutes after reading
         * the warning about it.
         *
         * A sentinel is the way to ask the measured question: riched20's
         * result is always 0 and the point is written, so "did it refuse"
         * is exactly "was the point left alone". */
        POINTL p0, pl;
        p0.x = p0.y = pl.x = pl.y = 0x7f0e5eed;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p0, 0);
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&pl, (LPARAM)len);
        if (p0.x == 0x7f0e5eed || p0.y == 0x7f0e5eed)
            return "EM_POSFROMCHAR left index 0 unanswered";
        if (pl.x == 0x7f0e5eed || pl.y == 0x7f0e5eed)
            return "EM_POSFROMCHAR left the end of the text unanswered";
    }

    /* NOT ASSERTED: "distinct indices have distinct positions." True across a
     * line break for riched20 -- index 3 ends line 1 and index 4 starts line
     * 2, where an EDIT answers the same point for both -- but **not
     * everywhere**: indices 7 and 8 of "abc\r\ndef" both answer x 21 y 16.
     * The measured rule is that the boundary *answers*, not that every index
     * differs. */

    memset(&cr, 0, sizeof cr);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
    /* design, not measured: whether the control normalises a backwards range
     * has not been read off the machine. What is asserted is only that both
     * ends are inside the text -- an end outside it is a fault under any
     * normalisation rule. */
    if (cr.cpMin < 0 || cr.cpMax < 0)
        return "a selection end is negative";
    if (cr.cpMin > len || cr.cpMax > len)
        return "a selection end is past the end of the text";

    /* design: a document ending in a break has an empty last line, and it
     * counts -- so the last character is on the last line. */
    lastline = (int)SendMessageA(re, EM_LINEFROMCHAR, (WPARAM)len, 0);
    if (lines < 1)
        return "the line count is less than one";
    if (lastline != lines - 1)
        return "the last index is not on the last line";

    /* design */
    if (first < 0 || first >= lines) {
        static char m[128];
        sprintf(m, "the first visible line is outside the document "
                   "(first %d, lines %d, len %d)", first, lines, len);
        return m;
    }

    /* MEASURED (machine): the control raises and lowers WS_VSCROLL itself,
     * and it is not a latch -- empty 550081C4, overflowing 552081C4, emptied
     * again 550081C4.
     *
     * **Asserted as one direction only.** alice asked for "set iff the
     * content exceeds the view"; Sam's three readings are empty, well over,
     * and empty again, and *the exactly-fits boundary is not among them*, so
     * `iff` is stronger than the reading. What is checked is the half that
     * cannot be wrong at a boundary: **if the style is up, the document must
     * not fit** -- because that is the state the machine's third reading
     * rules out. Whether it must be up when the content exceeds by one line
     * is left alone until somebody reads the boundary. */
    {
        int up = (GetWindowLongA(re, GWL_STYLE) & WS_VSCROLL) != 0;
        /* "Does the document fit" has no public message -- the visible-row
         * count is the control's own arithmetic -- so rather than reach
         * inside for it, this asserts the two ends of Sam's reading that
         * need no such number:
         *
         *   an empty document must not have the bar up   (his third state)
         *   a bar up implies more than one line          (a one-line
         *                                                 document cannot
         *                                                 overflow a box a
         *                                                 line tall)
         *
         * Both hold at every boundary, which is the property `iff` lacked. */
        if (up && len == 0)
            return "the bar is up on an empty document";
        if (up && lines <= 1)
            return "the bar is up on a document of one line";
    }
    return NULL;
}

/* ---- running a sequence, and shrinking one that fails --------------------- */

/* **Pairs, not a uniform shuffle -- and the pairs go through the pointer.**
 *
 * jd: *"Make sure the monkey testing is testing different scenarios, with
 * interaction between all those features."* Every bug he has found lived in
 * an interaction, and a uniform draw over twenty-one operations reaches a
 * particular two of them together about once in four hundred steps.
 *
 * `tools/vm/replay.h` has the same idea for the differential language. **The
 * list here is deliberately different**, because this instrument can do the
 * thing that one cannot: **drive a pointer.** An injected gesture is not
 * something riched20 can be asked to replay identically, so the shared
 * language has no mouse at all -- which leaves every drag, every click and
 * every press-inside-a-selection to this file.
 *
 * That is not a small remainder. §5's drag-and-drop, the word-snapping
 * latch, the selection bar and the click-count state are reachable only from
 * here, and the latch bug earlier tonight was a gesture setting state that
 * five other gestures read.
 */
static void gen_pair(struct step *out, int first, int second)
{
    out[0].op = first;
    out[0].a = (int)(nextr() % 1000u);
    out[0].b = (int)(nextr() % 1000u);
    out[1].op = second;
    out[1].a = (int)(nextr() % 1000u);
    out[1].b = (int)(nextr() % 1000u);
}

static void gen_seq(struct step *seq, int n)
{
    static const int pairs[][2] = {
        /* the pointer ones, which only this instrument can reach */
        { OP_DRAG, OP_TYPE },     /* select by pointer, then replace it */
        { OP_DRAG, OP_BOLD },     /* style what a gesture selected */
        { OP_SELALL, OP_DRAG },   /* a press inside a selection: §5's drag
                                     and drop, or a new selection -- the
                                     control decides on the button coming up */
        { OP_DRAG, OP_UNDO },
        { OP_CLICK, OP_BOLD },    /* caret placed by pointer, then armed */
        { OP_RESIZE, OP_DRAG },   /* the layout moved under the gesture */
        { OP_DRAG, OP_DRAG },     /* the word latch, set by one and read by
                                     the next */
        /* and the message-level ones the differential test also covers,
         * because a gesture mixed with them is a third thing again */
        { OP_BOLD, OP_TYPE },     { OP_SELECT, OP_REPLACE },
        { OP_PASTE, OP_UNDO },    { OP_SIZE, OP_TYPE },
        { OP_CLEAR, OP_TYPE }
    };
    int i = 0;
    while (i < n) {
        if (i + 1 < n && (nextr() & 1)) {
            int k = (int)(nextr() % (sizeof pairs / sizeof pairs[0]));
            gen_pair(seq + i, pairs[k][0], pairs[k][1]);
            i += 2;
        } else {
            seq[i].op = upto(OP_N);
            seq[i].a = (int)(nextr() % 1000u);
            seq[i].b = (int)(nextr() % 1000u);
            i++;
        }
    }
}

static HWND g_host;

/* **The document as a person sees it: its text, and what each character is
 * drawn in.** The oracle up to now read the selection, the length and the
 * line count -- none of which a formatting change touches -- so a monkey
 * that styled text and undid it went green all evening. jd found it by
 * hand instead: *"if you change the style and type something, only the
 * style is undone."*
 *
 * Effects are sampled per character because that is what an undo has to
 * restore. Comparing whole CHARFORMATs would compare fields nobody has
 * measured. */
struct doc {
    char text[512];
    unsigned char fx[512]; /* bold | italic | underline, per character */
    int len;
};

/* **The control counts characters differently from the text it hands out.**
 * A paragraph break is stored as one character and comes back through
 * `GetWindowTextA` as `\r\n`, so the string is longer than the document.
 * Indexing characters by `strlen` of that string walks off the end, and the
 * control answers a query past the end with the *armed insert format* rather
 * than refusing -- so a phantom character appears, carrying a format no undo
 * ever recorded.
 *
 * **That cost me an evening's wrong conclusion.** The invariant reported "a
 * formatting change could not be undone" on sequences where nothing was
 * wrong: `EM_EXSETSEL 3..4` on a three-character document clamps to an empty
 * selection, which *arms* a format instead of applying one, and the next
 * read of the phantom index showed it. The program was right and the oracle
 * was reading past the end of it.
 *
 * The length in the control's own numbering is what `EM_EXSETSEL` with a
 * `cpMax` of -1 reports back -- ctl14.txt measured that it resolves the -1
 * rather than storing it. */
static int doc_length(HWND re)
{
    CHARRANGE all, keep;
    memset(&keep, 0, sizeof keep);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&keep);
    all.cpMin = 0;
    all.cpMax = -1;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&all);
    memset(&all, 0, sizeof all);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&all);
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&keep);
    return (int)all.cpMax;
}

static void snap_doc(HWND re, struct doc *d)
{
    int i;
    d->len = doc_length(re);
    if (d->len > 511)
        d->len = 511;
    memset(d->text, 0, sizeof d->text);
    memset(d->fx, 0, sizeof d->fx);
    GetWindowTextA(re, d->text, (int)sizeof d->text);
    for (i = 0; i < d->len; i++) {
        CHARFORMATA cf;
        CHARRANGE r;
        r.cpMin = i;
        r.cpMax = i + 1;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        d->fx[i] = (unsigned char)(((cf.dwEffects & CFE_BOLD) ? 1 : 0) |
                                   ((cf.dwEffects & CFE_ITALIC) ? 2 : 0) |
                                   ((cf.dwEffects & CFE_UNDERLINE) ? 4 : 0));
    }
}

static int doc_same(const struct doc *a, const struct doc *b)
{
    if (a->len != b->len)
        return 0;
    if (memcmp(a->text, b->text, (size_t)a->len))
        return 0;
    return memcmp(a->fx, b->fx, (size_t)a->len) == 0;
}


static LRESULT CALLBACK host_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

/* Runs `n` steps into a control built fresh, and returns the first step at
 * which the oracle broke, or -1. The control is rebuilt every time so that a
 * replay is a replay: a monkey that reuses a control is testing the previous
 * run as well as this one. */
static int run_seq(const struct step *seq, int n, const char **why, int *at)
{
    HWND re = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                              WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                  ES_AUTOVSCROLL | ES_WANTRETURN,
                              0, 0, 220, 80, g_host, NULL, NULL, NULL);
    int i;
    if (!re) {
        *why = "the control could not be created";
        *at = 0;
        return 1;
    }
    SetFocus(re);
    for (i = 0; i < n; i++) {
        const char *bad;
        int styling = seq[i].op == OP_BOLD || seq[i].op == OP_ITALIC ||
                      seq[i].op == OP_SIZE;
        /* **One edit, then one undo, is the identity -- formatting
         * included.** That is jd's second Undo report stated as a rule: a
         * style change that cannot be taken back is an edit the record does
         * not hold.
         *
         * Only after formatting steps. It spends an undo and restores the
         * state it measured, so doing it on every step would double the work
         * and change what the rest of the sequence explores -- and the
         * formatting ops are the ones no invariant covered. */
        if (styling) {
            struct doc was, now;
            CHARRANGE keep;
            memset(&keep, 0, sizeof keep);
            SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&keep);
            /* **Only a set over a real range**, which is the case Sam
             * measured: bold 0..3, type, undo, undo. A set on an *empty*
             * selection is the other thing EM_SETCHARFORMAT does -- it arms
             * the format the next character will carry, changing no
             * character and pushing no step -- and whether riched20 makes
             * that undoable nobody has asked. Asserting it here cost me a
             * false failure: the undo took back the typing instead, which
             * looked like the formatting bug and was the invariant reaching
             * past its evidence. */
            if (keep.cpMin == keep.cpMax) {
                do_step(re, &seq[i]);
                bad = check_all(re);
                if (bad) {
                    *why = bad;
                    *at = i;
                    DestroyWindow(re);
                    return 1;
                }
                continue;
            }
            snap_doc(re, &was);
            SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&keep);
            do_step(re, &seq[i]);
            snap_doc(re, &now);
            if (!doc_same(&was, &now)) { /* the step changed something */
                SendMessageA(re, EM_UNDO, 0, 0);
                snap_doc(re, &now);
                if (!doc_same(&was, &now)) {
                    *why = "a formatting change could not be undone";
                    *at = i;
                    DestroyWindow(re);
                    return 1;
                }
                SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&keep);
                do_step(re, &seq[i]); /* put it back and carry on */
            }
            SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&keep);
        } else {
            do_step(re, &seq[i]);
        }
        bad = check_all(re);
        if (bad) {
            *why = bad;
            *at = i;
            DestroyWindow(re);
            return 1;
        }
    }
    DestroyWindow(re);
    return 0;
}

/* Drop steps while the failure survives. Reported shortest-first because a
 * four-step reproduction is a fix and a four-thousand-step one is a mystery. */
static int shrink(struct step *seq, int n, const char **why, int *at)
{
    int changed = 1;
    while (changed) {
        int i;
        changed = 0;
        for (i = 0; i < n; i++) {
            struct step keep = seq[i];
            const char *w2;
            int a2;
            memmove(seq + i, seq + i + 1,
                    (size_t)(n - i - 1) * sizeof *seq);
            if (run_seq(seq, n - 1, &w2, &a2)) {
                n--;
                *why = w2;
                *at = a2;
                changed = 1;
                i--;
            } else {
                memmove(seq + i + 1, seq + i,
                        (size_t)(n - i - 1) * sizeof *seq);
                seq[i] = keep;
            }
        }
    }
    return n;
}

/* **A shrunk sequence is not a prefix of its seed**, so a seed and a length
 * cannot replay it -- the shrinker drops steps from the middle. The first
 * version of this printed `monkey_test <seed> <n>` as the reproduction and
 * that command re-runs the first n steps of the seed instead, which passed
 * while the real sequence still failed. **A reproduction line that does not
 * reproduce is worse than none**, because it reads as evidence the bug is
 * gone. The sequence itself is printed instead, and taken back verbatim. */
static int parse_replay(const char *t, struct step *seq, int max)
{
    int n = 0;
    while (*t && n < max) {
        int op = 0, a = 0, b = 0, i;
        char name[16];
        for (i = 0; i < 15 && *t && *t != ':' && *t != ','; i++)
            name[i] = *t++;
        name[i] = 0;
        if (*t == ':')
            a = (int)strtol(++t, (char **)&t, 10);
        if (*t == ':')
            b = (int)strtol(++t, (char **)&t, 10);
        if (*t == ',')
            t++;
        for (op = 0; op < OP_N; op++)
            if (!strcmp(op_name(op), name))
                break;
        if (op == OP_N)
            return -1;
        seq[n].op = op;
        seq[n].a = a;
        seq[n].b = b;
        n++;
    }
    return n;
}

static void print_replay(const struct step *seq, int n)
{
    int i;
    for (i = 0; i < n; i++)
        printf("%s%s:%d:%d", i ? "," : "", op_name(seq[i].op), seq[i].a,
               seq[i].b);
    printf("\n");
}

int main(int argc, char **argv)
{
    WNDCLASSA wc;
    unsigned seed;
    int steps = 400, runs = 12, r;

    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenmonkey";
    RegisterClassA(&wc);
    g_host = CreateWindowExA(0, "weenmonkey", "monkey", WS_POPUP | WS_VISIBLE,
                             0, 0, 260, 120, NULL, NULL, NULL, NULL);

    /* The seed is an argument so a failure can be re-run, and printed on
     * every run so there is something to re-run it with. Without one it
     * varies, so the suite explores over days instead of walking one path
     * for ever. */
    if (argc > 2 && !strcmp(argv[1], "--replay")) {
        struct step seq[4096];
        const char *why = NULL;
        int at = 0;
        int n = parse_replay(argv[2], seq, 4096);
        if (n < 0) {
            printf("FAIL --replay: unknown operation in the sequence\n");
            return 2;
        }
        if (run_seq(seq, n, &why, &at)) {
            printf("FAIL replay of %d step(s) broke at step %d: %s\n", n,
                   at + 1, why);
            return 1;
        }
        printf("monkey_test: the %d-step sequence passed\n", n);
        return 0;
    }
    if (argc > 1) {
        seed = (unsigned)strtoul(argv[1], NULL, 0);
        runs = 1;
    } else {
        /* **`time(NULL)` alone is not a varying seed.** Six runs in one
         * second all got 0x6a930041 and walked the identical path, which is
         * the opposite of the point -- the suite is meant to explore over
         * days rather than repeat one sequence for ever. The pid moves
         * between runs where the clock does not. */
        seed = (unsigned)time(NULL) ^ ((unsigned)getpid() << 16);
    }
    if (argc > 2)
        steps = atoi(argv[2]);
    if (!seed)
        seed = 1;
    if (getenv("MONKEY_VERBOSE"))
        g_verbose = 1;

    /* **Every sequence this has ever found, run first and for ever.** alice's
     * rule for a fuzz finding: a seed that failed becomes a regression test,
     * or the next change to the wrap arithmetic quietly brings it back and
     * the monkey only notices if it happens to roll the same dice again.
     *
     * These are the shrunk sequences, not the seeds -- a shrunk sequence is
     * not a prefix of its seed, so the seed cannot replay it. */
    {
        static const char *const pinned[] = {
            /* first visible line left past the end of a document that got
             * shorter: resize small, fill, resize large. Fixed by
             * rich_clamp_scroll in src/richedit.c. */
            "resize:230:329,paste:299:333,resize:794:252",
            /* a drag using a word boundary latched by an earlier press, in a
             * document that had been cleared since: selection 0..2 with a
             * length of 0. Fixed by clamping the latch in src/richedit.c. */
            "resize:94:534,drag:615:791,click:278:528,drag:274:582,"
            "replace:538:807,enter:161:310,drag:609:170,clear:542:12,"
            "resize:300:936,drag:592:326",
        };
        /* **A gesture that reaches nothing looks exactly like a gesture that
         * found no bug.** The pointer ops go in as injected events now, and
         * two separate faults have made injection silently do nothing today
         * -- a drained queue that ended the loop, and an `ev.button` left at
         * zero. So one pinned case asserts the route is live rather than
         * assuming it: text, then a drag across it, must select something. */
        {
            struct step seq[8];
            const char *why = NULL;
            int at = 0, n = parse_replay("paste:59:0,drag:5:1", seq, 8);
            HWND probe;
            CHARRANGE cr;
            (void)run_seq(seq, n, &why, &at);
            probe = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                    WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0,
                                    220, 80, g_host, NULL, NULL, NULL);
            SetFocus(probe);
            do_step(probe, &seq[0]);
            do_step(probe, &seq[1]);
            memset(&cr, 0, sizeof cr);
            SendMessageA(probe, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHECK(cr.cpMax > cr.cpMin,
                  "an injected drag across text selects some of it, so the "
                  "route from a pixel to the control is live");
            DestroyWindow(probe);
        }
        size_t k;
        for (k = 0; k < sizeof pinned / sizeof pinned[0]; k++) {
            struct step seq[64];
            const char *why = NULL;
            int at = 0, n = parse_replay(pinned[k], seq, 64);
            char label[160];
            sprintf(label, "the pinned sequence %d still passes", (int)k);
            CHECK(n > 0 && !run_seq(seq, n, &why, &at), label);
            if (n > 0 && why)
                printf("     %s\n", why);
        }
    }

    for (r = 0; r < runs; r++) {
        struct step *seq = malloc((size_t)steps * sizeof *seq);
        unsigned this_seed = seed + (unsigned)r * 2654435761u;
        const char *why = NULL;
        int at = 0, i, n;
        if (!seq)
            break;
        g_rng = this_seed ? this_seed : 1;
        gen_seq(seq, steps);
        if (run_seq(seq, steps, &why, &at)) {
            printf("FAIL monkey seed 0x%08x broke after %d step(s): %s\n",
                   this_seed, at + 1, why);
            g_failures++;
            n = shrink(seq, at + 1, &why, &at);
            printf("     shortest sequence that still does it: %d step(s)\n", n);
            for (i = 0; i < n; i++)
                printf("       %2d  %-8s a=%d b=%d\n", i, op_name(seq[i].op),
                       seq[i].a, seq[i].b);
            printf("     re-run it with: tests/monkey_test --replay ");
            print_replay(seq, n);
        } else {
            /* **Deliberately not an `ok` line.** verify.sh counts them and
             * holds the total to never shrink, so a check whose *number*
             * varies between runs would make that gate meaningless. The
             * pinned sequences are a fixed count and are counted; the random
             * runs report only when they fail, or under MONKEY_VERBOSE. */
            g_checks++;
            if (g_verbose)
                printf("ok   seed 0x%08x survived %d steps\n", this_seed,
                       steps);
        }
        free(seq);
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("monkey_test: %d run(s) of %d steps from seed 0x%08x, all passed\n",
           runs, steps, seed);
    return 0;
}
