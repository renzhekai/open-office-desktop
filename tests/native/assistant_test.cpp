#include "assistant/assistant_commands.h"
#include "assistant/assistant_tasks.h"
#include "assistant/engine_client.h"
#include "assistant/local_assistant.h"
#include "assistant/local_engine.h"
#include "assistant/markdown_html.h"
#include "assistant/model_catalog.h"
#include "assistant/model_downloader.h"
#include "assistant/ollama_client.h"
#include "fake_ollama.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace openoffice;

class AssistantTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // The Ollama tests below talk to a fake Ollama; the built-in engine tests switch back themselves.
        qputenv("OPEN_OFFICE_AI_BACKEND", "ollama");
    }

    void catalogNamesAreUniqueAndDescribed()
    {
        QSet<QString> names;
        for (const CommandSpec& spec : commandCatalog()) {
            QVERIFY2(!names.contains(spec.name), qPrintable(spec.name));
            names.insert(spec.name);
            QVERIFY(!spec.title.isEmpty());
            QVERIFY(!spec.description.isEmpty());
            QVERIFY(findCommand(spec.name) == &spec);
        }
        QVERIFY(findCommand(QStringLiteral("file.delete")) == nullptr);
        // Content changes run in the editor; only the editor can undo them.
        QVERIFY(findCommand(QStringLiteral("text.insert"))->target == CommandTarget::Editor);
        QVERIFY(findCommand(QStringLiteral("text.insert"))->changesDocument);
        QVERIFY(!findCommand(QStringLiteral("selection.getText"))->changesDocument);
    }

    void catalogJsonIsAStrictToolSchema()
    {
        const QJsonArray tools = commandCatalogJson();
        QCOMPARE(tools.size(), commandCatalog().size());
        QJsonObject insert;
        for (const QJsonValue& tool : tools)
            if (tool.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("text.insert"))
                insert = tool.toObject();
        const QJsonObject schema = insert.value(QStringLiteral("inputSchema")).toObject();
        QCOMPARE(schema.value(QStringLiteral("type")).toString(), QStringLiteral("object"));
        QCOMPARE(schema.value(QStringLiteral("additionalProperties")).toBool(true), false);
        QCOMPARE(schema.value(QStringLiteral("required")).toArray(), QJsonArray{QStringLiteral("text")});
        const QJsonObject text = schema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("text")).toObject();
        QCOMPARE(text.value(QStringLiteral("maxLength")).toInt(), 20000);
        QCOMPARE(insert.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool(true), false);
    }

    void validationRejectsAnythingOffSchema_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QJsonObject>("arguments");
        QTest::addColumn<QString>("code");
        QTest::newRow("unknown command") << "file.delete" << QJsonObject{} << "unknown_command";
        QTest::newRow("missing text") << "text.insert" << QJsonObject{} << "missing_argument";
        QTest::newRow("empty text") << "text.insert" << QJsonObject{{"text", ""}} << "invalid_argument";
        QTest::newRow("number text") << "text.insert" << QJsonObject{{"text", 5}} << "invalid_argument";
        QTest::newRow("too long") << "text.insert" << QJsonObject{{"text", QString(20001, QLatin1Char('a'))}}
                                  << "argument_too_long";
        QTest::newRow("extra argument") << "edit.undo" << QJsonObject{{"steps", "2"}} << "unknown_argument";
        QTest::newRow("bad kind") << "document.create" << QJsonObject{{"kind", "pdf"}} << "invalid_argument";
    }

    void validationRejectsAnythingOffSchema()
    {
        QFETCH(QString, name);
        QFETCH(QJsonObject, arguments);
        QFETCH(QString, code);
        const CommandOutcome outcome = validateCommand({name, arguments, CommandSource::Assistant});
        QVERIFY(!outcome.ok);
        QCOMPARE(outcome.code, code);
        QVERIFY(!outcome.message.isEmpty());
    }

    void validationAcceptsWellFormedCommands()
    {
        QVERIFY(validateCommand({QStringLiteral("text.insert"), {{QStringLiteral("text"), QString(20000, QLatin1Char('x'))}},
                                 CommandSource::User}).ok);
        QVERIFY(validateCommand({QStringLiteral("document.create"), {{QStringLiteral("kind"), QStringLiteral("text")}},
                                 CommandSource::External}).ok);
        QVERIFY(validateCommand({QStringLiteral("edit.redo"), {}, CommandSource::Unknown}).ok);
    }

    void slashCommandsParseChineseEnglishAndFullWidth()
    {
        QString error;
        auto insert = parseSlashCommand(QStringLiteral("  /插入 你好  世界\n第二行 "), &error);
        QVERIFY(insert.has_value());
        QCOMPARE(insert->name, QStringLiteral("text.insert"));
        QCOMPARE(insert->arguments.value(QStringLiteral("text")).toString(), QStringLiteral("你好  世界\n第二行"));
        QVERIFY(insert->source == CommandSource::User);
        QVERIFY(error.isEmpty());

        QCOMPARE(parseSlashCommand(QStringLiteral("/UNDO"), &error)->name, QStringLiteral("edit.undo"));
        QCOMPARE(parseSlashCommand(QStringLiteral("／重做"), &error)->name, QStringLiteral("edit.redo"));
        const auto create = parseSlashCommand(QStringLiteral("/新建"), &error);
        QCOMPARE(create->arguments.value(QStringLiteral("kind")).toString(), QStringLiteral("text"));
        QCOMPARE(parseSlashCommand(QStringLiteral("/new 表格"), &error)->arguments.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("spreadsheet"));
        for (const auto& command : {parseSlashCommand(QStringLiteral("/命令"), &error), parseSlashCommand(QStringLiteral("/状态"), &error),
                                    parseSlashCommand(QStringLiteral("/保存"), &error), parseSlashCommand(QStringLiteral("/选中"), &error)})
            QVERIFY(command.has_value() && validateCommand(*command).ok);
    }

    void slashErrorsAndPlainTextAreDistinguished()
    {
        QString error;
        QVERIFY(!parseSlashCommand(QStringLiteral("帮我写一段总结"), &error).has_value());
        QVERIFY(error.isEmpty()); // plain text: not a command, not an error
        QVERIFY(!parseSlashCommand(QStringLiteral("/删除"), &error).has_value());
        QVERIFY(error.contains(QStringLiteral("/删除")));
        QVERIFY(!parseSlashCommand(QStringLiteral("/插入"), &error).has_value());
        QVERIFY(!error.isEmpty());
        QVERIFY(!parseSlashCommand(QStringLiteral("/撤销 3"), &error).has_value());
        QVERIFY(!error.isEmpty());
        QVERIFY(!parseSlashCommand(QStringLiteral("/新建 图片"), &error).has_value());
        QVERIFY(!error.isEmpty());
    }

    void sourcesAreNeverGuessed()
    {
        QCOMPARE(sourceName(CommandSource::User), QStringLiteral("user"));
        QCOMPARE(sourceName(CommandSource::Assistant), QStringLiteral("assistant"));
        QCOMPARE(sourceName(CommandSource::External), QStringLiteral("external"));
        QCOMPARE(sourceName(CommandSource::Unknown), QStringLiteral("unknown"));
        QVERIFY(AssistantCommand{}.source == CommandSource::Unknown);
    }

    // --- local AI (Ollama) ---

    void ollamaAddressStaysOnThisComputer()
    {
        ScopedEnvironment own("OPEN_OFFICE_OLLAMA_URL", QByteArray());
        ScopedEnvironment host("OLLAMA_HOST", QByteArray());
        QCOMPARE(OllamaClient::defaultBaseUrl(), QUrl(QStringLiteral("http://127.0.0.1:11434")));
        qputenv("OLLAMA_HOST", "0.0.0.0"); // Ollama's "listen everywhere" is reached locally
        QCOMPARE(OllamaClient::defaultBaseUrl(), QUrl(QStringLiteral("http://127.0.0.1:11434")));
        qputenv("OLLAMA_HOST", "0.0.0.0:8080");
        QCOMPARE(OllamaClient::defaultBaseUrl(), QUrl(QStringLiteral("http://127.0.0.1:8080")));
        qputenv("OLLAMA_HOST", "192.168.1.20:11434"); // another computer: never
        QCOMPARE(OllamaClient::defaultBaseUrl(), QUrl(QStringLiteral("http://127.0.0.1:11434")));
        qputenv("OPEN_OFFICE_OLLAMA_URL", "http://example.com:11434");
        QCOMPARE(OllamaClient::defaultBaseUrl(), QUrl(QStringLiteral("http://127.0.0.1:11434")));
        qputenv("OPEN_OFFICE_OLLAMA_URL", "localhost:12000");
        QCOMPARE(OllamaClient::defaultBaseUrl(), QUrl(QStringLiteral("http://localhost:12000")));
    }

    void modelChoiceFitsThisComputer()
    {
        const qint64 gb = 1LL << 30;
        const auto model = [](const QString& name, double gigabytes, const QString& family, bool tools) {
            LocalModel result;
            result.name = name;
            result.bytes = static_cast<qint64>(gigabytes * (1LL << 30));
            result.family = family;
            result.tools = tools;
            return result;
        };
        const QList<LocalModel> installed{model(QStringLiteral("granite4.1:3b"), 2.1, QStringLiteral("granite"), true),
                                          model(QStringLiteral("gpt-oss:20b"), 13.8, QStringLiteral("gptoss"), true),
                                          model(QStringLiteral("llama3:8b"), 4.7, QStringLiteral("llama"), false),
                                          model(QStringLiteral("nomic-embed-text:latest"), 0.3, QStringLiteral("nomic-bert"), false)};
        const LocalModel qwen8 = model(QStringLiteral("qwen3:8b"), 5.2, QStringLiteral("qwen3"), true);
        const LocalModel qwen4 = model(QStringLiteral("qwen3:4b"), 2.5, QStringLiteral("qwen3"), true);
        // This computer: 32 GB memory, a 12 GB graphics card. gpt-oss does not fit
        // the card and runs mostly on the processor (measured under 1 token/s).
        QCOMPARE(pickInstalledModel(installed, 32 * gb, 12 * gb), QStringLiteral("granite4.1:3b"));
        QCOMPARE(pickInstalledModel(installed + QList<LocalModel>{qwen8}, 32 * gb, 12 * gb), QStringLiteral("qwen3:8b"));
        // A 24 GB card holds gpt-oss: then its size wins over the small model.
        QCOMPARE(pickInstalledModel(installed, 64 * gb, 24 * gb), QStringLiteral("gpt-oss:20b"));
        // No graphics card: small models are the fast ones.
        QCOMPARE(pickInstalledModel(installed + QList<LocalModel>{qwen8, qwen4}, 32 * gb, 0), QStringLiteral("qwen3:4b"));
        QCOMPARE(pickInstalledModel(installed, 16 * gb), QStringLiteral("granite4.1:3b")); // 13.8 GB would not fit
        QCOMPARE(pickInstalledModel({installed[2], installed[3]}, 16 * gb), QStringLiteral("llama3:8b")); // chat only
        QCOMPARE(pickInstalledModel({installed[3]}, 16 * gb), QString()); // embeddings cannot chat
        QCOMPARE(pickInstalledModel({}, 16 * gb), QString());

        QVERIFY(runsSlowly(installed[1], 12 * gb));
        QVERIFY(!runsSlowly(qwen8, 12 * gb));
        QVERIFY(runsSlowly(qwen8, 0)); // on the processor
        QVERIFY(!runsSlowly(qwen4, 0));

        QCOMPARE(recommendDownload(32 * gb, 12 * gb).name, QStringLiteral("qwen3:8b")); // this computer
        QCOMPARE(recommendDownload(16 * gb, 6 * gb).name, QStringLiteral("qwen3:4b"));
        QCOMPARE(recommendDownload(static_cast<qint64>(15.8 * gb), 0).name, QStringLiteral("qwen3:4b")); // no card
        QCOMPARE(recommendDownload(static_cast<qint64>(7.8 * gb)).name, QStringLiteral("qwen3:4b")); // an "8 GB" laptop
        QCOMPARE(recommendDownload(4 * gb).name, QStringLiteral("qwen3:1.7b"));
        QCOMPARE(recommendDownload(64 * gb).name, QStringLiteral("qwen3:4b"));
        for (const qint64 memory : {4 * gb, 8 * gb, 16 * gb, 32 * gb}) {
            for (const qint64 video : {0LL, 6 * gb, 12 * gb}) {
                // Every recommendation fits and runs quickly, so it is then also picked.
                const ModelRecommendation recommended = recommendDownload(memory, video);
                QVERIFY(recommended.approximateGigabytes * gb < memory * 0.55);
                LocalModel downloaded = model(recommended.name, recommended.approximateGigabytes, QStringLiteral("qwen3"), true);
                QVERIFY2(!runsSlowly(downloaded, video), qPrintable(recommended.name));
                QCOMPARE(pickInstalledModel(installed + QList<LocalModel>{downloaded}, memory, video), recommended.name);
            }
        }
    }

    void failedDownloadsAreExplained()
    {
        const QString fakeIp = QStringLiteral(
            "max retries exceeded: Get \"https://x.r2.cloudflarestorage.com/ollama/...\": redirect target not allowed: "
            "x.r2.cloudflarestorage.com resolves to non-public 198.18.1.56");
        QVERIFY(explainPullError(fakeIp).contains(QStringLiteral("Fake-IP")));
        QVERIFY(explainPullError(fakeIp).contains(QStringLiteral("+.r2.cloudflarestorage.com")));
        QVERIFY(explainPullError(QStringLiteral("write /models/blobs: no space left on device")).contains(QStringLiteral("磁盘")));
        QVERIFY(explainPullError(QStringLiteral("pull model manifest: file does not exist")).contains(QStringLiteral("找不到")));
        QVERIFY(explainPullError(QStringLiteral("dial tcp: i/o timeout")).contains(QStringLiteral("网络")));
        QCOMPARE(explainPullError(QStringLiteral("something else")), QStringLiteral("something else"));
    }

    void stoppingAndReasoningModels()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("gpt-oss:test"), 1.0, QStringLiteral("gptoss"),
                                        {QStringLiteral("completion"), QStringLiteral("tools"), QStringLiteral("thinking")})});
        fake.chats = {FakeOllama::chat({QStringLiteral("{\"corrections\":[]}")}), FakeOllama::chat({QStringLiteral("晚到的回答")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        LocalAssistant assistant;
        assistant.refresh();
        QTRY_VERIFY(assistant.state() == LocalAiState::Ready);
        QList<QStringList> finished;
        connect(&assistant, &LocalAssistant::taskFinished, this,
                [&finished](const QString& id, const QString& text, bool ok, const QString& error) {
            finished.append({id, text, ok ? QStringLiteral("ok") : QStringLiteral("failed"), error});
        });
        // gpt-oss cannot stop reasoning; a schema-constrained answer stalls it, so none is asked for.
        QVERIFY(assistant.runTask(*findTask(QStringLiteral("proofread")), {QStringLiteral("文档内容"), {}, {}}));
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!fake.chatRequests().first().contains(QStringLiteral("format")));
        QCOMPARE(fake.chatRequests().first().value(QStringLiteral("think")).toString(), QStringLiteral("low"));

        // Stop: the task ends at once, as stopped, and the assistant is ready again.
        QVERIFY(assistant.runTask(*findTask(QStringLiteral("polish")), {QStringLiteral("原文"), {}, {}}));
        assistant.cancel();
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished[1].at(2), QStringLiteral("failed"));
        QCOMPARE(finished[1].at(3), QStringLiteral("已停止。"));
        QVERIFY(assistant.state() == LocalAiState::Ready);
    }

    void toolsMirrorTheCommandCatalog()
    {
        const QJsonArray tools = LocalAssistant::tools();
        QCOMPARE(tools.size(), commandCatalog().size() - 1); // listing commands means nothing to a model
        for (const QJsonValue& value : tools) {
            const QJsonObject function = value.toObject().value(QStringLiteral("function")).toObject();
            QCOMPARE(value.toObject().value(QStringLiteral("type")).toString(), QStringLiteral("function"));
            const QString name = function.value(QStringLiteral("name")).toString();
            QVERIFY2(!name.contains(QLatin1Char('.')), qPrintable(name));
            QVERIFY2(findCommand(LocalAssistant::commandName(name)), qPrintable(name));
            QCOMPARE(function.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("type")).toString(),
                     QStringLiteral("object"));
        }
        QCOMPARE(LocalAssistant::commandName(QStringLiteral("text_insert")), QStringLiteral("text.insert"));
        QCOMPARE(LocalAssistant::commandName(QStringLiteral("file_delete")), QStringLiteral("file_delete")); // refused later
    }

    void unreachableOllamaLeadsToSetup()
    {
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", closedOllamaUrl());
        LocalAssistant assistant;
        assistant.refresh();
        QVERIFY(assistant.state() == LocalAiState::Checking);
        QTRY_VERIFY_WITH_TIMEOUT(assistant.state() != LocalAiState::Checking, 8000);
        // Whether the setup card says "install" or "start" depends on this computer.
        QVERIFY(assistant.state() == (LocalAssistant::ollamaAppPath().isEmpty() ? LocalAiState::NotInstalled
                                                                                 : LocalAiState::NotRunning));
        QVERIFY(!assistant.statusText().isEmpty());
        QVERIFY(!assistant.ask(QStringLiteral("你好"), QString())); // nothing is sent anywhere
    }

    void conversationProposesCommandsAndHearsTheOutcome()
    {
        FakeOllama fake;
        QVERIFY(fake.server.isListening());
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools"), QStringLiteral("thinking")}),
                      FakeOllama::model(QStringLiteral("fake-chat:test"), 0.8, QStringLiteral("llama"), {QStringLiteral("completion")}),
                      FakeOllama::model(QStringLiteral("fake-embed:test"), 0.3, QStringLiteral("nomic-bert"), {QStringLiteral("embedding")}),
                      // Forwarded to a cloud service by Ollama: must never be offered.
                      QJsonObject{{QStringLiteral("name"), QStringLiteral("deepseek-v4-pro:cloud")},
                                  {QStringLiteral("remote_host"), QStringLiteral("https://ollama.com:443")},
                                  {QStringLiteral("size"), 300},
                                  {QStringLiteral("capabilities"), QJsonArray{QStringLiteral("tools")}}}});
        fake.chats = {FakeOllama::chat({}, QStringLiteral("text_insert"), {{QStringLiteral("text"), QStringLiteral("你好，世界")}}),
                      FakeOllama::chat({QStringLiteral("已经"), QStringLiteral("插入了。")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());

        LocalAssistant assistant;
        QCOMPARE(assistant.client()->baseUrl().port(), int(fake.server.serverPort()));
        assistant.refresh();
        QTRY_VERIFY(assistant.state() == LocalAiState::Ready);
        QCOMPARE(assistant.model(), QStringLiteral("fake-qwen3:test"));
        QCOMPARE(assistant.models().size(), 3);
        for (const LocalModel& model : assistant.models()) QVERIFY(!model.name.endsWith(QStringLiteral(":cloud")));

        QList<AssistantCommand> proposed;
        QStringList deltas;
        QList<QPair<QString, bool>> replies;
        connect(&assistant, &LocalAssistant::commandProposed, this, [&proposed](const AssistantCommand& command) { proposed.append(command); });
        connect(&assistant, &LocalAssistant::replyDelta, this, [&deltas](const QString& text) { deltas.append(text); });
        connect(&assistant, &LocalAssistant::replyFinished, this,
                [&replies](const QString& text, bool ok) { replies.append({text, ok}); });

        QVERIFY(assistant.ask(QStringLiteral("在文档里写一句你好，世界"), QStringLiteral("文档1.docx（文字文档）")));
        QVERIFY(assistant.state() == LocalAiState::Thinking);
        QVERIFY(!assistant.ask(QStringLiteral("再问一句"), QString())); // one question at a time
        QTRY_COMPARE(proposed.size(), 1);
        QCOMPARE(proposed[0].name, QStringLiteral("text.insert"));
        QCOMPARE(proposed[0].arguments.value(QStringLiteral("text")).toString(), QStringLiteral("你好，世界"));
        QVERIFY(proposed[0].source == CommandSource::Assistant);
        QVERIFY(validateCommand(proposed[0]).ok);
        QVERIFY(assistant.state() == LocalAiState::Thinking); // waiting for the window, not done

        // What reached the model: the document context, the tools, reasoning off.
        QCOMPARE(fake.chatRequests().size(), 1);
        const QJsonObject first = fake.chatRequests().first();
        QCOMPARE(first.value(QStringLiteral("model")).toString(), QStringLiteral("fake-qwen3:test"));
        QVERIFY(first.value(QStringLiteral("think")).isBool() && !first.value(QStringLiteral("think")).toBool());
        const QJsonArray messages = first.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
        QVERIFY(messages.first().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("文档1.docx")));
        QCOMPARE(messages.last().toObject().value(QStringLiteral("content")).toString(), QStringLiteral("在文档里写一句你好，世界"));
        QStringList toolNames;
        for (const QJsonValue& tool : first.value(QStringLiteral("tools")).toArray())
            toolNames.append(tool.toObject().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString());
        QVERIFY(toolNames.contains(QStringLiteral("text_insert")));
        QVERIFY(!toolNames.contains(QStringLiteral("assistant_listCommands")));

        assistant.commandFinished(proposed[0], {true, QStringLiteral("inserted"), QStringLiteral("已插入 5 个字符。"), {}});
        QTRY_VERIFY(assistant.state() == LocalAiState::Ready);
        QCOMPARE(deltas.join(QString()), QStringLiteral("已经插入了。"));
        QCOMPARE(replies.last().first, QStringLiteral("已经插入了。"));
        QVERIFY(replies.last().second);
        const QJsonArray history = fake.chatRequests().last().value(QStringLiteral("messages")).toArray();
        const QJsonObject toolResult = history.last().toObject();
        QCOMPARE(toolResult.value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
        QCOMPARE(toolResult.value(QStringLiteral("tool_name")).toString(), QStringLiteral("text_insert"));
        QVERIFY(toolResult.value(QStringLiteral("content")).toString().contains(QStringLiteral("inserted")));
        QVERIFY(history.at(history.size() - 2).toObject().contains(QStringLiteral("tool_calls")));
        for (const auto& request : fake.requests) QVERIFY(request.first.startsWith(QStringLiteral("/api/")));
    }

    void declinedCommandsAreToldToTheModel()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("gpt-oss:test"), 1.0, QStringLiteral("gptoss"),
                                        {QStringLiteral("completion"), QStringLiteral("tools"), QStringLiteral("thinking")})});
        fake.chats = {FakeOllama::chat({QStringLiteral("好的。")}, QStringLiteral("edit_undo")),
                      FakeOllama::chat({QStringLiteral("好的，不撤销。")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        LocalAssistant assistant;
        assistant.refresh();
        QTRY_VERIFY(assistant.state() == LocalAiState::Ready);
        QList<AssistantCommand> proposed;
        connect(&assistant, &LocalAssistant::commandProposed, this, [&proposed](const AssistantCommand& command) { proposed.append(command); });
        QVERIFY(assistant.ask(QStringLiteral("撤销一下"), QString()));
        QTRY_COMPARE(proposed.size(), 1);
        QCOMPARE(fake.chatRequests().first().value(QStringLiteral("think")).toString(), QStringLiteral("low")); // gpt-oss reasons briefly
        assistant.commandDeclined(proposed[0]);
        QTRY_VERIFY(assistant.state() == LocalAiState::Ready);
        const QJsonObject toolResult = fake.chatRequests().last().value(QStringLiteral("messages")).toArray().last().toObject();
        QCOMPARE(toolResult.value(QStringLiteral("tool_name")).toString(), QStringLiteral("edit_undo"));
        QVERIFY(toolResult.value(QStringLiteral("content")).toString().contains(QStringLiteral("declined")));
    }

    void ollamaWithoutModelsAsksForADownload()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("nomic-embed-text:latest"), 0.3, QStringLiteral("nomic-bert"),
                                        {QStringLiteral("embedding")})});
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        LocalAssistant assistant;
        assistant.refresh();
        QTRY_VERIFY(assistant.state() == LocalAiState::NoModel);
        QVERIFY(!assistant.ask(QStringLiteral("你好"), QString()));
        QVERIFY(assistant.recommendation().name.startsWith(QStringLiteral("qwen3:")));
        for (const auto& request : fake.requests) QVERIFY(request.first != QStringLiteral("/api/pull")); // never on its own
    }

    // --- office tasks ---

    void markdownBecomesDocumentFormatting()
    {
        const QString html = markdownToHtml(QStringLiteral(
            "<think>先想想</think># 会议通知\n正文**加粗**和*倾斜*\n- 一\n- 二\n1. 甲\n2) 乙\n"
            "| 项目 | 数量 |\n|---|---:|\n| 纸 | 3 |\n<script>alert(1)</script>\n---\n> 引用"));
        QVERIFY(!html.contains(QStringLiteral("先想想")));
        QVERIFY(html.contains(QStringLiteral("<h1>会议通知</h1>")));
        QVERIFY(html.contains(QStringLiteral("<p>正文<b>加粗</b>和<i>倾斜</i></p>")));
        QVERIFY(html.contains(QStringLiteral("<ul><li>一</li><li>二</li></ul>")));
        QVERIFY(html.contains(QStringLiteral("<ol><li>甲</li><li>乙</li></ol>")));
        QVERIFY(html.contains(QStringLiteral("<th>项目</th>")));
        QVERIFY(html.contains(QStringLiteral("<td>3</td>")));
        QVERIFY(html.contains(QStringLiteral("&lt;script&gt;"))); // text, never markup
        QVERIFY(!html.contains(QStringLiteral("<script>")));
        QVERIFY(html.contains(QStringLiteral("<p>引用</p>")));
        // In a reply bubble a heading is a bold line, not a giant title.
        QCOMPARE(markdownToHtml(QStringLiteral("## 要点"), MarkdownTarget::Bubble), QStringLiteral("<p><b>要点</b></p>"));
        // Office text: each line is its own paragraph.
        QCOMPARE(markdownToHtml(QStringLiteral("尊敬的各位：\n您好")), QStringLiteral("<p>尊敬的各位：</p><p>您好</p>"));
    }

    void rewritesComeBackClean()
    {
        QCOMPARE(cleanRewrite(QStringLiteral("以下是润色后的文字：\n\n“今天**天气**很好。”")), QStringLiteral("今天天气很好。"));
        QCOMPARE(cleanRewrite(QStringLiteral("第一段。\n\n第二段。")), QStringLiteral("第一段。\n\n第二段。"));
        QCOMPARE(cleanRewrite(QStringLiteral("```\n## 标题\n```")), QStringLiteral("标题"));
        // Quotes inside the text are kept.
        QCOMPARE(cleanRewrite(QStringLiteral("他说“好”，我说“行”。")), QStringLiteral("他说“好”，我说“行”。"));
        QCOMPARE(cleanRewrite(QStringLiteral("<think>嗯</think>结果")), QStringLiteral("结果"));
        // Markdown's two-space line breaks must not reach the document (seen in a real session).
        QCOMPARE(cleanRewrite(QStringLiteral("关于开展安全培训的通知  \n各部门：  \n请准时参加。\t\n行政部")),
                 QStringLiteral("关于开展安全培训的通知\n各部门：\n请准时参加。\n行政部"));
    }

    void tasksAreReadyMadeForOfficeWork()
    {
        QCOMPARE(tasksFor(TaskScope::Selection).size(), 6);
        QCOMPARE(tasksFor(TaskScope::Document).size(), 3);
        QCOMPARE(tasksFor(TaskScope::Writing).size(), 6);
        for (const TaskSpec* spec : tasksFor(TaskScope::Writing)) QVERIFY(!spec->placeholder.isEmpty());

        const TaskSpec* translate = findTask(QStringLiteral("translate"));
        const QString toEnglish = taskMessages(*translate, {QStringLiteral("今天开会"), {}, {}}).last().toObject()
                                      .value(QStringLiteral("content")).toString();
        QVERIFY(toEnglish.contains(QStringLiteral("英文")));
        const QString toChinese = taskMessages(*translate, {QStringLiteral("We meet today."), {}, {}}).last().toObject()
                                      .value(QStringLiteral("content")).toString();
        QVERIFY(toChinese.contains(QStringLiteral("简体中文")));

        const QJsonArray polish = taskMessages(*findTask(QStringLiteral("polish")), {QStringLiteral("原文内容"), {}, {}});
        QVERIFY(polish.first().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("不要使用 Markdown")));
        QVERIFY(polish.last().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("原文内容")));

        const QJsonArray draft = taskMessages(*findTask(QStringLiteral("notice")), {{}, QStringLiteral("周五开会"), {}});
        QVERIFY(draft.first().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("【】")));
        QVERIFY(draft.last().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("周五开会")));

        // Long documents are cut, and the model is told so.
        const QString longText(kTaskTextLimit + 500, QChar(0x5B57));
        const QString summary = taskMessages(*findTask(QStringLiteral("summary")), {longText, {}, QStringLiteral("报告.docx")})
                                    .last().toObject().value(QStringLiteral("content")).toString();
        QVERIFY(summary.contains(QStringLiteral("《报告.docx》")));
        QVERIFY(summary.contains(QStringLiteral("只提供了前")));
        QVERIFY(summary.size() < kTaskTextLimit + 400);

        QVERIFY(taskFormat(*findTask(QStringLiteral("proofread"))).value(QStringLiteral("properties")).toObject()
                    .contains(QStringLiteral("corrections")));
        QVERIFY(taskFormat(*findTask(QStringLiteral("polish"))).isEmpty());
        // The engine writes fields in schema order (QJsonObject sorts keys): the wrong text must come first.
        const QJsonObject item = taskFormat(*findTask(QStringLiteral("proofread"))).value(QStringLiteral("properties")).toObject()
                                     .value(QStringLiteral("corrections")).toObject().value(QStringLiteral("items")).toObject();
        QCOMPARE(item.value(QStringLiteral("properties")).toObject().keys(),
                 (QStringList{QStringLiteral("error"), QStringLiteral("fix"), QStringLiteral("reason")}));
        QCOMPARE(contextLengthFor(QJsonArray{QJsonObject{{QStringLiteral("content"), QStringLiteral("短")}}}, false), 4096);
        QCOMPARE(contextLengthFor(QJsonArray{QJsonObject{{QStringLiteral("content"), QString(10000, QLatin1Char('a'))}}}, false), 16384);
        QCOMPARE(contextLengthFor(QJsonArray{QJsonObject{{QStringLiteral("content"), QString(90000, QLatin1Char('a'))}}}, true), 32768);
    }

    void onlyApplicableCorrectionsAreOffered()
    {
        const QString document = QStringLiteral("请按排好时间，带好笔计本。这是我们的的目标。");
        const QString answer = QStringLiteral(
            "```json\n{\"corrections\":["
            "{\"original\":\"按排\",\"corrected\":\"安排\",\"reason\":\"错别字\"},"
            "{\"original\":\"笔计本\",\"corrected\":\"笔记本\",\"reason\":\"错别字\"},"
            "{\"original\":\"按排\",\"corrected\":\"安排\",\"reason\":\"重复\"},"
            "{\"original\":\"好时间\",\"corrected\":\"好时间\",\"reason\":\"没改\"},"
            "{\"original\":\"不存在\",\"corrected\":\"x\",\"reason\":\"编的\"},"
            "{\"original\":\"\",\"corrected\":\"x\",\"reason\":\"空\"},"
            "{\"original\":\"按排好时间\",\"corrected\":\"按排\",\"reason\":\"只删字\"},"
            "{\"original\":\"的的\",\"corrected\":\"的\",\"reason\":\"重复\"}]}\n```");
        const QList<Correction> fixes = parseCorrections(answer, document);
        QCOMPARE(fixes.size(), 3);
        QCOMPARE(fixes[0].original, QStringLiteral("按排"));
        QCOMPARE(fixes[1].corrected, QStringLiteral("笔记本"));
        QCOMPARE(fixes[2].corrected, QStringLiteral("的")); // removing a doubled word is a real fix
        // "按排好时间 -> 按排" only drops words and keeps the typo: never offered.
        QVERIFY(parseCorrections(QStringLiteral("没有错误"), document).isEmpty());
    }

    void newCommandsCheckTheirChoices()
    {
        const auto check = [](const QString& name, const QJsonObject& arguments) {
            return validateCommand({name, arguments, CommandSource::Assistant}).code;
        };
        QCOMPARE(check(QStringLiteral("text.insert"), {{"text", "x"}, {"format", "markdown"}, {"position", "after"}}),
                 QStringLiteral("valid"));
        QCOMPARE(check(QStringLiteral("text.insert"), {{"text", "x"}, {"format", "html"}}), QStringLiteral("invalid_argument"));
        QCOMPARE(check(QStringLiteral("text.insert"), {{"text", "x"}, {"position", "start"}}), QStringLiteral("invalid_argument"));
        QCOMPARE(check(QStringLiteral("selection.replace"), {{"expected", "x"}}), QStringLiteral("missing_argument"));
        QCOMPARE(check(QStringLiteral("selection.replace"), {{"text", "新"}, {"expected", "旧"}}), QStringLiteral("valid"));
        QCOMPARE(check(QStringLiteral("document.replaceText"), {{"find", "按排"}, {"replace", "安排"}}), QStringLiteral("valid"));
        QCOMPARE(check(QStringLiteral("document.replaceText"), {{"find", QString(201, QLatin1Char('a'))}, {"replace", "b"}}),
                 QStringLiteral("argument_too_long"));
        QCOMPARE(check(QStringLiteral("document.getText"), {}), QStringLiteral("valid"));
        QVERIFY(!findCommand(QStringLiteral("document.getText"))->changesDocument);
        QVERIFY(findCommand(QStringLiteral("selection.replace"))->changesDocument);
        QString error;
        QCOMPARE(parseSlashCommand(QStringLiteral("/全文"), &error)->name, QStringLiteral("document.getText"));
        // The tool schema lists the allowed values.
        for (const QJsonValue& tool : commandCatalogJson()) {
            if (tool.toObject().value(QStringLiteral("name")).toString() != QStringLiteral("text.insert")) continue;
            const QJsonObject format = tool.toObject().value(QStringLiteral("inputSchema")).toObject()
                                           .value(QStringLiteral("properties")).toObject().value(QStringLiteral("format")).toObject();
            QCOMPARE(format.value(QStringLiteral("enum")).toArray(), (QJsonArray{QStringLiteral("plain"), QStringLiteral("markdown")}));
        }
    }

    void tasksAnswerInOneStepAndTheModelIsWarmedUp()
    {
        FakeOllama fake;
        fake.running({FakeOllama::model(QStringLiteral("fake-qwen3:test"), 1.2, QStringLiteral("qwen3"),
                                        {QStringLiteral("completion"), QStringLiteral("tools"), QStringLiteral("thinking")})});
        fake.chats = {FakeOllama::chat({QStringLiteral("请大家"), QStringLiteral("准时参会。")}),
                      FakeOllama::chat({QStringLiteral("{\"corrections\":[]}")})};
        ScopedEnvironment url("OPEN_OFFICE_OLLAMA_URL", fake.url());
        LocalAssistant assistant;
        assistant.refresh();
        QTRY_VERIFY(assistant.state() == LocalAiState::Ready);

        assistant.warmUp();
        assistant.warmUp(); // loaded already: no second request
        QTRY_VERIFY(std::any_of(fake.requests.begin(), fake.requests.end(),
                                [](const auto& request) { return request.first == QStringLiteral("/api/generate"); }));
        int warmUps = 0;
        for (const auto& request : fake.requests) {
            if (request.first != QStringLiteral("/api/generate")) continue;
            ++warmUps;
            QCOMPARE(request.second.value(QStringLiteral("model")).toString(), QStringLiteral("fake-qwen3:test"));
            QCOMPARE(request.second.value(QStringLiteral("keep_alive")).toString(), QStringLiteral("30m"));
        }
        QCOMPARE(warmUps, 1);

        QStringList deltas;
        QList<QStringList> finished;
        connect(&assistant, &LocalAssistant::taskDelta, this, [&deltas](const QString& text) { deltas.append(text); });
        connect(&assistant, &LocalAssistant::taskFinished, this,
                [&finished](const QString& id, const QString& text, bool ok, const QString& error) {
            finished.append({id, text, ok ? QStringLiteral("ok") : QStringLiteral("failed"), error});
        });
        QVERIFY(assistant.runTask(*findTask(QStringLiteral("polish")), {QStringLiteral("大家要准时来开会"), {}, {}}));
        QCOMPARE(assistant.runningTask(), QStringLiteral("polish"));
        QVERIFY(!assistant.runTask(*findTask(QStringLiteral("polish")), {QStringLiteral("再来"), {}, {}})); // one at a time
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished[0], (QStringList{QStringLiteral("polish"), QStringLiteral("请大家准时参会。"), QStringLiteral("ok"), QString()}));
        QCOMPARE(deltas.join(QString()), QStringLiteral("请大家准时参会。"));
        QVERIFY(assistant.state() == LocalAiState::Ready);
        const QJsonObject request = fake.chatRequests().first();
        QVERIFY(!request.contains(QStringLiteral("tools")));
        QVERIFY(!request.contains(QStringLiteral("format")));
        QCOMPARE(request.value(QStringLiteral("keep_alive")).toString(), QStringLiteral("30m"));
        QCOMPARE(request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_ctx")).toInt(), 4096);
        QVERIFY(request.value(QStringLiteral("think")).isBool());

        QVERIFY(assistant.runTask(*findTask(QStringLiteral("proofread")), {QStringLiteral("文档内容"), {}, {}}));
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(fake.chatRequests().last().value(QStringLiteral("format")).toObject().contains(QStringLiteral("properties")));
        QCOMPARE(fake.chatRequests().last().value(QStringLiteral("options")).toObject().value(QStringLiteral("temperature")).toDouble(), 0.0);
    }

    // --- built-in engine (llama.cpp server) ---

    void engineMessagesUseTheOpenAiShape()
    {
        const QJsonArray messages{
            QJsonObject{{"role", "user"}, {"content", "插入你好"}},
            QJsonObject{{"role", "assistant"}, {"content", ""},
                        {"tool_calls", QJsonArray{QJsonObject{{"function", QJsonObject{{"name", "text_insert"},
                                                                                         {"arguments", QJsonObject{{"text", "你好"}}}}}}}}},
            QJsonObject{{"role", "tool"}, {"tool_name", "text_insert"}, {"content", "{\"ok\":true}"}}};
        const QJsonArray converted = EngineClient::toOpenAiMessages(messages);
        const QJsonObject call = converted.at(1).toObject().value("tool_calls").toArray().first().toObject();
        QCOMPARE(call.value("type").toString(), QStringLiteral("function"));
        QVERIFY(!call.value("id").toString().isEmpty());
        QCOMPARE(call.value("function").toObject().value("arguments").toString(), QStringLiteral("{\"text\":\"你好\"}"));
        const QJsonObject result = converted.at(2).toObject();
        QCOMPARE(result.value("tool_call_id").toString(), call.value("id").toString()); // the answer matches its call
        QVERIFY(!result.contains("tool_name"));

        ChatOptions options;
        options.think = QStringLiteral("false");
        options.format = taskFormat(*findTask(QStringLiteral("proofread")));
        const QJsonObject body = EngineClient::requestBody(messages, options);
        QVERIFY(body.value("stream").toBool());
        QCOMPARE(body.value("chat_template_kwargs").toObject().value("enable_thinking").toBool(true), false);
        QCOMPARE(body.value("response_format").toObject().value("type").toString(), QStringLiteral("json_schema"));
        QVERIFY(body.value("response_format").toObject().value("json_schema").toObject().value("schema").isObject());
        QVERIFY(!body.contains("tools"));
    }

    void engineStreamsAnswersAndToolCalls()
    {
        FakeOllama fake;
        fake.chats = {FakeOllama::sse({QStringLiteral("你"), QStringLiteral("好")}, {}, {}, 37.5),
                      FakeOllama::sse({}, QStringLiteral("text_insert"), {{"text", "你好世界"}})};
        EngineClient client;
        client.setEndpoint(QUrl(QString::fromLatin1(fake.url())), QStringLiteral("secret"));
        QStringList deltas;
        QList<QJsonArray> calls;
        QStringList contents;
        connect(&client, &ChatClient::chatDelta, this, [&deltas](quint64, const QString& text) { deltas.append(text); });
        connect(&client, &ChatClient::chatFinished, this,
                [&calls, &contents](quint64, bool ok, const QString& content, const QJsonArray& toolCalls, const QString&) {
            QVERIFY(ok);
            contents.append(content);
            calls.append(toolCalls);
        });
        client.chat(QString(), QJsonArray{QJsonObject{{"role", "user"}, {"content", "你好"}}}, {});
        QTRY_COMPARE(contents.size(), 1);
        QCOMPARE(deltas.join(QString()), QStringLiteral("你好"));
        QCOMPARE(client.lastTokensPerSecond(), 37.5);
        QCOMPARE(fake.requestHeaders.first().value("authorization"), QByteArray("Bearer secret"));
        client.chat(QString(), QJsonArray{QJsonObject{{"role", "user"}, {"content", "插入"}}}, {});
        QTRY_COMPARE(contents.size(), 2);
        const QJsonObject function = calls.at(1).first().toObject().value("function").toObject();
        QCOMPARE(function.value("name").toString(), QStringLiteral("text_insert"));
        QCOMPARE(function.value("arguments").toObject().value("text").toString(), QStringLiteral("你好世界")); // pieces joined
        QCOMPARE(calls.at(1).first().toObject().value("id").toString(), QStringLiteral("call_7"));
    }

    void engineUsesTheGraphicsCardNotIntegratedGraphics()
    {
        const QString list = QStringLiteral(
            "Available devices:\n"
            "  Vulkan0: Intel(R) Graphics (18366 MiB, 17598 MiB free)\n"
            "  Vulkan1: NVIDIA GeForce RTX 5070 Ti Laptop GPU (11944 MiB, 11176 MiB free)\n");
        QCOMPARE(LocalEngine::pickDevice(list, QStringLiteral("NVIDIA GeForce RTX 5070 Ti Laptop GPU")), QStringLiteral("Vulkan1"));
        QCOMPARE(LocalEngine::pickDevice(list, QString()), QStringLiteral("Vulkan1")); // never the integrated one
        QCOMPARE(LocalEngine::pickDevice(QStringLiteral("  Vulkan0: Intel(R) Graphics (18366 MiB, 1 MiB free)\n"), QString()),
                 QString());
    }

    void catalogFitsTheComputerAndItsDisks()
    {
        const qint64 gb = 1LL << 30;
        QCOMPARE(supportedModels().size(), 3);
        QCOMPARE(recommendedCatalogModel(32 * gb, 12 * gb).id, QStringLiteral("qwen3-8b"));
        QCOMPARE(recommendedCatalogModel(16 * gb, 0).id, QStringLiteral("qwen3-4b"));
        QCOMPARE(recommendedCatalogModel(4 * gb, 0).id, QStringLiteral("qwen3-1.7b"));
        QVERIFY(asLocalModel(*findCatalogModel(QStringLiteral("qwen3-4b"))).tools);
        QVERIFY(!defaultModelFolder().isEmpty());

        QTemporaryDir folder;
        ScopedEnvironment directory("OPEN_OFFICE_MODEL_DIR", QFile::encodeName(folder.path()));
        QCOMPARE(modelFolder(), QDir::cleanPath(folder.path()));
        QVERIFY(installedModels().isEmpty());
        QFile part(folder.filePath(QStringLiteral("Qwen3-4B-Q4_K_M.gguf.part")));
        QVERIFY(part.open(QIODevice::WriteOnly));
        part.close();
        QVERIFY(installedModels().isEmpty()); // an unfinished download is not a model
        QFile model(folder.filePath(QStringLiteral("Qwen3-4B-Q4_K_M.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.close();
        QCOMPARE(installedModels().size(), 1);
        QCOMPARE(installedModels().first().id, QStringLiteral("qwen3-4b"));
    }

    void builtinEngineStartsOnFirstUseAndAnswers()
    {
        FakeOllama fake;
        fake.replies.insert(QStringLiteral("/health"), R"({"status":"ok"})");
        fake.chats = {FakeOllama::sse({QStringLiteral("请大家"), QStringLiteral("准时参会。")})};
        QTemporaryDir folder;
        QFile model(folder.filePath(QStringLiteral("Qwen3-4B-Q4_K_M.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.close();
        ScopedEnvironment backend("OPEN_OFFICE_AI_BACKEND", "builtin");
        ScopedEnvironment engineUrl("OPEN_OFFICE_AI_ENGINE_URL", fake.url());
        ScopedEnvironment directory("OPEN_OFFICE_MODEL_DIR", QFile::encodeName(folder.path()));
        LocalAssistant assistant;
        QVERIFY(assistant.builtin());
        assistant.refresh();
        QVERIFY(assistant.state() == LocalAiState::Ready);
        QCOMPARE(assistant.model(), QStringLiteral("qwen3-4b"));
        QVERIFY(assistant.statusText().contains(QStringLiteral("qwen3-4b")));
        QList<QStringList> finished;
        connect(&assistant, &LocalAssistant::taskFinished, this,
                [&finished](const QString& id, const QString& text, bool ok, const QString& error) {
            finished.append({id, text, ok ? QStringLiteral("ok") : QStringLiteral("failed"), error});
        });
        QVERIFY(assistant.runTask(*findTask(QStringLiteral("polish")), {QStringLiteral("大家要准时来开会"), {}, {}}));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished[0].at(1), QStringLiteral("请大家准时参会。"));
        QCOMPARE(finished[0].at(2), QStringLiteral("ok"));
        QVERIFY(assistant.engine()->isReady());
        const QJsonObject request = fake.chatRequests().first();
        // A small model reasons briefly where the engine is fast (a graphics card); otherwise it answers directly.
        if (assistant.videoMemory() > 0) QVERIFY(!request.contains("chat_template_kwargs"));
        else QCOMPARE(request.value("chat_template_kwargs").toObject().value("enable_thinking").toBool(true), false);
        QVERIFY(request.contains("response_format") == false); // polish answers in plain text
        QCOMPARE(assistant.lastSpeed(), 42.0);
    }

    void modelDownloadsResumeAndAreVerified()
    {
        const QByteArray payload = QByteArray(300000, 'x') + "end";
        QString sha = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        FakeOllama fake;
        fake.handler = [&payload, &sha](const QString& path, const QMap<QByteArray, QByteArray>& headers, FakeOllama::Response* response) {
            if (path == QStringLiteral("/api/v1/models/Qwen/Qwen3-1.7B-GGUF/repo/files")) {
                response->body = QJsonDocument(QJsonObject{{"Data", QJsonObject{{"Files", QJsonArray{QJsonObject{
                    {"Name", "Qwen3-1.7B-Q8_0.gguf"}, {"Size", double(payload.size())}, {"Sha256", sha}}}}}}})
                                     .toJson(QJsonDocument::Compact);
                return true;
            }
            if (path == QStringLiteral("/models/Qwen/Qwen3-1.7B-GGUF/resolve/master/Qwen3-1.7B-Q8_0.gguf")) {
                const QByteArray range = headers.value("range");
                const qint64 from = range.startsWith("bytes=") ? range.mid(6).split('-').first().toLongLong() : 0;
                response->status = from > 0 ? 206 : 200;
                response->body = payload.mid(from);
                return true;
            }
            return false;
        };
        QTemporaryDir folder;
        ScopedEnvironment source("OPEN_OFFICE_MODEL_SOURCE", fake.url());
        const CatalogModel& model = *findCatalogModel(QStringLiteral("qwen3-1.7b"));
        QFile part(QDir(folder.path()).filePath(model.file + QStringLiteral(".part")));
        QVERIFY(part.open(QIODevice::WriteOnly));
        part.write(payload.left(1000)); // an earlier, interrupted download
        part.close();

        ModelDownloader downloader;
        QList<QStringList> results;
        connect(&downloader, &ModelDownloader::finished, this, [&results](const QString& id, bool ok, const QString& message) {
            results.append({id, ok ? QStringLiteral("ok") : QStringLiteral("failed"), message});
        });
        downloader.start(model, folder.path());
        QVERIFY(downloader.active());
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0].at(1), QStringLiteral("ok"));
        QFile done(QDir(folder.path()).filePath(model.file));
        QVERIFY(done.open(QIODevice::ReadOnly));
        QCOMPARE(done.readAll(), payload);
        done.close();
        QVERIFY(!QFile::exists(part.fileName()));
        bool resumed = false;
        for (const auto& headers : fake.requestHeaders) resumed = resumed || headers.value("range") == "bytes=1000-";
        QVERIFY(resumed); // continued, not started over

        // A file that does not match the published SHA256 is thrown away, never used.
        QVERIFY(QFile::remove(done.fileName()));
        sha = QStringLiteral("00");
        downloader.start(model, folder.path());
        QTRY_COMPARE(results.size(), 2);
        QCOMPARE(results[1].at(1), QStringLiteral("failed"));
        QVERIFY(results[1].at(2).contains(QStringLiteral("核对没通过")));
        QVERIFY(!QFile::exists(done.fileName()));
        QVERIFY(!QFile::exists(part.fileName()));
    }
};

QTEST_GUILESS_MAIN(AssistantTest)
#include "assistant_test.moc"
