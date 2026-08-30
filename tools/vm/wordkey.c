/* What Ctrl+Left/Right and Tab actually do in riched20.
 *
 * jd: *"Ctrl+Right/Left (Ctrl+Shift+Right/Left) do not work as expected"*, and
 * Tab does nothing. Ours is measured: `VK_LEFT`/`VK_RIGHT` are absent from
 * `WM_KEYDOWN`'s `if (ctrl)` switch so they fall through to plain arrows, and
 * `VK_TAB` appears nowhere in `src/richedit.c`. **What the machine does
 * instead is not obvious**, and alice named four questions no amount of
 * reading answers:
 *
 *   - the next word's *start*, or the current word's *end*? WordPad and
 *     Notepad famously differ from each other here
 *   - at the end of a line, stop at the break or step over it?
 *   - does Tab insert a `\t`, or move to the next stop as a position -- and
 *     on a bulleted or indented paragraph, does it indent instead?
 *   - Ctrl+Shift+Right over a *backwards* selection: shrink, or flip?
 *
 * **The keys are pressed rather than posted.** riched20 asks `GetKeyState`
 * for the modifiers, and a `WM_KEYDOWN` synthesised with `SendMessage` sets
 * no key state at all -- so a posted Ctrl+Right would arrive as a plain
 * Right, which is *precisely the bug under investigation*. It would have
 * confirmed itself. `keybd_event` sets the real state and goes through the
 * queue the way a person does.
 *
 * **That trap is also why the control here is Ctrl+A.** A probe whose Ctrl is
 * silently not held reports "Ctrl+Right moves one character" -- true, and a
 * fact about the probe. Ctrl+A selecting the whole document proves the
 * modifier reached the control, through the same mechanism, in the same
 * window, immediately before the questions are asked.
 *
 * And the samples are chosen to be able to disagree: single spaces, a double
 * space, a comma, a hard break and a soft wrap. A rule fitted to
 * "alpha beta gamma delta" alone cannot fail, because every rival agrees on
 * evenly spaced lowercase words.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o wordkey.obj wordkey.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o wordkey.exe wordkey.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py wordkey.exe
 *   Z:\wordkey.exe        -> Z:\wordkey.txt
 */
#include <windows.h>
#include <richedit.h>
#include "guestcrt.h"

static void pump(int ms)
{
    MSG m;
    DWORD e = GetTickCount() + ms;
    while (GetTickCount() < e) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(2);
    }
}

static HWND host, re;

/* WordPad's style word, plus the two scroll permissions barwhy.c measured. */
#define WP_STYLE ((DWORD)(0x550081C4u | (unsigned)WS_VSCROLL | (unsigned)WS_HSCROLL))

/* A real key press. The modifiers go down before and up after, so
 * `GetKeyState` inside riched20 sees what a person's keyboard would say. */
static void key(int vk, int ctrl, int shift)
{
    if (ctrl)
        keybd_event(VK_CONTROL, 0, 0, 0);
    if (shift)
        keybd_event(VK_SHIFT, 0, 0, 0);
    pump(30);
    keybd_event((BYTE)vk, 0, 0, 0);
    pump(60);
    keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0);
    pump(30);
    if (shift)
        keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    if (ctrl)
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    pump(80);
}

static void sel_of(int *a, int *b)
{
    CHARRANGE cr;
    cr.cpMin = cr.cpMax = 0;
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
    *a = (int)cr.cpMin;
    *b = (int)cr.cpMax;
}

static void set_sel(int a, int b)
{
    CHARRANGE cr;
    cr.cpMin = a;
    cr.cpMax = b;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    pump(60);
}

static int textlen(void) { return (int)SendMessageA(re, WM_GETTEXTLENGTH, 0, 0); }

/* The document as bytes, so a tab shows up as 09 rather than as whitespace
 * nobody can see in a report. */
static void say_text(const char *what)
{
    char buf[256];
    int n, i;
    buf[0] = 0;
    SendMessageA(re, WM_GETTEXT, (WPARAM)sizeof buf, (LPARAM)buf);
    n = textlen();
    fprintf(GUEST_STREAM, "    %s len %d  text", what, n);
    for (i = 0; i < n && i < 40; i++)
        fprintf(GUEST_STREAM, " %02x", (unsigned)(unsigned char)buf[i]);
    fprintf(GUEST_STREAM, "\n");
}

static void set_text(const char *s)
{
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)s);
    pump(200);
}

/* Put the caret at `at`, press the key, print where the selection ended up. */
static void from(int at, int vk, int ctrl, int shift, const char *what)
{
    int a, b;
    set_sel(at, at);
    key(vk, ctrl, shift);
    sel_of(&a, &b);
    fprintf(GUEST_STREAM, "  %s  caret %d -> sel %d %d\n", what, at, a, b);
}

static void para_say(const char *what)
{
    PARAFORMAT pf;
    memset(&pf, 0, sizeof pf);
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_STARTINDENT | PFM_OFFSET | PFM_RIGHTINDENT | PFM_NUMBERING |
                PFM_TABSTOPS;
    SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    fprintf(GUEST_STREAM, "    %s  ind %d %d %d  num %d  tabs %d\n", what,
            (int)pf.dxStartIndent, (int)pf.dxOffset, (int)pf.dxRightIndent,
            (int)pf.wNumbering, (int)pf.cTabCount);
}

