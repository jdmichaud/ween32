/* Property sheets: a dialog with a row of tabs and a page behind each.
 *
 * comctl32 builds one out of pieces that already exist — a dialog, a tab
 * control, and one child dialog per page — and so does this. The sheet owns
 * the frame, the tabs and the buttons along the bottom; each page is an
 * ordinary dialog made from its own template with its own procedure, which is
 * what lets a program write a page without knowing it is in a sheet.
 *
 * The sheet is as big as its largest page needs, which is how the pages can
 * be written independently and still line up.
 */

#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

#define PS_TAB 0x3020 /* the tab control's id, as comctl32 numbers it */
#define PS_ATOM_BUTTON 0x0080 /* the BUTTON class's ordinal in a template */
#define PS_MAX_PAGES 16

/* The frame around a page, in *pixels*, measured off the machine's own Folder
 * Options: a 386 by 468 window whose client holds a 369 by 399 tab control at
 * (5, 9) with a 365 by 377 page in it, and three 75 by 23 buttons in a band
 * below, their right edges level with the tab control's.
 *
 * Pixels rather than dialog units because a sheet is not laid out by the
 * dialog manager: comctl32 sizes it to its largest page after the fact, and
 * so does this. Going through dialog units would round every edge to the
 * nearest one and a half pixels. */
#define PS_TAB_X 5    /* client left to tab control */
#define PS_TAB_Y 7    /* client top to tab control */
#define PS_RIGHT 6    /* tab control to client right */
#define PS_BOTTOM 7   /* buttons to client bottom */
#define PS_BTN_W 75   /* one of OK / Cancel / Apply */
#define PS_BTN_H 23
#define PS_BTN_GAP 6  /* between two of them */
#define PS_BTN_TOP 6  /* tab control to the button row */

typedef struct {
    HWND sheet;
    HWND tabs;
    HWND page[PS_MAX_PAGES];
    PROPSHEETPAGEA desc[PS_MAX_PAGES];
    int changed[PS_MAX_PAGES]; /* this page has something worth keeping */
    HWND placed[PS_MAX_PAGES]; /* what this one put the keyboard on itself */
    int count;
    int current;
    int result; /* what PropertySheetA gives back */
} ps_sheet;

/* ---- the template the frame is made from ---------------------------------
 *
 * Built here rather than compiled from a resource, the same way an
 * application without a .rc builds one. */

typedef struct {
    unsigned char *p;
    unsigned char *end;
} ps_buf;

static void ps_w(ps_buf *b, WORD v)
{
    if (b->p + 2 <= b->end) {
        b->p[0] = (unsigned char)v;
        b->p[1] = (unsigned char)(v >> 8);
    }
    b->p += 2;
}

static void ps_d(ps_buf *b, DWORD v)
{
    ps_w(b, (WORD)v);
    ps_w(b, (WORD)(v >> 16));
}

static void ps_sz(ps_buf *b, const char *s)
{
    if (s)
        while (*s)
            ps_w(b, (unsigned char)*s++);
    ps_w(b, 0);
}

static void ps_align(ps_buf *b)
{
    while (((UINT_PTR)b->p & 3) && b->p < b->end)
        *b->p++ = 0;
}

/* One control. `cls` names it when it is not one of the classes a template
 * can give by number, which the tab control is not. */
static void ps_item(ps_buf *b, DWORD style, int x, int y, int cx, int cy,
                    int id, WORD ord, const char *cls, const char *text)
{
    ps_align(b);
    ps_d(b, style);
    ps_d(b, 0); /* no extended style */
    ps_w(b, (WORD)x);
    ps_w(b, (WORD)y);
    ps_w(b, (WORD)cx);
    ps_w(b, (WORD)cy);
    ps_w(b, (WORD)id);
    if (cls) {
        ps_sz(b, cls);
    } else {
        ps_w(b, 0xFFFF); /* what says the class comes as a number */
        ps_w(b, ord);
    }
    ps_sz(b, text);
    ps_w(b, 0); /* no creation data */
}

/* ---- what the sheet is made of ------------------------------------------- */

static ps_sheet *sheet_of(HWND dlg)
{
    return (ps_sheet *)(INT_PTR)GetWindowLongPtrA(dlg, GWLP_USERDATA);
}

