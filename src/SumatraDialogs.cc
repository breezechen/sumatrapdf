#include <assert.h>

#include "SumatraDialogs.h"

#include "BaseUtils.h"
#include "dstring.h"
#include "DisplayModel.h"

#include "Resource.h"

static BOOL CALLBACK Dialog_GetPassword_Proc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND                       edit;
    HWND                       label;
    DString                    ds;
    Dialog_GetPassword_Data *  data;

    switch (message)
    {
        case WM_INITDIALOG:
            /* TODO: intelligently center the dialog within the parent window? */
            data = (Dialog_GetPassword_Data*)lParam;
            assert(data);
            assert(data->fileName);
            assert(!data->pwdOut);
            SetWindowLongPtr(hDlg, GWL_USERDATA, (LONG_PTR)data);
            DStringInit(&ds);
            DStringSprintf(&ds, "Enter password for %s", data->fileName);
            label = GetDlgItem(hDlg, IDC_GET_PASSWORD_LABEL);
            WinSetText(label, ds.pString);
            DStringFree(&ds);
            edit = GetDlgItem(hDlg, IDC_GET_PASSWORD_EDIT);
            WinSetText(edit, "");
            SetFocus(edit);
            return FALSE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                    data = (Dialog_GetPassword_Data*)GetWindowLongPtr(hDlg, GWL_USERDATA);
                    assert(data);
                    edit = GetDlgItem(hDlg, IDC_GET_PASSWORD_EDIT);
                    data->pwdOut = WinGetText(edit);
                    EndDialog(hDlg, DIALOG_OK_PRESSED);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hDlg, DIALOG_CANCEL_PRESSED);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

/* Shows a 'get password' dialog for a given file.
   Returns a password entered by user as a newly allocated string or
   NULL if user cancelled the dialog or there was an error.
   Caller needs to free() the result.
   TODO: should I get rid of fileName and get it from win? */
char *Dialog_GetPassword(WindowInfo *win, const char *fileName)
{
    int                     dialogResult;
    Dialog_GetPassword_Data data;
    
    assert(fileName);
    if (!fileName) return NULL;

    data.fileName = fileName;
    data.pwdOut = NULL;
    dialogResult = DialogBoxParam(NULL, MAKEINTRESOURCE(IDD_DIALOG_GET_PASSWORD), win->hwndFrame, Dialog_GetPassword_Proc, (LPARAM)&data);
    if (DIALOG_OK_PRESSED == dialogResult) {
        return data.pwdOut;
    }
    free((void*)data.pwdOut);
    return NULL;
}

static BOOL CALLBACK Dialog_GoToPage_Proc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND                    editPageNo;
    HWND                    labelOfPages;
    DString                 ds;
    TCHAR *                 newPageNoTxt;
    Dialog_GoToPage_Data *  data;

    switch (message)
    {
        case WM_INITDIALOG:
            /* TODO: intelligently center the dialog within the parent window? */
            data = (Dialog_GoToPage_Data*)lParam;
            assert(NULL != data);
            SetWindowLongPtr(hDlg, GWL_USERDATA, (LONG_PTR)data);
            assert(INVALID_PAGE_NUM != data->currPageNo);
            assert(data->pageCount >= 1);
            DStringInit(&ds);
            DStringSprintf(&ds, "%d", data->currPageNo);
            editPageNo = GetDlgItem(hDlg, IDC_GOTO_PAGE_EDIT);
            WinSetText(editPageNo, ds.pString);
            DStringFree(&ds);
            DStringSprintf(&ds, "(of %d)", data->pageCount);
            labelOfPages = GetDlgItem(hDlg, IDC_GOTO_PAGE_LABEL_OF);
            WinSetText(labelOfPages, ds.pString);
            DStringFree(&ds);
            WinEditSelectAll(editPageNo);
            SetFocus(editPageNo);
            return FALSE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                    data = (Dialog_GoToPage_Data*)GetWindowLongPtr(hDlg, GWL_USERDATA);
                    assert(data);
                    data->pageEnteredOut = INVALID_PAGE_NUM;
                    editPageNo = GetDlgItem(hDlg, IDC_GOTO_PAGE_EDIT);
                    newPageNoTxt = WinGetText(editPageNo);
                    if (newPageNoTxt) {
                        data->pageEnteredOut = atoi(newPageNoTxt);
                        free((void*)newPageNoTxt);
                    }
                    EndDialog(hDlg, DIALOG_OK_PRESSED);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hDlg, DIALOG_CANCEL_PRESSED);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

/* Shows a 'go to page' dialog and returns a page number entered by the user
   or INVALID_PAGE_NUM if user clicked "cancel" button, entered invalid
   page number or there was an error. */
int Dialog_GoToPage(WindowInfo *win)
{
    int                     dialogResult;
    Dialog_GoToPage_Data    data;
    
    assert(win);
    if (!win) return INVALID_PAGE_NUM;

    data.currPageNo = win->dm->startPage;
    data.pageCount = win->dm->pageCount;
    dialogResult = DialogBoxParam(NULL, MAKEINTRESOURCE(IDD_DIALOG_GOTO_PAGE), win->hwndFrame, Dialog_GoToPage_Proc, (LPARAM)&data);
    if (DIALOG_OK_PRESSED == dialogResult) {
        if (win->dm->ValidPageNo(data.pageEnteredOut)) {
            return data.pageEnteredOut;
        }
    }
    return INVALID_PAGE_NUM;
}

