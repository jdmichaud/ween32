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
typedef UINT_PTR DWORD_PTR;
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
struct ween_imagelist;
typedef struct ween_imagelist *HIMAGELIST;
typedef struct ween_gdiobj *HBITMAP;
typedef void *HCURSOR;
/* A real menu, or a control id squeezed into the same parameter — win32 uses
 * one slot for both, and CreateWindowEx says which by whether the window is a
 * child. */
struct ween_menu;
typedef struct ween_menu *HMENU;

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

/* What a common control sends its parent through WM_NOTIFY. */
typedef struct tagNMHDR {
    HWND hwndFrom;
    UINT_PTR idFrom;
    UINT code;
} NMHDR;

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
#define WM_SIZE 0x0005
#define SIZE_RESTORED 0
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
#define DS_ABSALIGN 0x0001 /* the template's position is on the screen */
#define DM_SETDEFID (WM_USER + 1)
#define DC_HASDEFID 0x534B
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_COMMAND 0x0111
#define WM_TIMER 0x0113
#define WM_SYSCOMMAND 0x0112
#define WM_INITMENU 0x0116
#define WM_INITMENUPOPUP 0x0117
#define WM_MENUSELECT 0x011F
#define SC_KEYMENU 0xF100
#define WM_NOTIFY 0x004E
#define WM_VSCROLL 0x0115
#define WM_HSCROLL 0x0114
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
/* A window is only sent double-click messages if its class asked for them.
 * Without this the second of two quick clicks arrives as another plain
 * WM_LBUTTONDOWN, which is what a control that does not care about double
 * clicks wants — and what keeps rapid clicking from losing every other one. */
#define CS_DBLCLKS 0x0008
#define WM_MOUSEWHEEL 0x020A
#define WM_MOUSELEAVE 0x02A3

/* Hover tracking: ask, once, to be told when the pointer leaves. The reply is
 * a single WM_MOUSELEAVE, so a control that wants a hot state re-arms this
 * every time it is entered. */
#define TME_LEAVE 0x00000002
#define TME_CANCEL 0x80000000
typedef struct {
    DWORD cbSize;
    DWORD dwFlags;
    HWND hwndTrack;
    DWORD dwHoverTime;
} TRACKMOUSEEVENT, *LPTRACKMOUSEEVENT;
BOOL TrackMouseEvent(TRACKMOUSEEVENT *track);

/* ---- cursors -------------------------------------------------------------
 *
 * The standard shapes, named as win32 names them. A window class carries one
 * and it is set whenever the pointer is over that window; SetCursor overrides
 * it until the pointer moves somewhere else, which is how a control shows a
 * different shape over part of itself. There are no custom cursors: these are
 * the window system's own, which is what the classic shell used. */
#define IDC_ARROW ((LPCSTR)32512)
#define IDC_IBEAM ((LPCSTR)32513)
#define IDC_WAIT ((LPCSTR)32514)
#define IDC_CROSS ((LPCSTR)32515)
#define IDC_SIZENWSE ((LPCSTR)32642)
#define IDC_SIZENESW ((LPCSTR)32643)
#define IDC_SIZEWE ((LPCSTR)32644)
#define IDC_SIZENS ((LPCSTR)32645)
#define IDC_SIZEALL ((LPCSTR)32646)
#define IDC_HAND ((LPCSTR)32649)

HCURSOR LoadCursorA(HINSTANCE inst, LPCSTR name);
HCURSOR SetCursor(HCURSOR cursor);
#define WM_SETCURSOR 0x0020
#define WHEEL_DELTA 120
#define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))

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
#define WS_THICKFRAME 0x00040000L
#define WS_SIZEBOX WS_THICKFRAME
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_GROUP 0x00020000L
#define WS_TABSTOP 0x00010000L

/* extended styles: the two field borders */
#define WS_EX_DLGMODALFRAME 0x00000001L
#define WS_EX_CLIENTEDGE 0x00000200L
#define WS_EX_STATICEDGE 0x00020000L

/* EDIT styles */
#define ES_LEFT 0x0000L
#define ES_CENTER 0x0001L
#define ES_RIGHT 0x0002L
#define ES_MULTILINE 0x0004L
#define ES_AUTOVSCROLL 0x0040L
#define ES_AUTOHSCROLL 0x0080L
#define ES_READONLY 0x0800L

