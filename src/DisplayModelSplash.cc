/* Written by Krzysztof Kowalczyk (http://blog.kowalczyk.info)
   License: GPLv2 */

#include "DisplayModelSplash.h"
#include <assert.h>
#include "BaseUtils.h"
#include "GlobalParams.h"
#include "GooMutex.h"
#include "GooString.h"
#include "Link.h"
#include "Object.h" /* must be included before SplashOutputDev.h because of sloppiness in SplashOutputDev.h */
#include "PDFDoc.h"
#include "SplashBitmap.h"
#include "SplashOutputDev.h"
#include <stdlib.h> /* malloc etc. */
#include "TextOutputDev.h"

#ifdef _WIN32
#define PREDICTIVE_RENDER 1
#endif

#define MAX_BITMAPS_CACHED 256

#define ACTION_NEXT_PAGE    "NextPage"
#define ACTION_PREV_PAGE    "PrevPage"
#define ACTION_FIRST_PAGE   "FirstPage"
#define ACTION_LAST_PAGE    "LastPage"

static GooMutex             cacheMutex;
static BitmapCacheEntry *   gBitmapCache[MAX_BITMAPS_CACHED] = {0};
static int                  gBitmapCacheCount = 0;

static MutexAutoInitDestroy gAutoCacheMutex(&cacheMutex);

static SplashColorMode              gSplashColorMode = splashModeBGR8;

static SplashColor splashColRed;
static SplashColor splashColGreen;
static SplashColor splashColBlue;
static SplashColor splashColWhite;
static SplashColor splashColBlack;

#define SPLASH_COL_RED_PTR (SplashColorPtr)&(splashColRed[0])
#define SPLASH_COL_GREEN_PTR (SplashColorPtr)&(splashColGreen[0])
#define SPLASH_COL_BLUE_PTR (SplashColorPtr)&(splashColBlue[0])
#define SPLASH_COL_WHITE_PTR (SplashColorPtr)&(splashColWhite[0])
#define SPLASH_COL_BLACK_PTR (SplashColorPtr)&(splashColBlack[0])

static SplashColorPtr  gBgColor = SPLASH_COL_WHITE_PTR;

static void SplashColorSet(SplashColorPtr col, Guchar red, Guchar green, Guchar blue, Guchar alpha)
{
    switch (gSplashColorMode)
    {
        case splashModeBGR8:
            col[0] = blue;
            col[1] = green;
            col[2] = red;
            break;
        case splashModeRGB8:
            col[0] = red;
            col[1] = green;
            col[2] = blue;
            break;
        default:
            assert(0);
            break;
    }
}

void SplashColorsInit(void)
{
    SplashColorSet(SPLASH_COL_RED_PTR, 0xff, 0, 0, 0);
    SplashColorSet(SPLASH_COL_GREEN_PTR, 0, 0xff, 0, 0);
    SplashColorSet(SPLASH_COL_BLUE_PTR, 0, 0, 0xff, 0);
    SplashColorSet(SPLASH_COL_BLACK_PTR, 0, 0, 0, 0);
    SplashColorSet(SPLASH_COL_WHITE_PTR, 0xff, 0xff, 0xff, 0);
}

void CDECL error(int pos, char *msg, ...) {
    va_list args;
    char        buf[4096], *p = buf;

    // NB: this can be called before the globalParams object is created
    if (globalParams && globalParams->getErrQuiet()) {
        return;
    }

    if (pos >= 0) {
        p += _snprintf(p, sizeof(buf)-1, "Error (%d): ", pos);
        *p   = '\0';
        OutputDebugString(p);
    } else {
        OutputDebugString("Error: ");
    }

    p = buf;
    va_start(args, msg);
    p += _vsnprintf(p, sizeof(buf) - 1, msg, args);
    while ( p > buf  &&  isspace(p[-1]) )
            *--p = '\0';
    *p++ = '\r';
    *p++ = '\n';
    *p   = '\0';
    OutputDebugString(buf);
    va_end(args);
}

void LockCache(void) {
    gLockMutex(&cacheMutex);
}

void UnlockCache(void) {
    gUnlockMutex(&cacheMutex);
}

#if 0
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

static void DumpLinks(DisplayModelSplash *dm, PDFDoc *doc)
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
#endif

static void TransfromUpsideDown(DisplayModelSplash *dm, int pageNo, double *y1, double *y2)
{
    assert(dm);
    if (!dm) return;

    PdfPageInfo *pageInfo = dm->getPageInfo(pageNo);
    double dy = pageInfo->pageDy;
    assert(*y1 <= dy);
    *y1 = dy - *y1;
    assert(*y2 <= dy);
    *y2 = dy - *y2;
}

DisplayModelSplash::DisplayModelSplash(DisplayMode displayMode) :
    DisplayModel(displayMode)
{
}

/* TODO: caches dm->textOutDevice, but new TextOutputDev() is cheap so
   we can probably get rid of this function */
static TextOutputDev *GetTextOutDevice(DisplayModelSplash *dm)
{
    assert(dm);
    if (!dm) return NULL;
    if (!dm->textOutDevice)
        dm->textOutDevice = new TextOutputDev(NULL, gTrue, gFalse, gFalse);
    return dm->textOutDevice;
}

