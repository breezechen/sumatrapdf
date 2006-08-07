#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <direct.h> /* for _mkdir() */

#include "SumatraPDF.h"

#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>

#include "ErrorCodes.h"
#include "GooString.h"
#include "GooList.h"
#include "GlobalParams.h"
#include "SplashBitmap.h"
#include "PDFDoc.h"
#include "Object.h" /* must be included before SplashOutputDev.h because of sloppiness in SplashOutputDev.h */
#include "SplashOutputDev.h"
#include "Link.h"
#include "SecurityHandler.h"

#include "SumatraDialogs.h"
#include "Win32FontList.h"
#include "FileHistory.h"
#include "AppPrefs.h"

#include "SimpleRect.h"
#include "DisplayModel.h"
#include "BaseUtils.h"

/* Next action for the benchmark mode */
#define MSG_BENCH_NEXT_ACTION WM_USER + 1

#define ZOOM_IN_FACTOR      1.2
#define ZOOM_OUT_FACTOR     1.0 / ZOOM_IN_FACTOR

/* Uncomment to visually show links as blue rectangles, for easier links
   debugging. 
   TODO: not implemented on windows */
//#define DEBUG_SHOW_LINKS            1

/* default UI settings */
#define DEFAULT_DISPLAY_MODE DM_CONTINUOUS

//#define DEFAULT_ZOOM            ZOOM_FIT_WIDTH
#define DEFAULT_ZOOM            ZOOM_FIT_PAGE
#define DEFAULT_ROTATION        0

//#define START_WITH_ABOUT        1

/* define if want to use double-buffering for rendering the PDF. Takes more memory!. */
#define DOUBLE_BUFFER 1

#define DRAGQUERY_NUMFILES 0xFFFFFFFF

#define MAX_LOADSTRING 100

#define WM_CREATE_FAILED -1
#define WM_CREATE_OK 0
#define WM_NCPAINT_HANDLED 0
#define WM_VSCROLL_HANDLED 0
#define WM_HSCROLL_HANDLED 0

/* A caption is 4 white/blue 2 pixel line and a 3 pixel white line */
#define CAPTION_DY 2*(2*4)+3

#define COL_CAPTION_BLUE RGB(0,0x50,0xa0)
#define COL_WHITE RGB(0xff,0xff,0xff)
#define COL_BLACK RGB(0,0,0)
#define COL_WINDOW_BG RGB(0xcc, 0xcc, 0xcc)
//#define COL_WINDOW_BG RGB(0xff, 0xff, 0xff)
#define COL_WINDOW_SHADOW RGB(0x40, 0x40, 0x40)

#define WIN_CLASS_NAME  _T("SUMATRA_PDF_WIN")
#define APP_NAME        _T("SumatraPDF")
#define PDF_DOC_NAME    _T("Adobe PDF Document")

#define PREFS_FILE_NAME _T("prefs.txt")
#define APP_SUB_DIR     _T("SumatraPDF")

#define BENCH_ARG_TXT   "-bench"

/* Default size for the window, happens to be american A4 size (I think) */
#define DEF_WIN_DX 612
#define DEF_WIN_DY 792

#define MAX_RECENT_FILES_IN_MENU 15

typedef struct StrList {
    struct StrList *    next;
    char *              str;
} StrList;

static FileHistoryList *            gFileHistoryRoot = NULL;

static SplashColor                  splashColRed;
static SplashColor                  splashColGreen;
static SplashColor                  splashColBlue;
static SplashColor                  splashColWhite;
static SplashColor                  splashColBlack;

static HINSTANCE                    ghinst = NULL;
TCHAR                               szTitle[MAX_LOADSTRING];

static WindowInfo*                  gWindowList = NULL;

static HCURSOR                      gCursorArrow = NULL;
static HCURSOR                      gCursorWait = NULL;
static HBRUSH                       gBrushBg;
static HBRUSH                       gBrushShadow;
static HBRUSH                       gBrushLinkDebug;

static HPEN                         ghpenWhite = NULL;
static HPEN                         ghpenBlue = NULL;

#define SPLASH_COL_RED_PTR          (SplashColorPtr)&(splashColRed[0])
#define SPLASH_COL_GREEN_PTR        (SplashColorPtr)&(splashColGreen[0])
#define SPLASH_COL_BLUE_PTR         (SplashColorPtr)&(splashColBlue[0])
#define SPLASH_COL_WHITE_PTR        (SplashColorPtr)&(splashColWhite[0])
#define SPLASH_COL_BLACK_PTR        (SplashColorPtr)&(splashColBlack[0])

static SplashColorPtr               gBgColor = SPLASH_COL_WHITE_PTR;
static SplashColorMode              gSplashColorMode = splashModeBGR8;

static AppVisualStyle               gVisualStyle = VS_WINDOWS;

static char *                       gBenchFileName = NULL;
static int                          gBenchPageNum = INVALID_PAGE_NUM;

#ifdef DOUBLE_BUFFER
static BOOL                         gUseDoubleBuffer = TRUE;
#else
static BOOL                         gUseDoubleBuffer = FALSE;
#endif

void WindowInfo_ResizeToPage(WindowInfo *win, int pageNo);

void CDECL error(int pos, char *msg, ...) {
    va_list args;
    char        buf[4096], *p = buf;

    // NB: this can be called before the globalParams object is created
    if (globalParams && globalParams->getErrQuiet()) {
        return;
    }

    if (pos >= 0) {
        p += _snprintf(p, sizeof(buf)-1, "Error (%d): ", pos);
        *p   = '\0';
        OutputDebugString(p);
    } else {
        OutputDebugString("Error: ");
    }

    p = buf;
    va_start(args, msg);
    p += _vsnprintf(p, sizeof(buf) - 1, msg, args);
    while ( p > buf  &&  isspace(p[-1]) )
            *--p = '\0';
    *p++ = '\r';
    *p++ = '\n';
    *p   = '\0';
    OutputDebugString(buf);
    va_end(args);
}

int RectDx(RECT *r)
{
    int dx = r->right - r->left;
    assert(dx >= 0);
    return dx;
}

int RectDy(RECT *r)
{
    int dy = r->bottom - r->top;
    assert(dy >= 0);
    return dy;
}

void LaunchBrowser(const TCHAR *url)
{
    SHELLEXECUTEINFO sei;
    BOOL             res;

    if (NULL == url)
        return;

    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize  = sizeof(sei);
    sei.fMask   = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb  = TEXT("open");
    sei.lpFile  = url;
    sei.nShow   = SW_SHOWNORMAL;

    res = ShellExecuteEx(&sei);
    return;
}

/* TODO: move to BaseUtils.h */
const char *Path_GetBaseName(const char *path)
{
    const char *fileBaseName = (const char*)strrchr(path, DIR_SEP_CHAR);
    if (NULL == fileBaseName)
        fileBaseName = path;
    else
        ++fileBaseName;
    return fileBaseName;
}

HMENU FindMenuItem(WindowInfo *win, UINT id)
{
    HMENU   menuMain;
    HMENU   subMenu;

    UINT    thisId;
    int     i, j;

    menuMain = GetMenu(win->hwnd);

    /* TODO: to be fully valid, it would have to be recursive */
    for (i = 0; i < GetMenuItemCount(menuMain); i++) {
        thisId = GetMenuItemID(menuMain, i);
        subMenu = GetSubMenu(menuMain, i);
        if (id == thisId)
            return subMenu;
        for (j = 0; j < GetMenuItemCount(subMenu); j++) {
            thisId = GetMenuItemID(menuMain, j);
            if (id == thisId)
                return GetSubMenu(subMenu, j);
        }
    }
    return NULL;
}

HMENU GetFileMenu(HWND hwnd)
{
    return GetSubMenu(GetMenu(hwnd), 0);
}

void SwitchToDisplayMode(WindowInfo *win, DisplayMode displayMode)
{
    HMENU   menuMain;
    UINT    id;
    
    menuMain = GetMenu(win->hwnd);
    CheckMenuItem(menuMain, IDM_VIEW_SINGLE_PAGE, MF_BYCOMMAND | MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VIEW_CONTINUOUS, MF_BYCOMMAND | MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VIEW_FACING, MF_BYCOMMAND | MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VIEW_CONTINUOUS_FACING, MF_BYCOMMAND | MF_UNCHECKED);

    DisplayModel_SetDisplayMode(win->dm, displayMode);
    if (DM_SINGLE_PAGE == displayMode) {
        id = IDM_VIEW_SINGLE_PAGE;
    } else if (DM_FACING == displayMode) {
        id =  IDM_VIEW_FACING;
    } else if (DM_CONTINUOUS == displayMode) {
        id =  IDM_VIEW_CONTINUOUS;
    } else if (DM_CONTINUOUS_FACING == displayMode) {
        id =  IDM_VIEW_CONTINUOUS_FACING;
    } else
        assert(0);

    CheckMenuItem(menuMain, id, MF_BYCOMMAND | MF_CHECKED);
}

UINT AllocNewMenuId(void)
{
    static UINT firstId = 1000;
    ++firstId;
    return firstId;
}

void AddMenuSepToFilesMenu(WindowInfo *win)
{
    HMENU               menuFile;
    menuFile = GetFileMenu(win->hwnd);
    AppendMenu(menuFile, MF_SEPARATOR, 0, NULL);
}

void AddMenuItemToFilesMenu(WindowInfo *win, FileHistoryList *node)
{
    HMENU               menuFile;
    UINT                newId;
    const char *        txt;

    assert(node);
    if (!node)
        return;
    menuFile = GetFileMenu(win->hwnd);
    assert(menuFile);
    if (!menuFile)
        return;

    txt = Path_GetBaseName(node->state.filePath);

    newId = node->menuId;
    if (INVALID_MENU_ID == node->menuId)
        newId = AllocNewMenuId();
    AppendMenu(menuFile, MF_ENABLED | MF_STRING, newId, txt);
    DBG_OUT("AddMenuItemToFilesMenu() txt=%s, newId=%d\n", txt, (int)newId);
    if (INVALID_MENU_ID == node->menuId)
        node->menuId = newId;
}

void AddRecentFilesToMenu(WindowInfo *win)
{
    int                 itemsAdded = 0;
    FileHistoryList *   curr;

    if (!gFileHistoryRoot)
        return;

    AddMenuSepToFilesMenu(win);

    curr = gFileHistoryRoot;
    while (curr) {
        assert(curr->state.filePath);
        if (curr->state.filePath) {
            AddMenuItemToFilesMenu(win, curr);
            assert(curr->menuId != INVALID_MENU_ID);
            ++itemsAdded;
            if (itemsAdded >= MAX_RECENT_FILES_IN_MENU) {
                DBG_OUT("  not adding, reached max %d items\n", MAX_RECENT_FILES_IN_MENU);
                return;
            }
        }
        curr = curr->next;
    }
}

void WinEditSetSel(HWND hwnd, DWORD selStart, DWORD selEnd)
{
   ::SendMessage(hwnd, EM_SETSEL, (WPARAM)selStart, (WPARAM)selEnd);
}

void WinEditSelectAll(HWND hwnd)
{
    WinEditSetSel(hwnd, 0, -1);
}

int WinGetTextLen(HWND hwnd)
{
    return (int)SendMessage(hwnd, WM_GETTEXTLENGTH, 0, 0);
}

void WinSetText(HWND hwnd, const TCHAR *txt)
{
    SendMessage(hwnd, WM_SETTEXT, (WPARAM)0, (LPARAM)txt);
}

/* return a text in edit control represented by hwnd
   return NULL in case of error (couldn't allocate memory)
   caller needs to free() the text */
TCHAR *WinGetText(HWND hwnd)
{
    int     cchTxtLen = WinGetTextLen(hwnd);
    TCHAR * txt = (TCHAR*)malloc((cchTxtLen+1)*sizeof(TCHAR));

    if (NULL == txt)
        return NULL;

    SendMessage(hwnd, WM_GETTEXT, cchTxtLen + 1, (LPARAM)txt);
    txt[cchTxtLen] = 0;
    return txt;
}
void Win32_GetScrollbarSize(int *scrollbarYDxOut, int *scrollbarXDyOut)
{
    if (scrollbarYDxOut)
        *scrollbarYDxOut = GetSystemMetrics(SM_CXHSCROLL);
    if (scrollbarXDyOut)
        *scrollbarXDyOut = GetSystemMetrics(SM_CYHSCROLL);
}

/* Set the client area size of the window 'hwnd' to 'dx'/'dy'. */
void WinResizeClientArea(HWND hwnd, int dx, int dy)
{
    RECT rc;
    RECT rw;
    int  win_dx, win_dy;

    GetClientRect(hwnd, &rc);
    if ((RectDx(&rc) == dx) && (RectDy(&rc) == dy))
        return;
    GetWindowRect(hwnd, &rw);
    win_dx = RectDx(&rw) + (dx - RectDx(&rc));
    win_dy = RectDy(&rw) + (dy - RectDy(&rc));
    SetWindowPos(hwnd, NULL, 0, 0, win_dx, win_dy, SWP_NOACTIVATE | SWP_NOREPOSITION | SWP_NOMOVE| SWP_NOZORDER);
}

