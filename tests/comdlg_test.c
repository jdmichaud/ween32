/* The colour box, driven the way a person drives it.
 *
 * ChooseColorA is a dialog with four controls of its own in it — two grids
 * of squares, the hue field, the brightness bar — and what they do to each
 * other is where the behaviour lives. The clicks below are the ones the
 * machine was measured with: pick a custom square, mix a colour, add it, and
 * see which of the sixteen it landed in.
 *
 * Every event is queued before the modal loop starts, as the other dialog
 * tests do: a run that reaches the end of its events quits. */

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

/* Dialog coordinates, from the machine's own probe of Edit Colors: the
 * squares are twenty by seventeen on a pitch of twenty-five and twenty-two,
 * three pixels in from each grid's corner. */
#define DLG_BASIC_X 12
#define DLG_BASIC_Y 48
#define DLG_CUSTOM_X 12
#define DLG_CUSTOM_Y 214
#define DLG_DEFINE_X 114
#define DLG_DEFINE_Y 277
#define DLG_ADD_X 339
#define DLG_ADD_Y 303
#define DLG_FIELD_X 232
#define DLG_FIELD_Y 30
#define DLG_LUM_X 429

static void click(int x, int y)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_MOUSE_DOWN;
    ev.button = 1;
    ev.x = x;
    ev.y = y;
    ween_headless_inject(ev);
    ev.kind = WEEN_EV_MOUSE_UP;
    ween_headless_inject(ev);
}

/* Press, walk, release: the colour field and the brightness bar are dragged
 * as much as they are clicked. */
static void drag(int x0, int y0, int x1, int y1)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_MOUSE_DOWN;
    ev.button = 1;
    ev.x = x0;
    ev.y = y0;
    ween_headless_inject(ev);
    ev.kind = WEEN_EV_MOUSE_MOVE;
    ev.x = (x0 + x1) / 2;
    ev.y = (y0 + y1) / 2;
    ween_headless_inject(ev);
    ev.x = x1;
    ev.y = y1;
    ween_headless_inject(ev);
    ev.kind = WEEN_EV_MOUSE_UP;
    ween_headless_inject(ev);
}

static void key(unsigned vk)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_KEY;
    ev.vk = vk;
    ween_headless_inject(ev);
}

/* Every place and size the hook saw the box in. */
static int g_seen, g_moved;
static int g_at_x, g_at_y, g_min_w, g_max_w;

/* What the owner of a Find or Replace box hears. */
static UINT g_fr_msg;
static int g_fr_told;
static DWORD g_fr_flags;
static const FINDREPLACEA *g_fr_ptr;

/* The hook a program installs on the Font box, which is how this test looks
 * inside a modal dialog and presses its buttons. */
static int g_cf_seen, g_cf_ok, g_cf_press, g_cf_check_sizes;
static int g_cf_sizes_tahoma, g_cf_sizes_sans;

static INT_PTR CALLBACK cf_hook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp;
    (void)lp;
    if (msg != WM_INITDIALOG)
        return 0;
    g_cf_seen = 1;
    g_cf_ok = 0;
    {
        char buf[64] = "";
        GetDlgItemTextA(dlg, 1136, buf, sizeof buf);
        if (strcmp(buf, "Tahoma") == 0)
            g_cf_ok |= 1;
        GetDlgItemTextA(dlg, 1138, buf, sizeof buf);
        if (strcmp(buf, "12") == 0)
            g_cf_ok |= 2;
        if (SendDlgItemMessageA(dlg, 1137, CB_GETCURSEL, 0, 0) == 3)
            g_cf_ok |= 4;
        if (IsDlgButtonChecked(dlg, 1041) && !IsDlgButtonChecked(dlg, 1040))
            g_cf_ok |= 8;
    }
    if (g_cf_check_sizes) {
        int i;
        g_cf_sizes_tahoma = (int)SendDlgItemMessageA(dlg, 1138, CB_GETCOUNT, 0,
                                                     0);
        i = (int)SendDlgItemMessageA(dlg, 1136, CB_FINDSTRINGEXACT,
                                     (WPARAM)-1, (LPARAM) "MS Sans Serif");
        SendDlgItemMessageA(dlg, 1136, CB_SETCURSEL, (WPARAM)i, 0);
        SendMessageA(dlg, WM_COMMAND, MAKEWPARAM(1136, CBN_SELCHANGE),
                     (LPARAM)GetDlgItem(dlg, 1136));
        g_cf_sizes_sans = (int)SendDlgItemMessageA(dlg, 1138, CB_GETCOUNT, 0,
                                                   0);
    }
    /* press what the test wants pressed, once the box is up */
    PostMessageA(dlg, WM_COMMAND, MAKEWPARAM(g_cf_press, BN_CLICKED),
                 (LPARAM)GetDlgItem(dlg, g_cf_press));
    return 0; /* the box may put the focus where it likes */
}

