#include "assistant_tasks.h"

#include "markdown_html.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace openoffice {
namespace {

QList<TaskSpec> buildTasks()
{
    const auto selection = [](const char* id, const char* title, const char* instruction) {
        return TaskSpec{QString::fromLatin1(id), QString::fromUtf8(title), TaskScope::Selection, TaskOutput::Rewrite,
                        QString::fromUtf8(instruction), QString()};
    };
    const auto writing = [](const char* id, const char* title, const char* placeholder) {
        return TaskSpec{QString::fromLatin1(id), QString::fromUtf8(title), TaskScope::Writing, TaskOutput::Markdown,
                        QString(), QString::fromUtf8(placeholder)};
    };
    return {
        selection("polish", "润色", "润色下面的文字：语句更通顺，用词更准确，保持原意、语气和段落数不变。"),
        selection("correct", "纠错", "改正下面文字中的错别字、标点错误和语病。没有错的地方保持原样，不要改变风格和段落数。"),
        selection("shorten", "精简", "把下面的文字精简到原来的一半左右，保留关键信息和原意。"),
        selection("expand", "扩写", "把下面的文字扩写得更充实、具体，长度约为原来的两倍。不要编造原文没有的数字、日期、人名等事实。"),
        selection("formal", "正式", "把下面的文字改写成正式、得体的书面语，适合在工作中使用，保持原意和段落数。"),
        selection("translate", "翻译", ""), // the direction depends on the text
        {QStringLiteral("summary"), QStringLiteral("总结全文"), TaskScope::Document, TaskOutput::Markdown,
         QStringLiteral("总结这份文档：先用一两句话概括主旨，再用 3 到 6 条要点列出关键内容。用 Markdown 无序列表写要点。"),
         QString()},
        {QStringLiteral("todos"), QStringLiteral("提取待办"), TaskScope::Document, TaskOutput::Markdown,
         QStringLiteral("从这份文档中找出需要去做的事项，用 Markdown 表格列出，列为：事项、负责人、截止时间。"
                        "文中没写的负责人或时间填“未写明”。如果没有待办事项，只回答“这份文档里没有找到待办事项。”"),
         QString()},
        {QStringLiteral("proofread"), QStringLiteral("全文校对"), TaskScope::Document, TaskOutput::Corrections,
         QStringLiteral("校对这份文档，找出错别字、用错的词、标点错误和明显的语病。只报告确定的错误，不要改写风格，"
                        "不要报告没有错的地方。每条依次给出：error（原文中出错的最短片段，必须和原文一字不差，"
                        "通常 2 到 15 个字）、fix（改正后的片段）、reason（简短原因）。没有错误时 corrections 为空数组。"
                        "例如原文有“请按装好软件”，应回答 {\"corrections\":[{\"error\":\"按装\",\"fix\":\"安装\",\"reason\":\"错别字\"}]}。"),
         QString()},
        writing("notice", "通知", "通知什么事？时间、地点、对象…"),
        writing("minutes", "会议纪要", "会议主题、时间、参会人、讨论了什么…"),
        writing("report", "工作总结", "哪段时间、做了哪些工作、成果和问题…"),
        writing("email", "邮件", "写给谁、什么事、希望对方做什么…"),
        writing("leave", "请假条", "请假原因、起止时间、写给谁…"),
        writing("plan", "活动方案", "活动名称、目的、时间地点、人数、预算…"),
        // Reached from typed requests (classifyRequest), not shown as quick actions.
        {QStringLiteral("rework"), QStringLiteral("按要求改写"), TaskScope::Selection, TaskOutput::Rewrite, QString(),
         QString(), false},
        {QStringLiteral("write"), QStringLiteral("按要求起草"), TaskScope::Writing, TaskOutput::Markdown, QString(),
         QString(), false},
    };
}

QString clipped(const QString& text, bool* truncated)
{
    *truncated = text.size() > kTaskTextLimit;
    return *truncated ? text.left(kTaskTextLimit) : text;
}

} // namespace

const QList<TaskSpec>& taskCatalog()
{
    static const QList<TaskSpec> tasks = buildTasks();
    return tasks;
}

const TaskSpec* findTask(const QString& id)
{
    for (const TaskSpec& spec : taskCatalog())
        if (spec.id == id) return &spec;
    return nullptr;
}

