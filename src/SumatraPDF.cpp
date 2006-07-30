#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#include "SumatraPDF.h"

#include <shellapi.h>
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

#include "Win32FontList.h"
#include "strlist_util.h"
#include "file_util.h"
#include "SimpleRect.h"
#include "DisplayModel.h"
#include "BaseUtils.h"

#define BITMAP_TOP_DOWN

/* Next action for the benchmark mode */
#define MSG_BENCH_NEXT_ACTION WM_USER + 1

#define dimof(X)    (sizeof(X)/sizeof((X)[0]))

enum AppVisualStyle {
    VS_WINDOWS = 1,
    VS_AMIGA
};

enum WinState {
    WS_EMPTY = 1,
    WS_ERROR_LOADING_PDF,
    WS_SHOWING_PDF
};

#define ZOOM_IN_FACTOR      1.2
#define ZOOM_OUT_FACTOR     1.0 / ZOOM_IN_FACTOR

/* Uncomment to visually show links as blue rectangles, for easier links
   debugging */
//#define DEBUG_SHOW_LINKS            1

/* default UI settings */
#define DEFAULT_CONTINUOUS          TRUE
#define DEFAULT_PAGES_AT_A_TIME     1

//#define DEFAULT_ZOOM         ZOOM_FIT_WIDTH
#define DEFAULT_ZOOM         ZOOM_FIT_PAGE
#define DEFAULT_ROTATION     0

/* define if want to use double-buffering for rendering the PDF. Takes more memory!. */
#define DOUBLE_BUFFER 1

#define DRAGQUERY_NUMFILES 0xFFFFFFFF

#define MAX_LOADSTRING 100

#define WM_CREATE_FAILED -1
#define WM_CREATE_OK 0
#define WM_NCPAINT_HANDLED 0
#define WM_VSCROLL_HANDLED 0
#define WM_HSCROLL_HANDLED 0

#define BORDER_TOP     4
#define BORDER_BOTTOM  4

/* TODO: those are not used yet */
#define BORDER_LEFT    4
#define BORDER_RIGHT   4

/* A caption is 4 white/blue 2 pixel line and a 3 pixel white line */
#define CAPTION_DY 2*(2*4)+3

#define COL_CAPTION_BLUE RGB(0,0x50,0xa0)
#define COL_WHITE RGB(0xff,0xff,0xff)
#define COL_WINDOW_BG RGB(0xcc, 0xcc, 0xcc)
//#define COL_WINDOW_BG RGB(0xff, 0xff, 0xff)
#define COL_WINDOW_SHADOW RGB(0x40, 0x40, 0x40)

#define WIN_CLASS_NAME  _T("SUMATRA_PDF_WIN")
#define APP_NAME        _T("SumatraPDF")
#define PDF_DOC_NAME    _T("Adobe PDF Document")

#define INVALID_PAGE_NUM -1
#define BENCH_ARG_TXT "-bench"

/* Describes information related to one window with (optional) pdf document
   on the screen */
typedef struct WindowInfo {
    /* points to the next element in the list or the first element if
       this is the first element */
    WindowInfo *    next;
    DisplayModel *  dm;
    HWND            hwnd;

    HDC             hdc;
    BITMAPINFO *    dibInfo;
    int             winDx;
    int             winDy;
    HDC             hdcToDraw;
    HDC             hdcDoubleBuffer;
    HBITMAP         bmpDoubleBuffer;
    WinState        state;
    PdfLink *       linkOnLastButtonDown;
} WindowInfo;

static SplashColor splashColRed;
static SplashColor splashColGreen;
static SplashColor splashColBlue;
static SplashColor splashColWhite;
static SplashColor splashColBlack;

#define SPLASH_COL_RED_PTR (SplashColorPtr)&(splashColRed[0])
#define SPLASH_COL_GREEN_PTR (SplashColorPtr)&(splashColGreen[0])
#define SPLASH_COL_BLUE_PTR (SplashColorPtr)&(splashColBlue[0])
#define SPLASH_COL_WHITE_PTR (SplashColorPtr)&(splashColWhite[0])
#define SPLASH_COL_BLACK_PTR (SplashColorPtr)&(splashColBlack[0])

static HINSTANCE    ghinst = NULL;
TCHAR               szTitle[MAX_LOADSTRING];

static WindowInfo*  gWindowList = NULL;

static HCURSOR      gCursorArrow = NULL;
static HCURSOR      gCursorWait = NULL;
static HBRUSH       gBrushBg;
static HBRUSH       gBrushShadow;
static HBRUSH       gBrushLinkDebug;

