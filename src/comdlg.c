/* The common dialogs: the one that asks for a file name and the one that
 * asks for a colour.
 *
 * These belong to the system, not to the application — every program that
 * opens a file gets the same dialog, which is the whole point of COMDLG32 —
 * so an application written to win32 calls GetOpenFileNameA and ChooseColorA
 * and gets whatever the system puts up. On Windows that is the shell's
 * browser with its places bar; here it is the plain one the API had before
 * that, built from a dialog template out of the controls ween32 already has.
 * Same call, same result in the caller's buffer.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ween_internal.h"

/* ---- building a template ---------------------------------------------- */

typedef struct {
    DWORD style;
    DWORD ex_style;
    short x, y, cx, cy;
    WORD id;
    WORD cls;          /* a predefined class atom... */
    const char *cls_name; /* ...or a name, for one of our own */
    const char *text;
} dlg_item;

#define ATOM_BUTTON 0x0080
#define ATOM_EDIT 0x0081
#define ATOM_STATIC 0x0082
#define ATOM_LISTBOX 0x0083
#define ATOM_COMBOBOX 0x0085

static unsigned char *tp;

static void tw(WORD v)
{
    *tp++ = (unsigned char)v;
    *tp++ = (unsigned char)(v >> 8);
}

static void td(DWORD v)
{
    tw((WORD)v);
    tw((WORD)(v >> 16));
}

static void ts(const char *s)
{
    if (s)
        while (*s)
            tw((unsigned char)*s++);
    tw(0);
}

static void talign(unsigned char *base)
{
    while ((size_t)(tp - base) & 3)
        *tp++ = 0;
}

static DLGTEMPLATE *build(void *buf, DWORD style, short cx, short cy,
                          const char *title, const dlg_item *items, int n)
{
    unsigned char *base = buf;
    tp = base;
    td(style);
    td(0);
    tw((WORD)n);
    tw(0);
    tw(0);
    tw((WORD)cx);
    tw((WORD)cy);
    tw(0);
    tw(0);
    ts(title);
    if (style & DS_SETFONT) {
        /* a template that says DS_SETFONT carries its face after the title */
        tw(8);
        ts("MS Sans Serif");
    }
    for (int i = 0; i < n; i++) {
        talign(base);
        td(items[i].style);
        td(items[i].ex_style);
        tw((WORD)items[i].x);
        tw((WORD)items[i].y);
        tw((WORD)items[i].cx);
        tw((WORD)items[i].cy);
        tw(items[i].id);
        if (items[i].cls_name) {
            ts(items[i].cls_name);
        } else {
            tw(0xFFFF);
            tw(items[i].cls);
        }
        ts(items[i].text);
        tw(0);
    }
    return (DLGTEMPLATE *)base;
}

/* ---- the file dialog ---------------------------------------------------- */

#define IDC_FILE_LIST 0x0460
#define IDC_FILE_NAME 0x0480
#define IDC_FILE_TYPE 0x0470
#define IDC_FILE_DIR 0x0471
#define IDC_FILE_UP 0x0472

static struct {
    OPENFILENAMEA *ofn;
    int saving;
    char dir[1024];
    char pick[1400];
} g_fd;

/* The extensions the caller's filter allows, as a list of "*.bmp" patterns.
 * The filter is the win32 double-NUL-terminated list of label/pattern pairs;
 * only the selected pair is used. */
static const char *filter_patterns(void)
{
    const char *p = g_fd.ofn->lpstrFilter;
    unsigned want = g_fd.ofn->nFilterIndex ? g_fd.ofn->nFilterIndex - 1 : 0;
    if (!p)
        return "*.*";
    for (unsigned i = 0; *p; i++) {
        const char *label = p;
        p += strlen(p) + 1; /* past the label */
        (void)label;
        if (!*p)
            break;
        if (i == want)
            return p;
        p += strlen(p) + 1; /* past the pattern */
    }
    return "*.*";
}

static int matches(const char *name, const char *patterns)
{
    char one[64];
    const char *p = patterns;
    while (*p) {
        const char *semi = strchr(p, ';');
        size_t n = semi ? (size_t)(semi - p) : strlen(p);
        if (n < sizeof one) {
            memcpy(one, p, n);
            one[n] = 0;
            if (strcmp(one, "*.*") == 0 || strcmp(one, "*") == 0)
                return 1;
            if (one[0] == '*' && one[1] == '.') {
                size_t ln = strlen(name), en = strlen(one + 1);
                if (ln > en && lstrcmpiA(name + ln - en, one + 1) == 0)
                    return 1;
            } else if (lstrcmpiA(name, one) == 0) {
                return 1;
            }
        }
        if (!semi)
            break;
        p = semi + 1;
    }
    return 0;
}

/* Directories first, in square brackets as the old dialog wrote them, then
 * the files the filter allows. */
