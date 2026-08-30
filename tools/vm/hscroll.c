/* Horizontal scrolling in riched20, with wrapping off -- the state nobody has
 * looked at.
 *
 * `src/richedit.c:1036` records that WS_HSCROLL is set in neither of the two
 * readings of the machine's editor, and both of those were taken of a
 * *wrapping* editor, where no horizontal bar is expected on either side. It
 * reads as though riched20 never puts one up. **The only state where the
 * question exists is No wrap**, and No wrap is `EM_SETTARGETDEVICE(0, 1440)`
 * (wordpad's src/main.zig:645).
 *
 * alice measured ween32's side of it: in No wrap the last character of 500
 * w's sits at x 3993 in a 752px client, WS_HSCROLL never appears and
 * WM_HSCROLL does nothing -- four fifths of the line unreachable. This asks
 * the machine the same questions before anything is built, because none of
 * the numbers are guessable: the vertical side's page turned out to be a
 * screenful *less one line*, and there is no reason the horizontal one must
 * match it.
 *
 * **The vertical axis is measured first and it is the control**, not padding:
 * its answers are already known from `reference/probe/window.txt` and the
 * three states in richedit.c -- clear when empty, WS_VSCROLL set when the
 * text overflows, clear again when it is deleted. If this probe cannot
 * reproduce that, then whatever it says about the horizontal axis is a fact
 * about the probe.
 *
 * **The first run of it did not**, and the control earned its place on its
 * first outing: 61 lines in a 529px client and no bar, on a control built
 * from the style word this repository has quoted all day. `barwhy.c` found
 * why, and it changes what the word means. **riched20 *lowers and raises*
 * WS_VSCROLL, and only on a control that was created with it**:
 *
 *     created without WS_VSCROLL   empty 550081C4  61 lines 550081C4  no bar
 *     created with    WS_VSCROLL   empty 550081C4  61 lines 552081C4
 *
 * The bit is a *permission*, not a request -- and `550081C4` for an empty
 * document is what riched20 left behind after taking the bar off, not what
 * WordPad asked for. So this probe creates with both scroll styles, and the
 * horizontal question becomes a sharp one: **if WordPad's editor is created
 * with WS_HSCROLL too, the permission is already there and the bar should
 * come up in No wrap.**
 *
 * **And the arrow step is measured at two fonts on purpose.** One font cannot
 * tell "a constant number of pixels" from "one average character width" --
 * the two rules agree everywhere a single sample can look. Arial 10 is
 * WordPad's own; Courier New 10 is there to separate them.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o hscroll.obj hscroll.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o hscroll.exe hscroll.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py hscroll.exe
 *   Z:\hscroll.exe        -> Z:\hscroll.txt
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

static HWND host;

/* WordPad's own editor, style word and all -- **plus the two scroll styles**,
 * which `barwhy.c` says have to be there at creation or riched20 never
 * manages a bar at all. 0x210 is WS_EX_CLIENTEDGE with WS_EX_ACCEPTFILES;
 * 0x550081C4 carries ES_MULTILINE, ES_AUTOVSCROLL, ES_AUTOHSCROLL,
 * ES_NOHIDESEL and ES_SELECTIONBAR. */
#define WP_STYLE ((DWORD)(0x550081C4u | (unsigned)WS_VSCROLL | (unsigned)WS_HSCROLL))

static HWND make(int w, int h)
{
    HWND re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "", WP_STYLE,
                              0, 0, w, h, host, NULL, NULL, NULL);
    SetFocus(re);
    pump(150);
    return re;
}

static void set_font(HWND re, const char *face, int pts)
{
    CHARFORMATA cf;
    int i;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_FACE | CFM_SIZE;
    cf.yHeight = pts * 20;
    for (i = 0; face[i] && i < LF_FACESIZE - 1; i++)
        cf.szFaceName[i] = face[i];
    SendMessageA(re, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);
    pump(150);
}

static LONG style_of(HWND re) { return GetWindowLongA(re, GWL_STYLE); }

static void bars(HWND re, const char *what)
{
    LONG s = style_of(re);
    fprintf(GUEST_STREAM, "  %s  style %08x  vscroll %s  hscroll %s\n", what,
            (unsigned)s, (s & WS_VSCROLL) ? "SET  " : "clear",
            (s & WS_HSCROLL) ? "SET" : "clear");
}

static void info(HWND re, int which, const char *what)
{
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(re, which, &si)) {
        fprintf(GUEST_STREAM, "  %s  GetScrollInfo failed\n", what);
        return;
    }
    fprintf(GUEST_STREAM, "  %s  min %d max %d page %d pos %d\n", what,
            (int)si.nMin, (int)si.nMax, (int)si.nPage, (int)si.nPos);
}

/* Where character `i` sits in the client. riched20 takes a POINTL* in wParam
 * and the index in lParam -- the EDIT's packed return value is the other
 * class's convention (share-sam/re1.txt). An unscrolled control answers x 1
 * for character 0, which is what says this reading is the right one. */
