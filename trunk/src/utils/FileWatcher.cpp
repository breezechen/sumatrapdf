/* Copyright 2014 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "BaseUtil.h"
#include "FileWatcher.h"

#include "FileUtil.h"
#include "ThreadUtil.h"
#include "WinUtil.h"

#define NOLOG 1
#include "DebugLog.h"

/*
This code is tricky, so here's a high-level overview. More info at:
http://qualapps.blogspot.com/2010/05/understanding-readdirectorychangesw.html

We use ReadDirectoryChangesW() with overlapped i/o and i/o completion
callback function.

Callback function is called in the context of the thread that called
ReadDirectoryChangesW() but only if it's in alertable state.
Our ui thread isn't so we create our own thread and run code that
calls ReadDirectoryChangesW() on that thread via QueueUserAPC().

g_watchedDirs and g_watchedFiles are shared between the main thread and
worker thread so must be protected via g_threadCritSec.

ReadDirectChangesW() doesn't always work for files on network drives,
so for those files, we do manual checks, by using a timeout to
periodically wake up thread.
*/

/*
TODO:
  - should I end the thread when there are no files to watch?

  - a single file copy can generate multiple notifications for the same
    file. add some delay mechanism so that subsequent change notifications
    cancel a previous, delayed one ? E.g. a copy f2.pdf f.pdf generates 3
    notifications if f2.pdf is 2 MB.

  - try to handle short file names as well: http://blogs.msdn.com/b/ericgu/archive/2005/10/07/478396.aspx
    but how to test it?

  - I could try to remove the need for g_threadCritSec by queing all code
    that touches g_watchedDirs/g_watchedFiles onto a thread via APC, but that's
    probably an overkill
*/

// there's a balance between responsiveness to changes and efficiency
#define FILEWATCH_DELAY_IN_MS       1000

// Some people use overlapped.hEvent to store data but I'm playing it safe.
struct OverlappedEx {
    OVERLAPPED      overlapped;
    void *          data;
};

// info needed to detect that a file has changed
struct FileState {
    FILETIME    time;
    int64       size;
};

struct WatchedDir {
    WatchedDir *    next;
    const WCHAR *   dirPath;
    HANDLE          hDir;
    OverlappedEx    overlapped;
    char            buf[8*1024];
};

struct WatchedFile {
    WatchedFile *           next;
    WatchedDir *            watchedDir;
    const WCHAR *           filePath;
    FileChangeObserver *    observer;

    // if true, the file is on a network drive and we have
    // to check if it changed manually, by periodically checking
    // file state for changes
    bool                    isManualCheck;
    FileState               fileState;
};

static HANDLE           g_threadHandle = 0;
static DWORD            g_threadId = 0;

static HANDLE           g_threadControlHandle = 0;

// protects data structures shared between ui thread and file
// watcher thread i.e. g_watchedDirs, g_watchedFiles
static CRITICAL_SECTION g_threadCritSec;

static WatchedDir *     g_watchedDirs = NULL;
static WatchedFile *    g_watchedFiles = NULL;

static void StartMonitoringDirForChanges(WatchedDir *wd);

static void AwakeWatcherThread()
{
    SetEvent(g_threadControlHandle);
}

static void GetFileState(const WCHAR *filePath, FileState* fs)
{
    // Note: in my testing on network drive that is mac volume mounted
    // via parallels, lastWriteTime is not updated. lastAccessTime is,
    // but it's also updated when the file is being read from (e.g.
    // copy f.pdf f2.pdf will change lastAccessTime of f.pdf)
    // So I'm sticking with lastWriteTime
    fs->time = file::GetModificationTime(filePath);
    fs->size = file::GetSize(filePath);
}

static bool FileStateEq(FileState *fs1, FileState *fs2)
{
    if (0 != CompareFileTime(&fs1->time, &fs2->time))
        return false;
    if (fs1->size != fs2->size)
        return false;
    return true;
}

static bool FileStateChanged(const WCHAR *filePath, FileState *fs)
{
    FileState fsTmp;

    GetFileState(filePath, &fsTmp);
    if (FileStateEq(fs, &fsTmp))
        return false;

    memcpy(fs, &fsTmp, sizeof(*fs));
    return true;
}

// TODO: per internet, fileName could be short, 8.3 dos-style name
// and we don't handle that. On the other hand, I've only seen references
// to it wrt. to rename/delete operation, which we don't get notified about
//
// TODO: to collapse multiple notifications for the same file, could put it on a
// queue, restart the thread with a timeout, restart the process if we
// get notified again before timeout expires, call OnFileChanges() when
// timeout expires
static void NotifyAboutFile(WatchedDir *d, const WCHAR *fileName)
{
    lf(L"NotifyAboutFile(): %s", fileName);

    for (WatchedFile *wf = g_watchedFiles; wf; wf = wf->next) {
        if (wf->watchedDir != d)
            continue;
        const WCHAR *wfFileName = path::GetBaseName(wf->filePath);

        if (!str::EqI(fileName, wfFileName))
            continue;

        // NOTE: It is not recommended to check whether the timestamp has changed
        // because the time granularity is so big that this can cause genuine
        // file notifications to be ignored. (This happens for instance for
        // PDF files produced by pdftex from small.tex document)
        wf->observer->OnFileChanged();
        return;
    }
}

