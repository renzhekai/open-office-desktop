#include "assistant/assistant_commands.h"
#include "assistant/local_assistant.h"
#include "assistant/ollama_client.h"
#include "fake_ollama.h"

#include <QJsonArray>
#include <QSet>
#include <QtTest>

using namespace openoffice;

class AssistantTest final : public QObject {
    Q_OBJECT

private slots:
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
        QCOMPARE(text.value(QStringLiteral("maxLength")).toInt(), 10000);
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
        QTest::newRow("too long") << "text.insert" << QJsonObject{{"text", QString(10001, QLatin1Char('a'))}}
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
        QVERIFY(validateCommand({QStringLiteral("text.insert"), {{QStringLiteral("text"), QString(10000, QLatin1Char('x'))}},
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
        QCOMPARE(pickInstalledModel(installed, 32 * gb), QStringLiteral("gpt-oss:20b"));
        QCOMPARE(pickInstalledModel(installed, 16 * gb), QStringLiteral("granite4.1:3b")); // 13.8 GB would not fit
        QCOMPARE(pickInstalledModel(installed + QList<LocalModel>{model(QStringLiteral("qwen3:8b"), 5.2, QStringLiteral("qwen3"), true)}, 16 * gb),
                 QStringLiteral("qwen3:8b"));
        QCOMPARE(pickInstalledModel({installed[2], installed[3]}, 16 * gb), QStringLiteral("llama3:8b")); // chat only
        QCOMPARE(pickInstalledModel({installed[3]}, 16 * gb), QString()); // embeddings cannot chat
        QCOMPARE(pickInstalledModel({}, 16 * gb), QString());

        QCOMPARE(recommendDownload(4 * gb).name, QStringLiteral("qwen3:1.7b"));
        QCOMPARE(recommendDownload(static_cast<qint64>(7.8 * gb)).name, QStringLiteral("qwen3:4b")); // an "8 GB" laptop
        QCOMPARE(recommendDownload(static_cast<qint64>(15.8 * gb)).name, QStringLiteral("qwen3:8b"));
        QCOMPARE(recommendDownload(64 * gb).name, QStringLiteral("qwen3:8b"));
        for (const qint64 memory : {4 * gb, 8 * gb, 16 * gb}) {
            // Every recommendation fits, so a recommended download is then also picked.
            const ModelRecommendation recommended = recommendDownload(memory);
            QVERIFY(recommended.approximateGigabytes * gb < memory * 0.55);
        }
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
};

QTEST_GUILESS_MAIN(AssistantTest)
#include "assistant_test.moc"
