/* X11 backend (port of telemouse's window.zig): a borderless, fixed-size
 * native window that blits the finished surface (XPutImage) and yields raw
 * input events. It never draws — ween32 windows paint their own Win2k chrome,
 * so the WM's decorations are disabled via Motif hints.
 *
 * The surface's 0x00RRGGBB pixels match a 24-bit TrueColor ZPixmap directly on
 * little-endian hosts, so present() is a straight memory blit.
 *
 * Compiled only when WEEN_BACKEND_X11 is defined (link with -lX11); otherwise
 * ween_backend_x11() reports the backend unavailable. */

#include <stdio.h>
#define _POSIX_C_SOURCE 200112L /* select */

#include <stdlib.h>
#include <sys/select.h>
#include <string.h>

#include "ween_internal.h"

#ifndef WEEN_BACKEND_X11

const ween_backend *ween_backend_x11(void)
{
    return NULL;
}

int ween_x11_probe_dpi(void)
{
    return 0;
}

#else

/* Self-declared Xlib subset (no Xlib.h dependency at build time). */

typedef struct XDisplay XDisplay;
typedef unsigned long XWindow;
typedef unsigned long XAtom;
typedef unsigned long XPixmap;
typedef struct XGC XGC;

typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay *display;
    XWindow window;
    XWindow root;
    XWindow subwindow;
    unsigned long time;
    int x, y;
    int x_root, y_root;
    unsigned state;
    unsigned button; /* keycode for key events */
    int same_screen;
} XButtonEvent;

typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay *display;
    XWindow window;
    XAtom message_type;
    int format;
    long data[5];
} XClientMessageEvent;

typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay *display;
    XWindow event;
    XWindow window;
    int x, y;
    int width, height;
    int border_width;
    XWindow above;
    int override_redirect;
} XConfigureEvent;

typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay *display;
    XWindow window;
} XAnyEvent;

typedef union {
    int type;
    XAnyEvent xany;
    XButtonEvent xbutton;
    XClientMessageEvent xclient;
    XConfigureEvent xconfigure;
    long pad[24];
} XEvent;

/* Xlib's XImage, declared as far as the fields we set. Width, height and
 * bytes_per_line have to be updated together when the window is resized:
 * XPutImage reads the stride from here, so an image that still describes the
 * old width shears every row across the next one. */
typedef struct {
    int width, height;
    int xoffset;
    int format;
    char *data;
    int byte_order;
    int bitmap_unit;
    int bitmap_bit_order;
    int bitmap_pad;
    int depth;
    int bytes_per_line;
    int bits_per_pixel;
    unsigned char rest[200];
} XImage;

/* Xlib's XSetWindowAttributes, declared in full: the two fields that matter
 * here sit in the middle of it, so the tail has to be the right size. */
typedef struct {
    XPixmap background_pixmap;
    unsigned long background_pixel;
    XPixmap border_pixmap;
    unsigned long border_pixel;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    int save_under;
    long event_mask;
    long do_not_propagate_mask;
    int override_redirect;
    unsigned long colormap;
    unsigned long cursor;
} XSetWindowAttributes;

typedef struct {
    long flags;
    int x, y;
    int width, height;
    int min_width, min_height;
    int max_width, max_height;
    int width_inc, height_inc;
    int min_aspect_x, min_aspect_y;
    int max_aspect_x, max_aspect_y;
    int base_width, base_height;
    int win_gravity;
} XSizeHints;

enum {
    X_KeyPress = 2,
    X_ButtonPress = 4,
    X_ButtonRelease = 5,
    X_MotionNotify = 6,
    X_Expose = 12,
    X_ConfigureNotify = 22,
    X_ClientMessage = 33
};

