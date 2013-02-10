/* Copyright 2013 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "BaseUtil.h"
#include "FileWatcher.h"

#include "FileUtil.h"
#include "ThreadUtil.h"
#include "WinUtil.h"

#define NOLOG 0
#include "DebugLog.h"

/*
This is APC-based version of file watcher, based on information in: http://qualapps.blogspot.com/2010/05/understanding-readdirectorychangesw.html

Advantages over overlapped.hEvent handle based:
 - no limit on the number of watched directories
 - hopefully get rid of the problem with overlapped.hEvent being changed
   after memory has been freed.
*/

/*
TODO:

  - it doesn't work at all (ReadDirectoryChangesNotification() is never called even though
    files are being modified)

  - handle files on non-fixed drives (network drives, usb) by using
    a timeout in the thread

  - should I end the thread when there are no files to watch?

  - a single file copy can generate multiple notifications for the same
    file. add some delay mechanism so that subsequent change notifications
    cancel a previous, delayed one ? E.g. a copy f2.pdf f.pdf generates 3
    notifications if f2.pdf is 2 MB.

  - implement it the way http://qualapps.blogspot.com/2010/05/understanding-readdirectorychangesw.html
    suggests?
*/

#define INVALID_TOKEN 0

#define FILEWATCH_DELAY_IN_MS       1000

struct OverlappedEx {
    OVERLAPPED      overlapped;
    void *          data;
};

struct WatchedDir {
    WatchedDir *                next;
    const WCHAR *               dirPath;
    HANDLE                      hDir;
    OverlappedEx                overlapped;
    char                        buf[8*1024];
};

struct WatchedFile {
    WatchedFile *           next;
    WatchedDir *            watchedDir;
    const WCHAR *           fileName;
    FileChangeObserver *    observer;
};

static int              g_currentToken = 1;
static HANDLE           g_threadHandle = 0;
// used to wake-up file watcher thread to notify about
// added/removed files to be watched
static HANDLE           g_threadControlHandle = 0;

// protects data structures shared between ui thread and file
// watcher thread i.e. g_firstDir, g_firstFile
static CRITICAL_SECTION g_threadCritSec;

static WatchedDir *     g_firstDir = NULL;
static WatchedFile *    g_firstFile = NULL;

static void StartMonitoringDirForChanges(WatchedDir *wd);

#if 0 // not used yet
static void WakeUpWatcherThread()
{
    SetEvent(g_threadControlHandle);
}
#endif

static void NotifyAboutFile(WatchedDir *d, const WCHAR *fileName)
{
    lf(L"NotifyAboutFile() %s", fileName);

    WatchedFile *curr = g_firstFile;
    while (curr) {
        if (curr->watchedDir == d) {
            if (str::EqI(fileName, curr->fileName)) {
                // NOTE: It is not recommended to check whether the timestamp has changed
                // because the time granularity is so big that this can cause genuine
                // file notifications to be ignored. (This happens for instance for
                // PDF files produced by pdftex from small.tex document)
                curr->observer->OnFileChanged();
                return;
            }
        }
        curr = curr->next;
    }
}

static void DeleteWatchedDir(WatchedDir *wd)
{
    free((void*)wd->dirPath);
    free(wd);
}

