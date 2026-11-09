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
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>
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

    // One streamed answer of the built-in engine (OpenAI server-sent events):
    // content pieces, then optionally one tool call whose arguments arrive in two pieces.
    static QByteArray sse(const QStringList& pieces, const QString& tool = QString(), const QJsonObject& arguments = {},
                          double tokensPerSecond = 42)
    {
        QByteArray stream;
        const auto event = [&stream](const QJsonObject& chunk) {
            stream += "data: " + QJsonDocument(chunk).toJson(QJsonDocument::Compact) + "\n\n";
        };
        const auto delta = [](const QJsonObject& value) {
            return QJsonObject{{QStringLiteral("choices"), QJsonArray{QJsonObject{{QStringLiteral("index"), 0},
                                                                                  {QStringLiteral("delta"), value}}}}};
        };
        for (const QString& piece : pieces) event(delta({{QStringLiteral("content"), piece}}));
        if (!tool.isEmpty()) {
            const QString text = QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact));
            const int half = text.size() / 2;
            event(delta({{QStringLiteral("tool_calls"), QJsonArray{QJsonObject{
                {QStringLiteral("index"), 0}, {QStringLiteral("id"), QStringLiteral("call_7")}, {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), text.left(half)}}}}}}}));
            event(delta({{QStringLiteral("tool_calls"), QJsonArray{QJsonObject{
                {QStringLiteral("index"), 0}, {QStringLiteral("function"), QJsonObject{{QStringLiteral("arguments"), text.mid(half)}}}}}}}));
        }
        QJsonObject last = delta({});
        QJsonObject choice = last.value(QStringLiteral("choices")).toArray().first().toObject();
        choice.insert(QStringLiteral("finish_reason"), tool.isEmpty() ? QStringLiteral("stop") : QStringLiteral("tool_calls"));
        last.insert(QStringLiteral("choices"), QJsonArray{choice});
        last.insert(QStringLiteral("timings"), QJsonObject{{QStringLiteral("predicted_per_second"), tokensPerSecond}});
        event(last);
        stream += "data: [DONE]\n\n";
        return stream;
    }

    QList<QJsonObject> chatRequests() const
    {
        QList<QJsonObject> result;
        for (const auto& request : requests)
            if (request.first == QStringLiteral("/api/chat") || request.first == QStringLiteral("/v1/chat/completions"))
                result.append(request.second);
        return result;
    }

    struct Response {
        int status = 200;
        QByteArray body;
    };
    // Optional: answers a request before the canned replies (path without the query; lower-case header names).
    std::function<bool(const QString& path, const QMap<QByteArray, QByteArray>& headers, Response* response)> handler;

    QTcpServer server;
    QHash<QString, QByteArray> replies;
    QList<QByteArray> chats;
    QList<QPair<QString, QJsonObject>> requests;
    QList<QMap<QByteArray, QByteArray>> requestHeaders;

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
        const QString target = QString::fromLatin1(head.split(' ').value(1));
        const QString path = target.section(QLatin1Char('?'), 0, 0);
        QMap<QByteArray, QByteArray> headers;
        for (const QByteArray& line : head.split('\n').mid(1)) {
            const int colon = line.indexOf(':');
            if (colon > 0) headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
        }
        requests.append({path, QJsonDocument::fromJson(buffer.mid(headerEnd + 4, length)).object()});
        requestHeaders.append(headers);
        buffer.clear();
        Response response;
        if (!(handler && handler(path, headers, &response))) {
            const bool chat = path == QStringLiteral("/api/chat") || path == QStringLiteral("/v1/chat/completions");
            response.body = chat && !chats.isEmpty() ? chats.takeFirst() : replies.value(path);
            response.status = response.body.isEmpty() ? 404 : 200;
        }
        const QByteArray reason = response.status == 200 ? "OK" : response.status == 206 ? "Partial Content" : "Error";
        socket->write("HTTP/1.1 " + QByteArray::number(response.status) + ' ' + reason
                      + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(response.body.size())
                      + "\r\nConnection: close\r\n\r\n" + response.body);
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
