/* The registry: where a Windows program keeps what it remembers.
 *
 * A program that has a font, a window position and a few ticked boxes does
 * not write a config file of its own -- it calls RegCreateKeyEx and
 * RegSetValueEx, and Windows finds somewhere to put them. Off Windows there
 * is nowhere, so this makes one: a file under the user's config directory,
 * written in the REGEDIT4 format regedit itself exports. That format is not
 * ours, it is readable, and a person who wants to see what a program has
 * remembered -- or change it -- can open it in an editor and will recognise
 * what they are looking at.
 *
 * What is here is what a program of this kind asks for: values under a key,
 * with REG_SZ and REG_DWORD spelled out and any other type kept as bytes.
 * What is deliberately not here is the rest of a registry -- enumerating
 * keys or values, deleting a key and its tree, security, remote hives, the
 * classes root meaning anything. A program needing those wants a registry
 * rather than somewhere to put its settings, and is better told so by a
 * missing symbol than half-answered by a call that pretends.
 *
 * The whole file is read once, on the first call, and written back whole
 * after every change. A settings file is a few hundred bytes and is written
 * when a program closes, so nothing here is worth a cache to go wrong.
 */

#define _POSIX_C_SOURCE 200112L /* mkdir */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ween_internal.h"

/* ---- what is held --------------------------------------------------------
 *
 * A value is its name, its type and its bytes; a key is a path and the
 * values under it. Keys are flat: "Software\ClassicNotepad" is a name, not a
 * tree walked one step at a time, which is all a program that opens the key
 * it wrote ever needs. */

typedef struct {
    char *name; /* "" is the key's default value, which regedit writes as @ */
    DWORD type;
    unsigned char *data;
    DWORD size;
} reg_value;

typedef struct {
    char *path; /* "HKEY_CURRENT_USER\Software\ClassicNotepad" */
    reg_value *value;
    int count, cap;
} reg_key;

struct HKEY__ {
    char *path;
};

static reg_key *g_key;
static int g_keys, g_keycap;
static int g_loaded;

/* Names and values are compared the way Windows compares them, which is
 * without regard to case. */
static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        int cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb)
            return 0;
    }
    return *a == *b;
}

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

/* ---- where it lives ------------------------------------------------------ */

/* The file, and the directory it wants to be in. WEEN32_REGISTRY names it
 * outright, which is what the tests use so that running them never touches
 * what a real program has stored. */
static char g_path[1024];
static int g_path_known;

static const char *reg_path(void)
{
    char *path = g_path;
    const char *env;
    if (g_path_known)
        return path[0] ? path : NULL;
    g_path_known = 1;
    env = getenv("WEEN32_REGISTRY");
    if (env && *env) {
        snprintf(path, sizeof g_path, "%s", env);
        return path;
    }
    env = getenv("XDG_CONFIG_HOME");
    if (env && *env)
        snprintf(path, sizeof g_path, "%s/ween32/registry.reg", env);
    else if ((env = getenv("HOME")) && *env)
        snprintf(path, sizeof g_path, "%s/.config/ween32/registry.reg", env);
    else
        path[0] = 0; /* nowhere to put it: the registry lives in memory only */
    return path[0] ? path : NULL;
}

/* Make the directories above the file, each one in turn; the ones that are
 * already there fail and are meant to. */
static void make_dirs(const char *file)
{
    char dir[1024];
    size_t n = strlen(file);
    if (n >= sizeof dir)
        return;
    memcpy(dir, file, n + 1);
    for (char *p = strchr(dir + 1, '/'); p; p = strchr(p + 1, '/')) {
        *p = 0;
        mkdir(dir, 0700);
        *p = '/';
    }
}

/* ---- keys and values in memory ------------------------------------------- */

static reg_key *key_find(const char *path)
{
    for (int i = 0; i < g_keys; i++)
        if (ieq(g_key[i].path, path))
            return &g_key[i];
    return NULL;
}

static reg_key *key_add(const char *path)
{
    reg_key *k;
    if (g_keys == g_keycap) {
        int cap = g_keycap ? g_keycap * 2 : 8;
        reg_key *grown = realloc(g_key, (size_t)cap * sizeof *grown);
        if (!grown)
            return NULL;
        g_key = grown;
        g_keycap = cap;
    }
    k = &g_key[g_keys];
    memset(k, 0, sizeof *k);
    k->path = dup_str(path);
    if (!k->path)
        return NULL;
    g_keys++;
    return k;
}

static reg_value *value_find(reg_key *k, const char *name)
{
    for (int i = 0; i < k->count; i++)
        if (ieq(k->value[i].name, name))
            return &k->value[i];
    return NULL;
}

