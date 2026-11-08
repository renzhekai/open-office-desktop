#pragma once

// Test-only stand-in for an Ollama server on a loopback port: canned replies
// per path, chat replies in order, and a record of every request body so a
// test can check exactly what would have reached the model.

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>

class FakeOllama {
public:
    FakeOllama()
    {
        server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (QTcpSocket* socket = server.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] { serve(socket, *buffer); });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QByteArray url() const { return "http://127.0.0.1:" + QByteArray::number(server.serverPort()); }

    // The usual answers of a running Ollama with these models installed.
    void running(const QJsonArray& models)
    {
        replies.insert(QStringLiteral("/api/version"), R"({"version":"0.40.15"})");
        replies.insert(QStringLiteral("/api/tags"),
                       QJsonDocument(QJsonObject{{QStringLiteral("models"), models}}).toJson(QJsonDocument::Compact));
    }

    static QJsonObject model(const QString& name, double gigabytes, const QString& family, const QStringList& capabilities)
    {
        return {{QStringLiteral("name"), name},
                {QStringLiteral("size"), gigabytes * (1LL << 30)},
                {QStringLiteral("details"), QJsonObject{{QStringLiteral("family"), family}}},
                {QStringLiteral("capabilities"), QJsonArray::fromStringList(capabilities)}};
    }

    // One streamed chat reply: content pieces, then optionally one tool call.
    static QByteArray chat(const QStringList& pieces, const QString& tool = QString(), const QJsonObject& arguments = {})
    {
        QByteArray stream;
        const auto line = [&stream](const QJsonObject& message, bool done) {
            stream += QJsonDocument(QJsonObject{{QStringLiteral("model"), QStringLiteral("fake")},
                                                {QStringLiteral("message"), message},
                                                {QStringLiteral("done"), done}}).toJson(QJsonDocument::Compact) + '\n';
        };
        for (const QString& piece : pieces)
            line({{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), piece}}, false);
        if (!tool.isEmpty()) {
            const QJsonObject call{{QStringLiteral("id"), QStringLiteral("call_1")},
                                   {QStringLiteral("function"), QJsonObject{{QStringLiteral("index"), 0},
                                                                            {QStringLiteral("name"), tool},
                                                                            {QStringLiteral("arguments"), arguments}}}};
            line({{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QString()},
                  {QStringLiteral("tool_calls"), QJsonArray{call}}}, false);
        }
        line({{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QString()}}, true);
        return stream;
    }

    QList<QJsonObject> chatRequests() const
    {
        QList<QJsonObject> result;
        for (const auto& request : requests)
            if (request.first == QStringLiteral("/api/chat")) result.append(request.second);
        return result;
    }

    QTcpServer server;
    QHash<QString, QByteArray> replies;
    QList<QByteArray> chats;
    QList<QPair<QString, QJsonObject>> requests;

private:
    void serve(QTcpSocket* socket, QByteArray& buffer)
    {
        buffer += socket->readAll();
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QByteArray head = buffer.left(headerEnd);
        int length = 0;
        for (const QByteArray& line : head.split('\n'))
            if (line.trimmed().toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
        if (buffer.size() < headerEnd + 4 + length) return;
        const QString path = QString::fromLatin1(head.split(' ').value(1));
        requests.append({path, QJsonDocument::fromJson(buffer.mid(headerEnd + 4, length)).object()});
        buffer.clear();
        const QByteArray body = path == QStringLiteral("/api/chat") && !chats.isEmpty() ? chats.takeFirst() : replies.value(path);
        socket->write((body.isEmpty() ? QByteArray("HTTP/1.1 404 Not Found") : QByteArray("HTTP/1.1 200 OK"))
                      + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
                      + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
};

// Sets (or, with a null value, removes) an environment variable for one scope.
class ScopedEnvironment {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : m_name(name), m_had(qEnvironmentVariableIsSet(name)), m_old(qgetenv(name))
    {
        if (value.isNull()) qunsetenv(name);
        else qputenv(name, value);
    }
    ~ScopedEnvironment()
    {
        if (m_had) qputenv(m_name, m_old);
        else qunsetenv(m_name);
    }

private:
    const char* m_name;
    bool m_had;
    QByteArray m_old;
};

// A loopback port with nothing listening on it.
inline QByteArray closedOllamaUrl()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    return "http://127.0.0.1:" + QByteArray::number(port);
}
