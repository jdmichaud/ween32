/* Keyboard conventions: Tab and Shift+Tab across controls, Space on a button,
 * the arrows inside a group of option buttons, and Alt+letter for a label's
 * mnemonic. The app offers each key to IsDialogMessageA first, which is how a
 * win32 app gets all of this. */

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

enum { ID_OK = 100, ID_SAVE, ID_DEAD, ID_CHECK, ID_R1, ID_R2, ID_R3 };

static HWND g_ok, g_save, g_dead, g_check, g_r1, g_r2, g_r3;
static int g_save_clicked;
static int g_accel_cmd, g_accel_from;

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_ok = CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             10, 10, 60, 22, hwnd, (HMENU)(UINT_PTR)ID_OK, NULL,
                             NULL);
        g_save = CreateWindowA("BUTTON", "&Save",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP, 80, 10, 60,
                               22, hwnd, (HMENU)(UINT_PTR)ID_SAVE, NULL, NULL);
        g_dead = CreateWindowA("BUTTON", "Nope",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED,
                               150, 10, 60, 22, hwnd, (HMENU)(UINT_PTR)ID_DEAD,
                               NULL, NULL);
        g_check = CreateWindowA("BUTTON", "Check me",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    BS_AUTOCHECKBOX,
                                10, 40, 100, 18, hwnd,
                                (HMENU)(UINT_PTR)ID_CHECK, NULL, NULL);
        g_r1 = CreateWindowA("BUTTON", "One",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP |
                                 BS_AUTORADIOBUTTON,
                             10, 62, 80, 18, hwnd, (HMENU)(UINT_PTR)ID_R1, NULL,
                             NULL);
        g_r2 = CreateWindowA("BUTTON", "Two",
                             WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 10, 80,
                             80, 18, hwnd, (HMENU)(UINT_PTR)ID_R2, NULL, NULL);
        g_r3 = CreateWindowA("BUTTON", "Three",
                             WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 10, 98,
                             80, 18, hwnd, (HMENU)(UINT_PTR)ID_R3, NULL, NULL);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_SAVE && HIWORD(wp) == BN_CLICKED)
            g_save_clicked++;
        if (HIWORD(wp) == 1) { /* an accelerator says so in the high word */
            g_accel_cmd = LOWORD(wp);
            g_accel_from = HIWORD(wp);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static HWND g_host;

/* One key press, offered to IsDialogMessageA and then dispatched if it did not
 * want it — exactly what an app's loop does with it. The message is built
 * here rather than injected because a headless script runs the loop once, and
 * these assertions want to look between one key and the next. lParam carries
 * Shift in bit 0 and Alt in bit 29, where the pump puts them. */
static void key(unsigned vk, unsigned ch, int shift, int alt)
{
    MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.hwnd = ween_focus_get() ? ween_focus_get() : g_host;
    msg.message = WM_KEYDOWN;
    msg.wParam = vk;
    msg.lParam = (LPARAM)(ch << 16) | (shift ? 1 : 0) | (alt ? (1L << 29) : 0);
    if (IsDialogMessageA(g_host, &msg))
        return;
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenkeys";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    g_host = CreateWindowExA(0, "weenkeys", "keys",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0,
                             0, 240, 140, NULL, NULL, NULL, NULL);
    CHECK(g_host != NULL, "a window with controls to walk");

    /* Tab moves forward across the tab stops, and skips the disabled one. */
    key(VK_TAB, 0, 0, 0);
    CHECK(ween_focus_get() == g_ok, "Tab reaches the first control");
    key(VK_TAB, 0, 0, 0);
    CHECK(ween_focus_get() == g_save, "Tab moves to the next");
    key(VK_TAB, 0, 0, 0);
    CHECK(ween_focus_get() == g_check, "and steps over the disabled one");
    key(VK_TAB, 0, 1, 0);
    CHECK(ween_focus_get() == g_save, "Shift+Tab goes back");

    /* Space presses the focused button. */
    key(VK_TAB, 0, 0, 0); /* onto the check box */
    key(VK_SPACE, ' ', 0, 0);
    CHECK(SendMessageA(g_check, BM_GETCHECK, 0, 0) == BST_CHECKED,
          "Space checked the focused check box");

    /* The arrows move within a group of option buttons, and select as they go
     * — which is what Windows does. */
    SetFocus(g_r1);
    SendMessageA(g_r1, BM_CLICK, 0, 0);
    key(VK_DOWN, 0, 0, 0);
    CHECK(ween_focus_get() == g_r2, "Down moved to the next option button");
    CHECK(SendMessageA(g_r2, BM_GETCHECK, 0, 0) == BST_CHECKED,
          "and selected it");
    CHECK(SendMessageA(g_r1, BM_GETCHECK, 0, 0) == BST_UNCHECKED,
          "leaving the one before it clear");
    key(VK_UP, 0, 0, 0);
    CHECK(ween_focus_get() == g_r1, "Up came back");
    key(VK_UP, 0, 0, 0);
    CHECK(ween_focus_get() == g_r3, "and wraps round the group");

    /* Alt+letter: the mnemonic marked with '&'. */
    g_save_clicked = 0;
    key('S', 's', 0, 1);
    CHECK(g_save_clicked == 1, "Alt+S clicked the button labelled \"&Save\"");
    CHECK(ween_focus_get() == g_save, "and gave it the focus");

    /* The same key, this time all the way from a backend event, so the bit
     * the pump packs Alt into is covered too. */
    {
        SetFocus(NULL);
        g_save_clicked = 0;
        ween_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.kind = WEEN_EV_KEY;
        ev.vk = 'S';
        ev.ch = 's';
        ev.alt = 1;
        ween_headless_inject(ev);
        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0)) {
            if (IsDialogMessageA(g_host, &msg))
                continue;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        CHECK(g_save_clicked == 1, "and the same key arriving as an event");
    }

    /* An accelerator table: a key combination that sends a command straight
     * to the window, with 1 in the high word to say where it came from. */
    {
        static ACCEL accels[] = {
            { FVIRTKEY | FCONTROL, 'S', ID_SAVE },
            { FVIRTKEY, VK_F1, ID_OK },
        };
        HACCEL table = CreateAcceleratorTableA(accels, 2);
        CHECK(table != NULL, "an accelerator table");

        MSG msg;
        memset(&msg, 0, sizeof(msg));
        msg.hwnd = g_host;
        msg.message = WM_KEYDOWN;
        msg.wParam = 'S';
        msg.lParam = (LPARAM)(1L << 28); /* Ctrl */
        g_save_clicked = 0;
        g_accel_cmd = 0;
        CHECK(TranslateAcceleratorA(g_host, table, &msg),
              "Ctrl+S was taken by the table");
        CHECK(g_accel_cmd == ID_SAVE, "and sent that entry's command");
        CHECK(g_accel_from == 1, "marked as coming from an accelerator");

        msg.lParam = 0; /* the same key without Ctrl is not the accelerator */
        g_accel_cmd = 0;
        CHECK(!TranslateAcceleratorA(g_host, table, &msg),
              "without Ctrl it is not a match");
        CHECK(g_accel_cmd == 0, "and nothing was sent");

        msg.wParam = VK_F1;
        CHECK(TranslateAcceleratorA(g_host, table, &msg),
              "a plain function key matches too");
        CHECK(g_accel_cmd == ID_OK, "with its own command");

        CHECK(DestroyAcceleratorTable(table), "the table was destroyed");
    }

    /* WM_NEXTDLGCTL moves the focus the way a dialog does it. */
    {
        SetFocus(g_ok);
        SendMessageA(g_host, WM_NEXTDLGCTL, 0, 0);
        CHECK(ween_focus_get() == g_save, "WM_NEXTDLGCTL moved on");
        SendMessageA(g_host, WM_NEXTDLGCTL, 1, 0);
        CHECK(ween_focus_get() == g_ok, "and back with a non-zero wParam");
        SendMessageA(g_host, WM_NEXTDLGCTL, (WPARAM)g_check, 1);
        CHECK(ween_focus_get() == g_check,
              "and straight to a named control when lParam says so");
    }

    /* An & in a label is a marker, not a character to draw. */
    {
        const ween_strike *f = ween_gui_font();
        int with = ween_strike_text_extent(f, "&Save", 5);
        int without = ween_strike_text_extent(f, "Save", 4);
        HDC dc;
        PAINTSTRUCT ps;
        dc = BeginPaint(g_save, &ps);
        RECT r = ps.rcPaint;
        int drawn = DrawTextA(dc, "&Save", -1, &r, DT_LEFT | DT_SINGLELINE);
        EndPaint(g_save, &ps);
        CHECK(with > without, "the raw string measures wider (it has the &)");
        CHECK(drawn > 0, "and DrawTextA draws it, minus the marker");
    }

    /* The X server's keys have to arrive as virtual keys, and a key nobody
     * types a character with is only ever seen through this table: F2 renames
     * a file and F5 refreshes the view, and both did nothing at all in a real
     * window until the function keys were in it — the headless script injects
     * virtual keys straight and so never showed it. */
    {
        CHECK(ween_x11_keysym_to_vk(0xffbf) == VK_F2 &&
                  ween_x11_keysym_to_vk(0xffc2) == VK_F5,
              "F2 and F5 come through as their virtual keys");
        CHECK(ween_x11_keysym_to_vk(0xffbe) == VK_F1 &&
                  ween_x11_keysym_to_vk(0xffc7) == VK_F10 &&
                  ween_x11_keysym_to_vk(0xffc9) == VK_F12,
              "and the twelve of them run in order");
        CHECK(ween_x11_keysym_to_vk(0xff55) == VK_PRIOR &&
                  ween_x11_keysym_to_vk(0xff56) == VK_NEXT,
              "Page Up and Page Down as well");
        CHECK(ween_x11_keysym_to_vk(0xff0d) == VK_RETURN &&
                  ween_x11_keysym_to_vk(0xff1b) == VK_ESCAPE &&
                  ween_x11_keysym_to_vk('a') == 'A',
              "beside the keys that were already there");
    }

    DestroyWindow(g_host);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("keys_test: all passed\n");
    return 0;
}
