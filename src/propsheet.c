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

/* The gaps around a page inside the sheet, in pixels, as the machine has
 * them: the tab control is inset from the frame, and the buttons sit in a
 * band below it. */
#define PS_MARGIN 7   /* frame to tab control */
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

INT_PTR PropertySheetA(LPCPROPSHEETHEADERA header)
{
    static unsigned char tmpl[1024];
    ps_buf b;
    ps_sheet ps;
    RECT page_area, largest;
    int cx, cy, tab_h, i, n, sheet_w, sheet_h, y, bx;
    INT_PTR r;

    if (!header || !header->ppsp || !header->nPages)
        return -1;
    n = (int)header->nPages;
    if (n > PS_MAX_PAGES)
        n = PS_MAX_PAGES;

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

    /* Everything past this point is in dialog units, because the template is:
     * four horizontally and eight vertically to the character cell. */
#define PS_DX(px) MulDiv((px), 4, 6)
#define PS_DY(px) MulDiv((px), 8, 13)
    tab_h = PS_DY(21); /* the strip the tabs themselves take */
    sheet_w = cx + 2 * PS_DX(PS_MARGIN) + PS_DX(6);
    sheet_h = tab_h + cy + PS_DY(PS_MARGIN + PS_BTN_TOP + PS_BTN_H + PS_MARGIN);

    b.p = tmpl;
    b.end = tmpl + sizeof(tmpl);
    ps_d(&b, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | DS_MODALFRAME |
                 DS_SETFONT);
    ps_d(&b, 0);
    ps_w(&b, (WORD)(1 + 3)); /* the tab control and three buttons */
    ps_w(&b, 0);
    ps_w(&b, 0);
    ps_w(&b, (WORD)sheet_w);
    ps_w(&b, (WORD)sheet_h);
    ps_sz(&b, NULL); /* no menu */
    ps_sz(&b, NULL); /* our own class */
    ps_sz(&b, header->pszCaption ? header->pszCaption : "Properties");
    ps_w(&b, 8); /* point size */
    ps_sz(&b, "MS Shell Dlg");

    ps_item(&b, WS_CHILD | WS_VISIBLE | WS_TABSTOP, PS_DX(PS_MARGIN),
            PS_DY(PS_MARGIN), sheet_w - 2 * PS_DX(PS_MARGIN),
            tab_h + cy + PS_DY(PS_MARGIN), PS_TAB, 0, WC_TABCONTROLA, "");

    y = PS_DY(PS_MARGIN) + tab_h + cy + PS_DY(PS_MARGIN + PS_BTN_TOP);
    bx = sheet_w - PS_DX(PS_MARGIN) - 3 * PS_DX(PS_BTN_W) - 2 * PS_DX(PS_BTN_GAP);
    ps_item(&b, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, bx, y,
            PS_DX(PS_BTN_W), PS_DY(PS_BTN_H), IDOK, PS_ATOM_BUTTON, NULL, "OK");
    bx += PS_DX(PS_BTN_W + PS_BTN_GAP);
    ps_item(&b, WS_CHILD | WS_VISIBLE | WS_TABSTOP, bx, y, PS_DX(PS_BTN_W),
            PS_DY(PS_BTN_H), IDCANCEL, PS_ATOM_BUTTON, NULL, "Cancel");
    bx += PS_DX(PS_BTN_W + PS_BTN_GAP);
    ps_item(&b, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED, bx, y,
            PS_DX(PS_BTN_W), PS_DY(PS_BTN_H), IDD_APPLYNOW, PS_ATOM_BUTTON, NULL,
            "Apply");
#undef PS_DX
#undef PS_DY
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
        /* what is in it is reachable from the sheet's tab ring */
        SetWindowLongA(ps.page[i], GWL_EXSTYLE,
                       GetWindowLongA(ps.page[i], GWL_EXSTYLE) |
                           WS_EX_CONTROLPARENT);
        MoveWindow(ps.page[i], page_area.left, page_area.top,
                   page_area.right - page_area.left,
                   page_area.bottom - page_area.top, FALSE);
        ShowWindow(ps.page[i], SW_HIDE);
    }

    i = (int)header->nStartPage;
    if (i < 0 || i >= n)
        i = 0;
    sheet_show(&ps, i);

    {   /* the owner goes down for as long as the sheet is up */
        int off = header->hwndParent &&
                  !(header->hwndParent->style & WS_DISABLED);
        if (off)
            EnableWindow(header->hwndParent, FALSE);
        r = ween_dialog_modal(ps.sheet, header->hwndParent, off);
    }
    return r;
}
