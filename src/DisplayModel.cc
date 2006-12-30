#include "DisplayModel.h"
#include <assert.h>
#include <stdlib.h>

bool IsDisplayModeContinuous(DisplayMode displayMode)
{
    if ((DM_SINGLE_PAGE == displayMode) || (DM_FACING == displayMode))
        return false;
    else if ((DM_CONTINUOUS == displayMode) || (DM_CONTINUOUS_FACING == displayMode))
        return true;
    assert(0);
    return false;
}

DisplayModel::DisplayModel()
{
    _fileName = NULL;
    _pageCount = 0;
    _rotation = INVALID_ROTATION;
    _zoomVirtual = INVALID_ZOOM;
    _fullScreen = false;
    _startPage = INVALID_PAGE;
    _appData = NULL;
}

DisplayModel::~DisplayModel()
{
    free((void*)_fileName);
}
