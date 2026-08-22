#include <windows.h>
#include <stdio.h>
int main(void)
{
    HDC dc = GetDC(NULL);
    HFONT f = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0,
                          0, DEFAULT_QUALITY, 0, "Tahoma");
    int w[256];
    FILE *out = fopen("wineadv.txt", "w");
    SelectObject(dc, f);
    GetCharWidth32A(dc, 0, 255, w);
    for (int c = 32; c < 256; c++)
        fprintf(out, "%d %d\n", c, w[c]);
    fclose(out);
    return 0;
}
