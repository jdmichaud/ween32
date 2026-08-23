/* ween32 internals: the software engine (surface, classic chrome, fonts) and
 * the window/backend model that the public win32-shaped API is built on.
 * Nothing in this header is public; apps see only include/ween32.h.
 *
 * Surface pixels are 0x00RRGGBB (one uint32_t per pixel) — the layout X11
 * ZPixmap and Win32 DIBs both accept on little-endian without conversion.
 * Note this differs from COLORREF (0x00BBGGRR); gdi.c converts at the API
 * boundary.
 */

#ifndef WEEN_INTERNAL_H
#define WEEN_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "../include/ween32.h"

/* ---- surface (from telemouse framebuffer.zig) -------------------------- */

typedef uint32_t ween_color; /* 0x00RRGGBB */

typedef struct {
    ween_color *px;
    int w, h;
    /* the clip rectangle every primitive draws through */
    int clip_x, clip_y, clip_r, clip_b;
} ween_surface;

#define WEEN_RGBX(r, g, b) ((ween_color)(((r) << 16) | ((g) << 8) | (b)))

/* Authentic Wine GetSysColor defaults (Win2000 scheme), in surface format. */
#define WEEN_FACE WEEN_RGBX(212, 208, 200)
#define WEEN_WHITE WEEN_RGBX(255, 255, 255)
#define WEEN_3DLIGHT WEEN_RGBX(212, 208, 200) /* == face in the Win2k scheme */
#define WEEN_SHADOW WEEN_RGBX(128, 128, 128)
#define WEEN_DKSHADOW WEEN_RGBX(64, 64, 64)
#define WEEN_BLACK WEEN_RGBX(0, 0, 0)
#define WEEN_WINDOWBG WEEN_RGBX(255, 255, 255)
#define WEEN_CAP_LEFT WEEN_RGBX(10, 36, 106)   /* #0A246A */
#define WEEN_CAP_RIGHT WEEN_RGBX(166, 202, 240) /* #A6CAF0 */
#define WEEN_CAP_TEXT WEEN_RGBX(255, 255, 255)

int ween_surface_init(ween_surface *s, int w, int h);
int ween_surface_resize(ween_surface *s, int w, int h);
void ween_surface_free(ween_surface *s);
void ween_surface_clear(ween_surface *s, ween_color c);
/* Restrict drawing to a rectangle; window painting sets this per window. */
void ween_surface_clip(ween_surface *s, int x, int y, int w, int h);
void ween_surface_get_clip(const ween_surface *s, RECT *r);
void ween_surface_pixel(ween_surface *s, int x, int y, ween_color c);
void ween_surface_fill(ween_surface *s, int x, int y, int w, int h, ween_color c);
void ween_surface_hline(ween_surface *s, int x, int y, int w, ween_color c);
void ween_surface_vline(ween_surface *s, int x, int y, int h, ween_color c);
void ween_surface_rect(ween_surface *s, int x, int y, int w, int h, ween_color c);
/* DrawFocusRect's dotted, inverting rectangle. */
void ween_surface_focus_rect(ween_surface *s, int x, int y, int w, int h);
/* 24-bit uncompressed BMP, for headless render verification. */
int ween_surface_write_bmp(const ween_surface *s, const char *path);
/* Nearest-neighbour integer magnification (dst must be src * zoom). */
void ween_surface_zoom_into(ween_surface *dst, const ween_surface *src, int zoom);

/* ---- classic chrome (from classic.zig; the Wine DrawEdge algorithm) ---- */

/* Wine's DrawEdge, faithfully: any BDR_ or EDGE_ type with any BF_ flags.
 * Fills *inner with the interior rect when BF_ADJUST is set. */
int ween_classic_edge(ween_surface *s, int x, int y, int w, int h,
                      unsigned type, unsigned flags, RECT *inner);
