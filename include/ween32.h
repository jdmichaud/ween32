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
typedef intptr_t INT_PTR;
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
typedef INT_PTR(CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);

/* Dialog templates share the real win32 binary layout (2-byte packed): an app
 * can build one in memory and CreateDialogIndirect it, and the same bytes feed
 * the real dialog manager on Windows. Each is followed in the stream by its
 * variable-length menu/class/title (and per-item creation data), with items
 * aligned to DWORD boundaries. */
#pragma pack(push, 2)
typedef struct {
    DWORD style;
    DWORD dwExtendedStyle;
    WORD cdit;
    short x, y, cx, cy;
} DLGTEMPLATE, *LPDLGTEMPLATE;
typedef const DLGTEMPLATE *LPCDLGTEMPLATEA;

typedef struct {
    DWORD style;
    DWORD dwExtendedStyle;
    short x, y, cx, cy;
    WORD id;
} DLGITEMTEMPLATE, *LPDLGITEMTEMPLATE;
#pragma pack(pop)

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
#define WM_ENABLE 0x000A
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_DRAWITEM 0x002B
#define WM_INITDIALOG 0x0110
#define WM_USER 0x0400
#define DM_GETDEFID (WM_USER + 0)
#define DM_SETDEFID (WM_USER + 1)
#define DC_HASDEFID 0x534B
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
#define WS_DISABLED 0x08000000L
#define WS_GROUP 0x00020000L
#define WS_TABSTOP 0x00010000L

/* dialog styles */
#define DS_3DLOOK 0x0004L
#define DS_SETFONT 0x40L
#define DS_MODALFRAME 0x80L
#define DS_CENTER 0x0800L

/* standard command ids */
#define IDOK 1
#define IDCANCEL 2

/* button / static control styles */
#define BS_PUSHBUTTON 0x00000000L
#define BS_DEFPUSHBUTTON 0x00000001L
#define BS_CHECKBOX 0x00000002L
#define BS_AUTOCHECKBOX 0x00000003L
#define BS_RADIOBUTTON 0x00000004L
#define BS_3STATE 0x00000005L
#define BS_AUTO3STATE 0x00000006L
#define BS_GROUPBOX 0x00000007L
#define BS_AUTORADIOBUTTON 0x00000009L
#define BS_OWNERDRAW 0x0000000BL
#define BS_TYPEMASK 0x0000000FL
#define BS_LEFTTEXT 0x00000020L
#define SS_LEFT 0x00000000L
#define SS_CENTER 0x00000001L
#define SS_RIGHT 0x00000002L

/* button notifications */
#define BN_CLICKED 0

/* button messages and check states */
#define BM_GETCHECK 0x00F0
#define BM_SETCHECK 0x00F1
#define BM_GETSTATE 0x00F2
#define BM_SETSTATE 0x00F3
#define BST_UNCHECKED 0x0000
#define BST_CHECKED 0x0001
#define BST_INDETERMINATE 0x0002
#define BST_PUSHED 0x0004

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
#define COLOR_HIGHLIGHT 13
#define COLOR_HIGHLIGHTTEXT 14
#define COLOR_GRAYTEXT 17
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
#define BDR_OUTER (BDR_RAISEDOUTER | BDR_SUNKENOUTER)
#define BDR_INNER (BDR_RAISEDINNER | BDR_SUNKENINNER)
#define EDGE_ETCHED (BDR_SUNKENOUTER | BDR_RAISEDINNER)
#define EDGE_BUMP (BDR_RAISEDOUTER | BDR_SUNKENINNER)
#define BF_LEFT 0x0001
#define BF_TOP 0x0002
#define BF_RIGHT 0x0004
#define BF_BOTTOM 0x0008
#define BF_TOPLEFT (BF_TOP | BF_LEFT)
#define BF_TOPRIGHT (BF_TOP | BF_RIGHT)
#define BF_BOTTOMLEFT (BF_BOTTOM | BF_LEFT)
#define BF_BOTTOMRIGHT (BF_BOTTOM | BF_RIGHT)
#define BF_RECT (BF_LEFT | BF_TOP | BF_RIGHT | BF_BOTTOM)
#define BF_MIDDLE 0x0800
#define BF_SOFT 0x1000
#define BF_ADJUST 0x2000
#define BF_FLAT 0x4000
#define BF_MONO 0x8000

