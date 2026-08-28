/* Resources: what a program asks USER32 for by number.
 *
 * A Windows program keeps its menu, its accelerators, its dialogs and its
 * strings in a .rc, which the toolchain compiles into the binary; it then
 * asks for them back with LoadMenu, LoadAccelerators, DialogBox and
 * LoadString. Off Windows there is no binary to carry them, so the build
 * compiles the script with `zig rc` -- the same compiler the Windows build
 * uses -- and links the resulting .res in as bytes, which this reads.
 *
 * Reading the .res rather than the .rc is what makes this small: inside one,
 * a menu is the MENUITEMTEMPLATE tree LoadMenuIndirect takes, an accelerator
 * table is the ACCELTABLEENTRY rows CreateAcceleratorTable takes, a dialog is
 * the DLGTEMPLATE DialogBoxIndirectParam takes, and a string block is sixteen
 * counted UTF-16 strings. So each Load* here is a walk to the right entry and
 * a hand-over to the call the library already has -- which is what Windows
 * does behind the same names.
 *
 * A program with no resource script links the empty data below, every Load*
 * answers with nothing, and none of it costs anything.
 */

#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

/* What the build embedded, if anything: defined weakly and empty in
 * resource_none.c, and replaced by the real thing when a program links a
 * compiled .res. */
extern const unsigned char ween_app_resource_data[];
extern const unsigned int ween_app_resource_len;

/* Resource types, by the numbers winuser.h gives them. */
#define WEEN_RT_MENU 4
#define WEEN_RT_DIALOG 5
#define WEEN_RT_STRING 6
#define WEEN_RT_ACCELERATOR 9

/* ---- walking a .res ------------------------------------------------------ */

/* Every entry is: two sizes, a type, a name, five more words of housekeeping,
 * then the data -- and each entry starts on a four-byte boundary. Type and
 * name are either 0xFFFF followed by a number, or a zero-terminated UTF-16
 * string; nothing here is named, so a named one is stepped over. */
typedef struct {
    const unsigned char *p; /* where the next entry starts */
    const unsigned char *end;
} res_walk;

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned rd32(const unsigned char *p)
{
    return rd16(p) | (rd16(p + 2) << 16);
}

/* A type or a name: its number, or -1 when it is a string. */
static int res_ident(const unsigned char **p, const unsigned char *end)
{
    if (*p + 2 > end)
        return -1;
    if (rd16(*p) == 0xFFFF) {
        if (*p + 4 > end)
            return -1;
        int v = (int)rd16(*p + 2);
        *p += 4;
        return v;
    }
    while (*p + 2 <= end && rd16(*p) != 0)
        *p += 2;
    *p += 2;
    return -1;
}

/* The data of the first entry of this type and number, or NULL.
 *
 * The sizes come out of the file, so every one of them is checked against
 * what is left rather than added to a pointer first: a truncated or
 * malformed .res would otherwise walk past the end, or -- with a size near
 * the top of its range -- wrap and walk backwards for ever. Nothing here
 * trusts the file further than the room it can prove it has. */
static const unsigned char *res_find(int type, int name, unsigned *size)
{
    const unsigned char *p = ween_app_resource_data;
    const unsigned char *end = p + ween_app_resource_len;
    while ((size_t)(end - p) >= 8) {
        size_t left = (size_t)(end - p);
        unsigned data_size = rd32(p);
        unsigned head_size = rd32(p + 4);
        size_t step;
        const unsigned char *q = p + 8;
        if (head_size < 8 || head_size > left)
            return NULL;
        int t = res_ident(&q, end);
        int n = res_ident(&q, end);
        if (t == type && n == name && data_size <= left - head_size) {
            if (size)
                *size = data_size;
            return p + head_size;
        }
        /* both sizes are rounded up to the next four bytes, and a step that
         * does not move forward -- or does not fit -- ends the walk rather
         * than repeating it */
        if (data_size > (unsigned)-1 - head_size - 3)
            return NULL;
        step = ((size_t)head_size + data_size + 3) & ~(size_t)3;
        if (step == 0 || step > left)
            return NULL;
        p += step;
    }
    return NULL;
}

