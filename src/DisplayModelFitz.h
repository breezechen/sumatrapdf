#ifndef _DISPLAY_MODEL_FITZ_H_
#define _DISPLAY_MODEL_FITZ_H_

#include "DisplayModel.h"

class DisplayModelFitz : public DisplayModel
{
public:
    DisplayModelFitz(DisplayMode displayMode);
    virtual ~DisplayModelFitz();

    virtual void  setDisplayMode(DisplayMode displayMode);
    virtual int   currentPageNo(void) const;
    virtual void  setZoomVirtual(double zoomVirtual);
};

DisplayModelFitz *DisplayModelFitz_CreateFromFileName(
  const char *fileName, void *data,
  RectDSize totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage);

#endif
