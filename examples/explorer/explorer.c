/* A Windows 2000 file browser, written as plain win32.
 *
 * Like every example here it compiles unchanged against real <windows.h> and
 * against ween32, so the same source can be rendered by Windows and by us and
 * the two compared. Reading directories is the one thing win32 and POSIX
 * disagree about, and fs.h is where that disagreement is kept.
 *
 * The layout is measured off a screenshot of the real thing: a menu bar and
 * two rebar bands across the top, a tree and a list side by side with a
 * splitter between them, and a status bar in three parts underneath.
 */

#include <ween32.h>

#ifdef _WIN32
#include <commctrl.h>
#define HAVE(feature) 1
#else
#define HAVE(feature) WEEN32_HAS_##feature
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../fs.h"

/* ---- what the window is made of ------------------------------------------ */

enum {
    ID_TREE = 100,
    ID_LIST,
    ID_TOOLBAR,
    ID_REBAR,
    ID_ADDRESS,
    ID_STATUS,
    ID_SPLIT,
    ID_PANEHEAD,

    /* toolbar and menu commands */
    IDM_BACK = 200,
    IDM_FORWARD,
    IDM_UP,
    IDM_SEARCH,
    IDM_FOLDERS,
    IDM_HISTORY,
    IDM_MOVETO,
    IDM_COPYTO,
    IDM_DELETE,
    IDM_UNDO,
    IDM_VIEWS,
    IDM_CLOSE,
    IDM_ABOUT
};

/* the icons the shell shows, by their number in assets/icons */
#define ICON_FOLDER "4"
#define ICON_FOLDER_OPEN "5"
#define ICON_FILE "1"
#define ICON_COMPUTER "16"
#define ICON_DRIVE "9"
#define ICON_UP "34"
#define ICON_MOVETO "137"
#define ICON_COPYTO "138"
#define ICON_SEARCH "50"
#define ICON_HISTORY "35"
#define ICON_APP "46" /* the folder-and-magnifier the caption wears */

/* Image-list indices, in the order they are added. */
enum { IMG_FOLDER, IMG_FOLDER_OPEN, IMG_FILE, IMG_COMPUTER, IMG_DRIVE,
       IMG_UP, IMG_SEARCH, IMG_HISTORY, IMG_MOVETO, IMG_COPYTO,
       /* the drawn ones, added after everything read from disk */
       IMG_BACK, IMG_FORWARD, IMG_DELETE, IMG_UNDO, IMG_VIEWS, IMG_COUNT };

/* Some of a toolbar's glyphs were never icons — they came out of a bitmap
 * strip — so they are drawn here instead, a pixel at a time. '.' is the
 * colour the image list masks out and the digits index a grey ramp, which is
 * all these need: an arrow lit from the top left, and the greys a button
 * with nothing to act on wears. */
typedef struct {
    int w, h;
    const char *const *rows;
} glyph;

static const char *const GLYPH_BACK[] = {
    ".....0.......",
    "....20.......",
    "...260.......",
    "..26502222220",
    ".265555555520",
    ".024422222220",
    "..02200000000",
    "...020.......",
    "....00.......",
    ".....0.......",
    ".............",
};

static const char *const GLYPH_FORWARD[] = {
    ".......2.....",
    ".......22....",
    ".......222...",
    "22222222222..",
    "222222222222.",
    "2222222222226",
    "2222222222266",
    ".66666622266.",
    ".......2266..",
    ".......266...",
    "........6....",
};

static const char *const GLYPH_DELETE[] = {
    "222.........1",
    "2640......20.",
    ".2240....20..",
    "...240..20...",
    "....22020....",
    ".....220.....",
    "....22020....",
    "...220..20...",
    "..240....20..",
    ".240......2..",
    "2620.......1.",
    "220..........",
    ".0..........1",
};

static const char *const GLYPH_UNDO[] = {
    "........22222...",
    "......22566540..",
    ".2...2644000440.",
    ".20.26400...0440",
    ".260640......240",
    ".26640.......240",
    ".25440.......240",
    ".244440.....2440",
    ".0000000....240.",
    "............000.",
    "................",
    "................",
};

