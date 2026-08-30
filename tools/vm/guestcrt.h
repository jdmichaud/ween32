/* The few C library functions `dump.h` needs, over Win32, for a guest probe.
 *
 * **A probe for the Windows 2000 guest cannot link the C library**, and that
 * is not a style choice. `zig cc -target x86-windows-gnu` with the CRT
 * produces a binary importing the Universal CRT:
 *
 *     api-ms-win-crt-{stdio,heap,string,runtime,math,environment}-l1-1-0.dll
 *
 * Windows 2000 has none of those, so it fails at load with nothing useful
 * said, and `pe2k.py` rewriting the subsystem version does not help -- the
 * imports are the problem, not the header. There is no older CRT to fall
 * back to in this toolchain either: `-lmsvcrt` answers *unable to find
 * dynamic system library 'msvcrt'* however it is asked. Hence `-nostdlib`
 * for every probe in this directory, and hence this file.
 *
 * **It exists so that `dump.h` stays the single serialiser.** Two readings of
 * a written contract is the mistake the shared header avoids; forking it for
 * the guest would reintroduce exactly that, one level down. The platform
 * difference belongs in one small file rather than in a second dump.
 *
 * Only what `dump.h` actually calls is here, and no more: an `fprintf` that
 * understands the five conversions it uses, and allocation over the process
 * heap.
 *
 * **These are definitions, not replacements, and `FILE` is left alone.** The
 * first attempt made them `static` and typedef'd `FILE` to a handle, which
 * does not compile: `windows.h` already pulls in `stdlib.h` and `string.h`,
 * so `malloc`, `free`, `strncpy` and `strcmp` are declared non-static before
 * this file is read, and redefining `FILE` would rewrite `stdio.h`'s own uses
 * of it. So the prototypes are the real ones and the output handle is a
 * global instead: `dump.h` writes to exactly one file, and the `FILE *` it
 * passes is never dereferenced here.
 */
#ifndef GUESTCRT_H
#define GUESTCRT_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Where everything written through `fprintf` goes. The probe sets it before
 * calling into `dump.h`; the `FILE *` argument is ignored. */
static HANDLE g_out = INVALID_HANDLE_VALUE;

static void g_write(const char *s, int n)
{
    DWORD w;
    if (g_out != INVALID_HANDLE_VALUE && n > 0)
        WriteFile(g_out, s, (DWORD)n, &w, NULL);
}

static int g_str(char *out, const char *s)
{
    int n = 0;
    while (s[n]) { out[n] = s[n]; n++; }
    return n;
}

/* Signed decimal is handled rather than assumed away: a negative indent is a
 * real value here -- `dxOffset` of -720 is WordPad's hanging bullet -- and
 * printing it unsigned would look like corruption in a dump. */
static int g_num(char *out, long v, int hex, int pad)
{
    char tmp[24];
    int n = 0, i = 0, neg = 0;
    unsigned long u;
    if (!hex && v < 0) { neg = 1; u = (unsigned long)(-v); }
    else u = (unsigned long)v;
    if (!u) tmp[n++] = '0';
    while (u) {
        int d = (int)(u % (hex ? 16u : 10u));
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        u /= (hex ? 16u : 10u);
    }
    while (n < pad) tmp[n++] = '0';
    if (neg) tmp[n++] = '-';
    while (n) out[i++] = tmp[--n];
    return i;
}

/* The five conversions `dump.h` uses: %d, %ld, %02x, %c, %s. **Anything else
 * is written through literally rather than guessed at**, so a format this
 * does not implement appears in the dump as itself and is obvious, rather
 * than silently producing a plausible wrong number. */
int fprintf(FILE *f, const char *fmt, ...)
{
    char out[1024];
    int n = 0;
    va_list ap;
    (void)f;
    va_start(ap, fmt);
    while (*fmt && n < (int)sizeof out - 32) {
        if (*fmt != '%') { out[n++] = *fmt++; continue; }
        fmt++;
        {
            int pad = 0, is_long = 0;
            while (*fmt >= '0' && *fmt <= '9') { pad = pad * 10 + (*fmt - '0'); fmt++; }
            if (*fmt == 'l') { is_long = 1; fmt++; }
            switch (*fmt) {
            case 'd': n += g_num(out + n, is_long ? va_arg(ap, long)
                                                  : (long)va_arg(ap, int), 0, pad);
                      break;
            case 'x': n += g_num(out + n, (long)va_arg(ap, unsigned), 1, pad);
                      break;
            case 'c': out[n++] = (char)va_arg(ap, int); break;
            case 's': n += g_str(out + n, va_arg(ap, const char *)); break;
            case '%': out[n++] = '%'; break;
            default:  out[n++] = '%'; out[n++] = *fmt; break;
            }
            if (*fmt) fmt++;
        }
    }
    va_end(ap);
    g_write(out, n);
    return n;
}

void *malloc(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n); }

void free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i]) { d[i] = s[i]; i++; }
    while (i < n) d[i++] = 0;
    return d;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)((unsigned char)*a) - (int)((unsigned char)*b);
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++)
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    while (n--) *a++ = *b++;
    return d;
}

/* `parse` reads its numbers with `strtol` and reports an unknown word to
 * `stderr`. Both need answering here rather than in `replay.h`: the header is
 * shared and correct as it stands, and this is the guest's problem.
 *
 * **`stderr` is redefined rather than implemented.** mingw expands it to
 * `__acrt_iob_func(2)`, which is a Universal CRT import and the whole thing
 * this file exists to avoid -- linking it back in for one error message would
 * undo the point. `fprintf` above ignores its stream argument, so a null one
 * is honest: there is one output handle and the message goes to it, where a
 * reader of the dump will see it. **An unknown operation must be loud, and a
 * line in the dump file is louder than a stderr nobody captures off a guest.**
 *
 * The value is 1 rather than 0 because mingw declares `fprintf`'s stream
 * non-null and a null one warns. **It is never dereferenced** -- by anything
 * here or in `dump.h` -- so any non-null value does; `GUEST_STREAM` is the
 * name so that a reader meets the intent rather than a bare cast.
 */
#define GUEST_STREAM ((FILE *)1)
#undef stderr
#define stderr GUEST_STREAM

long strtol(const char *s, char **end, int base)
{
    long v = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if (base == 10 || base == 0) {
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    } else {
        /* Not reached by anything here; refused rather than half-done, so a
         * future caller gets nothing back instead of a plausible wrong
         * number. */
        v = 0;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}

void *__stack_chk_guard = (void *)0x0bad57ac;
void __stack_chk_fail(void) { ExitProcess(3); }

#endif