static void DeleteWatchedDir(WatchedDir *wd)
{
    free((void*)wd->dirPath);
    free(wd);
}

static void CALLBACK ReadDirectoryChangesNotification(DWORD errCode,
    DWORD bytesTransfered, LPOVERLAPPED overlapped)
{
    ScopedCritSec cs(&g_threadCritSec);

    OverlappedEx *over = (OverlappedEx*)overlapped;
    WatchedDir* wd = (WatchedDir*)over->data;

    lf(L"ReadDirectoryChangesNotification() dir: %s, numBytes: %d", wd->dirPath, (int)bytesTransfered);

    CrashIf(wd != wd->overlapped.data);

    if (errCode == ERROR_OPERATION_ABORTED) {
        lf("   ERROR_OPERATION_ABORTED");
        DeleteWatchedDir(wd);
        return;
    }

    // This might mean overflow? Not sure.
    if (!bytesTransfered)
        return;

    FILE_NOTIFY_INFORMATION *notify = (FILE_NOTIFY_INFORMATION*)wd->buf;

    // collect files that changed, removing duplicates
    WStrVec changedFiles;
    for (;;) {
        WCHAR *fileName = str::DupN(notify->FileName, notify->FileNameLength / sizeof(WCHAR));
        if (notify->Action == FILE_ACTION_MODIFIED) {
            if (!changedFiles.Contains(fileName)) {
                lf(L"ReadDirectoryChangesNotification() FILE_ACTION_MODIFIED, for '%s'", fileName);
                changedFiles.Append(fileName);
                fileName = NULL;
            } else {
                lf(L"ReadDirectoryChangesNotification() eliminating duplicate notification for '%s'", fileName);
            }
        } else {
            lf(L"ReadDirectoryChangesNotification() action=%d, for '%s'", (int)notify->Action, fileName);
        }
        free(fileName);

        // step to the next entry if there is one
        DWORD nextOff = notify->NextEntryOffset;
        if (!nextOff)
            break;
        notify = (FILE_NOTIFY_INFORMATION *)((char*)notify + nextOff);
    }

    StartMonitoringDirForChanges(wd);

    for (WCHAR **f = changedFiles.IterStart(); f; f = changedFiles.IterNext()) {
        NotifyAboutFile(wd, *f);
    }
}

