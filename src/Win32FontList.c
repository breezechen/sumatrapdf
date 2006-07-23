#include "Win32FontList.h"
#include "BaseUtils.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <strsafe.h>

FontMapping * gFontMapMSList = NULL;

FontMapping *FontMapping_Find(FontMapping **root, char *facename, int index)
{
    FontMapping *cur;
    assert(root);
    if (!root)
        return NULL;
    if (!facename)
        return NULL;
    cur = *root;
    while (cur) {
        if ((index == cur->index) && (Str_Eq(facename, cur->name)))
            return cur;
        cur = cur->next;
    }
    return NULL;
}

BOOL FontMapping_Exists(FontMapping **root, char *facename, int index)
{
    if (FontMapping_Find(root, facename, index))
        return TRUE;
    return FALSE;
}

void FontMapping_Destroy(FontMapping *entry)
{
    if (entry) {
        free((void*)entry->name);
        free((void*)entry->path);
    }
    free((void*)entry);
}

FontMapping *FontMapping_Create(char *name, char *path, int index)
{
    FontMapping *entry;

    if (!name || !path)
        return NULL;

    entry = (FontMapping*)malloc(sizeof(FontMapping));
    if (!entry)
        return NULL;

    entry->name = Str_Dup(name);
    if (!entry->name)
        goto Error;
    entry->path = Str_Dup(path);
    if (!entry->path)
        goto Error;
    entry->index = index;
    return entry;
Error:
    FontMapping_Destroy(entry);
    return NULL;
}

void FontMappingList_Destroy(FontMapping **root)
{
    FontMapping *cur, *next;
    if (!root)
        return;
    cur = *root;
    while (cur) {
        next = cur->next;
        FontMapping_Destroy(cur);
        cur = next;
    }
}

void
FontMappingList_InsertUnique(FontMapping **root, char *facename, char *path, int index)
{
    FontMapping *entry;

    /* TODO: better error handling */
    if (FontMapping_Exists(root, facename, index))
        return;

    entry = FontMapping_Create(facename, path, index);
    if (!entry)
        return;

    entry->next = *root;
    *root = entry;
    return;
}

void FontMappingList_Dump(FontMapping **root)
{
    FontMapping *cur;
    if (!root)
        return;
    cur = *root;
    while (cur) {
/*        DBG_OUT("name=%s, path=%s\n", cur->name, cur->path); */
        cur = cur->next;
    }
}

BOOL freadsafe(FILE *file, void *buf, int size)
{
    int byteread;
    while (size > 0)
    {
        byteread = fread(buf, 1, size, file);
        if (byteread < 0)
            return FALSE;
        assert(byteread <= size);
        size -= byteread;
    }
    return TRUE;
}

BOOL
swapword(char* pbyte, int nLen)
{
    int     i;
    char    tmp;
    int     nMax;

    if (nLen % 2)
        return FALSE;

    nMax = nLen / 2;
    for(i = 0; i < nLen; ++i) {
        tmp = pbyte[i*2];
        pbyte[i*2] = pbyte[i*2+1];
        pbyte[i*2+1] = tmp;
    }
    return TRUE;
}

/* pSouce and PDest can be same */
BOOL
decodeunicodeBMP(char* source, int sourcelen,char* dest, int destlen)
{
    wchar_t tmp[1024*2];
    int converted;
    memset(tmp,0,sizeof(tmp));
    memcpy(tmp,source,sourcelen);
    swapword((char*)tmp,sourcelen);

    converted = WideCharToMultiByte(CP_ACP, 0, tmp,
        -1, dest, destlen, NULL, NULL);

    if(converted == 0)
        return FALSE;

    return TRUE;
}

BOOL
decodeunicodeplatform(char* source, int sourcelen,char* dest, int destlen, int enctype)
{
    switch (enctype)
    {
        case UNI_ENC_UNI_1:
        case UNI_ENC_UNI_2_BMP:
            return decodeunicodeBMP(source,sourcelen,dest,destlen);
        case UNI_ENC_UNI_2_FULL_REPERTOIRE:
        case UNI_ENC_UNI_1_1:
        case UNI_ENC_ISO:
        default:
            return FALSE;
    }
}

BOOL
decodemacintoshplatform(char* source, int sourcelen,char* dest, int destlen, int enctype)
{
    if (MAC_ROMAN != enctype)
        return FALSE;

    if (sourcelen + 1 > destlen)
            return FALSE;
    memcpy(source,dest,sourcelen);
    dest[sourcelen] = 0;
    return TRUE;
}

BOOL
decodemicrosoftplatform(char* source, int sourcelen,char* dest, int destlen, int enctype)
{
    switch (enctype)
    {
        case MS_ENC_SYMBOL:
        case MS_ENC_UNI_BMP:
            return decodeunicodeBMP(source,sourcelen,dest,destlen);
    }
    return FALSE;
    }

