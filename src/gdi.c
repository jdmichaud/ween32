/* The GDI32-shaped drawing API over the software engine.
 *
 * COLORREF is 0x00BBGGRR (as on Windows); the surface stores 0x00RRGGBB, so
 * every color crosses cr_to_px()/px_to_cr() at this boundary. System colors
 * are the authentic Wine Win2000 defaults, which is what makes DrawEdge and
 * friends come out pixel-identical to the classic look. */

#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

static ween_color cr_to_px(COLORREF c)
{
    return WEEN_RGBX(GetRValue(c), GetGValue(c), GetBValue(c));
}

static COLORREF px_to_cr(ween_color p)
{
    return RGB((p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff);
}

ween_color ween_cr_to_px(COLORREF c)
{
    return cr_to_px(c);
}

/* ---- system colors (Wine Win2000 GetSysColor defaults) ------------------- */

static ween_color sys_color_px(int index)
{
    switch (index) {
    case COLOR_ACTIVECAPTION:
        return WEEN_CAP_LEFT;
    case COLOR_GRADIENTACTIVECAPTION:
        return WEEN_CAP_RIGHT;
    case COLOR_CAPTIONTEXT:
        return WEEN_CAP_TEXT;
    case COLOR_WINDOW:
        return WEEN_WINDOWBG;
    case COLOR_WINDOWTEXT:
    case COLOR_BTNTEXT:
    case COLOR_MENUTEXT:
        return WEEN_BLACK;
    case COLOR_BTNFACE:
    case COLOR_3DLIGHT:
    case COLOR_MENU:
        return WEEN_FACE;
    case COLOR_BTNSHADOW:
    case COLOR_GRAYTEXT:
        return WEEN_SHADOW;
    case COLOR_HIGHLIGHT:
        return WEEN_CAP_LEFT; /* the Win2k selection navy is the caption navy */
    case COLOR_HIGHLIGHTTEXT:
        return WEEN_WHITE;
    case COLOR_BTNHIGHLIGHT:
        return WEEN_WHITE;
    case COLOR_3DDKSHADOW:
        return WEEN_DKSHADOW;
    default:
        return WEEN_BLACK;
    }
}

DWORD GetSysColor(int index)
{
    return px_to_cr(sys_color_px(index));
}

/* ---- GDI objects ----------------------------------------------------------- */

HBRUSH GetSysColorBrush(int index)
{
    /* One static brush per system color index, as on Windows. */
    static ween_gdiobj brushes[32];
    if (index < 0 || index >= 32)
        return NULL;
    brushes[index].kind = WEEN_OBJ_BRUSH;
    brushes[index].color = sys_color_px(index);
    brushes[index].is_static = 1;
    return &brushes[index];
}

HBRUSH CreateSolidBrush(COLORREF color)
{
    ween_gdiobj *b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->kind = WEEN_OBJ_BRUSH;
    b->color = cr_to_px(color);
    return b;
}

/* v1 font realisation: the engine carries Tahoma's 11px embedded bitmap
 * strikes (regular and bold) — the classic GUI face. Weight selects between
 * them (GDI's threshold: above FW_MEDIUM is bold); the height and face name
 * are accepted but the 11px strike is what every classic dialog used. */
HFONT CreateFontA(int height, int width, int escapement, int orientation,
                  int weight, DWORD italic, DWORD underline, DWORD strike_out,
                  DWORD charset, DWORD out_precision, DWORD clip_precision,
                  DWORD quality, DWORD pitch_and_family, LPCSTR face_name)
{
    (void)height;
    (void)width;
    (void)escapement;
    (void)orientation;
    (void)italic;
    (void)underline;
    (void)strike_out;
    (void)charset;
    (void)out_precision;
    (void)clip_precision;
    (void)quality;
    (void)pitch_and_family;
    (void)face_name;
    ween_gdiobj *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->kind = WEEN_OBJ_FONT;
    f->font = weight > 500 ? ween_gui_font_bold() : ween_gui_font();
    return f;
}

BOOL DeleteObject(HGDIOBJ obj)
{
    if (!obj)
        return FALSE;
    if (obj->is_static)
        return TRUE;
    if (obj->kind == WEEN_OBJ_BITMAP)
        ween_surface_free(&obj->bitmap);
    free(obj);
    return TRUE;
}

HGDIOBJ GetStockObject(int what)
{
    /* is_static is what keeps DeleteObject from freeing these. */
    static ween_gdiobj gui_font = { .kind = WEEN_OBJ_FONT, .color = 0, .is_static = 1 };
    static ween_gdiobj white = { .kind = WEEN_OBJ_BRUSH, .color = 0x00ffffff, .is_static = 1 };
    static ween_gdiobj black = { .kind = WEEN_OBJ_BRUSH, .color = 0x00000000, .is_static = 1 };
    static ween_gdiobj gray = { .kind = WEEN_OBJ_BRUSH, .color = 0x00808080, .is_static = 1 };
    static ween_gdiobj ltgray = { .kind = WEEN_OBJ_BRUSH, .color = 0x00c0c0c0, .is_static = 1 };
    static ween_gdiobj dkgray = { .kind = WEEN_OBJ_BRUSH, .color = 0x00404040, .is_static = 1 };
    switch (what) {
    case DEFAULT_GUI_FONT:
    case SYSTEM_FONT:
        gui_font.font = ween_gui_font();
        return &gui_font;
    case WHITE_BRUSH:
        return &white;
    case BLACK_BRUSH:
        return &black;
    case GRAY_BRUSH:
        return &gray;
    case LTGRAY_BRUSH:
        return &ltgray;
    case DKGRAY_BRUSH:
        return &dkgray;
    default:
        return NULL;
    }
}

/* The font a DC starts with, as a selectable object: a paint DC comes with
 * the window's font already in it, and restoring a "previous" object has to
 * put that back rather than the stock one. */
void ween_dc_set_font(struct ween_dc *dc, const ween_strike *font)
{
    dc->initial_font.kind = WEEN_OBJ_FONT;
    dc->initial_font.font = font;
    dc->initial_font.is_static = 1;
    dc->font_obj = &dc->initial_font;
    dc->font = font;
}

/* Returns what was selected before, which is the whole point of the call: the
 * usual idiom is old = SelectObject(dc, f); ...; SelectObject(dc, old), and
 * handing back the stock font instead quietly dropped a bold selection on the
 * way out. A fresh DC holds the stock objects, as GDI's does. */
HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj)
{
    if (!dc || !obj)
        return NULL;
    if (obj->kind == WEEN_OBJ_FONT) {
        HGDIOBJ prev = dc->font_obj ? dc->font_obj
                                    : GetStockObject(DEFAULT_GUI_FONT);
        dc->font_obj = obj;
        dc->font = obj->font;
        return prev;
    }
    if (obj->kind == WEEN_OBJ_BRUSH) {
        /* Nothing draws with the DC's brush yet — FillRect and the rest take
         * one explicitly — but the selection is tracked so the idiom works. */
        HGDIOBJ prev = dc->brush_obj ? dc->brush_obj
                                     : GetStockObject(WHITE_BRUSH);
        dc->brush_obj = obj;
        return prev;
    }
    return NULL;
}

