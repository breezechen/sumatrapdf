/* Copyright 2006-2010 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "SumatraPDF.h"
#include "RenderCache.h"

/* Define if you want to conserve memory by always freeing cached bitmaps
   for pages not visible. Disabling this might lead to pages not rendering
   due to insufficient (GDI) memory. */
#define CONSERVE_MEMORY

static DWORD WINAPI PageRenderThread(LPVOID data);

RenderCache::RenderCache(void)
    : _cacheCount(0), _requestCount(0), invertColors(NULL), useGdiRenderer(NULL)
{
    InitializeCriticalSection(&_access);

    LONG semMaxCount = 1000; /* don't really know what the limit should be */
    renderSemaphore = CreateSemaphore(NULL, 0, semMaxCount, NULL);
    clearQueueEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    _queueClearedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    _renderThread = CreateThread(NULL, 0, PageRenderThread, this, 0, 0);
    assert(NULL != _renderThread);
}

RenderCache::~RenderCache(void)
{
    CloseHandle(_renderThread);
    CloseHandle(_queueClearedEvent);
    CloseHandle(clearQueueEvent);
    CloseHandle(renderSemaphore);

    DeleteCriticalSection(&_access);
}

/* Find a bitmap for a page defined by <dm> and <pageNo> and optionally also
   <rotation> and <zoomLevel> in the cache */
BitmapCacheEntry *RenderCache::Find(DisplayModel *dm, int pageNo, int rotation, double zoomLevel)
{
    BitmapCacheEntry *entry;
    normalizeRotation(&rotation);
    Lock();
    for (int i = 0; i < _cacheCount; i++) {
        entry = _cache[i];
        if ((dm == entry->dm) && (pageNo == entry->pageNo) && (rotation == entry->rotation) &&
            (INVALID_ZOOM == zoomLevel || zoomLevel == entry->zoomLevel)) {
            goto Exit;
        }
    }
    entry = NULL;
Exit:
    Unlock();
    return entry;
}

void RenderCache::Add(DisplayModel *dm, int pageNo, int rotation, double zoomLevel, RenderedBitmap *bitmap, double renderTime)
{
    assert(_cacheCount <= MAX_BITMAPS_CACHED);
    assert(dm);
    assert(validRotation(rotation));

    normalizeRotation(&rotation);
    DBG_OUT("BitmapCache_Add(pageNo=%d, rotation=%d, zoomLevel=%.2f%%)\n", pageNo, rotation, zoomLevel);
    Lock();

    /* It's possible there still is a cached bitmap with different zoomLevel/rotation */
    FreePage(dm, pageNo);

    if (_cacheCount >= MAX_BITMAPS_CACHED) {
        // free an invisible page of the same DisplayModel ...
        for (int i = 0; i < _cacheCount; i++) {
            if (_cache[i]->dm == dm && !dm->pageVisibleNearby(_cache[i]->pageNo)) {
                Free(_cache[i]);
                _cacheCount--;
                memmove(&_cache[i], &_cache[i + 1], (_cacheCount - i) * sizeof(_cache[0]));
                break;
            }
        }
        // ... or just the oldest cached page
        if (_cacheCount >= MAX_BITMAPS_CACHED) {
            Free(_cache[0]);
            _cacheCount--;
            memmove(&_cache[0], &_cache[1], _cacheCount * sizeof(_cache[0]));
        }
    }

    BitmapCacheEntry entry = { dm, pageNo, rotation, zoomLevel, bitmap, renderTime };
    _cache[_cacheCount] = (BitmapCacheEntry *)_memdup(&entry);
    assert(_cache[_cacheCount]);
    if (!_cache[_cacheCount])
        delete bitmap;
    else
        _cacheCount++;
    dm->ageStore();
    Unlock();
}

/* Free all bitmaps in the cache that are of a specific page (or all pages
   of the given DisplayModel, or even all invisible pages). Returns TRUE if freed
   at least one item. */
bool RenderCache::FreePage(DisplayModel *dm, int pageNo)
{
    Lock();
    int cacheCount = _cacheCount;
    bool freedSomething = false;
    int curPos = 0;

    for (int i = 0; i < cacheCount; i++) {
        BitmapCacheEntry* entry = _cache[i];
        bool shouldFree;
        if (dm && pageNo != INVALID_PAGE_NO) // a specific page
            shouldFree = (entry->dm == dm) && (entry->pageNo == pageNo);
        else if (dm) // all pages of this DisplayModel
            shouldFree = (_cache[i]->dm == dm);
        else // all invisible pages
            shouldFree = !entry->dm->pageVisibleNearby(entry->pageNo);

        if (shouldFree) {
            if (!freedSomething)
                DBG_OUT("BitmapCache_FreePage(%#x, %d) ", dm, pageNo);
            DBG_OUT("freed %d ", entry->pageNo);
            freedSomething = true;
            Free(entry);
            _cache[i] = NULL;
            _cacheCount--;
        }

        if (curPos != i)
            _cache[curPos] = _cache[i];
        if (!shouldFree)
            curPos++;
    }

    Unlock();
    if (freedSomething)
        DBG_OUT("\n");
    return freedSomething;
}

