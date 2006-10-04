/* Written by Krzysztof Kowalczyk (http://blog.kowalczyk.info)
   License: GPLv2 */

#include <assert.h>
#include <stdlib.h> /* malloc etc. */
#include "DisplayModel.h"
#include "SplashBitmap.h"
#include "Object.h" /* must be included before SplashOutputDev.h because of sloppiness in SplashOutputDev.h */
#include "SplashOutputDev.h"
#include "Link.h"
#include "PDFDoc.h"
#include "BaseUtils.h"
#include "GooMutex.h"

#ifdef _WIN32
#define PREDICTIVE_RENDER 1
#endif

#define MAX_BITMAPS_CACHED 256

#define ACTION_NEXT_PAGE    "NextPage"
#define ACTION_PREV_PAGE    "PrevPage"
#define ACTION_FIRST_PAGE   "FirstPage"
#define ACTION_LAST_PAGE    "LastPage"

/* the default distance between a page and window border edges, in pixels */
#ifdef _WIN32
  #define PADDING_PAGE_BORDER_TOP_DEF      6
#else
  #define PADDING_PAGE_BORDER_TOP_DEF      4
#endif
#define PADDING_PAGE_BORDER_BOTTOM_DEF   PADDING_PAGE_BORDER_TOP_DEF
#define PADDING_PAGE_BORDER_LEFT_DEF     2
#define PADDING_PAGE_BORDER_RIGHT_DEF    2
/* the distance between pages in x axis, in pixels. Only applicable if
   columns > 1 */
#define PADDING_BETWEEN_PAGES_X_DEF      4
/* the distance between pages in y axis, in pixels. Only applicable if
   more than one page in y axis (continuous mode) */
#define PADDING_BETWEEN_PAGES_Y_DEF      PADDING_PAGE_BORDER_TOP_DEF * 2

#define PADDING_PAGE_BORDER_TOP      gDisplaySettings.paddingPageBorderTop
#define PADDING_PAGE_BORDER_BOTTOM   gDisplaySettings.paddingPageBorderBottom
#define PADDING_PAGE_BORDER_LEFT     gDisplaySettings.paddingPageBorderLeft
#define PADDING_PAGE_BORDER_RIGHT    gDisplaySettings.paddingPageBorderRight
#define PADDING_BETWEEN_PAGES_X      gDisplaySettings.paddingBetweenPagesX
#define PADDING_BETWEEN_PAGES_Y      gDisplaySettings.paddingBetweenPagesY

static GooMutex             cacheMutex;
static BitmapCacheEntry *   gBitmapCache[MAX_BITMAPS_CACHED] = {0};
static int                  gBitmapCacheCount = 0;

static MutexAutoInitDestroy gAutoCacheMutex(&cacheMutex);

void LockCache(void) {
    gLockMutex(&cacheMutex);
}

void UnlockCache(void) {
    gUnlockMutex(&cacheMutex);
}

static DisplaySettings gDisplaySettings = {
  PADDING_PAGE_BORDER_TOP_DEF,
  PADDING_PAGE_BORDER_BOTTOM_DEF,
  PADDING_PAGE_BORDER_LEFT_DEF,
  PADDING_PAGE_BORDER_RIGHT_DEF,
  PADDING_BETWEEN_PAGES_X_DEF,
  PADDING_BETWEEN_PAGES_Y_DEF
};

BOOL ValidDisplayMode(DisplayMode dm)
{
    if ((int)dm < (int)DM_FIRST)
        return FALSE;
    if ((int)dm > (int)DM_LAST)
        return FALSE;
    return TRUE;
}

DisplaySettings *DisplayModel_GetGlobalDisplaySettings(void)
{
    return &gDisplaySettings;
}

static int ColumnsFromDisplayMode(DisplayMode displayMode)
{
    if (DM_SINGLE_PAGE == displayMode) {
        return 1;
    } else if (DM_FACING == displayMode) {
        return 2;
    } else if (DM_CONTINUOUS == displayMode) {
        return 1;
    } else if (DM_CONTINUOUS_FACING == displayMode) {
        return 2;
    } else
        assert(0);
    return 1;
}

static BOOL IsDisplayModeContinuous(DisplayMode displayMode)
{
    if ((DM_SINGLE_PAGE == displayMode) || (DM_FACING == displayMode))
        return FALSE;
    else if ((DM_CONTINUOUS == displayMode) || (DM_CONTINUOUS_FACING == displayMode))
        return TRUE;
    assert(0);
    return FALSE;
}

static Links *GetLinksForPage(PDFDoc *doc, int pageNo)
{
    Object obj;
    Catalog *catalog = doc->getCatalog();
    Page *page = catalog->getPage(pageNo);
    Links *links = new Links(page->getAnnots(&obj), catalog->getBaseURI());
    obj.free();
    return links;
}

static int GetPageRotation(PDFDoc *doc, int pageNo)
{
    Catalog *catalog = doc->getCatalog();
    Page *page = catalog->getPage(pageNo);
    int rotation = page->getRotate();
    return rotation;
}

static const char * GetLinkActionKindName(LinkActionKind kind) {
    switch (kind) {
        case (actionGoTo):
            return "actionGoTo";
        case actionGoToR:
            return "actionGoToR";
        case actionLaunch:
            return "actionLaunch";
        case actionURI:
            return "actionURI";
        case actionNamed:
            return "actionNamed";
        case actionMovie:
            return "actionMovie";
        case actionUnknown:
            return "actionUnknown";
        default:
            assert(0);
            return "unknown action";
    }
}

/* Flip coordinates <y1>, <y2>, <y3> and <y4> upside-down if a page <pageNo>
   in <dm> is upside-down */
static void TransformUpsideDown(DisplayModel *dm, int pageNo, double *y1, double *y2, double *y3, double *y4)
{
    PdfPageInfo *   pageInfo;
    double          pageDy;
    assert(dm);
    if (!dm) return;
    if (!dm->outputDevice->upsideDown())
        return;
    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
    pageDy = pageInfo->pageDy;
    if (y1)
        *y1 = pageDy - *y1;
    if (y2)
        *y2 = pageDy - *y2;
    if (y3)
        *y3 = pageDy - *y3;
    if (y4)
        *y4 = pageDy - *y4;
}

