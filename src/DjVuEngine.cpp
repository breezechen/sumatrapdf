/* Copyright 2006-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// hack to prevent libdjvu from being built as an export/import library
#define DDJVUAPI /**/
#define MINILISPAPI /**/

#include <ddjvuapi.h>
#include <miniexp.h>
#include "DjVuEngine.h"
#include "FileUtil.h"
#include "Vec.h"

// TODO: libdjvu leaks memory - among others
//       DjVuPort::corpse_lock, DjVuPort::corpse_head, pcaster,
//       DataPool::OpenFiles::global_ptr, FCPools::global_ptr
//       cf. http://sourceforge.net/projects/djvu/forums/forum/103286/topic/3553602

class RenderedDjVuPixmap : public RenderedBitmap {
public:
    RenderedDjVuPixmap(char *data, int width, int height);
};

RenderedDjVuPixmap::RenderedDjVuPixmap(char *data, int width, int height) :
    RenderedBitmap(NULL, width, height)
{
    int stride = ((width * 3 + 3) / 4) * 4;

    BITMAPINFO *bmi = (BITMAPINFO *)calloc(1, sizeof(BITMAPINFOHEADER));

    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = width;
    bmi->bmiHeader.biHeight = -height;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biCompression = BI_RGB;
    bmi->bmiHeader.biBitCount = 24;
    bmi->bmiHeader.biSizeImage = height * stride;
    bmi->bmiHeader.biClrUsed = 0;

    HDC hDC = GetDC(NULL);
    _hbmp = CreateDIBitmap(hDC, &bmi->bmiHeader, CBM_INIT, data, bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hDC);

    free(bmi);
}

class DjVuDestination : public PageDestination {
    // the link format can be any of
    //   #<pageNo>         e.g. #1 for FirstPage and #13 for page 13
    //   #[+-]<pageCount>  e.g. #+1 for NextPage and #-1 for PrevPage
    //   #filename.djvu    use ResolveNamedDest to get a link in #<pageNo> format
    //   http://example.net/#hyperlink
    char *link;

public:
    DjVuDestination(const char *link) : link(Str::Dup(link)) { }
    ~DjVuDestination() { free(link); }

    virtual const char *GetType() const {
        if (Str::StartsWith(link, "#") && ChrIsDigit(link[1]))
            return "ScrollTo";
        if (Str::Eq(link, "#+1"))
            return "NextPage";
        if (Str::Eq(link, "#-1"))
            return "PrevPage";
        if (Str::StartsWithI(link, "http:") || Str::StartsWithI(link, "https:"))
            return "LaunchURL";
        return NULL;
    }
    virtual int GetDestPageNo() const {
        if (Str::StartsWith(link, "#") && ChrIsDigit(link[1]))
            return atoi(link + 1);
        return 0;
    }
    virtual RectD GetDestRect() const {
        return RectD();
    }
    virtual TCHAR *GetDestValue() const {
        if (Str::Eq(GetType(), "LaunchURL"))
            return Str::Conv::FromUtf8(link);
        return NULL;
    }
};

class DjVuLink : public PageElement {
    DjVuDestination *dest;
    int pageNo;
    RectD rect;
    TCHAR *value;

public:
    DjVuLink(int pageNo, RectI rect, const char *link, const char *comment) :
        pageNo(pageNo), rect(rect.Convert<double>()), value(NULL) {
        dest = new DjVuDestination(link);
        if (!Str::IsEmpty(comment))
            value = Str::Conv::FromUtf8(comment);
    }
    virtual ~DjVuLink() {
        delete dest;
        free(value);
    }

    virtual int GetPageNo() const { return pageNo; }
    virtual RectD GetRect() const { return rect; }
    virtual TCHAR *GetValue() const {
        if (value)
            return Str::Dup(value);
        if (Str::Eq(dest->GetType(), "LaunchURL"))
            return dest->GetDestValue();
        return NULL;
    }

    virtual PageDestination *AsLink() { return dest; }
};

class DjVuToCItem : public DocToCItem {
    DjVuDestination *dest;

public:
    bool isGeneric;

