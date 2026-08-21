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

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
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
        return TRUE;
    }
    if (msg == WM_COMMAND) {
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
          "Modal" },
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 30, 30, 50, 14,
          IDOK, ATOM_BUTTON, "OK" },
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 90, 30, 50, 14,
          IDCANCEL, ATOM_BUTTON, "Cancel" },
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

    DestroyWindow(g_owner);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("modal_test: all passed\n");
    return 0;
}
