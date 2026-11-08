#pragma once

#include <QJsonArray>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace openoffice {

struct LocalModel {
    QString name;
    qint64 bytes = 0;
    QString family;
    QString parameterSize;
    bool tools = false;    // can call the assistant's commands
    bool thinking = false; // has a reasoning phase before answering
};

// Client for an Ollama server on this computer. It only ever talks to a
// loopback address, never through a proxy, and drops models that Ollama
// forwards to a cloud service, so prompts and document text stay local.
class OllamaClient : public QObject {
    Q_OBJECT

public:
    explicit OllamaClient(QObject* parent = nullptr);

    // OPEN_OFFICE_OLLAMA_URL, then OLLAMA_HOST, when they name a loopback
    // address; otherwise http://127.0.0.1:11434.
    static QUrl defaultBaseUrl();
    static bool isLoopback(const QUrl& url);
    QUrl baseUrl() const { return m_base; }

    void probe();      // -> probed
    void listModels(); // -> modelsListed
    // Streams one chat turn. think: "" omits the field; otherwise "low", "false"...
    quint64 chat(const QString& model, const QJsonArray& messages, const QJsonArray& tools, const QString& think);
    void cancelChat();
    void pull(const QString& model); // -> pullProgress*, pullFinished
    void cancelPull();
    bool pulling() const { return m_pull; }

signals:
    void probed(bool running, QString version);
    void modelsListed(QList<openoffice::LocalModel> models, QString error);
    void chatDelta(quint64 id, QString content);
    void chatFinished(quint64 id, bool ok, QString content, QJsonArray toolCalls, QString error);
    void pullProgress(QString status, qint64 completed, qint64 total);
    void pullFinished(bool ok, QString error);

private:
    QNetworkReply* post(const QString& path, const QByteArray& body, int idleTimeoutMs);
    QNetworkReply* get(const QString& path, int timeoutMs);

    QNetworkAccessManager* m_network = nullptr;
    QUrl m_base;
    QPointer<QNetworkReply> m_chat;
    QPointer<QNetworkReply> m_pull;
    quint64 m_chatId = 0;
};

// Total installed memory, used to recommend a model size.
qint64 totalPhysicalMemory();
// The best installed model for this assistant: one that can call commands,
// fits the memory, and handles Chinese well. Empty when none is usable.
QString pickInstalledModel(const QList<LocalModel>& models, qint64 memoryBytes);

struct ModelRecommendation {
    QString name;
    double approximateGigabytes = 0;
};
// A model to download for a computer with this much memory.
ModelRecommendation recommendDownload(qint64 memoryBytes);

} // namespace openoffice
