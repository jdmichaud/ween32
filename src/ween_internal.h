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
    /* How many pixels the buffer really holds. A window being dragged by its
     * corner is resized sixty times a second, and asking the system for a new
     * four-megabyte block each time is most of what that costs. */
    long cap;
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
/* And the same three for a window that is not the active one: the grey ramp
 * a Windows 2000 caption goes to when the keyboard is somewhere else, with
 * its title in the face colour rather than white. */
#define WEEN_CAP_INACT_LEFT WEEN_RGBX(128, 128, 128)
#define WEEN_CAP_INACT_RIGHT WEEN_RGBX(192, 192, 192)
#define WEEN_CAP_INACT_TEXT WEEN_RGBX(212, 208, 200)

int ween_surface_init(ween_surface *s, int w, int h);
int ween_surface_resize(ween_surface *s, int w, int h);
void ween_surface_free(ween_surface *s);
void ween_surface_clear(ween_surface *s, ween_color c);
/* Restrict drawing to a rectangle; window painting sets this per window. */
void ween_surface_clip(ween_surface *s, int x, int y, int w, int h);
void ween_surface_get_clip(const ween_surface *s, RECT *r);
int ween_surface_clipped_out(const ween_surface *s, int x, int y, int w, int h);
void ween_surface_pixel(ween_surface *s, int x, int y, ween_color c);
void ween_surface_fill(ween_surface *s, int x, int y, int w, int h, ween_color c);
void ween_surface_hline(ween_surface *s, int x, int y, int w, ween_color c);
void ween_surface_vline(ween_surface *s, int x, int y, int h, ween_color c);
void ween_surface_rect(ween_surface *s, int x, int y, int w, int h, ween_color c);
/* DrawFocusRect's dotted, inverting rectangle. */
void ween_surface_focus_rect(ween_surface *s, int x, int y, int w, int h,
                             int phase);
/* Where a DC may draw, in surface coordinates: its window, narrowed by
 * whatever IntersectClipRect has been given. */
void ween_dc_clip_box(HDC dc, RECT *out);

/* A picture the file dialog draws, cut out of a capture of the machine's own
 * dialog by tools/refcapture/shellart.py. Opaque, background and all: the
 * background it was cut from is the one it is drawn on, which is what makes
 * it exact without anything having to be interpreted. */
typedef struct {
    int w, h;
    const ween_color *px;
} ween_shell_art;

enum {
    WEEN_ART_HISTORY,
    WEEN_ART_DESKTOP,
    WEEN_ART_DOCUMENTS,
    WEEN_ART_COMPUTER,
    WEEN_ART_NETWORK,
    WEEN_ART_TOOLBAR,
    WEEN_ART_LOOKIN,
    WEEN_ART_DOCUMENT16,
    /* the same places at sixteen pixels, out of the dropped "Look in" list,
     * with the plain folder and the drive the tree draws beside a path */
    WEEN_ART_HISTORY16,
    WEEN_ART_DESKTOP16,
    WEEN_ART_DOCUMENTS16,
    WEEN_ART_COMPUTER16,
    WEEN_ART_DRIVE16,
    WEEN_ART_FOLDER16,
    WEEN_ART_PICTURES16,
    WEEN_ART_NETDRIVE16,
    WEEN_ART_NETWORK16
};

const ween_shell_art *ween_shell_picture(int which);
/* The same dots drawn in one colour instead of inverting what is under them:
 * what a button's rectangle is, where a view's caret inverts. */
void ween_surface_focus_rect_in(ween_surface *s, int x, int y, int w, int h,
                                int phase, ween_color c);
/* 24-bit uncompressed BMP, for headless render verification. */
int ween_surface_write_bmp(const ween_surface *s, const char *path);
/* Nearest-neighbour integer magnification (dst must be src * zoom). */
void ween_surface_zoom_into(ween_surface *dst, const ween_surface *src, int zoom);
void ween_surface_zoom_rect(ween_surface *dst, const ween_surface *src,
                            int zoom, int x, int y, int w, int h);

/* ---- classic chrome (from classic.zig; the Wine DrawEdge algorithm) ---- */

/* Wine's DrawEdge, faithfully: any BDR_ or EDGE_ type with any BF_ flags.
 * Fills *inner with the interior rect when BF_ADJUST is set. */
