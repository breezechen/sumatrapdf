/* Copyright 2006-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#ifndef DisplayModel_h
#define DisplayModel_h

#include "PdfEngine.h"
#include "ImagesEngine.h"
#include "DisplayState.h"
#include "TextSearch.h"

// define the following if you want shadows drawn around the pages
// #define DRAW_PAGE_SHADOWS

#define INVALID_PAGE_NO     -1
#define INVALID_ROTATION    -1

typedef struct {
    // padding around the whole canvas
    int top, left;
    int bottom, right;
    // padding between two pages in X and Y direction
    int inBetweenX, inBetweenY;
} ScreenPagePadding;

/* the default distance between a page and window border edges, in pixels */
#ifdef DRAW_PAGE_SHADOWS
#define PADDING_PAGE_BORDER_TOP_DEF      5
#define PADDING_PAGE_BORDER_LEFT_DEF     5
#define PADDING_PAGE_BORDER_BOTTOM_DEF   7
#define PADDING_PAGE_BORDER_RIGHT_DEF    7
/* the distance between pages in y axis, in pixels. Only applicable if
   more than one page in y axis (continuous mode) */
#define PADDING_BETWEEN_PAGES_Y_DEF      8
#else
// TODO: those should probably be more modular so that we can control space
// before first page, after last page and between pages (currently
// betwen pages = first page + last page (i.e. top + bottom)
#define PADDING_PAGE_BORDER_TOP_DEF      2
#define PADDING_PAGE_BORDER_LEFT_DEF     4
#define PADDING_PAGE_BORDER_BOTTOM_DEF   2
#define PADDING_PAGE_BORDER_RIGHT_DEF    4
/* the distance between pages in y axis, in pixels. Only applicable if
   more than one page in y axis (continuous mode) */
#define PADDING_BETWEEN_PAGES_Y_DEF      PADDING_PAGE_BORDER_TOP_DEF + PADDING_PAGE_BORDER_BOTTOM_DEF
#endif
/* the distance between pages in x axis, in pixels. Only applicable if
   columns > 1 */
#define PADDING_BETWEEN_PAGES_X_DEF      PADDING_BETWEEN_PAGES_Y_DEF

#define NAV_HISTORY_LEN             50

/* Describes many attributes of one page in one, convenient place */
typedef struct {
    /* data that is constant for a given page. page size and rotation
       recorded in PDF file */
    SizeD           page;
    int             rotation;

    /* data that is calculated when needed. actual content size within a page (View target) */
    RectI           contentBox;

    /* data that needs to be set before DisplayModel::Relayout().
       Determines whether a given page should be shown on the screen. */
    bool            shown;

    /* data that changes when zoom and rotation changes */
    /* position and size within total area after applying zoom and rotation.
       Represents display rectangle for a given page.
       Calculated in DisplayModel::Relayout() */
    RectI           pos;
    /* data that changes due to scrolling. Calculated in DisplayModel::RecalcVisibleParts() */
    float           visibleRatio; /* (0.0 = invisible, 1.0 = fully visible) */
    /* part of the image that should be shown */
    RectI           bitmap;
    /* where it should be blitted in the view port */
    PointI          screen;
    /* position of page relative to visible view port */
    RectI           pageOnScreen;
} PageInfo;

class DisplayModel;

class DisplayModelCallback : public PasswordUI, public TextSearchTracker {
public:
    virtual void Repaint() = 0;
    virtual void PageNoChanged(int pageNo) = 0;
    virtual void UpdateScrollbars(SizeI canvas) = 0; // TODO: no longer need to pass canvas in
    virtual void RenderPage(int pageNo) = 0;
    virtual int  GetScreenDPI() = 0;
    virtual void CleanUp(DisplayModel *dm) = 0;
};

/* Information needed to drive the display of a given document on a screen.
   You can think of it as a model in the MVC pardigm.
   All the display changes should be done through changing this model via
   API and re-displaying things based on new display information */
class DisplayModel
{
public:
    DisplayModel(DisplayModelCallback *callback, DisplayMode displayMode);
    ~DisplayModel();

    const TCHAR *fileName() const { return engine->FileName(); }
    /* number of pages in the document */
    int  pageCount() const { return engine->PageCount(); }
    bool validPageNo(int pageNo) const {
        return 1 <= pageNo && pageNo <= engine->PageCount();
    }

    bool hasTocTree() {
        if (!pdfEngine)
            return false;
        return pdfEngine->hasTocTree();
    }
    PdfTocItem *getTocTree() {
        if (!pdfEngine)
            return NULL;
        return pdfEngine->getTocTree();
    }

    /* current rotation selected by user */
    int rotation() const { return _rotation; }
    void setRotation(int rotation) { _rotation = rotation; }

    DisplayMode displayMode() const { return _displayMode; }
    void changeDisplayMode(DisplayMode displayMode);
    void setPresentationMode(bool enable);
    bool getPresentationMode() const { return _presentationMode; }

    /* a "virtual" zoom level. Can be either a real zoom level in percent
       (i.e. 100.0 is original size) or one of virtual values ZOOM_FIT_PAGE,
       ZOOM_FIT_WIDTH or ZOOM_FIT_CONTENT, whose real value depends on draw area size */
    float zoomVirtual() const { return _zoomVirtual; }
    float zoomReal() const { return _zoomReal; }
    float zoomReal(int pageNo);

    int startPage() const { return _startPage; }
    int currentPageNo() const;

    BaseEngine *    engine;
    PdfEngine *     pdfEngine;
    XpsEngine *     xpsEngine;
    ImagesEngine *  imagesEngine;
    TextSelection * textSelection;

    PageInfo *      getPageInfo(int pageNo) const;

