/* Engine render tests (headless): the same pixel assertions the original Zig
 * tests made, plus BMP dumps for visual inspection. Uses the internal engine
 * API directly — the win32 layer is exercised by api_test.c. */

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

static const char *out_path(const char *name)
{
    static char buf[512];
    const char *dir = getenv("WEEN_TEST_OUT");
    snprintf(buf, sizeof(buf), "%s/%s", dir ? dir : ".", name);
    return buf;
}

static void test_classic_dialog(void)
{
    ween_surface s;
    ween_surface_init(&s, 320, 180);

    /* dialog body + raised frame + caption + well (mirrors classic.zig test) */
    ween_surface_clear(&s, WEEN_FACE);
    ween_classic_bevel(&s, 0, 0, s.w, s.h, 0);
    ween_classic_caption(&s, 3, 3, s.w - 6, 20);
    const ween_strike *f = ween_gui_font();
    ween_strike_draw(f, &s, 8, 8, "ween32 - classic dialog", 23, WEEN_CAP_TEXT);
    ween_surface_fill(&s, 10, 30, 300, 110, WEEN_RGBX(58, 110, 165));
    ween_classic_bevel(&s, 10, 30, 300, 110, 1);

    CHECK(s.px[0] == WEEN_WHITE, "raised frame top-left outer is white");
    CHECK(s.px[(long)(s.h - 1) * s.w + (s.w - 1)] == WEEN_DKSHADOW,
          "raised frame bottom-right outer is dark shadow");
    CHECK(s.px[(long)4 * s.w + 3] == WEEN_CAP_LEFT,
          "caption gradient starts at #0A246A");
    CHECK(s.px[(long)31 * s.w + 11] == WEEN_SHADOW,
          "well sunken inner top-left is shadow");

    ween_surface_write_bmp(&s, out_path("engine_dialog.bmp"));
    ween_surface_free(&s);
}

static void test_text(void)
{
    ween_surface s;
    ween_surface_init(&s, 380, 100);
    ween_surface_clear(&s, WEEN_FACE);
    const ween_strike *f = ween_gui_font();
    CHECK(f != NULL, "embedded Tahoma strike parses");
    const char *l1 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char *l2 = "abcdefghijklmnopqrstuvwxyz 0123456789";
    const char *l3 = "OK  Cancel  192.168.1.20:24800  1920x1080";
    ween_strike_draw(f, &s, 6, 6, l1, (int)strlen(l1), WEEN_BLACK);
    ween_strike_draw(f, &s, 6, 22, l2, (int)strlen(l2), WEEN_BLACK);
    ween_strike_draw(f, &s, 6, 38, l3, (int)strlen(l3), WEEN_BLACK);

    long black = 0;
    for (long i = 0; i < (long)s.w * s.h; i++)
        if (s.px[i] == WEEN_BLACK)
            black++;
    CHECK(black > 300, "text rendered a substantial glyph mass");
    CHECK(ween_strike_text_width(f, "OK", 2) > 5, "text width is positive");

    ween_surface_write_bmp(&s, out_path("engine_text.bmp"));
    ween_surface_free(&s);
}

static void test_marlett(void)
{
    ween_surface s;
    ween_surface_init(&s, 16, 16);
    ween_surface_clear(&s, WEEN_WHITE);
    const ween_marlett *m = ween_caption_font();
    CHECK(m != NULL, "embedded Marlett parses");
    ween_marlett_draw(m, &s, 0x72 /* close */, 2, 2, 12, WEEN_BLACK);

    long black = 0;
    for (long i = 0; i < (long)s.w * s.h; i++)
        if (s.px[i] == WEEN_BLACK)
            black++;
    CHECK(black > 20 && black < 90, "close glyph is an X, not a blob");
    CHECK(s.px[(long)8 * s.w + 8] == WEEN_BLACK, "the X's centre is filled");

    ween_surface_write_bmp(&s, out_path("engine_marlett.bmp"));
    ween_surface_free(&s);
}

static void test_dialog_units(void)
{
    LONG base = GetDialogBaseUnits();
    int bx = LOWORD(base), by = HIWORD(base);
    printf("info: dialog base units = %d x %d\n", bx, by);
    /* Tahoma 11 ppem carries the classic GUI-font metrics. */
    CHECK(bx == 6, "horizontal base unit (avg char width) is 6");
    CHECK(by == 13, "vertical base unit (font height) is 13");

    /* The standard 50x14 DLU push button maps to the classic 75x23 px. */
    int bw = MulDiv(50, bx, 4);
    int bh = MulDiv(14, by, 8);
    printf("info: 50x14 DLU button = %d x %d px\n", bw, bh);
    CHECK(bw == 75, "50 DLU wide button is 75 px");
    CHECK(bh == 23, "14 DLU tall button is 23 px");
}

static void test_xft_dpi_parser(void)
{
    CHECK(ween_parse_xft_dpi("Xft.dpi:\t120\nXft.rgba: none\n") == 120,
          "Xft.dpi parses (tab separated)");
    CHECK(ween_parse_xft_dpi("Xft.antialias: 1\nXft.dpi: 96.5\n") == 97,
          "fractional Xft.dpi rounds");
    CHECK(ween_parse_xft_dpi("Xft.dpiX: 999\nfoo: bar\n") == 0,
          "prefix keys are not mistaken for Xft.dpi");
    CHECK(ween_parse_xft_dpi("") == 0, "empty resources yield no dpi");
}

int main(void)
{
    /* Pin the dpi: these tests assert 96-dpi pixels and must not pick up the
     * desktop's Xft.dpi. The env override exists exactly for this. */
    setenv("WEEN32_DPI", "96", 1);

    test_classic_dialog();
    test_text();
    test_marlett();
    test_dialog_units();
    test_xft_dpi_parser();
    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("render_test: all passed\n");
    return 0;
}