static const char *const GLYPH_VIEWS[] = {
    "2222222222222220",
    "2533333333333330",
    "2111111111111110",
    "2666666666666640",
    "2630600663060040",
    "2600666660066640",
    "2666666666666640",
    "2630600663060040",
    "2600666660066640",
    "2666666666666640",
    "2630600663060040",
    "2600666660066640",
    "2444444444444440",
    "0000000000000000",
};

/* indexed by IMG_BACK.. — the order add_glyph is called in */
static const glyph GLYPHS[] = {
    { 13, 11, GLYPH_BACK },
    { 13, 11, GLYPH_FORWARD },
    { 13, 13, GLYPH_DELETE },
    { 16, 12, GLYPH_UNDO },
    { 16, 14, GLYPH_VIEWS },
};

static COLORREF glyph_colour(char c)
{
    static const int ramp[] = { 0, 64, 128, 160, 192, 224, 255 };
    int i;
    if (c < '0' || c > '6')
        return RGB(255, 0, 255); /* masked out */
    i = c - '0';
    return RGB(ramp[i], ramp[i], ramp[i]);
}

static HWND g_main, g_tree, g_list, g_toolbar, g_rebar, g_address, g_status;
static HWND g_split, g_panehead;
static HIMAGELIST g_images;
static HFONT g_font;
static int g_split_x = 203; /* the tree pane's width, measured off the shot */
static int g_dragging;
static char g_path[1024] = "/";

/* ---- geometry ------------------------------------------------------------ */

#define PANE_HEAD_H 20  /* the "Folders" bar above the tree */
#define SPLIT_W 2
#define STATUS_H 20

static int rebar_height(void)
{
    return g_rebar ? (int)SendMessageA(g_rebar, RB_GETBARHEIGHT, 0, 0) : 0;
}

static void layout(HWND w)
{
    RECT cr;
    int top, bottom, left_w;
    GetClientRect(w, &cr);
    top = rebar_height();
    bottom = cr.bottom - STATUS_H;
    left_w = g_split_x;
    if (left_w < 60)
        left_w = 60;
    if (left_w > cr.right - 120)
        left_w = cr.right - 120;

    if (g_rebar)
        MoveWindow(g_rebar, 0, 0, cr.right, top, TRUE);
    if (g_panehead)
        MoveWindow(g_panehead, 0, top, left_w, PANE_HEAD_H, TRUE);
    if (g_tree)
        MoveWindow(g_tree, 0, top + PANE_HEAD_H, left_w,
                   bottom - top - PANE_HEAD_H, TRUE);
    if (g_split)
        MoveWindow(g_split, left_w, top, SPLIT_W, bottom - top, TRUE);
    if (g_list)
        MoveWindow(g_list, left_w + SPLIT_W, top,
                   cr.right - left_w - SPLIT_W, bottom - top, TRUE);
    if (g_status) {
        /* The two right-hand parts keep a fixed width and the first takes
         * whatever is left, so the counts stay put as the window widens. */
        int parts[3];
        MoveWindow(g_status, 0, bottom, cr.right, STATUS_H, TRUE);
        parts[0] = cr.right - 233;
        parts[1] = cr.right - 152;
        parts[2] = -1;
        SendMessageA(g_status, SB_SETPARTS, 3, (LPARAM)parts);
    }
}

/* ---- the file list ------------------------------------------------------- */

static const char *type_of(const fs_entry *e)
{
    const char *dot;
    if (e->is_dir)
        return "File Folder";
    dot = strrchr(e->name, '.');
    if (!dot || !dot[1])
        return "File";
    if (!strcmp(dot, ".txt"))
        return "Text Document";
    if (!strcmp(dot, ".ini"))
        return "Configuration Settings";
    if (!strcmp(dot, ".exe"))
        return "Application";
    if (!strcmp(dot, ".dll"))
        return "Application Extension";
    if (!strcmp(dot, ".log"))
        return "Text Document";
    return "File";
}

static void set_cell(HWND list, int row, int col, const char *text)
{
    LVITEMA it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iSubItem = col;
    it.pszText = (char *)text;
    SendMessageA(list, LVM_SETITEMTEXTA, (WPARAM)row, (LPARAM)&it);
}

