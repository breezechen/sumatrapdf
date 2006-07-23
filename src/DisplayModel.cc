/* Written by Krzysztof Kowalczyk (http://blog.kowalczyk.info)
   License: GPLv2 */

#include <assert.h>
#include <stdlib.h> /* malloc etc. */
#include "DisplayModel.h"
#include "SplashBitmap.h"
#include "Object.h" /* must be included before SplashOutputDev.h because of sloppiness in SplashOutputDev.h */
#include "SplashOutputDev.h"
#include "PDFDoc.h"
#include "BaseUtils.h"

static void NormalizeRotation(int *rotation)
{
    assert(rotation);
    if (!rotation) return;
    while (*rotation < 0)
        *rotation += 360;
    while (*rotation >= 360)
        *rotation -= 360;
}

static bool ValidRotation(int rotation)
{
    NormalizeRotation(&rotation);
    if ((0 == rotation) || (90 == rotation) ||
        (180 == rotation) || (270 == rotation))
        return true;
    return false;
}

static int FlippedRotation(int rotation)
{
    assert(ValidRotation(rotation));
    if ((90 == rotation) || (270 == rotation))
        return TRUE;
    return FALSE;
}

static bool ValidZoomReal(double zoomReal)
{
    if ((zoomReal < ZOOM_MIN) || (zoomReal > ZOOM_MAX)) {
        printf("ValidZoomReal() invalid zoom: %.4f\n", zoomReal);
        return false;
    }
    return true;
}

bool ValidZoomVirtual(double zoomVirtual)
{
    if ((ZOOM_FIT_PAGE == zoomVirtual) || (ZOOM_FIT_WIDTH == zoomVirtual))
        return true;
    if ((zoomVirtual < ZOOM_MIN) || (zoomVirtual > ZOOM_MAX)) {
        printf("ValidZoomVirtual() invalid zoom: %.4f\n", zoomVirtual);
        return false;
    }
    return true;
}

/* Given 'pageInfo', which should contain correct information about
   pageDx, pageDy and rotation, return a page size after applying a global
   rotation */
static void PageSizeAfterRotation(PdfPageInfo *pageInfo, int rotation,
    double *pageDxOut, double *pageDyOut)
{
    assert(pageInfo && pageDxOut && pageDyOut);
    if (!pageInfo || !pageDxOut || !pageDyOut)
        return;

    *pageDxOut = pageInfo->pageDx;
    *pageDyOut = pageInfo->pageDy;

    rotation = rotation + pageInfo->rotation;
    NormalizeRotation(&rotation);
    if (FlippedRotation(rotation))
        SwapDouble(pageDxOut, pageDyOut);
}

bool DisplayModel_ValidPageNo(DisplayModel *dm, int pageNo)
{
    assert(dm);
    if (!dm) return false;
    if ((pageNo >= 1) && (pageNo <= dm->pageCount))
        return true;
    return false;
}

PdfPageInfo *DisplayModel_GetPageInfo(DisplayModel *dm, int pageNo)
{
    assert(dm);
    if (!dm) return NULL;
    assert(DisplayModel_ValidPageNo(dm, pageNo));
    assert(dm->pagesInfo);
    return &(dm->pagesInfo[pageNo-1]);
}

SplashBitmap* DisplayModel_GetBitmapForPage(DisplayModel *dm, int pageNo)
{
    PdfPageInfo *pageInfo;
    int         pageDx, pageDy;
    double      hDPI, vDPI;
    GBool       useMediaBox = gFalse;
    GBool       crop        = gTrue;
    GBool       doLinks     = gTrue;
    double      zoomReal;
    int         rotation;
    SplashBitmap *bmp;
    int         bmpDx, bmpDy;

    assert(dm);
    if (!dm) return NULL;
    assert(dm->pdfDoc);
    if (!dm->pdfDoc) return NULL;

    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
    zoomReal = dm->zoomReal;
    rotation = dm->rotation;
    pageDx = (int)pageInfo->pageDx;
    pageDy = (int)pageInfo->pageDy;

    printf("DisplayModel_GetBitmapForPage(pageNo=%d) orig=(%d,%d) curr=(%d,%d) rotate=%d, zoomReal=%.2f%%\n",
        pageNo,
        pageDx, pageDy,
        (int)pageInfo->currDx,
        (int)pageInfo->currDy,
        rotation, zoomReal);

    hDPI = (double)PDF_FILE_DPI * zoomReal * 0.01;
    vDPI = (double)PDF_FILE_DPI * zoomReal * 0.01;
    assert(dm->outputDevice);
    dm->pdfDoc->displayPage(dm->outputDevice, pageNo, hDPI, vDPI, rotation, useMediaBox, crop, doLinks);
    bmp = dm->outputDevice->takeBitmap();
    bmpDx = bmp->getWidth();
    bmpDy = bmp->getHeight();
    if ( (bmpDx != (int)pageInfo->currDx) || (bmpDy != (int)pageInfo->currDy)) {
        printf("  mismatched bitmap sizes!!!\n");
        printf("  calculated: (%4d-%4d)\n", (int)pageInfo->currDx, (int)pageInfo->currDy);
        printf("  real:       (%4d-%4d)\n", bmpDx, bmpDy);
        assert(0);
    }
    return bmp;
}

