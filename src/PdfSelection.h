#ifndef _PDF_SELECTION_H
#define _PDF_SELECTION_H

#include "PdfEngine.h"

typedef struct {
    int len;
    int *pages;
    RectI *rects;
} PdfSel;

class PdfSelection
{
public:
    PdfSelection(PdfEngine *engine);
    ~PdfSelection();

    void StartAt(int pageNo, int glyphIx);
    void StartAt(int pageNo, double x, double y) {
        StartAt(pageNo, FindClosestGlyph(pageNo, x, y));
    }
    void SelectUpTo(int pageNo, int glyphIx);
    void SelectUpTo(int pageNo, double x, double y) {
        SelectUpTo(pageNo, FindClosestGlyph(pageNo, x, y));
    }
    void Reset();

    PdfSel result;

protected:
    PdfEngine * engine;
    fz_bbox  ** coords;
    int       * lens;

    int         startPage;
    int         startGlyph;

    int FindClosestGlyph(int pageNo, double x, double y);
    void FillResultRects(int pageNo, int glyph, int length);
};

#endif // _PDF_SELECTION_H
