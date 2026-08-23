/* The drawing half of GDI: a memory context with a bitmap in it, pens and
 * the shapes they draw, the raster operations, the flood fill, the device-
 * independent bitmap round trip — and the scroll bars a window wears itself.
 *
 * Several of the numbers checked here were measured off a Windows 2000
 * machine rather than reasoned about, and the comments say which: they are
 * the fidelity decisions, and a test is the only thing that keeps them from
 * being tidied away.
 */

#define _POSIX_C_SOURCE 200112L

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

/* A memory context with a bitmap of its own to draw on. */
static HDC scratch(int w, int h, HBITMAP *out)
{
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    SelectObject(dc, bmp);
    if (out)
        *out = bmp;
    return dc;
}

static void fill_white(HDC dc, int w, int h)
{
    RECT r = { 0, 0, w, h };
    FillRect(dc, &r, GetStockObject(WHITE_BRUSH));
}

static int black_count(HDC dc, int w, int h)
{
    int n = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (GetPixel(dc, x, y) == RGB(0, 0, 0))
                n++;
    return n;
}

/* One row of a stamp, as a string of '#' and '.', so a shape can be checked
 * against how it looks rather than against a pixel count. */
static const char *row_of(HDC dc, int x0, int y, int len, char *buf)
{
    for (int i = 0; i < len; i++)
        buf[i] = GetPixel(dc, x0 + i, y) == RGB(0, 0, 0) ? '#' : '.';
    buf[len] = 0;
    return buf;
}

static void test_memory_dc(void)
{
    HBITMAP bmp;
    HDC dc = scratch(16, 8, &bmp);
    BITMAP info;
    CHECK(dc != NULL && bmp != NULL, "a memory context takes a bitmap");
    CHECK(GetObjectA(bmp, sizeof info, &info) == (int)sizeof info &&
              info.bmWidth == 16 && info.bmHeight == 8,
          "and GetObject says how big it is");
    fill_white(dc, 16, 8);
    CHECK(GetPixel(dc, 0, 0) == RGB(255, 255, 255), "FillRect reaches it");
    SetPixel(dc, 3, 4, RGB(0, 0, 0));
    CHECK(GetPixel(dc, 3, 4) == RGB(0, 0, 0), "and so does SetPixel");
    DeleteDC(dc);
    DeleteObject(bmp);
}

static void test_rops(void)
{
    HBITMAP sb, db;
    HDC src = scratch(8, 8, &sb), dst = scratch(8, 8, &db);
    fill_white(src, 8, 8);
    SetPixel(src, 1, 1, RGB(0, 0, 0));
    fill_white(dst, 8, 8);

    BitBlt(dst, 0, 0, 8, 8, src, 0, 0, SRCCOPY);
    CHECK(GetPixel(dst, 1, 1) == RGB(0, 0, 0) &&
              GetPixel(dst, 2, 2) == RGB(255, 255, 255),
          "SRCCOPY copies the source");

    PatBlt(dst, 0, 0, 8, 8, DSTINVERT);
    CHECK(GetPixel(dst, 1, 1) == RGB(255, 255, 255) &&
              GetPixel(dst, 2, 2) == RGB(0, 0, 0),
          "DSTINVERT turns it inside out, with no source at all");

    fill_white(dst, 8, 8);
    BitBlt(dst, 0, 0, 8, 8, src, 0, 0, SRCINVERT);
    CHECK(GetPixel(dst, 1, 1) == RGB(255, 255, 255),
          "SRCINVERT is the exclusive or of the two");

    /* A blit with the extents negative mirrors, which is how Flip works.
     * The rectangle runs back from where it starts, so a mirror onto the
     * same size starts one past the right edge. */
    fill_white(dst, 8, 8);
    StretchBlt(dst, 8, 0, -8, 8, src, 0, 0, 8, 8, SRCCOPY);
    CHECK(GetPixel(dst, 6, 1) == RGB(0, 0, 0),
          "a negative width mirrors what is blitted");

    DeleteDC(src);
    DeleteDC(dst);
    DeleteObject(sb);
    DeleteObject(db);
}

