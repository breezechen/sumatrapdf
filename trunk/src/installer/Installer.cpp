/* Copyright 2010-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

/*
The installer is good enough for production but it doesn't mean it couldn't be improved:
 * allow to change the installation directory

 * some more fanciful animations e.g.:
 * letters could drop down and back up when cursor is over it
 * messages could scroll-in
 * some background thing could be going on, e.g. a spinning 3d cube
 * show fireworks on successful installation/uninstallation
*/

#include <windows.h>
#include <windowsx.h>
#include <GdiPlus.h>
#include <tchar.h>
#include <shlobj.h>
#include <Tlhelp32.h>
#include <Shlwapi.h>
#include <objidl.h>

#include <ioapi.h>
#include <iowin32.h>
#include <unzip.h>

#include "Resource.h"
#include "base_util.h"
#include "tstr_util.h"
#include "file_util.h"
#include "win_util.h"
#include "WinUtil.hpp"
#include "Version.h"

using namespace Gdiplus;

// define for testing the uninstaller
// #define TEST_UNINSTALLER

// define for a shadow effect
#define DRAW_TEXT_SHADOW

#define INSTALLER_FRAME_CLASS_NAME    _T("SUMATRA_PDF_INSTALLER_FRAME")

#define INSTALLER_WIN_DX    420
#define INSTALLER_WIN_DY    320
#define UNINSTALLER_WIN_DX  420
#define UNINSTALLER_WIN_DY  320

#define ID_BUTTON_INSTALL             11
#define ID_BUTTON_UNINSTALL           12
#define ID_CHECKBOX_MAKE_DEFAULT      13
#define ID_CHECKBOX_BROWSER_PLUGIN    14
#define ID_BUTTON_START_SUMATRA       15
#define ID_BUTTON_EXIT                16

#define WM_APP_INSTALLATION_FINISHED        (WM_APP + 1)

// The window is divided in two parts:
// * top part, where we display nice graphics
// * bottom part, with install/uninstall button
// This is the height of the lower part
#define BOTTOM_PART_DY 46

static HINSTANCE        ghinst;
static HWND             gHwndFrame = NULL;
static HWND             gHwndButtonInstall = NULL;
static HWND             gHwndButtonExit = NULL;
static HWND             gHwndButtonRunSumatra = NULL;
static HWND             gHwndCheckboxRegisterDefault = NULL;
static HWND             gHwndCheckboxRegisterBrowserPlugin = NULL;
static HWND             gHwndButtonUninstall = NULL;
static HFONT            gFontDefault;

static TCHAR *          gMsg;
static TCHAR *          gMsgError = NULL;
static Color            gMsgColor;

Color gCol1(196, 64, 50); Color gCol1Shadow(134, 48, 39);
Color gCol2(227, 107, 35); Color gCol2Shadow(155, 77, 31);
Color gCol3(93,  160, 40); Color gCol3Shadow(51, 87, 39);
Color gCol4(69, 132, 190); Color gCol4Shadow(47, 89, 127);
Color gCol5(112, 115, 207); Color gCol5Shadow(66, 71, 118);

static Color            COLOR_MSG_WELCOME(gCol5);
static Color            COLOR_MSG_OK(gCol5);
static Color            COLOR_MSG_INSTALLATION(gCol5);
static Color            COLOR_MSG_FAILED(gCol1);

#define TAPP                _T("SumatraPDF")
#define EXENAME             TAPP _T(".exe")

// This is in HKLM. Note that on 64bit windows, if installing 32bit app
// the installer has to be 32bit as well, so that it goes into proper
// place in registry (under Software\Wow6432Node\Microsoft\Windows\...
#define REG_PATH_UNINST     _T("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") TAPP
#define REG_PATH_SOFTWARE   _T("Software\\") TAPP

#define REG_CLASSES_APP     _T("Software\\Classes\\") TAPP
#define REG_CLASSES_PDF     _T("Software\\Classes\\.pdf")

#define REG_EXPLORER_PDF_EXT  _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.pdf")
#define PROG_ID               _T("ProgId")

// Keys we'll set in REG_PATH_UNINST path

// REG_SZ, a path to installed executable (or "$path,0" to force the first icon)
#define DISPLAY_ICON _T("DisplayIcon")
// REG_SZ, e.g "SumatraPDF"
#define DISPLAY_NAME _T("DisplayName")
// REG_SZ, e.g. "1.2"
#define DISPLAY_VERSION _T("DisplayVersion")
// REG_DWORD, get size of installed directory after copying files
#define ESTIMATED_SIZE _T("EstimatedSize")
// REG_DWORD, set to 1
#define NO_MODIFY _T("NoModify")
// REG_DWORD, set to 1
#define NO_REPAIR _T("NoRepair")
// REG_SZ, e.g. "Krzysztof Kowalczyk"
#define PUBLISHER _T("Publisher")
// REG_SZ, path to uninstaller exe
#define UNINSTALL_STRING _T("UninstallString")
// REG_SZ, e.g. "http://blog.kowalczyk/info/software/sumatrapdf/"
#define URL_INFO_ABOUT _T("UrlInfoAbout")

// Installation directory (set in HKLM REG_PATH_SOFTWARE
// for compatibility with the old NSIS installer)
#define INSTALL_DIR _T("Install_Dir")

// Note: make sure that this mark is never contained as a single-byte
//       sequence in the executable, as we cut at the first occurrence
//       for the uninstaller
#define UN_INST_MARK L"!uninst_end!"
static char *gUnInstMark = NULL; // wstr_to_utf8(UN_INST_MARK)

struct {
    bool uninstall;
    bool silent;
    TCHAR *installDir;
    bool registerAsDefault;
    bool installBrowserPlugin;

    TCHAR *firstError;
    HANDLE hThread;
    bool success;
} gGlobalData = {
    false, /* bool uninstall */
    false,  /* bool silent */
    NULL,   /* TCHAR *installDir */
    false,  /* bool registerAsDefault */
    false,  /* bool installBrowserPlugin */

    NULL,   /* TCHAR *firstError */
    NULL,   /* HANDLE hThread */
    false,  /* bool success */
};

void NotifyFailed(TCHAR *msg)
{
    if (!gGlobalData.firstError)
        gGlobalData.firstError = tstr_dup(msg);
    // MessageBox(gHwndFrame, msg, _T("Installation failed"),  MB_ICONEXCLAMATION | MB_OK);
}

#define TEN_SECONDS_IN_MS 10*1000

// Kill a process with given <processId> if it's loaded from <processPath>.
// If <waitUntilTerminated> is TRUE, will wait until process is fully killed.
// Returns TRUE if killed a process
BOOL KillProcIdWithName(DWORD processId, TCHAR *processPath, BOOL waitUntilTerminated)
{
    BOOL killed = FALSE;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE, FALSE, processId);
    HANDLE hModSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
    if (!hProcess || INVALID_HANDLE_VALUE == hModSnapshot)
        goto Exit;

    MODULEENTRY32 me32;
    me32.dwSize = sizeof(me32);
    if (!Module32First(hModSnapshot, &me32))
        goto Exit;
    if (!FilePath_IsSameFile(processPath, me32.szExePath))
        goto Exit;

    killed = TerminateProcess(hProcess, 0);
    if (!killed)
        goto Exit;

    if (waitUntilTerminated)
        WaitForSingleObject(hProcess, TEN_SECONDS_IN_MS);

    UpdateWindow(FindWindow(NULL, _T("Shell_TrayWnd")));
    UpdateWindow(GetDesktopWindow());

Exit:
    CloseHandle(hModSnapshot);
    CloseHandle(hProcess);
    return killed;
}

