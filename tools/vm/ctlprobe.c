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
#include <stddef.h>
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

/* A status bar's parts, which are two questions in one.
 *
 * SB_SETPARTS is given a list of right edges. Where each part's *rectangle*
 * then falls is not stated anywhere and cannot be read off a picture without
 * knowing the answer already, because a part drawn two right of its edge and
 * a part two narrow look identical unless you know which edge was asked for.
 * SB_GETRECT says outright.
 *
 * ween32 starts a part at the previous edge plus two and ends it at its own
 * edge; WordPad's status bar comes out two pixels right of the machine's on
 * both dividers, which is what that plus-two would do. But changing it moves
 * 853 pixels of explorer's status bar, and that band is recorded as matching
 * the machine within one pixel. One of the two is fitted to the other and a
 * rectangle from comctl32 says which.
 */
static void statusbar(HWND parent)
{
    static const int edges[3] = { 200, 260, 320 };
    HWND sb;
    int i;

    sb = CreateWindowExA(0, STATUSCLASSNAMEA, NULL,
                         WS_CHILD | WS_VISIBLE | CCS_NORESIZE |
                         CCS_NOPARENTALIGN,
                         0, 240, 400, 20, parent, (HMENU)(UINT_PTR)360, NULL,
                         NULL);
    SendMessageA(sb, SB_SETPARTS, 3, (LPARAM)edges);
    SendMessageA(sb, SB_SETTEXTA, 0, (LPARAM)"first");
    SendMessageA(sb, SB_SETTEXTA, 1, (LPARAM)"second");
    SendMessageA(sb, SB_SETTEXTA, 2, (LPARAM)"third");

    emit("== status bar, SB_SETPARTS 200 260 320 ==\r\n");
    for (i = 0; i < 3; i++) {
        RECT r;
        r.left = r.top = r.right = r.bottom = 0;
        SendMessageA(sb, SB_GETRECT, (WPARAM)i, (LPARAM)&r);
        wsprintfA(buf, "  part %d  %ld,%ld %ldx%ld  (left %ld right %ld)\r\n",
                  i, r.left, r.top, r.right - r.left, r.bottom - r.top,
                  r.left, r.right);
        emit(buf);
    }
    /* Two parts with the same text, one bordered and one not, so a capture
     * says whether losing the border moves the text. ween32 puts a borderless
     * part's text one row above where the machine puts WordPad's, and its
     * vertical rule was measured on bordered parts only. */
    SendMessageA(sb, SB_SETTEXTA, 1, (LPARAM)"Ay");
    SendMessageA(sb, SB_SETTEXTA, 2 | SBT_NOBORDERS, (LPARAM)"Ay");
    emit("  part 1 bordered and part 2 not, both \"Ay\"; read the capture\r\n");

    /* And WordPad's own bar, to the pixel: 18 tall, one borderless part, the
     * string the machine's shows. ween32 puts that text one row above where
     * the machine's WordPad puts it, and this says whether the machine's row
     * is plain comctl32's or something MFC does on top. */
    {
        static const int one[1] = { 300 };
        HWND wp = CreateWindowExA(0, STATUSCLASSNAMEA, NULL,
                                  WS_CHILD | WS_VISIBLE | CCS_NORESIZE |
                                  CCS_NOPARENTALIGN,
                                  0, 300, 400, 18, parent,
                                  (HMENU)(UINT_PTR)380, NULL, NULL);
        RECT r;
        SendMessageA(wp, SB_SETPARTS, 1, (LPARAM)one);
        SendMessageA(wp, SB_SETTEXTA, 0 | SBT_NOBORDERS,
                     (LPARAM)"For Help, press F1");
        r.left = r.top = r.right = r.bottom = 0;
        SendMessageA(wp, SB_GETRECT, 0, (LPARAM)&r);
        wsprintfA(buf,
                  "  an 18-tall bar at window y=300: part 0 %ld,%ld %ldx%ld\r\n",
                  r.left, r.top, r.right - r.left, r.bottom - r.top);
        emit(buf);
    }
}

/* A toolbar in a rebar band, which is where explorer's menu band lives and
 * the only place ween32's centring has ever been checked. Two numbers: where
 * the rebar puts the toolbar, and where the toolbar puts its button. ween32
 * gets the sum right and may have neither half right. */
static void rebarband(HWND parent)
{
    HWND rb, tb;
    REBARINFO ri;
    REBARBANDINFOA bi;
    TBBUTTON b;
    RECT wr, cr;

    memset(&ri, 0, sizeof ri);
    ri.cbSize = sizeof ri;
    rb = CreateWindowExA(0, REBARCLASSNAMEA, NULL,
                         WS_CHILD | WS_VISIBLE | RBS_VARHEIGHT |
                         CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
                         0, 270, 400, 26, parent, (HMENU)(UINT_PTR)370, NULL,
                         NULL);
    SendMessageA(rb, RB_SETBARINFO, 0, (LPARAM)&ri);

    tb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
                         WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST |
                         CCS_NORESIZE | CCS_NODIVIDER | CCS_NOPARENTALIGN,
                         0, 0, 100, 22, rb, (HMENU)(UINT_PTR)371, NULL, NULL);
    SendMessageA(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageA(tb, TB_SETPADDING, 0, MAKELPARAM(16, 0));
    SendMessageA(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(0, 19));
    memset(&b, 0, sizeof b);
    b.iBitmap = -1;
    b.idCommand = 600;
    b.fsState = TBSTATE_ENABLED;
    b.fsStyle = TBSTYLE_BUTTON;
    b.iString = (INT_PTR)"File";
    SendMessageA(tb, TB_ADDBUTTONS, 1, (LPARAM)&b);

    memset(&bi, 0, sizeof bi);
    /* Windows 2000's comctl32 knows a shorter REBARBANDINFO than the header
     * this is compiled against, and a cbSize it does not recognise makes
     * RB_INSERTBAND fail outright -- which is why the first run of this
     * reported the toolbar at 0x0 and I had nothing to read. The classic size
     * ends at cxHeader; ask for that. Same trap as NONCLIENTMETRICS. */
    bi.cbSize = (UINT)(offsetof(REBARBANDINFOA, cxHeader) + sizeof(UINT));
    bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE;
    bi.fStyle = RBBS_NOGRIPPER;
    bi.hwndChild = tb;
    bi.cxMinChild = 100;
    bi.cyMinChild = 22;
    {
        /* Windows 2000's comctl32 knows a shorter REBARBANDINFO than this
         * header declares, and refuses a cbSize it does not recognise. Rather
         * than guess which of the historical sizes it wants, try them from
         * the newest down and report the one that takes -- that answer is
         * worth as much as the rectangle it unlocks. */
        static const int sizes[5] = { 80, 68, 60, 56, 52 };
        LRESULT ok = 0;
        int j;
        for (j = 0; j < 5 && !ok; j++) {
            bi.cbSize = (UINT)sizes[j];
            ok = SendMessageA(rb, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);
            wsprintfA(buf, "  RB_INSERTBAND cbSize %2d -> %s\r\n", sizes[j],
                      ok ? "ok" : "refused");
            emit(buf);
        }
        emit("== a toolbar in a rebar band ==\r\n");
    }
    wr.left = wr.top = wr.right = wr.bottom = 0;
    GetWindowRect(tb, &wr);
    cr.left = cr.top = cr.right = cr.bottom = 0;
    GetWindowRect(rb, &cr);
    wsprintfA(buf, "  toolbar sits at %ld,%ld in the rebar, %ldx%ld\r\n",
              wr.left - cr.left, wr.top - cr.top, wr.right - wr.left,
              wr.bottom - wr.top);
    emit(buf);
    wr.left = wr.top = wr.right = wr.bottom = 0;
    SendMessageA(tb, TB_GETITEMRECT, 0, (LPARAM)&wr);
    wsprintfA(buf, "  and its button at %ld,%ld %ldx%ld inside that\r\n",
              wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top);
    emit(buf);
}

