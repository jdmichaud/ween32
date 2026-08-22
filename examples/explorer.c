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
#include <windowsx.h> /* GET_X_LPARAM, which win32 keeps in its own header */
#define HAVE(feature) 1
#else
#define HAVE(feature) WEEN32_HAS_##feature
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"

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
    ID_MENUBAR,
    ID_ADDRBAND,
    ID_BRAND,
    ID_GO,

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
    IDM_GO,
    IDM_CTX_EXPLORE,
    IDM_CTX_OPEN,
    IDM_CTX_PROPERTIES,
    IDM_CTX_REFRESH,
    IDM_CLOSE,
    IDM_ABOUT
};

/* the icons the shell shows, by their number in assets/icons */
#define ICON_FOLDER "4"
#define ICON_FOLDER_OPEN "5"
#define ICON_FILE "1"
#define ICON_COMPUTER "16"
#define ICON_DRIVE "9"
#define ICON_APP "46" /* the folder-and-magnifier the caption wears */

/* Image-list indices, in the order they are added: what the shell keeps as
 * icons first, then the toolbar's own images, which were never icons at all
 * — they came out of one bitmap strip — and so are drawn here. */
enum { IMG_FOLDER, IMG_FOLDER_OPEN, IMG_FILE, IMG_COMPUTER, IMG_DRIVE,
       IMG_BACK, IMG_FORWARD, IMG_UP, IMG_SEARCH, IMG_FOLDERS, IMG_HISTORY,
       IMG_MOVETO, IMG_COPYTO, IMG_DELETE, IMG_UNDO, IMG_VIEWS, IMG_GO,
       /* the shell's own, for the fixture: a file browser has no use for
        * them, but the machine's tree is full of them */
       IMG_SHELL_DESKTOP, IMG_SHELL_MYDOCS, IMG_SHELL_COMPUTER,
       IMG_SHELL_DISK, IMG_SHELL_CPANEL, IMG_SHELL_NETWORK, IMG_SHELL_BIN,
       IMG_SHELL_IE, IMG_SHELL_CFG, IMG_SHELL_BAT, IMG_SHELL_SYS,
       IMG_COUNT };

/* The toolbar's images, taken a pixel at a time off a Windows 2000 machine.
 * They were never icons — one bitmap strip held the lot — so there is nothing
 * to load them from, and each carries the palette it was drawn with and the
 * corner it sits in within its sixteen-pixel box. '.' is masked out; the
 * other characters index the palette. */
typedef struct {
    int w, h;                 /* the art */
    int ox, oy;               /* where it sits in the 16x16 image */
    const char *const *rows;
    const COLORREF *palette;
    int ncolours;
} glyph;

#define BACK_N 5
static const COLORREF BACK_PAL[BACK_N] = {
    RGB(0, 0, 0), RGB(134, 134, 134), RGB(221, 221, 221),
    RGB(248, 248, 248), RGB(192, 192, 192),
};
static const char *const GLYPH_BACK[] = {
    "....0.......",
    "...10.......",
    "..130.......",
    ".13201111110",
    "132222222210",
    "014411111110",
    ".01100000000",
    "..010.......",
    "...00.......",
    "....0.......",
};

#define FORWARD_N 2
static const COLORREF FORWARD_PAL[FORWARD_N] = {
    RGB(128, 128, 128), RGB(255, 255, 255),
};
static const char *const GLYPH_FORWARD[] = {
    ".......0.....",
    ".......00....",
    ".......000...",
    "00000000000..",
    "000000000000.",
    "0000000000001",
    "0000000000011",
    ".11111100011.",
    ".......0011..",
    ".......011...",
    "........1....",
};

#define UP_N 8
static const COLORREF UP_PAL[UP_N] = {
    RGB(255, 255, 255), RGB(134, 134, 134), RGB(4, 4, 4),
    RGB(204, 204, 204), RGB(221, 221, 221), RGB(178, 178, 178),
    RGB(77, 77, 77), RGB(227, 227, 227),
};
static const char *const GLYPH_UP[] = {
    "..11111........",
    ".1007352.......",
    "111111111111112",
    "100000000000002",
    "100006000434332",
    "100066604443352",
    "100666664433552",
    "100006444335352",
    "100006433355512",
    "100046666665512",
    "100444335355112",
    "104443355511112",
    "222222222222222",
};

#define SEARCH_N 14
static const COLORREF SEARCH_PAL[SEARCH_N] = {
    RGB(134, 134, 134), RGB(0, 0, 0), RGB(95, 95, 95),
    RGB(192, 192, 192), RGB(178, 178, 178), RGB(77, 77, 77),
    RGB(221, 221, 221), RGB(204, 204, 204), RGB(255, 255, 255),
    RGB(234, 234, 234), RGB(248, 248, 248), RGB(102, 102, 102),
    RGB(119, 119, 119), RGB(128, 128, 128),
};
static const char *const GLYPH_SEARCH[] = {
    ".....00000......",
    "...00a674520....",
    "..0a6345b4421...",
    ".0a755b3404021..",
    ".0045334040421..",
    "033b33455550021.",
    "033334507701d21.",
    "000345068860121.",
    "099245788663121.",
    "099725786663121.",
    ".0774506663011..",
    ".0044010330121..",
    "..0002311117021.",
    "...11222221c7021",
    ".....11111..c701",
    ".............11.",
};

#define FOLDERS_N 6
static const COLORREF FOLDERS_PAL[FOLDERS_N] = {
    RGB(255, 255, 255), RGB(134, 134, 134), RGB(4, 4, 4),
    RGB(221, 221, 221), RGB(178, 178, 178), RGB(204, 204, 204),
};
static const char *const GLYPH_FOLDERS[] = {
    ".1112..........",
    "1000111111.....",
    "10000000002....",
    "10000335542....",
    "10003355442....",
    "10033111242....",
    "10331000111111.",
    "103310000000002",
    "222210000335442",
    "....10003355412",
    "....10035554412",
    "....10355444112",
    "....10414141112",
    "....22222222222",
};

#define HISTORY_N 12
static const COLORREF HISTORY_PAL[HISTORY_N] = {
    RGB(0, 0, 0), RGB(248, 248, 248), RGB(134, 134, 134),
    RGB(215, 215, 215), RGB(150, 150, 150), RGB(95, 95, 95),
    RGB(77, 77, 77), RGB(119, 119, 119), RGB(192, 192, 192),
    RGB(234, 234, 234), RGB(255, 255, 255), RGB(128, 128, 128),
};
static const char *const GLYPH_HISTORY[] = {
    ".............2..",
    ".....222222.2a2.",
    "...223151132a60.",
    "..231115192a600.",
    ".235111192a6000.",
    ".21151192a60000.",
    "23111192a6005550",
    "2111119860577770",
    "2551193344444000",
    "2311933333444450",
    "2b89433388804470",
    ".27633868884060.",
    ".23778864446650.",
    "..033775566770..",
    "...0034444400...",
    ".....000000.....",
};

#define MOVETO_N 9
static const COLORREF MOVETO_PAL[MOVETO_N] = {
    RGB(255, 255, 255), RGB(4, 4, 4), RGB(134, 134, 134),
    RGB(204, 204, 204), RGB(178, 178, 178), RGB(221, 221, 221),
    RGB(95, 95, 95), RGB(51, 51, 51), RGB(192, 192, 192),
};
static const char *const GLYPH_MOVETO[] = {
    "222222..........",
    "2000021...111...",
    "20000201.....1..",
    "200001111..66711",
    "200055331...171.",
    "200053381....1..",
    "200552221.......",
    "20552000122222..",
    "205520000000001.",
    "233320055334341.",
    "111120553343441.",
    "....20533434421.",
    "....20334344241.",
    "....20343442421.",
    "....11111111111.",
};

#define COPYTO_N 10
static const COLORREF COPYTO_PAL[COPYTO_N] = {
    RGB(4, 4, 4), RGB(255, 255, 255), RGB(119, 119, 119),
    RGB(204, 204, 204), RGB(134, 134, 134), RGB(221, 221, 221),
    RGB(178, 178, 178), RGB(95, 95, 95), RGB(51, 51, 51),
    RGB(192, 192, 192),
};
static const char *const GLYPH_COPYTO[] = {
    "4444442.........",
    "4333330...000...",
    "432222220....0..",
    "432111130..77800",
    "432111130...080.",
    "432111130....0..",
    "432111530.......",
    "492115530.......",
    "002155532222....",
    "..233332111222..",
    "..0000021111110.",
    ".......21153660.",
    ".......21536640.",
    ".......21364440.",
    ".......00000000.",
};

#define DELETE_N 5
static const COLORREF DELETE_PAL[DELETE_N] = {
    RGB(134, 134, 134), RGB(4, 4, 4), RGB(204, 204, 204),
    RGB(77, 77, 77), RGB(255, 255, 255),
};
static const char *const GLYPH_DELETE[] = {
    "000.........3",
    "0421......01.",
    ".0021....01..",
    "...021..01...",
    "....00101....",
    ".....001.....",
    "....00101....",
    "...001..01...",
    "..021....01..",
    ".021......0..",
    "0401.......3.",
    "001..........",
    ".1..........3",
};

#define UNDO_N 7
static const COLORREF UNDO_PAL[UNDO_N] = {
    RGB(4, 4, 4), RGB(134, 134, 134), RGB(204, 204, 204),
    RGB(178, 178, 178), RGB(255, 255, 255), RGB(227, 227, 227),
    RGB(192, 192, 192),
};
static const char *const GLYPH_UNDO[] = {
    ".......11111...",
    ".....11544520..",
    "1...1423000320.",
    "10.14300...0630",
    "140430......120",
    "14420.......120",
    "15220.......120",
    "133330.....1230",
    "0000000....120.",
    "...........000.",
};

#define VIEWS_N 7
static const COLORREF VIEWS_PAL[VIEWS_N] = {
    RGB(255, 255, 255), RGB(4, 4, 4), RGB(134, 134, 134),
    RGB(204, 204, 204), RGB(160, 160, 164), RGB(95, 95, 95),
    RGB(221, 221, 221),
};
static const char *const GLYPH_VIEWS[] = {
    "2222222222222221",
    "2644444444444441",
    "2555555555555551",
    "2000000000000031",
    "2041011004101131",
    "2011000001100031",
    "2000000000000031",
    "2041011004101131",
    "2011000001100031",
    "2000000000000031",
    "2041011004101131",
    "2011000001100031",
    "2333333333333331",
    "1111111111111111",
};

#define FOLDERS_HOT_N 7
static const COLORREF FOLDERS_HOT_PAL[FOLDERS_HOT_N] = {
    RGB(255, 255, 153), RGB(255, 204, 0), RGB(102, 102, 0),
    RGB(4, 4, 4), RGB(204, 153, 0), RGB(255, 255, 255),
    RGB(204, 204, 153),
};
static const char *const GLYPH_FOLDERS_HOT[] = {
    ".2222..........",
    "2000622222.....",
    "20000000003....",
    "20151011143....",
    "20510111443....",
    "20101222243....",
    "20012000622222.",
    "201120000000003",
    "333320151011143",
    "....20510111143",
    "....20101111443",
    "....20011114143",
    "....20111141443",
    "....33333333333",
};

