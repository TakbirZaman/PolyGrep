#pragma once
#include <afxcmn.h>
#include <afxdlgs.h>
#include <afxwin.h>

#include <vector>

#include "resource.h"
#include "threadforge/capi.h"

// Worker -> UI messages. lParam owns a heap object that the handler must delete.
constexpr UINT WM_TF_BATCH = WM_APP + 1;  // lParam: std::vector<HitRow>*
constexpr UINT WM_TF_DONE = WM_APP + 2;   // lParam: CString* error, or nullptr on success

struct HitRow {
    CString file, line, text;
};

struct WorkerCtx;  // defined in MainDlg.cpp

class CMainDlg : public CDialogEx {
public:
    explicit CMainDlg(CWnd* parent = nullptr);
    ~CMainDlg() override;
    enum { IDD = IDD_MAIN_DIALOG };

protected:
    void DoDataExchange(CDataExchange* pDX) override;
    BOOL OnInitDialog() override;
    DECLARE_MESSAGE_MAP()

    afx_msg void OnBrowse();
    afx_msg void OnSearch();
    afx_msg void OnCancelSearch();
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* mmi);
    afx_msg void OnTimer(UINT_PTR id);
    afx_msg void OnDestroy();
    afx_msg void OnGetDispInfo(NMHDR* hdr, LRESULT* result);
    afx_msg void OnItemActivate(NMHDR* hdr, LRESULT* result);
    afx_msg LRESULT OnBatch(WPARAM, LPARAM lp);
    afx_msg LRESULT OnDone(WPARAM, LPARAM lp);

private:
    void SetRunning(bool running);
    void UpdateStatus();
    void JoinWorker();
    void DrainPostedMessages();

    // DDX
    CString m_root, m_pattern, m_ext;
    int m_regex = 0, m_case = 0;
    UINT m_threads = 0, m_max = 50000;

    CListCtrl m_list;
    std::vector<HitRow> hits_;
    tf_handle handle_ = nullptr;
    CWinThread* worker_ = nullptr;
    WorkerCtx* ctx_ = nullptr;
    int margin_ = 7;
    CSize minSize_;
};
