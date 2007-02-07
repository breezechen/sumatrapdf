#include "DisplayModelFitz.h"

// TODO: must go
#include "GooString.h"
#include "UGooString.h"

DisplayModelFitz::DisplayModelFitz(DisplayMode displayMode) :
    DisplayModel(displayMode)
{
    // TODO: probably will need something
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

/* Send the request to render a given page to a rendering thread */
void DisplayModelFitz::startRenderingPage(int pageNo)
{
    // TODO: implement me
}

