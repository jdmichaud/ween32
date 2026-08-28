/* KERNEL32's smaller corners: memory a handle stands for, the clock, and the
 * two text encodings.
 *
 * None of these is a window or a pixel, and all of them are things a win32
 * program reaches for without thinking: GlobalAlloc because the clipboard
 * will free it, GetLocalTime and GetDateFormat because the user's machine
 * decides how a date is written, MultiByteToWideChar because a file may be
 * UTF-16 even when the program itself is an ANSI one.
 */

#define _POSIX_C_SOURCE 200112L /* localtime_r */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ween_internal.h"

/* ---- memory --------------------------------------------------------------
 *
 * Windows keeps moveable blocks and hands out handles that have to be locked
 * before they can be touched; that mattered when memory was scarce enough to
 * be shuffled. Nothing here moves, so a block is its own handle and locking
 * it hands it back -- which is exactly what Windows does for the fixed kind,
 * and what GMEM_FIXED asks for anyway.
 *
 * The size is kept in front of the block, since GlobalSize has to answer and
 * free() will not say. */

typedef struct {
    size_t size;
    double align; /* so what follows is aligned for anything */
} mem_head;

static void *mem_alloc(UINT flags, size_t bytes)
{
    mem_head *h = malloc(sizeof(mem_head) + bytes);
    if (!h)
        return NULL;
    h->size = bytes;
    if (flags & GMEM_ZEROINIT)
        memset(h + 1, 0, bytes);
    return h + 1;
}

static void *mem_free(void *p)
{
    if (p)
        free((mem_head *)p - 1);
    return NULL;
}

static size_t mem_size(void *p)
{
    return p ? ((mem_head *)p - 1)->size : 0;
}

HGLOBAL GlobalAlloc(UINT flags, size_t bytes)
{
    return mem_alloc(flags, bytes);
}

void *GlobalLock(HGLOBAL mem)
{
    return mem;
}

/* Windows answers FALSE for a block whose lock count has reached zero, and a
 * fixed block never has one to begin with; a program checks GetLastError to
 * tell that from a failure, and nothing here can fail. */
BOOL GlobalUnlock(HGLOBAL mem)
{
    (void)mem;
    return FALSE;
}

HGLOBAL GlobalFree(HGLOBAL mem)
{
    return mem_free(mem);
}

size_t GlobalSize(HGLOBAL mem)
{
    return mem_size(mem);
}

HLOCAL LocalAlloc(UINT flags, size_t bytes)
{
    return mem_alloc(flags, bytes);
}

void *LocalLock(HLOCAL mem)
{
    return mem;
}

BOOL LocalUnlock(HLOCAL mem)
{
    (void)mem;
    return FALSE;
}

HLOCAL LocalFree(HLOCAL mem)
{
    return mem_free(mem);
}

size_t LocalSize(HLOCAL mem)
{
    return mem_size(mem);
}

/* ---- the clock ----------------------------------------------------------- */

static void from_tm(const struct tm *t, SYSTEMTIME *st)
{
    st->wYear = (WORD)(t->tm_year + 1900);
    st->wMonth = (WORD)(t->tm_mon + 1);
    st->wDayOfWeek = (WORD)t->tm_wday;
    st->wDay = (WORD)t->tm_mday;
    st->wHour = (WORD)t->tm_hour;
    st->wMinute = (WORD)t->tm_min;
    st->wSecond = (WORD)t->tm_sec;
    st->wMilliseconds = 0;
}

void GetLocalTime(SYSTEMTIME *st)
{
    time_t now = time(NULL);
    struct tm t;
    if (!st)
        return;
    memset(st, 0, sizeof *st);
    if (localtime_r(&now, &t))
        from_tm(&t, st);
}

void GetSystemTime(SYSTEMTIME *st)
{
    time_t now = time(NULL);
    struct tm t;
    if (!st)
        return;
    memset(st, 0, sizeof *st);
    if (gmtime_r(&now, &t))
        from_tm(&t, st);
}

/* ---- writing a date and a time -------------------------------------------
 *
 * How a date is spelled belongs to the user rather than to the program, which
 * is why win32 makes it a call rather than a printf. The locale here is the C
 * library's -- the same idea in the same place -- so %x and %X are the short
 * date and the time the user's machine is set to.
 *
 * A caller can also hand over a picture, and the fields understood are the
 * ones a program actually writes: yyyy, yy, MMMM, MMM, MM, M, dddd, ddd, dd,
 * d for a date, and HH, H, hh, h, mm, m, ss, s, tt, t for a time. Anything
 * else in the picture is copied through, and text inside single quotes is
 * copied without being read, both of which is what win32 does with it.
 */

