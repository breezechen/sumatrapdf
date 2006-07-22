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
#include "strlist_util.h"
#include "file_util.h"

#include "SimpleRect.h"
#include "DisplayModel.h"
#include "BaseUtils.h"

/* Next action for the benchmark mode */
#define MSG_BENCH_NEXT_ACTION WM_USER + 1

/* A long TODO list:

Must have before first release:

Nice to have:
- remove dependency on PDFCore/WinPDFCore
- much better "About" box
- bug: window size slightly changes at startup, don't know why
- continuous-mode
- access encrypted files
- handle links (actions)
- remember the history of opened files (infinite "Recent Files"). Remember file name and
  UI state (windows posisition, current page, zoom level, scroll position) at the time
  file was close. Restore the UI state when file is re-opened. That way user gets where
  he left off, which is probably what he wants most of the time. Might want to make it
  user-configurable option (not sure if that's what all of the people would want).
  History is infinite (as opposed to only N recently opened, as in most software).
- good UI for history of opened files. At the very least a type-down list, ordered by
  last access time. Type-down filters by file name (or maybe full path?). Filter out
  files that don't exist anymore (and remove them from history)
- session saving (similar to what a Firefox extension does). Save the whole state of the app (windows
  position, which PDF file is open, current page, zoom level, scroll position etc.). Restore the
  session when called without a command-line argument.
- Ctrl-G - "go to page" dialog
- cursor up/down and page up/down should advance pages if at the top/bottom
- check for new version on-line
- toolbar (?)
- easy toggle to full-screen mode (Ctrl-L ?)
- UI for table of contents
- bookmarks per PDF, persisted across sessions. UI for handling bookmarks (add bookmark, delete
  bookmark, go to bookmark). 2 cases need to be handled: going to a bookmark in the currently
  displayed PDF file. Going to a "global" bookmark, across all PDF files (i.e. show a list
  of all bookmarks in all PDF files, grouped by PDF file). Those 2 cases need separate UI.
- "launch with acrobat" option (menu item?), so that it's easy to launch a doc
  with acrobat if it implements an important function that I don't (e.g. priting)
  Needs to read registry to find where acrobat is installed.
- easy toggle of full-screen mode (Ctrl-L ?)
- detect if *pdf files are associsated with me at startup, if not, either silently
  restore or have a dialog box
- printing
- get rid of dependency on freetype, use windows functions instead (not sure if that's
  actually possible with poppler)
- optimize the use of embedded fonts, currently they're being written out
  to disk and then read into memory, which seems completely unnecessary since
  freetype can create a face directly from memory.
  Also, it uses char-by-char IO, which is slow, switch to reading/writing in
  chunks.
- nicer-looking window
 - drop shadows
 - custom caption area
- replace windows font scanning code with a better version from mupdf

BUGS:
- C:\kjk\downloads\6.05.HowToBeCreative.pdf popup on every page
*/

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
//#define COL_WINDOW_BG RGB(0xcc, 0xcc, 0xcc)
#define COL_WINDOW_BG RGB(0xff, 0xff, 0xff)
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

#ifdef DEBUG
void __cdecl DBG_OUT(const char *format, ...) {
    char        buf[4096], *p = buf;
    va_list     args;

    va_start(args, format);

    p += _vsnprintf(p, sizeof(buf) - 1, format, args);
    while ( (p > buf)  &&  isspace(p[-1]) )
            *--p = '\0';
    *p++ = '\r';
    *p++ = '\n';
    *p   = '\0';
    OutputDebugString(buf);

    va_end(args);
}
#else
#define DBG_OUT
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

