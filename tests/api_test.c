/* End-to-end test of the win32-shaped API over the headless backend: create a
 * captioned window with buttons through RegisterClass/CreateWindowEx, drive it
 * with scripted mouse events, and assert message routing and rendered pixels.
 * No display needed. */

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

#define ID_OK 1
#define BTN_X 20
#define BTN_Y 40
#define BTN_W 75
#define BTN_H 23

static int g_got_click = 0;
static int g_got_create = 0;

static LRESULT CALLBACK test_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_got_create = 1;
        CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      BTN_X, BTN_Y, BTN_W, BTN_H, hwnd,
                      (HMENU)(UINT_PTR)ID_OK, NULL, NULL);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_OK && HIWORD(wp) == BN_CLICKED) {
            g_got_click = 1;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static ween_event ev_mouse(ween_ev_kind kind, int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.x = x;
    ev.y = y;
    ev.button = 1;
    return ev;
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1); /* pixel asserts are 96-dpi */
    ween_active_backend = ween_backend_headless();
    const char *dir = getenv("WEEN_TEST_OUT");
    char path[512];
    snprintf(path, sizeof(path), "%s/api_dialog.bmp", dir ? dir : ".");
    ween_headless_set_bmp_path(path);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = test_proc;
    wc.lpszClassName = "weentest";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    CHECK(RegisterClassA(&wc) != 0, "RegisterClassA succeeds");

    /* Click the OK button: its client rect is at (BTN_X,BTN_Y); the window's
     * client area starts at (frame, frame+caption) in window coordinates. */
    int cx = WEEN_NC_FRAME + BTN_X + BTN_W / 2;
    int cy = WEEN_NC_FRAME + WEEN_NC_CAPTION + BTN_Y + BTN_H / 2;
    ween_headless_inject(ev_mouse(WEEN_EV_MOUSE_DOWN, cx, cy));
    ween_headless_inject(ev_mouse(WEEN_EV_MOUSE_UP, cx, cy));

    HWND wnd = CreateWindowExA(0, "weentest", "ween32 api test",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               0, 0, 320, 180, NULL, NULL, NULL, NULL);
    CHECK(wnd != NULL, "CreateWindowExA creates a captioned top-level");
    CHECK(g_got_create, "WM_CREATE was delivered during creation");
    CHECK(GetDlgItem(wnd, ID_OK) != NULL, "GetDlgItem finds the child by id");

    RECT cr;
    GetClientRect(wnd, &cr);
    CHECK(cr.right == 320 - 2 * WEEN_NC_FRAME, "client width excludes the frame");
    CHECK(cr.bottom == 180 - 2 * WEEN_NC_FRAME - WEEN_NC_CAPTION,
          "client height excludes frame and caption");

    LRESULT hit = SendMessageA(wnd, WM_NCHITTEST, 0, MAKELPARAM(160, 10));
    CHECK(hit == HTCAPTION, "WM_NCHITTEST reports the caption strip");
    hit = SendMessageA(wnd, WM_NCHITTEST, 0, MAKELPARAM(160, 100));
    CHECK(hit == HTCLIENT, "WM_NCHITTEST reports the client area");

    UpdateWindow(wnd);
    const ween_surface *s = ween_headless_surface();
    CHECK(s != NULL, "the surface was presented");
    if (s) {
        /* A window frame is the plain EDGE_RAISED: COLOR_3DLIGHT (face) on the
         * outside, the white highlight one pixel in — as win32 draws it. */
        CHECK(s->px[0] == WEEN_3DLIGHT, "window frame starts with the 3DLIGHT edge");
        CHECK(s->px[(long)s->w + 1] == WEEN_WHITE,
              "the white highlight sits inside that edge");
        CHECK(s->px[(long)4 * s->w + 3] == WEEN_CAP_LEFT,
              "caption gradient starts at #0A246A");
        /* a pixel inside the button face (avoid the bevel and the label) */
        long bx = WEEN_NC_FRAME + BTN_X + 5;
        long by = WEEN_NC_FRAME + WEEN_NC_CAPTION + BTN_Y + 5;
        CHECK(s->px[by * s->w + bx] == WEEN_FACE, "button face is BTNFACE");
    }

    /* Pump: the scripted click presses the button, fires WM_COMMAND, destroys
     * the window and posts WM_QUIT. */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    CHECK(g_got_click, "BN_CLICKED arrived via WM_COMMAND");
    CHECK(msg.message == WM_QUIT, "the loop ended on WM_QUIT");

    /* SelectObject gives back what was really selected, so the save/restore
     * idiom round-trips instead of collapsing to the stock font. */
    {
        PAINTSTRUCT ps;
        HWND w = CreateWindowExA(0, "weentest", "gdi", WS_POPUP, 0, 0, 80, 40,
                                 NULL, NULL, NULL, NULL);
        HDC dc = w ? BeginPaint(w, &ps) : NULL;
        CHECK(dc != NULL, "a DC to select into");
        if (dc) {
            HFONT bold = CreateFontA(0, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0,
                                     0, "Tahoma");
            HFONT plain = CreateFontA(0, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0,
                                      0, 0, "Tahoma");
            HGDIOBJ first = SelectObject(dc, bold);
            CHECK(first != NULL, "selecting a font returns the previous one");
            HGDIOBJ second = SelectObject(dc, plain);
            CHECK(second == bold, "and that is the font actually selected");
            SelectObject(dc, second); /* the usual restore */
            CHECK(dc->font == bold->font, "restoring puts the bold font back");
            SelectObject(dc, first);
            CHECK(dc->font == w->font, "and unwinding reaches the DC's own");
            EndPaint(w, &ps);
            DeleteObject(bold);
            DeleteObject(plain);
        }
        if (w)
            DestroyWindow(w);
    }

    /* Nothing that used to be a fixed cap fails quietly any more. */
    {
        WNDCLASSA many;
        memset(&many, 0, sizeof(many));
        many.lpfnWndProc = DefWindowProcA;
        int registered = 0;
        for (int i = 0; i < 200; i++) { /* past the old table of 32 */
            char name[64];
            sprintf(name, "weenclass%d", i);
            many.lpszClassName = name;
            if (RegisterClassA(&many))
                registered++;
        }
        CHECK(registered == 200, "the class table grows past its old 32");

        /* a class name longer than the old 32-byte field, round-tripped */
        char longname[200];
        memset(longname, 'c', sizeof(longname) - 1);
        longname[sizeof(longname) - 1] = 0;
        many.lpszClassName = longname;
        CHECK(RegisterClassA(&many) != 0, "a long class name registers");
        HWND lw = CreateWindowExA(0, longname, "x", WS_POPUP, 0, 0, 40, 20,
                                  NULL, NULL, NULL, NULL);
        CHECK(lw != NULL, "and is found again in full, not truncated");
        if (lw)
            DestroyWindow(lw);
    }

    /* A cursor an application draws itself: the two masks win32 has always
     * taken, resolved into one picture with the transparent parts marked. */
    {
        /* 8x8: the top row white, the second black, the rest transparent */
        unsigned char and_bits[8] = { 0x00, 0x00, 0xFF, 0xFF,
                                      0xFF, 0xFF, 0xFF, 0xFF };
        unsigned char xor_bits[8] = { 0xFF, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00 };
        HCURSOR cur = CreateCursor(NULL, 3, 4, 8, 8, and_bits, xor_bits);
        CHECK(cur != NULL, "a cursor can be made out of two masks");
        const ween_cursor *c = ween_cursor_of(cur);
        CHECK(c != NULL, "and is told apart from a stock shape by its handle");
        CHECK(c && c->w == 8 && c->h == 8 && c->xhot == 3 && c->yhot == 4,
              "which remembers its size and its hot spot");
        CHECK(c && c->argb[0] == 0xFFFFFFFFu,
              "an AND bit clear and an XOR bit set is a white pixel");
        CHECK(c && c->argb[8] == 0xFF000000u, "both clear is a black one");
        CHECK(c && (c->argb[16] >> 24) == 0,
              "and AND set with XOR clear is nothing at all");
        CHECK(ween_cursor_of(LoadCursorA(NULL, IDC_ARROW)) == NULL,
              "a stock cursor is still a number, not one of those");
        CHECK(DestroyCursor(cur) == TRUE, "and it can be given back");
    }

    {
        /* The file calls a win32 program reads and writes with. Paint uses
         * these rather than the C library's so that its Windows build carries
         * no C runtime at all — the one a modern toolchain links is the UCRT,
         * and a machine of the age this library imitates cannot start a
         * program that asks for it. */
        char fpath[512];
        char back[16];
        DWORD n = 0;
        HANDLE f;
        snprintf(fpath, sizeof(fpath), "%s/api_file.bin", dir ? dir : ".");
        f = CreateFileA(fpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(f != INVALID_HANDLE_VALUE, "CreateFileA makes a file");
        CHECK(WriteFile(f, "ween32", 6, &n, NULL) && n == 6,
              "WriteFile says how much of it went");
        CHECK(CloseHandle(f), "and the handle is given back");

        f = CreateFileA(fpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(f != INVALID_HANDLE_VALUE, "and opened again for reading");
        CHECK(GetFileSize(f, NULL) == 6, "GetFileSize is what was written");
        CHECK(ReadFile(f, back, 6, &n, NULL) && n == 6 &&
                  !memcmp(back, "ween32", 6),
              "ReadFile gives it back");
        CHECK(SetFilePointer(f, 2, NULL, FILE_BEGIN) == 2,
              "SetFilePointer moves to where it says");
        CHECK(ReadFile(f, back, 4, &n, NULL) && n == 4 &&
                  !memcmp(back, "en32", 4),
              "and reading goes on from there");
        /* the end of the file is a short read, not a failure: the count is
         * what says so, which is the one thing about this call that catches
         * people out */
        CHECK(ReadFile(f, back, 4, &n, NULL) && n == 0,
              "reading past the end succeeds, with nothing in it");
        CHECK(CloseHandle(f), "and it closes");
        CHECK(CreateFileA("nothing/of/the/sort", GENERIC_READ, 0, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                          NULL) == INVALID_HANDLE_VALUE,
              "a file that is not there is INVALID_HANDLE_VALUE");
        remove(fpath);
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("api_test: all passed\n");
    return 0;
}
