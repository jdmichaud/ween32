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
/* On Windows, ween32 *is* win32: use the real thing — all of it a program
 * written against this header uses, so it needs no #ifdef of its own to
 * reach the common controls or the message crackers. */
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
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
typedef int16_t SHORT;
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

/* What an owner-drawn control asks its parent before it lays anything out.
 * The shell's file dialog answers it for the "Look in" box, which is why that
 * box is a pixel taller than the plain one below it. */
typedef struct tagMEASUREITEMSTRUCT {
    UINT CtlType;
    UINT CtlID;
    UINT itemID;
    UINT itemWidth;
    UINT itemHeight;
    UINT_PTR itemData;
} MEASUREITEMSTRUCT, *LPMEASUREITEMSTRUCT;

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
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ENABLE 0x000A
/* What a control says it wants from the dialog manager. Only the one bit is
 * acted on so far: a control that keeps a selection has all of it selected
 * when a dialog gives it the focus, so that typing replaces what is there. */
#define WM_GETDLGCODE 0x0087
#define DLGC_WANTARROWS 0x0001
#define DLGC_WANTTAB 0x0002
#define DLGC_WANTALLKEYS 0x0004
#define DLGC_WANTMESSAGE 0x0004
#define DLGC_HASSETSEL 0x0008
#define DLGC_DEFPUSHBUTTON 0x0010
#define DLGC_UNDEFPUSHBUTTON 0x0020
#define DLGC_RADIOBUTTON 0x0040
#define DLGC_WANTCHARS 0x0080
#define DLGC_STATIC 0x0100
#define DLGC_BUTTON 0x2000

#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_SETICON 0x0080
#define WM_GETICON 0x007F
#define ICON_SMALL 0
#define ICON_BIG 1
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_DRAWITEM 0x002B
#define WM_MEASUREITEM 0x002C
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
/* Which item of an open menu the highlight is on, so an application can say
 * what it does — the line a shell writes in its status bar. wParam is the
 * item's id, or its position when it is a submenu; the high word carries the
 * MF_ flags, and MF_POPUP marks a submenu. Both words are 0xffff and lParam
 * NULL when the menu closes. */
#define WM_MENUSELECT 0x011F
#define SC_KEYMENU 0xF100
#define SC_CONTEXTHELP 0xF180
#define WM_NOTIFY 0x004E
#define WM_VSCROLL 0x0115
#define WM_HSCROLL 0x0114
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
/* Sent once the right button comes back up, carrying the point in desktop
 * coordinates — which is where a context menu goes. DefWindowProc sends it,
 * so a window that does not want one need do nothing. */
#define WM_CONTEXTMENU 0x007B
/* What was held when a mouse message was sent, in its wParam — a list view
 * asks, because Ctrl adds to a selection and Shift extends it. */
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_MBUTTON 0x0010
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
/* A window is only sent double-click messages if its class asked for them.
 * Without this the second of two quick clicks arrives as another plain
 * WM_LBUTTONDOWN, which is what a control that does not care about double
 * clicks wants — and what keeps rapid clicking from losing every other one. */
#define CS_DBLCLKS 0x0008
#define WM_MOUSEWHEEL 0x020A
#define WM_MOUSELEAVE 0x02A3

/* The keyboard cues: whether the underlines under mnemonics and the focus
 * rectangles are showing. Windows keeps this per window and DefWindowProc
 * answers for it, so an application that draws its own labels — a menu in a
 * rebar band, say — asks rather than guesses. Alt is what turns the
 * underlines on; ween32 answers for the whole process, which is as much as
 * one keyboard can mean. */
#define WM_CHANGEUISTATE 0x0127
#define WM_UPDATEUISTATE 0x0128
#define WM_QUERYUISTATE 0x0129
#define UIS_SET 1
#define UIS_CLEAR 2
#define UIS_INITIALIZE 3
#define UISF_HIDEFOCUS 0x1
#define UISF_HIDEACCEL 0x2
#define UISF_ACTIVE 0x4

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
/* Whether Shift, Control or Alt is down now, as win32 reports it: the high
 * bit set means held. Asked mid-gesture, when there is no message to read
 * it from -- constraining a drag to a square while it is being dragged. */
SHORT GetKeyState(int vk);

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
/* A cursor of the application's own, out of the two masks win32 has always
 * taken: one bit per pixel, rows padded to a byte, AND first. A clear AND
 * bit is a pixel that is drawn, and the XOR bit is then black or white. */
HCURSOR CreateCursor(HINSTANCE inst, int xhot, int yhot, int width,
                     int height, const void *and_plane, const void *xor_plane);
BOOL DestroyCursor(HCURSOR cursor);
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
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
/* The frame an application window wears: a caption with a system menu, a
 * sizing border, and both size boxes. */
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

/* extended styles: the two field borders */
#define WS_EX_DLGMODALFRAME 0x00000001L
#define WS_EX_CLIENTEDGE 0x00000200L
#define WS_EX_CONTROLPARENT 0x00010000L
/* A caption with a question mark in it, left of the close box: the window is
 * offering per-control help. A window with either size box does not get one,
 * which is why it is a dialog's mark. */
#define WS_EX_CONTEXTHELP 0x00000400L
/* A window that does not take the keyboard when it appears — which is what a
 * menu is: the window under it keeps its focus, and its caret with it. */
#define WS_EX_NOACTIVATE 0x08000000L
/* A question mark in the caption, before the close box. A window that has it
 * has no minimise or maximise box: the three do not share the strip. */
#define WS_EX_CONTEXTHELP 0x00000400L
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
/* Keep the bar there, greyed, when everything fits. Without it a list box
 * that has nothing to scroll shows no bar at all. */
#define LBS_DISABLENOSCROLL 0x1000L
/* Take the height given rather than trimming it to whole rows. The last row
 * is then cut off by the bottom, which is what the box under an address bar
 * does. */
#define LBS_NOINTEGRALHEIGHT 0x0100L
#define LB_ADDSTRING 0x0180
#define LB_INSERTSTRING 0x0181
#define LB_DELETESTRING 0x0182
#define LB_RESETCONTENT 0x0184
#define LB_SETCURSEL 0x0186
#define LB_GETCURSEL 0x0188
#define LB_GETTEXT 0x0189
#define LB_GETTEXTLEN 0x018A
/* What a list box says when it is asked about an item it has not got. */
#define LB_ERR (-1)
#define LB_GETCOUNT 0x018B
#define LB_GETTOPINDEX 0x018E
#define LB_GETITEMHEIGHT 0x01A1
#define LB_SETITEMHEIGHT 0x01A0
#define LB_SETTOPINDEX 0x0197
#define LBN_SELCHANGE 1
#define LBN_DBLCLK 2

#define CBS_SIMPLE 0x0001L
#define CBS_DROPDOWN 0x0002L
#define CBS_DROPDOWNLIST 0x0003L
#define CBS_OWNERDRAWFIXED 0x0010L
#define CB_ERR (-1)
#define CB_ADDSTRING 0x0143
#define CB_DELETESTRING 0x0144
#define CB_GETCOUNT 0x0146
#define CB_GETCURSEL 0x0147
#define CB_GETLBTEXT 0x0148
#define CB_GETLBTEXTLEN 0x0149
#define CB_INSERTSTRING 0x014A
#define CB_RESETCONTENT 0x014B

/* ComboBoxEx: a combo box whose items carry an image and an indent. It is
 * what a shell's address bar is — a path shown as the tree it walks down. */
#define WC_COMBOBOXEXA "ComboBoxEx32"
#define CBEM_INSERTITEMA (WM_USER + 1)
/* How tall the field is. A combo box works it out from its font; an
 * application that has been given a height to fit into says so instead. */
#define CB_SETITEMHEIGHT 0x0153
#define CB_GETITEMHEIGHT 0x0154

/* What a combo box is made of, so a program can put something beside one of
 * its parts. The rectangles are in the combo box's client coordinates. */
