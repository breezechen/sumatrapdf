/* Copyright 2014 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "BaseUtil.h"
#include "SumatraPDF.h"
#include <malloc.h>
#include <wininet.h>

#include "AppPrefs.h"
#include "AppTools.h"
#include "AppUtil.h"
#include "ChmEngine.h"
#include "CmdLineParser.h"
#include "Controller.h"
#include "CrashHandler.h"
#include "CryptoUtil.h"
#include "DebugLog.h"
#include "DirIter.h"
#include "EbookController.h"
#include "EbookWindow.h"
#include "EngineManager.h"
#include "ExternalPdfViewer.h"
#include "FileHistory.h"
#include "FileModifications.h"
#include "Favorites.h"
#include "FileThumbnails.h"
#include "FileUtil.h"
#include "FileWatcher.h"
using namespace Gdiplus;
#include "GdiPlusUtil.h"
#include "HttpUtil.h"
#include "HtmlWindow.h"
#include "Menu.h"
#include "Mui.h"
#include "Notifications.h"
#include "ParseCommandLine.h"
#include "PdfEngine.h"
#include "PdfSync.h"
#include "Print.h"
#include "PsEngine.h"
#include "RenderCache.h"
#include "resource.h"
#include "Search.h"
#include "Selection.h"
#include "SquareTreeParser.h"
#include "SumatraAbout.h"
#include "SumatraAbout2.h"
#include "SumatraDialogs.h"
#include "SumatraProperties.h"
#include "SumatraWindow.h"
#include "StressTesting.h"
#include "TableOfContents.h"
#include "Tabs.h"
#include "Timer.h"
#include "ThreadUtil.h"
#include "Toolbar.h"
#include "Touch.h"
#include "Translations.h"
#include "uia/Provider.h"
#include "UITask.h"
#include "Version.h"
#include "WindowInfo.h"
#include "WinUtil.h"

/* if true, we're in debug mode where we show links as blue rectangle on
   the screen. Makes debugging code related to links easier. */
#ifdef DEBUG
bool             gDebugShowLinks = true;
#else
bool             gDebugShowLinks = false;
#endif

// if defined, ebook documents are loaded into a WindowInfo instead an EbookWindow
#define DISABLE_EBOOK_WINDOW

/* if true, we're rendering everything with the GDI+ back-end,
   otherwise Fitz/MuPDF is used at least for screen rendering.
   In Debug builds, you can switch between the two through the Debug menu */
bool             gUseGdiRenderer = false;

// in plugin mode, the window's frame isn't drawn and closing and
// fullscreen are disabled, so that SumatraPDF can be displayed
// embedded (e.g. in a web browser)
WCHAR *          gPluginURL = NULL; // owned by CommandLineInfo in WinMain

#define ABOUT_BG_LOGO_COLOR     RGB(0xFF, 0xF2, 0x00)
#define ABOUT_BG_GRAY_COLOR     RGB(0xCC, 0xCC, 0xCC)

// Background color comparison:
// Adobe Reader X   0x565656 without any frame border
// Foxit Reader 5   0x9C9C9C with a pronounced frame shadow
// PDF-XChange      0xACA899 with a 1px frame and a gradient shadow
// Google Chrome    0xCCCCCC with a symmetric gradient shadow
// Evince           0xD7D1CB with a pronounced frame shadow
#ifdef DRAW_PAGE_SHADOWS
// SumatraPDF (old) 0xCCCCCC with a pronounced frame shadow
#define COL_WINDOW_BG           RGB(0xCC, 0xCC, 0xCC)
#define COL_PAGE_FRAME          RGB(0x88, 0x88, 0x88)
#define COL_PAGE_SHADOW         RGB(0x40, 0x40, 0x40)
#else
// SumatraPDF       0x999999 without any frame border
#define COL_WINDOW_BG           RGB(0x99, 0x99, 0x99)
#endif

#define CANVAS_CLASS_NAME            L"SUMATRA_PDF_CANVAS"
#define SIDEBAR_SPLITTER_CLASS_NAME  L"SidebarSplitter"
#define FAV_SPLITTER_CLASS_NAME      L"FavSplitter"
#define RESTRICTIONS_FILE_NAME       L"sumatrapdfrestrict.ini"
#define CRASH_DUMP_FILE_NAME         L"sumatrapdfcrash.dmp"

#define DEFAULT_LINK_PROTOCOLS       L"http,https,mailto"
#define DEFAULT_FILE_PERCEIVED_TYPES L"audio,video"

#define SPLITTER_DX         5
#define SIDEBAR_MIN_WIDTH   150

#define SPLITTER_DY         4
#define TOC_MIN_DY          100

// minimum size of the window
#define MIN_WIN_DX 480
#define MIN_WIN_DY 320

#define REPAINT_TIMER_ID            1
#define REPAINT_MESSAGE_DELAY_IN_MS 1000

#define HIDE_CURSOR_TIMER_ID        3
#define HIDE_CURSOR_DELAY_IN_MS     3000

#define AUTO_RELOAD_TIMER_ID        5
#define AUTO_RELOAD_DELAY_IN_MS     100

// TODO: same as LAYOUT_TIMER_ID in EbookWindow.cpp
#define EBOOK_LAYOUT_TIMER_ID       7

HINSTANCE                    ghinst = NULL;

HCURSOR                      gCursorArrow;
HCURSOR                      gCursorHand;
HCURSOR                      gCursorIBeam;
HFONT                        gDefaultGuiFont;

// TODO: combine into Vec<SumatraWindow> (after 2.0) ?
Vec<WindowInfo*>             gWindows;
Vec<EbookWindow*>            gEbookWindows;
FileHistory                  gFileHistory;
Favorites                    gFavorites;

static HCURSOR                      gCursorDrag;
static HCURSOR                      gCursorScroll;
static HCURSOR                      gCursorSizeWE;
static HCURSOR                      gCursorSizeNS;
static HCURSOR                      gCursorNo;
static HBITMAP                      gBitmapReloadingCue;
static RenderCache                  gRenderCache;
static bool                         gCrashOnOpen = false;

// in restricted mode, some features can be disabled (such as
// opening files, printing, following URLs), so that SumatraPDF
// can be used as a PDF reader on locked down systems
static int                          gPolicyRestrictions = Perm_All;
// only the listed protocols will be passed to the OS for
// opening in e.g. a browser or an email client (ignored,
// if gPolicyRestrictions doesn't contain Perm_DiskAccess)
static WStrVec                      gAllowedLinkProtocols;
// only files of the listed perceived types will be opened
// externally by LinkHandler::LaunchFile (i.e. when clicking
// on an in-document link); examples: "audio", "video", ...
static WStrVec                      gAllowedFileTypes;

