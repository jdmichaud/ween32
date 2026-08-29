/* Menus: the bar across the top of a window, the drop-downs it opens, and the
 * modal loop that tracks them.
 *
 * The shape is USER32's. An HMENU is a list of items; an item is a string, a
 * separator, or a popup holding another HMENU. A window's menu is drawn in its
 * non-client area, above the client rectangle, and a drop-down is a real
 * top-level window of its own — which is why this waited on there being more
 * than one of those.
 *
 * Metrics are Wine's (dlls/user32/menu.c) checked against a wine capture of
 * examples/menu.c: a bar item is its text plus MENU_BAR_ITEMS_SPACE, half of
 * that on each side.
 */

#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

/* Measured off a screenshot of Windows 2000 itself — a shell context menu,
 * which is the same control an application menu drops down.
 *
 * These were taken from a wine capture first, and wine is wrong about menus in
 * two visible ways: it draws the border as one flat COLOR_3DSHADOW line where
 * Windows draws a raised edge, and it draws a separator as one line where
 * Windows draws an etched pair. Everywhere else in ween32 wine is the
 * reference, because everywhere else it agrees with Windows. Here it does not,
 * and the screenshot wins.
 *
 * A drop-down is: a two-pixel raised edge, one pixel of margin, then items of
 * seventeen and separators of nine, then margin and edge again. Every one of
 * those numbers tiles the real menu exactly — 121 x 195 for its nine items and
 * four separators. */
/* A bar item is its label plus this, half of it each side. Twelve, which is
 * Wine's MENU_BAR_ITEMS_SPACE and what a Windows 2000 menu bar measures: in a
 * screenshot of Paint's, the gap between one label's ink and the next is a
 * constant thirteen pixels across File, Edit, View, Image and Colors, and the
 * first label starts six in.
 *
 * It was sixteen here for a while, measured off the explorer's bar — but that
 * bar is not a menu bar at all. The shell's is a *toolbar* of drop-down
 * buttons inside its rebar, and a toolbar button's padding is not a menu
 * item's. Measure a menu bar against a program that has one. */
#define MENU_BAR_ITEMS_SPACE 12
#define MENU_BORDER 2       /* the raised edge */
#define MENU_PAD 1          /* between the edge and the first/last item */
#define MENU_GUTTER 20      /* the popup's left edge to the label */
#define MENU_LABEL_GAP 11   /* the widest label to the accelerator column */
#define MENU_ACCEL_GAP 9    /* the widest accelerator to the arrow column */
#define MENU_ARROW_COL 12   /* the column a submenu arrow is centred in */
#define MENU_ITEM_PAD 4     /* item height is the font's height plus this */
#define SEPARATOR_HEIGHT 9  /* the box; the etched pair sits at its middle */
#define MENU_SEP_INSET 4    /* the popup's edge to the end of the etched pair */
/* A menu with pictures in it is laid out wider all round: measured off the
 * shell's "Send To", whose four 16x16 icons make a 183 x 94 popup where the
 * same four labels alone would make 168 x 74. */
#define MENU_BMP_GUTTER 31  /* the popup's left edge to the label */
#define MENU_BMP_X 9        /* where the picture sits in that gutter */
#define MENU_BMP_PAD 3      /* over and under it, which is what heightens the
                             * item: sixteen and three each way is twenty-two */
#define MENU_BMP_MARGIN 4   /* and the extra such a menu keeps on the right */
/* Ours, kept clear of every MF_ bit: the item is one of a set, so its check
 * is drawn as a dot. Windows carries this as MFT_RADIOCHECK in a different
 * word, which is why it does not need a private bit here. */
#define WEEN_MENU_RADIO 0x40000000u

struct ween_menu {
    ween_menuitem *item;
    int count, cap;
    int is_bar;
    HWND owner;   /* the window this bar belongs to, for a bar */
};

/* ---- the list ------------------------------------------------------------ */

static HMENU menu_new(int is_bar)
{
    HMENU m = calloc(1, sizeof(*m));
    if (m)
        m->is_bar = is_bar;
    return m;
}

HMENU CreateMenu(void)
{
    return menu_new(1);
}

HMENU CreatePopupMenu(void)
{
    return menu_new(0);
}

BOOL DestroyMenu(HMENU menu)
{
    if (!menu)
        return FALSE;
    for (int i = 0; i < menu->count; i++) {
        free(menu->item[i].text);
        if (menu->item[i].popup) /* a submenu belongs to the menu holding it */
            DestroyMenu(menu->item[i].popup);
    }
    free(menu->item);
    free(menu);
    return TRUE;
}

BOOL AppendMenuA(HMENU menu, UINT flags, UINT_PTR id, LPCSTR text)
{
    if (!menu)
        return FALSE;
    if (menu->count == menu->cap) {
        int cap = menu->cap ? menu->cap * 2 : 8;
        ween_menuitem *grown = realloc(menu->item, (size_t)cap * sizeof *grown);
        if (!grown)
            return FALSE;
        menu->item = grown;
        menu->cap = cap;
    }
    ween_menuitem *it = &menu->item[menu->count];
    memset(it, 0, sizeof(*it));
    it->flags = flags;
    if (flags & MF_POPUP) {
        it->popup = (HMENU)id; /* the id slot carries the submenu */
        it->id = 0;
    } else {
        it->id = (UINT)id;
    }
    if (text && !(flags & MF_SEPARATOR)) {
        size_t n = strlen(text) + 1;
        it->text = malloc(n);
        if (!it->text)
            return FALSE;
        memcpy(it->text, text, n);
    }
    menu->count++;
    return TRUE;
}

/* Which item a caller means: a position, or the command it carries. */
static int menu_index(HMENU menu, UINT item, UINT flags)
{
    if (flags & MF_BYPOSITION)
        return (int)item;
    for (int i = 0; i < menu->count; i++)
        if (!(menu->item[i].flags & MF_POPUP) && menu->item[i].id == item)
            return i;
    return -1;
}

