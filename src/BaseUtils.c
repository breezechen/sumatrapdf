#include "BaseUtils.h"

#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
#include <strsafe.h>
#else
#include <unistd.h>
#include <time.h>
#include <errno.h>
#endif
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

int Char_IsWsOrZero(char c)
{
    switch (c) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case 0:
            return TRUE;
    }
    return FALSE;
}


int Char_IsWs(char c)
{
    switch (c) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            return TRUE;
    }
    return FALSE;
}

int Char_IsDigit(char c)
{
    if ((c >= '0') && (c <= '9'))
        return TRUE;
    return FALSE;
}

void memzero(void *dst, size_t len)
{
    if (len <= 0)
        return;
    memset(dst, 0, len);
}

int Str_Empty(const char *txt)
{
    if (!txt)
        return TRUE;
    if (0 == *txt)
        return TRUE;
    return FALSE;
}
int Str_ValidNumber(const char *txt)
{
    if (!txt)
        return FALSE;
    while (*txt) {
        if (!Char_IsDigit(*txt))
            return FALSE;
        ++txt;
    }
    return TRUE;
}

char *Str_New3(const char *str1, const char *str2, const char *str3)
{
    size_t  len1 = 0, len2 = 0, len3 = 0;
    char *  str, *tmp;

    if (str1)
        len1 = strlen(str1);
    if (str2)
        len2 = strlen(str2);
    if (str3)
        len3 = strlen(str3);

    str = (char*)malloc(len1 + len2 + len3 + 1);
    if (!str)
        return NULL;
    tmp = str;
    if (str1) {
        memcpy(tmp, str1, len1);
        tmp += len1;
    }
    if (str2) {
        memcpy(tmp, str2, len2);
        tmp += len2;
    }
    if (str3) {
        memcpy(tmp, str3, len3);
        tmp += len3;
    }
    *tmp = 0;
    return str;
}

char *Str_New2(const char *str1, const char *str2)
{
    return Str_New3(str1, str2, NULL);
}

char *Str_New(const char *str1)
{
    return Str_New3(str1, NULL, NULL);
}

char *Str_DupN(const char *str, size_t len)
{
    char *  str_new;
    if (!str)
        return NULL;

    str_new = (char*)malloc(len+1);
    if (!str_new)
        return NULL;

    memcpy(str_new, str, len);
    str_new[len] = 0;
    return str_new;
}

char *Str_Dup(const char *str)
{
    size_t  len;
    if (!str)
        return NULL;

    len = strlen(str);
    return Str_DupN(str, len);
}

int Str_Eq(const char *str1, const char *str2)
{
    if (!str1 && !str2)
        return TRUE;
    if (!str1 || !str2)
        return FALSE;
    if (0 == strcmp(str1, str2))
        return TRUE;
    return FALSE;
}

int Str_EqNoCase(const char *str1, const char *str2)
{
    if (!str1 && !str2)
        return TRUE;
    if (!str1 || !str2)
        return FALSE;
    if (0 == stricmp(str1, str2))
        return TRUE;
    return FALSE;
}

char *Str_Printf(const char *format, ...)
{
#ifdef _WIN32
    HRESULT     hr;
    va_list     args;
    char        message[256];
    char  *     buf;
    size_t      bufCchSize;
    char *      result = NULL;

    buf = &(message[0]);
    bufCchSize = sizeof(message);

    va_start(args, format);
    for (;;)
    {
        /* TODO: this only works on windows with recent C library */
        hr = StringCchVPrintfA(buf, bufCchSize, format, args);
        if (S_OK == hr)
            break;
        if (STRSAFE_E_INSUFFICIENT_BUFFER != hr)
        {
            /* any error other than buffer not big enough:
               a) should not happen
               b) means we give up */
            assert(FALSE);
            goto Error;
        }
        /* we have to make the buffer bigger. The algorithm used to calculate
           the new size is arbitrary (aka. educated guess) */
        if (buf != &(message[0]))
            free(buf);
        if (bufCchSize < 4*1024)
            bufCchSize += bufCchSize;
        else
            bufCchSize += 1024;
        buf = (char *)malloc(bufCchSize*sizeof(char));
        if (NULL == buf)
            goto Error;
    }
    va_end(args);

    /* free the buffer if it was dynamically allocated */
    if (buf == &(message[0]))
        return Str_Dup(buf);

    return buf;
Error:
    if (buf != &(message[0]))
        free((void*)buf);

    return NULL;
#else
  va_list args;
  char*   pBuffer;
  int     len;

  va_start(args, format);
  len = vasprintf(&pBuffer, format, args);
  return pBuffer;
#endif
}