static void UpdateUITextForLanguage();
static void UpdateToolbarAndScrollbarState(WindowInfo& win);
// in Canvas.cpp
static LRESULT CALLBACK WndProcCanvas(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool HasPermission(int permission)
{
    return (permission & gPolicyRestrictions) == permission;
}

static void SetCurrentLang(const char *langCode)
{
    if (langCode) {
        if (langCode != gGlobalPrefs->uiLanguage)
            str::ReplacePtr(&gGlobalPrefs->uiLanguage, langCode);
        trans::SetCurrentLangByCode(langCode);
    }
}

#ifndef SUMATRA_UPDATE_INFO_URL
#ifdef SVN_PRE_RELEASE_VER
#define SUMATRA_UPDATE_INFO_URL L"http://kjkpub.s3.amazonaws.com/sumatrapdf/sumpdf-prerelease-update.txt"
#else
#define SUMATRA_UPDATE_INFO_URL L"http://kjkpub.s3.amazonaws.com/sumatrapdf/sumpdf-update.txt"
#endif
#endif

#ifndef SVN_UPDATE_LINK
#ifdef SVN_PRE_RELEASE_VER
#define SVN_UPDATE_LINK         L"http://blog.kowalczyk.info/software/sumatrapdf/prerelease.html"
#else
#define SVN_UPDATE_LINK         L"http://blog.kowalczyk.info/software/sumatrapdf/download-free-pdf-viewer.html"
#endif
#endif

#define SECS_IN_DAY 60*60*24

// lets the shell open a URI for any supported scheme in
// the appropriate application (web browser, mail client, etc.)
bool LaunchBrowser(const WCHAR *url)
{
    if (gPluginMode) {
        // pass the URI back to the browser
        CrashIf(gWindows.Count() == 0);
        if (gWindows.Count() == 0)
            return false;
        HWND plugin = gWindows.At(0)->hwndFrame;
        HWND parent = GetAncestor(plugin, GA_PARENT);
        ScopedMem<char> urlUtf8(str::conv::ToUtf8(url));
        if (!parent || !urlUtf8 || str::Len(urlUtf8) > 4096)
            return false;
        COPYDATASTRUCT cds = { 0x4C5255 /* URL */, (DWORD)str::Len(urlUtf8) + 1, urlUtf8.Get() };
        return SendMessage(parent, WM_COPYDATA, (WPARAM)plugin, (LPARAM)&cds);
    }

    if (!HasPermission(Perm_DiskAccess))
        return false;

    // check if this URL's protocol is allowed
    ScopedMem<WCHAR> protocol;
    if (!str::Parse(url, L"%S:", &protocol))
        return false;
    str::ToLower(protocol);
    if (!gAllowedLinkProtocols.Contains(protocol))
        return false;

    return LaunchFile(url, NULL, L"open");
}

// lets the shell open a file of any supported perceived type
// in the default application for opening such files
bool OpenFileExternally(const WCHAR *path)
{
    if (!HasPermission(Perm_DiskAccess) || gPluginMode)
        return false;

    // check if this file's perceived type is allowed
    const WCHAR *ext = path::GetExt(path);
    ScopedMem<WCHAR> perceivedType(ReadRegStr(HKEY_CLASSES_ROOT, ext, L"PerceivedType"));
    // since we allow following hyperlinks, also allow opening local webpages
    if (str::EndsWithI(path, L".htm") || str::EndsWithI(path, L".html") || str::EndsWithI(path, L".xhtml"))
        perceivedType.Set(str::Dup(L"webpage"));
    if (str::IsEmpty(perceivedType.Get()))
        return false;
    str::ToLower(perceivedType);
    if (!gAllowedFileTypes.Contains(perceivedType) && !gAllowedFileTypes.Contains(L"*"))
        return false;

    // TODO: only do this for trusted files (cf. IsUntrustedFile)?
    return LaunchFile(path);
}

#define DEFINE_GUID_STATIC(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    static const GUID name = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }
DEFINE_GUID_STATIC(CLSID_SendMail, 0x9E56BE60, 0xC50F, 0x11CF, 0x9A, 0x2C, 0x00, 0xA0, 0xC9, 0x0A, 0x90, 0xCE);

bool CanSendAsEmailAttachment(WindowInfo *win)
{
    // Requirements: a valid filename and access to SendMail's IDropTarget interface
    if (!CanViewExternally(win))
        return false;

    ScopedComPtr<IDropTarget> pDropTarget;
    return pDropTarget.Create(CLSID_SendMail);
}

static bool SendAsEmailAttachment(WindowInfo *win)
{
    if (!CanSendAsEmailAttachment(win))
        return false;

    // We use the SendTo drop target provided by SendMail.dll, which should ship with all
    // commonly used Windows versions, instead of MAPISendMail, which doesn't support
    // Unicode paths and might not be set up on systems not having Microsoft Outlook installed.
    ScopedComPtr<IDataObject> pDataObject(GetDataObjectForFile(win->loadedFilePath, win->hwndFrame));
    if (!pDataObject)
        return false;

    ScopedComPtr<IDropTarget> pDropTarget;
    if (!pDropTarget.Create(CLSID_SendMail))
        return false;

    POINTL pt = { 0, 0 };
    DWORD dwEffect = 0;
    pDropTarget->DragEnter(pDataObject, MK_LBUTTON, pt, &dwEffect);
    HRESULT hr = pDropTarget->Drop(pDataObject, MK_LBUTTON, pt, &dwEffect);
    return SUCCEEDED(hr);
}

void SwitchToDisplayMode(WindowInfo *win, DisplayMode displayMode, bool keepContinuous)
{
    CrashIf(!win->IsDocLoaded());
    if (!win->IsDocLoaded()) return;

    win->ctrl->SetDisplayMode(displayMode, keepContinuous);
    UpdateToolbarState(win);
}

WindowInfo *FindWindowInfoByHwnd(HWND hwnd)
{
    HWND parent = GetParent(hwnd);
    for (size_t i = 0; i < gWindows.Count(); i++) {
        WindowInfo *win = gWindows.At(i);
        if (hwnd == win->hwndFrame)
            return win;
        if (!parent)
            continue;
        if (// canvas, toolbar, rebar, tocbox, splitters
            parent == win->hwndFrame    ||
            // infotips, message windows
            parent == win->hwndCanvas   ||
            // page and find labels and boxes
            parent == win->hwndToolbar  ||
            // ToC tree, sidebar title and close button
            parent == win->hwndTocBox   ||
            // Favorites tree, title, and close button
            parent == win->hwndFavBox   ||
            // tab bar
            parent == win->hwndTabBar) {
            return win;
        }
    }
    return NULL;
}

bool WindowInfoStillValid(WindowInfo *win)
{
    return gWindows.Contains(win);
}

// Find the first window showing a given PDF file
WindowInfo* FindWindowInfoByFile(const WCHAR *file)
{
    ScopedMem<WCHAR> normFile(path::Normalize(file));

    for (size_t i = 0; i < gWindows.Count(); i++) {
        WindowInfo *win = gWindows.At(i);
        if (!win->IsAboutWindow() && path::IsSame(win->loadedFilePath, normFile))
            return win;
    }

    return NULL;
}

// Find the first window that has been produced from <file>
WindowInfo* FindWindowInfoBySyncFile(const WCHAR *file)
{
    for (size_t i = 0; i < gWindows.Count(); i++) {
        WindowInfo *win = gWindows.At(i);
        Vec<RectI> rects;
        UINT page;
        if (win->pdfsync && win->pdfsync->SourceToDoc(file, 0, 0, &page, rects) != PDFSYNCERR_UNKNOWN_SOURCEFILE)
            return win;
    }
    return NULL;
}

class HwndPasswordUI : public PasswordUI
{
    HWND hwnd;
    WStrVec defaults;

public:
    explicit HwndPasswordUI(HWND hwnd) : hwnd(hwnd) {
        ParseCmdLine(gGlobalPrefs->defaultPasswords, defaults);
    }

    virtual WCHAR * GetPassword(const WCHAR *fileName, unsigned char *fileDigest,
                                unsigned char decryptionKeyOut[32], bool *saveKey);
};

/* Get password for a given 'fileName', can be NULL if user cancelled the
   dialog box or if the encryption key has been filled in instead.
   Caller needs to free() the result. */
WCHAR *HwndPasswordUI::GetPassword(const WCHAR *fileName, unsigned char *fileDigest,
                                   unsigned char decryptionKeyOut[32], bool *saveKey)
{
    DisplayState *fileFromHistory = gFileHistory.Find(fileName);
    if (fileFromHistory && fileFromHistory->decryptionKey) {
        ScopedMem<char> fingerprint(str::MemToHex(fileDigest, 16));
        *saveKey = str::StartsWith(fileFromHistory->decryptionKey, fingerprint.Get());
        if (*saveKey && str::HexToMem(fileFromHistory->decryptionKey + 32, decryptionKeyOut, 32))
            return NULL;
    }

    *saveKey = false;

    // try the list of default passwords before asking the user
    if (defaults.Count() > 0)
        return defaults.Pop();

    if (IsStressTesting())
        return NULL;

    // extract the filename from the URL in plugin mode instead
    // of using the more confusing temporary filename
    ScopedMem<WCHAR> urlName;
    if (gPluginMode) {
        urlName.Set(ExtractFilenameFromURL(gPluginURL));
        if (urlName)
            fileName = urlName;
    }

    fileName = path::GetBaseName(fileName);
    // check if the window is still valid as it might have been closed by now
    if (!IsWindow(hwnd))
        hwnd = GetForegroundWindow();
    bool *rememberPwd = gGlobalPrefs->rememberOpenedFiles ? saveKey : NULL;
    return Dialog_GetPassword(hwnd, fileName, rememberPwd);
}

// update global windowState for next default launch when either
// no pdf is opened or a document without window dimension information
static void RememberDefaultWindowPosition(WindowInfo& win)
{
    if (win.presentation)
        gGlobalPrefs->windowState = win.windowStateBeforePresentation;
    else if (win.isFullScreen)
        gGlobalPrefs->windowState = WIN_STATE_FULLSCREEN;
    else if (IsZoomed(win.hwndFrame))
        gGlobalPrefs->windowState = WIN_STATE_MAXIMIZED;
    else if (!IsIconic(win.hwndFrame))
        gGlobalPrefs->windowState = WIN_STATE_NORMAL;

    gGlobalPrefs->sidebarDx = WindowRect(win.hwndTocBox).dx;

    /* don't update the window's dimensions if it is maximized, mimimized or fullscreened */
    if (WIN_STATE_NORMAL == gGlobalPrefs->windowState &&
        !IsIconic(win.hwndFrame) && !win.presentation) {
        // TODO: Use Get/SetWindowPlacement (otherwise we'd have to separately track
        //       the non-maximized dimensions for proper restoration)
        gGlobalPrefs->windowPos = WindowRect(win.hwndFrame);
    }
}

// update global windowState for next default launch when either
// no pdf is opened or a document without window dimension information
static void RememberDefaultWindowPosition(EbookWindow *win)
{
    if (IsZoomed(win->hwndFrame))
        gGlobalPrefs->windowState = WIN_STATE_MAXIMIZED;
    else if (!IsIconic(win->hwndFrame))
        gGlobalPrefs->windowState = WIN_STATE_NORMAL;

    // don't touch gGlobalPrefs->sidebarDx as it's only relevant to non-mobi windows

    /* don't update the window's dimensions if it is maximized, mimimized or fullscreened */
    if (WIN_STATE_NORMAL == gGlobalPrefs->windowState && !IsIconic(win->hwndFrame)) {
        // TODO: Use Get/SetWindowPlacement (otherwise we'd have to separately track
        //       the non-maximized dimensions for proper restoration)
        gGlobalPrefs->windowPos = WindowRect(win->hwndFrame);
    }
}

static void UpdateDisplayStateWindowRect(WindowInfo& win, DisplayState& ds, bool updateGlobal=true)
{
    if (updateGlobal)
        RememberDefaultWindowPosition(win);

    ds.windowState = gGlobalPrefs->windowState;
    ds.windowPos   = gGlobalPrefs->windowPos;
    ds.sidebarDx   = gGlobalPrefs->sidebarDx;
}

static void UpdateSidebarDisplayState(WindowInfo *win, DisplayState *ds)
{
    ds->showToc = win->tocVisible;

    if (win->tocLoaded) {
        win->tocState.Reset();
        HTREEITEM hRoot = TreeView_GetRoot(win->hwndTocTree);
        if (hRoot)
            UpdateTocExpansionState(win, hRoot);
    }

    *ds->tocState = win->tocState;
}

static void UpdateSidebarDisplayState(EbookWindow *win, DisplayState *ds)
{
    ds->showToc = false;
    ds->tocState->Reset();
}

static void DisplayStateFromEbookWindow(EbookWindow* win, DisplayState* ds)
{
    if (!ds->filePath || !str::EqI(ds->filePath, win->LoadedFilePath()))
        str::ReplacePtr(&ds->filePath, win->LoadedFilePath());

    // don't modify any of the other DisplayState values
    // as long as they're not used, so that the same
    // DisplayState settings can also be used for MobiEngine
    // (in debug builds and in case we ever want to allow to
    // switch between the interfaces); we get reasonable
    // defaults from DisplayState's constructor anyway
    ds->reparseIdx = win->ebookController->CurrPageReparseIdx();
    if (win->ebookController->IsSinglePage())
        str::ReplacePtr(&ds->displayMode, prefs::conv::FromDisplayMode(DM_SINGLE_PAGE));
    else
        str::ReplacePtr(&ds->displayMode, prefs::conv::FromDisplayMode(DM_FACING));
}

static void UpdateCurrentFileDisplayStateForWinMobi(EbookWindow* win)
{
    RememberDefaultWindowPosition(win);
    DisplayState *ds = gFileHistory.Find(win->LoadedFilePath());
    if (!ds)
        return;
    DisplayStateFromEbookWindow(win, ds);
    ds->useDefaultState = !gGlobalPrefs->rememberStatePerDocument;
    ds->windowState = gGlobalPrefs->windowState;
    ds->windowPos   = gGlobalPrefs->windowPos;
    UpdateSidebarDisplayState(win, ds);
}

static void UpdateCurrentFileDisplayStateForWinInfo(WindowInfo* win)
{
    RememberDefaultWindowPosition(*win);
    if (!win->IsDocLoaded())
        return;
    DisplayState *ds = gFileHistory.Find(win->loadedFilePath);
    if (!ds)
        return;
    win->ctrl->UpdateDisplayState(ds);
    UpdateDisplayStateWindowRect(*win, *ds, false);
    UpdateSidebarDisplayState(win, ds);
}

void UpdateCurrentFileDisplayStateForWin(const SumatraWindow& win)
{
    if (win.AsWindowInfo())
        UpdateCurrentFileDisplayStateForWinInfo(win.AsWindowInfo());
    else if (win.AsEbookWindow())
        UpdateCurrentFileDisplayStateForWinMobi(win.AsEbookWindow());
}

bool IsUIRightToLeft()
{
    return trans::IsCurrLangRtl();
}

UINT MbRtlReadingMaybe()
{
    if (IsUIRightToLeft())
        return MB_RTLREADING;
    return 0;
}

void MessageBoxWarning(HWND hwnd, const WCHAR *msg, const WCHAR *title)
{
    UINT type =  MB_OK | MB_ICONEXCLAMATION | MbRtlReadingMaybe();
    if (!title)
        title = _TR("Warning");
    MessageBox(hwnd, msg, title, type);
}

// updates the layout for a window to either left-to-right or right-to-left
// depending on the currently used language (cf. IsUIRightToLeft)
static void UpdateWindowRtlLayout(WindowInfo *win)
{
    bool isRTL = IsUIRightToLeft();
    bool wasRTL = (GetWindowLong(win->hwndFrame, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    if (wasRTL == isRTL)
        return;

    bool tocVisible = win->tocVisible;
    if (tocVisible || gGlobalPrefs->showFavorites)
        SetSidebarVisibility(win, false, false);

    // cf. http://www.microsoft.com/middleeast/msdn/mirror.aspx
    ToggleWindowStyle(win->hwndFrame, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);

    ToggleWindowStyle(win->hwndTocBox, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);
    HWND tocBoxTitle = GetDlgItem(win->hwndTocBox, IDC_TOC_TITLE);
    ToggleWindowStyle(tocBoxTitle, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);

    ToggleWindowStyle(win->hwndFavBox, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);
    HWND favBoxTitle = GetDlgItem(win->hwndFavBox, IDC_FAV_TITLE);
    ToggleWindowStyle(favBoxTitle, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);
    ToggleWindowStyle(win->hwndFavTree, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);

    ToggleWindowStyle(win->hwndReBar, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);
    ToggleWindowStyle(win->hwndToolbar, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);
    ToggleWindowStyle(win->hwndFindBox, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);
    ToggleWindowStyle(win->hwndFindText, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);
    ToggleWindowStyle(win->hwndPageText, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRTL, GWL_EXSTYLE);

    win->notifications->Relayout();

    // TODO: also update the canvas scrollbars (?)

    // ensure that the ToC sidebar is on the correct side and that its
    // title and close button are also correctly laid out
    if (tocVisible || gGlobalPrefs->showFavorites) {
        SetSidebarVisibility(win, tocVisible, gGlobalPrefs->showFavorites);
        if (tocVisible)
            SendMessage(win->hwndTocBox, WM_SIZE, 0, 0);
        if (gGlobalPrefs->showFavorites)
            SendMessage(win->hwndFavBox, WM_SIZE, 0, 0);
    }
}

void UpdateRtlLayoutForAllWindows()
{
    for (size_t i = 0; i < gWindows.Count(); i++) {
        UpdateWindowRtlLayout(gWindows.At(i));
    }
}

static int GetPolicies(bool isRestricted)
{
    static struct {
        const char *name;
        int perm;
    } policies[] = {
        { "InternetAccess", Perm_InternetAccess },
        { "DiskAccess",     Perm_DiskAccess },
        { "SavePreferences",Perm_SavePreferences },
        { "RegistryAccess", Perm_RegistryAccess },
        { "PrinterAccess",  Perm_PrinterAccess },
        { "CopySelection",  Perm_CopySelection },
        { "FullscreenAccess",Perm_FullscreenAccess },
    };

    gAllowedLinkProtocols.Reset();
    gAllowedFileTypes.Reset();

    // allow to restrict SumatraPDF's functionality from an INI file in the
    // same directory as SumatraPDF.exe (cf. ../docs/sumatrapdfrestrict.ini)
    ScopedMem<WCHAR> restrictPath(GetExePath());
    restrictPath.Set(path::GetDir(restrictPath));
    restrictPath.Set(path::Join(restrictPath, RESTRICTIONS_FILE_NAME));
    if (file::Exists(restrictPath)) {
        ScopedMem<char> restrictData(file::ReadAll(restrictPath, NULL));
        SquareTree sqt(restrictData);
        SquareTreeNode *polsec = sqt.root ? sqt.root->GetChild("Policies") : NULL;
        if (!polsec)
            return Perm_RestrictedUse;

        int policy = Perm_RestrictedUse;
        for (size_t i = 0; i < dimof(policies); i++) {
            const char *value = polsec->GetValue(policies[i].name);
            if (value && atoi(value) != 0)
                policy = policy | policies[i].perm;
        }
        // determine the list of allowed link protocols and perceived file types
        if ((policy & Perm_DiskAccess)) {
            const char *value;
            if ((value = polsec->GetValue("LinkProtocols")) != NULL) {
                ScopedMem<WCHAR> protocols(str::conv::FromUtf8(value));
                str::ToLower(protocols);
                str::TransChars(protocols, L":; ", L",,,");
                gAllowedLinkProtocols.Split(protocols, L",", true);
            }
            if ((value = polsec->GetValue("SafeFileTypes")) != NULL) {
                ScopedMem<WCHAR> protocols(str::conv::FromUtf8(value));
                str::ToLower(protocols);
                str::TransChars(protocols, L":; ", L",,,");
                gAllowedFileTypes.Split(protocols, L",", true);
            }
        }

        return policy;
    }

    if (isRestricted)
        return Perm_RestrictedUse;

    gAllowedLinkProtocols.Split(DEFAULT_LINK_PROTOCOLS, L",");
    gAllowedFileTypes.Split(DEFAULT_FILE_PERCEIVED_TYPES, L",");
    return Perm_All;
}

bool ShouldSaveThumbnail(DisplayState& ds)
{
    // don't create thumbnails if we won't be needing them at all
    if (!HasPermission(Perm_SavePreferences))
        return false;

    // don't create thumbnails for files that won't need them anytime soon
    Vec<DisplayState *> list;
    gFileHistory.GetFrequencyOrder(list);
    int idx = list.Find(&ds);
    if (idx < 0 || FILE_HISTORY_MAX_FREQUENT * 2 <= idx)
        return false;

    if (HasThumbnail(ds))
        return false;
    return true;
}

void SaveThumbnailForFile(const WCHAR *filePath, RenderedBitmap *bmp)
{
    SetThumbnail(gFileHistory.Find(filePath), bmp);
}

class ThumbnailRenderingTask : public UITask, public RenderingCallback
{
    ScopedMem<WCHAR> filePath;
    RenderedBitmap *bmp;

public:
    explicit ThumbnailRenderingTask(const WCHAR *filePath) :
        filePath(str::Dup(filePath)), bmp(NULL) {
    }

    ~ThumbnailRenderingTask() {
        delete bmp;
    }

    virtual void Callback(RenderedBitmap *bmp) {
        this->bmp = bmp;
        uitask::Post(this);
    }

    virtual void Execute() {
        SaveThumbnailForFile(filePath, bmp);
        bmp = NULL;
    }
};

void RenderThumbnail(DisplayModel *dm, float zoom, RectD pageRect)
{
    RenderingCallback *callback = new ThumbnailRenderingTask(dm->engine->FileName());
    gRenderCache.Render(dm, 1, 0, zoom, pageRect, *callback);
}

static void CreateThumbnailForFile(WindowInfo& win, DisplayState& ds)
{
    if (!ShouldSaveThumbnail(ds))
        return;

    AssertCrash(win.IsDocLoaded());
    if (!win.IsDocLoaded()) return;

    win.ctrl->CreateThumbnail(&ds);
}

static void RebuildMenuBarForWindow(WindowInfo *win)
{
    HMENU oldMenu = win->menu;
    win->menu = BuildMenu(win);
    if (!win->presentation && !win->isFullScreen && !win->isMenuHidden)
        SetMenu(win->hwndFrame, win->menu);
    DestroyMenu(oldMenu);
}

static void RebuildMenuBarForAllWindows()
{
    for (size_t i = 0; i < gWindows.Count(); i++) {
        RebuildMenuBarForWindow(gWindows.At(i));
    }
    RebuildMenuBarForEbookWindows();
}

// meaning of the internal values of LoadArgs:
// isNewWindow : if true then 'win' refers to a newly created window that needs
//   to be resized and placed
// allowFailure : if false then keep displaying the previously loaded document
//   if the new one is broken
// placeWindow : if true then the Window will be moved/sized according
//   to the 'state' information even if the window was already placed
//   before (isNewWindow=false)
static bool LoadDocIntoWindow(LoadArgs& args, PasswordUI *pwdUI, DisplayState *state=NULL)
{
    ScopedMem<WCHAR> title;
    WindowInfo *win = args.win;

    float zoomVirtual = gGlobalPrefs->defaultZoomFloat;
    int rotation = 0;

    // Never load settings from a preexisting state if the user doesn't wish to
    // (unless we're just refreshing the document, i.e. only if args.placeWindow == true)
    if (args.placeWindow && (!gGlobalPrefs->rememberStatePerDocument || state && state->useDefaultState)) {
        state = NULL;
    } else if (NULL == state) {
        state = gFileHistory.Find(args.fileName);
        if (state) {
            if (state->windowPos.IsEmpty())
                state->windowPos = gGlobalPrefs->windowPos;
            EnsureAreaVisibility(state->windowPos);
        }
    }
    DisplayMode displayMode = gGlobalPrefs->defaultDisplayModeEnum;
    int startPage = 1;
    ScrollState ss(1, -1, -1);
    bool showAsFullScreen = WIN_STATE_FULLSCREEN == gGlobalPrefs->windowState;
    int showType = SW_NORMAL;
    if (gGlobalPrefs->windowState == WIN_STATE_MAXIMIZED || showAsFullScreen)
        showType = SW_MAXIMIZE;

    bool showToc = gGlobalPrefs->showToc;
    if (state) {
        startPage = state->pageNo;
        displayMode = prefs::conv::ToDisplayMode(state->displayMode);
        showAsFullScreen = WIN_STATE_FULLSCREEN == state->windowState;
        if (state->windowState == WIN_STATE_NORMAL)
            showType = SW_NORMAL;
        else if (state->windowState == WIN_STATE_MAXIMIZED || showAsFullScreen)
            showType = SW_MAXIMIZE;
        else if (state->windowState == WIN_STATE_MINIMIZED)
            showType = SW_MINIMIZE;
        showToc = state->showToc;
    }

    Controller *prevCtrl = win->ctrl;
    AbortFinding(args.win);
    delete win->pdfsync;
    win->pdfsync = NULL;

    str::ReplacePtr(&win->loadedFilePath, args.fileName);
    EngineType engineType;
    BaseEngine *engine = EngineManager::CreateEngine(args.fileName, pwdUI, &engineType,
                                                     gGlobalPrefs->chmUI.useFixedPageUI,
                                                     gGlobalPrefs->ebookUI.useFixedPageUI);

    if (engine && Engine_Chm == engineType) {
        // make sure that MSHTML can't be used as a potential exploit
        // vector through another browser and our plugin (which doesn't
        // advertise itself for Chm documents but could be tricked into
        // loading one nonetheless); note: this crash should never happen,
        // since gGlobalPrefs->chmUI.useFixedPageUI is set in SetupPluginMode
        CrashAlwaysIf(gPluginMode);
        // if CLSID_WebBrowser isn't available, fall back on Chm2Engine
        if (!static_cast<ChmEngine *>(engine)->SetParentHwnd(win->hwndCanvas)) {
            delete engine;
            engine = EngineManager::CreateEngine(args.fileName, pwdUI, &engineType, true);
            CrashIf(engineType != (engine ? Engine_Chm2 : Engine_None));
        }
    }

    DisplayModel *dm = NULL;
    if (!engine && Doc::IsSupportedFile(args.fileName)) {
        Doc doc = Doc::CreateFromFile(args.fileName);
        win->ctrl = !doc.IsNone() ? EbookUIController::Create(win->hwndCanvas) : NULL;
        if (win->ctrl) {
            EbookController *ectrl = new EbookController(win->AsEbook()->ctrls(), displayMode);
            win->AsEbook()->SetController(ectrl);
            ectrl->SetDoc(doc, state ? state->reparseIdx : 0);
            CrashIf(!win->ctrl || !win->AsEbook() || win->AsFixed() || win->AsChm());
        }
    }
    else if (!engine) {
        win->ctrl = NULL;
    }
    else if (Engine_Chm == engineType) {
        win->ctrl = ChmUIController::Create(static_cast<ChmEngine *>(engine), win);
        CrashIf(!win->ctrl || !win->AsChm() || win->AsFixed() || win->AsEbook());
    }
    else {
        dm = new DisplayModel(engine, win);
        win->ctrl = FixedPageUIController::Create(dm, win->linkHandler);
        CrashIf(!win->ctrl || !win->AsFixed() || win->AsChm() || win->AsEbook());
        win->ctrl->AsFixed()->engineType = engineType;
    }

    bool needRefresh = !win->ctrl;

    // ToC items might hold a reference to an Engine, so make sure to
    // delete them before destroying the whole DisplayModel
    // (same for linkOnLastButtonDown)
    if (win->ctrl || args.allowFailure) {
        ClearTocBox(win);
        delete win->linkOnLastButtonDown;
        win->linkOnLastButtonDown = NULL;
    }

    AssertCrash(!win->IsAboutWindow() && win->IsDocLoaded() == (win->ctrl != NULL));
    /* see http://code.google.com/p/sumatrapdf/issues/detail?id=1570
    if (!win->ctrl) {
        // TODO: this should be "Error opening %s". Change after 1.7 is released
        ScopedMem<WCHAR> msg(str::Format(_TR("Error loading %s"), win->loadedFilePath));
        ShowNotification(win, msg, true, false, NG_RESPONSE_TO_ACTION);
        // TODO: CloseWindow() does slightly more than this
        //       (also, some code presumes that there is at least one window with
        //        IsAboutWindow() == true and that that window is gWindows.At(0))
        str::ReplacePtr(&win->loadedFilePath, NULL);
    }
    */
    if (win->ctrl) {
        if (dm) {
            dm->SetInitialViewSettings(displayMode, startPage, win->GetViewPortSize(),
                                       gGlobalPrefs->customScreenDPI > 0 ? gGlobalPrefs->customScreenDPI : win->dpi);
            // TODO: also expose Manga Mode for image folders?
            if (engineType == Engine_ComicBook || engineType == Engine_ImageDir)
                dm->SetDisplayR2L(state ? state->displayR2L : gGlobalPrefs->comicBookUI.cbxMangaMode);
            if (prevCtrl && prevCtrl->AsFixed() && str::Eq(win->ctrl->FilePath(), prevCtrl->FilePath())) {
                gRenderCache.KeepForDisplayModel(prevCtrl->AsFixed()->model(), dm);
                dm->CopyNavHistory(*prevCtrl->AsFixed()->model());
            }

            // tell UI Automation about content change
            if (win->uia_provider)
                win->uia_provider->OnDocumentLoad(dm);
        }
        else if (win->IsChm()) {
            win->ctrl->SetDisplayMode(displayMode);
            win->ctrl->GoToPage(startPage, false);
        }
        else
            CrashIf(!win->IsEbookLoaded());
        delete prevCtrl;
    } else if (args.allowFailure) {
        delete prevCtrl;
        const WCHAR *titlePath = gGlobalPrefs->fullPathInTitle ? args.fileName : path::GetBaseName(args.fileName);
        ScopedMem<WCHAR> title2(str::Format(L"%s - %s", titlePath, SUMATRA_WINDOW_TITLE));
        win::SetText(win->hwndFrame, title2);
        goto Error;
    } else {
        // if there is an error while reading the document and a repair is not requested
        // then fallback to the previous state
        win->ctrl = prevCtrl;
    }

    if (win->ctrl != prevCtrl && win->IsFixedDocLoaded()) {
        win->AsFixed()->userAnnots = LoadFileModifications(args.fileName);
        win->AsFixed()->userAnnotsModified = false;
        win->AsFixed()->engine()->UpdateUserAnnotations(win->AsFixed()->userAnnots);
    }

    if (state) {
        zoomVirtual = prefs::conv::ToZoom(state->zoom);
        if (win->ctrl->ValidPageNo(startPage)) {
            ss.page = startPage;
            if (ZOOM_FIT_CONTENT != zoomVirtual) {
                ss.x = state->scrollPos.x;
                ss.y = state->scrollPos.y;
            }
            // else let win.dm->Relayout() scroll to fit the page (again)
        } else if (startPage > win->ctrl->PageCount()) {
            ss.page = win->ctrl->PageCount();
        }
        rotation = state->rotation;
        win->tocState = *state->tocState;
    }

    if (dm)
        dm->Relayout(zoomVirtual, rotation);
    else
        win->ctrl->SetZoomVirtual(zoomVirtual);

    if (!args.isNewWindow) {
        win->RedrawAll();
        OnMenuFindMatchCase(win);
    }
    UpdateFindbox(win);

    // menu for chm and ebook docs is different, so we have to re-create it
    RebuildMenuBarForWindow(win);
    // the toolbar isn't supported for ebook docs (yet)
    ShowOrHideToolbarForWindow(win);

    int pageCount = win->ctrl->PageCount();
    if (pageCount > 0) {
        UpdateToolbarPageText(win, pageCount);
        UpdateToolbarFindText(win);
    }

    const WCHAR *titlePath = gGlobalPrefs->fullPathInTitle ? win->ctrl->FilePath() : path::GetBaseName(win->ctrl->FilePath());
    WCHAR *docTitle = win->ctrl->GetProperty(Prop_Title);
    if (docTitle) {
        str::NormalizeWS(docTitle);
        if (!str::IsEmpty(docTitle)) {
            ScopedMem<WCHAR> docTitleBit(str::Format(L"- [%s] ", docTitle));
            free(docTitle);
            docTitle = docTitleBit.StealData();
        }
    }
    title.Set(str::Format(L"%s %s- %s", titlePath, docTitle ? docTitle : L"", SUMATRA_WINDOW_TITLE));
    if (IsUIRightToLeft()) {
        // explicitly revert the title, so that filenames aren't garbled
        title.Set(str::Format(L"%s %s- %s", SUMATRA_WINDOW_TITLE, docTitle ? docTitle : L"", titlePath));
    }
    free(docTitle);
    if (needRefresh)
        title.Set(str::Format(_TR("[Changes detected; refreshing] %s"), title));
    win::SetText(win->hwndFrame, title);

    if (HasPermission(Perm_DiskAccess) && Engine_PDF == engineType) {
        CrashIf(!dm);
        int res = Synchronizer::Create(args.fileName,
            static_cast<PdfEngine *>(dm->engine), &win->pdfsync);
        // expose SyncTeX in the UI
        if (PDFSYNCERR_SUCCESS == res)
            gGlobalPrefs->enableTeXEnhancements = true;
    }

Error:
    if (args.isNewWindow || args.placeWindow && state) {
        if (args.isNewWindow && state && !state->windowPos.IsEmpty()) {
            // Make sure it doesn't have a position like outside of the screen etc.
            RectI rect = ShiftRectToWorkArea(state->windowPos);
            // This shouldn't happen until !win.IsAboutWindow(), so that we don't
            // accidentally update gGlobalState with this window's dimensions
            MoveWindow(win->hwndFrame, rect);
        }

        if (args.showWin)
            ShowWindow(win->hwndFrame, showType);

        UpdateWindow(win->hwndFrame);
    }
    if (win->IsDocLoaded()) {
        ToggleWindowStyle(win->hwndPageBox, ES_NUMBER, !win->ctrl->HasPageLabels());
        // if the window isn't shown and win.canvasRc is still empty, zoom
        // has not been determined yet
        // cf. https://code.google.com/p/sumatrapdf/issues/detail?id=2541
        // AssertCrash(!args.showWin || !win->canvasRc.IsEmpty() || win->IsChm());
        if ((args.showWin || ss.page != 1) && dm)
            dm->SetScrollState(ss);
        else if (win->IsChm())
            win->ctrl->GoToPage(ss.page, false);
        UpdateToolbarState(win);
    }

    SetSidebarVisibility(win, showToc, gGlobalPrefs->showFavorites);
    win->RedrawAll(true);

    UpdateToolbarAndScrollbarState(*win);
    if (!win->IsDocLoaded()) {
        win->RedrawAll();
        return false;
    }
    // This should only happen after everything else is ready
    if ((args.isNewWindow || args.placeWindow) && args.showWin && showAsFullScreen)
        EnterFullScreen(*win);
    if (!args.isNewWindow && win->presentation && win->ctrl)
        win->ctrl->SetPresentationMode(true);

    return true;
}

void ReloadDocument(WindowInfo *win, bool autorefresh)
{
    if (!win->IsDocLoaded()) {
        if (!autorefresh && win->loadedFilePath) {
            LoadArgs args(win->loadedFilePath, win);
            args.forceReuse = true;
            LoadDocument(args);
        }
        return;
    }
    DisplayState *ds = NewDisplayState(win->loadedFilePath);
    win->ctrl->UpdateDisplayState(ds);
    UpdateDisplayStateWindowRect(*win, *ds);
    UpdateSidebarDisplayState(win, ds);
    // Set the windows state based on the actual window's placement
    ds->windowState = win->isFullScreen ? WIN_STATE_FULLSCREEN
                    : IsZoomed(win->hwndFrame) ? WIN_STATE_MAXIMIZED
                    : IsIconic(win->hwndFrame) ? WIN_STATE_MINIMIZED
                    : WIN_STATE_NORMAL ;

    ScopedMem<WCHAR> path(str::Dup(win->loadedFilePath));
    HwndPasswordUI pwdUI(win->hwndFrame);
    LoadArgs args(path, win);
    args.showWin = true;
    // We don't allow PDF-repair if it is an autorefresh because
    // a refresh event can occur before the file is finished being written,
    // in which case the repair could fail. Instead, if the file is broken,
    // we postpone the reload until the next autorefresh event
    args.allowFailure = !autorefresh;
    args.placeWindow = false;
    if (!LoadDocIntoWindow(args, &pwdUI, ds)) {
        DeleteDisplayState(ds);
        return;
    }

    if (gGlobalPrefs->showStartPage) {
        // refresh the thumbnail for this file
        DisplayState *state = gFileHistory.Find(ds->filePath);
        if (state)
            CreateThumbnailForFile(*win, *state);
    }

    if (win->IsFixedDocLoaded()) {
        // save a newly remembered password into file history so that
        // we don't ask again at the next refresh
        ScopedMem<char> decryptionKey(win->AsFixed()->engine()->GetDecryptionKey());
        if (decryptionKey) {
            DisplayState *state = gFileHistory.Find(ds->filePath);
            if (state && !str::Eq(state->decryptionKey, decryptionKey)) {
                free(state->decryptionKey);
                state->decryptionKey = decryptionKey.StealData();
            }
        }
    }

    DeleteDisplayState(ds);
}

static void UpdateToolbarAndScrollbarState(WindowInfo& win)
{
    ToolbarUpdateStateForWindow(&win, true);
    if (win.IsDocLoaded() && !win.IsChm())
        return;
    ShowScrollBar(win.hwndCanvas, SB_BOTH, FALSE);
    if (win.IsAboutWindow())
        win::SetText(win.hwndFrame, SUMATRA_WINDOW_TITLE);
}

static void CreateSidebar(WindowInfo* win)
{
    win->hwndSidebarSplitter = CreateWindow(SIDEBAR_SPLITTER_CLASS_NAME, L"",
        WS_CHILDWINDOW, 0, 0, 0, 0, win->hwndFrame, (HMENU)0, ghinst, NULL);

    CreateToc(win);
    win->hwndFavSplitter = CreateWindow(FAV_SPLITTER_CLASS_NAME, L"",
        WS_CHILDWINDOW, 0, 0, 0, 0, win->hwndFrame, (HMENU)0, ghinst, NULL);
    CreateFavorites(win);

    if (win->tocVisible) {
        InvalidateRect(win->hwndTocBox, NULL, TRUE);
        UpdateWindow(win->hwndTocBox);
    }

    if (gGlobalPrefs->showFavorites) {
        InvalidateRect(win->hwndFavBox, NULL, TRUE);
        UpdateWindow(win->hwndFavBox);
    }
}

static WindowInfo* CreateWindowInfo()
{
    RectI windowPos = gGlobalPrefs->windowPos;
    if (!windowPos.IsEmpty())
        EnsureAreaVisibility(windowPos);
    else
        windowPos = GetDefaultWindowPos();

    HWND hwndFrame = CreateWindow(
            FRAME_CLASS_NAME, SUMATRA_WINDOW_TITLE,
            WS_OVERLAPPEDWINDOW,
            windowPos.x, windowPos.y, windowPos.dx, windowPos.dy,
            NULL, NULL,
            ghinst, NULL);
    if (!hwndFrame)
        return NULL;

    AssertCrash(NULL == FindWindowInfoByHwnd(hwndFrame));
    WindowInfo *win = new WindowInfo(hwndFrame);

    win->hwndCanvas = CreateWindowEx(
            WS_EX_STATICEDGE,
            CANVAS_CLASS_NAME, NULL,
            WS_CHILD | WS_HSCROLL | WS_VSCROLL,
            0, 0, 0, 0, /* position and size determined in OnSize */
            hwndFrame, NULL,
            ghinst, NULL);
    if (!win->hwndCanvas) {
        delete win;
        return NULL;
    }

    // hide scrollbars to avoid showing/hiding on empty window
    ShowScrollBar(win->hwndCanvas, SB_BOTH, FALSE);

    AssertCrash(!win->menu);
    win->menu = BuildMenu(win);
    win->isMenuHidden = !gGlobalPrefs->showMenubar;
    if (!win->isMenuHidden)
        SetMenu(win->hwndFrame, win->menu);

    ShowWindow(win->hwndCanvas, SW_SHOW);
    UpdateWindow(win->hwndCanvas);

    win->hwndInfotip = CreateWindowEx(WS_EX_TOPMOST,
        TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        win->hwndCanvas, NULL, ghinst, NULL);

    CreateToolbar(win);
    if (gGlobalPrefs->showTabBar)
        CreateTabbar(win);
    CreateSidebar(win);
    UpdateFindbox(win);
    if (HasPermission(Perm_DiskAccess) && !gPluginMode)
        DragAcceptFiles(win->hwndCanvas, TRUE);

    gWindows.Append(win);
    UpdateWindowRtlLayout(win);

    if (Touch::SupportsGestures()) {
        GESTURECONFIG gc = { 0, GC_ALLGESTURES, 0 };
        Touch::SetGestureConfig(win->hwndCanvas, 0, 1, &gc, sizeof(GESTURECONFIG));
    }

    return win;
}

WindowInfo *CreateAndShowWindowInfo()
{
    bool enterFullScreen = (WIN_STATE_FULLSCREEN == gGlobalPrefs->windowState);
    WindowInfo *win = CreateWindowInfo();
    if (!win)
        return NULL;

    if (WIN_STATE_FULLSCREEN == gGlobalPrefs->windowState ||
        WIN_STATE_MAXIMIZED == gGlobalPrefs->windowState)
        ShowWindow(win->hwndFrame, SW_MAXIMIZE);
    else
        ShowWindow(win->hwndFrame, SW_SHOW);
    UpdateWindow(win->hwndFrame);

    if (enterFullScreen)
        EnterFullScreen(*win);
    return win;
}

static void DeleteWindowInfo(WindowInfo *win)
{
    FileWatcherUnsubscribe(win->watcher);
    win->watcher = NULL;

    DeletePropertiesWindow(win->hwndFrame);
    gWindows.Remove(win);

    ImageList_Destroy((HIMAGELIST)SendMessage(win->hwndToolbar, TB_GETIMAGELIST, 0, 0));
    DragAcceptFiles(win->hwndCanvas, FALSE);

    AbortFinding(win);
    AbortPrinting(win);

    if (win->uia_provider) {
        // tell UIA to release all objects cached in its store
        uia::ReturnRawElementProvider(win->hwndCanvas, 0, 0, NULL);
    }

    delete win;
}

class FileChangeCallback : public UITask, public FileChangeObserver
{
    WindowInfo *win;
public:
    explicit FileChangeCallback(WindowInfo *win) : win(win) { }

    virtual void OnFileChanged() {
        // We cannot call win->Reload directly as it could cause race conditions
        // between the watching thread and the main thread (and only pass a copy of this
        // callback to uitask::Post, as the object will be deleted after use)
        uitask::Post(new FileChangeCallback(win));
    }

    virtual void Execute() {
        if (WindowInfoStillValid(win)) {
            // delay the reload slightly, in case we get another request immediately after this one
            SetTimer(win->hwndCanvas, AUTO_RELOAD_TIMER_ID, AUTO_RELOAD_DELAY_IN_MS, NULL);
        }
    }
};

static void RenameFileInHistory(const WCHAR *oldPath, const WCHAR *newPath)
{
    DisplayState *ds = gFileHistory.Find(newPath);
    bool oldIsPinned = false;
    int oldOpenCount = 0;
    if (ds) {
        oldIsPinned = ds->isPinned;
        oldOpenCount = ds->openCount;
        gFileHistory.Remove(ds);
        // TODO: merge favorites as well?
        if (ds->favorites->Count() > 0)
            UpdateFavoritesTreeForAllWindows();
        DeleteDisplayState(ds);
    }
    ds = gFileHistory.Find(oldPath);
    if (ds) {
        str::ReplacePtr(&ds->filePath, newPath);
        // merge Frequently Read data, so that a file
        // doesn't accidentally vanish from there
        ds->isPinned = ds->isPinned || oldIsPinned;
        ds->openCount += oldOpenCount;
        // the thumbnail is recreated by LoadDocument
        delete ds->thumbnail;
        ds->thumbnail = NULL;
    }
}

// document path is either a file or a directory
// (when browsing images inside directory).
static bool DocumentPathExists(const WCHAR *path)
{
    if (file::Exists(path) || dir::Exists(path))
        return true;
    if (str::FindChar(path + 2, ':')) {
        // remove information needed for pointing at embedded documents
        // (e.g. "C:\path\file.pdf:3:0") to check at least whether the
        // container document exists
        ScopedMem<WCHAR> realPath(str::DupN(path, str::FindChar(path + 2, ':') - path));
        return file::Exists(realPath);
    }
    return false;
}

void LoadDocument2(const WCHAR *fileName, const SumatraWindow& win)
{
    // TODO: opening non-mobi files from mobi window doesn't work exactly
    // the same as opening them from non-mobi window
    if (win.AsWindowInfo()) {
        LoadArgs args(fileName, win.AsWindowInfo());
        LoadDocument(args);
        return;
    }
    CrashIf(!win.AsEbookWindow());
    // TODO: LoadDocument() needs to handle EbookWindow, for now
    // we force opening in a new window
    LoadArgs args(fileName);
    LoadDocument(args);
}

#if 0
// Load a file into a new or existing window, show error message
// if loading failed, set the right window position (based on history
// settings for this file or default position), update file history,
// update frequently read information, generate a thumbnail if necessary
// TODO: write me
static WindowInfo* LoadDocumentNew(LoadArgs& args)
{
    ScopedMem<WCHAR> fullPath(path::Normalize(args.fileName));
    // TODO: try to find file on other drives if doesn't exist

    CrashIf(true);
    return NULL;
}
#endif

// TODO: eventually I would like to move all loading to be async. To achieve that
// we need clear separatation of loading process into 2 phases: loading the
// file (and showing progress/load failures in topmost window) and placing
// the loaded document in the window (either by replacing document in existing
// window or creating a new window for the document)
static WindowInfo* LoadDocumentOld(LoadArgs& args)
{
    if (gCrashOnOpen)
        CrashMe();

    ScopedMem<WCHAR> fullPath(path::Normalize(args.fileName));
    WindowInfo *win = args.win;

    bool failEarly = win && !args.forceReuse && !DocumentPathExists(fullPath);
    // try to find inexistent files with history data
    // on a different removable drive before failing
    if (failEarly && gFileHistory.Find(fullPath)) {
        ScopedMem<WCHAR> adjPath(str::Dup(fullPath));
        if (AdjustVariableDriveLetter(adjPath)) {
            RenameFileInHistory(fullPath, adjPath);
            fullPath.Set(adjPath.StealData());
            failEarly = false;
        }
    }

    // fail with a notification if the file doesn't exist and
    // there is a window the user has just been interacting with
    if (failEarly) {
        ScopedMem<WCHAR> msg(str::Format(_TR("File %s not found"), fullPath));
        ShowNotification(win, msg, true /* autoDismiss */, true /* highlight */);
        // display the notification ASAP (prefs::Save() can introduce a notable delay)
        win->RedrawAll(true);

        if (gFileHistory.MarkFileInexistent(fullPath)) {
            prefs::Save();
            // update the Frequently Read list
            if (1 == gWindows.Count() && gWindows.At(0)->IsAboutWindow())
                gWindows.At(0)->RedrawAll(true);
        }
        return NULL;
    }

#ifndef DISABLE_EBOOK_WINDOW
    if (!gGlobalPrefs->ebookUI.useFixedPageUI && IsEbookFile(fullPath)) {
        if (!win) {
            if ((1 == gWindows.Count()) && gWindows.At(0)->IsAboutWindow())
                win = gWindows.At(0);
        } else if (!win->IsAboutWindow() && !args.forceReuse)
            win = NULL;
        if (!win) {
            // create a dummy window so that we can return
            // a non-NULL value to indicate loading success
            win = CreateWindowInfo();
        }
        if (win->IsAboutWindow()) {
            // don't crash if multiple ebook files are opened at once
            // (e.g. via dragging on the window)
            // TODO: figure out a better way to handle this
            win->loadedFilePath = str::Dup(fullPath);
        }
        LoadEbookAsync(fullPath, SumatraWindow::Make(win));
        // TODO: we should show a notification in the window user is looking at
        return win;
    }
#endif

    if (gGlobalPrefs->showTabBar) {
        // modify the args so that we always reuse the same window
        if (!args.win) {
            if (gWindows.Count()) {
                args.win = gWindows.At(0);
                args.isNewWindow = false;
                args.forceReuse = true;
            }
        }
        else
            args.forceReuse = true;
        args.allowFailure = true;

        SaveCurrentTabData(args.win);

        win = args.win;
    }

    if (!win && 1 == gWindows.Count() && gWindows.At(0)->IsAboutWindow()) {
        win = gWindows.At(0);
        args.win = win;
        args.isNewWindow = false;
    } else if (!win || win->IsDocLoaded() && !args.forceReuse) {
        WindowInfo *currWin = win;
        win = CreateWindowInfo();
        if (!win)
            return NULL;
        args.win = win;
        args.isNewWindow = true;
        if (currWin) {
            RememberFavTreeExpansionState(currWin);
            win->expandedFavorites = currWin->expandedFavorites;
        }
    } else if (!win->IsAboutWindow() && !win->IsDocLoaded()) {
        // reuse the window if it only contains an error message
        args.forceReuse = true;
    }

    if (!win->IsAboutWindow()) {
        CrashIf(!args.forceReuse);
        CloseDocumentInWindow(win);
    }
    // invalidate the links on the Frequently Read page
    win->staticLinks.Reset();

    HwndPasswordUI pwdUI(win->hwndFrame);
    args.fileName = fullPath;
    args.allowFailure = true;
    args.placeWindow = true;
    bool loaded = LoadDocIntoWindow(args, &pwdUI);
    // don't fail if a user tries to load an SMX file instead
    if (!loaded && IsModificationsFile(fullPath)) {
        *(WCHAR *)path::GetExt(fullPath) = '\0';
        loaded = LoadDocIntoWindow(args, &pwdUI);
    }

    // insert new tab item for the loaded document
    if (gGlobalPrefs->showTabBar && win && !win->IsAboutWindow())
        TabsOnLoadedDoc(win);

    if (gPluginMode) {
        // hide the menu for embedded documents opened from the plugin
        SetMenu(win->hwndFrame, NULL);
        return win;
    }

    if (!loaded) {
        if (gFileHistory.MarkFileInexistent(fullPath))
            prefs::Save();
        return win;
    }

    FileWatcherUnsubscribe(win->watcher);
    win->watcher = NULL;

    if (gGlobalPrefs->reloadModifiedDocuments)
        win->watcher = FileWatcherSubscribe(fullPath, new FileChangeCallback(win));

    if (gGlobalPrefs->rememberOpenedFiles) {
        CrashIf(!str::Eq(fullPath, win->loadedFilePath));
        DisplayState *ds = gFileHistory.MarkFileLoaded(fullPath);
        if (gGlobalPrefs->showStartPage)
            CreateThumbnailForFile(*win, *ds);
        prefs::Save();
    }

    // Add the file also to Windows' recently used documents (this doesn't
    // happen automatically on drag&drop, reopening from history, etc.)
    if (HasPermission(Perm_DiskAccess) && !gPluginMode && !IsStressTesting())
        SHAddToRecentDocs(SHARD_PATH, fullPath);

    return win;
}

WindowInfo* LoadDocument(LoadArgs& args)
{
#if 0
    return LoadDocumentNew(args);
#else
    return LoadDocumentOld(args);
#endif
}

// Loads document data into the WindowInfo.
void LoadModelIntoTab(WindowInfo *win, TabData *tdata)
{
    if (!win || !tdata) return;

    // TODO: this misses changes to documents opened in a background tab!
    FileWatcherUnsubscribe(win->watcher);
    win->watcher = NULL;

    ClearTocBox(win);
    AbortFinding(win);
    delete win->linkOnLastButtonDown;
    win->linkOnLastButtonDown = NULL;
    if (win->uia_provider)
        win->uia_provider->OnDocumentUnload();

    delete win->pdfsync;
    win->pdfsync = NULL;

    win->notifications->RemoveAllInGroup(NG_RESPONSE_TO_ACTION);
    win->notifications->RemoveAllInGroup(NG_PAGE_INFO_HELPER);
    win->mouseAction = MA_IDLE;

    DeletePropertiesWindow(win->hwndFrame);

    str::ReplacePtr(&win->loadedFilePath, tdata->ctrl->FilePath());

    delete win->ctrl;
    win->ctrl = tdata->ctrl;
    if (win->IsChm())
        win->AsChm()->engine()->SetParentHwnd(win->hwndCanvas);

    // tell UI Automation about content change
    if (win->uia_provider && win->IsFixedDocLoaded())
        win->uia_provider->OnDocumentLoad(win->AsFixed()->model());

    if (win->IsFixedDocLoaded() && win->AsFixed()->model()->viewPort != win->canvasRc)
        win->ctrl->SetViewPortSize(win->GetViewPortSize());

    win->RedrawAll();
    OnMenuFindMatchCase(win);
    UpdateFindbox(win);

    // menu for chm docs is different, so we have to re-create it
    RebuildMenuBarForWindow(win);
    // the toolbar isn't supported for ebook docs (yet)
    ShowOrHideToolbarForWindow(win);

    int pageCount = win->ctrl->PageCount();
    if (pageCount > 0) {
        UpdateToolbarPageText(win, pageCount);
        UpdateToolbarFindText(win);
    }

    win::SetText(win->hwndFrame, tdata->title);

    if (HasPermission(Perm_DiskAccess) && win->GetEngineType() == Engine_PDF) {
        int res = Synchronizer::Create(win->ctrl->FilePath(),
            static_cast<PdfEngine *>(win->AsFixed()->engine()), &win->pdfsync);
        // expose SyncTeX in the UI
        if (PDFSYNCERR_SUCCESS == res)
            gGlobalPrefs->enableTeXEnhancements = true;
    }

    bool enable = !win->IsDocLoaded() || !win->ctrl->HasPageLabels();
    ToggleWindowStyle(win->hwndPageBox, ES_NUMBER, enable);

    if (win->IsFixedDocLoaded()) {
        DisplayModel *dm = win->AsFixed()->model();
        dm->SetScrollState(dm->GetScrollState());
    }
    else
        win->ctrl->GoToPage(win->ctrl->CurrentPageNo(), false);

    UpdateTextSelection(win, false);

    UpdateToolbarState(win);

    win->tocState = tdata->tocState;
    SetSidebarVisibility(win, tdata->showToc, gGlobalPrefs->showFavorites);

    win->RedrawAll(true);

    UpdateToolbarAndScrollbarState(*win);

    if (gGlobalPrefs->reloadModifiedDocuments)
        win->watcher = FileWatcherSubscribe(win->loadedFilePath, new FileChangeCallback(win));
}

// The current page edit box is updated with the current page number
void WindowInfo::PageNoChanged(int pageNo)
{
    AssertCrash(ctrl && ctrl->PageCount() > 0);
    if (!ctrl || ctrl->PageCount() == 0)
        return;

    if (INVALID_PAGE_NO != pageNo) {
        ScopedMem<WCHAR> buf(ctrl->GetPageLabel(pageNo));
        win::SetText(hwndPageBox, buf);
        ToolbarUpdateStateForWindow(this, false);
        if (ctrl->HasPageLabels())
            UpdateToolbarPageText(this, ctrl->PageCount(), true);
    }
    if (pageNo == currPageNo)
        return;

    UpdateTocSelection(this, pageNo);
    currPageNo = pageNo;

    NotificationWnd *wnd = notifications->GetFirstInGroup(NG_PAGE_INFO_HELPER);
    if (!wnd)
        return;

    ScopedMem<WCHAR> pageInfo(str::Format(L"%s %d / %d", _TR("Page:"), pageNo, ctrl->PageCount()));
    if (ctrl->HasPageLabels()) {
        ScopedMem<WCHAR> label(ctrl->GetPageLabel(pageNo));
        pageInfo.Set(str::Format(L"%s %s (%d / %d)", _TR("Page:"), label, pageNo, ctrl->PageCount()));
    }
    wnd->UpdateMessage(pageInfo);
}

bool DoCachePageRendering(DisplayModel *dm, int pageNo)
{
    CrashIf(!dm);
    if (!dm || !dm->engine->IsImageCollection())
        return true;

    // cache large images (mainly photos), as shrinking them
    // for every UI update (WM_PAINT) can cause notable lags
    // TODO: stretching small images also causes minor lags
    RectD page = dm->engine->PageMediabox(pageNo);
    return page.dx * page.dy > 1024 * 1024;
}

/* Send the request to render a given page to a rendering thread */
void WindowInfo::RequestRendering(int pageNo)
{
    CrashIf(!IsFixedDocLoaded());
    if (!IsFixedDocLoaded()) return;

    DisplayModel *dm = AsFixed()->model();
    // don't render any plain images on the rendering thread,
    // they'll be rendered directly in DrawDocument during
    // WM_PAINT on the UI thread
    if (DoCachePageRendering(dm, pageNo))
        gRenderCache.RequestRendering(dm, pageNo);
}

void WindowInfo::CleanUp(DisplayModel *dm)
{
    gRenderCache.CancelRendering(dm);
    gRenderCache.FreeForDisplayModel(dm);
}

void AssociateExeWithPdfExtension()
{
    if (!HasPermission(Perm_RegistryAccess)) return;

    DoAssociateExeWithPdfExtension(HKEY_CURRENT_USER);
    DoAssociateExeWithPdfExtension(HKEY_LOCAL_MACHINE);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, 0, 0);

    // Remind the user, when a different application takes over
    str::ReplacePtr(&gGlobalPrefs->associatedExtensions, L".pdf");
    gGlobalPrefs->associateSilently = false;
}

// Registering happens either through the Installer or the Options dialog;
// here we just make sure that we're still registered
static bool RegisterForPdfExtentions(HWND hwnd)
{
    if (IsRunningInPortableMode() || !HasPermission(Perm_RegistryAccess) || gPluginMode)
        return false;

    if (IsExeAssociatedWithPdfExtension())
        return true;

    /* Ask user for permission, unless he previously said he doesn't want to
       see this dialog */
    if (!gGlobalPrefs->associateSilently) {
        INT_PTR result = Dialog_PdfAssociate(hwnd, &gGlobalPrefs->associateSilently);
        str::ReplacePtr(&gGlobalPrefs->associatedExtensions, IDYES == result ? L".pdf" : NULL);
    }
    // for now, .pdf is the only choice
    if (!str::EqI(gGlobalPrefs->associatedExtensions, L".pdf"))
        return false;

    AssociateExeWithPdfExtension();
    return true;
}

void OnDropFiles(HDROP hDrop, bool dragFinish)
{
    WCHAR       filePath[MAX_PATH];
    const int   count = DragQueryFile(hDrop, DRAGQUERY_NUMFILES, 0, 0);

    for (int i = 0; i < count; i++) {
        DragQueryFile(hDrop, i, filePath, dimof(filePath));
        if (str::EndsWithI(filePath, L".lnk")) {
            ScopedMem<WCHAR> resolved(ResolveLnk(filePath));
            if (resolved)
                str::BufSet(filePath, dimof(filePath), resolved);
        }
        // The first dropped document may override the current window
        LoadArgs args(filePath);
        LoadDocument(args);
    }
    if (dragFinish)
        DragFinish(hDrop);
}

#ifdef SUPPORTS_AUTO_UPDATE
static void RememberCurrentlyOpenedFiles()
{
    CrashIf(gGlobalPrefs->reopenOnce);
    str::Str<WCHAR> cmdLine;

    DisplayState *ds;
    for (size_t i = 0; i < gWindows.Count(); i++) {
        WindowInfo *win = gWindows.At(i);
        if (win->hwndTabBar) {
            TabData *td;
            for (int j = 0; (td = GetTabData(win->hwndTabBar, j)) != NULL; j++) {
                if (td->ctrl && (ds = gFileHistory.Find(td->ctrl->FilePath())) != NULL) {
                    cmdLine.Append('"'); cmdLine.Append(ds->filePath); cmdLine.Append(L"\" ");
                }
            }
        }
        else if ((ds = gFileHistory.Find(win->loadedFilePath)) != NULL) {
            cmdLine.Append('"'); cmdLine.Append(ds->filePath); cmdLine.Append(L"\" ");
        }
    }
    for (size_t i = 0; i < gEbookWindows.Count(); i++) {
        EbookWindow *win = gEbookWindows.At(i);
        if ((ds = gFileHistory.Find(win->LoadedFilePath())) != NULL) {
            cmdLine.Append('"'); cmdLine.Append(ds->filePath); cmdLine.Append(L"\" ");
        }
    }

    if (cmdLine.Size() > 0) {
        cmdLine.Pop();
        gGlobalPrefs->reopenOnce = cmdLine.StealData();
    }
}

bool AutoUpdateInitiate(const char *mode, const char *url, const char *hash)
{
    if (!url || !hash || !str::EndsWithI(url, ".exe"))
        return false;

    ScopedMem<WCHAR> exeUrl(str::conv::FromUtf8(url));
    HttpReq req(exeUrl);
    if (req.error)
        return false;

    unsigned char digest[32];
    CalcSHA2Digest((const unsigned char *)req.data->Get(), req.data->Size(), digest);
    ScopedMem<char> fingerPrint(_MemToHex(&digest));
    if (!str::EqI(fingerPrint, hash))
        return false;

    // TODO: use %TEMP% instead of portable path(?)
    ScopedMem<WCHAR> updater(GetExePath());
    updater.Set(str::Join(updater, L"-updater.exe"));
    bool ok = file::WriteAll(updater, req.data->Get(), req.data->Size());
    if (!ok)
        return false;

    ok = LaunchFile(updater, L"-autoupdate replace");
    if (ok) {
        RememberCurrentlyOpenedFiles();
        OnMenuExit();
    }
    return ok;
}
#endif

/* The format used for SUMATRA_UPDATE_INFO_URL looks as follows:

[SumatraPDF]
# the first line must start with SumatraPDF (optionally as INI header)
Latest 2.6
# Latest must be the version number of the version currently offered for download
Stable 2.5.3
# Stable is optional and indicates the oldest version for which automated update
# checks don't yet report the available update

# further information can be added, e.g. the following experimental subkey for
# auto-updating (requires SUPPORTS_AUTO_UPDATE)
Portable [
    URL: <download URL for the uncompressed portable .exe>
    Hash <SHA-256 hash of that file>
]
*/

static DWORD ShowAutoUpdateDialog(HWND hParent, HttpReq *ctx, bool silent)
{
    if (ctx->error)
        return ctx->error;
    if (!str::StartsWith(ctx->url, SUMATRA_UPDATE_INFO_URL))
        return ERROR_INTERNET_INVALID_URL;

    // See http://code.google.com/p/sumatrapdf/issues/detail?id=725
    // If a user configures os-wide proxy that is not regular ie proxy
    // (which we pick up) we might get complete garbage in response to
    // our query. Make sure to check whether the returned data is sane.
    if (!str::StartsWith(ctx->data->Get(), '[' == ctx->data->At(0) ? "[SumatraPDF]" : "SumatraPDF"))
        return ERROR_INTERNET_INVALID_URL;

    SquareTree tree(ctx->data->Get());
    SquareTreeNode *node = tree.root ? tree.root->GetChild("SumatraPDF") : NULL;
    const char *latest = node ? node->GetValue("Latest") : NULL;
    if (!latest || !IsValidProgramVersion(latest))
        return ERROR_INTERNET_INVALID_URL;

    ScopedMem<WCHAR> verTxt(str::conv::FromUtf8(latest));
    if (CompareVersion(verTxt, UPDATE_CHECK_VER) <= 0) {
        /* if automated => don't notify that there is no new version */
        if (!silent)
            MessageBoxWarning(hParent, _TR("You have the latest version."), _TR("SumatraPDF Update"));
        return 0;
    }

    if (silent) {
        const char *stable = node->GetValue("Stable");
        if (stable && IsValidProgramVersion(stable) &&
            CompareVersion(ScopedMem<WCHAR>(str::conv::FromUtf8(stable)), UPDATE_CHECK_VER) <= 0) {
            // don't update just yet if the older version is still marked as stable
            return 0;
        }
    }

    // if automated, respect gGlobalPrefs->versionToSkip
    if (silent && str::EqI(gGlobalPrefs->versionToSkip, verTxt))
        return 0;

    // ask whether to download the new version and allow the user to
    // either open the browser, do nothing or don't be reminded of
    // this update ever again
    bool skipThisVersion = false;
    INT_PTR res = Dialog_NewVersionAvailable(hParent, UPDATE_CHECK_VER, verTxt, &skipThisVersion);
    if (skipThisVersion) {
        free(gGlobalPrefs->versionToSkip);
        gGlobalPrefs->versionToSkip = verTxt.StealData();
    }
    if (IDYES == res) {
#ifdef SUPPORTS_AUTO_UPDATE
        // TODO: support auto-updating using the installer
        const char *mode = "Portable";
        SquareTreeNode *data = node->GetChild(mode);
        const char *url = data ? data->GetValue("URL") : NULL;
        const char *hash = data ? data->GetValue("Hash") : NULL;
        if (AutoUpdateInitiate(mode, url, hash))
            return 0;
#endif
        LaunchBrowser(SVN_UPDATE_LINK);
    }
    prefs::Save();

    return 0;
}

#ifdef SUPPORTS_AUTO_UPDATE
static bool AutoUpdateMain()
{
    WStrVec argList;
    ParseCmdLine(GetCommandLine(), argList, 4);
    if (argList.Count() != 3 || !str::Eq(argList.At(1), L"-autoupdate")) {
        // the argument was misinterpreted, let SumatraPDF start as usual
        return false;
    }
    ScopedMem<WCHAR> thisExe(GetExePath());
    ScopedMem<WCHAR> otherExe;
    bool beforeUpdate = str::Eq(argList.At(2), L"replace");
    if (beforeUpdate) {
        CrashIf(!str::EndsWith(thisExe, L".exe-updater.exe"));
        otherExe.Set(str::DupN(thisExe, str::Len(thisExe) - 12));
    }
    else {
        CrashIf(!str::Eq(argList.At(2), L"cleanup"));
        otherExe.Set(str::Join(thisExe, L"-updater.exe"));
    }
    for (int tries = 10; tries > 0; tries--) {
        if (file::Delete(otherExe))
            break;
        Sleep(200);
    }
    if (!beforeUpdate) {
        // continue startup
        // TODO: restore previous session?
        return false;
    }
    bool ok = CopyFile(thisExe, otherExe, FALSE);
    // TODO: somehow indicate success or failure
    for (int tries = 10; tries > 0; tries--) {
        ok = LaunchFile(otherExe, L"-autoupdate cleanup");
        if (ok)
            break;
        Sleep(200);
    }
    return true;
}
#endif

static void ProcessAutoUpdateCheckResult(HWND hwnd, HttpReq *req, bool autoCheck)
{
    ScopedMem<WCHAR> msg;

    if (!IsWindowVisible(hwnd) || !req)
        goto Exit;
    DWORD error = ShowAutoUpdateDialog(hwnd, req, autoCheck);
    if ((0 == error) || autoCheck)
        goto Exit;

    // notify the user about network error during a manual update check
    msg.Set(str::Format(_TR("Can't connect to the Internet (error %#x)."), error));
    MessageBoxWarning(hwnd, msg, _TR("SumatraPDF Update"));
Exit:
    delete req;
}

class UpdateDownloadTask : public UITask, public HttpReqCallback
{
    HWND        hwnd;
    bool        autoCheck;
    HttpReq *   req;

public:
    UpdateDownloadTask(HWND hwnd, bool autoCheck) :
        hwnd(hwnd), autoCheck(autoCheck), req(NULL) { }

    virtual void Callback(HttpReq *aReq) {
        req = aReq;
        uitask::Post(this);
    }

    virtual void Execute() {
        ProcessAutoUpdateCheckResult(hwnd, req, autoCheck);
    }
};

// start auto-update check by downloading auto-update information from url
// on a background thread and processing the retrieved data on ui thread
// if autoCheck is true, this is a check *not* triggered by explicit action
// of the user and therefore will show less UI
void AutoUpdateCheckAsync(HWND hwnd, bool autoCheck)
{
    if (!HasPermission(Perm_InternetAccess))
        return;

    // For auto-check, only check if at least a day passed since last check
    if (autoCheck) {
        // don't check if the timestamp or version to skip can't be updated
        // (mainly in plugin mode, stress testing and restricted settings)
        if (!HasPermission(Perm_SavePreferences))
            return;

        // don't check for updates at the first start, so that privacy
        // sensitive users can disable the update check in time
        FILETIME never = { 0 };
        if (FileTimeEq(gGlobalPrefs->timeOfLastUpdateCheck, never))
            return;

        FILETIME currentTimeFt;
        GetSystemTimeAsFileTime(&currentTimeFt);
        int secs = FileTimeDiffInSecs(currentTimeFt, gGlobalPrefs->timeOfLastUpdateCheck);
        // if secs < 0 => somethings wrong, so ignore that case
        if ((secs >= 0) && (secs < SECS_IN_DAY))
            return;
    }

    const WCHAR *url = SUMATRA_UPDATE_INFO_URL L"?v=" UPDATE_CHECK_VER;
    new HttpReq(url, new UpdateDownloadTask(hwnd, autoCheck));

    GetSystemTimeAsFileTime(&gGlobalPrefs->timeOfLastUpdateCheck);
}

static void RerenderEverything()
{
    for (size_t i = 0; i < gWindows.Count(); i++) {
        if (!gWindows.At(i)->IsFixedDocLoaded())
            continue;
        DisplayModel *dm = gWindows.At(i)->AsFixed()->model();
        gRenderCache.CancelRendering(dm);
        gRenderCache.KeepForDisplayModel(dm, dm);
        gWindows.At(i)->RedrawAll(true);
    }
}

COLORREF GetFixedPageUiTextColor()
{
    if (gGlobalPrefs->useSysColors)
        return GetSysColor(COLOR_WINDOWTEXT);
    return gGlobalPrefs->fixedPageUI.textColor;
}

COLORREF GetFixedPageUiBgColor()
{
    if (gGlobalPrefs->useSysColors)
        return GetSysColor(COLOR_WINDOW);
    return gGlobalPrefs->fixedPageUI.backgroundColor;
}

void UpdateDocumentColors()
{
    COLORREF text = GetFixedPageUiTextColor();
    COLORREF bg = GetFixedPageUiBgColor();
    if ((text == gRenderCache.textColor) &&
        (bg == gRenderCache.backgroundColor)) {
            return; // colors didn't change
    }

    gRenderCache.textColor = text;
    gRenderCache.backgroundColor = bg;
    RerenderEverything();
}

#if defined(SHOW_DEBUG_MENU_ITEMS) || defined(DEBUG)
static void ToggleGdiDebugging()
{
    gUseGdiRenderer = !gUseGdiRenderer;
    DebugGdiPlusDevice(gUseGdiRenderer);
    RerenderEverything();
}
#endif

void OnMenuExit()
{
    if (gPluginMode)
        return;

    for (size_t i = 0; i < gWindows.Count(); i++) {
        WindowInfo *win = gWindows.At(i);
        if (win->printThread && !win->printCanceled) {
            int res = MessageBox(win->hwndFrame, _TR("Printing is still in progress. Abort and quit?"), _TR("Printing in progress."), MB_ICONEXCLAMATION | MB_YESNO | MbRtlReadingMaybe());
            if (IDNO == res)
                return;
        }
        AbortFinding(win);
        AbortPrinting(win);

        if (gGlobalPrefs->showTabBar)
            TabsOnCloseWindow(win, true);
    }

    prefs::Save();
    PostQuitMessage(0);
}

size_t TotalWindowsCount()
{
    return gWindows.Count() + gEbookWindows.Count();
}

// closes a document inside a WindowInfo and turns it into
// about window
void CloseDocumentInWindow(WindowInfo *win)
{
    bool wasntFixed = !win->IsFixedDocLoaded();
    if (win->IsChm())
        win->AsChm()->engine()->RemoveParentHwnd();
    FileWatcherUnsubscribe(win->watcher);
    win->watcher = NULL;
    SetSidebarVisibility(win, false, gGlobalPrefs->showFavorites);
    ClearTocBox(win);
    AbortFinding(win, true);
    delete win->linkOnLastButtonDown;
    win->linkOnLastButtonDown = NULL;
    if (win->uia_provider)
        win->uia_provider->OnDocumentUnload();
    delete win->ctrl;
    win->ctrl = NULL;
    str::ReplacePtr(&win->loadedFilePath, NULL);
    delete win->pdfsync;
    win->pdfsync = NULL;
    win->notifications->RemoveAllInGroup(NG_RESPONSE_TO_ACTION);
    win->notifications->RemoveAllInGroup(NG_PAGE_INFO_HELPER);
    win->mouseAction = MA_IDLE;

    DeletePropertiesWindow(win->hwndFrame);
    UpdateToolbarPageText(win, 0);
    UpdateToolbarFindText(win);
    if (wasntFixed) {
        // restore the full menu and toolbar
        RebuildMenuBarForWindow(win);
        ShowOrHideToolbarForWindow(win);
    }

    DeleteOldSelectionInfo(win, true);
    win->RedrawAll();
    UpdateFindbox(win);
    SetFocus(win->hwndFrame);

#ifdef DEBUG
    // cf. https://code.google.com/p/sumatrapdf/issues/detail?id=2039
    // HeapValidate() is left here to help us catch the possibility that the fix
    // in FileWatcher::SynchronousAbort() isn't correct
    HeapValidate((HANDLE)_get_heap_handle(), 0, NULL);
#endif
}

void QuitIfNoMoreWindows()
{
    if (0 == TotalWindowsCount()) {
        PostQuitMessage(0);
    }
}

void CloseDocumentAndDeleteWindowInfo(WindowInfo *win)
{
    if (!win)
        return;
    HWND hwndToDestroy = win->hwndFrame;
    CloseDocumentInWindow(win);
    DeleteWindowInfo(win);
    DestroyWindow(hwndToDestroy);
}

/* Close the document associated with window 'hwnd'.
   Closes the window unless this is the last window in which
   case it switches to empty window and disables the "File\Close"
   menu item. */
void CloseWindow(WindowInfo *win, bool quitIfLast, bool forceClose)
{
    CrashIf(!win);
    if (!win) return;
    CrashIf(forceClose && !quitIfLast);
    if (forceClose) quitIfLast = true;

    // when used as an embedded plugin, closing should happen automatically
    // when the parent window is destroyed (cf. WM_DESTROY)
    if (gPluginMode && gWindows.Find(win) == 0 && !forceClose)
        return;

    if (win->printThread && !win->printCanceled) {
        int res = MessageBox(win->hwndFrame, _TR("Printing is still in progress. Abort and quit?"), _TR("Printing in progress."), MB_ICONEXCLAMATION | MB_YESNO | MbRtlReadingMaybe());
        if (IDNO == res)
            return;
    }

    if (win->IsFixedDocLoaded() && win->AsFixed()->userAnnots && win->AsFixed()->userAnnotsModified) {
        // TODO: warn about unsaved changes
    }

    if (gGlobalPrefs->showTabBar) {
        TabsOnCloseWindow(win, quitIfLast);
        if (!quitIfLast && TabCtrl_GetItemCount(win->hwndTabBar))
            return;
    }

    if (win->IsFixedDocLoaded())
        win->AsFixed()->model()->dontRenderFlag = true;
    if (win->presentation)
        ExitFullScreen(*win);

    bool lastWindow = (1 == TotalWindowsCount());
    // hide the window before saving prefs (closing seems slightly faster that way)
    if (lastWindow && quitIfLast && !forceClose)
        ShowWindow(win->hwndFrame, SW_HIDE);
    if (lastWindow)
        prefs::Save();
    else
        UpdateCurrentFileDisplayStateForWin(SumatraWindow::Make(win));

    if (forceClose) {
        // WM_DESTROY has already been sent, so don't destroy win->hwndFrame again
        DeleteWindowInfo(win);
    } else if (lastWindow && !quitIfLast) {
        /* last window - don't delete it */
        CloseDocumentInWindow(win);
    } else {
        HWND hwndToDestroy = win->hwndFrame;
        DeleteWindowInfo(win);
        DestroyWindow(hwndToDestroy);
    }

    if (lastWindow && quitIfLast) {
        AssertCrash(0 == gWindows.Count());
        PostQuitMessage(0);
    } else if (lastWindow && !quitIfLast) {
        CrashIf(!gWindows.Contains(win));
        UpdateToolbarAndScrollbarState(*win);

        if (gGlobalPrefs->showTabBar)
            ShowOrHideTabbar(win, SW_HIDE);
    }
}

// returns false if no filter has been appended
static bool AppendFileFilterForDoc(Controller *ctrl, str::Str<WCHAR>& fileFilter)
{
    EngineType type = ctrl->AsFixed() ? ctrl->AsFixed()->engineType :
                      ctrl->AsChm() ? Engine_Chm :
                      Engine_None;
    switch (type) {
        case Engine_XPS:    fileFilter.Append(_TR("XPS documents")); break;
        case Engine_DjVu:   fileFilter.Append(_TR("DjVu documents")); break;
        case Engine_ComicBook: fileFilter.Append(_TR("Comic books")); break;
        case Engine_Image:  fileFilter.AppendFmt(_TR("Image files (*.%s)"), ctrl->DefaultFileExt() + 1); break;
        case Engine_ImageDir: return false; // only show "All files"
        case Engine_PS:     fileFilter.Append(_TR("Postscript documents")); break;
        case Engine_Chm:    fileFilter.Append(_TR("CHM documents")); break;
        case Engine_Epub:   fileFilter.Append(_TR("EPUB ebooks")); break;
        case Engine_Mobi:   fileFilter.Append(_TR("Mobi documents")); break;
        case Engine_Fb2:    fileFilter.Append(_TR("FictionBook documents")); break;
        case Engine_Pdb:    fileFilter.Append(L"PalmDOC"); break;
        case Engine_Chm2:   fileFilter.Append(_TR("CHM documents")); break;
        case Engine_Tcr:    fileFilter.Append(L"TCR ebooks"); break;
        case Engine_Txt:    fileFilter.Append(_TR("Text documents")); break;
        default:            fileFilter.Append(_TR("PDF documents")); break;
    }
    return true;
}

static void OnMenuSaveAs(WindowInfo& win)
{
    if (!HasPermission(Perm_DiskAccess)) return;
    AssertCrash(win.ctrl);
    if (!win.IsDocLoaded()) return;

    const WCHAR *srcFileName = win.ctrl->FilePath();
    ScopedMem<WCHAR> urlName;
    if (gPluginMode) {
        urlName.Set(ExtractFilenameFromURL(gPluginURL));
        // fall back to a generic "filename" instead of the more confusing temporary filename
        srcFileName = urlName ? urlName : L"filename";
    }

    AssertCrash(srcFileName);
    if (!srcFileName) return;

    BaseEngine *engine = win.IsFixedDocLoaded() ? win.AsFixed()->engine() : NULL;
    // Can't save a document's content as plain text if text copying isn't allowed
    bool hasCopyPerm = engine && engine->IsImageCollection();
#ifndef DISABLE_DOCUMENT_RESTRICTIONS
    if (engine && !engine->AllowsCopyingText())
        hasCopyPerm = false;
#endif
    CrashIf(hasCopyPerm && !engine);
    bool canConvertToPDF = engine && Engine_PS == win.GetEngineType();
    CrashIf(canConvertToPDF && (!engine || win.GetEngineType() != Engine_PS));

    const WCHAR *defExt = win.ctrl->DefaultFileExt();
    // Prepare the file filters (use \1 instead of \0 so that the
    // double-zero terminated string isn't cut by the string handling
    // methods too early on)
    str::Str<WCHAR> fileFilter(256);
    if (AppendFileFilterForDoc(win.ctrl, fileFilter))
        fileFilter.AppendFmt(L"\1*%s\1", defExt);
    if (hasCopyPerm) {
        fileFilter.Append(_TR("Text documents"));
        fileFilter.Append(L"\1*.txt\1");
    }
    if (canConvertToPDF) {
        fileFilter.Append(_TR("PDF documents"));
        fileFilter.Append(L"\1*.pdf\1");
    }
    fileFilter.Append(_TR("All files"));
    fileFilter.Append(L"\1*.*\1");
    str::TransChars(fileFilter.Get(), L"\1", L"\0");

    WCHAR dstFileName[MAX_PATH];
    str::BufSet(dstFileName, dimof(dstFileName), path::GetBaseName(srcFileName));
    if (str::FindChar(dstFileName, ':')) {
        // handle embed-marks (for embedded PDF documents):
        // remove the container document's extension and include
        // the embedding reference in the suggested filename
        WCHAR *colon = (WCHAR *)str::FindChar(dstFileName, ':');
        str::TransChars(colon, L":", L"_");
        WCHAR *ext;
        for (ext = colon; ext > dstFileName && *ext != '.'; ext--);
        if (ext == dstFileName)
            ext = colon;
        memmove(ext, colon, (str::Len(colon) + 1) * sizeof(WCHAR));
    }
    // Remove the extension so that it can be re-added depending on the chosen filter
    else if (str::EndsWithI(dstFileName, defExt))
        dstFileName[str::Len(dstFileName) - str::Len(defExt)] = '\0';

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win.hwndFrame;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = fileFilter.Get();
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = defExt + 1;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    // note: explicitly not setting lpstrInitialDir so that the OS
    // picks a reasonable default (in particular, we don't want this
    // in plugin mode, which is likely the main reason for saving as...)

    bool ok = GetSaveFileName(&ofn);
    if (!ok)
        return;

    WCHAR * realDstFileName = dstFileName;
    // Make sure that the file has a valid ending
    if (!str::EndsWithI(dstFileName, defExt) &&
        !(hasCopyPerm && str::EndsWithI(dstFileName, L".txt")) &&
        !(canConvertToPDF && str::EndsWithI(dstFileName, L".pdf"))) {
        if (hasCopyPerm && 2 == ofn.nFilterIndex)
            defExt = L".txt";
        else if (canConvertToPDF && (hasCopyPerm ? 3 : 2) == (int)ofn.nFilterIndex)
            defExt = L".pdf";
        realDstFileName = str::Format(L"%s%s", dstFileName, defExt);
    }

    ScopedMem<WCHAR> errorMsg;
    // Extract all text when saving as a plain text file
    if (hasCopyPerm && str::EndsWithI(realDstFileName, L".txt") &&
        (2 == ofn.nFilterIndex || Engine_Txt != win.GetEngineType())) {
        str::Str<WCHAR> text(1024);
        for (int pageNo = 1; pageNo <= win.ctrl->PageCount(); pageNo++) {
            WCHAR *tmp = engine->ExtractPageText(pageNo, L"\r\n", NULL, Target_Export);
            text.AppendAndFree(tmp);
        }

        ScopedMem<char> textUTF8(str::conv::ToUtf8(text.LendData()));
        ScopedMem<char> textUTF8BOM(str::Join(UTF8_BOM, textUTF8));
        ok = file::WriteAll(realDstFileName, textUTF8BOM, str::Len(textUTF8BOM));
    }
    // Convert the Postscript file into a PDF one
    else if (canConvertToPDF && str::EndsWithI(realDstFileName, L".pdf")) {
        ok = static_cast<PsEngine *>(engine)->SaveFileAsPDF(realDstFileName);
    }
    // Recreate inexistant files from memory...
    else if (!file::Exists(srcFileName) && engine) {
        ok = engine->SaveFileAs(realDstFileName);
    }
    // ... as well as files containing annotations ...
    else if (gGlobalPrefs->annotationDefaults.saveIntoDocument &&
             engine && engine->SupportsAnnotation(true)) {
        ok = engine->SaveFileAs(realDstFileName);
    }
    // ... else just copy the file
    else if (!path::IsSame(srcFileName, realDstFileName)) {
        WCHAR *msgBuf;
        ok = CopyFile(srcFileName, realDstFileName, FALSE);
        if (ok) {
            // Make sure that the copy isn't write-locked or hidden
            const DWORD attributesToDrop = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
            DWORD attributes = GetFileAttributes(realDstFileName);
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & attributesToDrop))
                SetFileAttributes(realDstFileName, attributes & ~attributesToDrop);
        } else if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), 0, (LPWSTR)&msgBuf, 0, NULL)) {
            errorMsg.Set(str::Format(L"%s\n\n%s", _TR("Failed to save a file"), msgBuf));
            LocalFree(msgBuf);
        }
    }
    if (ok && win.IsFixedDocLoaded() && win.AsFixed()->userAnnots && win.AsFixed()->userAnnotsModified) {
        if (!gGlobalPrefs->annotationDefaults.saveIntoDocument ||
            !engine || !engine->SupportsAnnotation(true)) {
            ok = SaveFileModifictions(realDstFileName, win.AsFixed()->userAnnots);
        }
        if (ok && path::IsSame(srcFileName, realDstFileName))
            win.AsFixed()->userAnnotsModified = false;
    }
    if (!ok)
        MessageBoxWarning(win.hwndFrame, errorMsg ? errorMsg : _TR("Failed to save a file"));

    if (ok && IsUntrustedFile(win.ctrl->FilePath(), gPluginURL))
        file::SetZoneIdentifier(realDstFileName);

    if (realDstFileName != dstFileName)
        free(realDstFileName);
}