static inline BOOL isDir(WIN32_FIND_DATA *fileData)
{
    if (fileData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return TRUE;
    return FALSE;
}

#define SWAPWORD(x)     MAKEWORD(HIBYTE(x), LOBYTE(x))
#define SWAPLONG(x)     MAKELONG(SWAPWORD(HIWORD(x)), SWAPWORD(LOWORD(x)))

#define PLATFORM_UNICODE                                0
#define PLATFORM_MACINTOSH                                1
#define PLATFORM_ISO                                        2
#define PLATFORM_MICROSOFT                                3

#define UNI_ENC_UNI_1                                        0
#define UNI_ENC_UNI_1_1                                        1
#define UNI_ENC_ISO                                                2
#define UNI_ENC_UNI_2_BMP                                3
#define UNI_ENC_UNI_2_FULL_REPERTOIRE        4

#define MAC_ROMAN                                                0
#define MAC_JAPANESE                                        1
#define MAC_CHINESE_TRADITIONAL                        2
#define MAC_KOREAN                                                3
#define MAC_CHINESE_SIMPLIFIED                        25

#define MS_ENC_SYMBOL                                        0
#define MS_ENC_UNI_BMP                                        1
#define MS_ENC_SHIFTJIS                                        2
#define MS_ENC_PRC                                                3
#define MS_ENC_BIG5                                                4
#define MS_ENC_WANSUNG                                        5
#define MS_ENC_JOHAB                                        6
#define MS_ENC_UNI_FULL_REPETOIRE                10

typedef struct _tagTT_OFFSET_TABLE
{
    USHORT  uMajorVersion;
    USHORT  uMinorVersion;
    USHORT  uNumOfTables;
    USHORT  uSearchRange;
    USHORT  uEntrySelector;
    USHORT  uRangeShift;
    } TT_OFFSET_TABLE;

    typedef struct _tagTT_TABLE_DIRECTORY
    {
    char    szTag[4];                        //table name
    ULONG   uCheckSum;                        //Check sum
    ULONG   uOffset;                        //Offset from beginning of file
    ULONG   uLength;                        //length of the table in bytes
    } TT_TABLE_DIRECTORY;

    typedef struct _tagTT_NAME_TABLE_HEADER
    {
    USHORT  uFSelector;                        //format selector. Always 0
    USHORT  uNRCount;                        //Name Records count
    USHORT  uStorageOffset;                //Offset for strings storage, from start of the table
    } TT_NAME_TABLE_HEADER;

    typedef struct _tagTT_NAME_RECORD
    {
    USHORT  uPlatformID;
    USHORT  uEncodingID;
    USHORT  uLanguageID;
    USHORT  uNameID;
    USHORT  uStringLength;
    USHORT  uStringOffset;        //from start of storage area
    } TT_NAME_RECORD;

    typedef struct _tagFONT_COLLECTION
    {
    char    Tag[4];
    ULONG   Version;
    ULONG   NumFonts;
}FONT_COLLECTION;

typedef struct FontMapping {
    struct FontMapping *next;
    char *name;
    char *path;
    int index;
} FontMapping;

FontMapping *g_fontMapMSList = NULL;

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

BOOL streq(char *str1, char *str2)
{
    if (!str1 && !str2)
        return TRUE;
    if (!str1 || !str2)
        return FALSE;
    if (0 == strcmp(str1, str2))
        return TRUE;
    return FALSE;
}

BOOL strieq(char *str1, char *str2)
{
    if (!str1 && !str2)
        return TRUE;
    if (!str1 || !str2)
        return FALSE;
    if (0 == stricmp(str1, str2))
        return TRUE;
    return FALSE;
}

FontMapping *FontMapping_Find(FontMapping **root, char *facename, int index)
{
    FontMapping *cur;
    assert(root);
    if (!root)
        return NULL;
    if (!facename)
        return NULL;
    cur = *root;
    while (cur) {
        if ((index == cur->index) && (streq(facename, cur->name)))
            return cur;
        cur = cur->next;
    }
    return NULL;
}

BOOL FontMapping_Exists(FontMapping **root, char *facename, int index)
{
    if (FontMapping_Find(root, facename, index))
        return TRUE;
    return FALSE;
}

void FontMapping_Destroy(FontMapping *entry)
{
    if (entry) {
        free((void*)entry->name);
        free((void*)entry->path);
    }
    free((void*)entry);
}

FontMapping *FontMapping_Create(char *name, char *path, int index)
{
    FontMapping *entry;

    if (!name || !path)
        return NULL;

    entry = (FontMapping*)malloc(sizeof(FontMapping));
    if (!entry)
        return NULL;

    entry->name = StrDup(name);
    if (!entry->name)
        goto Error;
    entry->path = StrDup(path);
    if (!entry->path)
        goto Error;
    entry->index = index;
    return entry;
Error:
    FontMapping_Destroy(entry);
    return NULL;
}

void FontMappingList_Destroy(FontMapping **root)
{
    FontMapping *cur, *next;
    if (!root)
        return;
    cur = *root;
    while (cur) {
        next = cur->next;
        FontMapping_Destroy(cur);
        cur = next;
    }
}

void
FontMappingList_InsertUnique(FontMapping **root, char *facename, char *path, int index)
{
    /* TODO: better error handling */
    if (FontMapping_Exists(root, facename, index))
        return;

    FontMapping *entry = FontMapping_Create(facename, path, index);
    if (!entry)
        return;

    entry->next = *root;
    *root = entry;
    return;
}

void FontMappingList_Dump(FontMapping **root)
{
    FontMapping *cur;
    if (!root)
        return;
    cur = *root;
    while (cur) {
        DBG_OUT("name=%s, path=%s\n", cur->name, cur->path);
        cur = cur->next;
    }
}

BOOL freadsafe(FILE *file, void *buf, int size)
{
    int byteread;
    while (size > 0)
    {
        byteread = fread(buf, 1, size, file);
        if (byteread < 0)
            return FALSE;
        assert(byteread <= size);
        size -= byteread;
    }
    return TRUE;
}

BOOL
swapword(char* pbyte, int nLen)
{
    int     i;
    char    tmp;
    int     nMax;

    if (nLen % 2)
        return FALSE;

    nMax = nLen / 2;
    for(i = 0; i < nLen; ++i) {
        tmp = pbyte[i*2];
        pbyte[i*2] = pbyte[i*2+1];
        pbyte[i*2+1] = tmp;
    }
    return TRUE;
}

/* pSouce and PDest can be same */
BOOL
decodeunicodeBMP(char* source, int sourcelen,char* dest, int destlen)
{
    wchar_t tmp[1024*2];
    int converted;
    memset(tmp,0,sizeof(tmp));
    memcpy(tmp,source,sourcelen);
    swapword((char*)tmp,sourcelen);

    converted = WideCharToMultiByte(CP_ACP, 0, tmp,
        -1, dest, destlen, NULL, NULL);

    if(converted == 0)
        return FALSE;

    return TRUE;
}

BOOL
decodeunicodeplatform(char* source, int sourcelen,char* dest, int destlen, int enctype)
{
    switch (enctype)
    {
        case UNI_ENC_UNI_1:
        case UNI_ENC_UNI_2_BMP:
            return decodeunicodeBMP(source,sourcelen,dest,destlen);
        case UNI_ENC_UNI_2_FULL_REPERTOIRE:
        case UNI_ENC_UNI_1_1:
        case UNI_ENC_ISO:
        default:
            return FALSE;
    }
}

BOOL
decodemacintoshplatform(char* source, int sourcelen,char* dest, int destlen, int enctype)
{
    if (MAC_ROMAN != enctype)
        return FALSE;

    if (sourcelen + 1 > destlen)
            return FALSE;
    memcpy(source,dest,sourcelen);
    dest[sourcelen] = 0;
    return TRUE;
}

BOOL
decodemicrosoftplatform(char* source, int sourcelen,char* dest, int destlen, int enctype)
{
    switch (enctype)
    {
        case MS_ENC_SYMBOL:
        case MS_ENC_UNI_BMP:
            return decodeunicodeBMP(source,sourcelen,dest,destlen);
    }
    return FALSE;
    }

/* TODO: proper cleanup on file errors */
void TTFFontFileParse(char *path, int offset, int index)
{
    FILE *              file;
    TT_OFFSET_TABLE     ttOffsetTable;
    TT_TABLE_DIRECTORY  tblDir;
    TT_NAME_TABLE_HEADER ttNTHeader;
    TT_NAME_RECORD      ttRecord;
    char                szTemp[4096];
    int                 found;
    int                 i;
    BOOL                fOk;

    file = fopen(path, "rb");
    if (NULL == file)
        return;
    fseek(file, offset, SEEK_SET);
    if (!freadsafe(file, &ttOffsetTable, sizeof(TT_OFFSET_TABLE)))
        goto Error;

    ttOffsetTable.uNumOfTables = SWAPWORD(ttOffsetTable.uNumOfTables);
    ttOffsetTable.uMajorVersion = SWAPWORD(ttOffsetTable.uMajorVersion);
    ttOffsetTable.uMinorVersion = SWAPWORD(ttOffsetTable.uMinorVersion);

    //check is this is a true type font and the version is 1.0
    if (ttOffsetTable.uMajorVersion != 1 || ttOffsetTable.uMinorVersion != 0)
        goto Error;

    found = 0;
    for (i = 0; i < ttOffsetTable.uNumOfTables; i++)
    {
        if (!freadsafe(file,&tblDir,sizeof(TT_TABLE_DIRECTORY)))
            return;

        memcpy(szTemp, tblDir.szTag, 4);
        szTemp[4] = 0;

        if (stricmp(szTemp, "name") == 0)
        {
            found = 1;
            tblDir.uLength = SWAPLONG(tblDir.uLength);
            tblDir.uOffset = SWAPLONG(tblDir.uOffset);
            break;
        }
        else if (szTemp[0] == 0)
            break;
    }

    if (!found)
        goto Error;

    fseek(file, tblDir.uOffset, SEEK_SET);

    if (!freadsafe(file, &ttNTHeader, sizeof(TT_NAME_TABLE_HEADER)))
        goto Error;

    ttNTHeader.uNRCount = SWAPWORD(ttNTHeader.uNRCount);
    ttNTHeader.uStorageOffset = SWAPWORD(ttNTHeader.uStorageOffset);

    offset = tblDir.uOffset + sizeof(TT_NAME_TABLE_HEADER);

    for (i = 0; i < ttNTHeader.uNRCount; ++i)
    {
        fseek(file, offset + sizeof(TT_NAME_RECORD)*i, SEEK_SET);
        if (!freadsafe(file, &ttRecord, sizeof(TT_NAME_RECORD)))
            goto Error;

        ttRecord.uNameID = SWAPWORD(ttRecord.uNameID);
        ttRecord.uLanguageID = SWAPWORD(ttRecord.uLanguageID);

        // Full Name
        if (ttRecord.uNameID == 6)
        {
            ttRecord.uPlatformID = SWAPWORD(ttRecord.uPlatformID);
            ttRecord.uEncodingID = SWAPWORD(ttRecord.uEncodingID);
            ttRecord.uStringLength = SWAPWORD(ttRecord.uStringLength);
            ttRecord.uStringOffset = SWAPWORD(ttRecord.uStringOffset);

            fseek(file, tblDir.uOffset + ttRecord.uStringOffset + ttNTHeader.uStorageOffset, SEEK_SET);
            if (!freadsafe(file, szTemp, ttRecord.uStringLength))
                goto Error;

            switch(ttRecord.uPlatformID)
            {
                case PLATFORM_UNICODE:
                    fOk = decodeunicodeplatform(szTemp, ttRecord.uStringLength,
                        szTemp, sizeof(szTemp), ttRecord.uEncodingID);
                    break;

                case PLATFORM_MACINTOSH:
                    fOk = decodemacintoshplatform(szTemp, ttRecord.uStringLength,
                        szTemp, sizeof(szTemp), ttRecord.uEncodingID);
                    break;

                case PLATFORM_ISO:
                    goto Error;

                case PLATFORM_MICROSOFT:
                    fOk = decodemicrosoftplatform(szTemp, ttRecord.uStringLength,
                        szTemp, sizeof(szTemp), ttRecord.uEncodingID);
                    break;
            }

            if (fOk)
                FontMappingList_InsertUnique(&g_fontMapMSList, szTemp, path, index);
        }
    }
Error:
    if (file)
        fclose(file);
}

void TTCFontFileParse(char *filePath)
{

}

/* Get a list of windows fonts.
   TODO: it should cache the data on disk and don't rescan font directory unless
   timestamp on last modified time on directory has changed */
void WinFontList_Create(void)
{
    HRESULT hr;
    char    fontDir[MAX_PATH*2];
    char    searchPattern[MAX_PATH*2];
    char    file[MAX_PATH*2];
    HANDLE  h;
    WIN32_FIND_DATA fileData;

    GetWindowsDirectory(fontDir, sizeof(fontDir));
    hr = StringCchCat(fontDir, dimof(fontDir), "\\Fonts\\");
    assert(S_OK == hr);
    hr = StringCchPrintf(searchPattern, sizeof(searchPattern), "%s*.tt?", fontDir);
    assert(S_OK == hr);

    h = FindFirstFile(searchPattern, &fileData);
    if (h == INVALID_HANDLE_VALUE)
    {
        /* most likely no font directory at all - not very good */
        assert(0);
        return;
    }

    for (;;)
    {
        if (!isDir(&fileData))
        {
            hr = StringCchPrintf(file, dimof(file), "%s%s", fontDir, fileData.cFileName);
            assert(S_OK == hr);
            if ( ('c' == file[strlen(file)-1]) ||
                 ('C' == file[strlen(file)-1]))
            {
                TTCFontFileParse(file);
            }
            else if ( ('f' == file[strlen(file)-1]) ||
                      ('F' == file[strlen(file)-1]) )
            {
                TTFFontFileParse(file, 0, 0);
            }
        }

        if (!FindNextFile(h, &fileData))
        {
            if (ERROR_NO_MORE_FILES == GetLastError())
                break;
        }
    }
}

void WinFontList_Destroy(void)
{
    FontMappingList_Destroy(&g_fontMapMSList);
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

static void WindowInfo_UpdateSize(WindowInfo *win)
{
    RECT  rc;
    GetClientRect(win->hwnd, &rc);
    win->winDx = RectDx(&rc);
    win->winDy = RectDy(&rc);
    /* TODO: get rid of bmp_dx and bmp_dy ? */
#if 0
    win->bmpDx = win->winDx;
    win->bmpDy = win->winDy;
#endif
    // DBG_OUT("WindowInfo_UpdateSize() win_dx=%d, win_dy=%d\n", win->win_dx, win->win_dy);
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
    WindowInfo_UpdateSize(win);
    if (!gUseDoubleBuffer || (0 == win->winDx) || (0 == win->winDy))
        return TRUE;

    win->hdcDoubleBuffer = CreateCompatibleDC(win->hdc);
    if (!win->hdcDoubleBuffer)
        return FALSE;

    //win->bmpDoubleBuffer = CreateCompatibleBitmap(win->hdc, win->bmpDx, win->bmpDy);
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
        //BitBlt(hdc, 0, 0, win->bmpDx, win->bmpDy, win->hdcDoubleBuffer, 0, 0, SRCCOPY);
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

static WindowInfo* OpenPdf(const TCHAR *file_name, BOOL close_invalid_files)
{
    int             err;
    WindowInfo *    win;
    GooString *     file_name_str = NULL;
    int             reuse_existing_window = FALSE;
    PDFDoc *        pdfDoc;
    RectDSize       totalDrawAreaSize;
    int             scrollbarDx, scrollbarDy;
    SplashOutputDev *outputDev = NULL;

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
    pdfDoc = new PDFDoc(file_name_str, NULL, NULL, NULL);
    if (!pdfDoc->isOk())
    {
        err = errOpenFile;
        error(-1, "LoadPdf(): failed to open PDF file %s\n", file_name_str);
    }

    if (close_invalid_files && (errNone != err) && !reuse_existing_window)
    {
        WindowInfo_Delete(win);
        return NULL;
    }

    outputDev = new SplashOutputDev(gSplashColorMode, 4, gFalse, gBgColor);
    if (!outputDev)
        return NULL; /* TODO: probably should WindowInfo_Delete() using the same logic as above */

    WindowInfo_UpdateSize(win);

    totalDrawAreaSize.dx = (double)win->winDx;
    totalDrawAreaSize.dy = (double)win->winDy;
    scrollbarDx = 0;
    scrollbarDy = 0; /* TODO: doesn't matter yet, but fix me anyway */
    win->dm = DisplayModel_CreateFromPdfDoc(pdfDoc, outputDev, totalDrawAreaSize,
        scrollbarDx, scrollbarDx, DM_CONTINUOUS, 1);
    if (!win->dm) {
        delete outputDev;
        WindowInfo_Delete(win);
        return NULL;
    }

    win->dm->appData = (void*)win;
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
        DBG_OUT("failed to load file %s, error=%d\n", file_name, err);
    } else {
        win->state = WS_SHOWING_PDF;
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
    WindowInfo *win;

    assert(dm);
    if (!dm) return;

    win = (WindowInfo*)dm->appData;
    assert(win);
    if (!win) return;

    /* TODO: write me */
}

#if 0
void WinPDFCore::updateScrollbars()
{
    SCROLLINFO si = {0};

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;

    int maxPosDx = 1;
    if (pages->getLength() > 0) {
        if (continuousMode) {
            maxPosDx = maxPageW;
        } else {
            maxPosDx = ((PDFCorePage *)pages->get(0))->w;
        }
    }

    if (maxPosDx < drawAreaWidth) {
        maxPosDx = drawAreaWidth;
    }

    int maxPosDy = 1;
    if (pages->getLength() > 0) {
        if (continuousMode) {
            maxPosDy = totalDocH;
        } else {
            maxPosDy = ((PDFCorePage *)pages->get(0))->h;
        }
    }

    if (maxPosDy < drawAreaHeight) {
        maxPosDy = drawAreaHeight;
    }

    si.nPos = scrollX;
    si.nMin = 0;
    si.nMax = maxPosDx;
    si.nPage = drawAreaWidth;
    SetScrollInfo(win->hwnd, SB_HORZ, &si, TRUE);

    si.nPos = scrollY;
    si.nMin = 0;
    si.nMax = maxPosDy;
    si.nPage = drawAreaHeight;
    SetScrollInfo(win->hwnd, SB_VERT, &si, TRUE);
}
#endif

#if 0
void WinPDFCore::resizeToWindow(WindowInfo *win)
{
    RECT    rc;
    int     newDx, newDy;
    int     sx, sy;

    assert(this->win == win);

    GetClientRect(win->hwnd, &rc);
    newDx = RectDx(&rc);

    if (VS_AMIGA == gVisualStyle)
        newDy = RectDy(&rc) - CAPTION_DY - (BORDER_TOP + BORDER_BOTTOM);
    else
        newDy = RectDy(&rc) - (BORDER_TOP + BORDER_BOTTOM);

    if ((drawAreaWidth == newDx) && (drawAreaHeight == newDy))
        return;

    drawAreaWidth = newDx;
    drawAreaHeight = newDy;

    if ((zoomPage == zoom) || (zoomWidth == zoom)) {
        sx = sy = -1;
    } else {
        sx = scrollX;
        sy = scrollY;
    }

    update(topPage, sx, sy, zoom, rotate, gTrue, gFalse);
}
#endif

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

#if 0
void WinPDFCore::toggleZoom(void)
{
    bool redisplay = false;

    if (zoomPage == getZoom())
    {
        zoom = zoomWidth;
        redisplay = true;
    }
    else if (zoomWidth == getZoom())
    {
        zoom = zoomPage;
        redisplay = true;
    }

    if (redisplay) {
        displayPage(topPage, zoom, 0, gTrue, gFalse);
    }
}
#endif

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
        OpenPdf(filename, TRUE);
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
//    win->pdfCore->resizeToWindow(win);
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

#if 0
void WinPDFCore::redrawRect(PDFCoreTile *tileA, int xSrc, int ySrc,
                            int xDest, int yDest, int width, int height)
{
    SplashBitmap *        splashBmp = NULL;
    int                   splashBmpDx, splashBmpDy;
    SplashColorMode       splashBmpColorMode;
    SplashColorPtr        splashBmpData;
    int                   splashBmpRowSize;
    RECT                  rc;
    int                   offsetDiff;

    if (VS_AMIGA == gVisualStyle)
        yDest += CAPTION_DY;

    yDest += BORDER_TOP;

    assert(win->hdcToDraw);

    if (tileA)
    {
        splashBmp = tileA->bitmap;
        if (!splashBmp)
        {
            DBG_OUT("redrawRect() tileA->bitmap is NULL!\n");
            return;
        }
        splashBmpDx = splashBmp->getWidth();
        splashBmpDy = splashBmp->getHeight();
        splashBmpRowSize = splashBmp->getRowSize();
        splashBmpData = splashBmp->getDataPtr();
        splashBmpColorMode = splashBmp->getMode();

        // DBG_OUT("redrawRect(tileA=0x%x, xSrc=%d, ySrc=%d, xDest=%d, yDest=%d, width=%d, height=%d, bmpDx=%d, bmpDy=%d)\n",  tileA, xSrc, ySrc, xDest, yDest, width, height, splashBmpDx, splashBmpDy);

        win->dibInfo->bmiHeader.biWidth = splashBmpDx;
        win->dibInfo->bmiHeader.biHeight = -splashBmpDy;
        win->dibInfo->bmiHeader.biSizeImage = splashBmpDy * splashBmpRowSize;

        SplashColorPtr newBmpData = NULL;

        if (!newBmpData)
            newBmpData = splashBmpData;
        offsetDiff = 0;
        if (splashBmpDy > drawAreaHeight)
            offsetDiff = splashBmpDy - drawAreaHeight;
        SetDIBitsToDevice(win->hdcToDraw,
            xDest, /* destx */
            yDest, /* desty */
            width, /* destw */
            height, /* desth */
            xSrc, /* srcx */
            /* TODO: I don't understand why I have to do this trick with offsetDiff,
               it shouldn't be necessary */
            offsetDiff - ySrc , /* srcy */
            0, /* startscan */
            splashBmpDy, /* numscans */
            newBmpData, /* pBits */
            win->dibInfo, /* pInfo */
            DIB_RGB_COLORS /* color use flag */
        );
        if (newBmpData != splashBmpData)
            free((void*)newBmpData);
    }
    else
    {
        // DBG_OUT("redrawRect(tileA=NULL, xSrc=%d, ySrc=%d, xDest=%d, yDest=%d, width=%d, height=%d)\n", xSrc, ySrc, xDest, yDest, width, height);
        rc.left = 0;
        rc.right = win->winDx;
        if (VS_AMIGA == gVisualStyle) {
            rc.top = CAPTION_DY;
            rc.bottom = win->winDy + CAPTION_DY;
        } else {
            assert(VS_WINDOWS == gVisualStyle);
            rc.top = 0;
            rc.bottom = win->winDy;
        }

        FillRect(win->hdcToDraw, &rc, gBrushBg);
    }
    updateScrollbars();
}
#endif

void WindowInfo_Paint(WindowInfo *win)
{


}

void OnPaint(WindowInfo *win)
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
        WinResizeIfNeeded(win);

        if (VS_AMIGA == gVisualStyle)
            AmigaCaptionDraw(win);
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

#if 0
    core->displayPage(core->getPageNum(), zoom, core->getRotate(), gTrue, gFalse);
    core->displayPage(core->getPageNum(), zoom, core->getRotate(), gTrue, gFalse);
    WindowInfo_RedrawAll(win);
#endif
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

    win = OpenPdf(file_name, TRUE);
    if (!win)
        return;
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
    if (' ' == key) {
        /*  Space does smart scrolling.
            TODO: Overlapp scrolling?
            TODO: backspace should scroll in the opposite direction.
        */
        SCROLLINFO si;
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_RANGE|SIF_POS|SIF_PAGE;
        GetScrollInfo(win->hwnd, SB_VERT, &si);
        if (si.nPos + (int)si.nPage >= si.nMax)
        {
            if (win->dm)
                DisplayModel_GoToNextPage(win->dm, 0);
        }
        else
            SendMessage(win->hwnd, WM_VSCROLL, SB_PAGEDOWN, 0);
    } else if ('k' == key) {
        SendMessage(win->hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
    } else if ('j' == key) {
        SendMessage(win->hwnd, WM_VSCROLL, SB_LINEUP, 0);
    } else if ('n' == key) {
        if (win->dm)
            DisplayModel_GoToNextPage(win->dm, 0);
    } else if ('p' == key)
    {
        if (win->dm)
            DisplayModel_GoToPrevPage(win->dm, 0);
    } else if ('z' == key)
    {
    /* TODO: do me */
#if 0
        win->pdfCore->toggleZoom();
        WindowInfo_RedrawAll(win);
#endif
    } else if ('q' == key) {
        DestroyWindow(win->hwnd);
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

static inline BOOL IsBenchArg(char *txt)
{
    if (strieq(txt, BENCH_ARG_TXT))
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
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_XPDFWIN));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = NULL;
    wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_XPDFWIN);
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

    /* globalParams->setupBaseFonts(NULL); */

    ColorsInit();
    gCursorArrow = LoadCursor(NULL, IDC_ARROW);
    gCursorWait  = LoadCursor(NULL, IDC_WAIT);

    gBrushBg     = CreateSolidBrush(COL_WINDOW_BG);
    gBrushShadow = CreateSolidBrush(COL_WINDOW_SHADOW);

    return TRUE;
}

static inline BOOL CharIsWs(char c)
{
    switch (c) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            return TRUE;
    }
    return FALSE;
}

static inline BOOL CharIsWsOrZero(char c)
{
    switch (c) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case 0:
            return TRUE;
    }
    return FALSE;
}

