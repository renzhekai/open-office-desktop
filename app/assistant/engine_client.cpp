#include "engine_client.h"

#include <QJsonDocument>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <memory>

namespace openoffice {
namespace {

struct ToolCallParts {
    QString id;
    QString name;
    QString arguments; // streamed in pieces
};

QString errorFrom(const QByteArray& body, QNetworkReply* reply)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    const QJsonValue error = object.value(QStringLiteral("error"));
    const QString message = error.isObject() ? error.toObject().value(QStringLiteral("message")).toString() : error.toString();
    if (!message.isEmpty()) return QStringLiteral("内置引擎返回错误：") + message;
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
        return QStringLiteral("内置引擎没有在运行。");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("内置引擎没有及时响应。");
    default:
        return reply->errorString();
    }
}

} // namespace

EngineClient::EngineClient(QObject* parent) : ChatClient(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_network->setProxy(QNetworkProxy::NoProxy); // the engine is on this computer; never through a proxy
}

void EngineClient::setEndpoint(const QUrl& base, const QString& apiKey)
{
    m_base = base;
    m_key = apiKey;
}

QJsonArray EngineClient::toOpenAiMessages(const QJsonArray& messages)
{
    QJsonArray result;
    QStringList pendingIds; // calls still waiting for their result, oldest first
    int counter = 0;
    for (const QJsonValue& value : messages) {
        QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if (role == QStringLiteral("assistant") && message.contains(QStringLiteral("tool_calls"))) {
            QJsonArray calls;
            for (const QJsonValue& callValue : message.value(QStringLiteral("tool_calls")).toArray()) {
                const QJsonObject call = callValue.toObject();
                const QJsonObject function = call.value(QStringLiteral("function")).toObject();
                QString id = call.value(QStringLiteral("id")).toString();
                if (id.isEmpty()) id = QStringLiteral("call_%1").arg(++counter);
                const QJsonValue arguments = function.value(QStringLiteral("arguments"));
                const QString text = arguments.isString()
                    ? arguments.toString()
                    : QString::fromUtf8(QJsonDocument(arguments.toObject()).toJson(QJsonDocument::Compact));
                calls.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("type"), QStringLiteral("function")},
                                         {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), function.value(QStringLiteral("name"))},
                                                                                  {QStringLiteral("arguments"), text}}}});
                pendingIds.append(id);
            }
            message.insert(QStringLiteral("tool_calls"), calls);
            if (!message.contains(QStringLiteral("content"))) message.insert(QStringLiteral("content"), QString());
        } else if (role == QStringLiteral("tool")) {
            message.remove(QStringLiteral("tool_name"));
            message.insert(QStringLiteral("tool_call_id"),
                           pendingIds.isEmpty() ? QStringLiteral("call_0") : pendingIds.takeFirst());
        }
        result.append(message);
    }
    return result;
}

QJsonObject EngineClient::requestBody(const QJsonArray& messages, const ChatOptions& options)
{
    QJsonObject body{{QStringLiteral("messages"), toOpenAiMessages(messages)},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("temperature"), options.temperature},
                     {QStringLiteral("max_tokens"), 4096}}; // a runaway answer stops in time
    if (!options.tools.isEmpty()) body.insert(QStringLiteral("tools"), options.tools);
    if (!options.format.isEmpty())
        body.insert(QStringLiteral("response_format"),
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")},
                                {QStringLiteral("json_schema"), QJsonObject{{QStringLiteral("name"), QStringLiteral("answer")},
                                                                            {QStringLiteral("schema"), options.format}}}});
    // Qwen3 reasons first unless told not to; office jobs are short and need speed.
    if (options.think == QStringLiteral("false"))
        body.insert(QStringLiteral("chat_template_kwargs"), QJsonObject{{QStringLiteral("enable_thinking"), false}});
    else if (!options.think.isEmpty())
        body.insert(QStringLiteral("reasoning_effort"), options.think);
    return body;
}

