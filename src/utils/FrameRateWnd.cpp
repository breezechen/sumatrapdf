/* Copyright 2014 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "BaseUtil.h"
#include "FrameRateWnd.h"
#include "WinUtil.h"

/*
Frame rate window is a debugging tool that shows the frame rate, most likely
of how long it takes to service WM_PAINT. It's good for a rough measure of
how fast painting is.

The window is a top-level window, semi-transparent, without decorations
that sits in the upper right corner of some other window (associated window).

The window must follow associated window so that it maintains an illusion
that it's actually a part of that window.
*/

// TODO: nicer font

#define FRAME_RATE_CLASS_NAME L"FrameRateWnd"

#define COL_WHITE         RGB(0xff, 0xff, 0xff)
#define COL_BLACK         RGB(0, 0, 0)

static RECT GetClientRect(HWND hwnd)
{
    RECT r;
    GetClientRect(hwnd, &r);
    return r;
}

static void FrameRatePaint(FrameRateWnd *w, HDC hdc, PAINTSTRUCT& ps)
{
    RECT rc = GetClientRect(w->hwnd);
    ScopedGdiObj<HBRUSH> brush(CreateSolidBrush(COL_BLACK));
    FillRect(hdc, &rc, brush);

    SetTextColor(hdc, COL_WHITE);

    WCHAR *txt = str::Format(L"%d", w->frameRate);
    DrawCenteredText(hdc, rc, txt);
    free(txt);
}

static void PositionWindow(FrameRateWnd *w, SIZE s)
{
    RECT rc = GetClientRect(w->hwndAssociatedWith);
    POINT p = { rc.right - s.cx, rc.top };
    ClientToScreen(w->hwndAssociatedWith, &p);
    // put the window above associated window in z order
    SetWindowPos(w->hwnd, HWND_TOPMOST, p.x, p.y, s.cx, s.cy, SWP_NOACTIVATE);
}

static SIZE GetIdealSize(FrameRateWnd *w)
{
    WCHAR *txt = str::Format(L"%d", w->frameRate);
    SizeI s = TextSizeInHwnd(w->hwnd, txt);

    // add padding
    s.dy += 4;
    s.dx += 8;

    // we wan't to avoid the window to grow/shrink when the number changes
    // so we keep the largest size so far, since the difference isn't big
    if (s.dx > w->maxSizeSoFar.cx) {
        w->maxSizeSoFar.cx = s.dx;
    }
    if (s.dy > w->maxSizeSoFar.cy) {
        w->maxSizeSoFar.cy = s.dy;
    }
    free(txt);
    return w->maxSizeSoFar;
}

void ShowFrameRate(FrameRateWnd *w, int frameRate)
{
    if (w->frameRate == frameRate) {
        return;
    }
    w->frameRate = frameRate;
    SIZE s = GetIdealSize(w);
    PositionWindow(w, s);
    ScheduleRepaint(w->hwnd);
}

static void FrameRateOnPaint(FrameRateWnd *w)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(w->hwnd, &ps);
    FrameRatePaint(w, hdc, ps);
    EndPaint(w->hwnd, &ps);
}

static LRESULT CALLBACK WndProcFrameRateAssociated(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (WM_MOVING == msg ||
        WM_SIZING == msg ||
        WM_MOVE == msg) {
        FrameRateWnd *w = (FrameRateWnd*) dwRefData;
        PositionWindow(w, w->maxSizeSoFar);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProcFrameRate(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    FrameRateWnd *w;

    if (msg == WM_ERASEBKGND) {
        return TRUE; // tells Windows we handle background erasing so it doesn't do it
    }

    if (msg == WM_NCCREATE) {
        LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lp);
        w = reinterpret_cast<FrameRateWnd *>(lpcs->lpCreateParams);
        w->hwnd = hwnd;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(w));
    } else {
        w = reinterpret_cast<FrameRateWnd *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (!w) {
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    if (WM_PAINT == msg) {
        FrameRateOnPaint(w);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

static void RegisterFrameRateWndClass()
{
    static ATOM atom = NULL;
    if (!atom) {
        WNDCLASSEX  wcex;
        FillWndClassEx(wcex, FRAME_RATE_CLASS_NAME, WndProcFrameRate);
        atom = RegisterClassEx(&wcex);
        CrashIf(!atom);
    }
}

FrameRateWnd *AllocFrameRateWnd(HWND hwndAssociatedWith)
{
    RegisterFrameRateWndClass();
    FrameRateWnd *w = AllocStruct<FrameRateWnd>();
    w->hwndAssociatedWith = hwndAssociatedWith;
    w->frameRate = -1;
    return w;
}

bool CreateFrameRateWnd(FrameRateWnd *w)
{
    // WS_POPUP removes all decorations
    DWORD dwStyle = WS_POPUP | WS_VISIBLE;
    RECT r = GetClientRect(w->hwndAssociatedWith);
    HWND hwnd = CreateWindowEx(WS_EX_LAYERED, FRAME_RATE_CLASS_NAME, NULL, dwStyle,
             0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), w);
    CrashIf(hwnd != w->hwnd);
    if (!hwnd) {
        return false;
    }
    SetWindowSubclass(w->hwndAssociatedWith, WndProcFrameRateAssociated, 0, (DWORD_PTR) w);

    SetLayeredWindowAttributes(hwnd, 0, 0x7f, LWA_ALPHA);
    ShowFrameRate(w, 0);
    return true;
}

void DeleteFrameRateWnd(FrameRateWnd *w)
{
    RemoveWindowSubclass(w->hwndAssociatedWith, WndProcFrameRateAssociated, 0);
    free(w);
}

int FrameRateFromDuration(double durMs)
{
    return (int)(double(1000) / durMs);
}