#define GO_N 8
static const COLORREF GO_PAL[GO_N] = {
    RGB(0, 0, 0), RGB(134, 134, 134), RGB(248, 248, 248),
    RGB(221, 221, 221), RGB(178, 178, 178), RGB(192, 192, 192),
    RGB(204, 204, 204), RGB(150, 150, 150),
};
static const char *const GLYPH_GO[] = {
    ".......1.....",
    ".......10....",
    ".......120...",
    ".....111230..",
    "...112223550.",
    "..12233355410",
    ".12361144410.",
    ".1340000410..",
    "1260...010...",
    "130....00....",
    "160....0.....",
    "150..........",
    "170..........",
    ".070.........",
    "..000........",
};

/* The Back arrow again, in the colours it wears under the pointer: a
 * toolbar had two sets of images and swapped to the second for whichever
 * button the pointer was on. */
#define BACK_HOT_N 5
static const COLORREF BACK_HOT_PAL[BACK_HOT_N] = {
    RGB(0, 0, 0), RGB(51, 153, 255), RGB(153, 255, 255),
    RGB(51, 102, 255), RGB(51, 204, 255),
};
static const char *const GLYPH_BACK_HOT[] = {
    "....0.......",
    "...30.......",
    "..320.......",
    ".32203333330",
    "322222222210",
    "014111111110",
    ".01100000000",
    "..010.......",
    "...00.......",
    "....0.......",
};

/* Indexed by IMG_BACK.. — the order add_glyph is called in. */
static const glyph GLYPHS[] = {
    { 12, 10, 2, 3, GLYPH_BACK, BACK_PAL, BACK_N },
    { 13, 11, 1, 3, GLYPH_FORWARD, FORWARD_PAL, FORWARD_N },
    { 15, 13, 0, 2, GLYPH_UP, UP_PAL, UP_N },
    { 16, 16, 0, 0, GLYPH_SEARCH, SEARCH_PAL, SEARCH_N },
    { 15, 14, 0, 1, GLYPH_FOLDERS, FOLDERS_PAL, FOLDERS_N },
    { 16, 16, 0, 0, GLYPH_HISTORY, HISTORY_PAL, HISTORY_N },
    { 16, 15, 0, 0, GLYPH_MOVETO, MOVETO_PAL, MOVETO_N },
    { 16, 15, 0, 0, GLYPH_COPYTO, COPYTO_PAL, COPYTO_N },
    { 13, 13, 1, 2, GLYPH_DELETE, DELETE_PAL, DELETE_N },
    { 15, 10, 0, 3, GLYPH_UNDO, UNDO_PAL, UNDO_N },
    { 16, 14, 0, 1, GLYPH_VIEWS, VIEWS_PAL, VIEWS_N },
    { 13, 15, 3, 0, GLYPH_GO, GO_PAL, GO_N },
};

/* The same set for the hot list, differing only where a glyph has a
 * hot drawing of its own. */
static const glyph GLYPHS_HOT[] = {
    { 12, 10, 2, 3, GLYPH_BACK_HOT, BACK_HOT_PAL, BACK_HOT_N },
    { 13, 11, 1, 3, GLYPH_FORWARD, FORWARD_PAL, FORWARD_N },
    { 15, 13, 0, 2, GLYPH_UP, UP_PAL, UP_N },
    { 16, 16, 0, 0, GLYPH_SEARCH, SEARCH_PAL, SEARCH_N },
    { 15, 14, 0, 1, GLYPH_FOLDERS_HOT, FOLDERS_HOT_PAL, FOLDERS_HOT_N },
    { 16, 16, 0, 0, GLYPH_HISTORY, HISTORY_PAL, HISTORY_N },
    { 16, 15, 0, 0, GLYPH_MOVETO, MOVETO_PAL, MOVETO_N },
    { 16, 15, 0, 0, GLYPH_COPYTO, COPYTO_PAL, COPYTO_N },
    { 13, 13, 1, 2, GLYPH_DELETE, DELETE_PAL, DELETE_N },
    { 15, 10, 0, 3, GLYPH_UNDO, UNDO_PAL, UNDO_N },
    { 16, 14, 0, 1, GLYPH_VIEWS, VIEWS_PAL, VIEWS_N },
    { 13, 15, 3, 0, GLYPH_GO, GO_PAL, GO_N },
};

