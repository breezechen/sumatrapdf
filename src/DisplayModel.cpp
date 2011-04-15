/* Copyright 2006-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

/* How to think of display logic: physical screen of size
   viewPortSize is a window into (possibly much larger)
   total area (canvas) of size canvasSize.

   In DM_SINGLE_PAGE mode total area is the size of currently displayed page
   given current zoom level and rotation.
   In DM_CONTINUOUS mode canvas area consist of all pages rendered sequentially
   with a given zoom level and rotation. canvasSize.dy is the sum of heights
   of all pages plus spaces between them and canvasSize.dx is the size of
   the widest page.

   A possible configuration could look like this:

 -----------------------------------
 |                                 |
 |          -------------          |
 |          | window    |          |
 |          | i.e.      |          |
 |          | view port |          |
 |          -------------          |
 |                                 |
 |                                 |
 |    canvas                       |
 |                                 |
 |                                 |
 |                                 |
 |                                 |
 -----------------------------------

  We calculate the canvas size and position of each page we display on the
  canvas.

  Changing zoom level or rotation requires recalculation of canvas size and
  position of pages in it.

  We keep the offset of view port relative to canvas. The offset changes
  due to scrolling (with keys or using scrollbars).

  To draw we calculate which part of each page overlaps draw area, we render
  those pages to a bitmap and display those bitmaps.
*/

#include "DisplayModel.h"

// define the following if you want the pages right before and after
// the visible ones to be pre-rendered
#define PREDICTIVE_RENDER 1

ScreenPagePadding gPagePadding = {
    PADDING_PAGE_BORDER_TOP_DEF, PADDING_PAGE_BORDER_LEFT_DEF,
    PADDING_PAGE_BORDER_BOTTOM_DEF, PADDING_PAGE_BORDER_RIGHT_DEF,
    PADDING_BETWEEN_PAGES_X_DEF, PADDING_BETWEEN_PAGES_Y_DEF
}, gPagePaddingPresentation = {
    0, 0,
    0, 0,
    PADDING_BETWEEN_PAGES_X_DEF, PADDING_BETWEEN_PAGES_X_DEF
}, gImagePadding = {
    0, 0,
    0, 0,
    PADDING_BETWEEN_PAGES_X_DEF, PADDING_BETWEEN_PAGES_X_DEF
};

bool displayModeFacing(DisplayMode displayMode)
{
    return DM_FACING == displayMode ||
           DM_CONTINUOUS_FACING == displayMode ||
           displayModeShowCover(displayMode);
}

bool displayModeContinuous(DisplayMode displayMode)
{
    return DM_CONTINUOUS == displayMode ||
           DM_CONTINUOUS_FACING == displayMode ||
           DM_CONTINUOUS_BOOK_VIEW == displayMode;
}

bool displayModeShowCover(DisplayMode displayMode)
{
    return DM_BOOK_VIEW == displayMode ||
           DM_CONTINUOUS_BOOK_VIEW == displayMode;
}

int columnsFromDisplayMode(DisplayMode displayMode)
{
    if (displayModeFacing(displayMode))
        return 2;
    return 1;
}

bool DisplayModel::displayStateFromModel(DisplayState *ds)
{
    if (!ds->filePath || !Str::Eq(ds->filePath, fileName())) {
        TCHAR *filePath = Str::Dup(fileName());
        if (!filePath)
            return false;

        free(ds->filePath);
        ds->filePath = filePath;
    }

    ds->displayMode = _presentationMode ? _presDisplayMode : displayMode();
    ds->rotation = _rotation;
    ds->zoomVirtual = _presentationMode ? _presZoomVirtual : _zoomVirtual;

    ScrollState ss = GetScrollState();
    ds->pageNo = ss.page;
    if (_presentationMode)
        ds->scrollPos = PointI();
    else
        ds->scrollPos = PointD(ss.x, ss.y).Convert<int>();

    free(ds->decryptionKey);
    ds->decryptionKey = pdfEngine ? pdfEngine->GetDecryptionKey() : NULL;

    return true;
}

SizeD DisplayModel::PageSizeAfterRotation(int pageNo, bool fitToContent)
{
    PageInfo *pageInfo = getPageInfo(pageNo);
    if (fitToContent && pageInfo->contentBox.IsEmpty()) {
        pageInfo->contentBox = engine->PageContentBox(pageNo);
        if (pageInfo->contentBox.IsEmpty())
            return PageSizeAfterRotation(pageNo);
    }

    RectD box;
    if (fitToContent)
        box = pageInfo->contentBox.Convert<double>();
    else
        box = RectD(PointD(), pageInfo->page);

    return engine->Transform(box, pageNo, 1.0, _rotation).Size();
}

/* given 'columns' and an absolute 'pageNo', return the number of the first
   page in a row to which a 'pageNo' belongs e.g. if 'columns' is 2 and we
   have 5 pages in 3 rows (depending on showCover):

   Pages   Result           Pages   Result
   (1,2)   1                  (1)   1
   (3,4)   3                (2,3)   2
   (5)     5                (4,5)   4
 */
// TODO: should it take _displayRL into account?
static int FirstPageInARowNo(int pageNo, int columns, bool showCover)
{
    if (showCover && columns > 1)
        pageNo++;
    int firstPageNo = pageNo - ((pageNo - 1) % columns);
    if (showCover && columns > 1 && firstPageNo > 1)
        firstPageNo--;
    return firstPageNo;
}

static int LastPageInARowNo(int pageNo, int columns, bool showCover, int pageCount)
{
    int lastPageNo = FirstPageInARowNo(pageNo, columns, showCover) + columns - 1;
    if (showCover && pageNo < columns)
        lastPageNo--;
    return min(lastPageNo, pageCount);
}

DisplayModel::DisplayModel(DisplayModelCallback *callback, DisplayMode displayMode)
{
    _displayMode = displayMode;
    _presDisplayMode = displayMode;
    _presZoomVirtual = INVALID_ZOOM;
    _rotation = INVALID_ROTATION;
    _zoomVirtual = INVALID_ZOOM;
    padding = &gPagePadding;
    _presentationMode = false;
    _zoomReal = INVALID_ZOOM;
    _dpiFactor = 1.0f;
    _startPage = INVALID_PAGE_NO;
    _callback = callback;

    engine = NULL;
    pdfEngine = NULL;
    xpsEngine = NULL;
    cbxEngine = NULL;
    imageEngine = NULL;

    textSelection = NULL;
    textSearch = NULL;
    _pagesInfo = NULL;

    _navHistoryIx = 0;
    _navHistoryEnd = 0;
    
    _dontRenderFlag = false;
}

DisplayModel::~DisplayModel()
{
    _dontRenderFlag = true;
    _callback->CleanUp(this);

    free(_pagesInfo);
    delete textSearch;
    delete textSelection;
    delete engine;
}

PageInfo *DisplayModel::getPageInfo(int pageNo) const
{
    if (!validPageNo(pageNo))
        return NULL;
    assert(validPageNo(pageNo));
    assert(_pagesInfo);
    if (!_pagesInfo) return NULL;
    return &(_pagesInfo[pageNo-1]);
}

