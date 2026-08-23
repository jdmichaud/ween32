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
/* A message box's own metrics. Declared here because the box's paint wants
 * them as much as its layout does. */
#define MB_MARGIN 12   /* text inset from the client edge */
#define MB_ICON_W 32   /* the picture beside the message, and what follows it */
#define MB_ICON_GAP 12

/* The messages whose answer *is* the dialog procedure's return value. For
 * every other message the return says only whether the procedure dealt with
 * it, and the answer is whatever it left in DWLP_MSGRESULT — nothing, meaning
 * zero, unless it said otherwise. Win32's list also has the WM_CTLCOLOR*
 * messages and a few others ween32 has no use for yet; add them here when it
 * does, rather than letting a return value stand in for an answer. */
static int dlg_returns_answer(UINT msg)
{
    switch (msg) {
    case WM_INITDIALOG:
        return 1;
    default:
        return 0;
    }
}

static LRESULT dlg_class_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (w->dlgproc) {
        INT_PTR r;
        w->dlg_msgresult = 0;
        w->dlg_msgresult_set = 0;
        r = w->dlgproc(w, msg, wp, lp);
        if (r)
            return dlg_returns_answer(msg) ? (LRESULT)r : w->dlg_msgresult;
    }
    return DefDlgProcA(w, msg, wp, lp);
}

LRESULT DefDlgProcA(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        /* A dialog's close box is its Cancel button. win32's DefDlgProc
         * turns it into WM_COMMAND with IDCANCEL and leaves the rest to the
         * dialog procedure, which is where a program decides whether it may
         * close at all — so a box that ignores IDCANCEL stays up, there and
         * here.
         *
         * What must not happen is DefWindowProc's answer, which is to
         * destroy the window: a modal dialog's loop waits on the window it
         * was given, and destroying that out from under it leaves the loop
         * spinning on freed memory with the owner still disabled. The
         * program looks hung, because it is. */
        SendMessageA(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED),
                     (LPARAM)GetDlgItem(dlg, IDCANCEL));
        return 0;
    case WM_PAINT:
        if (dlg->msgbox_icon) { /* the picture beside the message */
            PAINTSTRUCT ps;
            struct ween_wnd *top = ween_top_level(dlg);
            int ox, oy;
            BeginPaint(dlg, &ps);
            ween_client_origin(dlg, &ox, &oy);
            ween_classic_msgbox_icon(&top->surface, ox + MB_MARGIN,
                                     oy + MB_MARGIN,
                                     (unsigned)dlg->msgbox_icon);
            EndPaint(dlg, &ps);
            return 0;
        }
        return DefWindowProcA(dlg, msg, wp, lp);
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

/* Handing a control the focus, the way the dialog manager does it rather than
 * the way SetFocus does: a control that says it keeps a selection has all of
 * it selected first, so that what is typed replaces what was there. It is the
 * dialog that does this, not the control — clicking into a field puts the
 * caret where the click was, and leaves the rest alone. */
