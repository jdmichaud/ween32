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

static void key(unsigned vk)
{
    ween_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = WEEN_EV_KEY;
    ev.vk = vk;
    ween_headless_inject(ev);
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

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("comdlg_test: all passed\n");
    return 0;
}