    /* viewPortOffset is "polymorphic". If viewPortSize.dx > totalAreSize.dx then
       viewPortOffset.x is offset of total area rect inside draw area, otherwise
       an offset of draw area inside total area.
       The same for viewPortOffset.y, except it's for dy */
    PointI          viewPortOffset;
    /* total size of view port (draw area), including scroll bars */
    SizeI           totalViewPortSize;
    /* size of view port available for content (totalViewPortSize minus scroll bars) */
    SizeI           viewPortSize;

    bool            needHScroll() const { return viewPortSize.dy < totalViewPortSize.dy; }
    bool            needVScroll() const { return viewPortSize.dx < totalViewPortSize.dx; }

    void            ChangeViewPortSize(SizeI newViewPortSize);

    bool            pageShown(int pageNo);
    bool            pageVisible(int pageNo);
    bool            pageVisibleNearby(int pageNo);
    int             firstVisiblePageNo() const;
    bool            firstBookPageVisible();
    bool            lastBookPageVisible();
    void            Relayout(float zoomVirtual, int rotation);

    void            goToPage(int pageNo, int scrollY, bool addNavPt=false, int scrollX=-1);
    bool            goToPrevPage(int scrollY);
    bool            goToNextPage(int scrollY);
    bool            goToFirstPage();
    bool            goToLastPage();

    void            scrollXTo(int xOff);
    void            scrollXBy(int dx);

    void            scrollYTo(int yOff);
    void            scrollYBy(int dy, bool changePage);

    void            zoomTo(float zoomVirtual, PointI *fixPt=NULL);
    void            zoomBy(float zoomFactor, PointI *fixPt=NULL);
    void            rotateBy(int rotation);

    TCHAR *         getTextInRegion(int pageNo, RectD& region);
    bool            isOverText(int x, int y);

    // TODO: generalize for non-PDF formats(?)
    PdfLink *       getLinkAtPosition(PointI pt);
    PdfComment *    getCommentAtPosition(PointI pt);

    bool            cvtUserToScreen(int pageNo, PointD *pt);
    bool            cvtUserToScreen(int pageNo, RectD *r);
    bool            cvtScreenToUser(int *pageNo, PointD *pt);
    bool            cvtScreenToUser(int *pageNo, RectD *r);
    ScreenPagePadding *getPadding() { return padding; }
    RectD           getContentBox(int pageNo, RenderTarget target=Target_View);

    void            SetFindMatchCase(bool match) { _textSearch->SetSensitive(match); }
    TextSel *       Find(TextSearchDirection direction=FIND_FORWARD, TCHAR *text=NULL, UINT fromPage=0);
    // note: lastFoundPage might not be a valid page number!
    int             lastFoundPage() const { return _textSearch->findPage; }

    int             getPageNoByPoint(PointI pt);

    bool            ShowResultRectToScreen(TextSel *res);

    bool            getScrollState(ScrollState *state);
    void            setScrollState(ScrollState *state);

    bool            addNavPoint(bool keepForward=false);
    bool            canNavigate(int dir) const;
    void            navigate(int dir);

    bool            displayStateFromModel(DisplayState *ds);

    // called when we decide that the display needs to be redrawn
    void            RepaintDisplay() { if (_callback) _callback->Repaint(); }
    // called after rendering a page so that objects that had to be
    // allocated for this can be freed by the rendering engine
    void            runEngineGC() const { if (pdfEngine) pdfEngine->ageStore(); }

protected:

    bool            load(const TCHAR *fileName, int startPage, SizeI viewPort);

    bool            buildPagesInfo();
    float           zoomRealFromVirtualForPage(float zoomVirtual, int pageNo);
    void            changeStartPage(int startPage);
    void            getContentStart(int pageNo, int *x, int *y);
    void            setZoomVirtual(float zoomVirtual);
    void            RecalcVisibleParts();
    void            RenderVisibleParts();

    /* an array of PageInfo, len of array is pageCount */
    PageInfo *      _pagesInfo;

    TextSearch *    _textSearch;
    DisplayMode     _displayMode;
    /* In non-continuous mode is the first page from a file that we're
       displaying.
       No meaning in continous mode. */
    int             _startPage;

    /* A callback to notify UI about required changes */
    DisplayModelCallback *_callback;

    /* size of virtual canvas containing all rendered pages. */
    SizeI           canvasSize;
    ScreenPagePadding* padding;

    /* real zoom value calculated from zoomVirtual. Same as zoomVirtual * 0.01 except
       for ZOOM_FIT_PAGE, ZOOM_FIT_WIDTH and ZOOM_FIT_CONTENT */
    float           _zoomReal;
    float           _zoomVirtual;
    int             _rotation;
    /* dpi correction factor by which _zoomVirtual has to be multiplied in
       order to get _zoomReal */
    float           _dpiFactor;
    /* whether to display pages Left-to-Right or Right-to-Left.
       this value is extracted from the PDF document */
    bool            _displayR2L;

    /* when we're in presentation mode, _pres* contains the pre-presentation values */
    bool            _presentationMode;
    float           _presZoomVirtual;
    DisplayMode     _presDisplayMode;

    ScrollState   * _navHistory;
    int             _navHistoryIx;
    int             _navHistoryEnd;

public:
    /* allow resizing a window without triggering a new rendering (needed for window destruction) */
    bool            _dontRenderFlag;

    static DisplayModel *CreateFromFileName(DisplayModelCallback *callback,
                                            const TCHAR *fileName,
                                            DisplayMode displayMode,
                                            int startPage,
                                            SizeI viewPort);
};

bool    displayModeContinuous(DisplayMode displayMode);
bool    displayModeFacing(DisplayMode displayMode);
bool    displayModeShowCover(DisplayMode displayMode);
int     columnsFromDisplayMode(DisplayMode displayMode);
bool    rotationFlipped(int rotation);

#endif
