/* fs.h — listing a directory, on either side of the dual build.
 *
 * This is app-side code, like win32_dlg.h: it uses win32 where win32 is what
 * it is compiled against, and the POSIX equivalent otherwise. ween32 itself
 * stays a GUI library and knows nothing about files.
 *
 * Listing is one struct and three calls:
 *
 *     fs_dir d;
 *     fs_entry e;
 *     if (fs_open(&d, "/home/jd"))
 *         while (fs_next(&d, &e))
 *             ... e.name, e.is_dir, e.size ...
 *     fs_close(&d);
 *
 * and the four things the File and Edit menus do to what they find —
 * fs_mkdir, fs_rename, fs_delete and fs_copy — each returning zero on
 * failure, since a shell says so rather than stopping.
 */

#ifndef WEEN32_EXAMPLE_FS_H
#define WEEN32_EXAMPLE_FS_H

#include <string.h>

typedef struct {
    char name[260];
    int is_dir;
    unsigned long size;
    /* when it was last written, as "M/D/YYYY h:mm AM" — a string rather than
     * a time_t or a FILETIME, because those are the two sides' own shapes and
     * this is the one place that has to agree */
    char modified[40]; /* "12/31/2038 12:59 PM" and room to spare */
} fs_entry;

#ifdef _WIN32

typedef struct {
    HANDLE h;
    WIN32_FIND_DATAA fd;
    int first;
} fs_dir;

static int fs_open(fs_dir *d, const char *path)
{
    char spec[512];
    snprintf(spec, sizeof(spec), "%s\\*", path);
    d->h = FindFirstFileA(spec, &d->fd);
    d->first = 1;
    return d->h != INVALID_HANDLE_VALUE;
}

static int fs_next(fs_dir *d, fs_entry *e)
{
    if (d->h == INVALID_HANDLE_VALUE)
        return 0;
    if (!d->first && !FindNextFileA(d->h, &d->fd))
        return 0;
    d->first = 0;
    strncpy(e->name, d->fd.cFileName, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = 0;
    e->is_dir = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e->size = d->fd.nFileSizeLow;
    {
        SYSTEMTIME st;
        FILETIME local;
        FileTimeToLocalFileTime(&d->fd.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &st);
        snprintf(e->modified, sizeof(e->modified), "%d/%d/%d %d:%02d %s",
                 st.wMonth, st.wDay, st.wYear,
                 st.wHour % 12 ? st.wHour % 12 : 12, st.wMinute,
                 st.wHour < 12 ? "AM" : "PM");
    }
    return 1;
}

static void fs_close(fs_dir *d)
{
    if (d->h != INVALID_HANDLE_VALUE)
        FindClose(d->h);
}

/* The separator, so a path can be put together without knowing which side
 * this is: a shell shows one and the file system takes the other. */
#define FS_SEP '\\'

static int fs_mkdir(const char *path)
{
    return CreateDirectoryA(path, NULL) != 0;
}

static int fs_rename(const char *from, const char *to)
{
    return MoveFileA(from, to) != 0;
}

static int fs_delete(const char *path, int is_dir)
{
    return is_dir ? RemoveDirectoryA(path) != 0 : DeleteFileA(path) != 0;
}

static int fs_copy(const char *from, const char *to)
{
    return CopyFileA(from, to, TRUE) != 0;
}

static int fs_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

#else /* POSIX */

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    DIR *dir;
    char base[512];
} fs_dir;

static int fs_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int fs_open(fs_dir *d, const char *path)
{
    strncpy(d->base, path, sizeof(d->base) - 1);
    d->base[sizeof(d->base) - 1] = 0;
    d->dir = opendir(path);
    return d->dir != NULL;
}

static int fs_next(fs_dir *d, fs_entry *e)
{
    struct dirent *ent;
    struct stat st;
    char full[800];
    if (!d->dir)
        return 0;
    while ((ent = readdir(d->dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0)
            continue;
        snprintf(full, sizeof(full), "%s/%s", d->base, ent->d_name);
        if (stat(full, &st) != 0)
            continue;
        strncpy(e->name, ent->d_name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = 0;
        e->is_dir = S_ISDIR(st.st_mode) != 0;
        e->size = (unsigned long)st.st_size;
        {
            struct tm *tm = localtime(&st.st_mtime);
            if (tm)
                snprintf(e->modified, sizeof(e->modified),
                         "%d/%d/%d %d:%02d %s", tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_year + 1900,
                         tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, tm->tm_min,
                         tm->tm_hour < 12 ? "AM" : "PM");
            else
                e->modified[0] = 0;
        }
        return 1;
    }
    return 0;
}

static void fs_close(fs_dir *d)
{
    if (d->dir)
        closedir(d->dir);
}

/* The separator, so a path can be put together without knowing which side
 * this is: a shell shows one and the file system takes the other. */
#define FS_SEP '/'

static int fs_mkdir(const char *path)
{
    return mkdir(path, 0777) == 0;
}

static int fs_rename(const char *from, const char *to)
{
    return rename(from, to) == 0;
}

static int fs_delete(const char *path, int is_dir)
{
    return is_dir ? rmdir(path) == 0 : unlink(path) == 0;
}

static int fs_copy(const char *from, const char *to)
{
    FILE *in, *out;
    char buf[8192];
    size_t n;
    int ok = 1;
    if (fs_exists(to))
        return 0; /* never over the top of something that is there */
    in = fopen(from, "rb");
    if (!in)
        return 0;
    out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    fclose(in);
    fclose(out);
    return ok;
}

#endif

#endif /* WEEN32_EXAMPLE_FS_H */
