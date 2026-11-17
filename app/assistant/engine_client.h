#pragma once

#include "assistant/chat_client.h"

#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace openoffice {

// Client for the built-in engine (llama.cpp's server, started by LocalEngine on
// a loopback port with a one-off key). Speaks its OpenAI-compatible chat API:
// streamed answers, tool calls and JSON-schema answers; never uses a proxy.
// With setCloud it talks to a cloud service's OpenAI-compatible API instead:
// the model is named in each request, the engine's own options (no reasoning
// switch, JSON schema) are left out, and the system's proxy settings apply.
class EngineClient : public ChatClient {
    Q_OBJECT

public:
    explicit EngineClient(QObject* parent = nullptr);

    void setEndpoint(const QUrl& base, const QString& apiKey);
    QUrl endpoint() const { return m_base; }
    // A cloud service, by the name people know it by (for messages); base is then
    // the API's base URL ("https://api.deepseek.com/v1").
    void setCloud(const QString& serviceName);
    bool cloud() const { return !m_service.isEmpty(); }
    // GET <base>/models: the model names the service offers -> modelsFetched.
    void fetchModels();

    quint64 chat(const QString& model, const QJsonArray& messages, const ChatOptions& options) override;
    void cancelChat() override;
    double lastTokensPerSecond() const override { return m_tokensPerSecond; }

    // The assistant's message shape -> OpenAI's: tool-call arguments become
    // JSON text with ids, and each tool result answers the matching id.
    static QJsonArray toOpenAiMessages(const QJsonArray& messages);
    // The request body for one chat turn (public for tests).
    static QJsonObject requestBody(const QJsonArray& messages, const ChatOptions& options);
    // For a cloud service: with the model's name, without the engine's own options.
    static QJsonObject cloudRequestBody(const QString& model, const QJsonArray& messages, const ChatOptions& options);
    // <base>/<path>: "https://x/v1" + "chat/completions" -> "https://x/v1/chat/completions".
    static QUrl serviceUrl(const QUrl& base, const QString& path);

signals:
    void modelsFetched(QStringList models, QString error);

private:
    QNetworkAccessManager* m_network = nullptr;
    QUrl m_base;
    QString m_key;
    QString m_service; // empty: the built-in engine
    QPointer<QNetworkReply> m_chat;
    quint64 m_chatId = 0;
    double m_tokensPerSecond = 0;
};

} // namespace openoffice
