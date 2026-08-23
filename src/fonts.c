/* The built-in stock fonts: Wine's redistributable Tahoma (the classic UI
 * face, 11 ppem bitmap strikes = DEFAULT_GUI_FONT), its MS Sans Serif (which
 * is what "MS Shell Dlg" means on this Windows, and so what every dialog the
 * shell puts up is lettered in), and Marlett (the caption glyphs). Embedded
 * so the library needs no files at run time; all are lazily parsed on first
 * use. */

#include <string.h>

#include "ween_internal.h"

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

/* The strike a face name asks for. A dialog template names one; "MS Shell
 * Dlg" is not a face at all but a stand-in the system resolves, and on this
 * Windows it resolves to MS Sans Serif. */
const ween_strike *ween_font_by_face(const char *face)
{
    if (!face || !*face)
        return ween_gui_font();
    if (!strcmp(face, "MS Shell Dlg") || !strcmp(face, "MS Sans Serif"))
        return ween_dialog_font();
    return ween_gui_font();
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
