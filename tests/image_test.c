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
                                  WS_CHILD | WS_VISIBLE, 10, 10, 200, 100, w,
                                  NULL, NULL, NULL);
        CHECK(tv != NULL, "a tree view to hang it on");
        SendMessageA(tv, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)il);

        TVINSERTSTRUCTA is;
        memset(&is, 0, sizeof(is));
        is.hParent = TVI_ROOT;
        is.item.mask = TVIF_TEXT | TVIF_IMAGE;
        is.item.pszText = (char *)"With an icon";
        is.item.iImage = 0;
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
        DestroyWindow(w);
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
