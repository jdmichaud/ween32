/* Why the harness's control is not the size it was asked for.
 *
 * I measured riched20's control coming back 408x289 when `rp_create` asks for
 * 280x160, and reported it as observed and unexplained. Dan then measured
 * ours honouring the request exactly -- **so the two halves of the
 * differential test were laying text out in controls of different widths**,
 * which makes every wrapping comparison between them meaningless.
 *
 * It is one bit of the style word, and it is not a mistake in it:
 *
 *     0x550081C4 as it stands           window 412 x 293   the host's client
 *     same, minus 0x01000000            window 280 x 160
 *     0x540081C4 written out            window 280 x 160
 *     WS_CHILD|WS_VISIBLE|ES_MULTILINE  window 280 x 160
 *     the same plus WS_MAXIMIZE         window 412 x 293
 *
 * **`WS_MAXIMIZE` is `0x01000000` and `0x550081C4` contains it.** On Windows
 * a child window created with it is sized to fill its parent's client area
 * and the width and height passed to `CreateWindowExA` are ignored. It
 * happens for a plain `EDIT` too, so it is the window manager rather than
 * riched20, and it happens immediately rather than after a message is pumped.
 *
 * **The style word is right.** It is WordPad's own, and WordPad's editor
 * fills its frame -- this is how. What follows is two different things:
 *
 *   1  ween32 does not honour WS_MAXIMIZE on a child window, which is a
 *      divergence from win32 in its own right
 *   2  until it does, the two sides' controls are sized by different rules,
 *      so their wrap points cannot be compared -- ours by the 280x160 in the
 *      call, the machine's by whatever host window the probe made
 *
 * The sizes measured either way are in maxq-sizes.txt beside this.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c -o maxq.obj maxq.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o maxq.exe maxq.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py maxq.exe
 *   Z:\maxq.exe
 */
#include <windows.h>
#include <richedit.h>
#include "guestcrt.h"
static void pump(int ms){MSG m;DWORD e=GetTickCount()+ms;
  while(GetTickCount()<e){while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}Sleep(2);}}
static HWND host;
static void one(DWORD style, const char *what)
{
    RECT wr;
    HWND re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "", style, 0, 0, 280, 160,
                              host, NULL, NULL, NULL);
    if (!re) { fprintf(GUEST_STREAM, "  %s: NOT CREATED\n", what); return; }
    GetWindowRect(re, &wr);
    fprintf(GUEST_STREAM, "  %s: window %ld x %ld\n", what,
            (long)(wr.right-wr.left), (long)(wr.bottom-wr.top));
    DestroyWindow(re);
}
void WinMainCRTStartup(void)
{
    WNDCLASSA wc; RECT hc;
    LoadLibraryA("riched20.dll");
    memset(&wc,0,sizeof wc); wc.lpfnWndProc=DefWindowProcA;
    wc.hInstance=GetModuleHandleA(NULL); wc.lpszClassName="mq";
    RegisterClassA(&wc);
    host=CreateWindowExA(0,"mq","mq",WS_OVERLAPPEDWINDOW,40,40,420,320,NULL,NULL,wc.hInstance,NULL);
    g_out=CreateFileA("Z:\\maxq.txt",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    ShowWindow(host,SW_SHOW); UpdateWindow(host); pump(300);
    GetClientRect(host,&hc);
    fprintf(GUEST_STREAM,"host client %ld x %ld; asked for 280x160 each time\n",
            (long)hc.right,(long)hc.bottom);
    fprintf(GUEST_STREAM,"WS_MAXIMIZE is 0x01000000 and 0x550081C4 contains it\n\n");
    one(0x550081C4,                 "0x550081C4 as it stands      ");
    one(0x550081C4u & ~0x01000000u, "same, minus 0x01000000       ");
    one(0x540081C4,                 "0x540081C4 written out       ");
    one(WS_CHILD|WS_VISIBLE|ES_MULTILINE, "WS_CHILD|WS_VISIBLE|MULTILINE");
    one(WS_CHILD|WS_VISIBLE|ES_MULTILINE|WS_MAXIMIZE, "the same plus WS_MAXIMIZE    ");
    CloseHandle(g_out);
    ExitProcess(0);
}
