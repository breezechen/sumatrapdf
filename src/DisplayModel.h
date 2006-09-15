#ifndef _DISPLAY_MODEL_H_
#define _DISPLAY_MODEL_H_
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

#include "SimpleRect.h"
#include "BaseUtils.h"
#include "DisplayState.h"

class SplashBitmap;
class PDFDoc;
class SplashOutputDev;
class Link;
class Links;
class LinkGoTo;
class LinkGoToR;
class LinkURI;
class LinkLaunch;
class LinkNamed;

/* must be implemented somewhere else */
extern void LaunchBrowser(const char *uri);

/* It seems that PDF documents are encoded assuming DPI of 72.0 */
#define PDF_FILE_DPI        72

#define INVALID_ZOOM        -99
/* arbitrary but big */
#define INVALID_BIG_ZOOM    999999.0

#define INVALID_PAGE        -1

#define INVALID_ROTATION    -1

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
    SimpleRect      rectCanvas; /* position of the link on canvas */
} PdfLink;

/* A special "pointer" value indicating that this bitmap is being rendered
   on a separate thread */
#define BITMAP_BEING_RENDERED (SplashBitmap*)-1
/* A special "pointer" vlaue indicating that we tried to render this bitmap
   but couldn't (e.g. due to lack of memory) */
#define BITMAP_CANNOT_RENDER (SplashBitmap*)-2

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

    /* a bitmap representing the whole page. Should be of (currDx,currDy) size */
    SplashBitmap *  bitmap;

    Links *         links;
} PdfPageInfo;

/* Information needed to drive the display of a given PDF document on a screen.
   You can think of it as a model in the MVC pardigm.
   All the display changes should be done through changing this model via
   API and re-displaying things based on new display information */
typedef struct DisplayModel {
    /* an arbitrary pointer that can be used by an app e.g. a multi-window GUI
       could link this to a data describing window displaying  this document */
    void *          appData;

    /* PDF document we're displaying. Owned by this structure */
    PDFDoc *        pdfDoc;

    SplashOutputDev * outputDevice;
    /* number of pages in PDF document. A cache of pdfDoc->getNumPages() */
    int             pageCount;
    /* an array of PdfPageInfo, len of array is pageCount */
    PdfPageInfo *   pagesInfo;

    DisplayMode     displayMode;

    BOOL            fullScreen;

    /* In non-continuous mode is the first page from a PDF file that we're
       displaying.
       No meaning in continous mode. */
    int             startPage;

    /* current rotation selected by user */
    int             rotation;

    /* a "virtual" zoom level. Can be either a real zoom level in percent
       (i.e. 100.0 is original size) or one of virtual values ZOOM_FIT_PAGE
       or ZOOM_FIT_WIDTH, whose real value depends on draw area size */
    double          zoomVirtual;
    /* real zoom value calculated from zoomVirtual. Same as zoomVirtual except
       for ZOOM_FIT_PAGE and ZOOM_FIT_WIDTH */
    double          zoomReal;

    /* size of scrollbars */
    int             scrollbarXDy;
    int             scrollbarYDx;

    /* size of total draw area (i.e. window size) */
    RectDSize       totalDrawAreaSize;

    /* size of draw area i.e. totalDrawAreaSize minus scrollbarsSize (if
       they're shown) */
    RectDSize       drawAreaSize;

    /* size of virtual canvas containing all rendered pages.
       TODO: re-consider, 32 signed number should be large enough for everything. */
    RectDSize       canvasSize;

    /* areaOffset is "polymorphic". If drawAreaSize.dx > totalAreSize.dx then
       areaOffset.x is offset of total area rect inside draw area, otherwise
       an offset of draw area inside total area.
       The same for areaOff.y, except it's for dy */
    RectDPos        areaOffset;

    /* total number of links */
    int             linkCount;

    /* an array of 'totalLinksCount' size, each entry describing a link */
    PdfLink *       links;

    /* if TRUE, we're in debug mode where we show links as blue rectangle on
       the screen. Makes debugging code related to links easier. */
    int             debugShowLinks;
} DisplayModel;


typedef struct {
    DisplayModel *  dm;
    int             pageNo;
    int             abort;
} PageRenderRequest;

