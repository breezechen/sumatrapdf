#ifndef _WIN32_FONT_LIST_H_
#define _WIN32_FONT_LIST_H_

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SWAPWORD(x)     MAKEWORD(HIBYTE(x), LOBYTE(x))
#define SWAPLONG(x)     MAKELONG(SWAPWORD(HIWORD(x)), SWAPWORD(LOWORD(x)))

#define PLATFORM_UNICODE                    0
#define PLATFORM_MACINTOSH                  1
#define PLATFORM_ISO                        2
#define PLATFORM_MICROSOFT                  3

#define UNI_ENC_UNI_1                       0
#define UNI_ENC_UNI_1_1                     1
#define UNI_ENC_ISO                         2
#define UNI_ENC_UNI_2_BMP                   3
#define UNI_ENC_UNI_2_FULL_REPERTOIRE       4

#define MAC_ROMAN                           0
#define MAC_JAPANESE                        1
#define MAC_CHINESE_TRADITIONAL             2
#define MAC_KOREAN                          3
#define MAC_CHINESE_SIMPLIFIED              25

#define MS_ENC_SYMBOL                       0
#define MS_ENC_UNI_BMP                      1
#define MS_ENC_SHIFTJIS                     2
#define MS_ENC_PRC                          3
#define MS_ENC_BIG5                         4
#define MS_ENC_WANSUNG                      5
#define MS_ENC_JOHAB                        6
#define MS_ENC_UNI_FULL_REPETOIRE           10

typedef struct _tagTT_OFFSET_TABLE
{
    USHORT  uMajorVersion;
    USHORT  uMinorVersion;
    USHORT  uNumOfTables;
    USHORT  uSearchRange;
    USHORT  uEntrySelector;
    USHORT  uRangeShift;
    } TT_OFFSET_TABLE;

    typedef struct _tagTT_TABLE_DIRECTORY
    {
    char    szTag[4];                        //table name
    ULONG   uCheckSum;                        //Check sum
    ULONG   uOffset;                        //Offset from beginning of file
    ULONG   uLength;                        //length of the table in bytes
    } TT_TABLE_DIRECTORY;

    typedef struct _tagTT_NAME_TABLE_HEADER
    {
    USHORT  uFSelector;                        //format selector. Always 0
    USHORT  uNRCount;                        //Name Records count
    USHORT  uStorageOffset;                //Offset for strings storage, from start of the table
    } TT_NAME_TABLE_HEADER;

    typedef struct _tagTT_NAME_RECORD
    {
    USHORT  uPlatformID;
    USHORT  uEncodingID;
    USHORT  uLanguageID;
    USHORT  uNameID;
    USHORT  uStringLength;
    USHORT  uStringOffset;        //from start of storage area
    } TT_NAME_RECORD;

    typedef struct _tagFONT_COLLECTION
    {
    char    Tag[4];
    ULONG   Version;
    ULONG   NumFonts;
}FONT_COLLECTION;

typedef struct FontMapping {
    struct FontMapping *next;
    char *name;
    char *path;
    int index;
} FontMapping;

extern FontMapping *          gFontMapMSList;

void    WinFontList_Create(void);
void    WinFontList_Destroy(void);

#ifdef __cplusplus
}
#endif

#endif
