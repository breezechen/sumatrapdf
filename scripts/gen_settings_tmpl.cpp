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

%(structs_metadata)s

%(values_global_data)s

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
