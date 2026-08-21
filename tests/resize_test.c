/* A window with a sizing border: dragging its grip resizes it, the client
 * area follows, and WM_SIZE reaches the app — all through the headless
 * backend, so no window manager is involved. */

#define _POSIX_C_SOURCE 200112L /* setenv */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ween_internal.h"

static int g_failures = 0;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("ok   %s\n", name);                                         \
        } else {                                                               \
            printf("FAIL %s\n", name);                                         \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static HWND g_status;
static int g_sized;

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_status = CreateWindowA(STATUSCLASSNAMEA, "",
                                 WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0,
                                 0, hwnd, NULL, NULL, NULL);
        return 0;
    case WM_SIZE:
        g_sized++;
        if (g_status) /* as a win32 app forwards it */
            SendMessageA(g_status, WM_SIZE, wp, lp);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static void mouse(ween_ev_kind kind, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.x = ev.x_root = x;
    ev.y = ev.y_root = y;
    ev.button = 1;
    ween_headless_inject(ev);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenresize";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    /* grab the grip in the bottom-right corner and pull it out 40 x 30 */
    mouse(WEEN_EV_MOUSE_DOWN, 296, 196);
    mouse(WEEN_EV_MOUSE_MOVE, 336, 226);
    mouse(WEEN_EV_MOUSE_UP, 336, 226);

    HWND wnd = CreateWindowExA(0, "weenresize", "resize",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU |
                                   WS_THICKFRAME | WS_VISIBLE,
                               0, 0, 300, 200, NULL, NULL, NULL, NULL);
    CHECK(wnd != NULL, "a window with a sizing border was created");

    RECT cr;
    GetClientRect(wnd, &cr);
    CHECK(cr.right == 300 - 2 * WEEN_NC_SIZEFRAME,
          "the sizing border is wider than a fixed frame");

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0))
        DispatchMessageA(&msg);

    CHECK(wnd->w == 340 && wnd->h == 230,
          "dragging the status bar's grip resized the window");
    CHECK(g_sized > 0, "the app was told through WM_SIZE");

    GetClientRect(wnd, &cr);
    CHECK(cr.right == 340 - 2 * WEEN_NC_SIZEFRAME,
          "the client area grew with the window");
    CHECK(g_status && g_status->w == cr.right,
          "the status bar followed the new width");

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("resize_test: all passed\n");
    return 0;
}
