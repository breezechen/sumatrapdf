#ifndef SUMATRAPDF_H_
#define SUMATRAPDF_H_

// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.
#ifndef WINVER
#define WINVER 0x0410
#endif

#ifndef _WIN32_WINNT 
#define _WIN32_WINNT 0x0400
#endif

#ifndef _WIN32_WINDOWS                // Allow use of features specific to Windows 98 or later.
#define _WIN32_WINDOWS 0x0410 // Change this to the appropriate value to target Windows Me or later.
#endif

#ifndef _WIN32_IE                        // Allow use of features specific to IE 6.0 or later.
#define _WIN32_IE 0x0600        // Change this to the appropriate value to target other versions of IE.
#endif

//#define WIN32_LEAN_AND_MEAN                // Exclude rarely-used stuff from Windows headers

#include <windows.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include "resource.h"

#include "win_util.h"
#include "DisplayModelSplash.h"
#include "DisplayModelFitz.h"

/* TODO: Currently not used. The idea is to be able to switch between different
   visual styles. Because I can. */
enum AppVisualStyle {
    VS_WINDOWS = 1,
    VS_AMIGA
};

/* Current state of a window:
  - WS_ERROR_LOADING_PDF - showing an error message after failing to open a PDF
  - WS_SHOWING_PDF - showing a PDF file
  - WS_ABOUT - showing "about" screen */
enum WinState {
    WS_ERROR_LOADING_PDF = 1,
    WS_SHOWING_PDF,
    WS_ABOUT
};

/* When doing "about" animation, remembers the current animation state */
typedef struct {
    HWND        hwnd;
    int         frame;
    UINT_PTR    timerId;
} AnimState;

/* Describes information related to one window with (optional) pdf document
   on the screen */
class WindowInfo {
public:
    WindowInfo() {
        memzero(this, sizeof(*this)); // TODO: this might not be valid
    }
    void GetCanvasSize() { 
        GetClientRect(hwndCanvas, &m_canvasRc);
    }
    int winDx() { return rect_dx(&m_canvasRc); }
    int winDy() { return rect_dy(&m_canvasRc); }
    SizeI winSize() { return SizeI(rect_dx(&m_canvasRc), rect_dy(&m_canvasRc)); }

    /* points to the next element in the list or the first element if
       this is the first element */
    WindowInfo *    next;
    WinState        state;
    WinState        prevState;

    DisplayModel *  dm;
    HWND            hwndFrame;
    HWND            hwndCanvas;
    HWND            hwndToolbar;
    HWND            hwndReBar;

    HDC             hdc;
    BITMAPINFO *    dibInfo;

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

private:
    RECT m_canvasRc;
};

#endif