static void to_tm(const SYSTEMTIME *st, struct tm *t)
{
    memset(t, 0, sizeof *t);
    t->tm_year = st->wYear - 1900;
    t->tm_mon = st->wMonth ? st->wMonth - 1 : 0;
    t->tm_mday = st->wDay;
    t->tm_hour = st->wHour;
    t->tm_min = st->wMinute;
    t->tm_sec = st->wSecond;
    t->tm_wday = st->wDayOfWeek;
    t->tm_isdst = -1;
}

/* How many of the same letter start here. */
static int run_of(const char *p)
{
    int n = 1;
    while (p[n] == p[0])
        n++;
    return n;
}

static int put(char *out, int max, int at, const char *text)
{
    while (*text) {
        if (at + 1 < max)
            out[at] = *text;
        at++;
        text++;
    }
    return at;
}

static int put_num(char *out, int max, int at, int value, int digits)
{
    char buf[16];
    snprintf(buf, sizeof buf, digits > 1 ? "%02d" : "%d", value);
    return put(out, max, at, buf);
}

static int format_picture(const SYSTEMTIME *st, const char *fmt, char *out,
                          int max, int is_time)
{
    struct tm t;
    int at = 0;
    to_tm(st, &t);
    while (*fmt) {
        int n;
        char buf[64];
        if (*fmt == '\'') { /* quoted text is copied, and '' is one quote */
            fmt++;
            while (*fmt && *fmt != '\'') {
                if (at + 1 < max)
                    out[at] = *fmt;
                at++;
                fmt++;
            }
            if (*fmt)
                fmt++;
            continue;
        }
        n = run_of(fmt);
        if (!is_time && *fmt == 'y') {
            at = put_num(out, max, at, n <= 2 ? st->wYear % 100 : st->wYear,
                         n <= 2 ? 2 : 4);
        } else if (!is_time && *fmt == 'M') {
            if (n >= 4) {
                strftime(buf, sizeof buf, "%B", &t);
                at = put(out, max, at, buf);
            } else if (n == 3) {
                strftime(buf, sizeof buf, "%b", &t);
                at = put(out, max, at, buf);
            } else {
                at = put_num(out, max, at, st->wMonth, n);
            }
        } else if (!is_time && *fmt == 'd') {
            if (n >= 4) {
                strftime(buf, sizeof buf, "%A", &t);
                at = put(out, max, at, buf);
            } else if (n == 3) {
                strftime(buf, sizeof buf, "%a", &t);
                at = put(out, max, at, buf);
            } else {
                at = put_num(out, max, at, st->wDay, n);
            }
        } else if (is_time && *fmt == 'H') {
            at = put_num(out, max, at, st->wHour, n);
        } else if (is_time && *fmt == 'h') {
            int h = st->wHour % 12;
            at = put_num(out, max, at, h ? h : 12, n);
        } else if (is_time && *fmt == 'm') {
            at = put_num(out, max, at, st->wMinute, n);
        } else if (is_time && *fmt == 's') {
            at = put_num(out, max, at, st->wSecond, n);
        } else if (is_time && *fmt == 't') {
            const char *ampm = st->wHour < 12 ? "AM" : "PM";
            at = put(out, max, at, n == 1 ? (st->wHour < 12 ? "A" : "P") : ampm);
        } else {
            for (int i = 0; i < n; i++) {
                if (at + 1 < max)
                    out[at] = *fmt;
                at++;
            }
        }
        fmt += n;
    }
    if (max > 0)
        out[at < max ? at : max - 1] = 0;
    return at + 1 <= max ? at + 1 : 0;
}

/* Windows counts the terminator in what it returns, and a zero-length buffer
 * asks how much room the answer needs. */
static int format_time(const SYSTEMTIME *st, DWORD flags, LPCSTR fmt,
                       LPSTR out, int max, int is_time)
{
    SYSTEMTIME now;
    char buf[256];
    struct tm t;
    int n;
    if (!st) {
        GetLocalTime(&now);
        st = &now;
    }
    if (fmt)
        return max > 0 ? format_picture(st, fmt, out, max, is_time)
                       : format_picture(st, fmt, buf, (int)sizeof buf, is_time);
    to_tm(st, &t);
    if (is_time)
        n = (int)strftime(buf, sizeof buf,
                          (flags & TIME_NOMINUTESORSECONDS) ? "%H"
                          : (flags & TIME_NOSECONDS)        ? "%H:%M"
                                                            : "%X",
                          &t);
    else
        n = (int)strftime(buf, sizeof buf,
                          (flags & DATE_LONGDATE) ? "%A, %B %d, %Y" : "%x", &t);
    if (max <= 0)
        return n + 1;
    if (n + 1 > max) {
        if (out && max > 0)
            out[0] = 0;
        return 0;
    }
    if (out)
        memcpy(out, buf, (size_t)n + 1);
    return n + 1;
}

