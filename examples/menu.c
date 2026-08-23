/* The menu and dialog sampler: a menu bar, its drop-downs, and the two modal
 * paths (MessageBoxA and DialogBox).
 *
 * Like examples/controls.c this file has two jobs and compiles unchanged for
 * both: against real <windows.h> it is the reference Wine renders, and against
 * ween32 it is the example. tools/refcapture/capture.sh drives the first, and
 * opens the File menu before it captures — a drop-down only exists while it is
 * being tracked, so the reference has to be taken with one up.
 *
 * WEEN32_MENU_OPEN, when set, opens that menu from a timer instead of waiting
 * for a click. That is how the headless screenshot gets a popup into it.
 */

#include <ween32.h>

#include "win32_dlg.h" /* the DLGTEMPLATE builder, shared with the other example */

#ifdef _WIN32
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define HAVE(feature) 1
#else
#define HAVE(feature) WEEN32_HAS_##feature
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#endif

enum {
    ID_NEW = 200,
    ID_OPEN,
    ID_RECENT_1,
    ID_RECENT_2,
    ID_EXIT,
    ID_CUT,
    ID_COPY,
    ID_PASTE,
    ID_WRAP,
    ID_ABOUT,
    ID_MSGBOX,
    ID_DIALOG,
    ID_STATUS
};

static HFONT g_font;
static HWND g_status;
static int g_wrap = 1;

static HWND mk(const char *cls, const char *text, DWORD style, DWORD ex, int x,
               int y, int w, int h, HWND parent, int id)
{
    HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y,
                             w, h, parent, (HMENU)(UINT_PTR)id, NULL, NULL);
    if (c && g_font) /* without this a control gets the bold system font */
        SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void say(const char *what)
{
    if (g_status)
        SetWindowTextA(g_status, what);
}

#if HAVE(MENU)
static void build_menu(HWND w)
{
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU recent = CreatePopupMenu();
    HMENU edit = CreatePopupMenu();
    HMENU help = CreatePopupMenu();

    AppendMenuA(recent, MF_STRING, ID_RECENT_1, "1 report.txt");
    AppendMenuA(recent, MF_STRING, ID_RECENT_2, "2 notes.txt");

    AppendMenuA(file, MF_STRING, ID_NEW, "&New\tCtrl+N");
    AppendMenuA(file, MF_STRING, ID_OPEN, "&Open...\tCtrl+O");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_POPUP, (UINT_PTR)recent, "&Recent files");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_EXIT, "E&xit");

    AppendMenuA(edit, MF_STRING, ID_CUT, "Cu&t\tCtrl+X");
    AppendMenuA(edit, MF_STRING, ID_COPY, "&Copy\tCtrl+C");
    AppendMenuA(edit, MF_STRING | MF_GRAYED, ID_PASTE, "&Paste\tCtrl+V");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING | MF_CHECKED, ID_WRAP, "&Word wrap");

    AppendMenuA(help, MF_STRING, ID_ABOUT, "&About...");

    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)edit, "&Edit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)help, "&Help");
    SetMenu(w, bar);
}
#endif

#if HAVE(DIALOGBOX)
/* Any button ends the dialog, and its id is the answer. */
static INT_PTR CALLBACK dialog_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_COMMAND) {
        EndDialog(dlg, (INT_PTR)LOWORD(wp));
        return TRUE;
    }
    return FALSE;
}
#endif

static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
#if HAVE(MENU)
        build_menu(w);
#endif
        mk("BUTTON", "&Message box", WS_TABSTOP, 0, 20, 30, 110, 23, w,
           ID_MSGBOX);
        mk("BUTTON", "Modal &dialog", WS_TABSTOP, 0, 140, 30, 110, 23, w,
           ID_DIALOG);
        g_status = mk("STATIC", "Ready", 0, WS_EX_CLIENTEDGE, 20, 70, 230, 18,
                      w, ID_STATUS);
        /* The reference capture needs a drop-down showing, and a drop-down
         * exists only while it is tracked. SC_KEYMENU is how a window is asked
         * to open one by mnemonic — the same thing Alt+F does. */
