#include "local_engine.h"

#include "ollama_client.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace openoffice {
namespace {

constexpr int kStartTimeoutMs = 240000; // a large model can take minutes to load the first time

QString randomKey()
{
    QString key;
    for (int i = 0; i < 4; ++i) key += QString::number(QRandomGenerator::system()->generate64(), 16);
    return key;
}

int freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const int port = probe.serverPort();
    probe.close();
    return port;
}

void hideConsole(QProcess* process)
{
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#else
    Q_UNUSED(process);
#endif
}

} // namespace

LocalEngine::LocalEngine(QObject* parent) : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_network->setProxy(QNetworkProxy::NoProxy);
    m_poll = new QTimer(this);
    m_poll->setInterval(500);
    connect(m_poll, &QTimer::timeout, this, &LocalEngine::poll);
}

LocalEngine::~LocalEngine()
{
    stop();
}

QString LocalEngine::engineRoot()
{
    const QString configured = qEnvironmentVariable("OPEN_OFFICE_AI_ENGINE");
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ai-engine"));
}

QString LocalEngine::serverProgram(const QString& build)
{
    // The program layout ("vulkan", "cpu") and the downloaded release layout.
    const QDir root(engineRoot());
    const QStringList folders = build == QStringLiteral("vulkan")
        ? QStringList{QStringLiteral("vulkan"), QStringLiteral("win-vulkan-x64")}
        : QStringList{QStringLiteral("cpu"), QStringLiteral("win-cpu-x64")};
    for (const QString& folder : folders) {
        const QString program = root.filePath(folder + QStringLiteral("/llama-server.exe"));
        if (QFileInfo(program).isFile()) return program;
    }
    return {};
}

bool LocalEngine::available() const
{
    if (!qEnvironmentVariable("OPEN_OFFICE_AI_ENGINE_URL").isEmpty()) return true; // tests: an engine already running
    return !serverProgram(QStringLiteral("vulkan")).isEmpty() || !serverProgram(QStringLiteral("cpu")).isEmpty();
}

QString LocalEngine::pickDevice(const QString& deviceList, const QString& cardName)
{
    static const QRegularExpression line(QStringLiteral("^\\s*(\\w+\\d+):\\s*(.+?)\\s*\\((\\d+) MiB"));
    QString byName;
    QString byMemory;
    qint64 most = -1;
    for (const QString& text : deviceList.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = line.match(text);
        if (!match.hasMatch()) continue;
        const QString device = match.captured(1);
        const QString name = match.captured(2);
        if (!cardName.isEmpty() && byName.isEmpty()
            && (name.contains(cardName, Qt::CaseInsensitive) || cardName.contains(name, Qt::CaseInsensitive)))
            byName = device;
        // Integrated graphics report shared memory as their own; never prefer them.
        const bool integrated = name.contains(QStringLiteral("Intel(R) Graphics"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("UHD"), Qt::CaseInsensitive) || name.contains(QStringLiteral("Iris"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("Radeon(TM) Graphics"), Qt::CaseInsensitive);
        const qint64 memory = match.captured(3).toLongLong();
        if (!integrated && memory > most) {
            most = memory;
            byMemory = device;
        }
    }
    return !byName.isEmpty() ? byName : byMemory;
}

void LocalEngine::start(const QString& modelPath)
{
    if ((m_ready || m_starting) && m_modelPath == modelPath) return;
    stop();
    m_modelPath = modelPath;
    m_log.clear();
    m_starting = true;
    m_clock.start();

    const QString given = qEnvironmentVariable("OPEN_OFFICE_AI_ENGINE_URL");
    if (!given.isEmpty()) { // tests and development: talk to an engine that is already running
        m_url = QUrl(given);
        m_key = qEnvironmentVariable("OPEN_OFFICE_AI_ENGINE_KEY");
        m_deviceText = QStringLiteral("外部引擎");
        m_poll->start();
        return;
    }
    if (!QFileInfo(modelPath).isFile()) {
        fail(QStringLiteral("找不到模型文件：") + QDir::toNativeSeparators(modelPath));
        return;
    }
    const GpuInfo card = largestGpu();
    QString program = card.bytes > 0 ? serverProgram(QStringLiteral("vulkan")) : QString();
    QStringList arguments;
    if (!program.isEmpty()) {
        // Ask the engine which devices it sees, then use the card only.
        QProcess list;
        hideConsole(&list);
        list.setWorkingDirectory(QFileInfo(program).absolutePath());
        list.start(program, {QStringLiteral("--list-devices")});
        list.waitForFinished(20000);
        const QString device = pickDevice(QString::fromLocal8Bit(list.readAllStandardOutput() + list.readAllStandardError()),
                                          card.name);
        if (device.isEmpty()) {
            program.clear(); // no usable card after all
        } else {
            arguments << QStringLiteral("--device") << device << QStringLiteral("-ngl") << QStringLiteral("99");
            m_deviceText = QStringLiteral("显卡 ") + card.name;
        }
    }
    if (program.isEmpty()) {
        program = serverProgram(QStringLiteral("cpu"));
        if (program.isEmpty()) {
            fail(QStringLiteral("没有找到内置 AI 引擎。"));
            return;
        }
        const int threads = qMax(2, QThread::idealThreadCount() / 2); // leave room for the office suite
        arguments << QStringLiteral("-ngl") << QStringLiteral("0") << QStringLiteral("-c") << QStringLiteral("8192")
                  << QStringLiteral("-t") << QString::number(threads);
        m_deviceText = QStringLiteral("处理器");
    }
    const int port = freePort();
    m_key = randomKey();
    m_url = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
    arguments << QStringLiteral("-m") << QDir::toNativeSeparators(modelPath) << QStringLiteral("--host") << QStringLiteral("127.0.0.1")
              << QStringLiteral("--port") << QString::number(port) << QStringLiteral("--api-key") << m_key
              << QStringLiteral("--offline") << QStringLiteral("-np") << QStringLiteral("1")
              << QStringLiteral("--reasoning-budget") << QStringLiteral("1024") // a reasoning phase stays short
              << QStringLiteral("--fit") << QStringLiteral("on");

    m_process = new QProcess(this);
    hideConsole(m_process);
    m_process->setWorkingDirectory(QFileInfo(program).absolutePath());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        m_log += m_process->readAll();
        if (m_log.size() > 16000) m_log = m_log.right(12000);
    });
    connect(m_process, &QProcess::finished, this, [this] {
        if (m_starting) fail(explainExit());
        else if (m_ready) {
            m_ready = false;
            emit failed(QStringLiteral("内置引擎意外停止了。") + explainExit());
        }
    });
    m_process->start(program, arguments);
    if (!m_process->waitForStarted(10000)) {
        fail(QStringLiteral("内置引擎无法启动：") + m_process->errorString());
        return;
    }
    m_poll->start();
}

