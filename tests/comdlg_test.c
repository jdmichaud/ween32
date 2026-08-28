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
        FINDREPLACEA fr;
        PRINTDLGA pd;
        PAGESETUPDLGA psd;
        DOCINFOA di;
        char what[64] = "needle";

        memset(&lf, 0, sizeof lf);
        strcpy(lf.lfFaceName, "Tahoma");
        memset(&cf, 0, sizeof cf);
        cf.lStructSize = sizeof cf;
        cf.lpLogFont = &lf;
        cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS;
        CHECK(ChooseFontA(&cf) == FALSE, "there is no font dialog to open");
        CHECK(strcmp(lf.lfFaceName, "Tahoma") == 0,
              "and what the program had chosen is left as it was");

        memset(&fr, 0, sizeof fr);
        fr.lStructSize = sizeof fr;
        fr.lpstrFindWhat = what;
        fr.wFindWhatLen = sizeof what;
        fr.Flags = FR_DOWN;
        CHECK(FindTextA(&fr) == NULL, "nor a Find window to put up");
        CHECK(ReplaceTextA(&fr) == NULL, "nor a Replace one");
        CHECK(strcmp(what, "needle") == 0, "and neither touched the buffer");

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

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("comdlg_test: all passed\n");
    return 0;
}