#define DFC_CAPTION 1
#define DFC_BUTTON 4
#define DFCS_CAPTIONCLOSE 0x0000
#define DFCS_CAPTIONMIN 0x0001
#define DFCS_CAPTIONMAX 0x0002
#define DFCS_CAPTIONRESTORE 0x0003
#define DFCS_PUSHED 0x0200

/* DFC_BUTTON states */
#define DFCS_BUTTONCHECK 0x0000
#define DFCS_BUTTONRADIOIMAGE 0x0001
#define DFCS_BUTTONRADIOMASK 0x0002
#define DFCS_BUTTONRADIO 0x0004
#define DFCS_BUTTON3STATE 0x0008
#define DFCS_BUTTONPUSH 0x0010
#define DFCS_INACTIVE 0x0100
#define DFCS_CHECKED 0x0400
#define DFCS_FLAT 0x4000
#define DFCS_MONO 0x8000

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

/* CreateFont */
#define FW_NORMAL 400
#define FW_BOLD 700
#define ANSI_CHARSET 0
#define DEFAULT_CHARSET 1
#define SYMBOL_CHARSET 2
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define DEFAULT_PITCH 0
#define FIXED_PITCH 1
#define FF_DONTCARE (0 << 4)

/* ---- implemented controls -------------------------------------------------
 * examples/controls.c switches its blocks on these: a control is announced
 * here once it renders like the real one. See ROADMAP.md. */

#define WEEN32_HAS_DISABLED 1
#define WEEN32_HAS_CHECKBOX 1
#define WEEN32_HAS_RADIO 1
#define WEEN32_HAS_GROUPBOX 1

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
BOOL AdjustWindowRect(LPRECT rect, DWORD style, BOOL menu);
BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD ex_style);
UINT GetDpiForSystem(void);
BOOL MoveWindow(HWND wnd, int x, int y, int w, int h, BOOL repaint);
BOOL InvalidateRect(HWND wnd, const RECT *rect, BOOL erase);
BOOL UpdateWindow(HWND wnd);
HWND GetDlgItem(HWND dlg, int id);
int GetDlgCtrlID(HWND wnd);
HWND SetFocus(HWND wnd);
BOOL EnableWindow(HWND wnd, BOOL enable);
BOOL IsWindowEnabled(HWND wnd);
BOOL CheckDlgButton(HWND dlg, int id, UINT check);
UINT IsDlgButtonChecked(HWND dlg, int id);
BOOL CheckRadioButton(HWND dlg, int first, int last, int check);
HWND SetCapture(HWND wnd);
BOOL ReleaseCapture(void);
HWND GetCapture(void);

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

/* The dialog manager: create a dialog from a template (the manager instantiates
 * every control and maps its DLUs to pixels), run the DLGPROC, and route
 * keyboard navigation. This is how win32 dialogs were built. */
HWND CreateDialogIndirectParamA(HINSTANCE inst, LPCDLGTEMPLATEA tmpl,
                                HWND parent, DLGPROC proc, LPARAM init_param);
#define CreateDialogIndirectA(inst, tmpl, parent, proc) \
    CreateDialogIndirectParamA(inst, tmpl, parent, proc, 0)
BOOL IsDialogMessageA(HWND dlg, LPMSG msg);
LRESULT DefDlgProcA(HWND dlg, UINT msg, WPARAM wp, LPARAM lp);
BOOL EndDialog(HWND dlg, INT_PTR result);

/* ---- GDI ------------------------------------------------------------------ */

HDC BeginPaint(HWND wnd, PAINTSTRUCT *ps);
BOOL EndPaint(HWND wnd, const PAINTSTRUCT *ps);
BOOL FillRect(HDC dc, const RECT *rect, HBRUSH brush);
int FrameRect(HDC dc, const RECT *rect, HBRUSH brush);
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
HFONT CreateFontA(int height, int width, int escapement, int orientation,
                  int weight, DWORD italic, DWORD underline, DWORD strike_out,
                  DWORD charset, DWORD out_precision, DWORD clip_precision,
                  DWORD quality, DWORD pitch_and_family, LPCSTR face_name);
BOOL DeleteObject(HGDIOBJ obj);
HGDIOBJ GetStockObject(int what);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj);

#ifdef __cplusplus
}
#endif

#endif /* !_WIN32 */
#endif /* WEEN32_H */