static void fill_list(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_FILE_LIST);
    const char *patterns = filter_patterns();
    DIR *d;
    struct dirent *e;
    char line[300];

    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    SetDlgItemTextA(dlg, IDC_FILE_DIR, g_fd.dir);
    d = opendir(g_fd.dir);
    if (!d)
        return;
    while ((e = readdir(d))) {
        struct stat st;
        char full[1400];
        if (strcmp(e->d_name, ".") == 0)
            continue;
        snprintf(full, sizeof full, "%s/%s", g_fd.dir, e->d_name);
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            snprintf(line, sizeof line, "[%s]", e->d_name);
            SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)line);
        }
    }
    closedir(d);
    d = opendir(g_fd.dir);
    if (!d)
        return;
    while ((e = readdir(d))) {
        struct stat st;
        char full[1400];
        snprintf(full, sizeof full, "%s/%s", g_fd.dir, e->d_name);
        if (stat(full, &st) != 0 || S_ISDIR(st.st_mode))
            continue;
        if (!matches(e->d_name, patterns))
            continue;
        SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)e->d_name);
    }
    closedir(d);
}

/* Follow a directory, or take a file name. */
static void chosen(HWND dlg, const char *name)
{
    if (name[0] == '[') {
        char sub[300];
        size_t n = strlen(name);
        if (n < 3)
            return;
        memcpy(sub, name + 1, n - 2);
        sub[n - 2] = 0;
        if (strcmp(sub, "..") == 0) {
            char *slash = strrchr(g_fd.dir, '/');
            if (slash && slash != g_fd.dir)
                *slash = 0;
            else if (slash)
                slash[1] = 0;
        } else {
            size_t len = strlen(g_fd.dir);
            snprintf(g_fd.dir + len, sizeof g_fd.dir - len, "%s%s",
                     len && g_fd.dir[len - 1] == '/' ? "" : "/", sub);
        }
        fill_list(dlg);
        SetDlgItemTextA(dlg, IDC_FILE_NAME, "");
        return;
    }
    SetDlgItemTextA(dlg, IDC_FILE_NAME, name);
}

/* Put the typed name together with the directory and hand it back. */
static int accept(HWND dlg)
{
    char name[300];
    GetDlgItemTextA(dlg, IDC_FILE_NAME, name, sizeof name);
    if (!name[0])
        return 0;
    if (name[0] == '[') {
        chosen(dlg, name);
        return 0;
    }
    if (name[0] != '/') {
        size_t len = strlen(g_fd.dir);
        snprintf(g_fd.pick, sizeof g_fd.pick, "%s%s%s", g_fd.dir,
                 len && g_fd.dir[len - 1] == '/' ? "" : "/", name);
    } else {
        snprintf(g_fd.pick, sizeof g_fd.pick, "%s", name);
    }
    /* the default extension, if the name has none */
    if (g_fd.ofn->lpstrDefExt && !strrchr(name, '.')) {
        size_t len = strlen(g_fd.pick);
        snprintf(g_fd.pick + len, sizeof g_fd.pick - len, ".%s",
                 g_fd.ofn->lpstrDefExt);
    }
    if (!g_fd.saving && (g_fd.ofn->Flags & OFN_FILEMUSTEXIST)) {
        struct stat st;
        if (stat(g_fd.pick, &st) != 0) {
            MessageBoxA(dlg, "The file could not be found.", "Open",
                        MB_OK | MB_ICONERROR);
            return 0;
        }
    }
    return 1;
}

static INT_PTR CALLBACK file_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        HWND type = GetDlgItem(dlg, IDC_FILE_TYPE);
        const char *p = g_fd.ofn->lpstrFilter;
        fill_list(dlg);
        if (g_fd.ofn->lpstrFile && g_fd.ofn->lpstrFile[0]) {
            const char *slash = strrchr(g_fd.ofn->lpstrFile, '/');
            SetDlgItemTextA(dlg, IDC_FILE_NAME,
                            slash ? slash + 1 : g_fd.ofn->lpstrFile);
        }
        while (p && *p) { /* the filter's labels go in the combo box */
            SendMessageA(type, CB_ADDSTRING, 0, (LPARAM)p);
            p += strlen(p) + 1;
            if (!*p)
                break;
            p += strlen(p) + 1;
        }
        SendMessageA(type, CB_SETCURSEL,
                     g_fd.ofn->nFilterIndex ? g_fd.ofn->nFilterIndex - 1 : 0, 0);
        SetFocus(GetDlgItem(dlg, IDC_FILE_NAME));
        return 0; /* the focus is ours */
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if (id == IDC_FILE_LIST && code == LBN_SELCHANGE) {
            char name[300];
            int sel = (int)SendMessageA(GetDlgItem(dlg, IDC_FILE_LIST),
                                        LB_GETCURSEL, 0, 0);
            if (sel >= 0 &&
                SendMessageA(GetDlgItem(dlg, IDC_FILE_LIST), LB_GETTEXT,
                             (WPARAM)sel, (LPARAM)name) >= 0 &&
                name[0] != '[')
                SetDlgItemTextA(dlg, IDC_FILE_NAME, name);
            return TRUE;
        }
        if (id == IDC_FILE_LIST && code == LBN_DBLCLK) {
            char name[300];
            int sel = (int)SendMessageA(GetDlgItem(dlg, IDC_FILE_LIST),
                                        LB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                SendMessageA(GetDlgItem(dlg, IDC_FILE_LIST), LB_GETTEXT,
                             (WPARAM)sel, (LPARAM)name);
                chosen(dlg, name);
                if (name[0] != '[' && accept(dlg))
                    EndDialog(dlg, 1);
            }
            return TRUE;
        }
        if (id == IDC_FILE_TYPE && code == CBN_SELCHANGE) {
            int sel = (int)SendMessageA(GetDlgItem(dlg, IDC_FILE_TYPE),
                                        CB_GETCURSEL, 0, 0);
            g_fd.ofn->nFilterIndex = (DWORD)(sel < 0 ? 1 : sel + 1);
            fill_list(dlg);
            return TRUE;
        }
        if (id == IDC_FILE_UP) {
            chosen(dlg, "[..]");
            return TRUE;
        }
        if (id == IDOK) {
            if (accept(dlg))
                EndDialog(dlg, 1);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, 0);
            return TRUE;
        }
        return FALSE;
    }
    default:
        return FALSE;
    }
}