static int value_set(reg_key *k, const char *name, DWORD type,
                     const unsigned char *data, DWORD size)
{
    reg_value *v = value_find(k, name);
    unsigned char *copy = malloc(size ? size : 1);
    if (!copy)
        return 0;
    if (size)
        memcpy(copy, data, size);
    if (!v) {
        if (k->count == k->cap) {
            int cap = k->cap ? k->cap * 2 : 8;
            reg_value *grown = realloc(k->value, (size_t)cap * sizeof *grown);
            if (!grown) {
                free(copy);
                return 0;
            }
            k->value = grown;
            k->cap = cap;
        }
        v = &k->value[k->count];
        memset(v, 0, sizeof *v);
        v->name = dup_str(name);
        if (!v->name) {
            free(copy);
            return 0;
        }
        k->count++;
    }
    free(v->data);
    v->data = copy;
    v->size = size;
    v->type = type;
    return 1;
}

/* ---- the file ------------------------------------------------------------
 *
 * REGEDIT4, which is the ANSI export regedit has written since NT 4:
 *
 *   REGEDIT4
 *
 *   [HKEY_CURRENT_USER\Software\ClassicNotepad]
 *   "lfFaceName"="Lucida Console"
 *   "iPointSize"=dword:00000064
 *   "whatever"=hex(3):01,02,03
 *
 * A string is written as text with \ and " escaped, a four-byte REG_DWORD as
 * eight hex digits, and everything else -- including a REG_SZ holding
 * something that is not text -- as its bytes. Long hex runs on one line
 * rather than being folded with a backslash: regedit reads that back, and
 * the settings a program of this size stores are short.
 */

static void write_escaped(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (*s == '\\' || *s == '"')
            fputc('\\', f);
        fputc(*s, f);
    }
}

/* Whether a value can be written as a string: the right type, and bytes that
 * really are one -- text ending in a NUL with none inside it. */
static int is_text(const reg_value *v)
{
    if (v->type != REG_SZ || v->size == 0 || v->data[v->size - 1] != 0)
        return 0;
    for (DWORD i = 0; i + 1 < v->size; i++)
        if (v->data[i] == 0)
            return 0;
    return 1;
}

/* Written beside the file and renamed over it, so that a program stopped
 * halfway through saving leaves the settings it had rather than half of
 * them. */
static void reg_save(void)
{
    const char *path = reg_path();
    char tmp[1080];
    FILE *f;
    if (!path)
        return;
    snprintf(tmp, sizeof tmp, "%s.new", path);
    f = fopen(tmp, "w");
    if (!f) {
        make_dirs(path);
        f = fopen(tmp, "w");
        if (!f)
            return;
    }
    fputs("REGEDIT4\n", f);
    for (int i = 0; i < g_keys; i++) {
        fprintf(f, "\n[%s]\n", g_key[i].path);
        for (int j = 0; j < g_key[i].count; j++) {
            const reg_value *v = &g_key[i].value[j];
            if (v->name[0]) {
                fputc('"', f);
                write_escaped(f, v->name);
                fputs("\"=", f);
            } else {
                fputs("@=", f);
            }
            if (is_text(v)) {
                fputc('"', f);
                write_escaped(f, (const char *)v->data);
                fputs("\"\n", f);
            } else if (v->type == REG_DWORD && v->size == 4) {
                DWORD d = (DWORD)v->data[0] | ((DWORD)v->data[1] << 8) |
                          ((DWORD)v->data[2] << 16) | ((DWORD)v->data[3] << 24);
                fprintf(f, "dword:%08lx\n", (unsigned long)d);
            } else {
                if (v->type == REG_BINARY)
                    fputs("hex:", f);
                else
                    fprintf(f, "hex(%lx):", (unsigned long)v->type);
                for (DWORD b = 0; b < v->size; b++)
                    fprintf(f, "%s%02x", b ? "," : "", v->data[b]);
                fputc('\n', f);
            }
        }
    }
    fclose(f);
    if (rename(tmp, path) != 0)
        remove(tmp);
}

/* A line's worth of parsing. Each returns where it stopped, so a malformed
 * line costs that line and nothing else. */

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* An escaped string up to its closing quote, into a buffer of its own. */
static char *read_quoted(const char **p)
{
    const char *s = *p;
    size_t n = 0;
    char *out;
    for (const char *q = s; *q && *q != '"'; q++) {
        if (*q == '\\' && q[1])
            q++;
        n++;
    }
    out = malloc(n + 1);
    if (!out)
        return NULL;
    n = 0;
    while (*s && *s != '"') {
        if (*s == '\\' && s[1])
            s++;
        out[n++] = *s++;
    }
    out[n] = 0;
    if (*s == '"')
        s++;
    *p = s;
    return out;
}