static void test_pens(void)
{
    HBITMAP bmp;
    HDC dc = scratch(24, 24, &bmp);
    char buf[32];
    HPEN pen;

    /* The stamp a wide pen puts down, measured off a Windows 2000 Paint by
     * clicking its line tool at each width: two across is a full square,
     * three a plus, four and five squares with the corners taken off. */
    fill_white(dc, 24, 24);
    pen = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
    SelectObject(dc, pen);
    MoveToEx(dc, 10, 10, NULL);
    LineTo(dc, 10, 10);
    CHECK(strcmp(row_of(dc, 8, 9, 5, buf), "..#..") == 0 &&
              strcmp(row_of(dc, 8, 10, 5, buf), ".###.") == 0 &&
              strcmp(row_of(dc, 8, 11, 5, buf), "..#..") == 0,
          "a three-pixel pen is a plus, and a line of no length draws it");
    DeleteObject(pen);

    /* The extra pixel of an even width goes above and left. */
    fill_white(dc, 24, 24);
    pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    SelectObject(dc, pen);
    MoveToEx(dc, 4, 10, NULL);
    LineTo(dc, 20, 10);
    CHECK(GetPixel(dc, 10, 9) == RGB(0, 0, 0) &&
              GetPixel(dc, 10, 10) == RGB(0, 0, 0) &&
              GetPixel(dc, 10, 11) != RGB(0, 0, 0),
          "a two-pixel line covers the row above the one it is on");
    DeleteObject(pen);

    /* A one-pixel line leaves its last point to whatever comes next. */
    fill_white(dc, 24, 24);
    pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    SelectObject(dc, pen);
    MoveToEx(dc, 4, 4, NULL);
    LineTo(dc, 9, 4);
    CHECK(black_count(dc, 24, 24) == 5 && GetPixel(dc, 8, 4) == RGB(0, 0, 0) &&
              GetPixel(dc, 9, 4) != RGB(0, 0, 0),
          "a one-pixel line stops one short of where it was going");
    DeleteObject(pen);

    SelectObject(dc, GetStockObject(BLACK_PEN));
    DeleteDC(dc);
    DeleteObject(bmp);
}

static void test_shapes(void)
{
    HBITMAP bmp;
    HDC dc = scratch(32, 32, &bmp);
    fill_white(dc, 32, 32);
    SelectObject(dc, GetStockObject(BLACK_PEN));
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 4, 4, 14, 12);
    /* GDI's rectangle stops one short of its right and bottom edge. */
    CHECK(GetPixel(dc, 13, 4) == RGB(0, 0, 0) &&
              GetPixel(dc, 14, 4) != RGB(0, 0, 0) &&
              GetPixel(dc, 4, 11) == RGB(0, 0, 0) &&
              GetPixel(dc, 4, 12) != RGB(0, 0, 0),
          "a rectangle stops one short of its far corner");
    CHECK(GetPixel(dc, 8, 8) == RGB(255, 255, 255),
          "and a null brush leaves the middle alone");

    fill_white(dc, 32, 32);
    SelectObject(dc, GetStockObject(BLACK_BRUSH));
    Rectangle(dc, 4, 4, 14, 12);
    CHECK(GetPixel(dc, 8, 8) == RGB(0, 0, 0), "a black one fills it");

    fill_white(dc, 32, 32);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, 4, 4, 20, 20);
    CHECK(GetPixel(dc, 11, 4) == RGB(0, 0, 0) &&
              GetPixel(dc, 4, 4) != RGB(0, 0, 0),
          "an ellipse touches the middle of its box's edge, not the corner");

    DeleteDC(dc);
    DeleteObject(bmp);
}

static void test_flood(void)
{
    HBITMAP bmp;
    HDC dc = scratch(20, 20, &bmp);
    HBRUSH red = CreateSolidBrush(RGB(255, 0, 0));
    fill_white(dc, 20, 20);
    SelectObject(dc, GetStockObject(BLACK_PEN));
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 2, 2, 12, 12);
    SelectObject(dc, red);
    ExtFloodFill(dc, 6, 6, GetPixel(dc, 6, 6), FLOODFILLSURFACE);
    CHECK(GetPixel(dc, 6, 6) == RGB(255, 0, 0),
          "a flood fill fills where it starts");
    CHECK(GetPixel(dc, 16, 16) == RGB(255, 255, 255),
          "and does not leak past the outline");
    DeleteObject(red);
    DeleteDC(dc);
    DeleteObject(bmp);
}

static void test_dibits(void)
{
    HBITMAP bmp;
    HDC dc = scratch(4, 3, &bmp);
    unsigned char bits[3 * 12]; /* four 24-bit pixels is twelve bytes a row */
    BITMAPINFO info;
    fill_white(dc, 4, 3);
    SetPixel(dc, 0, 0, RGB(255, 0, 0));

    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = 4;
    info.bmiHeader.biHeight = 3; /* positive: the bottom row comes first */
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    CHECK(GetDIBits(dc, bmp, 0, 3, bits, &info, DIB_RGB_COLORS) == 3,
          "GetDIBits hands back every row asked for");
    /* the top-left pixel is red, and in a bottom-up DIB it is in the last row */
    CHECK(bits[2 * 12 + 0] == 0 && bits[2 * 12 + 1] == 0 &&
              bits[2 * 12 + 2] == 0xff,
          "bottom-up, with blue first, as a .bmp holds it");

    /* and back again */
    bits[2 * 12 + 0] = 0xff;
    bits[2 * 12 + 2] = 0;
    SetDIBits(dc, bmp, 0, 3, bits, &info, DIB_RGB_COLORS);
    CHECK(GetPixel(dc, 0, 0) == RGB(0, 0, 255), "SetDIBits puts them back");

    DeleteDC(dc);
    DeleteObject(bmp);
}