#define X_ExposureMask (1L << 15)
#define X_ButtonPressMask (1L << 2)
#define X_ButtonReleaseMask (1L << 3)
#define X_PointerMotionMask (1L << 6)
#define X_KeyPressMask (1L << 0)
#define X_StructureNotifyMask (1L << 17)
#define X_ZPixmap 2
#define X_CWBitGravity (1L << 4)
#define X_NorthWestGravity 1
#define X_PPosition (1L << 2)
#define X_PSize (1L << 3)
#define X_PMinSize (1L << 4)
#define X_PMaxSize (1L << 5)

extern XDisplay *XOpenDisplay(const char *);
extern int XCloseDisplay(XDisplay *);
extern int XDestroyWindow(XDisplay *, XWindow);
extern int XFreeGC(XDisplay *, XGC *);
extern int XDefaultScreen(XDisplay *);
extern void *XDefaultVisual(XDisplay *, int);
extern int XDefaultDepth(XDisplay *, int);
extern XWindow XDefaultRootWindow(XDisplay *);
extern int XDisplayWidth(XDisplay *, int);
extern int XDisplayHeight(XDisplay *, int);
extern XWindow XCreateSimpleWindow(XDisplay *, XWindow, int, int, unsigned,
                                   unsigned, unsigned, unsigned long,
                                   unsigned long);
extern int XStoreName(XDisplay *, XWindow, const char *);
extern int XSelectInput(XDisplay *, XWindow, long);
extern int XMapWindow(XDisplay *, XWindow);
extern XGC *XCreateGC(XDisplay *, XWindow, unsigned long, void *);
extern XImage *XCreateImage(XDisplay *, void *, unsigned, int, int, char *,
                            unsigned, unsigned, int, int);
extern int XPutImage(XDisplay *, XWindow, XGC *, XImage *, int, int, int, int,
                     unsigned, unsigned);
extern int XSetForeground(XDisplay *, XGC *, unsigned long);
extern int XFillRectangle(XDisplay *, XWindow, XGC *, int, int, unsigned,
                          unsigned);
extern int XNextEvent(XDisplay *, XEvent *);
extern int XPending(XDisplay *);
extern int XConnectionNumber(XDisplay *);
extern int XCheckTypedWindowEvent(XDisplay *, XWindow, int, XEvent *);
extern int XChangeWindowAttributes(XDisplay *, XWindow, unsigned long,
                                   XSetWindowAttributes *);
extern int XFlush(XDisplay *);
extern XAtom XInternAtom(XDisplay *, const char *, int);
extern int XSetWMProtocols(XDisplay *, XWindow, XAtom *, int);
extern void XSetWMNormalHints(XDisplay *, XWindow, XSizeHints *);
extern int XMoveWindow(XDisplay *, XWindow, int, int);
extern int XResizeWindow(XDisplay *, XWindow, unsigned, unsigned);
extern int XChangeProperty(XDisplay *, XWindow, XAtom, XAtom, int, int,
                           const void *, int);
extern unsigned long XLookupKeysym(XButtonEvent *, int);
extern char *XResourceManagerString(XDisplay *);

/* System dpi from the desktop's Xft.dpi resource (a brief probe connection —
 * dpi is needed before any window exists, e.g. for GetDialogBaseUnits).
 * Physical DisplayWidthMM math is deliberately not used: monitors lie. */
int ween_x11_probe_dpi(void)
{
    XDisplay *dpy = XOpenDisplay(NULL);
    if (!dpy)
        return 0;
    int dpi = 0;
    const char *rm = XResourceManagerString(dpy);
    if (rm)
        dpi = ween_parse_xft_dpi(rm);
    XCloseDisplay(dpy);
    return dpi;
}

typedef struct x11_win_s {
    XDisplay *dpy;
    XWindow win;
    XGC *gc;
    XImage *img;
    XAtom wm_delete;
    int pos_x, pos_y;
    int w, h;         /* native (renderer) size */
    int win_w, win_h; /* what the window manager actually gave us, in device
                       * pixels: a tiling one hands back its tile whatever the
                       * window asked for */
    int zoom;    /* integer HiDPI magnification */
    ween_surface zbuf; /* zoomed present buffer (zoom > 1) */
    struct x11_win_s *next;
} x11_win;