/* Room for one more, wherever it is going. */
static BOOL menu_grow(HMENU menu)
{
    int cap;
    ween_menuitem *grown;
    if (menu->count < menu->cap)
        return TRUE;
    cap = menu->cap ? menu->cap * 2 : 8;
    grown = realloc(menu->item, (size_t)cap * sizeof *grown);
    if (!grown)
        return FALSE;
    menu->item = grown;
    menu->cap = cap;
    return TRUE;
}

/* Put an item in ahead of another, which is how a list that changes -- the
 * files a program was last asked to open, say -- is kept in a menu. */
BOOL InsertMenuA(HMENU menu, UINT before, UINT flags, UINT_PTR id, LPCSTR text)
{
    int at;
    ween_menuitem *it;
    if (!menu || !menu_grow(menu))
        return FALSE;
    at = menu_index(menu, before, flags);
    if (at < 0 || at > menu->count)
        at = menu->count; /* win32 appends when the mark is not found */
    memmove(&menu->item[at + 1], &menu->item[at],
            (size_t)(menu->count - at) * sizeof *menu->item);
    it = &menu->item[at];
    memset(it, 0, sizeof(*it));
    it->flags = flags & ~(UINT)MF_BYPOSITION;
    if (flags & MF_POPUP)
        it->popup = (HMENU)id;
    else
        it->id = (UINT)id;
    if (text && !(flags & MF_SEPARATOR)) {
        size_t n = strlen(text) + 1;
        it->text = malloc(n);
        if (!it->text) {
            menu->count++; /* the hole is filled either way */
            return FALSE;
        }
        memcpy(it->text, text, n);
    }
    menu->count++;
    return TRUE;
}

/* Take one out. A submenu hanging off it is destroyed with it, as
 * DeleteMenu does and RemoveMenu does not. */
BOOL DeleteMenu(HMENU menu, UINT item, UINT flags)
{
    int at;
    if (!menu)
        return FALSE;
    at = menu_index(menu, item, flags);
    if (at < 0 || at >= menu->count)
        return FALSE;
    free(menu->item[at].text);
    if (menu->item[at].popup)
        DestroyMenu(menu->item[at].popup);
    memmove(&menu->item[at], &menu->item[at + 1],
            (size_t)(menu->count - at - 1) * sizeof *menu->item);
    menu->count--;
    return TRUE;
}

int ween_menu_count(HMENU menu)
{
    return menu ? menu->count : 0;
}

ween_menuitem *ween_menu_item(HMENU menu, int i)
{
    if (!menu || i < 0 || i >= menu->count)
        return NULL;
    return &menu->item[i];
}

int GetMenuItemCount(HMENU menu)
{
    return ween_menu_count(menu);
}

/* The item carrying a command, wherever in the menu it is. Asked for by id
 * rather than by position, which is how a program that knows what it put in
 * a menu reaches it again -- and win32 looks through the submenus too, since
 * an id is unique to the whole menu rather than to one level of it. */
static ween_menuitem *menu_by_command(HMENU menu, UINT id)
{
    int n = ween_menu_count(menu);
    for (int i = 0; i < n; i++) {
        ween_menuitem *it = ween_menu_item(menu, i);
        if (!it)
            continue;
        if (!(it->flags & MF_POPUP) && it->text && it->id == id)
            return it;
        if (it->flags & MF_POPUP) {
            ween_menuitem *found = menu_by_command(it->popup, id);
            if (found)
                return found;
        }
    }
    return NULL;
}

int GetMenuStringA(HMENU menu, UINT item, LPSTR out, int max, UINT flags)
{
    ween_menuitem *it;
    int n;
    if (!(flags & MF_BYPOSITION)) {
        it = menu_by_command(menu, item);
        if (!it || !it->text)
            return 0;
        n = (int)strlen(it->text);
        if (!out || max <= 0)
            return n;
        if (n > max - 1)
            n = max - 1;
        memcpy(out, it->text, (size_t)n);
        out[n] = 0;
        return n;
    }
    it = ween_menu_item(menu, (int)item);
    if (!it || !it->text)
        return 0;
    n = (int)strlen(it->text);
    if (!out || max <= 0)
        return n; /* win32 answers the length when asked for no buffer */
    if (n > max - 1)
        n = max - 1;
    memcpy(out, it->text, (size_t)n);
    out[n] = 0;
    return n;
}

HMENU GetSubMenu(HMENU menu, int pos)
{
    ween_menuitem *it = ween_menu_item(menu, pos);
    return it ? it->popup : NULL;
}

/* Find an item by command id, walking into submenus as win32 does. */
static ween_menuitem *find_id(HMENU menu, UINT id)
{
    if (!menu)
        return NULL;
    for (int i = 0; i < menu->count; i++) {
        if (menu->item[i].popup) {
            ween_menuitem *deep = find_id(menu->item[i].popup, id);
            if (deep)
                return deep;
        } else if (menu->item[i].id == id) {
            return &menu->item[i];
        }
    }
    return NULL;
}

DWORD CheckMenuItem(HMENU menu, UINT id, UINT check)
{
    ween_menuitem *it = find_id(menu, id);
    if (!it)
        return (DWORD)-1;
    UINT was = it->flags & MF_CHECKED;
    if (check & MF_CHECKED)
        it->flags |= MF_CHECKED;
    else
        it->flags &= ~MF_CHECKED;
    return was;
}

/* Windows keeps two bitmaps per item and picks between them by the check
 * state. Nothing here checks an item that has one, so the unchecked bitmap is
 * the one that gets drawn; the other is kept so the call is not a lie. */
BOOL SetMenuItemBitmaps(HMENU menu, UINT item, UINT flags, HBITMAP unchecked,
                        HBITMAP checked)
{
    ween_menuitem *it;
    if (flags & MF_BYPOSITION)
        it = (menu && (int)item < menu->count) ? &menu->item[item] : NULL;
    else
        it = find_id(menu, item);
    if (!it)
        return FALSE;
    it->bmp = unchecked ? unchecked : checked;
    return TRUE;
}

