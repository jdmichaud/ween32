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

/* A .ico file.
 *
 * It is a directory of images at different sizes and colour depths; the one
 * nearest the size asked for is taken. Each image is a BITMAPINFOHEADER whose
 * height counts double, because two bitmaps are stacked in it: the colours,
 * then a one-bit mask saying which pixels are drawn. That mask is why an icon
 * is not just a bitmap, and why one can be put over any background.
 */
static int ico_read_image(const unsigned char *p, size_t len, ween_gdiobj *out)
{
#define RD16(o) ((unsigned)p[o] | ((unsigned)p[(o) + 1] << 8))
#define RD32(o) (RD16(o) | ((unsigned long)RD16((o) + 2) << 16))
    if (len < 40)
        return 0;
    unsigned long hdr = RD32(0);
    int w = (int)RD32(4);
    int h = (int)RD32(8) / 2; /* colours and mask, stacked */
    int bpp = (int)RD16(14);
    unsigned long compression = RD32(16);
    unsigned long used = RD32(32);
    if (hdr < 40 || w <= 0 || h <= 0 || compression != 0)
        return 0;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)
        return 0;

    size_t pal_entries = bpp <= 8 ? (used ? used : (size_t)1u << bpp) : 0;
    size_t pal = hdr;
    size_t bits = pal + pal_entries * 4;
    int xor_stride = ((w * bpp + 31) / 32) * 4;
    int and_stride = ((w + 31) / 32) * 4;
    if (bits + (size_t)xor_stride * h + (size_t)and_stride * h > len)
        return 0;

    if (!ween_surface_init(&out->bitmap, w, h))
        return 0;
    out->mask = calloc((size_t)w * h, 1);
    if (!out->mask) {
        ween_surface_free(&out->bitmap);
        return 0;
    }
    out->kind = WEEN_OBJ_ICON;

    for (int row = 0; row < h; row++) {
        int y = h - 1 - row; /* stored bottom-up */
        const unsigned char *xr = p + bits + (size_t)row * xor_stride;
        const unsigned char *ar = p + bits + (size_t)xor_stride * h +
                                  (size_t)row * and_stride;
        for (int x = 0; x < w; x++) {
            unsigned idx = 0, r, g, b;
            if (bpp == 32 || bpp == 24) {
                const unsigned char *q = xr + (size_t)x * (bpp / 8);
                b = q[0];
                g = q[1];
                r = q[2];
            } else {
                if (bpp == 8)
                    idx = xr[x];
                else if (bpp == 4)
                    idx = (x & 1) ? (xr[x / 2] & 0xf) : (xr[x / 2] >> 4);
                else
                    idx = (xr[x / 8] >> (7 - (x % 8))) & 1;
                const unsigned char *q = p + pal + idx * 4;
                b = q[0];
                g = q[1];
                r = q[2];
            }
            out->bitmap.px[(size_t)y * w + x] = WEEN_RGBX(r, g, b);
            /* the AND mask is 1 where the background shows through */
            out->mask[(size_t)y * w + x] =
                (unsigned char)(((ar[x / 8] >> (7 - (x % 8))) & 1) == 0);
        }
    }
    return 1;
#undef RD16
#undef RD32
}

static HICON load_ico(const char *path, int cx, int cy)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    unsigned char dir[6];
    if (fread(dir, 1, 6, f) != 6 || dir[0] || dir[1] || dir[2] != 1) {
        fclose(f);
        return NULL;
    }
    int count = dir[4] | (dir[5] << 8);
    if (count <= 0 || count > 64) {
        fclose(f);
        return NULL;
    }
    unsigned char *ent = malloc((size_t)count * 16);
    if (!ent || fread(ent, 1, (size_t)count * 16, f) != (size_t)count * 16) {
        free(ent);
        fclose(f);
        return NULL;
    }
    /* the entry nearest the size asked for; 0 means "whatever is first" */
    int best = 0, best_d = 1 << 20;
    for (int i = 0; i < count; i++) {
        int w = ent[i * 16] ? ent[i * 16] : 256;
        int d = cx > 0 ? (w > cx ? w - cx : cx - w) : 0;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    (void)cy;
    unsigned long size = (unsigned long)ent[best * 16 + 8] |
                         ((unsigned long)ent[best * 16 + 9] << 8) |
                         ((unsigned long)ent[best * 16 + 10] << 16) |
                         ((unsigned long)ent[best * 16 + 11] << 24);
    unsigned long off = (unsigned long)ent[best * 16 + 12] |
                        ((unsigned long)ent[best * 16 + 13] << 8) |
                        ((unsigned long)ent[best * 16 + 14] << 16) |
                        ((unsigned long)ent[best * 16 + 15] << 24);
    free(ent);
    unsigned char *blob = malloc(size ? size : 1);
    ween_gdiobj *icon = calloc(1, sizeof(*icon));
    if (!blob || !icon || fseek(f, (long)off, SEEK_SET) != 0 ||
        fread(blob, 1, size, f) != size || !ico_read_image(blob, size, icon)) {
        free(blob);
        free(icon);
        fclose(f);
        return NULL;
    }
    free(blob);
    fclose(f);
    return (HICON)icon;
}

