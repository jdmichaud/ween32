/* The controls under the mouse and the keyboard: scripted input through the
 * headless backend, asserting the state each control ends up in. No display
 * needed, so CI runs it. */

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

enum { ID_EDIT = 100, ID_CHECK, ID_LIST, ID_TRACK };

static HWND g_edit, g_check, g_list, g_track;
static int g_edit_changed, g_check_clicked, g_list_changed, g_scrolled;

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "abc",
                                 WS_CHILD | WS_VISIBLE | ES_LEFT, 10, 10, 120,
                                 20, hwnd, (HMENU)(UINT_PTR)ID_EDIT, NULL, NULL);
        g_check = CreateWindowA("BUTTON", "check",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 10, 40,
                                100, 18, hwnd, (HMENU)(UINT_PTR)ID_CHECK, NULL,
                                NULL);
        g_list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                                 WS_CHILD | WS_VISIBLE | LBS_NOTIFY, 10, 65, 120,
                                 56, hwnd, (HMENU)(UINT_PTR)ID_LIST, NULL, NULL);
        SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM) "one");
        SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM) "two");
        SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM) "three");
        SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM) "four");
        SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM) "five");
        g_track = CreateWindowA(TRACKBAR_CLASSA, "",
                                WS_CHILD | WS_VISIBLE | TBS_HORZ, 10, 130, 100,
                                30, hwnd, (HMENU)(UINT_PTR)ID_TRACK, NULL, NULL);
        SendMessageA(g_track, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10));
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_EDIT && HIWORD(wp) == EN_CHANGE)
            g_edit_changed++;
        if (LOWORD(wp) == ID_CHECK && HIWORD(wp) == BN_CLICKED)
            g_check_clicked++;
        if (LOWORD(wp) == ID_LIST && HIWORD(wp) == LBN_SELCHANGE)
            g_list_changed++;
        return 0;
    case WM_HSCROLL:
        g_scrolled++;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static void inject(ween_ev_kind kind, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.x = x;
    ev.y = y;
    ev.button = 1;
    ween_headless_inject(ev);
}

static void type_char(unsigned ch)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_KEY;
    ev.ch = ch;
    ev.vk = ch >= 'a' && ch <= 'z' ? ch - 32 : ch;
    ween_headless_inject(ev);
}

static void wheel(int notches, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_WHEEL;
    ev.button = notches;
    ev.x = x;
    ev.y = y;
    ween_headless_inject(ev);
}

static void press(unsigned vk)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_KEY;
    ev.vk = vk;
    ween_headless_inject(ev);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weeninput";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    /* client origin, so the script can be written in client coordinates */
    const int cx = WEEN_NC_FRAME, cy = WEEN_NC_FRAME + WEEN_NC_CAPTION;

    /* type into the edit: click at its end, then "de", then a backspace */
    inject(WEEN_EV_MOUSE_DOWN, cx + 120, cy + 20);
    inject(WEEN_EV_MOUSE_UP, cx + 120, cy + 20);
    press(VK_END);
    type_char('d');
    type_char('e');
    press(VK_BACK);

    /* click the check box, then an item in the list box */
    inject(WEEN_EV_MOUSE_DOWN, cx + 20, cy + 49);
    inject(WEEN_EV_MOUSE_UP, cx + 20, cy + 49);
    inject(WEEN_EV_MOUSE_DOWN, cx + 40, cy + 80);
    inject(WEEN_EV_MOUSE_UP, cx + 40, cy + 80);

    /* the wheel scrolls the focused list box, and does not select */
    wheel(-1, cx + 40, cy + 80);

    /* drag the trackbar's thumb to the far end */
    inject(WEEN_EV_MOUSE_DOWN, cx + 30, cy + 145);
    inject(WEEN_EV_MOUSE_MOVE, cx + 100, cy + 145);
    inject(WEEN_EV_MOUSE_UP, cx + 100, cy + 145);

    HWND wnd = CreateWindowExA(0, "weeninput", "input test",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               0, 0, 200, 200, NULL, NULL, NULL, NULL);
    CHECK(wnd != NULL, "the host window and its controls were created");

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    char text[64] = "";
    GetWindowTextA(g_edit, text, sizeof text);
    CHECK(strcmp(text, "abcd") == 0, "typing and backspace edited the text");
    CHECK(g_edit_changed == 3, "each edit sent EN_CHANGE");

    CHECK(SendMessageA(g_check, BM_GETCHECK, 0, 0) == BST_CHECKED,
          "clicking the check box checked it");
    CHECK(g_check_clicked == 1, "the check box sent BN_CLICKED");

    CHECK(SendMessageA(g_list, LB_GETCURSEL, 0, 0) == 1,
          "clicking the list box selected the item under the cursor");
    CHECK(g_list_changed == 1, "the list box sent LBN_SELCHANGE");

    CHECK(SendMessageA(g_list, LB_GETTOPINDEX, 0, 0) > 0,
          "the wheel scrolled the list box");
    CHECK(SendMessageA(g_list, LB_GETCURSEL, 0, 0) == 1,
          "the wheel did not change the selection");

    CHECK(SendMessageA(g_track, TBM_GETPOS, 0, 0) == 10,
          "dragging the trackbar moved it to the end of its range");
    CHECK(g_scrolled > 0, "the trackbar sent WM_HSCROLL while dragging");

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("input_test: all passed\n");
    return 0;
}