QList<const TaskSpec*> tasksFor(TaskScope scope)
{
    QList<const TaskSpec*> result;
    for (const TaskSpec& spec : taskCatalog())
        if (spec.scope == scope && spec.offered) result.append(&spec);
    return result;
}

RequestKind classifyRequest(const QString& text, bool hasSelection)
{
    const QString request = text.trimmed();
    if (request.isEmpty() || request.startsWith(QLatin1Char('/')) || request.startsWith(QChar(0xFF0F)))
        return RequestKind::Chat;
    // Questions are answered, whether or not something is selected.
    static const QRegularExpression question(QStringLiteral(
        "[？?]\\s*$|[吗呢么]\\s*[。！!]?\\s*$|什么|为什么|为何|怎么样|意思|解释|是否|是不是|对不对|有没有|哪些|哪里|哪个|多少"));
    if (question.match(request).hasMatch()) return RequestKind::Chat;
    // Thanks and the like are not instructions, even with text selected.
    static const QRegularExpression small(QStringLiteral(
        "^(好的?|好吧|好嘞|谢谢|多谢|感谢|辛苦了|可以了?|行|嗯+|不错|很好|收到|明白|知道了|你好|在吗|ok|OK|Ok)[。！!~～,，]*$"));
    if (small.match(request).hasMatch()) return RequestKind::Chat;
    // With text selected, a short instruction is about that text: 详细点, 改得正式一些, 翻译成英文.
    if (hasSelection && request.size() <= 60) return RequestKind::ReworkSelection;
    static const QRegularExpression write(QStringLiteral(
        "^(请你?|麻烦你?|你)?(帮我|给我|替我)?(写|起草|草拟|拟|撰写|编写|生成|写作)"));
    if (write.match(request).hasMatch()) return RequestKind::Write;
    return RequestKind::Chat;
}

bool mostlyChinese(const QString& text)
{
    int han = 0;
    int latin = 0;
    for (const QChar character : text) {
        const ushort unit = character.unicode();
        if (unit >= 0x4E00 && unit <= 0x9FFF) ++han;
        else if ((unit >= 'A' && unit <= 'Z') || (unit >= 'a' && unit <= 'z')) ++latin;
    }
    // A Chinese character carries about as much as a short English word.
    return han * 4 >= latin;
}

QJsonArray taskMessages(const TaskSpec& spec, const TaskInput& input)
{
    QString system = QStringLiteral("你是 Open Office 里的办公助手，运行在用户自己的电脑上。用简体中文工作（翻译任务除外）。"
                                    "只输出结果本身：不要解释，不要寒暄，不要加“以下是……”之类的开头，不要给结果加引号。");
    QString instruction = spec.instruction;
    QString user;
    bool truncated = false;
    switch (spec.scope) {
    case TaskScope::Selection: {
        if (spec.id == QStringLiteral("rework"))
            instruction = QStringLiteral("按用户的要求处理下面这段文字。用户的要求：") + input.detail.trimmed()
                + QStringLiteral("\n不要编造原文没有的数字、日期、人名等事实。");
        if (spec.id == QStringLiteral("translate")) {
            instruction = mostlyChinese(input.text)
                ? QStringLiteral("把下面的文字翻译成地道、准确的英文，保持段落数。只输出译文。")
                : QStringLiteral("把下面的文字翻译成通顺、准确的简体中文，保持段落数。只输出译文。");
        }
        system += QStringLiteral("输出要直接替换用户选中的原文，所以不要使用 Markdown 符号（如 #、**、-），"
                                 "按原文的段落分行。");
        user = instruction + QStringLiteral("\n\n原文：\n") + clipped(input.text, &truncated);
        break;
    }
    case TaskScope::Document: {
        const QString text = clipped(input.text, &truncated);
        user = instruction + QStringLiteral("\n\n文档");
        if (!input.documentName.isEmpty()) user += QStringLiteral("《") + input.documentName + QStringLiteral("》");
        if (truncated) user += QStringLiteral("（较长，只提供了前 %1 个字）").arg(kTaskTextLimit);
        user += QStringLiteral("内容如下：\n") + text;
        if (spec.output == TaskOutput::Markdown) system += QStringLiteral("可以使用 Markdown 的列表和表格。");
        break;
    }
    case TaskScope::Writing:
        system += QStringLiteral("写出可以直接放进文档的正文：用 Markdown 的 # 标题、列表组织结构，格式规范、语气得体。"
                                 "用户没有提供的具体信息（日期、人名、地点、数字等）用【】标出让用户填写，绝不编造。");
        if (spec.id == QStringLiteral("write")) // anything asked for: articles need no blanks to fill
            system += QStringLiteral("用 ## 小标题分节，不要用【】做小标题；只有必须由用户补充的具体信息（如日期、人名、金额）才用【】。");
        user = spec.id == QStringLiteral("write")
            ? QStringLiteral("请按下面的要求写：\n") + input.detail.trimmed()
            : QStringLiteral("请写一份%1。用户提供的信息：\n%2").arg(spec.title, input.detail.trimmed());
        break;
    }
    return {QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), user}}};
}

