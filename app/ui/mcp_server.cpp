#include "mcp_server.h"

#include "assistant/assistant_commands.h"
#include "assistant/conversation_store.h"
#include "assistant/local_assistant.h"
#include "ui/assistant_ball.h"
#include "ui/main_window.h"

#include <QAction>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <memory>

namespace openoffice {
namespace {

constexpr const char* kProtocolVersion = "2025-06-18";
constexpr int kMaxRequestBytes = 2 * 1024 * 1024;
constexpr int kCommandTimeoutMs = 120000; // an editor command takes well under a second; saving, a few

const char* const kInstructions =
    "Open Office 文字文档的编辑命令。先用 document.outline 看文档结构（段落 P1、P2…，表格 T1、T2…），"
    "用 table.read 读表格的每个单元格；再按编号和行列，用 table.setCell、format.cells、format.paragraphs、"
    "format.text 等一处一处地改，不要整段重写。每次修改都能用 edit.undo 撤销；是否保存由用户决定。";

QJsonObject rpcResult(const QJsonValue& id, const QJsonObject& result)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}};
}

QJsonObject rpcError(const QJsonValue& id, int code, const QString& message)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

// Tool results carry the same JSON twice: as text for any client, and structured.
QJsonObject toolResult(const QJsonObject& structured, bool isError)
{
    const QString text = QString::fromUtf8(QJsonDocument(structured).toJson(QJsonDocument::Compact));
    return {{QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                               {QStringLiteral("text"), text}}}},
            {QStringLiteral("structuredContent"), structured},
            {QStringLiteral("isError"), isError}};
}

QJsonObject failureJson(const QString& code, const QString& message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
}

QJsonObject outcomeJson(const CommandOutcome& outcome)
{
    return {{QStringLiteral("ok"), outcome.ok}, {QStringLiteral("code"), outcome.code},
            {QStringLiteral("message"), outcome.message}, {QStringLiteral("value"), outcome.value}};
}

bool sameSecret(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (int i = 0; i < a.size(); ++i) difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}

bool localName(const QByteArray& value, quint16 port, bool withScheme)
{
    const QByteArray prefix = withScheme ? QByteArrayLiteral("http://") : QByteArray();
    for (const QByteArray& host : {QByteArrayLiteral("127.0.0.1"), QByteArrayLiteral("localhost")}) {
        const QByteArray bare = prefix + host;
        if (value == bare || value == bare + ':' + QByteArray::number(port)) return true;
    }
    return false;
}

QString aiStateName(LocalAiState state)
{
    switch (state) {
    case LocalAiState::Unknown: return QStringLiteral("unknown");
    case LocalAiState::Checking: return QStringLiteral("checking");
    case LocalAiState::NotInstalled: return QStringLiteral("not_installed");
    case LocalAiState::NotRunning: return QStringLiteral("not_running");
    case LocalAiState::NoModel: return QStringLiteral("no_model");
    case LocalAiState::Downloading: return QStringLiteral("downloading");
    case LocalAiState::Ready: return QStringLiteral("ready");
    case LocalAiState::Thinking: return QStringLiteral("thinking");
    }
    return QStringLiteral("unknown");
}

QJsonObject testTool(const QString& name, const QString& description, const QJsonObject& properties,
                     const QStringList& required = {})
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("description"), description},
            {QStringLiteral("inputSchema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                        {QStringLiteral("properties"), properties},
                                                        {QStringLiteral("required"), QJsonArray::fromStringList(required)},
                                                        {QStringLiteral("additionalProperties"), false}}}};
}

QJsonObject stringProperty(const QString& description)
{
    return {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), description}};
}

QJsonObject timeoutProperty()
{
    return {{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1},
            {QStringLiteral("maximum"), 900}, {QStringLiteral("description"), QStringLiteral("最多等多少秒（默认 240）。")}};
}

} // namespace