/* One of a set: the items from `first` to `last` are marked as a set and the
 * one named by `check` is the one it is on, which a menu shows with a dot
 * rather than a tick. */
BOOL CheckMenuRadioItem(HMENU menu, UINT first, UINT last, UINT check,
                        UINT flags)
{
    if (!menu)
        return FALSE;
    for (int i = 0; i < menu->count; i++) {
        ween_menuitem *it = &menu->item[i];
        UINT key = (flags & MF_BYPOSITION) ? (UINT)i : it->id;
        if ((flags & MF_BYPOSITION) ? (key < first || key > last)
                                    : (it->id < first || it->id > last))
            continue;
        it->flags |= WEEN_MENU_RADIO;
        if (key == check)
            it->flags |= MF_CHECKED;
        else
            it->flags &= ~MF_CHECKED;
    }
    return TRUE;
}

BOOL EnableMenuItem(HMENU menu, UINT id, UINT enable)
{
    ween_menuitem *it = find_id(menu, id);
    if (!it)
        return FALSE;
    if (enable & (MF_GRAYED | MF_DISABLED))
        it->flags |= MF_GRAYED;
    else
        it->flags &= ~MF_GRAYED;
    return TRUE;
}

/* Change an item's text, which is how a command that names what it would do
 * — "Undo Delete" — comes to say so. */
BOOL ModifyMenuA(HMENU menu, UINT item, UINT flags, UINT_PTR id, LPCSTR text)
{
    ween_menuitem *it;
    if (flags & MF_BYPOSITION) {
        if (!menu || (int)item >= menu->count)
            return FALSE;
        it = &menu->item[item];
    } else {
        it = find_id(menu, item);
    }
    if (!it)
        return FALSE;
    /* MF_STRING is zero, so what says the item carries text is that text was
     * given and the flags do not say it is something else. */
    if (text && !(flags & (MF_SEPARATOR | MF_POPUP))) {
        char *copy = NULL;
        if (text) {
            size_t n = strlen(text) + 1;
            copy = malloc(n);
            if (!copy)
                return FALSE;
            memcpy(copy, text, n);
        }
        free(it->text);
        it->text = copy;
        it->w = 0; /* it will be measured again */
    }
    it->id = (UINT)id;
    return TRUE;
}

/* ---- measuring ----------------------------------------------------------- */

/* An item's text is "label\taccelerator": the tab splits the two, and the
 * accelerator is right-aligned in the popup. */
static const char *accel_of(const char *text, int *label_len)
{
    const char *tab = text ? strchr(text, '\t') : NULL;
    *label_len = tab ? (int)(tab - text) : (text ? (int)strlen(text) : 0);
    return tab ? tab + 1 : NULL;
}

/* Menus are laid out on the width the glyphs actually occupy — the strike's
 * own advances — everywhere: the bar and the drop-downs alike.
 *
 * The other measure available is what GDI reports, which rounds each
 * character's outline advance up and so runs wide by about half a pixel a
 * character. The bar used it for a while because it matched a wine capture.
 * Against a screenshot of Windows itself the drawn width is plainly the right
 * one: it makes the bar's padding come out as the same sixteen pixels on five
 * labels of very different lengths, where the reported width gives thirteen,
 * fifteen, thirteen, fifteen, fourteen. A constant that is only constant under
 * one of two measures is telling you which measure it was built from. */
static int text_drawn(const ween_strike *f, const char *s, int len)
{
    return (!f || !s || len <= 0) ? 0 : ween_strike_text_width(f, s, len);
}

/* Mnemonic markers are not drawn, so they are not measured either. */
static int strip_mnemonic(const char *text, int len, char *buf, int cap)
{
    int n = 0;
    for (int i = 0; i < len && n < cap - 1; i++) {
        if (text[i] == '&' && i + 1 < len) {
            if (text[i + 1] != '&')
                continue;
            i++;
        }
        buf[n++] = text[i];
    }
    return n;
}

static int label_drawn(const ween_strike *f, const char *text, int len)
{
    char buf[256];
    int n = strip_mnemonic(text, len, buf, (int)sizeof(buf));
    return text_drawn(f, buf, n);
}

/* Lay the bar out across the window's width; each item keeps its own rect. */
void ween_menu_layout_bar(HMENU menu, const ween_strike *f, int width)
{
    int x = 0;
    if (!menu)
        return;
    for (int i = 0; i < menu->count; i++) {
        ween_menuitem *it = &menu->item[i];
        int len;
        accel_of(it->text, &len);
        it->w = label_drawn(f, it->text, len) +
                ween_ncm(MENU_BAR_ITEMS_SPACE);
        it->x = x;
        it->y = 0;
        it->h = ween_ncm(WEEN_NC_MENU);
        x += it->w;
    }
    (void)width;
}

/* The widest and tallest picture in a menu. One item with a bitmap widens the
 * gutter for all of them, the way one long accelerator sets the accelerator
 * column, so this has to be known before any item is measured or drawn. */
static void menu_bitmap_size(HMENU menu, int *w, int *h)
{
    *w = *h = 0;
    for (int i = 0; menu && i < menu->count; i++) {
        const ween_gdiobj *o = menu->item[i].bmp;
        if (!o)
            continue;
        if (o->bitmap.w > *w)
            *w = o->bitmap.w;
        if (o->bitmap.h > *h)
            *h = o->bitmap.h;
    }
}

/* A popup's size and the rectangle of every item in it. Items run the full
 * width inside the border; the label, the accelerator and the submenu arrow
 * each have a column, and the columns are what the width is made of. */
