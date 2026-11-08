#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QMediaPlayer>
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QPainter>
#include <QSvgRenderer>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVideoWidget>

#include <iostream>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {

void append16(QByteArray& bytes, quint16 value)
{
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
}

void append32(QByteArray& bytes, quint32 value)
{
    append16(bytes, quint16(value & 0xffff));
    append16(bytes, quint16(value >> 16));
}

bool createSilence(const QString& path, QString& error)
{
    constexpr quint32 sampleRate = 48000;
    constexpr quint32 dataSize = sampleRate * 2; // One second, mono signed 16-bit PCM.
    QByteArray wav("RIFF", 4);
    append32(wav, 36 + dataSize);
    wav.append("WAVEfmt ", 8);
    append32(wav, 16);
    append16(wav, 1); // PCM.
    append16(wav, 1); // One channel.
    append32(wav, sampleRate);
    append32(wav, sampleRate * 2);
    append16(wav, 2);
    append16(wav, 16);
    wav.append("data", 4);
    append32(wav, dataSize);
    wav.append(QByteArray(dataSize, char(0)));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        error = file.errorString();
        return false;
    }
    const qint64 written = file.write(wav);
    if (written != wav.size() || !file.flush()) {
        error = file.errorString();
        return false;
    }
    return true;
}

QJsonArray loadedSdkModules(bool& ffmpegLoaded)
{
    QJsonArray modules;
    ffmpegLoaded = false;
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                                    GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return modules;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            const QString name = QString::fromWCharArray(entry.szModule);
            const QString lower = name.toLower();
            if (lower.contains(QStringLiteral("ffmpegmedia"))) ffmpegLoaded = true;
            if (lower.startsWith(QStringLiteral("qt6")) || lower.contains(QStringLiteral("ffmpeg"))
                || lower.startsWith(QStringLiteral("avcodec")) || lower.startsWith(QStringLiteral("avformat"))
                || lower.startsWith(QStringLiteral("avutil")) || lower.startsWith(QStringLiteral("swresample"))
                || lower.startsWith(QStringLiteral("swscale"))) {
                modules.append(QJsonObject{{"name", name}, {"path", QString::fromWCharArray(entry.szExePath)}});
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
#endif
    return modules;
}

QString mediaStatusName(QMediaPlayer::MediaStatus status)
{
    const char* name = QMetaEnum::fromType<QMediaPlayer::MediaStatus>().valueToKey(int(status));
    return name ? QString::fromLatin1(name) : QString::number(int(status));
}

void printJson(const QJsonObject& report)
{
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    std::cout.write(bytes.constData(), bytes.size());
    std::cout.flush();
}

} // namespace

