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

CommandArgument addressArgument(const QString& name, bool required, const QString& description)
{
    return {name, ArgumentType::Address, required, 20, description};
}

CommandArgument sheetArgument()
{
    return {QStringLiteral("sheet"), ArgumentType::String, false, 31, QStringLiteral("哪个工作表（名称）；不给就是当前的工作表。")};
}

CommandArgument headerArgument()
{
    return {QStringLiteral("header"), ArgumentType::Boolean, false, 0,
            QStringLiteral("第一行是不是表头（表头不动）；不给由程序判断。")};
}

// Worksheet names: 1 to 31 characters, none of [ ] : * ? / \, not starting or ending with '.
bool sheetNameValid(const QString& text)
{
    static const QRegularExpression forbidden(QStringLiteral("[\\[\\]:*?/\\\\]"));
    const QString name = text.trimmed();
    return !name.isEmpty() && name.size() <= 31 && !forbidden.match(name).hasMatch()
        && !name.startsWith(QLatin1Char('\'')) && !name.endsWith(QLatin1Char('\''));
}

const QRegularExpression& columnPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^\\$?[A-Za-z]{1,3}(\\$?[0-9]{1,7})?$"));
    return pattern;
}

// A column named by a person or a model: its letter (" C 列", spaced from the Chinese before it)
// or its header ("“销售额”列").
QString columnWords(const QString& given)
{
    QString text = given.trimmed();
    if (!columnPattern().match(text).hasMatch()) return QStringLiteral("“%1”列").arg(text);
    text = text.toUpper().remove(QLatin1Char('$'));
    return QLatin1Char(' ') + text.remove(QRegularExpression(QStringLiteral("[0-9]+$"))) + QStringLiteral(" 列");
}

const QRegularExpression& areasPattern()
{
    static const QRegularExpression pattern(QStringLiteral(
        "^\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7}(:\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7})?"
        "(\\s*,\\s*\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7}(:\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7})?){0,3}$"));
    return pattern;
}

