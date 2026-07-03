/* Headless backend: no display, no input devices. Tests inject scripted
 * events and read the presented surface (or a BMP dump) back — the whole
 * win32 layer is exercised end to end without a window system. */

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

static void *hl_open(int w, int h, const char *title)
{
    (void)w;
    (void)h;
    (void)title;
    static int marker;
    return &marker;
}

static void hl_present(void *win, const ween_surface *s)
{
    (void)win;
    g_last = *s; /* the surface outlives the pump in tests */
    if (g_bmp_path[0])
        ween_surface_write_bmp(s, g_bmp_path);
}

static void hl_move_by(void *win, int dx, int dy)
{
    (void)win;
    (void)dx;
    (void)dy;
}

static ween_event hl_next_event(void *win)
{
    (void)win;
    ween_event ev;
    if (g_ev_head == g_ev_tail) {
        memset(&ev, 0, sizeof(ev));
        ev.kind = WEEN_EV_END; /* script exhausted: end the message loop */
        return ev;
    }
    ev = g_events[g_ev_head];
    g_ev_head = (g_ev_head + 1) % MAX_INJECT;
    return ev;
}

static void hl_close(void *win)
{
    (void)win;
}

const ween_backend *ween_backend_headless(void)
{
    static const ween_backend b = { hl_open, hl_present, hl_move_by,
                                    hl_next_event, hl_close };
    return &b;
}