typedef struct tagCOMBOBOXINFO {
    DWORD cbSize;
    RECT rcItem;   /* where the text sits: the field, less the button */
    RECT rcButton; /* the arrow at the end */
    DWORD stateButton;
    HWND hwndCombo;
    HWND hwndItem; /* the edit control, when it has one */
    HWND hwndList;
} COMBOBOXINFO, *LPCOMBOBOXINFO;

BOOL GetComboBoxInfo(HWND combo, COMBOBOXINFO *info);
#define CB_GETITEMHEIGHT 0x0154
#define CBEM_SETIMAGELIST (WM_USER + 2)
#define CBEIF_TEXT 0x0001
#define CBEIF_IMAGE 0x0002
#define CBEIF_SELECTEDIMAGE 0x0004
#define CBEIF_INDENT 0x0010
typedef struct tagCOMBOBOXEXITEMA {
    UINT mask;
    INT_PTR iItem;
    LPSTR pszText;
    int cchTextMax;
    int iImage;
    int iSelectedImage;
    int iOverlay;
    int iIndent;
    LPARAM lParam;
} COMBOBOXEXITEMA;
#define CB_SETCURSEL 0x014E
/* Drop the list, or put it away, without the button being pressed — which is
 * how a list of suggestions comes up as you type. */
#define CB_SHOWDROPDOWN 0x014F
#define CB_GETDROPPEDSTATE 0x0157
#define CBN_SELCHANGE 1
#define CBN_EDITCHANGE 5
#define CBN_DROPDOWN 7

/* A ComboBoxEx whose field can be typed in says when the typing is over, and
 * why: Enter, Escape, the focus going elsewhere, or the list being dropped.
 * An address bar is this — the path you are looking at, there to be edited. */
#define CBEN_FIRST (0U - 800U)
#define CBEN_ENDEDITA (CBEN_FIRST - 5)
#define CBENF_KILLFOCUS 1
#define CBENF_RETURN 2
#define CBENF_ESCAPE 3
#define CBENF_DROPDOWN 4
#define CBEMAXSTRLEN 260

typedef struct {
    NMHDR hdr;
    BOOL fChanged;      /* whether the text is not what it was */
    int iNewSelection;  /* the item picked from the list, or -1 */
    char szText[CBEMAXSTRLEN];
    int iWhy;           /* one of the CBENF_ above */
} NMCBEENDEDITA;

/* The field itself, for an application that wants to reach into it. */
#define CBEM_GETEDITCONTROL (WM_USER + 7)
#define EN_SETFOCUS 0x0100
#define EN_KILLFOCUS 0x0200
#define EN_CHANGE 0x0300
#define EN_UPDATE 0x0400
/* Select a run of the text: wParam is where it starts, lParam where it ends,
 * and -1 for the end means all of it. */
#define EM_SETSEL 0x00B1
/* What an edit leaves before and after its text. It works one out from the
 * font by default; a control that puts an edit inside itself says otherwise,
 * which is how a combo box lines its field up with what it draws beside it. */
#define EM_SETMARGINS 0x00D3
#define EC_LEFTMARGIN 0x0001
#define EC_RIGHTMARGIN 0x0002

/* trackbar (comctl32) */
#define TRACKBAR_CLASSA "msctls_trackbar32"

/* ---- the up-down: the pair of arrows beside a number ---------------------
 * Two buttons stacked, stepping a value up and down. Given a buddy — the
 * field it belongs to — it writes the value there and reads it back, which is
 * what makes the pair behave as one thing. */
#define UPDOWN_CLASSA "msctls_updown32"
#define UDS_WRAP 0x0001
#define UDS_SETBUDDYINT 0x0002
#define UDS_ALIGNRIGHT 0x0004
#define UDS_ALIGNLEFT 0x0008
#define UDS_ARROWKEYS 0x0020
#define UDS_NOTHOUSANDS 0x0080
#define UDM_SETRANGE (WM_USER + 101)
#define UDM_GETRANGE (WM_USER + 102)
#define UDM_SETPOS (WM_USER + 103)
#define UDM_GETPOS (WM_USER + 104)
#define UDM_SETBUDDY (WM_USER + 105)
#define UDM_GETBUDDY (WM_USER + 106)
#define UDN_DELTAPOS (0U - 722U)
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
#define TVIF_HANDLE 0x0010
#define TVIF_SELECTEDIMAGE 0x0020
/* cChildren says an item can be opened before anything is under it, so a
 * tree that fills a level only when it is opened still shows the box to open
 * it with. */
#define TVIF_CHILDREN 0x0040
#define TVSIL_NORMAL 0
/* The pictures a tree draws *before* an item — a tick box, an option button —
 * kept in a list of their own. An item names one in the top four bits of its
 * state, which is what INDEXTOSTATEIMAGEMASK builds; zero means none, and the
 * column is reserved for every item once the list is set. */
#define TVSIL_STATE 2
#define TVIS_STATEIMAGEMASK 0xF000
#define TVIF_STATE 0x0008
/* Where an item goes among its brothers and sisters: at the front, at the
 * back, or in the place its own text puts it — which is how a shell's folder
 * tree comes out in alphabetical order without the application sorting
 * anything. Anything else is the item to put it after. */
#define TVI_ROOT ((HTREEITEM)(UINT_PTR)-0x10000)
#define TVI_FIRST ((HTREEITEM)(UINT_PTR)-0x0FFFF)
#define TVI_LAST ((HTREEITEM)(UINT_PTR)-0x0FFFE)
#define TVI_SORT ((HTREEITEM)(UINT_PTR)-0x0FFFD)
#define TVE_COLLAPSE 0x0001
#define TVE_EXPAND 0x0002
#define TV_FIRST 0x1100
#define TVM_INSERTITEMA (TV_FIRST + 0)
#define TVM_SETIMAGELIST (TV_FIRST + 9)
#define TVM_DELETEITEM (TV_FIRST + 1)
#define TVM_GETNEXTITEM (TV_FIRST + 10)
#define TVM_GETITEMA (TV_FIRST + 12)
#define TVM_SETITEMA (TV_FIRST + 13)
/* which item TVM_GETNEXTITEM is being asked for */
#define TVGN_ROOT 0x0000
#define TVGN_NEXT 0x0001
#define TVGN_PARENT 0x0003
#define TVGN_CHILD 0x0004
#define TVGN_CARET 0x0009
#define TVM_EXPAND (TV_FIRST + 2)
#define TVM_SELECTITEM (TV_FIRST + 11)
#define TVM_HITTEST (TV_FIRST + 17)
#define TVHT_ONITEMICON 0x0002
#define TVHT_ONITEMLABEL 0x0004
#define TVHT_ONITEMBUTTON 0x0010
#define TVHT_ONITEMRIGHT 0x0020
#define TVHT_ONITEMSTATEICON 0x0040
#define TVHT_ONITEM (TVHT_ONITEMICON | TVHT_ONITEMLABEL | TVHT_ONITEMSTATEICON)

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

typedef struct tagTVHITTESTINFO {
    POINT pt;
    UINT flags;
    HTREEITEM hItem;
} TVHITTESTINFO;

/* How a folder is shown. The low two bits are the view: big icons in a grid,
 * a column of rows with details, small icons across, or a list down. */
#define LVS_ICON 0x0000L
#define LVS_REPORT 0x0001L
#define LVS_SMALLICON 0x0002L
#define LVS_LIST 0x0003L
#define LVS_TYPEMASK 0x0003L
#define LVS_SINGLESEL 0x0004L
#define LVS_SHOWSELALWAYS 0x0008L
#define LVS_SHAREIMAGELISTS 0x0040L
/* A row's label can be typed over in place — which is what Rename is, and
 * what a folder just made is left in. */
#define LVS_EDITLABELS 0x0200L
/* No heading strip: the columns are there but nothing names them, which is
 * what a list used as a plain roll of items wants. */
#define LVS_NOCOLUMNHEADER 0x4000
#define LVIF_TEXT 0x0001
#define LVIF_IMAGE 0x0002
#define LVIF_STATE 0x0008
/* Two sets of pictures: the big ones the icon view draws and the small ones
 * every other view draws. */
