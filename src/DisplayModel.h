#ifndef _DISPLAY_MODEL_H_
#define _DISPLAY_MODEL_H_

#include "BaseUtils.h"
#include "PdfEngine.h"
#include "SimpleRect.h"

class GooString;
class Link;
class Links;
class TextPage;
class UGooString;

// TODO: remove those when dependency on Link and TextPage are gone
//#include "Link.h"
//#include "TextOutputDev.h"

#define INVALID_ZOOM        -99

enum DisplayMode {
    DM_FIRST = 1,
    DM_SINGLE_PAGE = DM_FIRST,
    DM_FACING,
    DM_CONTINUOUS,
    DM_CONTINUOUS_FACING,
    DM_LAST = DM_CONTINUOUS_FACING
};

typedef struct DisplaySettings {
    int     paddingPageBorderTop;
    int     paddingPageBorderBottom;
    int     paddingPageBorderLeft;
    int     paddingPageBorderRight;
    int     paddingBetweenPagesX;
    int     paddingBetweenPagesY;
} DisplaySettings;

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

/* Describes a link on PDF page. */
typedef struct PdfLink {
    int             pageNo;     /* on which Pdf page the link exists. 1..pageCount */
    // TODO: remove it from common code, only splash needs it
    Link *          link;       /* a reference to a link; we don't own it */
    RectD           rectPage;   /* position of the link on the page */
    RectI           rectCanvas; /* position of the link on canvas */
} PdfLink;

/* Describes many attributes of one page in one, convenient place */
typedef struct PdfPageInfo {
    /* data that is constant for a given page. page size and rotation
       recorded in PDF file */
    double          pageDx;
    double          pageDy;
    int             rotation;

    /* data that needs to be set before DisplayModel_Relayout().
       Determines whether a given page should be shown on the screen. */
    bool            shown;

    /* data that changes when zoom and rotation changes */
    /* position and size within total area after applying zoom and rotation.
       Represents display rectangle for a given page.
       Calculated in DisplayModel_Relayout() */

    /* TODO: change it to RectD ?*/
    double          currDx;
    double          currDy;
    double          currPosX;
    double          currPosY;

    /* data that changes due to scrolling. Calculated in DisplayModel_RecalcVisibleParts() */
    bool            visible; /* is currently visible on the page ? */
    /* part of the image that should be shown */
    int             bitmapX, bitmapY, bitmapDx, bitmapDy;
    /* where it should be blitted on the screen */
    int             screenX, screenY;

    Links *         links;

    // TODO: remove it from common code, only splash needs it
    TextPage *      textPage;
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
    /* String we search for, both regular and unicode versions */
    GooString *     str;
    UGooString *    strU;
    /* The page on which we started the search */
    int             startPage;
    /* did we wrap (crossed last page when searching forward or first page
       when searching backwards) */
    BOOL            wrapped;
    SearchState     searchState;
    BOOL            caseSensitive;
    int             currPage; /* page for the last hit */
} SearchStateData;


class DisplayModel
{
public:
    DisplayModel();
    virtual ~DisplayModel();

    /* number of pages in PDF document */
    int  pageCount() const {
        return _pageCount;
    }

    void setPageCount(int pageCount) {
        _pageCount = pageCount;
    }

    bool validPageNo(int pageNo) const
    {
        if ((pageNo >= 1) && (pageNo <= pageCount()))
            return true;
        return false;
    }

    /* current rotation selected by user */
    int rotation(void) const {
        return _rotation; 
    }

    void setRotation(int rotation) {
        _rotation = rotation;
    }

    DisplayMode displayMode() const {
        return _displayMode;
    }

    /* TODO: make non-virtual */
    virtual void SetDisplayMode(DisplayMode displayMode) = 0;

    const char *fileName(void) const {
        return _fileName;
    }

    void setFileName(const char *fileName) {
        _fileName = (const char*)Str_Dup(fileName);
    }

    bool fullScreen(void) const {
        return _fullScreen;
    }

    void setFullScreen(bool fullScreen) {
        _fullScreen = fullScreen;
    }

    /* a "virtual" zoom level. Can be either a real zoom level in percent
       (i.e. 100.0 is original size) or one of virtual values ZOOM_FIT_PAGE
       or ZOOM_FIT_WIDTH, whose real value depends on draw area size */
    double zoomVirtual(void) const {
        return _zoomVirtual;
    }

    virtual void SetZoomVirtual(double zoomVirtual) = 0;

    int startPage(void) const {
        return _startPage;
    }

    /* TODO: should become non-virtual */
    virtual int currentPageNo(void) const = 0;

    /* an arbitrary pointer that can be used by an app e.g. a multi-window GUI
       could link this to a data describing window displaying  this document */
    void * appData() const {
        return _appData;
    }

    void setAppData(void *appData) {
        _appData = appData;
    }

    /* an array of PdfPageInfo, len of array is pageCount */
    PdfPageInfo *   pagesInfo;

    /* areaOffset is "polymorphic". If drawAreaSize.dx > totalAreSize.dx then
       areaOffset.x is offset of total area rect inside draw area, otherwise
       an offset of draw area inside total area.
       The same for areaOff.y, except it's for dy */
    RectDPos        areaOffset;

    /* size of draw area i.e. totalDrawAreaSize minus scrollbarsSize (if
       they're shown) */
    RectDSize       drawAreaSize;

    /* size of scrollbars */
    int             scrollbarXDy;
    int             scrollbarYDx;

    /* size of total draw area (i.e. window size) */
    RectDSize       totalDrawAreaSize;

    SearchStateData searchState;

    int             searchHitPageNo;
    RectD           searchHitRectPage;
    RectI           searchHitRectCanvas;

protected:
    const char *    _fileName;
    DisplayMode     _displayMode;
    int             _pageCount;
    int             _rotation;
    double          _zoomVirtual;
    bool            _fullScreen;
    int             _startPage;
    void *          _appData;
};

bool                IsDisplayModeContinuous(DisplayMode displayMode);
DisplaySettings *   GetGlobalDisplaySettings(void);

extern DisplaySettings gDisplaySettings;

/* must be implemented somewhere else */
extern void         LaunchBrowser(const char *uri);

#endif
