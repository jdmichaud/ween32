/* Headless backend: no display, no input devices. Tests inject scripted
 * events and read the presented surface (or a BMP dump) back — the whole
 * win32 layer is exercised end to end without a window system. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

#define MAX_INJECT 256

static ween_event g_events[MAX_INJECT];
static int g_ev_head = 0, g_ev_tail = 0;
static char g_bmp_path[256];
static ween_surface g_last; /* shallow copy of the last presented surface */

void ween_headless_inject(ween_event ev)
{
    int next = (g_ev_tail + 1) % MAX_INJECT;
    if (next == g_ev_head)
        return;
    g_events[g_ev_tail] = ev;
    g_ev_tail = next;
}

void ween_headless_set_bmp_path(const char *path)
{
    strncpy(g_bmp_path, path ? path : "", sizeof(g_bmp_path) - 1);
    g_bmp_path[sizeof(g_bmp_path) - 1] = 0;
}

const ween_surface *ween_headless_surface(void)
{
    return g_last.px ? &g_last : NULL;
}

/* WEEN32_SCRIPT: space-separated scripted input, e.g. "d:110,146 u:110,146
 * k:27" — d/u/m = mouse down/up/move at window coordinates, k = a virtual-key
 * press, K = the same with Shift held, c = with Control held and C = with
 * both (which is how an accelerator is reached), w = milliseconds of timer
 * time to let pass. Lets any example run and be screenshotted with no
 * display. */
static void inject_script(const char *script)
{
    const char *p = script;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        char kind = *p;
        ween_event ev;
        memset(&ev, 0, sizeof(ev));
        if ((kind == 'k' || kind == 'K' || kind == 'c' || kind == 'C') &&
            p[1] == ':') {
            ev.kind = WEEN_EV_KEY;
            /* a virtual key only: VK_END and '#' share a code, so typing is
             * what t: is for. The capital holds Shift down over it, which is
             * the difference between Tab and Shift+Tab; c and C hold Control
             * as well, which is how an accelerator is pressed. */
            ev.shift = kind == 'K' || kind == 'C';
            ev.ctrl = kind == 'c' || kind == 'C';
            ev.vk = (unsigned)strtol(p + 2, (char **)&p, 10);
            ween_headless_inject(ev);
        } else if (kind == 't' && p[1] == ':') {
            for (p += 2; *p && *p != ' '; p++) {
                memset(&ev, 0, sizeof(ev));
                ev.kind = WEEN_EV_KEY;
                ev.ch = (unsigned char)(*p == '_' ? ' ' : *p);
                ev.vk = ev.ch >= 'a' && ev.ch <= 'z' ? ev.ch - 32 : ev.ch;
                ween_headless_inject(ev);
            }
        } else if (kind == 'w' && p[1] == ':') {
            ev.kind = WEEN_EV_TIME; /* w:500 — let 500ms of timer time pass */
            ev.x = (int)strtol(p + 2, (char **)&p, 10);
            ween_headless_inject(ev);
        } else if ((kind == 'd' || kind == 'u' || kind == 'm' || kind == 'D' ||
                    kind == 'U') && p[1] == ':') {
            char *end;
            ev.x = (int)strtol(p + 2, &end, 10);
            if (*end == ',')
                ev.y = (int)strtol(end + 1, &end, 10);
            p = end;
            ev.kind = (kind == 'd' || kind == 'D') ? WEEN_EV_MOUSE_DOWN
                      : (kind == 'u' || kind == 'U') ? WEEN_EV_MOUSE_UP
                                                     : WEEN_EV_MOUSE_MOVE;
            /* the capitals are the right button, which is what asks for a
             * context menu */
            ev.button = (kind == 'D' || kind == 'U') ? 3 : 1;
            ween_headless_inject(ev);
        } else {
            break; /* malformed: stop rather than loop */
        }
    }
}

