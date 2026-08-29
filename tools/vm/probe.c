/* Ask the guest what its windows really are, instead of measuring a picture.
 *
 * A screenshot says where an edge is; it does not say which control drew it,
 * what class it is, or what style it has. This walks a running application's
 * window tree and its menu and writes the answer to a file on the share.
 *
 * Built for Windows 2000 with no C runtime at all -- mingw's CRT imports the
 * api-ms-win-crt-* stubs, which that version has never heard of. Compiling
 * and linking are separate steps because the two need opposite things: the
 * compiler wants the headers, and only the linker is to be told there is no
 * runtime.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o probe.obj probe.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,console \
 *          -Wl,--entry,mainCRTStartup -o probe.exe probe.obj \
 *          -lkernel32 -luser32 -lcomctl32
 *   tools/vm/pe2k.py probe.exe        # and its PE header says NT 4.0
 *
 * `-nostartfiles -nodefaultlibs` in one step was enough for an older zig and
 * is not for this one: it links its own crt2.obj anyway and the two entry
 * points below collide with it.
 */
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>

/* With no CRT, the two the compiler still calls for itself. */
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

/* And the stack guard, whose two symbols normally come from the CRT. A probe
 * that has smashed its own stack has nothing true left to say, so the check
 * stands and only the symbols are supplied. */
void *__stack_chk_guard = (void *)0x0bad57ac;

void __stack_chk_fail(void)
{
    ExitProcess(3);
}

static HANDLE out_file;
static char buf[2048];

static void emit(const char *s)
{
    DWORD n;
    WriteFile(out_file, s, (DWORD)lstrlenA(s), &n, NULL);
}

static void dump(HWND w, int depth)
{
    char cls[128], txt[256], pad[64];
    RECT r;
    int i;
    for (i = 0; i < depth * 2 && i < 63; i++)
        pad[i] = ' ';
    pad[i] = 0;
    cls[0] = txt[0] = 0;
    GetClassNameA(w, cls, sizeof cls);
    GetWindowTextA(w, txt, sizeof txt);
    GetWindowRect(w, &r);
    /* The parent, because EnumChildWindows walks every descendant and hands
     * them all back at one level: without this the tree reads as if a combo
     * box inside a toolbar were a child of the frame. */
    wsprintfA(buf, "%s%08lX parent=%08lX %-22s %4ld,%-4ld %4ldx%-4ld "
                   "style=%08lX ex=%08lX id=%ld \"%s\"\r\n",
              pad, (unsigned long)(UINT_PTR)w,
              (unsigned long)(UINT_PTR)GetParent(w), cls,
              r.left, r.top, r.right - r.left, r.bottom - r.top,
              GetWindowLongA(w, GWL_STYLE), GetWindowLongA(w, GWL_EXSTYLE),
              GetWindowLongA(w, GWL_ID), txt);
    emit(buf);
}

/* What this can and cannot ask across processes.
 *
 * Everything below reads a window through USER32 -- GetClassName,
 * GetWindowRect, GetWindowLong, GetWindowText, and the menu calls -- and all
 * of those are marshalled between processes by Windows itself. What is *not*
 * safe is a control message that takes a pointer: SB_GETPARTS, SB_GETTEXT,
 * TB_GETBUTTON, TB_GETITEMRECT and their kind hand the receiving process an
 * address, and an address here means nothing there. This probe asked
 * WordPad's status bar for its parts and Windows answered with "WORDPAD.exe
 * has generated errors and will be closed" -- twice, once for the status bar
 * and once for the toolbar. A control's insides are measured off the pixels
 * instead, or by a program running inside that process. */

/* ...but a control message that takes *no* pointer is safe, and there is a
 * useful family of them. The answer comes back in the return value, nothing
 * is marshalled, and what they report is how the program *configured* its
 * control rather than how a control we built behaves. That is the difference
 * between "comctl32 does this" and "MFC asked for this", which is exactly
 * what a picture cannot tell you. */
static void ask_toolbar(HWND w)
{
    LRESULT size = SendMessageA(w, TB_GETBUTTONSIZE, 0, 0);
    LRESULT pad = SendMessageA(w, TB_GETPADDING, 0, 0);
    emit("    -- asked, no pointer either way --\r\n");
    wsprintfA(buf, "    button size %dx%d   padding %dx%d\r\n",
              (int)LOWORD(size), (int)HIWORD(size), (int)LOWORD(pad),
              (int)HIWORD(pad));
    emit(buf);
    wsprintfA(buf, "    buttons %d   rows %d   text rows %d\r\n",
              (int)SendMessageA(w, TB_BUTTONCOUNT, 0, 0),
              (int)SendMessageA(w, TB_GETROWS, 0, 0),
              (int)SendMessageA(w, TB_GETTEXTROWS, 0, 0));
    emit(buf);
    wsprintfA(buf, "    bitmap flags %08lX   style %08lX   image list %s\r\n",
              (unsigned long)SendMessageA(w, TB_GETBITMAPFLAGS, 0, 0),
              (unsigned long)SendMessageA(w, TB_GETSTYLE, 0, 0),
              SendMessageA(w, TB_GETIMAGELIST, 0, 0) ? "set" : "none");
    emit(buf);
}

static void ask_status(HWND w)
{
    /* SB_GETPARTS with a null pointer is the documented way to ask how many
     * parts there are, and it is the one status-bar getter that hands nothing
     * across. */
    int n = (int)SendMessageA(w, SB_GETPARTS, 0, 0);
    int i;
    emit("    -- asked, no pointer either way --\r\n");
    wsprintfA(buf, "    parts %d   simple %d\r\n", n,
              (int)SendMessageA(w, SB_ISSIMPLE, 0, 0));
    emit(buf);
    for (i = 0; i < n && i < 8; i++) {
        LRESULT len = SendMessageA(w, SB_GETTEXTLENGTHA, (WPARAM)i, 0);
        wsprintfA(buf, "    part %d text %d chars, type %04X\r\n", i,
                  (int)LOWORD(len), (int)HIWORD(len));
        emit(buf);
    }
}

