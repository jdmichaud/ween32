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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "win32_dlg.h"

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
    IDM_ABOUT,

    /* File */
    IDM_NEW_FOLDER,
    IDM_NEW_SHORTCUT,
    IDM_CREATE_SHORTCUT,
    IDM_RENAME,
    /* Edit */
    IDM_CUT,
    IDM_COPY,
    IDM_PASTE,
    IDM_PASTE_SHORTCUT,
    IDM_SELECT_ALL,
    IDM_INVERT,
    /* View: the five ways of showing a folder, in the order the menu has
     * them — the code leans on that, so they stay together and in order */
    IDM_VIEW_LARGE,
    IDM_VIEW_SMALL,
    IDM_VIEW_LIST,
    IDM_VIEW_DETAILS,
    IDM_VIEW_THUMBS,
    IDM_TOOLBAR_STD,
    IDM_TOOLBAR_ADDR,
    IDM_TOOLBAR_LINKS,
    IDM_TOOLBAR_CUSTOMIZE,
    IDM_STATUSBAR,
    IDM_BAR_SEARCH,
    IDM_BAR_FAVORITES,
    IDM_BAR_HISTORY,
    IDM_BAR_TIP,
    /* and the four orders it can be in, in the same way */
    IDM_ARRANGE_NAME,
    IDM_ARRANGE_TYPE,
    IDM_ARRANGE_SIZE,
    IDM_ARRANGE_DATE,
    IDM_AUTO_ARRANGE,
    IDM_LINEUP,
    IDM_CHOOSE_COLUMNS,
    IDM_CUSTOMIZE_FOLDER,
    IDM_HOME,
    IDM_REFRESH,
    /* Favorites */
    IDM_FAV_ADD,
    IDM_FAV_ORGANIZE,
    IDM_FAV_MSN,
    IDM_FAV_RADIO,
    IDM_FAV_WEB,
    /* Tools */
    IDM_MAP_DRIVE,
    IDM_DISCONNECT,
    IDM_SYNCHRONIZE,
    IDM_FOLDER_OPTIONS,
    /* Help */
    IDM_HELP_TOPICS,

    /* one per title of the menu bar, since a toolbar's buttons are known by
     * their command and these are the buttons the bar is made of */
    IDM_MENU_FIRST = 400,
    /* one per step of the walk, for the lists Back and Forward drop down */
    IDM_HIST_FIRST = 500
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
static HIMAGELIST g_images, g_hot_images, g_big_images;
static HFONT g_font;
static int g_split_x = 203; /* the tree pane's width, measured off the shot */
static int g_folders = 1;   /* whether the tree pane is shown at all */
static int g_crossing;      /* one pane clearing the other's selection */
static int g_dragging;
static char g_path[1024] = "/";

/* Where this window has been and where it is in that, which is what the two
 * arrows walk. A folder opened from anywhere else is pushed on the end; going
 * back and then somewhere new drops what was in front. */
#define HIST_MAX 32
static char g_hist[HIST_MAX][sizeof(g_path)];
static int g_hist_n, g_hist_at = -1;
static int g_navigating; /* set while Back or Forward is doing the walking */
static int g_show_status = 1; /* View > Status Bar */
static HACCEL g_accel;                            /* the menus' own shortcuts */
static int g_show_toolbar = 1, g_show_address = 1; /* View > Toolbars */
static int g_view = 3;        /* which of the five View offers, Details being
                               * the fourth — the order the menu has them in */
/* The folder to open in, when the command line named one. */
static char g_start[512];

/* Where Home goes: the top of the tree the explorer is showing. */
static const char *home_path(void)
{
#ifdef _WIN32
    return "C:\\";
#else
    return "/";
#endif
}

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
    out->bottom = cr->bottom - (g_show_status ? STATUS_H : 0);
}

static void layout(HWND w)
{
    RECT cr;
    int top, bottom, left_w;
    GetClientRect(w, &cr);
    top = rebar_height();
    bottom = cr.bottom - (g_show_status ? STATUS_H : 0);
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
        r = lstrcmpiA(type_of(a), type_of(b));
        break;
    case 3: /* modified, which is a string in a form that does not sort */
        r = lstrcmpiA(a->modified, b->modified);
        break;
    default:
        break;
    }
    if (r == 0)
        r = lstrcmpiA(a->name, b->name);
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
    /* The arrow belongs to the heading, so it is asked of the header the list
     * keeps its columns in — not of the column, whose format is only the
     * alignment its cells are laid out to. */
    HWND head = (HWND)(INT_PTR)SendMessageA(g_list, LVM_GETHEADER, 0, 0);
    if (!head)
        return;
    for (int c = 0; c < 4; c++) {
        HDITEMA hd;
        /* Read, change, write: the format carries more than the alignment —
         * whether the heading has a name to draw, for one — and setting it
         * from nothing takes the rest of it away. */
        memset(&hd, 0, sizeof(hd));
        hd.mask = HDI_FORMAT;
        if (!SendMessageA(head, HDM_GETITEMA, (WPARAM)c, (LPARAM)&hd))
            continue;
        hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (c == g_sort_col)
            hd.fmt |= g_sort_down ? HDF_SORTDOWN : HDF_SORTUP;
        SendMessageA(head, HDM_SETITEMA, (WPARAM)c, (LPARAM)&hd);
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

/* ---- what the File and Edit menus do -------------------------------------
 *
 * The commands themselves. Everything here works on the list's selection,
 * which is a set rather than a row: Select All picks the lot, and Cut, Copy,
 * Delete and the rest take whatever is picked.
 */

/* Long enough for the folder, a separator and a name — every path a command
 * puts together is one of those. */
#define PATH_MAX_LEN (sizeof(g_path) + sizeof(g_entry->name) + 2)

/* The full path of a row, which is the folder and the name in it. */
static void path_of_row(int row, char *out, size_t max)
{
    const char *sep = g_path[strlen(g_path) - 1] == FS_SEP ? "" : NULL;
    char one[2] = { FS_SEP, 0 };
    if (row < 0 || row >= g_entries) {
        out[0] = 0;
        return;
    }
    snprintf(out, max, "%s%s%s", g_path, sep ? sep : one, g_entry[row].name);
}

/* The rows that are picked, in order. Returns how many. */
#define SEL_MAX 512
static int selected_rows(int *out, int max)
{
    int n = 0, i = -1;
    while (n < max &&
           (i = (int)SendMessageA(g_list, LVM_GETNEXTITEM, (WPARAM)i,
                                  LVNI_SELECTED)) >= 0)
        out[n++] = i;
    return n;
}

/* What the last command did, so Edit can offer to undo it and name itself
 * after it — "Undo Delete", which is what the shell's menu says. */
static struct {
    const char *what;             /* "Delete", "Rename", ... or NULL for none */
    char from[SEL_MAX][260];      /* where each thing was */
    char to[SEL_MAX][260];        /* and where it went */
    int count;
} g_undo;

static void undo_clear(void)
{
    g_undo.what = NULL;
    g_undo.count = 0;
}

static void undo_add(const char *what, const char *from, const char *to)
{
    if (g_undo.what != what)
        undo_clear();
    if (g_undo.count >= SEL_MAX)
        return;
    g_undo.what = what;
    snprintf(g_undo.from[g_undo.count], sizeof(g_undo.from[0]), "%s", from);
    snprintf(g_undo.to[g_undo.count], sizeof(g_undo.to[0]), "%s", to);
    g_undo.count++;
}

/* Where a deleted thing goes, so that deleting it is not the end of it: the
 * shell has a Recycle Bin and this has a folder of its own beside the
 * program. Undo Delete brings a thing back out of it. */
static const char *recycle_dir(void)
{
    static char dir[600];
    if (!dir[0]) {
        const char *tmp = getenv("TEMP");
        if (!tmp)
            tmp = getenv("TMP");
        if (!tmp)
            tmp = "/tmp";
        snprintf(dir, sizeof(dir), "%s%cween32-recycled", tmp, FS_SEP);
        fs_mkdir(dir); /* already there is fine */
    }
    return dir;
}

/* The clipboard, as a shell keeps it: the paths, and whether they were cut
 * rather than copied — a cut file is drawn ghosted until the paste. */
/* one path each, long enough for the longest the app can build */
static char g_clip[SEL_MAX][PATH_MAX_LEN];
static int g_clip_n, g_clip_cut;

static void show_directory(const char *path);

/* Read this folder again, keeping where we are: what a command has changed
 * on disk shows up because the list is filled from disk again. */
static void refresh_view(void)
{
    char here[sizeof(g_path)];
    snprintf(here, sizeof(here), "%s", g_path);
    g_navigating = 1; /* re-reading where we are is not a step in the walk */
    show_directory(here);
    g_navigating = 0;
}

/* Cut and Copy: remember what is picked, and ghost it if it was cut. */
static void do_clip(int cut)
{
    int rows[SEL_MAX];
    int n = selected_rows(rows, SEL_MAX);
    LVITEMA st;
    if (!n)
        return;
    memset(&st, 0, sizeof(st));
    st.mask = LVIF_STATE;
    st.stateMask = LVIS_CUT;
    SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
    g_clip_n = 0;
    g_clip_cut = cut;
    for (int i = 0; i < n; i++) {
        path_of_row(rows[i], g_clip[g_clip_n], sizeof(g_clip[0]));
        if (g_clip[g_clip_n][0])
            g_clip_n++;
        if (cut) {
            st.state = LVIS_CUT;
            SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)rows[i],
                         (LPARAM)&st);
        }
    }
}