static BOOL run_file_dialog(OPENFILENAMEA *ofn, int saving)
{
    static unsigned char buf[4096] __attribute__((aligned(4)));
    const DWORD child = WS_CHILD | WS_VISIBLE;
    dlg_item items[] = {
        { child | SS_LEFT, 0, 7, 9, 40, 8, 0, ATOM_STATIC, NULL, "Look &in:" },
        { child | SS_LEFT, WS_EX_CLIENTEDGE, 50, 7, 150, 12, IDC_FILE_DIR,
          ATOM_STATIC, NULL, "" },
        { child | WS_TABSTOP | BS_PUSHBUTTON, 0, 206, 7, 30, 12, IDC_FILE_UP,
          ATOM_BUTTON, NULL, "&Up" },
        { child | WS_TABSTOP | LBS_NOTIFY, WS_EX_CLIENTEDGE, 7, 24, 229, 90,
          IDC_FILE_LIST, ATOM_LISTBOX, NULL, "" },
        { child | SS_LEFT, 0, 7, 121, 45, 8, 0, ATOM_STATIC, NULL, "File &name:" },
        { child | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 55, 119, 130,
          12, IDC_FILE_NAME, ATOM_EDIT, NULL, "" },
        { child | SS_LEFT, 0, 7, 139, 45, 8, 0, ATOM_STATIC, NULL, "Files of &type:" },
        { child | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 55, 137, 130, 60,
          IDC_FILE_TYPE, ATOM_COMBOBOX, NULL, "" },
        { child | WS_TABSTOP | WS_GROUP | BS_DEFPUSHBUTTON, 0, 192, 119, 44, 14,
          IDOK, ATOM_BUTTON, NULL, saving ? "&Save" : "&Open" },
        { child | WS_TABSTOP | BS_PUSHBUTTON, 0, 192, 137, 44, 14, IDCANCEL,
          ATOM_BUTTON, NULL, "Cancel" },
    };
    DLGTEMPLATE *tmpl;

    if (!ofn || !ofn->lpstrFile)
        return FALSE;
    g_fd.ofn = ofn;
    g_fd.saving = saving;
    g_fd.pick[0] = 0;
    if (ofn->lpstrInitialDir && ofn->lpstrInitialDir[0])
        snprintf(g_fd.dir, sizeof g_fd.dir, "%s", ofn->lpstrInitialDir);
    else if (!getcwd(g_fd.dir, sizeof g_fd.dir))
        snprintf(g_fd.dir, sizeof g_fd.dir, "/");

    tmpl = build(buf,
                 WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
                     DS_SETFONT | DS_3DLOOK,
                 243, 158,
                 ofn->lpstrTitle ? ofn->lpstrTitle : (saving ? "Save As" : "Open"),
                 items, (int)(sizeof items / sizeof items[0]));
    if (DialogBoxIndirectParamA(NULL, tmpl, ofn->hwndOwner, file_proc, 0) != 1)
        return FALSE;

    snprintf(ofn->lpstrFile, ofn->nMaxFile ? ofn->nMaxFile : sizeof g_fd.pick,
             "%s", g_fd.pick);
    {
        const char *slash = strrchr(ofn->lpstrFile, '/');
        ofn->nFileOffset = (WORD)(slash ? slash + 1 - ofn->lpstrFile : 0);
        if (ofn->lpstrFileTitle && ofn->nMaxFileTitle)
            snprintf(ofn->lpstrFileTitle, ofn->nMaxFileTitle, "%s",
                     ofn->lpstrFile + ofn->nFileOffset);
    }
    return TRUE;
}

BOOL GetOpenFileNameA(OPENFILENAMEA *ofn)
{
    return run_file_dialog(ofn, 0);
}

BOOL GetSaveFileNameA(OPENFILENAMEA *ofn)
{
    return run_file_dialog(ofn, 1);
}

/* ---- the colour dialog -------------------------------------------------- */

