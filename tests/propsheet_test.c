/* A property sheet: a dialog with a row of tabs and a page behind each.
 *
 * The pages drive the sheet from inside — a page hears that it has come to
 * the front and posts the next step — so the test needs no coordinates and
 * no guess about where a tab landed. Through the headless backend, so CI
 * runs it. */

#define _POSIX_C_SOURCE 200112L /* setenv */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ween_internal.h"
#include "../examples/win32_dlg.h"

static int g_failures = 0;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("ok   %s\n", name);                                         \
        } else {                                                               \
            printf("FAIL %s\n", name);                                         \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

enum { ID_CHECK = 200, ID_EDIT = 201, ID_CHECK2 = 202 };

static int g_active[2], g_kill[2], g_apply[2], g_reset[2];
static HWND g_page[2];
static int g_step;
/* The first page places the keyboard itself, the way a Properties page does —
 * on its second item, not the one the sheet would have picked — and what it
 * set is what still has the keyboard once the sheet has put the page up. */
static HWND g_placed, g_had_focus;
/* Gathered while the sheet is still up: it and its pages are gone by the time
 * PropertySheetA returns, which is what makes it modal. */
static int g_had_tabs, g_same_sheet, g_two_pages;
static int g_ctrltab_wrapped;
static HWND g_focus_before_ctrltab, g_focus_after_ctrltab;
/* What PSH_NOAPPLYNOW does to the four buttons, asked from inside a page. */
static int g_btn_exists[4], g_btn_visible[4], g_ok_x, g_cancel_x, g_apply_x;

/* Both pages answer the same way; which one is telling is in `which`. */
static INT_PTR page_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp, int which)
{
    HWND sheet = GetParent(dlg);
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG:
        g_page[which] = dlg;
        if (which == 0) { /* answering FALSE says the page placed it */
            g_placed = GetDlgItem(dlg, ID_CHECK2);
            SetFocus(g_placed);
            return FALSE;
        }
        return TRUE;
    case WM_USER + 101:
        g_focus_after_ctrltab = GetFocus();
        PostMessageA(sheet, WM_COMMAND, IDOK, 0);
        return TRUE;
    case WM_USER + 100:
        g_focus_before_ctrltab = GetFocus();
        PostMessageA(sheet, WM_KEYDOWN, VK_TAB, 1L << 28);
        return TRUE;
    case WM_NOTIFY: {
        const NMHDR *nm = (const NMHDR *)lp;
        if (!nm)
            return FALSE;
        switch (nm->code) {
        case PSN_SETACTIVE:
            g_active[which]++;
            if (PropSheet_GetTabControl(sheet))
                g_had_tabs = 1;
            {
                /* **All four exist whatever the flags say; only some are
                 * seen.** WordPad's §8.6 read the machine's own Options sheet
                 * and found four children where a person sees two -- Apply
                 * disabled and invisible, Help invisible -- so a program that
                 * asks by id finds them, which is what MFC's CPropertySheet
                 * does to light Apply when a page goes dirty. */
                static const int ids[4] = { IDOK, IDCANCEL, IDD_APPLYNOW,
                                            IDHELP };
                RECT r, sr;
                int i;
                GetWindowRect(sheet, &sr);
                for (i = 0; i < 4; i++) {
                    HWND b = GetDlgItem(sheet, ids[i]);
                    g_btn_exists[i] = b != NULL;
                    g_btn_visible[i] = b && IsWindowVisible(b);
                }
                if (GetDlgItem(sheet, IDOK)) {
                    GetWindowRect(GetDlgItem(sheet, IDOK), &r);
                    g_ok_x = r.left - sr.left;
                }
                if (GetDlgItem(sheet, IDCANCEL)) {
                    GetWindowRect(GetDlgItem(sheet, IDCANCEL), &r);
                    g_cancel_x = r.left - sr.left;
                }
                if (GetDlgItem(sheet, IDD_APPLYNOW)) {
                    GetWindowRect(GetDlgItem(sheet, IDD_APPLYNOW), &r);
                    g_apply_x = r.left - sr.left;
                }
            }
            if (g_page[0] && g_page[1]) {
                g_two_pages = g_page[0] != g_page[1];
                g_same_sheet = GetParent(g_page[0]) == GetParent(g_page[1]);
            }
            /* Drive the sheet on from here, one posted step at a time, so
             * nothing happens inside the notification that caused it. */
            if (g_step == 0 && which == 0) {
                g_step = 1;
                PropSheet_Changed(sheet, dlg); /* something worth keeping */
                PostMessageA(sheet, PSM_SETCURSEL, 1, 0);
            } else if (g_step == 1 && which == 1) {
                g_step = 2;
                g_had_focus = GetFocus();
                /* **Ctrl+Tab from the last page.** Measured on the machine:
                 * it advances one and wraps -- Embedded came round to Options
                 * -- where the tab control's own arrows stop dead at either
                 * end. Posted to the sheet so it goes through the modal
                 * loop's IsDialogMessageA, which is the only path that
                 * carries it; a Ctrl+Tab sent straight to a control would
                 * prove nothing about the route. */
                /* Read the focus *after* the page switch has settled, not
                 * here: PSN_SETACTIVE is sent before sheet_show places the
                 * keyboard, so a reading taken now is the previous page's and
                 * the check would compare two different moments. */
                PostMessageA(dlg, WM_USER + 100, 0, 0);
            } else if (g_step == 2 && which == 0) {
                g_step = 3;
                g_ctrltab_wrapped = 1;
                PostMessageA(dlg, WM_USER + 101, 0, 0);
            }
            return TRUE;
        case PSN_KILLACTIVE:
            g_kill[which]++;
            SetWindowLongPtrA(dlg, DWLP_MSGRESULT, FALSE); /* let go */
            return TRUE;
        case PSN_APPLY:
            g_apply[which]++;
            return TRUE;
        case PSN_RESET:
            g_reset[which]++;
            return TRUE;
        default:
            return FALSE;
        }
    }
    default:
        return FALSE;
    }
}

