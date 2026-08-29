/* Ask Windows itself what a control does, instead of inferring it from a
 * picture of one.
 *
 * probe.c walks another program's windows; this one is the other half. It
 * creates the controls *in its own process*, so the messages that take a
 * pointer -- TB_GETITEMRECT, TB_SETBUTTONINFO, MapDialogRect -- are legal,
 * and it writes down the answers. Where a question is about drawing rather
 * than about layout, it puts the control on the screen at a stated pixel and
 * the answer is read off a capture: the report says where the control is, the
 * capture says what was drawn inside it.
 *
 * Three questions it was written for:
 *
 *   1. How wide is a toolbar separator that says nothing, and does saying
 *      something -- iBitmap at add time, or TBIF_SIZE afterwards -- change it?
 *   2. What does TB_SETBUTTONSIZE mean: the button's whole rectangle, or the
 *      part inside its edges?
 *   3. Where in an option button's rectangle does the circle start, and where
 *      does the dialog manager put a control asked for at a given unit?
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o ctlprobe.obj ctlprobe.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o ctlprobe.exe ctlprobe.obj \
 *          -lkernel32 -luser32 -lgdi32 -lcomctl32
 *   tools/vm/pe2k.py ctlprobe.exe        # and its PE header says NT 4.0
 *
 * It writes its report to the path given as the first argument, or Z:\ctl.txt,
 * and then stays up until it is closed so the window can be captured.
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

void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    while (n--)
        *a++ = *b++;
    return d;
}

/* And the stack guard, which the compiler emits calls to whatever -f flag it
 * is handed and which normally lives in the CRT this binary has not got. A
 * probe that has smashed its own stack has nothing true left to say, so the
 * check stands and only its two symbols are supplied here. */
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

/* The controls whose drawing is the question, at pixels this file chooses, so
 * that a capture can be read against the number rather than against a guess.
 * Every one is placed with CreateWindowEx and not by the dialog manager: the
 * circle's column inside the rectangle is the control's business, and mixing
 * the two questions is what left the answer unsettled in the first place. */
static void draw_questions(HWND parent, HFONT font)
{
    static const struct {
        const char *cls;
        DWORD style;
        const char *text;
        int x, y, w, h;
    } c[] = {
        { "BUTTON", BS_AUTORADIOBUTTON | WS_GROUP, "Radio at 100", 100, 10, 120, 14 },
        { "BUTTON", BS_AUTORADIOBUTTON, "Radio at 101", 101, 30, 120, 14 },
        { "BUTTON", BS_AUTOCHECKBOX | WS_GROUP, "Check at 100", 100, 50, 120, 14 },
        { "BUTTON", BS_GROUPBOX, "Group at 100", 100, 70, 120, 40 },
    };
    for (int i = 0; i < (int)(sizeof c / sizeof c[0]); i++) {
        HWND w = CreateWindowExA(0, c[i].cls, c[i].text,
                                 WS_CHILD | WS_VISIBLE | c[i].style,
                                 c[i].x, c[i].y, c[i].w, c[i].h,
                                 parent, (HMENU)(UINT_PTR)(200 + i), NULL, NULL);
        RECT r;
        SendMessageA(w, WM_SETFONT, (WPARAM)font, 0);
        GetWindowRect(w, &r);
        MapWindowPoints(NULL, parent, (POINT *)&r, 2);
        wsprintfA(buf, "control %d %-18s asked %d,%d %dx%d  got %ld,%ld %ldx%ld\r\n",
                  i, c[i].text, c[i].x, c[i].y, c[i].w, c[i].h,
                  r.left, r.top, r.right - r.left, r.bottom - r.top);
        emit(buf);
    }
}

/* What the dialog manager makes of a unit -- which is a different question
 * from where the circle is drawn inside the control it puts there, and the
 * two have to be asked separately or the answer to neither is trustworthy.
 *
 * MapDialogRect is asked of a real dialog, because a plain window has no base
 * units of its own and hands the rectangle back unchanged. The dialog is
 * built here rather than loaded from a resource so that the units the
 * controls are asked for are in this file, next to the pixels they come back
 * as. The two interesting ones are 57 -- Folder Options' "Underline icon
 * titles" -- and 112 and 138, the two option buttons in Find. */
static WCHAR *put_wide(WCHAR *w, const char *s)
{
    while (*s)
        *w++ = (WCHAR)(unsigned char)*s++;
    *w++ = 0;
    return w;
}

static void *align4(void *p)
{
    return (void *)(((UINT_PTR)p + 3) & ~(UINT_PTR)3);
}

static const short unit_x[] = { 7, 8, 57, 58, 112, 138 };

static INT_PTR CALLBACK dlgproc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    (void)wp;
    (void)lp;
    if (m == WM_CLOSE) {
        DestroyWindow(d);
        return TRUE;
    }
    return FALSE;
}

