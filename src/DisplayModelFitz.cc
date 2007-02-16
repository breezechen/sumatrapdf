#include "DisplayModelFitz.h"

// TODO: must go
#include "GooString.h"
#include "UGooString.h"

HBITMAP RenderedBitmapFitz::CreateDIBitmap(HDC)
{
    return NULL;
}

DisplayModelFitz::DisplayModelFitz(DisplayMode displayMode) :
    DisplayModel(displayMode)
{
    _pdfEngine = new PdfEngineFitz();
}

DisplayModelFitz::~DisplayModelFitz()
{
    // TODO: probably will need something
}

DisplayModelFitz *DisplayModelFitz_CreateFromFileName(
  const char *fileName, void *data,
  SizeD totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage)
{
    DisplayModelFitz *    dm = NULL;

    dm = new DisplayModelFitz(displayMode);
    if (!dm)
        goto Error;

    if (!dm->load(fileName, startPage))
        goto Error;

    dm->setScrollbarsSize(scrollbarXDy, scrollbarYDx);
    dm->setTotalDrawAreaSize(totalDrawAreaSize);

//    DBG_OUT("DisplayModelFitz_CreateFromPageTree() pageCount = %d, startPage=%d, displayMode=%d\n",
//        dm->pageCount(), (int)dm->startPage, (int)displayMode);
    return dm;
Error:
    delete dm;
    return NULL;
}

void DisplayModelFitz::cvtUserToScreen(int pageNo, double *x, double *y)
{
    // TODO: implement me
}

PlatformRenderedBitmap *DisplayModelFitz::renderBitmap(
                           int pageNo, double zoomReal, int rotation,
                           BOOL (*abortCheckCbkA)(void *data),
                           void *abortCheckCbkDataA)
{
    return NULL;
}

