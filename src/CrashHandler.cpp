#include "SumatraPDF.h"
#include "CrashHandler.h"

#include <dbghelp.h>
#include <process.h>
#include "base_util.h"
#include "tstr_util.h"
#include "WinUtil.hpp"

typedef BOOL WINAPI MiniDumpWriteProc(
    HANDLE hProcess,
    DWORD ProcessId,
    HANDLE hFile,
    LONG DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    PMINIDUMP_CALLBACK_INFORMATION CallbackParam
);

static TCHAR g_crashDumpPath[MAX_PATH];
static TCHAR g_exePath[MAX_PATH];

static MiniDumpWriteProc *g_MiniDumpWriteDump = NULL;

static bool InitDbgHelpDll()
{
#ifdef DEBUG
    static bool wasHere = false;
    assert(!wasHere);
    wasHere = true;
#endif

    WinLibrary lib(_T("DBGHELP.DLL"));
    g_MiniDumpWriteDump = (MiniDumpWriteProc *)lib.GetProcAddr("MiniDumpWriteDump");
    return (g_MiniDumpWriteDump != NULL);
}

static BOOL CALLBACK OpenMiniDumpCallback(void* /*param*/, PMINIDUMP_CALLBACK_INPUT input, PMINIDUMP_CALLBACK_OUTPUT output)
{
    if (!input || !output) 
        return FALSE; 

    switch (input->CallbackType) {
    case ModuleCallback:
        if (!(output->ModuleWriteFlags & ModuleReferencedByMemory))
            output->ModuleWriteFlags &= ~ModuleWriteModule; 
        return TRUE;
    case IncludeModuleCallback:
    case IncludeThreadCallback:
    case ThreadCallback:
    case ThreadExCallback:
        return TRUE;
    default:
        return FALSE;
    }
}

static unsigned WINAPI CrashDumpThread(LPVOID data)
{
    HANDLE dumpFile = CreateFile(g_crashDumpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (INVALID_HANDLE_VALUE == dumpFile)
        return 0;

    MINIDUMP_CALLBACK_INFORMATION mci = { OpenMiniDumpCallback, NULL }; 
    MINIDUMP_EXCEPTION_INFORMATION *mei = (MINIDUMP_EXCEPTION_INFORMATION *)data;

    MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);
    //type |= MiniDumpWithDataSegs|MiniDumpWithHandleData|MiniDumpWithPrivateReadWriteMemory;

    // TODO: this seems to fail consistently on Win7 - why?
    BOOL ok = g_MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile, type, mei, NULL, &mci);
    // BOOL ok = g_MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile, type, mei, NULL, NULL);

    CloseHandle(dumpFile);

    // exec_with_params(g_exePath, CMD_ARG_SEND_CRASHDUMP, TRUE /* hidden */);
    return 0;
}

static LONG WINAPI OpenDnsCrashHandler(EXCEPTION_POINTERS *exceptionInfo)
{
    static bool wasHere = false;
    if (wasHere)
        return EXCEPTION_CONTINUE_SEARCH;

    if (exceptionInfo && (EXCEPTION_BREAKPOINT == exceptionInfo->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;

    wasHere = true;

    // we either forgot to call InitDbgHelpDll() or it failed to obtain address of
    // MiniDumpWriteDump(), so nothing we can do
    if (!g_MiniDumpWriteDump)
        return EXCEPTION_CONTINUE_SEARCH;

    MINIDUMP_EXCEPTION_INFORMATION mei = { GetCurrentThreadId(), exceptionInfo, FALSE };

    // per msdn (which is backed by my experience), MiniDumpWriteDump() doesn't
    // write callstack for the calling thread correctly. We use msdn-recommended
    // work-around of spinning a thread to do the writing
    unsigned tid;
    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, CrashDumpThread, &mei, 0, &tid);
    if (INVALID_HANDLE_VALUE != hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallCrashHandler(const TCHAR *crashDumpPath)
{
    // do as much work as possible here (as opposed to in crash handler)
    // the downside is that startup time might suffer due to loading of dbghelp.dll
    if (!InitDbgHelpDll())
        return;

    GetFullPathName(crashDumpPath, dimof(g_crashDumpPath), g_crashDumpPath, NULL);
    if (!tstr_endswithi(g_crashDumpPath, _T(".dmp")))
        _tcscat_s(g_crashDumpPath, dimof(g_crashDumpPath), _T(".dmp"));
    GetModuleFileName(NULL, g_exePath, dimof(g_exePath));

    SetUnhandledExceptionFilter(OpenDnsCrashHandler);
}
