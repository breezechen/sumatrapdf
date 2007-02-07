#include "DisplayModel.h"
#include <assert.h>
#include <stdlib.h>

// TODO: get rid of the need for GooString and UGooString in common code
#include "GooString.h"
#include "UGooString.h"

DisplaySettings gDisplaySettings = {
  PADDING_PAGE_BORDER_TOP_DEF,
  PADDING_PAGE_BORDER_BOTTOM_DEF,
  PADDING_PAGE_BORDER_LEFT_DEF,
  PADDING_PAGE_BORDER_RIGHT_DEF,
  PADDING_BETWEEN_PAGES_X_DEF,
  PADDING_BETWEEN_PAGES_Y_DEF
};

bool validZoomReal(double zoomReal)
{
    if ((zoomReal < ZOOM_MIN) || (zoomReal > ZOOM_MAX)) {
        DBG_OUT("validZoomReal() invalid zoom: %.4f\n", zoomReal);
        return false;
    }
    return true;
}

#if 0  // FIXME:: remove me
bool validDisplayMode(DisplayMode dm)
{
    if ((int)dm < (int)DM_FIRST)
        return false;
    if ((int)dm > (int)DM_LAST)
        return false;
    return true;
}
#endif

bool displayModeFacing(DisplayMode displayMode)
{
    if ((DM_SINGLE_PAGE == displayMode) || (DM_CONTINUOUS == displayMode))
        return false;
    else if ((DM_FACING == displayMode) || (DM_CONTINUOUS_FACING == displayMode))
        return true;
    assert(0);
    return false;
}

bool displayModeContinuous(DisplayMode displayMode)
{
    if ((DM_SINGLE_PAGE == displayMode) || (DM_FACING == displayMode))
        return false;
    else if ((DM_CONTINUOUS == displayMode) || (DM_CONTINUOUS_FACING == displayMode))
        return true;
    assert(0);
    return false;
}

int columnsFromDisplayMode(DisplayMode displayMode)
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

DisplaySettings *globalDisplaySettings(void)
{
    return &gDisplaySettings;
}

int FlippedRotation(int rotation)
{
    assert(validRotation(rotation));
    if ((90 == rotation) || (270 == rotation))
        return TRUE;
    return FALSE;
}

