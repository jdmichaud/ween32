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
/* what the tree said it was about to open, and how many times it said so */
static HTREEITEM g_expanding;
static int g_expandings;

static LRESULT CALLBACK splitter_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(w, msg, wp, lp);
}

static HTREEITEM add_node(HTREEITEM parent, const char *text);

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NOTIFY) {
        const NMHDR *nm = (const NMHDR *)lp;
        if (nm->code == LVN_COLUMNCLICK)
            g_column_clicked = ((const NMLISTVIEW *)lp)->iSubItem;
        if (nm->code == TVN_ITEMEXPANDINGA) {
            const NMTREEVIEWA *tv = (const NMTREEVIEWA *)lp;
            g_expandings++;
            g_expanding = tv->itemNew.hItem;
            /* fill it here, as an app reading a directory would */
            if (tv->action == TVE_EXPAND && g_expanding)
                add_node(g_expanding, "read when opened");
        }
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

    /* The bar down the right, driven by clicking it rather than by the wheel.
     * The wheel goes through the clamp and always lands somewhere sane; the
     * bar goes through win32's arithmetic, where nMax is the last row and a
     * page is taken off it once. Hand that a range with the page already
     * taken off and it comes off twice — on a view tall enough to show more
     * than half its rows the range goes negative and the bar will not move at
     * all, which is what resizing an explorer smaller used to produce. */
    {
        ween_lv_view st;
        RECT cr;
        int sb = ween_scroll_metric(), x, y;

        MoveWindow(g_list, 10, 10, 240, 320, TRUE);
        SendMessageA(g_list, WM_KEYDOWN, VK_HOME, 0);
        GetClientRect(g_list, &cr);
        x = cr.right - sb / 2;
        y = cr.bottom - sb - 1; /* the track, just above the down arrow */
        for (int i = 0; i < 100; i++) {
            SendMessageA(g_list, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
            SendMessageA(g_list, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        }
        ween_listview_view(g_list, &st);
        CHECK(st.max_top > 0 && st.visible * 2 > 40,
              "a view showing more than half its rows still has some to go");
        CHECK(st.top == st.max_top,
              "and paging down its bar reaches the last screenful");
        MoveWindow(g_list, 10, 10, 240, 160, TRUE);
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

    /* A tree filled a level at a time. An item says through cChildren that it
     * can be opened before anything is under it — otherwise a shell would
     * have to walk every directory on the machine to draw one tree, and no
     * folder would have a box to open it with. Clicking that box asks the app
     * to read the level, before the item opens, so what is drawn straight
     * after already has it. */
    {
        TVINSERTSTRUCTA is;
        HTREEITEM lazy;
        memset(&is, 0, sizeof(is));
        is.hParent = TVI_ROOT;
        is.item.mask = TVIF_TEXT | TVIF_CHILDREN;
        is.item.pszText = (char *)"unread";
        is.item.cChildren = 1;
        lazy = (HTREEITEM)SendMessageA(g_tree, TVM_INSERTITEMA, 0, (LPARAM)&is);
        CHECK(lazy && !SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_CHILD,
                                    (LPARAM)lazy),
              "an item can claim children it does not have");

        g_expandings = 0;
        g_expanding = NULL;
        /* the box is WEEN_TV_BUTTON wide, five pixels in, on the first row */
        SendMessageA(g_tree, WM_LBUTTONDOWN, 0, MAKELPARAM(7, 8));
        SendMessageA(g_tree, WM_LBUTTONUP, 0, MAKELPARAM(7, 8));
        CHECK(g_expandings == 1 && g_expanding == lazy,
              "clicking its box asks the app to read that item's level");
        CHECK(SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_CHILD,
                           (LPARAM)lazy) != 0,
              "and what the app inserted while it asked is there");

        /* And it is only asked while something changes: closing and opening
         * again asks twice more, but asking to open what is open asks
         * nothing. */
        SendMessageA(g_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)lazy);
        CHECK(g_expandings == 1, "opening what is already open asks nothing");
        SendMessageA(g_tree, TVM_EXPAND, TVE_COLLAPSE, (LPARAM)lazy);
        CHECK(g_expandings == 2, "and closing it is a change worth telling");

        SendMessageA(g_tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    }

    /* Dragging a header divider resizes the column, and does not sort it. */
    {
        LVCOLUMNA col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (char *)"Name";
        col.cx = 120;
        g_column_clicked = -1;

        /* the divider at the right of column 0 is at x = 120 */
        SendMessageA(g_list, WM_LBUTTONDOWN, 0, MAKELPARAM(120, 4));
        SendMessageA(g_list, WM_MOUSEMOVE, 0, MAKELPARAM(160, 4));
        SendMessageA(g_list, WM_LBUTTONUP, 0, MAKELPARAM(160, 4));
        /* the width is read back by putting something at that column's edge:
         * the next divider has moved with it */
        SendMessageA(g_list, WM_LBUTTONDOWN, 0, MAKELPARAM(160, 4));
        SendMessageA(g_list, WM_MOUSEMOVE, 0, MAKELPARAM(150, 4));
        SendMessageA(g_list, WM_LBUTTONUP, 0, MAKELPARAM(150, 4));
        CHECK(g_column_clicked == -1,
              "dragging a divider resizes rather than sorting");

        /* and setting a width explicitly still works */
        SendMessageA(g_list, LVM_SETCOLUMNWIDTH, 0, MAKELPARAM(90, 0));
        SendMessageA(g_list, WM_LBUTTONDOWN, 0, MAKELPARAM(90, 4));
        SendMessageA(g_list, WM_MOUSEMOVE, 0, MAKELPARAM(140, 4));
        SendMessageA(g_list, WM_LBUTTONUP, 0, MAKELPARAM(140, 4));
        CHECK(g_column_clicked == -1,
              "a column set to a width can then be dragged from it");

        /* a press away from any divider is a sort, as before */
        SendMessageA(g_list, WM_LBUTTONDOWN, 0, MAKELPARAM(60, 4));
        SendMessageA(g_list, WM_LBUTTONUP, 0, MAKELPARAM(60, 4));
        CHECK(g_column_clicked == 0,
              "and a press away from one still sorts the column");
    }

    /* Cursors: a class carries one, and a control can override it over part
     * of itself — which is what a splitter needs. */
    {
        WNDCLASSA sp;
        memset(&sp, 0, sizeof(sp));
        sp.lpfnWndProc = splitter_proc;
        sp.lpszClassName = "weensplit";
        sp.hCursor = LoadCursorA(NULL, IDC_SIZEWE);
        sp.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        RegisterClassA(&sp);
        HWND bar = CreateWindowA("weensplit", "", WS_CHILD | WS_VISIBLE, 250, 10,
                                 6, 160, w, NULL, NULL, NULL);
        CHECK(bar != NULL, "a splitter with a resize cursor on its class");

        ween_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.kind = WEEN_EV_MOUSE_MOVE;
        ev.win = w->backend_win;
        ev.x = WEEN_NC_FRAME + 252;
        ev.y = WEEN_NC_FRAME + WEEN_NC_CAPTION + 80;
        ween_headless_inject(ev);
        ev.x = WEEN_NC_FRAME + 100; /* back over the list view */
        ween_headless_inject(ev);
        ween_event end;
        memset(&end, 0, sizeof(end));
        end.kind = WEEN_EV_END;
        ween_headless_inject(end);

        MSG msg;
        int over_bar = -1, over_list = -1;
        while (GetMessageA(&msg, NULL, 0, 0)) {
            DispatchMessageA(&msg);
            if (msg.message == WM_MOUSEMOVE) {
                if (over_bar < 0)
                    over_bar = ween_headless_cursor(w->backend_win);
                else if (over_list < 0)
                    over_list = ween_headless_cursor(w->backend_win);
            }
        }
        CHECK(over_bar == WEEN_CURSOR_SIZEWE,
              "the pointer over the splitter is a resize arrow");
        CHECK(over_list == WEEN_CURSOR_ARROW,
              "and an ordinary one again once it moves off");
        DestroyWindow(bar);
    }

    DestroyWindow(w);

    /* A name too long for its column is cut short with an ellipsis, not run
     * on over the column beside it. A list view of files is full of names
     * longer than the Name column, and every one of them used to write
     * straight through the size and the type. Its own window, so nothing
     * else is drawing in the pixels being counted. */
    {
        HWND lw = CreateWindowExA(0, "weenviews", "narrow",
                                  WS_POPUP | WS_VISIBLE, 0, 0, 300, 120, NULL,
                                  NULL, NULL, NULL);
        HWND narrow = CreateWindowExA(0, WC_LISTVIEWA, "",
                                      WS_CHILD | WS_VISIBLE, 0, 0, 280, 100, lw,
                                      (HMENU)(UINT_PTR)7, NULL, NULL);
        LVCOLUMNA col;
        LVITEMA it;
        const ween_surface *s;
        int beyond = 0, within = 0;

        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (char *)"Name";
        col.cx = 60;
        SendMessageA(narrow, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.pszText = (char *)"a name far longer than sixty pixels of Tahoma";
        SendMessageA(narrow, LVM_INSERTITEMA, 0, (LPARAM)&it);

        InvalidateRect(lw, NULL, TRUE);
        ween_flush_paint();
        s = &lw->surface; /* this window's own, not whichever presented last */
        /* The row sits under the header — seventeen pixels of it — and is
         * fourteen tall. The column ends sixty pixels in. */
        for (int y = 19; s && y < 31; y++)
            for (int x = 0; x < s->w; x++)
                if ((s->px[(size_t)y * s->w + x] & 0xffffff) == WEEN_BLACK) {
                    if (x >= 60)
                        beyond++;
                    else
                        within++;
                }
        CHECK(within > 0, "a long name is drawn");
        CHECK(beyond == 0, "and stops at its column rather than running on");
        DestroyWindow(lw);
    }

    /* A combo box's drop-down. It can be emptied — an address bar refills
     * itself on every folder, and without CB_RESETCONTENT it only ever grew,
     * going on showing the first path it was ever given. And once open it
     * follows the pointer: click, let go, move over the list, and the item
     * under the pointer is the one lit up. */
    {
        HWND cw = CreateWindowExA(0, "weenviews", "combo",
                                  WS_POPUP | WS_VISIBLE, 0, 0, 220, 160, NULL,
                                  NULL, NULL, NULL);
        HWND cb = CreateWindowExA(0, "COMBOBOX", "",
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 10,
                                  10, 150, 100, cw, (HMENU)(UINT_PTR)8, NULL,
                                  NULL);
        const ween_surface *s;
        int ox, oy, first_top = -1, first_bottom = -1, then_top = -1;

        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"one");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"two");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"three");
        CHECK(SendMessageA(cb, CB_GETCOUNT, 0, 0) == 3, "a combo with three");
        SendMessageA(cb, CB_RESETCONTENT, 0, 0);
        CHECK(SendMessageA(cb, CB_GETCOUNT, 0, 0) == 0,
              "CB_RESETCONTENT empties it, so it can be refilled");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"one");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"two");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"three");
        SendMessageA(cb, CB_SETCURSEL, 0, 0);

        /* open it, and let go — which is where the tracking used to stop */
        SendMessageA(cb, WM_LBUTTONDOWN, 0, MAKELPARAM(140, 8));
        SendMessageA(cb, WM_LBUTTONUP, 0, MAKELPARAM(140, 8));
        InvalidateRect(cw, NULL, TRUE);
        ween_flush_paint();
        s = &cw->surface;
        for (int y = 0; y < s->h; y++)
            if ((s->px[(size_t)y * s->w + 20] & 0xffffff) == WEEN_CAP_LEFT) {
                if (first_top < 0)
                    first_top = y;
                first_bottom = y;
            }
        CHECK(first_top > 0, "opening it highlights the item it is showing");

        /* one item further down, in the coordinates a routed move arrives in */
        ween_client_origin(cb, &ox, &oy);
        SendMessageA(cb, WM_MOUSEMOVE, 0,
                     MAKELPARAM(20, first_bottom + 2 - oy));
        InvalidateRect(cw, NULL, TRUE);
        ween_flush_paint();
        s = &cw->surface;
        for (int y = 0; y < s->h && then_top < 0; y++)
            if ((s->px[(size_t)y * s->w + 20] & 0xffffff) == WEEN_CAP_LEFT)
                then_top = y;
        CHECK(then_top > first_top,
              "and moving over the next one moves the highlight to it");
        DestroyWindow(cw);
    }

    /* The same control told about images and indents — a ComboBoxEx, which
     * is what a shell's address bar is: a path shown as the tree it walks
     * down, each level a step further in and wearing its own icon. */
    {
        HWND cw = CreateWindowExA(0, "weenviews", "cbex", WS_POPUP | WS_VISIBLE,
                                  0, 0, 220, 160, NULL, NULL, NULL, NULL);
        HWND cb = CreateWindowExA(0, WC_COMBOBOXEXA, "",
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 10,
                                  10, 150, 100, cw, (HMENU)(UINT_PTR)9, NULL,
                                  NULL);
        COMBOBOXEXITEMA ci;
        HIMAGELIST il2 = ImageList_Create(16, 16, ILC_MASK, 2, 2);
        int was_h = cb->h;

        SendMessageA(cb, CBEM_SETIMAGELIST, 0, (LPARAM)il2);
        CHECK(cb->h > was_h,
              "an image list makes the rows tall enough for the images");

        memset(&ci, 0, sizeof(ci));
        ci.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_INDENT;
        ci.iItem = -1;
        ci.pszText = (char *)"root";
        ci.iImage = 0;
        ci.iIndent = 0;
        CHECK(SendMessageA(cb, CBEM_INSERTITEMA, 0, (LPARAM)&ci) == 0,
              "an item goes in with an image and an indent");
        ci.pszText = (char *)"under it";
        ci.iIndent = 1;
        CHECK(SendMessageA(cb, CBEM_INSERTITEMA, 0, (LPARAM)&ci) == 1,
              "and another a step further in");
        CHECK(SendMessageA(cb, CB_GETCOUNT, 0, 0) == 2,
              "it counts them like any combo box");

        /* Emptying it is what an address bar does on every folder, and the
         * image list has to survive that — it belongs to the control, not to
         * the items that were in it. */
        SendMessageA(cb, CB_RESETCONTENT, 0, 0);
        CHECK(SendMessageA(cb, CB_GETCOUNT, 0, 0) == 0, "and can be emptied");
        CHECK(SendMessageA(cb, CBEM_INSERTITEMA, 0, (LPARAM)&ci) == 0 &&
                  cb->h > was_h,
              "with the image list still on it afterwards");
        DestroyWindow(cw);
        ImageList_Destroy(il2);
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("views_test: all passed\n");
    return 0;
}
