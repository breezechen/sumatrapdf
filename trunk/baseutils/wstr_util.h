/* Written by Krzysztof Kowalczyk (http://blog.kowalczyk.info)
   The author disclaims copyright to this source code. */
#ifndef WSTR_UTIL_H_
#define WSTR_UTIL_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define wchar_is_ws iswspace
#define wchar_is_digit iswdigit

#define wstr_len wcslen
#define wstrlen wcslen
#define wstr_dup _wcsdup
#define wstr_find_char wcschr

int     wstr_eq(const WCHAR *str1, const WCHAR *str2);
int     wstr_ieq(const WCHAR *str1, const WCHAR *str2);
int     wstr_eqn(const WCHAR *str1, const WCHAR *str2, int len);
int     wstr_startswith(const WCHAR *str, const WCHAR *txt);
int     wstr_startswithi(const WCHAR *str, const WCHAR *txt);
int     wstr_endswith(const WCHAR *str, const WCHAR *end);
int     wstr_endswithi(const WCHAR *str, const WCHAR *end);
int     wstr_empty(const WCHAR *str);
int     wstr_copy(WCHAR *dst, size_t dst_cch_size, const WCHAR *src);
int     wstr_copyn(WCHAR *dst, size_t dst_cch_size, const WCHAR *src, size_t src_cch_size);
WCHAR * wstr_dupn(const WCHAR *str, size_t str_len_cch);
WCHAR * wstr_cat_s(WCHAR *dst, size_t dst_cch_size, const WCHAR *src);
WCHAR * wstr_catn_s(WCHAR *dst, size_t dst_cch_size, const WCHAR *src, size_t src_cch_size);
WCHAR * wstr_cat(const WCHAR *str1, const WCHAR *str2);
WCHAR * wstr_cat3(const WCHAR *str1, const WCHAR *str2, const WCHAR *str3);
int     wstr_contains(const WCHAR *str, WCHAR c);
WCHAR * wstr_printf(const WCHAR *format, ...);
int     wstr_printf_s(WCHAR *out, size_t out_cch_size, const WCHAR *format, ...);
BOOL    wstr_dup_replace(WCHAR **dst, const WCHAR *src);

char *  wstr_to_multibyte(const WCHAR *txt,  UINT CodePage);
char *  wstr_to_utf8(const WCHAR *txt);
WCHAR * multibyte_to_wstr(const char *src, UINT CodePage);
WCHAR * utf8_to_wstr(const char *utf8);

#ifdef _WIN32
void win32_dbg_outW(const WCHAR *format, ...);
#endif
#ifdef DEBUG
  #ifdef _WIN32
    #define DBG_OUT_W(format, ...) win32_dbg_outW(L##format, __VA_ARGS__)
  #else
    #define DBG_OUT_W(format, ...) wprintf(L##format, __VA_ARGS__)
  #endif
  void wstr_util_test(void);
#else
  #define DBG_OUT_W(format, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif
#endif