/* Ask one page a question, the way comctl32 does: a WM_NOTIFY carrying a
 * PSHNOTIFY, from the sheet, about the page. What the page answers is what
 * the sheet acts on. */
static LRESULT page_notify(ps_sheet *ps, int i, UINT code)
{
    PSHNOTIFY pn;
    if (i < 0 || i >= ps->count || !ps->page[i])
        return 0;
    memset(&pn, 0, sizeof(pn));
    pn.hdr.hwndFrom = ps->sheet;
    pn.hdr.idFrom = 0;
    pn.hdr.code = code;
    pn.lParam = 0;
    return SendMessageA(ps->page[i], WM_NOTIFY, 0, (LPARAM)&pn);
}

static void apply_enable(ps_sheet *ps)
{
    HWND apply = GetDlgItem(ps->sheet, IDD_APPLYNOW);
    int any = 0;
    for (int i = 0; i < ps->count; i++)
        any |= ps->changed[i];
    if (apply)
        EnableWindow(apply, any);
}

/* Every page that has something to keep is asked to keep it. A page that
 * refuses stops the rest and is brought to the front, which is what tells
 * the person which one it was. */
static void sheet_show(ps_sheet *ps, int want);

static int sheet_apply(ps_sheet *ps)
{
    for (int i = 0; i < ps->count; i++) {
        if (!ps->changed[i])
            continue;
        LRESULT said = page_notify(ps, i, PSN_APPLY);
        if (said == PSNRET_INVALID || said == PSNRET_INVALID_NOCHANGEPAGE) {
            if (said == PSNRET_INVALID)
                sheet_show(ps, i); /* show which page said no */
            return 0;
        }
        ps->changed[i] = 0;
    }
    apply_enable(ps);
    ps->result = 1;
    return 1;
}

/* Bring one page to the front. The one going back is asked first and may
 * refuse, which is how a page with something wrong in it keeps the keyboard. */
static void sheet_show(ps_sheet *ps, int want)
{
    if (want < 0 || want >= ps->count || want == ps->current)
        return;
    if (ps->current >= 0 && page_notify(ps, ps->current, PSN_KILLACTIVE))
        return; /* it said no */
    if (ps->current >= 0)
        ShowWindow(ps->page[ps->current], SW_HIDE);
    ps->current = want;
    ShowWindow(ps->page[want], SW_SHOW);
    SendMessageA(ps->tabs, TCM_SETCURSEL, (WPARAM)want, 0);
    page_notify(ps, want, PSN_SETACTIVE);
    {   /* The page that comes to the front takes the keyboard with it: what
         * the machine shows is a focus rectangle on the new page's first
         * item, not on the tabs that were clicked. A group of option buttons
         * hands the stop to whichever of them is set.
         *
         * A page that placed the keyboard itself — one whose WM_INITDIALOG
         * answered FALSE having called SetFocus, which is what the machine's
         * Properties page does when it puts it on the first attribute — is
         * shown with it where it put it, once. */
        HWND first = ween_tab_next(ps->page[want], NULL, 1);
        HWND put = ps->placed[want];
        ps->placed[want] = NULL; /* the once */
        if (put)
            SetFocus(put);
        else if (first)
            SetFocus(first);
    }
}

/* The sheet's procedure is a dialog's, so what it *answers* goes in
 * DWLP_MSGRESULT and what it returns says only that it dealt with the
 * message. A program asking the sheet a question reads the former. */
static INT_PTR sheet_answer(HWND dlg, LRESULT v)
{
    SetWindowLongPtrA(dlg, DWLP_MSGRESULT, (LONG_PTR)v);
    return TRUE;
}