static void CALLBACK StartMonitoringDirForChangesAPC(ULONG_PTR arg)
{
    WatchedDir *wd = (WatchedDir*)arg;
    ZeroMemory(&wd->overlapped, sizeof(wd->overlapped));

    OVERLAPPED *overlapped = (OVERLAPPED*)&(wd->overlapped);
    wd->overlapped.data = (HANDLE)wd;

    lf(L"StartMonitoringDirForChangesAPC() %s", wd->dirPath);

    CrashIf(g_threadId != GetCurrentThreadId());

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

static void StartMonitoringDirForChanges(WatchedDir *wd)
{
    QueueUserAPC(StartMonitoringDirForChangesAPC, g_threadHandle, (ULONG_PTR)wd);
}

static DWORD GetTimeoutInMs()
{
    ScopedCritSec cs(&g_threadCritSec);
    for (WatchedFile *wf = g_watchedFiles; wf; wf = wf->next) {
        if (wf->isManualCheck)
            return FILEWATCH_DELAY_IN_MS;
    }
    return INFINITE;
}

static void RunManualCheck()
{
    ScopedCritSec cs(&g_threadCritSec);

    for (WatchedFile *wf = g_watchedFiles; wf; wf = wf->next) {
        if (!wf->isManualCheck)
            continue;
        if (FileStateChanged(wf->filePath, &wf->fileState)) {
            lf(L"RunManualCheck() %s changed", wf->filePath);
            wf->observer->OnFileChanged();
        }
    }
}

static DWORD WINAPI FileWatcherThread(void *param)
{
    HANDLE handles[1];
    // must be alertable to receive ReadDirectoryChangesW() callbacks and APCs
    BOOL alertable = TRUE;

    for (;;) {
        handles[0] = g_threadControlHandle;
        DWORD timeout = GetTimeoutInMs();
        DWORD obj = WaitForMultipleObjectsEx(1, handles, FALSE, timeout, alertable);
        if (WAIT_TIMEOUT == obj) {
            RunManualCheck();
            continue;
        }

        if (WAIT_IO_COMPLETION == obj) {
            // APC complete. Nothing to do
            lf("FileWatcherThread(): APC complete");
            continue;
        }

        int n = (int)(obj - WAIT_OBJECT_0);

        if (n == 0) {
            // a thread was explicitly awaken
            ResetEvent(g_threadControlHandle);
            lf("FileWatcherThread(): g_threadControlHandle signalled");
        } else {
            dbglog::CrashLogF("FileWatcherThread(): n=%d", n);
            CrashIf(true);
        }
    }
}

static void StartThreadIfNecessary()
{
    if (g_threadHandle)
        return;

    InitializeCriticalSection(&g_threadCritSec);
    g_threadControlHandle = CreateEvent(NULL, TRUE, FALSE, NULL);

    g_threadHandle = CreateThread(NULL, 0, FileWatcherThread, 0, 0, &g_threadId);
    SetThreadName(g_threadId, "FileWatcherThread");
}

static WatchedDir *FindExistingWatchedDir(const WCHAR *dirPath)
{
    for (WatchedDir *wd = g_watchedDirs; wd; wd = wd->next) {
        // TODO: normalize dirPath?
        if (str::EqI(dirPath, wd->dirPath))
            return wd;
    }
    return NULL;
}

static void CALLBACK StopMonitoringDirAPC(ULONG_PTR arg)
{
    WatchedDir *wd = (WatchedDir*)arg;
    lf("StopMonitoringDirAPC() wd=0x%p", wd);

    // this will cause ReadDirectoryChangesNotification() to be called
    // with errCode = ERROR_OPERATION_ABORTED
    BOOL ok = CancelIo(wd->hDir);
    if (!ok)
        LogLastError();
    SafeCloseHandle(&wd->hDir);
}

static WatchedDir *NewWatchedDir(const WCHAR *dirPath)
{
    HANDLE hDir = CreateFile(
        dirPath, FILE_LIST_DIRECTORY,
        FILE_SHARE_READ|FILE_SHARE_DELETE|FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS  | FILE_FLAG_OVERLAPPED, NULL);
    if (INVALID_HANDLE_VALUE == hDir)
        return NULL;

    WatchedDir *wd = AllocStruct<WatchedDir>();
    wd->hDir = hDir;
    wd->dirPath = str::Dup(dirPath);

    ListInsert(&g_watchedDirs, wd);
    return wd;
}

static WatchedFile *NewWatchedFile(const WCHAR *filePath, FileChangeObserver *observer)
{
    bool isManualCheck = PathIsNetworkPath(filePath);
    ScopedMem<WCHAR> dirPath(path::GetDir(filePath));
    WatchedDir *wd = NULL;
    bool newDir = false;
    if (!isManualCheck) {
        wd = FindExistingWatchedDir(dirPath);
        if (!wd) {
            wd = NewWatchedDir(dirPath);
            if (!wd)
                return NULL;
            newDir = true;
        }
    }

    WatchedFile *wf = AllocStruct<WatchedFile>();
    wf->filePath = str::Dup(filePath);
    wf->observer = observer;
    wf->watchedDir = wd;
    wf->isManualCheck = isManualCheck;

    ListInsert(&g_watchedFiles, wf);

    if (wf->isManualCheck) {
        GetFileState(filePath, &wf->fileState);
        AwakeWatcherThread();
    } else {
        if (newDir)
            StartMonitoringDirForChanges(wf->watchedDir);
    }

    return wf;
}

static void DeleteWatchedFile(WatchedFile *wf)
{
    free((void*)wf->filePath);
    delete wf->observer;
    free(wf);
}

/* Subscribe for notifications about file changes. When a file changes, we'll
call observer->OnFileChanged().

We take ownership of observer object.

Returns a cancellation token that can be used in FileWatcherUnsubscribe(). That
way we can support multiple callers subscribing to the same file.
*/
WatchedFile *FileWatcherSubscribe(const WCHAR *path, FileChangeObserver *observer)
{
    CrashIf(!observer);

    lf(L"FileWatcherSubscribe() path: %s", path);

    if (!file::Exists(path)) {
        delete observer;
        return NULL;
    }

    StartThreadIfNecessary();

    ScopedCritSec cs(&g_threadCritSec);
    return NewWatchedFile(path, observer);
}

static bool IsWatchedDirReferenced(WatchedDir *wd)
{
    for (WatchedFile *wf = g_watchedFiles; wf; wf = wf->next) {
        if (wf->watchedDir == wd)
            return true;
    }
    return false;
}

static void RemoveWatchedDirIfNotReferenced(WatchedDir *wd)
{
    if (IsWatchedDirReferenced(wd))
        return;

    bool ok = ListRemove(&g_watchedDirs, wd);
    CrashIf(!ok);
    // memory will be eventually freed in ReadDirectoryChangesNotification()
    QueueUserAPC(StopMonitoringDirAPC, g_threadHandle, (ULONG_PTR)wd);
}

static void RemoveWatchedFile(WatchedFile *wf)
{
    WatchedDir *wd = wf->watchedDir;
    bool ok = ListRemove(&g_watchedFiles, wf);
    CrashIf(!ok);

    bool needsAwakeThread = wf->isManualCheck;
    DeleteWatchedFile(wf);
    if (needsAwakeThread)
        AwakeWatcherThread();
    else
        RemoveWatchedDirIfNotReferenced(wd);
}

void FileWatcherUnsubscribe(WatchedFile *wf)
{
    if (!wf)
        return;
    CrashIf(!g_threadHandle);

    ScopedCritSec cs(&g_threadCritSec);

    RemoveWatchedFile(wf);
}
