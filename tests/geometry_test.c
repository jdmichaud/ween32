/* What is drawn and what is clicked have to agree.
 *
 * A window and the buffer shown in it need not be the same size: a window
 * manager can impose one — a tiling one always does — and a window that has
 * declared itself fixed keeps drawing at its own size regardless. What is
 * drawn is centred in what the window system gave us, and the pointer has to
 * come back through exactly that offset.
 *
 * When those two disagreed, a tiled window drew perfectly and answered
 * nothing: every click landed hundreds of pixels from where it looked. Nothing
 * headless could reach that, because the headless backend had no notion of a
 * window being a different size from its surface. It has one now, and these
 * are the assertions that failed before the offsets were made to share their
 * arithmetic.
 */

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

enum { ID_BUTTON = 400 };

static HWND g_big, g_fixed;
static int g_clicks_big, g_clicks_fixed;

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        CreateWindowA("BUTTON", "Press", WS_CHILD | WS_VISIBLE, 20, 20, 80, 24,
                      w, (HMENU)(UINT_PTR)ID_BUTTON, NULL, NULL);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_BUTTON) { /* which window's button was it */
            if (w == g_big)
                g_clicks_big++;
            else if (w == g_fixed)
                g_clicks_fixed++;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(w, msg, wp, lp);
    }
}

/* Both are named for the window they land on, the way a real one's are. */
static void click(HWND w, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_MOUSE_DOWN;
    ev.button = 1;
    ev.win = w->backend_win;
    ev.x = x;
    ev.y = y;
    ween_headless_inject(ev);
    ev.kind = WEEN_EV_MOUSE_UP;
    ween_headless_inject(ev);
}

static void resize(HWND w, int width, int height)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_RESIZE;
    ev.win = w->backend_win;
    ev.x = width;
    ev.y = height;
    ween_headless_inject(ev);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    /* The arithmetic on its own, before any window is involved. */
    {
        ween_letterbox lb;
        int ox, oy, x, y;
        memset(&lb, 0, sizeof(lb));

        ween_letterbox_window(&lb, 300, 200);
        ween_letterbox_shown(&lb, 300, 200);
        ween_letterbox_origin(&lb, &ox, &oy);
        CHECK(ox == 0 && oy == 0, "a buffer that fills its window has no offset");

        ween_letterbox_window(&lb, 900, 600);
        ween_letterbox_origin(&lb, &ox, &oy);
        CHECK(ox == 300 && oy == 200,
              "a smaller buffer is centred in a bigger window");

        x = 320;
        y = 210;
        ween_letterbox_to_surface(&lb, 1, &x, &y);
        CHECK(x == 20 && y == 10,
              "and the pointer comes back through the same offset");

        ween_letterbox_shown(&lb, 900, 600); /* the buffer caught up */
        ween_letterbox_origin(&lb, &ox, &oy);
        CHECK(ox == 0 && oy == 0,
              "a buffer that grows to fill the window loses the offset");

        x = 320;
        y = 210;
        ween_letterbox_to_surface(&lb, 1, &x, &y);
        CHECK(x == 320 && y == 210, "and the pointer stops being shifted");
    }

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weengeom";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    /* The window manager imposes a tile far bigger than either window asks
     * for. One of them has a sizing border and follows it; the other has not
     * and keeps its own size, so its buffer is centred and a click has to be
     * offset by however far. Both windows are driven from one message loop,
     * because WM_QUIT is final and there is only one to have. */
    ween_headless_set_window_size(900, 600);

    g_big = CreateWindowExA(0, "weengeom", "tiled",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU |
                                   WS_THICKFRAME | WS_VISIBLE,
                               0, 0, 300, 200, NULL, NULL, NULL, NULL);
    g_fixed = CreateWindowExA(0, "weengeom", "fixed",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU |
                                     WS_VISIBLE,
                                 0, 0, 300, 200, NULL, NULL, NULL, NULL);
    CHECK(g_big && g_fixed, "two windows the window manager oversized");
    ween_flush_paint(); /* so both have presented, as a real display would */

    resize(g_big, 900, 600);
    resize(g_fixed, 900, 600);

    /* The resizable one grew to fill the tile, so its button is where its own
     * coordinates say. */
    click(g_big, WEEN_NC_SIZEFRAME + 40,
          WEEN_NC_SIZEFRAME + WEEN_NC_CAPTION + 30);
    /* The fixed one did not, so its button is wherever the centring put it. */
    click(g_fixed, (900 - 300) / 2 + WEEN_NC_FRAME + 40,
          (600 - 200) / 2 + WEEN_NC_FRAME + WEEN_NC_CAPTION + 30);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0))
        DispatchMessageA(&msg);

    CHECK(g_big->w == 900 && g_big->h == 600, "the resizable one took the tile");
    CHECK(g_clicks_big == 1,
          "and a click at its button's own coordinates reached it");
    CHECK(g_fixed->w == 300 && g_fixed->h == 200,
          "the fixed one kept the size it asked for");
    CHECK(g_clicks_fixed == 1,
          "and a click where its button appears on screen reached it");

    /* GetWindowRect and ClientToScreen have to measure from the same corner.
     * A window that wants to know where the pointer is inside itself reads a
     * child's rectangle back and puts it through ScreenToClient — the way a
     * splitter follows a drag — and if the two disagree it lands wherever the
     * difference is. */
    {
        HWND child = CreateWindowA("BUTTON", "x", WS_CHILD | WS_VISIBLE, 37,
                                   23, 40, 20, g_big, NULL, NULL, NULL);
        RECT r;
        POINT pt;
        GetWindowRect(child, &r);
        pt.x = r.left;
        pt.y = r.top;
        ScreenToClient(g_big, &pt);
        CHECK(pt.x == 37 && pt.y == 23,
              "a child's rectangle comes back through ScreenToClient as the "
              "place it was put");
        DestroyWindow(child);
    }

    DestroyWindow(g_big);
    DestroyWindow(g_fixed);
    ween_headless_set_window_size(0, 0);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("geometry_test: all passed\n");
    return 0;
}