int main(int argc, char* argv[])
{
    // Process-local selection: do not silently pass via a native fallback backend.
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("OpenOfficeQtSdkCheck"));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windowless Qt SDK load check; no playback or network requests."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("work-dir"), QStringLiteral("Build-local parent for a new fixture directory."),
                      QStringLiteral("path"), QCoreApplication::applicationDirPath() + QStringLiteral("/runtime")});
    parser.addOption({QStringLiteral("timeout-ms"), QStringLiteral("Media-load deadline, 1000 to 15000 ms."),
                      QStringLiteral("milliseconds"), QStringLiteral("10000")});
    parser.process(app);

    QElapsedTimer elapsed;
    elapsed.start();
    QJsonObject report{{"purpose", "Qt SDK runtime verification; not an office application test"},
                       {"checked_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                       {"qt_compile_version", QString::fromLatin1(QT_VERSION_STR)},
                       {"qt_runtime_version", QString::fromLatin1(qVersion())},
                       {"qt_prefix", QLibraryInfo::path(QLibraryInfo::PrefixPath)},
                       {"backend_requested", "ffmpeg"}, {"network_requests_issued", 0},
                       {"audio_output_attached", false}, {"playback_requested", false}};
#ifdef _MSC_VER
    report["compiler"] = QStringLiteral("MSVC");
    report["msvc_version"] = _MSC_VER;
#else
    report["compiler"] = QStringLiteral("non-MSVC");
#endif

    bool timeoutValid = false;
    const int timeoutMs = parser.value(QStringLiteral("timeout-ms")).toInt(&timeoutValid);
    if (!timeoutValid || timeoutMs < 1000 || timeoutMs > 15000) {
        report["status"] = "failed";
        report["reason"] = "timeout-ms must be between 1000 and 15000";
        printJson(report);
        return 1;
    }
    report["timeout_ms"] = timeoutMs;

    QVideoWidget video; // Deliberately never shown.
    QNetworkAccessManager network; // Deliberately no get/post/sendCustomRequest call.
    const QByteArray svg("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
                         "<rect width='16' height='16' fill='#4c6fff'/></svg>");
    QSvgRenderer renderer(svg);
    QImage rendered(16, 16, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    QPainter painter(&rendered);
    renderer.render(&painter);
    painter.end();
    const bool svgValid = renderer.isValid() && rendered.pixelColor(8, 8) == QColor("#4c6fff");
    report["svg_valid_and_rendered"] = svgValid;
    report["video_widget_created"] = true;
    report["video_widget_visible"] = video.isVisible();
    report["network_manager_created"] = true;

    QDir work(parser.value(QStringLiteral("work-dir")));
    const QString folder = QStringLiteral("qt-sdk-silence-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!work.mkpath(QStringLiteral(".")) || !work.mkdir(folder)) {
        report["status"] = "failed";
        report["reason"] = "Cannot create a new build-local fixture directory";
        printJson(report);
        return 1;
    }
    const QString fixture = work.absoluteFilePath(folder + QStringLiteral("/silence-1s.wav"));
    QString fixtureError;
    if (!createSilence(fixture, fixtureError)) {
        report["status"] = "failed";
        report["reason"] = "Cannot write the new silence fixture";
        report["fixture_error"] = fixtureError;
        printJson(report);
        return 1;
    }
    report["fixture_path"] = fixture;
    report["fixture_bytes"] = 96044;

    QMediaPlayer player;
    player.setVideoOutput(&video);
    // No QAudioOutput object exists, and this program never calls play().
    bool loadedSeen = false;
    bool playbackStarted = false;
    bool finished = false;
    QTimer deadline;
    deadline.setSingleShot(true);

    const auto finish = [&](const QString& status, const QString& reason, int exitCode) {
        if (finished) return;
        finished = true;
        deadline.stop();
        bool ffmpegLoaded = false;
        report["loaded_sdk_modules"] = loadedSdkModules(ffmpegLoaded);
        report["ffmpeg_plugin_loaded"] = ffmpegLoaded;
        report["status"] = status;
        report["reason"] = reason;
        report["loaded_media_observed"] = loadedSeen;
        report["media_available"] = player.isAvailable();
        report["duration_ms"] = double(player.duration());
        report["media_status"] = mediaStatusName(player.mediaStatus());
        report["media_error_code"] = int(player.error());
        report["media_error"] = player.errorString();
        report["playback_started"] = playbackStarted;
        report["video_widget_visible"] = video.isVisible();
        report["elapsed_ms"] = double(elapsed.elapsed());
        if (exitCode == 0 && (!ffmpegLoaded || !svgValid || video.isVisible() || playbackStarted
            || QString::fromLatin1(qVersion()) != QString::fromLatin1(QT_VERSION_STR))) {
            report["status"] = "failed";
            report["reason"] = "SDK, FFmpeg plugin, SVG, or no-playback assertion failed";
            exitCode = 1;
        }
        printJson(report);
        QTimer::singleShot(0, &app, [&, exitCode] { app.exit(exitCode); });
    };
    const auto evaluate = [&] {
        if (finished || !loadedSeen || player.duration() <= 0) return;
        const bool valid = player.isAvailable() && player.error() == QMediaPlayer::NoError
            && player.duration() >= 990 && player.duration() <= 1010;
        finish(valid ? QStringLiteral("passed") : QStringLiteral("failed"),
               valid ? QStringLiteral("Local one-second WAV loaded without playback")
                     : QStringLiteral("Unexpected availability, duration, or media error"), valid ? 0 : 1);
    };
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &app, [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia) loadedSeen = true;
        if (status == QMediaPlayer::InvalidMedia) {
            finish(QStringLiteral("failed"), QStringLiteral("Invalid local media"), 1);
            return;
        }
        evaluate();
    });
    QObject::connect(&player, &QMediaPlayer::durationChanged, &app, [&](qint64) { evaluate(); });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &app, [&](QMediaPlayer::Error, const QString&) {
        finish(QStringLiteral("failed"), QStringLiteral("Media backend reported an error"), 1);
    });
    QObject::connect(&player, &QMediaPlayer::playbackStateChanged, &app, [&](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState) {
            playbackStarted = true;
            player.stop();
            finish(QStringLiteral("failed"), QStringLiteral("Unexpected playback state"), 1);
        }
    });
    QObject::connect(&deadline, &QTimer::timeout, &app, [&] {
        finish(QStringLiteral("failed"), QStringLiteral("Timed out waiting for LoadedMedia and duration"), 2);
    });
    deadline.start(timeoutMs);
    if (!player.isAvailable()) {
        finish(QStringLiteral("failed"), QStringLiteral("FFmpeg media player is unavailable"), 1);
    } else {
        player.setSource(QUrl::fromLocalFile(fixture));
    }
    return app.exec();
}