bool LinkSaver::SaveEmbedded(const unsigned char *data, size_t len)
{
    if (!HasPermission(Perm_DiskAccess))
        return false;

    WCHAR dstFileName[MAX_PATH];
    str::BufSet(dstFileName, dimof(dstFileName), fileName ? fileName : L"");
    CrashIf(fileName && str::FindChar(fileName, '/'));

    // Prepare the file filters (use \1 instead of \0 so that the
    // double-zero terminated string isn't cut by the string handling
    // methods too early on)
    ScopedMem<WCHAR> fileFilter(str::Format(L"%s\1*.*\1", _TR("All files")));
    str::TransChars(fileFilter, L"\1", L"\0");

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner->hwndFrame;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = fileFilter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    bool ok = GetSaveFileName(&ofn);
    if (!ok)
        return false;
    ok = file::WriteAll(dstFileName, data, len);
    if (ok && IsUntrustedFile(owner->ctrl ? owner->ctrl->FilePath() : owner->loadedFilePath, gPluginURL))
        file::SetZoneIdentifier(dstFileName);
    return ok;
}

static void OnMenuRenameFile(WindowInfo &win)
{
    if (!HasPermission(Perm_DiskAccess)) return;
    CrashIf(!win.ctrl);
    if (!win.IsDocLoaded()) return;
    if (gPluginMode) return;

    const WCHAR *srcFileName = win.ctrl->FilePath();
    // this happens e.g. for embedded documents and directories
    if (!file::Exists(srcFileName))
        return;

    // Prepare the file filters (use \1 instead of \0 so that the
    // double-zero terminated string isn't cut by the string handling
    // methods too early on)
    const WCHAR *defExt = win.ctrl->DefaultFileExt();
    str::Str<WCHAR> fileFilter(256);
    bool ok = AppendFileFilterForDoc(win.ctrl, fileFilter);
    CrashIf(!ok);
    fileFilter.AppendFmt(L"\1*%s\1", defExt);
    str::TransChars(fileFilter.Get(), L"\1", L"\0");

    WCHAR dstFileName[MAX_PATH];
    str::BufSet(dstFileName, dimof(dstFileName), path::GetBaseName(srcFileName));
    // Remove the extension so that it can be re-added depending on the chosen filter
    if (str::EndsWithI(dstFileName, defExt))
        dstFileName[str::Len(dstFileName) - str::Len(defExt)] = '\0';

    ScopedMem<WCHAR> initDir(path::GetDir(srcFileName));

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win.hwndFrame;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = fileFilter.Get();
    ofn.nFilterIndex = 1;
    // note: the other two dialogs are named "Open" and "Save As"
    ofn.lpstrTitle = _TR("Rename To");
    ofn.lpstrInitialDir = initDir;
    ofn.lpstrDefExt = defExt + 1;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    ok = GetSaveFileName(&ofn);
    if (!ok)
        return;

    UpdateCurrentFileDisplayStateForWinInfo(&win);
    // note: srcFileName is deleted together with the DisplayModel
    ScopedMem<WCHAR> srcFilePath(str::Dup(srcFileName));
    CloseDocumentInWindow(&win);

    DWORD flags = MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING;
    BOOL moveOk = MoveFileEx(srcFilePath.Get(), dstFileName, flags);
    if (!moveOk) {
        LogLastError();
        LoadArgs args(srcFilePath, &win);
        LoadDocument(args);
        ShowNotification(&win, _TR("Failed to rename the file!"), false /* autoDismiss */, true /* highlight */);
        return;
    }

    ScopedMem<WCHAR> newPath(path::Normalize(dstFileName));
    RenameFileInHistory(srcFilePath, newPath);

    LoadArgs args(dstFileName, &win);
    LoadDocument(args);
}