/* A rich edit's own getters that hand nothing across a process boundary.
 * EM_GETMARGINS answers in its return value -- left in the low word, right
 * in the high one -- which is what decides whether a frame moved its text
 * with a margin or with a paragraph indent. The indent needs a PARAFORMAT
 * and a pointer, so it cannot be asked from here; the Paragraph box shows
 * it instead. */
static void ask_richedit(HWND w)
{
    LRESULT m = SendMessageA(w, EM_GETMARGINS, 0, 0);
    emit("    -- asked, no pointer either way --\r\n");
    wsprintfA(buf, "    margins left %d right %d\r\n", (int)LOWORD(m),
              (int)HIWORD(m));
    emit(buf);
    wsprintfA(buf, "    text length %ld   lines %ld   first visible %ld\r\n",
              SendMessageA(w, WM_GETTEXTLENGTH, 0, 0),
              SendMessageA(w, EM_GETLINECOUNT, 0, 0),
              SendMessageA(w, EM_GETFIRSTVISIBLELINE, 0, 0));
    emit(buf);
    wsprintfA(buf, "    event mask %08lX   modified %ld\r\n",
              (unsigned long)SendMessageA(w, EM_GETEVENTMASK, 0, 0),
              SendMessageA(w, EM_GETMODIFY, 0, 0));
    emit(buf);
}

static BOOL CALLBACK child(HWND w, LPARAM lp)
{
    char cls[64];
    cls[0] = 0;
    dump(w, (int)lp);
    GetClassNameA(w, cls, sizeof cls);
    if (lstrcmpiA(cls, TOOLBARCLASSNAMEA) == 0)
        ask_toolbar(w);
    else if (lstrcmpiA(cls, STATUSCLASSNAMEA) == 0)
        ask_status(w);
    else if (lstrcmpiA(cls, "RichEdit20W") == 0 ||
             lstrcmpiA(cls, "RichEdit20A") == 0 ||
             lstrcmpiA(cls, "RICHEDIT") == 0)
        ask_richedit(w);
    return TRUE;
}

static void menus(HMENU m, int depth)
{
    int i, n = GetMenuItemCount(m);
    char s[128], pad[64];
    for (i = 0; i < depth * 2 && i < 63; i++)
        pad[i] = ' ';
    pad[i] = 0;
    for (i = 0; i < n; i++) {
        MENUITEMINFOA mi;
        HMENU sub;
        s[0] = 0;
        GetMenuStringA(m, i, s, sizeof s, MF_BYPOSITION);
        mi.cbSize = sizeof mi;
        mi.fMask = MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_TYPE;
        mi.dwTypeData = NULL;
        mi.cch = 0;
        mi.hSubMenu = NULL;
        GetMenuItemInfoA(m, i, TRUE, &mi);
        sub = GetSubMenu(m, i);
        wsprintfA(buf, "%sitem %2d id=%-5u state=%04X type=%04X \"%s\"\r\n",
                  pad, i, mi.wID, mi.fState, mi.fType, s);
        emit(buf);
        if (sub)
            menus(sub, depth + 1);
    }
}

/* argv without a CRT: the command line, split on spaces, quotes honoured.
 * The caller supplies the buffer -- one static one, shared, would mean the
 * second argument quietly overwrote the first. */
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

static void probe_main(void);

/* Both entry names, because which one the linker looks for depends on the
 * subsystem it picked, and this binary has no CRT to decide it. */
void mainCRTStartup(void) { probe_main(); }
void wWinMainCRTStartup(void) { probe_main(); }

static void probe_main(void)
{
    static char title_buf[512], path_buf[512];
    char *title = arg(1, title_buf, sizeof title_buf);
    char *path = arg(2, path_buf, sizeof path_buf);
    HWND top;
    RECT c;
    if (!title)
        title = "untitled - Paint";
    out_file = CreateFileA(path ? path : "Z:\\probe.txt", GENERIC_WRITE, 0,
                           NULL, CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    top = FindWindowA(NULL, title);
    if (!top) {
        wsprintfA(buf, "no window titled \"%s\"\r\n", title);
        emit(buf);
        CloseHandle(out_file);
        ExitProcess(2);
    }
    emit("== windows ==\r\n");
    dump(top, 0);
    EnumChildWindows(top, child, 1);
    GetClientRect(top, &c);
    wsprintfA(buf, "client %ldx%ld\r\n", c.right, c.bottom);
    emit(buf);
    wsprintfA(buf, "metrics cxframe=%d cyframe=%d cycaption=%d cymenu=%d "
                   "cxvscroll=%d cyhscroll=%d cxborder=%d cxsmicon=%d "
                   "cxedge=%d cyedge=%d cxscreen=%d cyscreen=%d\r\n",
              GetSystemMetrics(SM_CXFRAME), GetSystemMetrics(SM_CYFRAME),
              GetSystemMetrics(SM_CYCAPTION), GetSystemMetrics(SM_CYMENU),
              GetSystemMetrics(SM_CXVSCROLL), GetSystemMetrics(SM_CYHSCROLL),
              GetSystemMetrics(SM_CXBORDER), GetSystemMetrics(SM_CXSMICON),
              GetSystemMetrics(SM_CXEDGE), GetSystemMetrics(SM_CYEDGE),
              GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    emit(buf);
    emit("== menu ==\r\n");
    menus(GetMenu(top), 0);
    CloseHandle(out_file);
    ExitProcess(0);
}
