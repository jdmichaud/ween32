/* The clipboard and double-click word selection, through an EDIT control.
 * Both paths are driven by messages rather than the pump, so the assertions
 * can look between one step and the next. */

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

static HWND g_edit;
static int g_changes, g_button_clicks, g_list_selchanges;

enum { ID_FAST = 700, ID_LIST };

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND && LOWORD(wp) == ID_FAST) {
        g_button_clicks++;
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == ID_LIST &&
        HIWORD(wp) == LBN_SELCHANGE) {
        g_list_selchanges++;
        return 0;
    }
    if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE) {
        g_changes++;
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* A press and release through the pump, named for the window they land on. */
static void inject_click(HWND w, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_MOUSE_DOWN;
    ev.button = 1;
    ev.win = w->backend_win;
    ev.x = x;
    ev.y = y;
    ween_headless_inject(ev);
    ev.kind = WEEN_EV_MOUSE_UP;
    ween_headless_inject(ev);
}

static void key(unsigned vk, int ctrl)
{
    SendMessageA(g_edit, WM_KEYDOWN, vk, ctrl ? (LPARAM)(1L << 28) : 0);
}

static const char *text_of(HWND w)
{
    static char buf[256];
    GetWindowTextA(w, buf, sizeof buf);
    return buf;
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenclip";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    HWND w = CreateWindowExA(0, "weenclip", "clip",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0,
                             0, 300, 120, NULL, NULL, NULL, NULL);
    g_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "one two three",
                             WS_CHILD | WS_VISIBLE | ES_LEFT, 10, 10, 200, 20,
                             w, NULL, NULL, NULL);
    CHECK(g_edit != NULL, "an edit with something in it");
    SetFocus(g_edit);

    /* The clipboard by itself: it is empty until something is put in it. */
    CHECK(!IsClipboardFormatAvailable(CF_TEXT), "the clipboard starts empty");
    CHECK(OpenClipboard(w), "it opens");
    CHECK(!OpenClipboard(w), "and only once at a time");
    CloseClipboard();

    /* Double-click in the middle of "two" selects that word and no more. */
    {
        const ween_strike *f = ween_gui_font();
        /* the x of a caret two characters into "two", which starts at 4 */
        int x = ween_strike_pen(f, "one two three", 5) + 3;
        SendMessageA(g_edit, WM_LBUTTONDBLCLK, 0, MAKELPARAM(x, 8));
    }

    /* What a word *is*, against the machine's own EDIT. "cat_dog cat9 don't
     * (cat)" in a real one, double clicked a character into each of the four
     * runs, gives 0..8, 8..13, 13..19 and 19..24 -- every one of them a run
     * between spaces with its trailing space, brackets and underscores and
     * apostrophes all inside the word. Before this was measured ween32 split
     * "cat_dog" nowhere but "(cat)" at both brackets. */
    {
        static const struct {
            int at, from, to;
            const char *what;
        } cases[] = {
            { 1, 0, 8, "an underscore is inside an EDIT's word" },
            { 9, 8, 13, "so is a digit, with the trailing space" },
            { 14, 13, 19, "so is an apostrophe" },
            { 21, 19, 24, "and so are the brackets round a word" },
        };
        const char *text = "cat_dog cat9 don't (cat)";
        const ween_strike *sf = ween_gui_font();
        HWND ed = CreateWindowExA(0, "EDIT", text,
                                  WS_CHILD | WS_VISIBLE | ES_LEFT, 10, 40,
                                  260, 20, w, NULL, NULL, NULL);
        int k;
        /* The x of a character, the way the test above finds one: this
         * control's EM_POSFROMCHAR answers nought for every index, which the
         * machine's does not -- worth knowing and not fixed here. */
        for (k = 0; ed && k < 4; k++) {
            DWORD from = 0, to = 0;
            int x = ween_strike_pen(sf, text, cases[k].at) + 3;
            SendMessageA(ed, WM_LBUTTONDBLCLK, 0, MAKELPARAM(x, 8));
            SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
            CHECK((int)from == cases[k].from && (int)to == cases[k].to,
                  cases[k].what);
        }
        if (ed)
            DestroyWindow(ed);
        SetFocus(g_edit);
    }

    /* What was selected is what Ctrl+C puts on the clipboard, which is how
     * the selection is checked without reaching inside the control. */
    key('C', 1);
    CHECK(IsClipboardFormatAvailable(CF_TEXT), "Ctrl+C put text on it");
    OpenClipboard(w);
    CHECK(strcmp((const char *)GetClipboardData(CF_TEXT), "two ") == 0,
          "a double click selected the word under it, with its space");
    CloseClipboard();

    /* Copying leaves the word selected, so pasting straight after replaces
     * what was copied with itself — which is what win32 does and is worth
     * pinning down, because it looks like nothing happened. */
    g_changes = 0;
    key('V', 1);
    CHECK(strcmp(text_of(g_edit), "one two three") == 0,
          "pasting over the selection replaces it");
    CHECK(g_changes == 1, "and the parent heard EN_CHANGE once");

    /* With the caret collapsed first, the paste inserts. */
    key(VK_HOME, 0);
    key('V', 1);
    CHECK(strcmp(text_of(g_edit), "two one two three") == 0,
          "with nothing selected it inserts at the caret");

    /* Ctrl+A then Ctrl+X empties the control into the clipboard. */
    key('A', 1);
    key('X', 1);
    CHECK(strcmp(text_of(g_edit), "") == 0, "Ctrl+A and Ctrl+X cut the lot");
    OpenClipboard(w);
    CHECK(strcmp((const char *)GetClipboardData(CF_TEXT), "two one two three") ==
              0,
          "and the lot is what is on the clipboard");
    CloseClipboard();

    /* A read-only control copies but does not cut. */
    {
        HWND ro = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "keep me",
                                  WS_CHILD | WS_VISIBLE | ES_LEFT | ES_READONLY,
                                  10, 40, 200, 20, w, NULL, NULL, NULL);
        SendMessageA(ro, WM_LBUTTONDBLCLK, 0, MAKELPARAM(4, 8));
        SendMessageA(ro, WM_COPY, 0, 0);
        OpenClipboard(w);
        CHECK(strcmp((const char *)GetClipboardData(CF_TEXT), "keep ") == 0,
              "a read-only edit still selects and copies");
        CloseClipboard();
        SendMessageA(ro, WM_CUT, 0, 0);
        CHECK(strcmp(text_of(ro), "keep me") == 0,
              "but WM_CUT leaves its text alone");
        DestroyWindow(ro);
    }

    /* Clicking quickly must not lose clicks. A window is only sent double
     * clicks if its class asked for them, and a control that did — BUTTON —
     * treats one as a press too. This was the bug: every second press arrived
     * as WM_LBUTTONDBLCLK and controls that did not handle it dropped it.
     *
     * Both controls are driven by injected events rather than SendMessage,
     * because the gate lives in the routing and a sent message goes straight
     * past it. A button alone would prove nothing either: it survives double
     * clicks by handling them, so the list box is the one that tests the gate.
     */
    {
        HWND host = CreateWindowExA(0, "weenclip", "fast",
                                    WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0,
                                    240, 160, NULL, NULL, NULL, NULL);
        int cx = WEEN_NC_FRAME, cy = WEEN_NC_FRAME + WEEN_NC_CAPTION;
        CreateWindowA("BUTTON", "Press", WS_CHILD | WS_VISIBLE, 10, 10, 80, 24,
                      host, (HMENU)(UINT_PTR)ID_FAST, NULL, NULL);
        HWND list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                                    WS_CHILD | WS_VISIBLE | LBS_NOTIFY, 10, 50,
                                    120, 60, host, (HMENU)(UINT_PTR)ID_LIST,
                                    NULL, NULL);
        SendMessageA(list, LB_ADDSTRING, 0, (LPARAM) "one");
        SendMessageA(list, LB_ADDSTRING, 0, (LPARAM) "two");

        for (int i = 0; i < 4; i++)
            inject_click(host, cx + 40, cy + 20);   /* the button */
        for (int i = 0; i < 4; i++)
            inject_click(host, cx + 40, cy + 70);   /* the list box */

        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0))
            DispatchMessageA(&msg);

        CHECK(g_button_clicks == 4, "four fast clicks on a button are four");
        CHECK(g_list_selchanges == 4,
              "and four on a control that never asked for double clicks");
        DestroyWindow(host);
    }

    DestroyWindow(w);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("clip_test: all passed\n");
    return 0;
}
