// DON'T EDIT MANUALLY !!!!
// auto-generated by scripts/gen_settings.py !!!!

#include "BaseUtil.h"
#include "Settings.h"

#define MAGIC_ID 0x53756d53  // 'SumS' as 'Sumatra Settings'

#define SERIALIZED_HEADER_LEN 12

typedef struct {
    uint32_t   magicId;
    uint32_t   version;
    uint32_t   topLevelStructOffset;
} SerializedHeader;

STATIC_ASSERT(sizeof(SerializedHeader) == SERIALIZED_HEADER_LEN, SerializedHeader_is_12_bytes);

static const uint16_t TYPE_BOOL         = 0;
static const uint16_t TYPE_I16          = 1;
static const uint16_t TYPE_U16          = 2;
static const uint16_t TYPE_I32          = 3;
static const uint16_t TYPE_U32          = 4;
static const uint16_t TYPE_STR          = 5;
static const uint16_t TYPE_STRUCT_PTR   = 6;

struct FieldMetadata;

typedef struct {
    uint16_t        size;
    uint16_t        nFields;
    FieldMetadata * fields;
} StructMetadata;

// information about a single field
struct FieldMetadata {
    uint16_t type; // TYPE_*
    // from the beginning of the struct
    uint16_t offset;
    // type is TYPE_STRUCT_PTR, otherwise NULL
    StructMetadata *def;
};

%(structs_metadata)s
%(values_global_data)s

// returns -1 on error
static int NextIntVal(const char **sInOut)
{
    const char *s = *sInOut;
    char c;
    int val = 0;
    int n;
    for (;;) {
        c = *s++;
        if (0 == c) {
            s--; // position at ending 0
            goto Exit;
        }
        if ('.' == c)
            goto Exit;
        n = c - '0';
        if ((c < 0) || (c > 9))
            return -1;
        val *= 10;
        val += n;
    }
Exit:
    if (val > 255)
        return -1;
    *sInOut = s;
    return val;
}

// parses a vrsion string in the format "x.y.z", of up to 4 parts
// return 0 on parsing error
static uint32_t VersionFromStr(const char *s)
{
    uint32_t ver = 0;
    int left = 4;
    int n;
    while (left > 0) {
        if (0 == *s)
            goto Exit;
        n = NextIntVal(&s);
        if (-1 == n)
            return 0;
        --left;
    }
Exit:
    ver = ver << (left * 8);
    return ver;
}

static uint8_t* DeserializeRec(const uint8_t *data, int dataSize, int dataOff, StructMetadata *def)
{
#if 0
    uint8_t *res = AllocArray<uint8_t>(def->size);
    FieldMetadata *fieldDef = NULL;
    uint64_t decodedVal;
    for (int i = 0; i < def->nFields; i++) {
        fieldDef = def->fields + i;
        uint16_t offset = fieldDef->offset;
        uint16_t type = fieldDef->type;
    }
#endif
    return NULL;
}

// a serialized format is a linear chunk of memory with pointers
// replaced with offsets from the beginning of the memory (base)
// to deserialize, we malloc() each struct and replace offsets
// with pointers to those newly allocated structs
// TODO: when version of the data doesn't match our version,
// especially in the case of our version being higher (and hence
// having more data), we should decode the default values and
// then over-write them with whatever values we decoded.
// alternatively, we could keep a default value in struct metadata
static uint8_t* Deserialize(const uint8_t *data, int dataSize, const char *version, StructMetadata *def)
{
    if (!data)
        return NULL;
    if (dataSize < sizeof(SerializedHeader))
        return NULL;
    SerializedHeader *hdr = (SerializedHeader*)data;
    if (hdr->magicId != MAGIC_ID)
        return NULL;
    //uint32_t ver = VersionFromStr(version);
    return DeserializeRec(data, dataSize, SERIALIZED_HEADER_LEN, def);
}

// the assumption here is that the data was either build by deserialize_struct
// or was set by application code in a way that observes our rule: each
// object was separately allocated with malloc()
void FreeStruct(uint8_t *data, StructMetadata *def)
{
    if (!data)
        return;
    FieldMetadata *fieldDef = NULL;
    for (int i = 0; i < def->nFields; i++) {
        fieldDef = def->fields + i;
        if (TYPE_STRUCT_PTR ==  fieldDef->type) {
            uint8_t **p = (uint8_t**)(data + fieldDef->offset);
            FreeStruct(*p, fieldDef->def);
        }
    }
    free(data);
}

// TODO: write me
uint8_t *Serialize(const uint8_t *data, const char *version, StructMetadata *def, int *sizeOut)
{
    *sizeOut = 0;
    return NULL;
}
%(top_level_funcs)s
