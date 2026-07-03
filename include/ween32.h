/* ween32 — the classic win32 API, reimplemented small and portable.
 *
 * This header exposes a subset of the win32 USER32/GDI32 API with the same
 * names, signatures, constants and semantics as <windows.h>, backed by a
 * software renderer that reproduces the classic Windows 2000 look pixel for
 * pixel. Programs written against this subset compile unchanged on real
 * Windows (where this header simply defers to <windows.h>) and everywhere
 * else through ween32.
 *
 * Constant values are taken verbatim from the Windows SDK (verified against
 * mingw-w64's winuser.h/wingdi.h) so message numbers, styles and color
 * indices match real Windows bit for bit.
 */

#ifndef WEEN32_H
#define WEEN32_H

#ifdef _WIN32
/* On Windows, ween32 *is* win32: use the real thing. */
#include <windows.h>
#else

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- fundamental types (LLP64-faithful) ------------------------------- */

typedef int BOOL;
typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t UINT;
typedef intptr_t LONG_PTR;
typedef uintptr_t UINT_PTR;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef char CHAR;
typedef const CHAR *LPCSTR;
typedef CHAR *LPSTR;
typedef void *LPVOID;
typedef WORD ATOM;
typedef DWORD COLORREF; /* 0x00BBGGRR, as on Windows */

#define TRUE 1
#define FALSE 0
#define CALLBACK
#define WINAPI

/* Handles are opaque pointers, as on Windows. */
typedef struct ween_wnd *HWND;
typedef struct ween_dc *HDC;
typedef struct ween_gdiobj *HGDIOBJ;
typedef struct ween_gdiobj *HBRUSH;
typedef struct ween_gdiobj *HFONT;
typedef void *HINSTANCE;
typedef void *HICON;
typedef void *HCURSOR;
typedef void *HMENU;

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;
typedef RECT *LPRECT;

typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE;

typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG;
typedef MSG *LPMSG;

typedef LRESULT(CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagWNDCLASSA {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
} WNDCLASSA;

typedef struct tagPAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT;

typedef struct tagDRAWITEMSTRUCT {
    UINT CtlType;
    UINT CtlID;
    UINT itemID;
    UINT itemAction;
    UINT itemState;
    HWND hwndItem;
    HDC hDC;
    RECT rcItem;
    UINT_PTR itemData;
} DRAWITEMSTRUCT, *LPDRAWITEMSTRUCT;

typedef struct tagCREATESTRUCTA {
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HMENU hMenu;
    HWND hwndParent;
    int cy;
    int cx;
    int y;
    int x;
    LONG style;
    LPCSTR lpszName;
    LPCSTR lpszClass;
    DWORD dwExStyle;
} CREATESTRUCTA;

/* ---- macros ------------------------------------------------------------ */

#define LOWORD(l) ((WORD)(((UINT_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((UINT_PTR)(l)) >> 16) & 0xffff))
#define MAKELPARAM(a, b) ((LPARAM)(DWORD)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define MAKEWPARAM(a, b) ((WPARAM)(DWORD)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)((c) >> 8))
#define GetBValue(c) ((BYTE)((c) >> 16))
#define MAKEINTRESOURCEA(i) ((LPCSTR)(UINT_PTR)((WORD)(i)))
#define CW_USEDEFAULT ((int)0x80000000)

/* ---- window messages (Windows SDK values) ------------------------------ */

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_SETTEXT 0x000C
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_DRAWITEM 0x002B
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_COMMAND 0x0111
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202

/* ---- window styles ------------------------------------------------------ */

#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_SYSMENU 0x00080000L

/* button / static control styles */
#define BS_PUSHBUTTON 0x00000000L
#define BS_DEFPUSHBUTTON 0x00000001L
#define BS_OWNERDRAW 0x0000000BL
#define SS_LEFT 0x00000000L
#define SS_CENTER 0x00000001L
#define SS_RIGHT 0x00000002L

/* button notifications */
#define BN_CLICKED 0

/* owner draw */
#define ODT_BUTTON 4
#define ODA_DRAWENTIRE 0x0001
#define ODA_SELECT 0x0002
#define ODS_SELECTED 0x0001

/* ---- hit-test results --------------------------------------------------- */

#define HTNOWHERE 0
#define HTCLIENT 1
#define HTCAPTION 2
#define HTCLOSE 20

/* ---- ShowWindow --------------------------------------------------------- */

#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5

/* ---- virtual keys ------------------------------------------------------- */

#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B

/* ---- system colors (indices as on Windows) ------------------------------ */

#define COLOR_ACTIVECAPTION 2
#define COLOR_WINDOW 5
#define COLOR_WINDOWTEXT 8
#define COLOR_CAPTIONTEXT 9
#define COLOR_BTNFACE 15
#define COLOR_BTNSHADOW 16
#define COLOR_BTNTEXT 18
#define COLOR_BTNHIGHLIGHT 20
#define COLOR_3DDKSHADOW 21
#define COLOR_3DLIGHT 22
#define COLOR_GRADIENTACTIVECAPTION 27

/* ---- DrawEdge / DrawFrameControl ---------------------------------------- */

#define BDR_RAISEDOUTER 0x0001
#define BDR_SUNKENOUTER 0x0002
#define BDR_RAISEDINNER 0x0004
#define BDR_SUNKENINNER 0x0008
#define EDGE_RAISED (BDR_RAISEDOUTER | BDR_RAISEDINNER)
#define EDGE_SUNKEN (BDR_SUNKENOUTER | BDR_SUNKENINNER)
#define BF_LEFT 0x0001
#define BF_TOP 0x0002
#define BF_RIGHT 0x0004
#define BF_BOTTOM 0x0008
#define BF_RECT (BF_LEFT | BF_TOP | BF_RIGHT | BF_BOTTOM)

#define DFC_CAPTION 1
#define DFCS_CAPTIONCLOSE 0x0000
#define DFCS_CAPTIONMIN 0x0001
#define DFCS_CAPTIONMAX 0x0002
#define DFCS_CAPTIONRESTORE 0x0003
#define DFCS_PUSHED 0x0200

/* ---- DrawText ------------------------------------------------------------ */

#define DT_LEFT 0x00000000
#define DT_CENTER 0x00000001
#define DT_RIGHT 0x00000002
#define DT_VCENTER 0x00000004
#define DT_SINGLELINE 0x00000020

/* ---- GDI misc ------------------------------------------------------------ */

#define TRANSPARENT 1
#define OPAQUE 2
#define SYSTEM_FONT 13
#define DEFAULT_GUI_FONT 17

/* ---- USER32 -------------------------------------------------------------- */

ATOM RegisterClassA(const WNDCLASSA *wc);
HWND CreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR window_name,
                     DWORD style, int x, int y, int w, int h,
                     HWND parent, HMENU menu, HINSTANCE inst, LPVOID param);
