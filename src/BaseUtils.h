#ifndef _BASE_UTILS_H_
#define _BASE_UTILS_H_

#include <stddef.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#endif

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

#ifndef BOOL
#define BOOL int
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef INVALID_FILE_SIZE
#define INVALID_FILE_SIZE (unsigned long)-1
#endif

#ifdef DEBUG
  #ifdef _WIN32
  #define DBG_OUT win32_dbg_out
  #else
  #define DBG_OUT printf
  #endif
#else
  #define DBG_OUT
#endif

#ifndef _WIN32
#define stricmp strcasecmp
#endif

#define MAX_FILENAME_SIZE 1024

#ifdef _WIN32
void    win32_dbg_out(const char *format, ...);
#endif

#define DIR_SEP_CHAR '/'
#define DIR_SEP_STR  "/"

#define UNIX_NEWLINE "\x0a"
#define UNIX_NEWLINE_C 0xa

#define WHITE_SPACE_CHARS " \n\t\r"

/* TODO: consider using standard C macros for SWAP and MIN */
void        SwapInt(int *one, int *two);
void        SwapDouble(double *one, double *two);
int         MinInt(int one, int two);

#ifndef _WIN32 /* TODO: should probably be based on MSVC version */
/* Re-implementation of Visual Studio C runtime function for c libs that don't
   have it. */
void    strcpy_s(char *dst, size_t dstLen, const char *src);
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
int     Str_EndsWithNoCase(const char *txt, const char *end);

char *  Str_Printf(const char* pFormat, ...);

void    Str_StripWsLeft(char *str);
void    Str_StripWsRight(char *str);
void    Str_StripWsBoth(char *str);
void    Str_SkipWs(char **txtInOut);

char *  Str_SplitIter(char **txt, char c);

char *  Str_NormalizeNewline(char *txt, const char *replace);

char *  Str_ParseQuoted(char **txt);
char *  Str_ParseNonQuoted(char **txt);
char *  Str_ParsePossiblyQuoted(char **txt);
char *  Str_Escape(const char *txt);

char *  Str_PathJoin(const char *path, const char *fileName);

char *  CanonizeAbsolutePath(const char *path);

unsigned long File_GetSize(const char *file_name);
char *  File_Slurp(const char *file_name, unsigned long *file_size_out);

#ifdef __cplusplus
}
#endif

#endif