/* Return TRUE if string 'txt' ends with string 'end', case-insensitive */
int Str_EndsWithNoCase(const char *txt, const char *end)
{
    size_t end_len;
    size_t txt_len;

    if (!txt || !end)
        return FALSE;

    txt_len = strlen(txt);
    end_len = strlen(end);
    if (end_len > txt_len)
        return FALSE;
    if (Str_EqNoCase(txt+txt_len-end_len, end))
        return TRUE;
    return FALSE;
}

/* Return TRUE if string <txt> starts with string <start>, case-insensitive */
int Str_StartsWithNoCase(const char *txt, const char *start)
{
    size_t txt_len;
    size_t start_len;

    if (!txt || !start)
        return FALSE;
    txt_len = strlen(txt);
    start_len = strlen(start);
    if (start_len > txt_len)
        return FALSE;
    if (0 == strnicmp(txt, start, start_len))
        return TRUE;
    return FALSE;    
}

int Str_Contains(const char *str, char c)
{
    const char *pos = strchr(str, c);
    if (!pos)
        return FALSE;
    return TRUE;
}

/* Strip all 'to_strip' characters from the beginning of the string.
   Does stripping in-place */
void Str_StripLeft(char *txt, const char *to_strip)
{
    char *new_start = txt;
    char c;
    if (!txt || !to_strip)
        return;
    for (;;) {
        c = *new_start;
        if (0 == c)
            break;
        if (!Str_Contains(to_strip, c))
            break;
        ++new_start;
    }

    if (new_start != txt) {
        memmove(txt, new_start, strlen(new_start)+1);
    }
}

/* Strip white-space characters from the beginning of the string.
   Does stripping in-place */
void Str_StripWsLeft(char *txt)
{
    Str_StripLeft(txt, WHITE_SPACE_CHARS);
}

void Str_StripRight(char *txt, const char *to_strip)
{
    char * new_end;
    char   c;
    if (!txt || !to_strip)
        return;
    if (0 == *txt)
        return;
    /* point at the last character in the string */
    new_end = txt + strlen(txt) - 1;
    for (;;) {
        c = *new_end;
        if (!Str_Contains(to_strip, c))
            break;
        if (txt == new_end)
            break;
        --new_end;
    }
    if (Str_Contains(to_strip, *new_end))
        new_end[0] = 0;
    else
        new_end[1] = 0;
}

void Str_StripWsRight(char *txt)
{
    Str_StripRight(txt, WHITE_SPACE_CHARS);
}

void Str_StripBoth(char *txt, const char *to_strip)
{
    Str_StripLeft(txt, to_strip);
    Str_StripRight(txt, to_strip);
}

void Str_StripWsBoth(char *txt)
{
    Str_StripWsLeft(txt);
    Str_StripWsRight(txt);
}


/* Given a pointer to a string in '*txt', skip past whitespace in the string
   and put the result in '*txt' */
void Str_SkipWs(char **txtInOut)
{
    char *cur;
    if (!txtInOut)
        return;
    cur = *txtInOut;
    if (!cur)
        return;
    while (Char_IsWs(*cur)) {
        ++cur;
    }
    *txtInOut = cur;
}

char *Str_ParseQuoted(char **txt)
{
    char *      strStart;
    char *      strCopy;
    char *      cur;
    char *      dst;
    char        c;
    size_t      len;

    assert(txt);
    if (!txt) return NULL;
    strStart = *txt;
    assert(strStart);
    if (!strStart) return NULL;

    assert('"' == *strStart);
    ++strStart;
    cur = strStart;
    len = 0;
    for (;;) {
        c = *cur;
        if ((0 == c) || ('"' == c))
            break;
        if ('\\' == c) {
            /* TODO: should I un-escape everything or just '\' and '"' ? */
            if (('\\' == cur[1]) || ('"' == cur[1])) {
                ++cur;
                c = *cur;
            }
        }
        ++cur;
        ++len;
    }

    strCopy = (char*)malloc(len+1);
    if (!strCopy)
        return NULL;

    cur = strStart;
    dst = strCopy;
    for (;;) {
        c = *cur;
        if (0 == c)
            break;
        if ('"' == c) {
            ++cur;
            break;
        }
        if ('\\' == c) {
            /* TODO: should I un-escape everything or just '\' and '"' ? */
            if (('\\' == cur[1]) || ('"' == cur[1])) {
                ++cur;
                c = *cur;
            }
        }
        *dst++ = c;
        ++cur;
    }
    *dst = 0;
    *txt = cur;
    return strCopy;
}

