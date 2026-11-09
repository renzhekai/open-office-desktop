#include "local_assistant.h"

#include "markdown_html.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace openoffice {
namespace {

constexpr int kMaxToolRounds = 4;
constexpr int kHistoryLimit = 16;

QString gigabytes(qint64 bytes)
{
    return QString::number(bytes / double(1LL << 30), 'f', 1) + QStringLiteral(" GB");
}

// The Ollama model the person chose; it lives with the person's account.
QString savedModel()
{
    return QSettings(QSettings::UserScope, QStringLiteral("Open Office Project"), QStringLiteral("Open Office"))
        .value(QStringLiteral("ai/model")).toString();
}

void saveModel(const QString& name)
{
    QSettings(QSettings::UserScope, QStringLiteral("Open Office Project"), QStringLiteral("Open Office"))
        .setValue(QStringLiteral("ai/model"), name);
}

QString systemPrompt(const QString& context)
{
    QString prompt = QStringLiteral(
        "你是 Open Office 文字处理软件里的办公助手，运行在用户自己的电脑上，不联网。"
        "用简体中文回答，简洁、友好，面向不熟悉电脑的人；可以用 Markdown 列表和表格。\n"
        "下面附有当前文档的内容和用户选中的文字。回答关于文档的问题时以它为准，不要编造文档里没有的内容。\n"
        "只是回答问题时直接回答，不要调用工具。用户要你修改文档时才调用工具，不要假装已经改了：\n"
        "- 往文档里写新内容：text_insert，format 用 markdown，position 用 after；只写正文，不要加解释；\n"
        "- 修改用户选中的文字：selection_replace，text 是改好的文字，expected 填原来选中的文字；\n"
        "- 把文档里某个词或句子改掉：document_replaceText；\n"
        "- 撤销刚才的修改：edit_undo。\n"
        "工具执行后，用一句话告诉用户结果。");
    if (!context.isEmpty()) prompt += QStringLiteral("\n\n") + context;
    return prompt;
}

} // namespace

