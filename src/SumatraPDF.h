#ifndef SUMATRAPDF_H_
#define SUMATRAPDF_H_

// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.
#ifndef WINVER                                // Allow use of features specific to Windows XP or later.
#define WINVER 0x0501                // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINNT                // Allow use of features specific to Windows XP or later.
#define _WIN32_WINNT 0x0501        // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINDOWS                // Allow use of features specific to Windows 98 or later.
#define _WIN32_WINDOWS 0x0410 // Change this to the appropriate value to target Windows Me or later.
#endif

#ifndef _WIN32_IE                        // Allow use of features specific to IE 6.0 or later.
#define _WIN32_IE 0x0600        // Change this to the appropriate value to target other versions of IE.
#endif

//#define WIN32_LEAN_AND_MEAN                // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include "resource.h"

#include "DisplayModel.h"

/* TODO: Currently not used. The idea is to be able to switch between different
   visual styles. Because I can. */
enum AppVisualStyle {
    VS_WINDOWS = 1,
    VS_AMIGA
};

/* Current state of a window:
  - WS_EMPTY - an empty window with no PDF opened
  - WS_ERROR_LOADING_PDF - showing an error message after failing to open a PDF
  - WS_SHOWING_PDF - showing a PDF file
  - WS_ABOUT_ANIM - showing "about" animation */
enum WinState {
    WS_EMPTY = 1,
    WS_ERROR_LOADING_PDF,
    WS_SHOWING_PDF,
    WS_ABOUT_ANIM = WS_EMPTY
};

#define INVALID_PAGE_NUM -1

/* When doing "about" animation, remembers the current animation state */
typedef struct {
    HWND        hwnd;
    int         frame;
    UINT_PTR    timerId;
} AnimState;

/* Describes information related to one window with (optional) pdf document
   on the screen */
typedef struct WindowInfo {
    /* points to the next element in the list or the first element if
       this is the first element */
    WindowInfo *    next;
    WinState        state;
    WinState        prevState;
    DisplayModel *  dm;
    HWND            hwnd;

    HDC             hdc;
    BITMAPINFO *    dibInfo;
    /* TODO: get rid of winDx, winDy, query it every time you need it */
    int             winDx;
    int             winDy;

    /* bitmap and hdc for (optional) double-buffering */
    HDC             hdcToDraw;
    HDC             hdcDoubleBuffer;
    HBITMAP         bmpDoubleBuffer;

    PdfLink *       linkOnLastButtonDown;
    const char *    url;

    /* when dragging the document around, this is previous position of the
       cursor. A delta between previous and current is by how much we
       moved */
    BOOL            dragging;
    int             dragPrevPosX, dragPrevPosY;
    AnimState       animState;
} WindowInfo;

void    WinSetText(HWND hwnd, const TCHAR *txt);
void    WinEditSelectAll(HWND hwnd);
void    WinEditSetSel(HWND hwnd, DWORD selStart, DWORD selEnd);
TCHAR * WinGetText(HWND hwnd);

#endif