/* TODO: proper cleanup on file errors */
void TTFFontFileParse(char *path, int offset, int index)
{
    FILE *              file;
    TT_OFFSET_TABLE     ttOffsetTable;
    TT_TABLE_DIRECTORY  tblDir;
    TT_NAME_TABLE_HEADER ttNTHeader;
    TT_NAME_RECORD      ttRecord;
    char                szTemp[4096];
    int                 found;
    int                 i;
    BOOL                fOk;

    file = fopen(path, "rb");
    if (NULL == file)
        return;
    fseek(file, offset, SEEK_SET);
    if (!freadsafe(file, &ttOffsetTable, sizeof(TT_OFFSET_TABLE)))
        goto Error;

    ttOffsetTable.uNumOfTables = SWAPWORD(ttOffsetTable.uNumOfTables);
    ttOffsetTable.uMajorVersion = SWAPWORD(ttOffsetTable.uMajorVersion);
    ttOffsetTable.uMinorVersion = SWAPWORD(ttOffsetTable.uMinorVersion);

    //check is this is a true type font and the version is 1.0
    if (ttOffsetTable.uMajorVersion != 1 || ttOffsetTable.uMinorVersion != 0)
        goto Error;

    found = 0;
    for (i = 0; i < ttOffsetTable.uNumOfTables; i++)
    {
        if (!freadsafe(file,&tblDir,sizeof(TT_TABLE_DIRECTORY)))
            return;

        memcpy(szTemp, tblDir.szTag, 4);
        szTemp[4] = 0;

        if (stricmp(szTemp, "name") == 0)
        {
            found = 1;
            tblDir.uLength = SWAPLONG(tblDir.uLength);
            tblDir.uOffset = SWAPLONG(tblDir.uOffset);
            break;
        }
        else if (szTemp[0] == 0)
            break;
    }

    if (!found)
        goto Error;

    fseek(file, tblDir.uOffset, SEEK_SET);

    if (!freadsafe(file, &ttNTHeader, sizeof(TT_NAME_TABLE_HEADER)))
        goto Error;

    ttNTHeader.uNRCount = SWAPWORD(ttNTHeader.uNRCount);
    ttNTHeader.uStorageOffset = SWAPWORD(ttNTHeader.uStorageOffset);

    offset = tblDir.uOffset + sizeof(TT_NAME_TABLE_HEADER);

    for (i = 0; i < ttNTHeader.uNRCount; ++i)
    {
        fseek(file, offset + sizeof(TT_NAME_RECORD)*i, SEEK_SET);
        if (!freadsafe(file, &ttRecord, sizeof(TT_NAME_RECORD)))
            goto Error;

        ttRecord.uNameID = SWAPWORD(ttRecord.uNameID);
        ttRecord.uLanguageID = SWAPWORD(ttRecord.uLanguageID);

        // Full Name
        if (ttRecord.uNameID == 6)
        {
            ttRecord.uPlatformID = SWAPWORD(ttRecord.uPlatformID);
            ttRecord.uEncodingID = SWAPWORD(ttRecord.uEncodingID);
            ttRecord.uStringLength = SWAPWORD(ttRecord.uStringLength);
            ttRecord.uStringOffset = SWAPWORD(ttRecord.uStringOffset);

            fseek(file, tblDir.uOffset + ttRecord.uStringOffset + ttNTHeader.uStorageOffset, SEEK_SET);
            if (!freadsafe(file, szTemp, ttRecord.uStringLength))
                goto Error;

            switch(ttRecord.uPlatformID)
            {
                case PLATFORM_UNICODE:
                    fOk = decodeunicodeplatform(szTemp, ttRecord.uStringLength,
                        szTemp, sizeof(szTemp), ttRecord.uEncodingID);
                    break;

                case PLATFORM_MACINTOSH:
                    fOk = decodemacintoshplatform(szTemp, ttRecord.uStringLength,
                        szTemp, sizeof(szTemp), ttRecord.uEncodingID);
                    break;

                case PLATFORM_ISO:
                    goto Error;

                case PLATFORM_MICROSOFT:
                    fOk = decodemicrosoftplatform(szTemp, ttRecord.uStringLength,
                        szTemp, sizeof(szTemp), ttRecord.uEncodingID);
                    break;
            }

            if (fOk)
                FontMappingList_InsertUnique(&gFontMapMSList, szTemp, path, index);
        }
    }
Error:
    if (file)
        fclose(file);
}

void TTCFontFileParse(char *filePath)
{

}

static BOOL isDir(WIN32_FIND_DATA *fileData)
{
    if (fileData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return TRUE;
    return FALSE;
}

/* Get a list of windows fonts.
   TODO: it should cache the data on disk and don't rescan font directory unless
   timestamp on last modified time on directory has changed */
void WinFontList_Create(void)
{
    HRESULT hr;
    char    fontDir[MAX_PATH*2];
    char    searchPattern[MAX_PATH*2];
    char    file[MAX_PATH*2];
    HANDLE  h;
    WIN32_FIND_DATA fileData;

    GetWindowsDirectory(fontDir, sizeof(fontDir));
    hr = StringCchCat(fontDir, dimof(fontDir), "\\Fonts\\");
    assert(S_OK == hr);
    hr = StringCchPrintf(searchPattern, sizeof(searchPattern), "%s*.tt?", fontDir);
    assert(S_OK == hr);

    h = FindFirstFile(searchPattern, &fileData);
    if (h == INVALID_HANDLE_VALUE)
    {
        /* most likely no font directory at all - not very good */
        assert(0);
        return;
    }

    for (;;)
    {
        if (!isDir(&fileData))
        {
            hr = StringCchPrintf(file, dimof(file), "%s%s", fontDir, fileData.cFileName);
            assert(S_OK == hr);
            if ( ('c' == file[strlen(file)-1]) ||
                 ('C' == file[strlen(file)-1]))
            {
                TTCFontFileParse(file);
            }
            else if ( ('f' == file[strlen(file)-1]) ||
                      ('F' == file[strlen(file)-1]) )
            {
                TTFFontFileParse(file, 0, 0);
            }
        }

        if (!FindNextFile(h, &fileData))
        {
            if (ERROR_NO_MORE_FILES == GetLastError())
                break;
        }
    }
}

void WinFontList_Destroy(void)
{
    FontMappingList_Destroy(&gFontMapMSList);
}