char *Str_ParseNonQuoted(char **txt)
{
    char *  cur;
    char *  strStart;
    char *  strCopy;
    char    c;
    size_t  strLen;

    strStart = *txt;
    assert(strStart);
    if (!strStart) return NULL;
    assert('"' != *strStart);
    cur = strStart;
    for (;;) {
        c = *cur;
        if (Char_IsWsOrZero(c))
            break;
        ++cur;
    }

    strLen = cur - strStart;
    assert(strLen > 0);
    strCopy = Str_DupN(strStart, strLen);
    *txt = cur;
    return strCopy;
}

char *Str_ParsePossiblyQuoted(char **txt)
{
    char *  cur;
    char *  strCopy;

    if (!txt)
        return NULL;
    cur = *txt;
    if (!cur)
        return NULL;

    Str_SkipWs(&cur);
    if (0 == *cur)
        return NULL;
    if ('"' == *cur)
        strCopy = Str_ParseQuoted(&cur);
    else
        strCopy = Str_ParseNonQuoted(&cur);
    *txt = cur;
    return strCopy;
}

/* split a string '*txt' at the border character 'c'. Something like python's
   string.split() except called iteratively.
   Returns a copy of the string (must be free()d by the caller).
   Returns NULL to indicate there's no more items. */
char *Str_SplitIter(char **txt, char c)
{
    const char *tmp;
    const char *pos;
    char *result;

    tmp = (const char*)*txt;
    if (!tmp)
        return NULL;

    pos = strchr(tmp, c);
    if (pos) {
         result = Str_DupN(tmp, (int)(pos-tmp));
         *txt = (char*)pos+1;
    } else {
        result = Str_Dup(tmp);
        *txt = NULL; /* next iteration will return NULL */
    }
    return result;
}

/* Replace all posible versions (Unix, Windows, Mac) of newline character
   with 'replace'. Returns newly allocated string with normalized newlines
   or NULL if error.
   Caller needs to free() the result */
char *Str_NormalizeNewline(const char *txt, const char *replace)
{
    size_t          replace_len;
    char            c;
    char *          result;
    const char *    tmp;
    char *          tmpResult;
    size_t          result_len = 0;

    replace_len = strlen(replace);
    tmp = txt;
    for (;;) {
        c = *tmp++;
        if (!c)
            break;
        if (0xa == c) {
            /* a single 0xa => Unix */
            result_len += replace_len;
        } else if (0xd == c) {
            if (0xa == *tmp) {
                /* 0xd 0xa => dos */
                result_len += replace_len;
                ++tmp;
            }
            else {
                /* just 0xd => Mac */
                result_len += replace_len;
            }
        } else
            ++result_len;
    }

    if (0 == result_len)
        return NULL;

    result = (char*)malloc(result_len+1);
    if (!result)
        return NULL;
    tmpResult = result;
    for (;;) {
        c = *txt++;
        if (!c)
            break;
        if (0xa == c) {
            /* a single 0xa => Unix */
            memcpy(tmpResult, replace, replace_len);
            tmpResult += replace_len;
        } else if (0xd == c) {
            if (0xa == *txt) {
                /* 0xd 0xa => dos */
                memcpy(tmpResult, replace, replace_len);
                tmpResult += replace_len;
                ++txt;
            }
            else {
                /* just 0xd => Mac */
                memcpy(tmpResult, replace, replace_len);
                tmpResult += replace_len;
            }
        } else
            *tmpResult++ = c;
    }

    *tmpResult = 0;
    return result;
}


char *Str_Escape(const char *txt)
{
    /* TODO: */
    return Str_Dup(txt);
}

