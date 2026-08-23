/* Ask the guest what its windows really are, instead of measuring a picture.
 *
 * A screenshot says where an edge is; it does not say which control drew it,
 * what class it is, or what style it has. This walks a running application's
 * window tree and its menu and writes the answer to a file on the share.
 *
 * Built for Windows 2000 with no C runtime at all -- mingw's CRT imports the
 * api-ms-win-crt-* stubs, which that version has never heard of:
 *
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,console \
 *          -o probe.exe probe.c -lkernel32 -luser32
 *   tools/vm/pe2k.py probe.exe        # and its PE header says NT 4.0
 */
#include <windows.h>

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
    wsprintfA(buf, "%s%08lX %-22s %4ld,%-4ld %4ldx%-4ld style=%08lX ex=%08lX id=%ld \"%s\"\r\n",
              pad, (unsigned long)(UINT_PTR)w, cls,
              r.left, r.top, r.right - r.left, r.bottom - r.top,
              GetWindowLongA(w, GWL_STYLE), GetWindowLongA(w, GWL_EXSTYLE),
              GetWindowLongA(w, GWL_ID), txt);
    emit(buf);
}

/* A status bar knows where its parts end and what is in them, and will say
 * so if asked -- which beats measuring the sunken edges off a screenshot. */
static void status_parts(HWND w)
{
    int parts[16], n, i;
    n = (int)SendMessageA(w, SB_GETPARTS, 16, (LPARAM)parts);
    for (i = 0; i < n && i < 16; i++) {
        char text[256];
        text[0] = 0;
        SendMessageA(w, SB_GETTEXTA, (WPARAM)i, (LPARAM)text);
        wsprintfA(buf, "    part %d right=%d \"%s\"\r\n", i, parts[i], text);
        emit(buf);
    }
}

static BOOL CALLBACK child(HWND w, LPARAM lp)
{
    char cls[64];
    cls[0] = 0;
    dump(w, (int)lp);
    GetClassNameA(w, cls, sizeof cls);
    if (lstrcmpiA(cls, "msctls_statusbar32") == 0)
        status_parts(w);
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
