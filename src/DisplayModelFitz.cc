#include "DisplayModelFitz.h"

// TODO: must go
#include "GooString.h"
#include "UGooString.h"

RenderedBitmapFitz::RenderedBitmapFitz(fz_pixmap *image)
{
    _image = image;
}

RenderedBitmapFitz::~RenderedBitmapFitz()
{
    if (_image)
        fz_droppixmap(_image);
}

HBITMAP RenderedBitmapFitz::CreateDIBitmap(HDC hdc)
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

DisplayModelFitz::DisplayModelFitz(DisplayMode displayMode) :
    DisplayModel(displayMode)
{
    _pdfEngine = new PdfEngineFitz();
}

DisplayModelFitz::~DisplayModelFitz()
{
    // TODO: probably will need something
}

DisplayModelFitz *DisplayModelFitz_CreateFromFileName(
  const char *fileName, void *data,
  SizeD totalDrawAreaSize,
  int scrollbarXDy, int scrollbarYDx,
  DisplayMode displayMode, int startPage)
{
    DisplayModelFitz *    dm = NULL;

    dm = new DisplayModelFitz(displayMode);
    if (!dm)
        goto Error;

    if (!dm->load(fileName, startPage))
        goto Error;

    dm->setScrollbarsSize(scrollbarXDy, scrollbarYDx);
    dm->setTotalDrawAreaSize(totalDrawAreaSize);

//    DBG_OUT("DisplayModelFitz_CreateFromPageTree() pageCount = %d, startPage=%d, displayMode=%d\n",
//        dm->pageCount(), (int)dm->startPage, (int)displayMode);
    return dm;
Error:
    delete dm;
    return NULL;
}

void DisplayModelFitz::cvtUserToScreen(int pageNo, double *x, double *y)
{
    // TODO: implement me
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
    fz_free(image->samples);
    image->samples = bmpdata;
}

fz_matrix pdfapp_viewctm(pdf_page *page, float zoom, int rotate)
{
    fz_matrix ctm;
    ctm = fz_identity();
    ctm = fz_concat(ctm, fz_translate(0, -page->mediabox.y1));
    ctm = fz_concat(ctm, fz_scale(zoom, -zoom));
    ctm = fz_concat(ctm, fz_rotate(rotate + page->rotate));
    return ctm;
}

PlatformRenderedBitmap *DisplayModelFitz::renderBitmap(
                           int pageNo, double zoomReal, int rotation,
                           BOOL (*abortCheckCbkA)(void *data),
                           void *abortCheckCbkDataA)
{
    fz_error *          error;
#ifdef FITZ_HEAD
    fz_graphics *       rast;
#else
    fz_renderer *       rast;
#endif
    fz_matrix           ctm;
    fz_rect             bbox;
    fz_obj *            obj;
    pdf_page *          page;
    fz_pixmap *         image;

#ifdef FITZ_HEAD
    error = fz_newgraphics(&rast, 1024 * 512);
#else
    error = fz_newrenderer(&rast, pdf_devicergb, 0, 1024 * 512);
#endif

    // TODO: should check for error from pdf_getpageobject?
    obj = pdf_getpageobject(pdfEngineFitz()->pages(), pageNo - 1);
    error = pdf_loadpage(&page, pdfEngineFitz()->xref(), obj);
    if (error)
        return NULL;
    zoomReal = zoomReal / 100.0;
    ctm = pdfapp_viewctm(page, zoomReal, rotation);
    bbox = fz_transformaabb(ctm, page->mediabox);
#ifdef FITZ_HEAD
    error = fz_drawtree(&image, rast, page->tree, ctm, pdf_devicergb, fz_roundrect(bbox), 1);
#else
    error = fz_rendertree(&image, rast, page->tree, ctm, fz_roundrect(bbox), 1);
#endif
    if (error)
        return NULL;

    pdf_droppage(page);
    ConvertPixmapForWindows(image);
    return new RenderedBitmapFitz(image);
}

