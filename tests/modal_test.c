/* Modal dialogs: DialogBox does not return until EndDialog is called, the
 * owner cannot be used while one is up, and MessageBoxA is the same machinery
 * with the window built from the message.
 *
 * Every modal loop here is ended by a key that was injected before it started,
 * so the script never runs dry — a run that reaches the end of its events
 * quits, and these want three loops one after another. */

#define _POSIX_C_SOURCE 200112L /* setenv */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ween_internal.h"
#include "../examples/win32_dlg.h"

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

static HWND g_owner;
static int g_owner_disabled_during = -1;
static int g_init_seen;
static int g_dlg_x, g_dlg_y;
/* The close-box case: the dialog asks to be closed the way its caption
 * would, and counts the Cancel command that should come back. */
static int g_close_case;
static int g_cancel_commands;

static int g_owner_clicks;

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_LBUTTONDOWN) {
        g_owner_clicks++;
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        g_init_seen++;
        /* the whole of what modal means: the owner is disabled meanwhile */
        g_owner_disabled_during = (g_owner->style & WS_DISABLED) != 0;
        g_dlg_x = dlg->x;
        g_dlg_y = dlg->y;
        if (g_close_case) /* as pressing the caption's close box does */
            PostMessageA(dlg, WM_CLOSE, 0, 0);
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDCANCEL)
            g_cancel_commands++;
        EndDialog(dlg, (INT_PTR)LOWORD(wp));
        return TRUE;
    }
    return FALSE;
}

static void key(unsigned vk)
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
    wc.lpszClassName = "weenmodal";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    g_owner = CreateWindowExA(0, "weenmodal", "owner",
                              WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                              0, 0, 300, 200, NULL, NULL, NULL, NULL);
    CHECK(g_owner != NULL, "an owner for the dialogs");

    static const dlg_item items[] = {
        { WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 10, 120, 8, 0, ATOM_STATIC,
          "Modal", NULL, 0 },
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 30, 30, 50, 14,
          IDOK, ATOM_BUTTON, "OK", NULL, 0 },
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 90, 30, 50, 14,
          IDCANCEL, ATOM_BUTTON, "Cancel", NULL, 0 },
    };
    static unsigned char tmpl[1024];
    build_dialog_template(tmpl, sizeof(tmpl),
                          WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 150,
                          52, "Modal", items, 3);

    /* Every key the three loops below will consume, in the order they run. */
    key(VK_RETURN); /* the first dialog: the default button */
    key(VK_ESCAPE); /* the second: cancelled */
    key(VK_RETURN); /* the message box */

    INT_PTR r = DialogBoxIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, g_owner,
                                        dlg_proc, 0);
    CHECK(g_init_seen == 1, "the dialog was initialised");
    CHECK(g_dlg_x == g_owner->x + WEEN_NC_FRAME &&
              g_dlg_y == g_owner->y + WEEN_NC_FRAME + WEEN_NC_CAPTION,
          "a template's 0,0 is the owner's client corner, not the screen's");
    CHECK(r == IDOK, "Enter on the default button is what DialogBox returned");
    CHECK(g_owner_disabled_during == 1, "the owner was disabled while it was up");
    CHECK((g_owner->style & WS_DISABLED) == 0, "and enabled again afterwards");

    r = DialogBoxIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, g_owner, dlg_proc,
                                0);
    CHECK(r == IDCANCEL, "Escape cancels, and that is returned too");
    CHECK(g_init_seen == 2, "a second dialog ran after the first returned");

    /* Modal means the owner takes no input at all — not in its client area
     * and not in its caption. Without that the owner can still be dragged and
     * closed from under the dialog that is supposed to be blocking it. */
    {
        ween_event click;
        memset(&click, 0, sizeof(click));
        click.kind = WEEN_EV_MOUSE_DOWN;
        click.button = 1;
        click.win = g_owner->backend_win;
        click.x = 30;
        click.y = 8; /* the caption, which would start a drag */
        g_owner->style |= WS_DISABLED;
        int was_x = g_owner->x;
        ween_headless_inject(click);
        ween_event end;
        memset(&end, 0, sizeof(end));
        end.kind = WEEN_EV_END;
        ween_headless_inject(end);
        MSG m;
        while (GetMessageA(&m, NULL, 0, 0))
            DispatchMessageA(&m);
        CHECK(g_owner->x == was_x,
              "a disabled window cannot be dragged by its caption");
        g_owner->style &= ~(DWORD)WS_DISABLED;
    }

    int mb = MessageBoxA(g_owner, "Two lines\nof message.", "Title", MB_OK);
    CHECK(mb == IDOK, "the message box came back with its button's id");
    CHECK((g_owner->style & WS_DISABLED) == 0,
          "and left the owner usable again");

    /* The caption's close box. DefDlgProc turns WM_CLOSE into a Cancel
     * command and leaves the rest to the dialog procedure; DefWindowProc's
     * answer — destroy the window — would leave the modal loop waiting on a
     * window that no longer exists, with the owner still disabled, which is
     * a hung program. The Escape is only a way out if that happens, so a
     * regression fails here rather than hanging. */
    g_close_case = 1;
    g_cancel_commands = 0;
    key(VK_ESCAPE);
    r = DialogBoxIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, g_owner, dlg_proc,
                                0);
    CHECK(r == IDCANCEL, "the close box ends a modal dialog as a Cancel");
    CHECK(g_cancel_commands == 1,
          "and the dialog procedure heard it as one, not as a destroyed window");
    CHECK((g_owner->style & WS_DISABLED) == 0,
          "the owner is usable again after a dialog is closed that way");
    g_close_case = 0;

    /* What takes the keyboard when a dialog closes. A box put up not to be
     * activated — a menu, the suggestions under an address bar — must never
     * become the active window, or every press meant for the window behind
     * it goes there instead and the program looks dead. */
    {
        WNDCLASSA pc;
        HWND box;
        memset(&pc, 0, sizeof(pc));
        pc.lpfnWndProc = host_proc;
        pc.lpszClassName = "weenmodalbox";
        pc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassA(&pc);
        /* made after the owner, and never shown: the newest window is what
         * activation used to fall to */
        box = CreateWindowExA(WS_EX_NOACTIVATE, "weenmodalbox", "",
                              WS_POPUP | WS_BORDER, 0, 0, 300, 200, NULL, NULL,
                              NULL, NULL);
        CHECK(box != NULL, "a box that is not to be activated, and not shown");
        g_cancel_commands = 0;
        key(VK_ESCAPE);
        DialogBoxIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, g_owner, dlg_proc,
                                0);
        CHECK(GetActiveWindow() == g_owner,
              "and when it closes the owner is active again, not the box");
        DestroyWindow(box);
    }

    DestroyWindow(g_owner);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("modal_test: all passed\n");
    return 0;
}