/* One connection for the whole process, and every window on it. X delivers a
 * display's events through a single queue, so waiting on one window would mean
 * never hearing from the others; the pump takes them all and hands each event
 * back tagged with the window it names. */
static XDisplay *g_dpy;
static int g_windows;
static x11_win *g_list;

static x11_win *find_window(XWindow id)
{
    for (x11_win *w = g_list; w; w = w->next)
        if (w->win == id)
            return w;
    return NULL;
}

static void *x11_open(int x, int y, int w, int h, const char *title)
{
    if (!g_dpy)
        g_dpy = XOpenDisplay(NULL);
    XDisplay *dpy = g_dpy;
    if (!dpy)
        return NULL;
    x11_win *xw = calloc(1, sizeof(*xw));
    if (!xw)
        return NULL;
    int zoom = ween_zoom();
    int ww = w * zoom, wh = h * zoom;
    if (zoom > 1 && !ween_surface_init(&xw->zbuf, ww, wh)) {
        free(xw);
        return NULL;
    }
    int scr = XDefaultScreen(dpy);
    XWindow root = XDefaultRootWindow(dpy);

    /* where the app asked for, or centred on the primary screen when it did
     * not care — two windows that both centre would sit on top of each other */
    int px = x == CW_USEDEFAULT ? (XDisplayWidth(dpy, scr) - ww) / 2 : x * zoom;
    int py = y == CW_USEDEFAULT ? (XDisplayHeight(dpy, scr) - wh) / 2 : y * zoom;

    XWindow win = XCreateSimpleWindow(dpy, root, px, py, (unsigned)ww,
                                      (unsigned)wh, 0, 0, 0x00c0c0c0);
    XStoreName(dpy, win, title);

    /* Keep what is already on screen across a resize. The default gravity is
     * Forget: the server throws the contents away and tiles the whole window
     * with the background at every single step of a drag, so what you see is a
     * run of grey flashes with our frames in between — the flicker. North-west
     * pins the old pixels to the corner the window grows from, and only the
     * newly uncovered strip is filled, in the face grey it is about to be
     * painted anyway. The background is deliberately left as it is: on a
     * forwarded display a frame takes long enough to arrive that no background
     * at all would show the framebuffer's leftovers instead. */
    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.bit_gravity = X_NorthWestGravity;
    XChangeWindowAttributes(dpy, win, X_CWBitGravity, &attrs);

    /* fixed size: no WM resize handles */
    /* fixed size until the window says otherwise: a window whose style has
     * no sizing border must not be resizable by the window manager either */
    XSizeHints hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = X_PPosition | X_PSize | X_PMinSize | X_PMaxSize;
    hints.x = px;
    hints.y = py;
    hints.width = hints.min_width = hints.max_width = ww;
    hints.height = hints.min_height = hints.max_height = wh;
    XSetWMNormalHints(dpy, win, &hints);

    /* no WM decorations: ween32 windows draw their own Win2k caption */
    long motif[5] = { 2, 0, 0, 0, 0 }; /* MWM_HINTS_DECORATIONS, none */
    XAtom motif_atom = XInternAtom(dpy, "_MOTIF_WM_HINTS", 0);
    XChangeProperty(dpy, win, motif_atom, motif_atom, 32, 0, motif, 5);

    XSelectInput(dpy, win,
                 X_ExposureMask | X_ButtonPressMask | X_ButtonReleaseMask |
                     X_PointerMotionMask | X_KeyPressMask | X_StructureNotifyMask);
    XAtom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    XMapWindow(dpy, win);
    XMoveWindow(dpy, win, px, py); /* some WMs ignore the create position */

    XGC *gc = XCreateGC(dpy, win, 0, NULL);
    XImage *img = XCreateImage(dpy, XDefaultVisual(dpy, scr),
                               (unsigned)XDefaultDepth(dpy, scr), X_ZPixmap, 0,
                               NULL, (unsigned)ww, (unsigned)wh, 32, ww * 4);
    if (!gc || !img) {
        XDestroyWindow(dpy, win);
        free(xw);
        return NULL;
    }
    xw->dpy = dpy;
    xw->win = win;
    xw->gc = gc;
    xw->img = img;
    xw->wm_delete = wm_delete;
    xw->pos_x = px;
    xw->pos_y = py;
    xw->w = w;
    xw->h = h;
    xw->win_w = ww;
    xw->win_h = wh;
    xw->zoom = zoom;
    xw->next = g_list;
    g_list = xw;
    g_windows++;
    return xw;
}

