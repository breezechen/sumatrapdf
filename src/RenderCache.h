/* Copyright Krzysztof Kowalczyk 2006-2009
   License: GPLv3 */
#ifndef _RENDER_CACHE_H_
#define _RENDER_CACHE_H_

#include "DisplayModel.h"

#define RENDER_DELAY_UNDEFINED ((UINT)-1)
#define RENDER_DELAY_FAILED    ((UINT)-2)

/* We keep a cache of rendered bitmaps. BitmapCacheEntry keeps data
   that uniquely identifies rendered page (dm, pageNo, rotation, zoomReal)
   and corresponding rendered bitmap.
*/
typedef struct {
  DisplayModel * dm;
  int            pageNo;
  int            rotation;
  double         zoomLevel;
  RenderedBitmap *bitmap;
  double         renderTime;
} BitmapCacheEntry;

typedef struct {
    DisplayModel *  dm;
    int             pageNo;
    int             rotation;
    double          zoomLevel;
    BOOL            abort;
    DWORD           timestamp;
} PageRenderRequest;

#define MAX_PAGE_REQUESTS 8

// keep this value reasonably low, else we'll run
// out of GDI memory when caching many larger bitmaps
#define MAX_BITMAPS_CACHED 64

class RenderCache
{
private:
    BitmapCacheEntry *  _cache[MAX_BITMAPS_CACHED];
    int                 _cacheCount;
    CRITICAL_SECTION    _cacheAccess;

    PageRenderRequest   _requests[MAX_PAGE_REQUESTS];
    int                 _requestCount;
    PageRenderRequest * _curReq;
    CRITICAL_SECTION    _requestAccess;
    HANDLE              _renderThread;

public:
    HANDLE              startRendering;

    /* point these to the actual preferences for live updates */
    BOOL              * invertColors;
    bool              * useGdiRenderer;

    RenderCache(void);
    ~RenderCache(void);

    void                Render(DisplayModel *dm, int pageNo);
    void                CancelRendering(DisplayModel *dm);
    bool                FreeForDisplayModel(DisplayModel *dm);
    void                KeepForDisplayModel(DisplayModel *oldDm, DisplayModel *newDm);
    UINT                Paint(HDC hdc, RECT *bounds, DisplayModel *dm, int pageNo,
                              PdfPageInfo *pageInfo, bool *renderOutOfDateCue);

    bool                ClearCurrentRequest(void);
    bool                GetNextRequest(PageRenderRequest *req);
    void                Add(DisplayModel *dm, int pageNo, int rotation, double zoomLevel,
                            RenderedBitmap *bitmap, double renderTime);
    bool                FreeNotVisible(void);

private:
    bool                IsRenderQueueFull(void) const {
        return _requestCount == MAX_PAGE_REQUESTS;
    }
    UINT                GetRenderDelay(DisplayModel *dm, int pageNo);
    void                ClearQueueForDisplayModel(DisplayModel *dm);

    BitmapCacheEntry *  Find(DisplayModel *dm, int pageNo, int rotation, double zoomLevel=INVALID_ZOOM);
    bool                FreePage(DisplayModel *dm=NULL, int pageNo=-1);
    void                Free(BitmapCacheEntry *entry);
};

#endif