static void CALLBACK ReadDirectoryChangesNotification(
        DWORD errCode,              // completion code
        DWORD bytesTransfered,      // number of bytes transferred
        LPOVERLAPPED overlapped)    // I/O information buffer
{
    WatchedDir* wd = (WatchedDir*)overlapped->hEvent;
    lf(L"ReadDirectoryChangesNotification() dir: %s", wd->dirPath);

    ScopedCritSec cs(&g_threadCritSec);

    if (errCode == ERROR_OPERATION_ABORTED)
    {
        lf("   ERROR_OPERATION_ABORTED");
        DeleteWatchedDir(wd);
        return;
    }

    lf("  numBytes: %d", (int)bytesTransfered);

    // This might mean overflow? Not sure.
    if (!bytesTransfered)
        return;

    FILE_NOTIFY_INFORMATION *notify = (FILE_NOTIFY_INFORMATION*)wd->buf;

    // a single notification can have multiple notifications for the same file, so we filter duplicates
    WStrVec seenFiles;
    for (;;) {
        WCHAR *fileName = str::DupN(notify->FileName, notify->FileNameLength / sizeof(WCHAR));
        if (notify->Action == FILE_ACTION_MODIFIED) {
            if (!seenFiles.Contains(fileName)) {
                seenFiles.Append(fileName);
                lf(L"NotifyAboutDirChanged() FILE_ACTION_MODIFIED, for '%s'", fileName);
            } else {
                lf(L"NotifyAboutDirChanged() eliminating duplicate notification for '%s'", fileName);
                free(fileName);
            }
        } else {
            lf(L"NotifyAboutDirChanged() action=%d, for '%s'", (int)notify->Action, fileName);
            free(fileName);
        }

        // step to the next entry if there is one
        DWORD nextOff = notify->NextEntryOffset;
        if (!nextOff)
            break;
        notify = (FILE_NOTIFY_INFORMATION *)((char*)notify + nextOff);
    }

    StartMonitoringDirForChanges(wd);
    for (WCHAR **f = seenFiles.IterStart(); f; f = seenFiles.IterNext()) {
        NotifyAboutFile(wd, *f);
    }
}

static void StartMonitoringDirForChanges(WatchedDir *wd)
{
    ZeroMemory(&wd->overlapped, sizeof(wd->overlapped));

    OVERLAPPED *overlapped = (OVERLAPPED*)&(wd->overlapped);
    wd->overlapped.data = (HANDLE)wd;

    ReadDirectoryChangesW(
         wd->hDir,
         wd->buf,                           // read results buffer
         sizeof(wd->buf),                   // length of buffer
         FALSE,                             // bWatchSubtree
         FILE_NOTIFY_CHANGE_LAST_WRITE,     // filter conditions
         NULL,                              // bytes returned
         overlapped,                        // overlapped buffer
         ReadDirectoryChangesNotification); // completion routine
}

static DWORD WINAPI FileWatcherThread(void *param)
{
    HANDLE handles[1];
    for (;;) {
        handles[0] = g_threadControlHandle;
        DWORD obj = WaitForMultipleObjectsEx(1, handles, FALSE, INFINITE, TRUE /* alertable */);
        if (obj == WAIT_IO_COMPLETION) {
            // APC complete. Nothing to do
            lf("FileWatcherThread(): APC complete");
        } else {
            int n = (int)(obj - WAIT_OBJECT_0);
            CrashIf(n < 0 || n >= 1);

            if (n == 0) {
                // a thread was explicitly awaken
                ResetEvent(g_threadControlHandle);
                lf("FileWatcherThread(): g_threadControlHandle signalled");
            } else {
                lf("FileWatcherThread(): n=%d", n);
                CrashIf(true);
            }
        }
    }
    return 0;
}

static void StartThreadIfNecessary()
{
    if (g_threadHandle)
        return;

    InitializeCriticalSection(&g_threadCritSec);
    g_threadControlHandle = CreateEvent(NULL, TRUE, FALSE, NULL);

    DWORD threadId;
    g_threadHandle = CreateThread(NULL, 0, FileWatcherThread, 0, 0, &threadId);
    SetThreadName(threadId, "FileWatcherThread");
}

static WatchedDir *FindExistingWatchedDir(const WCHAR *dirPath)
{
    WatchedDir *curr = g_firstDir;
    while (curr) {
        // TODO: normalize dirPath?
        if (str::EqI(dirPath, curr->dirPath))
            return curr;
        curr = curr->next;
    }
    return NULL;
}

static void StopMonitoringDir(WatchedDir *wd)
{
    lf("StopMonitoringDir() wd=0x%p", wd);

    // this will cause ReadDirectoryChangesNotification() to be called
    // with errCode = ERROR_OPERATION_ABORTED
    BOOL ok = CancelIo(wd->hDir);
    if (!ok)
        LogLastError();
    SafeCloseHandle(&wd->hDir);
}