/* Where a classic button's picture goes inside it.
 *
 * ween32 puts it at `bx + 1 + (w - 1 - 16) / 2` -- four in, for a button of
 * 23 -- and calls that centring in the machine's name. WordPad's nineteen
 * pictures land three columns right of the machine's, so the machine's inset
 * is one or two and it is not a centring. One or two is not a difference to
 * fit, so: a solid black 16x16 image, whose every column is ink, in a button
 * whose rectangle is reported beside it. The capture then says the inset with
 * no arithmetic at all.
 */
static void imageinset(HWND parent)
{
    static const struct { const char *what; DWORD extra; int y; } bars[2] = {
        { "flat", TBSTYLE_FLAT, 330 },
        { "classic", 0, 355 },
    };
    unsigned char bits[16 * 16 * 4];
    HBITMAP bm;
    int k, j;

    for (j = 0; j < 16 * 16 * 4; j++)
        bits[j] = 0; /* black, and opaque: every column of it is ink */
    bm = CreateBitmap(16, 16, 1, 32, bits);

    for (k = 0; k < 2; k++) {
        TBBUTTON b;
        HWND tb;
        HIMAGELIST il;
        RECT r;

        memset(&b, 0, sizeof b);
        b.iBitmap = 0;
        b.idCommand = 700 + k;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = TBSTYLE_BUTTON;
        tb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
                             WS_CHILD | WS_VISIBLE | CCS_NODIVIDER |
                             CCS_NORESIZE | CCS_NOPARENTALIGN | bars[k].extra,
                             0, bars[k].y, 200, 24, parent,
                             (HMENU)(UINT_PTR)(390 + k), NULL, NULL);
        SendMessageA(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageA(tb, TB_SETBITMAPSIZE, 0, MAKELPARAM(16, 16));
        SendMessageA(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(23, 22));
        il = ImageList_Create(16, 16, ILC_COLOR32, 1, 1);
        ImageList_Add(il, bm, NULL);
        SendMessageA(tb, TB_SETIMAGELIST, 0, (LPARAM)il);
        SendMessageA(tb, TB_ADDBUTTONS, 1, (LPARAM)&b);
        r.left = r.top = r.right = r.bottom = 0;
        SendMessageA(tb, TB_GETITEMRECT, 0, (LPARAM)&r);
        wsprintfA(buf,
                  "  %-7s bar at window y=%d: button %ld,%ld %ldx%ld\r\n",
                  bars[k].what, bars[k].y, r.left, r.top, r.right - r.left,
                  r.bottom - r.top);
        emit(buf);
    }
    emit("== a solid 16x16 image in a 23x22 button; read the inset off the capture ==\r\n");
}

/* Where a status bar puts its text, as its height changes.
 *
 * ween32 has one formula and it cannot serve both the cases we have checked:
 * with it, Paint's 23-tall bar lands on the machine's row and WordPad's
 * 18-tall bar lands one above; without it, WordPad is right and Paint is 467
 * pixels wrong. So the rule depends on the height in a way the formula does
 * not capture, and four heights with the same string in each will say how.
 */
static HFONT the_font;
static HFONT the_status_font;

/* How tall a button comes out, which is not the cy it was told.
 *
 * explorer's menu band asks for 19 and real comctl32 hands back 16, where
 * ween32 hands back the 19 it was asked for -- and ween32's menu band matches
 * the machine, so the height and the top inset are wrong together and come
 * out right together. Neither can be moved without the other, and moving them
 * needs the rule rather than the one case.
 *
 * Four heights asked for, in the menu band's own configuration and again with
 * a plain image button, so the answer separates "the cy is ignored" from "the
 * cy is a minimum" from "the cy is used unless the text needs more".
 */
static void buttonheights(HWND parent)
{
    static const int asked[4] = { 16, 19, 22, 26 };
    int k, j;
    emit("== button heights: told cy, given back ==\r\n");
    for (k = 0; k < 2; k++) {
        for (j = 0; j < 4; j++) {
            TBBUTTON b;
            HWND tb;
            RECT r;
            memset(&b, 0, sizeof b);
            b.idCommand = 800 + k * 10 + j;
            b.fsState = TBSTATE_ENABLED;
            b.fsStyle = TBSTYLE_BUTTON;
            if (k == 0) { /* a menu band's title: text, no image */
                b.iBitmap = -1;
                b.iString = (INT_PTR)"File";
            } else { /* a plain image button */
                b.iBitmap = 0;
            }
            tb = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
                                 WS_CHILD | TBSTYLE_FLAT | CCS_NODIVIDER |
                                 CCS_NORESIZE | CCS_NOPARENTALIGN |
                                 (k == 0 ? TBSTYLE_LIST : 0),
                                 0, 0, 200, 40, parent,
                                 (HMENU)(UINT_PTR)(410 + k * 4 + j), NULL,
                                 NULL);
            SendMessageA(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
            SendMessageA(tb, WM_SETFONT, (WPARAM)the_font, FALSE);
            SendMessageA(tb, TB_SETBITMAPSIZE, 0, MAKELPARAM(16, 16));
            if (k == 0)
                SendMessageA(tb, TB_SETPADDING, 0, MAKELPARAM(16, 0));
            SendMessageA(tb, TB_SETBUTTONSIZE, 0, MAKELPARAM(0, asked[j]));
            SendMessageA(tb, TB_ADDBUTTONS, 1, (LPARAM)&b);
            r.left = r.top = r.right = r.bottom = 0;
            SendMessageA(tb, TB_GETITEMRECT, 0, (LPARAM)&r);
            wsprintfA(buf, "  %-10s cy=%2d -> y=%ld h=%ld w=%ld\r\n",
                      k == 0 ? "menu band" : "image", asked[j], r.top,
                      r.bottom - r.top, r.right - r.left);
            emit(buf);
            DestroyWindow(tb);
        }
    }
}