#define MAX_PROCESSES 1024

static int KillProcess(TCHAR *processPath, BOOL waitUntilTerminated)
{
    int killCount = 0;

    HANDLE hProcSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (INVALID_HANDLE_VALUE == hProcSnapshot)
        goto Error;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(pe32);
    if (!Process32First(hProcSnapshot, &pe32))
        goto Error;

    do {
        if (KillProcIdWithName(pe32.th32ProcessID, processPath, waitUntilTerminated))
            killCount++;
    } while (Process32Next(hProcSnapshot, &pe32));

Error:
    CloseHandle(hProcSnapshot);
    return killCount;
}

TCHAR *GetExePath()
{
    static TCHAR exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, dimof(exePath));
    return exePath;
}

TCHAR *GetInstallationDir(bool forUninstallation=false)
{
    TCHAR dir[MAX_PATH];

    // try the previous installation directory first
    bool ok = ReadRegStr(HKEY_LOCAL_MACHINE, REG_PATH_SOFTWARE, INSTALL_DIR, dir, dimof(dir));
    if (ok) {
        if (tstr_endswithi(dir, _T(".exe")))
            *(TCHAR *)FilePath_GetBaseName(dir) = '\0';
        if (*dir && dir_exists(dir))
            return tstr_dup(dir);
    }

    if (forUninstallation) {
        // fall back to the uninstaller's path
        return FilePath_GetDir(GetExePath());
    }

    // fall back to %ProgramFiles%
    ok = !!SHGetSpecialFolderPath(NULL, dir, CSIDL_PROGRAM_FILES, FALSE);
    if (!ok)
        return NULL;
    return tstr_cat(dir, _T("\\") TAPP);
}

TCHAR *GetUninstallerPath()
{
    return tstr_cat(gGlobalData.installDir, _T("\\") _T("uninstall.exe"));
}

TCHAR *GetInstalledExePath()
{
    return tstr_cat(gGlobalData.installDir, _T("\\") EXENAME);
}

TCHAR *GetBrowserPluginPath()
{
    return tstr_cat(gGlobalData.installDir, _T("\\") _T("npPdfViewer.dll"));
}

TCHAR *GetStartMenuProgramsPath(bool allUsers)
{
    static TCHAR dir[MAX_PATH];
    // CSIDL_COMMON_PROGRAMS => installing for all users
    BOOL ok = SHGetSpecialFolderPath(NULL, dir, allUsers ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS, FALSE);
    if (!ok)
        return NULL;
    return dir;
}

TCHAR *GetShortcutPath(bool allUsers)
{
    return tstr_cat(GetStartMenuProgramsPath(allUsers), _T("\\") TAPP _T(".lnk"));
}

BOOL IsValidInstaller(void)
{
    zlib_filefunc64_def ffunc;
#ifdef UNICODE
    fill_win32_filefunc64W(&ffunc);
#else
    fill_win32_filefunc64A(&ffunc);
#endif
    unzFile uf = unzOpen2_64(GetExePath(), &ffunc);
    if (!uf)
        return FALSE;

    unz_global_info64 ginfo;
    int err = unzGetGlobalInfo64(uf, &ginfo);
    unzClose(uf);

    return err == UNZ_OK && ginfo.number_entry > 0;
}

BOOL InstallCopyFiles(void)
{
    zlib_filefunc64_def ffunc;
#ifdef UNICODE
    fill_win32_filefunc64W(&ffunc);
#else
    fill_win32_filefunc64A(&ffunc);
#endif
    unzFile uf = unzOpen2_64(GetExePath(), &ffunc);
    if (!uf) {
        NotifyFailed(_T("Invalid payload format"));
        goto Error;
    }

    unz_global_info64 ginfo;
    int err = unzGetGlobalInfo64(uf, &ginfo);
    if (err != UNZ_OK) {
        NotifyFailed(_T("Broken payload format (couldn't get global info)"));
        goto Error;
    }

    // extract all contained files one by one
    int filesCopied = 0;
    for (int count = 0; count < ginfo.number_entry; count++) {
        BOOL success = FALSE;
        char filename[MAX_PATH];
        unz_file_info64 finfo;
        err = unzGetCurrentFileInfo64(uf, &finfo, filename, dimof(filename), NULL, 0, NULL, 0);
        if (err != UNZ_OK) {
            NotifyFailed(_T("Broken payload format (couldn't get file info)"));
            goto Error;
        }

        err = unzOpenCurrentFilePassword(uf, NULL);
        if (err != UNZ_OK) {
            NotifyFailed(_T("Can't access payload data"));
            goto Error;
        }

        // TODO: extract block by block instead of everything at once
        char *data = SAZA(char, (size_t)finfo.uncompressed_size);
        if (!data) {
            NotifyFailed(_T("Not enough memory to extract all files"));
            goto Error;
        }

        err = unzReadCurrentFile(uf, data, (unsigned int)finfo.uncompressed_size);
        if (err != (int)finfo.uncompressed_size) {
            NotifyFailed(_T("Payload data was damaged (parts missing)"));
            free(data);
            goto Error;
        }

        TCHAR *inpath = ansi_to_tstr(filename);
        TCHAR *extpath = tstr_cat3(gGlobalData.installDir, _T("\\"), FilePath_GetBaseName(inpath));

        BOOL ok = write_to_file(extpath, data, (size_t)finfo.uncompressed_size);
        if (ok) {
            // set modification time to original value
            HANDLE hFile = CreateFile(extpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                FILETIME ftModified, ftLocal;
                DosDateTimeToFileTime(HIWORD(finfo.dosDate), LOWORD(finfo.dosDate), &ftLocal);
                LocalFileTimeToFileTime(&ftLocal, &ftModified);
                SetFileTime(hFile, NULL, NULL, &ftModified);
                CloseHandle(hFile);
            }
            success = TRUE;
            filesCopied++;
        }
        else {
            NotifyFailed(_T("Couldn't write extracted file to disk"));
            success = FALSE;
        }

        free(inpath);
        free(extpath);
        free(data);

        err = unzCloseCurrentFile(uf);
        if (err != UNZ_OK) {
            NotifyFailed(_T("Payload data was damaged (CRC failed)"));
            goto Error;
        }

        err = unzGoToNextFile(uf);
        if (err != UNZ_OK || !success)
            break;
    }

    unzClose(uf);
    return filesCopied == ginfo.number_entry;

Error:
    if (uf) {
        unzCloseCurrentFile(uf);
        unzClose(uf);
    }
    return FALSE;
}

#ifdef __cplusplus
extern "C" {
#endif
// needed because we compile bzip2 with #define BZ_NO_STDIO
void bz_internal_error(int errcode)
{
    NotifyFailed(_T("fatal error: bz_internal_error()"));
}
#ifdef __cplusplus
}
#endif

BOOL CreateUninstaller(void)
{
    size_t installerSize;
    char *installerData = file_read_all(GetExePath(), &installerSize);
    if (!installerData) {
        NotifyFailed(_T("Couldn't access installer for uninstaller extraction"));
        return FALSE;
    }

    if (!gUnInstMark)
        gUnInstMark = wstr_to_utf8(UN_INST_MARK);
    int markSize = str_len(gUnInstMark);

    // find the end of the (un)installer
    char *end = (char *)memchr(installerData, *gUnInstMark, installerSize);
    while (end && memcmp(end, gUnInstMark, markSize) != 0)
        end = (char *)memchr(end + 1, *gUnInstMark, installerSize - (end - installerData) - 1);

    // if it's not found, append a new uninstaller marker to the end of the executable
    if (!end) {
        char *extData = (char *)realloc(installerData, installerSize + markSize);
        if (!extData) {
            NotifyFailed(_T("Couldn't write uninstaller to disk"));
            free(installerData);
            return FALSE;
        }
        memcpy(installerData + installerSize, gUnInstMark, markSize);
        installerSize += markSize;
    }
    else {
        installerSize = end - installerData + markSize;
    }

    TCHAR *uninstallerPath = GetUninstallerPath();
    BOOL ok = write_to_file(uninstallerPath, installerData, installerSize);
    free(uninstallerPath);
    free(installerData);

    if (!ok)
        NotifyFailed(_T("Couldn't write uninstaller to disk"));

    return ok;
}

