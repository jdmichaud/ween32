/* Replay a sequence against riched20 and dump the document it produced.
 *
 * jd: *"I still see problems with the editor. Test it extensively... Make sure
 * the monkey testing is testing different scenarios, with interaction between
 * all those features."*
 *
 * alice's reading of that is the one this file serves: **our oracle has been
 * our own model of a rich edit.** The monkey checks ween32 against what we
 * believe riched20 does, and the machine gets asked one question at a time,
 * after somebody already suspects something. Nothing compares a *sequence* of
 * operations against the real control. This is the machine end of that:
 *
 *     one sequence -> riched20 here  -> dump
 *                  -> ween32 there   -> dump      -> diff
 *
 * **The dump is a value, not a picture.** Comparing pixels would drown every
 * real finding under two faces against the machine's seven; a document is
 * text, formatting, selection and line breaks, and those compare exactly.
 *
 *   Z:\seqprobe.exe Z:\seq.txt Z:\dump.txt
 *
 * ---
 *
 * **The order of the dump is load-bearing and not cosmetic.** Reading a run's
 * format means selecting it, and riched20 **discards an armed insertion
 * format when the caret moves** -- undoprobe.c lost an entire finding to that
 * this evening, reporting "arming does not take" when what had happened was
 * that looking cleared it. So the two things a walk would destroy are read
 * before the walk begins:
 *
 *     1  SEL   the live selection
 *     2  ARM   what the next typed character would carry
 *     3  RUN   the character formatting, which moves the selection
 *     4  PARA  the paragraph formatting, likewise
 *     5  LINE  where the control broke the text
 *
 * **`ARM` is in the dump because it is state that no rendering shows.** A
 * sequence ending in `bold 1` leaves a control that looks identical to one
 * that does not and behaves differently on the next keystroke, and that
 * difference is precisely the class of bug jd has been finding by hand.
 *
 * **Runs and paragraphs are ranges, not per character.** Per character is the
 * same information fifty times over, and a disagreement about where a run
 * *begins* is a finding that a range dump states in one line and a
 * per-character dump buries in fifty identical ones.
 */
#include <windows.h>
#include <richedit.h>

void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    while (n--)
        *a++ = *b++;
    return d;
}

void *__stack_chk_guard = (void *)0x0bad57ac;

void __stack_chk_fail(void) { ExitProcess(3); }

static HANDLE out_file;
static char buf[4096];

static void emit(const char *s)
{
    DWORD n;
    WriteFile(out_file, s, (DWORD)lstrlenA(s), &n, NULL);
}

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

/* ---- the document's length, the control's way ----------------------------
 *
 * **Not `WM_GETTEXTLENGTH` and not the length of what `WM_GETTEXT` hands
 * back.** A paragraph break is one character in the document and comes back
 * as two from a text fetch, so those two numbers differ by the number of
 * paragraphs -- and an index built on the wrong one walks off the end, where
 * riched20 answers from the armed insertion format rather than failing. bob
 * lost an evening's debugging to exactly that. `EM_EXSETSEL` with `cpMax` of
 * -1 resolves to the real end and reports it back. */
static int doc_len(HWND re)
{
    CHARRANGE r, back;
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&back);
    r.cpMin = 0;
    r.cpMax = -1;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&r);
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&back);
    return (int)r.cpMax;
}

static void sel_set(HWND re, int a, int b)
{
    CHARRANGE r;
    r.cpMin = a;
    r.cpMax = b;
    SendMessageA(re, EM_EXSETSEL, 0, (LPARAM)&r);
}

static void fmt_at(HWND re, int a, int b, CHARFORMATA *cf)
{
    sel_set(re, a, b);
    memset(cf, 0, sizeof *cf);
    cf->cbSize = sizeof *cf;
    SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)cf);
}