static WatchedDir *NewWatchedDir(const WCHAR *dirPath)
{
    WatchedDir *wd = AllocStruct<WatchedDir>();
    wd->dirPath = str::Dup(dirPath);
    wd->hDir = CreateFile(
        dirPath, FILE_LIST_DIRECTORY,
        FILE_SHARE_READ|FILE_SHARE_DELETE|FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS  | FILE_FLAG_OVERLAPPED, NULL);
    if (!wd->hDir)
        goto Failed;

    wd->next = g_firstDir;
    g_firstDir = wd;

    return wd;
Failed:
    DeleteWatchedDir(wd);
    return NULL;
}

static WatchedFile *NewWatchedFile(const WCHAR *filePath, FileChangeObserver *observer)
{
    ScopedMem<WCHAR> dirPath(path::GetDir(filePath));
    WatchedDir *wd = FindExistingWatchedDir(dirPath);
    bool newDir = false;
    if (!wd) {
        wd = NewWatchedDir(dirPath);
        newDir = true;
    }

    WatchedFile *wf = AllocStruct<WatchedFile>();
    wf->fileName = str::Dup(path::GetBaseName(filePath));
    wf->watchedDir = wd;
    wf->observer = observer;
    wf->next = g_firstFile;
    g_firstFile = wf;

    if (newDir)
        StartMonitoringDirForChanges(wd);

    return wf;
}

static void DeleteWatchedFile(WatchedFile *wf)
{
    free((void*)wf->fileName);
    delete wf->observer;
    free(wf);
}

/* Subscribe for notifications about file changes. When a file changes, we'll
call observer->OnFileChanged().

We take ownership of observer object.

Returns a cancellation token that can be used in FileWatcherUnsubscribe(). That
way we can support multiple callers subscribing for the same file.
*/
FileWatcherToken FileWatcherSubscribe(const WCHAR *path, FileChangeObserver *observer)
{
    CrashIf(!observer);

    lf(L"FileWatcherSubscribe() path: %s", path);

    if (!file::Exists(path)) {
        delete observer;
        return NULL;
    }

    StartThreadIfNecessary();

    ScopedCritSec cs(&g_threadCritSec);

    // TODO: if the file is on a network drive we should periodically check
    // it ourselves, because ReadDirectoryChangesW()
    // doesn't work in that case
    WatchedFile *wf = NewWatchedFile(path, observer);
    return wf;
}

static bool IsWatchedDirReferenced(WatchedDir *wd)
{
    for (WatchedFile *wf = g_firstFile; wf; wf->next) {
        if (wf->watchedDir == wd)
            return true;
    }
    return false;
}

static void RemoveWatchedDirIfNotReferenced(WatchedDir *wd)
{
    if (IsWatchedDirReferenced(wd))
        return;
    WatchedDir **currPtr = &g_firstDir;
    WatchedDir *curr;
    for (;;) {
        curr = *currPtr;
        CrashAlwaysIf(!curr);
        if (curr == wd)
            break;
        currPtr = &(curr->next);
    }
    WatchedDir *toRemove = curr;
    *currPtr = toRemove->next;

    StopMonitoringDir(toRemove);
}

static void RemoveWatchedFile(WatchedFile *wf)
{
    WatchedDir *wd = wf->watchedDir;

    WatchedFile **currPtr = &g_firstFile;
    WatchedFile *curr;
    for (;;) {
        curr = *currPtr;
        CrashAlwaysIf(!curr);
        if (curr == wf)
            break;
        currPtr = &(curr->next);
    }
    WatchedFile *toRemove = curr;
    *currPtr = toRemove->next;
    DeleteWatchedFile(toRemove);

    RemoveWatchedDirIfNotReferenced(wd);
}

void FileWatcherUnsubscribe(FileWatcherToken token)
{
    if (!token)
        return;
    CrashIf(!g_threadHandle);

    ScopedCritSec cs(&g_threadCritSec);

    RemoveWatchedFile(token);
}
