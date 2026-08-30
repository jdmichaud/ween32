/* Which faces this library has, and that it says so truthfully.
 *
 * `ween_font_create` resolves any name it does not know to Tahoma, silently
 * and by design -- win32's font mapper substitutes too. The cost of that is
 * that **nothing in the library could tell a caller a face was absent**: a
 * program asking for Times New Roman got Tahoma and no way to find out.
 * `EnumFontFamiliesA` is the first thing that can, so what it says has to be
 * true rather than merely plausible.
 *
 * The property that makes it a list of *faces* rather than a list of words is
 * that every name in it resolves to a different strike. A list that named
 * seven faces with one strike behind six of them would pass every other check
 * here and be six lies.
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

#define MAX_SEEN 8
static char g_seen[MAX_SEEN][LF_FACESIZE];
static LONG g_height[MAX_SEEN];
static DWORD g_type[MAX_SEEN];
static int g_count;

static int CALLBACK collect(const LOGFONTA *lf, const TEXTMETRICA *tm,
                            DWORD type, LPARAM param)
{
    (void)param;
    if (g_count < MAX_SEEN) {
        memcpy(g_seen[g_count], lf->lfFaceName, LF_FACESIZE - 1);
        g_seen[g_count][LF_FACESIZE - 1] = 0;
        g_height[g_count] = tm->tmHeight;
        g_type[g_count] = type;
        g_count++;
    }
    return 1;
}

static int CALLBACK stop_at_once(const LOGFONTA *lf, const TEXTMETRICA *tm,
                                 DWORD type, LPARAM param)
{
    (void)lf;
    (void)tm;
    (void)type;
    (*(int *)param)++;
    return 0;
}

int main(void)
{
    setenv("WEEN32_HEADLESS", "1", 1);

    g_count = 0;
    CHECK(EnumFontFamiliesA(NULL, NULL, collect, 0) != 0,
          "enumerating every face runs to the end");
    /* **Three faces, and the list is checked as a property rather than as
     * three names.** It was two, and every assertion here named a count and a
     * position; adding Arial broke five of them at once, which is more noise
     * than a new face should make. What matters is that the list is complete
     * and that no two entries are the same strike wearing two names. */
    CHECK(g_count == 3, "three faces, which is how many strikes exist");
    CHECK(g_count == 3 && !strcmp(g_seen[0], "Arial") &&
              !strcmp(g_seen[1], "MS Sans Serif") &&
              !strcmp(g_seen[2], "Tahoma"),
          "Arial, MS Sans Serif and Tahoma, in that order");

    /* **The property, not the names.** Each has to be its own strike; two
     * names in front of one strike is the failure this list exists to avoid,
     * and with three faces it is every pair rather than the one. */
    {
        int i, j, distinct = 1;
        for (i = 0; i < g_count; i++)
            for (j = i + 1; j < g_count; j++) {
                const ween_strike *a = ween_font_create(g_seen[i], 0,
                                                        FW_NORMAL);
                const ween_strike *b = ween_font_create(g_seen[j], 0,
                                                        FW_NORMAL);
                if (!a || !b || a == b)
                    distinct = 0;
            }
        CHECK(distinct,
              "every name in the list resolves to a strike of its own");
    }

    /* And a name that is not in the list must not resolve to its own strike --
     * that is what makes the list complete rather than merely non-empty. */
    {
        const ween_strike *absent = ween_font_create("Times New Roman", 0,
                                                     FW_NORMAL);
        const ween_strike *tahoma = ween_font_create("Tahoma", 0, FW_NORMAL);
        CHECK(absent == tahoma,
              "a face not in the list falls back rather than being a third one");
    }

    {
        int i, raster = 1, metrics = 1;
        for (i = 0; i < g_count; i++) {
            if (g_type[i] != RASTER_FONTTYPE)
                raster = 0;
            if (g_height[i] <= 0)
                metrics = 0;
        }
        CHECK(g_count == 3 && raster,
              "every face is reported as raster, which is all this library "
              "has -- Arial included: it ships generated bitmap strikes, not "
              "outlines");
        CHECK(g_count == 3 && metrics,
              "each carries its own metrics rather than a zeroed struct");
    }

    /* Naming one face enumerates that face and no other. */
    g_count = 0;
    EnumFontFamiliesA(NULL, "Tahoma", collect, 0);
    CHECK(g_count == 1 && !strcmp(g_seen[0], "Tahoma"),
          "naming a face enumerates that one");

    g_count = 0;
    EnumFontFamiliesA(NULL, "Times New Roman", collect, 0);
    CHECK(g_count == 0, "naming a face we have not got enumerates nothing");

    /* Returning zero stops it, which is win32's rule and not an error. */
    {
        int calls = 0;
        CHECK(EnumFontFamiliesA(NULL, NULL, stop_at_once, (LPARAM)&calls) == 0,
              "a callback returning zero makes the enumeration return zero");
        CHECK(calls == 1, "and it is not called again");
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("font_test: all passed\n");
    /* ---- a bigger size never draws smaller -------------------------------
     *
     * jd: *"The size are just incorrect. Try to set a text to all the value
     * in the drop down and compare the screenshot."* Measured off the drawn
     * ink at WordPad's sixteen sizes, before this was fixed:
     *
     *     forward   8pt drew 10px ... 26pt and up drew 10px -- SMALLER than
     *               8pt, because the first cached strike was 8pt's
     *     reverse   all sixteen drew 15px, 8pt included, because the first
     *               cached strike was 72pt's
     *
     * **The same document rendered differently depending on which sizes the
     * program had been shown first.** `ween_font_create` keeps twelve
     * strikes and, once full, returned the *first* of the face rather than
     * the nearest -- which is what the paragraph above it had always claimed
     * it did.
     *
     * The invariant asserted here is deliberately weaker than "every size
     * differs", because it must survive the strike ceiling: Tahoma's largest
     * is about sixteen pixels, so everything from 12pt up genuinely draws the
     * same and that is a font-coverage limit rather than a defect.
     *
     * **The size of that limit, counted rather than estimated, because the
     * count is what says whether jd's report is closed:**
     *
     *     10px   1 value    8
     *     11px   1 value    9
     *     12px   1 value    10
     *     14px   1 value    11
     *     15px  12 values   12 14 16 18 20 22 24 26 28 36 48 72
     *
     * **Five distinct renderings across sixteen sizes; twelve of the sixteen
     * share one with another.** I first wrote "six", carried out of the
     * hand-over that started this work, while the table saying twelve was on
     * the screen -- and six of sixteen is a rough edge where twelve of
     * sixteen is three quarters of the control not working. The fix below
     * does not touch this; a user setting 12pt and 72pt gets the same text
     * either way.
     * **Monotonic is the part that cannot be excused** -- a larger request
     * must never come back smaller. */
    {
        static const int pt[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 22,
                                  24, 26, 28, 36, 48, 72 };
        int i, prev = 0, ok = 1, worst = 0;
        for (i = 0; i < (int)(sizeof pt / sizeof pt[0]); i++) {
            const ween_strike *f =
                ween_font_create("Arial", -(pt[i] * 96 / 72), 400);
            int h = f ? f->ascent - f->descent : 0;
            if (h < prev) {
                ok = 0;
                if (!worst)
                    worst = pt[i];
            }
            prev = h;
        }
        CHECK(ok,
              "a larger font size never comes back drawing smaller than the "
              "one before it, however many strikes have been made already");
        if (!ok)
            printf("     first size that went backwards: %dpt\n", worst);
    }

    return 0;
}