#define IDC_COLOR_BASIC 0x0710
#define IDC_COLOR_CUSTOM 0x0711
#define IDC_COLOR_FIELD 0x0712  /* the hue/saturation square */
#define IDC_COLOR_LUM 0x0713    /* the brightness bar beside it */
#define IDC_COLOR_SAMPLE 0x0714 /* colour|solid */
#define IDC_COLOR_DEFINE 0x0715
#define IDC_COLOR_ADD 0x0716
#define IDC_COLOR_RED 0x0706
#define IDC_COLOR_GREEN 0x0707
#define IDC_COLOR_BLUE 0x0708
#define IDC_COLOR_HUE 0x0703
#define IDC_COLOR_SAT 0x0704
#define IDC_COLOR_LUMEDIT 0x0705

/* The 48 the dialog has always offered, in the order it shows them: six
 * columns by eight rows, read across. */
static const COLORREF g_basic[48] = {
    RGB(255, 128, 128), RGB(255, 255, 128), RGB(128, 255, 128),
    RGB(0, 255, 128),   RGB(128, 255, 255), RGB(0, 128, 255),
    RGB(255, 128, 192), RGB(255, 128, 255), RGB(255, 0, 0),
    RGB(255, 255, 0),   RGB(128, 255, 0),   RGB(0, 255, 64),
    RGB(0, 255, 255),   RGB(0, 128, 192),   RGB(128, 128, 192),
    RGB(255, 0, 255),   RGB(128, 64, 64),   RGB(255, 128, 64),
    RGB(0, 255, 0),     RGB(0, 128, 128),   RGB(0, 64, 128),
    RGB(128, 128, 255), RGB(128, 0, 64),    RGB(255, 0, 128),
    RGB(128, 0, 0),     RGB(255, 128, 0),   RGB(0, 128, 0),
    RGB(0, 128, 64),    RGB(0, 0, 255),     RGB(0, 0, 160),
    RGB(128, 0, 128),   RGB(128, 0, 255),   RGB(64, 0, 0),
    RGB(128, 64, 0),    RGB(0, 64, 0),      RGB(0, 64, 64),
    RGB(0, 0, 128),     RGB(0, 0, 64),      RGB(64, 0, 64),
    RGB(64, 0, 128),    RGB(0, 0, 0),       RGB(128, 128, 0),
    RGB(128, 128, 64),  RGB(128, 128, 128), RGB(64, 128, 128),
    RGB(192, 192, 192), RGB(64, 0, 64),     RGB(255, 255, 255)
};

static struct {
    CHOOSECOLORA *cc;
    COLORREF chosen;
    int expanded;
    int hue, sat, lum; /* 0..240, as the dialog has always scaled them */
} g_cc;

/* The dialog works in hue, saturation and luminosity, and its own scale for
 * them is 0..240. These are the conversions it has always used. */
static void rgb_to_hsl(COLORREF c, int *h, int *s, int *l)
{
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    int max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int sum = max + min, diff = max - min;
    *l = (sum * 240 + 255) / 510;
    if (!diff) {
        *h = 160;
        *s = 0;
        return;
    }
    *s = sum <= 255 ? (diff * 240 + sum / 2) / sum
                    : (diff * 240 + (510 - sum) / 2) / (510 - sum);
    if (max == r)
        *h = ((g - b) * 40 + diff / 2) / diff;
    else if (max == g)
        *h = 80 + ((b - r) * 40 + diff / 2) / diff;
    else
        *h = 160 + ((r - g) * 40 + diff / 2) / diff;
    if (*h < 0)
        *h += 240;
}

static int hue_to_channel(int n1, int n2, int hue)
{
    if (hue > 240)
        hue -= 240;
    if (hue < 0)
        hue += 240;
    if (hue < 40)
        return n1 + ((n2 - n1) * hue + 20) / 40;
    if (hue < 120)
        return n2;
    if (hue < 160)
        return n1 + ((n2 - n1) * (160 - hue) + 20) / 40;
    return n1;
}

