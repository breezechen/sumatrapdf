/* Copyright 2006-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#ifndef APP_TOOLS_H_
#define APP_TOOLS_H_

// Class to encapsulate code that has to be executed on UI thread but can be
// triggered from other threads. Instead of defining a new message for
// each piece of code, derive from UIThreadWorkItem and implement
// Execute() method
class UIThreadWorkItem
{
    static UINT msg;
    static void RegisterMsgIfNecessary()
    {
        if (0 == msg)
            msg = RegisterWindowMessageA("SUMATRA_THREAD_WORK_ITEM_MSG");
    }

public:
    HWND hwnd;

    UIThreadWorkItem(HWND hwnd) : hwnd(hwnd) {
        RegisterMsgIfNecessary();
    }
    virtual ~UIThreadWorkItem() {}

    void MarshallOnUIThread() {
        PostMessage(hwnd, UIThreadWorkItem::msg, (WPARAM)this, 0);
    }

    virtual void Execute() = 0;

    static bool Process(MSG *msg) {
        if (msg->message == UIThreadWorkItem::msg) {
            UIThreadWorkItem *me = (UIThreadWorkItem*)msg->wParam;
            me->Execute();
            delete me;
            return true;
        }
        return false;
    }
};

bool ValidProgramVersion(char *txt);
int CompareVersion(TCHAR *txt1, TCHAR *txt2);
const char *GuessLanguage();

TCHAR *ExePathGet();
bool IsRunningInPortableMode();
TCHAR *AppGenDataFilename(TCHAR *pFilename);
void AdjustRemovableDriveLetter(TCHAR *path);

void DoAssociateExeWithPdfExtension(HKEY hkey);
bool IsExeAssociatedWithPdfExtension();

bool GetAcrobatPath(TCHAR *bufOut=NULL, int bufCchSize=0);
bool GetFoxitPath(TCHAR *buffer=NULL, int bufCchSize=0);
bool GetPDFXChangePath(TCHAR *bufOut=NULL, int bufCchSize=0);

LPTSTR AutoDetectInverseSearchCommands(HWND hwndCombo=NULL);
void DDEExecute(LPCTSTR server, LPCTSTR topic, LPCTSTR command);

HFONT Win32_Font_GetSimple(HDC hdc, TCHAR *fontName, int fontSize);
void Win32_Font_Delete(HFONT font);

#endif
