#include "ollama_client.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi.h>
#endif

namespace openoffice {
namespace {

QUrl normalise(QString text)
{
    text = text.trimmed();
    if (text.isEmpty()) return {};
    if (!text.contains(QStringLiteral("://"))) text.prepend(QStringLiteral("http://"));
    QUrl url(text);
    if (!url.isValid() || url.host().isEmpty()) return {};
    // Ollama treats 0.0.0.0 as "listen everywhere"; this computer reaches it locally.
    if (url.host() == QStringLiteral("0.0.0.0")) url.setHost(QStringLiteral("127.0.0.1"));
    if (url.port() < 0) url.setPort(11434);
    url.setPath(QString());
    return url;
}

bool cloudModel(const QJsonObject& model)
{
    const QString name = model.value(QStringLiteral("name")).toString();
    return model.contains(QStringLiteral("remote_host")) || model.contains(QStringLiteral("remote_model"))
        || name.endsWith(QStringLiteral(":cloud")) || name.endsWith(QStringLiteral("-cloud"));
}

QString errorText(QNetworkReply* reply)
{
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("连接不到本机的 Ollama。");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("本机的 Ollama 没有及时响应。");
    default:
        return reply->errorString();
    }
}

} // namespace

OllamaClient::OllamaClient(QObject* parent) : QObject(parent), m_base(defaultBaseUrl())
{
    m_network = new QNetworkAccessManager(this);
    m_network->setProxy(QNetworkProxy::NoProxy); // loopback traffic must never go through a proxy
}

QUrl OllamaClient::defaultBaseUrl()
{
    for (const char* variable : {"OPEN_OFFICE_OLLAMA_URL", "OLLAMA_HOST"}) {
        const QUrl url = normalise(qEnvironmentVariable(variable));
        if (url.isValid() && isLoopback(url)) return url;
    }
    return QUrl(QStringLiteral("http://127.0.0.1:11434"));
}

bool OllamaClient::isLoopback(const QUrl& url)
{
    const QString host = url.host();
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) return true;
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

QNetworkReply* OllamaClient::get(const QString& path, int timeoutMs)
{
    QUrl url = m_base;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    return m_network->get(request);
}

QNetworkReply* OllamaClient::post(const QString& path, const QByteArray& body, int idleTimeoutMs)
{
    QUrl url = m_base;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(idleTimeoutMs); // an inactivity limit: streamed bytes keep it alive
    return m_network->post(request, body);
}

void OllamaClient::probe()
{
    QNetworkReply* reply = get(QStringLiteral("/api/version"), 2500);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit probed(false, QString());
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        emit probed(true, object.value(QStringLiteral("version")).toString());
    });
}

void OllamaClient::listModels()
{
    QNetworkReply* reply = get(QStringLiteral("/api/tags"), 6000);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit modelsListed({}, errorText(reply));
            return;
        }
        QList<LocalModel> models;
        const QJsonArray list = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("models")).toArray();
        for (const QJsonValue& value : list) {
            const QJsonObject object = value.toObject();
            if (cloudModel(object)) continue; // would send prompts to a remote service
            LocalModel model;
            model.name = object.value(QStringLiteral("name")).toString();
            model.bytes = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
            const QJsonObject details = object.value(QStringLiteral("details")).toObject();
            model.family = details.value(QStringLiteral("family")).toString();
            model.parameterSize = details.value(QStringLiteral("parameter_size")).toString();
            const QJsonArray capabilities = object.value(QStringLiteral("capabilities")).toArray();
            model.tools = capabilities.contains(QStringLiteral("tools"));
            model.thinking = capabilities.contains(QStringLiteral("thinking"));
            if (!model.name.isEmpty()) models.append(model);
        }
        emit modelsListed(models, QString());
    });
}