/* ---- drawing ----------------------------------------------------------------- */

/* Clamp a window-relative rect to the DC's clip and convert it to surface
 * coordinates. Returns 0 for an empty result. */
static int dc_rect(HDC dc, const RECT *r, int *x, int *y, int *w, int *h)
{
    LONG left = r->left < 0 ? 0 : r->left;
    LONG top = r->top < 0 ? 0 : r->top;
    LONG right = r->right > dc->clip_w ? dc->clip_w : r->right;
    LONG bottom = r->bottom > dc->clip_h ? dc->clip_h : r->bottom;
    if (right <= left || bottom <= top)
        return 0;
    *x = dc->org_x + left;
    *y = dc->org_y + top;
    *w = right - left;
    *h = bottom - top;
    return 1;
}

BOOL FillRect(HDC dc, const RECT *rect, HBRUSH brush)
{
    int x, y, w, h;
    if (!dc || !rect || !brush || !dc_rect(dc, rect, &x, &y, &w, &h))
        return FALSE;
    ween_surface_fill(dc->s, x, y, w, h, brush->color);
    return TRUE;
}

int FrameRect(HDC dc, const RECT *rect, HBRUSH brush)
{
    if (!dc || !rect || !brush)
        return 0;
    /* a 1-logical-unit border drawn with the brush, as on Windows */
    RECT r;
    r = *rect;
    r.bottom = r.top + 1;
    FillRect(dc, &r, brush); /* top */
    r = *rect;
    r.top = r.bottom - 1;
    FillRect(dc, &r, brush); /* bottom */
    r = *rect;
    r.right = r.left + 1;
    FillRect(dc, &r, brush); /* left */
    r = *rect;
    r.left = r.right - 1;
    FillRect(dc, &r, brush); /* right */
    return 1;
}

