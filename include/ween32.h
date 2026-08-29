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
/* The rich edit's own header, so that a program asking for RICHEDIT_CLASS
 * needs no include of its own on either side. On Windows the class comes
 * from riched20.dll, which a program loads with LoadLibrary before it makes
 * one; here the class is registered with the rest and the call is a
 * no-op that still has to be written, so the same source builds. */
#include <richedit.h>
#else

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- fundamental types (LLP64-faithful) ------------------------------- */

typedef int BOOL;
typedef int INT;
/* Memory a handle stands for, which the calls below hand out and take. */
typedef void *HGLOBAL;
typedef void *HLOCAL;
typedef unsigned char BYTE;
typedef BYTE *LPBYTE, *PBYTE;
typedef uint16_t WORD;
typedef int16_t SHORT;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t UINT;
typedef intptr_t LONG_PTR;
typedef uintptr_t UINT_PTR;
typedef UINT_PTR DWORD_PTR;
typedef UINT_PTR ULONG_PTR;
typedef UINT *PUINT;
/* A colour palette. ween32 draws in true colour and has no palettes, but a
 * property sheet's header names one, so the type has to exist to be named. */
typedef struct ween_palette *HPALETTE;
/* A count of bytes, which is as wide as a pointer -- not as wide as a DWORD.
 * MEMORYSTATUS is declared with these, and declaring them DWORD put every
 * field after the second one four bytes short of where win32 has it. */
typedef ULONG_PTR SIZE_T;
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

/* The same two numbers under the name the rich edit's messages use. */
typedef struct _POINTL {
    LONG x;
    LONG y;
} POINTL, *PPOINTL;

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
#define MAKELONG(a, b) ((LONG)(DWORD)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
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
/* Drawing switched off while a program makes a run of changes, and on again
 * after; the control redraws once rather than once a change. */
#define WM_SETREDRAW 0x000B
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
/* What a parent is told about its own children. win32 sends this when a child
 * is created, when one is destroyed, and when one is pressed; only the destroy
 * half is here, because that is the half something depends on — a control that
 * holds a child's HWND has to hear when that window goes, or it is left with a
 * pointer to nothing. The low word of wParam is the event, the high word the
 * child's id, and lParam the child itself. See the ROADMAP for the other two.
 *
 * A child created WS_EX_NOPARENTNOTIFY sends none of it, which is how a
 * control made of many small windows keeps from telling its parent about each
 * of them. */
#define WM_PARENTNOTIFY 0x0210
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
/* A palette that floats over the window it belongs to: a shorter caption, a
 * smaller close box and nothing else in it, and no place in the task bar. */
#define WS_EX_TOOLWINDOW 0x00000080L
/* A window that does not take the keyboard when it appears — which is what a
 * menu is: the window under it keeps its focus, and its caret with it. */
#define WS_EX_NOACTIVATE 0x08000000L
/* A question mark in the caption, before the close box. A window that has it
 * has no minimise or maximise box: the three do not share the strip. */
#define WS_EX_CONTEXTHELP 0x00000400L
#define WS_EX_STATICEDGE 0x00020000L
/* A window that takes files dropped on it. ween32 has no drag and drop, so
 * nothing acts on this yet -- but a program that accepts files says so when
 * it creates its window, and WordPad's frame does. Named so it can be said. */
#define WS_EX_ACCEPTFILES 0x00000010L
/* A child that does not tell its parent when it comes and goes. */
#define WS_EX_NOPARENTNOTIFY 0x00000004L

/* EDIT styles */
#define ES_LEFT 0x0000L
#define ES_CENTER 0x0001L
#define ES_RIGHT 0x0002L
#define ES_MULTILINE 0x0004L
#define ES_AUTOVSCROLL 0x0040L
#define ES_AUTOHSCROLL 0x0080L
#define ES_READONLY 0x0800L
/* A field that takes digits and nothing else, and one that keeps no undo. */
#define ES_NUMBER 0x2000L
#define ES_NOHIDESEL 0x0100L
#define ES_UPPERCASE 0x0008L
#define ES_LOWERCASE 0x0010L
#define ES_PASSWORD 0x0020L
#define ES_WANTRETURN 0x1000L

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
#define EN_MAXTEXT 0x0501
#define EN_HSCROLL 0x0601
#define EN_VSCROLL 0x0602
#define EN_UPDATE 0x0400
/* Select a run of the text: wParam is where it starts, lParam where it ends,
 * and -1 for the end means all of it. */
#define EM_GETSEL 0x00B0
#define EM_SETSEL 0x00B1
#define EM_LINESCROLL 0x00B6
#define EM_SCROLLCARET 0x00B7
#define EM_GETMODIFY 0x00B8
#define EM_SETMODIFY 0x00B9
#define EM_GETLINECOUNT 0x00BA
#define EM_LINEINDEX 0x00BB
#define EM_GETHANDLE 0x00BD
#define EM_LINELENGTH 0x00C1
#define EM_REPLACESEL 0x00C2
#define EM_GETLINE 0x00C4
#define EM_LIMITTEXT 0x00C5
#define EM_CANUNDO 0x00C6
#define EM_UNDO 0x00C7
#define EM_LINEFROMCHAR 0x00C9
#define EM_EMPTYUNDOBUFFER 0x00CD
#define EM_GETFIRSTVISIBLELINE 0x00CE
#define EM_SETLIMITTEXT EM_LIMITTEXT
/* What an edit leaves before and after its text. It works one out from the
 * font by default; a control that puts an edit inside itself says otherwise,
 * which is how a combo box lines its field up with what it draws beside it. */
#define EM_SETMARGINS 0x00D3
/* Where a character is drawn, and which character is at a point. One number
 * each, and two conventions: an EDIT takes the index in wParam and packs the
 * point into what it answers, a rich edit takes a POINTL to fill in and the
 * index in lParam. Same message, and the rich edit's own header leaves
 * winuser's number alone. */
#define EM_POSFROMCHAR 0x00D6
#define EM_CHARFROMPOS 0x00D7
#define EC_LEFTMARGIN 0x0001
#define EC_RIGHTMARGIN 0x0002

/* ---- the rich edit (riched20) --------------------------------------------
 *
 * A second text control, not a widened EDIT: its text carries formatting per
 * run of characters and per paragraph, which the EDIT's plain buffer cannot
 * hold. What is here is the plain-text half -- the class, the selection in
 * the terms the rich edit states it in, and the mask that decides which
 * notifications reach the parent, which is nothing until a program asks.
 *
 * The window class is riched20.dll's on Windows and this library's here, so
 * a program that loads the DLL before it makes one goes on working: the load
 * is what Windows needs and the registration is what this needs. */
#define RICHEDIT_CLASSA "RichEdit20A"
#define RICHEDIT_CLASS10A "RICHEDIT"
/* A rich edit shows a scroll bar only when there is something to scroll,
 * unless this says otherwise -- which is the opposite way round from an
 * EDIT, where WS_VSCROLL alone means the bar is always there. The machine's
 * WordPad has no bar at all on an empty document. */
