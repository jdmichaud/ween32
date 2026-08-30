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
    /* **WordPad's own style word and its startup default.** The control
     * under test has to be the one jd drives, or a difference could be the
     * configuration rather than the program. */
    HWND re = CreateWindowExA(0x00000210, RICHEDIT_CLASSA, "",
                              (DWORD)0x550081C4, 0, 0, 280, 160, host, NULL,
                              NULL, NULL);
    CHARFORMATA d;
    memset(&d, 0, sizeof d);
    d.cbSize = sizeof d;
    d.dwMask = CFM_FACE | CFM_SIZE;
    d.yHeight = 200;
    strcpy(d.szFaceName, "Arial");
    SendMessageA(re, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&d);
    SetFocus(re);

    n = parse(argv[1], seq, 4096);
    if (n < 0)
        return 2;
    for (i = 0; i < n; i++)
        step(re, &seq[i]);
    dump_open(stdout, re);
    return 0;
}
