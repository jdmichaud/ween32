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

/* ---- the file dialog ----------------------------------------------------
 *
 * The shell's dialog, the way Windows 2000 puts it up: a places bar down the
 * left, a tool bar beside the "Look in" box, and the files in a list view.
 *
 * Every rectangle here was read off the machine rather than guessed --
 * tools/vm/probe.c dumps the dialog's whole window tree -- and they are in
 * pixels rather than dialog units, because the shell builds this dialog
 * itself and its parts do not land on the dialog grid. The pictures are cut
 * out of a capture of it, in src/shellart.c.
 */

/* The ids the shell uses. A program that hooks this dialog asks for its
 * controls by these numbers, so they are not ours to choose. */
#define IDC_FILE_LOOKIN_LABEL 1091
#define IDC_FILE_LOOKIN 1137
#define IDC_FILE_PLACES 1184
#define IDC_FILE_BAR 1186
#define IDC_FILE_LIST 1
#define IDC_FILE_NAME_LABEL 1090
#define IDC_FILE_NAME 1148
#define IDC_FILE_TYPE_LABEL 1089
#define IDC_FILE_TYPE 1136

/* The client area, and where everything in it sits. */
#define FD_CX 555
#define FD_CY 320
#define FD_PLACES_X 6
#define FD_PLACES_Y 36
#define FD_PLACES_CX 87
#define FD_PLACES_CY 273
#define FD_PLACE_PITCH 52 /* one place under the next */
#define FD_LIST_X 99
#define FD_LIST_Y 36
#define FD_LIST_CX 450
#define FD_LIST_CY 218

