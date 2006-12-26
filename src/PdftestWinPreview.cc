#include <assert.h>
#include <windows.h>
#include "SplashBitmap.h"
#include <fitz.h>
#include <mupdf.h>

#define WIN_CLASS_NAME  "PDFTEST_PDF_WIN"
#define COL_WINDOW_BG RGB(0xff, 0xff, 0xff)

static HWND             gHwndSplash = NULL;
static HWND             gHwndFitz = NULL;
static HBRUSH           gBrushBg;
static BITMAPINFO *     gDibInfo = NULL;
static SplashBitmap *   gCurrBitmapSplash = NULL;
static fz_pixmap *      gCurrBitmapFitz = NULL;
static int              gBitmapSplashDx = -1;
static int              gBitmapSplashDy = -1;
static int              gBitmapFitzDx = -1;
static int              gBitmapFitzDy = -1;

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
void WinResizeClientArea(HWND hwnd, int x, int dx, int dy, int *dx_out)
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
    SetWindowPos(hwnd, NULL, x, 0, win_dx, win_dy, SWP_NOACTIVATE | SWP_NOREPOSITION | SWP_NOZORDER);
    *dx_out = win_dx;
}

void DrawSplash(HWND hwnd, SplashBitmap *bmp)
{
    HDC             hdc;
    PAINTSTRUCT     ps;
    RECT            rc;
    int             bmpRowSize;
    SplashColorPtr  bmpData;

    hdc = BeginPaint(hwnd, &ps);
    SetBkMode(hdc, TRANSPARENT);
    GetClientRect(hwnd, &rc);

    bmpRowSize = bmp->getRowSize();
    bmpData = bmp->getDataPtr();

    gDibInfo->bmiHeader.biWidth = gBitmapSplashDx;
    gDibInfo->bmiHeader.biHeight = -gBitmapSplashDy;
    gDibInfo->bmiHeader.biSizeImage = gBitmapSplashDy * bmpRowSize;

    FillRect(hdc, &ps.rcPaint, gBrushBg);

    SetDIBitsToDevice(hdc,
        0, /* destx */
        0, /* desty */
        gBitmapSplashDx, /* destw */
        gBitmapSplashDy, /* desth */
        0, /* srcx */
        0, /* srcy */
        0, /* startscan */
        gBitmapSplashDy, /* numscans */
        bmpData, /* pBits */
        gDibInfo, /* pInfo */
        DIB_RGB_COLORS /* color use flag */
    );

    EndPaint(hwnd, &ps);
}

static int bmpstride = 0;
static unsigned char *bmpdata = NULL;

void winconvert(fz_pixmap *image)
{
    int y, x;

    if (bmpdata)
        fz_free(bmpdata);

    bmpstride = ((image->w * 3 + 3) / 4) * 4;
    bmpdata = (unsigned char*)fz_malloc(image->h * bmpstride);
    if (!bmpdata)
        return;

    for (y = 0; y < image->h; y++)
    {
        unsigned char *p = bmpdata + y * bmpstride;
        unsigned char *s = image->samples + y * image->w * 4;
        for (x = 0; x < image->w; x++)
        {
            p[x * 3 + 0] = s[x * 4 + 3];
            p[x * 3 + 1] = s[x * 4 + 2];
            p[x * 3 + 2] = s[x * 4 + 1];
        }
    }
}

void winblit(HDC hdc, fz_pixmap *image)
{
    if (!bmpdata)
        return;

    gDibInfo->bmiHeader.biWidth = image->w;
    gDibInfo->bmiHeader.biHeight = -image->h;
    gDibInfo->bmiHeader.biSizeImage = image->h * bmpstride;
    SetDIBitsToDevice(hdc,
        0, /* destx */
        0, /* desty */
        image->w, /* destw */
        image->h, /* desth */
        0, /* srcx */
        0, /* srcy */
        0, /* startscan */
        image->h, /* numscans */
        bmpdata, /* pBits */
        gDibInfo, /* pInfo */
        DIB_RGB_COLORS /* color use flag */
    );
}


