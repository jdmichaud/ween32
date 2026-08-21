/* The control sampler: one window holding every control on ween32's roadmap.
 *
 * This file has two jobs, and compiles unchanged for both:
 *
 *   - against real <windows.h> it is the *reference*: the genuine win32
 *     controls, rendered by Wine's classic theme, which is the Windows 2000
 *     look ween32 is chasing (tools/refcapture/capture.sh drives it);
 *   - against ween32 it is an *example*: the same window, with the controls
 *     ween32 has actually implemented.
 *
 * The two renders are meant to be diffed, so nothing here is laid out in
 * dialog units — every control sits at a fixed pixel position, identical in
 * both worlds.
 *
 * A control appears on the ween32 side once the header announces it with a
 * WEEN32_HAS_* define. Until then its block is compiled out here and the
 * window simply has a hole where it will go, so the diff against the
 * reference is the to-do list. See ROADMAP.md.
 */

#include <ween32.h>

#ifdef _WIN32
#include <commctrl.h>
#include <string.h>
#define HAVE(feature) 1
#else
/* An undefined identifier evaluates to 0 in #if, so a control that ween32 has
 * not announced yet is simply absent. */
#define HAVE(feature) WEEN32_HAS_##feature
#define ZeroMemory(p, n) memset((p), 0, (n))
#include <string.h>
#endif

static HFONT g_font;

static HWND mk(const char *cls, const char *text, DWORD style, DWORD ex, int x,
               int y, int w, int h, HWND parent, int id)
{
    HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y,
                             w, h, parent, (HMENU)(UINT_PTR)id, NULL, NULL);
    if (c && g_font)
        SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

/* ---- the controls, one block each ------------------------------------- */

static void buttons(HWND w)
{
    mk("BUTTON", "Click me", BS_PUSHBUTTON | WS_TABSTOP, 0, 12, 12, 75, 23, w, 0);
    mk("BUTTON", "OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 95, 12, 75, 23, w, 0);
#if HAVE(DISABLED)
    HWND dis = mk("BUTTON", "I cannot be clicked", BS_PUSHBUTTON, 0, 178, 12,
                  130, 23, w, 0);
    EnableWindow(dis, FALSE);
#endif
}

#if HAVE(CHECKBOX)
static void checkboxes(HWND w)
{
    mk("BUTTON", "This is a checkbox", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 12, 46,
       130, 18, w, 0);
    HWND ck = mk("BUTTON", "I am checked", BS_AUTOCHECKBOX, 0, 12, 66, 130, 18,
                 w, 0);
    SendMessageA(ck, BM_SETCHECK, BST_CHECKED, 0);
#if HAVE(DISABLED)
    HWND ci = mk("BUTTON", "I am inactive", BS_AUTOCHECKBOX, 0, 12, 86, 130, 18,
                 w, 0);
    EnableWindow(ci, FALSE);
    HWND cic = mk("BUTTON", "Inactive but checked", BS_AUTOCHECKBOX, 0, 12, 106,
                  150, 18, w, 0);
    SendMessageA(cic, BM_SETCHECK, BST_CHECKED, 0);
    EnableWindow(cic, FALSE);
#endif
}
#endif

#if HAVE(GROUPBOX) || HAVE(RADIO)
static void options(HWND w)
{
#if HAVE(GROUPBOX)
    mk("BUTTON", "Select one:", BS_GROUPBOX, 0, 178, 42, 150, 86, w, 0);
#endif
#if HAVE(RADIO)
    HWND r1 = mk("BUTTON", "Yes", BS_AUTORADIOBUTTON | WS_GROUP, 0, 188, 60, 120,
                 18, w, 0);
    SendMessageA(r1, BM_SETCHECK, BST_CHECKED, 0);
    mk("BUTTON", "No", BS_AUTORADIOBUTTON, 0, 188, 80, 120, 18, w, 0);
#if HAVE(DISABLED)
    HWND r3 = mk("BUTTON", "Disabled", BS_AUTORADIOBUTTON, 0, 188, 100, 120, 18,
                 w, 0);
    EnableWindow(r3, FALSE);
#endif
#endif
}
#endif

static void labels(HWND w)
{
    mk("STATIC", "Occupation:", SS_LEFT, 0, 12, 140, 70, 16, w, 0);
    mk("STATIC", "Volume:", SS_LEFT, 0, 12, 252, 50, 16, w, 0);
}

#if HAVE(EDIT)
static void edits(HWND w)
{
    mk("EDIT", "Web developer", ES_LEFT | WS_TABSTOP, WS_EX_CLIENTEDGE, 84, 137,
       120, 20, w, 0);
#if HAVE(DISABLED)
    HWND ed = mk("EDIT", "Disabled text", ES_LEFT, WS_EX_CLIENTEDGE, 212, 137,
                 116, 20, w, 0);
    EnableWindow(ed, FALSE);
#endif
    mk("EDIT", "Multi-line edit\r\nwith a vertical scroll bar.",
       ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
       WS_EX_CLIENTEDGE, 12, 162, 316, 52, w, 0);
}
#endif

#if HAVE(COMBOBOX)
static void dropdown(HWND w)
{
    HWND cb = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
                 12, 222, 150, 200, w, 0);
    SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "5 - Incredible!");
    SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "4 - Great!");
    SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "3 - Pretty good");
    SendMessageA(cb, CB_SETCURSEL, 0, 0);
}
#endif

