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
class EngineClient : public ChatClient {
    Q_OBJECT

public:
    explicit EngineClient(QObject* parent = nullptr);

    void setEndpoint(const QUrl& base, const QString& apiKey);
    QUrl endpoint() const { return m_base; }

    quint64 chat(const QString& model, const QJsonArray& messages, const ChatOptions& options) override;
    void cancelChat() override;
    double lastTokensPerSecond() const override { return m_tokensPerSecond; }

    // The assistant's message shape -> OpenAI's: tool-call arguments become
    // JSON text with ids, and each tool result answers the matching id.
    static QJsonArray toOpenAiMessages(const QJsonArray& messages);
    // The request body for one chat turn (public for tests).
    static QJsonObject requestBody(const QJsonArray& messages, const ChatOptions& options);

private:
    QNetworkAccessManager* m_network = nullptr;
    QUrl m_base;
    QString m_key;
    QPointer<QNetworkReply> m_chat;
    quint64 m_chatId = 0;
    double m_tokensPerSecond = 0;
};

} // namespace openoffice
