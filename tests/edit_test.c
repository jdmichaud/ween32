/* The edit control, asked the things a program asks it.
 *
 * A text editor is a multiline EDIT with menus round it, and what it does to
 * the text it does through messages: where the selection is, what to put in
 * its place, which line the caret is on, whether anything has been edited,
 * and whether the last thing can be taken back. Each one is checked here
 * against what win32 answers, and the typing that goes with them -- a return
 * making a line, a backspace taking a line break as one thing, an arrow
 * keeping its column as it moves between lines.
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

static int g_maxtext;
static int g_vscroll;

static LRESULT CALLBACK host_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND && HIWORD(wp) == EN_MAXTEXT)
        g_maxtext++;
    if (msg == WM_COMMAND && HIWORD(wp) == EN_VSCROLL)
        g_vscroll++;
    return DefWindowProcA(wnd, msg, wp, lp);
}

/* A key, the way the backend posts one: Shift in bit 0, Control in bit 28. */
static void key(HWND w, int vk, int shift, int ctrl)
{
    SendMessageA(w, WM_KEYDOWN, (WPARAM)vk,
                 (LPARAM)((shift ? 1 : 0) | (ctrl ? (1L << 28) : 0)));
}

static void typed(HWND w, const char *text)
{
    for (const char *p = text; *p; p++)
        SendMessageA(w, WM_CHAR, (WPARAM)(unsigned char)*p, 0);
}

static const char *text_of(HWND w)
{
    static char buf[512];
    GetWindowTextA(w, buf, sizeof buf);
    return buf;
}

