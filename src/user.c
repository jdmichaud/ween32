/* The USER32-shaped windowing core: window classes, the window tree, the
 * message queue and loop, DefWindowProc's non-client chrome (caption bar,
 * close box, drag), and the built-in BUTTON and STATIC control classes.
 *
 * Model: a top-level window owns one backend (native) window and one software
 * surface; child windows are rectangles painted into the parent's surface and
 * receive their input via hit-testing, exactly the USER32 shape. v1 scope:
 * one top-level window, non-overlapping children, no subclassing.
 */

#include <stdlib.h>
#include <string.h>

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

static ween_class g_classes[WEEN_MAX_CLASSES];
static struct ween_wnd *g_top = NULL;
static HWND g_focus = NULL;
static HWND g_capture = NULL;

#define QUEUE_LEN 64
static MSG g_queue[QUEUE_LEN];
static int g_qhead = 0, g_qtail = 0;
static int g_quit = 0, g_quit_code = 0;

static LRESULT button_proc(HWND, UINT, WPARAM, LPARAM);
static LRESULT static_proc(HWND, UINT, WPARAM, LPARAM);

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
    for (int i = 0; i < WEEN_MAX_CLASSES; i++) {
        if (g_classes[i].in_use && name_ieq(g_classes[i].name, name))
            return &g_classes[i];
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
}

ATOM RegisterClassA(const WNDCLASSA *wc)
{
    if (!wc || !wc->lpszClassName || !wc->lpfnWndProc)
        return 0;
    for (int i = 0; i < WEEN_MAX_CLASSES; i++) {
        if (!g_classes[i].in_use) {
            ween_class *c = &g_classes[i];
            strncpy(c->name, wc->lpszClassName, sizeof(c->name) - 1);
            c->proc = wc->lpfnWndProc;
            c->background = wc->hbrBackground;
            c->in_use = 1;
            return (ATOM)(i + 1);
        }
    }
    return 0;
}

/* ---- geometry ---------------------------------------------------------- */

static int has_caption(const struct ween_wnd *w)
{
    return !w->parent && (w->style & WS_CAPTION) == WS_CAPTION;
}

/* Client origin within the window's own rectangle. */
static void own_client_origin(const struct ween_wnd *w, int *ox, int *oy)
{
    if (has_caption(w)) {
        *ox = ween_ncm(WEEN_NC_FRAME);
        *oy = ween_ncm(WEEN_NC_FRAME) + ween_ncm(WEEN_NC_CAPTION);
    } else {
        *ox = 0;
        *oy = 0;
    }
}

BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD ex_style)
{
    (void)menu;
    (void)ex_style;
    if (!rect)
        return FALSE;
    if ((style & WS_CAPTION) == WS_CAPTION && !(style & WS_CHILD)) {
        int frame = ween_ncm(WEEN_NC_FRAME);
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
    rect->right = wnd->w - ox - (has_caption(wnd) ? ween_ncm(WEEN_NC_FRAME) : 0);
    rect->bottom = wnd->h - oy - (has_caption(wnd) ? ween_ncm(WEEN_NC_FRAME) : 0);
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
    int frame = ween_ncm(WEEN_NC_FRAME);
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
    (void)ex_style;
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
    wnd->x = x == CW_USEDEFAULT ? 0 : x;
    wnd->y = y == CW_USEDEFAULT ? 0 : y;
    wnd->w = w;
    wnd->h = h;
    wnd->id = (UINT_PTR)menu;
    wnd->font = ween_gui_font();
    wnd->visible = (style & WS_VISIBLE) != 0;
    if (window_name)
        strncpy(wnd->text, window_name, WEEN_MAX_TEXT - 1);

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
        if (g_top) { /* v1: a single top-level window */
            free(wnd);
            return NULL;
        }
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
        wnd->backend_win = ween_active_backend->open(w, h, wnd->text);
        if (!wnd->backend_win) {
            ween_surface_free(&wnd->surface);
            free(wnd);
            return NULL;
        }
        g_top = wnd;
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
        if (g_top == wnd)
            g_top = NULL;
    }
    if (g_focus == wnd)
        g_focus = g_top;
    if (g_capture == wnd)
        g_capture = NULL;
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
    strncpy(wnd->text, text ? text : "", WEEN_MAX_TEXT - 1);
    wnd->text[WEEN_MAX_TEXT - 1] = 0;
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
    wnd->w = w;
    wnd->h = h; /* v1: resizing the top-level surface is unsupported */
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
    g_dc.font = wnd->font;

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

/* Paint one window (background + WM_PAINT) and recurse into its children. */
static void paint_tree(struct ween_wnd *w)
{
    if (!w->visible)
        return;
    if (w->cls && w->cls->background) {
        struct ween_wnd *top = ween_top_level(w);
        int ox, oy;
        ween_client_origin(w, &ox, &oy);
        RECT cr;
        GetClientRect(w, &cr);
        ween_surface_fill(&top->surface, ox, oy, cr.right, cr.bottom,
                          w->cls->background->color);
    }
    SendMessageA(w, WM_PAINT, 0, 0);
    for (struct ween_wnd *c = w->first_child; c; c = c->next_sibling)
        paint_tree(c);
}

void ween_flush_paint(void)
{
    struct ween_wnd *top = g_top;
    if (!top || !top->dirty)
        return;
    top->dirty = 0;
    SendMessageA(top, WM_NCPAINT, 0, 0);
    paint_tree(top);
    if (ween_active_backend)
        ween_active_backend->present(top->backend_win, &top->surface);
}

BOOL UpdateWindow(HWND wnd)
{
    if (!wnd)
        return FALSE;
    ween_flush_paint();
    return TRUE;
}

/* ---- messages ------------------------------------------------------------ */

static void post_msg(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int next = (g_qtail + 1) % QUEUE_LEN;
    if (next == g_qhead)
        return; /* full: drop, as USER32 drops posts to a full queue */
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
    return SendMessageA(msg->hwnd, msg->message, msg->wParam, msg->lParam);
}

BOOL TranslateMessage(const MSG *msg)
{
    (void)msg; /* WM_CHAR generation: not in v1 */
    return FALSE;
}

/* Route a mouse event to the child under the point (or the capture), sending
 * `msg` with client-relative coordinates. */
static void route_mouse(struct ween_wnd *top, UINT msg, int x, int y)
{
    int ox, oy;
    struct ween_wnd *dst = g_capture;
    if (!dst) {
        /* find the child containing the point (client coords of top) */
        int cx0, cy0;
        ween_client_origin(top, &cx0, &cy0);
        int cx = x - cx0, cy = y - cy0;
        dst = top;
        for (struct ween_wnd *c = top->first_child; c; c = c->next_sibling) {
            if (c->visible && cx >= c->x && cx < c->x + c->w && cy >= c->y &&
                cy < c->y + c->h) {
                dst = c;
                break;
            }
        }
    }
    ween_client_origin(dst, &ox, &oy);
    /* x,y are window coords of the top-level == surface coords */
    post_msg(dst, msg, 0, MAKELPARAM((WORD)(x - ox), (WORD)(y - oy)));
}

/* Non-client interactions that USER32 handled internally: dragging the window
 * by its caption and tracking the close box. */
static void nc_drag_caption(struct ween_wnd *top, const ween_event *down)
{
    int last_x = down->x_root, last_y = down->y_root;
    for (;;) {
        ween_event ev = ween_active_backend->next_event(top->backend_win);
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
        ween_event ev = ween_active_backend->next_event(top->backend_win);
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
        LRESULT hit = SendMessageA(top, WM_NCHITTEST, 0,
                                   MAKELPARAM((WORD)ev->x, (WORD)ev->y));
        if (hit == HTCAPTION)
            nc_drag_caption(top, ev);
        else if (hit == HTCLOSE)
            nc_track_close(top);
        else
            route_mouse(top, WM_LBUTTONDOWN, ev->x, ev->y);
        break;
    }
    case WEEN_EV_MOUSE_UP:
        route_mouse(top, WM_LBUTTONUP, ev->x, ev->y);
        break;
    case WEEN_EV_MOUSE_MOVE:
        route_mouse(top, WM_MOUSEMOVE, ev->x, ev->y);
        break;
    case WEEN_EV_KEY:
        post_msg(g_focus ? g_focus : (HWND)top, WM_KEYDOWN, ev->vk, 0);
        break;
    case WEEN_EV_CLOSE:
        post_msg(top, WM_CLOSE, 0, 0);
        break;
    case WEEN_EV_END:
        g_quit = 1;
        break;
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
            g_qhead = (g_qhead + 1) % QUEUE_LEN;
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
        if (!g_top || !ween_active_backend) {
            g_quit = 1;
            continue;
        }
        ween_event ev = ween_active_backend->next_event(g_top->backend_win);
        pump_event(g_top, &ev);
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
        int frame = ween_ncm(WEEN_NC_FRAME);
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
        int frame = ween_ncm(WEEN_NC_FRAME);
        int cap = ween_ncm(WEEN_NC_CAPTION);
        ween_classic_caption(s, frame, frame, wnd->w - 2 * frame, cap - 1);
        const ween_strike *f = ween_gui_font_bold();
        if (f) {
            int ty = frame + (cap - (f->ascent - f->descent)) / 2;
            ween_strike_draw(f, s, frame + ween_ncm(5), ty, wnd->text,
                             (int)strlen(wnd->text), WEEN_CAP_TEXT);
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

/* ---- built-in BUTTON class --------------------------------------------------- */

static LRESULT button_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        /* Owner-drawn: the parent paints it (WM_DRAWITEM), as on Windows. */
        if ((wnd->style & 0x0F) == BS_OWNERDRAW) {
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
            EndPaint(wnd, &ps);
            return 0;
        }
        RECT r = ps.rcPaint;
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
        DrawEdge(dc, &r, wnd->pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        RECT tr = r;
        if (wnd->pressed) {
            tr.left += 1;
            tr.top += 1;
            tr.right += 1;
            tr.bottom += 1;
        }
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        DrawTextA(dc, wnd->text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
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
            if (fire && wnd->parent)
                SendMessageA(wnd->parent, WM_COMMAND,
                             MAKEWPARAM((WORD)wnd->id, BN_CLICKED), (LPARAM)wnd);
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