static void OnMenuSaveBookmark(WindowInfo& win)
{
    if (!HasPermission(Perm_DiskAccess) || gPluginMode) return;
    CrashIf(!win.ctrl);
    if (!win.IsDocLoaded()) return;

    const WCHAR *defExt = win.ctrl->DefaultFileExt();

    WCHAR dstFileName[MAX_PATH];
    // Remove the extension so that it can be replaced with .lnk
    str::BufSet(dstFileName, dimof(dstFileName), path::GetBaseName(win.ctrl->FilePath()));
    str::TransChars(dstFileName, L":", L"_");
    if (str::EndsWithI(dstFileName, defExt))
        dstFileName[str::Len(dstFileName) - str::Len(defExt)] = '\0';

    // Prepare the file filters (use \1 instead of \0 so that the
    // double-zero terminated string isn't cut by the string handling
    // methods too early on)
    ScopedMem<WCHAR> fileFilter(str::Format(L"%s\1*.lnk\1", _TR("Bookmark Shortcuts")));
    str::TransChars(fileFilter, L"\1", L"\0");

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win.hwndFrame;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = fileFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"lnk";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetSaveFileName(&ofn))
        return;

    ScopedMem<WCHAR> fileName(str::Dup(dstFileName));
    if (!str::EndsWithI(dstFileName, L".lnk"))
        fileName.Set(str::Join(dstFileName, L".lnk"));

    ScrollState ss(win.ctrl->CurrentPageNo());
    if (win.IsFixedDocLoaded())
        ss = win.AsFixed()->model()->GetScrollState();
    const WCHAR *viewMode = prefs::conv::FromDisplayMode(win.ctrl->GetDisplayMode());
    ScopedMem<WCHAR> ZoomVirtual(str::Format(L"%.2f", win.ctrl->GetZoomVirtual()));
    if (ZOOM_FIT_PAGE == win.ctrl->GetZoomVirtual())
        ZoomVirtual.Set(str::Dup(L"fitpage"));
    else if (ZOOM_FIT_WIDTH == win.ctrl->GetZoomVirtual())
        ZoomVirtual.Set(str::Dup(L"fitwidth"));
    else if (ZOOM_FIT_CONTENT == win.ctrl->GetZoomVirtual())
        ZoomVirtual.Set(str::Dup(L"fitcontent"));

    ScopedMem<WCHAR> exePath(GetExePath());
    ScopedMem<WCHAR> args(str::Format(L"\"%s\" -page %d -view \"%s\" -zoom %s -scroll %d,%d -reuse-instance",
                          win.ctrl->FilePath(), ss.page, viewMode, ZoomVirtual, (int)ss.x, (int)ss.y));
    ScopedMem<WCHAR> label(win.ctrl->GetPageLabel(ss.page));
    ScopedMem<WCHAR> desc(str::Format(_TR("Bookmark shortcut to page %s of %s"),
                          label, path::GetBaseName(win.ctrl->FilePath())));

    CreateShortcut(fileName, exePath, args, desc, 1);
}