static HPEN         ghpenWhite = NULL;
static HPEN         ghpenBlue = NULL;

static SplashColorPtr  gBgColor = SPLASH_COL_WHITE_PTR;
static SplashColorMode gSplashColorMode = splashModeBGR8;

static AppVisualStyle  gVisualStyle = VS_WINDOWS;

static StrList *    gArgListRoot = NULL;
static char *       gBenchFileName = NULL;
static int          gBenchPageNum = INVALID_PAGE_NUM;

#ifdef DOUBLE_BUFFER
static BOOL            gUseDoubleBuffer = TRUE;
#else
static BOOL            gUseDoubleBuffer = FALSE;
#endif

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
const char *FileGetBaseName(const char *path)
{
    /* TODO: implement me */
    return path;
}

void WinSetText(HWND hwnd, const TCHAR *txt)
{
    SendMessage(hwnd, WM_SETTEXT, (WPARAM)0, (LPARAM)txt);
}

int isPdfFile(FileInfo *fi)
{
    assert(fi);
    if (!fi) return 0;

#if 0 /* TODO: figure out why doesn't work */
    if (FileInfo_IsDir(fi))
        return 0;
#endif

    if (utf8_endswith(fi->name, ".pdf"))
        return 1;
    return 0;
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
    SetWindowPos(hwnd, NULL, 0, 0, win_dx, win_dy, SWP_NOACTIVATE | SWP_NOREPOSITION | SWP_NOZORDER);
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

static char *GetPasswordForFile(const char *fileName)
{
    /* TODO: show a dialog */
    return Str_Dup("kjk");
}

void *StandardSecurityHandler::getAuthData() 
{
    WindowInfo *        winInfo;
    const char *        pwd;
    StandardAuthData *  authData;

    winInfo = (WindowInfo*)doc->getGUIData();
    assert(winInfo);
    if (!winInfo)
        return NULL;

    pwd = GetPasswordForFile(doc->getFileName()->getCString());
    if (!pwd)
        return NULL;

    authData = new StandardAuthData(new GooString(pwd), new GooString(pwd));
    free((void*)pwd);
    return (void*)authData;
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

    win->state = WS_EMPTY;
    win->hwnd = hwnd;
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

/* Disable File\Close menu if we're not showing any PDF files,
   enable otherwise */
static void CloseMenuUpdateState(void)
{
    BOOL            enabled = FALSE;
    WindowInfo *    win = gWindowList;
    HMENU           hmenu;

    while (!enabled && win) {
        if (win->state == WS_SHOWING_PDF)
            enabled = TRUE;
        win = win->next;
    }

    win = gWindowList;
    while (win) {
        hmenu = GetMenu(win->hwnd);
        if (enabled)
            EnableMenuItem(hmenu, IDM_CLOSE, MF_BYCOMMAND | MF_ENABLED);
        else
            EnableMenuItem(hmenu, IDM_CLOSE, MF_BYCOMMAND | MF_GRAYED);
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
            CW_USEDEFAULT, 0,
            CW_USEDEFAULT, 0,
            NULL, NULL,
            ghinst, NULL);

    if (!hwnd)
        return NULL;

    win = WindowInfo_New(hwnd);
    return win;
}

static WindowInfo* LoadPdf(const TCHAR *file_name, BOOL close_invalid_files)
{
    int             err;
    WindowInfo *    win;
    GooString *     file_name_str = NULL;
    int             reuse_existing_window = FALSE;
    PDFDoc *        pdfDoc;
    RectDSize       totalDrawAreaSize;
    int             scrollbarYDx, scrollbarXDy;
    SplashOutputDev *outputDev = NULL;
#ifdef BITMAP_TOP_DOWN
    GBool           bitmapTopDown = gTrue;
#else
    GBool           bitmapTopDown = gFalse;
#endif
    int             continuous = DEFAULT_CONTINUOUS;
    int             pagesAtATime = DEFAULT_PAGES_AT_A_TIME;

    if ((1 == WindowInfoList_Len()) && (WS_SHOWING_PDF != gWindowList->state)) {
        win = gWindowList;
        reuse_existing_window = TRUE;
    }

    if (!reuse_existing_window) {
        win = WindowInfo_CreateEmpty();
        if (!win)
            return NULL;
        if (!file_name) {
            WindowInfoList_Add(win);
            goto Exit;
        }
    }

    file_name_str = new GooString(file_name);
    if (!file_name_str)
        return win;

    err = errNone;
    pdfDoc = new PDFDoc(file_name_str, NULL, NULL, (void*)win);
    if (!pdfDoc->isOk())
    {
        err = errOpenFile;
        error(-1, "LoadPdf(): failed to open PDF file %s\n", file_name);
    }

    if (close_invalid_files && (errNone != err) && !reuse_existing_window)
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

    /* In theory I should get scrollbars sizes using Win32_GetScrollbarSize(&scrollbarYDx, &scrollbarXDy);
       but scrollbars are not part of the client area on windows so it's better
       not to have them taken into account by DisplayModel code.
       TODO: I think it's broken anyway and DisplayModel needs to know if
             scrollbars are part of client area in order to accomodate windows
             UI properly */
    scrollbarYDx = 0;
    scrollbarXDy = 0;
    win->dm = DisplayModel_CreateFromPdfDoc(pdfDoc, outputDev, totalDrawAreaSize,
        scrollbarYDx, scrollbarXDy, continuous, pagesAtATime, 1);
    if (!win->dm) {
        delete outputDev;
        WindowInfo_Delete(win);
        return NULL;
    }

#ifdef DEBUG_SHOW_LINKS
    win->dm->debugShowLinks = TRUE;
#endif

    win->dm->appData = (void*)win;

Error:
    if (!reuse_existing_window) {
        if (errNone != err) {
            if (WindowInfoList_ExistsWithError()) {
                /* don't create more than one window with errors */
                WindowInfo_Delete(win);
                return NULL;
            }
        }
        WindowInfoList_Add(win);
    }

    if (errNone != err) {
        win->state = WS_ERROR_LOADING_PDF;
        DBG_OUT("failed to load file %s, error=%d\n", file_name, (int)err);
    } else {
        win->state = WS_SHOWING_PDF;
        DisplayModel_Relayout(win->dm, DEFAULT_ZOOM, DEFAULT_ROTATION);
        DisplayModel_GoToPage(win->dm, 1, 0);
    }
    if (reuse_existing_window)
        WindowInfo_RedrawAll(win);

Exit:
    CloseMenuUpdateState();
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
    /* splash colors */
    SplashColorSet(SPLASH_COL_RED_PTR, 0xff, 0, 0, 0);
    SplashColorSet(SPLASH_COL_GREEN_PTR, 0, 0xff, 0, 0);
    SplashColorSet(SPLASH_COL_BLUE_PTR, 0, 0, 0xff, 0);
    SplashColorSet(SPLASH_COL_BLACK_PTR, 0, 0, 0, 0);
    SplashColorSet(SPLASH_COL_WHITE_PTR, 0xff, 0xff, 0xff, 0);
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
    baseName = FileGetBaseName(win->dm->pdfDoc->getFileName()->getCString());
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

#if 0
void WinPDFCore::resizeToPage(int pg)
{
    int         width, height;
    double      width1, height1;
    int         displayW, displayH;
    int         fullScreen = 0;
    HDC         hdc = GetDC(win->hwnd);

    displayW = GetDeviceCaps(hdc, HORZRES);
    displayH = GetDeviceCaps(hdc, VERTRES);

    if (fullScreen) {
        /* TODO: fullscreen not yet supported */
        assert(0);
        width = displayW;
        height = displayH;
    } else {
        if (pg < 0 || pg > doc->getNumPages()) {
            width1 = 612;
            height1 = 792;
        } else if (doc->getPageRotate(pg) == 90 ||
            doc->getPageRotate(pg) == 270) {
            width1 = doc->getPageCropHeight(pg);
            height1 = doc->getPageCropWidth(pg);
        } else {
            width1 = doc->getPageCropWidth(pg);
            height1 = doc->getPageCropHeight(pg);
        }

        if (zoom == zoomPage || zoom == zoomWidth) {
            width = (int)(width1 * 0.01 * defZoom + 0.5);
            height = (int)(height1 * 0.01 * defZoom + 0.5);
        } else {
            width = (int)(width1 * 0.01 * zoom + 0.5);
            height = (int)(height1 * 0.01 * zoom + 0.5);
        }

        if (continuousMode) {
            height += continuousModePageSpacing;
        }

        if (width > displayW - 100) {
            width = displayW - 100;
        }

        if (height > displayH - 100) {
            height = displayH - 100;
        }
    }

    WinResizeClientArea(win->hwnd, width, height + BORDER_BOTTOM + BORDER_TOP);
}
#endif

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

    hr = StringCchPrintfA(tmp, dimof(tmp), "%s,1", exePath);
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
        (win_dy == win->winDy))
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
    if (!win->dm)
        return;
    win->linkOnLastButtonDown = DisplayModel_GetLinkAtPosition(win->dm, x, y);
}

void LaunchBrowser(const char *uri)
{
    /* TODO: implement me */
}

static void OnMouseLeftButtonUp(WindowInfo *win, int x, int y)
{
    PdfLink *   link;

    assert(win);
    if (!win) return;
    if (!win->dm)
        return;

    if (!win->linkOnLastButtonDown)
        return;

    link = DisplayModel_GetLinkAtPosition(win->dm, x, y);
    if (link && (link == win->linkOnLastButtonDown))
        HandleLink(win->dm, link);
    win->linkOnLastButtonDown = NULL;
}

static void OnMouseMove(WindowInfo *win, int x, int y, WPARAM flags)
{
    DisplayModel *  dm;
    PdfLink *       link;

    assert(win);
    if (!win) return;
    dm = win->dm;
    if (!dm) return;

    link = DisplayModel_GetLinkAtPosition(dm, x, y);
    if (link) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        //DBG_OUT("OnMoseMove(): found link at pos (%d,%d)\n", x, y);
    } else {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }
}

