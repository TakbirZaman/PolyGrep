#include "MainDlg.h"

#include <iterator>
#include <memory>
#include <string>

namespace {
constexpr UINT_PTR kStatsTimer = 1;

CString FromUtf8(const char* s) {
    CString out;
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s, -1, out.GetBuffer(n - 1), n);
        out.ReleaseBuffer(n - 1);
    }
    return out;
}

std::string ToUtf8(const CString& s) {
    std::string out;
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n > 1) {
        out.resize(static_cast<size_t>(n) - 1);
        WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    }
    return out;
}
}  // namespace

// Everything the worker thread needs; owned by the dialog, freed after the thread is joined.
struct WorkerCtx {
    HWND hwnd = nullptr;
    tf_handle handle = nullptr;
    std::string root, pattern, ext;
    tf_options opts{};
    std::vector<HitRow> batch;   // touched only from the engine's (serialised) callback
    ULONGLONG lastFlush = 0;
};

static void FlushBatch(WorkerCtx* c) {
    if (c->batch.empty()) return;
    auto* v = new std::vector<HitRow>(std::move(c->batch));
    c->batch.clear();
    if (!::PostMessage(c->hwnd, WM_TF_BATCH, 0, reinterpret_cast<LPARAM>(v))) delete v;
    c->lastFlush = GetTickCount64();
}

// Called by the native engine on its pool threads (serialised by the engine).
static void MatchThunk(const char* file, uint64_t line, const char* text, void* user) {
    auto* c = static_cast<WorkerCtx*>(user);
    CString l;
    l.Format(_T("%llu"), static_cast<unsigned long long>(line));
    c->batch.push_back({FromUtf8(file), l, FromUtf8(text)});
    if (c->batch.size() >= 200 || GetTickCount64() - c->lastFlush >= 50) FlushBatch(c);
}

// MFC worker thread (AfxBeginThread): runs the blocking native scan, then reports back.
static UINT AFX_CDECL WorkerProc(LPVOID param) {
    auto* c = static_cast<WorkerCtx*>(param);
    c->lastFlush = GetTickCount64();
    const int rc = tf_run(c->handle, &c->opts, &MatchThunk, c);
    FlushBatch(c);
    CString* err = rc != 0 ? new CString(FromUtf8(tf_last_error(c->handle))) : nullptr;
    if (!::PostMessage(c->hwnd, WM_TF_DONE, 0, reinterpret_cast<LPARAM>(err))) delete err;
    return 0;
}

BEGIN_MESSAGE_MAP(CMainDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_BROWSE, &CMainDlg::OnBrowse)
    ON_BN_CLICKED(IDC_BTN_SEARCH, &CMainDlg::OnSearch)
    ON_BN_CLICKED(IDC_BTN_CANCEL, &CMainDlg::OnCancelSearch)
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST_RESULTS, &CMainDlg::OnGetDispInfo)
    ON_NOTIFY(NM_DBLCLK, IDC_LIST_RESULTS, &CMainDlg::OnItemActivate)
    ON_MESSAGE(WM_TF_BATCH, &CMainDlg::OnBatch)
    ON_MESSAGE(WM_TF_DONE, &CMainDlg::OnDone)
END_MESSAGE_MAP()

CMainDlg::CMainDlg(CWnd* parent) : CDialogEx(IDD_MAIN_DIALOG, parent) {
    TCHAR cwd[MAX_PATH] = {};
    GetCurrentDirectory(MAX_PATH, cwd);
    m_root = cwd;
    m_ext = _T("cpp,h,hpp,cs,py,js,ts,txt,md");
    handle_ = tf_create();
}

CMainDlg::~CMainDlg() {
    if (handle_) tf_destroy(handle_);
}

void CMainDlg::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_EDIT_ROOT, m_root);
    DDX_Text(pDX, IDC_EDIT_PATTERN, m_pattern);
    DDX_Text(pDX, IDC_EDIT_EXT, m_ext);
    DDX_Check(pDX, IDC_CHK_REGEX, m_regex);
    DDX_Check(pDX, IDC_CHK_CASE, m_case);
    DDX_Text(pDX, IDC_EDIT_THREADS, m_threads);
    DDX_Text(pDX, IDC_EDIT_MAX, m_max);
    DDX_Control(pDX, IDC_LIST_RESULTS, m_list);
}