    DjVuToCItem(const char *title, const char *link) :
        DocToCItem(Str::Conv::FromUtf8(title)), isGeneric(false) {
        dest = new DjVuDestination(link);
        pageNo = dest->GetDestPageNo();
    }
    virtual ~DjVuToCItem() { delete dest; }

    virtual PageDestination *GetLink() { return dest; }
};

class CDjVuEngine : public DjVuEngine {
    friend DjVuEngine;

public:
    CDjVuEngine();
    virtual ~CDjVuEngine();
    virtual DjVuEngine *Clone() {
        return CreateFromFileName(fileName);
    }

    virtual const TCHAR *FileName() const { return fileName; };
    virtual int PageCount() const { return pageCount; }

    virtual RectD PageMediabox(int pageNo) {
        assert(1 <= pageNo && pageNo <= PageCount());
        return mediaboxes[pageNo-1];
    }
    virtual RectD PageContentBox(int pageNo, RenderTarget target=Target_View);

    virtual RenderedBitmap *RenderBitmap(int pageNo, float zoom, int rotation,
                         RectD *pageRect=NULL, /* if NULL: defaults to the page's mediabox */
                         RenderTarget target=Target_View, bool useGdi=false);
    virtual bool RenderPage(HDC hDC, int pageNo, RectI screenRect,
                         float zoom=0, int rotation=0,
                         RectD *pageRect=NULL, RenderTarget target=Target_View);

    virtual PointD Transform(PointD pt, int pageNo, float zoom, int rotate, bool inverse=false);
    virtual RectD Transform(RectD rect, int pageNo, float zoom, int rotate, bool inverse=false);

    virtual unsigned char *GetFileData(size_t *cbCount);
    virtual bool HasTextContent() { return true; }
    virtual TCHAR * ExtractPageText(int pageNo, TCHAR *lineSep, RectI **coords_out=NULL,
                                    RenderTarget target=Target_View);
    virtual bool IsImagePage(int pageNo) { return true; }
    virtual PageLayoutType PreferredLayout() { return Layout_Single; }

    // DPI isn't constant for all pages and thus premultiplied
    virtual float GetFileDPI() const { return 300.0f; }
    virtual const TCHAR *GetDefaultFileExt() const { return _T(".djvu"); }

    // we currently don't load pages lazily, so there's nothing to do here
    virtual bool BenchLoadPage(int pageNo) { return true; }

    virtual Vec<PageElement *> *GetElements(int pageNo);
    virtual PageElement *GetElementAtPos(int pageNo, PointD pt);

    virtual PageDestination *GetNamedDest(const TCHAR *name);
    virtual bool HasToCTree() const { return outline != miniexp_nil; }
    virtual DocToCItem *GetToCTree();

protected:
    const TCHAR *fileName;

    int pageCount;
    RectD *mediaboxes;

    ddjvu_context_t *ctx;
    ddjvu_document_t *doc;
    miniexp_t outline;
    miniexp_t *annos;

    Vec<ddjvu_fileinfo_t> fileInfo;

    CRITICAL_SECTION ctxAccess;

    bool ExtractPageText(miniexp_t item, const TCHAR *lineSep,
                         Str::Str<TCHAR>& extracted, Vec<RectI>& coords);
    char *ResolveNamedDest(const char *name);
    DjVuToCItem *BuildToCTree(miniexp_t entry, int& idCounter);
    bool Load(const TCHAR *fileName);
};

void SpinDdjvuMessageLoop(ddjvu_context_t *ctx, bool wait=true)
{
    const ddjvu_message_t *msg;
    if (wait)
        msg = ddjvu_message_wait(ctx);
    while ((msg = ddjvu_message_peek(ctx)))
        ddjvu_message_pop(ctx);
}

CDjVuEngine::CDjVuEngine() : fileName(NULL), pageCount(0), mediaboxes(NULL),
    outline(miniexp_nil), annos(NULL)
{
    InitializeCriticalSection(&ctxAccess);

    // for now, create one ddjvu context per document
    // TODO: share between all DjVuEngines
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ScopedMem<char> unique(_MemToHex(&ft));
    ctx = ddjvu_context_create(unique);
}

