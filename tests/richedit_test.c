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

static void face_copy_test(char *dst, const char *src)
{
    int i = 0;
    while (src[i] && i < LF_FACESIZE - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}


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
static int g_selchange, g_sel_from, g_sel_to, g_seltyp;

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
    if (msg == WM_NOTIFY) {
        const NMHDR *nm = (const NMHDR *)lp;
        if (nm && nm->code == EN_SELCHANGE) {
            const SELCHANGE *sc = (const SELCHANGE *)lp;
            g_selchange++;
            g_sel_from = (int)sc->chrg.cpMin;
            g_sel_to = (int)sc->chrg.cpMax;
            g_seltyp = sc->seltyp;
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

/* The two ends of a stream, as a program writes them. */
static char g_stream[8192];
static int g_stream_len, g_stream_at;

static DWORD CALLBACK stream_out(DWORD_PTR cookie, LPBYTE bytes, LONG cb,
                                 LONG *written)
{
    LONG i;
    (void)cookie;
    for (i = 0; i < cb && g_stream_len < (int)sizeof g_stream - 1; i++)
        g_stream[g_stream_len++] = (char)bytes[i];
    g_stream[g_stream_len] = 0;
    *written = i;
    return 0;
}

static DWORD CALLBACK stream_in(DWORD_PTR cookie, LPBYTE bytes, LONG cb,
                                LONG *read)
{
    LONG i = 0;
    (void)cookie;
    while (i < cb && g_stream_at < g_stream_len)
        bytes[i++] = (BYTE)g_stream[g_stream_at++];
    *read = i;
    return 0;
}

static const char *streamed_out(HWND w, WPARAM how)
{
    EDITSTREAM es;
    memset(&es, 0, sizeof es);
    es.pfnCallback = stream_out;
    g_stream_len = 0;
    g_stream[0] = 0;
    SendMessageA(w, EM_STREAMOUT, how, (LPARAM)&es);
    return g_stream;
}

static void stream_into(HWND w, WPARAM how, const char *text)
{
    EDITSTREAM es;
    memset(&es, 0, sizeof es);
    es.pfnCallback = stream_in;
    g_stream_len = (int)strlen(text);
    memcpy(g_stream, text, (size_t)g_stream_len + 1);
    g_stream_at = 0;
    SendMessageA(w, EM_STREAMIN, how, (LPARAM)&es);
}

static int caret_of(HWND w)
{
    DWORD from = 0, to = 0;
    SendMessageA(w, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
    return (int)to;
}

/* The caret's height, off the surface, **inside the control and nowhere
 * else**. The headless surface is the whole screen and the windows earlier
 * tests made are still standing on it, so a scan of all of it finds their
 * borders: the first draft measured 215 and 96 for two cases that differ by
 * three, which is a number with nothing to do with the question. */
/* How much of the control's right-hand strip is scrollbar. The trough is a
 * dither, so half its pixels are white -- counting "non-white columns" finds
 * nothing, which is what the first attempt did. Face-coloured pixels in the
 * strip is the count that separates a bar from a blank client. */
static int bar_pixels(HWND ctl)
{
    RECT c;
    POINT o = {0, 0};
    InvalidateRect(ctl, NULL, TRUE);
    ween_flush_paint();
    const ween_surface *s = ween_headless_surface();
    int n = 0, w = ween_scroll_metric();
    if (!s || !GetClientRect(ctl, &c) || !ClientToScreen(ctl, &o))
        return 0;
    for (int x = o.x + c.right - w; x < o.x + c.right && x < s->w; x++)
        for (int y = o.y; y < o.y + c.bottom && y < s->h; y++)
            if ((s->px[(size_t)y * s->w + x] & 0xffffff) ==
                WEEN_RGBX(212, 208, 200))
                n++;
    return n;
}

static int caret_rows(HWND ctl, HWND host)
{
    RECT c;
    POINT o = {0, 0};
    InvalidateRect(host, NULL, TRUE);
    ween_flush_paint();
    const ween_surface *s = ween_headless_surface();
    if (!s || !GetClientRect(ctl, &c) || !ClientToScreen(ctl, &o))
        return 0;
    int best = 0;
    for (int x = o.x; x < o.x + c.right && x < s->w; x++) {
        int run = 0;
        for (int y = o.y; y < o.y + c.bottom && y < s->h; y++) {
            if ((s->px[(size_t)y * s->w + x] & 0xffffff) == WEEN_RGBX(0, 0, 0))
                run++;
            else {
                if (run > best)
                    best = run;
                run = 0;
            }
        }
        if (run > best)
            best = run;
    }
    return best;
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

    /* Every offset below is in the control's own numbering, where a
     * paragraph mark is one carriage return -- so the second line of
     * "one\r\ntwo\r\nthree" begins at 4 and not at 5. The machine says the
     * same: "one\r\ntwo\r\n" is three lines and the last begins at 8. */
    CHECK(SendMessageA(re, EM_GETLINECOUNT, 0, 0) == 3,
          "three lines, counted by the marks between them");
    CHECK(SendMessageA(re, EM_LINEINDEX, 0, 0) == 0, "the first starts at nought");
    CHECK(SendMessageA(re, EM_LINEINDEX, 1, 0) == 4,
          "and the second one past the first's mark, which is one character");
    CHECK(SendMessageA(re, EM_LINEINDEX, 2, 0) == 8, "and the third past that");
    CHECK(SendMessageA(re, EM_LINEFROMCHAR, 5, 0) == 1,
          "an offset says which line it is on");
    CHECK(SendMessageA(re, EM_LINELENGTH, 5, 0) == 3,
          "and the line it is on is three characters, mark not counted");
    CHECK(SendMessageA(re, EM_LINELENGTH, 9, 0) == 5,
          "the last line has no mark and is measured to the end");
    {
        char buf[32];
        *(WORD *)buf = (WORD)sizeof buf;
        int n = (int)SendMessageA(re, EM_GETLINE, 1, (LPARAM)buf);
        CHECK(n == 3 && memcmp(buf, "two", 3) == 0,
              "a line comes back by number, without its mark");
    }
    {
        /* And the machine's own case, offsets and all. */
        SetWindowTextA(re, "one\r\ntwo\r\n");
        CHECK(SendMessageA(re, EM_GETLINECOUNT, 0, 0) == 3 &&
                  SendMessageA(re, EM_LINEINDEX, 2, 0) == 8,
              "a text ending in a mark has an empty line after it, beginning "
              "where the machine says: 8");
        CHECK(GetWindowTextLengthA(re) == 10,
              "while the length a program is told is the one it handed in, "
              "with both characters of every mark");
        {
            char back[32] = "";
            GetWindowTextA(re, back, sizeof back);
            CHECK(strcmp(back, "one\r\ntwo\r\n") == 0,
                  "and so is the text, however the control keeps it");
        }
        {
            char sel[32] = "";
            CHARRANGE cr;
            int n;
            cr.cpMin = 2;
            cr.cpMax = 6;
            SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
            n = (int)SendMessageA(re, EM_GETSELTEXT, 0, (LPARAM)sel);
            CHECK(n == 4 && memcmp(sel, "e\rtw", 4) == 0,
                  "a range handed out is the control's own bytes: four for "
                  "2..6, with one carriage return in them -- which is what "
                  "the machine answers");
        }
        SetWindowTextA(re, "one\r\ntwo\r\nthree");
    }

    /* The line table is the control's own answer to where a line begins,
     * and the shared ween_text_line_* are not it: those count a CRLF as one
     * break and this control keeps a single carriage return, so the two
     * disagree by one per mark and only one of them is riched20's. Walking
     * down the lines with the arrow has to land on the table's own starts. */
    {
        static const char mixed[] = "alpha\r\n\r\nbeta\ngamma\r\n";
        static const int starts[] = { 0, 6, 7, 12, 18 };
        int lines, i, agreed = 1;
        SetWindowTextA(re, mixed);
        lines = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0);
        CHECK(lines == 5,
              "a text with both kinds of break, an empty line in the middle "
              "and one at the end is five lines");
        SendMessageA(re, EM_SETSEL, 0, 0);
        for (i = 0; i < lines; i++) {
            int at = caret_of(re);
            if ((int)SendMessageA(re, EM_LINEFROMCHAR, at, 0) != i)
                agreed = 0;
            if (at != starts[i])
                agreed = 0;
            key(re, VK_DOWN, 0, 0);
            key(re, VK_HOME, 0, 0);
        }
        CHECK(agreed,
              "and walking down it with the arrow lands on each line's start "
              "in the control's own numbering, a bare line feed counting as "
              "a mark like a CRLF");
    }

    /* ---- typing ---- */

    SetWindowTextA(re, "");
    typed(re, "hello");
    CHECK(strcmp(text_of(re), "hello") == 0, "typing puts the characters in");
    CHECK(caret_of(re) == 5, "and leaves the caret after them");
    SendMessageA(re, WM_CHAR, (WPARAM)'\r', 0);
    typed(re, "world");
    CHECK(strcmp(text_of(re), "hello\r\nworld") == 0,
          "Return makes a paragraph, which comes back out as the two "
          "characters a program expects");
    CHECK(SendMessageA(re, EM_LINEINDEX, 1, 0) == 6,
          "and is one character where the control counts: the second line "
          "begins at 6, not 7");
    SendMessageA(re, WM_CHAR, (WPARAM)'\b', 0);
    CHECK(strcmp(text_of(re), "hello\r\nworl") == 0,
          "backspace takes one character");
    SendMessageA(re, EM_SETSEL, 6, 6); /* just after the mark */
    SendMessageA(re, WM_CHAR, (WPARAM)'\b', 0);
    CHECK(strcmp(text_of(re), "helloworl") == 0,
          "and one backspace takes the mark, there being one of it");

    /* ---- where the arrows go ---- */

    /* Lines at 0, 15 and 21 in the control's numbering: fourteen characters
     * and a mark, five and a mark, fifteen. */
    SetWindowTextA(re, "long line here\r\nshort\r\nlong line again");
    SendMessageA(re, EM_SETSEL, 12, 12); /* twelve characters into line one */
    key(re, VK_DOWN, 0, 0);
    CHECK(caret_of(re) == 20,
          "down a line stops at the end of a line too short to reach the "
          "place the caret set out from");
    key(re, VK_HOME, 0, 0);
    CHECK(caret_of(re) == 15, "Home is the start of the line");
    key(re, VK_END, 0, 0);
    CHECK(caret_of(re) == 20, "End is the end of it");
    key(re, VK_HOME, 0, 1);
    CHECK(caret_of(re) == 0, "Control and Home is the start of the document");
    key(re, VK_END, 0, 1);
    CHECK(caret_of(re) == 36, "Control and End the end of it");

    /* Where a walk up and down the lines comes out, which is the one
     * behaviour the two controls are *meant* to differ in -- and the
     * difference is the machine's, read with tools/vm/ctlprobe.c rather
     * than reasoned about. Its rich edit walks down from twelve characters
     * into a long line, through a five-character line, and comes out at the
     * pixel it set out from; two presses of Up put the caret back on the
     * very character it left. Its EDIT takes the pixel from wherever the
     * caret is now, so the same walk ends somewhere else and never comes
     * back. Both are implemented, and both are checked here.
     *
     * The landing offsets themselves are the font's -- the machine's are
     * Tahoma's, ours are this strike's -- so what is asserted is the
     * property rather than the number: back where it started, or not. */
    {
        HWND ed = CreateWindowExA(0, "EDIT", "",
                                  WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 200,
                                  380, 80, host, (HMENU)(UINT_PTR)11, NULL,
                                  NULL);
        const char *both = "long line here\r\nshort\r\nlong line again";
        int start = 12, back_r, back_e, deep_r, deep_e;
        SetWindowTextA(ed, both);
        SetWindowTextA(re, both);
        SendMessageA(ed, EM_SETSEL, start, start);
        SendMessageA(re, EM_SETSEL, start, start);
        key(ed, VK_DOWN, 0, 0);
        key(re, VK_DOWN, 0, 0);
        key(ed, VK_DOWN, 0, 0);
        key(re, VK_DOWN, 0, 0);
        deep_r = caret_of(re);
        {
            DWORD f = 0, t = 0;
            SendMessageA(ed, EM_GETSEL, (WPARAM)&f, (LPARAM)&t);
            deep_e = (int)t;
        }
        key(ed, VK_UP, 0, 0);
        key(re, VK_UP, 0, 0);
        key(ed, VK_UP, 0, 0);
        key(re, VK_UP, 0, 0);
        back_r = caret_of(re);
        {
            DWORD f = 0, t = 0;
            SendMessageA(ed, EM_GETSEL, (WPARAM)&f, (LPARAM)&t);
            back_e = (int)t;
        }
        CHECK(back_r == start,
              "a rich edit remembers where a walk down the lines set out "
              "from, so walking back up comes out on the character it left");
        CHECK(back_e != start && deep_e != deep_r,
              "and an EDIT does not, which is the machine's difference and "
              "not ours");
        /* And the goal is forgotten the moment the caret is moved another
         * way, or the next Down would set out from somewhere it has left. */
        SendMessageA(re, EM_SETSEL, start, start);
        key(re, VK_DOWN, 0, 0);
        key(re, VK_LEFT, 0, 0);
        key(re, VK_RIGHT, 0, 0);
        key(re, VK_DOWN, 0, 0);
        key(re, VK_UP, 0, 0);
        key(re, VK_UP, 0, 0);
        CHECK(caret_of(re) != start,
              "unless something else moved the caret in between, which "
              "forgets it");
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
    /* **This asserted the opposite until jd reported Undo as broken**, on
     * the stated grounds that it is what "an edit control does" -- which is
     * true of the EDIT and was never read off a rich edit. A reading of one
     * control is not evidence about the other; that is the same mistake as
     * the EDIT's `EM_POSFROMCHAR` -1 being written into a rich edit's
     * oracle, and it stood here longer.
     *
     * A second undo now goes *further back*, to whatever this test did
     * before -- which is why what is asserted is that it does **not** return
     * to "before!", rather than a particular string. A stack's contents
     * depend on everything above it in the file, and an assertion that
     * names one is an assertion about the test rather than the control.
     *
     * **The direction is jd's report and the depth is not measured.** Nobody
     * has counted riched20's undo steps on the machine, or asked whether a
     * typed run comes back in one; both are recorded in src/richedit.c as
     * open rather than settled here. */
    SendMessageA(re, EM_UNDO, 0, 0);
    CHECK(strcmp(text_of(re), "before!") != 0,
          "and undoing again does not put it back -- a rich edit's undo is "
          "a stack, not the EDIT's swap");
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

    /* ---- runs of formatting ----
     *
     * Every number and rule below is riched20's own answer, read with
     * tools/vm/ctlprobe.c and written up in docs/testing.md. */

    /* ---- SCF_DEFAULT: the format that outlives the text ----
     *
     * **Both halves, because either alone passes with the bug in place.** The
     * default reaching an empty control's selection was already true by
     * accident -- the old code armed the insertion format, which reads back
     * the same way -- so a test that stopped there went green while WordPad
     * still lost a document's font on open. The second half is the one that
     * fails without the fix: `WM_SETTEXT` threw the armed format away and put
     * every run back to the control's own face.
     *
     * riched20's answers, tools/vm/deffmt.txt on Windows 2000:
     *
     *   SCF_DEFAULT   Arial, then SetWindowTextA -> Arial
     *   SCF_SELECTION Arial, then SetWindowTextA -> the control's own face
     *
     * SCF_DEFAULT is 0x0000, so the call below passes no flag at all. That is
     * the thing itself and not a shorthand: a default call is one with
     * neither SCF_SELECTION nor SCF_ALL. */
    {
        CHARFORMATA cf;
        SetWindowTextA(re, "");
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_FACE;
        face_copy_test(cf.szFaceName, "Arial");
        SendMessageA(re, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);

        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(strcmp(cf.szFaceName, "Arial") == 0,
              "SCF_DEFAULT on an empty control is what the selection reports");

        SetWindowTextA(re, "hello");
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(strcmp(cf.szFaceName, "Arial") == 0,
              "and text set after it arrives in that face, not the control's");

        /* And the other row of the machine's table, so the test says which of
         * the two rules holds rather than only that one of them does. */
        SetWindowTextA(re, "");
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_FACE;
        face_copy_test(cf.szFaceName, "Courier New");
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        SetWindowTextA(re, "hello");
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(strcmp(cf.szFaceName, "Arial") == 0,
              "a selection format does not outlive the text; the default does");
    }

    {
        CHARFORMATA cf;
        CHARRANGE cr;
        SetWindowTextA(re, "abcdefghijklmnopqrst");
        CHECK(ween_rich_run_count(re) == 1,
              "a document with nothing said about it is one run");

        /* Bolding the middle of a run splits it into three. */
        cr.cpMin = 5;
        cr.cpMax = 10;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(ween_rich_run_count(re) == 3,
              "and formatting the middle of it makes three");

        cr.cpMin = 4;
        cr.cpMax = 5;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwMask & CFM_BOLD) && !(cf.dwEffects & CFE_BOLD),
              "the character before the run is not in it");
        cr.cpMin = 5;
        cr.cpMax = 6;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD),
              "the first character of it is");
        cr.cpMin = 10;
        cr.cpMax = 11;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwMask & CFM_BOLD) && !(cf.dwEffects & CFE_BOLD),
              "and the one after it is not");

        /* A selection spanning two runs: the mask says what it is sure of
         * and the effects carry the character before the end -- both of
         * which the machine answers and neither of which is obvious. */
        cr.cpMin = 4;
        cr.cpMax = 6;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(!(cf.dwMask & CFM_BOLD),
              "over two runs the bold bit is cleared in the mask, which is "
              "how a format bar knows to show Bold as neither in nor out");
        CHECK((cf.dwEffects & CFE_BOLD),
              "while the effects carry the character before the end, which "
              "here is bold");
        cr.cpMin = 0;
        cr.cpMax = 20;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(!(cf.dwMask & CFM_BOLD) && !(cf.dwEffects & CFE_BOLD),
              "and over the whole text they carry the last character, which "
              "is not");

        /* Making the neighbour identical merges the two. */
        cr.cpMin = 10;
        cr.cpMax = 15;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(ween_rich_run_count(re) == 3,
              "bolding what follows a bold run leaves three runs and not "
              "four: a run identical to its neighbour is merged with it");
        cr.cpMin = 5;
        cr.cpMax = 15;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD),
              "and the ten characters are one bold stretch");

        /* And taking it away from the middle of that stretch splits it
         * again, five runs where riched20 has five. */
        cr.cpMin = 8;
        cr.cpMax = 12;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = 0;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(ween_rich_run_count(re) == 5,
              "unbolding the middle of the bold stretch gives five runs");
    }

    {
        /* What a character typed takes with it. */
        CHARFORMATA cf;
        CHARRANGE cr;
        SetWindowTextA(re, "abcdefghij");
        cr.cpMin = 0;
        cr.cpMax = 5;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        cr.cpMin = cr.cpMax = 5; /* between the bold run and the plain one */
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        typed(re, "X");
        cr.cpMin = 5;
        cr.cpMax = 6;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwEffects & CFE_BOLD),
              "a character typed at a boundary takes the formatting of the "
              "character before the caret, not the one after");

        /* And what a program arms with a set on an empty selection. */
        SetWindowTextA(re, "plain");
        cr.cpMin = cr.cpMax = 5;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(strcmp(text_of(re), "plain") == 0,
              "setting a format with nothing selected changes no text");
        typed(re, "Z");
        cr.cpMin = 5;
        cr.cpMax = 6;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwEffects & CFE_BOLD),
              "and the next character typed is the one it was armed for");
        cr.cpMin = cr.cpMax = 6; /* the caret at the end, nothing selected */
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        typed(re, "z");
        cr.cpMin = 6;
        cr.cpMax = 7;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwEffects & CFE_BOLD),
              "and the one after that carries on from it, being now the "
              "character before the caret");
    }

    {
        /* A size and a colour, and what they do to the drawing: a run in a
         * bigger face makes its line taller and pushes what follows it
         * along, which EM_POSFROMCHAR is the way to see. */
        CHARFORMATA cf;
        CHARRANGE cr;
        POINTL a, b;
        SetWindowTextA(re, "small BIG\r\nnext line");
        a.x = a.y = b.x = b.y = 0;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&a, 9);  /* end of line 1 */
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&b, 11); /* line 2 */
        cr.cpMin = 6;
        cr.cpMax = 9;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_SIZE;
        cf.yHeight = 400; /* twenty points, against the default's eight */
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        {
            POINTL a2, b2;
            a2.x = a2.y = b2.x = b2.y = 0;
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&a2, 9);
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&b2, 11);
            CHECK(a2.x > a.x,
                  "a run set in a bigger size takes more room along the line");
            CHECK(b2.y > b.y, "and makes the line it is on taller");
        }
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwMask & CFM_SIZE) && cf.yHeight == 400,
              "and the size comes back as it was set, in twips");

        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = RGB(255, 0, 0);
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwMask & CFM_COLOR) && cf.crTextColor == RGB(255, 0, 0) &&
                  !(cf.dwEffects & CFE_AUTOCOLOR),
              "a colour set is a colour kept, and turns the automatic one off");
    }

    {
        /* SCF_ALL is the whole document whatever is selected. */
        CHARFORMATA cf;
        SetWindowTextA(re, "one two three");
        SendMessageA(re, EM_SETSEL, 0, 3);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_ITALIC;
        cf.dwEffects = CFE_ITALIC;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
        CHECK(ween_rich_run_count(re) == 1,
              "SCF_ALL puts one formatting over everything, which is one run");
        SendMessageA(re, EM_SETSEL, 9, 13);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwEffects & CFE_ITALIC),
              "including the part that was not selected when it was set");
    }

    {
        /* Text put in and taken out moves the runs with it. */
        CHARFORMATA cf;
        CHARRANGE cr;
        SetWindowTextA(re, "one two three");
        cr.cpMin = 4;
        cr.cpMax = 7;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        SendMessageA(re, EM_SETSEL, 0, 0);
        typed(re, "XX"); /* two characters before the bold run */
        cr.cpMin = 6;
        cr.cpMax = 9; /* where "two" is now */
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD),
              "text put in before a run carries the run along with it");
        SendMessageA(re, EM_SETSEL, 0, 2);
        SendMessageA(re, WM_CLEAR, 0, 0);
        cr.cpMin = 4;
        cr.cpMax = 7;
        SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD),
              "and taking it out again brings the run back where it was");
    }

    {
        /* EN_SELCHANGE, which is what keeps a format bar in step with the
         * caret -- and, like everything else here, waits on the mask. */
        SetWindowTextA(re, "one two three");
        SendMessageA(re, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_UPDATE);
        g_selchange = 0;
        SendMessageA(re, EM_SETSEL, 2, 5);
        CHECK(g_selchange == 0,
              "a selection that moves says nothing until it is asked to");
        SendMessageA(re, EM_SETEVENTMASK, 0,
                     ENM_CHANGE | ENM_UPDATE | ENM_SELCHANGE);
        g_selchange = 0;
        SendMessageA(re, EM_SETSEL, 4, 9);
        CHECK(g_selchange == 1 && g_sel_from == 4 && g_sel_to == 9,
              "and then says where it is now");
        CHECK(g_seltyp == (SEL_TEXT | SEL_MULTICHAR),
              "with what kind of thing is in it: more than one character of "
              "text");
        g_selchange = 0;
        SendMessageA(re, EM_SETSEL, 4, 9);
        CHECK(g_selchange == 0, "a selection set where it already is says nothing");
        g_selchange = 0;
        SendMessageA(re, EM_SETSEL, 6, 6);
        CHECK(g_selchange == 1 && g_seltyp == SEL_EMPTY,
              "and an empty one says so");
        SendMessageA(re, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_UPDATE);
    }

    /* ---- paragraphs ----
     *
     * Every rule here is riched20's own answer to tools/vm/ctlprobe.c. The
     * offsets are the control's, where a mark is one character: "one\r\ntwo
     * \r\nthree" is 0..3, 4..7 and 8..13. */

    {
        PARAFORMAT pf;
        SetWindowTextA(re, "one\r\ntwo\r\nthree");
        SendMessageA(re, EM_SETSEL, 0, 13);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK((pf.dwMask & PFM_ALIGNMENT) && pf.wAlignment == PFA_LEFT,
              "a fresh control's paragraphs are ranged left");
        CHECK(pf.dxStartIndent == 0 && pf.dxRightIndent == 0 &&
                  pf.dxOffset == 0 && pf.cTabCount == 0,
              "with no indents and no tab stops");

        /* One character of the second paragraph selected, and the whole
         * paragraph takes the alignment -- while its neighbours do not. */
        SendMessageA(re, EM_SETSEL, 5, 6);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_CENTER;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        SendMessageA(re, EM_SETSEL, 4, 5);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_CENTER,
              "setting an alignment over one character of a paragraph "
              "centres the whole of it: its first character");
        SendMessageA(re, EM_SETSEL, 6, 7);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_CENTER, "and its last");
        SendMessageA(re, EM_SETSEL, 0, 1);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_LEFT, "the paragraph before is untouched");
        SendMessageA(re, EM_SETSEL, 8, 9);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_LEFT, "and so is the one after");
        SendMessageA(re, EM_SETSEL, 0, 13);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(!(pf.dwMask & PFM_ALIGNMENT),
              "read across three that do not agree, the alignment bit is "
              "cleared in the mask, as a CHARFORMAT's is");

        /* A selection reaching into the next paragraph takes it whole. */
        SetWindowTextA(re, "one\r\ntwo\r\nthree");
        SendMessageA(re, EM_SETSEL, 0, 5);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_RIGHT;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        SendMessageA(re, EM_SETSEL, 0, 1);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_RIGHT, "a range over two takes the first");
        SendMessageA(re, EM_SETSEL, 6, 7);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_RIGHT,
              "and the second, of which it touched one character");
        SendMessageA(re, EM_SETSEL, 9, 10);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_LEFT, "and not the third, which it did not");
    }

    {
        /* What a new paragraph inherits, and what survives a join. */
        PARAFORMAT pf;
        SetWindowTextA(re, "alpha beta");
        SendMessageA(re, EM_SETSEL, 0, 10);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_CENTER;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        SendMessageA(re, EM_SETSEL, 5, 5);
        SendMessageA(re, WM_CHAR, (WPARAM)'\r', 0);
        SendMessageA(re, EM_SETSEL, 0, 1);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_CENTER,
              "a paragraph split in two leaves the first half as it was");
        SendMessageA(re, EM_SETSEL, 7, 8);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_CENTER, "and the second half carries it too");

        SetWindowTextA(re, "left\r\nright");
        SendMessageA(re, EM_SETSEL, 0, 4);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_LEFT;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        SendMessageA(re, EM_SETSEL, 5, 10);
        pf.wAlignment = PFA_RIGHT;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        SendMessageA(re, EM_SETSEL, 5, 5); /* just past the mark */
        SendMessageA(re, WM_CHAR, (WPARAM)'\b', 0);
        CHECK(strcmp(text_of(re), "leftright") == 0,
              "a backspace over the mark joins the two paragraphs");
        SendMessageA(re, EM_SETSEL, 0, 9);
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK((pf.dwMask & PFM_ALIGNMENT) && pf.wAlignment == PFA_LEFT,
              "and what is left is the first one's, not the second's");
    }

    {
        /* And what the alignment and the indents do to the drawing, which
         * EM_POSFROMCHAR is the way to see. */
        PARAFORMAT pf;
        POINTL flush, centred, indented, righted;
        SetWindowTextA(re, "a line of words");
        flush.x = flush.y = 0;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&flush, 0);
        SendMessageA(re, EM_SETSEL, 0, 15);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_CENTER;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        centred.x = centred.y = 0;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&centred, 0);
        CHECK(centred.x > flush.x,
              "a centred paragraph starts further along than a flush one");
        pf.wAlignment = PFA_RIGHT;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        righted.x = righted.y = 0;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&righted, 0);
        CHECK(righted.x > centred.x, "and a ranged-right one further still");
        pf.wAlignment = PFA_LEFT;
        pf.dwMask = PFM_ALIGNMENT | PFM_STARTINDENT;
        pf.dxStartIndent = 1440; /* an inch, which is 96 pixels at 96 dpi */
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        indented.x = indented.y = 0;
        SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&indented, 0);
        CHECK(indented.x == flush.x + 96,
              "and an indent of a whole inch moves it ninety-six pixels at "
              "ninety-six dots to the inch");
    }

    /* ---- wrapping ----
     *
     * The machine's rule, read off riched20 in a control 200 pixels wide: a
     * line breaks at the last space that fits and the space stays on the
     * line that broke, a word too long for a line of its own breaks at the
     * character that fits, and EM_SETTARGETDEVICE with any width at all
     * turns the breaking off. */
    {
        HWND w = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0,
                                 200, 90, host, (HMENU)(UINT_PTR)12, NULL,
                                 NULL);
        const char *sentence = "the quick brown fox jumps over the lazy dog";
        int lines, i, broke_after_space = 1;
        SetWindowTextA(w, sentence);
        lines = (int)SendMessageA(w, EM_GETLINECOUNT, 0, 0);
        CHECK(lines > 1, "a sentence too long for the control is broken");
        for (i = 0; i + 1 < lines; i++) {
            int start = (int)SendMessageA(w, EM_LINEINDEX, i, 0);
            int len = (int)SendMessageA(w, EM_LINELENGTH, start, 0);
            if (start + len - 1 < 0 || sentence[start + len - 1] != ' ')
                broke_after_space = 0;
        }
        CHECK(broke_after_space,
              "and every break is after a space, which stays on the line it "
              "broke -- as the machine's does");
        CHECK(SendMessageA(w, EM_LINEINDEX, 1, 0) ==
                  SendMessageA(w, EM_LINEINDEX, 0, 0) +
                      SendMessageA(w, EM_LINELENGTH, 0, 0),
              "the next line begins where the last one ended, there being no "
              "mark between them");

        SetWindowTextA(w, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        CHECK(SendMessageA(w, EM_GETLINECOUNT, 0, 0) > 1,
              "a word too long for a line of its own is broken anyway");

        SetWindowTextA(w, sentence);
        lines = (int)SendMessageA(w, EM_GETLINECOUNT, 0, 0);
        SendMessageA(w, EM_SETTARGETDEVICE, 0, 1);
        CHECK(SendMessageA(w, EM_GETLINECOUNT, 0, 0) == 1,
              "EM_SETTARGETDEVICE with a width and no device stops the "
              "breaking, which is what No Wrap sends");
        SendMessageA(w, EM_SETTARGETDEVICE, 0, 0);
        CHECK(SendMessageA(w, EM_GETLINECOUNT, 0, 0) == lines,
              "and nought brings it back to the window's own width");

        /* Two paragraphs, so that the count is of the lines they are drawn
         * on and not of the paragraphs themselves. */
        SetWindowTextA(w, "the quick brown fox jumps over the lazy dog\r\nand "
                          "again");
        CHECK(SendMessageA(w, EM_GETLINECOUNT, 0, 0) > 2,
              "a paragraph that breaks counts for more than one line");
        DestroyWindow(w);
    }

    /* ---- the bar comes and goes ----
     *
     * A rich edit puts a vertical bar up only when there is something to
     * scroll, where an EDIT with WS_VSCROLL always has one. The machine's
     * WordPad shows none at all on an empty document -- columns 749..761 of
     * `wordpad/reference/shots/win.png` are white where ours had a track --
     * and ES_DISABLENOSCROLL is what asks for one that is always there. */
    {
        HWND w = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                     ES_MULTILINE,
                                 0, 0, 200, 60, host, (HMENU)(UINT_PTR)13,
                                 NULL, NULL);
        RECT cr;
        int bare, full;
        GetClientRect(w, &cr);
        bare = cr.right;
        SetWindowTextA(w, "one\r\ntwo\r\nthree\r\nfour\r\nfive\r\nsix\r\n"
                          "seven\r\neight\r\nnine\r\nten");
        GetClientRect(w, &cr);
        full = cr.right;
        CHECK(bare == full,
              "the client is the client either way -- the bar is drawn "
              "inside it, as every control here draws its own");
        {
            /* what the text is broken to is what says whether the bar took
             * room: a line that fitted before has to break once it has */
            int wide = (int)SendMessageA(w, EM_GETLINECOUNT, 0, 0);
            SetWindowTextA(w, "one");
            CHECK((int)SendMessageA(w, EM_GETLINECOUNT, 0, 0) == 1,
                  "a document of one line is one line");
            CHECK(wide == 10,
                  "and ten short paragraphs are ten lines, none of them "
                  "broken by a bar that took width from them");
        }
        DestroyWindow(w);
    }

    /* ---- RTF ---- */

    {
        /* What the writer puts out, in the shape riched20 puts it: the
         * header, the two tables, then the paragraphs. */
        CHARFORMATA cf;
        PARAFORMAT pf;
        const char *rtf;
        SetWindowTextA(re, "plain and formatted");
        SendMessageA(re, EM_SETSEL, 10, 19);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT |
                    CFM_SIZE | CFM_COLOR;
        cf.dwEffects = CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT;
        cf.yHeight = 240;
        cf.crTextColor = RGB(255, 0, 0);
        SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_ALIGNMENT | PFM_STARTINDENT;
        pf.wAlignment = PFA_CENTER;
        pf.dxStartIndent = 720;
        SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);

        rtf = streamed_out(re, SF_RTF);
        CHECK(strncmp(rtf, "{\\rtf1\\ansi", 11) == 0,
              "an RTF document opens the way the machine's does");
        CHECK(strstr(rtf, "{\\fonttbl") && strstr(rtf, "{\\colortbl ;"),
              "with a font table and a colour table whose first entry is the "
              "automatic one");
        CHECK(strstr(rtf, "\\red255\\green0\\blue0"),
              "the colour a run was given is in the table");
        CHECK(strstr(rtf, "\\pard") && strstr(rtf, "\\qc") &&
                  strstr(rtf, "\\li720"),
              "the paragraph states its alignment and its indent");
        CHECK(strstr(rtf, "\\b\\i") || strstr(rtf, "\\b") ,
              "and the run its bold");
        CHECK(strstr(rtf, "\\fs24"),
              "a size goes out in half-points: 240 twips is \\fs24");
        CHECK(strstr(rtf, "plain and ") && strstr(rtf, "formatted"),
              "the text is in there, in its pieces");
        CHECK(strstr(rtf, "\\par"), "and the paragraph ends with a \\par");
    }

    {
        /* Out and back in: what the writer wrote, the reader has to
         * understand. */
        char keep[4096];
        CHARFORMATA cf;
        PARAFORMAT pf;
        {
            const char *w = streamed_out(re, SF_RTF);
            size_t n = strlen(w);
            if (n > sizeof keep - 1)
                n = sizeof keep - 1;
            memcpy(keep, w, n);
            keep[n] = 0;
        }
        SetWindowTextA(re, "");
        stream_into(re, SF_RTF, keep);
        CHECK(strcmp(text_of(re), "plain and formatted") == 0,
              "a document read back has the text it was written from");
        SendMessageA(re, EM_SETSEL, 12, 14);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwEffects & CFE_BOLD) && (cf.dwEffects & CFE_ITALIC) &&
                  (cf.dwEffects & CFE_UNDERLINE) &&
                  (cf.dwEffects & CFE_STRIKEOUT),
              "and the formatting of its runs");
        CHECK(cf.yHeight == 240, "including the size");
        CHECK(!(cf.dwEffects & CFE_AUTOCOLOR) &&
                  cf.crTextColor == RGB(255, 0, 0),
              "and the colour");
        SendMessageA(re, EM_SETSEL, 0, 1);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_CENTER && pf.dxStartIndent == 720,
              "and what its paragraphs carried");
    }

    {
        /* And a document written by hand, which is what a file is: the very
         * one the machine was given, with the answers it gave. */
        CHARFORMATA cf;
        PARAFORMAT pf;
        stream_into(re, SF_RTF,
                    "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}"
                    "{\\colortbl;\\red0\\green0\\blue255;}"
                    "\\pard\\qc\\li720\\fs28 centred \\b bold \\b0\\cf1 blue\\par}");
        CHECK(strncmp(text_of(re), "centred bold blue", 17) == 0,
              "a hand-written document comes in as its text");
        SendMessageA(re, EM_SETSEL, 0, 7);
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(cf.yHeight == 280 && strcmp(cf.szFaceName, "Arial") == 0,
              "in the size and face its header named: 280 twips, Arial");
        SendMessageA(re, EM_SETSEL, 8, 12);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK((cf.dwEffects & CFE_BOLD), "with the bold word bold");
        SendMessageA(re, EM_SETSEL, 13, 17);
        SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHECK(!(cf.dwEffects & CFE_AUTOCOLOR) &&
                  cf.crTextColor == RGB(0, 0, 255),
              "and the blue one blue, out of the colour table");
        SendMessageA(re, EM_SETSEL, 0, 5);
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.wAlignment == PFA_CENTER && pf.dxStartIndent == 720,
              "and the paragraph centred and indented, as the machine "
              "answered for the same document");
    }

    {
        /* Plain text, which is the other half of what WordPad saves. */
        const char *text;
        SetWindowTextA(re, "one\r\ntwo");
        text = streamed_out(re, SF_TEXT);
        CHECK(strcmp(text, "one\r\ntwo") == 0,
              "SF_TEXT hands the text out with both characters of every mark");
        stream_into(re, SF_TEXT, "back\r\nagain");
        CHECK(strcmp(text_of(re), "back\r\nagain") == 0 &&
                  SendMessageA(re, EM_GETLINECOUNT, 0, 0) == 2,
              "and takes one in the same way");
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

    {
        /* Tabs, held to the machine's own numbers.
         *
         * Every one of these came off riched20 through EM_POSFROMCHAR in
         * tools/vm/ctlprobe.c, in a control made the same way as this one:
         * WS_EX_CLIENTEDGE, no vertical bar, and the message font. Because
         * the text here is tabs and nothing else, none of it depends on the
         * strike -- a tab's advance is the stop's, not the glyph's -- so
         * these are the machine's pixels and not an approximation of them.
         *
         *   nine tabs, no stops of their own   1 49 97 145 193 241 ...
         *   stops at 300, 1000, 2137 twips     1 21 68 143 145 193
         *   one stop at 500 twips              1 34 49 97 145 193
         *   cleared again                      1 49 97 145
         */
        HWND t = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                     ES_AUTOVSCROLL,
                                 0, 0, 560, 60, host, NULL, NULL, NULL);
        RECT tc;
        PARAFORMAT pf;
        POINTL p;
        int i;
        static const int plain[6] = { 1, 49, 97, 145, 193, 241 };
        static const int three[6] = { 1, 21, 68, 143, 145, 193 };
        static const int one[6] = { 1, 34, 49, 97, 145, 193 };
        GetClientRect(t, &tc);
        CHECK(tc.right == 556,
              "a rich edit 560 wide has the machine's 556 of client");
        SetWindowTextA(t, "\t\t\t\t\t.");
        for (i = 0; i < 6; i++) {
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, i);
            if (p.x != plain[i])
                break;
        }
        CHECK(i == 6, "with no stops of its own a tab goes to the next "
                      "half-inch: 1, 49, 97, 145, 193, 241");

        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_TABSTOPS;
        pf.cTabCount = 3;
        pf.rgxTabs[0] = 300;
        pf.rgxTabs[1] = 1000;
        pf.rgxTabs[2] = 2137;
        SendMessageA(t, EM_SETSEL, 0, -1);
        SendMessageA(t, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        for (i = 0; i < 6; i++) {
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, i);
            if (p.x != three[i])
                break;
        }
        CHECK(i == 6, "stops at 300, 1000 and 2137 twips put it at 21, 68 "
                      "and 143 -- and the two tabs past the last stop go "
                      "back to the half-inch grid at 145 and 193");
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_TABSTOPS;
        SendMessageA(t, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.cTabCount == 3 && pf.rgxTabs[0] == 300 &&
                  pf.rgxTabs[1] == 1000 && pf.rgxTabs[2] == 2137,
              "and EM_GETPARAFORMAT gives the three of them back");

        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_TABSTOPS;
        pf.cTabCount = 1;
        pf.rgxTabs[0] = 500;
        SendMessageA(t, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        for (i = 0; i < 6; i++) {
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, i);
            if (p.x != one[i])
                break;
        }
        CHECK(i == 6, "one stop at 500 twips is 33 pixels in, and the grid "
                      "past it is measured from the left edge and not from "
                      "the stop: 34, 49, 97, 145, 193");
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_TABSTOPS;
        pf.cTabCount = 0;
        SendMessageA(t, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        for (i = 0; i < 4; i++) {
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, i);
            if (p.x != plain[i])
                break;
        }
        CHECK(i == 4, "and clearing the stops comes back to the half-inch");

        /* Stops belong to a paragraph. The machine: a stop of 300 on the
         * first of two puts its tab at 21 and leaves the second's at 49. */
        SetWindowTextA(t, "\t.\r\n\t.");
        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_TABSTOPS;
        pf.cTabCount = 1;
        pf.rgxTabs[0] = 300;
        SendMessageA(t, EM_SETSEL, 0, 1);
        SendMessageA(t, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        {
            int x0, x1, x3, x4;
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 0);
            x0 = p.x;
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 1);
            x1 = p.x;
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 3);
            x3 = p.x;
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 4);
            x4 = p.x;
            CHECK(x0 == 1 && x1 == 21 && x3 == 1 && x4 == 49,
                  "a stop on the first of two paragraphs leaves the second "
                  "on the half-inch grid");
        }

        /* Where a click lands inside a tab. The machine's rule, measured on
         * "a<tab>b" and on "abcdef" alike, is the nearest place between two
         * characters with a tie going left: 'a' spans 1..7 and the caret
         * turns at 5, the tab spans 7..49 and it turns at 29 -- both one
         * past the middle. Here the tab is the first character, so it spans
         * 1..49 and the turn is at 26. */
        SetWindowTextA(t, "\tb");
        SetFocus(t);
        for (i = 24; i <= 27; i++) {
            CHARRANGE cr;
            SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(i, 4));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(i, 4));
            memset(&cr, 0, sizeof cr);
            SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
            if (cr.cpMin != (i <= 25 ? 0 : 1))
                break;
        }
        CHECK(i == 28, "a click in a tab's own stretch goes to whichever end "
                       "is nearer, and the middle itself goes left");
        /* And the drawing, since every number above is a measurement and
         * none of them is a pixel: with the tab selected, the room it makes
         * is filled, and the fill is the 48 the stop asked for. */
        SetWindowTextA(t, "\tX");
        SendMessageA(t, EM_SETSEL, 0, 1);
        SetFocus(t);
        InvalidateRect(t, NULL, TRUE);
        ween_flush_paint();
        {
            struct ween_wnd *w = (struct ween_wnd *)host;
            int y, run = 0, best = 0;
            for (y = 0; y < 60; y++) {
                int x, n = 0;
                for (x = 0; x < 200; x++) {
                    unsigned px =
                        w->surface.px
                            ? w->surface.px[(size_t)y * w->surface.w + x] &
                                  0xffffff
                            : 0;
                    if (px == WEEN_CAP_LEFT) {
                        n++;
                        if (n > run)
                            run = n;
                    } else {
                        n = 0;
                    }
                }
                if (run > best)
                    best = run;
                run = 0;
            }
            CHECK(best == 48, "a selected tab's room is drawn, and it is the "
                              "48 pixels the stop asked for");
        }
        DestroyWindow(t);

        /* A tab that would land past the edge takes the line with it. The
         * machine, in a control whose client is 116 wide: four tabs and a
         * stop are two lines, [0,2] and [2,3], and the third tab -- which
         * would have gone to 145 -- begins the second line at 1 and
         * advances to 49 from there. */
        t = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                            WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                ES_AUTOVSCROLL,
                            0, 0, 120, 60, host, NULL, NULL, NULL);
        GetClientRect(t, &tc);
        CHECK(tc.right == 116, "and 120 wide has the machine's 116");
        SetWindowTextA(t, "\t\t\t\t.");
        CHECK(SendMessageA(t, EM_GETLINECOUNT, 0, 0) == 2,
              "four tabs in a control 116 wide are two lines");
        CHECK(SendMessageA(t, EM_LINEINDEX, 1, 0) == 2,
              "the second of them beginning at the third tab");
        {
            static const int narrow[5] = { 1, 49, 1, 49, 97 };
            for (i = 0; i < 5; i++) {
                p.x = p.y = 0;
                SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, i);
                if (p.x != narrow[i])
                    break;
            }
            CHECK(i == 5, "and the wrapped tab starts the new line at 1 and "
                          "advances to 49 from there");
        }
        DestroyWindow(t);
    }

    {
        /* Finding, every row of it the machine's own answer through
         * EM_FINDTEXTEX -- see docs/testing.md for the table this came
         * from and ctlprobe.c's `finding` block for how it was asked. */
        struct find_case {
            const char *what;
            DWORD flags;
            int from, to;
            const char *needle;
            int want, wmin, wmax;
        };
        static const struct find_case ones[] = {
            { "without FR_DOWN a forward range finds nothing", 0, 0, -1,
              "cat", -1, -1, -1 },
            { "and not even a word at the very start", 0, 0, -1, "one", -1,
              -1, -1 },
            { "FR_DOWN over the whole document", FR_DOWN, 0, -1, "cat", 4, 4,
              7 },
            { "case is ignored, so \"cat\" finds the lower one first",
              FR_DOWN, 0, -1, "cat", 4, 4, 7 },
            { "FR_MATCHCASE with \"Cat\" passes it and takes the upper",
              FR_DOWN | FR_MATCHCASE, 0, -1, "Cat", 12, 12, 15 },
            { "FR_WHOLEWORD does not take the \"cat\" inside \"catalog\"",
              FR_DOWN | FR_WHOLEWORD, 0, -1, "cat", 4, 4, 7 },
            { "and takes the whole word when the whole word is asked for",
              FR_DOWN | FR_WHOLEWORD, 0, -1, "catalog", 22, 22, 29 },
            { "a match starting exactly at cpMin counts", FR_DOWN, 4, -1,
              "cat", 4, 4, 7 },
            { "and one before it is passed by", FR_DOWN, 5, -1, "cat", 12,
              12, 15 },
            { "a match that would run past cpMax is not one", FR_DOWN, 0, 6,
              "cat", -1, -1, -1 },
            { "and the same range one longer is", FR_DOWN, 0, 7, "cat", 4, 4,
              7 },
            { "backwards from the end takes the last match", 0, 34, 0, "cat",
              30, 30, 33 },
            { "FR_DOWN from the end finds nothing, the flag deciding the "
              "direction and not the order of the range",
              FR_DOWN, 34, 0, "cat", -1, -1, -1 },
            { "backwards answers the nearest match behind, not the first",
              0, 20, 0, "cat", 12, 12, 15 },
            { "backwards, a match starting before the far end is not one", 0,
              20, 13, "cat", -1, -1, -1 },
            { "and the same range one further out is", 0, 20, 12, "cat", 12,
              12, 15 },
            { "backwards, a match ending exactly where it starts counts", 0,
              15, 0, "cat", 12, 12, 15 },
            { "and one ending past it does not", 0, 14, 0, "cat", 4, 4, 7 },
            { "forwards from 30 finds the one at 30", FR_DOWN, 30, -1, "cat",
              30, 30, 33 },
            { "and from 31 finds nothing", FR_DOWN, 31, -1, "cat", -1, -1,
              -1 },
            { "a word that is not there", FR_DOWN, 0, -1, "zebra", -1, -1,
              -1 },
            { "the empty string is never found", FR_DOWN, 0, -1, "", -1, -1,
              -1 },
        };
        static const struct find_case marks[] = {
            { "the mark is the single CR it is stored as", FR_DOWN, 0, -1,
              "e\rt", 2, 2, 5 },
            { "and the CRLF a program handed in is not in the document",
              FR_DOWN, 0, -1, "e\r\nt", -1, -1, -1 },
            { "the first paragraph", FR_DOWN, 0, -1, "one", 0, 0, 3 },
            { "and the second, at the offset the storage gives it", FR_DOWN,
              0, -1, "two", 4, 4, 7 },
        };
        static const struct find_case words[] = {
            { "a stop ends a word", FR_DOWN | FR_WHOLEWORD, 1, -1, "cat", 4,
              4, 7 },
            { "so does a hyphen", FR_DOWN | FR_WHOLEWORD, 5, -1, "cat", 9, 9,
              12 },
            { "a digit does not -- \"cat9\" is passed by for \"cat_\"",
              FR_DOWN | FR_WHOLEWORD, 10, -1, "cat", 20, 20, 23 },
            { "which an underscore does", FR_DOWN | FR_WHOLEWORD, 16, -1,
              "cat", 20, 20, 23 },
            { "and a bracket", FR_DOWN | FR_WHOLEWORD, 21, -1, "cat", 26, 26,
              29 },
        };
        struct {
            const char *text;
            const struct find_case *c;
            int n;
        } sets[3];
        int k;
        sets[0].text = "one cat two Cat three catalog cat.";
        sets[0].c = ones;
        sets[0].n = (int)(sizeof ones / sizeof ones[0]);
        sets[1].text = "one\r\ntwo";
        sets[1].c = marks;
        sets[1].n = (int)(sizeof marks / sizeof marks[0]);
        sets[2].text = "cat cat. cat-o cat9 cat_ (cat)";
        sets[2].c = words;
        sets[2].n = (int)(sizeof words / sizeof words[0]);
        for (k = 0; k < 3; k++) {
            int j;
            SetWindowTextA(re, sets[k].text);
            for (j = 0; j < sets[k].n; j++) {
                const struct find_case *c = &sets[k].c[j];
                FINDTEXTEXA ft;
                LRESULT r;
                memset(&ft, 0, sizeof ft);
                ft.chrg.cpMin = c->from;
                ft.chrg.cpMax = c->to;
                ft.lpstrText = (LPSTR)c->needle;
                ft.chrgText.cpMin = -2;
                ft.chrgText.cpMax = -2;
                r = SendMessageA(re, EM_FINDTEXTEX, c->flags, (LPARAM)&ft);
                CHECK((int)r == c->want && ft.chrgText.cpMin == c->wmin &&
                          ft.chrgText.cpMax == c->wmax,
                      c->what);
                if ((int)r != c->want || ft.chrgText.cpMin != c->wmin ||
                    ft.chrgText.cpMax != c->wmax)
                    printf("     wanted %d (%d..%d), got %d (%ld..%ld)\n",
                           c->want, c->wmin, c->wmax, (int)r,
                           (long)ft.chrgText.cpMin, (long)ft.chrgText.cpMax);
            }
        }
        {
            /* EM_FINDTEXT itself, which takes the shorter struct and has no
             * range to fill in; and a find moving nothing, which the frame
             * will lean on when it puts the selection where the match is. */
            FINDTEXTA ft;
            CHARRANGE cr;
            memset(&ft, 0, sizeof ft);
            ft.chrg.cpMin = 0;
            ft.chrg.cpMax = -1;
            ft.lpstrText = (LPSTR) "cat";
            SetWindowTextA(re, "one cat two Cat three catalog cat.");
            cr.cpMin = 1;
            cr.cpMax = 2;
            SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&cr);
            CHECK(SendMessageA(re, EM_FINDTEXT, FR_DOWN, (LPARAM)&ft) == 4,
                  "EM_FINDTEXT answers the same first character");
            memset(&cr, 0, sizeof cr);
            SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHECK(cr.cpMin == 1 && cr.cpMax == 2,
                  "and a find moves nothing: the selection is where it was");
        }
    }

    {
        /* What a double click takes, and where a line breaks round a tab --
         * both riched20's own answers, and both were wrong here until they
         * were asked.
         *
         * On "cat_dog cat9 don't (cat)" the machine gives 0..3, 8..13,
         * 13..19 and 20..23: an underscore breaks a word, a digit and an
         * apostrophe do not, and the trailing space comes with the word when
         * there is one. ween32 counted the underscore in, inherited from the
         * EDIT -- whose own rule turns out to be different again and is now
         * measured in tests/clip_test.c. */
        static const struct {
            int at, from, to;
            const char *what;
        } words[] = {
            { 1, 0, 3, "an underscore breaks a rich edit's word, where the "
                       "EDIT keeps it" },
            { 9, 8, 13, "a digit does not, and the trailing space comes too" },
            { 14, 13, 19, "nor does an apostrophe" },
            { 21, 20, 23, "and a bracket is not part of the word it is round" },
        };
        int k;
        SetWindowTextA(re, "cat_dog cat9 don't (cat)");
        SetFocus(re);
        for (k = 0; k < 4; k++) {
            POINTL p;
            CHARRANGE cr;
            p.x = p.y = 0;
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, words[k].at);
            SendMessageA(re, WM_LBUTTONDBLCLK, 0, MAKELPARAM(p.x + 1, p.y + 2));
            memset(&cr, 0, sizeof cr);
            SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHECK(cr.cpMin == words[k].from && cr.cpMax == words[k].to,
                  words[k].what);
        }

        /* And the wrapping. A tab is a break opportunity and it breaks
         * *before* the tab, where a space breaks after it. Both in a control
         * whose client is 116 wide, as the machine's were. */
        {
            HWND t = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                     WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                         ES_AUTOVSCROLL,
                                     0, 0, 120, 60, host, NULL, NULL, NULL);
            SetWindowTextA(t, "a b\t\t\tc");
            CHECK(SendMessageA(t, EM_GETLINECOUNT, 0, 0) == 2 &&
                      SendMessageA(t, EM_LINEINDEX, 1, 0) == 5,
                  "a line with a space in it still breaks at the tab that "
                  "does not fit, not back at the space");
            SetWindowTextA(t, "xx\tyyyyyyyyyyyyyyyyyyyyyyyy");
            CHECK(SendMessageA(t, EM_LINEINDEX, 1, 0) == 2,
                  "and a word too long to fit breaks back at the tab before "
                  "it rather than in the middle of itself");
            DestroyWindow(t);
        }
    }

    {
        /* The selection bar, which is where jd's misaligned text came from
         * and which is a style bit rather than a margin. The machine, asked
         * with the same control either way and nothing but the bit
         * different: the first character stands at x=1 without it and x=9
         * with it -- and at 9 with WordPad's own style word, with Arial 10
         * instead of the message font, and with 0x04000000 and ES_SAVESEL
         * taken off one at a time. Eight pixels, and not a function of the
         * font. */
        static const struct {
            DWORD extra;
            int want;
            const char *what;
        } bars[] = {
            { 0, 1, "a rich edit without a selection bar starts its text at "
                    "one, as the machine's does" },
            { ES_SELECTIONBAR, 9,
              "and with ES_SELECTIONBAR at nine -- eight pixels, which is "
              "the machine's bar" },
            { 0x550081C4 & ~0x50000000u, 9,
              "WordPad's own style word puts it at nine too, every other bit "
              "of it making no difference" },
        };
        int k;
        for (k = 0; k < 3; k++) {
            HWND t = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                     WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                         ES_AUTOVSCROLL | bars[k].extra,
                                     0, 0, 300, 60, host, NULL, NULL, NULL);
            POINTL p;
            SetWindowTextA(t, "cat\r\ndog");
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 0);
            CHECK(p.x == bars[k].want, bars[k].what);
            if (p.x != bars[k].want)
                printf("     wanted %d, got %d\n", bars[k].want, (int)p.x);
            /* the second line as well, since a bar that only moved the first
             * would pass the check above */
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 4);
            CHECK(p.x == bars[k].want, "and its second line with it");
            DestroyWindow(t);
        }
        {
            /* The bar takes its eight pixels out of the width there is to
             * wrap in -- reasoned rather than measured, since WordPad's own
             * right edge carries five pixels this library does not explain,
             * but it is the only reading that leaves the right edge where
             * the client's is. */
            HWND a = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                     WS_CHILD | WS_VISIBLE | ES_MULTILINE,
                                     0, 0, 120, 60, host, NULL, NULL, NULL);
            HWND b = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                     WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                         ES_SELECTIONBAR,
                                     0, 0, 120, 60, host, NULL, NULL, NULL);
            const char *many = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            SetWindowTextA(a, many);
            SetWindowTextA(b, many);
            CHECK(SendMessageA(b, EM_LINEINDEX, 1, 0) <
                      SendMessageA(a, EM_LINEINDEX, 1, 0),
                  "and the same text breaks earlier in a control that has "
                  "one, the bar coming out of the width it may wrap in");
            DestroyWindow(a);
            DestroyWindow(b);
        }
    }

    /* ---- EM_SETRECT: the box the text is laid out in ---- */
    {
        /* WordPad's first character stands on its ruler's zero and a rich
         * edit built with WordPad's own style word puts it five pixels short
         * of that. bob eliminated the other mechanisms on the machine --
         * EM_GETMARGINS answers nought with the bar and without it, and
         * Format > Paragraph reads nought for all three indents -- which
         * leaves the formatting rectangle, a message this library did not
         * have at all.
         *
         * **These are the library's own checks and not WordPad's.** A
         * message whose only evidence is that one application looks right is
         * not a measured message. */
        HWND t = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                     ES_SELECTIONBAR,
                                 0, 0, 300, 60, host, NULL, NULL, NULL);
        POINTL p;
        RECT got, set;
        SetWindowTextA(t, "cat\r\ndog");

        /* Untouched, it is the client rectangle less the border, and the
         * text is where ES_SELECTIONBAR left it. */
        memset(&got, 0, sizeof got);
        SendMessageA(t, EM_GETRECT, 0, (LPARAM)&got);
        CHECK(got.left == 1 && got.top == 1,
              "a rich edit nobody has set a rectangle on formats in its "
              "client less the border");
        p.x = p.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 0);
        CHECK(p.x == 9, "and its first character is at nine, the bar's eight "
                        "and the border's one");

        /* Set one, and the text moves with it. The selection bar is inside
         * the rectangle -- see rich_fmt in richedit.c, which says so and says
         * that nobody has measured which way the machine has it. */
        set.left = 6;
        set.top = 1;
        set.right = 290;
        set.bottom = 55;
        SendMessageA(t, EM_SETRECT, 0, (LPARAM)&set);
        p.x = p.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 0);
        CHECK(p.x == 14, "a formatting rectangle at six puts the first "
                         "character at fourteen, the bar inside it");
        if (p.x != 14)
            printf("     wanted 14, got %d\n", (int)p.x);
        p.x = p.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 4);
        CHECK(p.x == 14, "and the second line with it, not the first alone");

        memset(&got, 0, sizeof got);
        SendMessageA(t, EM_GETRECT, 0, (LPARAM)&got);
        CHECK(got.left == 6 && got.top == 1 && got.right == 290 &&
                  got.bottom == 55,
              "and EM_GETRECT hands back the one that was set");

        /* The top moves the text down, which the left cannot show. */
        set.top = 11;
        SendMessageA(t, EM_SETRECTNP, 0, (LPARAM)&set);
        p.x = p.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 0);
        CHECK(p.y == 11, "the rectangle's top is where the first line sits");

        /* NULL puts it back, which is what makes the message reversible. */
        SendMessageA(t, EM_SETRECT, 0, 0);
        memset(&got, 0, sizeof got);
        SendMessageA(t, EM_GETRECT, 0, (LPARAM)&got);
        CHECK(got.left == 1 && got.top == 1,
              "and a NULL rectangle puts it back on the client");
        p.x = p.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 0);
        CHECK(p.x == 9 && p.y == 1, "with the text where it started");

        {
            /* A narrower rectangle wraps sooner, which is the whole of what
             * a formatting rectangle means and not merely where it draws. */
            HWND wide = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                        WS_CHILD | WS_VISIBLE | ES_MULTILINE,
                                        0, 0, 200, 60, host, NULL, NULL, NULL);
            const char *many = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
            LRESULT before, after;
            RECT narrow;
            SetWindowTextA(wide, many);
            before = SendMessageA(wide, EM_LINEINDEX, 1, 0);
            narrow.left = 1;
            narrow.top = 1;
            narrow.right = 90;
            narrow.bottom = 55;
            SendMessageA(wide, EM_SETRECT, 0, (LPARAM)&narrow);
            after = SendMessageA(wide, EM_LINEINDEX, 1, 0);
            CHECK(after < before && after > 0,
                  "and the same text breaks sooner in a narrower formatting "
                  "rectangle, the box being what it wraps to");
            DestroyWindow(wide);
        }
        DestroyWindow(t);
    }

    {
        /* §5's clicking, the parts the machine was measured for: a triple
         * click takes the paragraph, the fourth press starts the count over,
         * and a press in the selection bar takes a display line -- one
         * wrapped row -- where a double click there takes the paragraph.
         *
         * The clock is the headless backend's virtual one, which does not
         * advance unless a script asks it to, so three presses sent in a row
         * are three presses at the same millisecond: exactly the run a
         * person makes and the one win32 counts. */
        HWND t = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                     ES_SELECTIONBAR,
                                 0, 0, 140, 80, host, NULL, NULL, NULL);
        CHARRANGE cr;
        POINTL p;
        int wrapped_rows;
        /* "one" and its mark, then the middle paragraph and its mark: the
         * range a triple click should take. Computed rather than counted by
         * hand, so the text can be made longer without the numbers rotting. */
        const int para_start = 4;
        const int para_end =
            para_start + (int)strlen("aaa bbb ccc ddd eee fff ggg hhh iii jjj") + 1;
        /* Three paragraphs, the middle one long enough to wrap. */
        SetWindowTextA(t,
                       "one\r\naaa bbb ccc ddd eee fff ggg hhh iii jjj\r\n"
                       "three");
        SetFocus(t);
        wrapped_rows = (int)SendMessageA(t, EM_GETLINECOUNT, 0, 0);
        CHECK(wrapped_rows > 3,
              "the middle paragraph wraps, so a display line and a paragraph "
              "are different things to select");

        /* Where the middle paragraph's second row is, so the clicks below
         * land inside it rather than on arithmetic. */
        p.x = p.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 10);
        {
            int x = p.x + 2, y = p.y + 2;
            SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            SendMessageA(t, WM_LBUTTONDBLCLK, 0, MAKELPARAM(x, y));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            memset(&cr, 0, sizeof cr);
            SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHECK(cr.cpMax - cr.cpMin <= 4 && cr.cpMax > cr.cpMin,
                  "a double click in the text takes a word");
            /* the third press, close in time and place */
            SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            memset(&cr, 0, sizeof cr);
            SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHECK(cr.cpMin == para_start && cr.cpMax == para_end,
                  "and a third press takes the whole paragraph, wrapped "
                  "lines and all");
            /* and the fourth is a word again */
            SendMessageA(t, WM_LBUTTONDBLCLK, 0, MAKELPARAM(x, y));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            memset(&cr, 0, sizeof cr);
            SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHECK(cr.cpMax - cr.cpMin <= 4 && cr.cpMax > cr.cpMin,
                  "a fourth press is a word again -- the count starts over");
        }

        /* A press far from the last double click is a first press, not a
         * third: the run is broken by distance as well as by time. */
        {
            int x, y;
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 10);
            x = p.x + 2;
            y = p.y + 2;
            SendMessageA(t, WM_LBUTTONDBLCLK, 0, MAKELPARAM(x, y));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(x + 40, y));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(x + 40, y));
            memset(&cr, 0, sizeof cr);
            SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHECK(cr.cpMin == cr.cpMax,
                  "and a press forty pixels away is a first press: it moves "
                  "the caret and selects nothing");
        }

        /* The selection bar. It is the eight pixels ES_SELECTIONBAR adds, so
         * x = 4 is inside it and the text begins at 9. */
        {
            int row1_y;
            p.x = p.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p, 10);
            row1_y = p.y + 2;
            SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(4, row1_y));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(4, row1_y));
            memset(&cr, 0, sizeof cr);
            SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHECK(cr.cpMin >= para_start && cr.cpMax < para_end &&
                      cr.cpMax > cr.cpMin,
                  "a click in the selection bar takes the display line "
                  "beside it -- part of the paragraph, not all of it");
            SendMessageA(t, WM_LBUTTONDBLCLK, 0, MAKELPARAM(4, row1_y));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(4, row1_y));
            memset(&cr, 0, sizeof cr);
            SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHECK(cr.cpMin == para_start && cr.cpMax == para_end,
                  "and a double click there takes the whole paragraph");
        }

        /* A drag down the bar takes the lines it passes. */
        {
            POINTL a, b;
            a.x = a.y = b.x = b.y = 0;
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&a, 0);
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&b, 10);
            SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(4, a.y + 2));
            SendMessageA(t, WM_MOUSEMOVE, 0, MAKELPARAM(4, b.y + 2));
            memset(&cr, 0, sizeof cr);
            SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(4, b.y + 2));
            CHECK(cr.cpMin == 0 && cr.cpMax > 10,
                  "a drag down the selection bar takes every line it passes, "
                  "from the one it began on");
        }
        DestroyWindow(t);
    }

    {
        /* §5's dragging: a drag that stays inside one word takes characters,
         * and the moment it leaves that word it takes whole words and keeps
         * taking them -- "auto word selection", which the machine has on by
         * default. */
        HWND t = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0,
                                 300, 60, host, NULL, NULL, NULL);
        CHARRANGE cr;
        POINTL a, b, c;
        SetWindowTextA(t, "alpha bravo charlie");
        SetFocus(t);
        a.x = a.y = b.x = b.y = c.x = c.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&a, 1);  /* inside "alpha" */
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&b, 3);  /* still inside it */
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&c, 8);  /* inside "bravo" */

        SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(a.x + 1, a.y + 2));
        SendMessageA(t, WM_MOUSEMOVE, 0, MAKELPARAM(b.x + 1, b.y + 2));
        memset(&cr, 0, sizeof cr);
        SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(cr.cpMin == 1 && cr.cpMax == 3,
              "a drag inside one word takes characters, exactly as far as it "
              "has gone");

        SendMessageA(t, WM_MOUSEMOVE, 0, MAKELPARAM(c.x + 1, c.y + 2));
        memset(&cr, 0, sizeof cr);
        SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(cr.cpMin == 0 && cr.cpMax == 12,
              "and the moment it crosses into the next word it takes both "
              "whole -- \"alpha \" and \"bravo \", trailing spaces and all");

        /* Coming back inside the first word does not un-snap it. */
        SendMessageA(t, WM_MOUSEMOVE, 0, MAKELPARAM(b.x + 1, b.y + 2));
        memset(&cr, 0, sizeof cr);
        SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(cr.cpMin == 0 && cr.cpMax == 6,
              "and having snapped it keeps snapping, so coming back inside "
              "the first word still takes the whole of it");
        SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(b.x + 1, b.y + 2));

        /* A fresh press starts over: characters again until it crosses.
         *
         * Somewhere the last selection does *not* cover, because a press
         * inside a selection is the start of a drag of the text rather than
         * a new one -- which is §5 as well, and is asserted below. */
        SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(c.x + 1, c.y + 2));
        SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(c.x + 1, c.y + 2));
        SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(a.x + 1, a.y + 2));
        SendMessageA(t, WM_MOUSEMOVE, 0, MAKELPARAM(b.x + 1, b.y + 2));
        memset(&cr, 0, sizeof cr);
        SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(cr.cpMin == 1 && cr.cpMax == 3,
              "and a new press starts the snapping over rather than "
              "inheriting it");
        SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(b.x + 1, b.y + 2));
        DestroyWindow(t);
    }

    {
        /* §5's drag and drop: a press inside a selection drags the text
         * rather than starting a new selection; the run stays selected where
         * it lands; Ctrl+Z puts it back; and dropping inside the selection
         * itself changes nothing. */
        HWND t = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0,
                                 300, 60, host, NULL, NULL, NULL);
        CHARRANGE cr;
        POINTL src, dst;
        char back[64];
        SetWindowTextA(t, "alpha bravo charlie");
        SetFocus(t);
        SendMessageA(t, EM_SETSEL, 0, 6);   /* "alpha " */
        src.x = src.y = dst.x = dst.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&src, 2);   /* inside it */
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&dst, 19);  /* the very end */

        SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(src.x + 1, src.y + 2));
        memset(&cr, 0, sizeof cr);
        SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(cr.cpMin == 0 && cr.cpMax == 6,
              "a press inside a selection leaves it alone -- it may be a drag "
              "of the text, and nothing is decided until the button comes up");

        SendMessageA(t, WM_MOUSEMOVE, 0, MAKELPARAM(dst.x + 1, dst.y + 2));
        SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(dst.x + 1, dst.y + 2));
        back[0] = 0;
        GetWindowTextA(t, back, sizeof back);
        CHECK(strcmp(back, "bravo charliealpha ") == 0,
              "and dropping it elsewhere moves the run there");
        memset(&cr, 0, sizeof cr);
        SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(cr.cpMin == 13 && cr.cpMax == 19,
              "the run stays selected where it lands");

        SendMessageA(t, EM_UNDO, 0, 0);
        back[0] = 0;
        GetWindowTextA(t, back, sizeof back);
        CHECK(strcmp(back, "alpha bravo charlie") == 0,
              "and one undo puts the whole move back, both halves of it");

        /* Dropping inside the selection changes nothing at all. */
        SetWindowTextA(t, "alpha bravo charlie");
        SendMessageA(t, EM_SETSEL, 0, 6);
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&src, 1);
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&dst, 4);
        SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(src.x + 1, src.y + 2));
        SendMessageA(t, WM_MOUSEMOVE, 0, MAKELPARAM(dst.x + 1, dst.y + 2));
        SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(dst.x + 1, dst.y + 2));
        back[0] = 0;
        GetWindowTextA(t, back, sizeof back);
        memset(&cr, 0, sizeof cr);
        SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(strcmp(back, "alpha bravo charlie") == 0 && cr.cpMin == 0 &&
                  cr.cpMax == 6,
              "dropping inside the selection itself changes nothing, text or "
              "selection");

        /* A press and release with no movement is a click: it clears the
         * selection and puts the caret where it landed. */
        SendMessageA(t, EM_SETSEL, 0, 6);
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&src, 3);
        SendMessageA(t, WM_LBUTTONDOWN, 0, MAKELPARAM(src.x + 1, src.y + 2));
        SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(src.x + 1, src.y + 2));
        memset(&cr, 0, sizeof cr);
        SendMessageA(t, EM_EXGETSEL, 0, (LPARAM)&cr);
        CHECK(cr.cpMin == cr.cpMax,
              "and a press inside a selection that does not move is a click: "
              "it clears the selection");

        /* The formatting goes with the text: bold dragged elsewhere is still
         * bold, rather than taking the format of where it landed. */
        {
            CHARFORMATA cf;
            SetWindowTextA(t, "alpha bravo charlie");
            memset(&cf, 0, sizeof cf);
            cf.cbSize = sizeof cf;
            cf.dwMask = CFM_BOLD;
            cf.dwEffects = CFE_BOLD;
            SendMessageA(t, EM_SETSEL, 0, 6);
            SendMessageA(t, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&src, 2);
            SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&dst, 19);
            SendMessageA(t, WM_LBUTTONDOWN, 0,
                         MAKELPARAM(src.x + 1, src.y + 2));
            SendMessageA(t, WM_MOUSEMOVE, 0, MAKELPARAM(dst.x + 1, dst.y + 2));
            SendMessageA(t, WM_LBUTTONUP, 0, MAKELPARAM(dst.x + 1, dst.y + 2));
            memset(&cf, 0, sizeof cf);
            cf.cbSize = sizeof cf;
            cf.dwMask = CFM_BOLD;
            SendMessageA(t, EM_SETSEL, 13, 19);
            SendMessageA(t, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            CHECK((cf.dwEffects & CFE_BOLD) != 0,
                  "and the run carries its formatting with it rather than "
                  "taking that of where it landed");
        }
        DestroyWindow(t);
    }

    /* ---- an empty document's line is as tall as what would be typed ----
     *
     * jd, of WordPad: *"the cursor seems smaller than the original"*. A new
     * document has no characters, and an application sets its font on it
     * before one exists -- which lands on an empty selection, arming the
     * insert format and leaving every run alone. The line then had nothing
     * to take a height from and fell back to the control's own face.
     *
     * Measured at 96 dpi: the machine's empty-document caret is 16 tall
     * (tools/refcapture/caret-machine.png, rows 11..26) where ours was 13 --
     * Sam's capture, taken for wordpad and kept here instead, because what it
     * evidences is this control's line height rather than that program's.
     *
     * **This is a drawn test because the fault is only visible drawn.** The
     * control answers EM_POSFROMCHAR with y = 1 for character zero whatever
     * the height is, so nothing a program can ask distinguishes the two --
     * and one character of text hides it, which is why every reading anyone
     * took had text under it.
     */
    {
        /* The headless surface is one screen and `host` is the size of it, so
         * a control made beside it is drawn under it and the scan below finds
         * that window's ink instead -- 9 and 13 for two cases that differ by
         * three, which is a pair of numbers about the wrong window. */
        DestroyWindow(host);
        HWND host2 = CreateWindowExA(0, "weenrich", "h2", WS_POPUP | WS_VISIBLE,
                                     0, 0, 200, 100, NULL, NULL, NULL, NULL);
        HWND t = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0,
                                 180, 80, host2, NULL, NULL, NULL);
        SetFocus(t);
        int bare = caret_rows(t, host2);
        CHECK(bare == 13,
              "an empty document's caret is the control's own font tall");

        CHARFORMATA cf;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_FACE | CFM_SIZE;
        cf.yHeight = 200; /* 10pt in twips, what WordPad sends */
        strcpy(cf.szFaceName, "Arial");
        SendMessageA(t, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
        int arial = caret_rows(t, host2);
        CHECK(arial == 16,
              "and Arial 10 on that empty document makes it sixteen, which "
              "is the machine's");
        CHECK(arial > bare,
              "a set that lands on no characters still changes the line the "
              "caret is on");
        DestroyWindow(host2);
    }

    /* ---- more text than fits puts the bar up, and the style with it ----
     *
     * jd, of WordPad: *"when you reach the end of the editor zone no
     * scrollbar appears and you cannot write any more"*.
     *
     * **The second half of that is not what happens here and it matters.**
     * Driven with sixty lines in a hundred-pixel box, the text is not
     * refused: WM_GETTEXTLENGTH goes 530 -> 531 on a typed character, and
     * EM_SCROLLCARET lands on first-visible 54 of 61. It scrolls correctly
     * and then shows nothing, so what jd sees is **the caret below the
     * visible rows with no bar to say so** -- typing that lands invisibly.
     *
     * **The control puts the style up itself**, which is measured rather
     * than assumed and is why the two readings we had looked contradictory:
     *
     *     reference/probe/window.txt   550081C4   an empty document
     *     Sam, overflowing             552081C4   the same control, full
     *     difference                   0x00200000, WS_VSCROLL, alone
     *
     * Both true, of different moments. WordPad creates it without the style
     * (src/main.zig:959) and riched20 adds it. Neither file was wrong;
     * neither said *when*.
     */
    {
        HWND host3 = CreateWindowExA(0, "weenrich", "h3", WS_POPUP | WS_VISIBLE,
                                     0, 0, 300, 200, NULL, NULL, NULL, NULL);
        /* WordPad's own word, and the point is what is *not* in it. */
        HWND t = CreateWindowExA(WS_EX_CLIENTEDGE, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                     ES_AUTOVSCROLL | ES_WANTRETURN,
                                 0, 0, 280, 100, host3, NULL, NULL, NULL);
        char big[4000];
        int n = 0;
        for (int i = 0; i < 60; i++)
            n += sprintf(big + n, "line %d\r\n", i);

        CHECK(bar_pixels(t) == 0, "an editor that fits has no scrollbar");

        SendMessageA(t, WM_SETTEXT, 0, (LPARAM)big);
        CHECK((GetWindowLongA(t, GWL_STYLE) & WS_VSCROLL) != 0,
              "text past the bottom puts WS_VSCROLL on the control itself, "
              "which is what riched20 does rather than what its creator "
              "asked for");
        CHECK(bar_pixels(t) > 0, "and the bar is drawn");

        /* The text is not refused, which is the half of jd's sentence that
         * is not a defect here. */
        int len = (int)SendMessageA(t, WM_GETTEXTLENGTH, 0, 0);
        SendMessageA(t, EM_SETSEL, len, len);
        SendMessageA(t, WM_CHAR, 'Z', 1);
        CHECK((int)SendMessageA(t, WM_GETTEXTLENGTH, 0, 0) == len + 1,
              "and a character typed past the last visible line still lands");

        /* The third state, and it is measured rather than assumed from the
         * symmetry: a document that overflowed and was then emptied reads
         * 550081C4 again on the machine. */
        SendMessageA(t, WM_SETTEXT, 0, (LPARAM) "short");
        CHECK((GetWindowLongA(t, GWL_STYLE) & WS_VSCROLL) == 0,
              "and emptying it takes the style back off, which the machine "
              "does too");
        CHECK(bar_pixels(t) == 0, "and the bar with it");

        /* But a bar its creator asked for is not the control's to remove:
         * every machine reading is of an editor created without the style. */
        HWND asked = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                         ES_MULTILINE,
                                     0, 100, 280, 60, host3, NULL, NULL, NULL);
        SendMessageA(asked, WM_SETTEXT, 0, (LPARAM) "short");
        CHECK((GetWindowLongA(asked, GWL_STYLE) & WS_VSCROLL) != 0,
              "while a WS_VSCROLL the program asked for survives a document "
              "that does not need it");
        DestroyWindow(host3);
    }

    /* ---- undo goes back, and keeps going back ---------------------------
     *
     * **A rich edit's undo is a stack; an EDIT's is a swap.** ween32's rich
     * edit had the EDIT's -- `rich_remember` kept one string and `EM_UNDO`
     * put the current one in its place, so the second undo *redid* the
     * first. Driven before the fix, six characters typed one at a time:
     *
     *     undo 1  "abcde"      undo 2  "abcdef"      undo 3  "abcde"
     *     EM_CANUNDO answered 1 for ever
     *
     * jd found it as *"Undo"* being broken; the audit called it partial; it
     * is a one-slot toggle, which is worse than no undo because it looks
     * like undo.
     *
     * **What is measured here is that it walks back and stops.** Whether
     * riched20 groups a typed run into one step is *not* measured -- real
     * WordPad may undo a whole word where this undoes a letter -- and that
     * is recorded in the code rather than guessed at.
     */
    {
        HWND host4 = CreateWindowExA(0, "weenrich", "h4", WS_POPUP | WS_VISIBLE,
                                     0, 0, 300, 200, NULL, NULL, NULL, NULL);
        HWND t = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0,
                                 280, 100, host4, NULL, NULL, NULL);
        char buf[64];
        int i;
        SetFocus(t);
        for (i = 0; i < 6; i++)
            SendMessageA(t, WM_CHAR, (WPARAM)('a' + i), 1);
        GetWindowTextA(t, buf, sizeof buf);
        CHECK(!strcmp(buf, "abcdef"), "six characters typed");

        /* **One undo, not six.** A typed run is one step -- riched20 calls
         * it UID_TYPING, and Sam measured "hello" and "ab cd" each coming
         * back in a single undo, a space not breaking the run. This file
         * asserted one-per-character until that reading existed, on a
         * comment that said so and called it unmeasured. */
        SendMessageA(t, EM_UNDO, 0, 0);
        GetWindowTextA(t, buf, sizeof buf);
        CHECK(!strcmp(buf, ""),
              "one undo takes back the whole typed run, not the last "
              "character of it");
        CHECK(!SendMessageA(t, EM_CANUNDO, 0, 0),
              "and there is nothing behind it, because it was one step");
        SendMessageA(t, EM_UNDO, 0, 0);
        GetWindowTextA(t, buf, sizeof buf);
        CHECK(!strcmp(buf, ""), "an undo with nothing to undo changes nothing");
        DestroyWindow(host4);
    }

    /* ---- arming a format closes the typed run before it ----------------
     *
     * **Measured, and the machine is the only reason this is here.** Sam's
     * undoprobe: bold with *nothing selected*, then type -- the arming takes,
     * it pushes no undo step, and one undo takes back the typing and leaves
     * the text that was there before.
     *
     *     machine   type abc, arm bold, type XY, undo   -> "abc"
     *     ours      the same, before this                -> ""
     *
     * The arming recorded nothing, correctly, and also failed to *close* the
     * run typed before it -- so the X joined the step that began at "a" and
     * one undo swallowed the lot. **Second missing boundary of the same
     * kind**, after WM_SETTEXT. */
    {
        HWND host5 = CreateWindowExA(0, "weenrich", "h5", WS_POPUP | WS_VISIBLE,
                                     0, 0, 300, 200, NULL, NULL, NULL, NULL);
        HWND t = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0,
                                 280, 100, host5, NULL, NULL, NULL);
        CHARFORMATA cf;
        char buf[64];
        int i;
        SetFocus(t);
        for (i = 0; i < 3; i++)
            SendMessageA(t, WM_CHAR, (WPARAM)('a' + i), 1);
        /* Nothing selected, so this arms rather than applies -- and nothing
         * may look at the selection in between, which is what discards an
         * armed format on the machine. */
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;
        SendMessageA(t, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        SendMessageA(t, WM_CHAR, 'X', 1);
        SendMessageA(t, WM_CHAR, 'Y', 1);
        GetWindowTextA(t, buf, sizeof buf);
        CHECK(!strcmp(buf, "abcXY"), "an armed format lets the typing through");
        SendMessageA(t, EM_UNDO, 0, 0);
        GetWindowTextA(t, buf, sizeof buf);
        CHECK(!strcmp(buf, "abc"),
              "and one undo takes back only what was typed after the arming, "
              "because arming closes the run before it");
        DestroyWindow(host5);
    }

    /* ---- a bullet indents its first line and draws a dot ---------------
     *
     * jd's #7, and the geometry is the machine's (tools/vm/bulletprobe.txt
     * and bullet-machine.png), taken as two passes because the two disagree
     * and both are right:
     *
     *     riched20 alone   first character x 1 -> 12, the wrapped line back
     *                      to x 1, dxStartIndent and dxOffset unchanged
     *     WordPad          hangs -- but because it sets Left 0.5",
     *                      First line -0.5" itself, which is a paragraph
     *                      format this control already honours
     *
     * **So the control indents its first line and does not hang**, and it
     * reports none of it through the paragraph format. The glyph is a 5x5
     * dot with its corners off, at the paragraph's *un-indented* left edge --
     * the text moves right past it. */
    {
        HWND host6 = CreateWindowExA(0, "weenrich", "h6", WS_POPUP | WS_VISIBLE,
                                     0, 0, 200, 120, NULL, NULL, NULL, NULL);
        HWND t = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0,
                                 110, 80, host6, NULL, NULL, NULL);
        PARAFORMAT pf;
        POINTL p0, p1;
        int plain_x, second;
        SetWindowTextA(t, "the quick brown fox jumps");
        p0.x = p0.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p0, 0);
        plain_x = (int)p0.x;
        CHECK(SendMessageA(t, EM_GETLINECOUNT, 0, 0) > 1,
              "the sample wraps, which is what makes the next check mean "
              "anything");

        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_NUMBERING;
        pf.wNumbering = PFN_BULLET;
        SendMessageA(t, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
        p0.x = p0.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p0, 0);
        CHECK(p0.x == plain_x + 11,
              "a bullet moves the first line eleven pixels right");

        second = (int)SendMessageA(t, EM_LINEINDEX, 1, 0);
        p1.x = p1.y = 0;
        SendMessageA(t, EM_POSFROMCHAR, (WPARAM)&p1, (LPARAM)second);
        CHECK(p1.x == plain_x,
              "and the wrapped line goes back to the margin -- the control "
              "does not hang, whatever WordPad's own paragraph format does");

        memset(&pf, 0, sizeof pf);
        pf.cbSize = sizeof pf;
        pf.dwMask = PFM_STARTINDENT | PFM_OFFSET;
        SendMessageA(t, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        CHECK(pf.dxStartIndent == 0 && pf.dxOffset == 0,
              "and says nothing about it through the paragraph format, which "
              "is what the machine does too");
        DestroyWindow(host6);
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("richedit_test: all passed\n");
    return 0;
}
