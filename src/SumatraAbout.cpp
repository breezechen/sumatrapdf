/* Copyright 2006-2010 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "SumatraPDF.h"
#include "Version.h"
#include "AppPrefs.h"

#define ABOUT_LINE_OUTER_SIZE       2
#define ABOUT_LINE_SEP_SIZE         1
#define ABOUT_LEFT_RIGHT_SPACE_DX   8
#define ABOUT_MARGIN_DX            10
#define ABOUT_BOX_MARGIN_DY         6
#define ABOUT_BORDER_COL            RGB(0,0,0)
#define ABOUT_TXT_DY                6

#define ABOUT_RECT_PADDING          8
#define MIN_ABOUT_DX                326 // chosen so that (dbg) in debug builds is not cut off

#define ABOUT_WIN_TITLE         _TR("About SumatraPDF")

#ifndef SUMATRA_TXT
#define SUMATRA_TXT             _T("Sumatra PDF")
#endif
#define SUMATRA_TXT_FONT        _T("Arial Black")
#define SUMATRA_TXT_FONT_SIZE   24

#define BETA_TXT_FONT           _T("Arial Black")
#define BETA_TXT_FONT_SIZE      12

#ifdef SVN_PRE_RELEASE_VER
 #define BETA_TXT                _T("Pre-Release")
#else
 #ifdef DEBUG
 #define BETA_TXT                _T(" v") _T(CURR_VERSION) _T(" (dbg)")
 #else
 #define BETA_TXT                _T(" v") _T(CURR_VERSION)
 #endif
#endif

static HWND gHwndAbout;

static AboutLayoutInfoEl gAboutLayoutInfo[] = {
    { _T("website"), _T("SumatraPDF website"), _T("http://blog.kowalczyk.info/software/sumatrapdf"),
    0, 0, 0, 0, 0, 0, 0, 0 },

    { _T("forums"), _T("SumatraPDF forums"), _T("http://blog.kowalczyk.info/forum_sumatra"),
    0, 0, 0, 0, 0, 0, 0, 0 },

    { _T("programming"), _T("Krzysztof Kowalczyk"), _T("http://blog.kowalczyk.info"),
    0, 0, 0, 0, 0, 0, 0, 0 },

    { _T("programming"), _T("Simon B\xFCnzli"), _T("http://www.zeniko.ch/#SumatraPDF"),
    0, 0, 0, 0, 0, 0, 0, 0 },
    
    { _T("programming"), _T("William Blum"), _T("http://william.famille-blum.org/"),
    0, 0, 0, 0, 0, 0, 0, 0 },

#ifdef _TEX_ENHANCEMENT
    { _T("note"), _T("TeX build"), _T("http://william.famille-blum.org/software/sumatra/index.html"),
    0, 0, 0, 0, 0, 0, 0, 0 },
#endif 
#ifdef SVN_PRE_RELEASE_VER
    { _T("a note"), _T("Pre-release version, for testing only!"), NULL,
    0, 0, 0, 0, 0, 0, 0, 0 },
#endif
    { _T("pdf rendering"), _T("MuPDF"), _T("http://mupdf.com"),
    0, 0, 0, 0, 0, 0, 0, 0 },

    { _T("program icon"), _T("Zenon"), _T("http://www.flashvidz.tk/"),
    0, 0, 0, 0, 0, 0, 0, 0 },

    { _T("toolbar icons"), _T("Mark James"), _T("http://www.famfamfam.com/lab/icons/silk/"),
    0, 0, 0, 0, 0, 0, 0, 0 },

    { _T("translators"), _T("The Translators"), _T("http://blog.kowalczyk.info/software/sumatrapdf/translators.html"),
    0, 0, 0, 0, 0, 0, 0, 0 },

    { _T("translations"), _T("Contribute translation"), _T("http://blog.kowalczyk.info/software/sumatrapdf/translations.html"),
    0, 0, 0, 0, 0, 0, 0, 0 },

#ifdef _TEX_ENHANCEMENT
    { _T("SyncTeX"), _T("J\xE9rome Laurens"), _T("http://itexmac.sourceforge.net/SyncTeX.html"),
    0, 0, 0, 0, 0, 0, 0, 0 },
#endif 

    { NULL, NULL, NULL,
    0, 0, 0, 0, 0, 0, 0, 0 }
};

/* Draws the about screen a remember some state for hyperlinking.
   It transcribes the design I did in graphics software - hopeless
   to understand without seeing the design. */
