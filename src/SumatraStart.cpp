/* Copyright 2006-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "SumatraPDF.h"
#include "WindowInfo.h"
#include "SumatraStart.h"
#include "SumatraAbout.h"
#include "FileHistory.h"
#include "translations.h"
#include "Version.h"
#include "WinUtil.h"
#include "FileUtil.h"
#include "Vec.h"
#include "Resource.h"
#include "AppTools.h"

// TODO: move the whole title calculations/drawing into SumatraAbout
#define SUMATRA_TXT_FONT        _T("Arial Black")
#define SUMATRA_TXT_FONT_SIZE   24

#define VERSION_TXT_FONT        _T("Arial Black")
#define VERSION_TXT_FONT_SIZE   12

#define VERSION_TXT             _T("v") CURR_VERSION_STR
#ifdef SVN_PRE_RELEASE_VER
 #define VERSION_SUB_TXT        _T("Pre-release")
#else
 #define VERSION_SUB_TXT        _T("")
#endif

#define ABOUT_BOX_MARGIN_DX         6
#define ABOUT_BOX_MARGIN_DY         6
#define ABOUT_BORDER_COL            RGB(0,0,0)

#define DOCLIST_SEPARATOR_DY        2
#define DOCLIST_THUMBNAIL_BORDER_W  2
#define DOCLIST_MARGIN_LEFT         40
#define DOCLIST_MARGIN_BETWEEN_X    30
#define DOCLIST_MARGIN_RIGHT        40
#define DOCLIST_MARGIN_TOP          60
#define DOCLIST_MARGIN_BETWEEN_Y    50
#define DOCLIST_MARGIN_BOTTOM       40
#define DOCLIST_MAX_THUMBNAILS_X    5
#define DOCLIST_BOTTOM_BOX_DY       50

struct StartPageLink {
    const TCHAR *filePath;
    RectI rect;
} gLinkInfo[FILE_HISTORY_MAX_FREQUENT + 2];

static void DrawStartPage(WindowInfo *win, HDC hdc, FileHistory& fileHistory)
{
    HBRUSH brushBg = CreateSolidBrush(gGlobalPrefs.m_bgColor);
    HBRUSH brushBg2 = CreateSolidBrush(RGB(0xCC, 0xCC, 0xCC));
    HBRUSH brushEmpty = CreateSolidBrush(WIN_COL_WHITE);

    HPEN penBorder = CreatePen(PS_SOLID, DOCLIST_SEPARATOR_DY, WIN_COL_BLACK);
    HPEN penThumbBorder = CreatePen(PS_SOLID, DOCLIST_THUMBNAIL_BORDER_W, WIN_COL_BLACK);
    HPEN penLinkLine = CreatePen(PS_SOLID, 1, COL_BLUE_LINK);

    Win::Font::ScopedFont fontSumatraTxt(hdc, SUMATRA_TXT_FONT, SUMATRA_TXT_FONT_SIZE);
    Win::Font::ScopedFont fontVersionTxt(hdc, VERSION_TXT_FONT, VERSION_TXT_FONT_SIZE);
    Win::Font::ScopedFont fontLeftTxt(hdc, _T("MS Shell Dlg"), 14);

    HGDIOBJ origFont = SelectObject(hdc, fontSumatraTxt); /* Just to remember the orig font */

    SetBkMode(hdc, TRANSPARENT);

    ClientRect rc(win->hwndCanvas);
    FillRect(hdc, &rc.ToRECT(), brushBg);

    SelectObject(hdc, brushBg);
    SelectObject(hdc, penBorder);

    /* calculate minimal top box size */
    SIZE txtSize;
    const TCHAR *txt = APP_NAME_STR;
    GetTextExtentPoint32(hdc, txt, Str::Len(txt), &txtSize);
    SizeI titleBox(txtSize.cx + ABOUT_BOX_MARGIN_DX * 2, txtSize.cy + ABOUT_BOX_MARGIN_DY * 2);

    /* consider version and version-sub strings */
    SelectObject(hdc, fontVersionTxt);
    txt = VERSION_TXT;
    GetTextExtentPoint32(hdc, txt, Str::Len(txt), &txtSize);
    SizeI versionBox(txtSize.cx, txtSize.cy * 2 + 3);
    txt = VERSION_SUB_TXT;
    GetTextExtentPoint32(hdc, txt, Str::Len(txt), &txtSize);
    versionBox.dx = max(versionBox.dx, txtSize.cx);
    titleBox.dy = max(titleBox.dy, versionBox.dy);

    /* render title */
    int x = max(rc.dx - titleBox.dx - versionBox.dx, 0);
    SelectObject(hdc, fontSumatraTxt);
    DrawSumatraPDF(hdc, x, ABOUT_BOX_MARGIN_DY);

    SetTextColor(hdc, ABOUT_BORDER_COL);
    SelectObject(hdc, fontVersionTxt);
    x += titleBox.dx - 6;
    txt = VERSION_TXT;
    TextOut(hdc, x, ABOUT_BOX_MARGIN_DY, txt, Str::Len(txt));
    txt = VERSION_SUB_TXT;
    TextOut(hdc, x, ABOUT_BOX_MARGIN_DY + (versionBox.dy - 3) / 2, txt, Str::Len(txt));

    SetTextColor(hdc, ABOUT_BORDER_COL);
    PaintLine(hdc, RectI(0, titleBox.dy, rc.dx, 0));

    /* render recent files list */
    SelectObject(hdc, brushBg2);
    SelectObject(hdc, penThumbBorder);

    rc.y += titleBox.dy;
    rc.dy -= titleBox.dy;
    FillRect(hdc, &rc.ToRECT(), brushBg2);
    rc.dy -= DOCLIST_BOTTOM_BOX_DY;

    int width = min((rc.dx - DOCLIST_MARGIN_LEFT - DOCLIST_MARGIN_RIGHT + DOCLIST_MARGIN_BETWEEN_X) / (THUMBNAIL_DX + DOCLIST_MARGIN_BETWEEN_X), DOCLIST_MAX_THUMBNAILS_X);
    int height = min((rc.dy - DOCLIST_MARGIN_TOP - DOCLIST_MARGIN_BOTTOM + DOCLIST_MARGIN_BETWEEN_Y) / (THUMBNAIL_DY + DOCLIST_MARGIN_BETWEEN_Y), FILE_HISTORY_MAX_FREQUENT / width);
    PointI offset(rc.x + DOCLIST_MARGIN_LEFT + (rc.dx - width * THUMBNAIL_DX - (width - 1) * DOCLIST_MARGIN_BETWEEN_X - DOCLIST_MARGIN_LEFT - DOCLIST_MARGIN_RIGHT) / 2, rc.y + DOCLIST_MARGIN_TOP);

    SelectObject(hdc, fontSumatraTxt);
    // TODO: translate
    txt = _T("Frequently Read");
    GetTextExtentPoint32(hdc, txt, Str::Len(txt), &txtSize);
    TextOut(hdc, offset.x, rc.y + (DOCLIST_MARGIN_TOP - txtSize.cy) / 2, txt, Str::Len(txt));

    SelectObject(hdc, fontLeftTxt);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));

    ZeroMemory(&gLinkInfo, sizeof(gLinkInfo));
    Vec<DisplayState *> *list = fileHistory.GetFrequencyOrder();

    int h;
    for (h = 0; h < height; h++) {
        for (int w = 0; w < width; w++) {
            if (h * width + w >= (int)list->Count() || !list->At(h * width + w)->openCount) {
                height = w > 0 ? h + 1 : h;
                break;
            }
            DisplayState *state = list->At(h * width + w);

            RectI page(offset.x + w * (THUMBNAIL_DX + DOCLIST_MARGIN_BETWEEN_X),
                       offset.y + h * (THUMBNAIL_DY + DOCLIST_MARGIN_BETWEEN_Y),
                       THUMBNAIL_DX, THUMBNAIL_DY);
            if (state->thumbnail) {
                HRGN clip = CreateRoundRectRgn(page.x, page.y, page.x + page.dx, page.y + page.dy, 10, 10);
                SelectClipRgn(hdc, clip);
                state->thumbnail->StretchDIBits(hdc, page);
                SelectClipRgn(hdc, NULL);
                DeleteObject(clip);
            }
            RoundRect(hdc, page.x, page.y, page.x + page.dx, page.y + page.dy, 10, 10);

            RectI rect(page.x, page.y + THUMBNAIL_DY + 3, THUMBNAIL_DX, 16);
            DrawText(hdc, Path::GetBaseName(state->filePath), -1, &rect.ToRECT(), DT_LEFT | DT_END_ELLIPSIS);

            gLinkInfo[h * width + w].filePath = state->filePath;
            gLinkInfo[h * width + w].rect = rect.Union(page);
        }
    }
    delete list;

    /* render bottom links */
    rc.y += DOCLIST_MARGIN_TOP + height * THUMBNAIL_DY + (height - 1) * DOCLIST_MARGIN_BETWEEN_Y + DOCLIST_MARGIN_BOTTOM;
    rc.dy = DOCLIST_BOTTOM_BOX_DY;

    SetTextColor(hdc, COL_BLUE_LINK);
    SelectObject(hdc, penLinkLine);

    HIMAGELIST himl = (HIMAGELIST)SendMessage(win->hwndToolbar, TB_GETIMAGELIST, 0, 0);
    RectI rectIcon(offset.x, rc.y, 0, 0);
    ImageList_GetIconSize(himl, &rectIcon.dx, &rectIcon.dy);
    rectIcon.y += (rc.dy - rectIcon.dy) / 2;
    ImageList_Draw(himl, 0, hdc, rectIcon.x, rectIcon.y, ILD_NORMAL);

    // TODO: translate
    txt = _T("Open a document...");
    GetTextExtentPoint32(hdc, txt, Str::Len(txt), &txtSize);
    RectI rect(offset.x + rectIcon.dx + 3, rc.y + (rc.dy - txtSize.cy) / 2, txtSize.cx, txtSize.cy);
    DrawText(hdc, txt, -1, &rect.ToRECT(), DT_LEFT);
    PaintLine(hdc, RectI(rect.x, rect.y + rect.dy, rect.dx, 0));
    gLinkInfo[FILE_HISTORY_MAX_FREQUENT].filePath = _T("<File,Open>");
    // make the click target larger
    rect = rect.Union(rectIcon);
    rect.Inflate(10, 10);
    gLinkInfo[FILE_HISTORY_MAX_FREQUENT].rect = rect;

    rc = ClientRect(win->hwndCanvas);
    // TODO: translate
    txt = _T("Hide frequently read");
    GetTextExtentPoint32(hdc, txt, Str::Len(txt), &txtSize);
    rect = RectI(rc.dx - txtSize.cx - 6, rc.y + rc.dy - txtSize.cy - 6, txtSize.cx, txtSize.cy);
    DrawText(hdc, txt, -1, &rect.ToRECT(), DT_LEFT);
    PaintLine(hdc, RectI(rect.x, rect.y + rect.dy, rect.dx, 0));
    gLinkInfo[FILE_HISTORY_MAX_FREQUENT + 1].filePath = _T("<View,HideList>");
    gLinkInfo[FILE_HISTORY_MAX_FREQUENT + 1].rect = rect;

    SelectObject(hdc, origFont);

    DeleteObject(brushBg);
    DeleteObject(brushBg2);
    DeleteObject(brushEmpty);
    DeleteObject(penBorder);
    DeleteObject(penThumbBorder);
    DeleteObject(penLinkLine);
}