/* LISTBOX and COMBOBOX styles, messages and classes */
#define LBS_NOTIFY 0x0001L
#define LBS_SORT 0x0002L
#define LB_ADDSTRING 0x0180
#define LB_INSERTSTRING 0x0181
#define LB_DELETESTRING 0x0182
#define LB_RESETCONTENT 0x0184
#define LB_SETCURSEL 0x0186
#define LB_GETCURSEL 0x0188
#define LB_GETTEXT 0x0189
#define LB_GETCOUNT 0x018B
#define LB_GETTOPINDEX 0x018E
#define LB_SETTOPINDEX 0x0197
#define LBN_SELCHANGE 1
#define LBN_DBLCLK 2

#define CBS_SIMPLE 0x0001L
#define CBS_DROPDOWN 0x0002L
#define CBS_DROPDOWNLIST 0x0003L
#define CB_ADDSTRING 0x0143
#define CB_DELETESTRING 0x0144
#define CB_GETCOUNT 0x0146
#define CB_GETCURSEL 0x0147
#define CB_GETLBTEXT 0x0148
#define CB_INSERTSTRING 0x014A
#define CB_RESETCONTENT 0x014B
#define CB_SETCURSEL 0x014E
#define CBN_SELCHANGE 1
#define EN_CHANGE 0x0300
#define EN_UPDATE 0x0400

/* trackbar (comctl32) */
#define TRACKBAR_CLASSA "msctls_trackbar32"
#define TBS_AUTOTICKS 0x0001L
#define TBS_VERT 0x0002L
#define TBS_HORZ 0x0000L
#define TBS_BOTH 0x0008L
#define TBS_NOTICKS 0x0010L
#define TBM_GETPOS (WM_USER)
#define TBM_SETPOS (WM_USER + 5)
#define TBM_SETRANGE (WM_USER + 6)
#define TBM_SETRANGEMIN (WM_USER + 7)
#define TBM_SETRANGEMAX (WM_USER + 8)
#define TBM_SETTICFREQ (WM_USER + 20)

/* tree view and list view (comctl32) */
#define WC_TREEVIEWA "SysTreeView32"
#define WC_LISTVIEWA "SysListView32"
#define TVS_HASBUTTONS 0x0001L
#define TVS_HASLINES 0x0002L
#define TVS_LINESATROOT 0x0004L
#define TVS_SHOWSELALWAYS 0x0020L
#define TVIF_TEXT 0x0001
#define TVIF_IMAGE 0x0002
#define TVSIL_NORMAL 0
#define TVI_ROOT ((HTREEITEM)(UINT_PTR)-0x10000)
#define TVI_FIRST ((HTREEITEM)(UINT_PTR)-0x0FFFF)
#define TVI_LAST ((HTREEITEM)(UINT_PTR)-0x0FFFE)
#define TVE_COLLAPSE 0x0001
#define TVE_EXPAND 0x0002
#define TV_FIRST 0x1100
#define TVM_INSERTITEMA (TV_FIRST + 0)
#define TVM_SETIMAGELIST (TV_FIRST + 9)
#define TVM_DELETEITEM (TV_FIRST + 1)
#define TVM_GETNEXTITEM (TV_FIRST + 10)
#define TVM_GETITEMA (TV_FIRST + 12)
/* which item TVM_GETNEXTITEM is being asked for */
#define TVGN_ROOT 0x0000
#define TVGN_NEXT 0x0001
#define TVGN_PARENT 0x0003
#define TVGN_CHILD 0x0004
#define TVGN_CARET 0x0009
#define TVM_EXPAND (TV_FIRST + 2)
#define TVM_SELECTITEM (TV_FIRST + 11)

typedef struct ween_tvitem *HTREEITEM;
typedef struct tagTVITEMA {
    UINT mask;
    HTREEITEM hItem;
    UINT state, stateMask;
    LPSTR pszText;
    int cchTextMax, iImage, iSelectedImage, cChildren;
    LPARAM lParam;
} TVITEMA;
typedef struct tagTVINSERTSTRUCTA {
    HTREEITEM hParent;
    HTREEITEM hInsertAfter;
    TVITEMA item;
} TVINSERTSTRUCTA;

