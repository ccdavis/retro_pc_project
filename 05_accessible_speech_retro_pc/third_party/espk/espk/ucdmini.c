/* ucdmini.c - a small replacement for espeak-ng's ucd-tools (740 KB of
 * Unicode tables): general category and case mapping for U+0000..U+052F
 * from a generated table (espk/ucdtab.h), the espeak-specific punctuation
 * properties for the characters English text uses, and the wide-character
 * ctype/string functions DJGPP's libc lacks (wcompat.h). */
#include "config.h"
#include <stdint.h>
#include <wchar.h>
#include <wctype.h>
#include <ucd/ucd.h>
#include "ucdtab.h"

ucd_category ucd_lookup_category(codepoint_t c)
{
	if (c < UCDTAB_N) return (ucd_category)ucd_cat_tab[c];
	if (c >= 0x2000 && c <= 0x200A) return UCD_CATEGORY_Zs;
	if (c == 0x2028) return UCD_CATEGORY_Zl;
	if (c == 0x2029) return UCD_CATEGORY_Zp;
	if (c >= 0x2010 && c <= 0x2015) return UCD_CATEGORY_Pd;
	if (c >= 0x2016 && c <= 0x2027) return UCD_CATEGORY_Po;
	if (c == 0x2018 || c == 0x201C) return UCD_CATEGORY_Pi;
	if (c == 0x2019 || c == 0x201D) return UCD_CATEGORY_Pf;
	if (c >= 0x2030 && c <= 0x205E) return UCD_CATEGORY_Po;
	if (c >= 0x20A0 && c <= 0x20CF) return UCD_CATEGORY_Sc;
	if (c >= 0x2100 && c <= 0x214F) return UCD_CATEGORY_So;
	if (c >= 0x2190 && c <= 0x23FF) return UCD_CATEGORY_Sm;
	if (c >= 0x2500 && c <= 0x27BF) return UCD_CATEGORY_So;
	if (c >= 0xE000 && c <= 0xF8FF) return UCD_CATEGORY_Co;
	if (c >= 0x1F300 && c <= 0x1FAFF) return UCD_CATEGORY_So;
	return UCD_CATEGORY_Lo;
}

ucd_category_group ucd_get_category_group_for_category(ucd_category c)
{
	switch (c) {
	case UCD_CATEGORY_Cc: case UCD_CATEGORY_Cf: case UCD_CATEGORY_Cn: case UCD_CATEGORY_Co: case UCD_CATEGORY_Cs:
		return UCD_CATEGORY_GROUP_C;
	case UCD_CATEGORY_Ll: case UCD_CATEGORY_Lm: case UCD_CATEGORY_Lo: case UCD_CATEGORY_Lt: case UCD_CATEGORY_Lu:
		return UCD_CATEGORY_GROUP_L;
	case UCD_CATEGORY_Mc: case UCD_CATEGORY_Me: case UCD_CATEGORY_Mn:
		return UCD_CATEGORY_GROUP_M;
	case UCD_CATEGORY_Nd: case UCD_CATEGORY_Nl: case UCD_CATEGORY_No:
		return UCD_CATEGORY_GROUP_N;
	case UCD_CATEGORY_Pc: case UCD_CATEGORY_Pd: case UCD_CATEGORY_Pe: case UCD_CATEGORY_Pf: case UCD_CATEGORY_Pi: case UCD_CATEGORY_Po: case UCD_CATEGORY_Ps:
		return UCD_CATEGORY_GROUP_P;
	case UCD_CATEGORY_Sc: case UCD_CATEGORY_Sk: case UCD_CATEGORY_Sm: case UCD_CATEGORY_So:
		return UCD_CATEGORY_GROUP_S;
	case UCD_CATEGORY_Zl: case UCD_CATEGORY_Zp: case UCD_CATEGORY_Zs:
		return UCD_CATEGORY_GROUP_Z;
	default:
		return UCD_CATEGORY_GROUP_I;
	}
}

ucd_category_group ucd_lookup_category_group(codepoint_t c)
{
	return ucd_get_category_group_for_category(ucd_lookup_category(c));
}

ucd_script ucd_lookup_script(codepoint_t c)
{
	if (c < 0x370) return UCD_SCRIPT_Latn;
	if (c < 0x400) return UCD_SCRIPT_Grek;
	if (c < 0x530) return UCD_SCRIPT_Cyrl;
	return UCD_SCRIPT_Zyyy;
}

