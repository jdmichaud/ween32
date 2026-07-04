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
        return WEEN_BLACK;
    case COLOR_BTNFACE:
    case COLOR_3DLIGHT:
        return WEEN_FACE;
    case COLOR_BTNSHADOW:
        return WEEN_SHADOW;
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
    if (!obj->is_static)
        free(obj);
    return TRUE;
}

HGDIOBJ GetStockObject(int what)
{
    static ween_gdiobj gui_font = { WEEN_OBJ_FONT, 0, NULL, 1 };
    switch (what) {
    case DEFAULT_GUI_FONT:
    case SYSTEM_FONT:
        gui_font.font = ween_gui_font();
        return &gui_font;
    default:
        return NULL;
    }
}

HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj)
{
    if (!dc || !obj)
        return NULL;
    if (obj->kind == WEEN_OBJ_FONT) {
        ween_gdiobj *prev = GetStockObject(DEFAULT_GUI_FONT);
        dc->font = obj->font;
        return prev; /* v1: the GUI font is the only selectable font */
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
    if (!dc || !rect || !dc_rect(dc, rect, &x, &y, &w, &h))
        return FALSE;
    if ((flags & BF_RECT) != BF_RECT)
        return FALSE; /* v1 draws full rectangles only */
    if (edge == EDGE_RAISED)
        ween_classic_bevel(dc->s, x, y, w, h, 0);
    else if (edge == EDGE_SUNKEN)
        ween_classic_bevel(dc->s, x, y, w, h, 1);
    else
        return FALSE;
    return TRUE;
}

BOOL DrawFrameControl(HDC dc, LPRECT rect, UINT type, UINT state)
{
    int x, y, w, h;
    if (!dc || !rect || type != DFC_CAPTION || !dc_rect(dc, rect, &x, &y, &w, &h))
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
    default:
        return FALSE;
    }

    /* Wine's UITOOLS95_DrawFrameCaption: the button face + bevel on the full
     * rect, then Marlett at SmallDiam = short side - 2, centred — and NOT
     * shifted when pushed (only the bevel flips to sunken). */
    ween_surface_fill(dc->s, x, y, w, h, WEEN_FACE);
    ween_classic_bevel(dc->s, x, y, w, h, pushed);
    const ween_marlett *m = ween_caption_font();
    if (m) {
        int size = (w < h ? w : h) - 2;
        int gx = x + (w - size) / 2;
        int gy = y + (h - size) / 2;
        ween_marlett_draw(m, dc->s, code, gx, gy, size, WEEN_BLACK);
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
    size->cy = f->ascent - f->descent;
    return TRUE;
}

int DrawTextA(HDC dc, LPCSTR text, int len, LPRECT rect, UINT format)
{
    const ween_strike *f = dc_font(dc);
    if (!dc || !text || !rect || !f)
        return 0;
    if (len < 0)
        len = (int)strlen(text);

    int tw = ween_strike_text_width(f, text, len);
    int th = f->ascent - f->descent;

    LONG x = rect->left;
    if (format & DT_CENTER)
        x = rect->left + ((rect->right - rect->left) - tw) / 2;
    else if (format & DT_RIGHT)
        x = rect->right - tw;

    LONG y = rect->top;
    if ((format & DT_VCENTER) && (format & DT_SINGLELINE))
        y = rect->top + ((rect->bottom - rect->top) - th) / 2;

    ween_strike_draw(f, dc->s, dc->org_x + x, dc->org_y + y, text, len,
                     cr_to_px(dc->text_color));
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
