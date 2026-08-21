/* The USER32-shaped windowing core: window classes, the window tree, the
 * message queue and loop, DefWindowProc's non-client chrome (caption bar,
 * close box, drag), and the built-in BUTTON and STATIC control classes.
 *
 * Model: a top-level window owns one backend (native) window and one software
 * surface; child windows are rectangles painted into the parent's surface and
 * receive their input via hit-testing, exactly the USER32 shape. v1 scope:
 * non-overlapping children, no subclassing.
 */

#define _POSIX_C_SOURCE 200112L /* clock_gettime */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ween_internal.h"

const ween_backend *ween_active_backend = NULL;

/* ---- system dpi --------------------------------------------------------- */

static int g_render_dpi = 0; /* 0 = not initialised */
static int g_zoom = 1;

/* Parse "Xft.dpi: <value>" out of an X resource-manager string — the same
 * resource Xft/GTK/Qt read; desktops set it when the user scales the UI. */
int ween_parse_xft_dpi(const char *resources)
{
    for (const char *line = resources; line && *line;) {
        if (strncmp(line, "Xft.dpi", 7) == 0) {
            const char *p = line + 7;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == ':') {
                double v = strtod(p + 1, NULL);
                if (v >= 48.0 && v <= 480.0)
                    return (int)(v + 0.5);
            }
        }
        const char *nl = strchr(line, '\n');
        line = nl ? nl + 1 : NULL;
    }
    return 0;
}

static void dpi_init(void)
{
    if (g_render_dpi)
        return;
    int dpi = 0;
    const char *e = getenv("WEEN32_DPI");
    if (e)
        dpi = atoi(e); /* explicit override (also the deterministic CI knob) */
    if (dpi < 48 || dpi > 480)
        dpi = 0;
    /* Autodetect from the desktop's Xft.dpi resource — unless running
     * headless, where determinism matters more than the host's scaling. */
    if (!dpi && !getenv("WEEN32_HEADLESS"))
        dpi = ween_x11_probe_dpi();
    if (dpi < 48 || dpi > 480)
        dpi = 96;
    /* Near-integer multiples >= 2x render native and pixel-double (the crisp
     * HiDPI path); fractional scales re-render at the scaled dpi. */
    int z = (dpi + 48) / 96;
    int d = dpi - z * 96;
    if (z >= 2 && d >= -5 && d <= 5) {
        g_zoom = z;
        g_render_dpi = 96;
    } else {
        g_zoom = 1;
        g_render_dpi = dpi;
    }
}

int ween_render_dpi(void)
{
    dpi_init();
    return g_render_dpi;
}

int ween_zoom(void)
{
    dpi_init();
    return g_zoom;
}

int ween_ncm(int base96)
{
    return MulDiv(base96, ween_render_dpi(), 96);
}

UINT GetDpiForSystem(void)
{
    dpi_init();
    return (UINT)(g_render_dpi * g_zoom);
}

/* Registered classes. Each is allocated on its own because a window holds a
 * pointer to its class for life, so the table cannot move them; the table of
 * pointers is what grows. There used to be room for 32, and RegisterClassA
 * returned 0 with no other sign once they were gone. */
static ween_class **g_classes;
static int g_nclasses, g_classes_cap;
/* Every top-level window, newest first. g_active is the one an event with no
 * window of its own belongs to — a headless script's input, and the focus
 * fallback. */
static struct ween_wnd *g_tops = NULL;
static struct ween_wnd *g_active = NULL;
static HWND g_focus = NULL;
static HWND g_capture = NULL;

/* The message queue grows as needed. It was 64 entries and dropped whatever
 * did not fit, which is what USER32 does at its own (far higher) limit, but at
 * 64 a burst of mouse moves could swallow a keystroke behind it. */
static MSG *g_queue;
static int g_qcap = 0, g_qhead = 0, g_qtail = 0;
static int g_quit = 0, g_quit_code = 0;

static LRESULT button_proc(HWND, UINT, WPARAM, LPARAM);
static LRESULT static_proc(HWND, UINT, WPARAM, LPARAM);
static void resize_top(struct ween_wnd *top, int w, int h);

/* ---- class registry --------------------------------------------------- */

/* Class names are case-insensitive, as on Windows ("button" == "BUTTON"). */
static int name_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a;
        char cb = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static const ween_class *find_class(LPCSTR name)
{
    for (int i = 0; i < g_nclasses; i++) {
        if (g_classes[i]->in_use && name_ieq(g_classes[i]->name, name))
            return g_classes[i];
    }
    return NULL;
}

static void register_builtin(const char *name, WNDPROC proc)
{
    if (find_class(name))
        return;
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.lpszClassName = name;
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);
}

static void ensure_builtins(void)
{
    register_builtin("BUTTON", button_proc);
    register_builtin("STATIC", static_proc);
    ween_register_controls();
}

/* Window text grows to fit. It used to be a fixed 128 bytes that truncated in
 * silence — the tail of a string simply went missing, with nothing returned to
 * say so. Capacity doubles, so repeated typing does not reallocate per key. */
int ween_wnd_reserve_text(struct ween_wnd *w, int len)
{
    if (len + 1 <= w->text_cap)
        return 1;
    int cap = w->text_cap ? w->text_cap : 32;
    while (cap < len + 1)
        cap *= 2;
    char *grown = realloc(w->text, (size_t)cap);
    if (!grown)
        return 0; /* the old text is still there and still valid */
    w->text = grown;
    w->text_cap = cap;
    return 1;
}

int ween_wnd_set_text(struct ween_wnd *w, const char *text)
{
    if (!text)
        text = "";
    int len = (int)strlen(text);
    if (!ween_wnd_reserve_text(w, len))
        return 0;
    memcpy(w->text, text, (size_t)len + 1);
    return 1;
}

ATOM RegisterClassA(const WNDCLASSA *wc)
{
    if (!wc || !wc->lpszClassName || !wc->lpfnWndProc)
        return 0;
    if (g_nclasses == g_classes_cap) {
        int cap = g_classes_cap ? g_classes_cap * 2 : 32;
        ween_class **grown = realloc(g_classes, (size_t)cap * sizeof *grown);
        if (!grown)
            return 0;
        g_classes = grown;
        g_classes_cap = cap;
    }
    ween_class *c = calloc(1, sizeof(*c));
    if (!c)
        return 0;
    size_t n = strlen(wc->lpszClassName) + 1;
    c->name = malloc(n);
    if (!c->name) {
        free(c);
        return 0;
    }
    memcpy(c->name, wc->lpszClassName, n);
    c->proc = wc->lpfnWndProc;
    c->background = wc->hbrBackground;
    c->in_use = 1;
    g_classes[g_nclasses++] = c;
    return (ATOM)g_nclasses;
}

