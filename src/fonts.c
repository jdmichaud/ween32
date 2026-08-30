/* The built-in stock fonts: Wine's redistributable Tahoma (the classic UI
 * face, 11 ppem bitmap strikes = DEFAULT_GUI_FONT), its MS Sans Serif (which
 * is what "MS Shell Dlg" means on this Windows, and so what every dialog the
 * shell puts up is lettered in), and Marlett (the caption glyphs). Embedded
 * so the library needs no files at run time; all are lazily parsed on first
 * use. */

#include <string.h>

#include "ween_internal.h"

#include "../fonts/arial_ttf.h"
#include "../fonts/marlett_ttf.h"
#include "../fonts/mssans_ttf.h"
#include "../fonts/tahoma_ttf.h"
#include "../fonts/tahomabd_ttf.h"

/* The GUI font is 8pt Tahoma sized against the system dpi, GDI-style:
 * ppem = MulDiv(8, dpi, 72) -> 11px @96, 13px @120, 16px @144 — snapped to
 * the strikes the font actually carries (a between-strike size would fall to
 * rough outline rendering). Ties snap up, Large Fonts style. */
static int gui_ppem(void)
{
    static const int strikes[] = { 9, 10, 11, 12, 13, 15, 16 };
    int want = MulDiv(8, ween_render_dpi(), 72);
    int best = strikes[0], bd = 1 << 30;
    for (size_t i = 0; i < sizeof(strikes) / sizeof(strikes[0]); i++) {
        int d = want > strikes[i] ? want - strikes[i] : strikes[i] - want;
        if (d < bd || (d == bd && strikes[i] > best)) {
            bd = d;
            best = strikes[i];
        }
    }
    return best;
}

const ween_strike *ween_gui_font(void)
{
    static ween_strike f;
    static int ready = 0;
    if (!ready)
        ready = ween_strike_init(&f, ween_tahoma_ttf, ween_tahoma_ttf_len,
                                 gui_ppem());
    return ready ? &f : NULL;
}

/* MS Sans Serif at the size a dialog asks for it: eight point, which is the
 * thirteen-pixel strike at 96 dpi — the one the machine's dialogs are set in.
 * Its capitals are nine rows where Tahoma's are eight, which is the whole of
 * why a dialog does not look like the rest of the shell. */
const ween_strike *ween_dialog_font(void)
{
    static ween_strike f;
    static int ready = 0;
    if (!ready) {
        ready = ween_strike_init(&f, ween_mssans_ttf, ween_mssans_ttf_len,
                                 ween_ncm(13));
        /* It stands in for a bitmap-only font, so what it measures is what it
         * draws — unlike Tahoma, where GDI reports the outline's width. */
        if (ready)
            f.bitmap_only = 1;
    }
    return ready ? &f : NULL;
}

/* A face at the size and weight CreateFont was handed.
 *
 * There is no rasteriser here: a face carries a handful of bitmap strikes and
 * a request lands on the nearest of them. Tahoma has eight through sixteen
 * pixels, MS Sans Serif thirteen, sixteen and twenty, so a program asking for
 * a size in between gets the closest the font really has and one asking for
 * something enormous gets the largest. Height is read the way GDI reads
 * lfHeight: negative is the character height, positive the cell, and the
 * magnitude is what the strike is chosen by.
 *
 * The strikes are kept: a program that makes a font, draws with it and
 * deletes it — which is every program, every repaint — parses each size once.
 */