/* Fill the list from a directory, and say in the status bar what is in it. */
static void show_directory(const char *path)
{
    fs_dir d;
    fs_entry e;
    int row = 0;
    unsigned long bytes = 0;
    char line[256];

    SendMessageA(g_list, LVM_DELETEALLITEMS, 0, 0);
    if (!fs_open(&d, path)) {
        SendMessageA(g_status, SB_SETTEXTA, 0, (LPARAM) "Access denied");
        return;
    }
    while (fs_next(&d, &e)) {
        LVITEMA it;
        char size[32];
        if (e.name[0] == '.') /* the shell hides these, and so do we */
            continue;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_IMAGE;
        it.iItem = row;
        it.pszText = e.name;
        it.iImage = e.is_dir ? IMG_FOLDER : IMG_FILE;
        SendMessageA(g_list, LVM_INSERTITEMA, 0, (LPARAM)&it);
        if (e.is_dir) {
            set_cell(g_list, row, 1, "");
        } else {
            snprintf(size, sizeof(size), "%lu KB", (e.size + 1023) / 1024);
            set_cell(g_list, row, 1, size);
            bytes += e.size;
        }
        set_cell(g_list, row, 2, type_of(&e));
        set_cell(g_list, row, 3, e.modified);
        row++;
    }
    fs_close(&d);

    snprintf(line, sizeof(line), "%d object(s)", row);
    SendMessageA(g_status, SB_SETTEXTA, 0, (LPARAM)line);
    snprintf(line, sizeof(line), "%lu bytes", bytes);
    SendMessageA(g_status, SB_SETTEXTA, 1, (LPARAM)line);
    SendMessageA(g_status, SB_SETTEXTA, 2, (LPARAM) "My Computer");

    strncpy(g_path, path, sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = 0;

    /* the address bar shows where you are, and the caption the folder's own
     * name — which is what the shell puts there */
    SendMessageA(g_address, CB_RESETCONTENT, 0, 0);
    SendMessageA(g_address, CB_ADDSTRING, 0, (LPARAM)path);
    SendMessageA(g_address, CB_SETCURSEL, 0, 0);
    {
        const char *leaf = strrchr(path, '/');
        SetWindowTextA(g_main, leaf && leaf[1] ? leaf + 1 : path);
    }
}

/* ---- the folder tree ------------------------------------------------------ */

static HTREEITEM add_node(HTREEITEM parent, const char *text, int image,
                          int has_children)
{
    TVINSERTSTRUCTA is;
    memset(&is, 0, sizeof(is));
    is.hParent = parent ? parent : TVI_ROOT;
    is.item.mask = TVIF_TEXT | TVIF_IMAGE;
    is.item.pszText = (char *)text;
    is.item.iImage = image;
    (void)has_children;
    return (HTREEITEM)SendMessageA(g_tree, TVM_INSERTITEMA, 0, (LPARAM)&is);
}

/* The path of a tree item, walked back up to the root. */
static void path_of_item(HTREEITEM item, char *out, size_t len)
{
    char names[32][260];
    int n = 0;
    while (item && n < 32) {
        TVITEMA q;
        memset(&q, 0, sizeof(q));
        q.mask = TVIF_TEXT;
        q.hItem = item;
        q.pszText = names[n];
        q.cchTextMax = (int)sizeof(names[n]);
        if (!SendMessageA(g_tree, TVM_GETITEMA, 0, (LPARAM)&q))
            break;
        n++;
        item = (HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_PARENT,
                                       (LPARAM)item);
    }
    /* the names came back leaf first, so the path is built in reverse */
    out[0] = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (strcmp(names[i], "/") != 0) {
            strncat(out, "/", len - strlen(out) - 1);
            strncat(out, names[i], len - strlen(out) - 1);
        }
    }
    if (!out[0])
        strncpy(out, "/", len - 1);
}

/* One level of the tree, filled when its parent is opened. */
static void fill_children(HTREEITEM parent, const char *path)
{
    fs_dir d;
    fs_entry e;
    if (!fs_open(&d, path))
        return;
    while (fs_next(&d, &e)) {
        if (!e.is_dir || e.name[0] == '.')
            continue;
        add_node(parent, e.name, IMG_FOLDER, 1);
    }
    fs_close(&d);
}

/* ---- the pane header and the splitter ------------------------------------- */