static void statusheights(HWND parent)
{
    static const int heights[4] = { 18, 20, 23, 26 };
    int j, y = 380;
    emit("== status bars of four heights, same string ==\r\n");
    for (j = 0; j < 4; j++) {
        static const int one[1] = { 180 };
        HWND sb;
        RECT r;
        sb = CreateWindowExA(0, STATUSCLASSNAMEA, NULL,
                             WS_CHILD | WS_VISIBLE | CCS_NORESIZE |
                             CCS_NOPARENTALIGN,
                             0, y, 200, heights[j], parent,
                             (HMENU)(UINT_PTR)(400 + j), NULL, NULL);
        /* The message font, which is what a real program's bar has and what
         * the first run of this did *not*: every one of its four answers came
         * out a row below the machine's own WordPad and Paint, by exactly the
         * one row a different font moves a line. Set it and the two are
         * comparable. */
        /* A status bar's font is NONCLIENTMETRICS' *status* font, not its
         * message font -- a separate field, and the one a real program's bar
         * ends up with. With the message font every one of these four came
         * out a row below the machine's own WordPad and Paint. */
        SendMessageA(sb, WM_SETFONT, (WPARAM)the_status_font, TRUE);
        SendMessageA(sb, SB_SETPARTS, 1, (LPARAM)one);
        SendMessageA(sb, SB_SETTEXTA, 0 | SBT_NOBORDERS, (LPARAM)"Hg");
        r.left = r.top = r.right = r.bottom = 0;
        SendMessageA(sb, SB_GETRECT, 0, (LPARAM)&r);
        wsprintfA(buf, "  %2d tall at window y=%3d: part %ld,%ld %ldx%ld\r\n",
                  heights[j], y, r.left, r.top, r.right - r.left,
                  r.bottom - r.top);
        emit(buf);
        y += heights[j] + 4;
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
    wsprintfA(buf, "  -- RTF after %s (%d bytes) --\r\n", what, rtf_len);
    emit(buf);
    /* All of it, header and all: what a writer has to produce is the whole
     * document and not the part after \pard. */
    (void)i;
    emit("  ");
    emit(rtf_buf);
    emit("\r\n");
}

/* ---- paragraphs ----------------------------------------------------------
 *
 * The other half of a rich edit's model, and the same kind of question: what
 * a command does to a paragraph a selection only touches, what the control
 * answers over two that differ, and what a new paragraph inherits. Each one
 * decides a data structure rather than a pixel. */

static void paraformat_of(HWND re, int from, int to, const char *what)
{
    PARAFORMAT pf;
    CHARRANGE cr;
    cr.cpMin = from;
    cr.cpMax = to;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    memset(&pf, 0, sizeof pf);
    pf.cbSize = sizeof pf;
    SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    wsprintfA(buf,
              "  %-30s %2d..%-2d mask %08lx  align %s  indent %ld right %ld "
              "offset %ld  tabs %d\r\n",
              what, from, to, pf.dwMask,
              (pf.dwMask & PFM_ALIGNMENT)
                  ? (pf.wAlignment == PFA_LEFT     ? "left"
                     : pf.wAlignment == PFA_RIGHT  ? "right"
                     : pf.wAlignment == PFA_CENTER ? "centre"
                                                   : "other")
                  : "MIXED",
              pf.dxStartIndent, pf.dxRightIndent, pf.dxOffset,
              (int)pf.cTabCount);
    emit(buf);
}

static void set_align(HWND re, int from, int to, WORD how)
{
    PARAFORMAT pf;
    CHARRANGE cr;
    cr.cpMin = from;
    cr.cpMax = to;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    memset(&pf, 0, sizeof pf);
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_ALIGNMENT;
    pf.wAlignment = how;
    SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void paragraphs(HWND parent, HFONT font)
{
    HWND re = CreateWindowExA(WS_EX_CLIENTEDGE, "RichEdit20A", "",
                              WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                  ES_AUTOVSCROLL,
                              10, 120, 300, 100, parent, NULL, NULL, NULL);
    CHARRANGE cr;
    DWORD from = 0, to = 0;
    if (!re) {
        wsprintfA(buf, "  no rich edit: %lu\r\n", GetLastError());
        emit(buf);
        return;
    }
    SendMessageA(re, WM_SETFONT, (WPARAM)font, FALSE);

    /* Three paragraphs. The offsets are the *stored* ones, and a mark is
     * stored as a single CR whatever was handed in -- which the block at
     * the end of this function measures and which the first version of the
     * comment here got wrong, counting two for every break: "one" 0..2, its
     * mark at 3, "two" 4..6, its mark at 7, "three" 8..12. */
    SetWindowTextA(re, "one\r\ntwo\r\nthree");
    emit("== paragraphs ==\r\n");
    paraformat_of(re, 0, 15, "a fresh control, all of it");

    /* A selection touching the middle of the second paragraph only. If a
     * command formats the whole paragraph, the first and last characters of
     * it come back centred too. */
    set_align(re, 6, 7, PFA_CENTER);
    paraformat_of(re, 5, 6, "first character of the second");
    paraformat_of(re, 7, 8, "last character of it");
    paraformat_of(re, 0, 1, "the paragraph before");
    paraformat_of(re, 10, 11, "and the one after");
    paraformat_of(re, 0, 15, "across all three");

    /* A selection that ends exactly on a paragraph's first character: does
     * the paragraph it only touches take the command? 0..4 is the question,
     * since 4 is where "two" begins; 0..5 is a different one -- it has a
     * character of the second paragraph in it, and the first run of this
     * asked that one by accident, counting the mark as two characters. */
    SetWindowTextA(re, "one\r\ntwo\r\nthree");
    set_align(re, 0, 4, PFA_RIGHT);
    paraformat_of(re, 0, 1, "0..4 set: the first paragraph");
    paraformat_of(re, 4, 5, "0..4 set: the second, whose first character it "
                            "ended on");
    SetWindowTextA(re, "one\r\ntwo\r\nthree");
    set_align(re, 0, 5, PFA_RIGHT);
    paraformat_of(re, 4, 5, "0..5 set: the second, of which it took one");

    /* What a new paragraph inherits: type a return in the middle of a
     * centred one. */
    SetWindowTextA(re, "alpha beta");
    set_align(re, 0, 10, PFA_CENTER);
    cr.cpMin = cr.cpMax = 5;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    SetFocus(re);
    SendMessageA(re, WM_CHAR, (WPARAM)'\r', 0);
    paraformat_of(re, 0, 1, "after a return: the first half");
    paraformat_of(re, 8, 9, "after a return: the second");

    /* And what survives the break being taken out again: two paragraphs
     * with different alignments, then a backspace at the start of the
     * second. Through WM_KEYDOWN, because a rich edit does not take a
     * backspace as a character the way an EDIT does -- the first run of this
     * sent WM_CHAR '\b' and the text came back unchanged, which is a probe
     * measuring nothing and saying nothing about it. */
    SetWindowTextA(re, "left\r\nright");
    set_align(re, 0, 4, PFA_LEFT);
    set_align(re, 6, 11, PFA_RIGHT);
    cr.cpMin = cr.cpMax = 5;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    SetFocus(re);
    SendMessageA(re, WM_KEYDOWN, VK_BACK, 0);
    SendMessageA(re, WM_CHAR, (WPARAM)'\b', 0);
    {
        char text[64] = "";
        GetWindowTextA(re, text, sizeof text);
        wsprintfA(buf, "  after joining them the text is \"%s\"\r\n", text);
        emit(buf);
    }
    paraformat_of(re, 0, 9, "the joined paragraph");

    /* The selection's own corners: what a backwards CHARRANGE does, and
     * where the caret ends up. */
    SetWindowTextA(re, "abcdefghij");
    cr.cpMin = 7;
    cr.cpMax = 3;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    memset(&cr, 0, sizeof cr);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
    wsprintfA(buf, "  a backwards CHARRANGE 7..3 comes back %ld..%ld\r\n",
              cr.cpMin, cr.cpMax);
    emit(buf);
    SendMessageA(re, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
    wsprintfA(buf, "  and EM_GETSEL says %lu..%lu\r\n", from, to);
    emit(buf);
    cr.cpMin = 0;
    cr.cpMax = 0;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    {
        char sel[32] = "?";
        int n = (int)SendMessageA(re, EM_GETSELTEXT, 0, (LPARAM)sel);
        wsprintfA(buf, "  EM_GETSELTEXT of nothing answers %d, \"%s\"\r\n", n,
                  sel);
        emit(buf);
    }

    /* How a paragraph mark is *stored*, which decides every offset a program
     * computes. Rich Edit 2.0 is said to keep a CRLF as a single CR; if it
     * does, the text handed back is one byte shorter per break than the text
     * that was set, and a selection across a break says so too. */
    SetWindowTextA(re, "one\r\ntwo");
    {
        char back[64] = "";
        int n = (int)SendMessageA(re, WM_GETTEXT, (WPARAM)sizeof back,
                                  (LPARAM)back);
        int len = (int)SendMessageA(re, WM_GETTEXTLENGTH, 0, 0);
        int i;
        wsprintfA(buf, "  set 8 bytes; WM_GETTEXTLENGTH %d, WM_GETTEXT %d, "
                       "bytes:",
                  len, n);
        emit(buf);
        for (i = 0; i < n && i < 16; i++) {
            wsprintfA(buf, " %02x", (unsigned char)back[i]);
            emit(buf);
        }
        emit("\r\n");
    }
    {
        char sel[32] = "";
        int n;
        cr.cpMin = 2;
        cr.cpMax = 6;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        n = (int)SendMessageA(re, EM_GETSELTEXT, 0, (LPARAM)sel);
        wsprintfA(buf, "  EM_GETSELTEXT of 2..6 answers %d, bytes:", n);
        emit(buf);
        {
            int i;
            for (i = 0; i < n && i < 16; i++) {
                wsprintfA(buf, " %02x", (unsigned char)sel[i]);
                emit(buf);
            }
        }
        emit("\r\n");
        SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
        wsprintfA(buf, "  and the selection comes back %ld..%ld\r\n", cr.cpMin,
                  cr.cpMax);
        emit(buf);
    }

    /* Where the paragraphs are, as the control counts lines: a text with no
     * break at the end, and one with. */
    SetWindowTextA(re, "one\r\ntwo");
    wsprintfA(buf, "  \"one\\r\\ntwo\"    lines %ld\r\n",
              SendMessageA(re, EM_GETLINECOUNT, 0, 0));
    emit(buf);
    SetWindowTextA(re, "one\r\ntwo\r\n");
    wsprintfA(buf, "  \"one\\r\\ntwo\\r\\n\" lines %ld  and EM_LINEINDEX 2 is %ld\r\n",
              SendMessageA(re, EM_GETLINECOUNT, 0, 0),
              SendMessageA(re, EM_LINEINDEX, 2, 0));
    emit(buf);

    DestroyWindow(re);
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

static void wrapping(HWND parent, HFONT font);
static void tabs(HWND parent, HFONT font);
static void finding(HWND parent, HFONT font);
static void selection_bar(HWND parent, HFONT font);

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
    paragraphs(parent, font);
    wrapping(parent, font);
    tabs(parent, font);
    finding(parent, font);
    selection_bar(parent, font);

    DestroyWindow(re);
}

/* ---- wrapping, and what the control writes -------------------------------
 *
 * Where a line breaks, what the break does with the space it broke at, and
 * what EM_GETLINECOUNT counts once there are more lines than paragraphs.
 * Then the RTF: the writer's own output for everything ween32 can hold, and
 * a small document read back in, which is the only way to know what the
 * reader will accept from a file WordPad saved. */

static const char *rtf_in_text;
static int rtf_in_at;

static DWORD CALLBACK rtf_in(DWORD_PTR cookie, LPBYTE bytes, LONG cb,
                             LONG *read)
{
    LONG i = 0;
    (void)cookie;
    while (i < cb && rtf_in_text[rtf_in_at])
        bytes[i++] = (BYTE)rtf_in_text[rtf_in_at++];
    *read = i;
    return 0;
}

static void lines_of(HWND re, const char *what)
{
    int n = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0), i;
    wsprintfA(buf, "  %-34s %d lines:", what, n);
    emit(buf);
    for (i = 0; i < n && i < 12; i++) {
        wsprintfA(buf, " [%ld,%ld]", SendMessageA(re, EM_LINEINDEX, i, 0),
                  SendMessageA(re, EM_LINELENGTH,
                               SendMessageA(re, EM_LINEINDEX, i, 0), 0));
        emit(buf);
    }
    emit("\r\n");
}


/* Where a tab puts the text, which is the last thing this library's rich
 * edit does not draw. Everything here is a number the control gives back
 * rather than a picture of it: EM_POSFROMCHAR of the character *after* a tab
 * is the x the tab advanced to, which is the only thing the drawing needs.
 *
 * Twips are the unit a PARAFORMAT tab stop is in -- 1440 to the inch -- so
 * at 96 dpi a pixel is fifteen of them, and the interesting part is what the
 * control does with a stop that is not a whole number of pixels. */
static int rich_x_of(HWND re, int i)
{
    POINTL p;
    p.x = 0;
    p.y = 0;
    SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)i);
    return (int)p.x;
}

