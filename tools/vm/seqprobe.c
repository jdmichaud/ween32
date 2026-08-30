/* Replay a sequence against the real riched20 and dump the document.
 *
 * The machine half of the differential test. `tests/replay_test` runs the
 * same sequence against ween32 and writes the same dump; the two are diffed.
 * **That turns "ours agrees with our model of a rich edit" into "ours agrees
 * with WordPad"**, which is the project's premise and what jd asked for.
 *
 *     Z:\seqprobe.exe "type:97:0,bold:1:0,type:98:0" Z:\dump.txt
 *     Z:\seqprobe.exe @Z:\seq.txt Z:\dump.txt
 *
 * **Almost nothing happens here, and that is the design.** The language, the
 * parser, the executor and the serialiser are all in `tools/vm/replay.h` and
 * `tools/vm/dump.h`, compiled from the same source on both sides. What is
 * left is a window, a file, and the loop between them -- because every line
 * of behaviour written *here* would be a second implementation of the
 * contract, and a disagreement between two implementations of a contract
 * looks exactly like a finding about the editor.
 *
 * The control is 400x200 with no `EM_SETRECT`, which is part of the contract
 * rather than a detail: `wrapprobe.c` measured that a control with a
 * formatting rectangle and one without wrap differently, so two sides
 * disagreeing about it would report a difference on every wrapping sequence.
 *
 *   zig cc -target x86-windows-gnu -fno-sanitize=undefined -c \
 *          -o seqprobe.obj seqprobe.c
 *   zig cc -target x86-windows-gnu -nostdlib -Wl,--subsystem,windows \
 *          -Wl,--entry,WinMainCRTStartup -o seqprobe.exe seqprobe.obj \
 *          -lkernel32 -luser32 -lgdi32
 *   tools/vm/pe2k.py seqprobe.exe
 */
#include <windows.h>
#include <richedit.h>

#include "guestcrt.h"
#include "replay.h"
#include "dump.h"

#define MAX_STEPS 512

static void pump(int ms)
{
    MSG m;
    DWORD end = GetTickCount() + (DWORD)ms;
    while (GetTickCount() < end) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(2);
    }
}

/* argv without a CRT. Two arguments: the sequence -- or `@path` to read one
 * from a file -- and where the dump goes. */
static void args_of(char *one, char *two, int cap)
{
    char *p = GetCommandLineA();
    int seen = 0, q, n;
    one[0] = two[0] = 0;
    for (;;) {
        while (*p == ' ') p++;
        if (!*p) return;
        q = 0;
        if (*p == '"') { q = 1; p++; }
        {
            char *dst = seen == 1 ? one : two;
            n = 0;
            while (*p && (q ? *p != '"' : *p != ' ')) {
                if (seen >= 1 && n < cap - 1) dst[n++] = *p;
                p++;
            }
            if (seen >= 1) dst[n] = 0;
        }
        if (*p) p++;
        if (++seen > 2) return;
    }
}

/* A sequence file, flattened into the one line `parse` expects.
 *
 * **`parse` parses a sequence; turning a file into one belongs here**, which
 * is the only thing in the harness that reads files. Leaving comments to
 * `parse` would put file handling in a header that both sides share and only
 * one side needs.
 *
 * Found by running it rather than by reading it: the files in `tools/vm/seq` carry a
 * `#` header saying what each scenario is for -- `type:97:0,type:98:0` does
 * not say `abc` to a reader -- and the first `@file` run answered
 * `unknown operation "# The arming bo"` and exited 3. The error path worked
 * perfectly; the input did not.
 *
 * Lines are joined with a comma rather than concatenated, so a sequence split
 * across two lines cannot silently weld `undo:0:0` to `type:97:0` and become
 * a different sequence than the one written down. Doubled commas are then
 * dropped, which makes a trailing comma harmless. */
static int read_file(const char *path, char *out, int cap)
{
    static char raw[8192];
    DWORD got = 0;
    int i, n = 0, sol = 1, skip = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ReadFile(h, raw, sizeof raw - 1, &got, NULL);
    CloseHandle(h);
    raw[got] = 0;
    for (i = 0; raw[i] && n < cap - 2; i++) {
        char c = raw[i];
        if (c == '\n' || c == '\r') {
            if (!sol && n && out[n - 1] != ',')
                out[n++] = ',';
            sol = 1;
            skip = 0;
            continue;
        }
        if (sol && (c == ' ' || c == '\t'))
            continue;
        if (sol && c == '#')
            skip = 1;
        sol = 0;
        if (skip || c == ' ' || c == '\t')
            continue;
        out[n++] = c;
    }
    while (n && out[n - 1] == ',')
        n--;
    out[n] = 0;
    /* `,,` cannot arise from the joining above, but can from a file that
     * already ended a line with one; removed rather than left for `parse` to
     * read as an empty operation name. */
    {
        int r = 0, w = 0;
        while (out[r]) {
            if (out[r] == ',' && w && out[w - 1] == ',') { r++; continue; }
            out[w++] = out[r++];
        }
        out[w] = 0;
    }
    return 1;
}

static void probe_main(void)
{
    static char a1[4096], a2[512], text[4096];
    static struct rp_step seq[MAX_STEPS];
    WNDCLASSA wc;
    HWND host, re;
    const char *src = a1;
    int n, i;

    args_of(a1, a2, sizeof a2);

    /* `@path` reads the sequence from a file, because a command line long
     * enough for a generated sequence is a command line the shell will
     * mangle -- and the shrunk sequences worth running are the long ones. */
    if (a1[0] == '@') {
        if (!read_file(a1 + 1, text, sizeof text))
            ExitProcess(1);
        src = text;
    }

    LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "seqprobe";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "seqprobe", "seqprobe", WS_OVERLAPPEDWINDOW, 40,
                           40, 480, 320, NULL, NULL, wc.hInstance, NULL);

    g_out = CreateFileA(a2[0] ? a2 : "Z:\\dump.txt", GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, 0, NULL);
    if (g_out == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    re = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_VSCROLL,
                         0, 0, 400, 200, host, NULL, wc.hInstance, NULL);
    if (!re) {
        fprintf(GUEST_STREAM, "!! the control would not be created\n");
        ExitProcess(2);
    }
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    pump(400);

    n = parse(src, seq, MAX_STEPS);
    if (n < 0) {
        /* parse() already said which word. **Exit 3 rather than dumping
         * anyway**: a sequence one side executed and the other refused would
         * produce a diff that reads as a finding about the editor. */
        ExitProcess(3);
    }
    for (i = 0; i < n; i++) {
        step(re, &seq[i]);
        pump(20);
    }
    pump(200);
    dump_open(GUEST_STREAM, re);
    CloseHandle(g_out);
    ExitProcess(0);
}

void WinMainCRTStartup(void) { probe_main(); }
