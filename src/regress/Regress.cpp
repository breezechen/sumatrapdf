/* Copyright 2012 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

/*
A regression test suite. The idea is to add tests for bugs we fix that
are too time consuming to be part of unit tests. The tests can rely
on presence of shared test files.

Note: because it can be run as both release and debug, we can't use
assert() or CrashIf() but CrashAlwaysIf().

To write new regression test:
- add a file src/regress/Regress${NN}.cpp with Regress${NN} function
- #include "Regress${NN}.cpp" right before RunTest() function
- call Regress${NN} function from RunTests()
*/

#include "BaseUtil.h"
#include "DbgHelpDyn.h"
#include "DirIter.h"
#include "Doc.h"
#include "FileUtil.h"
using namespace Gdiplus;
#include "GdiPlusUtil.h"
#include "HtmlFormatter.h"
#include "Mui.h"
#include "WinUtil.h"

#include "DebugLog.h"

static TCHAR *gTestFilesDir;

static TCHAR *TestFilesDir()
{
    return gTestFilesDir;
}

static int Usage()
{
    printf("regress.exe\n");
    printf("Error: didn't find test files on this computer!\n");
    return 1;
}

static void printflush(const char *s)
{
    printf(s);
    fflush(stdout);
}

/* Auto-detect the location of test files. Ultimately we might add a cmd-line
option to specify this directory, for now just add your location(s) to the list */
static bool FindTestFilesDir()
{
    TCHAR *dirsToCheck[] = {
        _T("C:\\Documents and Settings\\kkowalczyk\\My Documents\\Google Drive\\Sumatra")
    };
    for (size_t i = 0; i < dimof(dirsToCheck); i++) {
        TCHAR *dir = dirsToCheck[i];
        if (dir::Exists(dir)) {
            gTestFilesDir = dir;
            return true;
        }
    }
    return false;
}

static void VerifyFileExists(const TCHAR *filePath)
{
    if (!file::Exists(filePath)) {
        _tprintf(_T("File '%s' doesn't exist!\n"), filePath);
        exit(1);
    }
}

static HANDLE   gDumpEvent = NULL;
static HANDLE   gDumpThread = NULL;

static MINIDUMP_EXCEPTION_INFORMATION gMei = { 0 };
static LPTOP_LEVEL_EXCEPTION_FILTER gPrevExceptionFilter = NULL;

static DWORD WINAPI CrashDumpThread(LPVOID data)
{
    WaitForSingleObject(gDumpEvent, INFINITE);
    printflush("Captain, we've got a crash!\n");
    if (!dbghelp::Load())
        return 0;

    // TODO: print a callstack
    return 0;
}

static LONG WINAPI DumpExceptionHandler(EXCEPTION_POINTERS *exceptionInfo)
{
    if (!exceptionInfo || (EXCEPTION_BREAKPOINT == exceptionInfo->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;

    static bool wasHere = false;
    if (wasHere)
        return EXCEPTION_CONTINUE_SEARCH;
    wasHere = true;

    gMei.ThreadId = GetCurrentThreadId();
    gMei.ExceptionPointers = exceptionInfo;
    // per msdn (which is backed by my experience), MiniDumpWriteDump() doesn't
    // write callstack for the calling thread correctly. We use msdn-recommended
    // work-around of spinning a thread to do the writing
    SetEvent(gDumpEvent);
    WaitForSingleObject(gDumpThread, INFINITE);

    TerminateProcess(GetCurrentProcess(), 1);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void InstallCrashHandler()
{
    gDumpEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!gDumpEvent) {
        printflush("InstallCrashHandler(): CreateEvent() failed\n");
        return;
    }
    gDumpThread = CreateThread(NULL, 0, CrashDumpThread, NULL, 0, 0);
    if (!gDumpThread) {
        printflush("InstallCrashHandler(): CreateThread() failed\n");
        return;
    }
    gPrevExceptionFilter = SetUnhandledExceptionFilter(DumpExceptionHandler);
}

static void UninstallCrashHandler()
{
    if (gDumpEvent)
        SetUnhandledExceptionFilter(gPrevExceptionFilter);
    TerminateThread(gDumpThread, 1);
    CloseHandle(gDumpThread);
    CloseHandle(gDumpEvent);
}


#include "Regress00.cpp"

static void RunTests()
{
    Regress00();
}

extern "C"
int main(int argc, char **argv)
{
    if (!FindTestFilesDir())
        return Usage();

    InstallCrashHandler();
    InitAllCommonControls();
    ScopedGdiPlus gdi;
    mui::Initialize();

    RunTests();
    printflush("All tests completed successfully!\n");
    mui::Destroy();
    UninstallCrashHandler();
    return 0;
}
