/* Replay a sequence against ween32's rich edit and dump the document.
 *
 *     tests/replay_test 'type:97:0,bold:1:0,type:98:0'   > ours.txt
 *
 * The other half of this runs the same sequence against a real RichEdit20W
 * on the guest and writes the same dump; the two are diffed. **That turns
 * "ours agrees with bob's model of a rich edit" into "ours agrees with
 * WordPad"**, which is the project's premise and what jd asked for.
 *
 * The language and the serialiser are in tools/vm/replay.h and
 * tools/vm/dump.h so that both sides compile the *same* code rather than two
 * readings of one description -- which is the mistake a written contract
 * invites, and it would blame the program for a disagreement between two
 * implementations of the contract.
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ween_internal.h"
#include <ween32.h>

#include "../tools/vm/replay.h"
#include "../tools/vm/dump.h"

static LRESULT CALLBACK host_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

int main(int argc, char **argv)
{
    WNDCLASSA wc;
    struct rp_step seq[4096];
    int n, i;
    /* `--emit <seed> <n>` prints a generated sequence and does not run it.
     * Generation lives here rather than in tests/monkey_test.c because the
     * monkey drives a mouse and this language cannot: a sequence with a drag
     * in it is not one riched20 can be asked to replay, and the point of a
     * generated sequence is that both sides run it. */
    if (argc >= 4 && !strcmp(argv[1], "--emit")) {
        struct rp_step out[4096];
        int want = atoi(argv[3]);
        if (want > 4096)
            want = 4096;
        rp_print(stdout, out,
                 rp_generate(out, want, (unsigned)strtoul(argv[2], NULL, 0)));
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: %s <sequence>\n"
                        "       %s --emit <seed> <steps>\n",
                argv[0], argv[0]);
        return 2;
    }
    setenv("WEEN32_DPI", "96", 1);
    ween_active_backend = ween_backend_headless();
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = host_proc;
    wc.lpszClassName = "replayhost";
    RegisterClassA(&wc);
    HWND host = CreateWindowExA(0, "replayhost", "h", WS_POPUP | WS_VISIBLE,
                                0, 0, 400, 300, NULL, NULL, NULL, NULL);
    HWND re = rp_create(host);
    if (!re) {
        fprintf(stderr, "no control\n");
        return 2;
    }

    /* `--glyphs` measures how wide the text is rather than where it breaks.
     * The same string, indices and message as tools/vm/glyphs.c, so the two
     * columns can be subtracted. Wrap off, so the client width is out of it
     * and `x` is pure cumulative advance. See that file for why. */
    if (!strcmp(argv[1], "--glyphs")) {
        static const char *t =
            "the quick brown fox jumps over the lazy dog and the heron "
            "waits here";
        RECT cr;
        int len;
        /* 1440 is arbitrary: Sam bisected every width from 1 to 15840 and
         * they turn wrapping off identically, so it only had to be non-zero.
         * Written as a number it reads like a measured twips-per-inch. */
        SendMessageA(re, EM_SETTARGETDEVICE, 0, 1440);
        SetWindowTextA(re, t);
        GetClientRect(re, &cr);
        printf("client %ld %ld\n", (long)(cr.right - cr.left),
               (long)(cr.bottom - cr.top));
        printf("lines %ld\n", (long)SendMessageA(re, EM_GETLINECOUNT, 0, 0));
        for (len = 0; t[len]; len++) {
        }
        {
            CHARFORMATA cf;
            memset(&cf, 0, sizeof cf);
            cf.cbSize = sizeof cf;
            SendMessageA(re, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
            printf("default face %s size %ld effects %08lx\n", cf.szFaceName,
                   (long)cf.yHeight, (unsigned long)cf.dwEffects);
            memset(&cf, 0, sizeof cf);
            cf.cbSize = sizeof cf;
            SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            printf("actual  face %s size %ld effects %08lx\n", cf.szFaceName,
                   (long)cf.yHeight, (unsigned long)cf.dwEffects);
        }
        for (i = 0; i <= len; i += 4) {
            POINTL pt;
            pt.x = 0;
            pt.y = 0;
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)i);
            printf("x %d %ld\n", i, (long)pt.x);
        }
        {
            /* A hundred w's, Sam's own unit: he read WordPad's longest line
             * as nMax 901 over a hundred of them, 9px each and exact, and a
             * probe asking for Arial 10 as 11. Same measurement here so the
             * three can be put in a row. */
            char ws[101];
            POINTL a, b;
            a.x = a.y = b.x = b.y = 0;
            for (i = 0; i < 100; i++)
                ws[i] = 'w';
            ws[100] = 0;
            SetWindowTextA(re, ws);
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&a, (LPARAM)0);
            SendMessageA(re, EM_POSFROMCHAR, (WPARAM)&b, (LPARAM)100);
            printf("wrun 100 %ld\n", (long)(b.x - a.x));
        }
        return 0;
    }

    n = parse(argv[1], seq, 4096);
    if (n < 0)
        return 2;
    for (i = 0; i < n; i++)
        step(re, &seq[i]);
    dump_open(stdout, re);
    return 0;
}
