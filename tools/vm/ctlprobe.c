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
 *   4. What a Rich Edit 2.0 does with runs of formatting: where a run's
 *      boundaries end up, whether identical neighbours are merged, what
 *      EM_GETCHARFORMAT answers over a selection that spans two, and what a
 *      character typed at a boundary takes its formatting from. Those decide
 *      the shape of a text model rather than a pixel, which is why they are
 *      asked of riched20 rather than reasoned about.
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

        /* One field or two? The rectangle is the same either way, so it
         * cannot tell us -- and an implementation still has to choose. If
         * comctl32 keeps a separator's width *in* iBitmap, which is where it
         * reads that width from at add time, then TBIF_SIZE wrote there and
         * TB_GETBUTTON will say 20. If they are two fields with the later set
         * winning, iBitmap is still the 0 it was added with. */
        {
            TBBUTTON g;
            TBBUTTONINFOA q;
            memset(&g, 0, sizeof g);
            SendMessageA(tb, TB_GETBUTTON, 7, (LPARAM)&g);
            wsprintfA(buf, "item  7 TB_GETBUTTON iBitmap=%d style=%02x\r\n",
                      g.iBitmap, g.fsStyle);
            emit(buf);
            memset(&q, 0, sizeof q);
            q.cbSize = sizeof q;
            q.dwMask = TBIF_SIZE | TBIF_IMAGE | TBIF_BYINDEX;
            SendMessageA(tb, TB_GETBUTTONINFOA, (WPARAM)7, (LPARAM)&q);
            wsprintfA(buf, "item  7 TB_GETBUTTONINFO cx=%d iImage=%d\r\n",
                      q.cx, q.iImage);
            emit(buf);
            /* And the same question from the other side: a separator told
             * through iBitmap and never through TBIF_SIZE. If they are one
             * field, this one's cx comes back 14. */
            memset(&q, 0, sizeof q);
            q.cbSize = sizeof q;
            q.dwMask = TBIF_SIZE | TBIF_IMAGE | TBIF_BYINDEX;
            SendMessageA(tb, TB_GETBUTTONINFOA, (WPARAM)5, (LPARAM)&q);
            wsprintfA(buf, "item  5 TB_GETBUTTONINFO cx=%d iImage=%d\r\n",
                      q.cx, q.iImage);
            emit(buf);
        }
    }
}

/* What a button *looks* like in each of its states, which no rectangle can
 * say. Two bars, because the answer differs between them and WordPad needs
 * the one the shell does not use:
 *
 *   the flat bar   TBSTYLE_FLAT, which is explorer's -- a button wears no
 *                  edge until the pointer is on it
 *   the classic    no TBSTYLE_FLAT, which is WordPad's -- every button wears
 *                  a raised edge all the time, so "hot" has nowhere obvious
 *                  left to go, and whether it goes anywhere is the question
 *
 * Four buttons on each: ordinary, hot, checked, disabled. The images are
 * comctl32's own standard set, so the art is Windows' and not something this
 * file drew. The rectangles are emitted so a capture is read against numbers
 * rather than counted along by eye.
 */
