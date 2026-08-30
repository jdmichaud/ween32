/* Two questions jd's reports raised, asked of riched20.
 *
 * jd: *"When we reach the bottom of the editor the cursor keeps going below
 * the window and becomes hidden. The scrollbar appears too late."* and
 * *"What about the fact that the right ruler's cursor does nothing on the
 * text?"*
 *
 * alice measured both in ours and named what neither could be fixed without:
 * **our fix must not invent a rule.** Three instruments print `dxRightIndent`
 * and none checks that a character moved because of it, and the scrollbar's
 * `iff` was dropped when the exactly-fits boundary turned out unmeasured.
 *
 * **1. riched20 wraps to `client width - dxRightIndent`, exactly.**
 *
 *     indent    break at    last x on line 1    client width - indent
 *        0         64            379                  408
 *      720         58            345                  360
 *     1440         52            309                  312
 *     2880         35            209                  216
 *
 * Monotonic, and the last two columns agree to within the width of the
 * character that did not fit. So the setting is not merely stored.
 *
 * **2. The bar appears when the content needs MORE than the client height,
 * not when it fills it.**
 *
 *     18 lines   last line bottom 289, client 289   WS_VSCROLL not set
 *     19 lines                                      WS_VSCROLL SET
 *
 * Exactly-fitting does not raise it. That is the boundary the monkey's
 * invariant lost its `iff` over, and it is the one jd walked into.
 *
 * **Two faults of my own are recorded here rather than tidied away.**
 *
 * The first run used `%-34s` and `%-5d`, which `guestcrt.h` does not
 * implement -- it has no left-justify and no width for strings -- and an
 * unhandled conversion does not consume its argument, so the format printed
 * itself and every number after it came from the wrong place. **That caveat
 * is written in that header, by me, and I used the unsupported form anyway.**
 *
 * The second run said the scrollbar never appears in twenty-one lines. It
 * cannot: `0x550081C4 & WS_VSCROLL` is zero, so `rp_create`'s style word
 * builds a control that has no bar to raise. **That reading was a fact about
 * the style and would have been reported as one about riched20.** The
 * scrollbar section adds the bit; the wrapping section deliberately does not,
 * since it is asking about the harness's own control.
 *
 * **And one thing measured but not explained**: the control is asked for
 * 280x160 and its client comes back 408x289, which is the host window's
 * client area. Nothing here resizes it. It matters because a wrap comparison
 * between two sides that disagree about the control's width would differ for
 * that reason and look like a layout bug.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o rulerbar.obj rulerbar.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o rulerbar.exe rulerbar.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py rulerbar.exe
 *   Z:\rulerbar.exe
 */
#include <windows.h>
#include <richedit.h>
#include "guestcrt.h"

static void pump(int ms)
{
    MSG m; DWORD e = GetTickCount() + ms;
    while (GetTickCount() < e) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
        Sleep(2);
    }
}

static HWND host;

/* rp_create's control, spelled out here so this probe does not depend on a
 * header that is still moving. Style word, ex style, size and default
 * character format are WordPad's, and EM_SETTARGETDEVICE(0,0) is the
 * wrap-to-window that rp_create now sends. */
static HWND mk(void)
{
    CHARFORMATA d;
    HWND re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "", (DWORD)0x550081C4,
                              0, 0, 280, 160, host, NULL, NULL, NULL);
    memset(&d, 0, sizeof d);
    d.cbSize = sizeof d;
    d.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    d.dwEffects = 0;
    d.yHeight = 200;
    d.szFaceName[0]='A'; d.szFaceName[1]='r'; d.szFaceName[2]='i';
    d.szFaceName[3]='a'; d.szFaceName[4]='l'; d.szFaceName[5]=0;
    SendMessageA(re, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&d);
    SendMessageA(re, EM_SETTARGETDEVICE, 0, 0);
    SetFocus(re);
    pump(150);
    return re;
}

/* The same control with WS_VSCROLL added, for the scrollbar question only. */
static HWND mk_scroll(void)
{
    CHARFORMATA d;
    HWND re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "",
                              (DWORD)0x550081C4 | WS_VSCROLL,
                              0, 0, 280, 160, host, NULL, NULL, NULL);
    memset(&d, 0, sizeof d);
    d.cbSize = sizeof d;
    d.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    d.dwEffects = 0;
    d.yHeight = 200;
    d.szFaceName[0]='A'; d.szFaceName[1]='r'; d.szFaceName[2]='i';
    d.szFaceName[3]='a'; d.szFaceName[4]='l'; d.szFaceName[5]=0;
    SendMessageA(re, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&d);
    SendMessageA(re, EM_SETTARGETDEVICE, 0, 0);
    SetFocus(re);
    pump(150);
    return re;
}