bool DisplayModel::load(const TCHAR *fileName, int startPage, SizeI viewPort)
{ 
    assert(fileName);
    if (Str::EndsWithI(fileName, _T(".pdf")))
        engine = pdfEngine = PdfEngine::CreateFromFileName(fileName, _callback);
    else if (Str::EndsWithI(fileName, _T(".xps")))
        engine = xpsEngine = XpsEngine::CreateFromFileName(fileName);
    else if (CbxEngine::IsSupportedFile(fileName))
        engine = cbxEngine = CbxEngine::CreateFromFileName(fileName);
    else if (ImageEngine::IsSupportedFile(fileName))
        engine = imageEngine = ImageEngine::CreateFromFileName(fileName);
    else {
        // try loading as either supported file format
        // TODO: sniff the file content instead
        engine = pdfEngine = PdfEngine::CreateFromFileName(fileName, _callback);
        if (!engine)
            engine = xpsEngine = XpsEngine::CreateFromFileName(fileName);
    }
    if (!engine)
        return false;

    if (cbxEngine || imageEngine)
        padding = &gImagePadding;

    _dpiFactor = 1.0f * _callback->GetScreenDPI() / engine->GetFileDPI();
    totalViewPortSize = viewPort;

    if (validPageNo(startPage))
        _startPage = startPage;
    else
        _startPage = 1;

    PageLayoutType layout = engine->PreferredLayout();
    if (DM_AUTOMATIC == _displayMode) {
        switch (layout & ~Layout_R2L) {
        case Layout_Single: _displayMode = DM_CONTINUOUS; break;
        case Layout_Facing: _displayMode = DM_CONTINUOUS_FACING; break;
        case Layout_Book: _displayMode = DM_CONTINUOUS_BOOK_VIEW; break;
        case Layout_Single | Layout_NonContinuous: _displayMode = DM_SINGLE_PAGE; break;
        case Layout_Facing | Layout_NonContinuous: _displayMode = DM_FACING; break;
        case Layout_Book | Layout_NonContinuous: _displayMode = DM_BOOK_VIEW; break;
        }
    }
    _displayR2L = (layout & Layout_R2L) != 0;

    if (!buildPagesInfo())
        return false;

    textSelection = new TextSelection(engine);
    textSearch = new TextSearch(engine, _callback);
    return true;
}

bool DisplayModel::buildPagesInfo()
{
    assert(!_pagesInfo);
    int _pageCount = pageCount();

    _pagesInfo = SAZA(PageInfo, _pageCount);
    if (!_pagesInfo)
        return false;

    int columns = columnsFromDisplayMode(_displayMode);
    int startPage = _startPage;
    if (displayModeShowCover(_displayMode) && startPage == 1 && columns > 1)
        startPage--;
    for (int pageNo = 1; pageNo <= _pageCount; pageNo++) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        pageInfo->page = engine->PageMediabox(pageNo).Size();
        pageInfo->rotation = engine->PageRotation(pageNo);
        pageInfo->visibleRatio = 0.0;
        pageInfo->shown = false;
        if (displayModeContinuous(_displayMode)) {
            pageInfo->shown = true;
        } else if (pageNo >= startPage && pageNo < startPage + columns) {
            pageInfo->shown = true;
        }
    }
    return true;
}


// TODO: a better name e.g. ShouldShow() to better distinguish between
// before-layout info and after-layout visibility checks
bool DisplayModel::pageShown(int pageNo)
{
    PageInfo *pageInfo = getPageInfo(pageNo);
    if (!pageInfo)
        return false;
    return pageInfo->shown;
}

bool DisplayModel::pageVisible(int pageNo)
{
    PageInfo *pageInfo = getPageInfo(pageNo);
    if (!pageInfo)
        return false;
    return pageInfo->visibleRatio > 0.0;
}

/* Return true if a page is visible or a page in a row below or above is visible */
bool DisplayModel::pageVisibleNearby(int pageNo)
{
    DisplayMode mode = displayMode();
    int columns = columnsFromDisplayMode(mode);

    pageNo = FirstPageInARowNo(pageNo, columns, displayModeShowCover(mode));
    for (int i = pageNo - columns; i < pageNo + 2 * columns; i++) {
        if (validPageNo(i) && pageVisible(i))
            return true;
    }
    return false;
}

/* Return true if the first page is fully visible and alone on a line in
   show cover mode (i.e. it's not possible to flip to a previous page) */
bool DisplayModel::firstBookPageVisible()
{
    if (!displayModeShowCover(displayMode()))
        return false;
    if (currentPageNo() != 1)
        return false;
    return true;
}

/* Return true if the last page is fully visible and alone on a line in
   facing or show cover mode (i.e. it's not possible to flip to a next page) */
bool DisplayModel::lastBookPageVisible()
{
    int count = pageCount();
    DisplayMode mode = displayMode();
    if (!displayModeFacing(mode))
        return false;
    if (currentPageNo() == count)
        return true;
    if (getPageInfo(count)->visibleRatio < 1.0)
        return false;
    if (FirstPageInARowNo(count, columnsFromDisplayMode(mode),
                          displayModeShowCover(mode)) < count)
        return false;
    return true;
}

/* Given a zoom level that can include a "virtual" zoom levels like ZOOM_FIT_WIDTH,
   ZOOM_FIT_PAGE or ZOOM_FIT_CONTENT, calculate an absolute zoom level */
float DisplayModel::zoomRealFromVirtualForPage(float zoomVirtual, int pageNo)
{
    if (zoomVirtual != ZOOM_FIT_WIDTH && zoomVirtual != ZOOM_FIT_PAGE && zoomVirtual != ZOOM_FIT_CONTENT)
        return zoomVirtual * 0.01f * this->_dpiFactor;

    SizeD row;
    PageInfo *pageInfo = getPageInfo(pageNo);
    int columns = columnsFromDisplayMode(displayMode());

    bool fitToContent = (ZOOM_FIT_CONTENT == zoomVirtual);
    if (fitToContent && columns > 1) {
        // Fit the content of all the pages in the same row into the visible area
        // (i.e. don't crop inner margins but just the left-most, right-most, etc.)
        int first = FirstPageInARowNo(pageNo, columns, displayModeShowCover(displayMode()));
        int last = LastPageInARowNo(pageNo, columns, displayModeShowCover(displayMode()), pageCount());
        for (int i = first; i <= last; i++) {
            SizeD pageSize = PageSizeAfterRotation(i);
            row.dx += pageSize.dx;

            pageInfo = getPageInfo(i);
            if (pageInfo->contentBox.IsEmpty())
                pageInfo->contentBox = engine->PageContentBox(i);
            if (i == first && !pageInfo->contentBox.IsEmpty()) {
                if ((pageInfo->rotation + _rotation) % 180 != 0)
                    row.dx -= pageInfo->contentBox.y - engine->PageMediabox(first).y;
                else
                    row.dx -= pageInfo->contentBox.x - engine->PageMediabox(first).x;
            }
            if (i == last && !pageInfo->contentBox.IsEmpty()) {
                if ((pageInfo->rotation + _rotation) % 180 != 0)
                    row.dx -= engine->PageMediabox(last).BR().y - pageInfo->contentBox.BR().y;
                else
                    row.dx -= engine->PageMediabox(last).BR().x - pageInfo->contentBox.BR().x;
            }

            pageSize = PageSizeAfterRotation(i, true);
            if (row.dy < pageSize.dy)
                row.dy = pageSize.dy;
        }
    } else {
        row = PageSizeAfterRotation(pageNo, fitToContent);
        row.dx *= columns;
    }

    assert(!RectD(PointD(), row).IsEmpty());

    int areaForPagesDx = viewPortSize.dx - padding->left - padding->right - padding->inBetweenX * (columns - 1);
    int areaForPagesDy = viewPortSize.dy - padding->top - padding->bottom;
    if (areaForPagesDx <= 0 || areaForPagesDy <= 0)
        return 0;

    float zoomX = areaForPagesDx / (float)row.dx;
    float zoomY = areaForPagesDy / (float)row.dy;
    if (zoomX < zoomY || ZOOM_FIT_WIDTH == zoomVirtual)
        return zoomX;
    return zoomY;
}