/* ---- geometry ---------------------------------------------------------- */

static int has_caption(const struct ween_wnd *w)
{
    return !w->parent && (w->style & WS_CAPTION) == WS_CAPTION;
}

/* A window with a sizing border has a wider frame than a fixed one. */
int ween_frame_width(const struct ween_wnd *w)
{
    return ween_ncm((w->style & WS_THICKFRAME) ? WEEN_NC_SIZEFRAME
                                               : WEEN_NC_FRAME);
}

/* Client origin within the window's own rectangle. */
static void own_client_origin(const struct ween_wnd *w, int *ox, int *oy)
{
    if (has_caption(w)) {
        *ox = ween_frame_width(w);
        *oy = ween_frame_width(w) + ween_ncm(WEEN_NC_CAPTION);
    } else {
        *ox = ween_ex_edge(w);
        *oy = ween_ex_edge(w);
    }
}

BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD ex_style)
{
    (void)menu;
    (void)ex_style;
    if (!rect)
        return FALSE;
    if ((style & WS_CAPTION) == WS_CAPTION && !(style & WS_CHILD)) {
        int frame = ween_ncm((style & WS_THICKFRAME) ? WEEN_NC_SIZEFRAME
                                                     : WEEN_NC_FRAME);
        rect->left -= frame;
        rect->right += frame;
        rect->top -= frame + ween_ncm(WEEN_NC_CAPTION);
        rect->bottom += frame;
    }
    return TRUE;
}

BOOL AdjustWindowRect(LPRECT rect, DWORD style, BOOL menu)
{
    return AdjustWindowRectEx(rect, style, menu, 0);
}

BOOL GetClientRect(HWND wnd, LPRECT rect)
{
    if (!wnd || !rect)
        return FALSE;
    int ox, oy;
    own_client_origin(wnd, &ox, &oy);
    rect->left = 0;
    rect->top = 0;
    int trail = has_caption(wnd) ? ween_frame_width(wnd) : ween_ex_edge(wnd);
    rect->right = wnd->w - ox - trail;
    rect->bottom = wnd->h - oy - trail;
    return TRUE;
}

/* The client origin of `wnd` within its top-level surface. */
void ween_client_origin(HWND wnd, int *ox, int *oy)
{
    int x = 0, y = 0;
    for (const struct ween_wnd *w = wnd; w; w = w->parent) {
        int cox, coy;
        own_client_origin(w, &cox, &coy);
        x += cox;
        y += coy;
        if (w->parent) {
            x += w->x;
            y += w->y;
        }
    }
    *ox = x;
    *oy = y;
}

HWND ween_top_level(HWND wnd)
{
    struct ween_wnd *w = wnd;
    while (w && w->parent)
        w = w->parent;
    return w;
}

static RECT nc_close_rect(const struct ween_wnd *w)
{
    RECT r;
    int frame = ween_frame_width(w);
    int cap = ween_ncm(WEEN_NC_CAPTION);
    int bw = ween_ncm(WEEN_NC_BTN_W);
    int bh = ween_ncm(WEEN_NC_BTN_H);
    r.left = w->w - frame - ween_ncm(2) - bw;
    r.top = frame + (cap - bh) / 2;
    r.right = r.left + bw;
    r.bottom = r.top + bh;
    return r;
}

/* ---- creation / destruction ------------------------------------------- */

HWND CreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR window_name,
                     DWORD style, int x, int y, int w, int h,
                     HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    (void)inst;
    ensure_builtins();
    const ween_class *cls = find_class(class_name ? class_name : "");
    if (!cls)
        return NULL;

    struct ween_wnd *wnd = calloc(1, sizeof(*wnd));
    if (!wnd)
        return NULL;
    wnd->cls = cls;
    wnd->proc = cls->proc;
    wnd->style = style;
    wnd->ex_style = ex_style;
    wnd->scroll_max = 100;
    wnd->x = x == CW_USEDEFAULT ? 0 : x;
    wnd->y = y == CW_USEDEFAULT ? 0 : y;
    wnd->w = w;
    wnd->h = h;
    wnd->id = (UINT_PTR)menu;
    wnd->font = ween_gui_font();
    wnd->visible = (style & WS_VISIBLE) != 0;
    if (!ween_wnd_set_text(wnd, window_name)) {
        free(wnd);
        return NULL;
    }

    if (style & WS_CHILD) {
        if (!parent) {
            free(wnd);
            return NULL;
        }
        wnd->parent = parent;
        /* append to preserve creation (tab) order */
        struct ween_wnd **link = &parent->first_child;
        while (*link)
            link = &(*link)->next_sibling;
        *link = wnd;
    } else {
        if (!ween_active_backend) {
            /* WEEN32_HEADLESS=1 runs any app without a display (present goes
             * to $WEEN32_BMP if set) — used to screenshot examples in CI. */
            if (getenv("WEEN32_HEADLESS"))
                ween_active_backend = ween_backend_headless();
            else
                ween_active_backend = ween_backend_x11();
        }
        if (!ween_active_backend) {
            free(wnd);
            return NULL;
        }
        if (!ween_surface_init(&wnd->surface, w, h)) {
            free(wnd);
            return NULL;
        }
        wnd->backend_win = ween_active_backend->open(x, y, w, h, wnd->text);
        if (wnd->backend_win && (style & WS_THICKFRAME) &&
            ween_active_backend->set_resizable)
            ween_active_backend->set_resizable(wnd->backend_win, 1);
        if (!wnd->backend_win) {
            ween_surface_free(&wnd->surface);
            free(wnd);
            return NULL;
        }
        wnd->next_top = g_tops;
        g_tops = wnd;
        g_active = wnd;
        g_focus = wnd;
    }

    CREATESTRUCTA cs;
    memset(&cs, 0, sizeof(cs));
    cs.lpCreateParams = param;
    cs.hwndParent = parent;
    cs.x = wnd->x;
    cs.y = wnd->y;
    cs.cx = w;
    cs.cy = h;
    cs.style = (LONG)style;
    cs.lpszName = window_name;
    cs.lpszClass = class_name;
    SendMessageA(wnd, WM_CREATE, 0, (LPARAM)&cs);

    wnd->dirty = 1;
    if (wnd->parent)
        ween_top_level(wnd)->dirty = 1;
    return wnd;
}