static void reg_load(void)
{
    const char *path = reg_path();
    FILE *f;
    char line[4096];
    reg_key *cur = NULL;
    g_loaded = 1;
    if (!path || !(f = fopen(path, "r")))
        return;
    while (fgets(line, (int)sizeof line, f)) {
        char *nl = strchr(line, '\n');
        const char *p = line;
        char *name = NULL;
        if (nl)
            *nl = 0;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '[') {
            char *end;
            p++;
            end = strrchr(p, ']');
            if (!end)
                continue;
            *end = 0;
            cur = key_find(p);
            if (!cur)
                cur = key_add(p);
            continue;
        }
        if (!cur)
            continue; /* REGEDIT4, a blank line, or a value before any key */
        if (*p == '"') {
            p++;
            name = read_quoted(&p);
        } else if (*p == '@') {
            p++;
            name = dup_str("");
        }
        if (!name)
            continue;
        if (*p == '=')
            p++;
        if (*p == '"') {
            char *text = (p++, read_quoted(&p));
            if (text)
                value_set(cur, name, REG_SZ, (unsigned char *)text,
                          (DWORD)strlen(text) + 1);
            free(text);
        } else if (strncmp(p, "dword:", 6) == 0) {
            unsigned long d = strtoul(p + 6, NULL, 16);
            unsigned char b[4];
            b[0] = (unsigned char)d;
            b[1] = (unsigned char)(d >> 8);
            b[2] = (unsigned char)(d >> 16);
            b[3] = (unsigned char)(d >> 24);
            value_set(cur, name, REG_DWORD, b, 4);
        } else if (strncmp(p, "hex", 3) == 0) {
            DWORD type = REG_BINARY, n = 0;
            unsigned char *bytes;
            p += 3;
            if (*p == '(') {
                type = (DWORD)strtoul(p + 1, (char **)&p, 16);
                if (*p == ')')
                    p++;
            }
            if (*p == ':')
                p++;
            bytes = malloc(strlen(p) / 2 + 1);
            while (bytes && *p) {
                int hi = hex_digit(*p), lo = p[1] ? hex_digit(p[1]) : -1;
                if (hi < 0 || lo < 0) {
                    p++;
                    continue;
                }
                bytes[n++] = (unsigned char)(hi * 16 + lo);
                p += 2;
            }
            if (bytes)
                value_set(cur, name, type, bytes, n);
            free(bytes);
        }
        free(name);
    }
    fclose(f);
}

static void reg_ready(void)
{
    if (!g_loaded)
        reg_load();
}

/* Drop everything held, so that the next call reads the file again. Not part
 * of the win32 API: it is what a test uses to prove that what was written
 * came back from the file rather than from memory. */
void ween_registry_forget(void)
{
    for (int i = 0; i < g_keys; i++) {
        for (int j = 0; j < g_key[i].count; j++) {
            free(g_key[i].value[j].name);
            free(g_key[i].value[j].data);
        }
        free(g_key[i].value);
        free(g_key[i].path);
    }
    free(g_key);
    g_key = NULL;
    g_keys = g_keycap = 0;
    g_loaded = 0;
    g_path_known = 0; /* WEEN32_REGISTRY is read again, which is what a test
                         changing it between rounds depends on */
}

/* ---- handles -------------------------------------------------------------
 *
 * The predefined keys are the numbers winreg.h gives them rather than
 * pointers to anything, so a handle is one of those or a little block naming
 * the path it stands for. */

/* Compared against the constants themselves rather than against the numbers
 * behind them: winreg.h's cast runs through a signed LONG, so on a 64-bit
 * build HKEY_CURRENT_USER is 0xffffffff80000001 and a table of 0x80000001
 * would match none of them -- and the next thing done with a handle that is
 * not a root is to read through it. */
static const char *root_name(HKEY key)
{
    if (key == HKEY_CLASSES_ROOT)
        return "HKEY_CLASSES_ROOT";
    if (key == HKEY_CURRENT_USER)
        return "HKEY_CURRENT_USER";
    if (key == HKEY_LOCAL_MACHINE)
        return "HKEY_LOCAL_MACHINE";
    if (key == HKEY_USERS)
        return "HKEY_USERS";
    if (key == HKEY_CURRENT_CONFIG)
        return "HKEY_CURRENT_CONFIG";
    return NULL;
}

static const char *key_path(HKEY key)
{
    const char *root = root_name(key);
    if (root)
        return root;
    return key ? key->path : NULL;
}