static void DumpLinks(DisplayModel *dm, PDFDoc *doc)
{
    Links *     links = NULL;
    int         pagesCount, linkCount;
    int         pageRotation;
    Link *      link;
    LinkURI *   linkUri;
    GBool       upsideDown;

    DBG_OUT("DumpLinks() started\n");
    if (!doc)
        return;

    upsideDown = dm->outputDevice->upsideDown();
    pagesCount = doc->getNumPages();
    for (int pageNo = 1; pageNo < pagesCount; ++pageNo) {
        delete links;
        links = GetLinksForPage(doc, pageNo);
        if (!links)
            goto Exit;
        linkCount = links->getNumLinks();
        if (linkCount > 0)
            DBG_OUT(" :page %d linkCount = %d\n", pageNo, linkCount);
        pageRotation = GetPageRotation(doc, pageNo);
        for (int i=0; i<linkCount; i++) {
            link = links->getLink(i);
            LinkAction *action = link->getAction();
            LinkActionKind actionKind = action->getKind();
            double xs, ys, xe, ye;
            link->getRect(&xs, &ys, &xe, &ye);
            TransformUpsideDown(dm, pageNo, &ys, &ye, NULL, NULL);
            if (ys > ye)
                SwapDouble(&ys, &ye);
            DBG_OUT( "   link %d: pageRotation=%d upsideDown=%d, action=%d (%s), (xs=%d,ys=%d - xe=%d,ye=%d)\n", i,
                pageRotation, (int)upsideDown,
                (int)actionKind,
                GetLinkActionKindName(actionKind),
                (int)xs, (int)ys, (int)xe, (int)ye);
            if (actionURI == actionKind) {
                linkUri = (LinkURI*)action;
                DBG_OUT("   uri=%s\n", linkUri->getURI()->getCString());
            }
        }
    }
Exit:
    delete links;
    DBG_OUT("DumpLinks() finished\n");
}

static int FlippedRotation(int rotation)
{
    assert(ValidRotation(rotation));
    if ((90 == rotation) || (270 == rotation))
        return TRUE;
    return FALSE;
}

/* Transform coordinates in <rectInOut> by a given <rotation> and <zoomLevel> */
static void RectD_Transform(RectD *rectInOut, int rotation, double zoomLevel)
{
    NormalizeRotation(&rotation);
    assert(ValidRotation(rotation));

    if (FlippedRotation(rotation))
        SwapDouble(&(rectInOut->dx), &(rectInOut->dy));

    rectInOut->x = rectInOut->x * zoomLevel;
    rectInOut->y = rectInOut->y * zoomLevel;
    rectInOut->dx = rectInOut->dx * zoomLevel;
    rectInOut->dy = rectInOut->dy * zoomLevel;
}