static COLORREF glyph_colour(const glyph *g, char c)
{
    static const char set[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char *at = strchr(set, c);
    int i = at && c ? (int)(at - set) : -1;
    if (i < 0 || i >= g->ncolours)
        return RGB(255, 0, 255); /* masked out */
    return g->palette[i];
}

static HWND g_main, g_tree, g_list, g_toolbar, g_rebar, g_address, g_status;
static HWND g_menubar, g_addrband;
/* The brand stands a row taller than the band beside it, and its edge
 * reaches over the rule beneath: see brand_proc. */
#define BRAND_W 40
#define BRAND_H 23
static HWND g_brand;
static HWND g_split, g_panehead;
static HIMAGELIST g_images, g_hot_images;
static HFONT g_font;
static int g_split_x = 203; /* the tree pane's width, measured off the shot */
static int g_folders = 1;   /* whether the tree pane is shown at all */
static int g_crossing;      /* one pane clearing the other's selection */
static int g_dragging;
static char g_path[1024] = "/";

/* The directory the list is showing, kept rather than re-read: sorting it is
 * a matter of ordering what is already here, and opening a row means knowing
 * which entry that row was. */
static fs_entry *g_entry;
static int g_entries;
static int g_ctx_row = -1;   /* the row a context menu was asked about */
static HTREEITEM g_ctx_item; /* or the tree item, when it came from there */
static int g_sort_col;  /* the column the list is ordered by */
static int g_sort_down; /* and whether that order is reversed */

/* ---- geometry ------------------------------------------------------------ */

#define PANE_HEAD_H 20  /* the "Folders" bar above the tree */
#define PANE_INSET 2    /* the pane frame's edge to what is inside it */
#define SPLIT_W 4
#define STATUS_H 20

static int rebar_height(void)
{
    return g_rebar ? (int)SendMessageA(g_rebar, RB_GETBARHEIGHT, 0, 0) : 0;
}

/* The tree pane's width, held between something usable and what is left of
 * the window. Both the layout and the frame drawn round it ask for it here. */
static int pane_width(const RECT *cr)
{
    int w = g_split_x;
    if (w < 60)
        w = 60;
    if (w > cr->right - 120)
        w = cr->right - 120;
    return w;
}

/* The frame drawn round the "Folders" bar and the tree, in client
 * coordinates: three pixels below the rebar, down to the status bar, and as
 * wide as the pane less the splitter's own strip. */
static void pane_frame(const RECT *cr, RECT *out)
{
    out->left = 0;
    out->top = rebar_height() + 3;
    out->right = pane_width(cr) - 3;
    out->bottom = cr->bottom - STATUS_H;
}

static void layout(HWND w)
{
    RECT cr;
    int top, bottom, left_w;
    GetClientRect(w, &cr);
    top = rebar_height();
    bottom = cr.bottom - STATUS_H;
    left_w = pane_width(&cr);

    if (g_rebar) {
        MoveWindow(g_rebar, 0, 0, cr.right, top, TRUE);
        if (g_brand) { /* against the rebar's right edge */
            RECT rr;
            GetClientRect(g_rebar, &rr);
            MoveWindow(g_brand, rr.right - 2 - BRAND_W, 2, BRAND_W, BRAND_H,
                       TRUE);
        }
    }
    /* The pane is a frame of its own: an etched rectangle three pixels below
     * the rebar and three above the status bar, with the "Folders" bar and
     * the tree inside it and a rule between them. The frame is drawn by this
     * window — see WM_PAINT — and what goes in it is inset two pixels from
     * its edge, which is where the machine has all of it. */
    if (g_panehead)
        ShowWindow(g_panehead, g_folders ? SW_SHOW : SW_HIDE);
    if (g_tree)
        ShowWindow(g_tree, g_folders ? SW_SHOW : SW_HIDE);
    if (g_split)
        ShowWindow(g_split, g_folders ? SW_SHOW : SW_HIDE);
    if (!g_folders) { /* the list has the whole width to itself */
        if (g_list)
            MoveWindow(g_list, 0, top + 3, cr.right, bottom - top - 3, TRUE);
    } else {
        RECT fr;
        int in_x, in_w, in_y;
        pane_frame(&cr, &fr);
        in_x = fr.left + PANE_INSET;
        in_w = fr.right - fr.left - 2 * PANE_INSET;
        in_y = fr.top + PANE_INSET;
        if (g_panehead)
            MoveWindow(g_panehead, in_x, in_y, in_w, PANE_HEAD_H, TRUE);
        if (g_tree) /* the rule between them takes the row after the bar */
            MoveWindow(g_tree, in_x, in_y + PANE_HEAD_H + 1, in_w,
                       fr.bottom - PANE_INSET - (in_y + PANE_HEAD_H + 1), TRUE);
        if (g_split)
            MoveWindow(g_split, left_w - 3, top + 3, SPLIT_W,
                       bottom - top - 3, TRUE);
        if (g_list)
            MoveWindow(g_list, left_w + 1, top + 3, cr.right - left_w - 1,
                       bottom - top - 3, TRUE);
    }
    /* The strip between the panes is what is left between the tree's frame
     * and the list's edge — four pixels of face on the machine, with the
     * list starting one past where the splitter is taken to be. */
    if (g_status) {
        /* The two right-hand parts keep a fixed width and the first takes
         * whatever is left, so the counts stay put as the window widens. */
        int parts[3];
        MoveWindow(g_status, 0, bottom, cr.right, STATUS_H, TRUE);
        parts[0] = cr.right - 233;
        parts[1] = cr.right - 155;
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

/* Case-insensitive, which is how a shell orders names. */
static int name_cmp(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        int cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

/* Folders before files whatever the column, which is what the shell does,
 * and then by the column that was clicked. */
static int entry_cmp(const void *pa, const void *pb)
{
    const fs_entry *a = pa, *b = pb;
    int r = 0;
    if (a->is_dir != b->is_dir)
        return a->is_dir ? -1 : 1;
    switch (g_sort_col) {
    case 1: /* size */
        r = a->size < b->size ? -1 : (a->size > b->size ? 1 : 0);
        break;
    case 2: /* type */
        r = name_cmp(type_of(a), type_of(b));
        break;
    case 3: /* modified, which is a string in a form that does not sort */
        r = name_cmp(a->modified, b->modified);
        break;
    default:
        break;
    }
    if (r == 0)
        r = name_cmp(a->name, b->name);
    return g_sort_down ? -r : r;
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

/* The address bar: the path opened out, one item per level, each indented a
 * step further than the one above and wearing the icon for what it is. The
 * one you are in is the one selected. */
static void fill_address(const char *path)
{
    COMBOBOXEXITEMA ci;
    const char *p = path;
    int level = 0;

    SendMessageA(g_address, CB_RESETCONTENT, 0, 0);
    memset(&ci, 0, sizeof(ci));
    ci.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_INDENT;
    ci.iItem = -1;
    ci.pszText = (char *)"/";
    ci.iImage = IMG_COMPUTER;
    ci.iIndent = 0;
    SendMessageA(g_address, CBEM_INSERTITEMA, 0, (LPARAM)&ci);

    while (*p) {
        char name[260];
        size_t n = 0;
        while (*p == '/')
            p++;
        while (p[n] && p[n] != '/' && n < sizeof(name) - 1)
            n++;
        if (!n)
            break;
        memcpy(name, p, n);
        name[n] = 0;
        p += n;
        ci.pszText = name;
        ci.iImage = IMG_FOLDER;
        ci.iIndent = ++level;
        SendMessageA(g_address, CBEM_INSERTITEMA, 0, (LPARAM)&ci);
    }
    SendMessageA(g_address, CB_SETCURSEL, (WPARAM)level, 0);
}

/* What the status bar says about the whole directory, which is what it says
 * whenever nothing in it is picked out. */
/* ---- the fixture ----------------------------------------------------------
 *
 * WEEN32_EXPLORER_FIXTURE=1 fills both panes with what a Windows 2000
 * explorer shows sitting on Local Disk (C:), so this window can be put beside
 * a screenshot of that machine and the two counted pixel for pixel. Nothing
 * else in the example knows about it: the tree and the list come from a table
 * instead of from the file system, and that is the whole of the difference.
 * Without it the example is an ordinary file browser, which is the point.
 */
static int g_fixture;

static const struct {
    int depth;
    const char *name;
    int image;
    int children; /* 0 none, 1 a box that opens it, 2 already open */
} g_fix_tree[] = {
    { 0, "Desktop", IMG_SHELL_DESKTOP, 2 },
    { 1, "My Documents", IMG_SHELL_MYDOCS, 1 },
    { 1, "My Computer", IMG_SHELL_COMPUTER, 2 },
    { 2, "Local Disk (C:)", IMG_SHELL_DISK, 2 },
    { 3, "Documents and Settings", IMG_FOLDER, 1 },
    { 3, "Program Files", IMG_FOLDER, 1 },
    { 3, "WINNT", IMG_FOLDER, 1 },
    { 2, "Control Panel", IMG_SHELL_CPANEL, 1 },
    { 1, "My Network Places", IMG_SHELL_NETWORK, 1 },
    { 1, "Recycle Bin", IMG_SHELL_BIN, 0 },
    { 1, "Internet Explorer", IMG_SHELL_IE, 0 },
};

static const struct {
    const char *name;
    const char *size;
    const char *type;
    const char *modified;
    int is_dir;
    int image;  /* -1 for the folder or the plain file icon */
    int hidden; /* drawn ghosted, the way the shell draws a hidden file */
} g_fix_list[] = {
    { "Documents and Settings", "", "File Folder", "7/8/2017 6:26 PM", 1, -1,
      0 },
    { "Program Files", "", "File Folder", "7/8/2017 6:27 PM", 1, -1, 0 },
    { "WINNT", "", "File Folder", "7/8/2017 6:26 PM", 1, -1, 0 },
    /* the two hidden ones are drawn ghosted, as the shell draws a hidden
     * file — which is what the machine has here */
    { "AUTOEXEC", "0 KB", "MS-DOS Batch File", "7/8/2017 6:33 PM", 0,
      IMG_SHELL_BAT, 1 },
    { "boot", "1 KB", "Configuration Settings", "7/22/2017 7:37 PM", 0,
      IMG_SHELL_CFG, 0 },
    { "CONFIG.SYS", "0 KB", "System file", "7/8/2017 6:33 PM", 0,
      IMG_SHELL_SYS, 1 },
};

static void status_for_directory(void)
{
    unsigned long bytes = 0;
    char line[256];
    if (g_fixture) { /* what the machine says about this folder */
        SendMessageA(g_status, SB_SETTEXTA, 0,
                     (LPARAM) "6 object(s) (Disk free space: 499 MB)");
        SendMessageA(g_status, SB_SETTEXTA, 1, (LPARAM) "203 bytes");
        return;
    }
    for (int i = 0; i < g_entries; i++)
        if (!g_entry[i].is_dir)
            bytes += g_entry[i].size;
    snprintf(line, sizeof(line), "%d object(s)", g_entries);
    SendMessageA(g_status, SB_SETTEXTA, 0, (LPARAM)line);
    snprintf(line, sizeof(line), "%lu bytes", bytes);
    SendMessageA(g_status, SB_SETTEXTA, 1, (LPARAM)line);
}

/* And what it says about one of them. */
static void status_for_selection(int row)
{
    char line[256];
    if (g_fixture) {
        int n = (int)(sizeof(g_fix_list) / sizeof(*g_fix_list));
        if (row < 0 || row >= n) {
            status_for_directory();
            return;
        }
        SendMessageA(g_status, SB_SETTEXTA, 0, (LPARAM) "1 object(s) selected");
        SendMessageA(g_status, SB_SETTEXTA, 1,
                     (LPARAM)(g_fix_list[row].is_dir ? "" : g_fix_list[row].size));
        return;
    }
    if (row < 0 || row >= g_entries) {
        status_for_directory();
        return;
    }
    snprintf(line, sizeof(line), "Type: %s", type_of(&g_entry[row]));
    SendMessageA(g_status, SB_SETTEXTA, 0, (LPARAM)line);
    if (g_entry[row].is_dir)
        SendMessageA(g_status, SB_SETTEXTA, 1, (LPARAM)"");
    else {
        snprintf(line, sizeof(line), "%lu bytes", g_entry[row].size);
        SendMessageA(g_status, SB_SETTEXTA, 1, (LPARAM)line);
    }
}

/* A folder opens with the caret on its first item and nothing selected, which
 * is what the shell does and why an arrow key straight after opening one moves
 * from the top rather than from nowhere. */
static void focus_first_row(void)
{
    LVITEMA it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_STATE;
    it.state = LVIS_FOCUSED;
    it.stateMask = LVIS_FOCUSED;
    SendMessageA(g_list, LVM_SETITEMSTATE, 0, (LPARAM)&it);
}

/* The arrow in the heading of the column the view is ordered by, and none in
 * the others. It is the column's format the list view passes to its header,
 * which is how a win32 application asks for one. */
static void mark_sorted_column(void)
{
    for (int c = 0; c < 4; c++) {
        LVCOLUMNA col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_FMT;
        col.fmt = (c == 1) ? LVCFMT_RIGHT : LVCFMT_LEFT;
        if (c == g_sort_col)
            col.fmt |= g_sort_down ? HDF_SORTDOWN : HDF_SORTUP;
        SendMessageA(g_list, LVM_SETCOLUMNA, (WPARAM)c, (LPARAM)&col);
    }
}

/* Put what was read into the list, in whatever order the columns are in. */
static void fill_list(void)
{
    SendMessageA(g_list, LVM_DELETEALLITEMS, 0, 0);
    if (g_entries > 1)
        qsort(g_entry, (size_t)g_entries, sizeof(*g_entry), entry_cmp);
    for (int row = 0; row < g_entries; row++) {
        const fs_entry *e = &g_entry[row];
        LVITEMA it;
        char size[32];
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_IMAGE;
        it.iItem = row;
        it.pszText = (char *)e->name;
        it.iImage = e->is_dir ? IMG_FOLDER : IMG_FILE;
        SendMessageA(g_list, LVM_INSERTITEMA, 0, (LPARAM)&it);
        if (e->is_dir) {
            set_cell(g_list, row, 1, "");
        } else {
            snprintf(size, sizeof(size), "%lu KB", (e->size + 1023) / 1024);
            set_cell(g_list, row, 1, size);
        }
        set_cell(g_list, row, 2, type_of(e));
        set_cell(g_list, row, 3, e->modified);
    }
    focus_first_row();
    mark_sorted_column();
    status_for_directory();
}


static void fill_fixture_list(void)
{
    int n = (int)(sizeof(g_fix_list) / sizeof(*g_fix_list));
    SendMessageA(g_list, LVM_DELETEALLITEMS, 0, 0);
    for (int row = 0; row < n; row++) {
        LVITEMA it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_IMAGE;
        it.iItem = row;
        it.pszText = (char *)g_fix_list[row].name;
        it.iImage = g_fix_list[row].image >= 0
                        ? g_fix_list[row].image
                        : (g_fix_list[row].is_dir ? IMG_FOLDER : IMG_FILE);
        SendMessageA(g_list, LVM_INSERTITEMA, 0, (LPARAM)&it);
        set_cell(g_list, row, 1, g_fix_list[row].size);
        set_cell(g_list, row, 2, g_fix_list[row].type);
        set_cell(g_list, row, 3, g_fix_list[row].modified);
        if (g_fix_list[row].hidden) {
            LVITEMA st;
            memset(&st, 0, sizeof(st));
            st.mask = LVIF_STATE;
            st.state = LVIS_CUT;
            st.stateMask = LVIS_CUT;
            SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)row, (LPARAM)&st);
        }
    }
    focus_first_row();
    mark_sorted_column();
    SendMessageA(g_status, SB_SETTEXTA, 0,
                 (LPARAM) "6 object(s) (Disk free space: 499 MB)");
    SendMessageA(g_status, SB_SETTEXTA, 1, (LPARAM) "203 bytes");
    SendMessageA(g_status, SB_SETTEXTA, 2, (LPARAM) "My Computer");
    SetWindowTextA(g_main, "Local Disk (C:)");
}

/* Fill the list from a directory, and say in the status bar what is in it. */
static void show_directory(const char *path)
{
    if (g_fixture) {
        COMBOBOXEXITEMA ci;
        fill_fixture_list();
        /* the address bar has the folder in it, as the machine's has */
        SendMessageA(g_address, CB_RESETCONTENT, 0, 0);
        memset(&ci, 0, sizeof(ci));
        ci.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE;
        ci.iItem = 0;
        ci.pszText = (char *)"Local Disk (C:)";
        ci.iImage = IMG_DRIVE;
        ci.iSelectedImage = IMG_DRIVE;
        SendMessageA(g_address, CBEM_INSERTITEMA, 0, (LPARAM)&ci);
        SendMessageA(g_address, CB_SETCURSEL, 0, 0);
        return;
    }

    fs_dir d;
    fs_entry e;
    int cap = 0;

    SendMessageA(g_list, LVM_DELETEALLITEMS, 0, 0);
    g_entries = 0;
    if (!fs_open(&d, path)) {
        SendMessageA(g_status, SB_SETTEXTA, 0, (LPARAM) "Access denied");
        SendMessageA(g_status, SB_SETTEXTA, 1, (LPARAM) "");
        return;
    }
    while (fs_next(&d, &e)) {
        if (e.name[0] == '.') /* the shell hides these, and so do we */
            continue;
        if (g_entries == cap) {
            int grown = cap ? cap * 2 : 64;
            fs_entry *bigger = realloc(g_entry, (size_t)grown * sizeof(*bigger));
            if (!bigger)
                break;
            g_entry = bigger;
            cap = grown;
        }
        g_entry[g_entries++] = e;
    }
    fs_close(&d);
    fill_list();
    SendMessageA(g_status, SB_SETTEXTA, 2, (LPARAM) "My Computer");

    strncpy(g_path, path, sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = 0;

    /* The address bar shows the way down to where you are, a step in for
     * each level — which is what the shell's does, only walking its own
     * namespace rather than the file system. The caption gets the folder's
     * own name, which is also what the shell puts there. */
    fill_address(path);
    {
        const char *leaf = strrchr(path, '/');
        SetWindowTextA(g_main, leaf && leaf[1] ? leaf + 1 : path);
    }
}

/* ---- the folder tree ------------------------------------------------------ */

static HTREEITEM add_node(HTREEITEM parent, const char *text, int image,
                          int sel_image, int has_children)
{
    TVINSERTSTRUCTA is;
    memset(&is, 0, sizeof(is));
    is.hParent = parent ? parent : TVI_ROOT;
    /* cChildren claims children the item does not have yet, so the box to
     * open it with is there before anything has been read off the disk. What
     * is behind it is read when it is opened, and not before: walking every
     * directory on the machine to draw one tree would not do. */
    is.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
    is.item.pszText = (char *)text;
    is.item.iImage = image;
    is.item.iSelectedImage = sel_image;
    is.item.cChildren = has_children;
    return (HTREEITEM)SendMessageA(g_tree, TVM_INSERTITEMA, 0, (LPARAM)&is);
}

static void fill_fixture_tree(void)
{
    int n = (int)(sizeof(g_fix_tree) / sizeof(*g_fix_tree));
    HTREEITEM item[32], stack[8];
    memset(stack, 0, sizeof(stack));
    for (int i = 0; i < n && i < 32; i++) {
        int d = g_fix_tree[i].depth;
        item[i] = add_node(d ? stack[d - 1] : NULL, g_fix_tree[i].name,
                           g_fix_tree[i].image, g_fix_tree[i].image,
                           g_fix_tree[i].children != 0);
        stack[d] = item[i];
    }
    /* opened afterwards, parents first, so each has its children by then */
    for (int i = 0; i < n && i < 32; i++)
        if (g_fix_tree[i].children == 2)
            SendMessageA(g_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)item[i]);
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

/* Whether a folder has any folder in it, which is what decides the box to
 * open it with. The shell looks — a folder with nothing under it gets no
 * plus, and the only way to know is to read it. It stops at the first one
 * found, so the usual case costs one directory entry. */
static int has_subdir(const char *path)
{
    fs_dir d;
    fs_entry e;
    int found = 0;
    if (!fs_open(&d, path))
        return 0;
    while (!found && fs_next(&d, &e))
        if (e.is_dir && e.name[0] != '.')
            found = 1;
    fs_close(&d);
    return found;
}

/* One level of the tree, filled when its parent is opened — once. */
static void fill_children(HTREEITEM parent, const char *path)
{
    fs_dir d;
    fs_entry e;
    if (SendMessageA(g_tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)parent))
        return; /* already read */
    if (!fs_open(&d, path))
        return;
    while (fs_next(&d, &e)) {
        char child[1400];
        if (!e.is_dir || e.name[0] == '.')
            continue;
        snprintf(child, sizeof(child), "%s%s%s", path,
                 strcmp(path, "/") ? "/" : "", e.name);
        add_node(parent, e.name, IMG_FOLDER, IMG_FOLDER_OPEN,
                 has_subdir(child));
    }
    fs_close(&d);
}

/* ---- the menus a right click brings up ------------------------------------
 *
 * One for a folder and one for a file, built the way the shell builds them:
 * what to open it with first and in bold, then the rest in groups, and Send
 * To carrying a submenu. They are made once and kept, because a context menu
 * is the same menu wherever it is asked for — only what it acts on changes.
 */
static HMENU g_folder_menu, g_file_menu, g_back_menu, g_col_menu;

/* Send To is a menu with pictures in it, so here are its four, taken the same
 * way as the toolbar's and in the same form. Nothing is masked out: a menu
 * bitmap is blitted whole, and '.' is the menu's own colour. */
static const char *const send_floppy_rows[] = {
    ".........000000.",
    "........12222223",
    "........14444433",
    "........14445553",
    "........15555553",
    "........16768783",
    "..00000017000073",
    ".077777716131483",
    "044444444333333.",
    "077777777797013.",
    "077700007777013.",
    "070033330007013.",
    "077744447777013.",
    "00000000000003..",
    ".333333333333...",
    "................",
};
static const COLORREF send_floppy_palette[] = {
    RGB(132, 132, 132), RGB(115, 115, 115), RGB(156, 255, 255),
    RGB(  0,   0,   0), RGB(255, 255, 255), RGB(206, 255, 255),
    RGB(206, 206, 206), RGB(198, 198, 198), RGB(181, 181, 181),
    RGB(255,   0,   0),
};

static const char *const send_desktop_rows[] = {
    "............01..",
    "...........234..",
    "5555677777289a7a",
    "5b56cdded2f9a56a",
    "556cddee289agb6a",
    "56cdhhh2f9a7eg6a",
    "6cddhd289a7eccga",
    "7ddehdi9aaece7ha",
    "7deehdadeace7cha",
    "7eeehdddeaecc7ha",
    "7eeehdddeacc7cha",
    "5eeeheeeeac7c7ga",
    "55eeaaaaaa7c7gga",
    "5b5eececc7c7gbga",
    "6665hhhhhhhgggga",
    "aaaaaaaaaaaaaaaa",
};
static const COLORREF send_desktop_palette[] = {
    RGB(255,  82,  82), RGB(206,  49,   0), RGB(206, 156,   0),
    RGB(255, 156, 206), RGB(156,   0,   0), RGB(  0, 156, 206),
    RGB(  0,  99, 156), RGB(173, 173, 148), RGB(255, 206,   0),
    RGB(156,  99,   0), RGB(  0,   0,   0), RGB( 99, 206, 255),
    RGB(206, 206, 156), RGB(255, 255, 255), RGB(255, 255, 206),
    RGB(255, 255, 156), RGB( 49,  99, 156), RGB(148, 148, 148),
    RGB(156, 156,  99),
};

static const char *const send_mail_rows[] = {
    "................",
    "................",
    ".0000000000000..",
    ".01232323232101.",
    ".02124242421201.",
    ".03212424212401.",
    ".02421242124201.",
    ".03212121212401.",
    ".02124212421201.",
    ".01242424242101.",
    ".02424242424201.",
    ".00000000000001.",
    "..1111111111111.",
    "................",
    "................",
    "................",
};
static const COLORREF send_mail_palette[] = {
    RGB(132, 132, 132), RGB(  0,   0,   0), RGB(255, 255,   0),
    RGB(255, 255, 255), RGB(198, 198, 198),
};

static const char *const send_docs_rows[] = {
    "........0.......",
    ".......012......",
    "...33301452.....",
    "..3660147152....",
    ".366014894152aa.",
    ".36014b94b41526c",
    ".3015de5de55552c",
    "aaaaaaaaaaaae152",
    "a4ffffffgfg6c552",
    "a4ffffgffgf6ce2c",
    ".a4fffffgfggacac",
    ".a4ffgfgfggg6cac",
    "..a4gfgfgggg6acc",
    "..aaaaaaaaaaaacc",
    "...ccccccccccccc",
    "................",
};
static const COLORREF send_docs_palette[] = {
    RGB( 99,  99, 156), RGB(206, 255, 255), RGB(  0,   0,   0),
    RGB(132, 132,   0), RGB(255, 255, 255), RGB(206, 206, 255),
    RGB(206, 206,  99), RGB( 49, 156, 255), RGB(  0,  99, 255),
    RGB(156, 255, 255), RGB(156, 156,   0), RGB( 49,  49, 206),
    RGB( 49,  49,   0), RGB( 49,   0, 156), RGB(156, 156, 255),
    RGB(255, 255, 156), RGB(255, 206, 156),
};

/* the four, in the order the shell lists them */
static const glyph g_send_icons[] = {
    { 16, 16, 0, 0, send_floppy_rows, send_floppy_palette,
      (int)(sizeof(send_floppy_palette) / sizeof(*send_floppy_palette)) },
    { 16, 16, 0, 0, send_desktop_rows, send_desktop_palette,
      (int)(sizeof(send_desktop_palette) / sizeof(*send_desktop_palette)) },
    { 16, 16, 0, 0, send_mail_rows, send_mail_palette,
      (int)(sizeof(send_mail_palette) / sizeof(*send_mail_palette)) },
    { 16, 16, 0, 0, send_docs_rows, send_docs_palette,
      (int)(sizeof(send_docs_palette) / sizeof(*send_docs_palette)) },
};

/* My Computer as the shell draws it: not one of the icons in
 * assets/icons, so taken a pixel at a time the way the toolbar's were.
 * From the status bar's copy, which the machine draws straight; the
 * tree's goes through a sixteen bit image list and comes out coarser.
 * What is transparent is what did not change between the two. */
static const char *const shell_computer_rows[] = {
    ".0111111111123..",
    "456665555577689.",
    "48a51bccccccdef.",
    "4g8abhiiijjklh9.",
    "4m87biiihinnoi9.",
    "48m7phiihhnnoq9.",
    "4mm6pjihhjjros9.",
    "4m86knnnnnkrls9.",
    "ttm5joooooooo6u.",
    ".ttv8w555555w1x.",
    "tyzAAAAAAAAABCu.",
    "t4DEFG4HvvGx4F..",
    "t3IJD8DK85vLv8vF",
    "t3348D8LLL81II8M",
    ".t3MD8D85858D8DM",
    "..HMMMMMMMMMMMMM",
};
static const COLORREF shell_computer_palette[] = {
    RGB(134, 134, 134), RGB(153, 153, 204), RGB(153, 153, 102),
    RGB(102, 102, 153), RGB( 57,  57,  57), RGB(255, 255, 204),
    RGB(255, 255, 255), RGB(255, 251, 240), RGB(204, 204, 255),
    RGB( 66,  66,  66), RGB(241, 241, 241), RGB(102, 102, 204),
    RGB(  0,   0,  51), RGB(  0,  51, 102), RGB(204, 236, 255),
    RGB( 77,  77,  77), RGB(153, 204, 255), RGB(102, 255, 255),
    RGB(153, 255, 255), RGB(102, 204, 255), RGB( 51, 153, 255),
    RGB(  0, 102, 255), RGB(178, 178, 178), RGB( 51, 204, 255),
    RGB( 51, 102, 204), RGB(102, 153, 204), RGB(204, 255, 255),
    RGB(  0, 153, 255), RGB(248, 248, 248), RGB( 51,  51,   0),
    RGB( 41,  41,  41), RGB(173, 169, 144), RGB(221, 221, 221),
    RGB( 22,  22,  22), RGB(204, 153, 153), RGB( 12,  12,  12),
    RGB(  0,   0,   0), RGB(  8,   8,   8), RGB( 17,  17,  17),
    RGB(239, 214, 198), RGB(204, 153, 204), RGB(102, 102, 102),
    RGB(102,  51,  51), RGB(102, 102,  51), RGB(128, 128,   0),
    RGB(192, 192, 192), RGB(234, 234, 234), RGB(231, 231, 214),
    RGB( 34,  34,  34),
};

static const glyph g_shell_computer = {
    16, 16, 0, 0, shell_computer_rows, shell_computer_palette,
    (int)(sizeof(shell_computer_palette) / sizeof(*shell_computer_palette))
};

/* The same art as an icon, for the status bar: the AND mask says what shows
 * through, the colours go under it. */
static HICON glyph_icon(const glyph *g)
{
    unsigned char bits[16 * 16 * 4];
    unsigned char mask[16 * 2];
    memset(bits, 0, sizeof(bits));
    memset(mask, 0xff, sizeof(mask)); /* everything through until it is drawn */
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            unsigned char *p = bits + (((size_t)(y + g->oy) * 16) + x + g->ox) * 4;
            COLORREF c;
            if (g->rows[y][x] == '.')
                continue;
            c = glyph_colour(g, g->rows[y][x]);
            p[0] = (unsigned char)(c >> 16);
            p[1] = (unsigned char)(c >> 8);
            p[2] = (unsigned char)c;
            mask[(y + g->oy) * 2 + (x + g->ox) / 8] &=
                (unsigned char)~(0x80 >> ((x + g->ox) % 8));
        }
    }
    return CreateIcon(NULL, 16, 16, 1, 32, mask, bits);
}

/* One of them as a bitmap the size Windows expects, background and all. */
static HBITMAP menu_bitmap(const glyph *g)
{
    unsigned char bits[16 * 16 * 4];
    COLORREF back = GetSysColor(COLOR_MENU);

    for (int i = 0; i < 16 * 16; i++) {
        bits[i * 4 + 0] = (unsigned char)(back >> 16); /* B,G,R, as a DIB is */
        bits[i * 4 + 1] = (unsigned char)(back >> 8);
        bits[i * 4 + 2] = (unsigned char)back;
        bits[i * 4 + 3] = 0;
    }
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            unsigned char *p = bits + (((size_t)(y + g->oy) * 16) + x + g->ox) * 4;
            COLORREF c;
            if (g->rows[y][x] == '.')
                continue;
            c = glyph_colour(g, g->rows[y][x]);
            p[0] = (unsigned char)(c >> 16);
            p[1] = (unsigned char)(c >> 8);
            p[2] = (unsigned char)c;
        }
    }
    return CreateBitmap(16, 16, 1, 32, bits);
}