LocalAssistant::LocalAssistant(QObject* parent)
    : QObject(parent), m_memory(totalPhysicalMemory()), m_video(totalVideoMemory())
{
    m_client = new OllamaClient(this);
    m_engineClient = new EngineClient(this);
    m_engine = new LocalEngine(this);
    m_downloader = new ModelDownloader(this);
    m_builtin = !useOllama();

    // Ollama (optional): detection and its own model downloads.
    connect(m_client, &OllamaClient::probed, this, [this](bool running, const QString&) {
        if (m_builtin) return;
        if (!running) {
            setState(ollamaAppPath().isEmpty() ? LocalAiState::NotInstalled : LocalAiState::NotRunning);
            return;
        }
        m_client->listModels();
    });
    connect(m_client, &OllamaClient::modelsListed, this, [this](const QList<LocalModel>& models, const QString& error) {
        if (m_builtin) return;
        if (!error.isEmpty()) {
            setState(LocalAiState::NotRunning);
            return;
        }
        m_models = models;
        const auto installed = [&models](const QString& name) {
            return !name.isEmpty() && std::any_of(models.begin(), models.end(),
                                                  [&name](const LocalModel& model) { return model.name == name; });
        };
        if (installed(m_selectAfterRefresh)) saveModel(m_selectAfterRefresh); // what the person just downloaded
        m_selectAfterRefresh.clear();
        const QString saved = savedModel();
        m_model = installed(saved) ? saved : pickInstalledModel(models, m_memory, m_video);
        setState(m_model.isEmpty() ? LocalAiState::NoModel : LocalAiState::Ready);
        emit modelsChanged();
    });
    connect(m_client, &OllamaClient::pullProgress, this, [this](const QString& status, qint64 completed, qint64 total) {
        const ModelRecommendation model = recommendation();
        m_downloadStatus = total > 0
            ? QStringLiteral("正在下载 %1：%2%（%3 / %4）").arg(model.name).arg(qRound(100.0 * completed / total))
                  .arg(gigabytes(completed), gigabytes(total))
            : QStringLiteral("正在准备下载 %1（%2）").arg(model.name, status);
        emit downloadProgress(m_downloadStatus);
    });
    connect(m_client, &OllamaClient::pullFinished, this, [this](bool ok, const QString& error) {
        m_downloadStatus = ok ? QStringLiteral("下载完成，已改用 %1。").arg(recommendation().name)
                              : QStringLiteral("下载没有完成：") + explainPullError(error);
        if (ok) m_selectAfterRefresh = recommendation().name;
        emit downloadProgress(m_downloadStatus);
        refresh();
    });

    // Answers from whichever engine is active.
    connect(m_client, &ChatClient::chatDelta, this, [this](quint64 id, const QString& text) {
        if (activeClient() == m_client) handleDelta(id, text);
    });
    connect(m_engineClient, &ChatClient::chatDelta, this, [this](quint64 id, const QString& text) {
        if (activeClient() == m_engineClient) handleDelta(id, text);
    });
    connect(m_client, &ChatClient::chatFinished, this,
            [this](quint64 id, bool ok, const QString& content, const QJsonArray& toolCalls, const QString& error) {
        if (activeClient() == m_client) handleFinished(id, ok, content, toolCalls, error);
    });
    connect(m_engineClient, &ChatClient::chatFinished, this,
            [this](quint64 id, bool ok, const QString& content, const QJsonArray& toolCalls, const QString& error) {
        if (activeClient() == m_engineClient) handleFinished(id, ok, content, toolCalls, error);
    });

    // The built-in engine: started on first use, the waiting request goes on when it is ready.
    connect(m_engine, &LocalEngine::ready, this, [this] {
        m_engineClient->setEndpoint(m_engine->url(), m_engine->apiKey());
        emit modelsChanged();
        if (!m_waitingForEngine) return;
        m_waitingForEngine = false;
        m_chatId = m_engineClient->chat(m_model, m_pendingMessages, m_pendingOptions);
        emit stateChanged(); // "正在启动…" becomes "正在思考…"
    });
    connect(m_engine, &LocalEngine::failed, this, [this](const QString& message) {
        emit modelsChanged();
        if (!m_waitingForEngine && m_state != LocalAiState::Thinking) return;
        m_waitingForEngine = false;
        finishWithError(message);
    });

    // Built-in model downloads (from the AI settings or the ball's card).
    connect(m_downloader, &ModelDownloader::progress, this, [this] {
        m_downloadStatus = m_downloader->progressText();
        emit downloadProgress(m_downloadStatus);
    });
    connect(m_downloader, &ModelDownloader::finished, this, [this](const QString& id, bool ok, const QString& message) {
        m_downloadStatus = message;
        if (ok) m_selectAfterRefresh = id; // what the person just downloaded is what they want to use
        emit downloadProgress(message);
        if (m_state == LocalAiState::Downloading) setState(LocalAiState::Unknown);
        if (m_state != LocalAiState::Thinking) refresh();
        emit modelsChanged();
    });
}

ChatClient* LocalAssistant::activeClient() const
{
    return m_builtin ? static_cast<ChatClient*>(m_engineClient) : static_cast<ChatClient*>(m_client);
}