/* Paste: copy what is on the clipboard into this folder, and take the
 * originals away if they were cut. */
static void do_paste(void)
{
    int moved = 0;
    if (!g_clip_n)
        return;
    undo_clear();
    for (int i = 0; i < g_clip_n; i++) {
        const char *from = g_clip[i];
        const char *leaf = strrchr(from, FS_SEP);
        char to[PATH_MAX_LEN];
        size_t n, len;
        leaf = leaf ? leaf + 1 : from;
        /* the folder, a separator and the name, as far as the buffer goes */
        n = (size_t)snprintf(to, sizeof(to), "%s%c", g_path, FS_SEP);
        len = strlen(leaf);
        if (n < sizeof(to) - 1) {
            if (len > sizeof(to) - n - 1)
                len = sizeof(to) - n - 1;
            memcpy(to + n, leaf, len);
            to[n + len] = 0;
        }
        if (!strcmp(to, from))
            continue; /* into the folder it is already in */
        if (g_clip_cut) {
            if (fs_rename(from, to)) {
                undo_add("Move", from, to);
                moved++;
            }
        } else if (fs_copy(from, to)) {
            undo_add("Copy", from, to);
            moved++;
        }
    }
    if (g_clip_cut && moved)
        g_clip_n = 0; /* a cut is spent once it has been pasted */
    refresh_view();
}

/* Delete: the shell asks first, and says what it is about to do. */
static void do_delete(void)
{
    int rows[SEL_MAX];
    int n = selected_rows(rows, SEL_MAX);
    char question[600];
    char path[PATH_MAX_LEN], to[PATH_MAX_LEN];
    if (!n)
        return;
    if (n == 1) {
        int dir = g_entry[rows[0]].is_dir;
        snprintf(question, sizeof(question),
                 dir ? "Are you sure you want to remove the folder '%s' and "
                       "move all its contents to the Recycle Bin?"
                     : "Are you sure you want to send '%s' to the Recycle Bin?",
                 g_entry[rows[0]].name);
        if (MessageBoxA(g_main, question,
                        dir ? "Confirm Folder Delete" : "Confirm File Delete",
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    } else {
        snprintf(question, sizeof(question),
                 "Are you sure you want to send these %d items to the "
                 "Recycle Bin?", n);
        if (MessageBoxA(g_main, question, "Confirm Multiple File Delete",
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }
    undo_clear();
    for (int i = 0; i < n; i++) {
        path_of_row(rows[i], path, sizeof(path));
        snprintf(to, sizeof(to), "%s%c%s", recycle_dir(), FS_SEP,
                 g_entry[rows[i]].name);
        if (fs_rename(path, to))
            undo_add("Delete", path, to); /* from where it was, to the bin */
    }
    refresh_view();
}

/* Undo: put back what the last command did, in reverse. */
static void do_undo(void)
{
    if (!g_undo.what)
        return;
    for (int i = g_undo.count - 1; i >= 0; i--) {
        if (!strcmp(g_undo.what, "Copy"))
            fs_delete(g_undo.to[i], 0); /* the copy goes away again */
        else
            fs_rename(g_undo.to[i], g_undo.from[i]); /* back where it was */
    }
    undo_clear();
    refresh_view();
}

/* Rename: the name is typed over where it is drawn. Given a name, the row
 * with that name is the one — which is how a folder just made is left ready
 * to be named. Given none, whatever is picked. */
static void begin_rename_of(const char *name)
{
    int row = -1;
    if (name) {
        for (int i = 0; i < g_entries; i++)
            if (!strcmp(g_entry[i].name, name)) {
                row = i;
                break;
            }
    } else {
        row = (int)SendMessageA(g_list, LVM_GETNEXTITEM, (WPARAM)-1,
                                LVNI_SELECTED);
    }
    if (row < 0)
        return;
    {   /* pick it first, as the shell does, so what is being named shows */
        LVITEMA st;
        memset(&st, 0, sizeof(st));
        st.mask = LVIF_STATE;
        st.state = LVIS_SELECTED | LVIS_FOCUSED;
        st.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
        st.state = 0;
        SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
        st.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)row, (LPARAM)&st);
    }
    SetFocus(g_list);
    SendMessageA(g_list, LVM_EDITLABELA, (WPARAM)row, 0);
}

/* And what to do when the typing is done: the file is renamed, and the view
 * read again so it lands in its new place in the order. */
static int end_rename(int row, const char *name)
{
    char from[PATH_MAX_LEN], to[PATH_MAX_LEN];
    if (row < 0 || row >= g_entries || !name || !name[0])
        return 0;
    if (!strcmp(name, g_entry[row].name))
        return 0; /* the same name is no rename at all */
    path_of_row(row, from, sizeof(from));
    snprintf(to, sizeof(to), "%s%c%s", g_path, FS_SEP, name);
    if (fs_exists(to)) {
        char msg[PATH_MAX_LEN + 200];
        snprintf(msg, sizeof(msg),
                 "Cannot rename %s: A file with the name you specified "
                 "already exists. Specify a different file name.",
                 g_entry[row].name);
        MessageBoxA(g_main, msg, "Error Renaming File or Folder",
                    MB_OK | MB_ICONEXCLAMATION);
        return 0;
    }
    if (!fs_rename(from, to))
        return 0;
    undo_clear();
    undo_add("Rename", from, to);
    refresh_view();
    return 0; /* the view is filled again, so the row's own text is stale */
}

/* What each command does, in the words the machine's status bar uses. The
 * shell keeps these in its resources; this keeps them in a table. */
static const char *command_help(UINT id)
{
    switch (id) {
    case IDM_NEW_FOLDER:
        return "Creates a new, empty folder.";
    case IDM_NEW_SHORTCUT:
        return "Creates a shortcut to an item.";
    case IDM_CREATE_SHORTCUT:
        return "Creates a shortcut to the selected items.";
    case IDM_DELETE:
        return "Deletes the selected items.";
    case IDM_RENAME:
        return "Renames the selected item.";
    case IDM_CTX_PROPERTIES:
        return "Displays the properties of the selected items.";
    case IDM_CLOSE:
        return "Closes the window.";
    case IDM_UNDO:
        return "Undoes the last action.";
    case IDM_CUT:
        return "Removes the selected items and copies them onto the "
               "Clipboard.";
    case IDM_COPY:
        return "Copies the selected items to the Clipboard.";
    case IDM_PASTE:
        return "Inserts the items you have copied or cut into the selected "
               "location.";
    case IDM_PASTE_SHORTCUT:
        return "Inserts shortcuts to the items you have copied or cut.";
    case IDM_COPYTO:
        return "Copies the selected items to a folder you choose.";
    case IDM_MOVETO:
        return "Moves the selected items to a folder you choose.";
    case IDM_SELECT_ALL:
        return "Selects all items in the window.";
    case IDM_INVERT:
        return "Reverses which items are selected and which are not.";
    case IDM_STATUSBAR:
        return "Shows or hides the status bar.";
    case IDM_VIEW_LARGE:
        return "Displays items by using large icons.";
    case IDM_VIEW_SMALL:
        return "Displays items by using small icons.";
    case IDM_VIEW_LIST:
        return "Displays items in a list.";
    case IDM_VIEW_DETAILS:
        return "Displays information about each item in the window.";
    case IDM_VIEW_THUMBS:
        return "Displays items by using thumbnails.";
    case IDM_REFRESH:
    case IDM_CTX_REFRESH:
        return "Refreshes the contents of the window.";
    case IDM_FOLDERS:
        return "Shows or hides the Folders bar.";
    case IDM_BACK:
        return "Goes to the previous page.";
    case IDM_FORWARD:
        return "Goes to the next page.";
    case IDM_UP:
        return "Goes up one level.";
    case IDM_HOME:
        return "Goes to your Home page.";
    case IDM_ABOUT:
        return "Displays program information, version number, and copyright.";
    case IDM_HELP_TOPICS:
        return "Displays Help topics.";
    case IDM_FOLDER_OPTIONS:
        return "Changes the appearance and behavior of files and folders.";
    default:
        return NULL;
    }
}

/* ---- Properties -----------------------------------------------------------
 *
 * What the shell shows about one thing: its name in a box, what kind it is,
 * where it is, how big, when it was written, and the three attributes. Built
 * from a dialog template, so the same source puts it up on either side.
 */
enum {
    IDC_PROP_NAME = 100,
    IDC_PROP_TYPE,
    IDC_PROP_WHERE,
    IDC_PROP_SIZE,
    IDC_PROP_WHEN,
    IDC_PROP_READONLY,
    IDC_PROP_HIDDEN,
    IDC_PROP_ARCHIVE
};

static int g_prop_row = -1; /* the row Properties is about */

static int end_rename(int row, const char *name);

static INT_PTR CALLBACK prop_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_COMMAND) {
        WORD id = LOWORD(wp);
        if (id == IDOK) { /* a name typed in the box is a rename */
            char typed[280];
            GetDlgItemTextA(dlg, IDC_PROP_NAME, typed, (int)sizeof(typed));
            EndDialog(dlg, id);
            if (typed[0])
                end_rename(g_prop_row, typed);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, id);
            return TRUE;
        }
    }
    return FALSE;
}