void ween_menu_popup_size(HMENU menu, const ween_strike *f, int *w, int *h)
{
    int label = 0, accel = 0, arrow = 0, bw = 0, bh = 0;
    int inset = ween_ncm(MENU_BORDER) + ween_ncm(MENU_PAD);
    int y = inset;
    /* the font's height, not its cell: an item is as tall as a line of text
     * plus four, which is what the capture shows */
    int th = f ? f->ascent - f->descent : 13;
    int item_h = th + ween_ncm(MENU_ITEM_PAD);

    if (!menu) {
        *w = *h = 0;
        return;
    }
    menu_bitmap_size(menu, &bw, &bh);
    if (bh + 2 * ween_ncm(MENU_BMP_PAD) > item_h)
        item_h = bh + 2 * ween_ncm(MENU_BMP_PAD);
    for (int i = 0; i < menu->count; i++) {
        ween_menuitem *it = &menu->item[i];
        int len;
        const char *acc = accel_of(it->text, &len);
        int lw = label_drawn(f, it->text, len);
        if (lw > label)
            label = lw;
        if (acc) {
            int aw = text_drawn(f, acc, (int)strlen(acc));
            if (aw > accel)
                accel = aw;
        }
        if (it->popup)
            arrow = ween_ncm(MENU_ARROW_COL);
        it->x = inset;
        it->y = y;
        it->h = (it->flags & MF_SEPARATOR) ? ween_ncm(SEPARATOR_HEIGHT)
                                           : item_h;
        y += it->h;
    }

    /* The column a submenu arrow goes in is reserved whether or not anything
     * in this menu has one. It is what puts a margin between the longest item
     * and the right border, and reserving it only when an arrow turns up gave
     * every menu but the one with a cascade in it text hard against the edge.
     * The accelerator column, by contrast, is only there when something needs
     * it — a menu with no accelerators does not leave a gap for them. */
    *w = ween_ncm(bw ? MENU_BMP_GUTTER : MENU_GUTTER) + label;
    if (accel)
        *w += ween_ncm(MENU_LABEL_GAP) + accel;
    *w += ween_ncm(MENU_ACCEL_GAP) + ween_ncm(MENU_ARROW_COL);
    if (bw)
        *w += ween_ncm(MENU_BMP_MARGIN);
    *w += inset;
    (void)arrow;
    *h = y + inset;
    for (int i = 0; i < menu->count; i++)
        menu->item[i].w = *w - 2 * inset;
}

/* The item under a point, in the coordinates the layout used. -1 for none. */
int ween_menu_hit(HMENU menu, int x, int y)
{
    if (!menu)
        return -1;
    for (int i = 0; i < menu->count; i++) {
        ween_menuitem *it = &menu->item[i];
        if (it->flags & MF_SEPARATOR)
            continue;
        if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h)
            return i;
    }
    return -1;
}

/* The item a mnemonic names, for Alt+letter on the bar. */
int ween_menu_mnemonic(HMENU menu, unsigned ch)
{
    if (ch >= 'A' && ch <= 'Z')
        ch += 32;
    for (int i = 0; menu && i < menu->count; i++) {
        const char *t = menu->item[i].text;
        for (const char *p = t; p && *p; p++) {
            if (*p != '&')
                continue;
            if (p[1] == '&') {
                p++;
                continue;
            }
            unsigned c = (unsigned char)p[1];
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (c == ch)
                return i;
            break;
        }
    }
    return -1;
}

/* ---- drawing -------------------------------------------------------------- */

/* One label, with its mnemonic underlined; DrawTextA already knows how. */
int ween_menu_cues = 0;

static void draw_label(ween_surface *s, const ween_strike *f, int x, int y,
                       const char *text, int len, ween_color c)
{
    struct ween_dc dc;
    RECT r;
    memset(&dc, 0, sizeof(dc));
    dc.s = s;
    dc.clip_w = s->w;
    dc.clip_h = s->h;
    dc.text_color = c;
    ween_dc_set_font(&dc, f);
    r.left = x;
    r.top = y;
    r.right = s->w;
    r.bottom = y + 32;
    DrawTextA(&dc, text, len, &r,
              DT_LEFT | DT_SINGLELINE | (ween_menu_cues ? 0 : DT_HIDEPREFIX));
}

void ween_menu_draw_bar(HMENU menu, ween_surface *s, int ox, int oy, int width,
                        const ween_strike *f, int hot)
{
    int h = ween_ncm(WEEN_NC_MENU);
    int cell = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 12;
    ween_surface_fill(s, ox, oy, width, h, WEEN_FACE);
    if (!menu)
        return;
    for (int i = 0; i < menu->count; i++) {
        ween_menuitem *it = &menu->item[i];
        int len;
        ween_color fg = WEEN_BLACK;
        accel_of(it->text, &len);
        /* The open one is pushed in, not filled: Windows 2000 draws a bar
         * item the way it draws a button, and the label stays black. The
         * highlight belongs to the drop-down's items, not to the bar. One
         * row of the bar is left clear above and below the box. */
        if (i == hot)
            ween_classic_edge(s, ox + it->x, oy + 1, it->w, h - 2,
                              BDR_SUNKENOUTER, BF_RECT, NULL);
        if (it->flags & MF_GRAYED)
            fg = WEEN_SHADOW;
        /* One *above* centred, which is where the machine puts it: in a
         * screenshot of Paint the six titles' ink starts four rows into the
         * nineteen-row bar, and centring the cell alone would put it six.
         * Wine draws it a row lower again; this follows the machine. */
        draw_label(s, f, ox + it->x + ween_ncm(MENU_BAR_ITEMS_SPACE) / 2,
                   oy + (h - cell) / 2 - ween_ncm(1), it->text, len, fg);
    }
}

