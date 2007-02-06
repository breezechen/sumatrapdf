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

bool ValidZoomReal(double zoomReal)
{
    if ((zoomReal < ZOOM_MIN) || (zoomReal > ZOOM_MAX)) {
        DBG_OUT("ValidZoomReal() invalid zoom: %.4f\n", zoomReal);
        return false;
    }
    return true;
}

bool IsDisplayModeContinuous(DisplayMode displayMode)
{
    if ((DM_SINGLE_PAGE == displayMode) || (DM_FACING == displayMode))
        return false;
    else if ((DM_CONTINUOUS == displayMode) || (DM_CONTINUOUS_FACING == displayMode))
        return true;
    assert(0);
    return false;
}

int ColumnsFromDisplayMode(DisplayMode displayMode)
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

DisplaySettings *GetGlobalDisplaySettings(void)
{
    return &gDisplaySettings;
}

int FlippedRotation(int rotation)
{
    assert(ValidRotation(rotation));
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
    if (IsDisplayModeContinuous(dm->displayMode())) {
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
void PageSizeAfterRotation(PdfPageInfo *pageInfo, int rotation,
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

bool DisplayModel::buildPagesInfo()
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
        if (IsDisplayModeContinuous(_displayMode)) {
            pageInfo->shown = true;
        } else {
            if ((pageNo >= _startPage) && (pageNo < _startPage + ColumnsFromDisplayMode(_displayMode))) {
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

    PageSizeAfterRotation(getPageInfo(pageNo), rotation(), &pageDx, &pageDy);

    assert(0 != (int)pageDx);
    assert(0 != (int)pageDy);

    columns = ColumnsFromDisplayMode(displayMode());
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

    assert(ValidZoomReal(_zoomReal));
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
    if (IsDisplayModeContinuous(displayMode()))
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
void DisplayModel::Relayout(double zoomVirtual, int rotation)
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

    NormalizeRotation(&rotation);
    assert(ValidRotation(rotation));

    if (_rotation != rotation)
        freeCache = TRUE;
    _rotation = rotation;

    currPosY = PADDING_PAGE_BORDER_TOP;
    currZoomReal = _zoomReal;
    setZoomVirtual(zoomVirtual);
    if (currZoomReal != _zoomReal)
        freeCache = TRUE;

//    DBG_OUT("DisplayModel::Relayout(), pageCount=%d, zoomReal=%.6f, zoomVirtual=%.2f\n", pageCount, dm->zoomReal, dm->zoomVirtual);
    totalAreaDx = 0;

    if (0 == currZoomReal)
        newAreaOffsetX = 0.0;
    else
        newAreaOffsetX = areaOffset.x * _zoomReal / currZoomReal;
    areaOffset.x = newAreaOffsetX;
    /* calculate the position of each page on the canvas, given current zoom,
       rotation, columns parameters. You can think of it as a simple
       table layout i.e. rows with a fixed number of columns. */
    columns = ColumnsFromDisplayMode(displayMode());
    columnsLeft = columns;
    currPosX = PADDING_PAGE_BORDER_LEFT;
    rowMaxPageDy = 0;
    for (pageNo = 1; pageNo <= pageCount(); ++pageNo) {

        pageInfo = getPageInfo(pageNo);
        if (!pageInfo->shown) {
            assert(!pageInfo->visible);
            continue;
        }
        PageSizeAfterRotation(pageInfo, rotation, &pageDx, &pageDy);
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