#if HAVE(MENU)
        if (getenv("WEEN32_MENU_OPEN"))
            SetTimer(w, 1, 400, NULL);
#endif
        return 0;

#if HAVE(MENU)
    case WM_TIMER: {
        /* Show the File drop-down where the menu bar would put it. The app
         * asks for this itself rather than going through the menu bar, so the
         * capture does not depend on the window having the input focus. */
        RECT wr;
        KillTimer(w, 1);
        GetWindowRect(w, &wr);
        TrackPopupMenu(GetSubMenu(GetMenu(w), 0), TPM_LEFTALIGN, wr.left + 3,
                       wr.top + 3 + GetSystemMetrics(SM_CYCAPTION) +
                           GetSystemMetrics(SM_CYMENU),
                       0, w, NULL);
        return 0;
    }
#endif

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_NEW:
            say("File / New");
            return 0;
        case ID_OPEN:
            say("File / Open");
            return 0;
        case ID_RECENT_1:
            say("Recent / report.txt");
            return 0;
        case ID_RECENT_2:
            say("Recent / notes.txt");
            return 0;
        case ID_CUT:
            say("Edit / Cut");
            return 0;
        case ID_COPY:
            say("Edit / Copy");
            return 0;
        case ID_WRAP: {
            g_wrap = !g_wrap;
#if HAVE(MENU)
            HMENU bar = GetMenu(w);
            if (bar)
                CheckMenuItem(GetSubMenu(bar, 1), ID_WRAP,
                              g_wrap ? MF_CHECKED : MF_UNCHECKED);
#endif
            say(g_wrap ? "Word wrap on" : "Word wrap off");
            return 0;
        }
        case ID_ABOUT:
        case ID_MSGBOX:
#if HAVE(MESSAGEBOX)
            MessageBoxA(w, "A window with nothing in it but a message,\n"
                           "an icon and one button.",
                        "About the sampler", MB_OK);
            say("The message box was closed");
#else
            say("Message box: not built yet");
#endif
            return 0;
        case ID_DIALOG: {
#if HAVE(DIALOGBOX)
            static const dlg_item items[] = {
                { WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 10, 130, 8, 0,
                  ATOM_STATIC, "Modal: the sampler is disabled.", NULL, 0 },
                { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 30, 32,
                  50, 14, IDOK, ATOM_BUTTON, "OK", NULL, 0 },
                { WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 90, 32, 50,
                  14, IDCANCEL, ATOM_BUTTON, "Cancel", NULL, 0 },
            };
            static unsigned char tmpl[1024];
            build_dialog_template(tmpl, sizeof(tmpl),
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU |
                                      WS_VISIBLE,
                                  150, 54, "A modal dialog", items, 3);
            INT_PTR r = DialogBoxIndirectParamA(NULL, (LPCDLGTEMPLATEA)tmpl, w,
                                                dialog_proc, 0);
            say(r == IDOK ? "Dialog: OK" : "Dialog: cancelled");
#else
            say("Modal dialog: not built yet");
#endif
            return 0;
        }
        case ID_EXIT:
            DestroyWindow(w);
            return 0;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

int main(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.lpszClassName = "ween32menu";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassA(&wc);
    g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    HWND w = CreateWindowExA(WS_EX_DLGMODALFRAME, "ween32menu",
                             "win32 menu sampler",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                 WS_VISIBLE,
                             40, 40, 280, 140, NULL, NULL, NULL, NULL);
    ShowWindow(w, SW_SHOWNORMAL);
    UpdateWindow(w);

    /* The accelerators the File menu advertises beside its items. An app
     * offers every message here first, exactly as it does to
     * IsDialogMessageA. */
#if HAVE(ACCELERATORS)
    static ACCEL accels[] = {
        { FVIRTKEY | FCONTROL, 'N', ID_NEW },
        { FVIRTKEY | FCONTROL, 'O', ID_OPEN },
    };
    HACCEL table = CreateAcceleratorTableA(accels, 2);
#endif

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
#if HAVE(ACCELERATORS)
        if (TranslateAcceleratorA(w, table, &msg))
            continue;
#endif
        if (IsDialogMessageA(w, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