static HMENU build_send_to(void)
{
    HMENU m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, 0, "3\275 Floppy (A)");
    AppendMenuA(m, MF_STRING, 0, "Desktop (create shortcut)");
    AppendMenuA(m, MF_STRING, 0, "Mail Recipient");
    AppendMenuA(m, MF_STRING, 0, "My Documents");
    for (int i = 0; i < 4; i++) {
        HBITMAP b = menu_bitmap(&g_send_icons[i]);
        SetMenuItemBitmaps(m, (UINT)i, MF_BYPOSITION, b, b);
    }
    return m;
}

/* The menu the machine puts on the background of a folder view: nothing is
 * selected, so it is about the folder rather than about a file. Measured at
 * 160 x 204 with these thirteen entries. */
static HMENU build_background_menu(void)
{
    HMENU m = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    HMENU arrange = CreatePopupMenu();
    HMENU new_menu = CreatePopupMenu();

    AppendMenuA(view, MF_STRING, 0, "Lar&ge Icons");
    AppendMenuA(view, MF_STRING, 0, "S&mall Icons");
    AppendMenuA(view, MF_STRING, 0, "&List");
    AppendMenuA(view, MF_STRING, 0, "&Details");
    AppendMenuA(view, MF_STRING, 0, "Thu&mbnails");
    /* Details is the view this explorer is in, and a menu says which of a set
     * it is on with a bullet rather than a tick */
    CheckMenuRadioItem(view, 0, 4, 3, MF_BYPOSITION);

    AppendMenuA(arrange, MF_STRING, 0, "by &Name");
    AppendMenuA(arrange, MF_STRING, 0, "by &Type");
    AppendMenuA(arrange, MF_STRING, 0, "by &Size");
    AppendMenuA(arrange, MF_STRING, 0, "by &Date");
    AppendMenuA(arrange, MF_SEPARATOR, 0, NULL);
    AppendMenuA(arrange, MF_STRING | MF_GRAYED, 0, "&Auto Arrange");

    AppendMenuA(new_menu, MF_STRING, 0, "&Folder");
    AppendMenuA(new_menu, MF_STRING, 0, "&Shortcut");
    AppendMenuA(new_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(new_menu, MF_STRING, 0, "Briefcase");
    AppendMenuA(new_menu, MF_STRING, 0, "Bitmap Image");
    AppendMenuA(new_menu, MF_STRING, 0, "WordPad Document");
    AppendMenuA(new_menu, MF_STRING, 0, "Rich Text Document");
    AppendMenuA(new_menu, MF_STRING, 0, "Text Document");
    AppendMenuA(new_menu, MF_STRING, 0, "Wave Sound");

    AppendMenuA(m, MF_POPUP, (UINT_PTR)view, "&View");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_POPUP, (UINT_PTR)arrange, "Arrange &Icons");
    AppendMenuA(m, MF_STRING | MF_GRAYED, 0, "Li&ne Up Icons");
    AppendMenuA(m, MF_STRING, IDM_CTX_REFRESH, "R&efresh");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, 0, "C&ustomize This Folder...");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING | MF_GRAYED, 0, "&Paste");
    AppendMenuA(m, MF_STRING | MF_GRAYED, 0, "Paste &Shortcut");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_POPUP, (UINT_PTR)new_menu, "Ne&w");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, IDM_CTX_PROPERTIES, "P&roperties");
    return m;
}

