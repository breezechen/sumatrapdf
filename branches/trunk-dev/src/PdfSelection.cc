#include "PdfSelection.h"

PdfSelection::PdfSelection(PdfEngine *engine) : engine(engine)
{
    coords = (fz_bbox **)calloc(engine->pageCount(), sizeof(fz_bbox *));
    lens = (int *)calloc(engine->pageCount(), sizeof(int));

    result.len = 0;
    result.pages = NULL;
    result.rects = NULL;

    startPage = -1;
    startGlyph = -1;
}

PdfSelection::~PdfSelection()
{
    for (int i = 0; i < engine->pageCount(); i++) {
        if (coords[i]) {
            free(coords[i]);
            coords[i] = NULL;
        }
    }

    free(coords);
    free(lens);
    free(result.pages);
    free(result.rects);
}

int PdfSelection::FindClosestGlyph(int pageNo, double x, double y)
{
    assert(1 <= pageNo && pageNo <= engine->pageCount());
    if (!coords[pageNo - 1]) {
        TCHAR *text = engine->ExtractPageText(pageNo, _T(DOS_NEWLINE), &coords[pageNo - 1]);
        if (!text)
            return -1;
        lens[pageNo - 1] = lstrlen(text);
        free(text);
    }

    double maxDist = -1;
    int result = -1;
    fz_bbox *_coords = coords[pageNo - 1];

    for (int i = 0; i < lens[pageNo - 1]; i++) {
        if (!_coords[i].x0 && !_coords[i].x1)
            continue;

        double dist = _hypot(x - 0.5 * (_coords[i].x0 + _coords[i].x1),
                             y - 0.5 * (_coords[i].y0 + _coords[i].y1));
        if (maxDist < 0 || dist < maxDist) {
            result = i;
            maxDist = dist;
        }
    }

    // the result indexes the first glyph to be selected in a forward selection
    if (-1 == result || x > 0.5 * (_coords[result].x0 + _coords[result].x1))
        result++;

    assert(0 <= result && result <= lens[pageNo - 1]);
    return result;
}

void PdfSelection::FillResultRects(int pageNo, int glyph, int length)
{
    fz_bbox mediabox = fz_roundrect(engine->pageMediabox(pageNo));
    fz_bbox *c = &coords[pageNo - 1][glyph], *end = c + length;
    for (; c < end; c++) {
        // skip line breaks
        if (!c->x0 && !c->x1)
            continue;

        fz_bbox c0 = *c;
        for (; c < end && (c->x0 || c->x1); c++);
        c--;
        fz_bbox c1 = *c;
        fz_bbox bbox = fz_intersectbbox(fz_unionbbox(c0, c1), mediabox);
        // skip text that's completely outside a page's mediabox
        if (fz_isemptyrect(bbox))
            continue;

        // cut the right edge, if it overlaps the next character
        if ((c[1].x0 || c[1].x1) && bbox.x0 < c[1].x0 && bbox.x1 > c[1].x0)
            bbox.x1 = c[1].x0;

        result.len++;
        result.pages = (int *)realloc(result.pages, sizeof(int) * result.len);
        result.pages[result.len - 1] = pageNo;
        result.rects = (RectI *)realloc(result.rects, sizeof(RectI) * result.len);
        RectI_FromXY(&result.rects[result.len - 1], bbox.x0, bbox.x1, bbox.y0, bbox.y1);
    }
}

void PdfSelection::StartAt(int pageNo, double x, double y)
{
    startPage = pageNo;
    startGlyph = FindClosestGlyph(pageNo, x, y);
}

void PdfSelection::SelectUpTo(int pageNo, double x, double y)
{
    int endGlyph = FindClosestGlyph(pageNo, x, y);
    bool isForward = pageNo > startPage || pageNo == startPage && endGlyph > startGlyph;
    result.len = 0;
    int fromPage = min(pageNo, startPage), toPage = max(pageNo, startPage);
    int fromGlyph = (fromPage == pageNo ? endGlyph : startGlyph);
    int toGlyph = (fromPage == pageNo ? startGlyph : endGlyph);
    if (fromPage == toPage && fromGlyph > toGlyph)
        swap_int(&fromGlyph, &toGlyph);

    if (fromGlyph != toGlyph)
        x=x;
    for (int page = fromPage; page <= toPage; page++) {
        int glyph = page == fromPage ? fromGlyph : 0;
        int length = (page == toPage ? toGlyph : lens[page - 1]) - glyph;
        if (length > 0)
            FillResultRects(page, glyph, length);
    }
}