BOOL DestroyWindow(HWND wnd)
{
    if (!wnd || wnd->destroyed)
        return FALSE;
    wnd->destroyed = 1;

    while (wnd->first_child) {
        struct ween_wnd *c = wnd->first_child;
        wnd->first_child = c->next_sibling;
        c->next_sibling = NULL;
        DestroyWindow(c);
    }
    SendMessageA(wnd, WM_DESTROY, 0, 0);

    if (wnd->parent) {
        struct ween_wnd **link = &wnd->parent->first_child;
        while (*link && *link != wnd)
            link = &(*link)->next_sibling;
        if (*link)
            *link = wnd->next_sibling;
    } else {
        if (ween_active_backend && wnd->backend_win)
            ween_active_backend->close(wnd->backend_win);
        ween_surface_free(&wnd->surface);
        for (struct ween_wnd **link = &g_tops; *link; link = &(*link)->next_top) {
            if (*link == wnd) {
                *link = wnd->next_top;
                break;
            }
        }
        if (g_active == wnd)
            g_active = g_tops;
    }
    if (g_focus == wnd)
        g_focus = g_active;
    if (g_capture == wnd)
        g_capture = NULL;
    ween_controls_free(wnd);
    ween_kill_timers_of(wnd);
    free(wnd->text);
    free(wnd);
    return TRUE;
}

BOOL ShowWindow(HWND wnd, int cmd)
{
    if (!wnd)
        return FALSE;
    BOOL was = wnd->visible;
    wnd->visible = cmd != SW_HIDE;
    ween_top_level(wnd)->dirty = 1;
    return was;
}

BOOL SetWindowTextA(HWND wnd, LPCSTR text)
{
    if (!wnd)
        return FALSE;
    if (!ween_wnd_set_text(wnd, text))
        return FALSE;
    SendMessageA(wnd, WM_SETTEXT, 0, (LPARAM)text);
    ween_top_level(wnd)->dirty = 1;
    return TRUE;
}

int GetWindowTextA(HWND wnd, LPSTR out, int max)
{
    if (!wnd || !out || max <= 0)
        return 0;
    int n = (int)strlen(wnd->text);
    if (n >= max)
        n = max - 1;
    memcpy(out, wnd->text, (size_t)n);
    out[n] = 0;
    return n;
}

BOOL MoveWindow(HWND wnd, int x, int y, int w, int h, BOOL repaint)
{
    if (!wnd)
        return FALSE;
    wnd->x = x;
    wnd->y = y;
    if (!wnd->parent && (w != wnd->w || h != wnd->h)) {
        resize_top(wnd, w, h);
        if (ween_active_backend && ween_active_backend->resize)
            ween_active_backend->resize(wnd->backend_win, wnd->w, wnd->h);
    } else {
        wnd->w = w;
        wnd->h = h;
    }
    if (repaint)
        ween_top_level(wnd)->dirty = 1;
    return TRUE;
}

HWND GetDlgItem(HWND dlg, int id)
{
    if (!dlg)
        return NULL;
    for (struct ween_wnd *c = dlg->first_child; c; c = c->next_sibling) {
        if ((int)c->id == id)
            return c;
    }
    return NULL;
}

int GetDlgCtrlID(HWND wnd)
{
    return wnd ? (int)wnd->id : 0;
}

HWND ween_focus_get(void)
{
    return g_focus;
}

/* Walk the child list in creation order to the next/previous WS_TABSTOP window
 * after `cur`, wrapping around — the dialog manager's Tab navigation. */
HWND ween_tab_next(HWND dlg, HWND cur, int forward)
{
    HWND list[64];
    int n = 0;
    for (struct ween_wnd *c = dlg->first_child; c && n < 64; c = c->next_sibling)
        if (c->visible && (c->style & WS_TABSTOP))
            list[n++] = c;
    if (n == 0)
        return NULL;
    int idx = -1;
    for (int i = 0; i < n; i++)
        if (list[i] == cur)
            idx = i;
    if (idx < 0)
        return list[0];
    idx = (idx + (forward ? 1 : n - 1)) % n;
    return list[idx];
}

HWND SetCapture(HWND wnd)
{
    HWND prev = g_capture;
    g_capture = wnd;
    return prev;
}

BOOL ReleaseCapture(void)
{
    g_capture = NULL;
    return TRUE;
}

HWND GetCapture(void)
{
    return g_capture;
}

BOOL EnableWindow(HWND wnd, BOOL enable)
{
    if (!wnd)
        return FALSE;
    BOOL was_disabled = (wnd->style & WS_DISABLED) != 0;
    if (enable)
        wnd->style &= ~(DWORD)WS_DISABLED;
    else
        wnd->style |= WS_DISABLED;
    if (was_disabled != !enable) {
        SendMessageA(wnd, WM_ENABLE, (WPARAM)enable, 0);
        ween_top_level(wnd)->dirty = 1;
    }
    return was_disabled;
}

BOOL IsWindowEnabled(HWND wnd)
{
    return wnd && !(wnd->style & WS_DISABLED);
}

BOOL CheckDlgButton(HWND dlg, int id, UINT check)
{
    HWND c = GetDlgItem(dlg, id);
    if (!c)
        return FALSE;
    SendMessageA(c, BM_SETCHECK, check, 0);
    return TRUE;
}

UINT IsDlgButtonChecked(HWND dlg, int id)
{
    HWND c = GetDlgItem(dlg, id);
    return c ? (UINT)SendMessageA(c, BM_GETCHECK, 0, 0) : 0;
}

BOOL CheckRadioButton(HWND dlg, int first, int last, int check)
{
    if (!dlg)
        return FALSE;
    for (struct ween_wnd *c = dlg->first_child; c; c = c->next_sibling)
        if ((int)c->id >= first && (int)c->id <= last)
            SendMessageA(c, BM_SETCHECK,
                         (int)c->id == check ? BST_CHECKED : BST_UNCHECKED, 0);
    return TRUE;
}

HWND SetFocus(HWND wnd)
{
    HWND prev = g_focus;
    if (prev == wnd)
        return prev;
    if (prev)
        SendMessageA(prev, WM_KILLFOCUS, (WPARAM)wnd, 0);
    g_focus = wnd;
    if (wnd)
        SendMessageA(wnd, WM_SETFOCUS, (WPARAM)prev, 0);
    return prev;
}

BOOL InvalidateRect(HWND wnd, const RECT *rect, BOOL erase)
{
    (void)rect;
    (void)erase;
    if (!wnd)
        return FALSE;
    ween_top_level(wnd)->dirty = 1;
    return TRUE;
}

/* ---- painting ----------------------------------------------------------- */

static struct ween_dc g_dc; /* single-threaded: one DC serves every paint */

