#include "PdfEngine.h"

#include <assert.h>

#include "ErrorCodes.h"
#include "GooString.h"
#include "GooList.h"
#include "GlobalParams.h"
#include "SplashBitmap.h"
#include "Object.h" /* must be included before SplashOutputDev.h because of sloppiness in SplashOutputDev.h */
#include "SplashOutputDev.h"
#include "TextOutputDev.h"
#include "PDFDoc.h"
#include "SecurityHandler.h"
#include "Link.h"

static SplashColorMode gSplashColorMode = splashModeBGR8;

static SplashColor splashColRed;
static SplashColor splashColGreen;
static SplashColor splashColBlue;
static SplashColor splashColWhite;
static SplashColor splashColBlack;

#define SPLASH_COL_RED_PTR (SplashColorPtr)&(splashColRed[0])
#define SPLASH_COL_GREEN_PTR (SplashColorPtr)&(splashColGreen[0])
#define SPLASH_COL_BLUE_PTR (SplashColorPtr)&(splashColBlue[0])
#define SPLASH_COL_WHITE_PTR (SplashColorPtr)&(splashColWhite[0])
#define SPLASH_COL_BLACK_PTR (SplashColorPtr)&(splashColBlack[0])

static SplashColorPtr  gBgColor = SPLASH_COL_WHITE_PTR;

static void SplashColorSet(SplashColorPtr col, Guchar red, Guchar green, Guchar blue, Guchar alpha)
{
    switch (gSplashColorMode)
    {
        case splashModeBGR8:
            col[0] = blue;
            col[1] = green;
            col[2] = red;
            break;
        case splashModeRGB8:
            col[0] = red;
            col[1] = green;
            col[2] = blue;
            break;
        default:
            assert(0);
            break;
    }
}

void SplashColorsInit(void)
{
    SplashColorSet(SPLASH_COL_RED_PTR, 0xff, 0, 0, 0);
    SplashColorSet(SPLASH_COL_GREEN_PTR, 0, 0xff, 0, 0);
    SplashColorSet(SPLASH_COL_BLUE_PTR, 0, 0, 0xff, 0);
    SplashColorSet(SPLASH_COL_BLACK_PTR, 0, 0, 0, 0);
    SplashColorSet(SPLASH_COL_WHITE_PTR, 0xff, 0xff, 0xff, 0);
}

RenderedBitmapFitz::RenderedBitmapFitz(fz_pixmap *image)
{
    _image = image;
}

RenderedBitmapFitz::~RenderedBitmapFitz()
{
    if (_image)
        fz_droppixmap(_image);
}

HBITMAP RenderedBitmapFitz::createDIBitmap(HDC hdc)
{
    int bmpDx = _image->w;
    int bmpDy = _image->h;
    int bmpRowSize = ((_image->w * 3 + 3) / 4) * 4;

    BITMAPINFOHEADER bmih;
    bmih.biSize = sizeof(bmih);
    bmih.biHeight = -bmpDy;
    bmih.biWidth = bmpDx;
    bmih.biPlanes = 1;
    bmih.biBitCount = 24;
    bmih.biCompression = BI_RGB;
    bmih.biSizeImage = bmpDy * bmpRowSize;;
    bmih.biXPelsPerMeter = bmih.biYPelsPerMeter = 0;
    bmih.biClrUsed = bmih.biClrImportant = 0;

#ifdef FITZ_HEAD
    unsigned char* bmpData = _image->p;
#else
    unsigned char* bmpData = _image->samples;
#endif
    HBITMAP hbmp = ::CreateDIBitmap(hdc, &bmih, CBM_INIT, bmpData, (BITMAPINFO *)&bmih , DIB_RGB_COLORS);
    return hbmp;
}

