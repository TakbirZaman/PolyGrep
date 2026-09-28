#include <afxwin.h>
#include <afxcmn.h>

#include "MainDlg.h"
#include "ThreadForgeApp.h"

CThreadForgeApp theApp;

BOOL CThreadForgeApp::InitInstance() {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);
    CWinApp::InitInstance();

    CMainDlg dlg;
    m_pMainWnd = &dlg;
    dlg.DoModal();
    return FALSE;  // no message pump: dialog-based app exits when the dialog closes
}