static HWND dialog(void)
{
    static char tmpl[2048];
    DLGTEMPLATE *dt = (DLGTEMPLATE *)tmpl;
    WCHAR *w;
    int i, n = (int)(sizeof unit_x / sizeof unit_x[0]);
    HWND d;

    memset(tmpl, 0, sizeof tmpl);
    dt->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | DS_SETFONT |
                DS_MODALFRAME;
    dt->dwExtendedStyle = 0;
    dt->cdit = (WORD)(n + 1);
    dt->x = 0;
    dt->y = 0;
    dt->cx = 200;
    dt->cy = 100;
    w = (WCHAR *)(dt + 1);
    *w++ = 0;                       /* no menu */
    *w++ = 0;                       /* the standard dialog class */
    w = put_wide(w, "ctlprobe units");
    *w++ = 8;                       /* the point size DS_SETFONT asks for */
    w = put_wide(w, "MS Sans Serif");

    for (i = 0; i <= n; i++) {
        DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)align4(w);
        it->style = WS_CHILD | WS_VISIBLE |
                    (i == n ? BS_AUTOCHECKBOX : BS_AUTORADIOBUTTON) |
                    (i == 0 ? WS_GROUP : 0);
        it->dwExtendedStyle = 0;
        it->x = i == n ? 7 : unit_x[i];
        it->y = (short)(8 + i * 12);
        it->cx = 60;
        it->cy = 10;
        it->id = (WORD)(400 + i);
        w = (WCHAR *)(it + 1);
        *w++ = 0xFFFF;              /* a class by atom */
        *w++ = 0x0080;              /* and the atom is BUTTON */
        w = put_wide(w, i == n ? "Check at unit 7" : "Option");
        *w++ = 0;                   /* no creation data */
    }

    d = CreateDialogIndirectParamA(GetModuleHandleA(NULL), dt, NULL, dlgproc, 0);
    if (!d) {
        wsprintfA(buf, "the dialog would not be created: %lu\r\n",
                  GetLastError());
        emit(buf);
        return NULL;
    }
    SetWindowPos(d, HWND_TOP, 40, 300, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    return d;
}

static void units(HWND parent, HWND d)
{
    LONG base = GetDialogBaseUnits();
    POINT o;
    RECT r;
    int u, i;
    emit("== dialog units ==\r\n");
    wsprintfA(buf, "GetDialogBaseUnits x=%d y=%d\r\n",
              (int)LOWORD(base), (int)HIWORD(base));
    emit(buf);
    o.x = o.y = 0;
    ClientToScreen(parent, &o);
    wsprintfA(buf, "the plain window's client origin on screen %ld,%ld\r\n",
              o.x, o.y);
    emit(buf);
    if (!d)
        return;
    o.x = o.y = 0;
    ClientToScreen(d, &o);
    wsprintfA(buf, "the dialog's client origin on screen %ld,%ld\r\n", o.x, o.y);
    emit(buf);
    for (u = 0; u <= 24; u++) {
        r.left = u;
        r.top = 0;
        r.right = u + 1;
        r.bottom = 1;
        MapDialogRect(d, &r);
        wsprintfA(buf, "unit %2d -> %ld%s", u, r.left, (u % 8) == 7 ? "\r\n" : "  ");
        emit(buf);
    }
    emit("\r\n");
    for (i = 0; i <= (int)(sizeof unit_x / sizeof unit_x[0]); i++) {
        HWND c = GetDlgItem(d, 400 + i);
        RECT cr;
        if (!c)
            continue;
        GetWindowRect(c, &cr);
        MapWindowPoints(NULL, d, (POINT *)&cr, 2);
        wsprintfA(buf, "item %d asked unit %d -> client %ld,%ld %ldx%ld"
                       "  screen x %ld\r\n",
                  i, i == (int)(sizeof unit_x / sizeof unit_x[0]) ? 7 : unit_x[i],
                  cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top,
                  cr.left + o.x);
        emit(buf);
    }
}

/* The toolbar's own arithmetic, asked of the toolbar. Five separators, each
 * told something different, and eleven buttons of a stated size: what comes
 * back from TB_GETITEMRECT is what win32 means by both. */