BOOL CMainDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    m_list.InsertColumn(0, _T("File"), LVCFMT_LEFT, 300);
    m_list.InsertColumn(1, _T("Line"), LVCFMT_RIGHT, 55);
    m_list.InsertColumn(2, _T("Text"), LVCFMT_LEFT, 600);

    CRect r;
    m_list.GetWindowRect(&r);
    ScreenToClient(&r);
    margin_ = r.left;
    CRect w;
    GetWindowRect(&w);
    minSize_ = w.Size();
    return TRUE;
}

void CMainDlg::OnGetMinMaxInfo(MINMAXINFO* mmi) {
    mmi->ptMinTrackSize.x = minSize_.cx;
    mmi->ptMinTrackSize.y = minSize_.cy;
    CDialogEx::OnGetMinMaxInfo(mmi);
}

void CMainDlg::OnSize(UINT type, int cx, int cy) {
    CDialogEx::OnSize(type, cx, cy);
    if (!m_list.GetSafeHwnd() || type == SIZE_MINIMIZED) return;

    // Right-hand buttons hug the right edge; folder box stretches; list fills the rest.
    int buttonLeft = cx - margin_;
    for (UINT id : {IDC_BTN_BROWSE, IDC_BTN_SEARCH, IDC_BTN_CANCEL}) {
        CWnd* b = GetDlgItem(id);
        CRect br;
        b->GetWindowRect(&br);
        ScreenToClient(&br);
        br.MoveToX(cx - margin_ - br.Width());
        b->MoveWindow(&br);
        buttonLeft = br.left;
    }
    CWnd* root = GetDlgItem(IDC_EDIT_ROOT);
    CRect rr;
    root->GetWindowRect(&rr);
    ScreenToClient(&rr);
    rr.right = buttonLeft - 6;
    root->MoveWindow(&rr);

    CWnd* status = GetDlgItem(IDC_STATIC_STATUS);
    CRect sr;
    status->GetWindowRect(&sr);
    ScreenToClient(&sr);
    sr.MoveToY(cy - sr.Height() - margin_);
    sr.right = cx - margin_;
    status->MoveWindow(&sr);

    CRect lr;
    m_list.GetWindowRect(&lr);
    ScreenToClient(&lr);
    lr.right = cx - margin_;
    lr.bottom = sr.top - margin_ / 2;
    m_list.MoveWindow(&lr);
}

void CMainDlg::OnBrowse() {
    UpdateData(TRUE);
    CFolderPickerDialog dlg(m_root, 0, this);
    if (dlg.DoModal() == IDOK) {
        m_root = dlg.GetPathName();
        UpdateData(FALSE);
    }
}

