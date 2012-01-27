/* Copyright 2006-2012 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

extern "C" {
__pragma(warning(push))
#include <fitz.h>
__pragma(warning(pop))
}

#include "PdfEngine.h"
#include "StrUtil.h"
#include "FileUtil.h"
#include "Scopes.h"

// maximum size of a file that's entirely loaded into memory before parsed
// and displayed; larger files will be kept open while they're displayed
// so that their content can be loaded on demand in order to preserve memory
#define MAX_MEMORY_FILE_SIZE (10 * 1024 * 1024)

// number of page content trees to cache for quicker rendering
#define MAX_PAGE_RUN_CACHE  8
// maximum estimated memory requirement allowed for the run cache of one document
#define MAX_PAGE_RUN_MEMORY (40 * 1024 * 1024)

// maximum amount of memory that MuPDF should use per fz_context store
#define MAX_CONTEXT_MEMORY  (256 * 1024 * 1024)

// when set, always uses GDI+ for rendering (else GDI+ is only used for
// zoom levels above 4000% and for rendering directly into an HDC)
static bool gDebugGdiPlusDevice = false;

void DebugGdiPlusDevice(bool enable)
{
    gDebugGdiPlusDevice = enable;
}

void CalcMD5Digest(unsigned char *data, size_t byteCount, unsigned char digest[16])
{
    fz_md5 md5;
    fz_md5_init(&md5);
#ifdef _WIN64
    for (; byteCount > UINT_MAX; data += UINT_MAX, byteCount -= UINT_MAX)
        fz_md5_update(&md5, data, UINT_MAX);
#endif
    fz_md5_update(&md5, data, (unsigned int)byteCount);
    fz_md5_final(&md5, digest);
}

inline fz_rect fz_bbox_to_rect(fz_bbox bbox)
{
    fz_rect result = { (float)bbox.x0, (float)bbox.y0, (float)bbox.x1, (float)bbox.y1 };
    return result;
}

inline RectD fz_rect_to_RectD(fz_rect rect)
{
    return RectD::FromXY(rect.x0, rect.y0, rect.x1, rect.y1);
}

inline fz_rect fz_RectD_to_rect(RectD rect)
{
    fz_rect result = { (float)rect.x, (float)rect.y, (float)(rect.x + rect.dx), (float)(rect.y + rect.dy) };
    return result;
}

inline RectI fz_bbox_to_RectI(fz_bbox bbox)
{
    return RectI::FromXY(bbox.x0, bbox.y0, bbox.x1, bbox.y1);
}

inline fz_bbox fz_RectI_to_bbox(RectI bbox)
{
    fz_bbox result = { bbox.x, bbox.y, bbox.x + bbox.dx, bbox.y + bbox.dy };
    return result;
}

inline bool fz_is_pt_in_rect(fz_rect rect, fz_point pt)
{
    return fz_rect_to_RectD(rect).Inside(PointD(pt.x, pt.y));
}

inline float fz_calc_overlap(fz_rect r1, fz_rect r2)
{
    if (r1.x0 == r1.x1 || r1.y0 == r1.y1)
        return 0.0f;
    fz_rect isect = fz_intersect_rect(r1, r2);
    return (isect.x1 - isect.x0) * (isect.y1 - isect.y0) / ((r1.x1 - r1.x0) * (r1.y1 - r1.y0));
}

class RenderedFitzBitmap : public RenderedBitmap {
public:
    RenderedFitzBitmap(fz_context *ctx, fz_pixmap *pixmap);
};

RenderedFitzBitmap::RenderedFitzBitmap(fz_context *ctx, fz_pixmap *pixmap) :
    RenderedBitmap(NULL, SizeI(pixmap->w, pixmap->h))
{
    int paletteSize = 0;
    bool hasPalette = false;

    int w = pixmap->w;
    int h = pixmap->h;
    int rows8 = ((w + 3) / 4) * 4;

    /* BGRA is a GDI compatible format */
    fz_pixmap *bgrPixmap;
    fz_try(ctx) {
        bgrPixmap = fz_new_pixmap_with_rect(ctx, fz_find_device_colorspace("DeviceBGR"), fz_bound_pixmap(pixmap));
        fz_convert_pixmap(ctx, pixmap, bgrPixmap);
    }
    fz_catch(ctx) {
        return;
    }

    assert(bgrPixmap->n == 4);

    BITMAPINFO *bmi = (BITMAPINFO *)calloc(1, sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));

    // always try to produce an 8-bit palette for saving some memory
    unsigned char *bmpData = (unsigned char *)calloc(rows8, h);
    if (bmpData)
    {
        unsigned char *dest = bmpData;
        unsigned char *source = bgrPixmap->samples;

        for (int j = 0; j < h; j++)
        {
            for (int i = 0; i < w; i++)
            {
                RGBQUAD c = { 0 };

                c.rgbBlue = *source++;
                c.rgbGreen = *source++;
                c.rgbRed = *source++;
                source++;

                /* find this color in the palette */
                int k;
                for (k = 0; k < paletteSize; k++)
                    if (*(int *)&bmi->bmiColors[k] == *(int *)&c)
                        break;
                /* add it to the palette if it isn't in there and if there's still space left */
                if (k == paletteSize)
                {
                    if (k >= 256)
                        goto ProducingPaletteDone;
                    *(int *)&bmi->bmiColors[paletteSize] = *(int *)&c;
                    paletteSize++;
                }
                /* 8-bit data consists of indices into the color palette */
                *dest++ = k;
            }
            dest += rows8 - w;
        }
ProducingPaletteDone:
        hasPalette = paletteSize < 256;
    }

    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = w;
    bmi->bmiHeader.biHeight = -h;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biCompression = BI_RGB;
    bmi->bmiHeader.biBitCount = hasPalette ? 8 : 32;
    bmi->bmiHeader.biSizeImage = h * (hasPalette ? rows8 : w * 4);
    bmi->bmiHeader.biClrUsed = hasPalette ? paletteSize : 0;

    HDC hDC = GetDC(NULL);
    hbmp = CreateDIBitmap(hDC, &bmi->bmiHeader, CBM_INIT,
        hasPalette ? bmpData : bgrPixmap->samples, bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hDC);

    fz_drop_pixmap(ctx, bgrPixmap);
    free(bmi);
    free(bmpData);
}

fz_stream *fz_open_file2(fz_context *ctx, const TCHAR *filePath)
{
    size_t fileSize = file::GetSize(filePath);
    // load small files entirely into memory so that they can be
    // overwritten even by programs that don't open files with FILE_SHARE_READ
    if (fileSize < MAX_MEMORY_FILE_SIZE) {
        fz_buffer *data = NULL;
        fz_stream *file = NULL;
        fz_var(data);
        fz_try(ctx) {
            data = fz_new_buffer(ctx, (int)fileSize);
            if (file::ReadAll(filePath, (char *)data->data, (data->len = (int)fileSize)))
                file = fz_open_buffer(ctx, data);
        }
        fz_catch(ctx) {
            file = NULL;
        }
        fz_drop_buffer(ctx, data);
        if (file)
            return file;
    }

#ifdef UNICODE
    return fz_open_file_w(ctx, filePath);
#else
    return fz_open_file(ctx, filePath);
#endif
}

unsigned char *fz_extract_stream_data(fz_stream *stream, size_t *cbCount)
{
    fz_seek(stream, 0, 2);
    int fileLen = fz_tell(stream);
    fz_seek(stream, 0, 0);

    fz_buffer *buffer = fz_read_all(stream, fileLen);
    assert(fileLen == buffer->len);

    unsigned char *data = (unsigned char *)memdup(buffer->data, buffer->len);
    if (cbCount)
        *cbCount = buffer->len;

    fz_drop_buffer(stream->ctx, buffer);

    if (!data)
        fz_throw(stream->ctx, "OOM in fz_extract_stream_data");
    return data;
}

void fz_stream_fingerprint(fz_stream *file, unsigned char digest[16])
{
    int fileLen;
    fz_buffer *buffer;

    fz_try(file->ctx) {
        fz_seek(file, 0, 2);
        fileLen = fz_tell(file);
        fz_seek(file, 0, 0);
        buffer = fz_read_all(file, fileLen);
    }
    fz_catch(file->ctx) {
        fz_warn(file->ctx, "couldn't read stream data, using a NULL fingerprint instead");
        ZeroMemory(digest, 16);
        return;
    }
    assert(fileLen == buffer->len);

    CalcMD5Digest(buffer->data, buffer->len, digest);

    fz_drop_buffer(file->ctx, buffer);
}

TCHAR *fz_span_to_tchar(fz_text_span *text, TCHAR *lineSep, RectI **coords_out=NULL)
{
    size_t lineSepLen = str::Len(lineSep);
    size_t textLen = 0;
    for (fz_text_span *span = text; span; span = span->next)
        textLen += span->len + lineSepLen;

    TCHAR *content = SAZA(TCHAR, textLen + 1);
    if (!content)
        return NULL;

    RectI *destRect = NULL;
    if (coords_out) {
        destRect = *coords_out = new RectI[textLen];
        if (!*coords_out) {
            free(content);
            return NULL;
        }
    }

    TCHAR *dest = content;
    for (fz_text_span *span = text; span; span = span->next) {
        for (int i = 0; i < span->len; i++) {
#ifdef UNICODE
            *dest = span->text[i].c;
#else
            WCHAR c = span->text[i].c;
            if (!WideCharToMultiByte(CP_ACP, 0, &c, 1, dest, 1, NULL, NULL))
                *dest = '?';
#endif
            if (*dest < 32)
                *dest = '?';
            dest++;
            if (destRect)
                *destRect++ = fz_bbox_to_RectI(span->text[i].bbox);
        }
        if (!span->eol && span->next)
            continue;
        lstrcpy(dest, lineSep);
        dest += lineSepLen;
        if (destRect) {
            ZeroMemory(destRect, lineSepLen * sizeof(fz_bbox));
            destRect += lineSepLen;
        }
    }

    return content;
}

extern "C" static int read_istream(fz_stream *stm, unsigned char *buf, int len)
{
    ULONG cbRead = len;
    HRESULT res = ((IStream *)stm->state)->Read(buf, len, &cbRead);
    if (FAILED(res))
        fz_throw(stm->ctx, "IStream read error: %x", res);
    return (int)cbRead;
}

extern "C" static void seek_istream(fz_stream *stm, int offset, int whence)
{
    LARGE_INTEGER off;
    ULARGE_INTEGER n;
    off.QuadPart = offset;
    HRESULT res = ((IStream *)stm->state)->Seek(off, whence, &n);
    if (FAILED(res))
        fz_throw(stm->ctx, "IStream seek error: %x", res);
    if (n.HighPart != 0 || n.LowPart > INT_MAX)
        fz_throw(stm->ctx, "documents beyond 2GB aren't supported");
    stm->pos = n.LowPart;
    stm->rp = stm->wp = stm->bp;
}

extern "C" static void close_istream(fz_context *ctx, void *state)
{
    ((IStream *)state)->Release();
}

fz_stream *fz_open_istream(fz_context *ctx, IStream *stream);

extern "C" static fz_stream *reopen_istream(fz_context *ctx, fz_stream *stm)
{
    ScopedComPtr<IStream> stream2;
    HRESULT res = ((IStream *)stm->state)->Clone(&stream2);
    if (E_NOTIMPL == res)
        fz_throw(ctx, "IStream doesn't support cloning");
    if (FAILED(res))
        fz_throw(ctx, "IStream clone error: %x", res);
    return fz_open_istream(ctx, stream2);
}

fz_stream *fz_open_istream(fz_context *ctx, IStream *stream)
{
    if (!stream)
        return NULL;

    LARGE_INTEGER zero = { 0 };
    HRESULT res = stream->Seek(zero, STREAM_SEEK_SET, NULL);
    if (FAILED(res))
        fz_throw(ctx, "IStream seek error: %x", res);
    stream->AddRef();

    fz_stream *stm = fz_new_stream(ctx, stream, read_istream, close_istream);
    stm->seek = seek_istream;
    stm->reopen = reopen_istream;
    return stm;
}

fz_obj *fz_copy_str_dict(fz_context *ctx, fz_obj *dict)
{
    fz_obj *copy = fz_copy_dict(ctx, dict);
    for (int i = 0; i < fz_dict_len(copy); i++) {
        fz_obj *val = fz_dict_get_val(copy, i);
        // resolve all indirect references
        if (fz_is_indirect(val)) {
            fz_obj *val2 = fz_new_string(ctx, fz_to_str_buf(val), fz_to_str_len(val));
            fz_dict_put(copy, fz_dict_get_key(copy, i), val2);
            fz_drop_obj(val2);
        }
    }
    return copy;
}

fz_matrix fz_create_view_ctm(fz_rect mediabox, float zoom, int rotation)
{
    fz_matrix ctm = fz_identity;

    assert(0 == mediabox.x0 && 0 == mediabox.y0);
    rotation = (rotation + 360) % 360;
    if (90 == rotation)
        ctm = fz_concat(ctm, fz_translate(0, -mediabox.y1));
    else if (180 == rotation)
        ctm = fz_concat(ctm, fz_translate(-mediabox.x1, -mediabox.y1));
    else if (270 == rotation)
        ctm = fz_concat(ctm, fz_translate(-mediabox.x1, 0));

    ctm = fz_concat(ctm, fz_scale(zoom, zoom));
    ctm = fz_concat(ctm, fz_rotate((float)rotation));

    assert(fz_matrix_expansion(ctm) > 0);
    if (fz_matrix_expansion(ctm) == 0)
        return fz_identity;

    return ctm;
}

struct LinkRectList {
    StrVec links;
    Vec<fz_rect> coords;
};

static bool LinkifyCheckMultiline(TCHAR *pageText, TCHAR *pos, RectI *coords)
{
    // multiline links end in a non-alphanumeric character and continue on a line
    // that starts left and only slightly below where the current line ended
    // (and that doesn't start with http itself)
    return
        '\n' == *pos && pos > pageText && *(pos + 1) &&
        !_istalnum(pos[-1]) && !_istspace(pos[1]) &&
        coords[pos - pageText + 1].BR().y > coords[pos - pageText - 1].y &&
        coords[pos - pageText + 1].y <= coords[pos - pageText - 1].BR().y &&
        coords[pos - pageText + 1].x < coords[pos - pageText - 1].BR().x &&
        !str::StartsWith(pos + 1, _T("http"));
}

static TCHAR *LinkifyFindEnd(TCHAR *start)
{
    TCHAR *end;

    // look for the end of the URL (ends in a space preceded maybe by interpunctuation)
    for (end = start; *end && !_istspace(*end); end++);
    if (',' == end[-1] || '.' == end[-1])
        end--;
    // also ignore a closing parenthesis, if the URL doesn't contain any opening one
    if (')' == end[-1] && (!str::FindChar(start, '(') || str::FindChar(start, '(') >= end))
        end--;

    return end;
}