static void CaptionPens_Create(void)
{
    LOGPEN  pen;

    assert(!ghpenWhite);
    pen.lopnStyle = PS_SOLID;
    pen.lopnWidth.x = 1;
    pen.lopnWidth.y = 1;
    pen.lopnColor = COL_WHITE;
    ghpenWhite = CreatePenIndirect(&pen);
    pen.lopnColor = COL_CAPTION_BLUE;
    ghpenBlue = CreatePenIndirect(&pen);
}

static void CaptionPens_Destroy(void)
{
    if (ghpenWhite) {
        DeleteObject(ghpenWhite);
        ghpenWhite = NULL;
    }

    if (ghpenBlue) {
        DeleteObject(ghpenBlue);
        ghpenBlue = NULL;
    }
}

static void AddFileToHistory(const char *filePath)
{
    FileHistoryList *   node;
    uint32_t            oldMenuId = INVALID_MENU_ID;

    assert(filePath);
    if (!filePath) return;

    /* if a history entry with the same name already exists, then delete it.
       That way we don't have duplicates and the file moves to the front of the list */
    node = FileHistoryList_Node_FindByFilePath(&gFileHistoryRoot, filePath);
    if (node) {
        oldMenuId = node->menuId;
        FileHistoryList_Node_RemoveAndFree(&gFileHistoryRoot, node);
    }
    node = FileHistoryList_Node_CreateFromFilePath(filePath);
    if (!node)
        return;
    node->menuId = oldMenuId;
    FileHistoryList_Node_InsertHead(&gFileHistoryRoot, node);
}

static char *GetPasswordForFile(WindowInfo *win, const char *fileName)
{
    fileName = Path_GetBaseName(fileName);
    return Dialog_GetPassword(win, fileName);
}

void *StandardSecurityHandler::getAuthData() 
{
    WindowInfo *        win;
    const char *        pwd;
    StandardAuthData *  authData;

    win = (WindowInfo*)doc->getGUIData();
    assert(win);
    if (!win)
        return NULL;

    pwd = GetPasswordForFile(win, doc->getFileName()->getCString());
    if (!pwd)
        return NULL;

    authData = new StandardAuthData(new GooString(pwd), new GooString(pwd));
    free((void*)pwd);
    return (void*)authData;
}

void AppGetAppDir(DString* pDs)
{
    char        dir[MAX_PATH];

    SHGetFolderPath(NULL, CSIDL_APPDATA|CSIDL_FLAG_CREATE, NULL, 0, dir);
    DStringSprintf(pDs, "%s/%s", dir, APP_SUB_DIR);
    _mkdir(pDs->pString);
}

/* Generate the full path for a filename used by the app in the userdata path. */
void AppGenDataFilename(char* pFilename, DString* pDs)
{
    assert(0 == pDs->length);
    assert(pFilename);
    if (!pFilename) return;
    assert(pDs);
    if (!pDs) return;

    AppGetAppDir(pDs);
    if (!Str_EndsWithNoCase(pDs->pString, DIR_SEP_STR) && !(DIR_SEP_CHAR == pFilename[0])) {
        DStringAppend(pDs, "/", 1);
    }
    DStringAppend(pDs, pFilename, -1);
}

static void Prefs_GetFileName(DString* pDs)
{
    assert(0 == pDs->length);
    AppGenDataFilename(PREFS_FILE_NAME, pDs);
}

/* Load preferences from the preferences file. */
void Prefs_Load(void)
{
    DString             path;
    static int          loaded = FALSE;
    unsigned long       prefsFileLen;
    char *              prefsTxt = NULL;
    BOOL                fOk;

    assert(!loaded);
    loaded = TRUE;

    DBG_OUT("Prefs_Load()\n");

    DStringInit(&path);
    Prefs_GetFileName(&path);

    prefsTxt = File_Slurp(path.pString, &prefsFileLen);
    if (Str_Empty(prefsTxt)) {
        DBG_OUT("  no prefs file or is empty\n");
        return;
    }
    DBG_OUT("Prefs file %s:\n%s\n", path.pString, prefsTxt);

    fOk = Prefs_Deserialize(prefsTxt, &gFileHistoryRoot);
    assert(fOk);

    DStringFree(&path);
    free((void*)prefsTxt);
}

void Win32_Win_GetPos(HWND hwnd, int *xOut, int *yOut)
{
    RECT    r;
    *xOut = 0;
    *yOut = 0;

    if (GetWindowRect(hwnd, &r)) {
        *xOut = r.left;
        *yOut = r.top;
    }
}

void Win32_Win_SetPos(HWND hwnd, int x, int y)
{
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE);
}

void UpdateDisplayStateWindowPos(WindowInfo *win, DisplayState *ds)
{
    int posX, posY;

    Win32_Win_GetPos(win->hwnd, &posX, &posY);

    ds->windowX = posX;
    ds->windowY = posY;
}

void UpdateCurrentFileDisplayStateForWin(WindowInfo *win)
{
    DisplayState    ds;
    const char *    fileName = NULL;
    FileHistoryList*node = NULL;

    if (!win)
        return;
    if (WS_SHOWING_PDF != win->state)
        return;
    if (!win->dm)
        return;
    if (!win->dm->pdfDoc)
        return;

    fileName = win->dm->pdfDoc->getFileName()->getCString();
    assert(fileName);
    if (!fileName)
        return;

    node = FileHistoryList_Node_FindByFilePath(&gFileHistoryRoot, fileName);
    assert(node);
    if (!node)
        return;

    DisplayState_Init(&ds);
    if (!DisplayState_FromDisplayModel(&ds, win->dm))
        return;

    UpdateDisplayStateWindowPos(win, &ds);
    DisplayState_Free(&(node->state));
    node->state = ds;
    node->state.visible = TRUE;
}

void UpdateCurrentFileDisplayState(void)
{
    WindowInfo *        currWin;
    FileHistoryList *   currFile;

    currFile = gFileHistoryRoot;
    while (currFile) {
        currFile->state.visible = FALSE;
        currFile = currFile->next;
    }

    currWin = gWindowList;
    while (currWin) {
        UpdateCurrentFileDisplayStateForWin(currWin);
        currWin = currWin->next;
    }
}

//static BOOL gPrefsSaved = FALSE;

void Prefs_Save(void)
{
    DString       path;
    DString       prefsStr;
    size_t        len = 0;
    FILE*         pFile = NULL;
    BOOL          fOk;

#if 0
    if (gPrefsSaved)
        return;
    gPrefsSaved = TRUE;
#endif

    DStringInit(&prefsStr);

    /* mark currently shown files as visible */
    UpdateCurrentFileDisplayState();

    fOk = Prefs_Serialize(&gFileHistoryRoot, &prefsStr);
    if (!fOk)
        goto Exit;

    DStringInit(&path);
    Prefs_GetFileName(&path);
    DBG_OUT("prefs file=%s\nprefs:\n%s\n", path.pString, prefsStr.pString);
    /* TODO: consider 2-step process:
        * write to a temp file
        * rename temp file to final file */
    pFile = fopen(path.pString, "w");
    if (!pFile) {
        goto Exit;
    }

    len = prefsStr.length;
    if (fwrite(prefsStr.pString, 1, len, pFile) != len) {
        goto Exit;
    }
    
Exit:
    DStringFree(&prefsStr);
    DStringFree(&path);
    if (pFile)
        fclose(pFile);
}

static void WindowInfo_GetWindowSize(WindowInfo *win)
{
    RECT  rc;
    GetClientRect(win->hwnd, &rc);
    win->winDx = RectDx(&rc);
    win->winDy = RectDy(&rc);
    // DBG_OUT("WindowInfo_GetWindowSize() win_dx=%d, win_dy=%d\n", win->win_dx, win->win_dy);
}

static BOOL WindowInfo_Dib_Init(WindowInfo *win)
{
    assert(NULL == win->dibInfo);
    win->dibInfo = (BITMAPINFO*)malloc(sizeof(BITMAPINFO) + 12);
    if (!win->dibInfo)
        return FALSE;
    win->dibInfo->bmiHeader.biSize = sizeof(win->dibInfo->bmiHeader);
    win->dibInfo->bmiHeader.biPlanes = 1;
    win->dibInfo->bmiHeader.biBitCount = 24;
    win->dibInfo->bmiHeader.biCompression = BI_RGB;
    win->dibInfo->bmiHeader.biXPelsPerMeter = 2834;
    win->dibInfo->bmiHeader.biYPelsPerMeter = 2834;
    win->dibInfo->bmiHeader.biClrUsed = 0;
    win->dibInfo->bmiHeader.biClrImportant = 0;
    return TRUE;
}

static void WindowInfo_Dib_Deinit(WindowInfo *win)
{
    free((void*)win->dibInfo);
    win->dibInfo = NULL;
}

static void WindowInfo_DoubleBuffer_Delete(WindowInfo *win)
{
    if (win->bmpDoubleBuffer) {
        DeleteObject(win->bmpDoubleBuffer);
        win->bmpDoubleBuffer = NULL;
    }

    if (win->hdcDoubleBuffer) {
        DeleteDC(win->hdcDoubleBuffer);
        win->hdcDoubleBuffer = NULL;
    }
    win->hdcToDraw = NULL;
}

static BOOL WindowInfo_DoubleBuffer_New(WindowInfo *win)
{
    RECT r = {0};
    WindowInfo_DoubleBuffer_Delete(win);

    win->hdc = GetDC(win->hwnd);
    win->hdcToDraw = win->hdc;
    WindowInfo_GetWindowSize(win);
    if (!gUseDoubleBuffer || (0 == win->winDx) || (0 == win->winDy))
        return TRUE;

    win->hdcDoubleBuffer = CreateCompatibleDC(win->hdc);
    if (!win->hdcDoubleBuffer)
        return FALSE;

    win->bmpDoubleBuffer = CreateCompatibleBitmap(win->hdc, win->winDx, win->winDy);
    if (!win->bmpDoubleBuffer) {
        WindowInfo_DoubleBuffer_Delete(win);
        return FALSE;
    }
    /* TODO: do I need this ? */
    SelectObject(win->hdcDoubleBuffer, win->bmpDoubleBuffer);
    /* fill out everything with background color */
    r.bottom = win->winDy;
    r.right = win->winDx;
    FillRect(win->hdcDoubleBuffer, &r, gBrushBg);
    win->hdcToDraw = win->hdcDoubleBuffer;
    return TRUE;
}

static void WindowInfo_DoubleBuffer_Show(WindowInfo *win, HDC hdc)
{
    if (win->hdc != win->hdcToDraw) {
        assert(win->hdcToDraw == win->hdcDoubleBuffer);
        BitBlt(hdc, 0, 0, win->winDx, win->winDy, win->hdcDoubleBuffer, 0, 0, SRCCOPY);
    }
}

static void WindowInfo_Delete(WindowInfo *win)
{
    DisplayModel_Delete(win->dm);
    WindowInfo_Dib_Deinit(win);
    WindowInfo_DoubleBuffer_Delete(win);
    free((void*)win);
}

static WindowInfo* WindowInfo_FindByHwnd(HWND hwnd)
{
    WindowInfo  *win = gWindowList;
    while (win) {
        if (hwnd == win->hwnd)
            return win;
        win = win->next;
    }
    return NULL;
}

static WindowInfo *WindowInfo_New(HWND hwnd)
{
    WindowInfo  *win;

    win = WindowInfo_FindByHwnd(hwnd);
    assert(!win);

    win = (WindowInfo*)calloc(sizeof(WindowInfo), 1);
    if (!win)
        return NULL;

    if (!WindowInfo_Dib_Init(win))
        goto Error;

#if START_WITH_ABOUT
    win->state = WS_ABOUT_ANIM;
#else
    win->state = WS_EMPTY;
#endif
    win->hwnd = hwnd;
    AddRecentFilesToMenu(win);
    return win;
Error:
    WindowInfo_Delete(win);
    return NULL;
}

static void WindowInfoList_Add(WindowInfo *win)
{
    win->next = gWindowList;
    gWindowList = win;
}

static BOOL WindowInfoList_ExistsWithError(void)
{
    WindowInfo *cur = gWindowList;
    while (cur) {
        if (WS_ERROR_LOADING_PDF == cur->state)
            return TRUE;
        cur = cur->next;
    }
    return FALSE;
}

static void WindowInfoList_Remove(WindowInfo *to_remove)
{
    WindowInfo *curr = gWindowList;

    assert(to_remove);

    if (gWindowList == to_remove) {
        gWindowList = to_remove->next;
    } else {
        while (curr) {
            if (to_remove == curr->next) {
                curr->next = to_remove->next;
            }
            curr = curr->next;
        }
    }
}

static void WindowInfoList_DeleteAll(void)
{
    WindowInfo *curr = gWindowList;
    WindowInfo *next;

    while (curr) {
        next = curr->next;
        WindowInfo_Delete(curr);
        curr = next;
    }
    gWindowList = NULL;
}

static int WindowInfoList_Len(void)
{
    WindowInfo * curr = gWindowList;
    int          len = 0;

    while (curr) {
        ++len;
        curr = curr->next;
    }
    return len;
}

static void WindowInfo_RedrawAll(WindowInfo *win)
{
    InvalidateRect(win->hwnd, NULL, FALSE);
    UpdateWindow(win->hwnd);
}

