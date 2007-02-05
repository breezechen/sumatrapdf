#ifndef _PDF_ENGINE_H_
#define _PDF_ENGINE_H_

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "SimpleRect.h"

#ifndef _FITZ_H_
#include <fitz.h>
#include <mupdf.h>
#endif

class PDFDoc;

/* For simplicity, all in one file. Would be cleaner if they were
   in separate files PdfEngineFitz.h and PdfEnginePoppler.h */

#define INVALID_PAGE_NO     -1
#define INVALID_ROTATION    -1

class PdfEngine {
public:
    PdfEngine() : 
        _fileName(0)
        , _pageCount(INVALID_PAGE_NO) 
    { }

    virtual ~PdfEngine() { free((void*)_fileName); }

    const char *fileName(void) { return _fileName; };

    void setFileName(const char *fileName) {
        assert(!_fileName);
        _fileName = (const char*)strdup(fileName);
    }

    bool validPageNo(int pageNo) {
        if ((pageNo >= 1) && (pageNo <= pageCount()))
            return true;
        return false;
    }

    int pageCount(void) { return _pageCount; }

    virtual bool load(const char *fileName) = 0;
    virtual int pageRotation(int pageNo) = 0;
    virtual SizeD pageSize(int pageNo) = 0;

protected:
    const char *_fileName;
    int _pageCount;
};

class PdfEnginePoppler : public PdfEngine {
public:
    PdfEnginePoppler();
    virtual ~PdfEnginePoppler();
    virtual bool load(const char *fileName);
    virtual int pageRotation(int pageNo);
    virtual SizeD pageSize(int pageNo);

    PDFDoc* pdfDoc() { return _pdfDoc; }
private:
    PDFDoc *    _pdfDoc;
};

class PdfEngineFitz :public  PdfEngine {
public:
    PdfEngineFitz();
    virtual ~PdfEngineFitz();
    virtual bool load(const char *fileName);
    virtual int pageRotation(int pageNo);
    virtual SizeD pageSize(int pageNo);

    pdf_xref * xref() { return _xref; }
    pdf_pagetree * pages() { return _pages; }
private:
    pdf_xref *      _xref;
#if 0
    pdf_outline *   _outline;
#endif
    pdf_pagetree *  _pages;
};

#endif