int DisplayModel::firstVisiblePageNo() const
{
    assert(_pagesInfo);
    if (!_pagesInfo) return INVALID_PAGE_NO;

    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        if (pageInfo->visibleRatio > 0.0)
            return pageNo;
    }
    
    /* If no pages are visible */
    return INVALID_PAGE_NO;
}

// TODO: this is really MostVisiblePage()
int DisplayModel::currentPageNo() const
{
    if (!displayModeContinuous(displayMode()))
        return _startPage;

    assert(_pagesInfo);
    if (!_pagesInfo) return INVALID_PAGE_NO;
    // determine the most visible page
    int mostVisiblePage = INVALID_PAGE_NO;
    float ratio = 0;

    for (int pageNo = 1; pageNo <= pageCount(); pageNo++) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        if (pageInfo->visibleRatio > ratio) {
            mostVisiblePage = pageNo;
            ratio = pageInfo->visibleRatio;
        }
    }

    /* if no page is visible, default to either the first or the last one */
    if (INVALID_PAGE_NO == mostVisiblePage) {
        PageInfo *pageInfo = getPageInfo(1);
        if (viewPortOffset.y > pageInfo->pos.y + pageInfo->pos.dy)
            mostVisiblePage = pageCount();
        else
            mostVisiblePage = 1;
    }

    return mostVisiblePage;
}

void DisplayModel::setZoomVirtual(float zoomVirtual)
{
    assert(ValidZoomVirtual(zoomVirtual));
    _zoomVirtual = zoomVirtual;

    if ((ZOOM_FIT_WIDTH == zoomVirtual) || (ZOOM_FIT_PAGE == zoomVirtual)) {
        /* we want the same zoom for all pages, so use the smallest zoom
           across the pages so that the largest page fits. In most documents
           all pages are the same size anyway */
        float minZoom = (float)HUGE_VAL;
        for (int pageNo = 1; pageNo <= pageCount(); pageNo++) {
            if (pageShown(pageNo)) {
                float thisPageZoom = zoomRealFromVirtualForPage(zoomVirtual, pageNo);
                if (minZoom > thisPageZoom)
                    minZoom = thisPageZoom;
            }
        }
        assert(minZoom != (float)HUGE_VAL);
        this->_zoomReal = minZoom;
    } else if (ZOOM_FIT_CONTENT == zoomVirtual) {
        float newZoom = zoomRealFromVirtualForPage(zoomVirtual, currentPageNo());
        // limit zooming in to 800% on almost empty pages
        if (newZoom > 8.0)
            newZoom = 8.0;
        // don't zoom in by just a few pixels (throwing away a prerendered page)
        if (newZoom < this->_zoomReal || this->_zoomReal / newZoom < 0.95 ||
            this->_zoomReal < zoomRealFromVirtualForPage(ZOOM_FIT_PAGE, currentPageNo()))
            this->_zoomReal = newZoom;
    } else
        this->_zoomReal = zoomVirtual * 0.01f * this->_dpiFactor;
}

float DisplayModel::zoomReal(int pageNo)
{
    DisplayMode mode = displayMode();
    if (displayModeContinuous(mode))
        return _zoomReal;
    if (!displayModeFacing(mode))
        return zoomRealFromVirtualForPage(_zoomVirtual, pageNo);
    pageNo = FirstPageInARowNo(pageNo, columnsFromDisplayMode(mode), displayModeShowCover(mode));
    if (pageNo == pageCount() || pageNo == 1 && displayModeShowCover(mode))
        return zoomRealFromVirtualForPage(_zoomVirtual, pageNo);
    return min(zoomRealFromVirtualForPage(_zoomVirtual, pageNo), zoomRealFromVirtualForPage(_zoomVirtual, pageNo + 1));
}

/* Given zoom and rotation, calculate the position of each page on a
   large sheet that is continous view. Needs to be recalculated when:
     * zoom changes
     * rotation changes
     * switching between display modes
     * navigating to another page in non-continuous mode */