ucd_property ucd_properties(codepoint_t c, ucd_category category)
{
	(void)category;
	switch (c) {
	case 0x21: return UCD_PROPERTY_TERMINAL_PUNCTUATION | UCD_PROPERTY_SENTENCE_TERMINAL | ESPEAKNG_PROPERTY_EXCLAMATION_MARK;
	case 0x2C: return UCD_PROPERTY_TERMINAL_PUNCTUATION | ESPEAKNG_PROPERTY_COMMA;
	case 0x2E: return UCD_PROPERTY_TERMINAL_PUNCTUATION | UCD_PROPERTY_SENTENCE_TERMINAL | ESPEAKNG_PROPERTY_FULL_STOP;
	case 0x3A: return UCD_PROPERTY_TERMINAL_PUNCTUATION | ESPEAKNG_PROPERTY_COLON;
	case 0x3B: return UCD_PROPERTY_TERMINAL_PUNCTUATION | ESPEAKNG_PROPERTY_SEMI_COLON;
	case 0x3F: return UCD_PROPERTY_TERMINAL_PUNCTUATION | UCD_PROPERTY_SENTENCE_TERMINAL | ESPEAKNG_PROPERTY_QUESTION_MARK;
	case 0xA1: return ESPEAKNG_PROPERTY_EXCLAMATION_MARK | ESPEAKNG_PROPERTY_OPTIONAL_SPACE_AFTER | ESPEAKNG_PROPERTY_INVERTED_TERMINAL_PUNCTUATION;
	case 0xBF: return ESPEAKNG_PROPERTY_QUESTION_MARK | ESPEAKNG_PROPERTY_OPTIONAL_SPACE_AFTER | ESPEAKNG_PROPERTY_INVERTED_TERMINAL_PUNCTUATION;
	case 0x2013: case 0x2014: return UCD_PROPERTY_DASH | ESPEAKNG_PROPERTY_EXTENDED_DASH;
	case 0x2026: return ESPEAKNG_PROPERTY_ELLIPSIS;
	case 0x203C: return UCD_PROPERTY_TERMINAL_PUNCTUATION | UCD_PROPERTY_SENTENCE_TERMINAL | ESPEAKNG_PROPERTY_EXCLAMATION_MARK;
	case 0x2047: return UCD_PROPERTY_TERMINAL_PUNCTUATION | UCD_PROPERTY_SENTENCE_TERMINAL | ESPEAKNG_PROPERTY_QUESTION_MARK;
	case 0x2048: case 0x2049: return UCD_PROPERTY_TERMINAL_PUNCTUATION | UCD_PROPERTY_SENTENCE_TERMINAL | ESPEAKNG_PROPERTY_QUESTION_MARK | ESPEAKNG_PROPERTY_EXCLAMATION_MARK;
	case 0x2D: return UCD_PROPERTY_DASH;
	default: return 0;
	}
}

codepoint_t ucd_toupper(codepoint_t c) { return c < UCDTAB_N ? ucd_upper_tab[c] : c; }
codepoint_t ucd_tolower(codepoint_t c) { return c < UCDTAB_N ? ucd_lower_tab[c] : c; }
codepoint_t ucd_totitle(codepoint_t c) { return ucd_toupper(c); }

int ucd_isalnum(codepoint_t c) { ucd_category_group g = ucd_lookup_category_group(c); return g == UCD_CATEGORY_GROUP_L || g == UCD_CATEGORY_GROUP_N; }
int ucd_isalpha(codepoint_t c) { return ucd_lookup_category_group(c) == UCD_CATEGORY_GROUP_L; }
int ucd_isblank(codepoint_t c) { return c == ' ' || c == '\t'; }
int ucd_iscntrl(codepoint_t c) { return ucd_lookup_category(c) == UCD_CATEGORY_Cc; }
int ucd_isdigit(codepoint_t c) { return ucd_lookup_category(c) == UCD_CATEGORY_Nd; }
int ucd_isgraph(codepoint_t c) { ucd_category_group g = ucd_lookup_category_group(c); return g != UCD_CATEGORY_GROUP_C && g != UCD_CATEGORY_GROUP_Z; }
int ucd_islower(codepoint_t c) { return ucd_lookup_category(c) == UCD_CATEGORY_Ll; }
int ucd_isprint(codepoint_t c) { return ucd_lookup_category_group(c) != UCD_CATEGORY_GROUP_C; }
int ucd_ispunct(codepoint_t c) { return ucd_lookup_category_group(c) == UCD_CATEGORY_GROUP_P; }
int ucd_isspace(codepoint_t c) { return ucd_lookup_category_group(c) == UCD_CATEGORY_GROUP_Z || (c >= 9 && c <= 13); }
int ucd_isupper(codepoint_t c) { return ucd_lookup_category(c) == UCD_CATEGORY_Lu; }
int ucd_isxdigit(codepoint_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

#ifdef __DJGPP__
/* wide-character functions missing from DJGPP's libc (wchar_t is 16 bit there) */
size_t espk_wcslen(const wchar_t *s) { size_t n = 0; while (s[n]) n++; return n; }
wchar_t *espk_wcschr(const wchar_t *s, wchar_t c) { for (;; s++) { if (*s == c) return (wchar_t *)s; if (!*s) return 0; } }
int espk_wcsncmp(const wchar_t *a, const wchar_t *b, size_t n) { for (; n; n--, a++, b++) { if (*a != *b) return (int)*a - (int)*b; if (!*a) return 0; } return 0; }
wchar_t *espk_wcsncpy(wchar_t *d, const wchar_t *s, size_t n) { size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d; }
int espk_iswalpha(int c) { return ucd_isalpha((codepoint_t)(unsigned)c); }
int espk_iswdigit(int c) { return ucd_isdigit((codepoint_t)(unsigned)c); }
int espk_iswspace(int c) { return ucd_isspace((codepoint_t)(unsigned)c); }
int espk_iswupper(int c) { return ucd_isupper((codepoint_t)(unsigned)c); }
int espk_iswlower(int c) { return ucd_islower((codepoint_t)(unsigned)c); }
int espk_iswalnum(int c) { return ucd_isalnum((codepoint_t)(unsigned)c); }
int espk_iswpunct(int c) { return ucd_ispunct((codepoint_t)(unsigned)c); }
int espk_towlower(int c) { return (int)ucd_tolower((codepoint_t)(unsigned)c); }
int espk_towupper(int c) { return (int)ucd_toupper((codepoint_t)(unsigned)c); }
#endif
