/* Copyright Krzysztof Kowalczyk 2006-2009
   License: GPLv3 */
#ifndef _RENDER_CACHE_H_
#define _RENDER_CACHE_H_

#include "DisplayModel.h"

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

/* Lock protecting both bitmap cache and page render queue */
void              LockCache();
void              UnlockCache();

void              RenderQueue_Add(DisplayModel *dm, int pageNo);
extern void       RenderQueue_RemoveForDisplayModel(DisplayModel *dm);
extern void       cancelRenderingForDisplayModel(DisplayModel *dm);

BitmapCacheEntry *BitmapCache_Find(DisplayModel *dm, int pageNo, int rotation, double zoomLevel=INVALID_ZOOM);
bool              BitmapCache_Exists(DisplayModel *dm, int pageNo, int rotation, double zoomLevel);
void              BitmapCache_Add(DisplayModel *dm, int pageNo, int rotation, double zoomLevel,
                                  RenderedBitmap *bitmap, double renderTime);
bool              BitmapCache_FreePage(DisplayModel *dm=NULL, int pageNo=-1);
bool              BitmapCache_FreeForDisplayModel(DisplayModel *dm);
void              BitmapCache_KeepForDisplayModel(DisplayModel *oldDm, DisplayModel *newDm);
bool              BitmapCache_FreeNotVisible(void);

#endif