/* The full path a call names: the handle's, then the subkey under it. */
static int build_path(HKEY key, LPCSTR sub, char *out, size_t max)
{
    const char *base = key_path(key);
    if (!base)
        return 0;
    if (sub && *sub)
        return snprintf(out, max, "%s\\%s", base, sub) < (int)max;
    return snprintf(out, max, "%s", base) < (int)max;
}

static LSTATUS open_handle(const char *path, PHKEY out)
{
    HKEY h = calloc(1, sizeof *h);
    if (!h)
        return ERROR_ACCESS_DENIED;
    h->path = dup_str(path);
    if (!h->path) {
        free(h);
        return ERROR_ACCESS_DENIED;
    }
    *out = h;
    return ERROR_SUCCESS;
}

/* ---- the calls ------------------------------------------------------------ */

LSTATUS RegCreateKeyExA(HKEY key, LPCSTR sub, DWORD reserved, LPSTR cls,
                        DWORD options, REGSAM access, void *security,
                        PHKEY out, DWORD *disposition)
{
    char path[1024];
    int existed;
    (void)reserved;
    (void)cls;
    (void)options;
    (void)access;
    (void)security;
    if (!out)
        return ERROR_INVALID_PARAMETER;
    *out = NULL;
    reg_ready();
    if (!build_path(key, sub, path, sizeof path))
        return ERROR_INVALID_PARAMETER;
    existed = key_find(path) != NULL;
    if (!existed && !key_add(path))
        return ERROR_ACCESS_DENIED;
    if (disposition)
        *disposition = existed ? REG_OPENED_EXISTING_KEY : REG_CREATED_NEW_KEY;
    return open_handle(path, out);
}

LSTATUS RegOpenKeyExA(HKEY key, LPCSTR sub, DWORD options, REGSAM access,
                      PHKEY out)
{
    char path[1024];
    (void)options;
    (void)access;
    if (!out)
        return ERROR_INVALID_PARAMETER;
    *out = NULL;
    reg_ready();
    if (!build_path(key, sub, path, sizeof path))
        return ERROR_INVALID_PARAMETER;
    if (!key_find(path))
        return ERROR_FILE_NOT_FOUND;
    return open_handle(path, out);
}

LSTATUS RegCloseKey(HKEY key)
{
    if (!key || root_name(key)) /* closing a predefined key is a no-op */
        return ERROR_SUCCESS;
    free(key->path);
    free(key);
    return ERROR_SUCCESS;
}

LSTATUS RegQueryValueExA(HKEY key, LPCSTR name, DWORD *reserved, DWORD *type,
                         BYTE *data, DWORD *size)
{
    reg_key *k;
    reg_value *v;
    const char *path;
    (void)reserved;
    reg_ready();
    path = key_path(key);
    if (!path)
        return ERROR_INVALID_HANDLE;
    k = key_find(path);
    v = k ? value_find(k, name ? name : "") : NULL;
    if (!v)
        return ERROR_FILE_NOT_FOUND;
    if (type)
        *type = v->type;
    /* No buffer, or a null one, asks how big it would have to be -- which is
     * how a program finds out what to allocate. */
    if (!size)
        return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!data) {
        *size = v->size;
        return ERROR_SUCCESS;
    }
    if (*size < v->size) {
        *size = v->size;
        return ERROR_MORE_DATA;
    }
    memcpy(data, v->data, v->size);
    *size = v->size;
    return ERROR_SUCCESS;
}

LSTATUS RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type,
                       const BYTE *data, DWORD size)
{
    reg_key *k;
    const char *path;
    (void)reserved;
    reg_ready();
    path = key_path(key);
    if (!path)
        return ERROR_INVALID_HANDLE;
    k = key_find(path);
    if (!k)
        k = key_add(path); /* a predefined key written to directly */
    if (!k || (size && !data))
        return ERROR_INVALID_PARAMETER;
    if (!value_set(k, name ? name : "", type, data, size))
        return ERROR_ACCESS_DENIED;
    reg_save();
    return ERROR_SUCCESS;
}

LSTATUS RegDeleteValueA(HKEY key, LPCSTR name)
{
    reg_key *k;
    const char *path;
    reg_ready();
    path = key_path(key);
    if (!path)
        return ERROR_INVALID_HANDLE;
    k = key_find(path);
    for (int i = 0; k && i < k->count; i++) {
        if (!ieq(k->value[i].name, name ? name : ""))
            continue;
        free(k->value[i].name);
        free(k->value[i].data);
        k->value[i] = k->value[--k->count];
        reg_save();
        return ERROR_SUCCESS;
    }
    return ERROR_FILE_NOT_FOUND;
}