void DisplayModel_RenderVisibleParts(DisplayModel *dm)
{
    int             pageNo;
    PdfPageInfo*    pageInfo;

    assert(dm);
    if (!dm) return;

    printf("DisplayModel_RenderVisibleParts()\n");
    for (pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        if (pageInfo->visible) {
            assert(pageInfo->shown);
            if (!pageInfo->bitmap)
                pageInfo->bitmap = DisplayModel_GetBitmapForPage(dm, pageNo);
        } else {
            /* TODO: do it lazily from malloc when allocation fails */
            delete pageInfo->bitmap;
            pageInfo->bitmap = NULL;
        }
    }
}

void DisplayModel_FreeBitmaps(DisplayModel *dm)
{
    assert(dm);
    if (!dm) return;

    printf("DisplayModel_FreeBitmaps()\n");
    for (int pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        delete dm->pagesInfo[pageNo-1].bitmap;
        dm->pagesInfo[pageNo-1].bitmap = NULL;
    }
}

void DisplayModel_Delete(DisplayModel *dm)
{
    assert(dm);
    if (!dm) return;

    DisplayModel_FreeBitmaps(dm);

    delete dm->outputDevice;
    free((void*)dm->pagesInfo);
    delete dm->pdfDoc;
    free((void*)dm);
}

void DisplayModel_SetTotalDrawAreaSize(DisplayModel *dm, RectDSize totalDrawAreaSize)
{
    int     newPageNo;
    int     currPageNo;

    assert(dm);
    if (!dm) return;

    currPageNo = DisplayModel_GetCurrentPageNo(dm);
    dm->totalDrawAreaSize = totalDrawAreaSize;
    /* TODO: drawAreaSize not always is minus scrollbars */
    dm->drawAreaSize.dx = dm->totalDrawAreaSize.dx - dm->scrollbarYDx;
    dm->drawAreaSize.dy = dm->totalDrawAreaSize.dy - dm->scrollbarXDy;

    DisplayModel_RecalcVisibleParts(dm);
    DisplayModel_RenderVisibleParts(dm);
    DisplayModel_SetScrollbarsState(dm);
    newPageNo = DisplayModel_GetCurrentPageNo(dm);
    if (newPageNo != currPageNo)
        DisplayModel_PageChanged(dm, newPageNo);
    DisplayModel_RepaintDisplay(dm);
}

/* Create display info from a 'pdfDoc' etc. 'pdfDoc' will be owned by DisplayInfo
   from now on, so the caller should not delete it itself. Not very good but
   alternative is worse.
   TODO: probably shouldn't hard-code SplashOutputDev
   */
