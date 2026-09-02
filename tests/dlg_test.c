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
/* An option button's focus rectangle stops at the control's own edge.
 *
 * The rectangle goes round the *label*: its text's height with a pixel of
 * margin, which is two rows more than a thirteen-pixel option button has got.
 * Asked for those rows, the library drew a rectangle the control then clipped
 * the top and the bottom off, and what reached the page was two upright sides
 * with nothing joining them -- 142 pixels of Folder Options General against
 * the machine, on the one button that had the keyboard.
 *
 * The machine draws that rectangle as the control's thirteen rows exactly,
 * and the one round Find's "Down" as sixteen inside a control of twenty. One
 * rule gives both: the label's rectangle, clipped to the control. Both halves
 * are asserted here, because clipping always -- the whole client rectangle --
 * would satisfy the first on its own and is not what the machine does.
 *
 * Eight dialog units is the thirteen pixels every option button in Folder
 * Options has; thirteen units is twenty-one, taller than the label wants. */
static int focus_rows(struct ween_wnd *top, HWND btn, int *first, int *last)
{
    RECT r;
    int ox, oy, rows = 0;
    *first = *last = -1;
    GetClientRect(btn, &r);
    ween_client_origin((struct ween_wnd *)btn, &ox, &oy);
    /* Counted over the label, past the box and the gap before the text, and
     * on rows the letters themselves do not reach: a row with a dozen dots
     * evenly spread is the rectangle and nothing else. */
    for (int y = oy - 2; y < oy + r.bottom + 2; y++) {
        int ink = 0, touching = 0, was = 0;
        for (int x = ox + 20; x < ox + 60; x++) {
            int on = (top->surface.px[(size_t)y * top->surface.w + x] &
                      0xffffff) == WEEN_BLACK;
            ink += on;
            touching += on && was;
            was = on;
        }
        /* A rectangle's row is every other column and nothing else: forty
         * columns give twenty dots with no two of them side by side, which
         * is what tells it from a row of letters. */
        if (ink >= 18 && !touching) {
            rows++;
            if (*first < 0)
                *first = y - oy;
            *last = y - oy;
        }
    }
    return rows;
}

