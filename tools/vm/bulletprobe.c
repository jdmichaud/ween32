/* What a bulleted paragraph *is*, asked of riched20 in messages.
 *
 * bob has #7: the model already carries `wNumbering` and only the drawing is
 * missing, and the geometry of that drawing is unread. He asked for this one
 * before any pixels, and he is right to: **a paragraph format is numbers the
 * control will tell you, and a screenshot is numbers you have to infer.** If
 * turning bullets on moves `dxStartIndent` and `dxOffset` by itself, then two
 * of the pixel questions answer themselves and the third -- where a wrapped
 * second line begins -- is already implied by `dxOffset`.
 *
 * Three paragraphs in one control, so that "bulleted" is read against
 * "the same text unbulleted" in the same run rather than against a memory of
 * one:
 *
 *   0  plain, untouched
 *   1  the same text, PFN_BULLET set and nothing else
 *   2  long enough to wrap, bulleted -- so the second line's start is a
 *      *drawn* fact and not only a stated one
 *
 * **Every field is poisoned before every read**, and `dwMask` is NOT among
 * the poisoned ones: `undoprobe.c` poisoned exactly that field an hour ago,
 * and since the mask is how riched20 says which of its answers mean anything,
 * the poison read back as "this answer is valid" whatever the control did.
 * Here the mask is zeroed and printed raw beside the values.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o bulletprobe.obj bulletprobe.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o bulletprobe.exe bulletprobe.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py bulletprobe.exe
 *   Z:\bulletprobe.exe Z:\bullet.txt
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

void __stack_chk_fail(void) { ExitProcess(3); }

static HANDLE out_file;
static char buf[2048];

static void emit(const char *s)
{
    DWORD n;
    WriteFile(out_file, s, (DWORD)lstrlenA(s), &n, NULL);
}

static void pump(int ms)
{
    MSG m;
    DWORD end = GetTickCount() + (DWORD)ms;
    while (GetTickCount() < end) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(5);
    }
}

static void select_at(HWND re, int at)
{
    CHARRANGE r;
    r.cpMin = at;
    r.cpMax = at;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
}

/* The paragraph format at a character position, every field printed. */
static void para_at(HWND re, int at, const char *what)
{
    PARAFORMAT pf;
    select_at(re, at);
    memset(&pf, 0, sizeof pf);
    pf.cbSize = sizeof pf;
    pf.dxStartIndent = 0x7BAD;
    pf.dxRightIndent = 0x7BAD;
    pf.dxOffset = 0x7BAD;
    pf.wNumbering = 0x7BAD;
    pf.wAlignment = 0x7BAD;
    SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    wsprintfA(buf, "  %-22s numbering %-6d startIndent %-6ld offset %-6ld "
                   "rightIndent %-6ld align %-3d mask %08X\r\n",
              what, (int)pf.wNumbering, (long)pf.dxStartIndent,
              (long)pf.dxOffset, (long)pf.dxRightIndent, (int)pf.wAlignment,
              (unsigned)pf.dwMask);
    emit(buf);
}

/* Where a character is drawn. EM_POSFROMCHAR on a rich edit writes a POINTL
 * through wParam -- one name, two calls, which reprobe.c settled -- so the
 * point is poisoned to tell "wrote nothing" from "wrote a zero". */
static void pos_at(HWND re, int at, const char *what)
{
    POINTL p;
    p.x = 0x7BAD;
    p.y = 0x7BAD;
    SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)at);
    wsprintfA(buf, "  %-22s index %-4d x %-6ld y %-6ld %s\r\n", what, at,
              (long)p.x, (long)p.y,
              (p.x == 0x7BAD && p.y == 0x7BAD) ? "UNTOUCHED" : "");
    emit(buf);
}