static const TCHAR *GetStartLink(int x, int y, RectI *rect=NULL)
{
    if (gRestrictedUse) return NULL;

    PointI cursor(x, y);
    for (int i = 0; i < dimof(gLinkInfo); i++) {
        if (gLinkInfo[i].rect.Inside(cursor)) {
            if (rect)
                *rect = gLinkInfo[i].rect;
            return gLinkInfo[i].filePath;
        }
    }

    return NULL;
}

LRESULT HandleWindowStartMsg(WindowInfo *win, FileHistory& fileHistory, HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, bool& handled)
{
    POINT        pt;

    assert(win->IsAboutWindow());
    handled = false;

    switch (message) {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(win->hwndCanvas, &ps);
            DrawStartPage(win, win->buffer->GetDC(), fileHistory);
            win->buffer->Flush(hdc);
            EndPaint(win->hwndCanvas, &ps);
            handled = true;
        }
        return 0;

    case WM_SETCURSOR:
        if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) {
            RectI rect;
            const TCHAR *path = GetStartLink(pt.x, pt.y, &rect);
            if (path) {
                if (*path != '<')
                    win->CreateInfotip(path, rect);
                SetCursor(gCursorHand);
                handled = true;
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN:
        // remember a link under so that on mouse up we only activate
        // link if mouse up is on the same link as mouse down
        win->url = GetStartLink(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        handled = true;
        break;

    case WM_LBUTTONUP:
        const TCHAR *url = GetStartLink(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (url && url == win->url) {
            if (Str::Eq(url, _T("<File,Open>")))
                SendMessage(win->hwndFrame, WM_COMMAND, IDM_OPEN, 0);
            else if (Str::Eq(url, _T("<View,HideList>"))) {
                gGlobalPrefs.m_showStartPage = false;
                win->RedrawAll(true);
            } else
                LoadDocument(url, win);
        }
        win->url = NULL;
        handled = true;
        break;            
    }

    return 0;
}

// TODO: create in TEMP directory instead?
static TCHAR *GetThumbnailPath(const TCHAR *filePath)
{
    ScopedMem<TCHAR> thumbsPath(AppGenDataFilename(THUMBNAILS_DIR_NAME));
    ScopedMem<char> fingerPrint(Path_Fingerprint(filePath));
    ScopedMem<TCHAR> fname(Str::Conv::FromAnsi(fingerPrint));

    return Str::Format(_T("%s\\%s.bmp"), thumbsPath, fname);
}

// removes thumbnails that don't belong to any frequently used item in file history
static void CleanUpCache(FileHistory& fileHistory)
{
    ScopedMem<TCHAR> thumbsPath(AppGenDataFilename(THUMBNAILS_DIR_NAME));
    ScopedMem<TCHAR> pattern(Path::Join(thumbsPath, _T("*.bmp")));

    StrVec files;
    WIN32_FIND_DATA fdata;

    HANDLE hfind = FindFirstFile(pattern, &fdata);
    if (INVALID_HANDLE_VALUE == hfind)
        return;
    do {
        if (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            files.Append(Str::Dup(fdata.cFileName));
    } while (FindNextFile(hfind, &fdata));
    FindClose(hfind);

    Vec<DisplayState *> *list = fileHistory.GetFrequencyOrder();
    for (size_t i = 0; i < list->Count() && i < FILE_HISTORY_MAX_FREQUENT; i++) {
        ScopedMem<TCHAR> bmpPath(GetThumbnailPath(list->At(i)->filePath));
        int idx = files.Find(Path::GetBaseName(bmpPath));
        if (idx != -1) {
            free(files[idx]);
            files.RemoveAt(idx);
        }
    }
    delete list;

    for (size_t i = 0; i < files.Count(); i++) {
        ScopedMem<TCHAR> bmpPath(Path::Join(thumbsPath, files[i]));
        File::Delete(bmpPath);
    }
}

void LoadThumbnails(FileHistory& fileHistory)
{
    DisplayState *state;
    for (int i = 0; (state = fileHistory.Get(i)); i++) {
        if (state->thumbnail)
            delete state->thumbnail;
        state->thumbnail = NULL;

        ScopedMem<TCHAR> bmpPath(GetThumbnailPath(state->filePath));
        HBITMAP hbmp = (HBITMAP)LoadImage(NULL, bmpPath, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
        if (!hbmp)
            continue;

        BITMAP bmp;
        GetObject(hbmp, sizeof(BITMAP), &bmp);

        state->thumbnail = new RenderedBitmap(hbmp, bmp.bmWidth, bmp.bmHeight);
    }

    CleanUpCache(fileHistory);
}

void SaveThumbnail(DisplayState *state)
{
    if (!state->thumbnail)
        return;

    size_t dataLen;
    unsigned char *data = state->thumbnail->Serialize(&dataLen);
    if (data) {
        ScopedMem<TCHAR> bmpPath(GetThumbnailPath(state->filePath));
        ScopedMem<TCHAR> thumbsPath(Path::GetDir(bmpPath));
        if (Dir::Create(thumbsPath))
            File::WriteAll(bmpPath, data, dataLen);
        free(data);
    }
}
