/* Dialog units and the dialog manager — the authentic win32 positioning
 * mechanism.
 *
 * Dialogs were laid out in template units derived from the dialog's font, so
 * the same template produced consistent dialogs at any font/DPI:
 *   horizontal base unit = the font's average character width
 *     (for a non-system font: averaged over a..z A..Z, per MS KB Q145994);
 *   vertical base unit   = the font height;
 *   pixelX = MulDiv(dluX, baseX, 4);   pixelY = MulDiv(dluY, baseY, 8).
 * Standard metrics in DLUs: push button 50x14, dialog margin 7, spacing 4. */

#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

int MulDiv(int number, int numerator, int denominator)
{
    if (denominator == 0)
        return -1;
    long long v = (long long)number * numerator;
    /* round half away from zero, as the real MulDiv does */
    if ((v < 0) == (denominator < 0))
        v += denominator / 2;
    else
        v -= denominator / 2;
    return (int)(v / denominator);
}

/* Base units of a strike font: KB Q145994's average over the 52 letters
 * (rounded to nearest), and the font's cell height. */
static void base_units(const ween_strike *f, int *bx, int *by)
{
    static const char letters[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int sum = 0;
    for (int i = 0; i < 52; i++)
        sum += ween_strike_char_advance(f, (unsigned char)letters[i]);
    *bx = (sum + 26) / 52;
    *by = f->ascent - f->descent;
}

LONG GetDialogBaseUnits(void)
{
    const ween_strike *f = ween_gui_font();
    int bx = 6, by = 13; /* the classic 96-dpi GUI-font values */
    if (f)
        base_units(f, &bx, &by);
    return (LONG)MAKELPARAM((WORD)bx, (WORD)by);
}

BOOL MapDialogRect(HWND dlg, LPRECT rect)
{
    if (!rect)
        return FALSE;
    const ween_strike *f = (dlg && dlg->font) ? dlg->font : ween_gui_font();
    if (!f)
        return FALSE;
    int bx, by;
    base_units(f, &bx, &by);
    rect->left = MulDiv(rect->left, bx, 4);
    rect->right = MulDiv(rect->right, bx, 4);
    rect->top = MulDiv(rect->top, by, 8);
    rect->bottom = MulDiv(rect->bottom, by, 8);
    return TRUE;
}

/* ---- the dialog manager ------------------------------------------------- */

static WORD rd_w(const unsigned char *b, size_t o)
{
    return (WORD)(b[o] | (b[o + 1] << 8));
}

static DWORD rd_d(const unsigned char *b, size_t o)
{
    return (DWORD)rd_w(b, o) | ((DWORD)rd_w(b, o + 2) << 16);
}

/* A template "sz_Or_Ord": 0x0000 = empty, 0xFFFF + WORD = ordinal, else a
 * null-terminated UTF-16 string. Returns the cursor past it; captures the
 * ordinal in *ord (0 empty, 0xFFFF string) and the ASCII of a string in str. */
static size_t parse_sz(const unsigned char *b, size_t o, WORD *ord, char *str,
                       int cap)
{
    WORD w = rd_w(b, o);
    if (str)
        str[0] = 0;
    if (w == 0x0000) {
        if (ord)
            *ord = 0;
        return o + 2;
    }
    if (w == 0xFFFF) {
        if (ord)
            *ord = rd_w(b, o + 2);
        return o + 4;
    }
    if (ord)
        *ord = 0xFFFF;
    int i = 0;
    size_t c = o;
    for (;;) {
        WORD ch = rd_w(b, c);
        c += 2;
        if (ch == 0)
            break;
        if (str && i < cap - 1)
            str[i++] = (char)ch;
    }
    if (str)
        str[i] = 0;
    return c;
}

/* Predefined window-class atoms used in templates (winuser.h). */
static const char *class_from_ord(WORD ord)
{
    switch (ord) {
    case 0x0080:
        return "BUTTON";
    case 0x0081:
        return "EDIT";
    case 0x0082:
        return "STATIC";
    case 0x0083:
        return "LISTBOX";
    case 0x0085:
        return "COMBOBOX";
    default:
        return "STATIC";
    }
}

/* The dialog frame's window proc ("#32770"): give the app's DLGPROC first
 * refusal, then fall back to the default dialog processing. */
static LRESULT dlg_class_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (w->dlgproc) {
        INT_PTR r = w->dlgproc(w, msg, wp, lp);
        if (r)
            return (LRESULT)r; /* handled */
    }
    return DefDlgProcA(w, msg, wp, lp);
}

LRESULT DefDlgProcA(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case DM_SETDEFID:
        dlg->defid = (UINT)wp;
        return TRUE;
    case DM_GETDEFID:
        return dlg->defid ? (LRESULT)MAKELPARAM((WORD)dlg->defid, DC_HASDEFID) : 0;
    default:
        return DefWindowProcA(dlg, msg, wp, lp);
    }
}

