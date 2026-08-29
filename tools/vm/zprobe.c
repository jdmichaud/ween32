/* Ask Windows what raising a window actually does, one operation at a time.
 *
 * `SetForegroundWindow`, `SetActiveWindow` and `BringWindowToTop` are three
 * names people use interchangeably and they are not synonyms. ween32 has none
 * of them, and `SetWindowPos` takes a `hWndInsertAfter` and drops it, so there
 * is no z-order for them to be built on. Before any of that is written, this
 * asks the machine which of the three moves the z-order, which moves the
 * activation, and which moves the keyboard.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o zprobe.obj zprobe.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o zprobe.exe zprobe.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py zprobe.exe
 *
 *   Z:\zprobe.exe <op> Z:\z<op>.txt
 *
 * **One operation per run, each from the same starting state**, rather than a
 * sequence: applied cumulatively, the second answer would be about whatever
 * the first left behind, and the question is what each one does on its own.
 *
 *   0   nothing -- the baseline the other three are measured against
 *   1   BringWindowToTop(A)
 *   2   SetActiveWindow(A)
 *   3   SetForegroundWindow(A)
 *
 * Two overlapping top-level windows: **A at 80,80 and B at 220,180**, B made
 * second so it starts on top. Their captions say which is which, and the
 * caption colour says which is active -- so the report answers the
 * programmatic half and a capture of the same run answers the visible half.
 * The window stays up after the report is written so that capture can be
 * taken.
 */
#include <windows.h>

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
static HWND g_a, g_b;

static void emit(const char *s)
{
    DWORD n;
    WriteFile(out_file, s, (DWORD)lstrlenA(s), &n, NULL);
}

/* Which of the two a handle is, so the report reads A and B rather than two
 * hex numbers that mean nothing to the person reading it. */
static const char *name_of(HWND h)
{
    if (!h)
        return "(none)";
    if (h == g_a)
        return "A";
    if (h == g_b)
        return "B";
    return "other";
}

/* The z-order, top first, by walking the desktop's children. **This is the
 * measurement** -- everything else in the report is a handle comparison, and
 * this is the only part that says what is in front of what. */
static void zorder(char *dst, int cap)
{
    HWND h = GetTopWindow(NULL);
    int n = 0;
    dst[0] = 0;
    for (; h && n < cap - 8; h = GetWindow(h, GW_HWNDNEXT)) {
        const char *nm;
        if (h != g_a && h != g_b)
            continue; /* the shell's windows are not what is being asked */
        nm = name_of(h);
        if (dst[0])
            lstrcatA(dst, " then ");
        lstrcatA(dst, nm);
        n++;
    }
    if (!dst[0])
        lstrcpyA(dst, "(neither found)");
}

static void report(const char *when)
{
    char z[128];
    zorder(z, sizeof z);
    wsprintfA(buf, "%-14s z-order %-16s foreground %-6s active %-6s focus %s\r\n",
              when, z, name_of(GetForegroundWindow()), name_of(GetActiveWindow()),
              name_of(GetFocus()));
    emit(buf);
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    /* **WM_ACTIVATE is recorded as it arrives**, because which window gets it
     * and in which order is a thing an implementation has to reproduce and a
     * capture cannot show. */
    if (m == WM_ACTIVATE && out_file) {
        wsprintfA(buf, "  WM_ACTIVATE   %s %s\r\n", name_of(h),
                  LOWORD(w) == WA_INACTIVE ? "losing it" : "taking it");
        emit(buf);
    }
    /* And WM_MOUSEACTIVATE, which is how a click decides whether it activates
     * at all -- the message a window answers to refuse. Its own answer is
     * recorded rather than assumed, since DefWindowProc's is the thing being
     * measured. */
    if (m == WM_MOUSEACTIVATE && out_file) {
        LRESULT r = DefWindowProcA(h, m, w, l);
        wsprintfA(buf, "  WM_MOUSEACTIVATE %s -> %s\r\n", name_of(h),
                  r == MA_ACTIVATE ? "MA_ACTIVATE" :
                  r == MA_ACTIVATEANDEAT ? "MA_ACTIVATEANDEAT" :
                  r == MA_NOACTIVATE ? "MA_NOACTIVATE" :
                  r == MA_NOACTIVATEANDEAT ? "MA_NOACTIVATEANDEAT" : "?");
        emit(buf);
        return r;
    }
    if (m == WM_LBUTTONDOWN && out_file) {
        wsprintfA(buf, "  WM_LBUTTONDOWN   %s\r\n", name_of(h));
        emit(buf);
    }
    return DefWindowProcA(h, m, w, l);
}

static void pump(int ms)
{
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(10);
    }
}

