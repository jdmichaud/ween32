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
        int v = (l * 255 + 120) / 240;
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
static void draw_swatches(HWND dlg, HDC dc, HWND ctl, const COLORREF *colors,
                          int count, int cols, COLORREF picked)
{
    RECT r;
    int ox, oy, cw, ch;
    (void)dlg;
    GetClientRect(ctl, &r);
    cw = (r.right + cols - 1) / cols;
    ch = 14;
    ween_client_origin(ctl, &ox, &oy);
    for (int i = 0; i < count; i++) {
        RECT cell;
        HBRUSH br;
        cell.left = (i % cols) * cw;
        cell.top = (i / cols) * ch;
        cell.right = cell.left + cw - 2;
        cell.bottom = cell.top + ch - 2;
        br = CreateSolidBrush(colors[i]);
        FillRect(dc, &cell, br);
        DeleteObject(br);
        FrameRect(dc, &cell, GetSysColorBrush(COLOR_BTNSHADOW));
        if (colors[i] == picked) {
            RECT out = cell;
            out.left--;
            out.top--;
            out.right++;
            out.bottom++;
            FrameRect(dc, &out, GetSysColorBrush(COLOR_WINDOWTEXT));
        }
    }
}

static COLORREF g_custom[16];

static INT_PTR CALLBACK color_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        rgb_to_hsl(g_cc.chosen, &g_cc.hue, &g_cc.sat, &g_cc.lum);
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
        return FALSE;
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
static LRESULT CALLBACK field_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RECT r;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        GetClientRect(wnd, &r);
        for (int y = 0; y < r.bottom; y++) {
            int sat = 240 - y * 240 / (r.bottom - 1);
            for (int x = 0; x < r.right; x++) {
                int hue = x * 239 / (r.right - 1);
                SetPixel(dc, x, y, hsl_to_rgb(hue, sat, 120));
            }
        }
        {   /* the cross where the colour sits */
            int cx = g_cc.hue * (r.right - 1) / 239;
            int cy = (240 - g_cc.sat) * (r.bottom - 1) / 240;
            RECT c;
            c.left = cx - 4; c.right = cx + 5; c.top = cy; c.bottom = cy + 1;
            InvertRect(dc, &c);
            c.left = cx; c.right = cx + 1; c.top = cy - 4; c.bottom = cy + 5;
            InvertRect(dc, &c);
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
            g_cc.hue = x * 239 / (r.right - 1);
            g_cc.sat = 240 - y * 240 / (r.bottom - 1);
            g_cc.chosen = hsl_to_rgb(g_cc.hue, g_cc.sat, g_cc.lum);
            InvalidateRect(GetParent(wnd), NULL, FALSE);
        }
        return 0;
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* The brightness bar beside it. */
static LRESULT CALLBACK lum_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RECT r;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        GetClientRect(wnd, &r);
        for (int y = 0; y < r.bottom; y++) {
            int lum = 240 - y * 240 / (r.bottom - 1);
            COLORREF c = hsl_to_rgb(g_cc.hue, g_cc.sat, lum);
            RECT row;
            HBRUSH br = CreateSolidBrush(c);
            row.left = 0; row.right = r.right; row.top = y; row.bottom = y + 1;
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
        RECT r;
        int cols = 8, cw, ch = 14, i;
        GetClientRect(wnd, &r);
        cw = (r.right + cols - 1) / cols;
        i = (GET_Y_LPARAM(lp) / ch) * cols + GET_X_LPARAM(lp) / cw;
        if (GetDlgCtrlID(wnd) == IDC_COLOR_BASIC) {
            if (i >= 0 && i < 48)
                g_cc.chosen = g_basic[i];
        } else if (i >= 0 && i < 16) {
            g_cc.chosen = g_custom[i];
        }
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
    dlg_item items[] = {
        { child | SS_LEFT, 0, 7, 7, 100, 8, 0, ATOM_STATIC, NULL,
          "&Basic colors:" },
        { child, 0, 7, 18, 137, 112, IDC_COLOR_BASIC, 0, "ween32ColorGrid", "" },
        { child | SS_LEFT, 0, 7, 136, 100, 8, 0, ATOM_STATIC, NULL,
          "&Custom colors:" },
        { child, 0, 7, 147, 137, 28, IDC_COLOR_CUSTOM, 0, "ween32ColorGrid",
          "" },
        { child | WS_TABSTOP | WS_GROUP | BS_DEFPUSHBUTTON, 0, 205, 180, 50, 14,
          IDOK, ATOM_BUTTON, NULL, "OK" },
        { child | WS_TABSTOP | BS_PUSHBUTTON, 0, 259, 180, 50, 14, IDCANCEL,
          ATOM_BUTTON, NULL, "Cancel" },
        { child | WS_TABSTOP | BS_PUSHBUTTON, 0, 7, 180, 104, 14,
          IDC_COLOR_ADD, ATOM_BUTTON, NULL, "&Add to Custom Colors" },
        /* the definition half: the field, the brightness bar beside it, and
         * the six numbers under them */
        { child, WS_EX_CLIENTEDGE, 152, 7, 116, 112, IDC_COLOR_FIELD, 0,
          "ween32ColorField", "" },
        { child, WS_EX_CLIENTEDGE, 274, 7, 12, 112, IDC_COLOR_LUM, 0,
          "ween32ColorLum", "" },
        { child, WS_EX_CLIENTEDGE, 152, 126, 40, 20, IDC_COLOR_SAMPLE, 0,
          "ween32ColorSample", "" },
        { child | SS_RIGHT, 0, 196, 126, 30, 8, 0, ATOM_STATIC, NULL, "Hue:" },
        { child | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 228, 124, 24,
          12, IDC_COLOR_HUE, ATOM_EDIT, NULL, "" },
        { child | SS_RIGHT, 0, 196, 140, 30, 8, 0, ATOM_STATIC, NULL, "Sat:" },
        { child | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 228, 138, 24,
          12, IDC_COLOR_SAT, ATOM_EDIT, NULL, "" },
        { child | SS_RIGHT, 0, 196, 154, 30, 8, 0, ATOM_STATIC, NULL, "Lum:" },
        { child | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 228, 152, 24,
          12, IDC_COLOR_LUMEDIT, ATOM_EDIT, NULL, "" },
        { child | SS_RIGHT, 0, 256, 126, 26, 8, 0, ATOM_STATIC, NULL, "Red:" },
        { child | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 284, 124, 24,
          12, IDC_COLOR_RED, ATOM_EDIT, NULL, "" },
        { child | SS_RIGHT, 0, 256, 140, 26, 8, 0, ATOM_STATIC, NULL, "Green:" },
        { child | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 284, 138, 24,
          12, IDC_COLOR_GREEN, ATOM_EDIT, NULL, "" },
        { child | SS_RIGHT, 0, 256, 154, 26, 8, 0, ATOM_STATIC, NULL, "Blue:" },
        { child | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 284, 152, 24,
          12, IDC_COLOR_BLUE, ATOM_EDIT, NULL, "" },
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

    tmpl = build(buf,
                 WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
                     DS_SETFONT | DS_3DLOOK,
                 316, 200, "Color", items,
                 (int)(sizeof items / sizeof items[0]));
    if (DialogBoxIndirectParamA(NULL, tmpl, cc->hwndOwner, color_proc, 0) != 1)
        return FALSE;
    cc->rgbResult = g_cc.chosen;
    memcpy(cc->lpCustColors, g_custom, sizeof g_custom);
    return TRUE;
}