static void x11_present(void *win, const ween_surface *s)
{
    x11_win *xw = win;
    const ween_surface *out = s;
    if (xw->zoom > 1) { /* crisp HiDPI: pixel-double the finished frame */
        if (xw->zbuf.w != s->w * xw->zoom || xw->zbuf.h != s->h * xw->zoom)
            ween_surface_resize(&xw->zbuf, s->w * xw->zoom, s->h * xw->zoom);
        ween_surface_zoom_into(&xw->zbuf, s, xw->zoom);
        out = &xw->zbuf;
    }
    /* the image must describe the buffer being handed over, whatever size the
     * window happens to be at this moment */
    if (xw->img->width != out->w || xw->img->height != out->h) {
        xw->img->width = out->w;
        xw->img->height = out->h;
        xw->img->bytes_per_line = out->w * 4;
    }
    xw->img->data = (char *)out->px;

    /* A window manager that will not respect a fixed size hands back a window
     * bigger than the one the app asked for. Stretching the app to fill it
     * would mean laying out a fixed dialog at a size it was never written for,
     * so centre it instead and letterbox what is left over. */
    int ox = xw->win_w > out->w ? (xw->win_w - out->w) / 2 : 0;
    int oy = xw->win_h > out->h ? (xw->win_h - out->h) / 2 : 0;
    if (ox > 0 || oy > 0) {
        XSetForeground(xw->dpy, xw->gc, 0x00404040);
        if (oy > 0) { /* above and below */
            XFillRectangle(xw->dpy, xw->win, xw->gc, 0, 0,
                           (unsigned)xw->win_w, (unsigned)oy);
            XFillRectangle(xw->dpy, xw->win, xw->gc, 0, oy + out->h,
                           (unsigned)xw->win_w,
                           (unsigned)(xw->win_h - oy - out->h));
        }
        if (ox > 0) { /* left and right */
            XFillRectangle(xw->dpy, xw->win, xw->gc, 0, oy, (unsigned)ox,
                           (unsigned)out->h);
            XFillRectangle(xw->dpy, xw->win, xw->gc, ox + out->w, oy,
                           (unsigned)(xw->win_w - ox - out->w),
                           (unsigned)out->h);
        }
    }
    XPutImage(xw->dpy, xw->win, xw->gc, xw->img, 0, 0, ox, oy,
              (unsigned)out->w, (unsigned)out->h);
    XFlush(xw->dpy);
}

static void x11_set_resizable(void *win, int resizable)
{
    x11_win *xw = win;
    XSizeHints hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = X_PMinSize | (resizable ? 0 : X_PMaxSize);
    hints.min_width = resizable ? 120 : xw->w * xw->zoom;
    hints.min_height = resizable ? 60 : xw->h * xw->zoom;
    hints.max_width = xw->w * xw->zoom;
    hints.max_height = xw->h * xw->zoom;
    XSetWMNormalHints(xw->dpy, xw->win, &hints);
    XFlush(xw->dpy);
}

static void x11_resize(void *win, int w, int h)
{
    x11_win *xw = win;
    /* This is the size the renderer works at from now on, whether or not the
     * window manager grants the request. */
    xw->w = w;
    xw->h = h;
    XResizeWindow(xw->dpy, xw->win, (unsigned)(w * xw->zoom),
                  (unsigned)(h * xw->zoom));
    XFlush(xw->dpy);
}