bool IsUninstaller()
{
    char *data = NULL;
    if (!gUnInstMark)
        gUnInstMark = wstr_to_utf8(UN_INST_MARK);
    int markSize = str_len(gUnInstMark);

    HANDLE h = CreateFile(GetExePath(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        NotifyFailed(_T("Couldn't open myself for reading"));
        goto Error;
    }
    DWORD res = SetFilePointer(h, -markSize, NULL, FILE_END);
    if (INVALID_SET_FILE_POINTER == res) {
        NotifyFailed(_T("Couldn't seek to end"));
        goto Error;
    }

    data = SAZA(char, markSize);
    DWORD bytesRead;
    BOOL ok = ReadFile(h, data, markSize, &bytesRead, NULL);
    if (!ok || bytesRead != markSize) {
        NotifyFailed(_T("Couldn't read marker at end of file"));
        goto Error;
    }

    CloseHandle(h);
    bool isUninstaller = !memcmp(data, gUnInstMark, markSize);
    free(data);

    return isUninstaller;

Error:
    free(data);
    CloseHandle(h);
    return FALSE;
}

static HFONT CreateDefaultGuiFont()
{
    NONCLIENTMETRICS m = { sizeof (NONCLIENTMETRICS) };
    if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &m, 0))
    {
        // fonts: lfMenuFont, lfStatusFont, lfMessageFont, lfCaptionFont
        return CreateFontIndirect(&m.lfMessageFont);
    }
    return NULL;
}

void InvalidateFrame()
{
    RECT rc;
    GetClientRect(gHwndFrame, &rc);
    rc.bottom -= BOTTOM_PART_DY;
    InvalidateRect(gHwndFrame, &rc, FALSE);
}

HANDLE CreateProcessHelper(TCHAR *exe, TCHAR *args=NULL)
{
    PROCESS_INFORMATION pi;
    STARTUPINFO si = {0};
    si.cb = sizeof(si);
    TCHAR *cmd;
    // per msdn, cmd has to be writeable
    if (args) {
        // Note: doesn't quote the args if but it's good enough for us
        cmd = tstr_cat3(exe, _T(" "), args);
    }
    else
        cmd = tstr_dup(exe);
    if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        free(cmd);
        return NULL;
    }
    free(cmd);
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

// cf. http://support.microsoft.com/default.aspx?scid=kb;en-us;207132
BOOL RegisterServerDLL(TCHAR *dllPath, BOOL unregister=FALSE)
{
    if (FAILED(OleInitialize(NULL)))
        return FALSE;

    BOOL success = FALSE;
    HMODULE lib = LoadLibrary(dllPath);
    if (lib) {
        FARPROC CallDLL = GetProcAddress(lib, unregister ? "DllUnregisterServer" : "DllRegisterServer");
        if (CallDLL)
            success = SUCCEEDED(CallDLL());
        FreeLibrary(lib);
    }

    OleUninitialize();

    return success;
}

// Note: doesn't handle (total) sizes above 4GB
DWORD GetDirSize(TCHAR *dir)
{
    DWORD totalSize = 0;
    WIN32_FIND_DATA findData;

    TCHAR *dirPattern = tstr_cat(dir, _T("\\*"));

    HANDLE h = FindFirstFile(dirPattern, &findData);
    if (h == INVALID_HANDLE_VALUE)
        goto Exit;

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            totalSize += findData.nFileSizeLow;
        }
        else if (!tstr_eq(findData.cFileName, _T(".")) && !tstr_eq(findData.cFileName, _T(".."))) {
            TCHAR *subdir = tstr_cat3(dir, _T("\\"), findData.cFileName);
            totalSize += GetDirSize(subdir);
            free(subdir);
        }
    } while (FindNextFile(h, &findData) != 0);
    FindClose(h);
Exit:
    free(dirPattern);
    return totalSize;
}

void WriteUninstallerRegistryInfo()
{
    HKEY hkey = HKEY_LOCAL_MACHINE;
    TCHAR *uninstallerPath = GetUninstallerPath();
    TCHAR *installedExePath = GetInstalledExePath();
    WriteRegStr(hkey,   REG_PATH_UNINST, DISPLAY_ICON, installedExePath);
    WriteRegStr(hkey,   REG_PATH_UNINST, DISPLAY_NAME, TAPP);
    WriteRegStr(hkey,   REG_PATH_UNINST, DISPLAY_VERSION, CURR_VERSION_STR);
    WriteRegDWORD(hkey, REG_PATH_UNINST, ESTIMATED_SIZE, GetDirSize(gGlobalData.installDir));
    WriteRegDWORD(hkey, REG_PATH_UNINST, NO_MODIFY, 1);
    WriteRegDWORD(hkey, REG_PATH_UNINST, NO_REPAIR, 1);
    WriteRegStr(hkey,   REG_PATH_UNINST, PUBLISHER, _T("Krzysztof Kowalczyk"));
    WriteRegStr(hkey,   REG_PATH_UNINST, UNINSTALL_STRING, uninstallerPath);
    WriteRegStr(hkey,   REG_PATH_UNINST, URL_INFO_ABOUT, _T("http://blog.kowalczyk/info/software/sumatrapdf/"));
    TCHAR *installDir = FilePath_GetDir(installedExePath);
    WriteRegStr(hkey,   REG_PATH_SOFTWARE, INSTALL_DIR, installDir);

    free(installDir);
    free(uninstallerPath);
    free(installedExePath);
}

BOOL RegDelKeyRecurse(HKEY hkey, TCHAR *path)
{
    LSTATUS res;
    res = SHDeleteKey(hkey, path);
    if ((ERROR_SUCCESS != res) && (res != ERROR_FILE_NOT_FOUND)) {
        SeeLastError(res);
        return FALSE;
    }
    return TRUE;
}

BOOL RemoveUninstallerRegistryInfo()
{
    BOOL ok1 = RegDelKeyRecurse(HKEY_LOCAL_MACHINE, REG_PATH_UNINST);
    BOOL ok2 = RegDelKeyRecurse(HKEY_LOCAL_MACHINE, REG_PATH_SOFTWARE);
    return ok1 && ok2;
}

/* Undo what DoAssociateExeWithPdfExtension() in SumatraPDF.cpp did */
void UnregisterFromBeingDefaultViewer(HKEY hkey)
{
    TCHAR buf[MAX_PATH + 8];
    bool ok = ReadRegStr(hkey, REG_CLASSES_APP, _T("previous.pdf"), buf, dimof(buf));
    if (ok) {
        WriteRegStr(hkey, REG_CLASSES_PDF, NULL, buf);
    } else {
        bool ok = ReadRegStr(hkey, REG_CLASSES_PDF, NULL, buf, dimof(buf));
        if (ok && tstr_eq(TAPP, buf))
            RegDelKeyRecurse(hkey, REG_CLASSES_PDF);
    }
    RegDelKeyRecurse(hkey, REG_CLASSES_APP);
}