TextPage *DisplayModelSplash::GetTextPage(int pageNo)
{
    PdfPageInfo *   pdfPageInfo;
    TextOutputDev * textOut;

    assert(pdfDoc);
    if (!pdfDoc) return NULL;
    assert(validPageNo(pageNo));
    assert(pagesInfo);
    if (!pagesInfo) return NULL;

    pdfPageInfo = &(pagesInfo[pageNo-1]);
    if (!pdfPageInfo->textPage) {
        textOut = GetTextOutDevice(this);
        if (!textOut || !textOut->isOk())
            return NULL;
        pdfDoc->displayPage(textOut, pageNo, 72, 72, 0, gFalse, gTrue, gFalse);
        pdfPageInfo->textPage = textOut->takeText();
    }
    return pdfPageInfo->textPage;
}

SplashBitmap* DisplayModelSplash::GetBitmapForPage(int pageNo, 
    BOOL (*abortCheckCbkA)(void *data),
    void *abortCheckCbkDataA)
{
    int             pageDx, pageDy;
    SplashBitmap *  bmp;
    int             bmpDx, bmpDy;
    PageRenderRequest *req;
    BOOL            aborted = FALSE;

    assert(pdfDoc);
    if (!pdfDoc) return NULL;

    PdfPageInfo *pageInfo = getPageInfo(pageNo);
    pageDx = (int)pageInfo->pageDx;
    pageDy = (int)pageInfo->pageDy;

    DBG_OUT("DisplayModelSplash::GetBitmapForPage(pageNo=%d) orig=(%d,%d) curr=(%d,%d)",
        pageNo,
        pageDx, pageDy,
        (int)pageInfo->currDx,
        (int)pageInfo->currDy);

    assert(outputDevice);
    if (!outputDevice) return NULL;
    bmp = RenderBitmap(this, pageNo, _zoomReal, rotation(), abortCheckCbkA, abortCheckCbkDataA);

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

/* Send the request to render a given page to a rendering thread */
void DisplayModelSplash::startRenderingPage(int pageNo)
{
    RenderQueue_Add(this, pageNo);
}

void DisplayModelSplash::FreeLinks(void)
{
    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        delete pagesInfo[pageNo-1].links;
        pagesInfo[pageNo-1].links = NULL;
    }
}

void DisplayModelSplash::FreeTextPages()
{
    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        delete pagesInfo[pageNo-1].textPage;
        pagesInfo[pageNo-1].textPage = NULL;
    }
}

extern void RenderQueue_RemoveForDisplayModel(DisplayModelSplash *dm);
extern void CancelRenderingForDisplayModel(DisplayModelSplash *dm);

DisplayModelSplash::~DisplayModelSplash()
{
    RenderQueue_RemoveForDisplayModel(this);
    BitmapCache_FreeForDisplayModel(this);
    CancelRenderingForDisplayModel(this);
    FreeLinks();
    FreeTextPages();

    delete outputDevice;
    delete textOutDevice;
    delete searchState.str;
    delete searchState.strU;
    free((void*)_links);
    free((void*)pagesInfo);
    delete pdfDoc;
}

/* Map point <x>/<y> on the page <pageNo> to point on the screen. */
void DisplayModelSplash::cvtUserToScreen(int pageNo, double *x, double *y)
{
    double          xTmp = *x;
    double          yTmp = *y;
    double          ctm[6];
    double          dpi;
    int             rotationTmp;

    assert(pdfDoc);
    if (!pdfDoc) return;

    dpi = (double)PDF_FILE_DPI * _zoomReal * 0.01;
    rotationTmp = rotation();
    normalizeRotation(&rotationTmp);
    pdfDoc->getCatalog()->getPage(pageNo)->getDefaultCTM(ctm, dpi, dpi, rotationTmp, outputDevice->upsideDown());

    PdfPageInfo *pageInfo = getPageInfo(pageNo);
    *x = ctm[0] * xTmp + ctm[2] * yTmp + ctm[4] + 0.5 + pageInfo->currPosX;
    *y = ctm[1] * xTmp + ctm[3] * yTmp + ctm[5] + 0.5 + pageInfo->currPosY;
}

/* Given <region> (in user coordinates ) on page <pageNo>, return a text in that
   region or NULL if no text */
GooString *DisplayModelSplash::GetTextInRegion(int pageNo, RectD *region)
{
    TextOutputDev *     textOut = NULL;
    GooString *         txt = NULL;
    double              xMin, yMin, xMax, yMax;
    double              dpi;
    GBool               useMediaBox = gFalse;
    GBool               crop = gTrue;
    GBool               doLinks = gFalse;

    assert(pdfDoc);
    if (!pdfDoc) return NULL;
    assert(outputDevice);
    if (!outputDevice) return NULL;

    dpi = (double)PDF_FILE_DPI * _zoomReal * 0.01;

    /* TODO: cache textOut? */
    textOut = new TextOutputDev(NULL, gTrue, gFalse, gFalse);
    if (!textOut->isOk()) {
        delete textOut;
        goto Exit;
    }
    /* TODO: make sure we're not doing background threading */
    pdfDoc->displayPage(textOut, pageNo, dpi, dpi, rotation(), useMediaBox, crop, doLinks);
    xMin = region->x;
    yMin = region->y;
    xMax = xMin + region->dx;
    yMax = yMin + region->dy;
    txt = textOut->getText(xMin, yMin, xMax, yMax);
    if (txt && (txt->getLength() > 0)) {
        DBG_OUT("DisplayModelSplash::GetTextInRegion() found text '%s' on pageNo=%d, (x=%d, y=%d), (dx=%d, dy=%d)\n",
            txt->getCString(), pageNo, (int)region->x, (int)region->y, (int)region->dx, (int)region->dy);
    } else {
        DBG_OUT("DisplayModelSplash::GetTextInRegion() didn't find text on pageNo=%d, (x=%d, y=%d), (dx=%d, dy=%d)\n",
            pageNo, (int)region->x, (int)region->y, (int)region->dx, (int)region->dy);
        if (txt) {
            delete txt;
            txt = NULL;
        }
    }
    delete textOut;
Exit:
    return txt;
}