HDC BeginPaint(HWND wnd, PAINTSTRUCT *ps)
{
    if (!wnd || !ps)
        return NULL;
    struct ween_wnd *top = ween_top_level(wnd);
    int ox, oy;
    ween_client_origin(wnd, &ox, &oy);

    memset(&g_dc, 0, sizeof(g_dc));
    g_dc.s = &top->surface;
    g_dc.org_x = ox;
    g_dc.org_y = oy;
    RECT cr;
    GetClientRect(wnd, &cr);
    g_dc.clip_w = cr.right;
    g_dc.clip_h = cr.bottom;
    g_dc.text_color = GetSysColor(COLOR_BTNTEXT);
    g_dc.bk_mode = TRANSPARENT;
    ween_dc_set_font(&g_dc, wnd->font);

    memset(ps, 0, sizeof(*ps));
    ps->hdc = &g_dc;
    ps->rcPaint = cr;
    return &g_dc;
}

BOOL EndPaint(HWND wnd, const PAINTSTRUCT *ps)
{
    (void)wnd;
    (void)ps;
    return TRUE; /* the surface is presented after the full paint pass */
}

/* Paint one window (background + WM_PAINT) and recurse into its children.
 *
 * Each window paints through a clip rectangle: its non-client edge within the
 * parent's clip, then its client area within that. Children of a shared
 * surface would otherwise scribble over each other — a long tree-view label
 * would run out past its own border. */
static void paint_tree(struct ween_wnd *w)
{
    struct ween_wnd *top = ween_top_level(w);
    RECT outer;
    int ox, oy;
    RECT cr;

    if (!w->visible)
        return;

    ween_surface_get_clip(&top->surface, &outer);
    ween_client_origin(w, &ox, &oy);
    GetClientRect(w, &cr);

    if (w->parent) {
        /* the frame sits outside the client area: clip it to the window rect */
        int edge = ween_ex_edge(w);
        int wx = ox - edge, wy = oy - edge;
        int l = wx > outer.left ? wx : outer.left;
        int t = wy > outer.top ? wy : outer.top;
        int r = wx + w->w < outer.right ? wx + w->w : outer.right;
        int b = wy + w->h < outer.bottom ? wy + w->h : outer.bottom;
        ween_surface_clip(&top->surface, l, t, r - l, b - t);
        ween_paint_ex_edge(w);
    }

    {   /* the client area, within whatever the parent allows */
        int l = ox > outer.left ? ox : outer.left;
        int t = oy > outer.top ? oy : outer.top;
        int r = ox + cr.right < outer.right ? ox + cr.right : outer.right;
        int b = oy + cr.bottom < outer.bottom ? oy + cr.bottom : outer.bottom;
        ween_surface_clip(&top->surface, l, t, r - l, b - t);
    }
    if (w->cls && w->cls->background)
        ween_surface_fill(&top->surface, ox, oy, cr.right, cr.bottom,
                          w->cls->background->color);
    SendMessageA(w, WM_PAINT, 0, 0);
    for (struct ween_wnd *c = w->first_child; c; c = c->next_sibling)
        paint_tree(c);
    ween_surface_clip(&top->surface, outer.left, outer.top,
                      outer.right - outer.left, outer.bottom - outer.top);
}

static void flush_one(struct ween_wnd *top)
{
    if (!top || !top->dirty)
        return;
    top->dirty = 0;
    ween_surface_clip(&top->surface, 0, 0, top->surface.w, top->surface.h);
    SendMessageA(top, WM_NCPAINT, 0, 0);
    paint_tree(top);
    ween_surface_clip(&top->surface, 0, 0, top->surface.w, top->surface.h);
    ween_popup_paint(); /* a dropped-down list goes over everything */
    if (ween_active_backend)
        ween_active_backend->present(top->backend_win, &top->surface);
}

void ween_flush_paint(void)
{
    /* Each top-level owns a surface and a backend window, so they are painted
     * and presented one at a time; nothing is shared but the paint code. */
    for (struct ween_wnd *t = g_tops; t;) {
        struct ween_wnd *next = t->next_top; /* a proc may destroy its window */
        flush_one(t);
        t = next;
    }
}

BOOL UpdateWindow(HWND wnd)
{
    if (!wnd)
        return FALSE;
    ween_flush_paint();
    return TRUE;
}

/* ---- timers ---------------------------------------------------------------
 *
 * A due timer posts WM_TIMER; the message loop asks how long it may wait
 * before the next one is due and hands that to the backend as its timeout.
 * Time is milliseconds from a monotonic clock, except under the headless
 * backend, where it only moves when a test says so (WEEN_EV_TIME) — that keeps
 * scripted runs deterministic, and keeps a repeating timer from making a
 * screenshot run that never ends. */

typedef struct ween_timer {
    HWND wnd;
    UINT_PTR id;
    UINT elapse;
    unsigned long due;
    TIMERPROC fn;
    struct ween_timer *next;
} ween_timer;

static ween_timer *g_timers;
static unsigned long g_virtual_ms; /* headless only */


static int clock_is_virtual(void)
{
    return ween_active_backend == ween_backend_headless();
}

static unsigned long now_ms(void)
{
    if (clock_is_virtual())
        return g_virtual_ms;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000 + (unsigned long)(ts.tv_nsec / 1000000);
}

UINT_PTR SetTimer(HWND wnd, UINT_PTR id, UINT elapse_ms, TIMERPROC fn)
{
    if (!wnd) { /* no window: the id is ours to choose */
        static UINT_PTR next_free = 1;
        id = next_free++;
    }
    ween_timer *t = NULL;
    for (ween_timer *p = g_timers; p; p = p->next) {
        if (p->wnd == wnd && p->id == id) {
            t = p; /* an id already running is reset, not duplicated */
            break;
        }
    }
    if (!t) {
        t = calloc(1, sizeof(*t));
        if (!t)
            return 0;
        t->next = g_timers;
        g_timers = t;
    }
    t->wnd = wnd;
    t->id = id;
    t->elapse = elapse_ms;
    t->fn = fn;
    t->due = now_ms() + elapse_ms;
    return id ? id : 1;
}

BOOL KillTimer(HWND wnd, UINT_PTR id)
{
    for (ween_timer **link = &g_timers; *link; link = &(*link)->next) {
        if ((*link)->wnd == wnd && (*link)->id == id) {
            ween_timer *dead = *link;
            *link = dead->next;
            free(dead);
            return TRUE;
        }
    }
    return FALSE;
}

void ween_kill_timers_of(HWND wnd)
{
    for (ween_timer **link = &g_timers; *link;) {
        if ((*link)->wnd == wnd) {
            ween_timer *dead = *link;
            *link = dead->next;
            free(dead);
        } else {
            link = &(*link)->next;
        }
    }
}

