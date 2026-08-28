/* Menus: the list, the bar's geometry, and tracking a drop-down to a command.
 * Tracking runs its own modal loop over injected events, so the whole path —
 * popup window, highlight, selection, WM_COMMAND — is exercised headless. */

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

enum { ID_NEW = 300, ID_OPEN, ID_EXIT, ID_SUB_A, ID_EDIT_A };

static HMENU g_bar, g_file, g_sub;
static int g_command, g_rbuttons;

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND) {
        g_command = LOWORD(wp);
        return 0;
    }
    if (msg == WM_RBUTTONDOWN) {
        g_rbuttons++;
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void mouse(ween_ev_kind kind, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.x = x;
    ev.y = y;
    ev.button = 1;
    ween_headless_inject(ev);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenmenu";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    g_bar = CreateMenu();
    g_file = CreatePopupMenu();
    g_sub = CreatePopupMenu();
    AppendMenuA(g_sub, MF_STRING, ID_SUB_A, "Deeper");
    AppendMenuA(g_file, MF_STRING, ID_NEW, "&New\tCtrl+N");
    AppendMenuA(g_file, MF_STRING, ID_OPEN, "&Open...");
    AppendMenuA(g_file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_file, MF_POPUP, (UINT_PTR)g_sub, "&More");
    AppendMenuA(g_file, MF_STRING | MF_GRAYED, ID_EXIT, "E&xit");
    AppendMenuA(g_bar, MF_POPUP, (UINT_PTR)g_file, "&File");
    CHECK(ween_menu_count(g_bar) == 1 && ween_menu_count(g_file) == 5,
          "items were appended, separators and all");
    CHECK(GetSubMenu(g_bar, 0) == g_file, "GetSubMenu reaches the drop-down");

    HWND w = CreateWindowExA(0, "weenmenu", "menus",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0,
                             0, 300, 200, NULL, NULL, NULL, NULL);
    CHECK(w != NULL, "a window to hang the menu on");

    RECT before, after;
    GetClientRect(w, &before);
    SetMenu(w, g_bar);
    GetClientRect(w, &after);
    CHECK(GetMenu(w) == g_bar, "the window kept the menu");
    CHECK(before.bottom - after.bottom == GetSystemMetrics(SM_CYMENU),
          "the client area lost exactly the menu bar's height");
    CHECK(GetSystemMetrics(SM_CYMENU) == 19, "which is 19 at 96 dpi");

    /* The bar's items are their label plus Wine's MENU_BAR_ITEMS_SPACE. */
    {
        const ween_strike *f = ween_gui_font();
        ween_menu_layout_bar(g_bar, f, 300);
        ween_menuitem *it = ween_menu_item(g_bar, 0);
        /* Twelve, on the width the glyphs occupy — Wine's number, and what a
         * Windows 2000 menu bar measures: Paint's six titles sit thirteen
         * pixels of ink apart and the first starts six in. It was sixteen
         * here for a while, measured off the explorer's bar, which is a
         * toolbar rather than a menu bar. */
        int label = ween_strike_text_width(f, "File", 4);
        CHECK(it->x == 0 && it->w == label + 12,
              "a bar item is its drawn label plus twelve");
        CHECK(ween_menu_hit(g_bar, 3, 5) == 0, "and hit-tests to itself");
        CHECK(ween_menu_hit(g_bar, it->w + 40, 5) == -1,
              "past the last item, nothing is hit");
    }

    /* Checked and grey states round-trip. */
    CheckMenuItem(g_file, ID_NEW, MF_CHECKED);
    CHECK((ween_menu_item(g_file, 0)->flags & MF_CHECKED) != 0,
          "CheckMenuItem ticks an item");
    CheckMenuItem(g_file, ID_NEW, MF_UNCHECKED);
    CHECK((ween_menu_item(g_file, 0)->flags & MF_CHECKED) == 0,
          "and unticks it");
    CHECK((ween_menu_item(g_file, 4)->flags & MF_GRAYED) != 0,
          "MF_GRAYED survived being appended");

    /* A press on the bar opens that item's drop-down and tracks it: the
     * move and release that follow are consumed by the tracking loop, and the
     * owner is told through WM_COMMAND. The item rectangles come from the
     * layout, so the test cannot drift from the drawing. */
    {
        int frame = WEEN_NC_FRAME, bar_y = WEEN_NC_FRAME + WEEN_NC_CAPTION;
        int pw, ph;
        ween_menu_popup_size(g_file, ween_gui_font(), &pw, &ph);
        ween_menuitem *open = ween_menu_item(g_file, 1);
        CHECK(pw > 0 && ph > 0, "the drop-down has a size");
        /* Every drop-down reserves the submenu-arrow column, so the one that
         * has a cascade in it is not the only one with a right margin. */
        {
            HMENU plain = CreatePopupMenu();
            int aw, ah;
            AppendMenuA(plain, MF_STRING, 900, "&Open...");
            ween_menu_popup_size(plain, ween_gui_font(), &aw, &ah);
            int label = ween_strike_text_width(ween_gui_font(), "Open...", 7);
            CHECK(aw == 20 + label + 9 + 12 + 3,
                  "a menu with no cascade still leaves room for the column");
            DestroyMenu(plain);
        }
        CHECK(ween_menu_item(g_file, 2)->h == 9,
              "a separator's box is nine pixels tall, as the capture has it");
        CHECK(ween_menu_item(g_file, 0)->h == 17,
              "and an item seventeen — the font's height plus four");
        CHECK(ween_menu_item(g_file, 0)->y == 3,
              "the first item starts below the border and its padding");

    /* A press of the right button away from the menu is not swallowed. The
     * menu goes away and the press is handed back to the window under it,
     * which is how a right click on a second file closes the first file's
     * menu, picks the second and opens its own. */
    {
        ween_event ev;
        MSG msg;
        memset(&ev, 0, sizeof(ev));
        ev.kind = WEEN_EV_MOUSE_DOWN;
        ev.button = 3;
        ev.win = w->backend_win;
        ev.x = 300; /* well outside the drop-down, which is at the corner */
        ev.y = 200;
        ween_headless_inject(ev);
        TrackPopupMenu(g_file, TPM_LEFTALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
                       0, 0, 0, w, NULL);
        CHECK(g_rbuttons == 0, "the press that closed the menu waits its turn");
        /* one turn of the loop is enough: the replay is handed over before it
         * goes anywhere near the backend for more */
        if (GetMessageA(&msg, NULL, 0, 0))
            DispatchMessageA(&msg);
        CHECK(g_rbuttons == 1, "and then reaches the window it was over");
    }

        /* The window manager has put the window somewhere other than it
         * asked to be, which is what a tiling one always does. The drop-down
         * has to open beside where the window is, not where it asked to be:
         * with these confused the menu appears somewhere off in the desktop,
         * detached from the window it belongs to. */
        ween_headless_set_window_origin(300, 200);

        mouse(WEEN_EV_MOUSE_DOWN, frame + 4, bar_y + 5); /* on "File" */
        mouse(WEEN_EV_MOUSE_MOVE, 10, open->y + open->h / 2);
        mouse(WEEN_EV_MOUSE_UP, 10, open->y + open->h / 2);

        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0))
            DispatchMessageA(&msg);
        CHECK(g_command == ID_OPEN,
              "clicking the bar opened the menu and chose from it");
        CHECK(w->menu_hot == -1, "and the bar item is no longer held open");
        {
            int px = 0, py = 0;
            ween_headless_last_unmanaged_origin(&px, &py);
            CHECK(px == 300 + frame + ween_menu_item(g_bar, 0)->x,
                  "the drop-down opened under its bar item on the desktop");
            CHECK(py == 200 + bar_y + WEEN_NC_MENU,
                  "just below the bar, wherever the window itself was put");
        }
        ween_headless_set_window_origin(0, 0);
    }

    /* TrackPopupMenu on its own, with TPM_RETURNCMD: the command comes back
     * rather than being posted, so no message loop is needed. */
    {
        ween_menuitem *new_item = ween_menu_item(g_file, 0);
        mouse(WEEN_EV_MOUSE_MOVE, 10, new_item->y + new_item->h / 2);
        mouse(WEEN_EV_MOUSE_UP, 10, new_item->y + new_item->h / 2);
        BOOL cmd = TrackPopupMenu(g_file, TPM_LEFTALIGN | TPM_RETURNCMD, 0, 0,
                                  0, w, NULL);
        CHECK(cmd == ID_NEW, "TPM_RETURNCMD hands the command straight back");
    }

    /* A grey item cannot be chosen, and Escape puts the menu away. */
    {
        ween_menuitem *exit_item = ween_menu_item(g_file, 4);
        ween_event esc;
        mouse(WEEN_EV_MOUSE_MOVE, 10, exit_item->y + exit_item->h / 2);
        mouse(WEEN_EV_MOUSE_UP, 10, exit_item->y + exit_item->h / 2);
        memset(&esc, 0, sizeof(esc));
        esc.kind = WEEN_EV_KEY;
        esc.vk = VK_ESCAPE;
        ween_headless_inject(esc);
        BOOL cmd = TrackPopupMenu(g_file, TPM_LEFTALIGN | TPM_RETURNCMD, 0, 0,
                                  0, w, NULL);
        CHECK(cmd == 0, "releasing over a grey item chooses nothing");
    }

    /* A cascade keeps its parent open: hovering "More" opens the submenu
     * beside it, and the item stays highlighted so you can walk back out. */
    {
        int sw, sh;
        ween_menu_popup_size(g_sub, ween_gui_font(), &sw, &sh); /* lay it out */
        ween_menuitem *more = ween_menu_item(g_file, 3);
        ween_menuitem *deeper = ween_menu_item(g_sub, 0);
        mouse(WEEN_EV_MOUSE_MOVE, 10, more->y + more->h / 2);
        /* the cascade is a window of its own, so the move into it names no
         * window and lands on the deepest level — which is the submenu */
        mouse(WEEN_EV_MOUSE_MOVE, 10, deeper->y + deeper->h / 2);
        mouse(WEEN_EV_MOUSE_UP, 10, deeper->y + deeper->h / 2);
        BOOL cmd = TrackPopupMenu(g_file, TPM_LEFTALIGN | TPM_RETURNCMD, 0, 0,
                                  0, w, NULL);
        CHECK(cmd == ID_SUB_A, "an item was chosen from a cascaded submenu");
    }

    /* The keyboard, inside a drop-down: Down walks it, Right opens a cascade
     * and Left comes back out of one, Escape closes a level at a time. */
    {
        ween_event k;
        memset(&k, 0, sizeof(k));
        k.kind = WEEN_EV_KEY;
        k.vk = VK_DOWN;
        ween_headless_inject(k);  /* onto "New" */
        k.vk = VK_DOWN;
        ween_headless_inject(k);  /* onto "Open..." */
        k.vk = VK_DOWN;
        ween_headless_inject(k);  /* the separator is skipped: "More" */
        k.vk = VK_RIGHT;
        ween_headless_inject(k);  /* into the cascade, on its first item */
        k.vk = VK_RETURN;
        ween_headless_inject(k);
        BOOL cmd = TrackPopupMenu(g_file, TPM_LEFTALIGN | TPM_RETURNCMD, 0, 0,
                                  0, w, NULL);
        CHECK(cmd == ID_SUB_A, "the arrows reached the submenu and chose from it");
    }

    {
        ween_event k;
        memset(&k, 0, sizeof(k));
        k.kind = WEEN_EV_KEY;
        k.vk = VK_DOWN;
        ween_headless_inject(k);
        k.vk = 'O'; /* the mnemonic in "&Open..." */
        k.ch = 'o';
        ween_headless_inject(k);
        BOOL cmd = TrackPopupMenu(g_file, TPM_LEFTALIGN | TPM_RETURNCMD, 0, 0,
                                  0, w, NULL);
        CHECK(cmd == ID_OPEN, "a letter picks the item its label marks");
    }

    /* Escape backs out of a cascade rather than the whole menu. */
    {
        ween_event k;
        memset(&k, 0, sizeof(k));
        k.kind = WEEN_EV_KEY;
        k.vk = VK_DOWN;
        ween_headless_inject(k);
        k.vk = VK_DOWN;
        ween_headless_inject(k);
        k.vk = VK_DOWN;
        ween_headless_inject(k);  /* "More" */
        k.vk = VK_RIGHT;
        ween_headless_inject(k);  /* into the cascade */
        k.vk = VK_ESCAPE;
        ween_headless_inject(k);  /* out of it, still in "File" */
        k.vk = VK_RETURN;
        ween_headless_inject(k);  /* so this opens the cascade again */
        k.vk = VK_RETURN;
        ween_headless_inject(k);  /* and this chooses from it */
        BOOL cmd = TrackPopupMenu(g_file, TPM_LEFTALIGN | TPM_RETURNCMD, 0, 0,
                                  0, w, NULL);
        CHECK(cmd == ID_SUB_A, "Escape leaves the cascade, not the menu");
    }

    /* Walking the bar from the keyboard: Right off the end of one drop-down's
     * items moves to the next drop-down, which needs a second bar item. */
    {
        HMENU edit = CreatePopupMenu();
        AppendMenuA(edit, MF_STRING, ID_EDIT_A, "&Wrap");
        AppendMenuA(g_bar, MF_POPUP, (UINT_PTR)edit, "&Edit");

        ween_event k;
        memset(&k, 0, sizeof(k));
        k.kind = WEEN_EV_KEY;
        k.vk = VK_RIGHT; /* "New" has no cascade, so this goes to the bar */
        ween_headless_inject(k);
        k.vk = VK_DOWN;
        ween_headless_inject(k);
        k.vk = VK_RETURN;
        ween_headless_inject(k);
        UINT cmd = ween_menu_track_bar(w, 0, 1);
        CHECK(cmd == ID_EDIT_A, "Right walked from one drop-down to the next");
        CHECK(w->menu_hot == -1, "and the bar is not left held open");
    }

    /* A press on the window but off the bar puts the menu away — and does
     * not get swallowed by the tracking loop, which is what left the close
     * box unreachable for as long as a menu had ever been opened. Events name
     * the window they are on, so the loop can tell the two apart. */
    {
        ween_event down;
        memset(&down, 0, sizeof(down));
        down.kind = WEEN_EV_MOUSE_DOWN;
        down.button = 1;
        down.win = w->backend_win;      /* on the owner... */
        down.x = 140;
        down.y = 100;                   /* ...well below the menu bar */
        ween_headless_inject(down);
        /* if the press is swallowed instead of closing the menu, the loop
         * runs on to the end of the script and this never returns 0 for the
         * right reason — so follow it with a second one that would be chosen
         * if the menu were somehow still up */
        UINT cmd = ween_menu_track_bar(w, 0, 0);
        CHECK(cmd == 0, "a press off the menu bar closed the menu");
        CHECK(w->menu_hot == -1, "and let the bar item go");
    }

    /* Alt is what starts that: IsDialogMessageA hands it to the menu bar
     * rather than to a control's mnemonic. */
    {
        ween_event k;
        memset(&k, 0, sizeof(k));
        k.kind = WEEN_EV_KEY;
        k.vk = VK_ESCAPE; /* so the session it opens ends at once */
        ween_headless_inject(k);

        MSG msg;
        memset(&msg, 0, sizeof(msg));
        msg.hwnd = w;
        msg.message = WM_KEYDOWN;
        msg.wParam = VK_MENU;
        CHECK(IsDialogMessageA(w, &msg), "Alt is taken by the menu bar");
    }

    /* And a program that never calls IsDialogMessage -- Notepad does not,
     * having a menu bar and nothing to tab between -- still gets its menus,
     * because on Windows this is the window procedure's work. The same keys
     * go to DefWindowProc, which is where they arrive when a message loop
     * dispatches them.
     *
     * DefWindowProc answers WM_KEYDOWN with nought whatever happens, so what
     * is checked is the effect: Alt and F opens the File menu -- the session
     * runs inside that call and the Escape waiting for it ends the session
     * again -- and the underlines a menu reached by key brings out are what
     * says the keys got there. */
    {
        ween_event k;
        LPARAM alt_f = (LPARAM)((1L << 29) | ((LPARAM)'F' << 16));
        memset(&k, 0, sizeof(k));
        k.kind = WEEN_EV_KEY;
        k.vk = VK_ESCAPE;
        ween_headless_inject(k);
        ween_menu_cues = 0;
        DefWindowProcA(w, WM_KEYDOWN, 'F', alt_f);
        CHECK(ween_menu_cues == 1,
              "Alt and a letter reach the menus through DefWindowProc");
        CHECK(w->menu_hot == -1, "and the bar is let go again afterwards");
    }

    {
        /* Alt on its own arms the bar: the underlines come out and the next
         * key belongs to the menus. Nothing is opened, so nothing has to be
         * escaped from. */
        CHECK(!ween_menu_armed(), "the bar is not armed to begin with");
        DefWindowProcA(w, WM_KEYDOWN, VK_MENU, 0);
        CHECK(ween_menu_armed(), "Alt alone arms it, through DefWindowProc");
        {
            ween_event k;
            memset(&k, 0, sizeof(k));
            k.kind = WEEN_EV_KEY;
            k.vk = VK_ESCAPE;
            ween_headless_inject(k);
            DefWindowProcA(w, WM_KEYDOWN, VK_ESCAPE, 0);
        }
    }

    /* The window is wearing the bar, so destroying the window destroys it:
     * that is win32's rule, and destroying it here as well would be a double
     * free there as much as here. */
    DestroyWindow(w);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("menu_test: all passed\n");
    return 0;
}
