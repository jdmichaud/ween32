/* What a file browser does to the two views: fill them, empty them, refill
 * them, scroll a list longer than the window, and hear about a column being
 * clicked. None of that was possible when they could only be filled. */

#define _POSIX_C_SOURCE 200112L /* setenv */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ween_internal.h"

static int g_failures = 0;

/* for the subclassing check further down: what was in front, and whether it
 * saw the message */
static WNDPROC g_sub_next;
static int g_sub_seen;

static LRESULT CALLBACK sub_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        g_sub_seen++;
        return 0; /* taken: the original never sees it */
    }
    return CallWindowProcA(g_sub_next, w, msg, wp, lp);
}

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
static int g_returns; /* Enter over the list, which is what opens a row */
static int g_column_clicked = -1;
static int g_column_dragged = -1, g_column_dropped = -1;
/* what the tree said it was about to open, and how many times it said so */
static HTREEITEM g_expanding;
static int g_expandings;

static LRESULT CALLBACK splitter_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(w, msg, wp, lp);
}

static HTREEITEM add_node_sorted(HTREEITEM parent, const char *text)
{
    TVINSERTSTRUCTA is;
    memset(&is, 0, sizeof(is));
    is.hParent = parent ? parent : TVI_ROOT;
    is.hInsertAfter = TVI_SORT;
    is.item.mask = TVIF_TEXT;
    is.item.pszText = (char *)text;
    return (HTREEITEM)SendMessageA(g_tree, TVM_INSERTITEMA, 0, (LPARAM)&is);
}