/* Shorthand for the button edge (EDGE_RAISED/SUNKEN | BF_RECT | BF_SOFT). */
void ween_classic_bevel(ween_surface *s, int x, int y, int w, int h, int sunken);
void ween_classic_caption(ween_surface *s, int x, int y, int w, int h,
                          int icon_w, int buttons_w);
#define WEEN_NC_SMICON 16 /* SM_CXSMICON: the caption icon, and its gradient stop */
/* DrawFrameControl's DFC_BUTTON glyphs (DFCS_* flags as in the SDK). */
void ween_classic_check(ween_surface *s, int x, int y, int w, int h, unsigned flags);
/* Scroll-bar parts: the dithered track, and one arrow button (dir: 0 up,
 * 1 down, 2 left, 3 right). */
void ween_classic_check_dither(ween_surface *s, int x, int y, int w, int h);
void ween_classic_scroll_track(ween_surface *s, int x, int y, int w, int h);
void ween_classic_sizegrip(ween_surface *s, int x1, int y1);
void ween_classic_menu_arrow(ween_surface *s, int x, int y, int h, ween_color c);
void ween_classic_menu_bullet(ween_surface *s, int x, int y, ween_color c);
void ween_classic_menu_check(ween_surface *s, int x, int y, ween_color c);
void ween_classic_sort_arrow(ween_surface *s, int x, int y, int up);
/* The 32x32 picture a message box puts beside its message. */
enum {
    WEEN_MB_ICON_ERROR = 1,
    WEEN_MB_ICON_QUESTION,
    WEEN_MB_ICON_WARNING,
    WEEN_MB_ICON_INFO
};
void ween_classic_msgbox_icon(ween_surface *s, int x, int y, unsigned which);
void ween_classic_arrow_down(ween_surface *s, int x, int y, int w,
                             ween_color c);
void ween_classic_checkmark(ween_surface *s, int x, int y, int w, int h,
                            ween_color c);
void ween_classic_scroll_arrow(ween_surface *s, int x, int y, int w, int h,
                               int dir, int inactive, int pushed);
void ween_classic_radio(ween_surface *s, int x, int y, int w, int h, unsigned flags);

/* ---- fonts -------------------------------------------------------------- */

/* Tahoma text from the font's embedded 11px bitmap strikes (EBLC/EBDT): the
 * same hand-tuned 1-bit glyphs GDI showed, no rasteriser (from font.zig). */
typedef struct {
    const unsigned char *ttf;
    size_t len;
    size_t cmap4; /* format-4 cmap subtable offset */
    size_t ebdt;
    size_t isa; /* strike's indexSubTableArray (absolute) */
    size_t nidx;
    int ascent;
    int descent;  /* negative, as in the font */
    int embolden; /* synthetic bold: overstrike 1px (weak bold strikes) */
    /* the logical (outline) metrics GDI reports, as opposed to the strike's */
    int cell_h;   /* the cell labels are centred within (tmHeight) */
    int ppem;
    size_t hmtx;
    int nhmtx;
    int upem;
} ween_strike;

int ween_strike_init(ween_strike *f, const unsigned char *ttf, size_t len, int ppem);
int ween_strike_text_width(const ween_strike *f, const char *s, int len);
int ween_strike_char_advance(const ween_strike *f, unsigned char c);
/* What GDI would *report* for a character or string — outline advances,
 * rounded up; wider than the strike actually draws. */
int ween_strike_char_extent(const ween_strike *f, unsigned char c);
uint32_t ween_strike_char_units(const ween_strike *f, unsigned char c);
int ween_strike_text_extent(const ween_strike *f, const char *s, int len);
/* y is the top of the text cell (TA_TOP); baseline = y + ascent. */
void ween_strike_draw(const ween_strike *f, ween_surface *s, int x, int y,
                      const char *text, int len, ween_color color);
/* The pen offset of a character, for placing a caret or hit-testing a click. */
int ween_strike_pen(const ween_strike *f, const char *text, int index);