#define ES_DISABLENOSCROLL 0x00002000
#define EM_CANPASTE (WM_USER + 50)
#define EM_EXGETSEL (WM_USER + 52)
#define EM_EXLIMITTEXT (WM_USER + 53)
#define EM_EXSETSEL (WM_USER + 55)
#define EM_GETEVENTMASK (WM_USER + 59)
#define EM_GETSELTEXT (WM_USER + 62)
#define EM_SETEVENTMASK (WM_USER + 69)
#define EM_GETTEXTRANGE (WM_USER + 75)
#define EM_SETUNDOLIMIT (WM_USER + 82)
/* Which notifications the control is allowed to send. A rich edit starts
 * with none of them, where an EDIT sends EN_CHANGE whether or not anybody
 * wanted it -- so a program that wants to hear about a change has to say so.
 */
#define ENM_NONE 0x00000000
#define ENM_CHANGE 0x00000001
#define ENM_UPDATE 0x00000002
#define ENM_SCROLL 0x00000004
#define ENM_SELCHANGE 0x00080000
#define EN_SELCHANGE 0x0702
/* A range of characters, which is how a rich edit says where the selection
 * is: two offsets rather than an EDIT's packed pair, so a document longer
 * than sixty-five thousand characters can still be talked about. */
typedef struct _charrange {
    LONG cpMin;
    LONG cpMax;
} CHARRANGE;
typedef struct _textrange {
    CHARRANGE chrg;
    LPSTR lpstrText;
} TEXTRANGEA;

/* ---- character formatting -------------------------------------------------
 *
 * The formatting a run of characters carries. dwMask is what the caller
 * means, or -- coming back from EM_GETCHARFORMAT over a range -- what the
 * control is sure of: an attribute that differs somewhere in the range has
 * its bit cleared, and dwEffects still carries the first run's value. A
 * format bar reads the mask to decide whether Bold is in, out, or neither;
 * reading the effect alone shows it in. Measured on the machine's own
 * riched20 -- see docs/testing.md.
 *
 * A height is in twips, a twentieth of a point: 165 is the eight and a
 * quarter points a fresh control is lettered in at 96 dpi. */
#define EM_GETCHARFORMAT (WM_USER + 58)
#define EM_SETCHARFORMAT (WM_USER + 68)
#define SCF_DEFAULT 0x0000
#define SCF_SELECTION 0x0001
#define SCF_WORD 0x0002
#define SCF_ALL 0x0004
#define CFM_BOLD 0x00000001
#define CFM_ITALIC 0x00000002
#define CFM_UNDERLINE 0x00000004
#define CFM_STRIKEOUT 0x00000008
#define CFM_PROTECTED 0x00000010
#define CFM_LINK 0x00000020
#define CFM_SIZE 0x80000000
#define CFM_COLOR 0x40000000
#define CFM_FACE 0x20000000
#define CFM_OFFSET 0x10000000
#define CFM_CHARSET 0x08000000
#define CFE_BOLD 0x00000001
#define CFE_ITALIC 0x00000002
#define CFE_UNDERLINE 0x00000004
#define CFE_STRIKEOUT 0x00000008
#define CFE_PROTECTED 0x00000010
#define CFE_LINK 0x00000020
#define CFE_AUTOCOLOR 0x40000000
/* ---- paragraph formatting -------------------------------------------------
 *
 * The other half of the model: what a paragraph carries, as opposed to what
 * a run of characters does. Set over a selection it applies to every
 * paragraph the selection touches, whole -- a rich edit has no notion of
 * half a paragraph being centred -- and read back over several that differ
 * it clears the mask bit of whatever is not the same throughout, exactly as
 * a CHARFORMAT does. Measured on the machine; see docs/testing.md. */
#define EM_SETTARGETDEVICE (WM_USER + 72)
#define EM_STREAMIN (WM_USER + 73)
#define EM_STREAMOUT (WM_USER + 74)
#define SF_TEXT 0x0001
#define SF_RTF 0x0002
#define SFF_SELECTION 0x8000
typedef DWORD (CALLBACK *EDITSTREAMCALLBACK)(DWORD_PTR, LPBYTE, LONG, LONG *);
/* Packed to four with the rest of <richedit.h>, which puts the callback at
 * twelve rather than sixteen: an eight-byte pointer after a DWORD, with no
 * padding to align it. */
#pragma pack(push, 4)
typedef struct _editstream {
    DWORD_PTR dwCookie;
    DWORD dwError;
    EDITSTREAMCALLBACK pfnCallback;
} EDITSTREAM;
#pragma pack(pop)
#define EM_GETPARAFORMAT (WM_USER + 61)
#define EM_SETPARAFORMAT (WM_USER + 71)
#define PFM_STARTINDENT 0x00000001
#define PFM_RIGHTINDENT 0x00000002
#define PFM_OFFSET 0x00000004
#define PFM_ALIGNMENT 0x00000008
#define PFM_TABSTOPS 0x00000010
#define PFM_NUMBERING 0x00000020
#define PFM_OFFSETINDENT 0x80000000
#define PFA_LEFT 1
#define PFA_RIGHT 2
#define PFA_CENTER 3
#define MAX_TAB_STOPS 32
typedef struct _paraformat {
    UINT cbSize;
    DWORD dwMask;
    WORD wNumbering;
    WORD wReserved;
    LONG dxStartIndent;
    LONG dxRightIndent;
    LONG dxOffset;
    WORD wAlignment;
    SHORT cTabCount;
    LONG rgxTabs[MAX_TAB_STOPS];
} PARAFORMAT;

/* What EN_SELCHANGE carries: where the selection is now, and what kind of
 * thing is in it, which is how a format bar knows to grey what cannot apply. */
#define SEL_EMPTY 0x0000
#define SEL_TEXT 0x0001
#define SEL_OBJECT 0x0002
#define SEL_MULTICHAR 0x0004
#define SEL_MULTIOBJECT 0x0008
/* Packed to four, because the whole of win32's <richedit.h> is: it opens
 * with pshpack4.h and closes with poppack.h, so a WORD after a CHARRANGE
 * leaves two bytes of padding and not six. Thirty-six bytes either side. */
#pragma pack(push, 4)
typedef struct _selchange {
    NMHDR nmhdr;
    CHARRANGE chrg;
    WORD seltyp;
} SELCHANGE;
#pragma pack(pop)

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
/* The item at the top of what is shown, which is how a program asks where a
 * tree has been scrolled to -- and how a test asks. */
#define TVGN_FIRSTVISIBLE 0x0005
#define TVM_EXPAND (TV_FIRST + 2)
/* How many items are shown whole. A page of this tree is one of these less a
 * row -- measured on the machine, and written down in docs/testing.md. */
