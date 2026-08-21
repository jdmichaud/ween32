/* Image lists, and the bitmaps that go into them.
 *
 * COMCTL32's shape: a list holds any number of images, all the same size, and
 * a control refers to one by index. That is how a tree view or a list view
 * gets an icon beside a label without knowing anything about where the picture
 * came from.
 *
 * An image is a surface plus a transparency mask — one bit per pixel, because
 * that is what the classic shell had. Colour depth beyond that (alpha, hot
 * spots, overlays) is not something Windows 2000's comctl32 did either.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

struct ween_imagelist {
    int cx, cy;
    int count, cap;
    ween_color *px;      /* count images, cx*cy each, one after another */
    unsigned char *mask; /* 1 where the pixel is drawn, 0 where it is not */
};

/* ---- bitmaps -------------------------------------------------------------- */

HBITMAP CreateBitmap(int w, int h, UINT planes, UINT bpp, const void *bits)
{
    if (w <= 0 || h <= 0 || planes != 1 || (bpp != 24 && bpp != 32))
        return NULL;
    ween_gdiobj *b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->kind = WEEN_OBJ_BITMAP;
    if (!ween_surface_init(&b->bitmap, w, h)) {
        free(b);
        return NULL;
    }
    if (bits) {
        const unsigned char *src = bits;
        int stride = bpp / 8;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const unsigned char *p = src + ((size_t)y * w + x) * stride;
                /* the caller's rows are B,G,R as every win32 DIB is */
                b->bitmap.px[(size_t)y * w + x] =
                    WEEN_RGBX(p[2], p[1], p[0]);
            }
        }
    }
    return b;
}

/* A .bmp, which is the only image format ween32 reads — and the one it
 * writes, so a headless screenshot can be loaded straight back in. 24- and
 * 32-bit uncompressed only, which is what BI_RGB means in practice. */
static HBITMAP load_bmp(const char *path)
{
    unsigned char hdr[54];
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) || hdr[0] != 'B' ||
        hdr[1] != 'M') {
        fclose(f);
        return NULL;
    }
#define RD32(o) ((unsigned)hdr[o] | ((unsigned)hdr[(o) + 1] << 8) |            \
                 ((unsigned)hdr[(o) + 2] << 16) | ((unsigned)hdr[(o) + 3] << 24))
    unsigned offset = RD32(10);
    int w = (int)RD32(18);
    int h = (int)RD32(22);
    int bpp = hdr[28] | (hdr[29] << 8);
    unsigned compression = RD32(30);
#undef RD32
    int flip = h > 0; /* a positive height means the rows run bottom-up */
    if (h < 0)
        h = -h;
    if (w <= 0 || h <= 0 || compression != 0 || (bpp != 24 && bpp != 32)) {
        fclose(f);
        return NULL;
    }

    ween_gdiobj *b = calloc(1, sizeof(*b));
    if (!b || !ween_surface_init(&b->bitmap, w, h)) {
        free(b);
        fclose(f);
        return NULL;
    }
    b->kind = WEEN_OBJ_BITMAP;

    int stride = ((w * bpp / 8) + 3) & ~3; /* rows are DWORD-aligned */
    unsigned char *row = malloc((size_t)stride);
    if (!row || fseek(f, (long)offset, SEEK_SET) != 0) {
        free(row);
        ween_surface_free(&b->bitmap);
        free(b);
        fclose(f);
        return NULL;
    }
    for (int i = 0; i < h; i++) {
        int y = flip ? h - 1 - i : i;
        if (fread(row, 1, (size_t)stride, f) != (size_t)stride)
            break;
        for (int x = 0; x < w; x++) {
            const unsigned char *p = row + (size_t)x * (bpp / 8);
            b->bitmap.px[(size_t)y * w + x] = WEEN_RGBX(p[2], p[1], p[0]);
        }
    }
    free(row);
    fclose(f);
    return b;
}

HANDLE LoadImageA(HINSTANCE inst, LPCSTR name, UINT type, int cx, int cy,
                  UINT flags)
{
    (void)inst;
    (void)cx;
    (void)cy;
    /* Only a file on disk: there are no resources to load from, ween32 having
     * no .exe to hold them. */
    if (!(flags & LR_LOADFROMFILE) || type != IMAGE_BITMAP || !name)
        return NULL;
    return load_bmp(name);
}

/* ---- the list ------------------------------------------------------------- */

