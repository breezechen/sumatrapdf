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
#define dimof(X)    (sizeof(X)/sizeof((X)[0]))

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
char *Str_DupN(const char *str, size_t len)
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

char *Str_Dup(const char *str)
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

unsigned long File_GetSize(const char *file_name)
{
    BOOL                        fOk;
    WIN32_FILE_ATTRIBUTE_DATA   fileInfo;

    if (NULL == file_name)
        return INVALID_FILE_SIZE;

    fOk = GetFileAttributesEx(file_name, GetFileExInfoStandard, (void*)&fileInfo);
    if (!fOk)
        return INVALID_FILE_SIZE;
    return (unsigned long)fileInfo.nFileSizeLow;
}

#define BUF_SIZE 1024
char *File_Slurp(const char *file_name, unsigned long *file_size_out)
{
    FILE *fp = NULL;
    unsigned char buf[BUF_SIZE];
    unsigned long left_to_read;
    size_t to_read, read;
    char *file_content = NULL;
    char *cur_content_pos;

    unsigned long file_size = File_GetSize(file_name);
    if (INVALID_FILE_SIZE == file_size)
        return NULL;
        
    file_content = (char*)malloc(file_size+1);
    if (!file_content)
        return NULL;
    file_content[file_size] = 0;

    cur_content_pos = file_content;
    *file_size_out = file_size;
    fp = fopen(file_name, "rb");
    if (!fp)
        goto Error;

    left_to_read = file_size;
    for(;;) {
        if (left_to_read > BUF_SIZE)
            to_read = BUF_SIZE;
        else
            to_read = (size_t)left_to_read;

        read = fread((void*)buf, 1, to_read, fp);
        if (ferror(fp))
            goto Error;
        memmove((void*)cur_content_pos, buf, read);
        cur_content_pos += read;
        left_to_read -= read;
        if (0 == left_to_read)
            break;
    }

    fclose(fp);
    return file_content;
Error:
    if (fp)
        fclose(fp);
    free((void*)file_content);
    return NULL;
}

/* split a string '*txt' at the border character 'c'. Something like python's
   string.split() except called iteratively.
   Returns a copy of the string (must be free()d by the caller).
   Returns NULL to indicate there's no more items. */
char *Str_SplitIter(char **txt, char c)
{
    const char *tmp;
    const char *pos;
    char *result;

    tmp = (const char*)*txt;
    if (!tmp)
        return NULL;

    pos = strchr(tmp, c);
    if (pos) {
         result = Str_DupN(tmp, (int)(pos-tmp));
         *txt = (char*)pos+1;
    } else {
        result = Str_Dup(tmp);
        *txt = NULL; /* next iteration will return NULL */
    }
    return result;
}

/* Replace all posible versions (Unix, Windows, Mac) of newline character
   with 'replace'. Returns newly allocated string with normalized newlines
   or NULL if error.
   Caller needs to free() the result */
char *Str_NormalizeNewline(char *txt, const char *replace)
{
    size_t  replace_len;
    char    c;
    char *  result;
    char *  tmp;
    size_t  result_len = 0;

    replace_len = strlen(replace);
    tmp = txt;
    for (;;) {
        c = *tmp++;
        if (!c)
            break;
        if (0xa == c) {
            /* a single 0xa => Unix */
            result_len += replace_len;
        } else if (0xd == c) {
            if (0xa == *tmp) {
                /* 0xd 0xa => dos */
                result_len += replace_len;
                ++tmp;
            }
            else {
                /* just 0xd => Mac */
                result_len += replace_len;
            }
        } else
            ++result_len;
    }

    if (0 == result_len)
        return NULL;

    result = (char*)malloc(result_len+1);
    if (!result)
        return NULL;
    tmp = result;
    for (;;) {
        c = *txt++;
        if (!c)
            break;
        if (0xa == c) {
            /* a single 0xa => Unix */
            memcpy(tmp, replace, replace_len);
            tmp += replace_len;
        } else if (0xd == c) {
            if (0xa == *txt) {
                /* 0xd 0xa => dos */
                memcpy(tmp, replace, replace_len);
                tmp += replace_len;
                ++txt;
            }
            else {
                /* just 0xd => Mac */
                memcpy(tmp, replace, replace_len);
                tmp += replace_len;
            }
        } else
            *tmp++ = c;
    }

    *tmp = 0;
    return result;
}