McpServer::McpServer(MainWindow* window, bool testTools, QObject* parent)
    : QObject(parent), m_window(window), m_testTools(testTools)
{
    QByteArray random(32, Qt::Uninitialized);
    for (char& byte : random) byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    m_token = QString::fromLatin1(random.toHex());
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &McpServer::accept);
    if (window && window->assistantBall()) {
        connect(window->assistantBall(), &AssistantBall::messageRecorded, this, [this](const ConversationMessage& message) {
            m_said.append({{QStringLiteral("role"), message.role}, {QStringLiteral("title"), message.title},
                           {QStringLiteral("text"), message.text}, {QStringLiteral("ok"), message.ok}});
        });
        connect(window->assistantBall(), &AssistantBall::cardShown, this, [this](const QString& title, const QString& text, bool ok) {
            m_said.append({{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("title"), title},
                           {QStringLiteral("text"), text}, {QStringLiteral("ok"), ok}, {QStringLiteral("card"), true}});
        });
    }
}

bool McpServer::listen(quint16 port, QString* error)
{
    if (m_server->listen(QHostAddress::LocalHost, port)) return true;
    if (error) *error = m_server->errorString();
    return false;
}

quint16 McpServer::port() const
{
    return m_server->serverPort();
}

bool McpServer::writeEndpoint(const QString& path, QString* error) const
{
    const QJsonObject endpoint{{QStringLiteral("url"), QStringLiteral("http://127.0.0.1:%1/mcp").arg(port())},
                               {QStringLiteral("token"), m_token},
                               {QStringLiteral("testTools"), m_testTools},
                               {QStringLiteral("pid"), QCoreApplication::applicationPid()},
                               {QStringLiteral("protocolVersion"), QString::fromLatin1(kProtocolVersion)}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(endpoint).toJson(QJsonDocument::Indented));
    if (file.commit()) return true;
    if (error) *error = file.errorString();
    return false;
}

void McpServer::accept()
{
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        m_clients.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readClient(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_clients.remove(socket);
            socket->deleteLater();
        });
    }
}

void McpServer::respond(QTcpSocket* socket, int status, const QByteArray& body, const QByteArray& type)
{
    static const QHash<int, QByteArray> reasons{{200, "OK"}, {202, "Accepted"}, {400, "Bad Request"}, {401, "Unauthorized"},
                                                {403, "Forbidden"}, {404, "Not Found"}, {405, "Method Not Allowed"},
                                                {413, "Payload Too Large"}, {415, "Unsupported Media Type"}};
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasons.value(status, "Error") + "\r\n";
    if (!body.isEmpty()) head += "Content-Type: " + type + "; charset=utf-8\r\n";
    if (status == 405) head += "Allow: POST\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    socket->write(head + body);
    socket->disconnectFromHost();
}

void McpServer::readClient(QTcpSocket* socket)
{
    auto found = m_clients.find(socket);
    if (found == m_clients.end()) return;
    found->buffer += socket->readAll();
    QByteArray& buffer = found->buffer;
    if (buffer.size() > kMaxRequestBytes) {
        m_clients.remove(socket);
        respond(socket, 413, {});
        return;
    }
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;
    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
    QHash<QByteArray, QByteArray> headers;
    for (int i = 1; i < lines.size(); ++i) {
        const int colon = lines[i].indexOf(':');
        if (colon > 0) headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
    }
    const int length = headers.value("content-length", "0").toInt();
    if (length < 0 || length > kMaxRequestBytes) {
        m_clients.remove(socket);
        respond(socket, 413, {});
        return;
    }
    if (buffer.size() < headerEnd + 4 + length) return; // the body is still coming
    const QByteArray body = buffer.mid(headerEnd + 4, length);
    m_clients.remove(socket); // one request per connection

    const QByteArray method = request.value(0);
    const QByteArray path = request.value(1).split('?').value(0);
    // Only this computer: a page in a browser cannot pass these checks (DNS rebinding, CSRF).
    if (!localName(headers.value("host"), port(), false)
        || (headers.contains("origin") && !localName(headers.value("origin"), port(), true))) {
        respond(socket, 403, {});
        return;
    }
    if (path != "/mcp" && path != "/mcp/") {
        respond(socket, 404, {});
        return;
    }
    if (method != "POST") {
        respond(socket, 405, {});
        return;
    }
    const QByteArray authorization = headers.value("authorization");
    if (!authorization.toLower().startsWith("bearer ") || !sameSecret(authorization.mid(7).trimmed(), m_token.toLatin1())) {
        respond(socket, 401, {});
        return;
    }
    if (!headers.value("content-type").toLower().startsWith("application/json")) {
        respond(socket, 415, {});
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        respond(socket, 400, QJsonDocument(rpcError(QJsonValue::Null, -32700, QStringLiteral("Parse error"))).toJson(QJsonDocument::Compact));
        return;
    }
    const QJsonObject message = document.object();
    if (!message.contains(QStringLiteral("id"))) { // a notification or a response: accepted, nothing to answer
        handleMessage(message, [](const QJsonObject&) {});
        respond(socket, 202, {});
        return;
    }
    const QPointer<QTcpSocket> client(socket);
    handleMessage(message, [this, client](const QJsonObject& reply) {
        if (client) respond(client, 200, QJsonDocument(reply).toJson(QJsonDocument::Compact));
    });
}