static void para_at(HWND re, int a, PARAFORMAT *pf)
{
    sel_set(re, a, a);
    memset(pf, 0, sizeof *pf);
    pf->cbSize = sizeof *pf;
    SendMessageA(re, EM_GETPARAFORMAT, 0, (LPARAM)pf);
}

static int fmt_same(const CHARFORMATA *a, const CHARFORMATA *b)
{
    int i;
    if ((a->dwEffects & (CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE)) !=
        (b->dwEffects & (CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE)))
        return 0;
    if (a->yHeight != b->yHeight)
        return 0;
    for (i = 0; i < LF_FACESIZE; i++) {
        if (a->szFaceName[i] != b->szFaceName[i])
            return 0;
        if (!a->szFaceName[i])
            break;
    }
    return 1;
}

static int para_same(const PARAFORMAT *a, const PARAFORMAT *b)
{
    return a->wAlignment == b->wAlignment &&
           a->dxStartIndent == b->dxStartIndent &&
           a->dxOffset == b->dxOffset &&
           a->dxRightIndent == b->dxRightIndent &&
           a->wNumbering == b->wNumbering;
}

static void write_fmt(const char *tag, int from, int to, const CHARFORMATA *cf)
{
    wsprintfA(buf, "%s %d %d b=%d i=%d u=%d face=%s size=%d\r\n", tag, from, to,
              (cf->dwEffects & CFE_BOLD) ? 1 : 0,
              (cf->dwEffects & CFE_ITALIC) ? 1 : 0,
              (cf->dwEffects & CFE_UNDERLINE) ? 1 : 0,
              cf->szFaceName[0] ? cf->szFaceName : "?", (int)cf->yHeight);
    emit(buf);
}

static void dump(HWND re)
{
    static char text[8192];
    int len = doc_len(re);
    int n, i, at;
    CHARRANGE sel;
    CHARFORMATA arm, cur, next;
    PARAFORMAT pcur, pnext;

    /* 1 and 2 first: both are destroyed by the walks below. */
    SendMessageA(re, EM_EXGETSEL, 0, (LPARAM)&sel);
    memset(&arm, 0, sizeof arm);
    arm.cbSize = sizeof arm;
    SendMessageA(re, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&arm);

    n = (int)SendMessageA(re, WM_GETTEXT, (WPARAM)sizeof text - 1,
                          (LPARAM)text);
    if (n < 0)
        n = 0;
    text[n] = 0;
    wsprintfA(buf, "TEXT %d ", len);
    emit(buf);
    for (i = 0; i < n; i++) {
        char one[8];
        if (text[i] == '\r') {
            one[0] = '\\'; one[1] = 'r'; one[2] = 0;
        } else if (text[i] == '\n') {
            one[0] = '\\'; one[1] = 'n'; one[2] = 0;
        } else if (text[i] == '\\') {
            one[0] = '\\'; one[1] = '\\'; one[2] = 0;
        } else {
            one[0] = text[i]; one[1] = 0;
        }
        emit(one);
    }
    emit("\r\n");

    wsprintfA(buf, "SEL %d %d\r\n", (int)sel.cpMin, (int)sel.cpMax);
    emit(buf);
    write_fmt("ARM", (int)sel.cpMin, (int)sel.cpMax, &arm);

    /* 3: character formatting, coalesced into ranges. */
    if (len > 0) {
        int start = 0;
        fmt_at(re, 0, 1, &cur);
        for (at = 1; at < len; at++) {
            fmt_at(re, at, at + 1, &next);
            if (!fmt_same(&cur, &next)) {
                write_fmt("RUN", start, at, &cur);
                start = at;
                cur = next;
            }
        }
        write_fmt("RUN", start, len, &cur);
    } else {
        emit("RUN none\r\n");
    }

    /* 4: paragraph formatting, likewise. A paragraph's format is asked at a
     * caret rather than over a range, because a selection spanning two
     * paragraphs answers with what they share and not with either. */
    {
        int start = 0;
        para_at(re, 0, &pcur);
        for (at = 1; at <= len; at++) {
            para_at(re, at, &pnext);
            if (!para_same(&pcur, &pnext)) {
                wsprintfA(buf, "PARA %d %d align=%d start=%d off=%d right=%d "
                               "num=%d\r\n", start, at, (int)pcur.wAlignment,
                          (int)pcur.dxStartIndent, (int)pcur.dxOffset,
                          (int)pcur.dxRightIndent, (int)pcur.wNumbering);
                emit(buf);
                start = at;
                pcur = pnext;
            }
        }
        wsprintfA(buf, "PARA %d %d align=%d start=%d off=%d right=%d num=%d\r\n",
                  start, len, (int)pcur.wAlignment, (int)pcur.dxStartIndent,
                  (int)pcur.dxOffset, (int)pcur.dxRightIndent,
                  (int)pcur.wNumbering);
        emit(buf);
    }

    /* 5: where the control chose to break. */
    {
        int lines = (int)SendMessageA(re, EM_GETLINECOUNT, 0, 0);
        for (i = 0; i < lines && i < 200; i++) {
            wsprintfA(buf, "LINE %d %d\r\n", i,
                      (int)SendMessageA(re, EM_LINEINDEX, (WPARAM)i, 0));
            emit(buf);
        }
    }

    /* The selection is put back, so a dump taken mid-sequence does not change
     * what the rest of the sequence does. Nothing here uses that yet; it
     * costs one message and removes a whole class of surprise later. */
    sel_set(re, (int)sel.cpMin, (int)sel.cpMax);
}