#define LVS_REPORT 0x0001L
#define LVS_SINGLESEL 0x0004L
#define LVS_SHOWSELALWAYS 0x0008L
#define LVIF_TEXT 0x0001
#define LVIF_IMAGE 0x0002
#define LVSIL_SMALL 1
#define LVIS_FOCUSED 0x0001
#define LVIS_SELECTED 0x0002
#define LVCF_WIDTH 0x0002
#define LVCF_TEXT 0x0004
#define LVM_FIRST 0x1000
#define LVM_INSERTCOLUMNA (LVM_FIRST + 27)
#define LVM_INSERTITEMA (LVM_FIRST + 7)
#define LVM_SETIMAGELIST (LVM_FIRST + 3)
#define LVM_DELETEALLITEMS (LVM_FIRST + 9)
#define LVM_GETITEMCOUNT (LVM_FIRST + 4)
#define LVM_GETNEXTITEM (LVM_FIRST + 12)
#define LVM_SETCOLUMNWIDTH (LVM_FIRST + 30)
#define LVM_ENSUREVISIBLE (LVM_FIRST + 19)
#define LVNI_SELECTED 0x0002
#define LVM_SETITEMTEXTA (LVM_FIRST + 46)
#define LVM_SETITEMSTATE (LVM_FIRST + 43)

typedef struct tagLVCOLUMNA {
    UINT mask;
    int fmt, cx;
    LPSTR pszText;
    int cchTextMax, iSubItem, iImage, iOrder;
} LVCOLUMNA;
typedef struct tagLVITEMA {
    UINT mask;
    int iItem, iSubItem;
    UINT state, stateMask;
    LPSTR pszText;
    int cchTextMax, iImage;
    LPARAM lParam;
} LVITEMA;
#define ListView_SetItemState(w, i, data, mask)                                \
    do {                                                                       \
        LVITEMA lv_;                                                           \
        memset(&lv_, 0, sizeof lv_);                                           \
        lv_.state = (data);                                                    \
        lv_.stateMask = (mask);                                                \
        SendMessageA((w), LVM_SETITEMSTATE, (WPARAM)(i), (LPARAM)&lv_);        \
    } while (0)

/* tab control (comctl32) */
#define WC_TABCONTROLA "SysTabControl32"
#define TCS_MULTILINE 0x0200L
#define TCM_FIRST 0x1300
#define TCM_INSERTITEMA (TCM_FIRST + 7)
#define TCM_SETCURSEL (TCM_FIRST + 12)
#define TCM_GETCURSEL (TCM_FIRST + 11)
#define TCM_ADJUSTRECT (TCM_FIRST + 40)
#define TCIF_TEXT 0x0001
#define TCIF_IMAGE 0x0002
#define TCN_SELCHANGE (0U - 551U)
#define TVN_SELCHANGEDA (0U - 401U)
#define TVN_ITEMEXPANDEDA (0U - 406U)
#define LVN_ITEMCHANGED (0U - 101U)
#define LVN_COLUMNCLICK (0U - 108U)

/* What a list view sends with LVN_COLUMNCLICK: iSubItem is the column, which
 * is what an app sorts on. */
typedef struct tagNMLISTVIEW {
    NMHDR hdr;
    int iItem;
    int iSubItem;
    UINT uNewState;
    UINT uOldState;
    UINT uChanged;
    POINT ptAction;
    LPARAM lParam;
} NMLISTVIEW;

/* scroll-bar notification codes */
#define SB_LINEUP 0
#define SB_LINELEFT 0
#define SB_LINEDOWN 1
#define SB_LINERIGHT 1
#define SB_PAGEUP 2
#define SB_PAGELEFT 2
#define SB_PAGEDOWN 3
#define SB_PAGERIGHT 3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK 5
#define SB_ENDSCROLL 8

typedef struct tagTCITEMA {
    UINT mask;
    DWORD dwState;
    DWORD dwStateMask;
    LPSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
} TCITEMA;

