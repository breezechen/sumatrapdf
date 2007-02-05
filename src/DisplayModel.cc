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
    _fileName = NULL;
    _pageCount = 0;
    _rotation = INVALID_ROTATION;
    _zoomVirtual = INVALID_ZOOM;
    _fullScreen = false;
    _startPage = INVALID_PAGE_NO;
    _appData = NULL;
}

DisplayModel::~DisplayModel()
{
    free((void*)_fileName);
}
