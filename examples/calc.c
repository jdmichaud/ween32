/* The classic Windows calculator (standard mode), recreated as plain win32
 * code — ween32's flagship example. Compiles unchanged against real
 * <windows.h> on Windows and against ween32 everywhere else.
 *
 * It is built the authentic win32 way: the controls are declared once as a
 * DIALOG TEMPLATE in dialog units, and the dialog manager
 * (CreateDialogIndirectParam) instantiates every button and maps its DLUs to
 * pixels. The app writes a control table, not coordinate arithmetic — exactly
 * as a .rc DIALOGEX resource would. Even the sunken display and the memory box
 * are placed with MapDialogRect.
 *
 * Faithful details:
 *   - dialog-unit layout via the dialog manager;
 *   - blue digit/operator keys and red clear & memory keys, drawn as
 *     BS_OWNERDRAW buttons in WM_DRAWITEM — the mechanism calc.exe used;
 *   - immediate-execution arithmetic with CE/C/Backspace, +/-, sqrt, %, 1/x
 *     and the MC/MR/MS/M+ memory keys with the "M" indicator;
 *   - Enter = equals, Esc = clear, digits/Backspace from the keyboard,
 *     routed through IsDialogMessage.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ween32.h>

#include "win32_dlg.h"

/* ---- control ids ------------------------------------------------------- */

#define ID_DIGIT(d) (100 + (d))
#define ID_ADD 10
#define ID_SUB 11
#define ID_MUL 12
#define ID_DIV 13
#define ID_EQ 14
#define ID_DOT 15
#define ID_SIGN 16
#define ID_SQRT 17
#define ID_PCT 18
#define ID_INV 19
#define ID_BACK 20
#define ID_CE 21
#define ID_C 22
#define ID_MC 23
#define ID_MR 24
#define ID_MS 25
#define ID_MPLUS 26

/* ---- layout, in dialog units (integer constants; the manager maps them) - */

#define M 4        /* window margin */
#define DISP_H 14  /* display height */
#define BW 24      /* key width */
#define BH 16      /* key height */
#define GX 3       /* horizontal gap */
#define GY 4       /* vertical gap */
#define MEMGAP 6   /* gap between the memory column and the main grid */

#define GRID_X (M + BW + MEMGAP)
#define GRID_W (5 * BW + 4 * GX)
#define ROW0 (M + DISP_H + GY)          /* indicator + Back/CE/C row */
#define ROW(r) (ROW0 + (BH + GY) * (r)) /* r = 0..4 */
#define COL(c) (GRID_X + (BW + GX) * (c))
#define TCW ((GRID_W - 2 * GX) / 3)     /* Back/CE/C third-width */
#define TCX(i) (GRID_X + (TCW + GX) * (i))
#define TOTAL_W (GRID_X + GRID_W + M)
#define TOTAL_H (ROW(4) + BH + M)

/* an owner-drawn key */
#define KEY(id, text, x, y, w, h)                                             \
    {                                                                          \
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, (x), (y), (w), (h), \
            (id), ATOM_BUTTON, (text)                                          \
    }

/* ---- calculator state --------------------------------------------------- */

static char g_entry[32] = "0";
static int g_entering = 1;
static double g_value = 0.0;
static double g_acc = 0.0;
static int g_pending = 0;
static double g_mem = 0.0;
static HWND g_wnd;

static double current(void)
{
    return g_entering ? strtod(g_entry, NULL) : g_value;
}

static void format_display(char *out, size_t n, double v)
{
    snprintf(out, n, "%.13g", v);
    if (!strchr(out, '.') && !strchr(out, 'e') && !strchr(out, 'n'))
        strncat(out, ".", n - strlen(out) - 1);
}

