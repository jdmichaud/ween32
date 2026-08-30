/* What is actually in WordPad's font combos, read off the running program.
 *
 * jd: *"Can you also add all the font size in the drop down. There is only 10
 * right now."* alice needs the real list before she fills ours, and said
 * outright that her guess at it -- `8 9 10 11 12 14 16 18 20 22 24 26 28 36
 * 48 72` -- is the kind of belief that has been wrong twice today.
 *
 * **Every item is read without a single pointer crossing the process
 * boundary**, which is the constraint `askbar.c` records the hard way: a
 * `CB_GETLBTEXT` would hand this process's address to that one, and that is
 * what closed WordPad twice. `CB_SETCURSEL` takes an index and returns one,
 * and `WM_GETTEXT` is system-defined and marshalled by USER32 -- so selecting
 * each item in turn and reading the combo's own text walks the whole list
 * with nothing unsafe in it. The original selection is put back afterwards.
 *
 * **And `CB_SETCURSEL` does not send `CBN_SELCHANGE`** -- only a user's
 * choice does -- so this reads the list without the application acting on it.
 * That matters here: the size combo on the machine *has* a handler, and a
 * probe that applied a font size to jd's document while measuring it would be
 * the disturb-what-you-measure fault again.
 *
 * `GetGUIThreadInfo` answers the other half of alice's question 4 -- where
 * focus goes after Enter -- and is safe for the same reason: the struct is
 * this process's and only the thread id crosses.
 *
 * Run it with WordPad up, and again with the Font dialog open: it walks every
 * window belonging to WordPad's GUI thread, so the dialog's own size list at
 * id 1138 comes out of the same run. It appends, so a sequence of states
 * accumulates with a clock between them.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o fontlist.obj fontlist.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o fontlist.exe fontlist.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py fontlist.exe
 *   Z:\fontlist.exe       -> appends to Z:\fontlist.txt
 */
#include <windows.h>
#include "guestcrt.h"

static DWORD wp_thread;

static void combo(HWND w)
{
    char buf[128];
    int count, cur, i;

    cur = (int)SendMessageA(w, CB_GETCURSEL, 0, 0);
    count = (int)SendMessageA(w, CB_GETCOUNT, 0, 0);
    buf[0] = 0;
    SendMessageA(w, WM_GETTEXT, (WPARAM)sizeof buf, (LPARAM)buf);
    fprintf(GUEST_STREAM, "  ComboBox id %d   count %d   cursel %d   shows \"%s\"\n",
            (int)GetWindowLongA(w, GWL_ID), count, cur, buf);
    if (count <= 0 || count > 400)
        return;
    fprintf(GUEST_STREAM, "   ");
    for (i = 0; i < count; i++) {
        SendMessageA(w, CB_SETCURSEL, (WPARAM)i, 0);
        buf[0] = 0;
        SendMessageA(w, WM_GETTEXT, (WPARAM)sizeof buf, (LPARAM)buf);
        fprintf(GUEST_STREAM, " %s", buf);
        if ((i % 16) == 15)
            fprintf(GUEST_STREAM, "\n   ");
    }
    fprintf(GUEST_STREAM, "\n");
    /* Put it back exactly as found. */
    SendMessageA(w, CB_SETCURSEL, (WPARAM)cur, 0);
}

static BOOL CALLBACK child(HWND w, LPARAM lp)
{
    char cls[64];
    (void)lp;
    cls[0] = 0;
    GetClassNameA(w, cls, sizeof cls);
    if (cls[0] == 'C' && cls[1] == 'o' && cls[2] == 'm' && cls[3] == 'b')
        combo(w);
    return TRUE;
}

static BOOL CALLBACK top(HWND w, LPARAM lp)
{
    char cls[64], txt[96];
    (void)lp;
    if (GetWindowThreadProcessId(w, NULL) != wp_thread)
        return TRUE;
    if (!IsWindowVisible(w))
        return TRUE;
    cls[0] = txt[0] = 0;
    GetClassNameA(w, cls, sizeof cls);
    GetWindowTextA(w, txt, sizeof txt);
    fprintf(GUEST_STREAM, "\n window %s \"%s\"\n", cls, txt);
    EnumChildWindows(w, child, 0);
    return TRUE;
}

void WinMainCRTStartup(void)
{
    HWND wp;
    SYSTEMTIME t;
    GUITHREADINFO gi;

    g_out = CreateFileA("Z:\\fontlist.txt", GENERIC_WRITE, FILE_SHARE_READ,
                        NULL, OPEN_ALWAYS, 0, NULL);
    if (g_out == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    SetFilePointer(g_out, 0, NULL, FILE_END);

    GetLocalTime(&t);
    fprintf(GUEST_STREAM, "== %02d:%02d:%02d ==\n", (int)t.wHour, (int)t.wMinute,
            (int)t.wSecond);

    wp = FindWindowA(NULL, "Document - WordPad");
    if (!wp) {
        fprintf(GUEST_STREAM, "  no window titled \"Document - WordPad\"\n");
        CloseHandle(g_out);
        ExitProcess(2);
    }
    wp_thread = GetWindowThreadProcessId(wp, NULL);

    /* Where the keyboard is, before anything is touched -- alice's question 4
     * is "applies and keeps focus, or applies and returns it to the editor",
     * and that is this line. */
    memset(&gi, 0, sizeof gi);
    gi.cbSize = sizeof gi;
    if (GetGUIThreadInfo(wp_thread, &gi) && gi.hwndFocus) {
        char cls[64];
        cls[0] = 0;
        GetClassNameA(gi.hwndFocus, cls, sizeof cls);
        fprintf(GUEST_STREAM, " focus is %s id %d\n", cls,
                (int)GetWindowLongA(gi.hwndFocus, GWL_ID));
    } else {
        fprintf(GUEST_STREAM, " focus unknown\n");
    }

    EnumWindows(top, 0);
    CloseHandle(g_out);
    ExitProcess(0);
}