static TCHAR *LinkifyMultilineText(LinkRectList *list, TCHAR *pageText, TCHAR *start, RectI *coords)
{
    size_t lastIx = list->coords.Count() - 1;
    ScopedMem<TCHAR> uri(list->links.At(lastIx));
    TCHAR *end = start;
    bool multiline = false;

    do {
        end = LinkifyFindEnd(start);
        multiline = LinkifyCheckMultiline(pageText, end, coords);
        *end = 0;

        uri.Set(str::Join(uri, start));
        RectI bbox = coords[start - pageText].Union(coords[end - pageText - 1]);
        list->coords.Append(fz_RectD_to_rect(bbox.Convert<double>()));

        start = end + 1;
    } while (multiline);

    // update the link URL for all partial links
    list->links.At(lastIx) = str::Dup(uri);
    for (size_t i = lastIx + 1; i < list->coords.Count(); i++)
        list->links.Append(str::Dup(uri));

    return end;
}

// cf. http://weblogs.mozillazine.org/gerv/archives/2011/05/html5_email_address_regexp.html
inline bool IsEmailUsernameChar(TCHAR c)
{
    return _istalnum(c) || str::FindChar(_T(".!#$%&'*+-/=?^`{|}~"), c);
}
inline bool IsEmailDomainChar(TCHAR c)
{
    return _istalnum(c) || '-' == c;
}

static TCHAR *LinkifyFindEmail(TCHAR *pageText, TCHAR *at)
{
    TCHAR *start;
    for (start = at; start > pageText && IsEmailUsernameChar(*(start - 1)); start--);
    return start != at ? start : NULL;
}

static TCHAR *LinkifyEmailAddress(TCHAR *start)
{
    TCHAR *end;
    for (end = start; IsEmailUsernameChar(*end); end++);
    if (end == start || *end != '@' || !IsEmailDomainChar(*(end + 1)))
        return NULL;
    for (end++; IsEmailDomainChar(*end); end++);
    if ('.' != *end || !IsEmailDomainChar(*(end + 1)))
        return NULL;
    do {
        for (end++; IsEmailDomainChar(*end); end++);
    } while ('.' == *end && IsEmailDomainChar(*(end + 1)));
    return end;
}

// caller needs to delete the result
static LinkRectList *LinkifyText(TCHAR *pageText, RectI *coords)
{
    LinkRectList *list = new LinkRectList;

    for (TCHAR *start = pageText; *start; start++) {
        TCHAR *end = NULL;
        bool multiline = false;
        const TCHAR *protocol = NULL;

        if ('@' == *start) {
            // potential email address without mailto:
            TCHAR *email = LinkifyFindEmail(pageText, start);
            end = email ? LinkifyEmailAddress(email) : NULL;
            protocol = _T("mailto:");
            if (end != NULL)
                start = email;
        }
        else if (start > pageText && ('/' == start[-1] || _istalnum(start[-1]))) {
            // hyperlinks must not be preceded by a slash (indicates a different protocol)
            // or an alphanumeric character (indicates part of a different protocol)
        }
        else if ('h' == *start && str::Parse(start, _T("http%?s://"))) {
            end = LinkifyFindEnd(start);
            multiline = LinkifyCheckMultiline(pageText, end, coords);
        }
        else if ('w' == *start && str::StartsWith(start, _T("www."))) {
            end = LinkifyFindEnd(start);
            multiline = LinkifyCheckMultiline(pageText, end, coords);
            protocol = _T("http://");
            // ignore www. links without a top-level domain
            if (end - start <= 4 || !multiline && (!_tcschr(start + 5, '.') || _tcschr(start + 5, '.') > end))
                end = NULL;
        }
        else if ('m' == *start && str::StartsWith(start, _T("mailto:"))) {
            end = LinkifyEmailAddress(start + 7);
        }
        if (!end)
            continue;

        *end = 0;
        TCHAR *uri = protocol ? str::Join(protocol, start) : str::Dup(start);
        list->links.Append(uri);
        RectI bbox = coords[start - pageText].Union(coords[end - pageText - 1]);
        list->coords.Append(fz_RectD_to_rect(bbox.Convert<double>()));
        if (multiline)
            end = LinkifyMultilineText(list, pageText, end + 1, coords);

        start = end;
    }

    return list;
}

static fz_link *FixupPageLinks(fz_link *root)
{
    // Links in PDF documents are added from bottom-most to top-most,
    // i.e. links that appear later in the list should be preferred
    // to links appearing before. Since we search from the start of
    // the (single-linked) list, we have to reverse the order of links
    // (cf. http://code.google.com/p/sumatrapdf/issues/detail?id=1303 )
    fz_link *new_root = NULL;
    while (root) {
        fz_link *tmp = root->next;
        root->next = new_root;
        new_root = root;
        root = tmp;

        // there are PDFs that have x,y positions in reverse order, so fix them up
        fz_link *link = new_root;
        if (link->rect.x0 > link->rect.x1)
            swap(link->rect.x0, link->rect.x1);
        if (link->rect.y0 > link->rect.y1)
            swap(link->rect.y0, link->rect.y1);
        assert(link->rect.x1 >= link->rect.x0);
        assert(link->rect.y1 >= link->rect.y0);
    }
    return new_root;
}

class SimpleDest : public PageDestination {
    int pageNo;
    RectD rect;

public:
    SimpleDest(int pageNo, RectD rect) : pageNo(pageNo), rect(rect) { }

    virtual const char *GetDestType() const { return NULL; }
    virtual int GetDestPageNo() const { return pageNo; }
    virtual RectD GetDestRect() const { return rect; }
};

struct FitzImagePos {
    fz_pixmap *image;
    fz_rect rect;

    FitzImagePos(fz_pixmap *image=NULL, fz_rect rect=fz_unit_rect) :
        image(image), rect(rect) { }
};

struct ListInspectionData {
    Vec<FitzImagePos> *images;
    bool requiresBlending;
    size_t mem_estimate;
};

extern "C" static void
fz_inspection_fill_image(fz_device *dev, fz_pixmap *image, fz_matrix ctm, float alpha)
{
    if (image && image->free_samples)
        ((ListInspectionData *)dev->user)->mem_estimate += image->w * image->h * image->n;

    // extract rectangles for images a user might want to extract
    // TODO: try to better distinguish images a user might actually want to extract
    if (image->w < 16 || image->h < 16)
        return;
    fz_rect rect = fz_transform_rect(ctm, fz_unit_rect);
    if (!fz_is_empty_rect(rect))
        ((ListInspectionData *)dev->user)->images->Append(FitzImagePos(image, rect));
}

extern "C" static void
fz_inspection_fill_image_mask(fz_device *dev, fz_pixmap *image, fz_matrix ctm, fz_colorspace *colorspace, float *color, float alpha)
{
    if (image && image->free_samples)
        ((ListInspectionData *)dev->user)->mem_estimate += image->w * image->h * image->n;
}

extern "C" static void
fz_inspection_clip_image_mask(fz_device *dev, fz_pixmap *image, fz_rect *rect, fz_matrix ctm)
{
    if (image && image->free_samples)
        ((ListInspectionData *)dev->user)->mem_estimate += image->w * image->h * image->n;
}

extern "C" static void
fz_inspection_begin_group(fz_device *dev, fz_rect rect, int isolated, int knockout, int blendmode, float alpha)
{
    if (blendmode != FZ_BLEND_NORMAL || alpha != 1.0f || !isolated || knockout)
        ((ListInspectionData *)dev->user)->requiresBlending = true;
}

extern "C" static void 
fz_inspection_free(fz_device *dev)
{
    // images are extracted in bottom-to-top order, but for GetElements
    // we want to access them in top-to-bottom order (since images at
    // the bottom might not be visible at all)
    ((ListInspectionData *)dev->user)->images->Reverse();
}

static fz_device *fz_new_inspection_device(fz_context *ctx, ListInspectionData *data)
{
    fz_device *dev = fz_new_device(ctx, data);
    dev->fill_image = fz_inspection_fill_image;
    dev->fill_image_mask = fz_inspection_fill_image_mask;
    dev->clip_image_mask = fz_inspection_clip_image_mask;
    dev->begin_group = fz_inspection_begin_group;
    dev->free_user = fz_inspection_free;
    return dev;
}

// Ensure that fz_accelerate is called before using Fitz the first time.
class FitzAccelerator { public: FitzAccelerator() { fz_accelerate(); } };
FitzAccelerator _globalAccelerator;

extern "C" {
#include <mupdf.h>
}

namespace str {
    namespace conv {

inline TCHAR *FromPdf(fz_obj *obj)
{
    ScopedMem<WCHAR> str(SAZA(WCHAR, fz_to_str_len(obj) + 1));
    pdf_to_ucs2_buf((unsigned short *)str.Get(), obj);
    return str::conv::FromWStr(str);
}

    }
}

// Note: make sure to only call with ctxAccess
fz_outline *pdf_loadattachments(pdf_document *xref)
{
    fz_obj *dict = pdf_load_name_tree(xref, "EmbeddedFiles");
    if (!dict)
        return NULL;

    fz_outline root = { 0 }, *node = &root;
    for (int i = 0; i < fz_dict_len(dict); i++) {
        fz_obj *name = fz_dict_get_key(dict, i);
        fz_obj *dest = fz_dict_get_val(dict, i);
        fz_obj *embedded = fz_dict_getsa(fz_dict_gets(dest, "EF"), "DOS", "F");
        if (!embedded)
            continue;

        node = node->next = (fz_outline *)fz_malloc_struct(xref->ctx, fz_outline);
        ZeroMemory(node, sizeof(fz_outline));
        node->ctx = xref->ctx;
        node->title = fz_strdup(xref->ctx, fz_to_name(name));
        node->dest.kind = FZ_LINK_LAUNCH;
        node->dest.ld.launch.file_spec = pdf_file_spec_to_str(xref->ctx, dest);
        node->dest.ld.launch.new_window = 1;
        node->dest.ld.launch.embedded = fz_keep_obj(embedded);
    }
    fz_drop_obj(dict);

    return root.next;
}

struct PageLabelInfo {
    int startAt, countFrom;
    const char *type;
    fz_obj *prefix;
};

int CmpPageLabelInfo(const void *a, const void *b)
{
    return ((PageLabelInfo *)a)->startAt - ((PageLabelInfo *)b)->startAt;
}

TCHAR *FormatPageLabel(const char *type, int pageNo, const TCHAR *prefix)
{
    if (str::Eq(type, "D"))
        return str::Format(_T("%s%d"), prefix, pageNo);
    if (str::EqI(type, "R")) {
        // roman numbering style
        ScopedMem<TCHAR> number(str::FormatRomanNumeral(pageNo));
        if (*type == 'r')
            str::ToLower(number.Get());
        return str::Format(_T("%s%s"), prefix, number);
    }
    if (str::EqI(type, "A")) {
        // alphabetic numbering style (A..Z, AA..ZZ, AAA..ZZZ, ...)
        str::Str<TCHAR> number;
        number.Append('A' + (pageNo - 1) % 26);
        for (int i = 0; i < (pageNo - 1) / 26; i++)
            number.Append(number.At(0));
        if (*type == 'a')
            str::ToLower(number.Get());
        return str::Format(_T("%s%s"), prefix, number.Get());
    }
    return str::Dup(prefix);
}

void BuildPageLabelRec(fz_obj *node, int pageCount, Vec<PageLabelInfo>& data)
{
    fz_obj *obj;
    if ((obj = fz_dict_gets(node, "Kids")) && !fz_dict_mark(node)) {
        for (int i = 0; i < fz_array_len(obj); i++)
            BuildPageLabelRec(fz_array_get(obj, i), pageCount, data);
        fz_dict_unmark(node);
    }
    else if ((obj = fz_dict_gets(node, "Nums"))) {
        for (int i = 0; i < fz_array_len(obj); i += 2) {
            fz_obj *info = fz_array_get(obj, i + 1);
            PageLabelInfo pli;
            pli.startAt = fz_to_int(fz_array_get(obj, i)) + 1;
            if (pli.startAt < 1)
                continue;

            pli.type = fz_to_name(fz_dict_gets(info, "S"));
            pli.prefix = fz_dict_gets(info, "P");
            pli.countFrom = fz_to_int(fz_dict_gets(info, "St"));
            if (pli.countFrom < 1)
                pli.countFrom = 1;
            data.Append(pli);
        }
    }
}

StrVec *BuildPageLabelVec(fz_obj *root, int pageCount)
{
    Vec<PageLabelInfo> data;
    BuildPageLabelRec(root, pageCount, data);
    data.Sort(CmpPageLabelInfo);

    if (data.Count() == 0)
        return NULL;

    if (data.Count() == 1 && data.At(0).startAt == 1 && data.At(0).countFrom == 1 &&
        !data.At(0).prefix && str::Eq(data.At(0).type, "D")) {
        // this is the default case, no need for special treatment
        return NULL;
    }

    StrVec *labels = new StrVec();
    labels->AppendBlanks(pageCount);

    for (size_t i = 0; i < data.Count() && data.At(i).startAt <= pageCount; i++) {
        int secLen = pageCount + 1 - data.At(i).startAt;
        if (i < data.Count() - 1 && data.At(i + 1).startAt <= pageCount)
            secLen = data.At(i + 1).startAt - data.At(i).startAt;
        ScopedMem<TCHAR> prefix(str::conv::FromPdf(data.At(i).prefix));
        for (int j = 0; j < secLen; j++) {
            free(labels->At(data.At(i).startAt + j - 1));
            labels->At(data.At(i).startAt + j - 1) =
                FormatPageLabel(data.At(i).type, data.At(i).countFrom + j, prefix);
        }
    }

    for (int ix = 0; (ix = labels->Find(NULL, ix)) != -1; ix++)
        labels->At(ix) = str::Dup(_T(""));

    // ensure that all page labels are unique (by appending a number to duplicates)
    StrVec dups(*labels);
    dups.Sort();
    for (size_t i = 1; i < dups.Count(); i++) {
        if (!str::Eq(dups.At(i), dups.At(i - 1)))
            continue;
        int ix = labels->Find(dups.At(i)), counter = 0;
        while ((ix = labels->Find(dups.At(i), ix + 1)) != -1) {
            ScopedMem<TCHAR> unique;
            do {
                unique.Set(str::Format(_T("%s.%d"), dups.At(i), ++counter));
            } while (labels->Find(unique) != -1);
            str::ReplacePtr(&labels->At(ix), unique);
        }
        for (; i + 1 < dups.Count() && str::Eq(dups.At(i), dups.At(i + 1)); i++);
    }

    return labels;
}