static COLORREF hsl_to_rgb(int h, int s, int l)
{
    int n1, n2, r, g, b;
    if (!s) {
        /* The grey case truncates where the coloured one rounds, which is
         * what the classic HLStoRGB does and what the machine's brightness
         * bar comes out as: luminance 232 is 246, not 247. */
        int v = l * 255 / 240;
        return RGB(v, v, v);
    }
    n2 = l <= 120 ? (l * (240 + s) + 120) / 240 : l + s - (l * s + 120) / 240;
    n1 = 2 * l - n2;
    r = (hue_to_channel(n1, n2, h + 80) * 255 + 120) / 240;
    g = (hue_to_channel(n1, n2, h) * 255 + 120) / 240;
    b = (hue_to_channel(n1, n2, h - 80) * 255 + 120) / 240;
    return RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

/* The grid of swatches, and the two pictures beside the sliders, are drawn by
 * the dialog rather than being controls: they are owner-drawn statics. */
/* One square of a grid of them, measured off the machine: a twenty by
 * seventeen sunken box three pixels in from the control's corner, on a
 * pitch of twenty-five across and twenty-two down, with the colour itself
 * filling the sixteen by thirteen inside the edge. */
#define SWATCH_W 20
#define SWATCH_H 17
#define SWATCH_PITCH_X 25
#define SWATCH_PITCH_Y 22
#define SWATCH_INSET 3

static void swatch_cell(int i, int cols, RECT *r)
{
    r->left = SWATCH_INSET + (i % cols) * SWATCH_PITCH_X;
    r->top = SWATCH_INSET + (i / cols) * SWATCH_PITCH_Y;
    r->right = r->left + SWATCH_W;
    r->bottom = r->top + SWATCH_H;
}

static void draw_swatches(HWND dlg, HDC dc, HWND ctl, const COLORREF *colors,
                          int count, int cols, COLORREF picked)
{
    (void)dlg;
    (void)ctl;
    for (int i = 0; i < count; i++) {
        RECT cell, in;
        HBRUSH br;
        swatch_cell(i, cols, &cell);
        DrawEdge(dc, &cell, EDGE_SUNKEN, BF_RECT);
        in.left = cell.left + 2;
        in.top = cell.top + 2;
        in.right = cell.right - 2;
        in.bottom = cell.bottom - 2;
        br = CreateSolidBrush(colors[i]);
        FillRect(dc, &in, br);
        DeleteObject(br);
        if (colors[i] == picked) {
            /* the one in hand wears a black rectangle outside its edge */
            RECT out = cell;
            out.left--;
            out.top--;
            out.right++;
            out.bottom++;
            FrameRect(dc, &out, GetSysColorBrush(COLOR_WINDOWFRAME));
        }
    }
}

static COLORREF g_custom[16];

/* How wide the dialog is in dialog units, shut and open. The template is
 * the open one: the definition half is in it from the start and the window
 * is narrowed over it, which is how the real one hides it. */
#define COLOR_DLG_SHUT 144
#define COLOR_DLG_OPEN 298

/* Narrow the dialog to the left half, or widen it back. */
static void color_show_half(HWND dlg, int open)
{
    static const int half_ids[] = {
        IDC_COLOR_FIELD, IDC_COLOR_LUM,      IDC_COLOR_SAMPLE, IDC_COLOR_HUE,
        IDC_COLOR_SAT,   IDC_COLOR_LUMEDIT,  IDC_COLOR_RED,    IDC_COLOR_GREEN,
        IDC_COLOR_BLUE,  IDC_COLOR_ADD,
    };
    RECT r = { 0, 0, open ? COLOR_DLG_OPEN : COLOR_DLG_SHUT, 184 };
    MapDialogRect(dlg, &r);
    AdjustWindowRect(&r, GetWindowLongA(dlg, GWL_STYLE), FALSE);
    RECT was;
    GetWindowRect(dlg, &was);
    MoveWindow(dlg, was.left, was.top, r.right - r.left, r.bottom - r.top,
               TRUE);
    for (size_t i = 0; i < sizeof half_ids / sizeof half_ids[0]; i++)
        EnableWindow(GetDlgItem(dlg, half_ids[i]), open ? TRUE : FALSE);
    /* the button that asked goes grey, as it does on the machine */
    EnableWindow(GetDlgItem(dlg, IDC_COLOR_DEFINE), open ? FALSE : TRUE);
    InvalidateRect(dlg, NULL, TRUE);
}

static INT_PTR CALLBACK color_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    /* An application that asked for a hook sees every message before the
     * dialog does, and saying so ends it there: that is how a program gives
     * the system's colour box a title of its own. */
    if (g_cc.cc && (g_cc.cc->Flags & CC_ENABLEHOOK) && g_cc.cc->lpfnHook) {
        INT_PTR r = g_cc.cc->lpfnHook(dlg, msg, wp, lp);
        if (r)
            return r;
    }
    switch (msg) {
    case WM_INITDIALOG: {
        rgb_to_hsl(g_cc.chosen, &g_cc.hue, &g_cc.sat, &g_cc.lum);
        /* CC_FULLOPEN asks for it open; anything else starts it shut. */
        color_show_half(dlg, (g_cc.cc->Flags & CC_FULLOPEN) != 0);
        return 1;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlID == IDC_COLOR_BASIC)
            draw_swatches(dlg, di->hDC, di->hwndItem, g_basic, 48, 8,
                          g_cc.chosen);
        else if (di->CtlID == IDC_COLOR_CUSTOM)
            draw_swatches(dlg, di->hDC, di->hwndItem, g_custom, 16, 8,
                          g_cc.chosen);
        else if (di->CtlID == IDC_COLOR_SAMPLE) {
            HBRUSH br = CreateSolidBrush(g_cc.chosen);
            FillRect(di->hDC, &di->rcItem, br);
            DeleteObject(br);
            FrameRect(di->hDC, &di->rcItem, GetSysColorBrush(COLOR_WINDOWTEXT));
        }
        return TRUE;
    }
    case WM_PAINT: {
        /* the numbers, which follow whatever was last picked */
        char buf[16];
        RECT bar;
        snprintf(buf, sizeof buf, "%d", GetRValue(g_cc.chosen));
        SetDlgItemTextA(dlg, IDC_COLOR_RED, buf);
        snprintf(buf, sizeof buf, "%d", GetGValue(g_cc.chosen));
        SetDlgItemTextA(dlg, IDC_COLOR_GREEN, buf);
        snprintf(buf, sizeof buf, "%d", GetBValue(g_cc.chosen));
        SetDlgItemTextA(dlg, IDC_COLOR_BLUE, buf);
        snprintf(buf, sizeof buf, "%d", g_cc.hue);
        SetDlgItemTextA(dlg, IDC_COLOR_HUE, buf);
        snprintf(buf, sizeof buf, "%d", g_cc.sat);
        SetDlgItemTextA(dlg, IDC_COLOR_SAT, buf);
        snprintf(buf, sizeof buf, "%d", g_cc.lum);
        SetDlgItemTextA(dlg, IDC_COLOR_LUMEDIT, buf);
        /* The dialog paints itself, and then the arrow beside the
         * brightness bar: it points at the luminance in hand from outside
         * the bar, so it belongs to the dialog rather than to the control. */
        DefWindowProcA(dlg, WM_PAINT, wp, lp);
        if (IsWindowEnabled(GetDlgItem(dlg, IDC_COLOR_LUM))) {
            HWND lum = GetDlgItem(dlg, IDC_COLOR_LUM);
            HDC dc = GetDC(dlg);
            HBRUSH ink = GetSysColorBrush(COLOR_WINDOWFRAME);
            int ox, oy, cox, coy, h;
            GetClientRect(lum, &bar);
            ween_client_origin(lum, &ox, &oy);
            ween_client_origin(dlg, &cox, &coy);
            ox -= cox;
            oy -= coy;
            h = bar.bottom;
            int ay = oy + ((240 - g_cc.lum) * (h - 1) + 120) / 240;
            int ax = ox + bar.right + 2;
            for (int i = 0; i < 5; i++) {
                RECT c;
                c.left = ax + i;
                c.right = ax + i + 1;
                c.top = ay - i;
                c.bottom = ay + i + 1;
                FillRect(dc, &c, ink);
            }
            ReleaseDC(dlg, dc);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDOK) {
            EndDialog(dlg, 1);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, 0);
            return TRUE;
        }
        if (id == IDC_COLOR_DEFINE) {
            color_show_half(dlg, 1);
            return TRUE;
        }
        if (id == IDC_COLOR_ADD) {
            for (int i = 0; i < 16; i++) {
                if (g_custom[i] == RGB(255, 255, 255) || i == 15) {
                    g_custom[i] = g_cc.chosen;
                    break;
                }
            }
            InvalidateRect(dlg, NULL, FALSE);
            return TRUE;
        }
        return FALSE;
    }
    case WM_LBUTTONDOWN: {
        /* which swatch was hit: the two grids are child windows, so the
         * point is theirs rather than the dialog's */
        return FALSE;
    }
    default:
        return FALSE;
    }
}