/* A resource name is either a string or a number smuggled in a pointer,
 * which is what MAKEINTRESOURCE does. Nothing here is stored by name: a
 * script that names a resource has that name in UTF-16 in the .res, and the
 * programs this serves all use numbers. */
static int res_number(LPCSTR name, int *ok)
{
    *ok = name && ((uintptr_t)name >> 16) == 0;
    return *ok ? (int)(uintptr_t)name : 0;
}

/* ---- what the words in a .res are made of -------------------------------- */

/* UTF-16 to the bytes the A-API talks in. What a resource script holds is
 * text a person typed; anything past Latin-1 has no ANSI spelling, and a
 * question mark is what WideCharToMultiByte puts there. */
static void wide_to_ansi(const unsigned char *w, int chars, char *out, int max)
{
    int n = 0;
    for (int i = 0; i < chars && n + 1 < max; i++) {
        unsigned c = rd16(w + i * 2);
        out[n++] = (char)(c < 0x100 ? c : '?');
    }
    if (max > 0)
        out[n] = 0;
}

/* A zero-terminated UTF-16 string: its length in characters, and past it. */
static int wide_len(const unsigned char *w, const unsigned char *end)
{
    int n = 0;
    while (w + 2 <= end && rd16(w) != 0) {
        w += 2;
        n++;
    }
    return n;
}

/* ---- the calls a program makes -------------------------------------------- */

/* Sixteen strings to a block, the block numbered id/16 + 1 and the string
 * sitting at id%16 inside it, each one a count and that many characters.
 * That is how a string table is stored, and why LoadString takes a number
 * rather than a name. */
int LoadStringA(HINSTANCE inst, UINT id, LPSTR buf, int max)
{
    unsigned size = 0;
    const unsigned char *p, *end;
    (void)inst;
    if (!buf || max <= 0)
        return 0;
    buf[0] = 0;
    p = res_find(WEEN_RT_STRING, (int)(id / 16) + 1, &size);
    if (!p)
        return 0;
    end = p + size;
    for (unsigned i = 0; i < 16; i++) {
        unsigned chars;
        if (p + 2 > end)
            return 0;
        chars = rd16(p);
        p += 2;
        if (i == (id % 16)) {
            if (p + chars * 2 > end)
                return 0;
            wide_to_ansi(p, (int)chars, buf, max);
            return (int)strlen(buf);
        }
        p += chars * 2;
    }
    return 0;
}

/* A menu template: a two-word header, then items. An item is its flags, its
 * id unless it opens a popup, and its text; MF_END closes the level it is
 * on. Built here through CreateMenu and AppendMenu, so a menu out of a
 * script and one an application builds by hand are the same menu after. */
#define MFR_END 0x0080
#define MFR_POPUP 0x0010

static const unsigned char *menu_level(HMENU into, const unsigned char *p,
                                       const unsigned char *end, int depth)
{
    char text[256];
    while (p + 2 <= end) {
        unsigned flags = rd16(p);
        unsigned id = 0;
        int chars;
        p += 2;
        if (!(flags & MFR_POPUP)) {
            if (p + 2 > end)
                return end;
            id = rd16(p);
            p += 2;
        }
        chars = wide_len(p, end);
        wide_to_ansi(p, chars, text, (int)sizeof text);
        p += (chars + 1) * 2;
        if (flags & MFR_POPUP) {
            HMENU sub = CreatePopupMenu();
            if (sub && depth < 8) {
                AppendMenuA(into, MF_POPUP, (UINT_PTR)sub, text);
                p = menu_level(sub, p, end, depth + 1);
            }
        } else {
            /* A separator has no text and no id; win32 spells it in the
             * flags and AppendMenu wants MF_SEPARATOR. */
            unsigned mf = flags & ~(unsigned)MFR_END;
            AppendMenuA(into, mf, (UINT_PTR)id, chars ? text : NULL);
        }
        if (flags & MFR_END)
            break;
    }
    return p;
}

