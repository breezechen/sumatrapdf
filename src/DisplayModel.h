/* Copyright Krzysztof Kowalczyk 2006-2009
   License: GPLv3 */
#ifndef _DISPLAY_MODEL_H_
#define _DISPLAY_MODEL_H_

#include "base_util.h"
#include "geom_util.h"
#include "DisplayState.h"
#include "PdfEngine.h"
#include "PdfSearch.h"

#define INVALID_ZOOM        -99
#define INVALID_BIG_ZOOM    999999.0   /* arbitrary but big */

typedef struct DisplaySettings {
    int     pageBorderTop;
    int     pageBorderBottom;
    int     pageBorderLeft;
    int     pageBorderRight;
    int     betweenPagesX;
    int     betweenPagesY;
} DisplaySettings;

/* the default distance between a page and window border edges, in pixels */
#define PADDING_PAGE_BORDER_TOP_DEF      5
#define PADDING_PAGE_BORDER_BOTTOM_DEF   7
#define PADDING_PAGE_BORDER_LEFT_DEF     5
#define PADDING_PAGE_BORDER_RIGHT_DEF    7
/* the distance between pages in x axis, in pixels. Only applicable if
   columns > 1 */
#define PADDING_BETWEEN_PAGES_X_DEF      8
/* the distance between pages in y axis, in pixels. Only applicable if
   more than one page in y axis (continuous mode) */
#define PADDING_BETWEEN_PAGES_Y_DEF      PADDING_PAGE_BORDER_TOP_DEF + PADDING_PAGE_BORDER_BOTTOM_DEF

#define POINT_OUT_OF_PAGE           0

#define NAV_HISTORY_LEN             50

/* Describes many attributes of one page in one, convenient place */
typedef struct PdfPageInfo {
    /* data that is constant for a given page. page size and rotation
       recorded in PDF file */
    double          pageDx; // TODO: consider SizeD instead of pageDx/pageDy
    double          pageDy;
    int             rotation;

    /* data that is calculated when needed. actual content size within a page */
    fz_bbox         contentBox;

    /* data that needs to be set before DisplayModel_relayout().
       Determines whether a given page should be shown on the screen. */
    bool            shown;

    /* data that changes when zoom and rotation changes */
    /* position and size within total area after applying zoom and rotation.
       Represents display rectangle for a given page.
       Calculated in DisplayModel_relayout() */

    /* TODO: change it to RectD ?*/
    double          currDx;
    double          currDy;
    double          currPosX;
    double          currPosY;

    /* data that changes due to scrolling. Calculated in DisplayModel_RecalcVisibleParts() */
    double          visible; /* visible ratio of the page (0 = invisible, 1 = fully visible) */
    /* part of the image that should be shown */
    int             bitmapX, bitmapY, bitmapDx, bitmapDy;
    /* where it should be blitted on the screen */
    int             screenX, screenY;
    /* position of page relative to visible draw area */
    RectI           pageOnScreen;
} PdfPageInfo;

/* When searching, we can be in one of those states. The state determines what
   will happen after searching for next or previous term.
   */
enum SearchState { 
    /* Search hasn't started yet. 'Next' will start searching from the top
       of current page, searching forward. 'Previous' will start searching from
       the top of current page, searching backward. */
    eSsNone,
    /* We searched the whole document and didn't find the search term at all. 'Next'
       and 'prev' do nothing. */
    eSsNotFound,
    /* Previous 'next' search found the term, without wrapping. 'Next' will
       continue searching forward from the current position. 'Previous' will
       search backward from the current position.*/
    eSsFoundNext, 
    /* Like eSsFoundNext but we wrapped past last page. In that case we show
       a message about being wrapped and continuing from top. */
    eSsFoundNextWrapped,
    /* Previous 'prev' search found the term, without wrapping. 'Next' will
       search forward from the current position. 'Prev' will continue searching
       backward */
    eSsFoundPrev,
    /* Like eSsFoundPrev, but wrapped around first page. */
    eSsFoundPrevWrapped,
/*   TODO: add eSsFoundOnlyOne as optimization i.e. if the hit is the same
   as previous hit, 'next' and 'previous' will do nothing, to avoid (possibly
   long) no-op search.
    eSsFoundOnlyOne */
};

/* The current state of searching */
typedef struct SearchStateData {
    /* The page on which we started the search */
    int             startPage;
    /* did we wrap (crossed last page when searching forward or first page
       when searching backwards) */
    BOOL            wrapped;
    SearchState     searchState;
    BOOL            caseSensitive;
    int             currPage; /* page for the last hit */
} SearchStateData;

/* The current scroll state (needed for saving/restoring the scroll position) */
typedef struct ScrollState {
    int page;
    double x; /* in user space units (per page) */
    double y; /* in user space units (per page) */
} ScrollState;

/* Information needed to drive the display of a given PDF document on a screen.
   You can think of it as a model in the MVC pardigm.
   All the display changes should be done through changing this model via
   API and re-displaying things based on new display information */