void DrawAbout(HWND hwnd, HDC hdc, RECT *rect)
{
    SIZE            txtSize;
    int             totalDx, totalDy;
    int             leftLargestDx;
    int             sumatraPdfTxtDx, sumatraPdfTxtDy;
    int             linePosX, linePosY, lineDy;
    int             offX, offY;
    int             x, y;
    int             boxDy;

    HBRUSH brushBg = CreateSolidBrush(gGlobalPrefs.m_bgColor);

    HPEN penBorder = CreatePen(PS_SOLID, ABOUT_LINE_OUTER_SIZE, COL_BLACK);
    HPEN penDivideLine = CreatePen(PS_SOLID, ABOUT_LINE_SEP_SIZE, COL_BLACK);
    HPEN penLinkLine = CreatePen(PS_SOLID, ABOUT_LINE_SEP_SIZE, COL_BLUE_LINK);

    HFONT fontSumatraTxt = Win32_Font_GetSimple(hdc, SUMATRA_TXT_FONT, SUMATRA_TXT_FONT_SIZE);
    HFONT fontBetaTxt = Win32_Font_GetSimple(hdc, BETA_TXT_FONT, BETA_TXT_FONT_SIZE);
    HFONT fontLeftTxt = Win32_Font_GetSimple(hdc, LEFT_TXT_FONT, LEFT_TXT_FONT_SIZE);
    HFONT fontRightTxt = Win32_Font_GetSimple(hdc, RIGHT_TXT_FONT, RIGHT_TXT_FONT_SIZE);

    HFONT origFont = (HFONT)SelectObject(hdc, fontSumatraTxt); /* Just to remember the orig font */

    SetBkMode(hdc, TRANSPARENT);

    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, brushBg);

    SelectObject(hdc, brushBg);
    SelectObject(hdc, penBorder);

    offX = rect->left;
    offY = rect->top;
    totalDx = rect_dx(rect);
    totalDy = rect_dy(rect);

    /* render title */
    const TCHAR *txt = SUMATRA_TXT;
    GetTextExtentPoint32(hdc, txt, lstrlen(txt), &txtSize);
    sumatraPdfTxtDx = txtSize.cx;
    sumatraPdfTxtDy = txtSize.cy;

    boxDy = sumatraPdfTxtDy + ABOUT_BOX_MARGIN_DY * 2;

    Rectangle(hdc, offX, offY + ABOUT_LINE_OUTER_SIZE, offX + totalDx, offY + boxDy + ABOUT_LINE_OUTER_SIZE);

    SetTextColor(hdc, ABOUT_BORDER_COL);
    (HFONT)SelectObject(hdc, fontSumatraTxt);
    x = offX + (totalDx - sumatraPdfTxtDx) / 2;
    y = offY + (boxDy - sumatraPdfTxtDy) / 2;
    txt = SUMATRA_TXT;
    TextOut(hdc, x, y, txt, lstrlen(txt));

    (HFONT)SelectObject(hdc, fontBetaTxt);
    x = offX + (totalDx - sumatraPdfTxtDx) / 2 + sumatraPdfTxtDx + 6;
    y = offY + (boxDy - sumatraPdfTxtDy) / 2;
    txt = BETA_TXT;
    TextOut(hdc, x, y, txt, lstrlen(txt));

#ifdef BUILD_RM_VERSION
    txt = _T("Adapted by RM");
    TextOut(hdc, x, y + 16, txt, lstrlen(txt));
#endif

#ifdef SVN_PRE_RELEASE_VER
    GetTextExtentPoint32(hdc, txt, lstrlen(txt), &txtSize);
    y += (int)txtSize.cy + 2;

    TCHAR buf[128];
    _sntprintf(buf, dimof(buf), _T("v%s svn %d"), _T(CURR_VERSION), SVN_PRE_RELEASE_VER);
    txt = &(buf[0]);
    TextOut(hdc, x, y, txt, lstrlen(txt));