#if HAVE(LISTBOX)
static void listbox(HWND w)
{
    HWND lb = mk("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP,
                 WS_EX_CLIENTEDGE, 178, 222, 150, 62, w, 0);
    SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM) "Item one");
    SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM) "Item two (selected)");
    SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM) "Item three");
    SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM) "Item four");
    SendMessageA(lb, LB_SETCURSEL, 1, 0);
}
#endif

#if HAVE(TRACKBAR)
static void trackbars(HWND w)
{
    HWND tb = mk(TRACKBAR_CLASSA, "", TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, 0,
                 62, 248, 100, 30, w, 0);
    SendMessageA(tb, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10));
    SendMessageA(tb, TBM_SETPOS, TRUE, 4);
    HWND tv = mk(TRACKBAR_CLASSA, "", TBS_AUTOTICKS | TBS_VERT | TBS_BOTH, 0, 12,
                 274, 30, 66, w, 0);
    SendMessageA(tv, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10));
    SendMessageA(tv, TBM_SETPOS, TRUE, 6);
}
#endif

#if HAVE(PROGRESS)
static void progress(HWND w)
{
    /* segmented is the classic default; PBS_SMOOTH is the solid bar */
    HWND p1 = mk(PROGRESS_CLASSA, "", 0, WS_EX_CLIENTEDGE, 62, 288, 266, 20, w, 0);
    SendMessageA(p1, PBM_SETPOS, 40, 0);
    HWND p2 = mk(PROGRESS_CLASSA, "", PBS_SMOOTH, WS_EX_CLIENTEDGE, 62, 314, 266,
                 20, w, 0);
    SendMessageA(p2, PBM_SETPOS, 65, 0);
}
#endif

#if HAVE(SCROLLBAR)
static void scrollbar(HWND w)
{
    mk("SCROLLBAR", "", SBS_HORZ, 0, 12, 344, 316, 17, w, 0);
}
#endif

#if HAVE(TABS)
static void tabs(HWND w)
{
    HWND tc = mk(WC_TABCONTROLA, "", WS_TABSTOP, 0, 344, 12, 300, 130, w, 0);
    const char *names[] = { "Desktop", "My computer", "Control panel", "Devices" };
    for (int i = 0; i < 4; i++) {
        TCITEMA ti;
        ZeroMemory(&ti, sizeof ti);
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPSTR)names[i];
        SendMessageA(tc, TCM_INSERTITEMA, i, (LPARAM)&ti);
    }
}
#endif

#if HAVE(TREEVIEW)
static void treeview(HWND w)
{
    HWND tr = mk(WC_TREEVIEWA, "",
                 TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | WS_TABSTOP,
                 WS_EX_CLIENTEDGE, 344, 150, 145, 130, w, 0);
    TVINSERTSTRUCTA is;
    ZeroMemory(&is, sizeof is);
    is.hParent = TVI_ROOT;
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT;
    is.item.pszText = (LPSTR) "Table of Contents";
    HTREEITEM root = (HTREEITEM)SendMessageA(tr, TVM_INSERTITEMA, 0, (LPARAM)&is);
    const char *kids[] = { "What is web development?", "CSS", "JavaScript", "HTML" };
    for (int i = 0; i < 4; i++) {
        is.hParent = root;
        is.item.pszText = (LPSTR)kids[i];
        HTREEITEM k = (HTREEITEM)SendMessageA(tr, TVM_INSERTITEMA, 0, (LPARAM)&is);
        if (i == 1) { /* one expanded branch, to show the connector lines */
            is.hParent = k;
            is.item.pszText = (LPSTR) "Selectors";
            SendMessageA(tr, TVM_INSERTITEMA, 0, (LPARAM)&is);
            is.item.pszText = (LPSTR) "Specificity";
            SendMessageA(tr, TVM_INSERTITEMA, 0, (LPARAM)&is);
        }
    }
    SendMessageA(tr, TVM_EXPAND, TVE_EXPAND, (LPARAM)root);
}
#endif

