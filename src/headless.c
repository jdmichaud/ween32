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
 * press, w = milliseconds of timer time to let pass. Lets any example run and
 * be screenshotted with no display. */
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
        if (kind == 'k' && p[1] == ':') {
            ev.kind = WEEN_EV_KEY;
            /* a virtual key only: VK_END and '#' share a code, so typing is
             * what t: is for */
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
        } else if ((kind == 'd' || kind == 'u' || kind == 'm') && p[1] == ':') {
            char *end;
            ev.x = (int)strtol(p + 2, &end, 10);
            if (*end == ',')
                ev.y = (int)strtol(end + 1, &end, 10);
            p = end;
            ev.kind = kind == 'd' ? WEEN_EV_MOUSE_DOWN
                      : kind == 'u' ? WEEN_EV_MOUSE_UP
                                    : WEEN_EV_MOUSE_MOVE;
            ev.button = 1;
            ween_headless_inject(ev);
        } else {
            break; /* malformed: stop rather than loop */
        }
    }
}

/* A handle per window, so the two can be told apart the way the X11 backend's
 * can. Injected events carry no window: they go to the active one, which is
 * the newest — a script drives one window at a time. Each handle is allocated
 * so a closed window's address is never handed to the next one. */

static void *hl_open(int x, int y, int w, int h, const char *title,
                     unsigned flags)
{
    (void)flags;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)title;
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
    return calloc(1, sizeof(int));
}

static void hl_present(void *win, const ween_surface *s)
{
    (void)win;
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

static void hl_resize(void *win, int w, int h)
{
    (void)win;
    (void)w;
    (void)h;
}

static void hl_set_resizable(void *win, int resizable)
{
    (void)win;
    (void)resizable;
}

static void hl_move_by(void *win, int dx, int dy)
{
    (void)win;
    (void)dx;
    (void)dy;
}

static ween_event hl_next_event(void *win, int timeout_ms)
{
    (void)win;
    (void)timeout_ms; /* nothing arrives on its own here: time is scripted */
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
    free(win);
}

const ween_backend *ween_backend_headless(void)
{
    static const ween_backend b = { hl_open,        hl_present,
                                    hl_move_by,     hl_resize,
                                    hl_set_resizable, hl_next_event,
                                    hl_close };
    return &b;
}