void ween_menu_draw_popup(HMENU menu, ween_surface *s, const ween_strike *f,
                          int w, int h, int hot)
{
    int border = ween_ncm(MENU_BORDER);
    int bw = 0, bh = 0;
    int gutter;
    int inset = border + ween_ncm(MENU_PAD);
    menu_bitmap_size(menu, &bw, &bh);
    gutter = ween_ncm(bw ? MENU_BMP_GUTTER : MENU_GUTTER);
    int arrow_col = ween_ncm(MENU_ARROW_COL);
    int cell = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 12;
    int label = 0, accel = 0, has_arrow = 0, accel_x;

    ween_surface_fill(s, 0, 0, w, h, WEEN_FACE); /* COLOR_MENU */
    /* A raised edge, as Windows draws it: the outer line is COLOR_3DLIGHT,
     * which is the face colour in this scheme and so invisible, with white
     * inside it at the top and left and shadow over dark shadow at the bottom
     * and right. Wine draws a flat grey rectangle here instead. */
    ween_classic_edge(s, 0, 0, w, h, EDGE_RAISED, BF_RECT, NULL);
    if (!menu)
        return;

    /* the accelerator column is the same for every item, left-aligned, so it
     * has to be found before any of them are drawn */
    for (int i = 0; i < menu->count; i++) {
        ween_menuitem *it = &menu->item[i];
        int len;
        const char *acc = accel_of(it->text, &len);
        int lw = label_drawn(f, it->text, len);
        if (lw > label)
            label = lw;
        if (acc) {
            int aw = text_drawn(f, acc, (int)strlen(acc));
            if (aw > accel)
                accel = aw;
        }
        if (it->popup)
            has_arrow = 1;
    }
    accel_x = gutter + label + ween_ncm(MENU_LABEL_GAP);

    for (int i = 0; i < menu->count; i++) {
        ween_menuitem *it = &menu->item[i];
        int len;
        const char *acc = accel_of(it->text, &len);
        ween_color fg = WEEN_BLACK;

        if (it->flags & MF_SEPARATOR) {
            /* An etched pair across the whole interior — shadow with white
             * under it — which is what makes a Windows separator look sunk
             * into the menu rather than drawn on top of it. */
            int mid = it->y + it->h / 2;
            int sx = ween_ncm(MENU_SEP_INSET);
            ween_surface_hline(s, sx, mid - 1, w - 2 * sx, WEEN_SHADOW);
            ween_surface_hline(s, sx, mid, w - 2 * sx, WEEN_WHITE);
            continue;
        }
        /* The bar goes under a greyed item too — Windows highlights the one
         * the keyboard is on whether or not it can be chosen — and its label
         * stays the shadow colour, without the white emboss underneath it. */
        if (i == hot) {
            ween_surface_fill(s, it->x, it->y, it->w, it->h, WEEN_CAP_LEFT);
            fg = WEEN_WHITE;
        }
        if (it->flags & MF_GRAYED)
            fg = WEEN_SHADOW;

        /* The pixels an item has over the font's cell are not split evenly:
         * the machine leaves one fewer above the text than below, in an item
         * of seventeen and in the taller one a picture makes. */
        int ty = it->y + (it->h - cell) / 2 - 1;
        if ((it->flags & MF_CHECKED) && (it->flags & WEEN_MENU_RADIO))
            ween_classic_menu_bullet(s, inset + ween_ncm(5),
                                     it->y + (it->h - 6) / 2, fg);
        else if (it->flags & MF_CHECKED) /* a bare tick, not a box */
            ween_classic_menu_check(s, inset + ween_ncm(4),
                                    it->y + (it->h - 7) / 2, fg);
        if (it->bmp) { /* opaque, as Windows blits a menu bitmap */
            const ween_surface *b = &it->bmp->bitmap;
            int bx = ween_ncm(MENU_BMP_X);
            int by = it->y + (it->h - b->h) / 2;
            for (int py = 0; py < b->h; py++)
                for (int px = 0; px < b->w; px++)
                    ween_surface_pixel(s, bx + px, by + py,
                                       b->px[(size_t)py * b->w + px]);
        }
        const ween_strike *lf =
            (it->flags & MF_DEFAULT) ? ween_gui_font_bold() : f;
        /* A disabled label is embossed, not merely pale: white one down and
         * one right, with the shadow colour over it. Windows draws every
         * greyed thing this way — a menu item, a bar item, a button's face —
         * and without the white it reads as a lighter black instead. */
        if (fg == WEEN_SHADOW && i != hot) {
            draw_label(s, lf, gutter + 1, ty + 1, it->text, len, WEEN_WHITE);
            if (acc)
                draw_label(s, f, accel_x + 1, ty + 1, acc, (int)strlen(acc),
                           WEEN_WHITE);
        }
        draw_label(s, lf, gutter, ty, it->text, len, fg);
        if (acc)
            draw_label(s, f, accel_x, ty, acc, (int)strlen(acc), fg);
        if (it->popup) { /* centred in its column at the right */
            int col = w - inset - arrow_col;
            if (fg == WEEN_SHADOW && i != hot)
                ween_classic_menu_arrow(s, col + ween_ncm(2) + 1, ty + 1, cell,
                                        WEEN_WHITE);
            ween_classic_menu_arrow(s, col + ween_ncm(2), ty, cell, fg);
        }
        (void)has_arrow;
    }
}

/* ---- tracking -------------------------------------------------------------
 *
 * A drop-down is a top-level window of its own, and tracking it is a modal
 * loop: the pointer moves the highlight, a release over an item chooses it,
 * and anything else dismisses. This is USER32's MENU_TrackMenu, and it is why
 * menus waited on there being more than one top-level window.
 */

#define WEEN_MENU_CLASS "#32768" /* the class win32 gives its menu windows */

static LRESULT CALLBACK menu_popup_proc(HWND wnd, UINT msg, WPARAM wp,
                                        LPARAM lp)
{
    if (msg == WM_PAINT) {
        const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
        PAINTSTRUCT ps;
        BeginPaint(wnd, &ps);
        ween_menu_draw_popup(wnd->menu, &wnd->surface, f, wnd->w, wnd->h,
                             wnd->menu_hot);
        EndPaint(wnd, &ps);
        return 0;
    }
    return DefWindowProcA(wnd, msg, wp, lp);
}

static void ensure_menu_class(void)
{
    static int done;
    WNDCLASSA wc;
    if (done)
        return;
    done = 1;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = menu_popup_proc;
    wc.lpszClassName = WEEN_MENU_CLASS;
    wc.hbrBackground = NULL; /* the popup paints its own face */
    RegisterClassA(&wc);
}

