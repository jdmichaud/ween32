/* A drag of the rich edit's text, driven the way a person drives one.
 *
 * Every other test of the editor's mouse -- and there are twenty of them in
 * tests/richedit_test.c -- hands the control its messages: `SendMessageA(re,
 * WM_LBUTTONDOWN, ...)`. That proves the handler and says nothing about the
 * path to it. **jd found four things in one evening that exist only while a
 * button is held down**, and nothing here had ever held one, so this drives
 * the whole way: injected events, the backend's queue, the routing, the hit
 * test and the capture.
 *
 * It is its own program for one reason: **the headless message loop ends when
 * its queue runs dry.** `GetMessage` answers nought at `WEEN_EV_END` and goes
 * on answering nought, so a suite that has already drained it once cannot
 * pump anything afterwards -- and a test written that way reads as a broken
 * path rather than a finished loop. That cost the first version of this an
 * hour, and `PeekMessageA` is not implemented here to work around it.
 */

#define _POSIX_C_SOURCE 200112L

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

static LRESULT CALLBACK host_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(w, m, wp, lp);
}

static void at(int kind, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = kind;
    ev.button = 1;
    ev.x = x; /* window coordinates, as a script's are */
    ev.y = y;
    ween_headless_inject(ev);
}

int main(void)
{
    WNDCLASSA wc;
    HWND host, re;
    CHARRANGE cr;
    POINTL a, c;
    MSG msg;
    int ox, oy;

    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenrichdrag";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "weenrichdrag", "drag",
                           WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 400, 200,
                           NULL, NULL, NULL, NULL);
    re = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE, 10, 10, 300, 60,
                         host, NULL, NULL, NULL);
    CHECK(re != NULL, "a rich edit in a window a pointer can reach");
    SetWindowTextA(re, "alpha bravo charlie");
    SetFocus(re);

    /* Where the two words are, asked of the control rather than worked out:
     * the drag has to land on a character, and which pixel that is depends on
     * the strike. */
    ween_client_origin(re, &ox, &oy);
    a.x = a.y = c.x = c.y = 0;
    SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&a, 1); /* inside "alpha" */
    SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&c, 8); /* inside "bravo" */

    /* The whole gesture goes in before anything is pumped, for the reason in
     * the header: a drain between two injections is a drain that finishes the
     * loop. */
    at(WEEN_EV_MOUSE_DOWN, ox + a.x + 1, oy + a.y + 2);
    at(WEEN_EV_MOUSE_MOVE, ox + (a.x + c.x) / 2, oy + a.y + 2);
    at(WEEN_EV_MOUSE_MOVE, ox + c.x + 1, oy + c.y + 2);
    at(WEEN_EV_MOUSE_UP, ox + c.x + 1, oy + c.y + 2);
    while (GetMessageA(&msg, NULL, 0, 0))
        DispatchMessageA(&msg);

    memset(&cr, 0, sizeof cr);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
    CHECK(cr.cpMin == 0 && cr.cpMax == 12,
          "a press, two moves and a release, all injected, select \"alpha \" "
          "and \"bravo \" whole -- so the path from a pixel to the selection "
          "works, and not only the handler at the end of it");
    if (!(cr.cpMin == 0 && cr.cpMax == 12))
        printf("     the drag left %d..%d\n", (int)cr.cpMin, (int)cr.cpMax);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("richdrag_test: all passed\n");
    return 0;
}