void DrawFitz(HWND hwnd, fz_pixmap *bmp)
{
    HDC             hdc;
    PAINTSTRUCT     ps;
    RECT            rc;

    hdc = BeginPaint(hwnd, &ps);
    SetBkMode(hdc, TRANSPARENT);
    GetClientRect(hwnd, &rc);

    winconvert(bmp);
    winblit(hdc, bmp);

    EndPaint(hwnd, &ps);
}

void OnPaint(HWND hwnd)
{

    if (gCurrBitmapSplash)
        DrawSplash(hwnd, gCurrBitmapSplash);
    if (gCurrBitmapFitz)
        DrawFitz(hwnd, gCurrBitmapFitz);
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
    if (gHwndSplash)
        return TRUE;

    if (!RegisterWinClass())
        return FALSE;

    gBrushBg = CreateSolidBrush(COL_WINDOW_BG);

    gHwndSplash = CreateWindow(
        WIN_CLASS_NAME, "Splash",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0,
        CW_USEDEFAULT, 0,
        NULL, NULL,
        NULL, NULL);

    if (!gHwndSplash)
        return FALSE;

    gHwndFitz = CreateWindow(
        WIN_CLASS_NAME, "Fitz",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0,
        CW_USEDEFAULT, 0,
        NULL, NULL,
        NULL, NULL);

    if (!gHwndFitz)
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

    ShowWindow(gHwndSplash, SW_HIDE);
    ShowWindow(gHwndFitz, SW_HIDE);
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

void PreviewBitmapInit(void)
{
    /* no need to do anything */
}

void PreviewBitmapDestroy(void)
{
    PostQuitMessage(0);
    PumpMessages();
    free(gDibInfo);
    gDibInfo = NULL;
    DeleteObject(gBrushBg);
}

static void SetSplash(SplashBitmap *bitmap)
{
    if (!InitWinIfNecessary())
        return;

    gCurrBitmapSplash = bitmap;
    if (!bitmap)
        return;

    if ( (bitmap->getWidth() != gBitmapSplashDx) ||
        (bitmap->getHeight() != gBitmapSplashDy) ) {
        gBitmapSplashDx = bitmap->getWidth();
        gBitmapSplashDy = bitmap->getHeight();
    }
}

static void SetFitz(fz_pixmap *bitmap)
{
    if (!InitWinIfNecessary())
        return;

    gCurrBitmapFitz = bitmap;
    if (!bitmap)
        return;

    if ( (bitmap->w != gBitmapFitzDx) ||
        (bitmap->h != gBitmapFitzDy) ) {
        gBitmapFitzDx = bitmap->w;
        gBitmapFitzDy = bitmap->h;
    }
}

static void UpdateWindows(void)
{
    int dx = 0;

    if (gCurrBitmapFitz) {
        WinResizeClientArea(gHwndFitz, 0, gBitmapFitzDx, gBitmapFitzDy, &dx);
        ShowWindow(gHwndFitz, SW_SHOW);
        InvalidateRect(gHwndFitz, NULL, FALSE);
        UpdateWindow(gHwndFitz);
    }

    if (gCurrBitmapSplash) {
        WinResizeClientArea(gHwndSplash, dx, gBitmapSplashDx, gBitmapSplashDy, &dx);
        ShowWindow(gHwndSplash, SW_SHOW);
        InvalidateRect(gHwndSplash, NULL, FALSE);
        UpdateWindow(gHwndSplash);
    }

    PumpMessages();
}

void PreviewBitmapSplashFitz(SplashBitmap *splash, fz_pixmap *fitz)
{
    SetSplash(splash);
    SetFitz(fitz);
    UpdateWindows();
}

void PreviewBitmapFitz(fz_pixmap *bitmap)
{
    SetSplash(NULL);
    SetFitz(bitmap);
    UpdateWindows();
}

void PreviewBitmapSplash(SplashBitmap *bitmap)
{
    SetFitz(NULL);
    SetSplash(bitmap);
    UpdateWindows();
}

