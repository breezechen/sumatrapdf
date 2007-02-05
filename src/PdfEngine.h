#ifndef _PDF_ENGINE_H_
#define _PDF_ENGINE_H_

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "SimpleRect.h"

class PDFDoc;

/* For simplicity, all in one file. Would be cleaner if they were
   in separate files PdfEngineFitz.h and PdfEnginePoppler.h */

#define INVALID_PAGE_NO     -1
#define INVALID_ROTATION    -1

class PdfEngine {
public:
    PdfEngine() { _fileName = 0; }
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

    virtual bool load(const char *fileName) = 0;
    virtual int pageCount(void) = 0;
    virtual int pageRotation(int pageNo) = 0;
    virtual SizeD pageSize(int pageNo) = 0;


protected:
    const char *_fileName;
};

class PdfEnginePoppler : PdfEngine {
public:
    PdfEnginePoppler() : 
        PdfEngine()
       , _pdfDoc(NULL)
       {}
    virtual ~PdfEnginePoppler() {}
    virtual bool load(const char *fileName);
    virtual int pageCount(void);
    virtual int pageRotation(int pageNo);
    virtual SizeD pageSize(int pageNo);

private:
    PDFDoc *    _pdfDoc;
};

class PdfEngineFitz : PdfEngine {
public:
    PdfEngineFitz() : 
        PdfEngine()
        {}
    virtual ~PdfEngineFitz() {}
    virtual bool load(const char *fileName);
    virtual int pageCount(void);
    virtual int pageRotation(int pageNo);
    virtual SizeD pageSize(int pageNo);

private:
};

#endif

