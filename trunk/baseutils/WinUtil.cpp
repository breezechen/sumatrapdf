/* Written by Krzysztof Kowalczyk (http://blog.kowalczyk.info)
   The author disclaims copyright to this source code. 
   Take all the code you want, we'll just write more.
*/

#include "base_util.h"
#include "WinUtil.hpp"
#include "str_util.h"
#include <Shlwapi.h>

#define DONT_INHERIT_HANDLES FALSE

// Return true if application is themed. Wrapper around IsAppThemed() in uxtheme.dll
// that is compatible with earlier windows versions.
bool IsAppThemed(void) {
    WinLibrary lib(_T("uxtheme.dll"));
    FARPROC pIsAppThemed = lib.GetProcAddr("IsAppThemed");
    if (!pIsAppThemed) 
        return false;
    if (pIsAppThemed())
        return true;
    return false;
}

// Loads a DLL explicitly from the system's library collection
HMODULE WinLibrary::_LoadSystemLibrary(const TCHAR *libName) {
    TCHAR dllPath[MAX_PATH];
    GetSystemDirectory(dllPath, dimof(dllPath));
    PathAppend(dllPath, libName);
    return LoadLibrary(dllPath);
}

// Given name of the command to exececute 'cmd', and its arguments 'args'
// return WinProcess object that makes it easier to handle the process
// Returns NULL if failed to create the process. Caller can use GetLastError()
// for detailed error information.
// TODO: exe name might be unicode so to support everything cmd or args
// should be unicode or we can assume that cmd and args are utf8 and
// convert them to utf16 and call CreateProcessW
WinProcess * WinProcess::Create(const char* cmd, char* args)
{
    UINT                res;
    HANDLE              stdOut = INVALID_HANDLE_VALUE;
    HANDLE              stdErr = INVALID_HANDLE_VALUE;
    STARTUPINFOA        siStartupInfo;
    PROCESS_INFORMATION piProcessInfo;
    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = 0;
    sa.bInheritHandle = 1;

    memzero(&siStartupInfo, sizeof(siStartupInfo));
    memzero(&piProcessInfo, sizeof(piProcessInfo));
    siStartupInfo.cb = sizeof(siStartupInfo);

    char stdoutTempName[MAX_PATH] = {0};
    char stderrTempName[MAX_PATH] = {0};
    char *stdoutTempNameCopy = NULL;
    char *stderrTempNameCopy = NULL;

    char buf[MAX_PATH] = {0};
    DWORD len = GetTempPathA(sizeof(buf), buf);
    assert(len < sizeof(buf));
    // create temporary files for capturing stdout and stderr or the command
    res = GetTempFileNameA(buf, "stdout", 0, stdoutTempName);
    if (0 == res)
        goto Error;

    res = GetTempFileNameA(buf, "stderr", 0, stderrTempName);
    if (0 == res)
        goto Error;

    stdoutTempNameCopy = str_dup(stdoutTempName);
    stderrTempNameCopy = str_dup(stderrTempName);

    stdOut = CreateFileA(stdoutTempNameCopy,
        GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_WRITE|FILE_SHARE_READ,
        &sa, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (INVALID_HANDLE_VALUE == stdOut)
        goto Error;

    stdErr = CreateFileA(stderrTempNameCopy,
        GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_WRITE|FILE_SHARE_READ,
        &sa, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (INVALID_HANDLE_VALUE == stdErr)
        goto Error;

    siStartupInfo.hStdOutput = stdOut;
    siStartupInfo.hStdError = stdErr;

    BOOL ok = CreateProcessA(cmd, args, NULL, NULL, DONT_INHERIT_HANDLES,
        CREATE_DEFAULT_ERROR_MODE, NULL /*env*/, NULL /*curr dir*/,
        &siStartupInfo, &piProcessInfo);

    if (!ok)
        goto Error;

    // TODO: pass stdoutTempNameCopy and stderrTempNameCopy so upon
    // WinProcess destruction the files can be deleted and their memory freed
    WinProcess *wp = new WinProcess(&piProcessInfo);
    return wp;

Error:
    if (INVALID_HANDLE_VALUE != stdOut) {
        CloseHandle(stdOut);
    }

    if (INVALID_HANDLE_VALUE != stdErr) {
        CloseHandle(stdErr);
    }

    if (stdoutTempName[0]) {
        // TODO: delete stdoutTempName
    }
    if (stderrTempName[0]) {
        // TODO: delete stderrTempName
    }
    free(stdoutTempNameCopy);
    free(stderrTempNameCopy);
    return NULL;
}

WinProcess::WinProcess(PROCESS_INFORMATION *pi)
{
    memcpy(&m_processInfo, pi, sizeof(PROCESS_INFORMATION));
}

static int WindowsVerMajor()
{
    DWORD version = GetVersion();
    return (int)(version & 0xFF);
}

static int WindowsVerMinor()
{
    DWORD version = GetVersion();
    return (int)((version & 0xFF00) >> 8);    
}

bool WindowsVer2000OrGreater()
{
    if (WindowsVerMajor() >= 5)
        return true;
    return false;
}

bool WindowsVerVistaOrGreater()
{
    if (WindowsVerMajor() >= 6)
        return true;
    return false;
}

void SeeLastError(void) {
    char *msgBuf = NULL;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msgBuf, 0, NULL);
    if (!msgBuf) return;
    DBG_OUT("SeeLastError(): %s\n", msgBuf);
    LocalFree(msgBuf);
}

bool ReadRegStr(HKEY keySub, const TCHAR *keyName, const TCHAR *valName, const TCHAR *buffer, DWORD bufLen)
{
    HKEY keyTmp = NULL;
    LONG res = RegOpenKeyEx(keySub, keyName, 0, KEY_READ, &keyTmp);

    if (ERROR_SUCCESS == res) {
        bufLen *= sizeof(TCHAR); // we need the buffer size in bytes not TCHARs
        res = RegQueryValueEx(keyTmp, valName, NULL, NULL, (BYTE *)buffer, &bufLen);
        RegCloseKey(keyTmp);
    }

    if (ERROR_SUCCESS != res)
        SeeLastError();
    return ERROR_SUCCESS == res;
}

bool WriteRegStr(HKEY keySub, const TCHAR *keyName, const TCHAR *valName, const TCHAR *value)
{
    HKEY keyTmp = NULL;
    LONG res = RegCreateKeyEx(keySub, keyName, 0, NULL, 0, KEY_WRITE, NULL, &keyTmp, NULL);

    if (ERROR_SUCCESS == res) {
        res = RegSetValueEx(keyTmp, valName, 0, REG_SZ, (const BYTE*)value, (lstrlen(value)+1) * sizeof(TCHAR));
        RegCloseKey(keyTmp);
    }

    if (ERROR_SUCCESS != res)
        SeeLastError();
    return ERROR_SUCCESS == res;
}