void McpServer::handleMessage(const QJsonObject& message, const std::function<void(const QJsonObject&)>& reply)
{
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();
    if (message.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0") || method.isEmpty()) {
        reply(rpcError(id, -32600, QStringLiteral("Invalid Request")));
        return;
    }
    if (method.startsWith(QStringLiteral("notifications/"))) return;
    if (method == QStringLiteral("initialize")) {
        static const QStringList known{QString::fromLatin1(kProtocolVersion), QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
        const QString asked = params.value(QStringLiteral("protocolVersion")).toString();
        reply(rpcResult(id, {{QStringLiteral("protocolVersion"), known.contains(asked) ? asked : QString::fromLatin1(kProtocolVersion)},
                             {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"),
                                                                           QJsonObject{{QStringLiteral("listChanged"), false}}}}},
                             {QStringLiteral("serverInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("open-office")},
                                                                        {QStringLiteral("title"), QStringLiteral("Open Office")},
                                                                        {QStringLiteral("version"), QCoreApplication::applicationVersion()}}},
                             {QStringLiteral("instructions"), QString::fromUtf8(kInstructions)}}));
        return;
    }
    if (method == QStringLiteral("ping")) {
        reply(rpcResult(id, {}));
        return;
    }
    if (method == QStringLiteral("tools/list")) {
        reply(rpcResult(id, {{QStringLiteral("tools"), tools()}}));
        return;
    }
    if (method == QStringLiteral("tools/call")) {
        const QString name = params.value(QStringLiteral("name")).toString();
        callTool(name, params.value(QStringLiteral("arguments")).toObject(),
                 [reply, id](const QJsonObject& result) { reply(rpcResult(id, result)); });
        return;
    }
    reply(rpcError(id, -32601, QStringLiteral("Method not found: ") + method));
}

