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
        if (it->popup) /* the mark that says a cascade opens from here */
            ween_classic_menu_arrow(s, it->x + it->w - check, ty, cell, fg);
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
#define WEEN_MENU_CASCADE_OVERLAP 3 /* a cascade sits this far over its parent */

typedef struct {
    HWND owner;    /* who hears WM_COMMAND */
    HWND bar_wnd;  /* the window whose bar is open, NULL for TrackPopupMenu */
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
    HWND wnd = CreateWindowExA(0, WEEN_MENU_CLASS, "", WS_POPUP | WS_VISIBLE, x,
                               y, w, h, NULL, NULL, NULL, NULL);
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

/* Open the cascade belonging to the highlighted item of `depth`, beside it. */
static void level_open_cascade(menu_session *s, int depth)
{
    menu_level *l = &s->level[depth];
    ween_menuitem *it = ween_menu_item(l->menu, l->hot);
    if (!it || !it->popup || (it->flags & MF_GRAYED))
        return;
    level_close_to(s, depth + 1);
    level_open(s, it->popup, l->wnd->x + l->wnd->w - WEEN_MENU_CASCADE_OVERLAP,
               l->wnd->y + it->y - WEEN_MENU_CASCADE_OVERLAP);
}

static void level_set_hot(menu_session *s, int depth, int hot)
{
    menu_level *l = &s->level[depth];
    if (l->hot == hot)
        return;
    l->hot = hot;
    l->wnd->menu_hot = hot;
    l->wnd->dirty = 1;
    /* anything opened under the old item goes with it */
    level_close_to(s, depth + 1);
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
        if (it && !(it->flags & (MF_SEPARATOR | MF_GRAYED)))
            break;
    }
    level_set_hot(s, depth, hot);
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
    if (!s->bar_wnd || index < 0 || index >= ween_menu_count(s->bar))
        return;
    it = ween_menu_item(s->bar, index);
    if (!it || !it->popup || (it->flags & MF_GRAYED))
        return;
    level_close_to(s, 0);
    s->bar_index = index;
    s->bar_wnd->menu_hot = index;
    s->bar_wnd->dirty = 1;
    frame = ween_frame_width(s->bar_wnd);
    bar_y = frame + ween_ncm(WEEN_NC_CAPTION);
    level_open(s, it->popup, s->bar_wnd->x + frame + it->x,
               s->bar_wnd->y + bar_y + ween_ncm(WEEN_NC_MENU));
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

static void session_run(menu_session *s)
{
    while (!s->done && s->depth > 0) {
        ween_flush_paint();
        ween_event ev =
            ween_active_backend->next_event(s->level[0].wnd->backend_win, -1);
        int lvl = level_of(s, ev.win);
        int on_bar = s->bar_wnd && ev.win == s->bar_wnd->backend_win;
        int deepest = s->depth - 1;

        switch (ev.kind) {
        case WEEN_EV_MOUSE_MOVE:
            if (lvl >= 0) {
                int hot = ween_menu_hit(s->level[lvl].menu, ev.x, ev.y);
                if (hot >= 0 || s->depth == lvl + 1) {
                    level_set_hot(s, lvl, hot);
                    if (hot >= 0)
                        level_open_cascade(s, lvl);
                }
            } else if (on_bar) {
                /* sliding along the bar with the button down switches
                 * drop-downs, which is how win32 menus are used */
                int frame = ween_frame_width(s->bar_wnd);
                int bar_y = frame + ween_ncm(WEEN_NC_CAPTION);
                int hit = ween_menu_hit(s->bar, ev.x - frame, ev.y - bar_y);
                if (hit >= 0 && hit != s->bar_index)
                    bar_open(s, hit);
            }
            break;

        case WEEN_EV_MOUSE_DOWN:
            if (lvl < 0 && !on_bar)
                s->done = 1; /* a press outside puts the whole menu away */
            break;

        case WEEN_EV_MOUSE_UP:
            if (lvl >= 0) {
                int hot = ween_menu_hit(s->level[lvl].menu, ev.x, ev.y);
                if (hot >= 0)
                    level_activate(s, lvl, hot);
            }
            break;

        case WEEN_EV_KEY:
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
                } else {
                    bar_step(s, 1);
                }
                break;
            }
            case VK_LEFT:
                if (s->depth > 1)
                    level_close(s);
                else
                    bar_step(s, -1);
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
            for (int i = 0; i < s->depth; i++)
                s->level[i].wnd->dirty = 1;
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
    if (s->bar_wnd) {
        s->bar_wnd->menu_hot = -1;
        s->bar_wnd->dirty = 1;
    }
    ween_flush_paint();
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
    if (!level_open(&s, menu, screen_x, screen_y))
        return 0;
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
