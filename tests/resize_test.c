/* A window with a sizing border: dragging its grip resizes it, the client
 * area follows, and WM_SIZE reaches the app — all through the headless
 * backend, so no window manager is involved. */

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

static HWND g_status;
static int g_sized;
static int g_first_w, g_first_h;

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_status = CreateWindowA(STATUSCLASSNAMEA, "",
                                 WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0,
                                 0, hwnd, NULL, NULL, NULL);
        return 0;
    case WM_SIZE:
        if (!g_sized++) { /* the size the app was handed to lay out at */
            g_first_w = LOWORD(lp);
            g_first_h = HIWORD(lp);
        }
        if (g_status) /* as a win32 app forwards it */
            SendMessageA(g_status, WM_SIZE, wp, lp);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static void mouse(ween_ev_kind kind, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.x = ev.x_root = x;
    ev.y = ev.y_root = y;
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
    wc.lpszClassName = "weenresize";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);

    /* grab the grip in the bottom-right corner and pull it out 40 x 30 */
    mouse(WEEN_EV_MOUSE_DOWN, 296, 196);
    mouse(WEEN_EV_MOUSE_MOVE, 336, 226);
    mouse(WEEN_EV_MOUSE_UP, 336, 226);

    HWND wnd = CreateWindowExA(0, "weenresize", "resize",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU |
                                   WS_THICKFRAME | WS_VISIBLE,
                               0, 0, 300, 200, NULL, NULL, NULL, NULL);
    CHECK(wnd != NULL, "a window with a sizing border was created");

    RECT cr;
    GetClientRect(wnd, &cr);
    CHECK(cr.right == 300 - 2 * WEEN_NC_SIZEFRAME,
          "the sizing border is wider than a fixed frame");

    /* Creating a window is itself a size change, and win32 says so before
     * CreateWindow returns. An app that lays its children out in WM_SIZE —
     * which is the usual way to write one — is otherwise left with all of
     * them at whatever size they were created with, here a status bar of
     * zero width, and draws nothing until something resizes the window. */
    CHECK(g_sized == 1, "creating the window was itself a WM_SIZE");
    CHECK(g_first_w == cr.right && g_first_h == cr.bottom,
          "reporting the client area, so children can be laid out in it");

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0))
        DispatchMessageA(&msg);

    CHECK(wnd->w == 340 && wnd->h == 230,
          "dragging the status bar's grip resized the window");
    CHECK(g_sized > 1, "the app was told through WM_SIZE");

    GetClientRect(wnd, &cr);
    CHECK(cr.right == 340 - 2 * WEEN_NC_SIZEFRAME,
          "the client area grew with the window");
    CHECK(g_status && g_status->w == cr.right,
          "the status bar followed the new width");

    /* A window manager may hand back a geometry of its own — a tiling one
     * always does. A window with a sizing border follows it; one without has
     * told the window manager it is fixed, and win32 semantics are that it
     * stays the size its app asked for however big the window it is given. */
    {
        ween_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.kind = WEEN_EV_RESIZE;
        ev.x = 900;
        ev.y = 500;
        ween_headless_inject(ev);
        ween_event end;
        memset(&end, 0, sizeof(end));
        end.kind = WEEN_EV_END;
        ween_headless_inject(end);

        WNDCLASSA fixed;
        memset(&fixed, 0, sizeof(fixed));
        fixed.lpfnWndProc = DefWindowProcA;
        fixed.lpszClassName = "weenfixed";
        fixed.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        RegisterClassA(&fixed);
        HWND stuck = CreateWindowExA(0, "weenfixed", "fixed",
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU |
                                         WS_VISIBLE,
                                     0, 0, 320, 180, NULL, NULL, NULL, NULL);
        MSG m;
        while (GetMessageA(&m, NULL, 0, 0))
            DispatchMessageA(&m);
        CHECK(stuck && stuck->w == 320 && stuck->h == 180,
              "a window with no sizing border keeps the size it asked for");
        RECT cr2;
        GetClientRect(stuck, &cr2);
        CHECK(cr2.right == 320 - 2 * WEEN_NC_FRAME,
              "and so does its client area, whatever it is given");
        DestroyWindow(stuck);
    }

    /* Where a status bar puts its parts, which is a rule and not a guess.
     * Real comctl32, asked with SB_GETRECT after SB_SETPARTS 200 260 320 --
     * tools/vm/ctlprobe.c on the Windows 2000 -- answers:
     *
     *     part 0  0,2 200x18     part 1  202,2 58x18     part 2  262,2 58x18
     *
     * So a part ends at the edge it was given and the next one starts two
     * past it: the two-pixel gap comes out of the *front* of the following
     * part, not off the back of the one before. Those are the exact numbers
     * below. It matters because a program that reads the divider positions off
     * a picture and calls them edges is out by exactly that two, which is what
     * happened to WordPad's status bar. */
    {
        HWND host = CreateWindowA("weenresize", "", WS_OVERLAPPEDWINDOW, 0, 0, 420,
                                  200, NULL, NULL, NULL, NULL);
        HWND sb = CreateWindowA(STATUSCLASSNAMEA, "", WS_CHILD | WS_VISIBLE, 0,
                                0, 400, 20, host, NULL, NULL, NULL);
        int edges[3] = { 200, 260, 320 };
        RECT p0, p1, p2;
        SendMessageA(sb, SB_SETPARTS, 3, (LPARAM)edges);
        SendMessageA(sb, SB_SETTEXTA, 0, (LPARAM) "first");
        SendMessageA(sb, SB_SETTEXTA, 1, (LPARAM) "second");
        SendMessageA(sb, SB_SETTEXTA, 2, (LPARAM) "third");
        CHECK(SendMessageA(sb, SB_GETRECT, 0, (LPARAM)&p0) &&
              SendMessageA(sb, SB_GETRECT, 1, (LPARAM)&p1) &&
              SendMessageA(sb, SB_GETRECT, 2, (LPARAM)&p2),
              "a status bar can be asked where its parts are");
        CHECK(p0.left == 0 && p0.right == 200,
              "the first part runs from nothing to the first edge");
        CHECK(p1.left == 202 && p1.right == 260,
              "and the next starts two past that edge and ends on its own");
        CHECK(p2.left == 262 && p2.right == 320,
              "and so does the one after it");
        CHECK(SendMessageA(sb, SB_GETRECT, 3, (LPARAM)&p0) == 0,
              "a part it has not got has no rectangle");

        /* SBT_NOBORDERS, which is what WordPad's message pane has: the part
         * keeps its rectangle and loses its sunken box. Read off the surface
         * rather than asserted about, because a flag that is stored and never
         * looked at passes every test that only asks where the part is. */
        {
            struct ween_wnd *top = ween_top_level(sb);
            int ox, oy, shadow_before = 0, shadow_after = 0;
            ween_client_origin(sb, &ox, &oy);
            ShowWindow(host, SW_SHOWNORMAL);
            InvalidateRect(sb, NULL, TRUE);
            ween_flush_paint();
            for (int y = p1.top; y < p1.bottom; y++)
                if ((top->surface.px[(size_t)(oy + y) * top->surface.w +
                                     ox + p1.left] &
                     0xffffff) == (WEEN_SHADOW & 0xffffff))
                    shadow_before++;
            SendMessageA(sb, SB_SETTEXTA, 1 | SBT_NOBORDERS,
                         (LPARAM) "second");
            InvalidateRect(sb, NULL, TRUE);
            ween_flush_paint();
            for (int y = p1.top; y < p1.bottom; y++)
                if ((top->surface.px[(size_t)(oy + y) * top->surface.w +
                                     ox + p1.left] &
                     0xffffff) == (WEEN_SHADOW & 0xffffff))
                    shadow_after++;
            CHECK(shadow_before > 0, "a part is drawn with a sunken box");
            CHECK(shadow_after == 0, "and SBT_NOBORDERS takes it away");
            SendMessageA(sb, SB_GETRECT, 1, (LPARAM)&p2);
            CHECK(p2.left == p1.left && p2.right == p1.right,
                  "and leaves the part where it was");
        }
        DestroyWindow(host);
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("resize_test: all passed\n");
    return 0;
}
