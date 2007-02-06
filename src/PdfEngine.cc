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
#include "BaseUtils.h"

PdfEnginePoppler::PdfEnginePoppler() : 
    PdfEngine()
   , _pdfDoc(NULL)
{
}

PdfEnginePoppler::~PdfEnginePoppler()
{
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

PdfEngineFitz::PdfEngineFitz() : 
        PdfEngine()
        , _xref(NULL)
#if 0
        , _outline(NULL)
#endif
        , _pages(NULL)
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

