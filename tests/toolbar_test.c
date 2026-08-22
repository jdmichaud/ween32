/* The toolbar: buttons that press, check, grey out, hot-track, and tell the
 * app which one was used. A shell's toolbar is the one control where "flat"
 * is a behaviour rather than a look — the edge only appears under the
 * pointer — so the hot state is as much a feature as the click. */

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

enum { ID_BACK = 1, ID_UP, ID_FOLDERS, ID_DEAD };

static HWND g_tb;
static int g_command, g_dropdown = -1;

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND) {
        g_command = LOWORD(wp);
        return 0;
    }
    if (msg == WM_NOTIFY) {
        const NMHDR *nm = (const NMHDR *)lp;
        if (nm->code == TBN_DROPDOWN)
            g_dropdown = ((const NMTOOLBAR *)lp)->iItem;
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* A press and release at a point inside the toolbar. */
static void click_at(int x)
{
    SendMessageA(g_tb, WM_LBUTTONDOWN, 0, MAKELPARAM(x, 10));
    SendMessageA(g_tb, WM_LBUTTONUP, 0, MAKELPARAM(x, 10));
}

static int button_left(int i)
{
    RECT r;
    SendMessageA(g_tb, TB_GETITEMRECT, (WPARAM)i, (LPARAM)&r);
    return r.left;
}

static int button_middle(int i)
{
    RECT r;
    SendMessageA(g_tb, TB_GETITEMRECT, (WPARAM)i, (LPARAM)&r);
    return (r.left + r.right) / 2;
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weentb";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    HWND w = CreateWindowExA(0, "weentb", "toolbar",
                             WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 460, 80,
                             NULL, NULL, NULL, NULL);
    g_tb = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                           WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST,
                           0, 0, 460, 22, w, (HMENU)(UINT_PTR)10, NULL, NULL);
    CHECK(g_tb != NULL, "a toolbar");

    TBBUTTON b[5];
    memset(b, 0, sizeof(b));
    b[0].idCommand = ID_BACK;
    b[0].fsState = TBSTATE_ENABLED;
    b[0].fsStyle = TBSTYLE_BUTTON | TBSTYLE_DROPDOWN;
    b[0].iString = (INT_PTR) "Back";
    b[1].fsStyle = TBSTYLE_SEP;
    b[2].idCommand = ID_UP;
    b[2].fsState = TBSTATE_ENABLED;
    b[2].fsStyle = TBSTYLE_BUTTON;
    b[2].iString = (INT_PTR) "Up";
    b[3].idCommand = ID_FOLDERS;
    b[3].fsState = TBSTATE_ENABLED;
    b[3].fsStyle = TBSTYLE_CHECK;
    b[3].iString = (INT_PTR) "Folders";
    b[4].idCommand = ID_DEAD;
    b[4].fsState = 0; /* not enabled */
    b[4].fsStyle = TBSTYLE_BUTTON;
    b[4].iString = (INT_PTR) "Nope";
    CHECK(SendMessageA(g_tb, TB_ADDBUTTONSA, 5, (LPARAM)b), "five buttons went in");
    CHECK(SendMessageA(g_tb, TB_BUTTONCOUNT, 0, 0) == 5, "and it counts five");

    /* Each button has a rectangle, and they run left to right without gaps. */
    CHECK(button_left(0) == 0, "the first button starts at the left edge");
    CHECK(button_left(2) > button_left(0),
          "and the ones after it are further along");

    /* A click reaches the app as WM_COMMAND with the button's id. */
    g_command = 0;
    click_at(button_middle(2)); /* "Up" */
    CHECK(g_command == ID_UP, "clicking a button sends its command");

    /* A check button toggles, and says so. */
    CHECK(!SendMessageA(g_tb, TB_ISBUTTONCHECKED, ID_FOLDERS, 0),
          "a check button starts unchecked");
    click_at(button_middle(3));
    CHECK(SendMessageA(g_tb, TB_ISBUTTONCHECKED, ID_FOLDERS, 0),
          "clicking it checks it");
    click_at(button_middle(3));
    CHECK(!SendMessageA(g_tb, TB_ISBUTTONCHECKED, ID_FOLDERS, 0),
          "and clicking again unchecks it");

    /* A disabled button does nothing at all. */
    g_command = 0;
    click_at(button_middle(4));
    CHECK(g_command == 0, "a disabled button ignores a click");
    SendMessageA(g_tb, TB_ENABLEBUTTON, ID_DEAD, MAKELPARAM(TRUE, 0));
    click_at(button_middle(4));
    CHECK(g_command == ID_DEAD, "and works once it is enabled");

    /* The arrow half of a drop-down button asks for a menu instead. */
    {
        RECT r;
        SendMessageA(g_tb, TB_GETITEMRECT, 0, (LPARAM)&r);
        g_command = 0;
        g_dropdown = -1;
        click_at(r.right - 4); /* inside the arrow */
        CHECK(g_dropdown == ID_BACK, "the arrow half asks for a drop-down");
        CHECK(g_command == 0, "and does not send the button's command");

        g_dropdown = -1;
        click_at(r.left + 20); /* the body */
        CHECK(g_command == ID_BACK, "while the body still sends it");
        CHECK(g_dropdown == -1, "and asks for no menu");
    }

    /* A separator is not a button: it cannot be pressed. */
    g_command = 0;
    click_at(button_middle(1));
    CHECK(g_command == 0, "a separator swallows nothing and sends nothing");

    /* Releasing somewhere other than where the press started does nothing,
     * which is what lets a person change their mind. */
    g_command = 0;
    SendMessageA(g_tb, WM_LBUTTONDOWN, 0, MAKELPARAM(button_middle(2), 10));
    SendMessageA(g_tb, WM_LBUTTONUP, 0, MAKELPARAM(button_middle(3), 10));
    CHECK(g_command == 0, "dragging off a button before letting go cancels it");

    DestroyWindow(w);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("toolbar_test: all passed\n");
    return 0;
}