static INT_PTR CALLBACK page0(HWND d, UINT m, WPARAM w, LPARAM l)
{
    return page_proc(d, m, w, l, 0);
}

static INT_PTR CALLBACK page1(HWND d, UINT m, WPARAM w, LPARAM l)
{
    return page_proc(d, m, w, l, 1);
}

int main(void)
{
    static unsigned char t0[2000], t1[2000];
    dlg_item a[4], b[4];
    int na = 0, nb = 0;
    PROPSHEETPAGEA pages[2];
    PROPSHEETHEADERA hdr;
    INT_PTR r;

    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();
    memset(a, 0, sizeof(a)); /* a field nobody sets is not a stray */
    memset(b, 0, sizeof(b));

    a[na].style = WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP;
    a[na].x = 7;
    a[na].y = 7;
    a[na].cx = 120;
    a[na].cy = 10;
    a[na].id = ID_CHECK;
    a[na].cls = ATOM_BUTTON;
    a[na].text = "A &check box";
    na++;
    a[na] = a[na - 1];
    a[na].y = 24;
    a[na].id = ID_CHECK2;
    a[na].text = "A&nother";
    na++;
    CHECK(build_dialog_template(t0, sizeof t0,
                                WS_CHILD | DS_SETFONT | WS_EX_CONTROLPARENT,
                                180, 90, "", a, na) > 0,
          "a page's template is built the way any dialog's is");

    b[nb].style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_LEFT;
    b[nb].x = 7;
    b[nb].y = 7;
    b[nb].cx = 120;
    b[nb].cy = 12;
    b[nb].id = ID_EDIT;
    b[nb].cls = ATOM_EDIT;
    b[nb].text = "typed";
    nb++;
    build_dialog_template(t1, sizeof t1, WS_CHILD | DS_SETFONT, 200, 70, "", b,
                          nb);

    memset(pages, 0, sizeof(pages));
    pages[0].dwSize = sizeof(pages[0]);
    pages[0].dwFlags = PSP_DLGINDIRECT | PSP_USETITLE;
    pages[0].pResource = (LPCDLGTEMPLATEA)t0;
    pages[0].pszTitle = "General";
    pages[0].pfnDlgProc = page0;
    pages[1] = pages[0];
    pages[1].pResource = (LPCDLGTEMPLATEA)t1;
    pages[1].pszTitle = "View";
    pages[1].pfnDlgProc = page1;

    memset(&hdr, 0, sizeof(hdr));
    hdr.dwSize = sizeof(hdr);
    hdr.dwFlags = PSH_PROPSHEETPAGE;
    hdr.pszCaption = "Folder Options";
    hdr.nPages = 2;
    hdr.ppsp = pages;

    r = PropertySheetA(&hdr);

    CHECK(g_two_pages, "each page is a dialog of its own");
    /* Twice, not once: the sheet goes 0 -> 1 by PSM_SETCURSEL and then
     * 1 -> 0 by Ctrl+Tab wrapping round, so the first page comes to the
     * front at the start and again at the end. */
    CHECK(g_active[0] == 2, "the first page was told it was at the front");
    CHECK(g_active[1] == 1, "and the second when it was moved to");
    /* Also twice, and for the same reason plus one: page 0 is asked when it
     * goes to the back, page 1 is asked when Ctrl+Tab leaves it, and page 0
     * is asked again when OK closes the sheet on it. */
    CHECK(g_kill[0] == 2,
          "the one going to the back was asked first, and let go");
    CHECK(g_kill[1] == 1, "and the page Ctrl+Tab left was asked as well");
    CHECK(g_apply[0] == 1,
          "OK asked the page that had something to keep to keep it");
    CHECK(g_apply[1] == 0, "and did not ask the one that had nothing");
    CHECK(g_reset[0] == 0 && g_reset[1] == 0, "nothing was thrown away");
    CHECK(r > 0, "and the sheet said it was answered rather than cancelled");

    /* The pieces are reachable the way a program expects: the sheet is the
     * page's parent, and it owns the tabs. */
    CHECK(g_placed && g_had_focus == g_placed,
          "a page that placed the keyboard itself keeps it");
    CHECK(g_had_tabs, "the sheet hands over its tab control");

    /* ---- Ctrl+Tab ----------------------------------------------------------
     *
     * Measured on the machine, and the second of these cost a correction: it
     * looked at first as though Ctrl+Tab kept the focus *in the page*,
     * because every run that showed it had started with the focus in a page.
     * Pressed with the tabs focused the ring stays on the tabs. The simpler
     * rule is the true one -- change the page and touch nothing else.
     */
    CHECK(g_ctrltab_wrapped,
          "Ctrl+Tab from the last page comes round to the first, where the "
          "arrows stop dead");
    CHECK(g_focus_before_ctrltab && g_focus_after_ctrltab &&
              g_focus_before_ctrltab == g_focus_after_ctrltab,
          "and it does not move the keyboard, which a click does");
    CHECK(g_same_sheet, "and both pages hang off the same sheet");

    /* ---- PSH_NOAPPLYNOW ----------------------------------------------------
     *
     * Without the flag, three buttons are seen. All four exist either way.
     */
    CHECK(g_btn_exists[0] && g_btn_exists[1] && g_btn_exists[2] &&
              g_btn_exists[3],
          "all four buttons exist, Help included, whatever is shown");
    CHECK(g_btn_visible[0] && g_btn_visible[1] && g_btn_visible[2],
          "OK, Cancel and Apply are seen when the sheet did not say otherwise");
    CHECK(!g_btn_visible[3], "and Help is not, nothing having asked for it");
    CHECK(g_cancel_x - g_ok_x == g_apply_x - g_cancel_x,
          "the three that are seen are evenly spaced");
    {
        int ok3 = g_ok_x, cancel3 = g_cancel_x, slot;
        PROPSHEETPAGEA pages2[2];
        PROPSHEETHEADERA hdr2;
        slot = cancel3 - ok3;

        memset(g_btn_exists, 0, sizeof(g_btn_exists));
        memset(g_btn_visible, 0, sizeof(g_btn_visible));
        g_step = 0;
        memcpy(pages2, pages, sizeof(pages2));
        memset(&hdr2, 0, sizeof(hdr2));
        hdr2.dwSize = sizeof(hdr2);
        hdr2.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
        hdr2.pszCaption = "Folder Options";
        hdr2.nPages = 2;
        hdr2.ppsp = pages2;
        PropertySheetA(&hdr2);

        /* **The picture loses a button and the program does not.** WordPad's
         * §8.6 is the machine's own Options sheet: two buttons drawn, four
         * children, Apply disabled and invisible. */
        CHECK(g_btn_exists[2],
              "PSH_NOAPPLYNOW still leaves an Apply button to be found");
        CHECK(!g_btn_visible[2], "it is simply not seen");
        CHECK(g_btn_visible[0] && g_btn_visible[1],
              "OK and Cancel still are");
        /* And the two that remain close up rather than leaving a gap: the row
         * is right-aligned, so dropping one moves both right by a slot. */
        CHECK(g_ok_x == ok3 + slot && g_cancel_x == cancel3 + slot,
              "and the two that are left move up into the space");
        CHECK(g_apply_x > g_cancel_x,
              "the hidden one is parked past them, where the machine has it");
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("propsheet_test: all passed\n");
    return 0;
}