CDjVuEngine::~CDjVuEngine()
{
    EnterCriticalSection(&ctxAccess);

    delete[] mediaboxes;
    free((void *)fileName);

    if (annos) {
        for (int i = 0; i < pageCount; i++)
            if (annos[i])
                ddjvu_miniexp_release(doc, annos[i]);
        free(annos);
    }
    if (outline != miniexp_nil)
        ddjvu_miniexp_release(doc, outline);
    if (doc)
        ddjvu_document_release(doc);
    if (ctx)
        ddjvu_context_release(ctx);

    LeaveCriticalSection(&ctxAccess);
    DeleteCriticalSection(&ctxAccess);
}

bool CDjVuEngine::Load(const TCHAR *fileName)
{
    if (!ctx)
        return false;

    this->fileName = Str::Dup(fileName);

    ScopedCritSec scope(&ctxAccess);
    ScopedMem<char> fileNameUtf8(Str::Conv::ToUtf8(fileName));
    doc = ddjvu_document_create_by_filename_utf8(ctx, fileNameUtf8, /* cache */ TRUE);
    if (!doc)
        return false;

    while (!ddjvu_document_decoding_done(doc))
        SpinDdjvuMessageLoop(ctx);
    if (ddjvu_document_decoding_error(doc))
        return false;

    pageCount = ddjvu_document_get_pagenum(doc);
    mediaboxes = new RectD[pageCount];
    annos = SAZA(miniexp_t, pageCount);

    for (int i = 0; i < pageCount; i++) {
        ddjvu_status_t status;
        ddjvu_pageinfo_t info;
        while ((status = ddjvu_document_get_pageinfo(doc, i, &info)) < DDJVU_JOB_OK)
            SpinDdjvuMessageLoop(ctx);
        if (status != DDJVU_JOB_OK)
            continue;

        mediaboxes[i] = RectD(0, 0, info.width * GetFileDPI() / info.dpi,
                                    info.height * GetFileDPI() / info.dpi);
        while ((annos[i] = ddjvu_document_get_pageanno(doc, i)) == miniexp_dummy)
            SpinDdjvuMessageLoop(ctx);
    }

    while ((outline = ddjvu_document_get_outline(doc)) == miniexp_dummy)
        SpinDdjvuMessageLoop(ctx);
    if (!miniexp_consp(outline) || miniexp_car(outline) != miniexp_symbol("bookmarks")) {
        ddjvu_miniexp_release(doc, outline);
        outline = miniexp_nil;
    }

    int fileCount = ddjvu_document_get_filenum(doc);
    for (int i = 0; i < fileCount; i++) {
        ddjvu_status_t status;
        ddjvu_fileinfo_s info;
        while ((status = ddjvu_document_get_fileinfo(doc, i, &info)) < DDJVU_JOB_OK)
            SpinDdjvuMessageLoop(ctx);
        if (DDJVU_JOB_OK == status && info.type == 'P')
            fileInfo.Append(info);
    }

    return true;
}