DisplayModel *DisplayModel_CreateFromPdfDoc(
  PDFDoc *pdfDoc, SplashOutputDev *outputDev, RectDSize totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  PdfDisplayMode displayMode, int startPage)
{
    PdfPageInfo *pageInfo;
    DisplayModel *  dm = NULL;

    assert(pdfDoc);
    if (!pdfDoc)
        goto Error;

    assert(outputDev);
    if (!outputDev)
        goto Error;

    dm = (DisplayModel*)calloc(1, sizeof(DisplayModel));
    if (!dm)
        goto Error;

    dm->appData = NULL;
    dm->pdfDoc = pdfDoc;
    dm->outputDevice = outputDev;
    dm->totalDrawAreaSize = totalDrawAreaSize;
    dm->scrollbarXDy = scrollbarXDy;
    dm->scrollbarYDx = scrollbarYDx;
    dm->displayMode = displayMode;
    dm->startPage = startPage;
    dm->rotation = INVALID_ROTATION;
    dm->zoomVirtual = INVALID_ZOOM;

    outputDev->startDoc(pdfDoc->getXRef());

    /* TODO: drawAreaSize not always is minus scrollbars */
    dm->drawAreaSize.dx = dm->totalDrawAreaSize.dx - dm->scrollbarYDx;
    dm->drawAreaSize.dy = dm->totalDrawAreaSize.dy - dm->scrollbarXDy;

    dm->pageCount = pdfDoc->getNumPages();
    printf("DisplayModel_CreateFromPdfDoc() pageCount = %d, startPage=%d\n", dm->pageCount, startPage);
    dm->pagesInfo = (PdfPageInfo*)calloc(1, dm->pageCount * sizeof(PdfPageInfo));
    if (!dm->pagesInfo)
        goto Error;

    for (int pageNo = 1; pageNo <= dm->pageCount; pageNo++) {
        pageInfo = &(dm->pagesInfo[pageNo-1]);
        pageInfo->pageDx = pdfDoc->getPageCropWidth(pageNo);
        pageInfo->pageDy = pdfDoc->getPageCropHeight(pageNo);
        pageInfo->rotation = pdfDoc->getPageRotate(pageNo);
        pageInfo->bitmap = NULL;
        pageInfo->shown = false;
        if (DM_CONTINUOUS == displayMode) {
            pageInfo->shown = true;
        } else if (DM_SINGLE_PAGE == displayMode) {
            if (pageNo == startPage) {
                printf("DisplayModel_CreateFromPdfDoc() set page %d as shown\n", pageNo);
                pageInfo->shown = true;
            }
        } else
            assert(0);
        pageInfo->visible = false;
    }

    return dm;
Error:
    if (dm)
        DisplayModel_Delete(dm);
    return NULL;
}

bool DisplayModel_IsPageShown(DisplayModel *dm, int pageNo)
{
    PdfPageInfo *pageInfo;
    bool         shown;

    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
    if (!pageInfo)
        return false;
    shown = pageInfo->shown;
    return shown;
}

int DisplayModel_FindFirstVisiblePageNo(DisplayModel *dm)
{
    PdfPageInfo *   pageInfo;
    int             pageNo;

    assert(dm);
    if (!dm) return INVALID_PAGE;
    assert(dm->pagesInfo);
    if (!dm->pagesInfo) return INVALID_PAGE;

    for (pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        if (pageInfo->visible)
            return pageNo;
    }
    assert(0);
    return INVALID_PAGE;
}

int DisplayModel_GetCurrentPageNo(DisplayModel *dm)
{
    int     currPageNo = INVALID_PAGE;

    printf("DisplayModel_GetCurrentPageNo()\n");

    assert(dm);
    if (!dm) goto Exit;

    if (DM_CONTINUOUS == dm->displayMode) {
        currPageNo = DisplayModel_FindFirstVisiblePageNo(dm);
    } else if (DM_SINGLE_PAGE == dm->displayMode) {
        currPageNo = dm->startPage;
    } else
        assert(0);
Exit:
    return currPageNo;
}

PdfPageInfo* DisplayModel_FindFirstVisiblePage(DisplayModel *dm)
{
    PdfPageInfo *   pageInfo;
    int             pageNo;

    pageNo = DisplayModel_FindFirstVisiblePageNo(dm);
    if (INVALID_PAGE == pageNo)
        return NULL;
    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
    return pageInfo;
}

static void DisplayModel_SetStartPage(DisplayModel *dm, int startPage)
{
    PdfPageInfo     *pageInfo;

    assert(dm);
    if (!dm) return;
    assert(DisplayModel_ValidPageNo(dm, startPage));
    assert(DM_SINGLE_PAGE == dm->displayMode);

    dm->startPage = startPage;
    for (int pageNo = 1; pageNo <= dm->pageCount; pageNo++) {
        pageInfo = &(dm->pagesInfo[pageNo-1]);
        delete pageInfo->bitmap; /* TODO: delete those lazily from allocator */
        pageInfo->bitmap = NULL;
        pageInfo->shown = false;
        if (pageNo == startPage) {
            printf("DisplayModel_SetStartPage() set page %d as shown\n", pageNo);
            pageInfo->shown = true;
        }
        pageInfo->visible = false;
    }
    DisplayModel_RecalcPagesInfo(dm, dm->zoomVirtual, dm->rotation);
}