static void show_value(double v)
{
    g_value = v;
    g_entering = 0;
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void digit(int d)
{
    if (!g_entering) {
        strcpy(g_entry, "0");
        g_entering = 1;
    }
    if (strlen(g_entry) >= 15)
        return;
    if (strcmp(g_entry, "0") == 0)
        g_entry[0] = 0;
    char buf[2] = { (char)('0' + d), 0 };
    strcat(g_entry, buf);
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void decimal_point(void)
{
    if (!g_entering) {
        strcpy(g_entry, "0");
        g_entering = 1;
    }
    if (!strchr(g_entry, '.'))
        strcat(g_entry, ".");
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void commit(void)
{
    double x = current();
    switch (g_pending) {
    case ID_ADD:
        g_acc += x;
        break;
    case ID_SUB:
        g_acc -= x;
        break;
    case ID_MUL:
        g_acc *= x;
        break;
    case ID_DIV:
        g_acc = x == 0.0 ? 0.0 : g_acc / x;
        break;
    default:
        g_acc = x;
        break;
    }
    g_pending = 0;
    show_value(g_acc);
}

static void key(int id)
{
    switch (id) {
    case ID_ADD:
    case ID_SUB:
    case ID_MUL:
    case ID_DIV:
        commit();
        g_pending = id;
        break;
    case ID_EQ:
        commit();
        break;
    case ID_DOT:
        decimal_point();
        break;
    case ID_SIGN:
        if (g_entering && strcmp(g_entry, "0") != 0) {
            if (g_entry[0] == '-')
                memmove(g_entry, g_entry + 1, strlen(g_entry));
            else {
                memmove(g_entry + 1, g_entry, strlen(g_entry) + 1);
                g_entry[0] = '-';
            }
            InvalidateRect(g_wnd, NULL, FALSE);
        } else {
            show_value(-current());
        }
        break;
    case ID_SQRT:
        show_value(current() < 0 ? 0.0 : sqrt(current()));
        break;
    case ID_PCT:
        show_value(g_acc * current() / 100.0);
        break;
    case ID_INV:
        show_value(current() == 0.0 ? 0.0 : 1.0 / current());
        break;
    case ID_BACK:
        if (g_entering) {
            size_t len = strlen(g_entry);
            if (len > 1)
                g_entry[len - 1] = 0;
            else
                strcpy(g_entry, "0");
            InvalidateRect(g_wnd, NULL, FALSE);
        }
        break;
    case ID_CE:
        strcpy(g_entry, "0");
        g_entering = 1;
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    case ID_C:
        strcpy(g_entry, "0");
        g_entering = 1;
        g_acc = g_value = 0;
        g_pending = 0;
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    case ID_MC:
        g_mem = 0;
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    case ID_MR:
        show_value(g_mem);
        break;
    case ID_MS:
        g_mem = current();
        g_entering = 0;
        g_value = g_mem;
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    case ID_MPLUS:
        g_mem += current();
        InvalidateRect(g_wnd, NULL, FALSE);
        break;
    default:
        if (id >= ID_DIGIT(0) && id <= ID_DIGIT(9))
            digit(id - ID_DIGIT(0));
        break;
    }
}

/* Red for the clear & memory keys, blue for everything else — the classic
 * Windows 2000 calc.exe scheme. */
static COLORREF key_color(int id)
{
    switch (id) {
    case ID_BACK:
    case ID_CE:
    case ID_C:
    case ID_MC:
    case ID_MR:
    case ID_MS:
    case ID_MPLUS:
        return RGB(255, 0, 0);
    default:
        return RGB(0, 0, 255);
    }
}

/* ---- dialog procedure ---------------------------------------------------- */

static INT_PTR CALLBACK CalcProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        g_wnd = hwnd;
        SendMessageA(hwnd, DM_SETDEFID, ID_EQ, 0); /* Enter = "=" */
        return FALSE; /* keep focus on the dialog so it sees digit keys */

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        RECT disp = { M, M, TOTAL_W - M, M + DISP_H };
        MapDialogRect(hwnd, &disp);
        FillRect(dc, &disp, GetSysColorBrush(COLOR_WINDOW));
        DrawEdge(dc, &disp, EDGE_SUNKEN, BF_RECT);
        char text[48];
        if (g_entering)
            snprintf(text, sizeof(text), "%s%s", g_entry,
                     strchr(g_entry, '.') ? "" : ".");
        else
            format_display(text, sizeof(text), g_value);
        RECT tr = disp;
        tr.right -= 4;
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        DrawTextA(dc, text, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        RECT mi = { M, ROW0, M + BW, ROW0 + BH };
        MapDialogRect(hwnd, &mi);
        FillRect(dc, &mi, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(dc, &mi, EDGE_SUNKEN, BF_RECT);
        if (g_mem != 0.0) {
            SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            DrawTextA(dc, "M", -1, &mi, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hwnd, &ps);
        return TRUE;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lp;
        RECT r = dis->rcItem;
        int down = (dis->itemState & ODS_SELECTED) != 0;
        FillRect(dis->hDC, &r, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(dis->hDC, &r, down ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        if (down) {
            r.left += 1;
            r.top += 1;
            r.right += 1;
            r.bottom += 1;
        }
        char label[32];
        GetWindowTextA(dis->hwndItem, label, sizeof(label));
        SetTextColor(dis->hDC, key_color((int)dis->CtlID));
        DrawTextA(dis->hDC, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }

    case WM_COMMAND:
        /* Esc arrives as IDCANCEL via IsDialogMessage: quit the app. */
        if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hwnd);
            return TRUE;
        }
        key(LOWORD(wp));
        return TRUE;

    case WM_KEYDOWN:
        if (wp >= '0' && wp <= '9')
            key(ID_DIGIT((int)wp - '0'));
        else if (wp == VK_BACK)
            key(ID_BACK);
        else
            return FALSE;
        return TRUE;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;

    default:
        return FALSE; /* let the default dialog processing run */
    }
}

/* ---- the control table (dialog units) ----------------------------------- */

static const dlg_item g_items[] = {
    /* Back / CE / C — exact thirds of the grid width */
    KEY(ID_BACK, "Backspace", TCX(0), ROW0, TCW, BH),
    KEY(ID_CE, "CE", TCX(1), ROW0, TCW, BH),
    KEY(ID_C, "C", TCX(2), ROW0, TCW, BH),
    /* memory column */
    KEY(ID_MC, "MC", M, ROW(1), BW, BH),
    KEY(ID_MR, "MR", M, ROW(2), BW, BH),
    KEY(ID_MS, "MS", M, ROW(3), BW, BH),
    KEY(ID_MPLUS, "M+", M, ROW(4), BW, BH),
    /* main 4x5 grid */
    KEY(ID_DIGIT(7), "7", COL(0), ROW(1), BW, BH),
    KEY(ID_DIGIT(8), "8", COL(1), ROW(1), BW, BH),
    KEY(ID_DIGIT(9), "9", COL(2), ROW(1), BW, BH),
    KEY(ID_DIV, "/", COL(3), ROW(1), BW, BH),
    KEY(ID_SQRT, "sqrt", COL(4), ROW(1), BW, BH),
    KEY(ID_DIGIT(4), "4", COL(0), ROW(2), BW, BH),
    KEY(ID_DIGIT(5), "5", COL(1), ROW(2), BW, BH),
    KEY(ID_DIGIT(6), "6", COL(2), ROW(2), BW, BH),
    KEY(ID_MUL, "*", COL(3), ROW(2), BW, BH),
    KEY(ID_PCT, "%", COL(4), ROW(2), BW, BH),
    KEY(ID_DIGIT(1), "1", COL(0), ROW(3), BW, BH),
    KEY(ID_DIGIT(2), "2", COL(1), ROW(3), BW, BH),
    KEY(ID_DIGIT(3), "3", COL(2), ROW(3), BW, BH),
    KEY(ID_SUB, "-", COL(3), ROW(3), BW, BH),
    KEY(ID_INV, "1/x", COL(4), ROW(3), BW, BH),
    KEY(ID_DIGIT(0), "0", COL(0), ROW(4), BW, BH),
    KEY(ID_SIGN, "+/-", COL(1), ROW(4), BW, BH),
    KEY(ID_DOT, ".", COL(2), ROW(4), BW, BH),
    KEY(ID_ADD, "+", COL(3), ROW(4), BW, BH),
    KEY(ID_EQ, "=", COL(4), ROW(4), BW, BH),
};

int main(void)
{
    static unsigned char tmpl[4096]; /* DWORD-aligned static storage */
    build_dialog_template(tmpl, sizeof(tmpl),
                          WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE |
                              DS_3DLOOK,
                          TOTAL_W, TOTAL_H, "Calculator", g_items,
                          (int)(sizeof(g_items) / sizeof(g_items[0])));

    HWND dlg = CreateDialogIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, NULL,
                                          CalcProc, 0);
    if (!dlg)
        return 1;

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return 0;
}
