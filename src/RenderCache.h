/* Copyright Krzysztof Kowalczyk 2006-2009
   License: GPLv3 */
#ifndef _RENDER_CACHE_H_
#define _RENDER_CACHE_H_

#include "DisplayModel.h"

#define RENDER_DELAY_UNDEFINED ((UINT)-1)
#define RENDER_DELAY_FAILED    ((UINT)-2)
#define INVALID_TILE_RES       ((USHORT)-1)

// TODO: figure out more optimal tile size (e.g. screen or canvas size?)
#define TILE_MAX_W 1000
#define TILE_MAX_H 1000

/* A page is split into tiles of at most TILE_MAX_W x TILE_MAX_H pixels.
 * A given tile starts at (col / 2^res * page_width, row / 2^res * page_height). */
typedef struct TilePosition {
    USHORT res, row, col;

    bool operator==(TilePosition other) {
        return res == other.res && row == other.row && col == other.col;
    }
} TilePosition;

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
  TilePosition   tile;
  int            refs;
} BitmapCacheEntry;

typedef struct {
    DisplayModel *  dm;
    int             pageNo;
    int             rotation;
    double          zoomLevel;
    TilePosition    tile;
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
    // make sure to never ask for _requestAccess in a _cacheAccess
    // protected critical section in order to avoid deadlocks
    CRITICAL_SECTION    _cacheAccess;

    PageRenderRequest   _requests[MAX_PAGE_REQUESTS];
    int                 _requestCount;
    PageRenderRequest * _curReq;
    CRITICAL_SECTION    _requestAccess;
    HANDLE              _renderThread;

public:
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

public:
    /* Interface for page rendering thread */
    HANDLE              startRendering;

    bool                ClearCurrentRequest(void);
    bool                GetNextRequest(PageRenderRequest *req);
    void                Add(DisplayModel *dm, int pageNo, int rotation, double zoomLevel,
                            TilePosition tile, RenderedBitmap *bitmap);
    bool                FreeNotVisible(void);

private:
    bool                IsRenderQueueFull(void) const {
                            return _requestCount == MAX_PAGE_REQUESTS;
                        }
    UINT                GetRenderDelay(DisplayModel *dm, int pageNo, TilePosition tile);
    void                Render(DisplayModel *dm, int pageNo, TilePosition tile);
    void                ClearQueueForDisplayModel(DisplayModel *dm, int pageNo=INVALID_PAGE_NO,
                                                  TilePosition *tile=NULL);

    BitmapCacheEntry *  Find(DisplayModel *dm, int pageNo, int rotation,
                             double zoomLevel=INVALID_ZOOM, TilePosition *tile=NULL);
    void                DropCacheEntry(BitmapCacheEntry *entry);
    bool                FreePage(DisplayModel *dm=NULL, int pageNo=-1, TilePosition *tile=NULL);

    UINT                PaintTile(HDC hdc, RectI *bounds, DisplayModel *dm, int pageNo,
                                  TilePosition tile, RectI *tileOnScreen, bool renderMissing,
                                  bool *renderOutOfDateCue, bool *renderedReplacement);
    UINT                PaintTiles(HDC hdc, RECT *bounds, DisplayModel *dm, int pageNo,
                                   RectI *pageOnScreen, USHORT tileRes, bool renderMissing,
                                   bool *renderOutOfDateCue, bool *renderedReplacement);
};

#endif
