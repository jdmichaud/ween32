/* Why a bare riched20 control raises no scroll bar when the machine's WordPad
 * editor does.
 *
 * `docs/testing.md` records three states of the machine's editor as measured
 * fact -- empty `550081C4`, overflowing `552081C4`, emptied again
 * `550081C4`, and *not a latch*. `hscroll.c` put 61 lines into a control
 * built from the same style word and the bar never came up: the text scrolls
 * (SB_LINEDOWN moves it 16px) and `GetScrollInfo` says there is no bar to
 * scroll it with. **So that probe cannot be trusted about the horizontal
 * axis either**, and this asks which difference between the two accounts for
 * it before anything else is measured.
 *
 * Four candidates, each its own row:
 *
 *   1  the class. `reference/probe/window.txt` line 17 says the machine's is
 *      **RichEdit20W**; the probe made a RichEdit20A.
 *   2  the size. WordPad's editor is 760x386 inside a 760x491 client, so MFC
 *      resizes it after creation and it receives a WM_SIZE the probe's never
 *      gets. (0x01000000 is ES_SELECTIONBAR to riched20 and WS_MAXIMIZE to
 *      the window manager -- `maxq.c` -- so a probe's control silently fills
 *      its host and is never resized again.)
 *   3  the style, created with WS_VSCROLL rather than gaining it.
 *   4  the parent, a plain host window rather than an MFC frame.
 *
 * Rows 1-3 are here. Row 4 cannot be built and is what `wpscroll.c` asks of
 * the running program instead.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o barwhy.obj barwhy.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o barwhy.exe barwhy.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py barwhy.exe
 *   Z:\barwhy.exe        -> Z:\barwhy.txt
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
static char many[2048];
static WCHAR manyw[2048];

static void say(HWND re, const char *what)
{
    LONG s = GetWindowLongA(re, GWL_STYLE);
    SCROLLINFO si;
    RECT cr;
    GetClientRect(re, &cr);
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_ALL;
    fprintf(GUEST_STREAM, "  %s  style %08x  vscroll %s  client %dx%d  lines %d",
            what, (unsigned)s, (s & WS_VSCROLL) ? "SET  " : "clear",
            (int)cr.right, (int)cr.bottom,
            (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0));
    if (GetScrollInfo(re, SB_VERT, &si))
        fprintf(GUEST_STREAM, "  vert min %d max %d page %d pos %d\n",
                (int)si.nMin, (int)si.nMax, (int)si.nPage, (int)si.nPos);
    else
        fprintf(GUEST_STREAM, "  no vertical bar\n");
}

static void fill_and_say(HWND re, const char *label)
{
    say(re, "empty                   ");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)many);
    pump(500);
    say(re, "61 lines                ");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM) "short");
    pump(500);
    say(re, "emptied again           ");
    (void)label;
}

void WinMainCRTStartup(void)
{
    WNDCLASSA wc;
    HWND re;
    int i, n;

    LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "bw";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "bw", "bw", WS_OVERLAPPEDWINDOW, 8, 8, 800, 560,
                           NULL, NULL, wc.hInstance, NULL);
    g_out = CreateFileA("Z:\\barwhy.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        0, NULL);
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    pump(400);

    for (i = 0, n = 0; i < 60; i++) {
        many[n++] = 'a';
        many[n++] = 'b';
        many[n++] = '\r';
        many[n++] = '\n';
    }
    many[n] = 0;
    for (i = 0; i <= n; i++)
        manyw[i] = (WCHAR)(unsigned char)many[i];

    fprintf(GUEST_STREAM, "== 1  RichEdit20A, WordPad's style word ==\n");
    re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "", (DWORD)0x550081C4, 0, 0,
                         756, 382, host, NULL, NULL, NULL);
    SetFocus(re);
    pump(200);
    fill_and_say(re, "A");
    DestroyWindow(re);

    fprintf(GUEST_STREAM, "\n== 2  RichEdit20W, which is what the machine's is ==\n");
    re = CreateWindowExW(0x210, L"RichEdit20W", L"", (DWORD)0x550081C4, 0, 0,
                         756, 382, host, NULL, NULL, NULL);
    if (!re) {
        fprintf(GUEST_STREAM, "  CreateWindowExW returned NULL\n");
    } else {
        SetFocus(re);
        pump(200);
        say(re, "empty                   ");
        SendMessageW(re, WM_SETTEXT, 0, (LPARAM)manyw);
        pump(500);
        say(re, "61 lines                ");
        SendMessageW(re, WM_SETTEXT, 0, (LPARAM)L"short");
        pump(500);
        say(re, "emptied again           ");
        DestroyWindow(re);
    }

    fprintf(GUEST_STREAM, "\n== 3  RichEdit20A resized after creation, as MFC does ==\n");
    re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "", (DWORD)0x550081C4, 0, 0,
                         756, 382, host, NULL, NULL, NULL);
    SetFocus(re);
    pump(200);
    SetWindowPos(re, NULL, 0, 0, 760, 386, SWP_NOZORDER | SWP_NOACTIVATE);
    pump(300);
    fill_and_say(re, "resized");
    DestroyWindow(re);

    fprintf(GUEST_STREAM, "\n== 4  RichEdit20A created WITH WS_VSCROLL, for contrast ==\n");
    re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "",
                         (DWORD)(0x550081C4u | (unsigned)WS_VSCROLL), 0, 0, 756,
                         382, host, NULL, NULL, NULL);
    SetFocus(re);
    pump(200);
    fill_and_say(re, "WS_VSCROLL");
    DestroyWindow(re);

    CloseHandle(g_out);
    ExitProcess(0);
}
