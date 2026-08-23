/* A window that stands over another one without taking it over: a menu, a
 * drop-down, a box of suggestions under a field. It is a window of its own,
 * so the window system has to place it where it was put, hand it the presses
 * that land on it, and leave the keyboard with whatever had it. Through the
 * headless backend, so CI runs it. */

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

enum { ID_FIELD = 100, ID_LIST = 101 };

static HWND g_main, g_field, g_pop, g_list;
static int g_main_clicks, g_pop_clicks, g_sized;

static LRESULT CALLBACK main_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_field = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                  WS_CHILD | WS_VISIBLE | ES_LEFT, 10, 10, 200,
                                  20, w, (HMENU)(UINT_PTR)ID_FIELD, NULL, NULL);
        return 0;
    case WM_LBUTTONDOWN:
        g_main_clicks++;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static LRESULT CALLBACK pop_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
        g_pop_clicks++;
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_SIZE) {
            /* what the corner reports: where the pointer is on the screen,
             * which is the one frame that does not move while the window
             * being sized does */
            g_sized = GET_Y_LPARAM(lp);
            return 0;
        }
        break;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void press(int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_MOUSE_DOWN;
    ev.button = 1;
    ev.x = x; /* measured against the active window, as a script's are */
    ev.y = y;
    ween_headless_inject(ev);
}

int main(void)
{
    MSG msg;
    RECT r;
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpfnWndProc = main_proc;
    wc.lpszClassName = "weenpopmain";
    RegisterClassA(&wc);
    wc.lpfnWndProc = pop_proc;
    wc.lpszClassName = "weenpop";
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    RegisterClassA(&wc);

    g_main = CreateWindowExA(0, "weenpopmain", "host",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0,
                             0, 400, 300, NULL, NULL, NULL, NULL);
    SetFocus(g_field);
    CHECK(GetFocus() == g_field, "the field in the window under it has focus");

    /* A window with no caption is one the application places itself: no
     * manager is going to move it, and it is not to be activated. */
    g_pop = CreateWindowExA(WS_EX_NOACTIVATE, "weenpop", "",
                            WS_POPUP | WS_BORDER, 0, 0, 200, 100, NULL, NULL,
                            NULL, NULL);
    CHECK(g_pop != NULL, "a popup with no caption was created");
    CHECK(GetFocus() == g_field,
          "putting it up left the keyboard where it was");

    /* Made in one place and moved to another, which is what a box that has to
     * follow the field it belongs to does. It has to move on the screen, not
     * only in the bookkeeping. */
    MoveWindow(g_pop, 120, 60, 200, 100, TRUE);
    ShowWindow(g_pop, SW_SHOW);
    GetWindowRect(g_pop, &r);
    CHECK(r.left == 120 && r.top == 60,
          "moving it moved it in the window system, not just on paper");
    CHECK(r.right == 320 && r.bottom == 160, "and it is the size it was given");

    CHECK(SetFocus(g_pop) == g_field && GetFocus() == g_field,
          "it refuses the keyboard: that is what not-to-be-activated means");

    g_list = CreateWindowExA(0, "LISTBOX", "",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 1,
                             1, 180, 90, g_pop, (HMENU)(UINT_PTR)ID_LIST, NULL,
                             NULL);
    SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM) "alpha");
    SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM) "beta");
    SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM) "gamma");

    {   /* a list box knows how tall a row is, and will say */
        int ih = (int)SendMessageA(g_list, LB_GETITEMHEIGHT, 0, 0);
        CHECK(ih > 0, "the list box says how tall one of its rows is");
        CHECK((g_list->h - 2 * ween_ex_edge(g_list)) % ih == 0 &&
                  g_list->h <= 90,
              "and trimmed the height it was made with down to whole ones");
        /* made a different size, it still ends on a row rather than in the
         * middle of a name */
        MoveWindow(g_list, 1, 1, 180, 90 + ih + ih / 2, TRUE);
        CHECK((g_list->h - 2 * ween_ex_edge(g_list)) % ih == 0,
              "resized, it snaps back to whole rows");
    }

    {   /* and will hand back what was put in it */
        char out[64];
        LRESULT n = SendMessageA(g_list, LB_GETTEXT, 1, (LPARAM)out);
        CHECK(n == 4 && strcmp(out, "beta") == 0,
              "an item's text comes back, with its length");
        CHECK(SendMessageA(g_list, LB_GETTEXTLEN, 2, 0) == 5,
              "and its length on its own");
        CHECK(SendMessageA(g_list, LB_GETTEXT, 9, (LPARAM)out) == LB_ERR &&
                  out[0] == 0,
              "an item it has not got is an error and an empty string");
    }

    /* A press over the popup is the popup's, even though the window under it
     * is the active one — which is the whole reason a menu can be clicked. */
    press(310, 70); /* the popup's own area, past the list inside it */
    press(130, 70); /* and the first row of that list */
    while (GetMessageA(&msg, NULL, 0, 0))
        DispatchMessageA(&msg);
    CHECK(g_pop_clicks == 1, "a press over the popup reached the popup");
    CHECK(g_main_clicks == 0, "and not the window it is standing on");
    CHECK(SendMessageA(g_list, LB_GETCURSEL, 0, 0) == 0,
          "one over the list inside it picked the row it landed on");
    CHECK(GetFocus() == g_field,
          "and through all of it the field still has the keyboard");

    /* The corner a window is dragged bigger by reports the pointer where it
     * will still mean the same thing a moment later: on the screen. */
    {
        HWND grip = CreateWindowExA(0, "SCROLLBAR", "",
                                    WS_CHILD | WS_VISIBLE | SBS_SIZEGRIP, 180,
                                    84, 16, 16, g_pop, NULL, NULL, NULL);
        SendMessageA(grip, WM_LBUTTONDOWN, 0, MAKELPARAM(8, 8));
        CHECK(g_sized == 60 + 84 + 8,
              "the corner says where the pointer is on the screen");
    }

    DestroyWindow(g_pop);
    DestroyWindow(g_main);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("popup_test: all passed\n");
    return 0;
}