static void tab_row(HWND re, const char *what, int n)
{
    int i;
    wsprintfA(buf, "  %-30s x:", what);
    emit(buf);
    for (i = 0; i < n; i++) {
        wsprintfA(buf, " %d", rich_x_of(re, i));
        emit(buf);
    }
    emit("\r\n");
}

static void set_tabs(HWND re, const LONG *tw, int n)
{
    PARAFORMAT pf;
    CHARRANGE cr;
    int i;
    cr.cpMin = 0;
    cr.cpMax = -1;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
    memset(&pf, 0, sizeof pf);
    pf.cbSize = sizeof pf;
    pf.dwMask = PFM_TABSTOPS;
    pf.cTabCount = (SHORT)n;
    for (i = 0; i < n; i++)
        pf.rgxTabs[i] = tw[i];
    if (!SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf)) {
        wsprintfA(buf, "  EM_SETPARAFORMAT refused %d stops: %lu\r\n", n,
                  GetLastError());
        emit(buf);
    }
}

/* What EM_FINDTEXT does, since the frame's Find box will ask this control to
 * search and searching is the control's job. Everything here is a number and
 * a range the control answers with; nothing is a picture. */
static void find_row(HWND re, const char *what, DWORD flags, int from, int to,
                     const char *needle)
{
    FINDTEXTEXA ft;
    LRESULT r;
    memset(&ft, 0, sizeof ft);
    ft.chrg.cpMin = from;
    ft.chrg.cpMax = to;
    ft.lpstrText = (LPSTR)needle;
    ft.chrgText.cpMin = -2;
    ft.chrgText.cpMax = -2;
    r = SendMessageA(re, EM_FINDTEXTEX, (WPARAM)flags, (LPARAM)&ft);
    wsprintfA(buf, "  %-40s -> %ld, range %ld..%ld\r\n", what, (long)r,
              ft.chrgText.cpMin, ft.chrgText.cpMax);
    emit(buf);
}

/* The selection bar, which is where WordPad's seven missing pixels come
 * from: its editor's style word carries ES_SELECTIONBAR (0x01000000) --
 * probe.c read 550081C4 off the machine -- and a rich edit with it inset
 * its text. How far is what this asks, with the same control either way so
 * nothing but the bit differs. */
