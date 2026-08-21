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

#include <stdlib.h>
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

typedef union {
    int type;
    XButtonEvent xbutton;
    XClientMessageEvent xclient;
    long pad[24];
} XEvent;

typedef struct {
    int width, height;
    int xoffset;
    int format;
    char *data;
    unsigned char rest[200];
} XImage;

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
    X_ClientMessage = 33
};

#define X_ExposureMask (1L << 15)
#define X_ButtonPressMask (1L << 2)
#define X_ButtonReleaseMask (1L << 3)
#define X_PointerMotionMask (1L << 6)
#define X_KeyPressMask (1L << 0)
#define X_StructureNotifyMask (1L << 17)
#define X_ZPixmap 2
#define X_PPosition (1L << 2)
#define X_PSize (1L << 3)
#define X_PMinSize (1L << 4)
#define X_PMaxSize (1L << 5)

extern XDisplay *XOpenDisplay(const char *);
extern int XCloseDisplay(XDisplay *);
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
extern int XNextEvent(XDisplay *, XEvent *);
extern int XFlush(XDisplay *);
extern XAtom XInternAtom(XDisplay *, const char *, int);
extern int XSetWMProtocols(XDisplay *, XWindow, XAtom *, int);
extern void XSetWMNormalHints(XDisplay *, XWindow, XSizeHints *);
extern int XMoveWindow(XDisplay *, XWindow, int, int);
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

typedef struct {
    XDisplay *dpy;
    XWindow win;
    XGC *gc;
    XImage *img;
    XAtom wm_delete;
    int pos_x, pos_y;
    int w, h;    /* native (renderer) size */
    int zoom;    /* integer HiDPI magnification */
    ween_surface zbuf; /* zoomed present buffer (zoom > 1) */
} x11_win;

static void *x11_open(int w, int h, const char *title)
{
    XDisplay *dpy = XOpenDisplay(NULL);
    if (!dpy)
        return NULL;
    x11_win *xw = calloc(1, sizeof(*xw));
    if (!xw) {
        XCloseDisplay(dpy);
        return NULL;
    }
    int zoom = ween_zoom();
    int ww = w * zoom, wh = h * zoom;
    if (zoom > 1 && !ween_surface_init(&xw->zbuf, ww, wh)) {
        XCloseDisplay(dpy);
        free(xw);
        return NULL;
    }
    int scr = XDefaultScreen(dpy);
    XWindow root = XDefaultRootWindow(dpy);

    /* centred on the primary screen */
    int px = (XDisplayWidth(dpy, scr) - ww) / 2;
    int py = (XDisplayHeight(dpy, scr) - wh) / 2;

    XWindow win = XCreateSimpleWindow(dpy, root, px, py, (unsigned)ww,
                                      (unsigned)wh, 0, 0, 0x00c0c0c0);
    XStoreName(dpy, win, title);

    /* fixed size: no WM resize handles */
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
        XCloseDisplay(dpy);
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
    xw->zoom = zoom;
    return xw;
}

static void x11_present(void *win, const ween_surface *s)
{
    x11_win *xw = win;
    const ween_surface *out = s;
    if (xw->zoom > 1) { /* crisp HiDPI: pixel-double the finished frame */
        ween_surface_zoom_into(&xw->zbuf, s, xw->zoom);
        out = &xw->zbuf;
    }
    xw->img->data = (char *)out->px;
    XPutImage(xw->dpy, xw->win, xw->gc, xw->img, 0, 0, 0, 0, (unsigned)out->w,
              (unsigned)out->h);
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
    default:
        if (ks >= 'a' && ks <= 'z')
            return (unsigned)(ks - 32); /* VK codes are uppercase ASCII */
        if (ks < 128)
            return (unsigned)ks;
        return 0;
    }
}

static ween_event x11_next_event(void *win)
{
    x11_win *xw = win;
    ween_event out;
    for (;;) {
        memset(&out, 0, sizeof(out));
        XEvent ev;
        XNextEvent(xw->dpy, &ev);
        XButtonEvent *b = &ev.xbutton;
        switch (ev.type) {
        case X_Expose:
            out.kind = WEEN_EV_EXPOSE;
            return out;
        case X_ButtonPress:
        case X_ButtonRelease:
            if (b->button == 4 || b->button == 5) { /* the wheel */
                if (ev.type != X_ButtonPress)
                    continue;
                out.kind = WEEN_EV_WHEEL;
                out.button = b->button == 4 ? 1 : -1;
                out.x = b->x / xw->zoom;
                out.y = b->y / xw->zoom;
                return out;
            }
            out.kind = ev.type == X_ButtonPress ? WEEN_EV_MOUSE_DOWN
                                                : WEEN_EV_MOUSE_UP;
            out.x = b->x / xw->zoom; /* window px -> renderer px */
            out.y = b->y / xw->zoom;
            out.x_root = b->x_root;
            out.y_root = b->y_root;
            out.button = (int)b->button;
            return out;
        case X_MotionNotify:
            out.kind = WEEN_EV_MOUSE_MOVE;
            out.x = b->x / xw->zoom;
            out.y = b->y / xw->zoom;
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
    XCloseDisplay(xw->dpy);
    if (xw->zoom > 1)
        ween_surface_free(&xw->zbuf);
    free(xw);
}

const ween_backend *ween_backend_x11(void)
{
    static const ween_backend b = { x11_open, x11_present, x11_move_by,
                                    x11_next_event, x11_close };
    return &b;
}

#endif /* WEEN_BACKEND_X11 */
