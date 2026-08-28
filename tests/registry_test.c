/* The registry, asked what a program that remembers its settings asks it.
 *
 * A key created, values written and read back, the sizes and the error codes
 * a caller checks, and -- the part that matters -- what is in the file
 * afterwards, since the point of the whole thing is that the settings are
 * still there next time. WEEN32_REGISTRY puts that file in /tmp, so running
 * the suite never touches what a real program has stored.
 */

#define _POSIX_C_SOURCE 200112L /* setenv */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static const char *g_path = "/tmp/ween32-registry-test.reg";

/* The whole file, so that a test can say what is written in it. */
static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf)
        buf[n] = 0;
    fclose(f);
    return buf;
}

int main(void)
{
    HKEY key = NULL;
    DWORD disp = 0, type = 0, size = 0;
    char text[64];
    DWORD number = 0;

    remove(g_path);
    setenv("WEEN32_REGISTRY", g_path, 1);
    ween_registry_forget();

    /* ---- a key, and what is under it ---- */

    CHECK(RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\ween32test", 0, NULL,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key,
                          &disp) == ERROR_SUCCESS,
          "a key is created under HKEY_CURRENT_USER");
    CHECK(disp == REG_CREATED_NEW_KEY, "and says it is a new one");
    CHECK(key != NULL, "and hands back a handle to it");

    CHECK(RegSetValueExA(key, "lfFaceName", 0, REG_SZ,
                         (const BYTE *)"Lucida Console",
                         (DWORD)strlen("Lucida Console") + 1) == ERROR_SUCCESS,
          "a string is written");
    number = 100;
    CHECK(RegSetValueExA(key, "iPointSize", 0, REG_DWORD, (const BYTE *)&number,
                         sizeof number) == ERROR_SUCCESS,
          "and a number");

    size = sizeof text;
    type = 0;
    CHECK(RegQueryValueExA(key, "lfFaceName", NULL, &type, (BYTE *)text,
                           &size) == ERROR_SUCCESS,
          "the string reads back");
    CHECK(type == REG_SZ, "as a REG_SZ");
    CHECK(strcmp(text, "Lucida Console") == 0, "with what was written in it");
    CHECK(size == strlen("Lucida Console") + 1,
          "and the size counts its terminator, as Windows does");

    number = 0;
    size = sizeof number;
    CHECK(RegQueryValueExA(key, "iPointSize", NULL, &type, (BYTE *)&number,
                           &size) == ERROR_SUCCESS &&
              type == REG_DWORD && number == 100,
          "the number reads back as a REG_DWORD");

    /* ---- what a caller checks ---- */

    CHECK(RegQueryValueExA(key, "notThere", NULL, &type, (BYTE *)text,
                           &size) == ERROR_FILE_NOT_FOUND,
          "a value that was never written is not found");
    size = 0;
    CHECK(RegQueryValueExA(key, "lfFaceName", NULL, NULL, NULL, &size) ==
                  ERROR_SUCCESS &&
              size == strlen("Lucida Console") + 1,
          "asking with no buffer answers how big one would have to be");
    size = 4;
    CHECK(RegQueryValueExA(key, "lfFaceName", NULL, &type, (BYTE *)text,
                           &size) == ERROR_MORE_DATA,
          "a buffer too small is ERROR_MORE_DATA");
    CHECK(size == strlen("Lucida Console") + 1,
          "and is told the size it needs");

    /* ---- and it is still there next time ---- */

    CHECK(RegCloseKey(key) == ERROR_SUCCESS, "the key closes");
    {
        char *file = slurp(g_path);
        CHECK(file != NULL, "a file was written");
        CHECK(file && strncmp(file, "REGEDIT4\n", 9) == 0,
              "in the format regedit exports, saying so on its first line");
        CHECK(file && strstr(file, "[HKEY_CURRENT_USER\\Software\\ween32test]"),
              "with the key in brackets, by its full path");
        CHECK(file && strstr(file, "\"lfFaceName\"=\"Lucida Console\""),
              "the string as text a person can read and edit");
        CHECK(file && strstr(file, "\"iPointSize\"=dword:00000064"),
              "and the number as eight hex digits, which is 100");
        free(file);
    }

    /* Everything held is dropped, so what comes back now comes back from the
     * file -- which is the whole point of writing one. */
    ween_registry_forget();
    key = NULL;
    CHECK(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ween32test", 0, KEY_READ,
                        &key) == ERROR_SUCCESS,
          "the key opens again in a registry that has forgotten everything");
    size = sizeof text;
    CHECK(RegQueryValueExA(key, "lfFaceName", NULL, &type, (BYTE *)text,
                           &size) == ERROR_SUCCESS &&
              strcmp(text, "Lucida Console") == 0,
          "and the string came back out of the file");
    number = 0;
    size = sizeof number;
    CHECK(RegQueryValueExA(key, "iPointSize", NULL, &type, (BYTE *)&number,
                           &size) == ERROR_SUCCESS &&
              number == 100,
          "and so did the number");
    CHECK(RegQueryValueExA(key, "LFFACENAME", NULL, &type, (BYTE *)text,
                           &size) != ERROR_FILE_NOT_FOUND,
          "a name is matched whatever its case, as on Windows");

    /* ---- the rest of the calls ---- */

    CHECK(RegSetValueExA(key, "gone", 0, REG_SZ, (const BYTE *)"x", 2) ==
              ERROR_SUCCESS,
          "a value written to delete");
    CHECK(RegDeleteValueA(key, "gone") == ERROR_SUCCESS, "and deleted");
    CHECK(RegDeleteValueA(key, "gone") == ERROR_FILE_NOT_FOUND,
          "and not there to delete twice");
    RegCloseKey(key);

    key = NULL;
    CHECK(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\neverWritten", 0,
                        KEY_READ, &key) == ERROR_FILE_NOT_FOUND,
          "a key nobody made does not open");
    CHECK(key == NULL, "and hands back no handle");
    CHECK(RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\ween32test", 0, NULL,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key,
                          &disp) == ERROR_SUCCESS &&
              disp == REG_OPENED_EXISTING_KEY,
          "creating a key that is there opens it, and says which happened");
    RegCloseKey(key);

    /* Bytes that are not text keep their type and come back whole. */
    {
        unsigned char blob[5] = { 0, 1, 2, 3, 255 };
        unsigned char back[5];
        key = NULL;
        RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\ween32test", 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL);
        RegSetValueExA(key, "blob", 0, REG_BINARY, blob, sizeof blob);
        ween_registry_forget();
        RegCloseKey(key);
        key = NULL;
        RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ween32test", 0, KEY_READ,
                      &key);
        size = sizeof back;
        CHECK(RegQueryValueExA(key, "blob", NULL, &type, back, &size) ==
                      ERROR_SUCCESS &&
                  type == REG_BINARY && size == sizeof blob &&
                  memcmp(back, blob, sizeof blob) == 0,
              "bytes that are not text survive the file whole");
        RegCloseKey(key);
    }

    ween_registry_forget();
    remove(g_path);

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("registry_test: all passed\n");
    return 0;
}