/* "91.0 KB (93,204 bytes)", which is how the shell says a size. */
static void size_text(unsigned long bytes, char *out, size_t max)
{
    char grouped[32];
    char digits[24];
    int n = snprintf(digits, sizeof(digits), "%lu", bytes);
    int j = 0;
    for (int i = 0; i < n && j < (int)sizeof(grouped) - 2; i++) {
        if (i && (n - i) % 3 == 0)
            grouped[j++] = ',';
        grouped[j++] = digits[i];
    }
    grouped[j] = 0;
    if (bytes >= 1024)
        snprintf(out, max, "%lu.%lu KB (%s bytes)", bytes / 1024,
                 (bytes % 1024) * 10 / 1024, grouped);
    else
        snprintf(out, max, "%s bytes", grouped);
}

static void show_properties(HWND owner)
{
    int row = (int)SendMessageA(g_list, LVM_GETNEXTITEM, (WPARAM)-1,
                                LVNI_SELECTED);
    static char title[300], name[280], type[64], size[64], when[64];
    static char where[sizeof(g_path)];
    unsigned char buf[3000];
    UINT_PTR len;
    dlg_item items[16];
    int n = 0;
    if (row < 0 && g_ctx_row >= 0)
        row = g_ctx_row;
    if (row < 0 || row >= g_entries) {
        MessageBoxA(owner, "Nothing is selected.", "Properties",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    g_prop_row = row;
    snprintf(title, sizeof(title), "%s Properties", g_entry[row].name);
    snprintf(name, sizeof(name), "%s", g_entry[row].name);
    snprintf(type, sizeof(type), "%s", type_of(&g_entry[row]));
    snprintf(where, sizeof(where), "%s", g_path);
    if (g_entry[row].is_dir)
        snprintf(size, sizeof(size), "%s", "");
    else
        size_text(g_entry[row].size, size, sizeof(size));
    snprintf(when, sizeof(when), "%s", g_entry[row].modified);

/* one row of the table, since a dozen of them written out is a wall */
#define ITEM(st, ix, iy, iw, ih, iid, icls, itx)                               \
    do {                                                                       \
        items[n].style = (st);                                                 \
        items[n].x = (short)(ix);                                              \
        items[n].y = (short)(iy);                                              \
        items[n].cx = (short)(iw);                                             \
        items[n].cy = (short)(ih);                                             \
        items[n].id = (WORD)(iid);                                             \
        items[n].cls = (WORD)(icls);                                           \
        items[n].text = (itx);                                                 \
        n++;                                                                   \
    } while (0)
    /* the name, in a box of its own, and then a row per thing */
    /* the name in a box you can type in, as the shell has it */
    ITEM(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER, 46, 8, 148, 12,
         IDC_PROP_NAME, ATOM_EDIT, name);
    ITEM(WS_CHILD | WS_VISIBLE, 8, 30, 40, 9, 0, ATOM_STATIC, "Type:");
    ITEM(WS_CHILD | WS_VISIBLE, 52, 30, 142, 9, IDC_PROP_TYPE, ATOM_STATIC,
         type);
    ITEM(WS_CHILD | WS_VISIBLE, 8, 44, 40, 9, 0, ATOM_STATIC, "Location:");
    ITEM(WS_CHILD | WS_VISIBLE, 52, 44, 142, 9, IDC_PROP_WHERE, ATOM_STATIC,
         where);
    ITEM(WS_CHILD | WS_VISIBLE, 8, 58, 40, 9, 0, ATOM_STATIC, "Size:");
    ITEM(WS_CHILD | WS_VISIBLE, 52, 58, 142, 9, IDC_PROP_SIZE, ATOM_STATIC,
         size);
    ITEM(WS_CHILD | WS_VISIBLE, 8, 76, 40, 9, 0, ATOM_STATIC, "Modified:");
    ITEM(WS_CHILD | WS_VISIBLE, 52, 76, 142, 9, IDC_PROP_WHEN, ATOM_STATIC,
         when);
    ITEM(WS_CHILD | WS_VISIBLE, 8, 96, 40, 9, 0, ATOM_STATIC, "Attributes:");
    ITEM(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 52, 95, 60, 10,
         IDC_PROP_READONLY, ATOM_BUTTON, "&Read-only");
    ITEM(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 52, 108, 60, 10,
         IDC_PROP_HIDDEN, ATOM_BUTTON, "&Hidden");
    ITEM(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 52, 121, 60, 10,
         IDC_PROP_ARCHIVE, ATOM_BUTTON, "A&rchive");
    ITEM(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 84, 142, 50,
         14, IDOK, ATOM_BUTTON, "OK");
    ITEM(WS_CHILD | WS_VISIBLE | WS_TABSTOP, 140, 142, 50, 14, IDCANCEL,
         ATOM_BUTTON, "Cancel");
#undef ITEM
    len = build_dialog_template(buf, sizeof(buf),
                                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                200, 164, title, items, n);
    if (len <= sizeof(buf))
        DialogBoxIndirectParamA(NULL, (LPCDLGTEMPLATEA)buf, owner, prop_proc, 0);
}

/* Select All, and its opposite. */
static void do_select_all(int invert)
{
    int n = (int)SendMessageA(g_list, LVM_GETITEMCOUNT, 0, 0);
    LVITEMA st;
    memset(&st, 0, sizeof(st));
    st.mask = LVIF_STATE;
    st.stateMask = LVIS_SELECTED;
    if (!invert) {
        st.state = LVIS_SELECTED;
        SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&st);
    } else {
        char picked[SEL_MAX];
        int rows[SEL_MAX];
        int m = selected_rows(rows, SEL_MAX);
        memset(picked, 0, sizeof(picked));
        for (int i = 0; i < m; i++)
            if (rows[i] < SEL_MAX)
                picked[rows[i]] = 1;
        for (int i = 0; i < n && i < SEL_MAX; i++) {
            st.state = picked[i] ? 0 : LVIS_SELECTED;
            SendMessageA(g_list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&st);
        }
    }
    SetFocus(g_list);
    status_for_selection(-1);
}

/* Fill the list from a directory, and say in the status bar what is in it. */
/* The arrows are lit by where in the walk we are, which is the only thing
 * that says whether there is anywhere to go. */
static void history_buttons(void)
{
    if (!g_toolbar)
        return;
    SendMessageA(g_toolbar, TB_ENABLEBUTTON, IDM_BACK,
                 MAKELPARAM(g_hist_at > 0, 0));
    SendMessageA(g_toolbar, TB_ENABLEBUTTON, IDM_FORWARD,
                 MAKELPARAM(g_hist_at >= 0 && g_hist_at < g_hist_n - 1, 0));
}

static void history_push(const char *path)
{
    if (g_hist_at >= 0 && !strcmp(g_hist[g_hist_at], path))
        return; /* already there: opening it again is not a step */
    if (g_hist_at + 1 >= HIST_MAX) { /* the oldest falls off the front */
        memmove(g_hist[0], g_hist[1], sizeof(g_hist) - sizeof(g_hist[0]));
        g_hist_at--;
    }
    g_hist_at++;
    snprintf(g_hist[g_hist_at], sizeof(g_hist[0]), "%s", path);
    g_hist_n = g_hist_at + 1;
    history_buttons();
}

static void history_go(int step)
{
    int to = g_hist_at + step;
    if (to < 0 || to >= g_hist_n)
        return;
    g_hist_at = to;
    g_navigating = 1;
    show_directory(g_hist[to]);
    g_navigating = 0;
    history_buttons();
}

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
    if (!g_navigating)
        history_push(g_path);

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

    AppendMenuA(view, MF_STRING, IDM_VIEW_LARGE, "Lar&ge Icons");
    AppendMenuA(view, MF_STRING, IDM_VIEW_SMALL, "S&mall Icons");
    AppendMenuA(view, MF_STRING, IDM_VIEW_LIST, "&List");
    AppendMenuA(view, MF_STRING, IDM_VIEW_DETAILS, "&Details");
    AppendMenuA(view, MF_STRING, IDM_VIEW_THUMBS, "Thu&mbnails");
    /* Details is the view this explorer is in, and a menu says which of a set
     * it is on with a bullet rather than a tick */
    CheckMenuRadioItem(view, 0, 4, 3, MF_BYPOSITION);

    AppendMenuA(arrange, MF_STRING, IDM_ARRANGE_NAME, "by &Name");
    AppendMenuA(arrange, MF_STRING, IDM_ARRANGE_TYPE, "by &Type");
    AppendMenuA(arrange, MF_STRING, IDM_ARRANGE_SIZE, "by &Size");
    AppendMenuA(arrange, MF_STRING, IDM_ARRANGE_DATE, "by &Date");
    AppendMenuA(arrange, MF_SEPARATOR, 0, NULL);
    AppendMenuA(arrange, MF_STRING | MF_GRAYED, IDM_AUTO_ARRANGE,
                "&Auto Arrange");

    AppendMenuA(new_menu, MF_STRING, IDM_NEW_FOLDER, "&Folder");
    AppendMenuA(new_menu, MF_STRING, IDM_NEW_SHORTCUT, "&Shortcut");
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
    AppendMenuA(m, MF_STRING | MF_GRAYED, IDM_LINEUP, "Li&ne Up Icons");
    AppendMenuA(m, MF_STRING, IDM_CTX_REFRESH, "R&efresh");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, IDM_CUSTOMIZE_FOLDER,
                "C&ustomize This Folder...");
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
 * On the machine the menu in a rebar is a toolbar: one drop-down button per
 * title, no arrow beside it, and the shell answers TBN_DROPDOWN with
 * TrackPopupMenu. That is all this is. Everything a menu bar does beyond a
 * toolbar's own — sliding from one drop-down to the next, Alt putting a title
 * under the keyboard, the underlines that come out with it — belongs to the
 * library and happens without this file saying anything.
 */