/* Marlett caption glyphs from glyf outlines, even-odd scanline fill (from
 * marlett.zig). code: 0x72 close, 0x30 min, 0x31 max, 0x32 restore. */
typedef struct {
    const unsigned char *ttf;
    size_t len;
} ween_marlett;

int ween_marlett_init(ween_marlett *m, const unsigned char *ttf, size_t len);
void ween_marlett_draw(const ween_marlett *m, ween_surface *s, int code,
                       int x, int y, int size, ween_color color);

/* The built-in fonts (Wine's redistributable Tahoma/Marlett, embedded). */
const ween_strike *ween_gui_font(void);      /* Tahoma 11px — DEFAULT_GUI_FONT */
const ween_strike *ween_gui_font_bold(void); /* Tahoma Bold 11px */
const ween_marlett *ween_caption_font(void);

/* ---- GDI objects and device contexts ------------------------------------ */

typedef struct ween_gdiobj {
    enum { WEEN_OBJ_BRUSH, WEEN_OBJ_FONT, WEEN_OBJ_BITMAP, WEEN_OBJ_ICON } kind;
    ween_color color;         /* brush fill (surface format) */
    const ween_strike *font;  /* font strike */
    ween_surface bitmap;      /* WEEN_OBJ_BITMAP/ICON: the pixels */
    unsigned char *mask;      /* WEEN_OBJ_ICON: 1 where a pixel is drawn */
    int is_static;            /* stock/system object: DeleteObject is a no-op */
} ween_gdiobj;

struct ween_dc {
    ween_surface *s;
    int org_x, org_y;   /* window origin within the surface */
    int clip_w, clip_h; /* drawing area (window size) */
    ween_color text_color;
    int bk_mode;
    const ween_strike *font;    /* the strike drawing uses, from font_obj */
    struct ween_gdiobj *font_obj;  /* what SelectObject was handed, so the */
    struct ween_gdiobj *brush_obj; /* previous one can be given back */
    /* The font a fresh DC comes with, as an object, so that restoring the
     * "previous" one puts back what was really there. */
    struct ween_gdiobj initial_font;
};

/* ---- the letterbox --------------------------------------------------------
 *
 * A window and the buffer shown in it need not be the same size. A window
 * manager can impose a size the window never asked for — a tiling one always
 * does — and a window that has declared itself fixed keeps drawing at its own
 * size regardless. What is drawn is then centred in what the window system
 * gave us, and the pointer has to come back through exactly that offset, or a
 * click lands somewhere other than where it looks like it landed.
 *
 * Both halves live here rather than in a backend so that they cannot drift
 * apart, which is precisely what went wrong when they were separate: drawing
 * used the buffer's size and the pointer used a size that had gone stale. */
typedef struct {
    int win_w, win_h;     /* what the window system gave us, in device pixels */
    int shown_w, shown_h; /* the buffer last presented into it */
} ween_letterbox;

void ween_letterbox_window(ween_letterbox *lb, int w, int h);
void ween_letterbox_shown(ween_letterbox *lb, int w, int h);
void ween_letterbox_origin(const ween_letterbox *lb, int *ox, int *oy);
/* Window coordinates to surface coordinates, through the offset and the zoom.
 * The inverse of where ween_letterbox_origin says the buffer was drawn. */
void ween_letterbox_to_surface(const ween_letterbox *lb, int zoom, int *x,
                               int *y);

/* ---- image lists ---------------------------------------------------------- */

int ween_imagelist_reserve(HIMAGELIST il, int count);
void ween_imagelist_draw_blend(HIMAGELIST il, int index, ween_surface *s,
                               int x, int y, ween_color c);
void ween_imagelist_draw_mono(HIMAGELIST il, int index, ween_surface *s, int x,
                              int y, ween_color c);
void ween_imagelist_draw(HIMAGELIST il, int index, ween_surface *s, int x,
                         int y);