///// Above are extensions to Fitz and MuPDF, now follows PdfEngine /////

struct PdfPageRun {
    pdf_page *page;
    fz_display_list *list;
    size_t size_est;
    bool req_blending;
    int refs;
};

class PdfTocItem;
class PdfLink;
class PdfImage;

class CPdfEngine : public PdfEngine {
    friend PdfEngine;
    friend PdfLink;
    friend PdfImage;

public:
    CPdfEngine();
    virtual ~CPdfEngine();
    virtual CPdfEngine *Clone();

    virtual const TCHAR *FileName() const { return _fileName; };
    virtual int PageCount() const {
        // make sure that pdf_load_page_tree is called as soon as
        // _doc is defined, so that pdf_count_pages can't throw
        return _doc ? pdf_count_pages(_doc) : 0;
    }

    virtual RectD PageMediabox(int pageNo);
    virtual RectD PageContentBox(int pageNo, RenderTarget target=Target_View);

    virtual RenderedBitmap *RenderBitmap(int pageNo, float zoom, int rotation,
                         RectD *pageRect=NULL, /* if NULL: defaults to the page's mediabox */
                         RenderTarget target=Target_View);
    virtual bool RenderPage(HDC hDC, RectI screenRect, int pageNo, float zoom, int rotation,
                         RectD *pageRect=NULL, RenderTarget target=Target_View) {
        return RenderPage(hDC, GetPdfPage(pageNo), screenRect, NULL, zoom, rotation, pageRect, target);
    }

    virtual PointD Transform(PointD pt, int pageNo, float zoom, int rotation, bool inverse=false);
    virtual RectD Transform(RectD rect, int pageNo, float zoom, int rotation, bool inverse=false);

    virtual unsigned char *GetFileData(size_t *cbCount);
    virtual TCHAR * ExtractPageText(int pageNo, TCHAR *lineSep, RectI **coords_out=NULL,
                                    RenderTarget target=Target_View);
    virtual bool IsImagePage(int pageNo);
    virtual PageLayoutType PreferredLayout();
    virtual TCHAR *GetProperty(char *name);

    virtual bool IsPrintingAllowed() {
        return pdf_has_permission(_doc, PDF_PERM_PRINT);
    }
    virtual bool IsCopyingTextAllowed() {
        return pdf_has_permission(_doc, PDF_PERM_COPY);
    }

    virtual float GetFileDPI() const { return 72.0f; }
    virtual const TCHAR *GetDefaultFileExt() const { return _T(".pdf"); }

    virtual bool BenchLoadPage(int pageNo) { return GetPdfPage(pageNo) != NULL; }

    virtual Vec<PageElement *> *GetElements(int pageNo);
    virtual PageElement *GetElementAtPos(int pageNo, PointD pt);

    virtual PageDestination *GetNamedDest(const TCHAR *name);
    virtual bool HasTocTree() const {
        return outline != NULL || attachments != NULL;
    }
    virtual DocTocItem *GetTocTree();

    virtual bool HasPageLabels() { return _pagelabels != NULL; }
    virtual TCHAR *GetPageLabel(int pageNo);
    virtual int GetPageByLabel(const TCHAR *label);

    virtual bool IsPasswordProtected() const { return isProtected; }
    virtual char *GetDecryptionKey() const;

protected:
    const TCHAR *_fileName;
    char *_decryptionKey;
    bool isProtected;

    // make sure to never ask for pagesAccess in an ctxAccess
    // protected critical section in order to avoid deadlocks
    CRITICAL_SECTION ctxAccess;
    fz_context *    ctx;
    pdf_document *  _doc;

    CRITICAL_SECTION pagesAccess;
    pdf_page **     _pages;

    virtual bool    Load(const TCHAR *fileName, PasswordUI *pwdUI=NULL);
    virtual bool    Load(IStream *stream, PasswordUI *pwdUI=NULL);
    bool            Load(fz_stream *stm, PasswordUI *pwdUI=NULL);
    bool            LoadFromStream(fz_stream *stm, PasswordUI *pwdUI=NULL);
    bool            FinishLoading();
    pdf_page      * GetPdfPage(int pageNo, bool failIfBusy=false);
    int             GetPageNo(pdf_page *page);
    fz_matrix       viewctm(int pageNo, float zoom, int rotation) {
        return fz_create_view_ctm(fz_RectD_to_rect(PageMediabox(pageNo)), zoom, rotation);
    }
    fz_matrix       viewctm(pdf_page *page, float zoom, int rotation) {
        return fz_create_view_ctm(pdf_bound_page(_doc, page), zoom, rotation);
    }
    bool            RenderPage(HDC hDC, pdf_page *page, RectI screenRect,
                               fz_matrix *ctm, float zoom, int rotation,
                               RectD *pageRect, RenderTarget target);
    bool            RequiresBlending(pdf_page *page);
    TCHAR         * ExtractPageText(pdf_page *page, TCHAR *lineSep, RectI **coords_out=NULL,
                                    RenderTarget target=Target_View, bool cacheRun=false);

    Vec<PdfPageRun*>runCache; // ordered most recently used first
    PdfPageRun    * CreatePageRun(pdf_page *page, fz_display_list *list);
    PdfPageRun    * GetPageRun(pdf_page *page, bool tryOnly=false);
    bool            RunPage(pdf_page *page, fz_device *dev, fz_matrix ctm,
                            RenderTarget target=Target_View,
                            fz_bbox clipbox=fz_infinite_bbox, bool cacheRun=true);
    void            DropPageRun(PdfPageRun *run, bool forceRemove=false);

    PdfTocItem   *  BuildTocTree(fz_outline *entry, int& idCounter);
    void            LinkifyPageText(pdf_page *page);
    pdf_annot    ** ProcessPageAnnotations(pdf_page *page);
    RenderedBitmap *GetPageImage(int pageNo, RectD rect, size_t imageIx);

    bool            SaveEmbedded(fz_obj *obj, LinkSaverUI& saveUI);

    RectD         * _mediaboxes;
    fz_outline    * outline;
    fz_outline    * attachments;
    fz_obj        * _info;
    StrVec        * _pagelabels;
    pdf_annot   *** pageComments;
    fz_rect      ** imageRects;
};

class PdfLink : public PageElement, public PageDestination {
    CPdfEngine *engine;
    fz_link_dest *link; // owned by an fz_link or fz_outline
    RectD rect;
    int pageNo;
    PointD pt;

public:
    PdfLink(CPdfEngine *engine, fz_link_dest *link,
        fz_rect rect=fz_empty_rect, int pageNo=-1, fz_point *pt=NULL) :
        engine(engine), link(link), rect(fz_rect_to_RectD(rect)), pageNo(pageNo) {
        // cursor coordinates for IsMap URI links
        if (pt)
            this->pt = PointD(pt->x, pt->y);
    }

    virtual PageElementType GetType() const { return Element_Link; }
    virtual int GetPageNo() const { return pageNo; }
    virtual RectD GetRect() const { return rect; }
    virtual TCHAR *GetValue() const;
    virtual PageDestination *AsLink() { return this; }

    virtual const char *GetDestType() const;
    virtual int GetDestPageNo() const;
    virtual RectD GetDestRect() const;
    virtual TCHAR *GetDestValue() const { return GetValue(); }
    virtual TCHAR *GetDestName() const;

    virtual bool SaveEmbedded(LinkSaverUI& saveUI);
};

class PdfComment : public PageElement {
    TCHAR *content;
    RectD rect;
    int pageNo;

public:
    PdfComment(const TCHAR *content, RectD rect, int pageNo=-1) :
        content(str::Dup(content)), rect(rect), pageNo(pageNo) { }
    virtual ~PdfComment() {
        free(content);
    }

    virtual PageElementType GetType() const { return Element_Comment; }
    virtual int GetPageNo() const { return pageNo; }
    virtual RectD GetRect() const { return rect; }
    virtual TCHAR *GetValue() const { return str::Dup(content); }
};

class PdfTocItem : public DocTocItem {
    PdfLink link;

public:
    PdfTocItem(TCHAR *title, PdfLink link) : DocTocItem(title), link(link) { }

    virtual PageDestination *GetLink() { return &link; }
};

class PdfImage : public PageElement {
    CPdfEngine *engine;
    int pageNo;
    RectD rect;
    size_t imageIx;

public:
    PdfImage(CPdfEngine *engine, int pageNo, fz_rect rect, size_t imageIx) :
        engine(engine), pageNo(pageNo), rect(fz_rect_to_RectD(rect)), imageIx(imageIx) { }

    virtual PageElementType GetType() const { return Element_Image; }
    virtual int GetPageNo() const { return pageNo; }
    virtual RectD GetRect() const { return rect; }
    virtual TCHAR *GetValue() const { return NULL; }

    virtual RenderedBitmap *GetImage() {
        return engine->GetPageImage(pageNo, rect, imageIx);
    }
};

CPdfEngine::CPdfEngine() : _fileName(NULL), _doc(NULL),
    _pages(NULL), _mediaboxes(NULL), _info(NULL),
    outline(NULL), attachments(NULL), _pagelabels(NULL),
    _decryptionKey(NULL), isProtected(false),
    pageComments(NULL), imageRects(NULL)    
{
    InitializeCriticalSection(&pagesAccess);
    InitializeCriticalSection(&ctxAccess);

    ctx = fz_new_context(NULL, MAX_CONTEXT_MEMORY);
}

CPdfEngine::~CPdfEngine()
{
    EnterCriticalSection(&pagesAccess);
    EnterCriticalSection(&ctxAccess);

    if (_pages) {
        for (int i = 0; i < PageCount(); i++) {
            if (_pages[i])
                pdf_free_page(ctx, _pages[i]);
        }
        free(_pages);
    }

    if (outline)
        fz_free_outline(outline);
    if (attachments)
        fz_free_outline(attachments);
    if (_info)
        fz_drop_obj(_info);

    if (pageComments) {
        for (int i = 0; i < PageCount(); i++)
            free(pageComments[i]);
        free(pageComments);
    }
    if (imageRects) {
        for (int i = 0; i < PageCount(); i++)
            free(imageRects[i]);
        free(imageRects);
    }

    if (_doc) {
        pdf_close_document(_doc);
        _doc = NULL;
    }

    while (runCache.Count() > 0) {
        assert(runCache.Last()->refs == 1);
        DropPageRun(runCache.Last(), true);
    }

    delete[] _mediaboxes;
    delete _pagelabels;
    free((void*)_fileName);
    free(_decryptionKey);

    fz_free_context(ctx);

    LeaveCriticalSection(&ctxAccess);
    DeleteCriticalSection(&ctxAccess);
    LeaveCriticalSection(&pagesAccess);
    DeleteCriticalSection(&pagesAccess);
}

class PasswordCloner : public PasswordUI {
    unsigned char *cryptKey;

public:
    PasswordCloner(unsigned char *cryptKey) : cryptKey(cryptKey) { }

    virtual TCHAR * GetPassword(const TCHAR *fileName, unsigned char *fileDigest,
                                unsigned char decryptionKeyOut[32], bool *saveKey)
    {
        memcpy(decryptionKeyOut, cryptKey, 32);
        *saveKey = true;
        return NULL;
    }
};

CPdfEngine *CPdfEngine::Clone()
{
    ScopedCritSec scope(&ctxAccess);

    // use this document's encryption key (if any) to load the clone
    PasswordCloner *pwdUI = NULL;
    if (_doc->crypt)
        pwdUI = new PasswordCloner(pdf_get_crypt_key(_doc));

    CPdfEngine *clone = new CPdfEngine();
    if (!clone || !clone->Load(_doc->file, pwdUI)) {
        delete clone;
        delete pwdUI;
        return NULL;
    }
    delete pwdUI;

    if (_fileName)
        clone->_fileName = str::Dup(_fileName);
    if (!_decryptionKey && _doc->crypt) {
        delete clone->_decryptionKey;
        clone->_decryptionKey = NULL;
    }

    return clone;
}

static const TCHAR *findEmbedMarks(const TCHAR *fileName)
{
    const TCHAR *embedMarks = NULL;

    int colonCount = 0;
    for (const TCHAR *c = fileName + str::Len(fileName) - 1; c > fileName; c--) {
        if (*c == ':') {
            if (!ChrIsDigit(*(c + 1)))
                break;
            if (++colonCount % 2 == 0)
                embedMarks = c;
        }
        else if (!ChrIsDigit(*c))
            break;
    }

    return embedMarks;
}

bool CPdfEngine::Load(const TCHAR *fileName, PasswordUI *pwdUI)
{
    assert(!_fileName && !_doc && ctx);
    _fileName = str::Dup(fileName);
    if (!_fileName || !ctx)
        return false;

    fz_stream *file;
    // File names ending in :<digits>:<digits> are interpreted as containing
    // embedded PDF documents (the digits are :<num>:<gen> of the embedded file stream)
    TCHAR *embedMarks = (TCHAR *)findEmbedMarks(_fileName);
    if (embedMarks)
        *embedMarks = '\0';
    fz_try(ctx) {
        file = fz_open_file2(ctx, _fileName);
    }
    fz_catch(ctx) {
        file = NULL;
    }
    if (embedMarks)
        *embedMarks = ':';

OpenEmbeddedFile:
    if (!LoadFromStream(file, pwdUI))
        return false;

    if (str::IsEmpty(embedMarks))
        return FinishLoading();

    int num, gen;
    embedMarks = (TCHAR *)str::Parse(embedMarks, _T(":%d:%d"), &num, &gen);
    assert(embedMarks);
    if (!embedMarks || !pdf_is_stream(_doc, num, gen))
        return false;

    fz_buffer *buffer = NULL;
    fz_var(buffer);
    fz_try(ctx) {
        buffer = pdf_load_stream(_doc, num, gen);
        file = fz_open_buffer(ctx, buffer);
    }
    fz_always(ctx) {
        fz_drop_buffer(ctx, buffer);
    }
    fz_catch(ctx) {
        return false;
    }

    pdf_close_document(_doc);
    _doc = NULL;

    goto OpenEmbeddedFile;
}

bool CPdfEngine::Load(IStream *stream, PasswordUI *pwdUI)
{
    assert(!_fileName && !_doc && ctx);
    if (!ctx)
        return false;

    fz_stream *stm;
    fz_try(ctx) {
        stm = fz_open_istream(ctx, stream);
    }
    fz_catch(ctx) {
        return false;
    }
    if (!LoadFromStream(stm, pwdUI))
        return false;
    return FinishLoading();
}