#define WHITE_SPACE_CHARS " \n\t\r"

int Str_Empty(const char *str)
{
    if (!str)
        return TRUE;
    if (0 == *str)
        return TRUE;
    return FALSE;
}

int Str_Contains(const char *str, char c)
{
    const char *pos = strchr(str, c);
    if (!pos)
        return FALSE;
    return TRUE;
}

/* Strip all 'to_strip' characters from the beginning of the string.
   Does stripping in-place */
void Str_StripLeft(char *txt, const char *to_strip)
{
    char *new_start = txt;
    char c;
    if (!txt || !to_strip)
        return;
    for (;;) {
        c = *new_start;
        if (0 == c)
            break;
        if (!Str_Contains(to_strip, c))
            break;
        ++new_start;
    }

    if (new_start != txt) {
        memmove(txt, new_start, strlen(new_start)+1);
    }
}

/* Strip white-space characters from the beginning of the string.
   Does stripping in-place */ 
void Str_StripWsLeft(char *txt)
{
    Str_StripLeft(txt, WHITE_SPACE_CHARS);
}

void Str_StripRight(char *txt, const char *to_strip)
{
    char * new_end;
    char   c;
    if (!txt || !to_strip)
        return;
    if (0 == *txt)
        return;
    /* point at the last character in the string */
    new_end = txt + strlen(txt) - 1;
    for (;;) {
        c = *new_end;
        if (!Str_Contains(to_strip, c))
            break;
        if (txt == new_end)
            break;
        --new_end;
    }
    if (Str_Contains(to_strip, *new_end))
        new_end[0] = 0;
    else
        new_end[1] = 0;
}

void Str_StripWsRight(char *txt)
{
    Str_StripRight(txt, WHITE_SPACE_CHARS);
}

void Str_StripBoth(char *txt, const char *to_strip)
{
    Str_StripLeft(txt, to_strip);
    Str_StripRight(txt, to_strip);
}

