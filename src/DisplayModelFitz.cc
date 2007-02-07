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

void DisplayModelFitz::setDisplayMode(DisplayMode displayMode)
{
#if 0  // TODO: obviously
    int             currPageNo;
    PdfPageInfo *   pageInfo;

    if (_displayMode == displayMode)
        return;

    _displayMode = displayMode;
    currPageNo = currentPageNo();
    if (displayModeContinuous(displayMode)) {
        /* mark all pages as shown but not yet visible. The equivalent code
           for non-continuous mode is in DisplayModel::changeStartPage() called
           from DisplayModel::GoToPage() */
        for (int pageNo = 1; pageNo <= pageCount(); pageNo++) {
            pageInfo = &(pagesInfo[pageNo-1]);
            pageInfo->shown = true;
            pageInfo->visible = false;
        }
        relayout(zoomVirtual(), rotation());
    }
    GoToPage(currPageNo, 0);
#endif
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

void DisplayModelFitz::CvtUserToScreen(int pageNo, double *x, double *y)
{
    // TODO: implement me
}