bool CPdfEngine::Load(fz_stream *stm, PasswordUI *pwdUI)
{
    assert(!_fileName && !_doc && ctx);
    if (!ctx)
        return false;

    fz_try(ctx) {
        stm = fz_clone_stream(ctx, stm);
    }
    fz_catch(ctx) {
        return false;
    }
    if (!LoadFromStream(stm, pwdUI))
        return false;
    return FinishLoading();
}

bool CPdfEngine::LoadFromStream(fz_stream *stm, PasswordUI *pwdUI)
{
    if (!stm)
        return false;

    fz_try(ctx) {
        _doc = pdf_open_document_with_stream(stm);
    }
    fz_always(ctx) {
        fz_close(stm);
    }
    fz_catch(ctx) {
        return false;
    }

    isProtected = pdf_needs_password(_doc);
    if (!isProtected)
        return true;

    if (!pwdUI)
        return false;

    unsigned char digest[16 + 32] = { 0 };
    fz_stream_fingerprint(_doc->file, digest);

    bool ok = false, saveKey = false;
    for (int i = 0; !ok && i < 3; i++) {
        ScopedMem<TCHAR> pwd(pwdUI->GetPassword(_fileName, digest, pdf_get_crypt_key(_doc), &saveKey));
        if (!pwd) {
            // password not given or encryption key has been remembered
            ok = saveKey;
            break;
        }

        ScopedMem<WCHAR> wstr(str::conv::ToWStr(pwd));
        fz_try(ctx) {
            char *pwd_doc = pdf_from_ucs2(ctx, (unsigned short *)wstr.Get());
            ok = pwd_doc && pdf_authenticate_password(_doc, pwd_doc);
            fz_free(ctx, pwd_doc);
        }
        fz_catch(ctx) { }
        // try the UTF-8 password, if the PDFDocEncoding one doesn't work
        if (!ok) {
            ScopedMem<char> pwd_utf8(str::conv::ToUtf8(pwd));
            ok = pwd_utf8 && pdf_authenticate_password(_doc, pwd_utf8);
        }
        // fall back to an ANSI-encoded password as a last measure
        if (!ok) {
            ScopedMem<char> pwd_ansi(str::conv::ToAnsi(pwd));
            ok = pwd_ansi && pdf_authenticate_password(_doc, pwd_ansi);
        }
    }

    if (ok && saveKey) {
        memcpy(digest + 16, pdf_get_crypt_key(_doc), 32);
        _decryptionKey = _MemToHex(&digest);
    }

    return ok;
}

bool CPdfEngine::FinishLoading()
{
    fz_try(ctx) {
        // this calls pdf_load_page_tree(xref) which may throw
        pdf_count_pages(_doc);
    }
    fz_catch(ctx) {
        return false;
    }
    if (PageCount() == 0) {
        fz_warn(ctx, "document has no pages");
        return false;
    }

    _pages = SAZA(pdf_page *, PageCount());
    _mediaboxes = new RectD[PageCount()];
    pageComments = SAZA(pdf_annot **, PageCount());
    imageRects = SAZA(fz_rect *, PageCount());

    if (!_pages || !_mediaboxes || !pageComments || !imageRects)
        return false;

    ScopedCritSec scope(&ctxAccess);

    fz_try(ctx) {
        outline = pdf_load_outline(_doc);
    }
    fz_catch(ctx) {
        // ignore errors from pdf_loadoutline()
        // this information is not critical and checking the
        // error might prevent loading some pdfs that would
        // otherwise get displayed
        fz_warn(ctx, "Couldn't load outline");
    }
    fz_try(ctx) {
        attachments = pdf_loadattachments(_doc);
    }
    fz_catch(ctx) {
        fz_warn(ctx, "Couldn't load attachments");
    }
    fz_try(ctx) {
        // keep a copy of the Info dictionary, as accessing the original
        // isn't thread safe and we don't want to block for this when
        // displaying document properties
        _info = fz_dict_gets(_doc->trailer, "Info");
        if (_info)
            _info = fz_copy_str_dict(ctx, pdf_resolve_indirect(_info));
    }
    fz_catch(ctx) {
        fz_warn(ctx, "Couldn't load Info dictionary");
        fz_drop_obj(_info);
        _info = NULL;
    }
    fz_try(ctx) {
        fz_obj *pagelabels = fz_dict_gets(fz_dict_gets(_doc->trailer, "Root"), "PageLabels");
        if (pagelabels)
            _pagelabels = BuildPageLabelVec(pagelabels, PageCount());
    }
    fz_catch(ctx) {
        fz_warn(ctx, "Couldn't load page labels");
    }

    return true;
}

PdfTocItem *CPdfEngine::BuildTocTree(fz_outline *entry, int& idCounter)
{
    PdfTocItem *node = NULL;

    for (; entry; entry = entry->next) {
        TCHAR *name = entry->title ? str::conv::FromUtf8(entry->title) : str::Dup(_T(""));
        PdfTocItem *item = new PdfTocItem(name, PdfLink(this, &entry->dest));
        item->open = entry->is_open;
        item->id = ++idCounter;

        if (entry->dest.kind == FZ_LINK_GOTO)
            item->pageNo = entry->dest.ld.gotor.page + 1;
        if (entry->down)
            item->child = BuildTocTree(entry->down, idCounter);

        if (!node)
            node = item;
        else
            node->AddSibling(item);
    }

    return node;
}

DocTocItem *CPdfEngine::GetTocTree()
{
    PdfTocItem *node = NULL;
    int idCounter = 0;

    if (outline) {
        node = BuildTocTree(outline, idCounter);
        if (attachments)
            node->AddSibling(BuildTocTree(attachments, idCounter));
    } else if (attachments)
        node = BuildTocTree(attachments, idCounter);

    return node;
}

PageDestination *CPdfEngine::GetNamedDest(const TCHAR *name)
{
    ScopedCritSec scope(&ctxAccess);

    ScopedMem<char> name_utf8(str::conv::ToUtf8(name));
    fz_obj *dest;
    fz_try(ctx) {
        fz_obj *nameobj = fz_new_string(ctx, (char *)name_utf8, (int)str::Len(name_utf8));
        dest = pdf_lookup_dest(_doc, nameobj);
        fz_drop_obj(nameobj);
    }
    fz_catch(ctx) {
        return NULL;
    }

    PageDestination *pageDest = NULL;
    fz_link_dest ld = { FZ_LINK_NONE, 0 };
    fz_try(ctx) {
        ld = pdf_parse_link_dest(_doc, dest);
    }
    fz_catch(ctx) {
        return NULL;
    }

    if (FZ_LINK_GOTO == ld.kind) {
        // create a SimpleDest because we have to
        // free the fz_link_dest before returning
        PdfLink tmp(this, &ld);
        pageDest = new SimpleDest(tmp.GetDestPageNo(), tmp.GetDestRect());
    }
    fz_free_link_dest(ctx, &ld);

    return pageDest;
}

pdf_page *CPdfEngine::GetPdfPage(int pageNo, bool failIfBusy)
{
    if (!_pages)
        return NULL;
    if (failIfBusy)
        return _pages[pageNo-1];

    ScopedCritSec scope(&pagesAccess);

    pdf_page *page = _pages[pageNo-1];
    if (!page) {
        fz_var(page);
        EnterCriticalSection(&ctxAccess);
        fz_try(ctx) {
            page = pdf_load_page(_doc, pageNo - 1);
            _pages[pageNo-1] = page;
            LinkifyPageText(page);
            pageComments[pageNo-1] = ProcessPageAnnotations(page);
        }
        fz_catch(ctx) { }
        LeaveCriticalSection(&ctxAccess);
    }

    return page;
}

int CPdfEngine::GetPageNo(pdf_page *page)
{
    for (int i = 0; i < PageCount(); i++)
        if (page == _pages[i])
            return i + 1;
    assert(0);
    return 0;
}

PdfPageRun *CPdfEngine::CreatePageRun(pdf_page *page, fz_display_list *list)
{
    Vec<FitzImagePos> positions;
    ListInspectionData data = { &positions, false, 0 };
    fz_device *dev = NULL;

    fz_var(dev);
    fz_try(ctx) {
        dev = fz_new_inspection_device(ctx, &data);
        fz_execute_display_list(list, dev, fz_identity, fz_infinite_bbox, NULL);
    }
    fz_catch(ctx) { }
    fz_free_device(dev);

    // save the image rectangles for this page
    int pageNo = GetPageNo(page);
    if (!imageRects[pageNo-1] && positions.Count() > 0) {
        // the list of page image rectangles is terminated with a null-rectangle
        fz_rect *rects = SAZA(fz_rect, positions.Count() + 1);
        if (rects) {
            for (size_t i = 0; i < positions.Count(); i++) {
                rects[i] = positions.At(i).rect;
            }
            imageRects[pageNo-1] = rects;
        }
    }

    PdfPageRun newRun = { page, list, data.mem_estimate, data.requiresBlending, 1 };
    return (PdfPageRun *)_memdup(&newRun);
}

PdfPageRun *CPdfEngine::GetPageRun(pdf_page *page, bool tryOnly)
{
    PdfPageRun *result = NULL;

    ScopedCritSec scope(&pagesAccess);

    for (size_t i = 0; i < runCache.Count(); i++) {
        if (runCache.At(i)->page == page) {
            result = runCache.At(i);
            break;
        }
    }
    if (!result && !tryOnly) {
        size_t mem = 0;
        for (size_t i = 0; i < runCache.Count(); i++) {
            // drop page runs that take up too much memory due to huge images
            // (except for the very recently used ones)
            if (i >= 2 && mem + runCache.At(i)->size_est >= MAX_PAGE_RUN_MEMORY)
                DropPageRun(runCache.At(i--), true);
            else
                mem += runCache.At(i)->size_est;
        }
        if (runCache.Count() >= MAX_PAGE_RUN_CACHE) {
            assert(runCache.Count() == MAX_PAGE_RUN_CACHE);
            DropPageRun(runCache.Last(), true);
        }

        ScopedCritSec scope2(&ctxAccess);

        fz_display_list *list = NULL;
        fz_device *dev = NULL;
        fz_var(list);
        fz_var(dev);
        fz_try(ctx) {
            list = fz_new_display_list(ctx);
            dev = fz_new_list_device(ctx, list);
            pdf_run_page(_doc, page, dev, fz_identity, NULL);
        }
        fz_catch(ctx) {
            fz_free_display_list(ctx, list);
            list = NULL;
        }
        fz_free_device(dev);

        if (list) {
            result = CreatePageRun(page, list);
            runCache.InsertAt(0, result);
        }
    }
    else if (result && result != runCache.At(0)) {
        // keep the list Most Recently Used first
        runCache.Remove(result);
        runCache.InsertAt(0, result);
    }

    if (result)
        result->refs++;
    return result;
}

bool CPdfEngine::RunPage(pdf_page *page, fz_device *dev, fz_matrix ctm, RenderTarget target, fz_bbox clipbox, bool cacheRun)
{
    bool ok = true;
    PdfPageRun *run;

    if (Target_View == target && (run = GetPageRun(page, !cacheRun))) {
        EnterCriticalSection(&ctxAccess);
        fz_try(ctx) {
            fz_execute_display_list(run->list, dev, ctm, clipbox, NULL);
        }
        fz_catch(ctx) {
            ok = false;
        }
        LeaveCriticalSection(&ctxAccess);
        DropPageRun(run);
    }
    else {
        char *targetName = target == Target_Print ? "Print" :
                           target == Target_Export ? "Export" : "View";
        EnterCriticalSection(&ctxAccess);
        fz_try(ctx) {
            pdf_run_page_with_usage(_doc, page, dev, ctm, targetName, NULL);
        }
        fz_catch(ctx) {
            ok = false;
        }
        LeaveCriticalSection(&ctxAccess);
    }

    EnterCriticalSection(&ctxAccess);
    fz_free_device(dev);
    LeaveCriticalSection(&ctxAccess);

    return ok;
}

void CPdfEngine::DropPageRun(PdfPageRun *run, bool forceRemove)
{
    EnterCriticalSection(&pagesAccess);
    run->refs--;

    if (0 == run->refs || forceRemove) {
        runCache.Remove(run);
        if (0 == run->refs) {
            EnterCriticalSection(&ctxAccess);
            fz_free_display_list(ctx, run->list);
            LeaveCriticalSection(&ctxAccess);
            free(run);
        }
    }

    LeaveCriticalSection(&pagesAccess);
}

RectD CPdfEngine::PageMediabox(int pageNo)
{
    assert(1 <= pageNo && pageNo <= PageCount());
    if (!_mediaboxes[pageNo-1].IsEmpty())
        return _mediaboxes[pageNo-1];

    fz_obj *page = _doc->page_objs[pageNo-1];
    if (!page)
        return RectD();

    ScopedCritSec scope(&ctxAccess);

    // cf. pdf_page.c's pdf_load_page
    fz_rect mbox = fz_empty_rect, cbox = fz_empty_rect;
    int rotate = 0;
    fz_try(ctx) {
        mbox = pdf_to_rect(ctx, fz_dict_gets(page, "MediaBox"));
        cbox = pdf_to_rect(ctx, fz_dict_gets(page, "CropBox"));
        rotate = fz_to_int(fz_dict_gets(page, "Rotate"));
    }
    fz_catch(ctx) { }
    if (fz_is_empty_rect(mbox)) {
        fz_warn(ctx, "cannot find page size for page %d", pageNo);
        mbox.x0 = 0; mbox.y0 = 0;
        mbox.x1 = 612; mbox.y1 = 792;
    }
    if (!fz_is_empty_rect(cbox)) {
        mbox = fz_intersect_rect(mbox, cbox);
        if (fz_is_empty_rect(mbox))
            return RectD();
    }
    if ((rotate % 90) != 0)
        rotate = 0;

    // cf. pdf_page.c's pdf_bound_page
    mbox = fz_transform_rect(fz_rotate((float)rotate), mbox);

    _mediaboxes[pageNo-1] = RectD(0, 0, mbox.x1 - mbox.x0, mbox.y1 - mbox.y0);
    return _mediaboxes[pageNo-1];
}

RectD CPdfEngine::PageContentBox(int pageNo, RenderTarget target)
{
    assert(1 <= pageNo && pageNo <= PageCount());
    pdf_page *page = GetPdfPage(pageNo);
    if (!page)
        return RectD();

    fz_bbox bbox;
    fz_device *dev;
    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        dev = fz_new_bbox_device(ctx, &bbox);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return RectD();
    }

    fz_bbox mediabox = fz_round_rect(pdf_bound_page(_doc, page));
    bool ok = RunPage(page, dev, fz_identity, target, mediabox, false);
    if (!ok)
        return PageMediabox(pageNo);
    if (fz_is_infinite_bbox(bbox))
        return PageMediabox(pageNo);

    RectD bbox2 = fz_bbox_to_RectI(bbox).Convert<double>();
    return bbox2.Intersect(PageMediabox(pageNo));
}