#endif
    SetTextColor(hdc, ABOUT_BORDER_COL);

    offY += boxDy;
    Rectangle(hdc, offX, offY, offX + totalDx, offY + totalDy - boxDy);

    /* render text on the left*/
    leftLargestDx = 0;
    (HFONT)SelectObject(hdc, fontLeftTxt);
    for (int i = 0; gAboutLayoutInfo[i].leftTxt != NULL; i++) {
        txt = gAboutLayoutInfo[i].leftTxt;
        x = gAboutLayoutInfo[i].leftTxtPosX;
        y = gAboutLayoutInfo[i].leftTxtPosY;
        TextOut(hdc, x, y, txt, lstrlen(txt));

        if (leftLargestDx < gAboutLayoutInfo[i].leftTxtDx)
            leftLargestDx = gAboutLayoutInfo[i].leftTxtDx;
    }

    /* render text on the right */
    (HFONT)SelectObject(hdc, fontRightTxt);
    for (int i = 0; gAboutLayoutInfo[i].leftTxt != NULL; i++) {
        bool hasUrl = !gRestrictedUse && gAboutLayoutInfo[i].url;
        SetTextColor(hdc, hasUrl ? COL_BLUE_LINK : ABOUT_BORDER_COL);

        txt = gAboutLayoutInfo[i].rightTxt;
        x = gAboutLayoutInfo[i].rightTxtPosX;
        y = gAboutLayoutInfo[i].rightTxtPosY;
        TextOut(hdc, x, y, txt, lstrlen(txt));

        if (!hasUrl)
            continue;

        int underlineY = y + gAboutLayoutInfo[i].rightTxtDy - 3;
        SelectObject(hdc, penLinkLine);
        MoveToEx(hdc, x, underlineY, NULL);
        LineTo(hdc, x + gAboutLayoutInfo[i].rightTxtDx, underlineY);    
    }

    linePosX = ABOUT_LINE_OUTER_SIZE + ABOUT_MARGIN_DX + leftLargestDx + ABOUT_LEFT_RIGHT_SPACE_DX;
    linePosY = 4;
    lineDy = (dimof(gAboutLayoutInfo)-1) * (gAboutLayoutInfo[0].rightTxtDy + ABOUT_TXT_DY);

    SelectObject(hdc, penDivideLine);
    MoveToEx(hdc, linePosX + offX, linePosY + offY, NULL);
    LineTo(hdc, linePosX + offX, linePosY + lineDy + offY);

    SelectObject(hdc, origFont);
    Win32_Font_Delete(fontSumatraTxt);
    Win32_Font_Delete(fontBetaTxt);
    Win32_Font_Delete(fontLeftTxt);
    Win32_Font_Delete(fontRightTxt);

    DeleteObject(brushBg);
    DeleteObject(penBorder);
    DeleteObject(penDivideLine);
    DeleteObject(penLinkLine);
}