static struct {
    OPENFILENAMEA *ofn;
    int saving;
    char dir[1400];
    char pick[1800];
    HIMAGELIST icons; /* the two the list draws: a folder and a document */
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

/* ---- the dialog's pictures ---------------------------------------------- */

/* A picture as a bitmap, made once and kept: these are drawn on every paint
 * and none of them ever changes. */
static HBITMAP art_bitmap(int which)
{
    static HBITMAP made[WEEN_ART_DOCUMENT16 + 1];
    const ween_shell_art *a;
    unsigned char *bits;
    HBITMAP bmp;
    if (which < 0 || which > WEEN_ART_DOCUMENT16)
        return NULL;
    if (made[which])
        return made[which];
    a = ween_shell_picture(which);
    if (!a)
        return NULL;
    bits = malloc((size_t)a->w * (size_t)a->h * 4);
    if (!bits)
        return NULL;
    for (int i = 0; i < a->w * a->h; i++) {
        bits[i * 4 + 0] = (unsigned char)(a->px[i] & 0xff);         /* blue */
        bits[i * 4 + 1] = (unsigned char)((a->px[i] >> 8) & 0xff);  /* green */
        bits[i * 4 + 2] = (unsigned char)((a->px[i] >> 16) & 0xff); /* red */
        bits[i * 4 + 3] = 0;
    }
    bmp = CreateBitmap(a->w, a->h, 1, 32, bits);
    free(bits);
    made[which] = bmp;
    return bmp;
}

static void art_draw(HDC dc, int which, int x, int y)
{
    const ween_shell_art *a = ween_shell_picture(which);
    HBITMAP bmp = art_bitmap(which);
    HDC mem;
    HGDIOBJ old;
    if (!a || !bmp)
        return;
    mem = CreateCompatibleDC(dc);
    old = SelectObject(mem, bmp);
    BitBlt(dc, x, y, a->w, a->h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

/* ---- the places bar ----------------------------------------------------- */

/* The five the shell offers, in its order. Where each one goes is this
 * machine's answer to the same question -- there is no Desktop folder here
 * that is not the home directory, and no network at all. */
static const struct {
    const char *name;
    int art;
} g_places[5] = {
    { "History", WEEN_ART_HISTORY },
    { "Desktop", WEEN_ART_DESKTOP },
    { "My Documents", WEEN_ART_DOCUMENTS },
    { "My Computer", WEEN_ART_COMPUTER },
    { "My Network Places", WEEN_ART_NETWORK },
};

static void places_go(HWND dlg, int which);

static LRESULT CALLBACK places_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT c, in;
        HBRUSH grey;
        GetClientRect(wnd, &c);
        /* the bar is sunken, and what is inside it is the shadow grey rather
         * than the face -- which is what the labels are white against */
        DrawEdge(dc, &c, EDGE_SUNKEN, BF_RECT);
        in.left = c.left + 2;
        in.top = c.top + 2;
        in.right = c.right - 2;
        in.bottom = c.bottom - 2;
        grey = CreateSolidBrush(RGB(128, 128, 128));
        FillRect(dc, &in, grey);
        DeleteObject(grey);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        for (int i = 0; i < 5; i++) {
            const ween_shell_art *a = ween_shell_picture(g_places[i].art);
            int top = in.top + 3 + FD_PLACE_PITCH * i;
            RECT t;
            art_draw(dc, g_places[i].art,
                     in.left + ((in.right - in.left) - a->w) / 2, top);
            /* four in on each side: "My Network Places" is cut to "My
             * Network P..." on the machine, and one more letter fits in the
             * whole width */
            t.left = in.left + 3;
            t.right = in.right - 3;
            t.top = top + 33; /* where the machine's line of text starts */
            t.bottom = t.top + 13;
            DrawTextA(dc, g_places[i].name, -1, &t,
                      DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int y = GET_Y_LPARAM(lp) - 2 - 3;
        int which = y / FD_PLACE_PITCH;
        if (which >= 0 && which < 5)
            places_go(GetParent(wnd), which);
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* ---- the tool bar ------------------------------------------------------- */

/* Four buttons of twenty-three pixels and the arrow beside the last: Back,
 * Up one level, Create New Folder and the view menu. The strip is drawn from
 * the capture, so what it looks like at rest is what the machine looks like;
 * only what the buttons do is ours. */
#define FD_BAR_BUTTON 23
#define FD_BAR_BACK 0
#define FD_BAR_UP 1
#define FD_BAR_NEW 2
#define FD_BAR_VIEWS 3

static void bar_click(HWND dlg, int which);

static LRESULT CALLBACK bar_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        art_draw(dc, WEEN_ART_TOOLBAR, 0, 0);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int which = GET_X_LPARAM(lp) / FD_BAR_BUTTON;
        if (which >= 0 && which <= FD_BAR_VIEWS)
            bar_click(GetParent(wnd), which);
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

/* The corner a resizable dialog is dragged bigger by. The machine puts a
 * scroll bar control with SBS_SIZEGRIP there; what shows is the hatch. */
static LRESULT CALLBACK grip_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT c;
        int ox, oy;
        BeginPaint(wnd, &ps);
        GetClientRect(wnd, &c);
        ween_client_origin(wnd, &ox, &oy);
        /* the twelve-pixel hatch a status bar wears, in the corner of the
         * sixteen the control occupies -- which is what the machine draws */
        ween_classic_sizegrip(&ween_top_level(wnd)->surface, ox + c.right - 1,
                              oy + c.bottom - 1);
        EndPaint(wnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcA(wnd, msg, wp, lp);
    }
}

static void register_parts(void)
{
    static int done;
    WNDCLASSA wc;
    if (done)
        return;
    done = 1;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = places_proc;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "weenfileplaces";
    RegisterClassA(&wc);
    wc.lpfnWndProc = bar_proc;
    wc.lpszClassName = "weenfilebar";
    RegisterClassA(&wc);
    wc.lpfnWndProc = grip_proc;
    wc.hCursor = LoadCursorA(NULL, IDC_SIZENWSE);
    wc.lpszClassName = "weenfilegrip";
    RegisterClassA(&wc);
}

/* ---- what the dialog shows ---------------------------------------------- */

/* The two the list draws beside a name. The folder is the one the machine's
 * own "Look in" box wears, since that is the folder picture this capture
 * has; the document is the one it drew beside a .bmp. */
#define FD_ICON_FOLDER 0
#define FD_ICON_FILE 1

static void make_icons(void)
{
    HBITMAP bmp;
    if (g_fd.icons)
        return;
    g_fd.icons = ImageList_Create(16, 16, ILC_MASK, 2, 0);
    if (!g_fd.icons)
        return;
    bmp = art_bitmap(WEEN_ART_LOOKIN);
    if (bmp)
        ImageList_Add(g_fd.icons, bmp, NULL);
    bmp = art_bitmap(WEEN_ART_DOCUMENT16);
    if (bmp)
        ImageList_Add(g_fd.icons, bmp, NULL);
}

/* The name a path ends in, which is what the "Look in" box shows. */
static const char *leaf(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash || !slash[1])
        return path;
    return slash + 1;
}

/* What the list writes beside the picture.
 *
 * The shell leaves the extension off a file whose type it knows, which is
 * every type its Open box is filtering for: the machine's list says "mru"
 * where the file is mru.bmp. What it hands back is still the whole name. */
static const char *shown_name(const char *name, const char *patterns)
{
    static char cut[300];
    const char *dot = strrchr(name, '.');
    char one[64];
    const char *p = patterns;
    if (!dot || dot == name || strlen(name) >= sizeof cut)
        return name;
    while (*p) {
        const char *semi = strchr(p, ';');
        size_t n = semi ? (size_t)(semi - p) : strlen(p);
        if (n < sizeof one && n > 2) {
            memcpy(one, p, n);
            one[n] = 0;
            if (one[0] == '*' && one[1] == '.' && lstrcmpiA(dot, one + 1) == 0) {
                memcpy(cut, name, (size_t)(dot - name));
                cut[dot - name] = 0;
                return cut;
            }
        }
        if (!semi)
            break;
        p = semi + 1;
    }
    return name;
}

static void add_item(HWND list, int at, const char *text, int image)
{
    LVITEMA it;
    memset(&it, 0, sizeof it);
    it.mask = LVIF_TEXT | LVIF_IMAGE;
    it.iItem = at;
    it.pszText = (LPSTR)text;
    it.iImage = image;
    SendMessageA(list, LVM_INSERTITEMA, 0, (LPARAM)&it);
}

/* Folders first, then the files the filter allows -- which is the order the
 * shell lists them in, each kind sorted by name. */
static void fill_list(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_FILE_LIST);
    HWND lookin = GetDlgItem(dlg, IDC_FILE_LOOKIN);
    const char *patterns = filter_patterns();
    DIR *d;
    struct dirent *e;
    int at = 0;

    SendMessageA(list, LVM_DELETEALLITEMS, 0, 0);
    if (lookin) {
        SendMessageA(lookin, CB_RESETCONTENT, 0, 0);
        SendMessageA(lookin, CB_ADDSTRING, 0, (LPARAM)leaf(g_fd.dir));
        SendMessageA(lookin, CB_SETCURSEL, 0, 0);
    }
    d = opendir(g_fd.dir);
    if (!d)
        return;
    while ((e = readdir(d))) {
        struct stat st;
        char full[1700];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (e->d_name[0] == '.')
            continue; /* the shell hides what begins with a dot */
        snprintf(full, sizeof full, "%s/%s", g_fd.dir, e->d_name);
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        add_item(list, at++, e->d_name, FD_ICON_FOLDER);
    }
    closedir(d);
    d = opendir(g_fd.dir);
    if (!d)
        return;
    while ((e = readdir(d))) {
        struct stat st;
        char full[1700];
        snprintf(full, sizeof full, "%s/%s", g_fd.dir, e->d_name);
        if (stat(full, &st) != 0 || S_ISDIR(st.st_mode))
            continue;
        if (!matches(e->d_name, patterns))
            continue;
        add_item(list, at++, shown_name(e->d_name, patterns), FD_ICON_FILE);
    }
    closedir(d);
}

/* Go somewhere: a folder inside this one, or the one above. */
static void go_to(HWND dlg, const char *path)
{
    if (strlen(path) >= sizeof g_fd.dir)
        return; /* deeper than anything here can hold */
    snprintf(g_fd.dir, sizeof g_fd.dir, "%s", path);
    fill_list(dlg);
}

static void go_into(HWND dlg, const char *name)
{
    char next[1700];
    size_t len = strlen(g_fd.dir);
    snprintf(next, sizeof next, "%s%s%s", g_fd.dir,
             len && g_fd.dir[len - 1] == '/' ? "" : "/", name);
    go_to(dlg, next);
}

static void go_up(HWND dlg)
{
    char up[1400];
    char *slash;
    snprintf(up, sizeof up, "%s", g_fd.dir);
    slash = strrchr(up, '/');
    if (!slash)
        return;
    if (slash == up)
        slash[1] = 0;
    else
        *slash = 0;
    go_to(dlg, up);
}

static void places_go(HWND dlg, int which)
{
    const char *home = getenv("HOME");
    switch (which) {
    case 1: /* Desktop */
    case 2: /* My Documents */
        if (home)
            go_to(dlg, home);
        break;
    case 3: /* My Computer */
        go_to(dlg, "/");
        break;
    default: /* History and the network are not places here */
        break;
    }
}

static void bar_click(HWND dlg, int which)
{
    if (which == FD_BAR_UP)
        go_up(dlg);
}

/* Put the typed name together with the directory and hand it back. */
static int accept(HWND dlg)
{
    char name[300];
    GetDlgItemTextA(dlg, IDC_FILE_NAME, name, sizeof name);
    if (!name[0])
        return 0;
    if (name[0] != '/') {
        size_t len = strlen(g_fd.dir);
        struct stat st;
        snprintf(g_fd.pick, sizeof g_fd.pick, "%s%s%s", g_fd.dir,
                 len && g_fd.dir[len - 1] == '/' ? "" : "/", name);
        /* a name that is a folder walks into it, as the shell does */
        if (stat(g_fd.pick, &st) == 0 && S_ISDIR(st.st_mode)) {
            go_into(dlg, name);
            SetDlgItemTextA(dlg, IDC_FILE_NAME, "");
            return 0;
        }
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

/* What the list has picked out, into the name box. */
static void selection_changed(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_FILE_LIST);
    LVITEMA it;
    char name[300];
    int sel = (int)SendMessageA(list, LVM_GETNEXTITEM, (WPARAM)-1,
                                LVNI_SELECTED);
    if (sel < 0)
        return;
    memset(&it, 0, sizeof it);
    it.mask = LVIF_TEXT | LVIF_IMAGE;
    it.iItem = sel;
    it.pszText = name;
    it.cchTextMax = sizeof name;
    SendMessageA(list, LVM_GETITEMA, 0, (LPARAM)&it);
    if (it.iImage != FD_ICON_FOLDER)
        SetDlgItemTextA(dlg, IDC_FILE_NAME, name);
}

static void open_selection(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_FILE_LIST);
    LVITEMA it;
    char name[300];
    int sel = (int)SendMessageA(list, LVM_GETNEXTITEM, (WPARAM)-1,
                                LVNI_SELECTED);
    if (sel < 0)
        return;
    memset(&it, 0, sizeof it);
    it.mask = LVIF_TEXT | LVIF_IMAGE;
    it.iItem = sel;
    it.pszText = name;
    it.cchTextMax = sizeof name;
    SendMessageA(list, LVM_GETITEMA, 0, (LPARAM)&it);
    if (it.iImage == FD_ICON_FOLDER) {
        go_into(dlg, name);
        SetDlgItemTextA(dlg, IDC_FILE_NAME, "");
        return;
    }
    SetDlgItemTextA(dlg, IDC_FILE_NAME, name);
    if (accept(dlg))
        EndDialog(dlg, 1);
}

/* One control, at the pixels the machine puts it. */
static HWND part(HWND dlg, DWORD ex, const char *cls, const char *text,
                 DWORD style, int x, int y, int cx, int cy, int id)
{
    HWND w = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y,
                             cx, cy, dlg, (HMENU)(INT_PTR)id, NULL, NULL);
    /* The dialog's own face, which is the one its template asked for. A
     * control the dialog manager makes is given it; one made by hand here
     * would otherwise be lettered in the shell's font instead, and the two
     * are not the same width. */
    if (w)
        ((struct ween_wnd *)w)->font = ((struct ween_wnd *)dlg)->font;
    return w;
}

static INT_PTR CALLBACK file_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        HWND type, list;
        const char *p = g_fd.ofn->lpstrFilter;
        RECT want;
        register_parts();
        make_icons();