QString LocalAssistant::statusText() const
{
    if (m_builtin) {
        switch (m_state) {
        case LocalAiState::Checking: return QStringLiteral("正在检查本地 AI…");
        case LocalAiState::NotInstalled: return QStringLiteral("这个版本没有带内置 AI 引擎");
        case LocalAiState::NoModel: return QStringLiteral("还没有下载本地模型");
        case LocalAiState::Downloading: return m_downloadStatus.isEmpty() ? QStringLiteral("正在下载模型…") : m_downloadStatus;
        case LocalAiState::Ready:
            return QStringLiteral("本地模型 %1 · 只在本机运行").arg(m_model) + (slowModel() ? QStringLiteral(" · 较慢") : QString());
        case LocalAiState::Thinking:
            return m_waitingForEngine ? QStringLiteral("正在启动内置引擎，把 %1 读进内存…").arg(m_model)
                                      : QStringLiteral("%1 正在思考…").arg(m_model);
        default: return QStringLiteral("本地 AI 尚未检查");
        }
    }
    switch (m_state) {
    case LocalAiState::Checking: return QStringLiteral("正在检测本机的 Ollama…");
    case LocalAiState::NotInstalled: return QStringLiteral("这台电脑还没有安装 Ollama");
    case LocalAiState::NotRunning: return QStringLiteral("Ollama 已安装，但现在没有运行");
    case LocalAiState::NoModel: return QStringLiteral("Ollama 已运行，还没有可用的本地模型");
    case LocalAiState::Downloading: return m_downloadStatus.isEmpty() ? QStringLiteral("正在下载模型…") : m_downloadStatus;
    case LocalAiState::Ready:
        return QStringLiteral("Ollama 模型 %1 · 只在本机运行").arg(m_model) + (slowModel() ? QStringLiteral(" · 较慢") : QString());
    case LocalAiState::Thinking: return QStringLiteral("%1 正在思考…").arg(m_model);
    default: return QStringLiteral("本地 AI 尚未检测");
    }
}

void LocalAssistant::setState(LocalAiState state)
{
    m_state = state;
    emit stateChanged();
}

void LocalAssistant::refresh()
{
    if (m_state == LocalAiState::Thinking || m_client->pulling()) return;
    const bool builtin = !useOllama();
    if (builtin != m_builtin) {
        m_builtin = builtin;
        m_history = {}; // a new engine starts a new conversation
        m_engine->stop();
    }
    if (m_builtin) {
        refreshBuiltin();
        return;
    }
    setState(LocalAiState::Checking);
    m_client->probe();
}

void LocalAssistant::refreshBuiltin()
{
    m_models.clear();
    if (!m_engine->available()) {
        m_model.clear();
        setState(LocalAiState::NotInstalled);
        emit modelsChanged();
        return;
    }
    for (const CatalogModel& model : installedModels()) m_models.append(asLocalModel(model));
    const auto installed = [this](const QString& id) {
        return !id.isEmpty() && std::any_of(m_models.begin(), m_models.end(), [&id](const LocalModel& model) { return model.name == id; });
    };
    if (installed(m_selectAfterRefresh)) setChosenBuiltinModel(m_selectAfterRefresh);
    m_selectAfterRefresh.clear();
    const QString chosen = chosenBuiltinModel();
    const QString model = installed(chosen) ? chosen : pickInstalledModel(m_models, m_memory, m_video);
    if (model != m_model) {
        m_engine->stop(); // the next request starts it with the new model
        m_warmed.invalidate();
    }
    m_model = model;
    if (m_downloader->active() && m_model.isEmpty()) setState(LocalAiState::Downloading);
    else setState(m_model.isEmpty() ? LocalAiState::NoModel : LocalAiState::Ready);
    emit modelsChanged();
}

void LocalAssistant::setModel(const QString& name)
{
    if (!std::any_of(m_models.begin(), m_models.end(), [&name](const LocalModel& model) { return model.name == name; }))
        return;
    if (m_builtin) {
        setChosenBuiltinModel(name);
        if (name != m_model) m_engine->stop(); // frees the memory; the next request loads the new one
    } else {
        saveModel(name);
    }
    m_model = name;
    m_warmed.invalidate();
    if (m_state == LocalAiState::NoModel) setState(LocalAiState::Ready);
    else emit stateChanged();
    emit modelsChanged();
}

LocalModel LocalAssistant::currentModel() const
{
    for (const LocalModel& model : m_models)
        if (model.name == m_model) return model;
    return {};
}

