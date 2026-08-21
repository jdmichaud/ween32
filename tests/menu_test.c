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
static int g_command;

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND) {
        g_command = LOWORD(wp);
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
        int label = ween_strike_text_extent(f, "File", 4);
        CHECK(it->x == 0 && it->w == label + 12,
              "a bar item is its label plus twelve");
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
        CHECK(ween_menu_item(g_file, 2)->h == 5,
              "a separator is five pixels tall");

        mouse(WEEN_EV_MOUSE_DOWN, frame + 4, bar_y + 5); /* on "File" */
        mouse(WEEN_EV_MOUSE_MOVE, 10, open->y + open->h / 2);
        mouse(WEEN_EV_MOUSE_UP, 10, open->y + open->h / 2);

        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0))
            DispatchMessageA(&msg);
        CHECK(g_command == ID_OPEN,
              "clicking the bar opened the menu and chose from it");
        CHECK(w->menu_hot == -1, "and the bar item is no longer held open");
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

    DestroyWindow(w);
    DestroyMenu(g_bar);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("menu_test: all passed\n");
    return 0;
}