void RenderCache::KeepForDisplayModel(DisplayModel *oldDm, DisplayModel *newDm)
{
    Lock();
    for (int i = 0; i < _cacheCount; i++) {
        // keep the cached bitmaps for visible pages to avoid flickering during a reload
        if (_cache[i]->dm == oldDm && oldDm->pageVisible(_cache[i]->pageNo)) {
            _cache[i]->dm = newDm;
            // make sure that the page is rerendered eventually
            _cache[i]->zoomLevel = INVALID_ZOOM;
            _cache[i]->bitmap->outOfDate = true;
        }
    }
    Unlock();
}

/* Free all bitmaps cached for a given <dm>. Returns TRUE if freed
   at least one item. */
bool RenderCache::FreeForDisplayModel(DisplayModel *dm)
{
    CancelRendering(dm);
    return FreePage(dm);
}

/* Free all bitmaps in the cache that are not visible. Returns TRUE if freed
   at least one item. */
bool RenderCache::FreeNotVisible(void)
{
    return FreePage();
}

void RenderCache::Free(BitmapCacheEntry *entry) {
    assert(entry);
    if (!entry) return;
    delete entry->bitmap;
    free((void*)entry);
}


/* Render a bitmap for page <pageNo> in <dm>. */
void RenderCache::Render(DisplayModel *dm, int pageNo)
{
    DBG_OUT("RenderQueue_Add(pageNo=%d)\n", pageNo);
    assert(dm);
    if (!dm || dm->_dontRenderFlag) goto Exit;

    Lock();
    int rotation = dm->rotation();
    normalizeRotation(&rotation);
    double zoomLevel = dm->zoomReal(pageNo);

    if (_curReq && (_curReq->pageNo == pageNo) && (_curReq->dm == dm)) {
        if ((_curReq->zoomLevel != zoomLevel) || (_curReq->rotation != rotation)) {
            /* Currently rendered page is for the same page but with different zoom
            or rotation, so abort it */
            DBG_OUT("  aborting rendering\n");
            _curReq->abort = TRUE;
        } else {
            /* we're already rendering exactly the same page */
            DBG_OUT("  already rendering this page\n");
            goto LeaveCsAndExit;
        }
    }

    for (int i=0; i < _requestCount; i++) {
        PageRenderRequest* req = &(_requests[i]);
        if ((req->pageNo == pageNo) && (req->dm == dm)) {
            if ((req->zoomLevel == zoomLevel) && (req->rotation == rotation)) {
                /* Request with exactly the same parameters already queued for
                   rendering. Move it to the top of the queue so that it'll
                   be rendered faster. */
                PageRenderRequest tmp;
                tmp = _requests[_requestCount-1];
                _requests[_requestCount-1] = *req;
                *req = tmp;
                DBG_OUT("  already queued\n");
                goto LeaveCsAndExit;
            } else {
                /* There was a request queued for the same page but with different
                   zoom or rotation, so only replace this request */
                DBG_OUT("Replacing request for page %d with new request\n", req->pageNo);
                req->zoomLevel = zoomLevel;
                req->rotation = rotation;
                goto LeaveCsAndExit;
            
            }
        }
    }

    if (Find(dm, pageNo, rotation, zoomLevel)) {
        /* This page has already been rendered in the correct dimensions
           and isn't about to be rerendered in different dimensions */
        goto LeaveCsAndExit;
    }

    PageRenderRequest* newRequest;
    /* add request to the queue */
    if (_requestCount == MAX_PAGE_REQUESTS) {
        /* queue is full -> remove the oldest items on the queue */
        memmove(&(_requests[0]), &(_requests[1]), sizeof(PageRenderRequest)*(MAX_PAGE_REQUESTS-1));
        newRequest = &(_requests[MAX_PAGE_REQUESTS-1]);
    } else {
        newRequest = &(_requests[_requestCount]);
        _requestCount++;
    }
    assert(_requestCount <= MAX_PAGE_REQUESTS);
    newRequest->dm = dm;
    newRequest->pageNo = pageNo;
    newRequest->zoomLevel = zoomLevel;
    newRequest->rotation = rotation;
    newRequest->abort = FALSE;
    newRequest->timestamp = GetTickCount();

    Unlock();

    /* tell rendering thread there's a new request to render */
    LONG  prevCount;
    ReleaseSemaphore(renderSemaphore, 1, &prevCount);
Exit:
    return;
LeaveCsAndExit:
    Unlock();
    return;
}

UINT RenderCache::GetRenderDelay(DisplayModel *dm, int pageNo)
{
    bool foundReq = false;
    DWORD timestamp;

    Lock();
    if (_curReq && _curReq->pageNo == pageNo && _curReq->dm == dm) {
        timestamp = _curReq->timestamp;
        foundReq = true;
    }
    for (int i = 0; !foundReq && i < _requestCount; i++) {
        if (_requests[i].pageNo == pageNo && _requests[i].dm == dm) {
            timestamp = _requests[i].timestamp;
            foundReq = true;
        }
    }
    Unlock();

    if (!foundReq)
        return RENDER_DELAY_UNDEFINED;
    return GetTickCount() - timestamp;
}

