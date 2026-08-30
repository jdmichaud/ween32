/* The document as a value, serialised identically on both sides.
 *
 * **Pixels are the wrong comparison for this.** ween32 has two strikes and
 * the machine has seven faces, so a rendered diff would be thousands of
 * pixels of known difference with any real finding buried in it. A document
 * is a value; compare values.
 *
 * One field per line, so a diff points at a field rather than an offset:
 *
 *     len 5
 *     text 61 62 0d 0a 63        hex -- CR and LF visible and unambiguous
 *     sel 2 4
 *     char 0..1 B-- Arial 200    collapsed runs: first..last, effects,
 *     char 2..4 --- Arial 200    face, twips
 *     para 0..0 align 0 ind 0 0 0 num 0 tabs 0
 *     line 0 at 0
 *     line 1 at 4
 *
 * **Runs are collapsed and the collapse is part of the contract**, not a
 * convenience: it must happen identically on both sides or it could hide a
 * difference rather than shorten one. Two adjacent characters merge only
 * when every recorded field is equal.
 *
 * **The face recorded is the one that was asked for, not the one resolved.**
 * ween32 substitutes; riched20 has the font. Recording the resolution would
 * make almost every line differ for a reason that is not a bug -- and the
 * point of a differential test is that a difference is worth reading.
 *
 * ## What this asks of the platform
 *
 * **`fprintf` and nothing else.** No `malloc`, no `str*`: the guest side
 * compiles `-nostdlib` because linking the CRT pulls in
 * `api-ms-win-crt-*.dll`, which is the Universal CRT and **is not present on
 * Windows 2000** -- a load-time failure with no useful message, and no older
 * CRT to fall back to in this toolchain. Sam found that on this side of the
 * boot rather than on the machine.
 *
 * So the buffer is static and the two string helpers are here. That leaves
 * one function for the guest's shim to provide instead of six, which is the
 * difference between a shim and a port.
 */
#ifndef DUMP_H
#define DUMP_H

#include <stdio.h>

/* Static rather than allocated, and hand-rolled rather than <string.h>: see
 * the note above about the guest having no CRT. Sized for the longest
 * sequence the language can express, since a paragraph break comes back as
 * two bytes. */
static char dump_text[16384];

static void dump_face(char *out, const char *in)
{
    int i;
    for (i = 0; i < 31 && in[i]; i++)
        out[i] = in[i];
    out[i] = 0;
}

static int dump_same(const char *a, const char *b)
{
    int i;
    for (i = 0; a[i] && a[i] == b[i]; i++)
        ;
    return a[i] == b[i];
}

struct dump_char { unsigned fx; char face[32]; long size; };
struct dump_para {
    long align, start_indent, offset, right_indent, numbering, tabs;
};

