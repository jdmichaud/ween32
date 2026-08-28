/* The dialog manager, headless: build a DLGTEMPLATE in memory, create the
 * dialog (the manager instantiates the controls and maps their DLUs), then
 * drive it — assert the controls exist at the mapped pixel rects, that a click
 * routes to WM_COMMAND, and that Enter fires the default id via
 * IsDialogMessage. This is the authentic win32 layout path end to end. */

#define _POSIX_C_SOURCE 200112L /* setenv */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ween_internal.h"
#include "../examples/win32_dlg.h"

static int g_failures = 0;
/* Evaluate the condition exactly once: some checks here (IsDialogMessage) have
 * side effects. */
#define CHECK(cond, name)                                                      \
    do {                                                                       \
        int ok_ = (cond);                                                      \
        printf(ok_ ? "ok   %s\n" : "FAIL %s\n", name);                         \
        if (!ok_)                                                              \
            g_failures++;                                                      \
    } while (0)

#define ID_OK 1
#define ID_CANCEL 2

static int g_ok_clicks = 0;
static int g_enter_clicks = 0;

static INT_PTR CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SendMessageA(hwnd, DM_SETDEFID, ID_OK, 0);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_OK)
            g_ok_clicks++;
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
    default:
        return FALSE;
    }
}

static ween_event ev_mouse(ween_ev_kind k, int x, int y)
{
    ween_event e;
    memset(&e, 0, sizeof(e));
    e.kind = k;
    e.x = x;
    e.y = y;
    e.button = 1;
    return e;
}

static ween_event ev_key(unsigned vk)
{
    ween_event e;
    memset(&e, 0, sizeof(e));
    e.kind = WEEN_EV_KEY;
    e.vk = vk;
    return e;
}

#define ID_EDIT1 10
#define ID_EDIT2 11

static INT_PTR CALLBACK fields_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd;
    (void)wp;
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE; /* let the manager set the focus */
    return FALSE;
}

/* A dialog hands a field the focus with everything in it selected, so that
 * what is typed replaces what was there rather than joining it. It is the
 * dialog that does this and not the field — the manager asks WM_GETDLGCODE
 * and sends EM_SETSEL to whatever says it keeps a selection. Paint's
 * Attributes box is the reason it was noticed: 120 typed over a width of 512
 * came out as 120512. */
static void test_field_focus(void)
{
    static const dlg_item items[] = {
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER, 10, 10, 40, 12,
          ID_EDIT1, ATOM_EDIT, "512", NULL, 0 },
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER, 60, 10, 40, 12,
          ID_EDIT2, ATOM_EDIT, "384", NULL, 0 },
    };
    static unsigned char tmpl[1024];
    char buf[32];
    build_dialog_template(tmpl, sizeof(tmpl),
                          WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 140,
                          60, "Fields", items, 2);
    HWND dlg = CreateDialogIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, NULL,
                                          fields_proc, 0);
    SendMessageA(GetFocus(), WM_CHAR, '7', 0);
    GetWindowTextA(GetDlgItem(dlg, ID_EDIT1), buf, sizeof(buf));
    CHECK(!strcmp(buf, "7"),
          "a dialog selects a field's text when it hands it the focus");

    MSG m;
    memset(&m, 0, sizeof(m));
    m.hwnd = dlg;
    m.message = WM_KEYDOWN;
    m.wParam = VK_TAB;
    IsDialogMessageA(dlg, &m);
    SendMessageA(GetFocus(), WM_CHAR, '9', 0);
    GetWindowTextA(GetDlgItem(dlg, ID_EDIT2), buf, sizeof(buf));
    CHECK(!strcmp(buf, "9"), "and again for the field Tab moves on to");
    DestroyWindow(dlg);
}

/* What a modal box does to the keyboard on its way out. A program puts one
 * up from a window where something already had the focus -- a text editor's
 * Open dialog, opened while the caret was in the page -- and when the box
 * closes the typing has to go back where it was. Windows activates the owner
 * again and restores the focus within it; here that used to be nobody's job,
 * so the next keystroke after Notepad's Open dialog went nowhere. */
static int g_owner_focus;