static void toolbar(HWND parent, int size_cx)
{
    TBBUTTON b[16];
    HWND tb;
    int n = 0, i;
    memset(b, 0, sizeof b);
    for (i = 0; i < 3; i++) {
        b[n].iBitmap = i;
        b[n].idCommand = 100 + i;
        b[n].fsState = TBSTATE_ENABLED;
        b[n].fsStyle = TBSTYLE_BUTTON;
        n++;
    }
    b[n].iBitmap = 0; /* a separator that says nothing */
    b[n].fsStyle = TBSTYLE_SEP;
    n++;
    b[n].iBitmap = 3;
    b[n].idCommand = 110;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].iBitmap = 14; /* and one that asks for fourteen: a value the default
                        * cannot be confused with, which eight was */
    b[n].fsStyle = TBSTYLE_SEP;
    n++;
    b[n].iBitmap = 4;
    b[n].idCommand = 111;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].iBitmap = 0; /* one more that says nothing, to be told afterwards */
    b[n].fsStyle = TBSTYLE_SEP;
    n++;
    b[n].iBitmap = 5;
    b[n].idCommand = 112;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;

    tb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
                         WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_NODIVIDER |
                         CCS_NORESIZE | CCS_NOPARENTALIGN,
                         0, 130, 400, 30, parent, (HMENU)(UINT_PTR)300, NULL, NULL);
    SendMessageA(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageA(tb, TB_SETBITMAPSIZE, 0, MAKELPARAM(16, 16));
    SendMessageA(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(size_cx, 22));
    SendMessageA(tb, TB_ADDBUTTONS, (WPARAM)n, (LPARAM)b);

    wsprintfA(buf, "== toolbar, TB_SETBUTTONSIZE cx=%d ==\r\n", size_cx);
    emit(buf);
    for (i = 0; i < n; i++) {
        RECT r;
        r.left = r.top = r.right = r.bottom = 0;
        SendMessageA(tb, TB_GETITEMRECT, (WPARAM)i, (LPARAM)&r);
        wsprintfA(buf, "item %2d %-10s iBitmap=%d  %ld,%ld %ldx%ld\r\n", i,
                  (b[i].fsStyle & TBSTYLE_SEP) ? "separator" : "button",
                  b[i].iBitmap, r.left, r.top, r.right - r.left,
                  r.bottom - r.top);
        emit(buf);
    }
    {   /* and now the last separator is told a width to its face */
        TBBUTTONINFOA bi;
        RECT r;
        memset(&bi, 0, sizeof bi);
        bi.cbSize = sizeof bi;
        bi.dwMask = TBIF_SIZE | TBIF_BYINDEX;
        bi.cx = 20;
        SendMessageA(tb, TB_SETBUTTONINFOA, (WPARAM)7, (LPARAM)&bi);
        r.left = r.top = r.right = r.bottom = 0;
        SendMessageA(tb, TB_GETITEMRECT, 7, (LPARAM)&r);
        wsprintfA(buf, "item  7 after TBIF_SIZE cx=20  %ld,%ld %ldx%ld\r\n",
                  r.left, r.top, r.right - r.left, r.bottom - r.top);
        emit(buf);
        r.left = r.top = r.right = r.bottom = 0;
        SendMessageA(tb, TB_GETITEMRECT, 8, (LPARAM)&r);
        wsprintfA(buf, "item  8 after that            %ld,%ld %ldx%ld\r\n",
                  r.left, r.top, r.right - r.left, r.bottom - r.top);
        emit(buf);
    }
}

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

static LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

static void probe_main(void);
void mainCRTStartup(void) { probe_main(); }
void wWinMainCRTStartup(void) { probe_main(); }
void WinMainCRTStartup(void) { probe_main(); }

static void probe_main(void)
{
    static char path_buf[512];
    char *path = arg(1, path_buf, sizeof path_buf);
    WNDCLASSA wc;
    HWND w, dlg;
    MSG msg;
    HFONT font;
    NONCLIENTMETRICSA ncm;

    InitCommonControls();
    out_file = CreateFileA(path ? path : "Z:\\ctl.txt", GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ctlprobe";
    RegisterClassA(&wc);
    w = CreateWindowExA(0, "ctlprobe", "ctlprobe",
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 460, 230,
                        NULL, NULL, wc.hInstance, NULL);

    /* The same font the dialogs use, because a control's size is measured in
     * it and this window is not a dialog. */
    /* Windows 2000's NONCLIENTMETRICS is one field shorter than the header
     * this is compiled against declares -- iPaddedBorderWidth arrived with
     * Vista -- and a cbSize it does not recognise makes the call fail and
     * leave the struct as it was. Ask for the older size. */
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm - sizeof(int);
    if (!SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) {
        ncm.cbSize = sizeof ncm;
        SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    }
    font = CreateFontIndirectA(&ncm.lfMessageFont);
    wsprintfA(buf, "== font ==\r\nmessage font \"%s\" height %ld weight %ld\r\n",
              ncm.lfMessageFont.lfFaceName, ncm.lfMessageFont.lfHeight,
              ncm.lfMessageFont.lfWeight);
    emit(buf);

    emit("== controls, placed at pixels ==\r\n");
    draw_questions(w, font);
    units(w, dlg = dialog());
    toolbar(w, 23);
    CloseHandle(out_file);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
