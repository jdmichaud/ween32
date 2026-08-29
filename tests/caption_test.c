/* The caption's gradient, against the machine's own.
 *
 * `tools/refcapture/caption-400-machine.png` and its neighbours are the top
 * thirty rows of a WordPad frame, sized from inside the guest with
 * SetWindowPos, so what is in them is the machine's ramp at three widths and
 * nothing else; the fourth is the machine's WordPad at 768 and the fifth is
 * ctlprobe's own window at 460, whose class icon is NULL. Row 4 of each is a
 * caption row clear of the icon and the title, which makes it the ramp and
 * only the ramp.
 *
 * The rule under it, which cost several attempts to find and is worth
 * stating once here as well as in docs/testing.md:
 *
 *   - the start colour is held across the icon's room and the two columns
 *     past it -- eighteen at 96 dpi, and eighteen whether or not the window
 *     has an icon of its own, since the machine draws a default one there;
 *   - the ramp then runs to fifty-five pixels before the client's right
 *     edge, which is the room the three caption buttons take;
 *   - and a channel at distance d along a span is
 *
 *         start + (d * (end - start) - 1) / span
 *
 *     in whole numbers. The subtracted one changes nothing except where the
 *     division comes out exactly whole, and there the machine is a shade
 *     below: at 500 wide that is every 35th column, since 156 and 420 share
 *     twelve. The plain floor of the same division misses those and nothing
 *     else; a step worked out once and added up misses far more.
 *
 * This draws the ramp with those numbers and holds it against all five
 * captures, so a change to any part of it has to say so here.
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

/* The captures are PNG and this suite has no decoder, so what is compared is
 * the run-length of the ramp read out of the file by the harness that made
 * it -- see tools/refcapture/caption_rows.py, which writes this table. Each
 * row is one width: its columns from the caption's left edge to the last
 * before the buttons, as 0xRRGGBB. */
#include "caption_rows.h"

int main(void)
{
    setenv("WEEN32_HEADLESS", "1", 1);
    setenv("WEEN32_DPI", "96", 1);

    for (int k = 0; k < (int)(sizeof caption_rows / sizeof caption_rows[0]);
         k++) {
        const struct caption_row *r = &caption_rows[k];
        ween_surface s;
        char name[64];
        int bad = 0;
        memset(&s, 0, sizeof s);
        if (!ween_surface_init(&s, r->width, 4))
            continue;
        /* the frame is four either side, the ramp holds across eighteen and
         * ends fifty-five before the client's right edge */
        ween_classic_caption(&s, 4, 0, r->width - 8, 4, 18, 54, 1);
        for (int i = 0; i < r->n; i++) {
            unsigned got = s.px[i + 4] & 0xffffff;
            if (got != r->px[i])
                bad++;
        }
        sprintf(name, "the machine's own ramp at %d wide, every column",
                r->width);
        CHECK(bad == 0, name);
        if (bad) {
            for (int i = 0; i < r->n; i++) {
                unsigned got = s.px[i + 4] & 0xffffff;
                if (got != r->px[i]) {
                    printf("     first at column %d: machine %06x ours %06x\n",
                           i + 4, r->px[i], got);
                    break;
                }
            }
        }
        ween_surface_free(&s);
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("caption_test: all passed\n");
    return 0;
}