#define TVM_GETVISIBLECOUNT (TV_FIRST + 16)
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
/* win32 carries an unnamed union of the extended item and the plain one, so
 * `item` is at the same place either way and the struct is the size of the
 * larger. TVITEMEXA is TVITEMA with iIntegral after it -- the height of a row
 * in whole items, which ween32 does not vary. */
typedef struct tagTVITEMEXA {
    UINT mask;
    HTREEITEM hItem;
    UINT state, stateMask;
    LPSTR pszText;
    int cchTextMax, iImage, iSelectedImage, cChildren;
    LPARAM lParam;
    int iIntegral;
} TVITEMEXA;
typedef struct tagTVINSERTSTRUCTA {
    HTREEITEM hParent;
    HTREEITEM hInsertAfter;
    __extension__ union {
        TVITEMEXA itemex;
        TVITEMA item;
    };
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
    UINT type;      /* a filter header's kind, which ween32 does not draw */
    void *pvFilter; /* and the filter itself */
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
/* The four after lParam are win32's and were missing here, which made every
 * LVITEMA short of the one an application fills in. iIndent is acted on by
 * nothing yet; the group fields belong to list-view groups, which ween32 has
 * not got. Named, taken, and listed in the ROADMAP as unread. */
typedef struct tagLVITEMA {
    UINT mask;
    int iItem, iSubItem;
    UINT state, stateMask;
    LPSTR pszText;
    int cchTextMax, iImage;
    LPARAM lParam;
    int iIndent;
    int iGroupId;
    UINT cColumns;
    PUINT puColumns;
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

/* Five of these fields are unions on win32 -- an icon or the name of one, a
 * page number or the name of a page, and so on -- and were single members
 * here. That is not only three missing fields at the end: a union of a UINT
 * and a pointer is eight bytes and eight-aligned, so nStartPage sat at 44
 * here and at 48 on win32, and everything after it was out too.
 *
 * `__extension__` because an unnamed union is C11 and this builds as C99
 * with -pedantic; win32's own header does the same thing through its
 * __C89_NAMELESS. Naming them would move the fields an application writes. */
typedef struct tagPROPSHEETHEADERA {
    DWORD dwSize;
    DWORD dwFlags;
    HWND hwndParent;
    HINSTANCE hInstance;
    __extension__ union {
        HICON hIcon;
        LPCSTR pszIcon;
    };
    LPCSTR pszCaption;
    UINT nPages;
    __extension__ union {
        UINT nStartPage;
        LPCSTR pStartPage;
    };
    __extension__ union {
        LPCPROPSHEETPAGEA ppsp;
        HPROPSHEETPAGE *phpage;
    };
    void *pfnCallback;
    __extension__ union {
        HBITMAP hbmWatermark;
        LPCSTR pszbmWatermark;
    };
    HPALETTE hplWatermark;
    __extension__ union {
        HBITMAP hbmHeader;
        LPCSTR pszbmHeader;
    };
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
#define SB_GETRECT (WM_USER + 10)
/* The flags SB_SETTEXTA takes above the part index. Only the first is drawn
 * differently here; the others are accepted and ignored. */
#define SBT_NOBORDERS 0x0100
#define SBT_POPOUT 0x0200
#define SBT_RTLREADING 0x0400
#define SBT_OWNERDRAW 0x1000
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
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_SHOWNA 8
#define SW_MINIMIZE 6
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_RESTORE 9
/* What a program started from a shortcut is given, and what it hands
 * straight to ShowWindow: whatever the shortcut said, or normal. */
#define SW_SHOWDEFAULT 10

/* The program itself, as a handle. Windows gives a module handle meaning the
 * loaded image; nothing here is loaded from an image, and every call that
 * takes an HINSTANCE ignores it, so this is a handle to say which program
 * rather than a way to reach into it. NULL names the program itself. */
HINSTANCE GetModuleHandleA(LPCSTR name);

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
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7a
#define VK_F12 0x7b
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
#define COLOR_INACTIVECAPTION 3
#define COLOR_WINDOWFRAME 6
/* The grey a document window's spare space is filled with -- the surround a
 * picture smaller than its window sits on. */
#define COLOR_APPWORKSPACE 12
#define COLOR_WINDOW 5
#define COLOR_MENU 4
#define COLOR_MENUTEXT 7
#define COLOR_WINDOWTEXT 8
#define COLOR_CAPTIONTEXT 9
#define COLOR_INACTIVECAPTIONTEXT 19
#define COLOR_BTNFACE 15
#define COLOR_BTNSHADOW 16
#define COLOR_HIGHLIGHT 13
#define COLOR_HIGHLIGHTTEXT 14
#define COLOR_GRAYTEXT 17
#define COLOR_BTNTEXT 18
#define COLOR_BTNHIGHLIGHT 20
#define COLOR_3DDKSHADOW 21
#define COLOR_3DLIGHT 22
/* what a tip is painted in: the pale yellow behind it and the black on it */
#define COLOR_INFOTEXT 23
#define COLOR_INFOBK 24
#define COLOR_GRADIENTACTIVECAPTION 27
#define COLOR_GRADIENTINACTIVECAPTION 28

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
/* Lay the text out the way an edit control does -- which for DrawText means
 * only whole lines, so a program printing what is in a field breaks the page
 * where the field would. Nothing here draws partial lines anyway, so it is
 * accepted and changes nothing. */
#define DT_EDITCONTROL 0x00002000

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
#define VARIABLE_PITCH 2
#define FF_DONTCARE (0 << 4)
#define FF_ROMAN (1 << 4)
#define FF_SWISS (2 << 4)
#define FF_MODERN (3 << 4)
#define FF_SCRIPT (4 << 4)
#define FF_DECORATIVE (5 << 4)
#define FW_DONTCARE 0
#define FW_THIN 100
#define FW_LIGHT 300
#define FW_REGULAR 400
#define FW_MEDIUM 500
#define FW_SEMIBOLD 600
#define FW_HEAVY 900
#define OEM_CHARSET 255
#define OUT_TT_PRECIS 4
#define OUT_TT_ONLY_PRECIS 7
#define CLIP_STROKE_PRECIS 2
#define DRAFT_QUALITY 1
#define PROOF_QUALITY 2
#define NONANTIALIASED_QUALITY 3
#define ANTIALIASED_QUALITY 4
#define CLEARTYPE_QUALITY 5

/* A font described rather than listed out: the same fourteen things
 * CreateFont takes, in a structure a program can keep, hand to ChooseFont and
 * store in its settings. CreateFontIndirect is CreateFont with them read out
 * of it, which is exactly what it is on Windows. */
#define LF_FACESIZE 32
typedef struct tagLOGFONTA {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, *PLOGFONTA, *LPLOGFONTA;

/* A run of characters' formatting, which needs a face name and so waits for
 * the one constant that says how long one is. The rest of the rich edit's
 * declarations are above, with its messages. */
typedef struct _charformat {
    UINT cbSize;
    DWORD dwMask;
    DWORD dwEffects;
    LONG yHeight;
    LONG yOffset;
    COLORREF crTextColor;
    BYTE bCharSet;
    BYTE bPitchAndFamily;
    char szFaceName[LF_FACESIZE];
} CHARFORMATA;
HFONT CreateFontIndirectA(const LOGFONTA *lf);

/* What a font comes out as once it is realised: the numbers a program lays
 * text out with. GetTextMetrics answers for the font selected into the DC. */
typedef struct tagTEXTMETRICA {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    BYTE tmFirstChar;
    BYTE tmLastChar;
    BYTE tmDefaultChar;
    BYTE tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICA, *PTEXTMETRICA, *LPTEXTMETRICA;
BOOL GetTextMetricsA(HDC dc, TEXTMETRICA *tm);

/* What the device this DC draws on is like. The screen here is the desktop
 * and its dots per inch are the ones the whole library scales by, which is
 * how a program turns a point size into a height and gets the same answer
 * the library would. */
#define DRIVERVERSION 0
#define TECHNOLOGY 2
#define HORZSIZE 4
#define VERTSIZE 6
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define PLANES 14
#define NUMCOLORS 24
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define PHYSICALWIDTH 110
#define PHYSICALHEIGHT 111
#define PHYSICALOFFSETX 112
#define PHYSICALOFFSETY 113
int GetDeviceCaps(HDC dc, int index);

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
    void *pvReserved;
    DWORD dwReserved;
    DWORD FlagsEx;
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

/* ---- the common dialogs that are not here yet -----------------------------
 *
 * A Windows program that can choose a font, find text or print links against
 * these whether or not it ever opens one, so a program cannot be built at all
 * without them. They are declared with the structures win32 gives them --
 * a program fills one in and takes sizeof it -- and they answer the way the
 * real call answers when the user changes their mind: FALSE, and nothing
 * written back. What each one would take to be real is in ROADMAP.md; until
 * then a program builds, runs, and finds that menu item does nothing, which
 * is a truthful state of affairs rather than a wrong dialog.
 */

typedef struct tagCHOOSEFONTA {
    DWORD lStructSize;
    HWND hwndOwner;
    HDC hDC;
    LPLOGFONTA lpLogFont;
    INT iPointSize;
    DWORD Flags;
    COLORREF rgbColors;
    LPARAM lCustData;
    INT_PTR(CALLBACK *lpfnHook)(HWND, UINT, WPARAM, LPARAM);
    LPCSTR lpTemplateName;
    HINSTANCE hInstance;
    LPSTR lpszStyle;
    WORD nFontType;
    WORD ___MISSING_ALIGNMENT__;
    INT nSizeMin;
    INT nSizeMax;
} CHOOSEFONTA, *LPCHOOSEFONTA;

#define CF_SCREENFONTS 0x00000001
#define CF_PRINTERFONTS 0x00000002
#define CF_BOTH 0x00000003
#define CF_INITTOLOGFONTSTRUCT 0x00000040
#define CF_USESTYLE 0x00000080
#define CF_EFFECTS 0x00000100
#define CF_APPLY 0x00000200
#define CF_ANSIONLY 0x00000400
#define CF_NOVECTORFONTS 0x00000800
#define CF_NOSIMULATIONS 0x00001000
#define CF_LIMITSIZE 0x00002000
#define CF_FIXEDPITCHONLY 0x00004000
#define CF_FORCEFONTEXIST 0x00010000
#define CF_SCALABLEONLY 0x00020000
#define CF_TTONLY 0x00040000
#define CF_NOFACESEL 0x00080000
#define CF_NOSCRIPTSEL 0x00800000
BOOL ChooseFontA(CHOOSEFONTA *cf);

/* Find and Replace are modeless: the real ones put a window up and send the
 * owner the message RegisterWindowMessage(FINDMSGSTRING) each time the user
 * presses a button. Nothing here puts that window up, so nothing sends that
 * message, and the call answers with no window at all. */
typedef struct tagFINDREPLACEA {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    DWORD Flags;
    LPSTR lpstrFindWhat;
    LPSTR lpstrReplaceWith;
    WORD wFindWhatLen;
    WORD wReplaceWithLen;
    LPARAM lCustData;
    INT_PTR(CALLBACK *lpfnHook)(HWND, UINT, WPARAM, LPARAM);
    LPCSTR lpTemplateName;
} FINDREPLACEA, *LPFINDREPLACEA;

#define FR_DOWN 0x00000001
#define FR_WHOLEWORD 0x00000002
#define FR_MATCHCASE 0x00000004
#define FR_FINDNEXT 0x00000008
#define FR_REPLACE 0x00000010
#define FR_REPLACEALL 0x00000020
#define FR_DIALOGTERM 0x00000040
#define FR_SHOWHELP 0x00000080
#define FR_NOUPDOWN 0x00000400
#define FR_NOMATCHCASE 0x00000800
#define FR_NOWHOLEWORD 0x00001000
#define FR_HIDEUPDOWN 0x00004000
#define FR_HIDEMATCHCASE 0x00008000
#define FR_HIDEWHOLEWORD 0x00010000
#define FINDMSGSTRING "commdlg_FindReplace"
HWND FindTextA(FINDREPLACEA *fr);
HWND ReplaceTextA(FINDREPLACEA *fr);

/* Printing. A printer here would mean a device context that draws into a
 * spool file and something to send it to, neither of which exists; the
 * dialogs answer as a cancelled one does and the document calls fail, which
 * is what a program checks for anyway. */
typedef struct tagPDA {
    DWORD lStructSize;
    HWND hwndOwner;
    HGLOBAL hDevMode;
    HGLOBAL hDevNames;
    HDC hDC;
    DWORD Flags;
    WORD nFromPage;
    WORD nToPage;
    WORD nMinPage;
    WORD nMaxPage;
    WORD nCopies;
    HINSTANCE hInstance;
    LPARAM lCustData;
    INT_PTR(CALLBACK *lpfnPrintHook)(HWND, UINT, WPARAM, LPARAM);
    INT_PTR(CALLBACK *lpfnSetupHook)(HWND, UINT, WPARAM, LPARAM);
    LPCSTR lpPrintTemplateName;
    LPCSTR lpSetupTemplateName;
    HGLOBAL hPrintTemplate;
    HGLOBAL hSetupTemplate;
} PRINTDLGA, *LPPRINTDLGA;

#define PD_ALLPAGES 0x00000000
#define PD_SELECTION 0x00000001
#define PD_PAGENUMS 0x00000002
#define PD_NOSELECTION 0x00000004
#define PD_NOPAGENUMS 0x00000008
#define PD_COLLATE 0x00000010
#define PD_PRINTTOFILE 0x00000020
#define PD_PRINTSETUP 0x00000040
#define PD_NOWARNING 0x00000080
#define PD_RETURNDC 0x00000100
#define PD_RETURNIC 0x00000200
#define PD_RETURNDEFAULT 0x00000400
#define PD_USEDEVMODECOPIES 0x00040000
#define PD_USEDEVMODECOPIESANDCOLLATE 0x00040000
#define PD_HIDEPRINTTOFILE 0x00100000
BOOL PrintDlgA(PRINTDLGA *pd);

typedef struct tagPSDA {
    DWORD lStructSize;
    HWND hwndOwner;
    HGLOBAL hDevMode;
    HGLOBAL hDevNames;
    DWORD Flags;
    POINT ptPaperSize;
    RECT rtMinMargin;
    RECT rtMargin;
    HINSTANCE hInstance;
    LPARAM lCustData;
    INT_PTR(CALLBACK *lpfnPageSetupHook)(HWND, UINT, WPARAM, LPARAM);
    INT_PTR(CALLBACK *lpfnPagePaintHook)(HWND, UINT, WPARAM, LPARAM);
    LPCSTR lpPageSetupTemplateName;
    HGLOBAL hPageSetupTemplate;
} PAGESETUPDLGA, *LPPAGESETUPDLGA;

#define PSD_DEFAULTMINMARGINS 0x00000000
#define PSD_INWININIINTLMEASURE 0x00000000
#define PSD_MINMARGINS 0x00000001
#define PSD_MARGINS 0x00000002
#define PSD_INTHOUSANDTHSOFINCHES 0x00000004
#define PSD_INHUNDREDTHSOFMILLIMETERS 0x00000008
#define PSD_DISABLEMARGINS 0x00000010
#define PSD_DISABLEPRINTER 0x00000020
#define PSD_NOWARNING 0x00000080
#define PSD_DISABLEORIENTATION 0x00000100
#define PSD_RETURNDEFAULT 0x00000400
#define PSD_DISABLEPAPER 0x00000200
BOOL PageSetupDlgA(PAGESETUPDLGA *psd);

/* A document on a printer DC: begun, paged and ended. Windows answers with a
 * positive number on success and a negative one or zero on failure, and that
 * is what a program checks. */
typedef struct tagDOCINFOA {
    int cbSize;
    LPCSTR lpszDocName;
    LPCSTR lpszOutput;
    LPCSTR lpszDatatype;
    DWORD fwType;
} DOCINFOA, *LPDOCINFOA;

int StartDocA(HDC dc, const DOCINFOA *di);
int StartPage(HDC dc);
int EndPage(HDC dc);
int EndDoc(HDC dc);
int AbortDoc(HDC dc);

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
/* ---- tips ----------------------------------------------------------------
 *
 * The little window that says what a button is for when the pointer rests on
 * it. A control with TBSTYLE_TOOLTIPS keeps one and asks its parent what to
 * put in it — TTN_GETDISPINFO, answered by filling in lpszText — so the words
 * belong to the application and the timing to the control.
 */
#define TOOLTIPS_CLASSA "tooltips_class32"
#define TTS_ALWAYSTIP 0x01
#define TTS_NOPREFIX 0x02
#define TTM_ACTIVATE (WM_USER + 1)
#define TTM_UPDATETIPTEXTA (WM_USER + 12)
#define TTN_FIRST (0U - 520U)
#define TTN_GETDISPINFOA (TTN_FIRST - 0U)

typedef struct tagNMTTDISPINFOA {
    NMHDR hdr;
    LPSTR lpszText;  /* what to show: point it at your own, or fill szText */
    char szText[80];
    HINSTANCE hinst;
    UINT uFlags;
    LPARAM lParam;
} NMTTDISPINFOA;

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
#define TBSTYLE_TOOLTIPS 0x0100 /* the bar shows a tip for a button with no
                                 * label of its own, as a shell's does */
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
    /* Six, not two: win32 declares this [6] under _WIN64 and [2] otherwise,
     * and this library builds 64-bit -- its own win32 gate targets x86_64.
     * Not written as that #ifdef, because _WIN64 is not defined when this
     * header is compiled natively, so the test would take the wrong arm on
     * the very side it is meant to describe. The padding to dwData is the
     * same either way, so every offset and the struct's size agree with two;
     * the only thing that differs is how much of it an application may write,
     * which is why only a comparison of the field's own width finds it. */
    BYTE bReserved[6];
    DWORD_PTR dwData;
    INT_PTR iString;
} TBBUTTON, *LPTBBUTTON;

#define TB_ENABLEBUTTON (WM_USER + 1)
#define TB_CHECKBUTTON (WM_USER + 2)
/* Hold a button down, or let it up, without the pointer: what a button that
 * has put a menu up under itself does until the menu is answered. */
#define TB_PRESSBUTTON (WM_USER + 3)
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
#define TB_GETTOOLTIPS (WM_USER + 35)
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
/* What the button at a place is, for an application that has to decide which
 * of its buttons a chevron must carry. */
#define TB_GETBUTTON (WM_USER + 23)
#define TB_AUTOSIZE (WM_USER + 33)

/* The arrow beside a drop-down button was pressed: show the menu. */
#define TBN_DROPDOWN (0U - 710U)
/* The four after iItem are win32's. A drop-down notification only needs the
 * item, which is why they were never here -- but the struct is what the
 * application declares its handler against, so it has to be the whole one.
 * ween32 fills in the item and leaves the rest as the caller left them. */
typedef struct {
    NMHDR hdr;
    int iItem;
    TBBUTTON tbButton;
    int cchText;
    LPSTR pszText;
    RECT rcButton;
} NMTOOLBAR;

/* ---- rebar ----------------------------------------------------------------
 *
 * The bands a shell's toolbars sit in: each is a row with a gripper at its
 * left, an optional label, and one control filling the rest. An etched line
 * runs above every band, which is what separates them.
 *
 * Bands share a row until one asks to start a new one with RBBS_BREAK, which
 * a shell sets on each of its bars — so a shell's stack and a plainer rebar's
 * row of bars side by side both come out as they do on Windows. What is not
 * here yet is carrying a band about by its gripper. */
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
#define RBBIM_BACKGROUND 0x00000080
#define RBBIM_ID 0x00000100
/* The width the band's child would like: what it takes to show everything in
 * it. A band narrower than this has something hidden, which is what puts a
 * chevron on it. */
#define RBBIM_IDEALSIZE 0x00000200
#define RBBIM_LPARAM 0x00000400
#define RBBIM_HEADERSIZE 0x00000800

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
/* A band too narrow for what is in it wears a chevron -- the double arrow at
 * its right edge -- and pressing it asks the application what to put up. */
#define RBBS_USECHEVRON 0x00000200

/* The whole of the classic struct, in win32's own order, down to cxHeader.
 * Not as far as the field the library happens to read: an application fills
 * this in, so the shape is the contract, and a program setting cxIdeal or
 * lParam has to compile on both sides whether or not ween32 acts on it yet.
 * The Vista pair after cxHeader -- rcChevronLocation, uChevronState -- is
 * left out; it is guarded by NTDDI_VERSION there too.
 *
 * What is stored and handed back, what is only taken, and what is acted on
 * are three different questions; the ROADMAP says which is which. */
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
    HBITMAP hbmBack;
    UINT wID;
    UINT cyChild;
    UINT cyMaxChild;
    UINT cyIntegral;
    UINT cxIdeal;
    LPARAM lParam;
    UINT cxHeader;
} REBARBANDINFOA;

#define RB_INSERTBANDA (WM_USER + 1)
#define RB_SETBANDINFOA (WM_USER + 6)
/* And read one back: what a program set is what it gets. */
#define RB_GETBANDINFOA (WM_USER + 29)
#define RB_GETBANDCOUNT (WM_USER + 12)
#define RB_GETBARHEIGHT (WM_USER + 27)
/* Show or hide one band, which is what View > Toolbars does to each of them. */
#define RB_SHOWBAND (WM_USER + 35)
/* Take a band out of the order and put it back somewhere else. This is the
 * move a gripper carried up or down makes, without the pointer. */
#define RB_MOVEBAND (WM_USER + 39)

/* What a rebar tells its parent. A bar whose height changed says so and the
 * application lays out around it -- which is the win32 shape, the control
 * noticing and the application arranging. It does *not* send its parent a
 * WM_SIZE: that is the frame's own message about its own client area, and a
 * control sending one has to invent the size it carries. */
#define RBN_FIRST (0U - 831U)
#define RBN_HEIGHTCHANGE (RBN_FIRST - 0U)
/* And what it says while a band is being carried by its gripper: once when
 * the drag starts, once when it ends, and once for the arrangement it left
 * behind. An application that has to save where the bars ended up listens to
 * the last of these. */
#define RBN_LAYOUTCHANGED (RBN_FIRST - 2U)
#define RBN_BEGINDRAG (RBN_FIRST - 4U)
#define RBN_ENDDRAG (RBN_FIRST - 5U)
/* A chevron was pressed. The rebar draws it and says so; what comes out of it
 * -- which buttons, as a menu or otherwise -- is the application's, because
 * only the application knows what is in the band. */
#define RBN_CHEVRONPUSHED (RBN_FIRST - 10U)

typedef struct {
    NMHDR hdr;
    UINT uBand;
    UINT wID;
    LPARAM lParam;
    RECT rc; /* where the chevron is, so a menu can be put under it */
    LPARAM lParamNM;
} NMREBARCHEVRON;

/* Which band, and which part of it. dwMask says which of the last three
 * fields were filled in. */
#define RBNM_ID 0x1
#define RBNM_STYLE 0x2
#define RBNM_LPARAM 0x4

typedef struct {
    NMHDR hdr;
    DWORD dwMask;
    UINT uBand;
    UINT fStyle;
    UINT wID;
    LPARAM lParam;
} NMREBAR;

/* What is under a point: nothing, a band's name, a band's contents, or the
 * handle it is carried by. */
#define RBHT_NOWHERE 0x1
#define RBHT_CAPTION 0x2
#define RBHT_CLIENT 0x3
#define RBHT_GRABBER 0x4
#define RBHT_CHEVRON 0x8

typedef struct {
    POINT pt;
    UINT flags;
    int iBand;
} RBHITTESTINFO;

/* Ask which band a point is in, and what part of it. */
#define RB_HITTEST (WM_USER + 8)

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

/* The same class, with the two fields the later one added: its own size, so
 * the call can tell the two apart, and a small icon for the caption. */
typedef struct tagWNDCLASSEXA {
    UINT cbSize;
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
    HICON hIconSm;
} WNDCLASSEXA;

ATOM RegisterClassExA(const WNDCLASSEXA *wc);

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
/* Whether it is an icon or filling the screen. A program asks before saving
 * its window position, since the position of a minimised window is not the
 * one to come back to. There is no minimising here -- the window manager's
 * business, and the library never asks for it -- so IsIconic answers FALSE
 * and means it. */
int GetWindowTextLengthA(HWND wnd);
/* A message the system gives a name rather than a number: everyone who asks
 * for the same name is given the same message, which is how two programs --
 * or a program and a common dialog -- agree on one without a header between
 * them. The numbers start where win32's do, above the last WM_. */
UINT RegisterWindowMessageA(LPCSTR name);
BOOL IsIconic(HWND wnd);
BOOL IsZoomed(HWND wnd);
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
UINT GetDoubleClickTime(void);
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
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
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
/* ---- files dropped on a window, and the About box -------------------------
 *
 * SHELL32's small corner of this: a window that says it will take dropped
 * files hears WM_DROPFILES with a handle it asks for the names, and a
 * program's About box is one the shell puts up with the program's name and
 * icon in it. Dropping needs the drag protocol of whatever is dragging --
 * XDND on X11 -- which is not here yet, so a window can say it accepts files
 * and simply never hears about any; the About box is a MessageBox with the
 * program's own text in it, which is what the shell's own is underneath. */
typedef struct HDROP__ *HDROP;
#define WM_DROPFILES 0x0233
void DragAcceptFiles(HWND wnd, BOOL accept);
UINT DragQueryFileA(HDROP drop, UINT index, LPSTR name, UINT max);
void DragFinish(HDROP drop);
BOOL DragQueryPoint(HDROP drop, POINT *pt);
void ShellAboutA(HWND owner, LPCSTR app, LPCSTR other, HICON icon);

/* ---- memory a handle stands for ------------------------------------------
 *
 * GlobalAlloc and LocalAlloc are what a win32 program allocates with when
 * something else will free it -- the clipboard, a common dialog, an edit
 * control's own buffer. Windows keeps moveable blocks and hands out handles
 * that have to be locked; nothing here moves, so a block is its own handle
 * and locking it hands it back, which is what GlobalLock does for the fixed
 * kind on Windows too. */
#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GPTR 0x0040
#define GHND 0x0042
#define LMEM_FIXED 0x0000
#define LMEM_MOVEABLE 0x0002
#define LMEM_ZEROINIT 0x0040
#define LPTR 0x0040
#define LHND 0x0042
HGLOBAL GlobalAlloc(UINT flags, size_t bytes);
void *GlobalLock(HGLOBAL mem);
BOOL GlobalUnlock(HGLOBAL mem);
HGLOBAL GlobalFree(HGLOBAL mem);
size_t GlobalSize(HGLOBAL mem);
HLOCAL LocalAlloc(UINT flags, size_t bytes);
void *LocalLock(HLOCAL mem);
BOOL LocalUnlock(HLOCAL mem);
HLOCAL LocalFree(HLOCAL mem);
size_t LocalSize(HLOCAL mem);
#define ZeroMemory(dst, len) memset((dst), 0, (len))
#define CopyMemory(dst, src, len) memcpy((dst), (src), (len))
#define FillMemory(dst, len, fill) memset((dst), (fill), (len))

/* ---- the clock, and the two text encodings -------------------------------
 *
 * A program that stamps the time into a document asks the system for it and
 * asks the system to spell it, so that it comes out the way the user's
 * machine writes dates. Here that is the C library's locale, which is the
 * same idea in the same place. */
typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;
void GetLocalTime(SYSTEMTIME *st);
void GetSystemTime(SYSTEMTIME *st);
typedef DWORD LCID;
#define LOCALE_USER_DEFAULT 0x0400
#define LOCALE_SYSTEM_DEFAULT 0x0800
#define DATE_SHORTDATE 0x00000001
#define DATE_LONGDATE 0x00000002
#define TIME_NOMINUTESORSECONDS 0x00000001
#define TIME_NOSECONDS 0x00000002
#define TIME_NOTIMEMARKER 0x00000004
#define TIME_FORCE24HOURFORMAT 0x00000008
int GetDateFormatA(LCID locale, DWORD flags, const SYSTEMTIME *st,
                   LPCSTR format, LPSTR out, int max);
int GetTimeFormatA(LCID locale, DWORD flags, const SYSTEMTIME *st,
                   LPCSTR format, LPSTR out, int max);

/* UTF-16 is what a file may be written in even when the program itself is an
 * ANSI one, so the two conversions are real: CP_UTF8 both ways, and CP_ACP
 * being Latin-1 here -- the character set the A-API of this library speaks.
 * A character with no Latin-1 spelling comes out as a question mark, which
 * is what WideCharToMultiByte does with a default character. */
typedef uint16_t WCHAR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
#define CP_ACP 0
#define CP_UTF8 65001
int MultiByteToWideChar(UINT page, DWORD flags, LPCSTR in, int in_len,
                        LPWSTR out, int out_len);
int WideCharToMultiByte(UINT page, DWORD flags, LPCWSTR in, int in_len,
                        LPSTR out, int out_len, LPCSTR default_char,
                        BOOL *used_default);

HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, void *security,
                   DWORD disposition, DWORD flags, HANDLE template_file);
BOOL ReadFile(HANDLE file, void *buf, DWORD to_read, DWORD *read, void *ovl);
BOOL WriteFile(HANDLE file, const void *buf, DWORD to_write, DWORD *written,
               void *ovl);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG *high, DWORD method);
DWORD GetFileSize(HANDLE file, DWORD *high);
BOOL CloseHandle(HANDLE h);
/* ---- the registry --------------------------------------------------------
 *
 * Where a Windows program keeps what it remembers between runs: the window it
 * had open, the font it was set to, the boxes that were ticked. Off Windows
 * there is no registry, so this one is a file in the user's config directory
 * written in the REGEDIT4 format regedit itself exports -- a readable file a
 * person can look at and edit, rather than an invention of ours.
 *
 * What it holds is what a program of this kind stores: values under a key,
 * REG_SZ and REG_DWORD spelled out and anything else kept as bytes. There is
 * no enumeration, no deletion of whole keys, and no security: a program that
 * needs those is asking for a registry rather than for somewhere to put its
 * settings, and would be better told so than half-answered. */
typedef struct HKEY__ *HKEY;
typedef HKEY *PHKEY;
typedef DWORD REGSAM;
typedef LONG LSTATUS;
/* The predefined keys are numbers rather than pointers to anything, and the
 * cast is winreg.h's own: through a signed LONG first, so that on a 64-bit
 * build the top bit is carried up the whole pointer. Casting the unsigned
 * number instead gives a different value, which the constant gate catches. */
#define HKEY_CLASSES_ROOT ((HKEY)(UINT_PTR)((LONG)0x80000000))
#define HKEY_CURRENT_USER ((HKEY)(UINT_PTR)((LONG)0x80000001))
#define HKEY_LOCAL_MACHINE ((HKEY)(UINT_PTR)((LONG)0x80000002))
#define HKEY_USERS ((HKEY)(UINT_PTR)((LONG)0x80000003))
#define HKEY_CURRENT_CONFIG ((HKEY)(UINT_PTR)((LONG)0x80000005))
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_ITEMS 259L
#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_DWORD_LITTLE_ENDIAN 4
#define REG_MULTI_SZ 7
#define REG_OPTION_NON_VOLATILE 0x00000000L
#define REG_OPTION_VOLATILE 0x00000001L
#define REG_CREATED_NEW_KEY 0x00000001L
#define REG_OPENED_EXISTING_KEY 0x00000002L
/* The access rights, by the numbers winnt.h works out for them: the standard
 * rights of a read or a write, or'd with the key rights and with SYNCHRONIZE
 * taken back out. Nothing here acts on them -- a key that opens, opens for
 * both -- but a program passes them and they have to be the right numbers. */
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_CREATE_SUB_KEY 0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_NOTIFY 0x0010
#define KEY_CREATE_LINK 0x0020
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_EXECUTE 0x20019
#define KEY_ALL_ACCESS 0xF003F
LSTATUS RegCreateKeyExA(HKEY key, LPCSTR sub, DWORD reserved, LPSTR cls,
                        DWORD options, REGSAM access, void *security,
                        PHKEY out, DWORD *disposition);
LSTATUS RegOpenKeyExA(HKEY key, LPCSTR sub, DWORD options, REGSAM access,
                      PHKEY out);
LSTATUS RegCloseKey(HKEY key);
LSTATUS RegQueryValueExA(HKEY key, LPCSTR name, DWORD *reserved, DWORD *type,
                         BYTE *data, DWORD *size);
LSTATUS RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type,
                       const BYTE *data, DWORD size);
