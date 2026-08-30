/* How wide the text actually is, in pixels, on each side.
 *
 * **This exists because "the fonts differ" was used twice as an explanation
 * and never once as a measurement.** known-differences.md entry 6 first
 * excused three real findings that way, and then -- after Sam re-took the
 * dumps at equal widths and the wrap columns agreed exactly -- was deleted.
 * One disagreement survives:
 *
 *     06-resize-while-selected, client 256 on both sides, same text
 *     ours  0 at 0 len 44      machine  0 at 0 len 40
 *
 * A break column cannot answer why. Word boundaries are sparse, so a width
 * difference of several percent moves a break in some texts and not others
 * -- which is exactly the pattern: 03 and 07 agree, 06 does not. **The
 * question is how wide the glyphs are, and no instrument in either
 * repository has ever asked it.**
 *
 * So: wrap turned OFF -- `EM_SETTARGETDEVICE` with any width, which is what
 * WordPad's No Wrap sends -- so the whole string is one line and `x` is
 * cumulative advance with the client width taken out of it entirely. Then
 * `EM_POSFROMCHAR` every four characters of 06's own text.
 *
 * Run `tests/replay_test --glyphs` here for the same table, and the two
 * columns are directly comparable: same string, same indices, same message.
 *
 * **`x 0` answers a second question for free.** `0x01000000` is `WS_MAXIMIZE`
 * -- maxq.c measured it sizing a child, a plain EDIT included, so that is the
 * window manager and it is not in doubt -- but for a rich edit the same value
 * is *also* `ES_SELECTIONBAR`. **They are not alternatives; the bit is both,
 * and WordPad's style word has it.** Ours draws the margin: our text begins
 * at `x 0 = 9` where an unmargined control begins at 1. Whether riched20
 * draws one for the same bit has never been read, and `x 0` on the machine
 * says so in one number.
 *
 * Note which way that cuts for 06: a selection bar makes the usable width
 * *smaller*, so if only we have one we should fit FEWER characters than the
 * machine. We fit more -- 44 against 40 -- so a margin we have and it lacks
 * cannot be the explanation. It would make the gap bigger, not smaller.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o glyphs.obj glyphs.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o glyphs.exe glyphs.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py glyphs.exe
 *   Z:\glyphs.exe          -> Z:\glyphs.txt
 */
#include <windows.h>
#include <richedit.h>
#include "guestcrt.h"
static const char *TEXT06 =
    "the quick brown fox jumps over the lazy dog and the heron waits here";
static void pump(int ms){MSG m;DWORD e=GetTickCount()+ms;while(GetTickCount()<e){while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}Sleep(2);} }
void WinMainCRTStartup(void){WNDCLASSA wc;HWND host,re;CHARFORMAT2A cf;RECT cr;int i,n;
  LoadLibraryA("riched20.dll");memset(&wc,0,sizeof wc);wc.lpfnWndProc=DefWindowProcA;
  wc.hInstance=GetModuleHandleA(NULL);wc.lpszClassName="g";RegisterClassA(&wc);
  host=CreateWindowExA(0,"g","g",WS_OVERLAPPEDWINDOW,40,40,400,300,NULL,NULL,wc.hInstance,NULL);
  g_out=CreateFileA("Z:\\glyphs.txt",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
  re=CreateWindowExA(0x210,RICHEDIT_CLASSA,"",(DWORD)0x550081C4,0,0,280,160,host,NULL,NULL,NULL);
  ShowWindow(host,SW_SHOW);UpdateWindow(host);SetFocus(re);pump(300);
  /* the same face rp_create sets, with the three style bits cleared */
  memset(&cf,0,sizeof cf);cf.cbSize=sizeof cf;
  cf.dwMask=CFM_FACE|CFM_SIZE|CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE;
  cf.yHeight=200;lstrcpyA(cf.szFaceName,"Arial");
  SendMessageA(re,EM_SETCHARFORMAT,SCF_DEFAULT,(LPARAM)&cf);
  SendMessageA(re,EM_SETTARGETDEVICE,0,1440);   /* wrap off: one long line */
  SetWindowTextA(re,TEXT06);pump(120);
  GetClientRect(re,&cr);
  fprintf(GUEST_STREAM,"client %ld %ld\n",(long)(cr.right-cr.left),(long)(cr.bottom-cr.top));
  fprintf(GUEST_STREAM,"lines %ld\n",(long)SendMessageA(re,EM_GETLINECOUNT,0,0));
  for(n=0;TEXT06[n];n++){}
  for(i=0;i<=n;i+=4){POINTL pt;pt.x=0;pt.y=0;
    SendMessageA(re,EM_POSFROMCHAR,(WPARAM)&pt,(LPARAM)i);
    fprintf(GUEST_STREAM,"x %d %ld\n",i,(long)pt.x);}
  ExitProcess(0);}
