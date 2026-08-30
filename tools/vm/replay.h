/* One sequence language and one document dump, shared by both sides of the
 * differential test.
 *
 * jd: *"Take the original wordpad as reference if needed. Make sure the
 * monkey testing is testing different scenarios, with interaction between
 * all those features."*
 *
 * **The oracle up to now has been our model of a rich edit.** The monkey
 * checks ween32 against what bob believes riched20 does, and the machine is
 * consulted one question at a time, after somebody already suspects
 * something. Nothing compared a *sequence* against riched20. This is the
 * half that makes "ours agrees with our model" into "ours agrees with
 * WordPad", which is the project's premise.
 *
 * **The same header compiles on both sides**: here against ween32, and on
 * the guest against the real riched20 through `zig cc -target
 * x86-windows-gnu`. One serialiser rather than two readings of a
 * description -- the mistake that a written contract invites is each side
 * implementing it slightly differently and the diff blaming the program.
 *
 * ## The language
 *
 * Comma-separated `op:a:b`, ASCII, one line. It is tests/monkey_test.c's
 * own format extended, because that shrinker already emits it and a shrunk
 * differential failure is the report worth having.
 *
 * **Absolute, never relative.** `select:3:7`, not "select three more". A
 * step has to mean the same thing on two documents that may already
 * disagree -- otherwise one divergence makes every later line differ and
 * the shrinker blames the wrong step.
 *
 * **Set, not toggle**, for bold/italic/underline. A toggle is a
 * read-then-write, so the two sides would diverge wherever they merely
 * *report* differently, which is not the bug being hunted.
 *
 * **No mouse.** It cannot be replayed identically across a headless
 * backend and a live guest, so any difference would be a fact about the
 * harness. Dan has the gesture half, one level up.
 */
#ifndef REPLAY_H
#define REPLAY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OP_TYPE, OP_ENTER, OP_PASTE, OP_BACK, OP_DELETE,
    OP_SELECT, OP_SELALL, OP_CLEAR, OP_HOME, OP_END,
    OP_UP, OP_DOWN, OP_LEFT, OP_RIGHT, OP_REPLACE,
    OP_BOLD, OP_ITALIC, OP_UNDER, OP_SIZE,
    OP_ALIGN, OP_BULLET, OP_INDENT, OP_RINDENT, OP_RESIZE, OP_UNDO,
    OP_N
};

static const char *const op_names[OP_N] = {
    "type", "enter", "paste", "back", "delete",
    "select", "selall", "clear", "home", "end",
    "up", "down", "left", "right", "replace",
    "bold", "italic", "under", "size",
    "align", "bullet", "indent", "rindent", "resize", "undo"
};

struct rp_step { int op, a, b; };

/* ---- the generator ------------------------------------------------------
 *
 * jd: *"Make sure the monkey testing is testing different scenarios, with
 * interaction between all those features."*
 *
 * **Every bug he has found lived in an interaction**, not in a feature: a
 * marker clamp that only failed once another marker had moved; a format
 * armed on an empty selection that failed to close a typed run; a scrollbar
 * whose appearance changed a wrapping width; a word latch set by one gesture
 * and read by five. **None is reachable by exercising one feature well**, and
 * a uniformly random sequence reaches them only by accident.
 *
 * So the generator emits **pairs** as often as it emits single operations,
 * and the pairs are the shapes that have actually bitten this project. Each
 * one is a bug somebody found by hand:
 *
 *     style then type        the arming boundary -- jd, twice
 *     select then replace    replacing a formatted run with another
 *     indent then type       paragraph state changed under the caret
 *     paste then undo        a grouping nobody had measured
 *     bullet then enter      what a new paragraph inherits
 *     resize while selected  layout moving under a selection
 *
 * **It emits the shared language, not the monkey's**, so a sequence that
 * fails here can be handed to the guest unchanged. That is the whole point:
 * tests/monkey_test.c drives a mouse and this cannot, and a sequence with a
 * drag in it is not a sequence riched20 can be asked to replay.
 */
static unsigned rp_rng;

static unsigned rp_next(void)
{
    rp_rng ^= rp_rng << 13;
    rp_rng ^= rp_rng >> 17;
    rp_rng ^= rp_rng << 5;
    return rp_rng;
}

static int rp_upto(int n) { return n > 0 ? (int)(rp_next() % (unsigned)n) : 0; }