#if 0
// code adapted from http://support.microsoft.com/kb/131462/en-us
static UINT_PTR CALLBACK FileOpenHook(HWND hDlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uiMsg) {
    case WM_INITDIALOG:
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
        break;
    case WM_NOTIFY:
        if (((LPOFNOTIFY)lParam)->hdr.code == CDN_SELCHANGE) {
            LPOPENFILENAME lpofn = (LPOPENFILENAME)GetWindowLongPtr(hDlg, GWLP_USERDATA);
            // make sure that the filename buffer is large enough to hold
            // all the selected filenames
            int cbLength = CommDlg_OpenSave_GetSpec(GetParent(hDlg), NULL, 0) + MAX_PATH;
            if (cbLength >= 0 && lpofn->nMaxFile < (DWORD)cbLength) {
                WCHAR *oldBuffer = lpofn->lpstrFile;
                lpofn->lpstrFile = (LPWSTR)realloc(lpofn->lpstrFile, cbLength * sizeof(WCHAR));
                if (lpofn->lpstrFile)
                    lpofn->nMaxFile = cbLength;
                else
                    lpofn->lpstrFile = oldBuffer;
            }
        }
        break;
    }

    return 0;
}
#endif

HWND GetSumatraWindowHwnd(const SumatraWindow& win)
{
    if (win.AsWindowInfo())
        return win.AsWindowInfo()->hwndFrame;
    CrashIf(!win.AsEbookWindow());
    return win.AsEbookWindow()->hwndFrame;
}

void OnMenuOpen(const SumatraWindow& win)
{
    if (!HasPermission(Perm_DiskAccess)) return;
    // don't allow opening different files in plugin mode
    if (gPluginMode)
        return;

    const struct {
        const WCHAR *name; /* NULL if only to include in "All supported documents" */
        const WCHAR *filter;
        bool available;
    } fileFormats[] = {
        { _TR("PDF documents"),         L"*.pdf",       true },
        { _TR("XPS documents"),         L"*.xps;*.oxps",true },
        { _TR("DjVu documents"),        L"*.djvu",      true },
        { _TR("Postscript documents"),  L"*.ps;*.eps",  PsEngine::IsAvailable() },
        { _TR("Comic books"),           L"*.cbz;*.cbr", true },
        { _TR("CHM documents"),         L"*.chm",       true },
        { _TR("Mobi documents"),        L"*.mobi",      true },
        { _TR("EPUB ebooks"),           L"*.epub",      true },
        { _TR("FictionBook documents"), L"*.fb2;*.fb2z;*.zfb2;*.fb2.zip", true },
        { NULL, /* multi-page images */ L"*.tif;*.tiff",true },
        { NULL, /* further ebooks */    L"*.pdb;*.tcr", gGlobalPrefs->ebookUI.useFixedPageUI },
        { _TR("Text documents"),        L"*.txt;*.log;*.nfo;file_id.diz;read.me", gGlobalPrefs->ebookUI.useFixedPageUI },
    };
    // Prepare the file filters (use \1 instead of \0 so that the
    // double-zero terminated string isn't cut by the string handling
    // methods too early on)
    str::Str<WCHAR> fileFilter;
    fileFilter.Append(_TR("All supported documents"));
    fileFilter.Append(L'\1');
    for (int i = 0; i < dimof(fileFormats); i++) {
        if (fileFormats[i].available) {
            fileFilter.Append(fileFormats[i].filter);
            fileFilter.Append(';');
        }
    }
    CrashIf(fileFilter.Last() != L';');
    fileFilter.Last() = L'\1';
    for (int i = 0; i < dimof(fileFormats); i++) {
        if (fileFormats[i].available && fileFormats[i].name) {
            fileFilter.Append(fileFormats[i].name);
            fileFilter.Append(L'\1');
            fileFilter.Append(fileFormats[i].filter);
            fileFilter.Append(L'\1');
        }
    }
    fileFilter.Append(_TR("All files"));
    fileFilter.Append(L"\1*.*\1");
    str::TransChars(fileFilter.Get(), L"\1", L"\0");

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetSumatraWindowHwnd(win);

    ofn.lpstrFilter = fileFilter.Get();
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY |
                OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    // OFN_ENABLEHOOK disables the new Open File dialog under Windows Vista
    // and later, so don't use it and just allocate enough memory to contain
    // several dozen file paths and hope that this is enough
    // TODO: Use IFileOpenDialog instead (requires a Vista SDK, though)
    ofn.nMaxFile = MAX_PATH * 100;
#if 0
    if (!IsVistaOrGreater())
    {
        ofn.lpfnHook = FileOpenHook;
        ofn.Flags |= OFN_ENABLEHOOK;
        ofn.nMaxFile = MAX_PATH / 2;
    }
    // note: ofn.lpstrFile can be reallocated by GetOpenFileName -> FileOpenHook
#endif
    ScopedMem<WCHAR> file(AllocArray<WCHAR>(ofn.nMaxFile));
    ofn.lpstrFile = file;

    if (!GetOpenFileName(&ofn))
        return;

    WCHAR *fileName = ofn.lpstrFile + ofn.nFileOffset;
    if (*(fileName - 1)) {
        // special case: single filename without NULL separator
        LoadDocument2(ofn.lpstrFile, win);
        return;
    }

    while (*fileName) {
        ScopedMem<WCHAR> filePath(path::Join(ofn.lpstrFile, fileName));
        if (filePath)
            LoadDocument2(filePath, win);
        fileName += str::Len(fileName) + 1;
    }
}