/* A template an application hands over itself. There is no length with it --
 * win32 takes the same template on the same terms and reads until the last
 * item says it is the last -- so the walk is bounded by a size no smaller
 * than any real menu rather than by anything the caller said. The path that
 * reads our own bytes is LoadMenuA below, and that one knows the real size
 * and passes it. */
HMENU LoadMenuIndirectA(const void *tmpl)
{
    const unsigned char *p = tmpl;
    HMENU menu;
    if (!p)
        return NULL;
    menu = CreateMenu();
    if (!menu)
        return NULL;
    /* MENUHEADER: a version and the length of what follows it, both zero for
     * the plain kind, which is the only kind rc writes for a MENU. */
    menu_level(menu, p + 4 + rd16(p + 2), p + 0x10000, 0);
    return menu;
}

HMENU LoadMenuA(HINSTANCE inst, LPCSTR name)
{
    unsigned size = 0;
    int ok, id = res_number(name, &ok);
    const unsigned char *p;
    HMENU menu;
    (void)inst;
    if (!ok)
        return NULL;
    p = res_find(WEEN_RT_MENU, id, &size);
    if (!p || size < 4)
        return NULL;
    menu = CreateMenu();
    if (!menu)
        return NULL;
    menu_level(menu, p + 4 + rd16(p + 2), p + size, 0);
    return menu;
}

/* An accelerator table: eight bytes an entry, the last one flagged. The rows
 * go to CreateAcceleratorTable, which is what an application would have
 * called with a table of its own. */
HACCEL LoadAcceleratorsA(HINSTANCE inst, LPCSTR name)
{
    unsigned size = 0;
    int ok, id = res_number(name, &ok);
    const unsigned char *p;
    ACCEL rows[256];
    int n = 0;
    (void)inst;
    if (!ok)
        return NULL;
    p = res_find(WEEN_RT_ACCELERATOR, id, &size);
    if (!p)
        return NULL;
    while ((unsigned)(n + 1) * 8 <= size && n < (int)(sizeof rows / sizeof rows[0])) {
        unsigned flags = rd16(p + n * 8);
        rows[n].fVirt = (BYTE)(flags & 0x7F);
        rows[n].key = (WORD)rd16(p + n * 8 + 2);
        rows[n].cmd = (WORD)rd16(p + n * 8 + 4);
        n++;
        if (flags & 0x80)
            break;
    }
    return n ? CreateAcceleratorTableA(rows, n) : NULL;
}

/* A dialog resource is the template itself, so both of these are a lookup
 * and the call the program could have made with a template of its own. */
INT_PTR DialogBoxParamA(HINSTANCE inst, LPCSTR name, HWND owner, DLGPROC proc,
                        LPARAM param)
{
    int ok, id = res_number(name, &ok);
    const unsigned char *p = ok ? res_find(WEEN_RT_DIALOG, id, NULL) : NULL;
    if (!p)
        return -1;
    return DialogBoxIndirectParamA(inst, (LPCDLGTEMPLATEA)p, owner, proc, param);
}

HWND CreateDialogParamA(HINSTANCE inst, LPCSTR name, HWND owner, DLGPROC proc,
                        LPARAM param)
{
    int ok, id = res_number(name, &ok);
    const unsigned char *p = ok ? res_find(WEEN_RT_DIALOG, id, NULL) : NULL;
    if (!p)
        return NULL;
    return CreateDialogIndirectParamA(inst, (LPCDLGTEMPLATEA)p, owner, proc,
                                      param);
}

/* An icon in a .res is a picture in a format this has no decoder for -- a
 * DIB, or a PNG in the newer ones. A program that asks for its own gets
 * nothing rather than a wrong one, which is what a window with no class icon
 * already draws. */
HICON LoadIconA(HINSTANCE inst, LPCSTR name)
{
    (void)inst;
    (void)name;
    return NULL;
}
