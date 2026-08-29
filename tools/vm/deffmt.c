/* Does `SCF_DEFAULT` survive `WM_SETTEXT`, and does riched20 have it at all?
 *
 * WordPad sets Arial 10 on its editor with `EM_SETCHARFORMAT` and
 * `SCF_DEFAULT` before a character exists. **Ours then loses it the moment a
 * document is opened**: the format bar reads `Arial` for a fresh document, a
 * typed one and one made by File > New, and `MS Shell Dlg` for one that was
 * opened -- a dialog face on a document.
 *
 * The cause is located in `src/richedit.c` and it is that **`SCF_DEFAULT` is
 * not implemented**. `EM_SETCHARFORMAT` tests `wParam` for `SCF_ALL` and
 * otherwise takes the selection, so a default-format call on an empty control
 * lands in `e->insert` -- the format the *next typed character* carries --
 * and `WM_SETTEXT` throws that away (`e->insert_armed = 0`) and resets every
 * run to the control's own face.
 *
 * **That is a diagnosis of our code, not of Windows**, and the fix depends on
 * something I have not measured: what riched20 does. Two candidate rules, and
 * they want different fixes:
 *
 *   A  `SCF_DEFAULT` sets a format that outlives the text, so text arriving
 *      by `WM_SETTEXT` comes out in it. Ours needs a third stored format.
 *   B  `SCF_DEFAULT` is the same as arming the insertion point, and the
 *      machine loses the face too -- in which case WordPad must be setting it
 *      again after a load, and the fix is in `doc.zig` and not here.
 *
 * **I have carried an EDIT's behaviour into a rich edit twice today**, so this
 * asks rather than assumes. Every reading is printed before and after, and the
 * face is poisoned before each read so "the control wrote nothing" cannot be
 * mistaken for "it wrote a face".
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o deffmt.obj deffmt.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o deffmt.exe deffmt.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py deffmt.exe
 *   Z:\deffmt.exe Z:\deffmt.txt
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
static char buf[1024];

static void emit(const char *s)
{
    DWORD n;
    WriteFile(out_file, s, (DWORD)lstrlenA(s), &n, NULL);
}

/* One reading, with the face poisoned first so an untouched buffer shows. */
static void report(HWND re, const char *when, DWORD scope)
{
    CHARFORMATA cf;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    lstrcpyA(cf.szFaceName, "<untouched>");
    cf.yHeight = 0x7BAD;
    SendMessageA(re, EM_GETCHARFORMAT, scope, (LPARAM)&cf);
    wsprintfA(buf, "  %-34s %-6s face %-22s height %d\r\n", when,
              scope == SCF_SELECTION ? "sel" : "deflt", cf.szFaceName,
              (int)cf.yHeight);
    emit(buf);
}

static void set_face(HWND re, DWORD scope, const char *face, int points)
{
    CHARFORMATA cf;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_FACE | CFM_SIZE;
    cf.yHeight = points * 20;
    lstrcpyA(cf.szFaceName, face);
    wsprintfA(buf, "  EM_SETCHARFORMAT %s %s %d -> %d\r\n",
              scope == SCF_DEFAULT ? "SCF_DEFAULT  " : "SCF_SELECTION",
              face, points,
              (int)SendMessageA(re, EM_SETCHARFORMAT, scope, (LPARAM)&cf));
    emit(buf);
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

static HWND host;
static WNDCLASSA wc;

static HWND make_edit(int y)
{
    return CreateWindowExA(0, RICHEDIT_CLASSA, "",
                           WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, y, 260, 60,
                           host, NULL, wc.hInstance, NULL);
}

static void probe_main(void)
{
    static char path_buf[512];
    char *path = NULL;
    HWND a, b;

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
    wc.lpszClassName = "deffmt";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "deffmt", "deffmt", WS_OVERLAPPEDWINDOW, 40, 40,
                           320, 220, NULL, NULL, wc.hInstance, NULL);

    out_file = CreateFileA(path ? path : "Z:\\deffmt.txt", GENERIC_WRITE, 0,
                           NULL, CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    pump(500);

    emit("== SCF_DEFAULT, the way WordPad sets its editor up ==\r\n");
    a = make_edit(0);
    if (!a) { emit("  no control\r\n"); CloseHandle(out_file); ExitProcess(2); }
    report(a, "before anything, selection", SCF_SELECTION);
    report(a, "before anything, default", SCF_DEFAULT);
    set_face(a, SCF_DEFAULT, "Arial", 10);
    report(a, "after the set, selection", SCF_SELECTION);
    report(a, "after the set, default", SCF_DEFAULT);
    SetWindowTextA(a, "hello");
    pump(200);
    emit("  -- SetWindowTextA(\"hello\") --\r\n");
    report(a, "after the text, selection", SCF_SELECTION);
    report(a, "after the text, default", SCF_DEFAULT);
    SendMessageA(a, EM_SETSEL, 0, -1);
    report(a, "after the text, all selected", SCF_SELECTION);

    emit("\r\n== SCF_SELECTION instead, same control otherwise ==\r\n");
    b = make_edit(70);
    if (!b) { emit("  no control\r\n"); }
    else {
        set_face(b, SCF_SELECTION, "Arial", 10);
        report(b, "after the set, selection", SCF_SELECTION);
        SetWindowTextA(b, "hello");
        pump(200);
        emit("  -- SetWindowTextA(\"hello\") --\r\n");
        report(b, "after the text, selection", SCF_SELECTION);
        SendMessageA(b, EM_SETSEL, 0, -1);
        report(b, "after the text, all selected", SCF_SELECTION);
    }

    emit("\r\nArial after the text in the first block -> rule A: SCF_DEFAULT\r\n"
         "outlives WM_SETTEXT and ween32 needs a third stored format.\r\n"
         "The control's own face there -> rule B: WordPad must set it again\r\n"
         "after a load, and the fix is in doc.zig.\r\n");
    CloseHandle(out_file);
    ExitProcess(0);
}

void WinMainCRTStartup(void) { probe_main(); }