static void BrowseFolder(WindowInfo& win, bool forward)
{
    AssertCrash(win.loadedFilePath);
    if (win.IsAboutWindow()) return;
    if (!HasPermission(Perm_DiskAccess) || gPluginMode) return;

    WStrVec files;
    ScopedMem<WCHAR> pattern(path::GetDir(win.loadedFilePath));
    // TODO: make pattern configurable (for users who e.g. want to skip single images)?
    pattern.Set(path::Join(pattern, L"*"));
    if (!CollectPathsFromDirectory(pattern, files))
        return;

    // remove unsupported files that have never been successfully loaded
    for (size_t i = files.Count(); i > 0; i--) {
        if (!EngineManager::IsSupportedFile(files.At(i - 1), false, gGlobalPrefs->ebookUI.useFixedPageUI) &&
            !gFileHistory.Find(files.At(i - 1))) {
            WCHAR *path = files.At(i - 1);
            files.RemoveAt(i - 1);
            free(path);
        }
    }

    if (!files.Contains(win.loadedFilePath))
        files.Append(str::Dup(win.loadedFilePath));
    files.SortNatural();

    int index = files.Find(win.loadedFilePath);
    if (forward)
        index = (index + 1) % files.Count();
    else
        index = (int)(index + files.Count() - 1) % files.Count();

    // TODO: check for unsaved modifications
    UpdateCurrentFileDisplayStateForWin(SumatraWindow::Make(&win));
    LoadArgs args(files.At(index), &win);
    args.forceReuse = true;
    LoadDocument(args);
}

// scrolls half a page down/up (needed for Shift+Up/Down)
#define SB_HPAGEUP   (WM_USER + 1)
#define SB_HPAGEDOWN (WM_USER + 2)

