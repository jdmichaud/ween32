/* The classic Windows calculator (standard mode), recreated as plain win32
 * code — ween32's flagship example. Compiles unchanged against real
 * <windows.h> on Windows and against ween32 everywhere else.
 *
 * Faithful details:
 *   - layout in dialog units mapped through the base units (nothing is
 *     hand-placed in pixels);
 *   - the colored key caps (blue digits/operators, red clear & memory keys)
 *     use BS_OWNERDRAW buttons painted in WM_DRAWITEM — the mechanism the
 *     real calc.exe used;
 *   - a sunken, right-aligned display showing the classic trailing period;
 *   - immediate-execution arithmetic with CE/C/Backspace, +/-, sqrt, %, 1/x
 *     and the MC/MR/MS/M+ memory keys with the "M" indicator.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ween32.h>

/* ---- control ids ------------------------------------------------------- */

#define ID_DIGIT(d) (100 + (d))
#define ID_ADD 1
#define ID_SUB 2
#define ID_MUL 3
#define ID_DIV 4
#define ID_EQ 5
#define ID_DOT 6
#define ID_SIGN 7
#define ID_SQRT 8
#define ID_PCT 9
#define ID_INV 10
#define ID_BACK 11
#define ID_CE 12
#define ID_C 13
#define ID_MC 14
#define ID_MR 15
#define ID_MS 16
#define ID_MPLUS 17

/* ---- layout in dialog units -------------------------------------------- */

#define M 4        /* window margin */
#define DISP_H 14  /* display height */
#define BW 24      /* key width  (maps to 36 px at 6x13 base units) */
#define BH 16      /* key height */
#define GX 3       /* horizontal gap */
#define GY 4       /* vertical gap */
#define MEMGAP 6   /* gap between the memory column and the main grid */

static int g_bx = 6, g_by = 13;
#define XDLU(u) MulDiv(u, g_bx, 4)
#define YDLU(u) MulDiv(u, g_by, 8)

/* ---- calculator state --------------------------------------------------- */

static char g_entry[32] = "0"; /* digits being typed */
static int g_entering = 1;
static double g_value = 0.0; /* last committed/displayed value */
static double g_acc = 0.0;
static int g_pending = 0; /* pending operator id, or 0 */
static double g_mem = 0.0;
static HWND g_wnd;

static double current(void)
{
    return g_entering ? strtod(g_entry, NULL) : g_value;
}