/* status bar (comctl32) */
#define STATUSCLASSNAMEA "msctls_statusbar32"
#define SBARS_SIZEGRIP 0x0100L
#define SB_SETTEXTA (WM_USER + 1)
#define SB_GETTEXTA (WM_USER + 2)
#define SB_SETPARTS (WM_USER + 4)
#define SB_GETPARTS (WM_USER + 6)
#define SB_SIMPLE (WM_USER + 9)

/* progress bar (comctl32) */
#define PROGRESS_CLASSA "msctls_progress32"
#define PBS_SMOOTH 0x01
#define PBS_VERTICAL 0x04
#define PBM_SETRANGE (WM_USER + 1)
#define PBM_SETPOS (WM_USER + 2)
#define PBM_DELTAPOS (WM_USER + 3)
#define PBM_SETSTEP (WM_USER + 4)
#define PBM_STEPIT (WM_USER + 5)
#define PBM_SETRANGE32 (WM_USER + 6)

/* SCROLLBAR styles */
#define SBS_HORZ 0x0000L
#define SBS_VERT 0x0001L

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
#define BM_CLICK 0x00F5
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
#define HTMENU 5
#define HTLEFT 10
#define HTRIGHT 11
#define HTTOP 12
#define HTTOPLEFT 13
#define HTTOPRIGHT 14
#define HTBOTTOM 15
#define HTBOTTOMLEFT 16
#define HTBOTTOMRIGHT 17
#define HTCLOSE 20
#define HTMINBUTTON 8
#define HTMAXBUTTON 9
/* what a caption button asks the window to do */
#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_CLOSE 0xF060
#define SC_RESTORE 0xF120

/* ---- ShowWindow --------------------------------------------------------- */

#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5

/* ---- virtual keys ------------------------------------------------------- */

#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_MENU 0x12 /* Alt */
#define VK_PRIOR 0x21 /* Page Up */
#define VK_NEXT 0x22  /* Page Down */
#define VK_F1 0x70
#define VK_F10 0x79
#define VK_SPACE 0x20
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2E

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
#define DT_NOPREFIX 0x00000800

/* ---- GDI misc ------------------------------------------------------------ */

#define TRANSPARENT 1
#define OPAQUE 2
#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
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
#define WEEN32_HAS_EDIT 1
#define WEEN32_HAS_SCROLLBAR 1
#define WEEN32_HAS_LISTBOX 1
#define WEEN32_HAS_COMBOBOX 1
#define WEEN32_HAS_PROGRESS 1
#define WEEN32_HAS_STATUSBAR 1
#define WEEN32_HAS_TABS 1
#define WEEN32_HAS_TREEVIEW 1
#define WEEN32_HAS_LISTVIEW 1
#define WEEN32_HAS_TRACKBAR 1
/* ---- toolbar --------------------------------------------------------------
 *
 * A row of flat buttons: no edge until the pointer is over one, a raised edge
 * when it is, and a sunken edge over a dithered background when one is held or
 * checked. That is the Windows 2000 shell's toolbar, and the metrics here are
 * measured off one — twenty-two tall, the icon six pixels in, the text
 * twenty-four.
 *
 * A button is a TBBUTTON: an image index, a command id, a state and a style.
 * iString may be a pointer to the button's text, which is what comctl32 5
 * allows and what an app writing its own toolbar actually does. */
#define TOOLBARCLASSNAMEA "ToolbarWindow32"

#define TBSTYLE_BUTTON 0x0000
#define TBSTYLE_SEP 0x0001
#define TBSTYLE_CHECK 0x0002
#define TBSTYLE_DROPDOWN 0x0008
#define TBSTYLE_FLAT 0x0800
#define TBSTYLE_LIST 0x1000 /* the text beside the icon, not under it */

#define TBSTATE_CHECKED 0x01
#define TBSTATE_PRESSED 0x02
#define TBSTATE_ENABLED 0x04
#define TBSTATE_HIDDEN 0x08

typedef struct {
    int iBitmap;
    int idCommand;
    BYTE fsState;
    BYTE fsStyle;
    BYTE bReserved[2];
    DWORD_PTR dwData;
    INT_PTR iString;
} TBBUTTON, *LPTBBUTTON;