/* The hue-and-saturation field: hue across, saturation up. Clicking in it
 * picks both at once, which is what makes it worth having. */
#define FIELD_COLS 60
#define FIELD_ROWS 30
static LRESULT CALLBACK field_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RECT r;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        GetClientRect(wnd, &r);
        /* Not a pixel at a time: the machine fills it as a grid of sixty
         * blocks across and thirty down -- hue every four of its two hundred
         * and forty, saturation every eight -- and the bands are visible, so
         * a smooth one would be wrong. */
        for (int j = 0; j < FIELD_ROWS; j++) {
            for (int i = 0; i < FIELD_COLS; i++) {
                RECT band;
                HBRUSH br;
                band.left = i * r.right / FIELD_COLS;
                band.right = (i + 1) * r.right / FIELD_COLS;
                band.top = j * r.bottom / FIELD_ROWS;
                band.bottom = (j + 1) * r.bottom / FIELD_ROWS;
                br = CreateSolidBrush(hsl_to_rgb(i * (240 / FIELD_COLS),
                                                 240 - j * (240 / FIELD_ROWS),
                                                 120));
                FillRect(dc, &band, br);
                DeleteObject(br);
            }
        }
        {
            /* Where the colour in hand sits, marked as the machine marks
             * it: a black cross with the middle left out -- arms four long
             * starting five away, the upright three thick and the crossbar
             * two -- so what is under it can still be seen. */
            int cx = (g_cc.hue * r.right + 120) / 240;
            int cy = ((240 - g_cc.sat) * r.bottom + 120) / 240;
            if (cx > r.right - 1)
                cx = r.right - 1;
            if (cy > r.bottom - 1)
                cy = r.bottom - 1;
            HBRUSH ink = GetSysColorBrush(COLOR_WINDOWFRAME);
            RECT c;
            c.left = cx - 1; c.right = cx + 2;
            c.top = cy - 9; c.bottom = cy - 4;
            FillRect(dc, &c, ink);
            c.top = cy + 5; c.bottom = cy + 10;
            FillRect(dc, &c, ink);
            c.top = cy - 1; c.bottom = cy + 1;
            c.left = cx - 9; c.right = cx - 4;
            FillRect(dc, &c, ink);
            c.left = cx + 5; c.right = cx + 10;
            FillRect(dc, &c, ink);
        }
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
        if (msg == WM_MOUSEMOVE && !(wp & MK_LBUTTON))
            return 0;
        GetClientRect(wnd, &r);
        {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= r.right) x = r.right - 1;
            if (y >= r.bottom) y = r.bottom - 1;
            g_cc.hue = x * 240 / r.right;
            g_cc.sat = 240 - y * 240 / r.bottom;
            g_cc.chosen = hsl_to_rgb(g_cc.hue, g_cc.sat, g_cc.lum);
            InvalidateRect(GetParent(wnd), NULL, FALSE);
        }
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* Where the bar's second band starts: the first is that much shorter than
 * the rest, on the machine. */
