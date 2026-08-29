/* Headless backend: no display, no input devices. Tests inject scripted
 * events and read the presented surface (or a BMP dump) back — the whole
 * win32 layer is exercised end to end without a window system. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

/* The injected event queue. **It was a fixed ring of 256 that dropped
 * silently once it filled**, and `inject_script` pushes an entire script
 * before the message loop runs — so any script asking for more than 255
 * events got 255 of them, with no error, no warning and a zero exit status.
 *
 * What that cost: **no instrument in either repository could put more than
 * three lines of text into an editor.** jd reported that WordPad stops
 * accepting text and that no scrollbar appears; the second half is real, and
 * the first was partly this — a harness quietly delivering a quarter of what
 * it was asked. Measured before it was changed, by typing the same words at
 * six lengths and reading how far down the ink reached:
 *
 *      chars    63  127  255  511  1023  2047
 *      rows     16   29   42   42    42    42
 *
 * It grows instead. A dropped event is not a thing this can afford to be
 * quiet about: **every check written on top of the harness inherits its
 * silence**, and a script that types less than it says will fail or pass for
 * a reason nobody can see from the result. */
static ween_event *g_events;
static int g_ev_cap, g_ev_head, g_ev_tail;
static char g_bmp_path[256];
static ween_surface g_last; /* shallow copy of the last presented surface */

void ween_headless_inject(ween_event ev)
{
    if (g_ev_tail == g_ev_cap) {
        int cap = g_ev_cap ? g_ev_cap * 2 : 256;
        ween_event *grown = realloc(g_events, (size_t)cap * sizeof(*grown));
        if (!grown) {
            /* Loud, and fatal. The alternative is the bug this replaced. */
            fprintf(stderr, "ween32: cannot hold %d injected events\n", cap);
            abort();
        }
        g_events = grown;
        g_ev_cap = cap;
    }
    g_events[g_ev_tail++] = ev;
    /* Something is driving this program after its queue ran dry, so the
     * `WEEN_EV_END` that emptying produced did not mean what it said. */
    ween_unquit_scripted();
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
 * k:27". Lets any example be driven and screenshotted with no display.
 *
 *   d/u/m:X,Y   mouse down, up, move, at window coordinates
 *   D/U:X,Y     the same with the right button
 *   k:N         a virtual-key press
 *   K:N         the same with Shift held — Tab against Shift+Tab
 *   c:N / C:N   with Control, and with Control and Shift: an accelerator
 *   a:N         with Alt, which is how the menu bar is reached
 *   h:s h:c h:sc h:      hold a modifier over the presses that follow
 *   t:TEXT      **type it**, a character at a time. `_` is a space, and the
 *               run ends at the first real one. This is how a file name gets
 *               into a common dialog, which is the whole of what a program's
 *               Open and Save can be driven with
 *   r:W,H       the window system hands the app a new size
 *   w:MS        milliseconds of timer time to let pass
 *
 * `t:` and `r:` were missing from *this* list while being in the one in
 * docs/testing.md, and the gap cost something: I read this one, concluded a
 * file name could not be typed into a common dialog without a machine, and
 * said so on the record. Both were wrong and the docs had it right.
 *
 * The lesson is not "read the docs" -- it is that **a list that gives eight
 * of its ten words is worse than one that gives none**, because the eight
 * make it look complete and nobody goes to look for the other two. If you add
 * a command here, add it in both places or in neither. */
static void inject_script(const char *script)
{
    const char *p = script;
    int hold_shift = 0, hold_ctrl = 0;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        char kind = *p;
        ween_event ev;
        memset(&ev, 0, sizeof(ev));
        if ((kind == 'k' || kind == 'K' || kind == 'c' || kind == 'C' ||
             kind == 'a') &&
            p[1] == ':') {
            ev.kind = WEEN_EV_KEY;
            /* a virtual key only: VK_END and '#' share a code, so typing is
             * what t: is for. The capital holds Shift down over it, which is
             * the difference between Tab and Shift+Tab; c and C hold Control
             * as well, which is how an accelerator is pressed; a holds Alt,
             * which is how the menu bar is reached from the keyboard — and
             * with it the underlines a window shows once it has been. */
            ev.shift = kind == 'K' || kind == 'C';
            ev.ctrl = kind == 'c' || kind == 'C';
            ev.alt = kind == 'a';
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
        } else if (kind == 'r' && p[1] == ':') {
            /* r:800,600 — the window system hands the app a new size, which
             * is what a person dragging a frame does sixty times a second */
            char *end;
            ev.kind = WEEN_EV_RESIZE;
            ev.x = (int)strtol(p + 2, &end, 10);
            if (*end == ',')
                ev.y = (int)strtol(end + 1, &end, 10);
            p = end;
            ween_headless_inject(ev);
        } else if (kind == 'w' && p[1] == ':') {
            ev.kind = WEEN_EV_TIME; /* w:500 — let 500ms of timer time pass */
            ev.x = (int)strtol(p + 2, (char **)&p, 10);
            ween_headless_inject(ev);
        } else if (kind == 'h' && p[1] == ':') {
            /* h:s — Shift is held over the presses that follow, h:c Control,
             * h:sc both, h: neither. A press carries the modifier keys the
             * way a key press does, and Shift and a click on a second file is
             * how the run between two of them is taken. */
            hold_shift = hold_ctrl = 0;
            for (p += 2; *p && *p != ' '; p++) {
                if (*p == 's')
                    hold_shift = 1;
                else if (*p == 'c')
                    hold_ctrl = 1;
            }
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
            ev.shift = hold_shift;
            ev.ctrl = hold_ctrl;
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
    int raised;    /* when it was last put in front, counting from one */
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

/* The fake window system's stacking: a stamp rather than a list, because
 * what a test needs to ask is "which of these two was put in front last",
 * and a counter answers that without inventing an order for windows nobody
 * has raised. */
static int g_raise_clock;

static void hl_raise(void *win)
{
    if (win)
        ((hl_win *)win)->raised = ++g_raise_clock;
}

int ween_headless_window_raised(void *win)
{
    return win ? ((hl_win *)win)->raised : 0;
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

static void hl_present(void *win, const ween_surface *s, const RECT *damage)
{
    /* Nothing is on a screen here, so what changed does not matter: what is
     * written out is the whole frame, which is what a test compares. */
    (void)damage;
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
    ev = g_events[g_ev_head++];
    /* Drained: start again at the front rather than walk off the end. Tests
     * inject and consume in turn, so this is the common case, not the rare
     * one. */
    if (g_ev_head == g_ev_tail)
        g_ev_head = g_ev_tail = 0;
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
    static const ween_backend b = { .open = hl_open,
                                    .present = hl_present,
                                    .move_by = hl_move_by,
                                    .resize = hl_resize,
                                    /* nothing answers: a script says what the
                                       window system did */
                                    .resize_is_answered = 0,
                                    .set_resizable = hl_set_resizable,
                                    .show = hl_show,
                                    .raise = hl_raise,
                                    .set_cursor = hl_set_cursor,
                                    /* no screen: the classic desktop stands,
                                       so a render is the same everywhere */
                                    .screen_size = NULL,
                                    .origin = hl_origin,
                                    .next_event = hl_next_event,
                                    .close = hl_close };
    return &b;
}
