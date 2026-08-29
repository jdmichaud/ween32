/* What `EM_POSFROMCHAR` does, in both controls that answer it.
 *
 * `ctlprobe.c` asked an EDIT and found that index 0 of an empty control and
 * index == length both answer **-1**, while the caret is drawn at both. That
 * reading was then written into an oracle for a monkey test that drives a
 * **rich edit**, with a note that riched20 was unmeasured -- and the note is
 * too kind, because the two controls do not share a signature:
 *
 *     EDIT      wParam = index                 the point comes back packed in
 *                                              the result, or -1
 *     RICHEDIT  wParam = POINTL *out           lParam = index; the result is
 *                                              not the position at all
 *
 * **One name, two calls.** So "does riched20 agree" is not a well-formed
 * question and this asks each of them what it does, separately, at the two
 * places a monkey lands most: an empty control, and the index one past the
 * last character -- which is where the caret sits after every append.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o reprobe.obj reprobe.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o reprobe.exe reprobe.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py reprobe.exe
 *   Z:\reprobe.exe Z:\re.txt
 */
#include <windows.h>
#include <richedit.h>

void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    while (n--)
        *a++ = *b++;
    return d;
}

void *__stack_chk_guard = (void *)0x0bad57ac;

void __stack_chk_fail(void)
{
    ExitProcess(3);
}

static HANDLE out_file;
static char buf[1024];

static void emit(const char *s)
{
    DWORD n;
    WriteFile(out_file, s, (DWORD)lstrlenA(s), &n, NULL);
}

/* The EDIT's convention: the answer is the return value, packed, or -1. */
static void ask_edit(HWND w, const char *what, int lo, int hi)
{
    int i;
    wsprintfA(buf, "  %s\r\n", what);
    emit(buf);
    for (i = lo; i <= hi; i++) {
        LRESULT r = SendMessageA(w, EM_POSFROMCHAR, (WPARAM)i, 0);
        if (r == -1)
            wsprintfA(buf, "    index %-3d -> -1 (the whole result)\r\n", i);
        else
            wsprintfA(buf, "    index %-3d -> x %d y %d  (raw %08X)\r\n", i,
                      (int)(short)LOWORD(r), (int)(short)HIWORD(r),
                      (unsigned)r);
        emit(buf);
    }
}

/* The rich edit's convention: wParam is where to put the answer, lParam is
 * the index, and the return value is something else entirely -- which is the
 * whole reason this file exists. **The point is poisoned before the call** so
 * that "the control did not write anything" is distinguishable from "it wrote
 * a zero", which is exactly the distinction an oracle needs. */
static void ask_rich(HWND w, const char *what, int lo, int hi)
{
    int i;
    wsprintfA(buf, "  %s\r\n", what);
    emit(buf);
    for (i = lo; i <= hi; i++) {
        POINTL p;
        LRESULT r;
        p.x = 0x7BAD;
        p.y = 0x7BAD;
        r = SendMessageA(w, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)i);
        wsprintfA(buf, "    index %-3d -> x %-6d y %-6d  %s (result %d)\r\n", i,
                  (int)p.x, (int)p.y,
                  (p.x == 0x7BAD && p.y == 0x7BAD) ? "UNTOUCHED" : "written",
                  (int)r);
        emit(buf);
    }
}

static void probe_main(void)
{
    static char path_buf[512];
    char *path;
    WNDCLASSA wc;
    HWND host, ed, re;
    HMODULE rich;
    int len;

    {   /* argv[1] without a CRT */
        char *p = GetCommandLineA();
        int n = 0, q = 0, seen = 0;
        for (;;) {
            while (*p == ' ')
                p++;
            if (!*p)
                break;
            if (*p == '"') { q = 1; p++; }
            n = 0;
            while (*p && (q ? *p != '"' : *p != ' ')) {
                if (n < (int)sizeof(path_buf) - 1)
                    path_buf[n++] = *p;
                p++;
            }
            path_buf[n] = 0;
            if (*p) p++;
            if (seen++ == 1) break;
            q = 0;
        }
        path = seen > 1 ? path_buf : NULL;
    }

    rich = LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "reprobe";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "reprobe", "reprobe", WS_OVERLAPPEDWINDOW,
                           40, 40, 400, 260, NULL, NULL, wc.hInstance, NULL);

    out_file = CreateFileA(path ? path : "Z:\\re.txt", GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    wsprintfA(buf, "riched20.dll %s\r\n\r\n", rich ? "loaded" : "MISSING");
    emit(buf);

    emit("== EDIT: wParam is the index, the answer is the return value ==\r\n");
    ed = CreateWindowExA(0, "EDIT", "",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL,
                         0, 0, 200, 100, host, NULL, wc.hInstance, NULL);
    ask_edit(ed, "empty control", 0, 1);
    SetWindowTextA(ed, "abc\r\ndef");
    len = (int)SendMessageA(ed, WM_GETTEXTLENGTH, 0, 0);
    wsprintfA(buf, "  after \"abc\\r\\ndef\", WM_GETTEXTLENGTH = %d\r\n", len);
    emit(buf);
    ask_edit(ed, "with text", len - 1, len + 1);

    emit("\r\n== RICHEDIT20W: wParam is a POINTL*, lParam is the index ==\r\n");
    re = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL,
                         0, 100, 200, 100, host, NULL, wc.hInstance, NULL);
    if (!re) {
        emit("  the control would not be created\r\n");
    } else {
        ask_rich(re, "empty control", 0, 1);
        SetWindowTextA(re, "abc\r\ndef");
        len = (int)SendMessageA(re, WM_GETTEXTLENGTH, 0, 0);
        wsprintfA(buf, "  after \"abc\\r\\ndef\", WM_GETTEXTLENGTH = %d\r\n",
                  len);
        emit(buf);
        ask_rich(re, "every index, and one past the end", 0, len + 1);
        /* And the same question the EDIT was asked about a line break: does
         * the pair of characters share one position, or does riched20 store
         * the break as one character? */
        emit("  (compare indices across the break with the EDIT's above)\r\n");
    }

    CloseHandle(out_file);
    ExitProcess(0);
}

void WinMainCRTStartup(void) { probe_main(); }