#ifndef _WIN32 /* TODO: should probably be based on MSVC version */
void strcpy_s(char *dst, size_t dstLen, const char *src)
{
    size_t  toCopy;

    assert(dst);
    assert(src);
    assert(dstLen > 0);

    if (!dst || !src || dstLen <= 0)
        return;

    toCopy = strlen(src);
    if (toCopy > (dstLen-1))
        toCopy = dstLen - 1;

    strncpy(dst, src, toCopy);
    dst[toCopy] = 0;
}
#endif

/* return TRUE if absolute path*/
static int IsAbsPath(const char *path)
{
    if (!path) return 0;
    if (*path == DIR_SEP_CHAR)
        return 1;
    return 0;
}

/* Suppress redundant ".", ".." and "/" from paths.
   Does it in place since the result is always smaller than the source. */
static void CanonizePathInPlace(const char *path)
{
    /* TODO: implement me */
    return;
}

static int IsUnixHome(const char *path)
{
    if (*path == '~' && (path[1] == '\0' || path[1] == DIR_SEP_CHAR))
        return TRUE;
    return FALSE;
}

char *Str_PathJoin(const char *path, const char *fileName)
{
    char *pathJoined;
    if (Str_Empty(fileName))
        return Str_Dup(path);

    if (Str_EndsWithNoCase(path, DIR_SEP_STR) || (DIR_SEP_CHAR == *fileName)) {
        pathJoined = Str_New2(path, fileName);
    } else {
        pathJoined = Str_New3(path, DIR_SEP_STR, fileName);
    }
    return pathJoined;
}

/* expand unix-style home path (~) in 'path' to full path.
   Return newly allocated full path if expansion took place or NULL
   if it didn't.
   Caller needs to free() the result. */
static char *ExpandUnixHome(const char *path)
{
    char *  homedir;
    char *  expanded;
    if (!IsUnixHome(path))
        return NULL;

    homedir = getenv("HOME");
    if (!homedir)
        return NULL;

    expanded = Str_PathJoin(homedir, path + 1);
    return expanded;
}

/* make a given (possibly relative to current directory) 'path' absolute and
   canonize it. Returns NULL in case of error.
   Caller needs to free() the result. */
char *CanonizeAbsolutePath(const char *path)
{
#ifdef _WIN32
   /* TODO: needs windows version. */
    assert(0);
    return Str_Dup(path);
#else
    char    cwd[MAX_FILENAME_SIZE];
    char *  canonized;

    if (!path) return NULL;

    if (IsAbsPath(path)) {
        canonized = Str_Dup(path);
    } else {
        canonized = ExpandUnixHome(path);
        if (!canonized) {
            getcwd(cwd, sizeof(cwd));
            canonized = Str_PathJoin(cwd, path);
        }
    }
    CanonizePathInPlace(canonized);
    return canonized;
#endif
}

#define U_HEX_TXT "0123456789ABCDEF"
// buf must be at least 2 characters in size!!!
static void ByteToHexStr(unsigned char b, char *buf) {
    buf[0] = U_HEX_TXT[b / 16];
    buf[1] = U_HEX_TXT[b % 16];
}

#ifdef _WIN32
void win32_dbg_out(const char *format, ...) 
{
    char        buf[4096];
    char *      p = buf;
    int         written;
    va_list     args;

    va_start(args, format);

    written = _vsnprintf(p, sizeof(buf) - 2, format, args);
    if (written > 0)
        p += written;
    else
        p += sizeof(buf) - 2;
    p[0] = 0;
/*    printf(buf);
    fflush(stdout); */
    OutputDebugString(buf);
    va_end(args);
}

void win32_dbg_out_hex(const char *dsc, const char *data, int dataLen)
{
    unsigned char    buf[64+1];
    unsigned char *  curPos;
    int              bufCharsLeft;

    if (dsc) win32_dbg_out(dsc); /* a bit dangerous if contains formatting codes */
    if (!data) return;

    bufCharsLeft = sizeof(buf)-1;
    curPos = buf;
    while (dataLen > 0) {
        if (bufCharsLeft <= 1) {
            *curPos = 0;
            win32_dbg_out((unsigned char*)buf);
            bufCharsLeft = sizeof(buf)-1;
            curPos = buf;
        }
        ByteToHexStr(*data, curPos);
        curPos += 2;
        bufCharsLeft -= 2;
        --dataLen;
        ++data;
    }

    if (curPos != buf) {
        *curPos = 0;
        win32_dbg_out((unsigned char*)buf);
    }
    win32_dbg_out("\n");
}
#endif

