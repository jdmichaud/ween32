/* Which message makes a rich edit insert a paragraph break.
 *
 * The differential harness sends both `WM_KEYDOWN VK_RETURN` and
 * `WM_CHAR '\r'` for `enter`, because ween32 acts on one and riched20 on the
 * other. **That makes the test work and hides the divergence underneath it**
 * -- the same shape as the Shift compensation -- so the singles are asked
 * here rather than left as "unmeasured on the machine" forever.
 *
 * Four forms, in order, against the same control `rp_create` builds:
 *
 *     WM_CHAR '\r'            WM_CHAR '\n'
 *     WM_KEYDOWN VK_RETURN    EM_REPLACESEL "\r"
 *
 * Measured on Windows 2000, output in keyform.txt, and identical on wine:
 * the two `WM_CHAR` forms do nothing, the keydown and the replace both
 * insert. ween32 is the exact opposite on the first two, which bob measured.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o keyform.obj keyform.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o keyform.exe keyform.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py keyform.exe
 *   Z:\keyform.exe
 */
#include <windows.h>
#include <richedit.h>
#include "guestcrt.h"
static void pump(int ms){MSG m;DWORD e=GetTickCount()+ms;while(GetTickCount()<e){while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}Sleep(2);} }
static void show(HWND re, const char *what){char t[256];int n=(int)SendMessageA(re,WM_GETTEXT,255,(LPARAM)t);int i;t[n<0?0:n]=0;
  fprintf(GUEST_STREAM,"%s",what);for(i=0;t[i];i++)fprintf(GUEST_STREAM," %02x",(unsigned char)t[i]);fprintf(GUEST_STREAM,"\n");}
void WinMainCRTStartup(void){WNDCLASSA wc;HWND host,re;
  LoadLibraryA("riched20.dll");memset(&wc,0,sizeof wc);wc.lpfnWndProc=DefWindowProcA;
  wc.hInstance=GetModuleHandleA(NULL);wc.lpszClassName="k";RegisterClassA(&wc);
  host=CreateWindowExA(0,"k","k",WS_OVERLAPPEDWINDOW,40,40,400,300,NULL,NULL,wc.hInstance,NULL);
  g_out=CreateFileA("Z:\\k.txt",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
  re=CreateWindowExA(0x210,RICHEDIT_CLASSA,"",(DWORD)0x550081C4,0,0,280,160,host,NULL,NULL,NULL);
  ShowWindow(host,SW_SHOW);UpdateWindow(host);SetFocus(re);pump(300);
  SendMessageA(re,WM_CHAR,'a',1);pump(50);
  SendMessageA(re,WM_CHAR,'\r',1);pump(50);           show(re,"after WM_CHAR CR");
  SendMessageA(re,WM_CHAR,'\n',1);pump(50);           show(re,"after WM_CHAR LF");
  SendMessageA(re,WM_KEYDOWN,VK_RETURN,0);pump(50);   show(re,"after WM_KEYDOWN VK_RETURN");
  SendMessageA(re,EM_REPLACESEL,TRUE,(LPARAM)"\r");pump(50); show(re,"after EM_REPLACESEL CR");
  ExitProcess(0);}