void DisplayModel::Relayout(float zoomVirtual, int rotation)
{
    assert(_pagesInfo);
    if (!_pagesInfo)
        return;

    normalizeRotation(&rotation);
    assert(validRotation(rotation));
    _rotation = rotation;

    bool needHScroll = false;
    bool needVScroll = false;
    viewPortSize = totalViewPortSize;

RestartLayout:
    int currPosY = padding->top;
    float currZoomReal = _zoomReal;
    setZoomVirtual(zoomVirtual);

//    DBG_OUT("DisplayModel::Relayout(), pageCount=%d, zoomReal=%.6f, zoomVirtual=%.2f\n", pageCount, dm->zoomReal, dm->zoomVirtual);

    int newViewPortOffsetX = 0;
    if (0 != currZoomReal && INVALID_ZOOM != currZoomReal)
        newViewPortOffsetX = (int)(viewPortOffset.x * _zoomReal / currZoomReal);
    viewPortOffset.x = newViewPortOffsetX;
    /* calculate the position of each page on the canvas, given current zoom,
       rotation, columns parameters. You can think of it as a simple
       table layout i.e. rows with a fixed number of columns. */
    int columns = columnsFromDisplayMode(displayMode());
    int columnMaxWidth[2] = { 0, 0 };
    int pageInARow = 0;
    int rowMaxPageDy = 0;
    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        if (!pageInfo->shown) {
            assert(0.0 == pageInfo->visibleRatio);
            continue;
        }
        SizeD pageSize = PageSizeAfterRotation(pageNo);
        RectI pos;
        pos.dx = (int)(pageSize.dx * _zoomReal + 0.5);
        pos.dy = (int)(pageSize.dy * _zoomReal + 0.5);

        if (rowMaxPageDy < pos.dy)
            rowMaxPageDy = pos.dy;
        pos.y = currPosY;

        // restart the layout if we detect we need to show scrollbars
        if (!needVScroll && viewPortSize.dy < currPosY + rowMaxPageDy) {
            needVScroll = true;
            viewPortSize.dx -= GetSystemMetrics(SM_CXVSCROLL);
            goto RestartLayout;
        }

        if (displayModeShowCover(displayMode()) && pageNo == 1 && columns - pageInARow > 1)
            pageInARow++;
        if (columnMaxWidth[pageInARow] < pos.dx)
            columnMaxWidth[pageInARow] = pos.dx;

        if (!needHScroll && viewPortSize.dx < padding->left + columnMaxWidth[0] + (columns == 2 ? padding->inBetweenX + columnMaxWidth[1] : 0) + padding->right) {
            needHScroll = true;
            viewPortSize.dy -= GetSystemMetrics(SM_CYHSCROLL);
            goto RestartLayout;
        }

        pageInfo->pos = pos;

        pageInARow++;
        assert(pageInARow <= columns);
        if (pageInARow == columns) {
            /* starting next row */
            currPosY += rowMaxPageDy + padding->inBetweenY;
            rowMaxPageDy = 0;
            pageInARow = 0;
        }
/*        DBG_OUT("  page = %3d, (x=%3d, y=%5d, dx=%4d, dy=%4d) orig=(dx=%d,dy=%d)\n",
            pageNo, pageInfo->currPos.x, pageInfo->currPos.y,
                    pageInfo->currPos.dx, pageInfo->currPos.dy,
                    (int)pageSize.dx, (int)pageSize.dy); */
    }

    if (pageInARow != 0) {
        /* this is a partial row */
        currPosY += rowMaxPageDy + padding->inBetweenY;
    }
    // restart the layout if we detect we need to show scrollbars
    // (there are some edge cases we can't catch in the above loop)
    const int canvasDy = currPosY + padding->bottom - padding->inBetweenY;
    if (!needVScroll && canvasDy > viewPortSize.dy) {
        needVScroll = true;
        viewPortSize.dx -= GetSystemMetrics(SM_CXVSCROLL);
        goto RestartLayout;
    }

    if (columns == 2 && pageCount() == 1) {
        /* don't center a single page over two columns */
        if (displayModeShowCover(displayMode()))
            columnMaxWidth[0] = columnMaxWidth[1];
        else
            columnMaxWidth[1] = columnMaxWidth[0];
    }

    // restart the layout if we detect we need to show scrollbars
    // (there are some edge cases we can't catch in the above loop)
    int canvasDx = padding->left + columnMaxWidth[0] + (columns == 2 ? padding->inBetweenX + columnMaxWidth[1] : 0) + padding->right;
    if (!needHScroll && canvasDx > viewPortSize.dx) {
        needHScroll = true;
        viewPortSize.dy -= GetSystemMetrics(SM_CYHSCROLL);
        goto RestartLayout;
    }

    /* since pages can be smaller than the drawing area, center them in x axis */
    int offX = 0;
    if (canvasDx < viewPortSize.dx) {
        viewPortOffset.x = 0;
        offX = (viewPortSize.dx - canvasDx) / 2;
        canvasDx = viewPortSize.dx;
    }

    assert(offX >= 0);
    pageInARow = 0;
    int pageOffX = offX + padding->left;
    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        if (!pageInfo->shown) {
            assert(0.0 == pageInfo->visibleRatio);
            continue;
        }
        // leave first spot empty in cover page mode
        if (displayModeShowCover(displayMode()) && pageNo == 1)
            pageOffX += columnMaxWidth[pageInARow++] + padding->inBetweenX;
        pageInfo->pos.x = pageOffX + (columnMaxWidth[pageInARow] - pageInfo->pos.dx) / 2;
        // center the cover page over the first two spots in non-continuous mode
        if (displayModeShowCover(displayMode()) && pageNo == 1 && !displayModeContinuous(displayMode()))
            pageInfo->pos.x = offX + padding->left + (columnMaxWidth[0] + padding->inBetweenX + columnMaxWidth[1] - pageInfo->pos.dx) / 2;
        // mirror the page layout when displaying a Right-to-Left document
        if (_displayR2L && columns > 1)
            pageInfo->pos.x = canvasDx - pageInfo->pos.x - pageInfo->pos.dx;
        pageOffX += columnMaxWidth[pageInARow++] + padding->inBetweenX;
        assert(pageOffX >= 0 && pageInfo->pos.x >= 0);

        if (pageInARow == columns) {
            pageOffX = offX + padding->left;
            pageInARow = 0;
        }
    }

    /* if after resizing we would have blank space on the right due to x offset
       being too much, make x offset smaller so that there's no blank space */
    if (viewPortSize.dx - (canvasDx - newViewPortOffsetX) > 0) {
        newViewPortOffsetX = canvasDx - viewPortSize.dx;
        viewPortOffset.x = newViewPortOffsetX;
    }

    /* if a page is smaller than drawing area in y axis, y-center the page */
    if (canvasDy < viewPortSize.dy) {
        int offY = padding->top + (viewPortSize.dy - canvasDy) / 2;
        DBG_OUT("  offY = %.2f\n", offY);
        assert(offY >= 0.0);
        for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
            PageInfo *pageInfo = getPageInfo(pageNo);
            if (!pageInfo->shown) {
                assert(0.0 == pageInfo->visibleRatio);
                continue;
            }
            pageInfo->pos.y += offY;
            DBG_OUT("  page = %3d, (x=%3d, y=%5d, dx=%4d, dy=%4d)\n",
                pageNo, pageInfo->pos.x, pageInfo->pos.y,
                        pageInfo->pos.dx, pageInfo->pos.dy);
        }
    }

    canvasSize = SizeI(max(canvasDx, viewPortSize.dx), max(canvasDy, viewPortSize.dy));
}

void DisplayModel::changeStartPage(int startPage)
{
    assert(validPageNo(startPage));
    assert(!displayModeContinuous(displayMode()));

    int columns = columnsFromDisplayMode(displayMode());
    _startPage = startPage;
    if (displayModeShowCover(displayMode()) && startPage == 1 && columns > 1)
        startPage--;
    for (int pageNo = 1; pageNo <= pageCount(); pageNo++) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        if (displayModeContinuous(displayMode()))
            pageInfo->shown = true;
        else if (pageNo >= startPage && pageNo < startPage + columns) {
            //DBG_OUT("DisplayModel::changeStartPage() set page %d as shown\n", pageNo);
            pageInfo->shown = true;
        } else
            pageInfo->shown = false;
        pageInfo->visibleRatio = 0.0;
    }
    Relayout(_zoomVirtual, _rotation);
}

/* Given positions of each page in a large sheet that is continous view and
   coordinates of a current view into that large sheet, calculate which
   parts of each page is visible on the screen.
   Needs to be recalucated after scrolling the view. */