static void OnPaint(WindowInfo *win)
{
    HDC         hdc;
    PAINTSTRUCT ps;
    RECT        rc;

    hdc = BeginPaint(win->hwnd, &ps);

    SetBkMode(hdc, TRANSPARENT);
    GetClientRect(win->hwnd, &rc);

    if (WS_EMPTY == win->state) {
        FillRect(hdc, &ps.rcPaint, gBrushBg);
        DrawText (hdc, "No PDF file opened. Open a new PDF file.", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER) ;
    } else if (WS_ERROR_LOADING_PDF == win->state) {
        FillRect(hdc, &ps.rcPaint, gBrushBg);
        DrawText (hdc, "Error loading PDF file.", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER) ;
    } else {
        //TODO: it might cause infinite loop due to showing/hiding scrollbars
        WinResizeIfNeeded(win);
        WindowInfo_Paint(win, hdc, &ps);
#if 0
        if (VS_AMIGA == gVisualStyle)
            AmigaCaptionDraw(win);
#endif
        WindowInfo_DoubleBuffer_Show(win, hdc);
    }
    EndPaint(win->hwnd, &ps);
}

void OnMenuExit(void)
{
    PostQuitMessage(0);
}

/* Close the document associated with window 'hwnd'.
   Closes the window unless this is the last window in which
   case it switches to empty window and disables the "File\Close"
   menu item. */
