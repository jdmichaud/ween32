/* What one undo step *is*, asked of riched20 rather than assumed.
 *
 * jd: *"Undo does not work very well. If you change the style and type
 * something, only the style is undone. Please test this feature extensively."*
 *
 * alice split that into three, and this is the two that need the machine:
 *
 *   1  formatting is not in our undo record        ours, and wrong
 *   2  grouping -- does a typed run come back in one undo?   unmeasured
 *   3  depth -- how many steps before it forgets?            unmeasured
 *
 * **2 decides the shape of the fix for 1**, which is why it is worth a boot
 * before anybody writes code. If riched20 takes a whole typed run back in one
 * undo, then **an undo step is a transaction and not a keystroke**, and a
 * formatting record has to be built into transactions rather than retrofitted
 * onto a per-keystroke stack. If it takes them back one at a time, the
 * existing shape is right and only the contents are missing.
 *
 * **Typed with WM_CHAR, one character at a time**, because that is what a
 * person's keyboard produces and grouping is a question about exactly that.
 * `SetWindowTextA` and `EM_REPLACESEL` are different doors into the control
 * and may well group differently; measuring one and reporting the other is
 * the mistake this file is written to avoid.
 *
 * `EM_GETUNDONAME` is asked at every step and it is the direct answer rather
 * than an inference: riched20 names the pending undo -- typing, delete, cut,
 * paste -- so a whole run coming back as one `UID_TYPING` says *transaction*
 * in the control's own words instead of in mine.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o undoprobe.obj undoprobe.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o undoprobe.exe undoprobe.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py undoprobe.exe
 *   Z:\undoprobe.exe Z:\undo.txt
 */
#include <windows.h>
#include <richedit.h>

#ifndef EM_GETUNDONAME
#define EM_GETUNDONAME (WM_USER + 86)
#endif
#ifndef EM_GETREDONAME
#define EM_GETREDONAME (WM_USER + 87)
#endif

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

static const char *undo_name(int id)
{
    switch (id) {
    case 0: return "unknown";
    case 1: return "TYPING";
    case 2: return "DELETE";
    case 3: return "DRAGDROP";
    case 4: return "CUT";
    case 5: return "PASTE";
    default: return "?";
    }
}

/* The control's text, with the line break made visible so a report never
 * silently loses one. */
static void text_of(HWND re, char *out, int cap)
{
    int n = (int)SendMessageA(re, WM_GETTEXT, (WPARAM)cap - 1, (LPARAM)out);
    int i;
    if (n < 0) n = 0;
    out[n] = 0;
    for (i = 0; i < n; i++)
        if (out[i] == '\r' || out[i] == '\n')
            out[i] = '.';
}

/* Is the range bold, not bold, or mixed? riched20 answers `mixed` by leaving
 * CFM_BOLD out of the mask, which is a third state and not a boolean -- so it
 * is reported as three and not squeezed into two.
 *
 * **`dwMask` is not poisoned, and the first run of this file poisoned it.**
 * The trick that works for a face -- write nonsense, see whether the control
 * overwrites it -- is wrong for this field, because `dwMask` is *how riched20
 * says which of its answers mean anything*. `CFM_BOLD` is bit 0 and `0xDEAD`
 * has bit 0 set, so a poisoned mask reads as "bold is meaningful" whatever
 * the control did, and every row of the first run said `bold`, including the
 * base before anything had been bolded. The mask and the effects are printed
 * raw beside the verdict so a reader can see the answer rather than trust
 * this function. */
static char bold_buf[64];

static const char *boldness(HWND re, int from, int to)
{
    CHARFORMATA cf;
    CHARRANGE keep, r;
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&keep);
    r.cpMin = from;
    r.cpMax = to;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&keep);
    wsprintfA(bold_buf, "%-5s(m%04X e%04X)",
              !(cf.dwMask & CFM_BOLD) ? "mixed"
                  : (cf.dwEffects & CFE_BOLD) ? "bold" : "plain",
              (unsigned)(cf.dwMask & 0xFFFF), (unsigned)(cf.dwEffects & 0xFFFF));
    return bold_buf;
}

