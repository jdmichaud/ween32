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
    WORD cls;           /* ATOM_BUTTON / ATOM_STATIC */
    const char *text;
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
 * returns the byte length used. */
static UINT_PTR build_dialog_template(void *buf, UINT_PTR cap, DWORD style,
                                      short cx, short cy, const char *title,
                                      const dlg_item *items, int n)
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

    /* DLGITEMTEMPLATE for each control */
    for (int i = 0; i < n; i++) {
        db_align(&b); /* items start on a DWORD boundary */
        db_d(&b, items[i].style);
        db_d(&b, 0); /* dwExtendedStyle */
        db_w(&b, (WORD)items[i].x);
        db_w(&b, (WORD)items[i].y);
        db_w(&b, (WORD)items[i].cx);
        db_w(&b, (WORD)items[i].cy);
        db_w(&b, items[i].id);
        db_w(&b, 0xFFFF); /* class given as an ordinal atom ... */
        db_w(&b, items[i].cls);
        db_wsz(&b, items[i].text);
        db_w(&b, 0); /* no creation data */
    }
    return (UINT_PTR)(b.p - (unsigned char *)buf);
}

#endif /* WIN32_DLG_H */