void CloseWindow(WindowInfo *win, BOOL quit_if_last)
{
    BOOL    last_window = FALSE;
    HWND    hwnd_to_destroy = NULL;
    win->state = WS_EMPTY;

    if (1 == WindowInfoList_Len())
        last_window = TRUE;

    if (last_window && !quit_if_last) {
        /* last window - don't delete it */
        //win->pdfCore->clear();
        WindowInfo_RedrawAll(win);
    } else {
        hwnd_to_destroy = win->hwnd;
        WindowInfoList_Remove(win);
        WindowInfo_Delete(win);
        DestroyWindow(hwnd_to_destroy);
    }

    if (last_window) {
        if (quit_if_last) {
            assert(0 == WindowInfoList_Len());
            OnMenuExit();
        }
    }
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
    char         file_name[260];
    GooString      fileNameStr;

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwnd;
    ofn.lpstrFile = file_name;

    // Set lpstrFile[0] to '\0' so that GetOpenFileName does not
    // use the contents of szFile to initialize itself.
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(file_name);
    ofn.lpstrFilter = "PDF\0*.pdf\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    // Display the Open dialog box.
    if (FALSE == GetOpenFileName(&ofn))
        return;

    win = LoadPdf(file_name, TRUE);
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

static void OnKeydown(WindowInfo *win, int key)
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
        SendMessage (win->hwnd, WM_VSCROLL, SB_LINEUP, 0);
    } else if (VK_DOWN == key) {
        /* SendMessage (win->hwnd, WM_VSCROLL, SB_LINEDOWN, 0); */
    } else if (VK_LEFT == key) {
        SendMessage (win->hwnd, WM_HSCROLL, SB_PAGEUP, 0);
    } else if (VK_RIGHT == key) {
        SendMessage (win->hwnd, WM_HSCROLL, SB_PAGEDOWN, 0);
    }
}