QJsonArray McpServer::tools() const
{
    QJsonArray list = commandCatalogJson();
    if (!m_testTools) return list;
    const QJsonObject timeout = timeoutProperty();
    list.append(testTool(QStringLiteral("test.state"), QStringLiteral("测试用：当前文档、助手是否在忙、本地 AI 状态、快捷操作、最后一条消息。"), {}));
    list.append(testTool(QStringLiteral("test.open"), QStringLiteral("测试用：在窗口里打开这份文档，等它就绪。"),
                         {{QStringLiteral("path"), stringProperty(QStringLiteral("文档的完整路径。"))}}, {QStringLiteral("path")}));
    list.append(testTool(QStringLiteral("test.ask"), QStringLiteral("测试用：像用户一样在助手输入框里输入并发送，等助手做完，返回它说的话和可以按的按钮。"),
                         {{QStringLiteral("text"), stringProperty(QStringLiteral("要发送的文字（也可以是 / 指令）。"))},
                          {QStringLiteral("timeoutSeconds"), timeout}}, {QStringLiteral("text")}));
    list.append(testTool(QStringLiteral("test.press"), QStringLiteral("测试用：按助手消息里最新的、文字是 button 的按钮（如 替换原文、全部改正、执行），等助手做完。"),
                         {{QStringLiteral("button"), stringProperty(QStringLiteral("按钮上的文字。"))},
                          {QStringLiteral("timeoutSeconds"), timeout}}, {QStringLiteral("button")}));
    list.append(testTool(QStringLiteral("test.quick"), QStringLiteral("测试用：按一个快捷操作（如 polish、proofread、summary），等助手做完。"),
                         {{QStringLiteral("id"), stringProperty(QStringLiteral("快捷操作的编号。"))},
                          {QStringLiteral("timeoutSeconds"), timeout}}, {QStringLiteral("id")}));
    list.append(testTool(QStringLiteral("test.wait"), QStringLiteral("测试用：等助手做完手上的事。"),
                         {{QStringLiteral("timeoutSeconds"), timeout}}));
    return list;
}

void McpServer::callTool(const QString& name, const QJsonObject& arguments, const std::function<void(const QJsonObject&)>& done)
{
    if (!m_window) {
        done(toolResult(failureJson(QStringLiteral("no_window"), QStringLiteral("窗口已关闭。")), true));
        return;
    }
    if (name.startsWith(QStringLiteral("test."))) {
        if (m_testTools) callTestTool(name, arguments, done);
        else done(toolResult(failureJson(QStringLiteral("unknown_tool"), QStringLiteral("没有这个工具。")), true));
        return;
    }
    if (!findCommand(name)) {
        done(toolResult(failureJson(QStringLiteral("unknown_tool"), QStringLiteral("没有这个工具：") + name), true));
        return;
    }
    // Every call gets one answer: a command that never reports back is answered as timed out
    // (and its late report, if any, is dropped), so a client is never left waiting.
    auto answered = std::make_shared<bool>(false);
    const auto finish = [done, answered](const QJsonObject& result) {
        if (*answered) return;
        *answered = true;
        done(result);
    };
    QTimer::singleShot(kCommandTimeoutMs, this, [finish] {
        finish(toolResult(failureJson(QStringLiteral("timeout"), QStringLiteral("命令在 %1 秒内没有完成。").arg(kCommandTimeoutMs / 1000)), true));
    });
    m_window->runExternalCommand({name, arguments, CommandSource::External},
                                 [finish](const CommandOutcome& outcome) { finish(toolResult(outcomeJson(outcome), !outcome.ok)); });
}