void DisplayModel_GoToPage(DisplayModel *dm, int pageNo, int scrollY)
{
    PdfPageInfo *   pageInfo;

    assert(dm);
    if (!dm) return;
    assert(DisplayModel_ValidPageNo(dm, pageNo));
    if (!DisplayModel_ValidPageNo(dm, pageNo))
        return;

    if (DM_SINGLE_PAGE == dm->displayMode) {
        /* in single page mode going to another page involves recalculating
           the size of canvas */
        DisplayModel_SetStartPage(dm, pageNo);
        DisplayModel_RecalcPagesInfo(dm, dm->zoomVirtual, dm->rotation);
    }
    printf("DisplayModel_GoToPage(pageNo=%d, scrollY=%d)\n", pageNo, scrollY);
    dm->areaOffset.x = 0.0;
    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
    dm->areaOffset.y = pageInfo->currPosY + (double)scrollY;
    /* TODO: prevent scrolling too far */

    DisplayModel_RecalcVisibleParts(dm);
    DisplayModel_RenderVisibleParts(dm);
    DisplayModel_SetScrollbarsState(dm);
    DisplayModel_PageChanged(dm, pageNo);
    DisplayModel_RepaintDisplay(dm);
}

bool DisplayModel_GoToPrevPage(DisplayModel *dm, int scrollY)
{
    int             currPageNo;

    assert(dm);
    if (!dm) return false;

    if (DM_CONTINUOUS == dm->displayMode) {
        currPageNo = DisplayModel_FindFirstVisiblePageNo(dm);
    } else if (DM_SINGLE_PAGE == dm->displayMode) {
        currPageNo = dm->startPage;
    }
    else {
        assert(0);
        return false;
    }

    printf("DisplayModel_GoToPrevPage(scrollY=%d), currPageNo=%d\n", scrollY, currPageNo);
    if (currPageNo <= 1) {
        /* we're on a first page, can't go back */
        return false;
    }
    DisplayModel_GoToPage(dm, currPageNo - 1, scrollY);
    return true;
}

bool DisplayModel_GoToLastPage(DisplayModel *dm)
{
    int             currPageNo;

    assert(dm);
    if (!dm) return false;

    printf("DisplayModel_GoToLastPage()\n");

    if (DM_CONTINUOUS == dm->displayMode) {
        currPageNo = DisplayModel_FindFirstVisiblePageNo(dm);
    } else if (DM_SINGLE_PAGE == dm->displayMode) {
        currPageNo = dm->startPage;
    }
    else {
        assert(0);
        return false;
    }
    if (currPageNo != dm->pageCount) { /* are we on the last page already ? */
        DisplayModel_GoToPage(dm, dm->pageCount, 0);
        return true;
    }
    return false;
}

bool DisplayModel_GoToFirstPage(DisplayModel *dm)
{
    assert(dm);
    if (!dm) return false;

    printf("DisplayModel_GoToFirstPage()\n");

    if (DM_CONTINUOUS == dm->displayMode) {
        if (0 == dm->areaOffset.y) {
            return false;
        }
    } else if (DM_SINGLE_PAGE == dm->displayMode) {
        assert(DisplayModel_IsPageShown(dm, dm->startPage));
        if (1 == dm->startPage) {
            /* we're on a first page already */
            return false;
        }
    }
    else {
        assert(0);
        return false;
    }
    DisplayModel_GoToPage(dm, 1, 0);

    return true;
}

/* In continuous mode just scrolls to the next page. In single page mode
   rebuilds the display model for the next page.
   Returns true if advanced to the next page or false if couldn't advance
   (e.g. because already was at the last page) */
bool DisplayModel_GoToNextPage(DisplayModel *dm, int scrollY)
{
    int             currPageNo;

    assert(dm);
    if (!dm) return false;

    if (DM_CONTINUOUS == dm->displayMode) {
        currPageNo = DisplayModel_FindFirstVisiblePageNo(dm);
    } else if (DM_SINGLE_PAGE == dm->displayMode) {
        currPageNo = dm->startPage;
    }
    else
        assert(0);

    printf("DisplayModel_GoToNextPage(scrollY=%d), currPageNo=%d\n", scrollY, currPageNo);
    if (dm->pageCount == currPageNo) {
        /* we're on a last page, can't go any further */
        return false;
    }
    DisplayModel_GoToPage(dm, currPageNo + 1, scrollY);
    return true;
}