ween_color ween_cr_to_px(COLORREF c);

/* ---- menus ---------------------------------------------------------------
 *
 * The item rectangles are filled in by the layout pass and then used by both
 * the drawing and the hit-testing, so the two can never disagree. */
/* The underlines under the letters a menu answers to. Windows 2000 keeps them
 * hidden until the keyboard has been used to reach a menu, and shows them from
 * then on; a menu opened and worked with the mouse never grows them. */
extern int ween_menu_cues;

/* And the dotted rectangle round the thing the keyboard would act on. This
 * one starts shown — a folder opens with it on the first item — and a click
 * in a list puts it away, an arrow key brings it back. */
extern int ween_ui_focus_cues;


typedef struct ween_menuitem {
    char *text;   /* NULL for a separator; "label\taccelerator" otherwise */
    UINT id;
    UINT flags;   /* MF_* */
    HMENU popup;  /* the submenu, for MF_POPUP */
    HBITMAP bmp;  /* drawn in the gutter, from SetMenuItemBitmaps */
    int x, y, w, h;
} ween_menuitem;

/* Where a list view is scrolled to. The control's own state is private to
 * controls.c; this is the part of it anything outside needs to see. */
typedef struct {
    int top;     /* the first row drawn */
    int sel;     /* 1-based selected row, 0 for none */
    int count;   /* rows in the list */
    int visible; /* rows that fit */
    int max_top; /* the furthest it can scroll */
} ween_lv_view;

void ween_listview_view(HWND w, ween_lv_view *out);

int ween_menu_bar_height(const struct ween_wnd *w);
int ween_menu_key(HWND top, unsigned vk, unsigned ch); /* Alt / Alt+letter */
int ween_menu_count(HMENU menu);
ween_menuitem *ween_menu_item(HMENU menu, int i);
void ween_menu_layout_bar(HMENU menu, const ween_strike *f, int width);
void ween_menu_popup_size(HMENU menu, const ween_strike *f, int *w, int *h);
int ween_menu_hit(HMENU menu, int x, int y);
int ween_menu_mnemonic(HMENU menu, unsigned ch);
void ween_menu_draw_bar(HMENU menu, ween_surface *s, int ox, int oy, int width,
                        const ween_strike *f, int hot);
void ween_menu_draw_popup(HMENU menu, ween_surface *s, const ween_strike *f,
                          int w, int h, int hot);
/* Runs the modal loop over an open drop-down. Returns the command chosen, or
 * 0 if it was dismissed. */
UINT ween_menu_track(HMENU menu, HWND owner, int screen_x, int screen_y);
/* The same, started from a window's menu bar, so the arrows can walk between
 * drop-downs. from_keyboard highlights the first item, as Alt does. */
UINT ween_menu_track_bar(HWND top, int index, int from_keyboard);

/* ---- windows ------------------------------------------------------------- */


typedef struct ween_class {
    UINT style;  /* CS_*: only CS_DBLCLKS is acted on */
    int cursor;  /* WEEN_CURSOR_*: the shape over a window of this class */
    char *name;
    WNDPROC proc;
    HBRUSH background;
    HICON icon;  /* drawn in the caption, at the left of the gradient */
    int in_use;
} ween_class;