/* The menu the header puts up: which columns the view is showing, with a tick
 * against each. Name is always there, so it is ticked and greyed. Measured at
 * 92 x 168 on the machine. */
static HMENU build_column_menu(void)
{
    HMENU m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING | MF_CHECKED | MF_GRAYED, 0, "Name");
    AppendMenuA(m, MF_STRING | MF_CHECKED, 0, "Size");
    AppendMenuA(m, MF_STRING | MF_CHECKED, 0, "Type");
    AppendMenuA(m, MF_STRING | MF_CHECKED, 0, "Modified");
    AppendMenuA(m, MF_STRING, 0, "Attributes");
    AppendMenuA(m, MF_STRING, 0, "Comment");
    AppendMenuA(m, MF_STRING, 0, "Created");
    AppendMenuA(m, MF_STRING, 0, "Accessed");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, 0, "More...");
    return m;
}

static void build_context_menus(void)
{
    g_folder_menu = CreatePopupMenu();
    AppendMenuA(g_folder_menu, MF_STRING | MF_DEFAULT, IDM_CTX_EXPLORE,
                "&Explore");
    AppendMenuA(g_folder_menu, MF_STRING, IDM_CTX_OPEN, "&Open");
    AppendMenuA(g_folder_menu, MF_STRING, 0, "Searc&h...");
    AppendMenuA(g_folder_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_folder_menu, MF_STRING, 0, "Sharing...");
    AppendMenuA(g_folder_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_folder_menu, MF_POPUP, (UINT_PTR)build_send_to(), "Se&nd To");
    AppendMenuA(g_folder_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_folder_menu, MF_STRING, 0, "Cu&t");
    AppendMenuA(g_folder_menu, MF_STRING, 0, "&Copy");
    AppendMenuA(g_folder_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_folder_menu, MF_STRING, 0, "Create &Shortcut");
    AppendMenuA(g_folder_menu, MF_STRING, 0, "&Delete");
    AppendMenuA(g_folder_menu, MF_STRING, 0, "Rena&me");
    AppendMenuA(g_folder_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_folder_menu, MF_STRING, IDM_CTX_PROPERTIES, "P&roperties");

    g_back_menu = build_background_menu();
    g_col_menu = build_column_menu();

    g_file_menu = CreatePopupMenu();
    AppendMenuA(g_file_menu, MF_STRING | MF_DEFAULT, IDM_CTX_OPEN, "&Open");
    AppendMenuA(g_file_menu, MF_STRING, 0, "&Edit");
    AppendMenuA(g_file_menu, MF_STRING, 0, "&Print");
    AppendMenuA(g_file_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_file_menu, MF_POPUP, (UINT_PTR)build_send_to(), "Se&nd To");
    AppendMenuA(g_file_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_file_menu, MF_STRING, 0, "Cu&t");
    AppendMenuA(g_file_menu, MF_STRING, 0, "&Copy");
    AppendMenuA(g_file_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_file_menu, MF_STRING, 0, "Create &Shortcut");
    AppendMenuA(g_file_menu, MF_STRING, 0, "&Delete");
    AppendMenuA(g_file_menu, MF_STRING, 0, "Rena&me");
    AppendMenuA(g_file_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_file_menu, MF_STRING, IDM_CTX_PROPERTIES, "P&roperties");
}

/* ---- the menu bar, as a band of the rebar ---------------------------------
 *
 * A window menu belongs to the frame and cannot live inside a client area, so
 * a menu shown in a rebar has to be drawn by the application: the band's
 * child measures the labels, draws them, and hands each drop-down to
 * TrackPopupMenu when its item is pressed. That is what it takes on either
 * side of the build — it is not a ween32 thing.
 */
#define MENUBAR_PAD 16  /* around each label, as Windows 2000 spaces them */
#define MENUBAR_LEAD 1  /* and the band leads in by one before the first */
#define MENUBAR_BRAND 38 /* the box at the right the shell animates in */

static HMENU g_menu;
#ifdef _WIN32
static int g_menu_open = -1; /* the item whose drop-down is showing */
#endif

/* An item's label without the marker that says which letter is its
 * mnemonic: it is not drawn, so it is not measured either. */
static void menubar_label(int i, char *out, size_t max)
{
    char raw[64];
    size_t j = 0;
    GetMenuStringA(g_menu, (UINT)i, raw, (int)sizeof(raw), MF_BYPOSITION);
    for (size_t k = 0; raw[k] && j < max - 1; k++)
        if (raw[k] != '&')
            out[j++] = raw[k];
    out[j] = 0;
}

/* Where each item sits, measured once. A press has to know this too, and
 * measuring text wants a device context, so the widths are kept from the
 * paint that found them rather than asked for again. */
static int g_mb_x[12], g_mb_w[12], g_mb_n;

static void menubar_measure(HDC dc)
{
    int x = MENUBAR_LEAD;
    g_mb_n = GetMenuItemCount(g_menu);
    if (g_mb_n > (int)(sizeof(g_mb_x) / sizeof(*g_mb_x)))
        g_mb_n = (int)(sizeof(g_mb_x) / sizeof(*g_mb_x));
    for (int i = 0; i < g_mb_n; i++) {
        char label[64];
        SIZE sz;
        menubar_label(i, label, sizeof(label));
        GetTextExtentPoint32A(dc, label, -1, &sz);
        g_mb_x[i] = x;
        g_mb_w[i] = sz.cx + MENUBAR_PAD;
        x += g_mb_w[i];
    }
#ifndef _WIN32 /* ween32 has a menu band; on win32 comctl32 does this */
    /* Hand the layout over: ween32 tracks the bar from here on — a press
     * opens a drop-down, the pointer switches between them, the arrows walk
     * them and Alt arms the lot. On win32 a menu band is a toolbar in a
     * rebar and comctl32 does all that; the code below this is what an
     * application would write there. */
    {
        RECT items[16];
        RECT cr;
        GetClientRect(g_menubar, &cr);
        for (int i = 0; i < g_mb_n && i < 16; i++) {
            items[i].left = g_mb_x[i];
            items[i].right = g_mb_x[i] + g_mb_w[i];
            items[i].top = 0;
            items[i].bottom = cr.bottom;
        }
        ween_menu_band_set(g_main, g_menubar, items, g_mb_n);
    }
#endif
}

static int menubar_hit(int x)
{
    for (int i = 0; i < g_mb_n; i++)
        if (x >= g_mb_x[i] && x < g_mb_x[i] + g_mb_w[i])
            return i;
    return -1;
}

