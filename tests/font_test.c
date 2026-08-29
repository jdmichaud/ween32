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
    CHECK(g_count == 2, "there are two faces, which is how many strikes exist");
    CHECK(g_count == 2 && !strcmp(g_seen[0], "MS Sans Serif"),
          "the first is MS Sans Serif");
    CHECK(g_count == 2 && !strcmp(g_seen[1], "Tahoma"), "the second is Tahoma");

    /* **The property, not the names.** Each has to be its own strike; two
     * names in front of one strike is the failure this list exists to avoid. */
    {
        const ween_strike *a = ween_font_create(g_seen[0], 0, FW_NORMAL);
        const ween_strike *b = ween_font_create(g_seen[1], 0, FW_NORMAL);
        CHECK(a && b && a != b, "the two names resolve to two different strikes");
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

    CHECK(g_count == 2 && g_type[0] == RASTER_FONTTYPE &&
              g_type[1] == RASTER_FONTTYPE,
          "both are reported as raster, which is all this library has");
    CHECK(g_count == 2 && g_height[0] > 0 && g_height[1] > 0,
          "each carries its own metrics rather than a zeroed struct");

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
    return 0;
}