int main(void)
{
    setenv("WEEN32_HEADLESS", "1", 1);
    setenv("WEEN32_DPI", "96", 1);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenedit";
    RegisterClassA(&wc);
    HWND host = CreateWindowExA(0, "weenedit", "host",
                                WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 400,
                                300, NULL, NULL, NULL, NULL);
    HWND ed = CreateWindowExA(0, "EDIT", "",
                              WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0, 380,
                              260, host, (HMENU)(UINT_PTR)10, NULL, NULL);
    CHECK(ed != NULL, "a multiline field");
    SetFocus(ed);

    /* ---- what is in it, and where ---- */

    SetWindowTextA(ed, "one\r\ntwo\r\nthree");
    CHECK(SendMessageA(ed, EM_GETLINECOUNT, 0, 0) == 3,
          "three lines, counted by the breaks between them");
    CHECK(SendMessageA(ed, EM_LINEINDEX, 0, 0) == 0, "the first starts at nought");
    CHECK(SendMessageA(ed, EM_LINEINDEX, 1, 0) == 5,
          "and the second past the first's break, both characters of it");
    CHECK(SendMessageA(ed, EM_LINEINDEX, 2, 0) == 10, "and the third past that");
    CHECK(SendMessageA(ed, EM_LINEFROMCHAR, 6, 0) == 1,
          "an offset says which line it is on");
    CHECK(SendMessageA(ed, EM_LINELENGTH, 6, 0) == 3,
          "and the line it is on is three characters, break not counted");
    CHECK(SendMessageA(ed, EM_LINELENGTH, 12, 0) == 5,
          "the last line has no break and is measured to the end");

    {   /* EM_GETLINE: the room is a word at the front of the buffer, which
         * is the one place win32 puts it there rather than in a parameter */
        char buf[32];
        *(WORD *)buf = (WORD)sizeof buf;
        int n = (int)SendMessageA(ed, EM_GETLINE, 1, (LPARAM)buf);
        CHECK(n == 3 && memcmp(buf, "two", 3) == 0,
              "a line comes back by number, without its break");
        char small[4];
        *(WORD *)small = 2;
        n = (int)SendMessageA(ed, EM_GETLINE, 2, (LPARAM)small);
        CHECK(n == 2 && memcmp(small, "th", 2) == 0,
              "and only as much of it as the buffer said it had room for");
    }

    /* ---- the selection ---- */

    SendMessageA(ed, EM_SETSEL, 4, 7);
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == 4 && to == 7, "the selection comes back as it was set");
        LRESULT packed = SendMessageA(ed, EM_GETSEL, 0, 0);
        CHECK(LOWORD(packed) == 4 && HIWORD(packed) == 7,
              "and packed into the answer as well, for a caller with no room");
    }
    SendMessageA(ed, EM_SETSEL, 0, -1);
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == 0 && to == 15, "minus one means the end of the text");
    }

    /* ---- putting something in its place ---- */

    SetWindowTextA(ed, "one\r\ntwo");
    SendMessageA(ed, EM_SETSEL, 5, 8);
    SendMessageA(ed, EM_REPLACESEL, TRUE, (LPARAM) "TWO");
    CHECK(strcmp(text_of(ed), "one\r\nTWO") == 0,
          "what is picked out is what a replacement replaces");
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == 8 && to == 8, "and the caret is left after it");
    }

    /* ---- whether it has been edited ---- */

    SendMessageA(ed, EM_SETMODIFY, FALSE, 0);
    CHECK(SendMessageA(ed, EM_GETMODIFY, 0, 0) == 0,
          "a program can say it has been saved");
    typed(ed, "x");
    CHECK(SendMessageA(ed, EM_GETMODIFY, 0, 0) != 0,
          "and typing says otherwise again");
    SendMessageA(ed, EM_SETMODIFY, FALSE, 0);
    SendMessageA(ed, EM_SETSEL, 0, 1);
    SendMessageA(ed, EM_REPLACESEL, TRUE, (LPARAM) "O");
    CHECK(SendMessageA(ed, EM_GETMODIFY, 0, 0) != 0,
          "so does a replacement");

    /* ---- taking it back ---- */

    SetWindowTextA(ed, "before");
    SendMessageA(ed, EM_EMPTYUNDOBUFFER, 0, 0);
    CHECK(SendMessageA(ed, EM_CANUNDO, 0, 0) == 0,
          "nothing to undo in a field that has just been set");
    SendMessageA(ed, EM_SETSEL, 0, 6);
    SendMessageA(ed, EM_REPLACESEL, TRUE, (LPARAM) "after");
    CHECK(SendMessageA(ed, EM_CANUNDO, 0, 0) != 0, "a replacement can be undone");
    CHECK(SendMessageA(ed, EM_UNDO, 0, 0) != 0 &&
              strcmp(text_of(ed), "before") == 0,
          "and undoing it puts back what was there");
    CHECK(SendMessageA(ed, EM_UNDO, 0, 0) != 0 &&
              strcmp(text_of(ed), "after") == 0,
          "and undoing again puts it back the other way, as an edit does");

    /* ---- typing ---- */

    SetWindowTextA(ed, "");
    typed(ed, "ab");
    SendMessageA(ed, WM_CHAR, (WPARAM)'\r', 0);
    typed(ed, "cd");
    CHECK(strcmp(text_of(ed), "ab\r\ncd") == 0,
          "a return makes a line, written the way Windows writes one");
    CHECK(SendMessageA(ed, EM_GETLINECOUNT, 0, 0) == 2, "so there are two");
    SendMessageA(ed, WM_CHAR, (WPARAM)'\b', 0);
    SendMessageA(ed, WM_CHAR, (WPARAM)'\b', 0);
    SendMessageA(ed, WM_CHAR, (WPARAM)'\b', 0);
    CHECK(strcmp(text_of(ed), "ab") == 0,
          "and a backspace over a line break takes both its characters at once");

    /* ---- moving about ---- */

    SetWindowTextA(ed, "hello\r\nworld\r\nagain");
    SendMessageA(ed, EM_SETSEL, 3, 3); /* on the first line, three along */
    key(ed, VK_DOWN, 0, 0);
    CHECK(SendMessageA(ed, EM_LINEFROMCHAR, -1, 0) == 1 &&
              SendMessageA(ed, EM_LINEINDEX, -1, 0) == 7,
          "Down goes to the line below");
    {
        /* Landing on the character nearest the *pixel* the caret was
         * standing at, which in a proportional face is not the character
         * with the same number. "hel" is fourteen pixels wide and "wo" is
         * fifteen, so the caret lands after "wo" rather than after "wor".
         * The machine settles it -- see "What a text control does with the
         * column" in docs/testing.md. */
        const ween_strike *f = ween_gui_font();
        int x = ween_strike_pen(f, "hello", 3);
        int want = 0, bestd = 1 << 30, i;
        for (i = 0; i <= 5; i++) {
            int pen = ween_strike_pen(f, "world", i);
            int d = pen > x ? pen - x : x - pen;
            if (d < bestd) {
                bestd = d;
                want = i;
            }
        }
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK((int)from == 7 + want,
              "landing where the caret was standing, which is a pixel and "
              "not a count of characters");
    }
    key(ed, VK_UP, 0, 0);
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == 3, "and Up comes back to it");
    }

    /* And what an EDIT does *not* do, which is the difference between it and
     * the rich edit: it takes that pixel from where the caret is now rather
     * than from where the walk set out. Down through a line too short for
     * the caret's place leaves the walk at the short line's own end, and a
     * second Down carries that forward instead of the original. Both
     * numbers are the machine's own, read with tools/vm/ctlprobe.c: 21 and
     * 29, and Up twice comes out at 6 rather than back at 12. */
    SetWindowTextA(ed, "long line here\r\nshort\r\nlong line again");
    SendMessageA(ed, EM_SETSEL, 12, 12);
    key(ed, VK_DOWN, 0, 0);
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(to == 21, "Down through a short line stops at its end");
    }
    key(ed, VK_DOWN, 0, 0);
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(to == 29,
              "and the line after takes the short line's pixel, not the one "
              "the walk began at");
    }
    key(ed, VK_UP, 0, 0);
    key(ed, VK_UP, 0, 0);
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(to == 6,
              "so walking back up does not come out where it started, and "
              "the machine's does not either");
    }
    SetWindowTextA(ed, "hello\r\nworld\r\nagain");
    SendMessageA(ed, EM_SETSEL, 3, 3);
    key(ed, VK_END, 0, 0);
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == 5, "End is the end of the line, not of the text");
    }
    key(ed, VK_HOME, 0, 0);
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == 0, "and Home its start");
    }
    key(ed, VK_END, 0, 1); /* with Control */
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == 19, "Control and End is the end of the whole text");
    }
    /* Shift keeps the far end where it was, which is how a run is picked out
     * with the keyboard. */
    SendMessageA(ed, EM_SETSEL, 0, 0);
    key(ed, VK_DOWN, 1, 0);
    {
        DWORD from = 0, to = 0;
        SendMessageA(ed, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == 0 && to == 7, "Shift and Down picks out the line it left");
    }

    /* ---- how much it will hold ---- */

    SetWindowTextA(ed, "");
    SendMessageA(ed, EM_SETLIMITTEXT, 4, 0);
    g_maxtext = 0;
    typed(ed, "abcdef");
    CHECK(strcmp(text_of(ed), "abcd") == 0,
          "a field told what it will hold stops there");
    CHECK(g_maxtext > 0, "and says so, which is what a program listens for");
    SendMessageA(ed, EM_SETLIMITTEXT, 0, 0);
    typed(ed, "ef");
    CHECK(strcmp(text_of(ed), "abcdef") == 0, "and nought means no limit");

    /* ---- the text itself ---- */

    SetWindowTextA(ed, "handle");
    {
        const char *p = (const char *)SendMessageA(ed, EM_GETHANDLE, 0, 0);
        CHECK(p && strcmp(p, "handle") == 0,
              "the field hands over the text it is holding");
    }

    /* ---- scrolling ----
     *
     * A note longer than its window is what a text editor mostly is, so the
     * top visible line follows the caret, and a program can move it itself.
     * The field is made short on purpose: how many lines it shows depends on
     * the font, so what is checked here is the behaviour rather than a
     * number of rows. */

    /* Its own window, so that what is drawn where it is is its own and not
     * whichever of two overlapping fields painted last. */
    HWND host2 = CreateWindowExA(0, "weenedit", "host2",
                                 WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 160,
                                 80, NULL, NULL, NULL, NULL);
    HWND shortfield = CreateWindowExA(0, "EDIT", "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0,
                                 120, 40, host2, (HMENU)(UINT_PTR)12, NULL,
                                 NULL);
    SetFocus(shortfield);
    SetWindowTextA(shortfield, "l0\r\nl1\r\nl2\r\nl3\r\nl4\r\nl5\r\nl6\r\nl7\r\nl8\r\nl9");
    CHECK(SendMessageA(shortfield, EM_GETLINECOUNT, 0, 0) == 10, "ten lines to move about in");
    CHECK(SendMessageA(shortfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
          "a field starts at its first line");
    SendMessageA(shortfield, EM_SETSEL, 36, 36); /* on the last line */
    SendMessageA(shortfield, EM_SCROLLCARET, 0, 0);
    {
        /* Forty pixels of a thirteen-pixel line is three rows, so the last
         * line of ten is in view when the seventh is at the top -- far
         * enough to reach it and no further. */
        LRESULT top = SendMessageA(shortfield, EM_GETFIRSTVISIBLELINE, 0, 0);
        CHECK(top == 7, "and scrolls just far enough to keep the caret in view");
    }
    SendMessageA(shortfield, EM_SETSEL, 0, 0);
    SendMessageA(shortfield, EM_SCROLLCARET, 0, 0);
    CHECK(SendMessageA(shortfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
          "and back to the top when the caret goes there");
    CHECK(SendMessageA(shortfield, EM_LINESCROLL, 0, 3) == TRUE,
          "a program can scroll it itself");
    CHECK(SendMessageA(shortfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 3,
          "three lines down");
    SendMessageA(shortfield, EM_LINESCROLL, 0, -1);
    CHECK(SendMessageA(shortfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 2,
          "and one back up");
    SendMessageA(shortfield, EM_LINESCROLL, 0, -50);
    CHECK(SendMessageA(shortfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
          "past the top stops at the top");
    SendMessageA(shortfield, EM_LINESCROLL, 0, 500);
    CHECK(SendMessageA(shortfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 9,
          "and past the end at the last line");
    /* What is drawn is what is scrolled to: the top row of a field scrolled
     * five lines down has to be the same pixels as the top row of a field
     * holding the same text from line five on. Painting and the top visible
     * line agreeing is the whole of what scrolling is. */
    {
        static const char tail[] = "l5\r\nl6\r\nl7\r\nl8\r\nl9";
        struct ween_wnd *top = ween_top_level(shortfield);
        int ox, oy, w = 120, h = 13, differs = 0, ink = 0;
        ween_color *band = malloc((size_t)w * h * sizeof *band);
        ween_client_origin(shortfield, &ox, &oy);
        /* Neither capture has the caret in it: it blinks, and a bar that is
         * on in one render and off in the other is not a difference in what
         * was scrolled to. */
        SetFocus(ed);
        SendMessageA(shortfield, EM_LINESCROLL, 0, -500);
        SendMessageA(shortfield, EM_LINESCROLL, 0, 5);
        InvalidateRect(shortfield, NULL, TRUE);
        ween_flush_paint();
        for (int y = 0; band && y < h; y++)
            for (int x = 0; x < w; x++)
                band[y * w + x] = top->surface.px[(size_t)(oy + y) * top->surface.w + ox + x];
        SetWindowTextA(shortfield, tail);
        SendMessageA(shortfield, EM_SETSEL, 0, 0);
        InvalidateRect(shortfield, NULL, TRUE);
        ween_flush_paint();
        for (int y = 0; band && y < h; y++)
            for (int x = 0; x < w; x++) {
                ween_color c = top->surface.px[(size_t)(oy + y) * top->surface.w + ox + x];
                if ((c & 0xffffff) != (band[y * w + x] & 0xffffff))
                    differs++;
                if ((c & 0xffffff) != (WEEN_WHITE & 0xffffff))
                    ink++;
            }
        CHECK(ink > 0, "the top row of the field has text on it");
        CHECK(differs == 0, "and a field scrolled five lines down draws the same");
        free(band);
        SetFocus(shortfield);
    }

    /* New text is read from the top, however far down the last was. */
    SendMessageA(shortfield, EM_LINESCROLL, 0, 5);
    SetWindowTextA(shortfield, "fresh\r\ntext");
    CHECK(SendMessageA(shortfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
          "a field given new text is back at the top of it");

    /* A click on the top row of a scrolled field lands on the line shown
     * there, not on the first line of the text. */
    SetWindowTextA(shortfield, "l0\r\nl1\r\nl2\r\nl3\r\nl4\r\nl5\r\nl6\r\nl7\r\nl8\r\nl9");
    SendMessageA(shortfield, EM_LINESCROLL, 0, -500);
    SendMessageA(shortfield, EM_LINESCROLL, 0, 5);
    SendMessageA(shortfield, WM_LBUTTONDOWN, 0, MAKELPARAM(0, 2));
    SendMessageA(shortfield, WM_LBUTTONUP, 0, MAKELPARAM(0, 2));
    {
        DWORD from = 0, to = 0;
        SendMessageA(shortfield, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK((int)from == (int)SendMessageA(shortfield, EM_LINEINDEX, 5, 0),
              "a click on the top row lands on the line shown there");
    }

    /* Typing where it already is leaves the view alone. */
    SendMessageA(shortfield, EM_LINESCROLL, 0, -500);
    SendMessageA(shortfield, EM_SETSEL, 0, 0);
    typed(shortfield, "x");
    CHECK(SendMessageA(shortfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
          "typing on a line in view does not move it");

    /* ---- the bar an application asks for with WS_VSCROLL ----
     *
     * Ten lines showing in a field of twenty: the thumb is half the track and
     * the arrows step a line, which is the same bar the list box and the
     * views draw and the same hit-testing behind it. */

    HWND host3 = CreateWindowExA(0, "weenedit", "host3",
                                 WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 160,
                                 170, NULL, NULL, NULL, NULL);
    HWND barfield = CreateWindowExA(0, "EDIT", "",
                                    WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                        WS_VSCROLL,
                                    0, 0, 120, 130, host3,
                                    (HMENU)(UINT_PTR)13, NULL, NULL);
    {
        char many[256];
        int n = 0;
        for (int i = 0; i < 20; i++)
            n += sprintf(many + n, "line%d%s", i, i < 19 ? "\r\n" : "");
        SetWindowTextA(barfield, many);
    }
    SetFocus(barfield);
    CHECK(SendMessageA(barfield, EM_GETLINECOUNT, 0, 0) == 20,
          "twenty lines in a field showing ten");
    {
        /* Sixteen pixels of bar down the right-hand edge; its bottom arrow is
         * the last sixteen of a hundred and thirty. */
        int bar = 120 - 16;
        SendMessageA(barfield, EM_SETSEL, 0, 0);
        g_vscroll = 0;
        SendMessageA(barfield, WM_LBUTTONDOWN, 0, MAKELPARAM(bar + 8, 122));
        SendMessageA(barfield, WM_LBUTTONUP, 0, MAKELPARAM(bar + 8, 122));
        CHECK(SendMessageA(barfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 1,
              "the bar's down arrow steps one line");
        CHECK(g_vscroll == 1,
              "and the parent hears EN_VSCROLL, as it does on Windows");
        {
            DWORD from = 0, to = 0;
            SendMessageA(barfield, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
            CHECK(from == 0 && to == 0,
                  "and a click in the bar does not move the caret");
        }
        SendMessageA(barfield, WM_LBUTTONDOWN, 0, MAKELPARAM(bar + 8, 2));
        SendMessageA(barfield, WM_LBUTTONUP, 0, MAKELPARAM(bar + 8, 2));
        CHECK(SendMessageA(barfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
              "and its up arrow steps back");
        /* Below the thumb is a screenful less one line: the line that was at
         * the bottom is at the top afterwards. Ten showing, so nine. That is
         * the machine's number, read off its own Notepad -- 36 lines
         * showing and a track click moving 35. */
        SendMessageA(barfield, WM_LBUTTONDOWN, 0, MAKELPARAM(bar + 8, 100));
        SendMessageA(barfield, WM_LBUTTONUP, 0, MAKELPARAM(bar + 8, 100));
        CHECK(SendMessageA(barfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 9,
              "the track below the thumb is a screenful less one line");
        SendMessageA(barfield, WM_LBUTTONDOWN, 0, MAKELPARAM(bar + 8, 20));
        SendMessageA(barfield, WM_LBUTTONUP, 0, MAKELPARAM(bar + 8, 20));
        CHECK(SendMessageA(barfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
              "and back up the same way");
        /* And the thumb is dragged: to the foot of the track is the last line
         * that still shows a full page. */
        SendMessageA(barfield, EM_LINESCROLL, 0, -50);
        SendMessageA(barfield, WM_LBUTTONDOWN, 0, MAKELPARAM(bar + 8, 20));
        SendMessageA(barfield, WM_MOUSEMOVE, 0, MAKELPARAM(bar + 8, 120));
        CHECK(SendMessageA(barfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 10,
              "dragging the thumb down goes to the last full page");
        SendMessageA(barfield, WM_MOUSEMOVE, 0, MAKELPARAM(bar + 8, 16));
        CHECK(SendMessageA(barfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
              "and back up to the top");
        SendMessageA(barfield, WM_LBUTTONUP, 0, MAKELPARAM(bar + 8, 16));
    }
    g_vscroll = 0;
    SendMessageA(barfield, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), 0);
    CHECK(SendMessageA(barfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 3,
          "a notch of the wheel is three lines");
    CHECK(g_vscroll == 0, "which is not the bar, and says nothing");
    SendMessageA(barfield, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)WHEEL_DELTA), 0);
    CHECK(SendMessageA(barfield, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
          "and back the other way");

    /* A single-line field has no lines to move between, and hands the key
     * back so that a dialog can move the focus with it. */
    HWND one = CreateWindowExA(0, "EDIT", "single", WS_CHILD | WS_VISIBLE, 0,
                               0, 100, 20, host, (HMENU)(UINT_PTR)11, NULL,
                               NULL);
    SetFocus(one);
    SendMessageA(one, EM_SETSEL, 3, 3);
    SendMessageA(one, WM_CHAR, (WPARAM)'\r', 0);
    CHECK(strcmp(text_of(one), "single") == 0,
          "a return is not typing in a field of one line");
    CHECK(SendMessageA(one, EM_GETLINECOUNT, 0, 0) == 1,
          "which has one line whatever is in it");
    CHECK(SendMessageA(one, EM_LINESCROLL, 0, 1) == FALSE,
          "and nothing to scroll, which is what it answers");

    DestroyWindow(host3);
    DestroyWindow(host2);
    DestroyWindow(host);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("edit_test: all passed\n");
    return 0;
}