HANDLE LoadImageA(HINSTANCE inst, LPCSTR name, UINT type, int cx, int cy,
                  UINT flags)
{
    (void)inst;
    /* Only a file on disk: there are no resources to load from, ween32 having
     * no .exe to hold them. */
    if (!(flags & LR_LOADFROMFILE) || !name)
        return NULL;
    if (type == IMAGE_ICON)
        return load_ico(name, cx, cy);
    if (type != IMAGE_BITMAP)
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

/* An icon brings its own mask, which is the whole reason it is not a bitmap:
 * it goes into the list a view draws from without needing a colour picked out
 * as transparent. */
int ImageList_AddIcon(HIMAGELIST il, HICON icon)
{
    ween_gdiobj *ic = (ween_gdiobj *)icon;
    if (!il || !ic || ic->kind != WEEN_OBJ_ICON)
        return -1;
    if (!ween_imagelist_reserve(il, il->count + 1))
        return -1;
    size_t base = (size_t)il->count * il->cx * il->cy;
    for (int y = 0; y < il->cy; y++) {
        for (int x = 0; x < il->cx; x++) {
            size_t at = base + (size_t)y * il->cx + x;
            int inside = x < ic->bitmap.w && y < ic->bitmap.h;
            size_t from = (size_t)y * ic->bitmap.w + x;
            il->px[at] = inside ? ic->bitmap.px[from] : 0;
            il->mask[at] = (unsigned char)(inside && ic->mask[from]);
        }
    }
    return il->count++;
}

void DestroyIcon(HICON icon)
{
    ween_gdiobj *ic = (ween_gdiobj *)icon;
    if (!ic || ic->kind != WEEN_OBJ_ICON)
        return;
    ween_surface_free(&ic->bitmap);
    free(ic->mask);
    free(ic);
}

BOOL DrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy,
                UINT frame, HBRUSH flicker, UINT flags)
{
    ween_gdiobj *ic = (ween_gdiobj *)icon;
    (void)cx;
    (void)cy;
    (void)frame;
    (void)flicker;
    (void)flags;
    if (!dc || !ic || ic->kind != WEEN_OBJ_ICON)
        return FALSE;
    for (int iy = 0; iy < ic->bitmap.h; iy++)
        for (int ix = 0; ix < ic->bitmap.w; ix++)
            if (ic->mask[(size_t)iy * ic->bitmap.w + ix])
                ween_surface_pixel(dc->s, dc->org_x + x + ix,
                                   dc->org_y + y + iy,
                                   ic->bitmap.px[(size_t)iy * ic->bitmap.w + ix]);
    return TRUE;
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

/* The same shape in one colour, which is what greying one takes: win32 draws
 * a dead image as its silhouette in white a pixel down and to the right, then
 * again in shadow on the spot. */
void ween_imagelist_draw_mono(HIMAGELIST il, int index, ween_surface *s, int x,
                              int y, ween_color c)
{
    if (!il || index < 0 || index >= il->count || !s)
        return;
    size_t base = (size_t)index * il->cx * il->cy;
    for (int iy = 0; iy < il->cy; iy++) {
        for (int ix = 0; ix < il->cx; ix++) {
            size_t at = base + (size_t)iy * il->cx + ix;
            if (il->mask[at])
                ween_surface_pixel(s, x + ix, y + iy, c);
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
