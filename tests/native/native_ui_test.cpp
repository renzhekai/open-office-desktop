#include "engine/onlyoffice_engine_adapter.h"
#include "fake_ollama.h"
#include "ui/assistant_art.h"
#include "ui/ai_settings_dialog.h"
#include "ui/assistant_ball.h"
#include "ui/main_window.h"
#include "ui/mcp_server.h"
#include "ui/relocation.h"
#include "ui/style_profile_dialog.h"

#include "assistant/model_catalog.h"
#include "assistant/style_profile.h"

#include <QAction>
#include <QAbstractButton>
#include <QMenu>
#include <QTemporaryFile>
#include <QCloseEvent>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QStorageInfo>
#include <QUuid>
#include <QStackedWidget>
#include <QTimer>
#include <QtTest>

// Test-only event driver. It exercises UI lifecycle rules, never opens an
// Office document, and is not built into the application or engine adapter.
class ControlledEngine final : public openoffice::OfficeEngine {
public:
    openoffice::EngineCapabilities capabilities() const noexcept override { return currentCapabilities; }
    bool documentModified() const noexcept override { return modified; }
    bool needsCloseHandshake() const noexcept override { return closeHandshake; }
    bool canCreate(openoffice::DocumentKind kind) const noexcept override
    {
        return textOnlyCreation ? currentCapabilities.create && kind == openoffice::DocumentKind::Text
                                : OfficeEngine::canCreate(kind);
    }
    openoffice::EngineResult requestOpen(quint64 id, const QString& path, bool discard) override
    {
        ++openCalls;
        viewExistedBeforeOpen = viewCreated;
        lastId = id;
        lastPath = path;
        lastReplaceDiscard = discard;
        return {acceptRequests, QStringLiteral("test"), QStringLiteral("test request")};
    }
    openoffice::EngineResult requestSave(quint64 id, const QString& path) override
    {
        ++saveCalls;
        lastId = id;
        lastPath = path;
        return {acceptRequests, QStringLiteral("test"), QStringLiteral("test request")};
    }
    openoffice::EngineResult requestCreate(quint64 id, openoffice::DocumentKind kind, const QString& title,
                                           bool discard) override
    {
        ++createCalls;
        lastId = id;
        lastKind = kind;
        lastTitle = title;
        lastReplaceDiscard = discard;
        return {acceptRequests, QStringLiteral("test"), QStringLiteral("test request")};
    }
    void cancelSaveDestination() override { ++cancelDestinationCalls; }
    void setEditorAppearance(bool dark) override { appearance << dark; }
    bool supportsEditorCommand(const QString& name) const noexcept override { return editorCommands.contains(name); }
    openoffice::EngineResult requestEditorCommand(quint64 id, const QString& name, const QJsonObject& arguments) override
    {
        if (answerReads && (name == QStringLiteral("document.getText") || name == QStringLiteral("selection.getText"))) {
            // Reads the window makes for the AI: answered like the editor would, a moment later.
            ++readCalls;
            const QString text = name == QStringLiteral("document.getText") ? documentText : selectionText;
            QTimer::singleShot(0, this, [this, id, text] {
                emit editorCommandFinished(id, {true, QStringLiteral("read"), QStringLiteral("read")},
                                           {{QStringLiteral("text"), text}, {QStringLiteral("length"), text.size()}});
            });
            return {true, QStringLiteral("test"), QStringLiteral("test read")};
        }
        if (answerReads && (name == QStringLiteral("document.outline") || name == QStringLiteral("table.read"))) {
            // The structure reads too: by default a document without tables.
            ++readCalls;
            const QJsonObject value = name == QStringLiteral("document.outline") ? outlineValue : tableValue;
            QTimer::singleShot(0, this, [this, id, name, value] {
                emit editorCommandFinished(id, {true, name == QStringLiteral("table.read") ? QStringLiteral("table") : QStringLiteral("outline"),
                                                QStringLiteral("read")}, value);
            });
            lastReadArguments = arguments;
            return {true, QStringLiteral("test"), QStringLiteral("test read")};
        }
        ++editorCommandCalls;
        lastCommandId = id;
        lastCommandName = name;
        lastCommandArguments = arguments;
        return {acceptRequests, QStringLiteral("test"), QStringLiteral("test command request")};
    }
    void completeCommand(quint64 id, bool ok, const QString& message, const QJsonObject& value = {})
    {
        emit editorCommandFinished(id, {ok, ok ? QStringLiteral("done") : QStringLiteral("failed"), message}, value);
    }
    openoffice::EngineResult requestClose(quint64 id, bool discardChanges) override
    {
        ++closeCalls;
        lastId = id;
        lastDiscardChanges = discardChanges;
        return {acceptRequests, QStringLiteral("test"), QStringLiteral("test close request")};
    }
    QWidget* createEditorWidget(QWidget* parent) override
    {
        viewCreated = true;
        view = new QWidget(parent);
        return view;
    }
    void complete(quint64 id, openoffice::EngineOperation operation, bool ok, const QString& path,
                  openoffice::DocumentKind kind = openoffice::DocumentKind::Text)
    {
        emit operationFinished(id, operation, {ok, QStringLiteral("test"), ok ? QStringLiteral("test complete") : QStringLiteral("test failed")},
                               path, kind);
    }
    void changeModified(bool value) { modified = value; emit modifiedChanged(value); }
    void changeCapabilities(openoffice::EngineCapabilities value) { currentCapabilities = value; emit capabilitiesChanged(); }
    void askDestination(const QString& suggested) { emit saveDestinationRequested(suggested, openoffice::DocumentKind::Text); }
    openoffice::EngineCapabilities currentCapabilities{true, true, true, true, true, true};
    bool textOnlyCreation = false;
    QStringList editorCommands{QStringLiteral("text.insert"), QStringLiteral("selection.getText"),
                               QStringLiteral("selection.replace"), QStringLiteral("document.getText"),
                               QStringLiteral("document.replaceText"), QStringLiteral("edit.undo"),
                               QStringLiteral("edit.redo"), QStringLiteral("document.outline"),
                               QStringLiteral("table.read"), QStringLiteral("table.setCell"),
                               QStringLiteral("format.cells"), QStringLiteral("format.paragraphs"),
                               QStringLiteral("format.text"), QStringLiteral("selection.select")};
    int editorCommandCalls = 0;
    bool answerReads = false; // answer document/selection reads automatically, outside editorCommandCalls
    QJsonObject outlineValue{{QStringLiteral("paragraphs"), 1}, {QStringLiteral("tables"), 0}, {QStringLiteral("blocks"), QJsonArray()}};
    QJsonObject tableValue;
    QJsonObject lastReadArguments;
    QString documentText;
    QString selectionText;
    int readCalls = 0;
    QList<bool> appearance;
    quint64 lastCommandId = 0;
    QString lastCommandName;
    QJsonObject lastCommandArguments;
    bool lastReplaceDiscard = false;
    int createCalls = 0;
    int cancelDestinationCalls = 0;
    openoffice::DocumentKind lastKind = openoffice::DocumentKind::Unknown;
    QString lastTitle;
    bool acceptRequests = true;
    bool modified = false;
    bool closeHandshake = false;
    bool lastDiscardChanges = false;
    bool viewCreated = false;
    bool viewExistedBeforeOpen = false;
    int openCalls = 0;
    int saveCalls = 0;
    int closeCalls = 0;
    quint64 lastId = 0;
    QString lastPath;
    QPointer<QWidget> view;
};

class NativeUiTest : public QObject
{
    Q_OBJECT
private:
    static QAction* action(openoffice::MainWindow& window, const char* name)
    {
        return window.findChild<QAction*>(QString::fromLatin1(name));
    }

    static void openFixture(openoffice::MainWindow& window, ControlledEngine& engine, const char* file)
    {
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/") + QString::fromLatin1(file));
        action(window, "openDocumentAction")->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
    }

    static void answerRelocation(const QString& buttonText)
    {
        QTimer::singleShot(0, [buttonText] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) return;
            for (QAbstractButton* button : box->buttons())
                if (button->text() == buttonText) button->click();
        });
    }

    static QString powershell(const QString& script)
    {
        QProcess process;
        process.start(QStringLiteral("powershell.exe"),
                      {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
                       QStringLiteral("[Console]::OutputEncoding = [Text.Encoding]::UTF8; ") + script});
        process.waitForFinished(60000);
        return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    }

    static QString psQuoted(const QString& text)
    {
        QString escaped = QDir::toNativeSeparators(text);
        escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
        return QLatin1Char('\'') + escaped + QLatin1Char('\'');
    }

    // Moves a small fake program folder from a temporary folder into `destinationRoot`
    // with the real helper, and checks the folder, a shortcut and an uninstall entry.
    static void runRelocationHelper(const QString& destinationRoot)
    {
        QTemporaryDir root;
        QTemporaryDir destinationParent(QDir(destinationRoot).filePath(QStringLiteral("relocation-test-XXXXXX")));
        QVERIFY(root.isValid() && destinationParent.isValid());
        const QString source = root.filePath(QStringLiteral("Open Office 程序"));
        QVERIFY(QDir().mkpath(source + QStringLiteral("/models")));
        QVERIFY(QDir().mkpath(source + QStringLiteral("/empty-folder")));
        const auto write = [](const QString& path, const QByteArray& bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write(source + QStringLiteral("/OpenOffice.exe"), QByteArray(4096, 'x')));
        QVERIFY(write(source + QStringLiteral("/models/Qwen3-1.7B-Q8_0.gguf"), QByteArray(1 << 20, 'm')));
        QVERIFY(write(source + QStringLiteral("/unins000.exe"), "u"));
        const QString links = root.filePath(QStringLiteral("links"));
        QVERIFY(QDir().mkpath(links + QStringLiteral("/Open Office")));
        const QString link = links + QStringLiteral("/Open Office/Open Office.lnk");
        const QString key = QStringLiteral("Software\\Open Office Project Tests\\Uninstall-%1")
                                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString app = QDir::toNativeSeparators(source);
        powershell(QStringLiteral("$s = (New-Object -ComObject WScript.Shell).CreateShortcut(%1); $s.TargetPath = %2; "
                                  "$s.WorkingDirectory = %3; $s.IconLocation = %4; $s.Save()")
                       .arg(psQuoted(link), psQuoted(app + QStringLiteral("\\OpenOffice.exe")), psQuoted(app),
                            psQuoted(app + QStringLiteral("\\OpenOffice.exe,0"))));
        QVERIFY(QFileInfo::exists(link));
        {
            QSettings entry(QStringLiteral("HKEY_CURRENT_USER\\") + key + QStringLiteral("\\App_is1"), QSettings::NativeFormat);
            entry.setValue(QStringLiteral("Inno Setup: App Path"), app);
            entry.setValue(QStringLiteral("InstallLocation"), app + QLatin1Char('\\'));
            entry.setValue(QStringLiteral("UninstallString"), QLatin1Char('"') + app + QStringLiteral("\\unins000.exe\""));
            entry.setValue(QStringLiteral("DisplayIcon"), app + QStringLiteral("\\OpenOffice.exe"));
            QSettings other(QStringLiteral("HKEY_CURRENT_USER\\") + key + QStringLiteral("\\Other"), QSettings::NativeFormat);
            other.setValue(QStringLiteral("InstallLocation"), QStringLiteral("C:\\Somewhere\\Else"));
        }

        QString error;
        const openoffice::RelocationPlan plan = openoffice::planRelocation(source, destinationParent.path(), &error);
        QVERIFY2(plan.valid(), qPrintable(error));
        QProcess finished; // the "program" has already exited
        finished.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QStringLiteral("exit")});
        QVERIFY(finished.waitForStarted());
        const qint64 pid = finished.processId();
        QVERIFY(finished.waitForFinished());
        openoffice::RelocationOptions options;
        options.shortcutFolders = {links};
        options.uninstallKey = QStringLiteral("HKCU:\\") + key;
        options.launch = false;
        const QString log = root.filePath(QStringLiteral("relocation.log"));
        QStringList command = openoffice::relocationCommand(plan, pid, log, options);
        QProcess helper;
        helper.start(command.takeFirst(), command);
        QVERIFY(helper.waitForFinished(120000));
        QCOMPARE(helper.exitCode(), 0);

        const openoffice::RelocationOutcome outcome = openoffice::readRelocationLog(log);
        QFile logFile(log);
        QVERIFY(logFile.open(QIODevice::ReadOnly));
        const QByteArray logText = logFile.readAll();
        QVERIFY2(outcome.moved && outcome.code == QStringLiteral("moved"), logText.constData());
        QCOMPARE(QDir::fromNativeSeparators(outcome.target), plan.target);
        QVERIFY(openoffice::relocationMessage(outcome).contains(QDir::toNativeSeparators(plan.target)));
        QVERIFY(!QFileInfo::exists(source));
        QCOMPARE(QFileInfo(plan.target + QStringLiteral("/models/Qwen3-1.7B-Q8_0.gguf")).size(), qint64(1 << 20));
        QCOMPARE(QFileInfo(plan.target + QStringLiteral("/OpenOffice.exe")).size(), qint64(4096));
        QVERIFY(QFileInfo(plan.target + QStringLiteral("/empty-folder")).isDir());
        const QString moved = QDir::toNativeSeparators(plan.target);
        QCOMPARE(powershell(QStringLiteral("$s = (New-Object -ComObject WScript.Shell).CreateShortcut(%1); "
                                           "$s.TargetPath + '|' + $s.WorkingDirectory + '|' + $s.IconLocation").arg(psQuoted(link))),
                 moved + QStringLiteral("\\OpenOffice.exe|") + moved + QLatin1Char('|') + moved + QStringLiteral("\\OpenOffice.exe,0"));
        {
            QSettings entry(QStringLiteral("HKEY_CURRENT_USER\\") + key + QStringLiteral("\\App_is1"), QSettings::NativeFormat);
            QCOMPARE(entry.value(QStringLiteral("Inno Setup: App Path")).toString(), moved);
            QCOMPARE(entry.value(QStringLiteral("InstallLocation")).toString(), moved + QLatin1Char('\\'));
            QCOMPARE(entry.value(QStringLiteral("UninstallString")).toString(),
                     QLatin1Char('"') + moved + QStringLiteral("\\unins000.exe\""));
            QCOMPARE(entry.value(QStringLiteral("DisplayIcon")).toString(), moved + QStringLiteral("\\OpenOffice.exe"));
            QSettings other(QStringLiteral("HKEY_CURRENT_USER\\") + key + QStringLiteral("\\Other"), QSettings::NativeFormat);
            QCOMPARE(other.value(QStringLiteral("InstallLocation")).toString(), QStringLiteral("C:\\Somewhere\\Else"));
        }
        QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Open Office Project Tests"), QSettings::NativeFormat).remove(QString());
    }

    static void answerClosePrompt(QMessageBox::StandardButton choice)
    {
        QTimer::singleShot(0, [choice] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (dialog && dialog->button(choice)) dialog->button(choice)->click();
        });
    }

