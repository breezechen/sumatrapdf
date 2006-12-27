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
#include "FileHistory.h"
#include "AppPrefs.h"

#include "SimpleRect.h"
#include "DisplayModelSplash.h"
#include "BaseUtils.h"
#include <windowsx.h>

//#define FANCY_UI 1

class WinRenderedBitmap : public PlatformCachedBitmap {
public:
    WinRenderedBitmap(SplashBitmap *b) : mBitmap(b) {}
    virtual ~WinRenderedBitmap() {
        delete mBitmap;
    }
    SplashBitmap *bitmap(void) { return mBitmap; }

protected:
    SplashBitmap *mBitmap;
};

/* Define if you want to conserve memory by always freeing cached bitmaps
   for pages not visible. Only enable for stress-testing the logic. On
   desktop machine we usually have plenty memory */
//#define CONSERVE_MEMORY 1

/* Next action for the benchmark mode */
#define MSG_BENCH_NEXT_ACTION WM_USER + 1

#define ZOOM_IN_FACTOR      1.2
#define ZOOM_OUT_FACTOR     1.0 / ZOOM_IN_FACTOR

/* if TRUE, we're in debug mode where we show links as blue rectangle on
   the screen. Makes debugging code related to links easier.
   TODO: make a menu item in DEBUG build to turn it on/off. */
#ifdef DEBUG
static BOOL             gDebugShowLinks = TRUE;
#else
static BOOL             gDebugShowLinks = FALSE;
#endif

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

#define WM_APP_MSG_REFRESH (WM_APP + 2)

/* A caption is 4 white/blue 2 pixel line and a 3 pixel white line */
#define CAPTION_DY 2*(2*4)+3

#define COL_CAPTION_BLUE RGB(0,0x50,0xa0)
#define COL_WHITE RGB(0xff,0xff,0xff)
#define COL_BLACK RGB(0,0,0)
#define COL_WINDOW_BG RGB(0xcc, 0xcc, 0xcc)
//#define COL_WINDOW_BG RGB(0xff, 0xff, 0xff)
#define COL_WINDOW_SHADOW RGB(0x40, 0x40, 0x40)

#define FRAME_CLASS_NAME   _T("SUMATRA_PDF_FRAME")
#define CANVAS_CLASS_NAME  _T("SUMATRA_PDF_CANVAS")
#define APP_NAME        _T("SumatraPDF")
#define PDF_DOC_NAME    _T("Adobe PDF Document")

#define PREFS_FILE_NAME _T("prefs.txt")
#define APP_SUB_DIR     _T("SumatraPDF")

#define BENCH_ARG_TXT   "-bench"

/* Default size for the window, happens to be american A4 size (I think) */
#define DEF_WIN_DX 612
#define DEF_WIN_DY 792

#define REPAINT_TIMER_ID    1
#define REPAINT_DELAY_IN_MS 400

/* A special "pointer" vlaue indicating that we tried to render this bitmap
   but couldn't (e.g. due to lack of memory) */
#define BITMAP_CANNOT_RENDER (SplashBitmap*)NULL

#define WS_TOOLBAR (WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | \
                    TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | TBSTYLE_ALTDRAG | \
                    TBSTYLE_LIST | CCS_NODIVIDER | CCS_NOPARENTALIGN | \
                    CCS_ADJUSTABLE)

#define WS_REBAR (WS_CHILD | WS_CLIPCHILDREN | WS_BORDER | RBS_VARHEIGHT | \
                  RBS_BANDBORDERS | CCS_NODIVIDER | CCS_NOPARENTALIGN)

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
static HBRUSH                       gBrushWhite;
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
BOOL                                gShowToolbar = TRUE;
#ifdef DOUBLE_BUFFER
static BOOL                         gUseDoubleBuffer = TRUE;
#else
static BOOL                         gUseDoubleBuffer = FALSE;
#endif

#define MAX_PAGE_REQUESTS 8
static PageRenderRequest            gPageRenderRequests[MAX_PAGE_REQUESTS];
static int                          gPageRenderRequestsCount = 0;

static HANDLE                       gPageRenderThreadHandle = NULL;
static HANDLE                       gPageRenderSem = NULL;
static PageRenderRequest *          gCurPageRenderReq = NULL;

static int                          gReBarDy;
static int                          gReBarDyFrame;

typedef struct ToolbarButtonInfo {
    /* information provided at compile time */
    int         bitmapResourceId;
    int         cmdId;
    TCHAR *     toolTip;

    /* information calculated at runtime */
    int         index;
} ToolbarButtonInfo;

#define IDB_SEPARATOR  -1

ToolbarButtonInfo gToolbarButtons[] = {
    { IDB_SILK_OPEN,     IDM_OPEN, _T("Open"), 0 },
    { IDB_SEPARATOR,     IDB_SEPARATOR, 0, 0 },
    { IDB_SILK_PREV,     IDM_GOTO_PREV_PAGE, _T("Previous page"), 0 },
    { IDB_SILK_NEXT,     IDM_GOTO_NEXT_PAGE, _T("Next page"), 0 },
    { IDB_SEPARATOR,     IDB_SEPARATOR, 0, 0 },
    { IDB_SILK_ZOOM_IN,  IDT_VIEW_ZOOMIN, _T("Zoom in"), 0 },
    { IDB_SILK_ZOOM_OUT, IDT_VIEW_ZOOMOUT, _T("Zoom out"), 0 }
};

#define TOOLBAR_BUTTONS_COUNT dimof(gToolbarButtons)

void WindowInfo_ResizeToPage(WindowInfo *win, int pageNo);
static void CreateToolbar(WindowInfo *win, HINSTANCE hInst);

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

static BOOL pageRenderAbortCb(void *data)
{
    PageRenderRequest *req = (PageRenderRequest*)data;
    if (req->abort) {
        DBG_OUT("Rendering of page %d aborted\n", req->pageNo);
        return TRUE;
    }
    else
        return FALSE;
}

void RenderQueue_RemoveForDisplayModel(DisplayModelSplash *dm) {
    int                 i = 0;
    int                 curPos = 0;
    int                 reqCount;
    BOOL                shouldRemove;
    PageRenderRequest * req = NULL;

    LockCache();
    reqCount = gPageRenderRequestsCount;
    while (i < reqCount) {
        req = &(gPageRenderRequests[i]);
        shouldRemove = (req->dm == dm);
        if (i != curPos)
            gPageRenderRequests[curPos] = gPageRenderRequests[i];
        if (shouldRemove)
            --gPageRenderRequestsCount;
        else
            ++curPos;
        ++i;
    }
    UnlockCache();
}

/* Wait until rendering of a page beloging to <dm> has finished. */
/* TODO: this might take some time, would be good to show a dialog to let the
   user know he has to wait until we finish */
void CancelRenderingForDisplayModel(DisplayModelSplash *dm)
{
    BOOL renderingFinished = FALSE;

    DBG_OUT("CancelRenderingForDisplayModel()\n");
    for (;;) {
        LockCache();
        if (!gCurPageRenderReq || (gCurPageRenderReq->dm != dm))
            renderingFinished = TRUE;
        else
            gCurPageRenderReq->abort = TRUE;
        UnlockCache();
        if (renderingFinished)
            break;
        /* TODO: busy loop is not good, but I don't have a better idea */
        SleepMilliseconds(500);
    }
}