#define CreateWindowA(cls, name, style, x, y, w, h, parent, menu, inst, param) \
    CreateWindowExA(0L, cls, name, style, x, y, w, h, parent, menu, inst, param)
BOOL DestroyWindow(HWND wnd);
BOOL ShowWindow(HWND wnd, int cmd);
BOOL SetWindowTextA(HWND wnd, LPCSTR text);
int GetWindowTextA(HWND wnd, LPSTR out, int max);
BOOL GetClientRect(HWND wnd, LPRECT rect);
BOOL MoveWindow(HWND wnd, int x, int y, int w, int h, BOOL repaint);
BOOL InvalidateRect(HWND wnd, const RECT *rect, BOOL erase);
BOOL UpdateWindow(HWND wnd);
HWND GetDlgItem(HWND dlg, int id);
HWND SetFocus(HWND wnd);

BOOL GetMessageA(LPMSG msg, HWND wnd, UINT min, UINT max);
BOOL TranslateMessage(const MSG *msg);
LRESULT DispatchMessageA(const MSG *msg);
void PostQuitMessage(int code);
LRESULT SendMessageA(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT DefWindowProcA(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

/* Dialog layout: base units derived from the dialog font, and the DLU->pixel
 * mapping (px = MulDiv(dlu, base, 4 or 8)) — exactly MapDialogRect. */
LONG GetDialogBaseUnits(void);
BOOL MapDialogRect(HWND dlg, LPRECT rect);
int MulDiv(int number, int numerator, int denominator);

/* ---- GDI ------------------------------------------------------------------ */

HDC BeginPaint(HWND wnd, PAINTSTRUCT *ps);
BOOL EndPaint(HWND wnd, const PAINTSTRUCT *ps);
BOOL FillRect(HDC dc, const RECT *rect, HBRUSH brush);
BOOL DrawEdge(HDC dc, LPRECT rect, UINT edge, UINT flags);
BOOL DrawFrameControl(HDC dc, LPRECT rect, UINT type, UINT state);
BOOL TextOutA(HDC dc, int x, int y, LPCSTR text, int len);
int DrawTextA(HDC dc, LPCSTR text, int len, LPRECT rect, UINT format);
BOOL GetTextExtentPoint32A(HDC dc, LPCSTR text, int len, SIZE *size);
COLORREF SetTextColor(HDC dc, COLORREF color);
int SetBkMode(HDC dc, int mode);
DWORD GetSysColor(int index);
HBRUSH GetSysColorBrush(int index);
HBRUSH CreateSolidBrush(COLORREF color);
BOOL DeleteObject(HGDIOBJ obj);
HGDIOBJ GetStockObject(int what);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj);

#ifdef __cplusplus
}
#endif

#endif /* !_WIN32 */
#endif /* WEEN32_H */