static void type_char(HWND re, char c)
{
    SendMessageA(re, WM_CHAR, (WPARAM)(unsigned char)c, 1);
}

static void type_run(HWND re, const char *s)
{
    while (*s)
        type_char(re, *s++);
}

/* Two ranges, because the whole question in jd's case is whether the text
 * that *inherited* the bold behaves like the text that was bolded. One
 * boldness column cannot tell them apart. */
static void row2(HWND re, const char *what, int a0, int a1, int b0, int b1)
{
    static char t[512];
    static char first[64];
    int name = (int)SendMessageA(re, EM_GETUNDONAME, 0, 0);
    int i;
    const char *p;
    text_of(re, t, sizeof t);
    p = boldness(re, a0, a1);
    for (i = 0; p[i] && i < 63; i++) first[i] = p[i];
    first[i] = 0;
    wsprintfA(buf, "  %-20s text %-10s canundo %d  next %-8s "
                   "old %-18s new %s\r\n",
              what, t, (int)SendMessageA(re, EM_CANUNDO, 0, 0),
              undo_name(name), first, boldness(re, b0, b1));
    emit(buf);
}

static void row(HWND re, const char *what)
{
    static char t[512];
    int name = (int)SendMessageA(re, EM_GETUNDONAME, 0, 0);
    text_of(re, t, sizeof t);
    wsprintfA(buf, "  %-22s text %-22s canundo %d  next-undo %-8s "
                   "0..3 %s\r\n",
              what, t, (int)SendMessageA(re, EM_CANUNDO, 0, 0),
              undo_name(name), boldness(re, 0, 3));
    emit(buf);
}

