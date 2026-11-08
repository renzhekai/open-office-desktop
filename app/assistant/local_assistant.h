#pragma once

#include "assistant/assistant_commands.h"
#include "assistant/ollama_client.h"

#include <QJsonArray>
#include <QObject>

namespace openoffice {

enum class LocalAiState { Unknown, Checking, NotInstalled, NotRunning, NoModel, Downloading, Ready, Thinking };

// The local AI behind the assistant ball: finds Ollama on this computer,
// chooses a suitable installed model, and runs a conversation in which the
// model may call the assistant's commands as tools. Every model-proposed
// command goes back to the window (which validates it, may ask the person
// to confirm, and executes it); its outcome returns to the model.
class LocalAssistant : public QObject {
    Q_OBJECT

public:
    explicit LocalAssistant(QObject* parent = nullptr);

    OllamaClient* client() const { return m_client; }
    LocalAiState state() const { return m_state; }
    QString model() const { return m_model; }
    QList<LocalModel> models() const { return m_models; }
    qint64 memory() const { return m_memory; }
    ModelRecommendation recommendation() const { return recommendDownload(m_memory); }
    QString statusText() const;
    QString downloadStatus() const { return m_downloadStatus; }

    void refresh(); // detect Ollama and its models; -> stateChanged
    void setModel(const QString& name); // remembered for next time
    // Starts a reply to the person's message. False when the AI is not ready.
    bool ask(const QString& text, const QString& documentContext);
    void commandFinished(const AssistantCommand& command, const CommandOutcome& outcome);
    void commandDeclined(const AssistantCommand& command);
    void downloadRecommended(); // only on the person's explicit request
    void cancel();

    // Where the Ollama desktop app is installed, if anywhere.
    static QString ollamaAppPath();
    bool launchOllama();

    // The command catalog as Ollama tools (names use '_' instead of '.').
    static QJsonArray tools();
    static QString toolName(const QString& command);
    static QString commandName(const QString& tool);

signals:
    void stateChanged();
    void replyStarted();
    void replyDelta(QString text);
    void replyFinished(QString text, bool ok);
    void commandProposed(openoffice::AssistantCommand command);
    void downloadProgress(QString text);

private:
    void setState(LocalAiState state);
    void send();
    LocalModel currentModel() const;

    OllamaClient* m_client = nullptr;
    LocalAiState m_state = LocalAiState::Unknown;
    QList<LocalModel> m_models;
    QString m_model;
    qint64 m_memory = 0;
    QJsonArray m_history; // user/assistant/tool messages; the system prompt is added per request
    QString m_context;
    QString m_pendingTool;
    quint64 m_chatId = 0;
    int m_rounds = 0;
    QString m_downloadStatus;
};

} // namespace openoffice