BOOL DrawEdge(HDC dc, LPRECT rect, UINT edge, UINT flags)
{
    int x, y, w, h;
    RECT inner;
    if (!dc || !rect || !dc_rect(dc, rect, &x, &y, &w, &h))
        return FALSE;
    if (!ween_classic_edge(dc->s, x, y, w, h, edge, flags, &inner) &&
        !(flags & BF_ADJUST))
        return FALSE;
    if (flags & BF_ADJUST) {
        /* hand back the interior, in the caller's coordinates */
        rect->left = inner.left - dc->org_x;
        rect->top = inner.top - dc->org_y;
        rect->right = inner.right - dc->org_x;
        rect->bottom = inner.bottom - dc->org_y;
    }
    return TRUE;
}

BOOL DrawFrameControl(HDC dc, LPRECT rect, UINT type, UINT state)
{
    int x, y, w, h;
    if (!dc || !rect || !dc_rect(dc, rect, &x, &y, &w, &h))
        return FALSE;

    if (type == DFC_BUTTON) {
        switch (state & 0x1f) {
        case DFCS_BUTTONCHECK:
        case DFCS_BUTTON3STATE:
            ween_classic_check(dc->s, x, y, w, h, state);
            return TRUE;
        case DFCS_BUTTONRADIO:
        case DFCS_BUTTONRADIOIMAGE:
        case DFCS_BUTTONRADIOMASK:
            ween_classic_radio(dc->s, x, y, w, h, state);
            return TRUE;
        case DFCS_BUTTONPUSH:
            ween_classic_edge(dc->s, x, y, w, h,
                              (state & DFCS_PUSHED) ? EDGE_SUNKEN : EDGE_RAISED,
                              BF_RECT | BF_SOFT | BF_MIDDLE, NULL);
            return TRUE;
        default:
            return FALSE;
        }
    }
    if (type != DFC_CAPTION)
        return FALSE;

    int pushed = (state & DFCS_PUSHED) != 0;
    int code;
    switch (state & 0x000f) {
    case DFCS_CAPTIONCLOSE:
        code = 0x72;
        break;
    case DFCS_CAPTIONMIN:
        code = 0x30;
        break;
    case DFCS_CAPTIONMAX:
        code = 0x31;
        break;
    case DFCS_CAPTIONRESTORE:
        code = 0x32;
        break;
    case DFCS_CAPTIONHELP:
        code = 0x73;
        break;
    default:
        return FALSE;
    }

    /* The face and its bevel on the full rect, then the glyph. */
    /* A soft edge: white on the outside of the top and left, shadow inside
     * the bottom and right with dark shadow outside them. Without it the
     * white lands a pixel in and the button reads as smaller than it is. */
    ween_classic_edge(dc->s, x, y, w, h, pushed ? EDGE_SUNKEN : EDGE_RAISED,
                      BF_RECT | BF_SOFT | BF_MIDDLE, NULL);
    /* The three a caption wears are drawn as they are on the machine rather
     * than through Marlett: the font's outlines at this size do not land on
     * the same pixels, and each of the three sits in its own place in the
     * button — the minimise bar low, the box up, the cross between. */
    {
        static const unsigned short mini[] = { 0x3f, 0x3f };
        static const unsigned short maxi[] = { 0x1ff, 0x1ff, 0x101, 0x101,
                                               0x101, 0x101, 0x101, 0x101,
                                               0x1ff };
        static const unsigned short cross[] = { 0xc3, 0x66, 0x3c, 0x18,
                                                0x3c, 0x66, 0xc3 };
        /* The question mark a property sheet wears, read off the machine's:
         * six wide and nine tall, five in and two down in the button. */
        static const unsigned short help[] = { 0x1e, 0x33, 0x33, 0x18, 0x0c,
                                               0x0c, 0x00, 0x0c, 0x0c };
        const unsigned short *art = NULL;
        int aw = 0, ah = 0, ax = 0, ay = 0;
        switch (state & 0x000f) {
        case DFCS_CAPTIONMIN:
            art = mini; aw = 6; ah = 2; ax = 4; ay = 9;
            break;
        case DFCS_CAPTIONMAX:
            art = maxi; aw = 9; ah = 9; ax = 3; ay = 2;
            break;
        case DFCS_CAPTIONCLOSE:
            art = cross; aw = 8; ah = 7; ax = 4; ay = 3;
            break;
        case DFCS_CAPTIONHELP:
            art = help; aw = 6; ah = 9; ax = 5; ay = 2;
            break;
        default:
            break;
        }
        if (art) {
            for (int r = 0; r < ah; r++)
                for (int i = 0; i < aw; i++)
                    if (art[r] & (1u << i))
                        ween_surface_pixel(dc->s, x + ax + i + pushed,
                                           y + ay + r + pushed, WEEN_BLACK);
        } else {
            const ween_marlett *m = ween_caption_font();
            if (m) {
                int size = (w < h ? w : h) - 2;
                ween_marlett_draw(m, dc->s, code, x + (w - size) / 2,
                                  y + (h - size) / 2, size, WEEN_BLACK);
            }
        }
    }
    return TRUE;
}

