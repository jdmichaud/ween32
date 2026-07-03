/* Dialog units — the authentic win32 positioning mechanism.
 *
 * Dialogs were laid out in template units derived from the dialog's font, so
 * the same template produced consistent dialogs at any font/DPI:
 *   horizontal base unit = the font's average character width
 *     (for a non-system font: averaged over a..z A..Z, per MS KB Q145994);
 *   vertical base unit   = the font height;
 *   pixelX = MulDiv(dluX, baseX, 4);   pixelY = MulDiv(dluY, baseY, 8).
 * Standard metrics in DLUs: push button 50x14, dialog margin 7, spacing 4. */

#include <string.h>

#include "ween_internal.h"

int MulDiv(int number, int numerator, int denominator)
{
    if (denominator == 0)
        return -1;
    long long v = (long long)number * numerator;
    /* round half away from zero, as the real MulDiv does */
    if ((v < 0) == (denominator < 0))
        v += denominator / 2;
    else
        v -= denominator / 2;
    return (int)(v / denominator);
}

/* Base units of a strike font: KB Q145994's average over the 52 letters
 * (rounded to nearest), and the font's cell height. */
static void base_units(const ween_strike *f, int *bx, int *by)
{
    static const char letters[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int sum = 0;
    for (int i = 0; i < 52; i++)
        sum += ween_strike_char_advance(f, (unsigned char)letters[i]);
    *bx = (sum + 26) / 52;
    *by = f->ascent - f->descent;
}

LONG GetDialogBaseUnits(void)
{
    const ween_strike *f = ween_gui_font();
    int bx = 6, by = 13; /* the classic 96-dpi GUI-font values */
    if (f)
        base_units(f, &bx, &by);
    return (LONG)MAKELPARAM((WORD)bx, (WORD)by);
}

BOOL MapDialogRect(HWND dlg, LPRECT rect)
{
    if (!rect)
        return FALSE;
    const ween_strike *f = (dlg && dlg->font) ? dlg->font : ween_gui_font();
    if (!f)
        return FALSE;
    int bx, by;
    base_units(f, &bx, &by);
    rect->left = MulDiv(rect->left, bx, 4);
    rect->right = MulDiv(rect->right, bx, 4);
    rect->top = MulDiv(rect->top, by, 8);
    rect->bottom = MulDiv(rect->bottom, by, 8);
    return TRUE;
}