RenderedBitmap *CDjVuEngine::RenderBitmap(int pageNo, float zoom, int rotation, RectD *pageRect, RenderTarget target, bool useGdi)
{
    ScopedCritSec scope(&ctxAccess);

    RectD pageRc = pageRect ? *pageRect : PageMediabox(pageNo);
    RectI screen = Transform(pageRc, pageNo, zoom, rotation).Round();
    RectI full = Transform(PageMediabox(pageNo), pageNo, zoom, rotation).Round();
    screen = full.Intersect(screen);

    ddjvu_page_t *page = ddjvu_page_create_by_pageno(doc, pageNo-1);
    if (!page)
        return NULL;
    int rotation4 = (((-rotation / 90) % 4) + 4) % 4;
    ddjvu_page_set_rotation(page, (ddjvu_page_rotation_t)rotation4);

    while (!ddjvu_page_decoding_done(page))
        SpinDdjvuMessageLoop(ctx);
    if (ddjvu_page_decoding_error(page))
        return NULL;

    ddjvu_format_t *fmt = ddjvu_format_create(DDJVU_FORMAT_BGR24, 0, NULL);
    ddjvu_format_set_row_order(fmt, /* top_to_bottom */ TRUE);
    ddjvu_rect_t prect = { full.x, full.y, full.dx, full.dy };
    ddjvu_rect_t rrect = { screen.x, 2 * full.y - screen.y + full.dy - screen.dy, screen.dx, screen.dy };

    RenderedBitmap *bmp = NULL;
    int stride = ((screen.dx * 3 + 3) / 4) * 4;
    ScopedMem<char> bmpData(SAZA(char, stride * (screen.dy + 5)));
    if (bmpData) {
#ifndef DEBUG
        if (ddjvu_page_render(page, DDJVU_RENDER_COLOR, &prect, &rrect, fmt, stride, bmpData.Get()))
#else
        // TODO: there seems to be a heap corruption in IW44Image.cpp
        //       in debug builds when passing in DDJVU_RENDER_COLOR
        if (ddjvu_page_render(page, DDJVU_RENDER_MASKONLY, &prect, &rrect, fmt, stride, bmpData.Get()))
#endif
            bmp = new RenderedDjVuPixmap(bmpData, screen.dx, screen.dy);
    }

    ddjvu_format_release(fmt);
    ddjvu_page_release(page);
    return bmp;
}

bool CDjVuEngine::RenderPage(HDC hDC, int pageNo, RectI screenRect, float zoom, int rotation, RectD *pageRect, RenderTarget target)
{
    bool success = true;
    RectD mediabox = PageMediabox(pageNo);
    HRGN clip = CreateRectRgn(screenRect.x, screenRect.y, screenRect.x + screenRect.dx, screenRect.y + screenRect.dy);
    SelectClipRgn(hDC, clip);

    // render in 1 MB bands, as otherwise GDI can run out of memory
    RectD rect = pageRect ? *pageRect : mediabox;
    int bandDy = (int)((1 << 20) / (rect.dy * zoom));
    PointI pt = Transform(rect, pageNo, zoom, rotation).TL().Convert<int>();

    for (int y = 0; y * bandDy < rect.dy; y++) {
        RectD pageBand(rect.x, y * bandDy, rect.dx, bandDy);
        pageBand = pageBand.Intersect(mediabox);
        RectI screenBand = Transform(pageBand, pageNo, zoom, rotation).Round();
        screenBand.Offset(screenRect.x - pt.x, screenRect.y - pt.y);

        RenderedBitmap *bmp = RenderBitmap(pageNo, zoom, rotation, &pageBand, target);
        if (bmp && bmp->GetBitmap())
            bmp->StretchDIBits(hDC, screenBand);
        else
            success = false;
        delete bmp;
    }

    SelectClipRgn(hDC, NULL);
    return success;
}

RectD CDjVuEngine::PageContentBox(int pageNo, RenderTarget target)
{
    ScopedCritSec scope(&ctxAccess);

    RectD pageRc = PageMediabox(pageNo);
    ddjvu_page_t *page = ddjvu_page_create_by_pageno(doc, pageNo-1);
    if (!page)
        return pageRc;
    ddjvu_page_set_rotation(page, DDJVU_ROTATE_0);

    while (!ddjvu_page_decoding_done(page))
        SpinDdjvuMessageLoop(ctx);
    if (ddjvu_page_decoding_error(page))
        return pageRc;

    // render the page in 8-bit grayscale up to 250x250 px in size
    ddjvu_format_t *fmt = ddjvu_format_create(DDJVU_FORMAT_GREY8, 0, NULL);
    ddjvu_format_set_row_order(fmt, /* top_to_bottom */ TRUE);
    double zoom = min(min(250.0 / pageRc.dx, 250.0 / pageRc.dy), 1.0);
    RectI full = RectD(0, 0, pageRc.dx * zoom, pageRc.dy * zoom).Round();
    ddjvu_rect_t prect = { full.x, full.y, full.dx, full.dy }, rrect = prect;

    ScopedMem<char> bmpData(SAZA(char, full.dx * full.dy + 1));
    if (bmpData && ddjvu_page_render(page, DDJVU_RENDER_MASKONLY, &prect, &rrect, fmt, full.dx, bmpData.Get())) {
        // determine the content box by counting white pixels from the edges
        RectD content(full.dx, -1, 0, 0);
        for (int y = 0; y < full.dy; y++) {
            int x;
            for (x = 0; x < full.dx && bmpData[y * full.dx + x] == '\xFF'; x++);
            if (x < full.dx) {
                // narrow the left margin down (if necessary)
                if (x < content.x)
                    content.x = x;
                // narrow the right margin down (if necessary)
                for (x = full.dx - 1; x > content.x + content.dx && bmpData[y * full.dx + x] == '\xFF'; x--);
                if (x > content.x + content.dx)
                    content.dx = x - content.x + 1;
                // narrow either the top or the bottom margin down
                if (content.y == -1)
                    content.y = y;
                else
                    content.dy = y - content.y + 1;
            }
        }
        if (!content.IsEmpty()) {
            // undo the zoom and round generously
            content.x /= zoom; content.dx /= zoom;
            content.y /= zoom; content.dy /= zoom;
            pageRc = content.Round().Convert<double>();
        }
    }

    ddjvu_format_release(fmt);
    ddjvu_page_release(page);

    return pageRc;
}