#define LVSIL_NORMAL 0
#define LVSIL_SMALL 1
#define LVIS_FOCUSED 0x0001
/* The picture drawn before a row, as a number in the top nibble: a list view
 * with check boxes uses 1 for cleared and 2 for ticked, and 0 for none. */
#define LVIS_STATEIMAGEMASK 0xF000
#define INDEXTOSTATEIMAGEMASK(i) ((i) << 12)
/* A cut item, drawn ghosted: the shell marks a hidden file this way too, and
 * what it comes to is the icon blended half way into the background. */
#define LVIS_CUT 0x0004
#define LVIS_SELECTED 0x0002
#define LVCF_FMT 0x0001
#define LVCF_WIDTH 0x0002
/* A column's cells and its own heading are laid out to this. The shell puts
 * a file's size on the right of its column and everything else on the left. */
#define LVCFMT_LEFT 0x0000
#define LVCFMT_RIGHT 0x0001
/* ---- the header inside a list view --------------------------------------
 *
 * A report-view list keeps its column headings in a header control, and an
 * application reaches it with LVM_GETHEADER and then talks to it: HDM_SETITEM
 * with HDI_FORMAT is how the arrow that says "sorted by this column" is asked
 * for. ween32's list draws the band itself and the header is where the
 * columns are said to be — see the ROADMAP for the header drawing its own.
 */
#define WC_HEADERA "SysHeader32"
#define HDM_FIRST 0x1200
#define HDM_GETITEMCOUNT (HDM_FIRST + 0)
#define HDM_GETITEMA (HDM_FIRST + 3)
#define HDM_SETITEMA (HDM_FIRST + 4)
#define HDI_WIDTH 0x0001
#define HDI_HEIGHT 0x0001
#define HDI_TEXT 0x0002
#define HDI_FORMAT 0x0004
#define HDI_LPARAM 0x0008
#define HDI_BITMAP 0x0010
#define HDI_IMAGE 0x0020
#define HDI_ORDER 0x0080
/* A heading draws its text only when its format says it has one: taking the
 * format apart and putting it back without this is how a column comes to
 * have no name. */
#define HDF_STRING 0x4000
#define HDF_LEFT 0x0000
#define HDF_RIGHT 0x0001
#define HDF_CENTER 0x0002
/* A heading dragged to another place: the column moves with it, cells and
 * all. The view says so afterwards, the way comctl32's header does, and an
 * application that keeps its own idea of the order — a shell with a Choose
 * Columns dialog — puts that idea right from it. */
#define HDN_FIRST (0U - 300U)
#define HDN_ENDDRAG (HDN_FIRST - 11)
typedef struct {
    NMHDR hdr;
    int iItem;  /* the column that moved */
    int iButton;
    void *pitem;
} NMHEADERA;
#define HDF_SORTDOWN 0x0200
#define HDF_SORTUP 0x0400
typedef struct {
    UINT mask;
    int cxy;
    LPSTR pszText;
    HBITMAP hbm;
    int cchTextMax;
    int fmt;
    LPARAM lParam;
    int iImage;
    int iOrder;
} HDITEMA, *LPHDITEMA;
#define LVCF_TEXT 0x0004
#define LVM_FIRST 0x1000
#define LVM_INSERTCOLUMNA (LVM_FIRST + 27)
#define LVM_GETITEMA (LVM_FIRST + 5)
#define LVM_INSERTITEMA (LVM_FIRST + 7)
#define LVM_SETIMAGELIST (LVM_FIRST + 3)
#define LVM_DELETEALLITEMS (LVM_FIRST + 9)
#define LVM_GETITEMCOUNT (LVM_FIRST + 4)
#define LVM_GETSELECTEDCOUNT (LVM_FIRST + 50)
/* Start typing over a row's label, and reach the box while it is up. The view
 * says LVN_BEGINLABELEDIT when it starts and LVN_ENDLABELEDIT when it is
 * done; the app answers that one with zero to keep the old name. */
#define LVM_EDITLABELA (LVM_FIRST + 23)
#define LVM_GETEDITCONTROL (LVM_FIRST + 24)
#define LVM_GETNEXTITEM (LVM_FIRST + 12)
#define LVM_SETCOLUMNA (LVM_FIRST + 26)
#define LVM_GETHEADER (LVM_FIRST + 31)
#define LVM_GETCOLUMNWIDTH (LVM_FIRST + 29)
/* Take a column out. Its number is its place, so the ones past it shift down
 * — which is what lets a program put a different set in by emptying first. */
#define LVM_DELETECOLUMN (LVM_FIRST + 28)
#define LVM_SETCOLUMNWIDTH (LVM_FIRST + 30)
/* Widths a column can be asked for instead of a number: fit what is in it,
 * or fit that and the heading too. */
#define LVSCW_AUTOSIZE (-1)
#define LVSCW_AUTOSIZE_USEHEADER (-2)
#define LVM_ENSUREVISIBLE (LVM_FIRST + 19)
#define LVNI_SELECTED 0x0002
#define LVNI_FOCUSED 0x0001
#define LVM_HITTEST (LVM_FIRST + 18)
/* Where an item is, in client coordinates. The code goes in on rect.left:
 * the whole row, or just the label — which is the box a click has to land in
 * for the row to be picked. An application also uses it to tell the header
 * from the rows, since the first row's top is where the header ends. */
#define LVM_GETITEMRECT (LVM_FIRST + 14)
#define LVIR_BOUNDS 0
#define LVIR_ICON 1
#define LVIR_LABEL 2
#define LVIR_SELECTBOUNDS 3
#define LVHT_NOWHERE 0x0001
#define LVHT_ONITEMICON 0x0002
#define LVHT_ONITEMLABEL 0x0004
#define LVHT_ONITEMSTATEICON 0x0008
#define LVHT_ONITEM (LVHT_ONITEMICON | LVHT_ONITEMLABEL | LVHT_ONITEMSTATEICON)
typedef struct tagLVHITTESTINFO {
    POINT pt;
    UINT flags;
    int iItem;
    int iSubItem;
} LVHITTESTINFO;
#define LVM_GETITEMTEXTA (LVM_FIRST + 45)
#define LVM_SETITEMTEXTA (LVM_FIRST + 46)
#define LVM_SETITEMSTATE (LVM_FIRST + 43)
#define LVM_GETITEMSTATE (LVM_FIRST + 44)
/* Extras a list view can be asked for after it is made. */
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 54)
#define LVM_GETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 55)
#define LVS_EX_CHECKBOXES 0x00000004
#define LVS_EX_FULLROWSELECT 0x00000020

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
/* Whether a row's box is ticked, and ticking it. A list view keeps this as
 * the row's state picture, which is why it reads so roundaboutly. */
#define ListView_GetCheckState(w, i)                                           \
    ((int)((((UINT)SendMessageA((w), LVM_GETITEMSTATE, (WPARAM)(i),            \
                                LVIS_STATEIMAGEMASK)) >>                           \
            12)) -                                                             \
     1)
#define ListView_SetCheckState(w, i, on)                                       \
    ListView_SetItemState(w, i, INDEXTOSTATEIMAGEMASK((on) ? 2 : 1),           \
                          LVIS_STATEIMAGEMASK)

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
#define TCN_SELCHANGING (0U - 552U)

/* ---- property sheets (comctl32) -------------------------------------------
 *
 * A dialog with a row of tabs across it, one page behind each. Each page is
 * an ordinary dialog of its own, made from its own template with its own
 * procedure; the sheet puts them all in the same place, one at a time, and
 * owns the OK, Cancel and Apply along the bottom.
 *
 * A page hears PSN_SETACTIVE when it comes to the front, PSN_KILLACTIVE
 * before it goes to the back, PSN_APPLY when what it holds is to be kept and
 * PSN_RESET when it is to be thrown away. It says it has something worth
 * keeping with PropSheet_Changed, which is what lights the Apply button. */