static void OnChar(WindowInfo *win, int key)
{
    if (VK_SPACE == key) {
        DisplayModel_ScrollYByAreaDy(win->dm, true, true);
    } else if (VK_BACK == key) {
        DisplayModel_ScrollYByAreaDy(win->dm, false, true);
    } else if ('k' == key) {
        SendMessage(win->hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
    } else if ('j' == key) {
        SendMessage(win->hwnd, WM_VSCROLL, SB_LINEUP, 0);
    } else if ('n' == key) {
        if (win->dm)
            DisplayModel_GoToNextPage(win->dm, 0);
    } else if ('c' == key) {
        if (win->dm)
            DisplayModel_SwitchToContinuous(win->dm);
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
    DisplayModel_SwitchToSinglePage(win->dm);
}

static void OnMenuViewFacing(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    DisplayModel_SwitchToFacing(win->dm);
}

static void OnMenuViewContinuous(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    DisplayModel_SwitchToContinuous(win->dm);
}

static void OnMenuViewContinuousFacing(WindowInfo *win)
{
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    DisplayModel_SwitchToContinuousFacing(win->dm);
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
    assert(win);
    if (!win) return;
    if (!WindowInfo_PdfLoaded(win))
        return;
    /* TODO: not implemented yet */
    assert(0);
}

static void OnMenuViewRotateLeft(WindowInfo *win)
{
    RotateLeft(win);
}

static void OnMenuViewRotateRight(WindowInfo *win)
{
    RotateRight(win);
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

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int             wmId, wmEvent;
    WindowInfo *    win;
    static int      iDeltaPerLine, iAccumDelta;      // for mouse wheel logic
    ULONG           ulScrollLines;                   // for mouse wheel logic

    win = WindowInfo_FindByHwnd(hwnd);

    switch (message)
    {
        case WM_CREATE:
            // do nothing
            goto InitMouseWheelInfo;

        case WM_COMMAND:
            wmId    = LOWORD(wParam);
            wmEvent = HIWORD(wParam);
            switch (wmId)
            {
                case IDM_OPEN:
                    OnMenuOpen(win);
                    break;

                case IDM_CLOSE:
                    CloseWindow(win, FALSE);
                    CloseMenuUpdateState();
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

                case IDM_ABOUT:
                    DialogBox(ghinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, About);
                    break;

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

        case WM_KEYDOWN:
            if (win)
                OnKeydown(win, wParam);
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
    StrList *   cur;
    char *      exeName;
    char *      benchPageNumStr = NULL;
    MSG         msg;
    HACCEL      hAccelTable;
    WindowInfo* win;

    UNREFERENCED_PARAMETER(hPrevInstance);

    u_DoAllTests();

    gArgListRoot = StrList_FromCmdLine(lpCmdLine);
    assert(gArgListRoot);
    if (!gArgListRoot)
        return 0;
    exeName = gArgListRoot->str;

    /* parse argument list. If BENCH_ARG_TXT was given, then we're in benchmarking mode. Otherwise
    we assume that all arguments are PDF file names.
    BENCH_ARG_TXT can be followed by file or directory name. If file, it can additionally be followed by
    a number which we interpret as page number */
    cur = gArgListRoot->next;
    while (cur) {
        if (IsBenchArg(cur->str)) {
            cur = cur->next;
            if (cur) {
                gBenchFileName = cur->str;
                if (cur->next)
                    benchPageNumStr = cur->next->str;
            }
            break;
        }
        cur = cur->next;
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

    int pdfOpened = 0;
    if (NULL != gBenchFileName) {
            win = LoadPdf(gBenchFileName, FALSE);
            if (win)
                ++pdfOpened;
    } else {
        cur = gArgListRoot->next;
        while (cur) {
            win = LoadPdf(cur->str, FALSE);
            if (!win)
                goto Exit;
           ++pdfOpened;
            cur = cur->next;
        }
    }

    if (0 == pdfOpened) {
        /* disable benchmark mode if we couldn't open file to benchmark */
        gBenchFileName = 0;
        win = WindowInfo_CreateEmpty();
        if (!win)
            goto Exit;
        WindowInfoList_Add(win);
        DragAcceptFiles(win->hwnd, TRUE);
        ShowWindow(win->hwnd, SW_SHOW);
        UpdateWindow(win->hwnd);
    }

    if (IsBenchMode()) {
        assert(win);
        assert(pdfOpened > 0);
        if (win)
            PostBenchNextAction(win->hwnd);
    }

    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

Exit:
    WindowInfoList_DeleteAll();
    CaptionPens_Destroy();
    DeleteObject(gBrushBg);
    DeleteObject(gBrushShadow);
    DeleteObject(gBrushLinkDebug);

    delete globalParams;
    //WinFontList_Destroy();
    StrList_Destroy(&gArgListRoot);
    return (int) msg.wParam;
}
