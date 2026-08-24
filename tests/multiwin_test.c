/* Two top-level windows at once: both are created, both paint into their own
 * surface, each hears its own messages, and closing one leaves the other
 * running. Through the headless backend, so CI runs it. */

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

static int g_painted_a, g_painted_b, g_clicked_b;

static LRESULT CALLBACK proc_a(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        g_painted_a++;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK proc_b(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        g_painted_b++;
        return 0;
    case WM_LBUTTONDOWN:
        g_clicked_b++;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpfnWndProc = proc_a;
    wc.lpszClassName = "weenmain";
    RegisterClassA(&wc);
    wc.lpfnWndProc = proc_b;
    wc.lpszClassName = "weensecond";
    RegisterClassA(&wc);

    HWND a = CreateWindowExA(0, "weenmain", "first",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0,
                             0, 300, 200, NULL, NULL, NULL, NULL);
    CHECK(a != NULL, "the first top-level window was created");

    HWND b = CreateWindowExA(0, "weensecond", "second",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                             320, 40, 180, 120, NULL, NULL, NULL, NULL);
    CHECK(b != NULL, "a second top-level window was created too");
    CHECK(a != b, "they are distinct windows");

    /* each owns its surface, sized to itself */
    CHECK(a->surface.w == 300 && a->surface.h == 200 && a->surface.px != NULL,
          "the first window has its own surface");
    CHECK(b->surface.w == 180 && b->surface.h == 120 && b->surface.px != NULL,
          "the second has its own, at its own size");
    CHECK(a->surface.px != b->surface.px, "the two surfaces are not shared");
    CHECK(a->backend_win != b->backend_win,
          "each window has its own backend window");

    /* a click goes to the window it names, not to whichever came first */
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_MOUSE_DOWN;
    ev.button = 1;
    ev.win = b->backend_win;
    ev.x = WEEN_NC_FRAME + 20;
    ev.y = WEEN_NC_FRAME + WEEN_NC_CAPTION + 20;
    ween_headless_inject(ev);

    /* A key arriving on a window does not make it the active one. Which
     * window a key arrives on is the window system's business -- a window
     * manager hands a palette the keyboard the moment it puts it up -- and
     * where the key goes is settled here, by what the library holds to be
     * active. The press above made the second window active; a key on the
     * first has to leave it that way. */
    ev.kind = WEEN_EV_KEY;
    ev.win = a->backend_win;
    ev.vk = 'A';
    ev.ch = 'a';
    ween_headless_inject(ev);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0))
        DispatchMessageA(&msg);

    CHECK(GetActiveWindow() == b,
          "a key on the other window leaves the active one where it was");

    CHECK(g_painted_a > 0, "the first window painted");
    CHECK(g_painted_b > 0, "the second window painted as well");
    CHECK(g_clicked_b == 1, "the click reached the window the event named");

    /* An expose belongs to the window it names, whoever is looking at events
     * at the time. A nested loop — a drag, a menu being tracked — used to
     * swallow the ones meant for other windows, which left whatever had been
     * covered up as a lump of grey. */
    {
        ween_event ex;
        memset(&ex, 0, sizeof(ex));
        ex.kind = WEEN_EV_EXPOSE;
        ex.win = a->backend_win;
        a->dirty = 0;
        b->dirty = 0;
        ween_mark_exposed(&ex);
        CHECK(a->dirty && !b->dirty, "an expose marks the window it names");

        ex.win = b->backend_win;
        a->dirty = 0;
        ween_mark_exposed(&ex);
        CHECK(b->dirty && !a->dirty, "and only that one");
    }

    /* closing one leaves the other alive and still able to paint */
    DestroyWindow(b);
    g_painted_a = 0;
    InvalidateRect(a, NULL, TRUE);
    ween_flush_paint();
    CHECK(g_painted_a > 0, "the survivor still paints after the other closed");
    CHECK(a->surface.px != NULL, "and still owns its surface");

    DestroyWindow(a);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("multiwin_test: all passed\n");
    return 0;
}