void RenderedBitmapFitz::stretchDIBits(HDC hdc, int leftMargin, int topMargin, int pageDx, int pageDy)
{
    int bmpDx = _image->w;
    int bmpDy = _image->h;
    int bmpRowSize = ((_image->w * 3 + 3) / 4) * 4;

    BITMAPINFOHEADER bmih;
    bmih.biSize = sizeof(bmih);
    bmih.biHeight = -bmpDy;
    bmih.biWidth = bmpDx;
    bmih.biPlanes = 1;
    // we could create this dibsection in monochrome
    // if the printer is monochrome, to reduce memory consumption
    // but fitz is currently setup to return a full colour bitmap
    bmih.biBitCount = 24;
    bmih.biCompression = BI_RGB;
    bmih.biSizeImage = bmpDy * bmpRowSize;;
    bmih.biXPelsPerMeter = bmih.biYPelsPerMeter = 0;
    bmih.biClrUsed = bmih.biClrImportant = 0;

#ifdef FITZ_HEAD
    unsigned char* bmpData = _image->p;
#else
    unsigned char* bmpData = _image->samples;
#endif
    ::StretchDIBits(hdc,
        // destination rectangle
        -leftMargin, -topMargin, pageDx, pageDy,
        // source rectangle
        0, 0, bmpDx, bmpDy,
        bmpData,
        (BITMAPINFO *)&bmih ,
        DIB_RGB_COLORS,
        SRCCOPY);
}

RenderedBitmapSplash::RenderedBitmapSplash(SplashBitmap *bitmap)
{
    _bitmap = bitmap;
}

RenderedBitmapSplash::~RenderedBitmapSplash() {
    delete _bitmap;
}

HBITMAP RenderedBitmapSplash::createDIBitmap(HDC hdc)
{
    int bmpDx = _bitmap->getWidth();
    int bmpDy = _bitmap->getHeight();
    int bmpRowSize = _bitmap->getRowSize();

    BITMAPINFOHEADER bmih;
    bmih.biSize = sizeof(bmih);
    bmih.biHeight = -bmpDy;
    bmih.biWidth = bmpDx;
    bmih.biPlanes = 1;
    bmih.biBitCount = 24;
    bmih.biCompression = BI_RGB;
    bmih.biSizeImage = bmpDy * bmpRowSize;;
    bmih.biXPelsPerMeter = bmih.biYPelsPerMeter = 0;
    bmih.biClrUsed = bmih.biClrImportant = 0;

    SplashColorPtr bmpData = _bitmap->getDataPtr();
    HBITMAP hbmp = ::CreateDIBitmap(hdc, &bmih, CBM_INIT, bmpData, (BITMAPINFO *)&bmih , DIB_RGB_COLORS);
    return hbmp;
}

void RenderedBitmapSplash::stretchDIBits(HDC hdc, int leftMargin, int topMargin, int pageDx, int pageDy)
{
    int bmpDx = _bitmap->getWidth();
    int bmpDy = _bitmap->getHeight();
    int bmpRowSize = _bitmap->getRowSize();

    BITMAPINFOHEADER bmih;
    bmih.biSize = sizeof(bmih);
    bmih.biHeight = -bmpDy;
    bmih.biWidth = bmpDx;
    bmih.biPlanes = 1;
    // we could create this dibsection in monochrome
    // if the printer is monochrome, to reduce memory consumption
    // but splash is currently setup to return a full colour bitmap
    bmih.biBitCount = 24;
    bmih.biCompression = BI_RGB;
    bmih.biSizeImage = bmpDy * bmpRowSize;;
    bmih.biXPelsPerMeter = bmih.biYPelsPerMeter = 0;
    bmih.biClrUsed = bmih.biClrImportant = 0;
    SplashColorPtr bmpData = _bitmap->getDataPtr();

    ::StretchDIBits(hdc,
        // destination rectangle
        -leftMargin, -topMargin, pageDx, pageDy,
        // source rectangle
        0, 0, bmpDx, bmpDy,
        bmpData,
        (BITMAPINFO *)&bmih ,
        DIB_RGB_COLORS,
        SRCCOPY);
}

PdfEnginePoppler::PdfEnginePoppler() : 
    PdfEngine()
   , _pdfDoc(NULL)
{
}

PdfEnginePoppler::~PdfEnginePoppler()
{
    delete _outputDev;
    delete _pdfDoc;
}

