#include "assistant_commands.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace openoffice {
namespace {

CommandOutcome failure(const char* code, const QString& message)
{
    return {false, QString::fromLatin1(code), message, {}};
}

const QStringList& documentKinds()
{
    static const QStringList kinds{QStringLiteral("text"), QStringLiteral("spreadsheet"),
                                   QStringLiteral("presentation")};
    return kinds;
}

CommandArgument tableArgument()
{
    return {QStringLiteral("table"), ArgumentType::Integer, true, 0,
            QStringLiteral("表格编号，从 1 开始（document.outline 里的 T 后面的数字）。"), {}, 1, 200};
}

CommandArgument rangeArgument(const QString& name, const QString& what)
{
    return {name, ArgumentType::String, false, 40, QStringLiteral("哪些%1：1、2-4、1,3 或 all（默认 all）。").arg(what)};
}

CommandArgument boolArgument(const QString& name, const QString& what)
{
    return {name, ArgumentType::Boolean, false, 0, QStringLiteral("true 设为%1，false 取消%1。").arg(what)};
}

CommandArgument fontSizeArgument()
{
    return {QStringLiteral("fontSize"), ArgumentType::Integer, false, 0, QStringLiteral("字号（磅），6 到 72。"), {}, 6, 72};
}

CommandArgument colorArgument(const QString& name, const QString& what, bool allowNone)
{
    CommandArgument argument{name, ArgumentType::Color, false, 0,
                             QStringLiteral("%1，写成 #RRGGBB，例如 #C00000%2。").arg(what, allowNone ? QStringLiteral("；none 为去掉") : QString())};
    argument.allowNone = allowNone;
    return argument;
}

CommandArgument alignArgument()
{
    return {QStringLiteral("align"), ArgumentType::Choice, false, 0, QStringLiteral("对齐：left、center、right 或 justify。"),
            {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right"), QStringLiteral("justify")}};
}

QList<CommandSpec> buildCatalog()
{
    using A = CommandArgument;
    return {
        {QStringLiteral("assistant.listCommands"), QStringLiteral("全部命令"),
         QStringLiteral("列出助手可以执行的全部命令及其参数。"), {}, CommandTarget::Host, false, false},
        {QStringLiteral("document.status"), QStringLiteral("文档状态"),
         QStringLiteral("返回当前文档的名称、类型和是否有未保存修改；不返回正文。"), {}, CommandTarget::Host, false, false},
        {QStringLiteral("document.create"), QStringLiteral("新建文档"),
         QStringLiteral("新建一份空白文档。当前文档有未保存修改时，由用户在提示里决定保存、不保存或取消。"),
         {A{QStringLiteral("kind"), ArgumentType::DocumentKind, true, 0,
            QStringLiteral("文档类型：text、spreadsheet 或 presentation（当前版本只开放 text）。")}},
         CommandTarget::Host, false, false},
        {QStringLiteral("document.save"), QStringLiteral("保存"),
         QStringLiteral("保存当前文档；新文档第一次保存时由用户选择位置。"), {}, CommandTarget::Host, true, false},
        {QStringLiteral("text.insert"), QStringLiteral("插入文字"),
         QStringLiteral("把文字插入文档；换行会成为新段落。format 为 markdown 时，# 标题、列表、表格、加粗会变成真正的格式。"
                        "position 为 after 时插在光标所在段落（或选中内容）之后的新段落里，光标在空段落时就插在这里。可用撤销恢复。"),
         {A{QStringLiteral("text"), ArgumentType::String, true, 20000, QStringLiteral("要插入的文字，1 到 20000 个字符。")},
          A{QStringLiteral("format"), ArgumentType::Choice, false, 0,
            QStringLiteral("plain（默认，纯文本）或 markdown（带格式）。"), {QStringLiteral("plain"), QStringLiteral("markdown")}},
          A{QStringLiteral("position"), ArgumentType::Choice, false, 0,
            QStringLiteral("cursor（默认，光标处）或 after（当前段落之后）。"), {QStringLiteral("cursor"), QStringLiteral("after")}}},
         CommandTarget::Editor, true, true},
        {QStringLiteral("selection.getText"), QStringLiteral("读取选中文字"),
         QStringLiteral("返回编辑器中当前选中的文字（最多 20000 个字符）和选中的段落数。"), {}, CommandTarget::Editor, true, false},
        {QStringLiteral("selection.replace"), QStringLiteral("替换选中文字"),
         QStringLiteral("用新文字替换当前选中的文字，尽量保留原来的字体和段落格式；换行会成为新段落。"
                        "给出 expected（原来选中的文字）时，选区变了就不替换。可用撤销恢复。"),
         {A{QStringLiteral("text"), ArgumentType::String, true, 20000, QStringLiteral("新文字，1 到 20000 个字符。")},
          A{QStringLiteral("expected"), ArgumentType::String, false, 20000, QStringLiteral("原来选中的文字，用来确认选区没有变。")}},
         CommandTarget::Editor, true, true},
        {QStringLiteral("document.getText"), QStringLiteral("读取全文"),
         QStringLiteral("返回整篇文档的文字（最多 40000 个字符）：标题前加 #，表格每行写成 | 单元格 |。不改变选区。"),
         {}, CommandTarget::Editor, true, false},
        {QStringLiteral("document.replaceText"), QStringLiteral("查找替换"),
         QStringLiteral("把文档中所有与 find 完全相同（区分大小写）的文字替换为 replace，返回替换的处数。可用撤销恢复。"),
         {A{QStringLiteral("find"), ArgumentType::String, true, 200, QStringLiteral("要查找的文字，1 到 200 个字符。")},
          A{QStringLiteral("replace"), ArgumentType::String, true, 200, QStringLiteral("替换成的文字，1 到 200 个字符。")}},
         CommandTarget::Editor, true, true},
        {QStringLiteral("document.outline"), QStringLiteral("读取文档结构"),
         QStringLiteral("按顺序列出文档的段落（P1、P2…，带样式名和开头文字）和表格（T1、T2…，行数、列数和第一行），"
                        "并说明光标在哪一段或哪个单元格。修改格式或表格前，先用它找到位置。"),
         {}, CommandTarget::Editor, true, false},
        {QStringLiteral("table.read"), QStringLiteral("读取表格"),
         QStringLiteral("读取第 table 张表格的每个单元格：文字（按 R行C列 编号），以及是否加粗、对齐方式和底色。"),
         {tableArgument()}, CommandTarget::Editor, true, false},
        {QStringLiteral("table.setCell"), QStringLiteral("修改单元格"),
         QStringLiteral("把第 table 张表格第 row 行第 column 列单元格的文字换成 text，保留原来的字体格式。只改这一格，可用撤销恢复。"),
         {tableArgument(), A{QStringLiteral("row"), ArgumentType::Integer, true, 0, QStringLiteral("行号，从 1 开始。"), {}, 1, 500},
          A{QStringLiteral("column"), ArgumentType::Integer, true, 0, QStringLiteral("列号，从 1 开始。"), {}, 1, 100},
          A{QStringLiteral("text"), ArgumentType::String, true, 2000, QStringLiteral("单元格的新文字，1 到 2000 个字符。")}},
         CommandTarget::Editor, true, true},
        {QStringLiteral("format.cells"), QStringLiteral("设置单元格格式"),
         QStringLiteral("给第 table 张表格里 rows 行与 columns 列交叉处的单元格设置格式（加粗、斜体、字号、文字颜色、底色、对齐）。"
                        "rows、columns 写成 1、2-4、1,3 或 all（默认 all）。只给出要改的那几项格式。可用撤销恢复。"),
         {tableArgument(), rangeArgument(QStringLiteral("rows"), QStringLiteral("行")),
          rangeArgument(QStringLiteral("columns"), QStringLiteral("列")), boolArgument(QStringLiteral("bold"), QStringLiteral("加粗")),
          boolArgument(QStringLiteral("italic"), QStringLiteral("斜体")), fontSizeArgument(),
          colorArgument(QStringLiteral("color"), QStringLiteral("文字颜色"), false),
          colorArgument(QStringLiteral("fill"), QStringLiteral("单元格底色"), true), alignArgument()},
         CommandTarget::Editor, true, true},
        {QStringLiteral("format.paragraphs"), QStringLiteral("设置段落格式"),
         QStringLiteral("给 paragraphs 指定的段落（按 document.outline 的 P 编号，写成 3、2-5 或 1,4）设置格式：加粗、斜体、字号、"
                        "文字颜色、对齐，或改成标题/正文样式。只给出要改的那几项。可用撤销恢复。"),
         {A{QStringLiteral("paragraphs"), ArgumentType::String, true, 40, QStringLiteral("段落编号：3、2-5 或 1,4。")},
          boolArgument(QStringLiteral("bold"), QStringLiteral("加粗")), boolArgument(QStringLiteral("italic"), QStringLiteral("斜体")),
          fontSizeArgument(), colorArgument(QStringLiteral("color"), QStringLiteral("文字颜色"), false), alignArgument(),
          A{QStringLiteral("style"), ArgumentType::Choice, false, 0, QStringLiteral("段落样式：heading1、heading2、heading3 或 normal（正文）。"),
            {QStringLiteral("heading1"), QStringLiteral("heading2"), QStringLiteral("heading3"), QStringLiteral("normal")}}},
         CommandTarget::Editor, true, true},
        {QStringLiteral("format.text"), QStringLiteral("设置文字格式"),
         QStringLiteral("找到文档里所有与 find 完全相同的文字（最多 100 处），设置加粗、斜体、下划线、字号、颜色或突出显示。可用撤销恢复。"),
         {A{QStringLiteral("find"), ArgumentType::String, true, 200, QStringLiteral("要设置格式的文字，1 到 200 个字符。")},
          boolArgument(QStringLiteral("bold"), QStringLiteral("加粗")), boolArgument(QStringLiteral("italic"), QStringLiteral("斜体")),
          boolArgument(QStringLiteral("underline"), QStringLiteral("下划线")), fontSizeArgument(),
          colorArgument(QStringLiteral("color"), QStringLiteral("文字颜色"), false),
          A{QStringLiteral("highlight"), ArgumentType::Choice, false, 0, QStringLiteral("突出显示的颜色，none 为去掉。"),
            {QStringLiteral("yellow"), QStringLiteral("green"), QStringLiteral("cyan"), QStringLiteral("magenta"),
             QStringLiteral("red"), QStringLiteral("blue"), QStringLiteral("lightGray"), QStringLiteral("none")}}},
         CommandTarget::Editor, true, true},
        {QStringLiteral("format.selection"), QStringLiteral("设置选中内容的格式"),
         QStringLiteral("给用户选中的文字设置加粗、斜体、下划线、字号、颜色或突出显示；对齐和样式（标题/正文）用于选中内容所在的段落。可用撤销恢复。"),
         {boolArgument(QStringLiteral("bold"), QStringLiteral("加粗")), boolArgument(QStringLiteral("italic"), QStringLiteral("斜体")),
          boolArgument(QStringLiteral("underline"), QStringLiteral("下划线")), fontSizeArgument(),
          colorArgument(QStringLiteral("color"), QStringLiteral("文字颜色"), false), alignArgument(),
          A{QStringLiteral("highlight"), ArgumentType::Choice, false, 0, QStringLiteral("突出显示的颜色，none 为去掉。"),
            {QStringLiteral("yellow"), QStringLiteral("green"), QStringLiteral("cyan"), QStringLiteral("magenta"),
             QStringLiteral("red"), QStringLiteral("blue"), QStringLiteral("lightGray"), QStringLiteral("none")}},
          A{QStringLiteral("style"), ArgumentType::Choice, false, 0, QStringLiteral("段落样式：heading1、heading2、heading3 或 normal（正文）。"),
            {QStringLiteral("heading1"), QStringLiteral("heading2"), QStringLiteral("heading3"), QStringLiteral("normal")}}},
         CommandTarget::Editor, true, true},
        {QStringLiteral("format.read"), QStringLiteral("读取文字格式"),
         QStringLiteral("找到文档里所有与 find 完全相同的文字，读出前 20 处的格式（加粗、斜体、下划线、字号、颜色、突出显示）。"),
         {A{QStringLiteral("find"), ArgumentType::String, true, 200, QStringLiteral("要读取格式的文字，1 到 200 个字符。")}},
         CommandTarget::Editor, true, false},
        {QStringLiteral("selection.select"), QStringLiteral("选中"),
         QStringLiteral("在编辑器里选中一处内容：find 为第一处相同的文字；paragraph 为第几段；table、row、column 为某个单元格。只给出其中一种。"),
         {A{QStringLiteral("find"), ArgumentType::String, false, 200, QStringLiteral("要选中的文字。")},
          A{QStringLiteral("paragraph"), ArgumentType::Integer, false, 0, QStringLiteral("段落编号（P 后面的数字）。"), {}, 1, 100000},
          A{QStringLiteral("table"), ArgumentType::Integer, false, 0, QStringLiteral("表格编号（T 后面的数字）。"), {}, 1, 200},
          A{QStringLiteral("row"), ArgumentType::Integer, false, 0, QStringLiteral("行号。"), {}, 1, 500},
          A{QStringLiteral("column"), ArgumentType::Integer, false, 0, QStringLiteral("列号。"), {}, 1, 100}},
         CommandTarget::Editor, true, false},
        {QStringLiteral("edit.undo"), QStringLiteral("撤销"),
         QStringLiteral("撤销编辑器中的上一步操作。"), {}, CommandTarget::Editor, true, true},
        {QStringLiteral("edit.redo"), QStringLiteral("重做"),
         QStringLiteral("重做上一步被撤销的操作。"), {}, CommandTarget::Editor, true, true},
    };
}

struct SlashAlias {
    QString command;
    bool takesRest; // the remainder of the line is the command's single argument
};

const QHash<QString, SlashAlias>& slashAliases()
{
    static const QHash<QString, SlashAlias> aliases{
        {QStringLiteral("命令"), {QStringLiteral("assistant.listCommands"), false}},
        {QStringLiteral("帮助"), {QStringLiteral("assistant.listCommands"), false}},
        {QStringLiteral("help"), {QStringLiteral("assistant.listCommands"), false}},
        {QStringLiteral("状态"), {QStringLiteral("document.status"), false}},
        {QStringLiteral("status"), {QStringLiteral("document.status"), false}},
        {QStringLiteral("新建"), {QStringLiteral("document.create"), true}},
        {QStringLiteral("new"), {QStringLiteral("document.create"), true}},
        {QStringLiteral("保存"), {QStringLiteral("document.save"), false}},
        {QStringLiteral("save"), {QStringLiteral("document.save"), false}},
        {QStringLiteral("插入"), {QStringLiteral("text.insert"), true}},
        {QStringLiteral("insert"), {QStringLiteral("text.insert"), true}},
        {QStringLiteral("选中"), {QStringLiteral("selection.getText"), false}},
        {QStringLiteral("全文"), {QStringLiteral("document.getText"), false}},
        {QStringLiteral("read"), {QStringLiteral("document.getText"), false}},
        {QStringLiteral("selection"), {QStringLiteral("selection.getText"), false}},
        {QStringLiteral("撤销"), {QStringLiteral("edit.undo"), false}},
        {QStringLiteral("undo"), {QStringLiteral("edit.undo"), false}},
        {QStringLiteral("重做"), {QStringLiteral("edit.redo"), false}},
        {QStringLiteral("redo"), {QStringLiteral("edit.redo"), false}},
    };
    return aliases;
}

QString kindFromWord(const QString& word)
{
    static const QHash<QString, QString> words{
        {QString(), QStringLiteral("text")},
        {QStringLiteral("文档"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("text")},
        {QStringLiteral("表格"), QStringLiteral("spreadsheet")}, {QStringLiteral("spreadsheet"), QStringLiteral("spreadsheet")},
        {QStringLiteral("演示"), QStringLiteral("presentation")}, {QStringLiteral("presentation"), QStringLiteral("presentation")},
    };
    return words.value(word.toLower());
}

} // namespace

QList<SlashHint> slashHints()
{
    static const QList<QPair<QString, QString>> usages{
        {QStringLiteral("assistant.listCommands"), QStringLiteral("/命令")},
        {QStringLiteral("document.status"), QStringLiteral("/状态")},
        {QStringLiteral("document.create"), QStringLiteral("/新建 文档")},
        {QStringLiteral("document.save"), QStringLiteral("/保存")},
        {QStringLiteral("text.insert"), QStringLiteral("/插入 文字")},
        {QStringLiteral("selection.getText"), QStringLiteral("/选中")},
        {QStringLiteral("document.getText"), QStringLiteral("/全文")},
        {QStringLiteral("edit.undo"), QStringLiteral("/撤销")},
        {QStringLiteral("edit.redo"), QStringLiteral("/重做")},
    };
    QList<SlashHint> hints;
    for (const auto& [name, usage] : usages) {
        const CommandSpec* spec = findCommand(name);
        if (!spec) continue;
        hints.append({usage.mid(1).section(QLatin1Char(' '), 0, 0), name, usage, spec->description,
                      usage.contains(QLatin1Char(' '))});
    }
    return hints;
}

const QList<CommandSpec>& commandCatalog()
{
    static const QList<CommandSpec> catalog = buildCatalog();
    return catalog;
}

const CommandSpec* findCommand(const QString& name)
{
    for (const CommandSpec& spec : commandCatalog())
        if (spec.name == name) return &spec;
    return nullptr;
}

QString sourceName(CommandSource source)
{
    switch (source) {
    case CommandSource::User: return QStringLiteral("user");
    case CommandSource::Assistant: return QStringLiteral("assistant");
    case CommandSource::External: return QStringLiteral("external");
    default: return QStringLiteral("unknown");
    }
}

bool integerArgument(const QJsonValue& value, int minimum, int maximum)
{
    double number = 0;
    if (value.isDouble()) {
        number = value.toDouble();
    } else if (value.isString()) { // small models often quote numbers
        bool ok = false;
        number = value.toString().trimmed().toInt(&ok);
        if (!ok) return false;
    } else {
        return false;
    }
    return number == static_cast<double>(static_cast<qint64>(number)) && number >= minimum && (maximum <= 0 || number <= maximum);
}

CommandOutcome validateCommand(const AssistantCommand& command)
{
    const CommandSpec* spec = findCommand(command.name);
    if (!spec) return failure("unknown_command", QStringLiteral("没有名为 %1 的命令。").arg(command.name));
    for (auto it = command.arguments.begin(); it != command.arguments.end(); ++it) {
        const bool known = std::any_of(spec->arguments.begin(), spec->arguments.end(),
                                       [&it](const CommandArgument& argument) { return argument.name == it.key(); });
        if (!known) return failure("unknown_argument", QStringLiteral("命令 %1 没有参数 %2。").arg(spec->name, it.key()));
    }
    for (const CommandArgument& argument : spec->arguments) {
        const QJsonValue value = command.arguments.value(argument.name);
        if (value.isUndefined()) {
            if (argument.required)
                return failure("missing_argument", QStringLiteral("缺少参数 %1。").arg(argument.name));
            continue;
        }
        if (argument.type == ArgumentType::Integer) {
            if (!integerArgument(value, argument.minimum, argument.maximum))
                return failure("invalid_argument", QStringLiteral("参数 %1 必须是 %2 到 %3 之间的整数。")
                                                      .arg(argument.name).arg(argument.minimum).arg(argument.maximum));
            continue;
        }
        if (argument.type == ArgumentType::Boolean) {
            if (!value.isBool() && value.toString() != QStringLiteral("true") && value.toString() != QStringLiteral("false"))
                return failure("invalid_argument", QStringLiteral("参数 %1 只能是 true 或 false。").arg(argument.name));
            continue;
        }
        if (!value.isString())
            return failure("invalid_argument", QStringLiteral("参数 %1 必须是文字。").arg(argument.name));
        const QString text = value.toString();
        if (argument.type == ArgumentType::Color) {
            static const QRegularExpression hex(QStringLiteral("^#?[0-9A-Fa-f]{6}$"));
            if (!hex.match(text).hasMatch() && !(argument.allowNone && text == QStringLiteral("none")))
                return failure("invalid_argument", QStringLiteral("参数 %1 要写成 #RRGGBB 形式的颜色%2。")
                                                      .arg(argument.name, argument.allowNone ? QStringLiteral("或 none") : QString()));
            continue;
        }
        if (argument.required && text.isEmpty())
            return failure("invalid_argument", QStringLiteral("参数 %1 不能为空。").arg(argument.name));
        if (argument.maxLength > 0 && text.size() > argument.maxLength)
            return failure("argument_too_long", QStringLiteral("参数 %1 超过 %2 个字符。").arg(argument.name).arg(argument.maxLength));
        if (argument.type == ArgumentType::DocumentKind && !documentKinds().contains(text))
            return failure("invalid_argument", QStringLiteral("文档类型只能是 text、spreadsheet 或 presentation。"));
        if (argument.type == ArgumentType::Choice && !argument.choices.contains(text))
            return failure("invalid_argument", QStringLiteral("参数 %1 只能是 %2。").arg(argument.name, argument.choices.join(QStringLiteral("、"))));
    }
    return {true, QStringLiteral("valid"), QString(), {}};
}

QJsonArray commandCatalogJson()
{
    QJsonArray tools;
    for (const CommandSpec& spec : commandCatalog()) {
        QJsonObject properties;
        QJsonArray required;
        for (const CommandArgument& argument : spec.arguments) {
            QJsonObject property{{QStringLiteral("type"), QStringLiteral("string")},
                                 {QStringLiteral("description"), argument.description}};
            if (argument.type == ArgumentType::Integer) {
                property.insert(QStringLiteral("type"), QStringLiteral("integer"));
                property.insert(QStringLiteral("minimum"), argument.minimum);
                if (argument.maximum > 0) property.insert(QStringLiteral("maximum"), argument.maximum);
            }
            if (argument.type == ArgumentType::Boolean) property.insert(QStringLiteral("type"), QStringLiteral("boolean"));
            if (argument.type == ArgumentType::Color)
                property.insert(QStringLiteral("pattern"), argument.allowNone ? QStringLiteral("^(#?[0-9A-Fa-f]{6}|none)$")
                                                                              : QStringLiteral("^#?[0-9A-Fa-f]{6}$"));
            if (argument.type == ArgumentType::DocumentKind)
                property.insert(QStringLiteral("enum"), QJsonArray::fromStringList(documentKinds()));
            if (argument.type == ArgumentType::Choice)
                property.insert(QStringLiteral("enum"), QJsonArray::fromStringList(argument.choices));
            if (argument.maxLength > 0) property.insert(QStringLiteral("maxLength"), argument.maxLength);
            if (argument.required) {
                if (argument.type == ArgumentType::String) property.insert(QStringLiteral("minLength"), 1);
                required.append(argument.name);
            }
            properties.insert(argument.name, property);
        }
        tools.append(QJsonObject{
            {QStringLiteral("name"), spec.name},
            {QStringLiteral("title"), spec.title},
            {QStringLiteral("description"), spec.description},
            {QStringLiteral("inputSchema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                        {QStringLiteral("properties"), properties},
                                                        {QStringLiteral("required"), required},
                                                        {QStringLiteral("additionalProperties"), false}}},
            {QStringLiteral("annotations"), QJsonObject{{QStringLiteral("readOnlyHint"), !spec.changesDocument}}},
        });
    }
    return tools;
}

std::optional<AssistantCommand> parseSlashCommand(const QString& text, QString* error)
{
    if (error) error->clear();
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith(QLatin1Char('/')) && !trimmed.startsWith(QChar(0xFF0F))) // also full-width ／
        return std::nullopt;
    const QString body = trimmed.mid(1);
    const int split = body.indexOf(QRegularExpression(QStringLiteral("\\s")));
    const QString word = (split < 0 ? body : body.left(split)).toLower();
    // The argument keeps its inner spacing and line breaks; only the separator goes.
    const QString rest = split < 0 ? QString() : body.mid(split + 1).trimmed();
    const auto alias = slashAliases().constFind(word);
    if (alias == slashAliases().constEnd()) {
        if (error) *error = QStringLiteral("不认识指令 /%1。输入 /命令 查看全部指令。").arg(word);
        return std::nullopt;
    }
    AssistantCommand command{alias->command, {}, CommandSource::User};
    if (alias->command == QStringLiteral("document.create")) {
        const QString kind = kindFromWord(rest);
        if (kind.isEmpty()) {
            if (error) *error = QStringLiteral("只能新建 文档、表格 或 演示。");
            return std::nullopt;
        }
        command.arguments.insert(QStringLiteral("kind"), kind);
    } else if (alias->takesRest) {
        if (rest.isEmpty()) {
            if (error) *error = QStringLiteral("请在 /%1 后面写上要插入的文字。").arg(word);
            return std::nullopt;
        }
        command.arguments.insert(QStringLiteral("text"), rest);
    } else if (!rest.isEmpty()) {
        if (error) *error = QStringLiteral("/%1 不需要参数。").arg(word);
        return std::nullopt;
    }
    return command;
}

} // namespace openoffice