bool RenderCache::GetNextRequest(PageRenderRequest *req)
{
    Lock();
    if (_requestCount == 0) {
        Unlock();
        return false;
    }

    assert(_requestCount > 0);
    assert(_requestCount <= MAX_PAGE_REQUESTS);
    _requestCount--;
    *req = _requests[_requestCount];
    _curReq = req;
    assert(_requestCount >= 0);
    Unlock();

    return true;
}

bool RenderCache::ClearCurrentRequest(void)
{
    Lock();
    _curReq = NULL;
    bool isQueueEmpty = _requestCount == 0;
    Unlock();

    return isQueueEmpty;
}

void RenderCache::ClearRequests(void)
{
    Lock();
    _requestCount = 0;
    Unlock();

    // Signal that the queue is cleared
    SetEvent(_queueClearedEvent);
}

/* Wait until rendering of a page beloging to <dm> has finished. */
/* TODO: this might take some time, would be good to show a dialog to let the
   user know he has to wait until we finish */
void RenderCache::CancelRendering(DisplayModel *dm)
{
    DBG_OUT("cancelRenderingForDisplayModel()\n");
    bool renderingFinished = false;
    while (!renderingFinished) {
        Lock();
        if (!_curReq || (_curReq->dm != dm))
            renderingFinished = true;
        else
            _curReq->abort = TRUE;
        Unlock();

        /* TODO: busy loop is not good, but I don't have a better idea */
        if (!renderingFinished)
            sleep_milliseconds(100);
    }

    ClearQueueForDisplayModel(dm);
}

void RenderCache::ClearQueueForDisplayModel(DisplayModel *dm)
{
    Lock();
    int reqCount = _requestCount;
    int curPos = 0;
    for (int i = 0; i < reqCount; i++) {
        PageRenderRequest *req = &(_requests[i]);
        bool shouldRemove = (req->dm == dm);
        if (i != curPos)
            _requests[curPos] = _requests[i];
        if (shouldRemove)
            _requestCount--;
        else
            curPos++;
    }
    Unlock();
}

// Clear all the requests from the PageRender queue.
void RenderCache::ClearPageRenderRequests()
{
    SetEvent(clearQueueEvent);
    WaitForSingleObject(_queueClearedEvent, INFINITE);
}


static BOOL pageRenderAbortCb(LPVOID data)
{
    PageRenderRequest *req = (PageRenderRequest *)data;
    if (!req->abort)
        return FALSE;

    DBG_OUT("Rendering of page %d aborted\n", req->pageNo);
    return TRUE;
}

static DWORD WINAPI PageRenderThread(LPVOID data)
{
    RenderCache *cache = (RenderCache *)data;
    PageRenderRequest   req;
    RenderedBitmap *    bmp;

    DBG_OUT("PageRenderThread() started\n");
    for (;;) {
        //DBG_OUT("Worker: wait\n");
        if (cache->ClearCurrentRequest()) {
            HANDLE handles[2] = { cache->renderSemaphore, cache->clearQueueEvent };
            DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            // is it a 'clear requests' request?
            if (WAIT_OBJECT_0+1 == waitResult) {
                cache->ClearRequests();
            }
            // Is it not a page render request?
            else if (WAIT_OBJECT_0 != waitResult) {
                DBG_OUT("  WaitForSingleObject() failed\n");
                continue;
            }
        }

        if (!cache->GetNextRequest(&req))
            continue;
        DBG_OUT("PageRenderThread(): dequeued %d\n", req.pageNo);
        if (!req.dm->pageVisibleNearby(req.pageNo)) {
            DBG_OUT("PageRenderThread(): not rendering because not visible\n");
            continue;
        }
        if (req.dm->_dontRenderFlag) {
            DBG_OUT("PageRenderThread(): not rendering because of _dontRenderFlag\n");
            continue;
        }

        assert(!req.abort);
        MsTimer renderTimer;
        bmp = req.dm->renderBitmap(req.pageNo, req.zoomLevel, req.rotation, NULL, pageRenderAbortCb, (void*)&req, Target_View, cache->useGdiRenderer && *cache->useGdiRenderer);
        renderTimer.stop();
        cache->ClearCurrentRequest();
        if (req.abort) {
            delete bmp;
            continue;
        }

        if (bmp && cache->invertColors && *cache->invertColors)
            bmp->invertColors();
        if (bmp)
            DBG_OUT("PageRenderThread(): finished rendering %d\n", req.pageNo);
        else
            DBG_OUT("PageRenderThread(): failed to render a bitmap of page %d\n", req.pageNo);
        double renderTime = renderTimer.timeInMs();
        cache->Add(req.dm, req.pageNo, req.rotation, req.zoomLevel, bmp, renderTime);
#ifdef CONSERVE_MEMORY
        cache->FreeNotVisible();
#endif
        req.dm->repaintDisplay();
    }

    DBG_OUT("PageRenderThread() finished\n");
    return 0;
}
