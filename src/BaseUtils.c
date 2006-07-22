#include "BaseUtils.h"

#include <string.h>
#include <assert.h>

#ifdef WIN32
#include <windows.h>
#include <strsafe.h>
#else
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>


void SwapInt(int *one, int *two)
{
    int tmp = *one;
    *one = *two;
    *two = tmp;
}

void SwapDouble(double *one, double *two)
{
    double tmp = *one;
    *one = *two;
    *two = tmp;
}

int MinInt(int one, int two)
{
    if (one < two)
        return one;
    else
        return two;
}

int Str_Empty(const char *txt)
{
    if (!txt)
        return TRUE;
    if (0 == *txt)
        return TRUE;
    return FALSE;
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
#ifdef WIN32
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

  va_start(args, pFormat);
  len = vasprintf(&pBuffer, pFormat, args);
  return pBuffer;
#endif
}

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

#ifndef WIN32 /* TODO: should probably be based on MSVC version */
void strcpy_s(char *dst, size_t dstLen, char *src)
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
#ifdef WIN32
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