PointD CPdfEngine::Transform(PointD pt, int pageNo, float zoom, int rotation, bool inverse)
{
    fz_point pt2 = { (float)pt.x, (float)pt.y };
    fz_matrix ctm = viewctm(pageNo, zoom, rotation);
    if (inverse)
        ctm = fz_invert_matrix(ctm);
    pt2 = fz_transform_point(ctm, pt2);
    return PointD(pt2.x, pt2.y);
}

RectD CPdfEngine::Transform(RectD rect, int pageNo, float zoom, int rotation, bool inverse)
{
    fz_rect rect2 = fz_RectD_to_rect(rect);
    fz_matrix ctm = viewctm(pageNo, zoom, rotation);
    if (inverse)
        ctm = fz_invert_matrix(ctm);
    rect2 = fz_transform_rect(ctm, rect2);
    return fz_rect_to_RectD(rect2);
}

bool CPdfEngine::RenderPage(HDC hDC, pdf_page *page, RectI screenRect, fz_matrix *ctm, float zoom, int rotation, RectD *pageRect, RenderTarget target)
{
    if (!page)
        return false;

    fz_matrix ctm2;
    if (!ctm) {
        ctm2 = viewctm(page, zoom, rotation);
        fz_rect pRect = pageRect ? fz_RectD_to_rect(*pageRect) : pdf_bound_page(_doc, page);
        fz_bbox bbox = fz_round_rect(fz_transform_rect(ctm2, pRect));
        ctm2 = fz_concat(ctm2, fz_translate((float)screenRect.x - bbox.x0, (float)screenRect.y - bbox.y0));
    }
    else
        ctm2 = *ctm;

    HBRUSH bgBrush = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    FillRect(hDC, &screenRect.ToRECT(), bgBrush); // initialize white background
    DeleteObject(bgBrush);

    fz_bbox clipbox = fz_RectI_to_bbox(screenRect);
    fz_device *dev = NULL;
    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        dev = fz_new_gdiplus_device(ctx, hDC, clipbox);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return false;
    }

    return RunPage(page, dev, ctm2, target, clipbox);
}

// Fitz' draw_device.c currently isn't able to correctly render
// transparency groups while our dev_gdiplus.cpp gets most of them right
// cf. http://bugs.ghostscript.com/show_bug.cgi?id=690686
// cf. http://code.google.com/p/sumatrapdf/issues/detail?id=51
bool CPdfEngine::RequiresBlending(pdf_page *page)
{
    if (!page->transparency)
        return false;
    PdfPageRun *run = GetPageRun(page);
    if (!run)
        return false;

    bool result = run->req_blending;
    DropPageRun(run);
    return result;
}

RenderedBitmap *CPdfEngine::RenderBitmap(int pageNo, float zoom, int rotation, RectD *pageRect, RenderTarget target)
{    
    pdf_page* page = GetPdfPage(pageNo);
    if (!page)
        return NULL;

    fz_rect pRect = pageRect ? fz_RectD_to_rect(*pageRect) : pdf_bound_page(_doc, page);
    fz_matrix ctm = viewctm(page, zoom, rotation);
    fz_bbox bbox = fz_round_rect(fz_transform_rect(ctm, pRect));

    // GDI+ seems to render quicker and more reliable at high zoom levels
    if ((zoom > 40.0 || RequiresBlending(page)) != gDebugGdiPlusDevice) {
        int w = bbox.x1 - bbox.x0, h = bbox.y1 - bbox.y0;
        ctm = fz_concat(ctm, fz_translate((float)-bbox.x0, (float)-bbox.y0));

        // for now, don't render directly into a DC but produce an HBITMAP instead
        HDC hDC = GetDC(NULL);
        HDC hDCMem = CreateCompatibleDC(hDC);
        HBITMAP hbmp = CreateCompatibleBitmap(hDC, w, h);
        DeleteObject(SelectObject(hDCMem, hbmp));

        RectI rc(0, 0, w, h);
        RectD pageRect2 = fz_rect_to_RectD(pRect);
        bool ok = RenderPage(hDCMem, page, rc, &ctm, 0, 0, &pageRect2, target);
        DeleteDC(hDCMem);
        ReleaseDC(NULL, hDC);
        if (!ok) {
            DeleteObject(hbmp);
            return NULL;
        }
        return new RenderedBitmap(hbmp, SizeI(w, h));
    }

    fz_pixmap *image;
    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        image = fz_new_pixmap_with_rect(ctx, fz_find_device_colorspace("DeviceRGB"), bbox);
        fz_clear_pixmap_with_color(image, 0xFF); // initialize white background
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return NULL;
    }

    fz_device *dev;
    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        dev = fz_new_draw_device(ctx, image);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return NULL;
    }

    bool ok = RunPage(page, dev, ctm, target, bbox);

    ScopedCritSec scope(&ctxAccess);

    RenderedBitmap *bitmap = NULL;
    if (ok)
        bitmap = new RenderedFitzBitmap(ctx, image);
    fz_drop_pixmap(ctx, image);
    return bitmap;
}

PageElement *CPdfEngine::GetElementAtPos(int pageNo, PointD pt)
{
    pdf_page *page = GetPdfPage(pageNo, true);
    if (!page)
        return NULL;

    fz_point p = { (float)pt.x, (float)pt.y };
    for (fz_link *link = page->links; link; link = link->next) {
        if (link->dest.kind != FZ_LINK_NONE && fz_is_pt_in_rect(link->rect, p))
            return new PdfLink(this, &link->dest, link->rect, pageNo, &p);
    }

    if (pageComments[pageNo-1]) {
        for (size_t i = 0; pageComments[pageNo-1][i]; i++) {
            pdf_annot *annot = pageComments[pageNo-1][i];
            fz_rect rect = fz_transform_rect(page->ctm, annot->rect);
            if (fz_is_pt_in_rect(rect, p)) {
                ScopedCritSec scope(&ctxAccess);

                ScopedMem<TCHAR> contents(str::conv::FromPdf(fz_dict_gets(annot->obj, "Contents")));
                return new PdfComment(contents, fz_rect_to_RectD(rect), pageNo);
            }
        }
    }

    if (imageRects[pageNo-1]) {
        for (size_t i = 0; !fz_is_empty_rect(imageRects[pageNo-1][i]); i++)
            if (fz_is_pt_in_rect(imageRects[pageNo-1][i], p))
                return new PdfImage(this, pageNo, imageRects[pageNo-1][i], i);
    }

    return NULL;
}

Vec<PageElement *> *CPdfEngine::GetElements(int pageNo)
{
    pdf_page *page = GetPdfPage(pageNo, true);
    if (!page)
        return NULL;

    // since all elements lists are in last-to-first order, append
    // item types in inverse order and reverse the whole list at the end
    Vec<PageElement *> *els = new Vec<PageElement *>();
    if (!els)
        return NULL;

    if (imageRects[pageNo-1]) {
        for (size_t i = 0; !fz_is_empty_rect(imageRects[pageNo-1][i]); i++)
            els->Append(new PdfImage(this, pageNo, imageRects[pageNo-1][i], i));
    }

    if (pageComments[pageNo-1]) {
        ScopedCritSec scope(&ctxAccess);

        for (size_t i = 0; pageComments[pageNo-1][i]; i++) {
            pdf_annot *annot = pageComments[pageNo-1][i];
            fz_rect rect = fz_transform_rect(page->ctm, annot->rect);
            ScopedMem<TCHAR> contents(str::conv::FromPdf(fz_dict_gets(annot->obj, "Contents")));
            els->Append(new PdfComment(contents, fz_rect_to_RectD(rect), pageNo));
        }
    }

    for (fz_link *link = page->links; link; link = link->next) {
        if (link->dest.kind != FZ_LINK_NONE) {
            els->Append(new PdfLink(this, &link->dest, link->rect, pageNo));
        }
    }

    els->Reverse();
    return els;
}

void CPdfEngine::LinkifyPageText(pdf_page *page)
{
    page->links = FixupPageLinks(page->links);

    RectI *coords;
    TCHAR *pageText = ExtractPageText(page, _T("\n"), &coords, Target_View, true);
    if (!pageText) {
        return;
    }

    LinkRectList *list = LinkifyText(pageText, coords);
    for (size_t i = 0; i < list->links.Count(); i++) {
        bool overlaps = false;
        for (fz_link *next = page->links; next && !overlaps; next = next->next)
            overlaps = fz_calc_overlap(next->rect, list->coords.At(i)) >= 0.25f;
        if (!overlaps) {
            ScopedMem<char> uri(str::conv::ToUtf8(list->links.At(i)));
            if (!uri) continue;
            fz_link_dest ld = { FZ_LINK_URI, 0 };
            ld.ld.uri.uri = fz_strdup(ctx, uri);
            // add links in top-to-bottom order (i.e. last-to-first)
            fz_link *link = fz_new_link(ctx, list->coords.At(i), ld);
            link->next = page->links;
            page->links = link;
        }
    }

    delete list;
    delete[] coords;
    free(pageText);
}

pdf_annot **CPdfEngine::ProcessPageAnnotations(pdf_page *page)
{
    Vec<pdf_annot *> comments;

    for (pdf_annot *annot = page->annots; annot; annot = annot->next) {
        char *subtype = fz_to_name(fz_dict_gets(annot->obj, "Subtype"));
        if (str::Eq(subtype, "FileAttachment")) {
            fz_obj *file = fz_dict_gets(annot->obj, "FS");
            fz_rect rect = pdf_to_rect(ctx, fz_dict_gets(annot->obj, "Rect"));
            fz_obj *embedded = fz_dict_getsa(fz_dict_gets(file, "EF"), "DOS", "F");
            if (file && embedded && !fz_is_empty_rect(rect)) {
                fz_link_dest ld;
                ld.kind = FZ_LINK_LAUNCH;
                ld.ld.launch.file_spec = pdf_file_spec_to_str(ctx, file);
                ld.ld.launch.new_window = 1;
                ld.ld.launch.embedded = fz_keep_obj(embedded);
                rect = fz_transform_rect(page->ctm, rect);
                // add links in top-to-bottom order (i.e. last-to-first)
                fz_link *link = fz_new_link(ctx, rect, ld);
                link->next = page->links;
                page->links = link;
            }
        }
        if (!str::IsEmpty(fz_to_str_buf(fz_dict_gets(annot->obj, "Contents"))) &&
            !str::Eq(subtype, "FreeText")) {
            comments.Append(annot);
        }
    }

    if (comments.Count() == 0)
        return NULL;

    // re-order list into top-to-bottom order (i.e. last-to-first)
    comments.Reverse();
    // add sentinel value
    comments.Append(NULL);
    return comments.StealData();
}

RenderedBitmap *CPdfEngine::GetPageImage(int pageNo, RectD rect, size_t imageIx)
{
    pdf_page *page = GetPdfPage(pageNo);
    if (!page)
        return NULL;

    Vec<FitzImagePos> positions;
    ListInspectionData data = { &positions, false, 0 };
    fz_device *dev;

    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        dev = fz_new_inspection_device(ctx, &data);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return NULL;
    }

    RunPage(page, dev, fz_identity);

    if (imageIx >= positions.Count() || fz_rect_to_RectD(positions.At(imageIx).rect) != rect) {
        assert(0);
        return NULL;
    }

    ScopedCritSec scope(&ctxAccess);
    return new RenderedFitzBitmap(ctx, positions.At(imageIx).image);
}

TCHAR *CPdfEngine::ExtractPageText(pdf_page *page, TCHAR *lineSep, RectI **coords_out, RenderTarget target, bool cacheRun)
{
    if (!page)
        return NULL;

    fz_text_span *text = NULL;
    fz_device *dev;
    fz_var(text);

    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        text = fz_new_text_span(ctx);
        dev = fz_new_text_device(ctx, text);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        fz_free_text_span(ctx, text);
        return NULL;
    }

    // use an infinite rectangle as bounds (instead of pdf_bound_page) to ensure that
    // the extracted text is consistent between cached runs using a list device and
    // fresh runs (otherwise the list device omits text outside the mediabox bounds)
    bool ok = RunPage(page, dev, fz_identity, target, fz_infinite_bbox, cacheRun);

    ScopedCritSec scope(&ctxAccess);

    TCHAR *content = NULL;
    if (ok)
        content = fz_span_to_tchar(text, lineSep, coords_out);
    fz_free_text_span(ctx, text);

    return content;
}

TCHAR *CPdfEngine::ExtractPageText(int pageNo, TCHAR *lineSep, RectI **coords_out, RenderTarget target)
{
    pdf_page *page = GetPdfPage(pageNo, true);
    if (page)
        return ExtractPageText(page, lineSep, coords_out, target);

    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        page = pdf_load_page(_doc, pageNo - 1);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return NULL;
    }

    TCHAR *result = ExtractPageText(page, lineSep, coords_out, target);

    EnterCriticalSection(&ctxAccess);
    pdf_free_page(ctx, page);
    LeaveCriticalSection(&ctxAccess);

    return result;
}

TCHAR *CPdfEngine::GetProperty(char *name)
{
    if (!_doc)
        return NULL;

    if (str::Eq(name, "PdfVersion")) {
        int major = _doc->version / 10, minor = _doc->version % 10;
        if (1 == major && 7 == minor && 5 == pdf_get_crypt_revision(_doc))
            return str::Format(_T("%d.%d Adobe Extension Level %d"), major, minor, 3);
        return str::Format(_T("%d.%d"), major, minor);
    }

    // _info is guaranteed not to contain any indirect references,
    // so no need for ctxAccess
    fz_obj *obj = fz_dict_gets(_info, name);
    if (!obj)
        return NULL;

    return str::conv::FromPdf(obj);
};

char *CPdfEngine::GetDecryptionKey() const
{
    if (!_decryptionKey)
        return NULL;
    return str::Dup(_decryptionKey);
}

PageLayoutType CPdfEngine::PreferredLayout()
{
    PageLayoutType layout = Layout_Single;

    ScopedCritSec scope(&ctxAccess);
    fz_obj *root;
    fz_try(ctx) {
        root = fz_dict_gets(_doc->trailer, "Root");
    }
    fz_catch(ctx) {
        return layout;
    }

    fz_try(ctx) {
        char *name = fz_to_name(fz_dict_gets(root, "PageLayout"));
        if (str::EndsWith(name, "Right"))
            layout = Layout_Book;
        else if (str::StartsWith(name, "Two"))
            layout = Layout_Facing;
    }
    fz_catch(ctx) { }

    fz_try(ctx) {
        fz_obj *prefs = fz_dict_gets(root, "ViewerPreferences");
        char *direction = fz_to_name(fz_dict_gets(prefs, "Direction"));
        if (str::Eq(direction, "R2L"))
            layout = (PageLayoutType)(layout | Layout_R2L);
    }
    fz_catch(ctx) { }

    return layout;
}

