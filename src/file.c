/* The file calls of KERNEL32, over the C library's.
 *
 * A win32 program does not open a file with fopen: it opens it with
 * CreateFile and reads it with ReadFile, and those are the calls it has on
 * any Windows there has ever been. Going through them rather than through the
 * C runtime is also what lets a program built for Windows carry no C runtime
 * at all -- which matters more than it sounds, because the one a modern
 * toolchain links is the UCRT, and a machine older than it cannot even start
 * a program that asks for it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ween_internal.h"

/* A HANDLE here is the FILE * the C library gave back. NULL is not a valid
 * one, and neither is INVALID_HANDLE_VALUE, so nothing of ours collides. */

HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, void *security,
                   DWORD disposition, DWORD flags, HANDLE template_file)
{
    const char *mode;
    FILE *f;
    (void)share;
    (void)security;
    (void)flags;
    (void)template_file;
    if (!name)
        return INVALID_HANDLE_VALUE;
    if (access & GENERIC_WRITE)
        mode = disposition == OPEN_EXISTING ? "r+b" : "wb";
    else
        mode = "rb";
    f = fopen(name, mode);
    if (!f)
        return INVALID_HANDLE_VALUE;
    return (HANDLE)f;
}

BOOL ReadFile(HANDLE file, void *buf, DWORD to_read, DWORD *read, void *ovl)
{
    size_t n;
    (void)ovl;
    if (!file || file == INVALID_HANDLE_VALUE || !buf)
        return FALSE;
    n = fread(buf, 1, to_read, (FILE *)file);
    if (read)
        *read = (DWORD)n;
    /* A short read is not a failure: the end of the file is reported by the
     * count, which is the one thing about this call that surprises people. */
    return TRUE;
}

BOOL WriteFile(HANDLE file, const void *buf, DWORD to_write, DWORD *written,
               void *ovl)
{
    size_t n;
    (void)ovl;
    if (!file || file == INVALID_HANDLE_VALUE || !buf)
        return FALSE;
    n = fwrite(buf, 1, to_write, (FILE *)file);
    if (written)
        *written = (DWORD)n;
    return n == to_write;
}

DWORD SetFilePointer(HANDLE file, LONG distance, LONG *high, DWORD method)
{
    int whence = method == FILE_CURRENT ? SEEK_CUR
                 : method == FILE_END   ? SEEK_END
                                        : SEEK_SET;
    (void)high; /* no file here is larger than a long */
    if (!file || file == INVALID_HANDLE_VALUE)
        return INVALID_SET_FILE_POINTER;
    if (fseek((FILE *)file, distance, whence) != 0)
        return INVALID_SET_FILE_POINTER;
    return (DWORD)ftell((FILE *)file);
}

DWORD GetFileSize(HANDLE file, DWORD *high)
{
    long here, end;
    if (high)
        *high = 0;
    if (!file || file == INVALID_HANDLE_VALUE)
        return 0xFFFFFFFFu;
    here = ftell((FILE *)file);
    if (fseek((FILE *)file, 0, SEEK_END) != 0)
        return 0xFFFFFFFFu;
    end = ftell((FILE *)file);
    fseek((FILE *)file, here, SEEK_SET);
    return (DWORD)end;
}

BOOL CloseHandle(HANDLE h)
{
    if (!h || h == INVALID_HANDLE_VALUE)
        return FALSE;
    return fclose((FILE *)h) == 0;
}