int DisplayModel_GetSinglePageDy(DisplayModel *dm)
{
    PdfPageInfo *   pageInfo;
    assert(dm);
    if (!dm) return 0;
    assert(DM_SINGLE_PAGE == dm->displayMode);

    pageInfo = DisplayModel_FindFirstVisiblePage(dm);
    assert(pageInfo);
    if (!pageInfo)
        return 0;

    return (int)pageInfo->currDy;
}

void DisplayModel_ScrollYTo(DisplayModel *dm, int yOff)
{
    int             newPageNo;
    int             currPageNo;

    assert(dm);
    if (!dm) return;

    printf("DisplayModel_ScrollYTo(yOff=%d)\n", yOff);
    dm->areaOffset.y = (double)yOff;
    DisplayModel_RecalcVisibleParts(dm);
    DisplayModel_RenderVisibleParts(dm);

    currPageNo = DisplayModel_GetCurrentPageNo(dm);
    newPageNo = DisplayModel_GetCurrentPageNo(dm);
    if (newPageNo != currPageNo)
        DisplayModel_PageChanged(dm, newPageNo);
    DisplayModel_RepaintDisplay(dm);
}

void DisplayModel_ScrollXTo(DisplayModel *dm, int xOff)
{
    assert(dm);
    if (!dm) return;

    printf("DisplayModel_ScrollXTo(xOff=%d)\n", xOff);
    dm->areaOffset.x = (double)xOff;
    DisplayModel_RepaintDisplay(dm);
}

void DisplayModel_ScrollYBy(DisplayModel *dm, int dy, bool changePage)
{
    PdfPageInfo *   pageInfo;
    int             currYOff = (int)dm->areaOffset.y;
    int             newYOff;
    int             newPageNo;
    int             currPageNo;

    assert(dm);
    if (!dm) return;

    printf("DisplayModel_ScrollYBy(dy=%d, changePage=%d)\n", dy, (int)changePage);
    assert(0 != dy);
    if (0 == dy) return;

    newYOff = currYOff;

    if ((DM_SINGLE_PAGE == dm->displayMode) && changePage) {
        if ((dy < 0) && (0 == currYOff)) {
            if (dm->startPage > 1) {
                newPageNo = dm->startPage-1;
                assert(DisplayModel_ValidPageNo(dm, newPageNo));
                pageInfo = DisplayModel_GetPageInfo(dm, newPageNo);
                newYOff = (int)pageInfo->currDy - (int)dm->drawAreaSize.dy;
                if (newYOff < 0)
                    newYOff = 0; /* TODO: center instead? */
                DisplayModel_GoToPrevPage(dm, newYOff);
                return;
            }
        }

        /* see if we have to change page when scrolling forward */
        if ((dy > 0) && (dm->startPage < dm->pageCount)) {
            if ((int)dm->areaOffset.y + (int)dm->drawAreaSize.dy >= (int)dm->canvasSize.dy) {
                DisplayModel_GoToNextPage(dm, 0);
                return;
            }
        }
    }

    newYOff += dy;
    if (newYOff < 0) {
        newYOff = 0;
    } else if (newYOff + (int)dm->drawAreaSize.dy > (int)dm->canvasSize.dy) {
        newYOff = (int)dm->canvasSize.dy - (int)dm->drawAreaSize.dy;
    }

    if (newYOff == currYOff)
        return;

    currPageNo = DisplayModel_GetCurrentPageNo(dm);

    dm->areaOffset.y = (double)newYOff;
    DisplayModel_RecalcVisibleParts(dm);
    DisplayModel_RenderVisibleParts(dm);
    DisplayModel_SetScrollbarsState(dm);
    newPageNo = DisplayModel_GetCurrentPageNo(dm);
    if (newPageNo != currPageNo)
        DisplayModel_PageChanged(dm, newPageNo);
    DisplayModel_RepaintDisplay(dm);
}

void DisplayModel_ScrollYByAreaDy(DisplayModel *dm, bool forward, bool changePage)
{
    assert(dm);
    if (!dm) return;

    if (forward)
        DisplayModel_ScrollYBy(dm, (int)dm->drawAreaSize.dy, changePage);
    else
        DisplayModel_ScrollYBy(dm, -(int)dm->drawAreaSize.dy, changePage);
}