PointD CDjVuEngine::Transform(PointD pt, int pageNo, float zoom, int rotate, bool inverse)
{
    assert(zoom > 0);
    if (zoom <= 0)
        return pt;

    SizeD page = PageMediabox(pageNo).Size();

    if (inverse) {
        // transform the page size to get a correct frame of reference
        page.dx *= zoom; page.dy *= zoom;
        if (rotate % 180 != 0)
            swap(page.dx, page.dy);
        // invert rotation and zoom
        rotate = -rotate;
        zoom = 1.0f / zoom;
    }

    PointD res;
    rotate = rotate % 360;
    if (rotate < 0) rotate += 360;
    if (90 == rotate)
        res = PointD(page.dy - pt.y, pt.x);
    else if (180 == rotate)
        res = PointD(page.dx - pt.x, page.dy - pt.y);
    else if (270 == rotate)
        res = PointD(pt.y, page.dx - pt.x);
    else // if (0 == rotate)
        res = pt;

    res.x *= zoom; res.y *= zoom;
    return res;
}

RectD CDjVuEngine::Transform(RectD rect, int pageNo, float zoom, int rotate, bool inverse)
{
    PointD TL = Transform(rect.TL(), pageNo, zoom, rotate, inverse);
    PointD BR = Transform(rect.BR(), pageNo, zoom, rotate, inverse);
    return RectD::FromXY(TL, BR);
}

unsigned char *CDjVuEngine::GetFileData(size_t *cbCount)
{
    return (unsigned char *)File::ReadAll(fileName, cbCount);
}

bool CDjVuEngine::ExtractPageText(miniexp_t item, const TCHAR *lineSep, Str::Str<TCHAR>& extracted, Vec<RectI>& coords)
{
    miniexp_t type = miniexp_car(item);
    if (!miniexp_symbolp(type))
        return false;
    item = miniexp_cdr(item);

    if (!miniexp_numberp(miniexp_car(item))) return false;
    int x0 = miniexp_to_int(miniexp_car(item)); item = miniexp_cdr(item);
    if (!miniexp_numberp(miniexp_car(item))) return false;
    int y0 = miniexp_to_int(miniexp_car(item)); item = miniexp_cdr(item);
    if (!miniexp_numberp(miniexp_car(item))) return false;
    int x1 = miniexp_to_int(miniexp_car(item)); item = miniexp_cdr(item);
    if (!miniexp_numberp(miniexp_car(item))) return false;
    int y1 = miniexp_to_int(miniexp_car(item)); item = miniexp_cdr(item);
    RectI rect = RectI::FromXY(x0, y0, x1, y1);

    miniexp_t str = miniexp_car(item);
    if (miniexp_stringp(str) && !miniexp_cdr(item)) {
        const char *content = miniexp_to_str(str);
        TCHAR *value = Str::Conv::FromUtf8(content);
        if (value) {
            size_t len = Str::Len(value);
            // TODO: split the rectangle into individual parts per glyph
            for (size_t i = 0; i < len; i++)
                coords.Append(RectI(rect.x, rect.y, rect.dx, rect.dy));
            extracted.AppendAndFree(value);
        }
        if (miniexp_symbol("word") == type) {
            extracted.Append(' ');
            coords.Append(RectI(rect.x + rect.dx, rect.y, 2, rect.dy));
        }
        else if (miniexp_symbol("char") != type) {
            extracted.Append(lineSep);
            for (size_t i = 0; i < Str::Len(lineSep); i++)
                coords.Append(RectI());
        }
        item = miniexp_cdr(item);
    }
    while (miniexp_consp(str)) {
        ExtractPageText(str, lineSep, extracted, coords);
        item = miniexp_cdr(item);
        str = miniexp_car(item);
    }
    return !item;
}