struct ween_wnd {
    const ween_class *cls;
    WNDPROC proc; /* class proc (subclassing not in v1) */
    struct ween_wnd *parent;
    struct ween_wnd *first_child;
    struct ween_wnd *next_sibling;
    DWORD style;
    DWORD ex_style;
    int x, y, w, h; /* window rect; children: in parent CLIENT coordinates */
    char *text;    /* never NULL; grows to fit, see ween_wnd_set_text */
    int text_cap;  /* bytes allocated, including the terminator */
    UINT_PTR id; /* (HMENU) child id */
    const ween_strike *font;
    int visible;
    int track_leave; /* asked for WM_MOUSELEAVE (TrackMouseEvent) */
    int pressed; /* BUTTON down-state */
    UINT check;  /* BUTTON check state (BST_*) */
    int scroll_pos, scroll_page, scroll_min, scroll_max; /* SCROLLBAR */
    int drag_offset;   /* where a drag grabbed the thumb */
    int drag_vertical; /* which of a view's two bars is being dragged */
    int sb_repeat;     /* the SB_ code a held scroll-bar arrow is repeating */
    void *ctl;   /* per-class state, freed with the window */
    void (*ctl_free)(void *); /* how to free it; plain free() when NULL */
    int destroyed;

    /* dialog frame (created by CreateDialogIndirect) */
    DLGPROC dlgproc;
    int is_dialog;
    int msgbox_icon; /* WEEN_MB_ICON_*, for a message box that has one */
    int is_modal;          /* DialogBox is running a loop over it */
    int dlg_ended;         /* EndDialog was called: the modal loop stops */
    INT_PTR dlg_result;    /* and this is what DialogBox returns */
    UINT defid; /* default-command id, for Enter (DM_SETDEFID) */

    /* top-level only */
    int cursor_shown; /* WEEN_CURSOR_*: what the backend was last told */
    HICON icon;    /* the caption's, from the class or WM_SETICON */
    HMENU menu;    /* the menu bar, drawn above the client area */
    int menu_hot;  /* the bar item whose drop-down is open, -1 for none */
    struct ween_wnd *next_top; /* the process's top-level windows, newest first */
    ween_surface surface;
    void *backend_win;
    int dirty;
    int nc_close_pressed;   /* close-box tracking */
    int nc_button_pressed;  /* 1 maximize, 2 minimize, 0 none */
    int maximized;          /* drawn as restore, and SC_MAXIMIZE toggles it */
    RECT restore_rect;      /* where it goes back to */
};

/* Non-client metrics of a WS_CAPTION window at 96 dpi (classic Win2k popup
 * chrome, matching the validated win2k_popup_wine reference: 16x14 caption
 * buttons at y=6, 2px in from the frame). Scale through ween_ncm() for the
 * system dpi, like the classic SM_* system metrics did. */
#define WEEN_NC_FRAME 3       /* a fixed window's frame */
#define WEEN_NC_SIZEFRAME 4   /* WS_THICKFRAME: the sizing border */
/* Caption strip: 19px at 96 dpi, of which the gradient paints the top 18 and
 * the last row stays face-coloured — what win32 does for CaptionHeight=18. */
#define WEEN_NC_CAPTION 19
/* The menu bar: 19px at 96 dpi, measured off the wine reference — the caption
 * ends and the client area begins exactly that far apart. */
#define WEEN_NC_MENU 19
#define WEEN_NC_MENUCHECK 13 /* SM_CXMENUCHECK: the tick column in a popup */
#define WEEN_NC_BTN_W 16
#define WEEN_NC_BTN_H 14

/* ---- DPI ------------------------------------------------------------------
 * Classic win32 model: one system dpi; fonts are sized in points against it
 * and dialog-unit layout follows the font. Sourced from WEEN32_DPI (default
 * 96). Near-integer multiples >= 2x render at 96 dpi and pixel-double in the
 * backend (crisp); fractional scales pick the nearest font strike. */
int ween_render_dpi(void); /* dpi the renderer works at (96 when zooming) */
int ween_zoom(void);       /* integer backend magnification (1 = native) */
int ween_ncm(int base96);  /* scale a 96-dpi non-client metric */
/* "Xft.dpi: N" from an X resource-manager string; 0 if absent/invalid. */
int ween_parse_xft_dpi(const char *resources);
/* Ask the display for its dpi (Xft.dpi); 0 if no display / not compiled. */
int ween_x11_probe_dpi(void);

/* ---- controls (controls.c) ----------------------------------------------- */