const ween_strike *ween_font_create(const char *face, int height, int weight)
{
    /* **Sized for the size box, which asks for more than any program used
     * to.** This was twelve, on the grounds that *"eight sizes across three
     * faces is more than any of the programs written against this ask for"* --
     * true when a face carried eight strikes and the largest was 16 ppem.
     *
     * WordPad's size box offers sixteen sizes and two faces now carry a strike
     * for each. Measured with twelve: **Arial plateaued at 26pt and Tahoma
     * drew every one of the sixteen at the same height**, because the cache
     * filled with Arial and every later Tahoma request found no entry of its
     * own face and fell back to the GUI font. A cap that was generous became
     * the thing that made a whole face draw one size.
     *
     * Sixty-four is sixteen sizes across two faces in both weights, with room
     * for the dialog face and the odd size a program picks itself. Each entry
     * is a `ween_strike` -- offsets into a font already in memory, not glyph
     * data -- so the cost is a few hundred bytes rather than a bitmap cache. */
    enum { KEPT = 64 };
    static struct {
        const unsigned char *ttf;
        int ppem, bold;
        ween_strike f;
    } kept[KEPT];
    static int count;
    const unsigned char *ttf;
    size_t len;
    int bold = weight > 500;
    int dialog = face && (!strcmp(face, "MS Shell Dlg") ||
                          !strcmp(face, "MS Sans Serif"));
    /* **Arial is a real face here now**, and it is Liberation Sans wearing
     * the name -- metric-compatible, which is what every Linux system does
     * with that name and what jd chose over aliasing it to Tahoma. The file
     * keeps Liberation's own `name` table and its OFL notice, so it says what
     * it is; this line is where it is given the name it answers to. */
    int arial = face && !strcmp(face, "Arial");
    int ppem = height < 0 ? -height : height;

    if (ppem <= 0)
        ppem = ween_ncm(11);
    if (dialog) {
        /* The face has no bold cut here, so a bold one is made the way GDI
         * makes one out of a face that has none: the glyphs are struck twice,
         * a pixel apart. */
        ttf = ween_mssans_ttf;
        len = ween_mssans_ttf_len;
    } else if (arial) {
        /* No bold cut shipped for it either, for the same reason as the
         * dialog face: a second 219K file to overstrike what one already
         * gives. Bold Arial is the regular strikes struck twice. */
        ttf = ween_arial_ttf;
        len = ween_arial_ttf_len;
    } else if (bold) {
        ttf = ween_tahomabd_ttf;
        len = ween_tahomabd_ttf_len;
    } else {
        ttf = ween_tahoma_ttf;
        len = ween_tahoma_ttf_len;
    }
    for (int i = 0; i < count; i++)
        if (kept[i].ttf == ttf && kept[i].ppem == ppem &&
            kept[i].bold == bold)
            return &kept[i].f;
    if (count == KEPT) {
        /* **The nearest already-made strike, which is what the paragraph
         * above has always claimed and what this did not do.** It returned
         * the *first* entry of the face, so once twelve strikes existed the
         * drawn size depended on the order sizes had first been asked for
         * rather than on the size asked for.
         *
         * jd: *"The size are just incorrect. Try to set a text to all the
         * value in the drop down and compare."* Sixteen sizes set in turn,
         * measured off the drawn ink rather than off `format.size`:
         *
         *     forward   8pt draws 10px ... 26pt and up draw 10px -- SMALLER
         *               than 8pt, because the first cached strike was 8pt's
         *     reverse   every one of the sixteen draws 15px, 8pt included,
         *               because the first cached strike was 72pt's
         *
         * **The same document in the same program renders differently
         * depending on what sizes it happened to be shown first**, which is
         * the half of jd's report that is not the strike ceiling.
         *
         * **The ceiling is the larger half and this does not touch it**:
         * Tahoma's largest strike is about sixteen pixels, so twelve of the
         * sixteen sizes in WordPad's box -- 12pt through 72pt -- draw
         * identically at 15px. Five distinct renderings across sixteen
         * values. Counted, because the estimate that first went on the
         * channel was six and the difference between six and twelve is the
         * difference between a rough edge and three quarters of the control
         * not working. Closing it needs strikes or a rasteriser. */
        int best = -1, bestd = 0;
        for (int i = 0; i < count; i++) {
            int d;
            if (kept[i].ttf != ttf)
                continue;
            d = kept[i].ppem > ppem ? kept[i].ppem - ppem : ppem - kept[i].ppem;
            if (best < 0 || d < bestd ||
                (d == bestd && kept[i].bold == bold)) {
                best = i;
                bestd = d;
            }
        }
        if (best >= 0)
            return &kept[best].f;
        return bold ? ween_gui_font_bold() : ween_gui_font();
    }
    if (!ween_strike_init(&kept[count].f, ttf, len, ppem))
        return bold ? ween_gui_font_bold() : ween_gui_font();
    kept[count].ttf = ttf;
    kept[count].ppem = ppem;
    kept[count].bold = bold;
    /* Wine's bold Tahoma has regular-weight stems at the larger strikes, and
     * a face with no bold cut has none at any size — both come out of the
     * same overstrike. MS Sans Serif measures from its glyphs rather than an
     * outline, which is the other thing ween_dialog_font works around. */
    if (bold && (dialog || arial || kept[count].f.ppem >= 13))
        kept[count].f.embolden = 1;
    if (dialog)
        kept[count].f.bitmap_only = 1;
    return &kept[count++].f;
}