void UnregisterExplorerFileExts()
{
    TCHAR buf[MAX_PATH + 8];
    bool ok = ReadRegStr(HKEY_CURRENT_USER, REG_EXPLORER_PDF_EXT, PROG_ID, buf, dimof(buf));
    if (!ok || !tstr_eq(buf, TAPP))
        return;

    HKEY hk;
    LONG res = RegOpenKeyEx(HKEY_CURRENT_USER, REG_EXPLORER_PDF_EXT, 0, KEY_SET_VALUE, &hk);
    if (ERROR_SUCCESS != res)
        return;

    res = RegDeleteValue(hk, PROG_ID);
    if (res != ERROR_SUCCESS) {
        SeeLastError(res);
    }
    RegCloseKey(hk);
}

void UnregisterFromBeingDefaultViewer()
{
    UnregisterFromBeingDefaultViewer(HKEY_LOCAL_MACHINE);
    UnregisterFromBeingDefaultViewer(HKEY_CURRENT_USER);
    UnregisterExplorerFileExts();
}

void UninstallBrowserPlugin()
{
    TCHAR *dllPath = GetBrowserPluginPath();
    RegisterServerDLL(dllPath, TRUE);
    free(dllPath);
}

/* Caller needs to free() the result. */
TCHAR *GetDefaultPdfViewer()
{
    TCHAR buf[MAX_PATH];
    bool ok = ReadRegStr(HKEY_CLASSES_ROOT, _T(".pdf"), NULL, buf, dimof(buf));
    if (!ok)
        return NULL;
    return tstr_dup(buf);
}

BOOL RemoveDirectoryWithFiles(TCHAR *dir)
{
    WIN32_FIND_DATA findData;
    BOOL success = TRUE;

    TCHAR *dirPattern = tstr_cat(dir, _T("\\*"));
    HANDLE h = FindFirstFile(dirPattern, &findData);
    if (h != INVALID_HANDLE_VALUE)
    {
        do {
            TCHAR *path = tstr_cat3(dir, _T("\\"), findData.cFileName);
            DWORD attrs = findData.dwFileAttributes;
            // filter out directories. Even though there shouldn't be any
            // subdirectories, it also filters out the standard "." and ".."
            if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                // per http://msdn.microsoft.com/en-us/library/aa363915(v=VS.85).aspx
                // have to remove read-only attribute for DeleteFile() to work
                if (attrs & FILE_ATTRIBUTE_READONLY) {
                    attrs = attrs & ~FILE_ATTRIBUTE_READONLY;
                    SetFileAttributes(path, attrs);
                }
                DeleteFile(path);
            }
            else if (!tstr_eq(findData.cFileName, _T(".")) && !tstr_eq(findData.cFileName, _T(".."))) {
                success &= RemoveDirectoryWithFiles(path);
            }
            free(path);
        } while (FindNextFile(h, &findData) != 0);
        FindClose(h);
    }

    if (!RemoveDirectory(dir)) {
        if (ERROR_FILE_NOT_FOUND != GetLastError()) {
            SeeLastError();
            success = FALSE;
        }
    }
    free(dirPattern);

    return success;
}

BOOL RemoveInstallationDirectory()
{
    return RemoveDirectoryWithFiles(gGlobalData.installDir);
}

BOOL CreateShortcut(TCHAR *shortcutPath, TCHAR *exePath, TCHAR *workingDir, TCHAR *description)
{
    IShellLink* sl = NULL;
    IPersistFile* pf = NULL;
    BOOL ok = TRUE;

    ComScope comScope;

    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (void **)&sl);
    if (FAILED(hr)) 
        goto Exit;

    hr = sl->QueryInterface(IID_IPersistFile, (void **)&pf);
    if (FAILED(hr))
        goto Exit;

    hr = sl->SetPath(exePath);
    sl->SetWorkingDirectory(workingDir);
    //sl->SetShowCmd(SW_SHOWNORMAL);
    //sl->SetHotkey(0);
    sl->SetIconLocation(exePath, 0);
    //sl->SetArguments(_T(""));
    if (description)
        sl->SetDescription(description);

#ifndef _UNICODE
    WCHAR *shortcutPathW = multibyte_to_wstr(shortcutPath, CP_ACP);
    hr = pf->Save(shortcutPathW, TRUE);
    free(shortcutPathW);
#else
    hr = pf->Save(shortcutPath, TRUE);
#endif

Exit:
    if (pf)
        pf->Release();
    if (sl)
        sl->Release();

    if (FAILED(hr)) {
        ok = FALSE;
        SeeLastError();
    }
    return ok;
}

BOOL CreateAppShortcut(bool allUsers)
{
    TCHAR *installedExePath = GetInstalledExePath();
    TCHAR *shortcutPath = GetShortcutPath(allUsers);
    BOOL ok = CreateShortcut(shortcutPath, installedExePath, gGlobalData.installDir, NULL);
    free(installedExePath);
    free(shortcutPath);
    return ok;
}

BOOL RemoveShortcut(bool allUsers)
{
    TCHAR *p = GetShortcutPath(allUsers);
    BOOL ok = DeleteFile(p);
    if (!ok && (ERROR_FILE_NOT_FOUND != GetLastError()))
        SeeLastError();
    else
        ok = TRUE;
    free(p);
    return ok;
}

BOOL CreateInstallationDirectory()
{
    BOOL ok = CreateDirectory(gGlobalData.installDir, NULL);
    if (!ok && (GetLastError() != ERROR_ALREADY_EXISTS)) {
        SeeLastError();
        NotifyFailed(_T("Couldn't create installation directory"));
    } else {
        ok = TRUE;
    }
    return ok;
}

void CreateButtonExit(HWND hwndParent)
{
    RECT    r;
    int     x, y;
    int     buttonDx = 80;
    int     buttonDy = 22;

    // TODO: determine the sizes of buttons by measuring their real size
    // and adjust size of the window appropriately
    GetClientRect(hwndParent, &r);
    x = RectDx(&r) - buttonDx - 8;
    y = RectDy(&r) - buttonDy - 8;
    gHwndButtonExit = CreateWindow(WC_BUTTON, _T("Close"),
                        BS_DEFPUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        x, y, buttonDx, buttonDy, hwndParent, 
                        (HMENU)ID_BUTTON_EXIT,
                        ghinst, NULL);
    SetFont(gHwndButtonExit, gFontDefault);
}

void CreateButtonRunSumatra(HWND hwndParent)
{
    RECT    r;
    int     x, y;
    int     buttonDx = 120;
    int     buttonDy = 22;

    // TODO: determine the sizes of buttons by measuring their real size
    // and adjust size of the window appropriately
    GetClientRect(hwndParent, &r);
    x = RectDx(&r) - buttonDx - 8;
    y = RectDy(&r) - buttonDy - 8;
    gHwndButtonRunSumatra= CreateWindow(WC_BUTTON, _T("Start ") TAPP,
                        BS_DEFPUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        x, y, buttonDx, buttonDy, hwndParent, 
                        (HMENU)ID_BUTTON_START_SUMATRA,
                        ghinst, NULL);
    SetFont(gHwndButtonRunSumatra, gFontDefault);
}

void OnButtonStartSumatra()
{
    TCHAR *s = GetInstalledExePath();
    HANDLE h = CreateProcessHelper(s);
    CloseHandle(h);
    free(s);
    SendMessage(gHwndFrame, WM_CLOSE, 0, 0);
}