static LRESULT CALLBACK menubar_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT cr = ps.rcPaint;
        FillRect(dc, &cr, GetSysColorBrush(COLOR_BTNFACE));
        SelectObject(dc, g_font);
        menubar_measure(dc);
        for (int i = 0; i < g_mb_n; i++) {
            char label[64];
            RECT r;
#ifndef _WIN32 /* ween32 has a menu band; on win32 comctl32 does this */
            int open = i == ween_menu_band_hot(w);
#else
            int open = i == g_menu_open;
#endif
            menubar_label(i, label, sizeof(label));
            /* The label's pen goes half the padding in, rather than being
             * centred in the item: centring would depend on the measure
             * DrawText happens to align with, and this does not. */
            r.left = g_mb_x[i] + MENUBAR_PAD / 2;
            r.top = 0;
            r.right = g_mb_x[i] + g_mb_w[i];
            /* Centred in the band less the two rows the pushed edge would
             * take underneath, which is a row higher than dead centre and is
             * where Windows 2000 puts it. */
            r.bottom = cr.bottom - 2;
            /* An open item is pushed in, not filled: one pixel of sunken
             * edge, and the label stays black. */
            if (open) {
                RECT e;
                e.left = g_mb_x[i];
                e.right = g_mb_x[i] + g_mb_w[i];
                e.top = 1;
                e.bottom = cr.bottom - 2;
                DrawEdge(dc, &e, BDR_SUNKENOUTER, BF_RECT);
            }
            SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));
            DrawTextA(dc, label, -1, &r,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(w, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int i = menubar_hit(GET_X_LPARAM(lp));
        if (i < 0)
            return 0;
#ifndef _WIN32 /* ween32 has a menu band; on win32 comctl32 does this */
        {
            UINT cmd = ween_menu_band_track(w, i, 0);
            if (cmd)
                SendMessageA(g_main, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0), 0);
        }
#else
        {
            RECT cr;
            POINT pt;
            GetClientRect(w, &cr);
            g_menu_open = i;
            InvalidateRect(w, NULL, FALSE);
            UpdateWindow(w);
            pt.x = g_mb_x[i];
            pt.y = cr.bottom;
            ClientToScreen(w, &pt);
            TrackPopupMenu(GetSubMenu(g_menu, i), TPM_LEFTALIGN, pt.x, pt.y, 0,
                           g_main, NULL);
            g_menu_open = -1;
            InvalidateRect(w, NULL, FALSE);
        }
#endif
        return 0;
    }
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* ---- the brand -----------------------------------------------------------
 *
 * The black box at the right of the menu bar, which the shell plays an
 * animation in. It is not part of the menu band: it stands a row taller and
 * its edge reaches over the rule beneath, so it is a window of the rebar's
 * own rather than a child of any band. The box is the constant part — what
 * runs in it is not, so no frame of it is the one to match.
 */
static LRESULT CALLBACK brand_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT cr, r;
        GetClientRect(w, &cr);
        r = cr;
        r.bottom -= 1; /* the last row belongs to the rule underneath */
        r.left = 2;
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        r = cr;
        r.right = 2;
        DrawEdge(dc, &r, EDGE_ETCHED, BF_LEFT);
        /* the rule carries on under it, bar the pixel the edge takes */
        r.left = 2;
        r.right = cr.right;
        r.top = cr.bottom - 1;
        r.bottom = cr.bottom;
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNSHADOW));
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* ---- the address band ----------------------------------------------------
 *
 * A rebar band holds one child, and this band has two things in it, so the
 * child is a plain window that holds them: the combo box across most of it
 * and a one-button toolbar at the right for Go.
 */
#define GO_W 49 /* what the Go button takes off the right of the band */

static HWND g_go;

static LRESULT CALLBACK address_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SIZE) {
        int cw = LOWORD(lp), ch = HIWORD(lp);
        if (g_address)
            MoveWindow(g_address, 0, 0, cw - GO_W, ch, TRUE);
        if (g_go)
            MoveWindow(g_go, cw - GO_W + 1, 0, GO_W - 1, ch, TRUE);
        return 0;
    }
    if (msg == WM_NOTIFY || msg == WM_COMMAND)
        return SendMessageA(GetParent(w), msg, wp, lp);
    return DefWindowProcA(w, msg, wp, lp);
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
        /* Six in and three down, not centred: the machine leaves three above
         * the text and five below in a bar of twenty. */
        GetClientRect(w, &r);
        r.left += 6;
        r.top += 3;
        DrawTextA(dc, "Folders", -1, &r, DT_LEFT | DT_SINGLELINE);
        /* The close box at its right is a bare glyph, not a button: eight by
         * seven, two pixels thick, seven in from the bar's right edge and six
         * down. Windows only draws a frame round it once the pointer is over
         * it — a caption button here reads far heavier than the machine. */
        {
            RECT cr;
            HBRUSH ink = CreateSolidBrush(GetSysColor(COLOR_BTNTEXT));
            int bx, by;
            GetClientRect(w, &cr);
            bx = cr.right - 15;
            by = 6;
            for (int k = 0; k < 7; k++) {
                int d = k <= 3 ? k : 6 - k;
                x.left = bx + d;
                x.top = by + k;
                x.right = x.left + 2;
                x.bottom = x.top + 1;
                FillRect(dc, &x, ink);
                x.left = bx + 6 - d;
                x.right = x.left + 2;
                FillRect(dc, &x, ink);
            }
            DeleteObject(ink);
        }
        EndPaint(w, &ps);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        /* the cross closes the pane, which is the same thing the Folders
         * button does */
        RECT cr;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        GetClientRect(w, &cr);
        if (x >= cr.right - 15 && x < cr.right - 15 + 8 && y >= 6 && y < 13)
            SendMessageA(GetParent(w), WM_COMMAND,
                         MAKEWPARAM(IDM_FOLDERS, 0), 0);
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

    /* The six the machine has, item for item. What this explorer does not
     * do is greyed or inert; the point is the menus a shell puts up. */
    HMENU newmenu = CreatePopupMenu(), bars = CreatePopupMenu();
    HMENU toolbars = CreatePopupMenu(), goto_menu = CreatePopupMenu();
    HMENU arrange = CreatePopupMenu(), links = CreatePopupMenu();
    HMENU media = CreatePopupMenu();

    AppendMenuA(newmenu, MF_STRING, 0, "&Folder");
    AppendMenuA(newmenu, MF_STRING, 0, "&Shortcut");
    AppendMenuA(file, MF_POPUP, (UINT_PTR)newmenu, "Ne&w");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING | MF_GRAYED, 0, "Create &Shortcut");
    AppendMenuA(file, MF_STRING, 0, "&Delete");
    AppendMenuA(file, MF_STRING, 0, "Rena&me");
    AppendMenuA(file, MF_STRING, IDM_CTX_PROPERTIES, "P&roperties");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, IDM_CLOSE, "&Close");

    AppendMenuA(edit, MF_STRING | MF_GRAYED, 0, "&Undo\tCtrl+Z");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING, 0, "Cu&t\tCtrl+X");
    AppendMenuA(edit, MF_STRING, 0, "&Copy\tCtrl+C");
    AppendMenuA(edit, MF_STRING | MF_GRAYED, 0, "&Paste\tCtrl+V");
    AppendMenuA(edit, MF_STRING | MF_GRAYED, 0, "Paste &Shortcut");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING, 0, "Select &All\tCtrl+A");
    AppendMenuA(edit, MF_STRING, 0, "&Invert Selection");

    AppendMenuA(toolbars, MF_STRING | MF_CHECKED, 0, "&Standard Buttons");
    AppendMenuA(toolbars, MF_STRING | MF_CHECKED, 0, "&Address Bar");
    AppendMenuA(toolbars, MF_STRING, 0, "&Links");
    AppendMenuA(toolbars, MF_SEPARATOR, 0, NULL);
    AppendMenuA(toolbars, MF_STRING, 0, "&Customize...");
    AppendMenuA(bars, MF_STRING, 0, "&Search");
    AppendMenuA(bars, MF_STRING, 0, "&Favorites");
    AppendMenuA(bars, MF_STRING, 0, "&History");
    AppendMenuA(bars, MF_STRING | MF_CHECKED, IDM_FOLDERS, "F&olders");
    AppendMenuA(bars, MF_SEPARATOR, 0, NULL);
    AppendMenuA(bars, MF_STRING, 0, "&Tip of the Day");
    AppendMenuA(arrange, MF_STRING, 0, "by &Name");
    AppendMenuA(arrange, MF_STRING, 0, "by &Type");
    AppendMenuA(arrange, MF_STRING, 0, "by &Size");
    AppendMenuA(arrange, MF_STRING, 0, "by &Date");
    AppendMenuA(arrange, MF_SEPARATOR, 0, NULL);
    AppendMenuA(arrange, MF_STRING | MF_GRAYED, 0, "&Auto Arrange");
    AppendMenuA(goto_menu, MF_STRING, 0, "&Back\tAlt+Left Arrow");
    AppendMenuA(goto_menu, MF_STRING | MF_GRAYED, 0, "&Forward\tAlt+Right Arrow");
    AppendMenuA(goto_menu, MF_STRING, IDM_UP, "&Up One Level");
    AppendMenuA(goto_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(goto_menu, MF_STRING, 0, "&Home Page\tAlt+Home");
    AppendMenuA(view, MF_POPUP, (UINT_PTR)toolbars, "&Toolbars");
    AppendMenuA(view, MF_STRING | MF_CHECKED, 0, "Status &Bar");
    AppendMenuA(view, MF_POPUP, (UINT_PTR)bars, "&Explorer Bar");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, 0, "Lar&ge Icons");
    AppendMenuA(view, MF_STRING, 0, "S&mall Icons");
    AppendMenuA(view, MF_STRING, 0, "&List");
    AppendMenuA(view, MF_STRING, 0, "&Details");
    AppendMenuA(view, MF_STRING, 0, "Thu&mbnails");
    CheckMenuRadioItem(view, 4, 8, 7, MF_BYPOSITION);
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_POPUP, (UINT_PTR)arrange, "Arrange &Icons");
    AppendMenuA(view, MF_STRING | MF_GRAYED, 0, "Line &Up Icons");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, 0, "&Choose Columns...");
    AppendMenuA(view, MF_STRING, 0, "Customi&ze This Folder...");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_POPUP, (UINT_PTR)goto_menu, "&Go To");
    AppendMenuA(view, MF_STRING, 0, "&Refresh");

    AppendMenuA(links, MF_STRING | MF_GRAYED, 0, "(empty)");
    AppendMenuA(media, MF_STRING | MF_GRAYED, 0, "(empty)");
    AppendMenuA(favorites, MF_STRING, 0, "&Add to Favorites...");
    AppendMenuA(favorites, MF_STRING, 0, "&Organize Favorites...");
    AppendMenuA(favorites, MF_SEPARATOR, 0, NULL);
    AppendMenuA(favorites, MF_POPUP, (UINT_PTR)links, "Links");
    AppendMenuA(favorites, MF_POPUP, (UINT_PTR)media, "Media");
    AppendMenuA(favorites, MF_STRING, 0, "MSN");
    AppendMenuA(favorites, MF_STRING, 0, "Radio Station Guide");
    AppendMenuA(favorites, MF_STRING, 0, "Web Events");

    AppendMenuA(tools, MF_STRING, 0, "&Map Network Drive...");
    AppendMenuA(tools, MF_STRING, 0, "&Disconnect Network Drive...");
    AppendMenuA(tools, MF_STRING, 0, "&Synchronize...");
    AppendMenuA(tools, MF_SEPARATOR, 0, NULL);
    AppendMenuA(tools, MF_STRING, 0, "F&older Options...");

    AppendMenuA(help, MF_STRING, 0, "&Help Topics");
    AppendMenuA(help, MF_SEPARATOR, 0, NULL);
    AppendMenuA(help, MF_STRING, IDM_ABOUT, "&About Windows");

    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)edit, "&Edit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "&View");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)favorites, "F&avorites");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)tools, "&Tools");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)help, "&Help");
    /* Not SetMenu: the shell does not hang its menu off the window frame. It
     * goes in the first band of the same rebar as the toolbar, which is why
     * the screenshot has a gripper to the left of File. See menubar_proc. */
    g_menu = bar;
#ifndef _WIN32 /* ween32 has a menu band; on win32 comctl32 does this */
    /* The frame is told about the menu even though it does not draw it: that
     * is what lets Alt reach it. ween_menu_band_set, in menubar_measure,
     * says where it really is, and the frame keeps none of its height. */
    SetMenu(w, bar);
#else
    (void)w;
#endif
}
#endif

