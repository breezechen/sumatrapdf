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

void DisplayModelFitz::SetZoomVirtual(double zoomVirtual)
{
    // TODO: probably will need something
}

void DisplayModelFitz::SetDisplayMode(DisplayMode displayMode)
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


static DisplayModelFitz *DisplayModelFitz_CreateFromPageTree(
  pdf_pagetree *pageTree,
  RectDSize totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage)
{
    PdfPageInfo *           pageInfo;
    DisplayModelFitz *    dm = NULL;

    assert(pageTree);
    if (!pageTree)
        goto Error;

    dm = new DisplayModelFitz(displayMode);
    if (!dm)
        goto Error;

//    dm->setFileName(pdfDoc->getFileName()->getCString());
//    dm->pdfDoc = pdfDoc;
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

//    dm->setPageCount(pdfDoc->getNumPages());
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

DisplayModelFitz *DisplayModelFitz_CreateFromFileName(
  const char *fileName, void *data,
  RectDSize totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage)
{
    pdf_xref *          xref;
    pdf_pagetree *      pages;
    fz_error *          error;

    error = pdf_newxref(&xref);
    if (error)
        goto Error;

    error = pdf_loadxref(xref, (char*)fileName);
    if (error) {
        if (!strncmp(error->msg, "ioerror", 7))
            goto Error;
        error = pdf_repairxref(xref, (char*)fileName);
        if (error)
            goto Error;
    }

    error = pdf_decryptxref(xref);
    if (error)
        goto Error;

    if (xref->crypt) {
#ifdef FITZ_HEAD
        int okay = pdf_setpassword(xref->crypt, "");
        if (!okay)
            goto Error;
#else
        error = pdf_setpassword(xref->crypt, "");
        if (error)
            goto Error;
#endif
    }

    error = pdf_loadpagetree(&pages, xref);
    if (error)
        goto Error;

    return DisplayModelFitz_CreateFromPageTree(pages, totalDrawAreaSize,
        scrollbarXDy, scrollbarYDx, displayMode, startPage);
Error:
    return NULL;
}