static DWORD WINAPI InstallerThread(LPVOID data)
{
    gGlobalData.success = false;

    /* if the app is running, we have to kill it so that we can over-write the executable */
    TCHAR *exePath = GetInstalledExePath();
    KillProcess(exePath, TRUE);
    free(exePath);

    if (!CreateInstallationDirectory())
        goto Error;

    if (!InstallCopyFiles())
        goto Error;

    if (gGlobalData.registerAsDefault) {
        // need to sublaunch SumatraPDF.exe instead of replicating the code
        // because registration uses translated strings
        TCHAR *installedExePath = GetInstalledExePath();
        HANDLE h = CreateProcessHelper(installedExePath, _T("-register-for-pdf"));
        CloseHandle(h);
        free(installedExePath);
    }

    if (gGlobalData.installBrowserPlugin) {
        TCHAR *dllPath = GetBrowserPluginPath();
        RegisterServerDLL(dllPath);
        free(dllPath);
    }

    if (!CreateUninstaller())
        goto Error;

    WriteUninstallerRegistryInfo();
    if (!CreateAppShortcut(true) && !CreateAppShortcut(false)) {
        NotifyFailed(_T("Failed to create a shortcut"));
        goto Error;
    }

    gGlobalData.success = true;
Error:
    // TODO: roll back installation on failure (restore previous installation!)
    if (!gGlobalData.silent)
        PostMessage(gHwndFrame, WM_APP_INSTALLATION_FINISHED, 0, 0);
    return 0;
}

void OnButtonInstall()
{
    gGlobalData.registerAsDefault = !!GetCheckboxState(gHwndCheckboxRegisterDefault);
    gGlobalData.installBrowserPlugin = !!GetCheckboxState(gHwndCheckboxRegisterBrowserPlugin);

    // disable the install button and remove checkbox during installation
    DestroyWindow(gHwndCheckboxRegisterDefault);
    gHwndCheckboxRegisterDefault = NULL;
    DestroyWindow(gHwndCheckboxRegisterBrowserPlugin);
    gHwndCheckboxRegisterBrowserPlugin = NULL;
    EnableWindow(gHwndButtonInstall, FALSE);

    gMsg = _T("Installation in progress...");
    gMsgColor = COLOR_MSG_INSTALLATION;
    InvalidateFrame();

    gGlobalData.hThread = CreateThread(NULL, 0, InstallerThread, NULL, 0, 0);
}

void OnInstallationFinished()
{
    DestroyWindow(gHwndButtonInstall);
    gHwndButtonInstall = NULL;

    if (gGlobalData.success) {
        CreateButtonRunSumatra(gHwndFrame);
        gMsg = _T("Thank you! ") TAPP _T(" has been installed.");
        gMsgColor = COLOR_MSG_OK;
    } else {
        CreateButtonExit(gHwndFrame);
        gMsg = _T("Installation failed!");
        gMsgColor = COLOR_MSG_FAILED;
    }
    gMsgError = gGlobalData.firstError;
    InvalidateFrame();

    CloseHandle(gGlobalData.hThread);
}

static DWORD WINAPI UninstallerThread(LPVOID data)
{
    /* if the app is running, we have to kill it to delete the files */
    TCHAR *exePath = GetInstalledExePath();
    KillProcess(exePath, TRUE);
    free(exePath);

    if (!RemoveUninstallerRegistryInfo())
        NotifyFailed(_T("Failed to delete uninstaller registry keys"));

    if (!RemoveShortcut(true) && !RemoveShortcut(false))
        NotifyFailed(_T("Couldn't remove the shortcut"));

    UnregisterFromBeingDefaultViewer();
    UninstallBrowserPlugin();

    if (!RemoveInstallationDirectory())
        NotifyFailed(_T("Couldn't remove installation directory"));

    // always succeed, even for partial uninstallations
    gGlobalData.success = true;

    if (!gGlobalData.silent)
        PostMessage(gHwndFrame, WM_APP_INSTALLATION_FINISHED, 0, 0);
    return 0;
}

void OnButtonUninstall()
{
    // disable the button during uninstallation
    EnableWindow(gHwndButtonUninstall, FALSE);
    gMsg = _T("Uninstallation in progress...");
    gMsgColor = COLOR_MSG_INSTALLATION;
    InvalidateFrame();

    gGlobalData.hThread = CreateThread(NULL, 0, UninstallerThread, NULL, 0, 0);
}

void OnUninstallationFinished()
{
    DestroyWindow(gHwndButtonUninstall);
    gHwndButtonUninstall = NULL;
    CreateButtonExit(gHwndFrame);
    gMsg = TAPP _T(" has been uninstalled.");
    gMsgError = gGlobalData.firstError;
    if (!gMsgError)
        gMsgColor = COLOR_MSG_OK;
    else
        gMsgColor = COLOR_MSG_FAILED;
    InvalidateFrame();

    CloseHandle(gGlobalData.hThread);
}

void OnButtonExit()
{
    SendMessage(gHwndFrame, WM_CLOSE, 0, 0);
}

// This display is inspired by http://letteringjs.com/
typedef struct {
    // part that doesn't change
    char c;
    Color col, colShadow;
    REAL rotation;
    REAL dyOff; // displacement

    // part calculated during layout
    REAL dx, dy;
    REAL x;
} LetterInfo;

LetterInfo gLetters[] = {
    { 'S', gCol1, gCol1Shadow, -3.f,     0, 0, 0 },
    { 'U', gCol2, gCol2Shadow,  0.f,     0, 0, 0 },
    { 'M', gCol3, gCol3Shadow,  2.f,  -2.f, 0, 0 },
    { 'A', gCol4, gCol4Shadow,  0.f, -2.4f, 0, 0 },
    { 'T', gCol5, gCol5Shadow,  0.f,     0, 0, 0 },
    { 'R', gCol5, gCol5Shadow, 2.3f, -1.4f, 0, 0 },
    { 'A', gCol4, gCol4Shadow,  0.f,     0, 0, 0 },
    { 'P', gCol3, gCol3Shadow,  0.f, -2.3f, 0, 0 },
    { 'D', gCol2, gCol2Shadow,  0.f,   3.f, 0, 0 },
    { 'F', gCol1, gCol1Shadow,  0.f,     0, 0, 0 }
};

#define SUMATRA_LETTERS_COUNT (dimof(gLetters))

char RandUppercaseLetter()
{
    // TODO: clearly, not random but seem to work ok anyway
    static char l = 'A' - 1;
    l++;
    if (l > 'Z')
        l = 'A';
    return l;
}

void RandomizeLetters()
{
    for (int i=0; i<dimof(gLetters); i++) {
        gLetters[i].c = RandUppercaseLetter();
    }
}

void SetLettersSumatraUpTo(int n)
{
    char *s = "SUMATRAPDF";
    for (int i=0; i<dimof(gLetters); i++) {
        if (i < n) {
            gLetters[i].c = s[i];
        } else {
            gLetters[i].c = ' ';
        }
    }
}

void SetLettersSumatra()
{
    SetLettersSumatraUpTo(SUMATRA_LETTERS_COUNT);
}

class FrameTimeoutCalculator {

    LARGE_INTEGER   timeStart;
    LARGE_INTEGER   timeLast;
    LONGLONG        ticksPerFrame;
    LONGLONG        ticsPerMs;
    LARGE_INTEGER   timeFreq;

public:
    FrameTimeoutCalculator(int framesPerSecond) {
        QueryPerformanceFrequency(&timeFreq); // number of ticks per second
        ticsPerMs = timeFreq.QuadPart / 1000;
        ticksPerFrame = timeFreq.QuadPart / framesPerSecond;
        QueryPerformanceCounter(&timeStart);
        timeLast = timeStart;
    }

