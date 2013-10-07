// DON'T EDIT MANUALLY !!!!
// auto-generated by gen_txt.py !!!!

#ifndef PagesLayoutDef_h
#define PagesLayoutDef_h

#include "SerializeTxt.h"
using namespace sertxt;

struct PagesLayoutDef {
    const char *  name;
    const char *  page1;
    const char *  page2;
    int32_t       spaceDx;
};

extern const StructMetadata gPagesLayoutDefMetadata;

inline PagesLayoutDef *DeserializePagesLayoutDef(char *data, size_t dataLen)
{
    return (PagesLayoutDef*)Deserialize(data, dataLen, &gPagesLayoutDefMetadata);
}

inline PagesLayoutDef *DeserializePagesLayoutDef(TxtNode* root)
{
    return (PagesLayoutDef*)Deserialize(root, &gPagesLayoutDefMetadata);
}

inline uint8_t *SerializePagesLayoutDef(PagesLayoutDef *val, size_t *dataLenOut)
{
    return Serialize((const uint8_t*)val, &gPagesLayoutDefMetadata, dataLenOut);
}

inline void FreePagesLayoutDef(PagesLayoutDef *val)
{
    FreeStruct((uint8_t*)val, &gPagesLayoutDefMetadata);
}

#endif
