/* The USER32-shaped windowing core: window classes, the window tree, the
 * message queue and loop, DefWindowProc's non-client chrome (caption bar,
 * close box, drag), and the built-in BUTTON and STATIC control classes.
 *
 * Model: a top-level window owns one backend (native) window and one software
 * surface; child windows are rectangles painted into the parent's surface and
 * receive their input via hit-testing, exactly the USER32 shape. Children do
 * not overlap; a window procedure can be replaced, which is how an
 * application gets between a control and its messages.
 */

#define _POSIX_C_SOURCE 200112L /* clock_gettime */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

/* Whether anything has been typed yet. A dialog created after it has takes
 * its keyboard cues from this, the way win32 takes them from the system's
 * own "the last thing the user did was type" flag. */
int ween_kbd_used = 0;

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
/* Windows destroyed but not yet freed; see the end of DestroyWindow. */
static struct ween_wnd *g_dead = NULL;
static HWND g_capture = NULL;
static HWND g_hot = NULL; /* what the pointer was last over, for hover */
static int g_dblclk = 0;  /* this press is the second of a pair */

/* The classic default; win32 reads it from Control Panel. */
#define WEEN_DOUBLE_CLICK_MS 500

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

/* Say that a class draws its own scroll bars, so that a window of it never
 * gets the non-client ones as well. Called once per class, at registration.*/
void ween_class_owns_scroll(LPCSTR name)
{
    ween_class *cls = (ween_class *)find_class(name);
    if (cls)
        cls->own_scroll = 1;
}

static void register_builtin(const char *name, WNDPROC proc, UINT style)
{
    if (find_class(name))
        return;
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = style;
    wc.lpfnWndProc = proc;
    wc.lpszClassName = name;
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);
}

static void ensure_builtins(void)
{
    /* BUTTON takes double clicks because win32's does — and treats one as
     * another press, so clicking fast never loses a click. STATIC does not
     * care, so it is left to receive ordinary presses. */
    register_builtin("BUTTON", button_proc, CS_DBLCLKS);
    register_builtin("STATIC", static_proc, 0);
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
    /* Registering a name twice hands back the class already under it, so the
     * ensure_*_class() idiom can be called from wherever it is needed without
     * piling up entries that lookup would never reach. */
    for (int i = 0; i < g_nclasses; i++)
        if (name_ieq(g_classes[i]->name, wc->lpszClassName))
            return (ATOM)(i + 1);
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
    c->style = wc->style;
    /* A stock class cursor is a shape, not a handle: LoadCursorA hands back
     * the shape number plus one, so there is nothing to keep alive. One the
     * application made is kept by pointer, and it must outlive the class. */
    c->cursor_img = ween_cursor_of(wc->hCursor);
    c->cursor = wc->hCursor && !c->cursor_img
                    ? (int)(INT_PTR)wc->hCursor - 1
                    : WEEN_CURSOR_ARROW;
    c->proc = wc->lpfnWndProc;
    c->background = wc->hbrBackground;
    c->icon = wc->hIcon;
    /* The menu every window of this class is given. A name here is a
     * resource name -- usually a number in a pointer -- and it is kept as it
     * came: what it means is LoadMenu's business, at the moment a window
     * needs one. */
    c->menu_name = (char *)wc->lpszMenuName;
    c->in_use = 1;
    g_classes[g_nclasses++] = c;
    return (ATOM)g_nclasses;
}

/* The later form of the same call. The two extra fields are its own size,
 * which says which one was passed, and a small icon; the small one is drawn
 * in the caption here, so it is taken when there is no other. */
ATOM RegisterClassExA(const WNDCLASSEXA *wc)
{
    WNDCLASSA plain;
    ATOM atom;
    if (!wc || wc->cbSize < sizeof(*wc))
        return 0;
    memset(&plain, 0, sizeof plain);
    plain.style = wc->style;
    plain.lpfnWndProc = wc->lpfnWndProc;
    plain.cbClsExtra = wc->cbClsExtra;
    plain.cbWndExtra = wc->cbWndExtra;
    plain.hInstance = wc->hInstance;
    plain.hIcon = wc->hIcon ? wc->hIcon : wc->hIconSm;
    plain.hCursor = wc->hCursor;
    plain.hbrBackground = wc->hbrBackground;
    plain.lpszMenuName = wc->lpszMenuName;
    plain.lpszClassName = wc->lpszClassName;
    atom = RegisterClassA(&plain);
    return atom;
}

/* ---- geometry ---------------------------------------------------------- */

/* WS_CAPTION is WS_BORDER | WS_DLGFRAME, so a captioned window has the border
 * bit set and draws it as part of its frame. Everything else that asks for
 * WS_BORDER — a window of its own, a control in a dialog — gets the flat line
 * instead; see ween_border_width. */
int ween_has_caption(const struct ween_wnd *w)
{
    return !w->parent && (w->style & WS_CAPTION) == WS_CAPTION;
}

/* A window of its own with WS_BORDER and no caption is bordered the way a
 * combo box's dropped list is: one pixel of COLOR_WINDOWFRAME, flat black,
 * rather than the raised edge a menu wears. Its own frame paints it; a
 * control's is painted with its other borders, in ween_paint_border. */
static int has_flat_border(const struct ween_wnd *w)
{
    return !w->parent && !ween_has_caption(w) && (w->style & WS_BORDER);
}

/* A window with a sizing border has a wider frame than a fixed one. */
int ween_frame_width(const struct ween_wnd *w)
{
    return ween_ncm((w->style & WS_THICKFRAME) ? WEEN_NC_SIZEFRAME
                                               : WEEN_NC_FRAME);
}

/* How tall this window's caption is. A tool window -- a palette floating over
 * the window it belongs to -- wears a shorter one with a smaller close box in
 * it and nothing else, which is what the machine's Fonts bar is. */
int ween_caption_height(const struct ween_wnd *w)
{
    return ween_ncm((w->ex_style & WS_EX_TOOLWINDOW) ? WEEN_NC_SMCAPTION
                                                     : WEEN_NC_CAPTION);
}

/* The strip the menu bar occupies, 0 when the window has no menu. A window
 * whose menu goes in a band of its own — a shell's is inside the rebar —
 * hangs none off the frame, so there is nothing to except here. */
int ween_menu_bar_height(const struct ween_wnd *w)
{
    return w->menu ? ween_ncm(WEEN_NC_MENU) : 0;
}

/* Client origin within the window's own rectangle. */
static void own_client_origin(const struct ween_wnd *w, int *ox, int *oy)
{
    if (ween_has_caption(w)) {
        *ox = ween_frame_width(w);
        *oy = ween_frame_width(w) + ween_caption_height(w) +
              ween_menu_bar_height(w);
    } else {
        *ox = ween_border_width(w);
        *oy = ween_border_width(w);
    }
}

/* The menu a window wears from now on. What was there is *not* destroyed:
 * the application put it there and may put it back, and win32 leaves it to
 * them. Destroying the window destroys whatever it is wearing then. */
BOOL SetMenu(HWND wnd, HMENU menu)
{
    if (!wnd)
        return FALSE;
    wnd->menu = menu;
    wnd->menu_hot = -1;
    ween_damage_all(wnd);
    return TRUE;
}

HMENU GetMenu(HWND wnd)
{
    return wnd ? wnd->menu : NULL;
}

BOOL GetWindowRect(HWND wnd, LPRECT rect)
{
    if (!wnd || !rect)
        return FALSE;
    /* A top-level window's rectangle is where the backend put it; a child's is
     * relative to the same origin, reached through its parents. The top's
     * corner comes from the same place ClientToScreen takes it, or the two
     * disagree and ScreenToClient of a rectangle read back here — which is
     * how a window works out where the pointer is inside itself — lands
     * somewhere else entirely. */
    int x = 0, y = 0;
    for (const struct ween_wnd *w = wnd; w; w = w->parent) {
        if (w->parent) {
            int cox, coy;
            own_client_origin(w->parent, &cox, &coy);
            x += w->x + cox;
            y += w->y + coy;
        } else {
            int wx, wy;
            ween_window_origin((struct ween_wnd *)w, &wx, &wy);
            x += wx;
            y += wy;
        }
    }
    rect->left = x;
    rect->top = y;
    rect->right = x + wnd->w;
    rect->bottom = y + wnd->h;
    return TRUE;
}

/* Client coordinates to the desktop and back.
 *
 * Anything positioned in desktop coordinates goes through here — a menu
 * tracked with TrackPopupMenu is the usual reason, and an application hosting
 * its own menu bar has no other way to say where the drop-down belongs. The
 * top-level's own origin comes from the window system rather than from where
 * it asked to be, because a window manager may have put it somewhere else.
 */
HWND GetParent(HWND wnd)
{
    return wnd ? wnd->parent : NULL;
}

BOOL ClientToScreen(HWND wnd, POINT *pt)
{
    struct ween_wnd *top;
    int ox, oy, wx = 0, wy = 0;
    if (!wnd || !pt)
        return FALSE;
    top = ween_top_level(wnd);
    /* where this window's client area is inside its top-level */
    for (const struct ween_wnd *w = wnd; w && w->parent; w = w->parent) {
        int cox, coy;
        own_client_origin(w->parent, &cox, &coy);
        wx += w->x + cox;
        wy += w->y + coy;
    }
    { int cox, coy; own_client_origin(wnd, &cox, &coy); wx += cox; wy += coy; }
    ween_window_origin(top, &ox, &oy);
    pt->x += wx + ox;
    pt->y += wy + oy;
    return TRUE;
}

/* Where the pointer is now, in desktop coordinates.
 *
 * The pump remembers where it last saw it, in the coordinates events arrive
 * in — its top-level window's — and this puts that back on the desktop the
 * same way ClientToScreen does, so that a window asking where the pointer is
 * inside itself gets an answer that agrees with its own rectangle. A window
 * that wants the pointer while handling WM_SETCURSOR has no other way to ask:
 * the message does not carry it. */
static struct ween_wnd *g_cursor_top;
static int g_cursor_x, g_cursor_y;

BOOL GetCursorPos(POINT *pt)
{
    int ox = 0, oy = 0;
    if (!pt)
        return FALSE;
    if (g_cursor_top)
        ween_window_origin(g_cursor_top, &ox, &oy);
    pt->x = g_cursor_x + ox;
    pt->y = g_cursor_y + oy;
    return TRUE;
}

BOOL ScreenToClient(HWND wnd, POINT *pt)
{
    POINT zero;
    zero.x = 0;
    zero.y = 0;
    if (!ClientToScreen(wnd, &zero))
        return FALSE;
    pt->x -= zero.x;
    pt->y -= zero.y;
    return TRUE;
}

/* The command line, as one string with the program name first: what win32
 * hands over, rebuilt here from what this machine keeps instead. An
 * argument with a space in it is quoted, as the caller will expect. */
/* The running program, as a handle. There is no loaded image here to point
 * at and every call taking an HINSTANCE ignores it, so what matters is only
 * that it is not NULL and is the same one every time: a program compares the
 * handle it was given in WinMain with this. A name asks for some other
 * module, and there are none. */
HINSTANCE GetModuleHandleA(LPCSTR name)
{
    static char self;
    return name ? NULL : (HINSTANCE)&self;
}

LPSTR GetCommandLineA(void)
{
    static char line[4096];
    static int built;
    if (built)
        return line;
    built = 1;
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (f) {
        char buf[4096];
        size_t n = fread(buf, 1, sizeof buf, f);
        size_t at = 0;
        fclose(f);
        for (size_t i = 0; i < n;) {
            size_t len = strlen(buf + i);
            int quote = strchr(buf + i, ' ') != NULL;
            if (at && at + 1 < sizeof line)
                line[at++] = ' ';
            if (quote && at + 1 < sizeof line)
                line[at++] = '"';
            if (at + len < sizeof line) {
                memcpy(line + at, buf + i, len);
                at += len;
            }
            if (quote && at + 1 < sizeof line)
                line[at++] = '"';
            i += len + 1;
        }
        line[at < sizeof line ? at : sizeof line - 1] = 0;
    }
    return line;
}

/* What the machine has, read where this machine keeps it. On Windows this
 * is kernel32's; here it is the page count, which is the same number. */
void GlobalMemoryStatus(LPMEMORYSTATUS status)
{
    if (!status)
        return;
    memset(status, 0, sizeof(*status));
    status->dwLength = sizeof(*status);
#ifdef _SC_PHYS_PAGES
    long pages = sysconf(_SC_PHYS_PAGES), avail = sysconf(_SC_AVPHYS_PAGES);
    long page = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0) {
        /* The byte counts are SIZE_T, as win32 declares them -- as wide as a
         * pointer, not as wide as a DWORD. They were DWORD here, which both
         * put every field after the second one where win32 does not have it
         * and clamped a machine like this one to four gigabytes; the clamp
         * was written to match what a 32-bit Windows answers, and on a 64-bit
         * build it was inventing a wrong number rather than reporting a
         * ceiling. GlobalMemoryStatusEx exists for the 32-bit case, not for
         * this one. */
        status->dwTotalPhys = (SIZE_T)pages * (SIZE_T)page;
        status->dwAvailPhys = (SIZE_T)(avail > 0 ? avail : 0) * (SIZE_T)page;
        status->dwMemoryLoad =
            (DWORD)(100 - (avail > 0 ? avail * 100 / pages : 0));
    }
#endif
}

int GetSystemMetrics(int index)
{
    switch (index) {
    case SM_CYCAPTION:
        return ween_ncm(WEEN_NC_CAPTION);
    case SM_CYMENU:
        return ween_ncm(WEEN_NC_MENU);
    case SM_CXBORDER:
    case SM_CYBORDER:
        return 1;
    case SM_CXVSCROLL:
    case SM_CYHSCROLL:
        return ween_scroll_metric();
    case SM_CXMENUCHECK:
    case SM_CYMENUCHECK:
        return ween_ncm(WEEN_NC_MENUCHECK);
    case SM_CXSCREEN:
    case SM_CYSCREEN: {
        /* the desktop the app is actually on, or a plausible classic one
         * where there is no desktop to ask */
        int w = 1024, h = 768;
        if (ween_active_backend && ween_active_backend->screen_size)
            ween_active_backend->screen_size(&w, &h);
        return index == SM_CXSCREEN ? w : h;
    }
    default:
        return 0;
    }
}

BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD ex_style)
{
    (void)ex_style;
    if (!rect)
        return FALSE;
    if ((style & WS_CAPTION) == WS_CAPTION && !(style & WS_CHILD)) {
        int frame = ween_ncm((style & WS_THICKFRAME) ? WEEN_NC_SIZEFRAME
                                                     : WEEN_NC_FRAME);
        rect->left -= frame;
        rect->right += frame;
        rect->top -= frame +
                     ween_ncm((ex_style & WS_EX_TOOLWINDOW) ? WEEN_NC_SMCAPTION
                                                            : WEEN_NC_CAPTION) +
                     (menu ? ween_ncm(WEEN_NC_MENU) : 0);
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
    int trail = ween_has_caption(wnd) ? ween_frame_width(wnd)
                                      : ween_border_width(wnd);
    rect->right = wnd->w - ox - trail;
    rect->bottom = wnd->h - oy - trail;
    /* A window's own scroll bars live outside the client area, which is why
     * asking for one makes the client rectangle smaller. */
    if (ween_wnd_sb_shown(wnd, 1))
        rect->right -= ween_scroll_metric();
    if (ween_wnd_sb_shown(wnd, 0))
        rect->bottom -= ween_scroll_metric();
    if (rect->right < 0)
        rect->right = 0;
    if (rect->bottom < 0)
        rect->bottom = 0;
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

/* The caption buttons, right to left: close at the edge, then a two-pixel gap,
 * then maximize and minimize side by side — the arrangement in a Windows 2000
 * caption, measured off a screenshot of one. `which` is 0 for close, 1 for
 * maximize, 2 for minimize. */
static RECT nc_button_rect(const struct ween_wnd *w, int which)
{
    RECT r;
    int tool = (w->ex_style & WS_EX_TOOLWINDOW) != 0;
    int frame = ween_frame_width(w);
    int cap = ween_caption_height(w);
    int bw = ween_ncm(tool ? WEEN_NC_SMBTN_W : WEEN_NC_BTN_W);
    int bh = ween_ncm(tool ? WEEN_NC_SMBTN_H : WEEN_NC_BTN_H);
    r.left = w->w - frame - ween_ncm(2) - bw;
    if (which >= 1) /* the gap sits between close and the other two */
        r.left -= ween_ncm(2) + bw * which;
    r.top = frame + (cap - bh) / 2;
    r.right = r.left + bw;
    r.bottom = r.top + bh;
    return r;
}

static RECT nc_close_rect(const struct ween_wnd *w)
{
    return nc_button_rect(w, 0);
}

/* Which caption buttons a window has, by its style. */
static int nc_has_min(const struct ween_wnd *w)
{
    /* a tool window's caption holds the close box and nothing else */
    return !(w->ex_style & WS_EX_TOOLWINDOW) && (w->style & WS_MINIMIZEBOX);
}

static int nc_has_max(const struct ween_wnd *w)
{
    return !(w->ex_style & WS_EX_TOOLWINDOW) && (w->style & WS_MAXIMIZEBOX);
}

/* A question mark before the close box, which a window asks for with
 * WS_EX_CONTEXTHELP. It only appears on one that has neither a minimise nor a
 * maximise box: the strip is not shared. */
static int nc_has_help(const struct ween_wnd *w)
{
    return (w->ex_style & WS_EX_CONTEXTHELP) && !nc_has_min(w) &&
           !nc_has_max(w);
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
    wnd->icon = cls->icon;
    wnd->proc = cls->proc;
    wnd->style = style;
    wnd->ex_style = ex_style;
    wnd->scroll_max = 100;
    /* Nothing has been said about the pointer yet, which is not the same as
     * having said "an arrow": a window that never tells the server what it
     * wants inherits the root's cursor, and on a bare X server that is the
     * X shape rather than an arrow. */
    wnd->cursor_shown = -1;
    /* CW_USEDEFAULT: the system picks. For a size it is valid only on an
     * overlapped window -- win32 sets a popup or a child asking for one to
     * nothing, and so does this -- and where it is valid Windows cascades a
     * window across the desktop at about three quarters of it. Nothing here
     * cascades; the size is those three quarters, which is what a program
     * that says "you decide" is asking for. Before this, a window created
     * that way took the number itself as its width and failed to be made at
     * all. */
    if (w == CW_USEDEFAULT || h == CW_USEDEFAULT) {
        int overlapped = !(style & (WS_CHILD | WS_POPUP));
        if (w == CW_USEDEFAULT)
            w = overlapped ? GetSystemMetrics(SM_CXSCREEN) * 3 / 4 : 0;
        if (h == CW_USEDEFAULT)
            h = overlapped ? GetSystemMetrics(SM_CYSCREEN) * 3 / 4 : 0;
    }
    wnd->x = x == CW_USEDEFAULT ? 0 : x;
    wnd->y = y == CW_USEDEFAULT ? 0 : y;
    wnd->w = w;
    wnd->h = h;
    /* The same parameter means two things, as it does in win32: a child's
     * control id, and a top-level window's menu. */
    if (style & WS_CHILD)
        wnd->id = (UINT_PTR)menu;
    else
        wnd->menu = (HMENU)menu;
    /* A window made from a class that names a menu is given that menu when
     * it asks for none of its own, which is how a program that keeps its bar
     * in its resource script comes to have one without a line about it. */
    if (!(style & WS_CHILD) && !wnd->menu && cls && cls->menu_name)
        wnd->menu = LoadMenuA(inst, cls->menu_name);
    wnd->menu_hot = -1;
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
        /* A top-level with neither caption nor border is a menu or a
         * tooltip: the window system should place it exactly and leave it
         * alone. Anything with a caption is a window a person manages. */
        unsigned wflags = (style & WS_CAPTION) == WS_CAPTION
                              ? 0u
                              : WEEN_WIN_UNMANAGED;
        wnd->backend_win =
            ween_active_backend->open(x, y, w, h, wnd->text, wflags);
        /* A window is opened off the screen and appears when it is shown.
         * Without WS_VISIBLE it stays back — which is how a box that is made
         * once and put up later avoids standing in the corner of the screen
         * from the moment the program starts. */
        if (wnd->backend_win && ween_active_backend->show)
            ween_active_backend->show(wnd->backend_win, (style & WS_VISIBLE) != 0);
        if (wnd->backend_win && (style & WS_THICKFRAME) &&
            ween_active_backend->set_resizable)
            ween_active_backend->set_resizable(wnd->backend_win, 1);
        /* Who put it up, and whether it is a dialog. A window system that is
         * told neither has no reason to treat a modal box differently from an
         * application's main window -- and a window manager that arranges
         * windows for you will arrange it. */
        wnd->owner = parent;
        if (wnd->backend_win && ween_active_backend->set_owner) {
            int is_dialog = (style & DS_MODALFRAME) != 0 ||
                            (cls->name && !strcmp(cls->name, "#32770"));
            ween_active_backend->set_owner(
                wnd->backend_win,
                parent && parent->backend_win ? parent->backend_win : NULL,
                is_dialog);
        }
        /* A window that did not say where it goes is put somewhere by the
         * window system, and where that is has to be read back rather than
         * assumed: taking it for the origin is what left a maximised window
         * sized to the screen but still sitting where it was. */
        if (wnd->backend_win && (x == CW_USEDEFAULT || y == CW_USEDEFAULT) &&
            ween_active_backend->origin)
            ween_active_backend->origin(wnd->backend_win, &wnd->x, &wnd->y);
        if (!wnd->backend_win) {
            ween_surface_free(&wnd->surface);
            free(wnd);
            return NULL;
        }
        wnd->next_top = g_tops;
        g_tops = wnd;
        /* A menu does not take the keyboard: the window under it keeps its
         * focus, so its caret is still there when the menu goes away. And a
         * window made without being shown takes nothing either — what is
         * active is what is on the screen, so a palette built ahead of time
         * and shown later leaves the caret where it was. */
        if (!(ex_style & WS_EX_NOACTIVATE) && (style & WS_VISIBLE)) {
            g_active = wnd;
            g_focus = wnd;
        }
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

    /* win32 sends WM_SIZE as part of creating a window, and an app that lays
     * its children out there — which is the usual way — sees nothing at all
     * without it. */
    {
        RECT cr;
        GetClientRect(wnd, &cr);
        SendMessageA(wnd, WM_SIZE, SIZE_RESTORED,
                     MAKELPARAM((WORD)cr.right, (WORD)cr.bottom));
    }

    ween_damage_all(wnd);
    if (wnd->parent)
        ween_damage_all(wnd);
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

    /* The parent is told its child is going, while the child is still there
     * to be named. A control that holds one of its children by handle — a
     * rebar's band, and it will not be the last — has nothing else to tell it
     * that the window it is pointing at has been freed, and lays the freed
     * one out the next time it lays anything out. win32 sends the same
     * message for a child being created and for one being pressed; only this
     * half is here, and the ROADMAP says so. */
    if (wnd->parent && !(wnd->ex_style & WS_EX_NOPARENTNOTIFY))
        SendMessageA(wnd->parent, WM_PARENTNOTIFY,
                     MAKEWPARAM(WM_DESTROY, (WORD)wnd->id), (LPARAM)wnd);

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
        if (g_active == wnd) {
            /* What takes the keyboard when the active window goes: the first
             * one that can have it. A window put up not to be activated — a
             * menu, the box under an address bar — never becomes active, and
             * neither does one that is hidden or disabled. Handing it to
             * whichever window happens to be newest is how a hidden popup
             * came to swallow every press meant for the window behind it. */
            struct ween_wnd *next = NULL;
            for (struct ween_wnd *t = g_tops; t; t = t->next_top)
                if (t->visible && !(t->style & WS_DISABLED) &&
                    !(t->ex_style & WS_EX_NOACTIVATE)) {
                    next = t;
                    break;
                }
            g_active = next;
            /* And it is repainted, because a caption says which window has
             * the keyboard: the one that takes it back from a box that has
             * just closed was still wearing the grey caption otherwise, with
             * the state right and the pixels a lie. Only the new one is
             * damaged -- the old one is this window, half torn down. */
            if (next)
                ween_damage_all(next);
        }
    }
    if (g_focus == wnd)
        g_focus = g_active;
    if (g_capture == wnd)
        g_capture = NULL;
    if (g_hot == wnd)
        g_hot = NULL;
    ween_controls_free(wnd);
    ween_kill_timers_of(wnd);
    /* The menu a window wears goes with it, submenus and all, which is
     * win32's rule and the only way a menu a class loaded can ever be freed:
     * the application never sees that handle. SetMenu is the other half of
     * the rule and does *not* free the one it replaces -- that one the
     * application put there and still owns. */
    if (wnd->menu)
        DestroyMenu(wnd->menu);
    /* Not freed here. A window destroyed from inside its own message
     * procedure is the ordinary way a modeless dialog closes itself --
     * `case IDCANCEL: DestroyWindow(hDlg)` -- and whoever called that
     * procedure is still inside it and about to read the window again: the
     * dialog manager wants the result the procedure left, the message loop
     * asks whether a window has gone before dispatching to it. On Windows a
     * window is a handle and the memory outlives the moment; here it is a
     * pointer, and freeing it under the caller is a use-after-free the
     * sanitizer finds and a crash it does not.
     *
     * So the window is taken out of every list, marked, and put aside. What
     * is left is freed by the message loop, which is the one place nothing
     * is inside a window procedure. Until then it is still readable and
     * still says it is destroyed, which is what every one of those callers
     * is asking. */
    wnd->next_top = g_dead;
    g_dead = wnd;
    return TRUE;
}

/* Free what DestroyWindow set aside. Called from the message loop only: a
 * window procedure may be somewhere up the stack anywhere else. */
static void reap_windows(void)
{
    while (g_dead) {
        struct ween_wnd *w = g_dead;
        g_dead = w->next_top;
        free(w->text);
        free(w);
    }
}

BOOL ShowWindow(HWND wnd, int cmd)
{
    if (!wnd)
        return FALSE;
    BOOL was = wnd->visible;
    wnd->visible = cmd != SW_HIDE;
    /* A window of its own is put on the screen or taken off it; a child is
     * only a rectangle in its parent's, and repainting is the whole of it. */
    if (!wnd->parent && wnd->backend_win && ween_active_backend) {
        /* Told before it goes up, not after: a window manager decides where
         * the keyboard goes at the moment it puts a window on the screen. */
        if (wnd->visible && (cmd == SW_SHOWNA || cmd == SW_SHOWNOACTIVATE) &&
            ween_active_backend->no_activate)
            ween_active_backend->no_activate(wnd->backend_win);
        if (ween_active_backend->show)
            ween_active_backend->show(wnd->backend_win, wnd->visible);
    }
    /* Showing a window of its own is what makes it the active one — that is
     * where a dialog gets the keyboard from. The two commands that say "and
     * do not activate it" are how a palette is floated over the window being
     * worked in without taking the caret out of it. */
    if (!wnd->parent && wnd->visible && cmd != SW_SHOWNA &&
        cmd != SW_SHOWNOACTIVATE && !(wnd->ex_style & WS_EX_NOACTIVATE)) {
        ween_set_active(wnd);
        /* The keyboard goes to it unless it is already inside it: a dialog
         * that put the caret in one of its fields while it was still hidden
         * keeps it there when it comes up.
         *
         * Through SetFocus rather than by assignment, so that the window
         * hears WM_SETFOCUS -- which is where a program puts the keyboard
         * where it really wants it. Notepad's whole answer to that message is
         * `SetFocus(hwndEdit)`, and until this it was never sent: the program
         * came up with the caret nowhere and typing went into the void until
         * something was clicked. Every program written to win32 assumes the
         * window it shows has the keyboard, because on Windows it does. */
        if (!g_focus || ween_top_level(g_focus) != wnd)
            SetFocus(wnd);
    }
    ween_damage_all(wnd);
    return was;
}

BOOL SetWindowTextA(HWND wnd, LPCSTR text)
{
    if (!wnd)
        return FALSE;
    if (!ween_wnd_set_text(wnd, text))
        return FALSE;
    SendMessageA(wnd, WM_SETTEXT, 0, (LPARAM)text);
    ween_damage_all(wnd);
    return TRUE;
}

/* A window's text is whatever it answers WM_GETTEXT with, not what is in its
 * own field: a combo box's is its edit's, and asking the window rather than
 * reading the structure is what lets it say so. */
int GetWindowTextA(HWND wnd, LPSTR out, int max)
{
    if (!wnd || !out || max <= 0)
        return 0;
    out[0] = 0;
    return (int)SendMessageA(wnd, WM_GETTEXT, (WPARAM)max, (LPARAM)out);
}

BOOL MoveWindow(HWND wnd, int x, int y, int w, int h, BOOL repaint)
{
    if (!wnd)
        return FALSE;
    /* A window of its own has to be moved on the screen, not only in the
     * bookkeeping: the window system is the thing that knows where it is,
     * and a popup put up once and moved afterwards — a box under a field
     * that follows it — would otherwise stay where it was made. */
    if (!wnd->parent && (x != wnd->x || y != wnd->y) && wnd->backend_win &&
        ween_active_backend && ween_active_backend->move_by)
        ween_active_backend->move_by(wnd->backend_win, x - wnd->x, y - wnd->y);
    wnd->x = x;
    wnd->y = y;
    if (!wnd->parent && (w != wnd->w || h != wnd->h)) {
        resize_top(wnd, w, h);
        if (ween_active_backend && ween_active_backend->resize)
            ween_active_backend->resize(wnd->backend_win, wnd->w, wnd->h);
    } else {
        int moved = w != wnd->w || h != wnd->h;
        wnd->w = w;
        wnd->h = h;
        /* Moving a child is a size change like any other, and win32 says so.
         * A control that lays its own contents out — which is the usual way
         * to write one — otherwise never hears that it grew. */
        if (moved) {
            RECT cr;
            GetClientRect(wnd, &cr);
            SendMessageA(wnd, WM_SIZE, SIZE_RESTORED,
                         MAKELPARAM((WORD)cr.right, (WORD)cr.bottom));
        }
    }
    if (repaint)
        ween_damage_all(wnd);
    return TRUE;
}

/* MoveWindow with the parts a caller does not care about left out. The one
 * that matters here is SWP_NOMOVE: a window that resizes itself must not
 * have to ask where it is first, because on a display with a window manager
 * the answer is where the manager put it, and moving it back there again
 * walks it across the screen by the width of its own frame. */
BOOL SetWindowPos(HWND wnd, HWND after, int x, int y, int cx, int cy,
                  UINT flags)
{
    (void)after; /* one window is in front of another here by its age */
    if (!wnd)
        return FALSE;
    if (flags & SWP_NOMOVE) {
        x = wnd->x;
        y = wnd->y;
    }
    if (flags & SWP_NOSIZE) {
        cx = wnd->w;
        cy = wnd->h;
    }
    MoveWindow(wnd, x, y, cx, cy, (flags & SWP_NOREDRAW) ? FALSE : TRUE);
    if (flags & SWP_SHOWWINDOW)
        ShowWindow(wnd, SW_SHOW);
    if (flags & SWP_HIDEWINDOW)
        ShowWindow(wnd, SW_HIDE);
    return TRUE;
}

/* A control's text by its id, and setting it: what a dialog reads back from
 * what was typed, and writes into a field it fills in. */
UINT GetDlgItemTextA(HWND dlg, int id, LPSTR out, int max)
{
    HWND c = GetDlgItem(dlg, id);
    if (!out || max <= 0)
        return 0;
    out[0] = 0;
    if (!c)
        return 0;
    return (UINT)GetWindowTextA(c, out, max);
}

BOOL SetDlgItemTextA(HWND dlg, int id, LPCSTR text)
{
    HWND c = GetDlgItem(dlg, id);
    return c ? SetWindowTextA(c, text) : FALSE;
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
/* The tab stops under a window, in the order Tab visits them. A container
 * that is not itself a stop but marks itself WS_EX_CONTROLPARENT is walked
 * into rather than over: that is how a control inside a rebar band, or
 * inside any other grouping window, gets into the ring. Without it Tab only
 * ever sees a window's own children, and a shell's address bar — which sits
 * two windows down — is unreachable. */
static void tab_collect(struct ween_wnd *parent, HWND *out, int *n, int max)
{
    for (struct ween_wnd *c = parent->first_child; c; c = c->next_sibling) {
        if (!c->visible || (c->style & WS_DISABLED)) /* as win32 skips them */
            continue;
        if (c->style & WS_TABSTOP) {
            if (*n < max)
                out[(*n)++] = c;
        } else if (c->ex_style & WS_EX_CONTROLPARENT) {
            tab_collect(c, out, n, max);
        }
    }
}

HWND ween_tab_next(HWND dlg, HWND cur, int forward)
{
    HWND stop[64];
    int n = 0, at = -1;
    tab_collect(dlg, stop, &n, (int)(sizeof(stop) / sizeof(*stop)));
    if (!n)
        return NULL;
    for (int i = 0; i < n; i++)
        if (stop[i] == cur)
            at = i;
    if (at < 0) /* the focus is nowhere in the ring: start at either end */
        return forward ? stop[0] : stop[n - 1];
    return stop[(at + (forward ? 1 : n - 1)) % n];
}

/* The character a label marks with '&', lowercased, or 0 if it marks none. */
static char mnemonic_of(const struct ween_wnd *w)
{
    for (const char *p = w->text; p && *p; p++) {
        if (*p != '&')
            continue;
        if (p[1] == '&') { /* a literal ampersand, not a marker */
            p++;
            continue;
        }
        if (p[1] >= 'A' && p[1] <= 'Z')
            return (char)(p[1] + 32);
        return p[1];
    }
    return 0;
}

/* Alt+key: the control whose label marks that letter takes the key. A button
 * is clicked; anything else just takes the focus, which is what win32 does
 * with a static label's mnemonic pointing at the control after it. */
HWND ween_mnemonic_target(HWND parent, unsigned ch)
{
    if (ch >= 'A' && ch <= 'Z')
        ch += 32;
    for (struct ween_wnd *c = parent->first_child; c; c = c->next_sibling) {
        if (!c->visible || (c->style & WS_DISABLED))
            continue;
        if (mnemonic_of(c) == (char)ch)
            return c;
    }
    return NULL;
}

static UINT button_type(const struct ween_wnd *w); /* defined with BUTTON */

/* A group of option buttons is one tab stop, and it is the *checked* button
 * that holds it: Tab into the group lands on what is set, not on whatever
 * happens to be first. Win32 does this by moving WS_TABSTOP as the selection
 * moves, and so does this — which is why the machine's Folder Options shows
 * its focus rectangle around "Use Windows classic desktop" rather than around
 * the button above it. */
static void radio_take_tabstop(struct ween_wnd *w)
{
    struct ween_wnd *c, *start;
    if (!w || !w->parent)
        return;
    start = w->parent->first_child;
    for (c = w->parent->first_child; c; c = c->next_sibling) {
        if (c->style & WS_GROUP)
            start = c;
        if (c == w)
            break;
    }
    for (c = start; c; c = c->next_sibling) {
        if (c != start && (c->style & WS_GROUP))
            break;
        if (button_type(c) != BS_AUTORADIOBUTTON &&
            button_type(c) != BS_RADIOBUTTON)
            continue;
        if (c == w)
            c->style |= WS_TABSTOP;
        else
            c->style &= ~(DWORD)WS_TABSTOP;
    }
}

/* Arrow keys inside a group of auto-radio buttons move the selection, as they
 * do on Windows: the group is the run between WS_GROUP markers. */
HWND ween_radio_step(HWND cur, int forward)
{
    if (!cur || !cur->parent || button_type(cur) != BS_AUTORADIOBUTTON)
        return NULL;
    struct ween_wnd *start = cur->parent->first_child, *c;
    for (c = cur->parent->first_child; c; c = c->next_sibling) {
        if (c->style & WS_GROUP)
            start = c;
        if (c == cur)
            break;
    }
    HWND list[64];
    int n = 0, idx = -1;
    for (c = start; c && n < 64; c = c->next_sibling) {
        if (c != start && (c->style & WS_GROUP))
            break;
        if (button_type(c) != BS_AUTORADIOBUTTON || !c->visible ||
            (c->style & WS_DISABLED))
            continue;
        if (c == cur)
            idx = n;
        list[n++] = c;
    }
    if (n < 2 || idx < 0)
        return NULL;
    return list[(idx + (forward ? 1 : n - 1)) % n];
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
        ween_damage_all(wnd);
    }
    return was_disabled;
}

BOOL IsWindowEnabled(HWND wnd)
{
    return wnd && !(wnd->style & WS_DISABLED);
}

BOOL IsWindowVisible(HWND wnd)
{
    /* and every window above it, since one inside a hidden window is not
     * shown either */
    for (const struct ween_wnd *w = wnd; w; w = w->parent)
        if (!w->visible)
            return FALSE;
    return wnd != NULL;
}

/* How long the text is, which a program asks before allocating room for it.
 * Through WM_GETTEXTLENGTH, so a control that keeps its text somewhere of its
 * own answers for itself. */
int GetWindowTextLengthA(HWND wnd)
{
    return wnd ? (int)SendMessageA(wnd, WM_GETTEXTLENGTH, 0, 0) : 0;
}

/* A message named rather than numbered. The same name always gives the same
 * number, and a name nobody has asked for before takes the next one; win32
 * hands these out from 0xC000 upwards, which is above every WM_ there is. */
UINT RegisterWindowMessageA(LPCSTR name)
{
    static char *names[256];
    static int count;
    if (!name || !*name)
        return 0;
    for (int i = 0; i < count; i++)
        if (!strcmp(names[i], name))
            return (UINT)(0xC000 + i);
    if (count == (int)(sizeof names / sizeof names[0]))
        return 0;
    names[count] = malloc(strlen(name) + 1);
    if (!names[count])
        return 0;
    strcpy(names[count], name);
    return (UINT)(0xC000 + count++);
}

/* Minimising is the window manager's, and nothing here asks for it, so a
 * window is never an icon. Saying so is what a program checking before it
 * saves its position needs; being told it is minimised when it is not would
 * lose the position instead. */
BOOL IsIconic(HWND wnd)
{
    (void)wnd;
    return FALSE;
}

/* Maximised, though, the library does know: a window filling the screen is
 * drawn with the restore button and SC_MAXIMIZE toggles it. */
BOOL IsZoomed(HWND wnd)
{
    return wnd && wnd->maximized ? TRUE : FALSE;
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

/* win32's own string calls. A shell orders names case-insensitively, and this
 * is what it uses to do it. */
int lstrcmpA(LPCSTR a, LPCSTR b)
{
    return strcmp(a ? a : "", b ? b : "");
}

int lstrcmpiA(LPCSTR a, LPCSTR b)
{
    const unsigned char *p = (const unsigned char *)(a ? a : "");
    const unsigned char *q = (const unsigned char *)(b ? b : "");
    for (; *p && *q; p++, q++) {
        int ca = *p >= 'A' && *p <= 'Z' ? *p + 32 : *p;
        int cb = *q >= 'A' && *q <= 'Z' ? *q + 32 : *q;
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    return *p ? 1 : (*q ? -1 : 0);
}

int lstrlenA(LPCSTR s)
{
    return s ? (int)strlen(s) : 0;
}

/* Which window has the keyboard. A control drawing itself needs this: a
 * selection is one colour with the focus and another without it. */
HWND GetFocus(void)
{
    return g_focus;
}

/* A window's style after the fact. Changing it tells the window, which is how
 * a list view comes to lay its folder out a different way. */
/* A window's procedure, and everything else GWL_ names, as a pointer — which
 * is what a procedure needs on a build where a LONG is half a pointer. */
LONG_PTR GetWindowLongPtrA(HWND wnd, int index)
{
    if (!wnd)
        return 0;
    if (index == GWL_WNDPROC)
        return (LONG_PTR)wnd->proc;
    if (index == GWL_USERDATA) /* whole, not squeezed through a LONG */
        return wnd->userdata;
    if (index == DWLP_MSGRESULT && wnd->is_dialog)
        return (LONG_PTR)wnd->dlg_msgresult;
    if (index == DWLP_USER && wnd->is_dialog)
        return wnd->dlg_user;
    return (LONG_PTR)GetWindowLongA(wnd, index);
}

LONG_PTR SetWindowLongPtrA(HWND wnd, int index, LONG_PTR value)
{
    if (!wnd)
        return 0;
    if (index == GWL_WNDPROC) {
        WNDPROC was = wnd->proc;
        wnd->proc = (WNDPROC)value;
        return (LONG_PTR)was;
    }
    if (index == GWL_USERDATA) {
        LONG_PTR was = wnd->userdata;
        wnd->userdata = value;
        return was;
    }
    if (index == DWLP_USER && wnd->is_dialog) {
        LONG_PTR was = wnd->dlg_user;
        wnd->dlg_user = value;
        return was;
    }
    if (index == DWLP_MSGRESULT && wnd->is_dialog) {
        LONG_PTR was = (LONG_PTR)wnd->dlg_msgresult;
        wnd->dlg_msgresult = (LRESULT)value;
        wnd->dlg_msgresult_set = 1;
        return was;
    }
    return (LONG_PTR)SetWindowLongA(wnd, index, (LONG)value);
}

/* Hand a message to a procedure that was subclassed away. */
LRESULT CallWindowProcA(WNDPROC proc, HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return proc ? proc(wnd, msg, wp, lp) : DefWindowProcA(wnd, msg, wp, lp);
}

LONG GetWindowLongA(HWND wnd, int index)
{
    if (!wnd)
        return 0;
    switch (index) {
    case GWL_STYLE:
        return (LONG)wnd->style;
    case GWL_EXSTYLE:
        return (LONG)wnd->ex_style;
    case GWL_ID:
        return (LONG)wnd->id;
    case GWL_USERDATA:
        return (LONG)wnd->userdata;
    default:
        return 0;
    }
}

LONG SetWindowLongA(HWND wnd, int index, LONG value)
{
    LONG was;
    if (!wnd)
        return 0;
    was = GetWindowLongA(wnd, index);
    switch (index) {
    case GWL_STYLE:
        wnd->style = (DWORD)value;
        wnd->visible = (wnd->style & WS_VISIBLE) != 0;
        SendMessageA(wnd, WM_STYLECHANGED, (WPARAM)GWL_STYLE, 0);
        InvalidateRect(wnd, NULL, TRUE);
        break;
    case GWL_EXSTYLE:
        wnd->ex_style = (DWORD)value;
        InvalidateRect(wnd, NULL, TRUE);
        break;
    case GWL_ID:
        wnd->id = (UINT)value;
        break;
    case GWL_USERDATA:
        wnd->userdata = (LONG_PTR)value;
        break;
    default:
        break;
    }
    return was;
}

/* Make a window the active one, and repaint the captions that say so: the
 * one losing it goes grey and the one taking it goes blue, and neither
 * happens unless both are told to draw themselves again. */
void ween_set_active(struct ween_wnd *w)
{
    struct ween_wnd *was = g_active;
    if (was == w)
        return;
    g_active = w;
    if (was)
        ween_damage_all(was);
    if (w)
        ween_damage_all(w);
}

/* The window the keyboard belongs to at the top level: what a menu, a dialog
 * or a box put up beside something is measured against. */
HWND GetActiveWindow(void)
{
    return g_active;
}

HWND SetFocus(HWND wnd)
{
    HWND prev = g_focus;
    if (prev == wnd)
        return prev;
    /* A window that was made not to be activated does not take the keyboard
     * either: that is the whole of what the style is for. Menus and the boxes
     * that drop under a field are put up this way, so that a press in one is
     * a press on the thing it belongs to and the caret stays where it was. */
    if (wnd && (ween_top_level(wnd)->ex_style & WS_EX_NOACTIVATE))
        return prev;
    if (prev)
        SendMessageA(prev, WM_KILLFOCUS, (WPARAM)wnd, 0);
    g_focus = wnd;
    if (wnd)
        SendMessageA(wnd, WM_SETFOCUS, (WPARAM)prev, 0);
    /* Both ends of the move repaint: the focus rectangle has to appear on one
     * and go from the other, and a control should not have to ask for that. */
    if (prev)
        InvalidateRect(prev, NULL, FALSE);
    if (wnd)
        InvalidateRect(wnd, NULL, FALSE);
    return prev;
}

/* Add a rectangle of the surface to what has to be painted again. */
void ween_damage_rect(struct ween_wnd *w, int x, int y, int cx, int cy)
{
    struct ween_wnd *top;
    if (!w || cx <= 0 || cy <= 0)
        return;
    top = ween_top_level(w);
    if (!top->dirty) { /* the first of the frame: it *is* the damage */
        top->damage.left = x;
        top->damage.top = y;
        top->damage.right = x + cx;
        top->damage.bottom = y + cy;
        top->dirty = 1;
        return;
    }
    if (x < top->damage.left)
        top->damage.left = x;
    if (y < top->damage.top)
        top->damage.top = y;
    if (x + cx > top->damage.right)
        top->damage.right = x + cx;
    if (y + cy > top->damage.bottom)
        top->damage.bottom = y + cy;
}

/* The whole of a top-level, which is what most things that ask for a repaint
 * mean: a window moved, a title changed, a menu opened. */
void ween_damage_all(struct ween_wnd *w)
{
    struct ween_wnd *top = ween_top_level(w);
    if (top)
        ween_damage_rect(top, 0, 0, top->surface.w, top->surface.h);
}

BOOL InvalidateRect(HWND wnd, const RECT *rect, BOOL erase)
{
    (void)erase;
    if (!wnd)
        return FALSE;
    if (!rect) {
        /* the whole window, frame and all: a child asking for itself still
         * means its own client area */
        if (wnd->parent) {
            RECT cr;
            int ox, oy;
            ween_client_origin(wnd, &ox, &oy);
            GetClientRect(wnd, &cr);
            int edge = ween_border_width(wnd);
            ween_damage_rect(wnd, ox - edge, oy - edge, wnd->w, wnd->h);
            (void)cr;
        } else {
            ween_damage_all(wnd);
        }
        return TRUE;
    }
    {
        /* a rectangle in the window's own client coordinates */
        int ox, oy;
        ween_client_origin(wnd, &ox, &oy);
        ween_damage_rect(wnd, ox + rect->left, oy + rect->top,
                         rect->right - rect->left, rect->bottom - rect->top);
    }
    return TRUE;
}

/* ---- painting ----------------------------------------------------------- */

static struct ween_dc g_dc; /* single-threaded: one DC serves every paint */
/* Whether a paint pass is running: the damage is still what is being
 * painted, though the flag that asked for it has been cleared. */
static int ween_painting;

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
    /* What actually needs painting, in the window's own coordinates: an
     * application that looks at this paints a stroke's worth instead of a
     * window's worth. Never larger than the client area, and the whole of it
     * when nothing narrower was asked for. */
    ps->rcPaint = cr;
    if (top->dirty || ween_painting) {
        RECT d = top->damage;
        int l = d.left - ox, t = d.top - oy;
        int r = d.right - ox, b = d.bottom - oy;
        if (l > ps->rcPaint.left)
            ps->rcPaint.left = l;
        if (t > ps->rcPaint.top)
            ps->rcPaint.top = t;
        if (r < ps->rcPaint.right)
            ps->rcPaint.right = r;
        if (b < ps->rcPaint.bottom)
            ps->rcPaint.bottom = b;
        if (ps->rcPaint.right < ps->rcPaint.left)
            ps->rcPaint.right = ps->rcPaint.left;
        if (ps->rcPaint.bottom < ps->rcPaint.top)
            ps->rcPaint.bottom = ps->rcPaint.top;
    }
    return &g_dc;
}

/* A device context for a window outside a paint. What it is for is measuring:
 * an application that lays something out has to ask how wide its text is
 * before it can draw any of it. Drawing through it works too — it is the same
 * context BeginPaint hands out, on the same surface — but a window that draws
 * outside WM_PAINT is drawing over whatever the next paint will put there. */
HDC GetDC(HWND wnd)
{
    PAINTSTRUCT ps;
    if (!wnd)
        wnd = g_tops;
    if (!wnd)
        return NULL;
    return BeginPaint(wnd, &ps);
}

int ReleaseDC(HWND wnd, HDC dc)
{
    (void)wnd;
    (void)dc; /* the one context is static: there is nothing to give back */
    return 1;
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
    /* Nothing of this window is inside what has to be painted: neither it
     * nor anything under it can have anything to say. */
    if (ox >= outer.right || oy >= outer.bottom || ox + cr.right <= outer.left ||
        oy + cr.bottom <= outer.top) {
        int edge = ween_border_width(w);
        if (ox - edge >= outer.right || oy - edge >= outer.bottom ||
            ox - edge + w->w <= outer.left || oy - edge + w->h <= outer.top)
            return;
    }

    if (w->parent) {
        /* the frame sits outside the client area: clip it to the window rect */
        int edge = ween_border_width(w);
        int wx = ox - edge, wy = oy - edge;
        int l = wx > outer.left ? wx : outer.left;
        int t = wy > outer.top ? wy : outer.top;
        int r = wx + w->w < outer.right ? wx + w->w : outer.right;
        int b = wy + w->h < outer.bottom ? wy + w->h : outer.bottom;
        ween_surface_clip(&top->surface, l, t, r - l, b - t);
        ween_paint_border(w);
        ween_wnd_sb_paint(w);
    }

    {   /* the client area, within whatever the parent allows */
        int l = ox > outer.left ? ox : outer.left;
        int t = oy > outer.top ? oy : outer.top;
        int r = ox + cr.right < outer.right ? ox + cr.right : outer.right;
        int b = oy + cr.bottom < outer.bottom ? oy + cr.bottom : outer.bottom;
        ween_surface_clip(&top->surface, l, t, r - l, b - t);
    }
    {
        /* A class background is either a brush or, by an old win32 habit, a
         * system colour index with one added -- `(HBRUSH)(COLOR_WINDOW + 1)`
         * is how nearly every program written since 1993 spells "the window
         * colour", and it is a small number rather than anything that can be
         * read through. */
        HBRUSH back = w->cls ? w->cls->background : NULL;
        if ((UINT_PTR)back &&
            (UINT_PTR)back <= COLOR_GRADIENTINACTIVECAPTION + 1)
            back = GetSysColorBrush((int)(UINT_PTR)back - 1);
        if (back)
            ween_surface_fill(&top->surface, ox, oy, cr.right, cr.bottom,
                              back->color);
    }
    SendMessageA(w, WM_PAINT, 0, 0);
    for (struct ween_wnd *c = w->first_child; c; c = c->next_sibling)
        paint_tree(c);
    ween_surface_clip(&top->surface, outer.left, outer.top,
                      outer.right - outer.left, outer.bottom - outer.top);
}

static void flush_one(struct ween_wnd *top)
{
    RECT d;
    if (!top || !top->dirty)
        return;
    top->dirty = 0;
    ween_painting = 1;
    /* Everything below draws through the damaged rectangle, which is what
     * makes a small change cost a small paint: the frame, the children and
     * anything dropped down over them are all clipped to it. */
    d = top->damage;
    if (d.left < 0)
        d.left = 0;
    if (d.top < 0)
        d.top = 0;
    if (d.right > top->surface.w)
        d.right = top->surface.w;
    if (d.bottom > top->surface.h)
        d.bottom = top->surface.h;
    if (d.right <= d.left || d.bottom <= d.top)
        return;
    ween_surface_clip(&top->surface, d.left, d.top, d.right - d.left,
                      d.bottom - d.top);
    SendMessageA(top, WM_NCPAINT, 0, 0);
    paint_tree(top);
    ween_surface_clip(&top->surface, d.left, d.top, d.right - d.left,
                      d.bottom - d.top);
    if (ween_active_backend)
        ween_active_backend->present(top->backend_win, &top->surface, &d);
    ween_painting = 0;
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

/* How close together two presses have to be to be a double click. win32 lets
 * the mouse control panel set it; ween32 keeps the number Windows ships with,
 * and a control that has to tell a pair from two clicks asks here rather than
 * inventing its own delay. */
UINT GetDoubleClickTime(void)
{
    return WEEN_DOUBLE_CLICK_MS;
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

/* ---- the clipboard -------------------------------------------------------
 *
 * USER32's shape: open it, empty it, put something in, close it. The data
 * belongs to the clipboard once handed over, and what comes back out stays
 * valid until the next thing replaces it.
 *
 * This is the process's own clipboard. Nothing is shared with other X clients
 * yet — that needs selection ownership and the round trip that goes with it —
 * so cut and paste work within an application and not yet between them. */

static char *g_clipboard;   /* CF_TEXT, owned here */
static HBITMAP g_clipboard_bitmap; /* CF_BITMAP, likewise */
static HWND g_clipboard_owner;
static int g_clipboard_open;

BOOL OpenClipboard(HWND owner)
{
    if (g_clipboard_open)
        return FALSE;
    g_clipboard_open = 1;
    g_clipboard_owner = owner;
    return TRUE;
}

BOOL CloseClipboard(void)
{
    if (!g_clipboard_open)
        return FALSE;
    g_clipboard_open = 0;
    return TRUE;
}

BOOL EmptyClipboard(void)
{
    if (!g_clipboard_open)
        return FALSE;
    free(g_clipboard);
    g_clipboard = NULL;
    if (g_clipboard_bitmap) {
        DeleteObject(g_clipboard_bitmap);
        g_clipboard_bitmap = NULL;
    }
    return TRUE;
}

HANDLE SetClipboardData(UINT format, HANDLE data)
{
    if (!g_clipboard_open)
        return NULL;
    if (format == CF_TEXT) {
        free(g_clipboard);
        g_clipboard = (char *)data; /* the clipboard owns it from here */
        return data;
    }
    if (format == CF_BITMAP) {
        /* A picture, which is what a drawing program cuts and pastes. The
         * clipboard owns the bitmap from here, as it does the text. */
        if (g_clipboard_bitmap)
            DeleteObject(g_clipboard_bitmap);
        g_clipboard_bitmap = (HBITMAP)data;
        return data;
    }
    return NULL;
}

HANDLE GetClipboardData(UINT format)
{
    if (!g_clipboard_open)
        return NULL;
    if (format == CF_TEXT)
        return g_clipboard;
    if (format == CF_BITMAP)
        return g_clipboard_bitmap;
    return NULL;
}

BOOL IsClipboardFormatAvailable(UINT format)
{
    if (format == CF_TEXT)
        return g_clipboard != NULL;
    if (format == CF_BITMAP)
        return g_clipboard_bitmap != NULL;
    return FALSE;
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

BOOL PostMessageA(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!wnd)
        return FALSE;
    post_msg(wnd, msg, wp, lp);
    return TRUE;
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
/* The window under a point, however deep. A newly created child sits at the
 * top of the z-order, so the last one in the list that contains the point is
 * the one on top — the group box created before its radio buttons must not
 * swallow their clicks. Having found one, look inside it the same way: a
 * control may hold controls of its own, and it is the innermost that the
 * mouse belongs to. */
static struct ween_wnd *child_at(struct ween_wnd *parent, int x, int y)
{
    int cx0, cy0;
    struct ween_wnd *hit = NULL;
    ween_client_origin(parent, &cx0, &cy0);
    for (struct ween_wnd *c = parent->first_child; c; c = c->next_sibling)
        if (c->visible && x - cx0 >= c->x && x - cx0 < c->x + c->w &&
            y - cy0 >= c->y && y - cy0 < c->y + c->h)
            hit = c;
    return hit ? child_at(hit, x, y) : parent;
}

/* Hover tracking. A window that asked for it with TrackMouseEvent hears
 * WM_MOUSELEAVE once, when the pointer next goes somewhere else — the
 * one-shot win32 gives, so a control re-arms it each time it is entered. */

BOOL TrackMouseEvent(TRACKMOUSEEVENT *track)
{
    if (!track || !track->hwndTrack)
        return FALSE;
    if (track->dwFlags & TME_CANCEL)
        track->hwndTrack->track_leave = 0;
    else if (track->dwFlags & TME_LEAVE)
        track->hwndTrack->track_leave = 1;
    return TRUE;
}

static void hover_moved_to(struct ween_wnd *now)
{
    if (g_hot == now)
        return;
    if (g_hot && !g_hot->destroyed && g_hot->track_leave) {
        g_hot->track_leave = 0; /* one shot, as win32 does */
        post_msg(g_hot, WM_MOUSELEAVE, 0, 0);
    }
    g_hot = now;
}

/* ---- cursors --------------------------------------------------------------
 *
 * A shape, not a bitmap: the window system has these already, and the classic
 * shell used its own. A handle is the shape number plus one, so it is never
 * NULL and there is nothing to free.
 *
 * The shape follows the pointer. A window's class says what it should be; a
 * control that wants something different over part of itself answers
 * WM_SETCURSOR, which is asked first — that is how a splitter shows a resize
 * arrow over the bar and an ordinary one either side of it.
 */

static int g_cursor_override = -1; /* what SetCursor asked for, this move */

HCURSOR LoadCursorA(HINSTANCE inst, LPCSTR name)
{
    int shape;
    (void)inst;
    switch ((int)(INT_PTR)name) {
    case 32513: shape = WEEN_CURSOR_IBEAM; break;
    case 32514: shape = WEEN_CURSOR_WAIT; break;
    case 32515: shape = WEEN_CURSOR_CROSS; break;
    case 32642: shape = WEEN_CURSOR_SIZENWSE; break;
    case 32643: shape = WEEN_CURSOR_SIZENESW; break;
    case 32644: shape = WEEN_CURSOR_SIZEWE; break;
    case 32645: shape = WEEN_CURSOR_SIZENS; break;
    case 32646: shape = WEEN_CURSOR_SIZEALL; break;
    case 32649: shape = WEEN_CURSOR_HAND; break;
    default: shape = WEEN_CURSOR_ARROW; break;
    }
    return (HCURSOR)(INT_PTR)(shape + 1);
}

/* A handle is either a small number -- a stock shape, plus one, so that zero
 * stays "none" -- or a pointer to one the application made. Nothing is
 * allocated in the first WEEN_CURSOR_COUNT bytes of an address space, so the
 * two cannot be confused. */
const ween_cursor *ween_cursor_of(void *handle)
{
    const ween_cursor *c = handle;
    if (!handle || (INT_PTR)handle <= WEEN_CURSOR_COUNT)
        return NULL;
    return c->magic == WEEN_CURSOR_MAGIC ? c : NULL;
}

HCURSOR CreateCursor(HINSTANCE inst, int xhot, int yhot, int width,
                     int height, const void *and_plane, const void *xor_plane)
{
    const unsigned char *ap = and_plane, *xp = xor_plane;
    ween_cursor *c;
    (void)inst;
    if (width <= 0 || height <= 0 || !ap || !xp)
        return NULL;
    c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->argb = calloc((size_t)width * (size_t)height, sizeof(unsigned));
    if (!c->argb) {
        free(c);
        return NULL;
    }
    c->magic = WEEN_CURSOR_MAGIC;
    c->w = width;
    c->h = height;
    c->xhot = xhot;
    c->yhot = yhot;
    int stride = (width + 7) / 8;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int bit = 0x80 >> (x % 8);
            int a = (ap[y * stride + x / 8] & bit) != 0;
            int v = (xp[y * stride + x / 8] & bit) != 0;
            /* AND set and XOR clear is the transparent one; AND set and XOR
             * set means invert, which is drawn black here. */
            unsigned px = a ? (v ? 0xFF000000u : 0u)
                            : (v ? 0xFFFFFFFFu : 0xFF000000u);
            c->argb[y * width + x] = px;
        }
    }
    return (HCURSOR)c;
}

BOOL DestroyCursor(HCURSOR cursor)
{
    ween_cursor *c = (ween_cursor *)ween_cursor_of(cursor);
    if (!c)
        return FALSE;
    /* whatever the backend made of it goes with it */
    if (ween_active_backend && ween_active_backend->set_cursor && c->backend)
        ween_active_backend->set_cursor(NULL, WEEN_CURSOR_ARROW, c);
    free(c->argb);
    c->magic = 0;
    free(c);
    return TRUE;
}

/* What SetCursor was handed, kept whole rather than as a shape number, so a
 * picture survives the trip to apply_cursor. */
static HCURSOR g_cursor_override_img;

HCURSOR SetCursor(HCURSOR cursor)
{
    int was = g_cursor_override;
    HCURSOR was_img = g_cursor_override_img;
    if (ween_cursor_of(cursor)) {
        g_cursor_override_img = cursor;
        g_cursor_override = WEEN_CURSOR_ARROW;
        return was_img ? was_img
                       : (was < 0 ? NULL : (HCURSOR)(INT_PTR)(was + 1));
    }
    g_cursor_override_img = NULL;
    g_cursor_override = cursor ? (int)(INT_PTR)cursor - 1 : WEEN_CURSOR_ARROW;
    return was_img ? was_img : (was < 0 ? NULL : (HCURSOR)(INT_PTR)(was + 1));
}

/* Ask the window under the pointer what shape it wants, and tell the backend
 * if it has changed. Called as the pointer moves, so this is the one place
 * the shape is decided. */
static void apply_cursor(struct ween_wnd *top, struct ween_wnd *under, int x,
                         int y)
{
    int shape;
    const ween_cursor *img;
    if (!ween_active_backend || !ween_active_backend->set_cursor || !top ||
        !under)
        return;
    g_cursor_override = -1;
    g_cursor_override_img = NULL;
    /* WM_SETCURSOR carries where the pointer is, as a hit-test code, and
     * which message asked -- an application that has a cursor of its own
     * answers only for HTCLIENT, and would sit on its hands if this said
     * nothing. */
    LRESULT hit = SendMessageA(under, WM_NCHITTEST, 0,
                               MAKELPARAM((WORD)x, (WORD)y));
    SendMessageA(under, WM_SETCURSOR, (WPARAM)under,
                 MAKELPARAM((WORD)hit, WM_MOUSEMOVE));
    img = ween_cursor_of(g_cursor_override_img);
    shape = g_cursor_override >= 0
                ? g_cursor_override
                : (under->cls ? under->cls->cursor : WEEN_CURSOR_ARROW);
    if (!img && under->cls && g_cursor_override < 0)
        img = under->cls->cursor_img;
    if (shape != top->cursor_shown || img != top->cursor_img_shown) {
        top->cursor_shown = shape;
        top->cursor_img_shown = img;
        ween_active_backend->set_cursor(top->backend_win, shape, img);
    }
}

/* What is held down as far as the backend has told us: the events carry it,
 * and a mouse message passes it on in its wParam the way win32 does. */
static int g_mods;
/* Which mouse buttons are down, as MK_* bits: set on the press and cleared
 * on the release, so that a move between the two says so. */
static int g_buttons;

static int button_bit(int button)
{
    switch (button) {
    case 1:
        return MK_LBUTTON;
    case 2:
        return MK_MBUTTON;
    case 3:
        return MK_RBUTTON;
    default:
        return 0;
    }
}
/* What was held when the last event arrived, which is as much as one
 * keyboard can say between messages. GetKeyState answers from it. */
static int g_shift_down, g_ctrl_down, g_alt_down;

/* Whether a modifier is down right now. win32 answers for every key; here
 * only the three an application asks about mid-gesture are tracked -- a
 * drawing program holding Shift to constrain a line, say. */
SHORT GetKeyState(int vk)
{
    int down = 0;
    switch (vk) {
    case VK_SHIFT:
        down = g_shift_down;
        break;
    case VK_CONTROL:
        down = g_ctrl_down;
        break;
    case VK_MENU:
        down = g_alt_down;
        break;
    default:
        return 0;
    }
    return (SHORT)(down ? (short)0x8000 : 0);
}

static void route_mouse(struct ween_wnd *top, UINT msg, int x, int y)
{
    int ox, oy;
    struct ween_wnd *dst = g_capture;
    g_cursor_top = top; /* the last place the pointer was seen */
    g_cursor_x = x;
    g_cursor_y = y;
    if (!dst)
        dst = child_at(top, x, y);
    if (msg == WM_MOUSEMOVE) {
        hover_moved_to(dst);
        apply_cursor(top, dst, x, y);
    }
    /* Whether the second of a quick pair is a double click is up to the window
     * it lands on: only a class registered with CS_DBLCLKS hears about them.
     * Anything else gets another ordinary press, which is what stops rapid
     * clicking from losing every other one on a control that does not care. */
    if (msg == WM_LBUTTONDOWN && g_dblclk && dst->cls &&
        (dst->cls->style & CS_DBLCLKS))
        msg = WM_LBUTTONDBLCLK;
    /* x,y arrived measured against the window the pointer was in. What they
     * are being delivered to may be in another one — a drag that started on a
     * popup keeps the pointer while it wanders off the popup — so they are
     * moved into that window's own frame first. */
    {
        struct ween_wnd *dtop = ween_top_level(dst);
        if (dtop != top) {
            int tx, ty, dx, dy;
            ween_window_origin(top, &tx, &ty);
            ween_window_origin(dtop, &dx, &dy);
            x += tx - dx;
            y += ty - dy;
        }
    }
    /* A press in the window's own scroll bar is not a click in its client
     * area: the library owns those pixels, as USER32 does. */
    if (ween_wnd_sb_mouse(dst, msg, x, y))
        return;
    ween_client_origin(dst, &ox, &oy);
    /* x,y are window coords of the top-level == surface coords */
    post_msg(dst, msg, (WPARAM)g_mods,
             MAKELPARAM((WORD)(x - ox), (WORD)(y - oy)));
}

/* Grow or shrink the top-level window: the surface follows the backend, and
 * the app hears about it through WM_SIZE. */
static void resize_top(struct ween_wnd *top, int w, int h)
{
    RECT cr;
    /* A person dragging a frame cannot pull it down to nothing; a program
     * saying what size it wants is taken at its word, since a menu or a box
     * with two names in it is smaller than any floor worth having. */
    if (top->style & WS_THICKFRAME) {
        if (w < 120)
            w = 120;
        if (h < 60)
            h = 60;
    }
    if (w == top->w && h == top->h)
        return;
    top->w = w;
    top->h = h;
    ween_surface_resize(&top->surface, w, h);
    GetClientRect(top, &cr);
    SendMessageA(top, WM_SIZE, SIZE_RESTORED,
                 MAKELPARAM((WORD)cr.right, (WORD)cr.bottom));
    ween_damage_all(top);
}

/* Dragging a sizing border: the pointer moves an edge, the window follows.
 *
 * Two things this cannot do by counting steps. The size is measured from
 * where the drag began, not added up move by move, because the window may not
 * end up the size that was asked for — a window manager clamps, a minimum
 * stops a shrink — and adding the next step to a size that was refused walks
 * the border away from the pointer. And the pointer moves in screen pixels
 * while the window is measured in the pixels it is drawn from, which at 2x
 * are not the same pixel: following it one for one grew the window twice as
 * fast as the hand moved. */
static void nc_drag_size(struct ween_wnd *top, const ween_event *down, int edge)
{
    int zoom = ween_zoom();
    int x0 = down->x_root, y0 = down->y_root, w0 = top->w, h0 = top->h;
    int want_w = w0, want_h = h0;
    for (;;) {
        ween_event ev = ween_active_backend->next_event(top->backend_win, -1);
        if (ev.kind == WEEN_EV_EXPOSE) {
            ween_mark_exposed(&ev); /* whatever was uncovered still needs it */
            ween_flush_paint();
            continue;
        }
        if (ev.kind == WEEN_EV_MOUSE_MOVE) {
            int dx = (ev.x_root - x0) / zoom, dy = (ev.y_root - y0) / zoom;
            int w = w0, h = h0;
            if (edge == HTRIGHT || edge == HTBOTTOMRIGHT || edge == HTTOPRIGHT)
                w = w0 + dx;
            if (edge == HTBOTTOM || edge == HTBOTTOMRIGHT || edge == HTBOTTOMLEFT)
                h = h0 + dy;
            /* win32 will not let a drag pull a window past the screen it is
             * on — the maximum tracking size WM_GETMINMAXINFO gives — and a
             * window bigger than the display is one whose every frame is
             * copied to a screen that cannot show it. */
            {
                int max_w = GetSystemMetrics(SM_CXSCREEN);
                int max_h = GetSystemMetrics(SM_CYSCREEN);
                if (max_w > 0 && w > max_w)
                    w = max_w;
                if (max_h > 0 && h > max_h)
                    h = max_h;
            }
            if (w == want_w && h == want_h)
                continue;
            want_w = w;
            want_h = h;
            /* Ask, and let the answer do the resizing. Resizing here as well
             * means a window manager that hands back a size of its own is
             * answered by our asking again, and the window flickers between
             * the two for as long as the drag lasts. */
            if (ween_active_backend->resize)
                ween_active_backend->resize(top->backend_win, w, h);
            if (!ween_active_backend->resize_is_answered) {
                resize_top(top, w, h);
                ween_flush_paint();
            }
        } else if (ev.kind == WEEN_EV_MOUSE_UP || ev.kind == WEEN_EV_END) {
            return;
        } else if (ev.kind == WEEN_EV_RESIZE) {
            resize_top(top, ev.x, ev.y);
            ween_flush_paint();
        }
    }
}

/* Non-client interactions that USER32 handled internally: dragging the window
 * by its caption and tracking the close box. */
static void nc_drag_caption(struct ween_wnd *top, const ween_event *down)
{
    int zoom = ween_zoom();
    /* Measured from where the drag began and counted in the pixels the window
     * is measured in, for the same two reasons a sizing drag is: the pointer
     * moves in screen pixels, and a step that rounds to nothing must not be
     * lost — a hundred of them are a hundred pixels. */
    int x0 = down->x_root, y0 = down->y_root, dx = 0, dy = 0;
    for (;;) {
        ween_event ev = ween_active_backend->next_event(top->backend_win, -1);
        if (ev.kind == WEEN_EV_EXPOSE) {
            ween_mark_exposed(&ev);
            ween_flush_paint();
            continue;
        }
        if (ev.kind == WEEN_EV_MOUSE_MOVE) {
            int want_x = (ev.x_root - x0) / zoom, want_y = (ev.y_root - y0) / zoom;
            if (want_x == dx && want_y == dy)
                continue;
            ween_active_backend->move_by(top->backend_win, want_x - dx,
                                         want_y - dy);
            /* the window rect follows the window: a program that asks where
             * it is, or is moved somewhere else afterwards, is otherwise
             * working from where it was before anyone touched it */
            top->x += want_x - dx;
            top->y += want_y - dy;
            dx = want_x;
            dy = want_y;
        } else if (ev.kind == WEEN_EV_MOUSE_UP || ev.kind == WEEN_EV_END) {
            return;
        }
    }
}

/* Holding a caption button: it goes down, follows the pointer in and out, and
 * acts on the release — the same contract as the close box. */
static void nc_track_button(struct ween_wnd *top, int which)
{
    top->nc_button_pressed = which;
    ween_damage_all(top);
    ween_flush_paint();
    for (;;) {
        ween_event ev = ween_active_backend->next_event(top->backend_win, -1);
        if (ev.kind == WEEN_EV_EXPOSE) {
            ween_mark_exposed(&ev);
            ween_flush_paint();
            continue;
        }
        if (ev.kind == WEEN_EV_MOUSE_MOVE) {
            RECT c = nc_button_rect(top, which);
            int in = ev.x >= c.left && ev.x < c.right && ev.y >= c.top &&
                     ev.y < c.bottom;
            if (in != (top->nc_button_pressed == which)) {
                top->nc_button_pressed = in ? which : 0;
                ween_damage_all(top);
                ween_flush_paint();
            }
        } else if (ev.kind == WEEN_EV_MOUSE_UP || ev.kind == WEEN_EV_END) {
            int acted = top->nc_button_pressed == which;
            top->nc_button_pressed = 0;
            ween_damage_all(top);
            ween_flush_paint();
            if (acted)
                SendMessageA(top, WM_SYSCOMMAND,
                             which != 1 ? SC_MINIMIZE
                             : nc_has_help(top)
                                 ? SC_CONTEXTHELP
                                 : top->maximized ? SC_RESTORE : SC_MAXIMIZE,
                             0);
            return;
        }
    }
}

static void nc_track_close(struct ween_wnd *top)
{
    top->nc_close_pressed = 1;
    ween_damage_all(top);
    ween_flush_paint();
    for (;;) {
        ween_event ev = ween_active_backend->next_event(top->backend_win, -1);
        if (ev.kind == WEEN_EV_EXPOSE) {
            ween_mark_exposed(&ev);
            ween_flush_paint();
            continue;
        }
        if (ev.kind == WEEN_EV_MOUSE_MOVE) {
            RECT c = nc_close_rect(top);
            int in = ev.x >= c.left && ev.x < c.right && ev.y >= c.top && ev.y < c.bottom;
            if (in != top->nc_close_pressed) {
                top->nc_close_pressed = in;
                ween_damage_all(top);
                ween_flush_paint();
            }
        } else if (ev.kind == WEEN_EV_MOUSE_UP || ev.kind == WEEN_EV_END) {
            int fire = top->nc_close_pressed;
            top->nc_close_pressed = 0;
            ween_damage_all(top);
            ween_flush_paint();
            if (fire)
                post_msg(top, WM_CLOSE, 0, 0);
            return;
        }
    }
}

/* A press on the menu bar: the item under it opens, and stays open while the
 * drop-down is tracked. win32 keeps the bar item drawn selected throughout,
 * which is what menu_hot is for. */
static void nc_track_menu(struct ween_wnd *top, const ween_event *ev)
{
    int frame = ween_frame_width(top);
    int bar_y = frame + ween_caption_height(top);
    int index;

    ween_menu_layout_bar(top->menu, ween_gui_font(), top->w - 2 * frame);
    index = ween_menu_hit(top->menu, ev->x - frame, ev->y - bar_y);
    if (index < 0)
        return;
    UINT cmd = ween_menu_track_bar(top, index, 0);
    if (cmd)
        post_msg(top, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0), 0);
}

/* Shown to begin with, unlike the menu's underlines: a Windows 2000 folder
 * opens with the caret on its first item and no selection at all. A click in
 * a list puts it away again, and a key brings it back. */
int ween_ui_focus_cues = 1;

/* Alt, or Alt+letter, opens the bar from the keyboard. Returns whether the
 * key was one the menu wanted. */
/* Alt on its own arms the bar rather than opening it: the underlines come
 * out, the first item goes under the keyboard, and the bar waits — for a
 * letter, for the arrows to walk it, or for Down to open what it is on.
 * That is what Windows does, in a frame's bar and in a shell's band alike. */
static int g_menu_armed;
static int g_menu_armed_at;
static HWND g_menu_armed_top;

int ween_menu_armed(void)
{
    return g_menu_armed;
}

/* Where the armed bar shows itself: the frame's own strip. */
static void armed_mark(HWND top, int index)
{
    if (top) {
        top->menu_hot = index;
        ween_damage_all(top);
        InvalidateRect(top, NULL, FALSE);
    }
}

/* Once Alt has been pressed the underlines stay out: they are what the window
 * has been told about how it is being driven, not part of the menu being up.
 * A dialog opened from a menu that was walked by key has them, which is what
 * the machine's Column Settings shows — and one opened with the mouse never
 * turned them on in the first place. */

void ween_menu_disarm(void)
{
    HWND top = g_menu_armed_top;
    if (!g_menu_armed)
        return;
    g_menu_armed = 0;
    armed_mark(top, -1);
    g_menu_armed_top = NULL;
}

int ween_menu_key(HWND top, unsigned vk, unsigned ch)
{
    int index = 0;
    UINT cmd;
    if (!top || !top->menu)
        return 0;
    ween_menu_layout_bar(top->menu, ween_gui_font(),
                         top->w - 2 * ween_frame_width(top));
    if (ch) {
        index = ween_menu_mnemonic(top->menu, ch);
        if (index < 0)
            return 0;
    } else if (vk != VK_MENU && vk != VK_F10) {
        return 0;
    }
    /* the bar was reached by key, so the letters show — and Alt brings out
     * the focus rectangles with them */
    ween_menu_cues = 1;
    ween_ui_focus_cues = 1;
    if (!ch) { /* Alt alone: arm it and wait, opening nothing */
        if (g_menu_armed) {
            ween_menu_disarm(); /* a second Alt puts it away again */
            return 1;
        }
        g_menu_armed = 1;
        g_menu_armed_at = 0;
        g_menu_armed_top = top;
        armed_mark(top, 0);
        ween_damage_all(top);
        return 1;
    }
    g_menu_armed = 0;
    g_menu_armed_top = NULL;
    cmd = ween_menu_track_bar(top, index, 1);
    if (cmd)
        post_msg(top, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0), 0);
    return 1;
}

/* The keys the armed bar answers before anything else sees them: the arrows
 * walk it, Down or Enter opens what it is on with that drop-down's first item
 * picked, Escape puts it away. Returns whether the key was one of them. */
int ween_menu_armed_key(HWND top, unsigned vk)
{
    int count;
    UINT cmd;
    if (!g_menu_armed || !top || !top->menu)
        return 0;
    count = GetMenuItemCount(top->menu);
    if (count <= 0)
        return 0;
    switch (vk) {
    case VK_LEFT:
    case VK_RIGHT:
        g_menu_armed_at = (g_menu_armed_at + (vk == VK_RIGHT ? 1 : count - 1)) %
                          count;
        armed_mark(top, g_menu_armed_at);
        return 1;
    case VK_ESCAPE:
        ween_menu_disarm();
        return 1;
    case VK_DOWN:
    case VK_RETURN:
    case VK_SPACE: {
        int index = g_menu_armed_at;
        g_menu_armed = 0; /* the session draws the item open from here */
        g_menu_armed_top = NULL;
        cmd = ween_menu_track_bar(top, index, 1);
        if (cmd)
            post_msg(top, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0), 0);
        return 1;
    }
    default:
        return 0;
    }
}