/* One open drop-down. A menu that is being walked is a stack of these: the
 * bar item that started it, then a level for every cascade opened under it.
 * The parent stays up and keeps its item highlighted, which is what makes a
 * submenu usable — you can walk back out of one. */
typedef struct {
    HMENU menu;
    HWND wnd;
    int hot;
} menu_level;

#define WEEN_MENU_MAX_DEPTH 8
/* Where a cascade goes beside the item that opened it: six pixels back over
 * its parent's right edge, and lifted by the popup's own inset so that its
 * first item lines up with that item. Both measured on the machine, whose
 * "Send To" submenu starts six columns inside a 121-wide menu and three rows
 * above the row it belongs to. */
#define WEEN_MENU_CASCADE_OVERLAP 6

typedef struct {
    ween_event replay; /* a press this loop is giving back, see below */
    int has_replay;
    HWND owner;    /* who hears WM_COMMAND */
    HWND bar_wnd;  /* the window whose bar is open, NULL for TrackPopupMenu */
    HWND tb;       /* the toolbar this drop-down belongs to, when it does */
    int tb_item;   /* and which of its buttons */
    HMENU bar;
    int bar_index;
    menu_level level[WEEN_MENU_MAX_DEPTH];
    int depth;
    UINT chosen;
    int done;
} menu_session;

static void level_close(menu_session *s)
{
    menu_level *l = &s->level[--s->depth];
    l->wnd->menu = NULL; /* the menu outlives the window showing it */
    DestroyWindow(l->wnd);
    l->wnd = NULL;
}

static void level_close_to(menu_session *s, int depth)
{
    while (s->depth > depth)
        level_close(s);
}

static int level_open(menu_session *s, HMENU menu, int x, int y)
{
    const ween_strike *f = ween_gui_font();
    int w = 0, h = 0;
    if (s->depth >= WEEN_MENU_MAX_DEPTH || !ween_menu_count(menu))
        return 0;
    ween_menu_popup_size(menu, f, &w, &h);
    if (s->owner)
        SendMessageA(s->owner, WM_INITMENUPOPUP, (WPARAM)menu, 0);
    HWND wnd = CreateWindowExA(WS_EX_NOACTIVATE, WEEN_MENU_CLASS, "",
                               WS_POPUP | WS_VISIBLE, x, y, w, h, NULL, NULL,
                               NULL, NULL);
    if (!wnd)
        return 0;
    wnd->menu = menu;
    wnd->menu_hot = -1;
    s->level[s->depth].menu = menu;
    s->level[s->depth].wnd = wnd;
    s->level[s->depth].hot = -1;
    s->depth++;
    return 1;
}

/* Open the cascade belonging to the highlighted item of `depth`, beside it.
 * If it is already the one open, leave it alone: the pointer sitting on a
 * submenu's parent sends a move for every pixel, and tearing the submenu down
 * and building it again on each of those is what made it flicker. */
static void level_open_cascade(menu_session *s, int depth)
{
    menu_level *l = &s->level[depth];
    ween_menuitem *it = ween_menu_item(l->menu, l->hot);
    if (!it || !it->popup || (it->flags & MF_GRAYED))
        return;
    if (s->depth > depth + 1 && s->level[depth + 1].menu == it->popup)
        return; /* already showing, and showing the right one */
    level_close_to(s, depth + 1);
    level_open(s, it->popup,
               l->wnd->x + l->wnd->w - ween_ncm(WEEN_MENU_CASCADE_OVERLAP),
               l->wnd->y + it->y - ween_ncm(MENU_BORDER) - ween_ncm(MENU_PAD));
}

static void level_set_hot(menu_session *s, int depth, int hot)
{
    menu_level *l = &s->level[depth];
    ween_menuitem *it;
    if (l->hot == hot)
        return;
    l->hot = hot;
    l->wnd->menu_hot = hot;
    ween_damage_all(l->wnd);
    /* anything opened under the old item goes with it */
    level_close_to(s, depth + 1);
    /* and the owner is told what the highlight is on, which is how a shell
     * comes to describe the item in its status bar */
    it = hot >= 0 ? ween_menu_item(l->menu, hot) : NULL;
    if (s->owner)
        SendMessageA(s->owner, WM_MENUSELECT,
                     it ? MAKEWPARAM(it->popup ? (WORD)hot : (WORD)it->id,
                                     (WORD)it->flags)
                        : MAKEWPARAM(0xffff, 0xffff),
                     (LPARAM)(it ? l->menu : NULL));
}

/* Step the highlight through a popup, skipping what cannot be chosen. */
static void level_step(menu_session *s, int depth, int step)
{
    menu_level *l = &s->level[depth];
    int n = ween_menu_count(l->menu), hot = l->hot;
    if (n <= 0)
        return;
    if (hot < 0)
        hot = step > 0 ? -1 : 0;
    for (int i = 0; i < n; i++) {
        hot = (hot + step + n) % n;
        ween_menuitem *it = ween_menu_item(l->menu, hot);
        /* A greyed item is stepped onto like any other — Windows highlights
         * it and refuses to choose it, rather than hiding it from the
         * arrows. A separator is not an item at all. */
        if (it && !(it->flags & MF_SEPARATOR))
            break;
    }
    level_set_hot(s, depth, hot);
}

/* Whether a point on the owner window is within the menu bar strip, and which
 * item it is over. This has to mean the strip and not the window: a press
 * anywhere else on the owner puts the menu away, and treating the whole window
 * as "the bar" swallowed those presses instead — so the menu never closed and
 * nothing on the window, the close box included, could be reached again. */
static int bar_hit(const menu_session *s, const ween_event *ev, int *index)
{
    int frame, bar_y, bar_h;
    *index = -1;
    if (!s->bar_wnd || ev->win != s->bar_wnd->backend_win)
        return 0;
    frame = ween_frame_width(s->bar_wnd);
    bar_y = frame + ween_caption_height(s->bar_wnd);
    bar_h = ween_ncm(WEEN_NC_MENU);
    if (ev->y < bar_y || ev->y >= bar_y + bar_h || ev->x < frame ||
        ev->x >= s->bar_wnd->w - frame)
        return 0;
    *index = ween_menu_hit(s->bar, ev->x - frame, ev->y - bar_y);
    return 1;
}

