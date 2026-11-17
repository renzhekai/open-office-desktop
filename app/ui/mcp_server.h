#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>

class QTcpServer;
class QTcpSocket;

namespace openoffice {

class MainWindow;
struct ConversationMessage;

// The assistant's commands as an MCP server (Model Context Protocol, JSON-RPC 2.0
// over HTTP POST, the "Streamable HTTP" transport answered with plain JSON), so
// another AI program, or a test, can use the same commands the built-in assistant
// uses: read the document's structure and tables, change cells and formatting,
// insert, replace, undo, save.
//
// Only on this computer: it listens on 127.0.0.1, every request must carry the
// bearer token written next to the profile, and requests from web pages (an
// Origin or Host that is not this computer) are refused. Off unless the program
// is started with --mcp-port.
//
// With test tools (--mcp-test) it also drives the assistant ball the way a
// person does (open a document, type to the assistant, press a reply button, a
// quick action) and waits until the assistant is done, so the assistant can be
// tested on a real editor without anyone at the screen.
class McpServer final : public QObject {
    Q_OBJECT

public:
    McpServer(MainWindow* window, bool testTools, QObject* parent = nullptr);

    bool listen(quint16 port, QString* error); // 0: any free port
    quint16 port() const;
    QString token() const { return m_token; }
    // {url, token, testTools, pid} for clients; written new each start.
    bool writeEndpoint(const QString& path, QString* error) const;

    // One JSON-RPC message. Requests get exactly one reply; notifications none.
    void handleMessage(const QJsonObject& message, const std::function<void(const QJsonObject&)>& reply);
    QJsonArray tools() const;

private:
    struct Client {
        QByteArray buffer;
    };
    void accept();
    void readClient(QTcpSocket* socket);
    void respond(QTcpSocket* socket, int status, const QByteArray& body, const QByteArray& type = "application/json");
    void callTool(const QString& name, const QJsonObject& arguments, const std::function<void(const QJsonObject&)>& done);
    void callTestTool(const QString& name, const QJsonObject& arguments, const std::function<void(const QJsonObject&)>& done);
    // Calls done when the assistant has been idle for a moment, waits for the person
    // (a 执行 card), or the time is up; with what was said since `since`.
    void waitForAssistant(int since, int timeoutMs, const std::function<void(const QJsonObject&)>& done);
    bool assistantIdle() const;
    // Runs action as soon as the assistant is idle, or timedOut after timeoutMs.
    void whenIdle(int timeoutMs, const std::function<void()>& action, const std::function<void()>& timedOut);
    QJsonObject assistantReport(int since) const;
    QJsonObject stateReport() const;

    QPointer<MainWindow> m_window;
    bool m_testTools = false;
    QTcpServer* m_server = nullptr;
    QString m_token;
    QHash<QTcpSocket*, Client> m_clients;
    QList<QJsonObject> m_said; // every bubble the assistant ball recorded, in order
};

} // namespace openoffice