static char *arg(int want, char *dst, int cap)
{
    char *p = GetCommandLineA();
    int i = 0;
    for (;;) {
        int q = 0, n = 0;
        while (*p == ' ')
            p++;
        if (!*p)
            return NULL;
        if (*p == '"') {
            q = 1;
            p++;
        }
        while (*p && (q ? *p != '"' : *p != ' ')) {
            if (n < cap - 1)
                dst[n++] = *p;
            p++;
        }
        dst[n] = 0;
        if (*p)
            p++;
        if (i++ == want)
            return dst;
    }
}

static void probe_main(void)
{
    static char a1[64], a2[512];
    char *ops = arg(1, a1, sizeof a1);
    char *path = arg(2, a2, sizeof a2);
    WNDCLASSA wc;
    int op = ops && ops[0] >= '0' && ops[0] <= '9' ? ops[0] - '0' : 0;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "zprobe";
    RegisterClassA(&wc);

    /* A first, then B, so B starts in front: the library's own note says one
     * window is in front of another "by its age", and this is the state that
     * claim describes. */
    g_a = CreateWindowExA(0, "zprobe", "A", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          80, 80, 300, 200, NULL, NULL, wc.hInstance, NULL);
    g_b = CreateWindowExA(0, "zprobe", "B", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          220, 180, 300, 200, NULL, NULL, wc.hInstance, NULL);
    pump(700);

    out_file = CreateFileA(path ? path : "Z:\\zprobe.txt", GENERIC_WRITE, 0,
                           NULL, CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    wsprintfA(buf, "== op %d: %s ==\r\n", op,
              op == 1 ? "BringWindowToTop(A)" :
              op == 2 ? "SetActiveWindow(A)" :
              op == 3 ? "SetForegroundWindow(A)" :
              op == 4 ? "SetWindowPos(B, HWND_BOTTOM)" :
              op == 5 ? "SetWindowPos(A, HWND_TOPMOST), then SetActiveWindow(B)" :
              op == 6 ? "SetWindowPos(A, B) -- explicitly behind B" :
              op == 7 ? "a click on A, which is the window behind" :
                        "nothing (the baseline)");
    emit(buf);
    report("before");

    switch (op) {
    case 1:
        wsprintfA(buf, "  returned      %d\r\n", (int)BringWindowToTop(g_a));
        break;
    case 2: {
        HWND was = SetActiveWindow(g_a);
        wsprintfA(buf, "  returned      %s (the one that was active)\r\n",
                  name_of(was));
        break;
    }
    case 3:
        wsprintfA(buf, "  returned      %d\r\n", (int)SetForegroundWindow(g_a));
        break;
    case 4:
        wsprintfA(buf, "  returned      %d\r\n",
                  (int)SetWindowPos(g_b, HWND_BOTTOM, 0, 0, 0, 0,
                                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        break;
    case 5:
        wsprintfA(buf, "  returned      %d\r\n",
                  (int)SetWindowPos(g_a, HWND_TOPMOST, 0, 0, 0, 0,
                                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        break;
    case 6:
        wsprintfA(buf, "  returned      %d\r\n",
                  (int)SetWindowPos(g_a, g_b, 0, 0, 0, 0,
                                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        break;
    case 7:
        /* **Nothing is called: the operation is a person clicking.** A's
         * visible corner is around screen (100,110), clear of B. Eight
         * seconds of pumping is the window in which to do it. */
        lstrcpyA(buf, "  waiting 8s for a click on A ...\r\n");
        break;
    default:
        buf[0] = 0;
        break;
    }
    if (buf[0])
        emit(buf);
    pump(op == 7 ? 8000 : 700);
    report("after");

    /* **TOPMOST is only interesting if it survives the other window being
     * activated** -- otherwise it is an expensive way of saying HWND_TOP.
     *
     * **And activating B straight away proves nothing, because B is already
     * active**: `SWP_NOACTIVATE` left it that way, so `SetActiveWindow(B)` is
     * a no-op and A staying on top would be the answer whatever topmost
     * meant. The first version of this did exactly that. So A is activated
     * first -- which makes the second step a real activation change -- and
     * the question becomes whether activating B lifts it over a topmost A. */
    if (op == 5) {
        emit("\r\n== SetActiveWindow(A) first, so the next step is not a no-op ==\r\n");
        SetActiveWindow(g_a);
        pump(600);
        report("A active");
        emit("== then SetActiveWindow(B): does it come over a topmost A? ==\r\n");
        SetActiveWindow(g_b);
        pump(600);
        report("B active");
    }

    /* And what a plain SetWindowPos with HWND_TOP does, which is the call
     * ween32 already has and already ignores the argument of. */
    if (op == 0) {
        emit("\r\n== and SetWindowPos(A, HWND_TOP, SWP_NOMOVE|SWP_NOSIZE) ==\r\n");
        SetWindowPos(g_a, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        pump(500);
        report("after");
    }
    CloseHandle(out_file);
    out_file = NULL;

    /* Stays up so the captions can be captured: which one is blue is the
     * visible half of the same question. */
    pump(600000);
    ExitProcess(0);
}

void WinMainCRTStartup(void) { probe_main(); }