HIMAGELIST ImageList_Create(int cx, int cy, UINT flags, int initial, int grow)
{
    (void)flags;
    (void)grow;
    if (cx <= 0 || cy <= 0)
        return NULL;
    HIMAGELIST il = calloc(1, sizeof(*il));
    if (!il)
        return NULL;
    il->cx = cx;
    il->cy = cy;
    if (initial > 0 && !ween_imagelist_reserve(il, initial)) {
        free(il);
        return NULL;
    }
    return il;
}

int ween_imagelist_reserve(HIMAGELIST il, int count)
{
    if (count <= il->cap)
        return 1;
    int cap = il->cap ? il->cap : 4;
    while (cap < count)
        cap *= 2;
    size_t pixels = (size_t)cap * il->cx * il->cy;
    ween_color *px = realloc(il->px, pixels * sizeof(*px));
    unsigned char *mask = realloc(il->mask, pixels);
    if (px)
        il->px = px;
    if (mask)
        il->mask = mask;
    if (!px || !mask)
        return 0;
    il->cap = cap;
    return 1;
}

BOOL ImageList_Destroy(HIMAGELIST il)
{
    if (!il)
        return FALSE;
    free(il->px);
    free(il->mask);
    free(il);
    return TRUE;
}

int ImageList_GetImageCount(HIMAGELIST il)
{
    return il ? il->count : 0;
}

BOOL ImageList_GetIconSize(HIMAGELIST il, int *cx, int *cy)
{
    if (!il)
        return FALSE;
    if (cx)
        *cx = il->cx;
    if (cy)
        *cy = il->cy;
    return TRUE;
}

/* Copy one image in, taking everything of the given colour as transparent.
 * CLR_NONE keeps the lot. */
int ImageList_AddMasked(HIMAGELIST il, HBITMAP bmp, COLORREF transparent)
{
    if (!il || !bmp || bmp->kind != WEEN_OBJ_BITMAP)
        return -1;
    if (!ween_imagelist_reserve(il, il->count + 1))
        return -1;
    ween_color clear = ween_cr_to_px(transparent);
    size_t base = (size_t)il->count * il->cx * il->cy;
    for (int y = 0; y < il->cy; y++) {
        for (int x = 0; x < il->cx; x++) {
            size_t at = base + (size_t)y * il->cx + x;
            int inside = x < bmp->bitmap.w && y < bmp->bitmap.h;
            ween_color c =
                inside ? bmp->bitmap.px[(size_t)y * bmp->bitmap.w + x] : 0;
            il->px[at] = c;
            il->mask[at] = (unsigned char)(inside &&
                                           (transparent == CLR_NONE ||
                                            c != clear));
        }
    }
    return il->count++;
}

int ImageList_Add(HIMAGELIST il, HBITMAP bmp, HBITMAP mask)
{
    int index = ImageList_AddMasked(il, bmp, CLR_NONE);
    if (index < 0 || !mask || mask->kind != WEEN_OBJ_BITMAP)
        return index;
    /* a separate mask bitmap: black means draw, white means leave alone, as
     * every win32 icon has done since 3.0 */
    size_t base = (size_t)index * il->cx * il->cy;
    for (int y = 0; y < il->cy && y < mask->bitmap.h; y++)
        for (int x = 0; x < il->cx && x < mask->bitmap.w; x++)
            il->mask[base + (size_t)y * il->cx + x] =
                (unsigned char)(mask->bitmap.px[(size_t)y * mask->bitmap.w + x]
                                == 0);
    return index;
}

/* ---- drawing -------------------------------------------------------------- */

void ween_imagelist_draw(HIMAGELIST il, int index, ween_surface *s, int x,
                         int y)
{
    if (!il || index < 0 || index >= il->count || !s)
        return;
    size_t base = (size_t)index * il->cx * il->cy;
    for (int iy = 0; iy < il->cy; iy++) {
        for (int ix = 0; ix < il->cx; ix++) {
            size_t at = base + (size_t)iy * il->cx + ix;
            if (il->mask[at])
                ween_surface_pixel(s, x + ix, y + iy, il->px[at]);
        }
    }
}

BOOL ImageList_Draw(HIMAGELIST il, int index, HDC dc, int x, int y, UINT style)
{
    (void)style; /* ILD_NORMAL and ILD_TRANSPARENT are the same here: the mask
                  * decides, and there is no background to fill against */
    if (!dc || !il)
        return FALSE;
    ween_imagelist_draw(il, index, dc->s, dc->org_x + x, dc->org_y + y);
    return TRUE;
}