/* Where the icons are.
 *
 * Run from the top of the source tree the example finds them under the
 * working directory, but nothing says it will be run from there, and an
 * example that only works from one directory is a trap. So it also looks
 * beside its own executable, which is the one thing it always knows. Set
 * WEEN32_ASSETS to override both.
 */
static char g_argv0[512];

static const char *asset_dir(void)
{
    static char base[512];
    static int looked;
    const char *tries[5];
    char beside[600], up[640], up2[680];
    int n = 0;

    if (looked)
        return base;
    looked = 1;

    {   /* the directory the executable is in, and the tree above it */
        const char *slash = strrchr(g_argv0, '/');
#ifdef _WIN32
        const char *back = strrchr(g_argv0, '\\');
        if (back && (!slash || back > slash))
            slash = back;
#endif
        if (slash) {
            size_t len = (size_t)(slash - g_argv0);
            if (len > sizeof(beside) - 32)
                len = sizeof(beside) - 32;
            memcpy(beside, g_argv0, len);
            beside[len] = 0;
            /* built in place the assets sit one level up; installed
             * somewhere they may sit beside the executable instead */
            snprintf(up, sizeof(up), "%s/../assets/icons", beside);
            snprintf(up2, sizeof(up2), "%s/../../assets/icons", beside);
            strcat(beside, "/assets/icons");
            tries[n++] = up;
            tries[n++] = up2;
            tries[n++] = beside;
        }
    }
    tries[n++] = "assets/icons";

    for (int i = 0; i < n; i++) {
        char probe[600];
        FILE *f;
        snprintf(probe, sizeof(probe), "%s/" ICON_FOLDER ".ico", tries[i]);
        f = fopen(probe, "rb");
        if (f) {
            fclose(f);
            snprintf(base, sizeof(base), "%s", tries[i]);
            return base;
        }
    }
    {
        const char *env = getenv("WEEN32_ASSETS");
        if (env)
            snprintf(base, sizeof(base), "%s", env);
    }
    return base;
}

#if HAVE(IMAGELIST)
/* The art, centred in a 16x16 bitmap and handed over with its background
 * named as the transparent colour — which is all ImageList_AddMasked wants. */
static void add_glyph(HIMAGELIST il, const glyph *g)
{
    unsigned char bits[16 * 16 * 4];
    HBITMAP bmp;
    int ox = g->ox, oy = g->oy;

    for (int i = 0; i < 16 * 16; i++) {
        bits[i * 4 + 0] = 0xff; /* B,G,R as every win32 DIB is */
        bits[i * 4 + 1] = 0x00;
        bits[i * 4 + 2] = 0xff;
        bits[i * 4 + 3] = 0;
    }
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            COLORREF c = glyph_colour(g, g->rows[y][x]);
            unsigned char *p = bits + (((size_t)(y + oy) * 16) + x + ox) * 4;
            p[0] = (unsigned char)(c >> 16);
            p[1] = (unsigned char)(c >> 8);
            p[2] = (unsigned char)c;
        }
    }
    bmp = CreateBitmap(16, 16, 1, 32, bits);
    ImageList_AddMasked(il, bmp, RGB(255, 0, 255));
    DeleteObject(bmp);
}

/* Nothing, but it takes up a slot. Every image is named by its index, so an
 * icon that will not load has to leave a hole rather than close the gap: skip
 * it and every image after it answers to the wrong name — the tree draws
 * arrows for its folders and a toolbar button wears its neighbour's icon. */
static void add_blank(HIMAGELIST il)
{
    unsigned char bits[16 * 16 * 4];
    HBITMAP bmp;
    for (int i = 0; i < 16 * 16; i++) {
        bits[i * 4 + 0] = 0xff;
        bits[i * 4 + 1] = 0x00;
        bits[i * 4 + 2] = 0xff;
        bits[i * 4 + 3] = 0;
    }
    bmp = CreateBitmap(16, 16, 1, 32, bits);
    ImageList_AddMasked(il, bmp, RGB(255, 0, 255));
    DeleteObject(bmp);
}

/* One image list. Two are built: the ordinary one, and the one a toolbar
 * shows for the button under the pointer, which is the same set except where
 * a glyph has a hot drawing of its own. Both have to be the same length and
 * in the same order, because a button names its image by index. */
static HIMAGELIST build_images(const glyph *glyphs, int *missing)
{
    static const char *names[] = {
        ICON_FOLDER, ICON_FOLDER_OPEN, ICON_FILE, ICON_COMPUTER, ICON_DRIVE
    };
    HIMAGELIST il = ImageList_Create(16, 16, ILC_MASK, IMG_COUNT, 4);

    for (int i = 0; i < (int)(sizeof(names) / sizeof(*names)); i++) {
        char path[600];
        HICON icon;
        snprintf(path, sizeof(path), "%s/%s.ico", asset_dir(), names[i]);
        icon = (HICON)LoadImageA(NULL, path, IMAGE_ICON, 16, 16,
                                 LR_LOADFROMFILE);
        if (icon) {
            ImageList_AddIcon(il, icon);
            DestroyIcon(icon);
        } else {
            add_blank(il);
            (*missing)++;
        }
    }
    for (int i = 0; i < (int)(sizeof(GLYPHS) / sizeof(*GLYPHS)); i++)
        add_glyph(il, &glyphs[i]);
    /* The shell's namespace icons, which only the fixture shows. Blanks keep
     * the indices the same when it is off, since a button names its image by
     * number and everything after a hole would answer to the wrong one. */
    {
        static const char *shell[] = { "35", "mydocs", NULL, "9",
                                       "137", "18", "32", "512", "151",
                                       "153", "154" };
        for (int i = 0; i < (int)(sizeof(shell) / sizeof(*shell)); i++) {
            char path[600];
            HICON icon = NULL;
            if (g_fixture && !shell[i]) { /* My Computer, drawn from the art */
                add_glyph(il, &g_shell_computer);
                continue;
            }
            if (g_fixture) {
                snprintf(path, sizeof(path), "%s/%s.ico", asset_dir(), shell[i]);
                icon = (HICON)LoadImageA(NULL, path, IMAGE_ICON, 16, 16,
                                         LR_LOADFROMFILE);
            }
            if (icon) {
                ImageList_AddIcon(il, icon);
                DestroyIcon(icon);
            } else {
                add_blank(il);
            }
        }
    }
    return il;
}

static void load_icons(void)
{
    int missing = 0;
    g_images = build_images(GLYPHS, &missing);
    g_hot_images = build_images(GLYPHS_HOT, &missing);
    if (missing)
        fprintf(stderr,
                "explorer: %d of the icons are missing — looked in \"%s\". "
                "Set WEEN32_ASSETS to the assets/icons directory.\n",
                missing / 2, asset_dir()[0] ? asset_dir() : "assets/icons");
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
    SendMessageA(g_toolbar, TB_SETHOTIMAGELIST, 0, (LPARAM)g_hot_images);

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
    b[n].iBitmap = IMG_FOLDERS;
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
    b[n].fsState = 0;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].iBitmap = IMG_COPYTO;
    b[n].idCommand = IDM_COPYTO;
    b[n].fsState = 0;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].iBitmap = IMG_DELETE;
    b[n].idCommand = IDM_DELETE;
    b[n].fsState = TBSTATE_ENABLED;
    b[n].fsStyle = TBSTYLE_BUTTON;
    n++;
    b[n].iBitmap = IMG_UNDO;
    b[n].idCommand = IDM_UNDO;
    b[n].fsState = TBSTATE_ENABLED;
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
    {
        /* The views button does not follow from its image and its arrow the
         * way the others do: the shell gives it thirty-one, which puts its
         * arrow four into the last twelve of that. */
        TBBUTTONINFOA bi;
        memset(&bi, 0, sizeof(bi));
        bi.cbSize = sizeof(bi);
        bi.dwMask = TBIF_SIZE;
        bi.cx = 31;
        SendMessageA(g_toolbar, TB_SETBUTTONINFOA, IDM_VIEWS, (LPARAM)&bi);
    }

    g_addrband = CreateWindowA("exploreraddr", "", WS_CHILD | WS_VISIBLE, 0, 0,
                               300, 22, g_rebar, (HMENU)(UINT_PTR)ID_ADDRBAND,
                               NULL, NULL);
    g_address = CreateWindowExA(WS_EX_CLIENTEDGE, WC_COMBOBOXEXA, "",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    CBS_DROPDOWNLIST, 0, 0,
                                300, 21, g_addrband,
                                (HMENU)(UINT_PTR)ID_ADDRESS, NULL, NULL);
    g_go = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                           WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST,
                           0, 0, GO_W, 22, g_addrband,
                           (HMENU)(UINT_PTR)ID_GO, NULL, NULL);
    SendMessageA(g_go, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageA(g_go, TB_SETIMAGELIST, 0, (LPARAM)g_images);
    SendMessageA(g_go, TB_SETHOTIMAGELIST, 0, (LPARAM)g_hot_images);
    {
        TBBUTTON g;
        memset(&g, 0, sizeof(g));
        g.iBitmap = IMG_GO;
        g.idCommand = IDM_GO;
        g.fsState = TBSTATE_ENABLED;
        g.fsStyle = TBSTYLE_BUTTON;
        g.iString = (INT_PTR) "Go";
        SendMessageA(g_go, TB_ADDBUTTONSA, 1, (LPARAM)&g);
    }
#if HAVE(IMAGELIST)
    SendMessageA(g_address, CBEM_SETIMAGELIST, 0, (LPARAM)g_images);
#endif

    /* registered before anything is laid out, so the frame never keeps a
     * strip for a bar it is not going to draw */
    g_menubar = CreateWindowA("explorermenu", "", WS_CHILD | WS_VISIBLE, 0, 0,
                              100, 22, g_rebar, (HMENU)(UINT_PTR)ID_MENUBAR,
                              NULL, NULL);
#ifndef _WIN32 /* ween32 has a menu band; on win32 comctl32 does this */
    {
        RECT none = { 0, 0, 0, 0 };
        ween_menu_band_set(g_main, g_menubar, &none, 0);
    }
#endif
    g_brand = CreateWindowA("explorerbrand", "", WS_CHILD | WS_VISIBLE, 0, 2,
                            BRAND_W, BRAND_H, g_rebar,
                            (HMENU)(UINT_PTR)ID_BRAND, NULL, NULL);

    memset(&bi, 0, sizeof(bi));
    bi.cbSize = sizeof(bi);
    bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE;
    bi.hwndChild = g_menubar;
    bi.cyMinChild = 22;
    SendMessageA(g_rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);

    bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE;
    bi.hwndChild = g_toolbar;
    bi.cyMinChild = 22;
    SendMessageA(g_rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);

    bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_TEXT;
    bi.hwndChild = g_addrband;
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
        int fmt;
    } columns[] = {
        /* the machine's, off its header's dividers; a size goes on the right
         * of its column, as the shell puts it */
        { "Name", 120, LVCFMT_LEFT },
        { "Size", 96, LVCFMT_RIGHT },
        { "Type", 120, LVCFMT_LEFT },
        { "Modified", 120, LVCFMT_LEFT }
    };

    g_panehead = CreateWindowA("explorerpane", "", WS_CHILD | WS_VISIBLE, 0, 0,
                               10, 10, w, (HMENU)(UINT_PTR)ID_PANEHEAD, NULL,
                               NULL);
    /* LINESATROOT is what carries the lines and the boxes out to the top
     * level; without it the root sits bare, which is not what the shot has. */
    /* No edge of its own: the pane frame drawn round the "Folders" bar and
     * the tree together is the only one the machine has. And no lines at the
     * root either: the shell's tree gives Desktop neither a line nor a box,
     * and everything under it both. */
    g_tree = CreateWindowExA(0, WC_TREEVIEWA, "",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                 TVS_HASLINES | TVS_HASBUTTONS,
                             0, 0, 10, 10, w, (HMENU)(UINT_PTR)ID_TREE, NULL,
                             NULL);
    g_split = CreateWindowA("explorersplit", "", WS_CHILD | WS_VISIBLE, 0, 0,
                            10, 10, w, (HMENU)(UINT_PTR)ID_SPLIT, NULL, NULL);
    /* REPORT is the view with the columns in it. A list view left to itself
     * comes up as icons, which is not what a details pane is. */
    /* Both panes are tab stops, and so is the address bar: Tab walks them,
     * which is how the machine moves between them. */
    g_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                                 LVS_SINGLESEL,
                             0, 0, 10, 10, w, (HMENU)(UINT_PTR)ID_LIST, NULL,
                             NULL);