static INT_PTR CALLBACK sheet_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    ps_sheet *ps = sheet_of(dlg);
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrA(dlg, GWLP_USERDATA, (LONG_PTR)lp);
        return TRUE;
    case WM_NOTIFY: {
        const NMHDR *nm = (const NMHDR *)lp;
        if (ps && nm && nm->hwndFrom == ps->tabs && nm->code == TCN_SELCHANGE) {
            int want = (int)SendMessageA(ps->tabs, TCM_GETCURSEL, 0, 0);
            sheet_show(ps, want);
            /* the tab moved itself when it was clicked; if the page would not
             * let go, sheet_show left ps->current alone and the tab goes back */
            SendMessageA(ps->tabs, TCM_SETCURSEL, (WPARAM)ps->current, 0);
        }
        return TRUE;
    }
    case PSM_CHANGED:
    case PSM_UNCHANGED:
        if (ps)
            for (int i = 0; i < ps->count; i++)
                if (ps->page[i] == (HWND)wp) {
                    ps->changed[i] = msg == PSM_CHANGED;
                    apply_enable(ps);
                }
        return TRUE;
    case PSM_APPLY:
        return sheet_answer(dlg, ps ? sheet_apply(ps) : 0);
    case PSM_GETTABCONTROL:
        return sheet_answer(dlg, ps ? (LRESULT)(INT_PTR)ps->tabs : 0);
    case PSM_GETCURRENTPAGEHWND:
        return sheet_answer(dlg,
                            ps && ps->current >= 0
                                ? (LRESULT)(INT_PTR)ps->page[ps->current]
                                : 0);
    case PSM_SETCURSEL:
        if (ps)
            sheet_show(ps, (int)wp);
        return TRUE;
    case WM_COMMAND:
        if (!ps)
            return FALSE;
        switch (LOWORD(wp)) {
        case IDOK:
            if (page_notify(ps, ps->current, PSN_KILLACTIVE))
                return TRUE;
            if (!sheet_apply(ps))
                return TRUE;
            EndDialog(dlg, ps->result ? ps->result : 1);
            return TRUE;
        case IDD_APPLYNOW:
            if (page_notify(ps, ps->current, PSN_KILLACTIVE))
                return TRUE;
            sheet_apply(ps);
            return TRUE;
        case IDCANCEL:
            for (int i = 0; i < ps->count; i++)
                page_notify(ps, i, PSN_RESET);
            EndDialog(dlg, ps->result);
            return TRUE;
        default:
            break;
        }
        return FALSE;
    case WM_CLOSE:
        SendMessageA(dlg, WM_COMMAND, IDCANCEL, 0);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

/* Past one of the three things a template's header ends with: a menu, a class
 * or a title. Each is nothing (a zero word), an ordinal (0xffff and a word),
 * or a string of wide characters. */
static const unsigned char *ps_skip_name(const unsigned char *p)
{
    WORD w = (WORD)(p[0] | (p[1] << 8));
    if (w == 0)
        return p + 2;
    if (w == 0xffff)
        return p + 4;
    while (w) {
        p += 2;
        w = (WORD)(p[0] | (p[1] << 8));
    }
    return p + 2;
}

/* The face and size a page is set in, so the frame can be set in the same one:
 * a sheet whose pages are the shell's face has its tabs and buttons in it too,
 * which is the difference between the machine's Properties and its Folder
 * Options. Zero back if the page named no font. */
static int ps_page_font(const unsigned char *t, char *face, size_t max,
                        int *points)
{
    DWORD style;
    const unsigned char *p;
    size_t i = 0;
    if (!t)
        return 0;
    style = (DWORD)(t[0] | (t[1] << 8) | ((DWORD)t[2] << 16) |
                    ((DWORD)t[3] << 24));
    if (!(style & DS_SETFONT))
        return 0;
    p = ps_skip_name(t + 18); /* menu */
    p = ps_skip_name(p);      /* class */
    p = ps_skip_name(p);      /* title */
    *points = (int)(WORD)(p[0] | (p[1] << 8));
    p += 2;
    while (i + 1 < max) {
        WORD c = (WORD)(p[0] | (p[1] << 8));
        if (!c)
            break;
        face[i++] = (char)c;
        p += 2;
    }
    face[i] = 0;
    return i != 0;
}