#define TB_ENABLEBUTTON (WM_USER + 1)
#define TB_CHECKBUTTON (WM_USER + 2)
#define TB_ISBUTTONCHECKED (WM_USER + 12)
#define TB_ISBUTTONENABLED (WM_USER + 9)
#define TB_ADDBUTTONSA (WM_USER + 20)
#define TB_BUTTONSTRUCTSIZE (WM_USER + 30)
#define TB_SETIMAGELIST (WM_USER + 48)
#define TB_GETITEMRECT (WM_USER + 29)
#define TB_BUTTONCOUNT (WM_USER + 24)
#define TB_AUTOSIZE (WM_USER + 33)

/* The arrow beside a drop-down button was pressed: show the menu. */
#define TBN_DROPDOWN (0U - 710U)
typedef struct {
    NMHDR hdr;
    int iItem;
} NMTOOLBAR;

#define WEEN32_HAS_TOOLBAR 1
#define WEEN32_HAS_MENU 1
#define WEEN32_HAS_MESSAGEBOX 1
#define WEEN32_HAS_DIALOGBOX 1
#define WEEN32_HAS_ACCELERATORS 1
#define WEEN32_HAS_IMAGELIST 1
#define WEEN32_HAS_CLIPBOARD 1

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
/* Timers. SetTimer with an id that is already running resets it; with a NULL
 * window it allocates an id and returns it. lpTimerFunc, when given, is called
 * by DispatchMessage instead of the window procedure. */
typedef void(CALLBACK *TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
UINT_PTR SetTimer(HWND wnd, UINT_PTR id, UINT elapse_ms, TIMERPROC fn);
BOOL KillTimer(HWND wnd, UINT_PTR id);

/* ---- menus ---------------------------------------------------------------
 *
 * A menu is a list of items: a string, a separator (MF_SEPARATOR), or a
 * submenu (MF_POPUP, with the HMENU passed in the id). A window's menu is
 * drawn above its client area and opens its drop-downs on a click; a popup
 * can also be tracked anywhere on screen with TrackPopupMenu. An item's text
 * may hold a tab, and what follows it is right-aligned as the accelerator. */
#define MF_STRING 0x0000
#define MF_ENABLED 0x0000
#define MF_UNCHECKED 0x0000
#define MF_BYCOMMAND 0x0000
#define MF_GRAYED 0x0001
#define MF_DISABLED 0x0002
#define MF_CHECKED 0x0008
#define MF_POPUP 0x0010
#define MF_SEPARATOR 0x0800
#define MF_DEFAULT 0x1000 /* drawn bold: the one a double click would pick */

#define TPM_LEFTALIGN 0x0000
#define TPM_RIGHTBUTTON 0x0002
#define TPM_RETURNCMD 0x0100

HMENU CreateMenu(void);
HMENU CreatePopupMenu(void);
BOOL DestroyMenu(HMENU menu);
BOOL AppendMenuA(HMENU menu, UINT flags, UINT_PTR id, LPCSTR text);
BOOL SetMenu(HWND wnd, HMENU menu);
HMENU GetMenu(HWND wnd);
HMENU GetSubMenu(HMENU menu, int pos);
DWORD CheckMenuItem(HMENU menu, UINT id, UINT check);
BOOL EnableMenuItem(HMENU menu, UINT id, UINT enable);
BOOL TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved,
                    HWND owner, const RECT *unused);

/* ---- system metrics ----------------------------------------------------- */
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CYCAPTION 4
#define SM_CXBORDER 5
#define SM_CYBORDER 6
#define SM_CYMENU 15
#define SM_CXVSCROLL 2
#define SM_CYHSCROLL 3
#define SM_CXMENUCHECK 71
#define SM_CYMENUCHECK 72
int GetSystemMetrics(int index);
BOOL GetWindowRect(HWND wnd, LPRECT rect);

/* ---- the clipboard -------------------------------------------------------
 *
 * Open it, empty it, put something in, close it. The data belongs to the
 * clipboard once handed over — do not free it — and what GetClipboardData
 * returns stays valid until the next thing replaces it.
 *
 * WM_CUT/WM_COPY/WM_PASTE are what a control acts on; an EDIT also takes
 * Ctrl+X, Ctrl+C, Ctrl+V and Ctrl+A directly. */
