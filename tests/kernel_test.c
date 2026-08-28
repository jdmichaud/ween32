/* KERNEL32's smaller corners: memory a handle stands for, the clock, and the
 * two text encodings.
 *
 * None of these draws anything, and all of them are things a win32 program
 * reaches for without thinking -- so what is checked here is that each one
 * behaves the way the call it stands in for behaves, down to what it returns
 * when the caller is only asking how much room an answer needs.
 */

#define _POSIX_C_SOURCE 200112L

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

int main(void)
{
    /* ---- memory ---- */
    {
        char *p = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 64);
        CHECK(p != NULL, "GlobalAlloc gives a block");
        CHECK(GlobalLock(p) == p, "locking a fixed block hands it back");
        CHECK(GlobalSize(p) == 64, "and it knows how big it is");
        {
            int zeroed = 1;
            for (int i = 0; i < 64; i++)
                if (p[i])
                    zeroed = 0;
            CHECK(zeroed, "GMEM_ZEROINIT means what it says");
        }
        strcpy(p, "kept");
        GlobalUnlock(p);
        CHECK(strcmp(p, "kept") == 0, "unlocking does not take it away");
        CHECK(GlobalFree(p) == NULL, "and freeing it answers with nothing");

        p = LocalAlloc(LPTR, 8);
        CHECK(p != NULL && LocalLock(p) == p && LocalSize(p) == 8,
              "the Local half is the same block by another name");
        LocalUnlock(p);
        CHECK(LocalFree(p) == NULL, "and frees the same way");
    }

    /* ---- the clock ---- */
    {
        SYSTEMTIME st;
        memset(&st, 0, sizeof st);
        GetLocalTime(&st);
        CHECK(st.wYear >= 2020 && st.wYear < 2200, "the year is a year");
        CHECK(st.wMonth >= 1 && st.wMonth <= 12, "the month is one of twelve");
        CHECK(st.wDay >= 1 && st.wDay <= 31, "and the day one of the month's");
        CHECK(st.wHour < 24 && st.wMinute < 60 && st.wSecond < 60,
              "the time of day is a time of day");
        CHECK(st.wDayOfWeek < 7, "and Sunday is nought, as win32 counts");
    }

    /* ---- writing a date and a time ---- */
    {
        SYSTEMTIME st;
        char buf[64];
        int n;
        memset(&st, 0, sizeof st);
        st.wYear = 2007;
        st.wMonth = 3;
        st.wDay = 9;
        st.wHour = 14;
        st.wMinute = 5;
        st.wSecond = 30;

        n = GetDateFormatA(LOCALE_USER_DEFAULT, 0, &st, "yyyy-MM-dd", buf,
                           sizeof buf);
        CHECK(strcmp(buf, "2007-03-09") == 0, "a date written to a picture");
        CHECK(n == (int)strlen(buf) + 1,
              "and the count includes the terminator, as win32's does");
        GetDateFormatA(LOCALE_USER_DEFAULT, 0, &st, "d/M/yy", buf, sizeof buf);
        CHECK(strcmp(buf, "9/3/07") == 0, "one letter is the short form");
        GetDateFormatA(LOCALE_USER_DEFAULT, 0, &st, "'on' d MMM yyyy", buf,
                       sizeof buf);
        CHECK(strncmp(buf, "on 9 ", 5) == 0 && strstr(buf, "2007"),
              "quoted words are copied rather than read");

        GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &st, "HH:mm:ss", buf, sizeof buf);
        CHECK(strcmp(buf, "14:05:30") == 0, "a time written to a picture");
        GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &st, "h:mm tt", buf, sizeof buf);
        CHECK(strcmp(buf, "2:05 PM") == 0, "the twelve-hour clock with its mark");
        st.wHour = 0;
        GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &st, "h:mm tt", buf, sizeof buf);
        CHECK(strcmp(buf, "12:05 AM") == 0, "and midnight is twelve, not nought");

        st.wHour = 14;
        n = GetTimeFormatA(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, buf,
                           sizeof buf);
        CHECK(n > 0 && strchr(buf, ':') != NULL && strlen(buf) <= 8,
              "no picture and TIME_NOSECONDS is the clock without seconds");
        n = GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, buf,
                           sizeof buf);
        CHECK(n > 0 && strstr(buf, "07") != NULL,
              "and DATE_SHORTDATE the date the machine is set to write");
        CHECK(GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL,
                             NULL, 0) > 0,
              "asking with no buffer says how much room it needs");
    }

    /* ---- the two encodings ---- */
    {
        /* Two characters that need two and three bytes in UTF-8: a pound
         * sign, which Latin-1 also has, and a Greek pi, which it has not. */
        static const char utf8[] = "a\xc2\xa3\xcf\x80";
        WCHAR wide[8];
        char back[16];
        int n;

        n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
        CHECK(n == 4, "asking with no buffer counts the characters, and the NUL");
        n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 8);
        CHECK(n == 4 && wide[0] == 'a' && wide[1] == 0xA3 && wide[2] == 0x3C0 &&
                  wide[3] == 0,
              "UTF-8 comes in as the characters it spells");

        n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, back, sizeof back, NULL,
                                NULL);
        CHECK(n == (int)sizeof utf8 && memcmp(back, utf8, sizeof utf8) == 0,
              "and goes back out as the same bytes");

        {
            BOOL used = FALSE;
            n = WideCharToMultiByte(CP_ACP, 0, wide, -1, back, sizeof back,
                                    NULL, &used);
            CHECK(n == 4 && (unsigned char)back[1] == 0xA3,
                  "Latin-1 keeps what it can");
            CHECK(back[2] == '?' && used == TRUE,
                  "and says so when it cannot, which is a question mark");
        }

        n = MultiByteToWideChar(CP_ACP, 0, "\xe9t\xe9", 3, wide, 8);
        CHECK(n == 3 && wide[0] == 0xE9, "a Latin-1 byte is that character");

        /* A character outside the sixteen-bit range is two WCHARs there and
         * four bytes here, and has to survive both ways. */
        {
            static const char emoji[] = "\xf0\x9f\x92\xa9";
            WCHAR pair[4];
            char eight[8];
            n = MultiByteToWideChar(CP_UTF8, 0, emoji, 4, pair, 4);
            CHECK(n == 2 && pair[0] >= 0xD800 && pair[0] < 0xDC00 &&
                      pair[1] >= 0xDC00 && pair[1] < 0xE000,
                  "a character past the sixteen-bit range is a surrogate pair");
            n = WideCharToMultiByte(CP_UTF8, 0, pair, 2, eight, sizeof eight,
                                    NULL, NULL);
            CHECK(n == 4 && memcmp(eight, emoji, 4) == 0,
                  "and the pair goes back to the four bytes it came from");
        }

        CHECK(MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 2) == 0,
              "a buffer too small answers with nothing, as win32 does");
    }

    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("kernel_test: all passed\n");
    return 0;
}