void DisplayModelSplash::changeTotalDrawAreaSize(SizeD totalDrawAreaSize)
{
    int     newPageNo;
    int     currPageNo;

    currPageNo = currentPageNo();

    setTotalDrawAreaSize(totalDrawAreaSize);

    relayout(zoomVirtual(), rotation());
    recalcVisibleParts();
    recalcLinksCanvasPos();
    renderVisibleParts();
    setScrollbarsState();
    newPageNo = currentPageNo();
    if (newPageNo != currPageNo)
        pageChanged();
    repaintDisplay(true);
}

/* Create display info from a 'fileName' etc. 'pdfDoc' will be owned by DisplayInfo
   from now on, so the caller should not delete it itself. Not very good but
   alternative is worse.
   */
DisplayModelSplash *DisplayModelSplash_CreateFromFileName(
  const char *fileName, void *data,
  SizeD totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage)
{
    DisplayModelSplash * dm = new DisplayModelSplash(displayMode);
    if (!dm)
        goto Error;

    if (!dm->load(fileName, startPage))
        goto Error;

    dm->setScrollbarsSize(scrollbarXDy, scrollbarYDx);
    dm->setTotalDrawAreaSize(totalDrawAreaSize);

    GBool bitmapTopDown = gTrue;
    SplashOutputDev *outputDev = new SplashOutputDev(gSplashColorMode, 4, gFalse, gBgColor, bitmapTopDown);
    if (!outputDev)
        goto Error;

    dm->pdfDoc = dm->pdfEnginePoppler()->pdfDoc();
    dm->outputDevice = outputDev;
    dm->textOutDevice = NULL;
    outputDev->startDoc(dm->pdfDoc->getXRef());

    DBG_OUT("DisplayModelSplash::CreateFromPdfDoc() pageCount = %d, startPage=%d, displayMode=%d\n",
        dm->pageCount(), (int)dm->startPage(), (int)displayMode);
    return dm;
Error:
    delete dm;
    return NULL;
}