/* What a page's fields mean. */
#define PSP_DEFAULT 0x0000
#define PSP_DLGINDIRECT 0x0001 /* pResource is the template itself */
#define PSP_USEHICON 0x0002
#define PSP_USEICONID 0x0004
#define PSP_USETITLE 0x0008 /* pszTitle is the tab's label */
#define PSP_HASHELP 0x0020
#define PSP_PREMATURE 0x0400

/* and the sheet's. */
#define PSH_DEFAULT 0x00000000
#define PSH_PROPTITLE 0x00000001 /* the caption reads "<title> Properties" */
#define PSH_USEHICON 0x00000002
#define PSH_USEICONID 0x00000004
#define PSH_PROPSHEETPAGE 0x00000008 /* ppsp is an array, not handles */
#define PSH_USEPSTARTPAGE 0x00000040
#define PSH_NOAPPLYNOW 0x00000080 /* no Apply button at all */
#define PSH_USECALLBACK 0x00000100
#define PSH_HASHELP 0x00000200
#define PSH_MODELESS 0x00000400
#define PSH_NOCONTEXTHELP 0x02000000

typedef struct _PSP *HPROPSHEETPAGE;

typedef struct tagPROPSHEETPAGEA {
    DWORD dwSize;
    DWORD dwFlags;
    HINSTANCE hInstance;
    /* win32 keeps these in nameless unions with pszTemplate and pszIcon; a
     * program that names the members used here reads the same on both. */
    LPCDLGTEMPLATEA pResource;
    HICON hIcon;
    LPCSTR pszTitle;
    DLGPROC pfnDlgProc;
    LPARAM lParam;
    void *pfnCallback;
    UINT *pcRefParent;
} PROPSHEETPAGEA, *LPPROPSHEETPAGEA;
typedef const PROPSHEETPAGEA *LPCPROPSHEETPAGEA;

typedef struct tagPROPSHEETHEADERA {
    DWORD dwSize;
    DWORD dwFlags;
    HWND hwndParent;
    HINSTANCE hInstance;
    HICON hIcon;
    LPCSTR pszCaption;
    UINT nPages;
    UINT nStartPage;
    LPCPROPSHEETPAGEA ppsp;
    void *pfnCallback;
} PROPSHEETHEADERA, *LPPROPSHEETHEADERA;
typedef const PROPSHEETHEADERA *LPCPROPSHEETHEADERA;

/* What a page is told, and what it says back. */
#define PSN_FIRST (0U - 200U)
#define PSN_SETACTIVE (PSN_FIRST - 0)
#define PSN_KILLACTIVE (PSN_FIRST - 1)
#define PSN_APPLY (PSN_FIRST - 2)
#define PSN_RESET (PSN_FIRST - 3)
#define PSN_HELP (PSN_FIRST - 5)
#define PSN_QUERYCANCEL (PSN_FIRST - 9)

#define PSNRET_NOERROR 0
#define PSNRET_INVALID 1
#define PSNRET_INVALID_NOCHANGEPAGE 2

typedef struct _PSHNOTIFY {
    NMHDR hdr;
    LPARAM lParam;
} PSHNOTIFY, *LPPSHNOTIFY;

/* What a page says to the sheet. */
#define PSM_SETCURSEL (WM_USER + 101)
#define PSM_CHANGED (WM_USER + 104)
#define PSM_CANCELTOCLOSE (WM_USER + 107)
#define PSM_UNCHANGED (WM_USER + 109)
#define PSM_APPLY (WM_USER + 110)
#define PSM_GETTABCONTROL (WM_USER + 116)
#define PSM_GETCURRENTPAGEHWND (WM_USER + 118)

/* A page tells the sheet it now holds something worth keeping, which is what
 * lights Apply; and that it does not, which puts it out again. */
#define PropSheet_Changed(sheet, page)                                             SendMessageA(sheet, PSM_CHANGED, (WPARAM)(page), 0)
#define PropSheet_UnChanged(sheet, page)                                           SendMessageA(sheet, PSM_UNCHANGED, (WPARAM)(page), 0)
#define PropSheet_Apply(sheet) SendMessageA(sheet, PSM_APPLY, 0, 0)
#define PropSheet_SetCurSel(sheet, page, i)                                        SendMessageA(sheet, PSM_SETCURSEL, (WPARAM)(i), (LPARAM)(page))
#define PropSheet_GetTabControl(sheet)                                             ((HWND)(INT_PTR)SendMessageA(sheet, PSM_GETTABCONTROL, 0, 0))
#define PropSheet_GetCurrentPageHwnd(sheet)                                        ((HWND)(INT_PTR)SendMessageA(sheet, PSM_GETCURRENTPAGEHWND, 0, 0))

/* The Apply button's id, which is comctl32's own. */
#define IDD_APPLYNOW 0x3021

/* Put the sheet up and do not return until it is answered: >0 if OK or Apply
 * was pressed, 0 if it was cancelled, -1 if it could not be made. */
INT_PTR PropertySheetA(LPCPROPSHEETHEADERA header);
#define TVN_SELCHANGEDA (0U - 402U)
/* ITEMEXPANDING comes before the item opens, which is where a tree that
 * fills lazily puts the children; ITEMEXPANDED comes after. */
#define TVN_ITEMEXPANDINGA (0U - 405U)
#define TVN_ITEMEXPANDEDA (0U - 406U)

typedef struct tagNMTREEVIEWA {
    NMHDR hdr;
    UINT action; /* TVE_EXPAND or TVE_COLLAPSE */
    TVITEMA itemOld, itemNew;
    POINT ptDrag;
} NMTREEVIEWA;
/* Sent by any common control that a double click landed on. A shell opens
 * what was double-clicked on this. */
/* A press on an item and a double press on one, which a view sends its
 * parent so a program can decide what either means. Enter over a view says
 * the same thing as the double press: a shell opens what is picked. */
#define NM_CLICK (0U - 2U)
#define NM_DBLCLK (0U - 3U)
#define NM_RETURN (0U - 4U)
#define LVN_ITEMCHANGED (0U - 101U)
#define LVN_COLUMNCLICK (0U - 108U)
/* A row's label is being typed over, and has been. The app answers the end
 * with non-zero to take the new name and zero to keep the old one; pszText is
 * NULL when the edit was abandoned. */
#define LVN_BEGINLABELEDITA (0U - 105U)
#define LVN_ENDLABELEDITA (0U - 106U)

/* What both carry: the row, and the text it now has. */
typedef struct tagNMLVDISPINFOA {
    NMHDR hdr;
    LVITEMA item;
} NMLVDISPINFOA;

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
/* An icon in a part, drawn before its text. */
#define SB_SETICON (WM_USER + 15)
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

/* Which bar a scroll call is about: a window's own, or the control itself. */
#define SB_HORZ 0
#define SB_VERT 1
#define SB_CTL 2
#define SB_BOTH 3

/* What of a scroll bar's state is being set or asked for. */
#define SIF_RANGE 0x0001
#define SIF_PAGE 0x0002
#define SIF_POS 0x0004
#define SIF_DISABLENOSCROLL 0x0008
#define SIF_TRACKPOS 0x0010
#define SIF_ALL (SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS)

/* A scroll bar's whole state. The thumb's size comes from nPage against the
 * range, which is why a bar cannot be driven by position alone. */
typedef struct tagSCROLLINFO {
    UINT cbSize;
    UINT fMask;
    int nMin;
    int nMax;
    UINT nPage;
    int nPos;
    int nTrackPos;
} SCROLLINFO, *LPSCROLLINFO;

/* Set or read what a scroll bar shows. `bar` is SB_CTL for a SCROLLBAR
 * control, which is the one ween32 has. Returns the position it settled on. */
int SetScrollInfo(HWND wnd, int bar, const SCROLLINFO *si, BOOL redraw);
BOOL GetScrollInfo(HWND wnd, int bar, SCROLLINFO *si);

/* SCROLLBAR styles */
#define SBS_HORZ 0x0000L
#define SBS_VERT 0x0001L
/* A scroll bar can be a corner instead: the hatched square a window is
 * resized by, which is what a status bar's right end and a list that can be
 * dragged bigger both put there. */
