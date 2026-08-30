/* What the machine's own WordPad editor says about its scroll bars.
 *
 * `hscroll.c` measures a control this process made. That control now
 * reproduces the three vertical states `docs/testing.md` records of the
 * machine -- so it is a fair stand-in -- but every number it gives is still
 * *our* control answering about itself, and the question alice asked is
 * about WordPad. **"Which control gave you that?" is the four words that
 * unpicked a whole exchange here once already.** This asks the running
 * program.
 *
 * Read-only, and pointer-free by construction: `GetWindowLongA`,
 * `GetWindowRect` and `GetScrollInfo` all take this process's memory and the
 * other process's window handle. **Nothing here sends a message with a
 * buffer in it** -- `askbar.c`'s header records that doing so closed WordPad
 * twice -- so this cannot disturb what it measures.
 *
 * Run it with WordPad in whatever state is being asked about; it appends, so
 * a sequence of states accumulates in one file with a marker between them.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o wpscroll.obj wpscroll.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o wpscroll.exe wpscroll.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py wpscroll.exe
 *   Z:\wpscroll.exe       -> appends to Z:\wpscroll.txt
 */
#include <windows.h>
#include <richedit.h>
#include "guestcrt.h"

static void one(HWND w, const char *cls)
{
    LONG s = GetWindowLongA(w, GWL_STYLE);
    LONG ex = GetWindowLongA(w, GWL_EXSTYLE);
    RECT r, c;
    SCROLLINFO si;

    GetWindowRect(w, &r);
    GetClientRect(w, &c);
    fprintf(GUEST_STREAM,
            "  %s  style %08x  ex %08x  window %dx%d  client %dx%d\n", cls,
            (unsigned)s, (unsigned)ex, (int)(r.right - r.left),
            (int)(r.bottom - r.top), (int)c.right, (int)c.bottom);
    fprintf(GUEST_STREAM, "    vscroll %s   hscroll %s\n",
            (s & WS_VSCROLL) ? "SET  " : "clear",
            (s & WS_HSCROLL) ? "SET" : "clear");

    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_ALL;
    if (GetScrollInfo(w, SB_HORZ, &si))
        fprintf(GUEST_STREAM, "    horz  min %d max %d page %d pos %d\n",
                (int)si.nMin, (int)si.nMax, (int)si.nPage, (int)si.nPos);
    else
        fprintf(GUEST_STREAM, "    horz  no bar\n");

    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_ALL;
    if (GetScrollInfo(w, SB_VERT, &si))
        fprintf(GUEST_STREAM, "    vert  min %d max %d page %d pos %d\n",
                (int)si.nMin, (int)si.nMax, (int)si.nPage, (int)si.nPos);
    else
        fprintf(GUEST_STREAM, "    vert  no bar\n");
}

static BOOL CALLBACK child(HWND w, LPARAM lp)
{
    char cls[64];
    (void)lp;
    cls[0] = 0;
    GetClassNameA(w, cls, sizeof cls);
    /* RichEdit20A and RichEdit20W both, and nothing else: the frame's other
     * children are the bars and they have their own probe. */
    if (cls[0] == 'R' && cls[1] == 'i' && cls[2] == 'c' && cls[3] == 'h')
        one(w, cls);
    return TRUE;
}

void WinMainCRTStartup(void)
{
    HWND top;
    SYSTEMTIME t;

    g_out = CreateFileA("Z:\\wpscroll.txt", GENERIC_WRITE, FILE_SHARE_READ,
                        NULL, OPEN_ALWAYS, 0, NULL);
    if (g_out == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    SetFilePointer(g_out, 0, NULL, FILE_END);

    GetLocalTime(&t);
    fprintf(GUEST_STREAM, "== %02d:%02d:%02d ==\n", (int)t.wHour, (int)t.wMinute,
            (int)t.wSecond);

    top = FindWindowA(NULL, "Document - WordPad");
    if (!top) {
        fprintf(GUEST_STREAM, "  no window titled \"Document - WordPad\"\n");
        CloseHandle(g_out);
        ExitProcess(2);
    }
    EnumChildWindows(top, child, 0);
    CloseHandle(g_out);
    ExitProcess(0);
}
