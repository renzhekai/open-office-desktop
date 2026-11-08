#include "local_assistant.h"

#include <QDir>
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

// The chosen model is the only setting; it lives with the person's account.
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
        "你是 Open Office 文字处理软件里的助手，运行在用户自己的电脑上，不联网。"
        "用简体中文回答，简洁、友好，面向不熟悉电脑的人。"
        "用户要你读取或修改文档时，调用提供的工具来完成，不要假装已经做了；"
        "需要用户选中的文字时，先调用 selection_getText；"
        "插入文字时只插入正文内容，不要加解释或引号。"
        "工具执行后，用一句话告诉用户结果。");
    if (!context.isEmpty()) prompt += QStringLiteral("\n当前文档：") + context;
    return prompt;
}

} // namespace

LocalAssistant::LocalAssistant(QObject* parent) : QObject(parent), m_memory(totalPhysicalMemory())
{
    m_client = new OllamaClient(this);
    connect(m_client, &OllamaClient::probed, this, [this](bool running, const QString&) {
        if (!running) {
            setState(ollamaAppPath().isEmpty() ? LocalAiState::NotInstalled : LocalAiState::NotRunning);
            return;
        }
        m_client->listModels();
    });
    connect(m_client, &OllamaClient::modelsListed, this, [this](const QList<LocalModel>& models, const QString& error) {
        if (!error.isEmpty()) {
            setState(LocalAiState::NotRunning);
            return;
        }
        m_models = models;
        const QString saved = savedModel();
        const bool savedInstalled = std::any_of(models.begin(), models.end(),
                                                [&saved](const LocalModel& model) { return model.name == saved; });
        m_model = savedInstalled ? saved : pickInstalledModel(models, m_memory);
        setState(m_model.isEmpty() ? LocalAiState::NoModel : LocalAiState::Ready);
    });
    connect(m_client, &OllamaClient::chatDelta, this, [this](quint64 id, const QString& text) {
        if (id == m_chatId) emit replyDelta(text);
    });
    connect(m_client, &OllamaClient::chatFinished, this,
            [this](quint64 id, bool ok, const QString& content, const QJsonArray& toolCalls, const QString& error) {
        if (id != m_chatId) return;
        if (!ok) {
            setState(LocalAiState::Ready);
            emit replyFinished(error.isEmpty() ? QStringLiteral("本地模型没有完成回答。") : error, false);
            if (error.startsWith(QStringLiteral("连接不到"))) refresh(); // Ollama stopped meanwhile
            return;
        }
        QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), content}};
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
        emit replyFinished(content.trimmed().isEmpty() ? QStringLiteral("（模型没有给出文字回答。）") : content, true);
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
        m_downloadStatus = ok ? QStringLiteral("下载完成。") : QStringLiteral("下载没有完成：") + error;
        emit downloadProgress(m_downloadStatus);
        refresh();
    });
}

QString LocalAssistant::statusText() const
{
    switch (m_state) {
    case LocalAiState::Checking: return QStringLiteral("正在检测本机的本地 AI…");
    case LocalAiState::NotInstalled: return QStringLiteral("这台电脑还没有安装本地 AI 引擎 Ollama");
    case LocalAiState::NotRunning: return QStringLiteral("Ollama 已安装，但现在没有运行");
    case LocalAiState::NoModel: return QStringLiteral("Ollama 已运行，还没有可用的本地模型");
    case LocalAiState::Downloading: return m_downloadStatus.isEmpty() ? QStringLiteral("正在下载模型…") : m_downloadStatus;
    case LocalAiState::Ready: return QStringLiteral("本地模型 %1 · 只在本机运行").arg(m_model);
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
    setState(LocalAiState::Checking);
    m_client->probe();
}

void LocalAssistant::setModel(const QString& name)
{
    if (!std::any_of(m_models.begin(), m_models.end(), [&name](const LocalModel& model) { return model.name == name; }))
        return;
    m_model = name;
    saveModel(name);
    if (m_state == LocalAiState::NoModel) setState(LocalAiState::Ready);
    else emit stateChanged();
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
    // Reasoning models answer faster with a short reasoning phase.
    const QString think = !model.thinking ? QString()
        : (model.family.contains(QStringLiteral("gptoss")) || model.name.startsWith(QStringLiteral("gpt-oss")))
            ? QStringLiteral("low") : QStringLiteral("false");
    setState(LocalAiState::Thinking);
    m_chatId = m_client->chat(m_model, messages, model.tools ? tools() : QJsonArray(), think);
}

void LocalAssistant::commandFinished(const AssistantCommand& command, const CommandOutcome& outcome)
{
    if (command.source != CommandSource::Assistant || m_pendingTool.isEmpty()) return;
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

void LocalAssistant::downloadRecommended()
{
    if (m_state != LocalAiState::NoModel && m_state != LocalAiState::Ready) return;
    m_downloadStatus.clear();
    setState(LocalAiState::Downloading);
    m_client->pull(recommendation().name);
}

void LocalAssistant::cancel()
{
    m_client->cancelChat();
    m_client->cancelPull();
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