void StrStrip(char *str)
{
    char *  new_start = str;
    char *  new_end;
    int     new_len;
    char    c;

    for (c = *new_start; c && CharIsWs(c);) {
        new_start++;
        c = *new_start;
    }
    new_end = new_start;

    while (*new_end)
        ++new_end;

    --new_end;
    while ((new_end > new_start) && CharIsWs(*new_end))
        --new_end;

    new_len = new_end - new_start;
    assert(new_len >= 0);
    /* TODO: move the string if necessary */
}

void Str_SkipWs(char **txt)
{
    char *cur;
    if (!txt)
        return;
    cur = *txt;
    if (!cur)
        return;
    while (CharIsWs(*cur)) {
        ++cur;
    }
    *txt = cur;
}

char *Str_ParseQuoted(char **txt)
{
    char *      strStart;
    char *      strCopy;
    char *      cur;
    char *      dst;
    char        c;
    size_t      len;

    assert(txt);
    if (!txt) return NULL;
    strStart = *txt;
    assert(strStart);
    if (!strStart) return NULL;

    assert('"' == *strStart);
    ++strStart;
    cur = strStart;
    len = 0;
    for (;;) {
        c = *cur;
        if ((0 == c) || ('"' == c))
            break;
        if ('\\' == c) {
            /* TODO: should I un-escape everything or just '\' and '"' ? */
            if (('\\' == cur[1]) || ('"' == cur[1])) {
                ++cur;
                c = *cur;
            }
        }
        ++cur;
        ++len;
    }

    strCopy = (char*)malloc(len+1);
    if (!strCopy)
        return NULL;

    cur = strStart;
    dst = strCopy;
    for (;;) {
        c = *cur;
        if (0 == c)
            break;
        if ('"' == c) {
            ++cur;
            break;
        }
        if ('\\' == c) {
            /* TODO: should I un-escape everything or just '\' and '"' ? */
            if (('\\' == cur[1]) || ('"' == cur[1])) {
                ++cur;
                c = *cur;
            }
        }
        *dst++ = c;
        ++cur;
    }
    *dst = 0;
    *txt = cur;
    return strCopy;
}