static void test_focus_rect_clipped(void)
{
    static const dlg_item items[] = {
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
          10, 6, 180, 8, ID_EDIT1, ATOM_BUTTON, "Use Windows classic desktop",
          NULL, 1 },
        { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
          10, 20, 180, 13, ID_EDIT2, ATOM_BUTTON, "Use Windows classic desktop",
          NULL, 1 },
    };
    static unsigned char tmpl[1024];
    build_dialog_template(tmpl, sizeof(tmpl),
                          WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 220,
                          80, "Focus", items, 2);
    HWND dlg = CreateDialogIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, NULL,
                                          NULL, 0);
    struct ween_wnd *top = ween_top_level((struct ween_wnd *)dlg);
    HWND shortb = GetDlgItem(dlg, ID_EDIT1), tallb = GetDlgItem(dlg, ID_EDIT2);
    RECT sr, tr;
    int first, last;
    GetClientRect(shortb, &sr);
    GetClientRect(tallb, &tr);

    SetFocus(shortb);
    ween_flush_paint();
    CHECK(sr.bottom == 13 && focus_rows(top, shortb, &first, &last) == 2 &&
              first == 0 && last == sr.bottom - 1,
          "a thirteen-pixel option button's focus rectangle is its own top "
          "and bottom rows, the label's two extra clipped away");

    SetFocus(tallb);
    ween_flush_paint();
    CHECK(tr.bottom == 21 && focus_rows(top, tallb, &first, &last) == 2 &&
              last - first == 15,
          "and on a taller one it stays the label's sixteen rows, as the "
          "machine's Find box draws it");
    DestroyWindow(dlg);
}

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

    /* **A bare letter presses the button that letter marks.** A message box
     * draws `&Yes` and `&No` with the mnemonic underlined, and win32 answers
     * a plain `Y` as well as `Alt+Y` -- Sam drove that on the machine:
     * WordPad, a dirty document, Alt+F, x, then a bare `Y`, and its Save As
     * opened. Ours dispatched mnemonics inside `if (alt)` only, so the
     * underline promised a key that did nothing and the box could be
     * answered with Tab or the mouse alone.
     *
     * **The reason it is safe is the same question the Enter above asks.**
     * A dialog must not take letters from a field -- typing `y` into a file
     * name has to type a `y` -- and the field already says so:
     * `src/controls.c` answers `WM_GETDLGCODE` with `DLGC_WANTCHARS`. The
     * guard was in place and nothing asked it. */
    {
        /* Its own id, not ID_OK: a button that reports to the dialog's
         * counter would make every later assertion about that counter an
         * assertion about this test as well. The first draft did exactly
         * that and broke "clicking OK routed a second WM_COMMAND" three
         * hundred lines further down. */
        HWND yes = CreateWindowExA(0, "BUTTON", "&Yes",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 100,
                                   60, 23, dlg, (HMENU)(UINT_PTR)993, NULL,
                                   NULL);
        HWND field = CreateWindowExA(0, "EDIT", "",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP, 80,
                                     100, 80, 21, dlg, (HMENU)(UINT_PTR)992,
                                     NULL, NULL);
        char typed[32];
        /* The dispatch gives the button the focus and then clicks it, so
         * where the focus went says the letter was routed -- without a
         * counter that anything else reads. */
        SetFocus(field);
        memset(&m, 0, sizeof(m));
        m.hwnd = dlg;
        m.message = WM_KEYDOWN;
        m.wParam = 'Y';
        IsDialogMessageA(dlg, &m);
        CHECK(GetFocus() == field,
              "a field keeps its letters -- the mnemonic is not taken");
        SendMessageA(field, WM_CHAR, 'y', 1);
        GetWindowTextA(field, typed, sizeof typed);
        CHECK(!strcmp(typed, "y"), "and the letter lands in the field");

        SetFocus(yes);
        memset(&m, 0, sizeof(m));
        m.hwnd = dlg;
        m.message = WM_KEYDOWN;
        m.wParam = 'Y';
        CHECK(IsDialogMessageA(dlg, &m),
              "a bare letter is taken when the focus does not want it");
        CHECK(GetFocus() == yes,
              "and it goes to the button whose label marks that letter");

        /* **A space is a letter to a field and a press to a button**, and
         * the dialog manager used to take it from both. It pressed the
         * focus window's control whatever that was, and a field turns no
         * key into a character by itself -- that is TranslateMessage's
         * job, and told the key was handled the pump never ran it. So a
         * space could not be typed into any field in any dialog: a file
         * name with one in it came out without it, and the Open box said
         * "The file could not be found" of a file that was there. The
         * space asks the field the same question the letter above does. */
        /* A field of its own, empty: what the letter tests left in theirs
         * is theirs to keep, and a space asserted against a used field says
         * as much about the caret as about the key. */
        HWND space_field =
            CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            80, 130, 80, 21, dlg, (HMENU)(UINT_PTR)994, NULL,
                            NULL);
        SetFocus(space_field);
        memset(&m, 0, sizeof(m));
        m.hwnd = space_field; /* the character is the focus window's, as a
                                 pump addresses it */
        m.message = WM_KEYDOWN;
        m.wParam = VK_SPACE;
        m.lParam = (LPARAM)' ' << 16; /* it rides in the scan code's word */
        CHECK(!IsDialogMessageA(dlg, &m),
              "a field keeps the space -- it is typing, not a press");
        TranslateMessage(&m);
        DispatchMessageA(&m);
        /* The character is a posted message: it lands when a pump picks it
         * up, as the one below main's is going to. An empty queue is this
         * backend's "done", and the click main injects later re-arms it. */
        while (GetMessageA(&m, NULL, 0, 0)) {
            if (!IsDialogMessageA(dlg, &m)) {
                TranslateMessage(&m);
                DispatchMessageA(&m);
            }
        }
        GetWindowTextA(space_field, typed, sizeof typed);
        CHECK(!strcmp(typed, " "),
              "and the space lands in the field as a character");
        DestroyWindow(space_field);

        {
            /* Its own snapshot, put back afterwards: a press of OK here is
             * this test's business and not the ledger the click at the end
             * of main reads -- the comment above the mnemonic block says
             * exactly how a shared counter turns a later assertion into an
             * assertion about this one. */
            int before_space = g_ok_clicks;
            SetFocus(ok);
            memset(&m, 0, sizeof(m));
            m.hwnd = dlg;
            m.message = WM_KEYDOWN;
            m.wParam = VK_SPACE;
            m.lParam = (LPARAM)' ' << 16;
            CHECK(IsDialogMessageA(dlg, &m),
                  "a space is taken when the focus is a button");
            CHECK(g_ok_clicks == before_space + 1,
                  "and it presses that button");
            g_ok_clicks = before_space;
        }
        DestroyWindow(field);
        DestroyWindow(yes);
    }

    /* **Unless the focused control wants the key.** A combo box with its
     * list dropped answers `DLGC_WANTMESSAGE`, and the dialog has to let the
     * Enter through -- otherwise the default button fires, the dialog closes,
     * and the list closing with it looks exactly like a selection being made.
     *
     * That is what WordPad's `Files of type` did: the list dropped, the
     * arrows moved the highlight, and the type never committed, so one of
     * the two formats it can write could not be asked for. **Fixing the
     * combo alone was not enough** -- its handler was correct and the key
     * never reached it. The check for the control's half is in
     * tests/views_test.c; this is the half that lets it arrive. */
    {
        HWND cb = CreateWindowExA(0, "COMBOBOX", "",
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                                      WS_TABSTOP,
                                  10, 60, 120, 21, dlg, (HMENU)(UINT_PTR)991,
                                  NULL, NULL);
        int before = g_ok_clicks;
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "alpha");
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM) "beta");
        SendMessageA(cb, CB_SETCURSEL, 0, 0);
        SetFocus(cb);
        SendMessageA(cb, WM_KEYDOWN, VK_DOWN, 0); /* opens the list */
        SendMessageA(cb, WM_KEYDOWN, VK_DOWN, 0); /* highlight on "beta" */
        CHECK(SendMessageA(cb, CB_GETDROPPEDSTATE, 0, 0),
              "a dropped combo in a dialog is dropped");
        CHECK((SendMessageA(cb, WM_GETDLGCODE, VK_RETURN, 0) &
               DLGC_WANTMESSAGE) != 0,
              "and says it wants the Enter for itself");
        memset(&m, 0, sizeof(m));
        m.hwnd = dlg;
        m.message = WM_KEYDOWN;
        m.wParam = VK_RETURN;
        IsDialogMessageA(dlg, &m);
        CHECK(g_ok_clicks == before,
              "so Enter does not press the dialog's default button");
        SendMessageA(cb, WM_KEYDOWN, VK_RETURN, 0);
        CHECK(SendMessageA(cb, CB_GETCURSEL, 0, 0) == 1,
              "and the selection it commits is the one under the highlight");
        DestroyWindow(cb);
    }

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
    test_focus_rect_clipped();
    test_focus_comes_back();
    (void)g_enter_clicks;

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("dlg_test: all passed\n");
    return 0;
}