#define LUM_BAND_TOP 4

/* The brightness bar beside it. */
static LRESULT CALLBACK lum_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RECT r;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        GetClientRect(wnd, &r);
        /* Banded like the field beside it: thirty-one steps of eight
         * luminance each, on the same grid as the field's rows but pushed
         * four pixels down, which is where the machine's bands start. */
        for (int j = 0; j <= FIELD_ROWS; j++) {
            RECT row;
            HBRUSH br = CreateSolidBrush(
                hsl_to_rgb(g_cc.hue, g_cc.sat, 240 - j * (240 / FIELD_ROWS)));
            row.left = 0;
            row.right = r.right;
            row.top = j ? LUM_BAND_TOP + (j - 1) * r.bottom / FIELD_ROWS : 0;
            row.bottom = j < FIELD_ROWS
                             ? LUM_BAND_TOP + j * r.bottom / FIELD_ROWS
                             : r.bottom;
            FillRect(dc, &row, br);
            DeleteObject(br);
        }
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
        if (msg == WM_MOUSEMOVE && !(wp & MK_LBUTTON))
            return 0;
        GetClientRect(wnd, &r);
        {
            int y = GET_Y_LPARAM(lp);
            if (y < 0) y = 0;
            if (y >= r.bottom) y = r.bottom - 1;
            g_cc.lum = 240 - y * 240 / (r.bottom - 1);
            g_cc.chosen = hsl_to_rgb(g_cc.hue, g_cc.sat, g_cc.lum);
            InvalidateRect(GetParent(wnd), NULL, FALSE);
        }
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* The colour as it will be. */
static LRESULT CALLBACK sample_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT r;
        HBRUSH br = CreateSolidBrush(g_cc.chosen);
        GetClientRect(wnd, &r);
        FillRect(dc, &r, br);
        FrameRect(dc, &r, GetSysColorBrush(COLOR_WINDOWTEXT));
        DeleteObject(br);
        EndPaint(wnd, &ps);
        return 0;
    }
    return DefWindowProcA(wnd, msg, wp, lp);
}

/* A swatch grid is a child window that draws itself and reports a click. */
static LRESULT CALLBACK swatch_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT all;
        GetClientRect(wnd, &all);
        FillRect(dc, &all, GetSysColorBrush(COLOR_BTNFACE));
        if (GetDlgCtrlID(wnd) == IDC_COLOR_BASIC)
            draw_swatches(GetParent(wnd), dc, wnd, g_basic, 48, 8, g_cc.chosen);
        else
            draw_swatches(GetParent(wnd), dc, wnd, g_custom, 16, 8, g_cc.chosen);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int cols = 8, count, i;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        count = GetDlgCtrlID(wnd) == IDC_COLOR_BASIC ? 48 : 16;
        i = -1;
        for (int n = 0; n < count; n++) {
            RECT cell;
            swatch_cell(n, cols, &cell);
            if (x >= cell.left && x < cell.right && y >= cell.top &&
                y < cell.bottom) {
                i = n;
                break;
            }
        }
        if (i < 0)
            return 0;
        g_cc.chosen = GetDlgCtrlID(wnd) == IDC_COLOR_BASIC ? g_basic[i]
                                                           : g_custom[i];
        rgb_to_hsl(g_cc.chosen, &g_cc.hue, &g_cc.sat, &g_cc.lum);
        InvalidateRect(GetParent(wnd), NULL, FALSE);
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