BOOL FileCloseMenuEnabled(void)
{
    WindowInfo *    win;
    win = gWindowList;
    while (win) {
        if (win->state == WS_SHOWING_PDF)
            return TRUE;
        win = win->next;
    }
    return FALSE;
}

static void MenuUpdateStateForWindow(WindowInfo *win)
{
    HMENU     hmenu;
    BOOL      fileCloseEnabled;
    UINT      menuId;
    SCROLLINFO      si = {0};

    static UINT menusToDisableIfNoPdf[] = {
        IDM_VIEW_SINGLE_PAGE, IDM_VIEW_FACING, IDM_VIEW_CONTINUOUS, IDM_VIEW_CONTINUOUS_FACING,
        IDM_VIEW_ROTATE_LEFT, IDM_VIEW_ROTATE_RIGHT, IDM_GOTO_NEXT_PAGE, IDM_GOTO_PREV_PAGE,
        IDM_GOTO_FIRST_PAGE, IDM_GOTO_LAST_PAGE, IDM_GOTO_PAGE, IDM_ZOOM_FIT_PAGE,
        IDM_ZOOM_ACTUAL_SIZE, IDM_ZOOM_FIT_WIDTH, IDM_ZOOM_6400, IDM_ZOOM_3200,
        IDM_ZOOM_1600, IDM_ZOOM_800, IDM_ZOOM_400, IDM_ZOOM_200, IDM_ZOOM_150,
        IDM_ZOOM_125, IDM_ZOOM_100, IDM_ZOOM_50, IDM_ZOOM_25, IDM_ZOOM_12_5,
        IDM_ZOOM_8_33 };

    fileCloseEnabled = FileCloseMenuEnabled();

    hmenu = GetMenu(win->hwnd);
    if (fileCloseEnabled)
        EnableMenuItem(hmenu, IDM_CLOSE, MF_BYCOMMAND | MF_ENABLED);
    else
        EnableMenuItem(hmenu, IDM_CLOSE, MF_BYCOMMAND | MF_GRAYED);

    for (int i = 0; i < dimof(menusToDisableIfNoPdf); i++) {
        menuId = menusToDisableIfNoPdf[i];
        if (WS_SHOWING_PDF == win->state)
            EnableMenuItem(hmenu, menuId, MF_BYCOMMAND | MF_ENABLED);
        else
            EnableMenuItem(hmenu, menuId, MF_BYCOMMAND | MF_GRAYED);
    }
    /* Hide scrollbars if not showing a PDF */
    /* TODO: doesn't really fit the name of the function */
    if (WS_SHOWING_PDF == win->state)
        ShowScrollBar(win->hwnd, SB_BOTH, TRUE);
    else {
        ShowScrollBar(win->hwnd, SB_BOTH, FALSE);
        WinSetText(win->hwnd, APP_NAME);
    }
}

/* Disable/enable menu items depending on wheter a given window shows a PDF 
   file or not. */
static void MenuUpdateStateForAllWindows(void)
{
    WindowInfo *    win;

    win = gWindowList;
    while (win) {
        MenuUpdateStateForWindow(win);
        win = win->next;
    }
}

static WindowInfo* WindowInfo_CreateEmpty(void)
{
    HWND        hwnd;
    WindowInfo* win;

    hwnd = CreateWindow(
            WIN_CLASS_NAME, szTitle,
            WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT,
            DEF_WIN_DX, DEF_WIN_DY,
            NULL, NULL,
            ghinst, NULL);

    if (!hwnd)
        return NULL;

    win = WindowInfo_New(hwnd);
    return win;
}

static WindowInfo* LoadPdf(const TCHAR *fileName, BOOL closeInvalidFiles, BOOL ignoreHistorySizePos = TRUE, BOOL ignoreHistory = FALSE)
{
    int                 err;
    WindowInfo *        win;
    GooString *         fileNameStr = NULL;
    int                 reuseExistingWindow = FALSE;
    PDFDoc *            pdfDoc;
    RectDSize           totalDrawAreaSize;
    int                 scrollbarYDx, scrollbarXDy;
    SplashOutputDev *   outputDev = NULL;
    GBool               bitmapTopDown = gTrue;
    BOOL                fromHistory;
    FileHistoryList *   fileHistory = NULL;
    int                 startPage;
    double              zoomVirtual;
    int                 rotation;
    DisplayMode         displayMode;
    int                 offsetX, offsetY;

    startPage = 1;
    displayMode = DEFAULT_DISPLAY_MODE;
    offsetX = 0;
    offsetY = 0;

    if (!ignoreHistory)
        fileHistory = FileHistoryList_Node_FindByFilePath(&gFileHistoryRoot, fileName);

    if (fileHistory)
        fromHistory = TRUE;
    else
        fromHistory = FALSE;

    if ((1 == WindowInfoList_Len()) && (WS_SHOWING_PDF != gWindowList->state)) {
        win = gWindowList;
        reuseExistingWindow = TRUE;
    }

    if (!reuseExistingWindow) {
        win = WindowInfo_CreateEmpty();
        if (!win)
            return NULL;
        if (!fileName) {
            WindowInfoList_Add(win);
            goto Exit;
        }
    }

    fileNameStr = new GooString(fileName);
    if (!fileNameStr)
        return win;

    err = errNone;
    pdfDoc = new PDFDoc(fileNameStr, NULL, NULL, (void*)win);
    if (!pdfDoc->isOk())
    {
        err = errOpenFile;
        error(-1, "LoadPdf(): failed to open PDF file %s\n", fileName);
    }

    if (closeInvalidFiles && (errNone != err) && !reuseExistingWindow)
    {
        WindowInfo_Delete(win);
        return NULL;
    }

    if (errNone != err)
        goto Error;

    outputDev = new SplashOutputDev(gSplashColorMode, 4, gFalse, gBgColor, bitmapTopDown);
    if (!outputDev)
        return NULL; /* TODO: probably should WindowInfo_Delete() using the same logic as above */

    WindowInfo_GetWindowSize(win);

    totalDrawAreaSize.dx = (double)win->winDx;
    totalDrawAreaSize.dy = (double)win->winDy;
    if (fromHistory && !ignoreHistorySizePos) {
        WinResizeClientArea(win->hwnd, fileHistory->state.windowDx, fileHistory->state.windowDy);
        totalDrawAreaSize.dx = (double)fileHistory->state.windowDx;
        totalDrawAreaSize.dy = (double)fileHistory->state.windowDy;
        /* TODO: make sure it doesn't have a stupid position like 
           outside of the screen etc. */
        Win32_Win_SetPos(win->hwnd, fileHistory->state.windowX, fileHistory->state.windowY);
    }

    /* In theory I should get scrollbars sizes using Win32_GetScrollbarSize(&scrollbarYDx, &scrollbarXDy);
       but scrollbars are not part of the client area on windows so it's better
       not to have them taken into account by DisplayModel code.
       TODO: I think it's broken anyway and DisplayModel needs to know if
             scrollbars are part of client area in order to accomodate windows
             UI properly */
    scrollbarYDx = 0;
    scrollbarXDy = 0;
    if (fromHistory) {
        startPage = fileHistory->state.pageNo;
        displayMode = fileHistory->state.displayMode;
        offsetX = fileHistory->state.scrollX;
        offsetY = fileHistory->state.scrollY;
    }
    win->dm = DisplayModel_CreateFromPdfDoc(pdfDoc, outputDev, totalDrawAreaSize,
        scrollbarYDx, scrollbarXDy, displayMode, startPage);
    if (!win->dm) {
        delete outputDev;
        WindowInfo_Delete(win);
        return NULL;
    }

#ifdef DEBUG_SHOW_LINKS
    win->dm->debugShowLinks = TRUE;
#endif

    win->dm->appData = (void*)win;

    if (!fromHistory)
        AddFileToHistory(fileName);

Error:
    if (!reuseExistingWindow) {
        if (errNone != err) {
            if (WindowInfoList_ExistsWithError()) {
                /* don't create more than one window with errors */
                WindowInfo_Delete(win);
                return NULL;
            }
        }
        WindowInfoList_Add(win);
    }

    /* TODO: if fromHistory, set the state based on gFileHistoryList node for
       this entry */
    if (errNone != err) {
        win->state = WS_ERROR_LOADING_PDF;
        DBG_OUT("failed to load file %s, error=%d\n", fileName, (int)err);
    } else {
        win->state = WS_SHOWING_PDF;
        zoomVirtual = DEFAULT_ZOOM;
        rotation = DEFAULT_ROTATION;
        if (fromHistory) {
            zoomVirtual = fileHistory->state.zoomVirtual;
            rotation = fileHistory->state.rotation;
        }
        DisplayModel_Relayout(win->dm, zoomVirtual, rotation);
        if (!DisplayModel_ValidPageNo(win->dm, startPage))
            startPage = 1;
        /* TODO: need to calculate proper offsetY, currently giving large offsetY
           remembered for continuous mode breaks things (makes all pages invisible) */
        offsetY = 0;
        /* TODO: make sure offsetX isn't bogus */
        DisplayModel_GoToPage(win->dm, startPage, offsetY);
        DisplayModel_ScrollXTo(win->dm, offsetX);
        /* only resize the window if it's a newly opened window */
        if (!reuseExistingWindow && !fromHistory)
            WindowInfo_ResizeToPage(win, startPage);
    }
    if (reuseExistingWindow)
        WindowInfo_RedrawAll(win);

Exit:
    MenuUpdateStateForAllWindows();
    if (win) {
        DragAcceptFiles(win->hwnd, TRUE);
        ShowWindow(win->hwnd, SW_SHOW);
        UpdateWindow(win->hwnd);
    }
    return win;
}

static void SplashColorSet(SplashColorPtr col, Guchar red, Guchar green, Guchar blue, Guchar alpha)
{
    switch (gSplashColorMode)
    {
        case splashModeBGR8:
            col[0] = blue;
            col[1] = green;
            col[2] = red;
            break;
        case splashModeRGB8:
            col[0] = red;
            col[1] = green;
            col[2] = blue;
            break;
        default:
            assert(0);
            break;
    }
}

static void ColorsInit(void)
{
    /* Colors for poppler's splash rendering backend */
    SplashColorSet(SPLASH_COL_RED_PTR, 0xff, 0, 0, 0);
    SplashColorSet(SPLASH_COL_GREEN_PTR, 0, 0xff, 0, 0);
    SplashColorSet(SPLASH_COL_BLUE_PTR, 0, 0, 0xff, 0);
    SplashColorSet(SPLASH_COL_BLACK_PTR, 0, 0, 0, 0);
    SplashColorSet(SPLASH_COL_WHITE_PTR, 0xff, 0xff, 0xff, 0);
}

static HFONT Win32_Font_GetSimple(HDC hdc, char *fontName, int fontSize)
{
    HFONT       font_dc;
    HFONT       font;
    LOGFONT     lf = {0};

    font_dc = (HFONT)GetStockObject(SYSTEM_FONT);
    if (!GetObject(font_dc, sizeof(LOGFONT), &lf))
        return NULL;

    lf.lfHeight = (LONG)-fontSize;
    lf.lfWidth = 0;
    //lf.lfHeight = -MulDiv(fontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfItalic = FALSE;
    lf.lfUnderline = FALSE;
    lf.lfStrikeOut = FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH;    
    strcpy_s(lf.lfFaceName, LF_FACESIZE, fontName);
    lf.lfWeight = FW_DONTCARE;
    font = CreateFontIndirect(&lf);
    return font;
}

static void Win32_Font_Delete(HFONT font)
{
    DeleteObject(font);
}

void DisplayModel_PageChanged(DisplayModel *dm, int currPageNo)
{
    char            titleBuf[256];
    const char *    baseName;
    int             pageCount;
    HRESULT         hr;
    WindowInfo *    win;

    assert(dm);
    if (!dm) return;

    win = (WindowInfo*)dm->appData;
    assert(win);
    if (!win) return;

    if (!win->dm->pdfDoc)
        return;

    pageCount = win->dm->pdfDoc->getNumPages();
    baseName = Path_GetBaseName(win->dm->pdfDoc->getFileName()->getCString());
    if (pageCount <= 0)
        WinSetText(win->hwnd, baseName);
    else {
        hr = StringCchPrintfA(titleBuf, dimof(titleBuf), "%s page %d of %d", baseName, currPageNo, pageCount);
        WinSetText(win->hwnd, titleBuf);
    }
}

void DisplayModel_RepaintDisplay(DisplayModel *dm)
{
    WindowInfo *win;

    assert(dm);
    if (!dm) return;

    win = (WindowInfo*)dm->appData;
    assert(win);
    if (!win) return;

    WindowInfo_RedrawAll(win);
}