void CMainDlg::OnSearch() {
    if (worker_) return;
    if (!UpdateData(TRUE)) return;
    if (m_pattern.IsEmpty()) {
        SetDlgItemText(IDC_STATIC_STATUS, _T("Enter a pattern first"));
        return;
    }

    hits_.clear();
    m_list.SetItemCountEx(0);

    ctx_ = new WorkerCtx;
    ctx_->hwnd = m_hWnd;
    ctx_->handle = handle_;
    ctx_->root = ToUtf8(m_root);
    ctx_->pattern = ToUtf8(m_pattern);
    ctx_->ext = ToUtf8(m_ext);
    ctx_->opts.root = ctx_->root.c_str();
    ctx_->opts.pattern = ctx_->pattern.c_str();
    ctx_->opts.extensions = ctx_->ext.c_str();
    ctx_->opts.max_matches = m_max;
    ctx_->opts.threads = m_threads;
    ctx_->opts.use_regex = m_regex == BST_CHECKED;
    ctx_->opts.case_sensitive = m_case == BST_CHECKED;

    worker_ = AfxBeginThread(&WorkerProc, ctx_, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
    if (!worker_) {
        delete ctx_;
        ctx_ = nullptr;
        AfxMessageBox(_T("Could not start worker thread."), MB_ICONERROR);
        return;
    }
    worker_->m_bAutoDelete = FALSE;  // we join and delete it ourselves
    worker_->ResumeThread();
    SetRunning(true);
    SetTimer(kStatsTimer, 150, nullptr);
}

void CMainDlg::OnCancelSearch() {
    tf_cancel(handle_);  // atomic flag inside the engine: safe from the UI thread
}

void CMainDlg::OnTimer(UINT_PTR id) {
    if (id == kStatsTimer) UpdateStatus();
    CDialogEx::OnTimer(id);
}

void CMainDlg::UpdateStatus() {
    tf_stats s{};
    tf_get_stats(handle_, &s);
    CString t;
    t.Format(_T("%llu matches  |  %llu files (%llu skipped)  |  %.1f MiB  |  %llu ms%s"),
             s.matches, s.files_scanned, s.files_skipped, static_cast<double>(s.bytes_read) / 1048576.0, s.elapsed_ms,
             s.running ? _T("  |  scanning...") : _T(""));
    SetDlgItemText(IDC_STATIC_STATUS, t);
}

void CMainDlg::SetRunning(bool running) {
    GetDlgItem(IDC_BTN_SEARCH)->EnableWindow(!running);
    GetDlgItem(IDC_BTN_BROWSE)->EnableWindow(!running);
    GetDlgItem(IDC_BTN_CANCEL)->EnableWindow(running);
    for (UINT id : {IDC_EDIT_ROOT, IDC_EDIT_PATTERN, IDC_EDIT_EXT}) GetDlgItem(id)->EnableWindow(!running);
}

LRESULT CMainDlg::OnBatch(WPARAM, LPARAM lp) {
    std::unique_ptr<std::vector<HitRow>> v(reinterpret_cast<std::vector<HitRow>*>(lp));
    hits_.insert(hits_.end(), std::make_move_iterator(v->begin()), std::make_move_iterator(v->end()));
    m_list.SetItemCountEx(static_cast<int>(hits_.size()), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    return 0;
}

LRESULT CMainDlg::OnDone(WPARAM, LPARAM lp) {
    std::unique_ptr<CString> err(reinterpret_cast<CString*>(lp));
    JoinWorker();
    KillTimer(kStatsTimer);
    UpdateStatus();
    SetRunning(false);
    if (err) AfxMessageBox(*err, MB_ICONWARNING);
    return 0;
}

void CMainDlg::JoinWorker() {
    if (!worker_) return;
    ::WaitForSingleObject(worker_->m_hThread, INFINITE);
    delete worker_;
    worker_ = nullptr;
    delete ctx_;
    ctx_ = nullptr;
}

void CMainDlg::DrainPostedMessages() {
    MSG msg;
    while (::PeekMessage(&msg, m_hWnd, WM_TF_BATCH, WM_TF_DONE, PM_REMOVE)) {
        if (msg.message == WM_TF_BATCH) delete reinterpret_cast<std::vector<HitRow>*>(msg.lParam);
        else if (msg.message == WM_TF_DONE) delete reinterpret_cast<CString*>(msg.lParam);
    }
}

void CMainDlg::OnDestroy() {
    if (worker_) {
        tf_cancel(handle_);
        JoinWorker();          // never free the engine while a scan is inside it
    }
    DrainPostedMessages();     // free heap payloads that will never be delivered
    KillTimer(kStatsTimer);
    CDialogEx::OnDestroy();
}

void CMainDlg::OnGetDispInfo(NMHDR* hdr, LRESULT* result) {
    *result = 0;
    auto* di = reinterpret_cast<NMLVDISPINFO*>(hdr);
    if (!(di->item.mask & LVIF_TEXT)) return;
    if (di->item.iItem < 0 || static_cast<size_t>(di->item.iItem) >= hits_.size()) return;
    const HitRow& h = hits_[static_cast<size_t>(di->item.iItem)];
    const CString& s = di->item.iSubItem == 0 ? h.file : di->item.iSubItem == 1 ? h.line : h.text;
    _tcsncpy_s(di->item.pszText, static_cast<size_t>(di->item.cchTextMax), s, _TRUNCATE);
}

void CMainDlg::OnItemActivate(NMHDR* hdr, LRESULT* result) {
    *result = 0;
    auto* a = reinterpret_cast<NMITEMACTIVATE*>(hdr);
    if (a->iItem >= 0 && static_cast<size_t>(a->iItem) < hits_.size())
        ShellExecute(m_hWnd, _T("open"), hits_[static_cast<size_t>(a->iItem)].file, nullptr, nullptr, SW_SHOWNORMAL);
}
