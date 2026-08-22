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

static HWND g_edit, g_host;
static int g_changes, g_button_clicks;

enum { ID_FAST = 700 };

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND && LOWORD(wp) == ID_FAST) {
        g_button_clicks++;
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
     * clicks if its class asked for them, and a control that did — the EDIT
     * here — treats one as a click too. This was the bug: every second press
     * arrived as WM_LBUTTONDBLCLK, controls that did not handle it dropped
     * it, and half of a run of fast clicks vanished. */
    {
        HWND host = CreateWindowExA(0, "weenclip", "fast",
                                    WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0,
                                    200, 120, NULL, NULL, NULL, NULL);
        int cx = WEEN_NC_FRAME, cy = WEEN_NC_FRAME + WEEN_NC_CAPTION;
        g_button_clicks = 0;
        g_host = host;
        CreateWindowA("BUTTON", "Press", WS_CHILD | WS_VISIBLE, 10, 10, 80, 24,
                      host, (HMENU)(UINT_PTR)ID_FAST, NULL, NULL);
        for (int i = 0; i < 4; i++) { /* four presses, one place, no waiting */
            ween_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.kind = WEEN_EV_MOUSE_DOWN;
            ev.button = 1;
            ev.win = host->backend_win;
            ev.x = cx + 40;
            ev.y = cy + 20;
            ween_headless_inject(ev);
            ev.kind = WEEN_EV_MOUSE_UP;
            ween_headless_inject(ev);
        }
        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0))
            DispatchMessageA(&msg);
        CHECK(g_button_clicks == 4, "four fast clicks are four clicks");
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
