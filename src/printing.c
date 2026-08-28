/* Printing, which this cannot do.
 *
 * A win32 program prints by asking PrintDlg for a device context and then
 * drawing on it: StartDoc, StartPage, the same GDI calls it uses on the
 * screen, EndPage, EndDoc. That is a good design -- the program does not know
 * what it is drawing on -- and it means the missing half here is a device
 * context whose pixels go somewhere other than a window, plus something to
 * send the result to. A PostScript or PDF device context is the honest shape
 * of that work, and it is a piece of work rather than an oversight.
 *
 * These exist because a program that has a Print item on its File menu links
 * against them whether or not anybody prints. They answer as a cancelled
 * dialog and a failed document do: the program checks, gives up, and prints
 * nothing -- which is what is actually happening.
 */

#include "ween_internal.h"

BOOL PrintDlgA(PRINTDLGA *pd)
{
    /* PD_RETURNDC asks for a device context back, and the caller reads
     * pd->hDC; there is none, so it is cleared rather than left as it was. */
    if (pd)
        pd->hDC = NULL;
    return FALSE;
}

BOOL PageSetupDlgA(PAGESETUPDLGA *psd)
{
    (void)psd;
    return FALSE;
}

/* Windows answers a positive number for a document begun and something at or
 * below zero for one that was not, which is what a program checks. */
int StartDocA(HDC dc, const DOCINFOA *di)
{
    (void)dc;
    (void)di;
    return 0;
}

int StartPage(HDC dc)
{
    (void)dc;
    return 0;
}

int EndPage(HDC dc)
{
    (void)dc;
    return 0;
}

int EndDoc(HDC dc)
{
    (void)dc;
    return 0;
}

int AbortDoc(HDC dc)
{
    (void)dc;
    return 0;
}