void DisplayModel::RecalcVisibleParts()
{
    assert(_pagesInfo);
    if (!_pagesInfo)
        return;

    RectI viewPortRect(viewPortOffset, viewPortSize);

//    DBG_OUT("DisplayModel::RecalcVisibleParts() draw area         (x=%3d,y=%3d,dx=%4d,dy=%4d)\n",
//        viewPortRect.x, viewPortRect.y, viewPortRect.dx, viewPortRect.dy);
    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        if (!pageInfo->shown) {
            assert(0.0 == pageInfo->visibleRatio);
            continue;
        }

        RectI pageRect = pageInfo->pos;
        pageInfo->visibleRatio = 0.0;

        RectI visiblePart = pageRect.Intersect(viewPortRect);
        if (pageRect.dx > 0 && pageRect.dy > 0 && !visiblePart.IsEmpty()) {
            // calculate with floating point precision to prevent an integer overflow
            pageInfo->visibleRatio = 1.0f * visiblePart.dx * visiblePart.dy / ((float)pageRect.dx * pageRect.dy);

            pageInfo->bitmap = visiblePart;
            pageInfo->bitmap.x -= pageRect.x;
            assert(pageInfo->bitmap.x >= 0);
            pageInfo->bitmap.y -= pageRect.y;
            assert(pageInfo->bitmap.y >= 0);
            pageInfo->screen.x = visiblePart.x - viewPortOffset.x;
            assert(pageInfo->screen.x >= 0);
            assert(pageInfo->screen.x <= viewPortSize.dx);
            pageInfo->screen.y = visiblePart.y - viewPortOffset.y;
            assert(pageInfo->screen.y >= 0);
            assert(pageInfo->screen.y <= viewPortSize.dy);
/*          DBG_OUT("                                  visible page = %d, (x=%3d,y=%3d,dx=%4d,dy=%4d) at (x=%d,y=%d)\n",
                pageNo, pageInfo->bitmap.x, pageInfo->bitmap.y,
                          pageInfo->bitmap.dx, pageInfo->bitmap.dy,
                          pageInfo->screen.x, pageInfo->screen.y); */
        }

        pageInfo->pageOnScreen = pageRect;
        pageInfo->pageOnScreen.x = pageInfo->pageOnScreen.x - viewPortOffset.x;
        pageInfo->pageOnScreen.y = pageInfo->pageOnScreen.y - viewPortOffset.y;
        assert(max(pageInfo->pageOnScreen.x, 0) == pageInfo->screen.x || 0.0 == pageInfo->visibleRatio);
        assert(max(pageInfo->pageOnScreen.y, 0) == pageInfo->screen.y || 0.0 == pageInfo->visibleRatio);
    }
}

int DisplayModel::GetPageNoByPoint(PointI pt) 
{
    // no reasonable answer possible, if zoom hasn't been set yet
    if (!_zoomReal)
        return -1;

    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        assert(0.0 == pageInfo->visibleRatio || pageInfo->shown);
        if (!pageInfo->shown)
            continue;

        if (pageInfo->pageOnScreen.Inside(pt))
            return pageNo;
    }

    return -1;
}

int DisplayModel::GetPageNextToPoint(PointI pt)
{
    if (!_zoomReal)
        return _startPage;

    double maxDist = -1;
    int closest = _startPage;

    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        assert(0.0 == pageInfo->visibleRatio || pageInfo->shown);
        if (!pageInfo->shown)
            continue;

        if (pageInfo->pageOnScreen.Inside(pt))
            return pageNo;

        double dist = _hypot(pt.x - pageInfo->pageOnScreen.x - 0.5 * pageInfo->pageOnScreen.dx,
                             pt.y - pageInfo->pageOnScreen.y - 0.5 * pageInfo->pageOnScreen.dy);
        if (maxDist < 0 || dist < maxDist) {
            closest = pageNo;
            maxDist = dist;
        }
    }

    return closest;
}


PointI DisplayModel::CvtToScreen(int pageNo, PointD pt)
{
    PageInfo *pageInfo = getPageInfo(pageNo);
    assert(pageInfo);
    if (!pageInfo)
        return PointI();

    PointD p = engine->Transform(pt, pageNo, _zoomReal, _rotation);
    p.x += 0.5 + pageInfo->pos.x - viewPortOffset.x;
    p.y += 0.5 + pageInfo->pos.y - viewPortOffset.y;

    return p.Convert<int>();
}

RectI DisplayModel::CvtToScreen(int pageNo, RectD r)
{
    PointI TL = CvtToScreen(pageNo, r.TL());
    PointI BR = CvtToScreen(pageNo, r.BR());
    return RectI::FromXY(TL, BR);
}

PointD DisplayModel::CvtFromScreen(PointI pt, int pageNo)
{
    if (!validPageNo(pageNo))
        pageNo = GetPageNextToPoint(pt);

    const PageInfo *pageInfo = getPageInfo(pageNo);
    assert(pageInfo);
    if (!pageInfo)
        return PointD();

    PointD p = PointD(pt.x - 0.5 - pageInfo->pos.x + viewPortOffset.x,
                      pt.y - 0.5 - pageInfo->pos.y + viewPortOffset.y);
    return engine->Transform(p, pageNo, _zoomReal, _rotation, true);
}

RectD DisplayModel::CvtFromScreen(RectI r, int pageNo)
{
    PointD TL = CvtFromScreen(r.TL(), pageNo);
    PointD BR = CvtFromScreen(r.BR(), pageNo);
    return RectD::FromXY(TL, BR);
}

/* Given position 'x'/'y' in the draw area, returns a structure describing
   a link or NULL if there is no link at this position.
   TODO: this function is called frequently from UI code so make sure that
         it's fast enough for a decent number of links.
         Possible speed improvement: remember which links are visible after
         scrolling and skip the _Inside test for those invisible.
         Another way: build a list with only those visible, so we don't
         even have to traverse those that are invisible.
   */
PageElement *DisplayModel::GetElementAtPos(PointI pt)
{
    if (!pdfEngine)
        return NULL;

    int pageNo = GetPageNoByPoint(pt);
    if (!validPageNo(pageNo))
        return NULL;

    PointD pos = CvtFromScreen(pt, pageNo);
    return pdfEngine->GetElementAtPos(pageNo, pos);
}

bool DisplayModel::IsOverText(PointI pt)
{
    int pageNo = GetPageNoByPoint(pt);
    if (!validPageNo(pageNo))
        return false;

    PointD pos = CvtFromScreen(pt, pageNo);
    return textSelection->IsOverGlyph(pageNo, pos.x, pos.y);
}

void DisplayModel::RenderVisibleParts()
{
    int firstVisible = 0;
    int lastVisible = 0;

    for (int pageNo = 1; pageNo <= pageCount(); ++pageNo) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        if (pageInfo->visibleRatio > 0.0) {
            assert(pageInfo->shown);
            _callback->RenderPage(pageNo);
            if (0 == firstVisible)
                firstVisible = pageNo;
            lastVisible = pageNo;
        }
    }

#ifdef PREDICTIVE_RENDER
    // TODO: prerender two pages in Facing and Book View modes?
    if (lastVisible < pageCount())
        _callback->RenderPage(lastVisible + 1);
    if (firstVisible > 1)
        _callback->RenderPage(firstVisible - 1);
#endif
}

void DisplayModel::ChangeViewPortSize(SizeI newViewPortSize)
{
    ScrollState ss = GetScrollState();

    bool isIsDocLoaded = validPageNo(_startPage);
    totalViewPortSize = newViewPortSize;
    Relayout(_zoomVirtual, _rotation);
    if (isIsDocLoaded) {
        // when fitting to content, let goToPage do the necessary scrolling
        if (_zoomVirtual != ZOOM_FIT_CONTENT)
            SetScrollState(ss);
        else
            goToPage(ss.page, 0);
    } else {
        RecalcVisibleParts();
        RenderVisibleParts();
        _callback->UpdateScrollbars(canvasSize);
    }
}

RectD DisplayModel::getContentBox(int pageNo, RenderTarget target)
{
    RectI cbox;
    // we cache the contentBox for the View target
    if (Target_View == target) {
        PageInfo *pageInfo = getPageInfo(pageNo);
        if (pageInfo->contentBox.IsEmpty())
            pageInfo->contentBox = engine->PageContentBox(pageNo);
        cbox = pageInfo->contentBox;
    }
    else
        cbox = engine->PageContentBox(pageNo, target);

    return engine->Transform(cbox.Convert<double>(), pageNo, _zoomReal, _rotation);
}

