#ifndef _DISPLAY_MODEL_SPLASH_H_
#define _DISPLAY_MODEL_SPLASH_H_
/* Written by Krzysztof Kowalczyk (http://blog.kowalczyk.info)
   License: GPLv2 */

/* How to think of display logic: physical screen of size
   drawAreaSize is a window into (possibly much larger)
   total area (canvas) of size canvasSize.

   In DM_SINGLE_PAGE mode total area is the size of currently displayed page
   given current zoomLovel and rotation.
   In DM_CONTINUOUS mode total area consist of all pages rendered sequentially
   with a given zoomLevel and rotation. totalAreaDy is the sum of heights
   of all pages plus spaces between them and totalAreaDx is the size of
   the widest page.

   A possible configuration could look like this:

 -----------------------------------
 |                                 |
 |          -------------          |
 |          | screen    |          |
 |          | i.e.      |          |
 |          | drawArea  |          |
 |          -------------          |
 |                                 |
 |                                 |
 |    canvas                       |
 |                                 |
 |                                 |
 |                                 |
 |                                 |
 -----------------------------------

  We calculate the total area size and position of each page we display on the
  canvas. Canvas has to be >= draw area.

  Changing zoomLevel or rotation requires recalculation of total area and
  position of pdf pages in it.

  We keep the offset of draw area relative to total area. The offset changes
  due to scrolling (with keys or using scrollbars).

  To draw we calculate which part of each page overlaps draw area, we render
  those pages to a bitmap and display those bitmaps.
*/

#include "BaseUtils.h"
#include "DisplayState.h"
#include "DisplayModel.h"

class GooString;
class Link;
class LinkDest;
class LinkGoTo;
class LinkGoToR;
class LinkLaunch;
class LinkNamed;
class LinkURI;
class Links;
class PDFDoc;
class SplashOutputDev;
class SplashBitmap;
class TextOutputDev;
class TextPage;
class UGooString;

/* Abstract class representing cached bitmap. Allows different implementations
   on different platforms. */
class PlatformCachedBitmap {
public:
  virtual ~PlatformCachedBitmap() {};
protected:
  PlatformCachedBitmap() {}; /* disallow creating objects of this class */
};

/* must be implemented somewhere else */
extern void LaunchBrowser(const char *uri);

/* It seems that PDF documents are encoded assuming DPI of 72.0 */
#define PDF_FILE_DPI        72

/* arbitrary but big */
#define INVALID_BIG_ZOOM    999999.0

typedef struct DisplaySettings {
    int     paddingPageBorderTop;
    int     paddingPageBorderBottom;
    int     paddingPageBorderLeft;
    int     paddingPageBorderRight;
    int     paddingBetweenPagesX;
    int     paddingBetweenPagesY;
} DisplaySettings;