static void selection_bar(HWND parent, HFONT font)
{
    static const DWORD styles[2] = { 0, 0x01000000 };
    int k;
    emit("== the selection bar ==\r\n");
    for (k = 0; k < 2; k++) {
        HWND re = CreateWindowExA(WS_EX_CLIENTEDGE, "RichEdit20A", "",
                                  WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                      ES_AUTOVSCROLL | styles[k],
                                  10, 430, 300, 40, parent, NULL, NULL, NULL);
        POINTL p;
        RECT rc;
        if (!re)
            continue;
        SendMessageA(re, WM_SETFONT, (WPARAM)font, FALSE);
        SetWindowTextA(re, "cat\r\ndog");
        GetClientRect(re, &rc);
        p.x = p.y = 0;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, 0);
        wsprintfA(buf, "  style %08lX: client %ld wide, first character at "
                       "%ld,%ld\r\n",
                  (unsigned long)styles[k], rc.right, p.x, p.y);
        emit(buf);
        p.x = p.y = 0;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, 4);
        wsprintfA(buf, "    and the second line's first at %ld,%ld\r\n", p.x,
                  p.y);
        emit(buf);
        {
            LRESULT m = SendMessageA(re, EM_GETMARGINS, 0, 0);
            wsprintfA(buf, "    EM_GETMARGINS left %d right %d\r\n",
                      (int)LOWORD(m), (int)HIWORD(m));
            emit(buf);
        }
        DestroyWindow(re);
    }
    /* WordPad's own style word, exactly as probe.c read it off the machine:
     * 550081C4 with ex 210. If a control built like that puts its first
     * character where WordPad's does, the whole of the difference is in the
     * style; if it does not, WordPad is doing something else as well. The
     * variants below take the two bits that could be responsible off one at
     * a time. */
    {
        static const struct {
            DWORD style;
            const char *what;
        } like[] = {
            { 0x550081C4, "WordPad's own style word" },
            { 0x550081C4 & ~0x01000000u, "the same without ES_SELECTIONBAR" },
            { 0x550081C4 & ~0x04000000u, "the same without 0x04000000" },
            { 0x550081C4 & ~0x00008000u, "the same without ES_SAVESEL" },
        };
        LOGFONTA lf;
        HFONT arial;
        int j;
        memset(&lf, 0, sizeof lf);
        lf.lfHeight = -13;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lstrcpyA(lf.lfFaceName, "Arial");
        arial = CreateFontIndirectA(&lf);
        for (j = 0; j < 4; j++) {
            HWND re = CreateWindowExA(0x210, "RichEdit20W", NULL,
                                      like[j].style, 10, 480, 760, 40, parent,
                                      NULL, NULL, NULL);
            POINTL p;
            if (!re) {
                wsprintfA(buf, "  %s: would not be created\r\n", like[j].what);
                emit(buf);
                continue;
            }
            if (arial)
                SendMessageA(re, WM_SETFONT, (WPARAM)arial, FALSE);
            SetWindowTextA(re, "cat");
            p.x = p.y = 0;
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, 0);
            wsprintfA(buf, "  %-34s first character at %ld,%ld\r\n",
                      like[j].what, p.x, p.y);
            emit(buf);
            DestroyWindow(re);
        }
        if (arial)
            DeleteObject(arial);
    }

    /* And whether the bar's width follows the font, which is what would
     * explain WordPad's own editor sitting further in than this one: its
     * text is Arial 10 where the probe's is the message font. */
    {
        LOGFONTA lf;
        HFONT big;
        memset(&lf, 0, sizeof lf);
        lf.lfHeight = -13; /* ten points at 96 dpi */
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lstrcpyA(lf.lfFaceName, "Arial");
        big = CreateFontIndirectA(&lf);
        if (big) {
            HWND re = CreateWindowExA(WS_EX_CLIENTEDGE, "RichEdit20A", "",
                                      WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                          ES_AUTOVSCROLL | 0x01000000,
                                      330, 430, 300, 40, parent, NULL, NULL,
                                      NULL);
            if (re) {
                POINTL p;
                SendMessageA(re, WM_SETFONT, (WPARAM)big, FALSE);
                SetWindowTextA(re, "cat");
                p.x = p.y = 0;
                SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, 0);
                wsprintfA(buf, "  with a selection bar and Arial 10: first "
                               "character at %ld,%ld\r\n",
                          p.x, p.y);
                emit(buf);
                DestroyWindow(re);
            }
            DeleteObject(big);
        }
    }
}