int ween_ex_edge(const struct ween_wnd *w); /* field-border width, 0 if none */
void ween_paint_ex_edge(struct ween_wnd *w);
int ween_scroll_metric(void); /* SM_CXVSCROLL at the system dpi */
void ween_draw_scrollbar(ween_surface *s, int x, int y, int w, int h, int vert,
                         int enabled, int pos, int page, int min, int max);
void ween_register_controls(void);
/* A toolbar in menu mode, for the menu tracker: which bar has a drop-down up,
 * which of its buttons that is, whether the keyboard opened it, what is under
 * a point, and which button an arrow key walks to. */
HWND ween_toolbar_menu_bar(void);
int ween_toolbar_menu_item(void);
int ween_toolbar_menu_keyed(void);
void ween_toolbar_menu_switch(int index);
int ween_toolbar_menu_hit(HWND bar, int x, int y);
int ween_toolbar_menu_step(HWND bar, int from, int dir);
/* A control showing a drop-down paints it after everything else and gets
 * first refusal on the mouse: this is how a combo box's list escapes its
 * own client area without a second top-level window. */
void ween_popup_paint(void);
HWND ween_popup_hit(int x, int y);
void ween_controls_free(HWND w); /* per-class state, on destroy */

/* Window text. Grows to fit whatever is stored; both return 0 only if the
 * allocation failed, and leave the old text intact when they do. */
void ween_kill_timers_of(HWND w); /* a destroyed window's timers, on destroy */
void ween_dc_set_font(struct ween_dc *dc, const ween_strike *font);
int ween_wnd_set_text(struct ween_wnd *w, const char *text);
int ween_wnd_reserve_text(struct ween_wnd *w, int len); /* room for len + NUL */

/* The client origin of a window within its top-level surface. */
void ween_client_origin(HWND wnd, int *ox, int *oy);
HWND ween_top_level(HWND wnd);
int ween_frame_width(const struct ween_wnd *w); /* scaled, per style */
HWND ween_focus_get(void);
/* The next/previous focusable (WS_TABSTOP) child of `dlg`, wrapping. */
HWND ween_tab_next(HWND dlg, HWND cur, int forward);
HWND ween_mnemonic_target(HWND parent, unsigned ch); /* the '&' in a label */
HWND ween_radio_step(HWND cur, int forward);         /* arrows within a group */
/* Repaint the whole tree into the surface and present it, if dirty. */
void ween_flush_paint(void);

/* ---- backend contract ------------------------------------------------------
 * A backend owns the native window: it blits the finished surface and yields
 * raw input events. It never draws. */

typedef enum {
    WEEN_EV_NONE, /* nothing to report: a zeroed event, and an expired wait */
    WEEN_EV_EXPOSE,
    WEEN_EV_MOUSE_DOWN,
    WEEN_EV_MOUSE_UP,
    WEEN_EV_MOUSE_MOVE,
    WEEN_EV_KEY,   /* vk: translated virtual-key code */
    WEEN_EV_WHEEL,  /* button: +1 away from the user, -1 toward */
    WEEN_EV_RESIZE, /* x, y: the window's new size */
    WEEN_EV_CLOSE,
    WEEN_EV_TIME, /* x: virtual milliseconds elapsed (headless timer tests) */
    WEEN_EV_END /* event source exhausted (headless) / connection lost */
} ween_ev_kind;

#define WEEN_WIN_UNMANAGED 1u /* a menu: no decoration, no management */

/* The pointer shapes a backend must know. These are the classic set; there
 * are no custom cursors, because the window system's own are what the classic
 * shell used and what X can supply without a bitmap of our own. */