void DisplayModel_SwitchToSinglePage(DisplayModel *dm)
{
    int     currPageNo;

    assert(dm);
    if (!dm) return;

    assert(DM_SINGLE_PAGE != dm->displayMode);

    currPageNo = DisplayModel_FindFirstVisiblePageNo(dm);
    printf("DisplayModel_SwitchToSinglePage() currPageNo = %d\n", currPageNo);
    assert(INVALID_PAGE != currPageNo);
    dm->displayMode = DM_SINGLE_PAGE;
    DisplayModel_GoToPage(dm, currPageNo, 0);
}

void DisplayModel_SwitchToContinuous(DisplayModel *dm)
{
    PdfPageInfo *   pageInfo;
    int             currPageNo;
    assert(dm);
    if (!dm) return;
    assert(DM_CONTINUOUS != dm->displayMode);

    currPageNo = dm->startPage;

    printf("DisplayModel_SwitchToContinuous() currPageNo=%d\n", currPageNo);
    dm->displayMode = DM_CONTINUOUS;

    /* mark all pages as shown but not yet visible */
    for (int pageNo = 1; pageNo <= dm->pageCount; pageNo++) {
        pageInfo = &(dm->pagesInfo[pageNo-1]);
        pageInfo->bitmap = NULL;
        pageInfo->shown = true;
        pageInfo->visible = false;
    }
    DisplayModel_RecalcPagesInfo(dm, dm->zoomVirtual, dm->rotation);
    DisplayModel_GoToPage(dm, currPageNo, 0);
}

void DisplayModel_ToggleContinuous(DisplayModel *dm)
{
    assert(dm);
    if (!dm) return;

    if (DM_CONTINUOUS == dm->displayMode) {
        DisplayModel_SwitchToSinglePage(dm);
    } else if (DM_SINGLE_PAGE == dm->displayMode) {
        DisplayModel_SwitchToContinuous(dm);
    } else
        assert(0);
}

void DisplayModel_ZoomTo(DisplayModel *dm, double zoomVirtual)
{
    int     currPageNo;

    printf("DisplayModel_ZoomTo() zoomVirtual=%.6f\n", zoomVirtual);
    if (DM_CONTINUOUS == dm->displayMode) {
        currPageNo = DisplayModel_FindFirstVisiblePageNo(dm);
        DisplayModel_RecalcPagesInfo(dm, zoomVirtual, dm->rotation);
        DisplayModel_GoToPage(dm, currPageNo, 0);
    } else if (DM_SINGLE_PAGE == dm->displayMode) {
        DisplayModel_RecalcPagesInfo(dm, zoomVirtual, dm->rotation);
        DisplayModel_GoToPage(dm, dm->startPage, 0);
    } else
        assert(0);
}

void DisplayModel_ZoomBy(DisplayModel *dm, double zoomFactor)
{
    double newZoom;
    newZoom = dm->zoomReal * zoomFactor;
    printf("DisplayModel_ZoomBy() zoomReal=%.6f, zoomFactor=%.2f, newZoom=%.2f\n", dm->zoomReal, zoomFactor, newZoom);
    if (newZoom > ZOOM_MAX)
        return;
    DisplayModel_ZoomTo(dm, newZoom);
}

void DisplayModel_RotateBy(DisplayModel *dm, int rotation)
{
    int     newRotation;
    int     currPageNo;

    assert(dm);
    if (!dm) return;

    NormalizeRotation(&rotation);
    assert(0 != rotation);
    if (0 == rotation)
        return;
    assert(ValidRotation(rotation));
    if (!ValidRotation(rotation))
        return;

    newRotation = dm->rotation + rotation;
    NormalizeRotation(&newRotation);
    assert(ValidRotation(newRotation));
    if (!ValidRotation(newRotation))
        return;

    if (DM_CONTINUOUS == dm->displayMode) {
        currPageNo = DisplayModel_FindFirstVisiblePageNo(dm);
        DisplayModel_RecalcPagesInfo(dm, dm->zoomVirtual, newRotation);
        DisplayModel_GoToPage(dm, currPageNo, 0);
    } else if (DM_SINGLE_PAGE == dm->displayMode) {
         DisplayModel_RecalcPagesInfo(dm, dm->zoomVirtual, newRotation);
        DisplayModel_GoToPage(dm, dm->startPage, 0);
    } else
        assert(0);
}