static LRESULT CALLBACK owner_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SETFOCUS) {
        g_owner_focus++;
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void test_focus_comes_back(void)
{
    static const dlg_item items[] = {
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 10, 30, 50, 14,
          ID_OK, ATOM_BUTTON, "OK", NULL, 0 },
    };
    static unsigned char tmpl[1024];
    WNDCLASSA wc;
    HWND owner, field;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = owner_proc;
    wc.lpszClassName = "weendlgowner";
    RegisterClassA(&wc);
    owner = CreateWindowExA(0, "weendlgowner", "owner",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 240, 120,
                            NULL, NULL, NULL, NULL);
    field = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE, 0, 0, 200, 20,
                            owner, (HMENU)(UINT_PTR)ID_EDIT1, NULL, NULL);
    SetFocus(field);
    CHECK(GetFocus() == field, "the keyboard is in the owner's field");

    build_dialog_template(tmpl, sizeof(tmpl),
                          WS_POPUP | WS_CAPTION | WS_SYSMENU, 140, 60, "Modal",
                          items, 1);
    ween_headless_inject(ev_key(VK_ESCAPE)); /* the box cancels itself */
    g_owner_focus = 0;
    DialogBoxIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, owner, proc, 0);
    CHECK(GetFocus() == field,
          "and it is back in it when the box comes down");
    SendMessageA(GetFocus(), WM_CHAR, 'z', 0);
    {
        char buf[16] = "";
        GetWindowTextA(field, buf, sizeof buf);
        CHECK(strcmp(buf, "z") == 0, "so what is typed next lands in it");
    }
    CHECK(GetActiveWindow() == owner, "and the owner is the active window again");
    DestroyWindow(owner);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1); /* pixel asserts are 96-dpi */
    ween_active_backend = ween_backend_headless();

    static const dlg_item items[] = {
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 10, 30, 50, 14,
          ID_OK, ATOM_BUTTON, "OK", NULL, 0 },
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 70, 30, 50, 14,
          ID_CANCEL, ATOM_BUTTON, "Cancel", NULL, 0 },
        { WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 8, 120, 8, 100, ATOM_STATIC,
          "Pick one", NULL, 0 },
    };
    static unsigned char tmpl[1024];
    build_dialog_template(tmpl, sizeof(tmpl),
                          WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 140,
                          60, "Test", items, 3);

    HWND dlg = CreateDialogIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, NULL,
                                          proc, 0);
    CHECK(dlg != NULL, "CreateDialogIndirectParamA builds the dialog");
    HWND ok = GetDlgItem(dlg, ID_OK);
    CHECK(ok != NULL, "the OK control was instantiated by the manager");
    CHECK(GetDlgItem(dlg, 100) != NULL, "the STATIC control was instantiated");
    CHECK(GetDlgCtrlID(ok) == ID_OK, "GetDlgCtrlID returns the template id");

    /* The 50x14 DLU button maps to the classic 75x23 px. */
    RECT cr;
    GetClientRect(ok, &cr);
    printf("info: OK button client size = %ldx%ld px\n", (long)cr.right,
           (long)cr.bottom);
    CHECK(cr.right == 75 && cr.bottom == 23, "50x14 DLU button is 75x23 px");

    /* Enter fires the default id (ID_OK) through IsDialogMessage. */
    MSG m;
    memset(&m, 0, sizeof(m));
    m.hwnd = dlg;
    m.message = WM_KEYDOWN;
    m.wParam = VK_RETURN;
    CHECK(IsDialogMessageA(dlg, &m), "IsDialogMessage consumes Enter");
    CHECK(g_ok_clicks == 1, "Enter fired the default command (ID_OK)");

    /* Click OK: client (10,30)+edges -> window coords via the client origin. */
    int ox, oy;
    ween_client_origin(ok, &ox, &oy);
    int cx = ox + 75 / 2, cy = oy + 23 / 2;
    ween_headless_inject(ev_mouse(WEEN_EV_MOUSE_DOWN, cx, cy));
    ween_headless_inject(ev_mouse(WEEN_EV_MOUSE_UP, cx, cy));
    ween_headless_inject(ev_key(VK_ESCAPE)); /* Esc -> IDCANCEL (ignored) */
    UpdateWindow(dlg);

    while (GetMessageA(&m, NULL, 0, 0)) {
        if (!IsDialogMessageA(dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
    CHECK(g_ok_clicks == 2, "clicking OK routed a second WM_COMMAND");
    test_field_focus();
    test_focus_comes_back();
    (void)g_enter_clicks;

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("dlg_test: all passed\n");
    return 0;
}
