/* Copyright Krzysztof Kowalczyk 2006-2009
   License: GPLv3 */
#ifndef _RENDER_CACHE_H_
#define _RENDER_CACHE_H_

#include "DisplayModel.h"

#define RENDER_DELAY_UNDEFINED ((UINT)-1)

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
    int             abort;
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
    CRITICAL_SECTION    _access;

    PageRenderRequest   _requests[MAX_PAGE_REQUESTS];
    int                 _requestCount;
    HANDLE              _renderThread;
    HANDLE              _queueClearedEvent;
    PageRenderRequest * _curReq;

public:
    HANDLE              renderSemaphore;
    HANDLE              clearQueueEvent;

    /* point these to the actual preferences for live updates */
    BOOL              * invertColors;
    bool              * useGdiRenderer;

    RenderCache(void);
    ~RenderCache(void);

    /* Lock protecting both bitmap cache and page render queue */
    void                Lock(void) { EnterCriticalSection(&_access); }
    void                Unlock(void) { LeaveCriticalSection(&_access); }

    BitmapCacheEntry *  Find(DisplayModel *dm, int pageNo, int rotation, double zoomLevel=INVALID_ZOOM);
    void                Add(DisplayModel *dm, int pageNo, int rotation, double zoomLevel,
                            RenderedBitmap *bitmap, double renderTime);
    bool                FreePage(DisplayModel *dm=NULL, int pageNo=-1);
    bool                FreeForDisplayModel(DisplayModel *dm);
    void                KeepForDisplayModel(DisplayModel *oldDm, DisplayModel *newDm);
    bool                FreeNotVisible(void);

    void                Render(DisplayModel *dm, int pageNo);
    bool                IsRenderQueueFull(void) const {
        return _requestCount == MAX_PAGE_REQUESTS;
    }
    UINT                GetRenderDelay(DisplayModel *dm, int pageNo);
    bool                GetNextRequest(PageRenderRequest *req);
    bool                ClearCurrentRequest(void);
    void                ClearRequests(void);
    void                CancelRendering(DisplayModel *dm);
    void                ClearPageRenderRequests();

private:
    void                Free(BitmapCacheEntry *entry);
    void                ClearQueueForDisplayModel(DisplayModel *dm);
};

#endif
