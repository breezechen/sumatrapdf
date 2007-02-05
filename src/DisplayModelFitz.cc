#include "DisplayModelFitz.h"

// TODO: must go
#include "GooString.h"
#include "UGooString.h"

DisplayModelFitz::DisplayModelFitz(DisplayMode displayMode)
{
    // TODO: probably will need something
}

DisplayModelFitz::~DisplayModelFitz()
{
    // TODO: probably will need something
}

int DisplayModelFitz::currentPageNo(void) const
{
    // TODO: probably will need something
    return 1;
}

void DisplayModelFitz::setZoomVirtual(double zoomVirtual)
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
    if (IsDisplayModeContinuous(displayMode)) {
        /* mark all pages as shown but not yet visible. The equivalent code
           for non-continuous mode is in DisplayModelSplash::SetStartPage() called
           from DisplayModelSplash::GoToPage() */
        for (int pageNo = 1; pageNo <= pageCount(); pageNo++) {
            pageInfo = &(pagesInfo[pageNo-1]);
            pageInfo->shown = true;
            pageInfo->visible = false;
        }
        Relayout(zoomVirtual(), rotation());
    }
    GoToPage(currPageNo, 0);
#endif
}


static DisplayModelFitz *DisplayModelFitz_CreateFromFileName(
  const char *fileName, void *data,
  RectDSize totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage)
{
    PdfPageInfo *           pageInfo;
    DisplayModelFitz *    dm = NULL;

    dm = new DisplayModelFitz(displayMode);
    if (!dm)
        goto Error;

    if (!dm->load(fileName))
        goto Error;

//    dm->textOutDevice = NULL;
    dm->totalDrawAreaSize = totalDrawAreaSize;
    dm->scrollbarXDy = scrollbarXDy;
    dm->scrollbarYDx = scrollbarYDx;
    dm->setFullScreen(false);
//    dm->startPage = startPage;
    dm->searchState.searchState = eSsNone;
    dm->searchState.str = new GooString();
    dm->searchState.strU = new UGooString();
    dm->searchHitPageNo = INVALID_PAGE_NO;

//    outputDev->startDoc(pdfDoc->getXRef());

    /* TODO: drawAreaSize not always is minus scrollbars (e.g. on Windows)*/
    dm->drawAreaSize.dx = dm->totalDrawAreaSize.dx - dm->scrollbarYDx;
    dm->drawAreaSize.dy = dm->totalDrawAreaSize.dy - dm->scrollbarXDy;

//    DBG_OUT("DisplayModelFitz_CreateFromPageTree() pageCount = %d, startPage=%d, displayMode=%d\n",
//        dm->pageCount(), (int)dm->startPage, (int)displayMode);
    dm->pagesInfo = (PdfPageInfo*)calloc(1, dm->pageCount() * sizeof(PdfPageInfo));
    if (!dm->pagesInfo)
        goto Error;

    for (int pageNo = 1; pageNo <= dm->pageCount(); pageNo++) {
        pageInfo = &(dm->pagesInfo[pageNo-1]);
//        pageInfo->pageDx = pdfDoc->getPageCropWidth(pageNo);
//        pageInfo->pageDy = pdfDoc->getPageCropHeight(pageNo);
//        pageInfo->rotation = pdfDoc->getPageRotate(pageNo);
        pageInfo->links = NULL;
        pageInfo->textPage = NULL;
        pageInfo->visible = false;
        pageInfo->shown = false;
#if 0
        if (IsDisplayModeContinuous(dm->displayMode())) {
            pageInfo->shown = true;
        } else {
            if ((pageNo >= startPage) && (pageNo < startPage + ColumnsFromDisplayMode(dm->displayMode()))) {
                DBG_OUT("DisplayModelSplash::CreateFromPdfDoc() set page %d as shown\n", pageNo);
                pageInfo->shown = true;
            }
        }
#endif
    }

    return dm;
Error:
    delete dm;
    return NULL;
}