QJsonObject taskFormat(const TaskSpec& spec)
{
    if (spec.output != TaskOutput::Corrections) return {};
    const QJsonObject text{{QStringLiteral("type"), QStringLiteral("string")}};
    const QJsonObject item{
        {QStringLiteral("type"), QStringLiteral("object")},
        // The engine writes the fields in the schema's order, and QJsonObject sorts
        // keys: the names are chosen so that sorting keeps "the wrong text" before
        // "the fix". With "corrected" before "original", small models copied the
        // typo into the fix (measured with Qwen3 1.7B on the built-in engine).
        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("error"), text},
                                                   {QStringLiteral("fix"), text},
                                                   {QStringLiteral("reason"), text}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("error"), QStringLiteral("fix"), QStringLiteral("reason")}}};
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{{QStringLiteral("corrections"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), item}}}}},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("corrections")}}};
}

int contextLengthFor(const QJsonArray& messages, bool withTools)
{
    qsizetype characters = 0;
    for (const QJsonValue& message : messages)
        characters += message.toObject().value(QStringLiteral("content")).toString().size();
    // Roughly one token per Chinese character; room for tools and the answer.
    const qsizetype needed = characters + (withTools ? 2500 : 0) + 3000;
    int length = 4096;
    while (length < needed && length < 32768) length *= 2;
    return length;
}

QList<Correction> parseCorrections(const QString& answer, const QString& documentText)
{
    QString json = stripThinking(answer).trimmed();
    // Tolerate a fenced block or text around the object.
    const int start = json.indexOf(QLatin1Char('{'));
    const int end = json.lastIndexOf(QLatin1Char('}'));
    if (start < 0 || end <= start) return {};
    json = json.mid(start, end - start + 1);
    const QJsonArray items = QJsonDocument::fromJson(json.toUtf8()).object().value(QStringLiteral("corrections")).toArray();
    QList<Correction> result;
    QSet<QString> seen;
    for (const QJsonValue& value : items) {
        const QJsonObject item = value.toObject();
        // "error"/"fix" from the schema; "original"/"corrected" from engines that ignore it.
        const auto field = [&item](const char* name, const char* fallback) {
            const QString given = item.value(QLatin1String(name)).toString();
            return (given.isEmpty() ? item.value(QLatin1String(fallback)).toString() : given).trimmed();
        };
        Correction correction{field("error", "original"), field("fix", "corrected"),
                              item.value(QStringLiteral("reason")).toString().trimmed()};
        if (correction.original.isEmpty() || correction.corrected.isEmpty()) continue;
        if (correction.original == correction.corrected) continue;
        if (correction.original.size() > 60 || correction.corrected.size() > 80) continue;
        // A fix that only deletes text is accepted when it removes a doubled word ("的的" -> "的");
        // small models otherwise "fix" a typo by dropping the words after it.
        if (correction.corrected.size() < correction.original.size() && correction.original.contains(correction.corrected)) {
            QString removed = correction.original;
            removed.replace(removed.indexOf(correction.corrected), correction.corrected.size(), QString());
            if (!correction.original.contains(removed + removed)) continue;
        }
        if (!documentText.contains(correction.original)) continue; // must be found to be fixed
        if (seen.contains(correction.original)) continue;
        seen.insert(correction.original);
        result.append(correction);
        if (result.size() == 30) break;
    }
    return result;
}

} // namespace openoffice