/* ---- the sequence -------------------------------------------------------- */

static int eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int num(const char **p)
{
    int v = 0, neg = 0;
    while (**p == ' ') (*p)++;
    if (**p == '-') { neg = 1; (*p)++; }
    while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; }
    return neg ? -v : v;
}

static void set_char(HWND re, DWORD mask, DWORD effects, const char *face,
                     int twips)
{
    CHARFORMATA cf;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = mask;
    cf.dwEffects = effects;
    if (face) {
        int i = 0;
        while (face[i] && i < LF_FACESIZE - 1) { cf.szFaceName[i] = face[i]; i++; }
        cf.szFaceName[i] = 0;
    }
    cf.yHeight = twips;
    SendMessageA(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void set_para(HWND re, DWORD mask, int align, int start, int off,
                     int right, int num_)
{
    PARAFORMAT pf;
    memset(&pf, 0, sizeof pf);
    pf.cbSize = sizeof pf;
    pf.dwMask = mask;
    pf.wAlignment = (WORD)align;
    pf.dxStartIndent = start;
    pf.dxOffset = off;
    pf.dxRightIndent = right;
    pf.wNumbering = (WORD)num_;
    SendMessageA(re, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

/* Does `line` begin with `word` followed by a space or its end? */
static int starts(const char *line, const char *word, const char **rest)
{
    int i = 0;
    while (word[i]) {
        if (line[i] != word[i])
            return 0;
        i++;
    }
    if (line[i] && line[i] != ' ')
        return 0;
    *rest = line[i] ? line + i + 1 : line + i;
    return 1;
}

static int vk_of(const char *name)
{
    if (starts(name, "enter", &name)) return VK_RETURN;
    if (starts(name, "back", &name))  return VK_BACK;
    if (starts(name, "del", &name))   return VK_DELETE;
    if (starts(name, "home", &name))  return VK_HOME;
    if (starts(name, "end", &name))   return VK_END;
    if (starts(name, "left", &name))  return VK_LEFT;
    if (starts(name, "right", &name)) return VK_RIGHT;
    if (starts(name, "up", &name))    return VK_UP;
    if (starts(name, "down", &name))  return VK_DOWN;
    if (starts(name, "tab", &name))   return VK_TAB;
    return 0;
}

static int has(const char *s, const char *what)
{
    int i;
    while (*s) {
        for (i = 0; what[i] && s[i] == what[i]; i++)
            ;
        if (!what[i])
            return 1;
        s++;
    }
    return 0;
}

/* One line of the sequence. Answers 0 on a word this build does not know,
 * which is reported rather than skipped: **a sequence the two sides execute
 * differently because one of them ignored a line is the worst failure a
 * differential test has**, because the diff then looks like a finding about
 * the editor. Both ends must refuse the same unknown word loudly. */
static int do_op(HWND re, const char *line)
{
    const char *r;

    if (starts(line, "type", &r)) {
        /* The rest of the line verbatim, spaces included -- deliberately not
         * the headless script's `_`-for-space convention, which cost an hour
         * when `tempfile.mkdtemp` put an underscore in a path. A sequence
         * file has one op per line and needs no escape for a space. */
        while (*r)
            SendMessageA(re, WM_CHAR, (WPARAM)(unsigned char)*r++, 1);
        return 1;
    }
    if (starts(line, "key", &r)) {
        int vk = vk_of(r);
        int ctrl = has(r, "+ctrl"), shift = has(r, "+shift");
        if (!vk)
            return 0;
        if (ctrl)  SendMessageA(re, WM_KEYDOWN, VK_CONTROL, 0);
        if (shift) SendMessageA(re, WM_KEYDOWN, VK_SHIFT, 0);
        SendMessageA(re, WM_KEYDOWN, (WPARAM)vk, 1);
        SendMessageA(re, WM_KEYUP, (WPARAM)vk, 1);
        if (shift) SendMessageA(re, WM_KEYUP, VK_SHIFT, 0);
        if (ctrl)  SendMessageA(re, WM_KEYUP, VK_CONTROL, 0);
        return 1;
    }
    if (starts(line, "sel", &r)) {
        int a = num(&r), b = num(&r);
        sel_set(re, a, b);
        return 1;
    }
    if (starts(line, "bold", &r)) {
        set_char(re, CFM_BOLD, num(&r) ? CFE_BOLD : 0, NULL, 0);
        return 1;
    }
    if (starts(line, "ital", &r)) {
        set_char(re, CFM_ITALIC, num(&r) ? CFE_ITALIC : 0, NULL, 0);
        return 1;
    }
    if (starts(line, "under", &r)) {
        set_char(re, CFM_UNDERLINE, num(&r) ? CFE_UNDERLINE : 0, NULL, 0);
        return 1;
    }
    if (starts(line, "face", &r)) {
        set_char(re, CFM_FACE, 0, r, 0);
        return 1;
    }
    if (starts(line, "size", &r)) {
        set_char(re, CFM_SIZE, 0, NULL, num(&r) * 20); /* points to twips */
        return 1;
    }
    if (starts(line, "align", &r)) {
        int a = *r == 'c' ? PFA_CENTER : *r == 'r' ? PFA_RIGHT : PFA_LEFT;
        set_para(re, PFM_ALIGNMENT, a, 0, 0, 0, 0);
        return 1;
    }
    if (starts(line, "indent", &r)) {
        int st = num(&r), of = num(&r), ri = num(&r);
        set_para(re, PFM_STARTINDENT | PFM_OFFSET | PFM_RIGHTINDENT, 0, st, of,
                 ri, 0);
        return 1;
    }
    if (starts(line, "bullet", &r)) {
        set_para(re, PFM_NUMBERING, 0, 0, 0, 0, num(&r) ? PFN_BULLET : 0);
        return 1;
    }
    if (starts(line, "copy", &r))  { SendMessageA(re, WM_COPY, 0, 0);  return 1; }
    if (starts(line, "cut", &r))   { SendMessageA(re, WM_CUT, 0, 0);   return 1; }
    if (starts(line, "paste", &r)) { SendMessageA(re, WM_PASTE, 0, 0); return 1; }
    if (starts(line, "undo", &r))  { SendMessageA(re, EM_UNDO, 0, 0);  return 1; }
    if (starts(line, "redo", &r))  { SendMessageA(re, EM_REDO, 0, 0);  return 1; }
    if (starts(line, "resize", &r)) {
        int w = num(&r), h = num(&r);
        SetWindowPos(re, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
        pump(120);
        return 1;
    }
    return 0;
}

static char *read_all(const char *path, int *out_len)
{
    static char data[1 << 16];
    DWORD got = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    ReadFile(h, data, sizeof data - 1, &got, NULL);
    CloseHandle(h);
    data[got] = 0;
    *out_len = (int)got;
    return data;
}

static void probe_main(void)
{
    static char argv1[512], argv2[512], line[1024];
    WNDCLASSA wc;
    HWND host, re;
    char *seq;
    int seq_len = 0, i, k, lineno = 0, unknown = 0;

    {   /* argv[1] and argv[2], no CRT */
        char *p = GetCommandLineA();
        int seen = 0, q, n;
        for (;;) {
            while (*p == ' ') p++;
            if (!*p) break;
            q = 0;
            if (*p == '"') { q = 1; p++; }
            n = 0;
            {
                char *dst = seen == 1 ? argv1 : argv2;
                while (*p && (q ? *p != '"' : *p != ' ')) {
                    if (n < 511) dst[n++] = *p;
                    p++;
                }
                dst[n] = 0;
            }
            if (*p) p++;
            if (++seen > 2) break;
        }
    }

    LoadLibraryA("riched20.dll");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "seqprobe";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "seqprobe", "seqprobe", WS_OVERLAPPEDWINDOW, 40,
                           40, 480, 320, NULL, NULL, wc.hInstance, NULL);

    out_file = CreateFileA(argv2[0] ? argv2 : "Z:\\dump.txt", GENERIC_WRITE, 0,
                           NULL, CREATE_ALWAYS, 0, NULL);
    if (out_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);

    /* **400x200 and no EM_SETRECT**, stated here because a wrapping width is
     * an input to half of what this measures and the other side must use the
     * same one. wrapprobe.c showed a bare control and one with a formatting
     * rectangle wrap differently; a differential test that did not fix this
     * would report that difference as an editor bug on every wrapping
     * sequence. */
    re = CreateWindowExA(0, RICHEDIT_CLASSA, "",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_VSCROLL,
                         0, 0, 400, 200, host, NULL, wc.hInstance, NULL);
    if (!re) {
        emit("!! the control would not be created\r\n");
        CloseHandle(out_file);
        ExitProcess(2);
    }
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    pump(400);

    seq = read_all(argv1[0] ? argv1 : "Z:\\seq.txt", &seq_len);
    if (!seq) {
        emit("!! no sequence file\r\n");
        CloseHandle(out_file);
        ExitProcess(1);
    }

    emit("# seqprobe, riched20\r\n");
    for (i = 0; i <= seq_len; i++) {
        if (i < seq_len && seq[i] != '\n' && seq[i] != '\r')
            continue;
        k = 0;
        /* the line just ended; copy it out without its terminator */
        {
            int j = i;
            while (j > 0 && seq[j - 1] != '\n' && seq[j - 1] != '\r')
                j--;
            while (j < i && k < 1023)
                line[k++] = seq[j++];
        }
        line[k] = 0;
        if (!k || line[0] == '#')
            continue;
        lineno++;
        if (!do_op(re, line)) {
            wsprintfA(buf, "!! line %d not understood: %s\r\n", lineno, line);
            emit(buf);
            unknown++;
        }
        pump(30);
    }
    pump(200);

    wsprintfA(buf, "# %d op(s), %d not understood\r\n", lineno, unknown);
    emit(buf);
    dump(re);
    CloseHandle(out_file);
    ExitProcess(unknown ? 4 : 0);
}

void WinMainCRTStartup(void) { probe_main(); }