QList<CommandSpec> buildCatalog()
{
    using A = CommandArgument;
    QList<CommandSpec> catalog{
        {QStringLiteral("assistant.listCommands"), QStringLiteral("全部命令"),
         QStringLiteral("列出助手可以执行的全部命令及其参数。"), {}, CommandTarget::Host, false, false},
        {QStringLiteral("document.status"), QStringLiteral("文档状态"),
         QStringLiteral("返回当前文档的名称、类型和是否有未保存修改；不返回正文。"), {}, CommandTarget::Host, false, false},
        {QStringLiteral("document.create"), QStringLiteral("新建文档"),
         QStringLiteral("新建一份空白文档。当前文档有未保存修改时，由用户在提示里决定保存、不保存或取消。"),
         {A{QStringLiteral("kind"), ArgumentType::DocumentKind, true, 0,
            QStringLiteral("文档类型：text（文字）、spreadsheet（表格）或 presentation（演示文稿）。")}},
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
            QStringLiteral("cursor（默认，光标处）、after（当前段落之后）或 end（文档最后）。"),
            {QStringLiteral("cursor"), QStringLiteral("after"), QStringLiteral("end")}}},
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
         QStringLiteral("撤销编辑器中的上一步操作。"), {}, CommandTarget::Editor, true, true, QStringLiteral("any")},
        {QStringLiteral("edit.redo"), QStringLiteral("重做"),
         QStringLiteral("重做上一步被撤销的操作。"), {}, CommandTarget::Editor, true, true, QStringLiteral("any")},
        // Spreadsheets.
        {QStringLiteral("sheet.read"), QStringLiteral("读取工作表"),
         QStringLiteral("读取工作表（sheet 不给就是当前的）：已用范围里每一格的值（公式格同时给出公式），加粗和底色，选中的单元格，"
                        "还有所有工作表的名称、这张表上的图表和筛选。最多 60 行 × 26 列；range 可以只读一部分，例如 A1:F20。改表格前先用它看清楚。"),
         {addressArgument(QStringLiteral("range"), false, QStringLiteral("只读这一部分，例如 A1:F20；不给就读已用范围。")), sheetArgument()},
         CommandTarget::Editor, true, false, QStringLiteral("spreadsheet")},
        {QStringLiteral("cells.set"), QStringLiteral("写入单元格"),
         QStringLiteral("从 start 这一格开始，按行写入 rows：rows 的每一项是一行，每行是这一行从左到右各格的内容。"
                        "以 = 开头的是公式（例如 \"=B2*C2\"、\"=SUM(D2:D9)\"），会自动计算；空字符串表示清空这一格。可用撤销恢复。"),
         {addressArgument(QStringLiteral("start"), true, QStringLiteral("从哪一格开始写，例如 D2。")),
          A{QStringLiteral("rows"), ArgumentType::Grid, true, 1000,
            QStringLiteral("要写的内容，例如 [[\"乘积\"],[\"=A2*B2\"],[\"=A3*B3\"]]；最多 200 行，每格最多 1000 个字符。"), {}, 0, 200},
          sheetArgument()},
         CommandTarget::Editor, true, true, QStringLiteral("spreadsheet")},
        {QStringLiteral("cells.format"), QStringLiteral("设置单元格格式"),
         QStringLiteral("给 range 里的单元格设置格式：加粗、斜体、字号、文字颜色、底色、水平对齐、数字格式、边框。只给出要改的那几项。可用撤销恢复。"),
         {addressArgument(QStringLiteral("range"), true, QStringLiteral("要设置的单元格，例如 A1:D1 或 B2。")),
          boolArgument(QStringLiteral("bold"), QStringLiteral("加粗")), boolArgument(QStringLiteral("italic"), QStringLiteral("斜体")),
          fontSizeArgument(), colorArgument(QStringLiteral("color"), QStringLiteral("文字颜色"), false),
          colorArgument(QStringLiteral("fill"), QStringLiteral("单元格底色"), true),
          A{QStringLiteral("align"), ArgumentType::Choice, false, 0, QStringLiteral("水平对齐：left、center 或 right。"),
            {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}},
          A{QStringLiteral("numberFormat"), ArgumentType::String, false, 40,
            QStringLiteral("数字格式，例如 0.00（两位小数）、#,##0（千分位）、0%（百分比）、yyyy-mm-dd（日期）。")},
          boolArgument(QStringLiteral("borders"), QStringLiteral("细边框（每一格四周）")), sheetArgument()},
         CommandTarget::Editor, true, true, QStringLiteral("spreadsheet")},
        {QStringLiteral("cells.sort"), QStringLiteral("排序"),
         QStringLiteral("把 range（不给就是整张表的已用范围）的行按 by 这一列排序：order 为 asc 从小到大（默认），desc 从大到小。"
                        "表头那一行不动。可用撤销恢复。"),
         {A{QStringLiteral("by"), ArgumentType::Column, true, 40, QStringLiteral("按哪一列排序：列字母（例如 C）或表头上的名字（例如 销售额）。")},
          A{QStringLiteral("order"), ArgumentType::Choice, false, 0, QStringLiteral("asc（从小到大，默认）或 desc（从大到小）。"),
            {QStringLiteral("asc"), QStringLiteral("desc")}},
          addressArgument(QStringLiteral("range"), false, QStringLiteral("要排序的范围，例如 A1:D9；不给就是已用范围。")),
          headerArgument(), sheetArgument()},
         CommandTarget::Editor, true, true, QStringLiteral("spreadsheet")},
        {QStringLiteral("cells.filter"), QStringLiteral("筛选"),
         QStringLiteral("只显示 column 这一列符合要求的行，其余的行隐藏（不删除）。values 是要显示的值；condition 是比较条件，"
                        "例如 >1000、<=60、<>完成；top / bottom 是最大 / 最小的几行。四种只给一种。已有筛选时会叠加在上面。可用撤销恢复。"),
         {A{QStringLiteral("column"), ArgumentType::Column, true, 40, QStringLiteral("按哪一列筛选：列字母（例如 A）或表头上的名字（例如 区域）。")},
          A{QStringLiteral("values"), ArgumentType::Lines, false, 100, QStringLiteral("要显示的值，例如 [\"华东\",\"华南\"]；最多 20 个。"), {}, 0, 20},
          A{QStringLiteral("condition"), ArgumentType::String, false, 60,
            QStringLiteral("比较条件：>、>=、<、<=、=、<> 后面跟数或文字，例如 >1000。")},
          A{QStringLiteral("top"), ArgumentType::Integer, false, 0, QStringLiteral("只显示这一列最大的几行。"), {}, 1, 500},
          A{QStringLiteral("bottom"), ArgumentType::Integer, false, 0, QStringLiteral("只显示这一列最小的几行。"), {}, 1, 500},
          addressArgument(QStringLiteral("range"), false, QStringLiteral("要筛选的范围（含表头），例如 A1:D9；不给就是已用范围。")),
          sheetArgument()},
         CommandTarget::Editor, true, true, QStringLiteral("spreadsheet")},
        {QStringLiteral("cells.clearFilter"), QStringLiteral("取消筛选"),
         QStringLiteral("去掉工作表上的筛选，所有行都显示出来。可用撤销恢复。"),
         {sheetArgument()}, CommandTarget::Editor, true, true, QStringLiteral("spreadsheet")},
        {QStringLiteral("cells.removeDuplicates"), QStringLiteral("删除重复的行"),
         QStringLiteral("删除 range（不给就是已用范围）里重复的行，每组只留第一行；columns 给出时只比较这几列（列字母）。"
                        "表头那一行不动。可用撤销恢复。"),
         {addressArgument(QStringLiteral("range"), false, QStringLiteral("范围，例如 A1:C20；不给就是已用范围。")),
          A{QStringLiteral("columns"), ArgumentType::Lines, false, 40,
            QStringLiteral("只比较这几列（列字母或表头上的名字），例如 [\"A\",\"B\"]；不给就比较整行。"), {}, 0, 10},
          headerArgument(), sheetArgument()},
         CommandTarget::Editor, true, true, QStringLiteral("spreadsheet")},
        {QStringLiteral("chart.add"), QStringLiteral("插入图表"),
         QStringLiteral("用 range 里的数据插一张图表，放在数据右边。range 要包括表头那一行和类别那一列，例如 A1:C9；"
                        "不相邻的列用逗号分开，例如 A1:A9,C1:C9。type：column 柱形图（默认）、bar 条形图、line 折线图、pie 饼图、"
                        "doughnut 圆环图、area 面积图、scatter 散点图。可用撤销恢复。"),
         {A{QStringLiteral("range"), ArgumentType::Areas, true, 100, QStringLiteral("数据所在的范围，例如 A1:C9 或 A1:A9,C1:C9。")},
          A{QStringLiteral("type"), ArgumentType::Choice, false, 0, QStringLiteral("图表类型，默认 column。"),
            {QStringLiteral("column"), QStringLiteral("bar"), QStringLiteral("line"), QStringLiteral("pie"), QStringLiteral("doughnut"),
             QStringLiteral("area"), QStringLiteral("scatter")}},
          A{QStringLiteral("title"), ArgumentType::String, false, 100, QStringLiteral("图表标题，最多 100 个字符。")}, sheetArgument()},
         CommandTarget::Editor, true, true, QStringLiteral("spreadsheet")},
        {QStringLiteral("sheet.add"), QStringLiteral("新建工作表"),
         QStringLiteral("新建一个名为 name 的工作表，并切换过去。可用撤销恢复。"),
         {A{QStringLiteral("name"), ArgumentType::String, true, 31, QStringLiteral("新工作表的名称，1 到 31 个字，不能有 [ ] : * ? / \\。")}},
         CommandTarget::Editor, true, true, QStringLiteral("spreadsheet")},
        {QStringLiteral("sheet.rename"), QStringLiteral("工作表改名"),
         QStringLiteral("把工作表 sheet（不给就是当前的）改名为 name。可用撤销恢复。"),
         {A{QStringLiteral("name"), ArgumentType::String, true, 31, QStringLiteral("新名称，1 到 31 个字，不能有 [ ] : * ? / \\。")},
          sheetArgument()},
         CommandTarget::Editor, true, true, QStringLiteral("spreadsheet")},
        // Presentations.
        {QStringLiteral("slides.read"), QStringLiteral("读取幻灯片"),
         QStringLiteral("按顺序列出每一页幻灯片的标题、副标题和正文各行（最多 50 页），以及现在在第几页。"),
         {}, CommandTarget::Editor, true, false, QStringLiteral("presentation")},
        {QStringLiteral("slide.add"), QStringLiteral("加一页幻灯片"),
         QStringLiteral("加一页幻灯片：有 points 时用“标题和内容”版式，标题写 title、正文每个要点一行；只有 subtitle 时用标题页版式。"
                        "默认加在当前页后面，position 为 end 时加在最后。可用撤销恢复。"),
         {A{QStringLiteral("title"), ArgumentType::String, true, 200, QStringLiteral("这一页的标题，1 到 200 个字符。")},
          A{QStringLiteral("points"), ArgumentType::Lines, false, 300, QStringLiteral("正文的要点，每项一行，最多 12 行。"), {}, 0, 12},
          A{QStringLiteral("subtitle"), ArgumentType::String, false, 300, QStringLiteral("副标题（标题页用），最多 300 个字符。")},
          A{QStringLiteral("position"), ArgumentType::Choice, false, 0, QStringLiteral("after（默认，当前页后面）或 end（最后）。"),
            {QStringLiteral("after"), QStringLiteral("end")}}},
         CommandTarget::Editor, true, true, QStringLiteral("presentation")},
        {QStringLiteral("slide.setText"), QStringLiteral("修改幻灯片文字"),
         QStringLiteral("把第 slide 页的标题（part 为 title）、副标题（subtitle）或正文（body）换成 text（一段）或 points（每项一行）。只给 text 或 points 其中一个。可用撤销恢复。"),
         {A{QStringLiteral("slide"), ArgumentType::Integer, true, 0, QStringLiteral("第几页，从 1 开始。"), {}, 1, 500},
          A{QStringLiteral("part"), ArgumentType::Choice, true, 0, QStringLiteral("title、subtitle 或 body。"),
            {QStringLiteral("title"), QStringLiteral("subtitle"), QStringLiteral("body")}},
          A{QStringLiteral("text"), ArgumentType::String, false, 2000, QStringLiteral("新的文字（一段）。")},
          A{QStringLiteral("points"), ArgumentType::Lines, false, 300, QStringLiteral("新的各行文字，最多 12 行。"), {}, 0, 12}},
         CommandTarget::Editor, true, true, QStringLiteral("presentation")},
    };
    return catalog;
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

