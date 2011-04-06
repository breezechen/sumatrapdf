/* Copyright 2006-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#ifndef CbxEngine_H
#define CbxEngine_H

#include "BaseEngine.h"
#include "Vec.h"

class ComicBookPage {
public:
    HGLOBAL             bmpData;
    Gdiplus::Bitmap *   bmp;
    int                 width, height;

    ComicBookPage(HGLOBAL bmpData, Gdiplus::Bitmap *bmp) :
        bmpData(bmpData), bmp(bmp),
        width(bmp->GetWidth()), height(bmp->GetHeight()) { }

    ~ComicBookPage() {
        delete bmp;
        GlobalFree(bmpData);
    }

    SizeI size() { return SizeI(width, height); }
};

class CbxEngine : public BaseEngine {
public:
    CbxEngine(const TCHAR *fileName);
    virtual ~CbxEngine();
    virtual CbxEngine *Clone() {
        return CreateFromFileName(_fileName);
    }

    virtual const TCHAR *FileName() const { return _fileName; };
    virtual int PageCount() const { return _pages.Count(); }

    virtual RectD PageMediabox(int pageNo) {
        assert(1 <= pageNo && pageNo <= PageCount());
        ComicBookPage *page = _pages[pageNo - 1];
        return RectD(0, 0, page->width, page->height);
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
    virtual TCHAR * ExtractPageText(int pageNo, TCHAR *lineSep=DOS_NEWLINE, RectI **coords_out=NULL, RenderTarget target=Target_View) { return NULL; }
    virtual bool IsImagePage(int pageNo) { return true; }

    // there's no text...
    virtual bool IsCopyingTextAllowed() { return false; }

    // we currently don't load pages lazily, so there's nothing to do here
    virtual bool _benchLoadPage(int pageNo) { return true; }

protected:
    const TCHAR *_fileName;
    Vec<ComicBookPage *> _pages;

    void GetTransform(Gdiplus::Matrix& m, int pageNo, float zoom, int rotate);

public:
    static CbxEngine *CreateFromFileName(const TCHAR *fileName);    
};

#endif