static int xof(HWND re, int i)
{
    POINTL p;
    p.x = p.y = 0;
    SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)(UINT_PTR)i);
    return (int)p.x;
}

static int yof(HWND re, int i)
{
    POINTL p;
    p.x = p.y = 0;
    SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)(UINT_PTR)i);
    return (int)p.y;
}

/* One scroll command, and what the view did about it. The view's movement is
 * read off character 0 rather than off nPos, because nPos is what the bar
 * says and the pixels are what the user gets -- and on the vertical axis
 * those are in different units already. */
static void step_h(HWND re, int code, const char *name)
{
    int before = xof(re, 0), after, pos_b, pos_a;
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_POS;
    GetScrollInfo(re, SB_HORZ, &si);
    pos_b = (int)si.nPos;
    SendMessageA(re, WM_HSCROLL, MAKEWPARAM(code, 0), 0);
    pump(120);
    after = xof(re, 0);
    GetScrollInfo(re, SB_HORZ, &si);
    pos_a = (int)si.nPos;
    fprintf(GUEST_STREAM, "  %s  x0 %d -> %d  moved %d px   pos %d -> %d\n",
            name, before, after, before - after, pos_b, pos_a);
}

static void step_v(HWND re, int code, const char *name)
{
    int before = yof(re, 0), after, pos_b, pos_a;
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_POS;
    GetScrollInfo(re, SB_VERT, &si);
    pos_b = (int)si.nPos;
    SendMessageA(re, WM_VSCROLL, MAKEWPARAM(code, 0), 0);
    pump(120);
    after = yof(re, 0);
    GetScrollInfo(re, SB_VERT, &si);
    pos_a = (int)si.nPos;
    fprintf(GUEST_STREAM, "  %s  y0 %d -> %d  moved %d px   pos %d -> %d\n",
            name, before, after, before - after, pos_b, pos_a);
}

static char big[1024];
static char many[2048];
static char both[2048];


/* The page and the arrow against the client width.
 *
 * At one width, "the client less ten" and every rival rule that happens to
 * pass through 778 at 788 are the same number, and a rule fitted to one
 * sample cannot fail. Three widths, and the vertical bar taken up beside the
 * horizontal one so the client changes for a reason other than the call.
 *
 * **Predicted before running: page = client - 10 at every width, and the
 * arrow stays 7.** Written down here rather than afterwards, because a rule
 * read off the numbers and a rule confirmed by them look identical in a
 * report. */
static void widths(int w, int tall)
{
    HWND re = make(756, 382);
    RECT cr;
    int arrow, page, before;

    set_font(re, "Arial", 10);
    SendMessageA(re, EM_SETTARGETDEVICE, 0, (LPARAM)1440);
    pump(200);
    SetWindowPos(re, NULL, 0, 0, w, 382, SWP_NOZORDER | SWP_NOACTIVATE);
    pump(300);
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)(tall ? both : big));
    pump(500);
    GetClientRect(re, &cr);
    SendMessageA(re, WM_HSCROLL, MAKEWPARAM(SB_LEFT, 0), 0);
    pump(150);

    before = xof(re, 0);
    SendMessageA(re, WM_HSCROLL, MAKEWPARAM(SB_LINERIGHT, 0), 0);
    pump(120);
    arrow = before - xof(re, 0);

    before = xof(re, 0);
    SendMessageA(re, WM_HSCROLL, MAKEWPARAM(SB_PAGERIGHT, 0), 0);
    pump(120);
    page = before - xof(re, 0);

    {
        SCROLLINFO si;
        LONG st = style_of(re);
        memset(&si, 0, sizeof si);
        si.cbSize = sizeof si;
        si.fMask = SIF_ALL;
        GetScrollInfo(re, SB_HORZ, &si);
        fprintf(GUEST_STREAM,
                "  asked %d  client %d  %s  arrow %d  page moved %d  "
                "nPage %d  nMax %d  client-10 %d\n",
                w, (int)cr.right, (st & WS_VSCROLL) ? "both bars" : "one bar  ",
                arrow, page, (int)si.nPage, (int)si.nMax, (int)cr.right - 10);
    }
    DestroyWindow(re);
}