/* One operation, filled in with plausible arguments. */
static void rp_one(struct rp_step *s, int op)
{
    s->op = op;
    s->a = 0;
    s->b = 0;
    switch (op) {
    case OP_TYPE: s->a = 'a' + rp_upto(26); break;
    case OP_PASTE:
    case OP_REPLACE: s->a = 1 + rp_upto(40); s->b = rp_upto(27); break;
    case OP_SELECT: s->a = rp_upto(60); s->b = rp_upto(60); break;
    case OP_BOLD:
    case OP_ITALIC:
    case OP_UNDER:
    case OP_BULLET: s->a = rp_upto(2); break;
    case OP_SIZE: s->a = 8 + rp_upto(20); break;
    case OP_ALIGN: s->a = rp_upto(3); break;
    case OP_INDENT: s->a = rp_upto(4) * 360; s->b = rp_upto(5) * 180; break;
    case OP_RINDENT: s->a = rp_upto(6) * 360; break;
    case OP_RESIZE: s->a = rp_upto(240); s->b = rp_upto(120); break;
    default: break;
    }
}

/* Fills `seq` with `n` steps and returns how many it wrote -- pairs mean the
 * count is reached rather than divided into. */
static int rp_generate(struct rp_step *seq, int n, unsigned seed)
{
    static const int pairs[][2] = {
        { OP_BOLD, OP_TYPE },      { OP_ITALIC, OP_TYPE },
        { OP_SELECT, OP_REPLACE }, { OP_INDENT, OP_TYPE },
        { OP_PASTE, OP_UNDO },     { OP_BULLET, OP_ENTER },
        { OP_SELALL, OP_RESIZE },  { OP_SIZE, OP_TYPE },
        { OP_ALIGN, OP_ENTER },    { OP_SELECT, OP_BOLD },
        /* jd found this one by driving the program: a right indent set and
         * then text long enough to meet it. Nothing could ask it before. */
        { OP_RINDENT, OP_PASTE }
    };
    static const int singles[] = {
        OP_TYPE, OP_TYPE, OP_ENTER, OP_PASTE, OP_BACK, OP_DELETE,
        OP_SELECT, OP_SELALL, OP_HOME, OP_END, OP_UP, OP_DOWN,
        OP_LEFT, OP_RIGHT, OP_REPLACE, OP_UNDO, OP_RESIZE
    };
    int i = 0;
    rp_rng = seed ? seed : 1;
    while (i < n) {
        /* **Half pairs.** Uniform singles reach an interaction only when the
         * dice happen to put two related operations together, which for two
         * specific ops out of twenty-four is rare enough that jd found these
         * by hand first. */
        if (i + 1 < n && rp_upto(2)) {
            int k = rp_upto((int)(sizeof pairs / sizeof pairs[0]));
            rp_one(&seq[i++], pairs[k][0]);
            rp_one(&seq[i++], pairs[k][1]);
        } else {
            rp_one(&seq[i++],
                   singles[rp_upto((int)(sizeof singles / sizeof singles[0]))]);
        }
    }
    return i;
}

static void rp_print(FILE *f, const struct rp_step *seq, int n)
{
    int i;
    for (i = 0; i < n; i++)
        fprintf(f, "%s%s:%d:%d", i ? "," : "", op_names[seq[i].op], seq[i].a,
                seq[i].b);
    fprintf(f, "\n");
}

/* The filler a `paste` or `replace` inserts. Generated rather than stored so
 * both sides produce identical bytes from the same two numbers, and chosen
 * to contain a space and no CR: a paste that carried a line break would mix
 * two questions in one step. */
static void rp_filler(char *out, int n, int seed)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz ";
    int i;
    for (i = 0; i < n; i++)
        out[i] = alphabet[(seed + i * 7) % 27];
    out[n] = 0;
}

/* ---- the control under test ---------------------------------------------
 *
 * **Both sides create it here, or a difference is the configuration.**
 * Sam's wine run found `enter:0:0` producing no paragraph break, which
 * looked like an operation that did nothing -- and a control without
 * `ES_MULTILINE` or `ES_WANTRETURN` swallows Return whatever the executor
 * sends. The language, the executor and the serialiser were shared and the
 * *window* was not, so the one remaining place the two sides could differ by
 * construction was the thing being tested.
 *
 * The style word is WordPad's own, read off the machine
 * (`reference/probe/window.txt`): `550081C4`, ex `210`. The default format
 * is what `src/main.zig` sends before a character exists. **The control
 * under test is the one jd drives**, so a divergence is the program rather
 * than the setup.
 */
