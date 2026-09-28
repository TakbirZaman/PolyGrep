#include "MainWindow.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QDir>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("PolyGrep - Qt");
    resize(980, 640);

    root_ = new QLineEdit(QDir::currentPath());
    auto* browseBtn = new QPushButton("Browse...");
    pattern_ = new QLineEdit;
    pattern_->setPlaceholderText("text or regex to find");
    ext_ = new QLineEdit("cpp,h,hpp,cs,py,js,ts,txt,md");
    regex_ = new QCheckBox("Regex");
    case_ = new QCheckBox("Case sensitive");
    threads_ = new QSpinBox;
    threads_->setRange(0, 256);
    threads_->setSpecialValueText("auto");
    maxResults_ = new QSpinBox;
    maxResults_->setRange(0, 1000000);
    maxResults_->setValue(50000);
    maxResults_->setSpecialValueText("unlimited");
    searchBtn_ = new QPushButton("Search");
    searchBtn_->setDefault(true);
    cancelBtn_ = new QPushButton("Cancel");
    cancelBtn_->setEnabled(false);

    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({"File", "Line", "Text"});
    table_ = new QTableView;
    table_->setModel(model_);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 380);
    table_->setColumnWidth(1, 60);

    status_ = new QLabel("Ready");
    statusBar()->addWidget(status_, 1);

    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel("Folder:"), 0, 0);
    grid->addWidget(root_, 0, 1, 1, 5);
    grid->addWidget(browseBtn, 0, 6);
    grid->addWidget(new QLabel("Pattern:"), 1, 0);
    grid->addWidget(pattern_, 1, 1, 1, 3);
    grid->addWidget(new QLabel("Ext:"), 1, 4);
    grid->addWidget(ext_, 1, 5);
    grid->addWidget(searchBtn_, 1, 6);
    grid->addWidget(regex_, 2, 1);
    grid->addWidget(case_, 2, 2);
    grid->addWidget(new QLabel("Threads:"), 2, 3, Qt::AlignRight);
    grid->addWidget(threads_, 2, 4);
    grid->addWidget(new QLabel("Max:"), 2, 5, Qt::AlignRight);
    grid->addWidget(maxResults_, 2, 6);
    grid->addWidget(cancelBtn_, 3, 6);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(5, 1);

    auto* central = new QWidget;
    auto* v = new QVBoxLayout(central);
    v->addLayout(grid);
    v->addWidget(table_, 1);
    setCentralWidget(central);

    // Worker thread setup: QObject::moveToThread pattern.
    qRegisterMetaType<QList<Hit>>("QList<Hit>");
    worker_ = new SearchWorker;
    worker_->moveToThread(&thread_);
    thread_.start();

    connect(worker_, &SearchWorker::hits, this, &MainWindow::appendHits, Qt::QueuedConnection);
    connect(worker_, &SearchWorker::finished, this, &MainWindow::onFinished, Qt::QueuedConnection);
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::browse);
    connect(searchBtn_, &QPushButton::clicked, this, &MainWindow::startSearch);
    connect(pattern_, &QLineEdit::returnPressed, this, &MainWindow::startSearch);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::cancelSearch);
    connect(table_, &QTableView::doubleClicked, this, &MainWindow::openRow);
    connect(&statsTimer_, &QTimer::timeout, this, &MainWindow::updateStats);
    statsTimer_.setInterval(150);
}

MainWindow::~MainWindow() {
    worker_->engine().cancel();
    thread_.quit();
    thread_.wait();
    delete worker_;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    worker_->engine().cancel();
    QMainWindow::closeEvent(e);
}

void MainWindow::browse() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Choose folder", root_->text());
    if (!dir.isEmpty()) root_->setText(dir);
}

void MainWindow::startSearch() {
    if (!searchBtn_->isEnabled()) return;
    if (pattern_->text().isEmpty()) {
        status_->setText("Enter a pattern first");
        return;
    }
    tf::Options o;
    o.root = root_->text().toUtf8().toStdString();
    o.pattern = pattern_->text().toUtf8().toStdString();
    o.use_regex = regex_->isChecked();
    o.case_sensitive = case_->isChecked();
    o.threads = static_cast<unsigned>(threads_->value());
    o.max_matches = static_cast<std::uint64_t>(maxResults_->value());
    for (const QString& e : ext_->text().split(',', Qt::SkipEmptyParts)) o.extensions.push_back(e.trimmed().toStdString());

    model_->removeRows(0, model_->rowCount());
    worker_->setOptions(std::move(o));
    setRunning(true);
    statsTimer_.start();
    QMetaObject::invokeMethod(worker_, &SearchWorker::run, Qt::QueuedConnection);
}

void MainWindow::cancelSearch() { worker_->engine().cancel(); }  // atomic flag: safe from GUI thread

void MainWindow::appendHits(const QList<Hit>& batch) {
    QList<QList<QStandardItem*>> rows;
    rows.reserve(batch.size());
    for (const Hit& h : batch) {
        auto* line = new QStandardItem(QString::number(h.line));
        line->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rows.append({new QStandardItem(h.file), line, new QStandardItem(h.text.trimmed())});
    }
    for (const auto& r : rows) model_->appendRow(r);
}

void MainWindow::onFinished(const QString& error) {
    statsTimer_.stop();
    updateStats();
    setRunning(false);
    if (!error.isEmpty()) {
        status_->setText("Error: " + error);
        QMessageBox::warning(this, "PolyGrep", error);
    }
}

void MainWindow::updateStats() {
    const tf::Stats s = worker_->engine().stats();
    status_->setText(QString("%1 matches  |  %2 files (%3 skipped)  |  %4 MiB  |  %5 ms%6")
                         .arg(s.matches).arg(s.files_scanned).arg(s.files_skipped)
                         .arg(s.bytes_read / 1048576.0, 0, 'f', 1).arg(s.elapsed_ms)
                         .arg(s.running ? "  |  scanning..." : ""));
}

void MainWindow::openRow(const QModelIndex& index) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(model_->item(index.row(), 0)->text()));
}

void MainWindow::setRunning(bool running) {
    searchBtn_->setEnabled(!running);
    cancelBtn_->setEnabled(running);
    root_->setEnabled(!running);
    pattern_->setEnabled(!running);
}
