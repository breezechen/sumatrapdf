/* Copyright 2006-2011 the SumatraPDF project authors (see ../AUTHORS file).
   License: FreeBSD (see ./COPYING) */

/* The most basic things, including string handling functions */
#include "BaseUtil.h"
#include "StrUtil.h"

namespace Str {

bool IsEmpty(const char *str)
{
    if (!str)
        return true;
    if (0 == *str)
        return true;
    return false;
}

bool IsEmpty(const WCHAR *str)
{
    if (!str)
        return true;
    if (0 == *str)
        return true;
    return false;
}

bool Eq(const char *str1, const char *str2)
{
    if (str1 == str2)
        return true;
    if (!str1 || !str2)
        return false;
    if (0 == strcmp(str1, str2))
        return true;
    return false;
}

bool Eq(const WCHAR *str1, const WCHAR *str2)
{
    if (str1 == str2)
        return true;
    if (!str1 || !str2)
        return false;
    if (0 == wcscmp(str1, str2))
        return true;
    return false;
}

bool EqI(const char *str1, const char *str2)
{
    if (str1 == str2)
        return true;
    if (!str1 || !str2)
        return false;
    if (0 == _stricmp(str1, str2))
        return true;
    return false;
}

bool EqI(const WCHAR *str1, const WCHAR *str2)
{
    if (str1 == str2)
        return true;
    if (!str1 || !str2)
        return false;
    if (0 == _wcsicmp(str1, str2))
        return true;
    return false;
}

bool EqN(const char *str1, const char *str2, size_t len)
{
    if (str1 == str2)
        return true;
    if (!str1 || !str2)
        return false;
    if (0 == strncmp(str1, str2, len))
        return true;
    return false;
}

bool EqN(const WCHAR *str1, const WCHAR *str2, size_t len)
{
    if (str1 == str2)
        return true;
    if (!str1 || !str2)
        return false;
    if (0 == wcsncmp(str1, str2, len))
        return true;
    return false;
}

/* return true if 'str' starts with 'txt', NOT case-sensitive */
bool StartsWithI(const char *str, const char *txt)
{
    if (str == txt)
        return true;
    if (!str || !txt)
        return false;

    if (0 == _strnicmp(str, txt, Str::Len(txt)))
        return true;
    return false;
}

/* return true if 'str' starts with 'txt', NOT case-sensitive */
bool StartsWithI(const WCHAR *str, const WCHAR *txt)
{
    if (!str && !txt)
        return true;
    if (!str || !txt)
        return false;

    if (0 == _wcsnicmp(str, txt, Str::Len(txt)))
        return true;
    return false;
}

bool EndsWith(const char *txt, const char *end)
{
    size_t end_len;
    size_t txt_len;

    if (!txt || !end)
        return FALSE;

    txt_len = Str::Len(txt);
    end_len = Str::Len(end);
    if (end_len > txt_len)
        return false;
    return Str::Eq(txt+txt_len-end_len, end);
}

bool EndsWithI(const char *txt, const char *end)
{
    size_t end_len;
    size_t txt_len;

    if (!txt || !end)
        return false;

    txt_len = Str::Len(txt);
    end_len = Str::Len(end);
    if (end_len > txt_len)
        return false;
    return Str::EqI(txt+txt_len-end_len, end);
}

bool EndsWith(const WCHAR *txt, const WCHAR *end)
{
    size_t end_len;
    size_t txt_len;
    if (!txt || !end)
        return false;
    txt_len = Str::Len(txt);
    end_len = Str::Len(end);
    if (end_len > txt_len)
        return false;
    return Str::Eq(txt+txt_len-end_len, end);
}

bool EndsWithI(const WCHAR *txt, const WCHAR *end)
{
    size_t end_len;
    size_t txt_len;

    if (!txt || !end)
        return false;

    txt_len = Str::Len(txt);
    end_len = Str::Len(end);
    if (end_len > txt_len)
        return false;
    return Str::EqI(txt+txt_len-end_len, end);
}

}

char * str_cat_s(char * dst, size_t dst_cch_size, const char * src)
{
    return str_catn_s(dst, dst_cch_size, src, strlen(src));
}

char * str_catn_s(char *dst, size_t dst_cch_size, const char *src, size_t src_cch_size)
{
    char *dstEnd = dst + strlen(dst);
    size_t len = min(src_cch_size + 1, dst_cch_size - (dstEnd - dst));
    if (dst_cch_size <= (size_t)(dstEnd - dst))
        return NULL;
    
    strncpy(dstEnd, src, len);
    dstEnd[len - 1] = '\0';
    
    if (src_cch_size >= len)
        return NULL;
    return dst;
}

/* Concatenate 2 strings. Any string can be NULL.
   Caller needs to free() memory. */
char *str_cat(const char *str1, const char *str2)
{
    if (!str1)
        str1 = "";
    if (!str2)
        str2 = "";

    return str_printf("%s%s", str1, str2);
}

