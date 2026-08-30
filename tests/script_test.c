/* The scripted-input vocabulary, asked whether it means what it says.
 *
 * `WEEN32_SCRIPT` is how every rendered check in both repositories drives the
 * program, and **nothing tested it until this file**. That is not a gap of
 * tidiness: a harness that misreports its own input produces measurements
 * that are correct about a program which never received them, and the
 * repository has now paid for that three times in one day -- a `SendMessage`
 * that carried no key state, a modifier ridden in the lParam where nothing
 * read it, and the one below.
 *
 * `h:s` is documented as holding Shift "over the presses that follow". It
 * held it over mouse presses only: the key branch took its modifiers from the
 * *case of the letter* -- `K:` and `C:` -- and never looked at the held
 * state. So `h:s k:39` and `K:39` read as two spellings of one instruction
 * and one of them silently did nothing.
 *
 * It cost a false finding: a probe selected `def` in "abc def ghi", measured
 * that a font size applied to it changed no pixels, and reported a rendering
 * bug that three people then failed to reproduce. The selection had never
 * been made. **The measurement was honest and the input was not.**
 *
 * Driven end to end rather than by inspecting the queue, because the queue is
 * not what the checks depend on -- they depend on a key arriving at a control
 * with a modifier attached, which is what this asserts.
 */

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

static LRESULT CALLBACK host_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(wnd, msg, wp, lp);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    /* Home, four Rights to the start of "def", then Shift held over three
     * more. The capital spelling `K:39` is asserted by the same shape in the
     * checks that already use it; this is the one that was inert. */
    setenv("WEEN32_SCRIPT",
           "k:36 k:39 k:39 k:39 k:39 h:s k:39 k:39 k:39 h:", 1);
    ween_active_backend = ween_backend_headless();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenscript";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    HWND host = CreateWindowExA(0, "weenscript", "script",
                                WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 320,
                                160, NULL, NULL, NULL, NULL);
    HWND ed = CreateWindowExA(0, "EDIT", "abc def ghi",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 4, 4, 300,
                              24, host, NULL, NULL, NULL);
    SetFocus(ed);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    DWORD sel = (DWORD)SendMessageA(ed, EM_GETSEL, 0, 0);
    int lo = LOWORD(sel), hi = HIWORD(sel);
    printf("     selection %d..%d, want 4..7\n", lo, hi);
    CHECK(lo == 4 && hi == 7,
          "h:s holds Shift over the arrow keys, not only over the mouse");

    printf("%s\n", g_failures ? "script_test: FAILURES" : "script_test: all passed");
    return g_failures ? 1 : 0;
}
