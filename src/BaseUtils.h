#ifndef _BASE_UTILS_H_
#define _BASE_UTILS_H_

#include <stddef.h>
#include <limits.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define dimof(X)    (sizeof(X)/sizeof((X)[0]))

#ifdef _WIN32
#ifndef WIN32
#define WIN32
#endif
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifdef WIN32
#define DBG_OUT win32_dbg_out
#else
#define DBG_OUT printf
#endif

#ifndef WIN32
#define stricmp strcasecmp
#endif

#define MAX_FILENAME_SIZE 1024

#ifdef _WIN32
void    win32_dbg_out(const char *format, ...);
#endif

#define DIR_SEP_CHAR '/'
#define DIR_SEP_STR  "/"

enum PdfDisplayMode {
    DM_SINGLE_PAGE = 1,
    DM_CONTINUOUS
};

/* TODO: consider using standard C macros for SWAP and MIN */
void        SwapInt(int *one, int *two);
void        SwapDouble(double *one, double *two);
int         MinInt(int one, int two);

#ifndef WIN32 /* TODO: should probably be based on MSVC version */
/* Re-implementation of Visual Studio C runtime function for c libs that don't
   have it. */
void    strcpy_s(char *dst, size_t dstLen, char *src);
#endif

int     Char_IsWs(char c);
int     Char_IsWsOrZero(char c);
int     Str_Empty(const char *txt);
char *  Str_New3(const char *str1, const char *str2, const char *str3);
char *  Str_New2(const char *str1, const char *str2);
char *  Str_New(const char *str1);
char *  Str_DupN(const char *str, size_t len);
char *  Str_Dup(const char *str);
int     Str_Eq(const char *str1, const char *str2);
int     Str_EqNoCase(const char *str1, const char *str2);
char *  Str_Printf(const char* pFormat, ...);
int     Str_EndsWithNoCase(const char *txt, const char *end);
void    Str_StripWs(char *str);
void    Str_SkipWs(char **txtInOut);
char *  Str_ParseQuoted(char **txt);
char *  Str_ParseNonQuoted(char **txt);
char *  Str_ParsePossiblyQuoted(char **txt);

char *  Str_PathJoin(const char *path, const char *fileName);

char *  CanonizeAbsolutePath(const char *path);

#ifdef __cplusplus
}
#endif

#endif
