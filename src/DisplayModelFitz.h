#ifndef _DISPLAY_MODEL_FITZ_H_
#define _DISPLAY_MODEL_FITZ_H_

#include "DisplayModel.h"

class DisplayModelFitz : public DisplayModel
{
public:
    DisplayModelFitz(DisplayMode displayMode);
    virtual ~DisplayModelFitz();

    PdfEngineFitz *pdfEngineFitz(void) { return (PdfEngineFitz*)_pdfEngine; }
    virtual void   handleLink(PdfLink *pdfLink);

protected:
    virtual void cvtUserToScreen(int pageNo, double *x, double *y);
};

DisplayModelFitz *DisplayModelFitz_CreateFromFileName(
  const char *fileName,
  SizeD totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage,
  WindowInfo *win);

#endif
