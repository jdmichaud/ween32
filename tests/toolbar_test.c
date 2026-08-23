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

enum { ID_BACK = 1, ID_UP, ID_FOLDERS, ID_DEAD, ID_VIEWS };

static HWND g_tb;
static int g_command, g_dropdown = -1;
/* the menu-mode bar under test, and how many times each of its titles was
 * asked to drop down */
static HWND g_menu_bar;
static int g_dropped[2];

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
        if (nm->code == TBN_DROPDOWN && nm->hwndFrom == g_menu_bar) {
            /* stand in for the menu this would put up: the first title's
             * "menu" is left for the second, as sliding along the bar does */
            int i = ((const NMTOOLBAR *)lp)->iItem == ID_BACK ? 0 : 1;
            g_dropped[i]++;
            if (i == 0)
                ween_toolbar_menu_switch(1);
        }
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

    TBBUTTON b[6];
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
    b[5].idCommand = ID_VIEWS;
    b[5].fsState = TBSTATE_ENABLED;
    b[5].fsStyle = TBSTYLE_BUTTON; /* no label: an image and nothing else */
    CHECK(SendMessageA(g_tb, TB_ADDBUTTONSA, 6, (LPARAM)b), "six buttons went in");
    CHECK(SendMessageA(g_tb, TB_BUTTONCOUNT, 0, 0) == 6, "and it counts six");

    /* The two widths a button can have, both measured off a real toolbar. A
     * labelled one is the image's inset, then the text, then seven. One with
     * nothing but an image is not padded symmetrically: the inset stays and
     * only two pixels follow the image. Get either wrong and the buttons
     * after it walk away from where they belong, a little at a time. */
    {
        RECT r;
        const ween_strike *f = ween_gui_font();
        int text = ween_strike_text_width(f, "Folders", 7);
        SendMessageA(g_tb, TB_GETITEMRECT, 3, (LPARAM)&r);
        CHECK(r.right - r.left == 24 + text + 7,
              "a labelled button is the inset, its text, and seven");
        SendMessageA(g_tb, TB_GETITEMRECT, 5, (LPARAM)&r);
        CHECK(r.right - r.left == 6 + 16 + 2,
              "one with only an image keeps the inset and adds two");
    }

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

    /* The arrow half of a drop-down button asks for a menu instead — once
     * the bar has been told to draw the arrows at all. Without that the
     * whole button is the drop-down, which is what a menu title is. */
    {
        RECT r, before;
        SendMessageA(g_tb, TB_GETITEMRECT, 0, (LPARAM)&before);
        SendMessageA(g_tb, TB_SETEXTENDEDSTYLE, 0,
                     (LPARAM)TBSTYLE_EX_DRAWDDARROWS);
        CHECK(SendMessageA(g_tb, TB_GETEXTENDEDSTYLE, 0, 0) ==
                  TBSTYLE_EX_DRAWDDARROWS,
              "a toolbar remembers being told to draw drop-down arrows");
        SendMessageA(g_tb, TB_GETITEMRECT, 0, (LPARAM)&r);
        CHECK(r.right - r.left > before.right - before.left,
              "and a drop-down button grows by the arrow half it now has");
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

    /* What a menu band asks of a toolbar: titles that are their text and a
     * padding, a button height of its own, and a letter that finds one. */
    {
        HWND bar = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                   WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT |
                                       TBSTYLE_LIST,
                                   0, 0, 300, 22, w, NULL, NULL, NULL);
        TBBUTTON t[2];
        RECT r;
        const ween_strike *f = ween_gui_font();
        int hit = -1;
        SendMessageA(bar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageA(bar, TB_SETPADDING, 0, MAKELPARAM(16, 0));
        SendMessageA(bar, TB_SETBUTTONSIZE, 0, MAKELPARAM(0, 19));
        memset(t, 0, sizeof(t));
        t[0].iBitmap = -1;
        t[0].idCommand = ID_BACK;
        t[0].fsState = TBSTATE_ENABLED;
        t[0].fsStyle = TBSTYLE_BUTTON | TBSTYLE_DROPDOWN;
        t[0].iString = (INT_PTR) "&File";
        t[1].iBitmap = -1;
        t[1].idCommand = ID_UP;
        t[1].fsState = TBSTATE_ENABLED;
        t[1].fsStyle = TBSTYLE_BUTTON | TBSTYLE_DROPDOWN;
        t[1].iString = (INT_PTR) "&Edit";
        SendMessageA(bar, TB_ADDBUTTONSA, 2, (LPARAM)t);
        SendMessageA(bar, TB_GETITEMRECT, 0, (LPARAM)&r);
        CHECK(r.right - r.left == ween_strike_text_width(f, "File", 4) + 16,
              "a title is its text and the padding, with the marker out of it");
        CHECK(r.bottom - r.top == 19 && r.top == (22 - 19) / 2,
              "and the height it was given, centred in the bar");
        CHECK(SendMessageA(bar, TB_MAPACCELERATORA, 'e', (LPARAM)&hit) &&
                  hit == 1,
              "a letter finds the title whose label marks it");
        CHECK(!SendMessageA(bar, TB_MAPACCELERATORA, 'z', (LPARAM)&hit),
              "and finds nothing when no label marks it");
        SendMessageA(bar, TB_SETHOTITEM, 1, 0);
        CHECK(SendMessageA(bar, TB_GETHOTITEM, 0, 0) == 1,
              "the keyboard can put a title under itself");

        /* Menu mode: a title opens on the press, not the release, and while
         * its menu is up the tracker can ask for the next one — which the bar
         * opens as soon as the first has closed. The application sees one
         * TBN_DROPDOWN per title and answers each the same way. */
        g_menu_bar = bar;
        g_dropped[0] = g_dropped[1] = 0;
        g_dropdown = -1;
        SendMessageA(bar, WM_LBUTTONDOWN, 0, MAKELPARAM(r.left + 4, 10));
        CHECK(g_dropped[0] == 1 && g_dropped[1] == 1,
              "a title opens on the press, and the bar goes on to the one the "
              "tracker asks for");
        /* And with the menus closed the bar is flat again: a title is lit
         * only while its menu is up or the pointer is on it, and here the
         * pointer never went there — the presses were sent, not pointed. */
        CHECK(SendMessageA(bar, TB_GETHOTITEM, 0, 0) == -1,
              "and with them closed the bar is lit by nothing");
        g_menu_bar = NULL;
        DestroyWindow(bar);
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

    /* A rebar: the bands a shell's toolbars sit in, each a row with a gripper
     * and the control filling what is left of it. */
    {
        HWND rebar = CreateWindowExA(0, REBARCLASSNAMEA, "",
                                     WS_CHILD | WS_VISIBLE | RBS_BANDBORDERS,
                                     0, 30, 460, 60, w, NULL, NULL, NULL);
        CHECK(rebar != NULL, "a rebar");

        HWND addr = CreateWindowExA(WS_EX_CLIENTEDGE, "COMBOBOX", "",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0,
                                    0, 200, 21, rebar, NULL, NULL, NULL);
        REBARBANDINFOA bi;
        memset(&bi, 0, sizeof(bi));
        bi.cbSize = sizeof(bi);
        bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE;
        bi.hwndChild = g_tb;
        bi.cyMinChild = 22;
        CHECK(SendMessageA(rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi),
              "a band holding the toolbar went in");

        bi.fMask = RBBIM_CHILD | RBBIM_TEXT | RBBIM_CHILDSIZE | RBBIM_STYLE;
        bi.fStyle = RBBS_BREAK; /* a row of its own, as a shell's bars have */
        bi.hwndChild = addr;
        bi.lpText = (char *)"Address";
        CHECK(SendMessageA(rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi),
              "and a second one with a label before its control");
        CHECK(SendMessageA(rebar, RB_GETBANDCOUNT, 0, 0) == 2,
              "the rebar counts two bands");

        /* Two bands of 22 with an edge over each and one under the last:
         * the rebar is ruled off top and bottom, not only between — which is
         * what RBS_BANDBORDERS asks for, and without it there are no rules
         * and no height for them. */
        int height = (int)SendMessageA(rebar, RB_GETBARHEIGHT, 0, 0);
        CHECK(height == 2 + 22 + 2 + 22 + 2,
              "and is both of them, each ruled off, and ruled off underneath");

        /* the bands moved their children into place, stacked */
        RECT tbr, ar;
        GetWindowRect(g_tb, &tbr);
        GetWindowRect(addr, &ar);
        CHECK(ar.top > tbr.top, "the second band sits below the first");
        CHECK(tbr.left > 0 && ar.left > tbr.left,
              "both are inset past the gripper, and the labelled one further");

        /* A band that does not ask to break shares the row with the one
         * before it — which is what a rebar is named for, and what an
         * application that never asks for the break gets on Windows. */
        {
            HWND side = CreateWindowA("BUTTON", "beside", WS_CHILD | WS_VISIBLE,
                                      0, 0, 60, 22, rebar, NULL, NULL, NULL);
            RECT sr, ar2;
            int was = (int)SendMessageA(rebar, RB_GETBARHEIGHT, 0, 0);
            bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE;
            bi.fStyle = 0;
            bi.hwndChild = side;
            bi.cyMinChild = 22;
            SendMessageA(rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);
            CHECK((int)SendMessageA(rebar, RB_GETBARHEIGHT, 0, 0) == was,
                  "a band that does not break adds no height");
            GetWindowRect(addr, &ar2);
            GetWindowRect(side, &sr);
            CHECK(sr.top == ar2.top && sr.left > ar2.left,
                  "it sits beside the band before it, on the same row");
            DestroyWindow(side);
        }

        /* A control created as the rebar's own child, the way a shell makes
         * them: what it sends goes to its parent, which is the rebar, and the
         * rebar has to pass it on or every button in the band is dead. */
        {
            HWND inner = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                         WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT,
                                         0, 0, 100, 22, rebar, NULL, NULL,
                                         NULL);
            TBBUTTON b;
            RECT r;
            SendMessageA(inner, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
            memset(&b, 0, sizeof(b));
            b.iBitmap = -1;
            b.idCommand = ID_BACK;
            b.fsState = TBSTATE_ENABLED;
            b.fsStyle = TBSTYLE_BUTTON;
            SendMessageA(inner, TB_ADDBUTTONSA, 1, (LPARAM)&b);
            SendMessageA(inner, TB_GETITEMRECT, 0, (LPARAM)&r);
            g_command = 0;
            SendMessageA(inner, WM_LBUTTONDOWN, 0,
                         MAKELPARAM((r.left + r.right) / 2, 10));
            SendMessageA(inner, WM_LBUTTONUP, 0,
                         MAKELPARAM((r.left + r.right) / 2, 10));
            CHECK(g_command == ID_BACK,
                  "a rebar passes on what a control inside it sends");
            DestroyWindow(inner);
        }
        DestroyWindow(rebar);
    }

    DestroyWindow(w);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("toolbar_test: all passed\n");
    return 0;
}