quint64 EngineClient::chat(const QString& model, const QJsonArray& messages, const ChatOptions& options)
{
    Q_UNUSED(model); // the engine serves the one model it was started with
    cancelChat();
    const quint64 id = ++m_chatId;
    QUrl url = m_base;
    url.setPath(QStringLiteral("/v1/chat/completions"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    request.setTransferTimeout(300000); // inactivity limit; streamed bytes keep it alive
    QNetworkReply* reply = m_network->post(request, QJsonDocument(requestBody(messages, options)).toJson(QJsonDocument::Compact));
    m_chat = reply;

    auto buffer = std::make_shared<QByteArray>();
    auto raw = std::make_shared<QByteArray>(); // non-stream bodies (errors)
    auto content = std::make_shared<QString>();
    auto calls = std::make_shared<QMap<int, ToolCallParts>>();
    auto done = std::make_shared<bool>(false);
    auto failure = std::make_shared<QString>();
    const auto consume = [this, id, buffer, raw, content, calls, done, failure](bool flush) {
        int newline;
        while ((newline = buffer->indexOf('\n')) >= 0 || (flush && !buffer->isEmpty())) {
            QByteArray line = newline >= 0 ? buffer->left(newline) : *buffer;
            buffer->remove(0, newline >= 0 ? newline + 1 : buffer->size());
            line = line.trimmed();
            if (line.isEmpty()) continue;
            if (!line.startsWith("data:")) {
                raw->append(line);
                continue;
            }
            const QByteArray data = line.mid(5).trimmed();
            if (data == "[DONE]") {
                *done = true;
                continue;
            }
            const QJsonObject chunk = QJsonDocument::fromJson(data).object();
            if (chunk.contains(QStringLiteral("error"))) {
                const QJsonValue error = chunk.value(QStringLiteral("error"));
                *failure = error.isObject() ? error.toObject().value(QStringLiteral("message")).toString() : error.toString();
                continue;
            }
            const QJsonObject timings = chunk.value(QStringLiteral("timings")).toObject();
            if (timings.contains(QStringLiteral("predicted_per_second")))
                m_tokensPerSecond = timings.value(QStringLiteral("predicted_per_second")).toDouble();
            const QJsonObject choice = chunk.value(QStringLiteral("choices")).toArray().first().toObject();
            const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
            const QString piece = delta.value(QStringLiteral("content")).toString();
            if (!piece.isEmpty()) {
                content->append(piece);
                emit chatDelta(id, piece);
            }
            for (const QJsonValue& callValue : delta.value(QStringLiteral("tool_calls")).toArray()) {
                const QJsonObject call = callValue.toObject();
                ToolCallParts& parts = (*calls)[call.value(QStringLiteral("index")).toInt()];
                if (call.contains(QStringLiteral("id")) && !call.value(QStringLiteral("id")).toString().isEmpty())
                    parts.id = call.value(QStringLiteral("id")).toString();
                const QJsonObject function = call.value(QStringLiteral("function")).toObject();
                if (function.contains(QStringLiteral("name"))) parts.name += function.value(QStringLiteral("name")).toString();
                parts.arguments += function.value(QStringLiteral("arguments")).toString();
            }
            if (!choice.value(QStringLiteral("finish_reason")).toString().isEmpty()) *done = true;
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, buffer, consume] {
        buffer->append(reply->readAll());
        consume(false);
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply, buffer, raw, content, calls, done, failure, consume] {
        reply->deleteLater();
        buffer->append(reply->readAll());
        consume(true);
        if (m_chat == reply) m_chat = nullptr;
        // Back in the assistant's shape: arguments parsed, ids kept.
        QJsonArray toolCalls;
        for (const ToolCallParts& parts : *calls) {
            const QJsonDocument arguments = QJsonDocument::fromJson(parts.arguments.toUtf8());
            toolCalls.append(QJsonObject{
                {QStringLiteral("id"), parts.id},
                {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), parts.name},
                                                         {QStringLiteral("arguments"), arguments.isObject() ? QJsonValue(arguments.object())
                                                                                                           : QJsonValue(parts.arguments)}}}});
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!failure->isEmpty()) {
            emit chatFinished(id, false, *content, toolCalls, QStringLiteral("内置引擎返回错误：") + *failure);
        } else if ((reply->error() != QNetworkReply::NoError && !*done) || (status >= 400)) {
            emit chatFinished(id, false, *content, toolCalls, errorFrom(*raw, reply));
        } else {
            emit chatFinished(id, true, *content, toolCalls, QString());
        }
    });
    return id;
}

void EngineClient::cancelChat()
{
    if (m_chat) m_chat->abort(); // finishes with OperationCanceledError
}

} // namespace openoffice
