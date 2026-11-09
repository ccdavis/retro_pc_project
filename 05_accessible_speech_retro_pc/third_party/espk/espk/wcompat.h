/* wcompat.h - DJGPP: route the wide-character calls in the espeak sources to
 * ucdmini.c (DJGPP 2.05 has no wide string or wide ctype functions). */
#ifndef ESPK_WCOMPAT_H
#define ESPK_WCOMPAT_H
#include <stddef.h>
size_t espk_wcslen(const wchar_t *s);
wchar_t *espk_wcschr(const wchar_t *s, wchar_t c);
int espk_wcsncmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t *espk_wcsncpy(wchar_t *d, const wchar_t *s, size_t n);
int espk_iswalpha(int c); int espk_iswdigit(int c); int espk_iswspace(int c);
int espk_iswupper(int c); int espk_iswlower(int c); int espk_iswalnum(int c);
int espk_iswpunct(int c); int espk_towlower(int c); int espk_towupper(int c);
#define wcslen espk_wcslen
#define wcschr espk_wcschr
#define wcsncmp espk_wcsncmp
#define wcsncpy espk_wcsncpy
#define iswalpha espk_iswalpha
#define iswdigit espk_iswdigit
#define iswspace espk_iswspace
#define iswupper espk_iswupper
#define iswlower espk_iswlower
#define iswalnum espk_iswalnum
#define iswpunct espk_iswpunct
#define towlower espk_towlower
#define towupper espk_towupper
#endif