static void set_bullet(HWND re, int at)
{
    PARAFORMAT pf;
    select_at(re, at);
    memset(&pf, 0, sizeof pf);
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_NUMBERING;
    pf.wNumbering = PFN_BULLET;
    SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static const char *P0 = "alpha beta gamma";
static const char *P2 =
    "the quick brown fox jumps over the lazy dog and the quick brown fox "
    "jumps over the lazy dog and the patient heron waits";

static void probe_main(void)
{
    static char path_buf[512], text[1024];
    char *path = NULL;
    WNDCLASSA wc;
    HWND host, re;
    int p1_start, p2_start, n = 0, i;

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
    wc.lpszClassName = "bulletprobe";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "bulletprobe", "bulletprobe",
                           WS_OVERLAPPEDWINDOW, 40, 40, 460, 300, NULL, NULL,
                           wc.hInstance, NULL);
    out_file = CreateFileA(path ? path : "Z:\\bullet.txt", GENERIC_WRITE, 0,
                           NULL, CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    re = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0, 400, 220,
                         host, NULL, wc.hInstance, NULL);
    if (!re) {
        emit("the control would not be created\r\n");
        CloseHandle(out_file);
        ExitProcess(2);
    }
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    pump(500);

    /* Three paragraphs. A break is stored as ONE character -- bob measured
     * that this hour -- so the starts are counted that way and not from the
     * \r\n a GetWindowText would hand back. */
    for (i = 0; P0[i]; i++) text[n++] = P0[i];
    text[n++] = '\r';
    p1_start = n;
    for (i = 0; P0[i]; i++) text[n++] = P0[i];
    text[n++] = '\r';
    p2_start = n;
    for (i = 0; P2[i]; i++) text[n++] = P2[i];
    text[n] = 0;
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)text);
    pump(200);

    emit("== three paragraphs, none bulleted yet ==\r\n");
    para_at(re, 0, "para 0, plain");
    para_at(re, p1_start, "para 1, plain");
    para_at(re, p2_start, "para 2, plain");
    pos_at(re, 0, "para 0 first char");
    pos_at(re, p1_start, "para 1 first char");
    pos_at(re, p2_start, "para 2 first char");

    emit("\r\n== PFM_NUMBERING/PFN_BULLET on paragraphs 1 and 2 ==\r\n");
    set_bullet(re, p1_start);
    set_bullet(re, p2_start);
    pump(300);
    para_at(re, 0, "para 0, still plain");
    para_at(re, p1_start, "para 1, bulleted");
    para_at(re, p2_start, "para 2, bulleted");

    emit("\r\n  Same startIndent and offset before and after means the\r\n"
         "  control does NOT move the text for a bullet, and WordPad's\r\n"
         "  menu item must be sending the indents itself. Different means\r\n"
         "  the control owns the geometry and it is these numbers.\r\n");

    emit("\r\n== where the characters actually land ==\r\n");
    pos_at(re, 0, "para 0 first char");
    pos_at(re, p1_start, "para 1 first char");
    pos_at(re, p2_start, "para 2 first char");
    /* The wrapped line: walk forward until y changes, and report the first
     * character of the second line. **That is the question a screenshot
     * cannot answer without a ruler** -- whether a wrapped line hangs under
     * the text or under the bullet. */
    {
        POINTL first, p;
        int at;
        first.x = first.y = 0x7BAD;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&first, (LPARAM)p2_start);
        for (at = p2_start + 1; at < n; at++) {
            p.x = p.y = 0x7BAD;
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)at);
            if (p.y != first.y) {
                wsprintfA(buf, "  para 2 wraps at index %d: x %ld y %ld "
                               "(first line x %ld y %ld)\r\n",
                          at, (long)p.x, (long)p.y, (long)first.x,
                          (long)first.y);
                emit(buf);
                break;
            }
        }
        if (at >= n)
            emit("  para 2 did not wrap -- the control is too wide for it\r\n");
    }
    emit("\r\n  A wrapped line starting at the same x as the first line means\r\n"
         "  the bullet hangs outside the text block. A smaller x means it\r\n"
         "  does not.\r\n");

    CloseHandle(out_file);
    ExitProcess(0);
}

void WinMainCRTStartup(void) { probe_main(); }