static bool ValidZoomReal(double zoomReal)
{
    if ((zoomReal < ZOOM_MIN) || (zoomReal > ZOOM_MAX)) {
        DBG_OUT("ValidZoomReal() invalid zoom: %.4f\n", zoomReal);
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
    if (!dm->pagesInfo) return NULL;
    return &(dm->pagesInfo[pageNo-1]);
}

SplashBitmap* DisplayModel_GetBitmapForPage(DisplayModel *dm, int pageNo, 
    BOOL (*abortCheckCbkA)(void *data),
    void *abortCheckCbkDataA)
{
    PdfPageInfo *   pageInfo;
    int             pageDx, pageDy;
    SplashBitmap *  bmp;
    int             bmpDx, bmpDy;
    PageRenderRequest *req;
    BOOL            aborted = FALSE;

    assert(dm);
    if (!dm) return NULL;
    assert(dm->pdfDoc);
    if (!dm->pdfDoc) return NULL;

    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
    pageDx = (int)pageInfo->pageDx;
    pageDy = (int)pageInfo->pageDy;

    DBG_OUT("DisplayModel_GetBitmapForPage(pageNo=%d) orig=(%d,%d) curr=(%d,%d)",
        pageNo,
        pageDx, pageDy,
        (int)pageInfo->currDx,
        (int)pageInfo->currDy);

    assert(dm->outputDevice);
    if (!dm->outputDevice) return NULL;
    bmp = RenderBitmap(dm, pageNo, dm->zoomReal, dm->rotation, abortCheckCbkA, abortCheckCbkDataA);

    bmpDx = bmp->getWidth();
    bmpDy = bmp->getHeight();
    req = (PageRenderRequest*)abortCheckCbkDataA;
    if (req && req->abort)
        aborted = TRUE;
    if ( (bmpDx != (int)pageInfo->currDx) || (bmpDy != (int)pageInfo->currDy)) {
        DBG_OUT("  mismatched bitmap sizes (aborted=%d)!!!\n", aborted);
        DBG_OUT("  calculated: (%4d-%4d)\n", (int)pageInfo->currDx, (int)pageInfo->currDy);
        DBG_OUT("  real:       (%4d-%4d)\n", bmpDx, bmpDy);
        assert(aborted);
    }
    return bmp;
}

extern void RenderQueue_Add(DisplayModel *dm, int pageNo);
/* Send the request to render a given page to a rendering thread */
void DisplayModel_StartRenderingPage(DisplayModel *dm, int pageNo)
{
    RenderQueue_Add(dm, pageNo);
}

void DisplayModel_RenderVisibleParts(DisplayModel *dm)
{
    int             pageNo;
    PdfPageInfo*    pageInfo;
    int             lastVisible = 0;

    assert(dm);
    if (!dm) return;

    DBG_OUT("DisplayModel_RenderVisibleParts()\n");
    for (pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        if (pageInfo->visible) {
            assert(pageInfo->shown);
            DisplayModel_StartRenderingPage(dm, pageNo);
            lastVisible = pageNo;
        }
    }
    assert(0 != lastVisible);
#ifdef PREDICTIVE_RENDER
    if (lastVisible != dm->pageCount) {
        DisplayModel_StartRenderingPage(dm, lastVisible+1);
    }        
#endif
}

void DisplayModel_FreeLinks(DisplayModel *dm)
{
    assert(dm);
    if (!dm) return;

    //DBG_OUT("DisplayModel_FreeLinks()\n");
    for (int pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        delete dm->pagesInfo[pageNo-1].links;
        dm->pagesInfo[pageNo-1].links = NULL;
    }
}

extern void RenderQueue_RemoveForDisplayModel(DisplayModel *dm);

void DisplayModel_Delete(DisplayModel *dm)
{
    if (!dm) return;

    RenderQueue_RemoveForDisplayModel(dm);
    BitmapCache_FreeForDisplayModel(dm);
    DisplayModel_FreeLinks(dm);

    delete dm->outputDevice;
    free((void*)dm->links);
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

    DisplayModel_Relayout(dm, dm->zoomVirtual, dm->rotation);
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
  DisplayMode displayMode, int startPage)
{
    PdfPageInfo *   pageInfo;
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
    dm->fullScreen = FALSE;
    dm->startPage = startPage;
    dm->rotation = INVALID_ROTATION;
    dm->zoomVirtual = INVALID_ZOOM;
    dm->links = NULL;
    dm->linkCount = 0;
    dm->debugShowLinks = FALSE;

    outputDev->startDoc(pdfDoc->getXRef());

    /* TODO: drawAreaSize not always is minus scrollbars (e.g. on Windows)*/
    dm->drawAreaSize.dx = dm->totalDrawAreaSize.dx - dm->scrollbarYDx;
    dm->drawAreaSize.dy = dm->totalDrawAreaSize.dy - dm->scrollbarXDy;

    dm->pageCount = pdfDoc->getNumPages();
    DBG_OUT("DisplayModel_CreateFromPdfDoc() pageCount = %d, startPage=%d, displayMode=%d\n",
        dm->pageCount, (int)dm->startPage, (int)displayMode);
    dm->pagesInfo = (PdfPageInfo*)calloc(1, dm->pageCount * sizeof(PdfPageInfo));
    if (!dm->pagesInfo)
        goto Error;

    for (int pageNo = 1; pageNo <= dm->pageCount; pageNo++) {
        pageInfo = &(dm->pagesInfo[pageNo-1]);
        pageInfo->pageDx = pdfDoc->getPageCropWidth(pageNo);
        pageInfo->pageDy = pdfDoc->getPageCropHeight(pageNo);
        pageInfo->rotation = pdfDoc->getPageRotate(pageNo);
        pageInfo->links = NULL;
        pageInfo->visible = false;
        pageInfo->shown = false;
        if (IsDisplayModeContinuous(dm->displayMode)) {
            pageInfo->shown = true;
        } else {
            if ((pageNo >= startPage) && (pageNo < startPage + ColumnsFromDisplayMode(dm->displayMode))) {
                DBG_OUT("DisplayModel_CreateFromPdfDoc() set page %d as shown\n", pageNo);
                pageInfo->shown = true;
            }
        }
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

    assert(dm);
    if (!dm) goto Exit;

    if (IsDisplayModeContinuous(dm->displayMode)) {
        currPageNo = DisplayModel_FindFirstVisiblePageNo(dm);
    } else {
        currPageNo = dm->startPage;
    }

Exit:
    //DBG_OUT("DisplayModel_GetCurrentPageNo() currPageNo=%d\n", currPageNo);
    return currPageNo;
}

int DisplayModel_GetRotation(DisplayModel *dm)
{
    assert(dm);
    if (!dm) return 0;
    return dm->rotation;
}

double DisplayModel_GetZoomVirtual(DisplayModel *dm)
{
    assert(dm);
    if (!dm) return 100.0;

    DBG_OUT("DisplayModel_GetZoomVirtual() zoom=%.2f\n", dm->zoomVirtual);
    return dm->zoomVirtual;
}

double DisplayModel_GetZoomReal(DisplayModel *dm)
{
    assert(dm);
    if (!dm) return 100.0;

    DBG_OUT("DisplayModel_GetZoomReal() zoom=%.2f\n", dm->zoomReal);
    return dm->zoomReal;
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
    int              columns;

    assert(dm);
    if (!dm) return;
    assert(DisplayModel_ValidPageNo(dm, startPage));
    assert(!IsDisplayModeContinuous(dm->displayMode));

    columns = ColumnsFromDisplayMode(dm->displayMode);
    dm->startPage = startPage;
    for (int pageNo = 1; pageNo <= dm->pageCount; pageNo++) {
        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        if (IsDisplayModeContinuous(dm->displayMode))
            pageInfo->shown = true;
        else
            pageInfo->shown = false;
        if ((pageNo >= startPage) && (pageNo < startPage + columns)) {
            DBG_OUT("DisplayModel_SetStartPage() set page %d as shown\n", pageNo);
            pageInfo->shown = true;
        }
        pageInfo->visible = false;
    }
    DisplayModel_Relayout(dm, dm->zoomVirtual, dm->rotation);
}

void DisplayModel_GoToPage(DisplayModel *dm, int pageNo, int scrollY, int scrollX)
{
    PdfPageInfo *   pageInfo;

    assert(dm);
    if (!dm) return;
    assert(DisplayModel_ValidPageNo(dm, pageNo));
    if (!DisplayModel_ValidPageNo(dm, pageNo))
        return;

    if (!IsDisplayModeContinuous(dm->displayMode)) {
        /* in single page mode going to another page involves recalculating
           the size of canvas */
        DisplayModel_SetStartPage(dm, pageNo);
    }
    //DBG_OUT("DisplayModel_GoToPage(pageNo=%d, scrollY=%d)\n", pageNo, scrollY);
    if (-1 != scrollX)
        dm->areaOffset.x = (double)scrollX;
    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);

    /* Hack: if an image is smaller in Y axis than the draw area, then we center
       the image by setting pageInfo->currPosY in RecalcPagesInfo. So we shouldn't
       scroll (adjust areaOffset.y) there because it defeats the purpose.
       TODO: is there a better way of y-centering?
       TODO: it probably doesn't work in continuous mode (but that's a corner
             case, I hope) */
    if (!IsDisplayModeContinuous(dm->displayMode))
        dm->areaOffset.y = (double)scrollY;
    else
        dm->areaOffset.y = pageInfo->currPosY - PADDING_PAGE_BORDER_TOP + (double)scrollY;
    /* TODO: prevent scrolling too far */

    DisplayModel_RecalcVisibleParts(dm);
    DisplayModel_RenderVisibleParts(dm);
    DisplayModel_SetScrollbarsState(dm);
    DisplayModel_PageChanged(dm, pageNo);
    DisplayModel_RepaintDisplay(dm);
}

/* given 'columns' and an absolute 'pageNo', return the number of the first
   page in a row to which a 'pageNo' belongs e.g. if 'columns' is 2 and we
   have 5 pages in 3 rows:
   (1,2)
   (3,4)
   (5)
   then, we return 1 for pages (1,2), 3 for (3,4) and 5 for (5).
   This is 1-based index, not 0-based. */
static int FirstPageInARowNo(int pageNo, int columns)
{
    int row = ((pageNo - 1) / columns); /* 0-based row number */
    int firstPageNo = row * columns + 1; /* 1-based page in a row */
    return firstPageNo;
}

/* In continuous mode just scrolls to the next page. In single page mode
   rebuilds the display model for the next page.
   Returns true if advanced to the next page or false if couldn't advance
   (e.g. because already was at the last page) */
bool DisplayModel_GoToNextPage(DisplayModel *dm, int scrollY)
{
    int             currPageNo;
    int             newPageNo;
    int             firstPageInCurrRow, firstPageInNewRow;
    int             columns;

    assert(dm);
    if (!dm) return false;

    columns = ColumnsFromDisplayMode(dm->displayMode);
    currPageNo = DisplayModel_GetCurrentPageNo(dm);
    firstPageInCurrRow = FirstPageInARowNo(currPageNo, columns);
    newPageNo = currPageNo + columns;
    firstPageInNewRow = FirstPageInARowNo(newPageNo, columns);

    DBG_OUT("DisplayModel_GoToNextPage(scrollY=%d), currPageNo=%d, firstPageInNewRow=%d\n", scrollY, currPageNo, firstPageInNewRow);
    if ((firstPageInNewRow > dm->pageCount) || (firstPageInCurrRow == firstPageInNewRow)) {
        /* we're on a last row or after it, can't go any further */
        return false;
    }
    DisplayModel_GoToPage(dm, firstPageInNewRow, scrollY);
    return true;
}

bool DisplayModel_GoToPrevPage(DisplayModel *dm, int scrollY)
{
    int             currPageNo;
    int             columns;

    assert(dm);
    if (!dm) return false;

    columns = ColumnsFromDisplayMode(dm->displayMode);
    currPageNo = DisplayModel_GetCurrentPageNo(dm);
    DBG_OUT("DisplayModel_GoToPrevPage(scrollY=%d), currPageNo=%d\n", scrollY, currPageNo);
    if (currPageNo <= columns) {
        /* we're on a first page, can't go back */
        return false;
    }
    DisplayModel_GoToPage(dm, currPageNo - columns, scrollY);
    return true;
}

bool DisplayModel_GoToLastPage(DisplayModel *dm)
{
    int             currPageNo;
    int             firstPageInLastRow;
    int             columns;

    assert(dm);
    if (!dm) return false;

    DBG_OUT("DisplayModel_GoToLastPage()\n");

    columns = ColumnsFromDisplayMode(dm->displayMode);
    currPageNo = DisplayModel_GetCurrentPageNo(dm);
    firstPageInLastRow = FirstPageInARowNo(dm->pageCount, columns);

    if (currPageNo != firstPageInLastRow) { /* are we on the last page already ? */
        DisplayModel_GoToPage(dm, firstPageInLastRow, 0);
        return true;
    }
    return false;
}

bool DisplayModel_GoToFirstPage(DisplayModel *dm)
{
    assert(dm);
    if (!dm) return false;

    DBG_OUT("DisplayModel_GoToFirstPage()\n");

    if (IsDisplayModeContinuous(dm->displayMode)) {
        if (0 == dm->areaOffset.y) {
            return false;
        }
    } else {
        assert(DisplayModel_IsPageShown(dm, dm->startPage));
        if (1 == dm->startPage) {
            /* we're on a first page already */
            return false;
        }
    }
    DisplayModel_GoToPage(dm, 1, 0);
    return true;
}

void DisplayModel_ScrollYTo(DisplayModel *dm, int yOff)
{
    int             newPageNo;
    int             currPageNo;

    assert(dm);
    if (!dm) return;

    DBG_OUT("DisplayModel_ScrollYTo(yOff=%d)\n", yOff);

    currPageNo = DisplayModel_GetCurrentPageNo(dm);
    dm->areaOffset.y = (double)yOff;
    DisplayModel_RecalcVisibleParts(dm);
    DisplayModel_RenderVisibleParts(dm);

    newPageNo = DisplayModel_GetCurrentPageNo(dm);
    if (newPageNo != currPageNo)
        DisplayModel_PageChanged(dm, newPageNo);
    DisplayModel_RepaintDisplay(dm);
}

void DisplayModel_ScrollXTo(DisplayModel *dm, int xOff)
{
    assert(dm);
    if (!dm) return;

    DBG_OUT("DisplayModel_ScrollXTo(xOff=%d)\n", xOff);
    dm->areaOffset.x = (double)xOff;
    DisplayModel_RecalcVisibleParts(dm);
    DisplayModel_RepaintDisplay(dm);
}

void DisplayModel_ScrollXBy(DisplayModel *dm, int dx)
{
    double  newX, prevX;
    double  maxX;

    assert(dm);
    if (!dm) return;

    DBG_OUT("DisplayModel_ScrollXBy(dx=%d)\n", dx);

    maxX = dm->canvasSize.dx - dm->drawAreaSize.dx;
    assert(maxX >= 0.0);
    prevX = dm->areaOffset.x;
    newX = prevX + (double)dx;
    if (newX < 0.0)
        newX = 0.0;
    else
        if (newX > maxX)
            newX = maxX;

    if (newX == prevX)
        return;

    DisplayModel_ScrollXTo(dm, (int)newX);
}

/* Scroll the doc in y-axis by 'dy'. If 'changePage' is TRUE, automatically
   switch to prev/next page in non-continuous mode if we scroll past the edges
   of current page */
void DisplayModel_ScrollYBy(DisplayModel *dm, int dy, bool changePage)
{
    PdfPageInfo *   pageInfo;
    int             currYOff = (int)dm->areaOffset.y;
    int             newYOff;
    int             newPageNo;
    int             currPageNo;

    assert(dm);
    if (!dm) return;

    DBG_OUT("DisplayModel_ScrollYBy(dy=%d, changePage=%d)\n", dy, (int)changePage);
    assert(0 != dy);
    if (0 == dy) return;

    newYOff = currYOff;

    if (!IsDisplayModeContinuous(dm->displayMode) && changePage) {
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
    int     toScroll;
    assert(dm);
    if (!dm) return;

    toScroll = (int)dm->drawAreaSize.dy;
    if (forward)
        DisplayModel_ScrollYBy(dm, toScroll, changePage);
    else
        DisplayModel_ScrollYBy(dm, -toScroll, changePage);
}

void DisplayModel_SetDisplayMode(DisplayModel *dm, DisplayMode displayMode)
{
    int             currPageNo;
    PdfPageInfo *   pageInfo;

    if (dm->displayMode == displayMode)
        return;

    dm->displayMode = displayMode;
    currPageNo = DisplayModel_GetCurrentPageNo(dm);
    if (IsDisplayModeContinuous(displayMode)) {
        /* mark all pages as shown but not yet visible. The equivalent code
           for non-continuous mode is in DisplayModel_SetStartPage() called
           from DisplayModel_GoToPage() */
        for (int pageNo = 1; pageNo <= dm->pageCount; pageNo++) {
            pageInfo = &(dm->pagesInfo[pageNo-1]);
            pageInfo->shown = true;
            pageInfo->visible = false;
        }
        DisplayModel_Relayout(dm, dm->zoomVirtual, dm->rotation);
    }
    DisplayModel_GoToPage(dm, currPageNo, 0);

}

void DisplayModel_ZoomTo(DisplayModel *dm, double zoomVirtual)
{
    int     currPageNo;

    //DBG_OUT("DisplayModel_ZoomTo() zoomVirtual=%.6f\n", zoomVirtual);
    currPageNo = DisplayModel_GetCurrentPageNo(dm);
    DisplayModel_Relayout(dm, zoomVirtual, dm->rotation);
    DisplayModel_GoToPage(dm, currPageNo, 0);
}

void DisplayModel_ZoomBy(DisplayModel *dm, double zoomFactor)
{
    double newZoom;
    newZoom = dm->zoomReal * zoomFactor;
    //DBG_OUT("DisplayModel_ZoomBy() zoomReal=%.6f, zoomFactor=%.2f, newZoom=%.2f\n", dm->zoomReal, zoomFactor, newZoom);
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

    currPageNo = DisplayModel_GetCurrentPageNo(dm);
    DisplayModel_Relayout(dm, dm->zoomVirtual, newRotation);
    DisplayModel_GoToPage(dm, currPageNo, 0);

}

/* Given a zoom level that can include a "virtual" zoom levels like ZOOM_FIT_WIDTH
   and ZOOM_FIT_PAGE, calculate an absolute zoom level */
static double DisplayModel_ZoomRealFromFirtualForPage(DisplayModel *dm, double zoomVirtual, int pageNo)
{
    double          zoomReal, zoomX, zoomY, pageDx, pageDy;
    double          areaForPageDx, areaForPageDy;
    int             areaForPageDxInt;
    PdfPageInfo *   pageInfo;
    int             columns;

    assert(dm);
    if (!dm) return INVALID_ZOOM;

    assert(0 != (int)dm->drawAreaSize.dx);
    assert(0 != (int)dm->drawAreaSize.dy);

    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
    PageSizeAfterRotation(pageInfo, dm->rotation, &pageDx, &pageDy);

    assert(0 != (int)pageDx);
    assert(0 != (int)pageDy);

    columns = ColumnsFromDisplayMode(dm->displayMode);
    areaForPageDx = (dm->drawAreaSize.dx - PADDING_PAGE_BORDER_LEFT - PADDING_PAGE_BORDER_RIGHT);
    areaForPageDx -= (PADDING_BETWEEN_PAGES_X * (columns - 1));
    areaForPageDxInt = (int)(areaForPageDx / columns);
    areaForPageDx = (double)areaForPageDxInt;
    areaForPageDy = (double)dm->drawAreaSize.dy - PADDING_PAGE_BORDER_TOP - PADDING_PAGE_BORDER_BOTTOM;
    if (ZOOM_FIT_WIDTH == zoomVirtual) {
        /* TODO: should use gWinDx if we don't show scrollbarY */
        zoomReal = (areaForPageDx * 100.0) / (double)pageDx;
    } else if (ZOOM_FIT_PAGE == zoomVirtual) {
        zoomX = (areaForPageDx * 100.0) / (double)pageDx;
        zoomY = (areaForPageDy * 100.0) / (double)pageDy;
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

/* Recalculates the position of each link on the canvas i.e. applies current
   rotation and zoom level and offsets it by the offset of each page in
   the canvas.
   TODO: applying rotation and zoom level could be split into a separate
         function for speedup, since it only has to change after rotation/zoomLevel
         changes while this function has to be called after each scrolling.
         But I'm not sure if this would be a significant speedup */
static void DisplayModel_RecalcLinksCanvasPos(DisplayModel *dm)
{
    PdfLink *       pdfLink;
    PdfPageInfo *   pageInfo;
    int             rotation;
    int             linkNo;
    double          zoomReal;
    RectD           rect;
    SimpleRect      rectCanvas;

    assert(dm);
    if (!dm) return;

    //DBG_OUT("DisplayModel_RecalcLinksCanvasPos() links=%d, rotation=%d, zoom=%2.f\n", dm->linkCount, dm->rotation, dm->zoomReal);
    if (0 == dm->linkCount)
        return;
    assert(dm->links);
    if (!dm->links)
        return;

    zoomReal = dm->zoomReal * 0.01;

    for (linkNo = 0; linkNo < dm->linkCount; linkNo++) {
        pdfLink = &(dm->links[linkNo]);
        pageInfo = DisplayModel_GetPageInfo(dm, pdfLink->pageNo);
        rotation = dm->rotation + pageInfo->rotation;
        if (!pageInfo->visible) {
            /* hack: make the links on pages that are not shown invisible by
                     moving it off canvas. A better solution would probably be
                     not adding those links in the first place */
            pdfLink->rectCanvas.x = -100;
            pdfLink->rectCanvas.y = -100;
            pdfLink->rectCanvas.dx = 0;
            pdfLink->rectCanvas.dy = 0;
            continue;
        }

        rect = pdfLink->rectPage;
        RectD_Transform(&rect, rotation, zoomReal);
        rectCanvas.x = (int)pageInfo->currPosX + (int)rect.x;
        rectCanvas.y = (int)pageInfo->currPosY + (int)rect.y;
        rectCanvas.dx = (int)rect.dx;
        rectCanvas.dy = (int)rect.dy;
        pdfLink->rectCanvas = rectCanvas;
/*        DBG_OUT("  link on canvas (x=%d, y=%d, dx=%d, dy=%d)\n",
                  rectCanvas.x, rectCanvas.y,
                  rectCanvas.dx, rectCanvas.dy); */
    }
}

/* Recalcualte 'dm->linkCount' and 'dm->links' out of 'dm->pagesInfo' data.
   Should only be called if link data has chagned in 'dm->pagesInfo'. */
static void DisplayModel_RecalcLinks(DisplayModel *dm)
{
    int             linkCount;
    int             pageNo;
    int             i;
    PdfPageInfo *   pageInfo;
    Link *          link;
    PdfLink *       currPdfLink;
    int             currPdfLinkNo;
    double          xs, ys, xe, ye;

    assert(dm);
    if (!dm) return;

    free((void*)dm->links);
    dm->linkCount = 0;

    /* calculate number of links */
    linkCount = 0;
    for (pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        if (!pageInfo->links)
            continue;
        linkCount += pageInfo->links->getNumLinks();
    }

    assert(linkCount > 0);
    dm->links = (PdfLink*)malloc(linkCount * sizeof(PdfLink));
    if (!dm->links)
        return;

    /* build links info */
    currPdfLinkNo = 0;
    for (pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        if (!pageInfo->links)
            continue;
        for (i = 0; i < pageInfo->links->getNumLinks(); i++) {
            currPdfLink = &(dm->links[currPdfLinkNo]);
            link = pageInfo->links->getLink(i);
            link->getRect(&xs, &ys, &xe, &ye);
            TransformUpsideDown(dm, pageNo, &ys, &ye, NULL, NULL);
            if (ys > ye)
                SwapDouble(&ys, &ye);
            /* note: different param order than getRect() is intentional */
            RectD_FromXY(&currPdfLink->rectPage, xs, xe, ys, ye);
            assert(currPdfLink->rectPage.dx >= 0);
            assert(currPdfLink->rectPage.dy >= 0);
            currPdfLink->pageNo = pageNo;
            currPdfLink->link = link;
            ++currPdfLinkNo;
        }
    }
    assert(linkCount == currPdfLinkNo);
    dm->linkCount = linkCount;
    DBG_OUT("DisplayModel_RecalcLinks() new link count: %d\n", dm->linkCount);
}

/* Given PDFDoc and zoom/rotation, calculate the position of each page on a
   large sheet that is continous view. Needs to be recalculated when:
     * zoom changes
     * rotation changes
     * switching between display modes
     * navigating to another page in non-continuous mode */
void DisplayModel_Relayout(DisplayModel *dm, double zoomVirtual, int rotation)
{
    int         pageNo;
    PdfPageInfo*pageInfo = NULL;
    double      currPosX, currPosY;
    double      pageDx, pageDy;
    int         currDxInt, currDyInt;
    int         pageCount;
    double      totalAreaDx, totalAreaDy;
    double      areaPerPageDx;
    int         areaPerPageDxInt;
    double      thisRowDx;
    double      rowMaxPageDy;
    double      offX, offY;
    double      pageOffX;
    int         columnsLeft;
    int         pageInARow;
    int         columns;
    double      currZoomReal;
    BOOL        freeCache = FALSE;
    double      newAreaOffsetX;

    assert(dm);
    if (!dm) return;
    assert(dm->pagesInfo);
    if (!dm->pagesInfo)
        return;

    NormalizeRotation(&rotation);
    assert(ValidRotation(rotation));

    if (rotation != dm->rotation)
        freeCache = TRUE;
    dm->rotation = rotation;
    pageCount = dm->pageCount;

    currPosY = PADDING_PAGE_BORDER_TOP;
    currZoomReal = dm->zoomReal;
    DisplayModel_SetZoomVirtual(dm, zoomVirtual);
    if (currZoomReal != dm->zoomReal)
        freeCache = TRUE;

    DBG_OUT("DisplayModel_Relayout(), pageCount=%d, zoomReal=%.6f, zoomVirtual=%.2f\n",
        pageCount, dm->zoomReal, dm->zoomVirtual);
    totalAreaDx = 0;

    if (0 == currZoomReal)
        newAreaOffsetX = 0.0;
    else
        newAreaOffsetX = dm->areaOffset.x * dm->zoomReal / currZoomReal;
    dm->areaOffset.x = newAreaOffsetX;
    /* calculate the position of each page on the canvas, given current zoom,
       rotation, columns parameters. You can think of it as a simple
       table layout i.e. rows with a fixed number of columns. */
    columns = ColumnsFromDisplayMode(dm->displayMode);
    columnsLeft = columns;
    currPosX = PADDING_PAGE_BORDER_LEFT;
    rowMaxPageDy = 0;
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

        if (rowMaxPageDy < pageInfo->currDy)
            rowMaxPageDy = pageInfo->currDy;

        pageInfo->currPosX = currPosX;
        pageInfo->currPosY = currPosY;
        /* set position of the next page to be after this page with padding.
           Note: for the last page we don't want padding so we'll have to
           substract it when we create new page */
        currPosX += (pageInfo->currDx + PADDING_BETWEEN_PAGES_X);

        --columnsLeft;
        assert(columnsLeft >= 0);
        if (0 == columnsLeft) {
            /* starting next row */
            currPosY += rowMaxPageDy + PADDING_BETWEEN_PAGES_Y;
            rowMaxPageDy = 0;
            thisRowDx = currPosX - PADDING_BETWEEN_PAGES_X + PADDING_PAGE_BORDER_RIGHT;
            if (totalAreaDx < thisRowDx)
                totalAreaDx = thisRowDx;
            columnsLeft = columns;
            currPosX = PADDING_PAGE_BORDER_LEFT;
        }
/*        DBG_OUT("  page = %3d, (x=%3d, y=%5d, dx=%4d, dy=%4d) orig=(dx=%d,dy=%d)\n",
            pageNo, (int)pageInfo->currPosX, (int)pageInfo->currPosY,
                    (int)pageInfo->currDx, (int)pageInfo->currDy,
                    (int)pageDx, (int)pageDy); */
    }

    if (columnsLeft < columns) {
        /* this is a partial row */
        currPosY += rowMaxPageDy + PADDING_BETWEEN_PAGES_Y;
        thisRowDx = currPosX + (pageInfo->currDx + PADDING_BETWEEN_PAGES_X) - PADDING_BETWEEN_PAGES_X + PADDING_PAGE_BORDER_RIGHT;
        if (totalAreaDx < thisRowDx)
            totalAreaDx = thisRowDx;
    }

    /* since pages can be smaller than the drawing area, center them in x axis */
    if (totalAreaDx < dm->drawAreaSize.dx) {
        dm->areaOffset.x = 0.0;
        offX = (dm->drawAreaSize.dx - totalAreaDx) / 2.0 + PADDING_PAGE_BORDER_LEFT;
        assert(offX >= 0.0);
        areaPerPageDx = totalAreaDx - PADDING_PAGE_BORDER_LEFT - PADDING_PAGE_BORDER_RIGHT;
        areaPerPageDx = areaPerPageDx - (PADDING_BETWEEN_PAGES_X * (columns - 1));
        areaPerPageDxInt = (int)(areaPerPageDx / (double)columns);
        areaPerPageDx = (double)areaPerPageDxInt;
        totalAreaDx = dm->drawAreaSize.dx;
        pageInARow = 0;
        for (pageNo = 1; pageNo <= pageCount; ++pageNo) {
            pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
            if (!pageInfo->shown) {
                assert(!pageInfo->visible);
                continue;
            }
            pageOffX = (pageInARow * (PADDING_BETWEEN_PAGES_X + areaPerPageDx));
            pageOffX += (areaPerPageDx - pageInfo->currDx) / 2;
            assert(pageOffX >= 0.0);
            pageInfo->currPosX = pageOffX + offX;
            ++pageInARow;
            if (pageInARow == columns)
                pageInARow = 0;
        }
    }

    /* if after resizing we would have blank space on the right due to x offset
       being too much, make x offset smaller so that there's no blank space */
    if (dm->drawAreaSize.dx - (totalAreaDx - newAreaOffsetX) > 0) {
        newAreaOffsetX = totalAreaDx - dm->drawAreaSize.dx;
        dm->areaOffset.x = newAreaOffsetX;
    }

    /* if a page is smaller than drawing area in y axis, y-center the page */
    totalAreaDy = currPosY + PADDING_PAGE_BORDER_BOTTOM - PADDING_BETWEEN_PAGES_Y;
    if (totalAreaDy < dm->drawAreaSize.dy) {
        offY = PADDING_PAGE_BORDER_TOP + (dm->drawAreaSize.dy - totalAreaDy) / 2;
        DBG_OUT("  offY = %.2f\n", offY);
        assert(offY >= 0.0);
        totalAreaDy = dm->drawAreaSize.dy;
        for (pageNo = 1; pageNo <= pageCount; ++pageNo) {
            pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
            if (!pageInfo->shown) {
                assert(!pageInfo->visible);
                continue;
            }
            pageInfo->currPosY += offY;
            DBG_OUT("  page = %3d, (x=%3d, y=%5d, dx=%4d, dy=%4d) orig=(dx=%d,dy=%d)\n",
                pageNo, (int)pageInfo->currPosX, (int)pageInfo->currPosY,
                        (int)pageInfo->currDx, (int)pageInfo->currDy,
                        (int)pageDx, (int)pageDy);
        }
    }

    dm->canvasSize.dx = totalAreaDx;
    dm->canvasSize.dy = totalAreaDy;

    DisplayModel_RecalcLinksCanvasPos(dm);
    if (freeCache) {
        /* bitmaps generated before are no longer valid if we changed rotation
           or zoom level */
        BitmapCache_FreeForDisplayModel(dm);
    }
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
    int             visibleCount;

    assert(dm);
    if (!dm) return;
    assert(dm->pagesInfo);
    if (!dm->pagesInfo)
        return;

    drawAreaRect.x = (int)dm->areaOffset.x;
    drawAreaRect.y = (int)dm->areaOffset.y;
    drawAreaRect.dx = (int)dm->drawAreaSize.dx;
    drawAreaRect.dy = (int)dm->drawAreaSize.dy;

//    DBG_OUT("DisplayModel_RecalcVisibleParts() draw area         (x=%3d,y=%3d,dx=%4d,dy=%4d)\n",
//        drawAreaRect.x, drawAreaRect.y, drawAreaRect.dx, drawAreaRect.dy);
    visibleCount = 0;
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
            visibleCount += 1;
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
/*            DBG_OUT("                                  visible page = %d, (x=%3d,y=%3d,dx=%4d,dy=%4d) at (x=%d,y=%d)\n",
                pageNo, pageInfo->bitmapX, pageInfo->bitmapY,
                          pageInfo->bitmapDx, pageInfo->bitmapDy,
                          pageInfo->screenX, pageInfo->screenY); */
        }
    }

    assert(visibleCount > 0);
}

/* Given position 'x'/'y' in the draw area, returns a structure describing
   a link or NULL if there is no link at this position.
   Note: DisplayModel owns this memory so it should not be changed by the
   caller and caller should not reference it after it has changed (i.e. process
   it immediately since it will become invalid after each _Relayout()).
   TODO: this function is called frequently from UI code so make sure that
         it's fast enough for a decent number of link.
         Possible speed improvement: remember which links are visible after
         scrolling and skip the _Inside test for those invisible.
         Another way: build another list with only those visible, so we don't
         even have to travers those that are invisible.
   */
PdfLink *DisplayModel_GetLinkAtPosition(DisplayModel *dm, int x, int y)
{
    PdfLink *       currLink;
    int             i;
    int             canvasPosX, canvasPosY;

    assert(dm);
    if (!dm) return NULL;

    if (0 == dm->linkCount)
        return NULL;

    assert(dm->links);
    if (!dm->links)
        return NULL;

    canvasPosX = x + (int)dm->areaOffset.x;
    canvasPosY = y + (int)dm->areaOffset.y;
    for (i = 0; i < dm->linkCount; i++) {
        currLink = &(dm->links[i]);

        if (SimpleRect_Inside(&(currLink->rectCanvas), canvasPosX, canvasPosY))
            return currLink;
    }
    return NULL;
}

static void DisplayModel_GoToDest(DisplayModel *dm, LinkDest *linkDest)
{
    Ref             pageRef;
    int             newPage = INVALID_PAGE;
    int             left, top;
    int             scrollY = 0;

    assert(dm);
    if (!dm) return;
    assert(linkDest);
    if (!linkDest) return;

    if (linkDest->isPageRef()) {
        pageRef = linkDest->getPageRef();
        newPage = dm->pdfDoc->findPage(pageRef.num, pageRef.gen);
    } else {
        newPage = linkDest->getPageNum();
    }

    if (newPage <= 0 || newPage > dm->pdfDoc->getNumPages()) {
        newPage = 1;
    }

    left = (int)linkDest->getLeft();
    top = (int)linkDest->getTop();
    /* TODO: convert left/top coordinates to window space */

    /* TODO: this logic needs to be implemented */
    switch (linkDest->getKind()) {
        case destXYZ:
            break;
        case destFitR:
            break;
    }
    DisplayModel_GoToPage(dm, newPage, scrollY);
}

static void DisplayModel_GoToNamedDest(DisplayModel *dm, UGooString *dest)
{
    LinkDest *d;

    assert(dm);
    if (!dm) return;
    assert(dest);
    if (!dest) return;

    d = dm->pdfDoc->findDest(dest);
    assert(d);
    if (!d) return;
    DisplayModel_GoToDest(dm, d);
    delete d;
}

void DisplayModel_HandleLinkGoTo(DisplayModel *dm, LinkGoTo *linkGoTo)
{
    LinkDest *      linkDest;
    UGooString *    linkNamedDest;

    assert(dm);
    if (!dm) return;
    assert(linkGoTo);
    if (!linkGoTo) return;

    linkDest = linkGoTo->getDest();
    linkNamedDest = linkGoTo->getNamedDest();
    if (linkDest) {
        assert(!linkNamedDest);
        DisplayModel_GoToDest(dm, linkDest);
    } else {
        assert(linkNamedDest);
        DisplayModel_GoToNamedDest(dm, linkNamedDest);
    }
}

void DisplayModel_HandleLinkGoToR(DisplayModel *dm, LinkGoToR *linkGoToR)
{
    LinkDest *      linkDest;
    UGooString *    linkNamedDest;
    GooString *     fileName;

    assert(dm);
    if (!dm) return;
    assert(linkGoToR);
    if (!linkGoToR) return;

    linkDest = linkGoToR->getDest();
    linkNamedDest = linkGoToR->getNamedDest();

    fileName = linkGoToR->getFileName();
    /* TODO: see if a file exists, if not show a dialog box. If exists,
       load it and go to a destination in this file.
       Should also search currently opened files. */
    /* Test file: C:\kjk\test_pdfs\pda\palm\dev tools\sdk\user_interface.pdf, page 633 */
}

void DisplayModel_HandleLinkURI(DisplayModel *dm, LinkURI *linkURI)
{
    const char *uri;

    uri = linkURI->getURI()->getCString();
    if (Str_Empty(uri))
        return;
    LaunchBrowser(uri);
}

void DisplayModel_HandleLinkLaunch(DisplayModel *dm, LinkLaunch* linkLaunch)
{
    assert(dm);
    if (!dm) return;
    assert(linkLaunch);
    if (!linkLaunch) return;

    /* Launching means executing another application. It's not supported
       due to security and portability reasons */
}

void DisplayModel_HandleLinkNamed(DisplayModel *dm, LinkNamed *linkNamed)
{
    GooString * name;
    char *      nameTxt;

    assert(dm);
    if (!dm) return;
    assert(linkNamed);
    if (!linkNamed) return;

    name = linkNamed->getName();
    if (!name)
      return;
    nameTxt = name->getCString();
    if (Str_Eq(ACTION_NEXT_PAGE, nameTxt)) {
        DisplayModel_GoToNextPage(dm, 0);
    } else if (Str_Eq(ACTION_PREV_PAGE, nameTxt)) {
        DisplayModel_GoToPrevPage(dm, 0);
    } else if (Str_Eq(ACTION_LAST_PAGE, nameTxt)) {
        DisplayModel_GoToLastPage(dm);
    } else if (Str_Eq(ACTION_FIRST_PAGE, nameTxt)) {
        DisplayModel_GoToFirstPage(dm);
    } else {
        /* not supporting: "GoBack", "GoForward", "Quit" */
    }
}

BOOL DisplayState_FromDisplayModel(DisplayState *ds, DisplayModel *dm)
{
    ds->filePath = Str_Escape(dm->pdfDoc->getFileName()->getCString());
    if (!ds->filePath)
        return FALSE;
    ds->displayMode = dm->displayMode;
    ds->fullScreen = dm->fullScreen;
    ds->pageNo = DisplayModel_GetCurrentPageNo(dm);
    ds->rotation = DisplayModel_GetRotation(dm);
    ds->zoomVirtual = DisplayModel_GetZoomVirtual(dm);
    ds->scrollX = (int)dm->areaOffset.x;
    if (IsDisplayModeContinuous(dm->displayMode)) {
        /* TODO: should be offset of top page */
        ds->scrollY = 0;
    } else {
        ds->scrollY = (int)dm->areaOffset.y;
    }
    ds->windowDx = (int)dm->drawAreaSize.dx;
    ds->windowDy = (int)dm->drawAreaSize.dy;
    ds->windowX = 0;
    ds->windowY = 0;
    return TRUE;
}

SplashBitmap* RenderBitmap(DisplayModel *dm,
                           int pageNo, double zoomReal, int rotation,
                           BOOL (*abortCheckCbkA)(void *data),
                           void *abortCheckCbkDataA)
{
    double          hDPI, vDPI;
    GBool           useMediaBox = gFalse;
    GBool           crop        = gTrue;
    GBool           doLinks     = gTrue;
    PdfPageInfo *   pageInfo;

    DBG_OUT("RenderBitmap(pageNo=%d) rotate=%d, zoomReal=%.2f%%\n", pageNo, rotation, zoomReal);

    hDPI = (double)PDF_FILE_DPI * zoomReal * 0.01;
    vDPI = (double)PDF_FILE_DPI * zoomReal * 0.01;
    assert(dm->outputDevice);
    if (!dm->outputDevice) return NULL;
    dm->pdfDoc->displayPage(dm->outputDevice, pageNo, hDPI, vDPI, rotation, useMediaBox, crop, doLinks,
        abortCheckCbkA, abortCheckCbkDataA);
    pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
    if (!pageInfo->links) {
        /* displayPage calculates links for this page (if doLinks is true)
           and puts inside pdfDoc */
        pageInfo->links = dm->pdfDoc->takeLinks();
        if (pageInfo->links->getNumLinks() > 0)
            DisplayModel_RecalcLinks(dm);
    }
    return dm->outputDevice->takeBitmap();
}

static void BitmapCacheEntry_Free(BitmapCacheEntry *entry) {
    assert(entry);
    if (!entry) return;
    DBG_OUT("BitmapCacheEntry_Free() page=%d\n", entry->pageNo);
    assert(entry->bitmap);
    delete entry->bitmap;
    entry->bitmap = NULL;
}

void BitmapCache_FreeAll(void) {
    LockCache();
    for (int i=0; i < gBitmapCacheCount; i++) {
        BitmapCacheEntry_Free(gBitmapCache[i]);
    }
    gBitmapCacheCount = 0;
    UnlockCache();
}

/* Free all bitmaps in the cache that are not visible. Returns TRUE if freed
   at least one item. */
BOOL BitmapCache_FreeNotVisible(void)
{
    int                 curPos = 0;
    int                 i;
    BOOL                shouldFree;
    BitmapCacheEntry *  entry;
    DisplayModel *      dm;
    PdfPageInfo *       pageInfo;
    BOOL                freedSomething = FALSE;
    int                 cacheCount;

    DBG_OUT("BitmapCache_FreeNotVisible()\n");
    LockCache();
    cacheCount = gBitmapCacheCount;
    for (i = 0; i < cacheCount; i++) {
        entry = gBitmapCache[i];
        dm = entry->dm;
        pageInfo = DisplayModel_GetPageInfo(dm, entry->pageNo);
        shouldFree = FALSE;
        if (!pageInfo->visible)
            shouldFree = TRUE;
        
        if (shouldFree) {
            freedSomething = TRUE;
            BitmapCacheEntry_Free(gBitmapCache[i]);
            --gBitmapCacheCount;
        }

        if (curPos != i)
            gBitmapCache[curPos] = gBitmapCache[i];

        if (!shouldFree)
            ++curPos;
    }
    UnlockCache();
    return freedSomething;
}

/* Free all bitmaps cached for a given <dm>. Returns TRUE if freed
   at least one item. */
BOOL BitmapCache_FreeForDisplayModel(DisplayModel *dm)
{
    int     curPos = 0;
    int     i;
    BOOL    shouldFree;
    BOOL    freedSomething = FALSE;
    int     cacheCount;

    DBG_OUT("BitmapCache_FreeForDisplayModel()\n");
    LockCache();
    cacheCount = gBitmapCacheCount;
    for (i = 0; i < cacheCount; i++) {
        shouldFree = (gBitmapCache[i]->dm == dm);
        if (shouldFree) {
            freedSomething = TRUE;
            BitmapCacheEntry_Free(gBitmapCache[i]);
            --gBitmapCacheCount;
        }

        if (curPos != i)
            gBitmapCache[curPos] = gBitmapCache[i];

        if (!shouldFree)
            ++curPos;
    }
    UnlockCache();
    return freedSomething;
}

void BitmapCache_Add(DisplayModel *dm, int pageNo, double zoomLevel, int rotation, SplashBitmap *bmp)
{
    BitmapCacheEntry *entry;
    assert(gBitmapCacheCount <= MAX_BITMAPS_CACHED);
    assert(dm);
    assert(bmp);
    assert(ValidRotation(rotation));
    assert(ValidZoomReal(zoomLevel));

    NormalizeRotation(&rotation);
    DBG_OUT("BitmapCache_Add(pageNo=%d, zoomLevel=%.2f%%, rotation=%d)\n", pageNo, zoomLevel, rotation);
    LockCache();
    entry = (BitmapCacheEntry*)malloc(sizeof(BitmapCacheEntry));
    if (!entry) {
        delete bmp;
        return;
    }
    entry->dm = dm;
    entry->pageNo = pageNo;
    entry->zoomLevel = zoomLevel;
    entry->rotation = rotation;
    entry->bitmap = bmp;
    if (gBitmapCacheCount >= MAX_BITMAPS_CACHED - 1) {
        /* TODO: find entry that is not visible and remove it from cache to
           make room for new entry */
        BitmapCacheEntry_Free(entry);
        UnlockCache();
        return;
    }
    gBitmapCache[gBitmapCacheCount++] = entry;
    UnlockCache();
}

BitmapCacheEntry *BitmapCache_Find(DisplayModel *dm, int pageNo, double zoomLevel, int rotation) {
    BitmapCacheEntry *entry;

    NormalizeRotation(&rotation);
    DBG_OUT("BitmapCache_Find(pageNo=%d, zoomLevel=%.2f%%, rotation=%d)\n", pageNo, zoomLevel, rotation);
    LockCache();
    for (int i = 0; i < gBitmapCacheCount; i++) {
        entry = gBitmapCache[i];
        if ( (dm == entry->dm) && (pageNo == entry->pageNo) && 
             (zoomLevel == entry->zoomLevel) && (rotation == entry->rotation)) {
             DBG_OUT("   found\n");
             goto Exit;
        }
    }
    DBG_OUT("   didn't find\n");
    entry = NULL;
Exit:
    UnlockCache();
    return entry;
}

/* Return TRUE if a bitmap for a page defined by <dm>, <pageNo>, <zoomLevel>
   and <rotation> exists in the cache */
BOOL BitmapCache_Exists(DisplayModel *dm, int pageNo, double zoomLevel, int rotation) {
    BitmapCacheEntry *entry;
    entry = BitmapCache_Find(dm, pageNo, zoomLevel, rotation);
    if (entry)
        return TRUE;
    return FALSE;
}