void McpServer::callTestTool(const QString& name, const QJsonObject& arguments, const std::function<void(const QJsonObject&)>& done)
{
    AssistantBall* ball = m_window->assistantBall();
    const int timeoutMs = qBound(1, arguments.value(QStringLiteral("timeoutSeconds")).toInt(240), 900) * 1000;
    const auto fail = [done](const QString& code, const QString& message) { done(toolResult(failureJson(code, message), true)); };
    if (name == QStringLiteral("test.state")) {
        done(toolResult(stateReport(), false));
        return;
    }
    if (name == QStringLiteral("test.open")) {
        const QString path = QFileInfo(arguments.value(QStringLiteral("path")).toString()).absoluteFilePath();
        if (!QFileInfo(path).isFile()) return fail(QStringLiteral("not_found"), QStringLiteral("没有这个文件。"));
        m_window->selectDocument(path);
        auto* open = m_window->findChild<QAction*>(QStringLiteral("openDocumentAction"));
        if (!open || !open->isEnabled()) return fail(QStringLiteral("cannot_open"), QStringLiteral("现在不能打开文档。"));
        open->trigger();
        auto* timer = new QTimer(this);
        auto elapsed = std::make_shared<int>(0);
        connect(timer, &QTimer::timeout, this, [this, timer, elapsed, path, done] {
            *elapsed += 250;
            const bool ready = m_window && m_window->documentOpened() && !m_window->documentBusy()
                && QFileInfo(m_window->openedDocumentPath()).canonicalFilePath().compare(
                       QFileInfo(path).canonicalFilePath(), Qt::CaseInsensitive) == 0;
            if (!ready && *elapsed < 120000) return;
            timer->deleteLater();
            QJsonObject report = stateReport();
            report.insert(QStringLiteral("ok"), ready);
            done(toolResult(report, !ready));
        });
        timer->start(250);
        return;
    }
    if (name == QStringLiteral("test.wait")) {
        waitForAssistant(m_said.size(), timeoutMs, done);
        return;
    }
    if (!ball) return fail(QStringLiteral("no_assistant"), QStringLiteral("没有助手。"));
    if (name == QStringLiteral("test.ask")) {
        const QString text = arguments.value(QStringLiteral("text")).toString();
        // Like a person: open the ball, then type once it takes input (opening it, and
        // moving the cursor, make it read the selection for the quick actions for a moment).
        if (!ball->expanded()) ball->expand();
        whenIdle(15000, [this, text, timeoutMs, done, fail] {
            AssistantBall* ball = m_window ? m_window->assistantBall() : nullptr;
            if (!ball) return fail(QStringLiteral("no_assistant"), QStringLiteral("没有助手。"));
            const int since = m_said.size();
            ball->submitText(text);
            // Everything typed shows as the person's message, except /新对话 and /历史.
            const bool panelCommand = text.trimmed().startsWith(QLatin1Char('/')) || text.trimmed().startsWith(QChar(0xFF0F));
            if (m_said.size() == since && !panelCommand)
                return fail(QStringLiteral("not_taken"), QStringLiteral("助手没有接收这条输入。"));
            waitForAssistant(since, timeoutMs, done);
        }, [fail] { fail(QStringLiteral("assistant_busy"), QStringLiteral("助手一直在忙。")); });
        return;
    }
    if (name == QStringLiteral("test.press")) {
        const QString title = arguments.value(QStringLiteral("button")).toString();
        QPushButton* target = nullptr;
        for (QPushButton* button : ball->findChildren<QPushButton*>())
            if (button->text() == title && button->isEnabled() && !button->isHidden()) target = button; // the newest
        if (!target) return fail(QStringLiteral("no_button"), QStringLiteral("没有可以按的“%1”。").arg(title));
        const int since = m_said.size();
        target->click();
        waitForAssistant(since, timeoutMs, done);
        return;
    }
    if (name == QStringLiteral("test.quick")) {
        const QString id = arguments.value(QStringLiteral("id")).toString();
        if (!ball->quickActionIds().contains(id))
            return fail(QStringLiteral("no_quick_action"),
                        QStringLiteral("现在没有这个快捷操作，可用的有：") + ball->quickActionIds().join(QStringLiteral("、")));
        if (!ball->expanded()) ball->expand();
        whenIdle(15000, [this, id, timeoutMs, done] {
            const int since = m_said.size();
            if (AssistantBall* ball = m_window ? m_window->assistantBall() : nullptr) ball->triggerQuickAction(id);
            waitForAssistant(since, timeoutMs, done);
        }, [fail] { fail(QStringLiteral("assistant_busy"), QStringLiteral("助手一直在忙。")); });
        return;
    }
    fail(QStringLiteral("unknown_tool"), QStringLiteral("没有这个测试工具：") + name);
}

bool McpServer::assistantIdle() const
{
    AssistantBall* ball = m_window ? m_window->assistantBall() : nullptr;
    return ball && !ball->working() && !m_window->documentBusy() && !m_window->assistantBusy();
}

void McpServer::whenIdle(int timeoutMs, const std::function<void()>& action, const std::function<void()>& timedOut)
{
    if (assistantIdle()) {
        action();
        return;
    }
    auto* timer = new QTimer(this);
    auto elapsed = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [this, timer, elapsed, timeoutMs, action, timedOut] {
        *elapsed += 100;
        const bool idle = assistantIdle();
        if (!idle && *elapsed < timeoutMs) return;
        timer->deleteLater();
        if (idle) action();
        else timedOut();
    });
    timer->start(100);
}