static HWND rp_create(HWND parent)
{
    CHARFORMATA d;
    HWND re = CreateWindowExA(0x00000210, RICHEDIT_CLASSA, "",
                              (DWORD)0x550081C4, 0, 0, 280, 160, parent, NULL,
                              NULL, NULL);
    if (!re)
        return NULL;
    memset(&d, 0, sizeof d);
    d.cbSize = sizeof d;
    /* **The effects are named as well as the face, and that is not
     * decoration.** WordPad sets only face and size, and on the machine that
     * leaves riched20's own default *effects* in place -- its default face is
     * System and **System is bold** -- so every character came back `B--`
     * from the machine and `---` from us, in all seven scenarios, on
     * documents where nothing had been bolded.
     *
     * Read as a finding it is seven findings; read properly it is one
     * sentence: **the two sides did not start from the same character
     * format.** Naming the three effects with a mask of 0 makes both start
     * unbolded, so a bold difference later in a sequence is a real one.
     *
     * **The alternative was to teach the differ that effects may differ**,
     * which would have hidden every genuine bold divergence for ever. Making
     * the two sides start the same is the fix; excusing a difference is not. */
    d.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    d.dwEffects = 0;
    d.yHeight = 200; /* ten point, in twips */
    d.szFaceName[0] = 'A'; d.szFaceName[1] = 'r'; d.szFaceName[2] = 'i';
    d.szFaceName[3] = 'a'; d.szFaceName[4] = 'l'; d.szFaceName[5] = 0;
    SendMessageA(re, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&d);
    /* **Wrapping on, so the two sides wrap or neither does.**
     *
     * ween32's control wraps to its client by default; a bare
     * `RichEdit20W` does not, and three scenarios came back from the machine
     * with `len 84` and a single `line 0 at 0` while ours had wrapped at 44.
     * Comparing a wrapped document against an unwrapped one says nothing
     * about either.
     *
     * `EM_SETTARGETDEVICE(0, 0)` is what turns it on there -- Sam measured
     * that. **What is *not* measured is whether it is what WordPad uses**;
     * it is one way to get wrapping and possibly not that program's way. So
     * this is a statement about the harness and not about WordPad: the two
     * controls are put in the same condition, and if it later turns out
     * WordPad reaches that condition by another route, the route changes
     * here and the comparison does not. */
    SendMessageA(re, EM_SETTARGETDEVICE, 0, 0);
    SetFocus(re);
    return re;
}

/* ---- the executor -------------------------------------------------------
 *
 * **Here rather than in the caller, for the same reason the serialiser is.**
 * The language being shared is not enough: if each side writes its own
 * `step()`, the two can disagree about *how* an operation is performed --
 * which message, which flags, which order -- and the diff would blame the
 * program for a difference between two readings of the contract. Sam asked
 * for this and there is nothing in it that is ween32's: standard messages,
 * `CHARFORMATA` and `PARAFORMAT` throughout.
 *
 * **The keyboard operations pass lParam 0 and that is a workaround**, not a
 * choice. ween32 reads Shift out of lParam bit 0 (src/richedit.c:3766,
 * src/dialog.c:462) where win32 keeps the repeat count, so the 1 a real
 * keystroke carries reads as shift-held. Passing 0 makes the two sides agree
 * by feeding riched20 a repeat count Windows never sends -- **so this
 * harness cannot detect the divergence it is compensating for.** That is
 * recorded here rather than fixed here because the fix is in ween32's
 * message queue; see the note on the channel and docs/testing.md. */
static int parse(const char *t, struct rp_step *seq, int max)
{
    int n = 0;
    while (*t && n < max) {
        char name[16];
        int op, i, a = 0, b = 0;
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
            if (!strcmp(op_names[op], name))
                break;
        if (op == OP_N) {
            fprintf(stderr, "unknown operation \"%s\"\n", name);
            return -1;
        }
        seq[n].op = op; seq[n].a = a; seq[n].b = b;
        n++;
    }
    return n;
}