int ween_classic_edge(ween_surface *s, int x, int y, int w, int h,
                      unsigned type, unsigned flags, RECT *inner);
/* Shorthand for the button edge (EDGE_RAISED/SUNKEN | BF_RECT | BF_SOFT). */
void ween_classic_bevel(ween_surface *s, int x, int y, int w, int h, int sunken);
void ween_classic_caption(ween_surface *s, int x, int y, int w, int h,
                          int icon_w, int buttons_w, int active);
#define WEEN_NC_SMICON 16 /* SM_CXSMICON: the caption icon, and its gradient stop */
/* DrawFrameControl's DFC_BUTTON glyphs (DFCS_* flags as in the SDK). */
void ween_classic_check(ween_surface *s, int x, int y, int w, int h, unsigned flags);
/* The flat one a list view puts before a row, which is thirteen square. */
void ween_classic_check_flat(ween_surface *s, int x, int y, int on);
/* Scroll-bar parts: the dithered track, and one arrow button (dir: 0 up,
 * 1 down, 2 left, 3 right). */
void ween_classic_check_dither(ween_surface *s, int x, int y, int w, int h);
void ween_classic_check_dither_at(ween_surface *s, int x, int y, int w, int h,
                                  int ox, int oy);
void ween_classic_scroll_track(ween_surface *s, int x, int y, int w, int h);
void ween_classic_sizegrip(ween_surface *s, int x1, int y1);
void ween_classic_sizegrip_size(ween_surface *s, int x1, int y1, int size,
                                int fill);

/* Whether a size grip is standing in this window's bottom-right corner, which
 * is how anything that scrolls knows to draw its bar short of it. */
int ween_corner_taken(HWND wnd);

/* Which top-level window the keyboard belongs to. Setting it is what
 * showing a window and closing a dialog both do. */
void ween_set_active(struct ween_wnd *w);
/* Run an already-created dialog modally until EndDialog answers it. */
INT_PTR ween_dialog_modal(HWND dlg, HWND owner, int reenable);
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
    /* A face that stands in for a bitmap-only font. GDI measures one of
     * those from its glyphs, not from an outline, so measuring and drawing
     * agree — which they do not for a scalable face like Tahoma. */
    int bitmap_only;
    /* the logical (outline) metrics GDI reports, as opposed to the strike's */
    int cell_h;   /* the cell labels are centred within (tmHeight) */
    int ppem;
    size_t hmtx;
    int nhmtx;
    int upem;
} ween_strike;

int ween_strike_init(ween_strike *f, const unsigned char *ttf, size_t len, int ppem);
int ween_strike_text_width(const ween_strike *f, const char *s, int len);
/* Text with the two styles the glyphs do not hold: slanted, and ruled under
 * on the row below the baseline. */
void ween_strike_draw_styled(const ween_strike *f, ween_surface *s, int x,
                             int y, const char *text, int len,
                             ween_color color, int italic, int underline);
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
const ween_strike *ween_gui_font(void);
/* What a dialog is lettered in, and how a face name is resolved to a strike. */
const ween_strike *ween_dialog_font(void);
const ween_strike *ween_font_by_face(const char *face);      /* Tahoma 11px — DEFAULT_GUI_FONT */

/* The faces this library has, which is `src/fonts.c`'s table and the only
 * list of them. `EnumFontFamiliesA` is the way out to a program. */
int ween_font_family_count(void);
const char *ween_font_family(int i);
const ween_strike *ween_gui_font_bold(void); /* Tahoma Bold 11px */
/* A face at a size, as CreateFont asks for one: the nearest strike that face
 * carries, and the bold cut when the weight says so. */
const ween_strike *ween_font_create(const char *face, int height, int weight);
const ween_marlett *ween_caption_font(void);

/* ---- GDI objects and device contexts ------------------------------------ */

