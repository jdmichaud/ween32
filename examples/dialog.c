/* The ween32 demo: a classic Win2000 dialog written as plain old win32 code.
 *
 * This file is the library's fidelity contract: it compiles UNCHANGED against
 * real <windows.h> on Windows (ween32.h defers to it there) and against ween32
 * everywhere else — same API, same layout, same classic look.
 *
 * Positioning follows the authentic dialog-unit convention: controls are
 * declared in DLUs (button 50x14, margins 7, spacing 4) and mapped to pixels
 * through the dialog base units, so nothing is hand-placed. */

#include <ween32.h>

#define IDC_LABEL 100
#define ID_OK 1     /* IDOK */
#define ID_CANCEL 2 /* IDCANCEL */

#define DLG_W 320
#define DLG_H 180

/* Dialog-unit helpers over GetDialogBaseUnits (works on any window, unlike
 * MapDialogRect which needs a real dialog on Windows — see MS KB Q145994). */
static int g_bx = 6, g_by = 13;
#define XDLU(u) MulDiv(u, g_bx, 4)
#define YDLU(u) MulDiv(u, g_by, 8)

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int bw = XDLU(50), bh = YDLU(14); /* the standard 50x14 DLU button */
        int margin = XDLU(7), gap = XDLU(4);

        CreateWindowA("STATIC", "Drag the caption. Escape or the buttons close.",
                      WS_CHILD | WS_VISIBLE | SS_LEFT,
                      margin, YDLU(7), rc.right - 2 * margin, YDLU(8),
                      hwnd, (HMENU)(UINT_PTR)IDC_LABEL, NULL, NULL);
        CreateWindowA("BUTTON", "OK",
                      WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                      rc.right - margin - 2 * bw - gap, rc.bottom - YDLU(7) - bh,
                      bw, bh, hwnd, (HMENU)(UINT_PTR)ID_OK, NULL, NULL);
        CreateWindowA("BUTTON", "Cancel",
                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      rc.right - margin - bw, rc.bottom - YDLU(7) - bh,
                      bw, bh, hwnd, (HMENU)(UINT_PTR)ID_CANCEL, NULL, NULL);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        /* a sunken well, like a canvas or list area */
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT well = rc;
        well.left += XDLU(7);
        well.right -= XDLU(7);
        well.top += YDLU(7) + YDLU(8) + YDLU(4);
        well.bottom -= YDLU(7) + YDLU(14) + YDLU(4);
        HBRUSH blue = CreateSolidBrush(RGB(58, 110, 165));
        FillRect(dc, &well, blue);
        DeleteObject(blue);
        DrawEdge(dc, &well, EDGE_SUNKEN, BF_RECT);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_OK || LOWORD(wp) == ID_CANCEL)
            DestroyWindow(hwnd);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
            DestroyWindow(hwnd);
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
    wc.style = 0;
    wc.lpfnWndProc = DlgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = NULL;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "ween32demo";
    RegisterClassA(&wc);

    HWND wnd = CreateWindowExA(0, "ween32demo", "ween32 - it is now safe to party like it is 1999",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               100, 100, DLG_W, DLG_H, NULL, NULL, NULL, NULL);
    if (!wnd)
        return 1;
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