class DisplayModel
{
public:
    DisplayModel(DisplayMode displayMode, int dpi=USER_DEFAULT_SCREEN_DPI);
    ~DisplayModel();

    RenderedBitmap *renderBitmap(int pageNo, double zoomReal, int rotation,
                         fz_rect *pageRect, /* if NULL: defaults to the page's mediabox */
                         BOOL (*abortCheckCbkA)(void *data),
                         void *abortCheckCbkDataA,
                         RenderTarget target=Target_View,
                         bool useGdi=false) {
        if (!pdfEngine) return NULL;
        return pdfEngine->renderBitmap(pageNo, zoomReal, rotation, pageRect,
            abortCheckCbkA, abortCheckCbkDataA, target, useGdi);
    }
    bool renderPage(HDC hDC, int pageNo, RECT *screenRect, double zoomReal=0, int rotation=0, fz_rect *pageRect=NULL, RenderTarget target=Target_View) {
        if (!pdfEngine) return false;
        return pdfEngine->renderPage(hDC, pageNo, screenRect, NULL, zoomReal, rotation, pageRect, target);
    }

    /* number of pages in PDF document */
    int  pageCount() const { return pdfEngine->pageCount(); }
    bool load(const TCHAR *fileName, int startPage, WindowInfo *win, bool tryrepair);
    bool validPageNo(int pageNo) const { return pdfEngine->validPageNo(pageNo); }
    bool hasTocTree() { return pdfEngine->hasTocTree(); }
    PdfTocItem *getTocTree() { return pdfEngine->getTocTree(); }

    /* current rotation selected by user */
    int rotation(void) const { return _rotation; }
    void setRotation(int rotation) { _rotation = rotation; }

    DisplayMode displayMode() const { return _displayMode; }
    void changeDisplayMode(DisplayMode displayMode);
    void setPresentationMode(bool enable);
    bool getPresentationMode() const { return _presentationMode; }

    const TCHAR *fileName(void) const { return pdfEngine->fileName(); }

    /* a "virtual" zoom level. Can be either a real zoom level in percent
       (i.e. 100.0 is original size) or one of virtual values ZOOM_FIT_PAGE,
       ZOOM_FIT_WIDTH or ZOOM_FIT_CONTENT, whose real value depends on draw area size */
    double zoomVirtual(void) const { return _zoomVirtual; }

    double zoomReal(void) const { return _zoomReal; }
    double zoomReal(int pageNo);

    int startPage(void) const { return _startPage; }

    int currentPageNo(void) const;

    PdfEngine *     pdfEngine;

    /* an arbitrary pointer that can be used by an app e.g. a multi-window GUI
       could link this to a data describing window displaying  this document */
    void * appData() const { return _appData; }

    void setAppData(void *appData) { _appData = appData; }

    /* TODO: rename to pageInfo() */
    PdfPageInfo * getPageInfo(int pageNo) const;

    /* an array of PdfPageInfo, len of array is pageCount */
    PdfPageInfo *   _pagesInfo;

    /* areaOffset is "polymorphic". If drawAreaSize.dx > totalAreSize.dx then
       areaOffset.x is offset of total area rect inside draw area, otherwise
       an offset of draw area inside total area.
       The same for areaOff.y, except it's for dy */
    PointD          areaOffset;

    /* size of draw area i.e. _totalDrawAreaSize minus scrollbarsSize (if
       they're shown) */
    SizeD           drawAreaSize;

    SearchStateData searchState;

    int             searchHitPageNo;
    RectD           searchHitRectPage;
    RectI           searchHitRectCanvas;

    void            setScrollbarsSize(int scrollbarXDy, int scrollbarYDx) {
        _scrollbarXDy = scrollbarXDy;
        _scrollbarYDx = scrollbarYDx;
    }

    void            setTotalDrawAreaSize(SizeD size) {
        _totalDrawAreaSize = size;
        drawAreaSize = SizeD(size.dx() - _scrollbarYDx, size.dy() - _scrollbarXDy);
    }
    
    bool            needHScroll() { return drawAreaSize.dxI() < _canvasSize.dxI(); }
    bool            needVScroll() { return drawAreaSize.dyI() < _canvasSize.dyI(); }

    void            changeTotalDrawAreaSize(SizeD totalDrawAreaSize);

    bool            pageShown(int pageNo);
    bool            pageVisible(int pageNo);
    bool            pageVisibleNearby(int pageNo);
    void            relayout(double zoomVirtual, int rotation);

    void            goToPage(int pageNo, int scrollY, bool addNavPt=false, int scrollX=-1);
    bool            goToPrevPage(int scrollY);
    bool            goToNextPage(int scrollY);
    bool            goToFirstPage(void);
    bool            goToLastPage(void);

    void            scrollXTo(int xOff);
    void            scrollXBy(int dx);