/* ---- text --------------------------------------------------------------------- */

static const ween_strike *dc_font(HDC dc)
{
    return dc->font ? dc->font : ween_gui_font();
}

BOOL TextOutA(HDC dc, int x, int y, LPCSTR text, int len)
{
    const ween_strike *f = dc_font(dc);
    if (!dc || !text || !f)
        return FALSE;
    if (len < 0)
        len = (int)strlen(text);
    ween_strike_draw(f, dc->s, dc->org_x + x, dc->org_y + y, text, len,
                     cr_to_px(dc->text_color));
    return TRUE;
}

BOOL GetTextExtentPoint32A(HDC dc, LPCSTR text, int len, SIZE *size)
{
    const ween_strike *f = dc_font(dc);
    if (!dc || !text || !size || !f)
        return FALSE;
    if (len < 0)
        len = (int)strlen(text);
    size->cx = ween_strike_text_width(f, text, len);
    size->cy = f->cell_h ? f->cell_h : f->ascent - f->descent;
    return TRUE;
}

/* '&' marks the next character as the label's mnemonic: it is not drawn, and
 * the character after it is underlined. "&&" is a literal ampersand. Returns
 * the stripped length, and where the underline goes (-1 for nowhere). */
static int strip_prefix(const char *text, int len, char *out, int cap,
                        int *underline)
{
    int n = 0;
    *underline = -1;
    for (int i = 0; i < len && n < cap - 1; i++) {
        if (text[i] == '&' && i + 1 < len) {
            if (text[i + 1] == '&') { /* && is one real ampersand */
                out[n++] = '&';
                i++;
                continue;
            }
            if (*underline < 0)
                *underline = n; /* the first & wins, as in win32 */
            continue;
        }
        out[n++] = text[i];
    }
    out[n] = 0;
    return n;
}