enum {
    WEEN_CURSOR_ARROW,
    WEEN_CURSOR_IBEAM,
    WEEN_CURSOR_WAIT,
    WEEN_CURSOR_CROSS,
    WEEN_CURSOR_SIZENWSE,
    WEEN_CURSOR_SIZENESW,
    WEEN_CURSOR_SIZEWE,
    WEEN_CURSOR_SIZENS,
    WEEN_CURSOR_SIZEALL,
    WEEN_CURSOR_HAND,
    WEEN_CURSOR_COUNT
};

typedef struct {
    ween_ev_kind kind;
    void *win;          /* the backend window it belongs to (NULL: any) */
    int x, y;           /* window coordinates */
    int x_root, y_root; /* desktop coordinates (caption drag) */
    int button;
    unsigned vk;
    unsigned ch;    /* the character the key produced, 0 for none */
    int shift;      /* Shift held, for back-tab and typing */
    int alt;        /* Alt held, which is what makes a mnemonic fire */
    int ctrl;       /* Ctrl held, for the clipboard shortcuts */
} ween_event;

typedef struct {
    /* x, y are the requested desktop position, or CW_USEDEFAULT to let the
     * backend place the window (it centres it, as a lone window wants).
     * WEEN_WIN_UNMANAGED asks for a window the window system places and sizes
     * exactly as told and never decorates — what a menu is. */
    void *(*open)(int x, int y, int w, int h, const char *title, unsigned flags);
    void (*present)(void *win, const ween_surface *s);
    void (*move_by)(void *win, int dx, int dy);
    /* Ask the window system for a new size, and say whether the user may
     * resize the window themselves. */
    void (*resize)(void *win, int w, int h);
    void (*set_resizable)(void *win, int resizable);
    /* The pointer's shape over this window, as one of WEEN_CURSOR_*. */
    void (*set_cursor)(void *win, int shape);
    /* Where the window's surface actually is on the desktop. A window
     * manager may put a window somewhere other than it asked to be — a
     * tiling one always does — so a window that wants to place something
     * beside itself, which is what a menu is, has to ask rather than assume.
     * May be NULL, and may fail; both leave *x and *y alone. */
    void (*origin)(void *win, int *x, int *y);
    /* Blocks until the next event on any window; the event says which one.
     * timeout_ms < 0 waits indefinitely; otherwise it gives up after that
     * long and returns WEEN_EV_NONE, which is how a timer gets to run. */
    ween_event (*next_event)(void *win, int timeout_ms);
    void (*close)(void *win);
} ween_backend;

/* Where a top-level window's client-area origin is on the desktop, for the
 * benefit of anything placed in desktop coordinates. Falls back to where the
 * window asked to be when the backend cannot say. */
void ween_window_origin(struct ween_wnd *top, int *x, int *y);

/* An expose arriving inside a nested loop belongs to the window it names. */
void ween_mark_exposed(const ween_event *ev);

/* Give an event back to the window under it, from a loop that has decided it
 * was not its own. It is dispatched by the message loop, not on the spot. */
void ween_replay_event(const ween_event *ev);

/* Alt has been pressed and the bar is waiting for a letter. */
int ween_menu_armed(void);
void ween_menu_disarm(void);
int ween_menu_armed_key(HWND top, unsigned vk);

extern const ween_backend *ween_active_backend; /* set before CreateWindowExA */
const ween_backend *ween_backend_x11(void);      /* NULL if not compiled in */
const ween_backend *ween_backend_headless(void);

/* headless test hooks */
void ween_headless_inject(ween_event ev);
/* Make the fake window system impose a size, as a tiling window manager does;
 * 0 goes back to giving each window the size it asked for. */
void ween_headless_set_window_size(int w, int h);
void ween_headless_set_window_origin(int x, int y);
void ween_headless_last_unmanaged_origin(int *x, int *y);
int ween_headless_cursor(void *backend_win); /* the shape last asked for */
void ween_headless_set_bmp_path(const char *path); /* written on present */
const ween_surface *ween_headless_surface(void);   /* last presented */

#endif /* WEEN_INTERNAL_H */
