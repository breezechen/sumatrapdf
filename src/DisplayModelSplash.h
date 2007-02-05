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

/* It seems that PDF documents are encoded assuming DPI of 72.0 */
#define PDF_FILE_DPI        72

/* arbitrary but big */
#define INVALID_BIG_ZOOM    999999.0

/* Information needed to drive the display of a given PDF document on a screen.
   You can think of it as a model in the MVC pardigm.
   All the display changes should be done through changing this model via
   API and re-displaying things based on new display information */
class DisplayModelSplash : public DisplayModel
{
public:
    DisplayModelSplash(DisplayMode displayMode);
    virtual ~DisplayModelSplash();

    virtual void  setDisplayMode(DisplayMode displayMode);
    virtual int   currentPageNo(void) const;
    virtual void  setZoomVirtual(double zoomVirtual);

    PdfEnginePoppler * pdfEnginePoppler() { return (PdfEnginePoppler*)pdfEngine(); }

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
    PDFDoc *            pdfDoc;
    SplashOutputDev *   outputDevice;
    TextOutputDev *     textOutDevice;

    /* In non-continuous mode is the first page from a PDF file that we're
       displaying.
       No meaning in continous mode. */
    int             startPage;

    /* real zoom value calculated from zoomVirtual. Same as zoomVirtual except
       for ZOOM_FIT_PAGE and ZOOM_FIT_WIDTH */
    double          zoomReal;

    /* size of virtual canvas containing all rendered pages.
       TODO: re-consider, 32 signed number should be large enough for everything. */
    RectDSize       canvasSize;

    /* total number of links */
    int             linkCount;

    /* an array of 'totalLinksCount' size, each entry describing a link */
    PdfLink *       links;
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
  double         renderTime;
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
void              BitmapCache_Add(DisplayModelSplash *dm, int pageNo, double zoomLevel, int rotation, 
                                  PlatformCachedBitmap *bitmap, double renderTime);
void              BitmapCache_FreeAll(void);
BOOL              BitmapCache_FreeForDisplayModel(DisplayModelSplash *dm);
BOOL              BitmapCache_FreeNotVisible(void);

void SplashColorsInit(void);

#endif
