#include "engine_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QNetworkProxyFactory>
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

// A cloud service goes through the proxy the system is set to use.
class SystemProxies final : public QNetworkProxyFactory {
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override { return systemProxyForQuery(query); }
};

QString errorFrom(const QByteArray& body, QNetworkReply* reply, const QString& service)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    const QJsonValue error = object.value(QStringLiteral("error"));
    const QString message = error.isObject() ? error.toObject().value(QStringLiteral("message")).toString() : error.toString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!service.isEmpty()) {
        // What people can do about it first; the service's own words after.
        QString meaning;
        if (status == 401 || status == 403) meaning = QStringLiteral("API Key 不对、过期，或没有使用这个模型的权限。");
        else if (status == 404) meaning = QStringLiteral("接口地址或模型名称不对。");
        else if (status == 402 || status == 429) meaning = QStringLiteral("请求太频繁，或账户额度用完了。");
        else if (status >= 500) meaning = QStringLiteral("服务暂时出问题了，请稍后再试。");
        else if (reply->error() == QNetworkReply::HostNotFoundError || reply->error() == QNetworkReply::ConnectionRefusedError)
            meaning = QStringLiteral("连不上，请检查网络和接口地址。");
        else if (reply->error() == QNetworkReply::TimeoutError || reply->error() == QNetworkReply::OperationCanceledError)
            meaning = QStringLiteral("没有及时响应。");
        else if (reply->error() == QNetworkReply::SslHandshakeFailedError)
            meaning = QStringLiteral("安全连接失败，请检查网络或代理设置。");
        QString text = service + QStringLiteral("：") + (meaning.isEmpty() ? reply->errorString() : meaning);
        if (!message.isEmpty()) text += QStringLiteral("（") + message.left(200) + QStringLiteral("）");
        return text;
    }
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

void EngineClient::setCloud(const QString& serviceName)
{
    m_service = serviceName;
    if (cloud()) m_network->setProxyFactory(new SystemProxies); // the manager owns it
    else m_network->setProxy(QNetworkProxy::NoProxy);
}

QUrl EngineClient::serviceUrl(const QUrl& base, const QString& path)
{
    QUrl url = base;
    QString basePath = url.path();
    while (basePath.endsWith(QLatin1Char('/'))) basePath.chop(1);
    url.setPath(basePath + QLatin1Char('/') + path);
    return url;
}

void EngineClient::fetchModels()
{
    QNetworkRequest request(serviceUrl(m_base, QStringLiteral("models")));
    if (!m_key.isEmpty()) request.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    request.setTransferTimeout(20000);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            emit modelsFetched({}, errorFrom(body, reply, m_service.isEmpty() ? QStringLiteral("服务") : m_service));
            return;
        }
        QStringList models;
        for (const QJsonValue& value : QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray()) {
            const QString id = value.toObject().value(QStringLiteral("id")).toString();
            if (!id.isEmpty() && !models.contains(id)) models.append(id);
        }
        emit modelsFetched(models, models.isEmpty() ? QStringLiteral("服务没有列出模型，请手动填写模型名称。") : QString());
    });
}

QJsonArray EngineClient::toOpenAiMessages(const QJsonArray& messages)
{
    // OpenAI-style services (DeepSeek, OpenAI...) refuse a conversation in which a tool call
    // has no answer before the next message, or an answer has no call: every call gets one
    // ("没有执行" if the program never ran it), and an answer to nothing is left out.
    QJsonArray result;
    QStringList pendingIds; // calls still waiting for their result, oldest first
    int counter = 0;
    const auto answerTheRest = [&result, &pendingIds] {
        for (const QString& id : pendingIds)
            result.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("tool_call_id"), id},
                                      {QStringLiteral("content"), QStringLiteral(R"({"ok":false,"code":"not_run","message":"没有执行这个操作。"})")}});
        pendingIds.clear();
    };
    for (const QJsonValue& value : messages) {
        QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if (role != QStringLiteral("tool")) answerTheRest();
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
            if (pendingIds.isEmpty()) continue; // an answer to no call
            message.remove(QStringLiteral("tool_name"));
            message.insert(QStringLiteral("tool_call_id"), pendingIds.takeFirst());
        }
        result.append(message);
    }
    answerTheRest();
    return result;
}

QJsonObject EngineClient::cloudRequestBody(const QString& model, const QJsonArray& messages, const ChatOptions& options)
{
    QJsonObject body{{QStringLiteral("model"), model},
                     {QStringLiteral("messages"), toOpenAiMessages(messages)},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("temperature"), options.temperature},
                     {QStringLiteral("max_tokens"), 4096}};
    if (!options.tools.isEmpty()) body.insert(QStringLiteral("tools"), options.tools);
    // No JSON schema or reasoning switch: services differ in what they accept, and the
    // program reads the JSON out of the answer anyway.
    return body;
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
    // The engine serves the one model it was started with; a cloud service is told which.
    cancelChat();
    const quint64 id = ++m_chatId;
    QUrl url = m_base;
    if (cloud()) url = serviceUrl(m_base, QStringLiteral("chat/completions"));
    else url.setPath(QStringLiteral("/v1/chat/completions"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_key.isEmpty()) request.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    request.setTransferTimeout(300000); // inactivity limit; streamed bytes keep it alive
    const QJsonObject body = cloud() ? cloudRequestBody(model, messages, options) : requestBody(messages, options);
    QNetworkReply* reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
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
            emit chatFinished(id, false, *content, toolCalls,
                              (cloud() ? m_service + QStringLiteral(" 返回错误：") : QStringLiteral("内置引擎返回错误：")) + *failure);
        } else if ((reply->error() != QNetworkReply::NoError && !*done) || (status >= 400)) {
            emit chatFinished(id, false, *content, toolCalls, errorFrom(*raw, reply, m_service));
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
