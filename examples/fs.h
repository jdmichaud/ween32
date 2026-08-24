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
    unsigned long on_disk; /* what it takes up, which is whole clusters of it */
    /* the same dates the long way round, which is how a shell's Properties
     * writes them: "Saturday, July 08, 2017, 6:33:06 PM". The one it was read
     * on it writes without a time, and as "Today" when that is what it is. */
    char modified_long[64];
    char created_long[64];
    char accessed_long[64];
    /* when it was last written, as "M/D/YYYY h:mm AM" — a string rather than
     * a time_t or a FILETIME, because those are the two sides' own shapes and
     * this is the one place that has to agree */
    char modified[40]; /* "12/31/2038 12:59 PM" and room to spare */
    char created[40];  /* the same, for the other two dates a shell offers */
    char accessed[40];
    char attributes[8]; /* "RHSA", in that order, of whatever is set */
} fs_entry;

/* One of the three dates, written the way a shell writes them. Shared so the
 * three cannot drift apart. */
#define FS_STAMP(out, mo, dy, yr, hh, mi)                                          snprintf((out), sizeof(out), "%d/%d/%d %d:%02d %s", (mo), (dy), (yr),                   (hh) % 12 ? (hh) % 12 : 12, (mi), (hh) < 12 ? "AM" : "PM")

/* The same moment written out in full, with the day of the week and the
 * seconds, which is the form a Properties page uses. */
static const char *fs_weekday(int i)
{
    static const char *n[] = { "Sunday",   "Monday", "Tuesday", "Wednesday",
                               "Thursday", "Friday", "Saturday" };
    return n[i % 7];
}

static const char *fs_month(int m)
{
    static const char *n[] = { "January",   "February", "March",    "April",
                               "May",       "June",     "July",     "August",
                               "September", "October",  "November", "December" };
    return n[(m - 1) % 12];
}

#define FS_STAMP_LONG(out, wd, mo, dy, yr, hh, mi, se)                         \
    snprintf((out), sizeof(out), "%s, %s %02d, %d, %d:%02d:%02d %s",           \
             fs_weekday(wd), fs_month(mo), (dy), (yr),                         \
             (hh) % 12 ? (hh) % 12 : 12, (mi), (se), (hh) < 12 ? "AM" : "PM")

/* Today's date, for the one date that is written as "Today" when it is. */
static void fs_today(int *yr, int *mo, int *dy);

/* A date with no time on it: "Saturday, July 22, 2017", or "Today, August 23,
 * 2026" for the day it is being read on — which is what a Properties page has
 * against Accessed, since a file is accessed by being looked at. */
static void fs_stamp_date(char *out, size_t max, int wd, int mo, int dy, int yr)
{
    int ty, tm, td;
    fs_today(&ty, &tm, &td);
    if (yr == ty && mo == tm && dy == td)
        snprintf(out, max, "Today, %s %02d, %d", fs_month(mo), dy, yr);
    else
        snprintf(out, max, "%s, %s %02d, %d", fs_weekday(wd), fs_month(mo), dy,
                 yr);
}

#ifdef _WIN32

typedef struct {
    HANDLE h;
    WIN32_FIND_DATAA fd;
    int first;
} fs_dir;

static void fs_today(int *yr, int *mo, int *dy)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    *yr = now.wYear;
    *mo = now.wMonth;
    *dy = now.wDay;
}

/* How many bytes a file of that size takes up: whole clusters of the volume
 * it is on. The volume the program was started on is close enough for a
 * listing, and is what the shell asks for anyway. */
