/* Why a bare control's horizontal page is not WordPad's.
 *
 * Both rules are clean and they disagree by four pixels:
 *
 *     bare control, five widths   page = client - 10   (hscroll.txt)
 *     the machine's WordPad, two  page = client - 14   (wpscroll.txt)
 *         client 756 -> page 742,  client 1020 -> page 1006
 *
 * A rule fitted at one width could have been either; each of these was
 * predicted before it was read and neither left a residue, so the difference
 * is between the two *controls* rather than between two readings. This is the
 * bisection: one variable per row, everything else held, and the row that
 * lands on `client - 14` names it.
 *
 * The candidates, in the order they are worth suspecting:
 *
 *   class      WordPad's is **RichEdit20W** (window.txt); the probe's was A
 *   font       WordPad's 'w' is 9px (nMax 901 over 100 chars, 2197 over 244,
 *              both exact); the probe's "Arial 10" gives 11px. **That was
 *              this probe's own fault and the last row of the file proves
 *              it**: naming CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE and zeroing
 *              them takes 11 to 9. The face took all along -- riched20's
 *              default face is System, System is bold, and a mask of
 *              CFM_FACE|CFM_SIZE leaves the effects at that default, so
 *              "Arial 10" laid out as *bold* Arial. seq/machine/README
 *              records this trap costing seven findings; this walked into
 *              it one field over. **The page is 746 at 9, 10 and 11 pixels
 *              per character, so the font is not the four either.**
 *   selbar     ES_SELECTIONBAR puts character 0 at x=9 rather than x=1
 *   size       the probe's control fills its host unless resized, because
 *              0x01000000 is WS_MAXIMIZE to the window manager (maxq.c)
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o hpage.obj hpage.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o hpage.exe hpage.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py hpage.exe
 *   Z:\hpage.exe        -> Z:\hpage.txt
 */
#include <windows.h>
#include <richedit.h>
#include "guestcrt.h"

static void pump(int ms)
{
    MSG m;
    DWORD e = GetTickCount() + ms;
    while (GetTickCount() < e) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(2);
    }
}

static HWND host;
static char big[600];
static WCHAR bigw[600];

#define WP_STYLE ((DWORD)(0x550081C4u | (unsigned)WS_VSCROLL | (unsigned)WS_HSCROLL))

static int g_effects = 0;   /* 1: name bold/italic/underline and zero them */

static void set_font(HWND re, const char *face, int pts)
{
    CHARFORMATA cf;
    int i;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_FACE | CFM_SIZE;
    /* **riched20's default face is System, and System is bold.** Naming the
     * face and the size leaves the effects at that default, so "Arial 10"
     * lays out as *bold* Arial -- 11px per w against 9. The machine README
     * records this exact trap costing seven findings, and this probe walked
     * into it one field over. */
    if (g_effects)
        cf.dwMask |= CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    cf.yHeight = pts * 20;
    for (i = 0; face[i] && i < LF_FACESIZE - 1; i++)
        cf.szFaceName[i] = face[i];
    SendMessageA(re, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);
    pump(150);
}

static int xof(HWND re, int i)
{
    POINTL p;
    p.x = p.y = 0;
    SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&p, (LPARAM)(UINT_PTR)i);
    return (int)p.x;
}

/* One row. `wide` picks the W class, `face` NULL leaves the control's own
 * font alone, `sel` clears ES_SELECTIONBAR. */
static long g_target = 1440;
static int g_inset = -1;   /* >=0: EM_SETRECT, the client inset by this much */