/* get the (screen) coordinates of the point where a page's actual
   content begins (relative to the page's top left corner) */
PointI DisplayModel::getContentStart(int pageNo)
{
    RectD contentBox = getContentBox(pageNo);
    if (contentBox.IsEmpty())
        return PointI(0, 0);
    return contentBox.TL().Convert<int>();
}

void DisplayModel::goToPage(int pageNo, int scrollY, bool addNavPt, int scrollX)
{
    assert(validPageNo(pageNo));
    if (!validPageNo(pageNo))
        return;
    if (addNavPt)
        addNavPoint();

    /* in facing mode only start at odd pages (odd because page
       numbering starts with 1, so odd is really an even page) */
    if (displayModeFacing(displayMode()))
        pageNo = FirstPageInARowNo(pageNo, columnsFromDisplayMode(displayMode()), displayModeShowCover(displayMode()));

    if (!displayModeContinuous(displayMode())) {
        /* in single page mode going to another page involves recalculating
           the size of canvas */
        changeStartPage(pageNo);
    } else if (ZOOM_FIT_CONTENT == _zoomVirtual) {
        // make sure that setZoomVirtual uses the correct page to calculate
        // the zoom level for (visibility will be recalculated below anyway)
        for (int i =  pageCount(); i > 0; i--)
            getPageInfo(i)->visibleRatio = (i == pageNo ? 1.0f : 0);
        Relayout(_zoomVirtual, _rotation);
    }
    //DBG_OUT("DisplayModel::goToPage(pageNo=%d, scrollY=%d)\n", pageNo, scrollY);
    PageInfo * pageInfo = getPageInfo(pageNo);

    if (-1 == scrollX && 0 == scrollY && ZOOM_FIT_CONTENT == _zoomVirtual) {
        // scroll down to where the actual content starts
        PointI start = getContentStart(pageNo);
        scrollX = start.x;
        scrollY = start.y;
        if (columnsFromDisplayMode(displayMode()) > 1) {
            int lastPageNo = LastPageInARowNo(pageNo, columnsFromDisplayMode(displayMode()), displayModeShowCover(displayMode()), pageCount());
            PointI second = getContentStart(lastPageNo);
            scrollY = min(scrollY, second.y);
        }
        viewPortOffset.x = scrollX + pageInfo->pos.x - padding->left;
    }
    else if (-1 != scrollX)
        viewPortOffset.x = scrollX;
    // make sure to not display the blank space beside the first page in cover mode
    else if (-1 == scrollX && 1 == pageNo && displayModeShowCover(displayMode()))
        viewPortOffset.x = pageInfo->pos.x - padding->left;

    /* Hack: if an image is smaller in Y axis than the draw area, then we center
       the image by setting pageInfo->currPos.y in RecalcPagesInfo. So we shouldn't
       scroll (adjust viewPortOffset.y) there because it defeats the purpose.
       TODO: is there a better way of y-centering? */
    viewPortOffset.y = scrollY;
    // Move the next page to the top (unless the remaining pages fit onto a single screen)
    if (displayModeContinuous(displayMode()))
        viewPortOffset.y = pageInfo->pos.y - padding->top + scrollY;

    viewPortOffset.x = limitValue(viewPortOffset.x, 0, canvasSize.dx - viewPortSize.dx);
    viewPortOffset.y = limitValue(viewPortOffset.y, 0, canvasSize.dy - viewPortSize.dy);

    RecalcVisibleParts();
    RenderVisibleParts();
    _callback->UpdateScrollbars(canvasSize);
    _callback->PageNoChanged(pageNo);
    RepaintDisplay();
}

void DisplayModel::changeDisplayMode(DisplayMode displayMode)
{
    if (_displayMode == displayMode)
        return;

    int currPageNo = currentPageNo();
    _displayMode = displayMode;
    if (displayModeContinuous(displayMode)) {
        /* mark all pages as shown but not yet visible. The equivalent code
           for non-continuous mode is in DisplayModel::changeStartPage() called
           from DisplayModel::goToPage() */
        for (int pageNo = 1; pageNo <= pageCount(); pageNo++) {
            PageInfo *pageInfo = &(_pagesInfo[pageNo-1]);
            pageInfo->shown = true;
            pageInfo->visibleRatio = 0.0;
        }
        Relayout(_zoomVirtual, _rotation);
    }
    goToPage(currPageNo, 0);
}

void DisplayModel::setPresentationMode(bool enable)
{
    _presentationMode = enable;
    if (enable) {
        _presDisplayMode = _displayMode;
        _presZoomVirtual = _zoomVirtual;
        padding = &gPagePaddingPresentation;
        changeDisplayMode(DM_SINGLE_PAGE);
        zoomTo(ZOOM_FIT_PAGE);
    }
    else {
        padding = &gPagePadding;
        if (cbxEngine || imageEngine)
            padding = &gImagePadding;
        changeDisplayMode(_presDisplayMode);
        if (!ValidZoomVirtual(_presZoomVirtual))
            _presZoomVirtual = _zoomVirtual;
        zoomTo(_presZoomVirtual);
    }
}

/* In continuous mode just scrolls to the next page. In single page mode
   rebuilds the display model for the next page.
   Returns true if advanced to the next page or false if couldn't advance
   (e.g. because already was at the last page) */
bool DisplayModel::goToNextPage(int scrollY)
{
    int columns = columnsFromDisplayMode(displayMode());
    int currPageNo = currentPageNo();
    // Fully display the current page, if the previous page is still visible
    if (validPageNo(currPageNo - columns) && pageVisible(currPageNo - columns) && getPageInfo(currPageNo)->visibleRatio < 1.0) {
        goToPage(currPageNo, scrollY);
        return true;
    }
    int firstPageInNewRow = FirstPageInARowNo(currPageNo + columns, columns, displayModeShowCover(displayMode()));
//    DBG_OUT("DisplayModel::goToNextPage(scrollY=%d), currPageNo=%d, firstPageInNewRow=%d\n", scrollY, currPageNo, firstPageInNewRow);
    if (firstPageInNewRow > pageCount()) {
        /* we're on a last row or after it, can't go any further */
        return FALSE;
    }
    goToPage(firstPageInNewRow, scrollY);
    return TRUE;
}

bool DisplayModel::goToPrevPage(int scrollY)
{
    int columns = columnsFromDisplayMode(displayMode());
    int currPageNo = currentPageNo();
    DBG_OUT("DisplayModel::goToPrevPage(scrollY=%d), currPageNo=%d\n", scrollY, currPageNo);

    PointI top;
    if ((0 == scrollY || -1 == scrollY) && _zoomVirtual == ZOOM_FIT_CONTENT)
        top = getContentStart(currPageNo);

    PageInfo * pageInfo = getPageInfo(currPageNo);
    if (_zoomVirtual == ZOOM_FIT_CONTENT && pageInfo->bitmap.y <= top.y)
        scrollY = 0; // continue, even though the current page isn't fully visible
    else if (pageInfo->bitmap.y > scrollY && displayModeContinuous(displayMode())) {
        /* the current page isn't fully visible, so show it first */
        goToPage(currPageNo, scrollY);
        return true;
    }
    int firstPageInNewRow = FirstPageInARowNo(currPageNo - columns, columns, displayModeShowCover(displayMode()));
    if (firstPageInNewRow < 1) {
        /* we're on a first page, can't go back */
        return FALSE;
    }

    // scroll to the bottom of the page
    if (-1 == scrollY)
        scrollY = getPageInfo(firstPageInNewRow)->bitmap.dy;

    goToPage(firstPageInNewRow, scrollY);
    return TRUE;
}

