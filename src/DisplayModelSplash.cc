/* Written by Krzysztof Kowalczyk (http://blog.kowalczyk.info)
   License: GPLv2 */

#include "DisplayModelSplash.h"
#include "str_util.h"

#include "GlobalParams.h"
#include "GooMutex.h"
#include "GooString.h"
#include "Link.h"
#include "Object.h" /* must be included before SplashOutputDev.h because of sloppiness in SplashOutputDev.h */
#include "PDFDoc.h"
#include "SplashBitmap.h"
#include "SplashOutputDev.h"
#include "TextOutputDev.h"

#include <assert.h>
#include <stdlib.h> /* malloc etc. */

#ifdef _WIN32
#define PREDICTIVE_RENDER 1
#endif

#define ACTION_NEXT_PAGE    "NextPage"
#define ACTION_PREV_PAGE    "PrevPage"
#define ACTION_FIRST_PAGE   "FirstPage"
#define ACTION_LAST_PAGE    "LastPage"

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
    _pdfEngine = new PdfEnginePoppler();
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

#if 0
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
#endif

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

extern void RenderQueue_RemoveForDisplayModel(DisplayModel *dm);
extern void CancelRenderingForDisplayModel(DisplayModel *dm);

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
        goToPage(pageNo, yNewPos, xNewPos);
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
    goToPage( newPage, scrollY);
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
    if (str_empty(uri))
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
    if (str_eq(ACTION_NEXT_PAGE, nameTxt)) {
        goToNextPage(0);
    } else if (str_eq(ACTION_PREV_PAGE, nameTxt)) {
        goToPrevPage(0);
    } else if (str_eq(ACTION_LAST_PAGE, nameTxt)) {
        goToLastPage();
    } else if (str_eq(ACTION_FIRST_PAGE, nameTxt)) {
        goToFirstPage();
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
    cancelBackgroundRendering();

    showBusyCursor();

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
    showNormalCursor();
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
    cancelBackgroundRendering();

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
    showNormalCursor();
    return found;
}

RenderedBitmap *DisplayModelSplash::renderBitmap(
                           int pageNo, double zoomReal, int rotation,
                           BOOL (*abortCheckCbkA)(void *data),
                           void *abortCheckCbkDataA)
{
    double          hDPI, vDPI;
    GBool           useMediaBox = gFalse;
    GBool           crop        = gTrue;
    GBool           doLinks     = gTrue;

    DBG_OUT("DisplayModelSplash::RenderBitmap(pageNo=%d) rotate=%d, zoomReal=%.2f%%\n", pageNo, rotation, zoomReal);

    hDPI = (double)PDF_FILE_DPI * zoomReal * 0.01;
    vDPI = (double)PDF_FILE_DPI * zoomReal * 0.01;
    assert(outputDevice);
    if (!outputDevice) return NULL;
    pdfDoc->displayPage(outputDevice, pageNo, hDPI, vDPI, rotation, useMediaBox, crop, doLinks,
        abortCheckCbkA, abortCheckCbkDataA);

    PdfPageInfo *pageInfo = getPageInfo(pageNo);
    if (!pageInfo->links) {
        /* displayPage calculates links for this page (if doLinks is true)
           and puts inside pdfDoc */
        pageInfo->links = pdfDoc->takeLinks();
        if (pageInfo->links->getNumLinks() > 0)
            RecalcLinks();
    }
    RenderedBitmapSplash *renderedBitmap = new RenderedBitmapSplash(outputDevice->takeBitmap());
    return renderedBitmap;
}

#if 0
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
#endif
