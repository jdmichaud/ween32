/* Classic Windows chrome on the software surface (port of classic.zig).
 * The 2px 3D edge and the caption gradient reproduce the authentic Win2000
 * look; the bevel algorithm and palette follow Wine's DrawEdge/GetSysColor. */

#include "ween_internal.h"

/* A 2-pixel Windows 3D edge. sunken == 0 is raised. */
void ween_classic_bevel(ween_surface *s, int x, int y, int w, int h, int sunken)
{
    ween_color tlo = sunken ? WEEN_DKSHADOW : WEEN_WHITE; /* top-left outer */
    ween_color tli = sunken ? WEEN_SHADOW : WEEN_3DLIGHT; /* top-left inner */
    ween_color bri = sunken ? WEEN_3DLIGHT : WEEN_SHADOW; /* bottom-right inner */
    ween_color bro = sunken ? WEEN_WHITE : WEEN_DKSHADOW; /* bottom-right outer */

    ween_surface_hline(s, x, y, w, tlo);
    ween_surface_vline(s, x, y, h, tlo);
    ween_surface_hline(s, x, y + h - 1, w, bro);
    ween_surface_vline(s, x + w - 1, y, h, bro);

    ween_surface_hline(s, x + 1, y + 1, w - 2, tli);
    ween_surface_vline(s, x + 1, y + 1, h - 2, tli);
    ween_surface_hline(s, x + 1, y + h - 2, w - 2, bri);
    ween_surface_vline(s, x + w - 2, y + 1, h - 2, bri);
}

static ween_color lerp(ween_color a, ween_color b, int t)
{
    int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
    int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
    unsigned r = (unsigned)(ar + (br - ar) * t / 255);
    unsigned g = (unsigned)(ag + (bg - ag) * t / 255);
    unsigned bl = (unsigned)(ab + (bb - ab) * t / 255);
    return (r << 16) | (g << 8) | bl;
}

/* The classic navy -> light-blue horizontal caption gradient. */
void ween_classic_caption(ween_surface *s, int x, int y, int w, int h)
{
    if (w <= 0)
        return;
    for (int i = 0; i < w; i++) {
        int t = w == 1 ? 0 : i * 255 / (w - 1);
        ween_surface_vline(s, x + i, y, h, lerp(WEEN_CAP_LEFT, WEEN_CAP_RIGHT, t));
    }
}