static void finding(HWND parent, HFONT font)
{
    HWND re = CreateWindowExA(WS_EX_CLIENTEDGE, "RichEdit20A", "",
                              WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                  ES_AUTOVSCROLL,
                              10, 370, 300, 60, parent, NULL, NULL, NULL);
    if (!re) {
        wsprintfA(buf, "  no rich edit: %lu\r\n", GetLastError());
        emit(buf);
        return;
    }
    SendMessageA(re, WM_SETFONT, (WPARAM)font, FALSE);
    emit("== finding ==\r\n");

    /* "one cat two Cat three catalog cat." -- a lower match at 4, an upper
     * at 12, one inside a longer word at 22, and a last one at 30 followed
     * by a stop. */
    SetWindowTextA(re, "one cat two Cat three catalog cat.");
    emit("  text \"one cat two Cat three catalog cat.\", cat at 4, Cat at 12,"
         " catalog at 22, cat. at 30\r\n");
    find_row(re, "flags 0, 0..-1, \"cat\"", 0, 0, -1, "cat");
    find_row(re, "FR_DOWN, 0..-1", FR_DOWN, 0, -1, "cat");
    find_row(re, "FR_DOWN|FR_MATCHCASE, \"cat\"", FR_DOWN | FR_MATCHCASE, 0,
             -1, "cat");
    find_row(re, "FR_DOWN|FR_MATCHCASE, \"Cat\"", FR_DOWN | FR_MATCHCASE, 0,
             -1, "Cat");
    find_row(re, "FR_DOWN|FR_WHOLEWORD, \"cat\"", FR_DOWN | FR_WHOLEWORD, 0,
             -1, "cat");
    find_row(re, "FR_DOWN|FR_WHOLEWORD, \"catalog\"", FR_DOWN | FR_WHOLEWORD,
             0, -1, "catalog");
    /* Does a match that starts exactly at cpMin count, and does one that
     * would run past cpMax? */
    find_row(re, "FR_DOWN, 4..-1", FR_DOWN, 4, -1, "cat");
    find_row(re, "FR_DOWN, 5..-1", FR_DOWN, 5, -1, "cat");
    find_row(re, "FR_DOWN, 0..6 (the match is 4..7)", FR_DOWN, 0, 6, "cat");
    find_row(re, "FR_DOWN, 0..7", FR_DOWN, 0, 7, "cat");
    /* Backwards, which is what a cpMin above cpMax is said to mean. */
    find_row(re, "34..0, no FR_DOWN", 0, 34, 0, "cat");
    find_row(re, "0..-1 with neither flag nor order", 0, 0, -1, "one");
    find_row(re, "34..0 with FR_DOWN set as well", FR_DOWN, 34, 0, "cat");
    find_row(re, "20..0, backwards from the middle", 0, 20, 0, "cat");
    /* Backwards, at its edges: does the range's far end bound the match
     * going back the way cpMax bounds it going forward, and does a match
     * that ends exactly where the search starts count? */
    find_row(re, "backwards 20..13, the match is 4..7", 0, 20, 13, "cat");
    find_row(re, "backwards 20..12", 0, 20, 12, "cat");
    find_row(re, "backwards 15..0 (Cat is 12..15)", 0, 15, 0, "cat");
    find_row(re, "backwards 14..0", 0, 14, 0, "cat");
    find_row(re, "backwards 12..0", 0, 12, 0, "cat");
    find_row(re, "forwards 0..-1 from 30 (cat. is 30..33)", FR_DOWN, 30, -1,
             "cat");
    find_row(re, "forwards 31..-1", FR_DOWN, 31, -1, "cat");
    /* And whether a find moves the selection, which the frame will care
     * about: the selection is put somewhere first and read back after. */
    {
        CHARRANGE cr;
        cr.cpMin = 1;
        cr.cpMax = 2;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        find_row(re, "with 1..2 selected", FR_DOWN, 0, -1, "cat");
        memset(&cr, 0, sizeof cr);
        SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
        wsprintfA(buf, "  and the selection is still %ld..%ld\r\n", cr.cpMin,
                  cr.cpMax);
        emit(buf);
    }

    /* Nothing there, and nothing asked for. */
    find_row(re, "FR_DOWN, \"zebra\"", FR_DOWN, 0, -1, "zebra");
    find_row(re, "FR_DOWN, the empty string", FR_DOWN, 0, -1, "");
    /* A search that crosses a paragraph mark: is the mark a CR to match, or
     * the CRLF the program handed in? */
    SetWindowTextA(re, "one\r\ntwo");
    emit("  text \"one\\r\\ntwo\"\r\n");
    find_row(re, "FR_DOWN, \"e\\rt\"", FR_DOWN, 0, -1, "e\rt");
    find_row(re, "FR_DOWN, \"e\\r\\nt\"", FR_DOWN, 0, -1, "e\r\nt");
    find_row(re, "FR_DOWN, \"one\"", FR_DOWN, 0, -1, "one");
    find_row(re, "FR_DOWN, \"two\"", FR_DOWN, 0, -1, "two");
    /* And what EM_FINDTEXT itself answers, which takes a FINDTEXTA and has
     * no range to fill in. */
    {
        FINDTEXTA ft;
        memset(&ft, 0, sizeof ft);
        ft.chrg.cpMin = 0;
        ft.chrg.cpMax = -1;
        ft.lpstrText = (LPSTR) "two";
        wsprintfA(buf, "  EM_FINDTEXT (not EX) \"two\" -> %ld\r\n",
                  (long)SendMessageA(re, EM_FINDTEXT, FR_DOWN, (LPARAM)&ft));
        emit(buf);
    }
    /* And what a *double click* takes, which is a different question with a
     * confusingly similar answer: FR_WHOLEWORD's boundaries are measured
     * above, and ween32's double click counts an underscore in because the
     * EDIT does. Nobody has asked riched20. */
    SetWindowTextA(re, "cat_dog cat9 don't (cat)");
    emit("  text \"cat_dog cat9 don't (cat)\"\r\n");
    {
        static const int at[] = { 1, 9, 14, 21 };
        int k;
        for (k = 0; k < 4; k++) {
            POINTL p;
            CHARRANGE cr;
            p.x = p.y = 0;
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)at[k]);
            SendMessageA(re, WM_LBUTTONDOWN, 0, MAKELPARAM(p.x + 1, p.y + 2));
            SendMessageA(re, WM_LBUTTONUP, 0, MAKELPARAM(p.x + 1, p.y + 2));
            SendMessageA(re, WM_LBUTTONDBLCLK, 0,
                         MAKELPARAM(p.x + 1, p.y + 2));
            SendMessageA(re, WM_LBUTTONUP, 0, MAKELPARAM(p.x + 1, p.y + 2));
            memset(&cr, 0, sizeof cr);
            SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
            wsprintfA(buf, "  a double click on character %d takes %ld..%ld"
                           "\r\n",
                      at[k], cr.cpMin, cr.cpMax);
            emit(buf);
        }
    }

    /* The same double click of a plain EDIT, because ween32's two controls
     * do not agree about an underscore and only one of them has ever been
     * asked. */
    {
        HWND ed = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                  WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                      ES_AUTOVSCROLL,
                                  320, 370, 300, 40, parent, NULL, NULL,
                                  NULL);
        static const int at[] = { 1, 9, 14, 21 };
        int k;
        if (ed) {
            SendMessageA(ed, WM_SETFONT, (WPARAM)the_font, FALSE);
            SetWindowTextA(ed, "cat_dog cat9 don't (cat)");
            for (k = 0; k < 4; k++) {
                DWORD from = 0, to = 0;
                LRESULT pos = SendMessageA(ed, EM_POSFROMCHAR, (WPARAM)at[k],
                                           0);
                int x = (short)LOWORD(pos), y = (short)HIWORD(pos);
                SendMessageA(ed, WM_LBUTTONDOWN, 0, MAKELPARAM(x + 1, y + 2));
                SendMessageA(ed, WM_LBUTTONUP, 0, MAKELPARAM(x + 1, y + 2));
                SendMessageA(ed, WM_LBUTTONDBLCLK, 0,
                             MAKELPARAM(x + 1, y + 2));
                SendMessageA(ed, WM_LBUTTONUP, 0, MAKELPARAM(x + 1, y + 2));
                SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
                wsprintfA(buf, "  an EDIT double clicked on character %d "
                               "takes %lu..%lu\r\n",
                          at[k], from, to);
                emit(buf);
            }
            DestroyWindow(ed);
        }
    }

    /* What a word is, for FR_WHOLEWORD: a stop, a hyphen, a digit, an
     * underscore. */
    SetWindowTextA(re, "cat cat. cat-o cat9 cat_ (cat)");
    emit("  text \"cat cat. cat-o cat9 cat_ (cat)\", at 0, 4, 9, 15, 20, 26"
         "\r\n");
    find_row(re, "whole word from 1", FR_DOWN | FR_WHOLEWORD, 1, -1, "cat");
    find_row(re, "whole word from 5", FR_DOWN | FR_WHOLEWORD, 5, -1, "cat");
    find_row(re, "whole word from 10", FR_DOWN | FR_WHOLEWORD, 10, -1, "cat");
    find_row(re, "whole word from 16", FR_DOWN | FR_WHOLEWORD, 16, -1, "cat");
    find_row(re, "whole word from 21", FR_DOWN | FR_WHOLEWORD, 21, -1, "cat");
    DestroyWindow(re);
}

