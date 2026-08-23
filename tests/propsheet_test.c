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

enum { ID_CHECK = 200, ID_EDIT = 201 };

static int g_active[2], g_kill[2], g_apply[2], g_reset[2];
static HWND g_page[2];
static int g_step;
/* Gathered while the sheet is still up: it and its pages are gone by the time
 * PropertySheetA returns, which is what makes it modal. */
static int g_had_tabs, g_same_sheet, g_two_pages;

/* Both pages answer the same way; which one is telling is in `which`. */
static INT_PTR page_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp, int which)
{
    HWND sheet = GetParent(dlg);
    (void)wp;
    switch (msg) {
    case WM_INITDIALOG:
        g_page[which] = dlg;
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
                PostMessageA(sheet, WM_COMMAND, IDOK, 0);
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
    CHECK(g_active[0] == 1, "the first page was told it was at the front");
    CHECK(g_active[1] == 1, "and the second when it was moved to");
    CHECK(g_kill[0] == 1,
          "the one going to the back was asked first, and let go");
    CHECK(g_apply[0] == 1,
          "OK asked the page that had something to keep to keep it");
    CHECK(g_apply[1] == 0, "and did not ask the one that had nothing");
    CHECK(g_reset[0] == 0 && g_reset[1] == 0, "nothing was thrown away");
    CHECK(r > 0, "and the sheet said it was answered rather than cancelled");

    /* The pieces are reachable the way a program expects: the sheet is the
     * page's parent, and it owns the tabs. */
    CHECK(g_had_tabs, "the sheet hands over its tab control");
    CHECK(g_same_sheet, "and both pages hang off the same sheet");

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("propsheet_test: all passed\n");
    return 0;
}