static HTREEITEM add_node(HTREEITEM parent, const char *text);

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NOTIFY) {
        const NMHDR *nm = (const NMHDR *)lp;
        if (nm->code == LVN_COLUMNCLICK)
            g_column_clicked = ((const NMLISTVIEW *)lp)->iSubItem;
        if (nm->code == NM_RETURN && nm->hwndFrom == g_list)
            g_returns++;
        if (nm->code == HDN_ENDDRAG) {
            g_column_dragged = ((const NMHEADERA *)lp)->iItem;
            g_column_dropped = ((const NMHEADERA *)lp)->iButton;
        }
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
    /* LVS_REPORT: rows with columns. A list view with none of the view bits
     * set is LVS_ICON, which is what win32 makes of a bare style too. */
    g_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                             WS_CHILD | WS_VISIBLE | LVS_REPORT, 10, 10, 240,
                             160, w, (HMENU)(UINT_PTR)1, NULL, NULL);
    /* LINESATROOT carries the lines and the boxes out to the top level;
     * without it the roots have neither, on either side of the build. */
    g_tree = CreateWindowExA(WS_EX_CLIENTEDGE, WC_TREEVIEWA, "",
                             WS_CHILD | WS_VISIBLE | TVS_HASLINES |
                                 TVS_HASBUTTONS | TVS_LINESATROOT,
                             260, 10, 120, 160, w, (HMENU)(UINT_PTR)2, NULL,
                             NULL);
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

        /* Shift and an arrow takes the run from the end it started at, and
         * keeps that end for the next press: measuring from the caret each
         * time leaves two rows picked however many times it is pressed —
         * which is what it used to do. The library's own shift bit is the low
         * one of lParam. */
        SendMessageA(g_list, WM_KEYDOWN, VK_HOME, 0);
        SendMessageA(g_list, WM_KEYDOWN, VK_DOWN, 1);
        SendMessageA(g_list, WM_KEYDOWN, VK_DOWN, 1);
        SendMessageA(g_list, WM_KEYDOWN, VK_DOWN, 1);
        CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 4,
              "Shift and three downs pick the four rows they crossed");
        SendMessageA(g_list, WM_KEYDOWN, VK_UP, 1);
        CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 3,
              "and coming back up gives one of them up again");
        SendMessageA(g_list, WM_KEYDOWN, VK_DOWN, 0);
        CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 1,
              "an arrow with no Shift picks one and moves the end with it");
        SendMessageA(g_list, WM_KEYDOWN, VK_DOWN, 1);
        CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 2,
              "so the next run is measured from where that left the caret");

        /* Enter says what a double click says — a shell opens what is picked
         * — and moves nothing while it says it. */
        ween_listview_view(g_list, &st);
        {
            int was = st.sel;
            SendMessageA(g_list, WM_KEYDOWN, VK_RETURN, 0);
            ween_listview_view(g_list, &st);
            CHECK(g_returns == 1,
                  "Enter over the list asks the app to open a row");
            CHECK(st.sel == was, "and leaves the selection where it was");
        }
        SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&(LVITEMA){
            .mask = LVIF_STATE, .state = 0, .stateMask = LVIS_SELECTED });
        SendMessageA(g_list, WM_KEYDOWN, VK_RETURN, 0);
        CHECK(g_returns == 1, "and says nothing when nothing is picked");
    }

    /* A name too long for its column says itself in full when the pointer
     * rests on it: a tip in the same clothes as a button's, put over the name
     * rather than under the pointer — the machine's list does this, and its
     * tree does it too. */
    {
        LVITEMA it;
        RECT row, tr;
        POINT origin;
        HWND tip;
        const ween_strike *f = ween_gui_font();
        const char *longname = "a name far too long for the column it is in";
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = 0;
        it.pszText = (char *)longname;
        SendMessageA(g_list, LVM_SETITEMTEXTA, 0, (LPARAM)&it);
        SendMessageA(g_list, WM_KEYDOWN, VK_HOME, 0);

        row.left = LVIR_BOUNDS;
        SendMessageA(g_list, LVM_GETITEMRECT, 0, (LPARAM)&row);
        SendMessageA(g_list, WM_MOUSEMOVE, 0,
                     MAKELPARAM(20, (row.top + row.bottom) / 2));
        SendMessageA(g_list, WM_TIMER, 0x7e01, 0);
        origin.x = 0;
        origin.y = 0;
        ClientToScreen(g_list, &origin);
        tip = ween_listview_tip(g_list);
        CHECK(tip != NULL && IsWindowVisible(tip),
              "a name cut short by its column shows itself in full");
        if (tip) {
            GetWindowRect(tip, &tr);
            CHECK(tr.bottom - tr.top == 17, "the tip is seventeen tall");
            CHECK(tr.right - tr.left ==
                      ween_strike_text_width(f, longname,
                                             (int)strlen(longname)) + 6,
                  "and as wide as the whole name and six");
            CHECK(tr.top == origin.y + row.top,
                  "it sits on the row it is about");
        }
        /* a name that fits needs none */
        it.pszText = (char *)"short";
        SendMessageA(g_list, LVM_SETITEMTEXTA, 0, (LPARAM)&it);
        SendMessageA(g_list, WM_MOUSEMOVE, 0, MAKELPARAM(200, row.top + 2));
        SendMessageA(g_list, WM_MOUSEMOVE, 0,
                     MAKELPARAM(20, (row.top + row.bottom) / 2));
        SendMessageA(g_list, WM_TIMER, 0x7e01, 0);
        CHECK(!IsWindowVisible(ween_listview_tip(g_list)),
              "and one that fits shows none");
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

        /* Which item a point is on. A right click needs this before it can
         * put a menu on the folder under the pointer. */
        {
            TVHITTESTINFO ht;
            memset(&ht, 0, sizeof(ht));
            ht.pt.x = 30;
            ht.pt.y = 5; /* the first row */
            CHECK((HTREEITEM)SendMessageA(g_tree, TVM_HITTEST, 0,
                                          (LPARAM)&ht) == root,
                  "the tree says which item a point is on");
            ht.pt.y = 900; /* past the last */
            CHECK(SendMessageA(g_tree, TVM_HITTEST, 0, (LPARAM)&ht) == 0,
                  "and nothing below them");
        }

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

    /* Scrolling a tree taller than its window, and the one number in it that
     * had to be measured: a click in the track moves a screenful *less one
     * row*, so the row that was last whole is the row at the top afterwards.
     *
     * That is the machine's, read off its explorer -- tree client 208..490
     * with sixteen-pixel rows from 208, so seventeen whole and a clipped
     * eighteenth, and a track click put row sixteen at the top. The strip of
     * the row that landed there is pixel-identical to that row before the
     * click, so there is no judgement in it. The list view next door does
     * *not* do this: it moves a whole screenful. Two controls, two rules.
     */
    {
        int rows, visible, sb = ween_scroll_metric();
        HTREEITEM first = NULL;
        RECT cr;
        for (int i = 0; i < 40; i++) {
            char name[32];
            sprintf(name, "row%02d", i);
            HTREEITEM h = add_node(NULL, name);
            if (!i)
                first = h;
        }
        rows = 40;
        /* Painted before it is clicked, as a window on a screen would be:
         * the tree works out its rows and its bars while it draws. */
        InvalidateRect(g_tree, NULL, TRUE);
        ween_flush_paint();
        GetClientRect(g_tree, &cr);
        visible = (int)SendMessageA(g_tree, TVM_GETVISIBLECOUNT, 0, 0);
        CHECK(visible > 2 && visible < rows,
              "a tree with more rows than its window shows");
        CHECK((HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_FIRSTVISIBLE,
                                      0) == first,
              "and it starts at the first of them");
        /* a click in the track, below the thumb and above the down arrow */
        {
            int x = cr.right - sb / 2, y = cr.bottom - sb - 2;
            HTREEITEM top_after;
            SendMessageA(g_tree, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
            SendMessageA(g_tree, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            top_after = (HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM,
                                                TVGN_FIRSTVISIBLE, 0);
            {
                char buf[32];
                TVITEMA q;
                memset(&q, 0, sizeof q);
                q.mask = TVIF_TEXT;
                q.hItem = top_after;
                q.pszText = buf;
                q.cchTextMax = (int)sizeof buf;
                buf[0] = 0;
                SendMessageA(g_tree, TVM_GETITEMA, 0, (LPARAM)&q);
                {
                    char want[32];
                    sprintf(want, "row%02d", visible - 1);
                    CHECK(strcmp(buf, want) == 0,
                          "a click in the track moves a screenful less one "
                          "row, so the last whole row is the top one");
                }
            }
        }
        SendMessageA(g_tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
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

        /* A name the pane cuts short says itself in full here too. */
        {
            HTREEITEM longone;
            RECT tr;
            TVHITTESTINFO hi;
            const char *name = "a folder whose name is far wider than this pane";
            SendMessageA(g_tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
            longone = add_node(NULL, name);
            add_node(NULL, "short");
            memset(&hi, 0, sizeof(hi));
            hi.pt.x = 30;
            hi.pt.y = 8;
            CHECK((HTREEITEM)SendMessageA(g_tree, TVM_HITTEST, 0, (LPARAM)&hi) ==
                      longone,
                  "the long name is the item at the top of the tree");
            SendMessageA(g_tree, WM_MOUSEMOVE, 0, MAKELPARAM(30, 8));
            SendMessageA(g_tree, WM_TIMER, 0x7e01, 0);
            CHECK(ween_treeview_tip(g_tree) &&
                      IsWindowVisible(ween_treeview_tip(g_tree)),
                  "a name the pane cuts short shows itself in full");
            GetWindowRect(ween_treeview_tip(g_tree), &tr);
            CHECK(tr.bottom - tr.top == 17, "in a tip seventeen tall");
            CHECK(tr.right - tr.left ==
                      ween_strike_text_width(ween_gui_font(), name,
                                             (int)strlen(name)) + 6,
                  "as wide as the whole name and six");
            /* and the one that fits gets none */
            SendMessageA(g_tree, WM_MOUSEMOVE, 0, MAKELPARAM(30, 8 + 16));
            SendMessageA(g_tree, WM_TIMER, 0x7e01, 0);
            CHECK(!IsWindowVisible(ween_treeview_tip(g_tree)),
                  "and a name that fits shows none");
            SendMessageA(g_tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
        }

        /* Where an item goes among its brothers and sisters. TVI_SORT is
         * what a folder tree is built with: a directory hands its names back
         * in no order at all, and the tree comes out in one anyway. */
        {
            static const char *out_of_order[] = { "zebra", "Mango", "alpha",
                                                  "beta" };
            static const char *in_order[] = { "alpha", "beta", "Mango",
                                              "zebra" };
            HTREEITEM it;
            int i = 0;
            SendMessageA(g_tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
            for (i = 0; i < 4; i++)
                add_node_sorted(NULL, out_of_order[i]);
            it = (HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_ROOT, 0);
            for (i = 0; i < 4 && it; i++) {
                char label[64];
                TVITEMA q;
                memset(&q, 0, sizeof(q));
                q.mask = TVIF_TEXT;
                q.hItem = it;
                q.pszText = label;
                q.cchTextMax = (int)sizeof(label);
                SendMessageA(g_tree, TVM_GETITEMA, 0, (LPARAM)&q);
                if (strcmp(label, in_order[i]))
                    break;
                it = (HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_NEXT,
                                             (LPARAM)it);
            }
            CHECK(i == 4, "TVI_SORT puts each name where it belongs, whatever "
                          "its case");
        }

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

        /* A divider double-clicked fits the column to what is in it, which
         * is what LVSCW_AUTOSIZE asks for by hand. Every row here is
         * "fileNN.txt", so the fit is well under the width they are given. */
        SendMessageA(g_list, LVM_SETCOLUMNWIDTH, 0, MAKELPARAM(200, 0));
        SendMessageA(g_list, LVM_SETCOLUMNWIDTH, 0,
                     MAKELPARAM(LVSCW_AUTOSIZE, 0));
        int fit = (int)SendMessageA(g_list, LVM_GETCOLUMNWIDTH, 0, 0);
        CHECK(fit > 20 && fit < 120, "a column can be sized to fit its items");
        SendMessageA(g_list, LVM_SETCOLUMNWIDTH, 0, MAKELPARAM(200, 0));
        SendMessageA(g_list, WM_LBUTTONDBLCLK, 0, MAKELPARAM(200, 4));
        CHECK((int)SendMessageA(g_list, LVM_GETCOLUMNWIDTH, 0, 0) == fit,
              "and double-clicking its divider asks for the same fit");
        /* back to a width whose dividers are nowhere near what follows */
        SendMessageA(g_list, LVM_SETCOLUMNWIDTH, 0, MAKELPARAM(120, 0));

        /* The columns live in a header, and that is where the arrow saying
         * which one the view is sorted by is asked for. */
        {
            HWND head = (HWND)(INT_PTR)SendMessageA(g_list, LVM_GETHEADER, 0,
                                                    0);
            HDITEMA hd;
            char text[32];
            CHECK(head != NULL, "a list view has a header");
            CHECK((HWND)(INT_PTR)SendMessageA(g_list, LVM_GETHEADER, 0, 0) ==
                      head,
                  "and hands back the same one every time");
            CHECK(SendMessageA(head, HDM_GETITEMCOUNT, 0, 0) == 2,
                  "which has an item per column");
            memset(&hd, 0, sizeof(hd));
            hd.mask = HDI_FORMAT;
            hd.fmt = HDF_LEFT | HDF_SORTUP;
            CHECK(SendMessageA(head, HDM_SETITEMA, 0, (LPARAM)&hd),
                  "a heading takes the arrow that says it is sorted by");
            memset(&hd, 0, sizeof(hd));
            hd.mask = HDI_FORMAT | HDI_TEXT | HDI_WIDTH;
            hd.pszText = text;
            hd.cchTextMax = (int)sizeof(text);
            SendMessageA(head, HDM_GETITEMA, 0, (LPARAM)&hd);
            CHECK((hd.fmt & HDF_SORTUP) && !strcmp(text, "Name") &&
                      hd.cxy == 120,
                  "and reads back with its text and width");
            /* setting the column's own format leaves the arrow alone: one is
             * how the cells are laid out, the other is the heading's */
            {
                LVCOLUMNA col;
                memset(&col, 0, sizeof(col));
                col.mask = LVCF_FMT;
                col.fmt = LVCFMT_LEFT;
                SendMessageA(g_list, LVM_SETCOLUMNA, 0, (LPARAM)&col);
                memset(&hd, 0, sizeof(hd));
                hd.mask = HDI_FORMAT;
                SendMessageA(head, HDM_GETITEMA, 0, (LPARAM)&hd);
                CHECK((hd.fmt & HDF_SORTUP) != 0,
                      "and a column's own format does not disturb it");
            }
        }

        /* More than one row at a time: Select All picks every one of them,
         * asking for -1; Ctrl adds a row to what is there, Shift takes the
         * run from the anchor, and a plain click drops the rest. */
        {
            LVITEMA st;
            int n = 0, i = -1, rows;
            for (int k = 0; k < 8; k++) { /* the list was emptied earlier */
                char name[32];
                sprintf(name, "pick%d.txt", k);
                add_row(name);
            }
            rows = (int)SendMessageA(g_list, LVM_GETITEMCOUNT, 0, 0);
            memset(&st, 0, sizeof(st));
            st.mask = LVIF_STATE;
            st.state = LVIS_SELECTED;
            st.stateMask = LVIS_SELECTED;
            SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
            CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == rows,
                  "asking for -1 picks every row");
            while ((i = (int)SendMessageA(g_list, LVM_GETNEXTITEM, (WPARAM)i,
                                          LVNI_SELECTED)) >= 0)
                n++;
            CHECK(n == rows, "and they can be walked one after another");
            st.state = 0;
            SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
            CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 0,
                  "and dropped the same way");

            SendMessageA(g_list, WM_LBUTTONDOWN, 0, MAKELPARAM(30, 30));
            SendMessageA(g_list, WM_LBUTTONDOWN, MK_CONTROL,
                         MAKELPARAM(30, 30 + 3 * 17));
            CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 2,
                  "Ctrl and a click adds one to what is picked");
            SendMessageA(g_list, WM_LBUTTONDOWN, MK_SHIFT,
                         MAKELPARAM(30, 30 + 5 * 17));
            CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) > 2,
                  "Shift takes the run from the one before it");
            SendMessageA(g_list, WM_LBUTTONDOWN, 0, MAKELPARAM(30, 30));
            CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 1,
                  "and a plain click drops the rest");

            /* A rectangle dragged from a point on no row picks what it
             * touches, and what it touches is a row's picture and name — the
             * empty width of a column to the right of a name is no more part
             * of the row here than it is to a click, which is what the
             * machine's own does. The rows are asked where they are rather
             * than assumed: a row is as tall as the pictures beside it. */
            {
                RECT r1, r2;
                r1.left = LVIR_BOUNDS;
                SendMessageA(g_list, LVM_GETITEMRECT, 1, (LPARAM)&r1);
                r2.left = LVIR_BOUNDS;
                SendMessageA(g_list, LVM_GETITEMRECT, 2, (LPARAM)&r2);

                SendMessageA(g_list, WM_LBUTTONDOWN, 0,
                             MAKELPARAM(200, r1.top + 1));
                CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 0,
                      "a press past every name drops the selection");
                SendMessageA(g_list, WM_MOUSEMOVE, 0,
                             MAKELPARAM(190, r2.bottom - 1));
                CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 0,
                      "and a drag that stays past them picks nothing");
                SendMessageA(g_list, WM_MOUSEMOVE, 0,
                             MAKELPARAM(20, r2.bottom - 1));
                CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 2,
                      "dragged back across the names it picks the rows it "
                      "crosses");
                SendMessageA(g_list, WM_MOUSEMOVE, 0,
                             MAKELPARAM(20, r1.top + 1));
                CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 1,
                      "and lets go of the ones it is taken back off");
                SendMessageA(g_list, WM_LBUTTONUP, 0,
                             MAKELPARAM(20, r1.top + 1));
                CHECK(SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0) == 1,
                      "what it left picked stays picked when the button "
                      "comes up");
                CHECK(GetCapture() == NULL, "and the pointer is handed back");
            }
        }

        /* a press away from any divider is a sort, as before */
        SendMessageA(g_list, WM_LBUTTONDOWN, 0, MAKELPARAM(60, 4));
        SendMessageA(g_list, WM_LBUTTONUP, 0, MAKELPARAM(60, 4));
        CHECK(g_column_clicked == 0,
              "and a press away from one still sorts the column");
    }

    /* A combo box whose style says CBS_DROPDOWN can be typed in: it keeps a
     * field of its own, the field says when the typing is over and why, and
     * emptying the list to refill it does not take the field away. */
    {
        HWND cb = CreateWindowExA(0, "COMBOBOX", "",
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWN, 10,
                                  200, 160, 21, w, NULL, NULL, NULL);
        HWND list_only = CreateWindowExA(0, "COMBOBOX", "",
                                         WS_CHILD | WS_VISIBLE |
                                             CBS_DROPDOWNLIST,
                                         180, 200, 160, 21, w, NULL, NULL,
                                         NULL);
        HWND field = (HWND)(INT_PTR)SendMessageA(cb, CBEM_GETEDITCONTROL, 0, 0);
        char got[64];
        CHECK(field != NULL, "an editable combo box keeps a field");
        /* **And the field is a child with id 1001**, which is what win32
         * gives it and what a program asks for by name. WordPad's font combo
         * can be typed into, and §4 and §8.5 of its specification both read
         * an `Edit` id 1001 inside a `CBS_DROPDOWN` -- that is how the two
         * were told apart from `CBS_DROPDOWNLIST`, which has no such child.
         * Ours had one and gave it no id, so `GetDlgItem` answered nothing. */
        CHECK(GetDlgItem(cb, 1001) == field,
              "and it is a child with id 1001, which is how a program finds "
              "it");
        CHECK(GetDlgItem(list_only, 1001) == NULL,
              "while a list-only combo has no field to find");
        {
            /* Three in and three down from the box's own corner, and six
             * shorter than it. §8.5's probe reads the machine's: a combo at
             * 309,254 152x111 with its Edit at 312,257 146x15. Ours used to
             * put the field where the box's own painting had been putting
             * the text, and that painting was two rows low -- so the field
             * inherited the error from the thing it replaced, and every
             * character in WordPad's font combo sat two rows under the
             * machine's. */
            RECT bx, fx;
            GetWindowRect(cb, &bx);
            GetWindowRect(field, &fx);
            CHECK(fx.left - bx.left == 3 && fx.top - bx.top == 3,
                  "the field sits three in and three down from the box");
            CHECK((fx.bottom - fx.top) == (bx.bottom - bx.top) - 6,
                  "and is six shorter than it");
        }
        CHECK(SendMessageA(list_only, CBEM_GETEDITCONTROL, 0, 0) == 0,
              "and a list-only one does not");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "one");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "two");
        SendMessageA(cb, CB_SETCURSEL, 1, 0);
        GetWindowTextA(field, got, (int)sizeof(got));
        CHECK(!strcmp(got, "two"), "the field shows what is picked");
        SendMessageA(cb, CB_RESETCONTENT, 0, 0);
        CHECK((HWND)(INT_PTR)SendMessageA(cb, CBEM_GETEDITCONTROL, 0, 0) ==
                  field,
              "and emptying the list leaves the field where it was");

        /* The list is walked from the keyboard: Down opens it and moves
         * through it, the field follows the highlight, Escape puts it away.
         * The keys arrive at the field, which passes on the ones a box one
         * line tall has no use for. */
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "alpha");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "beta");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "gamma");
        SendMessageA(cb, CB_SETCURSEL, 0, 0);
        CHECK(!SendMessageA(cb, CB_GETDROPPEDSTATE, 0, 0),
              "a combo box starts with its list down");
        SendMessageA(field, WM_KEYDOWN, VK_DOWN, 0);
        CHECK(SendMessageA(cb, CB_GETDROPPEDSTATE, 0, 0),
              "and Down in the field opens it");
        SendMessageA(field, WM_KEYDOWN, VK_DOWN, 0);
        GetWindowTextA(field, got, (int)sizeof(got));
        CHECK(!strcmp(got, "beta"), "the field follows the highlight");
        SendMessageA(field, WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(!SendMessageA(cb, CB_GETDROPPEDSTATE, 0, 0),
              "and Escape puts the list away");

        /* More than it can show: the list stops at eight rows and puts a bar
         * down its side, and the highlight walking past the bottom scrolls
         * it rather than running off. */
        {
            RECT before, after;
            for (int k = 0; k < 20; k++) {
                char name[32];
                sprintf(name, "item%02d", k);
                SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)name);
            }
            SendMessageA(cb, CB_SETCURSEL, 0, 0);
            SendMessageA(cb, CB_SHOWDROPDOWN, TRUE, 0);
            ween_combo_list_rect(cb, &before);
            CHECK(before.bottom - before.top < 20 * 16,
                  "a long list stops rather than running the height of it");
            /* three were already in it, so the twelfth step down lands on
             * the ninth of the twenty added */
            for (int k = 0; k < 12; k++)
                SendMessageA(field, WM_KEYDOWN, VK_DOWN, 0);
            GetWindowTextA(field, got, (int)sizeof(got));
            CHECK(!strcmp(got, "item08"),
                  "and the highlight goes on past what is shown");
            /* Dragging the corner makes it taller. The points are the
             * combo's own, as a routed press would arrive. */
            {
                int ox, oy;
                ween_client_origin(cb, &ox, &oy);
                ween_combo_list_rect(cb, &before);
                SendMessageA(cb, WM_LBUTTONDOWN, 0,
                             MAKELPARAM(before.right - 6 - ox,
                                        before.bottom - 6 - oy));
                SendMessageA(cb, WM_MOUSEMOVE, 0,
                             MAKELPARAM(before.right - 6 - ox,
                                        before.bottom + 40 - oy));
                SendMessageA(cb, WM_LBUTTONUP, 0,
                             MAKELPARAM(before.right - 6 - ox,
                                        before.bottom + 40 - oy));
                ween_combo_list_rect(cb, &after);
                CHECK(after.bottom - after.top > before.bottom - before.top,
                      "and the corner drags it taller");
            }
        }

        /* The height a combo box is created with is the height it has with
         * its list *down*: that is what the number in CreateWindowEx means
         * for this class, and it is what decides how many rows it drops. The
         * machine's address bar drops ten rows in 162 pixels — 10 * 16 + 2,
         * and nothing else in it — so a list with everything in it is exactly
         * its rows tall, and only one with more than it can show gives up the
         * strip along the bottom the corner stands in. */
        {
            HWND tall;
            RECT r;
            int ih, k;
            tall = CreateWindowExA(0, "COMBOBOX", "",
                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                   350, 200, 160, 21, w, NULL, NULL, NULL);
            ih = (int)SendMessageA(tall, CB_GETITEMHEIGHT, 0, 0);
            DestroyWindow(tall);
            tall = CreateWindowExA(0, "COMBOBOX", "",
                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                   350, 200, 160, 21 + 10 * ih + 2, w, NULL,
                                   NULL, NULL);
            for (k = 0; k < 10; k++) {
                char name[32];
                sprintf(name, "row%02d", k);
                SendMessageA(tall, CB_ADDSTRING, 0, (LPARAM)name);
            }
            SendMessageA(tall, CB_SETCURSEL, 0, 0);
            SendMessageA(tall, CB_SHOWDROPDOWN, TRUE, 0);
            ween_combo_list_rect(tall, &r);
            CHECK(r.bottom - r.top == 10 * ih + 2,
                  "a list that fits is its rows tall and no more");
            for (k = 10; k < 30; k++) {
                char name[32];
                sprintf(name, "row%02d", k);
                SendMessageA(tall, CB_ADDSTRING, 0, (LPARAM)name);
            }
            ween_combo_list_rect(tall, &r);
            CHECK(r.bottom - r.top == 10 * ih + 2,
                  "one with more to show stops at the same ten rows, and is "
                  "no taller for the bar and the corner it grows");
            DestroyWindow(tall);
        }
        DestroyWindow(cb);
        DestroyWindow(list_only);
    }

    /* Subclassing: a window's procedure can be taken out and another put in
     * front of it, and what came back called for everything the new one does
     * not want. A control that puts another inside itself takes its keys this
     * way, which is how a combo box's field comes to give up Enter. */
    {
        HWND box = CreateWindowExA(0, "EDIT", "typed", WS_CHILD | WS_VISIBLE,
                                   10, 240, 120, 20, w, NULL, NULL, NULL);
        char got[32];
        g_sub_seen = 0;
        g_sub_next = (WNDPROC)SetWindowLongPtrA(box, GWLP_WNDPROC,
                                                (LONG_PTR)sub_proc);
        CHECK(g_sub_next != NULL, "a window gives up its procedure");
        CHECK((WNDPROC)(INT_PTR)GetWindowLongPtrA(box, GWLP_WNDPROC) ==
                  sub_proc,
              "and answers with the one put in front of it");
        SendMessageA(box, WM_KEYDOWN, VK_RETURN, 0);
        CHECK(g_sub_seen == 1, "which sees the message first");
        SendMessageA(box, WM_CHAR, 'x', 0);
        GetWindowTextA(box, got, (int)sizeof(got));
        CHECK(strlen(got) == 6 && strchr(got, 'x'),
              "and what it passes on still reaches the original");
        DestroyWindow(box);
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
        POINT at;
        RECT wr;
        memset(&ev, 0, sizeof(ev));
        ev.kind = WEEN_EV_MOUSE_MOVE;
        ev.win = w->backend_win;
        ev.x = WEEN_NC_FRAME + 252;
        ev.y = WEEN_NC_FRAME + WEEN_NC_CAPTION + 80;
        ween_headless_inject(ev);
        ev.x = WEEN_NC_FRAME + 100; /* back over the list view */
        ween_headless_inject(ev);
        /* And over the list's own column divider, which is not a class
         * cursor but the view answering for a strip of itself: the list sits
         * ten in with a two-pixel border, and its first column is 120 wide,
         * so the divider is at the end of that. The shape has to be there
         * before the press — it is the only sign the column can be pulled. */
        ev.x = WEEN_NC_FRAME + 10 + 2 + 120;
        ev.y = WEEN_NC_FRAME + WEEN_NC_CAPTION + 10 + 2 + 5;
        ween_headless_inject(ev);
        ev.x -= 40; /* the middle of the heading, not its edge */
        ween_headless_inject(ev);
        ween_event end;
        memset(&end, 0, sizeof(end));
        end.kind = WEEN_EV_END;
        ween_headless_inject(end);

        MSG msg;
        int over_bar = -1, over_list = -1, over_div = -1, over_head = -1;
        while (GetMessageA(&msg, NULL, 0, 0)) {
            DispatchMessageA(&msg);
            if (msg.message == WM_MOUSEMOVE) {
                if (over_bar < 0)
                    over_bar = ween_headless_cursor(w->backend_win);
                else if (over_list < 0)
                    over_list = ween_headless_cursor(w->backend_win);
                else if (over_div < 0)
                    over_div = ween_headless_cursor(w->backend_win);
                else if (over_head < 0)
                    over_head = ween_headless_cursor(w->backend_win);
            }
        }
        CHECK(over_bar == WEEN_CURSOR_SIZEWE,
              "the pointer over the splitter is a resize arrow");
        CHECK(over_list == WEEN_CURSOR_ARROW,
              "and an ordinary one again once it moves off");
        CHECK(over_div == WEEN_CURSOR_SIZEWE,
              "over a column divider it is a resize arrow too");
        CHECK(over_head == WEEN_CURSOR_ARROW,
              "and an ordinary one over the heading itself");
        GetWindowRect(w, &wr);
        CHECK(GetCursorPos(&at) && at.x == wr.left + ev.x &&
                  at.y == wr.top + ev.y,
              "and the pointer says where it is on the screen");
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
                                      WS_CHILD | WS_VISIBLE | LVS_REPORT, 0, 0,
                                      280, 100, lw,
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
        it.iItem = 1;
        it.pszText = (char *)"second";
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

        /* The same seventeen and fourteen say where the second row is, which
         * is what a right click has to turn a point into. */
        {
            LVHITTESTINFO ht;
            memset(&ht, 0, sizeof(ht));
            ht.pt.x = 20;
            ht.pt.y = 17 + 14 + 7;
            CHECK(SendMessageA(narrow, LVM_HITTEST, 0, (LPARAM)&ht) == 1,
                  "the list says which row a point is on");
            ht.pt.y = 5; /* in the header */
            CHECK(SendMessageA(narrow, LVM_HITTEST, 0, (LPARAM)&ht) == -1,
                  "and nothing for a point above the rows");
            /* the right button picks that row, as the shell does, so the menu
             * that follows is about the file under the pointer */
            SendMessageA(narrow, WM_RBUTTONDOWN, 0,
                         MAKELPARAM(20, 17 + 14 + 7));
            CHECK(SendMessageA(narrow, LVM_GETNEXTITEM, (WPARAM)-1,
                               LVNI_SELECTED) == 1,
                  "and the right button selects it");

            /* A row is only its icon and its label. The cells to the right of
             * the name are the background: clicking there drops the selection
             * rather than picking the row, which is what the shell does and
             * why a right click there brings up the folder's own menu. */
            ht.pt.x = 200; /* past the sixty-pixel name column */
            CHECK(SendMessageA(narrow, LVM_HITTEST, 0, (LPARAM)&ht) == -1,
                  "a point past the name is on no row at all");
            SendMessageA(narrow, WM_LBUTTONDOWN, 0,
                         MAKELPARAM(200, 17 + 14 + 7));
            CHECK(SendMessageA(narrow, LVM_GETNEXTITEM, (WPARAM)-1,
                               LVNI_SELECTED) == -1,
                  "clicking there drops the selection");
            CHECK(SendMessageA(narrow, LVM_GETNEXTITEM, (WPARAM)-1,
                               LVNI_FOCUSED) == 1,
                  "and leaves the caret on the row it was on");
            SendMessageA(narrow, WM_KEYDOWN, VK_UP, 0);
            CHECK(SendMessageA(narrow, LVM_GETNEXTITEM, (WPARAM)-1,
                               LVNI_SELECTED) == 0,
                  "so an arrow moves from there, not from the top");
        }
        DestroyWindow(lw);
    }

    /* A heading carried to another place takes its column with it, cells and
     * all: what the shell's Details view does when a column is dragged. A
     * press that does not travel is still a click, and still sorts. */
    {
        HWND mw = CreateWindowExA(0, "weenviews", "move", WS_POPUP | WS_VISIBLE,
                                  0, 0, 320, 140, NULL, NULL, NULL, NULL);
        HWND mv = CreateWindowExA(0, WC_LISTVIEWA, "",
                                  WS_CHILD | WS_VISIBLE | LVS_REPORT, 0, 0,
                                  300, 120, mw, (HMENU)(UINT_PTR)11, NULL,
                                  NULL);
        LVCOLUMNA col;
        LVITEMA it;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (char *)"Name";
        col.cx = 100;
        SendMessageA(mv, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
        col.pszText = (char *)"Size";
        col.cx = 80;
        SendMessageA(mv, LVM_INSERTCOLUMNA, 1, (LPARAM)&col);
        col.pszText = (char *)"Type";
        col.cx = 90;
        SendMessageA(mv, LVM_INSERTCOLUMNA, 2, (LPARAM)&col);
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.pszText = (char *)"a name";
        SendMessageA(mv, LVM_INSERTITEMA, 0, (LPARAM)&it);

        /* a press that stays put is a click on the column */
        g_column_clicked = -1;
        SendMessageA(mv, WM_LBUTTONDOWN, 0, MAKELPARAM(120, 5));
        SendMessageA(mv, WM_LBUTTONUP, 0, MAKELPARAM(120, 5));
        CHECK(g_column_clicked == 1,
              "a press on a heading that stays put is a click");

        /* and one that travels carries the heading: Type before Name */
        g_column_dragged = g_column_dropped = -1;
        SendMessageA(mv, WM_LBUTTONDOWN, 0, MAKELPARAM(200, 5));
        SendMessageA(mv, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 5));
        SendMessageA(mv, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(20, 5));
        SendMessageA(mv, WM_LBUTTONUP, 0, MAKELPARAM(20, 5));
        CHECK(g_column_dragged == 2 && g_column_dropped == 0,
              "one that travels says which heading went where");
        CHECK(SendMessageA(mv, LVM_GETCOLUMNWIDTH, 0, 0) == 90 &&
                  SendMessageA(mv, LVM_GETCOLUMNWIDTH, 1, 0) == 100 &&
                  SendMessageA(mv, LVM_GETCOLUMNWIDTH, 2, 0) == 80,
              "and the columns are in the order it left them");
        {   /* A heading that follows its cells — right-aligned, as Size is —
             * steps aside for the sort arrow rather than putting it past the
             * column's own edge, where nothing would show it. */
            const ween_surface *sf;
            int ox2, oy2, arrow = 0, right = 0;
            LVCOLUMNA sc;
            memset(&sc, 0, sizeof(sc));
            sc.mask = LVCF_FMT;
            sc.fmt = LVCFMT_RIGHT | HDF_STRING | HDF_SORTUP;
            SendMessageA(mv, LVM_SETCOLUMNA, 2, (LPARAM)&sc);
            InvalidateRect(mw, NULL, TRUE);
            ween_flush_paint();
            ween_client_origin(mv, &ox2, &oy2);
            sf = &mw->surface;
            /* the third column runs from 190 to 270: the arrow's white base
             * is inside it, not past its edge */
            for (int x = ox2 + 190; sf && x < ox2 + 270; x++)
                for (int y = oy2; y < oy2 + 17; y++)
                    if ((sf->px[(size_t)y * sf->w + x] & 0xffffff) ==
                        WEEN_WHITE) {
                        arrow++;
                        if (x > right)
                            right = x;
                    }
            CHECK(arrow > 0, "a right-aligned heading shows the sort arrow");
            CHECK(right <= ox2 + 268,
                  "and shows it inside the column, not past its edge");
        }

        {   /* The item's own picture and name go with their column: on the
             * machine the icons are in the Name column wherever it is put,
             * not in whichever column happens to be leftmost. A press on the
             * name in its new place still finds the row. */
            LVHITTESTINFO ht;
            memset(&ht, 0, sizeof(ht));
            ht.pt.x = 100; /* inside the Name column, now the second one */
            ht.pt.y = 17 + 5;
            CHECK(SendMessageA(mv, LVM_HITTEST, 0, (LPARAM)&ht) == 0 &&
                      (ht.flags & LVHT_ONITEMLABEL),
                  "the name is hit where its column now is");
            ht.pt.x = 40; /* where it used to be, now another column's cell */
            CHECK(SendMessageA(mv, LVM_HITTEST, 0, (LPARAM)&ht) == -1,
                  "and not where it used to be");
        }
        DestroyWindow(mw);
    }

    /* A view with tick boxes: what Choose Columns is made of. The box stands
     * where a picture would, the whole of its column answers for it, and the
     * name starts after it rather than under it. */
    {
        HWND tw = CreateWindowExA(0, "weenviews", "ticks",
                                  WS_POPUP | WS_VISIBLE, 0, 0, 260, 140, NULL,
                                  NULL, NULL, NULL);
        HWND ticks = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT |
                                         LVS_NOCOLUMNHEADER,
                                     10, 10, 200, 60, tw,
                                     (HMENU)(UINT_PTR)9, NULL, NULL);
        LVCOLUMNA col;
        LVHITTESTINFO ht;
        LVITEMA it;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_WIDTH | LVCF_TEXT;
        col.cx = 180;
        col.pszText = (char *)"Column";
        SendMessageA(ticks, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
        SendMessageA(ticks, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                     LVS_EX_CHECKBOXES);
        for (int i = 0; i < 5; i++) {
            LVITEMA it;
            memset(&it, 0, sizeof(it));
            it.mask = LVIF_TEXT;
            it.iItem = i;
            it.pszText = (char *)"Name";
            SendMessageA(ticks, LVM_INSERTITEMA, 0, (LPARAM)&it);
        }
        memset(&ht, 0, sizeof(ht));
        ht.pt.x = 2;
        ht.pt.y = 4;
        CHECK(SendMessageA(ticks, LVM_HITTEST, 0, (LPARAM)&ht) == 0 &&
                  (ht.flags & LVHT_ONITEMSTATEICON),
              "the near edge of the state column is the box");
        ht.pt.x = 17; /* the far edge of the same sixteen */
        CHECK(SendMessageA(ticks, LVM_HITTEST, 0, (LPARAM)&ht) == 0 &&
                  (ht.flags & LVHT_ONITEMSTATEICON),
              "and so is its far edge, not just the box drawn in it");
        ht.pt.x = 20; /* two past the column: the name */
        CHECK(SendMessageA(ticks, LVM_HITTEST, 0, (LPARAM)&ht) == 0 &&
                  (ht.flags & LVHT_ONITEMLABEL),
              "the name starts two past the box");

        /* LVS_EX_FULLROWSELECT: the whole row answers, so a press on a cell
         * to the right of the name picks the row rather than the background.
         * Without it that press drops the selection, which is what a shell's
         * file list does. */
        {
            LVHITTESTINFO fr;
            SendMessageA(ticks, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                         LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
            memset(&fr, 0, sizeof(fr));
            fr.pt.x = 150; /* past the name, inside the column */
            fr.pt.y = 4;
            CHECK(SendMessageA(ticks, LVM_HITTEST, 0, (LPARAM)&fr) == 0,
                  "a point past the name is on the row when the row is what "
                  "is picked");
            SendMessageA(ticks, WM_LBUTTONDOWN, 0, MAKELPARAM(150, 4));
            CHECK(SendMessageA(ticks, LVM_GETNEXTITEM, (WPARAM)-1,
                               LVNI_SELECTED) == 0,
                  "and a press there picks it");
            SendMessageA(ticks, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                         LVS_EX_CHECKBOXES);
            memset(&it, 0, sizeof(it));
            it.stateMask = LVIS_SELECTED;
            SendMessageA(ticks, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&it);
        }

        CHECK(!ListView_GetCheckState(ticks, 0), "a row starts unticked");
        SendMessageA(ticks, WM_LBUTTONDOWN, 0, MAKELPARAM(9, 4));
        CHECK(ListView_GetCheckState(ticks, 0),
              "clicking the box ticks it, and clicking it again");
        SendMessageA(ticks, WM_LBUTTONDOWN, 0, MAKELPARAM(9, 4));
        CHECK(!ListView_GetCheckState(ticks, 0), "unticks it");

        /* The second press of a quick pair arrives as a double click, and the
         * box takes it as it took the first: clicking one fast flips it every
         * time. The machine drops that press — its box turns over once for
         * every two — and this is a deliberate departure from it. The row is
         * still not picked, which the machine does agree with. */
        SendMessageA(ticks, WM_LBUTTONDBLCLK, 0, MAKELPARAM(9, 4));
        CHECK(ListView_GetCheckState(ticks, 0),
              "a double click on the box turns it over again");
        CHECK(SendMessageA(ticks, LVM_GETNEXTITEM, (WPARAM)-1,
                           LVNI_SELECTED) == -1,
              "and does not pick the row it is on");
        DestroyWindow(tw);
    }

    /* Renaming in place: the box the shell opens over a name. Measured on the
     * machine's C: window — a picked name's blue box is 69 wide and seventeen
     * tall for CONFIG.SYS, and the white box F2 opens is 81 wide starting two
     * pixels further left, standing on the same seventeen rows. The name
     * inside is drawn one pixel right of where the row drew it, which is the
     * margin the edit control keeps anyway. */
    {
        HWND rw = CreateWindowExA(0, "weenviews", "rename",
                                  WS_POPUP | WS_VISIBLE, 0, 0, 260, 140, NULL,
                                  NULL, NULL, NULL);
        HWND names = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT |
                                         LVS_EDITLABELS | LVS_NOCOLUMNHEADER,
                                     10, 10, 220, 80, rw, (HMENU)(UINT_PTR)10,
                                     NULL, NULL);
        HIMAGELIST img = ImageList_Create(16, 16, ILC_COLOR, 2, 0);
        const ween_strike *f = ween_gui_font();
        int drawn = ween_strike_text_width(f, "WINNT", 5);
        LVCOLUMNA col;
        LVITEMA it;
        RECT label, bounds, box;
        POINT origin;
        HWND ed;

        memset(&col, 0, sizeof(col));
        col.mask = LVCF_WIDTH | LVCF_TEXT;
        col.cx = 180;
        col.pszText = (char *)"Name";
        SendMessageA(names, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
        SendMessageA(names, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)img);
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_IMAGE;
        it.iItem = 0;
        it.pszText = (char *)"WINNT";
        it.iImage = 0;
        SendMessageA(names, LVM_INSERTITEMA, 0, (LPARAM)&it);
        it.iItem = 1;
        it.pszText = (char *)"boot";
        SendMessageA(names, LVM_INSERTITEMA, 1, (LPARAM)&it);

        label.left = LVIR_LABEL;
        SendMessageA(names, LVM_GETITEMRECT, 0, (LPARAM)&label);
        CHECK(label.right - label.left == drawn + 8,
              "a name's box is what it draws and eight — 42 for \"WINNT\" "
              "on the machine");
        bounds.left = LVIR_BOUNDS;
        SendMessageA(names, LVM_GETITEMRECT, 0, (LPARAM)&bounds);

        ed = (HWND)(INT_PTR)SendMessageA(names, LVM_EDITLABELA, 0, 0);
        CHECK(ed && ed == (HWND)(INT_PTR)SendMessageA(names,
                                                      LVM_GETEDITCONTROL, 0, 0),
              "renaming opens a box the view hands back");
        if (ed) {
            origin.x = 0;
            origin.y = 0;
            ClientToScreen(names, &origin);
            GetWindowRect(ed, &box);
            CHECK(box.left - origin.x == label.left - 2,
                  "it starts two pixels left of the name's own box");
            CHECK(box.right - box.left == label.right - label.left + 12,
                  "and is twelve wider than it");
            CHECK(box.top - origin.y == bounds.top &&
                      box.bottom - box.top == bounds.bottom - bounds.top,
                  "and stands on the row, as tall as the row");
        }
        if (ed)
            SendMessageA(ed, WM_KEYDOWN, VK_ESCAPE, 0);

        /* A click on the name that is already picked asks to rename it, and
         * the view waits out the double-click time before it does: that
         * second press could be the first half of a double click, and opening
         * what it is on comes first. Timed on the machine, whose box appears
         * between 450 and 550 ms after such a click. */
        {
            int x = label.left + 4, y = (bounds.top + bounds.bottom) / 2;
            SetFocus(names);
            SendMessageA(names, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
            SendMessageA(names, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            SendMessageA(names, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
            SendMessageA(names, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            CHECK(!SendMessageA(names, LVM_GETEDITCONTROL, 0, 0),
                  "a second click on a picked name does not rename it at once");
            SendMessageA(names, WM_TIMER, 0x7e03, 0); /* the wait running out */
            CHECK(SendMessageA(names, LVM_GETEDITCONTROL, 0, 0) != 0,
                  "it renames when the double-click time has passed");
            ed = (HWND)(INT_PTR)SendMessageA(names, LVM_GETEDITCONTROL, 0, 0);
            if (ed)
                SendMessageA(ed, WM_KEYDOWN, VK_ESCAPE, 0);

            /* and the pair that is a double click calls the wait off */
            SendMessageA(names, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
            SendMessageA(names, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            SendMessageA(names, WM_LBUTTONDBLCLK, 0, MAKELPARAM(x, y));
            SendMessageA(names, WM_TIMER, 0x7e03, 0);
            CHECK(!SendMessageA(names, LVM_GETEDITCONTROL, 0, 0),
                  "a double click opens what it is on instead of renaming it");

            /* a click on the other name picks it and nothing more */
            {
                int y2 = y + (bounds.bottom - bounds.top);
                SendMessageA(names, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y2));
                SendMessageA(names, WM_LBUTTONUP, 0, MAKELPARAM(x, y2));
                SendMessageA(names, WM_TIMER, 0x7e03, 0);
                CHECK(SendMessageA(names, LVM_GETNEXTITEM, (WPARAM)-1,
                                   LVNI_SELECTED) == 1 &&
                          !SendMessageA(names, LVM_GETEDITCONTROL, 0, 0),
                      "and a click that picks a name does not rename it");
            }
        }
        DestroyWindow(rw);
    }

    /* Whose the pictures are. A view takes the image lists it was given with
     * it when it goes, which is why the block above hands one over and never
     * destroys it. LVS_SHAREIMAGELISTS is how a program says the set hangs on
     * other controls too and is not this one's to take: the file dialog's
     * list says it, and so does an explorer's, whose one set of icons is also
     * on the tree, both toolbars and the address bar. Only the sharing half
     * can be asserted from here — the other half is a leak, and the
     * sanitizer is what sees that. */
    {
        unsigned char bits[16 * 16 * 3];
        HWND sw = CreateWindowExA(0, "weenviews", "shared", WS_POPUP, 0, 0,
                                  200, 120, NULL, NULL, NULL, NULL);
        HIMAGELIST shared = ImageList_Create(16, 16, ILC_MASK, 2, 0);
        HWND shares = CreateWindowExA(0, WC_LISTVIEWA, "",
                                      WS_CHILD | LVS_REPORT |
                                          LVS_SHAREIMAGELISTS,
                                      0, 0, 180, 80, sw, (HMENU)(UINT_PTR)11,
                                      NULL, NULL);
        HBITMAP art;

        memset(bits, 0x40, sizeof bits);
        art = CreateBitmap(16, 16, 1, 24, bits);
        ImageList_AddMasked(shared, art, RGB(0xff, 0, 0xff));
        DeleteObject(art);
        CHECK(ImageList_GetImageCount(shared) == 1,
              "an image list with one picture in it");
        CHECK(SendMessageA(shares, LVM_SETIMAGELIST, LVSIL_SMALL,
                           (LPARAM)shared) == 0,
              "a view that was carrying none hands back none");
        CHECK((HIMAGELIST)(UINT_PTR)SendMessageA(shares, LVM_SETIMAGELIST,
                                                 LVSIL_SMALL,
                                                 (LPARAM)shared) == shared,
              "and hands back the one it was carrying");
        DestroyWindow(sw);
        CHECK(ImageList_GetImageCount(shared) == 1,
              "a shared list stands after the view that drew from it is gone");
        ImageList_Destroy(shared);
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
        COMBOBOXINFO cbi;
        HWND list;
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

        /* open it, and let go — which is where the tracking used to stop.
         * The list is a window of its own, so what it drew is in its own
         * surface and the box will say which window that is. */
        SendMessageA(cb, WM_LBUTTONDOWN, 0, MAKELPARAM(140, 8));
        SendMessageA(cb, WM_LBUTTONUP, 0, MAKELPARAM(140, 8));
        memset(&cbi, 0, sizeof(cbi));
        cbi.cbSize = sizeof(cbi);
        GetComboBoxInfo(cb, &cbi);
        CHECK(cbi.hwndList != NULL, "an open list is a window, and it says so");
        list = cbi.hwndList;
        InvalidateRect(list, NULL, TRUE);
        ween_flush_paint();
        s = &list->surface;
        for (int y = 0; y < s->h; y++)
            if ((s->px[(size_t)y * s->w + 20] & 0xffffff) == WEEN_CAP_LEFT) {
                if (first_top < 0)
                    first_top = y;
                first_bottom = y;
            }
        CHECK(first_top > 0, "opening it highlights the item it is showing");

        /* one item further down, in the coordinates a routed move arrives in */
        ween_client_origin(cb, &ox, &oy);
        {
            RECT lr;
            ween_combo_list_rect(cb, &lr);
            SendMessageA(cb, WM_MOUSEMOVE, 0,
                         MAKELPARAM(20, lr.top + first_bottom + 2 - oy));
        }
        InvalidateRect(list, NULL, TRUE);
        ween_flush_paint();
        s = &list->surface;
        for (int y = 0; y < s->h && then_top < 0; y++)
            if ((s->px[(size_t)y * s->w + 20] & 0xffffff) == WEEN_CAP_LEFT)
                then_top = y;
        CHECK(then_top > first_top,
              "and moving over the next one moves the highlight to it");

        /* A list opened by a press stays open when the button comes up,
         * even if the pointer stirred in between. It is one click: the
         * pointer never keeps perfectly still between the press and the
         * release, and a list that shut itself the moment it opened was
         * unusable. */
        SendMessageA(cb, WM_LBUTTONUP, 0, MAKELPARAM(140, 8));
        SendMessageA(cb, WM_LBUTTONDOWN, 0, MAKELPARAM(140, 8));
        SendMessageA(cb, WM_MOUSEMOVE, 0, MAKELPARAM(141, 9));
        SendMessageA(cb, WM_LBUTTONUP, 0, MAKELPARAM(141, 9));
        memset(&cbi, 0, sizeof(cbi));
        cbi.cbSize = sizeof(cbi);
        GetComboBoxInfo(cb, &cbi);
        CHECK(cbi.hwndList != NULL,
              "a twitch of the pointer between press and release leaves it up");
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

        /* A box of this class keeps room for a picture whether or not it has
         * been given any, which is why the machine's file dialog has its file
         * name box a pixel taller than the plain one under it. */
        CHECK(was_h >= 16 + 6, "a box that carries pictures has room for one");
        SendMessageA(cb, CBEM_SETIMAGELIST, 0, (LPARAM)il2);
        CHECK(cb->h == was_h,
              "and an image list does not change that");

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

        {
            /* A combo box's text is whatever is in its field, both ways
             * round: that is what GetWindowText answers on win32, and it is
             * how a dialog reads the name somebody typed into a file box --
             * without it, Save As had nothing to save to. */
            HWND ed = CreateWindowExA(0, "COMBOBOX", "",
                                      WS_CHILD | WS_VISIBLE | CBS_DROPDOWN, 10,
                                      120, 150, 100, cw,
                                      (HMENU)(UINT_PTR)10, NULL, NULL);
            char back[64];
            SetWindowTextA(ed, "typed.bmp");
            GetWindowTextA(ed, back, (int)sizeof(back));
            CHECK(strcmp(back, "typed.bmp") == 0,
                  "setting a combo box's text sets what its field shows");
            {   /* and what the field is given comes back from the box */
                HWND field = (HWND)SendMessageA(ed, CBEM_GETEDITCONTROL, 0, 0);
                CHECK(field != NULL, "an editable box has a field");
                if (field) {
                    SetWindowTextA(field, "into the field");
                    GetWindowTextA(ed, back, (int)sizeof(back));
                    CHECK(strcmp(back, "into the field") == 0,
                          "and typing in the field is the box's text");
                }
            }
            DestroyWindow(ed);
        }

        /* Emptying it is what an address bar does on every folder, and the
         * image list has to survive that — it belongs to the control, not to
         * the items that were in it. */
        SendMessageA(cb, CB_RESETCONTENT, 0, 0);
        CHECK(SendMessageA(cb, CB_GETCOUNT, 0, 0) == 0, "and can be emptied");
        CHECK(SendMessageA(cb, CBEM_INSERTITEMA, 0, (LPARAM)&ci) == 0 &&
                  cb->h == was_h,
              "with the image list still on it afterwards");
        DestroyWindow(cw);
        ImageList_Destroy(il2);
    }
    /* **A control declared before a group box must survive it.** A group box
     * draws a frame and a label and nothing else, so on win32 one declared
     * after its contents leaves them alone -- and probe.exe reads the
     * machine's own first Options page declaring its group **fifth of six**,
     * after the four option buttons it holds, with all four still there.
     *
     * Ours lost them, because every built-in class was registered with a
     * COLOR_BTNFACE background and a control's window is filled before it
     * paints. win32's BUTTON and STATIC have **no** class background: a
     * control paints what it needs and lets the parent's colour through the
     * rest. wordpad's Options page had been declaring its group boxes first
     * to compensate. */
    {
        HWND host = CreateWindowA("weenviews", "", WS_OVERLAPPEDWINDOW, 0, 0,
                                  300, 160, NULL, NULL, NULL, NULL);
        HWND rb = CreateWindowExA(0, "BUTTON", "&Inches",
                                  WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                  30, 40, 90, 16, host, (HMENU)(UINT_PTR)61,
                                  NULL, NULL);
        /* declared *after* the button it holds, and around it */
        HWND box = CreateWindowExA(0, "BUTTON", "Units",
                                   WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 20, 20,
                                   200, 80, host, (HMENU)(UINT_PTR)62, NULL,
                                   NULL);
        const ween_surface *gs;
        int gx, gy, ink = 0;
        ShowWindow(host, SW_SHOWNORMAL);
        InvalidateRect(host, NULL, TRUE);
        ween_flush_paint();
        gs = ween_headless_surface();
        ween_client_origin(rb, &gx, &gy);
        for (int y = 0; gs && y < 16; y++)
            for (int x = 0; x < 90; x++)
                if ((gs->px[(size_t)(gy + y) * gs->w + gx + x] & 0xffffff) ==
                    (WEEN_BLACK & 0xffffff))
                    ink++;
        CHECK(ink > 0,
              "an option button declared before the group box around it is "
              "still there");
        (void)box;
        DestroyWindow(host);
    }


    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("views_test: all passed\n");
    return 0;
}