#if HAVE(IMAGELIST)
    SendMessageA(g_tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)g_images);
    SendMessageA(g_list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_images);
#endif

    for (int i = 0; i < 4; i++) {
        LVCOLUMNA col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = (char *)columns[i].title;
        col.cx = columns[i].width;
        col.fmt = columns[i].fmt;
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
        build_context_menus();
#endif
#if HAVE(IMAGELIST)
        load_icons();
#endif
#if HAVE(TOOLBAR)
        build_bands(w);
#endif
        build_views(w);
        /* the status bar's last part wears My Computer, as the machine's
         * does; it is set once, the parts themselves move with the window */
        if (g_status)
            SendMessageA(g_status, SB_SETICON, 2,
                         (LPARAM)glyph_icon(&g_shell_computer));
        /* The list has the keyboard when a folder opens, which is why an
         * arrow key moves through the files without clicking first. */
        if (g_list)
            SetFocus(g_list);
        if (g_fixture) {
            fill_fixture_tree();
        } else {
            HTREEITEM root = add_node(NULL, "/", IMG_COMPUTER, IMG_COMPUTER, 1);
            SendMessageA(g_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)root);
        }
        show_directory("/");
        return 0;

    case WM_SIZE:
        layout(w);
        return 0;

    case WM_PAINT: {
        /* The frame round the tree pane. It is the window's to draw because it
         * goes round two children at once — the "Folders" bar and the tree —
         * with a rule between them where they meet. */
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT cr, fr, r;
        GetClientRect(w, &cr);
        FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
        pane_frame(&cr, &fr);
        if (!g_folders) { /* nothing to put a frame round */
            EndPaint(w, &ps);
            return 0;
        }
        r = fr;
        DrawEdge(dc, &r, EDGE_ETCHED, BF_RECT);
        r.left = fr.left + PANE_INSET;
        r.right = fr.right - PANE_INSET;
        r.top = fr.top + PANE_INSET + PANE_HEAD_H;
        r.bottom = r.top + 1;
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNSHADOW));
        EndPaint(w, &ps);
        return 0;
    }

    case WM_CONTEXTMENU: {
        /* Which of the two panes asked, and about what. The list answers with
         * a row and the tree with an item; either way what is wanted is
         * whether it is a folder, since that is what the menu turns on. */
        HWND from = (HWND)wp;
        POINT pt;
        HMENU menu = NULL;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        if (from == g_list) {
            LVHITTESTINFO hi;
            memset(&hi, 0, sizeof(hi));
            hi.pt = pt;
            ScreenToClient(g_list, &hi.pt);
            if (SendMessageA(g_list, LVM_HITTEST, 0, (LPARAM)&hi) < 0) {
                /* Off every name. Above the first row it is the header, and
                 * the machine offers the columns there; anywhere else — the
                 * cells to the right of a name, or under the last row — it is
                 * the folder's background, and the list has dropped its
                 * selection by now, so what comes up is the folder's own. */
                RECT ir;
                g_ctx_row = -1;
                ir.left = LVIR_BOUNDS;
                menu = (SendMessageA(g_list, LVM_GETITEMRECT, 0, (LPARAM)&ir) &&
                        hi.pt.y < ir.top)
                           ? g_col_menu
                           : g_back_menu;
            } else {
                int is_dir;
                g_ctx_row = hi.iItem;
                if (g_fixture)
                    is_dir = hi.iItem < (int)(sizeof(g_fix_list) /
                                              sizeof(*g_fix_list)) &&
                             g_fix_list[hi.iItem].is_dir;
                else
                    is_dir = hi.iItem < g_entries && g_entry[hi.iItem].is_dir;
                menu = is_dir ? g_folder_menu : g_file_menu;
            }
        } else if (from == g_tree) {
            TVHITTESTINFO hi;
            memset(&hi, 0, sizeof(hi));
            hi.pt = pt;
            ScreenToClient(g_tree, &hi.pt);
            if (!SendMessageA(g_tree, TVM_HITTEST, 0, (LPARAM)&hi))
                return 0;
            g_ctx_item = hi.hItem; /* every item in the tree is a folder */
            g_ctx_row = -1;
            menu = g_folder_menu;
        }
        if (menu)
            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y,
                           0, w, NULL);
        return 0;
    }

    case WM_NOTIFY: {
        const NMHDR *nm = (const NMHDR *)lp;
        if (nm->code == TVN_SELCHANGEDA) {
            HTREEITEM sel = (HTREEITEM)SendMessageA(g_tree, TVM_GETNEXTITEM,
                                                    TVGN_CARET, 0);
            char path[1024];
            /* One selection between the two panes: picking a folder on the
             * left drops whatever was picked on the right, and the other way
             * about. The machine keeps only one of them lit. */
            if (!g_crossing) {
                LVITEMA st;
                g_crossing = 1;
                memset(&st, 0, sizeof(st));
                st.mask = LVIF_STATE;
                st.stateMask = LVIS_SELECTED;
                SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
                g_crossing = 0;
            }
            if (sel) {
                path_of_item(sel, path, sizeof(path));
                show_directory(path);
            }
        } else if (nm->code == LVN_COLUMNCLICK) {
            /* clicking the column already sorted by turns it round */
            int col = ((const NMLISTVIEW *)lp)->iSubItem;
            g_sort_down = col == g_sort_col ? !g_sort_down : 0;
            g_sort_col = col;
            mark_sorted_column();
            fill_list();
        } else if (nm->code == LVN_ITEMCHANGED) {
            int sel = (int)SendMessageA(g_list, LVM_GETNEXTITEM, (WPARAM)-1,
                                        LVNI_SELECTED);
            if (sel >= 0 && !g_crossing) { /* see TVN_SELCHANGED above */
                g_crossing = 1;
                SendMessageA(g_tree, TVM_SELECTITEM, TVGN_CARET, 0);
                g_crossing = 0;
            }
            status_for_selection(sel);
        } else if (nm->code == NM_DBLCLK) {
            /* opening a folder is what a double click does in a shell */
            int sel = (int)SendMessageA(g_list, LVM_GETNEXTITEM, (WPARAM)-1,
                                        LVNI_SELECTED);
            if (sel >= 0 && sel < g_entries && g_entry[sel].is_dir) {
                char path[1400];
                snprintf(path, sizeof(path), "%s%s%s", g_path,
                         strcmp(g_path, "/") ? "/" : "", g_entry[sel].name);
                show_directory(path);
            }
        } else if (nm->code == TVN_ITEMEXPANDINGA) {
            /* read the level being opened, so it is there to be drawn */
            const NMTREEVIEWA *tv = (const NMTREEVIEWA *)lp;
            if (tv->action == TVE_EXPAND && tv->itemNew.hItem && !g_fixture) {
                char path[1024];
                path_of_item(tv->itemNew.hItem, path, sizeof(path));
                fill_children(tv->itemNew.hItem, path);
            }
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_CLOSE:
            DestroyWindow(w);
            return 0;
        case IDM_FOLDERS:
            /* the toolbar's Folders button, View > Explorer Bar > Folders and
             * the cross in the pane's own bar all come here, as they all do on
             * the machine */
            g_folders = !g_folders;
            if (g_toolbar)
                SendMessageA(g_toolbar, TB_CHECKBUTTON, IDM_FOLDERS,
                             MAKELPARAM(g_folders, 0));
            if (g_menu) /* by command, so it reaches into Explorer Bar */
                CheckMenuItem(GetSubMenu(g_menu, 2), IDM_FOLDERS,
                              g_folders ? MF_CHECKED : MF_UNCHECKED);
            layout(w);
            InvalidateRect(w, NULL, TRUE);
            if (!g_folders && g_list)
                SetFocus(g_list);
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
        case IDM_CTX_EXPLORE:
        case IDM_CTX_OPEN: {
            /* Open what the menu was about: a row of the list, or an item of
             * the tree. */
            char path[1400];
            if (g_ctx_row >= 0 && g_ctx_row < g_entries) {
                if (!g_entry[g_ctx_row].is_dir)
                    return 0;
                snprintf(path, sizeof(path), "%s%s%s", g_path,
                         strcmp(g_path, "/") ? "/" : "",
                         g_entry[g_ctx_row].name);
                show_directory(path);
            } else if (g_ctx_item) {
                path_of_item(g_ctx_item, path, sizeof(path));
                show_directory(path);
            }
            return 0;
        }
#if HAVE(MESSAGEBOX)
        case IDM_CTX_PROPERTIES: {
            char msg[600];
            const char *name = g_ctx_row >= 0 && g_ctx_row < g_entries
                                   ? g_entry[g_ctx_row].name
                                   : "";
            snprintf(msg, sizeof(msg), "%s%s", name,
                     name[0] ? "" : "Nothing is selected.");
            MessageBoxA(w, msg[0] ? msg : "Nothing is selected.",
                        "Properties", MB_OK);
            return 0;
        }
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

int main(int argc, char **argv)
{
    WNDCLASSA wc;

    if (argc > 0 && argv[0])
        snprintf(g_argv0, sizeof(g_argv0), "%s", argv[0]);
    /* the fixed listing, for putting this window beside a screenshot of the
     * machine it is a reimplementation of */
    g_fixture = getenv("WEEN32_EXPLORER_FIXTURE") != NULL;

#ifdef _WIN32
    /* Built as a console app so that main() serves both worlds; on Windows
     * this is a GUI program, so drop the console window it came with. */
    FreeConsole();
    /* And the common controls have to be asked for by name there: the rebar
     * wants COOL, the address bar's ComboBoxEx wants USEREX, and the tree,
     * the list, the toolbar and the status bar are all in WIN95. Without
     * this every one of them fails to create and the window comes up empty.
     * ween32 registers its own, so this is the win32 side only. */
    {
        INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_WIN95_CLASSES |
                                                   ICC_USEREX_CLASSES |
                                                   ICC_COOL_CLASSES };
        InitCommonControlsEx(&ic);
    }
#endif

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = explorer_proc;
    wc.lpszClassName = "ween32explorer";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    {
        char path[600];
        snprintf(path, sizeof(path), "%s/" ICON_APP ".ico", asset_dir());
        wc.hIcon = (HICON)LoadImageA(NULL, path, IMAGE_ICON, 16, 16,
                                     LR_LOADFROMFILE);
    }
#if HAVE(CURSORS)
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
#endif
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = brand_proc;
    wc.lpszClassName = "explorerbrand";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = address_proc;
    wc.lpszClassName = "exploreraddr";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = menubar_proc;
    wc.lpszClassName = "explorermenu";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
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