void DisplayModel_SetScrollbarsState(DisplayModel *dm)
{
    WindowInfo *    win;
    SCROLLINFO      si = {0};
    int             canvasDx, canvasDy;
    int             drawAreaDx, drawAreaDy;
    int             offsetX, offsetY;

    assert(dm);
    if (!dm) return;

    win = (WindowInfo*)dm->appData;
    assert(win);
    if (!win) return;

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;

    canvasDx = (int)dm->canvasSize.dx;
    canvasDy = (int)dm->canvasSize.dy;
    drawAreaDx = (int)dm->drawAreaSize.dx;
    drawAreaDy = (int)dm->drawAreaSize.dy;
    offsetX = (int)dm->areaOffset.x;
    offsetY = (int)dm->areaOffset.y;

    if (drawAreaDx >= canvasDx) {
        si.nPos = 0;
        si.nMin = 0;
        si.nMax = 99;
        si.nPage = 100;
    } else {
        si.nPos = offsetX;
        si.nMin = 0;
        si.nMax = canvasDx-1;
        si.nPage = drawAreaDx;
    }
    SetScrollInfo(win->hwnd, SB_HORZ, &si, TRUE);

    if (drawAreaDy >= canvasDy) {
        si.nPos = 0;
        si.nMin = 0;
        si.nMax = 99;
        si.nPage = 100;
    } else {
        si.nPos = offsetY;
        si.nMin = 0;
        si.nMax = canvasDy-1;
        si.nPage = drawAreaDy;
    }
    SetScrollInfo(win->hwnd, SB_VERT, &si, TRUE);
}

void WindowInfo_ResizeToWindow(WindowInfo *win)
{
    DisplayModel *dm;
    RectDSize     totalDrawAreaSize;

    assert(win);
    if (!win) return;

    dm = win->dm;
    assert(dm);
    if (!dm) return;

    WindowInfo_GetWindowSize(win);

    totalDrawAreaSize.dx = (double)win->winDx;
    totalDrawAreaSize.dy = (double)win->winDy;
    DisplayModel_SetTotalDrawAreaSize(dm, totalDrawAreaSize);
}

void WindowInfo_ResizeToPage(WindowInfo *win, int pageNo)
{
    int                 dx, dy;
    int                 displayDx, displayDy;
    BOOL                fullScreen = FALSE;
    DisplaySettings *   displaySettings;
    DisplayModel *      dm;
    PdfPageInfo *       pageInfo;
    HDC                 hdc;

    assert(win);
    if (!win) return;
    dm = win->dm;
    assert(dm);
    if (!dm)
        return;

    /* TODO: should take current monitor into account? */
    hdc = GetDC(win->hwnd);
    displayDx = GetDeviceCaps(hdc, HORZRES);
    displayDy = GetDeviceCaps(hdc, VERTRES);

    if (fullScreen) {
        /* TODO: fullscreen not yet supported */
        assert(0);
        dx = displayDx;
        dy = displayDy;
    } else {
        assert(DisplayModel_ValidPageNo(dm, pageNo));
        if (!DisplayModel_ValidPageNo(dm, pageNo))
            return;
        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        assert(pageInfo);
        if (!pageInfo)
            return;
        displaySettings = DisplayModel_GetGlobalDisplaySettings();
        dx = pageInfo->currDx + displaySettings->paddingPageBorderLeft + displaySettings->paddingPageBorderRight;
        dy = pageInfo->currDy + displaySettings->paddingPageBorderTop + displaySettings->paddingPageBorderBottom;
        if (dx > displayDx - 10)
            dx = displayDx - 10;
        if (dy > displayDy - 10)
            dy = displayDy - 10;
    }

    WinResizeClientArea(win->hwnd, dx, dy);
}

void WindowInfo_ToggleZoom(WindowInfo *win)
{
    DisplayModel *  dm;

    assert(win);
    if (!win) return;

    dm = win->dm;
    assert(dm);
    if (!dm) return;

    if (ZOOM_FIT_PAGE == dm->zoomVirtual)
        DisplayModel_SetZoomVirtual(dm, ZOOM_FIT_WIDTH);
    else if (ZOOM_FIT_WIDTH == dm->zoomVirtual)
        DisplayModel_SetZoomVirtual(dm, ZOOM_FIT_PAGE);
}

BOOL WindowInfo_PdfLoaded(WindowInfo *win)
{
    assert(win);
    if (!win) return FALSE;
    if (!win->dm) return FALSE;
    assert(win->dm->pdfDoc);
    assert(win->dm->pdfDoc->isOk());
    return TRUE;
}

/* 'txt' is path that can be:
  - escaped, in which case it starts with '"', ends with '"' and each '"' that is part of the name is escaped
    with '\'
  - unescaped, in which case it start with != '"' and ends with ' ' or eol (0)
  This function extracts escaped or unescaped path from 'txt'. Returns NULL in case of error.
  Caller needs to free() the result. */
char *PossiblyUnescapePath(char *txt)
{
    char   *exePathStart, *exePathEnd;
    char   *exePath, *src, *dst;
    char   c;
    int     exePathLen;
    BOOL    escaped;

    if (!txt)
        return NULL;

    /* exe path is the first string in command line. It's an escaped string */
    exePathStart = txt;
    escaped = FALSE;
    if ('"' == *exePathStart) {
        ++exePathStart;
        escaped = TRUE;
    }
    exePathEnd = exePathStart;
    for (;;) {
        c = *exePathEnd;
        if (!escaped && ((0 == c) || (' ' == c)))
            break;
        assert(0 != c);
        if (0 == c)
            return NULL;
        if (escaped && ('"' == c))
            break;
        if (escaped && ('\\' == c) && ('"' == exePathEnd[1]))
            ++exePathEnd;
        ++exePathEnd;
    }
    /* might be a bit bigger due to un-escaping, but that's ok */
    exePathLen = (exePathEnd - exePathStart);
    exePath = (TCHAR*)malloc(sizeof(TCHAR)*exePathLen+1);
    if (!exePath)
        return NULL;
    src = exePathStart;
    dst = exePath;
    while (src < exePathEnd) {
        c = *src;
        if (!escaped && ((0 == *src) || (' ' == *src)))
            break;
        assert(0 != *src);
        if (('\\' == c) && ('"' == src[1])) {
            /* unescaping */
            ++src;
            c = *src;
            assert(0 != *src);
        }
        *dst++ = c;
        ++src;
    }
    if (escaped)
        assert('"' == *src);
    else
        assert((' ' == *src) || (0 == *src));
    *dst = 0;
    return exePath;
}

/* Return the full exe path of my own executable.
   Caller needs to free() the result. */
char *ExePathGet(void)
{
    return PossiblyUnescapePath(GetCommandLine());
}

static void RefreshIcons(void)
{
    DString ds;
    BYTE    buff[256];
    HKEY    hKey;
    DWORD   sz;
    DWORD   typ = REG_SZ;
    LONG    result;
    int     origIconSize;

    result = ::RegOpenKeyEx(HKEY_CURRENT_USER, "Control Panel\\Desktop\\WindowMetrics", 0, KEY_READ, &hKey);

    sz = sizeof(buff);
    RegQueryValueEx(hKey, "Shell Icon Size", 0, &typ, buff, &sz);
    RegCloseKey(hKey);

    origIconSize = atoi((const char*)buff);

    DStringInit(&ds);
    DStringSprintf(&ds, "%d", origIconSize+1);

    result = ::RegOpenKeyEx(HKEY_CURRENT_USER, "Control Panel\\Desktop\\WindowMetrics", 0, KEY_WRITE, &hKey);

    RegSetValueEx(hKey,"Shell Icon Size", 0, REG_SZ, (const BYTE*)ds.pString, ds.length);
    RegCloseKey(hKey);

    ::SendMessage(HWND_BROADCAST, WM_SETTINGCHANGE,SPI_SETNONCLIENTMETRICS,NULL);

    result = ::RegOpenKeyEx(HKEY_CURRENT_USER, "Control Panel\\Desktop\\WindowMetrics", 0, KEY_WRITE, &hKey);
    RegSetValueEx(hKey,"Shell Icon Size",0,REG_SZ, (const BYTE*)buff,strlen((const char*)buff));
    RegCloseKey(hKey);

    ::SendMessage(HWND_BROADCAST, WM_SETTINGCHANGE,SPI_SETNONCLIENTMETRICS,NULL);
}

/* Make my app the default app for PDF files. */
void AssociatePdfWithExe(TCHAR *exePath)
{
    char        tmp[256];
    HKEY        key = NULL, kicon = NULL, kshell = NULL, kopen = NULL, kcmd = NULL;
    DWORD       disp;
    HRESULT     hr;
    if (!exePath)
        return;

    /* HKEY_CLASSES_ROOT\.pdf */
    if (RegCreateKeyEx(HKEY_CLASSES_ROOT,
                ".pdf", 0, NULL, REG_OPTION_NON_VOLATILE,
                KEY_WRITE, NULL, &key, &disp))
        goto Exit;

    if (RegSetValueEx(key, "", 0, REG_SZ, (const BYTE*)APP_NAME, sizeof(APP_NAME)))
        goto Exit;

    RegCloseKey(key);
    key = NULL;

    /* HKEY_CLASSES_ROOT\APP_NAME */
    if (RegCreateKeyEx(HKEY_CLASSES_ROOT,
                APP_NAME, 0, NULL, REG_OPTION_NON_VOLATILE,
                KEY_WRITE, NULL, &key, &disp))
        goto Exit;

    if (RegSetValueEx(key, "", 0, REG_SZ, (const BYTE*)PDF_DOC_NAME, sizeof(PDF_DOC_NAME)))
        goto Exit;

    /* HKEY_CLASSES_ROOT\APP_NAME\DefaultIcon */
    if (RegCreateKeyEx(key,
                "DefaultIcon", 0, NULL, REG_OPTION_NON_VOLATILE,
                KEY_WRITE, NULL, &kicon, &disp))
        goto Exit;

    /* Note: I don't understand why icon index has to be 0, but it just has to */
    hr = StringCchPrintfA(tmp, dimof(tmp), "%s,0", exePath);
    if (RegSetValueEx(kicon, "", 0, REG_SZ, (const BYTE*)tmp, strlen(tmp)+1))
        goto Exit;

    RegCloseKey(kicon);
    kicon = NULL;

    /* HKEY_CLASSES_ROOT\APP_NAME\Shell\Open\Command */

    if (RegCreateKeyEx(key,
                "shell", 0, NULL, REG_OPTION_NON_VOLATILE,
                KEY_WRITE, NULL, &kshell, &disp))
        goto Exit;

    if (RegCreateKeyEx(kshell,
                "open", 0, NULL, REG_OPTION_NON_VOLATILE,
                KEY_WRITE, NULL, &kopen, &disp))
        goto Exit;

    if (RegCreateKeyEx(kopen,
                "command", 0, NULL, REG_OPTION_NON_VOLATILE,
                KEY_WRITE, NULL, &kcmd, &disp))
        goto Exit;

    hr = StringCchPrintfA(tmp,  dimof(tmp), "\"%s\" \"%%1\"", exePath);
    if (RegSetValueEx(kcmd, "", 0, REG_SZ, (const BYTE*)tmp, strlen(tmp)+1))
        goto Exit;

Exit:
    if (kcmd)
        RegCloseKey(kcmd);
    if (kopen)
        RegCloseKey(kopen);
    if (kshell)
        RegCloseKey(kshell);
    if (key)
        RegCloseKey(key);

    //RefreshIcons();
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
        case WM_INITDIALOG:
            return (INT_PTR)TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hDlg, LOWORD(wParam));
                return (INT_PTR)TRUE;
            }
            break;
    }
    return (INT_PTR)FALSE;
}

static void OnDropFiles(WindowInfo *win, HDROP hDrop)
{
    int         i;
    char        filename[MAX_PATH];
    const int   files_count = DragQueryFile(hDrop, DRAGQUERY_NUMFILES, 0, 0);

    for (i = 0; i < files_count; i++)
    {
        DragQueryFile(hDrop, i, filename, MAX_PATH);
        LoadPdf(filename, TRUE);
    }
    DragFinish(hDrop);

    if (files_count > 0)
        WindowInfo_RedrawAll(win);
}

void DrawLineSimple(HDC hdc, int sx, int sy, int ex, int ey)
{
    MoveToEx(hdc, sx, sy, NULL);
    LineTo(hdc, ex, ey);
}

/* Draw caption area for a given window 'win' in the classic AmigaOS style */
void AmigaCaptionDraw(WindowInfo *win)
{
    HGDIOBJ prevPen;
    HDC     hdc = win->hdc;

    assert(VS_AMIGA == gVisualStyle);

    prevPen = SelectObject(hdc, ghpenWhite);

    /* white */
    DrawLineSimple(hdc, 0, 0, win->winDx, 0);
    DrawLineSimple(hdc, 0, 1, win->winDx, 1);

    /* white */
    DrawLineSimple(hdc, 0, 4, win->winDx, 4);
    DrawLineSimple(hdc, 0, 5, win->winDx, 5);

    /* white */
    DrawLineSimple(hdc, 0, 8, win->winDx, 8);
    DrawLineSimple(hdc, 0, 9, win->winDx, 9);

    /* white */
    DrawLineSimple(hdc, 0, 12, win->winDx, 12);
    DrawLineSimple(hdc, 0, 13, win->winDx, 13);

    /* white */
    DrawLineSimple(hdc, 0, 16, win->winDx, 16);
    DrawLineSimple(hdc, 0, 17, win->winDx, 17);
    DrawLineSimple(hdc, 0, 18, win->winDx, 18);

    SelectObject(hdc, ghpenBlue);

    /* blue */
    DrawLineSimple(hdc, 0, 2, win->winDx, 2);
    DrawLineSimple(hdc, 0, 3, win->winDx, 3);

    /* blue */
    DrawLineSimple(hdc, 0, 6, win->winDx, 6);
    DrawLineSimple(hdc, 0, 7, win->winDx, 7);

    /* blue */
    DrawLineSimple(hdc, 0, 10, win->winDx, 10);
    DrawLineSimple(hdc, 0, 11, win->winDx, 11);

    /* blue */
    DrawLineSimple(hdc, 0, 14, win->winDx, 14);
    DrawLineSimple(hdc, 0, 15, win->winDx, 15);

    SelectObject(hdc, prevPen);
}