static void tabs(HWND parent, HFONT font)
{
    /* Wide, because a default tab is 48 pixels and eight of them do not fit
     * in the 300 the other blocks use -- and a tab that wraps is a different
     * measurement, taken separately below. */
    HWND re = CreateWindowExA(WS_EX_CLIENTEDGE, "RichEdit20A", "",
                              WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                  ES_AUTOVSCROLL,
                              10, 230, 560, 60, parent, NULL, NULL, NULL);
    LONG tw[8];
    PARAFORMAT got;
    RECT rc;
    if (!re) {
        wsprintfA(buf, "  no rich edit: %lu\r\n", GetLastError());
        emit(buf);
        return;
    }
    SendMessageA(re, WM_SETFONT, (WPARAM)font, FALSE);
    emit("== tabs ==\r\n");
    GetClientRect(re, &rc);
    wsprintfA(buf, "  client %ld wide, %ld tall\r\n", rc.right, rc.bottom);
    emit(buf);

    /* Nine tabs and nothing else, so the character after each one is at the
     * stop itself with nothing pushing it along. Index 0 is the first tab,
     * so x[1] is where the first tab landed, x[2] the second, and so on. */
    SetWindowTextA(re, "\t\t\t\t\t\t\t\t\t.");
    tab_row(re, "nine tabs, default stops", 10);

    /* The same with something before the first tab, to say whether a stop is
     * measured from the text's left edge or from the last stop passed. */
    SetWindowTextA(re, "ab\tcd\tef\tgh");
    tab_row(re, "text between them", 11);

    /* A word wide enough to cover the first stop, then the second, then the
     * third: does the tab go to the next stop past the pen, and does it ever
     * refuse to move at all? */
    SetWindowTextA(re, "wwwwwww\tX");
    tab_row(re, "a word past the first stop", 9);
    SetWindowTextA(re, "wwwwwwwwwwwwwwwww\tX");
    tab_row(re, "a word past the second", 19);

    /* Explicit stops. 300 twips is exactly 20 pixels at 96 dpi; 1000 is
     * 66 and two thirds and 2137 is 142 and a half, which is what says how
     * the control rounds. The last tab in the text is past every stop, so
     * the same text answers what happens after the last one.
     *
     * The text goes in *first*: WM_SETTEXT starts a new document and the
     * paragraph format goes with it. The first run of this set the stops and
     * then the text, and every row came back at the default 48 with
     * EM_GETPARAFORMAT saying nought stops -- a probe measuring the default
     * twice and calling one of them a measurement. */
    SetWindowTextA(re, "\t\t\t\t\t.");
    tw[0] = 300;
    tw[1] = 1000;
    tw[2] = 2137;
    set_tabs(re, tw, 3);
    tab_row(re, "stops 300/1000/2137 twips", 6);

    /* And what comes back, so the store is checked as well as the use. */
    memset(&got, 0, sizeof got);
    got.cbSize = sizeof got;
    got.dwMask = PFM_TABSTOPS;
    SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&got);
    wsprintfA(buf, "  EM_GETPARAFORMAT: %d stops, %ld %ld %ld\r\n",
              (int)got.cTabCount, got.rgxTabs[0], got.rgxTabs[1],
              got.rgxTabs[2]);
    emit(buf);

    /* What the RTF says with those stops still on. */
    dump_rtf(re, "three stops and five tabs");

    /* A single stop, so the run past the last one is long enough to show
     * whether the default interval starts again from zero or from it. 500
     * twips is 33 and a third pixels, which no default stop is near. */
    SetWindowTextA(re, "\t\t\t\t\t.");
    tw[0] = 500;
    set_tabs(re, tw, 1);
    tab_row(re, "one stop at 500 twips", 6);

    /* A stop with text before it that already reaches past it: the same
     * question as the default stops, asked of an explicit one. */
    SetWindowTextA(re, "wwwwwww\tX");
    tw[0] = 300;
    set_tabs(re, tw, 1);
    tab_row(re, "a word past a stop at 300", 9);

    /* No stops at all again: does clearing them come back to the default? */
    SetWindowTextA(re, "\t\t\t.");
    set_tabs(re, tw, 0);
    tab_row(re, "stops cleared", 4);

    /* And what a paragraph's stops do to its neighbour: set them on the
     * first of two and ask both. */
    SetWindowTextA(re, "\t.\r\n\t.");
    {
        PARAFORMAT pf;
        CHARRANGE cr;
        cr.cpMin = 0;
        cr.cpMax = 1;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_TABSTOPS;
        pf.cTabCount = 1;
        pf.rgxTabs[0] = 300;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
    tab_row(re, "a stop on the first of two", 6);
    dump_rtf(re, "a stop on the first of two");

    /* Where the caret goes when the mouse lands inside a tab's stretch: a
     * tab is one character but 48 pixels wide, and a rule that splits every
     * other character at its middle has to say what it does with this one.
     * The text is "a<tab>b" with the default stops, so the tab covers 9..49
     * and the answer is the x at which the index turns from 1 into 2. */
    SetWindowTextA(re, "a\tb");
    {
        int x, last = -1;
        emit("  a tab's own stretch, EM_CHARFROMPOS:");
        for (x = 4; x <= 56; x += 2) {
            POINTL p;
            int i;
            p.x = x;
            p.y = 4;
            i = (int)SendMessageA(re, EM_CHARFROMPOS, 0, (LPARAM)&p);
            if (i != last) {
                wsprintfA(buf, " %d at x=%d", i, x);
                emit(buf);
                last = i;
            }
        }
        emit("\r\n");
    }
    /* And where a *click* puts the caret, which is the rule the drawing
     * needs and not necessarily the same one: EM_CHARFROMPOS is documented
     * as the nearest character, and a click is the nearest place between
     * two. WM_LBUTTONDOWN is legal here because this is the control's own
     * process. */
    {
        int x, last = -1;
        emit("  and where a click lands:");
        for (x = 0; x <= 60; x++) {
            CHARRANGE cr;
            SendMessageA(re, WM_LBUTTONDOWN, 0, MAKELPARAM(x, 4));
            SendMessageA(re, WM_LBUTTONUP, 0, MAKELPARAM(x, 4));
            memset(&cr, 0, sizeof cr);
            SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
            if ((int)cr.cpMin != last) {
                wsprintfA(buf, " %ld at x=%d", cr.cpMin, x);
                emit(buf);
                last = (int)cr.cpMin;
            }
        }
        emit("\r\n");
    }
    /* The same two questions of ordinary text, so what the tab does can be
     * told apart from the rule every character follows. */
    SetWindowTextA(re, "abcdef");
    tab_row(re, "abcdef", 7);
    {
        int x, last = -1;
        emit("  abcdef, EM_CHARFROMPOS:");
        for (x = 0; x <= 60; x++) {
            POINTL p;
            int i;
            p.x = x;
            p.y = 4;
            i = (int)SendMessageA(re, EM_CHARFROMPOS, 0, (LPARAM)&p);
            if (i != last) {
                wsprintfA(buf, " %d at x=%d", i, x);
                emit(buf);
                last = i;
            }
        }
        emit("\r\n");
        last = -1;
        emit("  abcdef, a click:");
        for (x = 0; x <= 60; x++) {
            CHARRANGE cr;
            SendMessageA(re, WM_LBUTTONDOWN, 0, MAKELPARAM(x, 4));
            SendMessageA(re, WM_LBUTTONUP, 0, MAKELPARAM(x, 4));
            memset(&cr, 0, sizeof cr);
            SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
            if ((int)cr.cpMin != last) {
                wsprintfA(buf, " %ld at x=%d", cr.cpMin, x);
                emit(buf);
                last = (int)cr.cpMin;
            }
        }
        emit("\r\n");
        SetWindowTextA(re, "a\tb");
    }
    tab_row(re, "and where a\tb sits", 3);

    DestroyWindow(re);

    /* A tab that would land past the right edge, in a control narrow enough
     * to make it: does the line wrap at the tab, does the tab stop short, or
     * does the text run off the end? 120 pixels holds two default stops. */
    re = CreateWindowExA(WS_EX_CLIENTEDGE, "RichEdit20A", "",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                             ES_AUTOVSCROLL,
                         10, 300, 120, 60, parent, NULL, NULL, NULL);
    if (!re)
        return;
    SendMessageA(re, WM_SETFONT, (WPARAM)font, FALSE);
    SetWindowTextA(re, "\t\t\t\t.");
    GetClientRect(re, &rc);
    wsprintfA(buf, "  narrow client %ld wide\r\n", rc.right);
    emit(buf);
    tab_row(re, "four tabs in a narrow one", 5);
    lines_of(re, "four tabs in a narrow one");
    lines_of(re, "four tabs in a narrow one");
    {
        int i;
        for (i = 0; i < 5; i++) {
            POINTL p;
            p.x = p.y = 0;
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)i);
            wsprintfA(buf, "  char %d at %ld,%ld\r\n", i, p.x, p.y);
            emit(buf);
        }
    }
    /* Whether a tab is a place a line may break the way a space is. ween32
     * breaks at the last space that fits and treats a tab as a break only
     * when the tab's own stop is past the edge -- which the four tabs above
     * cannot tell apart from any other rule, since they have no space in
     * them.
     *
     * "a b<tab><tab><tab>c": the third tab would go to 145 in a client 116
     * wide, so the line has to break. If the break goes to the tab, the
     * second line starts at 5; if it goes to the last space that fits, it
     * starts at 2, on the "b". */
    SetWindowTextA(re, "a b\t\t\tc");
    lines_of(re, "a space, then tabs past the edge");
    /* And the other way about: a word too long to fit, with a tab before it
     * and no space anywhere. If a tab is a break opportunity the line breaks
     * after it, at 3; if it is not, the word breaks at the character that
     * fits. */
    SetWindowTextA(re, "xx\tyyyyyyyyyyyyyyyyyyyyyyyy");
    lines_of(re, "a tab, then a word too long");
    /* And a space and a tab together, to say which one wins when both fit:
     * "aa bb<tab>cc dd" wide enough that the tab is not itself past the
     * edge but the text after it is. */
    SetWindowTextA(re, "aa bb\tcc dd ee ff");
    lines_of(re, "spaces either side of a tab");
    DestroyWindow(re);
}

