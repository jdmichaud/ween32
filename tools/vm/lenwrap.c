/* Two questions the differential test's first machine run raised.
 *
 * **1. Two lengths, and in ours they are one number.** Dan asked before the
 * `len` fix was written: wordpad greys five commands on a test that asks
 * whether there is anything in the document, and if fixing `len` makes an
 * empty document length 1, every one of those silently becomes true. In
 * ween32 `GetWindowTextLengthA` and the selection's `cpMax` are the same
 * value, so the question could not be answered from here.
 *
 * riched20 keeps them apart:
 *
 *     empty     GetWindowTextLength 0    select-all cpMax 1
 *     "abc"                        3                      4
 *     "a\rb"                       4                      4
 *
 * So the trailing paragraph mark lives in the *selection's* index space and
 * not in the text length, and the five commands stay greyed. **The third row
 * is why this was worth measuring rather than reasoning about**: there the
 * two numbers agree, for unrelated reasons -- four bytes of text against
 * three characters plus the mark -- so a probe that only tried that case
 * would have concluded they are the same quantity.
 *
 * **2. The harness's own control does not wrap.** Three of the seven
 * sequences in tools/vm/seq were written to wrap, and all three produce one
 * line. Removing one style bit at a time says why:
 *
 *     as rp_create 0x550081C4    lines 1
 *     minus ES_AUTOHSCROLL       lines 2
 *     minus ES_AUTOVSCROLL       lines 1
 *
 * `ES_AUTOHSCROLL` on a multiline control turns word wrap off. The style
 * word is WordPad's own and being WordPad's is the point -- WordPad turns
 * wrapping on separately, through `EM_SETTARGETDEVICE`, which wrapprobe.c
 * measured. So this is not an argument for dropping the bit; it is a record
 * that `line`, one of the five fields the dump contract names, currently
 * only moves across an explicit paragraph mark.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o lenwrap.obj lenwrap.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o lenwrap.exe lenwrap.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py lenwrap.exe
 *   Z:\lenwrap.exe
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

static HWND mk(DWORD style)
{
    HWND r = CreateWindowExA(0x210, RICHEDIT_CLASSA, "", style, 0, 0, 280, 160,
                             host, NULL, NULL, NULL);
    SetFocus(r);
    pump(150);
    return r;
}

/* Dan's question: `GetWindowTextLengthA` against the selection's own cpMax.
 * In ours they are the same number; if they differ on the machine, then
 * fixing `len` does not silently un-grey wordpad's five "is there anything
 * here" tests, because those ask the first and the dump asks the second. */
static void lens(HWND re, const char *what)
{
    CHARRANGE r;
    long gwtl = (long)GetWindowTextLengthA(re);
    long wmtl = (long)SendMessageA(re, WM_GETTEXTLENGTH, 0, 0);
    r.cpMin = 0; r.cpMax = -1;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
    memset(&r, 0, sizeof r);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&r);
    fprintf(GUEST_STREAM, "  %s GetWindowTextLength %ld  WM_GETTEXTLENGTH %ld"
                          "  selectall cpMax %ld  lines %d\n",
            what, gwtl, wmtl, (long)r.cpMax,
            (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0));
}

void WinMainCRTStartup(void)
{
    WNDCLASSA wc;
    HWND re;
    static const char *LONG_ONE =
        "the quick brown fox jumps over the lazy dog and the heron waits here "
        "and more after that as well";
    LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "lw";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "lw", "lw", WS_OVERLAPPEDWINDOW, 40, 40, 400, 300,
                           NULL, NULL, wc.hInstance, NULL);
    g_out = CreateFileA("Z:\\lenwrap.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ShowWindow(host, SW_SHOW); UpdateWindow(host); pump(300);

    fprintf(GUEST_STREAM, "== two lengths, riched20, rp_create's style ==\n");
    re = mk(0x550081C4);
    lens(re, "empty        ");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)"abc"); pump(150);
    lens(re, "\"abc\"        ");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)"a\rb"); pump(150);
    lens(re, "\"a\\rb\"       ");
    DestroyWindow(re);

    fprintf(GUEST_STREAM, "\n== does it wrap? 96 characters in a 280px control ==\n");
    {
        static const struct { const char *name; DWORD st; } v[] = {
            { "as rp_create 0x550081C4  ", 0x550081C4 },
            { "minus ES_AUTOHSCROLL     ", 0x550081C4u & ~0x0080u },
            { "minus ES_AUTOVSCROLL     ", 0x550081C4u & ~0x0040u },
        };
        int k;
        for (k = 0; k < 3; k++) {
            HWND r2 = mk(v[k].st);
            SendMessageA(r2, WM_SETTEXT, 0, (LPARAM)LONG_ONE);
            pump(250);
            fprintf(GUEST_STREAM, "  %s lines %d\n", v[k].name,
                    (int)SendMessageA(r2, EM_GETLINECOUNT, 0, 0));
            DestroyWindow(r2);
        }
    }
    CloseHandle(g_out);
    ExitProcess(0);
}