void OllamaClient::warmUp(const QString& model)
{
    // A generate request without a prompt only loads the model.
    const QJsonObject body{{QStringLiteral("model"), model}, {QStringLiteral("keep_alive"), QStringLiteral("30m")}};
    QNetworkReply* reply = post(QStringLiteral("/api/generate"), QJsonDocument(body).toJson(QJsonDocument::Compact), 180000);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

quint64 OllamaClient::chat(const QString& model, const QJsonArray& messages, const ChatOptions& options)
{
    cancelChat();
    const quint64 id = ++m_chatId;
    QJsonObject settings{{QStringLiteral("temperature"), options.temperature}};
    if (options.contextLength > 0) settings.insert(QStringLiteral("num_ctx"), options.contextLength);
    QJsonObject body{{QStringLiteral("model"), model}, {QStringLiteral("messages"), messages},
                     {QStringLiteral("stream"), true}, {QStringLiteral("keep_alive"), QStringLiteral("30m")},
                     {QStringLiteral("options"), settings}};
    if (!options.tools.isEmpty()) body.insert(QStringLiteral("tools"), options.tools);
    if (!options.format.isEmpty()) body.insert(QStringLiteral("format"), options.format);
    if (options.think == QStringLiteral("false")) body.insert(QStringLiteral("think"), false);
    else if (!options.think.isEmpty()) body.insert(QStringLiteral("think"), options.think);
    QNetworkReply* reply = post(QStringLiteral("/api/chat"), QJsonDocument(body).toJson(QJsonDocument::Compact), 300000);
    m_chat = reply;
    auto buffer = std::make_shared<QByteArray>();
    auto content = std::make_shared<QString>();
    auto toolCalls = std::make_shared<QJsonArray>();
    auto done = std::make_shared<bool>(false);
    auto failure = std::make_shared<QString>();
    const auto consume = [this, id, buffer, content, toolCalls, done, failure](bool flush) {
        int newline;
        while ((newline = buffer->indexOf('\n')) >= 0 || (flush && !buffer->isEmpty())) {
            const QByteArray line = newline >= 0 ? buffer->left(newline) : *buffer;
            buffer->remove(0, newline >= 0 ? newline + 1 : buffer->size());
            if (line.trimmed().isEmpty()) continue;
            const QJsonObject chunk = QJsonDocument::fromJson(line).object();
            if (chunk.contains(QStringLiteral("error"))) {
                *failure = chunk.value(QStringLiteral("error")).toString();
                continue;
            }
            const QJsonObject message = chunk.value(QStringLiteral("message")).toObject();
            const QString piece = message.value(QStringLiteral("content")).toString();
            if (!piece.isEmpty()) {
                content->append(piece);
                emit chatDelta(id, piece);
            }
            for (const QJsonValue& call : message.value(QStringLiteral("tool_calls")).toArray()) toolCalls->append(call);
            if (chunk.value(QStringLiteral("done")).toBool()) {
                *done = true;
                const double count = chunk.value(QStringLiteral("eval_count")).toDouble();
                const double nanoseconds = chunk.value(QStringLiteral("eval_duration")).toDouble();
                if (count > 0 && nanoseconds > 0) m_tokensPerSecond = count / (nanoseconds / 1e9);
            }
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, buffer, consume] {
        buffer->append(reply->readAll());
        consume(false);
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply, buffer, content, toolCalls, done, failure, consume] {
        reply->deleteLater();
        buffer->append(reply->readAll());
        consume(true);
        if (m_chat == reply) m_chat = nullptr;
        if (!failure->isEmpty()) {
            emit chatFinished(id, false, *content, *toolCalls, QStringLiteral("模型返回错误：") + *failure);
        } else if (reply->error() != QNetworkReply::NoError && !*done) {
            emit chatFinished(id, false, *content, *toolCalls, errorText(reply));
        } else {
            emit chatFinished(id, true, *content, *toolCalls, QString());
        }
    });
    return id;
}

void OllamaClient::cancelChat()
{
    if (m_chat) m_chat->abort(); // finishes with OperationCanceledError
}

void OllamaClient::pull(const QString& model)
{
    cancelPull();
    const QJsonObject body{{QStringLiteral("model"), model}, {QStringLiteral("stream"), true}};
    QNetworkReply* reply = post(QStringLiteral("/api/pull"), QJsonDocument(body).toJson(QJsonDocument::Compact), 180000);
    m_pull = reply;
    auto buffer = std::make_shared<QByteArray>();
    auto success = std::make_shared<bool>(false);
    auto failure = std::make_shared<QString>();
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, buffer, success, failure] {
        buffer->append(reply->readAll());
        int newline;
        while ((newline = buffer->indexOf('\n')) >= 0) {
            const QJsonObject line = QJsonDocument::fromJson(buffer->left(newline)).object();
            buffer->remove(0, newline + 1);
            if (line.contains(QStringLiteral("error"))) {
                *failure = line.value(QStringLiteral("error")).toString();
                continue;
            }
            const QString status = line.value(QStringLiteral("status")).toString();
            if (status == QStringLiteral("success")) *success = true;
            emit pullProgress(status, static_cast<qint64>(line.value(QStringLiteral("completed")).toDouble()),
                              static_cast<qint64>(line.value(QStringLiteral("total")).toDouble()));
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, success, failure] {
        reply->deleteLater();
        if (m_pull == reply) m_pull = nullptr;
        if (*success) emit pullFinished(true, QString());
        else emit pullFinished(false, !failure->isEmpty() ? *failure : errorText(reply));
    });
}

void OllamaClient::cancelPull()
{
    if (m_pull) m_pull->abort();
}

qint64 totalPhysicalMemory()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (::GlobalMemoryStatusEx(&status)) return static_cast<qint64>(status.ullTotalPhys);
#endif
    return 8LL * 1024 * 1024 * 1024;
}

