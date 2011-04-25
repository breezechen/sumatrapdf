/* Copyright 2006-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include <DjVuDocument.h>
#include <DjVuImage.h>
#include <GBitmap.h>
#include "DjVuEngine.h"
#include "FileUtil.h"

// TODO: this code leaks memory!?

class RenderedDjVuBitmap : public RenderedBitmap {
public:
    RenderedDjVuBitmap(GBitmap *bitmap, HDC hDC);
};

RenderedDjVuBitmap::RenderedDjVuBitmap(GBitmap *bitmap, HDC hDC) :
    RenderedBitmap(NULL, bitmap->columns(), bitmap->rows())
{
    int w = bitmap->columns();
    int h = bitmap->rows();
    int stride = ((w + 3) / 4) * 4;
    int grays = bitmap->get_grays();

    BITMAPINFO *bmi = (BITMAPINFO *)calloc(1, sizeof(BITMAPINFOHEADER) + grays * sizeof(RGBQUAD));
    unsigned char *bmpData = (unsigned char *)calloc(stride, h);

    for (int c = 0; c < grays; c++) {
        int color = 255 * (grays - 1 - c) / (grays - 1);
        bmi->bmiColors[c].rgbBlue = bmi->bmiColors[c].rgbGreen = bmi->bmiColors[c].rgbRed = color;
    }
    for (int y = 0; y < h; y++)
        memcpy(bmpData + y * stride, bitmap->operator[](y), w);

    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = w;
    bmi->bmiHeader.biHeight = h;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biCompression = BI_RGB;
    bmi->bmiHeader.biBitCount = 8;
    bmi->bmiHeader.biSizeImage = h * stride;
    bmi->bmiHeader.biClrUsed = grays;

    _hbmp = CreateDIBitmap(hDC, &bmi->bmiHeader, CBM_INIT, bmpData, bmi, DIB_RGB_COLORS);

    free(bmi);
    free(bmpData);
}

class RenderedDjVuPixmap : public RenderedBitmap {
public:
    RenderedDjVuPixmap(GPixmap *pixmap, HDC hDC);
};

RenderedDjVuPixmap::RenderedDjVuPixmap(GPixmap *pixmap, HDC hDC) :
    RenderedBitmap(NULL, pixmap->columns(), pixmap->rows())
{
    int w = pixmap->columns();
    int h = pixmap->rows();
    int stride = ((w * 3 + 3) / 4) * 4;

    BITMAPINFO *bmi = (BITMAPINFO *)calloc(1, sizeof(BITMAPINFOHEADER));
    unsigned char *bmpData = (unsigned char *)calloc(stride, h);

    for (int y = 0; y < h; y++) {
        GPixel *row = pixmap->operator[](y);
        for (int x = 0; x < w; x++) {
            bmpData[y * stride + x * 3 + 0] = row[x].r;
            bmpData[y * stride + x * 3 + 1] = row[x].b;
            bmpData[y * stride + x * 3 + 2] = row[x].g;
        }
    }

    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = w;
    bmi->bmiHeader.biHeight = h;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biCompression = BI_RGB;
    bmi->bmiHeader.biBitCount = 24;
    bmi->bmiHeader.biSizeImage = h * stride;
    bmi->bmiHeader.biClrUsed = 0;

    _hbmp = CreateDIBitmap(hDC, &bmi->bmiHeader, CBM_INIT, bmpData, bmi, DIB_RGB_COLORS);

    free(bmi);
    free(bmpData);
}

class CDjVuEngine : public DjVuEngine {
    friend DjVuEngine;

public:
    CDjVuEngine();
    virtual ~CDjVuEngine();
    virtual DjVuEngine *Clone() {
        return CreateFromFileName(fileName);
    }

    virtual const TCHAR *FileName() const { return fileName; };
    virtual int PageCount() const {
        if (!pages)
            return 0;
        return doc->get_pages_num();
    }

    virtual RectD PageMediabox(int pageNo) {
        assert(1 <= pageNo && pageNo <= PageCount());
        return RectD(0, 0, pages[pageNo-1]->get_real_width(), pages[pageNo-1]->get_real_height());
    }

    virtual RenderedBitmap *RenderBitmap(int pageNo, float zoom, int rotation,
                         RectD *pageRect=NULL, /* if NULL: defaults to the page's mediabox */
                         RenderTarget target=Target_View, bool useGdi=false);
    virtual bool RenderPage(HDC hDC, int pageNo, RectI screenRect,
                         float zoom=0, int rotation=0,
                         RectD *pageRect=NULL, RenderTarget target=Target_View);

    virtual PointD Transform(PointD pt, int pageNo, float zoom, int rotate, bool inverse=false);
    virtual RectD Transform(RectD rect, int pageNo, float zoom, int rotate, bool inverse=false);

    virtual unsigned char *GetFileData(size_t *cbCount);
    virtual bool HasTextContent() { return false; }
    virtual TCHAR * ExtractPageText(int pageNo, TCHAR *lineSep, RectI **coords_out=NULL,
                                    RenderTarget target=Target_View) { return NULL; }
    virtual bool IsImagePage(int pageNo) { return true; }
    virtual PageLayoutType PreferredLayout() { return Layout_Single; }

    virtual float GetFileDPI() const {
        if (pages && PageCount() > 0)
            return (float)pages[0]->get_dpi();
        return 300.0f;
    }
    virtual const TCHAR *GetDefaultFileExt() const { return _T(".djvu"); }

    // we currently don't load pages lazily, so there's nothing to do here
    virtual bool BenchLoadPage(int pageNo) { return true; }