/* Which open level an event belongs to; -1 for anything else. A headless
 * event names no window and belongs to the deepest, which is where a script's
 * attention is. */
static int level_of(const menu_session *s, const void *backend_win)
{
    if (!backend_win)
        return s->depth - 1;
    for (int i = s->depth - 1; i >= 0; i--)
        if (s->level[i].wnd->backend_win == backend_win)
            return i;
    return -1;
}

/* Open the bar item at `index`, closing whatever was open before it. */
static void bar_open(menu_session *s, int index)
{
    ween_menuitem *it;
    int frame, bar_y;
    int ox, oy;
    if (!s->bar_wnd || index < 0 || index >= ween_menu_count(s->bar))
        return;
    it = ween_menu_item(s->bar, index);
    if (!it || !it->popup || (it->flags & MF_GRAYED))
        return;
    level_close_to(s, 0);
    s->bar_index = index;
    s->bar_wnd->menu_hot = index;
    ween_damage_all(s->bar_wnd);
    /* The owner is told about a highlighted **bar title** too, and was not.
     * win32 sends WM_MENUSELECT for one with `MF_POPUP` set and the item's
     * index where an id would go -- a title has no id -- and the bar's own
     * handle in lParam.
     *
     * This was sent from two places, neither of them here, so an application
     * describing the highlight in its status bar went on describing the last
     * item of the last menu while the bar's own title was lit. WordPad's pane
     * is **blank** at that moment, which bob read off the machine with File,
     * Edit and View, and it could not be blank without this. */
    if (s->owner)
        SendMessageA(s->owner, WM_MENUSELECT,
                     MAKEWPARAM((WORD)index, (WORD)(it->flags | MF_POPUP)),
                     (LPARAM)s->bar);
    frame = ween_frame_width(s->bar_wnd);
    bar_y = frame + ween_caption_height(s->bar_wnd);
    /* Where the window actually is, not where it asked to be: under a window
     * manager that puts it somewhere else the two part company, and the menu
     * opens beside a window that is not there. */
    ween_window_origin(s->bar_wnd, &ox, &oy);
    level_open(s, it->popup, ox + frame + it->x,
               oy + bar_y + ween_ncm(WEEN_NC_MENU));
}

/* Left and right at the top of a menu walk the bar, as they do on Windows. */
static void bar_step(menu_session *s, int step)
{
    int n = ween_menu_count(s->bar), index = s->bar_index;
    if (!s->bar_wnd || n <= 0)
        return;
    for (int i = 0; i < n; i++) {
        index = (index + step + n) % n;
        ween_menuitem *it = ween_menu_item(s->bar, index);
        if (it && it->popup && !(it->flags & MF_GRAYED))
            break;
    }
    bar_open(s, index);
}

/* Choose the item, or open it if it is a cascade. */
static void level_activate(menu_session *s, int depth, int index)
{
    menu_level *l = &s->level[depth];
    ween_menuitem *it = ween_menu_item(l->menu, index);
    if (!it || (it->flags & (MF_SEPARATOR | MF_GRAYED)))
        return;
    level_set_hot(s, depth, index);
    if (it->popup) {
        level_open_cascade(s, depth);
        if (s->depth > depth + 1)
            level_step(s, depth + 1, 1); /* the keyboard lands on the first */
        return;
    }
    s->chosen = it->id;
    s->done = 1;
}

/* The title the pointer is over in the bar this drop-down belongs to, or -1.
 * The event is in the top-level window's coordinates, which is where the
 * bar's client origin is measured too. */
static int tb_hit(const menu_session *s, const ween_event *ev)
{
    int ox, oy;
    if (!s->tb || !s->level[0].wnd)
        return -1;
    if (ev->win && ween_top_level(s->tb) &&
        ev->win != ween_top_level(s->tb)->backend_win)
        return -1;
    ween_client_origin(s->tb, &ox, &oy);
    return ween_toolbar_menu_hit(s->tb, ev->x - ox, ev->y - oy);
}

/* Leave this title and ask the bar for that one. */
static void tb_switch(menu_session *s, int index)
{
    ween_toolbar_menu_switch(index);
    s->done = 1;
}

