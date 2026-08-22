/* Walking a directory the way win32 does, which is what a file browser is
 * made of. Not GUI, and the only reason it is in a GUI library: an example
 * that browses files has to compile unchanged against real <windows.h> too. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/ween_internal.h"

static int g_failures = 0;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("ok   %s\n", name);                                         \
        } else {                                                               \
            printf("FAIL %s\n", name);                                         \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

int main(void)
{
    const char *dir = "/tmp/ween_files_test";
    char path[512];

    /* a small tree to walk: two files, one directory */
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/alpha.txt", dir);
    write_file(path, "hello");
    snprintf(path, sizeof(path), "%s/beta.log", dir);
    write_file(path, "worldly");
    snprintf(path, sizeof(path), "%s/sub", dir);
    mkdir(path, 0755);

    /* Everything in it. */
    {
        WIN32_FIND_DATAA fd;
        char spec[512];
        int files = 0, dirs = 0, dotdot = 0;
        snprintf(spec, sizeof(spec), "%s/*", dir);
        HANDLE h = FindFirstFileA(spec, &fd);
        CHECK(h != INVALID_HANDLE_VALUE, "a directory opens for walking");
        do {
            if (strcmp(fd.cFileName, "..") == 0) {
                dotdot++;
                continue;
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                dirs++;
            else
                files++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        CHECK(files == 2 && dirs == 1,
              "two files and one directory came back, told apart");
        CHECK(dotdot == 1, "and \"..\" is listed, as win32 lists it");
    }

    /* A pattern, matched case-insensitively as win32 matches. */
    {
        WIN32_FIND_DATAA fd;
        char spec[512];
        int n = 0;
        snprintf(spec, sizeof(spec), "%s/*.TXT", dir);
        HANDLE h = FindFirstFileA(spec, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                n++;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        CHECK(n == 1, "a wildcard matches, and ignores case doing it");
    }

    /* A size, and a name, off one file. */
    {
        WIN32_FIND_DATAA fd;
        char spec[512];
        snprintf(spec, sizeof(spec), "%s/beta.log", dir);
        HANDLE h = FindFirstFileA(spec, &fd);
        CHECK(h != INVALID_HANDLE_VALUE, "one named file is found");
        if (h != INVALID_HANDLE_VALUE) {
            CHECK(strcmp(fd.cFileName, "beta.log") == 0,
                  "with its name, not its path");
            CHECK(fd.nFileSizeLow == 7, "and its size");
            CHECK(fd.ftLastWriteTime.dwHighDateTime > 0,
                  "and a write time counted from 1601, as win32 counts it");
            FindClose(h);
        }
    }

    /* Nothing at all, which is not an error. */
    {
        WIN32_FIND_DATAA fd;
        char spec[512];
        snprintf(spec, sizeof(spec), "%s/*.nothing", dir);
        CHECK(FindFirstFileA(spec, &fd) == INVALID_HANDLE_VALUE,
              "a pattern matching nothing says so rather than failing oddly");
        CHECK(FindFirstFileA("/tmp/ween_no_such_dir/*", &fd) ==
                  INVALID_HANDLE_VALUE,
              "and so does a directory that is not there");
    }

    /* Attributes of a path, without walking to it. */
    CHECK(GetFileAttributesA(dir) & FILE_ATTRIBUTE_DIRECTORY,
          "a directory says it is one");
    snprintf(path, sizeof(path), "%s/alpha.txt", dir);
    CHECK(!(GetFileAttributesA(path) & FILE_ATTRIBUTE_DIRECTORY),
          "and a file says it is not");
    CHECK(GetFileAttributesA("/tmp/ween_no_such_file") ==
              INVALID_FILE_ATTRIBUTES,
          "and something absent says nothing");

    /* The roots a tree starts from. */
    {
        char buf[64];
        DWORD n = GetLogicalDriveStringsA(sizeof(buf), buf);
        CHECK(n > 0 && buf[0] == '/', "there is a root to start a tree at");
    }

    /* tidy up */
    snprintf(path, sizeof(path), "%s/alpha.txt", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/beta.log", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/sub", dir);
    rmdir(path);
    rmdir(dir);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("files_test: all passed\n");
    return 0;
}