bool DisplayModel::goToLastPage()
{
    DBG_OUT("DisplayModel::goToLastPage()\n");

    int columns = columnsFromDisplayMode(displayMode());
    int currPageNo = currentPageNo();
    int newPageNo = pageCount();
    int firstPageInLastRow = FirstPageInARowNo(newPageNo, columns, displayModeShowCover(displayMode()));

    if (currPageNo == firstPageInLastRow) /* are we on the last page already ? */
        return FALSE;
    goToPage(firstPageInLastRow, 0, true);
    return TRUE;
}

bool DisplayModel::goToFirstPage()
{
    DBG_OUT("DisplayModel::goToFirstPage()\n");

    if (displayModeContinuous(displayMode())) {
        if (0 == viewPortOffset.y) {
            return FALSE;
        }
    } else {
        assert(pageShown(_startPage));
        if (1 == _startPage) {
            /* we're on a first page already */
            return FALSE;
        }
    }
    goToPage(1, 0, true);
    return TRUE;
}

void DisplayModel::scrollXTo(int xOff)
{
    DBG_OUT("DisplayModel::scrollXTo(xOff=%d)\n", xOff);
    
    int currPageNo = currentPageNo();
    viewPortOffset.x = xOff;
    RecalcVisibleParts();
    _callback->UpdateScrollbars(canvasSize);
    
    if (currentPageNo() != currPageNo)
        _callback->PageNoChanged(currentPageNo());
    RepaintDisplay();
}

void DisplayModel::scrollXBy(int dx)
{
    DBG_OUT("DisplayModel::scrollXBy(dx=%d)\n", dx);

    int newOffX = limitValue(viewPortOffset.x + dx, 0, canvasSize.dx - viewPortSize.dx);
    if (newOffX != viewPortOffset.x)
        scrollXTo(newOffX);
}

void DisplayModel::scrollYTo(int yOff)
{
    DBG_OUT("DisplayModel::scrollYTo(yOff=%d)\n", yOff);

    int currPageNo = currentPageNo();
    viewPortOffset.y = yOff;
    RecalcVisibleParts();
    RenderVisibleParts();

    int newPageNo = currentPageNo();
    if (newPageNo != currPageNo)
        _callback->PageNoChanged(newPageNo);
    RepaintDisplay();
}

/* Scroll the doc in y-axis by 'dy'. If 'changePage' is TRUE, automatically
   switch to prev/next page in non-continuous mode if we scroll past the edges
   of current page */
void DisplayModel::scrollYBy(int dy, bool changePage)
{
    PageInfo *   pageInfo;
    int             currYOff = viewPortOffset.y;
    int             newPageNo;
    int             currPageNo;

    DBG_OUT("DisplayModel::scrollYBy(dy=%d, changePage=%d)\n", dy, (int)changePage);
    assert(0 != dy);
    if (0 == dy) return;

    int newYOff = currYOff;

    if (!displayModeContinuous(displayMode()) && changePage) {
        if ((dy < 0) && (0 == currYOff)) {
            if (_startPage > 1) {
                newPageNo = _startPage-1;
                assert(validPageNo(newPageNo));
                pageInfo = getPageInfo(newPageNo);
                newYOff = pageInfo->pos.dy - viewPortSize.dy;
                if (newYOff < 0)
                    newYOff = 0; /* TODO: center instead? */
                goToPrevPage(newYOff);
                return;
            }
        }

        /* see if we have to change page when scrolling forward */
        if ((dy > 0) && (_startPage < pageCount())) {
            if (viewPortOffset.y + viewPortSize.dy >= canvasSize.dy) {
                goToNextPage(0);
                return;
            }
        }
    }

    newYOff += dy;
    newYOff = limitValue(newYOff, 0, canvasSize.dy - viewPortSize.dy);
    if (newYOff == currYOff)
        return;

    currPageNo = currentPageNo();
    viewPortOffset.y = newYOff;
    RecalcVisibleParts();
    RenderVisibleParts();
    _callback->UpdateScrollbars(canvasSize);
    newPageNo = currentPageNo();
    if (newPageNo != currPageNo)
        _callback->PageNoChanged(newPageNo);
    RepaintDisplay();
}

void DisplayModel::zoomTo(float zoomVirtual, PointI *fixPt)
{
    if (!ValidZoomVirtual(zoomVirtual))
        return;

    ScrollState ss = GetScrollState();

    int centerPage;
    PointD centerPt;
    if (fixPt) {
        centerPage = GetPageNoByPoint(*fixPt);
        if (validPageNo(centerPage))
            centerPt = CvtFromScreen(*fixPt, centerPage);
        else
            fixPt = NULL;
    }

    if (ZOOM_FIT_CONTENT == zoomVirtual) {
        // SetScrollState's first call to goToPage will already scroll to fit
        ss.x = ss.y = -1;
        fixPt = NULL;
    }

    //DBG_OUT("DisplayModel::zoomTo() zoomVirtual=%.6f\n", _zoomVirtual);
    Relayout(zoomVirtual, _rotation);
    SetScrollState(ss);

    if (fixPt) {
        // scroll so that the fix point remains in the same screen location after zooming
        PointI centerI = CvtToScreen(centerPage, centerPt);
        if (centerI.x - fixPt->x != 0)
            scrollXBy(centerI.x - fixPt->x);
        if (centerI.y - fixPt->y != 0)
            scrollYBy(centerI.y - fixPt->y, false);
    }
}

void DisplayModel::zoomBy(float zoomFactor, PointI *fixPt)
{
    // zoomTo expects a zoomVirtual, so undo the _dpiFactor here
    float newZoom = 100.0f * _zoomReal / _dpiFactor * zoomFactor;
    newZoom = limitValue(newZoom, ZOOM_MIN, ZOOM_MAX);
    //DBG_OUT("DisplayModel::zoomBy() zoomReal=%.6f, zoomFactor=%.2f, newZoom=%.2f\n", dm->zoomReal, zoomFactor, newZoom);
    zoomTo(newZoom, fixPt);
}

void DisplayModel::rotateBy(int newRotation)
{
    normalizeRotation(&newRotation);
    assert(0 != newRotation);
    if (0 == newRotation)
        return;
    assert(validRotation(newRotation));
    if (!validRotation(newRotation))
        return;

    newRotation += _rotation;
    normalizeRotation(&newRotation);
    assert(validRotation(newRotation));
    if (!validRotation(newRotation))
        return;

    int currPageNo = currentPageNo();
    Relayout(_zoomVirtual, newRotation);
    goToPage(currPageNo, 0);
}

/* Given <region> (in user coordinates ) on page <pageNo>, copies text in that region
 * into a newly allocated buffer (which the caller needs to free()). */
