#pragma once

#include "assistant/model_catalog.h"

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace openoffice {

// Downloads one catalog model from ModelScope (魔搭): asks for the file's size
// and SHA256 first, writes "<file>.part" (continued after an interruption),
// checks the size and SHA256, and only then renames it to the model file.
// Only the model file travels; nothing about the person or their documents.
class ModelDownloader : public QObject {
    Q_OBJECT

public:
    explicit ModelDownloader(QObject* parent = nullptr);

    // OPEN_OFFICE_MODEL_SOURCE (tests), else https://www.modelscope.cn
    static QUrl sourceBase();
    void start(const CatalogModel& model, const QString& folder);
    void cancel(); // keeps the .part file so the next start continues
    bool active() const { return !m_id.isEmpty(); }
    QString currentId() const { return m_id; }
    // "正在下载 Qwen3 4B：45%（1.1 / 2.5 GB）"
    QString progressText() const { return m_progress; }

signals:
    void progress(QString id, qint64 received, qint64 total);
    void finished(QString id, bool ok, QString message);

private:
    void download();
    void verify();
    void finish(bool ok, const QString& message);

    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    CatalogModel m_model;
    QString m_id;
    QString m_folder;
    QString m_progress;
    QString m_sha256;
    qint64 m_size = 0;
    qint64 m_resumeFrom = 0;
    QFile m_part;
    QElapsedTimer m_clock;
    bool m_cancelled = false;
};

} // namespace openoffice
