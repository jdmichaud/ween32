/* Can wrapping be turned on in a control whose style says no wrap?
 *
 * `lenwrap.c` found that `rp_create`'s control does not wrap -- ES_AUTOHSCROLL
 * is in WordPad's style word and on a multiline control that turns word wrap
 * off -- so three of the differential test's seven sequences produce one line
 * and say nothing about line breaking. The style word is WordPad's own and
 * being WordPad's is the point, so the question is not whether to drop the
 * bit but whether WordPad's other half puts wrapping back.
 *
 * `wrapprobe.c` measured the message from the other direction this evening:
 * `EM_SETTARGETDEVICE` with a width and no device turns wrapping *off*, which
 * is what WordPad's No Wrap sends, and nought brings it back. **What was not
 * asked is whether nought can turn it ON against a style bit that says no.**
 *
 * It can. Measured on Windows 2000, output in wrapon.txt:
 *
 *     rp_create's control, 96 characters in 280px
 *       as created                          1 line
 *       after EM_SETTARGETDEVICE(0, 0)      2      <- overrides the style
 *       after EM_SETTARGETDEVICE(0, 1440)   1
 *       back to (0, 0)                      2      <- and reverses
 *
 *     without ES_AUTOHSCROLL
 *       as created                          2      wraps by default
 *       after EM_SETTARGETDEVICE(0, 1440)   1
 *
 * So `rp_create` sending `EM_SETTARGETDEVICE(0, 0)` after creating the control
 * gives WordPad's own default -- wrap to window -- without touching the style
 * word, and `line` starts meaning something in sequences 03, 06 and 07.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o wrapon.obj wrapon.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o wrapon.exe wrapon.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py wrapon.exe
 *   Z:\wrapon.exe
 */
#include <windows.h>
#include <richedit.h>
#include "guestcrt.h"
static void pump(int ms){MSG m;DWORD e=GetTickCount()+ms;
  while(GetTickCount()<e){while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}Sleep(2);}}
static HWND host;
static const char *L =
  "the quick brown fox jumps over the lazy dog and the heron waits here "
  "and more after that as well";
static void row(HWND re, const char *what)
{
    fprintf(GUEST_STREAM, "  %s lines %d\n", what,
            (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0));
}
void WinMainCRTStartup(void)
{
    WNDCLASSA wc; HWND re;
    LoadLibraryA("riched20.dll");
    memset(&wc,0,sizeof wc); wc.lpfnWndProc=DefWindowProcA;
    wc.hInstance=GetModuleHandleA(NULL); wc.lpszClassName="wo";
    RegisterClassA(&wc);
    host=CreateWindowExA(0,"wo","wo",WS_OVERLAPPEDWINDOW,40,40,400,300,NULL,NULL,wc.hInstance,NULL);
    g_out=CreateFileA("Z:\\wrapon.txt",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    ShowWindow(host,SW_SHOW); UpdateWindow(host); pump(300);

    fprintf(GUEST_STREAM,"== rp_create's control, 96 chars in 280px ==\n");
    re=CreateWindowExA(0x210,RICHEDIT_CLASSA,"",(DWORD)0x550081C4,0,0,280,160,host,NULL,NULL,NULL);
    SetFocus(re); pump(150);
    SendMessageA(re,WM_SETTEXT,0,(LPARAM)L); pump(250);
    row(re,"as created                    ");

    /* wrapprobe.c measured that a width with no device turns wrapping OFF and
     * nought brings it back. This asks whether nought can turn it ON in a
     * control whose ES_AUTOHSCROLL says no wrap. */
    SendMessageA(re,EM_SETTARGETDEVICE,0,(LPARAM)0); pump(300);
    row(re,"after EM_SETTARGETDEVICE(0,0) ");

    SendMessageA(re,EM_SETTARGETDEVICE,0,(LPARAM)1440); pump(300);
    row(re,"after EM_SETTARGETDEVICE(0,1440)");

    SendMessageA(re,EM_SETTARGETDEVICE,0,(LPARAM)0); pump(300);
    row(re,"back to (0,0)                 ");
    DestroyWindow(re);

    fprintf(GUEST_STREAM,"\n== and a control without ES_AUTOHSCROLL, for contrast ==\n");
    re=CreateWindowExA(0x210,RICHEDIT_CLASSA,"",(DWORD)(0x550081C4u&~0x0080u),0,0,280,160,host,NULL,NULL,NULL);
    SetFocus(re); pump(150);
    SendMessageA(re,WM_SETTEXT,0,(LPARAM)L); pump(250);
    row(re,"as created                    ");
    SendMessageA(re,EM_SETTARGETDEVICE,0,(LPARAM)1440); pump(300);
    row(re,"after EM_SETTARGETDEVICE(0,1440)");
    CloseHandle(g_out);
    ExitProcess(0);
}