static LRESULT CALLBACK panehead_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r = ps.rcPaint, x;
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        r.left += 6;
        DrawTextA(dc, "Folders", -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        /* the close box at its right, as the real one has */
        x = ps.rcPaint;
        x.left = x.right - 18;
        x.right = x.left + 14;
        x.top += 3;
        x.bottom = x.top + 14;
        DrawFrameControl(dc, &x, DFC_CAPTION, DFCS_CAPTIONCLOSE);
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static LRESULT CALLBACK splitter_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
        g_dragging = 1;
        SetCapture(w);
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging && GetCapture() == w) {
            RECT wr, mr;
            GetWindowRect(w, &wr);
            GetWindowRect(g_main, &mr);
            g_split_x = wr.left + GET_X_LPARAM(lp) - mr.left;
            layout(g_main);
            InvalidateRect(g_main, NULL, TRUE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) {
            g_dragging = 0;
            ReleaseCapture();
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r = ps.rcPaint;
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
        EndPaint(w, &ps);
        return 0;
    }
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* ---- building the window -------------------------------------------------- */

#if HAVE(MENU)
static void build_menu(HWND w)
{
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu(), edit = CreatePopupMenu();
    HMENU view = CreatePopupMenu(), favorites = CreatePopupMenu();
    HMENU tools = CreatePopupMenu(), help = CreatePopupMenu();

    AppendMenuA(file, MF_STRING, IDM_CLOSE, "&Close");
    AppendMenuA(edit, MF_STRING | MF_GRAYED, 0, "Cu&t\tCtrl+X");
    AppendMenuA(edit, MF_STRING | MF_GRAYED, 0, "&Copy\tCtrl+C");
    AppendMenuA(edit, MF_STRING | MF_GRAYED, 0, "&Paste\tCtrl+V");
    AppendMenuA(view, MF_STRING | MF_CHECKED, IDM_FOLDERS, "&Folders");
    AppendMenuA(favorites, MF_STRING | MF_GRAYED, 0, "(empty)");
    AppendMenuA(tools, MF_STRING | MF_GRAYED, 0, "&Map Network Drive...");
    AppendMenuA(help, MF_STRING, IDM_ABOUT, "&About Windows");

    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)edit, "&Edit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "&View");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)favorites, "F&avorites");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)tools, "&Tools");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)help, "&Help");
    SetMenu(w, bar);
}
#endif

#if HAVE(IMAGELIST)
/* The art, centred in a 16x16 bitmap and handed over with its background
 * named as the transparent colour — which is all ImageList_AddMasked wants. */
static void add_glyph(const glyph *g)
{
    unsigned char bits[16 * 16 * 4];
    HBITMAP bmp;
    int ox = (16 - g->w) / 2, oy = (16 - g->h) / 2;

    for (int i = 0; i < 16 * 16; i++) {
        bits[i * 4 + 0] = 0xff; /* B,G,R as every win32 DIB is */
        bits[i * 4 + 1] = 0x00;
        bits[i * 4 + 2] = 0xff;
        bits[i * 4 + 3] = 0;
    }
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            COLORREF c = glyph_colour(g->rows[y][x]);
            unsigned char *p = bits + (((size_t)(y + oy) * 16) + x + ox) * 4;
            p[0] = (unsigned char)(c >> 16);
            p[1] = (unsigned char)(c >> 8);
            p[2] = (unsigned char)c;
        }
    }
    bmp = CreateBitmap(16, 16, 1, 32, bits);
    ImageList_AddMasked(g_images, bmp, RGB(255, 0, 255));
    DeleteObject(bmp);
}

static void load_icons(void)
{
    static const char *names[] = {
        ICON_FOLDER, ICON_FOLDER_OPEN, ICON_FILE,   ICON_COMPUTER,
        ICON_DRIVE,  ICON_UP,          ICON_SEARCH, ICON_HISTORY,
        ICON_MOVETO, ICON_COPYTO
    };
    g_images = ImageList_Create(16, 16, ILC_MASK, IMG_COUNT, 4);
    for (int i = 0; i < (int)(sizeof(names) / sizeof(*names)); i++) {
        char path[128];
        HICON icon;
        snprintf(path, sizeof(path), "assets/icons/%s.ico", names[i]);
        icon = (HICON)LoadImageA(NULL, path, IMAGE_ICON, 16, 16,
                                 LR_LOADFROMFILE);
        if (icon) {
            ImageList_AddIcon(g_images, icon);
            DestroyIcon(icon);
        }
    }
    for (int i = 0; i < (int)(sizeof(GLYPHS) / sizeof(*GLYPHS)); i++)
        add_glyph(&GLYPHS[i]);
}
#endif