static void x11_move_by(void *win, int dx, int dy)
{
    x11_win *xw = win;
    xw->pos_x += dx;
    xw->pos_y += dy;
    XMoveWindow(xw->dpy, xw->win, xw->pos_x, xw->pos_y);
}

/* X keysym -> win32 virtual key (v1: the keys the dialog subset uses). */
static unsigned keysym_to_vk(unsigned long ks)
{
    switch (ks) {
    case 0xff1b:
        return VK_ESCAPE;
    case 0xff0d: /* Return */
    case 0xff8d: /* KP_Enter */
        return VK_RETURN;
    case 0xff09:
        return VK_TAB;
    case 0xff08:
        return VK_BACK;
    case 0xffff:
        return VK_DELETE;
    case 0xff50:
        return VK_HOME;
    case 0xff57:
        return VK_END;
    case 0xff51:
        return VK_LEFT;
    case 0xff52:
        return VK_UP;
    case 0xff53:
        return VK_RIGHT;
    case 0xff54:
        return VK_DOWN;
    case 0xffe9: /* Alt_L */
    case 0xffea: /* Alt_R */
        return 0x12; /* VK_MENU */
    case 0xffc7:
        return 0x79; /* VK_F10 */
    default:
        if (ks >= 'a' && ks <= 'z')
            return (unsigned)(ks - 32); /* VK codes are uppercase ASCII */
        if (ks < 128)
            return (unsigned)ks;
        return 0;
    }
}

/* The argument is only a hint about which connection to wait on: the event
 * that comes back names its own window, and every window shares the queue. */
/* Where the surface sits inside the window, when the two differ. */
static void surface_origin(const x11_win *xw, int *ox, int *oy)
{
    int sw = xw->w * xw->zoom, sh = xw->h * xw->zoom;
    *ox = xw->win_w > sw ? (xw->win_w - sw) / 2 : 0;
    *oy = xw->win_h > sh ? (xw->win_h - sh) / 2 : 0;
}

