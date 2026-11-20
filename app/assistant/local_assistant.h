#pragma once

#include "assistant/assistant_commands.h"
#include "assistant/assistant_tasks.h"
#include "assistant/cloud_models.h"
#include "assistant/engine_client.h"
#include "assistant/local_engine.h"
#include "assistant/model_catalog.h"
#include "assistant/model_downloader.h"
#include "assistant/ollama_client.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QObject>

namespace openoffice {

enum class LocalAiState { Unknown, Checking, NotInstalled, NotRunning, NoModel, Downloading, Ready, Thinking };

// Where answers come from: the built-in engine (default), the person's Ollama (when
// that option is on in the settings), or a cloud service they added (API key).
enum class AiBackend { Builtin, Ollama, Cloud };

// One line of the model list next to the input box.
struct ModelChoice {
    QString key;   // "builtin:qwen3-4b", "ollama:qwen3:8b", "cloud:<provider>/<model>"; "setup": open the settings
    QString group; // 本机模型, 云端模型, Ollama 模型
    QString label;
    QString note;  // size, the service's name...
    bool current = false;
    bool enabled = true;
};

// The local AI behind the assistant ball. By default it runs the built-in
// engine (LocalEngine: llama.cpp shipped with the program) on a model the
// person downloaded from the AI settings; Ollama is an optional alternative
// chosen there. It chooses a suitable model, and runs conversations in which
// the model may call the assistant's commands as tools, and one-step office
// tasks. Every model-proposed command goes back to the window (which validates
// it, may ask the person to confirm, and executes it); its outcome returns to the model.
class LocalAssistant : public QObject {
    Q_OBJECT

public:
    explicit LocalAssistant(QObject* parent = nullptr);

    OllamaClient* client() const { return m_client; }
    LocalEngine* engine() const { return m_engine; }
    ModelDownloader* downloader() const { return m_downloader; }
    bool builtin() const { return m_backend == AiBackend::Builtin; }
    AiBackend backend() const { return m_backend; }
    bool cloud() const { return m_backend == AiBackend::Cloud; }
    QString cloudServiceName() const { return m_cloudName; }
    // The model list: installed built-in models, every cloud service's models, and
    // Ollama's models when that option is on.
    QList<ModelChoice> modelChoices() const;
    QString currentModelKey() const;
    // Switches to a model from the list (remembered). False while an answer runs.
    bool chooseModel(const QString& key);
    LocalAiState state() const { return m_state; }
    QString model() const { return m_model; }
    QList<LocalModel> models() const { return m_models; }
    qint64 memory() const { return m_memory; }
    qint64 videoMemory() const { return m_video; }
    ModelRecommendation recommendation() const { return recommendDownload(m_memory, m_video); }
    // The model to download for this computer, in the active engine's naming.
    QString recommendedName() const;
    double recommendedGigabytes() const;
    bool recommendationInstalled() const;
    // The chosen model answers slowly here: it does not fit the graphics card,
    // or the last answer came at under 5 tokens per second.
    bool slowModel() const;
    // Why it is slow and what to do (switch or download); empty when it is fine.
    QString speedAdvice() const;
    double lastSpeed() const;
    // Where the built-in engine runs: "显卡 …" or "处理器".
    QString engineText() const;
    QString statusText() const;
    QString downloadStatus() const { return m_downloadStatus; }

    void refresh(); // find the engine and its models; -> stateChanged
    void setModel(const QString& name); // remembered for next time
    // Starts a reply to the person's message. False when the AI is not ready.
    // documentContext: the document's name, its text and the selection.
    bool ask(const QString& text, const QString& documentContext);
    // One office task (rewrite, summary, draft...): a single answer without
    // tools, streamed through taskDelta. False when the AI is not ready.
    bool runTask(const TaskSpec& spec, const TaskInput& input);
    QString runningTask() const { return m_task; }
    // Gets the chosen model ready ahead of the first question.
    void warmUp();
    void commandFinished(const AssistantCommand& command, const CommandOutcome& outcome);
    void commandDeclined(const AssistantCommand& command);
    void downloadRecommended(); // only on the person's explicit request
    // The AI settings page: built-in models.
    void downloadModel(const QString& id);
    void cancelDownload();
    bool deleteModel(const QString& id, QString* error);
    // The Ollama option: shows Ollama's models in the list (remembered). Turning it off
    // while an Ollama model answers goes back to the built-in engine.
    void setOllamaEnabled(bool enabled);
    void cancel(); // stops the running answer or task, and an Ollama download
    // What the model remembers of the current conversation (user, assistant and
    // tool messages). A new conversation passes {}, a reopened one its saved history.
    QJsonArray conversation() const { return m_history; }
    // What the person asked last (the newest user message), for the window's checks.
    QString lastRequest() const;
    void setConversation(const QJsonArray& history); // only while no answer is running

    // Where the Ollama desktop app is installed, if anywhere.
    static QString ollamaAppPath();
    bool launchOllama();

    // The command catalog as tools (names use '_' instead of '.').
    static QJsonArray tools();
    static QString toolName(const QString& command);
    static QString commandName(const QString& tool);

signals:
    void stateChanged();
    void modelsChanged(); // installed models, the chosen model or the engine changed
    void replyStarted();
    void replyDelta(QString text);
    void replyFinished(QString text, bool ok);
    void commandProposed(openoffice::AssistantCommand command);
    void downloadProgress(QString text);
    void taskStarted(QString id);
    void taskDelta(QString text);
    void taskFinished(QString id, QString text, bool ok, QString error);

private:
    void setState(LocalAiState state);
    void refreshBuiltin();
    void refreshCloud(const QString& providerAndModel);
    void send();
    void dispatch(const QJsonArray& messages, const ChatOptions& options);
    void finishWithError(const QString& message);
    void handleDelta(quint64 id, const QString& text);
    void handleFinished(quint64 id, bool ok, const QString& content, const QJsonArray& toolCalls, const QString& error);
    ChatClient* activeClient() const;
    LocalModel currentModel() const;
    QString thinkSetting() const;

    OllamaClient* m_client = nullptr;
    EngineClient* m_engineClient = nullptr;
    EngineClient* m_cloudClient = nullptr;
    QString m_cloudId;   // the cloud service now in use
    QString m_cloudName;
    QList<LocalModel> m_ollamaModels; // for the model list, whichever engine answers
    bool m_ollamaRunning = false;
    LocalEngine* m_engine = nullptr;
    ModelDownloader* m_downloader = nullptr;
    AiBackend m_backend = AiBackend::Builtin;
    LocalAiState m_state = LocalAiState::Unknown;
    QList<LocalModel> m_models;
    QString m_model;
    qint64 m_memory = 0;
    qint64 m_video = 0;
    bool m_cancelled = false;
    QString m_selectAfterRefresh; // the model just downloaded for the person
    QJsonArray m_history; // user/assistant/tool messages; the system prompt is added per request
    QString m_context;
    QString m_pendingTool;
    QStringList m_writtenThisTurn; // texts the model put into the document since the person's message
    quint64 m_chatId = 0;
    int m_rounds = 0;
    QString m_downloadStatus;
    QString m_task; // the running office task's id; empty for a conversation
    QElapsedTimer m_warmed;
    // The built-in engine starts on first use; the request waits for it.
    bool m_waitingForEngine = false;
    QJsonArray m_pendingMessages;
    ChatOptions m_pendingOptions;
};

} // namespace openoffice
