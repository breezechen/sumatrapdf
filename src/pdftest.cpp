#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef WIN32
#include <windows.h>
#endif

#if 0
#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 0
#endif
#endif

#include "ErrorCodes.h"
#include "GooString.h"
#include "GooList.h"
#include "GlobalParams.h"
#include "SplashBitmap.h"
#include "Object.h" /* must be included before SplashOutputDev.h because of sloppiness in SplashOutputDev.h */
#include "SplashOutputDev.h"
#include "PDFDoc.h"

extern void PreviewBitmap(SplashBitmap *);

/* TODO: list of pdf doc properties to print:
  Object info;
  doc->getDocInfo(&info);
  if (info.isDict()) {
    printInfoString(info.getDict(), "Title",        "Title:          ", uMap);
    printInfoString(info.getDict(), "Subject",      "Subject:        ", uMap);
    printInfoString(info.getDict(), "Keywords",     "Keywords:       ", uMap);
    printInfoString(info.getDict(), "Author",       "Author:         ", uMap);
    printInfoString(info.getDict(), "Creator",      "Creator:        ", uMap);
    printInfoString(info.getDict(), "Producer",     "Producer:       ", uMap);
    printInfoDate(info.getDict(),   "CreationDate", "CreationDate:   ");
    printInfoDate(info.getDict(),   "ModDate",      "ModDate:        ");
  }
  info.free();
*/

#ifdef WIN32
#define DIR_SEP_CHAR '\\'
#define DIR_SEP_STR  "\\"
#else
#define DIR_SEP_CHAR '/'
#define DIR_SEP_STR  "/"
#endif

#define MAX_FILENAME_SIZE 1024

struct FindFileState {
    char path[MAX_FILENAME_SIZE];
    char dirpath[MAX_FILENAME_SIZE]; /* current dir path */
    char pattern[MAX_FILENAME_SIZE]; /* search pattern */
    const char *bufptr;
#ifdef WIN32
    WIN32_FIND_DATA fileinfo;
    HANDLE dir;
#else
    DIR *dir;
#endif
};

#ifdef WIN32
#include <windows.h>
#include <sys/timeb.h>
#include <direct.h>

__inline char *getcwd(char *buffer, int maxlen)
{
    return _getcwd(buffer, maxlen);
}

/* XXX: not suffisant, but OK for basic operations */
int fnmatch(const char *pattern, const char *string, int flags)
{
    int prefix_len;
    const char *star_pos = strchr(pattern, '*');
    if (!star_pos)
        return strcmp(pattern, string) != 0;

    prefix_len = (int)(star_pos-pattern);
    if (0 == prefix_len)
        return 0;

    if (0 == _strnicmp(pattern, string, prefix_len))
        return 0;

    return 1;
}

#else
#include <fnmatch.h>
#endif

#ifdef WIN32
/* on windows to query dirs we need foo\* to get files in this directory.
    foo\ always fails and foo will return just info about foo directory,
    not files in this directory */
static void win_correct_path_for_FindFirstFile(char *path, int path_max_len)
{
    int path_len = strlen(path);
    if (path_len >= path_max_len-4)
        return;
    if (DIR_SEP_CHAR != path[path_len])
        path[path_len++] = DIR_SEP_CHAR;
    path[path_len++] = '*';
    path[path_len] = 0;
}
#endif

void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for (;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

FindFileState *find_file_open(const char *path, const char *pattern)
{
    FindFileState *s;

    s = (FindFileState*)malloc(sizeof(FindFileState));
    if (!s)
        return NULL;
    pstrcpy(s->path, sizeof(s->path), path);
    pstrcpy(s->dirpath, sizeof(s->path), path);
#ifdef WIN32
    win_correct_path_for_FindFirstFile(s->path, sizeof(s->path));
#endif
    pstrcpy(s->pattern, sizeof(s->pattern), pattern);
    s->bufptr = s->path;
#ifdef WIN32
    s->dir = INVALID_HANDLE_VALUE;
#else
    s->dir = NULL;
#endif
    return s;
}

/* strcat and truncate. */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
    if (len < buf_size) 
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}