protected:
    const TCHAR *fileName;
    GP<DjVuDocument> doc;
    GP<DjVuImage> *pages;

    CRITICAL_SECTION pagesAccess;

    void GetTransform(Gdiplus::Matrix& m, int pageNo, float zoom, int rotate);
    bool Load(const TCHAR *fileName);
};

CDjVuEngine::CDjVuEngine() : fileName(NULL), pages(NULL)
{
    InitializeCriticalSection(&pagesAccess);
}

CDjVuEngine::~CDjVuEngine()
{
    EnterCriticalSection(&pagesAccess);

    delete[] pages;
    free((void *)fileName);

    LeaveCriticalSection(&pagesAccess);
    DeleteCriticalSection(&pagesAccess);
}

bool CDjVuEngine::Load(const TCHAR *fileName)
{
    this->fileName = Str::Dup(fileName);

    ScopedMem<char> fileNameUtf8(Str::Conv::ToUtf8(fileName));
    GURL::Filename::UTF8 gurl(fileNameUtf8.Get());
    doc = DjVuDocument::create_wait(gurl);
    if (!doc->wait_for_complete_init() || !doc->is_init_ok())
        return false;

    // TODO: load the pages lazily
    pages = new GP<DjVuImage>[doc->get_pages_num()];
    for (int i = 0; i < doc->get_pages_num(); i++)
        pages[i] = doc->get_page(i);

    return true;
}

RenderedBitmap *CDjVuEngine::RenderBitmap(int pageNo, float zoom, int rotation, RectD *pageRect, RenderTarget target, bool useGdi)
{
    ScopedCritSec scope(&pagesAccess);

    RectD pageRc = pageRect ? *pageRect : PageMediabox(pageNo);
    RectI screen = Transform(pageRc, pageNo, zoom, rotation).Round();
    RectI full = Transform(PageMediabox(pageNo), pageNo, zoom, rotation).Round();
    screen = full.Intersect(screen);
    int rotation4 = (((rotation / 90) % 4) + 4) % 4;
    pages[pageNo-1]->set_rotate(rotation4);
    if (!pages[pageNo-1]->wait_for_complete_decode())
        return NULL;

    GRect all(full.x, full.y, full.dx, full.dy);
    GRect rect(screen.x, full.y + full.y - screen.y + full.dy - screen.dy, screen.dx, screen.dy);
#if 0
    // TODO: we're supposed to call get_pixmap before get_bitmap,
    //       but this leads to heap corruption!?
    GP<GPixmap> pix = pages[pageNo-1]->get_pixmap(rect, all);
    if (pix) {
        HDC hDC = GetDC(NULL);
        RenderedBitmap *bmp = new RenderedDjVuPixmap(pix, hDC);
        ReleaseDC(NULL, hDC);
        return bmp;
    }
#endif
    GP<GBitmap> gray = pages[pageNo-1]->get_bitmap(rect, all);
    if (!gray)
        return NULL;

    HDC hDC = GetDC(NULL);
    RenderedBitmap *bmp = new RenderedDjVuBitmap(gray, hDC);
    ReleaseDC(NULL, hDC);
    return bmp;
}

bool CDjVuEngine::RenderPage(HDC hDC, int pageNo, RectI screenRect, float zoom, int rotation, RectD *pageRect, RenderTarget target)
{
    RenderedBitmap *bmp = RenderBitmap(pageNo, zoom, rotation, pageRect, target);
    if (!bmp)
        return false;
    bmp->StretchDIBits(hDC, screenRect);
    delete bmp;
    return true;
}

void CDjVuEngine::GetTransform(Gdiplus::Matrix& m, int pageNo, float zoom, int rotate)
{
    SizeD size = PageMediabox(pageNo).Size();

    rotate = rotate % 360;
    if (rotate < 0) rotate = rotate + 360;
    if (90 == rotate)
        m.Translate(0, (float)-size.dy, Gdiplus::MatrixOrderAppend);
    else if (180 == rotate)
        m.Translate((float)-size.dx, (float)-size.dy, Gdiplus::MatrixOrderAppend);
    else if (270 == rotate)
        m.Translate((float)-size.dx, 0, Gdiplus::MatrixOrderAppend);
    else // if (0 == rotate)
        m.Translate(0, 0, Gdiplus::MatrixOrderAppend);

    m.Scale(zoom, zoom, Gdiplus::MatrixOrderAppend);
    m.Rotate((float)rotate, Gdiplus::MatrixOrderAppend);
}

PointD CDjVuEngine::Transform(PointD pt, int pageNo, float zoom, int rotate, bool inverse)
{
    RectD rect = Transform(RectD(pt, SizeD()), pageNo, zoom, rotate, inverse);
    return PointD(rect.x, rect.y);
}

RectD CDjVuEngine::Transform(RectD rect, int pageNo, float zoom, int rotate, bool inverse)
{
    Gdiplus::PointF pts[2] = {
        Gdiplus::PointF((float)rect.x, (float)rect.y),
        Gdiplus::PointF((float)(rect.x + rect.dx), (float)(rect.y + rect.dy))
    };
    Gdiplus::Matrix m;
    GetTransform(m, pageNo, zoom, rotate);
    if (inverse)
        m.Invert();
    m.TransformPoints(pts, 2);
    return RectD::FromXY(pts[0].X, pts[0].Y, pts[1].X, pts[1].Y);
}

unsigned char *CDjVuEngine::GetFileData(size_t *cbCount)
{
    return (unsigned char *)File::ReadAll(fileName, cbCount);
}

DjVuEngine *DjVuEngine::CreateFromFileName(const TCHAR *fileName)
{
    CDjVuEngine *engine = new CDjVuEngine();
    if (!engine->Load(fileName)) {
        delete engine;
        return NULL;
    }
    return engine;    
}