LSTATUS RegDeleteValueA(HKEY key, LPCSTR name);

#define CF_TEXT 1
#define CF_BITMAP 2
#define CF_UNICODETEXT 13
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
/* A BITMAP a program keeps in its script: one lookup, no file on disk. */
HBITMAP LoadBitmapA(HINSTANCE inst, LPCSTR name);
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

/* ---- resources ----------------------------------------------------------
 *
 * What a program keeps in its .rc and asks for back by number. On Windows
 * these read the resources the linker put in the binary; here they read the
 * .res the build compiled and embedded, which is the same data in the same
 * layouts.
 */

int LoadStringA(HINSTANCE inst, UINT id, LPSTR buf, int max);
HMENU LoadMenuA(HINSTANCE inst, LPCSTR name);
HMENU LoadMenuIndirectA(const void *tmpl);
HACCEL LoadAcceleratorsA(HINSTANCE inst, LPCSTR name);
HICON LoadIconA(HINSTANCE inst, LPCSTR name);
INT_PTR DialogBoxParamA(HINSTANCE inst, LPCSTR name, HWND owner, DLGPROC proc,
                        LPARAM param);
HWND CreateDialogParamA(HINSTANCE inst, LPCSTR name, HWND owner, DLGPROC proc,
                        LPARAM param);

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