/* A handle per window, so the two can be told apart the way the X11 backend's
 * can. Injected events carry no window: they go to the active one, which is
 * the newest — a script drives one window at a time. Each handle is allocated
 * so a closed window's address is never handed to the next one.
 *
 * A handle carries a letterbox, which is what makes this a fake window system
 * rather than a hole where one should be: a test can say the window manager
 * made the window a different size from the buffer being drawn into it, and
 * injected pointer coordinates then come back through the same mapping the
 * X11 backend uses. Without that, no test could reach the class of bug where
 * what is drawn and what is clicked disagree. */

#define MAX_WINDOWS 8

typedef struct {
    ween_letterbox box;
    int open;
    int cursor;    /* the shape last asked for, so a test can see it */
    int unmanaged; /* placed by the application rather than by the manager */
    int x, y;      /* and, if it is, where it was placed */
    int shown;     /* on the screen: a window kept back writes no frame */
} hl_win;

static hl_win g_wins[MAX_WINDOWS];
static int g_win_w, g_win_h; /* what the "window manager" is imposing, if any */
static int g_win_x, g_win_y; /* and where it is putting every window */

/* Tell the fake window system to hand every window this size, whatever size
 * the app asked for — which is what a tiling window manager does. Zero puts
 * it back to giving the app what it asked for. */
void ween_headless_set_window_size(int w, int h)
{
    g_win_w = w;
    g_win_h = h;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_wins[i].open)
            ween_letterbox_window(&g_wins[i].box, w, h);
}

/* Where the last window the app placed itself was put. A menu is one of
 * those — the window system is told exactly where to put it and does — so
 * this is how a test sees where a drop-down landed. */
static int g_unmanaged_x, g_unmanaged_y;

void ween_headless_last_unmanaged_origin(int *x, int *y)
{
    *x = g_unmanaged_x;
    *y = g_unmanaged_y;
}

static void *hl_open(int x, int y, int w, int h, const char *title,
                     unsigned flags)
{
    (void)w;
    (void)h;
    (void)title;
    int unmanaged = (flags & WEEN_WIN_UNMANAGED) != 0;
    if (unmanaged) {
        g_unmanaged_x = x;
        g_unmanaged_y = y;
    }
    if (!g_bmp_path[0]) {
        const char *env = getenv("WEEN32_BMP");
        if (env)
            ween_headless_set_bmp_path(env);
    }
    static int scripted;
    const char *script = getenv("WEEN32_SCRIPT");
    if (script && !scripted) { /* once for the process, not once per window */
        scripted = 1;
        inject_script(script);
    }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_wins[i].open)
            continue;
        memset(&g_wins[i], 0, sizeof(g_wins[i]));
        g_wins[i].open = 1;
        g_wins[i].unmanaged = unmanaged;
        g_wins[i].x = x;
        g_wins[i].y = y;
        ween_letterbox_window(&g_wins[i].box, g_win_w ? g_win_w : w * ween_zoom(),
                              g_win_h ? g_win_h : h * ween_zoom());
        return &g_wins[i];
    }
    return NULL;
}

static void hl_show(void *win, int on)
{
    if (win)
        ((hl_win *)win)->shown = on;
}

/* Whether the fake window system has been given this window to show, which is
 * how a test sees that one made but not shown stays off the screen. */
int ween_headless_window_shown(void *win)
{
    return win ? ((hl_win *)win)->shown : 0;
}

static void hl_present(void *win, const ween_surface *s)
{
    if (win && !((hl_win *)win)->shown)
        return; /* made but not put up: nothing of it is on the screen */
    if (win) /* what the pointer will be offset against, as on a real display */
        ween_letterbox_shown(&((hl_win *)win)->box, s->w * ween_zoom(),
                             s->h * ween_zoom());
    g_last = *s; /* the surface outlives the pump in tests */
    if (!g_bmp_path[0])
        return;
    /* A path holding %d is written once per frame instead of once per run —
     * the only way to see a modal window, which is gone again by the time the
     * run ends. */
    char numbered[300];
    const char *path = g_bmp_path;
    if (strstr(g_bmp_path, "%d")) {
        static int frame;
        snprintf(numbered, sizeof(numbered), g_bmp_path, frame++);
        path = numbered;
    }
    int zoom = ween_zoom();
    if (zoom > 1) { /* capture what the screen would show */
        static ween_surface zbuf;
        if (zbuf.w != s->w * zoom || zbuf.h != s->h * zoom) {
            ween_surface_free(&zbuf);
            if (!ween_surface_init(&zbuf, s->w * zoom, s->h * zoom))
                return;
        }
        ween_surface_zoom_into(&zbuf, s, zoom);
        ween_surface_write_bmp(&zbuf, path);
        return;
    }
    ween_surface_write_bmp(s, path);
}

