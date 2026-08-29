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
static int g_focus_msgs = 0;

#define ID_FIELD 77

/* A window shaped like a real program's: it makes a field and hands it the
 * keyboard when it is told it has it, which is what Notepad does and what
 * every program with one control in it does. */
static LRESULT CALLBACK focus_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE, 0, 0, 100, 20, hwnd,
                      (HMENU)(UINT_PTR)ID_FIELD, NULL, NULL);
        return 0;
    case WM_SETFOCUS:
        g_focus_msgs++;
        SetFocus(GetDlgItem(hwnd, ID_FIELD));
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

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
    {
        WNDCLASSA fc;
        memset(&fc, 0, sizeof fc);
        fc.lpfnWndProc = focus_proc;
        fc.lpszClassName = "weenfocus";
        RegisterClassA(&fc);
    }

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

    {
        /* A tool window: the palette a drawing program floats over its
         * picture. Its caption is shorter than an ordinary window's, it has
         * the close box and neither of the others, and the client area it
         * leaves is what the difference says it should be. */
        HWND tool = CreateWindowExA(WS_EX_TOOLWINDOW, "weentest", "Fonts",
                                    WS_POPUP | WS_CAPTION | WS_SYSMENU, 20, 20,
                                    200, 60, NULL, NULL, NULL, NULL);
        HWND plain = CreateWindowExA(0, "weentest", "Ordinary",
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU |
                                         WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                                     20, 20, 200, 60, NULL, NULL, NULL, NULL);
        RECT tc, pc;
        CHECK(tool && plain, "a tool window and an ordinary one were made");
        GetClientRect(tool, &tc);
        GetClientRect(plain, &pc);
        CHECK(tc.bottom > pc.bottom,
              "the tool window's shorter caption leaves it more client area");
        CHECK(tc.bottom - pc.bottom == 3,
              "three pixels more, which is nineteen against sixteen");
        DestroyWindow(tool);
        DestroyWindow(plain);
    }

    {
        /* A palette is shown without being activated: the window that had
         * the keyboard keeps it, which is what lets a text box go on taking
         * what is typed while the bar floats over it. */
        HWND owner = GetActiveWindow();
        HWND palette = CreateWindowExA(WS_EX_TOOLWINDOW, "weentest", "Fonts",
                                       WS_POPUP | WS_CAPTION | WS_SYSMENU, 20,
                                       20, 200, 60, NULL, NULL, NULL, NULL);
        CHECK(GetActiveWindow() == owner,
              "a window made out of sight does not take the keyboard");
        ShowWindow(palette, SW_SHOWNA);
        CHECK(GetActiveWindow() == owner,
              "and showing it without activating leaves it where it was");
        ShowWindow(palette, SW_SHOW);
        CHECK(GetActiveWindow() == palette,
              "showing it the ordinary way does make it the active one");
        DestroyWindow(palette);
    }

    {
        /* And a window shown is a window told: the whole of a win32 program's
         * answer to WM_SETFOCUS is usually to put the keyboard where it
         * really wants it -- Notepad's is `SetFocus(hwndEdit)` -- and it is
         * never called if showing a window only moves a variable. Before
         * this, a program came up with the caret nowhere and what was typed
         * went into the void until something was clicked. */
        HWND shown = CreateWindowExA(0, "weenfocus", "shown",
                                     WS_OVERLAPPEDWINDOW, 0, 0, 200, 120, NULL,
                                     NULL, NULL, NULL);
        g_focus_msgs = 0;
        ShowWindow(shown, SW_SHOWDEFAULT);
        CHECK(g_focus_msgs == 1, "showing a window sends it WM_SETFOCUS");
        CHECK(GetFocus() == GetDlgItem(shown, ID_FIELD),
              "so a program answering it puts the keyboard in its own field");
        SendMessageA(GetFocus(), WM_CHAR, (WPARAM)'x', 0);
        {
            char what[16] = "";
            GetWindowTextA(GetDlgItem(shown, ID_FIELD), what, sizeof what);
            CHECK(strcmp(what, "x") == 0,
                  "and typing lands in it without anything being clicked");
        }
        DestroyWindow(shown);
    }

    {
        /* CreateFont is asked for a face, a size and a weight, and all three
         * are answered: MS Sans Serif is a different set of glyphs from
         * Tahoma and measures wider, and bold is wider still. */
        HWND paper = CreateWindowExA(0, "weentest", "paper",
                                     WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, NULL,
                                     NULL, NULL, NULL);
        HDC dc = GetDC(paper);
        HFONT tahoma = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0,
                                   0, 0, "Tahoma");
        HFONT sans = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0,
                                 0, "MS Sans Serif");
        HFONT bold = CreateFontA(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0,
                                 "Tahoma");
        SIZE a, b, c;
        HGDIOBJ was = SelectObject(dc, tahoma);
        GetTextExtentPoint32A(dc, "Western", 7, &a);
        SelectObject(dc, sans);
        GetTextExtentPoint32A(dc, "Western", 7, &b);
        SelectObject(dc, bold);
        GetTextExtentPoint32A(dc, "Western", 7, &c);
        SelectObject(dc, was);
        CHECK(a.cx > 0 && b.cx > 0, "both faces measured something");
        CHECK(a.cx != b.cx, "a face asked for by name is not the other one");
        CHECK(c.cx > a.cx, "and the bold cut of one is wider than the plain");
        DeleteObject(tahoma);
        DeleteObject(sans);
        DeleteObject(bold);
        ReleaseDC(paper, dc);
        DestroyWindow(paper);
    }

    /* ---- a font described, and what it comes out as ---- */
    {
        HWND paper = CreateWindowExA(0, "weentest", "paper", WS_POPUP, 0, 0, 200,
                                     100, NULL, NULL, NULL, NULL);
        HDC dc = GetDC(paper);
        LOGFONTA lf;
        TEXTMETRICA tm;
        HFONT f, was;
        memset(&lf, 0, sizeof lf);
        lf.lfHeight = 16;
        lf.lfWeight = FW_BOLD;
        lf.lfItalic = 1;
        strcpy(lf.lfFaceName, "Tahoma");
        f = CreateFontIndirectA(&lf);
        CHECK(f != NULL, "a font described in a LOGFONT is made");
        was = SelectObject(dc, f);
        CHECK(GetTextMetricsA(dc, &tm), "and measured once it is selected");
        CHECK(tm.tmHeight == tm.tmAscent + tm.tmDescent,
              "its height is its ascent and its descent");
        CHECK(tm.tmAscent > 0 && tm.tmDescent > 0, "both of which it has");
        CHECK(tm.tmWeight == FW_BOLD, "the weight it was asked for");
        CHECK(tm.tmItalic == 1, "and the slant");
        CHECK(tm.tmAveCharWidth > 0 && tm.tmMaxCharWidth >= tm.tmAveCharWidth,
              "an average character is narrower than the widest one");
        SelectObject(dc, was);
        DeleteObject(f);

        /* The device: what a program turns a point size into a height with. */
        CHECK(GetDeviceCaps(dc, LOGPIXELSY) == 96,
              "the screen's dots per inch, which is what this test set");
        CHECK(GetDeviceCaps(dc, LOGPIXELSX) == GetDeviceCaps(dc, LOGPIXELSY),
              "the same both ways, the pixels being square");
        CHECK(GetDeviceCaps(dc, HORZRES) == GetSystemMetrics(SM_CXSCREEN),
              "the screen's width in pixels is the screen's width");
        CHECK(GetDeviceCaps(dc, BITSPIXEL) == 32, "and its colour depth");
        CHECK(GetDeviceCaps(dc, 4242) == 0, "something it does not know is 0");
        ReleaseDC(paper, dc);
        DestroyWindow(paper);
    }

    /* ---- a class background named by colour rather than by brush ---- */
    {
        /* `(HBRUSH)(COLOR_WINDOW + 1)` is how a program has spelled "the
         * window colour" since 1993: a small number, not a brush to read
         * through. */
        WNDCLASSA wc;
        HWND w;
        struct ween_wnd *tw;
        DWORD want = GetSysColor(COLOR_WINDOW);
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = DefWindowProcA;
        wc.lpszClassName = "weencolorback";
        wc.hbrBackground = (HBRUSH)(UINT_PTR)(COLOR_WINDOW + 1);
        RegisterClassA(&wc);
        w = CreateWindowExA(0, "weencolorback", "back",
                            WS_POPUP | WS_VISIBLE, 0, 0, 60, 40, NULL, NULL,
                            NULL, NULL);
        CHECK(w != NULL, "a window of a class whose background is a colour");
        InvalidateRect(w, NULL, TRUE);
        ween_flush_paint();
        tw = ween_top_level(w);
        CHECK(tw && (tw->surface.px[0] & 0xffffff) ==
                        (((want & 0xff) << 16) | (want & 0xff00) |
                         ((want >> 16) & 0xff)),
              "is filled with that colour rather than crashing on the number");
        DestroyWindow(w);
    }

    /* ---- a window that lets the system choose its size ---- */
    {
        HWND w = CreateWindowExA(0, "weentest", "default",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 NULL, NULL, NULL, NULL);
        RECT r;
        CHECK(w != NULL, "a window created with CW_USEDEFAULT is made at all");
        GetClientRect(w, &r);
        CHECK(r.right > 0 && r.bottom > 0, "and has a size somebody chose");
        CHECK(r.right < GetSystemMetrics(SM_CXSCREEN),
              "smaller than the screen, as a default window is");
        DestroyWindow(w);
    }

    /* ---- odds a program asks about itself ---- */
    {
        HWND w = CreateWindowExA(0, "weentest", "state", WS_POPUP | WS_VISIBLE,
                                 0, 0, 100, 50, NULL, NULL, NULL, NULL);
        SetWindowTextA(w, "twelve chars");
        CHECK(GetWindowTextLengthA(w) == 12, "the length of a window's text");
        CHECK(IsIconic(w) == FALSE, "a window is not an icon: nothing minimises");
        CHECK(IsZoomed(w) == FALSE, "nor is it maximised until it is");
        CHECK(GetModuleHandleA(NULL) != NULL, "the program has a handle");
        CHECK(GetModuleHandleA(NULL) == GetModuleHandleA(NULL),
              "and it is the same one every time");
        CHECK(GetModuleHandleA("other.dll") == NULL,
              "and there are no other modules to ask for");
        DestroyWindow(w);
    }

    /* ---- a message named rather than numbered ---- */
    {
        UINT a = RegisterWindowMessageA("commdlg_FindReplace");
        UINT b = RegisterWindowMessageA("commdlg_FindReplace");
        UINT c = RegisterWindowMessageA("something else entirely");
        CHECK(a >= 0xC000, "a registered message is above the WM_ numbers");
        CHECK(a == b, "the same name is the same message");
        CHECK(c != a, "and a different name a different one");
    }

    /* ---- a group box frames itself, not the damage ---- */
    {
        WNDCLASSA wc;
        HWND host, gb;
        struct ween_wnd *tw;
        RECT half;
        int x, full = 0, after = 0, spurious = 0;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = DefWindowProcA;
        wc.lpszClassName = "weengbdamage";
        wc.hbrBackground = (HBRUSH)(UINT_PTR)(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
        host = CreateWindowExA(0, "weengbdamage", "h", WS_POPUP | WS_VISIBLE,
                               0, 0, 240, 140, NULL, NULL, NULL, NULL);
        gb = CreateWindowExA(0, "BUTTON", "G",
                             WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 20, 20, 200,
                             100, host, NULL, NULL, NULL);
        CHECK(host && gb, "a group box on a face-coloured window");
        InvalidateRect(host, NULL, TRUE);
        ween_flush_paint();
        tw = ween_top_level(host);
        if (tw)
            for (x = 200; x < 230; x++)
                if ((tw->surface.px[70 * tw->surface.w + x] & 0xffffff) ==
                    0x808080)
                    full = x;
        CHECK(full == 218, "its frame's right line stands one in from its edge");

        /* Sixty columns of it. BeginPaint narrows rcPaint to the damage, so a
         * group box that framed *that* draws a line down its own middle --
         * which is what this one did until gb_paint asked for its client
         * rectangle. No capture could see it: every one of ours is a full
         * repaint, where the two rectangles are equal. */
        half.left = 0;
        half.top = 0;
        half.right = 60;
        half.bottom = 100;
        InvalidateRect(gb, &half, TRUE);
        ween_flush_paint();
        if (tw) {
            for (x = 70; x < 90; x++)
                if ((tw->surface.px[70 * tw->surface.w + x] & 0xffffff) ==
                    0x808080)
                    spurious = x;
            for (x = 200; x < 230; x++)
                if ((tw->surface.px[70 * tw->surface.w + x] & 0xffffff) ==
                    0x808080)
                    after = x;
        }
        CHECK(spurious == 0,
              "and repainting part of it draws no frame around that part");
        if (spurious)
            printf("     a frame line appeared at x=%d\n", spurious);
        CHECK(after == 218, "its own right line being where it was");
        DestroyWindow(host);
    }

    /* ---- z-order: what ween32 can promise ---- */
    {
        /* Before this existed a program could not put a window in front:
         * SetWindowPos took its `after` argument and dropped it, and there
         * was no GetWindow or GetTopWindow to ask with. The order below is
         * ween32's own and is exact; what the screen shows is the window
         * manager's, and the backend is asked rather than obeyed. */
        WNDCLASSA wc;
        HWND a, b;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = DefWindowProcA;
        wc.lpszClassName = "weenzorder";
        wc.hbrBackground = (HBRUSH)(UINT_PTR)(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
        a = CreateWindowExA(0, "weenzorder", "a", WS_POPUP | WS_VISIBLE, 0, 0,
                            80, 60, NULL, NULL, NULL, NULL);
        b = CreateWindowExA(0, "weenzorder", "b", WS_POPUP | WS_VISIBLE, 20,
                            20, 80, 60, NULL, NULL, NULL, NULL);
        CHECK(a && b, "two top-level windows");
        CHECK(GetTopWindow(NULL) == b, "the newer one starts in front");
        CHECK(GetWindow(b, GW_HWNDNEXT) == a, "and the older is behind it");
        CHECK(GetWindow(a, GW_HWNDNEXT) == NULL, "with nothing behind that");

        SetWindowPos(a, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        CHECK(GetTopWindow(NULL) == a, "SetWindowPos honours HWND_TOP");
        CHECK(GetWindow(a, GW_HWNDNEXT) == b, "with the other behind it");

        /* The backend was asked, which is separate from ween32's own order
         * having moved -- on X11 the first can happen without the second. */
        CHECK(ween_headless_window_raised(ween_top_level(a)->backend_win) >
                  ween_headless_window_raised(ween_top_level(b)->backend_win),
              "and the window system was asked for it last");

        SetWindowPos(b, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        CHECK(GetTopWindow(NULL) == a,
              "and SWP_NOZORDER leaves the order alone, which it could not "
              "have meant before");
        DestroyWindow(a);
        DestroyWindow(b);
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("api_test: all passed\n");
    return 0;
}
