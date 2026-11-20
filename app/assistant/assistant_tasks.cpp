#include "assistant_tasks.h"

#include "markdown_html.h"

#include <QJsonDocument>
#include <QPair>
#include <QRegularExpression>
#include <QSet>

#include <QHash>
#include <QLocale>
#include <QPair>

#include <algorithm>
#include <cmath>
#include <functional>

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
        selection("expand", "扩写", "把下面的文字扩写得更充实、具体：可以解释含义、补充理由、举一般性的例子，"
                                      "但不要加入原文没有的具体数字、年份、人名、机构、法规或文件名称。"),
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
        {QStringLiteral("table"), QStringLiteral("修改表格"), TaskScope::Table, TaskOutput::TablePlan, QString(),
         QString(), false},
        // The second look of a table check: the cells' words, proofread like a document.
        {QStringLiteral("tableProofread"), QStringLiteral("修改表格"), TaskScope::Document, TaskOutput::Corrections,
         QStringLiteral("下面是一张表格里各个单元格的文字，每行是一格，冒号前是这一列的名称。找出单元格文字里的错别字和用错的词"
                        "（常见的是同音字、形近字），只报告确定的错误，不要改写，不要报告没有错的地方，人名、型号、邮箱、网址不算错。"
                        "每条依次给出：error（出错的最短片段，必须和原文一字不差）、fix（改正后的正确写法，必须和 error 不同）、"
                        "reason（简短原因）。没有错误时 corrections 为空数组。"
                        "例如有一格是“按装费”，应回答 {\"corrections\":[{\"error\":\"按装\",\"fix\":\"安装\",\"reason\":\"错别字\"}]}。"),
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

QString presetForInstruction(const QString& request)
{
    static const QList<QPair<QString, QRegularExpression>> presets{
        {QStringLiteral("translate"), QRegularExpression(QStringLiteral("翻译|译成|译为|英文|英语"))},
        {QStringLiteral("shorten"), QRegularExpression(QStringLiteral("精简|简短|简洁|简练|短一?[点些]|缩短|压缩|删减|概括"))},
        {QStringLiteral("expand"), QRegularExpression(QStringLiteral("详细|具体|展开|充实|丰富|扩写|扩充|多写|写多|长一?[点些]|补充"))},
        {QStringLiteral("correct"), QRegularExpression(QStringLiteral("纠错|改错|错别字|错字|病句|语病|标点"))},
        {QStringLiteral("formal"), QRegularExpression(QStringLiteral("正式|书面|官方|公文|得体|专业"))},
        {QStringLiteral("polish"), QRegularExpression(QStringLiteral("润色|通顺|流畅|优美|好听|顺一?[点些]"))},
    };
    for (const auto& [id, pattern] : presets)
        if (pattern.match(request).hasMatch()) return id;
    return {};
}

bool rewriteUnchanged(const QString& original, const QString& result, const QString& preset)
{
    const auto words = [](const QString& text) {
        QString kept;
        for (const QChar c : text)
            if (!c.isSpace() && !c.isPunct() && !c.isSymbol()) kept += c;
        return kept;
    };
    const QString before = words(original);
    const QString after = words(result);
    if (before.isEmpty()) return false;
    if (after == before) return true;
    // Dropping or adding a word or two is not a rewrite either.
    const bool contained = after.contains(before) || before.contains(after);
    if (contained && qAbs(after.size() - before.size()) * 10 < before.size()) return true;
    return preset == QStringLiteral("expand") && after.size() * 100 < before.size() * 115;
}

bool isQuestion(const QString& request)
{
    static const QRegularExpression question(QStringLiteral(
        "[？?]\\s*$|[吗呢么]\\s*[。！!]?\\s*$|什么|为什么|为何|怎么样|意思|解释|是否|是不是|对不对|有没有|哪些|哪里|哪个|多少"));
    return question.match(request.trimmed()).hasMatch();
}

bool tableRequest(const QString& request, bool cursorInTable)
{
    const QString text = request.trimmed();
    if (text.isEmpty() || text.startsWith(QLatin1Char('/')) || text.startsWith(QChar(0xFF0F))) return false;
    static const QRegularExpression names(QStringLiteral("表格|表头|单元格|这张表|这个表|整张表|表里|表中|[第\\d一二三四五六七八九十]+张表"));
    static const QRegularExpression check(QStringLiteral("检查|核对|看看|看一下|有没有.{0,6}(错|问题)|算错|写错|错误|对不对"));
    static const QRegularExpression writing(QStringLiteral("^(请你?|帮我|给我)?((写|起草|生成)|(做|画|列)一?[张个份])"));
    if (writing.match(text).hasMatch()) return false; // "做一个表格" makes a new one, "写一段…" is writing
    // "在文末加一个表格" or "把这些整理成表格" anywhere in the sentence is a new table too, even in a
    // document that has one; "增加表格的行高" or "表格加一个表头" changes the one there.
    static const QRegularExpression newTable(QStringLiteral(
        "(加|添加|增加|新增|做|画|插入|插|新建|建|生成|制作|创建|弄)(上|出)?(一|1)?[张个份]?(新的?)?(表格(?![头里中的])|表(?=$|[，。,.:：、；;\\s]))"
        "|列(一|1)?[张个份](表格|表)|(做成|整理成|转成|变成|改成|列成|写成)(一|1)?[张个份]?(表格|表(?=$|[，。,.:：、；;\\s]))"));
    if (newTable.match(text).hasMatch()) return false;
    if (!names.match(text).hasMatch() && !cursorInTable) return false;
    return !isQuestion(text) || check.match(text).hasMatch();
}

int tableNumberIn(const QString& request)
{
    static const QRegularExpression number(QStringLiteral("第\\s*([0-9一二三四五六七八九十]+)\\s*[张个]?\\s*表|表\\s*([0-9]+)|\\bT([0-9]+)\\b"));
    const QRegularExpressionMatch match = number.match(request);
    if (!match.hasMatch()) return 0;
    QString digits = match.captured(1) + match.captured(2) + match.captured(3);
    static const QString chinese = QStringLiteral("一二三四五六七八九十");
    if (digits.size() == 1 && chinese.contains(digits)) return chinese.indexOf(digits) + 1;
    return digits.toInt();
}

TableGrid tableGridFrom(const QJsonObject& tableRead)
{
    TableGrid grid;
    for (const QJsonValue& rowValue : tableRead.value(QStringLiteral("rows")).toArray()) {
        QList<TableCell> row;
        for (const QJsonValue& cellValue : rowValue.toArray()) {
            const QJsonObject cell = cellValue.toObject();
            row.append({cell.value(QStringLiteral("text")).toString().trimmed(), cell.value(QStringLiteral("bold")).toBool(),
                        cell.value(QStringLiteral("italic")).toBool(), cell.value(QStringLiteral("align")).toString(),
                        cell.value(QStringLiteral("fill")).toString().toUpper()});
        }
        grid.append(row);
    }
    return grid;
}

QString tableForPrompt(const TableGrid& grid, int table, int maxCells)
{
    int columns = 0;
    for (const auto& row : grid) columns = qMax(columns, int(row.size()));
    QStringList lines{QStringLiteral("表格 T%1，%2 行 %3 列。每格写成 R行号C列号：文字，方括号里是现有格式；空着的格写“（空）”。")
                          .arg(table).arg(grid.size()).arg(columns)};
    int shown = 0;
    for (int r = 0; r < grid.size() && shown < maxCells; ++r)
        for (int c = 0; c < grid[r].size() && shown < maxCells; ++c, ++shown) {
            const TableCell& cell = grid[r][c];
            QStringList format;
            if (cell.bold) format << QStringLiteral("加粗");
            if (cell.italic) format << QStringLiteral("斜体");
            if (cell.align == QStringLiteral("center")) format << QStringLiteral("居中");
            if (cell.align == QStringLiteral("right")) format << QStringLiteral("右对齐");
            if (!cell.fill.isEmpty()) format << QStringLiteral("底色 ") + cell.fill;
            QString text = cell.text.simplified();
            if (text.size() > 80) text = text.left(80) + QStringLiteral("…");
            lines << QStringLiteral("R%1C%2：%3").arg(r + 1).arg(c + 1).arg(text.isEmpty() ? QStringLiteral("（空）") : text)
                         + (format.isEmpty() ? QString() : QStringLiteral(" [") + format.join(QStringLiteral("、")) + QLatin1Char(']'));
        }
    if (shown >= maxCells) lines << QStringLiteral("（表格较大，只列出了前 %1 格。）").arg(maxCells);
    return lines.join(QLatin1Char('\n'));
}

QString tablePlace(int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    const auto part = [](int first, int last, const QString& unit) {
        return first == last ? QStringLiteral("第 %1 %2").arg(first).arg(unit)
                             : QStringLiteral("第 %1–%2 %3").arg(first).arg(last).arg(unit);
    };
    return part(firstRow, lastRow, QStringLiteral("行")) + part(firstColumn, lastColumn, QStringLiteral("列"));
}

RequestKind classifyRequest(const QString& text, bool hasSelection)
{
    const QString request = text.trimmed();
    if (request.isEmpty() || request.startsWith(QLatin1Char('/')) || request.startsWith(QChar(0xFF0F)))
        return RequestKind::Chat;
    // Questions are answered, whether or not something is selected.
    if (isQuestion(request)) return RequestKind::Chat;
    // Thanks and the like are not instructions, even with text selected.
    static const QRegularExpression small(QStringLiteral(
        "^(好的?|好吧|好嘞|谢谢|多谢|感谢|辛苦了|可以了?|行|嗯+|不错|很好|收到|明白|知道了|你好|在吗|ok|OK|Ok)[。！!~～,，]*$"));
    if (small.match(request).hasMatch()) return RequestKind::Chat;
    // With text selected, a short instruction is about that text: 详细点, 改得正式一些, 翻译成英文;
    // unless it names another place in the document ("在文末再加一个表格").
    static const QRegularExpression elsewhere(QStringLiteral("文末|文档的?(最后|末尾|结尾)|全文的?(最后|末尾)"));
    if (hasSelection && request.size() <= 60 && !elsewhere.match(request).hasMatch()) return RequestKind::ReworkSelection;
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
        // A typed instruction that matches a quick action gets that action's instruction too.
        const QString preset = spec.id == QStringLiteral("rework") ? presetForInstruction(input.detail) : spec.id;
        if (const TaskSpec* base = findTask(preset); base && base->scope == TaskScope::Selection && base->id != QStringLiteral("rework"))
            instruction = base->instruction;
        if (preset == QStringLiteral("translate")) {
            instruction = mostlyChinese(input.text)
                ? QStringLiteral("把下面的文字翻译成地道、准确的英文，保持段落数。只输出译文。")
                : QStringLiteral("把下面的文字翻译成通顺、准确的简体中文，保持段落数。只输出译文。");
        }
        if (spec.id == QStringLiteral("rework")) {
            // A short request that is just a quick action ("详细一定啊") adds nothing but a chance
            // that a small model reworks those words instead of the text; longer ones may say more.
            const QString words = input.detail.trimmed();
            if (instruction.isEmpty()) instruction = QStringLiteral("按用户的要求修改下面的原文。");
            if (preset.isEmpty() || words.size() > 8)
                instruction += QStringLiteral("\n用户的要求（这只是要求，不是要修改的文字）：") + words;
            instruction += QStringLiteral("\n不要编造原文没有的数字、日期、人名等事实。");
        }
        // Small models follow a number of characters far better than "about twice as long".
        int characters = 0;
        for (const QChar c : input.text) characters += c.isSpace() ? 0 : 1;
        if (preset == QStringLiteral("expand"))
            instruction += QStringLiteral("\n原文约 %1 字，扩写到 %2 字左右，最多不超过 %3 字。")
                               .arg(characters).arg(qMax(2 * characters, 60)).arg(qMax(3 * characters, 100));
        else if (preset == QStringLiteral("shorten"))
            instruction += QStringLiteral("\n原文约 %1 字，精简到 %2 字左右。").arg(characters).arg(qMax(characters / 2, 10));
        if (input.insist) {
            instruction += QStringLiteral("\n注意：上一次的结果和原文几乎一模一样。这次必须真正按要求修改，结果要和原文有明显不同。");
        }
        system += QStringLiteral("输出要直接替换用户选中的原文，所以不要使用 Markdown 符号（如 #、**、-），"
                                 "按原文的段落分行。");
        user = spec.id == QStringLiteral("rework")
            ? instruction + QStringLiteral("\n\n【原文开始】\n") + clipped(input.text, &truncated) + QStringLiteral("\n【原文结束】")
            : instruction + QStringLiteral("\n\n原文：\n") + clipped(input.text, &truncated);
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
    case TaskScope::Table:
        system = QStringLiteral("你是 Open Office 里的表格修改助手，运行在用户自己的电脑上。按用户的要求检查一张表格，"
                                "列出需要修改的地方，由程序一处一处地改。只输出 JSON。");
        user = QStringLiteral("用户的要求：") + input.detail.trimmed() + QStringLiteral("\n\n") + input.text + QStringLiteral(
            "\n\n请列出修改计划 plan，每一项：\n"
            "- cells：要改的格，例如 R2C3；改格式时也可以写一行或一列的范围，例如 R1C1-R1C4。\n"
            "- change：text（改文字）、bold（加粗）、italic（斜体）、align（对齐）、fill（底色）、color（文字颜色）、fontSize（字号）之一。\n"
            "- value：text 时是这一格完整的新文字；bold、italic 时是 true 或 false；align 是 left、center 或 right；"
            "fill、color 是 #RRGGBB（fill 也可以是 none）；fontSize 是数字。\n"
            "- reason：一句话说明为什么改。\n"
            "规则：只列真正需要改的格，没问题的不要列，不要把整张表重写；改文字只改错的或不统一的地方，其余文字保持不变；"
            "不要编造表里没有的数据，算错的数按表里的数重新算；最多 20 项。没有需要改的，plan 就是空的。");
        if (checkRequest(input.detail))
            user += QStringLiteral("\n用户要你检查时，逐格看：文字有没有错别字或用错的词，同一列的写法是否统一，"
                                   "邮箱、电话、日期等的写法有没有明显错误；数字算得对不对以程序核算的结果为准。");
        break;
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
    if (spec.output == TaskOutput::TablePlan) {
        const QJsonObject text{{QStringLiteral("type"), QStringLiteral("string")}};
        // Sorted keys: cells, change, reason, value (where, what, why, then the new value).
        const QJsonObject item{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{{QStringLiteral("cells"), text},
                         {QStringLiteral("change"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                {QStringLiteral("enum"), QJsonArray{QStringLiteral("text"), QStringLiteral("bold"),
                                                                                                    QStringLiteral("italic"), QStringLiteral("align"),
                                                                                                    QStringLiteral("fill"), QStringLiteral("color"),
                                                                                                    QStringLiteral("fontSize")}}}},
                         {QStringLiteral("reason"), text},
                         {QStringLiteral("value"), text}}},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("cells"), QStringLiteral("change"), QStringLiteral("reason"),
                                                    QStringLiteral("value")}}};
        return {{QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("properties"), QJsonObject{{QStringLiteral("plan"),
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), item}}}}},
                {QStringLiteral("required"), QJsonArray{QStringLiteral("plan")}}};
    }
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