/* Given a zoom level that can include a "virtual" zoom levels like ZOOM_FIT_WIDTH
   and ZOOM_FIT_PAGE, calculate an absolute zoom level */
static double DisplayModel_ZoomRealFromFirtualForPage(DisplayModel *dm, double zoomVirtual, int pageNo)
{
    double          zoomReal, zoomX, zoomY, pageDx, pageDy;
    PdfPageInfo *   pageInfo;

    assert(dm);
    if (!dm) return INVALID_ZOOM;

    assert(0 != (int)dm->drawAreaSize.dx);
    assert(0 != (int)dm->drawAreaSize.dy);

    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
    PageSizeAfterRotation(pageInfo, dm->rotation, &pageDx, &pageDy);

    assert(0 != (int)pageDx);
    assert(0 != (int)pageDy);

    if (ZOOM_FIT_WIDTH == zoomVirtual) {
        /* TODO: should use gWinDx if we don't show scrollbarY */
        zoomReal = ((double)dm->drawAreaSize.dx * 100.0) / (double)pageDx;
    } else if (ZOOM_FIT_PAGE == zoomVirtual) {
        zoomX = ((double)dm->drawAreaSize.dx * 100.0) / (double)pageDx;
        zoomY = ((double)dm->drawAreaSize.dy * 100.0) / (double)pageDy;
        if (zoomX < zoomY)
            zoomReal = zoomX;
        else
            zoomReal= zoomY;
    } else
        zoomReal = zoomVirtual;

    assert(ValidZoomReal(zoomReal));
    return zoomReal;
}

void DisplayModel_SetZoomVirtual(DisplayModel *dm, double zoomVirtual)
{
    int     pageNo;
    double  minZoom = INVALID_BIG_ZOOM;
    double  thisPageZoom;

    assert(dm);
    if (!dm) return;

    assert(ValidZoomVirtual(zoomVirtual));
    dm->zoomVirtual = zoomVirtual;

    if ((ZOOM_FIT_WIDTH == zoomVirtual) || (ZOOM_FIT_PAGE == zoomVirtual)) {
        /* we want the same zoom for all pages, so use the smallest zoom
           across the pages so that the largest page fits. In most PDFs all
           pages are the same size anyway */
        for (pageNo = 1; pageNo <= dm->pageCount; pageNo++) {
            if (DisplayModel_IsPageShown(dm, pageNo)) {
                thisPageZoom = DisplayModel_ZoomRealFromFirtualForPage(dm, zoomVirtual, pageNo);
                assert(0 != thisPageZoom);
                if (minZoom > thisPageZoom)
                    minZoom = thisPageZoom;
            }
        }
        assert(minZoom != INVALID_BIG_ZOOM);
        dm->zoomReal = minZoom;
    } else
        dm->zoomReal = zoomVirtual;
}

/* Given PDFDoc and zoom/rotation, calculate the position of each page on a
   large sheet that is continous view. Needs to be recalculated when:
     * zoom changes
     * rotation changes
     * switching between continuous and single page modes
     * navigating to another page in sing page mode
   TODO: a better name. */