bool LocalAssistant::ask(const QString& text, const QString& documentContext)
{
    if (m_state != LocalAiState::Ready || text.trimmed().isEmpty()) return false;
    m_context = documentContext;
    m_history.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), text}});
    m_rounds = 0;
    m_pendingTool.clear();
    m_writtenThisTurn.clear();
    emit replyStarted();
    send();
    return true;
}

void LocalAssistant::send()
{
    while (m_history.size() > kHistoryLimit) {
        m_history.removeFirst();
        // Never start the window with an orphaned tool result.
        while (!m_history.isEmpty() && m_history.first().toObject().value(QStringLiteral("role")).toString() != QStringLiteral("user"))
            m_history.removeFirst();
    }
    QJsonArray messages{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"), systemPrompt(m_context)}}};
    for (const QJsonValue& message : m_history) messages.append(message);
    const LocalModel model = currentModel();
    ChatOptions options;
    options.tools = model.tools ? tools() : QJsonArray();
    options.think = thinkSetting();
    options.contextLength = contextLengthFor(messages, !options.tools.isEmpty());
    dispatch(messages, options);
}

void LocalAssistant::dispatch(const QJsonArray& messages, const ChatOptions& options)
{
    setState(LocalAiState::Thinking);
    if (!m_builtin) {
        m_chatId = m_client->chat(m_model, messages, options);
        return;
    }
    const CatalogModel* model = findCatalogModel(m_model);
    const QString path = model ? modelFilePath(*model) : QString();
    if (m_engine->isReady() && m_engine->modelPath() == path) {
        m_chatId = m_engineClient->chat(m_model, messages, options);
        return;
    }
    // First use (or another model): the engine loads the model, then the request goes on.
    m_pendingMessages = messages;
    m_pendingOptions = options;
    m_waitingForEngine = true;
    m_engine->start(path);
    emit stateChanged();
}

void LocalAssistant::finishWithError(const QString& message)
{
    setState(LocalAiState::Ready);
    if (!m_task.isEmpty()) {
        const QString task = m_task;
        m_task.clear();
        emit taskFinished(task, QString(), false, message);
    } else {
        emit replyFinished(message, false);
    }
}

void LocalAssistant::handleDelta(quint64 id, const QString& text)
{
    if (id != m_chatId) return;
    if (m_task.isEmpty()) emit replyDelta(text);
    else emit taskDelta(text);
}

void LocalAssistant::handleFinished(quint64 id, bool ok, const QString& content, const QJsonArray& toolCalls,
                                    const QString& error)
{
    if (id != m_chatId) return;
    const bool stopped = m_cancelled;
    m_cancelled = false;
    const QString failure = stopped ? QStringLiteral("已停止。")
        : error.isEmpty() ? QStringLiteral("本地模型没有完成。") : error;
    const bool lostEngine = error.startsWith(QStringLiteral("连接不到")) || error.contains(QStringLiteral("没有在运行"));
    if (!m_task.isEmpty()) {
        const QString task = m_task;
        m_task.clear();
        setState(LocalAiState::Ready);
        emit taskFinished(task, content, ok && !stopped, ok && !stopped ? QString() : failure);
        if (!ok && lostEngine) refresh();
        return;
    }
    if (!ok || stopped) {
        setState(LocalAiState::Ready);
        emit replyFinished(stopped ? failure : error.isEmpty() ? QStringLiteral("本地模型没有完成回答。") : error, false);
        if (lostEngine) refresh(); // the engine stopped meanwhile
        return;
    }
    // The last answer of a turn that wrote into the document: no second copy of what was written.
    const QString answer = toolCalls.isEmpty() && !m_writtenThisTurn.isEmpty()
        ? withoutEchoedText(content, m_writtenThisTurn) : content;
    QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), answer}};
    if (!toolCalls.isEmpty()) message.insert(QStringLiteral("tool_calls"), toolCalls);
    m_history.append(message);
    if (!toolCalls.isEmpty() && m_rounds < kMaxToolRounds) {
        // One tool at a time: the window validates, may ask, then reports back.
        const QJsonObject function = toolCalls.first().toObject().value(QStringLiteral("function")).toObject();
        QJsonValue arguments = function.value(QStringLiteral("arguments"));
        if (arguments.isString()) arguments = QJsonDocument::fromJson(arguments.toString().toUtf8()).object();
        m_pendingTool = function.value(QStringLiteral("name")).toString();
        ++m_rounds;
        emit replyFinished(content, true);
        emit commandProposed({commandName(m_pendingTool), arguments.toObject(), CommandSource::Assistant});
        return;
    }
    setState(LocalAiState::Ready);
    emit replyFinished(answer.trimmed().isEmpty() ? QStringLiteral("（模型没有给出文字回答。）") : answer, true);
}