static void row(const char *label, int wide, const char *face, int sel)
{
    DWORD st = WP_STYLE;
    HWND re;
    RECT cr;
    SCROLLINFO si;
    int page, before, adv;

    if (!sel)
        st &= ~0x01000000u;
    if (wide)
        re = CreateWindowExW(0x210, L"RichEdit20W", L"", st, 0, 0, 760, 386,
                             host, NULL, NULL, NULL);
    else
        re = CreateWindowExA(0x210, RICHEDIT_CLASSA, "", st, 0, 0, 760, 386,
                             host, NULL, NULL, NULL);
    if (!re) {
        fprintf(GUEST_STREAM, "  %s  CreateWindow failed\n", label);
        return;
    }
    SetFocus(re);
    pump(150);
    if (face)
        set_font(re, face, 10);
    /* The style word contains WS_MAXIMIZE's bit, so the control filled the
     * host whatever was passed; put it at WordPad's own 760x386. */
    SetWindowPos(re, NULL, 0, 0, 760, 386, SWP_NOZORDER | SWP_NOACTIVATE);
    pump(250);
    SendMessageA(re, EM_SETTARGETDEVICE, 0, (LPARAM)g_target);
    pump(200);
    if (g_inset >= 0) {
        RECT fr;
        GetClientRect(re, &fr);
        fr.left += g_inset;
        fr.right -= g_inset;
        SendMessageA(re, EM_SETRECT, 0, (LPARAM)&fr);
        pump(250);
    }
    if (wide)
        SendMessageW(re, WM_SETTEXT, 0, (LPARAM)bigw);
    else
        SendMessageA(re, WM_SETTEXT, 0, (LPARAM)big);
    pump(500);

    GetClientRect(re, &cr);
    SendMessageA(re, WM_HSCROLL, MAKEWPARAM(SB_LEFT, 0), 0);
    pump(150);
    before = xof(re, 0);
    SendMessageA(re, WM_HSCROLL, MAKEWPARAM(SB_PAGERIGHT, 0), 0);
    pump(150);
    page = before - xof(re, 0);

    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_ALL;
    GetScrollInfo(re, SB_HORZ, &si);
    adv = (int)((si.nMax - 1) / 500);
    fprintf(GUEST_STREAM,
            "  %s  client %dx%d  page %d  client-page %d  nMax %d  adv %d  x0 %d\n",
            label, (int)cr.right, (int)cr.bottom, page,
            (int)cr.right - page, (int)si.nMax, adv, before);
    DestroyWindow(re);
}

void WinMainCRTStartup(void)
{
    WNDCLASSA wc;
    int i;

    LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "hp";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "hp", "hp", WS_OVERLAPPEDWINDOW, 8, 8, 900, 600,
                           NULL, NULL, wc.hInstance, NULL);
    g_out = CreateFileA("Z:\\hpage.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        0, NULL);
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    pump(400);

    for (i = 0; i < 500; i++) {
        big[i] = 'w';
        bigw[i] = L'w';
    }
    big[500] = 0;
    bigw[500] = 0;

    fprintf(GUEST_STREAM, "WordPad's own, for comparison: client 756  page 742"
                          "  client-page 14  adv 9\n\n");
    row("A  Arial 10   selbar ", 0, "Arial", 1);
    row("W  Arial 10   selbar ", 1, "Arial", 1);
    row("A  own font   selbar ", 0, NULL, 1);
    row("W  own font   selbar ", 1, NULL, 1);
    row("A  Arial 10   no sel ", 0, "Arial", 0);
    row("W  own font   no sel ", 1, NULL, 0);

    /* MFC's CRichEditView::WrapChanged does not pass 1440 for No wrap -- it
     * passes a width of its own -- and wordpad's src/main.zig:645 sends 1440
     * because that is what turned wrapping off, not because anything measured
     * the value. A width that only has to be non-zero is exactly the kind of
     * constant that can be wrong without ever looking wrong. */
    fprintf(GUEST_STREAM, "\n== the same control, against the target width ==\n");
    g_target = 1;
    row("W  own font   target 1    ", 1, NULL, 1);
    g_target = 1440;
    row("W  own font   target 1440 ", 1, NULL, 1);
    g_target = 9360;
    row("W  own font   target 9360 ", 1, NULL, 1);
    g_target = 15840;
    row("W  own font   target 15840", 1, NULL, 1);

    /* MFC's CRichEditView sets a formatting rectangle, and `seqprobe.c`'s
     * header already records that a control with one and a control without
     * wrap differently. A rectangle a few pixels inside the client is exactly
     * the shape of a four-pixel page difference. */
    fprintf(GUEST_STREAM, "\n== the same control, against EM_SETRECT ==\n");
    g_target = 1440;
    g_inset = 0;
    row("W  own font   inset 0     ", 1, NULL, 1);
    g_inset = 1;
    row("W  own font   inset 1     ", 1, NULL, 1);
    g_inset = 2;
    row("W  own font   inset 2     ", 1, NULL, 1);
    g_inset = 4;
    row("W  own font   inset 4     ", 1, NULL, 1);

    fprintf(GUEST_STREAM, "\n== the same control, naming the effects ==\n");
    g_inset = -1;
    g_target = 1440;
    g_effects = 0;
    row("A  Arial 10   effects unnamed", 0, "Arial", 1);
    g_effects = 1;
    row("A  Arial 10   effects zeroed ", 0, "Arial", 1);

    CloseHandle(g_out);
    ExitProcess(0);
}