bool commandFitsDocument(const CommandSpec& spec, const QString& kind)
{
    if (spec.target == CommandTarget::Host || spec.documents == QStringLiteral("any")) return true;
    return spec.documents == (kind.isEmpty() ? QStringLiteral("text") : kind);
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
        if (argument.type == ArgumentType::Grid || argument.type == ArgumentType::Lines) {
            // Rows of cells, or lines: arrays of texts (numbers are taken as their text).
            const bool grid = argument.type == ArgumentType::Grid;
            const QJsonArray outer = value.toArray();
            if (!value.isArray() || outer.isEmpty() || (argument.maximum > 0 && outer.size() > argument.maximum))
                return failure("invalid_argument", QStringLiteral("参数 %1 要是 1 到 %2 项的数组。").arg(argument.name).arg(argument.maximum));
            for (const QJsonValue& item : outer) {
                const QJsonArray cells = grid ? item.toArray() : QJsonArray{item};
                if ((grid && (!item.isArray() || cells.size() > 50)))
                    return failure("invalid_argument", QStringLiteral("参数 %1 的每一行要是最多 50 格的数组。").arg(argument.name));
                for (const QJsonValue& cell : cells) {
                    if (!cell.isString() && !cell.isDouble() && !cell.isBool())
                        return failure("invalid_argument", QStringLiteral("参数 %1 里的每一项要是文字或数字。").arg(argument.name));
                    if (argument.maxLength > 0 && cell.toVariant().toString().size() > argument.maxLength)
                        return failure("argument_too_long", QStringLiteral("参数 %1 里有一项超过 %2 个字符。").arg(argument.name).arg(argument.maxLength));
                }
            }
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
        static const QRegularExpression address(QStringLiteral("^\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7}(:\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7})?$"));
        if (argument.type == ArgumentType::Address && !address.match(text).hasMatch())
            return failure("invalid_argument", QStringLiteral("参数 %1 要写成单元格地址，例如 D2 或 A1:C10。").arg(argument.name));
        if (argument.type == ArgumentType::Column && (text.trimmed().isEmpty() || text.contains(QLatin1Char('\n'))))
            return failure("invalid_argument", QStringLiteral("参数 %1 要写成列字母（例如 C）或表头上的名字。").arg(argument.name));
        if (argument.type == ArgumentType::Areas && !areasPattern().match(text).hasMatch())
            return failure("invalid_argument", QStringLiteral("参数 %1 要写成单元格范围，例如 A1:C9，不相邻的用逗号分开：A1:A9,C1:C9。").arg(argument.name));
        if ((argument.name == QStringLiteral("sheet") || (argument.name == QStringLiteral("name") && command.name.startsWith(QStringLiteral("sheet."))))
            && !sheetNameValid(text))
            return failure("invalid_argument", QStringLiteral("工作表名称要有 1 到 31 个字，不能有 [ ] : * ? / \\ 这些符号。"));
        if (argument.type == ArgumentType::DocumentKind && !documentKinds().contains(text))
            return failure("invalid_argument", QStringLiteral("文档类型只能是 text、spreadsheet 或 presentation。"));
        if (argument.type == ArgumentType::Choice && !argument.choices.contains(text))
            return failure("invalid_argument", QStringLiteral("参数 %1 只能是 %2。").arg(argument.name, argument.choices.join(QStringLiteral("、"))));
    }
    // A format command must say which format; selection.select which place (one of them).
    static const QStringList formats{QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("underline"),
                                     QStringLiteral("fontSize"), QStringLiteral("color"), QStringLiteral("fill"),
                                     QStringLiteral("align"), QStringLiteral("highlight"), QStringLiteral("style")};
    if (command.name.startsWith(QStringLiteral("format.")) && command.name != QStringLiteral("format.read")
        && std::none_of(formats.begin(), formats.end(), [&command](const QString& key) { return command.arguments.contains(key); }))
        return failure("missing_argument", QStringLiteral("没有说要设置哪种格式：至少给出 bold、italic、fontSize、color、align 等其中一项。"));
    static const QStringList cellFormats{QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("fontSize"),
                                         QStringLiteral("color"), QStringLiteral("fill"), QStringLiteral("align"),
                                         QStringLiteral("numberFormat"), QStringLiteral("borders")};
    if (command.name == QStringLiteral("cells.format")
        && std::none_of(cellFormats.begin(), cellFormats.end(), [&command](const QString& key) { return command.arguments.contains(key); }))
        return failure("missing_argument", QStringLiteral("没有说要设置哪种格式：至少给出 bold、fill、align、numberFormat 等其中一项。"));
    if (command.name == QStringLiteral("slide.setText")
        && command.arguments.contains(QStringLiteral("text")) == command.arguments.contains(QStringLiteral("points")))
        return failure("invalid_argument", QStringLiteral("text 和 points 要给出且只给出其中一个。"));
    if (command.name == QStringLiteral("cells.filter")) {
        const int ways = int(command.arguments.contains(QStringLiteral("values"))) + int(command.arguments.contains(QStringLiteral("condition")))
            + int(command.arguments.contains(QStringLiteral("top"))) + int(command.arguments.contains(QStringLiteral("bottom")));
        if (ways != 1) return failure("invalid_argument", QStringLiteral("values、condition、top、bottom 要给出且只给出其中一种。"));
        static const QRegularExpression comparison(QStringLiteral("^(>=|<=|<>|>|<|=)\\s*\\S.*$"));
        if (command.arguments.contains(QStringLiteral("condition"))
            && !comparison.match(command.arguments.value(QStringLiteral("condition")).toString().trimmed()).hasMatch())
            return failure("invalid_argument", QStringLiteral("condition 要以 >、>=、<、<=、=、<> 开头，例如 >1000。"));
    }
    if (command.name == QStringLiteral("selection.select")) {
        const int ways = int(command.arguments.contains(QStringLiteral("find"))) + int(command.arguments.contains(QStringLiteral("paragraph")))
            + int(command.arguments.contains(QStringLiteral("table")));
        if (ways != 1) return failure("invalid_argument", QStringLiteral("find、paragraph、table 要给出且只给出其中一种。"));
    }
    // Ranges like "2-4", "1,3" or "all".
    static const QRegularExpression span(QStringLiteral("^(all|\\d+(-\\d+)?(,\\d+(-\\d+)?)*)$"));
    for (const char* name : {"rows", "columns", "paragraphs"}) {
        // (cells.set has "rows" too: an array of cells, checked above.)
        const QJsonValue given = command.arguments.value(QLatin1String(name));
        const QString value = given.toString().remove(QLatin1Char(' '));
        if (given.isString() && !span.match(value).hasMatch())
            return failure("invalid_argument", QStringLiteral("参数 %1 要写成 1、2-4、1,3 或 all。").arg(QLatin1String(name)));
    }
    return {true, QStringLiteral("valid"), QString(), {}};
}