/* How long the loop may sleep: 0 if a timer is already due, -1 if none. */
static int timer_timeout(void)
{
    if (!g_timers)
        return -1;
    unsigned long now = now_ms(), soonest = 0;
    int have = 0;
    for (ween_timer *t = g_timers; t; t = t->next) {
        if (t->due <= now)
            return 0;
        if (!have || t->due < soonest) {
            soonest = t->due;
            have = 1;
        }
    }
    return (int)(soonest - now);
}

static void post_msg(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

static void fire_due_timers(void)
{
    unsigned long now = now_ms();
    for (ween_timer *t = g_timers; t; t = t->next) {
        if (t->due > now)
            continue;
        /* Step by the period rather than from now, so a timer keeps its
         * cadence instead of drifting by however late the loop was; if it has
         * fallen a whole period behind, give up the lost ticks and resync. */
        t->due += t->elapse;
        if (t->due <= now)
            t->due = now + t->elapse;
        post_msg(t->wnd, WM_TIMER, (WPARAM)t->id, (LPARAM)t->fn);
    }
}

/* ---- messages ------------------------------------------------------------ */

/* Doubles the ring, unrolling it so the entries come out in order. */
static int queue_grow(void)
{
    int cap = g_qcap ? g_qcap * 2 : 64;
    MSG *grown = malloc((size_t)cap * sizeof *grown);
    if (!grown)
        return 0;
    int n = 0;
    for (int i = g_qhead; i != g_qtail; i = (i + 1) % g_qcap)
        grown[n++] = g_queue[i];
    free(g_queue);
    g_queue = grown;
    g_qcap = cap;
    g_qhead = 0;
    g_qtail = n;
    return 1;
}

static void post_msg(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!g_qcap && !queue_grow())
        return;
    int next = (g_qtail + 1) % g_qcap;
    if (next == g_qhead) { /* full */
        if (!queue_grow())
            return; /* out of memory: drop, as USER32 drops to a full queue */
        next = (g_qtail + 1) % g_qcap;
    }
    g_queue[g_qtail].hwnd = wnd;
    g_queue[g_qtail].message = msg;
    g_queue[g_qtail].wParam = wp;
    g_queue[g_qtail].lParam = lp;
    g_qtail = next;
}

void PostQuitMessage(int code)
{
    g_quit = 1;
    g_quit_code = code;
}

LRESULT SendMessageA(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!wnd || !wnd->proc)
        return 0;
    return wnd->proc(wnd, msg, wp, lp);
}

LRESULT DispatchMessageA(const MSG *msg)
{
    if (!msg || !msg->hwnd)
        return 0;
    if (msg->message == WM_TIMER && msg->lParam) {
        /* win32 calls the TIMERPROC from here, not the window procedure */
        ((TIMERPROC)msg->lParam)(msg->hwnd, WM_TIMER, (UINT_PTR)msg->wParam,
                                 (DWORD)now_ms());
        return 0;
    }
    return SendMessageA(msg->hwnd, msg->message, msg->wParam, msg->lParam);
}

/* Turn a key press into the character message that follows it, as win32's
 * TranslateMessage does — the EDIT control lives on these. */
BOOL TranslateMessage(const MSG *msg)
{
    unsigned ch;
    if (!msg || msg->message != WM_KEYDOWN)
        return FALSE;
    ch = (unsigned)((msg->lParam >> 16) & 0xffff);
    if (!ch)
        switch (msg->wParam) { /* keys a layout always turns into characters */
        case VK_BACK:
            ch = '\b';
            break;
        case VK_TAB:
            ch = '\t';
            break;
        case VK_RETURN:
            ch = '\r';
            break;
        case VK_ESCAPE:
            ch = 27;
            break;
        default:
            return FALSE;
        }
    post_msg(msg->hwnd, WM_CHAR, (WPARAM)ch, msg->lParam);
    return TRUE;
}

/* Route a mouse event to the child under the point (or the capture), sending
 * `msg` with client-relative coordinates. */
/* The child under a point, or the window itself. A newly created child sits
 * at the top of the z-order, so the last one in the list that contains the
 * point is the one on top — the group box created before its radio buttons
 * must not swallow their clicks. */
static struct ween_wnd *child_at(struct ween_wnd *top, int x, int y)
{
    int cx0, cy0;
    struct ween_wnd *hit = top;
    ween_client_origin(top, &cx0, &cy0);
    for (struct ween_wnd *c = top->first_child; c; c = c->next_sibling)
        if (c->visible && x - cx0 >= c->x && x - cx0 < c->x + c->w &&
            y - cy0 >= c->y && y - cy0 < c->y + c->h)
            hit = c;
    return hit;
}

static void route_mouse(struct ween_wnd *top, UINT msg, int x, int y)
{
    int ox, oy;
    struct ween_wnd *dst = g_capture;
    if (!dst)
        dst = ween_popup_hit(x, y); /* an open drop-down is over everything */
    if (!dst)
        dst = child_at(top, x, y);
    ween_client_origin(dst, &ox, &oy);
    /* x,y are window coords of the top-level == surface coords */
    post_msg(dst, msg, 0, MAKELPARAM((WORD)(x - ox), (WORD)(y - oy)));
}

/* Grow or shrink the top-level window: the surface follows the backend, and
 * the app hears about it through WM_SIZE. */
static void resize_top(struct ween_wnd *top, int w, int h)
{
    RECT cr;
    if (w < 120)
        w = 120;
    if (h < 60)
        h = 60;
    if (w == top->w && h == top->h)
        return;
    top->w = w;
    top->h = h;
    ween_surface_resize(&top->surface, w, h);
    GetClientRect(top, &cr);
    SendMessageA(top, WM_SIZE, SIZE_RESTORED,
                 MAKELPARAM((WORD)cr.right, (WORD)cr.bottom));
    top->dirty = 1;
}

/* Dragging a sizing border: the pointer moves an edge, the window follows. */
static void nc_drag_size(struct ween_wnd *top, const ween_event *down, int edge)
{
    int last_x = down->x_root, last_y = down->y_root;
    for (;;) {
        ween_event ev = ween_active_backend->next_event(top->backend_win, -1);
        if (ev.kind == WEEN_EV_MOUSE_MOVE) {
            int dx = ev.x_root - last_x, dy = ev.y_root - last_y;
            int w = top->w, h = top->h;
            last_x = ev.x_root;
            last_y = ev.y_root;
            if (edge == HTRIGHT || edge == HTBOTTOMRIGHT || edge == HTTOPRIGHT)
                w += dx;
            if (edge == HTBOTTOM || edge == HTBOTTOMRIGHT || edge == HTBOTTOMLEFT)
                h += dy;
            if (w != top->w || h != top->h) {
                resize_top(top, w, h);
                if (ween_active_backend->resize)
                    ween_active_backend->resize(top->backend_win, top->w, top->h);
                ween_flush_paint();
            }
        } else if (ev.kind == WEEN_EV_MOUSE_UP || ev.kind == WEEN_EV_END) {
            return;
        } else if (ev.kind == WEEN_EV_RESIZE) {
            resize_top(top, ev.x, ev.y);
        }
    }
}