QString LocalAssistant::thinkSetting() const
{
    // Qwen3 (the built-in models) reasons first unless told not to. Measured on this
    // project: without it the 1.7B model just repeats the text it should polish and
    // misses typos; with a short reasoning phase it does both well. So small models
    // reason where the engine is fast (a graphics card, or 40+ tokens/s measured);
    // on a slow processor, and for the larger model, answers come directly.
    if (m_builtin) {
        const LocalModel model = currentModel();
        const bool small = model.bytes > 0 && model.bytes < static_cast<qint64>(3.5 * (1LL << 30));
        const double speed = lastSpeed();
        const bool fast = speed > 0 ? speed >= 40 : m_video > 0;
        return small && fast ? QString() : QStringLiteral("false");
    }
    // Reasoning models answer much sooner with a short (or no) reasoning phase.
    const LocalModel model = currentModel();
    if (!model.thinking) return {};
    return model.family.contains(QStringLiteral("gptoss")) || model.name.startsWith(QStringLiteral("gpt-oss"))
        ? QStringLiteral("low") : QStringLiteral("false");
}

bool LocalAssistant::runTask(const TaskSpec& spec, const TaskInput& input)
{
    if (m_state != LocalAiState::Ready) return false;
    const QJsonArray messages = taskMessages(spec, input);
    ChatOptions options;
    options.think = thinkSetting();
    // A reasoning model that cannot stop reasoning (gpt-oss) stalls on a
    // schema-constrained answer; it gets the plain instruction instead.
    if (options.think != QStringLiteral("low")) options.format = taskFormat(spec);
    options.contextLength = contextLengthFor(messages, false);
    options.temperature = spec.output == TaskOutput::Corrections ? 0.0 : 0.4;
    m_task = spec.id;
    emit taskStarted(spec.id);
    dispatch(messages, options);
    return true;
}

void LocalAssistant::warmUp()
{
    if (m_state != LocalAiState::Ready || m_model.isEmpty()) return;
    if (m_builtin) {
        const CatalogModel* model = findCatalogModel(m_model);
        if (model && !m_engine->isReady() && !m_engine->isStarting()) m_engine->start(modelFilePath(*model));
        return;
    }
    if (m_warmed.isValid() && m_warmed.elapsed() < 10 * 60 * 1000) return; // Ollama keeps it for 30 minutes
    m_warmed.start();
    m_client->warmUp(m_model);
}

void LocalAssistant::commandFinished(const AssistantCommand& command, const CommandOutcome& outcome)
{
    if (command.source != CommandSource::Assistant || m_pendingTool.isEmpty()) return;
    if (outcome.ok && (command.name == QStringLiteral("text.insert") || command.name == QStringLiteral("selection.replace")))
        m_writtenThisTurn << command.arguments.value(QStringLiteral("text")).toString();
    const QJsonObject result{{QStringLiteral("ok"), outcome.ok}, {QStringLiteral("code"), outcome.code},
                             {QStringLiteral("message"), outcome.message}, {QStringLiteral("value"), outcome.value}};
    m_history.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                                 {QStringLiteral("tool_name"), m_pendingTool},
                                 {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}});
    m_pendingTool.clear();
    emit replyStarted();
    send(); // let the model tell the person what happened
}