typedef struct ween_gdiobj {
    enum { WEEN_OBJ_BRUSH, WEEN_OBJ_FONT, WEEN_OBJ_BITMAP, WEEN_OBJ_ICON,
           WEEN_OBJ_PEN } kind;
    ween_color color;         /* brush fill / pen colour (surface format) */
    const ween_strike *font;  /* font strike */
    /* The two a strike cannot carry: a slant and a rule under the line,
     * both of which GDI puts on at drawing time rather than in the glyphs. */
    int font_italic, font_underline;
    int font_weight; /* what was asked for, which GetTextMetrics reports back */
    ween_surface bitmap;      /* WEEN_OBJ_BITMAP/ICON: the pixels */
    unsigned char *mask;      /* WEEN_OBJ_ICON: 1 where a pixel is drawn */
    int pen_style, pen_width; /* WEEN_OBJ_PEN: PS_*, and its width in pixels */
    int is_null;              /* NULL_PEN / NULL_BRUSH: draws nothing */
    int is_static;            /* stock/system object: DeleteObject is a no-op */
} ween_gdiobj;

struct ween_dc {
    ween_surface *s;
    int org_x, org_y;   /* window origin within the surface */
    int clip_w, clip_h; /* drawing area (window size) */
    /* IntersectClipRect: what the caller has narrowed this DC to, in the
     * window's own coordinates. Not set on a fresh DC, which is what the ones
     * the library builds on the stack rely on. */
    int clip_rect_set;
    int clip_l, clip_t, clip_r, clip_b;
    ween_color text_color;
    int bk_mode;
    const ween_strike *font;    /* the strike drawing uses, from font_obj */
    struct ween_gdiobj *font_obj;  /* what SelectObject was handed, so the */
    struct ween_gdiobj *brush_obj; /* previous one can be given back */
    struct ween_gdiobj *pen_obj;   /* and the pen lines are drawn with */
    /* The font a fresh DC comes with, as an object, so that restoring the
     * "previous" one puts back what was really there. */
    struct ween_gdiobj initial_font;