void WinResizeIfNeeded(WindowInfo *win)
{
    RECT    rc;
    int     win_dx, win_dy;
    GetClientRect(win->hwnd, &rc);
    win_dx = RectDx(&rc);
    win_dy = RectDy(&rc);

    if ((win_dx == win->winDx) &&
        (win_dy == win->winDy) && win->hdcToDraw)
    {
        return;
    }

    WindowInfo_DoubleBuffer_New(win);
    WindowInfo_ResizeToWindow(win);
}

void PostBenchNextAction(HWND hwnd)
{
    PostMessage(hwnd, MSG_BENCH_NEXT_ACTION, 0, 0);
}

void OnBenchNextAction(WindowInfo *win)
{
    if (!win->dm)
        return;

    if (DisplayModel_GoToNextPage(win->dm, 0))
        PostBenchNextAction(win->hwnd);
}

void WindowInfo_Paint(WindowInfo *win, HDC hdc, PAINTSTRUCT *ps)
{
    int                   pageNo;
    PdfPageInfo*          pageInfo;
    DisplayModel *        dm;
    SplashBitmap *        splashBmp;
    int                   splashBmpDx, splashBmpDy;
    SplashColorMode       splashBmpColorMode;
    SplashColorPtr        splashBmpData;
    int                   splashBmpRowSize;
    int                   xSrc, ySrc, xDest, yDest;
    int                   bmpDx, bmpDy;
    int                   linkNo;
    PdfLink *             pdfLink;
    SimpleRect            drawAreaRect;
    SimpleRect            intersect;
    SimpleRect            rectLink;
    RECT                  rectScreen;
    HBITMAP               hbmp = NULL;
    BITMAPINFOHEADER      bmih;
    HDC                   bmpDC = NULL;

    assert(win);
    if (!win) return;
    dm = win->dm;
    assert(dm);
    if (!dm) return;
    assert(dm->pdfDoc);
    if (!dm->pdfDoc) return;

    assert(win->hdcToDraw);
    hdc = win->hdcToDraw;

    FillRect(hdc, &(ps->rcPaint), gBrushBg);

    for (pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        pageInfo = DisplayModel_GetPageInfo(dm, pageNo);
        if (!pageInfo->visible)
            continue;
        assert(pageInfo->shown);
        if (!pageInfo->shown)
            continue;
        splashBmp = pageInfo->bitmap;
        if (!splashBmp) {
            DBG_OUT("DisplayModel_Draw() missing bitmap on visible page %d\n", pageNo+1);
            continue;
        }

        splashBmpDx = splashBmp->getWidth();
        splashBmpDy = splashBmp->getHeight();
        splashBmpRowSize = splashBmp->getRowSize();
        splashBmpData = splashBmp->getDataPtr();
        splashBmpColorMode = splashBmp->getMode();

        xSrc = (int)pageInfo->bitmapX;
        ySrc = (int)pageInfo->bitmapY;
        bmpDx = (int)pageInfo->bitmapDx;
        bmpDy = (int)pageInfo->bitmapDy;
        xDest = (int)pageInfo->screenX;
        yDest = (int)pageInfo->screenY;

        bmih.biSize = sizeof(bmih);
        bmih.biHeight = -splashBmpDy;
        bmih.biWidth = splashBmpDx;
        bmih.biPlanes = 1;
        bmih.biBitCount = 24;
        bmih.biCompression = BI_RGB;
        bmih.biSizeImage = splashBmpDy * splashBmpRowSize;;
        bmih.biXPelsPerMeter = bmih.biYPelsPerMeter = 0;
        bmih.biClrUsed = bmih.biClrImportant = 0;

        hbmp = CreateDIBitmap(hdc, &bmih, CBM_INIT, splashBmpData, (BITMAPINFO *)&bmih , DIB_RGB_COLORS );
        if (hbmp) {
            bmpDC = CreateCompatibleDC(hdc);
            if (bmpDC) {
                SelectObject(bmpDC, hbmp);
                BitBlt(hdc, xDest, yDest, bmpDx, bmpDy, bmpDC, xSrc, ySrc, SRCCOPY);
                DeleteDC(bmpDC);
                bmpDC = NULL;
            }
            DeleteObject(hbmp);
            hbmp = NULL;
        }
    }

    if (!dm->debugShowLinks)
        return;

    /* debug code to visualize links */
    drawAreaRect.x = (int)dm->areaOffset.x;
    drawAreaRect.y = (int)dm->areaOffset.y;
    drawAreaRect.dx = (int)dm->drawAreaSize.dx;
    drawAreaRect.dy = (int)dm->drawAreaSize.dy;

    for (linkNo = 0; linkNo < dm->linkCount; ++linkNo) {
        pdfLink = &(dm->links[linkNo]);

        rectLink.x = pdfLink->rectCanvas.x;
        rectLink.y = pdfLink->rectCanvas.y;
        rectLink.dx = pdfLink->rectCanvas.dx;
        rectLink.dy = pdfLink->rectCanvas.dy;

        if (SimpleRect_Intersect(&rectLink, &drawAreaRect, &intersect)) {
            rectScreen.left = (LONG) ((double)intersect.x - dm->areaOffset.x);
            rectScreen.top = (LONG) ((double)intersect.y - dm->areaOffset.y);
            rectScreen.right = rectScreen.left + rectLink.dx;
            rectScreen.bottom = rectScreen.top + rectLink.dy;
            FillRect(hdc, &rectScreen, gBrushLinkDebug);
            DBG_OUT("  link on screen rotate=%d, (x=%d, y=%d, dx=%d, dy=%d)\n",
                dm->rotation + dm->pagesInfo[pdfLink->pageNo-1].rotation,
                rectScreen.left, rectScreen.top, RectDx(&rectScreen), RectDy(&rectScreen));
        }
    }
}

/* TODO: change the name to DrawAbout.
   Draws the about screen a remember some state for hyperlinking.
   It transcribes the design I did in graphics software - hopeless
   to understand without seeing the design. */
#define ABOUT_RECT_PADDING          8
#define ABOUT_RECT_BORDER_DX_DY     4
#define ABOUT_LINE_OUTER_SIZE       2
#define ABOUT_LINE_RECT_SIZE        5
#define ABOUT_LINE_SEP_SIZE         1
#define ABOUT_LEFT_RIGHT_SPACE_DX   8
#define ABOUT_MARGIN_DX            10
#define ABOUT_BOX_MARGIN_DY         6

#define ABOUT_BORDER_COL            COL_BLACK

#define SUMATRA_TXT             "Sumatra PDF"
#define SUMATRA_TXT_FONT        "Arial Black"
#define SUMATRA_TXT_FONT_SIZE   24
#define BETA_TXT                "Beta v0.2"
#define BETA_TXT_FONT           "Arial Black"
#define BETA_TXT_FONT_SIZE      12
#define LEFT_TXT_FONT           "Arial"
#define LEFT_TXT_FONT_SIZE      12
#define RIGHT_TXT_FONT          "Arial Black"
#define RIGHT_TXT_FONT_SIZE     12

#define ABOUT_BG_COLOR          RGB(255,242,0)
#define ABOUT_RECT_BG_COLOR     RGB(247,148,29)

#define ABOUT_TXT_DY            6

typedef struct AboutLayoutInfoEl {
    const char *    leftTxt;
    const char *    rightTxt;
    const char *    url;

    int             leftTxtPosX;
    int             leftTxtPosY;
    int             leftTxtDx;
    int             leftTxtDy;

    int             rightTxtPosX;
    int             rightTxtPosY;
    int             rightTxtDx;
    int             rightTxtDy;
} AboutLayoutInfoEl;

AboutLayoutInfoEl gAboutLayoutInfo[] = {
    { "design", "Krzysztof Kowalczyk", "http://blog.kowalczyk.info",
    0, 0, 0, 0, 0, 0, 0, 0 },

    { "programming", "Krzysztof Kowalczyk", "http://blog.kowalczyk.info",
    0, 0, 0, 0, 0, 0, 0, 0 },

    { "pdf rendering", "poppler + xpdf", "http://poppler.freedesktop.org/",
    0, 0, 0, 0, 0, 0, 0, 0 },

    { "license", "GPL v2", "http://www.gnu.org/copyleft/gpl.html",
    0, 0, 0, 0, 0, 0, 0, 0 },

    { "website", "http://blog.kowalczyk.info/software/sumatra", "http://blog.kowalczyk.info/software/sumatrapdf",
    0, 0, 0, 0, 0, 0, 0, 0 },

    { "forums", "http://blog.kowalczyk.info/forum_sumatra", "http://blog.kowalczyk.info/forum_sumatra",
    0, 0, 0, 0, 0, 0, 0, 0 },

    { NULL, NULL, NULL,
    0, 0, 0, 0, 0, 0, 0, 0 }
};

const char *AboutGetLink(WindowInfo *win, int x, int y)
{
    int         i;

    for (i = 0; gAboutLayoutInfo[i].leftTxt; i++) {
        if ((x < gAboutLayoutInfo[i].rightTxtPosX) ||
            (x > gAboutLayoutInfo[i].rightTxtPosX + gAboutLayoutInfo[i].rightTxtDx))
            continue;
        if ((y < gAboutLayoutInfo[i].rightTxtPosY) ||
            (y > gAboutLayoutInfo[i].rightTxtPosY + gAboutLayoutInfo[i].rightTxtDy))
            continue;
        return gAboutLayoutInfo[i].url;
    }
    return NULL;
}