/* ---- the ANSI side of the T-names ----------------------------------------
 *
 * windows.h hands a program TCHAR, TEXT() and the undecorated names --
 * SendMessage, DefWindowProc, LoadString -- resolved to whichever half of the
 * API the build asked for. This header stands in for windows.h off Windows,
 * so it owes a program the same names; and since the library is an A-API one,
 * they resolve to the A half. On Windows none of this is reached: the real
 * header is included instead and defines them itself.
 *
 * A program built with UNICODE wants the W half, and there is none here.
 * Saying so once is worth more than five hundred errors about types nobody
 * declared.
 */

#if defined(UNICODE) || defined(_UNICODE)
#error "ween32 is an ANSI (A-API) library: build without UNICODE/_UNICODE."
#endif

typedef char TCHAR;
typedef char _TCHAR;
typedef char *LPTSTR, *PTSTR, *LPTCH, *PTCHAR;
typedef const char *LPCTSTR, *PCTSTR;
#define TEXT(quote) quote
#define _T(quote) quote
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define CreateWindow CreateWindowA
#define CreateWindowEx CreateWindowExA
#define RegisterClass RegisterClassA
#define RegisterClassEx RegisterClassExA
#define UnregisterClass UnregisterClassA
#define GetClassName GetClassNameA
#define DefWindowProc DefWindowProcA
#define CallWindowProc CallWindowProcA
#define SendMessage SendMessageA
#define SendDlgItemMessage SendDlgItemMessageA
#define PostMessage PostMessageA
#define GetMessage GetMessageA
#define PeekMessage PeekMessageA
#define DispatchMessage DispatchMessageA
#define IsDialogMessage IsDialogMessageA
#define SetWindowText SetWindowTextA
#define GetWindowText GetWindowTextA
#define GetWindowTextLength GetWindowTextLengthA
#define SetDlgItemText SetDlgItemTextA
#define GetDlgItemText GetDlgItemTextA
#define SetDlgItemInt SetDlgItemIntA
#define GetDlgItemInt GetDlgItemIntA
#define MessageBox MessageBoxA
#define LoadCursor LoadCursorA
#define LoadBitmap LoadBitmapA
#define LoadIcon LoadIconA
#define LoadString LoadStringA
#define LoadMenu LoadMenuA
#define LoadMenuIndirect LoadMenuIndirectA
#define LoadAccelerators LoadAcceleratorsA
#define CreateAcceleratorTable CreateAcceleratorTableA
#define TranslateAccelerator TranslateAcceleratorA
#define DialogBoxParam DialogBoxParamA
#define DialogBoxIndirectParam DialogBoxIndirectParamA
#define CreateDialogParam CreateDialogParamA
#define CreateDialogIndirectParam CreateDialogIndirectParamA
#define AppendMenu AppendMenuA
#define InsertMenu InsertMenuA
#define ModifyMenu ModifyMenuA
#define SetWindowLong SetWindowLongA
#define GetWindowLong GetWindowLongA
#define SetWindowLongPtr SetWindowLongPtrA
#define GetWindowLongPtr GetWindowLongPtrA
#define RegisterWindowMessage RegisterWindowMessageA
#define RegCreateKeyEx RegCreateKeyExA
#define RegOpenKeyEx RegOpenKeyExA
#define RegQueryValueEx RegQueryValueExA
#define RegSetValueEx RegSetValueExA
#define RegDeleteValue RegDeleteValueA
#define WNDCLASSEX WNDCLASSEXA
#define WNDCLASS WNDCLASSA
#define DrawText DrawTextA
#define TextOut TextOutA
#define GetTextExtentPoint32 GetTextExtentPoint32A
#define CreateFont CreateFontA
#define CreateFontIndirect CreateFontIndirectA
#define GetTextMetrics GetTextMetricsA
#define LOGFONT LOGFONTA
#define PLOGFONT PLOGFONTA
#define LPLOGFONT LPLOGFONTA
#define TEXTMETRIC TEXTMETRICA
#define LPTEXTMETRIC LPTEXTMETRICA
#define GetOpenFileName GetOpenFileNameA
#define GetSaveFileName GetSaveFileNameA
#define ChooseColor ChooseColorA
#define ChooseFont ChooseFontA
#define FindText FindTextA
#define ReplaceText ReplaceTextA
#define PrintDlg PrintDlgA
#define PageSetupDlg PageSetupDlgA
#define StartDoc StartDocA
#define CHOOSEFONT CHOOSEFONTA
#define LPCHOOSEFONT LPCHOOSEFONTA
#define FINDREPLACE FINDREPLACEA
#define LPFINDREPLACE LPFINDREPLACEA
#define PRINTDLG PRINTDLGA
#define LPPRINTDLG LPPRINTDLGA
#define PAGESETUPDLG PAGESETUPDLGA
#define LPPAGESETUPDLG LPPAGESETUPDLGA
#define DOCINFO DOCINFOA
#define LPDOCINFO LPDOCINFOA
#define OPENFILENAME OPENFILENAMEA
#define LPOPENFILENAME LPOPENFILENAMEA
#define CHOOSECOLOR CHOOSECOLORA
#define LPCHOOSECOLOR LPCHOOSECOLORA
#define CreateFile CreateFileA
#define GetModuleHandle GetModuleHandleA
#define GetDateFormat GetDateFormatA
#define GetTimeFormat GetTimeFormatA
#define DragQueryFile DragQueryFileA
#define ShellAbout ShellAboutA
#define DialogBox(inst, name, owner, proc)                                     \
    DialogBoxParamA(inst, name, owner, proc, 0)
#define CreateDialog(inst, name, owner, proc)                                  \
    CreateDialogParamA(inst, name, owner, proc, 0)
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#define PropertySheet PropertySheetA
#define STATUSCLASSNAME STATUSCLASSNAMEA
#define WC_TABCONTROL WC_TABCONTROLA
#define WC_TREEVIEW WC_TREEVIEWA
#define WC_LISTVIEW WC_LISTVIEWA
#define WC_COMBOBOXEX WC_COMBOBOXEXA
#define TOOLBARCLASSNAME TOOLBARCLASSNAMEA
#define REBARCLASSNAME REBARCLASSNAMEA
#define TRACKBAR_CLASS TRACKBAR_CLASSA
#define RICHEDIT_CLASS RICHEDIT_CLASSA
#define TEXTRANGE TEXTRANGEA
#define CHARFORMAT CHARFORMATA
#define PROGRESS_CLASS PROGRESS_CLASSA
#define SB_SETTEXT SB_SETTEXTA
#define SB_GETTEXT SB_GETTEXTA

#ifdef __cplusplus
}
#endif

#endif /* !_WIN32 */
#endif /* WEEN32_H */
