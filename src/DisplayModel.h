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

class SplashBitmap;
class PDFDoc;
class SplashOutputDev;

/* how many pixels between pages in continous mode */
#define PAGES_SPACE_DY      3

#define PDF_FILE_DPI        72

#define ZOOM_MAX            1600.0  /* max zoom in % */
#define ZOOM_MIN            10.0    /* min zoom in % */

#define ZOOM_FIT_PAGE       -1
#define ZOOM_FIT_WIDTH      -2
#define INVALID_ZOOM        -99
/* arbitrary but big */
#define INVALID_BIG_ZOOM    999999.0

#define INVALID_PAGE        -1

#define INVALID_ROTATION    -1

/* the distance between a page and left border, in pixels */
#define PADDING_PAGE_BORDER_LEFT  4
/* the distance between a page and right border, in pixels */
#define PADDING_PAGE_BORDER_RIGHT 4
/* the distance between pages, in pixels. Only applicable if
   pagesAtATime > 1 */
#define PADDING_PAGE_PAGE         8

typedef struct PdfPageInfo {
    /* data that is constant for a given page. page size and rotation
       recorded in PDF file */
    double          pageDx;
    double          pageDy;
    int             rotation;

    /* data that needs to be set before DisplayModel_RecalcPagesInfo().
       Determines whether a given page should be shown on the screen. */
    bool            shown;

    /* data that changes when zoom and rotation changes */
    /* position and size within total area after applying zoom and rotation.
       Represents display rectangle for a given page.
       Calculated in DisplayModel_RecalcPagesInfo() */

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

    int             continuousMode;
    int             pagesAtATime;

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

} DisplayModel;

bool          ValidZoomVirtual(double zoomVirtual);

DisplayModel *DisplayModel_CreateFromPdfDoc(PDFDoc *pdfDoc, SplashOutputDev *outputDev,
                                            RectDSize totalDrawAreaSize,
                                            int scrollbarXDy, int scrollbarYDx,
                                            int continuousMode, int pagesAtATime, int startPage);
void          DisplayModel_Delete(DisplayModel *dm);

PdfPageInfo * DisplayModel_GetPageInfo(DisplayModel *dm, int pageNo);

int           DisplayModel_GetCurrentPageNo(DisplayModel *dm);

void          DisplayModel_GoToPage(DisplayModel *dm, int pageNo, int scrollY);
bool          DisplayModel_GoToPrevPage(DisplayModel *dm, int scrollY);
bool          DisplayModel_GoToNextPage(DisplayModel *dm, int scrollY);
bool          DisplayModel_GoToFirstPage(DisplayModel *dm);
bool          DisplayModel_GoToLastPage(DisplayModel *dm);

int           DisplayModel_GetSinglePageDy(DisplayModel *dm);

void          DisplayModel_ScrollXTo(DisplayModel *dm, int xOff);
void          DisplayModel_ScrollXBy(DisplayModel *dm, int dx);

void          DisplayModel_ScrollYTo(DisplayModel *dm, int yOff);
void          DisplayModel_ScrollYBy(DisplayModel *dm, int dy, bool changePage);
void          DisplayModel_ScrollYByAreaDy(DisplayModel *dm, bool forward, bool changePage);

void          DisplayModel_SetLayout(DisplayModel *dm, int continuousMode, int pagesAtATime);

/* TODO: remove those 2 */
void          DisplayModel_SwitchToSinglePage(DisplayModel *dm);
void          DisplayModel_SwitchToContinuous(DisplayModel *dm);

void          DisplayModel_ToggleContinuous(DisplayModel *dm);

void          DisplayModel_FreeBitmaps(DisplayModel *dm);

void          DisplayModel_SetZoomVirtual(DisplayModel *dm, double zoomVirtual);
void          DisplayModel_ZoomTo(DisplayModel *dm, double zoomVirtual);
void          DisplayModel_ZoomBy(DisplayModel *dm, double zoomFactor);

void          DisplayModel_RotateBy(DisplayModel *dm, int rotation);

void          DisplayModel_RenderVisibleParts(DisplayModel *dm);
void          DisplayModel_RecalcPagesInfo(DisplayModel *dm, double zoomVirtual, int rotation);
void          DisplayModel_RecalcVisibleParts(DisplayModel *dm);

void          DisplayModel_SetTotalDrawAreaSize(DisplayModel *dm, RectDSize totalDrawAreaSize);

/* Those need to be implemented somewhere else by the GUI */
extern void DisplayModel_SetScrollbarsState(DisplayModel *dm);
/* called when a page number changes */
extern void DisplayModel_PageChanged(DisplayModel *dm, int currPageNo);
/* called when we decide that the display needs to be redrawn */
extern void DisplayModel_RepaintDisplay(DisplayModel *dm);

#endif
