/* Copyright 2006-2011 the SumatraPDF project authors (see ../AUTHORS file).
   License: FreeBSD (see ./COPYING) */

#include "BencUtil.h"
#include "TStrUtil.h"

BencObj *BencObj::Decode(const char *bytes, size_t *lenOut)
{
    size_t len;
    BencObj *result = BencString::Decode(bytes, &len);
    if (!result)
        result = BencInt::Decode(bytes, &len);
    if (!result)
        result = BencArray::Decode(bytes, &len);
    if (!result)
        result = BencDict::Decode(bytes, &len);

    // if the caller isn't interested in the amount of bytes
    // processed, verify that we've processed all of them
    if (result && !lenOut && bytes[len] != '\0') {
        delete result;
        result = NULL;
    }

    if (result && lenOut)
        *lenOut = len;
    return result;
}

static const char *ParseBencInt(const char *bytes, int64_t& value)
{
    bool negative = *bytes == '-';
    if (negative)
        bytes++;
    if (!ChrIsDigit(*bytes) || *bytes == '0' && ChrIsDigit(*(bytes + 1)))
        return NULL;

    value = 0;
    for (; ChrIsDigit(*bytes); bytes++) {
        value = value * 10 + (*bytes - '0');
        if (value - (negative ? 1 : 0) < 0)
            return NULL;
    }
    if (negative)
        value *= -1;

    return bytes;
}

BencString::BencString(const TCHAR *value) : BencObj(BT_STRING)
{
    this->value = tstr_to_utf8(value);
}

BencString::BencString(const char *rawValue, size_t len) : BencObj(BT_STRING)
{
    this->value = str_dupn(rawValue, len);
}

TCHAR *BencString::Value() const
{
    return utf8_to_tstr(value);
}

char *BencString::Encode() const
{
    return str_printf("%" PRIuPTR ":%s", Str::Len(value), value);
}

BencString *BencString::Decode(const char *bytes, size_t *lenOut)
{
    if (!bytes || !ChrIsDigit(*bytes))
        return NULL;

    int64_t len;
    const char *start = ParseBencInt(bytes, len);
    if (!start || *start != ':' || len < 0)
        return NULL;

    start++;
    if (Str::Len(start) < len)
        return NULL;

    if (lenOut)
        *lenOut = (start - bytes) + (size_t)len;
    return new BencRawString(start, (size_t)len);
}

BencRawString::BencRawString(const char *value, size_t len)
    : BencString(value, len == (size_t)-1 ? Str::Len(value) : len) { }

char *BencInt::Encode() const
{
    return str_printf("i%" PRId64 "e", value);
}

BencInt *BencInt::Decode(const char *bytes, size_t *lenOut)
{
    if (!bytes || *bytes != 'i')
        return NULL;

    int64_t value;
    const char *end = ParseBencInt(bytes + 1, value);
    if (!end || *end != 'e')
        return NULL;

    if (lenOut)
        *lenOut = (end - bytes) + 1;
    return new BencInt(value);
}

BencDict *BencArray::GetDict(size_t index) const {
    if (index < Length() && value[index]->Type() == BT_DICT)
        return static_cast<BencDict *>(value[index]);
    return NULL;
}

char *BencArray::Encode() const
{
    Str::Str<char> bytes(256);
    bytes.Append("l", 1);
    for (size_t i = 0; i < Length(); i++) {
        ScopedMem<char> objBytes(value[i]->Encode());
        bytes.Append(objBytes, Str::Len(objBytes));
    }
    bytes.Append("e", 1);
    return bytes.StealData();
}

BencArray *BencArray::Decode(const char *bytes, size_t *lenOut)
{
    if (!bytes || *bytes != 'l')
        return NULL;

    BencArray *list = new BencArray();
    size_t ix = 1;
    while (bytes[ix] != 'e') {
        size_t len;
        BencObj *obj = BencObj::Decode(bytes + ix, &len);
        if (!obj) {
            delete list;
            return NULL;
        }
        ix += len;
        list->Add(obj);
    }

    if (lenOut)
        *lenOut = ix + 1;
    return list;
}

static int keycmp(const void *a, const void *b)
{
    return strcmp(*(char **)a, *(char **)b);
}

BencObj *BencDict::GetObj(const char *key) const
{
    char **found = (char **)bsearch(&key, keys.LendData(), keys.Count(), sizeof(key), keycmp);
    if (found)
        return values[found - keys.LendData()];
    return NULL;
}

/* Per bencoding spec, keys must be ordered alphabetically when serialized,
   so we insert them in sorted order. This might be expensive due to lots of
   memory copying for lots of insertions in random order (more efficient would
   probably be: append at the end and sort when insertions are done or use a
   proper hash table instead of two parallel arrays). */
void BencDict::Add(const char *key, BencObj *obj)
{
    assert(key && obj && values.Find(obj) == -1);

    // determine the ordered insertion index
    size_t oix = 0;
    if (keys.Count() > 0 && strcmp(keys.Last(), key) < 0)
        oix = keys.Count();
    for (; oix < keys.Count(); oix++)
        if (strcmp(keys[oix], key) >= 0)
            break;

    if (oix < keys.Count() && Str::Eq(keys[oix], key)) {
        // overwrite a previous value
        delete values[oix];
        values[oix] = obj;
    }
    else {
        keys.InsertAt(oix, Str::Dup(key));
        values.InsertAt(oix, obj);
    }
}

char *BencDict::Encode() const
{
    Str::Str<char> bytes(256);
    bytes.Append("d", 1);
    for (size_t i = 0; i < Length(); i++) {
        ScopedMem<char> key(str_printf("%" PRIuPTR ":%s", Str::Len(keys[i]), keys[i]));
        bytes.Append(key, Str::Len(key));
        ScopedMem<char> objBytes(values[i]->Encode());
        bytes.Append(objBytes, Str::Len(objBytes));
    }
    bytes.Append("e", 1);
    return bytes.StealData();
}

BencDict *BencDict::Decode(const char *bytes, size_t *lenOut)
{
    if (!bytes || *bytes != 'd')
        return NULL;

    BencDict *dict = new BencDict();
    size_t ix = 1;
    while (bytes[ix] != 'e') {
        size_t len;
        BencString *key = BencString::Decode(bytes + ix, &len);
        if (!key || key->Type() != BT_STRING) {
            delete key;
            delete dict;
            return NULL;
        }
        ix += len;
        BencObj *obj = BencObj::Decode(bytes + ix, &len);
        if (!obj) {
            delete key;
            delete dict;
            return NULL;
        }
        ix += len;
        dict->Add(key->RawValue(), obj);
        delete key;
    }

    if (lenOut)
        *lenOut = ix + 1;
    return dict;
}
