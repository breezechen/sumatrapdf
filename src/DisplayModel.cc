#include "DisplayModel.h"
#include <assert.h>
#include <stdlib.h>

DisplaySettings gDisplaySettings = {
  PADDING_PAGE_BORDER_TOP_DEF,
  PADDING_PAGE_BORDER_BOTTOM_DEF,
  PADDING_PAGE_BORDER_LEFT_DEF,
  PADDING_PAGE_BORDER_RIGHT_DEF,
  PADDING_BETWEEN_PAGES_X_DEF,
  PADDING_BETWEEN_PAGES_Y_DEF
};

bool IsDisplayModeContinuous(DisplayMode displayMode)
{
    if ((DM_SINGLE_PAGE == displayMode) || (DM_FACING == displayMode))
        return false;
    else if ((DM_CONTINUOUS == displayMode) || (DM_CONTINUOUS_FACING == displayMode))
        return true;
    assert(0);
    return false;
}

DisplaySettings *GetGlobalDisplaySettings(void)
{
    return &gDisplaySettings;
}

DisplayModel::DisplayModel()
{
    _rotation = INVALID_ROTATION;
    _zoomVirtual = INVALID_ZOOM;
    _fullScreen = false;
    _startPage = INVALID_PAGE_NO;
    _appData = NULL;
    _pdfEngine = NULL;
    pagesInfo = NULL;
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

bool DisplayModel::load(const char *fileName) 
{ 
    if (!_pdfEngine->load(fileName))
        return false;
    if (!allocatePagesInfo())
        return false;
    return true;
}

bool DisplayModel::allocatePagesInfo()
{
    assert(!pagesInfo);
    pagesInfo = (PdfPageInfo*)calloc(1, pageCount() * sizeof(PdfPageInfo));
    if (!pagesInfo)
        return false;
    return true;
}