#define MENUBAR_PAD 16   /* what surrounds a title, as Windows 2000 spaces them */
#define MENUBAR_LEAD 1   /* and the band leads in by one before the first */
#define MENUBAR_H 19     /* a title is a row shorter than the band it is in */
#define MENUBAR_MAX 16   /* titles a bar is built with */
#define MENUBAR_BRAND 38 /* the box at the right the shell animates in */

static HMENU g_menu;

/* One button per top-level menu, labelled as the menu is — marker and all,
 * because the toolbar underlines the letter it marks when the cues show. */
static void build_menubar(void)
{
    static char label[MENUBAR_MAX][64];
    TBBUTTON b[MENUBAR_MAX];
    int n = GetMenuItemCount(g_menu);
    if (n > MENUBAR_MAX)
        n = MENUBAR_MAX;
    memset(b, 0, sizeof(b));
    for (int i = 0; i < n; i++) {
        GetMenuStringA(g_menu, (UINT)i, label[i], (int)sizeof(label[i]),
                       MF_BYPOSITION);
        b[i].iBitmap = -1; /* a title carries no image */
        b[i].idCommand = IDM_MENU_FIRST + i;
        b[i].fsState = TBSTATE_ENABLED;
        /* A drop-down with no arrow half beside it: the bar draws none
         * because it was never asked to, and the whole button is the
         * drop-down. BTNS_WHOLEDROPDOWN would say the same thing and draw an
         * arrow anyway, which a menu title does not have. */
        b[i].fsStyle = BTNS_DROPDOWN;
        b[i].iString = (INT_PTR)label[i];
    }
    SendMessageA(g_menubar, TB_ADDBUTTONSA, (WPARAM)n, (LPARAM)b);
    {   /* Each title is its label and the sixteen that surround it, said
         * outright: a toolbar left to work it out applies whatever padding it
         * has, and the titles walk apart. */
        HDC dc = GetDC(g_menubar);
        HGDIOBJ was = SelectObject(dc, g_font);
        for (int i = 0; i < n; i++) {
            TBBUTTONINFOA bi;
            SIZE sz;
            char plain[64];
            size_t j = 0;
            for (const char *p = label[i]; *p && j < sizeof(plain) - 1; p++)
                if (*p != '&')
                    plain[j++] = *p;
            plain[j] = 0;
            GetTextExtentPoint32A(dc, plain, (int)j, &sz);
            memset(&bi, 0, sizeof(bi));
            bi.cbSize = sizeof(bi);
            bi.dwMask = TBIF_SIZE;
            bi.cx = (WORD)(sz.cx + MENUBAR_PAD);
            SendMessageA(g_menubar, TB_SETBUTTONINFOA,
                         (WPARAM)(IDM_MENU_FIRST + i), (LPARAM)&bi);
        }
        SelectObject(dc, was);
        ReleaseDC(g_menubar, dc);
    }
    /* And again now the titles are in: a bar works its height out from what
     * it holds, so being told before it held anything did not stick. */
    SendMessageA(g_menubar, TB_SETBUTTONSIZE, 0, MAKELPARAM(0, MENUBAR_H));
}

/* A title was pressed, or reached by the keyboard: put its menu under it. */
static void menubar_drop(int index)
{
    RECT r;
    POINT pt;
    if (index < 0 || index >= GetMenuItemCount(g_menu))
        return;
    SendMessageA(g_menubar, TB_GETITEMRECT, (WPARAM)index, (LPARAM)&r);
    pt.x = r.left;
    pt.y = r.bottom;
    ClientToScreen(g_menubar, &pt);
    TrackPopupMenu(GetSubMenu(g_menu, index), TPM_LEFTALIGN, pt.x, pt.y, 0,
                   g_main, NULL);
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
        SelectObject(dc, g_font); /* the shell's face, not the DC's default */
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
         * it, or the keyboard has reached it — a caption button here reads far
         * heavier than the machine. The frame, when it comes, is the glyph
         * inflated six across and five down, raised by one. */
        {
            RECT cr;
            HBRUSH ink = CreateSolidBrush(GetSysColor(COLOR_BTNTEXT));
            int bx, by;
            GetClientRect(w, &cr);
            bx = cr.right - 15;
            by = 6;
            if (GetFocus() == w) {
                RECT b;
                b.left = bx - 6;
                b.top = by - 5;
                b.right = bx + 8 + 6;
                b.bottom = by + 7 + 5;
                DrawEdge(dc, &b, BDR_RAISEDINNER, BF_RECT);
            }
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
    if (msg == WM_KEYDOWN && (wp == VK_SPACE || wp == VK_RETURN)) {
        /* reached by Tab, it is pressed by the keyboard like any button */
        SendMessageA(GetParent(w), WM_COMMAND, MAKEWPARAM(IDM_FOLDERS, 0), 0);
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
            /* The split is a client coordinate, so the pointer has to come
             * back through the client origin: measured off the window rect it
             * carries the sizing frame with it and the splitter jumps by four
             * the moment it is picked up. */
            RECT wr;
            POINT pt;
            GetWindowRect(w, &wr);
            pt.x = wr.left + GET_X_LPARAM(lp);
            pt.y = wr.top + GET_Y_LPARAM(lp);
            ScreenToClient(g_main, &pt);
            g_split_x = pt.x;
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

    AppendMenuA(newmenu, MF_STRING, IDM_NEW_FOLDER, "&Folder");
    AppendMenuA(newmenu, MF_STRING, IDM_NEW_SHORTCUT, "&Shortcut");
    AppendMenuA(file, MF_POPUP, (UINT_PTR)newmenu, "Ne&w");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, IDM_CREATE_SHORTCUT, "Create &Shortcut");
    AppendMenuA(file, MF_STRING, IDM_DELETE, "&Delete");
    AppendMenuA(file, MF_STRING, IDM_RENAME, "Rena&me");
    AppendMenuA(file, MF_STRING, IDM_CTX_PROPERTIES, "P&roperties");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, IDM_CLOSE, "&Close");

    /* Undo is named after what it would undo — "Undo Delete", "Undo Rename" —
     * and WM_INITMENU writes that name in. */
    AppendMenuA(edit, MF_STRING, IDM_UNDO, "&Undo\tCtrl+Z");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING, IDM_CUT, "Cu&t\tCtrl+X");
    AppendMenuA(edit, MF_STRING, IDM_COPY, "&Copy\tCtrl+C");
    AppendMenuA(edit, MF_STRING, IDM_PASTE, "&Paste\tCtrl+V");
    AppendMenuA(edit, MF_STRING, IDM_PASTE_SHORTCUT, "Paste &Shortcut");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING, IDM_COPYTO, "Copy To F&older...");
    AppendMenuA(edit, MF_STRING, IDM_MOVETO, "Mo&ve To Folder...");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING, IDM_SELECT_ALL, "Select &All\tCtrl+A");
    AppendMenuA(edit, MF_STRING, IDM_INVERT, "&Invert Selection");

    AppendMenuA(toolbars, MF_STRING | MF_CHECKED, IDM_TOOLBAR_STD,
                "&Standard Buttons");
    AppendMenuA(toolbars, MF_STRING | MF_CHECKED, IDM_TOOLBAR_ADDR,
                "&Address Bar");
    AppendMenuA(toolbars, MF_STRING, IDM_TOOLBAR_LINKS, "&Links");
    AppendMenuA(toolbars, MF_SEPARATOR, 0, NULL);
    AppendMenuA(toolbars, MF_STRING, IDM_TOOLBAR_CUSTOMIZE, "&Customize...");
    AppendMenuA(bars, MF_STRING, IDM_BAR_SEARCH, "&Search");
    AppendMenuA(bars, MF_STRING, IDM_BAR_FAVORITES, "&Favorites");
    AppendMenuA(bars, MF_STRING, IDM_BAR_HISTORY, "&History");
    AppendMenuA(bars, MF_STRING | MF_CHECKED, IDM_FOLDERS, "F&olders");
    AppendMenuA(bars, MF_SEPARATOR, 0, NULL);
    AppendMenuA(bars, MF_STRING, IDM_BAR_TIP, "&Tip of the Day");
    AppendMenuA(arrange, MF_STRING, IDM_ARRANGE_NAME, "by &Name");
    AppendMenuA(arrange, MF_STRING, IDM_ARRANGE_TYPE, "by &Type");
    AppendMenuA(arrange, MF_STRING, IDM_ARRANGE_SIZE, "by &Size");
    AppendMenuA(arrange, MF_STRING, IDM_ARRANGE_DATE, "by &Date");
    AppendMenuA(arrange, MF_SEPARATOR, 0, NULL);
    AppendMenuA(arrange, MF_STRING | MF_GRAYED, IDM_AUTO_ARRANGE,
                "&Auto Arrange");
    AppendMenuA(goto_menu, MF_STRING, IDM_BACK, "&Back\tAlt+Left Arrow");
    AppendMenuA(goto_menu, MF_STRING, IDM_FORWARD,
                "&Forward\tAlt+Right Arrow");
    AppendMenuA(goto_menu, MF_STRING, IDM_UP, "&Up One Level");
    AppendMenuA(goto_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(goto_menu, MF_STRING, IDM_HOME, "&Home Page\tAlt+Home");
    AppendMenuA(view, MF_POPUP, (UINT_PTR)toolbars, "&Toolbars");
    AppendMenuA(view, MF_STRING | MF_CHECKED, IDM_STATUSBAR, "Status &Bar");
    AppendMenuA(view, MF_POPUP, (UINT_PTR)bars, "&Explorer Bar");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, IDM_VIEW_LARGE, "Lar&ge Icons");
    AppendMenuA(view, MF_STRING, IDM_VIEW_SMALL, "S&mall Icons");
    AppendMenuA(view, MF_STRING, IDM_VIEW_LIST, "&List");
    AppendMenuA(view, MF_STRING, IDM_VIEW_DETAILS, "&Details");
    AppendMenuA(view, MF_STRING, IDM_VIEW_THUMBS, "Thu&mbnails");
    CheckMenuRadioItem(view, 4, 8, 7, MF_BYPOSITION);
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_POPUP, (UINT_PTR)arrange, "Arrange &Icons");
    AppendMenuA(view, MF_STRING | MF_GRAYED, IDM_LINEUP, "Line &Up Icons");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING, IDM_CHOOSE_COLUMNS, "&Choose Columns...");
    AppendMenuA(view, MF_STRING, IDM_CUSTOMIZE_FOLDER,
                "Customi&ze This Folder...");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_POPUP, (UINT_PTR)goto_menu, "&Go To");
    AppendMenuA(view, MF_STRING, IDM_REFRESH, "&Refresh");

    AppendMenuA(links, MF_STRING | MF_GRAYED, 0, "(empty)");
    AppendMenuA(media, MF_STRING | MF_GRAYED, 0, "(empty)");
    AppendMenuA(favorites, MF_STRING, IDM_FAV_ADD, "&Add to Favorites...");
    AppendMenuA(favorites, MF_STRING, IDM_FAV_ORGANIZE,
                "&Organize Favorites...");
    AppendMenuA(favorites, MF_SEPARATOR, 0, NULL);
    AppendMenuA(favorites, MF_POPUP, (UINT_PTR)links, "Links");
    AppendMenuA(favorites, MF_POPUP, (UINT_PTR)media, "Media");
    AppendMenuA(favorites, MF_STRING, IDM_FAV_MSN, "MSN");
    AppendMenuA(favorites, MF_STRING, IDM_FAV_RADIO, "Radio Station Guide");
    AppendMenuA(favorites, MF_STRING, IDM_FAV_WEB, "Web Events");

    AppendMenuA(tools, MF_STRING, IDM_MAP_DRIVE, "&Map Network Drive...");
    AppendMenuA(tools, MF_STRING, IDM_DISCONNECT,
                "&Disconnect Network Drive...");
    AppendMenuA(tools, MF_STRING, IDM_SYNCHRONIZE, "&Synchronize...");
    AppendMenuA(tools, MF_SEPARATOR, 0, NULL);
    AppendMenuA(tools, MF_STRING, IDM_FOLDER_OPTIONS, "F&older Options...");

    AppendMenuA(help, MF_STRING, IDM_HELP_TOPICS, "&Help Topics");
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
     * the screenshot has a gripper to the left of File. The bar itself is
     * built in build_bands, out of this menu's titles. */
    g_menu = bar;
    (void)w;
}

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