bool PdfEnginePoppler::load(const char *fileName)
{
    setFileName(fileName);
    /* note: don't delete fileNameStr since PDFDoc takes ownership and deletes them itself */
    GooString *fileNameStr = new GooString(fileName);
    if (!fileNameStr) return false;

    _pdfDoc = new PDFDoc(fileNameStr, NULL, NULL, NULL);
    if (!_pdfDoc->isOk()) {
        return false;
    }
    _pageCount = _pdfDoc->getNumPages();
    return true;
}

int PdfEnginePoppler::pageRotation(int pageNo)
{
    assert(validPageNo(pageNo));
    return pdfDoc()->getPageRotate(pageNo);
}

SizeD PdfEnginePoppler::pageSize(int pageNo)
{
    double dx = pdfDoc()->getPageCropWidth(pageNo);
    double dy = pdfDoc()->getPageCropHeight(pageNo);
    return SizeD(dx, dy);
}

SplashOutputDev * PdfEnginePoppler::outputDevice() {
    if (!_outputDev) {
        GBool bitmapTopDown = gTrue;
        _outputDev = new SplashOutputDev(gSplashColorMode, 4, gFalse, gBgColor, bitmapTopDown);
        if (_outputDev)
            _outputDev->startDoc(_pdfDoc->getXRef());
    }
    return _outputDev;
}

RenderedBitmap *PdfEnginePoppler::renderBitmap(
                           int pageNo, double zoomReal, int rotation,
                           BOOL (*abortCheckCbkA)(void *data),
                           void *abortCheckCbkDataA)
{
    assert(outputDevice());
    if (!outputDevice()) return NULL;

    //DBG_OUT("PdfEnginePoppler::RenderBitmap(pageNo=%d) rotate=%d, zoomReal=%.2f%%\n", pageNo, rotation, zoomReal);

    double hDPI = (double)PDF_FILE_DPI * zoomReal * 0.01;
    double vDPI = (double)PDF_FILE_DPI * zoomReal * 0.01;
    GBool  useMediaBox = gFalse;
    GBool  crop        = gTrue;
    GBool  doLinks     = gTrue;
    _pdfDoc->displayPage(_outputDev, pageNo, hDPI, vDPI, rotation, useMediaBox, crop, doLinks,
        abortCheckCbkA, abortCheckCbkDataA);

#if 0
    PdfPageInfo *pageInfo = getPageInfo(pageNo);
    if (!pageInfo->links) {
        /* displayPage calculates links for this page (if doLinks is true)
           and puts inside pdfDoc */
        pageInfo->links = pdfDoc->takeLinks();
        if (pageInfo->links->getNumLinks() > 0)
            RecalcLinks();
    }
#endif
    RenderedBitmapSplash *renderedBitmap = new RenderedBitmapSplash(_outputDev->takeBitmap());
    return renderedBitmap;
}


PdfEngineFitz::PdfEngineFitz() : 
        PdfEngine()
        , _xref(NULL)
#if 0
        , _outline(NULL)
#endif
        , _pages(NULL)
        , _rast(NULL)
{
}

PdfEngineFitz::~PdfEngineFitz()
{
    if (_pages)
        pdf_droppagetree(_pages);

#if 0
    if (_outline)
        pdf_dropoutline(_outline);
#endif

    if (_xref) {
        if (_xref->store)
            pdf_dropstore(_xref->store);
        _xref->store = 0;
        pdf_closexref(_xref);
    }

    if (_rast) {
#ifdef FITZ_HEAD
        fz_dropgraphics(_rast);
#else
        fz_droprenderer(_rast);
#endif
    }
}

bool PdfEngineFitz::load(const char *fileName)
{
    setFileName(fileName);
    fz_error *error = pdf_newxref(&_xref);
    if (error)
        goto Error;

    error = pdf_loadxref(_xref, (char*)fileName);
    if (error) {
        if (!strncmp(error->msg, "ioerror", 7))
            goto Error;
        error = pdf_repairxref(_xref, (char*)fileName);
        if (error)
            goto Error;
    }

    error = pdf_decryptxref(_xref);
    if (error)
        goto Error;

    if (_xref->crypt) {
#ifdef FITZ_HEAD
        int okay = pdf_setpassword(_xref->crypt, "");
        if (!okay)
            goto Error;
#else
        error = pdf_setpassword(_xref->crypt, "");
        if (error)
            goto Error;
#endif
    }

    error = pdf_loadpagetree(&_pages, _xref);
    if (error)
        goto Error;

    _pageCount = _pages->count;
    return true;
Error:
    return false;
}