TCHAR *DisplayModel::getTextInRegion(int pageNo, RectD region)
{
    RectI *coords;
    TCHAR *pageText = engine->ExtractPageText(pageNo, _T("\r\n"), &coords);
    if (!pageText)
        return NULL;

    RectI regionI = region.Round();
    TCHAR *result = SAZA(TCHAR, Str::Len(pageText) + 1), *dest = result;
    for (TCHAR *src = pageText; *src; src++) {
        if (*src != '\r' && *src != '\n') {
            RectI rect = coords[src - pageText];
            RectI isect = regionI.Intersect(rect);
            if (!isect.IsEmpty() && 1.0 * isect.dx * isect.dy / (rect.dx * rect.dy) >= 0.3) {
                *dest++ = *src;
                //DBG_OUT("Char: %c : %d; ushort: %hu\n", (char)c, (int)c, (unsigned short)c);
                //DBG_OUT("Found char: %c : %hu; real %c : %hu\n", c, (unsigned short)(unsigned char) c, ln->text[i].c, ln->text[i].c);
            }
        } else if (dest > result && *(dest - 1) != '\n') {
            *dest++ = *src;
            //DBG_OUT("Char: %c : %d; ushort: %hu\n", (char)buf[p], (int)(unsigned char)buf[p], buf[p]);
        }
    }
    *dest = '\0';

    delete[] coords;
    free(pageText);

    return result;
}

// returns true if it was necessary to scroll the display (horizontally or vertically)
bool DisplayModel::ShowResultRectToScreen(TextSel *res)
{
    if (!res->len)
        return false;

    RectI extremes;
    for (int i = 0; i < res->len; i++) {
        RectI rc = CvtToScreen(res->pages[i], res->rects[i].Convert<double>());
        extremes = extremes.Union(rc);
    }

    PageInfo *pageInfo = getPageInfo(res->pages[0]);
    int sx = 0, sy = 0;
    //
    // Vertical scroll
    //

    // scroll up to make top side of selection visible
    if (extremes.y < 0) {
        sy = extremes.y - 5;
        if (sy + pageInfo->screen.y + pageInfo->bitmap.y < 0)
            sy = -(pageInfo->screen.y + pageInfo->bitmap.y);
    }
    // scroll down to make top side of selection visible
    else if (extremes.y >= viewPortSize.dy) {
        sy = extremes.y - viewPortSize.dy + 5;
    }

    // scroll up to make bottom side of selection visible
    // (if selection height fits in visible area)
    if (extremes.y + extremes.dy > viewPortSize.dy
        && extremes.dy <= viewPortSize.dy + 5) {
        sy = extremes.y + extremes.dy - viewPortSize.dy + 5;
    }

    //
    // Horizontal scroll
    //

    // scroll left to make left side of selection visible
    if (extremes.x < 0) {
        sx = extremes.x - 5;
        if (sx + pageInfo->screen.x + pageInfo->bitmap.x < 0)
            sx = -(pageInfo->screen.x + pageInfo->bitmap.x);
    }
    // scroll right to make left side of selection visible
    else if (extremes.x >= viewPortSize.dx) {
        sx = extremes.x - viewPortSize.dx + 5;
    }
    // scroll left to make right side of selection visible
    // (if selection width fits in visible area)
    if (extremes.x + extremes.dx > viewPortSize.dx
        && extremes.dx <= viewPortSize.dx - 5) {
        sx = extremes.x + extremes.dx - viewPortSize.dx + 5;
    }

    if (sx != 0)
        scrollXBy(sx);
    if (sy != 0)
        scrollYBy(sy, false);

    return sx != 0 || sy != 0;
}

ScrollState DisplayModel::GetScrollState()
{
    ScrollState state(firstVisiblePageNo(), -1, -1);
    if (!validPageNo(state.page))
        state.page = currentPageNo();

    PageInfo *pageInfo = getPageInfo(state.page);
    // Shortcut: don't calculate precise positions, if the
    // page wasn't scrolled right/down at all
    if (pageInfo->screen.x > 0 && pageInfo->screen.y > 0)
        return state;

    PointI pt;
    pt.x = max(pageInfo->screen.x - pageInfo->bitmap.x, 0);
    pt.y = max(pageInfo->screen.y - pageInfo->bitmap.y, 0);

    state.page = GetPageNextToPoint(pt);
    PointD ptD = CvtFromScreen(pt, state.page);
    // Remember to show the margin, if it's currently visible
    if (pageInfo->screen.x <= 0)
        state.x = ptD.x;
    if (pageInfo->screen.y <= 0)
        state.y = ptD.y;

    return state;
}

void DisplayModel::SetScrollState(ScrollState state)
{
    // Update the internal metrics first
    goToPage(state.page, 0);
    // Bail out, if the page wasn't scrolled
    if (state.x < 0 && state.y < 0)
        return;

    PointD newPtD(max(state.x, 0), max(state.y, 0));
    PointI newPt = CvtToScreen(state.page, newPtD);

    // Also show the margins, if this has been requested
    if (state.x < 0)
        newPt.x = -1;
    else
        newPt.x += viewPortOffset.x;
    if (state.y < 0)
        newPt.y = 0;
    goToPage(state.page, newPt.y, false, newPt.x);
}

/* Records the current scroll state for later navigating back to. */
void DisplayModel::addNavPoint(bool keepForward)
{
    ScrollState ss = GetScrollState();

    if (!keepForward && _navHistoryIx > 0 && !memcmp(&ss, &_navHistory[_navHistoryIx - 1], sizeof(ss))) {
        // don't add another point to exact the same position (so overwrite instead of append)
        _navHistoryIx--;
    }
    else if (NAV_HISTORY_LEN == _navHistoryIx) {
        memmove(_navHistory, _navHistory + 1, (NAV_HISTORY_LEN - 1) * sizeof(ScrollState));
        _navHistoryIx--;
    }
    _navHistory[_navHistoryIx] = ss;
    _navHistoryIx++;
    if (!keepForward || _navHistoryIx > _navHistoryEnd)
        _navHistoryEnd = _navHistoryIx;
}

bool DisplayModel::canNavigate(int dir) const
{
    return _navHistoryIx + dir >= 0 && _navHistoryIx + dir < _navHistoryEnd && (_navHistoryIx != NAV_HISTORY_LEN || _navHistoryIx + dir != 0);
}

/* Navigates |dir| steps forward or backwards. */
void DisplayModel::navigate(int dir)
{
    if (!canNavigate(dir))
        return;
    addNavPoint(true);
    _navHistoryIx += dir - 1; // -1 because adding a nav point increases the index
    if (dir != 0 && _navHistory[_navHistoryIx].page != 0)
        SetScrollState(_navHistory[_navHistoryIx]);
}

DisplayModel *DisplayModel::CreateFromFileName(DisplayModelCallback *callback,
    const TCHAR *fileName, DisplayMode displayMode, int startPage, SizeI viewPort)
{
    DisplayModel *dm = new DisplayModel(callback, displayMode);
    if (!dm || !dm->load(fileName, startPage, viewPort)) {
        delete dm;
        return NULL;
    }

//    DBG_OUT("DisplayModel::CreateFromFileName() pageCount = %d, startPage=%d, displayMode=%d\n",
//        dm->pageCount(), startPage, (int)displayMode);
    return dm;
}
