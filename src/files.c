/* The file-finding corner of kernel32.
 *
 * ween32 is a GUI library and this is not GUI. It is here because of the
 * contract the examples keep: every one of them compiles unchanged against
 * real <windows.h> and against ween32, which is what lets the same source be
 * rendered by Windows and by us and the two compared. An application that
 * browses files cannot honour that contract if it has to reach for readdir
 * behind an #ifdef — so the handful of calls it needs are shaped like win32's
 * and answered here.
 *
 * It is deliberately a handful. FindFirstFile/FindNextFile/FindClose, the
 * attributes of a path, and the list of drives: enough to walk a filesystem
 * and no more. Anything else an app wants from kernel32 it should get from
 * the C library, as the other examples do.
 */

#define _POSIX_C_SOURCE 200809L /* readdir, stat */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "ween_internal.h"

struct ween_find {
    DIR *dir;
    char base[1024];  /* the directory being walked */
    char pattern[256]; /* what to match in it, from the tail of the path */
};

/* win32's wildcards, as far as an app browsing files uses them: * and ?.
 * Matching is case-insensitive because win32's is, and an app written for it
 * will assume so. */
static int wildcard_match(const char *pat, const char *name)
{
    while (*pat && *name) {
        if (*pat == '*') {
            pat++;
            if (!*pat)
                return 1;
            for (const char *n = name; *n; n++)
                if (wildcard_match(pat, n))
                    return 1;
            return 0;
        }
        char a = *pat, b = *name;
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + 32);
        if (*pat != '?' && a != b)
            return 0;
        pat++;
        name++;
    }
    while (*pat == '*')
        pat++;
    return !*pat && !*name;
}

/* A file's time, as win32 counts it: hundreds of nanoseconds since 1601. */
static void unix_to_filetime(time_t t, FILETIME *out)
{
    unsigned long long ticks = (unsigned long long)t * 10000000ull +
                               116444736000000000ull;
    out->dwLowDateTime = (DWORD)(ticks & 0xffffffffu);
    out->dwHighDateTime = (DWORD)(ticks >> 32);
}

static int fill_find_data(const char *dir, const char *name,
                          WIN32_FIND_DATAA *out)
{
    char path[1400];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (stat(path, &st) != 0)
        return 0;
    memset(out, 0, sizeof(*out));
    strncpy(out->cFileName, name, sizeof(out->cFileName) - 1);
    out->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY
                                                : FILE_ATTRIBUTE_NORMAL;
    if (name[0] == '.' && name[1]) /* a dotfile is win32's hidden */
        out->dwFileAttributes |= FILE_ATTRIBUTE_HIDDEN;
    out->nFileSizeLow = (DWORD)(st.st_size & 0xffffffffu);
    out->nFileSizeHigh = (DWORD)((unsigned long long)st.st_size >> 32);
    unix_to_filetime(st.st_mtime, &out->ftLastWriteTime);
    unix_to_filetime(st.st_mtime, &out->ftCreationTime);
    unix_to_filetime(st.st_atime, &out->ftLastAccessTime);
    return 1;
}

/* Split "dir/pattern" the way win32 does: everything up to the last separator
 * is the directory, the rest is what to match in it. */
static void split_path(const char *spec, char *dir, size_t dirlen, char *pat,
                       size_t patlen)
{
    const char *slash = NULL;
    for (const char *p = spec; *p; p++)
        if (*p == '/' || *p == '\\')
            slash = p;
    if (slash) {
        size_t n = (size_t)(slash - spec);
        if (n == 0)
            n = 1; /* the root itself */
        if (n >= dirlen)
            n = dirlen - 1;
        memcpy(dir, spec, n);
        dir[n] = 0;
        strncpy(pat, slash + 1, patlen - 1);
        pat[patlen - 1] = 0;
    } else {
        strncpy(dir, ".", dirlen - 1);
        dir[dirlen - 1] = 0;
        strncpy(pat, spec, patlen - 1);
        pat[patlen - 1] = 0;
    }
    if (!pat[0])
        strncpy(pat, "*", patlen - 1);
}

HANDLE FindFirstFileA(LPCSTR spec, WIN32_FIND_DATAA *data)
{
    if (!spec || !data)
        return INVALID_HANDLE_VALUE;
    struct ween_find *f = calloc(1, sizeof(*f));
    if (!f)
        return INVALID_HANDLE_VALUE;
    split_path(spec, f->base, sizeof(f->base), f->pattern, sizeof(f->pattern));
    f->dir = opendir(f->base);
    if (!f->dir) {
        free(f);
        return INVALID_HANDLE_VALUE;
    }
    if (!FindNextFileA((HANDLE)f, data)) {
        FindClose((HANDLE)f);
        return INVALID_HANDLE_VALUE;
    }
    return (HANDLE)f;
}

BOOL FindNextFileA(HANDLE handle, WIN32_FIND_DATAA *data)
{
    struct ween_find *f = (struct ween_find *)handle;
    struct dirent *e;
    if (!f || !f->dir || !data)
        return FALSE;
    while ((e = readdir(f->dir)) != NULL) {
        if (strcmp(e->d_name, ".") == 0)
            continue; /* win32 lists "." and ".."; "." is never wanted */
        if (!wildcard_match(f->pattern, e->d_name))
            continue;
        if (fill_find_data(f->base, e->d_name, data))
            return TRUE;
    }
    return FALSE;
}

BOOL FindClose(HANDLE handle)
{
    struct ween_find *f = (struct ween_find *)handle;
    if (!f)
        return FALSE;
    if (f->dir)
        closedir(f->dir);
    free(f);
    return TRUE;
}

DWORD GetFileAttributesA(LPCSTR path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0)
        return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY
                               : FILE_ATTRIBUTE_NORMAL;
}

/* The roots to show at the top of a tree. On Windows this is the drive
 * letters; here it is the one root there is, given the same shape so an app
 * can walk it the same way. */
DWORD GetLogicalDriveStringsA(DWORD len, LPSTR buf)
{
    const char *roots = "/\0";
    DWORD need = 3; /* "/" NUL NUL */
    if (!buf || len < need)
        return need;
    memcpy(buf, roots, 2);
    buf[2] = 0;
    return need - 1;
}