static void session_run(menu_session *s)
{
    while (!s->done && s->depth > 0) {
        ween_flush_paint();
        ween_event ev =
            ween_active_backend->next_event(s->level[0].wnd->backend_win, -1);
        int lvl = level_of(s, ev.win);
        int bar_index = -1;
        int on_bar = bar_hit(s, &ev, &bar_index);
        int on_tb = tb_hit(s, &ev);
        int deepest = s->depth - 1;

        switch (ev.kind) {
        case WEEN_EV_MOUSE_MOVE:
            if (lvl >= 0) {
                int hot = ween_menu_hit(s->level[lvl].menu, ev.x, ev.y);
                if (hot >= 0) {
                    level_set_hot(s, lvl, hot);
                    level_open_cascade(s, lvl);
                } else if (s->depth == lvl + 1) {
                    /* off the items of the deepest menu: drop the highlight,
                     * but only when nothing is cascaded off it — otherwise
                     * the gap between a parent and its child would close the
                     * child on the way across */
                    level_set_hot(s, lvl, -1);
                }
            } else if (on_bar && bar_index >= 0 &&
                       bar_index != s->bar_index) {
                /* sliding along the bar switches drop-downs, which is how
                 * win32 menus are used */
                bar_open(s, bar_index);
            } else if (on_tb >= 0 && on_tb != s->tb_item) {
                tb_switch(s, on_tb); /* the same, along a toolbar's titles */
            }
            break;

        case WEEN_EV_MOUSE_DOWN:
            if (lvl >= 0)
                break; /* inside a drop-down: the release is what chooses */
            if (on_bar && bar_index == s->bar_index)
                s->done = 1; /* pressing the open one closes it, as win32 does */
            else if (on_bar && bar_index >= 0)
                bar_open(s, bar_index);
            else if (on_tb == s->tb_item)
                s->done = 1; /* the same for a title: pressing it closes it */
            else if (on_tb >= 0)
                tb_switch(s, on_tb);
            else {
                /* Anywhere else puts the whole menu away, and the press is
                 * handed back rather than swallowed: on the machine a click
                 * on a toolbar button with a menu open both closes the menu
                 * and presses the button. */
                s->done = 1;
                s->replay = ev;
                s->has_replay = 1;
            }
            break;

        case WEEN_EV_MOUSE_UP:
            if (lvl >= 0) {
                int hot = ween_menu_hit(s->level[lvl].menu, ev.x, ev.y);
                if (hot >= 0)
                    level_activate(s, lvl, hot);
            }
            break;

        case WEEN_EV_KEY:
            /* the keyboard has been used, so from here on the letters are
             * underlined — including in the menu this key is navigating */
            if (!ween_menu_cues) {
                ween_menu_cues = 1;
                for (int i = 0; i < s->depth; i++)
                    if (s->level[i].wnd)
                        ween_damage_all(s->level[i].wnd);
            }
            switch (ev.vk) {
            case VK_ESCAPE:
                if (s->depth > 1)
                    level_close(s); /* out of the cascade, not out of the menu */
                else
                    s->done = 1;
                break;
            case VK_DOWN:
                level_step(s, deepest, 1);
                break;
            case VK_UP:
                level_step(s, deepest, -1);
                break;
            case VK_RIGHT: {
                ween_menuitem *it =
                    ween_menu_item(s->level[deepest].menu, s->level[deepest].hot);
                if (it && it->popup) {
                    level_open_cascade(s, deepest);
                    if (s->depth > deepest + 1)
                        level_step(s, deepest + 1, 1);
                } else if (s->tb) {
                    int to = ween_toolbar_menu_step(s->tb, s->tb_item, 1);
                    if (to >= 0)
                        tb_switch(s, to);
                } else {
                    bar_step(s, 1);
                }
                break;
            }
            case VK_LEFT:
                if (s->depth > 1) {
                    level_close(s);
                } else if (s->tb) {
                    int to = ween_toolbar_menu_step(s->tb, s->tb_item, -1);
                    if (to >= 0)
                        tb_switch(s, to);
                } else {
                    bar_step(s, -1);
                }
                break;
            case VK_RETURN:
                level_activate(s, deepest, s->level[deepest].hot);
                break;
            default: {
                /* a letter picks the item whose label marks it */
                int hit = ween_menu_mnemonic(s->level[deepest].menu,
                                             ev.ch ? ev.ch : ev.vk);
                if (hit >= 0)
                    level_activate(s, deepest, hit);
                break;
            }
            }
            break;

        case WEEN_EV_EXPOSE:
            /* the owner is behind the menu and gets exposed too, most of all
             * when a drop-down that was covering it goes away */
            ween_mark_exposed(&ev);
            for (int i = 0; i < s->depth; i++)
                ween_damage_all(s->level[i].wnd);
            break;

        case WEEN_EV_END:
        case WEEN_EV_CLOSE:
            s->done = 1;
            break;
        default:
            break;
        }
    }

    level_close_to(s, 0);
    if (s->owner) /* the menu has gone: nothing is highlighted any more */
        SendMessageA(s->owner, WM_MENUSELECT, MAKEWPARAM(0xffff, 0xffff), 0);
    if (s->bar_wnd) {
        s->bar_wnd->menu_hot = -1;
        ween_damage_all(s->bar_wnd);
    }
    ween_flush_paint();
    /* the press that closed it, once the menu is really gone */
    if (s->has_replay)
        ween_replay_event(&s->replay);
}

UINT ween_menu_track(HMENU menu, HWND owner, int screen_x, int screen_y)
{
    menu_session s;
    if (!menu || !ween_menu_count(menu) || !ween_active_backend)
        return 0;
    ensure_menu_class();
    memset(&s, 0, sizeof(s));
    s.owner = owner;
    s.bar_index = -1;
    /* Put up from a toolbar's drop-down, this is one title of a menu bar:
     * the pointer moving to another title, or an arrow walking to it, ends
     * this one and asks the bar for that one. The bar is doing the asking, so
     * it is the bar that knows — the application only answered TBN_DROPDOWN. */
    s.tb = ween_toolbar_menu_bar();
    s.tb_item = ween_toolbar_menu_item();
    if (!level_open(&s, menu, screen_x, screen_y))
        return 0;
    if (s.tb && ween_toolbar_menu_keyed())
        level_step(&s, 0, 1); /* opened by key: the first item is picked */
    session_run(&s);
    return s.chosen;
}


/* Walking a window's menu bar: the same session, started from a bar item, so
 * the arrows can move between drop-downs and a cascade keeps its parent. */
UINT ween_menu_track_bar(HWND top, int index, int from_keyboard)
{
    menu_session s;
    if (!top || !top->menu || !ween_active_backend)
        return 0;
    ensure_menu_class();
    memset(&s, 0, sizeof(s));
    s.owner = top;
    s.bar_wnd = top;
    s.bar = top->menu;
    s.bar_index = -1;
    SendMessageA(top, WM_INITMENU, (WPARAM)top->menu, 0);
    ween_menu_layout_bar(top->menu, ween_gui_font(),
                         top->w - 2 * ween_frame_width(top));
    bar_open(&s, index);
    if (!s.depth)
        return 0;
    if (from_keyboard) /* a menu opened by key starts on its first item */
        level_step(&s, 0, 1);
    session_run(&s);
    return s.chosen;
}

BOOL TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved,
                    HWND owner, const RECT *unused)
{
    (void)reserved;
    (void)unused;
    UINT cmd = ween_menu_track(menu, owner, x, y);
    if (flags & TPM_RETURNCMD)
        return (BOOL)cmd;
    if (cmd && owner)
        PostMessageA(owner, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0), 0);
    return cmd != 0;
}