static void AdjustWindowEdge(WindowInfo& win)
{
    DWORD exStyle = GetWindowLong(win.hwndCanvas, GWL_EXSTYLE);
    DWORD newStyle = exStyle;

    // Remove the canvas' edge in the cases where the vertical scrollbar
    // would otherwise touch the screen's edge, making the scrollbar much
    // easier to hit with the mouse (cf. Fitts' law)
    // TODO: should we just always remove the canvas' edge?
    if (IsZoomed(win.hwndFrame) || win.isFullScreen || win.presentation || gPluginMode)
        newStyle &= ~WS_EX_STATICEDGE;
    else
        newStyle |= WS_EX_STATICEDGE;

    if (newStyle != exStyle) {
        SetWindowLong(win.hwndCanvas, GWL_EXSTYLE, newStyle);
        SetWindowPos(win.hwndCanvas, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

static void FrameOnSize(WindowInfo* win, int dx, int dy)
{
    int rebBarDy = 0;
    if (gGlobalPrefs->showToolbar && !(win->presentation || win->isFullScreen || win->IsEbookLoaded())) {
        SetWindowPos(win->hwndReBar, NULL, 0, 0, dx, 0, SWP_NOZORDER);
        rebBarDy = WindowRect(win->hwndReBar).dy;
    }

    int tabBarDy = 0;
    if (gGlobalPrefs->showTabBar && !(win->presentation || win->isFullScreen)) {
        SetWindowPos(win->hwndTabBar, NULL, 0, rebBarDy, dx, TABBAR_HEIGHT, SWP_NOZORDER);
        UpdateTabWidth(win);
        tabBarDy = IsWindowVisible(win->hwndTabBar) ? TABBAR_HEIGHT : 0;
    }

    bool tocVisible = win->tocLoaded && win->tocVisible;
    if (tocVisible || gGlobalPrefs->showFavorites)
        SetSidebarVisibility(win, tocVisible, gGlobalPrefs->showFavorites);
    else
        SetWindowPos(win->hwndCanvas, NULL, 0, rebBarDy + tabBarDy, dx, dy - (rebBarDy + tabBarDy), SWP_NOZORDER);

    if (win->presentation || win->isFullScreen) {
        RectI fullscreen = GetFullscreenRect(win->hwndFrame);
        WindowRect rect(win->hwndFrame);
        // Windows XP sometimes seems to change the window size on it's own
        if (rect != fullscreen && rect != GetVirtualScreenRect())
            MoveWindow(win->hwndFrame, fullscreen);
    }
}

void SetCurrentLanguageAndRefreshUi(const char *langCode)
{
    if (!langCode || str::Eq(langCode, trans::GetCurrentLangCode()))
        return;
    SetCurrentLang(langCode);
    UpdateRtlLayoutForAllWindows();
    RebuildMenuBarForAllWindows();
    UpdateUITextForLanguage();
    if (gWindows.Count() > 0 && gWindows.At(0)->IsAboutWindow())
        gWindows.At(0)->RedrawAll(true);
    prefs::Save();
}

void OnMenuChangeLanguage(HWND hwnd)
{
    const char *newLangCode = Dialog_ChangeLanguge(hwnd, trans::GetCurrentLangCode());
    SetCurrentLanguageAndRefreshUi(newLangCode);
}

static void OnMenuViewShowHideToolbar(WindowInfo *win)
{
    if (win->presentation || win->isFullScreen)
        return;

    gGlobalPrefs->showToolbar = !gGlobalPrefs->showToolbar;
    ShowOrHideToolbarGlobally();
}

void OnMenuAdvancedOptions()
{
    if (!HasPermission(Perm_DiskAccess) || !HasPermission(Perm_SavePreferences))
        return;

    ScopedMem<WCHAR> path(prefs::GetSettingsPath());
    // TODO: disable/hide the menu item when there's no prefs file
    //       (happens e.g. when run in portable mode from a CD)?
    LaunchFile(path, NULL, L"open");
}

void OnMenuOptions(HWND hwnd)
{
    if (!HasPermission(Perm_SavePreferences)) return;

    if (IDOK != Dialog_Settings(hwnd, gGlobalPrefs))
        return;

    if (!gGlobalPrefs->rememberOpenedFiles) {
        // TODO: also remove all favorites?
        gFileHistory.Clear(true);
        CleanUpThumbnailCache(gFileHistory);
    }
    UpdateDocumentColors();

    prefs::Save();
}

static void OnMenuOptions(WindowInfo& win)
{
    OnMenuOptions(win.hwndFrame);
    if (gWindows.Count() > 0 && gWindows.At(0)->IsAboutWindow())
        gWindows.At(0)->RedrawAll(true);
}

// toggles 'show pages continuously' state
static void OnMenuViewContinuous(WindowInfo& win)
{
    if (!win.IsDocLoaded())
        return;

    DisplayMode newMode = win.ctrl->GetDisplayMode();
    switch (newMode) {
        case DM_SINGLE_PAGE:
        case DM_CONTINUOUS:
            newMode = IsContinuous(newMode) ? DM_SINGLE_PAGE : DM_CONTINUOUS;
            break;
        case DM_FACING:
        case DM_CONTINUOUS_FACING:
            newMode = IsContinuous(newMode) ? DM_FACING : DM_CONTINUOUS_FACING;
            break;
        case DM_BOOK_VIEW:
        case DM_CONTINUOUS_BOOK_VIEW:
            newMode = IsContinuous(newMode) ? DM_BOOK_VIEW : DM_CONTINUOUS_BOOK_VIEW;
            break;
    }
    SwitchToDisplayMode(&win, newMode);
}

static void OnMenuViewMangaMode(WindowInfo *win)
{
    CrashIf(!win->IsCbx());
    if (!win->IsCbx()) return;
    DisplayModel *dm = win->AsFixed()->model();
    dm->SetDisplayR2L(!dm->GetDisplayR2L());
    ScrollState state = dm->GetScrollState();
    dm->Relayout(dm->ZoomVirtual(), dm->Rotation());
    dm->SetScrollState(state);
}

static void ChangeZoomLevel(WindowInfo *win, float newZoom, bool pagesContinuously)
{
    if (!win->IsDocLoaded())
        return;

    float zoom = win->ctrl->GetZoomVirtual();
    DisplayMode mode = win->ctrl->GetDisplayMode();
    DisplayMode newMode = pagesContinuously ? DM_CONTINUOUS : DM_SINGLE_PAGE;

    if (mode != newMode || zoom != newZoom) {
        DisplayMode prevMode = win->prevDisplayMode;
        float prevZoom = win->prevZoomVirtual;

        if (mode != newMode)
            SwitchToDisplayMode(win, newMode);
        OnMenuZoom(win, MenuIdFromVirtualZoom(newZoom));

        // remember the previous values for when the toolbar button is unchecked
        if (INVALID_ZOOM == prevZoom) {
            win->prevZoomVirtual = zoom;
            win->prevDisplayMode = mode;
        }
        // keep the rememberd values when toggling between the two toolbar buttons
        else {
            win->prevZoomVirtual = prevZoom;
            win->prevDisplayMode = prevMode;
        }
    }
    else if (win->prevZoomVirtual != INVALID_ZOOM) {
        float prevZoom = win->prevZoomVirtual;
        SwitchToDisplayMode(win, win->prevDisplayMode);
        ZoomToSelection(win, prevZoom);
    }
}

static void FocusPageNoEdit(HWND hwndPageBox)
{
    if (GetFocus() == hwndPageBox)
        SendMessage(hwndPageBox, WM_SETFOCUS, 0, 0);
    else
        SetFocus(hwndPageBox);
}

static void OnMenuGoToPage(WindowInfo& win)
{
    if (!win.IsDocLoaded())
        return;

    // Don't show a dialog if we don't have to - use the Toolbar instead
    if (gGlobalPrefs->showToolbar && !win.isFullScreen && !win.presentation && !win.IsEbookLoaded()) {
        FocusPageNoEdit(win.hwndPageBox);
        return;
    }

    ScopedMem<WCHAR> label(win.ctrl->GetPageLabel(win.ctrl->CurrentPageNo()));
    ScopedMem<WCHAR> newPageLabel(Dialog_GoToPage(win.hwndFrame, label, win.ctrl->PageCount(),
                                                  !win.ctrl->HasPageLabels()));
    if (!newPageLabel)
        return;

    int newPageNo = win.ctrl->GetPageByLabel(newPageLabel);
    if (win.ctrl->ValidPageNo(newPageNo))
        win.ctrl->GoToPage(newPageNo, true);
}

void EnterFullScreen(WindowInfo& win, bool presentation)
{
    if (!HasPermission(Perm_FullscreenAccess))
        return;

    if ((presentation ? win.presentation : win.isFullScreen) ||
        !IsWindowVisible(win.hwndFrame) || gPluginMode)
        return;

    AssertCrash(presentation ? !win.isFullScreen : !win.presentation);
    if (presentation) {
        AssertCrash(win.ctrl);
        if (!win.IsDocLoaded())
            return;

        if (IsZoomed(win.hwndFrame))
            win.windowStateBeforePresentation = WIN_STATE_MAXIMIZED;
        else
            win.windowStateBeforePresentation = WIN_STATE_NORMAL;
        win.presentation = PM_ENABLED;
        win.tocBeforeFullScreen = win.tocVisible;

        SetTimer(win.hwndCanvas, HIDE_CURSOR_TIMER_ID, HIDE_CURSOR_DELAY_IN_MS, NULL);
    }
    else {
        win.isFullScreen = true;
        win.tocBeforeFullScreen = win.IsDocLoaded() ? win.tocVisible : false;
    }

    // Remove TOC and favorites from full screen, add back later on exit fullscreen
    bool showFavoritesTmp = gGlobalPrefs->showFavorites;
    if (win.tocVisible || gGlobalPrefs->showFavorites) {
        SetSidebarVisibility(&win, false, false);
        // restore gGlobalPrefs->showFavorites changed by SetSidebarVisibility()
    }

    long ws = GetWindowLong(win.hwndFrame, GWL_STYLE);
    if (!presentation || !win.isFullScreen)
        win.nonFullScreenWindowStyle = ws;
    ws &= ~(WS_BORDER|WS_CAPTION|WS_THICKFRAME);
    ws |= WS_MAXIMIZE;

    win.nonFullScreenFrameRect = WindowRect(win.hwndFrame);
    RectI rect = GetFullscreenRect(win.hwndFrame);

    SetMenu(win.hwndFrame, NULL);
    ShowWindow(win.hwndReBar, SW_HIDE);
    ShowWindow(win.hwndTabBar, SW_HIDE);

    SetWindowLong(win.hwndFrame, GWL_STYLE, ws);
    SetWindowPos(win.hwndFrame, NULL, rect.x, rect.y, rect.dx, rect.dy, SWP_FRAMECHANGED | SWP_NOZORDER);
    SetWindowPos(win.hwndCanvas, NULL, 0, 0, rect.dx, rect.dy, SWP_NOZORDER);

    if (presentation)
        win.ctrl->SetPresentationMode(true);

    // Make sure that no toolbar/sidebar keeps the focus
    SetFocus(win.hwndFrame);
    gGlobalPrefs->showFavorites = showFavoritesTmp;
}

void ExitFullScreen(WindowInfo& win)
{
    if (!win.isFullScreen && !win.presentation)
        return;

    bool wasPresentation = PM_DISABLED != win.presentation;
    if (wasPresentation && win.IsDocLoaded()) {
        win.ctrl->SetPresentationMode(false);
        win.presentation = PM_DISABLED;
    }
    else
        win.isFullScreen = false;

    if (wasPresentation) {
        KillTimer(win.hwndCanvas, HIDE_CURSOR_TIMER_ID);
        SetCursor(gCursorArrow);
    }

    bool tocVisible = win.IsDocLoaded() && win.tocBeforeFullScreen;
    if (tocVisible || gGlobalPrefs->showFavorites)
        SetSidebarVisibility(&win, tocVisible, gGlobalPrefs->showFavorites);

    if (gGlobalPrefs->showToolbar && !win.IsEbookLoaded())
        ShowWindow(win.hwndReBar, SW_SHOW);
    if (gGlobalPrefs->showTabBar)
        ShowWindow(win.hwndTabBar, SW_SHOW);
    if (!win.isMenuHidden)
        SetMenu(win.hwndFrame, win.menu);

    SetWindowLong(win.hwndFrame, GWL_STYLE, win.nonFullScreenWindowStyle);
    UINT flags = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE;
    SetWindowPos(win.hwndFrame, NULL, 0, 0, 0, 0, flags);
    MoveWindow(win.hwndFrame, win.nonFullScreenFrameRect);
    CrashIf(WindowRect(win.hwndFrame) != win.nonFullScreenFrameRect);
}

static void OnMenuViewFullscreen(WindowInfo& win, bool presentation=false)
{
    bool enterFullScreen = presentation ? !win.presentation : !win.isFullScreen;

    if (!win.presentation && !win.isFullScreen)
        RememberDefaultWindowPosition(win);
    else
        ExitFullScreen(win);

    if (enterFullScreen && (!presentation || win.IsDocLoaded()))
        EnterFullScreen(win, presentation);
}

static void OnMenuViewPresentation(WindowInfo& win)
{
    OnMenuViewFullscreen(win, true);
}

void AdvanceFocus(WindowInfo* win)
{
    // Tab order: Frame -> Page -> Find -> ToC -> Favorites -> Frame -> ...

    bool hasToolbar = !win->isFullScreen && !win->presentation && !win->IsEbookLoaded() &&
                      gGlobalPrefs->showToolbar && win->IsDocLoaded();
    int direction = IsShiftPressed() ? -1 : 1;

    struct {
        HWND hwnd;
        bool isAvailable;
    } tabOrder[] = {
        { win->hwndFrame,   true                                },
        { win->hwndPageBox, hasToolbar                          },
        { win->hwndFindBox, hasToolbar && NeedsFindUI(win)      },
        { win->hwndTocTree, win->tocLoaded && win->tocVisible   },
        { win->hwndFavTree, gGlobalPrefs->showFavorites         },
    };

    /* // make sure that at least one element is available
    bool hasAvailable = false;
    for (int i = 0; i < dimof(tabOrder) && !hasAvailable; i++)
        hasAvailable = tabOrder[i].isAvailable;
    if (!hasAvailable)
        return;
    // */

    // find the currently focused element
    HWND current = GetFocus();
    int ix;
    for (ix = 0; ix < dimof(tabOrder); ix++)
        if (tabOrder[ix].hwnd == current)
            break;
    // if it's not in the tab order, start at the beginning
    if (ix == dimof(tabOrder))
        ix = -direction;

    // focus the next available element
    do {
        ix = (ix + direction + dimof(tabOrder)) % dimof(tabOrder);
    } while (!tabOrder[ix].isAvailable);
    SetFocus(tabOrder[ix].hwnd);
}

// allow to distinguish a '/' caused by VK_DIVIDE (rotates a document)
// from one typed on the main keyboard (focuses the find textbox)
static bool gIsDivideKeyDown = false;

static bool ChmForwardKey(WPARAM key)
{
    if ((VK_LEFT == key) || (VK_RIGHT == key))
        return true;
    if ((VK_UP == key) || (VK_DOWN == key))
        return true;
    if ((VK_HOME == key) || (VK_END == key))
        return true;
    if ((VK_PRIOR == key) || (VK_NEXT == key))
        return true;
    if ((VK_MULTIPLY == key) || (VK_DIVIDE == key))
        return true;
    return false;
}

bool FrameOnKeydown(WindowInfo *win, WPARAM key, LPARAM lparam, bool inTextfield)
{
    bool isCtrl = IsCtrlPressed();
    bool isShift = IsShiftPressed();

    if (gGlobalPrefs->showTabBar && isCtrl && VK_TAB == key) {
        TabsOnCtrlTab(win);
        return true;
    }

    if ((VK_LEFT == key || VK_RIGHT == key) &&
        isShift && isCtrl &&
        win->loadedFilePath && !inTextfield) {
        // folder browsing should also work when an error page is displayed,
        // so special-case it before the win->IsDocLoaded() check
        BrowseFolder(*win, VK_RIGHT == key);
        return true;
    }

    if (!win->IsDocLoaded())
        return false;

    DisplayModel *dm = win->IsFixedDocLoaded() ? win->AsFixed()->model() : NULL;
    // some of the chm key bindings are different than the rest and we
    // need to make sure we don't break them
    bool isChm = win->IsChm();

    bool isPageUp = (isCtrl && (VK_UP == key));
    if (!isChm)
        isPageUp |= (VK_PRIOR == key) && !isCtrl;
    if (isPageUp) {
        int currentPos = GetScrollPos(win->hwndCanvas, SB_VERT);
        if (win->ctrl->GetZoomVirtual() != ZOOM_FIT_CONTENT)
            SendMessage(win->hwndCanvas, WM_VSCROLL, SB_PAGEUP, 0);
        if (GetScrollPos(win->hwndCanvas, SB_VERT) == currentPos)
            win->ctrl->GoToPrevPage(true);
        return true;
    }

    bool isPageDown = (isCtrl && (VK_DOWN == key));
    if (!isChm)
        isPageDown |= (VK_NEXT == key) && !isCtrl;
    if (isPageDown) {
        int currentPos = GetScrollPos(win->hwndCanvas, SB_VERT);
        if (win->ctrl->GetZoomVirtual() != ZOOM_FIT_CONTENT)
            SendMessage(win->hwndCanvas, WM_VSCROLL, SB_PAGEDOWN, 0);
        if (GetScrollPos(win->hwndCanvas, SB_VERT) == currentPos)
            win->ctrl->GoToNextPage();
        return true;
    }

    if (isChm) {
        if (ChmForwardKey(key)) {
            win->AsChm()->engine()->PassUIMsg(WM_KEYDOWN, key, lparam);
            return true;
        }
    }
    //lf("key=%d,%c,shift=%d\n", key, (char)key, (int)WasKeyDown(VK_SHIFT));

    if (PM_BLACK_SCREEN == win->presentation || PM_WHITE_SCREEN == win->presentation)
        return false;

    if (VK_UP == key) {
        if (dm && dm->NeedVScroll())
            SendMessage(win->hwndCanvas, WM_VSCROLL, isShift ? SB_HPAGEUP : SB_LINEUP, 0);
        else
            win->ctrl->GoToPrevPage(true);
    } else if (VK_DOWN == key) {
        if (dm && dm->NeedVScroll())
            SendMessage(win->hwndCanvas, WM_VSCROLL, isShift ? SB_HPAGEDOWN : SB_LINEDOWN, 0);
        else
            win->ctrl->GoToNextPage();
    } else if (VK_PRIOR == key && isCtrl) {
        win->ctrl->GoToPrevPage();
    } else if (VK_NEXT == key && isCtrl) {
        win->ctrl->GoToNextPage();
    } else if (VK_HOME == key && isCtrl) {
        win->ctrl->GoToFirstPage();
    } else if (VK_END == key && isCtrl) {
        if (!win->ctrl->GoToLastPage())
            SendMessage(win->hwndCanvas, WM_VSCROLL, SB_BOTTOM, 0);
    } else if (inTextfield) {
        // The remaining keys have a different meaning
        return false;
    } else if (VK_LEFT == key) {
        if (dm && dm->NeedHScroll() && !isCtrl)
            SendMessage(win->hwndCanvas, WM_HSCROLL, isShift ? SB_PAGELEFT : SB_LINELEFT, 0);
        else
            win->ctrl->GoToPrevPage();
    } else if (VK_RIGHT == key) {
        if (dm && dm->NeedHScroll() && !isCtrl)
            SendMessage(win->hwndCanvas, WM_HSCROLL, isShift ? SB_PAGERIGHT : SB_LINERIGHT, 0);
        else
            win->ctrl->GoToNextPage();
    } else if (VK_HOME == key) {
        win->ctrl->GoToFirstPage();
    } else if (VK_END == key) {
        if (!win->ctrl->GoToLastPage())
            SendMessage(win->hwndCanvas, WM_VSCROLL, SB_BOTTOM, 0);
    } else if (VK_MULTIPLY == key && dm) {
        dm->RotateBy(90);
    } else if (VK_DIVIDE == key && dm) {
        dm->RotateBy(-90);
        gIsDivideKeyDown = true;
    } else {
        return false;
    }

    return true;
}

static void FrameOnChar(WindowInfo& win, WPARAM key)
{
    if (IsCharUpper((WCHAR)key))
        key = (WCHAR)CharLower((LPWSTR)(WCHAR)key);

    if (PM_BLACK_SCREEN == win.presentation || PM_WHITE_SCREEN == win.presentation) {
        win.ChangePresentationMode(PM_ENABLED);
        return;
    }

    switch (key) {
    case VK_ESCAPE:
        if (win.findThread)
            AbortFinding(&win);
        else if (win.notifications->GetFirstInGroup(NG_PAGE_INFO_HELPER))
            win.notifications->RemoveAllInGroup(NG_PAGE_INFO_HELPER);
        else if (win.presentation)
            OnMenuViewPresentation(win);
        else if (gGlobalPrefs->escToExit)
            CloseWindow(&win, true);
        else if (win.isFullScreen)
            OnMenuViewFullscreen(win);
        else if (win.showSelection)
            ClearSearchResult(&win);
        return;
    case 'q':
        // close the current document/window. Quit if this is the last window
        CloseWindow(&win, true);
        return;
    case 'r':
        ReloadDocument(&win);
        return;
    case VK_TAB:
        AdvanceFocus(&win);
        break;
    }

    if (!win.IsDocLoaded())
        return;

    switch (key) {
    case VK_SPACE:
    case VK_RETURN:
        FrameOnKeydown(&win, IsShiftPressed() ? VK_PRIOR : VK_NEXT, 0);
        break;
    case VK_BACK:
        {
            bool forward = IsShiftPressed();
            win.ctrl->Navigate(forward ? 1 : -1);
        }
        break;
    case 'g':
        OnMenuGoToPage(win);
        break;
    case 'j':
        FrameOnKeydown(&win, VK_DOWN, 0);
        break;
    case 'k':
        FrameOnKeydown(&win, VK_UP, 0);
        break;
    case 'n':
        win.ctrl->GoToNextPage();
        break;
    case 'p':
        win.ctrl->GoToPrevPage();
        break;
    case 'z':
        win.ToggleZoom();
        break;
    // per http://en.wikipedia.org/wiki/Keyboard_layout
    // almost all keyboard layouts allow to press either
    // '+' or '=' unshifted (and one of them is also often
    // close to '-'); the other two alternatives are for
    // the major exception: the two Swiss layouts
    case '+': case '=': case 0xE0: case 0xE4:
        ZoomToSelection(&win, win.ctrl->GetNextZoomStep(ZOOM_MAX), false);
        break;
    case '-':
        ZoomToSelection(&win, win.ctrl->GetNextZoomStep(ZOOM_MIN), false);
        break;
    case '/':
        if (!gIsDivideKeyDown)
            OnMenuFind(&win);
        gIsDivideKeyDown = false;
        break;
    case 'c':
        OnMenuViewContinuous(win);
        break;
    case 'b':
        if (win.IsFixedDocLoaded() && !IsSingle(win.ctrl->GetDisplayMode())) {
            // "e-book view": flip a single page
            bool forward = !IsShiftPressed();
            int currPage = win.ctrl->CurrentPageNo();
            if (forward ? win.AsFixed()->model()->LastBookPageVisible() : win.AsFixed()->model()->FirstBookPageVisible())
                break;

            DisplayMode newMode = DM_BOOK_VIEW;
            if (DisplayModeShowCover(win.ctrl->GetDisplayMode()))
                newMode = DM_FACING;
            SwitchToDisplayMode(&win, newMode, true);

            if (forward && currPage >= win.ctrl->CurrentPageNo() && (currPage > 1 || newMode == DM_BOOK_VIEW))
                win.ctrl->GoToNextPage();
            else if (!forward && currPage <= win.ctrl->CurrentPageNo())
                win.ctrl->GoToPrevPage();
        }
        else if (win.IsEbookLoaded() && !IsSingle(win.ctrl->GetDisplayMode())) {
            // "e-book view": flip a single page
            bool forward = !IsShiftPressed();
            int nextPage = win.ctrl->CurrentPageNo() + (forward ? 1 : -1);
            if (win.ctrl->ValidPageNo(nextPage))
                win.ctrl->GoToPage(nextPage, false);
        }
        else if (win.presentation)
            win.ChangePresentationMode(PM_BLACK_SCREEN);
        break;
    case '.':
        // for Logitech's wireless presenters which target PowerPoint's shortcuts
        if (win.presentation)
            win.ChangePresentationMode(PM_BLACK_SCREEN);
        break;
    case 'w':
        if (win.presentation)
            win.ChangePresentationMode(PM_WHITE_SCREEN);
        break;
    case 'i':
        // experimental "page info" tip: make figuring out current page and
        // total pages count a one-key action (unless they're already visible)
        if (!gGlobalPrefs->showToolbar || win.isFullScreen || PM_ENABLED == win.presentation) {
            int current = win.ctrl->CurrentPageNo(), total = win.ctrl->PageCount();
            ScopedMem<WCHAR> pageInfo(str::Format(L"%s %d / %d", _TR("Page:"), current, total));
            if (win.ctrl->HasPageLabels()) {
                ScopedMem<WCHAR> label(win.ctrl->GetPageLabel(current));
                pageInfo.Set(str::Format(L"%s %s (%d / %d)", _TR("Page:"), label, current, total));
            }
            bool autoDismiss = !IsShiftPressed();
            ShowNotification(&win, pageInfo, autoDismiss, false, NG_PAGE_INFO_HELPER);
        }
        break;
#ifdef DEBUG
    case '$':
        ToggleGdiDebugging();
        break;
#endif
#if defined(DEBUG) || defined(SVN_PRE_RELEASE_VER)
    case 'h': // convert selection to highlight annotation
        if (win.IsFixedDocLoaded() && win.AsFixed()->engine()->SupportsAnnotation() && win.showSelection && win.selectionOnPage) {
            if (!win.AsFixed()->userAnnots)
                win.AsFixed()->userAnnots = new Vec<PageAnnotation>();
            for (size_t i = 0; i < win.selectionOnPage->Count(); i++) {
                SelectionOnPage& sel = win.selectionOnPage->At(i);
                win.AsFixed()->userAnnots->Append(PageAnnotation(Annot_Highlight, sel.pageNo, sel.rect, PageAnnotation::Color(gGlobalPrefs->annotationDefaults.highlightColor, 0xCC)));
                gRenderCache.Invalidate(win.AsFixed()->model(), sel.pageNo, sel.rect);
            }
            win.AsFixed()->userAnnotsModified = true;
            win.AsFixed()->engine()->UpdateUserAnnotations(win.AsFixed()->userAnnots);
            ClearSearchResult(&win); // causes invalidated tiles to be rerendered
        }
#endif
    }
}

static void UpdateSidebarTitles(WindowInfo& win)
{
    HWND tocTitle = GetDlgItem(win.hwndTocBox, IDC_TOC_TITLE);
    win::SetText(tocTitle, _TR("Bookmarks"));
    if (win.tocVisible) {
        InvalidateRect(win.hwndTocBox, NULL, TRUE);
        UpdateWindow(win.hwndTocBox);
    }

    HWND favTitle = GetDlgItem(win.hwndFavBox, IDC_FAV_TITLE);
    win::SetText(favTitle, _TR("Favorites"));
    if (gGlobalPrefs->showFavorites) {
        InvalidateRect(win.hwndFavBox, NULL, TRUE);
        UpdateWindow(win.hwndFavBox);
    }
}

static void UpdateUITextForLanguage()
{
    for (size_t i = 0; i < gWindows.Count(); i++) {
        WindowInfo *win = gWindows.At(i);
        UpdateToolbarPageText(win, -1);
        UpdateToolbarFindText(win);
        UpdateToolbarButtonsToolTipsForWindow(win);
        // also update the sidebar title at this point
        UpdateSidebarTitles(*win);
    }
}

// TODO: the layout logic here is similar to what we do in SetSidebarVisibility()
// would be nice to consolidate.
static void ResizeSidebar(WindowInfo *win)
{
    POINT pcur;
    GetCursorPosInHwnd(win->hwndFrame, pcur);
    int sidebarDx = pcur.x; // without splitter

    ClientRect rToc(win->hwndTocBox);
    ClientRect rFav(win->hwndFavBox);
    AssertCrash(rToc.dx == rFav.dx);
    ClientRect rFrame(win->hwndFrame);

    // make sure to keep this in sync with the calculations in SetSidebarVisibility
    // note: without the min/max(..., rToc.dx), the sidebar will be
    //       stuck at its width if it accidentally got too wide or too narrow
    if (sidebarDx < min(SIDEBAR_MIN_WIDTH, rToc.dx) ||
        sidebarDx > max(rFrame.dx / 2, rToc.dx)) {
        SetCursor(gCursorNo);
        return;
    }

    SetCursor(gCursorSizeWE);

    int favSplitterDy = 0;
    bool favSplitterVisible = win->tocVisible && gGlobalPrefs->showFavorites;
    if (favSplitterVisible)
        favSplitterDy = SPLITTER_DY;

    int canvasDx = rFrame.dx - sidebarDx - SPLITTER_DX;
    int y = 0;
    int totalDy = rFrame.dy;
    if (gGlobalPrefs->showToolbar && !win->isFullScreen && !win->presentation && !win->IsEbookLoaded())
        y = WindowRect(win->hwndReBar).dy;
    if (gGlobalPrefs->showTabBar && !win->isFullScreen && !win->presentation)
        y += IsWindowVisible(win->hwndTabBar) ? TABBAR_HEIGHT : 0;
    totalDy -= y;

    // rToc.y is always 0, as rToc is a ClientRect, so we first have
    // to convert it into coordinates relative to hwndFrame:
    AssertCrash(MapRectToWindow(rToc, win->hwndTocBox, win->hwndFrame).y == y);
    //AssertCrash(totalDy == (rToc.dy + rFav.dy));

    MoveWindow(win->hwndTocBox,      0, y,                           sidebarDx, rToc.dy, TRUE);
    MoveWindow(win->hwndFavSplitter, 0, y + rToc.dy,                 sidebarDx, favSplitterDy, TRUE);
    MoveWindow(win->hwndFavBox,      0, y + rToc.dy + favSplitterDy, sidebarDx, rFav.dy, TRUE);

    MoveWindow(win->hwndSidebarSplitter, sidebarDx, y, SPLITTER_DX, totalDy, TRUE);
    MoveWindow(win->hwndCanvas, sidebarDx + SPLITTER_DX, y, canvasDx, totalDy, TRUE);
}

// TODO: the layout logic here is similar to what we do in SetSidebarVisibility()
// would be nice to consolidate.
static void ResizeFav(WindowInfo *win)
{
    POINT pcur;
    GetCursorPosInHwnd(win->hwndTocBox, pcur);
    int tocDy = pcur.y; // without splitter

    ClientRect rToc(win->hwndTocBox);
    ClientRect rFav(win->hwndFavBox);
    AssertCrash(rToc.dx == rFav.dx);
    ClientRect rFrame(win->hwndFrame);
    int tocDx = rToc.dx;

    // make sure to keep this in sync with the calculations in SetSidebarVisibility
    if (tocDy < min(TOC_MIN_DY, rToc.dy) ||
        tocDy > max(rFrame.dy - TOC_MIN_DY, rToc.dy)) {
        SetCursor(gCursorNo);
        return;
    }

    SetCursor(gCursorSizeNS);

    int y = 0;
    int totalDy = rFrame.dy;
    if (gGlobalPrefs->showToolbar && !win->isFullScreen && !win->presentation && !win->IsEbookLoaded())
        y = WindowRect(win->hwndReBar).dy;
    if (gGlobalPrefs->showTabBar && !win->isFullScreen && !win->presentation)
        y += IsWindowVisible(win->hwndTabBar) ? TABBAR_HEIGHT : 0;
    totalDy -= y;

    // rToc.y is always 0, as rToc is a ClientRect, so we first have
    // to convert it into coordinates relative to hwndFrame:
    AssertCrash(MapRectToWindow(rToc, win->hwndTocBox, win->hwndFrame).y == y);
    //AssertCrash(totalDy == (rToc.dy + rFav.dy));
    int favDy = totalDy - tocDy - SPLITTER_DY;
    AssertCrash(favDy >= 0);

    MoveWindow(win->hwndTocBox,      0, y,                       tocDx, tocDy,       TRUE);
    MoveWindow(win->hwndFavSplitter, 0, y + tocDy,               tocDx, SPLITTER_DY, TRUE);
    MoveWindow(win->hwndFavBox,      0, y + tocDy + SPLITTER_DY, tocDx, favDy,       TRUE);

    gGlobalPrefs->tocDy = tocDy;
}

static LRESULT CALLBACK WndProcFavSplitter(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WindowInfo *win = FindWindowInfoByHwnd(hwnd);
    if (!win)
        return DefWindowProc(hwnd, message, wParam, lParam);

    switch (message)
    {
        case WM_MOUSEMOVE:
            if (hwnd == GetCapture()) {
                ResizeFav(win);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            break;
        case WM_LBUTTONUP:
            ReleaseCapture();
            break;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

static LRESULT CALLBACK WndProcSidebarSplitter(HWND hwnd, UINT message,
                                               WPARAM wParam, LPARAM lParam)
{
    WindowInfo *win = FindWindowInfoByHwnd(hwnd);
    if (!win)
        return DefWindowProc(hwnd, message, wParam, lParam);

    switch (message)
    {
        case WM_MOUSEMOVE:
            if (hwnd == GetCapture()) {
                ResizeSidebar(win);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            break;
        case WM_LBUTTONUP:
            ReleaseCapture();
            break;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

// A tree container, used for toc and favorites, is a window with following children:
// - title (id + 1)
// - close button (id + 2)
// - tree window (id + 3)
// This function lays out the child windows inside the container based
// on the container size.
void LayoutTreeContainer(HWND hwndContainer, int id)
{
    HWND hwndTitle = GetDlgItem(hwndContainer, id + 1);
    HWND hwndClose = GetDlgItem(hwndContainer, id + 2);
    HWND hwndTree  = GetDlgItem(hwndContainer, id + 3);

    ScopedMem<WCHAR> title(win::GetText(hwndTitle));
    SizeI size = TextSizeInHwnd(hwndTitle, title);

    WindowInfo *win = FindWindowInfoByHwnd(hwndContainer);
    AssertCrash(win);
    int offset = win ? (int)(2 * win->uiDPIFactor) : 2;
    if (size.dy < 16)
        size.dy = 16;
    size.dy += 2 * offset;

    WindowRect rc(hwndContainer);
    MoveWindow(hwndTitle, offset, offset, rc.dx - 2 * offset - 16, size.dy - 2 * offset, TRUE);
    MoveWindow(hwndClose, rc.dx - 16, (size.dy - 16) / 2, 16, 16, TRUE);
    MoveWindow(hwndTree, 0, size.dy, rc.dx, rc.dy - size.dy, TRUE);
}

WNDPROC DefWndProcCloseButton = NULL;
// logic needed to track OnHover state of a close button by looking at
// WM_MOUSEMOVE and WM_MOUSELEAVE messages.
// We call it a button, but hwnd is really a static text.
// We persist the state by setting hwnd's text: if cursor is over the hwnd,
// text is "1", else it's something else.
LRESULT CALLBACK WndProcCloseButton(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    bool stateChanged = false;
    if (WM_MOUSEMOVE == msg) {
        ScopedMem<WCHAR> s(win::GetText(hwnd));
        if (!str::Eq(s, BUTTON_HOVER_TEXT)) {
            win::SetText(hwnd, BUTTON_HOVER_TEXT);
            stateChanged = true;
            // ask for WM_MOUSELEAVE notifications
            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
    } else if (WM_MOUSELEAVE == msg) {
        win::SetText(hwnd, L"");
        stateChanged = true;
    } else if (WM_ERASEBKGND == msg) {
        return FALSE;
    }

    if (stateChanged) {
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
    }

    return CallWindowProc(DefWndProcCloseButton, hwnd, msg, wParam, lParam);
}

static void SetWinPos(HWND hwnd, RectI r, bool isVisible)
{
    UINT flags = SWP_NOZORDER | (isVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
    SetWindowPos(hwnd, NULL, r.x, r.y, r.dx, r.dy, flags);
}

void SetSidebarVisibility(WindowInfo *win, bool tocVisible, bool showFavorites)
{
    if (gPluginMode || !HasPermission(Perm_DiskAccess))
        showFavorites = false;

    if (!win->IsDocLoaded() || !win->ctrl->HasTocTree())
        tocVisible = false;

    if (PM_BLACK_SCREEN == win->presentation || PM_WHITE_SCREEN == win->presentation) {
        tocVisible = false;
        showFavorites = false;
    }

    bool sidebarVisible = tocVisible || showFavorites;

    if (tocVisible)
        LoadTocTree(win);
    if (showFavorites)
        PopulateFavTreeIfNeeded(win);

    win->tocVisible = tocVisible;
    gGlobalPrefs->showFavorites = showFavorites;

    ClientRect rFrame(win->hwndFrame);
    int toolbarDy = 0;
    if (gGlobalPrefs->showToolbar && !win->isFullScreen && !win->presentation && !win->IsEbookLoaded())
        toolbarDy = WindowRect(win->hwndReBar).dy;

    int tabBarDy = 0;
    if (gGlobalPrefs->showTabBar && !win->isFullScreen && !win->presentation)
        tabBarDy = IsWindowVisible(win->hwndTabBar) ? TABBAR_HEIGHT : 0;

    int dy = rFrame.dy - (toolbarDy + tabBarDy);

    if (!sidebarVisible) {
        if (GetFocus() == win->hwndTocTree || GetFocus() == win->hwndFavTree)
            SetFocus(win->hwndFrame);

        SetWindowPos(win->hwndCanvas, NULL, 0, toolbarDy + tabBarDy, rFrame.dx, dy, SWP_NOZORDER);
        ShowWindow(win->hwndSidebarSplitter, SW_HIDE);
        ShowWindow(win->hwndTocBox, SW_HIDE);
        ShowWindow(win->hwndFavSplitter, SW_HIDE);
        ShowWindow(win->hwndFavBox, SW_HIDE);
        return;
    }

    if (rFrame.IsEmpty()) {
        // don't adjust the ToC sidebar size while the window is minimized
        if (win->tocVisible)
            UpdateTocSelection(win, win->ctrl->CurrentPageNo());
        return;
    }

    int y = toolbarDy + tabBarDy;
    ClientRect sidebarRc(win->hwndTocBox);
    int tocDx = sidebarRc.dx;
    if (tocDx == 0) {
        // TODO: use saved panelDx from saved preferences
        tocDx = rFrame.dx / 4;
    }

    // make sure that the sidebar is never too wide or too narrow
    // (when changing these values, also adjust ResizeSidebar() and ResizeFav())
    // TODO: we should also limit minimum size of the frame or else
    // limitValue() blows up with an assert() if frame.dx / 2 < SIDEBAR_MIN_WIDTH
    tocDx = limitValue(tocDx, SIDEBAR_MIN_WIDTH, rFrame.dx / 2);

    bool favSplitterVisible = tocVisible && showFavorites;

    int tocDy = 0; // if !tocVisible
    if (tocVisible) {
        if (!showFavorites)
            tocDy = dy;
        else if (gGlobalPrefs->tocDy)
            tocDy = gGlobalPrefs->tocDy;
        else
            tocDy = dy / 2; // default value
    }
    if (favSplitterVisible) {
        // TODO: we should also limit minimum size of the frame or else
        // limitValue() blows up with an assert() if TOC_MIN_DY < dy - TOC_MIN_DY
        tocDy = limitValue(tocDy, TOC_MIN_DY, dy-TOC_MIN_DY);
    }

    int canvasX = tocDx + SPLITTER_DX;
    RectI rToc(0, y, tocDx, tocDy);
    RectI rFavSplitter(0, y + tocDy, tocDx, SPLITTER_DY);
    int favSplitterDy = favSplitterVisible ? SPLITTER_DY : 0;
    RectI rFav(0, y + tocDy + favSplitterDy, tocDx, dy - tocDy - favSplitterDy);

    RectI rSplitter(tocDx, y, SPLITTER_DX, dy);
    RectI rCanvas(canvasX, y, rFrame.dx - canvasX, dy);

    SetWinPos(win->hwndTocBox,          rToc,           tocVisible);
    SetWinPos(win->hwndFavSplitter,     rFavSplitter,   favSplitterVisible);
    SetWinPos(win->hwndFavBox,          rFav,           showFavorites);
    SetWinPos(win->hwndSidebarSplitter, rSplitter,      true);
    SetWinPos(win->hwndCanvas,          rCanvas,        true);

    if (tocVisible)
        UpdateTocSelection(win, win->ctrl->CurrentPageNo());
}

// Tests that various ways to crash will generate crash report.
// Commented-out because they are ad-hoc. Left in code because
// I don't want to write them again if I ever need to test crash reporting
#if 0
#include <signal.h>
static void TestCrashAbort()
{
    raise(SIGABRT);
}

struct Base;
void foo(Base* b);

struct Base {
    Base() {
        foo(this);
    }
    virtual ~Base() = 0;
    virtual void pure() = 0;
};
struct Derived : public Base {
    void pure() { }
};

void foo(Base* b) {
    b->pure();
}

static void TestCrashPureCall()
{
    Derived d; // should crash
}

// tests that making a big allocation with new raises an exception
static int TestBigNew()
{
    size_t size = 1024*1024*1024*1;  // 1 GB should be out of reach
    char *mem = (char*)1;
    while (mem) {
        mem = new char[size];
    }
    // just some code so that compiler doesn't optimize this code to null
    for (size_t i = 0; i < 1024; i++) {
        mem[i] = i & 0xff;
    }
    int res = 0;
    for (size_t i = 0; i < 1024; i++) {
        res += mem[i];
    }
    return res;
}
#endif

static LRESULT FrameOnCommand(WindowInfo *win, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int wmId = LOWORD(wParam);

    // check if the menuId belongs to an entry in the list of
    // recently opened files and load the referenced file if it does
    if ((wmId >= IDM_FILE_HISTORY_FIRST) && (wmId <= IDM_FILE_HISTORY_LAST))
    {
        DisplayState *state = gFileHistory.Get(wmId - IDM_FILE_HISTORY_FIRST);
        if (state && HasPermission(Perm_DiskAccess)) {
            LoadArgs args(state->filePath, win);
            LoadDocument(args);
        }
        return 0;
    }

    if (win && IDM_OPEN_WITH_EXTERNAL_FIRST <= wmId && wmId <= IDM_OPEN_WITH_EXTERNAL_LAST)
    {
        ViewWithExternalViewer(wmId - IDM_OPEN_WITH_EXTERNAL_FIRST, win->loadedFilePath, win->ctrl ? win->ctrl->CurrentPageNo() : 0);
        return 0;
    }

    // 10 submenus max with 10 items each max (=100) plus generous buffer => 200
    STATIC_ASSERT(IDM_FAV_LAST - IDM_FAV_FIRST == 200, enough_fav_menu_ids);
    if ((wmId >= IDM_FAV_FIRST) && (wmId <= IDM_FAV_LAST))
    {
        GoToFavoriteByMenuId(win, wmId);
    }

    if (!win)
        return DefWindowProc(hwnd, msg, wParam, lParam);

    // most of them require a win, the few exceptions are no-ops
    switch (wmId)
    {
        case IDM_OPEN:
        case IDT_FILE_OPEN:
            OnMenuOpen(SumatraWindow::Make(win));
            break;
        case IDM_SAVEAS:
            OnMenuSaveAs(*win);
            break;

        case IDM_RENAME_FILE:
            OnMenuRenameFile(*win);
            break;

        case IDT_FILE_PRINT:
        case IDM_PRINT:
            OnMenuPrint(win);
            break;

        case IDT_FILE_EXIT:
        case IDM_CLOSE:
            // close the document and its window, unless it's the last window
            // in which case we close the document but convert the window
            // to about window
            CloseWindow(win, false);
            break;

        case IDM_EXIT:
            OnMenuExit();
            break;

        case IDM_REFRESH:
            ReloadDocument(win);
            break;

        case IDM_SAVEAS_BOOKMARK:
            OnMenuSaveBookmark(*win);
            break;

        case IDT_VIEW_FIT_WIDTH:
            ChangeZoomLevel(win, ZOOM_FIT_WIDTH, true);
            break;

        case IDT_VIEW_FIT_PAGE:
            ChangeZoomLevel(win, ZOOM_FIT_PAGE, false);
            break;

        case IDT_VIEW_ZOOMIN:
            if (win->IsDocLoaded())
                ZoomToSelection(win, win->ctrl->GetNextZoomStep(ZOOM_MAX), false);
            break;

        case IDT_VIEW_ZOOMOUT:
            if (win->IsDocLoaded())
                ZoomToSelection(win, win->ctrl->GetNextZoomStep(ZOOM_MIN), false);
            break;

        case IDM_ZOOM_6400:
        case IDM_ZOOM_3200:
        case IDM_ZOOM_1600:
        case IDM_ZOOM_800:
        case IDM_ZOOM_400:
        case IDM_ZOOM_200:
        case IDM_ZOOM_150:
        case IDM_ZOOM_125:
        case IDM_ZOOM_100:
        case IDM_ZOOM_50:
        case IDM_ZOOM_25:
        case IDM_ZOOM_12_5:
        case IDM_ZOOM_8_33:
        case IDM_ZOOM_FIT_PAGE:
        case IDM_ZOOM_FIT_WIDTH:
        case IDM_ZOOM_FIT_CONTENT:
        case IDM_ZOOM_ACTUAL_SIZE:
            OnMenuZoom(win, wmId);
            break;

        case IDM_ZOOM_CUSTOM:
            OnMenuCustomZoom(win);
            break;

        case IDM_VIEW_SINGLE_PAGE:
            SwitchToDisplayMode(win, DM_SINGLE_PAGE, true);
            break;

        case IDM_VIEW_FACING:
            SwitchToDisplayMode(win, DM_FACING, true);
            break;

        case IDM_VIEW_BOOK:
            SwitchToDisplayMode(win, DM_BOOK_VIEW, true);
            break;

        case IDM_VIEW_CONTINUOUS:
            OnMenuViewContinuous(*win);
            break;

        case IDM_VIEW_MANGA_MODE:
            OnMenuViewMangaMode(win);
            break;

        case IDM_VIEW_SHOW_HIDE_TOOLBAR:
            OnMenuViewShowHideToolbar(win);
            break;

        case IDM_VIEW_SHOW_HIDE_MENUBAR:
            ShowHideMenuBar(win);
            break;

        case IDM_CHANGE_LANGUAGE:
            OnMenuChangeLanguage(win->hwndFrame);
            break;

        case IDM_VIEW_BOOKMARKS:
            ToggleTocBox(win);
            break;

        case IDM_GOTO_NEXT_PAGE:
            if (win->IsDocLoaded())
                win->ctrl->GoToNextPage();
            break;

        case IDM_GOTO_PREV_PAGE:
            if (win->IsDocLoaded())
                win->ctrl->GoToPrevPage();
            break;

        case IDM_GOTO_FIRST_PAGE:
            if (win->IsDocLoaded())
                win->ctrl->GoToFirstPage();
            break;

        case IDM_GOTO_LAST_PAGE:
            if (win->IsDocLoaded())
                win->ctrl->GoToLastPage();
            break;

        case IDM_GOTO_PAGE:
            OnMenuGoToPage(*win);
            break;

        case IDM_VIEW_PRESENTATION_MODE:
            OnMenuViewPresentation(*win);
            break;

        case IDM_VIEW_FULLSCREEN:
            OnMenuViewFullscreen(*win);
            break;

        case IDM_VIEW_ROTATE_LEFT:
            if (win->IsFixedDocLoaded())
                win->AsFixed()->model()->RotateBy(-90);
            break;

        case IDM_VIEW_ROTATE_RIGHT:
            if (win->IsFixedDocLoaded())
                win->AsFixed()->model()->RotateBy(90);
            break;

        case IDM_FIND_FIRST:
            OnMenuFind(win);
            break;

        case IDM_FIND_NEXT:
            OnMenuFindNext(win);
            break;

        case IDM_FIND_PREV:
            OnMenuFindPrev(win);
            break;

        case IDM_FIND_MATCH:
            OnMenuFindMatchCase(win);
            break;

        case IDM_FIND_NEXT_SEL:
            OnMenuFindSel(win, FIND_FORWARD);
            break;

        case IDM_FIND_PREV_SEL:
            OnMenuFindSel(win, FIND_BACKWARD);
            break;

        case IDM_VISIT_WEBSITE:
            LaunchBrowser(WEBSITE_MAIN_URL);
            break;

        case IDM_MANUAL:
            LaunchBrowser(WEBSITE_MANUAL_URL);
            break;

        case IDM_CONTRIBUTE_TRANSLATION:
            LaunchBrowser(WEBSITE_TRANSLATIONS_URL);
            break;

        case IDM_ABOUT:
#if 0
            OnMenuAbout2();
#else
            OnMenuAbout();
#endif
            break;

        case IDM_CHECK_UPDATE:
            AutoUpdateCheckAsync(win->hwndFrame, false);
            break;

        case IDM_OPTIONS:
            OnMenuOptions(*win);
            break;

        case IDM_ADVANCED_OPTIONS:
            OnMenuAdvancedOptions();
            break;

        case IDM_VIEW_WITH_ACROBAT:
            ViewWithAcrobat(win);
            break;

        case IDM_VIEW_WITH_FOXIT:
            ViewWithFoxit(win);
            break;

        case IDM_VIEW_WITH_PDF_XCHANGE:
            ViewWithPDFXChange(win);
            break;

        case IDM_VIEW_WITH_XPS_VIEWER:
            ViewWithXPSViewer(win);
            break;

        case IDM_VIEW_WITH_HTML_HELP:
            ViewWithHtmlHelp(win);
            break;

        case IDM_SEND_BY_EMAIL:
            SendAsEmailAttachment(win);
            break;

        case IDM_PROPERTIES:
            OnMenuProperties(SumatraWindow::Make(win));
            break;

        case IDM_MOVE_FRAME_FOCUS:
            if (win->hwndFrame != GetFocus())
                SetFocus(win->hwndFrame);
            else if (win->tocVisible)
                SetFocus(win->hwndTocTree);
            break;

        case IDM_GOTO_NAV_BACK:
            if (win->IsDocLoaded())
                win->ctrl->Navigate(-1);
            break;

        case IDM_GOTO_NAV_FORWARD:
            if (win->IsDocLoaded())
                win->ctrl->Navigate(1);
            break;

        case IDM_COPY_SELECTION:
            // Don't break the shortcut for text boxes
            if (win->hwndFindBox == GetFocus() || win->hwndPageBox == GetFocus())
                SendMessage(GetFocus(), WM_COPY, 0, 0);
            else if (!HasPermission(Perm_CopySelection))
                break;
            else if (win->IsChm())
                win->AsChm()->engine()->CopySelection();
            else if (win->selectionOnPage)
                CopySelectionToClipboard(win);
            else
                ShowNotification(win, _TR("Select content with Ctrl+left mouse button"));
            break;

        case IDM_SELECT_ALL:
            OnSelectAll(win);
            break;

#ifdef SHOW_DEBUG_MENU_ITEMS
        case IDM_DEBUG_SHOW_LINKS:
            gDebugShowLinks = !gDebugShowLinks;
            for (size_t i = 0; i < gWindows.Count(); i++)
                gWindows.At(i)->RedrawAll(true);
            break;

        case IDM_DEBUG_GDI_RENDERER:
            ToggleGdiDebugging();
            break;

        case IDM_DEBUG_EBOOK_UI:
            gGlobalPrefs->ebookUI.useFixedPageUI = !gGlobalPrefs->ebookUI.useFixedPageUI;
            // use the same setting to also toggle the CHM UI
            gGlobalPrefs->chmUI.useFixedPageUI = !gGlobalPrefs->chmUI.useFixedPageUI;
            break;

        case IDM_DEBUG_MUI:
            SetDebugPaint(!IsDebugPaint());
            win::menu::SetChecked(GetMenu(win->hwndFrame), IDM_DEBUG_MUI, !IsDebugPaint());
            for (size_t i = 0; i < gWindows.Count(); i++)
                gWindows.At(i)->RedrawAll(true);
            break;

        case IDM_DEBUG_ANNOTATION:
            if (win)
                FrameOnChar(*win, 'h');
            break;

        case IDM_DEBUG_CRASH_ME:
            CrashMe();
            break;
#endif

        case IDM_FAV_ADD:
            AddFavorite(win);
            break;

        case IDM_FAV_DEL:
            DelFavorite(win);
            break;

        case IDM_FAV_TOGGLE:
            ToggleFavorites(win);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0;
}

static LRESULT OnFrameGetMinMaxInfo(MINMAXINFO *info)
{
    info->ptMinTrackSize.x = MIN_WIN_DX;
    info->ptMinTrackSize.y = MIN_WIN_DY;
    return 0;
}

// TODO: shared with Canvas.cpp - figure out better way of interaction
// these can be global, as the mouse wheel can't affect more than one window at once
static int  gDeltaPerLine = 0;         // for mouse wheel logic
static bool gWheelMsgRedirect = false; // set when WM_MOUSEWHEEL has been passed on (to prevent recursion)
static bool gSuppressAltKey = false;   // set after scrolling horizontally (to prevent the menu from getting the focus)

static LRESULT CALLBACK WndProcFrame(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WindowInfo *    win;
    ULONG           ulScrollLines;                   // for mouse wheel logic

    win = FindWindowInfoByHwnd(hwnd);

    switch (msg)
    {
        case WM_CREATE:
            // do nothing
            goto InitMouseWheelInfo;

        case WM_SIZE:
            if (win && SIZE_MINIMIZED != wParam) {
                RememberDefaultWindowPosition(*win);
                AdjustWindowEdge(*win);

                int dx = LOWORD(lParam);
                int dy = HIWORD(lParam);
                FrameOnSize(win, dx, dy);
            }
            break;

        case WM_GETMINMAXINFO:
            return OnFrameGetMinMaxInfo((MINMAXINFO*)lParam);

        case WM_MOVE:
            if (win) {
                RememberDefaultWindowPosition(*win);
                AdjustWindowEdge(*win);
            }
            break;

        case WM_INITMENUPOPUP:
            UpdateMenu(win, (HMENU)wParam);
            break;

        case WM_COMMAND:
            return FrameOnCommand(win, hwnd, msg, wParam, lParam);

        case WM_APPCOMMAND:
            // both keyboard and mouse drivers should produce WM_APPCOMMAND
            // messages for their special keys, so handle these here and return
            // TRUE so as to not make them bubble up further
            switch (GET_APPCOMMAND_LPARAM(lParam)) {
            case APPCOMMAND_BROWSER_BACKWARD:
                SendMessage(hwnd, WM_COMMAND, IDM_GOTO_NAV_BACK, 0);
                return TRUE;
            case APPCOMMAND_BROWSER_FORWARD:
                SendMessage(hwnd, WM_COMMAND, IDM_GOTO_NAV_FORWARD, 0);
                return TRUE;
            case APPCOMMAND_BROWSER_REFRESH:
                SendMessage(hwnd, WM_COMMAND, IDM_REFRESH, 0);
                return TRUE;
            case APPCOMMAND_BROWSER_SEARCH:
                SendMessage(hwnd, WM_COMMAND, IDM_FIND_FIRST, 0);
                return TRUE;
            case APPCOMMAND_BROWSER_FAVORITES:
                SendMessage(hwnd, WM_COMMAND, IDM_VIEW_BOOKMARKS, 0);
                return TRUE;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);

        case WM_CHAR:
            if (win)
                FrameOnChar(*win, wParam);
            break;

        case WM_KEYDOWN:
            if (win)
                FrameOnKeydown(win, wParam, lParam);
            break;

        case WM_SYSKEYUP:
            // pressing and releasing the Alt key focuses the menu even if
            // the wheel has been used for scrolling horizontally, so we
            // have to suppress that effect explicitly in this situation
            if (VK_MENU == wParam && gSuppressAltKey) {
                gSuppressAltKey = false;
                return 0;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);

        case WM_SYSCOMMAND:
            // temporarily show the menu bar if it has been hidden
            if (wParam == SC_KEYMENU && win->isMenuHidden)
                ShowHideMenuBar(win, true);
            return DefWindowProc(hwnd, msg, wParam, lParam);

        case WM_EXITMENULOOP:
            // hide the menu bar again if it was shown only temporarily
            if (!wParam && win->isMenuHidden)
                SetMenu(hwnd, NULL);
            return DefWindowProc(hwnd, msg, wParam, lParam);

        case WM_CONTEXTMENU:
            // opening the context menu with a keyboard doesn't call the canvas'
            // WM_CONTEXTMENU, as it never has the focus (mouse right-clicks are
            // handled as expected)
            if (win && GET_X_LPARAM(lParam) == -1 && GET_Y_LPARAM(lParam) == -1 && GetFocus() != win->hwndTocTree)
                return SendMessage(win->hwndCanvas, WM_CONTEXTMENU, wParam, lParam);
            return DefWindowProc(hwnd, msg, wParam, lParam);

        case WM_SETTINGCHANGE:
InitMouseWheelInfo:
            SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &ulScrollLines, 0);
            // ulScrollLines usually equals 3 or 0 (for no scrolling) or -1 (for page scrolling)
            // WHEEL_DELTA equals 120, so iDeltaPerLine will be 40
            if (ulScrollLines == (ULONG)-1)
                gDeltaPerLine = -1;
            else if (ulScrollLines != 0)
                gDeltaPerLine = WHEEL_DELTA / ulScrollLines;
            else
                gDeltaPerLine = 0;

            if (win) {
                // in tablets it's possible to rotate the screen. if we're
                // in full screen, resize our window to match new screen size
                if (win->presentation)
                    EnterFullScreen(*win, true);
                else if (win->isFullScreen)
                    EnterFullScreen(*win, false);
            }

            return 0;

        case WM_SYSCOLORCHANGE:
            if (gGlobalPrefs->useSysColors)
                UpdateDocumentColors();
            break;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            if (!win)
                break;

            if (win->IsChm()) {
                return win->AsChm()->engine()->PassUIMsg(msg, wParam, lParam);
            }

            // Pass the message to the canvas' window procedure
            // (required since the canvas itself never has the focus and thus
            // never receives WM_MOUSEWHEEL messages)
            return SendMessage(win->hwndCanvas, msg, wParam, lParam);

        case WM_CLOSE:
            CloseWindow(win, true);
            break;

        case WM_DESTROY:
            /* WM_DESTROY is generated by windows when close button is pressed
               or if we explicitly call DestroyWindow()
               It might be sent as a result of File\Close, in which
               case CloseWindow() has already been called. */
            if (win)
                CloseWindow(win, true, true);
            break;

        case WM_ENDSESSION:
            // TODO: check for unfinished print jobs in WM_QUERYENDSESSION?
            prefs::Save();
            break;

        case WM_DDE_INITIATE:
            if (gPluginMode)
                break;
            return OnDDEInitiate(hwnd, wParam, lParam);
        case WM_DDE_EXECUTE:
            return OnDDExecute(hwnd, wParam, lParam);
        case WM_DDE_TERMINATE:
            return OnDDETerminate(hwnd, wParam, lParam);

        case WM_TIMER:
            if (win && win->stressTest) {
                OnStressTestTimer(win, (int)wParam);
            }
            break;

        case WM_MOUSEACTIVATE:
            if (win && win->presentation && hwnd != GetForegroundWindow())
                return MA_ACTIVATEANDEAT;
            return MA_ACTIVATE;

        case WM_NOTIFY:
            if (wParam == IDC_TABBAR) {
                if (win)
                    return TabsOnNotify(win, lParam);
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

static int RunMessageLoop()
{
    HACCEL accTable = LoadAccelerators(ghinst, MAKEINTRESOURCE(IDC_SUMATRAPDF));
    MSG msg = { 0 };

    while (GetMessage(&msg, NULL, 0, 0)) {
        // dispatch the accelerator to the correct window
        WindowInfo *win = FindWindowInfoByHwnd(msg.hwnd);
        HWND accHwnd = win ? win->hwndFrame : msg.hwnd;
        if (TranslateAccelerator(accHwnd, accTable, &msg))
            continue;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

void GetProgramInfo(str::Str<char>& s)
{
    s.AppendFmt("Ver: %s", CURR_VERSION_STRA);
#ifdef SVN_PRE_RELEASE_VER
    s.AppendFmt(" pre-release");
#endif
#ifdef DEBUG
    if (!str::EndsWith(s.Get(), " (dbg)"))
        s.Append(" (dbg)");
#endif
    s.Append("\r\n");
    s.AppendFmt("Browser plugin: %s\r\n", gPluginMode ? "yes" : "no");
}

bool CrashHandlerCanUseNet()
{
    return HasPermission(Perm_InternetAccess);
}

void CrashHandlerMessage()
{
    // don't show a message box in restricted use, as the user most likely won't be
    // able to do anything about it anyway and it's up to the application provider
    // to fix the unexpected behavior (of which for a restricted set of documents
    // there should be much less, anyway)
    if (HasPermission(Perm_DiskAccess)) {
        int res = MessageBox(NULL, _TR("Sorry, that shouldn't have happened!\n\nPlease press 'Cancel', if you want to help us fix the cause of this crash."), _TR("SumatraPDF crashed"), MB_ICONERROR | MB_OKCANCEL | MbRtlReadingMaybe());
        if (IDCANCEL == res)
            LaunchBrowser(CRASH_REPORT_URL);
    }
}

// TODO: make this stand-alone
#include "Canvas.cpp"

// TODO: a hackish but cheap way to separate startup code.
// Could be made to compile stand-alone
#include "SumatraStartup.cpp"
