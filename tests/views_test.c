/* What a file browser does to the two views: fill them, empty them, refill
 * them, scroll a list longer than the window, and hear about a column being
 * clicked. None of that was possible when they could only be filled. */

#define _POSIX_C_SOURCE 200112L /* setenv */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ween_internal.h"

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

static HWND g_list, g_tree;
static int g_column_clicked = -1;

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NOTIFY) {
        const NMHDR *nm = (const NMHDR *)lp;
        if (nm->code == LVN_COLUMNCLICK)
            g_column_clicked = ((const NMLISTVIEW *)lp)->iSubItem;
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void add_row(const char *name)
{
    LVITEMA it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = (int)SendMessageA(g_list, LVM_GETITEMCOUNT, 0, 0);
    it.pszText = (char *)name;
    SendMessageA(g_list, LVM_INSERTITEMA, 0, (LPARAM)&it);
}

static HTREEITEM add_node(HTREEITEM parent, const char *text)
{
    TVINSERTSTRUCTA is;
    memset(&is, 0, sizeof(is));
    is.hParent = parent ? parent : TVI_ROOT;
    is.item.mask = TVIF_TEXT;
    is.item.pszText = (char *)text;
    return (HTREEITEM)SendMessageA(g_tree, TVM_INSERTITEMA, 0, (LPARAM)&is);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenviews";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    HWND w = CreateWindowExA(0, "weenviews", "views",
                             WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 400, 240,
                             NULL, NULL, NULL, NULL);
    /* a list ten rows tall, which is not enough for what goes in it */
    g_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                             WS_CHILD | WS_VISIBLE, 10, 10, 240, 160, w,
                             (HMENU)(UINT_PTR)1, NULL, NULL);
    g_tree = CreateWindowExA(WS_EX_CLIENTEDGE, WC_TREEVIEWA, "",
                             WS_CHILD | WS_VISIBLE, 260, 10, 120, 160, w,
                             (HMENU)(UINT_PTR)2, NULL, NULL);
    CHECK(g_list && g_tree, "a list view and a tree view");

    {
        LVCOLUMNA col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (char *)"Name";
        col.cx = 120;
        SendMessageA(g_list, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
        col.pszText = (char *)"Size";
        col.cx = 60;
        SendMessageA(g_list, LVM_INSERTCOLUMNA, 1, (LPARAM)&col);
    }

    /* Fill it with more than fits. */
    for (int i = 0; i < 40; i++) {
        char name[32];
        sprintf(name, "file%02d.txt", i);
        add_row(name);
    }
    CHECK(SendMessageA(g_list, LVM_GETITEMCOUNT, 0, 0) == 40,
          "forty rows went in, and the control can say so");

    /* Scrolling: the wheel, and the keys, and staying inside the list. */
    {
        ween_lv_view st;
        SendMessageA(g_list, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), 0);
        ween_listview_view(g_list, &st);
        CHECK(st.top == 3, "a wheel notch scrolls three rows");

        for (int i = 0; i < 100; i++) /* far past the end */
            SendMessageA(g_list, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA),
                         0);
        ween_listview_view(g_list, &st);
        CHECK(st.top == st.max_top && st.max_top > 0,
              "and it stops at the last screenful rather than running off");

        SendMessageA(g_list, WM_KEYDOWN, VK_HOME, 0);
        ween_listview_view(g_list, &st);
        CHECK(st.top == 0 && st.sel == 1,
              "Home selects the first row and brings it into view");

        SendMessageA(g_list, WM_KEYDOWN, VK_END, 0);
        ween_listview_view(g_list, &st);
        CHECK(st.sel == 40 && st.top == st.max_top,
              "End selects the last and scrolls to it");
    }

    /* Clicking a column header: the app hears which one, so it can sort. */
    SendMessageA(g_list, WM_LBUTTONDOWN, 0, MAKELPARAM(150, 4)); /* "Size" */
    SendMessageA(g_list, WM_LBUTTONUP, 0, MAKELPARAM(150, 4));
    CHECK(g_column_clicked == 1, "clicking a column header says which one");

    /* Emptying and refilling, which is what navigating a folder is. */
    SendMessageA(g_list, LVM_DELETEALLITEMS, 0, 0);
    CHECK(SendMessageA(g_list, LVM_GETITEMCOUNT, 0, 0) == 0,
          "the list can be emptied");
    {
        ween_lv_view st;
        ween_listview_view(g_list, &st);
        CHECK(st.top == 0 && st.sel == 0,
              "and emptying it takes the scroll and the selection with it");
    }
    add_row("after.txt");
    CHECK(SendMessageA(g_list, LVM_GETITEMCOUNT, 0, 0) == 1,
          "and refilled again");

    /* The tree, the same way round. */
    {
        HTREEITEM root = add_node(NULL, "C:");
        HTREEITEM kid = add_node(root, "WINNT");
        add_node(root, "Program Files");
        CHECK((HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_ROOT, 0) ==
                  root,
              "the tree can be asked for its root");
        CHECK((HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_CHILD,
                                      (LPARAM)root) == kid,
              "and for a node's first child");

        char buf[64];
        TVITEMA q;
        memset(&q, 0, sizeof(q));
        q.mask = TVIF_TEXT;
        q.hItem = kid;
        q.pszText = buf;
        q.cchTextMax = (int)sizeof(buf);
        SendMessageA(g_tree, TVM_GETITEMA, 0, (LPARAM)&q);
        CHECK(strcmp(buf, "WINNT") == 0, "and for an item's text");

        SendMessageA(g_tree, TVM_DELETEITEM, 0, (LPARAM)kid);
        CHECK((HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_CHILD,
                                      (LPARAM)root) != kid,
              "one item can be deleted");
        SendMessageA(g_tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
        CHECK(SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_ROOT, 0) == 0,
              "and the whole tree emptied for the next folder");
    }

    DestroyWindow(w);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("views_test: all passed\n");
    return 0;
}
