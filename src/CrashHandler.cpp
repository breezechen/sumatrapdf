/* Copyright 2006-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include "CrashHandler.h"

#include "Version.h"

#include "BaseUtil.h"
#include "StrUtil.h"
#include "Vec.h"
#include "WinUtil.h"
#include "FileUtil.h"

#include "translations.h"

typedef BOOL WINAPI MiniDumpWriteProc(
    HANDLE hProcess,
    DWORD ProcessId,
    HANDLE hFile,
    LONG DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

typedef BOOL _stdcall SymInitializeProc(
    HANDLE hProcess,
    PCSTR UserSearchPath,
    BOOL fInvadeProcess);

typedef DWORD _stdcall SymGetOptionsProc();
typedef DWORD _stdcall SymSetOptionsProc(DWORD SymOptions);

typedef BOOL _stdcall StackWalk64Proc(
    DWORD MachineType,
    HANDLE hProcess,
    HANDLE hThread,
    LPSTACKFRAME64 StackFrame,
    PVOID ContextRecord,
    PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
    PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
    PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
    PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);

typedef BOOL _stdcall SymFromAddrProc(
    HANDLE hProcess,
    DWORD64 Address,
    PDWORD64 Displacement,
    PSYMBOL_INFO Symbol
);

typedef PVOID _stdcall SymFunctionTableAccess64Proc(
    HANDLE hProcess,
    DWORD64 AddrBase);

typedef DWORD64 _stdcall SymGetModuleBase64Proc(
    HANDLE hProcess,
    DWORD64 qwAddr);

static MiniDumpWriteProc *              _MiniDumpWriteDump;
static SymInitializeProc *              _SymInitialize;
static SymGetOptionsProc *              _SymGetOptions;
static SymSetOptionsProc *              _SymSetOptions;
static StackWalk64Proc   *              _StackWalk64;
static SymFunctionTableAccess64Proc *   _SymFunctionTableAccess64;
static SymGetModuleBase64Proc *         _SymGetModuleBase64;
static SymFromAddrProc *                _SymFromAddr;

static ScopedMem<TCHAR> g_crashDumpPath(NULL);
static ScopedMem<TCHAR> g_crashTxtPath(NULL);
static HANDLE g_dumpEvent = NULL;
static HANDLE g_dumpThread = NULL;
static MINIDUMP_EXCEPTION_INFORMATION mei = { 0 };

static BOOL gSymInitializeOk = FALSE;

static void LoadDbgHelpFuncs()
{
    if (_MiniDumpWriteDump)
        return;
    WinLibrary lib(_T("DBGHELP.DLL"), true);
    _MiniDumpWriteDump = (MiniDumpWriteProc *)lib.GetProcAddr("MiniDumpWriteDump");
    _SymInitialize = (SymInitializeProc *)lib.GetProcAddr("SymInitialize");
    _SymGetOptions = (SymGetOptionsProc *)lib.GetProcAddr("SymGetOptions");
    _SymSetOptions = (SymSetOptionsProc *)lib.GetProcAddr("SymSetOptions");
    _StackWalk64 =   (StackWalk64Proc *)lib.GetProcAddr("StackWalk64");
    _SymFunctionTableAccess64 = (SymFunctionTableAccess64Proc *)lib.GetProcAddr("SymFunctionTableAccess64");
    _SymGetModuleBase64 = (SymGetModuleBase64Proc *)lib.GetProcAddr("SymGetModuleBase64");
    _SymFromAddr = (SymFromAddrProc *)lib.GetProcAddr("SymFromAddr");
}

static bool CanStackWalk()
{
    return gSymInitializeOk && _SymInitialize && _SymGetOptions 
        && _SymSetOptions&& _StackWalk64 && _SymFunctionTableAccess64 
        && _SymGetModuleBase64 && _SymFromAddr;
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

// returns false for 32bit processes running on 64bit os
static bool Is64Bit()
{
    typedef void (WINAPI * GetSystemInfoProc)(LPSYSTEM_INFO);
    WinLibrary lib(_T("kernel32.dll"));
    GetSystemInfoProc _GetNativeSystemInfo = (GetSystemInfoProc)lib.GetProcAddr("GetNativeSystemInfo");

    if (!_GetNativeSystemInfo)
        return false;
    SYSTEM_INFO sysInfo = {0};
    _GetNativeSystemInfo(&sysInfo);
    return sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
}

static char *OsNameFromVer(OSVERSIONINFOEX ver)
{
    if (VER_PLATFORM_WIN32_NT != ver.dwPlatformId)
        return "Unknown";
    // Windows 7 beta 1 reports the same major version as Vista does.
    if (ver.dwMajorVersion == 6 && ver.dwMinorVersion == 1) {
        return "7";
    } else if (ver.dwMajorVersion == 6 && ver.dwMinorVersion == 0) {
        return "Vista";
    } else if (ver.dwMajorVersion == 5 && ver.dwMinorVersion == 2) {
        return "Sever 2003";
    } else if (ver.dwMajorVersion == 5 && ver.dwMinorVersion == 1) {
        return "XP";
    } else if (ver.dwMajorVersion == 5 && ver.dwMinorVersion == 0) {
        return "2000";
    } else if (ver.dwMajorVersion <= 4) {
        return "9xOrNT";
    } else {
        return "Unknown";
    }
}


static bool IsWow64()
{
    typedef BOOL (WINAPI *IsWow64ProcessProc)(HANDLE, PBOOL);
    IsWow64ProcessProc _IsWow64Process;

    WinLibrary lib(_T("kernel32.dll"));
    _IsWow64Process = (IsWow64ProcessProc)lib.GetProcAddr("IsWow64Process");
    if (!_IsWow64Process)
        return false;
    BOOL isWow = FALSE;
    _IsWow64Process(GetCurrentProcess(), &isWow);
    return isWow;
}

static void GetOsVersion(Str::Str<char>& s)
{
    OSVERSIONINFOEX ver;
    ZeroMemory(&ver, sizeof(ver));
    ver.dwOSVersionInfoSize = sizeof(ver);
    BOOL ok = GetVersionEx((OSVERSIONINFO*)&ver);
    if (!ok)
        return;
    char *os = OsNameFromVer(ver);
    int servicePackMajor = ver.wServicePackMajor;
    int servicePackMinor = ver.wServicePackMinor;
    int buildNumber = ver.dwBuildNumber & 0xFFFF;
    char *is64bit = Is64Bit() ? "32bit" : "64bit";
    char *isWow = IsWow64() ?  "Wow64" : "";
    s.AppendFmt("OS: %s %d.%d build %d %s %s\n", os, servicePackMajor, servicePackMinor, buildNumber, is64bit, isWow);
}

static void GetModules(Str::Str<char>& s)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
      if (snap == INVALID_HANDLE_VALUE) return;

    MODULEENTRY32 mod;
    mod.dwSize = sizeof(mod);
    BOOL cont = Module32First(snap, &mod);
    while (cont) {
        TCHAR *name = mod.szModule;
        TCHAR *path = mod.szExePath;
        char *nameA = Str::Conv::ToUtf8(name);
        char *pathA = Str::Conv::ToUtf8(path);
        s.AppendFmt("Module: %s 0x%p 0x%x %s\n", nameA, (void*)mod.modBaseAddr, (int)mod.modBaseSize, pathA);
        free(nameA);
        free(pathA);
        cont = Module32Next(snap, &mod);
    }
    CloseHandle(snap);
}

static bool GetAddrInfo(void *addr, char *module, DWORD moduleLen, DWORD& sectionOut, DWORD& offsetOut)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
            return false;

    DWORD hMod = (DWORD)mbi.AllocationBase;
    if (!GetModuleFileNameA((HMODULE)hMod, module, moduleLen))
            return false;

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)(hMod + dosHeader->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(pNtHeader);
    
    DWORD lAddr = (DWORD)addr - hMod;
    for (unsigned int i = 0; i < pNtHeader->FileHeader.NumberOfSections; i++)
    {
            DWORD startAddr = section->VirtualAddress;
            DWORD endAddr = startAddr;
            if (section->SizeOfRawData > section->Misc.VirtualSize)
                    endAddr += section->SizeOfRawData;
            else
                    section->Misc.VirtualSize;
    
            if (lAddr >= startAddr && lAddr <= endAddr)
            {
                    sectionOut = i+1;
                    offsetOut = lAddr - startAddr;
                    return true;
            }
            section++;
    }
    return false;
}

#define MAX_NAME_LEN 512

static void GetCallstack(Str::Str<char>& s)
{
    if (!CanStackWalk())
        return;

    HANDLE hProc = GetCurrentProcess();
    HANDLE hThread = GetCurrentThread();
    DWORD threadId = GetCurrentThreadId();

    s.AppendFmt("\nThread: 0x%x\n", (int)threadId);

    CONTEXT ctx;
    RtlCaptureContext(&ctx);

    STACKFRAME64 stackFrame;
    memset(&stackFrame, 0, sizeof(stackFrame));
#ifdef  _WIN64
    stackFrame.AddrPC.Offset = ctx.Rip;
    stackFrame.AddrFrame.Offset = ctx.Rbp;
    stackFrame.AddrStack.Offset = ctx.Rsp;
#else
    stackFrame.AddrPC.Offset = ctx.Eip;
    stackFrame.AddrFrame.Offset = ctx.Ebp;
    stackFrame.AddrStack.Offset = ctx.Esp;
#endif
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Mode = AddrModeFlat;

    char buf[sizeof(SYMBOL_INFO) + MAX_NAME_LEN * sizeof(char)];
    SYMBOL_INFO *symInfo = (SYMBOL_INFO*)buf;

    int framesCount = 0;
    int maxFrames = 32;
    while (framesCount < maxFrames)
    {
        BOOL ok = _StackWalk64(IMAGE_FILE_MACHINE_I386, hProc, hThread,
            &stackFrame, &ctx, NULL, _SymFunctionTableAccess64,
            _SymGetModuleBase64, NULL);
        if (!ok)
            break;

        memset(buf, 0, sizeof(buf));
        symInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
        symInfo->MaxNameLen = MAX_NAME_LEN - 1;
        DWORD64 addr = stackFrame.AddrPC.Offset;

        DWORD64 symDisp = 0;
        ok = _SymFromAddr(hProc, addr, &symDisp, symInfo);
        if (ok) {
            char *name = &(symInfo->Name[0]);
            s.AppendFmt("0x%p %s+0x%x\n", (void*)addr, name, (int)symDisp);
        } else {
            char module[MAX_PATH] = { 0 };
            DWORD section, offset;
            // TODO: shouldn't we be able to use export table of the DLL to
            // at least know which function in the DLL it is even when we
            // don't have symbols?
            if (GetAddrInfo((void*)addr, module, sizeof(module), section, offset))
                s.AppendFmt("0x%p 0x%x:0x%x %s\n", (void*)addr, section, offset, module);
            else
                s.AppendFmt("0x%p\n", (void*)addr);
        }
        framesCount++;
    }
}

static char *BuildCrashInfoText()
{
    Str::Str<char> s;
    s.AppendFmt("Ver: %s\n", QM(CURR_VERSION));
    GetOsVersion(s);
    s.Append("\n");
    GetModules(s);
    s.Append("\n");
    // TODO: Suspend all threads and get their callstacks
    GetCallstack(s);
    // TODO: add info about the exception
    return s.StealData();
}

void SaveCrashInfoText()
{
    char *s = BuildCrashInfoText();
    if (s && g_crashTxtPath) {
        File::WriteAll(g_crashTxtPath, s, Str::Len(s));
    }
    free(s);
    // TODO: submit to a website
}

static DWORD WINAPI CrashDumpThread(LPVOID data)
{
    WaitForSingleObject(g_dumpEvent, INFINITE);
    LoadDbgHelpFuncs();

    if (!_MiniDumpWriteDump)
    {
#ifdef SVN_PRE_RELEASE_VER
        MessageBox(NULL, _T("Couldn't create a crashdump file: dbghelp.dll is unexpectedly missing."), _TR("SumatraPDF crashed"), MB_ICONEXCLAMATION | MB_OK);
#endif
        return 0;
    }

    HANDLE dumpFile = CreateFile(g_crashDumpPath.Get(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (INVALID_HANDLE_VALUE == dumpFile)
    {
#ifdef SVN_PRE_RELEASE_VER
        MessageBox(NULL, _T("Couldn't create a crashdump file."), _TR("SumatraPDF crashed"), MB_ICONEXCLAMATION | MB_OK);
#endif
        return 0;
    }

    MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);
    // set the SUMATRAPDF_FULLDUMP environment variable for far more complete minidumps
    if (GetEnvironmentVariable(_T("SUMATRAPDF_FULLDUMP"), NULL, 0))
        type = (MINIDUMP_TYPE)(type | MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithPrivateReadWriteMemory);
    MINIDUMP_CALLBACK_INFORMATION mci = { OpenMiniDumpCallback, NULL }; 

    _MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile, type, &mei, NULL, &mci);

    CloseHandle(dumpFile);

    SaveCrashInfoText();

    // exec_with_params(g_exePath, CMD_ARG_SEND_CRASHDUMP, TRUE /* hidden */);
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

    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = exceptionInfo;
    // per msdn (which is backed by my experience), MiniDumpWriteDump() doesn't
    // write callstack for the calling thread correctly. We use msdn-recommended
    // work-around of spinning a thread to do the writing
    SetEvent(g_dumpEvent);
    WaitForSingleObject(g_dumpThread, INFINITE);

    ScopedMem<TCHAR> msg(Str::Format(_T("%s\n\n%s"), _TR("Please include the following file in your crash report:"), g_crashDumpPath.Get()));
    MessageBox(NULL, msg.Get(), _TR("SumatraPDF crashed"), MB_ICONERROR | MB_OK);

    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallCrashHandler(const TCHAR *crashDumpPath, const TCHAR *crashTxtPath)
{
    LoadDbgHelpFuncs();

    if (_SymInitialize) {
       gSymInitializeOk = _SymInitialize(GetCurrentProcess(), NULL, FALSE);
       if (gSymInitializeOk) {
           DWORD symOptions =_SymGetOptions();
           symOptions |= SYMOPT_LOAD_LINES;
           symOptions |= SYMOPT_FAIL_CRITICAL_ERRORS; // don't show system msg box on errors
           symOptions |= SYMOPT_DEFERRED_LOADS;
           _SymSetOptions(symOptions);
           // TODO: should I setup sympath to Microsoft's symbol server via
           // SymSetSearchPath() so that at least I get names for system dlls?
       }
    }

    if (NULL == crashDumpPath)
        return;
    g_crashDumpPath.Set(Str::Dup(crashDumpPath));
    g_crashTxtPath.Set(Str::Dup(crashTxtPath));
    if (!g_dumpEvent && !g_dumpThread) {
        g_dumpEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_dumpThread = CreateThread(NULL, 0, CrashDumpThread, NULL, 0, 0);

        SetUnhandledExceptionFilter(DumpExceptionHandler);
    }
}