/* Classic display format: up to 13 significant digits, with the calculator's
 * trailing period on integer values. */
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
    size_t len = strlen(g_entry);
    if (len >= 15)
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
    case ID_SIGN: {
        if (g_entering && g_entry[0] != 0 && strcmp(g_entry, "0") != 0) {
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
    }
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
        g_acc = 0;
        g_value = 0;
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

/* ---- UI ------------------------------------------------------------------ */

/* Red for the clear & memory keys, blue for everything else — the classic
 * calc.exe scheme. */
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

static void make_key(HWND parent, int id, const char *label, int x, int y,
                     int w, int h)
{
    CreateWindowA("BUTTON", label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x, y,
                  w, h, parent, (HMENU)(UINT_PTR)id, NULL, NULL);
}

static RECT display_rect(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    RECT r;
    r.left = XDLU(M);
    r.top = YDLU(M);
    r.right = rc.right - XDLU(M);
    r.bottom = r.top + YDLU(DISP_H);
    return r;
}

static RECT mem_indicator_rect(void)
{
    RECT r;
    r.left = XDLU(M);
    r.top = YDLU(M + DISP_H + GY);
    r.right = r.left + XDLU(BW);
    r.bottom = r.top + YDLU(BH);
    return r;
}

static LRESULT CALLBACK CalcProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        int grid_x = XDLU(M + BW + MEMGAP);
        int row0 = M + DISP_H + GY; /* the indicator + Back/CE/C row, in DLUs */

        /* Back / CE / C: three keys spread across the main grid width. */
        int grid_w = XDLU(5 * BW + 4 * GX);
        int cw = (grid_w - 2 * XDLU(GX)) / 3;
        make_key(hwnd, ID_BACK, "Backspace", grid_x, YDLU(row0), cw, YDLU(BH));
        make_key(hwnd, ID_CE, "CE", grid_x + cw + XDLU(GX), YDLU(row0), cw,
                 YDLU(BH));
        make_key(hwnd, ID_C, "C", grid_x + 2 * (cw + XDLU(GX)), YDLU(row0), cw,
                 YDLU(BH));

        /* memory column */
        static const struct {
            int id;
            const char *label;
        } memkeys[] = {
            { ID_MC, "MC" }, { ID_MR, "MR" }, { ID_MS, "MS" }, { ID_MPLUS, "M+" }
        };
        for (int r = 0; r < 4; r++)
            make_key(hwnd, memkeys[r].id, memkeys[r].label, XDLU(M),
                     YDLU(row0 + (BH + GY) * (r + 1)), XDLU(BW), YDLU(BH));

        /* main 4x5 grid */
        static const struct {
            int id;
            const char *label;
        } grid[4][5] = {
            { { ID_DIGIT(7), "7" }, { ID_DIGIT(8), "8" }, { ID_DIGIT(9), "9" }, { ID_DIV, "/" }, { ID_SQRT, "sqrt" } },
            { { ID_DIGIT(4), "4" }, { ID_DIGIT(5), "5" }, { ID_DIGIT(6), "6" }, { ID_MUL, "*" }, { ID_PCT, "%" } },
            { { ID_DIGIT(1), "1" }, { ID_DIGIT(2), "2" }, { ID_DIGIT(3), "3" }, { ID_SUB, "-" }, { ID_INV, "1/x" } },
            { { ID_DIGIT(0), "0" }, { ID_SIGN, "+/-" }, { ID_DOT, "." }, { ID_ADD, "+" }, { ID_EQ, "=" } },
        };
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 5; c++) {
                make_key(hwnd, grid[r][c].id, grid[r][c].label,
                         grid_x + XDLU((BW + GX)) * c,
                         YDLU(row0 + (BH + GY) * (r + 1)), XDLU(BW), YDLU(BH));
            }
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        /* the display: a sunken well with right-aligned text */
        RECT disp = display_rect(hwnd);
        FillRect(dc, &disp, GetSysColorBrush(COLOR_WINDOW));
        DrawEdge(dc, &disp, EDGE_SUNKEN, BF_RECT);
        char text[48];
        if (g_entering) {
            snprintf(text, sizeof(text), "%s%s", g_entry,
                     strchr(g_entry, '.') ? "" : ".");
        } else {
            format_display(text, sizeof(text), g_value);
        }
        RECT tr = disp;
        tr.right -= XDLU(2);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        DrawTextA(dc, text, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        /* the memory indicator: a sunken box showing "M" when set */
        RECT mi = mem_indicator_rect();
        FillRect(dc, &mi, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(dc, &mi, EDGE_SUNKEN, BF_RECT);
        if (g_mem != 0.0) {
            SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            DrawTextA(dc, "M", -1, &mi, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lp;
        if (dis->CtlType != ODT_BUTTON)
            return 0;
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
        return 1;
    }

    case WM_COMMAND:
        key(LOWORD(wp));
        return 0;

    case WM_KEYDOWN:
        if (wp >= '0' && wp <= '9')
            key(ID_DIGIT((int)wp - '0'));
        else if (wp == VK_RETURN)
            key(ID_EQ);
        else if (wp == VK_BACK)
            key(ID_BACK);
        else if (wp == VK_ESCAPE)
            key(ID_C);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

int main(void)
{
    LONG base = GetDialogBaseUnits();
    g_bx = LOWORD(base);
    g_by = HIWORD(base);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = CalcProc;
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = "ween32calc";
    RegisterClassA(&wc);

    /* client size follows from the DLU layout; the window adds the chrome */
    int client_w = XDLU(M + BW + MEMGAP + 5 * BW + 4 * GX + M);
    int client_h = YDLU(M + DISP_H + GY + 5 * BH + 4 * GY + GY + M);
    HWND wnd = CreateWindowExA(0, "ween32calc", "Calculator",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               100, 100, client_w + 6, client_h + 26, NULL,
                               NULL, NULL, NULL);
    if (!wnd)
        return 1;
    g_wnd = wnd;
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