namespace {

// The model's word for a change, as the schema names it.
QString planChange(const QString& given)
{
    const QString change = given.trimmed().toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
    if (change == QStringLiteral("fontsize") || change == QStringLiteral("size") || change.contains(QStringLiteral("字号")))
        return QStringLiteral("fontSize");
    if (change == QStringLiteral("bold") || change.contains(QStringLiteral("加粗")) || change.contains(QStringLiteral("粗体")))
        return QStringLiteral("bold");
    if (change == QStringLiteral("italic") || change.contains(QStringLiteral("斜体"))) return QStringLiteral("italic");
    if (change.startsWith(QStringLiteral("align")) || change.contains(QStringLiteral("对齐"))) return QStringLiteral("align");
    if (change == QStringLiteral("fill") || change.contains(QStringLiteral("background")) || change.contains(QStringLiteral("shading"))
        || change.contains(QStringLiteral("底色")) || change.contains(QStringLiteral("背景")) || change.contains(QStringLiteral("填充")))
        return QStringLiteral("fill");
    if (change == QStringLiteral("color") || change.contains(QStringLiteral("colour")) || change.contains(QStringLiteral("颜色")))
        return QStringLiteral("color");
    if (change == QStringLiteral("text") || change == QStringLiteral("value") || change == QStringLiteral("content")
        || change.contains(QStringLiteral("文字")) || change.contains(QStringLiteral("内容")))
        return QStringLiteral("text");
    return change;
}

struct CellRange {
    int r1 = 0, c1 = 0, r2 = 0, c2 = 0; // 1-based, inclusive
};

// "R2C3", "R1C1-R1C4", "R1", "C2", and lists of them ("R3C3,R5C3", "R2C4、R3C4"):
// the rectangles that lie inside the table.
QList<CellRange> planCells(const QString& given, const TableGrid& grid)
{
    static const QRegularExpression aroundDash(QStringLiteral("\\s*([-:~至到—–])\\s*"));
    static const QRegularExpression separators(QStringLiteral("[,，;；、\\s]+"));
    static const QRegularExpression single(QStringLiteral("^R(\\d+)C(\\d+)$"));
    static const QRegularExpression rectangle(QStringLiteral("^R(\\d+)C(\\d+)[-:~至到—–]R(\\d+)C(\\d+)$"));
    static const QRegularExpression wholeRow(QStringLiteral("^R(\\d+)$"));
    static const QRegularExpression wholeColumn(QStringLiteral("^C(\\d+)$"));
    QString cells = given.toUpper().trimmed();
    cells.replace(aroundDash, QStringLiteral("\\1"));
    const int rows = grid.size();
    QList<CellRange> result;
    for (const QString& part : cells.split(separators, Qt::SkipEmptyParts)) {
        CellRange range;
        QRegularExpressionMatch m;
        if ((m = single.match(part)).hasMatch()) {
            range.r1 = range.r2 = m.captured(1).toInt();
            range.c1 = range.c2 = m.captured(2).toInt();
        } else if ((m = rectangle.match(part)).hasMatch()) {
            range.r1 = qMin(m.captured(1).toInt(), m.captured(3).toInt());
            range.r2 = qMax(m.captured(1).toInt(), m.captured(3).toInt());
            range.c1 = qMin(m.captured(2).toInt(), m.captured(4).toInt());
            range.c2 = qMax(m.captured(2).toInt(), m.captured(4).toInt());
        } else if ((m = wholeRow.match(part)).hasMatch()) {
            range.r1 = range.r2 = m.captured(1).toInt();
            range.c1 = 1;
            range.c2 = range.r1 >= 1 && range.r1 <= rows ? int(grid[range.r1 - 1].size()) : 0;
        } else if ((m = wholeColumn.match(part)).hasMatch()) {
            range.c1 = range.c2 = m.captured(1).toInt();
            range.r1 = 1;
            range.r2 = rows;
        } else {
            continue;
        }
        if (range.r1 < 1 || range.r2 > rows || range.c1 < 1 || range.c2 < range.c1) continue;
        bool inside = true;
        for (int r = range.r1; r <= range.r2; ++r) inside = inside && range.c2 <= grid[r - 1].size();
        if (inside) result.append(range);
    }
    return result;
}

// "bold: true", "align：center", "fill = #D9E2F3": the value without its name.
QString bareValue(const QString& value)
{
    static const QRegularExpression named(QStringLiteral("^\\s*[A-Za-z\\x{4e00}-\\x{9fa5}]{1,10}\\s*[:：=]\\s*"));
    QString bare = value.trimmed();
    bare.remove(named);
    return bare.trimmed();
}

// A color the model wrote as #RRGGBB anywhere in the value, or by name. Cell fills
// get light shades (text stays readable); text colors get strong ones.
QString planColor(const QString& value, bool fill)
{
    static const QRegularExpression hex(QStringLiteral("#?\\b([0-9A-Fa-f]{6})\\b"));
    const QRegularExpressionMatch match = hex.match(value);
    if (match.hasMatch()) return QLatin1Char('#') + match.captured(1).toUpper();
    const QString name = value.toLower();
    struct Named {
        QStringList words;
        const char* light;
        const char* strong;
    };
    static const QList<Named> colors{
        {{QStringLiteral("灰"), QStringLiteral("gray"), QStringLiteral("grey")}, "#D9D9D9", "#7F7F7F"},
        {{QStringLiteral("蓝"), QStringLiteral("blue")}, "#DDEBF7", "#0070C0"},
        {{QStringLiteral("绿"), QStringLiteral("green")}, "#E2EFDA", "#00B050"},
        {{QStringLiteral("黄"), QStringLiteral("yellow")}, "#FFF2CC", "#BF8F00"},
        {{QStringLiteral("橙"), QStringLiteral("orange")}, "#FCE4D6", "#ED7D31"},
        {{QStringLiteral("粉"), QStringLiteral("pink")}, "#FCE4EC", "#E91E63"},
        {{QStringLiteral("紫"), QStringLiteral("purple")}, "#E4DFEC", "#7030A0"},
        {{QStringLiteral("红"), QStringLiteral("red")}, "#F8CBAD", "#C00000"},
        {{QStringLiteral("黑"), QStringLiteral("black")}, "#BFBFBF", "#000000"},
        {{QStringLiteral("白"), QStringLiteral("white")}, "#FFFFFF", "#FFFFFF"},
    };
    for (const Named& color : colors)
        for (const QString& word : color.words)
            if (name.contains(word)) return QString::fromLatin1(fill ? color.light : color.strong);
    return {};
}

// Points, or a Chinese size name (小四 is 12 pt).
int planFontSize(const QString& value)
{
    static const QList<QPair<QString, double>> names{
        {QStringLiteral("小初"), 36}, {QStringLiteral("初号"), 42}, {QStringLiteral("小一"), 24}, {QStringLiteral("一号"), 26},
        {QStringLiteral("小二"), 18}, {QStringLiteral("二号"), 22}, {QStringLiteral("小三"), 15}, {QStringLiteral("三号"), 16},
        {QStringLiteral("小四"), 12}, {QStringLiteral("四号"), 14}, {QStringLiteral("小五"), 9}, {QStringLiteral("五号"), 10.5},
        {QStringLiteral("六号"), 7.5}};
    for (const auto& name : names)
        if (value.contains(name.first)) return qRound(name.second);
    static const QRegularExpression number(QStringLiteral("(\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch match = number.match(value);
    return match.hasMatch() ? qRound(match.captured(1).toDouble()) : 0;
}

// The value only repeats what the cells say ("序号" for R1C1, "事项|负责人|备注" for a header).
bool restatesCells(const QString& value, const TableGrid& grid, int r1, int c1, int r2, int c2)
{
    static const QRegularExpression separators(QStringLiteral("[|,，、/;；\\s]+"));
    QSet<QString> texts;
    for (int r = r1; r <= r2; ++r)
        for (int c = c1; c <= c2; ++c) texts.insert(grid[r - 1][c - 1].text.simplified());
    if (texts.contains(value.simplified()) && !value.trimmed().isEmpty()) return true;
    const QStringList parts = value.split(separators, Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    for (const QString& part : parts)
        if (!texts.contains(part.simplified())) return false;
    return true;
}

} // namespace

QList<PlannedEdit> parseTablePlan(const QString& answer, const TableGrid& grid, int table, const QString& request)
{
    QString json = stripThinking(answer).trimmed();
    const int start = json.indexOf(QLatin1Char('{'));
    const int end = json.lastIndexOf(QLatin1Char('}'));
    if (start < 0 || end <= start) return {};
    const QJsonArray items = QJsonDocument::fromJson(json.mid(start, end - start + 1).toUtf8()).object()
                                 .value(QStringLiteral("plan")).toArray();
    // Small models word the same thing many ways ("bold: true", "居中", "浅蓝色",
    // "R3C3,R5C3"); what they mean is taken, then checked against the table.
    static const QRegularExpression negative(QStringLiteral("false|\\bno\\b|\\boff\\b|取消|不要|不加|去掉|去除|否|^0$"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression textName(QStringLiteral("^\\s*(text|value|文字|内容)\\s*[:：=]\\s*"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression positive(QStringLiteral("^(true|yes|on|1|是|要|加粗|粗体|bold|italic|斜体)?[。.!！]?$"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression namedPart(QStringLiteral("^\\s*([A-Za-z\\x{4e00}-\\x{9fa5}]{1,10})\\s*[:：=]\\s*(.+?)\\s*$"));
    static const QRegularExpression partSeparator(QStringLiteral("[;；]"));
    QList<PlannedEdit> plan;
    QSet<QString> seen;
    for (const QJsonValue& entry : items) {
        const QJsonObject item = entry.toObject();
        const QJsonValue rawValue = item.value(QStringLiteral("value"));
        const QString rawText = rawValue.isString() ? rawValue.toString()
            : rawValue.isBool() ? (rawValue.toBool() ? QStringLiteral("true") : QStringLiteral("false"))
            : rawValue.isDouble() ? QString::number(rawValue.toDouble()) : QString();
        const QString reason = item.value(QStringLiteral("reason")).toString().trimmed();
        // One item may carry several format changes: "bold: true; fill: #D3D3D3".
        QList<QPair<QString, QString>> changes{{planChange(item.value(QStringLiteral("change")).toString()), rawText}};
        const QStringList parts = rawText.split(partSeparator, Qt::SkipEmptyParts);
        if (parts.size() > 1) {
            static const QStringList formats{QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("align"),
                                             QStringLiteral("fill"), QStringLiteral("color"), QStringLiteral("fontSize")};
            QList<QPair<QString, QString>> split;
            for (const QString& part : parts) {
                const QRegularExpressionMatch named = namedPart.match(part);
                if (!named.hasMatch() || !formats.contains(planChange(named.captured(1)))) break;
                split.append({planChange(named.captured(1)), named.captured(2)});
            }
            if (split.size() == parts.size() && formats.contains(changes.first().first)) changes = split;
        }
        for (const auto& pair : changes) {
        const QString value = pair.second;
        for (const CellRange& range : planCells(item.value(QStringLiteral("cells")).toString(), grid)) {
            if (plan.size() >= 20) return plan;
            const int r1 = range.r1, c1 = range.c1, r2 = range.r2, c2 = range.c2;
            // A fill or color whose value is no color: "fill: 已完成" says where, and the color
            // is the one the request names; in a request with no color at all, "fill: 未到" is
            // 填 as in "write in", a text change.
            QString change = pair.first;
            QString requestColor;
            if ((change == QStringLiteral("fill") || change == QStringLiteral("color")) && !request.isEmpty()
                && planColor(bareValue(value), change == QStringLiteral("fill")).isEmpty()) {
                static const QRegularExpression colorWords(QStringLiteral("色|底|背景|\\bcolou?r|\\bbackground|\\bshading|\\bhighlight"),
                                                           QRegularExpression::CaseInsensitiveOption);
                if (colorWords.match(request).hasMatch()) requestColor = planColor(request, change == QStringLiteral("fill"));
                else if (change == QStringLiteral("fill")) change = QStringLiteral("text");
            }
            const auto targets = [&](const std::function<bool(const TableCell&)>& already) {
                for (int r = r1; r <= r2; ++r)
                    for (int c = c1; c <= c2; ++c)
                        if (!already(grid[r - 1][c - 1])) return true;
                return false; // every cell is already like that
            };
            QJsonObject arguments{{QStringLiteral("table"), table},
                                  {QStringLiteral("rows"), r1 == r2 ? QString::number(r1) : QStringLiteral("%1-%2").arg(r1).arg(r2)},
                                  {QStringLiteral("columns"), c1 == c2 ? QString::number(c1) : QStringLiteral("%1-%2").arg(c1).arg(c2)}};
            PlannedEdit edit{tablePlace(r1, c1, r2, c2), QString(), QString(), reason, {}};
            if (change == QStringLiteral("text")) {
                if (r1 != r2 || c1 != c2) continue; // text goes one cell at a time
                const QString before = grid[r1 - 1][c1 - 1].text;
                QString text = value.trimmed();
                text.remove(textName);
                text = text.trimmed();
                if (text.isEmpty() || text == before.trimmed()) continue;
                // Formats written into the text ("text:'小计';bold:True") are not text.
                static const QRegularExpression serialized(QStringLiteral("\\b(bold|italic|fill|align|colou?r|font ?size)\\s*[:：=]"),
                                                           QRegularExpression::CaseInsensitiveOption);
                if (serialized.match(text).hasMatch()) continue;
                edit.before = before;
                edit.after = text;
                edit.command = {QStringLiteral("table.setCell"), {{QStringLiteral("table"), table}, {QStringLiteral("row"), r1},
                                                                  {QStringLiteral("column"), c1}, {QStringLiteral("text"), text}},
                                CommandSource::Assistant};
            } else if (change == QStringLiteral("bold") || change == QStringLiteral("italic")) {
                // "true", "加粗", or the cell's own text again (the model restating where): on;
                // "false", "取消": off; anything else ("单价" for a cell that says 12) is a
                // confused item and is dropped.
                const QString bare = bareValue(value);
                bool on = true;
                if (negative.match(bare).hasMatch()) on = false;
                else if (!positive.match(bare).hasMatch() && !restatesCells(bare, grid, r1, c1, r2, c2)) continue;
                const bool bold = change == QStringLiteral("bold");
                if (!targets([&](const TableCell& cell) { return (bold ? cell.bold : cell.italic) == on; })) continue;
                arguments.insert(change, on);
                edit.after = on ? (bold ? QStringLiteral("加粗") : QStringLiteral("斜体"))
                                : (bold ? QStringLiteral("取消加粗") : QStringLiteral("取消斜体"));
            } else if (change == QStringLiteral("align")) {
                const QString word = bareValue(value).toLower();
                QString align;
                if (word.contains(QStringLiteral("center")) || word.contains(QStringLiteral("centre")) || word.contains(QStringLiteral("middle"))
                    || word.contains(QStringLiteral("居中")) || word.contains(QStringLiteral("中间")))
                    align = QStringLiteral("center");
                else if (word.contains(QStringLiteral("right")) || word.contains(QStringLiteral("右"))) align = QStringLiteral("right");
                else if (word.contains(QStringLiteral("justify")) || word.contains(QStringLiteral("两端"))) align = QStringLiteral("justify");
                else if (word.contains(QStringLiteral("left")) || word.contains(QStringLiteral("左"))) align = QStringLiteral("left");
                if (align.isEmpty()) continue;
                if (!targets([&](const TableCell& cell) { return (cell.align.isEmpty() ? QStringLiteral("left") : cell.align) == align; })) continue;
                arguments.insert(QStringLiteral("align"), align);
                edit.after = align == QStringLiteral("center") ? QStringLiteral("居中") : align == QStringLiteral("right") ? QStringLiteral("右对齐")
                    : align == QStringLiteral("justify") ? QStringLiteral("两端对齐") : QStringLiteral("左对齐");
            } else if (change == QStringLiteral("fill") || change == QStringLiteral("color")) {
                const bool fill = change == QStringLiteral("fill");
                const QString word = bareValue(value);
                const bool none = fill && (word.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0 || word.contains(QStringLiteral("无"))
                                           || word.contains(QStringLiteral("去掉")) || word.contains(QStringLiteral("取消")));
                QString chosen = none ? QStringLiteral("none") : planColor(word, fill);
                if (chosen.isEmpty()) chosen = requestColor;
                if (chosen.isEmpty()) continue;
                if (fill && !targets([&](const TableCell& cell) { return none ? cell.fill.isEmpty() : cell.fill == chosen; })) continue;
                arguments.insert(change, chosen);
                edit.after = !fill ? QStringLiteral("文字颜色 ") + chosen : none ? QStringLiteral("去掉底色") : QStringLiteral("底色 ") + chosen;
            } else if (change == QStringLiteral("fontSize")) {
                const int size = planFontSize(bareValue(value));
                if (size < 6 || size > 72) continue;
                arguments.insert(QStringLiteral("fontSize"), size);
                edit.after = QStringLiteral("字号 %1").arg(size);
            } else {
                continue;
            }
            if (edit.command.name.isEmpty()) edit.command = {QStringLiteral("format.cells"), arguments, CommandSource::Assistant};
            const QString key = QStringLiteral("%1,%2,%3,%4|").arg(r1).arg(c1).arg(r2).arg(c2) + change + QLatin1Char('|') + edit.after;
            if (seen.contains(key)) continue;
            seen.insert(key);
            plan.append(edit);
        }
        }
    }
    return plan;
}

bool cellNumber(const QString& text, double* value)
{
    // One number, with at most a currency sign before it and a short unit after it.
    static const QRegularExpression number(QStringLiteral(
        "^[¥￥$€£\\s]*([-+]?(?:\\d{1,3}(?:,\\d{3})+|\\d+)(?:\\.\\d+)?)\\s*[^\\d\\s.,:：/\\\\\\-]{0,4}$"));
    const QRegularExpressionMatch match = number.match(text.trimmed());
    if (!match.hasMatch()) return false;
    QString digits = match.captured(1);
    digits.remove(QLatin1Char(','));
    bool ok = false;
    const double parsed = digits.toDouble(&ok);
    if (ok && value) *value = parsed;
    return ok;
}

namespace {

bool sameNumber(double a, double b)
{
    return std::fabs(a - b) <= std::max(0.011, std::fabs(b) * 1e-6);
}

// The cell's text with its number replaced by value: "¥80.00" -> "¥60.00", "80" -> "60".
QString withNumber(const QString& original, double value)
{
    static const QRegularExpression digits(QStringLiteral("[-+]?(?:\\d{1,3}(?:,\\d{3})+|\\d+)(?:\\.(\\d+))?"));
    const QRegularExpressionMatch match = digits.match(original);
    int decimals = match.hasMatch() ? int(match.captured(1).size()) : 0;
    while (decimals < 4 && std::fabs(value * std::pow(10.0, decimals) - std::round(value * std::pow(10.0, decimals))) > 1e-6)
        ++decimals;
    QLocale grouped(QLocale::English, QLocale::UnitedStates);
    if (match.hasMatch() && match.captured(0).contains(QLatin1Char(','))) grouped.setNumberOptions({});
    else grouped.setNumberOptions(QLocale::OmitGroupSeparator);
    const QString number = grouped.toString(value, 'f', decimals);
    if (!match.hasMatch()) return number;
    QString result = original.trimmed();
    const QRegularExpressionMatch inTrimmed = digits.match(result);
    result.replace(inTrimmed.capturedStart(0), inTrimmed.capturedLength(0), number);
    return result;
}

QString header(const TableGrid& grid, int column) // 1-based; the first row's text, short
{
    if (grid.isEmpty() || column < 1 || column > grid.first().size()) return {};
    const QString text = grid.first()[column - 1].text.simplified();
    double ignored = 0;
    return text.size() > 12 || cellNumber(text, &ignored) ? QString() : text;
}

QString columnsText(const QList<int>& columns)
{
    if (columns.size() > 2 && columns.last() - columns.first() == columns.size() - 1)
        return QStringLiteral("第 %1–%2 列").arg(columns.first()).arg(columns.last());
    QStringList parts;
    for (int column : columns) parts << QString::number(column);
    return QStringLiteral("第 ") + parts.join(QStringLiteral("、")) + QStringLiteral(" 列");
}

// "单价 × 数量", "语文 + 数学 + 英语"; empty when a column has no header.
QString formulaWords(const TableGrid& grid, const TableFormula& formula)
{
    QString words;
    for (int i = 0; i < formula.from.size(); ++i) {
        const QString name = header(grid, formula.from[i]);
        if (name.isEmpty()) return {};
        if (i) words += formula.product ? QStringLiteral(" × ") : formula.signs.value(i, 1) < 0 ? QStringLiteral(" − ") : QStringLiteral(" + ");
        words += name;
    }
    return words;
}

// "第 2 列 × 第 3 列", "第 2–4 列之和", "第 2 列 + 第 3 列 − 第 4 列".
QString formulaColumns(const TableFormula& formula)
{
    if (formula.product) return QStringLiteral("第 %1 列 × 第 %2 列").arg(formula.from.value(0)).arg(formula.from.value(1));
    if (!formula.signs.contains(-1)) return columnsText(formula.from) + QStringLiteral("之和");
    QString text;
    for (int i = 0; i < formula.from.size(); ++i) {
        if (i) text += formula.signs.value(i, 1) < 0 ? QStringLiteral(" − ") : QStringLiteral(" + ");
        text += QStringLiteral("第 %1 列").arg(formula.from[i]);
    }
    return text;
}

} // namespace

QList<TableFormula> tableFormulas(const TableGrid& grid)
{
    const int rows = grid.size();
    int columns = 0;
    for (const auto& row : grid) columns = qMax(columns, int(row.size()));
    QList<QList<double>> number(rows, QList<double>(columns, 0));
    QList<QList<bool>> has(rows, QList<bool>(columns, false));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < grid[r].size(); ++c) has[r][c] = cellNumber(grid[r][c].text, &number[r][c]);

    struct Candidate {
        QList<int> from; // 0-based
        bool product = false;
        QList<int> signs; // a sum's: +1 or -1 per column
    };
    QList<TableFormula> result;
    for (int k = 0; k < columns; ++k) {
        QList<Candidate> candidates;
        for (int a = 0; a < columns; ++a)
            for (int b = a + 1; b < columns; ++b)
                if (a != k && b != k) candidates.append({{a, b}, true});
        for (int s = 0; s + 1 < k; ++s) { // the columns just before it: s .. k-1, at least two
            Candidate sum{{}, false, {}};
            for (int c = s; c < k; ++c) {
                sum.from.append(c);
                sum.signs.append(1);
            }
            candidates.append(sum);
        }
        // Then with some of them subtracted (余额 = 上期 + 收入 − 支出), up to five columns.
        for (int s = qMax(0, k - 5); s + 1 < k; ++s) {
            const int width = k - s;
            for (int mask = 1; mask < (1 << (width - 1)); ++mask) {
                Candidate sum{{}, false, {}};
                for (int c = s; c < k; ++c) {
                    sum.from.append(c);
                    sum.signs.append(c > s && (mask >> (c - s - 1)) & 1 ? -1 : 1);
                }
                candidates.append(sum);
            }
        }
        int bestHolds = 0;
        TableFormula best;
        for (const Candidate& candidate : candidates) {
            int support = 0;
            int holds = 0;
            QList<TableFormula::Fix> wrong;
            for (int r = 0; r < rows; ++r) {
                if (!has[r][k]) continue;
                bool all = true;
                double value = candidate.product ? 1 : 0;
                for (int i = 0; i < candidate.from.size(); ++i) {
                    const int c = candidate.from[i];
                    all = all && has[r][c];
                    value = candidate.product ? value * number[r][c] : value + candidate.signs.value(i, 1) * number[r][c];
                }
                if (!all) continue;
                ++support;
                if (sameNumber(number[r][k], value)) ++holds;
                else wrong.append({r + 1, grid[r][k].text, withNumber(grid[r][k].text, value)});
            }
            // Evidence: at least three rows, two of them right, and most of them; a formula
            // that subtracts is one of many tried, so it needs three right rows.
            const int needed = candidate.signs.contains(-1) ? 3 : 2;
            if (support < 3 || holds < needed || holds * 10 < support * 6) continue;
            if (holds <= bestHolds) continue; // the first, simplest one wins a tie
            bestHolds = holds;
            best = TableFormula{k + 1, {}, candidate.product, candidate.signs, wrong};
            for (int c : candidate.from) best.from.append(c + 1);
        }
        if (bestHolds) result.append(best);
    }
    return result;
}

QString formulaFacts(const TableGrid& grid, const QList<TableFormula>& formulas)
{
    if (formulas.isEmpty()) return {};
    QStringList lines{QStringLiteral("程序已经核算过表里的数，下面的结果是准确的：照它改，不要自己重新计算，也不要改算对了的数。")};
    for (const TableFormula& formula : formulas) {
        const QString target = header(grid, formula.column);
        QString line = QStringLiteral("- 第 %1 列%2 = %3%4").arg(formula.column)
            .arg(target.isEmpty() ? QString() : QStringLiteral("（%1）").arg(target))
            .arg(formulaColumns(formula))
            .arg(formulaWords(grid, formula).isEmpty() ? QString() : QStringLiteral("（%1）").arg(formulaWords(grid, formula)));
        if (formula.wrong.isEmpty()) line += QStringLiteral("：每一行都算对了。");
        else {
            QStringList fixes;
            for (const TableFormula::Fix& fix : formula.wrong)
                fixes << QStringLiteral("R%1C%2 应为 %3（表里写的是 %4）").arg(fix.row).arg(formula.column).arg(fix.after, fix.before);
            line += QStringLiteral("：") + fixes.join(QStringLiteral("；")) + QStringLiteral("。");
        }
        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

bool checkRequest(const QString& request)
{
    static const QRegularExpression check(QStringLiteral(
        "检查|核对|核算|校对|审核|算错|算对|计算|对不对|有没有.{0,8}(错|问题)|错误|写错|有错|改正|纠正|修正|更正|"
        "\\b(check|verify|fix|correct|wrong|mistakes?|errors?)\\b"), QRegularExpression::CaseInsensitiveOption);
    return check.match(request).hasMatch();
}

QList<PlannedEdit> withFormulaFixes(const QList<PlannedEdit>& plan, const TableGrid& grid, int table,
                                    const QList<TableFormula>& formulas, bool addMissing)
{
    // Every number a formula touches: in a row where it holds, nobody has a reason to
    // change it; in a row where it does not, the program knows the right value.
    QHash<QPair<int, int>, QString> rightText; // (row, column) of a wrong result -> its text
    QHash<QPair<int, int>, double> rightValue;
    QSet<QPair<int, int>> settled;             // numbers in rows where the formula holds
    for (const TableFormula& formula : formulas) {
        QSet<int> wrongRows;
        for (const TableFormula::Fix& fix : formula.wrong) {
            wrongRows.insert(fix.row);
            rightText.insert({fix.row, formula.column}, fix.after);
            double value = 0;
            cellNumber(fix.after, &value);
            rightValue.insert({fix.row, formula.column}, value);
        }
        for (int r = 1; r <= grid.size(); ++r) {
            if (wrongRows.contains(r)) continue;
            double ignored = 0;
            const auto& row = grid[r - 1];
            if (formula.column > row.size() || !cellNumber(row[formula.column - 1].text, &ignored)) continue;
            bool all = true;
            for (int c : formula.from) all = all && c <= row.size() && cellNumber(row[c - 1].text, &ignored);
            if (!all) continue;
            settled.insert({r, formula.column});
            for (int c : formula.from) settled.insert({r, c});
        }
    }
    QList<PlannedEdit> kept;
    QSet<QPair<int, int>> covered;
    for (PlannedEdit edit : plan) {
        if (edit.command.name == QStringLiteral("table.setCell")) {
            const QPair<int, int> cell{edit.command.arguments.value(QStringLiteral("row")).toInt(),
                                       edit.command.arguments.value(QStringLiteral("column")).toInt()};
            if (settled.contains(cell)) continue; // a number that adds up
            if (rightText.contains(cell)) {
                double written = 0;
                if (!cellNumber(edit.after, &written) || !sameNumber(written, rightValue.value(cell))) continue; // miscalculated
                // Right, and written the way the column is ("37.5" -> "¥37.50").
                edit.after = rightText.value(cell);
                edit.command.arguments.insert(QStringLiteral("text"), edit.after);
                covered.insert(cell);
            }
        }
        kept.append(edit);
    }
    if (!addMissing) return kept;
    QList<PlannedEdit> fixes;
    for (const TableFormula& formula : formulas)
        for (const TableFormula::Fix& fix : formula.wrong) {
            if (covered.contains({fix.row, formula.column})) continue;
            const QString words = formulaWords(grid, formula);
            const QString how = !words.isEmpty() ? words : formulaColumns(formula);
            fixes.append({tablePlace(fix.row, formula.column, fix.row, formula.column), fix.before, fix.after,
                          QStringLiteral("按“%1”算，应为 %2").arg(how, fix.after),
                          {QStringLiteral("table.setCell"), {{QStringLiteral("table"), table}, {QStringLiteral("row"), fix.row},
                                                             {QStringLiteral("column"), formula.column}, {QStringLiteral("text"), fix.after}},
                           CommandSource::Assistant}});
        }
    return fixes + kept;
}

QList<PlannedEdit> withSameCleanup(const QList<PlannedEdit>& plan, const TableGrid& grid, int table, const QString& request)
{
    static const QRegularExpression uniform(QStringLiteral("统一|一致|规范|整理|\\b(consistent|uniform|normali[sz]e)\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    if (!uniform.match(request).hasMatch()) return plan;
    static const QString separators = QStringLiteral(" -－—–.·/()（）　");
    // What was taken out of `before` to give `after`, if only separators were.
    const auto removed = [](const QString& before, const QString& after, QString* taken) {
        int i = 0;
        for (const QChar character : before) {
            if (i < after.size() && after[i] == character) ++i;
            else if (separators.contains(character)) { if (!taken->contains(character)) *taken += character; }
            else return false;
        }
        return i == after.size() && !taken->isEmpty();
    };
    struct Column {
        bool allCleanups = true;
        QString taken;
        QSet<int> lengths;
        bool digits = true;
        QSet<int> rows;
    };
    QHash<int, Column> columns;
    for (const PlannedEdit& edit : plan) {
        if (edit.command.name != QStringLiteral("table.setCell")) continue;
        const int column = edit.command.arguments.value(QStringLiteral("column")).toInt();
        Column& info = columns[column];
        info.rows.insert(edit.command.arguments.value(QStringLiteral("row")).toInt());
        QString taken;
        if (!removed(edit.before.trimmed(), edit.after, &taken)) {
            info.allCleanups = false;
            continue;
        }
        for (const QChar character : taken) if (!info.taken.contains(character)) info.taken += character;
        info.lengths.insert(edit.after.size());
        for (const QChar character : edit.after) info.digits = info.digits && character.isDigit();
    }
    QList<PlannedEdit> result = plan;
    for (auto it = columns.begin(); it != columns.end(); ++it) {
        const Column& info = it.value();
        if (!info.allCleanups || info.taken.isEmpty() || info.lengths.size() != 1) continue;
        const int column = it.key();
        const int length = *info.lengths.begin();
        for (int r = 2; r <= grid.size(); ++r) { // the first row is the header
            if (info.rows.contains(r) || column > grid[r - 1].size()) continue;
            // Any separators may go ("138 0013 8000" after the model did "137-0013-7000"),
            // as long as what is left has the shape of the model's results.
            const QString before = grid[r - 1][column - 1].text.trimmed();
            QString after;
            for (const QChar character : before)
                if (!separators.contains(character)) after += character;
            bool digits = true;
            for (const QChar character : after) digits = digits && character.isDigit();
            if (after == before || after.size() != length || (info.digits && !digits)) continue;
            result.append({tablePlace(r, column, r, column), before, after, QStringLiteral("和这一列其他格的整理方式一样"),
                           {QStringLiteral("table.setCell"), {{QStringLiteral("table"), table}, {QStringLiteral("row"), r},
                                                              {QStringLiteral("column"), column}, {QStringLiteral("text"), after}},
                            CommandSource::Assistant}});
        }
    }
    return result.size() > 20 ? result.mid(0, 20) : result;
}

namespace {

bool singleNumber(const QString& text)
{
    bool ok = false;
    text.toInt(&ok);
    return ok;
}

// The request names this header as a column: "价格列", "金额那一列", "the Price column".
bool namesColumn(const QString& request, const QString& header)
{
    if (header.isEmpty()) return false;
    const QRegularExpression column(QRegularExpression::escape(header) + QStringLiteral("\\s*(那一?|这一?|整)?\\s*(列|colum)"),
                                    QRegularExpression::CaseInsensitiveOption);
    return column.match(request).hasMatch();
}

QString argumentKey(const AssistantCommand& command)
{
    return command.name + QString::fromUtf8(QJsonDocument(command.arguments).toJson(QJsonDocument::Compact));
}

} // namespace

QList<PlannedEdit> withRequestedScope(const QList<PlannedEdit>& plan, const TableGrid& grid, int table, const QString& request)
{
    static const QRegularExpression rowWord(QStringLiteral("行|\\brows?\\b"), QRegularExpression::CaseInsensitiveOption);
    QList<PlannedEdit> result;
    QSet<QString> seen;
    for (PlannedEdit edit : plan) {
        if (edit.command.name == QStringLiteral("format.cells")) {
            const QString rows = edit.command.arguments.value(QStringLiteral("rows")).toString();
            const QString columns = edit.command.arguments.value(QStringLiteral("columns")).toString();
            if (singleNumber(rows) && singleNumber(columns)) {
                const int r = rows.toInt();
                const int c = columns.toInt();
                if (r >= 1 && r <= grid.size() && c >= 1 && c <= grid[r - 1].size()) {
                    const QString text = grid[r - 1][c - 1].text.simplified();
                    const int width = int(grid[r - 1].size());
                    if (r >= 2 && !text.isEmpty() && rowWord.match(request).hasMatch() && request.contains(text, Qt::CaseInsensitive)) {
                        // "状态是已完成的那几行": the row of a cell whose text the request names.
                        edit.command.arguments.insert(QStringLiteral("columns"), QStringLiteral("1-%1").arg(width));
                        edit.place = tablePlace(r, 1, r, width);
                    } else if (r == 1 && namesColumn(request, text)) {
                        // "价格列右对齐": the column whose header the request names as a column.
                        edit.command.arguments.insert(QStringLiteral("rows"), QStringLiteral("1-%1").arg(grid.size()));
                        edit.place = tablePlace(1, c, int(grid.size()), c);
                    }
                }
            }
        }
        const QString key = argumentKey(edit.command);
        if (seen.contains(key)) continue;
        seen.insert(key);
        result.append(edit);
    }

    // Formats the request names that the plan left out ("加粗居中，加浅蓝底色" with only
    // bold and fill planned): added where the plan formats, when the request is about
    // one place (no rows or columns of their own).
    QString place = request;
    place.remove(QRegularExpression(QStringLiteral("表头行|标题行|header row|heading row"), QRegularExpression::CaseInsensitiveOption));
    if (QRegularExpression(QStringLiteral("行|列|\\brows?\\b|\\bcolumns?\\b"), QRegularExpression::CaseInsensitiveOption)
            .match(place).hasMatch())
        return result;
    const PlannedEdit* target = nullptr;
    int area = 0;
    const auto size = [](const QString& spec, int count) {
        if (spec == QStringLiteral("all") || spec.isEmpty()) return count;
        const QStringList ends = spec.split(QLatin1Char('-'));
        return ends.size() == 2 ? ends[1].toInt() - ends[0].toInt() + 1 : 1;
    };
    int columnsCount = 0;
    for (const auto& row : grid) columnsCount = qMax(columnsCount, int(row.size()));
    for (const PlannedEdit& edit : result) {
        if (edit.command.name != QStringLiteral("format.cells")) continue;
        const int cells = size(edit.command.arguments.value(QStringLiteral("rows")).toString(), int(grid.size()))
            * size(edit.command.arguments.value(QStringLiteral("columns")).toString(), columnsCount);
        if (cells > area) {
            area = cells;
            target = &edit;
        }
    }
    if (!target) return result;
    struct Wanted {
        QString key;
        QJsonValue value;
        QString words;
    };
    QList<Wanted> wanted;
    const auto asks = [&request](const char* pattern) {
        return QRegularExpression(QString::fromUtf8(pattern), QRegularExpression::CaseInsensitiveOption).match(request).hasMatch();
    };
    if (asks("(?<!不|取消|别)(加粗|粗体)|\\bbold\\b")) wanted.append({QStringLiteral("bold"), true, QStringLiteral("加粗")});
    if (asks("(?<!不|取消|别)斜体|\\bitalic")) wanted.append({QStringLiteral("italic"), true, QStringLiteral("斜体")});
    if (asks("居中|\\bcent(er|re)")) wanted.append({QStringLiteral("align"), QStringLiteral("center"), QStringLiteral("居中")});
    else if (asks("右对齐|靠右|right[- ]?align|align(ed)? (to the )?right")) wanted.append({QStringLiteral("align"), QStringLiteral("right"), QStringLiteral("右对齐")});
    else if (asks("左对齐|靠左|left[- ]?align|align(ed)? (to the )?left")) wanted.append({QStringLiteral("align"), QStringLiteral("left"), QStringLiteral("左对齐")});
    const QString fill = planColor(request, true);
    if (!fill.isEmpty() && asks("底色|背景|填充|底纹|\\bbackground|\\bshading|\\bfill")
        && !asks("文字颜色|字体颜色|字的颜色|\\btext colou?r|\\bfont colou?r"))
        wanted.append({QStringLiteral("fill"), fill, QStringLiteral("底色 ") + fill});
    QList<PlannedEdit> added;
    for (const Wanted& want : wanted) {
        bool planned = false;
        for (const PlannedEdit& edit : result)
            planned = planned || (edit.command.name == QStringLiteral("format.cells") && edit.command.arguments.contains(want.key));
        if (planned) continue;
        PlannedEdit edit{target->place, QString(), want.words, QStringLiteral("你要求了“%1”").arg(want.words), target->command};
        QJsonObject arguments{{QStringLiteral("table"), table},
                              {QStringLiteral("rows"), target->command.arguments.value(QStringLiteral("rows"))},
                              {QStringLiteral("columns"), target->command.arguments.value(QStringLiteral("columns"))},
                              {want.key, want.value}};
        edit.command.arguments = arguments;
        added.append(edit);
    }
    return result + added;
}

namespace {

QRegularExpression phrase(const char* pattern)
{
    return QRegularExpression(QString::fromUtf8(pattern), QRegularExpression::CaseInsensitiveOption);
}

// "第三", "12": a 1-based number; 0 when it is not one.
int ordinal(const QString& text)
{
    static const QString digits = QStringLiteral("一二三四五六七八九");
    if (text == QStringLiteral("十")) return 10;
    if (text.size() == 2 && text.startsWith(QStringLiteral("十")) && digits.contains(text[1])) return 10 + digits.indexOf(text[1]) + 1;
    if (text.size() == 1 && digits.contains(text)) return digits.indexOf(text) + 1;
    return text.toInt();
}

// A header's words without a unit in brackets: "单价（元）" -> "单价".
QString headerCore(const QString& header)
{
    QString core = header.simplified();
    core.remove(QRegularExpression(QStringLiteral("[（(][^）)]*[）)]")));
    return core.trimmed();
}

struct ClauseTarget {
    QList<int> rows; // 1-based, sorted; empty with columns set: every row
    QList<int> columns; // 1-based; empty: every column
    bool found = false;
};

ClauseTarget clauseTarget(const QString& clause, const TableGrid& grid)
{
    ClauseTarget target;
    const int rows = grid.size();
    QSet<int> rowSet;
    QSet<int> columnSet;
    if (phrase("整张表|全表|整个表格?|所有单元格|全部单元格|\\bwhole table|\\bentire table|\\ball cells").match(clause).hasMatch()) {
        target.found = true;
        return target; // everything
    }
    if (phrase("表头|标题行|首行|第一行|\\bheaders?( row)?\\b|\\bheading row|\\btop row|\\bfirst row").match(clause).hasMatch())
        rowSet.insert(1);
    static const QRegularExpression numberedRows(QStringLiteral("第\\s*([0-9一二三四五六七八九十]+)\\s*(?:[到至~—–-]\\s*第?\\s*([0-9一二三四五六七八九十]+)\\s*)?行"));
    for (auto it = numberedRows.globalMatch(clause); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const int first = ordinal(match.captured(1));
        const int last = match.captured(2).isEmpty() ? first : ordinal(match.captured(2));
        for (int r = first; r >= 1 && r <= last && r <= rows; ++r) rowSet.insert(r);
    }
    static const QRegularExpression numberedColumns(QStringLiteral("第\\s*([0-9一二三四五六七八九十]+)\\s*列"));
    for (auto it = numberedColumns.globalMatch(clause); it.hasNext();) {
        const int column = ordinal(it.next().captured(1));
        if (column >= 1) columnSet.insert(column);
    }
    // A column by its header: "小计那一列", "the Price column".
    if (!grid.isEmpty())
        for (int c = 1; c <= grid.first().size(); ++c) {
            const QString core = headerCore(grid.first()[c - 1].text);
            if (core.size() >= 2 && namesColumn(clause, core)) columnSet.insert(c);
        }
    // Rows by what a cell says: "已取消的预约整行", "状态是已完成的那几行", "张三所在的行".
    static const QRegularExpression byText(QStringLiteral(
        "([^，,。的是为把将在和与及]{1,12}?)(?:的[^，,。行]{0,6}?(?:整行|那几行|那些行|这几行|这些行|所有行|各行|一行|行)|所在的?行)"));
    const QRegularExpressionMatch condition = byText.match(clause);
    if (condition.hasMatch()) {
        const QString wanted = condition.captured(1).trimmed();
        QSet<int> exact, containing;
        for (int r = 2; r <= rows; ++r)
            for (const TableCell& cell : grid[r - 1]) {
                const QString text = cell.text.simplified();
                if (text.isEmpty()) continue;
                if (text == wanted) exact.insert(r);
                else if (text.contains(wanted)) containing.insert(r);
            }
        const QSet<int>& found = exact.isEmpty() ? containing : exact;
        if (!found.isEmpty()) {
            rowSet.unite(found);
        }
    }
    if (rowSet.isEmpty() && columnSet.isEmpty()) return target;
    target.found = true;
    target.rows = QList<int>(rowSet.begin(), rowSet.end());
    std::sort(target.rows.begin(), target.rows.end());
    target.columns = QList<int>(columnSet.begin(), columnSet.end());
    std::sort(target.columns.begin(), target.columns.end());
    return target;
}

struct ClauseFormat {
    QString key;
    QJsonValue value;
    QString words;
};

QList<ClauseFormat> clauseFormats(const QString& clause, bool* unclear)
{
    QList<ClauseFormat> formats;
    if (phrase("取消加粗|去掉加粗|不要加粗|不加粗|\\bunbold|\\bnot bold").match(clause).hasMatch())
        formats.append({QStringLiteral("bold"), false, QStringLiteral("取消加粗")});
    else if (phrase("加粗|粗体|\\bbold").match(clause).hasMatch())
        formats.append({QStringLiteral("bold"), true, QStringLiteral("加粗")});
    if (phrase("取消斜体|去掉斜体|不要斜体").match(clause).hasMatch())
        formats.append({QStringLiteral("italic"), false, QStringLiteral("取消斜体")});
    else if (phrase("斜体|倾斜|\\bitalics?\\b").match(clause).hasMatch())
        formats.append({QStringLiteral("italic"), true, QStringLiteral("斜体")});
    if (phrase("居中|中间对齐|\\bcent(er|re)(ed)?\\b").match(clause).hasMatch())
        formats.append({QStringLiteral("align"), QStringLiteral("center"), QStringLiteral("居中")});
    else if (phrase("右对齐|靠右|right[- ]align|align(ed)? (to the )?right").match(clause).hasMatch())
        formats.append({QStringLiteral("align"), QStringLiteral("right"), QStringLiteral("右对齐")});
    else if (phrase("左对齐|靠左|left[- ]align|align(ed)? (to the )?left").match(clause).hasMatch())
        formats.append({QStringLiteral("align"), QStringLiteral("left"), QStringLiteral("左对齐")});
    else if (phrase("两端对齐|\\bjustif").match(clause).hasMatch())
        formats.append({QStringLiteral("align"), QStringLiteral("justify"), QStringLiteral("两端对齐")});
    const bool textColor = phrase("文字颜色|字体颜色|字的颜色|色的字|色字|字体.{0,4}色|标红|\\btext colou?r|\\bfont colou?r|\\bin red\\b").match(clause).hasMatch();
    if (phrase("去掉底色|取消底色|去掉背景|取消背景|不要底色|\\bno background|\\bremove .{0,12}(background|shading)").match(clause).hasMatch()) {
        formats.append({QStringLiteral("fill"), QStringLiteral("none"), QStringLiteral("去掉底色")});
    } else if (!textColor && phrase("底色|背景|填充|底纹|(标|涂|填|改)成?.{0,4}色|色.{0,2}(标出|标记)|\\bbackground|\\bshading|\\bhighlight|\\bfill").match(clause).hasMatch()) {
        const QString color = planColor(clause, true);
        if (color.isEmpty()) *unclear = true;
        else formats.append({QStringLiteral("fill"), color, QStringLiteral("底色 ") + color});
    }
    if (textColor) {
        const QString color = planColor(clause, false);
        if (color.isEmpty()) *unclear = true;
        else formats.append({QStringLiteral("color"), color, QStringLiteral("文字颜色 ") + color});
    }
    static const QRegularExpression size(QStringLiteral(
        "字号.{0,4}?(\\d+(?:\\.\\d+)?|小?[初一二三四五六]号?)|(\\d+(?:\\.\\d+)?)\\s*(?:pt|磅|号字)|font size (?:of |to )?(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch sized = size.match(clause);
    if (sized.hasMatch()) {
        const int points = planFontSize(sized.captured(0).mid(sized.captured(0).startsWith(QStringLiteral("字号")) ? 2 : 0));
        if (points >= 6 && points <= 72) formats.append({QStringLiteral("fontSize"), points, QStringLiteral("字号 %1").arg(points)});
        else *unclear = true;
    }
    return formats;
}

QString numbersText(const QList<int>& numbers, const QString& unit)
{
    if (numbers.size() > 1 && numbers.last() - numbers.first() == numbers.size() - 1)
        return QStringLiteral("第 %1–%2 %3").arg(numbers.first()).arg(numbers.last()).arg(unit);
    QStringList parts;
    for (int number : numbers) parts << QString::number(number);
    return QStringLiteral("第 %1 %2").arg(parts.join(QStringLiteral("、")), unit);
}

QString numbersSpec(const QList<int>& numbers)
{
    if (numbers.size() > 1 && numbers.last() - numbers.first() == numbers.size() - 1)
        return QStringLiteral("%1-%2").arg(numbers.first()).arg(numbers.last());
    QStringList parts;
    for (int number : numbers) parts << QString::number(number);
    return parts.join(QLatin1Char(','));
}

} // namespace

QList<PlannedEdit> requestedFormats(const QString& request, const TableGrid& grid, int table, bool* allUnderstood)
{
    *allUnderstood = true;
    static const QRegularExpression separators(QStringLiteral("[，,；;。！!？?\\n]|\\band\\b|然后|并且|同时|另外|再"),
                                               QRegularExpression::CaseInsensitiveOption);
    int width = 0;
    for (const auto& row : grid) width = qMax(width, int(row.size()));
    QList<PlannedEdit> edits;
    ClauseTarget current;
    for (const QString& part : request.split(separators, Qt::SkipEmptyParts)) {
        const QString clause = part.trimmed();
        if (clause.isEmpty()) continue;
        bool unclear = false;
        const QList<ClauseFormat> formats = clauseFormats(clause, &unclear);
        const ClauseTarget target = clauseTarget(clause, grid);
        if (target.found) current = target; // a clause without a place goes on the last one named
        if (unclear) *allUnderstood = false;
        if (formats.isEmpty()) continue;
        if (!current.found) {
            *allUnderstood = false;
            continue;
        }
        QList<int> rows = current.rows;
        QList<int> columns = current.columns;
        if (rows.isEmpty()) for (int r = 1; r <= grid.size(); ++r) rows.append(r);
        if (columns.isEmpty()) for (int c = 1; c <= width; ++c) columns.append(c);
        QJsonObject arguments{{QStringLiteral("table"), table}, {QStringLiteral("rows"), numbersSpec(rows)},
                              {QStringLiteral("columns"), numbersSpec(columns)}};
        QStringList done;
        for (const ClauseFormat& format : formats) {
            // Nothing that is already so in every one of those cells.
            bool already = true;
            for (int r : rows)
                for (int c : columns) {
                    if (r > grid.size() || c > grid[r - 1].size()) continue;
                    const TableCell& cell = grid[r - 1][c - 1];
                    if (format.key == QStringLiteral("bold")) already = already && cell.bold == format.value.toBool();
                    else if (format.key == QStringLiteral("italic")) already = already && cell.italic == format.value.toBool();
                    else if (format.key == QStringLiteral("align"))
                        already = already && (cell.align.isEmpty() ? QStringLiteral("left") : cell.align) == format.value.toString();
                    else if (format.key == QStringLiteral("fill"))
                        already = already && (format.value.toString() == QStringLiteral("none") ? cell.fill.isEmpty() : cell.fill == format.value.toString());
                    else already = false; // text color and size are not read back
                }
            if (already) continue;
            arguments.insert(format.key, format.value);
            done << format.words;
        }
        if (done.isEmpty()) continue;
        const QString place = rows.size() == grid.size() && columns.size() == width ? QStringLiteral("整张表格")
            : rows.size() == grid.size() ? numbersText(columns, QStringLiteral("列"))
            : columns.size() == width ? numbersText(rows, QStringLiteral("行"))
            : numbersText(rows, QStringLiteral("行")) + numbersText(columns, QStringLiteral("列"));
        edits.append({place, QString(), done.join(QStringLiteral("、")), QStringLiteral("按你的要求：%1").arg(clause.left(40)),
                      {QStringLiteral("format.cells"), arguments, CommandSource::Assistant}});
    }
    return edits;
}

QList<OutlineParagraph> outlineParagraphs(const QJsonArray& blocks)
{
    QList<OutlineParagraph> paragraphs;
    for (const QJsonValue& value : blocks) {
        const QJsonObject block = value.toObject();
        const QString id = block.value(QStringLiteral("id")).toString();
        if (!id.startsWith(QLatin1Char('P'))) continue;
        paragraphs.append({id.mid(1).toInt(), block.value(QStringLiteral("text")).toString(), block.value(QStringLiteral("style")).toString(),
                           block.value(QStringLiteral("bold")).toBool(), block.value(QStringLiteral("italic")).toBool(),
                           block.value(QStringLiteral("align")).toString(), block.value(QStringLiteral("size")).toDouble(),
                           block.value(QStringLiteral("color")).toString()});
    }
    return paragraphs;
}

namespace {

// The formats a clause about a document asks for. A color is the text's, unless the
// clause says background or highlight (paragraphs have no fill: that is a highlight).
QList<ClauseFormat> documentClauseFormats(const QString& clause, bool* unclear)
{
    QList<ClauseFormat> formats;
    if (phrase("取消加粗|去掉加粗|不要加粗|不加粗|\\bunbold|\\bnot bold").match(clause).hasMatch())
        formats.append({QStringLiteral("bold"), false, QStringLiteral("取消加粗")});
    else if (phrase("加粗|粗体|\\bbold").match(clause).hasMatch())
        formats.append({QStringLiteral("bold"), true, QStringLiteral("加粗")});
    if (phrase("取消斜体|去掉斜体").match(clause).hasMatch())
        formats.append({QStringLiteral("italic"), false, QStringLiteral("取消斜体")});
    else if (phrase("斜体|倾斜|\\bitalics?\\b").match(clause).hasMatch())
        formats.append({QStringLiteral("italic"), true, QStringLiteral("斜体")});
    if (phrase("取消下划线|去掉下划线").match(clause).hasMatch())
        formats.append({QStringLiteral("underline"), false, QStringLiteral("去掉下划线")});
    else if (phrase("下划线|\\bunderline").match(clause).hasMatch())
        formats.append({QStringLiteral("underline"), true, QStringLiteral("下划线")});
    if (phrase("居中|中间对齐|\\bcent(er|re)(ed)?\\b").match(clause).hasMatch())
        formats.append({QStringLiteral("align"), QStringLiteral("center"), QStringLiteral("居中")});
    else if (phrase("右对齐|靠右|right[- ]align|align(ed)? (to the )?right").match(clause).hasMatch())
        formats.append({QStringLiteral("align"), QStringLiteral("right"), QStringLiteral("右对齐")});
    else if (phrase("左对齐|靠左|left[- ]align|align(ed)? (to the )?left").match(clause).hasMatch())
        formats.append({QStringLiteral("align"), QStringLiteral("left"), QStringLiteral("左对齐")});
    else if (phrase("两端对齐|\\bjustif").match(clause).hasMatch())
        formats.append({QStringLiteral("align"), QStringLiteral("justify"), QStringLiteral("两端对齐")});
    struct Style {
        const char* pattern;
        const char* value;
        const char* words;
    };
    static const Style styles[] = {{"(一|1)\\s*级标题|标题\\s*1|\\bheading\\s*1", "heading1", "一级标题"},
                                   {"(二|2)\\s*级标题|标题\\s*2|\\bheading\\s*2", "heading2", "二级标题"},
                                   {"(三|3)\\s*级标题|标题\\s*3|\\bheading\\s*3", "heading3", "三级标题"},
                                   {"正文样式|改成正文|改为正文|恢复成?正文|\\bnormal style", "normal", "正文样式"}};
    for (const Style& style : styles)
        if (phrase(style.pattern).match(clause).hasMatch()) {
            formats.append({QStringLiteral("style"), QString::fromLatin1(style.value), QString::fromUtf8(style.words)});
            break;
        }
    if (phrase("去掉(突出显示|高亮|底色|背景)|取消(突出显示|高亮)").match(clause).hasMatch()) {
        formats.append({QStringLiteral("highlight"), QStringLiteral("none"), QStringLiteral("去掉突出显示")});
    } else if (phrase("突出显示|高亮|底色|背景|标黄|标绿|涂黄|\\bhighlight|\\bbackground").match(clause).hasMatch()) {
        const QString name = clause.toLower();
        QString color = QStringLiteral("yellow");
        if (name.contains(QStringLiteral("绿")) || name.contains(QStringLiteral("green"))) color = QStringLiteral("green");
        else if (name.contains(QStringLiteral("蓝")) || name.contains(QStringLiteral("青")) || name.contains(QStringLiteral("blue")) || name.contains(QStringLiteral("cyan"))) color = QStringLiteral("cyan");
        else if (name.contains(QStringLiteral("灰")) || name.contains(QStringLiteral("gray")) || name.contains(QStringLiteral("grey"))) color = QStringLiteral("lightGray");
        else if (name.contains(QStringLiteral("粉")) || name.contains(QStringLiteral("紫")) || name.contains(QStringLiteral("pink"))) color = QStringLiteral("magenta");
        else if (name.contains(QStringLiteral("红")) || name.contains(QStringLiteral("red"))) color = QStringLiteral("red");
        formats.append({QStringLiteral("highlight"), color, QStringLiteral("突出显示")});
    } else if (phrase("[红蓝绿黄橙紫灰黑]色|标红|红字|\\b(red|blue|green|orange|purple|gr[ae]y|black)\\b").match(clause).hasMatch()) {
        const QString color = planColor(clause, false);
        if (color.isEmpty()) *unclear = true;
        else formats.append({QStringLiteral("color"), color, QStringLiteral("文字颜色 ") + color});
    }
    static const QRegularExpression size(QStringLiteral(
        "字号.{0,4}?(\\d+(?:\\.\\d+)?|小?[初一二三四五六]号?)|(\\d+(?:\\.\\d+)?)\\s*(?:pt|磅|号字)|font size (?:of |to )?(\\d+)|"
        "(?:改成|改为|设为|用)(小?[初一二三四五六]号)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch sized = size.match(clause);
    if (sized.hasMatch()) {
        const int points = planFontSize(sized.captured(0).mid(sized.captured(0).startsWith(QStringLiteral("字号")) ? 2 : 0));
        if (points >= 6 && points <= 72) formats.append({QStringLiteral("fontSize"), points, QStringLiteral("字号 %1").arg(points)});
        else *unclear = true;
    }
    return formats;
}

struct DocumentTarget {
    QList<int> paragraphs; // P numbers
    QStringList texts;     // words to find
    bool found = false;
};

DocumentTarget documentClauseTarget(const QString& clause, const QList<OutlineParagraph>& paragraphs)
{
    DocumentTarget target;
    QList<const OutlineParagraph*> written; // as people count paragraphs: those with text
    for (const OutlineParagraph& paragraph : paragraphs)
        if (!paragraph.text.trimmed().isEmpty()) written.append(&paragraph);
    const int count = written.size();
    // Style words name a format, not a place: "改成二级标题" is not about "the title".
    QString place = clause;
    place.remove(phrase("(一|二|三|1|2|3)\\s*级标题|标题\\s*[123]|\\bheading\\s*[123]|标题样式|正文样式"));
    QStringList quoted;
    static const QRegularExpression quote(QStringLiteral("[“\"「『‘']([^”\"」』’']{1,30})[”\"」』’']"));
    for (auto it = quote.globalMatch(place); it.hasNext();) quoted << it.next().captured(1).trimmed();
    QSet<int> chosen;
    const auto add = [&chosen](const OutlineParagraph* paragraph) { chosen.insert(paragraph->number); };
    static const QRegularExpression starts(QStringLiteral("开头|打头|\\bstart(s|ing)? with|\\bbegin(s|ning)? with"),
                                           QRegularExpression::CaseInsensitiveOption);
    if (starts.match(place).hasMatch()) {
        QStringList beginnings = quoted;
        static const QRegularExpression marker(QStringLiteral("（[一二三四五六七八九十\\d]+）|\\([一二三四五六七八九十\\d]+\\)|[一二三四五六七八九十]+、|\\d+[.、]"));
        if (beginnings.isEmpty())
            for (auto it = marker.globalMatch(place); it.hasNext();) beginnings << it.next().captured(0);
        for (const OutlineParagraph* paragraph : written)
            for (const QString& beginning : beginnings)
                if (!beginning.isEmpty() && paragraph->text.trimmed().startsWith(beginning)) add(paragraph);
    } else if (!quoted.isEmpty() && phrase("(含有|包含|带有|有).{0,8}(段|行|paragraph)|\\bparagraphs? (that )?contain").match(place).hasMatch()) {
        for (const OutlineParagraph* paragraph : written)
            for (const QString& word : quoted)
                if (paragraph->text.contains(word)) add(paragraph);
    } else if (!quoted.isEmpty()) {
        target.texts = quoted;
        target.found = true;
        return target;
    }
    static const QRegularExpression containing(QStringLiteral("(?:含有|包含|带有)([^的，,。“”\"]{1,12})的(?:段落|段|那几段|这几段|行)"));
    const QRegularExpressionMatch contains = containing.match(place);
    if (chosen.isEmpty() && contains.hasMatch())
        for (const OutlineParagraph* paragraph : written)
            if (paragraph->text.contains(contains.captured(1).trimmed())) add(paragraph);
    // Numbered: 第 3 段, 第三段到第四段, 最后两段, 倒数第二段.
    static const QRegularExpression numbered(QStringLiteral(
        "第\\s*([0-9一二三四五六七八九十]+)\\s*(?:段|行)?\\s*(?:[到至~—–-]\\s*第?\\s*([0-9一二三四五六七八九十]+)\\s*)?(?:段|行)"));
    for (auto it = numbered.globalMatch(place); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const int first = ordinal(match.captured(1));
        const int last = match.captured(2).isEmpty() ? first : ordinal(match.captured(2));
        for (int n = first; n >= 1 && n <= last && n <= count; ++n) add(written[n - 1]);
    }
    static const QRegularExpression fromEnd(QStringLiteral("倒数第\\s*([0-9一二三四五六七八九十两]+)\\s*(?:段|行)"));
    const QRegularExpressionMatch back = fromEnd.match(place);
    if (back.hasMatch()) {
        const int n = back.captured(1) == QStringLiteral("两") ? 2 : ordinal(back.captured(1));
        if (n >= 1 && n <= count) add(written[count - n]);
    } else {
        static const QRegularExpression last(QStringLiteral("最后\\s*([0-9一二三四五六七八九十两]*)\\s*(?:段|行)|末段|\\bthe last paragraph"),
                                             QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch tail = last.match(place);
        if (tail.hasMatch()) {
            const QString words = tail.captured(1);
            const int n = words.isEmpty() ? 1 : words == QStringLiteral("两") ? 2 : ordinal(words);
            for (int i = qMax(0, count - n); i < count; ++i) add(written[i]);
        }
    }
    static const QRegularExpression english(QStringLiteral("\\b(first|second|third|fourth|fifth) paragraph|\\bparagraph (\\d+)"),
                                            QRegularExpression::CaseInsensitiveOption);
    for (auto it = english.globalMatch(place); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        static const QStringList words{QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third"),
                                       QStringLiteral("fourth"), QStringLiteral("fifth")};
        const int n = match.captured(2).isEmpty() ? int(words.indexOf(match.captured(1).toLower())) + 1 : match.captured(2).toInt();
        if (n >= 1 && n <= count) add(written[n - 1]);
    }
    // The title: the first paragraph with text ("小标题", "所有标题" are not it).
    if (phrase("(?<![小副各级所有每个子])标题|大标题|题目|\\bthe title\\b|\\btitle\\b").match(place).hasMatch() && count)
        add(written.first());
    if (phrase("全文|全部段落|所有段落|每一?段|整篇|整个文档|全部文字|所有文字|\\bwhole document|\\ball (the )?(text|paragraphs)|\\bevery paragraph")
            .match(place).hasMatch())
        for (const OutlineParagraph* paragraph : written) add(paragraph);
    if (chosen.isEmpty()) return target;
    target.paragraphs = QList<int>(chosen.begin(), chosen.end());
    std::sort(target.paragraphs.begin(), target.paragraphs.end());
    target.found = true;
    return target;
}

} // namespace

QList<PlannedEdit> requestedDocumentFormats(const QString& request, const QList<OutlineParagraph>& paragraphs,
                                            bool hasSelection, bool* understood)
{
    *understood = false;
    static const QRegularExpression separators(QStringLiteral("[，,；;。！!？?\\n]|\\band\\b|然后|并且|同时|另外|再"),
                                               QRegularExpression::CaseInsensitiveOption);
    QHash<int, int> shown; // P number -> its place as people count
    for (const OutlineParagraph& paragraph : paragraphs)
        if (!paragraph.text.trimmed().isEmpty()) shown.insert(paragraph.number, int(shown.size()) + 1);
    QHash<int, const OutlineParagraph*> byNumber;
    for (const OutlineParagraph& paragraph : paragraphs) byNumber.insert(paragraph.number, &paragraph);
    QList<PlannedEdit> edits;
    DocumentTarget current;
    bool clear = true;
    bool anyFormat = false;
    // A request with quotes keeps its separators inside them: split outside quotes only.
    QString masked = request;
    bool inQuote = false;
    for (QChar& character : masked) {
        if (character == QChar(0x201C) || character == QChar(0x300C)) inQuote = true;
        else if (character == QChar(0x201D) || character == QChar(0x300D)) inQuote = false;
        else if (inQuote && QStringLiteral("，,；;。").contains(character)) character = QChar(0x2063);
    }
    for (QString clause : masked.split(separators, Qt::SkipEmptyParts)) {
        clause.replace(QChar(0x2063), QStringLiteral("，"));
        clause = clause.trimmed();
        if (clause.isEmpty()) continue;
        const QList<ClauseFormat> formats = documentClauseFormats(clause, &clear) ;
        const DocumentTarget target = documentClauseTarget(clause, paragraphs);
        if (target.found) current = target;
        if (formats.isEmpty()) continue;
        anyFormat = true;
        if (!current.found) {
            if (!hasSelection) {
                clear = false;
                continue;
            }
            // Nothing named: the selection.
            QJsonObject arguments;
            QStringList done;
            for (const ClauseFormat& format : formats) {
                arguments.insert(format.key, format.value);
                done << format.words;
            }
            edits.append({QStringLiteral("选中的文字"), QString(), done.join(QStringLiteral("、")),
                          QStringLiteral("按你的要求：%1").arg(clause.left(40)),
                          {QStringLiteral("format.selection"), arguments, CommandSource::Assistant}});
            continue;
        }
        if (!current.texts.isEmpty()) {
            // Words: text formats only (no alignment or paragraph style for a few words).
            QJsonObject arguments;
            QStringList done;
            for (const ClauseFormat& format : formats) {
                if (format.key == QStringLiteral("align") || format.key == QStringLiteral("style")) {
                    clear = false;
                    continue;
                }
                arguments.insert(format.key, format.value);
                done << format.words;
            }
            if (done.isEmpty()) continue;
            for (const QString& text : current.texts) {
                QJsonObject withFind = arguments;
                withFind.insert(QStringLiteral("find"), text);
                edits.append({QStringLiteral("文中所有“%1”").arg(text), QString(), done.join(QStringLiteral("、")),
                              QStringLiteral("按你的要求：%1").arg(clause.left(40)),
                              {QStringLiteral("format.text"), withFind, CommandSource::Assistant}});
            }
            continue;
        }
        // Paragraphs: no underline or highlight for whole paragraphs (the model may do those).
        QJsonObject arguments;
        QStringList done;
        for (const ClauseFormat& format : formats) {
            if (format.key == QStringLiteral("underline") || format.key == QStringLiteral("highlight")) {
                clear = false;
                continue;
            }
            bool already = true;
            for (int number : current.paragraphs) {
                const OutlineParagraph* paragraph = byNumber.value(number);
                if (!paragraph) continue;
                if (format.key == QStringLiteral("bold")) already = already && paragraph->bold == format.value.toBool();
                else if (format.key == QStringLiteral("italic")) already = already && paragraph->italic == format.value.toBool();
                else if (format.key == QStringLiteral("align"))
                    already = already && (paragraph->align.isEmpty() ? QStringLiteral("left") : paragraph->align) == format.value.toString();
                else if (format.key == QStringLiteral("fontSize")) already = already && qFuzzyCompare(paragraph->size, format.value.toDouble());
                else if (format.key == QStringLiteral("style")) {
                    const QString wanted = format.value.toString();
                    const QString name = wanted == QStringLiteral("normal") ? QStringLiteral("Normal")
                        : QStringLiteral("Heading ") + wanted.right(1);
                    already = already && (paragraph->style == name || (wanted == QStringLiteral("normal") && paragraph->style.isEmpty()));
                } else already = false;
            }
            if (already) continue;
            arguments.insert(format.key, format.value);
            done << format.words;
        }
        if (done.isEmpty()) continue;
        QList<int> people;
        for (int number : current.paragraphs) people << shown.value(number);
        QStringList numbers;
        for (int number : current.paragraphs) numbers << QString::number(number);
        arguments.insert(QStringLiteral("paragraphs"), numbers.join(QLatin1Char(',')));
        QString place = numbersText(people, QStringLiteral("段"));
        if (current.paragraphs.size() == 1 && byNumber.value(current.paragraphs.first())) {
            QString text = byNumber.value(current.paragraphs.first())->text.simplified();
            place += QStringLiteral("（%1）").arg(text.size() > 12 ? text.left(12) + QStringLiteral("…") : text);
        }
        edits.append({place, QString(), done.join(QStringLiteral("、")), QStringLiteral("按你的要求：%1").arg(clause.left(40)),
                      {QStringLiteral("format.paragraphs"), arguments, CommandSource::Assistant}});
    }
    *understood = anyFormat && clear;
    // One item per place: "标题加粗、居中，字号改成三号" is one change of the title.
    QList<PlannedEdit> merged;
    for (const PlannedEdit& edit : edits) {
        const auto where = [](const AssistantCommand& command) {
            return command.name + QLatin1Char('|') + command.arguments.value(QStringLiteral("paragraphs")).toString()
                + QLatin1Char('|') + command.arguments.value(QStringLiteral("find")).toString();
        };
        PlannedEdit* same = nullptr;
        for (PlannedEdit& earlier : merged)
            if (where(earlier.command) == where(edit.command)) same = &earlier;
        if (!same) {
            merged.append(edit);
            continue;
        }
        for (auto it = edit.command.arguments.begin(); it != edit.command.arguments.end(); ++it)
            same->command.arguments.insert(it.key(), it.value());
        same->after += QStringLiteral("、") + edit.after;
    }
    return merged;
}

bool vagueStyleRequest(const QString& request)
{
    static const QRegularExpression vague(QStringLiteral(
        "(优化|美化|调整|整理|改善|规范|设计|弄)[^，。,.!！?？]{0,10}(样式|格式|排版|版式|版面)|排一?下版|排版|好看(一)?(点|些)|美观|更专业|"
        "\\b(make (it|this|the \\w+) (look )?(nicer|better|prettier|more professional)|tidy up|beautify|"
        "improve the (format|formatting|layout|style))"),
        QRegularExpression::CaseInsensitiveOption);
    return vague.match(request).hasMatch();
}

QStringList requestedColumns(const QString& request)
{
    static const QString name = QStringLiteral("[^，。；;,.:：、/\\s]{1,12}");
    static const QRegularExpression columns(
        QStringLiteral("(?:[两二三四五六七八九十0-9]+\\s*列\\s*[，,]?\\s*(?:分别)?(?:是|为)?\\s*[:：]?"
                       "|列名?\\s*(?:分别)?(?:为|是|[:：])\\s*[:：]?|表头\\s*(?:分别)?(?:为|是|[:：])\\s*[:：]?)\\s*(")
        + name + QStringLiteral("(?:\\s*[、/]\\s*") + name + QStringLiteral(")+)"));
    const QRegularExpressionMatch match = columns.match(request);
    if (!match.hasMatch()) return {};
    static const QRegularExpression separator(QStringLiteral("\\s*[、/]\\s*"));
    return match.captured(1).split(separator, Qt::SkipEmptyParts);
}

bool namesAFormat(const QString& request)
{
    static const QRegularExpression formats(QStringLiteral(
        "加粗|粗体|斜体|倾斜|下划线|居中|对齐|字号|号字|磅|颜色|[红蓝绿黄橙紫灰黑]色|标红|底色|背景|突出显示|高亮|"
        "[一二三123]\\s*级标题|标题\\s*[123]|正文样式|\\b(bold|italic|underline|center|align|font size|colou?r|highlight|heading)"),
        QRegularExpression::CaseInsensitiveOption);
    return formats.match(request).hasMatch();
}

bool asksForTextChanges(const QString& request)
{
    static const QRegularExpression wording(QStringLiteral(
        "改成|改为|写成|写为|换成|替换|统一|填上|填入|填写|填成|补上|补全|删除|删掉|翻译|纠正|改正|修正|更正|错别字|"
        "检查|核对|核算|算错|计算|文字|内容|\\b(change .* to|replace|fill in|translate|rewrite|correct|fix|check|spell)"),
        QRegularExpression::CaseInsensitiveOption);
    return wording.match(request).hasMatch();
}

QList<PlannedEdit> defaultTableStyle(const TableGrid& grid, int table)
{
    QList<PlannedEdit> edits;
    if (grid.isEmpty()) return edits;
    int width = 0;
    for (const auto& row : grid) width = qMax(width, int(row.size()));
    const QString fill = QStringLiteral("#D9E2F3");
    // The header: bold, centred, a light fill.
    QJsonObject header{{QStringLiteral("table"), table}, {QStringLiteral("rows"), QStringLiteral("1")},
                       {QStringLiteral("columns"), QStringLiteral("1-%1").arg(width)}};
    bool bold = true, centred = true, filled = true;
    for (const TableCell& cell : grid.first()) {
        bold = bold && cell.bold;
        centred = centred && cell.align == QStringLiteral("center");
        filled = filled && !cell.fill.isEmpty();
    }
    if (!bold) header.insert(QStringLiteral("bold"), true);
    if (!centred) header.insert(QStringLiteral("align"), QStringLiteral("center"));
    if (!filled) header.insert(QStringLiteral("fill"), fill);
    if (header.size() > 3)
        edits.append({QStringLiteral("第 %1 张表格的表头").arg(table), QString(), formatWords(header),
                      QStringLiteral("表头醒目一些，便于阅读"), {QStringLiteral("format.cells"), header, CommandSource::Assistant}});
    // Columns of numbers: right-aligned, so the digits line up.
    if (grid.size() >= 3)
        for (int c = 0; c < width; ++c) {
            int numbers = 0, filledCells = 0;
            bool right = true;
            for (int r = 1; r < grid.size(); ++r) {
                if (c >= grid[r].size() || grid[r][c].text.trimmed().isEmpty()) continue;
                ++filledCells;
                double ignored = 0;
                if (cellNumber(grid[r][c].text, &ignored)) ++numbers;
                right = right && grid[r][c].align == QStringLiteral("right");
            }
            if (filledCells < 2 || numbers * 10 < filledCells * 8) continue;
            // Amounts line up on the right; a column of serial numbers (序号) sits in the middle.
            const QString name = grid.first().value(c).text.simplified();
            static const QRegularExpression serial(QStringLiteral("^(序号|编号|行号|No\\.?|#)$"), QRegularExpression::CaseInsensitiveOption);
            const bool centre = serial.match(name).hasMatch();
            bool already = true;
            for (int r = 1; r < grid.size(); ++r)
                if (c < grid[r].size() && !grid[r][c].text.trimmed().isEmpty())
                    already = already && grid[r][c].align == (centre ? QStringLiteral("center") : QStringLiteral("right"));
            if (already) continue;
            Q_UNUSED(right);
            const QJsonObject column{{QStringLiteral("table"), table}, {QStringLiteral("rows"), QStringLiteral("2-%1").arg(grid.size())},
                                     {QStringLiteral("columns"), QString::number(c + 1)},
                                     {QStringLiteral("align"), centre ? QStringLiteral("center") : QStringLiteral("right")}};
            edits.append({QStringLiteral("第 %1 张表格第 %2 列%3").arg(table).arg(c + 1).arg(name.isEmpty() ? QString() : QStringLiteral("（%1）").arg(name)),
                          QString(), centre ? QStringLiteral("居中") : QStringLiteral("右对齐"),
                          centre ? QStringLiteral("序号居中") : QStringLiteral("数字右对齐，个位对齐好比较"),
                          {QStringLiteral("format.cells"), column, CommandSource::Assistant}});
        }
    return edits;
}

QList<PlannedEdit> defaultDocumentStyle(const QList<OutlineParagraph>& paragraphs)
{
    QList<PlannedEdit> edits;
    QList<const OutlineParagraph*> written;
    for (const OutlineParagraph& paragraph : paragraphs)
        if (!paragraph.text.trimmed().isEmpty()) written.append(&paragraph);
    if (written.isEmpty()) return edits;
    const auto shortText = [](const OutlineParagraph* paragraph, int limit) { return paragraph->text.trimmed().size() <= limit; };
    static const QRegularExpression marker(QStringLiteral(
        "^(第[一二三四五六七八九十\\d]+[章节部分条]|[一二三四五六七八九十]+、|（[一二三四五六七八九十\\d]+）|\\(\\d+\\)|\\d+[.、．])"));
    static const QRegularExpression sentenceEnd(QStringLiteral("[。！？；：.!?;:，,]$"));
    static const QRegularExpression closing(QStringLiteral(
        "^\\d{4}\\s*年\\s*\\d{1,2}\\s*月\\s*\\d{1,2}\\s*日$|^\\d{4}[-/.]\\d{1,2}[-/.]\\d{1,2}$|"
        "(部|处|室|科|中心|公司|办公室|委员会|小组|团队|集团|学校|学院|医院|局|厅|协会)$"));
    const OutlineParagraph* title = written.first();
    const bool hasTitle = written.size() >= 2 && shortText(title, 40) && !sentenceEnd.match(title->text.trimmed()).hasMatch()
        && !marker.match(title->text.trimmed()).hasMatch();
    if (hasTitle) {
        QJsonObject arguments{{QStringLiteral("paragraphs"), QString::number(title->number)}};
        if (title->align != QStringLiteral("center")) arguments.insert(QStringLiteral("align"), QStringLiteral("center"));
        if (!title->bold) arguments.insert(QStringLiteral("bold"), true);
        if (title->size < 16) arguments.insert(QStringLiteral("fontSize"), 18);
        if (arguments.size() > 1)
            edits.append({QStringLiteral("标题（%1）").arg(title->text.simplified().left(12)), QString(), formatWords(arguments),
                          QStringLiteral("标题居中、醒目"), {QStringLiteral("format.paragraphs"), arguments, CommandSource::Assistant}});
    }
    QStringList headings;
    QList<int> shown;
    for (int i = hasTitle ? 1 : 0; i < written.size(); ++i) {
        const OutlineParagraph* paragraph = written[i];
        if (shortText(paragraph, 30) && marker.match(paragraph->text.trimmed()).hasMatch() && !paragraph->bold
            && !sentenceEnd.match(paragraph->text.trimmed()).hasMatch()) {
            headings << QString::number(paragraph->number);
            shown << i + 1;
        }
    }
    if (!headings.isEmpty()) {
        QStringList people;
        for (int number : shown) people << QString::number(number);
        const QJsonObject arguments{{QStringLiteral("paragraphs"), headings.join(QLatin1Char(','))}, {QStringLiteral("bold"), true}};
        edits.append({QStringLiteral("小标题（第 %1 段）").arg(people.join(QStringLiteral("、"))), QString(), QStringLiteral("加粗"),
                      QStringLiteral("分出层次"), {QStringLiteral("format.paragraphs"), arguments, CommandSource::Assistant}});
    }
    QStringList closings;
    for (int i = qMax(hasTitle ? 1 : 0, int(written.size()) - 2); i < written.size(); ++i) {
        const OutlineParagraph* paragraph = written[i];
        if (shortText(paragraph, 20) && closing.match(paragraph->text.trimmed()).hasMatch() && paragraph->align != QStringLiteral("right"))
            closings << QString::number(paragraph->number);
    }
    if (!closings.isEmpty()) {
        const QJsonObject arguments{{QStringLiteral("paragraphs"), closings.join(QLatin1Char(','))}, {QStringLiteral("align"), QStringLiteral("right")}};
        edits.append({QStringLiteral("落款"), QString(), QStringLiteral("右对齐"), QStringLiteral("落款（单位、日期）靠右"),
                      {QStringLiteral("format.paragraphs"), arguments, CommandSource::Assistant}});
    }
    return edits;
}

QList<PlannedEdit> withRequestedFormats(const QList<PlannedEdit>& plan, const TableGrid& grid, int table, const QString& request)
{
    bool allUnderstood = true;
    const QList<PlannedEdit> program = requestedFormats(request, grid, table, &allUnderstood);
    if (program.isEmpty()) return plan;
    QSet<QString> covered;
    for (const PlannedEdit& edit : program)
        for (auto it = edit.command.arguments.begin(); it != edit.command.arguments.end(); ++it) covered.insert(it.key());
    QList<PlannedEdit> result = program;
    for (const PlannedEdit& edit : plan) {
        if (edit.command.name == QStringLiteral("format.cells")) {
            if (allUnderstood) continue; // the program read every format the request asks for
            bool onlyCovered = true;
            for (const QString& key : {QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("align"), QStringLiteral("fill"),
                                       QStringLiteral("color"), QStringLiteral("fontSize")})
                onlyCovered = onlyCovered && (!edit.command.arguments.contains(key) || covered.contains(key));
            if (onlyCovered) continue;
        }
        result.append(edit);
    }
    return result;
}

QString insertedTableProblems(const QList<QList<QStringList>>& meant, const QJsonArray& outlineBlocks)
{
    QStringList problems;
    for (int t = 0; t < meant.size(); ++t) {
        const QList<QStringList>& table = meant[t];
        if (table.isEmpty()) continue;
        const int rows = table.size();
        const int columns = table.first().size();
        QStringList header;
        for (const QString& cell : table.first()) header << plainCellText(cell).left(40);
        bool found = false;
        int width = -1;
        for (const QJsonValue& value : outlineBlocks) {
            const QJsonObject block = value.toObject();
            if (!block.value(QStringLiteral("id")).toString().startsWith(QLatin1Char('T'))) continue;
            QStringList first;
            for (const QJsonValue& cell : block.value(QStringLiteral("firstRow")).toArray()) first << cell.toString().simplified();
            if (block.value(QStringLiteral("rows")).toInt() == rows && block.value(QStringLiteral("columns")).toInt() == columns
                && first == header) {
                found = true;
                width = block.contains(QStringLiteral("width")) ? block.value(QStringLiteral("width")).toInt() : -1;
            }
        }
        const QString name = meant.size() > 1 ? QStringLiteral("第 %1 个表格").arg(t + 1) : QStringLiteral("表格");
        if (!found)
            problems << QStringLiteral("%1应为 %2 行 %3 列（表头：%4），文档里没有找到这样的表格")
                            .arg(name).arg(rows).arg(columns).arg(header.join(QStringLiteral("、")));
        else if (width >= 0 && width < 50)
            problems << QStringLiteral("%1太窄，只占页面宽度的 %2%").arg(name).arg(width);
    }
    return problems.join(QStringLiteral("；"));
}

bool cellDate(const QString& text, CellDate* date)
{
    static const QList<QRegularExpression> patterns{
        QRegularExpression(QStringLiteral("^(\\d{4})\\s*[-/.]\\s*(\\d{1,2})\\s*[-/.]\\s*(\\d{1,2})$")),
        QRegularExpression(QStringLiteral("^(\\d{4})\\s*年\\s*(\\d{1,2})\\s*月\\s*(\\d{1,2})\\s*[日号]$")),
        QRegularExpression(QStringLiteral("^()(\\d{1,2})\\s*月\\s*(\\d{1,2})\\s*[日号]$")),
        QRegularExpression(QStringLiteral("^()(\\d{1,2})/(\\d{1,2})$"))};
    for (const QRegularExpression& pattern : patterns) {
        const QRegularExpressionMatch match = pattern.match(text.trimmed());
        if (!match.hasMatch()) continue;
        const CellDate found{match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt()};
        if (found.month < 1 || found.month > 12 || found.day < 1 || found.day > 31) return false;
        if (found.year && (found.year < 1900 || found.year > 2100)) return false;
        if (date) *date = found;
        return true;
    }
    return false;
}

QList<PlannedEdit> withDateStyle(const QList<PlannedEdit>& plan, const TableGrid& grid, int table, const QString& request)
{
    // The style the request shows: the earliest date written in it.
    static const QRegularExpression unify(QStringLiteral("统一|改成|改为|写成|换成|格式|一律|都用|\\bformat\\b|\\bconsistent"),
                                          QRegularExpression::CaseInsensitiveOption);
    if (!unify.match(request).hasMatch()) return plan;
    enum Style { Dashed, Slashed, Dotted, ChineseFull, ChineseShort, MonthSlashDay };
    struct Example {
        QRegularExpression pattern;
        Style style;
    };
    static const QList<Example> examples{
        {QRegularExpression(QStringLiteral("(\\d{4})年(\\d{1,2})月(\\d{1,2})[日号]")), ChineseFull},
        {QRegularExpression(QStringLiteral("(\\d{4})-(\\d{1,2})-(\\d{1,2})")), Dashed},
        {QRegularExpression(QStringLiteral("(\\d{4})/(\\d{1,2})/(\\d{1,2})")), Slashed},
        {QRegularExpression(QStringLiteral("(\\d{4})\\.(\\d{1,2})\\.(\\d{1,2})")), Dotted},
        {QRegularExpression(QStringLiteral("()(\\d{1,2})月(\\d{1,2})[日号]")), ChineseShort},
        {QRegularExpression(QStringLiteral("()(?<![\\d/])(\\d{1,2})/(\\d{1,2})(?![\\d/])")), MonthSlashDay}};
    int at = -1;
    Style style = Dashed;
    bool padded = false;
    QString example;
    for (const Example& candidate : examples) {
        const QRegularExpressionMatch match = candidate.pattern.match(request);
        if (!match.hasMatch() || (at >= 0 && match.capturedStart(0) >= at)) continue;
        at = int(match.capturedStart(0));
        style = candidate.style;
        example = match.captured(0);
        padded = (match.captured(2).size() == 2 && match.captured(2).startsWith(QLatin1Char('0')))
            || (match.captured(3).size() == 2 && match.captured(3).startsWith(QLatin1Char('0')))
            || ((style == Dashed || style == Slashed || style == Dotted) && match.captured(2).size() == 2 && match.captured(3).size() == 2);
    }
    if (at < 0) return plan;
    const auto two = [padded](int value) { return padded ? QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0')) : QString::number(value); };
    const auto written = [&](const CellDate& date) -> QString {
        switch (style) {
        case ChineseShort: return QStringLiteral("%1月%2日").arg(two(date.month), two(date.day));
        case MonthSlashDay: return QStringLiteral("%1/%2").arg(two(date.month), two(date.day));
        default: break;
        }
        if (!date.year) return {};
        if (style == ChineseFull) return QStringLiteral("%1年%2月%3日").arg(date.year).arg(two(date.month), two(date.day));
        const QChar separator = style == Dashed ? QLatin1Char('-') : style == Slashed ? QLatin1Char('/') : QLatin1Char('.');
        return QString::number(date.year) + separator + two(date.month) + separator + two(date.day);
    };
    // Date columns: two or more dates below the header. A date without a year takes the
    // year most of the column has.
    QHash<QPair<int, int>, QString> right;
    for (int c = 1; c <= 100; ++c) {
        QList<QPair<int, CellDate>> dates;
        QHash<int, int> years;
        for (int r = 2; r <= grid.size(); ++r) {
            CellDate date;
            if (c <= grid[r - 1].size() && cellDate(grid[r - 1][c - 1].text, &date)) {
                dates.append({r, date});
                if (date.year) ++years[date.year];
            }
        }
        if (dates.size() < 2) continue;
        int year = 0;
        for (auto it = years.begin(); it != years.end(); ++it)
            if (!year || it.value() > years.value(year)) year = it.key();
        for (auto [r, date] : dates) {
            if (!date.year) date.year = year;
            const QString text = written(date);
            if (!text.isEmpty()) right.insert({r, c}, text);
        }
    }
    if (right.isEmpty()) return plan;
    QList<PlannedEdit> result;
    for (int r = 2; r <= grid.size(); ++r)
        for (int c = 1; c <= grid[r - 1].size(); ++c) {
            const QString text = right.value({r, c});
            const QString before = grid[r - 1][c - 1].text.trimmed();
            if (text.isEmpty() || text == before) continue;
            result.append({tablePlace(r, c, r, c), before, text, QStringLiteral("统一写成“%1”的格式").arg(example),
                           {QStringLiteral("table.setCell"), {{QStringLiteral("table"), table}, {QStringLiteral("row"), r},
                                                              {QStringLiteral("column"), c}, {QStringLiteral("text"), text}},
                            CommandSource::Assistant}});
        }
    // The program writes the dates; the model's own date edits (it copies the example) go.
    for (const PlannedEdit& edit : plan) {
        const QPair<int, int> cell{edit.command.arguments.value(QStringLiteral("row")).toInt(),
                                   edit.command.arguments.value(QStringLiteral("column")).toInt()};
        if (edit.command.name == QStringLiteral("table.setCell") && right.contains(cell)) continue;
        result.append(edit);
    }
    return result.size() > 20 ? result.mid(0, 20) : result;
}

QString tableTexts(const TableGrid& grid)
{
    // Each cell with its column's name ("品名：笔计本"): a word alone is easy to misread.
    QStringList lines;
    QSet<QString> seen;
    int size = 0;
    for (int r = 0; r < grid.size(); ++r)
        for (int c = 0; c < grid[r].size(); ++c) {
            const QString text = grid[r][c].text.simplified();
            double ignored = 0;
            if (text.isEmpty() || seen.contains(text) || cellNumber(text, &ignored) || cellDate(text, nullptr)) continue;
            seen.insert(text);
            const QString name = r > 0 && c < grid.first().size() ? grid.first()[c].text.simplified() : QString();
            const QString line = name.isEmpty() || name == text ? text : name + QStringLiteral("：") + text;
            size += line.size() + 1;
            if (size > 4000) return lines.join(QLatin1Char('\n'));
            lines << line;
        }
    return lines.join(QLatin1Char('\n'));
}

QList<PlannedEdit> withTypoFixes(const QList<PlannedEdit>& plan, const QList<Correction>& corrections, const TableGrid& grid, int table)
{
    QSet<QPair<int, int>> edited;
    for (const PlannedEdit& edit : plan)
        if (edit.command.name == QStringLiteral("table.setCell"))
            edited.insert({edit.command.arguments.value(QStringLiteral("row")).toInt(),
                           edit.command.arguments.value(QStringLiteral("column")).toInt()});
    QList<PlannedEdit> result = plan;
    for (Correction correction : corrections) {
        // The model may quote the column's name too ("品名：笔计本"): only the cell's part counts.
        for (QString* text : {&correction.original, &correction.corrected}) {
            const int colon = text->indexOf(QStringLiteral("："));
            if (colon >= 0) *text = text->mid(colon + 1).trimmed();
        }
        if (correction.original.isEmpty() || correction.original == correction.corrected) continue;
        for (int r = 1; r <= grid.size(); ++r)
            for (int c = 1; c <= grid[r - 1].size(); ++c) {
                const QString before = grid[r - 1][c - 1].text.trimmed();
                if (edited.contains({r, c}) || !before.contains(correction.original)) continue;
                QString after = before;
                after.replace(correction.original, correction.corrected);
                if (after == before) continue;
                edited.insert({r, c});
                result.append({tablePlace(r, c, r, c), before, after,
                               correction.reason.isEmpty() ? QStringLiteral("错别字") : correction.reason,
                               {QStringLiteral("table.setCell"), {{QStringLiteral("table"), table}, {QStringLiteral("row"), r},
                                                                  {QStringLiteral("column"), c}, {QStringLiteral("text"), after}},
                                CommandSource::Assistant}});
            }
    }
    return result.size() > 20 ? result.mid(0, 20) : result;
}

int tableMentionedIn(const QString& request, const QJsonArray& outlineBlocks)
{
    int table = 0;
    for (const QJsonValue& value : outlineBlocks) {
        const QJsonObject block = value.toObject();
        if (!block.value(QStringLiteral("id")).toString().startsWith(QLatin1Char('T'))) continue;
        ++table;
        for (const QJsonValue& cell : block.value(QStringLiteral("firstRow")).toArray()) {
            const QString name = cell.toString().simplified();
            double ignored = 0;
            if (name.size() >= 2 && name.size() <= 12 && !cellNumber(name, &ignored)
                && request.contains(name, Qt::CaseInsensitive))
                return table;
        }
    }
    return 0;
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
