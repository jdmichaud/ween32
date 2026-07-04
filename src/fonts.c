/* The built-in stock fonts: Wine's redistributable Tahoma (the classic UI
 * face, 11 ppem bitmap strikes = DEFAULT_GUI_FONT) and Marlett (the caption
 * glyphs). Embedded so the library needs no files at run time; both are
 * lazily parsed on first use. */

#include "ween_internal.h"

#include "../fonts/marlett_ttf.h"
#include "../fonts/tahoma_ttf.h"
#include "../fonts/tahomabd_ttf.h"

const ween_strike *ween_gui_font(void)
{
    static ween_strike f;
    static int ready = 0;
    if (!ready)
        ready = ween_strike_init(&f, ween_tahoma_ttf, ween_tahoma_ttf_len, 11);
    return ready ? &f : NULL;
}

const ween_strike *ween_gui_font_bold(void)
{
    static ween_strike f;
    static int ready = 0;
    if (!ready)
        ready = ween_strike_init(&f, ween_tahomabd_ttf, ween_tahomabd_ttf_len, 11);
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