INT_PTR PropertySheetA(LPCPROPSHEETHEADERA header)
{
    static unsigned char tmpl[1024];
    ps_buf b;
    ps_sheet ps;
    RECT page_area, largest;
    int cx, cy, tab_h, i, n, sheet_w, sheet_h, y, bx;
    int points = 8;
    int no_apply, has_help;
    char face[64] = "MS Shell Dlg";
    INT_PTR r;

    if (!header || !header->ppsp || !header->nPages)
        return -1;
    n = (int)header->nPages;
    if (n > PS_MAX_PAGES)
        n = PS_MAX_PAGES;

    no_apply = (header->dwFlags & PSH_NOAPPLYNOW) != 0;
    has_help = (header->dwFlags & PSH_HASHELP) != 0;

    memset(&ps, 0, sizeof(ps));
    ps.count = n;
    ps.current = -1;
    for (i = 0; i < n; i++)
        ps.desc[i] = header->ppsp[i];

    /* The sheet is as big as the largest page wants, in dialog units, since
     * that is what a template says. The frame around it is the tab control's
     * own margins and the row of buttons. */
    largest.right = 0;
    largest.bottom = 0;
    for (i = 0; i < n; i++) {
        const unsigned char *t = (const unsigned char *)ps.desc[i].pResource;
        int w, h;
        if (!t)
            continue;
        w = (short)(t[14] | (t[15] << 8));
        h = (short)(t[16] | (t[17] << 8));
        if (w > largest.right)
            largest.right = w;
        if (h > largest.bottom)
            largest.bottom = h;
    }
    cx = (int)largest.right;
    cy = (int)largest.bottom;
    /* the frame is written in the same hand as its pages */
    for (i = 0; i < n; i++)
        if (ps_page_font((const unsigned char *)ps.desc[i].pResource, face,
                         sizeof(face), &points))
            break;

    /* A rough template: the frame is laid out in pixels once it exists, so
     * this only has to be big enough to hold the pieces. */
    tab_h = cy + 24;
    sheet_w = cx + 12;
    sheet_h = tab_h + 40;

    b.p = tmpl;
    b.end = tmpl + sizeof(tmpl);
    /* Not visible yet: the frame is made at a rough size, sized to its
     * largest page and filled with pages, and only then put up. A window that
     * appears first and is moved afterwards is seen in the wrong place. */
    ps_d(&b, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT);
    /* A sheet wears the question mark unless it was told not to, which is
     * what comctl32 does and what the machine's Folder Options has. */
    ps_d(&b, (header->dwFlags & PSH_NOCONTEXTHELP) ? 0
                                                  : (DWORD)WS_EX_CONTEXTHELP);
    ps_w(&b, (WORD)(1 + 4)); /* the tab control and four buttons */
    ps_w(&b, 0);
    ps_w(&b, 0);
    ps_w(&b, (WORD)sheet_w);
    ps_w(&b, (WORD)sheet_h);
    ps_sz(&b, NULL); /* no menu */
    ps_sz(&b, NULL); /* our own class */
    ps_sz(&b, header->pszCaption ? header->pszCaption : "Properties");
    ps_w(&b, (WORD)points);
    ps_sz(&b, face);

    ps_item(&b, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 3, 6, cx + 3, tab_h,
            PS_TAB, 0, WC_TABCONTROLA, "");
    y = tab_h + 12;
    bx = 10;
    ps_item(&b, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, bx, y,
            50, 14, IDOK, PS_ATOM_BUTTON, NULL, "OK");
    ps_item(&b, WS_CHILD | WS_VISIBLE | WS_TABSTOP, bx + 55, y, 50, 14,
            IDCANCEL, PS_ATOM_BUTTON, NULL, "Cancel");
    /* **All four exist; the flags decide which are seen.** §8.6 of WordPad's
     * specification is probe.exe's reading of the machine's own Options sheet,
     * and it has four children where a person sees two:
     *
     *     OK      id 1      50030000   visible
     *     Cancel  id 2      50010000   visible
     *     Apply   id 12321  48010000   WS_DISABLED, **no WS_VISIBLE**
     *     Help    id 9      40030000                **no WS_VISIBLE**
     *
     * Both invisible ones sit at 648, which is one button slot past Cancel
     * and one past the client's own right edge.
     *
     * So a sheet that asked for PSH_NOAPPLYNOW does not *lack* an Apply
     * button -- it has one nobody can see. That distinction is not pedantry
     * and it is not us copying an implementation: **a program can tell.**
     * `GetDlgItem(sheet, IDD_APPLYNOW)` answers a window in one case and
     * nothing in the other, and MFC's own CPropertySheet reaches for that id
     * to light the button when a page goes dirty. A program built against
     * real win32 finds it; against a library that did not create it, it would
     * not.
     *
     * The two instruments disagree here and both are right -- the probe reads
     * what the program stores and the capture shows what the user sees -- so
     * both are reproduced. */
    ps_item(&b, WS_CHILD | WS_TABSTOP | WS_DISABLED |
                    (no_apply ? 0 : WS_VISIBLE),
            bx + 110, y, 50, 14, IDD_APPLYNOW, PS_ATOM_BUTTON, NULL, "&Apply");
    ps_item(&b, WS_CHILD | WS_GROUP | WS_TABSTOP |
                    (has_help ? WS_VISIBLE : 0),
            bx + 165, y, 50, 14, IDHELP, PS_ATOM_BUTTON, NULL, "&Help");
    if (b.p > b.end)
        return -1;

    /* The frame is modeless while its pages are put in it, then run modally:
     * the pages have to exist before anyone can be shown one. */
    ps.sheet = CreateDialogIndirectParamA(header->hInstance,
                                          (LPCDLGTEMPLATEA)tmpl,
                                          header->hwndParent, sheet_proc,
                                          (LPARAM)&ps);
    if (!ps.sheet)
        return -1;
    ps.tabs = GetDlgItem(ps.sheet, PS_TAB);

    /* Now the frame, in pixels. The page's own size is what everything else
     * follows from, so it is asked for in dialog units — the template's own
     * measure — and turned into pixels the way the dialog manager would. */
    {
        RECT page = { 0, 0, cx, cy };
        RECT unit = { 0, 0, 4, 8 }; /* one unit across and one down */
        RECT tab, cr, wr;
        int tab_w, tab_h, strip, client_w, client_h, frame_w, frame_h, bx, by;
        /* A unit is wider than a pixel — six to four across and thirteen to
         * eight down — so a page's own edge falls half way through a pixel as
         * often as not. What the sheet is built around is the whole pixels
         * those units cover, and one more across, which is the margin the tab
         * control keeps beside a page. The machine's sheets come out pixel
         * for pixel that way, both this one and Folder Options. */
        MapDialogRect(ps.sheet, &unit);
        page.right = cx * unit.right / 4 + 1;
        page.bottom = cy * unit.bottom / 8;

        /* what the tab control must be to hold a page that size */
        tab.left = 0;
        tab.top = 0;
        tab.right = page.right;
        tab.bottom = page.bottom;
        SendMessageA(ps.tabs, TCM_ADJUSTRECT, TRUE, (LPARAM)&tab);
        tab_w = tab.right - tab.left;
        tab_h = tab.bottom - tab.top;
        strip = -tab.top;

        client_w = PS_TAB_X + tab_w + PS_RIGHT;
        client_h = PS_TAB_Y + tab_h + PS_BTN_TOP + PS_BTN_H + PS_BOTTOM;
        (void)cr;

        /* the window around that client: what this dialog's frame adds, which
         * is what AdjustWindowRect is for */
        GetWindowRect(ps.sheet, &wr);
        cr.left = 0;
        cr.top = 0;
        cr.right = client_w;
        cr.bottom = client_h;
        AdjustWindowRect(&cr, (DWORD)GetWindowLongA(ps.sheet, GWL_STYLE),
                         FALSE);
        frame_w = cr.right - cr.left;
        frame_h = cr.bottom - cr.top;
        MoveWindow(ps.sheet, wr.left, wr.top, frame_w, frame_h, FALSE);

        MoveWindow(ps.tabs, PS_TAB_X, PS_TAB_Y, tab_w, tab_h, FALSE);
        by = PS_TAB_Y + tab_h + PS_BTN_TOP;
        /* **The row is right-aligned on the tab control, and only the buttons
         * that are *seen* take a slot.** §8.6's machine has OK at client 278
         * and Cancel at 359 with Cancel's right edge at 434, which is its tab
         * control's own right edge -- so the rule is the same whether two or
         * three are showing, and a sheet that asked for PSH_NOAPPLYNOW does
         * not leave a gap where Apply would have been.
         *
         * The hidden ones are parked **one slot past the last visible**, both
         * at the same place, which is where the machine has them: Apply and
         * Help are both at 648, which is 359 + 81 and one past the client's
         * own right edge. They are out of sight either way; putting them
         * where the machine puts them costs nothing and means a program that
         * asks a hidden button where it is gets the machine's answer. */
        {
            int shown = 2 + (no_apply ? 0 : 1) + (has_help ? 1 : 0);
            int slot = PS_BTN_W + PS_BTN_GAP;
            int parked;
            bx = PS_TAB_X + tab_w - shown * PS_BTN_W - (shown - 1) * PS_BTN_GAP;
            MoveWindow(GetDlgItem(ps.sheet, IDOK), bx, by, PS_BTN_W, PS_BTN_H,
                       FALSE);
            bx += slot;
            MoveWindow(GetDlgItem(ps.sheet, IDCANCEL), bx, by, PS_BTN_W,
                       PS_BTN_H, FALSE);
            bx += slot;
            parked = PS_TAB_X + tab_w - PS_BTN_W + slot; /* one past the row */
            MoveWindow(GetDlgItem(ps.sheet, IDD_APPLYNOW),
                       no_apply ? parked : bx, by, PS_BTN_W, PS_BTN_H, FALSE);
            if (!no_apply)
                bx += slot;
            MoveWindow(GetDlgItem(ps.sheet, IDHELP),
                       has_help ? bx : parked, by, PS_BTN_W, PS_BTN_H, FALSE);
        }
        (void)strip;

    }

    for (i = 0; i < n; i++) {
        TCITEMA ti;
        memset(&ti, 0, sizeof(ti));
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPSTR)(ps.desc[i].dwFlags & PSP_USETITLE
                                 ? ps.desc[i].pszTitle
                                 : "");
        SendMessageA(ps.tabs, TCM_INSERTITEMA, (WPARAM)i, (LPARAM)&ti);
    }

    /* Where a page goes: the tab control's display area, past the tabs
     * themselves, said in the sheet's own coordinates — a page is the sheet's
     * child, not the tab control's, which is what puts its controls in the
     * same dialog as OK and Cancel and so in the same tab order. */
    {
        POINT at;
        GetClientRect(ps.tabs, &page_area);
        SendMessageA(ps.tabs, TCM_ADJUSTRECT, FALSE, (LPARAM)&page_area);
        at.x = page_area.left;
        at.y = page_area.top;
        ClientToScreen(ps.tabs, &at);
        ScreenToClient(ps.sheet, &at);
        page_area.right += at.x - page_area.left;
        page_area.bottom += at.y - page_area.top;
        page_area.left = at.x;
        page_area.top = at.y;
    }

    for (i = 0; i < n; i++) {
        if (!ps.desc[i].pResource)
            continue;
        ps.page[i] = CreateDialogIndirectParamA(ps.desc[i].hInstance,
                                                ps.desc[i].pResource, ps.sheet,
                                                ps.desc[i].pfnDlgProc,
                                                (LPARAM)&ps.desc[i]);
        if (!ps.page[i])
            continue;
        /* Where it put the keyboard itself, if it did: a page that answered
         * FALSE to WM_INITDIALOG having called SetFocus is shown with the
         * keyboard where it put it, and one that let the dialog manager place
         * it is shown with the keyboard on whatever its first tab stop is by
         * then — which for a group of option buttons is the one that is set,
         * and is not known until the page has been told what to set. */
        ps.placed[i] = ps.page[i]->dlg_placed_focus;
        /* what is in it is reachable from the sheet's tab ring */
        SetWindowLongA(ps.page[i], GWL_EXSTYLE,
                       GetWindowLongA(ps.page[i], GWL_EXSTYLE) |
                           WS_EX_CONTROLPARENT);
        MoveWindow(ps.page[i], page_area.left, page_area.top,
                   page_area.right - page_area.left,
                   page_area.bottom - page_area.top, FALSE);
        ShowWindow(ps.page[i], SW_HIDE);
        /* Tab goes tabs, then what is on the page, then OK and Cancel. The
         * buttons are in the frame's template and so were made first; the
         * page is moved to sit right behind the tab control, which is the
         * order the ring is taken in. */
        {
            struct ween_wnd **link = &ps.sheet->first_child;
            while (*link && *link != ps.page[i])
                link = &(*link)->next_sibling;
            if (*link) {
                *link = ps.page[i]->next_sibling;
                ps.page[i]->next_sibling = ps.tabs->next_sibling;
                ps.tabs->next_sibling = ps.page[i];
            }
        }
    }

    i = (int)header->nStartPage;
    if (i < 0 || i >= n)
        i = 0;
    sheet_show(&ps, i);
    ShowWindow(ps.sheet, SW_SHOW); /* now it is worth looking at */

    {   /* the owner goes down for as long as the sheet is up */
        int off = header->hwndParent &&
                  !(header->hwndParent->style & WS_DISABLED);
        if (off)
            EnableWindow(header->hwndParent, FALSE);
        r = ween_dialog_modal(ps.sheet, header->hwndParent, off);
    }
    return r;
}