/* The same five, at the size the Icons view draws: an .ico carries more than
 * one picture and the loader picks the one asked for, so this is the same art
 * a size up. Only the file kinds are here — a toolbar's glyphs are drawn at
 * sixteen and there is nothing bigger of them to have. */
static HIMAGELIST build_big_images(void)
{
    static const char *names[] = {
        ICON_FOLDER, ICON_FOLDER_OPEN, ICON_FILE, ICON_COMPUTER, ICON_DRIVE
    };
    HIMAGELIST il = ImageList_Create(32, 32, ILC_MASK, IMG_COUNT, 4);
    for (int i = 0; i < (int)(sizeof(names) / sizeof(*names)); i++) {
        char path[600];
        HICON icon;
        snprintf(path, sizeof(path), "%s/%s.ico", asset_dir(), names[i]);
        icon = (HICON)LoadImageA(NULL, path, IMAGE_ICON, 32, 32,
                                 LR_LOADFROMFILE);
        if (icon) {
            ImageList_AddIcon(il, icon);
            DestroyIcon(icon);
        } else {
            ImageList_Add(il, NULL, NULL); /* a hole, so the numbers agree */
        }
    }
    return il;
}

static void load_icons(void)
{
    int missing = 0;
    g_images = build_images(GLYPHS, &missing);
    g_hot_images = build_images(GLYPHS_HOT, &missing);
    g_big_images = build_big_images();
    if (missing)
        fprintf(stderr,
                "explorer: %d of the icons are missing — looked in \"%s\". "
                "Set WEEN32_ASSETS to the assets/icons directory.\n",
                missing / 2, asset_dir()[0] ? asset_dir() : "assets/icons");
}