static ween_event x11_next_event(void *win, int timeout_ms)
{
    ween_event out;
    for (;;) {
        memset(&out, 0, sizeof(out));
        /* A timeout means a timer is waiting to run, so the wait cannot be the
         * indefinite one XNextEvent does. Xlib buffers events of its own, so
         * ask it first and only then sleep on the connection. */
        if (timeout_ms >= 0 && !XPending(g_dpy)) {
            int fd = XConnectionNumber(g_dpy);
            fd_set r;
            FD_ZERO(&r);
            FD_SET(fd, &r);
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            if (select(fd + 1, &r, NULL, NULL, &tv) <= 0) {
                out.kind = WEEN_EV_NONE; /* expired, or interrupted */
                return out;
            }
        }
        XEvent ev;
        XNextEvent(g_dpy, &ev);
        int sx = 0, sy = 0;
        x11_win *xw = find_window(ev.xany.window);
        if (!xw) /* a window closed while its events were still in flight */
            continue;
        out.win = xw;
        (void)win;
        XButtonEvent *b = &ev.xbutton;
        switch (ev.type) {
        case X_Expose: {
            /* One present covers the window, so the rest of the burst is work
             * we would only throw away. */
            XEvent drop;
            while (XCheckTypedWindowEvent(g_dpy, xw->win, X_Expose, &drop))
                ;
            out.kind = WEEN_EV_EXPOSE;
            return out;
        }
        case X_ConfigureNotify: {
            /* Keep the last geometry of a drag's burst and drop the exposes it
             * dragged along: repainting at each size the pointer swept through
             * is what makes a resize crawl and tear. */
            XEvent newer;
            while (XCheckTypedWindowEvent(g_dpy, xw->win, X_ConfigureNotify,
                                          &newer))
                ev = newer;
            while (XCheckTypedWindowEvent(g_dpy, xw->win, X_Expose, &newer))
                ;
            /* Always report it, even when the size has not changed: a window
             * manager that refuses a resize answers with the geometry it is
             * keeping, and that is how the surface learns to go back. */
            xw->win_w = ev.xconfigure.width;
            xw->win_h = ev.xconfigure.height;
            out.kind = WEEN_EV_RESIZE;
            out.x = ev.xconfigure.width / xw->zoom;
            out.y = ev.xconfigure.height / xw->zoom;
            return out;
        }
        case X_ButtonPress:
        case X_ButtonRelease:
            if (b->button == 4 || b->button == 5) { /* the wheel */
                if (ev.type != X_ButtonPress)
                    continue;
                out.kind = WEEN_EV_WHEEL;
                out.button = b->button == 4 ? 1 : -1;
                surface_origin(xw, &sx, &sy);
                out.x = (b->x - sx) / xw->zoom;
                out.y = (b->y - sy) / xw->zoom;
                return out;
            }
            out.kind = ev.type == X_ButtonPress ? WEEN_EV_MOUSE_DOWN
                                                : WEEN_EV_MOUSE_UP;
            surface_origin(xw, &sx, &sy);
            /* window px -> renderer px, past whatever letterbox is in front */
            out.x = (b->x - sx) / xw->zoom;
            out.y = (b->y - sy) / xw->zoom;
            out.x_root = b->x_root;
            out.y_root = b->y_root;
            out.button = (int)b->button;
            return out;
        case X_MotionNotify:
            out.kind = WEEN_EV_MOUSE_MOVE;
            surface_origin(xw, &sx, &sy);
            out.x = (b->x - sx) / xw->zoom;
            out.y = (b->y - sy) / xw->zoom;
            out.x_root = b->x_root;
            out.y_root = b->y_root;
            return out;
        case X_KeyPress: {
            /* index 1 of the keysym list is the shifted symbol, which is what
             * the character (but not the virtual key) depends on */
            int shift = (b->state & 1) != 0;
            unsigned long sym = XLookupKeysym(b, shift);
            unsigned vk = keysym_to_vk(XLookupKeysym(b, 0));
            if (!vk)
                continue;
            out.kind = WEEN_EV_KEY;
            out.vk = vk;
            out.shift = shift;
            out.alt = (b->state & (1 << 3)) != 0; /* Mod1: Alt */
            if (sym >= 0x20 && sym <= 0x7e) /* Latin-1 keysyms are their code */
                out.ch = (unsigned)sym;
            else if (vk == VK_RETURN)
                out.ch = '\r';
            else if (vk == VK_BACK)
                out.ch = '\b';
            else if (vk == VK_TAB)
                out.ch = '\t';
            return out;
        }
        case X_ClientMessage:
            if (ev.xclient.data[0] == (long)xw->wm_delete) {
                out.kind = WEEN_EV_CLOSE;
                return out;
            }
            continue;
        default:
            continue;
        }
    }
}

static void x11_close(void *win)
{
    x11_win *xw = win;
    for (x11_win **link = &g_list; *link; link = &(*link)->next) {
        if (*link == xw) {
            *link = xw->next;
            break;
        }
    }
    XFreeGC(xw->dpy, xw->gc);
    XDestroyWindow(xw->dpy, xw->win);
    xw->img->data = NULL; /* the pixels belong to the surface */
    free(xw->img);
    if (xw->zoom > 1)
        ween_surface_free(&xw->zbuf);
    free(xw);
    if (--g_windows == 0) { /* the last window takes the connection with it */
        XCloseDisplay(g_dpy);
        g_dpy = NULL;
    }
}

const ween_backend *ween_backend_x11(void)
{
    static const ween_backend b = { x11_open,       x11_present,
                                    x11_move_by,    x11_resize,
                                    x11_set_resizable, x11_next_event,
                                    x11_close };
    return &b;
}

#endif /* WEEN_BACKEND_X11 */