#if HAVE(TOOLBAR)
static void build_bands(HWND w)
{
    TBBUTTON b[14];
    REBARBANDINFOA bi;
    int n = 0;

    g_rebar = CreateWindowExA(0, REBARCLASSNAMEA, "", WS_CHILD | WS_VISIBLE, 0,
                              0, 100, 50, w, (HMENU)(UINT_PTR)ID_REBAR, NULL,
                              NULL);
    g_toolbar = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT |
                                    TBSTYLE_LIST,
                                0, 0, 100, 22, g_rebar,
                                (HMENU)(UINT_PTR)ID_TOOLBAR, NULL, NULL);
    SendMessageA(g_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageA(g_toolbar, TB_SETIMAGELIST, 0, (LPARAM)g_images);

    memset(b, 0, sizeof(b));
    b[n].iBitmap = IMG_BACK;
    b[n].idCommand = IDM_BACK;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON | TBSTYLE_DROPDOWN;
    b[n].iString = (INT_PTR) "Back";
    n++;
    b[n].iBitmap = IMG_FORWARD;
    b[n].idCommand = IDM_FORWARD;
    b[n].fsState = 0; /* nowhere forward to go yet */
    b[n].fsStyle = TBSTYLE_BUTTON | TBSTYLE_DROPDOWN;
    n++;
    b[n].iBitmap = IMG_UP;
    b[n].idCommand = IDM_UP;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].fsStyle = TBSTYLE_SEP;
    n++;
    b[n].iBitmap = IMG_SEARCH;
    b[n].idCommand = IDM_SEARCH;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON;
    b[n].iString = (INT_PTR) "Search";
    n++;
    b[n].iBitmap = IMG_FOLDER_OPEN;
    b[n].idCommand = IDM_FOLDERS;
    b[n].fsState = TBSTATE_ENABLED | TBSTATE_CHECKED;
    b[n].fsStyle = TBSTYLE_CHECK;
    b[n].iString = (INT_PTR) "Folders";
    n++;
    b[n].iBitmap = IMG_HISTORY;
    b[n].idCommand = IDM_HISTORY;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON;
    b[n].iString = (INT_PTR) "History";
    n++;
    b[n].fsStyle = TBSTYLE_SEP;
    n++;
    /* The four that act on a selection, and so are labelless and — with
     * nothing selected, as the shot has it — all but the first two dead. */
    b[n].iBitmap = IMG_MOVETO;
    b[n].idCommand = IDM_MOVETO;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].iBitmap = IMG_COPYTO;
    b[n].idCommand = IDM_COPYTO;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].iBitmap = IMG_DELETE;
    b[n].idCommand = IDM_DELETE;
    b[n].fsState = 0;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].iBitmap = IMG_UNDO;
    b[n].idCommand = IDM_UNDO;
    b[n].fsState = 0;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].fsStyle = TBSTYLE_SEP;
    n++;
    b[n].iBitmap = IMG_VIEWS;
    b[n].idCommand = IDM_VIEWS;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON | TBSTYLE_DROPDOWN;
    n++;
    SendMessageA(g_toolbar, TB_ADDBUTTONSA, n, (LPARAM)b);

    g_address = CreateWindowExA(WS_EX_CLIENTEDGE, "COMBOBOX", "",
                                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0,
                                300, 21, g_rebar, (HMENU)(UINT_PTR)ID_ADDRESS,
                                NULL, NULL);

    memset(&bi, 0, sizeof(bi));
    bi.cbSize = sizeof(bi);
    bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE;
    bi.hwndChild = g_toolbar;
    bi.cyMinChild = 22;
    SendMessageA(g_rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);

    bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_TEXT;
    bi.hwndChild = g_address;
    bi.cyMinChild = 22;
    bi.lpText = (char *)"Address";
    SendMessageA(g_rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);
}
#endif

