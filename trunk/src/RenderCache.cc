/* Copyright 2006-2010 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "SumatraPDF.h"
#include "RenderCache.h"

// keep this value reasonably low, else we'll run
// out of GDI memory when caching many larger bitmaps
#define MAX_BITMAPS_CACHED 64

static CRITICAL_SECTION     cacheMutex;
static int cacheMutexInitialized = 0;
static BitmapCacheEntry *   gBitmapCache[MAX_BITMAPS_CACHED] = {0};
static int                  gBitmapCacheCount = 0;

static inline void InitCacheMutext() {
    if (!cacheMutexInitialized) {
        InitializeCriticalSection(&cacheMutex);
        cacheMutexInitialized = 1;
    }
}

void LockCache(void) {
    InitCacheMutext();
    EnterCriticalSection(&cacheMutex);
}

void UnlockCache(void) {
    LeaveCriticalSection(&cacheMutex);
}

static void BitmapCacheEntry_Free(BitmapCacheEntry *entry) {
    assert(entry);
    if (!entry) return;
    delete entry->bitmap;
    free((void*)entry);
}

/* Free all bitmaps in the cache that are of a specific page (or all pages
   of the given DisplayModel, or even all invisible pages). Returns TRUE if freed
   at least one item. */
bool BitmapCache_FreePage(DisplayModel *dm, int pageNo) {
    LockCache();
    int cacheCount = gBitmapCacheCount;
    bool freedSomething = false;
    int curPos = 0;

    for (int i = 0; i < cacheCount; i++) {
        BitmapCacheEntry* entry = gBitmapCache[i];
        bool shouldFree;
        if (dm && pageNo != INVALID_PAGE_NO) // a specific page
            shouldFree = (entry->dm == dm) && (entry->pageNo == pageNo);
        else if (dm) // all pages of this DisplayModel
            shouldFree = (gBitmapCache[i]->dm == dm);
        else // all invisible pages
            shouldFree = !entry->dm->pageVisibleNearby(entry->pageNo);

        if (shouldFree) {
            if (!freedSomething)
                DBG_OUT("BitmapCache_FreePage(%#x, %d) ", dm, pageNo);
            DBG_OUT("freed %d ", entry->pageNo);
            freedSomething = true;
            BitmapCacheEntry_Free(entry);
            gBitmapCache[i] = NULL;
            --gBitmapCacheCount;
        }

        if (curPos != i)
            gBitmapCache[curPos] = gBitmapCache[i];
        if (!shouldFree)
            ++curPos;
    }

    UnlockCache();
    if (freedSomething)
        DBG_OUT("\n");
    return freedSomething;
}


/* Free all bitmaps in the cache that are not visible. Returns TRUE if freed
   at least one item. */
bool BitmapCache_FreeNotVisible(void) {
    return BitmapCache_FreePage();
}

/* Free all bitmaps cached for a given <dm>. Returns TRUE if freed
   at least one item. */
bool BitmapCache_FreeForDisplayModel(DisplayModel *dm) {
    return BitmapCache_FreePage(dm);
}

void BitmapCache_KeepForDisplayModel(DisplayModel *oldDm, DisplayModel *newDm) {
    LockCache();
    for (int i = 0; i < gBitmapCacheCount; i++)
    {
        // keep the cached bitmaps for visible pages to avoid flickering during a reload
        if (gBitmapCache[i]->dm == oldDm && oldDm->pageVisible(gBitmapCache[i]->pageNo))
        {
            gBitmapCache[i]->dm = newDm;
            // make sure that the page is rerendered eventually
            gBitmapCache[i]->zoomLevel = INVALID_ZOOM;
            gBitmapCache[i]->bitmap->outOfDate = true;
        }
    }
    UnlockCache();
}

void BitmapCache_Add(DisplayModel *dm, int pageNo, int rotation, double zoomLevel,
    RenderedBitmap *bitmap, double renderTime) {
    assert(gBitmapCacheCount <= MAX_BITMAPS_CACHED);
    assert(dm);
    assert(validRotation(rotation));

    normalizeRotation(&rotation);
    DBG_OUT("BitmapCache_Add(pageNo=%d, rotation=%d, zoomLevel=%.2f%%)\n", pageNo, rotation, zoomLevel);
    LockCache();

    /* It's possible there still is a cached bitmap with different zoomLevel/rotation */
    BitmapCache_FreePage(dm, pageNo);

    if (gBitmapCacheCount >= MAX_BITMAPS_CACHED) {
        // free an invisible page of the same DisplayModel ...
        for (int i = 0; i < gBitmapCacheCount; i++) {
            if (gBitmapCache[i]->dm == dm && !dm->pageVisibleNearby(gBitmapCache[i]->pageNo)) {
                BitmapCacheEntry_Free(gBitmapCache[i]);
                gBitmapCacheCount--;
                memmove(&gBitmapCache[i], &gBitmapCache[i + 1], (gBitmapCacheCount - i) * sizeof(gBitmapCache[0]));
                break;
            }
        }
        // ... or just the oldest cached page
        if (gBitmapCacheCount >= MAX_BITMAPS_CACHED) {
            BitmapCacheEntry_Free(gBitmapCache[0]);
            gBitmapCacheCount--;
            memmove(&gBitmapCache[0], &gBitmapCache[1], gBitmapCacheCount * sizeof(gBitmapCache[0]));
        }
    }

    BitmapCacheEntry entry = { dm, pageNo, rotation, zoomLevel, bitmap, renderTime };
    gBitmapCache[gBitmapCacheCount] = (BitmapCacheEntry *)_memdup(&entry);
    assert(gBitmapCache[gBitmapCacheCount]);
    if (!gBitmapCache[gBitmapCacheCount])
        delete bitmap;
    else
        gBitmapCacheCount++;
    dm->ageStore();
    UnlockCache();
}

BitmapCacheEntry *BitmapCache_Find(DisplayModel *dm, int pageNo, int rotation, double zoomLevel) {
    BitmapCacheEntry *entry;
    normalizeRotation(&rotation);
    LockCache();
    for (int i = 0; i < gBitmapCacheCount; i++) {
        entry = gBitmapCache[i];
        if ((dm == entry->dm) && (pageNo == entry->pageNo) && (rotation == entry->rotation) &&
            (INVALID_ZOOM == zoomLevel || zoomLevel == entry->zoomLevel)) {
            goto Exit;
        }
    }
    entry = NULL;
Exit:
    UnlockCache();
    return entry;
}

/* Return true if a bitmap for a page defined by <dm>, <pageNo>, <rotation>
   and <zoomLevel> exists in the cache */
bool BitmapCache_Exists(DisplayModel *dm, int pageNo, int rotation, double zoomLevel) {
    if (BitmapCache_Find(dm, pageNo, rotation, zoomLevel))
        return true;
    return false;
}