static void barstates(HWND parent)
{
    static const struct { const char *what; DWORD extra; int y; } bars[2] = {
        { "flat", TBSTYLE_FLAT, 170 },
        { "classic", 0, 205 },
    };
    int k;

    for (k = 0; k < 2; k++) {
        TBBUTTON b[4];
        TBADDBITMAP ab;
        HWND tb;
        int i;

        memset(b, 0, sizeof b);
        for (i = 0; i < 4; i++) {
            b[i].iBitmap = i;
            b[i].idCommand = 400 + k * 10 + i;
            b[i].fsStyle = i == 2 ? TBSTYLE_CHECK : TBSTYLE_BUTTON;
            /* the fourth is the disabled one: no TBSTATE_ENABLED */
            b[i].fsState = i == 3 ? 0 : TBSTATE_ENABLED;
            if (i == 2)
                b[i].fsState |= TBSTATE_CHECKED;
        }

        tb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
                             WS_CHILD | WS_VISIBLE | CCS_NODIVIDER |
                             CCS_NORESIZE | CCS_NOPARENTALIGN | bars[k].extra,
                             0, bars[k].y, 400, 30, parent,
                             (HMENU)(UINT_PTR)(310 + k), NULL, NULL);
        SendMessageA(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageA(tb, TB_SETBITMAPSIZE, 0, MAKELPARAM(16, 16));
        SendMessageA(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(23, 22));
        ab.hInst = HINST_COMMCTRL;
        ab.nID = IDB_STD_SMALL_COLOR;
        SendMessageA(tb, TB_ADDBITMAP, 0, (LPARAM)&ab);
        SendMessageA(tb, TB_ADDBUTTONS, 4, (LPARAM)b);
        /* The hot one is the second, and it is set rather than hovered: a
         * capture taken with the pointer somewhere is a capture that also
         * says where the pointer was. */
        SendMessageA(tb, TB_SETHOTITEM, 1, 0);

        wsprintfA(buf, "== %s bar states, at window y=%d ==\r\n",
                  bars[k].what, bars[k].y);
        emit(buf);
        for (i = 0; i < 4; i++) {
            static const char *what[4] = { "ordinary", "hot", "checked",
                                           "disabled" };
            RECT r;
            r.left = r.top = r.right = r.bottom = 0;
            SendMessageA(tb, TB_GETITEMRECT, (WPARAM)i, (LPARAM)&r);
            wsprintfA(buf, "  %-8s  %ld,%ld %ldx%ld\r\n", what[i],
                      r.left, r.top, r.right - r.left, r.bottom - r.top);
            emit(buf);
        }
        wsprintfA(buf, "  hot item is now %d\r\n",
                  (int)SendMessageA(tb, TB_GETHOTITEM, 0, 0));
        emit(buf);
    }

    /* Where in the bar's height does the button sit? ween32 centres it, which
     * is right for a bar exactly a button tall and cannot be told apart from
     * anything else there. Four heights, both styles: centring gives
     * (h-22)/2 and a fixed inset gives the same number every time. */
    {
        static const int heights[4] = { 22, 26, 30, 32 };
        int k, j;
        for (k = 0; k < 2; k++) {
            for (j = 0; j < 4; j++) {
                TBBUTTON b;
                HWND tb;
                RECT r;
                memset(&b, 0, sizeof b);
                b.iBitmap = 0;
                b.idCommand = 500 + k * 10 + j;
                b.fsState = TBSTATE_ENABLED;
                b.fsStyle = TBSTYLE_BUTTON;
                tb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
                                     WS_CHILD | CCS_NODIVIDER | CCS_NORESIZE |
                                     CCS_NOPARENTALIGN |
                                     (k ? 0 : TBSTYLE_FLAT),
                                     0, 0, 200, heights[j], parent,
                                     (HMENU)(UINT_PTR)(330 + k * 4 + j), NULL,
                                     NULL);
                SendMessageA(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
                SendMessageA(tb, TB_SETBITMAPSIZE, 0, MAKELPARAM(16, 16));
                SendMessageA(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(23, 22));
                SendMessageA(tb, TB_ADDBUTTONS, 1, (LPARAM)&b);
                r.left = r.top = r.right = r.bottom = 0;
                SendMessageA(tb, TB_GETITEMRECT, 0, (LPARAM)&r);
                wsprintfA(buf, "  %-7s bar %2d tall -> button y=%ld h=%ld\r\n",
                          k ? "classic" : "flat", heights[j], r.top,
                          r.bottom - r.top);
                emit(buf);
                DestroyWindow(tb);
            }
        }
        /* And the same classic bar *without* CCS_NODIVIDER, which is what
         * WordPad's frame creates: if the divider costs two rows, the button
         * moves down by two and the two grey-and-white rows appear above it. */
        for (j = 0; j < 4; j++) {
            TBBUTTON b;
            HWND tb;
            RECT r;
            memset(&b, 0, sizeof b);
            b.iBitmap = 0;
            b.idCommand = 520 + j;
            b.fsState = TBSTATE_ENABLED;
            b.fsStyle = TBSTYLE_BUTTON;
            tb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
                                 WS_CHILD | CCS_NORESIZE | CCS_NOPARENTALIGN,
                                 0, 0, 200, heights[j], parent,
                                 (HMENU)(UINT_PTR)(340 + j), NULL, NULL);
            SendMessageA(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
            SendMessageA(tb, TB_SETBITMAPSIZE, 0, MAKELPARAM(16, 16));
            SendMessageA(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(23, 22));
            SendMessageA(tb, TB_ADDBUTTONS, 1, (LPARAM)&b);
            r.left = r.top = r.right = r.bottom = 0;
            SendMessageA(tb, TB_GETITEMRECT, 0, (LPARAM)&r);
            wsprintfA(buf, "  divider bar %2d tall -> button y=%ld h=%ld\r\n",
                      heights[j], r.top, r.bottom - r.top);
            emit(buf);
            DestroyWindow(tb);
        }
        /* And the case the sweep above cannot see, because every button in it
         * is as tall as the button size given: a button *shorter* than the
         * bar. explorer's menu band is 19 in a bar of 22, and if the inset is
         * a plain 0 that button sits at the top with three spare rows under
         * it; if anything centres, it sits at 1. */
        for (k = 0; k < 2; k++) {
            TBBUTTON b;
            HWND tb;
            RECT r;
            memset(&b, 0, sizeof b);
            b.iBitmap = -1;
            b.idCommand = 560 + k;
            b.fsState = TBSTATE_ENABLED;
            b.fsStyle = TBSTYLE_BUTTON;
            b.iString = (INT_PTR)"File";
            tb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
                                 WS_CHILD | CCS_NODIVIDER | CCS_NORESIZE |
                                 CCS_NOPARENTALIGN | TBSTYLE_LIST |
                                 (k ? 0 : TBSTYLE_FLAT),
                                 0, 0, 200, 22, parent,
                                 (HMENU)(UINT_PTR)(350 + k), NULL, NULL);
            SendMessageA(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
            SendMessageA(tb, TB_SETPADDING, 0, MAKELPARAM(16, 0));
            SendMessageA(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(0, 19));
            SendMessageA(tb, TB_ADDBUTTONS, 1, (LPARAM)&b);
            r.left = r.top = r.right = r.bottom = 0;
            SendMessageA(tb, TB_GETITEMRECT, 0, (LPARAM)&r);
            wsprintfA(buf, "  %-7s menu-band button, bar 22 -> y=%ld h=%ld\r\n",
                      k ? "classic" : "flat", r.top, r.bottom - r.top);
            emit(buf);
            DestroyWindow(tb);
        }
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

/* ---- Rich Edit 2.0: what the runs do -------------------------------------
 *
 * Four questions, none of which a picture can answer, and all of which
 * decide the shape of a text model rather than a pixel. The control is
 * created here rather than found, so every answer is riched20's own.
 *
 * The RTF the control streams out is the clearest of the instruments: it is
 * the run structure written down, so where a `\b' opens and closes says
 * where a run begins and ends, and two runs that were merged come out as one
 * group rather than two.
 */

static char rtf_buf[4096];
static int rtf_len;

static DWORD CALLBACK rtf_out(DWORD_PTR cookie, LPBYTE bytes, LONG cb,
                              LONG *written)
{
    LONG i;
    (void)cookie;
    for (i = 0; i < cb; i++)
        if (rtf_len < (int)sizeof rtf_buf - 1)
            rtf_buf[rtf_len++] = (char)bytes[i];
    rtf_buf[rtf_len] = 0;
    *written = cb;
    return 0;
}

/* The formatting in force over a range, and which of its bits the control
 * says are the same throughout it: dwMask is what it is sure of. */
static void charfmt_of(HWND re, int from, int to, const char *what)
{
    CHARFORMATA cf;
    CHARRANGE cr;
    cr.cpMin = from;
    cr.cpMax = to;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    wsprintfA(buf,
              "  %-28s %2d..%-2d  mask %08lx  effects %08lx  bold %s  "
              "italic %s  size %ld  face \"%s\"\r\n",
              what, from, to, cf.dwMask, cf.dwEffects,
              (cf.dwMask & CFM_BOLD) ? ((cf.dwEffects & CFE_BOLD) ? "on" : "off")
                                     : "MIXED",
              (cf.dwMask & CFM_ITALIC)
                  ? ((cf.dwEffects & CFE_ITALIC) ? "on" : "off")
                  : "MIXED",
              (cf.dwMask & CFM_SIZE) ? cf.yHeight : -1,
              (cf.dwMask & CFM_FACE) ? cf.szFaceName : "MIXED");
    emit(buf);
}

static void set_bold(HWND re, int from, int to, int on)
{
    CHARFORMATA cf;
    CHARRANGE cr;
    cr.cpMin = from;
    cr.cpMax = to;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_BOLD;
    cf.dwEffects = on ? CFE_BOLD : 0;
    SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void dump_rtf(HWND re, const char *what)
{
    EDITSTREAM es;
    int i;
    rtf_len = 0;
    rtf_buf[0] = 0;
    memset(&es, 0, sizeof es);
    es.pfnCallback = rtf_out;
    SendMessageA(re, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    wsprintfA(buf, "  -- RTF after %s --\r\n", what);
    emit(buf);
    /* The header is boilerplate; what matters is from the first \pard on,
     * and the line breaks in it are the control's own. */
    for (i = 0; i < rtf_len; i++)
        if (rtf_buf[i] == '\\' && rtf_buf[i + 1] == 'p' &&
            rtf_buf[i + 2] == 'a' && rtf_buf[i + 3] == 'r' &&
            rtf_buf[i + 4] == 'd')
            break;
    if (i >= rtf_len)
        i = 0;
    emit("  ");
    emit(rtf_buf + i);
    emit("\r\n");
}

/* Down twice over a line too short for the column the caret started in,
 * asked of both text controls -- because what Windows does with a remembered
 * column is the same question for each and neither of ween32's does it. */
/* Where the caret is, and how far along the line in pixels -- which is what
 * says whether a control remembers a column or an x. EM_POSFROMCHAR answers
 * differently in the two controls: an EDIT packs the point into the return,
 * a rich edit fills in a POINTL the caller passes. */
static void caret_here(HWND w, const char *cls, const char *when, int rich)
{
    DWORD from = 0, to = 0;
    int line, start, x;
    SendMessageA(w, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
    line = (int)SendMessageA(w, EM_LINEFROMCHAR, (WPARAM)to, 0);
    start = (int)SendMessageA(w, EM_LINEINDEX, (WPARAM)line, 0);
    if (rich) {
        POINTL pt;
        pt.x = pt.y = 0;
        SendMessageA(w, EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)to);
        x = pt.x;
    } else {
        LRESULT r = SendMessageA(w, EM_POSFROMCHAR, (WPARAM)to, 0);
        x = (short)LOWORD(r);
    }
    wsprintfA(buf, "  %-12s %-16s caret %lu  line %d  column %lu  x %d\r\n",
              cls, when, to, line, to - (DWORD)start, x);
    emit(buf);
}

static void column_walk(HWND parent, HFONT font, const char *cls, int rich)
{
    /* Visible, because an invisible control has not laid anything out and
     * cannot take the keyboard -- which is how the first run of this got two
     * numbers that meant nothing. */
    HWND w = CreateWindowExA(WS_EX_CLIENTEDGE, cls, "",
                             WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                 ES_AUTOVSCROLL,
                             10, 10, 300, 100, parent, NULL, NULL, NULL);
    if (!w) {
        wsprintfA(buf, "  %s would not be created: %lu\r\n", cls,
                  GetLastError());
        emit(buf);
        return;
    }
    SendMessageA(w, WM_SETFONT, (WPARAM)font, FALSE);
    SetWindowTextA(w, "long line here\r\nshort\r\nlong line again");
    SetFocus(w);
    wsprintfA(buf, "  %-12s focus %s\r\n", cls,
              GetFocus() == w ? "taken" : "NOT TAKEN");
    emit(buf);
    SendMessageA(w, EM_SETSEL, 12, 12); /* column 12 of the first line */
    caret_here(w, cls, "at the start", rich);
    SendMessageA(w, WM_KEYDOWN, VK_DOWN, 0);
    caret_here(w, cls, "after one Down", rich);
    SendMessageA(w, WM_KEYDOWN, VK_DOWN, 0);
    caret_here(w, cls, "after two Downs", rich);
    SendMessageA(w, WM_KEYDOWN, VK_UP, 0);
    SendMessageA(w, WM_KEYDOWN, VK_UP, 0);
    caret_here(w, cls, "and back up twice", rich);
    DestroyWindow(w);
}

static void richedit(HWND parent, HFONT font)
{
    HWND re;
    HMODULE lib = LoadLibraryA("riched20.dll");
    CHARFORMATA cf;
    CHARRANGE cr;
    int i;

    emit("== rich edit ==\r\n");
    wsprintfA(buf, "  riched20.dll %s\r\n", lib ? "loaded" : "NOT FOUND");
    emit(buf);
    re = CreateWindowExA(WS_EX_CLIENTEDGE, "RichEdit20A", "",
                         WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 300,
                         120, parent, NULL, NULL, NULL);
    if (!re) {
        wsprintfA(buf, "  RichEdit20A would not be created: %lu\r\n",
                  GetLastError());
        emit(buf);
        return;
    }
    SendMessageA(re, WM_SETFONT, (WPARAM)font, FALSE);

    /* What a fresh control says about itself, before anything is asked of
     * it: the format a first character would be typed in. */
    SetWindowTextA(re, "abcdefghijklmnopqrst");
    charfmt_of(re, 0, 20, "fresh control, all of it");

    /* One run made in the middle of another. 5..10 is "fghij". */
    set_bold(re, 5, 10, 1);
    emit("  after bolding 5..10:\r\n");
    for (i = 0; i < 20; i += 1) {
        if (i == 0 || i == 4 || i == 5 || i == 9 || i == 10 || i == 11) {
            char label[32];
            wsprintfA(label, "character %d", i);
            charfmt_of(re, i, i + 1, label);
        }
    }
    charfmt_of(re, 4, 6, "across the first boundary");
    charfmt_of(re, 0, 20, "the whole text");
    dump_rtf(re, "bolding 5..10");

    /* Now make the neighbour identical and see whether the two become one:
     * bolding 10..15 leaves 5..15 bold either way, and the RTF says whether
     * riched20 keeps two runs or one. */
    set_bold(re, 10, 15, 1);
    dump_rtf(re, "bolding 10..15 as well");

    /* And take it away again from the middle of the bold stretch, which is
     * the split a formatting command makes in a run. */
    set_bold(re, 8, 12, 0);
    dump_rtf(re, "unbolding 8..12");

    /* What a character typed at a boundary takes its formatting from. The
     * caret is put between a bold character and a plain one and a letter is
     * typed; whether it comes out bold is the insertion rule, and it decides
     * where a run's end is stored. */
    SetWindowTextA(re, "abcdefghij");
    set_bold(re, 0, 5, 1);
    cr.cpMin = cr.cpMax = 5; /* between the bold run and the plain one */
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageA(re, WM_CHAR, (WPARAM)'X', 0);
    charfmt_of(re, 5, 6, "typed at the boundary");
    dump_rtf(re, "typing X at the boundary");

    /* The other end: a caret inside the bold run rather than at its edge. */
    SetWindowTextA(re, "abcdefghij");
    set_bold(re, 0, 5, 1);
    cr.cpMin = cr.cpMax = 3;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageA(re, WM_CHAR, (WPARAM)'Y', 0);
    charfmt_of(re, 3, 4, "typed inside the run");

    /* What EM_SETCHARFORMAT with no selection does, which is the other way a
     * format bar's Bold button can be written: SCF_SELECTION with an empty
     * selection sets the format the next character will be typed in. */
    SetWindowTextA(re, "plain");
    cr.cpMin = cr.cpMax = 5;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_BOLD;
    cf.dwEffects = CFE_BOLD;
    SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageA(re, WM_CHAR, (WPARAM)'Z', 0);
    charfmt_of(re, 5, 6, "typed after an empty-selection set");
    dump_rtf(re, "an empty-selection set then a character");

    emit("== the caret's column over a short line ==\r\n");
    emit("  (line 2 starts at 16 and is five long; line 3 starts at 23. A\r\n"
         "   column kept from 12 lands on 35, a column taken from where the\r\n"
         "   caret actually is on 28.)\r\n");
    column_walk(parent, font, "RichEdit20A", 1);
    column_walk(parent, font, "EDIT", 0);

    DestroyWindow(re);
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
                        /* 300 tall, not 230: the two state bars go below the
                         * measuring one and a control off the client is a
                         * control that was never drawn. */
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 460, 300,
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
    richedit(w, font);
    barstates(w);
    CloseHandle(out_file);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
