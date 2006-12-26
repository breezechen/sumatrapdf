#include <assert.h>
#include <windows.h>
#include "SplashBitmap.h"
#include <fitz.h>
#include <mupdf.h>

#define WIN_CLASS_NAME  "PDFTEST_PDF_WIN"
#define COL_WINDOW_BG RGB(0xff, 0xff, 0xff)

static HWND             gHwnd = NULL;
static HBRUSH           gBrushBg;
static BITMAPINFO *     gDibInfo = NULL;
static SplashBitmap *   gCurrBitmapSplash = NULL;
static fz_pixmap *      gCurrBitmapFitz = NULL;
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
    else {
        assert(gCurrBitmapFitz);
        if (gCurrBitmapFitz)
            DrawFitz(hwnd, gCurrBitmapFitz);
    }
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

void PreviewBitmapFitz(fz_pixmap *bitmap)
{
    assert(bitmap);
    if (!bitmap)
        return;
    if (!InitWinIfNecessary())
        return;

    gCurrBitmapFitz = bitmap;
    gCurrBitmapSplash = NULL;
    if ( (bitmap->w != gBitmapDx) ||
        (bitmap->h != gBitmapDy) ) {
        gBitmapDx = bitmap->w;
        gBitmapDy = bitmap->h;
        WinResizeClientArea(gHwnd, gBitmapDx, gBitmapDy);
    }
    assert(gHwnd);

    InvalidateRect(gHwnd, NULL, FALSE);
    UpdateWindow(gHwnd);

    PumpMessages();
}

void PreviewBitmapSplash(SplashBitmap *bitmap)
{
    assert(bitmap);
    if (!bitmap)
        return;
    if (!InitWinIfNecessary())
        return;

    gCurrBitmapSplash = bitmap;
    gCurrBitmapFitz = NULL;
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


