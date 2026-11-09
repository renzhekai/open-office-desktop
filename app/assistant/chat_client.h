#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace openoffice {

// A model the assistant can use, whichever engine runs it.
struct LocalModel {
    QString name;
    qint64 bytes = 0;
    QString family;
    QString parameterSize;
    bool tools = false;    // can call the assistant's commands
    bool thinking = false; // has a reasoning phase before answering
};

// One chat request's settings besides the model and the messages.
struct ChatOptions {
    QJsonArray tools;      // functions the model may call; empty: none
    QString think;         // "" leaves it to the model; "false" turns reasoning off; "low" keeps it short
    QJsonObject format;    // a JSON schema for structured output; empty: free text
    int contextLength = 0; // Ollama num_ctx; the built-in engine sets its context when it starts
    double temperature = 0.3;
};

// What the assistant needs from an engine: one streamed chat turn at a time.
// Messages use one shape for both engines: assistant tool calls are
// {"function": {"name", "arguments"}} and tool results carry "tool_name".
class ChatClient : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual quint64 chat(const QString& model, const QJsonArray& messages, const ChatOptions& options) = 0;
    virtual void cancelChat() = 0;
    // Generation speed of the last finished chat (tokens per second); 0 when unknown.
    virtual double lastTokensPerSecond() const = 0;

signals:
    void chatDelta(quint64 id, QString content);
    void chatFinished(quint64 id, bool ok, QString content, QJsonArray toolCalls, QString error);
};

} // namespace openoffice