    // in seconds, as a double
    double ElapsedTotal() {
        LARGE_INTEGER timeCurr;
        QueryPerformanceCounter(&timeCurr);
        LONGLONG elapsedTicks =  timeCurr.QuadPart - timeStart.QuadPart;
        double res = (double)elapsedTicks / (double)timeFreq.QuadPart;
        return res;
    }

    DWORD GetTimeoutInMilliseconds() {
        LARGE_INTEGER timeCurr;
        LONGLONG elapsedTicks;
        QueryPerformanceCounter(&timeCurr);
        elapsedTicks = timeCurr.QuadPart - timeLast.QuadPart;
        if (elapsedTicks > ticksPerFrame) {
            return 0;
        } else {
            LONGLONG timeoutMs = (ticksPerFrame - elapsedTicks) / ticsPerMs;
            return (DWORD)timeoutMs;
        }
    }

    void Step() {
        timeLast.QuadPart += ticksPerFrame;
    }
};

// an animation that 'rotates' random letters 
static FrameTimeoutCalculator *gRotatingLettersAnim = NULL;

void RotatingLettersAnimStart()
{
    gRotatingLettersAnim = new FrameTimeoutCalculator(20);
}

void RotatingLettersAnimStop()
{
    delete gRotatingLettersAnim;
    gRotatingLettersAnim = NULL;
    SetLettersSumatra();
    InvalidateFrame();
}

void RotatingLettersAnim()
{
    if (gRotatingLettersAnim->ElapsedTotal() > 3) {
        RotatingLettersAnimStop();
        return;
    }
    DWORD timeOut = gRotatingLettersAnim->GetTimeoutInMilliseconds();
    if (timeOut != 0)
        return;
    RandomizeLetters();
    InvalidateFrame();
    gRotatingLettersAnim->Step();
}

// an animation that reveals letters one by one

// how long the animation lasts, in seconds
#define REVEALING_ANIM_DUR double(2)

static FrameTimeoutCalculator *gRevealingLettersAnim = NULL;

int gRevealingLettersAnimLettersToShow;

void RevealingLettersAnimStart()
{
    int framesPerSec = (int)(double(SUMATRA_LETTERS_COUNT) / REVEALING_ANIM_DUR);
    gRevealingLettersAnim = new FrameTimeoutCalculator(framesPerSec);
    gRevealingLettersAnimLettersToShow = 0;
    SetLettersSumatraUpTo(0);
}

void RevealingLettersAnimStop()
{
    delete gRevealingLettersAnim;
    gRevealingLettersAnim = NULL;
    SetLettersSumatra();
    InvalidateFrame();
}

void RevealingLettersAnim()
{
    if (gRevealingLettersAnim->ElapsedTotal() > REVEALING_ANIM_DUR) {
        RevealingLettersAnimStop();
        return;
    }
    DWORD timeOut = gRevealingLettersAnim->GetTimeoutInMilliseconds();
    if (timeOut != 0)
        return;
    SetLettersSumatraUpTo(++gRevealingLettersAnimLettersToShow);
    gRevealingLettersAnim->Step();
    InvalidateFrame();
}

void AnimStep() {
    if (gRotatingLettersAnim) {
        RotatingLettersAnim();
    }
    if (gRevealingLettersAnim) {
        RevealingLettersAnim();
    }
}

void CalcLettersLayout(Graphics& g, Font *f, int dx)
{
    static BOOL didLayout = FALSE;
    if (didLayout) return;

    LetterInfo *li;
    StringFormat sfmt;
    const REAL letterSpacing = -12.f;
    REAL totalDx = -letterSpacing; // counter last iteration of the loop
    WCHAR s[2] = { 0 };
    PointF origin(0.f, 0.f);
    RectF bbox;
    for (int i=0; i<dimof(gLetters); i++) {
        li = &gLetters[i];
        s[0] = li->c;
        g.MeasureString(s, 1, f, origin, &sfmt, &bbox);
        li->dx = bbox.Width;
        li->dy = bbox.Height;
        totalDx += li->dx;
        totalDx += letterSpacing;
    }

    REAL x = ((REAL)dx - totalDx) / 2.f;
    for (int i=0; i<dimof(gLetters); i++) {
        li = &gLetters[i];
        li->x = x;
        x += li->dx;
        x += letterSpacing;
    }
    //RotatingLettersAnimStart();
    RevealingLettersAnimStart();
    didLayout = TRUE;
}

REAL DrawMessage(Graphics &g, TCHAR *msg, REAL y, REAL dx, Color color)
{
    WCHAR *s = tstr_to_wstr(msg);

    Font f(L"Impact", 16, FontStyleRegular);
    RectF maxbox(0, y, dx, 0);
    RectF bbox;
    g.MeasureString(s, -1, &f, maxbox, &bbox);

    bbox.X += (dx - bbox.Width) / 2.f;
    StringFormat sft;
    sft.SetAlignment(StringAlignmentCenter);
#ifdef DRAW_TEXT_SHADOW
    {
        bbox.X--; bbox.Y++;
        SolidBrush b(Color(255,255,255));
        g.DrawString(s, -1, &f, bbox, &sft, &b);
        bbox.X++; bbox.Y--;
    }
#endif
    SolidBrush b(color);
    g.DrawString(s, -1, &f, bbox, &sft, &b);
    free(s);

    return bbox.Height;
}

void DrawSumatraLetters(Graphics &g, Font *f, Font *fVer, REAL y)
{
    LetterInfo *li;
    WCHAR s[2] = { 0 };
    for (int i=0; i<dimof(gLetters); i++) {
        li = &gLetters[i];
        s[0] = li->c;
        if (s[0] == ' ')
            return;

        g.RotateTransform(li->rotation, MatrixOrderAppend);
#ifdef DRAW_TEXT_SHADOW
        // draw shadow first
        SolidBrush b2(li->colShadow);
        PointF o2(li->x - 3.f, y + 4.f + li->dyOff);
        g.DrawString(s, 1, f, o2, &b2);
#endif

        SolidBrush b1(li->col);
        PointF o1(li->x, y + li->dyOff);
        g.DrawString(s, 1, f, o1, &b1);
        g.RotateTransform(li->rotation, MatrixOrderAppend);
        g.ResetTransform();
    }

    // draw version number
    REAL x = gLetters[dimof(gLetters)-1].x;
    g.TranslateTransform(x, y);
    g.RotateTransform(45.f);
    REAL x2 = 15; REAL y2 = -34;

    WCHAR *ver_s = tstr_to_wstr(_T("v") CURR_VERSION_STR);
#ifdef DRAW_TEXT_SHADOW
    SolidBrush b1(Color(0,0,0));
    g.DrawString(ver_s, -1, fVer, PointF(x2-2,y2-1), &b1);
#endif
    SolidBrush b2(Color(255,255,255));
    g.DrawString(ver_s, -1, fVer, PointF(x2,y2), &b2);
    g.ResetTransform();
    free(ver_s);
}

void DrawFrame2(Graphics &g, RECT *r)
{
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPageUnit(UnitPixel);

    Font f(L"Impact", 40, FontStyleRegular);
    CalcLettersLayout(g, &f, RectDx(r));

    SolidBrush bgBrush(Color(255,242,0));
    Rect r2(r->top-1, r->left-1, RectDx(r)+1, RectDy(r)+1);
    g.FillRectangle(&bgBrush, r2);

    Font f2(L"Impact", 16, FontStyleRegular);
    DrawSumatraLetters(g, &f, &f2, 18.f);

    REAL msgY = (REAL)(RectDy(r) / 2);
    if (gMsg)
        msgY += DrawMessage(g, gMsg, msgY, (REAL)RectDx(r), gMsgColor) + 5;
    if (gMsgError)
        DrawMessage(g, gMsgError, msgY, (REAL)RectDx(r), COLOR_MSG_FAILED);
}