BOOL DisplayState_FromDisplayModel(DisplayState *ds, DisplayModel *dm)
{
    ds->filePath = Str_Escape(dm->fileName());
    if (!ds->filePath)
        return FALSE;
    ds->displayMode = dm->displayMode();
    ds->fullScreen = dm->fullScreen();
    ds->pageNo = dm->currentPageNo();
    ds->rotation = dm->rotation();
    ds->zoomVirtual = dm->zoomVirtual();
    ds->scrollX = (int)dm->areaOffset.x;
    if (displayModeContinuous(dm->displayMode())) {
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

/* Given 'pageInfo', which should contain correct information about
   pageDx, pageDy and rotation, return a page size after applying a global
   rotation */
void pageSizeAfterRotation(PdfPageInfo *pageInfo, int rotation,
    double *pageDxOut, double *pageDyOut)
{
    assert(pageInfo && pageDxOut && pageDyOut);
    if (!pageInfo || !pageDxOut || !pageDyOut)
        return;

    *pageDxOut = pageInfo->pageDx;
    *pageDyOut = pageInfo->pageDy;

    rotation = rotation + pageInfo->rotation;
    normalizeRotation(&rotation);
    if (FlippedRotation(rotation))
        SwapDouble(pageDxOut, pageDyOut);
}

DisplayModel::DisplayModel(DisplayMode displayMode)
{
    _displayMode = displayMode;
    _rotation = INVALID_ROTATION;
    _zoomVirtual = INVALID_ZOOM;
    _fullScreen = false;
    _startPage = INVALID_PAGE_NO;
    _appData = NULL;
    _pdfEngine = NULL;
    pagesInfo = NULL;

    _linkCount = 0;
    _links = NULL;

    searchHitPageNo = INVALID_PAGE_NO;
    searchState.searchState = eSsNone;
    searchState.str = new GooString();
    searchState.strU = new UGooString();
}

DisplayModel::~DisplayModel()
{
    delete _pdfEngine;
}

PdfPageInfo *DisplayModel::getPageInfo(int pageNo) const
{
    assert(validPageNo(pageNo));
    assert(pagesInfo);
    if (!pagesInfo) return NULL;
    return &(pagesInfo[pageNo-1]);
}

bool DisplayModel::load(const char *fileName, int startPage) 
{ 
    if (!_pdfEngine->load(fileName))
        return false;

    if (validPageNo(startPage))
        _startPage = startPage;
    else
        _startPage = 1;

    if (!buildPagesInfo())
        return false;
    return true;
}

bool DisplayModel::buildPagesInfo(void)
{
    assert(!pagesInfo);
    int _pageCount = pageCount();

    pagesInfo = (PdfPageInfo*)calloc(1, _pageCount * sizeof(PdfPageInfo));
    if (!pagesInfo)
        return false;

    for (int pageNo = 1; pageNo <= _pageCount; pageNo++) {
        PdfPageInfo *pageInfo = getPageInfo(pageNo);
        SizeD pageSize = pdfEngine()->pageSize(pageNo);
        pageInfo->pageDx = pageSize.dx;
        pageInfo->pageDy = pageSize.dy;
        pageInfo->rotation = pdfEngine()->pageRotation(pageNo);

        pageInfo->links = NULL;
        pageInfo->textPage = NULL;

        pageInfo->visible = false;
        pageInfo->shown = false;
        if (displayModeContinuous(_displayMode)) {
            pageInfo->shown = true;
        } else {
            if ((pageNo >= _startPage) && (pageNo < _startPage + columnsFromDisplayMode(_displayMode))) {
                DBG_OUT("DisplayModelSplash::CreateFromPdfDoc() set page %d as shown\n", pageNo);
                pageInfo->shown = true;
            }
        }
    }
    return true;
}

bool DisplayModel::pageShown(int pageNo)
{
    PdfPageInfo *pageInfo = getPageInfo(pageNo);
    if (!pageInfo)
        return false;
    return pageInfo->shown;
}

/* Given a zoom level that can include a "virtual" zoom levels like ZOOM_FIT_WIDTH
   and ZOOM_FIT_PAGE, calculate an absolute zoom level */
double DisplayModel::zoomRealFromFirtualForPage(double zoomVirtual, int pageNo)
{
    double          _zoomReal, zoomX, zoomY, pageDx, pageDy;
    double          areaForPageDx, areaForPageDy;
    int             areaForPageDxInt;
    int             columns;

    assert(0 != (int)drawAreaSize.dx);
    assert(0 != (int)drawAreaSize.dy);

    pageSizeAfterRotation(getPageInfo(pageNo), rotation(), &pageDx, &pageDy);

    assert(0 != (int)pageDx);
    assert(0 != (int)pageDy);

    columns = columnsFromDisplayMode(displayMode());
    areaForPageDx = (drawAreaSize.dx - PADDING_PAGE_BORDER_LEFT - PADDING_PAGE_BORDER_RIGHT);
    areaForPageDx -= (PADDING_BETWEEN_PAGES_X * (columns - 1));
    areaForPageDxInt = (int)(areaForPageDx / columns);
    areaForPageDx = (double)areaForPageDxInt;
    areaForPageDy = (double)drawAreaSize.dy - PADDING_PAGE_BORDER_TOP - PADDING_PAGE_BORDER_BOTTOM;
    if (ZOOM_FIT_WIDTH == zoomVirtual) {
        /* TODO: should use gWinDx if we don't show scrollbarY */
        _zoomReal = (areaForPageDx * 100.0) / (double)pageDx;
    } else if (ZOOM_FIT_PAGE == zoomVirtual) {
        zoomX = (areaForPageDx * 100.0) / (double)pageDx;
        zoomY = (areaForPageDy * 100.0) / (double)pageDy;
        if (zoomX < zoomY)
            _zoomReal = zoomX;
        else
            _zoomReal= zoomY;
    } else
        _zoomReal = zoomVirtual;

    assert(validZoomReal(_zoomReal));
    return _zoomReal;
}

int DisplayModel::firstVisiblePageNo(void) const
{
    assert(pagesInfo);
    if (!pagesInfo) return INVALID_PAGE_NO;

    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        PdfPageInfo *pageInfo = getPageInfo(pageNo);
        if (pageInfo->visible)
            return pageNo;
    }
    assert(0);
    return INVALID_PAGE_NO;
}

int DisplayModel::currentPageNo(void) const
{
    if (displayModeContinuous(displayMode()))
        return firstVisiblePageNo();
    else
        return _startPage;
}

void DisplayModel::setZoomVirtual(double zoomVirtual)
{
    int     pageNo;
    double  minZoom = INVALID_BIG_ZOOM;
    double  thisPageZoom;

    assert(ValidZoomVirtual(zoomVirtual));
    _zoomVirtual = zoomVirtual;

    if ((ZOOM_FIT_WIDTH == zoomVirtual) || (ZOOM_FIT_PAGE == zoomVirtual)) {
        /* we want the same zoom for all pages, so use the smallest zoom
           across the pages so that the largest page fits. In most PDFs all
           pages are the same size anyway */
        for (pageNo = 1; pageNo <= pageCount(); pageNo++) {
            if (pageShown(pageNo)) {
                thisPageZoom = zoomRealFromFirtualForPage(this->zoomVirtual(), pageNo);
                assert(0 != thisPageZoom);
                if (minZoom > thisPageZoom)
                    minZoom = thisPageZoom;
            }
        }
        assert(minZoom != INVALID_BIG_ZOOM);
        this->_zoomReal = minZoom;
    } else
        this->_zoomReal = zoomVirtual;
}

/* Given pdf info and zoom/rotation, calculate the position of each page on a
   large sheet that is continous view. Needs to be recalculated when:
     * zoom changes
     * rotation changes
     * switching between display modes
     * navigating to another page in non-continuous mode */
void DisplayModel::relayout(double zoomVirtual, int rotation)
{
    int         pageNo;
    PdfPageInfo*pageInfo = NULL;
    double      currPosX, currPosY;
    double      pageDx, pageDy;
    int         currDxInt, currDyInt;
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

    assert(pagesInfo);
    if (!pagesInfo)
        return;

    normalizeRotation(&rotation);
    assert(validRotation(rotation));

    if (_rotation != rotation)
        freeCache = TRUE;
    _rotation = rotation;

    currPosY = PADDING_PAGE_BORDER_TOP;
    currZoomReal = _zoomReal;
    setZoomVirtual(zoomVirtual);
    if (currZoomReal != _zoomReal)
        freeCache = TRUE;

//    DBG_OUT("DisplayModel::relayout(), pageCount=%d, zoomReal=%.6f, zoomVirtual=%.2f\n", pageCount, dm->zoomReal, dm->zoomVirtual);
    totalAreaDx = 0;

    if (0 == currZoomReal)
        newAreaOffsetX = 0.0;
    else
        newAreaOffsetX = areaOffset.x * _zoomReal / currZoomReal;
    areaOffset.x = newAreaOffsetX;
    /* calculate the position of each page on the canvas, given current zoom,
       rotation, columns parameters. You can think of it as a simple
       table layout i.e. rows with a fixed number of columns. */
    columns = columnsFromDisplayMode(displayMode());
    columnsLeft = columns;
    currPosX = PADDING_PAGE_BORDER_LEFT;
    rowMaxPageDy = 0;
    for (pageNo = 1; pageNo <= pageCount(); ++pageNo) {

        pageInfo = getPageInfo(pageNo);
        if (!pageInfo->shown) {
            assert(!pageInfo->visible);
            continue;
        }
        pageSizeAfterRotation(pageInfo, rotation, &pageDx, &pageDy);
        currDxInt = (int)(pageDx * _zoomReal * 0.01 + 0.5);
        currDyInt = (int)(pageDy * _zoomReal * 0.01 + 0.5);
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
    if (totalAreaDx < drawAreaSize.dx) {
        areaOffset.x = 0.0;
        offX = (drawAreaSize.dx - totalAreaDx) / 2.0 + PADDING_PAGE_BORDER_LEFT;
        assert(offX >= 0.0);
        areaPerPageDx = totalAreaDx - PADDING_PAGE_BORDER_LEFT - PADDING_PAGE_BORDER_RIGHT;
        areaPerPageDx = areaPerPageDx - (PADDING_BETWEEN_PAGES_X * (columns - 1));
        areaPerPageDxInt = (int)(areaPerPageDx / (double)columns);
        areaPerPageDx = (double)areaPerPageDxInt;
        totalAreaDx = drawAreaSize.dx;
        pageInARow = 0;
        for (pageNo = 1; pageNo <= pageCount(); ++pageNo) {
            pageInfo = getPageInfo(pageNo);
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
    if (drawAreaSize.dx - (totalAreaDx - newAreaOffsetX) > 0) {
        newAreaOffsetX = totalAreaDx - drawAreaSize.dx;
        areaOffset.x = newAreaOffsetX;
    }

    /* if a page is smaller than drawing area in y axis, y-center the page */
    totalAreaDy = currPosY + PADDING_PAGE_BORDER_BOTTOM - PADDING_BETWEEN_PAGES_Y;
    if (totalAreaDy < drawAreaSize.dy) {
        offY = PADDING_PAGE_BORDER_TOP + (drawAreaSize.dy - totalAreaDy) / 2;
        DBG_OUT("  offY = %.2f\n", offY);
        assert(offY >= 0.0);
        totalAreaDy = drawAreaSize.dy;
        for (pageNo = 1; pageNo <= pageCount(); ++pageNo) {
            pageInfo = getPageInfo(pageNo);
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

    _canvasSize.dx = totalAreaDx;
    _canvasSize.dy = totalAreaDy;

#if 0 // TODO: implement me
    if (freeCache) {
        /* bitmaps generated before are no longer valid if we changed rotation
           or zoom level */
        BitmapCache_FreeForDisplayModel(this);
    }
#endif
}

void DisplayModel::changeStartPage(int startPage)
{
    assert(validPageNo(startPage));
    assert(!displayModeContinuous(displayMode()));

    int columns = columnsFromDisplayMode(displayMode());
    _startPage = startPage;
    for (int pageNo = 1; pageNo <= pageCount(); pageNo++) {
        PdfPageInfo *pageInfo = getPageInfo(pageNo);
        if (displayModeContinuous(displayMode()))
            pageInfo->shown = true;
        else
            pageInfo->shown = false;
        if ((pageNo >= startPage) && (pageNo < startPage + columns)) {
            //DBG_OUT("DisplayModel::changeStartPage() set page %d as shown\n", pageNo);
            pageInfo->shown = true;
        }
        pageInfo->visible = false;
    }
    relayout(zoomVirtual(), rotation());
}

/* Given positions of each page in a large sheet that is continous view and
   coordinates of a current view into that large sheet, calculate which
   parts of each page is visible on the screen.
   Needs to be recalucated after scrolling the view. */
void DisplayModel::recalcVisibleParts(void)
{
    int             pageNo;
    RectI           drawAreaRect;
    RectI           pageRect;
    RectI           intersect;
    PdfPageInfo*    pageInfo;
    int             visibleCount;

    assert(pagesInfo);
    if (!pagesInfo)
        return;

    drawAreaRect.x = (int)areaOffset.x;
    drawAreaRect.y = (int)areaOffset.y;
    drawAreaRect.dx = (int)drawAreaSize.dx;
    drawAreaRect.dy = (int)drawAreaSize.dy;

//    DBG_OUT("DisplayModel::recalcVisibleParts() draw area         (x=%3d,y=%3d,dx=%4d,dy=%4d)\n",
//        drawAreaRect.x, drawAreaRect.y, drawAreaRect.dx, drawAreaRect.dy);
    visibleCount = 0;
    for (pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        pageInfo = getPageInfo(pageNo);
        if (!pageInfo->shown) {
            assert(!pageInfo->visible);
            continue;
        }
        pageRect.x = (int)pageInfo->currPosX;
        pageRect.y = (int)pageInfo->currPosY;
        pageRect.dx = (int)pageInfo->currDx;
        pageRect.dy = (int)pageInfo->currDy;
        pageInfo->visible = false;
        if (RectI_Intersect(&pageRect, &drawAreaRect, &intersect)) {
            pageInfo->visible = true;
            visibleCount += 1;
            pageInfo->bitmapX = (int) ((double)intersect.x - pageInfo->currPosX);
            assert(pageInfo->bitmapX >= 0);
            pageInfo->bitmapY = (int) ((double)intersect.y - pageInfo->currPosY);
            assert(pageInfo->bitmapY >= 0);
            pageInfo->bitmapDx = intersect.dx;
            pageInfo->bitmapDy = intersect.dy;
            pageInfo->screenX = (int) ((double)intersect.x - areaOffset.x);
            assert(pageInfo->screenX >= 0);
            assert(pageInfo->screenX <= drawAreaSize.dx);
            pageInfo->screenY = (int) ((double)intersect.y - areaOffset.y);
            assert(pageInfo->screenX >= 0);
            assert(pageInfo->screenY <= drawAreaSize.dy);
/*            DBG_OUT("                                  visible page = %d, (x=%3d,y=%3d,dx=%4d,dy=%4d) at (x=%d,y=%d)\n",
                pageNo, pageInfo->bitmapX, pageInfo->bitmapY,
                          pageInfo->bitmapDx, pageInfo->bitmapDy,
                          pageInfo->screenX, pageInfo->screenY); */
        }
    }

    assert(visibleCount > 0);
}

/* Map rectangle <r> on the page <pageNo> to point on the screen. */
void DisplayModel::rectCvtUserToScreen(int pageNo, RectD *r)
{
    double          sx, sy, ex, ey;

    sx = r->x;
    sy = r->y;
    ex = r->x + r->dx;
    ey = r->y + r->dy;

    CvtUserToScreen(pageNo, &sx, &sy);
    CvtUserToScreen(pageNo, &ex, &ey);
    RectD_FromXY(r, sx, ex, sy, ey);
}

void DisplayModel::recalcSearchHitCanvasPos(void)
{
    int             pageNo;
    RectD           rect;

    pageNo = searchHitPageNo;
    if (INVALID_PAGE_NO == pageNo) return;
    rect = searchHitRectPage;
    rectCvtUserToScreen(pageNo, &rect);
    searchHitRectCanvas.x = (int)rect.x;
    searchHitRectCanvas.y = (int)rect.y;
    searchHitRectCanvas.dx = (int)rect.dx;
    searchHitRectCanvas.dy = (int)rect.dy;
}

/* Recalculates the position of each link on the canvas i.e. applies current
   rotation and zoom level and offsets it by the offset of each page in
   the canvas.
   TODO: applying rotation and zoom level could be split into a separate
         function for speedup, since it only has to change after rotation/zoomLevel
         changes while this function has to be called after each scrolling.
         But I'm not sure if this would be a significant speedup */
void DisplayModel::recalcLinksCanvasPos(void)
{
    PdfLink *       pdfLink;
    PdfPageInfo *   pageInfo;
    int             linkNo;
    RectD           rect;

    // TODO: calling it here is a bit of a hack
    recalcSearchHitCanvasPos();

    DBG_OUT("DisplayModel::recalcLinksCanvasPos() linkCount=%d\n", _linkCount);

    if (0 == _linkCount)
        return;
    assert(_links);
    if (!_links)
        return;

    for (linkNo = 0; linkNo < _linkCount; linkNo++) {
        pdfLink = link(linkNo);
        pageInfo = getPageInfo(pdfLink->pageNo);
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
        rectCvtUserToScreen(pdfLink->pageNo, &rect);
        pdfLink->rectCanvas.x = (int)rect.x;
        pdfLink->rectCanvas.y = (int)rect.y;
        pdfLink->rectCanvas.dx = (int)rect.dx;
        pdfLink->rectCanvas.dy = (int)rect.dy;
#if 0
        DBG_OUT("  link on page (x=%d, y=%d, dx=%d, dy=%d),\n",
            (int)pdfLink->rectPage.x, (int)pdfLink->rectPage.y,
            (int)pdfLink->rectPage.dx, (int)pdfLink->rectPage.dy);
        DBG_OUT("        screen (x=%d, y=%d, dx=%d, dy=%d)\n",
                (int)rect.x, (int)rect.y,
                (int)rect.dx, (int)rect.dy);
#endif
    }
}

void DisplayModel::clearSearchHit(void)
{
    DBG_OUT("DisplayModel::clearSearchHit()\n");
    searchHitPageNo = INVALID_PAGE_NO;
}

void DisplayModel::setSearchHit(int pageNo, RectD *hitRect)
{
    //DBG_OUT("DisplayModel::setSearchHit() page=%d at pos (%.2f, %.2f)-(%.2f,%.2f)\n", pageNo, xs, ys, xe, ye);
    searchHitPageNo = pageNo;
    searchHitRectPage = *hitRect;
    recalcSearchHitCanvasPos();
}

/* Given position 'x'/'y' in the draw area, returns a structure describing
   a link or NULL if there is no link at this position.
   Note: DisplayModelSplash owns this memory so it should not be changed by the
   caller and caller should not reference it after it has changed (i.e. process
   it immediately since it will become invalid after each _relayout()).
   TODO: this function is called frequently from UI code so make sure that
         it's fast enough for a decent number of link.
         Possible speed improvement: remember which links are visible after
         scrolling and skip the _Inside test for those invisible.
         Another way: build another list with only those visible, so we don't
         even have to travers those that are invisible.
   */
PdfLink *DisplayModel::linkAtPosition(int x, int y)
{
    if (0 == _linkCount) return NULL;
    assert(_links);
    if (!_links) return NULL;

    int canvasPosX = x + (int)areaOffset.x;
    int canvasPosY = y + (int)areaOffset.y;
    for (int i = 0; i < _linkCount; i++) {
        PdfLink *currLink = link(i);

        if (RectI_Inside(&(currLink->rectCanvas), canvasPosX, canvasPosY))
            return currLink;
    }
    return NULL;
}