QString formatWords(const QJsonObject& arguments)
{
    QStringList words;
    const auto flag = [&arguments, &words](const char* key, const QString& on, const QString& off) {
        const QJsonValue value = arguments.value(QLatin1String(key));
        if (value.isUndefined()) return;
        words << (value.toBool() || value.toString() == QStringLiteral("true") ? on : off);
    };
    flag("bold", QStringLiteral("加粗"), QStringLiteral("取消加粗"));
    flag("italic", QStringLiteral("斜体"), QStringLiteral("取消斜体"));
    flag("underline", QStringLiteral("下划线"), QStringLiteral("去掉下划线"));
    if (arguments.contains(QStringLiteral("fontSize")))
        words << QStringLiteral("字号 %1").arg(arguments.value(QStringLiteral("fontSize")).toVariant().toString());
    if (arguments.contains(QStringLiteral("color"))) words << QStringLiteral("文字颜色 ") + arguments.value(QStringLiteral("color")).toString();
    if (arguments.contains(QStringLiteral("fill"))) {
        const QString fill = arguments.value(QStringLiteral("fill")).toString();
        words << (fill == QStringLiteral("none") ? QStringLiteral("去掉底色") : QStringLiteral("底色 ") + fill);
    }
    static const QHash<QString, QString> aligns{{QStringLiteral("left"), QStringLiteral("左对齐")}, {QStringLiteral("center"), QStringLiteral("居中")},
                                                {QStringLiteral("right"), QStringLiteral("右对齐")}, {QStringLiteral("justify"), QStringLiteral("两端对齐")}};
    if (arguments.contains(QStringLiteral("align"))) words << aligns.value(arguments.value(QStringLiteral("align")).toString());
    if (arguments.contains(QStringLiteral("highlight"))) {
        const QString color = arguments.value(QStringLiteral("highlight")).toString();
        words << (color == QStringLiteral("none") ? QStringLiteral("去掉突出显示") : QStringLiteral("突出显示"));
    }
    static const QHash<QString, QString> styles{{QStringLiteral("heading1"), QStringLiteral("一级标题")}, {QStringLiteral("heading2"), QStringLiteral("二级标题")},
                                                {QStringLiteral("heading3"), QStringLiteral("三级标题")}, {QStringLiteral("normal"), QStringLiteral("正文样式")}};
    if (arguments.contains(QStringLiteral("style"))) words << styles.value(arguments.value(QStringLiteral("style")).toString());
    if (arguments.contains(QStringLiteral("numberFormat")))
        words << QStringLiteral("数字格式 ") + arguments.value(QStringLiteral("numberFormat")).toString();
    flag("borders", QStringLiteral("加细边框"), QStringLiteral("去掉边框"));
    return words.join(QStringLiteral("、"));
}