static void set_right(HWND re, long twips)
{
    PARAFORMAT pf;
    memset(&pf, 0, sizeof pf);
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_RIGHTINDENT;
    pf.dxRightIndent = twips;
    SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    pump(200);
}

/* Where the first line breaks, and how far right the last character of it
 * reaches. **The break index is the question; the x is the corroboration** --
 * a right indent that narrows the wrap width moves the break earlier AND
 * pulls the right edge in, and one that does nothing moves neither. */
static void wrapinfo(HWND re, const char *what)
{
    POINTL p0, p;
    CHARRANGE r;
    int at, brk = -1, len, maxx = 0;
    r.cpMin = 0; r.cpMax = -1;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
    memset(&r, 0, sizeof r);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&r);
    len = (int)r.cpMax;
    p0.x = p0.y = 0x7BAD;
    SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p0, (LPARAM)0);
    for (at = 1; at < len; at++) {
        p.x = p.y = 0x7BAD;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)at);
        if (p.y != p0.y) { brk = at; break; }
        if ((int)p.x > maxx) maxx = (int)p.x;
    }
    /* **Only the conversions guestcrt.h implements.** It has no left-justify
     * and no width for %s, and an unhandled conversion does not consume its
     * argument -- so `%-34s` printed itself and every number after it came
     * from the wrong place. The caveat is written in that header and I used
     * the unsupported form anyway; this is the same mistake the audit exists
     * to catch, made against my own note. */
    fprintf(GUEST_STREAM, "  %s: break at %d, last x on line 1 %d, lines %d\n",
            what, brk, maxx, (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0));
}

void WinMainCRTStartup(void)
{
    WNDCLASSA wc;
    HWND re;
    static const char *L =
        "the quick brown fox jumps over the lazy dog and the heron waits here "
        "and more after that as well so that it certainly has to wrap somewhere";
    LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "rb";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "rb", "rb", WS_OVERLAPPEDWINDOW, 40, 40, 420, 320,
                           NULL, NULL, wc.hInstance, NULL);
    g_out = CreateFileA("Z:\\rulerbar.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ShowWindow(host, SW_SHOW); UpdateWindow(host); pump(300);

    fprintf(GUEST_STREAM, "== does riched20 wrap to dxRightIndent? ==\n");
    {
        RECT c;
        HWND t = mk();
        GetClientRect(t, &c);
        fprintf(GUEST_STREAM, "  asked for 280x160; client is %ld x %ld\n",
                (long)c.right, (long)c.bottom);
        DestroyWindow(t);
    }
    {
        static const long v[] = { 0, 720, 1440, 2880 };
        int i;
        for (i = 0; i < 4; i++) {
            char lbl[64];
            re = mk();
            SendMessageA(re, WM_SETTEXT, 0, (LPARAM)L);
            pump(200);
            set_right(re, v[i]);
            wsprintfA(lbl, "dxRightIndent %ld twips = %ldpx", v[i], v[i] * 96 / 1440);
            wrapinfo(re, lbl);
            DestroyWindow(re);
        }
    }

    fprintf(GUEST_STREAM, "\n== when does the vertical scrollbar appear? ==\n");
    {
        RECT c;
        int n, prev_lines = 0;
        /* **WS_VSCROLL, because rp_create's style word does not have it.**
         * 0x550081C4 & 0x00200000 is zero, so the control cannot raise a bar
         * however much text it holds -- twenty-one lines gave WS_VSCROLL
         * never SET, which is a fact about the style and not about riched20.
         * That is the measurement this probe nearly reported. */
        re = mk_scroll();
        GetClientRect(re, &c);
        fprintf(GUEST_STREAM, "  client %ld x %ld px, style has WS_VSCROLL\n",
                (long)c.right, (long)c.bottom);
        for (n = 1; n <= 20; n++) {
            POINTL p;
            LONG st;
            SendMessageA(re, WM_KEYDOWN, VK_RETURN, 0);
            SendMessageA(re, WM_CHAR, '\r', 1);
            SendMessageA(re, WM_CHAR, 'x', 1);
            pump(120);
            st = GetWindowLongA(re, GWL_STYLE);
            p.x = p.y = 0x7BAD;
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p,
                         (LPARAM)SendMessageA(re, EM_LINEINDEX,
                                              (WPARAM)(SendMessageA(re, EM_GETLINECOUNT, 0, 0) - 1), 0));
            fprintf(GUEST_STREAM, "  lines %d, last line top y %ld, bottom %ld,"
                                  " WS_VSCROLL %s\n",
                    (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0), (long)p.y,
                    (long)p.y + 16, (st & WS_VSCROLL) ? "SET" : "-");
            prev_lines = n;
            if (st & WS_VSCROLL) break;
        }
        (void)prev_lines;
        DestroyWindow(re);
    }
    CloseHandle(g_out);
    ExitProcess(0);
}
