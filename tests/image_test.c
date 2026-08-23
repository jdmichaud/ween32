/* Image lists: making a bitmap, loading one from disk, masking a colour out
 * of it, and a tree view drawing what it names by index. */

#define _POSIX_C_SOURCE 200112L /* setenv */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ween_internal.h"

static int g_failures = 0;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("ok   %s\n", name);                                         \
        } else {                                                               \
            printf("FAIL %s\n", name);                                         \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

/* An 8x8 image: a red square on magenta, so the magenta can be masked out. */
static unsigned char *make_bits(void)
{
    static unsigned char bits[8 * 8 * 3];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            unsigned char *p = bits + ((size_t)y * 8 + x) * 3;
            int inside = x >= 2 && x < 6 && y >= 2 && y < 6;
            p[0] = inside ? 0 : 0xff;   /* B */
            p[1] = 0;                   /* G */
            p[2] = 0xff;                /* R */
        }
    }
    return bits;
}

/* The same square in green, so a test can tell which of two images was the
 * one drawn. */
static unsigned char *make_green_bits(void)
{
    static unsigned char bits[8 * 8 * 3];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            unsigned char *p = bits + ((size_t)y * 8 + x) * 3;
            int inside = x >= 2 && x < 6 && y >= 2 && y < 6;
            p[0] = inside ? 0 : 0xff;      /* B */
            p[1] = inside ? 0xff : 0;      /* G */
            p[2] = inside ? 0 : 0xff;      /* R */
        }
    }
    return bits;
}

static LRESULT CALLBACK host_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

