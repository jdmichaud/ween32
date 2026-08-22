/* fs.h — listing a directory, on either side of the dual build.
 *
 * This is app-side code, like win32_dlg.h: it uses win32 where win32 is what
 * it is compiled against, and the POSIX equivalent otherwise. ween32 itself
 * stays a GUI library and knows nothing about files.
 *
 * The whole of it is one struct and three calls:
 *
 *     fs_dir d;
 *     fs_entry e;
 *     if (fs_open(&d, "/home/jd"))
 *         while (fs_next(&d, &e))
 *             ... e.name, e.is_dir, e.size ...
 *     fs_close(&d);
 */

#ifndef WEEN32_EXAMPLE_FS_H
#define WEEN32_EXAMPLE_FS_H

#include <string.h>

typedef struct {
    char name[260];
    int is_dir;
    unsigned long size;
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
    return 1;
}

static void fs_close(fs_dir *d)
{
    if (d->h != INVALID_HANDLE_VALUE)
        FindClose(d->h);
}

#else /* POSIX */

#include <dirent.h>
#include <sys/stat.h>

typedef struct {
    DIR *dir;
    char base[512];
} fs_dir;

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
        return 1;
    }
    return 0;
}

static void fs_close(fs_dir *d)
{
    if (d->dir)
        closedir(d->dir);
}

#endif

#endif /* WEEN32_EXAMPLE_FS_H */