int GetDateFormatA(LCID locale, DWORD flags, const SYSTEMTIME *st,
                   LPCSTR format, LPSTR out, int max)
{
    (void)locale;
    return format_time(st, flags, format, out, max, 0);
}

int GetTimeFormatA(LCID locale, DWORD flags, const SYSTEMTIME *st,
                   LPCSTR format, LPSTR out, int max)
{
    (void)locale;
    return format_time(st, flags, format, out, max, 1);
}

/* ---- the two encodings ---------------------------------------------------
 *
 * CP_UTF8 is UTF-8, and CP_ACP -- "the system's ANSI code page" -- is Latin-1
 * here, that being the character set this library's A-API speaks. A character
 * with no Latin-1 spelling comes back as a question mark, which is the
 * default character Windows substitutes when the caller names none.
 *
 * Both calls have win32's shape: a negative input length means the string is
 * NUL-terminated and the terminator is converted with it, and an output
 * length of zero asks how much room the answer would need.
 */

static int utf8_decode(const unsigned char *s, int len, unsigned *cp)
{
    unsigned c = s[0];
    int n;
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        n = 2;
        c &= 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
        c &= 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
        c &= 0x07;
    } else {
        *cp = 0xFFFD; /* a byte that starts nothing */
        return 1;
    }
    if (n > len) {
        *cp = 0xFFFD;
        return 1;
    }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return 1;
        }
        c = (c << 6) | (unsigned)(s[i] & 0x3F);
    }
    *cp = c;
    return n;
}

int MultiByteToWideChar(UINT page, DWORD flags, LPCSTR in, int in_len,
                        LPWSTR out, int out_len)
{
    const unsigned char *p = (const unsigned char *)in;
    int left = in_len < 0 ? (int)strlen(in) + 1 : in_len;
    int n = 0;
    (void)flags;
    if (!in)
        return 0;
    while (left > 0) {
        unsigned cp;
        int step;
        if (page == CP_UTF8) {
            step = utf8_decode(p, left, &cp);
        } else {
            cp = *p;
            step = 1;
        }
        p += step;
        left -= step;
        if (cp >= 0x10000) { /* a surrogate pair, which is two WCHARs */
            unsigned v = cp - 0x10000;
            if (out_len > 0) {
                if (n + 2 > out_len)
                    return 0;
                out[n] = (WCHAR)(0xD800 + (v >> 10));
                out[n + 1] = (WCHAR)(0xDC00 + (v & 0x3FF));
            }
            n += 2;
            continue;
        }
        if (out_len > 0) {
            if (n + 1 > out_len)
                return 0;
            out[n] = (WCHAR)cp;
        }
        n++;
    }
    return n;
}

int WideCharToMultiByte(UINT page, DWORD flags, LPCWSTR in, int in_len,
                        LPSTR out, int out_len, LPCSTR default_char,
                        BOOL *used_default)
{
    int left = in_len, n = 0;
    (void)flags;
    if (!in)
        return 0;
    if (left < 0) {
        left = 0;
        while (in[left])
            left++;
        left++; /* the terminator goes too */
    }
    if (used_default)
        *used_default = FALSE;
    for (int i = 0; i < left; i++) {
        unsigned cp = in[i];
        char bytes[4];
        int count;
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < left && in[i + 1] >= 0xDC00 &&
            in[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
            i++;
        }
        if (page == CP_UTF8) {
            if (cp < 0x80) {
                bytes[0] = (char)cp;
                count = 1;
            } else if (cp < 0x800) {
                bytes[0] = (char)(0xC0 | (cp >> 6));
                bytes[1] = (char)(0x80 | (cp & 0x3F));
                count = 2;
            } else if (cp < 0x10000) {
                bytes[0] = (char)(0xE0 | (cp >> 12));
                bytes[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                bytes[2] = (char)(0x80 | (cp & 0x3F));
                count = 3;
            } else {
                bytes[0] = (char)(0xF0 | (cp >> 18));
                bytes[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                bytes[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                bytes[3] = (char)(0x80 | (cp & 0x3F));
                count = 4;
            }
        } else {
            if (cp < 0x100) {
                bytes[0] = (char)cp;
            } else {
                bytes[0] = default_char && *default_char ? *default_char : '?';
                if (used_default)
                    *used_default = TRUE;
            }
            count = 1;
        }
        if (out_len > 0) {
            if (n + count > out_len)
                return 0;
            memcpy(out + n, bytes, (size_t)count);
        }
        n += count;
    }
    return n;
}
