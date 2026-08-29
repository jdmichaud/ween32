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
static int g_tip_asked = -1; /* the button the bar asked about */
static int g_heights;        /* how many times a rebar said its height moved */
static int g_begindrag, g_enddrag, g_layouts, g_dragband = -1;
static int g_chevrons, g_chevband = -1;
static RECT g_chevrect;
static int g_dropped[2];

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND) {
        g_command = LOWORD(wp);
        return 0;
    }
    if (msg == WM_NOTIFY) {
        const NMHDR *nm = (const NMHDR *)lp;
        if (nm->code == RBN_HEIGHTCHANGE) {
            g_heights++;
            return 0;
        }
        if (nm->code == RBN_BEGINDRAG) {
            g_begindrag++;
            g_dragband = (int)((const NMREBAR *)lp)->uBand;
            return 0;
        }
        if (nm->code == RBN_ENDDRAG) {
            g_enddrag++;
            return 0;
        }
        if (nm->code == RBN_LAYOUTCHANGED) {
            g_layouts++;
            return 0;
        }
        if (nm->code == RBN_CHEVRONPUSHED) {
            const NMREBARCHEVRON *cv = (const NMREBARCHEVRON *)lp;
            g_chevrons++;
            g_chevband = (int)cv->uBand;
            g_chevrect = cv->rc;
            return 0;
        }
        if (nm->code == TTN_GETDISPINFOA) {
            NMTTDISPINFOA *ti = (NMTTDISPINFOA *)lp;
            g_tip_asked = (int)ti->hdr.idFrom;
            snprintf(ti->szText, sizeof(ti->szText), "Delete");
            ti->lpszText = ti->szText;
            return 0;
        }
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

    /* The tip a button shows when the pointer rests on it: the bar waits, asks
     * the window it belongs to what to say, and puts a window of its own where
     * the pointer is. Its shape and where it goes are the machine's, measured
     * off its own Explorer — "Delete" is thirty-seven by seventeen, and its
     * corner is the pointer's, twenty-one down. */
    {
        HWND bar = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                   WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT |
                                       TBSTYLE_TOOLTIPS,
                                   0, 0, 200, 22, w, NULL, NULL, NULL);
        TBBUTTON t[2];
        HWND tip;
        RECT r;
        POINT pt;
        memset(t, 0, sizeof(t));
        t[0].iBitmap = 0;
        t[0].idCommand = ID_BACK;
        t[0].fsState = TBSTATE_ENABLED;
        t[0].fsStyle = TBSTYLE_BUTTON;
        t[1] = t[0];
        t[1].idCommand = ID_UP;
        t[1].iString = (INT_PTR) "Labelled"; /* one that says what it is */
        SendMessageA(bar, TB_ADDBUTTONSA, 2, (LPARAM)t);

        /* the pointer rests on the first button, and the tip goes where the
         * window system says the pointer is — which is what it is being
         * placed by */
        SendMessageA(bar, WM_MOUSEMOVE, 0, MAKELPARAM(8, 10));
        CHECK(SendMessageA(bar, TB_GETTOOLTIPS, 0, 0) == 0,
              "no tip is made before one is needed");
        SendMessageA(bar, WM_TIMER, 0x7e01, 0);
        tip = (HWND)(INT_PTR)SendMessageA(bar, TB_GETTOOLTIPS, 0, 0);
        CHECK(tip != NULL && IsWindowVisible(tip),
              "resting on a button with no label of its own shows a tip");
        CHECK(g_tip_asked == ID_BACK,
              "and what it says was asked of the window the bar is in");
        GetWindowRect(tip, &r);
        GetCursorPos(&pt);
        CHECK(r.left == pt.x && r.top == pt.y + 21,
              "its corner is the pointer's, twenty-one pixels down");
        CHECK(r.right - r.left ==
                  ween_strike_text_width(ween_gui_font(), "Delete", 6) + 6,
              "it is as wide as its words and six");
        CHECK(r.bottom - r.top == 17, "and seventeen tall");

        /* off the button, and it goes */
        SendMessageA(bar, WM_MOUSEMOVE, 0, MAKELPARAM(150, 10));
        CHECK(!IsWindowVisible(tip), "moving off the button puts it away");

        /* a button that shows its own label is never asked about */
        g_tip_asked = -1;
        SendMessageA(bar, TB_GETITEMRECT, 1, (LPARAM)&r);
        SendMessageA(bar, WM_MOUSEMOVE, 0,
                     MAKELPARAM((r.left + r.right) / 2, 10));
        SendMessageA(bar, WM_TIMER, 0x7e01, 0);
        CHECK(g_tip_asked == -1 && !IsWindowVisible(tip),
              "a button wearing its label gets no tip, as the machine's does "
              "not");
        DestroyWindow(bar);
    }

    /* A separator is as wide as it asks to be, and there are two ways to ask.
     * All three numbers below are real comctl32's, read off TB_GETITEMRECT by
     * tools/vm/ctlprobe.c rather than off a picture: told nothing a separator
     * is eight, told fourteen through iBitmap it is fourteen, and told twenty
     * afterwards through TB_SETBUTTONINFO it is twenty.
     *
     * The eight is worth a word, because this test used to say six and pass.
     * examples/explorer asks for six outright and ween32 ignored it, so the
     * shell's separators came out six from a default that was six for no
     * reason -- a wrong number and a dropped message cancelling, with the
     * captures standing on both. Honouring the ask is what makes the default
     * visible, and once it is visible it is eight. */
    {
        HWND sb = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                  WS_CHILD | WS_VISIBLE, 0, 0, 300, 30, w,
                                  (HMENU)(UINT_PTR)77, NULL, NULL);
        TBBUTTON sep[4];
        RECT r0, r1, r2, r3;
        SendMessageA(sb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        memset(sep, 0, sizeof(sep));
        sep[0].idCommand = 501;
        sep[0].fsState = TBSTATE_ENABLED;
        sep[0].fsStyle = TBSTYLE_BUTTON;
        sep[1].fsStyle = TBSTYLE_SEP; /* says nothing: the default */
        sep[2].fsStyle = TBSTYLE_SEP;
        sep[2].iBitmap = 14; /* and this one asks, in a separator's field */
        sep[3].idCommand = 502;
        sep[3].fsState = TBSTATE_ENABLED;
        sep[3].fsStyle = TBSTYLE_BUTTON;
        SendMessageA(sb, TB_ADDBUTTONSA, 4, (LPARAM)sep);
        SendMessageA(sb, TB_GETITEMRECT, 0, (LPARAM)&r0);
        SendMessageA(sb, TB_GETITEMRECT, 1, (LPARAM)&r1);
        SendMessageA(sb, TB_GETITEMRECT, 2, (LPARAM)&r2);
        SendMessageA(sb, TB_GETITEMRECT, 3, (LPARAM)&r3);
        CHECK(r1.right - r1.left == 8,
              "a separator that asks for no width is eight");
        CHECK(r2.right - r2.left == 14,
              "and one that asks for fourteen through iBitmap is fourteen");
        CHECK(r3.left == r2.right && r2.left == r1.right,
              "and what follows each one starts where it ended");

        /* The other way of asking, which ween32 stored and then never read:
         * TB_SETBUTTONINFO consults `fixed` in the button branch only, so a
         * separator took the message and ignored it. */
        {
            TBBUTTONINFOA bi;
            RECT s1, b3;
            memset(&bi, 0, sizeof(bi));
            bi.cbSize = sizeof(bi);
            bi.dwMask = TBIF_SIZE | TBIF_BYINDEX;
            bi.cx = 20;
            SendMessageA(sb, TB_SETBUTTONINFOA, 1, (LPARAM)&bi);
            SendMessageA(sb, TB_GETITEMRECT, 1, (LPARAM)&s1);
            SendMessageA(sb, TB_GETITEMRECT, 3, (LPARAM)&b3);
            CHECK(s1.right - s1.left == 20,
                  "a separator told twenty afterwards is twenty");
            CHECK(b3.left == r3.left + 12,
                  "and the twelve it grew by moves everything after it");
        }

        /* What win32 hands back for a separator it filled the default into.
         * comctl32 resolves it at add time, not at paint time: added with 0,
         * TB_GETBUTTON says 8. Ours said 0, which is the number the app
         * passed rather than the number the control is using. */
        {
            TBBUTTON g;
            memset(&g, 0, sizeof(g));
            SendMessageA(sb, TB_GETBUTTON, 1, (LPARAM)&g);
            CHECK(g.iBitmap == 8,
                  "a separator added with no width reports the eight it got");
            memset(&g, 0, sizeof(g));
            SendMessageA(sb, TB_GETBUTTON, 2, (LPARAM)&g);
            CHECK(g.iBitmap == 14,
                  "and one added with fourteen still reports fourteen");
        }
        DestroyWindow(sb);
    }

    /* Only a flat bar has a hot item. A classic one refuses to take one, and
     * TB_GETHOTITEM goes on saying -1 -- both measured on real comctl32 with
     * tools/vm/ctlprobe.c, which set the hot item on one of each and asked.
     * It is not an omission: every button on a classic bar wears its raised
     * edge all the time, so there is nowhere for hot to show. */
    {
        static const DWORD flat[2] = { TBSTYLE_FLAT, 0 };
        for (int k = 0; k < 2; k++) {
            HWND hb = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                      WS_CHILD | WS_VISIBLE | flat[k], 0, 0,
                                      200, 30, w, (HMENU)(UINT_PTR)(88 + k),
                                      NULL, NULL);
            TBBUTTON hbn[2];
            memset(hbn, 0, sizeof(hbn));
            SendMessageA(hb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
            for (int i = 0; i < 2; i++) {
                hbn[i].iBitmap = i;
                hbn[i].idCommand = 601 + i;
                hbn[i].fsState = TBSTATE_ENABLED;
                hbn[i].fsStyle = TBSTYLE_BUTTON;
            }
            SendMessageA(hb, TB_ADDBUTTONSA, 2, (LPARAM)hbn);
            SendMessageA(hb, TB_SETHOTITEM, 1, 0);
            if (k == 0)
                CHECK(SendMessageA(hb, TB_GETHOTITEM, 0, 0) == 1,
                      "a flat bar takes the hot item it is given");
            else
                CHECK(SendMessageA(hb, TB_GETHOTITEM, 0, 0) == -1,
                      "and a classic bar has not got one to give");
            DestroyWindow(hb);
        }
    }

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

        /* Carrying a band about, without a pointer. RB_MOVEBAND takes one
         * out of the order and puts it back somewhere else, which is what a
         * gripper carried up or down comes to on the machine.
         *
         * A band takes its style with it and the layout's rule is unchanged:
         * whichever band ends up first starts a row whether it asks to or
         * not. So moving the toolbar past the address bar does not simply
         * swap two rows -- the address bar becomes the first band and keeps
         * its break, the toolbar follows it without one, and the two end up
         * side by side on a single row. That is the machine's behaviour too:
         * a bar carried onto another's row goes beside it, and the row it
         * came from closes up. */
        {
            RECT tb2, ar3;
            int stacked = (int)SendMessageA(rebar, RB_GETBARHEIGHT, 0, 0);
            GetWindowRect(g_tb, &tb2);
            GetWindowRect(addr, &ar3);
            CHECK(tb2.top < ar3.top, "the toolbar starts above the address bar");
            CHECK(SendMessageA(rebar, RB_MOVEBAND, 0, 1),
                  "and can be carried past it");
            GetWindowRect(g_tb, &tb2);
            GetWindowRect(addr, &ar3);
            /* Sideways, not downwards -- and the rebar losing a whole row
             * is what says they are on one. The tops are not compared: this
             * toolbar is the window's child rather than the rebar's, so the
             * band moves it to the same place within the band and it lands
             * on screen a rebar's-worth higher than the address bar does. */
            CHECK((int)SendMessageA(rebar, RB_GETBANDCOUNT, 0, 0) == 2 &&
                      (int)SendMessageA(rebar, RB_GETBARHEIGHT, 0, 0) ==
                          stacked - (2 + 22),
                  "which closes the row they were stacked in: two bands, one "
                  "row, the height of one less");
            CHECK(tb2.left > ar3.left,
                  "and leaves it beside the address bar rather than under "
                  "it -- whichever band is first starts the row, and this "
                  "one asked for no break of its own");
            CHECK(SendMessageA(rebar, RB_MOVEBAND, 1, 0),
                  "and carried back again");
            GetWindowRect(g_tb, &tb2);
            GetWindowRect(addr, &ar3);
            CHECK(tb2.top < ar3.top &&
                      (int)SendMessageA(rebar, RB_GETBARHEIGHT, 0, 0) == stacked,
                  "leaving them stacked as they started");
            CHECK(!SendMessageA(rebar, RB_MOVEBAND, 0, 7) &&
                      !SendMessageA(rebar, RB_MOVEBAND, -1, 0),
                  "a place that is not there moves nothing");

            /* And the window hears about it the way win32 says: the bar
             * whose height changed sends RBN_HEIGHTCHANGE, and the
             * application lays out round it. Not a WM_SIZE invented by the
             * control, and not on a move that left the height alone. */
            g_heights = 0;
            SendMessageA(rebar, RB_MOVEBAND, 0, 1);
            CHECK(g_heights == 1, "losing a row is said once, as a height "
                                  "change");
            SendMessageA(rebar, RB_MOVEBAND, 1, 0);
            CHECK(g_heights == 2, "and getting it back is said again");
            SendMessageA(rebar, RB_MOVEBAND, 1, 1);
            SendMessageA(rebar, RB_MOVEBAND, 0, 9);
            CHECK(g_heights == 2,
                  "a move that moves nothing says nothing");
        }

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
            /* The band outlives its control. Laying out afterwards used to
             * move a window that had been freed -- the rebar held the only
             * pointer to it and nothing told it the window had gone. */
            CHECK((int)SendMessageA(rebar, RB_GETBANDCOUNT, 0, 0) == 3,
                  "a band whose control is destroyed is still a band");
            CHECK((int)SendMessageA(rebar, RB_GETBARHEIGHT, 0, 0) > 0,
                  "and the rebar lays out again without it");
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

    {
        /* A bar that was not asked to be flat: every button wears the push
         * button's own edge all the time — white along the top and left,
         * shadow and dark shadow down the other two — and the size the bar
         * was given is the size a button with only a picture takes. */
        HWND raised = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                      WS_CHILD | WS_VISIBLE, 0, 25, 120, 28, w,
                                      NULL, NULL, NULL);
        TBBUTTON one;
        RECT r, wr;
        const ween_surface *s;
        SendMessageA(raised, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageA(raised, TB_SETBUTTONSIZE, 0, MAKELPARAM(23, 22));
        memset(&one, 0, sizeof(one));
        one.iBitmap = -1;
        one.idCommand = ID_BACK;
        one.fsState = TBSTATE_ENABLED;
        one.fsStyle = TBSTYLE_CHECK;
        SendMessageA(raised, TB_ADDBUTTONSA, 1, (LPARAM)&one);
        SendMessageA(raised, TB_GETITEMRECT, 0, (LPARAM)&r);
        CHECK(r.right - r.left == 23,
              "the button size the bar was given is the width it takes");
        CHECK(r.bottom - r.top == 22, "and the height");
        GetWindowRect(raised, &wr);
        InvalidateRect(w, NULL, FALSE);
        UpdateWindow(w);
        s = ween_headless_surface();
        if (s) {
            long bx = wr.left + r.left, by = wr.top + r.top;
            CHECK(s->px[by * s->w + bx] == WEEN_WHITE,
                  "a raised button starts with the white highlight");
            CHECK(s->px[by * s->w + bx + 22] == WEEN_DKSHADOW,
                  "and ends with the dark shadow on the other side");
            CHECK(s->px[(by + 21) * s->w + bx] == WEEN_DKSHADOW,
                  "as it does along the bottom");
        }
        DestroyWindow(raised);
    }

    DestroyWindow(w);

    /* And each of them carries its own gripper. The handle and the name
     * are placed from the band's left edge, which is the same thing as
     * the rebar's only while every band starts a row of its own: with two
     * on a row the second drew no gripper at all -- its handle went where
     * the first's already was -- and printed its name over the first's.
     *
     * A window of its own, sized to the one row, because the rebar above
     * already stands taller than the window it is in and a third row
     * would be painted off the bottom of it. Both bands carry the same
     * text, so their content sits the same distance into each and the
     * second gripper is exactly one band along from the first -- nothing
     * here has to know the numbers. */
    {
        HWND pw = CreateWindowExA(0, "weentb", "pair",
                                  WS_POPUP | WS_VISIBLE, 0, 0, 400, 60,
                                  NULL, NULL, NULL, NULL);
        HWND pr = CreateWindowExA(0, REBARCLASSNAMEA, "",
                                  WS_CHILD | WS_VISIBLE | RBS_BANDBORDERS,
                                  0, 0, 400, 40, pw, NULL, NULL, NULL);
        HWND lc = CreateWindowA("BUTTON", "l", WS_CHILD | WS_VISIBLE, 0, 0,
                                40, 22, pr, NULL, NULL, NULL);
        HWND rc = CreateWindowA("BUTTON", "r", WS_CHILD | WS_VISIBLE, 0, 0,
                                40, 22, pr, NULL, NULL, NULL);
        REBARBANDINFOA pb;
        const ween_surface *ps;
        RECT lr, rr;
        int gy, gx = -1, pitch;

        memset(&pb, 0, sizeof(pb));
        pb.cbSize = sizeof(pb);
        pb.fMask = RBBIM_CHILD | RBBIM_TEXT | RBBIM_CHILDSIZE | RBBIM_STYLE;
        pb.cyMinChild = 22;
        pb.lpText = (char *)"Pair";
        pb.fStyle = 0;
        pb.hwndChild = lc;
        SendMessageA(pr, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&pb);
        pb.hwndChild = rc; /* no break: beside it, on the same row */
        SendMessageA(pr, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&pb);

        InvalidateRect(pw, NULL, TRUE);
        ween_flush_paint();
        ps = ween_headless_surface();
        GetWindowRect(lc, &lr);
        GetWindowRect(rc, &rr);
        pitch = (int)(rr.left - lr.left);
        CHECK(lr.top == rr.top && pitch > 0,
              "two labelled bands sharing a row, the second along from "
              "the first");
        /* The gripper's left edge is white down its whole height, so a row
         * well inside it holds one white pixel per handle. The scan starts
         * left of the control, not on it: a button's own highlight is white
         * too, and a scan that begins there finds that instead -- and then
         * compares one control's edge against the other's, which is true
         * whether or not the band ever drew a gripper. */
        gy = (int)lr.top + 5;
        for (long x = lr.left - 1; x > lr.left - 40 && x > 0; x--)
            if (ps && ps->px[gy * ps->w + x] == WEEN_WHITE) {
                gx = (int)x;
                break;
            }
        CHECK(gx > 0, "the first band's gripper is left of its control");
        if (ps && gx > 0 && gx + pitch < ps->w)
            CHECK(ps->px[gy * ps->w + gx + pitch] == WEEN_WHITE,
                  "and the second band has one of its own, a band along, "
                  "drawn from its own edge and not the rebar's");
        DestroyWindow(pw);
    }

    /* Carrying a band by its gripper. Watched on the machine and written down
     * in docs/testing.md: one gesture with two axes -- up and down moves the
     * band between rows, along its row it takes width off the band to its
     * left -- the arrangement following the pointer as it goes rather than an
     * outline dropped at the end. */
    {
        HWND dw = CreateWindowExA(0, "weentb", "drag", WS_POPUP | WS_VISIBLE,
                                  0, 0, 400, 100, NULL, NULL, NULL, NULL);
        HWND dr = CreateWindowExA(0, REBARCLASSNAMEA, "",
                                  WS_CHILD | WS_VISIBLE | RBS_BANDBORDERS,
                                  0, 0, 400, 80, dw, NULL, NULL, NULL);
        HWND c1 = CreateWindowA("BUTTON", "1", WS_CHILD | WS_VISIBLE, 0, 0, 40,
                                22, dr, NULL, NULL, NULL);
        HWND c2 = CreateWindowA("BUTTON", "2", WS_CHILD | WS_VISIBLE, 0, 0, 40,
                                22, dr, NULL, NULL, NULL);
        REBARBANDINFOA db;
        RBHITTESTINFO ht;
        RECT r1, r2;
        int stacked, joined, was_left;

        memset(&db, 0, sizeof(db));
        db.cbSize = sizeof(db);
        db.fMask = RBBIM_CHILD | RBBIM_TEXT | RBBIM_CHILDSIZE | RBBIM_STYLE;
        db.cyMinChild = 22;
        db.lpText = (char *)"One";
        db.fStyle = 0;
        db.hwndChild = c1;
        SendMessageA(dr, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&db);
        db.lpText = (char *)"Two";
        db.fStyle = RBBS_BREAK; /* a row of its own, under the first */
        db.hwndChild = c2;
        SendMessageA(dr, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&db);
        stacked = (int)SendMessageA(dr, RB_GETBARHEIGHT, 0, 0);

        /* What is under a point. The handle, the name it wears, the control
         * filling the rest, and nothing at all. */
        memset(&ht, 0, sizeof(ht));
        ht.pt.x = 5;
        ht.pt.y = 30;
        CHECK(SendMessageA(dr, RB_HITTEST, 0, (LPARAM)&ht) == 1 &&
                  ht.flags == RBHT_GRABBER && ht.iBand == 1,
              "a point on the second band's handle is that band's grabber");
        ht.pt.x = 12;
        ht.pt.y = 30;
        CHECK(SendMessageA(dr, RB_HITTEST, 0, (LPARAM)&ht) == 1 &&
                  ht.flags == RBHT_CAPTION,
              "one on its name is its caption");
        ht.pt.x = 200;
        ht.pt.y = 30;
        CHECK(SendMessageA(dr, RB_HITTEST, 0, (LPARAM)&ht) == 1 &&
                  ht.flags == RBHT_CLIENT,
              "and one past that is the band itself");
        ht.pt.x = 200;
        ht.pt.y = 600;
        CHECK(SendMessageA(dr, RB_HITTEST, 0, (LPARAM)&ht) == -1 &&
                  ht.flags == RBHT_NOWHERE,
              "below every band there is no band");

        /* Taken hold of by the handle. */
        g_begindrag = g_enddrag = g_layouts = g_heights = 0;
        g_dragband = -1;
        SendMessageA(dr, WM_LBUTTONDOWN, 0, MAKELPARAM(5, 30));
        CHECK(g_begindrag == 1 && g_dragband == 1,
              "a press on a handle begins a drag, and says which band");
        CHECK(GetCapture() == dr, "and the rebar takes the pointer");

        /* Carried up onto the first band's row: it goes beside it, the row it
         * left closes, and the bar loses a row's height. */
        SendMessageA(dr, WM_MOUSEMOVE, 0, MAKELPARAM(5, 10));
        joined = (int)SendMessageA(dr, RB_GETBARHEIGHT, 0, 0);
        GetWindowRect(c1, &r1);
        GetWindowRect(c2, &r2);
        CHECK(joined == stacked - (2 + 22),
              "carried onto the row above, it joins it and the bar is a row "
              "shorter");
        CHECK(r1.top == r2.top && r2.left > r1.left,
              "the two of them side by side, in the order they were in");
        CHECK(g_heights == 1, "which the window is told about once");

        /* And along the row: the boundary follows the pointer, the width
         * coming off the band to its left. */
        was_left = (int)r2.left;
        SendMessageA(dr, WM_MOUSEMOVE, 0, MAKELPARAM(300, 10));
        GetWindowRect(c2, &r2);
        CHECK(r2.left > was_left,
              "carried along the row it takes width off its neighbour");
        CHECK((int)SendMessageA(dr, RB_GETBARHEIGHT, 0, 0) == joined,
              "and the bar is no taller for it");

        /* It cannot be pushed past what its neighbour can spare. */
        SendMessageA(dr, WM_MOUSEMOVE, 0, MAKELPARAM(-500, 10));
        GetWindowRect(c1, &r1);
        GetWindowRect(c2, &r2);
        CHECK(r2.left > r1.left,
              "pushed back past the end of the row it stops, rather than "
              "running through the band beside it");

        SendMessageA(dr, WM_LBUTTONUP, 0, MAKELPARAM(300, 10));
        CHECK(g_enddrag == 1 && GetCapture() != dr,
              "letting go ends the drag and gives the pointer back");
        CHECK(g_layouts > 0, "and the arrangement it left was said to have "
                             "changed");

        /* A press that is not on a handle is not a drag. */
        g_begindrag = 0;
        SendMessageA(dr, WM_LBUTTONDOWN, 0, MAKELPARAM(200, 10));
        CHECK(g_begindrag == 0 && GetCapture() != dr,
              "a press on the band itself carries nothing");
        DestroyWindow(dw);
    }

    /* The chevron. A band told to use one wears it when what is left for its
     * child is less than the child said it wanted -- cxIdeal is how an
     * application says that -- and pressing it asks the application what to
     * put up rather than putting up something the library invented. */
    {
        HWND cw = CreateWindowExA(0, "weentb", "chev", WS_POPUP | WS_VISIBLE,
                                  0, 0, 300, 60, NULL, NULL, NULL, NULL);
        HWND cr = CreateWindowExA(0, REBARCLASSNAMEA, "",
                                  WS_CHILD | WS_VISIBLE | RBS_BANDBORDERS,
                                  0, 0, 300, 40, cw, NULL, NULL, NULL);
        HWND cc = CreateWindowA("BUTTON", "c", WS_CHILD | WS_VISIBLE, 0, 0, 40,
                                22, cr, NULL, NULL, NULL);
        REBARBANDINFOA cb;
        RBHITTESTINFO ht;

        memset(&cb, 0, sizeof(cb));
        cb.cbSize = sizeof(cb);
        cb.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE | RBBIM_ID |
                   RBBIM_LPARAM;
        cb.cyMinChild = 22;
        cb.fStyle = RBBS_USECHEVRON;
        cb.hwndChild = cc;
        cb.wID = 77;
        cb.lParam = 1234;
        SendMessageA(cr, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&cb);

        /* What a program set is what it gets back -- RB_SETBANDINFOA had been
         * declared and never answered, so a band could only ever be described
         * as it went in. */
        memset(&cb, 0, sizeof(cb));
        cb.cbSize = sizeof(cb);
        cb.fMask = RBBIM_ID | RBBIM_LPARAM | RBBIM_IDEALSIZE;
        CHECK(SendMessageA(cr, RB_GETBANDINFOA, 0, (LPARAM)&cb) &&
                  cb.wID == 77 && cb.lParam == 1234 && cb.cxIdeal == 0,
              "a band hands back the id and the value it was given, and no "
              "ideal width until it is told one");

        /* With no ideal width there is nothing to be too narrow for. */
        memset(&ht, 0, sizeof(ht));
        ht.pt.x = 295;
        ht.pt.y = 10;
        SendMessageA(cr, RB_HITTEST, 0, (LPARAM)&ht);
        CHECK(ht.flags != RBHT_CHEVRON,
              "a band with no ideal width wears no chevron");

        /* Told the child wants far more room than the band has, it does. */
        memset(&cb, 0, sizeof(cb));
        cb.cbSize = sizeof(cb);
        cb.fMask = RBBIM_IDEALSIZE;
        cb.cxIdeal = 1000;
        CHECK(SendMessageA(cr, RB_SETBANDINFOA, 0, (LPARAM)&cb),
              "a band can be told the width its child would like");
        memset(&ht, 0, sizeof(ht));
        ht.pt.x = 295;
        ht.pt.y = 10;
        CHECK(SendMessageA(cr, RB_HITTEST, 0, (LPARAM)&ht) == 0 &&
                  ht.flags == RBHT_CHEVRON,
              "and one too narrow for it wears a chevron at its right edge");
        memset(&ht, 0, sizeof(ht));
        ht.pt.x = 150;
        ht.pt.y = 10;
        SendMessageA(cr, RB_HITTEST, 0, (LPARAM)&ht);
        CHECK(ht.flags == RBHT_CLIENT,
              "which is at the edge and not over the whole band");

        /* And pressing it asks rather than answers. */
        g_chevrons = 0;
        g_chevband = -1;
        SendMessageA(cr, WM_LBUTTONDOWN, 0, MAKELPARAM(295, 10));
        CHECK(g_chevrons == 1 && g_chevband == 0,
              "pressing it asks the window what to put up, naming the band");
        /* The rectangle, to the pixel, because it is where the application
         * hangs its menu and it was measured off the machine: eight by five,
         * three in from the band's right edge, and its top four below the
         * band's -- not centred in the band, which is what this was until the
         * machine was asked. */
        CHECK(g_chevrect.right - g_chevrect.left == 8 &&
                  g_chevrect.bottom - g_chevrect.top == 5,
              "and hands it a rectangle eight by five, as the machine draws "
              "it");
        CHECK(g_chevrect.right == 300 - 3,
              "three pixels in from the band's right edge");
        CHECK(g_chevrect.top == 4,
              "and four below the band's top, rather than centred in it");
        CHECK(GetCapture() != cr,
              "a chevron is not a handle: it takes no capture and drags "
              "nothing");

        /* And the band keeps the room for it. Without that the band draws its
         * arrows and the child paints straight over them -- which is what it
         * did: the chevron was there and invisible, and it took a photograph
         * of a narrowed explorer to see it. The machine reserves the same
         * room; its squeezed toolbar stops before the arrows. */
        {
            RECT cr2;
            POINT edge;
            GetWindowRect(cc, &cr2);
            edge.x = cr2.right;
            edge.y = cr2.top;
            ScreenToClient(cr, &edge);
            CHECK(edge.x <= g_chevrect.left,
                  "the band's control ends before the chevron rather than "
                  "running under it");
        }
        DestroyWindow(cw);
    }

    /* A band told RBBS_NOGRIPPER has no handle, so it leaves no room for one.
     * Real comctl32, asked with tools/vm/ctlprobe.c: a toolbar in a
     * no-gripper band comes back at **0,0** in the band. ween32 drew no
     * gripper for such a band and went on reserving its ten pixels, so every
     * child of one sat ten right of where win32 puts it. */
    {
        static const DWORD styles[2] = { 0, RBBS_NOGRIPPER };
        static const int want_x[2] = { 10, 0 };
        for (int k = 0; k < 2; k++) {
            /* A rebar each: two bands in one would sit side by side and the
             * second one's x would be the first one's width, which is not
             * what is being asked about. */
            HWND rb = CreateWindowExA(0, REBARCLASSNAMEA, "",
                                      WS_CHILD | WS_VISIBLE | RBS_VARHEIGHT |
                                          CCS_NODIVIDER | CCS_NORESIZE |
                                          CCS_NOPARENTALIGN,
                                      0, 0, 400, 26, w,
                                      (HMENU)(UINT_PTR)(91 + k * 10), NULL,
                                      NULL);
            HWND child = CreateWindowExA(0, TOOLBARCLASSNAMEA, "",
                                         WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT |
                                             CCS_NORESIZE | CCS_NODIVIDER |
                                             CCS_NOPARENTALIGN,
                                         0, 0, 100, 22, rb,
                                         (HMENU)(UINT_PTR)(92 + k), NULL, NULL);
            REBARBANDINFOA bi;
            RECT cw, cb;
            memset(&bi, 0, sizeof(bi));
            bi.cbSize = sizeof(bi);
            bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE;
            bi.fStyle = styles[k];
            bi.hwndChild = child;
            bi.cxMinChild = 100;
            bi.cyMinChild = 22;
            SendMessageA(rb, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&bi);
            GetWindowRect(child, &cw);
            GetWindowRect(rb, &cb);
            CHECK(cw.left - cb.left == want_x[k],
                  k == 0 ? "a band leaves ten pixels for its gripper"
                         : "and none at all when it is told it has not got one");
            DestroyWindow(rb);
        }
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("toolbar_test: all passed\n");
    return 0;
}