static void effect(HWND re, DWORD mask, DWORD bit, int on)
{
    CHARFORMATA cf;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = mask;
    cf.dwEffects = on ? bit : 0;
    SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void step(HWND re, const struct rp_step *s)
{
    char buf[512];
    CHARRANGE r;
    PARAFORMAT pf;
    switch (s->op) {
    case OP_TYPE: SendMessageA(re, WM_CHAR, (WPARAM)s->a, 1); break;
    case OP_ENTER:
        /* **Both messages, because a keyboard produces both and the two
         * implementations answer opposite ones.**
         *
         *     ween32           WM_CHAR CR inserts; WM_KEYDOWN VK_RETURN does not
         *     riched20 (wine)  WM_KEYDOWN VK_RETURN inserts; WM_CHAR CR does not
         *
         * Sam measured the second, I measured the first. **Neither single
         * form works on both**, so an executor that picks one silently loses
         * every paragraph break on one side -- and the diff would read as
         * *ours inserts paragraphs that WordPad does not*, which is a
         * spectacular finding that is entirely the harness.
         *
         * A real message loop delivers `WM_KEYDOWN` and then, via
         * `TranslateMessage`, `WM_CHAR`. Sending both is what a keystroke
         * *is*, and each side takes the one it acts on and ignores the
         * other -- one break, both sides, for the right reason rather than a
         * compensating one.
         *
         * **That the two act on different messages is itself a divergence**
         * and it is in known-differences.md; this makes the harness able to
         * run past it, not able to hide it. */
        SendMessageA(re, WM_KEYDOWN, VK_RETURN, 0);
        SendMessageA(re, WM_CHAR, '\r', 1);
        break;
    case OP_PASTE:
    case OP_REPLACE:
        rp_filler(buf, s->a > 400 ? 400 : s->a, s->b);
        SendMessageA(re, EM_REPLACESEL, TRUE, (LPARAM)buf);
        break;
    /* **lParam 0, not 1.** ween32's backend puts Shift in bit 0 of a
     * WM_KEYDOWN's lParam (src/richedit.c:3766, src/dialog.c:462) where
     * win32 keeps the repeat count. Passing 1 -- which is the natural thing
     * to write, and what WM_CHAR wants -- makes every arrow a *shift*-arrow.
     *
     * It cost a false finding within a minute of this program running:
     * `selall, end, type` left the selection at 0..3 and typing replaced the
     * document, which read exactly like "End does not collapse a selection"
     * and would have been reported as a divergence from riched20. The
     * control was right and the harness was holding Shift down.
     *
     * tests/monkey_test.c passes 1 as well, so its home/end/arrow operations
     * have been shift-modified since they were written. */
    case OP_BACK: SendMessageA(re, WM_KEYDOWN, VK_BACK, 0); break;
    case OP_DELETE: SendMessageA(re, WM_KEYDOWN, VK_DELETE, 0); break;
    case OP_SELECT:
        r.cpMin = s->a; r.cpMax = s->b;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
        break;
    case OP_SELALL: SendMessageA(re, EM_SETSEL, 0, -1); break;
    case OP_CLEAR:
        SendMessageA(re, EM_SETSEL, 0, -1);
        SendMessageA(re, WM_KEYDOWN, VK_DELETE, 0);
        break;
    case OP_HOME: SendMessageA(re, WM_KEYDOWN, VK_HOME, 0); break;
    case OP_END: SendMessageA(re, WM_KEYDOWN, VK_END, 0); break;
    case OP_UP: SendMessageA(re, WM_KEYDOWN, VK_UP, 0); break;
    case OP_DOWN: SendMessageA(re, WM_KEYDOWN, VK_DOWN, 0); break;
    case OP_LEFT: SendMessageA(re, WM_KEYDOWN, VK_LEFT, 0); break;
    case OP_RIGHT: SendMessageA(re, WM_KEYDOWN, VK_RIGHT, 0); break;
    case OP_BOLD: effect(re, CFM_BOLD, CFE_BOLD, s->a); break;
    case OP_ITALIC: effect(re, CFM_ITALIC, CFE_ITALIC, s->a); break;
    case OP_UNDER: effect(re, CFM_UNDERLINE, CFE_UNDERLINE, s->a); break;
    case OP_SIZE: {
        CHARFORMATA cf;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_SIZE;
        cf.yHeight = (LONG)s->a * 20;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        break;
    }
    case OP_ALIGN:
        memset(&pf, 0, sizeof pf); pf.cbSize = sizeof pf;
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = (WORD)(s->a == 1 ? PFA_CENTER
                                         : s->a == 2 ? PFA_RIGHT : PFA_LEFT);
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        break;
    case OP_BULLET:
        memset(&pf, 0, sizeof pf); pf.cbSize = sizeof pf;
        pf.dwMask = PFM_NUMBERING;
        pf.wNumbering = (WORD)(s->a ? PFN_BULLET : 0);
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        break;
    case OP_INDENT:
        memset(&pf, 0, sizeof pf); pf.cbSize = sizeof pf;
        pf.dwMask = PFM_STARTINDENT | PFM_OFFSET;
        pf.dxStartIndent = s->a;
        pf.dxOffset = s->b;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        break;
    /* **The right indent, which the language could not express at all.**
     * jd: *"the right ruler's cursor does nothing on the text"* -- and no
     * sequence could ask riched20 what it does with one, because there was
     * no operation for it. `dxRightIndent` has been written, read back,
     * printed and diffed all evening by three instruments and no character
     * ever moved because of it. */
    case OP_RINDENT:
        memset(&pf, 0, sizeof pf); pf.cbSize = sizeof pf;
        pf.dwMask = PFM_RIGHTINDENT;
        pf.dxRightIndent = s->a;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        break;
    case OP_RESIZE:
        SetWindowPos(re, NULL, 0, 0, 60 + s->a % 240, 30 + s->b % 120,
                     SWP_NOMOVE | SWP_NOZORDER);
        break;
    case OP_UNDO: SendMessageA(re, EM_UNDO, 0, 0); break;
    }
}

#endif