int main(void)
{
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();

    HBITMAP bmp = CreateBitmap(8, 8, 1, 24, make_bits());
    CHECK(bmp != NULL, "a bitmap built from pixels in memory");

    HIMAGELIST il = ImageList_Create(8, 8, ILC_MASK, 2, 2);
    CHECK(il != NULL, "an image list to put it in");
    CHECK(ImageList_GetImageCount(il) == 0, "which starts empty");

    int index = ImageList_AddMasked(il, bmp, RGB(0xff, 0, 0xff));
    CHECK(index == 0, "the image went in at zero");
    CHECK(ImageList_GetImageCount(il) == 1, "and the list counts one");

    int cx = 0, cy = 0;
    ImageList_GetIconSize(il, &cx, &cy);
    CHECK(cx == 8 && cy == 8, "the list reports the size it was made with");

    /* Draw it on a surface of its own: the masked colour must not land. */
    {
        ween_surface s;
        ween_surface_init(&s, 12, 12);
        ween_surface_clear(&s, WEEN_WHITE);
        ween_imagelist_draw(il, 0, &s, 2, 2);
        int red = 0, magenta = 0, white = 0;
        for (int i = 0; i < 12 * 12; i++) {
            ween_color c = s.px[i] & 0xffffff;
            if (c == WEEN_RGBX(0xff, 0, 0))
                red++;
            else if (c == WEEN_RGBX(0xff, 0, 0xff))
                magenta++;
            else if (c == WEEN_WHITE)
                white++;
        }
        CHECK(red == 16, "the opaque part of the image was drawn");
        CHECK(magenta == 0, "the masked colour was not");
        CHECK(white == 12 * 12 - 16, "and the rest was left alone");
        ween_surface_free(&s);
    }

    /* A .bmp from disk — the same format the headless backend writes, so a
     * screenshot can be read straight back in. */
    {
        ween_surface s;
        ween_surface_init(&s, 4, 3);
        ween_surface_clear(&s, WEEN_RGBX(0x11, 0x22, 0x33));
        ween_surface_pixel(&s, 0, 0, WEEN_RGBX(0xff, 0xee, 0xdd));
        CHECK(ween_surface_write_bmp(&s, "/tmp/ween_image_test.bmp"),
              "a bmp was written to load back");
        ween_surface_free(&s);

        HBITMAP loaded = (HBITMAP)LoadImageA(NULL, "/tmp/ween_image_test.bmp",
                                             IMAGE_BITMAP, 0, 0,
                                             LR_LOADFROMFILE);
        CHECK(loaded != NULL, "and LoadImageA read it back");
        if (loaded) {
            CHECK(loaded->bitmap.w == 4 && loaded->bitmap.h == 3,
                  "at the size it was written");
            CHECK((loaded->bitmap.px[0] & 0xffffff) == WEEN_RGBX(0xff, 0xee, 0xdd),
                  "with its top-left pixel where it belongs, not flipped");
            DeleteObject(loaded);
        }
        CHECK(LoadImageA(NULL, "/tmp/ween_no_such_file.bmp", IMAGE_BITMAP, 0, 0,
                         LR_LOADFROMFILE) == NULL,
              "a file that is not there loads as nothing");
    }

    /* A .ico. The file is built here rather than shipped, so the decoder is
     * tested against bytes this test controls: one 4x4 image, four bits a
     * pixel, with a mask that makes a single corner transparent. */
    {
        /* a 4x4 icon, four bits a pixel: red where the mask says draw */
        unsigned char ico[] = {
            0, 0, 1, 0, 1, 0,             /* ICONDIR: one image */
            4, 4, 16, 0, 1, 0, 4, 0,      /* 4x4, 16 colours, 4bpp */
            0, 0, 0, 0, 22, 0, 0, 0,      /* size filled in below, offset 22 */
            40, 0, 0, 0,                  /* BITMAPINFOHEADER */
            4, 0, 0, 0, 8, 0, 0, 0,       /* width 4, height 8 (doubled) */
            1, 0, 4, 0, 0, 0, 0, 0,       /* 1 plane, 4bpp, BI_RGB */
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0,               /* ...to a full 40-byte header */
        };
        unsigned char pal[16 * 4];
        unsigned char xor_bits[4 * 4]; /* 4 rows, 4 bytes each (padded) */
        unsigned char and_bits[4 * 4];
        memset(pal, 0, sizeof(pal));
        pal[4] = 0;    /* colour 1: blue=0 */
        pal[5] = 0;    /* green=0 */
        pal[6] = 0xff; /* red=255 */
        memset(xor_bits, 0x11, sizeof(xor_bits)); /* every pixel colour 1 */
        memset(and_bits, 0, sizeof(and_bits)); /* 0 = opaque */
        /* Rows are stored bottom-up, so the last one in the file is the top
         * row of the image: this makes the top-left pixel transparent. */
        and_bits[3 * 4] = 0x80;

        FILE *f = fopen("/tmp/ween_icon_test.ico", "wb");
        CHECK(f != NULL, "somewhere to write a test icon");
        if (f) {
            unsigned long isize = 40 + sizeof(pal) + sizeof(xor_bits) +
                                  sizeof(and_bits);
            ico[14] = (unsigned char)(isize & 0xff);
            ico[15] = (unsigned char)((isize >> 8) & 0xff);
            fwrite(ico, 1, sizeof(ico), f);
            fwrite(pal, 1, sizeof(pal), f);
            fwrite(xor_bits, 1, sizeof(xor_bits), f);
            fwrite(and_bits, 1, sizeof(and_bits), f);
            fclose(f);
        }

        HICON icon = (HICON)LoadImageA(NULL, "/tmp/ween_icon_test.ico",
                                       IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        CHECK(icon != NULL, "and LoadImageA reads an icon out of it");
        if (icon) {
            HIMAGELIST ic = ImageList_Create(4, 4, ILC_MASK, 1, 1);
            CHECK(ImageList_AddIcon(ic, icon) == 0,
                  "an icon goes into an image list with its own mask");
            ween_surface s;
            ween_surface_init(&s, 4, 4);
            ween_surface_clear(&s, WEEN_WHITE);
            ween_imagelist_draw(ic, 0, &s, 0, 0);
            int red = 0, white = 0;
            for (int i = 0; i < 16; i++) {
                if ((s.px[i] & 0xffffff) == WEEN_RGBX(0xff, 0, 0))
                    red++;
                else if ((s.px[i] & 0xffffff) == WEEN_WHITE)
                    white++;
            }
            CHECK(red == 15 && white == 1,
                  "and the pixel its mask calls transparent is not drawn");
            CHECK((s.px[0] & 0xffffff) == WEEN_WHITE,
                  "which is the one the mask named, in the right corner");
            ween_surface_free(&s);
            ImageList_Destroy(ic);
            DestroyIcon(icon);
        }
    }

    /* A tree view draws the image an item names, and moves its label over. */
    {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = host_proc;
        wc.lpszClassName = "weenimage";
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        RegisterClassA(&wc);
        HWND w = CreateWindowExA(0, "weenimage", "images",
                                 WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 240,
                                 160, NULL, NULL, NULL, NULL);
        HWND tv = CreateWindowExA(WS_EX_CLIENTEDGE, WC_TREEVIEWA, "",
                                  WS_CHILD | WS_VISIBLE | TVS_HASLINES |
                                      TVS_HASBUTTONS | TVS_LINESATROOT,
                                  10, 10, 200, 100, w, NULL, NULL, NULL);
        CHECK(tv != NULL, "a tree view to hang it on");
        SendMessageA(tv, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)il);

        /* a second image, so the selected one can be told from the other */
        HBITMAP green = CreateBitmap(8, 8, 1, 24, make_green_bits());
        int green_index = ImageList_AddMasked(il, green, RGB(0xff, 0, 0xff));
        DeleteObject(green);

        TVINSERTSTRUCTA is;
        memset(&is, 0, sizeof(is));
        is.hParent = TVI_ROOT;
        is.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        is.item.pszText = (char *)"With an icon";
        is.item.iImage = 0;
        is.item.iSelectedImage = green_index;
        HTREEITEM item = (HTREEITEM)SendMessageA(tv, TVM_INSERTITEMA, 0,
                                                 (LPARAM)&is);
        CHECK(item != NULL, "and an item that names the image");

        InvalidateRect(w, NULL, TRUE);
        ween_flush_paint();
        const ween_surface *s = ween_headless_surface();
        int red = 0;
        for (int i = 0; s && i < s->w * s->h; i++)
            if ((s->px[i] & 0xffffff) == WEEN_RGBX(0xff, 0, 0))
                red++;
        CHECK(red == 16, "the icon was painted into the tree view");

        /* Selecting it swaps in the other image, which is how a shell shows
         * an open folder for the one you are looking at. The tree is given
         * the focus first, so the row it is on is highlighted. */
        SetFocus(tv);
        SendMessageA(tv, TVM_SELECTITEM, TVGN_CARET, (LPARAM)item);
        InvalidateRect(w, NULL, TRUE);
        ween_flush_paint();
        s = ween_headless_surface();
        int green_px = 0;
        red = 0;
        /* A picked item's picture is drawn in its own colours over the
         * highlight: the machine washes nothing into it, in a tree or in a
         * list, so what is on screen is the pure green. */
        for (int i = 0; s && i < s->w * s->h; i++) {
            if ((s->px[i] & 0xffffff) == WEEN_RGBX(0, 0xff, 0))
                green_px++;
            else if ((s->px[i] & 0xffffff) == WEEN_RGBX(0xff, 0, 0))
                red++;
        }
        CHECK(green_px == 16 && red == 0,
              "and a selected item wears the image it named for that");
        DestroyWindow(w);
    }

    /* A window wears its class's icon in the caption, at the left of the
     * gradient — which already holds its start colour across exactly that
     * strip — and moves the title over to make room. WM_SETICON changes it. */
    {
        HICON icon = (HICON)LoadImageA(NULL, "/tmp/ween_icon_test.ico",
                                       IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = host_proc;
        wc.lpszClassName = "weencapicon";
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.hIcon = icon;
        RegisterClassA(&wc);
        HWND w = CreateWindowExA(0, "weencapicon", "Titled",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU |
                                     WS_VISIBLE,
                                 0, 0, 240, 80, NULL, NULL, NULL, NULL);
        ween_flush_paint();
        const ween_surface *s = ween_headless_surface();
        int left = 9999, count = 0;
        for (int y = 0; s && y < 24; y++)
            for (int x = 0; x < s->w; x++)
                if ((s->px[y * s->w + x] & 0xffffff) == WEEN_RGBX(0xff, 0, 0)) {
                    count++;
                    if (x < left)
                        left = x;
                }
        CHECK(count > 0, "a window draws its class icon in the caption");
        CHECK(left == WEEN_NC_FRAME + 2,
              "two pixels in from the frame, where the gradient makes room");
        CHECK(SendMessageA(w, WM_GETICON, ICON_SMALL, 0) ==
                  (LRESULT)(INT_PTR)icon,
              "and answers WM_GETICON with it");

        SendMessageA(w, WM_SETICON, ICON_SMALL, 0);
        InvalidateRect(w, NULL, TRUE);
        ween_flush_paint();
        s = ween_headless_surface();
        count = 0;
        for (int y = 0; s && y < 24; y++)
            for (int x = 0; x < s->w; x++)
                if ((s->px[y * s->w + x] & 0xffffff) == WEEN_RGBX(0xff, 0, 0))
                    count++;
        CHECK(count == 0, "and stops drawing one when WM_SETICON takes it away");
        DestroyWindow(w);
        DestroyIcon(icon);
    }

    ImageList_Destroy(il);
    DeleteObject(bmp);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("image_test: all passed\n");
    return 0;
}