BOOL          IsAllocatedBitmap(SplashBitmap *bmp);

DisplaySettings *DisplayModel_GetGlobalDisplaySettings(void);

BOOL          ValidDisplayMode(DisplayMode dm);

DisplayModel *DisplayModel_CreateFromPdfDoc(PDFDoc *pdfDoc, SplashOutputDev *outputDev,
                                            RectDSize totalDrawAreaSize,
                                            int scrollbarXDy, int scrollbarYDx,
                                            DisplayMode displayMode, int startPage);
void          DisplayModel_Delete(DisplayModel *dm);

PdfPageInfo * DisplayModel_GetPageInfo(DisplayModel *dm, int pageNo);

bool          DisplayModel_ValidPageNo(DisplayModel *dm, int pageNo);
int           DisplayModel_GetCurrentPageNo(DisplayModel *dm);
double        DisplayModel_GetZoomReal(DisplayModel *dm);
double        DisplayModel_GetZoomVirtual(DisplayModel *dm);
int           DisplayModel_GetRotation(DisplayModel *dm);

void          DisplayModel_GoToPage(DisplayModel *dm, int pageNo, int scrollY, int scrollX=-1);
bool          DisplayModel_GoToPrevPage(DisplayModel *dm, int scrollY);
bool          DisplayModel_GoToNextPage(DisplayModel *dm, int scrollY);
bool          DisplayModel_GoToFirstPage(DisplayModel *dm);
bool          DisplayModel_GoToLastPage(DisplayModel *dm);

void          DisplayModel_ScrollXTo(DisplayModel *dm, int xOff);
void          DisplayModel_ScrollXBy(DisplayModel *dm, int dx);

void          DisplayModel_ScrollYTo(DisplayModel *dm, int yOff);
void          DisplayModel_ScrollYBy(DisplayModel *dm, int dy, bool changePage);
void          DisplayModel_ScrollYByAreaDy(DisplayModel *dm, bool forward, bool changePage);

void          DisplayModel_SetDisplayMode(DisplayModel *dm, DisplayMode displayMode);

void          DisplayModel_FreeBitmaps(DisplayModel *dm);

void          DisplayModel_SetZoomVirtual(DisplayModel *dm, double zoomVirtual);
void          DisplayModel_ZoomTo(DisplayModel *dm, double zoomVirtual);
void          DisplayModel_ZoomBy(DisplayModel *dm, double zoomFactor);

void          DisplayModel_RotateBy(DisplayModel *dm, int rotation);

void          DisplayModel_RenderVisibleParts(DisplayModel *dm);
void          DisplayModel_Relayout(DisplayModel *dm, double zoomVirtual, int rotation);
void          DisplayModel_RecalcVisibleParts(DisplayModel *dm);

void          DisplayModel_SetTotalDrawAreaSize(DisplayModel *dm, RectDSize totalDrawAreaSize);

PdfLink *     DisplayModel_GetLinkAtPosition(DisplayModel *dm, int x, int y);
void          DisplayModel_HandleLinkGoTo(DisplayModel *dm, LinkGoTo *linkGoTo);
void          DisplayModel_HandleLinkGoToR(DisplayModel *dm, LinkGoToR *linkGoToR);
void          DisplayModel_HandleLinkURI(DisplayModel *dm, LinkURI *linkURI);
void          DisplayModel_HandleLinkLaunch(DisplayModel *dm, LinkLaunch* linkLaunch);
void          DisplayModel_HandleLinkNamed(DisplayModel *dm, LinkNamed *linkNamed);

BOOL          DisplayState_FromDisplayModel(DisplayState *ds, struct DisplayModel *dm);
SplashBitmap* DisplayModel_GetBitmapForPage(DisplayModel *dm, int pageNo, 
    BOOL (*abortCheckCbkA)(void *data) = NULL,
    void *abortCheckCbkDataA = NULL);

/* Those need to be implemented somewhere else by the GUI */
extern void DisplayModel_SetScrollbarsState(DisplayModel *dm);
/* called when a page number changes */
extern void DisplayModel_PageChanged(DisplayModel *dm, int currPageNo);
/* called when we decide that the display needs to be redrawn */
extern void DisplayModel_RepaintDisplay(DisplayModel *dm);

#endif