static void dump_open(FILE *f, HWND re)
{
    CHARRANGE all, keep;
    int len, i, n;
    memset(&keep, 0, sizeof keep);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&keep);

    /* **The control's own character count**, which is not the length of the
     * string it hands out: a paragraph break is one character and comes back
     * as CR LF. Indexing by strlen walks off the end, and the control answers
     * a query past the end from the armed insertion format rather than
     * failing -- a phantom character carrying a format nothing recorded.
     * `cpMax` of -1 resolves rather than stores, which is how the real length
     * is asked for. */
    all.cpMin = 0;
    all.cpMax = -1;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&all);
    memset(&all, 0, sizeof all);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&all);
    len = (int)all.cpMax;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&keep);

    /* **The control's own size, because two controls of different widths
     * wrap differently and that is not a fact about either program.**
     *
     * Sam asked for this after finding his control was 408 wide where ours
     * was 280 -- `WS_MAXIMIZE` in WordPad's style word, which ween32 does
     * not honour on a child. Until the two agree, every wrap comparison
     * differs for a reason that is in neither control.
     *
     * **It goes in the dump rather than being asserted out of band** so the
     * differ sees it: a size difference is then a NEW line in every run that
     * has one, instead of an assumption two people have to remember to
     * check. It is the state the rest of the dump is relative to. */
    {
        RECT cr;
        GetClientRect(re, &cr);
        fprintf(f, "client %ld %ld\n", (long)cr.right, (long)cr.bottom);
    }
    fprintf(f, "len %d\n", len);

    GetWindowTextA(re, dump_text, (int)sizeof dump_text - 1);
    fprintf(f, "text");
    for (i = 0; dump_text[i]; i++)
        fprintf(f, " %02x", (unsigned char)dump_text[i]);
    fprintf(f, "\n");

    fprintf(f, "sel %ld %ld\n", (long)keep.cpMin, (long)keep.cpMax);

    /* **What the next typed character would carry**, which is not in any of
     * the lines above. A sequence ending in `bold:1` on an empty selection
     * *arms* a format: it changes no character, so two controls can dump
     * identically here and behave differently on the very next keystroke.
     *
     * That is the boundary jd found by hand -- style, then type, then undo --
     * and it is the state a shrinker is most likely to shrink *to*, since it
     * is reached in two steps. Sam asked for it; it costs no extra movement,
     * because `EM_GETCHARFORMAT SCF_SELECTION` with the selection left as it
     * stands is already the question. */
    {
        CHARFORMATA cf;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        fprintf(f, "next %c%c%c %s %ld\n",
                (cf.dwEffects & CFE_BOLD) ? 'B' : '-',
                (cf.dwEffects & CFE_ITALIC) ? 'I' : '-',
                (cf.dwEffects & CFE_UNDERLINE) ? 'U' : '-',
                cf.szFaceName, (long)cf.yHeight);
    }

    /* characters, collapsed */
    for (i = 0; i < len; ) {
        struct dump_char a, b;
        CHARFORMATA cf;
        CHARRANGE r;
        int j;
        r.cpMin = i; r.cpMax = i + 1;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
        memset(&cf, 0, sizeof cf); cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        a.fx = (unsigned)cf.dwEffects;
        dump_face(a.face, cf.szFaceName);
        a.size = (long)cf.yHeight;
        for (j = i + 1; j < len; j++) {
            r.cpMin = j; r.cpMax = j + 1;
            SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
            memset(&cf, 0, sizeof cf); cf.cbSize = sizeof cf;
            SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            b.fx = (unsigned)cf.dwEffects;
            dump_face(b.face, cf.szFaceName);
            b.size = (long)cf.yHeight;
            if (b.fx != a.fx || b.size != a.size ||
                !dump_same(b.face, a.face))
                break;
        }
        fprintf(f, "char %d..%d %c%c%c %s %ld\n", i, j - 1,
                (a.fx & CFE_BOLD) ? 'B' : '-',
                (a.fx & CFE_ITALIC) ? 'I' : '-',
                (a.fx & CFE_UNDERLINE) ? 'U' : '-', a.face, a.size);
        i = j;
    }
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&keep);

    /* paragraphs, collapsed the same way */
    for (i = 0; i < (len > 0 ? len : 1); ) {
        struct dump_para a, b;
        PARAFORMAT pf;
        CHARRANGE r;
        int j;
        r.cpMin = i; r.cpMax = i + 1;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
        memset(&pf, 0, sizeof pf); pf.cbSize = sizeof pf;
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        a.align = pf.wAlignment; a.start_indent = pf.dxStartIndent;
        a.offset = pf.dxOffset; a.right_indent = pf.dxRightIndent;
        a.numbering = pf.wNumbering; a.tabs = pf.cTabCount;
        for (j = i + 1; j < len; j++) {
            r.cpMin = j; r.cpMax = j + 1;
            SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
            memset(&pf, 0, sizeof pf); pf.cbSize = sizeof pf;
            SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
            b.align = pf.wAlignment; b.start_indent = pf.dxStartIndent;
            b.offset = pf.dxOffset; b.right_indent = pf.dxRightIndent;
            b.numbering = pf.wNumbering; b.tabs = pf.cTabCount;
            if (memcmp(&a, &b, sizeof a))
                break;
        }
        fprintf(f, "para %d..%d align %ld ind %ld %ld %ld num %ld tabs %ld\n",
                i, j - 1, a.align, a.start_indent, a.offset, a.right_indent,
                a.numbering, a.tabs);
        i = j;
        if (len == 0)
            break;
    }
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&keep);

    /* **Where a line ends, not only where it begins.**
     *
     * jd: *"the right ruler's cursor does nothing on the text"* -- and no
     * comparison could have seen that, because every instrument in either
     * repository asserts on where text *starts*. A right indent moves where
     * a line ENDS, so a setting that is stored, reported and ignored passed
     * all of them.
     *
     * The length comes from the control rather than from the next line's
     * start, so the last line has one too. */
    n = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0);
    for (i = 0; i < n; i++) {
        long at = (long)SendMessageA(re, EM_LINEINDEX, (WPARAM)i, 0);
        fprintf(f, "line %d at %ld len %ld\n", i, at,
                (long)SendMessageA(re, EM_LINELENGTH, (WPARAM)at, 0));
    }

    /* **The scrollbar, whose timing was outside the contract entirely.**
     *
     * jd: *"the scrollbar appears too late"* -- and the dump could not have
     * said so, because it recorded nothing about the bar at all. The
     * monkey's invariant lost its *iff* when the exactly-fits boundary was
     * found to be unmeasured; **this is the field a machine run answers it
     * with**, since a sequence that fills the view step by step now records
     * where the bar came up. */
    fprintf(f, "vscroll %d\n",
            (GetWindowLongA(re, GWL_STYLE) & WS_VSCROLL) ? 1 : 0);
}

#endif
