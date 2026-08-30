/* Which line does a left indent pair narrow?
 *
 * `03-indent-then-type` differs between the two sides in line COUNT, not
 * column, at identical client widths: the machine makes three lines of 35,
 * 43 and 5, ours makes two of 44 and 39. I read that as the two sides
 * indenting different lines and said so as a reading rather than a cause.
 * **This is the cause, measured, because a fix has to copy a rule.**
 *
 * Four pairs, the same text, the same 276px client, reporting the x each
 * line's first character is drawn at:
 *
 *     start   offset      line 0    line 1    line 2
 *         0        0        x 1       x 1       x 1
 *       720        0        x 49      x 49      x 49
 *         0      720        x 1       x 49      x 49
 *       720     -720        x 49      x 1       x 1     <- 03's pair
 *
 * **`dxStartIndent` is the first line's indent; `dxOffset` is how much more
 * the lines after it get.** first = start, continuation = start + offset.
 * All four rows fit, including the two that look alike from a distance:
 * `(720, 0)` indents everything and `(0, 720)` indents everything except
 * the first.
 *
 * So for 03's `(720, -720)` riched20 narrows the FIRST line -- 35 characters
 * at x 49 -- and leaves the continuations full width at x 1, which is
 * exactly the 35, 43, 5 in the machine's dump. Ours gives 44 for its first
 * line, which is the *unindented* width, so ours is not applying
 * `dxStartIndent` to the first line at all.
 *
 * **And this reconciles with the bullet grid** in bulletprobe.txt, which
 * found `first = max(start + 11, start + offset)`. That 11 is the bullet's
 * own indent acting as a floor on the first line; take the bullet away and
 * the floor goes with it, leaving `first = start`. Two measurements that
 * looked like different rules are one rule and a bullet.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c -o hang.obj hang.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o hang.exe hang.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py hang.exe
 *   Z:\hang.exe
 */
#include <windows.h>
#include <richedit.h>
#include "guestcrt.h"
static void pump(int ms){MSG m;DWORD e=GetTickCount()+ms;
  while(GetTickCount()<e){while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}Sleep(2);}}
static HWND host;
static HWND mk(void)
{
    CHARFORMATA d;
    HWND re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "", (DWORD)0x540081C4,
                              0, 0, 280, 160, host, NULL, NULL, NULL);
    memset(&d,0,sizeof d); d.cbSize=sizeof d;
    d.dwMask=CFM_FACE|CFM_SIZE|CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE;
    d.dwEffects=0; d.yHeight=200;
    d.szFaceName[0]='A';d.szFaceName[1]='r';d.szFaceName[2]='i';
    d.szFaceName[3]='a';d.szFaceName[4]='l';d.szFaceName[5]=0;
    SendMessageA(re,EM_SETCHARFORMAT,SCF_DEFAULT,(LPARAM)&d);
    SendMessageA(re,EM_SETTARGETDEVICE,0,0);
    SetFocus(re); pump(150); return re;
}
/* For each line: where it starts, and the x its first character is drawn at.
 * **The x is the whole question** -- a line that is indented starts further
 * right, and which lines are indented is what distinguishes "the first line
 * is narrowed" from "the continuations are". */
static void lines_of(HWND re, const char *what)
{
    int n = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0), i;
    fprintf(GUEST_STREAM, "  %s\n", what);
    for (i = 0; i < n && i < 6; i++) {
        POINTL p;
        int at = (int)SendMessageA(re, EM_LINEINDEX, (WPARAM)i, 0);
        int ln = (int)SendMessageA(re, EM_LINELENGTH, (WPARAM)at, 0);
        p.x = p.y = 0x7BAD;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)at);
        fprintf(GUEST_STREAM, "    line %d: starts at char %d, x %ld, %d chars\n",
                i, at, (long)p.x, ln);
    }
}
void WinMainCRTStartup(void)
{
    WNDCLASSA wc; HWND re; RECT c;
    static const char *L =
      "the quick brown fox jumps over the lazy dog and the heron waits here "
      "and more after that as well so it wraps";
    LoadLibraryA("riched20.dll");
    memset(&wc,0,sizeof wc); wc.lpfnWndProc=DefWindowProcA;
    wc.hInstance=GetModuleHandleA(NULL); wc.lpszClassName="hg";
    RegisterClassA(&wc);
    host=CreateWindowExA(0,"hg","hg",WS_OVERLAPPEDWINDOW,40,40,420,320,NULL,NULL,wc.hInstance,NULL);
    g_out=CreateFileA("Z:\\hang.txt",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    ShowWindow(host,SW_SHOW); UpdateWindow(host); pump(300);
    fprintf(GUEST_STREAM,"which line does a left indent pair narrow?\n");
    {
        static const struct { long st, off; const char *name; } v[] = {
            {   0,    0, "start 0, offset 0    (neither)" },
            { 720,    0, "start 720, offset 0" },
            {   0,  720, "start 0, offset 720" },
            { 720, -720, "start 720, offset -720   <- 03's pair" },
        };
        int k;
        for (k = 0; k < 4; k++) {
            PARAFORMAT pf;
            re = mk();
            GetClientRect(re, &c);
            SendMessageA(re, WM_SETTEXT, 0, (LPARAM)L);
            pump(200);
            memset(&pf,0,sizeof pf); pf.cbSize=sizeof pf;
            pf.dwMask = PFM_STARTINDENT | PFM_OFFSET;
            pf.dxStartIndent = v[k].st; pf.dxOffset = v[k].off;
            SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
            pump(250);
            fprintf(GUEST_STREAM, "\n  client %ld px wide\n", (long)c.right);
            lines_of(re, v[k].name);
            DestroyWindow(re);
        }
    }
    CloseHandle(g_out); ExitProcess(0);
}
