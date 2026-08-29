/* The rich edit control, asked the things a program asks it -- and asked
 * them in the same words the EDIT is asked them in tests/edit_test.c.
 *
 * That is the point of this file as much as the checking is. The two
 * controls have different text underneath and the same behaviour on top, and
 * nothing but a test can keep the second half true: where a caret goes when
 * a line is shorter than the column it came from, what a backspace does to a
 * line break, whether Home is the line's start or the document's. Each of
 * those is written here as it is written there, so that a change to one
 * control that quietly disagrees with the other is a failure rather than a
 * surprise months later in WordPad.
 *
 * The things only a rich edit does are here too: the selection stated as a
 * CHARRANGE, text handed back by range, and the event mask -- which is the
 * one place the two are meant to differ, since a rich edit says nothing to
 * its parent until it is asked to.
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

static int g_change, g_update, g_maxtext, g_vscroll;

static LRESULT CALLBACK host_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND) {
        switch (HIWORD(wp)) {
        case EN_CHANGE: g_change++; break;
        case EN_UPDATE: g_update++; break;
        case EN_MAXTEXT: g_maxtext++; break;
        case EN_VSCROLL: g_vscroll++; break;
        default: break;
        }
    }
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

static int caret_of(HWND w)
{
    DWORD from = 0, to = 0;
    SendMessageA(w, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
    return (int)to;
}

int main(void)
{
    setenv("WEEN32_HEADLESS", "1", 1);
    setenv("WEEN32_DPI", "96", 1);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "weenrich";
    RegisterClassA(&wc);
    HWND host = CreateWindowExA(0, "weenrich", "host",
                                WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 400,
                                300, NULL, NULL, NULL, NULL);
    /* WordPad's own style word, from the machine: WS_VSCROLL, ES_MULTILINE,
     * ES_AUTOVSCROLL, ES_WANTRETURN, ES_NOHIDESEL, with WS_EX_CLIENTEDGE. */
    HWND re = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                                  ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
                              0, 0, 380, 200, host, (HMENU)(UINT_PTR)10, NULL,
                              NULL);
    CHECK(re != NULL, "a window of the rich edit's class");
    {
        HWND old = CreateWindowExA(0, RICHEDIT_CLASS10A, "", WS_CHILD, 0, 0, 10,
                                   10, host, NULL, NULL, NULL);
        CHECK(old != NULL,
              "and one of the name Rich Edit 1.0 answered to, which is the "
              "same control");
        if (old)
            DestroyWindow(old);
    }
    SetFocus(re);

    /* ---- its text is its own, and a program cannot tell ---- */

    SetWindowTextA(re, "one\r\ntwo\r\nthree");
    CHECK(strcmp(text_of(re), "one\r\ntwo\r\nthree") == 0,
          "what was set comes back through GetWindowText, which is a "
          "WM_GETTEXT the control answers itself");
    CHECK(GetWindowTextLengthA(re) == 15,
          "and its length is the document's, not the window's");
    /* WM_SETTEXT on its own, which is the message and not the call that
     * also writes the window's own field: what comes back proves the answer
     * is the control's storage rather than the window's. */
    SendMessageA(re, WM_SETTEXT, 0, (LPARAM) "its own");
    CHECK(strcmp(text_of(re), "its own") == 0 && GetWindowTextLengthA(re) == 7,
          "and the message alone is enough, the control keeping the text "
          "rather than the window");
    SetWindowTextA(re, "one\r\ntwo\r\nthree");

    /* ---- lines: the same questions edit_test.c asks the EDIT ---- */

    CHECK(SendMessageA(re, EM_GETLINECOUNT, 0, 0) == 3,
          "three lines, counted by the breaks between them");
    CHECK(SendMessageA(re, EM_LINEINDEX, 0, 0) == 0, "the first starts at nought");
    CHECK(SendMessageA(re, EM_LINEINDEX, 1, 0) == 5,
          "and the second past the first's break, both characters of it");
    CHECK(SendMessageA(re, EM_LINEINDEX, 2, 0) == 10, "and the third past that");
    CHECK(SendMessageA(re, EM_LINEFROMCHAR, 6, 0) == 1,
          "an offset says which line it is on");
    CHECK(SendMessageA(re, EM_LINELENGTH, 6, 0) == 3,
          "and the line it is on is three characters, break not counted");
    CHECK(SendMessageA(re, EM_LINELENGTH, 12, 0) == 5,
          "the last line has no break and is measured to the end");
    {
        char buf[32];
        *(WORD *)buf = (WORD)sizeof buf;
        int n = (int)SendMessageA(re, EM_GETLINE, 1, (LPARAM)buf);
        CHECK(n == 3 && memcmp(buf, "two", 3) == 0,
              "a line comes back by number, without its break");
    }

    /* The control draws from a line table of its own and answers these
     * messages from the shared line functions, so the two have to agree
     * about every line of a text with both kinds of break in it, an empty
     * line in the middle and one at the end. Moving down a line at a time is
     * what walks the table; EM_LINEFROMCHAR is what reads the functions. */
    {
        static const char mixed[] = "alpha\r\n\r\nbeta\ngamma\r\n";
        int lines, i, agreed = 1;
        SetWindowTextA(re, mixed);
        lines = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0);
        CHECK(lines == ween_text_line_count(mixed),
              "the control counts the lines of a mixed text as the shared "
              "line functions do");
        SendMessageA(re, EM_SETSEL, 0, 0);
        for (i = 0; i < lines; i++) {
            int at = caret_of(re);
            if ((int)SendMessageA(re, EM_LINEFROMCHAR, at, 0) != i)
                agreed = 0;
            if (at != ween_text_line_start(mixed, i))
                agreed = 0;
            key(re, VK_DOWN, 0, 0);
            key(re, VK_HOME, 0, 0);
        }
        CHECK(agreed,
              "and walking down it with the arrow lands on the same line "
              "starts the shared functions give");
    }

    /* ---- typing ---- */

    SetWindowTextA(re, "");
    typed(re, "hello");
    CHECK(strcmp(text_of(re), "hello") == 0, "typing puts the characters in");
    CHECK(caret_of(re) == 5, "and leaves the caret after them");
    SendMessageA(re, WM_CHAR, (WPARAM)'\r', 0);
    typed(re, "world");
    CHECK(strcmp(text_of(re), "hello\r\nworld") == 0,
          "Return is a line break, written as the two characters Windows "
          "writes");
    SendMessageA(re, WM_CHAR, (WPARAM)'\b', 0);
    CHECK(strcmp(text_of(re), "hello\r\nworl") == 0,
          "backspace takes one character");
    SendMessageA(re, EM_SETSEL, 7, 7);
    SendMessageA(re, WM_CHAR, (WPARAM)'\b', 0);
    CHECK(strcmp(text_of(re), "helloworl") == 0,
          "and a line break goes as one thing, not as two");

    /* ---- where the arrows go ---- */

    SetWindowTextA(re, "long line here\r\nshort\r\nlong line again");
    SendMessageA(re, EM_SETSEL, 12, 12); /* column 12 of the first line */
    key(re, VK_DOWN, 0, 0);
    CHECK(caret_of(re) == 21,
          "down a line stops at the end of a line too short for the column");
    key(re, VK_HOME, 0, 0);
    CHECK(caret_of(re) == 16, "Home is the start of the line");
    key(re, VK_END, 0, 0);
    CHECK(caret_of(re) == 21, "End is the end of it");
    key(re, VK_HOME, 0, 1);
    CHECK(caret_of(re) == 0, "Control and Home is the start of the document");
    key(re, VK_END, 0, 1);
    CHECK(caret_of(re) == 38, "Control and End the end of it");

    /* What happens to the column when the line under the caret is too short
     * for it is not measured on the machine yet -- Windows is said to
     * remember where the caret was aiming and neither control here does. So
     * what is checked is the thing this file exists for: that the two agree.
     * When it is measured, both change together or this fails. */
    {
        HWND ed = CreateWindowExA(0, "EDIT", "",
                                  WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 200,
                                  380, 80, host, (HMENU)(UINT_PTR)11, NULL,
                                  NULL);
        const char *both = "long line here\r\nshort\r\nlong line again";
        int a, b;
        SetWindowTextA(ed, both);
        SetWindowTextA(re, both);
        SendMessageA(ed, EM_SETSEL, 12, 12);
        SendMessageA(re, EM_SETSEL, 12, 12);
        key(ed, VK_DOWN, 0, 0);
        key(ed, VK_DOWN, 0, 0);
        key(re, VK_DOWN, 0, 0);
        key(re, VK_DOWN, 0, 0);
        {
            DWORD f = 0, t = 0;
            SendMessageA(ed, EM_GETSEL, (WPARAM)&f, (LPARAM)&t);
            a = (int)t;
        }
        b = caret_of(re);
        CHECK(a == b,
              "the two controls put the caret in the same place after a "
              "short line, whatever that place turns out to be");
        DestroyWindow(ed);
    }
    SetWindowTextA(re, "long line here\r\nshort\r\nlong line again");

    /* Shift keeps the anchor where it was; moving without it does not. */
    SendMessageA(re, EM_SETSEL, 5, 5);
    key(re, VK_RIGHT, 1, 0);
    key(re, VK_RIGHT, 1, 0);
    {
        DWORD from = 0, to = 0;
        SendMessageA(re, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == 5 && to == 7, "Shift and an arrow extends a selection");
    }
    key(re, VK_RIGHT, 0, 0);
    {
        DWORD from = 0, to = 0;
        SendMessageA(re, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        CHECK(from == to, "and an arrow without it drops one");
    }

    /* ---- the selection, in the rich edit's own terms ---- */

    SetWindowTextA(re, "the quick brown fox");
    {
        CHARRANGE cr;
        cr.cpMin = 4;
        cr.cpMax = 9;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cr, 0, sizeof cr);
        SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(cr.cpMin == 4 && cr.cpMax == 9,
              "a selection set with a CHARRANGE comes back as one");
        {
            char buf[64] = "";
            int n = (int)SendMessageA(re, EM_GETSELTEXT, 0, (LPARAM)buf);
            CHECK(n == 5 && strcmp(buf, "quick") == 0,
                  "and the text of it is what is between the two offsets");
        }
        cr.cpMin = 0;
        cr.cpMax = -1;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(cr.cpMax == 19, "a cpMax of -1 means the end of the document");
    }
    {
        TEXTRANGEA tr;
        char buf[64] = "";
        tr.chrg.cpMin = 10;
        tr.chrg.cpMax = 15;
        tr.lpstrText = buf;
        int n = (int)SendMessageA(re, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
        CHECK(n == 5 && strcmp(buf, "brown") == 0,
              "any range of the text can be asked for by its offsets");
    }
    SendMessageA(re, EM_SETSEL, 4, 9);
    SendMessageA(re, EM_REPLACESEL, TRUE, (LPARAM)"slow");
    CHECK(strcmp(text_of(re), "the slow brown fox") == 0,
          "replacing the selection puts the new text in its place");
    CHECK(caret_of(re) == 8, "with the caret after what was put in");

    /* ---- the event mask: silence until asked ---- */

    g_change = g_update = 0;
    typed(re, "!");
    CHECK(g_change == 0 && g_update == 0,
          "a rich edit tells its parent nothing about a change it was not "
          "asked about");
    {
        LRESULT was = SendMessageA(re, EM_SETEVENTMASK, 0,
                                   (LPARAM)(ENM_CHANGE | ENM_UPDATE));
        CHECK(was == ENM_NONE, "the mask it started with was none at all");
        CHECK(SendMessageA(re, EM_GETEVENTMASK, 0, 0) ==
                  (ENM_CHANGE | ENM_UPDATE),
              "and the one it was given is the one it keeps");
    }
    g_change = g_update = 0;
    typed(re, "?");
    CHECK(g_change == 1 && g_update == 1,
          "once asked, it says EN_UPDATE and EN_CHANGE for a change");

    /* ---- what has been edited, and taking it back ---- */

    SetWindowTextA(re, "start");
    CHECK(SendMessageA(re, EM_GETMODIFY, 0, 0) == 0,
          "text a program sets is not the user's change");
    typed(re, "!");
    CHECK(SendMessageA(re, EM_GETMODIFY, 0, 0) != 0,
          "and text the user types is");
    SendMessageA(re, EM_SETMODIFY, 0, 0);
    CHECK(SendMessageA(re, EM_GETMODIFY, 0, 0) == 0,
          "a program can say it has saved what there was");

    SetWindowTextA(re, "before");
    SendMessageA(re, EM_SETSEL, 6, 6);
    typed(re, "!");
    CHECK(SendMessageA(re, EM_CANUNDO, 0, 0) != 0, "the last change can be undone");
    SendMessageA(re, EM_UNDO, 0, 0);
    CHECK(strcmp(text_of(re), "before") == 0, "and undoing it puts back what was");
    SendMessageA(re, EM_UNDO, 0, 0);
    CHECK(strcmp(text_of(re), "before!") == 0,
          "and undoing again puts back the undo, as an edit control does");
    SendMessageA(re, EM_EMPTYUNDOBUFFER, 0, 0);
    CHECK(SendMessageA(re, EM_CANUNDO, 0, 0) == 0,
          "a program can throw the step away");

    /* ---- how much it will hold ---- */

    SetWindowTextA(re, "");
    SendMessageA(re, EM_EXLIMITTEXT, 0, 4);
    g_maxtext = 0;
    typed(re, "abcdef");
    CHECK(strcmp(text_of(re), "abcd") == 0,
          "EM_EXLIMITTEXT is the most it will hold, its number in lParam");
    CHECK(g_maxtext > 0, "and the parent hears EN_MAXTEXT when it is full");
    SendMessageA(re, EM_EXLIMITTEXT, 0, 0);

    /* ---- the vertical bar ---- */

    {
        char many[512];
        int i, n = 0;
        for (i = 0; i < 40; i++)
            n += sprintf(many + n, "line %d\r\n", i);
        SetWindowTextA(re, many);
        CHECK(SendMessageA(re, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
              "a document just set is looked at from its top");
        SendMessageA(re, EM_LINESCROLL, 0, 5);
        CHECK(SendMessageA(re, EM_GETFIRSTVISIBLELINE, 0, 0) == 5,
              "EM_LINESCROLL moves the view by lines");
        SendMessageA(re, EM_LINESCROLL, 0, -100);
        CHECK(SendMessageA(re, EM_GETFIRSTVISIBLELINE, 0, 0) == 0,
              "and cannot take it above the first line");

        /* The caret drags the view after it, which is what makes typing at
         * the bottom of a long document work at all. */
        SendMessageA(re, EM_SETSEL, (WPARAM)-1, -1);
        SendMessageA(re, EM_SCROLLCARET, 0, 0);
        CHECK(SendMessageA(re, EM_GETFIRSTVISIBLELINE, 0, 0) > 0,
              "and the caret at the end brings the view down to it");
    }

    /* ---- what the dialog manager is told, and what the bar does ---- */

    CHECK((SendMessageA(re, WM_GETDLGCODE, 0, 0) & DLGC_WANTMESSAGE) != 0,
          "a control made with ES_WANTRETURN asks for the Return key, which "
          "is how WordPad's editor makes a line inside a dialog");
    CHECK((SendMessageA(re, WM_GETDLGCODE, 0, 0) &
           (DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_HASSETSEL)) ==
              (DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_HASSETSEL),
          "and for the characters, the arrows, and to be told when it has a "
          "selection to show");

    {
        /* A click in the track moves a screenful less one line: the line
         * that was at the bottom is at the top afterwards, which is the rule
         * measured on the machine's own Notepad and the one the EDIT
         * follows. The click goes in the bar's column, below the thumb. */
        char many[512];
        int i, n = 0, rows, top;
        RECT cr;
        for (i = 0; i < 60; i++)
            n += sprintf(many + n, "row %d\r\n", i);
        SetWindowTextA(re, many);
        SendMessageA(re, EM_SETEVENTMASK, 0, ENM_SCROLL);
        GetClientRect(re, &cr);
        rows = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0);
        CHECK(rows == 61, "sixty lines and the empty one after the last break");
        g_vscroll = 0;
        SendMessageA(re, WM_LBUTTONDOWN, 0,
                     MAKELPARAM(cr.right - 4, cr.bottom / 2));
        SendMessageA(re, WM_LBUTTONUP, 0,
                     MAKELPARAM(cr.right - 4, cr.bottom / 2));
        top = (int)SendMessageA(re, EM_GETFIRSTVISIBLELINE, 0, 0);
        {
            const ween_strike *f = ween_gui_font();
            int line = f ? f->ascent - f->descent : 13;
            int page = (cr.bottom - cr.top - 2) / line;
            CHECK(top == page - 1,
                  "a click below the thumb moves a screenful less one line, "
                  "so the line that was at the bottom is at the top");
        }
        CHECK(g_vscroll > 0,
              "and the parent hears EN_VSCROLL, having asked for ENM_SCROLL");
        SendMessageA(re, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_UPDATE);
    }

    /* ---- ES_NOHIDESEL: the selection stays when the keyboard leaves ---- */

    {
        struct ween_wnd *w = (struct ween_wnd *)host;
        int shown = 0, y;
        SetWindowTextA(re, "highlight me");
        SendMessageA(re, EM_SETSEL, 0, 9);
        SetFocus(host); /* the keyboard goes elsewhere */
        InvalidateRect(re, NULL, TRUE);
        ween_flush_paint();
        for (y = 0; y < 40 && !shown; y++) {
            int x;
            for (x = 0; x < 200; x++)
                if (w->surface.px &&
                    (w->surface.px[(size_t)y * w->surface.w + x] & 0xffffff) ==
                        WEEN_CAP_LEFT) {
                    shown = 1;
                    break;
                }
        }
        CHECK(shown,
              "a control made with ES_NOHIDESEL keeps its selection drawn "
              "when the keyboard has gone -- which is what a modeless Find "
              "box needs of the window behind it");
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("richedit_test: all passed\n");
    return 0;
}