void Str_StripWsBoth(char *txt)
{
    Str_StripWsLeft(txt);
    Str_StripWsRight(txt);
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

int ParseInteger(const char *start, const char *end, int *intOut)
{
    char            numBuf[16];
    int             digitsCount;
    const char *    tmp;

    assert(start && end && intOut);
    assert(end >= start);
    if (!start || !end || !intOut || (start > end))
        return FALSE;

    digitsCount = 0;
    tmp = start;
    while (tmp <= end) {
        if (isspace(*tmp)) {
            /* do nothing, we allow whitespace */
        } else if (!isdigit(*tmp))
            return FALSE;
        numBuf[digitsCount] = *tmp;
        ++digitsCount;
        if (digitsCount == dimof(numBuf)-3) /* -3 to be safe */
            return FALSE;
        ++tmp;
    }
    if (0 == digitsCount)
        return FALSE;
    numBuf[digitsCount] = 0;
    *intOut = atoi(numBuf);
    return TRUE;
}

/* Given 'resolutionString' in format NxM (e.g. "100x200"), parse the string and put N
   into 'resolutionXOut' and M into 'resolutionYOut'.
   Return FALSE if there was an error (e.g. string is not in the right format */
int ParseResolutionString(const char *resolutionString, int *resolutionXOut, int *resolutionYOut)
{
    const char *    posOfX;

    assert(resolutionString);
    assert(resolutionXOut);
    assert(resolutionYOut);
    if (!resolutionString || !resolutionXOut || !resolutionYOut)
        return FALSE;
    *resolutionXOut = 0;
    *resolutionYOut = 0;
    posOfX = strchr(resolutionString, 'X');
    if (!posOfX)
        posOfX = strchr(resolutionString, 'x');
    if (!posOfX)
        return FALSE;
    if (posOfX == resolutionString)
        return FALSE;
    if (!ParseInteger(resolutionString, posOfX-1, resolutionXOut))
        return FALSE;
    if (!ParseInteger(posOfX+1, resolutionString+strlen(resolutionString)-1, resolutionYOut))
        return FALSE;
    return TRUE;
}

#ifdef DEBUG
void u_ParseResolutionString(void)
{
    int i;
    int result, resX, resY;
    const char *str;
    struct TestData {
        const char *    str;
        int             result;
        int             resX;
        int             resY;
    } testData[] = {
        { "", FALSE, 0, 0 },
        { "abc", FALSE, 0, 0},
        { "34", FALSE, 0, 0},
        { "0x0", TRUE, 0, 0},
        { "0x1", TRUE, 0, 1},
        { "0xab", FALSE, 0, 0},
        { "1x0", TRUE, 1, 0},
        { "100x200", TRUE, 100, 200},
        { "58x58", TRUE, 58, 58},
        { "  58x58", TRUE, 58, 58},
        { "58x  58", TRUE, 58, 58},
        { "58x58  ", TRUE, 58, 58},
        { "     58  x  58  ", TRUE, 58, 58},
        { "34x1234a", FALSE, 0, 0},
        { NULL, FALSE, 0, 0}
    };
    for (i=0; NULL != testData[i].str; i++) {
        str = testData[i].str;
        result = ParseResolutionString(str, &resX, &resY);
        assert(result == testData[i].result);
        if (result) {
            assert(resX == testData[i].resX);
            assert(resY == testData[i].resY);
        }
    }
}
#endif

void RunAllUnitTests(void)
{
#ifdef DEBUG
    u_ParseResolutionString();
#endif
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

#define UNIX_NEWLINE "\x0a"
#define UNIX_NEWLINE_C 0xa

void RenderPdfFileList(char *pdfFileList)
{
    char *data = NULL;
    char *dataNormalized = NULL;
    char *pdfFileName;
    unsigned long fileSize;

    assert(pdfFileList);
    if (!pdfFileList)
        return;
    data = File_Slurp(pdfFileList, &fileSize);
    if (!data) {
        error(-1, "couldn't load file '%s'", pdfFileList);
        return;
    }
    dataNormalized = Str_NormalizeNewline(data, UNIX_NEWLINE);
    if (!dataNormalized) {
        error(-1, "couldn't normalize data of file '%s'", pdfFileList);
        goto Exit;
    }
    for (;;) {
        pdfFileName = Str_SplitIter(&dataNormalized, UNIX_NEWLINE_C);
        if (!pdfFileName)
            break;
        Str_StripWsBoth(pdfFileName);
        if (Str_Empty(pdfFileName)) {
            free((void*)pdfFileName);
            continue;
        }
        RenderPdfFile(pdfFileName);
        free((void*)pdfFileName);
    }
Exit:
    free((void*)dataNormalized);
    free((void*)data);
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
    FILE *          outFile = NULL;
    FILE *          errFile = NULL;

    RunAllUnitTests();

    ParseCommandLine(argc, argv);
    if (0 == StrList_Len(&gArgsListRoot))
        PrintUsageAndExit();
    assert(gArgsListRoot);

    ColorsInit();
    globalParams = new GlobalParams("");
    if (!globalParams)
        return 1;
    globalParams->setErrQuiet(gFalse);

    if (gOutFileName) {
        outFile = fopen(gOutFileName, "wb");
        if (!outFile) {
            printf("failed to open -out file %s\n", gOutFileName);
            return 1;
        }
        gOutFile = outFile;
    }
    else
        gOutFile = stdout;

    if (gErrFileName) {
        errFile = fopen(gErrFileName, "wb");
        if (!errFile) {
            printf("failed to open -errout file %s\n", gErrFileName);
            return 1;
        }
        gErrFile = errFile;
    }
    else
        gErrFile = stderr;

    PreviewBitmap_Init();

    curr = gArgsListRoot;
    while (curr) {
        RenderCmdLineArg(curr->str);
        curr = curr->next;
    }
    if (outFile)
        fclose(outFile);
    if (errFile)
        fclose(errFile);
    PreviewBitmap_Destroy();
    StrList_Destroy(&gArgsListRoot);
    return 0;
}
