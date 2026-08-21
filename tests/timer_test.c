/* Timers, and the caret that hangs off them. Time is virtual under the
 * headless backend — a scripted "w:" event moves it — so this runs instantly
 * and always the same way. */

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

enum { ID_EDIT = 100, TIMER_TICK = 1, TIMER_PROC = 2 };

static HWND g_edit;
static int g_ticks, g_proc_ticks, g_caret_seen, g_caret_missing;

static void CALLBACK timer_fn(HWND h, UINT msg, UINT_PTR id, DWORD elapsed)
{
    (void)h;
    (void)elapsed;
    if (msg == WM_TIMER && id == TIMER_PROC)
        g_proc_ticks++;
}

/* Any black pixel inside the (empty) edit's client area is the caret. */
static void sample_caret(void)
{
    const ween_surface *s = ween_headless_surface();
    if (!s || !g_edit)
        return;
    int ox = WEEN_NC_FRAME + g_edit->x, oy = WEEN_NC_FRAME + WEEN_NC_CAPTION + g_edit->y;
    int found = 0;
    for (int y = oy + 2; y < oy + g_edit->h - 2 && !found; y++)
        for (int x = ox + 2; x < ox + g_edit->w - 2; x++)
            if ((s->px[(long)y * s->w + x] & 0xffffff) == 0) {
                found = 1;
                break;
            }
    if (found)
        g_caret_seen++;
    else
        g_caret_missing++;
}

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                 WS_CHILD | WS_VISIBLE | ES_LEFT, 10, 10, 120,
                                 20, hwnd, (HMENU)(UINT_PTR)ID_EDIT, NULL, NULL);
        SetFocus(g_edit); /* a focused edit starts its caret blinking */
        SetTimer(hwnd, TIMER_TICK, 100, NULL);
        SetTimer(hwnd, TIMER_PROC, 250, timer_fn);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_TICK) {
            g_ticks++;
            sample_caret();
            if (g_ticks == 6)
                KillTimer(hwnd, TIMER_PROC); /* the rest must not arrive */
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static void wait_ms(int ms)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_TIME;
    ev.x = ms;
    ween_headless_inject(ev);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weentimer";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    for (int i = 0; i < 10; i++) /* a second of scripted time, 100ms at a time */
        wait_ms(100);

    HWND wnd = CreateWindowExA(0, "weentimer", "timers",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               0, 0, 200, 120, NULL, NULL, NULL, NULL);
    CHECK(wnd != NULL, "a window with two timers on it");

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0))
        DispatchMessageA(&msg);

    CHECK(g_ticks == 10, "a 100ms timer fired ten times in a second");
    CHECK(g_proc_ticks == 2,
          "the 250ms timer fired twice, then stopped when it was killed");

    /* The caret is on for half of each blink and off for the other half, so a
     * run of samples has to contain both. */
    CHECK(g_caret_seen > 0, "the caret was drawn in some samples");
    CHECK(g_caret_missing > 0, "and absent in others: it blinks");

    CHECK(KillTimer(wnd, TIMER_TICK), "KillTimer finds a live timer");
    CHECK(!KillTimer(wnd, TIMER_TICK), "and not one already gone");

    DestroyWindow(wnd);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("timer_test: all passed\n");
    return 0;
}
