#include "SearchWorker.h"

#include <QElapsedTimer>

void SearchWorker::run() {
    QList<Hit> batch;
    QElapsedTimer sinceFlush;
    sinceFlush.start();
    auto flush = [&] {
        if (!batch.isEmpty()) {
            emit hits(batch);  // queued: delivered on the GUI thread
            batch.clear();
        }
        sinceFlush.restart();
    };

    QString error;
    try {
        // The engine serialises this callback, so `batch` needs no extra lock.
        engine_.run(options_, [&](const std::string& file, std::uint64_t line, const std::string& text) {
            batch.append({QString::fromUtf8(file.data(), static_cast<qsizetype>(file.size())), line,
                          QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()))});
            if (batch.size() >= 200 || sinceFlush.elapsed() >= 50) flush();
        });
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
    }
    flush();
    emit finished(error);
}