qint64 totalVideoMemory()
{
    qint64 largest = 0;
#ifdef Q_OS_WIN
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(adapter->GetDesc1(&description)) && !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                largest = qMax<qint64>(largest, static_cast<qint64>(description.DedicatedVideoMemory));
            adapter->Release();
        }
        factory->Release();
    }
#endif
    // Integrated graphics report a small carve-out; local models do not run on them.
    return largest >= (3LL << 30) ? largest : 0;
}

bool runsSlowly(const LocalModel& model, qint64 videoBytes)
{
    if (videoBytes > 0) return model.bytes > videoBytes * 0.85; // part of it would run on the processor
    return model.bytes > static_cast<qint64>(3.5 * (1LL << 30));
}

QString pickInstalledModel(const QList<LocalModel>& models, qint64 memoryBytes, qint64 videoBytes)
{
    // A model file should leave room for the system and the office suite.
    const double budget = (memoryBytes > 0 ? memoryBytes : 8LL << 30) * 0.55;
    // Bigger is better quality, up to what still runs quickly here.
    const double sweetSpot = videoBytes > 0 ? qMin<double>(videoBytes * 0.85, 9.0 * (1LL << 30)) : 3.5 * (1LL << 30);
    QString best;
    double bestScore = -1e9;
    for (const LocalModel& model : models) {
        const QString key = (model.name + QLatin1Char(' ') + model.family).toLower();
        if (key.contains(QStringLiteral("embed")) || key.contains(QStringLiteral("bert"))) continue; // not chat models
        if (model.bytes > budget) continue;
        double score = model.tools ? 100 : 0; // only these can work on the document
        if (key.contains(QStringLiteral("qwen"))) score += 30;      // strong Chinese
        else if (key.contains(QStringLiteral("glm"))) score += 25;
        else if (key.contains(QStringLiteral("deepseek"))) score += 20;
        else if (key.contains(QStringLiteral("llama")) || key.contains(QStringLiteral("gemma"))
                 || key.contains(QStringLiteral("mistral"))) score += 10;
        // gpt-oss always reasons before answering: slower for short office jobs.
        if (key.contains(QStringLiteral("gpt-oss")) || key.contains(QStringLiteral("gptoss"))) score -= 25;
        score += runsSlowly(model, videoBytes) ? -60 : 60; // speed matters more than size
        score += qMin<double>(model.bytes, sweetSpot) / sweetSpot * 40; // within that, bigger writes better
        if (score > bestScore) {
            bestScore = score;
            best = model.name;
        }
    }
    return best;
}

ModelRecommendation recommendDownload(qint64 memoryBytes, qint64 videoBytes)
{
    const double video = videoBytes / double(1LL << 30);
    const double memory = memoryBytes / double(1LL << 30);
    if (video >= 7.5 && memory >= 12.0) return {QStringLiteral("qwen3:8b"), 5.2};
    if (video >= 4.5 && memory >= 7.5) return {QStringLiteral("qwen3:4b"), 2.5};
    // On the processor: the 4b model still answers at a usable pace with enough memory.
    if (memory >= 7.5) return {QStringLiteral("qwen3:4b"), 2.5};
    return {QStringLiteral("qwen3:1.7b"), 1.4};
}

QString explainPullError(const QString& error)
{
    const QString text = error.toLower();
    if (text.contains(QStringLiteral("non-public")) || text.contains(QStringLiteral("redirect target not allowed")))
        return QStringLiteral("下载被网络代理软件拦住了：代理（例如 Clash 的 Fake-IP / 虚拟网卡模式）把下载地址解析成了内网地址，"
                              "Ollama 为了安全拒绝下载。可以在代理软件的 Fake-IP 过滤里加入 +.r2.cloudflarestorage.com，"
                              "或者下载时暂时换个网络模式，然后再点下载。");
    if (text.contains(QStringLiteral("no space")) || text.contains(QStringLiteral("disk")))
        return QStringLiteral("磁盘空间不够了。请清理出空间（模型存在 Ollama 的模型文件夹里）后再试。");
    if (text.contains(QStringLiteral("file does not exist")) || text.contains(QStringLiteral("not found")))
        return QStringLiteral("模型库里找不到这个模型，可能名字变了。请更新 Ollama 后再试。");
    if (text.contains(QStringLiteral("timeout")) || text.contains(QStringLiteral("max retries"))
        || text.contains(QStringLiteral("connection")) || text.contains(QStringLiteral("tls"))
        || text.contains(QStringLiteral("dial tcp")))
        return QStringLiteral("网络连不上 Ollama 的模型库。请检查网络或代理设置后再试；已下载的部分会保留，下次接着下。");
    return error;
}

} // namespace openoffice