static void build_views(HWND w)
{
    static const struct {
        const char *title;
        int width;
    } columns[] = {
        { "Name", 119 }, { "Size", 75 }, { "Type", 120 }, { "Modified", 120 }
    };

    g_panehead = CreateWindowA("explorerpane", "", WS_CHILD | WS_VISIBLE, 0, 0,
                               10, 10, w, (HMENU)(UINT_PTR)ID_PANEHEAD, NULL,
                               NULL);
    g_tree = CreateWindowExA(WS_EX_CLIENTEDGE, WC_TREEVIEWA, "",
                             WS_CHILD | WS_VISIBLE | TVS_HASLINES |
                                 TVS_HASBUTTONS,
                             0, 0, 10, 10, w, (HMENU)(UINT_PTR)ID_TREE, NULL,
                             NULL);
    g_split = CreateWindowA("explorersplit", "", WS_CHILD | WS_VISIBLE, 0, 0,
                            10, 10, w, (HMENU)(UINT_PTR)ID_SPLIT, NULL, NULL);
    g_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                             WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, w,
                             (HMENU)(UINT_PTR)ID_LIST, NULL, NULL);

#if HAVE(IMAGELIST)
    SendMessageA(g_tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)g_images);
    SendMessageA(g_list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_images);
#endif

    for (int i = 0; i < 4; i++) {
        LVCOLUMNA col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (char *)columns[i].title;
        col.cx = columns[i].width;
        SendMessageA(g_list, LVM_INSERTCOLUMNA, (WPARAM)i, (LPARAM)&col);
    }

    g_status = CreateWindowA(STATUSCLASSNAMEA, "",
                             WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 10,
                             10, w, (HMENU)(UINT_PTR)ID_STATUS, NULL, NULL);
}

static LRESULT CALLBACK explorer_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_main = w;
#if HAVE(MENU)
        build_menu(w);
#endif
#if HAVE(IMAGELIST)
        load_icons();
#endif
#if HAVE(TOOLBAR)
        build_bands(w);
#endif
        build_views(w);
        {
            HTREEITEM root = add_node(NULL, "/", IMG_COMPUTER, 1);
            fill_children(root, "/");
            SendMessageA(g_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)root);
        }
        show_directory("/");
        return 0;

    case WM_SIZE:
        layout(w);
        return 0;

    case WM_NOTIFY: {
        const NMHDR *nm = (const NMHDR *)lp;
        if (nm->code == TVN_SELCHANGEDA) {
            HTREEITEM sel = (HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM,
                                                    TVGN_CARET, 0);
            char path[1024];
            if (sel) {
                path_of_item(sel, path, sizeof(path));
                show_directory(path);
            }
        } else if (nm->code == TVN_ITEMEXPANDEDA) {
            /* fill the level that was just opened, once */
            HTREEITEM sel = (HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM,
                                                    TVGN_CARET, 0);
            (void)sel;
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_CLOSE:
            DestroyWindow(w);
            return 0;
        case IDM_UP: {
            char *slash = strrchr(g_path, '/');
            if (slash && slash != g_path) {
                *slash = 0;
                show_directory(g_path);
            } else {
                show_directory("/");
            }
            return 0;
        }
#if HAVE(MESSAGEBOX)
        case IDM_ABOUT:
            MessageBoxA(w, "ween32 — a win32 for the rest of us.",
                        "About Windows", MB_OK);
            return 0;
#endif
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

int main(void)
{
    WNDCLASSA wc;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = explorer_proc;
    wc.lpszClassName = "ween32explorer";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.hIcon = (HICON)LoadImageA(NULL, "assets/icons/" ICON_APP ".ico",
                                 IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
#if HAVE(CURSORS)
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
#endif
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = panehead_proc;
    wc.lpszClassName = "explorerpane";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = splitter_proc;
    wc.lpszClassName = "explorersplit";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
#if HAVE(CURSORS)
    wc.hCursor = LoadCursorA(NULL, IDC_SIZEWE);
#endif
    RegisterClassA(&wc);

    g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    HWND w = CreateWindowExA(0, "ween32explorer", "All Users",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                 WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                                 WS_THICKFRAME | WS_VISIBLE,
                             40, 40, 654, 544, NULL, NULL, NULL, NULL);
    ShowWindow(w, SW_SHOWNORMAL);
    UpdateWindow(w);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (IsDialogMessageA(w, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
