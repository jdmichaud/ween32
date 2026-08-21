/* Software surface and 2D primitives (port of telemouse's framebuffer.zig).
 * Everything ween32 shows is drawn into one of these, then blitted by a
 * backend — rendering is pixel-identical on every platform. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

int ween_surface_init(ween_surface *s, int w, int h)
{
    if (w <= 0 || h <= 0)
        return 0;
    s->px = calloc((size_t)w * (size_t)h, sizeof(ween_color));
    if (!s->px)
        return 0;
    s->w = w;
    s->h = h;
    ween_surface_clip(s, 0, 0, w, h);
    return 1;
}

void ween_surface_free(ween_surface *s)
{
    free(s->px);
    s->px = NULL;
    s->w = s->h = 0;
}

/* Every primitive draws through the clip rectangle: a window paints into its
 * own area of the shared surface and no further, which is what stops a long
 * tree-view label running out over its neighbour. */
void ween_surface_clip(ween_surface *s, int x, int y, int w, int h)
{
    int x1 = x + w, y1 = y + h;
    s->clip_x = x < 0 ? 0 : x;
    s->clip_y = y < 0 ? 0 : y;
    s->clip_r = x1 > s->w ? s->w : x1;
    s->clip_b = y1 > s->h ? s->h : y1;
}

void ween_surface_get_clip(const ween_surface *s, RECT *r)
{
    r->left = s->clip_x;
    r->top = s->clip_y;
    r->right = s->clip_r;
    r->bottom = s->clip_b;
}

void ween_surface_clear(ween_surface *s, ween_color c)
{
    for (long i = 0; i < (long)s->w * s->h; i++)
        s->px[i] = c;
}

void ween_surface_pixel(ween_surface *s, int x, int y, ween_color c)
{
    if (x < s->clip_x || y < s->clip_y || x >= s->clip_r || y >= s->clip_b)
        return;
    s->px[(long)y * s->w + x] = c;
}

void ween_surface_fill(ween_surface *s, int x, int y, int w, int h, ween_color c)
{
    int x0 = x < s->clip_x ? s->clip_x : x;
    int y0 = y < s->clip_y ? s->clip_y : y;
    int x1 = x + w > s->clip_r ? s->clip_r : x + w;
    int y1 = y + h > s->clip_b ? s->clip_b : y + h;
    for (int yy = y0; yy < y1; yy++) {
        ween_color *row = s->px + (long)yy * s->w;
        for (int xx = x0; xx < x1; xx++)
            row[xx] = c;
    }
}

void ween_surface_hline(ween_surface *s, int x, int y, int w, ween_color c)
{
    ween_surface_fill(s, x, y, w, 1, c);
}

void ween_surface_vline(ween_surface *s, int x, int y, int h, ween_color c)
{
    ween_surface_fill(s, x, y, 1, h, c);
}

void ween_surface_rect(ween_surface *s, int x, int y, int w, int h, ween_color c)
{
    ween_surface_hline(s, x, y, w, c);
    ween_surface_hline(s, x, y + h - 1, w, c);
    ween_surface_vline(s, x, y, h, c);
    ween_surface_vline(s, x + w - 1, y, h, c);
}

/* Nearest-neighbour integer magnification: the crisp HiDPI path (render at
 * 96 dpi, pixel-double on the way out). dst must be sized src * zoom. */
void ween_surface_zoom_into(ween_surface *dst, const ween_surface *src, int zoom)
{
    for (int y = 0; y < src->h; y++) {
        /* expand one source row into the first destination row... */
        ween_color *d0 = dst->px + (long)y * zoom * dst->w;
        const ween_color *sp = src->px + (long)y * src->w;
        for (int x = 0; x < src->w; x++) {
            ween_color c = sp[x];
            for (int i = 0; i < zoom; i++)
                d0[x * zoom + i] = c;
        }
        /* ...then replicate it */
        for (int i = 1; i < zoom; i++)
            memcpy(dst->px + ((long)y * zoom + i) * dst->w, d0,
                   (size_t)dst->w * sizeof(ween_color));
    }
}

static void put_le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

int ween_surface_write_bmp(const ween_surface *s, const char *path)
{
    size_t w = (size_t)s->w, h = (size_t)s->h;
    size_t stride = (w * 3 + 3) & ~(size_t)3;
    size_t total = 54 + stride * h;
    unsigned char *out = calloc(1, total);
    if (!out)
        return 0;

    out[0] = 'B';
    out[1] = 'M';
    put_le32(out + 2, (uint32_t)total);
    put_le32(out + 10, 54); /* pixel data offset */
    put_le32(out + 14, 40); /* BITMAPINFOHEADER */
    put_le32(out + 18, (uint32_t)s->w);
    put_le32(out + 22, (uint32_t)s->h);
    out[26] = 1;  /* planes */
    out[28] = 24; /* bpp */
    put_le32(out + 34, (uint32_t)(stride * h));

    /* Bottom-up rows, BGR. */
    for (size_t y = 0; y < h; y++) {
        const ween_color *src = s->px + (h - 1 - y) * w;
        unsigned char *dst = out + 54 + y * stride;
        for (size_t x = 0; x < w; x++) {
            dst[x * 3] = (unsigned char)src[x];
            dst[x * 3 + 1] = (unsigned char)(src[x] >> 8);
            dst[x * 3 + 2] = (unsigned char)(src[x] >> 16);
        }
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(out);
        return 0;
    }
    size_t n = fwrite(out, 1, total, f);
    fclose(f);
    free(out);
    return n == total;
}