/* Non-client interactions that USER32 handled internally: dragging the window
 * by its caption and tracking the close box. */
static void nc_drag_caption(struct ween_wnd *top, const ween_event *down)
{
    int last_x = down->x_root, last_y = down->y_root;
    for (;;) {
        ween_event ev = ween_active_backend->next_event(top->backend_win, -1);
        if (ev.kind == WEEN_EV_MOUSE_MOVE) {
            ween_active_backend->move_by(top->backend_win, ev.x_root - last_x,
                                         ev.y_root - last_y);
            last_x = ev.x_root;
            last_y = ev.y_root;
        } else if (ev.kind == WEEN_EV_MOUSE_UP || ev.kind == WEEN_EV_END) {
            return;
        }
    }
}

static void nc_track_close(struct ween_wnd *top)
{
    top->nc_close_pressed = 1;
    top->dirty = 1;
    ween_flush_paint();
    for (;;) {
        ween_event ev = ween_active_backend->next_event(top->backend_win, -1);
        if (ev.kind == WEEN_EV_MOUSE_MOVE) {
            RECT c = nc_close_rect(top);
            int in = ev.x >= c.left && ev.x < c.right && ev.y >= c.top && ev.y < c.bottom;
            if (in != top->nc_close_pressed) {
                top->nc_close_pressed = in;
                top->dirty = 1;
                ween_flush_paint();
            }
        } else if (ev.kind == WEEN_EV_MOUSE_UP || ev.kind == WEEN_EV_END) {
            int fire = top->nc_close_pressed;
            top->nc_close_pressed = 0;
            top->dirty = 1;
            ween_flush_paint();
            if (fire)
                post_msg(top, WM_CLOSE, 0, 0);
            return;
        }
    }
}

/* Translate one backend event into posted messages. */
static void pump_event(struct ween_wnd *top, const ween_event *ev)
{
    switch (ev->kind) {
    case WEEN_EV_EXPOSE:
        top->dirty = 1;
        break;
    case WEEN_EV_MOUSE_DOWN: {
        LRESULT hit;
        if (ev->button != 1) /* the wheel arrives as buttons 4 and 5 */
            break;
        hit = SendMessageA(top, WM_NCHITTEST, 0,
                           MAKELPARAM((WORD)ev->x, (WORD)ev->y));
        if (hit == HTCAPTION)
            nc_drag_caption(top, ev);
        else if (hit == HTCLOSE)
            nc_track_close(top);
        else if (hit >= HTLEFT && hit <= HTBOTTOMRIGHT)
            nc_drag_size(top, ev, (int)hit);
        else {
            /* a child can claim a sizing corner too: that is what the status
             * bar's grip is */
            struct ween_wnd *child = child_at(top, ev->x, ev->y);
            LRESULT ch = child != top
                             ? SendMessageA(child, WM_NCHITTEST, 0,
                                            MAKELPARAM((WORD)ev->x, (WORD)ev->y))
                             : HTCLIENT;
            if (ch >= HTLEFT && ch <= HTBOTTOMRIGHT &&
                (top->style & WS_THICKFRAME))
                nc_drag_size(top, ev, (int)ch);
            else
                route_mouse(top, WM_LBUTTONDOWN, ev->x, ev->y);
        }
        break;
    }
    case WEEN_EV_MOUSE_UP:
        if (ev->button != 1)
            break;
        route_mouse(top, WM_LBUTTONUP, ev->x, ev->y);
        break;
    case WEEN_EV_MOUSE_MOVE:
        route_mouse(top, WM_MOUSEMOVE, ev->x, ev->y);
        break;
    case WEEN_EV_RESIZE: /* the window manager resized us */
        resize_top(top, ev->x, ev->y);
        break;
    case WEEN_EV_WHEEL:
        /* win32 sends the wheel to the focused window, not the one under the
         * pointer — a control scrolls once it has been clicked */
        post_msg(g_focus ? g_focus : (HWND)top, WM_MOUSEWHEEL,
                 MAKEWPARAM(0, (WORD)(short)(ev->button * 120)),
                 MAKELPARAM((WORD)ev->x, (WORD)ev->y));
        break;
    case WEEN_EV_KEY:
        /* the character rides in the high word, where win32 keeps the scan
         * code and repeat count; TranslateMessage turns it into WM_CHAR */
        post_msg(g_focus ? g_focus : (HWND)top, WM_KEYDOWN, ev->vk,
                 (LPARAM)(ev->ch << 16) | (ev->shift ? 1 : 0));
        break;
    case WEEN_EV_CLOSE:
        post_msg(top, WM_CLOSE, 0, 0);
        break;
    case WEEN_EV_END:
        g_quit = 1;
        break;
    case WEEN_EV_TIME: /* both are handled by the loop, before dispatch */
    case WEEN_EV_NONE:
        break;
    }
}

BOOL GetMessageA(LPMSG msg, HWND wnd, UINT min, UINT max)
{
    (void)wnd;
    (void)min;
    (void)max;
    if (!msg)
        return -1;
    for (;;) {
        if (g_qhead != g_qtail) {
            *msg = g_queue[g_qhead];
            g_qhead = (g_qhead + 1) % g_qcap;
            if (msg->hwnd && msg->hwnd->destroyed)
                continue;
            return TRUE;
        }
        if (g_quit) {
            memset(msg, 0, sizeof(*msg));
            msg->message = WM_QUIT;
            msg->wParam = (WPARAM)g_quit_code;
            return FALSE;
        }
        /* Idle: paint (WM_PAINT is lowest priority, as in USER32), then block
         * for input. */
        ween_flush_paint();
        if (!g_tops || !ween_active_backend) {
            g_quit = 1; /* the last window closed and nothing can arrive now */
            continue;
        }
        ween_event ev =
            ween_active_backend->next_event(g_tops->backend_win, timer_timeout());
        if (ev.kind == WEEN_EV_NONE) { /* the wait expired: a timer is due */
            fire_due_timers();
            continue;
        }
        if (ev.kind == WEEN_EV_TIME) { /* a test moved the clock forward */
            g_virtual_ms += (unsigned long)ev.x;
            fire_due_timers();
            continue;
        }
        struct ween_wnd *target = g_active;
        if (ev.win) {
            for (struct ween_wnd *t = g_tops; t; t = t->next_top)
                if (t->backend_win == ev.win)
                    target = t;
        }
        if (!target)
            continue;
        g_active = target;
        pump_event(target, &ev);
    }
}