        /* the client area the machine's dialog has, whatever a frame costs */
        want.left = 0;
        want.top = 0;
        want.right = FD_CX;
        want.bottom = FD_CY;
        AdjustWindowRectEx(&want, (DWORD)GetWindowLongA(dlg, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongA(dlg, GWL_EXSTYLE));
        {
            RECT owner;
            int cx = want.right - want.left, cy = want.bottom - want.top;
            int x = 0, y = 0;
            if (g_fd.ofn->hwndOwner &&
                GetWindowRect(g_fd.ofn->hwndOwner, &owner)) {
                x = owner.left + ((owner.right - owner.left) - cx) / 2;
                y = owner.top + ((owner.bottom - owner.top) - cy) / 2;
            }
            if (x < 0)
                x = 0;
            if (y < 0)
                y = 0;
            MoveWindow(dlg, x, y, cx, cy, FALSE);
        }

        part(dlg, 0, "STATIC", "Look &in:", SS_RIGHT, 6, 11, 86, 13,
             IDC_FILE_LOOKIN_LABEL);
        /* The machine's "Look in" box is drawn by the shell rather than by
         * the control: a folder's picture, then its name, at margins of the
         * shell's own choosing. Ours is the same -- CBS_OWNERDRAWFIXED, and
         * WM_DRAWITEM below. */
        part(dlg, 0, "COMBOBOX", "",
                      WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED, 99,
                      7, 261, 22, IDC_FILE_LOOKIN);
        part(dlg, 0, "weenfilebar", "", 0, 372, 7, 120, 23, IDC_FILE_BAR);
        part(dlg, 0, "weenfileplaces", "", 0, FD_PLACES_X, FD_PLACES_Y,
             FD_PLACES_CX, FD_PLACES_CY, IDC_FILE_PLACES);
        list = part(dlg, WS_EX_CLIENTEDGE, "SysListView32", "",
                    WS_TABSTOP | LVS_LIST | LVS_SINGLESEL | LVS_SHAREIMAGELISTS,
                    FD_LIST_X, FD_LIST_Y, FD_LIST_CX, FD_LIST_CY,
                    IDC_FILE_LIST);
        if (list)
            SendMessageA(list, LVM_SETIMAGELIST, LVSIL_SMALL,
                         (LPARAM)g_fd.icons);
        part(dlg, 0, "STATIC", "File &name:", SS_LEFT, 101, 268, 87, 13,
             IDC_FILE_NAME_LABEL);
        /* the name box is the one with pictures in it on the machine, which
         * is why it is a pixel taller than the type box below it */
        part(dlg, 0, WC_COMBOBOXEXA, "", WS_TABSTOP | CBS_DROPDOWN, 195, 263,
             246, 22, IDC_FILE_NAME);
        part(dlg, 0, "STATIC", "Files of &type:", SS_LEFT, 101, 294, 87, 13,
             IDC_FILE_TYPE_LABEL);
        type = part(dlg, 0, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST, 195,
                    291, 246, 21, IDC_FILE_TYPE);
        part(dlg, 0, "BUTTON", g_fd.saving ? "&Save" : "&Open",
             WS_TABSTOP | WS_GROUP | BS_DEFPUSHBUTTON, 474, 263, 75, 23, IDOK);
        part(dlg, 0, "BUTTON", "Cancel", WS_TABSTOP | BS_PUSHBUTTON, 474, 289,
             75, 23, IDCANCEL);
        part(dlg, 0, "weenfilegrip", "", 0, 539, 304, 16, 16, -1);

        fill_list(dlg);
        if (g_fd.ofn->lpstrFile && g_fd.ofn->lpstrFile[0])
            SetDlgItemTextA(dlg, IDC_FILE_NAME, leaf(g_fd.ofn->lpstrFile));
        while (p && *p) { /* the filter's labels go in the type box */
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
    case WM_MEASUREITEM: {
        /* A row of the "Look in" box is the folder picture's sixteen, which
         * is what makes the box twenty-two rather than twenty-one. */
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        if (!mi || mi->CtlID != IDC_FILE_LOOKIN)
            return FALSE;
        mi->itemHeight = 16;
        return TRUE;
    }
    case WM_DRAWITEM: {
        /* The "Look in" box: the folder's picture four pixels in and three
         * down, its name five past that. Those three numbers are the shell's,
         * measured off the machine's own dialog. */
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        char name[300];
        if (!di || di->CtlID != IDC_FILE_LOOKIN)
            return FALSE;
        name[0] = 0;
        FillRect(di->hDC, &di->rcItem, GetSysColorBrush(COLOR_WINDOW));
        art_draw(di->hDC, WEEN_ART_LOOKIN, di->rcItem.left + 4,
                 di->rcItem.top + 1);
        if (SendMessageA(di->hwndItem, CB_GETLBTEXT, (WPARAM)di->itemID,
                         (LPARAM)name) != CB_ERR) {
            RECT t = di->rcItem;
            t.left += 4 + 16 + 4;
            t.top += 2;
            SetBkMode(di->hDC, TRANSPARENT);
            SetTextColor(di->hDC, GetSysColor(COLOR_WINDOWTEXT));
            DrawTextA(di->hDC, name, -1, &t,
                      DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        }
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm && nm->idFrom == IDC_FILE_LIST) {
            if (nm->code == LVN_ITEMCHANGED)
                selection_changed(dlg);
            else if (nm->code == NM_DBLCLK)
                open_selection(dlg);
        }
        return FALSE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if (id == IDC_FILE_TYPE && code == CBN_SELCHANGE) {
            int sel = (int)SendMessageA(GetDlgItem(dlg, IDC_FILE_TYPE),
                                        CB_GETCURSEL, 0, 0);
            g_fd.ofn->nFilterIndex = (DWORD)(sel < 0 ? 1 : sel + 1);
            fill_list(dlg);
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
    static unsigned char buf[1024] __attribute__((aligned(4)));
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

    /* An empty template: the size is set and the controls are made when it
     * comes up, because both are in pixels rather than in dialog units. */
    /* The machine's is resizable -- WS_THICKFRAME, which is where its size
     * grip comes from and why its frame is four pixels rather than three. */
    tmpl = build(buf,
                 WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                     DS_MODALFRAME | DS_SETFONT | DS_3DLOOK | DS_CONTEXTHELP,
                 200, 100,
                 ofn->lpstrTitle ? ofn->lpstrTitle : (saving ? "Save As" : "Open"),
                 NULL, 0);
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
    /* Which square is picked out, and in which grid. One at a time across
     * both of them, and by *position*: the sixteen custom squares start out
     * all white, and marking the chosen one by colour would ring every one
     * of them. */
    int sel_grid;  /* IDC_COLOR_BASIC, IDC_COLOR_CUSTOM, or 0 for neither */
    int sel_index;
    /* Where "Add to Custom Colors" will put the next one. Clicking a custom
     * square moves it there; each add walks it on by one, the way the
     * machine's does. */
    int add_index;
    /* While the six numbers are being written into, so that setting them
     * does not come back round as an edit of its own. */
    int quiet;
} g_cc;

/* The sixteen custom squares are eight across and two down, but the array
 * behind them runs *down* each column: the machine fills the top of a column
 * and then the bottom of the same one before moving right. */
static int custom_slot(int cell)
{
    return (cell % 8) * 2 + cell / 8;
}

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
                          int count, int cols, int grid)
{
    int custom = grid == IDC_COLOR_CUSTOM;
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
        br = CreateSolidBrush(colors[custom ? custom_slot(i) : i]);
        FillRect(dc, &in, br);
        DeleteObject(br);
        if (g_cc.sel_grid == grid && g_cc.sel_index == i) {
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
    /* Wider, and not a pixel to either side: asking where it is and putting
     * it back there is the way to walk a window across the screen. */
    SetWindowPos(dlg, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (size_t i = 0; i < sizeof half_ids / sizeof half_ids[0]; i++)
        EnableWindow(GetDlgItem(dlg, half_ids[i]), open ? TRUE : FALSE);
    /* the button that asked goes grey, as it does on the machine */
    EnableWindow(GetDlgItem(dlg, IDC_COLOR_DEFINE), open ? FALSE : TRUE);
    InvalidateRect(dlg, NULL, TRUE);
}

/* What one of the six numbers says, or -1 when it says nothing a number
 * could be made of -- an empty field while it is being retyped. */
static int color_edit_value(HWND dlg, int id)
{
    char buf[16];
    int v = 0, any = 0;
    if (!GetDlgItemTextA(dlg, id, buf, sizeof buf))
        return -1;
    for (const char *p = buf; *p; p++) {
        if (*p < '0' || *p > '9')
            return -1;
        v = v * 10 + (*p - '0');
        any = 1;
        if (v > 9999)
            break;
    }
    return any ? v : -1;
}

/* Write the six out again. `except` is the one being typed into, which is
 * left alone so that the caret stays where the person put it. */
static void color_show_numbers(HWND dlg, int except)
{
    static const struct {
        int id;
        int rgb; /* which of the two triples it belongs to */
    } fields[6] = { { IDC_COLOR_HUE, 0 },   { IDC_COLOR_SAT, 0 },
                    { IDC_COLOR_LUMEDIT, 0 }, { IDC_COLOR_RED, 1 },
                    { IDC_COLOR_GREEN, 1 }, { IDC_COLOR_BLUE, 1 } };
    char buf[16];
    g_cc.quiet = 1;
    for (int i = 0; i < 6; i++) {
        int v;
        if (fields[i].id == except)
            continue;
        switch (fields[i].id) {
        case IDC_COLOR_HUE: v = g_cc.hue; break;
        case IDC_COLOR_SAT: v = g_cc.sat; break;
        case IDC_COLOR_LUMEDIT: v = g_cc.lum; break;
        case IDC_COLOR_RED: v = GetRValue(g_cc.chosen); break;
        case IDC_COLOR_GREEN: v = GetGValue(g_cc.chosen); break;
        default: v = GetBValue(g_cc.chosen); break;
        }
        snprintf(buf, sizeof buf, "%d", v);
        SetDlgItemTextA(dlg, fields[i].id, buf);
    }
    g_cc.quiet = 0;
}

/* The three pictures that follow the colour: the field's cross, the bar and
 * its arrow, and the sample. */
static void color_repaint(HWND dlg)
{
    static const int ids[3] = { IDC_COLOR_FIELD, IDC_COLOR_LUM,
                                IDC_COLOR_SAMPLE };
    for (int i = 0; i < 3; i++) {
        HWND c = GetDlgItem(dlg, ids[i]);
        if (c)
            InvalidateRect(c, NULL, FALSE);
    }
    InvalidateRect(dlg, NULL, FALSE); /* the arrow, which the dialog draws */
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
        /* The square the colour came in on is picked out, if it is one of
         * them: the basic grid first, then the custom one. */
        g_cc.sel_grid = 0;
        g_cc.sel_index = 0;
        g_cc.add_index = 0;
        for (int i = 0; i < 48; i++) {
            if (g_basic[i] == g_cc.chosen) {
                g_cc.sel_grid = IDC_COLOR_BASIC;
                g_cc.sel_index = i;
                break;
            }
        }
        if (!g_cc.sel_grid) {
            for (int i = 0; i < 16; i++) {
                if (g_custom[custom_slot(i)] == g_cc.chosen) {
                    g_cc.sel_grid = IDC_COLOR_CUSTOM;
                    g_cc.sel_index = i;
                    g_cc.add_index = custom_slot(i);
                    break;
                }
            }
        }
        color_show_numbers(dlg, 0);
        /* CC_FULLOPEN asks for it open; anything else starts it shut. */
        color_show_half(dlg, (g_cc.cc->Flags & CC_FULLOPEN) != 0);
        return 1;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlID == IDC_COLOR_BASIC)
            draw_swatches(dlg, di->hDC, di->hwndItem, g_basic, 48, 8,
                          IDC_COLOR_BASIC);
        else if (di->CtlID == IDC_COLOR_CUSTOM)
            draw_swatches(dlg, di->hDC, di->hwndItem, g_custom, 16, 8,
                          IDC_COLOR_CUSTOM);
        else if (di->CtlID == IDC_COLOR_SAMPLE) {
            HBRUSH br = CreateSolidBrush(g_cc.chosen);
            FillRect(di->hDC, &di->rcItem, br);
            DeleteObject(br);
            FrameRect(di->hDC, &di->rcItem, GetSysColorBrush(COLOR_WINDOWTEXT));
        }
        return TRUE;
    }
    case WM_PAINT: {
        RECT bar;
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
            /* Into the square the custom grid is sitting on, and then on to
             * the next one -- so pressing it twice fills two, which is what
             * the machine does. */
            g_custom[g_cc.add_index % 16] = g_cc.chosen;
            g_cc.add_index = (g_cc.add_index + 1) % 16;
            InvalidateRect(GetDlgItem(dlg, IDC_COLOR_CUSTOM), NULL, FALSE);
            return TRUE;
        }
        /* One of the six numbers was typed in. Each drives the colour: the
         * three on the left are its hue, saturation and luminosity, the
         * three on the right its red, green and blue, and whichever is
         * touched the other five follow. */
        if (HIWORD(wp) == EN_CHANGE && !g_cc.quiet) {
            int v = color_edit_value(dlg, id);
            if (v < 0)
                return FALSE;
            switch (id) {
            case IDC_COLOR_HUE:
                g_cc.hue = v > 239 ? 239 : v;
                break;
            case IDC_COLOR_SAT:
                g_cc.sat = v > 240 ? 240 : v;
                break;
            case IDC_COLOR_LUMEDIT:
                g_cc.lum = v > 240 ? 240 : v;
                break;
            case IDC_COLOR_RED:
            case IDC_COLOR_GREEN:
            case IDC_COLOR_BLUE: {
                int r = GetRValue(g_cc.chosen), g = GetGValue(g_cc.chosen),
                    b = GetBValue(g_cc.chosen);
                if (v > 255)
                    v = 255;
                if (id == IDC_COLOR_RED)
                    r = v;
                else if (id == IDC_COLOR_GREEN)
                    g = v;
                else
                    b = v;
                g_cc.chosen = RGB(r, g, b);
                rgb_to_hsl(g_cc.chosen, &g_cc.hue, &g_cc.sat, &g_cc.lum);
                color_show_numbers(dlg, id);
                color_repaint(dlg);
                return TRUE;
            }
            default:
                return FALSE;
            }
            g_cc.chosen = hsl_to_rgb(g_cc.hue, g_cc.sat, g_cc.lum);
            color_show_numbers(dlg, id);
            color_repaint(dlg);
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
    case WM_LBUTTONUP:
        if (GetCapture() == wnd)
            ReleaseCapture();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
        if (msg == WM_MOUSEMOVE && !(wp & MK_LBUTTON))
            return 0;
        /* The pointer is held for the length of the drag, so that running
         * off the edge of the field goes on picking the colour at the edge
         * rather than handing the drag to whatever is under it. */
        if (msg == WM_LBUTTONDOWN)
            SetCapture(wnd);
        GetClientRect(wnd, &r);
        {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= r.right) x = r.right - 1;
            if (y >= r.bottom) y = r.bottom - 1;
            /* hue and saturation from where it was pressed; the brightness
             * stays where the bar beside it was left, which is what the
             * machine does */
            /* Hue over the whole width, saturation over one less than the
             * height: measured off the machine, which answers 228, 200 and
             * 173 for rows 10, 31 and 52 of its 187. */
            g_cc.hue = x * 240 / r.right;
            g_cc.sat = 240 - y * 240 / (r.bottom - 1);
            g_cc.chosen = hsl_to_rgb(g_cc.hue, g_cc.sat, g_cc.lum);
            color_show_numbers(GetParent(wnd), 0);
            color_repaint(GetParent(wnd));
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
    case WM_LBUTTONUP:
        if (GetCapture() == wnd)
            ReleaseCapture();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
        if (msg == WM_MOUSEMOVE && !(wp & MK_LBUTTON))
            return 0;
        if (msg == WM_LBUTTONDOWN) /* held for the length of the drag */
            SetCapture(wnd);
        GetClientRect(wnd, &r);
        {
            int y = GET_Y_LPARAM(lp);
            if (y < 0) y = 0;
            if (y >= r.bottom) y = r.bottom - 1;
            g_cc.lum = 240 - y * 240 / (r.bottom - 1);
            g_cc.chosen = hsl_to_rgb(g_cc.hue, g_cc.sat, g_cc.lum);
            color_show_numbers(GetParent(wnd), 0);
            color_repaint(GetParent(wnd));
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
            draw_swatches(GetParent(wnd), dc, wnd, g_basic, 48, 8,
                          IDC_COLOR_BASIC);
        else
            draw_swatches(GetParent(wnd), dc, wnd, g_custom, 16, 8,
                          IDC_COLOR_CUSTOM);
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
        /* One square is picked out at a time across both grids, so the one
         * that was is drawn again without its ring. */
        {
            HWND dlg = GetParent(wnd);
            int grid = GetDlgCtrlID(wnd);
            HWND other = GetDlgItem(dlg, grid == IDC_COLOR_BASIC
                                             ? IDC_COLOR_CUSTOM
                                             : IDC_COLOR_BASIC);
            if (g_cc.sel_grid && g_cc.sel_grid != grid && other)
                InvalidateRect(other, NULL, FALSE);
            g_cc.sel_grid = grid;
            g_cc.sel_index = i;
            if (grid == IDC_COLOR_CUSTOM) {
                /* and the next colour added goes into the one just picked */
                g_cc.add_index = custom_slot(i);
                g_cc.chosen = g_custom[g_cc.add_index];
            } else {
                g_cc.chosen = g_basic[i];
            }
            rgb_to_hsl(g_cc.chosen, &g_cc.hue, &g_cc.sat, &g_cc.lum);
            InvalidateRect(wnd, NULL, FALSE);
            color_show_numbers(dlg, 0);
            color_repaint(dlg);
        }
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
    INT_PTR answer = DialogBoxIndirectParamA(NULL, tmpl, cc->hwndOwner,
                                             color_proc, 0);
    /* The sixteen go back whatever the answer was: a colour mixed and added
     * is kept even if the box is then cancelled, which is what the machine
     * does and what the documentation means by "the system updates the
     * array". */
    memcpy(cc->lpCustColors, g_custom, sizeof g_custom);
    if (answer != 1)
        return FALSE;
    cc->rgbResult = g_cc.chosen;
    return TRUE;
}