/* "alpha beta gamma delta" -- alice's own sample, so our side's table and
 * this one are the same question. Words start at 0, 6, 11, 17; len 22. */
static const char *A = "alpha beta gamma delta";
/* The one that can disagree: a double space, a comma, and a hard break.
 *   0 one   4 two   9 three(,)  16 four   21\r\n   23 five   28 six */
static const char *B = "one two  three, four\r\nfive six";

void WinMainCRTStartup(void)
{
    WNDCLASSA wc;
    CHARFORMATA cf;
    int a, b, i;

    LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "wk";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "wk", "wk", WS_OVERLAPPEDWINDOW, 8, 8, 400, 300,
                           NULL, NULL, wc.hInstance, NULL);
    g_out = CreateFileA("Z:\\wordkey.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        0, NULL);
    ShowWindow(host, SW_SHOW);
    SetForegroundWindow(host);
    UpdateWindow(host);
    pump(500);

    re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "", WP_STYLE, 0, 0, 280, 160,
                         host, NULL, NULL, NULL);
    SetWindowPos(re, NULL, 0, 0, 280, 160, SWP_NOZORDER | SWP_NOACTIVATE);
    SetFocus(re);
    pump(300);

    /* Arial 10 with the effects *named*, because riched20's default face is
     * System and System is bold -- naming the face alone leaves the weight
     * behind and lays out 11px per w where WordPad is 9. */
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    cf.yHeight = 200;
    for (i = 0; "Arial"[i]; i++)
        cf.szFaceName[i] = "Arial"[i];
    SendMessageA(re, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);
    SendMessageA(re, EM_SETTARGETDEVICE, 0, (LPARAM)0); /* wrap to window */
    pump(200);

    /* ---- the control: do keys arrive, and is Ctrl actually held? ---- */
    fprintf(GUEST_STREAM, "== the control ==\n");
    set_text(A);
    from(0, VK_RIGHT, 0, 0, "plain Right from 0      ");
    fprintf(GUEST_STREAM, "    (1 1 means keys reach the control)\n");
    set_sel(3, 3);
    key('A', 1, 0);
    sel_of(&a, &b);
    fprintf(GUEST_STREAM, "  Ctrl+A                   sel %d %d\n", a, b);
    fprintf(GUEST_STREAM, "    (0 %d means Ctrl is really held -- without this"
                          " every line below is void)\n",
            textlen());

    /* ---- Ctrl+Right ---- */
    fprintf(GUEST_STREAM, "\n== Ctrl+Right, \"%s\", words at 0 6 11 17, len 22 ==\n", A);
    set_text(A);
    for (i = 0; i < 6; i++) {
        key(VK_RIGHT, 1, 0);
        sel_of(&a, &b);
        fprintf(GUEST_STREAM, "  press %d                   sel %d %d\n", i + 1, a, b);
    }
    set_text(A);
    from(2, VK_RIGHT, 1, 0, "from 2, inside \"alpha\"  ");
    from(8, VK_RIGHT, 1, 0, "from 8, inside \"beta\"   ");
    from(17, VK_RIGHT, 1, 0, "from 17, the last word  ");
    from(21, VK_RIGHT, 1, 0, "from 21, one from the end");
    from(22, VK_RIGHT, 1, 0, "from 22, the very end   ");

    /* ---- Ctrl+Left ---- */
    fprintf(GUEST_STREAM, "\n== Ctrl+Left ==\n");
    set_text(A);
    set_sel(22, 22);
    for (i = 0; i < 5; i++) {
        key(VK_LEFT, 1, 0);
        sel_of(&a, &b);
        fprintf(GUEST_STREAM, "  press %d                   sel %d %d\n", i + 1, a, b);
    }
    set_text(A);
    from(8, VK_LEFT, 1, 0, "from 8, inside \"beta\"   ");
    from(6, VK_LEFT, 1, 0, "from 6, at a word start ");
    from(0, VK_LEFT, 1, 0, "from 0, the very start  ");

    /* ---- the sample that can disagree ---- */
    fprintf(GUEST_STREAM,
            "\n== \"one two  three, four\\r\\nfive six\" -- double space, comma,"
            " hard break ==\n");
    fprintf(GUEST_STREAM, "   words at 0 4 9 16, break at 20, then 22 27\n");
    set_text(B);
    set_sel(0, 0);
    for (i = 0; i < 8; i++) {
        key(VK_RIGHT, 1, 0);
        sel_of(&a, &b);
        fprintf(GUEST_STREAM, "  Ctrl+Right %d             sel %d %d\n", i + 1, a, b);
    }
    set_text(B);
    from(16, VK_RIGHT, 1, 0, "from 16, last word of line 1");
    from(19, VK_RIGHT, 1, 0, "from 19, inside it      ");

    /* ---- a soft wrap, which is a line break with no character in it ---- */
    fprintf(GUEST_STREAM, "\n== across a soft wrap (no character at the break) ==\n");
    set_text("aaa bbb ccc ddd eee fff ggg hhh iii jjj kkk lll mmm nnn ooo");
    fprintf(GUEST_STREAM, "  lines %d\n",
            (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0));
    fprintf(GUEST_STREAM, "  line 1 starts at %d\n",
            (int)SendMessageA(re, EM_LINEINDEX, 1, 0));
    /* Enough presses to actually reach the wrap: the first run stopped at 32
     * and the break is at 48, so it said nothing about the question it was
     * there to ask. An instrument that stops short of its own case looks
     * exactly like one that found nothing. */
    set_sel(0, 0);
    for (i = 0; i < 16; i++) {
        key(VK_RIGHT, 1, 0);
        sel_of(&a, &b);
        fprintf(GUEST_STREAM, "  Ctrl+Right %d             sel %d %d%s\n", i + 1,
                a, b, (a == 48) ? "   <- the wrap" : "");
    }

    /* ---- and the same walk backwards over the hard mark ----
     *
     * bob built the leftward rule from the rightward one by symmetry and said
     * in his source that symmetry is not a reading. It is not: the right-hand
     * rule stops *on* the mark, and nothing about that says the left-hand one
     * does rather than stepping past it to the last word of the line above. */
    fprintf(GUEST_STREAM, "\n== Ctrl+Left back across the hard mark ==\n");
    set_text(B);
    set_sel(29, 29);
    for (i = 0; i < 8; i++) {
        key(VK_LEFT, 1, 0);
        sel_of(&a, &b);
        fprintf(GUEST_STREAM, "  Ctrl+Left %d              sel %d %d%s\n", i + 1,
                a, b, (a == 20) ? "   <- the mark" : "");
    }
    set_text(B);
    from(21, VK_LEFT, 1, 0, "from 21, first word after");
    from(22, VK_LEFT, 1, 0, "from 22, inside \"five\"  ");
    from(20, VK_LEFT, 1, 0, "from 20, on the mark    ");

    /* ---- Ctrl+Shift, including over a backwards selection ---- */
    fprintf(GUEST_STREAM, "\n== Ctrl+Shift ==\n");
    set_text(A);
    set_sel(0, 0);
    for (i = 0; i < 3; i++) {
        key(VK_RIGHT, 1, 1);
        sel_of(&a, &b);
        fprintf(GUEST_STREAM, "  Ctrl+Shift+Right %d       sel %d %d\n", i + 1, a, b);
    }
    /* EM_EXSETSEL's active end is cpMax, so 11..6 is a selection made
     * leftwards and the caret is at 6. Does Ctrl+Shift+Right shrink it from
     * that end, or flip to extending the other one? */
    set_text(A);
    set_sel(11, 6);
    sel_of(&a, &b);
    fprintf(GUEST_STREAM, "  set 11..6, reads back      sel %d %d\n", a, b);
    key(VK_RIGHT, 1, 1);
    sel_of(&a, &b);
    fprintf(GUEST_STREAM, "  then Ctrl+Shift+Right      sel %d %d\n", a, b);
    key(VK_LEFT, 1, 1);
    sel_of(&a, &b);
    fprintf(GUEST_STREAM, "  then Ctrl+Shift+Left       sel %d %d\n", a, b);

    /* ---- Tab ---- */
    fprintf(GUEST_STREAM, "\n== Tab ==\n");
    set_text(A);
    set_sel(11, 11);
    para_say("before, caret mid-text  ");
    say_text("before                  ");
    key(VK_TAB, 0, 0);
    say_text("after Tab at 11         ");
    sel_of(&a, &b);
    fprintf(GUEST_STREAM, "    sel %d %d\n", a, b);
    para_say("after                   ");

    fprintf(GUEST_STREAM, "\n  -- at the very start of a paragraph --\n");
    set_text(A);
    set_sel(0, 0);
    key(VK_TAB, 0, 0);
    say_text("after Tab at 0          ");
    para_say("after                   ");

    fprintf(GUEST_STREAM, "\n  -- on a bulleted paragraph --\n");
    set_text(A);
    {
        PARAFORMAT pf;
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_NUMBERING;
        pf.wNumbering = PFN_BULLET;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        pump(200);
    }
    set_sel(0, 0);
    para_say("before                  ");
    key(VK_TAB, 0, 0);
    say_text("after Tab at 0          ");
    para_say("after                   ");

    fprintf(GUEST_STREAM, "\n  -- on an indented paragraph --\n");
    set_text(A);
    {
        PARAFORMAT pf;
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_STARTINDENT;
        pf.dxStartIndent = 720;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        pump(200);
    }
    set_sel(0, 0);
    para_say("before                  ");
    key(VK_TAB, 0, 0);
    say_text("after Tab at 0          ");
    para_say("after                   ");

    fprintf(GUEST_STREAM, "\n  -- and Shift+Tab on the same, for contrast --\n");
    key(VK_TAB, 0, 1);
    say_text("after Shift+Tab         ");
    para_say("after                   ");

    CloseHandle(g_out);
    ExitProcess(0);
}
