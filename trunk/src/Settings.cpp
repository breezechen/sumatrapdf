
// DON'T EDIT MANUALLY !!!!
// auto-generated by scripts/gen_settings.py !!!!

#include "BaseUtil.h"
#include "Settings.h"

struct StructPointerInfo;

#define POINTER_SIZE 8

typedef struct {
    int                  size;
    int                  pointersCount;
    StructPointerInfo *  pointersInfo;
} StructDef;

// information about a single field
struct StructPointerInfo {
    // from the beginning of the struct
    int offset;
    // what kind of structure it points to, needed
    // for recursive application of the algorithm
    // NULL if that structure doesn't need fixup
    // (has no pointers)
    StructDef *def;
};

STATIC_ASSERT(sizeof(ForwardSearchSettings) == 16, ForwardSearchSettings_is_16_bytes);
StructDef gForwardSearchSettingsStructDef = { 16, 0, NULL};

STATIC_ASSERT(sizeof(PaddingSettings) == 12, PaddingSettings_is_12_bytes);
StructDef gPaddingSettingsStructDef = { 12, 0, NULL};

STATIC_ASSERT(offsetof(AdvancedSettings, pagePadding) == 12, pagePadding_is_12_bytes_in_AdvancedSettings);
STATIC_ASSERT(offsetof(AdvancedSettings, forwardSearch) == 20, forwardSearch_is_20_bytes_in_AdvancedSettings);
StructPointerInfo gAdvancedSettingsPointers[] = {
    { 12, &gPaddingSettingsStructDef },
    { 20, &gForwardSearchSettingsStructDef },
};

STATIC_ASSERT(sizeof(AdvancedSettings) == 28, AdvancedSettings_is_28_bytes);
StructDef gAdvancedSettingsStructDef = { 28, 2, &gAdvancedSettingsPointers[0]};


static uint8_t gAdvancedSettingsDefault[] = {

  // AdvancedSettings
  0x00, 0x00, 0x03, 0x02, // uint32_t version = 0x2030000
  0x00, 0x00, // bool traditionalEbookUI = False
  0x00, 0x00, // bool escToExit = False
  0x00, 0xf2, 0xff, 0x00, // uint32_t logoColor = 0xfff200
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Ptr<PaddingSettings> pagePadding = 0x0
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Ptr<ForwardSearchSettings> forwardSearch = 0x0

  // PaddingSettings
  0x02, 0x00, // uint16_t top = 0x2
  0x02, 0x00, // uint16_t bottom = 0x2
  0x04, 0x00, // uint16_t left = 0x4
  0x04, 0x00, // uint16_t right = 0x4
  0x04, 0x00, // uint16_t spaceX = 0x4
  0x04, 0x00, // uint16_t spaceY = 0x4

  // ForwardSearchSettings
  0x00, 0x00, 0x00, 0x00, // int32_t highlightOffset = 0x0
  0x0f, 0x00, 0x00, 0x00, // int32_t highlightWidth = 0xf
  0x00, 0x00, 0x00, 0x00, // int32_t highlightPermanent = 0x0
  0xff, 0x81, 0x65, 0x00, // uint32_t highlightColor = 0x6581ff
};

// a serialized format is a linear chunk of memory with pointers
// replaced with offsets from the beginning of the memory (base)
// to deserialize, we malloc() each struct and replace offsets
// with pointers to those newly allocated structs
char* deserialize_struct(const char *data, StructDef *def, const char *base, const int totalSize)
{
    int size = def->size;
    char *dataCopy = AllocArray<char>(size);
    // TODO: when we add size to each struct, we only copy up to that size
    memcpy(dataCopy, data, size);

    // this struct doesn't have pointer members to fix up
    if (!def)
        return dataCopy;

    for (int i=0; i < def->pointersCount; i++) {
        int memberOffset = def->pointersInfo[i].offset;
        StructDef *memberDef = def->pointersInfo[i].def;
        Ptr<char> *ptrToMemberPtr = (Ptr<char>*)(dataCopy + memberOffset);
        int memberDataOffset = (int)ptrToMemberPtr->ptr;
        ZeroMemory(ptrToMemberPtr, 8);
        if (memberDataOffset != 0) {
            CrashIf(memberDataOffset + memberDef->size > totalSize);
            char *memberDataNew = deserialize_struct(base + memberDataOffset, memberDef, base, totalSize);
            ptrToMemberPtr->ptr = memberDataNew;
        }
    }

    return dataCopy;
}

// the assumption here is that the data was either build by deserialize_struct
// or was set by application code in a way that observes our rule: each
// object was separately allocated with malloc()
void free_struct(char *data, StructDef *def)
{
    // recursively free all structs reachable from this struct
    for (int i=0; i < def->pointersCount; i++) {
        int memberOffset = def->pointersInfo[i].offset;
        Ptr<char> *ptrToPtr = (Ptr<char>*)(data + memberOffset);
        char *memberData = ptrToPtr->ptr;
        StructDef *memberDef = def->pointersInfo[i].def;
        if (memberData && memberDef)
            free_struct(memberData, memberDef);
        free(memberData);
    }
    free(data);
}

// TODO: write me
const char *serialize_struct(char *data, StructDef *def, uint32_t *sizeOut)
{
    *sizeOut = 0;
    return NULL;
}

