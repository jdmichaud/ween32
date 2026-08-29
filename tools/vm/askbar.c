/* What a running program's own bars will say across a process boundary.
 *
 * probe.c walks another process's windows; ctlprobe.c creates its own. There
 * is a third thing worth asking and neither does it: a control message sent
 * to *another* program, chosen so that it takes no pointer either way. Those
 * are safe -- the answer comes back in the return value and nothing is
 * marshalled -- and they are the only way to ask the machine's own WordPad
 * how its toolbar is configured rather than how a toolbar we made is.
 *
 *   TB_GETBUTTONSIZE, TB_GETPADDING, TB_BUTTONCOUNT, TB_GETBITMAPFLAGS
 *   SB_GETPARTS with a null pointer (the count), SB_GETTEXTLENGTH
 *
 * Anything with a buffer -- TB_GETBUTTON, SB_GETPARTS with an array,
 * SB_GETTEXT -- would hand this process's address to that one, and that is
 * what closed WordPad twice.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c -o askbar.obj askbar.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,console \
 *          -Wl,--entry,mainCRTStartup -o askbar.exe askbar.obj \
 *          -lkernel32 -luser32 -lcomctl32
 *   tools/vm/pe2k.py askbar.exe
 */
#include <windows.h>
#include <commctrl.h>

void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *__stack_chk_guard = (void *)0x0bad57ac;
void __stack_chk_fail(void) { ExitProcess(3); }

static HANDLE out_file;
static char buf[1024];

static void emit(const char *s)
{
    DWORD n;
    WriteFile(out_file, s, (DWORD)lstrlenA(s), &n, NULL);
}

static BOOL CALLBACK child(HWND w, LPARAM lp)
{
    char cls[64], txt[64];
    RECT r;
    (void)lp;
    cls[0] = txt[0] = 0;
    GetClassNameA(w, cls, sizeof cls);
    GetWindowTextA(w, txt, sizeof txt);
    GetWindowRect(w, &r);
    if (!lstrcmpA(cls, TOOLBARCLASSNAMEA)) {
        LRESULT size = SendMessageA(w, TB_GETBUTTONSIZE, 0, 0);
        LRESULT pad = SendMessageA(w, TB_GETPADDING, 0, 0);
        LRESULT n = SendMessageA(w, TB_BUTTONCOUNT, 0, 0);
        LRESULT rows = SendMessageA(w, TB_GETROWS, 0, 0);
        LRESULT flags = SendMessageA(w, TB_GETBITMAPFLAGS, 0, 0);
        wsprintfA(buf, "toolbar \"%s\" %ldx%ld at %ld,%ld\r\n"
                       "  TB_GETBUTTONSIZE  %d x %d\r\n"
                       "  TB_GETPADDING     %d x %d\r\n"
                       "  TB_BUTTONCOUNT    %ld    TB_GETROWS %ld\r\n"
                       "  TB_GETBITMAPFLAGS %ld\r\n",
                  txt, r.right - r.left, r.bottom - r.top, r.left, r.top,
                  (int)LOWORD(size), (int)HIWORD(size),
                  (int)LOWORD(pad), (int)HIWORD(pad),
                  (long)n, (long)rows, (long)flags);
        emit(buf);
    } else if (!lstrcmpA(cls, STATUSCLASSNAMEA)) {
        LRESULT parts = SendMessageA(w, SB_GETPARTS, 0, 0);
        LRESULT len0 = SendMessageA(w, SB_GETTEXTLENGTHA, 0, 0);
        LRESULT simple = SendMessageA(w, SB_ISSIMPLE, 0, 0);
        wsprintfA(buf, "status bar %ldx%ld at %ld,%ld\r\n"
                       "  SB_GETPARTS (count) %ld\r\n"
                       "  part 0 length %d, flags %04X\r\n"
                       "  SB_ISSIMPLE %ld\r\n",
                  r.right - r.left, r.bottom - r.top, r.left, r.top,
                  (long)parts, (int)LOWORD(len0), (unsigned)HIWORD(len0),
                  (long)simple);
        emit(buf);
    }
    return TRUE;
}

static char a1[256], a2[256];

static char *arg(int want, char *line, int cap)
{
    char *p = GetCommandLineA();
    int i = 0, n = 0;
    while (*p) {
        char q = 0;
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (*p == '"') {
            q = '"';
            p++;
        }
        if (n == want) {
            while (*p && (q ? *p != q : *p != ' ') && i < cap - 1)
                line[i++] = *p++;
            line[i] = 0;
            return line;
        }
        while (*p && (q ? *p != q : *p != ' '))
            p++;
        if (q && *p == q)
            p++;
        n++;
    }
    return 0;
}

void mainCRTStartup(void)
{
    char *title = arg(1, a1, sizeof a1);
    char *path = arg(2, a2, sizeof a2);
    HWND top;
    out_file = CreateFileA(path ? path : "Z:\\askbar.txt", GENERIC_WRITE, 0,
                           NULL, CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    top = FindWindowA(NULL, title ? title : "Document - WordPad");
    if (!top) {
        emit("no such window\r\n");
        CloseHandle(out_file);
        ExitProcess(2);
    }
    EnumChildWindows(top, child, 0);
    CloseHandle(out_file);
    ExitProcess(0);
}