void DrawFrame(HWND hwnd, HDC dc, PAINTSTRUCT *ps)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - BOTTOM_PART_DY;
    RECT rcTmp;
    if (IntersectRect(&rcTmp, &rc, &ps->rcPaint)) {
        HBRUSH brushNativeBg = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        FillRect(dc, &rc, brushNativeBg);
        DeleteObject(brushNativeBg);
    }

    // TODO: cache bmp object?
    Graphics g(dc);
    GetClientRect(hwnd, &rc);
    rc.bottom -= BOTTOM_PART_DY;
    int dx = RectDx(&rc); int dy = RectDy(&rc);
    Bitmap bmp(dx, dy, &g);
    Graphics g2((Image*)&bmp);
    DrawFrame2(g2, &rc);
    g.DrawImage(&bmp, 0, 0);
}

void OnPaintFrame(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    DrawFrame(hwnd, dc, &ps);
    EndPaint(hwnd, &ps);
}

void OnMouseMove(HWND hwnd, int x, int y)
{
    // nothing so far
}

void OnCreateUninstaller(HWND hwnd)
{
    RECT    r;
    int     x, y;
    int     buttonDx = 128;
    int     buttonDy = 22;

    GetClientRect(hwnd, &r);
    x = RectDx(&r) - buttonDx - 8;
    y = RectDy(&r) - buttonDy - 8;
    // TODO: determine the sizes of buttons by measuring their real size
    // and adjust size of the window appropriately
    gHwndButtonUninstall = CreateWindow(WC_BUTTON, _T("Uninstall ") TAPP,
                        BS_DEFPUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        x, y, buttonDx, buttonDy, hwnd,
                        (HMENU)ID_BUTTON_UNINSTALL, ghinst, NULL);
    SetFont(gHwndButtonUninstall, gFontDefault);
}