#define SBS_SIZEBOX 0x0008L
#define SBS_SIZEGRIP 0x0010L

/* dialog styles */
#define DS_3DLOOK 0x0004L
#define DS_SETFONT 0x40L
#define DS_MODALFRAME 0x80L
#define DS_CENTER 0x0800L
#define DS_CONTEXTHELP 0x2000L

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
/* The low five bits of a static's style are a type, not a set of flags: a
 * label, a rectangle of one of the system colours, a picture, or a line. */
#define SS_ICON 0x00000003L
#define SS_BLACKRECT 0x00000004L
#define SS_GRAYRECT 0x00000005L
#define SS_WHITERECT 0x00000006L
#define SS_BLACKFRAME 0x00000007L
#define SS_GRAYFRAME 0x00000008L
#define SS_WHITEFRAME 0x00000009L
/* The two that stay on one line; every other kind wraps at its own width. */
#define SS_SIMPLE 0x0000000BL
#define SS_LEFTNOWORDWRAP 0x0000000CL
#define SS_OWNERDRAW 0x0000000DL
#define SS_BITMAP 0x0000000EL
/* A rule across the dialog: two pixels, shadow over white. What a dialog puts
 * between what it asks and the buttons that answer. */
#define SS_ETCHEDHORZ 0x00000010L
#define SS_ETCHEDVERT 0x00000011L
#define SS_ETCHEDFRAME 0x00000012L
#define SS_TYPEMASK 0x0000001FL
#define SS_NOPREFIX 0x00000080L
#define SS_CENTERIMAGE 0x00000200L
#define SS_SUNKEN 0x00001000L

/* One picture, hung on the control: an icon with STM_SETICON, a bitmap with
 * STM_SETIMAGE, and IMAGE_BITMAP says which kind wParam names. */
#define STM_SETICON 0x0170
#define STM_GETICON 0x0171
#define STM_SETIMAGE 0x0172
#define STM_GETIMAGE 0x0173

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
#define ODT_MENU 1
#define ODT_LISTBOX 2
#define ODT_COMBOBOX 3
#define ODT_BUTTON 4
#define ODT_STATIC 5
#define ODA_DRAWENTIRE 0x0001
#define ODA_SELECT 0x0002
#define ODA_FOCUS 0x0004
#define ODS_SELECTED 0x0001
#define ODS_DISABLED 0x0004
#define ODS_FOCUS 0x0010
#define ODS_COMBOBOXEDIT 0x1000

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
#define HTHELP 21
#define HTMINBUTTON 8
#define HTMAXBUTTON 9
/* what a caption button asks the window to do */
#define SC_SIZE 0xF000
/* Which edge or corner a resize is being made from, added to SC_SIZE. */
#define WMSZ_LEFT 1
#define WMSZ_RIGHT 2
#define WMSZ_TOP 3
#define WMSZ_BOTTOM 6
#define WMSZ_BOTTOMRIGHT 8
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_CLOSE 0xF060
#define SC_CONTEXTHELP 0xF180
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
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12 /* Alt */
#define VK_PRIOR 0x21 /* Page Up */
#define VK_NEXT 0x22  /* Page Down */
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
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
#define COLOR_WINDOWFRAME 6
/* The grey a document window's spare space is filled with -- the surround a
 * picture smaller than its window sits on. */
#define COLOR_APPWORKSPACE 12
#define COLOR_WINDOW 5
#define COLOR_MENU 4
#define COLOR_MENUTEXT 7
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
#define DFCS_CAPTIONHELP 0x0004
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
#define DT_WORDBREAK 0x00000010
#define DT_SINGLELINE 0x00000020
#define DT_CALCRECT 0x00000400
#define DT_NOCLIP 0x00000100
#define DT_NOPREFIX 0x00000800
#define DT_END_ELLIPSIS 0x00008000
/* Draw the label without the '&' and without the underline under the letter
 * after it. Windows hides those until the keyboard has been used, and asks
 * for them this way when they are hidden. */
#define DT_HIDEPREFIX 0x00100000

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

/* ---- a window's own scroll bars -------------------------------------------
 *
 * WS_HSCROLL and WS_VSCROLL put a bar in the window's non-client area, below
 * and to the right of the client rectangle — which is why GetClientRect
 * shrinks when they appear. The window hears WM_HSCROLL/WM_VSCROLL with a
 * null lParam (a SCROLLBAR *control* puts its own handle there), and moves
 * its contents itself.
 */
/* SB_HORZ, SB_VERT, SB_CTL and the SIF_* mask, with SCROLLINFO itself, are
 * declared with the scroll bar control above; these are what a window's own
 * bars add to them. */
#define ESB_ENABLE_BOTH 0x0000
#define ESB_DISABLE_BOTH 0x0003

int SetScrollPos(HWND wnd, int bar, int pos, BOOL redraw);
int GetScrollPos(HWND wnd, int bar);
BOOL SetScrollRange(HWND wnd, int bar, int min, int max, BOOL redraw);
BOOL GetScrollRange(HWND wnd, int bar, int *min, int *max);
BOOL ShowScrollBar(HWND wnd, int bar, BOOL show);
BOOL EnableScrollBar(HWND wnd, UINT bar, UINT flags);

/* ---- pens, bitmaps and raster operations ----------------------------------
 *
 * The half of GDI a drawing program is made of: a memory device context with
 * a bitmap selected into it, a pen to draw lines with, and BitBlt to move
 * pixels between the two. The raster operation codes are the real ones, and
 * the general case is implemented rather than a handful of special cases --
 * the high byte of a ROP3 is the truth table of (pattern, source, dest), and
 * that is exactly how it is evaluated.
 */

#define NULL_BRUSH 5
#define HOLLOW_BRUSH NULL_BRUSH
#define WHITE_PEN 6
#define BLACK_PEN 7
#define NULL_PEN 8

/* CreatePen styles. A cosmetic pen wider than one unit draws solid whatever
 * style it was given, as it does in GDI. */
#define PS_SOLID 0
#define PS_DASH 1
#define PS_DOT 2
#define PS_DASHDOT 3
#define PS_DASHDOTDOT 4
#define PS_NULL 5
#define PS_INSIDEFRAME 6

/* SetROP2: how a pen or a brush combines with what is already there. */
#define R2_BLACK 1
#define R2_NOTMERGEPEN 2
#define R2_MASKNOTPEN 3
#define R2_NOTCOPYPEN 4
#define R2_MASKPENNOT 5
#define R2_NOT 6
#define R2_XORPEN 7
#define R2_NOTMASKPEN 8
#define R2_MASKPEN 9
#define R2_NOTXORPEN 10
#define R2_NOP 11
#define R2_MERGENOTPEN 12
#define R2_COPYPEN 13
#define R2_MERGEPENNOT 14
#define R2_MERGEPEN 15
#define R2_WHITE 16

/* Ternary raster operations, as BitBlt takes them. */
#define BLACKNESS 0x00000042
#define NOTSRCERASE 0x001100A6
#define NOTSRCCOPY 0x00330008
#define SRCERASE 0x00440328
#define DSTINVERT 0x00550009
#define PATINVERT 0x005A0049
#define SRCINVERT 0x00660046
#define SRCAND 0x008800C6
#define MERGEPAINT 0x00BB0226
#define MERGECOPY 0x00C000CA
#define SRCCOPY 0x00CC0020
#define SRCPAINT 0x00EE0086
#define PATCOPY 0x00F00021
#define PATPAINT 0x00FB0A09
#define WHITENESS 0x00FF0062

/* SetStretchBltMode. Only the two a paint program uses are distinguished:
 * dropping pixels (which is what shrinking a picture in Paint does) and the
 * colour-preserving halftone. */
#define BLACKONWHITE 1
#define WHITEONBLACK 2
#define COLORONCOLOR 3
#define HALFTONE 4
#define STRETCH_ANDSCANS BLACKONWHITE
#define STRETCH_ORSCANS WHITEONBLACK
#define STRETCH_DELETESCANS COLORONCOLOR
#define STRETCH_HALFTONE HALFTONE