static void DrawAnim(WindowInfo *win, HDC hdc, PAINTSTRUCT *ps)
{
    AnimState *     state = &(win->animState);
    DString         str;
    RECT            rc;
    RECT            rcTmp;
    HFONT           origFont = NULL;
    HFONT           fontSumatraTxt = NULL;
    HFONT           fontBetaTxt = NULL;
    HFONT           fontLeftTxt = NULL;
    HFONT           fontRightTxt = NULL;
    int             areaDx, areaDy;
    int             i;
    SIZE            txtSize;
    const char *    txt;
    int             totalDx, totalDy;
    int             leftDy, rightDy;
    int             leftLargestDx, rightLargestDx;
    int             sumatraPdfTxtDx, sumatraPdfTxtDy;
    int             betaTxtDx, betaTxtDy;
    HBRUSH          brushBg = NULL, brushRectBg = NULL;
    HPEN            penRectBorder = NULL, penBorder = NULL, penDivideLine = NULL;
    int             linePosX, linePosY, lineDy;
    int             currY;
    int             fontDyDiff;
    int             offX, offY;
    int             x, y;
    int             boxDy;

    brushBg = CreateSolidBrush(ABOUT_BG_COLOR);
    brushRectBg = CreateSolidBrush(ABOUT_RECT_BG_COLOR);

    penRectBorder = CreatePen(PS_SOLID, ABOUT_RECT_BORDER_DX_DY, COL_BLACK);
    penBorder = CreatePen(PS_SOLID, ABOUT_LINE_OUTER_SIZE, COL_BLACK);
    penDivideLine = CreatePen(PS_SOLID, ABOUT_LINE_SEP_SIZE, COL_BLACK);

    GetClientRect(win->hwnd, &rc);

    DStringInit(&str);

    areaDx = RectDx(&rc);
    areaDy = RectDy(&rc);

    fontSumatraTxt = Win32_Font_GetSimple(hdc, SUMATRA_TXT_FONT, SUMATRA_TXT_FONT_SIZE);
    fontBetaTxt = Win32_Font_GetSimple(hdc, BETA_TXT_FONT, BETA_TXT_FONT_SIZE);
    fontLeftTxt = Win32_Font_GetSimple(hdc, LEFT_TXT_FONT, LEFT_TXT_FONT_SIZE);
    fontRightTxt = Win32_Font_GetSimple(hdc, RIGHT_TXT_FONT, RIGHT_TXT_FONT_SIZE);

    origFont = (HFONT)SelectObject(hdc, fontSumatraTxt); /* Just to remember the orig font */

    SetBkMode(hdc, TRANSPARENT);

    /* Layout stuff */
    txt = SUMATRA_TXT;
    GetTextExtentPoint32(hdc, txt, strlen(txt), &txtSize);
    sumatraPdfTxtDx = txtSize.cx;
    sumatraPdfTxtDy = txtSize.cy;

    boxDy = sumatraPdfTxtDy + ABOUT_BOX_MARGIN_DY * 2;
    txt = BETA_TXT;
    GetTextExtentPoint32(hdc, txt, strlen(txt), &txtSize);
    betaTxtDx = txtSize.cx;
    betaTxtDy = txtSize.cy;

    (HFONT)SelectObject(hdc, fontLeftTxt);
    leftLargestDx = 0;
    leftDy = 0;
    for (i = 0; gAboutLayoutInfo[i].leftTxt != NULL; i++) {
        txt = gAboutLayoutInfo[i].leftTxt;
        GetTextExtentPoint32(hdc, txt, strlen(txt), &txtSize);
        gAboutLayoutInfo[i].leftTxtDx = (int)txtSize.cx;
        gAboutLayoutInfo[i].leftTxtDy = (int)txtSize.cy;
        if (0 == i)
            leftDy = gAboutLayoutInfo[i].leftTxtDy;
        else
            assert(leftDy == gAboutLayoutInfo[i].leftTxtDy);
        if (leftLargestDx < gAboutLayoutInfo[i].leftTxtDx)
            leftLargestDx = gAboutLayoutInfo[i].leftTxtDx;
    }

    (HFONT)SelectObject(hdc, fontRightTxt);
    rightLargestDx = 0;
    rightDy = 0;
    for (i = 0; gAboutLayoutInfo[i].leftTxt != NULL; i++) {
        txt = gAboutLayoutInfo[i].rightTxt;
        GetTextExtentPoint32(hdc, txt, strlen(txt), &txtSize);
        gAboutLayoutInfo[i].rightTxtDx = (int)txtSize.cx;
        gAboutLayoutInfo[i].rightTxtDy = (int)txtSize.cy;
        if (0 == i)
            rightDy = gAboutLayoutInfo[i].rightTxtDy;
        else
            assert(rightDy == gAboutLayoutInfo[i].rightTxtDy);
        if (rightLargestDx < gAboutLayoutInfo[i].rightTxtDx)
            rightLargestDx = gAboutLayoutInfo[i].rightTxtDx;
    }

    fontDyDiff = (rightDy - leftDy) / 2;

    /* in the x order */
    totalDx  = ABOUT_LINE_OUTER_SIZE + ABOUT_MARGIN_DX + leftLargestDx;
    totalDx += ABOUT_LEFT_RIGHT_SPACE_DX + ABOUT_LINE_SEP_SIZE + ABOUT_LEFT_RIGHT_SPACE_DX;
    totalDx += rightLargestDx + ABOUT_MARGIN_DX + ABOUT_LINE_OUTER_SIZE;

    totalDy = 0;
    totalDy += boxDy;
    totalDy += ABOUT_LINE_OUTER_SIZE;
    totalDy += (dimof(gAboutLayoutInfo)-1) * (rightDy + ABOUT_TXT_DY);
    totalDy += ABOUT_LINE_OUTER_SIZE + 4;

    offX = (areaDx - totalDx) / 2;
    offY = (areaDy - totalDy) / 2;

    rcTmp.left = offX;
    rcTmp.top = offY;
    rcTmp.right = totalDx + offX;
    rcTmp.bottom = totalDy + offY;

    FillRect(hdc, &rc, brushBg);

    SelectObject(hdc, brushBg);
    SelectObject(hdc, penBorder);

    Rectangle(hdc, offX, offY + ABOUT_LINE_OUTER_SIZE, offX + totalDx, offY + boxDy + ABOUT_LINE_OUTER_SIZE);

    SetTextColor(hdc, ABOUT_BORDER_COL);
    (HFONT)SelectObject(hdc, fontSumatraTxt);
    x = offX + (totalDx - sumatraPdfTxtDx) / 2;
    y = offY + (boxDy - sumatraPdfTxtDy) / 2;
    txt = SUMATRA_TXT;
    TextOut(hdc, x, y, txt, strlen(txt));

    //SetTextColor(hdc, ABOUT_RECT_BG_COLOR);
    (HFONT)SelectObject(hdc, fontBetaTxt);
    //SelectObject(hdc, brushRectBg);
    x = offX + (totalDx - sumatraPdfTxtDx) / 2 + sumatraPdfTxtDx + 2;
    y = offY + (boxDy - sumatraPdfTxtDy) / 2;
    txt = BETA_TXT;
    TextOut(hdc, x, y, txt, strlen(txt));
    SetTextColor(hdc, ABOUT_BORDER_COL);

    offY += boxDy;
    Rectangle(hdc, offX, offY, offX + totalDx, offY + totalDy - boxDy);

    linePosX = ABOUT_LINE_OUTER_SIZE + ABOUT_MARGIN_DX + leftLargestDx + ABOUT_LEFT_RIGHT_SPACE_DX;
    linePosY = 4;
    lineDy = (dimof(gAboutLayoutInfo)-1) * (rightDy + ABOUT_TXT_DY);

    /* render text on the left*/
    currY = linePosY;
    (HFONT)SelectObject(hdc, fontLeftTxt);
    for (i = 0; gAboutLayoutInfo[i].leftTxt != NULL; i++) {
        txt = gAboutLayoutInfo[i].leftTxt;
        x = linePosX + offX - ABOUT_LEFT_RIGHT_SPACE_DX - gAboutLayoutInfo[i].leftTxtDx;
        y = currY + fontDyDiff + offY;
        gAboutLayoutInfo[i].leftTxtPosX = x;
        gAboutLayoutInfo[i].leftTxtPosY = y;
        TextOut(hdc, x, y, txt, strlen(txt));
        currY += rightDy + ABOUT_TXT_DY;
    }

    /* render text on the rigth */
    currY = linePosY;
    (HFONT)SelectObject(hdc, fontRightTxt);
    for (i = 0; gAboutLayoutInfo[i].leftTxt != NULL; i++) {
        txt = gAboutLayoutInfo[i].rightTxt;
        x = linePosX + offX + ABOUT_LEFT_RIGHT_SPACE_DX;
        y = currY + offY;
        gAboutLayoutInfo[i].rightTxtPosX = x;
        gAboutLayoutInfo[i].rightTxtPosY = y;
        TextOut(hdc, x, y, txt, strlen(txt));
        currY += rightDy + ABOUT_TXT_DY;
    }

    SelectObject(hdc, penDivideLine);
    MoveToEx(hdc, linePosX + offX, linePosY + offY, NULL);
    LineTo(hdc, linePosX + offX, linePosY + lineDy + offY);

    if (origFont)
        SelectObject(hdc, origFont);

    Win32_Font_Delete(fontSumatraTxt);
    Win32_Font_Delete(fontBetaTxt);
    Win32_Font_Delete(fontLeftTxt);
    Win32_Font_Delete(fontRightTxt);

    DeleteObject(brushBg);
    DeleteObject(brushRectBg);
    DeleteObject(penBorder);
    DeleteObject(penDivideLine);
    DeleteObject(penRectBorder);
}

static void HandleLink(DisplayModel *dm, PdfLink *pdfLink)
{
    Link *          link;
    LinkAction *    action;
    LinkActionKind  actionKind;

    assert(dm);
    if (!dm) return;

    assert(pdfLink);
    if (!pdfLink) return;

    link = pdfLink->link;
    assert(link);
    if (!link) return;

    action = link->getAction();
    actionKind = action->getKind();

    switch (actionKind) {
        case actionGoTo:
            DisplayModel_HandleLinkGoTo(dm, (LinkGoTo*)action);
            break;
        case actionGoToR:
            DisplayModel_HandleLinkGoToR(dm, (LinkGoToR*)action);
            break;
        case actionLaunch:
            DisplayModel_HandleLinkLaunch(dm, (LinkLaunch*)action);
            break;
        case actionURI:
            DisplayModel_HandleLinkURI(dm, (LinkURI*)action);
            break;
        case actionNamed:
            DisplayModel_HandleLinkNamed(dm, (LinkNamed *)action);
            break;
        default:
            /* other kinds are not supported */
            break;
    }
}

static void OnMouseLeftButtonDown(WindowInfo *win, int x, int y)
{
    assert(win);
    if (!win) return;
    if (WS_SHOWING_PDF == win->state) {
        assert(win->dm);
        if (!win->dm) return;
        win->linkOnLastButtonDown = DisplayModel_GetLinkAtPosition(win->dm, x, y);
    } else if (WS_ABOUT_ANIM == win->state) {
        win->url = AboutGetLink(win, x, y);
    }
}

static void OnMouseLeftButtonUp(WindowInfo *win, int x, int y)
{
    PdfLink *       link;
    const char *    url;

    assert(win);
    if (!win) return;

    if (WS_SHOWING_PDF == win->state) {
        assert(win->dm);
        if (!win->dm) return;

        if (!win->linkOnLastButtonDown)
            return;

        link = DisplayModel_GetLinkAtPosition(win->dm, x, y);
        if (link && (link == win->linkOnLastButtonDown))
            HandleLink(win->dm, link);
        win->linkOnLastButtonDown = NULL;
    } else if (WS_ABOUT_ANIM == win->state) {
        url = AboutGetLink(win, x, y);
        if (url == win->url)
            LaunchBrowser(url);
        win->url = NULL;
    }
}

static void OnMouseMove(WindowInfo *win, int x, int y, WPARAM flags)
{
    DisplayModel *  dm;
    PdfLink *       link;
    const char *    url;

    assert(win);
    if (!win) return;

    if (WS_SHOWING_PDF == win->state) {
        dm = win->dm;
        assert(dm);
        if (!dm) return;
        link = DisplayModel_GetLinkAtPosition(dm, x, y);
        if (link) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
        } else {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
    } else if (WS_ABOUT_ANIM == win->state) {
        url = AboutGetLink(win, x, y);
        if (url) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
        } else {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
    }
}

#define ABOUT_ANIM_TIMER_ID 15

void AnimState_AnimStop(AnimState *state)
{
    KillTimer(state->hwnd, ABOUT_ANIM_TIMER_ID);
}

void AnimState_NextFrame(AnimState *state)
{
    state->frame += 1;
    InvalidateRect(state->hwnd, NULL, FALSE);
    UpdateWindow(state->hwnd);
}

void AnimState_AnimStart(AnimState *state, HWND hwnd, UINT freqInMs)
{
    assert(IsWindow(hwnd));
    AnimState_AnimStop(state);
    state->frame = 0;
    state->hwnd = hwnd;
    SetTimer(state->hwnd, ABOUT_ANIM_TIMER_ID, freqInMs, NULL);
    AnimState_NextFrame(state);
}

#define ANIM_FONT_NAME "Georgia"
#define ANIM_FONT_SIZE_START 20
#define SCROLL_SPEED 3

static void DrawAnim2(WindowInfo *win, HDC hdc, PAINTSTRUCT *ps)
{
    AnimState *     state = &(win->animState);
    DString         txt;
    RECT            rc;
    HFONT           fontArial24 = NULL;
    HFONT           origFont = NULL;
    int             curFontSize;
    static int      curTxtPosX = -1;
    static int      curTxtPosY = -1;
    static int      curDir = SCROLL_SPEED;
    int             areaDx, areaDy;

    GetClientRect(win->hwnd, &rc);

    DStringInit(&txt);

    if (-1 == curTxtPosX)
        curTxtPosX = 40;
    if (-1 == curTxtPosY)
        curTxtPosY = 25;

    areaDx = RectDx(&rc);
    areaDy = RectDy(&rc);

#if 0
    if (state->frame % 24 <= 12) {
        curFontSize = ANIM_FONT_SIZE_START + (state->frame % 24);
    } else {
        curFontSize = ANIM_FONT_SIZE_START + 12 - (24 - (state->frame % 24));
    }
#else
    curFontSize = ANIM_FONT_SIZE_START;
#endif

    curTxtPosY += curDir;
    if (curTxtPosY < 20)
        curDir = SCROLL_SPEED;
    else if (curTxtPosY > areaDy - 40)
        curDir = -SCROLL_SPEED;

    fontArial24 = Win32_Font_GetSimple(hdc, ANIM_FONT_NAME, curFontSize);
    assert(fontArial24);

    origFont = (HFONT)SelectObject(hdc, fontArial24);
    
    SetBkMode(hdc, TRANSPARENT);
    FillRect(hdc, &rc, gBrushBg);
    //DStringSprintf(&txt, "Welcome to animation %d", state->frame);
    DStringSprintf(&txt, "Welcome to animation");
    //DrawText (hdc, txt.pString, -1, &rc, DT_SINGLELINE);
    TextOut(hdc, curTxtPosX, curTxtPosY, txt.pString, txt.length);
    WindowInfo_DoubleBuffer_Show(win, hdc);
    if (state->frame > 99)
        state->frame = 0;

    if (origFont)
        SelectObject(hdc, origFont);
    Win32_Font_Delete(fontArial24);
}

void WindowInfo_DoubleBuffer_Resize_IfNeeded(WindowInfo *win)
{
    RECT    rc;
    int     win_dx, win_dy;
    GetClientRect(win->hwnd, &rc);
    win_dx = RectDx(&rc);
    win_dy = RectDy(&rc);

    if ((win_dx == win->winDx) &&
        (win_dy == win->winDy) && win->hdcToDraw)
    {
        return;
    }

    WindowInfo_DoubleBuffer_New(win);
}

static void OnPaintDrawAnim(WindowInfo *win, HDC hdc, PAINTSTRUCT *ps)
{
    WindowInfo_DoubleBuffer_Resize_IfNeeded(win);
    DrawAnim(win, win->hdcToDraw, ps);
    WindowInfo_DoubleBuffer_Show(win, hdc);
}

