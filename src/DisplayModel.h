#ifndef _DISPLAY_MODEL_H_
#define _DISPLAY_MODEL_H_

#include "BaseUtils.h"
#include "SimpleRect.h"

#define INVALID_PAGE        -1
#define INVALID_ROTATION    -1
#define INVALID_ZOOM        -99

enum DisplayMode {
    DM_FIRST = 1,
    DM_SINGLE_PAGE = DM_FIRST,
    DM_FACING,
    DM_CONTINUOUS,
    DM_CONTINUOUS_FACING,
    DM_LAST = DM_CONTINUOUS_FACING
};

bool IsDisplayModeContinuous(DisplayMode displayMode);

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

    /* areaOffset is "polymorphic". If drawAreaSize.dx > totalAreSize.dx then
       areaOffset.x is offset of total area rect inside draw area, otherwise
       an offset of draw area inside total area.
       The same for areaOff.y, except it's for dy */
    RectDPos        areaOffset;

    /* size of draw area i.e. totalDrawAreaSize minus scrollbarsSize (if
       they're shown) */
    RectDSize       drawAreaSize;

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

#endif
