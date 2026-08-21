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

    RECT wr = { 0, 0, MX(cx), MY(cy) };
    AdjustWindowRect(&wr, style, FALSE);
    int win_w = wr.right - wr.left;
    int win_h = wr.bottom - wr.top;

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
        /* The first BS_DEFPUSHBUTTON in the template is the dialog's default
         * command, which is what Enter presses. */
        if (!dlg->defid && (istyle & BS_DEFPUSHBUTTON) == BS_DEFPUSHBUTTON)
            dlg->defid = id;
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

/* Dialog keyboard navigation: the app calls this in its loop before dispatch,
 * and any window with controls in it wants to, not only a dialog. Tab and
 * Shift+Tab move focus across WS_TABSTOP controls; the arrows move within a
 * group of option buttons; Space presses the focused button; Alt+letter takes
 * the control whose label marks that letter; Enter fires the default command
 * and Esc cancels. */
BOOL IsDialogMessageA(HWND dlg, LPMSG msg)
{
    if (!dlg || !msg || msg->message != WM_KEYDOWN)
        return FALSE;
    HWND focus = ween_focus_get();
    /* the backend puts Shift in bit 0 of lParam and Alt in bit 29, where win32
     * keeps the context code */
    int shift = (msg->lParam & 1) != 0;
    int alt = (msg->lParam & (1L << 29)) != 0;

    if (alt) {
        unsigned ch = (unsigned)(msg->lParam >> 16) & 0xff;
        HWND target = ween_mnemonic_target(dlg, ch ? ch : (unsigned)msg->wParam);
        if (target) {
            SetFocus(target);
            SendMessageA(target, BM_CLICK, 0, 0);
            return TRUE;
        }
        return FALSE;
    }

    switch (msg->wParam) {
    case VK_TAB: {
        HWND nx = ween_tab_next(dlg, focus, !shift);
        if (nx)
            SetFocus(nx);
        return TRUE;
    }
    case VK_SPACE:
        if (focus && focus != dlg) {
            SendMessageA(focus, WM_KEYDOWN, VK_SPACE, msg->lParam);
            return TRUE;
        }
        return FALSE;
    case VK_UP:
    case VK_LEFT:
    case VK_DOWN:
    case VK_RIGHT: {
        int forward = msg->wParam == VK_DOWN || msg->wParam == VK_RIGHT;
        HWND nx = ween_radio_step(focus, forward);
        if (!nx)
            return FALSE; /* not in a group: the control keeps its arrows */
        SetFocus(nx);
        SendMessageA(nx, BM_CLICK, 0, 0);
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
    if (!dlg)
        return FALSE;
    /* A modal dialog is torn down by the loop that owns it, once it has the
     * result; a modeless one has no such loop and goes now. */
    dlg->dlg_result = result;
    dlg->dlg_ended = 1;
    if (!dlg->is_modal)
        return DestroyWindow(dlg);
    return TRUE;
}

/* The modal loop. The owner is disabled for as long as the dialog is up —
 * that is the whole of what "modal" means in win32 — and the loop runs the
 * dialog's own keyboard conventions before dispatching, as an app's would. */
INT_PTR DialogBoxIndirectParamA(HINSTANCE inst, LPCDLGTEMPLATEA tmpl,
                                HWND owner, DLGPROC proc, LPARAM param)
{
    /* The owner goes down before the dialog exists, so that a DLGPROC seeing
     * WM_INITDIALOG already finds the window it belongs to disabled. */
    int reenable = owner && !(owner->style & WS_DISABLED);
    if (reenable)
        EnableWindow(owner, FALSE);

    HWND dlg = CreateDialogIndirectParamA(inst, tmpl, owner, proc, param);
    if (!dlg) {
        if (reenable)
            EnableWindow(owner, TRUE);
        return -1;
    }
    dlg->is_modal = 1;

    MSG msg;
    while (!dlg->dlg_ended && GetMessageA(&msg, NULL, 0, 0)) {
        if (IsDialogMessageA(dlg, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    INT_PTR result = dlg->dlg_result;
    if (reenable)
        EnableWindow(owner, TRUE);
    if (!dlg->destroyed)
        DestroyWindow(dlg);
    return result;
}

/* ---- MessageBoxA ----------------------------------------------------------
 *
 * The one dialog every app has. It is built here rather than from a template
 * because its size comes from the text: win32 measures the message, wraps it,
 * and sizes the box around it, then centres the buttons under it.
 */

#define MB_MARGIN 12   /* text inset from the client edge */
#define MB_BTN_W 75    /* the classic 50x14 DLU button, in pixels at 96 dpi */
#define MB_BTN_H 23
#define MB_BTN_GAP 6

static int line_count(const char *text, const ween_strike *f, int *widest)
{
    int lines = 1;
    const char *start = text;
    *widest = 0;
    for (const char *p = text;; p++) {
        if (*p == '\n' || !*p) {
            int w = ween_strike_text_extent(f, start, (int)(p - start));
            if (w > *widest)
                *widest = w;
            if (!*p)
                break;
            lines++;
            start = p + 1;
        }
    }
    return lines;
}

/* The box's own dialog procedure: any command from its buttons is the answer,
 * whether it came from a click, from Enter on the default button, or from Esc.
 * Going through the DLGPROC rather than watching the queue means every one of
 * those paths ends the box the same way. */
static INT_PTR CALLBACK msgbox_proc(HWND box, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_COMMAND) {
        EndDialog(box, (INT_PTR)LOWORD(wp));
        return TRUE;
    }
    if (msg == WM_CLOSE) {
        EndDialog(box, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

int MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type)
{
    const ween_strike *f = ween_gui_font();
    int cell = f ? (f->cell_h ? f->cell_h : f->ascent - f->descent) : 12;
    int widest = 0, lines;
    int nbuttons = (type & MB_OKCANCEL) == MB_OKCANCEL ? 2 : 1;
    int ids[2] = { IDOK, IDCANCEL };
    const char *labels[2] = { "OK", "Cancel" };

    if (!text)
        text = "";
    lines = line_count(text, f, &widest);
    if ((type & MB_YESNO) == MB_YESNO) {
        nbuttons = 2;
        ids[0] = IDYES;
        ids[1] = IDNO;
        labels[0] = "&Yes";
        labels[1] = "&No";
    }

    int text_h = lines * (cell + 2);
    int buttons_w = nbuttons * MB_BTN_W + (nbuttons - 1) * MB_BTN_GAP;
    int client_w = widest + 2 * MB_MARGIN;
    if (client_w < buttons_w + 2 * MB_MARGIN)
        client_w = buttons_w + 2 * MB_MARGIN;
    int client_h = MB_MARGIN + text_h + MB_MARGIN + MB_BTN_H + MB_MARGIN;

    RECT wr = { 0, 0, client_w, client_h };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    AdjustWindowRect(&wr, style, FALSE);

    /* centred on the owner, as win32 puts it */
    int x = 60, y = 60;
    if (owner) {
        RECT o;
        GetWindowRect(owner, &o);
        x = o.left + ((o.right - o.left) - (wr.right - wr.left)) / 2;
        y = o.top + ((o.bottom - o.top) - (wr.bottom - wr.top)) / 2;
    }

    ensure_dialog_class();
    HWND box = CreateWindowExA(WS_EX_DLGMODALFRAME, "#32770",
                               caption ? caption : "", style, x, y,
                               wr.right - wr.left, wr.bottom - wr.top, owner,
                               NULL, NULL, NULL);
    if (!box)
        return 0;
    box->is_dialog = 1;
    box->is_modal = 1;
    box->dlgproc = msgbox_proc;

    /* One static per line: a newline in the message is a line break, and the
     * STATIC control itself draws a single line. */
    {
        const char *start = text;
        int ly = MB_MARGIN;
        for (const char *p = text;; p++) {
            if (*p != '\n' && *p)
                continue;
            char line[512];
            int n = (int)(p - start);
            if (n > (int)sizeof(line) - 1)
                n = (int)sizeof(line) - 1;
            memcpy(line, start, (size_t)n);
            line[n] = 0;
            CreateWindowExA(0, "STATIC", line, WS_CHILD | WS_VISIBLE, MB_MARGIN,
                            ly, widest + 2, cell + 2, box, NULL, NULL, NULL);
            ly += cell + 2;
            if (!*p)
                break;
            start = p + 1;
        }
    }
    int bx = (client_w - buttons_w) / 2;
    for (int i = 0; i < nbuttons; i++) {
        DWORD bs = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                   (i == 0 ? BS_DEFPUSHBUTTON : 0);
        HWND b = CreateWindowExA(0, "BUTTON", labels[i], bs, bx,
                                 client_h - MB_MARGIN - MB_BTN_H, MB_BTN_W,
                                 MB_BTN_H, box, (HMENU)(UINT_PTR)ids[i], NULL,
                                 NULL);
        if (i == 0 && b)
            SetFocus(b);
        bx += MB_BTN_W + MB_BTN_GAP;
    }
    box->defid = (UINT)ids[0];

    int reenable = owner && !(owner->style & WS_DISABLED);
    if (reenable)
        EnableWindow(owner, FALSE);

    MSG msg;
    while (!box->dlg_ended && GetMessageA(&msg, NULL, 0, 0)) {
        if (IsDialogMessageA(box, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    int result = (int)box->dlg_result;
    if (reenable)
        EnableWindow(owner, TRUE);
    if (!box->destroyed)
        DestroyWindow(box);
    return result ? result : ids[0];
}