static void build_bands(HWND w)
{
    TBBUTTON b[14];
    REBARBANDINFOA bi;
    int n = 0;

    /* CONTROLPARENT on the two windows between the frame and the address
     * bar: Tab walks into a container that wears it rather than over it,
     * which is how the address bar joins the ring the panes are in. */
    g_rebar = CreateWindowExA(WS_EX_CONTROLPARENT, REBARCLASSNAMEA, "",
                              WS_CHILD | WS_VISIBLE | RBS_BANDBORDERS, 0, 0,
                              100, 50, w, (HMENU)(UINT_PTR)ID_REBAR, NULL,
                              NULL);
    /* A toolbar left alone puts itself at the top of its parent and takes the
     * whole width — it is a control bar, and that is what one does. Inside a
     * rebar band the band says where it goes, so this one must not. */
    g_toolbar = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT |
                                    TBSTYLE_LIST | CCS_NORESIZE |
                                    CCS_NODIVIDER | CCS_NOPARENTALIGN,
                                0, 0, 100, 22, g_rebar,
                                (HMENU)(UINT_PTR)ID_TOOLBAR, NULL, NULL);
    SendMessageA(g_toolbar, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageA(g_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    /* Back, Forward and Views wear the arrow that says they drop down, which
     * a toolbar draws only when asked: without this the whole button is the
     * drop-down, which is what a menu title is and these are not. */
    SendMessageA(g_toolbar, TB_SETEXTENDEDSTYLE, 0,
                 (LPARAM)TBSTYLE_EX_DRAWDDARROWS);
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
        /* Every button is given the width the machine's shell gives it,
         * rather than left to whatever padding the toolbar would have
         * applied: a labelled one is twenty-four, its label and seven, with
         * five instead of the seven and thirteen more when an arrow follows;
         * an image on its own is six, the image and two, and fourteen more
         * with an arrow. Said outright, the bar comes out the same width
         * whichever library lays it out. The views button is the shell's own
         * exception at thirty-one. */
        HDC dc = GetDC(g_toolbar);
        HGDIOBJ was = SelectObject(dc, g_font);
        for (int i = 0; i < n; i++) {
            TBBUTTONINFOA bi;
            int drop = (b[i].fsStyle & TBSTYLE_DROPDOWN) != 0;
            int cx;
            if (b[i].fsStyle & TBSTYLE_SEP) {
                /* the machine's separator is six, where a toolbar's own is
                 * eight: said outright like the buttons */
                memset(&bi, 0, sizeof(bi));
                bi.cbSize = sizeof(bi);
                bi.dwMask = TBIF_SIZE | TBIF_BYINDEX;
                bi.cx = 6;
                SendMessageA(g_toolbar, TB_SETBUTTONINFOA, (WPARAM)i,
                             (LPARAM)&bi);
                continue;
            }
            if (b[i].iString) {
                SIZE sz;
                const char *t = (const char *)b[i].iString;
                GetTextExtentPoint32A(dc, t, (int)strlen(t), &sz);
                cx = 24 + sz.cx + (drop ? 5 + 13 : 7);
            } else {
                cx = 6 + 16 + 2 + (drop ? 12 : 0);
            }
            if (b[i].idCommand == IDM_VIEWS)
                cx = 31;
            memset(&bi, 0, sizeof(bi));
            bi.cbSize = sizeof(bi);
            bi.dwMask = TBIF_SIZE;
            bi.cx = (WORD)cx;
            SendMessageA(g_toolbar, TB_SETBUTTONINFOA, (WPARAM)b[i].idCommand,
                         (LPARAM)&bi);
        }
        SelectObject(dc, was);
        ReleaseDC(g_toolbar, dc);
    }

    g_addrband = CreateWindowExA(WS_EX_CONTROLPARENT, "exploreraddr", "",
                                 WS_CHILD | WS_VISIBLE, 0, 0, 300, 22, g_rebar,
                                 (HMENU)(UINT_PTR)ID_ADDRBAND, NULL, NULL);
    g_address = CreateWindowExA(WS_EX_CLIENTEDGE, WC_COMBOBOXEXA, "",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    CBS_DROPDOWNLIST, 0, 0,
                                300, 21, g_addrband,
                                (HMENU)(UINT_PTR)ID_ADDRESS, NULL, NULL);
    g_go = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                           WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST |
                               CCS_NORESIZE | CCS_NODIVIDER | CCS_NOPARENTALIGN,
                           0, 0, GO_W, 22, g_addrband,
                           (HMENU)(UINT_PTR)ID_GO, NULL, NULL);
    SendMessageA(g_go, WM_SETFONT, (WPARAM)g_font, TRUE);
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
    SendMessageA(g_address, CBEM_SETIMAGELIST, 0, (LPARAM)g_images);
    /* The field is as tall as the band leaves it, not as tall as the font
     * would have made it: the address bar is twenty-one in a band of
     * twenty-two, which is what the machine has. */
    SendMessageA(g_address, CB_SETITEMHEIGHT, (WPARAM)-1, 15);

    /* The menu bar: a flat toolbar of drop-down buttons, one per title, with
     * no arrow beside them — which is what makes the whole button the
     * drop-down. Sixteen pixels surround a title and it stands a row shorter
     * than the band, both as Windows 2000 has them. */
    g_menubar = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT |
                                    TBSTYLE_LIST | CCS_NORESIZE |
                                    CCS_NODIVIDER | CCS_NOPARENTALIGN,
                                0, 0, 100, 22, g_rebar,
                                (HMENU)(UINT_PTR)ID_MENUBAR, NULL, NULL);
    SendMessageA(g_menubar, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageA(g_menubar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    /* No images on this one: said in so many words, or a title reserves the
     * room a picture would have taken and the bar comes out twice as wide. */
    SendMessageA(g_menubar, TB_SETBITMAPSIZE, 0, MAKELPARAM(0, 0));
    SendMessageA(g_menubar, TB_SETPADDING, 0, MAKELPARAM(MENUBAR_PAD, 0));
    SendMessageA(g_menubar, TB_SETBUTTONSIZE, 0, MAKELPARAM(0, MENUBAR_H));
    SendMessageA(g_menubar, TB_SETINDENT, MENUBAR_LEAD, 0);
    build_menubar();
    g_brand = CreateWindowA("explorerbrand", "", WS_CHILD | WS_VISIBLE, 0, 2,
                            BRAND_W, BRAND_H, g_rebar,
                            (HMENU)(UINT_PTR)ID_BRAND, NULL, NULL);

    /* Three bands, each on a row of its own: bands share a row unless one
     * asks to break, and the shell's three bars are stacked. */
    memset(&bi, 0, sizeof(bi));
    bi.cbSize = sizeof(bi);
    bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE;
    bi.fStyle = RBBS_BREAK;
    bi.hwndChild = g_menubar;
    bi.cyMinChild = 22;
    SendMessageA(g_rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);

    bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE;
    bi.fStyle = RBBS_BREAK;
    bi.hwndChild = g_toolbar;
    bi.cyMinChild = 22;
    SendMessageA(g_rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);

    bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE | RBBIM_TEXT;
    bi.fStyle = RBBS_BREAK;
    bi.hwndChild = g_addrband;
    bi.cyMinChild = 22;
    bi.lpText = (char *)"Address";
    SendMessageA(g_rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);
}

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

    /* A tab stop for the cross in it, which the machine's ring holds between
     * the address bar and the tree. */
    g_panehead = CreateWindowA("explorerpane", "",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10,
                               w, (HMENU)(UINT_PTR)ID_PANEHEAD, NULL, NULL);
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
                                 LVS_SHOWSELALWAYS | LVS_EDITLABELS,
                             0, 0, 10, 10, w, (HMENU)(UINT_PTR)ID_LIST, NULL,
                             NULL);

    SendMessageA(g_tree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)g_images);
    SendMessageA(g_list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_images);
    SendMessageA(g_list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)g_big_images);
    /* Both panes draw in the shell's face, which is the window's, not
     * whatever the control would have picked for itself. */
    SendMessageA(g_tree, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageA(g_list, WM_SETFONT, (WPARAM)g_font, TRUE);

    for (int i = 0; i < 4; i++) {
        LVCOLUMNA col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = (char *)columns[i].title;
        col.cx = columns[i].width;
        col.fmt = columns[i].fmt;
        SendMessageA(g_list, LVM_INSERTCOLUMNA, (WPARAM)i, (LPARAM)&col);
    }

    /* Left to put itself along the bottom and to be as tall as its font
     * wants, which is what a status bar is for. */
    g_status = CreateWindowA(STATUSCLASSNAMEA, "",
                             WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 10,
                             10, w, (HMENU)(UINT_PTR)ID_STATUS, NULL, NULL);
    SendMessageA(g_status, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageA(g_rebar, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_address)
        SendMessageA(g_address, WM_SETFONT, (WPARAM)g_font, TRUE);
}

static LRESULT CALLBACK explorer_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_main = w;
        build_menu(w);
        build_context_menus();
        load_icons();
        build_bands(w);
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
        /* Laid out before anything is put in it: the status bar's parts have
         * to exist before text can go in them, and the first WM_SIZE is
         * after this. */
        layout(w);
        if (g_fixture) {
            fill_fixture_tree();
        } else {
            HTREEITEM root = add_node(NULL, "/", IMG_COMPUTER, IMG_COMPUTER, 1);
            SendMessageA(g_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)root);
        }
        show_directory(g_start[0] ? g_start : home_path());
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
                /* The header is where the header is, not "above the first
                 * row": asked that way an empty folder has no header at all
                 * and its columns cannot be reached. */
                HWND head = (HWND)(INT_PTR)SendMessageA(g_list, LVM_GETHEADER,
                                                        0, 0);
                int in_header = 0;
                g_ctx_row = -1;
                if (head) {
                    RECT hr;
                    POINT top, bottom;
                    GetWindowRect(head, &hr);
                    top.x = hr.left;
                    top.y = hr.top;
                    bottom.x = hr.right;
                    bottom.y = hr.bottom;
                    ScreenToClient(g_list, &top);
                    ScreenToClient(g_list, &bottom);
                    in_header = hi.pt.y >= top.y && hi.pt.y < bottom.y;
                }
                menu = in_header ? g_col_menu : g_back_menu;
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
        } else if (nm->code == NM_DBLCLK && nm->hwndFrom == g_list) {
            /* opening a folder is what a double click does in a shell — in
             * the list. The tree answers its own double click by opening the
             * branch, and NM_DBLCLK carries no control in its code, so the
             * one that sent it has to be asked for. */
            int sel = (int)SendMessageA(g_list, LVM_GETNEXTITEM, (WPARAM)-1,
                                        LVNI_SELECTED);
            if (sel >= 0 && sel < g_entries && g_entry[sel].is_dir) {
                char path[1400];
                snprintf(path, sizeof(path), "%s%s%s", g_path,
                         strcmp(g_path, "/") ? "/" : "", g_entry[sel].name);
                show_directory(path);
            }
        } else if (nm->code == TBN_DROPDOWN && nm->hwndFrom == g_menubar) {
            /* A title of the menu bar was pressed, or the keyboard opened it.
             * The bar keeps the button pushed in and slides to the next one
             * by itself; all this has to do is put the menu under it. */
            menubar_drop(((const NMTOOLBAR *)lp)->iItem - IDM_MENU_FIRST);
        } else if (nm->code == TBN_DROPDOWN && nm->hwndFrom == g_toolbar) {
            /* The arrows beside Back, Forward and Views. Views offers the
             * five the View menu does; the other two offer where they would
             * go, which is the walk this window has made. */
            int id = ((const NMTOOLBAR *)lp)->iItem;
            RECT r;
            POINT pt;
            HMENU m = NULL;
            SendMessageA(g_toolbar, TB_GETITEMRECT,
                         (WPARAM)SendMessageA(g_toolbar, TB_COMMANDTOINDEX,
                                              (WPARAM)id, 0),
                         (LPARAM)&r);
            pt.x = r.left;
            pt.y = r.bottom;
            ClientToScreen(g_toolbar, &pt);
            if (id == IDM_VIEWS) {
                static const char *names[] = { "Lar&ge Icons", "S&mall Icons",
                                               "&List", "&Details",
                                               "Thu&mbnails" };
                m = CreatePopupMenu();
                for (int i = 0; i < 5; i++)
                    AppendMenuA(m, MF_STRING, (UINT)(IDM_VIEW_LARGE + i),
                                names[i]);
                CheckMenuRadioItem(m, 0, 4, (UINT)g_view, MF_BYPOSITION);
            } else {
                /* where Back and Forward would take you, most recent first */
                int from = id == IDM_BACK ? g_hist_at - 1 : g_hist_at + 1;
                int step = id == IDM_BACK ? -1 : 1;
                m = CreatePopupMenu();
                for (int i = from, n = 0; i >= 0 && i < g_hist_n && n < 9;
                     i += step, n++) {
                    const char *leaf = strrchr(g_hist[i], FS_SEP);
                    AppendMenuA(m, MF_STRING, (UINT)(IDM_HIST_FIRST + i),
                                leaf && leaf[1] ? leaf + 1 : g_hist[i]);
                }
                if (!GetMenuItemCount(m))
                    AppendMenuA(m, MF_STRING | MF_GRAYED, 0, "(none)");
            }
            {
                UINT cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RETURNCMD,
                                          pt.x, pt.y, 0, g_main, NULL);
                DestroyMenu(m);
                if (cmd)
                    SendMessageA(g_main, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0),
                                 0);
            }
        } else if (nm->code == LVN_ENDLABELEDITA) {
            const NMLVDISPINFOA *di = (const NMLVDISPINFOA *)lp;
            return end_rename(di->item.iItem, di->item.pszText);
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

    case WM_INITMENU:
    case WM_INITMENUPOPUP: {
        /* What can be done depends on what is picked, and a menu says so
         * before it is shown. Undo carries the name of what it would undo,
         * which is what the shell's "Undo Delete" is. A bar sends the first
         * of these and one drop-down the second; either way what arrives is
         * a menu to go through, and an item that is not in it is not
         * disturbed. */
        HMENU bar = (HMENU)wp;
        int picked = (int)SendMessageA(g_list, LVM_GETSELECTEDCOUNT, 0, 0);
        UINT on = MF_BYCOMMAND | MF_ENABLED, off = MF_BYCOMMAND | MF_GRAYED;
        char undo[64];
        if (!bar)
            return 0;
        EnableMenuItem(bar, IDM_DELETE, picked ? on : off);
        EnableMenuItem(bar, IDM_RENAME, picked == 1 ? on : off);
        EnableMenuItem(bar, IDM_CREATE_SHORTCUT, picked ? on : off);
        EnableMenuItem(bar, IDM_CTX_PROPERTIES, picked ? on : off);
        EnableMenuItem(bar, IDM_CUT, picked ? on : off);
        EnableMenuItem(bar, IDM_COPY, picked ? on : off);
        EnableMenuItem(bar, IDM_COPYTO, picked ? on : off);
        EnableMenuItem(bar, IDM_MOVETO, picked ? on : off);
        EnableMenuItem(bar, IDM_PASTE, g_clip_n ? on : off);
        EnableMenuItem(bar, IDM_PASTE_SHORTCUT, off);
        EnableMenuItem(bar, IDM_UNDO, g_undo.what ? on : off);
        EnableMenuItem(bar, IDM_BACK, g_hist_at > 0 ? on : off);
        EnableMenuItem(bar, IDM_FORWARD,
                       g_hist_at >= 0 && g_hist_at < g_hist_n - 1 ? on : off);
        snprintf(undo, sizeof(undo), "&Undo%s%s\tCtrl+Z",
                 g_undo.what ? " " : "", g_undo.what ? g_undo.what : "");
        ModifyMenuA(bar, IDM_UNDO, MF_BYCOMMAND | MF_STRING, IDM_UNDO, undo);
        /* and the view it is in carries the bullet */
        CheckMenuRadioItem(bar, IDM_VIEW_LARGE, IDM_VIEW_THUMBS,
                           (UINT)(IDM_VIEW_LARGE + g_view), MF_BYCOMMAND);
        return 0;
    }
    case WM_MENUSELECT: {
        /* The status bar says what the highlighted item does, which is what
         * the first part of a shell's status bar is for while a menu is up. */
        UINT id = LOWORD(wp), flags = HIWORD(wp);
        const char *help = NULL;
        if (!g_status)
            return 0;
        if (id == 0xffff && flags == 0xffff) {
            status_for_selection(
                (int)SendMessageA(g_list, LVM_GETNEXTITEM, (WPARAM)-1,
                                  LVNI_SELECTED));
            return 0;
        }
        if (flags & MF_POPUP) {
            /* a submenu is named by its place, not by a command, so what it
             * is has to be read back off the menu it is in */
            static const struct {
                const char *label, *help;
            } subs[] = {
                { "New", "Contains commands for creating new items." },
                { "Toolbars", "Shows or hides toolbars." },
                { "Explorer Bar", "Shows or hides an Explorer bar." },
                { "Arrange Icons", "Arranges items in the window." },
                { "Go To", "Goes to another page." },
                { "Send To", "Sends the selected items to another place." },
            };
            char label[64], plain[64];
            size_t j = 0;
            GetMenuStringA((HMENU)lp, id, label, (int)sizeof(label),
                           MF_BYPOSITION);
            for (const char *q = label; *q && j < sizeof(plain) - 1; q++)
                if (*q != '&')
                    plain[j++] = *q;
            plain[j] = 0;
            help = "Contains commands for working with the selected items.";
            for (size_t k = 0; k < sizeof(subs) / sizeof(*subs); k++)
                if (!strcmp(plain, subs[k].label))
                    help = subs[k].help;
        }
        else if (flags & MF_SEPARATOR)
            help = "";
        else
            help = command_help(id);
        SendMessageA(g_status, SB_SETTEXTA, 0,
                     (LPARAM)(help ? help : ""));
        return 0;
    }

    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) {
            /* Alt, F10 or Alt and a letter. This window's menu is not the
             * frame's, so the frame cannot answer for it: the bar takes the
             * keyboard, the cues come out, and the title the letter marks —
             * or the first one — goes under it. */
            int hit = 0;
            if (lp && !SendMessageA(g_menubar, TB_MAPACCELERATORA, (WPARAM)lp,
                                    (LPARAM)&hit))
                return 1; /* no title marks it: let a control have the letter */
            SendMessageA(g_menubar, WM_CHANGEUISTATE,
                         MAKEWPARAM(UIS_CLEAR, UISF_HIDEACCEL | UISF_HIDEFOCUS),
                         0);
            SetFocus(g_menubar);
            SendMessageA(g_menubar, TB_SETHOTITEM, (WPARAM)hit, 0);
            if (lp) /* asked for by name, so it opens */
                SendMessageA(g_menubar, WM_KEYDOWN, VK_DOWN, 0);
            return 0;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_BACK:
            history_go(-1);
            return 0;
        case IDM_FORWARD:
            history_go(1);
            return 0;
        case IDM_CLOSE:
            DestroyWindow(w);
            return 0;

        /* ---- Edit, and the toolbar buttons that do the same things ---- */
        case IDM_CUT:
            do_clip(1);
            return 0;
        case IDM_COPY:
            do_clip(0);
            return 0;
        case IDM_PASTE:
            do_paste();
            return 0;
        case IDM_UNDO:
            do_undo();
            return 0;
        case IDM_SELECT_ALL:
            do_select_all(0);
            return 0;
        case IDM_INVERT:
            do_select_all(1);
            return 0;
        case IDM_DELETE:
            do_delete();
            return 0;
        case IDM_MOVETO:
        case IDM_COPYTO:
            /* The shell puts up a folder picker for these. Until there is
             * one, they are the clipboard pair with the paste left to the
             * folder you go to — which is what they amount to. */
            do_clip(LOWORD(wp) == IDM_MOVETO);
            return 0;

        /* ---- File ---- */
        case IDM_NEW_FOLDER: {
            /* "New Folder", or "New Folder (2)" when that is taken, and then
             * its name is there to be typed over — which is what the shell
             * leaves you with. */
            char name[64] = "New Folder", full[PATH_MAX_LEN];
            for (int n = 2; n < 100; n++) {
                snprintf(full, sizeof(full), "%s%c%s", g_path, FS_SEP, name);
                if (!fs_exists(full))
                    break;
                snprintf(name, sizeof(name), "New Folder (%d)", n);
            }
            if (fs_mkdir(full)) {
                undo_clear();
                undo_add("New", full, full);
                refresh_view();
                begin_rename_of(name);
            }
            return 0;
        }
        case IDM_RENAME:
            begin_rename_of(NULL);
            return 0;

        /* ---- View ---- */
        case IDM_REFRESH:
        case IDM_CTX_REFRESH:
            refresh_view();
            return 0;
        case IDM_STATUSBAR:
            g_show_status = !g_show_status;
            ShowWindow(g_status, g_show_status ? SW_SHOW : SW_HIDE);
            CheckMenuItem(GetSubMenu(g_menu, 2), IDM_STATUSBAR,
                          g_show_status ? MF_CHECKED : MF_UNCHECKED);
            layout(w);
            InvalidateRect(w, NULL, TRUE);
            return 0;
        case IDM_SEARCH:
        case IDM_BAR_SEARCH:
        case IDM_BAR_FAVORITES:
        case IDM_BAR_HISTORY:
        case IDM_HISTORY:
            /* The bars a shell can put beside the list. Only one of them is
             * built — Folders — so the others say so rather than doing
             * nothing at all. */
            MessageBoxA(w,
                        "This Explorer bar is not part of the example.\n"
                        "Folders, under the same menu, is.",
                        "Explorer Bar", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_HOME:
            show_directory(home_path());
            return 0;
        case IDM_VIEW_LARGE:
        case IDM_VIEW_SMALL:
        case IDM_VIEW_LIST:
        case IDM_VIEW_DETAILS:
        case IDM_VIEW_THUMBS: {
            /* The five the menu offers are four the list view has, with
             * Thumbnails shown as big icons — which is what it is without the
             * pictures being made from the files. */
            static const DWORD mode[] = { LVS_ICON, LVS_SMALLICON, LVS_LIST,
                                          LVS_REPORT, LVS_ICON };
            g_view = LOWORD(wp) - IDM_VIEW_LARGE;
            SetWindowLongA(g_list, GWL_STYLE,
                           (GetWindowLongA(g_list, GWL_STYLE) & ~LVS_TYPEMASK) |
                               mode[g_view]);
            SendMessageA(g_toolbar, TB_CHECKBUTTON, IDM_VIEWS, 0);
            InvalidateRect(g_list, NULL, TRUE);
            return 0;
        }
        case IDM_ARRANGE_NAME:
        case IDM_ARRANGE_TYPE:
        case IDM_ARRANGE_SIZE:
        case IDM_ARRANGE_DATE: {
            /* the four orders are the four columns, in the same order */
            static const int col[] = { 0, 2, 1, 3 };
            g_sort_col = col[LOWORD(wp) - IDM_ARRANGE_NAME];
            g_sort_down = 0;
            mark_sorted_column();
            fill_list();
            return 0;
        }
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
            /* through a copy: show_directory writes the path it is given back
             * into g_path, and a string may not be copied over itself */
            char up[sizeof(g_path)];
            char *slash;
            strncpy(up, g_path, sizeof(up) - 1);
            up[sizeof(up) - 1] = 0;
            slash = strrchr(up, '/');
            if (slash && slash != up)
                *slash = 0;
            else
                strcpy(up, "/");
            show_directory(up);
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
        case IDM_CTX_PROPERTIES:
            show_properties(w);
            return 0;
        /* ---- the ones that are a notice rather than a thing done ---- */
        case IDM_MAP_DRIVE:
        case IDM_DISCONNECT:
        case IDM_SYNCHRONIZE:
            MessageBoxA(w,
                        "This example browses the file system it is running "
                        "on.\nThere are no network drives to map.",
                        "Map Network Drive", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_FOLDER_OPTIONS:
        case IDM_CHOOSE_COLUMNS:
        case IDM_CUSTOMIZE_FOLDER:
        case IDM_TOOLBAR_CUSTOMIZE:
            MessageBoxA(w,
                        "The shell keeps these in property sheets, which this "
                        "example does not build.\nView > Details and the "
                        "column dividers do most of what they offer.",
                        "Options", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_FAV_ADD:
        case IDM_FAV_ORGANIZE:
        case IDM_FAV_MSN:
        case IDM_FAV_RADIO:
        case IDM_FAV_WEB:
            MessageBoxA(w,
                        "Favorites are the browser's, and this window is the "
                        "file half of the shell.",
                        "Favorites", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_HELP_TOPICS:
            MessageBoxA(w,
                        "The menus do what they say. Try New > Folder, then "
                        "type a name;\nF2 renames, Delete asks first, and "
                        "Edit > Undo puts back what the last command did.",
                        "Explorer Help", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_NEW_SHORTCUT:
        case IDM_CREATE_SHORTCUT:
        case IDM_PASTE_SHORTCUT:
            MessageBoxA(w,
                        "A shortcut is a .lnk file, which is a shell format "
                        "rather than a window one.",
                        "Create Shortcut", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_TOOLBAR_STD:
        case IDM_TOOLBAR_ADDR: {
            /* the two bars that are there can be shown and hidden */
            int std = LOWORD(wp) == IDM_TOOLBAR_STD;
            int *flag = std ? &g_show_toolbar : &g_show_address;
            HWND band = std ? g_toolbar : g_addrband;
            *flag = !*flag;
            ShowWindow(band, *flag ? SW_SHOW : SW_HIDE);
            CheckMenuItem(GetSubMenu(g_menu, 2), LOWORD(wp),
                          *flag ? MF_CHECKED : MF_UNCHECKED);
            SendMessageA(g_rebar, RB_SHOWBAND, (WPARAM)(std ? 1 : 2),
                         (LPARAM)*flag);
            layout(w);
            InvalidateRect(w, NULL, TRUE);
            return 0;
        }
        case IDM_ABOUT:
            MessageBoxA(w, "ween32 — a win32 for the rest of us.",
                        "About Windows", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        /* a step of the walk, picked from Back's or Forward's own list */
        if (LOWORD(wp) >= IDM_HIST_FIRST &&
            LOWORD(wp) < IDM_HIST_FIRST + HIST_MAX) {
            history_go((int)LOWORD(wp) - IDM_HIST_FIRST - g_hist_at);
            return 0;
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
    if (argc > 1 && argv[1]) /* a folder to open in, as a shell takes one */
        snprintf(g_start, sizeof(g_start), "%s", argv[1]);
    /* the fixed listing, for putting this window beside a screenshot of the
     * machine it is a reimplementation of */
    g_fixture = getenv("WEEN32_EXPLORER_FIXTURE") != NULL;

#ifdef _WIN32
    /* Built as a console app so that main() serves both worlds; on Windows
     * this is a GUI program, so drop the console window it came with. */
    FreeConsole();
#endif
    /* The common controls have to be asked for by name: the rebar wants COOL,
     * the address bar's ComboBoxEx wants USEREX, and the tree, the list, the
     * toolbar and the status bar are all in WIN95. Without this on Windows
     * every one of them fails to create and the window comes up empty; ween32
     * has them already and says so. */
    {
        INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_WIN95_CLASSES |
                                                   ICC_USEREX_CLASSES |
                                                   ICC_COOL_CLASSES };
        InitCommonControlsEx(&ic);
    }

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
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
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
    wc.lpfnWndProc = panehead_proc;
    wc.lpszClassName = "explorerpane";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = splitter_proc;
    wc.lpszClassName = "explorersplit";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.hCursor = LoadCursorA(NULL, IDC_SIZEWE);
    RegisterClassA(&wc);

    /* The face the shell draws in: Tahoma at eight points, which is the icon
     * title font of a Windows 2000. DEFAULT_GUI_FONT is not it — on the
     * machine that is MS Sans Serif — so it is asked for by name, and every
     * control is told to use it the way an application does. */
    g_font = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0,
                         0, DEFAULT_QUALITY, 0, "Tahoma");
    if (!g_font)
        g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    HWND w = CreateWindowExA(0, "ween32explorer", "All Users",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                 WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                                 WS_THICKFRAME | WS_VISIBLE,
                             40, 40, 654, 544, NULL, NULL, NULL, NULL);
    ShowWindow(w, SW_SHOWNORMAL);
    UpdateWindow(w);

    /* What the menus say beside their commands has to work: a menu that
     * offers Ctrl+C and does nothing when it is pressed is a menu telling a
     * lie. Delete and F2 carry no label, as the shell's do not either. */
    {
        static ACCEL accel[] = {
            { FVIRTKEY | FCONTROL, 'X', IDM_CUT },
            { FVIRTKEY | FCONTROL, 'C', IDM_COPY },
            { FVIRTKEY | FCONTROL, 'V', IDM_PASTE },
            { FVIRTKEY | FCONTROL, 'Z', IDM_UNDO },
            { FVIRTKEY | FCONTROL, 'A', IDM_SELECT_ALL },
            { FVIRTKEY, VK_DELETE, IDM_DELETE },
            { FVIRTKEY, VK_F2, IDM_RENAME },
            { FVIRTKEY, VK_F5, IDM_REFRESH },
            { FVIRTKEY | FALT, VK_LEFT, IDM_BACK },
            { FVIRTKEY | FALT, VK_RIGHT, IDM_FORWARD },
            { FVIRTKEY | FALT, VK_HOME, IDM_HOME },
            { FVIRTKEY | FALT, VK_RETURN, IDM_CTX_PROPERTIES },
        };
        g_accel = CreateAcceleratorTableA(accel,
                                          (int)(sizeof(accel) / sizeof(*accel)));
    }

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        /* The dialog manager's keys — Tab between the panes, Escape, the
         * arrows within a group — belong elsewhere twice over. While the menu
         * bar has the keyboard they are the bar's, which walks its titles with
         * them and leaves on Escape. And while a label is being typed over
         * they are the editor's: Enter takes the name and Escape drops it, and
         * an application must keep IsDialogMessage off them while that box is
         * up, which is what LVM_GETEDITCONTROL is for. */
        HWND editing =
            (HWND)(INT_PTR)SendMessageA(g_list, LVM_GETEDITCONTROL, 0, 0);
        /* the shortcuts first, and not while a name is being typed: Ctrl+C
         * there belongs to the box */
        if (!editing && TranslateAcceleratorA(w, g_accel, &msg))
            continue;
        if (GetFocus() != g_menubar && !editing && IsDialogMessageA(w, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