/* Describes a link on PDF page. */
typedef struct PdfLink {
    int             pageNo;     /* on which Pdf page the link exists. 1..pageCount */
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

/* Information needed to drive the display of a given PDF document on a screen.
   You can think of it as a model in the MVC pardigm.
   All the display changes should be done through changing this model via
   API and re-displaying things based on new display information */
class DisplayModelSplash : public DisplayModel
{
public:
    DisplayModelSplash(DisplayMode displayMode);
    virtual ~DisplayModelSplash();

    virtual void  SetDisplayMode(DisplayMode displayMode);
    virtual int   currentPageNo(void) const;
    virtual void  SetZoomVirtual(double zoomVirtual);

    PdfPageInfo * GetPageInfo(int pageNo) const;
    TextPage *    GetTextPage(int pageNo);

    double        GetZoomReal();

    void          GoToPage(int pageNo, int scrollY, int scrollX=-1);
    BOOL          GoToPrevPage(int scrollY);
    BOOL          GoToNextPage(int scrollY);
    BOOL          GoToFirstPage();
    BOOL          GoToLastPage();

    void          ScrollXTo(int xOff);
    void          ScrollXBy(int dx);

    void          ScrollYTo(int yOff);
    void          ScrollYBy(int dy, bool changePage);
    void          ScrollYByAreaDy(bool forward, bool changePage);

    void          EnsureSearchHitVisible();

    void          ZoomTo(double zoomVirtual);
    void          ZoomBy(double zoomFactor);

    void          RotateBy(int rotation);

    void          RenderVisibleParts();
    void          Relayout(double zoomVirtual, int rotation);
    void          RecalcVisibleParts();

    void          SetTotalDrawAreaSize(RectDSize totalDrawAreaSize);

    PdfLink *     GetLinkAtPosition(int x, int y);
    void          HandleLinkGoTo(LinkGoTo *linkGoTo);
    void          HandleLinkGoToR(LinkGoToR *linkGoToR);
    void          HandleLinkURI(LinkURI *linkURI);
    void          HandleLinkLaunch(LinkLaunch* linkLaunch);
    void          HandleLinkNamed(LinkNamed *linkNamed);
    BOOL          CanGoToNextPage();
    BOOL          CanGoToPrevPage();

    void          FindInit(int startPageNo);
    BOOL          FindNextForward();
    BOOL          FindNextBackward();

    void          ClearSearchHit();
    void          SetSearchHit(int pageNo, RectD *hitRect);

    GooString *   GetTextInRegion(int pageNo, RectD *region);

    SplashBitmap* GetBitmapForPage(int pageNo, 
        BOOL (*abortCheckCbkA)(void *data) = NULL, void *abortCheckCbkDataA = NULL);

    /* Those need to be implemented somewhere else by the GUI */
    void        SetScrollbarsState();
    /* called when a page number changes */
    void        PageChanged();
    /* called when we decide that the display needs to be redrawn */
    void        RepaintDisplay(bool delayed);

    void        ShowBusyCursor();
    void        ShowNormalCursor();
    void        CancelBackgroundRendering();

    void        FreeTextPages(void);
    void        RecalcSearchHitCanvasPos(void);
    void        RecalcLinksCanvasPos(void);
    void        SetStartPage(int startPage);
    double      ZoomRealFromFirtualForPage(double zoomVirtual, int pageNo);
    void        RecalcLinks(void);
    void        GoToDest(LinkDest *linkDest);
    void        GoToNamedDest(UGooString *dest);
    void        StartRenderingPage(int pageNo);
    void        FreeLinks(void);
    void        CvtUserToScreen(int pageNo, double *x, double *y);
    void        RectCvtUserToScreen(int pageNo, RectD *r);
    BOOL        IsPageShown(int pageNo);
    int         FindFirstVisiblePageNo(void) const;
    PdfPageInfo* FindFirstVisiblePage(void);

public:
    /* PDF document we're displaying. Owned by this structure */
    PDFDoc *        pdfDoc;

    SearchStateData searchState;

    SplashOutputDev * outputDevice;

    TextOutputDev * textOutDevice;

    /* an array of PdfPageInfo, len of array is pageCount */
    PdfPageInfo *   pagesInfo;

    /* In non-continuous mode is the first page from a PDF file that we're
       displaying.
       No meaning in continous mode. */
    int             startPage;

    /* real zoom value calculated from zoomVirtual. Same as zoomVirtual except
       for ZOOM_FIT_PAGE and ZOOM_FIT_WIDTH */
    double          zoomReal;

    /* size of scrollbars */
    int             scrollbarXDy;
    int             scrollbarYDx;

    /* size of total draw area (i.e. window size) */
    RectDSize       totalDrawAreaSize;

    /* size of virtual canvas containing all rendered pages.
       TODO: re-consider, 32 signed number should be large enough for everything. */
    RectDSize       canvasSize;

    /* total number of links */
    int             linkCount;

    /* an array of 'totalLinksCount' size, each entry describing a link */
    PdfLink *       links;

    int             searchHitPageNo;
    RectD           searchHitRectPage;
    RectI           searchHitRectCanvas;
};

/* We keep a cache of rendered bitmaps. BitmapCacheEntry keeps data
   that uniquely identifies rendered page (dm, pageNo, rotation, zoomReal)
   and corresponding rendered bitmap.
*/
typedef struct {
  DisplayModelSplash * dm;
  int            pageNo;
  int            rotation;
  double         zoomLevel;
  PlatformCachedBitmap *bitmap;
} BitmapCacheEntry;

typedef struct {
    DisplayModelSplash *  dm;
    int             pageNo;
    double          zoomLevel;
    int             rotation;
    int             abort;
} PageRenderRequest;

BOOL              ValidDisplayMode(DisplayMode dm);

DisplayModelSplash *DisplayModelSplash_CreateFromFileName(const char *fileName, void *data,
                                            RectDSize totalDrawAreaSize,
                                            int scrollbarXDy, int scrollbarYDx,
                                            DisplayMode displayMode, int startPage);

DisplayModelSplash *DisplayModelSplash_CreateFromPdfDoc(PDFDoc *pdfDoc, SplashOutputDev *outputDev,
                                            RectDSize totalDrawAreaSize,
                                            int scrollbarXDy, int scrollbarYDx,
                                            DisplayMode displayMode, int startPage);

BOOL              DisplayState_FromDisplayModel(DisplayState *ds, DisplayModel *dm);

DisplaySettings * GetGlobalDisplaySettings(void);

/* Lock protecting both bitmap cache and page render queue */
void              LockCache();
void              UnlockCache();

SplashBitmap*     RenderBitmap(DisplayModelSplash *dm,
                           int pageNo, double zoomReal, int rotation,
                           BOOL (*abortCheckCbkA)(void *data),
                           void *abortCheckCbkDataA);

void              RenderQueue_Add(DisplayModelSplash *dm, int pageNo);

BitmapCacheEntry *BitmapCache_Find(DisplayModelSplash *dm, int pageNo, double zoomLevel, int rotation);
BOOL              BitmapCache_Exists(DisplayModelSplash *dm, int pageNo, double zoomLevel, int rotation);
void              BitmapCache_Add(DisplayModelSplash *dm, int pageNo, double zoomLevel, int rotation, PlatformCachedBitmap *bitmap);
void              BitmapCache_FreeAll(void);
BOOL              BitmapCache_FreeForDisplayModel(DisplayModelSplash *dm);
BOOL              BitmapCache_FreeNotVisible(void);

#endif
