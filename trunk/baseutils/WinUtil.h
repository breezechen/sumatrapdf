/* Written by Krzysztof Kowalczyk (http://blog.kowalczyk.info)
   The author disclaims copyright to this source code. 
   Take all the code you want, we'll just write more.
*/
#ifndef WINUTIL_HPP__
#define WINUTIL_HPP__

#include <windows.h>
#include <CommCtrl.h>

class WinLibrary {
public:
    WinLibrary(const TCHAR *libName) {
        _hlib = _LoadSystemLibrary(libName);
    }
    ~WinLibrary() { FreeLibrary(_hlib); }
    FARPROC GetProcAddr(const char *procName) {
        if (!_hlib) return NULL;
        return GetProcAddress(_hlib, procName);
    }
private:
    HMODULE _hlib;
    HMODULE _LoadSystemLibrary(const TCHAR *libName);
};

class ScopedCom {
public:
    ScopedCom() { CoInitialize(NULL); }
    ~ScopedCom() { CoUninitialize(); }
};

class MillisecondTimer {
    LARGE_INTEGER   start;
    LARGE_INTEGER   end;
public:
    void Start() { QueryPerformanceCounter(&start); }
    void Stop() { QueryPerformanceCounter(&end); }

    double GetTimeInMs()
    {
        LARGE_INTEGER   freq;
        QueryPerformanceFrequency(&freq);
        double timeInSecs = (double)(end.QuadPart-start.QuadPart)/(double)freq.QuadPart;
        return timeInSecs * 1000.0;
    }
};

static inline void InitAllCommonControls()
{
    INITCOMMONCONTROLSEX cex = {0};
    cex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    cex.dwICC = ICC_WIN95_CLASSES | ICC_DATE_CLASSES | ICC_USEREX_CLASSES | ICC_COOL_CLASSES ;
    InitCommonControlsEx(&cex);
}

static inline void FillWndClassEx(WNDCLASSEX &wcex, HINSTANCE hInstance) 
{
    wcex.cbSize         = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = 0;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = NULL;
    wcex.lpszMenuName   = NULL;
    wcex.hIconSm        = 0;
}

static inline int RectDx(RECT *r)
{
    return r->right - r->left;
}

static inline int RectDy(RECT *r)
{
    return r->bottom - r->top;
}

bool IsAppThemed();
bool WindowsVerVistaOrGreater();

void SeeLastError(DWORD err=0);
bool ReadRegStr(HKEY keySub, const TCHAR *keyName, const TCHAR *valName, const TCHAR *buffer, DWORD bufLen);
bool WriteRegStr(HKEY keySub, const TCHAR *keyName, const TCHAR *valName, const TCHAR *value);
bool WriteRegDWORD(HKEY keySub, const TCHAR *keyName, const TCHAR *valName, DWORD value);

void EnableNx();
void RedirectIOToConsole();
TCHAR *ResolveLnk(TCHAR *path);
IDataObject* GetDataObjectForFile(LPCTSTR filePath, HWND hwnd=NULL);
DWORD GetFileVersion(TCHAR *path);

inline bool IsKeyPressed(int key)
{
    return GetKeyState(key) & 0x8000 ? true : false;
}
inline bool IsShiftPressed() { return IsKeyPressed(VK_SHIFT); }
inline bool IsAltPressed() { return IsKeyPressed(VK_MENU); }
inline bool IsCtrlPressed() { return IsKeyPressed(VK_CONTROL); }

namespace Win {
namespace Menu {

inline void Check(HMENU m, UINT id, bool check)
{
    CheckMenuItem(m, id, MF_BYCOMMAND | (check ? MF_CHECKED : MF_UNCHECKED));
}

inline void Enable(HMENU m, UINT id, bool enable)
{
    EnableMenuItem(m, id, MF_BYCOMMAND | (enable ? MF_ENABLED : MF_GRAYED));
}

} // namespace Menu
} // namespace Win

// used to be in win_util.h

#include <commctrl.h>
#include <windowsx.h>

/* Utilities to help in common windows programming tasks */

/* constant to make it easier to return proper LRESULT values when handling
   various windows messages */
#define WM_KILLFOCUS_HANDLED 0
#define WM_SETFOCUS_HANDLED 0
#define WM_KEYDOWN_HANDLED 0
#define WM_KEYUP_HANDLED 0
#define WM_LBUTTONDOWN_HANDLED 0
#define WM_LBUTTONUP_HANDLED 0
#define WM_PAINT_HANDLED 0
#define WM_DRAWITEM_HANDLED TRUE
#define WM_MEASUREITEM_HANDLED TRUE
#define WM_SIZE_HANDLED 0
#define LVN_ITEMACTIVATE_HANDLED 0
#define WM_VKEYTOITEM_HANDLED_FULLY -2
#define WM_VKEYTOITEM_NOT_HANDLED -1
#define WM_NCPAINT_HANDLED 0
#define WM_VSCROLL_HANDLED 0
#define WM_HSCROLL_HANDLED 0
#define WM_CREATE_FAILED -1
#define WM_CREATE_OK 0

#define WIN_COL_RED     RGB(255,0,0)
#define WIN_COL_WHITE   RGB(255,255,255)
#define WIN_COL_BLACK   RGB(0,0,0)
#define WIN_COL_BLUE    RGB(0,0,255)
#define WIN_COL_GREEN   RGB(0,255,0)
#define WIN_COL_GRAY    RGB(215,215,215)

#define DRAGQUERY_NUMFILES 0xFFFFFFFF

int     win_get_text_len(HWND hwnd);
TCHAR * win_get_text(HWND hwnd);
void    win_set_text(HWND hwnd, const TCHAR *txt);

#define Edit_SelectAll(hwnd) Edit_SetSel(hwnd, 0, -1)
#define ListBox_AppendString_NoSort(hwnd, txt) ListBox_InsertString(hwnd, -1, txt)
#define Window_SetFont(hwnd, font) SetWindowFont(hwnd, font, TRUE)

int     screen_get_dx(void);
int     screen_get_dy(void);
int     screen_get_menu_dy(void);
int     screen_get_caption_dy(void);
void    rect_shift_to_work_area(RECT *rect, BOOL bFully);

void    launch_url(const TCHAR *url);
void    exec_with_params(const TCHAR *exe, const TCHAR *params, BOOL hidden);

TCHAR * get_app_data_folder_path(BOOL f_create);

void    paint_round_rect_around_hwnd(HDC hdc, HWND hwnd_edit_parent, HWND hwnd_edit, COLORREF col);
void    paint_rect(HDC hdc, RECT * rect);
void    draw_centered_text(HDC hdc, RECT *r, const TCHAR *txt);

BOOL    IsCursorOverWindow(HWND hwnd);
void    CenterDialog(HWND hDlg);


#endif