void UpdateAboutLayoutInfo(HWND hwnd, HDC hdc, RECT * rect)
{
    SIZE            txtSize;
    int             totalDx, totalDy;
    int             leftDy, rightDy;
    int             leftLargestDx, rightLargestDx;
    int             linePosX, linePosY;
    int             currY;
    int             offX, offY;
    int             boxDy;

    HFONT fontSumatraTxt = Win32_Font_GetSimple(hdc, SUMATRA_TXT_FONT, SUMATRA_TXT_FONT_SIZE);
    HFONT fontLeftTxt = Win32_Font_GetSimple(hdc, LEFT_TXT_FONT, LEFT_TXT_FONT_SIZE);
    HFONT fontRightTxt = Win32_Font_GetSimple(hdc, RIGHT_TXT_FONT, RIGHT_TXT_FONT_SIZE);
    HFONT origFont = (HFONT)SelectObject(hdc, fontSumatraTxt);

    /* calculate top box height */
    const TCHAR *txt = SUMATRA_TXT;
    GetTextExtentPoint32(hdc, txt, lstrlen(txt), &txtSize);
    boxDy = txtSize.cy + ABOUT_BOX_MARGIN_DY * 2;

    /* calculate left text dimensions */
    (HFONT)SelectObject(hdc, fontLeftTxt);
    leftLargestDx = 0;
    leftDy = 0;
    for (int i = 0; gAboutLayoutInfo[i].leftTxt != NULL; i++) {
        txt = gAboutLayoutInfo[i].leftTxt;
        GetTextExtentPoint32(hdc, txt, lstrlen(txt), &txtSize);
        gAboutLayoutInfo[i].leftTxtDx = (int)txtSize.cx;
        gAboutLayoutInfo[i].leftTxtDy = (int)txtSize.cy;

        if (0 == i)
            leftDy = gAboutLayoutInfo[i].leftTxtDy;
        else
            assert(leftDy == gAboutLayoutInfo[i].leftTxtDy);
        if (leftLargestDx < gAboutLayoutInfo[i].leftTxtDx)
            leftLargestDx = gAboutLayoutInfo[i].leftTxtDx;
    }

    /* calculate right text dimensions */
    (HFONT)SelectObject(hdc, fontRightTxt);
    rightLargestDx = 0;
    rightDy = 0;
    for (int i = 0; gAboutLayoutInfo[i].leftTxt != NULL; i++) {
        txt = gAboutLayoutInfo[i].rightTxt;
        GetTextExtentPoint32(hdc, txt, lstrlen(txt), &txtSize);
        gAboutLayoutInfo[i].rightTxtDx = (int)txtSize.cx;
        gAboutLayoutInfo[i].rightTxtDy = (int)txtSize.cy;

        if (0 == i)
            rightDy = gAboutLayoutInfo[i].rightTxtDy;
        else
            assert(rightDy == gAboutLayoutInfo[i].rightTxtDy);
        if (rightLargestDx < gAboutLayoutInfo[i].rightTxtDx)
            rightLargestDx = gAboutLayoutInfo[i].rightTxtDx;
    }

    /* calculate total dimension and position */
    totalDx  = ABOUT_LINE_OUTER_SIZE + ABOUT_MARGIN_DX + leftLargestDx;
    totalDx += ABOUT_LEFT_RIGHT_SPACE_DX + ABOUT_LINE_SEP_SIZE + ABOUT_LEFT_RIGHT_SPACE_DX;
    totalDx += rightLargestDx + ABOUT_MARGIN_DX + ABOUT_LINE_OUTER_SIZE;
    if (totalDx < MIN_ABOUT_DX)
        totalDx = MIN_ABOUT_DX;

    totalDy  = boxDy;
    totalDy += ABOUT_LINE_OUTER_SIZE;
    totalDy += (dimof(gAboutLayoutInfo)-1) * (rightDy + ABOUT_TXT_DY);
    totalDy += ABOUT_LINE_OUTER_SIZE + 4;

    RECT rc;
    GetClientRect(hwnd, &rc);
    offX = (rect_dx(&rc) - totalDx) / 2;
    offY = (rect_dy(&rc) - totalDy) / 2;

    if (rect) {
        rect->left = offX;
        rect->top = offY;
        rect->right = offX + totalDx;
        rect->bottom = offY + totalDy;
    }

    /* calculate text positions */
    linePosX = ABOUT_LINE_OUTER_SIZE + ABOUT_MARGIN_DX + leftLargestDx + ABOUT_LEFT_RIGHT_SPACE_DX;
    linePosY = 4;

    currY = offY + boxDy + linePosY;
    for (int i = 0; gAboutLayoutInfo[i].leftTxt != NULL; i++) {
        gAboutLayoutInfo[i].leftTxtPosX = offX + linePosX - ABOUT_LEFT_RIGHT_SPACE_DX - gAboutLayoutInfo[i].leftTxtDx;
        gAboutLayoutInfo[i].leftTxtPosY = currY + (rightDy - leftDy) / 2;
        gAboutLayoutInfo[i].rightTxtPosX = offX + linePosX + ABOUT_LEFT_RIGHT_SPACE_DX;
        gAboutLayoutInfo[i].rightTxtPosY = currY;
        currY += rightDy + ABOUT_TXT_DY;
    }

    SelectObject(hdc, origFont);
    Win32_Font_Delete(fontSumatraTxt);
    Win32_Font_Delete(fontLeftTxt);
    Win32_Font_Delete(fontRightTxt);
}