static void probe_main(void)
{
    static char path_buf[512];
    char *path = NULL;
    WNDCLASSA wc;
    HWND host, re;
    int i;

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
    wc.lpszClassName = "undoprobe";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "undoprobe", "undoprobe", WS_OVERLAPPEDWINDOW,
                           40, 40, 420, 240, NULL, NULL, wc.hInstance, NULL);
    out_file = CreateFileA(path ? path : "Z:\\undo.txt", GENERIC_WRITE, 0,
                           NULL, CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    re = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0, 380, 160,
                         host, NULL, wc.hInstance, NULL);
    if (!re) {
        emit("the control would not be created\r\n");
        CloseHandle(out_file);
        ExitProcess(2);
    }
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    {
        MSG m;
        DWORD end = GetTickCount() + 500;
        while (GetTickCount() < end) {
            while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&m);
                DispatchMessageA(&m);
            }
            Sleep(5);
        }
    }

    /* ---- 2. grouping ---------------------------------------------------
     * Five characters in, then undo until it will not: if the text empties
     * in one step the control groups a typed run, and if it loses one
     * character a step it does not. */
    emit("== grouping: \"hello\" typed a character at a time ==\r\n");
    type_run(re, "hello");
    row(re, "after typing");
    for (i = 0; i < 8; i++) {
        if (!SendMessageA(re, EM_CANUNDO, 0, 0))
            break;
        SendMessageA(re, EM_UNDO, 0, 0);
        wsprintfA(buf, "  undo %d", i + 1);
        row(re, buf);
    }
    wsprintfA(buf, "  -> %d undo(s) to take five characters back\r\n\r\n", i);
    emit(buf);

    /* Does a space break the run? A word processor's undo usually groups by
     * word rather than by everything since the last click. */
    emit("== grouping: \"ab cd\", to see whether a space breaks the run ==\r\n");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)"");
    type_run(re, "ab cd");
    row(re, "after typing");
    for (i = 0; i < 10; i++) {
        if (!SendMessageA(re, EM_CANUNDO, 0, 0))
            break;
        SendMessageA(re, EM_UNDO, 0, 0);
        wsprintfA(buf, "  undo %d", i + 1);
        row(re, buf);
    }
    emit("\r\n");

    /* ---- 1's shape: is formatting in the record at all? ----------------
     * jd's case exactly: style something, type after it, then undo. The
     * `0..3` column is the styled range, so each row says whether that undo
     * gave the formatting back, the characters back, or both. */
    /* **The bold is cleared first, and the first run of this did not.**
     * riched20's default face is `System` (measured in deffmt.txt) and the
     * System font is bold, so every row read `bold` including the base --
     * a style test run against a control that already had the style. The
     * `0..3` column was true and useless. Unbolding everything first makes
     * the later bolding visible as a change. */
    emit("== jd's case: bold 0..3, then type, then undo ==\r\n");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)"abcdef");
    {
        CHARFORMATA cf;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = 0;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }
    SendMessageA(re, EM_EMPTYUNDOBUFFER, 0, 0);
    row(re, "the base, unbolded");
    {
        CHARFORMATA cf;
        CHARRANGE r;
        r.cpMin = 0;
        r.cpMax = 3;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        r.cpMin = r.cpMax = 6;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
    }
    row(re, "after bolding 0..3");
    type_run(re, "XY");
    row(re, "after typing XY");
    for (i = 0; i < 8; i++) {
        if (!SendMessageA(re, EM_CANUNDO, 0, 0))
            break;
        SendMessageA(re, EM_UNDO, 0, 0);
        wsprintfA(buf, "  undo %d", i + 1);
        row(re, buf);
    }
    emit("\r\n  A row where the text shortens is an undo of the typing.\r\n"
         "  A row where 0..3 goes bold->plain is an undo of the style.\r\n"
         "  Whether the style comes back at all is the answer to jd's\r\n"
         "  report, and the ORDER is the answer to whether one step can\r\n"
         "  hold both.\r\n\r\n");

    /* ---- jd's actual sequence, which the case above does not cover ------
     *
     * alice found that my `bold 0..3 then type` passes while jd's report
     * still reproduces, and named the difference exactly: **there the bold is
     * applied to existing text and the new characters fall outside it; here
     * the new characters INHERIT the bold.** Two shapes, and only the first
     * was measured. This is the second, by both routes into it:
     *
     *   A  select the text, bold it, put the caret at the end, type
     *   B  put the caret at the end, bold with nothing selected -- which arms
     *      an insertion format rather than changing any text -- then type
     *
     * B is the case bob flagged as unmeasured when his first invariant
     * asserted undoability of it. Both are asked, because "typed text comes
     * out bold" is the same *observation* by two different mechanisms and
     * they need not undo alike. */
    emit("== jd's sequence: select all, bold, then type (inherits) ==\r\n");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)"abc");
    {
        CHARFORMATA cf;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = 0;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }
    SendMessageA(re, EM_EMPTYUNDOBUFFER, 0, 0);
    row2(re, "abc, unbolded", 0, 3, 0, 3);
    {
        CHARFORMATA cf;
        CHARRANGE r;
        r.cpMin = 0;
        r.cpMax = 3;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        r.cpMin = r.cpMax = 3;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
    }
    row2(re, "all bolded", 0, 3, 0, 3);
    type_run(re, "XY");
    row2(re, "typed XY at the end", 0, 3, 3, 5);
    for (i = 0; i < 8; i++) {
        if (!SendMessageA(re, EM_CANUNDO, 0, 0))
            break;
        SendMessageA(re, EM_UNDO, 0, 0);
        wsprintfA(buf, "  undo %d", i + 1);
        row2(re, buf, 0, 3, 3, 5);
    }
    emit("\r\n  `old` is the original abc; `new` is the typed XY. The first\r\n"
         "  undo taking the text back leaves `old` bold. The first undo\r\n"
         "  taking the style back is jd's bug, on the machine.\r\n\r\n");

    /* **The base is TYPED here, not set with WM_SETTEXT, and that is the
     * whole question.** The section above sets "abc" with WM_SETTEXT and
     * empties the undo buffer, so the typing of XY is the only typing in the
     * transaction and one undo can only take XY. A person types "abc" as
     * keystrokes -- so the real question is whether arming a format BREAKS
     * the typing run, or whether "abc" and "XY" stay one transaction and a
     * single undo empties the document.
     *
     * Ours empties it. Whether that is a bug depends entirely on this
     * reading, and the earlier section cannot answer it because it was not
     * asking it. */
    emit("== typed base, then arm, then type: does arming break the run? ==\r\n");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)"");
    SendMessageA(re, EM_EMPTYUNDOBUFFER, 0, 0);
    type_run(re, "abc");
    {
        CHARFORMATA cf;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    type_run(re, "XY");
    row2(re, "typed abc, arm, typed XY", 0, 3, 3, 5);
    for (i = 0; i < 8; i++) {
        if (!SendMessageA(re, EM_CANUNDO, 0, 0))
            break;
        SendMessageA(re, EM_UNDO, 0, 0);
        wsprintfA(buf, "  undo %d", i + 1);
        row2(re, buf, 0, 3, 3, 5);
    }
    emit("\r\n  Text \"abc\" after the first undo means arming BREAKS the\r\n"
         "  typing run and they are two transactions. Empty means it does\r\n"
         "  not and the whole typing session is one.\r\n\r\n");

    emit("== the arming route, base set rather than typed ==\r\n");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)"abc");
    {
        CHARFORMATA cf;
        CHARRANGE r;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = 0;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
        SendMessageA(re, EM_EMPTYUNDOBUFFER, 0, 0);
        r.cpMin = r.cpMax = 3;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    /* **Nothing is read between arming and typing, and the first version of
     * this read there.** `boldness` selects the range it is asked about and
     * puts the selection back afterwards -- and riched20 discards an armed
     * insertion format when the caret moves, so the act of looking cleared
     * the thing being looked at. The XY came out plain and the honest reading
     * of that run is *my probe*, not the control. Nothing may touch the
     * selection between the arm and the first character. */
    type_run(re, "XY");
    row2(re, "typed XY", 0, 3, 3, 5);
    for (i = 0; i < 8; i++) {
        if (!SendMessageA(re, EM_CANUNDO, 0, 0))
            break;
        SendMessageA(re, EM_UNDO, 0, 0);
        wsprintfA(buf, "  undo %d", i + 1);
        row2(re, buf, 0, 3, 3, 5);
    }
    emit("\r\n  Arming changes no text, so if it pushes no undo step the\r\n"
         "  count here is one lower than above and the shapes differ.\r\n"
         "  Nothing is read between the arm and the typing: looking moves\r\n"
         "  the selection, and moving the caret discards the armed format.\r\n\r\n");

    /* ---- 3. depth ------------------------------------------------------ */
    /* **Typed characters cannot measure depth, because they group.** The
     * first run of this asked for 120 of them and got `1 undo`, which is the
     * grouping answer over again and says nothing about how many steps are
     * kept. Formatting changes are one step each -- shown above -- so the
     * stack is filled with those instead. */
    emit("== depth: how many steps does it keep? ==\r\n");
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM)"abcdef");
    SendMessageA(re, EM_EMPTYUNDOBUFFER, 0, 0);
    for (i = 0; i < 120; i++) {
        CHARFORMATA cf;
        CHARRANGE r;
        r.cpMin = 0;
        r.cpMax = 3;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = (i & 1) ? 0 : CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    {
        int n = 0;
        while (SendMessageA(re, EM_CANUNDO, 0, 0) && n < 400) {
            SendMessageA(re, EM_UNDO, 0, 0);
            n++;
        }
        {
            static char t[512];
            text_of(re, t, sizeof t);
            wsprintfA(buf, "  120 formatting changes, one step each\r\n"
                           "  %d undo(s) before it would not undo further\r\n"
                           "  0..3 is now %s\r\n", n, boldness(re, 0, 3));
            (void)t;
            emit(buf);
        }
        emit("  120 means it kept every step and the limit is higher.\r\n"
             "  Fewer is the depth, and the state left behind is what the\r\n"
             "  steps that fell off the bottom would have restored.\r\n");
    }

    CloseHandle(out_file);
    ExitProcess(0);
}

void WinMainCRTStartup(void) { probe_main(); }