int DrawTextA(HDC dc, LPCSTR text, int len, LPRECT rect, UINT format)
{
    const ween_strike *f = dc_font(dc);
    if (!dc || !text || !rect || !f)
        return 0;
    if (len < 0)
        len = (int)strlen(text);

    char stripped[512];
    int underline = -1;
    if (!(format & DT_NOPREFIX) && memchr(text, '&', (size_t)len)) {
        len = strip_prefix(text, len, stripped, (int)sizeof(stripped),
                           &underline);
        text = stripped;
        if (format & DT_HIDEPREFIX)
            underline = -1; /* the '&' goes, the line under it never comes */
    }

    /* Alignment is done with the *measured* width and cell height; the glyphs
     * are then drawn with the strike's own advances. */
    int tw = ween_strike_text_extent(f, text, len);
    int th = f->cell_h ? f->cell_h : f->ascent - f->descent;

    /* More than the rectangle is wide, and told to break: each line is drawn
     * on its own, aligned the same way, and the answer is how tall the lot
     * came out. A word too long for the rectangle takes a line of its own
     * rather than being cut mid-way.
     *
     * One line sits below the next by the *outline* cell — win32 steps by the
     * font's ascent and descent, not by the strike's own cell, which for a
     * bitmap face is a row taller. A paragraph in a dialog is where the two
     * part company. */
    if ((format & DT_WORDBREAK) && !(format & DT_SINGLELINE) &&
        rect->right > rect->left && tw > rect->right - rect->left) {
        int avail = rect->right - rect->left;
        int step = f->ascent - f->descent > 0 ? f->ascent - f->descent : th;
        int at = 0, line = 0;
        RECT one = *rect;
        while (at < len) {
            int take = 0, last_space = -1, i;
            for (i = at; i < len; i++) {
                if (text[i] == '\n') {
                    last_space = i;
                    break;
                }
                if (ween_strike_text_extent(f, text + at, i - at + 1) > avail)
                    break;
                if (text[i] == ' ')
                    last_space = i;
            }
            take = i >= len            ? len - at
                   : last_space > at   ? last_space - at
                                       : (i > at ? i - at : 1);
            one.top = rect->top + line * step;
            one.bottom = one.top + th;
            if (!(format & DT_CALCRECT))
                DrawTextA(dc, text + at, take, &one,
                          (format & ~DT_WORDBREAK) | DT_SINGLELINE |
                              DT_NOPREFIX);
            at += take;
            while (at < len && (text[at] == ' ' || text[at] == '\n'))
                at++;
            line++;
        }
        if (format & DT_CALCRECT)
            rect->bottom = rect->top + line * step;
        return line * step;
    }

    LONG x = rect->left;
    if (format & DT_CENTER)
        x = rect->left + ((rect->right - rect->left) - tw) / 2;
    else if (format & DT_RIGHT)
        x = rect->right - tw;

    LONG y = rect->top;
    if ((format & DT_VCENTER) && (format & DT_SINGLELINE))
        y = rect->top + ((rect->bottom - rect->top) - th) / 2;

    /* win32 clips to the rectangle unless told not to, and a caller that
     * hands over a rectangle usually means it: a status bar's part, a list
     * view's cell. Without this a long string runs straight through whatever
     * is beside it. */
    RECT saved;
    ween_surface_get_clip(dc->s, &saved);
    if (!(format & DT_NOCLIP)) {
        RECT c;
        c.left = dc->org_x + rect->left;
        c.top = dc->org_y + rect->top;
        c.right = dc->org_x + rect->right;
        c.bottom = dc->org_y + rect->bottom;
        if (c.left < saved.left)
            c.left = saved.left;
        if (c.top < saved.top)
            c.top = saved.top;
        if (c.right > saved.right)
            c.right = saved.right;
        if (c.bottom > saved.bottom)
            c.bottom = saved.bottom;
        ween_surface_clip(dc->s, c.left, c.top, c.right - c.left,
                          c.bottom - c.top);
    }

    ween_strike_draw(f, dc->s, dc->org_x + x, dc->org_y + y, text, len,
                     cr_to_px(dc->text_color));
    if (underline >= 0 && underline < len) {
        /* A one-pixel rule under the mnemonic character, on the row below the
         * baseline — which for the eleven-pixel faces is twelve down, where
         * both the reference capture and the machine put it. */
        int x0 = ween_strike_pen(f, text, underline);
        int x1 = ween_strike_pen(f, text, underline + 1);
        ween_surface_hline(dc->s, dc->org_x + x + x0,
                           dc->org_y + y + f->ascent + 1, x1 - x0,
                           cr_to_px(dc->text_color));
    }
    ween_surface_clip(dc->s, saved.left, saved.top, saved.right - saved.left,
                      saved.bottom - saved.top);
    return th;
}

COLORREF SetTextColor(HDC dc, COLORREF color)
{
    if (!dc)
        return 0;
    COLORREF prev = dc->text_color;
    dc->text_color = color;
    return prev;
}

int SetBkMode(HDC dc, int mode)
{
    if (!dc)
        return 0;
    int prev = dc->bk_mode;
    dc->bk_mode = mode; /* text is always drawn transparent in v1 */
    return prev;
}
