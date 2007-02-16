#ifndef _DISPLAY_MODEL_FITZ_H_
#define _DISPLAY_MODEL_FITZ_H_

#include "DisplayModel.h"

class RenderedBitmapFitz : public PlatformRenderedBitmap {
public:
    RenderedBitmapFitz() {}
    virtual ~RenderedBitmapFitz() {}
    virtual HBITMAP CreateDIBitmap(HDC hdc);
protected:
};

class DisplayModelFitz : public DisplayModel
{
public:
    DisplayModelFitz(DisplayMode displayMode);
    virtual ~DisplayModelFitz();

protected:
    virtual void cvtUserToScreen(int pageNo, double *x, double *y);
    virtual PlatformRenderedBitmap *renderBitmap(
                           int pageNo, double zoomReal, int rotation,
                           BOOL (*abortCheckCbkA)(void *data),
                           void *abortCheckCbkDataA);
};

DisplayModelFitz *DisplayModelFitz_CreateFromFileName(
  const char *fileName, void *data,
  SizeD totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage);

#endif