static void OnPaint(WindowInfo *win)
{
    HDC         hdc;
    PAINTSTRUCT ps;
    RECT        rc;

    hdc = BeginPaint(win->hwnd, &ps);

    SetBkMode(hdc, TRANSPARENT);
    GetClientRect(win->hwnd, &rc);

    if (WS_ABOUT_ANIM == win->state) {
        OnPaintDrawAnim(win, hdc, &ps);
    } else if (WS_EMPTY == win->state) {
        FillRect(hdc, &ps.rcPaint, gBrushBg);
        DrawText (hdc, "No PDF file opened. Open a new PDF file.", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER) ;
    } else if (WS_ERROR_LOADING_PDF == win->state) {
        FillRect(hdc, &ps.rcPaint, gBrushBg);
        DrawText (hdc, "Error loading PDF file.", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER) ;
    } else if (WS_SHOWING_PDF == win->state) {
        //TODO: it might cause infinite loop due to showing/hiding scrollbars
        WinResizeIfNeeded(win);
        WindowInfo_Paint(win, hdc, &ps);
#if 0
        if (VS_AMIGA == gVisualStyle)
            AmigaCaptionDraw(win);
#endif
        WindowInfo_DoubleBuffer_Show(win, hdc);
    } else
        assert(0);
    EndPaint(win->hwnd, &ps);
}

void OnMenuExit(void)
{
    Prefs_Save();
    PostQuitMessage(0);
}

/* Close the document associated with window 'hwnd'.
   Closes the window unless this is the last window in which
   case it switches to empty window and disables the "File\Close"
   menu item. */
void CloseWindow(WindowInfo *win, BOOL quitIfLast)
{
    BOOL    lastWindow = FALSE;
    HWND    hwndToDestroy = NULL;

    if (1 == WindowInfoList_Len())
        lastWindow = TRUE;


    if (lastWindow)
        Prefs_Save();
    else
        UpdateCurrentFileDisplayStateForWin(win);

    win->state = WS_EMPTY;

    if (lastWindow && !quitIfLast) {
        /* last window - don't delete it */
        //win->pdfCore->clear();
        WindowInfo_RedrawAll(win);
    } else {
        hwndToDestroy = win->hwnd;
        WindowInfoList_Remove(win);
        WindowInfo_Delete(win);
        DestroyWindow(hwndToDestroy);
    }

    if (lastWindow && quitIfLast) {
        assert(0 == WindowInfoList_Len());
        PostQuitMessage(0);
    } else
        MenuUpdateStateForAllWindows();
}

static struct idToZoomMap {
    UINT id;
    double zoom;
} gZoomMenuItemsId[] = {
    { IDM_ZOOM_6400, 6400.0 },
    { IDM_ZOOM_3200, 3200.0 },
    { IDM_ZOOM_1600, 1600.0 },
    { IDM_ZOOM_800, 800.0 },
    { IDM_ZOOM_400, 400.0 },
    { IDM_ZOOM_200, 200.0 },
    { IDM_ZOOM_150, 150.0 },
    { IDM_ZOOM_125, 125.0 },
    { IDM_ZOOM_100, 100.0 },
    { IDM_ZOOM_50, 50.0 },
    { IDM_ZOOM_25, 25.0 },
    { IDM_ZOOM_12_5, 12.5 },
    { IDM_ZOOM_8_33, 8.33 },
    { IDM_ZOOM_FIT_PAGE, ZOOM_FIT_PAGE },
    { IDM_ZOOM_FIT_WIDTH, ZOOM_FIT_WIDTH },
    { IDM_ZOOM_ACTUAL_SIZE, 100.0 }
};

static void ZoomMenuItemCheck(HMENU hmenu, UINT menuItemId)
{
    BOOL    found = FALSE;
    int     i;
    UINT    checkState;

    for (i=0; i<dimof(gZoomMenuItemsId); i++) {
        checkState = MF_BYCOMMAND | MF_UNCHECKED;
        if (menuItemId == gZoomMenuItemsId[i].id) {
            assert(!found);
            found = TRUE;
            checkState = MF_BYCOMMAND | MF_CHECKED;
        }
        CheckMenuItem(hmenu, gZoomMenuItemsId[i].id, checkState);
    }
    assert(found);
}

static double ZoomMenuItemToZoom(UINT menuItemId)
{
    int     i;
    for (i=0; i<dimof(gZoomMenuItemsId); i++) {
        if (menuItemId == gZoomMenuItemsId[i].id) {
            return gZoomMenuItemsId[i].zoom;
        }
    }
    assert(0);
    return 100.0;
}

/* Zoom document in window 'hwnd' to zoom level 'zoom'.
   'zoom' is given as a floating-point number, 1.0 is 100%, 2.0 is 200% etc.
*/
static void OnMenuZoom(WindowInfo *win, UINT menuId)
{
    double       zoom;

    if (!win->dm)
        return;

    zoom = ZoomMenuItemToZoom(menuId);
    DisplayModel_ZoomTo(win->dm, zoom);
    ZoomMenuItemCheck(GetMenu(win->hwnd), menuId);
}

void OnMenuOpen(WindowInfo *win)
{
    OPENFILENAME ofn = {0};
    char         fileName[260];
    GooString      fileNameStr;

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwnd;
    ofn.lpstrFile = fileName;

    // Set lpstrFile[0] to '\0' so that GetOpenFileName does not
    // use the contents of szFile to initialize itself.
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(fileName);
    ofn.lpstrFilter = "PDF\0*.pdf\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    // Display the Open dialog box.
    if (FALSE == GetOpenFileName(&ofn))
        return;

    win = LoadPdf(fileName, TRUE);
    if (!win)
        return;
}

static void RotateLeft(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    DisplayModel_RotateBy(win->dm, -90);
}

static void RotateRight(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    DisplayModel_RotateBy(win->dm, 90);
}