private slots:
    void initTestCase()
    {
        // Deterministic panel: no spring or fade animations under test.
        qputenv("OPEN_OFFICE_REDUCED_MOTION", "1");
        // No real local AI under test: nothing listens at this address. The
        // assistant tests below drive the Ollama path; one test switches to the built-in engine.
        qputenv("OPEN_OFFICE_OLLAMA_URL", closedOllamaUrl());
        qputenv("OPEN_OFFICE_AI_BACKEND", "ollama");
        // Settings go to a file of the tests' own, never the person's.
        QVERIFY(m_settings.isValid());
        qputenv("OPEN_OFFICE_SETTINGS_FILE", QFile::encodeName(m_settings.filePath(QStringLiteral("settings.ini"))));
    }

    void theModelListSwitchesAndAsksOnceBeforeCloud()
    {
        openoffice::aiSettings().clear();
        QTemporaryDir folder;
        QFile model(folder.filePath(QStringLiteral("Qwen3-1.7B-Q8_0.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.close();
        ScopedEnvironment backend("OPEN_OFFICE_AI_BACKEND", QByteArray()); // the model list decides
        ScopedEnvironment engineUrl("OPEN_OFFICE_AI_ENGINE_URL", closedOllamaUrl()); // an engine is "there", never started
        ScopedEnvironment directory("OPEN_OFFICE_MODEL_DIR", QFile::encodeName(folder.path()));
        FakeOllama cloud; // an OpenAI-compatible service
        cloud.chats = {FakeOllama::sse({QStringLiteral("云端的回答")})};
        QString error;
        QVERIFY(openoffice::saveCloudProvider({QStringLiteral("fake"), QStringLiteral("Fake Cloud"), QString::fromLatin1(cloud.url() + "/v1"),
                                               {QStringLiteral("fake-model")}}, QStringLiteral("test-key"), &error));
        ControlledEngine engine;
        engine.answerReads = true;
        engine.documentText = QStringLiteral("正文。");
        engine.outlineValue = {{QStringLiteral("tables"), 0}, {QStringLiteral("paragraphs"), 1}};
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        const auto button = [panel](const QString& text) -> QPushButton* {
            QPushButton* found = nullptr;
            for (QPushButton* candidate : panel->findChildren<QPushButton*>())
                if (candidate->text() == text && candidate->isEnabled()) found = candidate;
            return found;
        };
        panel->expand();
        QTRY_VERIFY(panel->modelButtonText().contains(QStringLiteral("1.7B")));
        // The list: grouped, the model in use ticked, a way to manage models.
        QWidget* menu = panel->showModelMenu();
        QVERIFY(menu && menu->isVisible());
        const QStringList lines = panel->modelMenuLines();
        QVERIFY2(lines.contains(QStringLiteral("本机模型")) && lines.contains(QStringLiteral("云端模型")), qPrintable(lines.join(QLatin1Char('|'))));
        // In the ball's own style, not the system menu: no QMenu, rounded card, the current model marked.
        QVERIFY(!qobject_cast<QMenu*>(menu));
        QVERIFY(menu->findChild<QFrame*>(QStringLiteral("modelCard")));
        QVERIFY(std::any_of(lines.begin(), lines.end(), [](const QString& line) { return line.startsWith(QStringLiteral("Qwen3 1.7B")) && line.endsWith(QStringLiteral(" ✓")); }));
        QVERIFY(std::any_of(lines.begin(), lines.end(), [](const QString& line) { return line.startsWith(QStringLiteral("fake-model")); }));
        QVERIFY(std::any_of(lines.begin(), lines.end(), [](const QString& line) { return line.startsWith(QStringLiteral("管理模型")); }));
        QVERIFY(!lines.contains(QStringLiteral("Ollama 模型"))); // the Ollama option is off
        menu->close();

        // A cloud service is asked about once, saying what goes where.
        panel->chooseModel(QStringLiteral("cloud:fake/fake-model"));
        QTRY_VERIFY(panel->lastMessageText().contains(QStringLiteral("Fake Cloud")) && panel->lastMessageText().contains(QStringLiteral("发给")));
        QVERIFY(!window.localAssistant()->cloud()); // nothing switched before the person agrees
        button(QStringLiteral("切换"))->click();
        QTRY_VERIFY(window.localAssistant()->cloud());
        QTRY_VERIFY(panel->modelButtonText().contains(QStringLiteral("fake-model")));
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("云端")));
        // It answers from the service.
        QTRY_VERIFY(!panel->working());
        panel->submitText(QStringLiteral("你好"));
        QTRY_VERIFY(panel->lastMessageText().contains(QStringLiteral("云端的回答")));
        QCOMPARE(cloud.requestHeaders.last().value("authorization"), QByteArray("Bearer test-key"));
        QTRY_VERIFY(!panel->working());
        // Back to the local model, and to the cloud again: no second question.
        panel->chooseModel(QStringLiteral("builtin:qwen3-1.7b"));
        QTRY_VERIFY(window.localAssistant()->builtin());
        panel->chooseModel(QStringLiteral("cloud:fake/fake-model"));
        QTRY_VERIFY(window.localAssistant()->cloud());
        openoffice::aiSettings().clear();
    }

    void cloudServicesAreAddedInTheSettings()
    {
        openoffice::aiSettings().clear();
        FakeOllama cloud;
        cloud.replies.insert(QStringLiteral("/v1/models"), R"({"data":[{"id":"fake-model"},{"id":"fake-large"}]})");
        openoffice::CloudProviderDialog dialog({}, nullptr);
        dialog.fill(QStringLiteral("Fake Cloud"), QStringLiteral("http://api.example.com/v1"), QStringLiteral("k"), QStringLiteral("m"));
        QVERIFY(!dialog.save());
        QVERIFY(dialog.statusText().contains(QStringLiteral("https")));
        dialog.fill(QStringLiteral("Fake Cloud"), QString::fromLatin1(cloud.url() + "/v1"), QStringLiteral("test-key"), QString());
        QVERIFY(!dialog.save());
        QVERIFY(dialog.statusText().contains(QStringLiteral("模型")));
        dialog.fetchModels();
        QTRY_VERIFY(dialog.modelsText().contains(QStringLiteral("fake-large")));
        QCOMPARE(cloud.requests.last().first, QStringLiteral("/v1/models"));
        QCOMPARE(cloud.requestHeaders.last().value("authorization"), QByteArray("Bearer test-key"));
        QVERIFY(dialog.save());
        QCOMPARE(openoffice::cloudProviders().size(), 1);
        QCOMPARE(openoffice::cloudProviders().first().models.size(), 2);
        QCOMPARE(openoffice::cloudApiKey(dialog.provider().id), QStringLiteral("test-key"));
        // The settings page lists it, never the key itself.
        ScopedEnvironment backend("OPEN_OFFICE_AI_BACKEND", "builtin");
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        auto* settings = window.openAiSettings();
        const QStringList rows = settings->cloudRowTexts();
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().contains(QStringLiteral("Fake Cloud")) && rows.first().contains(QStringLiteral("已加密保存")));
        QVERIFY(!rows.first().contains(QStringLiteral("test-key")));
        settings->close();
        openoffice::aiSettings().clear();
    }

    void badCommandsNeverReachThePersonAndChangesAreDescribed()
    {
        // The user's screenshot: "设置单元格格式" with no format, which failed only after "执行".
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools")})});
        fake.chats = {FakeOllama::chat({}, QStringLiteral("format_cells"), {{QStringLiteral("table"), 1}}),
                      FakeOllama::chat({}, QStringLiteral("format_paragraphs"),
                                       {{QStringLiteral("paragraphs"), QStringLiteral("1")}, {QStringLiteral("bold"), true}}),
                      FakeOllama::chat({QStringLiteral("好的，不改了。")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        ControlledEngine engine;
        engine.answerReads = true;
        engine.documentText = QStringLiteral("标题\n正文。");
        engine.outlineValue = {{QStringLiteral("tables"), 0}, {QStringLiteral("paragraphs"), 2}};
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        const auto buttons = [panel](const QString& text) {
            int count = 0;
            for (QPushButton* candidate : panel->findChildren<QPushButton*>())
                if (candidate->text() == text && candidate->isVisible()) ++count;
            return count;
        };
        panel->expand();
        panel->submitText(QStringLiteral("处理一下这个文档"));
        // The bad command went back to the model; the person sees only the good one, described.
        QTRY_VERIFY(panel->lastMessageText().contains(QStringLiteral("第 1 段：加粗")));
        QCOMPARE(buttons(QStringLiteral("执行")), 1);
        QVERIFY(QString::fromUtf8(QJsonDocument(fake.chatRequests().at(1)).toJson()).contains(QStringLiteral("没有说要设置哪种格式")));
        QCOMPARE(engine.editorCommandCalls, 0);
    }

    void vagueStyleRequestsGetDefaultsWithoutAModel()
    {
        // No local AI at all (nothing listens): the program's own style still works.
        ControlledEngine engine;
        engine.answerReads = true;
        const auto block = [](int number, const QString& text) {
            return QJsonObject{{QStringLiteral("id"), QStringLiteral("P%1").arg(number)}, {QStringLiteral("text"), text}};
        };
        engine.outlineValue = {{QStringLiteral("tables"), 0}, {QStringLiteral("paragraphs"), 4},
                               {QStringLiteral("blocks"), QJsonArray{block(1, QStringLiteral("周例会安排")), block(2, QStringLiteral("一、时间")),
                                                                     block(3, QStringLiteral("每周一上午九点。")), block(4, QStringLiteral("行政部"))}}};
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        panel->expand();
        panel->submitText(QStringLiteral("帮我优化一下样式"));
        QTRY_COMPARE(panel->correctionCount(), 3); // title, subheading, closing line
        QVERIFY(window.localAssistant()->state() != openoffice::LocalAiState::Thinking); // no model was asked
    }

    void unavailableEngineDisablesOfficeActions()
    {
        openoffice::OnlyOfficeEngineAdapter engine;
        openoffice::MainWindow window(&engine);
        for (const char* name : {"openDocumentAction", "saveDocumentAction", "saveAsDocumentAction", "createTextAction",
                                 "createSpreadsheetAction", "createPresentationAction", "editorPageAction"}) {
            const auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
            QVERIFY2(action, name);
            QVERIFY2(!action->isEnabled(), name);
        }
        const auto* choose = window.findChild<QAction*>(QStringLiteral("selectFileAction"));
        QVERIFY(choose && choose->isEnabled());
        QCOMPARE(window.findChild<QLabel*>(QStringLiteral("statusPrototypeLabel"))->text(), QStringLiteral("开发原型"));
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("attributionStatusLabel"))->text().contains(QStringLiteral("未包含上游引擎")));
    }

    void availableEngineShowsConnectedAttribution()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        const auto* attribution = window.findChild<QLabel*>(QStringLiteral("attributionStatusLabel"));
        QCOMPARE(window.findChild<QLabel*>(QStringLiteral("statusPrototypeLabel"))->text(), QStringLiteral("开发版"));
        QVERIFY(attribution->text().contains(QStringLiteral("Ascensio System SIA")));
        QVERIFY(!attribution->text().contains(QStringLiteral("未包含")));
        engine.changeCapabilities({});
        QTRY_VERIFY(attribution->text().contains(QStringLiteral("未包含上游引擎")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("selectFileAction"))->isEnabled());
    }

    void engineCanLockFurtherOpensWithoutLosingCurrentDocument()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
        const QString selected = window.selectedDocumentPath();
        engine.changeCapabilities({true, false, true, true, false});
        QTRY_VERIFY(!window.findChild<QAction*>(QStringLiteral("selectFileAction"))->isEnabled());
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->isEnabled());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/values-formula.xlsx"));
        QCOMPARE(window.selectedDocumentPath(), selected);
        QCOMPARE(engine.openCalls, 1);
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"))->text().contains(QStringLiteral("新实例")));
        engine.changeCapabilities({true, true, true, true, true});
        QTRY_VERIFY(window.findChild<QAction*>(QStringLiteral("selectFileAction"))->isEnabled());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->isEnabled());
    }

    void selectFixtureShowsMetadataWithoutEnablingSave()
    {
        openoffice::OnlyOfficeEngineAdapter engine;
        openoffice::MainWindow window(&engine);
        const QString file = QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx");
        QVERIFY(QFileInfo::exists(file));
        window.selectDocument(file);
        const auto* label = window.findChild<QLabel*>(QStringLiteral("selectedFileNameLabel"));
        QVERIFY(label);
        QVERIFY(label->text().contains(QStringLiteral("text-table.docx")));
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
    }

    void themeSwitchFollowsIntoTheEditor()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        engine.appearance.clear();
        action(window, "themeAction")->trigger();
        QCOMPARE(engine.appearance, QList<bool>{true});
        action(window, "themeAction")->trigger();
        QCOMPARE(engine.appearance, (QList<bool>{true, false}));
    }

    void themeSwitchDoesNotGrantEditorCapabilities()
    {
        openoffice::OnlyOfficeEngineAdapter engine;
        openoffice::MainWindow window(&engine);
        auto* theme = window.findChild<QAction*>(QStringLiteral("themeAction"));
        QVERIFY(theme);
        const auto before = window.styleSheet();
        theme->trigger();
        QVERIFY(window.styleSheet() != before);
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
        QVERIFY(!engine.capabilities().available);
    }

    void acceptedOpenWaitsForMatchingCompletionAndProtectsPendingClose()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        QVERIFY(engine.viewExistedBeforeOpen);
        auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("workspaceStack"));
        QVERIFY(stack);
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("editorHostPage"));
        QVERIFY(!window.documentOpened());
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
        QCloseEvent closeEvent;
        QCoreApplication::sendEvent(&window, &closeEvent);
        QVERIFY(!closeEvent.isAccepted());
        engine.complete(engine.lastId + 1, openoffice::EngineOperation::Open, true, engine.lastPath);
        QCoreApplication::processEvents();
        QVERIFY(!window.documentOpened());
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
    }

    void failedSaveKeepsUnsavedChangesAndRejectsLateSuccess()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
        engine.changeModified(true);
        auto* title = window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"));
        QTRY_VERIFY(title->text().contains(QStringLiteral("未保存")));
        window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->trigger();
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
        const auto saveId = engine.lastId;
        engine.complete(saveId, openoffice::EngineOperation::Save, false, QString());
        auto* feedback = window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"));
        QTRY_COMPARE(feedback->text(), QStringLiteral("test failed"));
        QVERIFY(title->text().contains(QStringLiteral("未保存")));
        engine.complete(saveId, openoffice::EngineOperation::Save, true, QStringLiteral("wrong.docx"));
        QCoreApplication::processEvents();
        QVERIFY(title->text().contains(QStringLiteral("text-table.docx")));
        QCOMPARE(feedback->text(), QStringLiteral("test failed"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        QCOMPARE(engine.openCalls, 1);
    }

    void saveSuccessRequiresCompletionAndKeepsLaterEditsDirty()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
        engine.changeModified(true);
        window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->trigger();
        auto* feedback = window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"));
        QVERIFY(feedback->text().contains(QStringLiteral("等待编辑引擎确认")));
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, true, engine.lastPath);
        QTRY_VERIFY(feedback->text().contains(QStringLiteral("后续修改尚未保存")));
        engine.changeModified(false);
        window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, true, engine.lastPath);
        QTRY_COMPARE(feedback->text(), QStringLiteral("编辑引擎已确认文档保存完成。"));
    }

    void rejectedRequestDoesNotOpenOrLockTheWindow()
    {
        ControlledEngine engine;
        engine.acceptRequests = false;
        openoffice::MainWindow window(&engine);
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        QVERIFY(!window.documentOpened());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->isEnabled());
        QCloseEvent closeEvent;
        QCoreApplication::sendEvent(&window, &closeEvent);
        QVERIFY(closeEvent.isAccepted());
    }

    void nativeCloseWaitsForMatchingAcknowledgementAndKeepsEditorAlive()
    {
        ControlledEngine engine;
        engine.closeHandshake = true;
        openoffice::MainWindow window(&engine);
        window.show();
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());

        QVERIFY(!window.close());
        QCOMPARE(engine.closeCalls, 1);
        QVERIFY(!engine.lastDiscardChanges);
        QVERIFY(window.isVisible());
        QVERIFY(!engine.view.isNull());
        QCOMPARE(window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"))->text(),
                 QStringLiteral("正在关闭文档…"));
        QVERIFY(!window.close());
        QCOMPARE(engine.closeCalls, 1);

        engine.complete(engine.lastId + 1, openoffice::EngineOperation::Close, true, QString(),
                        openoffice::DocumentKind::Unknown);
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY(!engine.view.isNull());
        engine.complete(engine.lastId, openoffice::EngineOperation::Close, true, QString(),
                        openoffice::DocumentKind::Unknown);
        QTRY_VERIFY(!window.isVisible());
        QCOMPARE(engine.closeCalls, 1);
    }

    void failedDiscardClosePreservesWindowAndUnsavedChanges()
    {
        ControlledEngine engine;
        engine.closeHandshake = true;
        openoffice::MainWindow window(&engine);
        window.show();
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
        engine.changeModified(true);
        auto* title = window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"));
        QTRY_VERIFY(title->text().contains(QStringLiteral("未保存")));

        answerClosePrompt(QMessageBox::Discard);
        QVERIFY(!window.close());
        QCOMPARE(engine.closeCalls, 1);
        QVERIFY(engine.lastDiscardChanges);
        QVERIFY(window.isVisible());
        engine.complete(engine.lastId, openoffice::EngineOperation::Close, false, QString(),
                        openoffice::DocumentKind::Unknown);
        QTRY_COMPARE(window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"))->text(),
                     QStringLiteral("test failed"));
        QVERIFY(window.isVisible());
        QVERIFY(!engine.view.isNull());
        QVERIFY(window.documentOpened());
        QVERIFY(title->text().contains(QStringLiteral("未保存")));
        QVERIFY(engine.documentModified());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());

        answerClosePrompt(QMessageBox::Cancel);
        QVERIFY(!window.close());
        QCOMPARE(engine.closeCalls, 1);
        QVERIFY(window.isVisible());
    }

    void saveBeforeCloseWaitsForSaveThenNativeClose()
    {
        ControlledEngine engine;
        engine.closeHandshake = true;
        openoffice::MainWindow window(&engine);
        window.show();
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
        engine.changeModified(true);
        QTRY_VERIFY(window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"))->text()
                    .contains(QStringLiteral("未保存")));

        answerClosePrompt(QMessageBox::Save);
        QVERIFY(!window.close());
        QCOMPARE(engine.saveCalls, 1);
        QCOMPARE(engine.closeCalls, 0);
        QVERIFY(window.isVisible());
        QVERIFY(!engine.view.isNull());
        engine.changeModified(false);
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, true, engine.lastPath);
        QTRY_COMPARE(engine.closeCalls, 1);
        QVERIFY(!engine.lastDiscardChanges);
        QVERIFY(window.isVisible());
        QVERIFY(!engine.view.isNull());
        QVERIFY(window.documentOpened());
        engine.complete(engine.lastId, openoffice::EngineOperation::Close, true, QString(),
                        openoffice::DocumentKind::Unknown);
        QTRY_VERIFY(!window.isVisible());
        QCOMPARE(engine.closeCalls, 1);
    }

    void delayedCleanNotificationClosesOnceAndCloseFailureRestoresEditor()
    {
        ControlledEngine engine;
        engine.closeHandshake = true;
        openoffice::MainWindow window(&engine);
        window.show();
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
        engine.changeModified(true);
        auto* title = window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"));
        QTRY_VERIFY(title->text().contains(QStringLiteral("未保存")));

        answerClosePrompt(QMessageBox::Save);
        QVERIFY(!window.close());
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, true, engine.lastPath);
        QTRY_VERIFY(!engine.view->isEnabled());
        QVERIFY(window.isVisible());
        QVERIFY(title->text().contains(QStringLiteral("未保存")));
        QCOMPARE(engine.closeCalls, 0);
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
        QVERIFY(!window.close()); // No second prompt or duplicate request while waiting.
        QCOMPARE(engine.saveCalls, 1);
        QCOMPARE(engine.closeCalls, 0);

        engine.changeModified(false);
        QTRY_COMPARE(engine.closeCalls, 1);
        QVERIFY(!engine.lastDiscardChanges);
        QVERIFY(!engine.view->isEnabled());
        engine.changeModified(false);
        QCoreApplication::processEvents();
        QCOMPARE(engine.closeCalls, 1);
        engine.complete(engine.lastId, openoffice::EngineOperation::Close, false, QString(),
                        openoffice::DocumentKind::Unknown);
        QTRY_VERIFY(engine.view->isEnabled());
        QVERIFY(window.isVisible());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
        QCOMPARE(window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"))->text(),
                 QStringLiteral("test failed"));
    }

    void missingCleanNotificationTimesOutWithoutInventingSavedState()
    {
        ControlledEngine engine;
        engine.closeHandshake = true;
        openoffice::MainWindow window(&engine);
        window.show();
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
        engine.changeModified(true);
        auto* title = window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"));
        QTRY_VERIFY(title->text().contains(QStringLiteral("未保存")));

        answerClosePrompt(QMessageBox::Save);
        QVERIFY(!window.close());
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, true, engine.lastPath);
        QTRY_VERIFY(!engine.view->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(engine.view->isEnabled(), 6500);
        QVERIFY(window.isVisible());
        QVERIFY(engine.documentModified());
        QVERIFY(title->text().contains(QStringLiteral("未保存")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"))->text()
                    .contains(QStringLiteral("已恢复编辑")));
        QCOMPARE(engine.closeCalls, 0);
        engine.changeModified(false); // Late state must not revive a cancelled close intent.
        QCoreApplication::processEvents();
        QCOMPARE(engine.closeCalls, 0);
        QVERIFY(window.isVisible());
    }

    void createShowsEditorAndFirstSaveChoosesDestination()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        QString suggestion;
        window.setSaveDestinationChooser([&suggestion](const QString& suggested, openoffice::DocumentKind) {
            suggestion = suggested;
            return QStringLiteral("C:/chosen/新报告.docx");
        });
        action(window, "createTextAction")->trigger();
        QCOMPARE(engine.createCalls, 1);
        QCOMPARE(engine.lastTitle, QStringLiteral("文档1.docx"));
        QVERIFY(engine.lastKind == openoffice::DocumentKind::Text);
        QVERIFY(!engine.lastReplaceDiscard);
        QCOMPARE(window.findChild<QStackedWidget*>(QStringLiteral("workspaceStack"))->currentWidget()->objectName(),
                 QStringLiteral("editorHostPage"));
        QVERIFY(!window.documentOpened());
        engine.complete(engine.lastId, openoffice::EngineOperation::Create, true, QString());
        QTRY_VERIFY(window.documentOpened());
        auto* title = window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"));
        QVERIFY(title->text().contains(QStringLiteral("文档1.docx")));

        action(window, "saveDocumentAction")->trigger();
        QCOMPARE(suggestion, QStringLiteral("文档1.docx"));
        QCOMPARE(engine.saveCalls, 1);
        QCOMPARE(engine.lastPath, QStringLiteral("C:/chosen/新报告.docx"));
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, true, engine.lastPath);
        QTRY_VERIFY(title->text().contains(QStringLiteral("新报告.docx")));

        action(window, "createTextAction")->trigger(); // Clean document: replaced without a prompt.
        QCOMPARE(engine.createCalls, 2);
        QCOMPARE(engine.lastTitle, QStringLiteral("文档2.docx"));
    }

    void cancelledFirstSaveSendsNothing()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        window.setSaveDestinationChooser([](const QString&, openoffice::DocumentKind) { return QString(); });
        action(window, "createTextAction")->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Create, true, QString());
        QTRY_VERIFY(window.documentOpened());
        action(window, "saveDocumentAction")->trigger();
        QCOMPARE(engine.saveCalls, 0);
        QVERIFY(action(window, "saveDocumentAction")->isEnabled());
    }

    void saveAsAdoptsNewPathOnlyAfterConfirmation()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        QString chosen = QStringLiteral("C:/other/copy.docx");
        window.setSaveDestinationChooser([&chosen](const QString&, openoffice::DocumentKind) { return chosen; });
        openFixture(window, engine, "text-table.docx");
        auto* title = window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"));
        QVERIFY(action(window, "saveAsDocumentAction")->isEnabled());

        action(window, "saveAsDocumentAction")->trigger();
        QCOMPARE(engine.saveCalls, 1);
        QCOMPARE(engine.lastPath, chosen);
        QVERIFY(!action(window, "saveAsDocumentAction")->isEnabled());
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, false, QString());
        QTRY_COMPARE(window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"))->text(), QStringLiteral("test failed"));
        QVERIFY(title->text().contains(QStringLiteral("text-table.docx")));

        action(window, "saveAsDocumentAction")->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, true, chosen);
        QTRY_VERIFY(title->text().contains(QStringLiteral("copy.docx")));

        chosen.clear(); // Cancelled dialog: no request.
        action(window, "saveAsDocumentAction")->trigger();
        QCOMPARE(engine.saveCalls, 2);
    }

    void replacingModifiedDocumentAsksAndKeepsItOnFailure()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        openFixture(window, engine, "text-table.docx");
        engine.changeModified(true);
        auto* title = window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"));
        QTRY_VERIFY(title->text().contains(QStringLiteral("未保存")));
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/values-formula.xlsx"));

        QString promptButtons;
        QTimer::singleShot(0, [&promptButtons] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!dialog) return;
            for (auto choice : {QMessageBox::Save, QMessageBox::Discard, QMessageBox::Cancel})
                promptButtons += dialog->button(choice)->text() + QLatin1Char('|');
            dialog->button(QMessageBox::Cancel)->click();
        });
        action(window, "openDocumentAction")->trigger();
        QCOMPARE(engine.openCalls, 1);
        QCOMPARE(promptButtons, QStringLiteral("保存|不保存|取消|"));

        answerClosePrompt(QMessageBox::Discard);
        action(window, "openDocumentAction")->trigger();
        QCOMPARE(engine.openCalls, 2);
        QVERIFY(engine.lastReplaceDiscard);
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, false, QString());
        QTRY_COMPARE(window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"))->text(), QStringLiteral("test failed"));
        QVERIFY(window.documentOpened());
        QVERIFY(title->text().contains(QStringLiteral("text-table.docx")));
        QVERIFY(title->text().contains(QStringLiteral("未保存")));
        QVERIFY(action(window, "saveDocumentAction")->isEnabled());
    }

    void saveBeforeReplaceResumesOnlyAfterCleanState()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        openFixture(window, engine, "text-table.docx");
        engine.changeModified(true);
        QTRY_VERIFY(window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"))->text()
                    .contains(QStringLiteral("未保存")));
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/values-formula.xlsx"));

        answerClosePrompt(QMessageBox::Save);
        action(window, "openDocumentAction")->trigger();
        QCOMPARE(engine.saveCalls, 1);
        QCOMPARE(engine.openCalls, 1);
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, true, engine.lastPath);
        QTRY_VERIFY(!engine.view->isEnabled()); // Waiting for the editor's clean state.
        QCOMPARE(engine.openCalls, 1);
        engine.changeModified(false);
        QTRY_COMPARE(engine.openCalls, 2);
        QVERIFY(!engine.lastReplaceDiscard);
        QVERIFY(engine.view->isEnabled());
        QVERIFY(engine.lastPath.endsWith(QStringLiteral("values-formula.xlsx")));
    }

    void editorInitiatedSaveIsAnsweredOrCancelled()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        QString chosen = QStringLiteral("C:/chosen/a.docx");
        window.setSaveDestinationChooser([&chosen](const QString&, openoffice::DocumentKind) { return chosen; });
        action(window, "createTextAction")->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Create, true, QString());
        QTRY_VERIFY(window.documentOpened());

        engine.askDestination(QStringLiteral("文档1.docx"));
        QTRY_COMPARE(engine.saveCalls, 1);
        QCOMPARE(engine.lastPath, chosen);
        engine.askDestination(QStringLiteral("文档1.docx")); // Busy: never leave the editor waiting.
        QTRY_COMPARE(engine.cancelDestinationCalls, 1);
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, true, chosen);
        QTRY_VERIFY(action(window, "saveDocumentAction")->isEnabled());

        chosen.clear();
        engine.askDestination(QStringLiteral("C:/chosen/a.docx"));
        QTRY_COMPARE(engine.cancelDestinationCalls, 2);
        QCOMPARE(engine.saveCalls, 1);
    }

    void unsupportedKindsStayDisabled()
    {
        ControlledEngine engine;
        engine.textOnlyCreation = true;
        openoffice::MainWindow window(&engine);
        QVERIFY(action(window, "createTextAction")->isEnabled());
        QVERIFY(!action(window, "createSpreadsheetAction")->isEnabled());
        QVERIFY(!action(window, "createPresentationAction")->isEnabled());
        QVERIFY(!action(window, "editorPageAction")->isEnabled());
        action(window, "createTextAction")->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Create, true, QString());
        QTRY_VERIFY(window.documentOpened());
        // Nothing was selected: the selection card must not claim a selected file.
        QCOMPARE(window.findChild<QLabel*>(QStringLiteral("selectionStateLabel"))->text(), QStringLiteral("等待选择"));
        openFixture(window, engine, "text-table.docx");
        QTRY_VERIFY(action(window, "editorPageAction")->isEnabled());
        action(window, "homeAction")->trigger();
        auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("workspaceStack"));
        QVERIFY(stack->currentWidget()->objectName() != QStringLiteral("editorHostPage"));
        action(window, "editorPageAction")->trigger();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("editorHostPage"));
    }

    void continuousCornersStayInsideTheirRect()
    {
        const QRectF box(10, 20, 220, 96);
        const QPainterPath shape = openoffice::art::continuousRoundedRect(box, 24);
        QVERIFY(qAbs(shape.boundingRect().left() - box.left()) < 0.6);
        QVERIFY(qAbs(shape.boundingRect().right() - box.right()) < 0.6);
        QVERIFY(qAbs(shape.boundingRect().top() - box.top()) < 0.6);
        QVERIFY(qAbs(shape.boundingRect().bottom() - box.bottom()) < 0.6);
        QVERIFY(shape.contains(box.center()));
        QVERIFY(shape.contains(QPointF(box.center().x(), box.top() + 0.8)));    // straight edges remain
        for (const QPointF corner : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()})
            QVERIFY(!shape.contains(corner + (box.center() - corner) * 0.02)); // corners are cut
        // Preview for visual review: shapes on a white page with blurred shadows.
        QImage image(520, 260, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPainterPath card = openoffice::art::continuousRoundedRect(QRectF(30, 30, 260, 180), 24);
        openoffice::art::paintSoftShadow(painter, card, 18, QPointF(0, 8), QColor(0, 0, 0, 34));
        painter.setPen(QPen(QColor(0, 0, 0, 24), 1));
        painter.setBrush(QColor(242, 242, 247));
        painter.drawPath(card);
        const QPainterPath capsule = openoffice::art::continuousRoundedRect(QRectF(320, 160, 170, 46), 23);
        openoffice::art::paintSoftShadow(painter, capsule, 14, QPointF(0, 6), QColor(0, 0, 0, 40));
        painter.setBrush(Qt::white);
        painter.drawPath(capsule);
        painter.end();
        QVERIFY(image.save(QStringLiteral("assistant-shapes-preview.png")));
    }

    void assistantBallArtRendersBothThemes()
    {
        // A preview of the real renderer for design review: large and toolbar
        // sizes on light and dark surfaces, idle and working.
        QImage image(760, 420, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        // Light tile uses a white page, the surface the ball floats over most.
        const QColor page = Qt::white;
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor surfaces[] = {page, QColor("#1C1C1E")};
        for (int dark = 0; dark < 2; ++dark) {
            const QRectF tile(20 + dark * 370, 20, 350, 380);
            painter.setPen(Qt::NoPen);
            painter.setBrush(surfaces[dark]);
            painter.drawPath(openoffice::art::continuousRoundedRect(tile, 28));
            const QRectF big(tile.center().x() - 90, tile.top() + 36, 180, 180);
            QPainterPath footprint;
            footprint.addEllipse(big.center(), 80, 80);
            openoffice::art::paintSoftShadow(painter, footprint, 22, QPointF(0, 16), QColor(0, 0, 0, dark ? 150 : 60));
            openoffice::art::paintBall(painter, big, 1.2, 0.0, dark);
            const QRectF ball(tile.left() + 70, tile.top() + 270, 58, 58);
            openoffice::art::paintBallPresence(painter, ball.center(), 29, 0.0, dark);
            openoffice::art::paintBall(painter, ball, 1.2, 0.0, dark);
            const QRectF working(tile.left() + 170, tile.top() + 270, 58, 58);
            openoffice::art::paintBallPresence(painter, working.center(), 29, 1.0, dark);
            openoffice::art::paintActivityArc(painter, working.center(), 35, 0.9, 1.0, dark);
            openoffice::art::paintBall(painter, working, 2.0, 1.0, dark);
            openoffice::art::paintBall(painter, QRectF(tile.left() + 270, tile.top() + 290, 18, 18), 1.2, 0.0, dark);
        }
        painter.end();
        // The light ball's core is bright and its petals are coloured.
        QVERIFY(qGray(image.pixel(195, 146)) > 200);
        QVERIFY(image.save(QStringLiteral("assistant-ball-preview.png")));
        QVERIFY(!openoffice::art::assistantIcon(18).pixmap(18, 18).isNull());
    }

    void assistantBallFollowsTheDocumentPageAndStaysWhereDropped()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        auto* ball = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        QVERIFY(ball);
        QVERIFY(ball->isWindow()); // floats above the embedded editor
        window.show();
        QTest::qWait(30);
        QVERIFY(!ball->isVisible()); // the home page has no document
        QVERIFY(action(window, "assistantAction")->isChecked());
        openFixture(window, engine, "text-table.docx");
        QTRY_VERIFY(ball->isVisible());
        auto* page = window.findChild<QWidget*>(QStringLiteral("editorHostPage"));
        const QRect area(page->mapToGlobal(QPoint(0, 0)), page->size());
        QVERIFY(area.contains(ball->geometry().center()));
        action(window, "homeAction")->trigger();
        QTRY_VERIFY(!ball->isVisible());
        action(window, "editorPageAction")->trigger();
        QTRY_VERIFY(ball->isVisible());

        // Free placement: dropped in the middle of the page, it stays there.
        const QPoint grip = ball->rect().center();
        const QPoint target = area.center();
        QTest::mouseMove(ball, grip);
        QTest::mousePress(ball, Qt::LeftButton, {}, grip);
        QTest::mouseMove(ball, grip + (target - ball->geometry().center()));
        QTest::mouseRelease(ball, Qt::LeftButton, {}, grip);
        QTRY_VERIFY((ball->geometry().center() - target).manhattanLength() <= 4);
        QVERIFY(!ball->expanded());

        // Dropped outside the page, it springs back inside its edge.
        QTest::mousePress(ball, Qt::LeftButton, {}, grip);
        QTest::mouseMove(ball, grip + QPoint(-3000, -3000));
        QTest::mouseRelease(ball, Qt::LeftButton, {}, grip);
        QTRY_VERIFY(area.contains(ball->geometry().center()));
        QVERIFY(ball->geometry().center().y() < area.center().y()); // stays near where it was let go
        // Near the top there is no room above: it opens downward, inside the page.
        ball->expand();
        QVERIFY(ball->expanded());
        QVERIFY(ball->geometry().top() >= area.top() - 60);
        ball->collapse();
        QTRY_VERIFY(!ball->expanded());

        action(window, "assistantAction")->trigger();
        QTRY_VERIFY(!ball->isVisible());
        action(window, "assistantAction")->trigger();
        QTRY_VERIFY(ball->isVisible());
        window.hide();
        QTRY_VERIFY(!ball->isVisible());
    }

    void assistantEditorCommandWaitsForEngineReport()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        QTemporaryDir journalFolder;
        QVERIFY(journalFolder.isValid());
        const QString journal = journalFolder.filePath(QStringLiteral("assistant-commands.jsonl"));
        window.setAssistantJournalPath(journal);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));

        panel->submitText(QStringLiteral("/插入 你好 世界"));
        QCOMPARE(engine.editorCommandCalls, 0); // nothing is open yet
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("请先打开或新建文档")));

        openFixture(window, engine, "text-table.docx");
        panel->submitText(QStringLiteral("/插入 你好 世界"));
        QCOMPARE(engine.editorCommandCalls, 1);
        QCOMPARE(engine.lastCommandName, QStringLiteral("text.insert"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("text")).toString(), QStringLiteral("你好 世界"));
        QVERIFY(panel->working());
        const int before = panel->messageCount();
        engine.completeCommand(engine.lastCommandId + 1, true, QStringLiteral("stale")); // not ours
        QCoreApplication::processEvents();
        QVERIFY(panel->working());
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已插入 5 个字符。"));
        QTRY_VERIFY(!panel->working());
        QCOMPARE(panel->messageCount(), before + 1);
        QCOMPARE(panel->lastMessageText(), QStringLiteral("已插入 5 个字符。"));

        QFile log(journal);
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = log.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        const QJsonObject entry = QJsonDocument::fromJson(lines.last()).object();
        QCOMPARE(entry.value(QStringLiteral("command")).toString(), QStringLiteral("text.insert"));
        QCOMPARE(entry.value(QStringLiteral("source")).toString(), QStringLiteral("user"));
        QVERIFY(entry.value(QStringLiteral("ok")).toBool());
        QVERIFY(!lines.last().contains("你好")); // metadata only, never the text
    }

    void assistantRefusesOverlapAndReportsEngineRefusal()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        window.runAssistantCommand({QStringLiteral("edit.undo"), {}, openoffice::CommandSource::External});
        QCOMPARE(engine.editorCommandCalls, 1);
        window.runAssistantCommand({QStringLiteral("edit.redo"), {}, openoffice::CommandSource::External});
        QCOMPARE(engine.editorCommandCalls, 1);
        QCOMPARE(panel->lastMessageText(), QStringLiteral("上一条命令还在执行。"));
        engine.completeCommand(engine.lastCommandId, false, QStringLiteral("没有可撤销的操作。"));
        QTRY_VERIFY(!panel->working());
        QCOMPARE(panel->lastMessageText(), QStringLiteral("没有可撤销的操作。"));

        engine.acceptRequests = false;
        window.runAssistantCommand({QStringLiteral("selection.getText"), {}, openoffice::CommandSource::User});
        QCOMPARE(panel->lastMessageText(), QStringLiteral("test command request"));
        QVERIFY(!panel->working());
        window.runAssistantCommand({QStringLiteral("text.insert"), {}, openoffice::CommandSource::Assistant});
        QCOMPARE(engine.editorCommandCalls, 2); // schema failure never reaches the engine
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("text")));
    }

    void localAiChangesTheDocumentOnlyAfterConfirmation()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools")})});
        fake.chats = {FakeOllama::chat({QStringLiteral("好的。")}, QStringLiteral("text_insert"),
                                       {{QStringLiteral("text"), QStringLiteral("会议纪要")}}),
                      FakeOllama::chat({QStringLiteral("已经插入。")}),
                      FakeOllama::chat({}, QStringLiteral("text_insert"), {{QStringLiteral("text"), QStringLiteral("不要的")}}),
                      FakeOllama::chat({QStringLiteral("好的，没有插入。")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        ControlledEngine engine;
        engine.answerReads = true;
        engine.documentText = QStringLiteral("本周五在三楼开会。");
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        const auto button = [panel](const QString& text) -> QPushButton* {
            QPushButton* found = nullptr; // the newest card's button with this text
            for (QPushButton* candidate : panel->findChildren<QPushButton*>())
                if (candidate->text() == text && candidate->isEnabled()) found = candidate;
            return found;
        };

        panel->submitText(QStringLiteral("插入会议纪要四个字"));
        QTRY_VERIFY(button(QStringLiteral("执行")) != nullptr);
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("会议纪要")));
        QCOMPARE(engine.editorCommandCalls, 0); // proposed, not done
        const QString system = fake.chatRequests().first().value(QStringLiteral("messages")).toArray().first().toObject()
                                   .value(QStringLiteral("content")).toString();
        QVERIFY(system.contains(QStringLiteral("text-table.docx")));
        QVERIFY(system.contains(QStringLiteral("本周五在三楼开会。"))); // the model sees the real document
        QVERIFY(engine.readCalls >= 2);
        button(QStringLiteral("执行"))->click();
        QCOMPARE(engine.editorCommandCalls, 1);
        QCOMPARE(engine.lastCommandName, QStringLiteral("text.insert"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("text")).toString(), QStringLiteral("会议纪要"));
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已插入 4 个字符。"));
        QTRY_COMPARE(panel->lastMessageText(), QStringLiteral("已经插入。"));
        QTRY_VERIFY(!panel->working());

        panel->submitText(QStringLiteral("再插入点别的"));
        QTRY_VERIFY(button(QStringLiteral("不要")) != nullptr);
        button(QStringLiteral("不要"))->click();
        QTRY_COMPARE(panel->lastMessageText(), QStringLiteral("好的，没有插入。"));
        QCOMPARE(engine.editorCommandCalls, 1); // declined: the engine never saw it
        QTRY_VERIFY(!panel->working());
        QCOMPARE(fake.chatRequests().size(), 4);
    }

    void quickActionsReworkSelectionProofreadAndDraft()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools")})});
        const QJsonObject corrections{{QStringLiteral("corrections"), QJsonArray{
            QJsonObject{{QStringLiteral("original"), QStringLiteral("按排")}, {QStringLiteral("corrected"), QStringLiteral("安排")},
                        {QStringLiteral("reason"), QStringLiteral("错别字")}},
            QJsonObject{{QStringLiteral("original"), QStringLiteral("不在文中")}, {QStringLiteral("corrected"), QStringLiteral("x")},
                        {QStringLiteral("reason"), QStringLiteral("模型编的")}}}}};
        fake.chats = {FakeOllama::chat({QStringLiteral("以下是润色后的文字：\n"), QStringLiteral("请大家准时参加会议。")}),
                      FakeOllama::chat({QString::fromUtf8(QJsonDocument(corrections).toJson(QJsonDocument::Compact))}),
                      FakeOllama::chat({QStringLiteral("# 通知\n\n请于【时间】到三楼参加安全会。")}),
                      FakeOllama::chat({QStringLiteral(R"({"style":"official-classic"})")})};
        openoffice::saveStyleProfile({}); // what the style profile learns here starts from nothing
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        ControlledEngine engine;
        engine.answerReads = true;
        engine.selectionText = QStringLiteral("大家要准时来开会");
        engine.documentText = QStringLiteral("请按排好时间。\n大家要准时来开会");
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        const auto button = [panel](const QString& text) -> QPushButton* {
            QPushButton* found = nullptr;
            for (QPushButton* candidate : panel->findChildren<QPushButton*>())
                if (candidate->text() == text && candidate->isEnabled()) found = candidate;
            return found;
        };

        // Opening the ball finds the local AI, reads the selection and offers rework.
        panel->expand();
        QTRY_VERIFY(panel->quickActionIds().contains(QStringLiteral("polish")));
        QVERIFY(panel->quickContext().contains(QStringLiteral("大家要准时来开会")));
        panel->triggerQuickAction(QStringLiteral("polish"));
        QTRY_VERIFY(button(QStringLiteral("替换原文")) != nullptr);
        QCOMPARE(panel->lastMessageText(), QStringLiteral("请大家准时参加会议。")); // the intro line is gone
        const QJsonObject polish = fake.chatRequests().first();
        QVERIFY(!polish.contains(QStringLiteral("tools"))); // one answer, no tool round trip
        QVERIFY(polish.value(QStringLiteral("messages")).toArray().last().toObject().value(QStringLiteral("content"))
                    .toString().contains(QStringLiteral("大家要准时来开会")));
        QCOMPARE(engine.editorCommandCalls, 0); // nothing changes before the person chooses
        button(QStringLiteral("替换原文"))->click();
        // A quiet read of the selection may still be running; the replacement waits its turn.
        QTRY_COMPARE(engine.editorCommandCalls, 1);
        QCOMPARE(engine.lastCommandName, QStringLiteral("selection.replace"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("text")).toString(), QStringLiteral("请大家准时参加会议。"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("expected")).toString(), QStringLiteral("大家要准时来开会"));
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已替换选中的文字，可以撤销。"));
        QTRY_VERIFY(!panel->working());

        // Proofreading offers only fixes that exist in the document.
        engine.selectionText.clear();
        panel->triggerQuickAction(QStringLiteral("proofread"));
        QTRY_COMPARE(panel->correctionCount(), 1);
        QVERIFY(fake.chatRequests().at(1).value(QStringLiteral("format")).isObject()); // structured answer
        button(QStringLiteral("改"))->click();
        QTRY_COMPARE(engine.editorCommandCalls, 2); // after a quiet read that may still be running
        QCOMPARE(engine.lastCommandName, QStringLiteral("document.replaceText"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("find")).toString(), QStringLiteral("按排"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("replace")).toString(), QStringLiteral("安排"));
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已替换 1 处，可以撤销。"));
        const auto marked = [panel] {
            for (QPushButton* candidate : panel->findChildren<QPushButton*>())
                if (candidate->text() == QStringLiteral("✓ 已改") && !candidate->isEnabled()) return true;
            return false;
        };
        QTRY_VERIFY(marked()); // the fix is marked done and cannot be applied twice

        // Drafting: pick the kind, type a few words, insert with real formatting.
        panel->triggerQuickAction(QStringLiteral("draft"));
        QVERIFY(panel->quickActionIds().contains(QStringLiteral("notice")));
        panel->triggerQuickAction(QStringLiteral("notice"));
        QCOMPARE(panel->draftMode(), QStringLiteral("notice"));
        panel->submitText(QStringLiteral("周五下午三点三楼安全会"));
        QCOMPARE(panel->draftMode(), QString());
        // The draft gets a look: the program scores the style packs, the model picks one of the best.
        QTRY_VERIFY(button(QStringLiteral("换个风格")) != nullptr);
        QVERIFY(fake.chatRequests().at(2).value(QStringLiteral("messages")).toArray().last().toObject()
                    .value(QStringLiteral("content")).toString().contains(QStringLiteral("周五下午三点三楼安全会")));
        const QJsonObject styleAsk = fake.chatRequests().at(3);
        QVERIFY(!styleAsk.contains(QStringLiteral("tools")));
        const QJsonArray offered = styleAsk.value(QStringLiteral("format")).toObject().value(QStringLiteral("properties")).toObject()
                                       .value(QStringLiteral("style")).toObject().value(QStringLiteral("enum")).toArray();
        QCOMPARE(offered.size(), 5);
        QVERIFY(offered.contains(QStringLiteral("official-classic"))); // a notice: the official look scores best
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("黑体标题、仿宋正文")));
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("模型从 5 个候选里挑的")));
        QCOMPARE(engine.editorCommandCalls, 2); // nothing goes in before the person chooses
        // 换个风格: another pack by the weights, shown at once (no model call).
        QTRY_VERIFY(!panel->working());
        button(QStringLiteral("换个风格"))->click();
        QTRY_VERIFY(!panel->lastMessageText().contains(QStringLiteral("黑体标题、仿宋正文")) && button(QStringLiteral("插入文档")) != nullptr);
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("按场景和权重换的")));
        button(QStringLiteral("插入文档"))->click();
        QTRY_COMPARE(engine.editorCommandCalls, 3);
        QCOMPARE(engine.lastCommandName, QStringLiteral("text.insert"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("format")).toString(), QStringLiteral("markdown"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("position")).toString(), QStringLiteral("after"));
        const QString noticeHtml = engine.lastCommandArguments.value(QStringLiteral("html")).toString();
        QVERIFY(noticeHtml.contains(QStringLiteral("通知</h1>")) && noticeHtml.contains(QStringLiteral("<h1 style=")));
        QVERIFY(!noticeHtml.contains(QStringLiteral("FangSong"))); // the look picked second, not the first
        // The style profile learned: the first look was switched away from, the second chosen.
        const openoffice::StyleProfile learned = openoffice::loadStyleProfile();
        QCOMPARE(learned.entries.size(), 2);
        QCOMPARE(learned.entries[0].pack, QStringLiteral("official-classic"));
        QCOMPARE(learned.entries[0].action, QStringLiteral("rejected"));
        QCOMPARE(learned.entries[0].scene, QStringLiteral("official"));
        QCOMPARE(learned.entries[1].action, QStringLiteral("chosen"));
        QVERIFY(learned.entries[1].pack != QStringLiteral("official-classic"));
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已插入 20 个字符。"));
        QTRY_VERIFY(!panel->working());
        QCOMPARE(fake.chatRequests().size(), 4);
    }

    void styleProfileShowsWhatWasLearnedAndCanBeSet()
    {
        using namespace openoffice;
        StyleProfile profile;
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        for (const char* document : {"D:/a.docx", "D:/b.docx"})
            for (int i = 0; i < 2; ++i)
                recordStyleSignal(&profile, QStringLiteral("tech-teal"), QStringLiteral("tech"), QString::fromLatin1(document),
                                  QStringLiteral("chosen"), now);
        recordStyleSignal(&profile, QStringLiteral("festive-red"), QStringLiteral("event"), QStringLiteral("D:/c.docx"),
                          QStringLiteral("rejected"), now);
        saveStyleProfile(profile);
        StyleProfileDialog dialog;
        QVERIFY(dialog.summaryText().contains(QStringLiteral("已记录 5 次选择")));
        const auto row = [&dialog](const QString& pack) {
            for (const QString& text : dialog.rowTexts())
                if (text.startsWith(pack + QLatin1Char(' '))) return text;
            return QString();
        };
        QCOMPARE(dialog.rowTexts().size(), builtinStylePacks().size());
        QVERIFY(row(QStringLiteral("tech-teal")).contains(QStringLiteral("你常选它（4 次，2 份文档）")));
        QVERIFY(row(QStringLiteral("tech-teal")).endsWith(QStringLiteral("[自动]")));
        QVERIFY(row(QStringLiteral("festive-red")).contains(QStringLiteral("还在观察（1 次，1 份文档）")));
        QVERIFY(row(QStringLiteral("slate-minimal")).contains(QStringLiteral("还没有记录")));
        // A picture of it, only when asked for (development: to look at the window).
        if (const QString folder = qEnvironmentVariable("OPEN_OFFICE_TEST_SNAPSHOTS"); !folder.isEmpty()) {
            dialog.resize(640, 680);
            dialog.show();
            QApplication::processEvents();
            QVERIFY(dialog.grab().save(QDir(folder).filePath(QStringLiteral("style-profile.png"))));
        }
        // Set by hand, paused, cleared: each kept at once.
        dialog.setManual(QStringLiteral("festive-red"), -1);
        QVERIFY(row(QStringLiteral("festive-red")).contains(QStringLiteral("你设为不用")) && row(QStringLiteral("festive-red")).endsWith(QStringLiteral("[不用]")));
        QCOMPARE(loadStyleProfile().manual.value(QStringLiteral("festive-red")), -1);
        dialog.setPaused(true);
        QVERIFY(dialog.summaryText().contains(QStringLiteral("学习已暂停")) && loadStyleProfile().paused);
        dialog.clearLearned();
        QVERIFY(dialog.summaryText().contains(QStringLiteral("已记录 0 次选择")));
        QCOMPARE(loadStyleProfile().manual.value(QStringLiteral("festive-red")), -1); // what was set by hand stays
        QCOMPARE(dialog.rowTexts().size(), builtinStylePacks().size());
        saveStyleProfile({});
    }

    void quickActionBarFitsChipsThatArriveWhileOpen()
    {
        // The selection is read after the ball opens, so its chips arrive while it is open. They used to be
        // measured before the style sheet reached them; the bar kept that short height and squashed them.
        QTemporaryDir folder;
        ScopedEnvironment backend("OPEN_OFFICE_AI_BACKEND", "builtin");
        ScopedEnvironment engineUrl("OPEN_OFFICE_AI_ENGINE_URL", closedOllamaUrl());
        ScopedEnvironment directory("OPEN_OFFICE_MODEL_DIR", QFile::encodeName(folder.path()));
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        auto* ball = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        window.show();
        openFixture(window, engine, "text-table.docx");
        QTRY_VERIFY(ball->isVisible());
        ball->expand();
        auto* bar = ball->findChild<QWidget*>(QStringLiteral("assistantActionBar"));
        auto* context = ball->findChild<QLabel*>(QStringLiteral("assistantContext"));
        QVERIFY(bar && context);
        const auto squashed = [bar, context] {
            QStringList found;
            if (!bar->isVisible()) found << QStringLiteral("bar hidden");
            if (context->height() < context->sizeHint().height())
                found << QStringLiteral("context %1 < %2").arg(context->height()).arg(context->sizeHint().height());
            int chips = 0;
            for (QPushButton* chip : bar->findChildren<QPushButton*>(QStringLiteral("assistantChip"))) {
                ++chips;
                if (chip->height() < chip->sizeHint().height() || !bar->rect().contains(chip->geometry()))
                    found << QStringLiteral("%1 %2 < %3").arg(chip->text()).arg(chip->height()).arg(chip->sizeHint().height());
            }
            if (!chips) found << QStringLiteral("no chips");
            return found.join(QStringLiteral("; "));
        };
        QTRY_COMPARE(squashed(), QString()); // the chips present when it opened

        const QList<openoffice::AssistantBall::QuickAction> selection{
            {QStringLiteral("polish"), QStringLiteral("润色")}, {QStringLiteral("proofread"), QStringLiteral("纠错")},
            {QStringLiteral("shorten"), QStringLiteral("精简")}, {QStringLiteral("expand"), QStringLiteral("扩写")},
            {QStringLiteral("formal"), QStringLiteral("改正式")}, {QStringLiteral("translate"), QStringLiteral("翻译")}};
        ball->setQuickActions(QStringLiteral("已选中 114 字：关于开展安全培训的通知各部门：为了提高大家的安全意识，公司决定…"),
                              selection);
        QTest::qWait(300); // the new chips are shown and styled on the next turns of the event loop
        QCOMPARE(squashed(), QString());
        ball->setQuickActions(QStringLiteral("处理整篇文档"), {{QStringLiteral("draft"), QStringLiteral("起草…")}});
        QTest::qWait(300);
        QCOMPARE(squashed(), QString());
        ball->setQuickActions(QStringLiteral("已选中 4 字：大家要来"), selection);
        QTest::qWait(300);
        QCOMPARE(squashed(), QString());
    }

    void aiSettingsListModelsAndMarkTheOneForThisComputer()
    {
        QTemporaryDir folder;
        QFile model(folder.filePath(QStringLiteral("Qwen3-1.7B-Q8_0.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.close();
        ScopedEnvironment backend("OPEN_OFFICE_AI_BACKEND", "builtin");
        ScopedEnvironment engineUrl("OPEN_OFFICE_AI_ENGINE_URL", closedOllamaUrl()); // an engine is "there", never started
        ScopedEnvironment directory("OPEN_OFFICE_MODEL_DIR", QFile::encodeName(folder.path()));
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        auto* dialog = window.openAiSettings();
        QVERIFY(dialog->isVisible());
        QVERIFY(dialog->styleSummaryText().contains(QStringLiteral("起草时按你选过的排版风格调整推荐")));
        dialog->openStyleProfile();
        auto* styles = dialog->findChild<openoffice::StyleProfileDialog*>();
        QVERIFY(styles && styles->isVisible());
        styles->close();
        // Shows where the program is (models go with it); no folder of their own to pick.
        QCOMPARE(dialog->folderText(), QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
        QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("aiSettingsChangeFolder")));
        QVERIFY(dialog->findChild<QPushButton*>(QStringLiteral("aiSettingsRelocate")));
        QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("aiSettingsFolderNote"))->text()
                    .contains(QDir::toNativeSeparators(QDir::cleanPath(folder.path())))); // the development override is said
        const QStringList rows = dialog->rowTexts();
        QCOMPARE(rows.size(), 3);
        QVERIFY(rows[0].contains(QStringLiteral("Qwen3 1.7B")) && rows[0].contains(QStringLiteral("已下载"))
                && rows[0].contains(QStringLiteral("[删除]")));
        QVERIFY(rows[1].contains(QStringLiteral("[下载]")) && rows[2].contains(QStringLiteral("[下载]")));
        int marked = 0;
        for (const QString& row : rows) marked += row.contains(QStringLiteral("推荐")) ? 1 : 0;
        QCOMPARE(marked, 1);
        QVERIFY(rows[1].contains(QStringLiteral("Qwen3 4B")) && rows[1].contains(QStringLiteral("推荐"))); // 4B is the recommendation
        QVERIFY(dialog->engineText().contains(QStringLiteral("内置引擎")));
        QCOMPARE(window.openAiSettings(), dialog); // one settings window
        dialog->close();
    }

    void typedRequestsUseTasksAndModelInsertsAreTidy()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools")})});
        fake.chats = {FakeOllama::chat({QStringLiteral("扩写以后更加详细的句子。")}),
                      FakeOllama::chat({QStringLiteral("# 学习方法\n\n- 制定计划\n- 坚持复习")}),
                      FakeOllama::chat({QStringLiteral(R"({"style":"fresh-green"})")}),
                      FakeOllama::chat({}, QStringLiteral("text_insert"),
                                       {{QStringLiteral("text"), QStringLiteral("## 小标题\n- 第一条要点内容\n- 第二条要点内容")}}),
                      FakeOllama::chat({QStringLiteral("已插入，内容如下：\n\n## 小标题\n- 第一条要点内容\n- 第二条要点内容")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        ControlledEngine engine;
        engine.answerReads = true;
        engine.selectionText = QStringLiteral("原来的句子。");
        engine.documentText = QStringLiteral("原来的句子。");
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        const auto button = [panel](const QString& text) -> QPushButton* {
            QPushButton* found = nullptr;
            for (QPushButton* candidate : panel->findChildren<QPushButton*>())
                if (candidate->text() == text && candidate->isEnabled()) found = candidate;
            return found;
        };

        // "详细点" with text selected: a rewrite of that text, shown first, to replace it.
        panel->submitText(QStringLiteral("详细点"));
        QTRY_VERIFY(button(QStringLiteral("替换原文")) != nullptr);
        const QJsonObject rework = fake.chatRequests().first();
        QVERIFY(!rework.contains(QStringLiteral("tools"))); // no inserting at the cursor on its own
        const QString reworkUser = rework.value(QStringLiteral("messages")).toArray().last().toObject()
                                       .value(QStringLiteral("content")).toString();
        QVERIFY(reworkUser.contains(QStringLiteral("扩写到")) && reworkUser.contains(QStringLiteral("原来的句子。")));
        QCOMPARE(engine.editorCommandCalls, 0);
        QTRY_VERIFY(!panel->working()); // the quick actions read the selection right after a result
        button(QStringLiteral("替换原文"))->click();
        QCOMPARE(engine.lastCommandName, QStringLiteral("selection.replace"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("text")).toString(), QStringLiteral("扩写以后更加详细的句子。"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("expected")).toString(), QStringLiteral("原来的句子。"));
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已替换选中的文字，可以撤销。"));
        QTRY_VERIFY(!panel->working());

        // "写一篇…": drafted and shown, then inserted with real formatting in one piece.
        engine.selectionText.clear();
        panel->submitText(QStringLiteral("写一篇关于学习的文章"));
        QTRY_VERIFY(button(QStringLiteral("插入文档")) != nullptr);
        QVERIFY(!fake.chatRequests().at(1).contains(QStringLiteral("tools")));
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("绿色标题"))); // 清新绿, the model's pick
        QTRY_VERIFY(!panel->working()); // the quick actions read the selection right after a result
        button(QStringLiteral("插入文档"))->click();
        QCOMPARE(engine.lastCommandName, QStringLiteral("text.insert"));
        const QString green = openoffice::paletteFor(145, 0.5).primary;
        const QString draftHtml = engine.lastCommandArguments.value(QStringLiteral("html")).toString();
        QVERIFY(draftHtml.contains(QStringLiteral("<h1 style=\"text-align:center;color:%1").arg(green)) && draftHtml.contains(QStringLiteral("学习方法</h1>")));
        QVERIFY(draftHtml.contains(QStringLiteral("<li style=\"font-family:'DengXian'")));
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已插入 12 个字符。"));
        QTRY_VERIFY(!panel->working());

        // Anything else is a conversation; Markdown the model did not mark is formatted, and
        // its answer does not repeat what it wrote.
        panel->submitText(QStringLiteral("在后面加两条要点"));
        QTRY_VERIFY(button(QStringLiteral("执行")) != nullptr);
        QVERIFY(fake.chatRequests().at(3).contains(QStringLiteral("tools")));
        button(QStringLiteral("执行"))->click(); // the answer waits for this, so the ball stays busy
        QCOMPARE(engine.lastCommandName, QStringLiteral("text.insert"));
        // The document keeps the look its draft went in with: a section, not a second title.
        const QString pointsHtml = engine.lastCommandArguments.value(QStringLiteral("html")).toString();
        QVERIFY(pointsHtml.contains(QStringLiteral("<h2 style=\"color:%1").arg(green)) && pointsHtml.contains(QStringLiteral("小标题</h2>")));
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已插入 20 个字符。"));
        QTRY_COMPARE(panel->lastMessageText(), QStringLiteral("已经写进文档了，可以在文档里查看。需要调整的话告诉我。"));
        QTRY_VERIFY(!panel->working());

        // A table the model wrote as HTML (the 1.7B does): previewed and inserted as a real,
        // page-wide table at the end as asked, and read back after inserting.
        fake.chats.append(FakeOllama::chat({}, QStringLiteral("text_insert"),
            {{QStringLiteral("text"), QStringLiteral("<table border=\"1\"><thead><tr><th>姓名</th><th>电话</th></tr></thead><tbody>"
                                                     "<tr><td>张三</td><td>13800000001</td></tr><tr><td>李四</td><td>13800000002</td></tr>"
                                                     "</tbody></table>")},
             {QStringLiteral("format"), QStringLiteral("markdown")}}));
        fake.chats.append(FakeOllama::chat({QStringLiteral("表格加在文档最后了。")}));
        panel->submitText(QStringLiteral("在文末加一个表格，两列：姓名、电话"));
        QTRY_VERIFY(button(QStringLiteral("执行")) != nullptr);
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("在文档最后插入")));
        QVERIFY(!panel->lastMessageText().contains(QStringLiteral("&lt;td")));
        engine.outlineValue = {{QStringLiteral("tables"), 1}, {QStringLiteral("paragraphs"), 1},
                               {QStringLiteral("blocks"), QJsonArray{QJsonObject{
                                   {QStringLiteral("id"), QStringLiteral("T1")}, {QStringLiteral("rows"), 3}, {QStringLiteral("columns"), 2},
                                   {QStringLiteral("firstRow"), QJsonArray{QStringLiteral("姓名"), QStringLiteral("电话")}},
                                   {QStringLiteral("width"), 100}}}}};
        button(QStringLiteral("执行"))->click();
        QCOMPARE(engine.lastCommandName, QStringLiteral("text.insert"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("position")).toString(), QStringLiteral("end"));
        const QString tableHtml = engine.lastCommandArguments.value(QStringLiteral("html")).toString();
        QVERIFY(tableHtml.contains(QStringLiteral("width:100%")) && tableHtml.contains(QStringLiteral(">姓名</th>"))
                && tableHtml.contains(QStringLiteral("13800000002</td>")));
        QVERIFY(!tableHtml.contains(QStringLiteral("&lt;")));
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已插入 30 个字符。"));
        QTRY_VERIFY(!panel->working());
        const QJsonArray sent = fake.chatRequests().last().value(QStringLiteral("messages")).toArray();
        QVERIFY(sent.last().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("插入后核对")));
    }

    void tableRequestsArePlannedAndAppliedOneByOne()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools")})});
        fake.chats = {FakeOllama::chat({QStringLiteral(R"({"plan":[{"cells":"R2C1","change":"text","reason":"错别字","value":"笔记本"},)"
                                                        R"({"cells":"R1C1-R1C3","change":"bold","reason":"表头","value":"true"},)"
                                                        R"({"cells":"R2C2","change":"text","reason":"没变","value":"10"}]})")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        ControlledEngine engine;
        engine.answerReads = true;
        engine.outlineValue = {{QStringLiteral("tables"), 1}, {QStringLiteral("paragraphs"), 2},
                               {QStringLiteral("cursor"), QJsonObject{{QStringLiteral("table"), 1}, {QStringLiteral("row"), 2},
                                                                      {QStringLiteral("column"), 1}}}};
        const auto cell = [](const QString& text) { return QJsonObject{{QStringLiteral("text"), text}}; };
        engine.tableValue = {{QStringLiteral("table"), 1}, {QStringLiteral("rows"), QJsonArray{
            QJsonArray{cell(QStringLiteral("品名")), cell(QStringLiteral("数量")), cell(QStringLiteral("单价"))},
            QJsonArray{cell(QStringLiteral("笔计本")), cell(QStringLiteral("10")), cell(QStringLiteral("12"))}}}};
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        const auto button = [panel](const QString& text) -> QPushButton* {
            QPushButton* found = nullptr;
            for (QPushButton* candidate : panel->findChildren<QPushButton*>())
                if (candidate->text() == text && candidate->isEnabled()) found = candidate;
            return found;
        };

        // The cursor is in the table: the table is read and planned, not rewritten. A check
        // changes words and numbers: the model's "表头加粗" is no error, and the no-op is dropped.
        panel->submitText(QStringLiteral("帮我检查一下这个表格"));
        QTRY_COMPARE(panel->correctionCount(), 1);
        QCOMPARE(engine.lastReadArguments.value(QStringLiteral("table")).toInt(), 1);
        const QJsonObject request = fake.chatRequests().first();
        QVERIFY(!request.contains(QStringLiteral("tools")));
        QVERIFY(request.value(QStringLiteral("format")).toObject().value(QStringLiteral("properties")).toObject().contains(QStringLiteral("plan")));
        const QString user = request.value(QStringLiteral("messages")).toArray().last().toObject().value(QStringLiteral("content")).toString();
        QVERIFY(user.contains(QStringLiteral("R2C1：笔计本")) && user.contains(QStringLiteral("帮我检查一下这个表格")));
        QCOMPARE(engine.editorCommandCalls, 0); // nothing changes before the person chooses

        // 全部改正: one change after another, each its own command.
        QTRY_VERIFY(!panel->working());
        const auto shown = [panel] {
            QStringList texts;
            for (QPushButton* candidate : panel->findChildren<QPushButton*>())
                texts << candidate->text() + (candidate->isEnabled() ? QString() : QStringLiteral("(off)"));
            return texts.join(QLatin1Char(',')) + QStringLiteral(" | ") + panel->lastMessageText();
        };
        QTRY_VERIFY2(button(QStringLiteral("全部改正")) != nullptr, qPrintable(shown()));
        button(QStringLiteral("全部改正"))->click();
        QTRY_COMPARE(engine.lastCommandName, QStringLiteral("table.setCell"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("text")).toString(), QStringLiteral("笔记本"));
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已修改第 1 张表格第 2 行第 1 列，可以撤销。"));
        QTRY_VERIFY(panel->lastMessageText().contains(QStringLiteral("已改好 1 处")));
        QCOMPARE(engine.lastCommandName, QStringLiteral("table.setCell")); // no format.cells
        QTRY_VERIFY(!panel->working());

        // Two tables and the cursor in neither: ask which one; "第 2 张表格" says it.
        engine.outlineValue = {{QStringLiteral("tables"), 2}, {QStringLiteral("paragraphs"), 3}};
        panel->submitText(QStringLiteral("把表格的表头加粗"));
        QTRY_VERIFY(panel->lastMessageText().contains(QStringLiteral("请先把光标点进")));
        QTRY_VERIFY(!panel->working());
        // The program reads "表头加粗" itself: a header that is bold already needs nothing...
        const auto boldCell = [](const QString& text) { return QJsonObject{{QStringLiteral("text"), text}, {QStringLiteral("bold"), true}}; };
        engine.tableValue = {{QStringLiteral("table"), 2}, {QStringLiteral("rows"), QJsonArray{
            QJsonArray{boldCell(QStringLiteral("事项")), boldCell(QStringLiteral("负责人"))},
            QJsonArray{cell(QStringLiteral("布置")), cell(QStringLiteral("孙悦"))}}}};
        fake.chats = {FakeOllama::chat({QStringLiteral(R"({"plan":[]})")})};
        panel->submitText(QStringLiteral("把第 2 张表格的表头加粗"));
        QTRY_VERIFY(panel->lastMessageText().contains(QStringLiteral("没有需要修改的地方")));
        QCOMPARE(engine.lastReadArguments.value(QStringLiteral("table")).toInt(), 2);
        QTRY_VERIFY(!panel->working());
        // ...a plain one is made bold even when the model plans nothing.
        engine.tableValue = {{QStringLiteral("table"), 2}, {QStringLiteral("rows"), QJsonArray{
            QJsonArray{cell(QStringLiteral("事项")), cell(QStringLiteral("负责人"))},
            QJsonArray{cell(QStringLiteral("布置")), cell(QStringLiteral("孙悦"))}}}};
        fake.chats = {FakeOllama::chat({QStringLiteral(R"({"plan":[]})")})};
        panel->submitText(QStringLiteral("把第 2 张表格的表头加粗"));
        // The count from the first plan is 1 too: wait for this plan's list.
        QTRY_VERIFY2(panel->lastMessageText().contains(QStringLiteral("找到 1 处")) && button(QStringLiteral("全部改正")) != nullptr,
                     qPrintable(shown()));
        QCOMPARE(panel->correctionCount(), 1);
        button(QStringLiteral("全部改正"))->click();
        QTRY_COMPARE(engine.lastCommandName, QStringLiteral("format.cells"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("table")).toInt(), 2);
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("rows")).toString(), QStringLiteral("1"));
        QVERIFY(engine.lastCommandArguments.value(QStringLiteral("bold")).toBool());
        engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已设置 2 个单元格的格式，可以撤销。"));
        QTRY_VERIFY(panel->lastMessageText().contains(QStringLiteral("已改好 1 处")));
    }

    void reworkThatChangesNothingIsTriedAgainThenExplained()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools")})});
        fake.chats = {FakeOllama::chat({QStringLiteral("原来的句子。")}),
                      FakeOllama::chat({QStringLiteral("扩写以后更加详细、具体而充实的句子。")}),
                      FakeOllama::chat({QStringLiteral("原来的句子。")}),
                      FakeOllama::chat({QStringLiteral("原来的句子！")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        ControlledEngine engine;
        engine.answerReads = true;
        engine.selectionText = QStringLiteral("原来的句子。");
        engine.documentText = QStringLiteral("原来的句子。");
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        const auto replaceButtons = [panel] {
            int count = 0;
            for (QPushButton* candidate : panel->findChildren<QPushButton*>())
                count += candidate->text() == QStringLiteral("替换原文") ? 1 : 0;
            return count;
        };
        const auto userText = [&fake](int request) {
            return fake.chatRequests().at(request).value(QStringLiteral("messages")).toArray().last().toObject()
                .value(QStringLiteral("content")).toString();
        };

        // The original came back: tried once more, told plainly, and only that result is offered.
        panel->submitText(QStringLiteral("详细一定啊"));
        QTRY_COMPARE(replaceButtons(), 1);
        QCOMPARE(fake.chatRequests().size(), 2);
        QVERIFY(userText(0).contains(QStringLiteral("扩写到")) && userText(0).contains(QStringLiteral("【原文开始】\n原来的句子。")));
        QVERIFY(userText(1).contains(QStringLiteral("明显不同")));
        QCOMPARE(panel->lastMessageText(), QStringLiteral("扩写以后更加详细、具体而充实的句子。"));
        QTRY_VERIFY(!panel->working());

        // Twice the original: said so, with 重试, and nothing new to replace.
        panel->submitText(QStringLiteral("详细一定啊"));
        QTRY_VERIFY(panel->lastMessageText().contains(QStringLiteral("两次都没有真正改动")));
        QCOMPARE(fake.chatRequests().size(), 4);
        QCOMPARE(replaceButtons(), 1);
        QTRY_VERIFY(!panel->working());
        QCOMPARE(engine.editorCommandCalls, 0);
    }

    void conversationsStartOverKeepHistoryAndFollowTheDocument()
    {
        QTemporaryDir folder;
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools")})});
        fake.chats = {FakeOllama::chat({QStringLiteral("第一次的回答。")}),
                      FakeOllama::chat({QStringLiteral("第二次的回答。")}),
                      FakeOllama::chat({QStringLiteral("接着第一次聊。")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        ControlledEngine engine;
        engine.answerReads = true;
        engine.documentText = QStringLiteral("正文。");
        openoffice::MainWindow window(&engine);
        window.setConversationFolder(folder.path());
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        const auto said = [&fake](int request) {
            QString text;
            for (const QJsonValue& message : fake.chatRequests().at(request).value(QStringLiteral("messages")).toArray())
                if (message.toObject().value(QStringLiteral("role")).toString() != QStringLiteral("system"))
                    text += message.toObject().value(QStringLiteral("content")).toString() + QLatin1Char('\n');
            return text;
        };
        const auto cardButton = [panel](const QString& id, const QString& text) -> QPushButton* {
            for (QWidget* card : panel->findChildren<QWidget*>(QStringLiteral("assistantConversation"))) {
                if (card->isHidden() || card->property("conversationId").toString() != id) continue;
                for (QPushButton* button : card->findChildren<QPushButton*>())
                    if (button->text() == text) return button;
            }
            return nullptr;
        };

        const QString firstId = window.conversation().id;
        panel->submitText(QStringLiteral("第一个问题"));
        QTRY_COMPARE(panel->lastMessageText(), QStringLiteral("第一次的回答。"));
        QTRY_VERIFY(!panel->working());
        QCOMPARE(window.conversation().messages.size(), 2);
        QCOMPARE(window.conversation().title, QStringLiteral("第一个问题"));
        QCOMPARE(window.conversation().documentName, QStringLiteral("text-table.docx"));
        QVERIFY(QFileInfo::exists(QDir(folder.path()).filePath(firstId + QStringLiteral(".json"))));

        // /新对话: the bubbles and what the AI remembers start over; the old one is kept.
        panel->submitText(QStringLiteral("/新对话"));
        QTRY_VERIFY(!panel->working()); // the quick actions read the selection again
        const QString secondId = window.conversation().id;
        QVERIFY(secondId != firstId);
        QVERIFY(window.conversation().messages.isEmpty());
        QVERIFY(panel->lastMessageText().isEmpty());
        panel->submitText(QStringLiteral("第二个问题"));
        QTRY_COMPARE(panel->lastMessageText(), QStringLiteral("第二次的回答。"));
        QTRY_VERIFY(!panel->working());
        QVERIFY(said(1).contains(QStringLiteral("第二个问题")));
        QVERIFY(!said(1).contains(QStringLiteral("第一个问题")));

        // 历史: newest first, with the work file; the current one is marked.
        panel->submitText(QStringLiteral("/历史"));
        QVERIFY(panel->conversationListShown());
        QStringList rows = panel->conversationListTexts();
        QCOMPARE(rows.size(), 2);
        QVERIFY2(rows[0].contains(QStringLiteral("第二个问题")) && rows[0].contains(QStringLiteral("当前对话"))
                     && rows[0].contains(QStringLiteral("工作文件：text-table.docx")), qPrintable(rows[0]));
        QVERIFY(rows[1].contains(QStringLiteral("第一个问题")) && !rows[1].contains(QStringLiteral("当前对话")));
        QVERIFY(!cardButton(secondId, QStringLiteral("打开"))); // already open

        // Opening the first brings its bubbles back, and the AI continues it.
        cardButton(firstId, QStringLiteral("打开"))->click();
        QCOMPARE(window.conversation().id, firstId);
        QVERIFY(!panel->conversationListShown());
        QCOMPARE(panel->lastMessageText(), QStringLiteral("第一次的回答。")); // same document: no work-file note
        QTRY_VERIFY(!panel->working());
        panel->submitText(QStringLiteral("接着问"));
        QTRY_COMPARE(panel->lastMessageText(), QStringLiteral("接着第一次聊。"));
        QTRY_VERIFY(!panel->working());
        QVERIFY(said(2).contains(QStringLiteral("第一个问题")) && said(2).contains(QStringLiteral("第一次的回答。")));
        QVERIFY(!said(2).contains(QStringLiteral("第二个问题")));
        QCOMPARE(window.conversation().messages.size(), 4);

        // Rename in place, then delete with two clicks.
        panel->submitText(QStringLiteral("/历史"));
        cardButton(secondId, QStringLiteral("重命名"))->click();
        auto* editor = panel->findChild<QLineEdit*>(QStringLiteral("assistantRename"));
        QVERIFY(editor);
        editor->setText(QStringLiteral("改过的名字"));
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(panel->conversationListTexts().join(QLatin1Char('\n')).contains(QStringLiteral("改过的名字")));
        cardButton(secondId, QStringLiteral("删除"))->click();
        QCOMPARE(panel->conversationListTexts().size(), 2); // the first click only asks
        cardButton(secondId, QStringLiteral("确认删除"))->click();
        QTRY_COMPARE(panel->conversationListTexts().size(), 1);
        QVERIFY(!QFileInfo::exists(QDir(folder.path()).filePath(secondId + QStringLiteral(".json"))));

        // Another document begins its own conversation; 另存为 does not.
        panel->hideConversationList();
        const int opened = engine.openCalls;
        openFixture(window, engine, "values-formula.xlsx");
        QCOMPARE(engine.openCalls, opened + 1);
        QTRY_VERIFY(window.conversation().id != firstId); // the completion arrives a moment later
        QVERIFY(window.conversation().messages.isEmpty());
        QCOMPARE(window.conversation().documentName, QStringLiteral("values-formula.xlsx"));
        QVERIFY(panel->lastMessageText().isEmpty());

        // A new start of the program begins a new conversation; the old ones are still there.
        ControlledEngine engineAgain;
        openoffice::MainWindow again(&engineAgain);
        again.setConversationFolder(folder.path());
        QVERIFY(again.conversation().messages.isEmpty());
        QVERIFY(again.conversation().id != firstId);
        openoffice::ConversationStore store(folder.path());
        QCOMPARE(store.list().size(), 1);
        QCOMPARE(store.list().first().id, firstId);
        QCOMPARE(store.list().first().messages.size(), 4);
    }

    void slashListOffersCommandsAndCompletesThem()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        window.show();
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        openFixture(window, engine, "text-table.docx");
        QTRY_VERIFY(panel->isVisible());
        panel->expand();
        auto* input = panel->findChild<QLineEdit*>(QStringLiteral("assistantInput"));
        QVERIFY(input);
        QTest::keyClicks(input, QStringLiteral("/"));
        const QStringList offered = panel->slashSuggestions();
        QVERIFY2(offered.size() >= 10, qPrintable(offered.join(QLatin1Char(','))));
        QCOMPARE(offered.mid(0, 2), (QStringList{QStringLiteral("新对话"), QStringLiteral("历史")}));
        QVERIFY(offered.contains(QStringLiteral("保存")) && offered.contains(QStringLiteral("插入")));
        input->insert(QStringLiteral("插")); // typed text that is not ASCII
        QCOMPARE(panel->slashSuggestions(), QStringList{QStringLiteral("插入")});
        QTest::keyClick(input, Qt::Key_Return); // a command that takes text: completed, not run
        QCOMPARE(input->text(), QStringLiteral("/插入 "));
        QVERIFY(panel->slashSuggestions().isEmpty());

        input->clear();
        QTest::keyClicks(input, QStringLiteral("/"));
        QTest::keyClick(input, Qt::Key_Down); // 历史
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(panel->conversationListShown()); // run at once
        QVERIFY(input->text().isEmpty());

        QTest::keyClicks(input, QStringLiteral("/"));
        QTest::keyClick(input, Qt::Key_Escape); // closes the list, not the capsule
        QVERIFY(panel->slashSuggestions().isEmpty());
        QVERIFY(panel->expanded());
        input->clear();
        QTest::keyClicks(input, QStringLiteral("/zz"));
        QVERIFY(panel->slashSuggestions().isEmpty());
        input->clear();
        input->setText(QStringLiteral("你好 /"));
        QVERIFY(panel->slashSuggestions().isEmpty()); // only at the start
    }

    // A JSON-RPC request to the MCP server; returns the HTTP status and the body.
    static QPair<int, QJsonObject> mcpPost(openoffice::McpServer& server, const QJsonObject& message, const QByteArray& token,
                                           const QList<QPair<QByteArray, QByteArray>>& headers = {})
    {
        QNetworkAccessManager network;
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(server.port())));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        if (!token.isEmpty()) request.setRawHeader("Authorization", "Bearer " + token);
        for (const auto& header : headers) request.setRawHeader(header.first, header.second);
        QNetworkReply* reply = network.post(request, QJsonDocument(message).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(300000, &loop, &QEventLoop::quit);
        loop.exec();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        return {status, body};
    }

    static QJsonObject rpc(int id, const QString& method, const QJsonObject& params = {})
    {
        return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
                {QStringLiteral("method"), method}, {QStringLiteral("params"), params}};
    }

    static QJsonObject mcpCall(openoffice::McpServer& server, int id, const QString& tool, const QJsonObject& arguments = {})
    {
        return mcpPost(server, rpc(id, QStringLiteral("tools/call"), {{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), arguments}}),
                       server.token().toLatin1()).second.value(QStringLiteral("result")).toObject();
    }

    void mcpServerServesTheCommandsOnlyToThisComputer()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        openFixture(window, engine, "text-table.docx");
        openoffice::McpServer server(&window, false);
        QString error;
        QVERIFY2(server.listen(0, &error), qPrintable(error));
        const QByteArray token = server.token().toLatin1();
        QCOMPARE(token.size(), 64);

        // Refused: no token, a wrong one, a web page's origin. A local origin is fine.
        QCOMPARE(mcpPost(server, rpc(1, QStringLiteral("ping")), {}).first, 401);
        QCOMPARE(mcpPost(server, rpc(1, QStringLiteral("ping")), QByteArray(64, 'a')).first, 401);
        QCOMPARE(mcpPost(server, rpc(1, QStringLiteral("ping")), token, {{"Origin", "http://evil.example"}}).first, 403);
        QCOMPARE(mcpPost(server, rpc(1, QStringLiteral("ping")), token,
                         {{"Origin", "http://127.0.0.1:" + QByteArray::number(server.port())}}).first, 200);

        const auto initialize = mcpPost(server, rpc(1, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")}}), token);
        QCOMPARE(initialize.first, 200);
        const QJsonObject info = initialize.second.value(QStringLiteral("result")).toObject();
        QCOMPARE(info.value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-06-18"));
        QCOMPARE(info.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("open-office"));
        QVERIFY(info.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tools")));
        QCOMPARE(mcpPost(server, {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("notifications/initialized")}},
                         token).first, 202);

        QStringList names;
        for (const QJsonValue& tool : mcpPost(server, rpc(2, QStringLiteral("tools/list")), token).second.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("tools")).toArray())
            names << tool.toObject().value(QStringLiteral("name")).toString();
        QVERIFY(names.contains(QStringLiteral("table.setCell")) && names.contains(QStringLiteral("format.cells"))
                && names.contains(QStringLiteral("document.outline")));
        QVERIFY(!names.contains(QStringLiteral("test.ask"))); // test tools only when asked for

        // A host command, then an editor command the editor answers a moment later.
        const QJsonObject status = mcpCall(server, 3, QStringLiteral("document.status"));
        QVERIFY(!status.value(QStringLiteral("isError")).toBool());
        QVERIFY(status.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("ok")).toBool());
        QTimer::singleShot(300, &window, [&engine] {
            engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已读取第 1 张表格（2 行 2 列）。"),
                                   {{QStringLiteral("rowsCount"), 2}});
        });
        const QJsonObject table = mcpCall(server, 4, QStringLiteral("table.read"), {{QStringLiteral("table"), 1}});
        QCOMPARE(engine.lastCommandName, QStringLiteral("table.read"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("table")).toInt(), 1);
        QVERIFY(!table.value(QStringLiteral("isError")).toBool());
        QCOMPARE(table.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("value")).toObject()
                     .value(QStringLiteral("rowsCount")).toInt(), 2);
        QVERIFY(table.value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString()
                    .contains(QStringLiteral("rowsCount")));

        // Mistakes come back as tool errors; unknown methods as JSON-RPC errors.
        const QJsonObject wrong = mcpCall(server, 5, QStringLiteral("table.setCell"), {{QStringLiteral("table"), 0}});
        QVERIFY(wrong.value(QStringLiteral("isError")).toBool());
        QCOMPARE(mcpCall(server, 6, QStringLiteral("test.ask"), {{QStringLiteral("text"), QStringLiteral("x")}})
                     .value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("code")).toString(),
                 QStringLiteral("unknown_tool"));
        QCOMPARE(mcpPost(server, rpc(7, QStringLiteral("resources/list")), token).second.value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toInt(), -32601);
    }

    void mcpTestToolsDriveTheAssistantLikeAPerson()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools")})});
        fake.chats = {FakeOllama::chat({QStringLiteral("更加详细具体的一句话，包含原来的意思。")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        ControlledEngine engine;
        engine.answerReads = true;
        engine.selectionText = QStringLiteral("原来的句子。");
        engine.documentText = QStringLiteral("原来的句子。");
        openoffice::MainWindow window(&engine);
        openFixture(window, engine, "text-table.docx");
        openoffice::McpServer server(&window, true);
        QString error;
        QVERIFY2(server.listen(0, &error), qPrintable(error));

        const QJsonObject asked = mcpCall(server, 1, QStringLiteral("test.ask"), {{QStringLiteral("text"), QStringLiteral("详细点")}})
                                      .value(QStringLiteral("structuredContent")).toObject();
        QVERIFY2(asked.value(QStringLiteral("ok")).toBool(), QJsonDocument(asked).toJson().constData());
        QVERIFY(asked.value(QStringLiteral("buttons")).toArray().contains(QStringLiteral("替换原文")));
        const QJsonArray said = asked.value(QStringLiteral("said")).toArray();
        QCOMPARE(said.first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("详细点"));
        QCOMPARE(said.last().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("更加详细具体的一句话，包含原来的意思。"));

        // Pressing a reply button runs its command; the editor answers a moment later.
        QTimer::singleShot(500, &window, [&engine] {
            engine.completeCommand(engine.lastCommandId, true, QStringLiteral("已替换选中的文字，可以撤销。"));
        });
        const QJsonObject pressed = mcpCall(server, 2, QStringLiteral("test.press"), {{QStringLiteral("button"), QStringLiteral("替换原文")}})
                                        .value(QStringLiteral("structuredContent")).toObject();
        QVERIFY2(pressed.value(QStringLiteral("ok")).toBool(), QJsonDocument(pressed).toJson().constData());
        QCOMPARE(engine.lastCommandName, QStringLiteral("selection.replace"));
        QCOMPARE(engine.lastCommandArguments.value(QStringLiteral("text")).toString(), QStringLiteral("更加详细具体的一句话，包含原来的意思。"));

        const QJsonObject state = mcpCall(server, 3, QStringLiteral("test.state")).value(QStringLiteral("structuredContent")).toObject();
        QVERIFY(state.value(QStringLiteral("documentOpened")).toBool());
        QCOMPARE(state.value(QStringLiteral("ai")).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QCOMPARE(mcpCall(server, 4, QStringLiteral("test.quick"), {{QStringLiteral("id"), QStringLiteral("no-such")}})
                     .value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("code")).toString(),
                 QStringLiteral("no_quick_action"));
        QCOMPARE(mcpCall(server, 5, QStringLiteral("test.press"), {{QStringLiteral("button"), QStringLiteral("不存在的按钮")}})
                     .value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("code")).toString(),
                 QStringLiteral("no_button"));
    }

    void relocationPlanChecksTheChosenFolder()
    {
        QTemporaryDir root;
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("apps/OpenOffice/models")));
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("other/OpenOffice")));
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("empty")));
        const QString source = root.filePath(QStringLiteral("apps/OpenOffice"));
        QString error;
        const openoffice::RelocationPlan plan = openoffice::planRelocation(source, root.filePath(QStringLiteral("empty")), &error);
        QVERIFY2(plan.valid(), qPrintable(error));
        QCOMPARE(plan.target, QDir::cleanPath(root.filePath(QStringLiteral("empty/OpenOffice"))));
        QVERIFY(plan.sameVolume);
        QVERIFY(QDir(root.filePath(QStringLiteral("empty"))).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden));
        const auto refused = [&](const QString& chosen, const QString& reason) {
            QString why;
            return !openoffice::planRelocation(source, chosen, &why).valid() && why.contains(reason);
        };
        QVERIFY(refused(root.filePath(QStringLiteral("apps")), QStringLiteral("已经在这个位置")));
        QVERIFY(refused(root.filePath(QStringLiteral("apps/OpenOffice/models")), QStringLiteral("自己的文件夹里面")));
        QVERIFY(refused(source, QStringLiteral("自己的文件夹里面")));
        QVERIFY(refused(root.filePath(QStringLiteral("other")), QStringLiteral("已经存在")));
        QVERIFY(refused(root.filePath(QStringLiteral("missing")), QStringLiteral("不是一个文件夹")));
    }

    void relocationHelperMovesTheFolderShortcutsAndUninstallEntry()
    {
        // Same drive (a rename) here; another drive (a verified copy) when the build folder is on one.
        runRelocationHelper(QDir::tempPath());
        if (QTest::currentTestFailed()) return;
        if (QStorageInfo(QDir::tempPath()).rootPath().compare(QStorageInfo(QDir::currentPath()).rootPath(), Qt::CaseInsensitive) != 0)
            runRelocationHelper(QDir::currentPath());
    }

    void relocateAsksConfirmsAndClosesWithThePlan()
    {
        QTemporaryDir destination;
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        window.show();
        QString offered;
        window.setRelocationFolderChooser([&](const QString& start) {
            offered = start;
            return QString();
        });
        window.relocateApplication(); // cancelled in the folder dialog
        QCOMPARE(offered, QFileInfo(QCoreApplication::applicationDirPath()).absolutePath());
        QVERIFY(!window.relocationPlan().valid());

        window.setRelocationFolderChooser([&](const QString&) { return destination.path(); });
        answerRelocation(QStringLiteral("取消"));
        window.relocateApplication();
        QVERIFY(!window.relocationPlan().valid());
        QVERIFY(window.isVisible());

        answerRelocation(QStringLiteral("迁移并重新打开"));
        window.relocateApplication();
        QVERIFY(window.relocationPlan().valid());
        QCOMPARE(window.relocationPlan().target,
                 QDir(destination.path()).filePath(QFileInfo(QCoreApplication::applicationDirPath()).fileName()));
        QTRY_VERIFY(!window.isVisible()); // it closes; main() starts the helper after the program exits
        QVERIFY(!QFileInfo::exists(window.relocationPlan().target)); // nothing moved yet
    }

    void assistantHostCommandsUseTheWindowsOwnFlows()
    {
        ControlledEngine engine;
        openoffice::MainWindow window(&engine);
        auto* panel = window.findChild<openoffice::AssistantBall*>(QStringLiteral("assistantBall"));
        panel->submitText(QStringLiteral("/状态"));
        QCOMPARE(panel->lastMessageText(), QStringLiteral("当前没有打开的文档。"));
        panel->submitText(QStringLiteral("/新建"));
        QCOMPARE(engine.createCalls, 1);
        QCOMPARE(engine.lastTitle, QStringLiteral("文档1.docx"));
        QVERIFY(panel->working());
        engine.complete(engine.lastId, openoffice::EngineOperation::Create, true, QString());
        QTRY_VERIFY(!panel->working());
        QCOMPARE(panel->lastMessageText(), QStringLiteral("test complete"));
        panel->submitText(QStringLiteral("/状态"));
        QVERIFY(panel->lastMessageText().startsWith(QStringLiteral("文档1.docx")));

        window.setSaveDestinationChooser([](const QString&, openoffice::DocumentKind) { return QString(); });
        panel->submitText(QStringLiteral("/保存"));
        QCOMPARE(engine.saveCalls, 0); // the person cancelled the save dialog
        QVERIFY(panel->lastMessageText().startsWith(QStringLiteral("没有开始")));
        QVERIFY(!panel->working());

        // The document is read first (an editor answers reads), then the missing AI is explained.
        engine.answerReads = true;
        const int before = panel->messageCount();
        panel->submitText(QStringLiteral("帮我写一段总结"));
        // No local AI here: the answer is a setup card, never a made-up reply.
        QTRY_VERIFY_WITH_TIMEOUT(panel->messageCount() == before + 2, 8000);
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("Ollama")));
        QVERIFY(!panel->working());
        QCOMPARE(engine.editorCommandCalls, 0);
        panel->submitText(QStringLiteral("/命令"));
        QVERIFY(panel->lastMessageText().contains(QStringLiteral("/插入 文字")));
    }

    void failedSaveBeforeCloseRestoresUsableEditorAndCancelsCloseIntent()
    {
        ControlledEngine engine;
        engine.closeHandshake = true;
        openoffice::MainWindow window(&engine);
        window.show();
        window.selectDocument(QStringLiteral(PROJECT_FIXTURES_DIR "/text-table.docx"));
        window.findChild<QAction*>(QStringLiteral("openDocumentAction"))->trigger();
        engine.complete(engine.lastId, openoffice::EngineOperation::Open, true, engine.lastPath);
        QTRY_VERIFY(window.documentOpened());
        engine.changeModified(true);
        auto* title = window.findChild<QLabel*>(QStringLiteral("editorDocumentTitleLabel"));
        QTRY_VERIFY(title->text().contains(QStringLiteral("未保存")));

        answerClosePrompt(QMessageBox::Save);
        QVERIFY(!window.close());
        engine.complete(engine.lastId, openoffice::EngineOperation::Save, false, QString());
        QTRY_COMPARE(window.findChild<QLabel*>(QStringLiteral("activityStatusLabel"))->text(),
                     QStringLiteral("test failed"));
        QVERIFY(engine.view->isEnabled());
        QVERIFY(window.isVisible());
        QVERIFY(engine.documentModified());
        QVERIFY(title->text().contains(QStringLiteral("未保存")));
        QVERIFY(window.findChild<QAction*>(QStringLiteral("saveDocumentAction"))->isEnabled());
        engine.changeModified(false);
        QCoreApplication::processEvents();
        QCOMPARE(engine.closeCalls, 0);
        QVERIFY(window.isVisible());
    }

private:
    QTemporaryDir m_settings; // its settings.ini: a file QSettings may replace (a QTemporaryFile may not be)
};

QTEST_MAIN(NativeUiTest)
#include "native_ui_test.moc"