static void ensure_dialog_class(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = dlg_class_proc;
    wc.lpszClassName = "#32770";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc); /* idempotent: RegisterClassA ignores a dup name */
}

HWND CreateDialogIndirectParamA(HINSTANCE inst, LPCDLGTEMPLATEA tmpl,
                                HWND parent, DLGPROC proc, LPARAM init_param)
{
    if (!tmpl)
        return NULL;
    ensure_dialog_class();
    const unsigned char *b = (const unsigned char *)tmpl;

    DWORD style = rd_d(b, 0);
    WORD cdit = rd_w(b, 8);
    short dx = (short)rd_w(b, 10), dy = (short)rd_w(b, 12);
    short cx = (short)rd_w(b, 14), cy = (short)rd_w(b, 16);

    size_t p = 18;
    char title[128];
    p = parse_sz(b, p, NULL, NULL, 0);        /* menu */
    p = parse_sz(b, p, NULL, NULL, 0);        /* class (ignored: our own) */
    p = parse_sz(b, p, NULL, title, sizeof title); /* title */
    if (style & DS_SETFONT) {
        p += 2;                        /* point size */
        p = parse_sz(b, p, NULL, NULL, 0); /* typeface */
    }

    const ween_strike *f = ween_gui_font();
    int bx = 6, by = 13;
    if (f)
        base_units(f, &bx, &by);
#define MX(u) MulDiv((u), bx, 4)
#define MY(u) MulDiv((u), by, 8)

    int cap = ((style & WS_CAPTION) == WS_CAPTION) ? WEEN_NC_CAPTION : 0;
    int win_w = MX(cx) + 2 * WEEN_NC_FRAME;
    int win_h = MY(cy) + 2 * WEEN_NC_FRAME + cap;

    HWND dlg = CreateWindowExA(0, "#32770", title, style, MX(dx), MY(dy), win_w,
                               win_h, parent, NULL, inst, NULL);
    if (!dlg)
        return NULL;
    dlg->is_dialog = 1;
    dlg->dlgproc = proc;

    /* Instantiate every control, mapping its DLU rect to pixels edge by edge
     * (MapDialogRect semantics) so shared DLU edges land on shared pixels. */
    for (int i = 0; i < cdit; i++) {
        p = (p + 3) & ~(size_t)3; /* items are DWORD-aligned in the stream */
        DWORD istyle = rd_d(b, p);
        short ix = (short)rd_w(b, p + 8), iy = (short)rd_w(b, p + 10);
        short icx = (short)rd_w(b, p + 12), icy = (short)rd_w(b, p + 14);
        WORD id = rd_w(b, p + 16);
        p += 18;
        WORD cls_ord;
        char cls_str[32], itext[64];
        p = parse_sz(b, p, &cls_ord, cls_str, sizeof cls_str);
        p = parse_sz(b, p, NULL, itext, sizeof itext);
        WORD cdata = rd_w(b, p);
        p += 2 + cdata; /* creation data block */

        const char *cls = cls_str[0] ? cls_str : class_from_ord(cls_ord);
        int px = MX(ix), py = MY(iy);
        CreateWindowExA(0, cls, itext, istyle | WS_CHILD, px, py,
                        MX(ix + icx) - px, MY(iy + icy) - py, dlg,
                        (HMENU)(UINT_PTR)id, inst, NULL);
    }

    /* WM_INITDIALOG (after the controls exist). TRUE => set default focus. */
    HWND first = ween_tab_next(dlg, NULL, 1);
    INT_PTR r = SendMessageA(dlg, WM_INITDIALOG, (WPARAM)first, init_param);
    if (r && first)
        SetFocus(first);
#undef MX
#undef MY
    return dlg;
}

/* Dialog keyboard navigation: the app calls this in its loop before dispatch.
 * Tab moves focus across WS_TABSTOP controls; Enter fires the default command;
 * Esc cancels. */
BOOL IsDialogMessageA(HWND dlg, LPMSG msg)
{
    if (!dlg || !msg || msg->message != WM_KEYDOWN)
        return FALSE;
    switch (msg->wParam) {
    case VK_TAB: {
        HWND nx = ween_tab_next(dlg, ween_focus_get(), 1);
        if (nx)
            SetFocus(nx);
        return TRUE;
    }
    case VK_RETURN:
        if (dlg->defid)
            SendMessageA(dlg, WM_COMMAND, MAKEWPARAM((WORD)dlg->defid, BN_CLICKED),
                         (LPARAM)GetDlgItem(dlg, (int)dlg->defid));
        return TRUE;
    case VK_ESCAPE:
        SendMessageA(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        return TRUE;
    default:
        return FALSE;
    }
}

BOOL EndDialog(HWND dlg, INT_PTR result)
{
    (void)result; /* modal loop (DialogBox) awaits multi-window support */
    return DestroyWindow(dlg);
}