TCHAR *CDjVuEngine::ExtractPageText(int pageNo, TCHAR *lineSep, RectI **coords_out, RenderTarget target)
{
    ScopedCritSec scope(&ctxAccess);

    miniexp_t pagetext;
    while ((pagetext = ddjvu_document_get_pagetext(doc, pageNo-1, NULL)) == miniexp_dummy)
        SpinDdjvuMessageLoop(ctx);
    if (miniexp_nil == pagetext)
        return NULL;

    Str::Str<TCHAR> extracted;
    Vec<RectI> coords;
    bool success = ExtractPageText(pagetext, lineSep, extracted, coords);
    ddjvu_miniexp_release(doc, pagetext);
    if (!success)
        return NULL;

    assert(Str::Len(extracted.Get()) == coords.Count());
    if (coords_out) {
        // TODO: the coordinates aren't completely correct yet
        RectI page = PageMediabox(pageNo).Round();
        for (size_t i = 0; i < coords.Count(); i++)
            if (!coords[i].IsEmpty())
                coords[i].y = page.dy - coords[i].y - coords[i].dy;
        *coords_out = coords.StealData();
    }

    return extracted.StealData();
}

Vec<PageElement *> *CDjVuEngine::GetElements(int pageNo)
{
    assert(1 <= pageNo && pageNo <= PageCount());
    if (!annos || !annos[pageNo-1])
        return NULL;

    Vec<PageElement *> *els = new Vec<PageElement *>();
    RectI page = PageMediabox(pageNo).Round();

    miniexp_t *links = ddjvu_anno_get_hyperlinks(annos[pageNo-1]);
    for (int i = 0; links[i]; i++) {
        miniexp_t anno = miniexp_cdr(links[i]);

        miniexp_t url = miniexp_car(anno);
        const char *urlUtf8 = NULL;
        if (miniexp_stringp(url))
            urlUtf8 = miniexp_to_str(url);
        else if (miniexp_consp(url) && miniexp_car(url) == miniexp_symbol("url") &&
                 miniexp_stringp(miniexp_cadr(url)) && miniexp_stringp(miniexp_caddr(url))) {
            urlUtf8 = miniexp_to_str(miniexp_cadr(url));
        }
        if (!urlUtf8)
            continue;

        anno = miniexp_cdr(anno);
        miniexp_t comment = miniexp_car(anno);
        const char *commentUtf8 = NULL;
        if (miniexp_stringp(comment))
            commentUtf8 = miniexp_to_str(comment);

        anno = miniexp_cdr(anno);
        miniexp_t area = miniexp_car(anno);
        miniexp_t type = miniexp_car(area);
        if (type != miniexp_symbol("rect") && type != miniexp_symbol("oval") && type != miniexp_symbol("text"))
            continue; // unsupported shape;

        area = miniexp_cdr(area);
        if (!miniexp_numberp(miniexp_car(area))) continue;
        int x = miniexp_to_int(miniexp_car(area)); area = miniexp_cdr(area);
        if (!miniexp_numberp(miniexp_car(area))) continue;
        int y = miniexp_to_int(miniexp_car(area)); area = miniexp_cdr(area);
        if (!miniexp_numberp(miniexp_car(area))) continue;
        int w = miniexp_to_int(miniexp_car(area)); area = miniexp_cdr(area);
        if (!miniexp_numberp(miniexp_car(area))) continue;
        int h = miniexp_to_int(miniexp_car(area)); area = miniexp_cdr(area);
        RectI rect(x, page.dy - y - h, w, h);

        ScopedMem<char> link(ResolveNamedDest(urlUtf8));
        els->Append(new DjVuLink(pageNo, rect, link ? link : urlUtf8, commentUtf8));
    }
    free(links);

    return els;
}