char *Str_ParseNonQuoted(char **txt)
{
    char *  cur;
    char *  strStart;
    char *  strCopy;
    char    c;
    size_t  strLen;

    strStart = *txt;
    assert(strStart);
    if (!strStart) return NULL;
    assert('"' != *strStart);
    cur = strStart;
    for (;;) {
        c = *cur;
        if (CharIsWsOrZero(c))
            break;
        ++cur;
    }

    strLen = cur - strStart;
    assert(strLen > 0);
    strCopy = StrDupN(strStart, strLen);
    *txt = cur;
    return strCopy;
}

char *Str_ParsePossiblyQuoted(char **txt)
{
    char *  cur;
    char *  strCopy;

    if (!txt)
        return NULL;
    cur = *txt;
    if (!cur)
        return NULL;

    Str_SkipWs(&cur);
    if (0 == *cur)
        return NULL;
    if ('"' == *cur)
        strCopy = Str_ParseQuoted(&cur);
    else
        strCopy = Str_ParseNonQuoted(&cur);
    *txt = cur;
    return strCopy;
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

    WinFontList_Create();
    FontMappingList_Dump(&g_fontMapMSList);

    CaptionPens_Create();
    if (!InstanceInit(hInstance, nCmdShow))
        goto Exit;

    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_XPDFWIN));

    /* TODO: detect it's not me and show a dialog box ? */
    AssociatePdfWithExe(exeName);

    int pdfOpened = 0;
    if (NULL != gBenchFileName) {
            win = OpenPdf(gBenchFileName, FALSE);
            if (win)
                ++pdfOpened;
    } else {
        cur = gArgListRoot->next;
        while (cur) {
            win = OpenPdf(cur->str, FALSE);
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

    WinFontList_Destroy();
    StrList_Destroy(&gArgListRoot);
    return (int) msg.wParam;
}
