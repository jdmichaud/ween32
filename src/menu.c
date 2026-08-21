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

/* Wine's menu.c constants. */
#define MENU_BAR_ITEMS_SPACE 12
#define SEPARATOR_HEIGHT 5
#define MENU_TAB_SPACE 8  /* between an item's text and its accelerator */
#define MENU_ITEM_LEFT 2  /* the popup's border to the check column */

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

/* ---- measuring ----------------------------------------------------------- */

/* An item's text is "label\taccelerator": the tab splits the two, and the
 * accelerator is right-aligned in the popup. */
static const char *accel_of(const char *text, int *label_len)
{
    const char *tab = text ? strchr(text, '\t') : NULL;
    *label_len = tab ? (int)(tab - text) : (text ? (int)strlen(text) : 0);
    return tab ? tab + 1 : NULL;
}

static int text_width(const ween_strike *f, const char *s, int len)
{
    return (!f || !s || len <= 0) ? 0 : ween_strike_text_extent(f, s, len);
}

/* Mnemonic markers are not drawn, so they are not measured either. */
static int label_width(const ween_strike *f, const char *text, int len)
{
    char buf[256];
    int n = 0;
    for (int i = 0; i < len && n < (int)sizeof(buf) - 1; i++) {
        if (text[i] == '&' && i + 1 < len) {
            if (text[i + 1] != '&')
                continue;
            i++;
        }
        buf[n++] = text[i];
    }
    return text_width(f, buf, n);
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
        it->w = label_width(f, it->text, len) + ween_ncm(MENU_BAR_ITEMS_SPACE);
        it->x = x;
        it->y = 0;
        it->h = ween_ncm(WEEN_NC_MENU);
        x += it->w;
    }
    (void)width;
}

/* A popup's size: the widest label, plus a check column and an accelerator
 * column, and every item as tall as the font's cell. */
void ween_menu_popup_size(HMENU menu, const ween_strike *f, int *w, int *h)
{
    int label = 0, accel = 0, y = ween_ncm(MENU_ITEM_LEFT);
    int check = ween_ncm(WEEN_NC_MENUCHECK);
    int cell = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 12;
    int item_h = cell + ween_ncm(4);
    int arrow = 0;

    if (!menu) {
        *w = *h = 0;
        return;
    }
    for (int i = 0; i < menu->count; i++) {
        ween_menuitem *it = &menu->item[i];
        int len;
        const char *acc = accel_of(it->text, &len);
        int lw = label_width(f, it->text, len);
        if (lw > label)
            label = lw;
        if (acc) {
            int aw = text_width(f, acc, (int)strlen(acc));
            if (aw > accel)
                accel = aw;
        }
        if (it->popup)
            arrow = check; /* room for the submenu arrow on the right */
        it->x = ween_ncm(MENU_ITEM_LEFT);
        it->y = y;
        it->h = (it->flags & MF_SEPARATOR) ? ween_ncm(SEPARATOR_HEIGHT)
                                           : item_h;
        y += it->h;
    }
    *w = check + label + (accel ? ween_ncm(MENU_TAB_SPACE) + accel : 0) +
         arrow + ween_ncm(MENU_ITEM_LEFT) * 2 + ween_ncm(6);
    *h = y + ween_ncm(MENU_ITEM_LEFT);
    for (int i = 0; i < menu->count; i++)
        menu->item[i].w = *w - ween_ncm(MENU_ITEM_LEFT) * 2;
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
    DrawTextA(&dc, text, len, &r, DT_LEFT | DT_SINGLELINE);
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
        if (i == hot) { /* the open one is drawn selected, as win32 does */
            ween_surface_fill(s, ox + it->x, oy, it->w, h, WEEN_CAP_LEFT);
            fg = WEEN_WHITE;
        }
        if (it->flags & MF_GRAYED)
            fg = WEEN_SHADOW;
        /* One below centred, which is where the reference puts it. */
        draw_label(s, f, ox + it->x + ween_ncm(MENU_BAR_ITEMS_SPACE) / 2,
                   oy + (h - cell) / 2 + ween_ncm(1), it->text, len, fg);
    }
}