PageElement *CDjVuEngine::GetElementAtPos(int pageNo, PointD pt)
{
    Vec<PageElement *> *els = GetElements(pageNo);
    if (!els)
        return NULL;

    PageElement *el = NULL;
    for (size_t i = 0; i < els->Count() && !el; i++)
        if (els->At(i)->GetRect().Inside(pt))
            el = els->At(i);

    if (el)
        els->Remove(el);
    DeleteVecMembers(*els);
    delete els;

    return el;
}

// returns a numeric DjVu link to a named page (if the name resolves)
// caller needs to free() the result
char *CDjVuEngine::ResolveNamedDest(const char *name)
{
    if (!Str::StartsWith(name, "#"))
        return NULL;
    for (size_t i = 0; i < fileInfo.Count(); i++)
        if (fileInfo[i].pageno >= 0 && Str::EqI(name + 1, fileInfo[i].id))
            return Str::Format("#%d", fileInfo[i].pageno + 1);
    return NULL;
}

PageDestination *CDjVuEngine::GetNamedDest(const TCHAR *name)
{
    ScopedMem<char> nameUtf8(Str::Conv::ToUtf8(name));
    if (!Str::StartsWith(nameUtf8.Get(), "#"))
        nameUtf8.Set(Str::Join("#", nameUtf8));

    ScopedMem<char> link(ResolveNamedDest(nameUtf8));
    if (link)
        return new DjVuDestination(link);
    return NULL;
}

DjVuToCItem *CDjVuEngine::BuildToCTree(miniexp_t entry, int& idCounter)
{
    DjVuToCItem *node = NULL;
    DocToCItem *last = node;

    for (miniexp_t rest = miniexp_cdr(entry); miniexp_consp(rest); rest = miniexp_cdr(rest)) {
        miniexp_t item = miniexp_car(rest);
        if (!miniexp_consp(item) || !miniexp_consp(miniexp_cdr(item)) ||
            !miniexp_stringp(miniexp_car(item)) || !miniexp_stringp(miniexp_cadr(item)))
            continue;

        const char *name = miniexp_to_str(miniexp_car(item));
        const char *link = miniexp_to_str(miniexp_cadr(item));

        DjVuToCItem *tocItem = NULL;
        ScopedMem<char> linkNo(ResolveNamedDest(link));
        if (!linkNo)
            tocItem = new DjVuToCItem(name, link);
        else if (!Str::IsEmpty(name) && !Str::Eq(name, link + 1))
            tocItem = new DjVuToCItem(name, linkNo);
        else {
            ScopedMem<char> name(Str::Format("Page %d", atoi(linkNo + 1)));
            tocItem = new DjVuToCItem(name, linkNo);
            // don't display such generic items by default
            tocItem->isGeneric = true;
        }

        tocItem->id = ++idCounter;
        tocItem->child = BuildToCTree(miniexp_cddr(item), idCounter);
        if (tocItem->child) {
            // close items that only contain generic page references
            tocItem->open = false;
            for (DocToCItem *n = tocItem->child; n && !tocItem->open; n = n->next)
                if (!static_cast<DjVuToCItem *>(n)->isGeneric)
                    tocItem->open = true;
        }

        if (!node)
            last = node = tocItem;
        else
            last = last->next = tocItem;
    }

    return node;
}

DocToCItem *CDjVuEngine::GetToCTree()
{
    if (!HasToCTree())
        return NULL;

    int idCounter = 0;
    return BuildToCTree(outline, idCounter);
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

#ifdef DEBUG
class MinilispCleanup { public: ~MinilispCleanup() { minilisp_finish(); } };
MinilispCleanup cleanUpMinilispAtShutdownInOrderToPreventExcessiveLeakNotifications;
#endif
