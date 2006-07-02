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

/* TODO: move to a separate file */
void PreviewBitmap(SplashBitmap *bitmap)
{
    /* dummy preview functions, does nothing */
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
static int gfPreview = TRUE;

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

int Str_EqNoCase(char *str1, char *str2)
{
    if (!str1 && !str2)
        return TRUE;
    if (!str1 || !str2)
        return FALSE;
    if (0 == stricmp(str1, str2))
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
        error(0, "renderPdfFile(): failed to create outputDev\n");
        goto Exit;
    }

    MsTimer_Start(&msTimer);
    pdfDoc = new PDFDoc(fileNameStr, ownerPasswordStr, userPasswordStr, NULL);
    if (!pdfDoc->isOk()) {
        error(0, "renderPdfFile(): failed to open PDF file %s\n", fileName);
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

#define TEST_PDF_FILE_NAME "C:\\kjk\\downloads\\curve25519-20051115.pdf"

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
        arg = argv[0];
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

int main(int argc, char **argv)
{
    ParseCommandLine(argc, argv);
    if (0 == StrList_Len(&gArgsListRoot))
        PrintUsageAndExit();

    ColorsInit();
    globalParams = new GlobalParams("");
    if (!globalParams)
        return 1;
    globalParams->setErrQuiet(gFalse);

    /* TODO: set to gOutFileName/g_errFileName if given */
    gOutFile = stdout;
    gErrFile = stderr;

    RenderPdfFile(TEST_PDF_FILE_NAME);
    StrList_Destroy(&gArgsListRoot);
    return 0;
}