static void test_viewport(void)
{
    HBITMAP bmp;
    HDC dc = scratch(20, 20, &bmp);
    POINT prev;
    fill_white(dc, 20, 20);
    SetViewportOrgEx(dc, 5, 5, &prev);
    CHECK(prev.x == 0 && prev.y == 0, "the origin starts at the corner");
    SetPixel(dc, 1, 1, RGB(0, 0, 0));
    SetViewportOrgEx(dc, 0, 0, NULL);
    CHECK(GetPixel(dc, 6, 6) == RGB(0, 0, 0),
          "and moving it moves everything drawn through it");
    DeleteDC(dc);
    DeleteObject(bmp);
}

/* The bars a window wears itself: the client rectangle loses their width,
 * and SetScrollInfo clamps the position to the last full page. */
static LRESULT CALLBACK test_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(w, m, wp, lp);
}

static void test_window_scrollbars(void)
{
    WNDCLASSA wc;
    HWND win, plain;
    RECT with, without;
    SCROLLINFO si;
    int bar = GetSystemMetrics(SM_CXVSCROLL);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = test_proc;
    wc.lpszClassName = "ween32ScrollTest";
    RegisterClassA(&wc);

    plain = CreateWindowExA(0, "ween32ScrollTest", "plain",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 200,
                            NULL, NULL, NULL, NULL);
    win = CreateWindowExA(0, "ween32ScrollTest", "with bars",
                          WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_HSCROLL |
                              WS_VSCROLL,
                          0, 0, 300, 200, NULL, NULL, NULL, NULL);
    GetClientRect(plain, &without);
    GetClientRect(win, &with);
    CHECK(without.right - with.right == bar &&
              without.bottom - with.bottom == bar,
          "a window with scroll bars has that much less client area");

    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = 99;
    si.nPage = 20;
    si.nPos = 95;
    SetScrollInfo(win, SB_VERT, &si, FALSE);
    CHECK(GetScrollPos(win, SB_VERT) == 80,
          "and a position past the last full page is pulled back to it");

    si.fMask = SIF_ALL;
    CHECK(GetScrollInfo(win, SB_VERT, &si) && si.nMax == 99 && si.nPage == 20,
          "GetScrollInfo gives the range back");

    ShowScrollBar(win, SB_BOTH, FALSE);
    GetClientRect(win, &with);
    CHECK(with.right == without.right && with.bottom == without.bottom,
          "hiding them gives the client area back");

    DestroyWindow(win);
    DestroyWindow(plain);
}

/* DrawFocusRect, whose dots were measured against the machine while Paint's
 * page was being sized: the pattern falls on the coordinates of the window
 * being drawn in, and the corner where two of the four inverted strips
 * overlap comes out blank because inverting twice is not drawing. */
static void test_focus_rect(void)
{
    HBITMAP bmp;
    HDC dc = scratch(40, 20, &bmp);
    RECT all = {0, 0, 40, 20};
    HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
    /* a corner on an even sum, so that the pixel the top and the left both
     * want is one the pattern would otherwise have filled */
    RECT r = {2, 4, 20, 12};
    FillRect(dc, &all, white);
    DrawFocusRect(dc, &r);
    CHECK(GetPixel(dc, 2, 4) == RGB(255, 255, 255),
          "the corner two of a focus rectangle's sides share is left blank");
    CHECK(GetPixel(dc, 4, 4) == RGB(0, 0, 0) &&
              GetPixel(dc, 6, 4) == RGB(0, 0, 0),
          "its top is dotted on the even sum of its coordinates");
    CHECK(GetPixel(dc, 5, 4) == RGB(255, 255, 255) &&
              GetPixel(dc, 7, 4) == RGB(255, 255, 255),
          "and blank on the odd one");
    CHECK(GetPixel(dc, 2, 6) == RGB(0, 0, 0) &&
              GetPixel(dc, 2, 5) == RGB(255, 255, 255),
          "its left side keeps the same pattern going down");
    /* drawn twice, it is gone again: that is what makes a rubber band cheap */
    DrawFocusRect(dc, &r);
    CHECK(GetPixel(dc, 4, 4) == RGB(255, 255, 255),
          "drawn a second time it takes itself away");
    DeleteDC(dc);
    DeleteObject(bmp);
}

int main(void)
{
    setenv("WEEN32_HEADLESS", "1", 1);
    setenv("WEEN32_DPI", "96", 1);
    test_memory_dc();
    test_rops();
    test_pens();
    test_shapes();
    test_flood();
    test_dibits();
    test_viewport();
    test_window_scrollbars();
    test_focus_rect();
    printf(g_failures ? "%d failure(s)\n" : "draw_test: all passed\n",
           g_failures);
    return g_failures ? 1 : 0;
}