static LRESULT CALLBACK fr_owner_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_fr_msg && msg == g_fr_msg) {
        const FINDREPLACEA *fr = (const FINDREPLACEA *)lp;
        g_fr_told++;
        g_fr_ptr = fr;
        g_fr_flags = fr ? fr->Flags : 0;
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static INT_PTR CALLBACK watch_hook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)msg;
    (void)wp;
    (void)lp;
    if (!g_seen++) {
        g_at_x = dlg->x;
        g_at_y = dlg->y;
        g_min_w = g_max_w = dlg->w;
    }
    if (dlg->x != g_at_x || dlg->y != g_at_y)
        g_moved = 1;
    if (dlg->w < g_min_w)
        g_min_w = dlg->w;
    if (dlg->w > g_max_w)
        g_max_w = dlg->w;
    return 0; /* and the box carries on with it */
}

static void custom_click(int row, int col)
{
    click(DLG_CUSTOM_X + col * 25 + 8, DLG_CUSTOM_Y + row * 22 + 8);
}

int main(void)
{
    setenv("WEEN32_HEADLESS", "1", 1);
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    COLORREF custom[16];
    for (int i = 0; i < 16; i++)
        custom[i] = RGB(255, 255, 255);

    CHOOSECOLORA cc;
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.lpCustColors = custom;
    cc.rgbResult = RGB(0, 0, 0);
    cc.Flags = CC_RGBINIT;

    /* Open it, show the definition half, pick the third custom square — one
     * of sixteen identical white ones — mix a colour and add it twice. */
    click(DLG_DEFINE_X, DLG_DEFINE_Y);
    custom_click(0, 2);
    click(DLG_FIELD_X + 42, DLG_FIELD_Y + 31); /* hue 57, saturation 200 */
    click(DLG_LUM_X, DLG_FIELD_Y + 82);        /* and a luminosity to see it */
    click(DLG_ADD_X, DLG_ADD_Y);
    click(DLG_ADD_X, DLG_ADD_Y);
    key(VK_ESCAPE);

    BOOL ok = ChooseColorA(&cc);
    CHECK(!ok, "Escape cancels the colour box");

    /* The white square that was clicked is the one that took the colour.
     * Picking it out by colour instead would have marked all sixteen, and
     * the add would have gone to whichever came first. */
    COLORREF mixed = custom[4];
    CHECK(mixed != RGB(255, 255, 255),
          "the custom square that was clicked is the one that filled");
    CHECK(custom[0] == RGB(255, 255, 255) && custom[2] == RGB(255, 255, 255),
          "and no other did");
    /* The sixteen run down each column of the eight-by-two grid, so the
     * square under the first is the next one along in the array. */
    CHECK(custom[5] == mixed, "a second add walks on to the next square");
    CHECK(custom[6] == RGB(255, 255, 255), "and no further than that");
    /* The colour itself: hue and saturation from the field, luminosity from
     * the bar beside it. */
    CHECK(mixed == RGB(157, 236, 51),
          "the colour is the one the field and the bar were left on");

    /* Cancelled or not, the sixteen go back to the caller: a colour mixed
     * and added is kept. */
    CHECK(cc.lpCustColors[4] == mixed,
          "the custom colours come back even from a cancelled box");

    /* The box is built at its full width and shrunk to the left half before
     * it is ever shown, so everything below depends on the backend being
     * told at once that the window is a different size. A backend that waits
     * to be told centres what is drawn inside the size the window was opened
     * at: on a display the picture is pushed right by half the difference
     * and cut off at the edge, and a press lands a hundred and fifteen
     * pixels from where it was aimed. Cancel is the rightmost thing in the
     * shut box, so it is the one that says. */
    click(118, 303); /* Cancel */
    BOOL cancelled = ChooseColorA(&cc);
    CHECK(!cancelled, "Cancel is where it is drawn after the box shrinks");

    click(46, 303); /* OK */
    cc.rgbResult = RGB(0, 0, 0);
    CHECK(ChooseColorA(&cc), "and so is OK");

    /* Widening it must not move it. The box asks to be wider and nothing
     * else, which matters on a display with a window manager: a window there
     * is where the manager put it rather than where it asked to be, so a box
     * that reads its own position back and writes it again walks across the
     * screen by the width of its own frame every time it is opened. The
     * fake window system is told to place windows somewhere of its own here,
     * which is what makes the two positions differ at all. */
    ween_headless_set_window_origin(120, 90);
    cc.Flags = CC_RGBINIT | CC_ENABLEHOOK;
    cc.lpfnHook = watch_hook;
    click(DLG_DEFINE_X, DLG_DEFINE_Y);
    key(VK_ESCAPE);
    ChooseColorA(&cc);
    CHECK(g_min_w < g_max_w, "Define Custom Colors widened the box");
    CHECK(!g_moved, "and left it where it was");
    ween_headless_set_window_origin(0, 0);

    /* A drag in the field ends on the colour it ends on, which is the same
     * colour a click there would have given. Without the buttons in a mouse
     * message's wParam nothing can tell a drag from a pointer wandering
     * across, and the cross would only ever move on the press. */
    cc.Flags = CC_RGBINIT;
    cc.lpfnHook = NULL;
    click(DLG_DEFINE_X, DLG_DEFINE_Y);
    click(DLG_LUM_X, DLG_FIELD_Y + 82);              /* a luminosity to see */
    click(DLG_FIELD_X + 98, DLG_FIELD_Y + 90);       /* clicked there */
    click(46, 303);                                  /* OK */
    ChooseColorA(&cc);
    COLORREF clicked = cc.rgbResult;

    cc.rgbResult = RGB(0, 0, 0);
    click(DLG_DEFINE_X, DLG_DEFINE_Y);
    click(DLG_LUM_X, DLG_FIELD_Y + 82);
    drag(DLG_FIELD_X + 42, DLG_FIELD_Y + 31,         /* dragged there */
         DLG_FIELD_X + 98, DLG_FIELD_Y + 90);
    click(46, 303);
    ChooseColorA(&cc);
    CHECK(cc.rgbResult == clicked && clicked != RGB(0, 0, 0),
          "the field follows a drag, and ends where a click there would");

    /* The same for the bar beside it. */
    cc.rgbResult = RGB(0, 0, 0);
    click(DLG_DEFINE_X, DLG_DEFINE_Y);
    click(DLG_FIELD_X + 98, DLG_FIELD_Y + 90);
    click(DLG_LUM_X, DLG_FIELD_Y + 120);
    click(46, 303);
    ChooseColorA(&cc);
    COLORREF bar_clicked = cc.rgbResult;

    cc.rgbResult = RGB(0, 0, 0);
    click(DLG_DEFINE_X, DLG_DEFINE_Y);
    click(DLG_FIELD_X + 98, DLG_FIELD_Y + 90);
    drag(DLG_LUM_X, DLG_FIELD_Y + 20, DLG_LUM_X, DLG_FIELD_Y + 120);
    click(46, 303);
    ChooseColorA(&cc);
    CHECK(cc.rgbResult == bar_clicked && bar_clicked != RGB(0, 0, 0),
          "and so does the brightness bar");

    /* ---- the ones that are not here yet ----
     *
     * These exist so that a program with a Font, a Find or a Print item on
     * its menus can be built at all. What is checked is that each answers the
     * way a cancelled dialog answers, and writes nothing back -- so a program
     * checking the return, as every program does, carries on correctly rather
     * than acting on a structure nobody filled in. */
    {
        LOGFONTA lf;
        CHOOSEFONTA cf;
        PRINTDLGA pd;
        PAGESETUPDLGA psd;
        DOCINFOA di;

        memset(&lf, 0, sizeof lf);
        strcpy(lf.lfFaceName, "Tahoma");
        memset(&cf, 0, sizeof cf);
        cf.lStructSize = sizeof cf;
        cf.lpLogFont = &lf;
        cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS;
        CHECK(ChooseFontA(&cf) == FALSE, "there is no font dialog to open");
        CHECK(strcmp(lf.lfFaceName, "Tahoma") == 0,
              "and what the program had chosen is left as it was");

        memset(&pd, 0, sizeof pd);
        pd.lStructSize = sizeof pd;
        pd.Flags = PD_RETURNDC;
        pd.hDC = (HDC)(UINT_PTR)0x1234; /* whatever was there before */
        CHECK(PrintDlgA(&pd) == FALSE, "there is no printer to choose");
        CHECK(pd.hDC == NULL,
              "and no device context, cleared rather than left as rubbish");

        memset(&psd, 0, sizeof psd);
        psd.lStructSize = sizeof psd;
        CHECK(PageSetupDlgA(&psd) == FALSE, "nor a page to set up");

        memset(&di, 0, sizeof di);
        di.cbSize = sizeof di;
        di.lpszDocName = "nothing";
        CHECK(StartDocA(NULL, &di) <= 0, "a document cannot be started");
        CHECK(StartPage(NULL) <= 0 && EndPage(NULL) <= 0 && EndDoc(NULL) <= 0,
              "and the calls that would page it all fail the same way");
    }

    /* Files dropped on a window: a window can say it takes them, and nothing
     * can drop yet, so a handle nobody handed out names no files. */
    {
        HWND w = CreateWindowExA(0, "STATIC", "drop", WS_POPUP, 0, 0, 40, 20,
                                 NULL, NULL, NULL, NULL);
        char name[32] = "x";
        DragAcceptFiles(w, TRUE);
        CHECK(DragQueryFileA(NULL, 0, name, sizeof name) == 0,
              "no files have been dropped, there being no way to drop any");
        CHECK(name[0] == 0, "and the name comes back empty rather than unset");
        DragFinish(NULL);
        DestroyWindow(w);
    }

    /* ---- Find and Replace ----
     *
     * Modeless: the call puts a window up and answers with it, and every
     * press reaches the owner as RegisterWindowMessage(FINDMSGSTRING) with
     * the FINDREPLACE the program handed over. What the box does not do is
     * search -- so what is checked here is what it tells the program, and
     * that what was typed is in the program's own buffer when it is told. */
    {
        static char what[64] = "needle";
        static char with[64] = "";
        FINDREPLACEA fr;
        HWND owner, dlg;
        WNDCLASSA fc;

        memset(&fc, 0, sizeof fc);
        fc.lpfnWndProc = fr_owner_proc;
        fc.lpszClassName = "weenfrowner";
        RegisterClassA(&fc);
        owner = CreateWindowExA(0, "weenfrowner", "owner",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 400,
                                200, NULL, NULL, NULL, NULL);
        g_fr_msg = RegisterWindowMessageA(FINDMSGSTRING);
        CHECK(g_fr_msg >= 0xC000, "FINDMSGSTRING has a message number");

        memset(&fr, 0, sizeof fr);
        fr.lStructSize = sizeof fr;
        fr.hwndOwner = owner;
        fr.lpstrFindWhat = what;
        fr.wFindWhatLen = sizeof what;
        fr.Flags = FR_DOWN | FR_MATCHCASE;
        dlg = FindTextA(&fr);
        CHECK(dlg != NULL, "the Find box comes up");
        {
            /* And the owner goes grey behind it, because the keyboard has
             * left. Painted now so that the check after the box closes is
             * comparing against a caption that really was repainted grey. */
            struct ween_wnd *w = (struct ween_wnd *)owner;
            ween_flush_paint();
            CHECK(w->surface.px &&
                      (w->surface.px[(size_t)6 * w->surface.w + 6] & 0xffffff) ==
                          WEEN_CAP_INACT_LEFT,
                  "and the window behind it wears the caption of one that is "
                  "not active");
        }
        CHECK(GetDlgItem(dlg, 0x0480) && GetDlgItem(dlg, 0x0410) &&
                  GetDlgItem(dlg, 0x0420) && GetDlgItem(dlg, 0x0421),
              "with the controls win32 gives it, under win32's own ids");
        {
            char buf[64] = "";
            GetDlgItemTextA(dlg, 0x0480, buf, sizeof buf);
            CHECK(strcmp(buf, "needle") == 0,
                  "the field starts with what the program was looking for");
        }
        CHECK(IsDlgButtonChecked(dlg, 0x0410) == BST_CHECKED,
              "and the tick with the flag it was given");
        CHECK(IsDlgButtonChecked(dlg, 0x0421) == BST_CHECKED,
              "FR_DOWN is the Down radio, as it is on the machine");
        CHECK(IsWindowEnabled(GetDlgItem(dlg, IDOK)),
              "Find Next can be pressed while there is something to find");

        /* Emptying the field greys it, which is what the machine shows the
         * moment the box comes up with nothing in it. Typed rather than set:
         * a field that is *given* its text is a different path, and it is
         * the user's that has to work. */
        {
            HWND field = GetDlgItem(dlg, 0x0480);
            SendMessageA(field, EM_SETSEL, 0, (LPARAM)-1);
            for (int i = 0; i < 8; i++)
                SendMessageA(field, WM_KEYDOWN, VK_DELETE, 0);
            CHECK(!IsWindowEnabled(GetDlgItem(dlg, IDOK)),
                  "and cannot while the field is empty");
            SendMessageA(field, WM_CHAR, (WPARAM)'h', 0);
            CHECK(IsWindowEnabled(GetDlgItem(dlg, IDOK)),
                  "and can again the moment something is typed");
            for (const char *p2 = "aystack"; *p2; p2++)
                SendMessageA(field, WM_CHAR, (WPARAM)*p2, 0);
        }

        /* Three columns of the machine's own Find box, kept here so that
         * none of them can drift back without a test saying so. They are
         * the capture's (tools/refcapture/find-machine.png) and the guest's
         * own GetWindowRect agrees with them: the two option buttons are at
         * dialog units 111 and 138, which put their controls at columns 170
         * and 210, and each circle begins one column inside its control.
         *
         * The "h" just typed begins at column 78, one pixel inside the
         * field's border, where half an average character would have put it
         * at 81 -- a field is given no margin of its own in a face that
         * stands in for a bitmap font. */
        {
            struct ween_wnd *w = (struct ween_wnd *)dlg;
            unsigned face1 = 0, rim1 = 0, face2 = 0, rim2 = 0;
            int ink = 0;
            ween_flush_paint();
            if (w->surface.px && w->surface.w >= 360 && w->surface.h >= 126) {
                size_t row = (size_t)91 * w->surface.w;
                face1 = w->surface.px[row + 170] & 0xffffff;
                rim1 = w->surface.px[row + 171] & 0xffffff;
                face2 = w->surface.px[row + 210] & 0xffffff;
                rim2 = w->surface.px[row + 211] & 0xffffff;
                for (int y = 36; y < 49 && !ink; y++)
                    for (int x = 76; x < 100; x++)
                        if ((w->surface.px[(size_t)y * w->surface.w + x] &
                             0xffffff) == WEEN_BLACK) {
                            ink = x;
                            break;
                        }
            }
            CHECK(face1 == WEEN_FACE && rim1 == WEEN_SHADOW,
                  "the Up circle begins one column inside its control, as "
                  "the machine draws it");
            CHECK(face2 == WEEN_FACE && rim2 == WEEN_SHADOW,
                  "and the Down circle does the same forty pixels along");
            CHECK(ink == 78,
                  "and the field's text starts where the machine starts it, "
                  "one pixel inside the border");
        }

        /* The press, and what the owner is told. */
        g_fr_told = 0;
        g_fr_flags = 0;
        CheckDlgButton(dlg, 0x0410, BST_UNCHECKED);
        CheckRadioButton(dlg, 0x0420, 0x0421, 0x0420); /* Up */
        SendMessageA(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
                     (LPARAM)GetDlgItem(dlg, IDOK));
        CHECK(g_fr_told == 1, "pressing Find Next tells the owner, once");
        CHECK(g_fr_ptr == &fr, "with the FINDREPLACE it was given");
        CHECK((g_fr_flags & FR_FINDNEXT) != 0, "and FR_FINDNEXT set");
        CHECK(strcmp(what, "haystack") == 0,
              "what was typed is in the program's own buffer");
        CHECK((g_fr_flags & FR_DOWN) == 0 && (g_fr_flags & FR_MATCHCASE) == 0,
              "and the direction and the case are the controls', not the "
              "flags it was opened with");

        /* Closing it: the program is told before the window goes, since what
         * it does with that is forget the handle it is holding. */
        g_fr_told = 0;
        SendMessageA(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED),
                     (LPARAM)GetDlgItem(dlg, IDCANCEL));
        CHECK(g_fr_told == 1 && (g_fr_flags & FR_DIALOGTERM) != 0,
              "Cancel says FR_DIALOGTERM");

        /* And the window that takes the keyboard back is repainted, because
         * a caption is how a person is told which window has it: with the
         * state right and the pixels stale, Notepad sat behind a closed box
         * still wearing the grey caption. */
        {
            struct ween_wnd *w = (struct ween_wnd *)owner;
            ween_flush_paint();
            CHECK(GetActiveWindow() == owner,
                  "the owner is active again once the box has gone");
            CHECK(w->surface.px &&
                      (w->surface.px[(size_t)6 * w->surface.w + 6] & 0xffffff) ==
                          WEEN_CAP_LEFT,
                  "and its caption is drawn in the colour of one that is");
        }

        /* Replace is the same box with another field and two more buttons. */
        memset(&fr, 0, sizeof fr);
        fr.lStructSize = sizeof fr;
        fr.hwndOwner = owner;
        fr.lpstrFindWhat = what;
        fr.wFindWhatLen = sizeof what;
        fr.lpstrReplaceWith = with;
        fr.wReplaceWithLen = sizeof with;
        fr.Flags = FR_DOWN;
        dlg = ReplaceTextA(&fr);
        CHECK(dlg != NULL, "the Replace box comes up too");
        CHECK(GetDlgItem(dlg, 0x0481) && GetDlgItem(dlg, 0x0400) &&
                  GetDlgItem(dlg, 0x0401),
              "with the second field and the two buttons Find has not got");
        SetDlgItemTextA(dlg, 0x0480, "old");
        SetDlgItemTextA(dlg, 0x0481, "new");
        g_fr_told = 0;
        SendMessageA(dlg, WM_COMMAND, MAKEWPARAM(0x0400, BN_CLICKED),
                     (LPARAM)GetDlgItem(dlg, 0x0400));
        CHECK(g_fr_told == 1 && (g_fr_flags & FR_REPLACE) != 0,
              "Replace says so");
        CHECK(strcmp(what, "old") == 0 && strcmp(with, "new") == 0,
              "and both buffers come back filled");
        g_fr_told = 0;
        SendMessageA(dlg, WM_COMMAND, MAKEWPARAM(0x0401, BN_CLICKED),
                     (LPARAM)GetDlgItem(dlg, 0x0401));
        CHECK(g_fr_told == 1 && (g_fr_flags & FR_REPLACEALL) != 0,
              "and Replace All says something else");

        /* One at a time, which is what a program keeping a single handle
         * expects: opening the other takes this one down. */
        {
            HWND second = FindTextA(&fr);
            CHECK(second != NULL && second != dlg,
                  "opening Find while Replace is up gives a new window");
            DestroyWindow(second);
        }
        DestroyWindow(owner);
    }

    /* ---- ChooseFont ----
     *
     * Every rectangle in the box is the machine's own, out of the probe's
     * walk of a running Font dialog; what its lists hold is ours, because
     * this library has no rasteriser and offering a face it would then draw
     * in another one would be worse than none. Both halves are in
     * docs/testing.md.
     *
     * The box is modal, so the test drives it through the hook a program
     * would use -- which also checks that the hook works at all. */
    {
        LOGFONTA lf;
        CHOOSEFONTA cf;
        BOOL ok;

        memset(&lf, 0, sizeof lf);
        strcpy(lf.lfFaceName, "Tahoma");
        lf.lfHeight = -16; /* twelve points at 96 dpi */
        lf.lfWeight = 700;
        lf.lfItalic = 1;
        lf.lfUnderline = 1;
        memset(&cf, 0, sizeof cf);
        cf.lStructSize = sizeof cf;
        cf.hwndOwner = NULL;
        cf.lpLogFont = &lf;
        cf.rgbColors = RGB(255, 0, 0);
        cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_EFFECTS |
                   CF_ENABLEHOOK;
        cf.lpfnHook = cf_hook;
        g_cf_press = IDOK;
        ok = ChooseFontA(&cf);
        CHECK(ok == TRUE, "the Font box comes up and OK closes it");
        CHECK(g_cf_seen,
              "the hook a program installs is called, WM_INITDIALOG and all");
        CHECK(g_cf_ok & 1,
              "the face the program asked for is the one selected");
        CHECK(g_cf_ok & 2,
              "the size its height works out to is chosen, not the first in "
              "the list");
        CHECK(g_cf_ok & 4, "the style is Bold Italic, being weight and slant");
        CHECK(g_cf_ok & 8, "and the effects are ticked as they were given");
        CHECK(strcmp(lf.lfFaceName, "Tahoma") == 0 && lf.lfHeight == -16 &&
                  lf.lfWeight == 700 && lf.lfItalic && lf.lfUnderline,
              "what comes back is what went in, nobody having changed it");
        CHECK(cf.iPointSize == 120,
              "with the size in tenths of a point, which is the one place "
              "this structure states one that way");

        lf.lfWeight = 400;
        g_cf_press = IDCANCEL;
        ok = ChooseFontA(&cf);
        CHECK(ok == FALSE && lf.lfWeight == 400,
              "Cancel answers FALSE and writes nothing back");

        g_cf_press = IDCANCEL;
        g_cf_check_sizes = 1;
        ChooseFontA(&cf);
        g_cf_check_sizes = 0;
        CHECK(g_cf_sizes_tahoma == 7 && g_cf_sizes_sans == 6,
              "each face offers the sizes its own strikes hold -- seven for "
              "Tahoma and six for MS Sans Serif -- the way a bitmap face's "
              "own sizes are what the machine's box lists");
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("comdlg_test: all passed\n");
    return 0;
}