static unsigned long fs_on_disk(unsigned long size)
{
    static DWORD cluster;
    if (!cluster) {
        DWORD spc = 0, bps = 0, free_clusters = 0, clusters = 0;
        if (GetDiskFreeSpaceA(NULL, &spc, &bps, &free_clusters, &clusters))
            cluster = spc * bps;
        if (!cluster)
            cluster = 4096;
    }
    return (size + cluster - 1) / cluster * cluster;
}

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
    e->on_disk = e->is_dir ? 0 : fs_on_disk(e->size);
    {
        SYSTEMTIME st;
        FILETIME local;
        DWORD a = d->fd.dwFileAttributes;
        int n = 0;
        FileTimeToLocalFileTime(&d->fd.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &st);
        FS_STAMP(e->modified, st.wMonth, st.wDay, st.wYear, st.wHour,
                 st.wMinute);
        FS_STAMP_LONG(e->modified_long, st.wDayOfWeek, st.wMonth, st.wDay,
                      st.wYear, st.wHour, st.wMinute, st.wSecond);
        FileTimeToLocalFileTime(&d->fd.ftCreationTime, &local);
        FileTimeToSystemTime(&local, &st);
        FS_STAMP(e->created, st.wMonth, st.wDay, st.wYear, st.wHour,
                 st.wMinute);
        FS_STAMP_LONG(e->created_long, st.wDayOfWeek, st.wMonth, st.wDay,
                      st.wYear, st.wHour, st.wMinute, st.wSecond);
        FileTimeToLocalFileTime(&d->fd.ftLastAccessTime, &local);
        FileTimeToSystemTime(&local, &st);
        FS_STAMP(e->accessed, st.wMonth, st.wDay, st.wYear, st.wHour,
                 st.wMinute);
        fs_stamp_date(e->accessed_long, sizeof(e->accessed_long),
                      st.wDayOfWeek, st.wMonth, st.wDay, st.wYear);
        if (a & FILE_ATTRIBUTE_READONLY)
            e->attributes[n++] = 'R';
        if (a & FILE_ATTRIBUTE_HIDDEN)
            e->attributes[n++] = 'H';
        if (a & FILE_ATTRIBUTE_SYSTEM)
            e->attributes[n++] = 'S';
        if (a & FILE_ATTRIBUTE_ARCHIVE)
            e->attributes[n++] = 'A';
        e->attributes[n] = 0;
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

static int fs_is_dir(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* An empty file, and only where there is none: what New makes. */
static int fs_create(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CloseHandle(h);
    return 1;
}

/* The three attributes a Properties page offers, set to what it says. The
 * ones it does not show — system, and the rest — are left as they were. */
static int fs_set_attributes(const char *path, int readonly, int hidden,
                             int archive)
{
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES)
        return 0;
    a &= ~(DWORD)(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                  FILE_ATTRIBUTE_ARCHIVE);
    if (readonly)
        a |= FILE_ATTRIBUTE_READONLY;
    if (hidden)
        a |= FILE_ATTRIBUTE_HIDDEN;
    if (archive)
        a |= FILE_ATTRIBUTE_ARCHIVE;
    return SetFileAttributesA(path, a) != 0;
}

static int fs_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

#else /* POSIX */

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
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

static void fs_today(int *yr, int *mo, int *dy)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    *yr = tm ? tm->tm_year + 1900 : 0;
    *mo = tm ? tm->tm_mon + 1 : 0;
    *dy = tm ? tm->tm_mday : 0;
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
        /* what it takes up rather than what is in it, which the file system
         * counts in blocks of five hundred and twelve */
        e->on_disk = e->is_dir ? 0 : (unsigned long)st.st_blocks * 512;
        {
            /* the three dates a shell offers, and the attributes it can show
             * of the ones this side has: a dot file is the hidden one, and a
             * file with no write bit is the read-only one */
            struct tm *tm;
            int n = 0;
            e->modified[0] = e->created[0] = e->accessed[0] = 0;
            e->modified_long[0] = e->created_long[0] = e->accessed_long[0] = 0;
            if ((tm = localtime(&st.st_mtime)) != NULL) {
                FS_STAMP(e->modified, tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_year + 1900, tm->tm_hour, tm->tm_min);
                FS_STAMP_LONG(e->modified_long, tm->tm_wday, tm->tm_mon + 1,
                              tm->tm_mday, tm->tm_year + 1900, tm->tm_hour,
                              tm->tm_min, tm->tm_sec);
            }
            if ((tm = localtime(&st.st_ctime)) != NULL) {
                FS_STAMP(e->created, tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_year + 1900, tm->tm_hour, tm->tm_min);
                FS_STAMP_LONG(e->created_long, tm->tm_wday, tm->tm_mon + 1,
                              tm->tm_mday, tm->tm_year + 1900, tm->tm_hour,
                              tm->tm_min, tm->tm_sec);
            }
            if ((tm = localtime(&st.st_atime)) != NULL) {
                FS_STAMP(e->accessed, tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_year + 1900, tm->tm_hour, tm->tm_min);
                fs_stamp_date(e->accessed_long, sizeof(e->accessed_long),
                              tm->tm_wday, tm->tm_mon + 1, tm->tm_mday,
                              tm->tm_year + 1900);
            }
            if (!(st.st_mode & S_IWUSR))
                e->attributes[n++] = 'R';
            if (ent->d_name[0] == '.')
                e->attributes[n++] = 'H';
            e->attributes[n] = 0;
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

static int fs_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* An empty file, and only where there is none: what New makes. */
static int fs_create(const char *path)
{
    FILE *f;
    if (fs_exists(path))
        return 0;
    f = fopen(path, "wb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* The attributes a Properties page offers. This side has one of the three:
 * read-only is the write bit, and hidden is what a name beginning with a dot
 * means, which is not something to rename a file over. Archive it has no
 * place for at all. */
static int fs_set_attributes(const char *path, int readonly, int hidden,
                             int archive)
{
    struct stat st;
    mode_t mode;
    (void)hidden;
    (void)archive;
    if (stat(path, &st) != 0)
        return 0;
    mode = st.st_mode & 07777;
    if (readonly)
        mode &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
    else
        mode |= S_IWUSR;
    return chmod(path, mode) == 0;
}

#endif

/* Copy a whole folder, or one file — what a shell's paste does with either.
 * A folder is made and everything in it copied name by name, since no file
 * system has a call that copies a tree; a name that cannot be copied is
 * stepped over rather than stopping the rest, which is what the shell does
 * when one file in a folder is locked. Nothing is written over: a name that
 * is taken is the caller's to settle before it asks. */
static int fs_copy_tree(const char *from, const char *to)
{
    fs_dir d;
    fs_entry e;
    int ok = 1;
    if (!fs_is_dir(from))
        return fs_copy(from, to);
    if (fs_exists(to) || !fs_mkdir(to))
        return 0;
    if (!fs_open(&d, from))
        return 0;
    while (fs_next(&d, &e)) {
        char a[800], b[800];
        if (!strcmp(e.name, ".") || !strcmp(e.name, ".."))
            continue;
        snprintf(a, sizeof(a), "%s%c%s", from, FS_SEP, e.name);
        snprintf(b, sizeof(b), "%s%c%s", to, FS_SEP, e.name);
        if (!fs_copy_tree(a, b))
            ok = 0;
    }
    fs_close(&d);
    return ok;
}

#endif /* WEEN32_EXAMPLE_FS_H */