BOOL ChooseColorA(CHOOSECOLORA *cc)
{
    static unsigned char buf[4096] __attribute__((aligned(4)));
    static int registered;
    const DWORD child = WS_CHILD | WS_VISIBLE;
    /* Every rectangle here is the machine's, in the dialog units its own
     * resource is written in: the probe walks a running Edit Colors and
     * tools/paint/dlu.py maps the pixels back. The right-hand half is in
     * the template from the start and simply left outside the window until
     * "Define Custom Colors" widens it, which is how the real one works. */
    const DWORD half = child | WS_DISABLED;
    dlg_item items[] = {
        { child | WS_GROUP | SS_LEFT, 0, 4, 4, 140, 9, 0, ATOM_STATIC, NULL,
          "&Basic colors:" },
        { child | WS_GROUP | WS_TABSTOP, 0, 4, 14, 140, 86, IDC_COLOR_BASIC, 0,
          "ween32ColorGrid", "" },
        { child | WS_GROUP | SS_LEFT, 0, 4, 106, 140, 9, 0, ATOM_STATIC, NULL,
          "&Custom colors:" },
        { child | WS_GROUP | WS_TABSTOP, 0, 4, 116, 140, 28, IDC_COLOR_CUSTOM,
          0, "ween32ColorGrid", "" },
        { child | WS_GROUP | WS_TABSTOP | BS_PUSHBUTTON, 0, 4, 150, 140, 14,
          IDC_COLOR_DEFINE, ATOM_BUTTON, NULL, "&Define Custom Colors >>" },
        { child | WS_GROUP | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 4, 166, 44, 14,
          IDOK, ATOM_BUTTON, NULL, "OK" },
        { child | WS_GROUP | WS_TABSTOP | BS_PUSHBUTTON, 0, 52, 166, 44, 14,
          IDCANCEL, ATOM_BUTTON, NULL, "Cancel" },
        /* the half that is not there until it is asked for */
        { half, WS_EX_STATICEDGE, 152, 4, 118, 116, IDC_COLOR_FIELD, 0,
          "ween32ColorField", "" },
        { half, WS_EX_STATICEDGE, 280, 4, 8, 116, IDC_COLOR_LUM, 0,
          "ween32ColorLum", "" },
        { half, WS_EX_STATICEDGE, 152, 124, 40, 26, IDC_COLOR_SAMPLE, 0,
          "ween32ColorSample", "" },
        { half | WS_GROUP | SS_RIGHT, 0, 152, 151, 20, 9, 0, ATOM_STATIC, NULL,
          "Color" },
        { half | SS_LEFT, 0, 172, 151, 20, 9, 0, ATOM_STATIC, NULL, "|S&olid" },
        { half | SS_RIGHT, 0, 194, 126, 20, 9, 0, ATOM_STATIC, NULL, "Hu&e:" },
        { half | WS_GROUP | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 216,
          124, 18, 12, IDC_COLOR_HUE, ATOM_EDIT, NULL, "" },
        { half | SS_RIGHT, 0, 194, 140, 20, 9, 0, ATOM_STATIC, NULL, "&Sat:" },
        { half | WS_GROUP | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 216,
          138, 18, 12, IDC_COLOR_SAT, ATOM_EDIT, NULL, "" },
        { half | SS_RIGHT, 0, 194, 154, 20, 9, 0, ATOM_STATIC, NULL, "&Lum:" },
        { half | WS_GROUP | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 216,
          152, 18, 12, IDC_COLOR_LUMEDIT, ATOM_EDIT, NULL, "" },
        { half | SS_RIGHT, 0, 243, 126, 24, 9, 0, ATOM_STATIC, NULL, "&Red:" },
        { half | WS_GROUP | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 269,
          124, 18, 12, IDC_COLOR_RED, ATOM_EDIT, NULL, "" },
        { half | SS_RIGHT, 0, 243, 140, 24, 9, 0, ATOM_STATIC, NULL, "&Green:" },
        { half | WS_GROUP | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 269,
          138, 18, 12, IDC_COLOR_GREEN, ATOM_EDIT, NULL, "" },
        { half | SS_RIGHT, 0, 243, 154, 24, 9, 0, ATOM_STATIC, NULL, "Bl&ue:" },
        { half | WS_GROUP | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 269,
          152, 18, 12, IDC_COLOR_BLUE, ATOM_EDIT, NULL, "" },
        { half | WS_GROUP | WS_TABSTOP | BS_PUSHBUTTON, 0, 152, 166, 142, 14,
          IDC_COLOR_ADD, ATOM_BUTTON, NULL, "&Add to Custom Colors" },
    };
    DLGTEMPLATE *tmpl;

    if (!cc || !cc->lpCustColors)
        return FALSE;
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = swatch_proc;
        wc.lpszClassName = "ween32ColorGrid";
        RegisterClassA(&wc);
        wc.lpfnWndProc = field_proc;
        wc.lpszClassName = "ween32ColorField";
        RegisterClassA(&wc);
        wc.lpfnWndProc = lum_proc;
        wc.lpszClassName = "ween32ColorLum";
        RegisterClassA(&wc);
        wc.lpfnWndProc = sample_proc;
        wc.lpszClassName = "ween32ColorSample";
        RegisterClassA(&wc);
        registered = 1;
        for (int i = 0; i < 16; i++)
            g_custom[i] = RGB(255, 255, 255);
    }
    memcpy(g_custom, cc->lpCustColors, sizeof g_custom);
    g_cc.cc = cc;
    g_cc.chosen = (cc->Flags & CC_RGBINIT) ? cc->rgbResult : RGB(0, 0, 0);

    /* Wide enough for the definition half, and shrunk to the left half on
     * the way up; DS_CONTEXTHELP because the machine's wears the mark. */
    tmpl = build(buf,
                 WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
                     DS_SETFONT | DS_3DLOOK | DS_CONTEXTHELP,
                 COLOR_DLG_OPEN, 184, "Color", items,
                 (int)(sizeof items / sizeof items[0]));
    if (DialogBoxIndirectParamA(NULL, tmpl, cc->hwndOwner, color_proc, 0) != 1)
        return FALSE;
    cc->rgbResult = g_cc.chosen;
    memcpy(cc->lpCustColors, g_custom, sizeof g_custom);
    return TRUE;
}