#if HAVE(LISTVIEW)
static void listview(HWND w)
{
    HWND lv = mk(WC_LISTVIEWA, "", LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                 WS_EX_CLIENTEDGE, 499, 150, 145, 130, w, 0);
    LVCOLUMNA col;
    ZeroMemory(&col, sizeof col);
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 80;
    col.pszText = (LPSTR) "Name";
    SendMessageA(lv, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
    col.cx = 60;
    col.pszText = (LPSTR) "Version";
    SendMessageA(lv, LVM_INSERTCOLUMNA, 1, (LPARAM)&col);
    const char *rows[][2] = { { "MySQL ODBC", "3.51.11" },
                              { "SQL Server", "3.70.06" },
                              { "Oracle", "8.01.06" } };
    for (int i = 0; i < 3; i++) {
        LVITEMA it;
        ZeroMemory(&it, sizeof it);
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = (LPSTR)rows[i][0];
        SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);
        it.iSubItem = 1;
        it.pszText = (LPSTR)rows[i][1];
        SendMessageA(lv, LVM_SETITEMTEXTA, i, (LPARAM)&it);
    }
    ListView_SetItemState(lv, 1, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
}
#endif

#if HAVE(STATUSBAR)
static void statusbar(HWND w)
{
    HWND sb = CreateWindowExA(0, STATUSCLASSNAMEA, "",
                              WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                              w, NULL, NULL, NULL);
    if (g_font)
        SendMessageA(sb, WM_SETFONT, (WPARAM)g_font, TRUE);
    int parts[] = { 200, 380, -1 };
    SendMessageA(sb, SB_SETPARTS, 3, (LPARAM)parts);
    SendMessageA(sb, SB_SETTEXTA, 0, (LPARAM) "Press F1 for help");
    SendMessageA(sb, SB_SETTEXTA, 1, (LPARAM) "Slide 1");
    SendMessageA(sb, SB_SETTEXTA, 2, (LPARAM) "CPU Usage: 14%");
}
#endif

static void build(HWND w)
{
    g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    buttons(w);
    labels(w);
#if HAVE(CHECKBOX)
    checkboxes(w);
#endif
#if HAVE(GROUPBOX) || HAVE(RADIO)
    options(w);
#endif
#if HAVE(EDIT)
    edits(w);
#endif
#if HAVE(COMBOBOX)
    dropdown(w);
#endif
#if HAVE(LISTBOX)
    listbox(w);
#endif
#if HAVE(TRACKBAR)
    trackbars(w);
#endif
#if HAVE(PROGRESS)
    progress(w);
#endif
#if HAVE(SCROLLBAR)
    scrollbar(w);
#endif
#if HAVE(TABS)
    tabs(w);
#endif
#if HAVE(TREEVIEW)
    treeview(w);
#endif
#if HAVE(LISTVIEW)
    listview(w);
#endif
#if HAVE(STATUSBAR)
    statusbar(w);
#endif
}

static LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        build(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
            DestroyWindow(w);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

int main(void)
{
#ifdef _WIN32
    /* Built as a console app so that main() serves both worlds; on Windows
     * this is a GUI program, so drop the console window it came with. */
    FreeConsole();
    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_WIN95_CLASSES | ICC_BAR_CLASSES |
                                               ICC_TREEVIEW_CLASSES |
                                               ICC_LISTVIEW_CLASSES |
                                               ICC_PROGRESS_CLASS |
                                               ICC_TAB_CLASSES };
    InitCommonControlsEx(&ic);
#endif

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = WndProc;
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = "ween32ref";
    RegisterClassA(&wc);

    RECT r = { 0, 0, 660, 420 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    AdjustWindowRect(&r, style, FALSE);
    HWND w = CreateWindowExA(0, "ween32ref", "win32 control sampler", style, 40,
                             40, r.right - r.left, r.bottom - r.top, NULL, NULL,
                             NULL, NULL);
    ShowWindow(w, SW_SHOWNORMAL);
    UpdateWindow(w);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0))
        DispatchMessageA(&msg);
    return 0;
}