void DisplayModel_RecalcPagesInfo(DisplayModel *dm, double zoomVirtual, int rotation)
{
    double      currPosY;
    double      pageDx, pageDy;
    PdfPageInfo*pageInfo;
    int         currDxInt, currDyInt;
    int         pageNo;
    int         pageCount;
    double      totalAreaDx, totalAreaDy;

    assert(dm);
    if (!dm) return;
    assert(dm->pagesInfo);
    if (!dm->pagesInfo)
        return;

    NormalizeRotation(&rotation);
    assert(ValidRotation(rotation));

    dm->rotation = rotation;
    pageCount = dm->pageCount;

    currPosY = 0.0;
    DisplayModel_SetZoomVirtual(dm, zoomVirtual);

    printf("DisplayModel_RecalcPagesInfo(), pageCount=%d, zoomReal=%.6f, zoomVirtual=%.2f\n",
        pageCount, dm->zoomReal, dm->zoomVirtual);
    totalAreaDx = 0;
    for (pageNo = 1; pageNo <= pageCount; ++pageNo) {

        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        if (!pageInfo->shown) {
            assert(!pageInfo->visible);
            continue;
        }

        PageSizeAfterRotation(pageInfo, rotation, &pageDx, &pageDy);
        currDxInt = (int)(pageDx * dm->zoomReal * 0.01 + 0.5);
        currDyInt = (int)(pageDy * dm->zoomReal * 0.01 + 0.5);
        pageInfo->currDx = (double)currDxInt;
        pageInfo->currDy = (double)currDyInt;

        pageInfo->currPosY = currPosY;
        /* TODO: what should pageInfo->currPosX be ? */
        pageInfo->currPosX = 0.0;
        currPosY += pageInfo->currDy + (double)PAGES_SPACE_DY;
        if (totalAreaDx < pageInfo->currDx)
            totalAreaDx = pageInfo->currDx;
        printf("  page = %3d, (x=%3d, y=%5d, dx=%4d, dy=%4d) orig=(dx=%d,dy=%d)\n",
            pageNo, (int)pageInfo->currPosX, (int)pageInfo->currPosY,
                    (int)pageInfo->currDx, (int)pageInfo->currDy,
                    (int)pageDx, (int)pageDy);
    }
    if (totalAreaDx < dm->drawAreaSize.dx) {
        totalAreaDx = dm->drawAreaSize.dx;
        /* TODO: center all pages */
    }
    totalAreaDy = currPosY;
    if (totalAreaDy < dm->drawAreaSize.dy) {
        totalAreaDy = dm->drawAreaSize.dy;
        /* TODO: set dm->areaOffset so that the image is centered */
    }

    dm->canvasSize.dx = totalAreaDx;
    dm->canvasSize.dy = totalAreaDy;

    /* bitmaps generated before are no longer valid after we changed sizes of pages */
    DisplayModel_FreeBitmaps(dm);
}

/* Given positions of each page in a large sheet that is continous view and
   coordinates of a current view into that large sheet, calculate which
   parts of each page is visible on the screen.
   Needs to be recalucated after scrolling the view. */
void DisplayModel_RecalcVisibleParts(DisplayModel *dm)
{
    int             pageNo;
    SimpleRect      drawAreaRect;
    SimpleRect      pageRect;
    SimpleRect      intersect;
    PdfPageInfo*    pageInfo;

    assert(dm);
    if (!dm) return;
    assert(dm->pagesInfo);
    if (!dm->pagesInfo)
        return;

    drawAreaRect.x = (int)dm->areaOffset.x;
    drawAreaRect.y = (int)dm->areaOffset.y;
    drawAreaRect.dx = (int)dm->drawAreaSize.dx;
    drawAreaRect.dy = (int)dm->drawAreaSize.dy;

    printf("DisplayModel_RecalcVisibleParts() draw area         (x=%3d,y=%3d,dx=%4d,dy=%4d)\n",
        drawAreaRect.x, drawAreaRect.y, drawAreaRect.dx, drawAreaRect.dy);
    for (pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        if (!pageInfo->shown) {
            assert(!pageInfo->visible);
            continue;
        }
        pageRect.x = (int)pageInfo->currPosX;
        pageRect.y = (int)pageInfo->currPosY;
        pageRect.dx = (int)pageInfo->currDx;
        pageRect.dy = (int)pageInfo->currDy;
        pageInfo->visible = false;
        if (SimpleRect_Intersect(&pageRect, &drawAreaRect, &intersect)) {
            pageInfo->visible = true;
            pageInfo->bitmapX = (int) ((double)intersect.x - pageInfo->currPosX);
            assert(pageInfo->bitmapX >= 0);
            pageInfo->bitmapY = (int) ((double)intersect.y - pageInfo->currPosY);
            assert(pageInfo->bitmapY >= 0);
            pageInfo->bitmapDx = intersect.dx;
            pageInfo->bitmapDy = intersect.dy;
            pageInfo->screenX = (int) ((double)intersect.x - dm->areaOffset.x);
            assert(pageInfo->screenX >= 0);
            assert(pageInfo->screenX <= dm->drawAreaSize.dx);
            pageInfo->screenY = (int) ((double)intersect.y - dm->areaOffset.y);
            assert(pageInfo->screenX >= 0);
            assert(pageInfo->screenY <= dm->drawAreaSize.dy);
            printf("                                  visible page = %d, (x=%3d,y=%3d,dx=%4d,dy=%4d) at (x=%d,y=%d)\n",
                pageNo, pageInfo->bitmapX, pageInfo->bitmapY,
                          pageInfo->bitmapDx, pageInfo->bitmapDy,
                          pageInfo->screenX, pageInfo->screenY);
        }
    }
}