/* The horizontal half, at one font. Everything after the control. */
static void run_h(const char *face, int pts, int w, int h)
{
    HWND re = make(w, h);
    RECT cr;
    int n;

    set_font(re, face, pts);
    GetClientRect(re, &cr);
    fprintf(GUEST_STREAM, "\n== No wrap, %s %d, client %d x %d ==\n", face,
            pts, (int)(cr.right - cr.left), (int)(cr.bottom - cr.top));

    /* wordpad's No wrap: a width with no device means do not break. */
    SendMessageA(re, EM_SETTARGETDEVICE, 0, (LPARAM)1440);
    pump(200);
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM) "short");
    pump(200);
    bars(re, "a line that fits        ");

    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)big);
    pump(400);
    n = (int)SendMessageA(re, WM_GETTEXTLENGTH, 0, 0);
    fprintf(GUEST_STREAM, "  500 w's                  lines %d   len %d\n",
            (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0), n);
    bars(re, "                        ");
    fprintf(GUEST_STREAM, "  x of char 0 %d   x of the last char %d\n",
            xof(re, 0), xof(re, n - 1));
    info(re, SB_HORZ, "horizontal              ");

    /* Does the view follow the caret when the text is *typed* rather than
     * set? That is ES_AUTOHSCROLL's half of the question and it is the one
     * jd meets: WM_SETTEXT leaves the selection at 0 and need not scroll. */
    SendMessageA(re, EM_SETSEL, (WPARAM)n, (LPARAM)n);
    pump(150);
    SendMessageA(re, WM_CHAR, (WPARAM)'X', 1);
    pump(300);
    fprintf(GUEST_STREAM, "  after typing one more    x of char 0 %d\n",
            xof(re, 0));
    info(re, SB_HORZ, "                        ");

    /* Home, so the steps below start from a known place. */
    SendMessageA(re, WM_HSCROLL, MAKEWPARAM(SB_LEFT, 0), 0);
    pump(200);
    fprintf(GUEST_STREAM, "  SB_LEFT                  x of char 0 %d\n",
            xof(re, 0));

    step_h(re, SB_LINERIGHT, "SB_LINERIGHT (an arrow) ");
    step_h(re, SB_LINERIGHT, "SB_LINERIGHT again      ");
    step_h(re, SB_PAGERIGHT, "SB_PAGERIGHT (the track)");
    step_h(re, SB_PAGERIGHT, "SB_PAGERIGHT again      ");
    step_h(re, SB_LINELEFT,  "SB_LINELEFT             ");
    step_h(re, SB_PAGELEFT,  "SB_PAGELEFT             ");
    step_h(re, SB_RIGHT,     "SB_RIGHT (the end)      ");
    step_h(re, SB_LEFT,      "SB_LEFT (the start)     ");

    /* Third state: the overflow removed. The vertical bar comes back off; the
     * question is whether this one does. */
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM) "short");
    pump(400);
    bars(re, "the long line deleted   ");

    DestroyWindow(re);
}

void WinMainCRTStartup(void)
{
    WNDCLASSA wc;
    HWND re;
    RECT cr;
    int i, n;

    LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "hs";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "hs", "hs", WS_OVERLAPPEDWINDOW, 8, 8, 800, 560,
                           NULL, NULL, wc.hInstance, NULL);
    g_out = CreateFileA("Z:\\hscroll.txt", GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, 0, NULL);
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    pump(400);

    for (i = 0; i < 500; i++)
        big[i] = 'w';
    big[500] = 0;
    for (i = 0, n = 0; i < 60; i++) {
        many[n++] = 'a';
        many[n++] = 'b';
        many[n++] = '\r';
        many[n++] = '\n';
    }
    many[n] = 0;
    for (i = 0; i < 300; i++)
        both[i] = 'w';
    n = 300;
    for (i = 0; i < 60; i++) {
        both[n++] = '\r';
        both[n++] = '\n';
        both[n++] = 'a';
    }
    both[n] = 0;

    /* ---- the control: the vertical axis, whose answers are already known ---- */
    fprintf(GUEST_STREAM, "== the control: WS_VSCROLL, already known ==\n");
    re = make(756, 382);
    set_font(re, "Arial", 10);
    GetClientRect(re, &cr);
    fprintf(GUEST_STREAM, "  client %d x %d\n", (int)(cr.right - cr.left),
            (int)(cr.bottom - cr.top));
    bars(re, "empty                   ");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)many);
    pump(400);
    fprintf(GUEST_STREAM, "  60 lines                 lines %d\n",
            (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0));
    bars(re, "                        ");
    info(re, SB_VERT, "vertical                ");
    step_v(re, SB_LINEDOWN, "SB_LINEDOWN (an arrow)  ");
    step_v(re, SB_PAGEDOWN, "SB_PAGEDOWN (the track) ");
    SendMessageA(re, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
    pump(200);
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM) "short");
    pump(400);
    bars(re, "emptied again           ");
    DestroyWindow(re);

    /* ---- and the same wrapping control given 500 w's, which is where the
     * two existing readings were taken: no horizontal bar is expected. ---- */
    fprintf(GUEST_STREAM, "\n== the control: wrapping, 500 w's ==\n");
    re = make(756, 382);
    set_font(re, "Arial", 10);
    SendMessageA(re, EM_SETTARGETDEVICE, 0, (LPARAM)0);
    pump(200);
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)big);
    pump(400);
    fprintf(GUEST_STREAM, "  lines %d\n",
            (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0));
    bars(re, "                        ");
    DestroyWindow(re);

    /* ---- the question ---- */
    run_h("Arial", 10, 756, 382);
    run_h("Courier New", 10, 756, 382);

    fprintf(GUEST_STREAM, "\n== the page and the arrow against the client ==\n");
    widths(760, 0);
    widths(600, 0);
    widths(400, 0);
    widths(760, 1);
    widths(400, 1);

    CloseHandle(g_out);
    ExitProcess(0);
}