/* ---- DefWindowProc --------------------------------------------------------- */

LRESULT DefWindowProcA(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp;
    switch (msg) {
    case WM_NCHITTEST: {
        if (!has_caption(wnd))
            return HTCLIENT;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT c = nc_close_rect(wnd);
        if (x >= c.left && x < c.right && y >= c.top && y < c.bottom)
            return HTCLOSE;
        int frame = ween_frame_width(wnd);
        if (wnd->style & WS_THICKFRAME) {
            /* the sizing border, corners first */
            int r = x >= wnd->w - frame, l = x < frame;
            int b = y >= wnd->h - frame, t = y < frame;
            if (b && r)
                return HTBOTTOMRIGHT;
            if (b && l)
                return HTBOTTOMLEFT;
            if (t && r)
                return HTTOPRIGHT;
            if (t && l)
                return HTTOPLEFT;
            if (l)
                return HTLEFT;
            if (r)
                return HTRIGHT;
            if (t)
                return HTTOP;
            if (b)
                return HTBOTTOM;
        }
        if (y < frame + ween_ncm(WEEN_NC_CAPTION) && y >= frame && x >= frame &&
            x < wnd->w - frame)
            return HTCAPTION;
        return HTCLIENT;
    }

    case WM_NCPAINT: {
        if (wnd->parent)
            return 0;
        ween_surface *s = &wnd->surface;
        /* raised frame + face border */
        ween_surface_clear(s, WEEN_FACE);
        /* A window frame is the plain EDGE_RAISED: its outer line is
         * COLOR_3DLIGHT (face), the white one sits inside it. */
        ween_classic_edge(s, 0, 0, wnd->w, wnd->h, EDGE_RAISED, BF_RECT, NULL);
        if (!has_caption(wnd))
            return 0;
        /* caption gradient + title (bold, as Win2k captions were) + close */
        int frame = ween_frame_width(wnd);
        int cap = ween_ncm(WEEN_NC_CAPTION);
        /* the gradient holds its end colours behind the icon and the
         * buttons; see ween_classic_caption */
        int icon_w = (wnd->style & WS_SYSMENU) ? ween_ncm(WEEN_NC_SMICON) : 0;
        int buttons_w = ween_ncm(WEEN_NC_CAPTION) - 1;
        ween_classic_caption(s, frame, frame, wnd->w - 2 * frame, cap - 1,
                             icon_w, buttons_w);
        const ween_strike *f = ween_gui_font_bold();
        if (f) {
            int ty = frame + (cap - (f->ascent - f->descent)) / 2;
            /* the title starts two pixels in — after the system-menu icon,
             * when there is one */
            int tx = frame + ween_ncm(2);
            ween_strike_draw(f, s, tx, ty, wnd->text, (int)strlen(wnd->text),
                             WEEN_CAP_TEXT);
        }
        if (wnd->style & WS_SYSMENU) {
            RECT c = nc_close_rect(wnd);
            struct ween_dc dc;
            memset(&dc, 0, sizeof(dc));
            dc.s = s;
            dc.clip_w = wnd->w;
            dc.clip_h = wnd->h;
            DrawFrameControl(&dc, &c, DFC_CAPTION,
                             DFCS_CAPTIONCLOSE |
                                 (wnd->nc_close_pressed ? DFCS_PUSHED : 0));
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(wnd);
        return 0;

    case WM_GETFONT:
        return (LRESULT)GetStockObject(DEFAULT_GUI_FONT);

    default:
        return 0;
    }
}

/* ---- built-in BUTTON class --------------------------------------------------
 *
 * Ports of Wine's button.c: PB_Paint for push buttons, CB_Paint for check
 * boxes and radio buttons, GB_Paint for group boxes. The layout arithmetic is
 * theirs verbatim, because it is what puts every pixel where win32 puts it. */

static UINT button_type(const struct ween_wnd *w)
{
    return (UINT)(w->style & BS_TYPEMASK);
}

/* A disabled label is embossed: the text again in the highlight colour, one
 * pixel down and right, with grey over it. */
static void button_label(HWND wnd, HDC dc, RECT *r, UINT fmt)
{
    if (wnd->style & WS_DISABLED) {
        RECT sh = *r;
        sh.left++;
        sh.top++;
        sh.right++;
        sh.bottom++;
        SetTextColor(dc, GetSysColor(COLOR_BTNHIGHLIGHT));
        DrawTextA(dc, wnd->text, -1, &sh, fmt);
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    } else {
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    }
    DrawTextA(dc, wnd->text, -1, r, fmt);
}

/* The cell a label is centred within: the strike's, not the outline's. */
static int label_height(const struct ween_wnd *w)
{
    const ween_strike *f = w->font ? w->font : ween_gui_font();
    if (!f)
        return 13;
    return f->cell_h ? f->cell_h : f->ascent - f->descent;
}

static void pb_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    RECT r = ps->rcPaint;
    if (wnd->style & BS_DEFPUSHBUTTON) {
        /* the default ring: 1px black outline, button inset within */
        struct ween_wnd *top = ween_top_level(wnd);
        int ox, oy;
        ween_client_origin(wnd, &ox, &oy);
        ween_surface_rect(&top->surface, ox, oy, wnd->w, wnd->h, WEEN_BLACK);
        r.left += 1;
        r.top += 1;
        r.right -= 1;
        r.bottom -= 1;
    }
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    DrawEdge(dc, &r, wnd->pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT | BF_SOFT);
    RECT tr = r;
    if (wnd->pressed) {
        tr.left += 1;
        tr.top += 1;
        tr.right += 1;
        tr.bottom += 1;
    }
    button_label(wnd, dc, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* Wine's CB_Paint: a 13px box (at 96 dpi) on the left, the label beside it
 * half a '0' further right. */
static void cb_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT client = ps->rcPaint, rbox = client, rtext = client;
    int box = MulDiv(12, ween_render_dpi(), 96) + 1;
    int offset = f ? ween_strike_char_advance(f, '0') / 2 : 3;
    int lh = label_height(wnd), delta;
    UINT flags;

    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));

    rtext.left += box + offset;
    rbox.right = rbox.left + box;

    /* the label is vertically centred, and the box follows it */
    rtext.top = client.top + ((client.bottom - client.top) - lh) / 2;
    rtext.bottom = rtext.top + lh;
    rbox.top = rtext.top;
    rbox.bottom = rtext.bottom;
    delta = (rbox.bottom - rbox.top) - box;
    if (delta > 0) {
        rbox.bottom -= delta / 2 + 1;
        rbox.top = rbox.bottom - box;
    } else if (delta < 0) {
        rbox.top -= -delta / 2 + 1;
        rbox.bottom = rbox.top + box;
    }

    if (button_type(wnd) == BS_RADIOBUTTON ||
        button_type(wnd) == BS_AUTORADIOBUTTON)
        flags = DFCS_BUTTONRADIO;
    else if (wnd->check == BST_INDETERMINATE)
        flags = DFCS_BUTTON3STATE;
    else
        flags = DFCS_BUTTONCHECK;
    if (wnd->check == BST_CHECKED || wnd->check == BST_INDETERMINATE)
        flags |= DFCS_CHECKED;
    if (wnd->pressed)
        flags |= DFCS_PUSHED;
    if (wnd->style & WS_DISABLED)
        flags |= DFCS_INACTIVE;
    DrawFrameControl(dc, &rbox, DFC_BUTTON, flags);

    /* Wine's BUTTON_CalcLabelRect shifts a side-aligned label one pixel away
     * from its edge, to leave room for the focus rectangle. */
    rtext.left++;
    rtext.right++;
    button_label(wnd, dc, &rtext, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

/* Wine's GB_Paint: an etched frame starting half a text height down, with the
 * label punched out of it. */
static void gb_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT client = ps->rcPaint, frame = client, r;
    int lh = label_height(wnd);
    int tw = f ? ween_strike_text_extent(f, wnd->text, (int)strlen(wnd->text)) : 0;

    frame.top += lh / 2 - 1;
    DrawEdge(dc, &frame, EDGE_ETCHED, BF_RECT);

    if (!wnd->text[0])
        return;
    r = client;
    r.left += 7;
    r.right -= 7;
    r.top -= 1;
    r.bottom += 1;
    r.left++; /* the DT_LEFT / DT_TOP nudge, as in CalcLabelRect */
    r.right++;
    r.top++;
    r.bottom = r.top + lh;
    r.right = r.left + tw;
    /* one pixel of margin left, right and below, so the frame is erased */
    r.left--;
    r.right++;
    r.bottom++;
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    r.left++;
    r.right--;
    r.bottom--;
    button_label(wnd, dc, &r, DT_LEFT | DT_SINGLELINE);
}

/* An auto button toggles on click; an auto radio also clears its group. */
static void button_click(HWND wnd)
{
    switch (button_type(wnd)) {
    case BS_AUTOCHECKBOX:
        wnd->check = wnd->check == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED;
        break;
    case BS_AUTO3STATE:
        wnd->check = wnd->check == BST_UNCHECKED     ? BST_CHECKED
                     : wnd->check == BST_CHECKED     ? BST_INDETERMINATE
                                                     : BST_UNCHECKED;
        break;
    case BS_AUTORADIOBUTTON:
        if (wnd->parent) {
            /* the group runs from the last WS_GROUP at or before this button
             * up to the next one after it */
            struct ween_wnd *c, *start = wnd->parent->first_child;
            for (c = wnd->parent->first_child; c; c = c->next_sibling) {
                if (c->style & WS_GROUP)
                    start = c;
                if (c == wnd)
                    break;
            }
            for (c = start; c; c = c->next_sibling) {
                if (c != start && (c->style & WS_GROUP))
                    break;
                if (button_type(c) == BS_AUTORADIOBUTTON)
                    c->check = c == wnd ? BST_CHECKED : BST_UNCHECKED;
            }
            ween_top_level(wnd)->dirty = 1;
        }
        wnd->check = BST_CHECKED;
        break;
    default:
        break;
    }
    if (wnd->parent)
        SendMessageA(wnd->parent, WM_COMMAND,
                     MAKEWPARAM((WORD)wnd->id, BN_CLICKED), (LPARAM)wnd);
}

static LRESULT button_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        switch (button_type(wnd)) {
        case BS_OWNERDRAW: {
            /* the parent paints it (WM_DRAWITEM), as on Windows */
            DRAWITEMSTRUCT dis;
            memset(&dis, 0, sizeof(dis));
            dis.CtlType = ODT_BUTTON;
            dis.CtlID = (UINT)wnd->id;
            dis.itemAction = ODA_DRAWENTIRE;
            dis.itemState = wnd->pressed ? ODS_SELECTED : 0;
            dis.hwndItem = wnd;
            dis.hDC = dc;
            dis.rcItem = ps.rcPaint;
            if (wnd->parent)
                SendMessageA(wnd->parent, WM_DRAWITEM, (WPARAM)wnd->id,
                             (LPARAM)&dis);
            break;
        }
        case BS_CHECKBOX:
        case BS_AUTOCHECKBOX:
        case BS_RADIOBUTTON:
        case BS_3STATE:
        case BS_AUTO3STATE:
        case BS_AUTORADIOBUTTON:
            cb_paint(wnd, dc, &ps);
            break;
        case BS_GROUPBOX:
            gb_paint(wnd, dc, &ps);
            break;
        default:
            pb_paint(wnd, dc, &ps);
            break;
        }
        EndPaint(wnd, &ps);
        return 0;
    }
    case BM_GETCHECK:
        return (LRESULT)wnd->check;
    case BM_SETCHECK:
        wnd->check = (UINT)wp;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case BM_GETSTATE:
        return (LRESULT)(wnd->check | (wnd->pressed ? BST_PUSHED : 0));
    case WM_LBUTTONDOWN:
        if (wnd->style & WS_DISABLED || button_type(wnd) == BS_GROUPBOX)
            return 0;
        g_capture = wnd;
        wnd->pressed = 1;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (g_capture == wnd) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int in = x >= 0 && x < wnd->w && y >= 0 && y < wnd->h;
            if (in != wnd->pressed) {
                wnd->pressed = in;
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_capture == wnd) {
            g_capture = NULL;
            int fire = wnd->pressed;
            wnd->pressed = 0;
            InvalidateRect(wnd, NULL, FALSE);
            if (fire)
                button_click(wnd);
        }
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- built-in STATIC class ----------------------------------------------------- */

static LRESULT static_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        UINT fmt = DT_SINGLELINE;
        switch (wnd->style & 0x03) {
        case SS_CENTER:
            fmt |= DT_CENTER;
            break;
        case SS_RIGHT:
            fmt |= DT_RIGHT;
            break;
        default:
            fmt |= DT_LEFT;
            break;
        }
        RECT r = ps.rcPaint;
        DrawTextA(dc, wnd->text, -1, &r, fmt);
        EndPaint(wnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}