/* A window that asks to be a different size gets it, unless the fake window
 * manager is imposing one. A window that changes size after it is up — the
 * colour dialog does, when it is asked to show its other half — has to say
 * so, or the pointer goes on being mapped through a letterbox for the size
 * it was made, and a press lands somewhere other than where it was aimed:
 * exactly the class of bug the letterbox is here to catch. */
static void hl_resize(void *win, int w, int h)
{
    hl_win *hw = win;
    if (!hw || g_win_w || g_win_h)
        return;
    ween_letterbox_window(&hw->box, w * ween_zoom(), h * ween_zoom());
}

static void hl_set_resizable(void *win, int resizable)
{
    (void)win;
    (void)resizable;
}

static void hl_set_cursor(void *win, int shape, const ween_cursor *custom)
{
    /* Nothing draws a pointer here, so a picture is only remembered by the
     * shape it stands in for; a test can still read back what was asked. */
    (void)custom;
    if (win)
        ((hl_win *)win)->cursor = shape;
}

int ween_headless_cursor(void *win)
{
    return win ? ((hl_win *)win)->cursor : WEEN_CURSOR_ARROW;
}

/* Tell the fake window system to put every window here rather than where it
 * asked to be, which is the other half of what a tiling window manager does
 * to an application — and the half that decides where its menus land. */
void ween_headless_set_window_origin(int x, int y)
{
    g_win_x = x;
    g_win_y = y;
}

static void hl_origin(void *win, int *x, int *y)
{
    hl_win *w = win;
    int ox = 0, oy = 0;
    ween_letterbox_origin(&w->box, &ox, &oy);
    /* A window the manager placed is wherever the manager decided; one that
     * asked for a place — a menu, a box under a field — got it, and knowing
     * where it is is what lets a press over it be found. */
    *x = (w->unmanaged ? w->x : g_win_x) + ox;
    *y = (w->unmanaged ? w->y : g_win_y) + oy;
}

static void hl_move_by(void *win, int dx, int dy)
{
    hl_win *w = win;
    if (w && w->unmanaged) {
        w->x += dx;
        w->y += dy;
    }
}

static ween_event hl_next_event(void *win, int timeout_ms)
{
    (void)timeout_ms; /* nothing arrives on its own here: time is scripted */
    ween_event ev;
    if (g_ev_head == g_ev_tail) {
        memset(&ev, 0, sizeof(ev));
        ev.kind = WEEN_EV_END; /* script exhausted: end the message loop */
        return ev;
    }
    ev = g_events[g_ev_head];
    g_ev_head = (g_ev_head + 1) % MAX_INJECT;
    /* Injected pointer coordinates are window coordinates, as a real one's
     * are, and come back through the same mapping — through the letterbox of
     * the window the event names, which is not always the one the pump
     * happened to be waiting on. */
    if (ev.kind == WEEN_EV_MOUSE_DOWN || ev.kind == WEEN_EV_MOUSE_UP ||
        ev.kind == WEEN_EV_MOUSE_MOVE || ev.kind == WEEN_EV_WHEEL) {
        hl_win *w = ev.win ? (hl_win *)ev.win : (hl_win *)win;
        if (w)
            ween_letterbox_to_surface(&w->box, ween_zoom(), &ev.x, &ev.y);
    }
    return ev;
}

static void hl_close(void *win)
{
    if (win)
        ((hl_win *)win)->open = 0;
}

const ween_backend *ween_backend_headless(void)
{
    static const ween_backend b = { hl_open,           hl_present,
                                    hl_move_by,        hl_resize,
                                    hl_set_resizable,  hl_show,
                                    hl_set_cursor,     hl_origin,
                                    hl_next_event,     hl_close };
    return &b;
}