/* Render a bitmap for page <pageNo> in <dm>. */
void RenderQueue_Add(DisplayModelSplash *dm, int pageNo) {
    PageRenderRequest *   newRequest = NULL;
    PageRenderRequest *   req = NULL;
    PdfPageInfo *         pageInfo;
    LONG                  prevCount;
    int                   rotation;
    double                zoomLevel;

    DBG_OUT("RenderQueue_Add(pageNo=%d)\n", pageNo);
    assert(dm);
    if (!dm) goto Exit;

    LockCache();
    pageInfo = dm->GetPageInfo(pageNo);
    rotation = dm->rotation();
    NormalizeRotation(&rotation);
    zoomLevel = dm->zoomReal;

    if (BitmapCache_Exists(dm, pageNo, zoomLevel, rotation)) {
        goto LeaveCsAndExit;
    }

    if (gCurPageRenderReq && 
        (gCurPageRenderReq->pageNo == pageNo) && (gCurPageRenderReq->dm == dm)) {
        if ((gCurPageRenderReq->zoomLevel != zoomLevel) || (gCurPageRenderReq->rotation != rotation)) {
            /* Currently rendered page is for the same page but with different zoom
            or rotation, so abort it */
            DBG_OUT("  aborting rendering\n");
            gCurPageRenderReq->abort = TRUE;
        } else {
            /* we're already rendering exactly the same page */
            DBG_OUT("  already rendering this page\n");
            goto LeaveCsAndExit;
        }
    }

    for (int i=0; i  < gPageRenderRequestsCount; i++) {
        req = &(gPageRenderRequests[i]);
        if ((req->pageNo == pageNo) && (req->dm == dm)) {
            if ((req->zoomLevel == zoomLevel) && (req->rotation == rotation)) {
                /* Request with exactly the same parameters already queued for
                   rendering. Move it to the top of the queue so that it'll
                   be rendered faster. */
                PageRenderRequest tmp;
                tmp = gPageRenderRequests[gPageRenderRequestsCount-1];
                gPageRenderRequests[gPageRenderRequestsCount-1] = *req;
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

    /* add request to the queue */
    if (gPageRenderRequestsCount == MAX_PAGE_REQUESTS) {
        /* queue is full -> remove the oldest items on the queue */
        memmove(&(gPageRenderRequests[0]), &(gPageRenderRequests[1]), sizeof(PageRenderRequest)*(MAX_PAGE_REQUESTS-1));
        newRequest = &(gPageRenderRequests[MAX_PAGE_REQUESTS-1]);
    } else {
        newRequest = &(gPageRenderRequests[gPageRenderRequestsCount]);
        gPageRenderRequestsCount++;
    }
    assert(gPageRenderRequestsCount <= MAX_PAGE_REQUESTS);
    newRequest->dm = dm;
    newRequest->pageNo = pageNo;
    newRequest->zoomLevel = zoomLevel;
    newRequest->rotation = rotation;
    newRequest->abort = FALSE;

    UnlockCache();
    /* tell rendering thread there's a new request to render */
    ReleaseSemaphore(gPageRenderSem, 1, &prevCount);
Exit:
    return;
LeaveCsAndExit:
    UnlockCache();
    return;
}

void RenderQueue_Pop(PageRenderRequest *req)
{
    LockCache();
    assert(gPageRenderRequestsCount > 0);
    assert(gPageRenderRequestsCount <= MAX_PAGE_REQUESTS);
    --gPageRenderRequestsCount;
    *req = gPageRenderRequests[gPageRenderRequestsCount];
    assert(gPageRenderRequestsCount >= 0);
    UnlockCache();
}

/* TODO: move to BaseUtils.h */
static const char *Path_GetBaseName(const char *path)
{
    const char *fileBaseName = (const char*)strrchr(path, DIR_SEP_CHAR);
    if (NULL == fileBaseName)
        fileBaseName = path;
    else
        ++fileBaseName;
    return fileBaseName;
}

static HMENU FindMenuItem(WindowInfo *win, UINT id)
{
    HMENU   menuMain;
    HMENU   subMenu;

    UINT    thisId;
    int     i, j;

    menuMain = GetMenu(win->hwndFrame);

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

static HMENU GetFileMenu(HWND hwnd)
{
    return GetSubMenu(GetMenu(hwnd), 0);
}

static void SwitchToDisplayMode(WindowInfo *win, DisplayMode displayMode)
{
    HMENU   menuMain;
    UINT    id;
    
    menuMain = GetMenu(win->hwndFrame);
    CheckMenuItem(menuMain, IDM_VIEW_SINGLE_PAGE, MF_BYCOMMAND | MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VIEW_CONTINUOUS, MF_BYCOMMAND | MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VIEW_FACING, MF_BYCOMMAND | MF_UNCHECKED);
    CheckMenuItem(menuMain, IDM_VIEW_CONTINUOUS_FACING, MF_BYCOMMAND | MF_UNCHECKED);

    win->dm->SetDisplayMode(displayMode);
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

static UINT AllocNewMenuId(void)
{
    static UINT firstId = 1000;
    ++firstId;
    return firstId;
}

static void AddMenuSepToFilesMenu(WindowInfo *win)
{
    HMENU               menuFile;
    menuFile = GetFileMenu(win->hwndFrame);
    AppendMenu(menuFile, MF_SEPARATOR, 0, NULL);
}

static void AddMenuItemToFilesMenu(WindowInfo *win, FileHistoryList *node)
{
    HMENU               menuFile;
    UINT                newId;
    const char *        txt;

    assert(node);
    if (!node)
        return;
    menuFile = GetFileMenu(win->hwndFrame);
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

static void AddRecentFilesToMenu(WindowInfo *win)
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

static void WinEditSetSel(HWND hwnd, DWORD selStart, DWORD selEnd)
{
   ::SendMessage(hwnd, EM_SETSEL, (WPARAM)selStart, (WPARAM)selEnd);
}

void WinEditSelectAll(HWND hwnd)
{
    WinEditSetSel(hwnd, 0, -1);
}

static int WinGetTextLen(HWND hwnd)
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

static void Win32_GetScrollbarSize(int *scrollbarYDxOut, int *scrollbarXDyOut)
{
    if (scrollbarYDxOut)
        *scrollbarYDxOut = GetSystemMetrics(SM_CXHSCROLL);
    if (scrollbarXDyOut)
        *scrollbarXDyOut = GetSystemMetrics(SM_CYHSCROLL);
}

/* Set the client area size of the window 'hwnd' to 'dx'/'dy'. */
static void WinResizeClientArea(HWND hwnd, int dx, int dy)
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

static void AppGetAppDir(DString* pDs)
{
    char        dir[MAX_PATH];

    SHGetFolderPath(NULL, CSIDL_APPDATA|CSIDL_FLAG_CREATE, NULL, 0, dir);
    DStringSprintf(pDs, "%s/%s", dir, APP_SUB_DIR);
    _mkdir(pDs->pString);
}

/* Generate the full path for a filename used by the app in the userdata path. */
static void AppGenDataFilename(char* pFilename, DString* pDs)
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
static void Prefs_Load(void)
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

static void Win32_Win_GetSize(HWND hwnd, int *dxOut, int *dyOut)
{
    RECT    r;
    *dxOut = 0;
    *dyOut = 0;

    if (GetWindowRect(hwnd, &r)) {
        *dxOut = (r.right - r.left);
        *dyOut = (r.bottom - r.top);
    }
}

static void Win32_Win_SetSize(HWND hwnd, int dx, int dy)
{
    SetWindowPos(hwnd, NULL, 0, 0, dx, dy, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_DRAWFRAME);
}
static void Win32_Win_GetPos(HWND hwnd, int *xOut, int *yOut)
{
    RECT    r;
    *xOut = 0;
    *yOut = 0;

    if (GetWindowRect(hwnd, &r)) {
        *xOut = r.left;
        *yOut = r.top;
    }
}

static void Win32_Win_SetPos(HWND hwnd, int x, int y)
{
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE);
}

static void UpdateDisplayStateWindowPos(WindowInfo *win, DisplayState *ds)
{
    int posX, posY;

    Win32_Win_GetPos(win->hwndCanvas, &posX, &posY);

    ds->windowX = posX;
    ds->windowY = posY;
}

static void UpdateCurrentFileDisplayStateForWin(WindowInfo *win)
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

static void UpdateCurrentFileDisplayState(void)
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

static void Prefs_Save(void)
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
    GetClientRect(win->hwndCanvas, &rc);
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

    win->hdc = GetDC(win->hwndCanvas);
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
    LockCache();
    if (gCurPageRenderReq && (gCurPageRenderReq->dm == win->dm)) {
        /* TODO: should somehow wait for for the page to finish rendering */
        gCurPageRenderReq->abort = TRUE;
    }
    UnlockCache();
    delete win->dm;
    WindowInfo_Dib_Deinit(win);
    WindowInfo_DoubleBuffer_Delete(win);
    free((void*)win);
}

static WindowInfo* WindowInfo_FindByHwnd(HWND hwnd)
{
    WindowInfo  *win = gWindowList;
    while (win) {
        if (hwnd == win->hwndFrame)
            return win;
        if (hwnd == win->hwndCanvas)
            return win;
        if (hwnd == win->hwndReBar)
            return win;
        win = win->next;
    }
    return NULL;
}

static WindowInfo *WindowInfo_New(HWND hwndFrame)
{
    WindowInfo  *win;

    win = WindowInfo_FindByHwnd(hwndFrame);
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
    win->hwndFrame = hwndFrame;
    win->dragging = FALSE;
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

static void WindowInfo_RedrawAll(WindowInfo *win, BOOL update=FALSE)
{
    InvalidateRect(win->hwndCanvas, NULL, FALSE);
    if (update)
        UpdateWindow(win->hwndCanvas);
}

static BOOL FileCloseMenuEnabled(void)
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

static void ToolbarUpdateStateForWindow(WindowInfo *win)
{
    int     cmdId;
    LPARAM  enable = (LPARAM)MAKELONG(1,0);
    LPARAM  disable = (LPARAM)MAKELONG(0,0);
    LPARAM buttonState;

    for (int i=0; i < TOOLBAR_BUTTONS_COUNT; i++) {
        cmdId = gToolbarButtons[i].cmdId;
        if (IDB_SEPARATOR == cmdId)
            continue;
        buttonState = enable;
        if (IDM_OPEN != cmdId) {
            if (WS_SHOWING_PDF != win->state)
                buttonState = disable;
        }
        SendMessage(win->hwndToolbar, TB_ENABLEBUTTON, cmdId, buttonState);
    }
}

static void MenuUpdateShowToolbarStateForWindow(WindowInfo *win)
{
    HMENU     hmenu;
    hmenu = GetMenu(win->hwndFrame);

    if (gShowToolbar)
        CheckMenuItem(hmenu, IDM_VIEW_SHOW_HIDE_TOOLBAR, MF_BYCOMMAND | MF_CHECKED);
    else
        CheckMenuItem(hmenu, IDM_VIEW_SHOW_HIDE_TOOLBAR, MF_BYCOMMAND | MF_UNCHECKED);
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

    hmenu = GetMenu(win->hwndFrame);
    if (fileCloseEnabled)
        EnableMenuItem(hmenu, IDM_CLOSE, MF_BYCOMMAND | MF_ENABLED);
    else
        EnableMenuItem(hmenu, IDM_CLOSE, MF_BYCOMMAND | MF_GRAYED);

    MenuUpdateShowToolbarStateForWindow(win);
        
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
        ShowScrollBar(win->hwndCanvas, SB_BOTH, TRUE);
    else {
        ShowScrollBar(win->hwndCanvas, SB_BOTH, FALSE);
        WinSetText(win->hwndFrame, APP_NAME);
    }
}

/* Disable/enable menu items and toolbar buttons depending on wheter a
   given window shows a PDF file or not. */
static void MenuToolbarUpdateStateForAllWindows(void)
{
    WindowInfo *    win = gWindowList;

    while (win) {
        MenuUpdateStateForWindow(win);
        ToolbarUpdateStateForWindow(win);
        win = win->next;
    }
}

static WindowInfo* WindowInfo_CreateEmpty(void)
{
    HWND        hwndFrame, hwndCanvas;
    WindowInfo* win;

#if FANCY_UI
    hwndFrame = CreateWindowEx(
//            WS_EX_TOOLWINDOW,
        0,
//            WS_OVERLAPPEDWINDOW,
//            WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE,
        //WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_HSCROLL | WS_VSCROLL,
        FRAME_CLASS_NAME, szTitle,
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DEF_WIN_DX, DEF_WIN_DY,
        NULL, NULL,
        ghinst, NULL);
#else
    hwndFrame = CreateWindow(
            FRAME_CLASS_NAME, szTitle,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            DEF_WIN_DX, DEF_WIN_DY,
            NULL, NULL,
            ghinst, NULL);

#endif

    if (!hwndFrame)
        return NULL;

    win = WindowInfo_New(hwndFrame);
    hwndCanvas = CreateWindow(
            CANVAS_CLASS_NAME, NULL,
            WS_CHILD | WS_HSCROLL | WS_VSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT,
            DEF_WIN_DX, DEF_WIN_DY,
            hwndFrame, NULL,
            ghinst, NULL);
    if (!hwndCanvas)
        return NULL;
    win->hwndCanvas = hwndCanvas;
    CreateToolbar(win, ghinst);
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
        WinResizeClientArea(win->hwndCanvas, fileHistory->state.windowDx, fileHistory->state.windowDy);
        totalDrawAreaSize.dx = (double)fileHistory->state.windowDx;
        totalDrawAreaSize.dy = (double)fileHistory->state.windowDy;
        /* TODO: make sure it doesn't have a stupid position like 
           outside of the screen etc. */
        Win32_Win_SetPos(win->hwndFrame, fileHistory->state.windowX, fileHistory->state.windowY);
    }

    /* In theory I should get scrollbars sizes using Win32_GetScrollbarSize(&scrollbarYDx, &scrollbarXDy);
       but scrollbars are not part of the client area on windows so it's better
       not to have them taken into account by DisplayModelSplash code.
       TODO: I think it's broken anyway and DisplayModelSplash needs to know if
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
    win->dm = DisplayModelSplash_CreateFromPdfDoc(pdfDoc, outputDev, totalDrawAreaSize,
        scrollbarYDx, scrollbarXDy, displayMode, startPage);
    if (!win->dm) {
        delete outputDev;
        WindowInfo_Delete(win);
        return NULL;
    }

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
        win->dm->Relayout(zoomVirtual, rotation);
        if (!win->dm->ValidPageNo(startPage))
            startPage = 1;
        /* TODO: need to calculate proper offsetY, currently giving large offsetY
           remembered for continuous mode breaks things (makes all pages invisible) */
        offsetY = 0;
        /* TODO: make sure offsetX isn't bogus */
        win->dm->GoToPage(startPage, offsetY);
        win->dm->ScrollXTo(offsetX);
        /* only resize the window if it's a newly opened window */
        if (!reuseExistingWindow && !fromHistory)
            WindowInfo_ResizeToPage(win, startPage);
    }
    if (reuseExistingWindow)
        WindowInfo_RedrawAll(win);

Exit:
    MenuToolbarUpdateStateForAllWindows();
    if (win) {
        DragAcceptFiles(win->hwndFrame, TRUE);
        DragAcceptFiles(win->hwndCanvas, TRUE);
        ShowWindow(win->hwndCanvas, SW_SHOW);
        UpdateWindow(win->hwndCanvas);
        ShowWindow(win->hwndFrame, SW_SHOW);
        UpdateWindow(win->hwndFrame);
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

void DisplayModelSplash::PageChanged(void)
{
    char            titleBuf[256];
    const char *    baseName;
    int             pageCount;
    HRESULT         hr;
    WindowInfo *    win;

    win = (WindowInfo*)appData;
    assert(win);
    if (!win) return;

    if (!win->dm->pdfDoc)
        return;

    int currPageNo = GetCurrentPageNo();
    pageCount = win->dm->pdfDoc->getNumPages();
    baseName = Path_GetBaseName(win->dm->pdfDoc->getFileName()->getCString());
    if (pageCount <= 0)
        WinSetText(win->hwndFrame, baseName);
    else {
        hr = StringCchPrintfA(titleBuf, dimof(titleBuf), "%s page %d of %d", baseName, currPageNo, pageCount);
        WinSetText(win->hwndFrame, titleBuf);
    }
}

void DisplayModelSplash::RepaintDisplay(bool delayed)
{
    WindowInfo *win;

    win = (WindowInfo*)appData;
    assert(win);
    if (!win) return;

    WindowInfo_RedrawAll(win);
}

void DisplayModelSplash::SetScrollbarsState(void)
{
    WindowInfo *    win;
    SCROLLINFO      si = {0};
    int             canvasDx, canvasDy;
    int             drawAreaDx, drawAreaDy;
    int             offsetX, offsetY;

    win = (WindowInfo*)this->appData;
    assert(win);
    if (!win) return;

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;

    canvasDx = (int)canvasSize.dx;
    canvasDy = (int)canvasSize.dy;
    drawAreaDx = (int)drawAreaSize.dx;
    drawAreaDy = (int)drawAreaSize.dy;
    offsetX = (int)areaOffset.x;
    offsetY = (int)areaOffset.y;

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
    SetScrollInfo(win->hwndCanvas, SB_HORZ, &si, TRUE);

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
    SetScrollInfo(win->hwndCanvas, SB_VERT, &si, TRUE);
}

static void WindowInfo_ResizeToWindow(WindowInfo *win)
{
    DisplayModelSplash *dm;
    RectDSize     totalDrawAreaSize;

    assert(win);
    if (!win) return;

    dm = win->dm;
    assert(dm);
    if (!dm) return;

    WindowInfo_GetWindowSize(win);

    totalDrawAreaSize.dx = (double)win->winDx;
    totalDrawAreaSize.dy = (double)win->winDy;
    dm->SetTotalDrawAreaSize(totalDrawAreaSize);
}

static void WindowInfo_ResizeToPage(WindowInfo *win, int pageNo)
{
    int                 dx, dy;
    int                 displayDx, displayDy;
    BOOL                fullScreen = FALSE;
    DisplaySettings *   displaySettings;
    DisplayModelSplash *dm;
    PdfPageInfo *       pageInfo;
    HDC                 hdc;

    assert(win);
    if (!win) return;
    dm = win->dm;
    assert(dm);
    if (!dm)
        return;

    /* TODO: should take current monitor into account? */
    hdc = GetDC(win->hwndCanvas);
    displayDx = GetDeviceCaps(hdc, HORZRES);
    displayDy = GetDeviceCaps(hdc, VERTRES);

    if (fullScreen) {
        /* TODO: fullscreen not yet supported */
        assert(0);
        dx = displayDx;
        dy = displayDy;
    } else {
        assert(dm->ValidPageNo(pageNo));
        if (!dm->ValidPageNo(pageNo))
            return;
        pageInfo = dm->GetPageInfo(pageNo);
        assert(pageInfo);
        if (!pageInfo)
            return;
        displaySettings = GetGlobalDisplaySettings();
        dx = pageInfo->currDx + displaySettings->paddingPageBorderLeft + displaySettings->paddingPageBorderRight;
        dy = pageInfo->currDy + displaySettings->paddingPageBorderTop + displaySettings->paddingPageBorderBottom;
        if (dx > displayDx - 10)
            dx = displayDx - 10;
        if (dy > displayDy - 10)
            dy = displayDy - 10;
    }

    WinResizeClientArea(win->hwndCanvas, dx, dy);
}

static void WindowInfo_ToggleZoom(WindowInfo *win)
{
    DisplayModelSplash *  dm;

    assert(win);
    if (!win) return;

    dm = win->dm;
    assert(dm);
    if (!dm) return;

    if (ZOOM_FIT_PAGE == dm->zoomVirtual)
        dm->SetZoomVirtual(ZOOM_FIT_WIDTH);
    else if (ZOOM_FIT_WIDTH == dm->zoomVirtual)
        dm->SetZoomVirtual(ZOOM_FIT_PAGE);
}

static BOOL WindowInfo_PdfLoaded(WindowInfo *win)
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
static char *PossiblyUnescapePath(char *txt)
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
static char *ExePathGet(void)
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
static void AssociatePdfWithExe(TCHAR *exePath)
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

static void DrawLineSimple(HDC hdc, int sx, int sy, int ex, int ey)
{
    MoveToEx(hdc, sx, sy, NULL);
    LineTo(hdc, ex, ey);
}

/* Draw caption area for a given window 'win' in the classic AmigaOS style */
static void AmigaCaptionDraw(WindowInfo *win)
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

static void WinResizeIfNeeded(WindowInfo *win)
{
    RECT    rc;
    int     win_dx, win_dy;
    GetClientRect(win->hwndCanvas, &rc);
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

static void PostBenchNextAction(HWND hwnd)
{
    PostMessage(hwnd, MSG_BENCH_NEXT_ACTION, 0, 0);
}

static void OnBenchNextAction(WindowInfo *win)
{
    if (!win->dm)
        return;

    if (win->dm->GoToNextPage(0))
        PostBenchNextAction(win->hwndFrame);
}

static void DrawCenteredText(HDC hdc, RECT *r, char *txt)
{    
    SetBkMode(hdc, TRANSPARENT);
    DrawText(hdc, txt, strlen(txt), r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void WindowInfo_Paint(WindowInfo *win, HDC hdc, PAINTSTRUCT *ps)
{
    int                   pageNo;
    PdfPageInfo*          pageInfo;
    DisplayModelSplash *  dm;
    SplashBitmap *        splashBmp;
    int                   splashBmpDx, splashBmpDy;
    SplashColorMode       splashBmpColorMode;
    SplashColorPtr        splashBmpData;
    int                   splashBmpRowSize;
    int                   xSrc, ySrc, xDest, yDest;
    int                   bmpDx, bmpDy;
    int                   linkNo;
    PdfLink *             pdfLink;
    RectI                 drawAreaRect;
    RectI                 intersect;
    RectI                 rectLink;
    RECT                  rectScreen;
    HBITMAP               hbmp = NULL;
    BITMAPINFOHEADER      bmih;
    HDC                   bmpDC = NULL;
    RECT                  bounds;
    BitmapCacheEntry *    entry;

    assert(win);
    if (!win) return;
    dm = win->dm;
    assert(dm);
    if (!dm) return;
    assert(dm->pdfDoc);
    if (!dm->pdfDoc) return;

    assert(win->hdcToDraw);
    hdc = win->hdcToDraw;

    //TODO: FillRect() ps->rcPaint - bounds
    FillRect(hdc, &(ps->rcPaint), gBrushBg);

    DBG_OUT("WindowInfo_Paint() start\n");
    for (pageNo = 1; pageNo <= dm->pageCount; ++pageNo) {
        pageInfo = dm->GetPageInfo(pageNo);
        if (!pageInfo->visible)
            continue;
        assert(pageInfo->shown);
        if (!pageInfo->shown)
            continue;

        splashBmp = NULL;
        entry = BitmapCache_Find(dm, pageNo, dm->zoomReal, dm->rotation());
        if (entry) {
            WinRenderedBitmap *renderedBmp = (WinRenderedBitmap*)entry->bitmap;
            splashBmp = renderedBmp->bitmap();
        }

        if (!splashBmp)
            DBG_OUT("   missing bitmap on visible page %d\n", pageNo);

        //TODO: FillRect() ps->rcPaint - bounds

        xSrc = (int)pageInfo->bitmapX;
        ySrc = (int)pageInfo->bitmapY;
        bmpDx = (int)pageInfo->bitmapDx;
        bmpDy = (int)pageInfo->bitmapDy;
        xDest = (int)pageInfo->screenX;
        yDest = (int)pageInfo->screenY;

        if (!entry) {
            /* TODO: assert is queued for rendering ? */
            bounds.left = xDest;
            bounds.top = yDest;
            bounds.right = xDest + bmpDx;
            bounds.bottom = yDest + bmpDy;
            FillRect(hdc, &bounds, gBrushWhite);
            DrawCenteredText(hdc, &bounds, "Please wait - rendering...");
            DBG_OUT("   drawing empty for %d\n", pageNo);
            continue;
        }

        if (BITMAP_CANNOT_RENDER == splashBmp) {
            bounds.left = xDest;
            bounds.top = yDest;
            bounds.right = xDest + bmpDx;
            bounds.bottom = yDest + bmpDy;
            FillRect(hdc, &bounds, gBrushWhite);
            DrawCenteredText(hdc, &bounds, "Couldn't render the page");
            continue;
        }

        DBG_OUT("   drawing bitmap for %d\n", pageNo);
        splashBmpDx = splashBmp->getWidth();
        splashBmpDy = splashBmp->getHeight();
        splashBmpRowSize = splashBmp->getRowSize();
        splashBmpData = splashBmp->getDataPtr();
        splashBmpColorMode = splashBmp->getMode();

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

    DBG_OUT("WindowInfo_Paint() finish\n");
    if (!gDebugShowLinks)
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

        if (RectI_Intersect(&rectLink, &drawAreaRect, &intersect)) {
            rectScreen.left = (LONG) ((double)intersect.x - dm->areaOffset.x);
            rectScreen.top = (LONG) ((double)intersect.y - dm->areaOffset.y);
            rectScreen.right = rectScreen.left + rectLink.dx;
            rectScreen.bottom = rectScreen.top + rectLink.dy;
            FillRect(hdc, &rectScreen, gBrushLinkDebug);
            DBG_OUT("  link on screen rotate=%d, (x=%d, y=%d, dx=%d, dy=%d)\n",
                dm->rotation() + dm->pagesInfo[pdfLink->pageNo-1].rotation,
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
#define BETA_TXT                "Beta v0.3"
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
    /* static data, must be provided */
    const char *    leftTxt;
    const char *    rightTxt;
    const char *    url;

    /* data calculated by the layout */
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

    { "toolbar icons", "Mark James", "http://www.famfamfam.com/lab/icons/silk/",
    0, 0, 0, 0, 0, 0, 0, 0 },

    { NULL, NULL, NULL,
    0, 0, 0, 0, 0, 0, 0, 0 }
};

static const char *AboutGetLink(WindowInfo *win, int x, int y)
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

    GetClientRect(win->hwndCanvas, &rc);

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
    x = offX + (totalDx - sumatraPdfTxtDx) / 2 + sumatraPdfTxtDx + 6;
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

static void HandleLink(DisplayModelSplash *dm, PdfLink *pdfLink)
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
            dm->HandleLinkGoTo((LinkGoTo*)action);
            break;
        case actionGoToR:
            dm->HandleLinkGoToR((LinkGoToR*)action);
            break;
        case actionLaunch:
            dm->HandleLinkLaunch((LinkLaunch*)action);
            break;
        case actionURI:
            dm->HandleLinkURI((LinkURI*)action);
            break;
        case actionNamed:
            dm->HandleLinkNamed((LinkNamed *)action);
            break;
        default:
            /* other kinds are not supported */
            break;
    }
}

static void WinMoveDocBy(WindowInfo *win, int dx, int dy)
{
    assert(win);
    if (!win) return;
    assert (WS_SHOWING_PDF == win->state);
    if (WS_SHOWING_PDF != win->state) return;
    assert(win->dm);
    if (!win->dm) return;
    assert(!win->linkOnLastButtonDown);
    if (win->linkOnLastButtonDown) return;
    if (0 != dx)
        win->dm->ScrollXBy(dx);
    if (0 != dy)
        win->dm->ScrollYBy(dy, FALSE);
}

static void OnMouseLeftButtonDown(WindowInfo *win, int x, int y)
{
    assert(win);
    if (!win) return;
    if (WS_SHOWING_PDF == win->state) {
        assert(win->dm);
        if (!win->dm) return;
        win->linkOnLastButtonDown = win->dm->GetLinkAtPosition(x, y);
        /* dragging mode only starts when we're not on a link */
        if (!win->linkOnLastButtonDown) {
            SetCapture(win->hwndCanvas);
            win->dragging = TRUE;
            win->dragPrevPosX = x;
            win->dragPrevPosY = y;
            SetCursor(LoadCursor(NULL, IDC_HAND));
            DBG_OUT(" dragging start, x=%d, y=%d\n", x, y);
        }
    } else if (WS_ABOUT_ANIM == win->state) {
        win->url = AboutGetLink(win, x, y);
    }
}

void DisplayModelSplash::ShowNormalCursor(void)
{
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}

void DisplayModelSplash::ShowBusyCursor(void)
{
    // TODO: what is the right cursor?
    // can I set it per-window only?
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}

void DisplayModelSplash::CancelBackgroundRendering(void)
{
    // TODO: implement me!
    return;
}

static void OnMouseLeftButtonUp(WindowInfo *win, int x, int y)
{
    PdfLink *       link;
    const char *    url;
    int             dragDx, dragDy;

    assert(win);
    if (!win) return;

    if (WS_SHOWING_PDF == win->state) {
        assert(win->dm);
        if (!win->dm) return;
        if (win->dragging && (GetCapture() == win->hwndCanvas)) {
            dragDx = 0; dragDy = 0;
            dragDx = x - win->dragPrevPosX;
            dragDy = y - win->dragPrevPosY;
            DBG_OUT(" dragging ends, x=%d, y=%d, dx=%d, dy=%d\n", x, y, dragDx, dragDy);
            assert(!win->linkOnLastButtonDown);
            WinMoveDocBy(win, dragDx, -dragDy*2);
            win->dragPrevPosX = x;
            win->dragPrevPosY = y;
            win->dragging = FALSE;
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            ReleaseCapture();            
            return;
        }

        if (!win->linkOnLastButtonDown)
            return;

        link = win->dm->GetLinkAtPosition(x, y);
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
    DisplayModelSplash *  dm;
    PdfLink *       link;
    const char *    url;
    int             dragDx, dragDy;

    assert(win);
    if (!win) return;

    if (WS_SHOWING_PDF == win->state) {
        dm = win->dm;
        assert(dm);
        if (!dm) return;
        if (win->dragging) {
            dragDx = 0; dragDy = 0;
            dragDx = x - win->dragPrevPosX;
            dragDy = y - win->dragPrevPosY;
            DBG_OUT(" drag move, x=%d, y=%d, dx=%d, dy=%d\n", x, y, dragDx, dragDy);
            WinMoveDocBy(win, dragDx, dragDy*2);
            win->dragPrevPosX = x;
            win->dragPrevPosY = y;
            return;
        }
        link = dm->GetLinkAtPosition(x, y);
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

static void AnimState_AnimStop(AnimState *state)
{
    KillTimer(state->hwnd, ABOUT_ANIM_TIMER_ID);
}

static void AnimState_NextFrame(AnimState *state)
{
    state->frame += 1;
    InvalidateRect(state->hwnd, NULL, FALSE);
    UpdateWindow(state->hwnd);
}

static void AnimState_AnimStart(AnimState *state, HWND hwnd, UINT freqInMs)
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

    GetClientRect(win->hwndCanvas, &rc);

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

static void WindowInfo_DoubleBuffer_Resize_IfNeeded(WindowInfo *win)
{
    RECT    rc;
    int     win_dx, win_dy;
    GetClientRect(win->hwndCanvas, &rc);
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

    hdc = BeginPaint(win->hwndCanvas, &ps);

    SetBkMode(hdc, TRANSPARENT);
    GetClientRect(win->hwndCanvas, &rc);

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
    EndPaint(win->hwndCanvas, &ps);
}

static void OnMenuExit(void)
{
    Prefs_Save();
    PostQuitMessage(0);
}

/* Close the document associated with window 'hwnd'.
   Closes the window unless this is the last window in which
   case it switches to empty window and disables the "File\Close"
   menu item. */
static void CloseWindow(WindowInfo *win, BOOL quitIfLast)
{
    BOOL    lastWindow = FALSE;
    HWND    hwndToDestroy;

    assert(win);
    if (!win)
        return;

    if (1 == WindowInfoList_Len())
        lastWindow = TRUE;

    if (lastWindow)
        Prefs_Save();
    else
        UpdateCurrentFileDisplayStateForWin(win);

    win->state = WS_EMPTY;

    if (lastWindow && !quitIfLast) {
        /* last window - don't delete it */
        delete win->dm;
        win->dm = NULL;
        WindowInfo_RedrawAll(win);
    } else {
        hwndToDestroy = win->hwndFrame;
        WindowInfoList_Remove(win);
        WindowInfo_Delete(win);
        DragAcceptFiles(hwndToDestroy, FALSE);
        DestroyWindow(hwndToDestroy);
    }

    if (lastWindow && quitIfLast) {
        assert(0 == WindowInfoList_Len());
        PostQuitMessage(0);
    } else
        MenuToolbarUpdateStateForAllWindows();
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
    win->dm->ZoomTo(zoom);
    ZoomMenuItemCheck(GetMenu(win->hwndFrame), menuId);
}

/* Show Print Dialog box to allow user to select the printer
and the pages to print.

Creates a new dummy page for each page with a large zoom factor,
and then uses StretchDIBits to copy this to the printer's dc.

So far have tested printing from XP to
 - Acrobat Professional 6 (note that acrobat is usually set to
   downgrade the resolution of its bitmaps to 150dpi)
 - HP Laserjet 2300d
 - Lexmark Z515 inkjet, which should cover most bases.

TODO:
 * test on my printer
 * a better, synchronous way of generating a bitmap to print
*/
static void OnMenuPrint(WindowInfo *win)
{
    int                 pageNo;
    PdfPageInfo*        pageInfo;
    DisplayModelSplash *      dm;
    SplashBitmap *      splashBmp;
    int                 splashBmpDx, splashBmpDy;
    SplashColorMode     splashBmpColorMode;
    SplashColorPtr      splashBmpData;
    int                 splashBmpRowSize;
    BITMAPINFOHEADER    bmih;
    BitmapCacheEntry *  entry;
    PRINTDLG            pd;

    assert(win);
    if (!win) return;
    dm = win->dm;
    assert(dm);
    if (!dm) return;
    assert(dm->pdfDoc);
    if (!dm->pdfDoc) return;

    /* store current page number and zoom state to reset
       when finished printing */
    int pageNoInitial = dm->startPage;
    int zoomInitial = dm->zoomReal;

    /* printing uses the WindowInfo win that is created for the
       screen, it may be possible to create a new WindowInfo
       for printing to so we don't mess with the screen one,
       but the user is not inconvenienced too much, and this
       way we only need to concern ourselves with one dm.
       TODO: don't re-use WindowInfo, use a different, synchronious
       way of creating a bitmap */

    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner   = win->hwndFrame;
    pd.hDevMode    = NULL;   
    pd.hDevNames   = NULL;   
    pd.Flags       = PD_USEDEVMODECOPIESANDCOLLATE | PD_RETURNDC;
    pd.nCopies     = 1;
    /* by default print all pages */
    pd.nFromPage   = 1;
    pd.nToPage     = dm->pageCount;
    pd.nMinPage    = 1;
    pd.nMaxPage    = dm->pageCount;

    if (FALSE == PrintDlg(&pd)) {
        if (CommDlgExtendedError()) {
            /* if PrintDlg was cancelled then
               CommDlgExtendedError is zero, otherwise it returns the
               error code, which we could look at here if we wanted.
               for now just warn the user that printing has stopped
               becasue of an error */
            MessageBox(win->hwndFrame, "Cannot initialise printer", "Printing problem.", MB_ICONEXCLAMATION | MB_OK);
        }
    }

    DOCINFO di;
    ZeroMemory(&di,sizeof(DOCINFO));
    di.cbSize = sizeof (DOCINFO);

    //set the print job name from the file name
    di.lpszDocName = (LPCSTR) dm->pdfDoc->getFileName()->getCString() ;

    //to increase the resolution of the bitmap created for
    //the printed page, zoom in by say 300, seems to work ok.
    dm->ZoomTo(300);

    //start the printing
    if (StartDoc(pd.hDC, &di) <= 0)
        goto Exit;

    BOOL printingOk = TRUE;

    // print all the pages the user requested unless
    // bContinue flags there is a problem.
    for (pageNo = pd.nFromPage;
        pageNo <= pd.nToPage && printingOk;
        pageNo++)
    {

        pageInfo = dm->GetPageInfo(pageNo);

        int rotation = dm->rotation() + pageInfo->rotation;
        double zoomLevel = dm->zoomReal;

        splashBmp = NULL;

        // initiate the creation of the bitmap for the page.
        dm->GoToPage(pageNo, 0);

        // because we're multithreaded the bitmap may not be
        // ready yet so keep checking until it is available
        do
        {
            entry = BitmapCache_Find(dm, pageNo, zoomLevel, rotation);
            if (entry)
                splashBmp = ((WinRenderedBitmap*)entry->bitmap)->bitmap();
            Sleep(10);
        } while (!splashBmp);

        DBG_OUT(" printing:  drawing bitmap for page %d\n", pageNo);
        splashBmpDx = splashBmp->getWidth();
        splashBmpDy = splashBmp->getHeight();
        splashBmpRowSize = splashBmp->getRowSize();
        splashBmpData = splashBmp->getDataPtr();
        splashBmpColorMode = splashBmp->getMode();

        bmih.biSize = sizeof(bmih);
        bmih.biHeight = -splashBmpDy;
        bmih.biWidth = splashBmpDx;
        bmih.biPlanes = 1;
        //we could create this dibsection in monochrome
        //if the printer is monochrome, to reduce memory consumption
        //but splash is currently setup to return a full colour bitmap
        bmih.biBitCount = 24;
        bmih.biCompression = BI_RGB;
        bmih.biSizeImage = splashBmpDy * splashBmpRowSize;;
        bmih.biXPelsPerMeter = bmih.biYPelsPerMeter = 0;
        bmih.biClrUsed = bmih.biClrImportant = 0;

        //initiate the printed page
        StartPage(pd.hDC);

        //initialise device context
        SetMapMode(pd.hDC, MM_TEXT);
        //MM_TEXT: Each logical unit is mapped to one device pixel.
        //Positive x is to the right; positive y is down.

        //most printers can support stretchdibits,
        //whereas a lot of printers do not support bitblt

        //we check here that the printer does support
        //StretchDIBits and if it does then we StretchDIBits
        //the dibsection bmih to the printer dc.

        if (GetDeviceCaps(pd.hDC, RASTERCAPS) & RC_STRETCHDIB) {
            //printer is capabable of performing StretchDIBits
            //get the full printable area in pixels of the paper,
            //note, this is not the full paper size, so we're in
            //effect resizing the page to fit printable area.
            int nPageHeight = GetDeviceCaps (pd.hDC, VERTRES);
            int nPageWidth = GetDeviceCaps (pd.hDC, HORZRES);

            StretchDIBits(pd.hDC,
                //destination rectanngle
                0, 0, nPageWidth, nPageHeight,
                //source rectangle
                0, 0, splashBmpDx, splashBmpDy,
                splashBmpData,
                (BITMAPINFO *)&bmih ,
                DIB_RGB_COLORS,
                SRCCOPY);

            // we're creating about 30MB per page so clear the
            // bitmap now to avoid running out of RAM on old machines.
            // ideally call BitmapCacheEntry_Free(entry) but that's
            // not available to us here, so clear all instead!
            BitmapCache_FreeAll();

            // end the page, and check no error occurred
            if (EndPage(pd.hDC) <= 0)
                printingOk = FALSE;
        }
        else
        {
            // printer is not capabable of preforming StretchDIBits
            // so stop printing
            EndPage(pd.hDC);
            printingOk = FALSE;
        }
    }

    if (printingOk)
        EndDoc(pd.hDC);
    else
        AbortDoc(pd.hDC);

Exit:
    DeleteDC(pd.hDC);

    if (pd.hDevNames != NULL) GlobalFree(pd.hDevNames);
    if (pd.hDevMode != NULL) GlobalFree(pd.hDevMode);

    // reset the page and zoom that the user had before starting to print.
    dm->GoToPage(pageNoInitial,0);
    dm->ZoomTo(zoomInitial);
}

static void OnMenuOpen(WindowInfo *win)
{
    OPENFILENAME ofn = {0};
    char         fileName[260];
    GooString      fileNameStr;

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
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
    win->dm->RotateBy(-90);
}

static void RotateRight(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    win->dm->RotateBy(90);
}

static void OnVScroll(WindowInfo *win, WPARAM wParam)
{
    SCROLLINFO   si = {0};
    int          iVertPos;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(win->hwndCanvas, SB_VERT, &si);

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
    SetScrollInfo(win->hwndCanvas, SB_VERT, &si, TRUE);
    GetScrollInfo(win->hwndCanvas, SB_VERT, &si);

    // If the position has changed, scroll the window and update it
    if (win->dm && (si.nPos != iVertPos))
        win->dm->ScrollYTo(si.nPos);
}

static void OnHScroll(WindowInfo *win, WPARAM wParam)
{
    SCROLLINFO   si = {0};
    int          iVertPos;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(win->hwndCanvas, SB_HORZ, &si);

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
    SetScrollInfo(win->hwndCanvas, SB_HORZ, &si, TRUE);
    GetScrollInfo(win->hwndCanvas, SB_HORZ, &si);

    // If the position has changed, scroll the window and update it
    if (win->dm && (si.nPos != iVertPos))
        win->dm->ScrollXTo(si.nPos);
}

static void ViewWithAcrobat(WindowInfo *win)
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

static void OnSize(WindowInfo *win, int dx, int dy)
{
    int rebBarDy = 0;
    if (gShowToolbar) {
        SetWindowPos(win->hwndReBar, NULL, 0, 0, dx, rebBarDy, SWP_NOZORDER);
        rebBarDy = gReBarDy + gReBarDyFrame;
    }
    SetWindowPos(win->hwndCanvas, NULL, 0, rebBarDy, dx, dy-rebBarDy, SWP_NOZORDER);
}

static void OnMenuViewShowHideToolbar(WindowInfo *win)
{
    int     dx, dy, x, y;
    assert(win);

    DBG_OUT("OnMenuViewShowHideToolbar()\n");

    if (gShowToolbar)
        gShowToolbar = FALSE;
    else
        gShowToolbar = TRUE;

    win = gWindowList;
    while (win) {
        if (gShowToolbar)
            ShowWindow(win->hwndReBar, SW_SHOW);
        else
            ShowWindow(win->hwndReBar, SW_HIDE);
        Win32_Win_GetPos(win->hwndFrame, &x, &y);
        Win32_Win_GetSize(win->hwndFrame, &dx, &dy);
        // TODO: a hack. I add 1 to dy to cause sending WM_SIZE msg to hwndFrame
        // but I shouldn't really change the size. But I don't know how to
        // cause sending WM_SIZE otherwise. I tried calling OnSize() directly,
        // but it left scrollbar partially hidden
        MoveWindow(win->hwndFrame, x, y, dx, dy+1, TRUE);
        MenuUpdateShowToolbarStateForWindow(win);
        win = win->next;
    }
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
    win->dm->GoToNextPage(0);
}

static void OnMenuGoToPrevPage(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    win->dm->GoToPrevPage(0);
}

static void OnMenuGoToLastPage(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    win->dm->GoToLastPage();
}

static void OnMenuGoToFirstPage(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    win->dm->GoToFirstPage();
}

static void OnMenuGoToPage(WindowInfo *win)
{
    int     newPageNo;

    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;

    newPageNo = Dialog_GoToPage(win);
    if (win->dm->ValidPageNo(newPageNo)) {
        win->dm->GoToPage(newPageNo, 0);
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

static BOOL IsCtrlLeftPressed(void)
{
    int state = GetKeyState(VK_LCONTROL);
    if (0 != state)
        return TRUE;
    return FALSE;
}

static BOOL  IsCtrlRightPressed(void)
{
    int state = GetKeyState(VK_RCONTROL);
    if (0 != state)
        return TRUE;
    return FALSE;
}

static BOOL  IsCtrlPressed(void)
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
            win->dm->GoToPrevPage(0);
       /* SendMessage (win->hwnd, WM_VSCROLL, SB_PAGEUP, 0); */
    } else if (VK_NEXT == key) {
        /* TODO: this probably should be more intelligent (scroll if not yet at the bottom,
           go to next page if at the bottom, and something entirely different in continuous mode */
        if (win->dm)
            win->dm->GoToNextPage(0);
        /* SendMessage (win->hwnd, WM_VSCROLL, SB_PAGEDOWN, 0); */
    } else if (VK_UP == key) {
        if (win->dm)
            SendMessage (win->hwndCanvas, WM_VSCROLL, SB_LINEUP, 0);
    } else if (VK_DOWN == key) {
        if (win->dm)
            SendMessage (win->hwndCanvas, WM_VSCROLL, SB_LINEDOWN, 0);
    } else if (VK_LEFT == key) {
        SendMessage (win->hwndCanvas, WM_HSCROLL, SB_PAGEUP, 0);
    } else if (VK_RIGHT == key) {
        SendMessage (win->hwndCanvas, WM_HSCROLL, SB_PAGEDOWN, 0);
    } else if (('g' == key) || ('G' == key)) {
        if (IsCtrlLeftPressed())
            OnMenuGoToPage(win);
    }
}

static void OnChar(WindowInfo *win, int key)
{
    if (VK_SPACE == key) {
        win->dm->ScrollYByAreaDy(true, true);
    } else if (VK_BACK == key) {
        win->dm->ScrollYByAreaDy(false, true);
    } else if ('g' == key) {
        OnMenuGoToPage(win);
    } else if ('k' == key) {
        SendMessage(win->hwndCanvas, WM_VSCROLL, SB_LINEDOWN, 0);
    } else if ('j' == key) {
        SendMessage(win->hwndCanvas, WM_VSCROLL, SB_LINEUP, 0);
    } else if ('n' == key) {
        if (win->dm)
            win->dm->GoToNextPage(0);
    } else if ('c' == key) {
        // TODO: probably should preserve facing vs. non-facing
        if (win->dm)
            win->dm->SetDisplayMode(DM_CONTINUOUS);
    } else if ('p' == key) {
        if (win->dm)
            win->dm->GoToPrevPage(0);
    } else if ('z' == key) {
        WindowInfo_ToggleZoom(win);
    } else if ('q' == key) {
        DestroyWindow(win->hwndFrame);
    } else if ('+' == key) {
        win->dm->ZoomBy(ZOOM_IN_FACTOR);
    } else if ('-' == key) {
        win->dm->ZoomBy(ZOOM_OUT_FACTOR);
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

static void OnMenuAbout(WindowInfo *win)
{
    if (WS_ABOUT_ANIM != win->state) {
        win->prevState = win->state;
        win->state = WS_ABOUT_ANIM;
        AnimState_AnimStart(&(win->animState), win->hwndCanvas, ANIM_FREQ_IN_MS);
    } else {
        AnimState_AnimStop(&(win->animState));
        win->state = win->prevState;
    }
}

BOOL PrivateIsAppThemed()
{
    BOOL isThemed = FALSE;
    HMODULE hDll = LoadLibrary("uxtheme.dll");
    if (!hDll) return FALSE;

    FARPROC fp = GetProcAddress(hDll,"IsAppThemed");
    if (fp)
        isThemed = fp();

    FreeLibrary(hDll);
    return isThemed;
}

static TBBUTTON TbButtonFromButtonInfo(int i)
{
    TBBUTTON tbButton = {0};

    if (IDB_SEPARATOR == gToolbarButtons[i].cmdId) {
        tbButton.fsStyle = TBSTYLE_SEP;
    } else {
        tbButton.iBitmap = gToolbarButtons[i].index;
        tbButton.idCommand = gToolbarButtons[i].cmdId;
        tbButton.fsState = TBSTATE_ENABLED;
        tbButton.fsStyle = TBSTYLE_BUTTON;
        tbButton.iString = (INT_PTR)gToolbarButtons[i].toolTip;
    }
    return tbButton;
}

static void CreateToolbar(WindowInfo *win, HINSTANCE hInst)
{
    TBBUTTON        tbButtons[TOOLBAR_BUTTONS_COUNT];
    HWND            hwndToolbar;
    DWORD           dwToolbarStyle = WS_TOOLBAR;
    DWORD           dwReBarStyle = WS_REBAR;
    HIMAGELIST      himl = 0;
    HBITMAP         hbmp;
    BITMAP          bmp;
    RECT            rc;
    REBARINFO       rbi;
    REBARBANDINFO   rbBand;
    HWND            hwndParent = win->hwndFrame;
    BOOL            bIsAppThemed = PrivateIsAppThemed();

    hwndToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, dwToolbarStyle,
                                 0,0,0,0, hwndParent,(HMENU)IDC_TOOLBAR, hInst,NULL);
    win->hwndToolbar = hwndToolbar;
    SendMessage(hwndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    for (int i=0; i < TOOLBAR_BUTTONS_COUNT; i++) {
        if (IDB_SEPARATOR != gToolbarButtons[i].bitmapResourceId) {
            hbmp = LoadBitmap(hInst, MAKEINTRESOURCE(gToolbarButtons[i].bitmapResourceId));
            if (!himl) {
                GetObject(hbmp, sizeof(BITMAP), &bmp);
                int dx = bmp.bmWidth;
                int dy = bmp.bmHeight;
                himl = ImageList_Create(dx, dy, ILC_COLORDDB | ILC_MASK, 0, 0);
            }
            int index = ImageList_AddMasked(himl, hbmp, RGB(255,0,255));
            DeleteObject(hbmp);
            gToolbarButtons[i].index = index;
        }
        tbButtons[i] = TbButtonFromButtonInfo(i);
    }
    SendMessage(hwndToolbar, TB_SETIMAGELIST, 0, (LPARAM)himl);

    // TODO: construct disabled image list as well?
    //SendMessage(hwndToolbar, TB_SETDISABLEDIMAGELIST, 0, (LPARAM)himl);

    SendMessage(hwndToolbar, TB_SETEXTENDEDSTYLE, 0,
          SendMessage(hwndToolbar, TB_GETEXTENDEDSTYLE, 0, 0) | TBSTYLE_EX_MIXEDBUTTONS);

    SendMessage(hwndToolbar, TB_ADDBUTTONS, TOOLBAR_BUTTONS_COUNT, (LPARAM)tbButtons);

    SendMessage(hwndToolbar, TB_GETITEMRECT, 0, (LPARAM)&rc);

    dwReBarStyle |= WS_VISIBLE;
    win->hwndReBar = CreateWindowEx(WS_EX_TOOLWINDOW, REBARCLASSNAME, NULL, dwReBarStyle,
                             0,0,0,0, hwndParent, (HMENU)IDC_REBAR, hInst, NULL);

    rbi.cbSize = sizeof(REBARINFO);
    rbi.fMask  = 0;
    rbi.himl   = (HIMAGELIST)NULL;
    SendMessage(win->hwndReBar, RB_SETBARINFO, 0, (LPARAM)&rbi);

    rbBand.cbSize  = sizeof(REBARBANDINFO);
    rbBand.fMask   = /*RBBIM_COLORS | RBBIM_TEXT | RBBIM_BACKGROUND | */
                   RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE /*| RBBIM_SIZE*/;
    rbBand.fStyle  = /*RBBS_CHILDEDGE |*//* RBBS_BREAK |*/ RBBS_FIXEDSIZE /*| RBBS_GRIPPERALWAYS*/;
    if (bIsAppThemed)
        rbBand.fStyle |= RBBS_CHILDEDGE;
    rbBand.hbmBack = NULL;
    rbBand.lpText     = "Toolbar";
    rbBand.hwndChild  = hwndToolbar;
    rbBand.cxMinChild = (rc.right - rc.left) * TOOLBAR_BUTTONS_COUNT;
    rbBand.cyMinChild = (rc.bottom - rc.top) + 2 * rc.top;
    rbBand.cx         = 0;
    SendMessage(win->hwndReBar, RB_INSERTBAND, (WPARAM)-1, (LPARAM)&rbBand);

    SetWindowPos(win->hwndReBar, NULL, 0, 0, 0, 0, SWP_NOZORDER);
    GetWindowRect(win->hwndReBar, &rc);
    gReBarDy = rc.bottom - rc.top;
    gReBarDyFrame = bIsAppThemed ? 0 : 2;
}

/* TODO: gAccumDelta must be per WindowInfo */
static int      gDeltaPerLine, gAccumDelta;      // for mouse wheel logic

static LRESULT CALLBACK WndProcCanvas(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WindowInfo *    win;
    win = WindowInfo_FindByHwnd(hwnd);
    switch (message)
    {
        case WM_APP_MSG_REFRESH:
            if (win) {
                SetTimer(win->hwndCanvas, REPAINT_TIMER_ID, REPAINT_DELAY_IN_MS, NULL);
            }
            break;

        case WM_VSCROLL:
            OnVScroll(win, wParam);
            return WM_VSCROLL_HANDLED;

        case WM_HSCROLL:
            OnHScroll(win, wParam);
            return WM_HSCROLL_HANDLED;

        case WM_MOUSEMOVE:
            if (win)
                OnMouseMove(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
            break;

        case WM_LBUTTONDOWN:
            if (win)
                OnMouseLeftButtonDown(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;

        case WM_LBUTTONUP:
            if (win)
                OnMouseLeftButtonUp(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;

        case WM_SETCURSOR:
            if (win && win->dragging) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
            break;

        case WM_TIMER:
            assert(win);
            if (win) {
                if (REPAINT_TIMER_ID == wParam)
                    WindowInfo_RedrawAll(win);
                else
                    AnimState_NextFrame(&win->animState);
            }
            break;

        case WM_ERASEBKGND:
            // do nothing, helps to avoid flicker
            return TRUE;

        case WM_PAINT:
            /* it might happen that we get WM_PAINT after destroying a window */
            if (win) {
                /* blindly kill the timer, just in case it's there */
                KillTimer(win->hwndCanvas, REPAINT_TIMER_ID);
                OnPaint(win);
            }
            break;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

static LRESULT CALLBACK WndProcFrame(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int             wmId, wmEvent;
    WindowInfo *    win;
    ULONG           ulScrollLines;                   // for mouse wheel logic
    const char *    fileName;
    int             dx, dy;

    win = WindowInfo_FindByHwnd(hwnd);

    switch (message)
    {
        case WM_CREATE:
            // do nothing
            goto InitMouseWheelInfo;

        case WM_SIZE:
            if (win) {
                dx = LOWORD(lParam);
                dy = HIWORD(lParam);
                OnSize(win, dx, dy);
            }

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
                case IDT_FILE_OPEN:
                    OnMenuOpen(win);
                    break;

                case IDT_FILE_PRINT:
                case IDM_PRINT:
                    //OnMenuPrint(win);
                    break;

                case IDT_FILE_EXIT:
                case IDM_CLOSE:
                    CloseWindow(win, FALSE);
                    break;

                case IDM_EXIT:
                    OnMenuExit();
                    break;

                case IDT_VIEW_ZOOMIN:
                    if (win->dm)
                        win->dm->ZoomBy(ZOOM_IN_FACTOR);
                    break;

                case IDT_VIEW_ZOOMOUT:
                    if (win->dm)
                        win->dm->ZoomBy(ZOOM_OUT_FACTOR);
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

                case IDM_VIEW_SHOW_HIDE_TOOLBAR:
                    OnMenuViewShowHideToolbar(win);
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

        case WM_CHAR:
            if (win)
                OnChar(win, wParam);
            break;

        case WM_KEYDOWN:
            if (win)
                OnKeydown(win, wParam, lParam);
            break;

        case WM_SETTINGCHANGE:
InitMouseWheelInfo:
            SystemParametersInfo (SPI_GETWHEELSCROLLLINES, 0, &ulScrollLines, 0);
            // ulScrollLines usually equals 3 or 0 (for no scrolling)
            // WHEEL_DELTA equals 120, so iDeltaPerLine will be 40
            if (ulScrollLines)
                gDeltaPerLine = WHEEL_DELTA / ulScrollLines;
            else
                gDeltaPerLine = 0;
            return 0;

        // TODO: I don't understand why WndProcCanvas() doesn't receive this message
        case WM_MOUSEWHEEL:
            if (!win || !win->dm) /* TODO: check for pdfDoc as well ? */
                break;

            if (gDeltaPerLine == 0)
               break;

            gAccumDelta += (short) HIWORD (wParam);     // 120 or -120

            while (gAccumDelta >= gDeltaPerLine)
            {
                SendMessage(win->hwndCanvas, WM_VSCROLL, SB_LINEUP, 0);
                gAccumDelta -= gDeltaPerLine;
            }

            while (gAccumDelta <= -gDeltaPerLine)
            {
                SendMessage(win->hwndCanvas, WM_VSCROLL, SB_LINEDOWN, 0);
                gAccumDelta += gDeltaPerLine;
            }
            return 0;

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

static BOOL RegisterWinClass(HINSTANCE hInstance)
{
    WNDCLASSEX  wcex;
    ATOM        atom;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProcFrame;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SUMATRAPDF));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = NULL;
    wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_SUMATRAPDF);
    wcex.lpszClassName  = FRAME_CLASS_NAME;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    atom = RegisterClassEx(&wcex);
    if (!atom)
        return FALSE;

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProcCanvas;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = 0;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = NULL;
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = CANVAS_CLASS_NAME;
    wcex.hIconSm        = 0;

    atom = RegisterClassEx(&wcex);
    if (atom)
        return TRUE;

    return FALSE;
}

static BOOL InstanceInit(HINSTANCE hInstance, int nCmdShow)
{
    ghinst = hInstance;

    globalParams = new GlobalParams("");
    if (!globalParams)
        return FALSE;

    ColorsInit();
    gCursorArrow = LoadCursor(NULL, IDC_ARROW);
    gCursorWait  = LoadCursor(NULL, IDC_WAIT);

    gBrushBg     = CreateSolidBrush(COL_WINDOW_BG);
    gBrushWhite  = CreateSolidBrush(COL_WHITE);
    gBrushShadow = CreateSolidBrush(COL_WINDOW_SHADOW);
    gBrushLinkDebug = CreateSolidBrush(RGB(0x00,0x00,0xff));
    return TRUE;
}

static void StrList_Reverse(StrList **strListRoot)
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

static BOOL StrList_InsertAndOwn(StrList **root, char *txt)
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

static void StrList_Destroy(StrList **root)
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

static StrList *StrList_FromCmdLine(char *cmdLine)
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

static void u_DoAllTests(void)
{
#ifdef DEBUG
    printf("Running tests\n");
    u_RectI_Intersect();
#else
    printf("Not running tests\n");
#endif
}

static DWORD WINAPI PageRenderThread(PVOID data)
{
    PageRenderRequest       req;
    SplashBitmap *          bmp;
    BOOL                    fOk;
    DWORD                   waitResult;
    int                     count;

    DBG_OUT("PageRenderThread() started\n");
    while (1) {
        DBG_OUT("Worker: wait\n");
        LockCache();
        gCurPageRenderReq = NULL;
        count = gPageRenderRequestsCount;
        UnlockCache();
        if (0 == count) {
            waitResult = WaitForSingleObject(gPageRenderSem, INFINITE);
            if (WAIT_OBJECT_0 != waitResult) {
                DBG_OUT("  WaitForSingleObject() failed\n");
                continue;
            }
        }
        if (0 == gPageRenderRequestsCount) {
            continue;
        }
        LockCache();
        RenderQueue_Pop(&req);
        gCurPageRenderReq = &req;
        UnlockCache();
        DBG_OUT("PageRenderThread(): dequeued %d\n", req.pageNo);
        assert(!req.abort);
        bmp = RenderBitmap(req.dm, req.pageNo, req.zoomLevel, req.rotation, pageRenderAbortCb, (void*)&req);
        LockCache();
        gCurPageRenderReq = NULL;
        UnlockCache();
        if (req.abort) {
            delete bmp;
            continue;
        }
        /* TODO: can bmp be NULL ? */
        assert(bmp);
        DBG_OUT("PageRenderThread(): finished rendering %d\n", req.pageNo);
        WinRenderedBitmap *renderedBmp = new WinRenderedBitmap(bmp);
        BitmapCache_Add(req.dm, req.pageNo, req.zoomLevel, req.rotation, renderedBmp);
#ifdef CONSERVE_MEMORY
        BitmapCache_FreeNotVisible();
#endif
        WindowInfo * win = NULL;
        win = (WindowInfo*)req.dm->appData;
        fOk = PostMessage(win->hwndCanvas, WM_APP_MSG_REFRESH, 0, 0);
    }
    DBG_OUT("PageRenderThread() finished\n");
    return 0;
}

static void CreatePageRenderThread(void)
{
    LONG semMaxCount = 1000; /* don't really know what the limit should be */
    DWORD dwThread1ID = 0;
    assert(NULL == gPageRenderThreadHandle);

    gPageRenderSem = CreateSemaphore(NULL, 0, semMaxCount, NULL);
    gPageRenderThreadHandle = CreateThread(NULL, 0, PageRenderThread, (void*)NULL, 0, &dwThread1ID);
    assert(NULL != gPageRenderThreadHandle);
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

    CreatePageRenderThread();
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
                if (win)
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
            DragAcceptFiles(win->hwndFrame, TRUE);
            ShowWindow(win->hwndCanvas, SW_SHOW);
            UpdateWindow(win->hwndCanvas);
            ShowWindow(win->hwndFrame, SW_SHOW);
            UpdateWindow(win->hwndFrame);
        }
    }

    if (IsBenchMode()) {
        assert(win);
        assert(pdfOpened > 0);
        if (win)
            PostBenchNextAction(win->hwndFrame);
    }

    if (0 == pdfOpened)
        MenuToolbarUpdateStateForAllWindows();

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
    DeleteObject(gBrushWhite);
    DeleteObject(gBrushShadow);
    DeleteObject(gBrushLinkDebug);

    delete globalParams;
    StrList_Destroy(&argListRoot);
    //histDump();
    return (int) msg.wParam;
}