void LocalAssistant::commandDeclined(const AssistantCommand& command)
{
    commandFinished(command, {false, QStringLiteral("declined"), QStringLiteral("用户没有同意执行这个操作。"), {}});
}

QString LocalAssistant::recommendedName() const
{
    return m_builtin ? recommendedCatalogModel(m_memory, m_video).id : recommendation().name;
}

double LocalAssistant::recommendedGigabytes() const
{
    return m_builtin ? recommendedCatalogModel(m_memory, m_video).gigabytes : recommendation().approximateGigabytes;
}

bool LocalAssistant::recommendationInstalled() const
{
    const QString name = recommendedName();
    return std::any_of(m_models.begin(), m_models.end(), [&name](const LocalModel& model) { return model.name == name; });
}

void LocalAssistant::downloadRecommended()
{
    if (m_state != LocalAiState::NoModel && m_state != LocalAiState::Ready) return;
    if (m_builtin) {
        downloadModel(recommendedName());
        return;
    }
    m_downloadStatus.clear();
    setState(LocalAiState::Downloading);
    m_client->pull(recommendation().name);
}

void LocalAssistant::downloadModel(const QString& id)
{
    const CatalogModel* model = findCatalogModel(id);
    if (!model || m_downloader->active()) return;
    m_downloadStatus = QStringLiteral("正在准备下载 %1…").arg(model->title);
    // Without any model yet, the assistant waits for this one; otherwise it stays usable.
    if (m_state == LocalAiState::NoModel || m_state == LocalAiState::NotInstalled) setState(LocalAiState::Downloading);
    emit downloadProgress(m_downloadStatus);
    m_downloader->start(*model, modelFolder());
}

void LocalAssistant::cancelDownload()
{
    m_downloader->cancel();
}

bool LocalAssistant::deleteModel(const QString& id, QString* error)
{
    const CatalogModel* model = findCatalogModel(id);
    if (!model) return false;
    if (m_state == LocalAiState::Thinking && id == m_model) {
        if (error) *error = QStringLiteral("这个模型正在回答，等它结束或点停止后再删除。");
        return false;
    }
    if (m_downloader->currentId() == id) m_downloader->cancel();
    if (QDir::cleanPath(m_engine->modelPath()) == QDir::cleanPath(modelFilePath(*model))) m_engine->stop();
    const QString path = modelFilePath(*model);
    if (QFileInfo(path).isFile() && !QFile::remove(path)) {
        if (error) *error = QStringLiteral("删不掉文件：") + QDir::toNativeSeparators(path);
        return false;
    }
    QFile::remove(path + QStringLiteral(".part"));
    if (chosenBuiltinModel() == id) setChosenBuiltinModel(QString());
    refresh();
    return true;
}

void LocalAssistant::setBackend(bool ollama)
{
    setUseOllama(ollama);
    refresh();
}

void LocalAssistant::setConversation(const QJsonArray& history)
{
    m_history = history; // the window switches only while no answer is running
    m_pendingTool.clear();
    m_rounds = 0;
}

void LocalAssistant::cancel()
{
    if (m_waitingForEngine) {
        m_waitingForEngine = false; // the engine keeps loading, so the next try is quicker
        finishWithError(QStringLiteral("已停止。"));
        return;
    }
    if (m_state == LocalAiState::Thinking) m_cancelled = true;
    activeClient()->cancelChat();
    m_client->cancelPull();
}

double LocalAssistant::lastSpeed() const
{
    return activeClient()->lastTokensPerSecond();
}

