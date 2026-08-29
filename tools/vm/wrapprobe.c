/* Does a rich edit re-wrap when its own scrollbar appears?
 *
 * Measured on WordPad, the answer is **no**: text appended until the bar came
 * up left the first line breaking in exactly the same place. But WordPad's
 * editor always has a formatting rectangle set with `EM_SETRECT`, so that
 * reading cannot tell two rules apart:
 *
 *   A  the wrap width never allows for the bar -- the bar overlays whatever
 *      is under it, and a control with no EM_SETRECT would run text beneath it
 *   B  the wrap width comes from the formatting rectangle when there is one,
 *      and from the client (bar subtracted) when there is not
 *
 * **Every machine reading we have is of a control with a rectangle set.** This
 * asks the other case: a plain `RichEdit20W`, **no `EM_SETRECT`**, filled a
 * paragraph at a time until it raises its own vertical bar, reporting after
 * every step:
 *
 *   the line count, so the growth is visible
 *   `EM_LINEINDEX(1)` and `(2)` -- the character each line starts at, which is
 *     the wrap point as a number rather than as ink, so no crop can include a
 *     scrollbar by accident
 *   whether `WS_VSCROLL` is set, so "the bar appeared" is read and not assumed
 *
 * **If the line starts do not move across the step where the bar appears,
 * rule A holds and the bar overlays.** If they move, rule B does, and the
 * machine's WordPad is stable only because its rectangle is fixed.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o wrapprobe.obj wrapprobe.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o wrapprobe.exe wrapprobe.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py wrapprobe.exe
 *   Z:\wrapprobe.exe Z:\wrap.txt
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

static const char *CHUNK = "the quick brown fox jumps over the lazy dog and ";

static void row(HWND re, int step)
{
    LONG st = GetWindowLongA(re, GWL_STYLE);
    int lines = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0);
    int l1 = (int)SendMessageA(re, EM_LINEINDEX, 1, 0);
    int l2 = (int)SendMessageA(re, EM_LINEINDEX, 2, 0);
    int len = (int)SendMessageA(re, WM_GETTEXTLENGTH, 0, 0);
    RECT c;
    GetClientRect(re, &c);
    wsprintfA(buf, "  %-4d %-6d %-6d %-8d %-8d %-4s  client %ld\r\n", step, len,
              lines, l1, l2, (st & WS_VSCROLL) ? "SET" : "-", c.right);
    emit(buf);
}

static void probe_main(void)
{
    static char path_buf[512], text[8192];
    char *path = NULL;
    WNDCLASSA wc;
    HWND host, re;
    int i, n = 0;

    {   /* argv[1], no CRT */
        char *p = GetCommandLineA();
        int seen = 0, q = 0, k;
        for (;;) {
            while (*p == ' ') p++;
            if (!*p) break;
            if (*p == '"') { q = 1; p++; }
            k = 0;
            while (*p && (q ? *p != '"' : *p != ' ')) {
                if (k < (int)sizeof(path_buf) - 1) path_buf[k++] = *p;
                p++;
            }
            path_buf[k] = 0;
            if (*p) p++;
            if (seen++ == 1) { path = path_buf; break; }
            q = 0;
        }
    }

    LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "wrapprobe";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "wrapprobe", "wrapprobe", WS_OVERLAPPEDWINDOW,
                           40, 40, 320, 200, NULL, NULL, wc.hInstance, NULL);

    out_file = CreateFileA(path ? path : "Z:\\wrap.txt", GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    /* **No EM_SETRECT anywhere in this file.** That absence is the experiment;
     * WordPad's editor always has one and every reading so far is of that. */
    re = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL,
                         0, 0, 220, 80, host, NULL, wc.hInstance, NULL);
    if (!re) {
        emit("the control would not be created\r\n");
        CloseHandle(out_file);
        ExitProcess(2);
    }

    /* **The host is shown and the queue pumped before anything is measured.**
     * The first run of this probe left it hidden and the bar never appeared at
     * all -- twenty-one lines in a control eighty pixels tall and `vsc` never
     * became SET, which reads as "a bare rich edit does not raise a bar" and
     * is really "a control in an unshown window has not laid itself out". The
     * `vsc` column is in the report so that reads as a missing case rather
     * than as an answer. */
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    {
        MSG m;
        DWORD end = GetTickCount() + 600;
        while (GetTickCount() < end) {
            while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&m);
                DispatchMessageA(&m);
            }
            Sleep(10);
        }
    }

    emit("a RichEdit20W 220x80, no EM_SETRECT, filled a chunk at a time\r\n");
    emit("  step len    lines  line1st  line2nd  vsc   client\r\n");

    text[0] = 0;
    for (i = 0; i < 14; i++) {
        int k = 0;
        while (CHUNK[k]) {
            if (n < (int)sizeof(text) - 2)
                text[n++] = CHUNK[k];
            k++;
        }
        text[n] = 0;
        SetWindowTextA(re, text);
        {
            MSG m;
            DWORD end = GetTickCount() + 120;
            while (GetTickCount() < end) {
                while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&m);
                    DispatchMessageA(&m);
                }
                Sleep(5);
            }
        }
        row(re, i);
    }

    /* **The bar never appears above, so the question has to be asked another
     * way.** A control created *with* `WS_VSCROLL` has a bar from the start
     * and the same client width in `GetClientRect` terms; if its wrap point is
     * the same as the barless one's, the bar is not subtracted from the wrap
     * width (rule A). If it wraps earlier, it is (rule B). Same size, same
     * text, same absence of `EM_SETRECT` -- one style bit apart. */
    {
        HWND re2 = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                                   WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                       ES_AUTOVSCROLL | WS_VSCROLL,
                                   0, 90, 220, 80, host, NULL, wc.hInstance,
                                   NULL);
        MSG m;
        DWORD end;
        emit("\r\nthe same control created WITH WS_VSCROLL, still no EM_SETRECT\r\n");
        emit("  step len    lines  line1st  line2nd  vsc   client\r\n");
        if (!re2) {
            emit("  would not be created\r\n");
        } else {
            SetWindowTextA(re2, text);
            end = GetTickCount() + 400;
            while (GetTickCount() < end) {
                while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&m);
                    DispatchMessageA(&m);
                }
                Sleep(5);
            }
            row(re2, 99);
        }
    }

    emit("\r\nSame line1st in both -> the bar is not taken out of the wrap width.\r\n"
         "Smaller in the second -> it is.\r\n");
    CloseHandle(out_file);
    ExitProcess(0);
}

void WinMainCRTStartup(void) { probe_main(); }
