#include "PdfEngine.h"

#include <assert.h>

#include <fitz.h>
#include <mupdf.h>

#include "ErrorCodes.h"
#include "GooString.h"
#include "GooList.h"
#include "GlobalParams.h"
#include "SplashBitmap.h"
#include "Object.h" /* must be included before SplashOutputDev.h because of sloppiness in SplashOutputDev.h */
#include "SplashOutputDev.h"
#include "TextOutputDev.h"
#include "PDFDoc.h"
#include "SecurityHandler.h"
#include "Link.h"

bool PdfEnginePoppler::load(const char *fileName)
{
    setFileName(fileName);
    return false;
}

int PdfEnginePoppler::pageCount(void)
{
    return INVALID_PAGE_NO;
}

int PdfEnginePoppler::pageRotation(int pageNo)
{
    assert(validPageNo(pageNo));
    return INVALID_ROTATION;
}

SizeD PdfEnginePoppler::pageSize(int pageNo)
{
    return SizeD(0,0);
}

bool PdfEngineFitz::load(const char *fileName)
{
    setFileName(fileName);
    // TODO: implement me
    return false;
}

int PdfEngineFitz::pageCount(void)
{
    return INVALID_PAGE_NO;
}

int PdfEngineFitz::pageRotation(int pageNo)
{
    assert(validPageNo(pageNo));
    return INVALID_ROTATION;
}

SizeD PdfEngineFitz::pageSize(int pageNo)
{
    return SizeD(0,0);
}

