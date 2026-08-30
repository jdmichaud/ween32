/* Replay a sequence against ween32's rich edit and dump the document.
 *
 *     tests/replay_test 'type:97:0,bold:1:0,type:98:0'   > ours.txt
 *
 * The other half of this runs the same sequence against a real RichEdit20W
 * on the guest and writes the same dump; the two are diffed. **That turns
 * "ours agrees with bob's model of a rich edit" into "ours agrees with
 * WordPad"**, which is the project's premise and what jd asked for.
 *
 * The language and the serialiser are in tools/vm/replay.h and
 * tools/vm/dump.h so that both sides compile the *same* code rather than two
 * readings of one description -- which is the mistake a written contract
 * invites, and it would blame the program for a disagreement between two
 * implementations of the contract.
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ween_internal.h"
#include <ween32.h>

#include "../tools/vm/replay.h"
#include "../tools/vm/dump.h"

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
    case OP_ENTER: SendMessageA(re, WM_CHAR, '\r', 1); break;
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
    case OP_RESIZE:
        SetWindowPos(re, NULL, 0, 0, 60 + s->a % 240, 30 + s->b % 120,
                     SWP_NOMOVE | SWP_NOZORDER);
        break;
    case OP_UNDO: SendMessageA(re, EM_UNDO, 0, 0); break;
    }
}

static LRESULT CALLBACK host_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

int main(int argc, char **argv)
{
    WNDCLASSA wc;
    struct rp_step seq[4096];
    int n, i;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <sequence>\n", argv[0]);
        return 2;
    }
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "replayhost";
    RegisterClassA(&wc);
    HWND host = CreateWindowExA(0, "replayhost", "h", WS_POPUP | WS_VISIBLE,
                                0, 0, 400, 300, NULL, NULL, NULL, NULL);
    /* **WordPad's own style word and its startup default.** The control
     * under test has to be the one jd drives, or a difference could be the
     * configuration rather than the program. */
    HWND re = CreateWindowExA(0x00000210, RICHEDIT_CLASSA, "",
                              (DWORD)0x550081C4, 0, 0, 280, 160, host, NULL,
                              NULL, NULL);
    CHARFORMATA d;
    memset(&d, 0, sizeof d);
    d.cbSize = sizeof d;
    d.dwMask = CFM_FACE | CFM_SIZE;
    d.yHeight = 200;
    strcpy(d.szFaceName, "Arial");
    SendMessageA(re, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&d);
    SetFocus(re);

    n = parse(argv[1], seq, 4096);
    if (n < 0)
        return 2;
    for (i = 0; i < n; i++)
        step(re, &seq[i]);
    dump_open(stdout, re);
    return 0;
}