void ween_menu_draw_popup(HMENU menu, ween_surface *s, const ween_strike *f,
                          int w, int h, int hot)
{
    int check = ween_ncm(WEEN_NC_MENUCHECK);
    int cell = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 12;

    ween_surface_fill(s, 0, 0, w, h, WEEN_FACE); /* COLOR_MENU */
    ween_classic_edge(s, 0, 0, w, h, BDR_RAISEDOUTER | BDR_RAISEDINNER,
                      BF_RECT, NULL);
    if (!menu)
        return;

    for (int i = 0; i < menu->count; i++) {
        ween_menuitem *it = &menu->item[i];
        int len;
        const char *acc = accel_of(it->text, &len);
        ween_color fg = WEEN_BLACK;

        if (it->flags & MF_SEPARATOR) {
            /* an etched line across, inset from both borders */
            int y = it->y + it->h / 2;
            ween_surface_hline(s, it->x, y, it->w, WEEN_SHADOW);
            ween_surface_hline(s, it->x, y + 1, it->w, WEEN_WHITE);
            continue;
        }
        if (i == hot && !(it->flags & MF_GRAYED)) {
            ween_surface_fill(s, it->x, it->y, it->w, it->h, WEEN_CAP_LEFT);
            fg = WEEN_WHITE;
        }
        if (it->flags & MF_GRAYED)
            fg = WEEN_SHADOW;

        int ty = it->y + (it->h - cell) / 2;
        if (it->flags & MF_CHECKED) /* the tick sits in the left column */
            ween_classic_check(s, it->x + ween_ncm(2), ty, check - ween_ncm(4),
                               cell, DFCS_CHECKED | DFCS_FLAT);
        draw_label(s, f, it->x + check, ty, it->text, len, fg);
        if (acc)
            draw_label(s, f, it->x + it->w - ween_ncm(MENU_ITEM_LEFT) -
                                text_width(f, acc, (int)strlen(acc)) -
                                ween_ncm(6),
                       ty, acc, (int)strlen(acc), fg);
        if (it->popup) /* the submenu arrow, from Marlett's right triangle */
            ween_marlett_draw(ween_caption_font(), s, '8',
                              it->x + it->w - check, ty, cell, fg);
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

UINT ween_menu_track(HMENU menu, HWND owner, int screen_x, int screen_y)
{
    const ween_strike *f = ween_gui_font();
    int w = 0, h = 0, chosen = 0, done = 0;

    if (!menu || !ween_menu_count(menu) || !ween_active_backend)
        return 0;
    ensure_menu_class();
    ween_menu_popup_size(menu, f, &w, &h);

    if (owner)
        SendMessageA(owner, WM_INITMENUPOPUP, (WPARAM)menu, 0);

    HWND popup = CreateWindowExA(0, WEEN_MENU_CLASS, "", WS_POPUP | WS_VISIBLE,
                                 screen_x, screen_y, w, h, NULL, NULL, NULL,
                                 NULL);
    if (!popup)
        return 0;
    popup->menu = menu;
    popup->menu_hot = -1;
    ween_flush_paint();

    while (!done) {
        ween_event ev = ween_active_backend->next_event(popup->backend_win, -1);
        int mine = !ev.win || ev.win == popup->backend_win;
        int hot;
        switch (ev.kind) {
        case WEEN_EV_MOUSE_MOVE:
            if (!mine)
                break;
            hot = ween_menu_hit(menu, ev.x, ev.y);
            if (hot != popup->menu_hot) {
                popup->menu_hot = hot;
                popup->dirty = 1;
                ween_flush_paint();
            }
            break;
        case WEEN_EV_MOUSE_UP:
        case WEEN_EV_MOUSE_DOWN: {
            if (!mine) { /* a press anywhere else puts the menu away */
                done = 1;
                break;
            }
            hot = ween_menu_hit(menu, ev.x, ev.y);
            ween_menuitem *it = ween_menu_item(menu, hot);
            if (!it || (it->flags & MF_GRAYED))
                break;
            if (ev.kind == WEEN_EV_MOUSE_DOWN)
                break; /* win32 chooses on the release */
            if (it->popup) {
                /* a cascade opens beside the item it belongs to */
                chosen = ween_menu_track(it->popup, owner, screen_x + it->w,
                                         screen_y + it->y);
                done = 1;
            } else {
                chosen = it->id;
                done = 1;
            }
            break;
        }
        case WEEN_EV_KEY:
            if (ev.vk == VK_ESCAPE) {
                done = 1;
            } else if (ev.vk == VK_DOWN || ev.vk == VK_UP) {
                int n = ween_menu_count(menu), step = ev.vk == VK_DOWN ? 1 : -1;
                hot = popup->menu_hot;
                for (int i = 0; i < n; i++) { /* skip separators and grey ones */
                    hot = (hot + step + n) % n;
                    ween_menuitem *it = ween_menu_item(menu, hot);
                    if (it && !(it->flags & (MF_SEPARATOR | MF_GRAYED)))
                        break;
                }
                popup->menu_hot = hot;
                popup->dirty = 1;
                ween_flush_paint();
            } else if (ev.vk == VK_RETURN) {
                ween_menuitem *it = ween_menu_item(menu, popup->menu_hot);
                if (it && !(it->flags & MF_GRAYED)) {
                    chosen = it->popup ? 0 : it->id;
                    done = 1;
                }
            }
            break;
        case WEEN_EV_EXPOSE:
            popup->dirty = 1;
            ween_flush_paint();
            break;
        case WEEN_EV_END:
        case WEEN_EV_CLOSE:
            done = 1;
            break;
        default:
            break;
        }
    }

    popup->menu = NULL; /* the menu outlives the window showing it */
    DestroyWindow(popup);
    ween_flush_paint();
    return (UINT)chosen;
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
