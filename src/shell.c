/* SHELL32's small corner: files dropped on a window, and the About box.
 *
 * A program that takes dropped files calls DragAcceptFiles and then reads the
 * names out of the handle WM_DROPFILES brings. What does the dropping is the
 * desktop's drag protocol -- XDND on X11 -- and ween32 does not speak it yet,
 * so a window may say it accepts files and will simply never hear about any.
 * That is a gap in the backend rather than in the program: the same source
 * dropped on by Windows works there, and the day XDND is here it works here
 * with nothing in the program changed.
 */

#include <stdio.h>
#include <string.h>

#include "ween_internal.h"

void DragAcceptFiles(HWND wnd, BOOL accept)
{
    /* Remembered on the window, so that the day something can drop on it the
     * answer to "does this window want them" is already here. */
    if (wnd)
        wnd->accepts_files = accept ? 1 : 0;
}

/* Nothing here ever hands out a HDROP, so anything asked of one is asked of a
 * handle that did not come from us. Answering nought -- no files, no name --
 * is what a program checks for and stops at. */
UINT DragQueryFileA(HDROP drop, UINT index, LPSTR name, UINT max)
{
    (void)drop;
    (void)index;
    if (name && max)
        name[0] = 0;
    return 0;
}

void DragFinish(HDROP drop)
{
    (void)drop;
}

BOOL DragQueryPoint(HDROP drop, POINT *pt)
{
    (void)drop;
    if (pt)
        pt->x = pt->y = 0;
    return FALSE;
}

/* The shell's About box is a message box with the program's name on the first
 * line and whatever else it passed under that, which is what this puts up --
 * the same words in the same order, without the shell's own version block
 * behind them. */
void ShellAboutA(HWND owner, LPCSTR app, LPCSTR other, HICON icon)
{
    char text[1024];
    const char *hash;
    char title[256];
    (void)icon;
    /* The app string is "Title#Caption": what is before the hash titles the
     * box, and what is after it is the first line of the message. */
    hash = app ? strchr(app, '#') : NULL;
    if (hash) {
        size_t n = (size_t)(hash - app);
        if (n >= sizeof title)
            n = sizeof title - 1;
        memcpy(title, app, n);
        title[n] = 0;
        snprintf(text, sizeof text, "%s\n\n%s", hash + 1, other ? other : "");
    } else {
        snprintf(title, sizeof title, "About %s", app ? app : "");
        snprintf(text, sizeof text, "%s\n\n%s", app ? app : "",
                 other ? other : "");
    }
    MessageBoxA(owner, text, title, MB_OK | MB_ICONINFORMATION);
}
