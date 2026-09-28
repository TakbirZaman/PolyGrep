#pragma once
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include "threadforge/engine.hpp"

struct Hit {
    QString file;
    quint64 line = 0;
    QString text;
};
Q_DECLARE_METATYPE(Hit)

// Lives on its own QThread. The engine fans work out to a C++ thread pool;
// this class only batches results and hands them to the GUI thread via queued signals.
class SearchWorker : public QObject {
    Q_OBJECT
public:
    explicit SearchWorker(QObject* parent = nullptr) : QObject(parent) {}
    tf::Engine& engine() { return engine_; }
    void setOptions(tf::Options o) { options_ = std::move(o); }  // call while idle

public slots:
    void run();

signals:
    void hits(QList<Hit> batch);
    void finished(QString error);

private:
    tf::Engine engine_;
    tf::Options options_;
};
