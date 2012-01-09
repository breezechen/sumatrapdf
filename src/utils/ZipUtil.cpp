/* Copyright 2011 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "ZipUtil.h"
#include "StrUtil.h"
#include "FileUtil.h"
#include "Vec.h"

// mini(un)zip
#include <ioapi.h>
#include <iowin32.h>
#include <unzip.h>

// returns the FileToUnzip that's been successfully unzipped (or NULL)
static FileToUnzip *UnzipFile(Allocator *allocator, unzFile& uf,  FileToUnzip *files, const TCHAR *dir)
{
    char fileName[MAX_PATH];
    unz_file_info64 finfo;

    int err = unzGetCurrentFileInfo64(uf, &finfo, fileName, dimof(fileName), NULL, 0, NULL, 0);
    if (err != UNZ_OK)
        return NULL;

    unsigned len = (unsigned)finfo.uncompressed_size;
    ZPOS64_T len2 = len;
    if (len2 != finfo.uncompressed_size) // overflow check
        return NULL;

    FileToUnzip *file = NULL;
    for (int i = 0; files[i].fileName; i++) {
        if (str::EqI(fileName, files[i].fileName)) {
            file = &files[i];
            break;
        }
    }
    if (!file)
        return NULL;
    str::Str<TCHAR> filePath(256, allocator);
    filePath.Append(dir);
    if (!str::EndsWith(filePath.Get(), _T("\\")))
        filePath.Append(_T("\\"));

    if (file->unzippedName) {
        filePath.Append(file->unzippedName);
    } else {
        TCHAR buf[256] = { 0 };
        str::conv::FromAnsiBuf(buf, dimof(buf), fileName);  // Note: maybe FromUtf8?
        filePath.Append(buf);
    }

    void *data = Allocator::Alloc(allocator, len);
    if (!data)
        return NULL;

    err = unzOpenCurrentFilePassword(uf, NULL);
    if (err != UNZ_OK)
        return NULL;

    unsigned int readBytes = unzReadCurrentFile(uf, data, len);
    if (readBytes != len || !file::WriteAll(filePath.Get(), data, len))
        file = NULL;

    unzCloseCurrentFile(uf); // ignoring error code

    return file;
}

static bool WereAllUnzipped(FileToUnzip *files, Vec<FileToUnzip *> unzipped)
{
    for (int i = 0; files[i].fileName; i++)
        if (unzipped.Find(&files[i]) == -1)
            return false;

    return true;
}

bool UnzipFiles(Allocator *allocator, const TCHAR *zipFile, FileToUnzip *files, const TCHAR *dir)
{
    Vec<FileToUnzip *> unzipped;

    zlib_filefunc64_def ffunc;
    fill_win32_filefunc64(&ffunc);
    unzFile uf = unzOpen2_64(zipFile, &ffunc);
    if (!uf)
        return false;

    unz_global_info64 ginfo;
    int err = unzGetGlobalInfo64(uf, &ginfo);
    if (err != UNZ_OK) {
        unzClose(uf);
        return false;
    }

    unzGoToFirstFile(uf);
    for (int n = 0; n < ginfo.number_entry; n++) {
        FileToUnzip *file = UnzipFile(allocator, uf, files, dir);
        if (file)
            unzipped.Append(file);
        err = unzGoToNextFile(uf);
        if (err != UNZ_OK)
            break;
    }
    unzClose(uf);

    return WereAllUnzipped(files, unzipped);
}