int PdfEngineFitz::pageRotation(int pageNo)
{
    assert(validPageNo(pageNo));
    fz_obj *dict = pdf_getpageobject(pages(), pageNo - 1);
    int rotation;
    fz_error *error = pdf_getpageinfo(dict, NULL, &rotation);
    if (error)
        return INVALID_ROTATION;
    return rotation;
}

SizeD PdfEngineFitz::pageSize(int pageNo)
{
    assert(validPageNo(pageNo));
    fz_obj *dict = pdf_getpageobject(pages(), pageNo - 1);
    fz_rect bbox;
    fz_error *error = pdf_getpageinfo(dict, &bbox, NULL);
    if (error)
        return SizeD(0,0);
    return SizeD(fabs(bbox.x1 - bbox.x0), fabs(bbox.y1 - bbox.y0));
}

static void ConvertPixmapForWindows(fz_pixmap *image)
{
    int bmpstride = ((image->w * 3 + 3) / 4) * 4;
    unsigned char *bmpdata = (unsigned char*)fz_malloc(image->h * bmpstride);
    if (!bmpdata)
        return;

    for (int y = 0; y < image->h; y++)
    {
        unsigned char *p = bmpdata + y * bmpstride;
#ifdef FITZ_HEAD
        unsigned char *s = image->p + y * image->w * 4;
#else
        unsigned char *s = image->samples + y * image->w * 4;
#endif
        for (int x = 0; x < image->w; x++)
        {
            p[x * 3 + 0] = s[x * 4 + 3];
            p[x * 3 + 1] = s[x * 4 + 2];
            p[x * 3 + 2] = s[x * 4 + 1];
        }
    }
#ifdef FITZ_HEAD
    fz_free(image->p);
    image->p = bmpdata;
#else
    fz_free(image->samples);
    image->samples = bmpdata;
#endif
}

static fz_matrix pdfapp_viewctm(pdf_page *page, float zoom, int rotate)
{
    fz_matrix ctm;
    ctm = fz_identity();
    ctm = fz_concat(ctm, fz_translate(0, -page->mediabox.y1));
    ctm = fz_concat(ctm, fz_scale(zoom, -zoom));
    ctm = fz_concat(ctm, fz_rotate(rotate + page->rotate));
    return ctm;
}

RenderedBitmap *PdfEngineFitz::renderBitmap(
                           int pageNo, double zoomReal, int rotation,
                           BOOL (*abortCheckCbkA)(void *data),
                           void *abortCheckCbkDataA)
{
    fz_error *          error;
    fz_matrix           ctm;
    fz_rect             bbox;
    fz_obj *            obj;
    pdf_page *          page;
    fz_pixmap *         image;

    if (!_rast) {
#ifdef FITZ_HEAD
        error = fz_newgraphics(&_rast, 1024 * 512);
#else
        error = fz_newrenderer(&_rast, pdf_devicergb, 0, 1024 * 512);
#endif
    }

    // TODO: should check for error from pdf_getpageobject?
    obj = pdf_getpageobject(_pages, pageNo - 1);
    error = pdf_loadpage(&page, _xref, obj);
    if (error)
        return NULL;
    zoomReal = zoomReal / 100.0;
    ctm = pdfapp_viewctm(page, zoomReal, rotation);
    bbox = fz_transformaabb(ctm, page->mediabox);
#ifdef FITZ_HEAD
    error = fz_drawtree(&image, _rast, page->tree, ctm, pdf_devicergb, fz_roundrect(bbox), 1);
#else
    error = fz_rendertree(&image, _rast, page->tree, ctm, fz_roundrect(bbox), 1);
#endif
    if (error)
        return NULL;

    pdf_droppage(page);
    ConvertPixmapForWindows(image);
    return new RenderedBitmapFitz(image);
}