static void dlg_focus(HWND ctl)
{
    if (!ctl)
        return;
    if (SendMessageA(ctl, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL)
        SendMessageA(ctl, EM_SETSEL, 0, (LPARAM)-1);
    SetFocus(ctl);
}

HWND CreateDialogIndirectParamA(HINSTANCE inst, LPCDLGTEMPLATEA tmpl,
                                HWND parent, DLGPROC proc, LPARAM init_param)
{
    if (!tmpl)
        return NULL;
    ensure_dialog_class();
    const unsigned char *b = (const unsigned char *)tmpl;

    DWORD style = rd_d(b, 0);
    DWORD ex_style = rd_d(b, 4); /* the template's own, which was being lost */
    WORD cdit = rd_w(b, 8);
    short dx = (short)rd_w(b, 10), dy = (short)rd_w(b, 12);
    short cx = (short)rd_w(b, 14), cy = (short)rd_w(b, 16);

    size_t p = 18;
    char title[128];
    p = parse_sz(b, p, NULL, NULL, 0);        /* menu */
    p = parse_sz(b, p, NULL, NULL, 0);        /* class (ignored: our own) */
    p = parse_sz(b, p, NULL, title, sizeof title); /* title */
    char face[64];
    face[0] = 0;
    if (style & DS_SETFONT) {
        p += 2;                                     /* point size */
        p = parse_sz(b, p, NULL, face, sizeof face); /* typeface */
    }

    /* A template names the face it was laid out in, and the dialog and
     * everything in it is lettered in that. Ignoring it left every dialog in
     * the shell's UI font, which is not what the shell's own dialogs are in. */
    const ween_strike *dlg_font = ween_font_by_face(face);
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

    /* A template's position is relative to the owner's client area unless it
     * says DS_ABSALIGN, in which case it is on the screen. Without this a
     * dialog whose template says 0,0 — which is most of them — lands in the
     * corner of the display instead of over the window that opened it. */
    int px = MX(dx), py = MY(dy);
    if (parent && !(style & DS_ABSALIGN)) {
        RECT owner_rect;
        int cox, coy;
        GetWindowRect(ween_top_level(parent), &owner_rect);
        ween_client_origin(parent, &cox, &coy);
        px += owner_rect.left + cox;
        py += owner_rect.top + coy;
    }

    /* A template asking for context help wears the question mark: the style
     * bit is on the dialog, the extended one is what the caption reads, and
     * it goes on top of whatever extended style the template carries. */
    DWORD dex = ex_style | ((style & DS_CONTEXTHELP) ? WS_EX_CONTEXTHELP : 0);
    HWND dlg = CreateWindowExA(dex, "#32770", title, style, px, py, win_w,
                               win_h, parent, NULL, inst, NULL);
    if (!dlg)
        return NULL;
    dlg->is_dialog = 1;
    dlg->dlgproc = proc;
    dlg->font = dlg_font;

    /* Instantiate every control. Its position and its size are mapped
     * *separately* -- pixel_x = MulDiv(x, bx, 4) and pixel_cx = MulDiv(cx,
     * bx, 4) -- and not as a pair of edges. The difference shows on any odd
     * dialog unit: Paint's Custom Zoom has a label 47 units wide at unit 13,
     * which Windows puts at x=20 and makes 71 wide, where mapping the far
     * edge (MulDiv(60) - MulDiv(13) = 90 - 20) would have made it 70. */
    for (int i = 0; i < cdit; i++) {
        p = (p + 3) & ~(size_t)3; /* items are DWORD-aligned in the stream */
        DWORD istyle = rd_d(b, p);
        /* The item's extended style, which is where a field gets its sunken
         * border from: a template that asks for WS_EX_CLIENTEDGE must get
         * one, and this used to drop it on the floor. */
        DWORD iex = rd_d(b, p + 4);
        short ix = (short)rd_w(b, p + 8), iy = (short)rd_w(b, p + 10);
        short icx = (short)rd_w(b, p + 12), icy = (short)rd_w(b, p + 14);
        WORD id = rd_w(b, p + 16);
        p += 18;
        WORD cls_ord;
        /* Room for a paragraph: a label in a dialog is often one, and the
         * shell's are. */
        char cls_str[32], itext[512];
        p = parse_sz(b, p, &cls_ord, cls_str, sizeof cls_str);
        p = parse_sz(b, p, NULL, itext, sizeof itext);
        WORD cdata = rd_w(b, p);
        p += 2 + cdata; /* creation data block */

        const char *cls = cls_str[0] ? cls_str : class_from_ord(cls_ord);
        HWND c = CreateWindowExA(iex, cls, itext, istyle | WS_CHILD, MX(ix),
                                 MY(iy), MX(icx), MY(icy), dlg,
                                 (HMENU)(UINT_PTR)id, inst, NULL);
        /* The strike straight on, not through WM_SETFONT: that takes an
         * HFONT, and the face named in a template is the dialog manager's own
         * business rather than something the program made. */
        if (c)
            c->font = dlg_font;
        /* The first BS_DEFPUSHBUTTON in the template is the dialog's default
         * command, which is what Enter presses — and only a button can be
         * one. The style bit it lives in is bit zero, which every class uses
         * for something else: a list view's LVS_REPORT is the same bit. And
         * the button styles are a small number in that field rather than a
         * set of flags — BS_AUTORADIOBUTTON is 9, which has bit zero in it —
         * so the whole field has to match, not the one bit. */
        if (!dlg->defid && cls && !strcmp(cls, "BUTTON") &&
            (istyle & BS_TYPEMASK) == BS_DEFPUSHBUTTON)
            dlg->defid = id;
    }

    /* WM_INITDIALOG (after the controls exist). TRUE => set default focus. */
    HWND first = ween_tab_next(dlg, NULL, 1);
    INT_PTR r = SendMessageA(dlg, WM_INITDIALOG, (WPARAM)first, init_param);
    if (r && first)
        dlg_focus(first);
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

    /* Alt on its own, or F10, opens the window's menu bar; Alt+letter opens
     * the drop-down that letter marks. The bar gets first refusal, because a
     * control's mnemonic and a menu's can be the same letter and win32 gives
     * the menu that key while Alt is down. */
    HWND top = ween_top_level(dlg);
    if (msg->wParam == VK_MENU || msg->wParam == VK_F10) {
        /* Through the window, not straight to the menu: WM_SYSCOMMAND with
         * SC_KEYMENU is how win32 asks, and an application whose menu is a
         * band of its own answers it rather than the frame. */
        if (SendMessageA(top, WM_SYSCOMMAND, SC_KEYMENU, 0) == 0)
            return TRUE;
    }
    /* With the bar armed — Alt pressed and waiting — the arrows walk it,
     * Down opens what they are on, Escape puts the underlines away, and a
     * letter on its own opens the drop-down it marks. */
    if (!alt && ween_menu_armed()) {
        unsigned ch2 = (unsigned)(msg->lParam >> 16) & 0xff;
        unsigned key2 = ch2 ? ch2 : (unsigned)msg->wParam;
        if (ween_menu_armed_key(top, (unsigned)msg->wParam))
            return TRUE;
        if (ween_menu_key(top, 0, key2))
            return TRUE;
    }
    if (alt) {
        unsigned ch = (unsigned)(msg->lParam >> 16) & 0xff;
        unsigned key = ch ? ch : (unsigned)msg->wParam;
        if (SendMessageA(top, WM_SYSCOMMAND, SC_KEYMENU, (LPARAM)key) == 0)
            return TRUE;
        HWND target = ween_mnemonic_target(dlg, key);
        if (target) {
            dlg_focus(target);
            SendMessageA(target, BM_CLICK, 0, 0);
            return TRUE;
        }
        return FALSE;
    }

    switch (msg->wParam) {
    case VK_TAB: {
        HWND nx = ween_tab_next(dlg, focus, !shift);
        if (nx)
            dlg_focus(nx);
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
    case VK_RETURN: {
        /* The keyboard carries the default with it: Enter presses the button
         * the focus is on, and the template's default only when the focus is
         * somewhere else. That is the same rule the black ring is drawn by. */
        HWND focus = ween_focus_get();
        int inside = 0;
        for (HWND p = focus; p; p = p->parent)
            if (p == dlg) {
                inside = 1;
                break;
            }
        if (focus && inside && ween_button_is_default(focus))
            SendMessageA(dlg, WM_COMMAND,
                         MAKEWPARAM((WORD)focus->id, BN_CLICKED),
                         (LPARAM)focus);
        else if (dlg->defid)
            SendMessageA(dlg, WM_COMMAND, MAKEWPARAM((WORD)dlg->defid, BN_CLICKED),
                         (LPARAM)GetDlgItem(dlg, (int)dlg->defid));
        return TRUE;
    }
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
    /* A modal dialog is shown by the dialog manager whatever its template
     * says; only a modeless one waits for WS_VISIBLE. Without this, a
     * template written the way the resource compiler writes them — no
     * WS_VISIBLE, because DialogBox supplies it — came up as an empty frame
     * with its controls unpainted behind it. */
    ShowWindow(dlg, SW_SHOW);
    return ween_dialog_modal(dlg, owner, reenable);
}

/* Run a dialog that already exists until EndDialog answers it. Split out
 * because a property sheet has to put its pages in before anyone can be shown
 * one, so it makes its frame first and runs it afterwards. */
INT_PTR ween_dialog_modal(HWND dlg, HWND owner, int reenable)
{
    MSG msg;
    INT_PTR result;
    if (!dlg)
        return -1;
    dlg->is_modal = 1;
    while (!dlg->dlg_ended && GetMessageA(&msg, NULL, 0, 0)) {
        if (IsDialogMessageA(dlg, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    result = dlg->dlg_result;
    if (reenable && owner)
        EnableWindow(owner, TRUE);
    if (!dlg->destroyed)
        DestroyWindow(dlg);
    return result;
}

/* ---- accelerators ---------------------------------------------------------
 *
 * A table of key combinations and the command each sends. The app offers its
 * messages here before dispatching, and a match becomes WM_COMMAND with the
 * accelerator's id — with 1 in the high word, which is how a handler tells an
 * accelerator from a menu pick or a button.
 */

struct ween_accel {
    ACCEL *entry;
    int count;
};

HACCEL CreateAcceleratorTableA(LPACCEL entries, int count)
{
    if (!entries || count <= 0)
        return NULL;
    struct ween_accel *t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->entry = malloc((size_t)count * sizeof(*t->entry));
    if (!t->entry) {
        free(t);
        return NULL;
    }
    memcpy(t->entry, entries, (size_t)count * sizeof(*t->entry));
    t->count = count;
    return t;
}

BOOL DestroyAcceleratorTable(HACCEL table)
{
    if (!table)
        return FALSE;
    free(table->entry);
    free(table);
    return TRUE;
}

int TranslateAcceleratorA(HWND wnd, HACCEL table, LPMSG msg)
{
    if (!wnd || !table || !msg || msg->message != WM_KEYDOWN)
        return 0;
    /* the pump packs the modifiers into lParam: Shift in bit 0, Ctrl in 28,
     * Alt in 29 */
    int shift = (msg->lParam & 1) != 0;
    int ctrl = (msg->lParam & (1L << 28)) != 0;
    int alt = (msg->lParam & (1L << 29)) != 0;
    for (int i = 0; i < table->count; i++) {
        const ACCEL *a = &table->entry[i];
        if (a->key != (WORD)msg->wParam)
            continue;
        if (!!(a->fVirt & FSHIFT) != shift || !!(a->fVirt & FCONTROL) != ctrl ||
            !!(a->fVirt & FALT) != alt)
            continue;
        /* 1 in the high word says an accelerator sent this, not a menu */
        SendMessageA(wnd, WM_COMMAND, MAKEWPARAM(a->cmd, 1), 0);
        return 1;
    }
    return 0;
}

/* ---- MessageBoxA ----------------------------------------------------------
 *
 * The one dialog every app has. It is built here rather than from a template
 * because its size comes from the text: win32 measures the message, wraps it,
 * and sizes the box around it, then centres the buttons under it.
 */

#define MB_BTN_W 75    /* the classic 50x14 DLU button, in pixels at 96 dpi */
#define MB_BTN_H 23
#define MB_BTN_GAP 6

/* Break the message where it will not fit. win32 wraps a message box's text
 * at a width it works out from the screen; this takes a maximum and breaks at
 * the last space before it, which is the same thing for anything a program
 * actually says. The caller's own newlines are kept. */
#define MB_WRAP_W 340
static void wrap_text(const char *text, const ween_strike *f, char *out,
                      size_t max)
{
    size_t n = 0;
    const char *line = text;   /* where the line being built started */
    const char *space = NULL;  /* the last place it could be broken */
    for (const char *p = text;; p++) {
        if (*p == ' ')
            space = p;
        if (*p && *p != '\n' &&
            ween_strike_text_extent(f, line, (int)(p - line + 1)) <= MB_WRAP_W)
            continue;
        if (*p && *p != '\n' && space && space > line) {
            p = space; /* back up to the space and break there */
        } else if (*p && *p != '\n') {
            /* one word longer than the line: let it run over */
            while (*p && *p != ' ' && *p != '\n')
                p++;
        }
        for (const char *q = line; q < p && n < max - 2; q++)
            out[n++] = *q;
        if (!*p)
            break;
        if (n < max - 2)
            out[n++] = '\n';
        line = p + 1;
        space = NULL;
    }
    out[n] = 0;
}

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
    int ids[3] = { IDOK, IDCANCEL, 0 };
    const char *labels[3] = { "OK", "Cancel", NULL };

    char wrapped[2048];
    int icon = 0;
    switch (type & MB_ICONMASK) {
    case MB_ICONHAND:
        icon = WEEN_MB_ICON_ERROR;
        break;
    case MB_ICONQUESTION:
        icon = WEEN_MB_ICON_QUESTION;
        break;
    case MB_ICONEXCLAMATION:
        icon = WEEN_MB_ICON_WARNING;
        break;
    case MB_ICONASTERISK:
        icon = WEEN_MB_ICON_INFO;
        break;
    default:
        break;
    }

    if (!text)
        text = "";
    wrap_text(text, f, wrapped, sizeof(wrapped));
    text = wrapped;
    lines = line_count(text, f, &widest);
    if ((type & MB_TYPEMASK) == MB_YESNOCANCEL) {
        /* the three-button question: save, throw away, or think again */
        nbuttons = 3;
        ids[0] = IDYES;
        ids[1] = IDNO;
        ids[2] = IDCANCEL;
        labels[0] = "&Yes";
        labels[1] = "&No";
        labels[2] = "Cancel";
    } else if ((type & MB_TYPEMASK) == MB_YESNO) {
        nbuttons = 2;
        ids[0] = IDYES;
        ids[1] = IDNO;
        labels[0] = "&Yes";
        labels[1] = "&No";
    }

    /* The picture, when there is one, stands at the left and the message is
     * inset past it — and the box is at least as tall as the picture. */
    int gutter = icon ? MB_ICON_W + MB_ICON_GAP : 0;
    int text_h = lines * (cell + 2);
    int buttons_w = nbuttons * MB_BTN_W + (nbuttons - 1) * MB_BTN_GAP;
    int client_w = gutter + widest + 2 * MB_MARGIN;
    if (client_w < buttons_w + 2 * MB_MARGIN)
        client_w = buttons_w + 2 * MB_MARGIN;
    if (icon && text_h < MB_ICON_W)
        text_h = MB_ICON_W;
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
            CreateWindowExA(0, "STATIC", line, WS_CHILD | WS_VISIBLE,
                            MB_MARGIN + gutter, ly, widest + 2, cell + 2, box,
                            NULL, NULL, NULL);
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
    box->msgbox_icon = icon; /* drawn by the box's own paint */

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