/* An expose that arrives inside a nested loop — a drag, a menu being tracked —
 * belongs to whichever window it names, not to the loop. Swallowing it leaves
 * that window holding whatever was on screen before, which is how a window
 * ends up as a lump of grey after something in front of it goes away. */
void ween_window_origin(struct ween_wnd *top, int *x, int *y)
{
    *x = top ? top->x : 0;
    *y = top ? top->y : 0;
    if (top && top->backend_win && ween_active_backend &&
        ween_active_backend->origin)
        ween_active_backend->origin(top->backend_win, x, y);
}

void ween_mark_exposed(const ween_event *ev)
{
    struct ween_wnd *target = g_active;
    if (ev->win) {
        for (struct ween_wnd *t = g_tops; t; t = t->next_top)
            if (t->backend_win == ev->win)
                target = t;
    }
    if (target)
        ween_damage_all(target);
}

/* Which window a pointer event happened in, and the event moved into that
 * window's coordinates. Events arrive measured against the active window; a
 * popup standing over it takes any that land on it, without becoming active
 * — a menu and a drop-down are put up precisely so as not to.
 */
static struct ween_wnd *pointer_over(struct ween_wnd *active, ween_event *ev)
{
    int ax, ay, sx, sy;
    if (!active || (ev->kind != WEEN_EV_MOUSE_DOWN &&
                    ev->kind != WEEN_EV_MOUSE_UP &&
                    ev->kind != WEEN_EV_MOUSE_MOVE && ev->kind != WEEN_EV_WHEEL))
        return active;
    if (g_capture) /* a drag holds on to the pointer wherever it goes */
        return active;
    ween_window_origin(active, &ax, &ay);
    sx = ax + ev->x;
    sy = ay + ev->y;
    for (struct ween_wnd *t = g_tops; t; t = t->next_top) {
        int ox, oy;
        if (t == active || !t->visible || !(t->style & WS_POPUP))
            continue;
        ween_window_origin(t, &ox, &oy);
        if (sx >= ox && sy >= oy && sx < ox + t->w && sy < oy + t->h) {
            ev->x = sx - ox;
            ev->y = sy - oy;
            return t;
        }
    }
    return active;
}

