#include "assistant/conversation_store.h"
#include "assistant/assistant_commands.h"
#include "assistant/cloud_models.h"
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
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
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
        // Settings go to a file of the tests' own, never the person's.
        QVERIFY(m_settings.isValid());
        qputenv("OPEN_OFFICE_SETTINGS_FILE", QFile::encodeName(m_settings.filePath(QStringLiteral("settings.ini"))));
    }

    void cloudProvidersKeepTheirKeysSealed()
    {
        aiSettings().clear();
        QVERIFY(checkCloudBaseUrl(QStringLiteral("https://api.deepseek.com/v1")).isEmpty());
        QVERIFY(checkCloudBaseUrl(QStringLiteral("http://127.0.0.1:8000/v1")).isEmpty()); // a server on this computer
        QVERIFY(!checkCloudBaseUrl(QStringLiteral("http://api.example.com/v1")).isEmpty()); // a key must not travel in clear
        QVERIFY(!checkCloudBaseUrl(QString()).isEmpty());
        QVERIFY(!checkCloudBaseUrl(QStringLiteral("ftp://example.com")).isEmpty());
        QCOMPARE(splitModelNames(QStringLiteral("deepseek-chat, deepseek-reasoner，deepseek-chat\nqwen-plus")),
                 (QStringList{QStringLiteral("deepseek-chat"), QStringLiteral("deepseek-reasoner"), QStringLiteral("qwen-plus")}));
        QCOMPARE(newCloudProviderId(QStringLiteral("DeepSeek")), QStringLiteral("deepseek"));
        QCOMPARE(newCloudProviderId(QStringLiteral("通义千问")), QStringLiteral("cloud"));

        // The key is sealed with Windows data protection: never in the settings as written.
        const QString secret = QStringLiteral("sk-test-1234567890");
        const QString sealed = protectSecret(secret);
        QVERIFY(sealed.startsWith(QStringLiteral("dpapi:")) && !sealed.contains(secret));
        QCOMPARE(unprotectSecret(sealed), secret);
        QVERIFY(unprotectSecret(sealed.left(sealed.size() - 4) + QStringLiteral("AAAA")).isEmpty()); // tampered
        QVERIFY(unprotectSecret(secret).isEmpty()); // plain text is never taken as a key

        CloudProvider provider{QStringLiteral("deepseek"), QStringLiteral("DeepSeek"), QStringLiteral("https://api.deepseek.com/v1"),
                               {QStringLiteral("deepseek-chat")}};
        QString error;
        QVERIFY(saveCloudProvider(provider, secret, &error));
        QCOMPARE(newCloudProviderId(QStringLiteral("DeepSeek")), QStringLiteral("deepseek-2"));
        QCOMPARE(cloudProviders().size(), 1);
        QCOMPARE(cloudApiKey(QStringLiteral("deepseek")), secret);
        QFile written(QString::fromLocal8Bit(qgetenv("OPEN_OFFICE_SETTINGS_FILE")));
        QVERIFY(written.open(QIODevice::ReadOnly));
        QVERIFY(!written.readAll().contains(secret.toUtf8()));
        written.close();
        // Changing it without a key keeps the key.
        provider.models << QStringLiteral("deepseek-reasoner");
        QVERIFY(saveCloudProvider(provider, QString(), &error));
        QCOMPARE(cloudApiKey(QStringLiteral("deepseek")), secret);
        QCOMPARE(findCloudProvider(QStringLiteral("deepseek"))->models.size(), 2);
        // What is missing is said.
        CloudProvider broken = provider;
        broken.models.clear();
        QVERIFY(!saveCloudProvider(broken, QString(), &error) && error.contains(QStringLiteral("模型")));
        broken = provider;
        broken.baseUrl = QStringLiteral("http://api.deepseek.com");
        QVERIFY(!saveCloudProvider(broken, QString(), &error) && error.contains(QStringLiteral("https")));
        // Removing it removes the key and the consent too.
        setCloudConfirmed(QStringLiteral("deepseek"));
        QVERIFY(cloudConfirmed(QStringLiteral("deepseek")));
        removeCloudProvider(QStringLiteral("deepseek"));
        QVERIFY(cloudProviders().isEmpty());
        QVERIFY(cloudApiKey(QStringLiteral("deepseek")).isEmpty() && !hasCloudApiKey(QStringLiteral("deepseek")));
        QVERIFY(!cloudConfirmed(QStringLiteral("deepseek")));
    }

    void cloudRequestsAreOpenAiShaped()
    {
        ChatOptions options;
        options.think = QStringLiteral("false");
        options.format = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
        options.tools = QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("function")}}};
        const QJsonArray messages{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("hi")}}};
        const QJsonObject body = EngineClient::cloudRequestBody(QStringLiteral("deepseek-chat"), messages, options);
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("deepseek-chat"));
        QVERIFY(body.value(QStringLiteral("stream")).toBool());
        QVERIFY(body.contains(QStringLiteral("tools")));
        // The engine's own options are left out: services differ in what they accept.
        QVERIFY(!body.contains(QStringLiteral("response_format")));
        QVERIFY(!body.contains(QStringLiteral("chat_template_kwargs")));
        QVERIFY(!body.contains(QStringLiteral("reasoning_effort")));
        QCOMPARE(EngineClient::serviceUrl(QUrl(QStringLiteral("https://api.example.com/v1/")), QStringLiteral("chat/completions")).toString(),
                 QStringLiteral("https://api.example.com/v1/chat/completions"));
        QCOMPARE(EngineClient::serviceUrl(QUrl(QStringLiteral("http://127.0.0.1:9000/compatible-mode/v1")), QStringLiteral("models")).toString(),
                 QStringLiteral("http://127.0.0.1:9000/compatible-mode/v1/models"));
    }

    void aCloudModelAnswersThroughItsService()
    {
        aiSettings().clear();
        ScopedEnvironment backend("OPEN_OFFICE_AI_BACKEND", QByteArray()); // the model list decides
        ScopedEnvironment noOllama("OPEN_OFFICE_OLLAMA_URL", closedOllamaUrl());
        FakeOllama fake; // as an OpenAI-compatible service
        fake.chats = {FakeOllama::sse({QStringLiteral("云端"), QStringLiteral("的回答")})};
        QString error;
        QVERIFY(saveCloudProvider({QStringLiteral("fake"), QStringLiteral("Fake Cloud"), QString::fromLatin1(fake.url() + "/v1"),
                                   {QStringLiteral("fake-model"), QStringLiteral("fake-large")}},
                                  QStringLiteral("test-key"), &error));
        LocalAssistant assistant;
        assistant.refresh();
        QVERIFY(!assistant.cloud());
        // The list: built-in models (none installed here), the service's models; no Ollama group with the option off.
        QList<ModelChoice> choices = assistant.modelChoices();
        const auto has = [&choices](const QString& key, const QString& group) {
            return std::any_of(choices.begin(), choices.end(), [&](const ModelChoice& choice) { return choice.key == key && choice.group == group; });
        };
        QVERIFY(has(QStringLiteral("cloud:fake/fake-model"), QStringLiteral("云端模型")));
        QVERIFY(has(QStringLiteral("cloud:fake/fake-large"), QStringLiteral("云端模型")));
        QVERIFY(std::none_of(choices.begin(), choices.end(), [](const ModelChoice& choice) { return choice.group == QStringLiteral("Ollama 模型"); }));

        QVERIFY(assistant.chooseModel(QStringLiteral("cloud:fake/fake-model")));
        QVERIFY(assistant.cloud());
        QCOMPARE(assistant.state(), LocalAiState::Ready);
        QCOMPARE(assistant.currentModelKey(), QStringLiteral("cloud:fake/fake-model"));
        QVERIFY(assistant.statusText().contains(QStringLiteral("Fake Cloud")) && assistant.statusText().contains(QStringLiteral("发给")));
        QCOMPARE(activeModelKey(), QStringLiteral("cloud:fake/fake-model")); // remembered
        choices = assistant.modelChoices();
        QVERIFY(std::any_of(choices.begin(), choices.end(), [](const ModelChoice& choice) {
            return choice.key == QStringLiteral("cloud:fake/fake-model") && choice.current;
        }));

        QSignalSpy finished(&assistant, &LocalAssistant::replyFinished);
        QVERIFY(assistant.ask(QStringLiteral("你好"), QString()));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toString(), QStringLiteral("云端的回答"));
        QVERIFY(finished.first().at(1).toBool());
        // Sent as the service expects: its path, the key, the model by name; the system prompt says it is a cloud service.
        QCOMPARE(fake.requests.last().first, QStringLiteral("/v1/chat/completions"));
        QCOMPARE(fake.requestHeaders.last().value("authorization"), QByteArray("Bearer test-key"));
        const QJsonObject sent = fake.chatRequests().last();
        QCOMPARE(sent.value(QStringLiteral("model")).toString(), QStringLiteral("fake-model"));
        QVERIFY(sent.value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("content")).toString()
                    .contains(QStringLiteral("云端服务")));

        // A wrong key: the reason in words people can act on.
        fake.handler = [](const QString& path, const QMap<QByteArray, QByteArray>&, FakeOllama::Response* response) {
            if (path != QStringLiteral("/v1/chat/completions")) return false;
            response->status = 401;
            response->body = R"({"error":{"message":"Invalid API key"}})";
            return true;
        };
        QVERIFY(assistant.ask(QStringLiteral("再说一次"), QString()));
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!finished.last().at(1).toBool());
        QVERIFY2(finished.last().at(0).toString().contains(QStringLiteral("API Key")), qPrintable(finished.last().at(0).toString()));

        // A service as strict as DeepSeek: the model asks for two tools at once; the person writes
        // again instead of answering 执行/不要. The next request still has an answer for its call.
        fake.handler = [&fake](const QString& path, const QMap<QByteArray, QByteArray>&, FakeOllama::Response* response) {
            if (path != QStringLiteral("/v1/chat/completions")) return false;
            const QJsonArray sent = fake.requests.last().second.value(QStringLiteral("messages")).toArray();
            QStringList waiting;
            for (const QJsonValue& value : sent) {
                const QJsonObject message = value.toObject();
                const QString role = message.value(QStringLiteral("role")).toString();
                if (role == QStringLiteral("tool")) waiting.removeOne(message.value(QStringLiteral("tool_call_id")).toString());
                else if (!waiting.isEmpty()) break; // a message while calls wait: refused below
                for (const QJsonValue& call : message.value(QStringLiteral("tool_calls")).toArray())
                    waiting << call.toObject().value(QStringLiteral("id")).toString();
            }
            if (!waiting.isEmpty()) {
                response->status = 400;
                response->body = R"({"error":{"message":"An assistant message with 'tool_calls' must be followed by tool messages"}})";
                return true;
            }
            return false; // the canned answers
        };
        // DeepSeek may ask for two tools in one answer; one runs (the window confirms it), and the
        // next request must not leave the other unanswered.
        const auto chunk = [](const QJsonObject& delta, const QString& finish = QString()) {
            QJsonObject choice{{QStringLiteral("index"), 0}, {QStringLiteral("delta"), delta}};
            if (!finish.isEmpty()) choice.insert(QStringLiteral("finish_reason"), finish);
            return "data: " + QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{choice}}}).toJson(QJsonDocument::Compact) + "\n\n";
        };
        const auto toolCall = [](int index, const char* id, const char* name, const char* arguments) {
            return QJsonObject{{QStringLiteral("index"), index}, {QStringLiteral("id"), QString::fromLatin1(id)},
                               {QStringLiteral("type"), QStringLiteral("function")},
                               {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), QString::fromLatin1(name)},
                                                                        {QStringLiteral("arguments"), QString::fromLatin1(arguments)}}}};
        };
        const QByteArray twoCalls = chunk({{QStringLiteral("tool_calls"), QJsonArray{toolCall(0, "call_a", "text_insert", R"({"text":"x"})"),
                                                                                     toolCall(1, "call_b", "edit_undo", "{}")}}})
            + chunk({}, QStringLiteral("tool_calls")) + "data: [DONE]\n\n";
        fake.chats = {twoCalls, FakeOllama::sse({QStringLiteral("好的，已经插入。")})};
        QSignalSpy proposed(&assistant, &LocalAssistant::commandProposed);
        QVERIFY(assistant.ask(QStringLiteral("做个表格"), QString()));
        QTRY_COMPARE(proposed.size(), 1); // one at a time
        const AssistantCommand first = proposed.first().first().value<AssistantCommand>();
        QCOMPARE(first.name, QStringLiteral("text.insert"));
        assistant.commandFinished(first, {true, QStringLiteral("inserted"), QStringLiteral("已插入。"), {}});
        QTRY_COMPARE(finished.size(), 4);
        QVERIFY2(finished.last().at(1).toBool(), qPrintable(finished.last().at(0).toString()));
        QCOMPARE(finished.last().at(0).toString(), QStringLiteral("好的，已经插入。"));
        fake.handler = nullptr;

        // Back to the built-in engine; the Ollama option shows its group even when Ollama is not running.
        QVERIFY(assistant.chooseModel(QStringLiteral("builtin:qwen3-4b")));
        QVERIFY(!assistant.cloud());
        assistant.setOllamaEnabled(true);
        choices = assistant.modelChoices();
        QTRY_VERIFY(std::any_of(choices.begin(), choices.end(), [](const ModelChoice& choice) { return choice.group == QStringLiteral("Ollama 模型"); }));
        assistant.setOllamaEnabled(false);
        aiSettings().clear();
    }

    void badCommandsAreCaughtAndChangesDescribed()
    {
        // The user's screenshot: a format command with no format failed only after "执行".
        const auto command = [](const char* name, const QJsonObject& arguments) {
            return AssistantCommand{QString::fromLatin1(name), arguments, CommandSource::Assistant};
        };
        QCOMPARE(validateCommand(command("format.cells", {{QStringLiteral("table"), 1}})).code, QStringLiteral("missing_argument"));
        QCOMPARE(validateCommand(command("format.cells", {{QStringLiteral("table"), 1}, {QStringLiteral("rows"), QStringLiteral("first")},
                                                          {QStringLiteral("bold"), true}})).code, QStringLiteral("invalid_argument"));
        QCOMPARE(validateCommand(command("selection.select", {{QStringLiteral("find"), QStringLiteral("a")}, {QStringLiteral("paragraph"), 2}})).code,
                 QStringLiteral("invalid_argument"));
        QVERIFY(validateCommand(command("format.read", {{QStringLiteral("find"), QStringLiteral("a")}})).ok);
        const AssistantCommand header = command("format.cells", {{QStringLiteral("table"), 1}, {QStringLiteral("rows"), QStringLiteral("1")},
                                                                {QStringLiteral("columns"), QStringLiteral("1-4")}, {QStringLiteral("bold"), true},
                                                                {QStringLiteral("align"), QStringLiteral("center")},
                                                                {QStringLiteral("fill"), QStringLiteral("#D9E2F3")}});
        QVERIFY(validateCommand(header).ok);
        QCOMPARE(describeCommand(header), QStringLiteral("第 1 张表格第 1 行、第 1–4 列：加粗、底色 #D9E2F3、居中"));
        QCOMPARE(describeCommand(command("format.paragraphs", {{QStringLiteral("paragraphs"), QStringLiteral("2,5")},
                                                               {QStringLiteral("style"), QStringLiteral("heading2")}})),
                 QStringLiteral("第 2、5 段：二级标题"));
        QCOMPARE(describeCommand(command("format.text", {{QStringLiteral("find"), QStringLiteral("截止日期")}, {QStringLiteral("color"), QStringLiteral("#C00000")}})),
                 QStringLiteral("文中所有“截止日期”：文字颜色 #C00000"));
        QCOMPARE(describeCommand(command("table.setCell", {{QStringLiteral("table"), 2}, {QStringLiteral("row"), 3},
                                                           {QStringLiteral("column"), 1}, {QStringLiteral("text"), QStringLiteral("已完成")}})),
                 QStringLiteral("把第 2 张表格第 3 行第 1 列改为：已完成"));
    }

    void vagueStyleRequestsGetTheProgramsDefaults()
    {
        QVERIFY(vagueStyleRequest(QStringLiteral("帮我优化一下样式")));
        QVERIFY(vagueStyleRequest(QStringLiteral("美化一下这个表格的格式")));
        QVERIFY(vagueStyleRequest(QStringLiteral("排版好看点")));
        QVERIFY(vagueStyleRequest(QStringLiteral("Make it look more professional")));
        QVERIFY(!vagueStyleRequest(QStringLiteral("把日期统一写成 2026-11-16")));
        QVERIFY(namesAFormat(QStringLiteral("优化一下表头，加粗")));
        QVERIFY(!namesAFormat(QStringLiteral("帮我优化一下样式")));
        // Formats only: the model's text changes are dropped; asked-for wording changes stay.
        QVERIFY(!asksForTextChanges(QStringLiteral("把状态是已完成的那几行加上浅绿色底色")));
        QVERIFY(!asksForTextChanges(QStringLiteral("Make the header row bold")));
        QVERIFY(asksForTextChanges(QStringLiteral("表头加粗，日期统一写成 11月3日")));
        QVERIFY(asksForTextChanges(QStringLiteral("空着的格子填上未到，并标成红色")));
        QVERIFY(asksForTextChanges(QStringLiteral("检查一下表格，有错改正后标黄")));
        // A plain table: header and the number column; nothing for the text columns.
        const auto grid = [](const QList<QStringList>& rows) {
            QJsonArray array;
            for (const QStringList& row : rows) {
                QJsonArray cells;
                for (const QString& text : row) cells.append(QJsonObject{{QStringLiteral("text"), text}});
                array.append(cells);
            }
            return tableGridFrom({{QStringLiteral("rows"), array}});
        };
        const TableGrid plain = grid({{QStringLiteral("部门"), QStringLiteral("负责人"), QStringLiteral("人数")},
                                      {QStringLiteral("市场部"), QStringLiteral("周岚"), QStringLiteral("12")},
                                      {QStringLiteral("技术部"), QStringLiteral("吴迪"), QStringLiteral("30")}});
        QList<PlannedEdit> edits = defaultTableStyle(plain, 2);
        QCOMPARE(edits.size(), 2);
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("1"));
        QVERIFY(edits[0].command.arguments.value(QStringLiteral("bold")).toBool());
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("table")).toInt(), 2);
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("3"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("right"));
        for (const PlannedEdit& edit : edits) QVERIFY(validateCommand(edit.command).ok);
        // Already like that: nothing.
        QJsonArray styled;
        for (int r = 0; r < 3; ++r) {
            QJsonArray cells;
            for (int c = 0; c < 3; ++c) {
                QJsonObject cell{{QStringLiteral("text"), plain[r][c].text}};
                if (r == 0) {
                    cell.insert(QStringLiteral("bold"), true);
                    cell.insert(QStringLiteral("align"), QStringLiteral("center"));
                    cell.insert(QStringLiteral("fill"), QStringLiteral("#EEEEEE"));
                } else if (c == 2) {
                    cell.insert(QStringLiteral("align"), QStringLiteral("right"));
                }
                cells.append(cell);
            }
            styled.append(cells);
        }
        QVERIFY(defaultTableStyle(tableGridFrom({{QStringLiteral("rows"), styled}}), 1).isEmpty());
        // A document: title, numbered subheadings, the closing lines.
        const auto block = [](int number, const QString& text, const QJsonObject& extra = {}) {
            QJsonObject object{{QStringLiteral("id"), QStringLiteral("P%1").arg(number)}, {QStringLiteral("text"), text}};
            for (auto it = extra.begin(); it != extra.end(); ++it) object.insert(it.key(), it.value());
            return object;
        };
        const QList<OutlineParagraph> letter = outlineParagraphs(QJsonArray{
            block(1, QStringLiteral("关于调整午休时间的说明")),
            block(2, QStringLiteral("各位同事：")),
            block(3, QStringLiteral("一、调整原因")),
            block(4, QStringLiteral("夏季天气炎热，为了大家休息得更好，决定调整午休时间。")),
            block(5, QStringLiteral("二、新的时间")),
            block(6, QStringLiteral("12:00 至 13:30。")),
            block(7, QStringLiteral("综合管理部")),
            block(8, QStringLiteral("2026年6月1日"))});
        edits = defaultDocumentStyle(letter);
        QCOMPARE(edits.size(), 3);
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("paragraphs")).toString(), QStringLiteral("1"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("center"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("paragraphs")).toString(), QStringLiteral("3,5"));
        QCOMPARE(edits[2].command.arguments.value(QStringLiteral("paragraphs")).toString(), QStringLiteral("7,8"));
        QCOMPARE(edits[2].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("right"));
        for (const PlannedEdit& edit : edits) QVERIFY(validateCommand(edit.command).ok);
        // A title that is already set is left alone.
        const QList<OutlineParagraph> done = outlineParagraphs(QJsonArray{
            block(1, QStringLiteral("会议通知"), {{QStringLiteral("bold"), true}, {QStringLiteral("align"), QStringLiteral("center")},
                                                {QStringLiteral("size"), 18}}),
            block(2, QStringLiteral("明天下午开会。"))});
        QVERIFY(defaultDocumentStyle(done).isEmpty());
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

        QCOMPARE(recommendDownload(32 * gb, 12 * gb).name, QStringLiteral("qwen3:4b")); // this computer: 4B is the recommendation
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
        QVERIFY(html.contains(QStringLiteral(">项目</th>")));
        QVERIFY(html.contains(QStringLiteral("<td>3</td>")));
        // Page-wide with a shaded header: without a width the editor squeezes every column.
        QVERIFY(html.contains(QStringLiteral("width:100%")));
        QVERIFY(html.contains(QStringLiteral("background-color:#D9E2F3")));

        // The user's screenshot: a "|" line before the separator, the header after it.
        const QString broken = QStringLiteral("|\n|------|------|------|\n| 项目 | 数值 | 单位 |\n| 温度 | 25 | °C |\n| 湿度 | 60 | % |\n| **压力** | 101 | kPa |");
        const QList<QList<QStringList>> tables = markdownTables(broken);
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables.first().size(), 4);
        QCOMPARE(tables.first().first(), (QStringList{QStringLiteral("项目"), QStringLiteral("数值"), QStringLiteral("单位")}));
        const QString fixedHtml = markdownToHtml(broken);
        QCOMPARE(fixedHtml.count(QStringLiteral("<tr>")), 4);
        QCOMPARE(fixedHtml.count(QStringLiteral("</th>")), 3);
        QVERIFY(!fixedHtml.contains(QStringLiteral("\"></th>"))); // no empty header cell
        // Uneven rows are evened out; a lone "a | b" line in a sentence is no table.
        QCOMPARE(markdownTables(QStringLiteral("| 姓名 | 电话 | 备注 |\n|---|---|---|\n| 张三 | 138 |\n| 李四 | 139 | 新 | 多出 |")).first(),
                 (QList<QStringList>{{QStringLiteral("姓名"), QStringLiteral("电话"), QStringLiteral("备注")},
                                     {QStringLiteral("张三"), QStringLiteral("138"), QString()},
                                     {QStringLiteral("李四"), QStringLiteral("139"), QStringLiteral("新 | 多出")}}));
        QVERIFY(markdownTables(QStringLiteral("选 A | 选 B 都可以。")).isEmpty());
        // What the 1.7B wrote for "两列：姓名、电话": an HTML table. The program reads it as a table.
        const QString htmlTable = QStringLiteral(
            "<p>联系人如下：</p>\n<table border=\"1\">\n<thead>\n<tr>\n<th>姓名</th>\n<th>电话</th>\n</tr>\n</thead>\n<tbody>\n"
            "<tr>\n<td>张三</td>\n<td>13800000001</td>\n</tr>\n<tr><td><b>李四</b></td><td>138&nbsp;0000&#48;002</td></tr>\n"
            "<tr><td>王五 | 小王<td>13800000003</tr>\n</tbody>\n</table>\n<h2>备注</h2><ul><li>甲 &amp; 乙</li><li>丙</li></ul>");
        const QString converted = htmlAsMarkdown(htmlTable);
        QCOMPARE(markdownTables(converted).size(), 1);
        QCOMPARE(markdownTables(converted).first(),
                 (QList<QStringList>{{QStringLiteral("姓名"), QStringLiteral("电话")},
                                     {QStringLiteral("张三"), QStringLiteral("13800000001")},
                                     {QStringLiteral("李四"), QStringLiteral("138 00000002")},
                                     {QStringLiteral("王五 ｜ 小王"), QStringLiteral("13800000003")}}));
        QVERIFY(converted.startsWith(QStringLiteral("联系人如下：\n\n| 姓名 | 电话 |")));
        QVERIFY(converted.endsWith(QStringLiteral("## 备注\n- 甲 & 乙\n- 丙")));
        QVERIFY(!converted.contains(QLatin1Char('<')));
        QVERIFY(markdownToHtml(converted).contains(QStringLiteral("<h2>备注</h2>")));
        // The columns the person named; the header the model left out is put back.
        QCOMPARE(requestedColumns(QStringLiteral("在文档最后加一个表格，两列：姓名、电话。张三 13800000001")),
                 (QStringList{QStringLiteral("姓名"), QStringLiteral("电话")}));
        QCOMPARE(requestedColumns(QStringLiteral("帮我在文末做一个表格，列出温度25℃、湿度60%、压力101kPa，列为项目、数值、单位")),
                 (QStringList{QStringLiteral("项目"), QStringLiteral("数值"), QStringLiteral("单位")}));
        QCOMPARE(requestedColumns(QStringLiteral("做个表，三列，分别是名称、数量、单价")),
                 (QStringList{QStringLiteral("名称"), QStringLiteral("数量"), QStringLiteral("单价")}));
        QCOMPARE(requestedColumns(QStringLiteral("表头是日期/事项")), (QStringList{QStringLiteral("日期"), QStringLiteral("事项")}));
        QVERIFY(requestedColumns(QStringLiteral("在文末加一个表格")).isEmpty());
        const QStringList contacts{QStringLiteral("姓名"), QStringLiteral("电话")};
        const QString noHeader = QStringLiteral("联系人：\n| 张三 | 13800000001 |\n|---|---|\n| 李四 | 13800000002 |\n| 王五 | 13800000003 |\n完");
        const QString headed = withTableHeader(noHeader, contacts);
        QVERIFY(headed.startsWith(QStringLiteral("联系人：\n| 姓名 | 电话 |")) && headed.endsWith(QStringLiteral("\n完")));
        QCOMPARE(markdownTables(headed).first().size(), 4);
        QCOMPARE(markdownTables(headed).first().at(1), (QStringList{QStringLiteral("张三"), QStringLiteral("13800000001")}));
        // The 4B's "| - | |" before the rows is no header: dropped, and the names put in its place.
        const QString dashes = QStringLiteral("| - | |\n|---|---|\n| 11月20日 | 交周报 |\n| 11月21日 | 开例会 |");
        QCOMPARE(markdownTables(dashes).first(), (QList<QStringList>{{QStringLiteral("11月20日"), QStringLiteral("交周报")},
                                                                    {QStringLiteral("11月21日"), QStringLiteral("开例会")}}));
        QCOMPARE(markdownTables(withTableHeader(dashes, {QStringLiteral("日期"), QStringLiteral("事项")})).first().first(),
                 (QStringList{QStringLiteral("日期"), QStringLiteral("事项")}));
        // A header in other words, a table that has it, or rows of words only: left as they are.
        for (const QString& kept : {QStringLiteral("| 名字 | 手机号 |\n|---|---|\n| 李四 | 13800000002 |"),
                                    QStringLiteral("| 姓名 | 电话 |\n|---|---|\n| 李四 | 13800000002 |"),
                                    QStringLiteral("| 张三 | 销售部 |\n|---|---|\n| 李四 | 市场部 |"),
                                    QStringLiteral("| 张三 | 138 | 备注 |\n|---|---|---|\n| 李四 | 139 | 无 |")})
            QCOMPARE(withTableHeader(kept, contacts), kept);
        // Text without HTML stays exactly as it is, "<" and all.
        for (const QString& kept : {QStringLiteral("a < b，且 b > c。"), QStringLiteral("## 标题\n| 甲 | 乙 |\n|---|---|\n| 1 | 2 |"),
                                    QStringLiteral("<自定义>不是网页标记")})
            QCOMPARE(htmlAsMarkdown(kept), kept);
        // After inserting: found as meant, wider than half the page; or what is wrong, in words.
        const auto block = [](int rows, int columns, const QStringList& first, int width) {
            return QJsonObject{{QStringLiteral("id"), QStringLiteral("T1")}, {QStringLiteral("rows"), rows}, {QStringLiteral("columns"), columns},
                               {QStringLiteral("firstRow"), QJsonArray::fromStringList(first)}, {QStringLiteral("width"), width}};
        };
        const QStringList header{QStringLiteral("项目"), QStringLiteral("数值"), QStringLiteral("单位")};
        QVERIFY(insertedTableProblems(tables, QJsonArray{block(4, 3, header, 100)}).isEmpty());
        QVERIFY(insertedTableProblems(tables, QJsonArray{block(5, 3, {QString()}, 30)}).contains(QStringLiteral("没有找到")));
        QVERIFY(insertedTableProblems(tables, QJsonArray{block(4, 3, header, 30)}).contains(QStringLiteral("太窄")));
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
        // A note about the answer at its end is dropped (seen with the 1.7B model).
        QCOMPARE(cleanRewrite(QStringLiteral("第一段。\n\n第二段。\n\n（注：以上内容基于现有信息进行合理扩展。）")),
                 QStringLiteral("第一段。\n\n第二段。"));
        QCOMPARE(cleanRewrite(QStringLiteral("注意安全。")), QStringLiteral("注意安全。")); // a sentence, not a note
        // Markdown's two-space line breaks must not reach the document (seen in a real session).
        QCOMPARE(cleanRewrite(QStringLiteral("关于开展安全培训的通知  \n各部门：  \n请准时参加。\t\n行政部")),
                 QStringLiteral("关于开展安全培训的通知\n各部门：\n请准时参加。\n行政部"));
    }

    void conversationsAreKeptOnePerFile()
    {
        QTemporaryDir folder;
        ConversationStore store(folder.path());
        Conversation first = ConversationStore::create(QStringLiteral("D:/文档/通知.docx"), QStringLiteral("通知.docx"));
        first.title = ConversationStore::titleFrom(QStringLiteral("/插入 一段很长很长的文字，用来看看标题会不会被截短到二十四个字以内"));
        QCOMPARE(first.title, QStringLiteral("插入 一段很长很长的文字，用来看看标题会不会被截…"));
        first.messages = {{QStringLiteral("user"), QString(), QStringLiteral("你好"), true, false},
                          {QStringLiteral("assistant"), QStringLiteral("本地 AI"), QStringLiteral("**你好**"), true, true}};
        first.modelHistory = QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                    {QStringLiteral("content"), QStringLiteral("你好")}}};
        QVERIFY(store.save(first));
        Conversation second = ConversationStore::create(QString(), QStringLiteral("未命名文档 1"));
        second.updated = first.updated.addSecs(60);
        second.messages = {{QStringLiteral("user"), QString(), QStringLiteral("再见"), true, false}};
        QVERIFY(store.save(second));

        const QList<Conversation> listed = ConversationStore(folder.path()).list(); // read back from the files
        QCOMPARE(listed.size(), 2);
        QCOMPARE(listed[0].id, second.id); // newest first
        const Conversation& back = listed[1];
        QCOMPARE(back.documentPath, QStringLiteral("D:/文档/通知.docx"));
        QCOMPARE(back.messages.size(), 2);
        QVERIFY(back.messages[1].markdown);
        QCOMPARE(back.modelHistory, first.modelHistory);

        QVERIFY(store.rename(first.id, QStringLiteral("  我起的名字 ")));
        QCOMPARE(store.load(first.id)->title, QStringLiteral("我起的名字"));
        QVERIFY(store.load(first.id)->titleChosen);
        QVERIFY(!store.load(QStringLiteral("../evil")));  // ids are never paths
        QVERIFY(!store.remove(QStringLiteral("..\\x")));
        QCOMPARE(store.removeAllExcept(second.id), 1);
        QCOMPARE(store.list().size(), 1);
        QVERIFY(store.remove(second.id));
        QVERIFY(store.list().isEmpty());

        ConversationStore memory; // no folder: kept while the program runs
        QVERIFY(memory.save(first));
        QCOMPARE(memory.list().size(), 1);
    }

    void structureAndStyleCommandsTakeTypedArguments()
    {
        const auto check = [](const QString& name, const QJsonObject& arguments) {
            return validateCommand({name, arguments, CommandSource::Assistant});
        };
        QVERIFY(check(QStringLiteral("table.setCell"), {{QStringLiteral("table"), 1}, {QStringLiteral("row"), 2},
                                                         {QStringLiteral("column"), 3}, {QStringLiteral("text"), QStringLiteral("1200")}}).ok);
        // Small models often quote numbers; that is accepted.
        QVERIFY(check(QStringLiteral("table.setCell"), {{QStringLiteral("table"), QStringLiteral("1")}, {QStringLiteral("row"), QStringLiteral("2")},
                                                         {QStringLiteral("column"), QStringLiteral("3")}, {QStringLiteral("text"), QStringLiteral("x")}}).ok);
        QCOMPARE(check(QStringLiteral("table.setCell"), {{QStringLiteral("table"), 0}, {QStringLiteral("row"), 2},
                                                          {QStringLiteral("column"), 3}, {QStringLiteral("text"), QStringLiteral("x")}}).code,
                 QStringLiteral("invalid_argument"));
        QCOMPARE(check(QStringLiteral("table.setCell"), {{QStringLiteral("table"), 1.5}, {QStringLiteral("row"), 2},
                                                          {QStringLiteral("column"), 3}, {QStringLiteral("text"), QStringLiteral("x")}}).code,
                 QStringLiteral("invalid_argument"));
        QVERIFY(check(QStringLiteral("format.cells"), {{QStringLiteral("table"), 1}, {QStringLiteral("rows"), QStringLiteral("1")},
                                                        {QStringLiteral("bold"), true}, {QStringLiteral("fill"), QStringLiteral("#D9E2F3")},
                                                        {QStringLiteral("align"), QStringLiteral("center")}}).ok);
        QVERIFY(check(QStringLiteral("format.cells"), {{QStringLiteral("table"), 1}, {QStringLiteral("fill"), QStringLiteral("none")}}).ok);
        QCOMPARE(check(QStringLiteral("format.cells"), {{QStringLiteral("table"), 1}, {QStringLiteral("color"), QStringLiteral("none")}}).code,
                 QStringLiteral("invalid_argument")); // text color cannot be "none"
        QCOMPARE(check(QStringLiteral("format.cells"), {{QStringLiteral("table"), 1}, {QStringLiteral("bold"), QStringLiteral("yes")}}).code,
                 QStringLiteral("invalid_argument"));
        QCOMPARE(check(QStringLiteral("format.cells"), {{QStringLiteral("table"), 1}, {QStringLiteral("color"), QStringLiteral("red")}}).code,
                 QStringLiteral("invalid_argument"));
        QVERIFY(check(QStringLiteral("format.paragraphs"), {{QStringLiteral("paragraphs"), QStringLiteral("2-3")},
                                                            {QStringLiteral("style"), QStringLiteral("heading2")}}).ok);
        QCOMPARE(check(QStringLiteral("format.text"), {{QStringLiteral("find"), QStringLiteral("重要")}, {QStringLiteral("fontSize"), 100}}).code,
                 QStringLiteral("invalid_argument"));

        // As tools: real JSON types, so a model or MCP client sends numbers and booleans.
        QHash<QString, QJsonObject> tools;
        for (const QJsonValue& tool : commandCatalogJson()) tools.insert(tool.toObject().value(QStringLiteral("name")).toString(), tool.toObject());
        const auto property = [&tools](const QString& tool, const QString& name) {
            return tools.value(tool).value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("properties")).toObject()
                .value(name).toObject();
        };
        QCOMPARE(property(QStringLiteral("table.setCell"), QStringLiteral("row")).value(QStringLiteral("type")).toString(), QStringLiteral("integer"));
        QCOMPARE(property(QStringLiteral("table.setCell"), QStringLiteral("row")).value(QStringLiteral("minimum")).toInt(), 1);
        QCOMPARE(property(QStringLiteral("format.cells"), QStringLiteral("bold")).value(QStringLiteral("type")).toString(), QStringLiteral("boolean"));
        QVERIFY(property(QStringLiteral("format.cells"), QStringLiteral("fill")).value(QStringLiteral("pattern")).toString().contains(QStringLiteral("none")));
        const auto readOnly = [&tools](const QString& tool) {
            return tools.value(tool).value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool();
        };
        QVERIFY(readOnly(QStringLiteral("document.outline")) && readOnly(QStringLiteral("table.read")) && readOnly(QStringLiteral("selection.select")));
        QVERIFY(!readOnly(QStringLiteral("table.setCell")) && !readOnly(QStringLiteral("format.cells")) && !readOnly(QStringLiteral("format.text")));
    }

    void tablePlansAreCheckedAgainstTheTable()
    {
        const auto cell = [](const QString& text, bool bold = false) {
            QJsonObject object{{QStringLiteral("text"), text}};
            if (bold) object.insert(QStringLiteral("bold"), true);
            return object;
        };
        const TableGrid grid = tableGridFrom({{QStringLiteral("rows"), QJsonArray{
            QJsonArray{cell(QStringLiteral("品名")), cell(QStringLiteral("单价")), cell(QStringLiteral("数量")), cell(QStringLiteral("金额"))},
            QJsonArray{cell(QStringLiteral("笔计本")), cell(QStringLiteral("12")), cell(QStringLiteral("10")), cell(QStringLiteral("120"))},
            QJsonArray{cell(QStringLiteral("签字笔")), cell(QStringLiteral("3")), cell(QStringLiteral("20")), cell(QString())}}}});
        const QString prompt = tableForPrompt(grid, 1);
        QVERIFY(prompt.contains(QStringLiteral("表格 T1，3 行 4 列")));
        QVERIFY(prompt.contains(QStringLiteral("R2C1：笔计本")));
        QVERIFY(prompt.contains(QStringLiteral("R3C4：（空）")));
        QCOMPARE(tablePlace(1, 1, 1, 4), QStringLiteral("第 1 行第 1–4 列"));
        QCOMPARE(tablePlace(2, 3, 5, 3), QStringLiteral("第 2–5 行第 3 列"));

        const QString answer = QStringLiteral(R"({"plan":[
            {"cells":"R2C1","change":"text","reason":"错别字","value":"笔记本"},
            {"cells":"R3C4","change":"text","reason":"3×20=60","value":"60"},
            {"cells":"R1C1-R1C4","change":"bold","reason":"表头加粗","value":"true"},
            {"cells":"R1","change":"align","reason":"表头居中","value":"居中"},
            {"cells":"C2","change":"align","reason":"数字右对齐","value":"right"},
            {"cells":"R2C2","change":"text","reason":"没变","value":"12"},
            {"cells":"R9C9","change":"text","reason":"没有这格","value":"x"},
            {"cells":"R2C1-R2C2","change":"text","reason":"文字只能一格一格改","value":"x"},
            {"cells":"r1c1","change":"fill","reason":"底色","value":"#d9e2f3"},
            {"cells":"R1C1","change":"fill","reason":"不是颜色","value":"好看一点"},
            {"cells":"R2C4","change":"fontSize","reason":"太大","value":"100"},
            {"cells":"R1C1-R1C4","change":"bold","reason":"重复","value":true},
            {"cells":"R2C3","change":"underline","reason":"不支持","value":"true"}]})");
        const QList<PlannedEdit> plan = parseTablePlan(answer, grid, 1);
        QCOMPARE(plan.size(), 6);
        QCOMPARE(plan[0].command.name, QStringLiteral("table.setCell"));
        QCOMPARE(plan[0].command.arguments.value(QStringLiteral("row")).toInt(), 2);
        QCOMPARE(plan[0].command.arguments.value(QStringLiteral("text")).toString(), QStringLiteral("笔记本"));
        QCOMPARE(plan[0].place, QStringLiteral("第 2 行第 1 列"));
        QCOMPARE(plan[0].before, QStringLiteral("笔计本"));
        QCOMPARE(plan[1].command.arguments.value(QStringLiteral("column")).toInt(), 4);
        QCOMPARE(plan[2].command.name, QStringLiteral("format.cells"));
        QCOMPARE(plan[2].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("1"));
        QCOMPARE(plan[2].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("1-4"));
        QVERIFY(plan[2].command.arguments.value(QStringLiteral("bold")).toBool());
        QCOMPARE(plan[3].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("center"));
        QCOMPARE(plan[4].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("1-3"));
        QCOMPARE(plan[4].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("2"));
        QCOMPARE(plan[5].command.arguments.value(QStringLiteral("fill")).toString(), QStringLiteral("#D9E2F3"));
        for (const PlannedEdit& edit : plan) QVERIFY(validateCommand(edit.command).ok);

        // Nothing that is already so: a bold header stays as it is.
        const TableGrid boldHeader = tableGridFrom({{QStringLiteral("rows"), QJsonArray{
            QJsonArray{cell(QStringLiteral("姓名"), true), cell(QStringLiteral("电话"), true)}}}});
        QVERIFY(parseTablePlan(QStringLiteral(R"({"plan":[{"cells":"R1","change":"bold","reason":"r","value":"true"}]})"), boldHeader, 2).isEmpty());
        QVERIFY(parseTablePlan(QStringLiteral("不是 JSON"), grid, 1).isEmpty());

        // The ways a small model really words a plan (Qwen3 1.7B on the built-in engine):
        // "bold: true", "align: center", color names, cell lists, Chinese font sizes.
        const QList<PlannedEdit> worded = parseTablePlan(QStringLiteral(R"({"plan":[
            {"cells":"R1C1-R1C4","change":"bold","reason":"r","value":"bold: true"},
            {"cells":"R1C1 - R1C4","change":"align","reason":"r","value":"align: center"},
            {"cells":"R1","change":"fill","reason":"r","value":"浅蓝色"},
            {"cells":"R2C4，R3C4","change":"text","reason":"r","value":"text: 0"},
            {"cells":"R2C2、R3C2","change":"对齐","reason":"r","value":"右对齐"},
            {"cells":"R2C1","change":"fontSize","reason":"r","value":"小四"},
            {"cells":"R3C1","change":"color","reason":"r","value":"red"},
            {"cells":"R1C1-R1C4","change":"bold","reason":"r","value":"取消加粗"}]})"), grid, 1);
        QCOMPARE(worded.size(), 9);
        QVERIFY(worded[0].command.arguments.value(QStringLiteral("bold")).toBool());
        QCOMPARE(worded[1].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("center"));
        QCOMPARE(worded[1].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("1-4"));
        QCOMPARE(worded[2].command.arguments.value(QStringLiteral("fill")).toString(), QStringLiteral("#DDEBF7")); // light, for a fill
        QCOMPARE(worded[3].command.arguments.value(QStringLiteral("row")).toInt(), 2); // a list: one change per cell
        QCOMPARE(worded[3].command.arguments.value(QStringLiteral("text")).toString(), QStringLiteral("0"));
        QCOMPARE(worded[4].command.arguments.value(QStringLiteral("row")).toInt(), 3);
        QCOMPARE(worded[5].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("right"));
        QCOMPARE(worded[6].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("3"));
        QCOMPARE(worded[7].command.arguments.value(QStringLiteral("fontSize")).toInt(), 12);
        QCOMPARE(worded[8].command.arguments.value(QStringLiteral("color")).toString(), QStringLiteral("#C00000")); // strong, for text
        // "取消加粗" on a header that is not bold: nothing to do, so it is not listed.
        for (const PlannedEdit& edit : worded) QVERIFY(validateCommand(edit.command).ok);

        // When a typed request is about a table.
        QVERIFY(tableRequest(QStringLiteral("帮我检查一下表格"), false));
        QVERIFY(tableRequest(QStringLiteral("把表头加粗居中"), false));
        QVERIFY(tableRequest(QStringLiteral("看看平均分有没有算错"), true));
        QVERIFY(tableRequest(QStringLiteral("帮我改改"), true));
        QVERIFY(!tableRequest(QStringLiteral("帮我改改"), false));
        QVERIFY(!tableRequest(QStringLiteral("这张表是什么意思？"), true));
        QVERIFY(!tableRequest(QStringLiteral("做一个表格"), false));
        // A new table, wherever the words are, even with the cursor in a table.
        for (const char* request : {"帮我在文末做一个表格，列出温度、湿度", "在文档最后加一个表格，两列：姓名、电话",
                                    "插入表格", "把下面的数据整理成表格", "在后面再加一张新表格", "列一个表格比较三种方案"})
            QVERIFY2(!tableRequest(QString::fromUtf8(request), true), request);
        // Changes to the table that is there stay table requests.
        for (const char* request : {"增加表格的行高", "表格加一个表头", "给表格增加一列", "在表格里插入一行合计",
                                    "把表格做成三列", "表格的字体加粗"})
            QVERIFY2(tableRequest(QString::fromUtf8(request), false), request);
        QVERIFY(!tableRequest(QStringLiteral("/保存"), true));
        QCOMPARE(tableNumberIn(QStringLiteral("把第二张表格的表头加粗")), 2);
        QCOMPARE(tableNumberIn(QStringLiteral("表3里的数字右对齐")), 3);
        QCOMPARE(tableNumberIn(QStringLiteral("检查 T2")), 2);
        QCOMPARE(tableNumberIn(QStringLiteral("把表头加粗")), 0);

        const TaskSpec* task = findTask(QStringLiteral("table"));
        QVERIFY(task && !task->offered && task->output == TaskOutput::TablePlan);
        const QJsonArray messages = taskMessages(*task, {prompt, QStringLiteral("帮我检查一下表格"), QString()});
        const QString user = messages.last().toObject().value(QStringLiteral("content")).toString();
        QVERIFY(user.contains(QStringLiteral("帮我检查一下表格")) && user.contains(QStringLiteral("R2C1：笔计本"))
                && user.contains(QStringLiteral("不要把整张表重写")));
        const QJsonObject schema = taskFormat(*task);
        QVERIFY(schema.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("plan")));
    }

    void tableArithmeticIsTheProgramsJob()
    {
        // Tables unlike the live-test samples: money with signs and decimals, thousands separators.
        const auto grid = [](const QList<QStringList>& rows) {
            QJsonArray array;
            for (const QStringList& row : rows) {
                QJsonArray cells;
                for (const QString& text : row) cells.append(QJsonObject{{QStringLiteral("text"), text}});
                array.append(cells);
            }
            return tableGridFrom({{QStringLiteral("rows"), array}});
        };
        double value = 0;
        QVERIFY(cellNumber(QStringLiteral("¥8.50"), &value) && value == 8.5);
        QVERIFY(cellNumber(QStringLiteral("1,200"), &value) && value == 1200);
        QVERIFY(cellNumber(QStringLiteral("85 g"), &value) && value == 85);
        QVERIFY(cellNumber(QStringLiteral("120元"), &value) && value == 120);
        for (const char* text : {"2026-11-04", "09:00-11:30", "11月3日", "AA x1", "138 0013 8000", "", "M350 Pro"})
            QVERIFY2(!cellNumber(QString::fromUtf8(text), &value), text);

        const TableGrid expenses = grid({{QStringLiteral("项目"), QStringLiteral("单价"), QStringLiteral("数量"), QStringLiteral("小计")},
                                         {QStringLiteral("打印纸"), QStringLiteral("¥25.00"), QStringLiteral("4"), QStringLiteral("¥100.00")},
                                         {QStringLiteral("墨盒"), QStringLiteral("¥180.00"), QStringLiteral("2"), QStringLiteral("¥360.00")},
                                         {QStringLiteral("快递"), QStringLiteral("¥12.50"), QStringLiteral("3"), QStringLiteral("¥36.50")},
                                         {QStringLiteral("胶带"), QStringLiteral("¥4.00"), QStringLiteral("6"), QStringLiteral("¥24.00")}});
        QList<TableFormula> formulas = tableFormulas(expenses);
        QCOMPARE(formulas.size(), 1);
        QCOMPARE(formulas[0].column, 4);
        QVERIFY(formulas[0].product);
        QCOMPARE(formulas[0].from, (QList<int>{2, 3}));
        QCOMPARE(formulas[0].wrong.size(), 1);
        QCOMPARE(formulas[0].wrong[0].row, 4);
        QCOMPARE(formulas[0].wrong[0].after, QStringLiteral("¥37.50")); // written the way the column is
        const QString facts = formulaFacts(expenses, formulas);
        QVERIFY2(facts.contains(QStringLiteral("第 4 列（小计） = 第 2 列 × 第 3 列（单价 × 数量）")), qPrintable(facts));
        QVERIFY2(facts.contains(QStringLiteral("R4C4 应为 ¥37.50（表里写的是 ¥36.50）")), qPrintable(facts));

        // The model's plan is held to it: a miscalculation and a change to a number that
        // adds up are dropped; a right answer is kept in the column's own format.
        const QString answer = QStringLiteral(R"({"plan":[
            {"cells":"R4C4","change":"text","reason":"算错","value":"¥38.00"},
            {"cells":"R2C4","change":"text","reason":"看着不对","value":"¥90.00"},
            {"cells":"R3C1","change":"text","reason":"写全","value":"墨盒（黑色）"},
            {"cells":"R1","change":"bold","reason":"表头","value":"true"}]})");
        QList<PlannedEdit> plan = withFormulaFixes(parseTablePlan(answer, expenses, 1), expenses, 1, formulas, true);
        QCOMPARE(plan.size(), 3);
        QCOMPARE(plan[0].command.arguments.value(QStringLiteral("text")).toString(), QStringLiteral("¥37.50"));
        QCOMPARE(plan[0].command.arguments.value(QStringLiteral("row")).toInt(), 4);
        QVERIFY(plan[0].reason.contains(QStringLiteral("单价 × 数量")));
        QCOMPARE(plan[1].after, QStringLiteral("墨盒（黑色）"));
        QCOMPARE(plan[2].command.name, QStringLiteral("format.cells"));
        // Not asked to check: nothing is added, the wrong numbers still go.
        plan = withFormulaFixes(parseTablePlan(answer, expenses, 1), expenses, 1, formulas, false);
        QCOMPARE(plan.size(), 2);
        // The model got it right: one change, not two, with the column's sign kept.
        plan = withFormulaFixes(parseTablePlan(QStringLiteral(R"({"plan":[{"cells":"R4C4","change":"text","reason":"r","value":"37.5"}]})"),
                                               expenses, 1), expenses, 1, formulas, true);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan[0].command.arguments.value(QStringLiteral("text")).toString(), QStringLiteral("¥37.50"));
        for (const PlannedEdit& edit : plan) QVERIFY(validateCommand(edit.command).ok);

        // A sum of the columns before it, with thousands separators.
        const TableGrid sales = grid({{QStringLiteral("门店"), QStringLiteral("一月"), QStringLiteral("二月"), QStringLiteral("三月"), QStringLiteral("季度合计")},
                                      {QStringLiteral("东区"), QStringLiteral("1,200"), QStringLiteral("1,350"), QStringLiteral("1,100"), QStringLiteral("3,650")},
                                      {QStringLiteral("西区"), QStringLiteral("980"), QStringLiteral("1,020"), QStringLiteral("1,005"), QStringLiteral("3,005")},
                                      {QStringLiteral("南区"), QStringLiteral("1,500"), QStringLiteral("1,480"), QStringLiteral("1,620"), QStringLiteral("4,500")}});
        formulas = tableFormulas(sales);
        QCOMPARE(formulas.size(), 1);
        QVERIFY(!formulas[0].product);
        QCOMPARE(formulas[0].from, (QList<int>{2, 3, 4}));
        QCOMPARE(formulas[0].wrong.size(), 1);
        QCOMPARE(formulas[0].wrong[0].after, QStringLiteral("4,600"));
        QVERIFY(formulaFacts(sales, formulas).contains(QStringLiteral("第 2–4 列之和")));

        // A balance: last month + income − spending (one row wrong: 500 + 300 − 420 is 380).
        const TableGrid balance = grid({{QStringLiteral("账户"), QStringLiteral("上月"), QStringLiteral("收入"), QStringLiteral("支出"), QStringLiteral("余额")},
                                        {QStringLiteral("现金"), QStringLiteral("1200"), QStringLiteral("800"), QStringLiteral("650"), QStringLiteral("1350")},
                                        {QStringLiteral("备用金"), QStringLiteral("500"), QStringLiteral("300"), QStringLiteral("420"), QStringLiteral("280")},
                                        {QStringLiteral("活动经费"), QStringLiteral("2000"), QStringLiteral("0"), QStringLiteral("760"), QStringLiteral("1240")},
                                        {QStringLiteral("零用"), QStringLiteral("90"), QStringLiteral("60"), QStringLiteral("35"), QStringLiteral("115")}});
        formulas = tableFormulas(balance);
        QCOMPARE(formulas.size(), 1);
        QCOMPARE(formulas[0].column, 5);
        QCOMPARE(formulas[0].signs, (QList<int>{1, 1, -1}));
        QCOMPARE(formulas[0].wrong.size(), 1);
        QCOMPARE(formulas[0].wrong[0].row, 3);
        QCOMPARE(formulas[0].wrong[0].after, QStringLiteral("380"));
        QVERIFY2(formulaFacts(balance, formulas).contains(QStringLiteral("第 2 列 + 第 3 列 − 第 4 列（上月 + 收入 − 支出）")),
                 qPrintable(formulaFacts(balance, formulas)));
        plan = withFormulaFixes({}, balance, 1, formulas, true);
        QCOMPARE(plan.size(), 1);
        QVERIFY(plan[0].reason.contains(QStringLiteral("上月 + 收入 − 支出")));

        // Too little evidence (two rows), or no relation at all: nothing is claimed.
        QVERIFY(tableFormulas(grid({{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                                    {QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("6")},
                                    {QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("20")}})).isEmpty());
        QVERIFY(tableFormulas(grid({{QStringLiteral("姓名"), QStringLiteral("年龄"), QStringLiteral("工号")},
                                    {QStringLiteral("甲"), QStringLiteral("31"), QStringLiteral("1024")},
                                    {QStringLiteral("乙"), QStringLiteral("27"), QStringLiteral("1187")},
                                    {QStringLiteral("丙"), QStringLiteral("45"), QStringLiteral("1302")}})).isEmpty());

        QVERIFY(checkRequest(QStringLiteral("帮我检查一下这个表格")));
        QVERIFY(checkRequest(QStringLiteral("看看总分有没有算错")));
        QVERIFY(checkRequest(QStringLiteral("Please check the totals")));
        QVERIFY(!checkRequest(QStringLiteral("把表头加粗居中")));
        QVERIFY(!checkRequest(QStringLiteral("In the table, make the header row bold")));

        // Naming a column points at its table; a request to write something is not about one.
        const QJsonArray blocks{QJsonObject{{QStringLiteral("id"), QStringLiteral("P1")}},
                                QJsonObject{{QStringLiteral("id"), QStringLiteral("T1")},
                                            {QStringLiteral("firstRow"), QJsonArray{QStringLiteral("姓名"), QStringLiteral("1"), QStringLiteral("总分")}}},
                                QJsonObject{{QStringLiteral("id"), QStringLiteral("T2")},
                                            {QStringLiteral("firstRow"), QJsonArray{QStringLiteral("事项"), QStringLiteral("负责人")}}}};
        QCOMPARE(tableMentionedIn(QStringLiteral("看看总分有没有算错"), blocks), 1);
        QCOMPARE(tableMentionedIn(QStringLiteral("负责人都写全名"), blocks), 2);
        QCOMPARE(tableMentionedIn(QStringLiteral("第1条要改"), blocks), 0); // a number is not a name
        QVERIFY(!tableRequest(QStringLiteral("帮我写一段关于总分的说明"), true));
    }

    void aCleanUpTheModelBeganIsFinished()
    {
        const auto grid = [](const QList<QStringList>& rows) {
            QJsonArray array;
            for (const QStringList& row : rows) {
                QJsonArray cells;
                for (const QString& text : row) cells.append(QJsonObject{{QStringLiteral("text"), text}});
                array.append(cells);
            }
            return tableGridFrom({{QStringLiteral("rows"), array}});
        };
        const TableGrid phones = grid({{QStringLiteral("单位"), QStringLiteral("座机"), QStringLiteral("邮箱")},
                                       {QStringLiteral("总部"), QStringLiteral("010-6552-1234"), QStringLiteral("hq@example.cn")},
                                       {QStringLiteral("上海"), QStringLiteral("021 5478 9900"), QStringLiteral("sh@example,cn")},
                                       {QStringLiteral("深圳"), QStringLiteral("0755-8833-112"), QStringLiteral("sz@example.cn")},
                                       {QStringLiteral("成都"), QStringLiteral("已停用"), QStringLiteral("cd@example.cn")},
                                       {QStringLiteral("杭州"), QStringLiteral("0571 8800 99"), QStringLiteral("hz@example,cn")}});
        const QString answer = QStringLiteral(R"({"plan":[
            {"cells":"R2C2","change":"text","reason":"去掉横线","value":"01065521234"},
            {"cells":"R3C2","change":"text","reason":"去掉空格","value":"02154789900"},
            {"cells":"R3C3","change":"text","reason":"逗号改点","value":"sh@example.cn"}]})");
        const QList<PlannedEdit> model = parseTablePlan(answer, phones, 1);
        QCOMPARE(model.size(), 3);
        const QList<PlannedEdit> plan = withSameCleanup(model, phones, 1, QStringLiteral("座机号码统一写成纯数字"));
        // Row 4 becomes 11 digits like the others; row 6 would be 10 digits, so it is left
        // to the person; "已停用" has nothing to take out; the e-mail column was not a clean-up.
        QCOMPARE(plan.size(), 4);
        QCOMPARE(plan[3].command.arguments.value(QStringLiteral("row")).toInt(), 4);
        QCOMPARE(plan[3].command.arguments.value(QStringLiteral("column")).toInt(), 2);
        QCOMPARE(plan[3].after, QStringLiteral("07558833112"));
        QVERIFY(validateCommand(plan[3].command).ok);
        // Not asked to make anything uniform: the plan stays the model's.
        QCOMPARE(withSameCleanup(model, phones, 1, QStringLiteral("把上海的座机改对")).size(), 3);
    }

    void theRequestDecidesWhereAndHow()
    {
        const auto grid = [](const QList<QStringList>& rows) {
            QJsonArray array;
            for (const QStringList& row : rows) {
                QJsonArray cells;
                for (const QString& text : row) cells.append(QJsonObject{{QStringLiteral("text"), text}});
                array.append(cells);
            }
            return tableGridFrom({{QStringLiteral("rows"), array}});
        };
        const TableGrid orders = grid({{QStringLiteral("订单号"), QStringLiteral("客户"), QStringLiteral("发货日期"), QStringLiteral("状态"), QStringLiteral("金额")},
                                       {QStringLiteral("A-01"), QStringLiteral("华东"), QStringLiteral("2026/3/7"), QStringLiteral("已发货"), QStringLiteral("300")},
                                       {QStringLiteral("A-02"), QStringLiteral("华南"), QStringLiteral("3月9日"), QStringLiteral("待发货"), QStringLiteral("120")},
                                       {QStringLiteral("A-03"), QStringLiteral("华北"), QStringLiteral("2026.03.12"), QStringLiteral("已发货"), QStringLiteral("80")}});
        // A confused item ("bold" with a value that is neither yes/no nor the cell's text) is dropped;
        // the cell's own text, or "bold: true; fill: …", counts.
        QList<PlannedEdit> plan = parseTablePlan(QStringLiteral(R"({"plan":[
            {"cells":"R2C5","change":"bold","reason":"r","value":"单价"},
            {"cells":"R1C1","change":"bold","reason":"r","value":"订单号"},
            {"cells":"R1C2-R1C5","change":"bold","reason":"r","value":"客户|发货日期|状态|金额"},
            {"cells":"R3C1","change":"bold","reason":"r","value":"italic: true; fill: #EEEEEE"}]})"), orders, 1);
        QCOMPARE(plan.size(), 4);
        QCOMPARE(plan[0].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("1"));
        QCOMPARE(plan[1].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("2-5"));
        QVERIFY(plan[2].command.arguments.value(QStringLiteral("italic")).toBool());
        QCOMPARE(plan[3].command.arguments.value(QStringLiteral("fill")).toString(), QStringLiteral("#EEEEEE"));

        // "已发货的行": the cell the model marked goes to its whole row; "金额那一列": the header goes to the column.
        plan = parseTablePlan(QStringLiteral(R"({"plan":[
            {"cells":"R2C4","change":"fill","reason":"r","value":"#E2EFDA"},
            {"cells":"R4C4","change":"fill","reason":"r","value":"#E2EFDA"}]})"), orders, 1);
        plan = withRequestedScope(plan, orders, 1, QStringLiteral("已发货的行都标成浅绿色"));
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan[0].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("1-5"));
        QCOMPARE(plan[0].place, QStringLiteral("第 2 行第 1–5 列"));
        plan = parseTablePlan(QStringLiteral(R"({"plan":[{"cells":"R1C5","change":"align","reason":"r","value":"right"}]})"), orders, 1);
        plan = withRequestedScope(plan, orders, 1, QStringLiteral("金额那一列右对齐"));
        QCOMPARE(plan[0].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("1-4"));
        // Formats the request names but the plan left out, added on the plan's place.
        plan = parseTablePlan(QStringLiteral(R"({"plan":[{"cells":"R1C1-R1C5","change":"fill","reason":"r","value":"#FFF2CC"}]})"), orders, 1);
        plan = withRequestedScope(plan, orders, 1, QStringLiteral("表头加粗、居中，底色用浅黄色"));
        QCOMPARE(plan.size(), 3);
        QVERIFY(plan[1].command.arguments.value(QStringLiteral("bold")).toBool());
        QCOMPARE(plan[1].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("1-5"));
        QCOMPARE(plan[2].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("center"));
        for (const PlannedEdit& edit : plan) QVERIFY(validateCommand(edit.command).ok);
        // A request about rows or columns of its own is not completed by guesswork.
        QCOMPARE(withRequestedScope(parseTablePlan(QStringLiteral(R"({"plan":[{"cells":"R1","change":"bold","reason":"r","value":"true"}]})"), orders, 1),
                                    orders, 1, QStringLiteral("表头加粗，金额列右对齐")).size(), 1);

        // Dates are rewritten by the program, in the style the request shows.
        CellDate date;
        QVERIFY(cellDate(QStringLiteral("2026/3/7"), &date) && date.year == 2026 && date.month == 3 && date.day == 7);
        QVERIFY(cellDate(QStringLiteral("3月9日"), &date) && date.year == 0 && date.day == 9);
        QVERIFY(!cellDate(QStringLiteral("2026/13/40"), &date));
        QVERIFY(!cellDate(QStringLiteral("09:00-11:30"), &date));
        const QList<PlannedEdit> model = parseTablePlan(QStringLiteral(R"({"plan":[
            {"cells":"R2C3","change":"text","reason":"抄了例子","value":"2026-03-01"},
            {"cells":"R2C2","change":"text","reason":"写全","value":"华东区"}]})"), orders, 1);
        plan = withDateStyle(model, orders, 1, QStringLiteral("发货日期统一写成 2026-03-01 这样"));
        QCOMPARE(plan.size(), 4);
        QCOMPARE(plan[0].after, QStringLiteral("2026-03-07"));
        QCOMPARE(plan[1].after, QStringLiteral("2026-03-09")); // the column's year
        QCOMPARE(plan[2].after, QStringLiteral("2026-03-12"));
        QCOMPARE(plan[3].after, QStringLiteral("华东区"));
        plan = withDateStyle({}, orders, 1, QStringLiteral("日期都改成“3月1日”的写法"));
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan[0].after, QStringLiteral("3月7日"));
        QCOMPARE(plan[1].after, QStringLiteral("3月12日"));
        QVERIFY(withDateStyle(model, orders, 1, QStringLiteral("把客户写全")).size() == 2); // no date style asked for

        // Typos found by proofreading the cells' words become cell edits.
        const TableGrid items = grid({{QStringLiteral("名称"), QStringLiteral("备注")},
                                      {QStringLiteral("按装费"), QStringLiteral("含上门按装")},
                                      {QStringLiteral("运费"), QStringLiteral("12")}});
        const QString words = tableTexts(items);
        QVERIFY(words.contains(QStringLiteral("按装费")) && !words.contains(QStringLiteral("12")));
        const QList<Correction> typos = parseCorrections(
            QStringLiteral(R"({"corrections":[{"error":"按装","fix":"安装","reason":"错别字"}]})"), words);
        plan = withTypoFixes({}, typos, items, 1);
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan[0].after, QStringLiteral("安装费"));
        QCOMPARE(plan[1].after, QStringLiteral("含上门安装"));
        QVERIFY(words.contains(QStringLiteral("备注：含上门按装"))); // each cell with its column's name
        // The model quoting the column's name, or "fixing" a word into itself.
        plan = withTypoFixes({}, {Correction{QStringLiteral("名称：按装费"), QStringLiteral("名称：安装费"), QString()},
                                  Correction{QStringLiteral("运费"), QStringLiteral("运费"), QString()}}, items, 1);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan[0].after, QStringLiteral("安装费"));
        const TaskSpec* proofread = findTask(QStringLiteral("tableProofread"));
        QVERIFY(proofread && !proofread->offered && proofread->output == TaskOutput::Corrections);
    }

    void theProgramReadsDocumentFormatRequestsItself()
    {
        // An outline as document.outline reports it: P numbers count empty paragraphs too.
        const auto block = [](int number, const QString& text, const QJsonObject& extra = {}) {
            QJsonObject object{{QStringLiteral("id"), QStringLiteral("P%1").arg(number)}, {QStringLiteral("text"), text},
                               {QStringLiteral("style"), QString()}};
            for (auto it = extra.begin(); it != extra.end(); ++it) object.insert(it.key(), it.value());
            return object;
        };
        const QList<OutlineParagraph> paragraphs = outlineParagraphs(QJsonArray{
            block(1, QStringLiteral("年终表彰大会议程"), {{QStringLiteral("bold"), true}}),
            block(2, QString()),
            block(3, QStringLiteral("第一部分 领导致辞")),
            block(4, QStringLiteral("请各位于下午两点前入场，入场时出示工牌。")),
            QJsonObject{{QStringLiteral("id"), QStringLiteral("T1")}},
            block(5, QStringLiteral("第二部分 颁奖")),
            block(6, QStringLiteral("颁奖顺序以名单为准，获奖者请提前到后台候场。")),
            block(7, QStringLiteral("办公室"), {{QStringLiteral("align"), QStringLiteral("right")}})});
        QCOMPARE(paragraphs.size(), 7);
        QCOMPARE(paragraphs[6].number, 7);
        bool understood = false;
        // The title: the first paragraph with text; bold already, so only the rest is planned.
        QList<PlannedEdit> edits = requestedDocumentFormats(QStringLiteral("标题加粗、居中，字号改成三号"), paragraphs, false, &understood);
        QVERIFY(understood);
        QCOMPARE(edits.size(), 1);
        QCOMPARE(edits[0].command.name, QStringLiteral("format.paragraphs"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("paragraphs")).toString(), QStringLiteral("1"));
        QVERIFY(!edits[0].command.arguments.contains(QStringLiteral("bold")));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("center"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("fontSize")).toInt(), 16);
        QVERIFY(edits[0].place.startsWith(QStringLiteral("第 1 段（年终表彰")));
        // Paragraphs counted as people count them: the empty one does not count.
        edits = requestedDocumentFormats(QStringLiteral("第二段和第四段改成二级标题"), paragraphs, false, &understood);
        QVERIFY(understood);
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("paragraphs")).toString(), QStringLiteral("3,5"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("style")).toString(), QStringLiteral("heading2"));
        QCOMPARE(edits[0].place, QStringLiteral("第 2、4 段"));
        // "…开头的", quoted or not; "含有…的段落"; "最后一段" already right-aligned.
        edits = requestedDocumentFormats(QStringLiteral("“第一部分”“第二部分”开头的段落加粗"), paragraphs, false, &understood);
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("paragraphs")).toString(), QStringLiteral("3,5"));
        edits = requestedDocumentFormats(QStringLiteral("把含有“工牌”的段落改成斜体"), paragraphs, false, &understood);
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("paragraphs")).toString(), QStringLiteral("4"));
        QVERIFY(edits[0].command.arguments.value(QStringLiteral("italic")).toBool());
        edits = requestedDocumentFormats(QStringLiteral("最后一段右对齐"), paragraphs, false, &understood);
        QVERIFY(understood && edits.isEmpty());
        // Words: every one found; a color is the text's; highlight on words is fine.
        edits = requestedDocumentFormats(QStringLiteral("把文中的“候场”标成蓝色，“工牌”加黄色突出显示"), paragraphs, false, &understood);
        QVERIFY(understood);
        QCOMPARE(edits.size(), 2);
        QCOMPARE(edits[0].command.name, QStringLiteral("format.text"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("find")).toString(), QStringLiteral("候场"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("color")).toString(), QStringLiteral("#0070C0"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("highlight")).toString(), QStringLiteral("yellow"));
        // Nothing named: the selection, if there is one; otherwise the model decides.
        edits = requestedDocumentFormats(QStringLiteral("加粗，改成红色"), paragraphs, true, &understood);
        QVERIFY(understood);
        QCOMPARE(edits.size(), 1);
        QCOMPARE(edits[0].command.name, QStringLiteral("format.selection"));
        QVERIFY(edits[0].command.arguments.value(QStringLiteral("bold")).toBool());
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("color")).toString(), QStringLiteral("#C00000"));
        requestedDocumentFormats(QStringLiteral("加粗"), paragraphs, false, &understood);
        QVERIFY(!understood);
        // Not a format request, or a format the program cannot put there.
        requestedDocumentFormats(QStringLiteral("帮我总结一下这份文档"), paragraphs, false, &understood);
        QVERIFY(!understood);
        requestedDocumentFormats(QStringLiteral("第三段加下划线"), paragraphs, false, &understood);
        QVERIFY(!understood); // whole paragraphs have no underline command: the model's turn
        // English.
        edits = requestedDocumentFormats(QStringLiteral("Make the title Heading 1, and make every \"badge\" italic."), paragraphs, false, &understood);
        QVERIFY(understood);
        QCOMPARE(edits.size(), 2);
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("style")).toString(), QStringLiteral("heading1"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("find")).toString(), QStringLiteral("badge"));
        for (const PlannedEdit& edit : edits) QVERIFY(validateCommand(edit.command).ok);
    }

    void theProgramReadsFormatRequestsItself()
    {
        const auto grid = [](const QList<QStringList>& rows) {
            QJsonArray array;
            for (const QStringList& row : rows) {
                QJsonArray cells;
                for (const QString& text : row) cells.append(QJsonObject{{QStringLiteral("text"), text}});
                array.append(cells);
            }
            return tableGridFrom({{QStringLiteral("rows"), array}});
        };
        const TableGrid tasks = grid({{QStringLiteral("任务"), QStringLiteral("负责人"), QStringLiteral("进度（%）"), QStringLiteral("状态")},
                                      {QStringLiteral("采购"), QStringLiteral("钱多多"), QStringLiteral("100"), QStringLiteral("已结束")},
                                      {QStringLiteral("布展"), QStringLiteral("孙小美"), QStringLiteral("40"), QStringLiteral("进行中")},
                                      {QStringLiteral("宣传"), QStringLiteral("阿土伯"), QStringLiteral("100"), QStringLiteral("已结束")},
                                      {QStringLiteral("复盘"), QStringLiteral("钱多多"), QStringLiteral("0"), QStringLiteral("未开始")}});
        bool understood = false;
        // A clause without a place goes on the one before it.
        QList<PlannedEdit> edits = requestedFormats(QStringLiteral("表头加粗、居中，然后加个浅紫色背景"), tasks, 3, &understood);
        QVERIFY(understood);
        QCOMPARE(edits.size(), 2);
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("1"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("1-4"));
        QVERIFY(edits[0].command.arguments.value(QStringLiteral("bold")).toBool());
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("center"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("fill")).toString(), QStringLiteral("#E4DFEC"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("table")).toInt(), 3);
        // A column by its header (the unit in brackets aside), rows by what a cell says.
        edits = requestedFormats(QStringLiteral("进度那一列右对齐；已结束的任务整行标成浅灰色"), tasks, 1, &understood);
        QVERIFY(understood);
        QCOMPARE(edits.size(), 2);
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("3"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("1-5"));
        QCOMPARE(edits[0].place, QStringLiteral("第 3 列"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("2,4"));
        QCOMPARE(edits[1].place, QStringLiteral("第 2、4 行"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("fill")).toString(), QStringLiteral("#D9D9D9"));
        // Numbered rows and columns, text color, size; English.
        edits = requestedFormats(QStringLiteral("第2到3行的字标红，第四列字号改成小四"), tasks, 1, &understood);
        QCOMPARE(edits.size(), 2);
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("2-3"));
        QCOMPARE(edits[0].command.arguments.value(QStringLiteral("color")).toString(), QStringLiteral("#C00000"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("4"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("fontSize")).toInt(), 12);
        const TableGrid people = grid({{QStringLiteral("Name"), QStringLiteral("Team"), QStringLiteral("Status")},
                                       {QStringLiteral("Ann"), QStringLiteral("A"), QStringLiteral("Open")},
                                       {QStringLiteral("Bo"), QStringLiteral("B"), QStringLiteral("Closed")}});
        edits = requestedFormats(QStringLiteral("Make the header row italic, and center the Status column."), people, 1, &understood);
        QVERIFY(understood);
        QCOMPARE(edits.size(), 2);
        QVERIFY(edits[0].command.arguments.value(QStringLiteral("italic")).toBool());
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("columns")).toString(), QStringLiteral("3"));
        QCOMPARE(edits[1].command.arguments.value(QStringLiteral("align")).toString(), QStringLiteral("center"));
        for (const PlannedEdit& edit : edits) QVERIFY(validateCommand(edit.command).ok);
        // A format with no place, or a fill with no color, is not understood: the model's plan stays.
        edits = requestedFormats(QStringLiteral("加个底色"), tasks, 1, &understood);
        QVERIFY(!understood && edits.isEmpty());
        // No format asked for (a check, filling in text): nothing from the program.
        QVERIFY(requestedFormats(QStringLiteral("空着的状态格都填上待定"), tasks, 1, &understood).isEmpty());
        QVERIFY(requestedFormats(QStringLiteral("帮我检查一下表格"), tasks, 1, &understood).isEmpty());

        // The program's formats replace the model's confused ones; the model's text changes stay.
        const QList<PlannedEdit> model = parseTablePlan(QStringLiteral(R"({"plan":[
            {"cells":"R2C4","change":"fill","reason":"r","value":"已结束"},
            {"cells":"R3C2","change":"text","reason":"r","value":"孙小梅"},
            {"cells":"R1C4","change":"text","reason":"r","value":"text:'状态';bold:True"}]})"),
            tasks, 1, QStringLiteral("已结束的行标成浅灰色，孙小美的名字改成孙小梅"));
        QCOMPARE(model.size(), 2); // the serialized "text" is dropped
        QCOMPARE(model[0].command.arguments.value(QStringLiteral("fill")).toString(), QStringLiteral("#D9D9D9")); // the request's color
        const QList<PlannedEdit> plan = withRequestedFormats(model, tasks, 1, QStringLiteral("已结束的行标成浅灰色，孙小美的名字改成孙小梅"));
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan[0].command.arguments.value(QStringLiteral("rows")).toString(), QStringLiteral("2,4"));
        QCOMPARE(plan[1].after, QStringLiteral("孙小梅"));
        // "fill" with a word in a request with no color at all: 填 as in write in.
        const QList<PlannedEdit> written = parseTablePlan(QStringLiteral(R"({"plan":[{"cells":"R5C3","change":"fill","reason":"r","value":"待定"}]})"),
                                                          tasks, 1, QStringLiteral("没开始的进度填上待定"));
        QCOMPARE(written.size(), 1);
        QCOMPARE(written[0].command.name, QStringLiteral("table.setCell"));
        QCOMPARE(written[0].after, QStringLiteral("待定"));
    }

    void typedRequestsGoWhereThePersonMeant()
    {
        QCOMPARE(classifyRequest(QStringLiteral("详细点"), true), RequestKind::ReworkSelection);
        QCOMPARE(classifyRequest(QStringLiteral("改得正式一些"), true), RequestKind::ReworkSelection);
        QCOMPARE(classifyRequest(QStringLiteral("翻译成英文"), true), RequestKind::ReworkSelection);
        QCOMPARE(classifyRequest(QStringLiteral("写一篇文章"), true), RequestKind::ReworkSelection); // the selection wins
        QCOMPARE(classifyRequest(QStringLiteral("这段末尾加一句总结"), true), RequestKind::ReworkSelection);
        QCOMPARE(classifyRequest(QStringLiteral("在文末再加一个表格，两列：日期、事项"), true), RequestKind::Chat); // another place
        QCOMPARE(classifyRequest(QStringLiteral("在文档最后加一段说明"), true), RequestKind::Chat);
        QCOMPARE(classifyRequest(QStringLiteral("这段话是什么意思"), true), RequestKind::Chat);
        QCOMPARE(classifyRequest(QStringLiteral("有错别字吗"), true), RequestKind::Chat);
        QCOMPARE(classifyRequest(QStringLiteral("这样写可以？"), true), RequestKind::Chat);
        QCOMPARE(classifyRequest(QStringLiteral("谢谢！"), true), RequestKind::Chat);
        QCOMPARE(classifyRequest(QStringLiteral("好的"), true), RequestKind::Chat);
        QCOMPARE(classifyRequest(QString(61, QChar(0x5B57)), true), RequestKind::Chat); // long: content, not an instruction
        QCOMPARE(classifyRequest(QStringLiteral("/插入 你好"), true), RequestKind::Chat);
        QCOMPARE(classifyRequest(QStringLiteral("详细点"), false), RequestKind::Chat);
        QCOMPARE(classifyRequest(QStringLiteral("写一篇文章"), false), RequestKind::Write);
        QCOMPARE(classifyRequest(QStringLiteral("帮我写一封感谢信给王老师"), false), RequestKind::Write);
        QCOMPARE(classifyRequest(QStringLiteral("请起草一份周报"), false), RequestKind::Write);
        QCOMPARE(classifyRequest(QStringLiteral("写什么好呢"), false), RequestKind::Chat);
        QCOMPARE(classifyRequest(QStringLiteral("今天天气怎么样"), false), RequestKind::Chat);

        // Their tasks are not quick actions; the instruction goes to the model with the text.
        const TaskSpec* rework = findTask(QStringLiteral("rework"));
        const TaskSpec* write = findTask(QStringLiteral("write"));
        QVERIFY(rework && !rework->offered && write && !write->offered);
        QVERIFY(!tasksFor(TaskScope::Selection).contains(rework));
        QVERIFY(!tasksFor(TaskScope::Writing).contains(write));
        const QJsonArray reworkMessages = taskMessages(*rework, {QStringLiteral("原来的句子。"), QStringLiteral("详细点"), QString()});
        const QString reworkUser = reworkMessages.last().toObject().value(QStringLiteral("content")).toString();
        QVERIFY(reworkUser.contains(QStringLiteral("扩写到")) && reworkUser.contains(QStringLiteral("原来的句子。")));
        QVERIFY(reworkMessages.first().toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("不要使用 Markdown")));
        const QString writeUser = taskMessages(*write, {QString(), QStringLiteral("写一篇关于学习的文章"), QString()})
                                      .last().toObject().value(QStringLiteral("content")).toString();
        QVERIFY(writeUser.contains(QStringLiteral("写一篇关于学习的文章")));
    }

    void reworkInstructionsAreMadePlainAndCheckedForChange()
    {
        QCOMPARE(presetForInstruction(QStringLiteral("详细一定啊")), QStringLiteral("expand")); // as typed in a real session
        QCOMPARE(presetForInstruction(QStringLiteral("再具体些")), QStringLiteral("expand"));
        QCOMPARE(presetForInstruction(QStringLiteral("短一点")), QStringLiteral("shorten"));
        QCOMPARE(presetForInstruction(QStringLiteral("改得正式点")), QStringLiteral("formal"));
        QCOMPARE(presetForInstruction(QStringLiteral("翻译成英文")), QStringLiteral("translate"));
        QCOMPARE(presetForInstruction(QStringLiteral("用中文写得详细点")), QStringLiteral("expand"));
        QCOMPARE(presetForInstruction(QStringLiteral("改一下错别字")), QStringLiteral("correct"));
        QCOMPARE(presetForInstruction(QStringLiteral("换个说法")), QString());

        const QString original = QStringLiteral("6. 结语\n·\t人工智能需以开放心态拥抱变革，同时坚守伦理底线\n"
                                                "·\t未来依赖技术与社会制度协同，方能成为推动人类文明进步的引擎");
        TaskInput input{original, QStringLiteral("详细一定啊"), QString()};
        const auto user = [](const TaskInput& in) {
            return taskMessages(*findTask(QStringLiteral("rework")), in).last().toObject().value(QStringLiteral("content")).toString();
        };
        // 扩写's instruction with a number of characters (the original has 58), and the person's words.
        QVERIFY(user(input).contains(QStringLiteral("不要加入原文没有的具体数字")));
        QVERIFY2(user(input).contains(QStringLiteral("原文约 58 字，扩写到 116 字左右，最多不超过 174 字")), qPrintable(user(input)));
        QVERIFY(!user(input).contains(QStringLiteral("详细一定啊"))); // just 扩写: the words would only mislead
        QVERIFY(user(input).contains(QStringLiteral("【原文开始】\n6. 结语")));
        QVERIFY(!user(input).contains(QStringLiteral("明显不同")));
        input.detail = QStringLiteral("详细一点，再加上具体的例子");
        QVERIFY(user(input).contains(QStringLiteral("不是要修改的文字）：详细一点，再加上具体的例子")));
        input.detail = QStringLiteral("详细一定啊");
        input.insist = true;
        QVERIFY(user(input).contains(QStringLiteral("明显不同")));
        input.detail = QStringLiteral("短一点");
        QVERIFY(user(input).contains(QStringLiteral("精简到 29 字左右")));

        // What the 1.7B model gave back in that session: the same words without the list marks.
        QVERIFY(rewriteUnchanged(original, QStringLiteral("6. 结语\n人工智能需以开放心态拥抱变革，同时坚守伦理底线\n"
                                                          "未来依赖技术与社会制度协同，方能成为推动人类文明进步的引擎"),
                                 QStringLiteral("expand")));
        QVERIFY(rewriteUnchanged(QStringLiteral("今天开会。"), QStringLiteral("今天开会！"), QStringLiteral("polish")));
        QVERIFY(rewriteUnchanged(QStringLiteral("大家要带好笔记本。"), QStringLiteral("大家都要带好笔记本。"), QStringLiteral("expand")));
        QVERIFY(!rewriteUnchanged(QStringLiteral("大家要带好笔记本。"),
                                  QStringLiteral("请各位参会人员携带笔记本，认真听讲并做好记录，会后整理要点。"), QStringLiteral("expand")));
        QVERIFY(!rewriteUnchanged(QStringLiteral("请大家准时参加。"), QStringLiteral("请各位准时出席。"), QStringLiteral("formal")));
    }

    void modelMarkdownAndEchoesAreTidied()
    {
        QVERIFY(looksLikeMarkdown(QStringLiteral("## 标题\n正文")));
        QVERIFY(looksLikeMarkdown(QStringLiteral("- 一\n- 二")));
        QVERIFY(looksLikeMarkdown(QStringLiteral("这是**重点**。")));
        QVERIFY(!looksLikeMarkdown(QStringLiteral("普通的一段话。\n第二段。")));
        QVERIFY(!looksLikeMarkdown(QStringLiteral("- 只有一项")));
        // Seen in a real session: the whole insert repeated after "内容如下：".
        const QString written = QStringLiteral("### 一、科学规划\n- 每周制定学习计划：建议使用甘特图或日历工具\n- 分配每日学习时间：采用番茄工作法");
        QCOMPARE(withoutEchoedText(QStringLiteral("您的文章已插入，内容如下：\n\n# 高效学习法\n\n## 一、科学规划\n"
                                                  "- **每周制定学习计划**：建议使用甘特图或日历工具   \n- **分配每日学习时间**：采用番茄工作法"),
                                   {written}),
                 QStringLiteral("已经写进文档了，可以在文档里查看。需要调整的话告诉我。"));
        QCOMPARE(withoutEchoedText(QStringLiteral("已插入到文档末尾。\n\n## 一、科学规划\n- 每周制定学习计划：建议使用甘特图或日历工具\n"
                                                  "- 分配每日学习时间：采用番茄工作法"), {written}),
                 QStringLiteral("已插入到文档末尾。"));
        QCOMPARE(withoutEchoedText(QStringLiteral("已经插入。"), {written}), QStringLiteral("已经插入。"));
        QCOMPARE(withoutEchoedText(QStringLiteral("还需要我补充什么吗？"), {}), QStringLiteral("还需要我补充什么吗？"));
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

        // The user's DeepSeek error ("An assistant message with 'tool_calls' must be followed by tool
        // messages responding to each 'tool_call_id'"): every call is answered before the next message,
        // and an answer to no call is left out.
        const auto call2 = [](const char* id, const char* name) {
            return QJsonObject{{"id", id}, {"function", QJsonObject{{"name", name}, {"arguments", QJsonObject{}}}}};
        };
        const QJsonArray unanswered{
            QJsonObject{{"role", "user"}, {"content", "做个表格"}},
            QJsonObject{{"role", "assistant"}, {"content", ""}, {"tool_calls", QJsonArray{call2("a", "text_insert"), call2("b", "edit_undo")}}},
            QJsonObject{{"role", "tool"}, {"content", "{\"ok\":true}"}},             // answers a only
            QJsonObject{{"role", "user"}, {"content", "重做表格"}},                    // b never answered
            QJsonObject{{"role", "tool"}, {"content", "{\"ok\":true}"}},             // an answer to nothing
            QJsonObject{{"role", "assistant"}, {"content", ""}, {"tool_calls", QJsonArray{call2("c", "text_insert")}}}};
        const QJsonArray fixed = EngineClient::toOpenAiMessages(unanswered);
        QStringList shape;
        for (const QJsonValue& value : fixed)
            shape << value.toObject().value("role").toString() + QLatin1Char(':') + value.toObject().value("tool_call_id").toString();
        QCOMPARE(shape, (QStringList{"user:", "assistant:", "tool:a", "tool:b", "user:", "assistant:", "tool:c"}));
        QVERIFY(fixed.at(3).toObject().value("content").toString().contains(QStringLiteral("没有执行")));

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
        QCOMPARE(recommendedCatalogModel(32 * gb, 12 * gb).id, QStringLiteral("qwen3-4b"));
        QCOMPARE(recommendedCatalogModel(16 * gb, 0).id, QStringLiteral("qwen3-4b"));
        QCOMPARE(recommendedCatalogModel(4 * gb, 0).id, QStringLiteral("qwen3-1.7b"));
        QVERIFY(asLocalModel(*findCatalogModel(QStringLiteral("qwen3-4b"))).tools);
        {
            // Fixed next to the program; only development builds and tests point elsewhere.
            ScopedEnvironment unset("OPEN_OFFICE_MODEL_DIR", QByteArray());
            QCOMPARE(modelFolder(), QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models")));
        }

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

private:
    QTemporaryDir m_settings; // its settings.ini: a file QSettings may replace (a QTemporaryFile may not be)
};

QTEST_GUILESS_MAIN(AssistantTest)
#include "assistant_test.moc"
