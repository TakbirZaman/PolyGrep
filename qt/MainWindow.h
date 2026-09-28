#pragma once
#include <QMainWindow>
#include <QThread>
#include <QTimer>

#include "SearchWorker.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStandardItemModel;
class QTableView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void browse();
    void startSearch();
    void cancelSearch();
    void appendHits(const QList<Hit>& batch);
    void onFinished(const QString& error);
    void updateStats();
    void openRow(const QModelIndex& index);

private:
    void setRunning(bool running);

    QLineEdit *root_, *pattern_, *ext_;
    QCheckBox *regex_, *case_;
    QSpinBox *threads_, *maxResults_;
    QPushButton *searchBtn_, *cancelBtn_;
    QTableView* table_;
    QStandardItemModel* model_;
    QLabel* status_;
    QTimer statsTimer_;
    QThread thread_;
    SearchWorker* worker_;
};