/* ExtFloodFill */
#define FLOODFILLBORDER 0
#define FLOODFILLSURFACE 1

/* GetObject */
#define OBJ_PEN 1
#define OBJ_BRUSH 2
#define OBJ_DC 3
#define OBJ_BITMAP 7
#define OBJ_FONT 6

typedef struct ween_gdiobj *HPEN;

typedef struct tagBITMAP {
    LONG bmType;
    LONG bmWidth;
    LONG bmHeight;
    LONG bmWidthBytes;
    WORD bmPlanes;
    WORD bmBitsPixel;
    LPVOID bmBits;
} BITMAP, *PBITMAP;

typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO;

#define BI_RGB 0
#define DIB_RGB_COLORS 0

/* A device context whose surface is a bitmap rather than a window: this is
 * where an application keeps the picture it is editing. A fresh one has a
 * 1x1 bitmap in it, as GDI's does, so nothing can be drawn until one is
 * selected. */
HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HBITMAP CreateCompatibleBitmap(HDC dc, int w, int h);
int GetObjectA(HGDIOBJ obj, int size, LPVOID out);

HPEN CreatePen(int style, int width, COLORREF color);
int SetROP2(HDC dc, int mode);
int GetROP2(HDC dc);
COLORREF SetBkColor(HDC dc, COLORREF color);
COLORREF GetBkColor(HDC dc);
int SetStretchBltMode(HDC dc, int mode);

/* Where the caller's (0,0) lands in the window: a scrolling view sets this
 * and then draws in the coordinates of what it is showing. */
BOOL SetViewportOrgEx(HDC dc, int x, int y, POINT *prev);
BOOL GetViewportOrgEx(HDC dc, POINT *pt);
BOOL MoveToEx(HDC dc, int x, int y, POINT *prev);
BOOL LineTo(HDC dc, int x, int y);
BOOL Polyline(HDC dc, const POINT *pts, int count);
BOOL PolyBezier(HDC dc, const POINT *pts, DWORD count);
BOOL Polygon(HDC dc, const POINT *pts, int count);
BOOL Rectangle(HDC dc, int left, int top, int right, int bottom);
BOOL Ellipse(HDC dc, int left, int top, int right, int bottom);
BOOL RoundRect(HDC dc, int left, int top, int right, int bottom, int ew, int eh);
COLORREF SetPixel(HDC dc, int x, int y, COLORREF color);
COLORREF GetPixel(HDC dc, int x, int y);
BOOL ExtFloodFill(HDC dc, int x, int y, COLORREF color, UINT type);

BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy,
            DWORD rop);
BOOL StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy,
                int sw, int sh, DWORD rop);
BOOL PatBlt(HDC dc, int x, int y, int w, int h, DWORD rop);
BOOL InvertRect(HDC dc, const RECT *rect);
BOOL DrawFocusRect(HDC dc, const RECT *rect);
/* What a clipping call made of the region it was given. */
#define ERROR 0
#define NULLREGION 1
#define SIMPLEREGION 2
#define COMPLEXREGION 3
int IntersectClipRect(HDC dc, int left, int top, int right, int bottom);

/* The pixels of a bitmap, as a device-independent bitmap: how a program that
 * has to write a .bmp file, or work on the picture a pixel at a time, gets
 * at them. 24 and 32 bits per pixel, bottom-up as a DIB is. */
int GetDIBits(HDC dc, HBITMAP bmp, UINT start, UINT lines, LPVOID bits,
              LPBITMAPINFO info, UINT usage);
int SetDIBits(HDC dc, HBITMAP bmp, UINT start, UINT lines, const void *bits,
              const BITMAPINFO *info, UINT usage);

/* ---- the common dialogs ---------------------------------------------------
 *
 * The dialogs that belong to the system rather than to the application: the
 * one that asks for a file name and the one that asks for a colour. Every
 * program gets the same two, which is the point of them — an application
 * calls GetOpenFileNameA and takes what comes back in its buffer, and what
 * it looks like is the system's business.
 */

typedef struct tagOFNA {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    LPCSTR lpstrFilter;
    LPSTR lpstrCustomFilter;
    DWORD nMaxCustFilter;
    DWORD nFilterIndex;
    LPSTR lpstrFile;
    DWORD nMaxFile;
    LPSTR lpstrFileTitle;
    DWORD nMaxFileTitle;
    LPCSTR lpstrInitialDir;
    LPCSTR lpstrTitle;
    DWORD Flags;
    WORD nFileOffset;
    WORD nFileExtension;
    LPCSTR lpstrDefExt;
    LPARAM lCustData;
    void *lpfnHook;
    LPCSTR lpTemplateName;
} OPENFILENAMEA, *LPOPENFILENAMEA;

#define OFN_READONLY 0x00000001
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_HIDEREADONLY 0x00000004
#define OFN_PATHMUSTEXIST 0x00000800
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_CREATEPROMPT 0x00002000
#define OFN_EXPLORER 0x00080000

BOOL GetOpenFileNameA(OPENFILENAMEA *ofn);
BOOL GetSaveFileNameA(OPENFILENAMEA *ofn);

typedef struct tagCHOOSECOLORA {
    DWORD lStructSize;
    HWND hwndOwner;
    HWND hInstance;
    COLORREF rgbResult;
    COLORREF *lpCustColors;
    DWORD Flags;
    LPARAM lCustData;
    /* The hook an application hands over with CC_ENABLEHOOK: it is offered
     * every message before the dialog sees it. */
    INT_PTR(CALLBACK *lpfnHook)(HWND, UINT, WPARAM, LPARAM);
    LPCSTR lpTemplateName;
} CHOOSECOLORA, *LPCHOOSECOLORA;

#define CC_RGBINIT 0x00000001
#define CC_FULLOPEN 0x00000002
#define CC_PREVENTFULLOPEN 0x00000004
#define CC_ANYCOLOR 0x00000100
/* An application that wants a word in the dialog -- its own title, say --
 * hands over a procedure that is offered every message first. */
#define CC_ENABLEHOOK 0x00000010

BOOL ChooseColorA(CHOOSECOLORA *cc);

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
/* The same styles by their later names, and the one that says the whole
 * button is the drop-down rather than an arrow half beside it — which is what
 * a menu title is. */
#define BTNS_BUTTON 0x0000
#define BTNS_SEP 0x0001
#define BTNS_CHECK 0x0002
#define BTNS_GROUP 0x0004
#define BTNS_DROPDOWN 0x0008
#define BTNS_AUTOSIZE 0x0010
#define BTNS_NOPREFIX 0x0020
#define BTNS_SHOWTEXT 0x0040
#define BTNS_WHOLEDROPDOWN 0x0080
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
#define TB_ISBUTTONCHECKED (WM_USER + 10)
#define TB_ISBUTTONENABLED (WM_USER + 9)
#define TB_ADDBUTTONSA (WM_USER + 20)
#define TB_BUTTONSTRUCTSIZE (WM_USER + 30)
/* One button's own size, which is how a button that does not follow from
 * its label and its image gets the width it wants. */
#define TB_SETBUTTONINFOA (WM_USER + 66)
#define TBIF_IMAGE 0x0001
#define TBIF_STYLE 0x0008
#define TBIF_SIZE 0x0040
/* the button is named by its place in the bar, not by its command — which is
 * the only way to reach a separator, since separators carry no command */
#define TBIF_BYINDEX 0x80000000
typedef struct tagTBBUTTONINFOA {
    UINT cbSize;
    DWORD dwMask;
    int idCommand;
    int iImage;
    BYTE fsState;
    BYTE fsStyle;
    WORD cx;
    DWORD_PTR lParam;
    LPSTR pszText;
    int cchText;
} TBBUTTONINFOA;

/* How big a button is and what surrounds its content. A toolbar works both
 * out from the label and the image it is given; an application that wants
 * them otherwise — a menu in a rebar band spaces its titles by sixteen —
 * says so. */