QString describeCommand(const AssistantCommand& command)
{
    const QJsonObject& a = command.arguments;
    const auto number = [&a](const char* key) { return a.value(QLatin1String(key)).toVariant().toString(); };
    const auto span = [&a](const char* key, const QString& unit) {
        const QString value = a.value(QLatin1String(key)).toString();
        if (value.isEmpty() || value == QStringLiteral("all")) return QStringLiteral("所有") + unit;
        QString shown = value;
        shown.replace(QLatin1Char(','), QStringLiteral("、")).replace(QLatin1Char('-'), QStringLiteral("–"));
        return QStringLiteral("第 %1 %2").arg(shown, unit);
    };
    const QString formats = formatWords(a);
    if (command.name == QStringLiteral("table.setCell")) {
        const QString text = a.value(QStringLiteral("text")).toString();
        return QStringLiteral("把第 %1 张表格第 %2 行第 %3 列改为：%4").arg(number("table"), number("row"), number("column"),
                                                                     text.size() > 200 ? text.left(200) + QStringLiteral("…") : text);
    }
    if (command.name == QStringLiteral("format.cells"))
        return QStringLiteral("第 %1 张表格%2、%3：%4").arg(number("table"), span("rows", QStringLiteral("行")), span("columns", QStringLiteral("列")), formats);
    if (command.name == QStringLiteral("format.paragraphs"))
        return QStringLiteral("%1：%2").arg(span("paragraphs", QStringLiteral("段")), formats);
    if (command.name == QStringLiteral("format.text"))
        return QStringLiteral("文中所有“%1”：%2").arg(a.value(QStringLiteral("find")).toString(), formats);
    if (command.name == QStringLiteral("format.selection")) return QStringLiteral("选中的文字：") + formats;
    // The sheet, when the command is for one other than the one shown.
    const QString onSheet = a.value(QStringLiteral("sheet")).toString().trimmed().isEmpty()
        ? QString() : QStringLiteral("工作表“%1”：").arg(a.value(QStringLiteral("sheet")).toString().trimmed());
    const auto column = [&a](const char* key) { return columnWords(a.value(QLatin1String(key)).toString()); };
    const auto whole = [&a]() {
        const QString range = a.value(QStringLiteral("range")).toString().trimmed().toUpper();
        return range.isEmpty() ? QStringLiteral("整张表") : range + QLatin1Char(' ');
    };
    if (command.name == QStringLiteral("cells.sort"))
        return onSheet + QStringLiteral("%1按%2%3排序（表头不动）").arg(whole(), column("by"),
            a.value(QStringLiteral("order")).toString() == QStringLiteral("desc") ? QStringLiteral("从大到小") : QStringLiteral("从小到大"));
    if (command.name == QStringLiteral("cells.filter")) {
        QString which;
        if (a.contains(QStringLiteral("values"))) {
            QStringList values;
            for (const QJsonValue& value : a.value(QStringLiteral("values")).toArray())
                values << QStringLiteral("“%1”").arg(value.toVariant().toString());
            which = QStringLiteral("%1是%2的行").arg(column("column"), values.join(QString()));
        } else if (a.contains(QStringLiteral("top"))) {
            which = QStringLiteral("%1最大的 %2 行").arg(column("column"), number("top"));
        } else if (a.contains(QStringLiteral("bottom"))) {
            which = QStringLiteral("%1最小的 %2 行").arg(column("column"), number("bottom"));
        } else {
            which = QStringLiteral("%1 %2 的行").arg(column("column"), a.value(QStringLiteral("condition")).toString().trimmed());
        }
        return onSheet + QStringLiteral("筛选：只显示%1（其余的隐藏，不删除）").arg(which);
    }
    if (command.name == QStringLiteral("cells.clearFilter")) return onSheet + QStringLiteral("取消筛选，显示所有行");
    if (command.name == QStringLiteral("cells.removeDuplicates")) {
        QStringList columns;
        for (const QJsonValue& value : a.value(QStringLiteral("columns")).toArray()) columns << columnWords(value.toVariant().toString());
        return onSheet + QStringLiteral("删除%1里重复的行%2（每组留第一行，表头不动）").arg(
            a.value(QStringLiteral("range")).toString().isEmpty() ? QStringLiteral("整张表") : QLatin1Char(' ') + a.value(QStringLiteral("range")).toString().toUpper() + QLatin1Char(' '),
            columns.isEmpty() ? QString() : QStringLiteral("，只比较%1").arg(columns.join(QStringLiteral("、")).replace(QStringLiteral("、 "), QStringLiteral("、"))));
    }
    if (command.name == QStringLiteral("chart.add")) {
        static const QHash<QString, QString> kinds{{QStringLiteral("column"), QStringLiteral("柱形图")}, {QStringLiteral("bar"), QStringLiteral("条形图")},
                                                   {QStringLiteral("line"), QStringLiteral("折线图")}, {QStringLiteral("pie"), QStringLiteral("饼图")},
                                                   {QStringLiteral("doughnut"), QStringLiteral("圆环图")}, {QStringLiteral("area"), QStringLiteral("面积图")},
                                                   {QStringLiteral("scatter"), QStringLiteral("散点图")}};
        QString range = a.value(QStringLiteral("range")).toString().toUpper();
        range.replace(QRegularExpression(QStringLiteral("\\s*,\\s*")), QStringLiteral("、"));
        const QString title = a.value(QStringLiteral("title")).toString().trimmed();
        return onSheet + QStringLiteral("用 %1 的数据插一张%2%3").arg(range, kinds.value(a.value(QStringLiteral("type")).toString(), QStringLiteral("柱形图")),
                                                                  title.isEmpty() ? QString() : QStringLiteral("，标题“%1”").arg(title));
    }
    if (command.name == QStringLiteral("sheet.add"))
        return QStringLiteral("新建工作表“%1”").arg(a.value(QStringLiteral("name")).toString().trimmed());
    if (command.name == QStringLiteral("sheet.rename")) {
        const QString from = a.value(QStringLiteral("sheet")).toString().trimmed();
        return QStringLiteral("把%1改名为“%2”").arg(from.isEmpty() ? QStringLiteral("当前工作表") : QStringLiteral("工作表“%1”").arg(from),
                                                    a.value(QStringLiteral("name")).toString().trimmed());
    }
    if (command.name == QStringLiteral("cells.set")) {
        const QJsonArray rows = a.value(QStringLiteral("rows")).toArray();
        int columns = 0;
        int formulas = 0;
        for (const QJsonValue& row : rows) {
            columns = std::max(columns, int(row.toArray().size()));
            for (const QJsonValue& cell : row.toArray()) formulas += cell.toVariant().toString().startsWith(QLatin1Char('=')) ? 1 : 0;
        }
        return onSheet + QStringLiteral("从 %1 开始写入 %2 行 × %3 列%4：").arg(a.value(QStringLiteral("start")).toString().toUpper())
            .arg(rows.size()).arg(columns).arg(formulas ? QStringLiteral("（含 %1 个公式）").arg(formulas) : QString());
    }
    if (command.name == QStringLiteral("cells.format"))
        return onSheet + QStringLiteral("%1：%2").arg(a.value(QStringLiteral("range")).toString().toUpper(), formats);
    if (command.name == QStringLiteral("slide.add")) {
        const int points = int(a.value(QStringLiteral("points")).toArray().size());
        return QStringLiteral("%1加一页幻灯片：标题“%2”%3").arg(
            a.value(QStringLiteral("position")).toString() == QStringLiteral("end") ? QStringLiteral("在最后") : QStringLiteral("在当前页后面"),
            a.value(QStringLiteral("title")).toString(),
            points ? QStringLiteral("，%1 个要点").arg(points) : QString());
    }
    if (command.name == QStringLiteral("slide.setText")) {
        static const QHash<QString, QString> parts{{QStringLiteral("title"), QStringLiteral("标题")},
                                                   {QStringLiteral("subtitle"), QStringLiteral("副标题")},
                                                   {QStringLiteral("body"), QStringLiteral("正文")}};
        QString text = a.value(QStringLiteral("text")).toString();
        if (a.contains(QStringLiteral("points"))) {
            QStringList lines;
            for (const QJsonValue& line : a.value(QStringLiteral("points")).toArray()) lines << line.toVariant().toString();
            text = lines.join(QStringLiteral(" / "));
        }
        return QStringLiteral("把第 %1 页的%2改为：%3").arg(number("slide"), parts.value(a.value(QStringLiteral("part")).toString()),
                                                       text.size() > 200 ? text.left(200) + QStringLiteral("…") : text);
    }
    if (command.name == QStringLiteral("edit.undo")) return QStringLiteral("撤销上一步修改。");
    if (command.name == QStringLiteral("edit.redo")) return QStringLiteral("重做刚才撤销的修改。");
    if (command.name == QStringLiteral("document.save")) return QStringLiteral("保存当前文档。");
    return {};
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
            if (argument.type == ArgumentType::Grid) {
                property.insert(QStringLiteral("type"), QStringLiteral("array"));
                property.insert(QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                     {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}});
                property.insert(QStringLiteral("maxItems"), argument.maximum);
            }
            if (argument.type == ArgumentType::Lines) {
                property.insert(QStringLiteral("type"), QStringLiteral("array"));
                property.insert(QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                     {QStringLiteral("maxLength"), argument.maxLength}});
                property.insert(QStringLiteral("maxItems"), argument.maximum);
            }
            if (argument.type == ArgumentType::Areas) property.insert(QStringLiteral("pattern"), areasPattern().pattern());
            if (argument.type == ArgumentType::Color)
                property.insert(QStringLiteral("pattern"), argument.allowNone ? QStringLiteral("^(#?[0-9A-Fa-f]{6}|none)$")
                                                                              : QStringLiteral("^#?[0-9A-Fa-f]{6}$"));
            if (argument.type == ArgumentType::DocumentKind)
                property.insert(QStringLiteral("enum"), QJsonArray::fromStringList(documentKinds()));
            if (argument.type == ArgumentType::Choice)
                property.insert(QStringLiteral("enum"), QJsonArray::fromStringList(argument.choices));
            if (argument.maxLength > 0 && argument.type != ArgumentType::Grid && argument.type != ArgumentType::Lines)
                property.insert(QStringLiteral("maxLength"), argument.maxLength);
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