    void            scrollYTo(int yOff);
    void            scrollYBy(int dy, bool changePage);

    void            zoomTo(double zoomVirtual, POINT *fixPt=NULL);
    void            zoomBy(double zoomFactor, POINT *fixPt=NULL);
    void            rotateBy(int rotation);

    TCHAR *         getTextInRegion(int pageNo, RectD *region);
    TCHAR *         extractAllText(RenderTarget target=Target_View);

    void            clearSearchHit(void);
    void            setSearchHit(int pageNo, RectD *hitRect);

    pdf_link *      getLinkAtPosition(int x, int y);
    int             getPdfLinks(int pageNo, pdf_link **links);
    TCHAR *         getLinkPath(pdf_link *link);
    void            goToTocLink(pdf_link *link);
    void            goToNamedDest(const char *name);

    bool            cvtUserToScreen(int pageNo, double *x, double *y);
    bool            cvtScreenToUser(int *pageNo, double *x, double *y);
    bool            rectCvtUserToScreen(int pageNo, RectD *r);
    fz_rect         rectCvtUserToScreen(int pageNo, fz_rect rect);
    bool            rectCvtScreenToUser(int *pageNo, RectD *r);

    void            SetFindMatchCase(bool match) { _pdfSearch->SetSensitive(match); }
    PdfSearchResult *Find(PdfSearchDirection direction = FIND_FORWARD, TCHAR *text = NULL, UINT fromPage = 0);
    int             lastFoundPage(void) const { return _pdfSearch->result.page; }
    BOOL            bFoundText;

    BOOL            _showToc;

    int             getPageNoByPoint (double x, double y);

    BOOL            MapResultRectToScreen(PdfSearchResult *res);

    bool            getScrollState(ScrollState *state);
    void            setScrollState(ScrollState *state);

    bool            addNavPoint(bool keepForward=false);
    bool            canNavigate(int dir) const;
    void            navigate(int dir);

    bool            saveStreamAs(fz_buffer *data, const TCHAR *fileName);

    bool            displayStateFromModel(DisplayState *ds);

    void            ageStore() const { pdfEngine->ageStore(); }
protected:

    void            startRenderingPage(int pageNo);

    bool            buildPagesInfo(void);
    double          zoomRealFromVirtualForPage(double zoomVirtual, int pageNo);
    int             firstVisiblePageNo(void) const;
    void            changeStartPage(int startPage);
    void            getContentStart(int pageNo, int *x, int *y);
    void            setZoomVirtual(double zoomVirtual);
    void            recalcVisibleParts(void);
    void            recalcSearchHitCanvasPos(void);
    void            renderVisibleParts(void);
    /* Those need to be implemented somewhere else by the GUI */
    void            setScrollbarsState(void);
    /* called when a page number changes */
    void            pageChanged(void);
    /* called when this DisplayModel is destroyed */
    void            clearAllRenderings(void);
public:
    /* called when we decide that the display needs to be redrawn */
    void            repaintDisplay(void);

protected:
    void            goToPdfDest(fz_obj *dest);

    PdfSearch *     _pdfSearch;
    DisplayMode     _displayMode; /* TODO: not used yet */
    /* In non-continuous mode is the first page from a PDF file that we're
       displaying.
       No meaning in continous mode. */
    int             _startPage;
    void *          _appData;

    /* size of scrollbars */
    int             _scrollbarXDy;
    int             _scrollbarYDx;

    /* size of total draw area (i.e. window size) */
    SizeD           _totalDrawAreaSize;

    /* size of virtual canvas containing all rendered pages.
       TODO: re-consider, 32 signed number should be large enough for everything. */
    SizeD           _canvasSize;
    DisplaySettings * _padding;

    /* real zoom value calculated from zoomVirtual. Same as zoomVirtual except
       for ZOOM_FIT_PAGE, ZOOM_FIT_WIDTH and ZOOM_FIT_CONTENT */
    double          _zoomReal;
    double          _zoomVirtual;
    int             _rotation;
    /* dpi correction factor by which _zoomVirtual has to be multiplied in
       order to get _zoomReal */
    double          _dpiFactor;

    /* if we're in presentation mode, _pres* contains the pre-presentation values */
    bool            _presentationMode;
    double          _presZoomVirtual;
    DisplayMode     _presDisplayMode;

    ScrollState   * _navHistory;
    int             _navHistoryIx;
    int             _navHistoryEnd;

public:
    /* allow resizing a window without triggering a new rendering (needed for window destruction) */
    bool            _dontRenderFlag;
};

bool                displayModeContinuous(DisplayMode displayMode);
bool                displayModeFacing(DisplayMode displayMode);
bool                displayModeShowCover(DisplayMode displayMode);
int                 columnsFromDisplayMode(DisplayMode displayMode);

DisplayModel *DisplayModel_CreateFromFileName(
  const TCHAR *fileName,
  SizeD totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage,
  WindowInfo *win, bool tryrepair);

#endif