char *makepath(char *buf, int buf_size, const char *path,
               const char *filename)
{
    int len;

    pstrcpy(buf, buf_size, path);
    len = strlen(path);
    if (len > 0 && path[len - 1] != DIR_SEP_CHAR && len + 1 < buf_size) {
        buf[len++] = DIR_SEP_CHAR;
        buf[len] = '\0';
    }
    return pstrcat(buf, buf_size, filename);
}

static int skip_matching_file(const char *filename)
{
    if (0 == strcmp(".", filename))
        return 1;
    if (0 == strcmp("..", filename))
        return 1;
    return 0;
}

int find_file_next(FindFileState *s, char *filename, int filename_size_max)
{
#ifdef WIN32
    BOOL    fFound;
    if (INVALID_HANDLE_VALUE == s->dir) {
        s->dir = FindFirstFile(s->path, &(s->fileinfo));
        if (INVALID_HANDLE_VALUE == s->dir)
            return -1;
        goto CheckFile;
    }

    while (1) {
        fFound = FindNextFile(s->dir, &(s->fileinfo));
        if (!fFound)
            return -1;
CheckFile:
        if (skip_matching_file(s->fileinfo.cFileName))
            continue;
        if (0 == fnmatch(s->pattern, s->fileinfo.cFileName, 0) ) {
            makepath(filename, filename_size_max, s->dirpath, s->fileinfo.cFileName);
            return 0;
        }
    }
#else
    struct dirent *dirent;
    const char *p;
    char *q;

    if (s->dir == NULL)
        goto redo;

    for (;;) {
        dirent = readdir(s->dir);
        if (dirent == NULL) {
        redo:
            if (s->dir) {
                closedir(s->dir);
                s->dir = NULL;
            }
            p = s->bufptr;
            if (*p == '\0')
                return -1;
            /* CG: get_str(&p, s->dirpath, sizeof(s->dirpath), ":") */
            q = s->dirpath;
            while (*p != ':' && *p != '\0') {
                if ((q - s->dirpath) < (int)sizeof(s->dirpath) - 1)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
            if (*p == ':')
                p++;
            s->bufptr = p;
            s->dir = opendir(s->dirpath);
            if (!s->dir)
                goto redo;
        } else {
            if (fnmatch(s->pattern, dirent->d_name, 0) == 0) {
                makepath(filename, filename_size_max,
                         s->dirpath, dirent->d_name);
                return 0;
            }
        }
    }
#endif
}

void find_file_close(FindFileState *s)
{
#ifdef WIN32
    if (INVALID_HANDLE_VALUE != s->dir)
       FindClose(s->dir);
#else
    if (s->dir)
        closedir(s->dir);
#endif
    free(s);
}

/* TODO: move to a separate file */
#define WIN_CLASS_NAME  "PDFTEST_PDF_WIN"
#define COL_WINDOW_BG RGB(0xff, 0xff, 0xff)

static HWND             gHwnd = NULL;
static HBRUSH           gBrushBg;
static BITMAPINFO *     gDibInfo = NULL;
static SplashBitmap *   gCurrBitmap = NULL;
static int              gBitmapDx = -1;
static int              gBitmapDy = -1;

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

void OnPaint(HWND hwnd)
{
    HDC             hdc;
    PAINTSTRUCT     ps;
    RECT            rc;
    int             bmpRowSize;
    SplashColorPtr  bmpData;

    hdc = BeginPaint(hwnd, &ps);
    SetBkMode(hdc, TRANSPARENT);
    GetClientRect(hwnd, &rc);

    bmpRowSize = gCurrBitmap->getRowSize();
    bmpData = gCurrBitmap->getDataPtr();

    gDibInfo->bmiHeader.biWidth = gBitmapDx;
    gDibInfo->bmiHeader.biHeight = -gBitmapDy;
    gDibInfo->bmiHeader.biSizeImage = gBitmapDy * bmpRowSize;

    FillRect(hdc, &ps.rcPaint, gBrushBg);
    DrawText (hdc, "Hello world", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER) ;

    SetDIBitsToDevice(hdc,
        0, /* destx */
        0, /* desty */
        gBitmapDx, /* destw */
        gBitmapDy, /* desth */
        0, /* srcx */
        0, /* srcy */
        0, /* startscan */
        gBitmapDy, /* numscans */
        bmpData, /* pBits */
        gDibInfo, /* pInfo */
        DIB_RGB_COLORS /* color use flag */
    );

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
            // do nothing
            break;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            /* it might happen that we get WM_PAINT after destroying a window */
            OnPaint(hwnd);
            break;

        case WM_DESTROY:
            /* WM_DESTROY might be sent as a result of File\Close, in which case CloseWindow() has already been called */
            break;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

BOOL RegisterWinClass(void)
{
    WNDCLASSEX  wcex;
    ATOM        atom;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = NULL;
    wcex.hIcon          = NULL;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = NULL;
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = WIN_CLASS_NAME;
    wcex.hIconSm        = NULL;

    atom = RegisterClassEx(&wcex);
    if (atom)
        return TRUE;
    return FALSE;
}

int InitWinIfNecessary(void)
{
    if (gHwnd)
        return TRUE;

    if (!RegisterWinClass())
        return FALSE;

    gBrushBg = CreateSolidBrush(COL_WINDOW_BG);

    gHwnd = CreateWindow(
        WIN_CLASS_NAME, "",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0,
        CW_USEDEFAULT, 0,
        NULL, NULL,
        NULL, NULL);

    if (!gHwnd)
        return FALSE;

    assert(!gDibInfo);
    gDibInfo = (BITMAPINFO*)malloc(sizeof(BITMAPINFO) + 12);
    if (!gDibInfo)
        return FALSE;
    gDibInfo->bmiHeader.biSize = sizeof(gDibInfo->bmiHeader);
    gDibInfo->bmiHeader.biPlanes = 1;
    gDibInfo->bmiHeader.biBitCount = 24;
    gDibInfo->bmiHeader.biCompression = BI_RGB;
    gDibInfo->bmiHeader.biXPelsPerMeter = 2834;
    gDibInfo->bmiHeader.biYPelsPerMeter = 2834;
    gDibInfo->bmiHeader.biClrUsed = 0;
    gDibInfo->bmiHeader.biClrImportant = 0;

    ShowWindow(gHwnd, SW_SHOW);
    return TRUE;
}

void PumpMessages(void)
{
    BOOL    isMessage;
    MSG     msg;

    for (;;) {
        isMessage = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
        if (!isMessage)
            return;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void PreviewBitmap_Init(void)
{
    /* no need to do anything */
}

void PreviewBitmap_Destroy(void)
{
    PostQuitMessage(0);
    PumpMessages();
    free(gDibInfo);
    gDibInfo = NULL;
    DeleteObject(gBrushBg);
}

void PreviewBitmap(SplashBitmap *bitmap)
{
    assert(bitmap);
    if (!bitmap)
        return;
    if (!InitWinIfNecessary())
        return;

    gCurrBitmap = bitmap;
    if ( (bitmap->getWidth() != gBitmapDx) ||
        (bitmap->getHeight() != gBitmapDy) ) {
        gBitmapDx = bitmap->getWidth();
        gBitmapDy = bitmap->getHeight();
        WinResizeClientArea(gHwnd, gBitmapDx, gBitmapDy);
    }
    assert(gHwnd);

    InvalidateRect(gHwnd, NULL, FALSE);
    UpdateWindow(gHwnd);

    PumpMessages();
}

typedef struct StrList {
    struct StrList *next;
    char *          str;
} StrList;

/* List of all command-line arguments that are not switches.
   We assume those are:
     - names of PDF files
     - names of a file with a list of PDF files
     - names of directories with PDF files
*/
static StrList *gArgsListRoot = NULL;

/* Names of all command-line switches we recognize */
#define TIMINGS_ARG         "-timings"
#define RESOLUTION_ARG      "-resolution"
#define RECURSIVE_ARG       "-recursive"
#define OUT_ARG             "-out"
#define ERROUT_ARG          "-errout"
#define PREVIEW_ARG         "-preview"

/* Should we record timings? True if -timings command-line argument was given. */
static int gfTimings = FALSE;

/* If true, we use render each page at resolution 'gResolutionX'/'gResolutionY'.
   If false, we render each page at its native resolution.
   True if -resolution NxM command-line argument was given. */
static int gfForceResolution = FALSE;
static int gResolutionX = 0;
static int gResolutionY = 0;
/* If NULL, we output the log info to stdout. If not NULL, should be a name
   of the file to which we output log info. 
   Controled by -out command-line argument. */
static char *   gOutFileName = NULL;
/* FILE * correspondig to gOutFileName or stdout if gOutFileName is NULL or 
   was invalid name */
static FILE *   gOutFile = NULL;
/* If NULL, we output the error log info to stderr. If not NULL, should be
   a name of the file to which we output error log info.
   Controlled by -err command-line argument */
static char *   gErrFileName = NULL;
/* FILE * correspondig to g_errFileName or stderr if gErrFileName is NULL or 
   was invalid name */
static FILE *   gErrFile = NULL;

/* If True and a directory is given as a command-line argument, we'll process
   pdf files in sub-directories as well.
   Controlled by -recursive command-line argument */
static int gfRecursive = FALSE;

/* If true, preview rendered image. To make sure that they're being rendered correctly. */
static int gfPreview = FALSE;

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

static SplashColorPtr  gBgColor = SPLASH_COL_WHITE_PTR;
static SplashColorMode gSplashColorMode = splashModeBGR8;

/* TODO: move to a separate file */
char *Str_DupN(char *str, size_t len)
{
    char *  str_new;
    if (!str)
        return NULL;

    str_new = (char*)malloc(len+1);
    if (!str_new)
        return NULL;

    memcpy(str_new, str, len);
    str_new[len] = 0;
    return str_new;
}

char *Str_Dup(char *str)
{
    size_t  len;
    if (!str)
        return NULL;

    len = strlen(str);
    return Str_DupN(str, len);
}

int Str_EqNoCase(const char *str1, const char *str2)
{
    if (!str1 && !str2)
        return TRUE;
    if (!str1 || !str2)
        return FALSE;
    if (0 == stricmp(str1, str2))
        return TRUE;
    return FALSE;
}

int Str_EndsWithNoCase(const char *txt, const char *end)
{
    size_t end_len;
    size_t txt_len;

    if (!txt || !end)
        return FALSE;

    txt_len = strlen(txt);
    end_len = strlen(end);
    if (end_len > txt_len)
        return FALSE;
    if (Str_EqNoCase(txt+txt_len-end_len, end))
        return TRUE;
    return FALSE;
}

int StrList_Len(StrList **root)
{
    int         len = 0;
    StrList *   cur;
    assert(root);
    if (!root)
        return 0;
    cur = *root;
    while (cur) {
        ++len;
        cur = cur->next;
    }
    return len;
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

BOOL StrList_Insert(StrList **root, char *txt)
{
    char *txtDup;

    assert(root && txt);
    if (!root || !txt)
        return FALSE;
    txtDup = Str_Dup(txt);
    if (!txtDup)
        return FALSE;

    if (!StrList_InsertAndOwn(root, txtDup)) {
        free((void*)txtDup);
        return FALSE;
    }
    return TRUE;
}

StrList* StrList_RemoveHead(StrList **root)
{
    StrList *tmp;
    assert(root);
    if (!root)
        return NULL;

    if (!*root)
        return NULL;
    tmp = *root;
    *root = tmp->next;
    tmp->next = NULL;
    return tmp;
}

void StrList_FreeElement(StrList *el)
{
    if (!el)
        return;
    free((void*)el->str);
    free((void*)el);
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
        StrList_FreeElement(cur);
        cur = next;
    }
    *root = NULL;
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
        OutputDebugString(buf);
        fprintf(gErrFile, buf);
    } else {
        OutputDebugString("Error: ");
        fprintf(gErrFile, "Error: ");
    }

    p = buf;
    va_start(args, msg);
    p += _vsnprintf(p, sizeof(buf) - 3, msg, args);
    while ( p > buf  &&  isspace(p[-1]) )
            *--p = '\0';
    *p++ = '\r';
    *p++ = '\n';
    *p   = '\0';
    OutputDebugString(buf);
    fprintf(gErrFile, buf);
    va_end(args);
}

void LogInfo(char *fmt, ...)
{
    va_list args;
    char        buf[4096], *p = buf;

    p = buf;
    va_start(args, fmt);
    p += _vsnprintf(p, sizeof(buf) - 1, fmt, args);
    *p   = '\0';
    fprintf(gOutFile, buf);

    va_end(args);
}

static void PrintUsageAndExit(void)
{
    printf("Usage: pdftest [-preview] [-timings] [-resolution 100x200] [-recursive] [-out out.txt] [-errout errout.txt] pdf-files-to-process\n");
    exit(0);
}

/* milli-second timer */
#ifdef WIN32
typedef struct MsTimer {
    LARGE_INTEGER   start;
    LARGE_INTEGER   end;
} MsTimer;

void MsTimer_Start(MsTimer *timer) 
{
    assert(timer);
    if (!timer)
        return;
    QueryPerformanceCounter(&timer->start);
}
void MsTimer_End(MsTimer *timer)
{
    assert(timer);
    if (!timer)
        return;
    QueryPerformanceCounter(&timer->end);
}

double MsTimer_GetTimeInMs(MsTimer *timer)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    double durationInSecs = (double)(timer->end.QuadPart-timer->start.QuadPart)/(double)freq.QuadPart;
    return durationInSecs * 1000.0;
}

#else
#include <time.h>

typedef struct MsTimer {
    struct timeval    start;
    struct timeval    end;
} MsTimer;

void MsTimer_Start(MsTimer *timer) 
{
    assert(timer);
    if (!timer)
        return;
    gettimeofday(&timer->start, NULL);
}

void MsTimer_End(MsTimer *timer)
{
    assert(timer);
    if (!timer)
        return;
    gettimeofday(&timer->end, NULL);
}

double MsTimer_GetTimeInMs(MsTimer *timer)
{
    double timeInMs;
    time_t seconds;
    int    usecs;

    assert(timer);
    if (!timer)
        return 0.0;
    /* TODO: this logic needs to be verified */
    seconds = timer->end.tv_sec - timer->start.tv_sec;
    usecs = timer->end.tv_usec - timer->start.tv_usec;
    if (usecs < 0) {
        --seconds;
        usecs += 1000000;
    timeInMs = (double)usecs*(double)1000.0 + (double)usecs/(double)1000.0;
    return timeInMs;
}
#endif

#define SCREEN_DPI 72

/* Render one pdf file with a given 'fileName'. Log apropriate info. */
static void RenderPdfFile(const char *fileName)
{
    MsTimer     msTimer;
    double      timeInMs;
    int         pageCount;
    int         pageDx, pageDy;
    int         renderDx, renderDy;
    double      hDPI, vDPI;
    int         rotate;
    GBool       useMediaBox;
    GBool       crop;
    GBool       doLinks;
    GooString * fileNameStr = NULL;
    GooString * ownerPasswordStr = NULL;
    GooString * userPasswordStr = NULL;
    PDFDoc *            pdfDoc = NULL;
    SplashOutputDev *   outputDevice = NULL;
    SplashBitmap *      bitmap = NULL;
    assert(fileName);
    if (!fileName)
        return;

    LogInfo("started: %s\n", fileName);

    /* note: don't delete fileNameStr, ownerPasswordStr and userPasswordStr
       since PDFDoc takes ownership and deletes them itself */
    fileNameStr = new GooString(fileName);
    if (!fileNameStr)
        goto Exit;

    ownerPasswordStr = new GooString("");
    if (!ownerPasswordStr)
        goto Exit;

    userPasswordStr = new GooString("");
    if (!userPasswordStr)
        goto Exit;

    outputDevice = new SplashOutputDev(gSplashColorMode, 4, gFalse, gBgColor);
    if (!outputDevice) {
        error(-1, "renderPdfFile(): failed to create outputDev\n");
        goto Exit;
    }

    MsTimer_Start(&msTimer);
    pdfDoc = new PDFDoc(fileNameStr, ownerPasswordStr, userPasswordStr, NULL);
    if (!pdfDoc->isOk()) {
        error(-1, "renderPdfFile(): failed to open PDF file %s\n", fileName);
        goto Exit;
    }
    outputDevice->startDoc(pdfDoc->getXRef());

    MsTimer_End(&msTimer);
    timeInMs = MsTimer_GetTimeInMs(&msTimer);
    LogInfo("load: %.2f ms\n", timeInMs);

    pageCount = pdfDoc->getNumPages();
    LogInfo("page count: %d\n", pageCount);

    for (int curPage = 1; curPage <= pageCount; curPage++) {
        pageDx = pdfDoc->getPageCropWidth(curPage);
        pageDy = pdfDoc->getPageCropHeight(curPage);

        renderDx = pageDx;
        renderDy = pageDy;
        if (gfForceResolution) {
            renderDx = gResolutionX;
            renderDy = gResolutionY;
        }
        MsTimer_Start(&msTimer);
        rotate = 0;
        useMediaBox = gFalse;
        crop = gTrue;
        doLinks = gTrue;
        hDPI = (double)SCREEN_DPI;
        vDPI = (double)SCREEN_DPI;
        pdfDoc->displayPage(outputDevice, curPage, hDPI, vDPI, rotate, useMediaBox, crop, doLinks);
        MsTimer_End(&msTimer);
        timeInMs = MsTimer_GetTimeInMs(&msTimer);
        if (gfTimings)
            LogInfo("page %d: %.2f ms\n", curPage, timeInMs);
        if (gfPreview) {
            delete bitmap;
            bitmap = outputDevice->takeBitmap();
            PreviewBitmap(bitmap);
        }
    }
Exit:
    LogInfo("finished: %s\n", fileName);    
    delete bitmap;
    delete outputDevice;
    delete pdfDoc;
}

/* Given 'resolutionString' in format NxM (e.g. "100x200"), parse the string and put N
   into 'resolutionXOut' and M into 'resolutionYOut'.
   Return FALSE if there was an error (e.g. string is not in the right format */
int ParseResolutionString(char *resolutionString, int *resolutionXOut, int *resolutionYOut)
{
    assert(resolutionString);
    assert(resolutionXOut);
    assert(resolutionYOut);
    if (!resolutionString || !resolutionXOut || !resolutionYOut)
        return FALSE;
    /* TODO: parse resolution string and return TRUE*/
    *resolutionXOut = 640;
    *resolutionYOut = 480;
    return FALSE;
}

void ParseCommandLine(int argc, char **argv)
{
    char *      arg;

    if (argc < 2)
        PrintUsageAndExit();
    for (int i=1; i < argc; i++) {
        arg = argv[i];
        assert(arg);
        if ('-' == arg[0]) {
            if (Str_EqNoCase(arg, TIMINGS_ARG)) {
                gfTimings = TRUE;
            } else if (Str_EqNoCase(arg, RESOLUTION_ARG)) {
                ++i;
                if (i == argc)
                    PrintUsageAndExit(); /* expect a file name after that */
                if (!ParseResolutionString(argv[i], &gResolutionX, &gResolutionY))
                    PrintUsageAndExit();
                gfForceResolution = TRUE;
            } else if (Str_EqNoCase(arg, RECURSIVE_ARG)) {
                gfRecursive = TRUE;
            } else if (Str_EqNoCase(arg, OUT_ARG)) {
                ++i;
                if (i == argc)
                    PrintUsageAndExit(); /* expect a file name after that */
                gOutFileName = Str_Dup(argv[i]);
            } else if (Str_EqNoCase(arg, ERROUT_ARG)) {
                ++i;
                if (i == argc)
                    PrintUsageAndExit(); /* expect a file name after that */
                gErrFileName = Str_Dup(argv[i]);
            } else if (Str_EqNoCase(arg, PREVIEW_ARG)) {
                gfPreview = TRUE;
            } else {
                /* unknown option */
                PrintUsageAndExit();
            }
        } else {
            /* we assume that this is not an option hence it must be
               a name of PDF/directory/file with PDF names */
            StrList_Insert(&gArgsListRoot, arg);
        }
    }
}

void RenderPdfFileList(char *pdfFileList)
{
    /* TODO: implement me */

}

#ifdef WIN32
#include <sys/types.h>
#include <sys/stat.h>

int IsDirectoryName(char *path)
{
    struct _stat    buf;
    int             result;

    result = _stat(path, &buf );
    if (0 != result)
        return FALSE;

    if (buf.st_mode & _S_IFDIR)
        return TRUE;

    return FALSE;
}

int IsFileName(char *path)
{
    struct _stat    buf;
    int             result;

    result = _stat(path, &buf );
    if (0 != result)
        return FALSE;

    if (buf.st_mode & _S_IFREG)
        return TRUE;

    return FALSE;
}
#else
int IsDirectoryName(char *path)
{
    /* TODO: implement me */
    return FALSE;
}

int IsFileName(char *path)
{
    /* TODO: implement me */
    return TRUE;
}
#endif

int IsPdfFileName(char *path)
{
    if (Str_EndsWithNoCase(path, ".pdf"))
        return TRUE;
    return FALSE;
}

void RenderDirectory(char *path)
{
    FindFileState * ffs;
    char            filename[MAX_FILENAME_SIZE];
    StrList *       dirList = NULL;
    StrList *       el;

    StrList_Insert(&dirList, path);

    while (0 != StrList_Len(&dirList)) {
        el = StrList_RemoveHead(&dirList);
        ffs = find_file_open(el->str, "*");
        while (!find_file_next(ffs, filename, sizeof(filename))) {
            if (IsDirectoryName(filename)) {
                if (gfRecursive) {
                    StrList_Insert(&dirList, filename);
                }
            } else if (IsFileName(filename)) {
                if (IsPdfFileName(filename)) {
                    RenderPdfFile(filename);
                }
            }
        }
        find_file_close(ffs);
        StrList_FreeElement(el);
    }
    StrList_Destroy(&dirList);
}

/* Render 'cmdLineArg', which can be:
   - directory name
   - name of PDF file
   - name of text file with names of PDF files
*/
void RenderCmdLineArg(char *cmdLineArg)
{
    assert(cmdLineArg);
    if (!cmdLineArg)
        return;
    if (IsDirectoryName(cmdLineArg)) {
        RenderDirectory(cmdLineArg);
    } else if (IsFileName(cmdLineArg)) {
        if (IsPdfFileName(cmdLineArg))
            RenderPdfFile(cmdLineArg);
        else
            RenderPdfFileList(cmdLineArg);
    } else {
        error(-1, "unexpected argument '%s'", cmdLineArg);
    }
}

int main(int argc, char **argv)
{
    StrList *       curr;

    ParseCommandLine(argc, argv);
    if (0 == StrList_Len(&gArgsListRoot))
        PrintUsageAndExit();
    assert(gArgsListRoot);
    ColorsInit();
    globalParams = new GlobalParams("");
    if (!globalParams)
        return 1;
    globalParams->setErrQuiet(gFalse);

    /* TODO: set to gOutFileName/g_errFileName if given */
    gOutFile = stdout;
    gErrFile = stderr;

    PreviewBitmap_Init();

    curr = gArgsListRoot;
    while (curr) {
        RenderCmdLineArg(curr->str);
        curr = curr->next;
    }
    PreviewBitmap_Destroy();
    StrList_Destroy(&gArgsListRoot);
    return 0;
}