static void wrapping(HWND parent, HFONT font)
{
    /* A control 200 pixels wide, so a line of ordinary words has to break
     * more than once and the numbers are small enough to read. */
    HWND re = CreateWindowExA(WS_EX_CLIENTEDGE, "RichEdit20A", "",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                  ES_MULTILINE | ES_AUTOVSCROLL,
                              320, 10, 200, 90, parent, NULL, NULL, NULL);
    EDITSTREAM es;
    if (!re) {
        wsprintfA(buf, "  no rich edit: %lu\r\n", GetLastError());
        emit(buf);
        return;
    }
    SendMessageA(re, WM_SETFONT, (WPARAM)font, FALSE);
    emit("== wrapping, in a control 200 wide ==\r\n");

    SetWindowTextA(re, "the quick brown fox jumps over the lazy dog");
    lines_of(re, "one paragraph of nine words");
    {
        char line[64];
        int i, n = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0);
        for (i = 0; i < n && i < 6; i++) {
            int got;
            *(WORD *)line = (WORD)sizeof line;
            got = (int)SendMessageA(re, EM_GETLINE, i, (LPARAM)line);
            line[got > 0 && got < (int)sizeof line ? got : 0] = 0;
            wsprintfA(buf, "    line %d = \"%s\" (%d)\r\n", i, line, got);
            emit(buf);
        }
    }

    /* A word longer than the line has to break somewhere. */
    SetWindowTextA(re, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    lines_of(re, "one word of forty-eight letters");

    /* Two paragraphs, so that the count says whether it is counting
     * paragraphs or the lines they are drawn on. */
    SetWindowTextA(re, "the quick brown fox jumps\r\nover the lazy dog again");
    lines_of(re, "two paragraphs");
    wsprintfA(buf, "    EM_LINEFROMCHAR of 30 is %ld\r\n",
              SendMessageA(re, EM_LINEFROMCHAR, 30, 0));
    emit(buf);

    /* And what EM_SETTARGETDEVICE does, on a paragraph long enough to feel
     * it: lParam is a line width in twips, and nought means the window. */
    SetWindowTextA(re, "the quick brown fox jumps over the lazy dog again and "
                       "again until it has to break somewhere");
    lines_of(re, "a long paragraph, wrapped to the window");
    SendMessageA(re, EM_SETTARGETDEVICE, 0, 1440);
    lines_of(re, "EM_SETTARGETDEVICE(0, 1440) -- an inch");
    SendMessageA(re, EM_SETTARGETDEVICE, 0, 14400);
    lines_of(re, "EM_SETTARGETDEVICE(0, 14400) -- ten inches");
    SendMessageA(re, EM_SETTARGETDEVICE, 0, 0);
    lines_of(re, "and back to (0, 0)");

    /* ---- what it writes ---- */
    emit("== RTF ==\r\n");
    SetWindowTextA(re, "plain and formatted");
    {
        CHARFORMATA cf;
        PARAFORMAT pf;
        CHARRANGE cr;
        cr.cpMin = 10;
        cr.cpMax = 19;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT |
                    CFM_SIZE | CFM_COLOR | CFM_FACE;
        cf.dwEffects = CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT;
        cf.yHeight = 240;
        cf.crTextColor = RGB(255, 0, 0);
        lstrcpyA(cf.szFaceName, "Courier New");
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_ALIGNMENT | PFM_STARTINDENT | PFM_RIGHTINDENT |
                    PFM_OFFSET | PFM_TABSTOPS;
        pf.wAlignment = PFA_CENTER;
        pf.dxStartIndent = 720;
        pf.dxRightIndent = 360;
        pf.dxOffset = -360;
        pf.cTabCount = 2;
        pf.rgxTabs[0] = 1440;
        pf.rgxTabs[1] = 2880;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
    dump_rtf(re, "one run formatted every way, in a paragraph likewise");

    /* ---- and what it reads ---- */
    rtf_in_text = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}"
                  "{\\colortbl;\\red0\\green0\\blue255;}"
                  "\\pard\\qc\\li720\\fs28 centred \\b bold \\b0\\cf1 blue\\par}";
    rtf_in_at = 0;
    memset(&es, 0, sizeof es);
    es.pfnCallback = rtf_in;
    SendMessageA(re, EM_STREAMIN, SF_RTF, (LPARAM)&es);
    {
        char text[128] = "";
        GetWindowTextA(re, text, sizeof text);
        wsprintfA(buf, "  after EM_STREAMIN the text is \"%s\", error %lu\r\n",
                  text, es.dwError);
        emit(buf);
    }
    charfmt_of(re, 0, 8, "what came in: the first word");
    charfmt_of(re, 8, 12, "the bold one");
    charfmt_of(re, 13, 17, "and the blue one");
    paraformat_of(re, 0, 5, "the paragraph it made");
    dump_rtf(re, "reading that in and writing it out again");

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

    {   /* The rebar is a "cool" class and the old InitCommonControls does not
         * register it: without this the window is never created, every
         * RB_ message goes nowhere, and RB_INSERTBAND answers 0 for a reason
         * that has nothing to do with the cbSize one then goes hunting. */
        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof icc;
        icc.dwICC = ICC_BAR_CLASSES | ICC_COOL_CLASSES | ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icc);
    }
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
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 460, 590,
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
    the_font = font;
    the_status_font = CreateFontIndirectA(&ncm.lfStatusFont);
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
    statusbar(w);
    rebarband(w);
    imageinset(w);
    statusheights(w);
    buttonheights(w);
    CloseHandle(out_file);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