#define TB_SETBUTTONSIZE (WM_USER + 31)
/* How far in the first button starts. */
#define TB_SETINDENT (WM_USER + 47)
/* How big the images are. A bar with none says so — zero by zero — or a
 * button with no image still reserves room for one. */
#define TB_SETBITMAPSIZE (WM_USER + 32)
#define TB_SETPADDING (WM_USER + 87)
#define TB_GETPADDING (WM_USER + 86)
/* Which button the keyboard is on, and the letters that reach one: a menu in
 * a band is walked with these. */
#define TB_SETHOTITEM (WM_USER + 72)
#define TB_GETHOTITEM (WM_USER + 71)
#define TB_MAPACCELERATORA (WM_USER + 78)
/* Extended styles. A drop-down button shows the arrow half only when the
 * toolbar has been told to draw them; without it the whole button is the
 * drop-down, which is what a menu title is. */
#define TB_SETEXTENDEDSTYLE (WM_USER + 84)
#define TB_GETEXTENDEDSTYLE (WM_USER + 85)
#define TBSTYLE_EX_DRAWDDARROWS 0x00000001
#define I_IMAGENONE (-2)
#define TB_SETIMAGELIST (WM_USER + 48)
/* The second set of images, used for whichever button the pointer is on.
 * Windows 2000 drew a toolbar's arrows grey and swapped to coloured ones
 * under the pointer, which is how the Back arrow comes to be blue. */
#define TB_SETHOTIMAGELIST (WM_USER + 52)
#define TB_GETITEMRECT (WM_USER + 29)
/* Which place in the bar a command's button is in — what a rectangle has to
 * be asked for by. */
#define TB_COMMANDTOINDEX (WM_USER + 25)
#define TB_BUTTONCOUNT (WM_USER + 24)
#define TB_AUTOSIZE (WM_USER + 33)

/* The arrow beside a drop-down button was pressed: show the menu. */
#define TBN_DROPDOWN (0U - 710U)
typedef struct {
    NMHDR hdr;
    int iItem;
} NMTOOLBAR;

/* ---- rebar ----------------------------------------------------------------
 *
 * The bands a shell's toolbars sit in: each is a row with a gripper at its
 * left, an optional label, and one control filling the rest. An etched line
 * runs above every band, which is what separates them.
 *
 * Bands are stacked, one per row. A real rebar can put two side by side and
 * let them be dragged; this does the arrangement a shell actually uses. */
#define REBARCLASSNAMEA "ReBarWindow32"
/* A rebar rules its bands off from each other and from what is above and
 * below. A shell's does; a bare one does not, and then the bands sit flush. */
#define RBS_TOOLTIPS 0x0100
#define RBS_VARHEIGHT 0x0200
#define RBS_BANDBORDERS 0x0400
#define RBS_FIXEDORDER 0x0800

#define RBBIM_STYLE 0x00000001
#define RBBIM_TEXT 0x00000004
#define RBBIM_CHILD 0x00000010
#define RBBIM_CHILDSIZE 0x00000020
#define RBBIM_SIZE 0x00000040

/* Where a control bar puts itself. A toolbar, a status bar and a rebar are
 * all "control bars": left alone they align themselves to the top of their
 * parent and take its whole width, which is what CCS_TOP means and is the
 * default. A toolbar inside a rebar band must say otherwise, or it climbs
 * out of the band and sits over whatever is at the top. */
#define CCS_TOP 0x00000001L
#define CCS_NOMOVEY 0x00000002L
#define CCS_BOTTOM 0x00000003L
#define CCS_NORESIZE 0x00000004L
#define CCS_NOPARENTALIGN 0x00000008L
#define CCS_ADJUSTABLE 0x00000020L
#define CCS_NODIVIDER 0x00000040L
#define CCS_VERT 0x00000080L

/* A band that starts a row of its own rather than sitting beside the one
 * before it. A shell sets this on each of its bars, which is why they stack. */
#define RBBS_BREAK 0x00000001
#define RBBS_FIXEDSIZE 0x00000002
#define RBBS_HIDDEN 0x00000008
#define RBBS_GRIPPERALWAYS 0x00000080
#define RBBS_NOGRIPPER 0x00000100

typedef struct {
    UINT cbSize;
    UINT fMask;
    UINT fStyle;
    COLORREF clrFore;
    COLORREF clrBack;
    LPSTR lpText;
    UINT cch;
    int iImage;
    HWND hwndChild;
    UINT cxMinChild;
    UINT cyMinChild;
    UINT cx;
} REBARBANDINFOA;

#define RB_INSERTBANDA (WM_USER + 1)
#define RB_SETBANDINFOA (WM_USER + 6)
#define RB_GETBANDCOUNT (WM_USER + 12)
#define RB_GETBARHEIGHT (WM_USER + 27)
/* Show or hide one band, which is what View > Toolbars does to each of them. */
#define RB_SHOWBAND (WM_USER + 35)

/* Registering the common control classes. ween32 has them registered before
 * anything can ask for one, so this says yes and does nothing — but an
 * application must still call it, because on Windows it is what puts the
 * classes there. */
typedef struct {
    DWORD dwSize;
    DWORD dwICC;
} INITCOMMONCONTROLSEX, *LPINITCOMMONCONTROLSEX;
#define ICC_LISTVIEW_CLASSES 0x00000001
#define ICC_TREEVIEW_CLASSES 0x00000002
#define ICC_BAR_CLASSES 0x00000004
#define ICC_TAB_CLASSES 0x00000008
#define ICC_UPDOWN_CLASS 0x00000010
#define ICC_PROGRESS_CLASS 0x00000020
#define ICC_HOTKEY_CLASS 0x00000040
#define ICC_ANIMATE_CLASS 0x00000080
#define ICC_WIN95_CLASSES 0x000000FF
#define ICC_DATE_CLASSES 0x00000100
#define ICC_USEREX_CLASSES 0x00000200
#define ICC_COOL_CLASSES 0x00000400
#define ICC_INTERNET_CLASSES 0x00000800
#define ICC_PAGESCROLLER_CLASS 0x00001000
#define ICC_NATIVEFNTCTL_CLASS 0x00002000
#define ICC_STANDARD_CLASSES 0x00004000
#define ICC_LINK_CLASS 0x00008000
BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX *icc);
void InitCommonControls(void);

#define WEEN32_HAS_REBAR 1
#define WEEN32_HAS_TOOLBAR 1
#define WEEN32_HAS_MENU 1
#define WEEN32_HAS_MESSAGEBOX 1
#define WEEN32_HAS_DIALOGBOX 1
#define WEEN32_HAS_ACCELERATORS 1
#define WEEN32_HAS_IMAGELIST 1
#define WEEN32_HAS_CLIPBOARD 1
/* A class cursor, SetCursor and WM_SETCURSOR — what a splitter needs to show
 * the shape that says it can be dragged. */
#define WEEN32_HAS_CURSORS 1

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
/* The same, said in parts: a window that wants to change size without moving
 * — which is a dialog widening itself — cannot say so with MoveWindow
 * without first asking where it is, and where it is is the window manager's
 * answer rather than the position it asked for. */
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
BOOL SetWindowPos(HWND wnd, HWND after, int x, int y, int cx, int cy,
                  UINT flags);
BOOL InvalidateRect(HWND wnd, const RECT *rect, BOOL erase);
BOOL UpdateWindow(HWND wnd);
HWND GetDlgItem(HWND dlg, int id);
/* A control's text by its id, which is how a dialog reads what was typed. */
UINT GetDlgItemTextA(HWND dlg, int id, LPSTR out, int max);
BOOL SetDlgItemTextA(HWND dlg, int id, LPCSTR text);
int GetDlgCtrlID(HWND wnd);
HWND SetFocus(HWND wnd);
HWND GetFocus(void);

/* A window's style, read and written after it was made — which is how a list
 * view is told to show its folder a different way. GWL_STYLE is the one index
 * that matters here; a control that cares about the change hears WM_STYLECHANGED. */