static LRESULT CALLBACK UninstallerWndProcFrame(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int         wmId;
    int         x, y;

    switch (message)
    {
        case WM_CREATE:
            OnCreateUninstaller(hwnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            OnPaintFrame(hwnd);
            break;

        case WM_COMMAND:
            wmId = LOWORD(wParam);
            switch (wmId)
            {
                case IDOK:
                    if (gHwndButtonUninstall)
                        OnButtonUninstall();
                    else if (gHwndButtonExit)
                        OnButtonExit();
                    break;

                case ID_BUTTON_UNINSTALL:
                    OnButtonUninstall();
                    break;

                case ID_BUTTON_EXIT:
                case IDCANCEL:
                    OnButtonExit();
                    break;

                default:
                    return DefWindowProc(hwnd, message, wParam, lParam);
            }
            break;

        case WM_MOUSEMOVE:
            x = GET_X_LPARAM(lParam); y = GET_Y_LPARAM(lParam);
            OnMouseMove(hwnd, x, y);
            break;

        case WM_APP_INSTALLATION_FINISHED:
            OnUninstallationFinished();
            SetFocus(gHwndButtonExit);
            break;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}

void OnCreateInstaller(HWND hwnd)
{
    RECT    r;
    int     x, y;
    int     buttonDx = 120;
    int     buttonDy = 22;

    // TODO: determine the sizes of buttons by measuring their real size
    // and adjust size of the window appropriately
    GetClientRect(hwnd, &r);
    x = RectDx(&r) - buttonDx - 8;
    y = RectDy(&r) - buttonDy - 8;
    gHwndButtonInstall = CreateWindow(WC_BUTTON, _T("Install ") TAPP,
                        BS_DEFPUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        x, y, buttonDx, buttonDy, hwnd, 
                        (HMENU)ID_BUTTON_INSTALL, ghinst, NULL);
    SetFont(gHwndButtonInstall, gFontDefault);

    y = RectDy(&r) - buttonDy - 5;

    TCHAR *defaultViewer = GetDefaultPdfViewer();
    BOOL hasOtherViewer = defaultViewer && !tstr_ieq(defaultViewer, TAPP);
    BOOL isSumatraDefaultViewer = defaultViewer && !hasOtherViewer;

    // only show the checbox if Sumatra is not already a default viewer.
    // the alternative (disabling the checkbox) is more confusing
    if (!isSumatraDefaultViewer) {
        gHwndCheckboxRegisterDefault = CreateWindow(
            WC_BUTTON, _T("Use as &default PDF reader"),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            8, y, 160, 22, hwnd, (HMENU)ID_CHECKBOX_MAKE_DEFAULT, ghinst, NULL);
        SetFont(gHwndCheckboxRegisterDefault, gFontDefault);
        // only check the "Use as default" checkbox when no other PDF viewer
        // is currently selected (not going to intrude)
        SetCheckboxState(gHwndCheckboxRegisterDefault, !hasOtherViewer || gGlobalData.registerAsDefault);
        free(defaultViewer);
        y -= 18;
    }

    gHwndCheckboxRegisterBrowserPlugin = CreateWindow(
        WC_BUTTON, _T("Install web browser plugin"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        8, y, 160, 22, hwnd, (HMENU)ID_CHECKBOX_BROWSER_PLUGIN, ghinst, NULL);
    SetFont(gHwndCheckboxRegisterBrowserPlugin, gFontDefault);
    SetCheckboxState(gHwndCheckboxRegisterBrowserPlugin, gGlobalData.installBrowserPlugin);

    SetFocus(gHwndButtonInstall);
}

static LRESULT CALLBACK InstallerWndProcFrame(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int         wmId;
    int         x, y;

    switch (message)
    {
        case WM_CREATE:
            if (IsValidInstaller()) {
                OnCreateInstaller(hwnd);
            }
            else {
                MessageBox(NULL, _T("The installer has been corrupted. Please download it again.\nSorry for the inconvenience!"), _T("Installation failed"),  MB_ICONEXCLAMATION | MB_OK);
                PostQuitMessage(0);
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            OnPaintFrame(hwnd);
            break;

        case WM_COMMAND:
            wmId = LOWORD(wParam);
            switch (wmId)
            {
                case IDOK:
                    if (gHwndButtonInstall)
                        OnButtonInstall();
                    else if (gHwndButtonRunSumatra)
                        OnButtonStartSumatra();
                    else if (gHwndButtonExit)
                        OnButtonExit();
                    break;

                case ID_BUTTON_INSTALL:
                    OnButtonInstall();
                    break;

                case ID_BUTTON_START_SUMATRA:
                    OnButtonStartSumatra();
                    break;

                case ID_BUTTON_EXIT:
                case IDCANCEL:
                    OnButtonExit();
                    break;

                default:
                    return DefWindowProc(hwnd, message, wParam, lParam);
            }
            break;

        case WM_MOUSEMOVE:
            x = GET_X_LPARAM(lParam); y = GET_Y_LPARAM(lParam);
            OnMouseMove(hwnd, x, y);
            break;

        case WM_APP_INSTALLATION_FINISHED:
            OnInstallationFinished();
            if (gHwndButtonRunSumatra)
                SetFocus(gHwndButtonRunSumatra);
            else if (gHwndButtonExit)
                SetFocus(gHwndButtonExit);
            break;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

static BOOL RegisterWinClass(HINSTANCE hInstance)
{
    WNDCLASSEX  wcex;
    ATOM        atom;

    FillWndClassEx(wcex, hInstance);
    wcex.lpszClassName  = INSTALLER_FRAME_CLASS_NAME;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SUMATRAPDF));

    if (gGlobalData.uninstall)
        wcex.lpfnWndProc    = UninstallerWndProcFrame;
    else
        wcex.lpfnWndProc    = InstallerWndProcFrame;

    atom = RegisterClassEx(&wcex);
    if (!atom)
        return FALSE;

    return TRUE;
}

static BOOL InstanceInit(HINSTANCE hInstance, int nCmdShow)
{
    ghinst = hInstance;
    
    gFontDefault = CreateDefaultGuiFont();

    if (gGlobalData.uninstall) {
        gHwndFrame = CreateWindow(
                INSTALLER_FRAME_CLASS_NAME, TAPP _T(" ") CURR_VERSION_STR _T(" Uninstaller"),
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 
                UNINSTALLER_WIN_DX, UNINSTALLER_WIN_DY,
                NULL, NULL,
                ghinst, NULL);
        gMsg = _T("Are you sure that you want to uninstall ") TAPP _T("?");
        gMsgColor = COLOR_MSG_WELCOME;
    } else {
        gHwndFrame = CreateWindow(
                INSTALLER_FRAME_CLASS_NAME, TAPP _T(" ") CURR_VERSION_STR _T(" Installer"),
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT,                
                INSTALLER_WIN_DX, INSTALLER_WIN_DY,
                NULL, NULL,
                ghinst, NULL);
        gMsg = _T("Thank you for downloading ") TAPP _T("!");
        gMsgColor = COLOR_MSG_WELCOME;
    }
    if (!gHwndFrame)
        return FALSE;

    CenterDialog(gHwndFrame);
    ShowWindow(gHwndFrame, SW_SHOW);

    return TRUE;
}

// Try harder getting temporary directory
// Ensures that name ends with \, to make life easier on callers.
// Caller needs to free() the result.
// Returns NULL if fails for any reason.
TCHAR *GetValidTempDir()
{
    TCHAR d[MAX_PATH];
    DWORD res = GetTempPath(dimof(d), d);
    if ((0 == res) || (res >= MAX_PATH)) {
        NotifyFailed(_T("Couldn't obtain temporary directory"));
        return NULL;
    }
    if (!tstr_endswithi(d, _T("\\")))
        tstr_cat_s(d, dimof(d), _T("\\"));
    res = CreateDirectory(d, NULL);
    if ((res == 0) && (ERROR_ALREADY_EXISTS != GetLastError())) {
        SeeLastError();
        NotifyFailed(_T("Couldn't create temporary directory"));
        return NULL;
    }
    return tstr_dup(d);
}

// If this is uninstaller and we're running from installation directory,
// copy uninstaller to temp directory and execute from there, exiting
// ourselves. This is needed so that uninstaller can delete itself
// from installation directory and remove installation directory
// If returns TRUE, this is an installer and we sublaunched ourselves,
// so the caller needs to exit
BOOL ExecuteUninstallerFromTempDir()
{
    BOOL ok = FALSE;

    // only need to sublaunch if running from installation dir
    TCHAR *ownDir = FilePath_GetDir(GetExePath());
    TCHAR *tempDir = GetValidTempDir();

    if (!tempDir)
        goto KeepRunning;

    // not running from the installation directory?
    if (!FilePath_IsSameFile(ownDir, gGlobalData.installDir)) {
        // TODO: use MoveFileEx() to mark this file as 'delete on reboot'
        // with MOVEFILE_DELAY_UNTIL_REBOOT flag?
        goto KeepRunning;
    }

    // already running from temp directory?
    if (FilePath_IsSameFile(ownDir, tempDir))
        goto KeepRunning;

    // Using fixed (unlikely) name instead of GetTempFileName()
    // so that we don't litter temp dir with copies of ourselves
    // Not sure how to ensure that we get deleted after we're done
    TCHAR *tempPath = tstr_cat(tempDir, _T("sum~inst.exe"));

    if (!CopyFile(GetExePath(), tempPath, FALSE)) {
        NotifyFailed(_T("Failed to copy uninstaller to temp directory"));
        free(tempPath);
        goto KeepRunning;
    }

    TCHAR *args = tstr_printf(_T("/u /d \"%s\" %s"), gGlobalData.installDir, gGlobalData.silent ? _T("/s") : _T(""));
    HANDLE h = CreateProcessHelper(tempPath, args);
    ok = h != NULL;
    CloseHandle(h);
    free(args);
    free(tempPath);

KeepRunning:
    free(ownDir);
    free(tempDir);
    return ok;
}

// inspired by http://engineering.imvu.com/2010/11/24/how-to-write-an-interactive-60-hz-desktop-application/
int RunApp()
{
    MSG msg;
    FrameTimeoutCalculator ftc(60);
    for (;;) {
        const DWORD timeout = ftc.GetTimeoutInMilliseconds();
        DWORD res = WAIT_TIMEOUT;
        if (timeout > 0) {
            res = MsgWaitForMultipleObjects(0, 0, TRUE, timeout, QS_ALLEVENTS);
        }
        if (res == WAIT_TIMEOUT) {
            AnimStep();
            ftc.Step();
        }

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return msg.wParam;
            }
            if (!IsDialogMessage(gHwndFrame, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
}

class GdiPlusScope {
protected:
    GdiplusStartupInput si;
    ULONG_PTR           token;
public:
    GdiPlusScope() { GdiplusStartup(&token, &si, NULL); }
    ~GdiPlusScope() { GdiplusShutdown(token); }
};

void ParseCommandLine(TCHAR *cmdLine)
{
    // skip the first arg (exe path)
    TCHAR *arg = tstr_parse_possibly_quoted(&cmdLine);
    free(arg);

#define get_next_arg() tstr_parse_possibly_quoted(&cmdLine)
#define is_arg(param) tstr_ieq(arg + 1, _T(param))

    for (; (arg = get_next_arg()); free(arg)) {
        if ('-' != *arg && '/' != *arg)
            continue;

        if (is_arg("u"))
            gGlobalData.uninstall = true;
        else if (is_arg("s"))
            gGlobalData.silent = true;
        else if (is_arg("d")) {
            if (gGlobalData.installDir)
                free(gGlobalData.installDir);
            gGlobalData.installDir = get_next_arg();
        }
        else if (is_arg("register"))
            gGlobalData.registerAsDefault = true;
    }
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    int ret = 1;

    SetErrorMode(SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS);

    ParseCommandLine(GetCommandLine());
    if (!gGlobalData.uninstall)
        gGlobalData.uninstall = IsUninstaller();
    if (!gGlobalData.installDir)
        gGlobalData.installDir = GetInstallationDir(gGlobalData.uninstall);

#ifndef TEST_UNINSTALLER
    if (gGlobalData.uninstall && ExecuteUninstallerFromTempDir())
        return 0;
#else
    gGlobalData.uninstall = true;
#endif

    ComScope comScope;
    InitAllCommonControls();
    GdiPlusScope gdiScope;

    if (gGlobalData.silent) {
        if (gGlobalData.uninstall)
            UninstallerThread(NULL);
        else
            InstallerThread(NULL);
        ret = gGlobalData.success ? 0 : 1;
        goto Exit;
    }

    if (!RegisterWinClass(hInstance))
        goto Exit;

    if (!InstanceInit(hInstance, nCmdShow))
        goto Exit;

    ret = RunApp();

Exit:
    free(gUnInstMark);
    free(gGlobalData.installDir);
    free(gGlobalData.firstError);
    CoUninitialize();

    return ret;
}