void DisplayModelSplash::GoToPage(int pageNo, int scrollY, int scrollX)
{
    assert(validPageNo(pageNo));
    if (!validPageNo(pageNo))
        return;

    /* in facing mode only start at odd pages (odd because page
       numbering starts with 1, so odd is really an even page) */
    if (displayModeFacing(displayMode()))
      pageNo = ((pageNo-1) & ~1) + 1;

    if (!displayModeContinuous(displayMode())) {
        /* in single page mode going to another page involves recalculating
           the size of canvas */
        changeStartPage(pageNo);
    }
    //DBG_OUT("DisplayModelSplash::GoToPage(pageNo=%d, scrollY=%d)\n", pageNo, scrollY);
    if (-1 != scrollX)
        areaOffset.x = (double)scrollX;
    PdfPageInfo * pageInfo = getPageInfo(pageNo);

    /* Hack: if an image is smaller in Y axis than the draw area, then we center
       the image by setting pageInfo->currPosY in RecalcPagesInfo. So we shouldn't
       scroll (adjust areaOffset.y) there because it defeats the purpose.
       TODO: is there a better way of y-centering?
       TODO: it probably doesn't work in continuous mode (but that's a corner
             case, I hope) */
    if (!displayModeContinuous(displayMode()))
        areaOffset.y = (double)scrollY;
    else
        areaOffset.y = pageInfo->currPosY - PADDING_PAGE_BORDER_TOP + (double)scrollY;
    /* TODO: prevent scrolling too far */

    recalcVisibleParts();
    recalcLinksCanvasPos();
    renderVisibleParts();
    setScrollbarsState();
    pageChanged();
    repaintDisplay(true);
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
BOOL DisplayModelSplash::GoToNextPage(int scrollY)
{
    int             currPageNo;
    int             newPageNo;
    int             firstPageInCurrRow, firstPageInNewRow;
    int             columns;

    columns = columnsFromDisplayMode(displayMode());
    currPageNo = currentPageNo();
    firstPageInCurrRow = FirstPageInARowNo(currPageNo, columns);
    newPageNo = currPageNo + columns;
    firstPageInNewRow = FirstPageInARowNo(newPageNo, columns);

//    DBG_OUT("DisplayModelSplash::GoToNextPage(scrollY=%d), currPageNo=%d, firstPageInNewRow=%d\n", scrollY, currPageNo, firstPageInNewRow);
    if ((firstPageInNewRow > pageCount()) || (firstPageInCurrRow == firstPageInNewRow)) {
        /* we're on a last row or after it, can't go any further */
        return FALSE;
    }
    GoToPage(firstPageInNewRow, scrollY);
    return TRUE;
}

BOOL DisplayModelSplash::GoToPrevPage(int scrollY)
{
    int             currPageNo;
    int             columns;

    columns = columnsFromDisplayMode(displayMode());
    currPageNo = currentPageNo();
    DBG_OUT("DisplayModelSplash::GoToPrevPage(scrollY=%d), currPageNo=%d\n", scrollY, currPageNo);
    if (currPageNo <= columns) {
        /* we're on a first page, can't go back */
        return FALSE;
    }
    GoToPage(currPageNo - columns, scrollY);
    return TRUE;
}

BOOL DisplayModelSplash::GoToLastPage(void)
{
    int             currPageNo;
    int             firstPageInLastRow;
    int             columns;

    DBG_OUT("DisplayModelSplash::GoToLastPage()\n");

    columns = columnsFromDisplayMode(displayMode());
    currPageNo = currentPageNo();
    firstPageInLastRow = FirstPageInARowNo(pageCount(), columns);

    if (currPageNo != firstPageInLastRow) { /* are we on the last page already ? */
        GoToPage(firstPageInLastRow, 0);
        return TRUE;
    }
    return FALSE;
}

BOOL DisplayModelSplash::GoToFirstPage(void)
{
    DBG_OUT("DisplayModelSplash::GoToFirstPage()\n");

    if (displayModeContinuous(displayMode())) {
        if (0 == areaOffset.y) {
            return FALSE;
        }
    } else {
        assert(pageShown(_startPage));
        if (1 == _startPage) {
            /* we're on a first page already */
            return FALSE;
        }
    }
    GoToPage(1, 0);
    return TRUE;
}

void DisplayModelSplash::ScrollYTo(int yOff)
{
    int             newPageNo;
    int             currPageNo;

    DBG_OUT("DisplayModelSplash::ScrollYTo(yOff=%d)\n", yOff);

    currPageNo = currentPageNo();
    areaOffset.y = (double)yOff;
    recalcVisibleParts();
    recalcLinksCanvasPos();
    renderVisibleParts();

    newPageNo = currentPageNo();
    if (newPageNo != currPageNo)
        pageChanged();
    repaintDisplay(false);
}

void DisplayModelSplash::ScrollXTo(int xOff)
{
    DBG_OUT("DisplayModelSplash::ScrollXTo(xOff=%d)\n", xOff);
    areaOffset.x = (double)xOff;
    recalcVisibleParts();
    recalcLinksCanvasPos();
    setScrollbarsState();
    repaintDisplay(false);
}

void DisplayModelSplash::ScrollXBy(int dx)
{
    double  newX, prevX;
    double  maxX;

    DBG_OUT("DisplayModelSplash::ScrollXBy(dx=%d)\n", dx);

    maxX = _canvasSize.dx - drawAreaSize.dx;
    assert(maxX >= 0.0);
    prevX = areaOffset.x;
    newX = prevX + (double)dx;
    if (newX < 0.0)
        newX = 0.0;
    else
        if (newX > maxX)
            newX = maxX;

    if (newX == prevX)
        return;

    ScrollXTo((int)newX);
}

/* Scroll the doc in y-axis by 'dy'. If 'changePage' is TRUE, automatically
   switch to prev/next page in non-continuous mode if we scroll past the edges
   of current page */
void DisplayModelSplash::ScrollYBy(int dy, bool changePage)
{
    PdfPageInfo *   pageInfo;
    int             currYOff = (int)areaOffset.y;
    int             newYOff;
    int             newPageNo;
    int             currPageNo;

    DBG_OUT("DisplayModelSplash::ScrollYBy(dy=%d, changePage=%d)\n", dy, (int)changePage);
    assert(0 != dy);
    if (0 == dy) return;

    newYOff = currYOff;

    if (!displayModeContinuous(displayMode()) && changePage) {
        if ((dy < 0) && (0 == currYOff)) {
            if (_startPage > 1) {
                newPageNo = _startPage-1;
                assert(validPageNo(newPageNo));
                pageInfo = getPageInfo(newPageNo);
                newYOff = (int)pageInfo->currDy - (int)drawAreaSize.dy;
                if (newYOff < 0)
                    newYOff = 0; /* TODO: center instead? */
                GoToPrevPage(newYOff);
                return;
            }
        }

        /* see if we have to change page when scrolling forward */
        if ((dy > 0) && (_startPage < pageCount())) {
            if ((int)areaOffset.y + (int)drawAreaSize.dy >= (int)_canvasSize.dy) {
                GoToNextPage(0);
                return;
            }
        }
    }

    newYOff += dy;
    if (newYOff < 0) {
        newYOff = 0;
    } else if (newYOff + (int)drawAreaSize.dy > (int)_canvasSize.dy) {
        newYOff = (int)_canvasSize.dy - (int)drawAreaSize.dy;
    }

    if (newYOff == currYOff)
        return;

    currPageNo = currentPageNo();
    areaOffset.y = (double)newYOff;
    recalcVisibleParts();
    recalcLinksCanvasPos();
    renderVisibleParts();
    setScrollbarsState();
    newPageNo = currentPageNo();
    if (newPageNo != currPageNo)
        pageChanged();
    repaintDisplay(false);
}

void DisplayModelSplash::ScrollYByAreaDy(bool forward, bool changePage)
{
    int     toScroll;

    toScroll = (int)drawAreaSize.dy;
    if (forward)
        ScrollYBy(toScroll, changePage);
    else
        ScrollYBy(-toScroll, changePage);
}

/* Make sure that search hit is visible on the screen */
void DisplayModelSplash::EnsureSearchHitVisible()
{
    int             pageNo;
    int             yStart, yEnd;
    int             xStart, xEnd;
    int             xNewPos, yNewPos;
    BOOL            needScroll = FALSE;

    pageNo = searchHitPageNo;
    yStart = searchHitRectCanvas.y;
    yEnd = yStart + searchHitRectCanvas.dy + 24; /* TODO: 24 to account for the find ui bar */

    xStart = searchHitRectCanvas.x;
    xEnd = xStart + searchHitRectCanvas.dx;

    //DBG_OUT("DisplayModelSplash::EnsureSearchHitVisible(), (yStart=%d, yEnd=%d)\n", yStart, yEnd);

    yNewPos = (int)areaOffset.y;

    if (yStart < (int)areaOffset.y) {
        yNewPos -= ((int)areaOffset.y - yStart);
        needScroll = TRUE;
    }
    if (yEnd > (int)(areaOffset.y + drawAreaSize.dy)) {
        yNewPos -= ((int)(areaOffset.y + drawAreaSize.dy) - yEnd);  
        needScroll = TRUE;
    }

    xNewPos = (int)areaOffset.x;
    if (xStart < (int)areaOffset.x) {
        xNewPos -= ((int)areaOffset.x - xStart);
        needScroll = TRUE;
    }
    if (xEnd > (int)(areaOffset.x + drawAreaSize.dx)) {
        xNewPos -= ((int)(areaOffset.x + drawAreaSize.dx) - xEnd);  
        needScroll = TRUE;
    }

    PdfPageInfo *pageInfo = getPageInfo(pageNo);
    if (!pageInfo->visible || needScroll)
        GoToPage(pageNo, yNewPos, xNewPos);
}

void DisplayModelSplash::setDisplayMode(DisplayMode displayMode)
{
    if (_displayMode == displayMode)
        return;

    _displayMode = displayMode;
    int currPageNo = currentPageNo();
    if (displayModeContinuous(displayMode)) {
        /* mark all pages as shown but not yet visible. The equivalent code
           for non-continuous mode is in DisplayModel::changeStartPage() called
           from DisplayModelSplash::GoToPage() */
        for (int pageNo = 1; pageNo <= pageCount(); pageNo++) {
            PdfPageInfo *pageInfo = &(pagesInfo[pageNo-1]);
            pageInfo->shown = true;
            pageInfo->visible = false;
        }
        relayout(zoomVirtual(), rotation());
    }
    GoToPage(currPageNo, 0);
}

void DisplayModelSplash::ZoomTo(double _zoomVirtual)
{
    int     currPageNo;

    //DBG_OUT("DisplayModelSplash::ZoomTo() zoomVirtual=%.6f\n", _zoomVirtual);
    currPageNo = currentPageNo();
    relayout(_zoomVirtual, rotation());
    GoToPage(currPageNo, 0);
}

void DisplayModelSplash::ZoomBy(double zoomFactor)
{
    double newZoom;
    newZoom = _zoomReal * zoomFactor;
    //DBG_OUT("DisplayModelSplash::ZoomBy() zoomReal=%.6f, zoomFactor=%.2f, newZoom=%.2f\n", dm->zoomReal, zoomFactor, newZoom);
    if (newZoom > ZOOM_MAX)
        return;
    ZoomTo(newZoom);
}

void DisplayModelSplash::RotateBy(int newRotation)
{
    int     currPageNo;

    normalizeRotation(&newRotation);
    assert(0 != newRotation);
    if (0 == newRotation)
        return;
    assert(validRotation(newRotation));
    if (!validRotation(newRotation))
        return;

    newRotation += rotation();
    normalizeRotation(&newRotation);
    assert(validRotation(newRotation));
    if (!validRotation(newRotation))
        return;

    currPageNo = currentPageNo();
    relayout(zoomVirtual(), newRotation);
    GoToPage(currPageNo, 0);

}

/* Recalcualte 'linkCount' and 'links' out of 'pagesInfo' data.
   Should only be called if link data has chagned in 'pagesInfo'. */
void DisplayModelSplash::RecalcLinks(void)
{
    int             pageNo;
    int             i;
    PdfPageInfo *   pageInfo;
    Link *          popplerLink;
    PdfLink *       currPdfLink;
    int             currPdfLinkNo;
    double          xs, ys, xe, ye;

    DBG_OUT("DisplayModelSplash::RecalcLinks()\n");

    free((void*)_links);
    _links = NULL;
    _linkCount = 0;

    /* calculate number of links */
    for (pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        pageInfo = getPageInfo(pageNo);
        if (!pageInfo->links)
            continue;
        _linkCount += pageInfo->links->getNumLinks();
    }

    assert(_linkCount > 0);
    _links = (PdfLink*)malloc(_linkCount * sizeof(PdfLink));
    if (!_links)
        return;

    /* build links info */
    currPdfLinkNo = 0;
    for (pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        pageInfo = getPageInfo(pageNo);
        if (!pageInfo->links)
            continue;
        for (i = 0; i < pageInfo->links->getNumLinks(); i++) {
            currPdfLink = link(currPdfLinkNo);
            popplerLink = pageInfo->links->getLink(i);
            popplerLink->getRect(&xs, &ys, &xe, &ye);
            /* note: different param order than getRect() is intentional */
            RectD_FromXY(&currPdfLink->rectPage, xs, xe, ys, ye);
            assert(currPdfLink->rectPage.dx >= 0);
            assert(currPdfLink->rectPage.dy >= 0);
            currPdfLink->pageNo = pageNo;
            currPdfLink->link = popplerLink;
            ++currPdfLinkNo;
        }
    }
    assert(_linkCount == currPdfLinkNo);
    recalcLinksCanvasPos();
    DBG_OUT("DisplayModelSplash::RecalcLinks() new link count: %d\n", _linkCount);
}

void DisplayModelSplash::GoToDest(LinkDest *linkDest)
{
    Ref             pageRef;
    int             newPage = INVALID_PAGE_NO;
    int             left, top;
    int             scrollY = 0;

    assert(linkDest);
    if (!linkDest) return;

    if (linkDest->isPageRef()) {
        pageRef = linkDest->getPageRef();
        newPage = pdfDoc->findPage(pageRef.num, pageRef.gen);
    } else {
        newPage = linkDest->getPageNum();
    }

    if (newPage <= 0 || newPage > pdfDoc->getNumPages()) {
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
        default:
            break;
    }
    GoToPage( newPage, scrollY);
}

void DisplayModelSplash::GoToNamedDest(UGooString *dest)
{
    LinkDest *d;

    assert(dest);
    if (!dest) return;

    d = pdfDoc->findDest(dest);
    assert(d);
    if (!d) return;
    GoToDest(d);
    delete d;
}

void DisplayModelSplash::HandleLinkGoTo(LinkGoTo *linkGoTo)
{
    LinkDest *      linkDest;
    UGooString *    linkNamedDest;

    assert(linkGoTo);
    if (!linkGoTo) return;

    linkDest = linkGoTo->getDest();
    linkNamedDest = linkGoTo->getNamedDest();
    if (linkDest) {
        assert(!linkNamedDest);
        GoToDest(linkDest);
    } else {
        assert(linkNamedDest);
        GoToNamedDest(linkNamedDest);
    }
}

void DisplayModelSplash::HandleLinkGoToR(LinkGoToR *linkGoToR)
{
    LinkDest *      linkDest;
    UGooString *    linkNamedDest;
    GooString *     fileName;

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

void DisplayModelSplash::HandleLinkURI(LinkURI *linkURI)
{
    const char *uri;

    uri = linkURI->getURI()->getCString();
    if (Str_Empty(uri))
        return;
    LaunchBrowser(uri);
}

void DisplayModelSplash::HandleLinkLaunch(LinkLaunch* linkLaunch)
{
    assert(linkLaunch);
    if (!linkLaunch) return;

    /* Launching means executing another application. It's not supported
       due to security and portability reasons */
}

void DisplayModelSplash::HandleLinkNamed(LinkNamed *linkNamed)
{
    GooString * name;
    char *      nameTxt;

    assert(linkNamed);
    if (!linkNamed) return;

    name = linkNamed->getName();
    if (!name)
      return;
    nameTxt = name->getCString();
    if (Str_Eq(ACTION_NEXT_PAGE, nameTxt)) {
        GoToNextPage(0);
    } else if (Str_Eq(ACTION_PREV_PAGE, nameTxt)) {
        GoToPrevPage(0);
    } else if (Str_Eq(ACTION_LAST_PAGE, nameTxt)) {
        GoToLastPage();
    } else if (Str_Eq(ACTION_FIRST_PAGE, nameTxt)) {
        GoToFirstPage();
    } else {
        /* not supporting: "GoBack", "GoForward", "Quit" */
    }
}

/* Return TRUE if can go to previous page (i.e. is not on the first page) */
BOOL DisplayModelSplash::CanGoToPrevPage(void)
{
    if (1 == currentPageNo())
        return FALSE;
    return TRUE;
}

/* Return TRUE if can go to next page (i.e. doesn't already show last page) */
BOOL DisplayModelSplash::CanGoToNextPage(void)
{
    if (displayModeFacing(displayMode())) {
        if (currentPageNo()+1 >= pageCount())
            return FALSE;
    } else {
        if (pageCount() == currentPageNo())
            return FALSE;
    }
    return TRUE;
}

void DisplayModelSplash::FindInit(int startPageNo)
{
    assert(validPageNo(startPageNo));
    searchState.searchState = eSsNone;
    searchState.wrapped = FALSE;
    searchState.startPage = startPageNo;
}

BOOL DisplayModelSplash::FindNextBackward(void)
{
    GBool               startAtTop, stopAtBottom;
    GBool               startAtLast, stopAtLast;
    GBool               caseSensitive, backward;
    GBool               found;
    double              xs, ys, xe, ye;
    UGooString *        strU;
    TextPage *          textPage;
    int                 pageNo;
    RectD               hitRect;

    DBG_OUT("DisplayModelSplash::FindNextBackward()\n");

    // previous search didn't find it, so there's no point looking again
    if (eSsNotFound == searchState.searchState)
        return FALSE;

    // search string was not entered
    if (0 == searchState.str->getLength())
        return FALSE;

    // searching uses the same code as rendering and that code is not
    // thread safe, so we have to cancel all background rendering
    CancelBackgroundRendering();

    ShowBusyCursor();

    backward = gTrue;
    caseSensitive = searchState.caseSensitive;
    strU = searchState.strU;

    if (eSsNone == searchState.searchState) {
        // starting a new search backward
        searchState.currPage = searchState.startPage;
        assert(FALSE == searchState.wrapped);
        startAtLast = gFalse;
        startAtTop = gFalse;
    } else {
        // continuing previous search backward or forward
        startAtLast = gTrue;
        startAtTop = gFalse;
    }
    stopAtBottom = gTrue;
    stopAtLast = gFalse;

    if ((eSsFoundNext == searchState.searchState) 
        || (eSsFoundNextWrapped == searchState.searchState)) {
        searchState.wrapped = FALSE;
    }

    for (;;) {
        pageNo = searchState.currPage;
        textPage = GetTextPage(pageNo);

        DBG_OUT("  search backward for %s, case sensitive=%d, page=%d\n", searchState.str->getCString(), caseSensitive, pageNo);
        found = textPage->findText(strU->unicode(), strU->getLength(), startAtTop, stopAtBottom,
            startAtLast, stopAtLast, caseSensitive, backward,
            &xs, &ys, &xe, &ye);

        if (found) {
            DBG_OUT(" found '%s' on page %d at pos (%.2f, %.2f)-(%.2f,%.2f)\n", 
                searchState.str->getCString(), pageNo, xs, ys, xe, ye);

            TransfromUpsideDown(this, pageNo, &ys, &ye);
            RectD_FromXY(&hitRect, xs, xe, ys, ye);
            if (searchState.wrapped) {
                searchState.wrapped = FALSE;
                searchState.searchState = eSsFoundPrevWrapped;
            } else {
                searchState.searchState = eSsFoundPrev;
            }
            setSearchHit(pageNo, &hitRect);
            EnsureSearchHitVisible();
            goto Exit;
        }

        if (1 == pageNo) {
            DBG_OUT(" wrapped\n");
            searchState.wrapped = TRUE;
            searchState.currPage = pageCount();
        } else
            searchState.currPage = pageNo - 1;

        // moved to another page, so starting from the top
        startAtTop = gTrue;
        startAtLast = gFalse;

        if (searchState.wrapped && 
            (searchState.currPage == searchState.startPage) &&
            (eSsNone == searchState.searchState) ) {
            searchState.searchState = eSsNotFound;
            clearSearchHit();
            goto Exit;
        }
    }
Exit:
    ShowNormalCursor();
    return found;
}

BOOL DisplayModelSplash::FindNextForward(void)
{
    GBool               startAtTop, stopAtBottom;
    GBool               startAtLast, stopAtLast;
    GBool               caseSensitive, backward;
    GBool               found;
    double              xs, ys, xe, ye;
    UGooString *        strU;
    TextPage *          textPage;
    int                 pageNo;
    RectD               hitRect;

    DBG_OUT("DisplayModelSplash::FindNextForward()\n");

    // previous search didn't find it, so there's no point looking again
    if (eSsNotFound == searchState.searchState)
        return FALSE;

    // search string was not entered
    if (0 == searchState.str->getLength())
        return FALSE;

    // searching uses the same code as rendering and that code is not
    // thread safe, so we have to cancel all background rendering
    CancelBackgroundRendering();

    backward = gFalse;
    caseSensitive =searchState.caseSensitive;
    strU = searchState.strU;
    if (eSsNone == searchState.searchState) {
        // starting a new search forward
        DBG_OUT("  new search\n");
        searchState.currPage = searchState.startPage;
        assert(FALSE == searchState.wrapped);
        startAtLast = gFalse;
        startAtTop = gTrue;
    } else {
        // continuing previous search forward or backward
        DBG_OUT("  continue search\n");
        startAtLast = gTrue;
        startAtTop = gFalse;
    }
    stopAtBottom = gTrue;
    stopAtLast = gFalse;

    if ((eSsFoundPrev == searchState.searchState) 
        || (eSsFoundPrevWrapped == searchState.searchState)) {
        searchState.wrapped = FALSE;
    }

    for (;;) {
        pageNo = searchState.currPage;
        textPage = GetTextPage(pageNo);
        DBG_OUT("  search forward for '%s', case sensitive=%d, page=%d\n", searchState.str->getCString(), caseSensitive, pageNo);
        found = textPage->findText(strU->unicode(), strU->getLength(), startAtTop, stopAtBottom,
            startAtLast, stopAtLast, caseSensitive, backward,
            &xs, &ys, &xe, &ye);

        if (found) {
            DBG_OUT(" found '%s' on page %d at pos (%.2f, %.2f)-(%.2f,%.2f)\n", 
                searchState.str->getCString(), pageNo, xs, ys, xe, ye);

            TransfromUpsideDown(this, pageNo, &ys, &ye);
            RectD_FromXY(&hitRect, xs, xe, ys, ye);
            if (searchState.wrapped) {
                searchState.wrapped = FALSE;
                searchState.searchState = eSsFoundNextWrapped;
            } else {
                searchState.searchState = eSsFoundNext;
            }
            setSearchHit(pageNo, &hitRect);
            EnsureSearchHitVisible();
            goto Exit;
        }

        if (pageCount() == pageNo) {
            DBG_OUT(" wrapped\n");
            searchState.wrapped = TRUE;
            searchState.currPage = 1;
        } else
            searchState.currPage = pageNo + 1;

        startAtTop = gTrue;
        startAtLast = gFalse;

        if (searchState.wrapped && 
            (searchState.currPage == searchState.startPage) &&
            (eSsNone == searchState.searchState) ) {
            searchState.searchState = eSsNotFound;
            clearSearchHit();
            goto Exit;
        }
    }
Exit:
    ShowNormalCursor();
    return found;
}

SplashBitmap* RenderBitmap(DisplayModelSplash *dm,
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

    pageInfo = dm->getPageInfo(pageNo);
    if (!pageInfo->links) {
        /* displayPage calculates links for this page (if doLinks is true)
           and puts inside pdfDoc */
        pageInfo->links = dm->pdfDoc->takeLinks();
        if (pageInfo->links->getNumLinks() > 0)
            dm->RecalcLinks();
    }
    return dm->outputDevice->takeBitmap();
}

static void BitmapCacheEntry_Free(BitmapCacheEntry *entry) {
    assert(entry);
    if (!entry) return;
    DBG_OUT("BitmapCacheEntry_Free() page=%d\n", entry->pageNo);
    assert(entry->bitmap);
    delete entry->bitmap;
    free((void*)entry);
}

void BitmapCache_FreeAll(void) {
    LockCache();
    for (int i=0; i < gBitmapCacheCount; i++) {
        BitmapCacheEntry_Free(gBitmapCache[i]);
        gBitmapCache[i] = NULL;
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
        pageInfo = dm->getPageInfo(entry->pageNo);
        shouldFree = FALSE;
        if (!pageInfo->visible)
            shouldFree = TRUE;
        
        if (shouldFree) {
            freedSomething = TRUE;
            BitmapCacheEntry_Free(gBitmapCache[i]);
            gBitmapCache[i] = NULL;
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
BOOL BitmapCache_FreeForDisplayModel(DisplayModelSplash *dm)
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
            gBitmapCache[i] = NULL;
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

void BitmapCache_Add(DisplayModelSplash *dm, int pageNo, double zoomLevel, int rotation, 
    PlatformCachedBitmap *bitmap, double renderTime)
{
    BitmapCacheEntry *entry;
    assert(gBitmapCacheCount <= MAX_BITMAPS_CACHED);
    assert(dm);
    assert(bitmap);
    assert(validRotation(rotation));
    assert(validZoomReal(zoomLevel));

    normalizeRotation(&rotation);
    DBG_OUT("BitmapCache_Add(pageNo=%d, zoomLevel=%.2f%%, rotation=%d)\n", pageNo, zoomLevel, rotation);
    LockCache();
    if (gBitmapCacheCount >= MAX_BITMAPS_CACHED - 1) {
        /* TODO: find entry that is not visible and remove it from cache to
           make room for new entry */
        delete bitmap;
        goto UnlockAndExit;
    }
    entry = (BitmapCacheEntry*)malloc(sizeof(BitmapCacheEntry));
    if (!entry) {
        delete bitmap;
        goto UnlockAndExit;
    }
    entry->dm = dm;
    entry->pageNo = pageNo;
    entry->zoomLevel = zoomLevel;
    entry->rotation = rotation;
    entry->bitmap = bitmap;
    entry->renderTime = renderTime;
    gBitmapCache[gBitmapCacheCount++] = entry;
UnlockAndExit:
    UnlockCache();
}

BitmapCacheEntry *BitmapCache_Find(DisplayModelSplash *dm, int pageNo, double zoomLevel, int rotation) {
    BitmapCacheEntry *entry;

    normalizeRotation(&rotation);
    LockCache();
    for (int i = 0; i < gBitmapCacheCount; i++) {
        entry = gBitmapCache[i];
        if ( (dm == entry->dm) && (pageNo == entry->pageNo) && 
             (zoomLevel == entry->zoomLevel) && (rotation == entry->rotation)) {
             //DBG_OUT("BitmapCache_Find(pageNo=%d, zoomLevel=%.2f%%, rotation=%d) found\n", pageNo, zoomLevel, rotation);
             goto Exit;
        }
    }
    //DBG_OUT("BitmapCache_Find(pageNo=%d, zoomLevel=%.2f%%, rotation=%d) didn't find\n", pageNo, zoomLevel, rotation);
    entry = NULL;
Exit:
    UnlockCache();
    return entry;
}

/* Return TRUE if a bitmap for a page defined by <dm>, <pageNo>, <zoomLevel>
   and <rotation> exists in the cache */
BOOL BitmapCache_Exists(DisplayModelSplash *dm, int pageNo, double zoomLevel, int rotation) {
    BitmapCacheEntry *entry;
    entry = BitmapCache_Find(dm, pageNo, zoomLevel, rotation);
    if (entry)
        return TRUE;
    return FALSE;
}

