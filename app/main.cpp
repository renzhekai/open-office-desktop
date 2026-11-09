#include "engine/onlyoffice_engine_adapter.h"
#include "ui/main_window.h"
#include "ui/relocation.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QDebug>
#include <QPixmap>
#include <QStyleFactory>
#include <QTimer>

namespace {
bool writeNewJson(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return false;
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size();
}

bool actionEnabled(QObject& object, const char* name)
{
    const auto* action = object.findChild<QAction*>(QString::fromLatin1(name));
    return action && action->isEnabled();
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Open Office"));
    QApplication::setApplicationVersion(QStringLiteral(OPEN_OFFICE_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Open Office Project"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Open Office development build."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("select"), QStringLiteral("Select a local file for metadata only."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("open"), QStringLiteral("Open a local document with the native engine."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("onlyoffice-runtime"), QStringLiteral("Verified native runtime beside this executable."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("engine-profile"), QStringLiteral("Separate local profile for this development build."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("smoke-report"), QStringLiteral("Write a new JSON launch report; never overwrite."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("snapshot"), QStringLiteral("Write a new PNG of this application's rendered window."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("exit-after-ms"), QStringLiteral("Exit after rendering, for local smoke tests."), QStringLiteral("milliseconds")});
    parser.addOption({QStringLiteral("relocation-log"), QStringLiteral("Report how moving the program went (started by the move helper)."), QStringLiteral("path")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("select")) && parser.isSet(QStringLiteral("open")))
        parser.showHelp(2);

    openoffice::OnlyOfficeEngineAdapter engine;
#ifdef OPEN_OFFICE_WITH_ONLYOFFICE
    const QString runtimeRoot = parser.isSet(QStringLiteral("onlyoffice-runtime"))
        ? parser.value(QStringLiteral("onlyoffice-runtime")) : QCoreApplication::applicationDirPath();
    const QString profileRoot = parser.isSet(QStringLiteral("engine-profile"))
        ? parser.value(QStringLiteral("engine-profile"))
        : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("openoffice-profile"));
    const auto initialization = engine.initializeNative(runtimeRoot, profileRoot, argc, argv);
    if (!initialization.ok) {
        qCritical().noquote() << initialization.code << initialization.message;
        if (parser.isSet(QStringLiteral("smoke-report"))) {
            writeNewJson(parser.value(QStringLiteral("smoke-report")),
                {{QStringLiteral("application"), QApplication::applicationName()},
                 {QStringLiteral("engine_connected"), false},
                 {QStringLiteral("initialization_error"), initialization.code},
                 {QStringLiteral("message"), initialization.message}});
        } else {
            QMessageBox::critical(nullptr, QStringLiteral("Open Office 启动失败"), initialization.message);
        }
        return 4;
    }
#endif
    openoffice::MainWindow window(&engine);
    window.resize(1240, 800);
#ifdef OPEN_OFFICE_WITH_ONLYOFFICE
    window.setAssistantJournalPath(QDir(profileRoot).filePath(QStringLiteral("assistant-commands.jsonl")));
    window.setConversationFolder(QDir(profileRoot).filePath(QStringLiteral("conversations")));
#endif
    if (parser.isSet(QStringLiteral("select")))
        window.selectDocument(parser.value(QStringLiteral("select")));
    if (parser.isSet(QStringLiteral("open")))
        window.selectDocument(parser.value(QStringLiteral("open")));
    window.show();
    if (parser.isSet(QStringLiteral("relocation-log"))) {
        const auto outcome = openoffice::readRelocationLog(parser.value(QStringLiteral("relocation-log")));
        QTimer::singleShot(300, &window, [&window, outcome] {
            const QString text = openoffice::relocationMessage(outcome);
            if (outcome.moved) QMessageBox::information(&window, QStringLiteral("迁移完成"), text);
            else QMessageBox::warning(&window, QStringLiteral("没有迁移"), text);
        });
    }
    if (parser.isSet(QStringLiteral("open"))) {
        QTimer::singleShot(0, &window, [&window] {
            if (auto* action = window.findChild<QAction*>(QStringLiteral("openDocumentAction")))
                action->trigger();
        });
    }

    const bool hasArtifacts = parser.isSet(QStringLiteral("smoke-report")) || parser.isSet(QStringLiteral("snapshot"));
    bool artifactFailure = false;
    if (hasArtifacts) {
        QTimer::singleShot(450, &window, [&]() {
            bool artifactsOk = true;
            bool screenshotSaved = false;
            if (parser.isSet(QStringLiteral("snapshot"))) {
                QFile imageFile(parser.value(QStringLiteral("snapshot")));
                screenshotSaved = imageFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                    && window.grab().save(&imageFile, "PNG");
                artifactsOk = screenshotSaved;
            }
            if (parser.isSet(QStringLiteral("smoke-report"))) {
                const auto capabilities = engine.capabilities();
                const QJsonObject report{
                    {QStringLiteral("application"), QApplication::applicationName()},
                    {QStringLiteral("version"), QApplication::applicationVersion()},
                    {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
                    {QStringLiteral("platform_plugin"), QApplication::platformName()},
                    {QStringLiteral("prototype"), true},
                    {QStringLiteral("window_rendered"), window.isVisible()},
                    {QStringLiteral("engine_connected"), capabilities.available},
                    {QStringLiteral("office_open_edit_save"), capabilities.available
                        ? QStringLiteral("engine_initialized_roundtrip_not_verified_by_smoke")
                        : QStringLiteral("not_connected")},
                    {QStringLiteral("engine_save_enabled"), actionEnabled(window, "saveDocumentAction")},
                    {QStringLiteral("select_file_enabled"), actionEnabled(window, "selectFileAction")},
                    {QStringLiteral("snapshot_saved"), screenshotSaved},
                    {QStringLiteral("snapshot_kind"), QStringLiteral("application QWidget render; native CEF child content requires desktop capture")},
                    {QStringLiteral("width"), window.width()},
                    {QStringLiteral("height"), window.height()}
                };
                artifactsOk = writeNewJson(parser.value(QStringLiteral("smoke-report")), report) && artifactsOk;
            }
            if (!artifactsOk) {
                artifactFailure = true;
                qWarning() << "Could not write the requested new smoke artifacts.";
                window.close();
            }
        });
    }
    if (parser.isSet(QStringLiteral("exit-after-ms"))) {
        bool ok = false;
        const int delay = parser.value(QStringLiteral("exit-after-ms")).toInt(&ok);
        if (!ok || delay < 600 || delay > 60000)
            return 2;
        // Closing the real host must use the same save/CEF handshake as its UI.
        QTimer::singleShot(delay, &window, &QWidget::close);
    }
    bool nativeLoopUsed = false;
    int result = 0;
#ifdef OPEN_OFFICE_WITH_ONLYOFFICE
    result = engine.runNativeEventLoop(nativeLoopUsed);
#endif
    if (!nativeLoopUsed)
        result = app.exec();
#ifdef OPEN_OFFICE_WITH_ONLYOFFICE
    engine.shutdownNative();
#endif
    // 迁移: the program folder can only move once nothing runs from it; the helper waits for that.
    const openoffice::RelocationPlan relocation = window.relocationPlan();
    if (relocation.valid()) {
        QString error;
        if (!openoffice::startRelocation(relocation, &error))
            QMessageBox::warning(nullptr, QStringLiteral("没有迁移"),
                                 QStringLiteral("没能启动迁移程序（%1）。Open Office 还在原来的位置。").arg(error));
    }
    return result == 0 && artifactFailure ? 3 : result;
}