QString LocalAssistant::engineText() const
{
    if (!m_builtin) return QStringLiteral("Ollama");
    if (!m_engine->deviceText().isEmpty()) return m_engine->deviceText();
    const GpuInfo card = largestGpu();
    return card.bytes > 0 ? QStringLiteral("显卡 ") + card.name : QStringLiteral("处理器");
}

bool LocalAssistant::slowModel() const
{
    const LocalModel model = currentModel();
    if (model.name.isEmpty()) return false;
    const double speed = lastSpeed();
    return runsSlowly(model, m_video) || (speed > 0 && speed < 5);
}

QString LocalAssistant::speedAdvice() const
{
    if (!slowModel()) return {};
    const LocalModel model = currentModel();
    const double speed = lastSpeed();
    QString why;
    if (runsSlowly(model, m_video))
        why = m_video > 0 ? QStringLiteral("放不进这台电脑的显卡，大部分要靠处理器来算")
                          : QStringLiteral("对没有独立显卡的电脑来说太大了");
    else
        why = QStringLiteral("在这台电脑上每秒只能写出约 %1 个词").arg(qMax(1, qRound(speed)));
    QString advice = QStringLiteral("现在用的 %1 %2，所以回答很慢。").arg(model.name, why);
    const QString better = recommendedName();
    if (better == model.name) return advice;
    if (recommendationInstalled()) return advice + QStringLiteral("本机已有更合适的 %1，可以改用它。").arg(better);
    return advice + QStringLiteral("推荐下载 %1（约 %2 GB，来自%3），它%4，回答会快很多；下载完会自动改用它。")
                        .arg(better, QString::number(recommendedGigabytes(), 'f', 1),
                             m_builtin ? QStringLiteral("魔搭 ModelScope") : QStringLiteral(" Ollama 官方模型库"),
                             m_video > 0 ? QStringLiteral("能整个放进显卡") : QStringLiteral("大小适合这台电脑"));
}

QString LocalAssistant::ollamaAppPath()
{
    // The Windows installer's per-user location, then a system-wide one, then PATH.
    const QStringList candidates{
        QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath(QStringLiteral("Programs/Ollama/ollama app.exe")),
        QDir(qEnvironmentVariable("ProgramFiles")).filePath(QStringLiteral("Ollama/ollama app.exe")),
    };
    for (const QString& path : candidates)
        if (QFileInfo(path).isFile()) return QDir::cleanPath(path);
    return QStandardPaths::findExecutable(QStringLiteral("ollama"));
}

bool LocalAssistant::launchOllama()
{
    const QString path = ollamaAppPath();
    if (path.isEmpty()) return false;
    const bool desktopApp = QFileInfo(path).fileName().compare(QStringLiteral("ollama app.exe"), Qt::CaseInsensitive) == 0;
    return QProcess::startDetached(path, desktopApp ? QStringList() : QStringList{QStringLiteral("serve")});
}

QString LocalAssistant::toolName(const QString& command)
{
    QString name = command;
    return name.replace(QLatin1Char('.'), QLatin1Char('_'));
}

QString LocalAssistant::commandName(const QString& tool)
{
    for (const CommandSpec& spec : commandCatalog())
        if (toolName(spec.name) == tool || spec.name == tool) return spec.name;
    return tool;
}

QJsonArray LocalAssistant::tools()
{
    QJsonArray result;
    for (const QJsonValue& value : commandCatalogJson()) {
        const QJsonObject tool = value.toObject();
        const QString name = tool.value(QStringLiteral("name")).toString();
        if (name == QStringLiteral("assistant.listCommands")) continue; // meaningless to a model
        result.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{
                {QStringLiteral("name"), toolName(name)},
                {QStringLiteral("description"), tool.value(QStringLiteral("title")).toString() + QStringLiteral("：")
                                                    + tool.value(QStringLiteral("description")).toString()},
                {QStringLiteral("parameters"), tool.value(QStringLiteral("inputSchema"))}}}});
    }
    return result;
}

} // namespace openoffice