/* Translate one backend event into posted messages. */
static void pump_event(struct ween_wnd *top, const ween_event *ev)
{
    /* A disabled window takes no input — not in its client area and not in
     * its caption either. That is the whole of what a modal dialog does to
     * the window that owns it, and without it the owner can still be dragged
     * and closed from under the dialog. */
    if ((top->style & WS_DISABLED) &&
        (ev->kind == WEEN_EV_MOUSE_DOWN || ev->kind == WEEN_EV_MOUSE_UP ||
         ev->kind == WEEN_EV_MOUSE_MOVE || ev->kind == WEEN_EV_KEY ||
         ev->kind == WEEN_EV_WHEEL || ev->kind == WEEN_EV_CLOSE))
        return;

    /* Every event says what was held when it happened; a mouse message passes
     * that on as win32 does, in the low bits of its wParam — the two
     * modifier keys, and which mouse buttons are down. The buttons are what
     * a drag is written against: `if (!(wp & MK_LBUTTON)) return;` is how
     * every control in win32 tells a drag from a pointer wandering over it,
     * and without them nothing could be dragged. */
    if (ev->kind == WEEN_EV_MOUSE_DOWN)
        g_buttons |= button_bit(ev->button);
    else if (ev->kind == WEEN_EV_MOUSE_UP)
        g_buttons &= ~button_bit(ev->button);
    g_mods = (ev->shift ? MK_SHIFT : 0) | (ev->ctrl ? MK_CONTROL : 0) |
             g_buttons;
    g_shift_down = ev->shift;
    g_ctrl_down = ev->ctrl;
    g_alt_down = ev->alt;
    switch (ev->kind) {
    case WEEN_EV_EXPOSE:
        ween_damage_all(top);
        break;
    case WEEN_EV_MOUSE_DOWN: {
        LRESULT hit;
        if (ev->button == 3) { /* the right one, which asks for a menu */
            route_mouse(top, WM_RBUTTONDOWN, ev->x, ev->y);
            break;
        }
        if (ev->button != 1) /* the wheel arrives as buttons 4 and 5 */
            break;
        /* Two presses close together in time and place are a double click.
         * win32 measures both — a drag away and back is not one — and counts
         * the pair, so a third press starts a new pair rather than making a
         * triple. */
        {
            static unsigned long last_ms;
            static int last_x, last_y, have_last;
            static struct ween_wnd *last_top;
            unsigned long now = now_ms();
            int near = ev->x - last_x <= 4 && last_x - ev->x <= 4 &&
                       ev->y - last_y <= 4 && last_y - ev->y <= 4;
            g_dblclk = have_last && last_top == top && near &&
                       now - last_ms <= WEEN_DOUBLE_CLICK_MS;
            /* A pair is a pair, not a run: the click that completes one does
             * not start the next. Tracked with a flag rather than a sentinel
             * time, because under the headless clock zero is a real time and
             * every click would pair with the one before it. */
            have_last = !g_dblclk;
            last_ms = now;
            last_x = ev->x;
            last_y = ev->y;
            last_top = top;
        }
        hit = SendMessageA(top, WM_NCHITTEST, 0,
                           MAKELPARAM((WORD)ev->x, (WORD)ev->y));
        if (hit == HTMENU)
            nc_track_menu(top, ev);
        else if (hit == HTCAPTION)
            nc_drag_caption(top, ev);
        else if (hit == HTCLOSE)
            nc_track_close(top);
        else if (hit == HTMAXBUTTON || hit == HTHELP)
            nc_track_button(top, 1);
        else if (hit == HTMINBUTTON)
            nc_track_button(top, 2);
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
        if (ev->button == 3) {
            route_mouse(top, WM_RBUTTONUP, ev->x, ev->y);
            break;
        }
        if (ev->button != 1)
            break;
        route_mouse(top, WM_LBUTTONUP, ev->x, ev->y);
        break;
    case WEEN_EV_MOUSE_MOVE:
        route_mouse(top, WM_MOUSEMOVE, ev->x, ev->y);
        break;
    case WEEN_EV_RESIZE:
        /* The window manager gave us a geometry. A window with a sizing border
         * follows it — that is what resizable means. One without has told the
         * window manager its size is fixed, and win32 semantics are that it
         * cannot be resized at all; a tiling window manager hands back its
         * tile regardless, and stretching to fill it would lay out a dialog at
         * a size it was never written for. The backend centres it instead. */
        if (top->style & WS_THICKFRAME)
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
        /* Alt with anything brings the underlines out, whether or not a menu
         * wants the key: they are what the window has been told about how it
         * is being driven, and Alt and Enter — which opens a Properties sheet
         * without going near a menu — brings them out on the machine. */
        if (ev->alt) {
            ween_menu_cues = 1;
            ween_ui_focus_cues = 1;
        }
        /* Somebody is using the keyboard, which is what a window put up from
         * here on has to know: the machine shows the underlines under the
         * mnemonics of a dialog opened by key and not of one opened by mouse,
         * and Ctrl+O is enough to count -- its Open box comes up with them.
         * The windows already on the screen keep what they had. */
        ween_kbd_used = 1;
        /* the character rides in the high word, where win32 keeps the scan
         * code and repeat count; TranslateMessage turns it into WM_CHAR */
        post_msg(g_focus ? g_focus : (HWND)top, WM_KEYDOWN, ev->vk,
                 (LPARAM)(ev->ch << 16) | (ev->shift ? 1 : 0) |
                     (ev->ctrl ? (1L << 28) : 0) | (ev->alt ? (1L << 29) : 0));
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

/* An event a nested loop decided was not its own. A menu that dismisses on a
 * press of the right button has to hand that press back: Windows delivers it,
 * which is how a right click on another file closes one menu, picks the file
 * and opens the next. It is put here rather than dispatched on the spot so
 * that the menu's loop has really finished before the window below hears it. */
static ween_event g_replay;
static int g_has_replay;

void ween_replay_event(const ween_event *ev)
{
    g_replay = *ev;
    g_has_replay = 1;
}

BOOL GetMessageA(LPMSG msg, HWND wnd, UINT min, UINT max)
{
    (void)wnd;
    (void)min;
    (void)max;
    if (!msg)
        return -1;
    /* The safe point for the windows DestroyWindow set aside: nothing is
     * inside a window procedure here. */
    reap_windows();
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
        if (g_has_replay) { /* something a nested loop handed back */
            ween_event ev = g_replay;
            struct ween_wnd *target = g_active;
            g_has_replay = 0;
            if (ev.win)
                for (struct ween_wnd *t = g_tops; t; t = t->next_top)
                    if (t->backend_win == ev.win)
                        target = t;
            if (target) {
                g_active = target;
                pump_event(target, &ev);
            }
            continue;
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
            if (!target)
                continue;
            /* What a window is sent is not what makes it the active one. A
             * press is a person turning to it, and that does; a repaint, the
             * pointer crossing it, or a key are none of them. A key least of
             * all: it arrives at whatever window the window system thinks
             * has the keyboard, and a palette it handed the keyboard to on
             * its own account must not take the typing away from the window
             * being worked in. Where a key goes is settled here, not there. */
            if (ev.kind == WEEN_EV_MOUSE_DOWN &&
                !(target->ex_style & WS_EX_NOACTIVATE)) {
                ween_set_active(target);
                if (!g_focus || ween_top_level(g_focus) != target)
                    g_focus = target;
            }
        } else {
            /* A backend that does not say which window an event was in —
             * the headless one — leaves it to be worked out from where the
             * pointer is, which is what a window system does anyway. A press
             * over a popup belongs to the popup even though the popup is not
             * the active window; that is how a menu, or a box of suggestions
             * under a field, is reached at all. */
            struct ween_wnd *over = pointer_over(target, &ev);
            if (!target)
                continue;
            if (over != target) {
                pump_event(over, &ev);
                continue;
            }
        }
        pump_event(target, &ev);
    }
}

/* ---- DefWindowProc --------------------------------------------------------- */

/* The keys the menu bar answers rather than whatever has the keyboard: Alt
 * or F10 arms it, Alt and a letter opens the drop-down that letter marks, and
 * once it is armed the arrows walk it and Escape puts it away. Answers 1 when
 * the menus took the key.
 *
 * Asked through WM_SYSCOMMAND with SC_KEYMENU rather than of the menu
 * directly, because an application whose menu is not the frame's -- a shell
 * keeps its bar in a rebar band -- answers that message itself.
 *
 * Both the message loop's helper and the window procedure come here, which is
 * the point: on Windows this is DefWindowProc's work, through WM_SYSKEYDOWN
 * and WM_SYSCHAR, so a program with a menu bar and nothing else gets it for
 * nothing. It used to live in IsDialogMessage alone, and a program that never
 * calls that -- Notepad does not -- could not open its own File menu. */
int ween_menu_keydown(HWND wnd, unsigned vk, LPARAM lp)
{
    HWND top = ween_top_level(wnd);
    /* the backend puts Alt in bit 29, where win32 keeps the context code,
     * and the character the key would type in the high word */
    int alt = (lp & (1L << 29)) != 0;
    unsigned ch = (unsigned)(lp >> 16) & 0xff;
    unsigned key = ch ? ch : vk;
    if (!top)
        return 0;
    if (vk == VK_MENU || vk == VK_F10)
        return SendMessageA(top, WM_SYSCOMMAND, SC_KEYMENU, 0) == 0;
    if (!alt && ween_menu_armed()) {
        if (ween_menu_armed_key(top, vk))
            return 1;
        if (ween_menu_key(top, 0, key))
            return 1;
    }
    if (alt)
        return SendMessageA(top, WM_SYSCOMMAND, SC_KEYMENU, (LPARAM)key) == 0;
    return 0;
}

LRESULT DefWindowProcA(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        /* Alt, F10 and Alt+letter belong to the menu bar wherever they are
         * pressed, which is why they are answered here rather than by
         * whatever has the keyboard. A window with no menu takes none of
         * them and the key goes on as before. */
        if (ween_menu_keydown(wnd, (unsigned)wp, lp))
            return 0;
        return 0;
    case WM_TIMER:
        /* The repeat behind a held scroll-bar arrow. Windows runs this on a
         * timer of its own that the application never sees; here it arrives
         * as a WM_TIMER the application has passed on, which is the same
         * thing as long as it does pass it on. */
        if (wp == (WPARAM)WEEN_SB_TIMER_ID)
            return ween_wnd_sb_timer(wnd);
        return 0;
    case WM_RBUTTONUP: {
        /* What asks for a context menu, carrying the point in desktop
         * coordinates the way TrackPopupMenu wants it. A control that does
         * not handle it passes it up, so the window gets asked in the end. */
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ClientToScreen(wnd, &pt);
        return SendMessageA(wnd, WM_CONTEXTMENU, (WPARAM)wnd,
                            MAKELPARAM((WORD)pt.x, (WORD)pt.y));
    }
    case WM_CONTEXTMENU:
        /* Unhandled here means the parent is asked, which is how a control
         * inside a window comes to show the window's menu. */
        if (wnd->parent)
            return SendMessageA(wnd->parent, WM_CONTEXTMENU, wp, lp);
        return 0;
    case WM_CHANGEUISTATE: {
        /* An application saying the cues should show or stop showing. Windows
         * keeps this per window and passes it down the tree; ween32 keeps one
         * for the process, which is as much as one keyboard can mean. */
        UINT action = LOWORD(wp), flags = HIWORD(wp);
        int show = action == UIS_CLEAR; /* the flags name what is hidden */
        if (action == UIS_INITIALIZE)
            return 0;
        if (flags & UISF_HIDEACCEL)
            ween_menu_cues = show;
        if (flags & UISF_HIDEFOCUS)
            ween_ui_focus_cues = show;
        ween_damage_all(wnd);
        InvalidateRect(ween_top_level(wnd), NULL, FALSE);
        return 0;
    }
    case WM_SETFONT:
        /* The font a control draws with. An application makes one and hands
         * it to everything it creates, which is how a window comes to be in
         * one face rather than whatever each control thought of. */
        if (wp) {
            const ween_gdiobj *o = (const ween_gdiobj *)wp;
            if (o->kind == WEEN_OBJ_FONT && o->font)
                wnd->font = o->font;
        } else {
            wnd->font = ween_gui_font();
        }
        if (LOWORD(lp))
            InvalidateRect(wnd, NULL, TRUE);
        return 0;
    case WM_QUERYUISTATE:
        /* What is hidden, not what is shown — the flags are named for what
         * they take away. An application drawing its own labels asks this to
         * know whether to underline the mnemonics. */
        return (ween_menu_cues ? 0 : UISF_HIDEACCEL) |
               (ween_ui_focus_cues ? 0 : UISF_HIDEFOCUS);
    case WM_SETICON: {
        /* One icon, not win32's small-and-large pair: the caption is the only
         * place ween32 draws one, and it wants the small one. */
        HICON was = wnd->icon;
        wnd->icon = (HICON)lp;
        ween_damage_all(wnd);
        return (LRESULT)(INT_PTR)was;
    }
    case WM_GETICON:
        return (LRESULT)(INT_PTR)wnd->icon;
    case WM_NEXTDLGCTL: {
        /* wParam is the control to focus when lParam says so, otherwise a
         * direction: 0 forward, non-zero back. win32 handles this in the
         * dialog procedure only; ween32 answers it from any window, for the
         * same reason IsDialogMessageA works on any window — a window with
         * controls in it wants the dialog keyboard whether or not it is one. */
        HWND next = lp ? (HWND)wp : ween_tab_next(wnd, ween_focus_get(), !wp);
        if (next)
            SetFocus(next);
        return 0;
    }
    case WM_NCHITTEST: {
        if (!ween_has_caption(wnd))
            return HTCLIENT;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT c = nc_button_rect(wnd, 0);
        if (x >= c.left && x < c.right && y >= c.top && y < c.bottom)
            return HTCLOSE;
        if (nc_has_max(wnd) || nc_has_help(wnd)) {
            c = nc_button_rect(wnd, 1);
            if (x >= c.left && x < c.right && y >= c.top && y < c.bottom)
                return nc_has_max(wnd) ? HTMAXBUTTON : HTHELP;
        }
        if (nc_has_min(wnd)) {
            c = nc_button_rect(wnd, 2);
            if (x >= c.left && x < c.right && y >= c.top && y < c.bottom)
                return HTMINBUTTON;
        }
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
        if (wnd->menu &&
            y >= frame + ween_caption_height(wnd) &&
            y < frame + ween_caption_height(wnd) + ween_ncm(WEEN_NC_MENU) &&
            x >= frame && x < wnd->w - frame)
            return HTMENU;
        if (y < frame + ween_caption_height(wnd) && y >= frame && x >= frame &&
            x < wnd->w - frame)
            return HTCAPTION;
        return HTCLIENT;
    }

    case WM_NCPAINT: {
        if (wnd->parent)
            return 0;
        ween_surface *s = &wnd->surface;
        if (has_flat_border(wnd)) {
            /* one line of COLOR_WINDOWFRAME and nothing else; what is inside
             * it is the client area's own business */
            ween_surface_fill(s, 0, 0, wnd->w, 1, WEEN_BLACK);
            ween_surface_fill(s, 0, wnd->h - 1, wnd->w, 1, WEEN_BLACK);
            ween_surface_fill(s, 0, 0, 1, wnd->h, WEEN_BLACK);
            ween_surface_fill(s, wnd->w - 1, 0, 1, wnd->h, WEEN_BLACK);
            return 0;
        }
        /* Raised frame + face border. Only the band outside the client
         * area: what is inside it belongs to the window and is filled with
         * its class's brush before its WM_PAINT, so clearing the whole
         * surface here painted a hundred and ten thousand pixels a frame
         * that were painted again immediately. */
        {
            int cox, coy;
            RECT cr;
            ween_client_origin(wnd, &cox, &coy);
            GetClientRect(wnd, &cr);
            ween_surface_fill(s, 0, 0, wnd->w, coy, WEEN_FACE);
            ween_surface_fill(s, 0, coy + cr.bottom, wnd->w,
                              wnd->h - coy - cr.bottom, WEEN_FACE);
            ween_surface_fill(s, 0, coy, cox, cr.bottom, WEEN_FACE);
            ween_surface_fill(s, cox + cr.right, coy,
                              wnd->w - cox - cr.right, cr.bottom, WEEN_FACE);
        }
        /* A window frame is the plain EDGE_RAISED: its outer line is
         * COLOR_3DLIGHT (face), the white one sits inside it. */
        ween_classic_edge(s, 0, 0, wnd->w, wnd->h, EDGE_RAISED, BF_RECT, NULL);
        if (!ween_has_caption(wnd))
            return 0;
        /* caption gradient + title (bold, as Win2k captions were) + close */
        int frame = ween_frame_width(wnd);
        int cap = ween_caption_height(wnd);
        /* the gradient holds its end colours behind the icon and the
         * buttons; see ween_classic_caption */
        /* What the gradient holds its start colour across. Two past the
         * icon when there is one to draw: the machine's ramp starts there,
         * and starting it at the icon's edge puts every step two columns
         * early. A window that reserves the room without drawing anything
         * gets the room alone. */
        /* Room for the small icon. A window with a system menu keeps it
         * whether or not it has one of its own, because it would show the
         * default; a dialog has no system-menu icon at all, so its title
         * starts hard left and its gradient starts there — which is where
         * the machine's Folder Options has it. */
        /* A tool window's caption holds its title and its close box and
         * nothing else — no icon, and no room kept for one. */
        int shows_icon = !(wnd->ex_style & WS_EX_TOOLWINDOW) &&
                         (wnd->icon ||
                          ((wnd->style & WS_SYSMENU) && !wnd->is_dialog));
        int icon_w = shows_icon ? ween_ncm(WEEN_NC_SMICON) +
                                      (wnd->icon ? ween_ncm(2) : 0)
                                : 0;
        /* The gradient stops short of every caption button, not just the
         * close one: on the machine it has reached its end colour three
         * pixels before the leftmost of the three. A caption with only a
         * close box keeps a single button's width, which is what both
         * reference renders show and what the machine has not been measured
         * with. */
        /* The gradient stops short of the buttons. Where exactly depends on
         * which they are: before the group of minimise, maximise and close it
         * leaves two pixels of caption, and against a question mark it stops
         * at its very edge. Both are measured — the first off the reference
         * render, the second off the machine's Folder Options. */
        int nbtn = 1 + (nc_has_min(wnd) ? 1 : 0) + (nc_has_max(wnd) ? 1 : 0) +
                   (nc_has_help(wnd) ? 1 : 0);
        RECT lb = nc_button_rect(wnd, nbtn - 1);
        /* A tool window's ramp stops at the close box's very edge too: its
         * caption has no room to spare and the machine's leaves none. */
        int flush = nc_has_help(wnd) || (wnd->ex_style & WS_EX_TOOLWINDOW);
        int buttons_w =
            wnd->w - frame - (lb.left - (flush ? 0 : ween_ncm(2)));
        /* Whether this is the window the keyboard belongs to: the active one
         * itself, or something it owns — a palette floating over its own
         * window is drawn active with it, the way the machine draws one. */
        int active = wnd == g_active ||
                     (g_active && ween_top_level(g_active) == wnd);
        ween_classic_caption(s, frame, frame, wnd->w - 2 * frame, cap - 1,
                             icon_w, buttons_w, active);
        /* The gradient already holds its start colour across icon_w; this is
         * what goes there. A window without one keeps its title hard left,
         * which is where a window without a system menu wants it anyway. */
        int title_x = frame + ween_ncm(2);
        if (wnd->icon && icon_w) {
            struct ween_dc idc;
            int side = ween_ncm(16);
            memset(&idc, 0, sizeof(idc));
            idc.s = s;
            idc.clip_w = wnd->w;
            idc.clip_h = wnd->h;
            /* centred in the caption, the odd pixel going above it */
            /* centred in the caption, the odd pixel going below it — which
             * is where the machine's sits */
            DrawIconEx(&idc, frame + ween_ncm(2), frame + (cap - side) / 2,
                       wnd->icon, side, side, 0, NULL, DI_NORMAL);
            title_x = frame + ween_ncm(2) + side + ween_ncm(2);
        }
        const ween_strike *f = ween_gui_font_bold();
        if (f) {
            /* Centred in the caption, the odd pixel going below the
             * title rather than above it: a nineteen-pixel caption comes out
             * the same either way, a tool window's sixteen does not, and the
             * machine's sits low. */
            int ty = frame + (cap - (f->ascent - f->descent) + 1) / 2 - 1;
            ween_strike_draw(f, s, title_x, ty, wnd->text,
                             (int)strlen(wnd->text),
                             active ? WEEN_CAP_TEXT : WEEN_CAP_INACT_TEXT);
        }
        if (wnd->style & WS_SYSMENU) {
            struct ween_dc dc;
            RECT c;
            memset(&dc, 0, sizeof(dc));
            dc.s = s;
            dc.clip_w = wnd->w;
            dc.clip_h = wnd->h;
            c = nc_button_rect(wnd, 0);
            DrawFrameControl(&dc, &c, DFC_CAPTION,
                             DFCS_CAPTIONCLOSE |
                                 (wnd->nc_close_pressed ? DFCS_PUSHED : 0));
            if (nc_has_help(wnd)) {
                c = nc_button_rect(wnd, 1);
                DrawFrameControl(&dc, &c, DFC_CAPTION,
                                 DFCS_CAPTIONHELP |
                                     (wnd->nc_button_pressed == 1 ? DFCS_PUSHED
                                                                  : 0));
            }
            if (nc_has_max(wnd)) {
                c = nc_button_rect(wnd, 1);
                DrawFrameControl(&dc, &c, DFC_CAPTION,
                                 (nc_has_help(wnd)
                                      ? DFCS_CAPTIONHELP
                                      : wnd->maximized ? DFCS_CAPTIONRESTORE
                                                       : DFCS_CAPTIONMAX) |
                                     (wnd->nc_button_pressed == 1 ? DFCS_PUSHED
                                                                  : 0));
            }
            if (nc_has_min(wnd)) {
                c = nc_button_rect(wnd, 2);
                DrawFrameControl(&dc, &c, DFC_CAPTION,
                                 DFCS_CAPTIONMIN |
                                     (wnd->nc_button_pressed == 2 ? DFCS_PUSHED
                                                                  : 0));
            }
        }
        if (wnd->menu) { /* between caption and client */
            const ween_strike *mf = ween_gui_font();
            int bar_w = wnd->w - 2 * frame;
            ween_menu_layout_bar(wnd->menu, mf, bar_w);
            ween_menu_draw_bar(wnd->menu, s, frame, frame + cap, bar_w, mf,
                               wnd->menu_hot);
        }
        return 0;
    }

    case WM_SYSCOMMAND:
        switch (wp & 0xfff0) {
        case SC_KEYMENU:
            /* Alt, F10, or Alt and a letter: the keyboard is asking for the
             * menu. An application whose menu is not the frame's — a shell
             * keeps its in a rebar band — answers this itself and puts its
             * own bar under the keyboard; what is left here is the frame's
             * own menu bar. */
            /* zero means it was used, as a window proc says of anything it
             * has handled: the caller falls back to a control's own mnemonic
             * only when the menus wanted none of it */
            return ween_menu_key(wnd, lp ? 0 : VK_MENU, (unsigned)lp) ? 0 : 1;
        case SC_MAXIMIZE: {
            /* Fill the screen, remembering where to go back to. There is no
             * work area to ask about, so the screen metrics are the extent. */
            RECT r;
            GetWindowRect(wnd, &r);
            wnd->restore_rect = r;
            wnd->maximized = 1;
            MoveWindow(wnd, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                       GetSystemMetrics(SM_CYSCREEN), TRUE);
            return 0;
        }
        case SC_RESTORE: {
            RECT r = wnd->restore_rect;
            wnd->maximized = 0;
            if (r.right > r.left)
                MoveWindow(wnd, r.left, r.top, r.right - r.left,
                           r.bottom - r.top, TRUE);
            return 0;
        }
        case SC_MINIMIZE:
            /* Nothing to minimise into: there is no taskbar, and hiding the
             * window would strand it. The app hears the command and can do
             * what it likes with it. */
            return 0;
        case SC_CLOSE:
            SendMessageA(wnd, WM_CLOSE, 0, 0);
            return 0;
        default:
            return 0;
        }
    case WM_CLOSE:
        DestroyWindow(wnd);
        return 0;

    case WM_GETTEXT: {
        /* the window's own copy, which is what every window that keeps one
         * answers with; a control that keeps its text somewhere else -- a
         * combo box in its field -- handles this itself */
        char *out = (char *)lp;
        int max = (int)wp;
        int n;
        if (!out || max <= 0)
            return 0;
        n = (int)strlen(wnd->text);
        if (n >= max)
            n = max - 1;
        memcpy(out, wnd->text, (size_t)n);
        out[n] = 0;
        return n;
    }
    case WM_GETTEXTLENGTH:
        return (LRESULT)strlen(wnd->text);
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
    /* The line under a mnemonic keeps the same company a menu's does: out of
     * sight until Alt has been pressed. A control is not a menu, but the
     * state is one state — UISF_HIDEACCEL covers both. */
    if (!ween_menu_cues)
        fmt |= DT_HIDEPREFIX;
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
/* How wide a control's label draws: what is between the '&' markers, which
 * are what tells a letter to be underlined rather than characters to draw. */
static int label_width(const struct ween_wnd *w)
{
    const ween_strike *f = w->font ? w->font : ween_gui_font();
    int tw = 0;
    if (!f || !w->text)
        return 0;
    for (const char *p = w->text; *p; p++) {
        if (*p == '&') {
            if (p[1] == '&')
                p++; /* a literal one: drawn, so measured */
            else
                continue;
        }
        /* what the glyphs take, which is what the rectangle round them is
         * measured off: the machine's Properties page draws "Read-only" its
         * forty-nine wide and the focus rectangle one pixel either side */
        tw += ween_strike_char_advance(f, (unsigned char)*p);
    }
    return tw;
}

/* How tall a line of text is: the ascent, the descent, and the row that
 * separates one line from the next. It is what the machine centres a button's
 * label by — not the strike's own cell, which is a row taller than this in the
 * dialog face and a row shorter in the shell's, and either would put a label a
 * pixel out of where the machine has it. */
static int label_line(const ween_strike *f)
{
    return f ? f->ascent - f->descent + 1 : 14;
}

static int label_height(const struct ween_wnd *w)
{
    const ween_strike *f = w->font ? w->font : ween_gui_font();
    if (!f)
        return 13;
    return f->cell_h ? f->cell_h : f->ascent - f->descent;
}

/* Whether a push button wears the black ring. The one the template marked
 * wears it until the keyboard reaches another: in win32 the focus takes the
 * default with it, so a dialog whose focus is on a page's button draws the
 * ring there and not around OK — which is what the machine's Folder Options
 * shows. Enter follows the same rule; see ween_dialog_key. */
/* Whether a window is a push button — which has to be asked of its class as
 * well as its style, since BS_PUSHBUTTON is zero and every other control's
 * style has those bits clear too. */
static int is_push_button(const struct ween_wnd *w)
{
    UINT kind;
    if (!w || !w->cls || !w->cls->name || strcmp(w->cls->name, "BUTTON"))
        return 0;
    kind = button_type(w);
    return kind == BS_PUSHBUTTON || kind == BS_DEFPUSHBUTTON;
}

int ween_button_is_default(const struct ween_wnd *w)
{
    HWND focus = ween_focus_get();
    if (!is_push_button(w))
        return 0;
    if (focus == w)
        return 1;
    if (focus && is_push_button(focus) &&
        ween_top_level(focus) == ween_top_level((struct ween_wnd *)w))
        return 0; /* another button has it */
    return button_type(w) == BS_DEFPUSHBUTTON;
}

static void pb_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    RECT r = ps->rcPaint;
    if (ween_button_is_default(wnd)) {
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
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    int lh = label_line(f);
    if (wnd->pressed) {
        tr.left += 1;
        tr.top += 1;
        tr.right += 1;
        tr.bottom += 1;
    }
    /* The label sits in a line of its own, centred in the button: placed
     * rather than centred again, since a strike's cell is not the line. */
    tr.top += ((tr.bottom - tr.top) - lh) / 2;
    tr.bottom = tr.top + lh;
    button_label(wnd, dc, &tr, DT_CENTER | DT_SINGLELINE);
    if (ween_focus_get() == wnd && !(wnd->style & WS_DISABLED)) {
        /* Wine's PB_Paint: the focus rectangle is the face, inset past the
         * bevel — how a keyboard user sees where they are. */
        struct ween_wnd *top = ween_top_level(wnd);
        int ox, oy;
        ween_client_origin(wnd, &ox, &oy);
        int in = ween_button_is_default(wnd) ? 4 : 3;
        ween_surface_focus_rect_in(&top->surface, ox + in, oy + in,
                                   wnd->w - 2 * in, wnd->h - 2 * in,
                                   (ox + oy) & 1, WEEN_BLACK);
    }
}

/* Wine's CB_Paint: a 13px box (at 96 dpi) on the left, the label beside it
 * half a '0' further right. */
static void cb_paint(HWND wnd, HDC dc, const PAINTSTRUCT *ps)
{
    const ween_strike *f = wnd->font ? wnd->font : ween_gui_font();
    RECT client = ps->rcPaint, rbox = client, rtext = client;
    int box = MulDiv(12, ween_render_dpi(), 96) + 1;
    int offset = f ? ween_strike_char_advance(f, '0') / 2 : 3;
    /* An option button's circle is drawn one column inside its control and
     * on its top row; a tick box's box is drawn on the control's own corner.
     * Both labels start in the same place, measured from the control -- the
     * machine's Folder Options looked as though a tick box kept one more
     * column only because the circle had moved.
     *
     * Windows' own rectangles say so, in two dialogs at once. Asked with
     * GetWindowRect inside the guest (tools/vm/probe.c), Folder Options
     * General has its `Use Windows classic desktop' button at x=245 and its
     * circle's leftmost pixel is 246; Notepad's Find box has `&Up' at 307
     * and `&Down' at 347 -- dialog units 111 and 138 -- with circles at 308
     * and 348. The tick box in the same Find box is at 146 and its box's
     * leftmost pixel is 146. */
    int radio = button_type(wnd) == BS_RADIOBUTTON ||
                button_type(wnd) == BS_AUTORADIOBUTTON;
    int lh = label_line(f), delta;
    UINT flags;

    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));

    rtext.left += box + offset + 1;
    rbox.left += radio;
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
    button_label(wnd, dc, &rtext, DT_LEFT | DT_SINGLELINE);
    if (ween_focus_get() == wnd && !(wnd->style & WS_DISABLED)) {
        /* The rectangle goes round the label, not the box, and not round the
         * control either: the label's own rectangle with a pixel of margin
         * on every side. On a control sixteen pixels tall the two readings
         * agree, which is why the Folder Options pages could not tell them
         * apart; the machine's Find box can, because its option buttons are
         * twenty pixels tall and the rectangle round "Down" is sixteen —
         * rows 86 to 101 of a control that runs 84 to 103. The marker in
         * "Use Windows &classic desktop" is not measured: it is not drawn
         * either. */
        struct ween_wnd *top = ween_top_level(wnd);
        int tw = label_width(wnd);
        int fy = rtext.top - 1, fh = lh + 2;
        int ox, oy;
        /* ...and it stops at the control's own edge. A label's rectangle is
         * the text's height with a pixel round it, which is two rows more
         * than a thirteen-pixel option button has got: asked for those rows
         * the library drew them outside the control, where the page's own
         * paint took them straight back and left the two upright sides of a
         * rectangle with no top and no bottom -- 142 pixels of Folder
         * Options General. The machine's rectangle round "Use Windows
         * classic desktop" is the control's thirteen rows exactly, and the
         * one round Find's "Down" is sixteen inside a control of twenty. One
         * rule gives both: the label's rectangle, clipped to the control. */
        if (fy < client.top) {
            fh -= client.top - fy;
            fy = client.top;
        }
        if (fy + fh > client.bottom)
            fh = client.bottom - fy;
        ween_client_origin(wnd, &ox, &oy);
        ween_surface_focus_rect_in(&top->surface, ox + rtext.left - 1,
                                   oy + fy, tw + 2, fh,
                                   (ox + oy) & 1, WEEN_BLACK);
    }
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
    /* The label sits eight in, not seven: the machine's group boxes have it
     * there and wine's have it a pixel to the left. */
    r.left += 8;
    r.right -= 7;
    r.top -= 1;
    r.bottom += 1;
    r.left++; /* the DT_LEFT / DT_TOP nudge, as in CalcLabelRect */
    r.right++;
    r.top++;
    r.bottom = r.top + lh;
    r.right = r.left + tw;
    /* Two pixels of margin either side of the label and one below, so the
     * frame is erased: on the machine's Find box the top of the Direction
     * frame stops at 170 and starts again at 217 for a label whose ink runs
     * 174 to 214, which is the label's rectangle and two more each way. */
    r.left -= 2;
    r.right += 2;
    r.bottom++;
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    r.left += 2;
    r.right -= 2;
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
            ween_damage_all(wnd);
        }
        wnd->check = BST_CHECKED;
        radio_take_tabstop(wnd);
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
    case WM_LBUTTONDBLCLK:
        /* win32's BUTTON falls through to its press handling for all but the
         * owner-draw and radio styles, so clicking fast keeps working. */
        msg = WM_LBUTTONDOWN;
        return button_proc(wnd, msg, wp, lp);
    case BM_CLICK: /* the keyboard's way in, and what a mnemonic sends */
        if (!(wnd->style & WS_DISABLED)) {
            button_click(wnd);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_SPACE && !(wnd->style & WS_DISABLED)) {
            SendMessageA(wnd, BM_CLICK, 0, 0);
            return 0;
        }
        return DefWindowProcA(wnd, msg, wp, lp);
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
        /* the tab stop follows the selection, as it does when it is clicked */
        if (wnd->check == BST_CHECKED &&
            (button_type(wnd) == BS_AUTORADIOBUTTON ||
             button_type(wnd) == BS_RADIOBUTTON))
            radio_take_tabstop(wnd);
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
    case STM_SETICON: {
        /* The picture this one shows. A label with SS_ICON is how a dialog
         * puts a picture beside a group of things without drawing it itself
         * — and, being a control, it is painted after whatever it sits on. */
        HICON was = wnd->icon;
        wnd->icon = (HICON)wp;
        InvalidateRect(wnd, NULL, TRUE);
        return (LRESULT)(INT_PTR)was;
    }
    case STM_GETICON:
        return (LRESULT)(INT_PTR)wnd->icon;
    /* The same for a bitmap: a static that says SS_BITMAP draws what was
     * hung on it here rather than its text. */
    case STM_SETIMAGE: {
        HANDLE was = wnd->image;
        wnd->image = (HANDLE)lp;
        InvalidateRect(wnd, NULL, FALSE);
        (void)wp;
        return (LRESULT)(INT_PTR)was;
    }
    case STM_GETIMAGE:
        return (LRESULT)(INT_PTR)wnd->image;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT r;
        GetClientRect(wnd, &r);
        /* The low five bits are a type, not a set of flags. */
        DWORD kind = wnd->style & SS_TYPEMASK;
        switch (kind) {
        case SS_ICON:
            if (wnd->icon)
                DrawIconEx(dc, 0, 0, wnd->icon, r.right, r.bottom, 0, NULL,
                           DI_NORMAL);
            EndPaint(wnd, &ps);
            return 0;
        case SS_BITMAP:
            FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
            if (wnd->image) {
                BITMAP bm;
                HDC src = CreateCompatibleDC(dc);
                HGDIOBJ old = SelectObject(src, (HGDIOBJ)wnd->image);
                if (GetObjectA((HGDIOBJ)wnd->image, sizeof bm, &bm))
                    BitBlt(dc, 0, 0, bm.bmWidth, bm.bmHeight, src, 0, 0,
                           SRCCOPY);
                if (old)
                    SelectObject(src, old);
                DeleteDC(src);
            }
            EndPaint(wnd, &ps);
            return 0;
        /* A rule, a column or a frame of them: an etched edge and no text at
         * all. What a dialog rules a section off with. */
        case SS_ETCHEDHORZ:
        case SS_ETCHEDVERT:
        case SS_ETCHEDFRAME:
            /* A line is a frame two pixels thick, not an edge on its own:
             * that is what puts the highlight round the far end of it, which
             * is where the machine's Properties page has it. */
            if (kind == SS_ETCHEDHORZ)
                r.bottom = r.top + 2;
            else if (kind == SS_ETCHEDVERT)
                r.right = r.left + 2;
            DrawEdge(dc, &r, EDGE_ETCHED, BF_RECT);
            EndPaint(wnd, &ps);
            return 0;
        case SS_BLACKRECT:
        case SS_GRAYRECT:
        case SS_WHITERECT: {
            int c = kind == SS_BLACKRECT  ? COLOR_WINDOWFRAME
                    : kind == SS_GRAYRECT ? COLOR_BTNSHADOW
                                          : COLOR_WINDOW;
            FillRect(dc, &r, GetSysColorBrush(c));
            EndPaint(wnd, &ps);
            return 0;
        }
        default:
            break;
        }
        FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        /* A label wraps at its own width unless it was made not to, which is
         * what SS_LEFT means in win32 and what a paragraph in a dialog needs.
         * SS_SIMPLE and SS_LEFTNOWORDWRAP are the ones that stay on one
         * line. */
        UINT fmt = (kind == SS_SIMPLE || kind == SS_LEFTNOWORDWRAP)
                       ? DT_SINGLELINE
                       : DT_WORDBREAK;
        if (wnd->style & SS_NOPREFIX) /* an ampersand that means one */
            fmt |= DT_NOPREFIX;
        if (!ween_menu_cues) /* as a control's mnemonic hides, so does a label's */
            fmt |= DT_HIDEPREFIX;
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
        /* Laid out in the whole label, not in whatever part of it was
         * damaged: where a line breaks is a property of the control's width,
         * so a repaint of half of it must not rewrap the words. */
        DrawTextA(dc, wnd->text, -1, &r, fmt);
        EndPaint(wnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}