/* -4 is the window's own procedure: reading it and putting another in its
 * place is subclassing, which is how a control that puts another inside
 * itself takes the messages it needs before the inner one sees them. What
 * came back is called for the rest. */
#define GWL_WNDPROC (-4)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_ID (-12)
/* A program's own value, hung off a window: what a class with no globals uses
 * to find its instance data from its procedure. */
#define GWL_USERDATA (-21)
#define GWLP_USERDATA (-21)
/* A dialog's procedure returns only whether it dealt with a message; what the
 * message *answers* goes here, which is how a page in a property sheet says
 * yes or no to being taken off the front. */
#define DWL_MSGRESULT 0
#define DWLP_MSGRESULT 0
/* The dialog's own extra bytes come after that answer and the procedure
 * pointer, so where the program's slot begins depends on how wide a pointer
 * is — which is how win32 spells it too. */
#define DWLP_DLGPROC (DWLP_MSGRESULT + (int)sizeof(LRESULT))
#define DWLP_USER (DWLP_DLGPROC + (int)sizeof(DLGPROC))
#define DWL_USER DWLP_USER
#define DWL_DLGPROC DWLP_DLGPROC
LONG GetWindowLongA(HWND wnd, int index);
LONG SetWindowLongA(HWND wnd, int index, LONG value);
/* The same two where the value is a pointer — a window procedure does not fit
 * in a LONG on a 64-bit build, so subclassing goes through these. */
LONG_PTR GetWindowLongPtrA(HWND wnd, int index);
LONG_PTR SetWindowLongPtrA(HWND wnd, int index, LONG_PTR value);
#define GWLP_WNDPROC (-4)
/* Call a procedure that was subclassed away, which is the other half of it. */
LRESULT CallWindowProcA(WNDPROC proc, HWND wnd, UINT msg, WPARAM wp,
                        LPARAM lp);
#define WM_STYLECHANGED 0x007D

/* The string calls win32 has of its own. Only the ones an application reaches
 * for are here: a case-insensitive compare is what orders names in a shell,
 * and doing it by hand gets the ASCII range right and nothing else. */
int lstrcmpA(LPCSTR a, LPCSTR b);
int lstrcmpiA(LPCSTR a, LPCSTR b);
int lstrlenA(LPCSTR s);
BOOL EnableWindow(HWND wnd, BOOL enable);
BOOL IsWindowEnabled(HWND wnd);
/* Whether a window is shown, which a program asks about one it put up. */
BOOL IsWindowVisible(HWND wnd);
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
#define MF_BYPOSITION 0x0400
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
int GetMenuItemCount(HMENU menu);
/* A menu whose items change while the program runs: the four files it was
 * last asked to open, say. InsertMenuA puts one ahead of another named by
 * position or by command; DeleteMenu takes one out, and any submenu with
 * it. */
BOOL InsertMenuA(HMENU menu, UINT before, UINT flags, UINT_PTR id, LPCSTR text);
BOOL DeleteMenu(HMENU menu, UINT item, UINT flags);
/* MF_BYPOSITION only: an application walking a menu bar to draw it itself —
 * which is what hosting one in a rebar band comes to — asks by position. */
int GetMenuStringA(HMENU menu, UINT item, LPSTR out, int max, UINT flags);
/* A picture in an item's gutter. Windows takes two — the one for the item
 * unchecked and the one for it checked — and draws them where the check mark
 * would go; the shell's "Send To" is a menu built this way. */
BOOL SetMenuItemBitmaps(HMENU menu, UINT item, UINT flags, HBITMAP unchecked,
                        HBITMAP checked);
DWORD CheckMenuItem(HMENU menu, UINT id, UINT check);
/* Which one of a set of items a menu is on: the range is marked as a set and
 * the one named is ticked with a dot rather than a check mark. */
BOOL CheckMenuRadioItem(HMENU menu, UINT first, UINT last, UINT check,
                        UINT flags);
BOOL EnableMenuItem(HMENU menu, UINT id, UINT enable);
/* An item's text, changed after the fact: what a command that names what it
 * would undo needs. */
BOOL ModifyMenuA(HMENU menu, UINT item, UINT flags, UINT_PTR id, LPCSTR text);
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
/* The whole command line as one string, program name first, the way win32
 * hands it over -- so an application takes its arguments the same way here
 * and there. */
LPSTR GetCommandLineA(void);

int GetSystemMetrics(int index);

/* How much memory the machine has, which is the one thing an About box says
 * that is about the machine rather than the program. */
typedef struct {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    DWORD dwTotalPhys;
    DWORD dwAvailPhys;
    DWORD dwTotalPageFile;
    DWORD dwAvailPageFile;
    DWORD dwTotalVirtual;
    DWORD dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;
void GlobalMemoryStatus(LPMEMORYSTATUS status);
BOOL GetWindowRect(HWND wnd, LPRECT rect);
HWND GetParent(HWND wnd);
BOOL ClientToScreen(HWND wnd, POINT *pt);
/* Where the pointer is, in screen coordinates: what a control asks when a
 * message tells it something moved but not where. */
BOOL GetCursorPos(POINT *pt);
/* Which top-level window is active — the one a press with no window of its
 * own is measured against, and the one a dialog is put up over. */
HWND GetActiveWindow(void);
BOOL ScreenToClient(HWND wnd, POINT *pt);
BOOL GetCursorPos(POINT *pt);

/* ---- the clipboard -------------------------------------------------------
 *
 * Open it, empty it, put something in, close it. The data belongs to the
 * clipboard once handed over — do not free it — and what GetClipboardData
 * returns stays valid until the next thing replaces it.
 *
 * WM_CUT/WM_COPY/WM_PASTE are what a control acts on; an EDIT also takes
 * Ctrl+X, Ctrl+C, Ctrl+V and Ctrl+A directly. */
typedef void *HANDLE;

/* ---- files -------------------------------------------------------------
 * The KERNEL32 calls a win32 program reads and writes a file with. A program
 * that uses these rather than the C library's needs no C runtime on Windows,
 * which is the difference between running on a machine of that age and not
 * starting at all. */
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, void *security,
                   DWORD disposition, DWORD flags, HANDLE template_file);
BOOL ReadFile(HANDLE file, void *buf, DWORD to_read, DWORD *read, void *ovl);
BOOL WriteFile(HANDLE file, const void *buf, DWORD to_write, DWORD *written,
               void *ovl);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG *high, DWORD method);
DWORD GetFileSize(HANDLE file, DWORD *high);
BOOL CloseHandle(HANDLE h);
#define CF_TEXT 1
#define CF_BITMAP 2
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
/* An icon from two bitmaps, win32's oldest way of making one: the AND mask
 * says what shows through and the XOR bits are the colours. */
HICON CreateIcon(HINSTANCE inst, int w, int h, BYTE planes, BYTE bpp,
                 const BYTE *and_bits, const BYTE *xor_bits);
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
#define MB_YESNOCANCEL 0x00000003
#define MB_YESNO 0x00000004
/* the buttons a box wants live in the low four bits */
#define MB_TYPEMASK 0x0000000F
/* The picture beside the message, which says what kind of message it is. A
 * box without one is a plain notice; the four here are what win32 draws. */
#define MB_ICONHAND 0x00000010
#define MB_ICONQUESTION 0x00000020
#define MB_ICONEXCLAMATION 0x00000030
#define MB_ICONASTERISK 0x00000040
#define MB_ICONERROR MB_ICONHAND
#define MB_ICONSTOP MB_ICONHAND
#define MB_ICONWARNING MB_ICONEXCLAMATION
#define MB_ICONINFORMATION MB_ICONASTERISK
#define MB_ICONMASK 0x000000f0
#define MB_DEFBUTTON1 0x00000000
#define MB_DEFBUTTON2 0x00000100
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
/* A context for a window outside a paint, which is what measuring text before
 * laying anything out needs. */
HDC GetDC(HWND wnd);
int ReleaseDC(HWND wnd, HDC dc);
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