char *str_dupn(const char *str, size_t str_len_cch)
{
    char *copy;

    if (!str)
        return NULL;
    copy = (char *)memdup((void *)str, str_len_cch + 1);
    if (copy)
        copy[str_len_cch] = 0;
    return copy;
}

int str_copyn(char *dst, size_t dst_cch_size, const char *src, size_t src_cch_size)
{
    size_t len = min(src_cch_size + 1, dst_cch_size);
    
    strncpy(dst, src, len);
    dst[len - 1] = '\0';
    
    if (src_cch_size >= dst_cch_size)
        return FALSE;
    return TRUE;
}

int str_copy(char *dst, size_t dst_cch_size, const char *src)
{
    return str_copyn(dst, dst_cch_size, src, Str::Len(src));
}

/* Convert binary data in <buf> of size <len> to a hex-encoded string */
char *mem_to_hexstr(const unsigned char *buf, int len)
{
    int i;
    /* 2 hex chars per byte, +1 for terminating 0 */
    char *ret = (char *)calloc((size_t)len + 1, 2);
    if (!ret)
        return NULL;
    for (i = 0; i < len; i++) {
        sprintf(ret + 2 * i, "%02x", *buf++);
    }
    ret[2 * len] = '\0';
    return ret;
}

/* Caller needs to free() the result */
static char *multibyte_to_multibyte(const char *src, UINT CodePage1, UINT CodePage2)
{
    assert(src);
    if (!src) return NULL;

    char *res = NULL;
    WCHAR *tmp;
    int requiredBufSize = MultiByteToWideChar(CodePage1, 0, src, -1, NULL, 0);
    tmp = SAZA(WCHAR, requiredBufSize);
    if (!tmp)
        return NULL;
    MultiByteToWideChar(CodePage1, 0, src, -1, tmp, requiredBufSize);

    requiredBufSize = WideCharToMultiByte(CodePage2, 0, tmp, -1, NULL, 0, NULL, NULL);
    res = SAZA(char, requiredBufSize);
    if (res)
        WideCharToMultiByte(CodePage2, 0, tmp, -1, res, requiredBufSize, NULL, NULL);
    free(tmp);

    return res;
}

/* Caller needs to free() the result */
char *str_to_multibyte(const char *src, UINT CodePage)
{
    if (CP_ACP == CodePage)
        return Str::Dup(src);

    return multibyte_to_multibyte(src, CP_ACP, CodePage);
}

/* Caller needs to free() the result */
char *multibyte_to_str(const char *src, UINT CodePage)
{
    if (CP_ACP == CodePage)
        return Str::Dup(src);

    return multibyte_to_multibyte(src, CodePage, CP_ACP);
}

/* Reverse of mem_to_hexstr. Convert a 0-terminatd hex-encoded string <s> to
   binary data pointed by <buf> of max sisze bufLen.
   Returns FALSE if size of <s> doesn't match <bufLen>. */
bool hexstr_to_mem(const char *s, unsigned char *buf, int bufLen)
{
    for (; bufLen > 0; bufLen--) {
        int c;
        if (1 != sscanf(s, "%02x", &c))
            return false;
        s += 2;
        *buf++ = (unsigned char)c;
    }
    return *s == '\0';
}

char *str_printf(const char *format, ...)
{
    va_list args;
    char    message[256];
    size_t  bufCchSize = dimof(message);
    char  * buf = message;

    va_start(args, format);
    for (;;)
    {
        int count = vsnprintf(buf, bufCchSize, format, args);
        if (0 <= count && (size_t)count < bufCchSize)
            break;
        /* we have to make the buffer bigger. The algorithm used to calculate
           the new size is arbitrary (aka. educated guess) */
        if (buf != message)
            free(buf);
        if (bufCchSize < 4*1024)
            bufCchSize += bufCchSize;
        else
            bufCchSize += 1024;
        buf = SAZA(char, bufCchSize);
        if (!buf)
            break;
    }
    va_end(args);

    if (buf == message)
        buf = Str::Dup(message);

    return buf;
}

int str_printf_s(char *out, size_t out_cch_size, const char *format, ...)
{
    va_list args;
    int count;

    va_start(args, format);
    count = vsnprintf(out, out_cch_size, format, args);
    if (count < 0 || (size_t)count >= out_cch_size)
        out[out_cch_size - 1] = '\0';
    va_end(args);

    return count;
}

void win32_dbg_out(const char *format, ...)
{
    char        buf[4096];
    va_list     args;

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    OutputDebugStringA(buf);
    va_end(args);
}

void win32_dbg_out_hex(const char *dsc, const unsigned char *data, int dataLen)
{
    char *hexStr;
    if (!data) dataLen = 0;

    hexStr = mem_to_hexstr(data, dataLen);
    win32_dbg_out("%s%s\n", dsc ? dsc : "", hexStr);
    free(hexStr);
}
