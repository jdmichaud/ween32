/* win32_dlg.h — build a DLGTEMPLATE in memory from a declarative control table.
 *
 * This is app-side utility code (pure win32: it uses only <ween32.h>/<windows.h>
 * types), the well-known idiom for creating dialogs at run time without a
 * compiled .rc resource. The bytes it produces are a standard dialog template,
 * fed to CreateDialogIndirectParam — so it works identically over ween32 and
 * over the real dialog manager on Windows.
 *
 * You declare each control's rectangle in DIALOG UNITS; the dialog manager does
 * the DLU->pixel mapping. No pixel arithmetic in the app.
 */

#ifndef WIN32_DLG_H
#define WIN32_DLG_H

/* predefined control-class atoms (winuser.h) */
#define ATOM_BUTTON 0x0080
#define ATOM_EDIT 0x0081
#define ATOM_STATIC 0x0082

typedef struct {
    DWORD style;
    short x, y, cx, cy; /* dialog units */
    WORD id;
    WORD cls;              /* ATOM_BUTTON / ATOM_STATIC / ... */
    const char *text;
    const char *clsname;   /* or a class by name, for one with no ordinal:
                            * a template may hold any registered class */
    DWORD exstyle;         /* WS_EX_CLIENTEDGE for a field border, and so on */
} dlg_item;

typedef struct {
    unsigned char *p;
    unsigned char *end;
} dlg_buf;

static void db_w(dlg_buf *b, WORD v)
{
    if (b->p + 2 <= b->end) {
        b->p[0] = (unsigned char)v;
        b->p[1] = (unsigned char)(v >> 8);
    }
    b->p += 2;
}

static void db_d(dlg_buf *b, DWORD v)
{
    db_w(b, (WORD)v);
    db_w(b, (WORD)(v >> 16));
}

/* a null-terminated UTF-16 string (ASCII widened), as templates require */
static void db_wsz(dlg_buf *b, const char *s)
{
    if (s)
        while (*s)
            db_w(b, (unsigned char)*s++);
    db_w(b, 0);
}

static void db_align(dlg_buf *b)
{
    while (((UINT_PTR)b->p & 3) && b->p < b->end)
        *b->p++ = 0;
}

/* Assemble a modeless dialog template into `buf` (which must be DWORD-aligned);
 * returns the byte length used. `face` and `points` are the font DS_SETFONT
 * promises: "MS Shell Dlg" is the older stand-in the system resolves to the
 * dialog face, "MS Shell Dlg 2" the one it resolves to the shell's own — which
 * is the difference between a dialog that looks like Folder Options and one
 * that looks like a Properties page. */
static UINT_PTR build_dialog_template_font(void *buf, UINT_PTR cap, DWORD style,
                                           short cx, short cy,
                                           const char *title,
                                           const dlg_item *items, int n,
                                           const char *face, int points)
{
    dlg_buf b;
    b.p = (unsigned char *)buf;
    b.end = b.p + cap;

    /* DLGTEMPLATE header */
    db_d(&b, style);
    db_d(&b, 0);         /* dwExtendedStyle */
    db_w(&b, (WORD)n);   /* cdit */
    db_w(&b, 0);         /* x */
    db_w(&b, 0);         /* y */
    db_w(&b, (WORD)cx);
    db_w(&b, (WORD)cy);
    db_w(&b, 0);         /* menu: none */
    db_w(&b, 0);         /* window class: default (dialog) */
    db_wsz(&b, title);
    /* DS_SETFONT promises a point size and a face name here, and the reader
     * skips exactly that much before the first control. Saying so without
     * writing them slides every control out of step. */
    if (style & DS_SETFONT) {
        db_w(&b, (WORD)points);
        db_wsz(&b, face);
    }

    /* DLGITEMTEMPLATE for each control */
    for (int i = 0; i < n; i++) {
        db_align(&b); /* items start on a DWORD boundary */
        db_d(&b, items[i].style);
        db_d(&b, items[i].exstyle);
        db_w(&b, (WORD)items[i].x);
        db_w(&b, (WORD)items[i].y);
        db_w(&b, (WORD)items[i].cx);
        db_w(&b, (WORD)items[i].cy);
        db_w(&b, items[i].id);
        if (items[i].clsname) {
            db_wsz(&b, items[i].clsname);
        } else {
            db_w(&b, 0xFFFF); /* class given as an ordinal atom ... */
            db_w(&b, items[i].cls);
        }
        db_wsz(&b, items[i].text);
        db_w(&b, 0); /* no creation data */
    }
    return (UINT_PTR)(b.p - (unsigned char *)buf);
}

/* The same, in the face every dialog here but one is set in. */
static UINT_PTR build_dialog_template(void *buf, UINT_PTR cap, DWORD style,
                                      short cx, short cy, const char *title,
                                      const dlg_item *items, int n)
{
    return build_dialog_template_font(buf, cap, style, cx, cy, title, items, n,
                                      "MS Shell Dlg", 8);
}

#endif /* WIN32_DLG_H */