/* The strike a face name asks for. A dialog template names one; "MS Shell
 * Dlg" is not a face at all but a stand-in the system resolves, and on this
 * Windows it resolves to MS Sans Serif. */
const ween_strike *ween_font_by_face(const char *face)
{
    if (!face || !*face)
        return ween_gui_font();
    if (!strcmp(face, "MS Shell Dlg") || !strcmp(face, "MS Sans Serif"))
        return ween_dialog_font();
    /* "MS Shell Dlg 2" is the other stand-in, and resolves to the shell's own
     * face: it is what a dialog written for this Windows asks for when it
     * wants to look like the shell rather than like an older dialog, and it
     * is what a Properties page is set in. */
    return ween_gui_font();
}

/* **Every face this library has, and the only list of them.**
 *
 * Two strikes are embedded: MS Sans Serif and Tahoma. `ween_font_create`
 * above resolves any other name to Tahoma without complaint -- which is what
 * win32's font mapper does too -- so a program could ask for a name nobody has
 * ever used and be given Tahoma with nothing to tell it so.
 *
 * **Arial used to be that example and is now a face.** jd: *"the selected
 * font by default is Arial in the dropdown, which is wrong, there is no
 * Arial"* -- wordpad asks for it at startup and the list did not contain it,
 * so the box named a face the program could not offer. It is Liberation Sans,
 * metric-compatible, shipped under the OFL with its own name table intact.
 *
 * These names must be the ones `ween_font_create` tests for. They are checked
 * against it by `tests/font_test.c`: every name here resolves to a *different*
 * strike, which is the property that makes it a list of faces rather than a
 * list of words.
 */
static const char *const g_families[] = { "Arial", "MS Sans Serif",
                                          "Tahoma" };

int ween_font_family_count(void)
{
    return (int)(sizeof g_families / sizeof *g_families);
}

const char *ween_font_family(int i)
{
    if (i < 0 || i >= ween_font_family_count())
        return NULL;
    return g_families[i];
}

const ween_strike *ween_gui_font_bold(void)
{
    static ween_strike f;
    static int ready = 0;
    if (!ready) {
        int ppem = gui_ppem();
        ready = ween_strike_init(&f, ween_tahomabd_ttf, ween_tahomabd_ttf_len,
                                 ppem);
        /* Wine's tahomabd strikes at 13/15/16px have regular-weight stems (a
         * font defect the reference also works around): synthesise bold. */
        if (ready && (ppem == 13 || ppem == 15 || ppem == 16))
            f.embolden = 1;
    }
    return ready ? &f : NULL;
}

const ween_marlett *ween_caption_font(void)
{
    static ween_marlett m;
    static int ready = 0;
    if (!ready)
        ready = ween_marlett_init(&m, ween_marlett_ttf, ween_marlett_ttf_len);
    return ready ? &m : NULL;
}
