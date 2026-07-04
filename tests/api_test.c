/* End-to-end test of the win32-shaped API over the headless backend: create a
 * captioned window with buttons through RegisterClass/CreateWindowEx, drive it
 * with scripted mouse events, and assert message routing and rendered pixels.
 * No display needed. */

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

#define ID_OK 1
#define BTN_X 20
#define BTN_Y 40
#define BTN_W 75
#define BTN_H 23

static int g_got_click = 0;
static int g_got_create = 0;

static LRESULT CALLBACK test_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_got_create = 1;
        CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      BTN_X, BTN_Y, BTN_W, BTN_H, hwnd,
                      (HMENU)(UINT_PTR)ID_OK, NULL, NULL);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_OK && HIWORD(wp) == BN_CLICKED) {
            g_got_click = 1;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static ween_event ev_mouse(ween_ev_kind kind, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.x = x;
    ev.y = y;
    ev.button = 1;
    return ev;
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1); /* pixel asserts are 96-dpi */
    ween_active_backend = ween_backend_headless();
    const char *dir = getenv("WEEN_TEST_OUT");
    char path[512];
    snprintf(path, sizeof(path), "%s/api_dialog.bmp", dir ? dir : ".");
    ween_headless_set_bmp_path(path);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = test_proc;
    wc.lpszClassName = "weentest";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    CHECK(RegisterClassA(&wc) != 0, "RegisterClassA succeeds");

    /* Click the OK button: its client rect is at (BTN_X,BTN_Y); the window's
     * client area starts at (frame, frame+caption) in window coordinates. */
    int cx = WEEN_NC_FRAME + BTN_X + BTN_W / 2;
    int cy = WEEN_NC_FRAME + WEEN_NC_CAPTION + BTN_Y + BTN_H / 2;
    ween_headless_inject(ev_mouse(WEEN_EV_MOUSE_DOWN, cx, cy));
    ween_headless_inject(ev_mouse(WEEN_EV_MOUSE_UP, cx, cy));

    HWND wnd = CreateWindowExA(0, "weentest", "ween32 api test",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               0, 0, 320, 180, NULL, NULL, NULL, NULL);
    CHECK(wnd != NULL, "CreateWindowExA creates a captioned top-level");
    CHECK(g_got_create, "WM_CREATE was delivered during creation");
    CHECK(GetDlgItem(wnd, ID_OK) != NULL, "GetDlgItem finds the child by id");

    RECT cr;
    GetClientRect(wnd, &cr);
    CHECK(cr.right == 320 - 2 * WEEN_NC_FRAME, "client width excludes the frame");
    CHECK(cr.bottom == 180 - 2 * WEEN_NC_FRAME - WEEN_NC_CAPTION,
          "client height excludes frame and caption");

    LRESULT hit = SendMessageA(wnd, WM_NCHITTEST, 0, MAKELPARAM(160, 10));
    CHECK(hit == HTCAPTION, "WM_NCHITTEST reports the caption strip");
    hit = SendMessageA(wnd, WM_NCHITTEST, 0, MAKELPARAM(160, 100));
    CHECK(hit == HTCLIENT, "WM_NCHITTEST reports the client area");

    UpdateWindow(wnd);
    const ween_surface *s = ween_headless_surface();
    CHECK(s != NULL, "the surface was presented");
    if (s) {
        CHECK(s->px[0] == WEEN_WHITE, "window frame top-left is raised white");
        CHECK(s->px[(long)4 * s->w + 3] == WEEN_CAP_LEFT,
              "caption gradient starts at #0A246A");
        /* a pixel inside the button face (avoid the bevel and the label) */
        long bx = WEEN_NC_FRAME + BTN_X + 5;
        long by = WEEN_NC_FRAME + WEEN_NC_CAPTION + BTN_Y + 5;
        CHECK(s->px[by * s->w + bx] == WEEN_FACE, "button face is BTNFACE");
    }

    /* Pump: the scripted click presses the button, fires WM_COMMAND, destroys
     * the window and posts WM_QUIT. */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    CHECK(g_got_click, "BN_CLICKED arrived via WM_COMMAND");
    CHECK(msg.message == WM_QUIT, "the loop ended on WM_QUIT");

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("api_test: all passed\n");
    return 0;
}