unsigned char *CPdfEngine::GetFileData(size_t *cbCount)
{
    unsigned char *data;
    ScopedCritSec scope(&ctxAccess);
    fz_try(ctx) {
        data = fz_extract_stream_data(_doc->file, cbCount);
    }
    fz_catch(ctx) {
        return _fileName ? (unsigned char *)file::ReadAll(_fileName, cbCount) : NULL;
    }
    return data;
}

bool CPdfEngine::SaveEmbedded(fz_obj *obj, LinkSaverUI& saveUI)
{
    ScopedCritSec scope(&ctxAccess);

    fz_buffer *data = NULL;
    fz_try(ctx) {
        data = pdf_load_stream(_doc, fz_to_num(obj), fz_to_gen(obj));
    }
    fz_catch(ctx) {
        return false;
    }
    bool result = saveUI.SaveEmbedded(data->data, data->len);
    fz_drop_buffer(ctx, data);
    return result;
}

bool CPdfEngine::IsImagePage(int pageNo)
{
    pdf_page *page = GetPdfPage(pageNo, true);
    // GetPdfPage extracts imageRects for us
    if (!page || !imageRects[pageNo-1])
        return false;

    fz_rect mbox = fz_RectD_to_rect(PageMediabox(pageNo));
    // check if any image covers at least 90% of the page
    for (int i = 0; !fz_is_empty_rect(imageRects[pageNo-1][i]); i++)
        if (fz_calc_overlap(mbox, imageRects[pageNo-1][i]) >= 0.9f)
            return true;
    return false;
}

TCHAR *CPdfEngine::GetPageLabel(int pageNo)
{
    if (!_pagelabels || pageNo < 1 || PageCount() < pageNo)
        return BaseEngine::GetPageLabel(pageNo);

    return str::Dup(_pagelabels->At(pageNo - 1));
}

int CPdfEngine::GetPageByLabel(const TCHAR *label)
{
    int pageNo = _pagelabels ? _pagelabels->Find(label) + 1 : 0;
    if (!pageNo)
        return BaseEngine::GetPageByLabel(label);

    return pageNo;
}

static bool IsRelativeURI(const TCHAR *uri)
{
    const TCHAR *colon = str::FindChar(uri, ':');
    const TCHAR *slash = str::FindChar(uri, '/');

    return !colon || (slash && colon > slash);
}

TCHAR *PdfLink::GetValue() const
{
    if (!link || !engine)
        return NULL;
    if (link->kind != FZ_LINK_URI && link->kind != FZ_LINK_LAUNCH &&
        link->kind != FZ_LINK_GOTOR)
        return NULL;

    ScopedCritSec scope(&engine->ctxAccess);

    TCHAR *path = NULL;

    switch (link->kind) {
    case FZ_LINK_URI:
        path = str::conv::FromUtf8(link->ld.uri.uri);
        if (IsRelativeURI(path)) {
            ScopedMem<TCHAR> base;
            fz_try(engine->ctx) {
                fz_obj *obj = fz_dict_gets(engine->_doc->trailer, "Root");
                obj = fz_dict_gets(fz_dict_gets(obj, "URI"), "Base");
                if (obj)
                    base.Set(str::conv::FromPdf(obj));
            }
            fz_catch(engine->ctx) { }
            if (!str::IsEmpty(base.Get())) {
                ScopedMem<TCHAR> uri(str::Join(base, path));
                free(path);
                path = uri.StealData();
            }
        }
        if (link->ld.uri.is_map) {
            int x = 0, y = 0;
            if (rect.Inside(pt)) {
                x = (int)(pt.x - rect.x + 0.5);
                y = (int)(pt.y - rect.y + 0.5);
            }
            ScopedMem<TCHAR> uri(str::Format(_T("%s?%d,%d"), path, x, y));
            free(path);
            path = uri.StealData();
        }
        break;
    case FZ_LINK_LAUNCH:
        // note: we (intentionally) don't support the /Win specific Launch parameters
        path = str::conv::FromUtf8(link->ld.launch.file_spec);
        if (path && link->ld.launch.embedded && str::EndsWithI(path, _T(".pdf"))) {
            free(path);
            path = str::Format(_T("%s:%d:%d"), engine->FileName(),
                fz_to_num(link->ld.launch.embedded), fz_to_gen(link->ld.launch.embedded));
        }
        break;
    case FZ_LINK_GOTOR:
        path = str::conv::FromUtf8(link->ld.gotor.file_spec);
        break;
    }

    return path;
}

const char *PdfLink::GetDestType() const
{
    if (!link)
        return NULL;

    switch (link->kind) {
    case FZ_LINK_GOTO: return "ScrollTo";
    case FZ_LINK_URI: return "LaunchURL";
    case FZ_LINK_NAMED: return link->ld.named.named;
    case FZ_LINK_LAUNCH:
        if (link->ld.launch.embedded)
            return "LaunchEmbedded";
        return "LaunchFile";
    case FZ_LINK_GOTOR: return "LaunchFile";
    default: return NULL; // unsupported action
    }
}

int PdfLink::GetDestPageNo() const
{
    if (link && FZ_LINK_GOTO == link->kind)
        return link->ld.gotor.page + 1;
    if (link && FZ_LINK_GOTOR == link->kind && !link->ld.gotor.rname)
        return link->ld.gotor.page + 1;
    return 0;
}

RectD PdfLink::GetDestRect() const
{
    RectD result(DEST_USE_DEFAULT, DEST_USE_DEFAULT, DEST_USE_DEFAULT, DEST_USE_DEFAULT);
    if (!link || FZ_LINK_GOTO != link->kind && FZ_LINK_GOTOR != link->kind)
        return result;
    if (link->ld.gotor.page < 0 || link->ld.gotor.page >= engine->PageCount())
        return result;

    pdf_page *page = engine->GetPdfPage(link->ld.gotor.page + 1);
    if (!page)
        return result;
    fz_point lt = fz_transform_point(page->ctm, link->ld.gotor.lt);
    fz_point rb = fz_transform_point(page->ctm, link->ld.gotor.rb);

    if ((link->ld.gotor.flags & fz_link_flag_r_is_zoom)) {
        // /XYZ link, undefined values for the coordinates mean: keep the current position
        if ((link->ld.gotor.flags & fz_link_flag_l_valid))
            result.x = lt.x;
        if ((link->ld.gotor.flags & fz_link_flag_t_valid))
            result.y = lt.y;
        result.dx = result.dy = 0;
    }
    else if ((link->ld.gotor.flags & (fz_link_flag_fit_h | fz_link_flag_fit_v)) == (fz_link_flag_fit_h | fz_link_flag_fit_v) &&
        (link->ld.gotor.flags & (fz_link_flag_l_valid | fz_link_flag_t_valid | fz_link_flag_r_valid | fz_link_flag_b_valid))) {
        // /FitR link
        result = RectD::FromXY(lt.x, lt.y, rb.x, rb.y);
        // an empty destination rectangle would imply an /XYZ-type link to callers
        if (result.IsEmpty())
            result.dx = result.dy = 0.1;
    }
    else if ((link->ld.gotor.flags & (fz_link_flag_fit_h | fz_link_flag_fit_v)) == fz_link_flag_fit_h) {
        // /FitH or /FitBH link
        result.y = lt.y;
    }
    // all other link types only affect the zoom level, which we intentionally leave alone
    return result;
}

TCHAR *PdfLink::GetDestName() const
{
    if (!link || FZ_LINK_GOTOR != link->kind || !link->ld.gotor.rname)
        return NULL;
    return str::conv::FromUtf8(link->ld.gotor.rname);
}

bool PdfLink::SaveEmbedded(LinkSaverUI& saveUI)
{
    ScopedCritSec scope(&engine->ctxAccess);
    return engine->SaveEmbedded(link->ld.launch.embedded, saveUI);
}

bool PdfEngine::IsSupportedFile(const TCHAR *fileName, bool sniff)
{
    if (sniff) {
        char header[1024];
        ZeroMemory(header, sizeof(header));
        file::ReadAll(fileName, header, sizeof(header));

        for (int i = 0; i < sizeof(header) - 4; i++)
            if (str::EqN(header + i, "%PDF", 4))
                return true;
        return false;
    }

    return str::EndsWithI(fileName, _T(".pdf")) || findEmbedMarks(fileName);
}

PdfEngine *PdfEngine::CreateFromFileName(const TCHAR *fileName, PasswordUI *pwdUI)
{
    CPdfEngine *engine = new CPdfEngine();
    if (!engine || !fileName || !engine->Load(fileName, pwdUI)) {
        delete engine;
        return NULL;
    }
    return engine;
}

PdfEngine *PdfEngine::CreateFromStream(IStream *stream, PasswordUI *pwdUI)
{
    CPdfEngine *engine = new CPdfEngine();
    if (!engine->Load(stream, pwdUI)) {
        delete engine;
        return NULL;
    }
    return engine;
}

///// XpsEngine is also based on Fitz and shares quite some code with PdfEngine /////

extern "C" {
#include <muxps.h>
}

struct XpsPageRun {
    xps_page *page;
    fz_display_list *list;
    size_t size_est;
    int refs;
};

class XpsTocItem;
class XpsImage;

class CXpsEngine : public XpsEngine {
    friend XpsEngine;
    friend XpsImage;

public:
    CXpsEngine();
    virtual ~CXpsEngine();
    virtual CXpsEngine *Clone();

    virtual const TCHAR *FileName() const { return _fileName; };
    virtual int PageCount() const {
        return _doc ? xps_count_pages(_doc) : 0;
    }

    virtual RectD PageMediabox(int pageNo);
    virtual RectD PageContentBox(int pageNo, RenderTarget target=Target_View);

    virtual RenderedBitmap *RenderBitmap(int pageNo, float zoom, int rotation,
                         RectD *pageRect=NULL, /* if NULL: defaults to the page's mediabox */
                         RenderTarget target=Target_View);
    virtual bool RenderPage(HDC hDC, RectI screenRect, int pageNo, float zoom, int rotation,
                         RectD *pageRect=NULL, RenderTarget target=Target_View) {
        return RenderPage(hDC, GetXpsPage(pageNo), screenRect, NULL, zoom, rotation, pageRect);
    }

    virtual PointD Transform(PointD pt, int pageNo, float zoom, int rotation, bool inverse=false);
    virtual RectD Transform(RectD rect, int pageNo, float zoom, int rotation, bool inverse=false);

    virtual unsigned char *GetFileData(size_t *cbCount);
    virtual TCHAR * ExtractPageText(int pageNo, TCHAR *lineSep, RectI **coords_out=NULL,
                                    RenderTarget target=Target_View);
    virtual bool IsImagePage(int pageNo);
    virtual TCHAR *GetProperty(char *name);

    virtual float GetFileDPI() const { return 72.0f; }
    virtual const TCHAR *GetDefaultFileExt() const { return _T(".xps"); }

    virtual bool BenchLoadPage(int pageNo) { return GetXpsPage(pageNo) != NULL; }

    virtual Vec<PageElement *> *GetElements(int pageNo);
    virtual PageElement *GetElementAtPos(int pageNo, PointD pt);

    virtual PageDestination *GetNamedDest(const TCHAR *name);
    virtual bool HasTocTree() const { return _outline != NULL; }
    virtual DocTocItem *GetTocTree();

    fz_rect FindDestRect(const char *target);

protected:
    const TCHAR *_fileName;

    // make sure to never ask for _pagesAccess in an ctxAccess
    // protected critical section in order to avoid deadlocks
    CRITICAL_SECTION ctxAccess;
    fz_context *    ctx;
    xps_document *  _doc;

    CRITICAL_SECTION _pagesAccess;
    xps_page **     _pages;

    virtual bool    Load(const TCHAR *fileName);
    virtual bool    Load(IStream *stream);
    bool            Load(fz_stream *stm);
    bool            LoadFromStream(fz_stream *stm);

    xps_page      * GetXpsPage(int pageNo, bool failIfBusy=false);
    int             GetPageNo(xps_page *page);
    fz_matrix       viewctm(int pageNo, float zoom, int rotation) {
        return fz_create_view_ctm(fz_RectD_to_rect(PageMediabox(pageNo)), zoom, rotation);
    }
    fz_matrix       viewctm(xps_page *page, float zoom, int rotation) {
        return fz_create_view_ctm(xps_bound_page(_doc, page), zoom, rotation);
    }
    bool            RenderPage(HDC hDC, xps_page *page, RectI screenRect,
                               fz_matrix *ctm, float zoom, int rotation,
                               RectD *pageRect);
    TCHAR         * ExtractPageText(xps_page *page, TCHAR *lineSep,
                                    RectI **coords_out=NULL, bool cacheRun=false);

    Vec<XpsPageRun*>runCache; // ordered most recently used first
    XpsPageRun    * CreatePageRun(xps_page *page, fz_display_list *list);
    XpsPageRun    * GetPageRun(xps_page *page, bool tryOnly=false, int firstForPage=0);
    bool            RunPage(xps_page *page, fz_device *dev, fz_matrix ctm,
                            fz_bbox clipbox=fz_infinite_bbox, bool cacheRun=true);
    void            DropPageRun(XpsPageRun *run, bool forceRemove=false);

    XpsTocItem   *  BuildTocTree(fz_outline *entry, int& idCounter);
    void            LinkifyPageText(xps_page *page, int pageNo);
    RenderedBitmap *GetPageImage(int pageNo, RectD rect, size_t imageIx);

    RectD         * _mediaboxes;
    fz_outline    * _outline;
    fz_link      ** _links;
    fz_obj        * _info;
    fz_rect      ** imageRects;
};

class XpsLink : public PageElement, public PageDestination {
    CXpsEngine *engine;
    fz_link_dest *link; // owned by a fz_link or fz_outline
    RectD rect;
    int pageNo;

public:
    XpsLink() : engine(NULL), link(NULL), pageNo(-1) { }
    XpsLink(CXpsEngine *engine, fz_link_dest *link, fz_rect rect=fz_empty_rect, int pageNo=-1) :
        engine(engine), link(link), rect(fz_rect_to_RectD(rect)), pageNo(pageNo) { }

    virtual PageElementType GetType() const { return Element_Link; }
    virtual int GetPageNo() const { return pageNo; }
    virtual RectD GetRect() const { return rect; }
    virtual TCHAR *GetValue() const {
        if (link && FZ_LINK_URI == link->kind)
            return str::conv::FromUtf8(link->ld.uri.uri);
        return NULL;
    }
    virtual PageDestination *AsLink() { return this; }

