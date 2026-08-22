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

enum { ID_EDIT = 100, ID_CHECK, ID_LIST, ID_TRACK, ID_GROUP, ID_NESTED };

static HWND g_edit, g_check, g_list, g_track, g_group, g_nested;
static int g_nested_clicked, g_left, g_nested_rdown;
static HWND g_ctx_from;
static int g_ctx_x, g_ctx_y;

/* A control that wants a hot state: it asks to hear when the pointer goes. */
static LRESULT CALLBACK nested_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tme;
        memset(&tme, 0, sizeof(tme));
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = w;
        TrackMouseEvent(&tme);
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        g_left++;
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        g_nested_clicked++;
        return 0;
    }
    if (msg == WM_RBUTTONDOWN) {
        g_nested_rdown++;
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}
static int g_edit_changed, g_check_clicked, g_list_changed, g_scrolled;


static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    /* a control that has no menu of its own leaves the request to its
     * parent, and it arrives here naming the window it started in */
    case WM_CONTEXTMENU:
        g_ctx_from = (HWND)wp;
        g_ctx_x = GET_X_LPARAM(lp);
        g_ctx_y = GET_Y_LPARAM(lp);
        return 0;
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
        /* a control inside a control: only the innermost should be hit */
        g_group = CreateWindowA("BUTTON", "Group",
                                WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 130, 10,
                                60, 60, hwnd, (HMENU)(UINT_PTR)ID_GROUP, NULL,
                                NULL);
        g_nested = CreateWindowA("weennested", "", WS_CHILD | WS_VISIBLE, 10, 20,
                                 40, 20, g_group, (HMENU)(UINT_PTR)ID_NESTED,
                                 NULL, NULL);
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

/* the right button, which is the one that asks for a context menu */
static void right_click(int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.x = x;
    ev.y = y;
    ev.button = 3;
    ev.kind = WEEN_EV_MOUSE_DOWN;
    ween_headless_inject(ev);
    ev.kind = WEEN_EV_MOUSE_UP;
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
    wc.lpfnWndProc = nested_proc;
    wc.lpszClassName = "weennested";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

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

    /* the nested control is inside the group box: the click belongs to it,
     * and the group box must not swallow it */
    inject(WEEN_EV_MOUSE_MOVE, cx + 130 + 20, cy + 10 + 30);
    inject(WEEN_EV_MOUSE_DOWN, cx + 130 + 20, cy + 10 + 30);
    inject(WEEN_EV_MOUSE_UP, cx + 130 + 20, cy + 10 + 30);

    /* the same spot with the right button: the control hears the press and
     * the request for a menu goes up to the frame */
    right_click(cx + 130 + 20, cy + 10 + 30);
    /* and moving off it is what WM_MOUSELEAVE is for */
    inject(WEEN_EV_MOUSE_MOVE, cx + 5, cy + 5);

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

    /* Window text is not capped: it used to stop at 128 bytes, silently. */
    {
        char big[4000];
        memset(big, 'w', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        CHECK(SetWindowTextA(g_edit, big), "a 4000-byte string is accepted");
        char back[4096];
        int n = GetWindowTextA(g_edit, back, sizeof back);
        CHECK(n == (int)sizeof(big) - 1 && strcmp(back, big) == 0,
              "and comes back whole, not truncated");
        SetWindowTextA(g_edit, "abcd");
    }
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

    CHECK(g_nested_clicked == 1,
          "a control nested inside another got the click, not its parent");
    CHECK(g_left == 1, "and heard WM_MOUSELEAVE when the pointer went away");

    CHECK(g_nested_rdown == 1, "the right button reached the control too");
    CHECK(g_ctx_from == g_nested,
          "and its parent was asked for a menu, told which window it was in");
    {
        /* the click landed ten in and ten down inside the control, which
         * sits at (10,20) in a group box at (130,10) */
        POINT pt;
        pt.x = 10;
        pt.y = 10;
        ClientToScreen(g_nested, &pt);
        CHECK(g_ctx_x == pt.x && g_ctx_y == pt.y,
              "the point comes in screen coordinates, where a menu goes");
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("input_test: all passed\n");
    return 0;
}