void LocalEngine::poll()
{
    if (!m_starting) {
        m_poll->stop();
        return;
    }
    if (m_clock.elapsed() > kStartTimeoutMs) {
        fail(QStringLiteral("内置引擎在 4 分钟内没有准备好。"));
        return;
    }
    QUrl health = m_url;
    health.setPath(QStringLiteral("/health"));
    QNetworkRequest request(health);
    request.setTransferTimeout(3000);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!m_starting) return;
        const QJsonObject status = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() == QNetworkReply::NoError && status.value(QStringLiteral("status")).toString() == QStringLiteral("ok")) {
            m_starting = false;
            m_ready = true;
            m_poll->stop();
            emit ready();
        }
    });
}

void LocalEngine::fail(const QString& message)
{
    const bool wasStarting = m_starting;
    m_starting = false;
    m_ready = false;
    m_poll->stop();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(3000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (wasStarting) emit failed(message);
}

QString LocalEngine::explainExit() const
{
    const QString log = QString::fromUtf8(m_log);
    if (log.contains(QStringLiteral("failed to load model")) || log.contains(QStringLiteral("unknown model architecture")))
        return QStringLiteral("模型文件读不出来，可能没下载完整，或者不是这个引擎支持的格式。");
    if (log.contains(QStringLiteral("out of memory"), Qt::CaseInsensitive)
        || log.contains(QStringLiteral("failed to allocate"), Qt::CaseInsensitive))
        return QStringLiteral("内存或显存不够。请关掉一些程序，或在设置里换一个小一点的模型。");
    const QStringList lines = log.trimmed().split(QLatin1Char('\n'));
    return QStringLiteral("内置引擎退出了：") + (lines.isEmpty() ? QString() : lines.last().trimmed().left(200));
}

void LocalEngine::stop()
{
    m_poll->stop();
    m_starting = false;
    m_ready = false;
    if (!m_process) return;
    m_process->disconnect(this);
    m_process->kill(); // a console program with no window: ending it is the only way
    m_process->waitForFinished(3000);
    delete m_process;
    m_process = nullptr;
}

} // namespace openoffice