    virtual const char *GetDestType() const {
        if (!link)
            return NULL;
        if (FZ_LINK_GOTO == link->kind)
            return "ScrollTo";
        if (FZ_LINK_URI == link->kind)
            return "LaunchURL";
        return NULL;
    }
    virtual int GetDestPageNo() const {
        if (!link || link->kind != FZ_LINK_GOTO)
            return 0;
        return link->ld.gotor.page + 1;
    }
    virtual RectD GetDestRect() const {
        if (!engine || !link || link->kind != FZ_LINK_GOTO || !link->ld.gotor.rname)
            return RectD(DEST_USE_DEFAULT, DEST_USE_DEFAULT, DEST_USE_DEFAULT, DEST_USE_DEFAULT);
        return fz_rect_to_RectD(engine->FindDestRect(link->ld.gotor.rname));
    }
    virtual TCHAR *GetDestValue() const { return GetValue(); }
};

class XpsTocItem : public DocTocItem {
    XpsLink link;

public:
    XpsTocItem(TCHAR *title, XpsLink link) : DocTocItem(title), link(link) { }

    virtual PageDestination *GetLink() { return &link; }
};

class XpsImage : public PageElement {
    CXpsEngine *engine;
    int pageNo;
    RectD rect;
    size_t imageIx;

public:
    XpsImage(CXpsEngine *engine, int pageNo, fz_rect rect, size_t imageIx) :
        engine(engine), pageNo(pageNo), rect(fz_rect_to_RectD(rect)), imageIx(imageIx) { }

    virtual PageElementType GetType() const { return Element_Image; }
    virtual int GetPageNo() const { return pageNo; }
    virtual RectD GetRect() const { return rect; }
    virtual TCHAR *GetValue() const { return NULL; }

    virtual RenderedBitmap *GetImage() {
        return engine->GetPageImage(pageNo, rect, imageIx);
    }
};

CXpsEngine::CXpsEngine() : _fileName(NULL), _doc(NULL), _pages(NULL), _mediaboxes(NULL),
    _outline(NULL), _links(NULL), _info(NULL), imageRects(NULL)
{
    InitializeCriticalSection(&_pagesAccess);
    InitializeCriticalSection(&ctxAccess);

    ctx = fz_new_context(NULL, MAX_CONTEXT_MEMORY);
}

CXpsEngine::~CXpsEngine()
{
    EnterCriticalSection(&_pagesAccess);
    EnterCriticalSection(&ctxAccess);

    if (_pages) {
        for (int i = 0; i < PageCount(); i++) {
            if (_pages[i])
                xps_free_page(_doc, _pages[i]);
        }
        free(_pages);
    }
    if (_links) {
        for (int i = 0; i < PageCount(); i++)
            if (_links[i])
                fz_free_link(ctx, _links[i]);
        free(_links);
    }

    if (_outline)
        fz_free_outline(_outline);
    if (_mediaboxes)
        delete[] _mediaboxes;
    if (imageRects) {
        for (int i = 0; i < PageCount(); i++)
            free(imageRects[i]);
        free(imageRects);
    }

    if (_doc) {
        xps_close_document(_doc);
        _doc = NULL;
    }
    if (_info)
        fz_drop_obj(_info);

    while (runCache.Count() > 0) {
        assert(runCache.Last()->refs == 1);
        DropPageRun(runCache.Last(), true);
    }

    free((void*)_fileName);

    fz_free_context(ctx);

    LeaveCriticalSection(&ctxAccess);
    DeleteCriticalSection(&ctxAccess);
    LeaveCriticalSection(&_pagesAccess);
    DeleteCriticalSection(&_pagesAccess);
}

CXpsEngine *CXpsEngine::Clone()
{
    ScopedCritSec scope(&ctxAccess);

    CXpsEngine *clone = new CXpsEngine();
    if (!clone || !clone->Load(_doc->file)) {
        delete clone;
        return NULL;
    }

    if (_fileName)
        clone->_fileName = str::Dup(_fileName);

    return clone;
}

bool CXpsEngine::Load(const TCHAR *fileName)
{
    assert(!_fileName && !_doc && ctx);
    _fileName = str::Dup(fileName);
    if (!_fileName || !ctx)
        return false;

    fz_stream *stm;
    fz_try(ctx) {
        stm = fz_open_file2(ctx, _fileName);
    }
    fz_catch(ctx) {
        return false;
    }
    return LoadFromStream(stm);
}

bool CXpsEngine::Load(IStream *stream)
{
    assert(!_fileName && !_doc && ctx);
    if (!ctx)
        return false;

    fz_stream *stm;
    fz_try(ctx) {
        stm = fz_open_istream(ctx, stream);
    }
    fz_catch(ctx) {
        return false;
    }
    return LoadFromStream(stm);
}

bool CXpsEngine::Load(fz_stream *stm)
{
    assert(!_fileName && !_doc && ctx);
    if (!ctx)
        return false;

    fz_try(ctx) {
        stm = fz_clone_stream(ctx, stm);
    }
    fz_catch(ctx) {
        return false;
    }
    return LoadFromStream(stm);
}

bool CXpsEngine::LoadFromStream(fz_stream *stm)
{
    if (!stm)
        return false;

    fz_try(ctx) {
        _doc = xps_open_document_with_stream(stm);
    }
    fz_always(ctx) {
        fz_close(stm);
    }
    fz_catch(ctx) {
        return false;
    }

    if (PageCount() == 0) {
        fz_warn(ctx, "document has no pages");
        return false;
    }

    _pages = SAZA(xps_page *, PageCount());
    _links = SAZA(fz_link *, PageCount());
    _mediaboxes = new RectD[PageCount()];
    imageRects = SAZA(fz_rect *, PageCount());

    if (!_pages || !_links || !_mediaboxes || !imageRects)
        return false;

    fz_try(ctx) {
        _outline = xps_load_outline(_doc);
    }
    fz_catch(ctx) {
        fz_warn(ctx, "Couldn't load outline");
    }
    fz_try(ctx) {
        _info = xps_extract_doc_props(_doc);
    }
    fz_catch(ctx) {
        fz_warn(ctx, "Couldn't load Info dictionary");
    }

    return true;
}

xps_page *CXpsEngine::GetXpsPage(int pageNo, bool failIfBusy)
{
    if (!_pages)
        return NULL;
    if (failIfBusy)
        return _pages[pageNo-1];

    ScopedCritSec scope(&_pagesAccess);

    xps_page *page = _pages[pageNo-1];
    if (!page) {
        fz_var(page);
        ScopedCritSec ctxScope(&ctxAccess);
        fz_try(ctx) {
            page = xps_load_page(_doc, pageNo - 1);
            _pages[pageNo-1] = page;
            LinkifyPageText(page, pageNo);
        }
        fz_catch(ctx) { }
    }

    return page;
}

int CXpsEngine::GetPageNo(xps_page *page)
{
    for (int i = 0; i < PageCount(); i++)
        if (page == _pages[i])
            return i + 1;
    assert(0);
    return 0;
}

XpsPageRun *CXpsEngine::CreatePageRun(xps_page *page, fz_display_list *list)
{
    Vec<FitzImagePos> positions;
    ListInspectionData data = { &positions, false, 0 };
    fz_device *dev = NULL;

    fz_var(dev);
    fz_try(ctx) {
        dev = fz_new_inspection_device(ctx, &data);
        fz_execute_display_list(list, dev, fz_identity, fz_infinite_bbox, NULL);
    }
    fz_catch(ctx) { }
    fz_free_device(dev);

    // save the image rectangles for this page
    int pageNo = GetPageNo(page);
    if (!imageRects[pageNo-1] && positions.Count() > 0) {
        // the list of page image rectangles is terminated with a null-rectangle
        fz_rect *rects = SAZA(fz_rect, positions.Count() + 1);
        if (rects) {
            for (size_t i = 0; i < positions.Count(); i++) {
                rects[i] = positions.At(i).rect;
            }
            imageRects[pageNo-1] = rects;
        }
    }

    XpsPageRun newRun = { page, list, data.mem_estimate, 1 };
    return (XpsPageRun *)_memdup(&newRun);
}

XpsPageRun *CXpsEngine::GetPageRun(xps_page *page, bool tryOnly, int firstForPage)
{
    ScopedCritSec scope(&_pagesAccess);

    XpsPageRun *result = NULL;

    for (size_t i = 0; i < runCache.Count(); i++) {
        if (runCache.At(i)->page == page) {
            result = runCache.At(i);
            break;
        }
    }
    if (!result && !tryOnly) {
        size_t mem = 0;
        for (size_t i = 0; i < runCache.Count(); i++) {
            // drop page runs that take up too much memory due to huge images
            // (except for the very recently used ones)
            if (i >= 2 && mem + runCache.At(i)->size_est >= MAX_PAGE_RUN_MEMORY)
                DropPageRun(runCache.At(i--), true);
            else
                mem += runCache.At(i)->size_est;
        }
        if (runCache.Count() >= MAX_PAGE_RUN_CACHE) {
            assert(runCache.Count() == MAX_PAGE_RUN_CACHE);
            DropPageRun(runCache.Last(), true);
        }

        ScopedCritSec ctxScope(&ctxAccess);

        fz_display_list *list = NULL;
        fz_device *dev = NULL;
        fz_link root = { 0 };
        fz_var(list);
        fz_var(dev);
        if (firstForPage)
            _doc->link_root = &root;
        fz_try(ctx) {
            list = fz_new_display_list(ctx);
            dev = fz_new_list_device(ctx, list);
            xps_run_page(_doc, page, dev, fz_identity, NULL);
        }
        fz_catch(ctx) {
            fz_free_link(ctx, root.next);
            fz_free_display_list(ctx, list);
            list = NULL;
        }
        fz_free_device(dev);
        _doc->link_root = NULL;

        if (list) {
            result = CreatePageRun(page, list);
            runCache.InsertAt(0, result);

            if (firstForPage) {
                assert(!_links[firstForPage-1]);
                _links[firstForPage-1] = root.next;
            }
        }
    }
    else if (result && result != runCache.At(0)) {
        // keep the list Most Recently Used first
        runCache.Remove(result);
        runCache.InsertAt(0, result);
    }

    if (result)
        result->refs++;
    return result;
}

bool CXpsEngine::RunPage(xps_page *page, fz_device *dev, fz_matrix ctm, fz_bbox clipbox, bool cacheRun)
{
    bool ok = true;

    XpsPageRun *run = GetPageRun(page, !cacheRun);
    if (run) {
        EnterCriticalSection(&ctxAccess);
        fz_try(ctx) {
            fz_execute_display_list(run->list, dev, ctm, clipbox, NULL);
        }
        fz_catch(ctx) {
            ok = false;
        }
        LeaveCriticalSection(&ctxAccess);
        DropPageRun(run);
    }
    else {
        ScopedCritSec scope(&ctxAccess);
        fz_try(ctx) {
            xps_run_page(_doc, page, dev, ctm, NULL);
        }
        fz_catch(ctx) {
            ok = false;
        }
    }

    EnterCriticalSection(&ctxAccess);
    fz_free_device(dev);
    LeaveCriticalSection(&ctxAccess);

    return ok;
}

void CXpsEngine::DropPageRun(XpsPageRun *run, bool forceRemove)
{
    ScopedCritSec scope(&_pagesAccess);
    run->refs--;

    if (0 == run->refs || forceRemove) {
        runCache.Remove(run);
        if (0 == run->refs) {
            ScopedCritSec ctxScope(&ctxAccess);
            fz_free_display_list(ctx, run->list);
            free(run);
        }
    }
}

// <FixedPage xmlns="http://schemas.microsoft.com/xps/2005/06" Width="816" Height="1056" xml:lang="en-US">

// MuPDF doesn't allow partial parsing of XML content, so try to
// extract a page's root element manually before feeding it to the parser
static RectD xps_extract_mediabox_quick_and_dirty(xps_document *doc, int pageNo)
{
    xps_part *part = NULL;
    for (xps_page *page = doc->first_page; page && !part; page = page->next)
        if (--pageNo == 0)
            part = xps_read_part(doc, page->name);
    if (!part)
        return RectD();

    byte *end = NULL;
    if (0xFF == part->data[0] && 0xFE == part->data[1]) {
        const WCHAR *start = str::Find((WCHAR *)part->data, L"<FixedPage");
        if (start)
            end = (byte *)str::FindChar(start, '>');
        if (end)
            end += 2;
    }
    else {
        const char *start = str::Find((char *)part->data, "<FixedPage");
        if (start)
            end = (byte *)str::FindChar(start, '>');
        if (end)
            // xml_parse_document ignores the length argument for UTF-8 data
            *(++end) = '\0';
    }
    if (!end) {
        xps_free_part(doc, part);
        return RectD();
    }

    // we depend on the parser not validating its input (else we'd
    // have to append a closing "</FixedPage>" to the byte data)
    xml_element *root;
    fz_try(doc->ctx) {
        root = xml_parse_document(doc->ctx, part->data, (int)(end - part->data));
    }
    fz_always(doc->ctx) {
        xps_free_part(doc, part);
    }
    fz_catch(doc->ctx) {
        fz_rethrow(doc->ctx);
    }

    RectD rect;
    if (str::Eq(xml_tag(root), "FixedPage")) {
        char *width = xml_att(root, "Width");
        if (width)
            rect.dx = atof(width) * 72 / 96;
        char *height = xml_att(root, "Height");
        if (height)
            rect.dy = atof(height) * 72 / 96;
    }

    xml_free_element(doc->ctx, root);
    return rect;
}

RectD CXpsEngine::PageMediabox(int pageNo)
{
    assert(1 <= pageNo && pageNo <= PageCount());
    if (!_mediaboxes)
        return RectD();

    RectD mbox = _mediaboxes[pageNo-1];
    if (!mbox.IsEmpty())
        return mbox;

    xps_page *page = GetXpsPage(pageNo, true);
    if (!page) {
        ScopedCritSec scope(&ctxAccess);
        fz_try(ctx) {
            mbox = xps_extract_mediabox_quick_and_dirty(_doc, pageNo);
        }
        fz_catch(ctx) { }
        if (!mbox.IsEmpty()) {
            _mediaboxes[pageNo-1] = mbox;
            return _mediaboxes[pageNo-1];
        }
    }
    if (!page && !(page = GetXpsPage(pageNo)))
        return RectD();

    _mediaboxes[pageNo-1] = fz_rect_to_RectD(xps_bound_page(_doc, page));
    return _mediaboxes[pageNo-1];
}