    /* ---- the drawing half (draw.c) ----
     * Zero is a working default for every one of these, which is what lets
     * the few DCs the library builds on the stack keep working: rop2 0 is
     * read as R2_COPYPEN and stretch 0 as the default mode. */
    int cur_x, cur_y;   /* the current position: MoveToEx, LineTo */
    /* SetViewportOrgEx: where the caller's (0,0) lands in the window. A
     * view that scrolls its contents sets it and then draws in the
     * coordinates of what it is showing. Zero for every other DC. */
    int vp_x, vp_y;
    int rop2;           /* R2_*: how a pen combines with the destination */
    COLORREF bk_color;  /* the gap colour of a styled pen, and OPAQUE text */
    int stretch_mode;   /* SetStretchBltMode */
    int is_memory;      /* CreateCompatibleDC: the surface is a bitmap */
    int owns_surface;   /* a memory DC's own 1x1 bitmap, freed with it */
    struct ween_gdiobj *bitmap_obj; /* what is selected into a memory DC */
    struct ween_gdiobj default_bitmap; /* the 1x1 one it starts with */
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
extern int ween_kbd_used;

/* Say which modifiers are held, for code that injects a keystroke with
 * SendMessage rather than through the queue -- the tests and the script
 * driver, which are this library's input system. A posted message carries
 * its own; see g_qmods in user.c. */
void ween_set_modifiers(int shift, int ctrl, int alt);

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
/* The window a view shows a name too long for its room in, for a test to look
 * at; NULL until one has been needed. */
HWND ween_listview_tip(HWND w);
HWND ween_treeview_tip(HWND w);

int ween_menu_bar_height(const struct ween_wnd *w);
int ween_menu_key(HWND top, unsigned vk, unsigned ch); /* Alt / Alt+letter */
/* The menu bar's own keys -- Alt, F10, Alt+letter, and the arrows once it is
 * armed -- answered for any window rather than only inside IsDialogMessage. */
int ween_menu_keydown(HWND wnd, unsigned vk, LPARAM lp);
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


/* A cursor an application made itself, out of the AND and XOR masks win32
 * has carried since 16-bit Windows: AND clear means the pixel is drawn, XOR
 * says black or white, and both set means invert what is under it -- which
 * X cannot do, so it is drawn black. `argb` is the two masks resolved into
 * one picture, with alpha zero where nothing is drawn; `backend` is where
 * the backend keeps whatever it made of it. */
typedef struct ween_cursor {
    unsigned magic;
    int w, h, xhot, yhot;
    unsigned *argb;
    void *backend;
} ween_cursor;

#define WEEN_CURSOR_MAGIC 0x63757273u /* "curs" */

/* Whether a handle is one of those rather than a stock shape number. */
const ween_cursor *ween_cursor_of(void *handle);

/* The picture USER32 draws for one of the stock shapes, where ween32 has it:
 * the sizing arrows, which no window system's own cursor font has. Null for
 * the rest, and the backend falls back to whatever it has. */
const ween_cursor *ween_stock_cursor(int shape);

typedef struct ween_class {
    UINT style;  /* CS_*: only CS_DBLCLKS is acted on */
    int cursor;  /* WEEN_CURSOR_*: the shape over a window of this class */
    const ween_cursor *cursor_img; /* ...or a cursor the application made */
    /* The control draws WS_VSCROLL/WS_HSCROLL itself, inside its client
     * area, rather than leaving them to the window's non-client one: an
     * edit, a list box and the two views all do. Without this they would
     * wear both, and the client rectangle would lose the width twice. */
    int own_scroll;
    char *name;
    /* The menu a window of this class is given when it is made, by the name
     * the class was registered with -- lpszMenuName. The window owns what it
     * is handed, the way win32 has it: the menu goes when the window does. */
    char *menu_name;
    WNDPROC proc;
    HBRUSH background;
    HICON icon;  /* drawn in the caption, at the left of the gradient */
    int in_use;
} ween_class;

struct ween_wnd {
    const ween_class *cls;
    WNDPROC proc; /* what this window answers with: its class's to begin
                   * with, and whatever SetWindowLong(GWL_WNDPROC) put there
                   * after — which is how a control that hosts another one
                   * takes the keys it needs off it */
    struct ween_wnd *parent;
    /* For a window of its own: the one it was created against. win32 calls
     * that its owner -- a dialog's is the window that put it up -- and passes
     * it in the same argument a child's parent goes in. */
    struct ween_wnd *owner;
    struct ween_wnd *first_child;
    struct ween_wnd *next_sibling;
    DWORD style;
    DWORD ex_style;
    LONG_PTR userdata;      /* GWLP_USERDATA: a program's own, hung off a window */
    LRESULT dlg_msgresult;  /* DWLP_MSGRESULT: what a dialog's last message
                             * answered, since its procedure returns only
                             * whether it dealt with it */
    int dlg_msgresult_set;  /* and whether it said so at all, since saying
                             * "no" and saying nothing are different answers */
    LONG_PTR dlg_user;      /* DWLP_USER: a dialog's own slot, which win32
                             * keeps apart from GWLP_USERDATA */
    struct ween_wnd *dlg_placed_focus; /* what its WM_INITDIALOG put the
                                        * keyboard on itself, having answered
                                        * FALSE — which is a page of a
                                        * property sheet's way of saying where
                                        * it wants to be shown */
    struct ween_wnd *dlg_prev_focus; /* what had the keyboard before the box
                                      * came up, so that closing it puts the
                                      * keyboard back where the user left it */
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
    /* WS_HSCROLL/WS_VSCROLL: the bars a window wears in its own non-client
     * area, [0] horizontal and [1] vertical. A control that hosts SCROLLBAR
     * children uses the four fields above instead; these belong to the
     * window itself, which is what SetScrollInfo addresses. */
    struct {
        int pos, min, max, page;
        int hidden;   /* ShowScrollBar(FALSE) */
        int disabled; /* EnableScrollBar */
        int grab;     /* where in the thumb a drag took hold, -1 for none */
        int repeat;   /* the SB_ code a held arrow or track is repeating */
    } sb[2];
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
    const ween_cursor *cursor_img_shown; /* ...or the picture it was told */
    HICON icon;    /* the caption's, from the class or WM_SETICON */
    HANDLE image;  /* a static's picture, from STM_SETIMAGE */
    HMENU menu;    /* the menu bar, drawn above the client area */
    int menu_hot;  /* the bar item whose drop-down is open, -1 for none */
    struct ween_wnd *next_top; /* the process's top-level windows, newest first */
    ween_surface surface;
    void *backend_win;
    int dirty;
    /* What of the surface has to be painted again, in surface coordinates.
     * Only a top-level has one: it owns the surface its children draw into.
     * Everything that asks for a repaint adds its rectangle to this, and the
     * paint pass clips to it -- which is the difference between a stroke of
     * a pencil costing the pixels under it and costing the whole window. */
    RECT damage;
    int nc_close_pressed;   /* close-box tracking */
    int nc_button_pressed;  /* 1 maximize, 2 minimize, 0 none */
    int maximized;          /* drawn as restore, and SC_MAXIMIZE toggles it */
    /* DragAcceptFiles: whether this window would take files dropped on it.
       Nothing can drop on it yet -- the backend does not speak XDND -- so
       this is remembered and read by nobody but the call that set it. */
    int accepts_files;
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
/* A tool window's, which is what a palette floating over a drawing wears:
 * shorter, with a smaller close box and no other buttons. */
#define WEEN_NC_SMCAPTION 16
/* The menu bar: 19px at 96 dpi, measured off the wine reference — the caption
 * ends and the client area begins exactly that far apart. */
#define WEEN_NC_MENU 19
#define WEEN_NC_MENUCHECK 13 /* SM_CXMENUCHECK: the tick column in a popup */
#define WEEN_NC_BTN_W 16
#define WEEN_NC_BTN_H 14
#define WEEN_NC_SMBTN_W 10
#define WEEN_NC_SMBTN_H 11

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

/* What a window wears outside its client area: WS_EX_CLIENTEDGE's two,
 * WS_EX_STATICEDGE's one, and WS_BORDER's line on top of either. 0 for a
 * window with none of them, and for a captioned one, whose border is its
 * frame. */
int ween_border_width(const struct ween_wnd *w);
void ween_paint_border(struct ween_wnd *w);
/* ---- what a text control and a view share -------------------------------
 *
 * A list box, a tree, an edit or a rich edit owns its scroll bars rather
 * than hosting SCROLLBAR children, so each hit-tests its own with these.
 * `at` is the offset along the bar and the answer is the new position.
 *
 * The line functions are the other half. What a line is -- where it starts,
 * how long it is, which line an offset is on, with CRLF and a bare LF both
 * counting as one break -- is the same question for both text controls, and
 * it is where off-by-ones live, so there is one set of them rather than two
 * that can drift apart. The rich edit keeps a line table of its own for
 * drawing, built in one pass instead of a scan from the top per line, and
 * uses these to answer the messages that ask about a line; its test checks
 * the two against each other.
 */
typedef struct {
    int pos, min, max, page;
    int line; /* what an arrow click scrolls by */
} ween_sbstate;
int ween_sb_maxpos(const ween_sbstate *st);
int ween_sb_click(int at, int len, const ween_sbstate *st, int *grab);
int ween_sb_drag(int at, int len, const ween_sbstate *st, int grab);
int ween_sb_clamp(int pos, const ween_sbstate *st);
int ween_text_line_start(const char *text, int line);
int ween_text_line_from_char(const char *text, int at);
int ween_text_line_count(const char *text);
int ween_text_line_length(const char *text, int start);

/* Win2000's default caret blink rate, the one Control Panel's slider sits at
 * in the middle of; win32 apps read it with GetCaretBlinkTime. Both text
 * controls blink on the same timer id, which an application is unlikely to
 * pick for one of its own. */
#define WEEN_CARET_BLINK_MS 530
#define WEEN_CARET_TIMER 0x57454549

/* The rich edit's class, registered beside the other controls. */
void ween_register_richedit(void);
/* How many runs of formatting the document is in. Nothing in the library
 * asks: the test does, because coalescing a run with an identical neighbour
 * is a property with no other outward sign -- a document that splits and
 * never merges draws exactly the same and grows without bound. */
int ween_rich_run_count(HWND w);

int ween_scroll_metric(void); /* SM_CXVSCROLL at the system dpi */
/* Milliseconds on the library's own clock -- the headless backend's virtual
 * one when it is running, so a scripted run counts the same every time. A
 * control that has to tell a third click from a first needs it: win32 has no
 * triple-click message and the control counts the presses itself. */
unsigned long ween_now_ms(void);
void ween_draw_scrollbar(ween_surface *s, int x, int y, int w, int h, int vert,
                         int enabled, int pos, int page, int min, int max);
/* The bars a window wears itself (WS_HSCROLL/WS_VSCROLL): how much of the
 * window they take, drawing them, and the mouse landing in one. */
int ween_wnd_sb_shown(const struct ween_wnd *w, int vert);
void ween_wnd_sb_paint(struct ween_wnd *w);
/* x,y in surface coordinates; returns 1 if the point (and the message) was
 * the window's own scroll bar and has been dealt with. */
int ween_wnd_sb_mouse(struct ween_wnd *w, UINT msg, int x, int y);
int ween_wnd_sb_at(const struct ween_wnd *w, int x, int y);
int ween_wnd_sb_timer(struct ween_wnd *w); /* the held-arrow repeat */
#define WEEN_SB_TIMER_ID 0x5343524C /* the id that repeat runs on */
void ween_register_controls(void);
/* The registry, emptied: the next call reads its file again. Test-only, and
 * not part of the win32 API. */
void ween_registry_forget(void);
/* An icon out of the bytes one is stored as -- a BITMAPINFOHEADER with the
 * colours and the mask under it, which is what an RT_ICON holds. */
HICON ween_icon_from_bits(const unsigned char *bits, size_t len);
HBITMAP ween_bitmap_from_dib(const unsigned char *dib, size_t len);
/* Mark a class as drawing its own WS_*SCROLL bars (user.c). */
void ween_class_owns_scroll(LPCSTR name);
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
/* Where a combo box's dropped list is, in surface pixels — the tests reach
 * for it, since a list that stops at eight rows and can be dragged taller has
 * a size worth checking. */
void ween_combo_list_rect(HWND combo, RECT *out);
/* Where a combo box's dropped list is, in surface pixels — the tests reach
 * for it, since a list that stops at eight rows and can be dragged taller has
 * a size worth checking. */
void ween_combo_list_rect(HWND combo, RECT *out);
void ween_controls_free(HWND w); /* per-class state, on destroy */

/* Window text. Grows to fit whatever is stored; both return 0 only if the
 * allocation failed, and leave the old text intact when they do. */
void ween_kill_timers_of(HWND w); /* a destroyed window's timers, on destroy */
void ween_dc_set_font(struct ween_dc *dc, const ween_strike *font);
/* SelectObject's bitmap case, which only a memory DC has (draw.c). */
HGDIOBJ ween_select_bitmap(HDC dc, HGDIOBJ obj);
int ween_wnd_set_text(struct ween_wnd *w, const char *text);
int ween_wnd_reserve_text(struct ween_wnd *w, int len); /* room for len + NUL */

/* The client origin of a window within its top-level surface. */
void ween_client_origin(HWND wnd, int *ox, int *oy);
HWND ween_top_level(HWND wnd);
int ween_frame_width(const struct ween_wnd *w);
/* How tall this window's caption is: a tool window's is shorter. */
int ween_caption_height(const struct ween_wnd *w); /* scaled, per style */
/* Whether the window carries a caption — and so draws WS_BORDER as frame. */
int ween_has_caption(const struct ween_wnd *w);
/* Whether a push button draws the default ring: the keyboard takes it. */
int ween_button_is_default(const struct ween_wnd *w);
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

/* Undo a quit that came from a drained injection queue, and **only** that
 * one -- `PostQuitMessage` and the last window closing set the same flag and
 * must survive. Called when an event is injected into a program whose queue
 * had already run dry, which is what driving one a gesture at a time does. */
void ween_unquit_scripted(void);

#define WEEN_WIN_UNMANAGED 1u /* a menu: no decoration, no management */

/* The pointer shapes a backend must know: the classic set, which the window
 * system supplies. An application with cursor art of its own hands over a
 * ween_cursor instead; see below. */
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
    /* Put the finished frame on the screen. `damage` is the part of it that
     * changed, in surface coordinates, which is all a backend need copy —
     * null means the whole of it. */
    void (*present)(void *win, const ween_surface *s, const RECT *damage);
    void (*move_by)(void *win, int dx, int dy);
    /* Ask the window system for a new size, and say whether the user may
     * resize the window themselves. */
    void (*resize)(void *win, int w, int h);
    /* Whether asking is answered: a display server always says what it
     * actually gave, in a WEEN_EV_RESIZE, and what it gave may not be what
     * was asked for. A backend that does not answer leaves the caller to
     * take its own word for the new size. */
    int resize_is_answered;
    void (*set_resizable)(void *win, int resizable);
    /* Put the window on the screen or take it off. A window is opened off
     * the screen and only appears when it is shown, which is what lets one
     * be made ready and kept back — a menu, a box of suggestions. May be
     * NULL, in which case the window is on the screen from the moment it is
     * opened. */
    void (*show)(void *win, int on);
    /* The pointer's shape over this window, as one of WEEN_CURSOR_*, or a
     * picture of its own when `custom` is not null -- in which case `shape`
     * is what to fall back to. */
    void (*set_cursor)(void *win, int shape, const ween_cursor *custom);
    /* Say that this window is not to be given the keyboard when it appears.
     * A palette floated over the window being worked in is put up this way:
     * the window manager decides who has the keyboard, and one that is not
     * told will hand it to whatever it has just put on the screen -- and the
     * typing that was meant for the picture underneath goes into the palette
     * instead. May be NULL. */
    void (*no_activate)(void *win);
    /* Whose window this one belongs to, and whether it is a dialog. A
     * window manager that is told neither has no reason to treat a modal box
     * differently from an application's main window: a tiling one gives it a
     * tile of the screen, which is how a dialog comes up full screen. */
    void (*set_owner)(void *win, void *owner, int dialog);
    /* How big the screen is, in the pixels a window is measured in. A
     * backend without one leaves the numbers alone, and the classic default
     * stands — which is what keeps a headless render the same everywhere. */
    void (*screen_size)(int *w, int *h);
    /* Where the window's surface actually is on the desktop. A window
     * manager may put a window somewhere other than it asked to be — a
     * tiling one always does — so a window that wants to place something
     * beside itself, which is what a menu is, has to ask rather than assume.
     * May be NULL, and may fail; both leave *x and *y alone. */
    void (*origin)(void *win, int *x, int *y);
    /* Ask the window system to put this window in front of the others.
     *
     * **It is a request and nothing here can promise it.** On X11 the window
     * manager may reorder, refuse, or prevent a focus steal, and
     * `_NET_ACTIVE_WINDOW` is advisory; so ween32's contract cannot be "this
     * window is in front". It is *"this window is in front in ween32's own
     * order, and the backend has been asked"* -- and a program can observe
     * the first through GetWindow and EnumWindows whether or not the screen
     * agrees. May be NULL, in which case only ween32's own order moves. */
    void (*raise)(void *win);
    /* Blocks until the next event on any window; the event says which one.
     * timeout_ms < 0 waits indefinitely; otherwise it gives up after that
     * long and returns WEEN_EV_NONE, which is how a timer gets to run. */
    ween_event (*next_event)(void *win, int timeout_ms);
    void (*close)(void *win);
} ween_backend;

/* What of a window has to be painted again: a rectangle of the surface, or
 * the whole of it. Both add to whatever was already waiting. */
void ween_damage_rect(struct ween_wnd *w, int x, int y, int cx, int cy);
void ween_damage_all(struct ween_wnd *w);

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
unsigned ween_x11_keysym_to_vk(unsigned long keysym); /* the key table */
const ween_backend *ween_backend_headless(void);

/* headless test hooks */
void ween_headless_inject(ween_event ev);
/* Make the fake window system impose a size, as a tiling window manager does;
 * 0 goes back to giving each window the size it asked for. */
void ween_headless_set_window_size(int w, int h);
void ween_headless_set_window_origin(int x, int y);
void ween_headless_last_unmanaged_origin(int *x, int *y);
int ween_headless_cursor(void *backend_win); /* the shape last asked for */
int ween_headless_window_shown(void *backend_win); /* on the screen, or kept back */
/* When this window was last raised, counting from one. Zero for a window
 * never raised; the highest is the one the fake window system was asked for
 * last, which is what a test asks about instead of looking at a screen. */
int ween_headless_window_raised(void *backend_win);
void ween_headless_set_bmp_path(const char *path); /* written on present */
const ween_surface *ween_headless_surface(void);   /* last presented */

#endif /* WEEN_INTERNAL_H */