void McpServer::waitForAssistant(int since, int timeoutMs, const std::function<void(const QJsonObject&)>& done)
{
    auto* timer = new QTimer(this);
    auto elapsed = std::make_shared<int>(0);
    auto idleTicks = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [this, timer, elapsed, idleTicks, since, timeoutMs, done] {
        *elapsed += 250;
        AssistantBall* ball = m_window ? m_window->assistantBall() : nullptr;
        bool confirm = false;
        if (ball)
            for (QPushButton* button : ball->findChildren<QPushButton*>())
                confirm = confirm || (button->text() == QStringLiteral("执行") && button->isEnabled() && !button->isHidden());
        const bool idle = assistantIdle();
        *idleTicks = idle ? *idleTicks + 1 : 0;
        // Idle for a second (a retry or a follow-up has had time to start), or waiting for the person.
        const bool settled = *idleTicks >= 4 || confirm;
        if (!settled && *elapsed < timeoutMs) return;
        timer->deleteLater();
        QJsonObject report = assistantReport(since);
        report.insert(QStringLiteral("ok"), settled);
        report.insert(QStringLiteral("waitingForConfirmation"), confirm);
        report.insert(QStringLiteral("timedOut"), !settled);
        report.insert(QStringLiteral("seconds"), *elapsed / 1000.0);
        done(toolResult(report, !settled));
    });
    timer->start(250);
}

QJsonObject McpServer::assistantReport(int since) const
{
    QJsonArray said;
    for (int i = qMax(0, since); i < m_said.size(); ++i) said.append(m_said[i]);
    QStringList buttons;
    if (AssistantBall* ball = m_window ? m_window->assistantBall() : nullptr)
        for (QPushButton* button : ball->findChildren<QPushButton*>()) {
            const QString kind = button->objectName();
            if ((kind == QStringLiteral("assistantPrimary") || kind == QStringLiteral("assistantSecondary"))
                && button->isEnabled() && !button->isHidden() && !buttons.contains(button->text()))
                buttons << button->text();
        }
    QJsonObject report = stateReport();
    report.insert(QStringLiteral("said"), said);
    report.insert(QStringLiteral("buttons"), QJsonArray::fromStringList(buttons));
    // What the model itself answered for the last office task, so a failed test shows
    // whether the model or the program got it wrong.
    if (m_window) report.insert(QStringLiteral("lastTaskAnswer"), m_window->lastTaskAnswer().left(8000));
    return report;
}

QJsonObject McpServer::stateReport() const
{
    if (!m_window) return failureJson(QStringLiteral("no_window"), QStringLiteral("窗口已关闭。"));
    QJsonObject state{{QStringLiteral("documentOpened"), m_window->documentOpened()},
                      {QStringLiteral("document"), m_window->openedDocumentPath()},
                      {QStringLiteral("documentBusy"), m_window->documentBusy()},
                      {QStringLiteral("conversation"), m_window->conversation().id},
                      {QStringLiteral("conversationMessages"), int(m_window->conversation().messages.size())}};
    if (LocalAssistant* ai = m_window->localAssistant())
        state.insert(QStringLiteral("ai"), QJsonObject{{QStringLiteral("state"), aiStateName(ai->state())},
                                                       {QStringLiteral("model"), ai->model()},
                                                       {QStringLiteral("builtin"), ai->builtin()},
                                                       {QStringLiteral("tokensPerSecond"), ai->lastSpeed()}});
    if (AssistantBall* ball = m_window->assistantBall()) {
        state.insert(QStringLiteral("working"), ball->working() || m_window->assistantBusy());
        state.insert(QStringLiteral("quickActions"), QJsonArray::fromStringList(ball->quickActionIds()));
        state.insert(QStringLiteral("lastMessage"), ball->lastMessageText());
    }
    return state;
}

} // namespace openoffice