RectD CXpsEngine::PageContentBox(int pageNo, RenderTarget target)
{
    assert(1 <= pageNo && pageNo <= PageCount());
    xps_page *page = GetXpsPage(pageNo);
    if (!page)
        return RectD();

    fz_bbox bbox;
    fz_device *dev;
    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        dev = fz_new_bbox_device(ctx, &bbox);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return RectD();
    }

    fz_bbox mediabox = fz_round_rect(xps_bound_page(_doc, page));
    bool ok = RunPage(page, dev, fz_identity, mediabox, false);
    if (!ok)
        return PageMediabox(pageNo);
    if (fz_is_infinite_bbox(bbox))
        return PageMediabox(pageNo);

    RectD bbox2 = fz_bbox_to_RectI(bbox).Convert<double>();
    return bbox2.Intersect(PageMediabox(pageNo));
}

PointD CXpsEngine::Transform(PointD pt, int pageNo, float zoom, int rotation, bool inverse)
{
    fz_point pt2 = { (float)pt.x, (float)pt.y };
    fz_matrix ctm = viewctm(pageNo, zoom, rotation);
    if (inverse)
        ctm = fz_invert_matrix(ctm);
    pt2 = fz_transform_point(ctm, pt2);
    return PointD(pt2.x, pt2.y);
}

RectD CXpsEngine::Transform(RectD rect, int pageNo, float zoom, int rotation, bool inverse)
{
    fz_rect rect2 = fz_RectD_to_rect(rect);
    fz_matrix ctm = viewctm(pageNo, zoom, rotation);
    if (inverse)
        ctm = fz_invert_matrix(ctm);
    rect2 = fz_transform_rect(ctm, rect2);
    return fz_rect_to_RectD(rect2);
}

bool CXpsEngine::RenderPage(HDC hDC, xps_page *page, RectI screenRect, fz_matrix *ctm, float zoom, int rotation, RectD *pageRect)
{
    if (!page)
        return false;

    fz_matrix ctm2;
    if (!ctm) {
        ctm2 = viewctm(page, zoom, rotation);
        fz_rect pRect = pageRect ? fz_RectD_to_rect(*pageRect) : xps_bound_page(_doc, page);
        fz_bbox bbox = fz_round_rect(fz_transform_rect(ctm2, pRect));
        ctm2 = fz_concat(ctm2, fz_translate((float)screenRect.x - bbox.x0, (float)screenRect.y - bbox.y0));
    }
    else
        ctm2 = *ctm;

    HBRUSH bgBrush = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    FillRect(hDC, &screenRect.ToRECT(), bgBrush); // initialize white background
    DeleteObject(bgBrush);

    fz_bbox clipbox = fz_RectI_to_bbox(screenRect);
    fz_device *dev;
    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        dev = fz_new_gdiplus_device(ctx, hDC, clipbox);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return false;
    }

    return RunPage(page, dev, ctm2, clipbox);
}

RenderedBitmap *CXpsEngine::RenderBitmap(int pageNo, float zoom, int rotation, RectD *pageRect, RenderTarget target)
{
    xps_page* page = GetXpsPage(pageNo);
    if (!page)
        return NULL;

    fz_rect pRect = pageRect ? fz_RectD_to_rect(*pageRect) : xps_bound_page(_doc, page);
    fz_matrix ctm = viewctm(page, zoom, rotation);
    fz_bbox bbox = fz_round_rect(fz_transform_rect(ctm, pRect));

    // GDI+ seems to render quicker and more reliable at high zoom levels
    if ((zoom > 40.0) != gDebugGdiPlusDevice) {
        int w = bbox.x1 - bbox.x0, h = bbox.y1 - bbox.y0;
        ctm = fz_concat(ctm, fz_translate((float)-bbox.x0, (float)-bbox.y0));

        // for now, don't render directly into a DC but produce an HBITMAP instead
        HDC hDC = GetDC(NULL);
        HDC hDCMem = CreateCompatibleDC(hDC);
        HBITMAP hbmp = CreateCompatibleBitmap(hDC, w, h);
        DeleteObject(SelectObject(hDCMem, hbmp));

        RectI rc(0, 0, w, h);
        bool ok = RenderPage(hDCMem, page, rc, &ctm, 0, 0, pageRect);
        DeleteDC(hDCMem);
        ReleaseDC(NULL, hDC);
        if (!ok) {
            DeleteObject(hbmp);
            return NULL;
        }
        return new RenderedBitmap(hbmp, SizeI(w, h));
    }

    fz_pixmap *image;
    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        image = fz_new_pixmap_with_rect(ctx, fz_find_device_colorspace("DeviceRGB"), bbox);
        fz_clear_pixmap_with_color(image, 0xFF); // initialize white background
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return NULL;
    }

    fz_device *dev;
    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        dev = fz_new_draw_device(ctx, image);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return NULL;
    }

    bool ok = RunPage(page, dev, ctm, bbox);

    ScopedCritSec scope(&ctxAccess);

    RenderedBitmap *bitmap = NULL;
    if (ok)
        bitmap = new RenderedFitzBitmap(ctx, image);
    fz_drop_pixmap(ctx, image);
    return bitmap;
}

TCHAR *CXpsEngine::ExtractPageText(xps_page *page, TCHAR *lineSep, RectI **coords_out, bool cacheRun)
{
    if (!page)
        return NULL;

    fz_text_span *text = NULL;
    fz_device *dev;
    fz_var(text);

    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        text = fz_new_text_span(ctx);
        dev = fz_new_text_device(ctx, text);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        fz_free_text_span(ctx, text);
        return NULL;
    }

    // use an infinite rectangle as bounds (instead of a mediabox) to ensure that
    // the extracted text is consistent between cached runs using a list device and
    // fresh runs (otherwise the list device omits text outside the mediabox bounds)
    RunPage(page, dev, fz_identity, fz_infinite_bbox, cacheRun);

    ScopedCritSec scope(&ctxAccess);

    TCHAR *content = fz_span_to_tchar(text, lineSep, coords_out);
    fz_free_text_span(ctx, text);

    return content;
}

TCHAR *CXpsEngine::ExtractPageText(int pageNo, TCHAR *lineSep, RectI **coords_out, RenderTarget target)
{
    xps_page *page = GetXpsPage(pageNo, true);
    if (page)
        return ExtractPageText(page, lineSep, coords_out);

    EnterCriticalSection(&ctxAccess);
    fz_var(page);
    fz_try(ctx) {
        page = xps_load_page(_doc, pageNo - 1);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return NULL;
    }

    TCHAR *result = ExtractPageText(page, lineSep, coords_out);

    EnterCriticalSection(&ctxAccess);
    xps_free_page(_doc, page);
    LeaveCriticalSection(&ctxAccess);

    return result;
}

unsigned char *CXpsEngine::GetFileData(size_t *cbCount)
{
    unsigned char *data;
    ScopedCritSec scope(&ctxAccess);
    fz_try(ctx) {
        data = fz_extract_stream_data(_doc->file, cbCount);
    }
    fz_catch(ctx) {
        return _fileName ? (unsigned char *)file::ReadAll(_fileName, cbCount) : NULL;
    }
    return data;
}

TCHAR *CXpsEngine::GetProperty(char *name)
{
    // _info is guaranteed not to contain any indirect references,
    // so no need for ctxAccess
    fz_obj *obj = fz_dict_gets(_info, name);
    if (!obj)
        return NULL;

    char *utf8 = fz_to_str_buf(obj);
    if (str::IsEmpty(utf8))
        return NULL;

    return str::conv::FromUtf8(utf8);
};

PageElement *CXpsEngine::GetElementAtPos(int pageNo, PointD pt)
{
    fz_point p = { (float)pt.x, (float)pt.y };
    for (fz_link *link = _links[pageNo-1]; link; link = link->next)
        if (fz_is_pt_in_rect(link->rect, p))
            return new XpsLink(this, &link->dest, link->rect, pageNo);

    if (imageRects[pageNo-1]) {
        for (int i = 0; !fz_is_empty_rect(imageRects[pageNo-1][i]); i++)
            if (fz_is_pt_in_rect(imageRects[pageNo-1][i], p))
                return new XpsImage(this, pageNo, imageRects[pageNo-1][i], i);
    }

    return NULL;
}

Vec<PageElement *> *CXpsEngine::GetElements(int pageNo)
{
    // since all elements lists are in last-to-first order, append
    // item types in inverse order and reverse the whole list at the end
    Vec<PageElement *> *els = new Vec<PageElement *>();
    if (!els)
        return NULL;

    if (imageRects[pageNo-1]) {
        for (int i = 0; !fz_is_empty_rect(imageRects[pageNo-1][i]); i++) {
            els->Append(new XpsImage(this, pageNo, imageRects[pageNo-1][i], i));
        }
    }

    for (fz_link *link = _links[pageNo-1]; link; link = link->next) {
        els->Append(new XpsLink(this, &link->dest, link->rect, pageNo));
    }

    els->Reverse();
    return els;
}

void CXpsEngine::LinkifyPageText(xps_page *page, int pageNo)
{
    // make MuXPS extract all links and named destinations from the page
    assert(!GetPageRun(page, true));
    XpsPageRun *run = GetPageRun(page, false, pageNo);
    assert(run);
    if (run)
        DropPageRun(run);
    _links[pageNo-1] = FixupPageLinks(_links[pageNo-1]);

    RectI *coords;
    TCHAR *pageText = ExtractPageText(page, _T("\n"), &coords, true);
    if (!pageText)
        return;

    LinkRectList *list = LinkifyText(pageText, coords);
    for (size_t i = 0; i < list->links.Count(); i++) {
        bool overlaps = false;
        for (fz_link *next = _links[pageNo-1]; next && !overlaps; next = next->next)
            overlaps = fz_calc_overlap(next->rect, list->coords.At(i)) >= 0.25f;
        if (!overlaps) {
            ScopedMem<char> uri(str::conv::ToUtf8(list->links.At(i)));
            if (!uri) continue;
            fz_link_dest ld = { FZ_LINK_URI, 0 };
            ld.ld.uri.uri = fz_strdup(ctx, uri);
            // add links in top-to-bottom order (i.e. last-to-first)
            fz_link *link = fz_new_link(ctx, list->coords.At(i), ld);
            link->next = _links[pageNo-1];
            _links[pageNo-1] = link;
        }
    }

    delete list;
    delete[] coords;
    free(pageText);
}

RenderedBitmap *CXpsEngine::GetPageImage(int pageNo, RectD rect, size_t imageIx)
{
    xps_page *page = GetXpsPage(pageNo);
    if (!page)
        return NULL;

    Vec<FitzImagePos> positions;
    ListInspectionData data = { &positions, false, 0 };
    fz_device *dev;

    EnterCriticalSection(&ctxAccess);
    fz_try(ctx) {
        dev = fz_new_inspection_device(ctx, &data);
    }
    fz_always(ctx) {
        LeaveCriticalSection(&ctxAccess);
    }
    fz_catch(ctx) {
        return NULL;
    }

    RunPage(page, dev, fz_identity);

    if (imageIx >= positions.Count() || fz_rect_to_RectD(positions.At(imageIx).rect) != rect) {
        assert(0);
        return NULL;
    }

    ScopedCritSec scope(&ctxAccess);
    return new RenderedFitzBitmap(ctx, positions.At(imageIx).image);
}

fz_rect CXpsEngine::FindDestRect(const char *target)
{
    if (str::IsEmpty(target))
        return fz_empty_rect;

    xps_target *found = xps_find_link_target_obj(_doc, (char *)target);
    if (!found)
        return fz_empty_rect;
    if (fz_is_empty_rect(found->rect)) {
        // ensure that the target rectangle could have been
        // updated through LinkifyPageText -> xps_extract_anchor_info
        GetXpsPage(found->page);
    }
    return found->rect;
}

PageDestination *CXpsEngine::GetNamedDest(const TCHAR *name)
{
    ScopedMem<char> name_utf8(str::conv::ToUtf8(name));
    if (!str::StartsWith(name_utf8.Get(), "#"))
        name_utf8.Set(str::Join("#", name_utf8));

    for (xps_target *dest = _doc->target; dest; dest = dest->next)
        if (str::EndsWithI(dest->name, name_utf8))
            return new SimpleDest(dest->page + 1, fz_rect_to_RectD(dest->rect));

    return NULL;
}

XpsTocItem *CXpsEngine::BuildTocTree(fz_outline *entry, int& idCounter)
{
    XpsTocItem *node = NULL;

    for (; entry; entry = entry->next) {
        TCHAR *name = entry->title ? str::conv::FromUtf8(entry->title) : str::Dup(_T(""));
        XpsTocItem *item = new XpsTocItem(name, XpsLink(this, &entry->dest));
        item->id = ++idCounter;
        item->open = entry->is_open;

        if (FZ_LINK_GOTO == entry->dest.kind)
            item->pageNo = entry->dest.ld.gotor.page + 1;
        if (entry->down)
            item->child = BuildTocTree(entry->down, idCounter);

        if (!node)
            node = item;
        else
            node->AddSibling(item);
    }

    return node;
}

DocTocItem *CXpsEngine::GetTocTree()
{
    if (!HasTocTree())
        return NULL;

    int idCounter = 0;
    return BuildTocTree(_outline, idCounter);
}

bool CXpsEngine::IsImagePage(int pageNo)
{
    xps_page *page = GetXpsPage(pageNo, true);
    // GetXpsPage extracts imageRects for us
    if (!page || !imageRects[pageNo-1])
        return false;

    fz_rect mbox = fz_RectD_to_rect(PageMediabox(pageNo));
    // check if any image covers at least 90% of the page
    for (int i = 0; !fz_is_empty_rect(imageRects[pageNo-1][i]); i++)
        if (fz_calc_overlap(mbox, imageRects[pageNo-1][i]) >= 0.9f)
            return true;
    return false;
}

bool XpsEngine::IsSupportedFile(const TCHAR *fileName, bool sniff)
{
    if (sniff) {
        // this check is technically not correct (ZIP files are read from back to front),
        // but it should catch all but specially crafted ZIP files anyway
        return file::StartsWith(fileName, "PK\x03\x04");
    }

    return str::EndsWithI(fileName, _T(".xps"));
}

XpsEngine *XpsEngine::CreateFromFileName(const TCHAR *fileName)
{
    CXpsEngine *engine = new CXpsEngine();
    if (!engine || !fileName || !engine->Load(fileName)) {
        delete engine;
        return NULL;
    }
    return engine;
}

XpsEngine *XpsEngine::CreateFromStream(IStream *stream)
{
    CXpsEngine *engine = new CXpsEngine();
    if (!engine->Load(stream)) {
        delete engine;
        return NULL;
    }
    return engine;
}
