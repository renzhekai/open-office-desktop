#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QProcess;
class QTimer;

namespace openoffice {

// The built-in AI engine: llama.cpp's server, shipped with the program under
// "ai-engine" (a graphics-card build and a processor build). It runs one model
// at a time, listens only on 127.0.0.1 with a one-off key, never touches the
// network itself (--offline), and stops with the program.
class LocalEngine : public QObject {
    Q_OBJECT

public:
    explicit LocalEngine(QObject* parent = nullptr);
    ~LocalEngine() override;

    // OPEN_OFFICE_AI_ENGINE, else "ai-engine" beside the program.
    static QString engineRoot();
    // The server program of one build ("vulkan" or "cpu"); empty when missing.
    static QString serverProgram(const QString& build);
    bool available() const;
    // Picks the engine device for a graphics card from the server's device list
    // (lines like "  Vulkan0: NVIDIA GeForce ... (11944 MiB, ...)"): the one named
    // like the card, else the one with the most memory that is not integrated graphics.
    static QString pickDevice(const QString& deviceList, const QString& cardName);

    // Starts the engine with this model (restarts it for another model).
    void start(const QString& modelPath);
    void stop();
    bool isReady() const { return m_ready; }
    bool isStarting() const { return m_starting; }
    QString modelPath() const { return m_modelPath; }
    QUrl url() const { return m_url; }
    QString apiKey() const { return m_key; }
    // "显卡 NVIDIA GeForce ..." or "处理器", for the settings page.
    QString deviceText() const { return m_deviceText; }

signals:
    void ready();
    void failed(QString message);

private:
    void poll();
    void fail(const QString& message);
    QString explainExit() const;

    QProcess* m_process = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QTimer* m_poll = nullptr;
    QElapsedTimer m_clock;
    QString m_modelPath;
    QUrl m_url;
    QString m_key;
    QString m_deviceText;
    QByteArray m_log; // the last part of the engine's output, for explaining failures
    bool m_ready = false;
    bool m_starting = false;
};

} // namespace openoffice
