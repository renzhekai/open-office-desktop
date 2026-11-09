#include "model_downloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStorageInfo>
#include <QThread>
#include <QUrlQuery>

#include <memory>

namespace openoffice {
namespace {

QString gigabytes(qint64 bytes)
{
    return QString::number(bytes / double(1LL << 30), 'f', 1);
}

QString explainNetwork(QNetworkReply* reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 404) return QStringLiteral("模型库里找不到这个文件，可能已经改名。请更新 Open Office 后再试。");
    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::SslHandshakeFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyTimeoutError:
        return QStringLiteral("连不上魔搭（modelscope.cn）。请检查网络；开着代理的话，可以把 modelscope.cn 设为直连。"
                              "已下载的部分会保留，下次接着下。");
    default:
        return QStringLiteral("下载出错：") + reply->errorString() + QStringLiteral("。已下载的部分会保留，下次接着下。");
    }
}

} // namespace

ModelDownloader::ModelDownloader(QObject* parent) : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QUrl ModelDownloader::sourceBase()
{
    const QString configured = qEnvironmentVariable("OPEN_OFFICE_MODEL_SOURCE");
    return QUrl(configured.isEmpty() ? QStringLiteral("https://www.modelscope.cn") : configured);
}

void ModelDownloader::start(const CatalogModel& model, const QString& folder)
{
    if (active()) return;
    m_model = model;
    m_id = model.id;
    m_folder = folder;
    m_cancelled = false;
    m_progress = QStringLiteral("正在查询 %1 的文件信息…").arg(model.title);
    if (!QDir().mkpath(folder)) {
        finish(false, QStringLiteral("无法创建模型文件夹：") + QDir::toNativeSeparators(folder));
        return;
    }
    QUrl url = sourceBase();
    url.setPath(QStringLiteral("/api/v1/models/%1/repo/files").arg(model.repo));
    url.setQuery(QUrlQuery{{QStringLiteral("Revision"), QStringLiteral("master")}, {QStringLiteral("Recursive"), QStringLiteral("true")}});
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    QNetworkReply* reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_cancelled) {
            finish(false, QStringLiteral("已取消。"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            finish(false, explainNetwork(reply));
            return;
        }
        const QJsonArray files = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("Data")).toObject()
                                     .value(QStringLiteral("Files")).toArray();
        for (const QJsonValue& value : files) {
            const QJsonObject file = value.toObject();
            if (file.value(QStringLiteral("Name")).toString() != m_model.file) continue;
            m_size = static_cast<qint64>(file.value(QStringLiteral("Size")).toDouble());
            m_sha256 = file.value(QStringLiteral("Sha256")).toString().toLower();
        }
        if (m_size <= 0 || m_sha256.isEmpty()) {
            finish(false, QStringLiteral("模型库里找不到 %1，可能已经改名。请更新 Open Office 后再试。").arg(m_model.file));
            return;
        }
        const qint64 have = QFileInfo(QDir(m_folder).filePath(m_model.file + QStringLiteral(".part"))).size();
        const qint64 needed = m_size - qMin(have, m_size) + (512LL << 20);
        if (QStorageInfo(m_folder).bytesAvailable() < needed) {
            finish(false, QStringLiteral("磁盘空间不够：还需要约 %1 GB。请在设置里换一个空间更大的文件夹，或清理磁盘后再试。")
                              .arg(gigabytes(needed)));
            return;
        }
        download();
    });
}

void ModelDownloader::download()
{
    m_part.setFileName(QDir(m_folder).filePath(m_model.file + QStringLiteral(".part")));
    m_resumeFrom = m_part.exists() ? m_part.size() : 0;
    if (m_resumeFrom > m_size) { // not this file: start over
        m_part.remove();
        m_resumeFrom = 0;
    }
    if (m_resumeFrom == m_size) {
        verify();
        return;
    }
    if (!m_part.open(m_resumeFrom > 0 ? QIODevice::Append : QIODevice::WriteOnly)) {
        finish(false, QStringLiteral("无法写入模型文件夹：") + QDir::toNativeSeparators(m_folder));
        return;
    }
    QUrl url = sourceBase();
    url.setPath(QStringLiteral("/models/%1/resolve/master/%2").arg(m_model.repo, m_model.file));
    QNetworkRequest request(url);
    request.setTransferTimeout(60000); // inactivity limit: a stalled connection fails and can be resumed
    if (m_resumeFrom > 0) request.setRawHeader("Range", "bytes=" + QByteArray::number(m_resumeFrom) + "-");
    QNetworkReply* reply = m_network->get(request);
    m_reply = reply;
    m_clock.start();
    auto checked = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, checked] {
        if (!*checked) {
            *checked = true;
            // The server ignored the range: it sends the whole file again.
            if (m_resumeFrom > 0 && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
                m_part.resize(0);
                m_part.seek(0);
                m_resumeFrom = 0;
            }
        }
        const QByteArray data = reply->readAll();
        if (m_part.write(data) != data.size()) {
            m_cancelled = true; // stop; the reason is reported below
            reply->abort();
            finish(false, QStringLiteral("写入模型文件失败，磁盘可能满了。"));
            return;
        }
        const qint64 received = m_part.size();
        m_progress = QStringLiteral("正在下载 %1：%2%（%3 / %4 GB）")
                         .arg(m_model.title).arg(qRound(100.0 * received / m_size)).arg(gigabytes(received), gigabytes(m_size));
        emit progress(m_id, received, m_size);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!active()) return; // already reported (write failure)
        m_part.write(reply->readAll());
        m_part.close();
        if (m_cancelled) {
            finish(false, QStringLiteral("已暂停，下次会接着下载。"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            finish(false, explainNetwork(reply));
            return;
        }
        verify();
    });
}

void ModelDownloader::verify()
{
    const QString part = m_part.fileName();
    if (QFileInfo(part).size() != m_size) {
        // Shorter than announced: the connection ended early; continue next time.
        finish(false, QStringLiteral("下载没有完成（连接中断）。再点一次下载会接着下。"));
        return;
    }
    m_progress = QStringLiteral("正在核对 %1 是否完整…").arg(m_model.title);
    emit progress(m_id, m_size, m_size);
    auto digest = std::make_shared<QString>();
    QThread* worker = QThread::create([part, digest] {
        QFile file(part);
        if (!file.open(QIODevice::ReadOnly)) return;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (hash.addData(&file)) *digest = QString::fromLatin1(hash.result().toHex());
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, part, digest] {
        if (*digest != m_sha256) {
            QFile::remove(part);
            finish(false, QStringLiteral("文件核对没通过（下载时出了错），已删除，请重新下载。"));
            return;
        }
        const QString target = QDir(m_folder).filePath(m_model.file);
        QFile::remove(target);
        if (!QFile::rename(part, target)) {
            finish(false, QStringLiteral("下载好了，但改名失败：") + QDir::toNativeSeparators(target));
            return;
        }
        finish(true, QStringLiteral("%1 已下载并核对完毕。").arg(m_model.title));
    });
    worker->start();
}

void ModelDownloader::cancel()
{
    if (!active()) return;
    m_cancelled = true;
    if (m_reply) m_reply->abort();
}

void ModelDownloader::finish(bool ok, const QString& message)
{
    if (m_part.isOpen()) m_part.close();
    const QString id = m_id;
    m_id.clear();
    m_progress = message;
    emit finished(id, ok, message);
}

} // namespace openoffice
