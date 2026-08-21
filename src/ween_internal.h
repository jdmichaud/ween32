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
void ween_classic_scroll_track(ween_surface *s, int x, int y, int w, int h);
void ween_classic_sizegrip(ween_surface *s, int x, int y, int w, int h);
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
int ween_strike_text_extent(const ween_strike *f, const char *s, int len);
/* y is the top of the text cell (TA_TOP); baseline = y + ascent. */
void ween_strike_draw(const ween_strike *f, ween_surface *s, int x, int y,
                      const char *text, int len, ween_color color);
/* As above, but stepping by the reported advances — how EDIT spaces text. */
void ween_strike_draw_logical(const ween_strike *f, ween_surface *s, int x,
                              int y, const char *text, int len, ween_color color);
int ween_strike_logical_pen(const ween_strike *f, const char *text, int len,
                            int index);

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
    enum { WEEN_OBJ_BRUSH, WEEN_OBJ_FONT } kind;
    ween_color color;         /* brush fill (surface format) */
    const ween_strike *font;  /* font strike */
    int is_static;            /* stock/system object: DeleteObject is a no-op */
} ween_gdiobj;

struct ween_dc {
    ween_surface *s;
    int org_x, org_y;   /* window origin within the surface */
    int clip_w, clip_h; /* drawing area (window size) */
    ween_color text_color;
    int bk_mode;
    const ween_strike *font;
};

/* ---- windows ------------------------------------------------------------- */

#define WEEN_MAX_CLASSES 32
#define WEEN_MAX_TEXT 128

typedef struct ween_class {
    char name[32];
    WNDPROC proc;
    HBRUSH background;
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
    char text[WEEN_MAX_TEXT];
    UINT_PTR id; /* (HMENU) child id */
    const ween_strike *font;
    int visible;
    int pressed; /* BUTTON down-state */
    UINT check;  /* BUTTON check state (BST_*) */
    int scroll_pos, scroll_page, scroll_min, scroll_max; /* SCROLLBAR */
    int drag_offset; /* where a drag grabbed the thumb */
    void *ctl;   /* per-class state, freed with the window */
    int destroyed;

    /* dialog frame (created by CreateDialogIndirect) */
    DLGPROC dlgproc;
    int is_dialog;
    UINT defid; /* default-command id, for Enter (DM_SETDEFID) */

    /* top-level only */
    ween_surface surface;
    void *backend_win;
    int dirty;
    int nc_close_pressed; /* close-box tracking */
};

/* Non-client metrics of a WS_CAPTION window at 96 dpi (classic Win2k popup
 * chrome, matching the validated win2k_popup_wine reference: 16x14 caption
 * buttons at y=6, 2px in from the frame). Scale through ween_ncm() for the
 * system dpi, like the classic SM_* system metrics did. */
#define WEEN_NC_FRAME 3
/* Caption strip: 19px at 96 dpi, of which the gradient paints the top 18 and
 * the last row stays face-coloured — what win32 does for CaptionHeight=18. */
#define WEEN_NC_CAPTION 19
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
/* A control showing a drop-down paints it after everything else and gets
 * first refusal on the mouse: this is how a combo box's list escapes its
 * own client area without a second top-level window. */
void ween_popup_paint(void);
HWND ween_popup_hit(int x, int y);
void ween_controls_free(HWND w); /* per-class state, on destroy */

/* The client origin of a window within its top-level surface. */
void ween_client_origin(HWND wnd, int *ox, int *oy);
HWND ween_top_level(HWND wnd);
HWND ween_focus_get(void);
/* The next/previous focusable (WS_TABSTOP) child of `dlg`, wrapping. */
HWND ween_tab_next(HWND dlg, HWND cur, int forward);
/* Repaint the whole tree into the surface and present it, if dirty. */
void ween_flush_paint(void);

/* ---- backend contract ------------------------------------------------------
 * A backend owns the native window: it blits the finished surface and yields
 * raw input events. It never draws. */

typedef enum {
    WEEN_EV_NONE,
    WEEN_EV_EXPOSE,
    WEEN_EV_MOUSE_DOWN,
    WEEN_EV_MOUSE_UP,
    WEEN_EV_MOUSE_MOVE,
    WEEN_EV_KEY, /* vk: translated virtual-key code */
    WEEN_EV_CLOSE,
    WEEN_EV_END /* event source exhausted (headless) / connection lost */
} ween_ev_kind;

typedef struct {
    ween_ev_kind kind;
    int x, y;           /* window coordinates */
    int x_root, y_root; /* desktop coordinates (caption drag) */
    int button;
    unsigned vk;
    unsigned ch;    /* the character the key produced, 0 for none */
    int shift;      /* Shift held, for back-tab and typing */
} ween_event;

typedef struct {
    void *(*open)(int w, int h, const char *title);
    void (*present)(void *win, const ween_surface *s);
    void (*move_by)(void *win, int dx, int dy);
    /* Blocks until the next event. */
    ween_event (*next_event)(void *win);
    void (*close)(void *win);
} ween_backend;

extern const ween_backend *ween_active_backend; /* set before CreateWindowExA */
const ween_backend *ween_backend_x11(void);      /* NULL if not compiled in */
const ween_backend *ween_backend_headless(void);

/* headless test hooks */
void ween_headless_inject(ween_event ev);
void ween_headless_set_bmp_path(const char *path); /* written on present */
const ween_surface *ween_headless_surface(void);   /* last presented */

#endif /* WEEN_INTERNAL_H */
