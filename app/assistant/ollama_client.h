#pragma once

#include "assistant/chat_client.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace openoffice {

// Client for an Ollama server on this computer (optional: the assistant uses
// its built-in engine unless the person chooses Ollama in the AI settings). It only ever talks to a
// loopback address, never through a proxy, and drops models that Ollama
// forwards to a cloud service, so prompts and document text stay local.
class OllamaClient : public ChatClient {
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
    // Streams one chat turn; the model stays loaded for a while afterwards.
    quint64 chat(const QString& model, const QJsonArray& messages, const ChatOptions& options) override;
    // Loads the model in the background so the first answer comes sooner.
    void warmUp(const QString& model);
    void cancelChat() override;
    void pull(const QString& model); // -> pullProgress*, pullFinished
    void cancelPull();
    bool pulling() const { return m_pull; }
    // Generation speed of the last finished chat (tokens per second); 0 when unknown.
    double lastTokensPerSecond() const override { return m_tokensPerSecond; }

signals:
    void probed(bool running, QString version);
    void modelsListed(QList<openoffice::LocalModel> models, QString error);
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
    double m_tokensPerSecond = 0;
};

// Total installed memory, used to recommend a model size.
qint64 totalPhysicalMemory();
// Dedicated memory of the largest graphics card; 0 when there is none or it is
// too small to run a model (integrated graphics share the system memory).
qint64 totalVideoMemory();
// The largest graphics card: its name (to pick it in the engine) and dedicated memory.
struct GpuInfo {
    QString name;
    qint64 bytes = 0;
};
GpuInfo largestGpu();
// A model answers quickly only when it fits the graphics card entirely; without
// one, only small models are fast enough on the processor.
bool runsSlowly(const LocalModel& model, qint64 videoBytes);
// The best installed model for this assistant: one that can call commands,
// answers quickly on this computer, and handles Chinese well. Models that
// always reason before answering (gpt-oss) count less. Empty when none is usable.
QString pickInstalledModel(const QList<LocalModel>& models, qint64 memoryBytes, qint64 videoBytes = 0);

struct ModelRecommendation {
    QString name;
    double approximateGigabytes = 0;
};
// A model to download for this computer: as large as fits the graphics card
// (up to qwen3:8b); without one, a small model the processor runs quickly.
ModelRecommendation recommendDownload(qint64 memoryBytes, qint64 videoBytes = 0);
// What a failed download means, in words a beginner can act on.
QString explainPullError(const QString& error);

} // namespace openoffice
