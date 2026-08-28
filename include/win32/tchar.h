/* <tchar.h> off Windows: the T-names for the C library, in their ANSI form.
 *
 * The API half of the same job is in ween32.h; this is the run-time half --
 * the string and printf family a TCHAR program calls. ween32 is an A-API
 * library, so each one is the narrow name it was always going to be.
 */
#ifndef WEEN_TCHAR_H
#define WEEN_TCHAR_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#if defined(UNICODE) || defined(_UNICODE)
#error "ween32 is an ANSI (A-API) library: build without UNICODE/_UNICODE."
#endif

#define _tcslen strlen
#define _tcsnlen strnlen
#define _tcscpy strcpy
#define _tcsncpy strncpy
#define _tcscat strcat
#define _tcsncat strncat
#define _tcscmp strcmp
#define _tcsncmp strncmp
#define _tcsicmp strcasecmp
#define _tcsnicmp strncasecmp
#define _tcschr strchr
#define _tcsrchr strrchr
#define _tcsstr strstr
#define _tcstok strtok
#define _tcsdup strdup
#define _tprintf printf
#define _ftprintf fprintf
#define _stprintf sprintf
#define _sntprintf snprintf
#define _vstprintf vsprintf
#define _vsntprintf vsnprintf
#define _tfopen fopen
#define _tremove remove
#define _trename rename
#define _ttoi atoi
#define _ttol atol
#define _tcstol strtol
#define _tcstoul strtoul
#define _tcstod strtod
#define _totupper toupper
#define _totlower tolower
#define _istdigit isdigit
#define _istspace isspace
#define _istalpha isalpha

#endif /* WEEN_TCHAR_H */