static void OnVScroll(WindowInfo *win, WPARAM wParam)
{
    SCROLLINFO   si = {0};
    int          iVertPos;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(win->hwnd, SB_VERT, &si);

    iVertPos = si.nPos;

    switch (LOWORD(wParam))
    {
        case SB_TOP:
           si.nPos = si.nMin;
           break;

        case SB_BOTTOM:
           si.nPos = si.nMax;
           break;

        case SB_LINEUP:
           si.nPos -= 16;
           break;

        case SB_LINEDOWN:
           si.nPos += 16;
           break;

        case SB_PAGEUP:
           si.nPos -= si.nPage;
           break;

        case SB_PAGEDOWN:
           si.nPos += si.nPage;
           break;

        case SB_THUMBTRACK:
           si.nPos = si.nTrackPos;
           break;

        default:
           break;
    }

    // Set the position and then retrieve it.  Due to adjustments
    // by Windows it may not be the same as the value set.
    si.fMask = SIF_POS;
    SetScrollInfo(win->hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo(win->hwnd, SB_VERT, &si);

    // If the position has changed, scroll the window and update it
    if (win->dm && (si.nPos != iVertPos))
        DisplayModel_ScrollYTo(win->dm, si.nPos);
}

static void OnHScroll(WindowInfo *win, WPARAM wParam)
{
    SCROLLINFO   si = {0};
    int          iVertPos;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(win->hwnd, SB_HORZ, &si);

    iVertPos = si.nPos;

    switch (LOWORD(wParam))
    {
        case SB_TOP:
           si.nPos = si.nMin;
           break;

        case SB_BOTTOM:
           si.nPos = si.nMax;
           break;

        case SB_LINEUP:
           si.nPos -= 16;
           break;

        case SB_LINEDOWN:
           si.nPos += 16;
           break;

        case SB_PAGEUP:
           si.nPos -= si.nPage;
           break;

        case SB_PAGEDOWN:
           si.nPos += si.nPage;
           break;

        case SB_THUMBTRACK:
           si.nPos = si.nTrackPos;
           break;

        default:
           break;
    }

    // Set the position and then retrieve it.  Due to adjustments
    // by Windows it may not be the same as the value set.
    si.fMask = SIF_POS;
    SetScrollInfo(win->hwnd, SB_HORZ, &si, TRUE);
    GetScrollInfo(win->hwnd, SB_HORZ, &si);

    // If the position has changed, scroll the window and update it
    if (win->dm && (si.nPos != iVertPos))
        DisplayModel_ScrollXTo(win->dm, si.nPos);
}

void ViewWithAcrobat(WindowInfo *win)
{
    // TODO: write me
}

static void OnMenuViewSinglePage(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    SwitchToDisplayMode(win, DM_SINGLE_PAGE);
}

static void OnMenuViewFacing(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    SwitchToDisplayMode(win, DM_FACING);
}

static void OnMenuViewContinuous(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    SwitchToDisplayMode(win, DM_CONTINUOUS);
}

static void OnMenuViewContinuousFacing(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    SwitchToDisplayMode(win, DM_CONTINUOUS_FACING);
}

static void OnMenuGoToNextPage(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    DisplayModel_GoToNextPage(win->dm, 0);
}

static void OnMenuGoToPrevPage(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    DisplayModel_GoToPrevPage(win->dm, 0);
}

static void OnMenuGoToLastPage(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    DisplayModel_GoToLastPage(win->dm);
}

static void OnMenuGoToFirstPage(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    DisplayModel_GoToFirstPage(win->dm);
}

static void OnMenuGoToPage(WindowInfo *win)
{
    int     newPageNo;

    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;

    newPageNo = Dialog_GoToPage(win);
    if (DisplayModel_ValidPageNo(win->dm, newPageNo)) {
        DisplayModel_GoToPage(win->dm, newPageNo, 0);
    }
}

static void OnMenuViewRotateLeft(WindowInfo *win)
{
    RotateLeft(win);
}

static void OnMenuViewRotateRight(WindowInfo *win)
{
    RotateRight(win);
}

BOOL  IsCtrlLeftPressed(void)
{
    int state = GetKeyState(VK_LCONTROL);
    if (0 != state)
        return TRUE;
    return FALSE;
}

BOOL  IsCtrlRightPressed(void)
{
    int state = GetKeyState(VK_RCONTROL);
    if (0 != state)
        return TRUE;
    return FALSE;
}

BOOL  IsCtrlPressed(void)
{
    if (IsCtrlLeftPressed() || IsCtrlRightPressed())
        return TRUE;
    return FALSE;
}

static void OnKeydown(WindowInfo *win, int key, LPARAM lparam)
{
    if (VK_PRIOR == key) {
        /* TODO: more intelligence (see VK_NEXT comment). Also, probably
           it's exactly the same as 'n' so the code should be factored out */
        if (win->dm)
            DisplayModel_GoToPrevPage(win->dm, 0);
       /* SendMessage (win->hwnd, WM_VSCROLL, SB_PAGEUP, 0); */
    } else if (VK_NEXT == key) {
        /* TODO: this probably should be more intelligent (scroll if not yet at the bottom,
           go to next page if at the bottom, and something entirely different in continuous mode */
        if (win->dm)
            DisplayModel_GoToNextPage(win->dm, 0);
        /* SendMessage (win->hwnd, WM_VSCROLL, SB_PAGEDOWN, 0); */
    } else if (VK_UP == key) {
        if (win->dm)
            SendMessage (win->hwnd, WM_VSCROLL, SB_LINEUP, 0);
    } else if (VK_DOWN == key) {
        if (win->dm)
            SendMessage (win->hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
    } else if (VK_LEFT == key) {
        SendMessage (win->hwnd, WM_HSCROLL, SB_PAGEUP, 0);
    } else if (VK_RIGHT == key) {
        SendMessage (win->hwnd, WM_HSCROLL, SB_PAGEDOWN, 0);
    } else if (('g' == key) || ('G' == key)) {
        if (IsCtrlLeftPressed())
            OnMenuGoToPage(win);
    }
}

static void OnChar(WindowInfo *win, int key)
{
    if (VK_SPACE == key) {
        DisplayModel_ScrollYByAreaDy(win->dm, true, true);
    } else if (VK_BACK == key) {
        DisplayModel_ScrollYByAreaDy(win->dm, false, true);
    } else if ('g' == key) {
        OnMenuGoToPage(win);
    } else if ('k' == key) {
        SendMessage(win->hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
    } else if ('j' == key) {
        SendMessage(win->hwnd, WM_VSCROLL, SB_LINEUP, 0);
    } else if ('n' == key) {
        if (win->dm)
            DisplayModel_GoToNextPage(win->dm, 0);
    } else if ('c' == key) {
        // TODO: probably should preserve facing vs. non-facing
        if (win->dm)
            DisplayModel_SetDisplayMode(win->dm, DM_CONTINUOUS);
    } else if ('p' == key) {
        if (win->dm)
            DisplayModel_GoToPrevPage(win->dm, 0);
    } else if ('z' == key) {
        WindowInfo_ToggleZoom(win);
    } else if ('q' == key) {
        DestroyWindow(win->hwnd);
    } else if ('+' == key) {
        DisplayModel_ZoomBy(win->dm, ZOOM_IN_FACTOR);
    } else if ('-' == key) {
        DisplayModel_ZoomBy(win->dm, ZOOM_OUT_FACTOR);
    } else if ('l' == key) {
        RotateLeft(win);
    } else if ('r' == key) {
        RotateRight(win);
    }
}

static inline BOOL IsBenchArg(char *txt)
{
    if (Str_EqNoCase(txt, BENCH_ARG_TXT))
        return TRUE;
    return FALSE;
}

static BOOL IsBenchMode(void)
{
    if (NULL != gBenchFileName)
        return TRUE;
    return FALSE;
}

/* Find a file in a file history list that has a given 'menuId'.
   Return a copy of filename or NULL if couldn't be found.
   It's used to figure out if a menu item selected by the user
   is one of the "recent files" menu items in File menu.
   Caller needs to free() the memory.
   */
static const char *RecentFileNameFromMenuItemId(UINT  menuId)
{
    FileHistoryList *   curr;

    DBG_OUT("RecentFileNameFromMenuItemId() looking for %d\n", (int)menuId);
    curr = gFileHistoryRoot;
    while (curr) {
        DBG_OUT("  id=%d for '%s'\n", (int)curr->menuId, curr->state.filePath);
        if (curr->menuId == menuId)
            return Str_Dup(curr->state.filePath);
        curr = curr->next;
    }
    return NULL;
}

#define FRAMES_PER_SECS 60
#define ANIM_FREQ_IN_MS  1000 / FRAMES_PER_SECS

void OnMenuAbout(WindowInfo *win)
{
    if (WS_ABOUT_ANIM != win->state) {
        win->prevState = win->state;
        win->state = WS_ABOUT_ANIM;
        AnimState_AnimStart(&(win->animState), win->hwnd, ANIM_FREQ_IN_MS);
    } else {
        AnimState_AnimStop(&(win->animState));
        win->state = win->prevState;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int             wmId, wmEvent;
    WindowInfo *    win;
    static int      iDeltaPerLine, iAccumDelta;      // for mouse wheel logic
    ULONG           ulScrollLines;                   // for mouse wheel logic
    const char *         fileName;

    win = WindowInfo_FindByHwnd(hwnd);

    switch (message)
    {
        case WM_CREATE:
            // do nothing
            goto InitMouseWheelInfo;

        case WM_COMMAND:
            wmId    = LOWORD(wParam);
            wmEvent = HIWORD(wParam);

            fileName = RecentFileNameFromMenuItemId(wmId);
            if (fileName) {
                LoadPdf(fileName, TRUE);
                free((void*)fileName);
                break;
            }

            switch (wmId)
            {
                case IDM_OPEN:
                    OnMenuOpen(win);
                    break;

                case IDM_CLOSE:
                    CloseWindow(win, FALSE);
                    break;

                case IDM_EXIT:
                    OnMenuExit();
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
                case IDM_ZOOM_ACTUAL_SIZE:
                    OnMenuZoom(win, (UINT)wmId);
                    break;

                case IDM_ZOOM_FIT_VISIBLE:
                    /* TODO: implement me */
                    break;

                case IDM_VIEW_SINGLE_PAGE:
                    OnMenuViewSinglePage(win);
                    break;

                case IDM_VIEW_FACING:
                    OnMenuViewFacing(win);
                    break;

                case IDM_VIEW_CONTINUOUS:
                    OnMenuViewContinuous(win);
                    break;

                case IDM_GOTO_NEXT_PAGE:
                    OnMenuGoToNextPage(win);
                    break;

                case IDM_GOTO_PREV_PAGE:
                    OnMenuGoToPrevPage(win);
                    break;

                case IDM_GOTO_FIRST_PAGE:
                    OnMenuGoToFirstPage(win);
                    break;

                case IDM_GOTO_LAST_PAGE:
                    OnMenuGoToLastPage(win);
                    break;

                case IDM_GOTO_PAGE:
                    OnMenuGoToPage(win);
                    break;

                case IDM_VIEW_CONTINUOUS_FACING:
                    OnMenuViewContinuousFacing(win);
                    break;

                case IDM_VIEW_ROTATE_LEFT:
                    OnMenuViewRotateLeft(win);
                    break;

                case IDM_VIEW_ROTATE_RIGHT:
                    OnMenuViewRotateRight(win);
                    break;

#if 0
                case IDM_ABOUT:
                    assert(win);
                    if (win)
                        OnMenuAbout(win);
                    break;
#endif
                default:
                    return DefWindowProc(hwnd, message, wParam, lParam);
            }
            break;

        case WM_VSCROLL:
            OnVScroll(win, wParam);
            return WM_VSCROLL_HANDLED;

        case WM_HSCROLL:
            OnHScroll(win, wParam);
            return WM_HSCROLL_HANDLED;

        case WM_SETTINGCHANGE:
InitMouseWheelInfo:
            SystemParametersInfo (SPI_GETWHEELSCROLLLINES, 0, &ulScrollLines, 0);
            // ulScrollLines usually equals 3 or 0 (for no scrolling)
            // WHEEL_DELTA equals 120, so iDeltaPerLine will be 40
            if (ulScrollLines)
                iDeltaPerLine = WHEEL_DELTA / ulScrollLines;
            else
                iDeltaPerLine = 0;
            return 0;

        case WM_MOUSEWHEEL:
            if (!win || !win->dm) /* TODO: check for pdfDoc as well ? */
                break;

            if (iDeltaPerLine == 0)
               break;

            iAccumDelta += (short) HIWORD (wParam);     // 120 or -120

            while (iAccumDelta >= iDeltaPerLine)
            {
               SendMessage (hwnd, WM_VSCROLL, SB_LINEUP, 0);
               iAccumDelta -= iDeltaPerLine;
            }

            while (iAccumDelta <= -iDeltaPerLine)
            {
               SendMessage (hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
               iAccumDelta += iDeltaPerLine;
            }
            return 0;

        case WM_ERASEBKGND:
            // do nothing, helps to avoid flicker
            return TRUE;

        case WM_PAINT:
            /* it might happen that we get WM_PAINT after destroying a window */
            if (win)
                OnPaint(win);
            break;

        case WM_CHAR:
            if (win)
                OnChar(win, wParam);
            break;

        case WM_TIMER:
            assert(win);
            if (win)
                AnimState_NextFrame(&win->animState);

        case WM_KEYDOWN:
            if (win)
                OnKeydown(win, wParam, lParam);
            break;

        case WM_MOUSEMOVE:
            if (win)
                OnMouseMove(win, LOWORD(lParam), HIWORD(lParam), wParam);
            break;

        case WM_LBUTTONDOWN:
            if (win)
                OnMouseLeftButtonDown(win, LOWORD(lParam), HIWORD(lParam));
            break;
        case WM_LBUTTONUP:
            if (win)
                OnMouseLeftButtonUp(win, LOWORD(lParam), HIWORD(lParam));
            break;

        case WM_DROPFILES:
            if (win)
                OnDropFiles(win, (HDROP)wParam);
            break;

        case WM_DESTROY:
            /* WM_DESTROY might be sent as a result of File\Close, in which case CloseWindow() has already been called */
            if (win)
                CloseWindow(win, TRUE);
            break;

        case IDM_VIEW_WITH_ACROBAT:
            if (win)
                ViewWithAcrobat(win);
            break;

        case MSG_BENCH_NEXT_ACTION:
            if (win)
                OnBenchNextAction(win);
            break;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

BOOL RegisterWinClass(HINSTANCE hInstance)
{
    WNDCLASSEX  wcex;
    ATOM        atom;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SUMATRAPDF));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = NULL;
    wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_SUMATRAPDF);
    wcex.lpszClassName  = WIN_CLASS_NAME;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    atom = RegisterClassEx(&wcex);
    if (atom)
        return TRUE;
    return FALSE;
}

BOOL InstanceInit(HINSTANCE hInstance, int nCmdShow)
{
    ghinst = hInstance;

    globalParams = new GlobalParams("");
    if (!globalParams)
        return FALSE;

    ColorsInit();
    gCursorArrow = LoadCursor(NULL, IDC_ARROW);
    gCursorWait  = LoadCursor(NULL, IDC_WAIT);

    gBrushBg     = CreateSolidBrush(COL_WINDOW_BG);
    gBrushShadow = CreateSolidBrush(COL_WINDOW_SHADOW);
    gBrushLinkDebug = CreateSolidBrush(RGB(0x00,0x00,0xff));
    return TRUE;
}

void StrList_Reverse(StrList **strListRoot)
{
    StrList *newRoot = NULL;
    StrList *cur, *next;
    if (!strListRoot)
        return;
    cur = *strListRoot;
    while (cur) {
        next = cur->next;
        cur->next = newRoot;
        newRoot = cur;
        cur = next;
    }
    *strListRoot = newRoot;
}

BOOL StrList_InsertAndOwn(StrList **root, char *txt)
{
    StrList *   el;
    assert(root && txt);
    if (!root || !txt)
        return FALSE;

    el = (StrList*)malloc(sizeof(StrList));
    if (!el)
        return FALSE;
    el->str = txt;
    el->next = *root;
    *root = el;
    return TRUE;
}

void StrList_Destroy(StrList **root)
{
    StrList *   cur;
    StrList *   next;

    if (!root)
        return;
    cur = *root;
    while (cur) {
        next = cur->next;
        free((void*)cur->str);
        free((void*)cur);
        cur = next;
    }
    *root = NULL;
}

StrList *StrList_FromCmdLine(char *cmdLine)
{
    char *     exePath;
    StrList *   strList = NULL;
    char *      txt;

    assert(cmdLine);

    if (!cmdLine)
        return NULL;

    exePath = ExePathGet();
    if (!exePath)
        return NULL;
    if (!StrList_InsertAndOwn(&strList, exePath)) {
        free((void*)exePath);
        return NULL;
    }

    for (;;) {
        txt = Str_ParsePossiblyQuoted(&cmdLine);
        if (!txt)
            break;
        if (!StrList_InsertAndOwn(&strList, txt)) {
            free((void*)txt);
            break;
        }
    }
    StrList_Reverse(&strList);
    return strList;
}

void u_DoAllTests(void)
{
#ifdef DEBUG
    printf("Running tests\n");
    u_SimpleRect_Intersect();
#else
    printf("Not running tests\n");
#endif
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
    StrList *           argListRoot;
    StrList *           currArg;
    char *              exeName;
    char *              benchPageNumStr = NULL;
    MSG                 msg;
    HACCEL              hAccelTable;
    WindowInfo*         win;
    FileHistoryList *   currFile;
    int                 pdfOpened = 0;

    UNREFERENCED_PARAMETER(hPrevInstance);

    u_DoAllTests();

    argListRoot = StrList_FromCmdLine(lpCmdLine);
    assert(argListRoot);
    if (!argListRoot)
        return 0;
    exeName = argListRoot->str;

    Prefs_Load();
    /* parse argument list. If BENCH_ARG_TXT was given, then we're in benchmarking mode. Otherwise
    we assume that all arguments are PDF file names.
    BENCH_ARG_TXT can be followed by file or directory name. If file, it can additionally be followed by
    a number which we interpret as page number */
    currArg = argListRoot->next;
    while (currArg) {
        if (IsBenchArg(currArg->str)) {
            currArg = currArg->next;
            if (currArg) {
                gBenchFileName = currArg->str;
                if (currArg->next)
                    benchPageNumStr = currArg->next->str;
            }
            break;
        }
        currArg = currArg->next;
    }

    if (benchPageNumStr) {
        gBenchPageNum = atoi(benchPageNumStr);
        if (gBenchPageNum < 1)
            gBenchPageNum = INVALID_PAGE_NUM;
    }

    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    if (!RegisterWinClass(hInstance))
        goto Exit;

#if 0
    WinFontList_Create();
    FontMappingList_Dump(&gFontMapMSList);
#endif

    CaptionPens_Create();
    if (!InstanceInit(hInstance, nCmdShow))
        goto Exit;

    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_SUMATRAPDF));

    /* TODO: detect it's not me and show a dialog box ? */
    AssociatePdfWithExe(exeName);

    if (NULL != gBenchFileName) {
            win = LoadPdf(gBenchFileName, FALSE);
            if (win)
                ++pdfOpened;
    } else {
        currArg = argListRoot->next;
        while (currArg) {
            win = LoadPdf(currArg->str, FALSE);
            if (!win)
                goto Exit;
           ++pdfOpened;
            currArg = currArg->next;
        }
    }

    if (0 == pdfOpened) {
        /* disable benchmark mode if we couldn't open file to benchmark */
        gBenchFileName = 0;
        currFile = gFileHistoryRoot;
        while (currFile) {
            if (currFile->state.visible) {
                win = LoadPdf(currFile->state.filePath, TRUE, FALSE);
                if (!win)
                    goto Exit;
                ++pdfOpened;
            }
            currFile = currFile->next;
        }
        if (0 == pdfOpened) {
            win = WindowInfo_CreateEmpty();
            if (!win)
                goto Exit;
            WindowInfoList_Add(win);

            /* TODO: should this be part of WindowInfo_CreateEmpty() ? */
            DragAcceptFiles(win->hwnd, TRUE);
            ShowWindow(win->hwnd, SW_SHOW);
            UpdateWindow(win->hwnd);
        }
    }

    if (IsBenchMode()) {
        assert(win);
        assert(pdfOpened > 0);
        if (win)
            PostBenchNextAction(win->hwnd);
    }

    if (0 == pdfOpened)
        MenuUpdateStateForAllWindows();
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

Exit:
    WindowInfoList_DeleteAll();
    FileHistoryList_Free(&gFileHistoryRoot);
    CaptionPens_Destroy();
    DeleteObject(gBrushBg);
    DeleteObject(gBrushShadow);
    DeleteObject(gBrushLinkDebug);

    delete globalParams;
    StrList_Destroy(&argListRoot);
    return (int) msg.wParam;
}