void OnPaintAbout(HWND hwnd)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC hdc = BeginPaint(hwnd, &ps);
    UpdateAboutLayoutInfo(hwnd, hdc, &rc);
    DrawAbout(hwnd, hdc, &rc);
    EndPaint(hwnd, &ps);
}

const TCHAR *AboutGetLink(WindowInfo *win, int x, int y, AboutLayoutInfoEl **el)
{
    if (gRestrictedUse) return NULL;

    // Update the link location information
    if (win)
        UpdateAboutLayoutInfo(win->hwndCanvas, win->hdcToDraw, NULL);
    else
        OnPaintAbout(gHwndAbout);

    for (int i = 0; gAboutLayoutInfo[i].leftTxt; i++) {
        if ((x < gAboutLayoutInfo[i].rightTxtPosX) ||
            (x > gAboutLayoutInfo[i].rightTxtPosX + gAboutLayoutInfo[i].rightTxtDx))
            continue;
        if ((y < gAboutLayoutInfo[i].rightTxtPosY) ||
            (y > gAboutLayoutInfo[i].rightTxtPosY + gAboutLayoutInfo[i].rightTxtDy))
            continue;
        if (el)
            *el = &gAboutLayoutInfo[i];
        return gAboutLayoutInfo[i].url;
    }
    return NULL;
}


void OnMenuAbout() {
    if (gHwndAbout) {
        SetActiveWindow(gHwndAbout);
        return;
    }

    gHwndAbout = CreateWindow(
            ABOUT_CLASS_NAME, ABOUT_WIN_TITLE,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            NULL, NULL,
            ghinst, NULL);
    if (!gHwndAbout)
        return;

    // get the dimensions required for the about box's content
    RECT rc;
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(gHwndAbout, &ps);
    UpdateAboutLayoutInfo(gHwndAbout, hdc, &rc);
    EndPaint(gHwndAbout, &ps);
    InflateRect(&rc, ABOUT_RECT_PADDING, ABOUT_RECT_PADDING);

    // resize the new window to just match these dimensions
    RECT wRc, cRc;
    GetWindowRect(gHwndAbout, &wRc);
    GetClientRect(gHwndAbout, &cRc);
    wRc.right += rect_dx(&rc) - rect_dx(&cRc);
    wRc.bottom += rect_dy(&rc) - rect_dy(&cRc);
    MoveWindow(gHwndAbout, wRc.left, wRc.top, rect_dx(&wRc), rect_dy(&wRc), FALSE);

    ShowWindow(gHwndAbout, SW_SHOW);
}

LRESULT CALLBACK WndProcAbout(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    const TCHAR * url;
    POINT pt;

    switch (message)
    {
        case WM_CREATE:
            assert(!gHwndAbout);
            break;

        case WM_ERASEBKGND:
            // do nothing, helps to avoid flicker
            return TRUE;

        case WM_PAINT:
            OnPaintAbout(hwnd);
            break;

        case WM_SETCURSOR:
            if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) {
                if (AboutGetLink(NULL, pt.x, pt.y)) {
                    SetCursor(gCursorHand);
                    return TRUE;
                }
            }
            return DefWindowProc(hwnd, message, wParam, lParam);

        case WM_LBUTTONDOWN:
            url = AboutGetLink(NULL, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            SetWindowLong(hwnd, GWL_USERDATA, (LONG)url);
            break;

        case WM_LBUTTONUP:
            url = AboutGetLink(NULL, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (url && url == (const TCHAR *)GetWindowLong(hwnd, GWL_USERDATA))
                LaunchBrowser(url);
            SetWindowLong(hwnd, GWL_USERDATA, 0);
            break;

        case WM_CHAR:
            if (VK_ESCAPE == wParam)
                DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            assert(gHwndAbout);
            gHwndAbout = NULL;
            break;

        /* TODO: handle mouse move/down/up so that links work */

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