typedef void *HANDLE;
#define CF_TEXT 1
#define WM_CUT 0x0300
#define WM_COPY 0x0301
#define WM_PASTE 0x0302
#define WM_CLEAR 0x0303
BOOL OpenClipboard(HWND owner);
BOOL CloseClipboard(void);
BOOL EmptyClipboard(void);
HANDLE SetClipboardData(UINT format, HANDLE data);
HANDLE GetClipboardData(UINT format);
BOOL IsClipboardFormatAvailable(UINT format);

/* ---- bitmaps and image lists ---------------------------------------------
 *
 * An image list holds any number of images, all one size, and a control names
 * one by index — which is how a tree or list view shows an icon beside a
 * label. Transparency is one bit per pixel, as the classic shell had it.
 *
 * LoadImageA reads a .bmp from disk (LR_LOADFROMFILE): there are no resources
 * to load from, ween32 having no .exe to hold them. */
#define IMAGE_BITMAP 0
#define IMAGE_ICON 1
#define LR_LOADFROMFILE 0x0010
#define CLR_NONE 0xFFFFFFFF
#define ILC_COLOR 0x0000
#define ILC_MASK 0x0001
#define ILD_NORMAL 0x0000
#define ILD_TRANSPARENT 0x0001

HBITMAP CreateBitmap(int w, int h, UINT planes, UINT bpp, const void *bits);
HANDLE LoadImageA(HINSTANCE inst, LPCSTR name, UINT type, int cx, int cy,
                  UINT flags);
HIMAGELIST ImageList_Create(int cx, int cy, UINT flags, int initial, int grow);
BOOL ImageList_Destroy(HIMAGELIST il);
int ImageList_Add(HIMAGELIST il, HBITMAP image, HBITMAP mask);
int ImageList_AddMasked(HIMAGELIST il, HBITMAP image, COLORREF transparent);
int ImageList_GetImageCount(HIMAGELIST il);
BOOL ImageList_GetIconSize(HIMAGELIST il, int *cx, int *cy);
BOOL ImageList_Draw(HIMAGELIST il, int index, HDC dc, int x, int y, UINT style);
/* An icon carries its own transparency mask, which is what makes it an icon
 * and not a bitmap. LoadImageA reads one from a .ico file with IMAGE_ICON. */
int ImageList_AddIcon(HIMAGELIST il, HICON icon);
void DestroyIcon(HICON icon);
BOOL DrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy, UINT frame,
                HBRUSH flicker, UINT flags);
#define DI_NORMAL 0x0003

/* ---- accelerators ---------------------------------------------------------
 *
 * A table of key combinations and the command each one sends. The app offers
 * every message to TranslateAcceleratorA before dispatching it, exactly as it
 * offers them to IsDialogMessageA; a match becomes a WM_COMMAND and the
 * message goes no further. */
#define FVIRTKEY 0x01
#define FSHIFT 0x04
#define FCONTROL 0x08
#define FALT 0x10

typedef struct {
    BYTE fVirt;
    WORD key;
    WORD cmd;
} ACCEL, *LPACCEL;

struct ween_accel;
typedef struct ween_accel *HACCEL;

HACCEL CreateAcceleratorTableA(LPACCEL entries, int count);
BOOL DestroyAcceleratorTable(HACCEL table);
int TranslateAcceleratorA(HWND wnd, HACCEL table, LPMSG msg);

/* Move the focus to the next or previous control, or to a named one — what a
 * dialog sends itself rather than calling SetFocus, so the dialog manager can
 * keep track of the default button. */
#define WM_NEXTDLGCTL 0x0028

BOOL PostMessageA(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
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
/* ---- message box --------------------------------------------------------- */
#define MB_OK 0x00000000
#define MB_OKCANCEL 0x00000001
#define MB_YESNO 0x00000004
#define IDYES 6
#define IDNO 7
int MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type);

BOOL EndDialog(HWND dlg, INT_PTR result);
/* Modal: does not return until EndDialog is called, and the owner cannot be
 * used in the meantime. */
INT_PTR DialogBoxIndirectParamA(HINSTANCE inst, LPCDLGTEMPLATEA tmpl,
                                HWND owner, DLGPROC proc, LPARAM param);
#define DialogBoxIndirectA(inst, tmpl, parent, proc)                           \
    DialogBoxIndirectParamA(inst, tmpl, parent, proc, 0)

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
