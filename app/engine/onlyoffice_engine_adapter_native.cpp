#include "onlyoffice_engine_adapter.h"

#include <qascapplicationmanager.h>
#include <qcefview.h>
#include <OfficeFileFormats.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QPointer>
#include <QSet>
#include <QStackedLayout>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <vector>
#include <mutex>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Native declarations/flows are from desktop-sdk ae6cf2a039355183c512fc314f7a9bd1a05a6a11.
// Upstream ONLYOFFICE/Ascensio notices and engine resources remain their own.
// This host does not implement document conversion or substitute file copying.
namespace openoffice {
namespace {
using namespace NSEditorApi;

EngineResult refused(const char* code, const QString& message)
{
    return {false, QString::fromLatin1(code), message};
}

EngineResult unavailable()
{
    return refused("engine_unavailable", QStringLiteral("ONLYOFFICE 原生引擎尚未就绪，本次操作未执行。"));
}

QString canonicalFile(const QString& path)
{
    const QFileInfo file(path);
    const QString canonical = file.canonicalFilePath();
    return canonical.isEmpty() ? QString() : QDir::cleanPath(canonical);
}

bool samePath(const QString& first, const QString& second)
{
    return !first.isEmpty() && !second.isEmpty()
        && QDir::cleanPath(first).compare(QDir::cleanPath(second), Qt::CaseInsensitive) == 0;
}

// A save destination may not exist yet: resolve its folder, never create it.
QString saveTarget(const QString& path)
{
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) return {};
    const QFileInfo file(QDir::cleanPath(path));
    if (file.exists()) return file.isFile() ? canonicalFile(file.filePath()) : QString();
    const QString folder = QFileInfo(file.absolutePath()).canonicalFilePath();
    if (folder.isEmpty() || file.fileName().isEmpty()) return {};
    return QDir::cleanPath(QDir(folder).filePath(file.fileName()));
}

// The editable OOXML format the native converter writes for a kind; 0 when
// this host does not save that kind.
int editableFormat(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Text: return AVS_OFFICESTUDIO_FILE_DOCUMENT_DOCX;
    case DocumentKind::Spreadsheet: return AVS_OFFICESTUDIO_FILE_SPREADSHEET_XLSX;
    case DocumentKind::Presentation: return AVS_OFFICESTUDIO_FILE_PRESENTATION_PPTX;
    default: return 0;
    }
}

// The native editor for a kind, and the blank seed CreateLocalFile starts it from
// (converter/empty/<lang>/new.<ext>, chosen by the lang editor parameter).
AscEditorType editorType(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Spreadsheet: return AscEditorType::etSpreadsheet;
    case DocumentKind::Presentation: return AscEditorType::etPresentation;
    default: return AscEditorType::etDocument;
    }
}

QString blankSeed(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Text: return QStringLiteral("converter/empty/zh-CN/new.docx");
    case DocumentKind::Spreadsheet: return QStringLiteral("converter/empty/zh-CN/new.xlsx");
    case DocumentKind::Presentation: return QStringLiteral("converter/empty/zh-CN/new.pptx");
    default: return {};
    }
}

// The title becomes the new document's name in the native recovery folder and
// the editor header, so it must be a plain file name.
bool validTitle(const QString& title, DocumentKind kind)
{
    if (title.isEmpty() || title.size() > 120 || title.trimmed() != title) return false;
    for (const QChar character : title)
        if (character.unicode() < 0x20 || QStringLiteral("\\/:*?\"<>|").contains(character)) return false;
    return classifyFileName(title) == kind;
}

// Pure-ASCII JavaScript string literal: the script crosses std::string and CEF
// string conversions, so every non-ASCII code unit is written as \uXXXX.
QString jsString(const QString& text)
{
    QString literal(QLatin1Char('"'));
    for (const QChar character : text) {
        const ushort unit = character.unicode();
        if (unit == '"' || unit == '\\') {
            literal += QLatin1Char('\\');
            literal += character;
        } else if (unit >= 0x20 && unit < 0x7F) {
            literal += character;
        } else {
            literal += QStringLiteral("\\u%1").arg(unit, 4, 16, QLatin1Char('0'));
        }
    }
    return literal + QLatin1Char('"');
}

const QStringList& editorCommandNames()
{
    static const QStringList names{QStringLiteral("text.insert"), QStringLiteral("selection.getText"),
                                   QStringLiteral("selection.replace"), QStringLiteral("document.getText"),
                                   QStringLiteral("document.replaceText"), QStringLiteral("edit.undo"),
                                   QStringLiteral("edit.redo"), QStringLiteral("document.outline"),
                                   QStringLiteral("table.read"), QStringLiteral("table.setCell"),
                                   QStringLiteral("format.cells"), QStringLiteral("format.paragraphs"),
                                   QStringLiteral("format.text"), QStringLiteral("selection.select"),
                                   QStringLiteral("format.read"), QStringLiteral("format.selection"),
                                   QStringLiteral("sheet.read"), QStringLiteral("cells.set"), QStringLiteral("cells.format"),
                                   QStringLiteral("cells.sort"), QStringLiteral("cells.filter"), QStringLiteral("cells.clearFilter"),
                                   QStringLiteral("cells.removeDuplicates"), QStringLiteral("chart.add"), QStringLiteral("sheet.add"),
                                   QStringLiteral("sheet.rename"),
                                   QStringLiteral("slides.read"), QStringLiteral("slide.add"), QStringLiteral("slide.setText")};
    return names;
}

// The commands that fit the open document: those written for its editor, and undo/redo.
bool commandFitsKind(const QString& name, DocumentKind kind)
{
    if (name == QStringLiteral("edit.undo") || name == QStringLiteral("edit.redo")) return true;
    static const QStringList sheet{QStringLiteral("sheet.read"), QStringLiteral("cells.set"), QStringLiteral("cells.format"),
                                   QStringLiteral("cells.sort"), QStringLiteral("cells.filter"), QStringLiteral("cells.clearFilter"),
                                   QStringLiteral("cells.removeDuplicates"), QStringLiteral("chart.add"), QStringLiteral("sheet.add"),
                                   QStringLiteral("sheet.rename")};
    static const QStringList slides{QStringLiteral("slides.read"), QStringLiteral("slide.add"), QStringLiteral("slide.setText")};
    if (kind == DocumentKind::Spreadsheet) return sheet.contains(name);
    if (kind == DocumentKind::Presentation) return slides.contains(name);
    return kind == DocumentKind::Text && !sheet.contains(name) && !slides.contains(name);
}

// What the engine itself accepts for each editor command, checked again here
// whatever the caller validated. "html" is not a catalog argument: the window
// adds it when it converts Markdown for text.insert.
struct EngineArgument {
    const char* name;
    bool required;
    int maxLength;
    QStringList choices;
    char kind = 's'; // s text, i integer (minimum..maximum), b true/false, c color #RRGGBB, n color or "none",
                     // a cell address (A1 or A1:C3), g rows of texts (maximum rows), l lines (maximum lines),
                     // k a column (C, or a cell of it), A up to 4 addresses split by commas, w a worksheet name
    int minimum = 0;
    int maximum = 0;
};

EngineArgument table(bool required) { return {"table", required, 0, {}, 'i', 1, 200}; }
EngineArgument flag(const char* name) { return {name, false, 0, {}, 'b'}; }
EngineArgument sheet() { return {"sheet", false, 31, {}, 'w'}; }
EngineArgument fontSize() { return {"fontSize", false, 0, {}, 'i', 6, 72}; }
EngineArgument align()
{
    return {"align", false, 0, {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right"), QStringLiteral("justify")}};
}

bool engineValueValid(const EngineArgument& argument, const QJsonValue& value)
{
    static const QRegularExpression hex(QStringLiteral("^#?[0-9A-Fa-f]{6}$"));
    switch (argument.kind) {
    case 'i': {
        bool ok = value.isDouble();
        double number = value.toDouble();
        if (value.isString()) number = value.toString().trimmed().toInt(&ok);
        return ok && number == static_cast<double>(static_cast<qint64>(number)) && number >= argument.minimum
            && (argument.maximum <= 0 || number <= argument.maximum);
    }
    case 'b':
        return value.isBool() || value.toString() == QStringLiteral("true") || value.toString() == QStringLiteral("false");
    case 'c':
    case 'n':
        return value.isString() && (hex.match(value.toString()).hasMatch() || (argument.kind == 'n' && value.toString() == QStringLiteral("none")));
    case 'a': {
        static const QRegularExpression address(QStringLiteral("^\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7}(:\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7})?$"));
        return value.isString() && address.match(value.toString()).hasMatch();
    }
    case 'A': {
        static const QRegularExpression addresses(QStringLiteral(
            "^\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7}(:\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7})?"
            "(\\s*,\\s*\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7}(:\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7})?){0,3}$"));
        return value.isString() && addresses.match(value.toString()).hasMatch();
    }
    case 'k': { // a column letter or a header text
        const QString text = value.toString().trimmed();
        return value.isString() && !text.isEmpty() && text.size() <= argument.maxLength && !text.contains(QLatin1Char('\n'));
    }
    case 'w': {
        static const QRegularExpression forbidden(QStringLiteral("[\\[\\]:*?/\\\\]"));
        const QString text = value.toString().trimmed();
        return value.isString() && !text.isEmpty() && text.size() <= 31 && !forbidden.match(text).hasMatch()
            && !text.startsWith(QLatin1Char('\'')) && !text.endsWith(QLatin1Char('\''));
    }
    case 'g':
    case 'l': {
        const QJsonArray outer = value.toArray();
        if (!value.isArray() || outer.isEmpty() || outer.size() > argument.maximum) return false;
        for (const QJsonValue& item : outer) {
            const QJsonArray cells = argument.kind == 'g' ? item.toArray() : QJsonArray{item};
            if (argument.kind == 'g' && (!item.isArray() || cells.size() > 50)) return false;
            for (const QJsonValue& cell : cells)
                if ((!cell.isString() && !cell.isDouble() && !cell.isBool()) || cell.toVariant().toString().size() > argument.maxLength)
                    return false;
        }
        return true;
    }
    default: {
        if (!value.isString()) return false;
        const QString text = value.toString();
        if (argument.maxLength > 0 && text.size() > argument.maxLength) return false;
        return argument.choices.isEmpty() || argument.choices.contains(text);
    }
    }
}

const QHash<QString, QList<EngineArgument>>& editorCommandArguments()
{
    static const QHash<QString, QList<EngineArgument>> arguments{
        {QStringLiteral("text.insert"),
         {{"text", true, 20000, {}},
          {"format", false, 0, {QStringLiteral("plain"), QStringLiteral("markdown")}},
          {"position", false, 0, {QStringLiteral("cursor"), QStringLiteral("after"), QStringLiteral("end")}},
          {"html", false, 400000, {}}}},
        {QStringLiteral("selection.replace"), {{"text", true, 20000, {}}, {"expected", false, 20000, {}}}},
        {QStringLiteral("document.replaceText"), {{"find", true, 200, {}}, {"replace", true, 200, {}}}},
        {QStringLiteral("table.read"), {table(true)}},
        {QStringLiteral("table.setCell"), {table(true), {"row", true, 0, {}, 'i', 1, 500}, {"column", true, 0, {}, 'i', 1, 100},
                                           {"text", true, 2000, {}}}},
        {QStringLiteral("format.cells"), {table(true), {"rows", false, 40, {}}, {"columns", false, 40, {}}, flag("bold"),
                                          flag("italic"), fontSize(), {"color", false, 0, {}, 'c'}, {"fill", false, 0, {}, 'n'},
                                          align()}},
        {QStringLiteral("format.paragraphs"), {{"paragraphs", true, 40, {}}, flag("bold"), flag("italic"), fontSize(),
                                               {"color", false, 0, {}, 'c'}, align(),
                                               {"style", false, 0, {QStringLiteral("heading1"), QStringLiteral("heading2"),
                                                                    QStringLiteral("heading3"), QStringLiteral("normal")}}}},
        {QStringLiteral("format.text"), {{"find", true, 200, {}}, flag("bold"), flag("italic"), flag("underline"), fontSize(),
                                         {"color", false, 0, {}, 'c'},
                                         {"highlight", false, 0, {QStringLiteral("yellow"), QStringLiteral("green"),
                                                                  QStringLiteral("cyan"), QStringLiteral("magenta"),
                                                                  QStringLiteral("red"), QStringLiteral("blue"),
                                                                  QStringLiteral("lightGray"), QStringLiteral("none")}}}},
        {QStringLiteral("format.read"), {{"find", true, 200, {}}}},
        {QStringLiteral("format.selection"), {flag("bold"), flag("italic"), flag("underline"), fontSize(),
                                              {"color", false, 0, {}, 'c'}, align(),
                                              {"highlight", false, 0, {QStringLiteral("yellow"), QStringLiteral("green"),
                                                                     QStringLiteral("cyan"), QStringLiteral("magenta"),
                                                                     QStringLiteral("red"), QStringLiteral("blue"),
                                                                     QStringLiteral("lightGray"), QStringLiteral("none")}},
                                              {"style", false, 0, {QStringLiteral("heading1"), QStringLiteral("heading2"),
                                                                 QStringLiteral("heading3"), QStringLiteral("normal")}}}},
        {QStringLiteral("sheet.read"), {{"range", false, 20, {}, 'a'}, sheet()}},
        {QStringLiteral("cells.set"), {{"start", true, 20, {}, 'a'}, {"rows", true, 1000, {}, 'g', 0, 200}, sheet()}},
        {QStringLiteral("cells.format"), {{"range", true, 20, {}, 'a'}, flag("bold"), flag("italic"), fontSize(),
                                          {"color", false, 0, {}, 'c'}, {"fill", false, 0, {}, 'n'},
                                          {"align", false, 0, {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}},
                                          {"numberFormat", false, 40, {}}, flag("borders"), sheet()}},
        {QStringLiteral("cells.sort"), {{"by", true, 40, {}, 'k'}, {"order", false, 0, {QStringLiteral("asc"), QStringLiteral("desc")}},
                                        {"range", false, 20, {}, 'a'}, flag("header"), sheet()}},
        {QStringLiteral("cells.filter"), {{"column", true, 40, {}, 'k'}, {"values", false, 100, {}, 'l', 0, 20},
                                          {"condition", false, 60, {}}, {"top", false, 0, {}, 'i', 1, 500},
                                          {"bottom", false, 0, {}, 'i', 1, 500}, {"range", false, 20, {}, 'a'}, sheet()}},
        {QStringLiteral("cells.clearFilter"), {sheet()}},
        {QStringLiteral("cells.removeDuplicates"), {{"range", false, 20, {}, 'a'}, {"columns", false, 40, {}, 'l', 0, 10},
                                                    flag("header"), sheet()}},
        {QStringLiteral("chart.add"), {{"range", true, 100, {}, 'A'},
                                       {"type", false, 0, {QStringLiteral("column"), QStringLiteral("bar"), QStringLiteral("line"),
                                                           QStringLiteral("pie"), QStringLiteral("doughnut"), QStringLiteral("area"),
                                                           QStringLiteral("scatter")}},
                                       {"title", false, 100, {}}, sheet()}},
        {QStringLiteral("sheet.add"), {{"name", true, 31, {}, 'w'}}},
        {QStringLiteral("sheet.rename"), {{"name", true, 31, {}, 'w'}, sheet()}},
        {QStringLiteral("slide.add"), {{"title", true, 200, {}}, {"points", false, 300, {}, 'l', 0, 12}, {"subtitle", false, 300, {}},
                                       {"position", false, 0, {QStringLiteral("after"), QStringLiteral("end")}}}},
        {QStringLiteral("slide.setText"), {{"slide", true, 0, {}, 'i', 1, 500},
                                           {"part", true, 0, {QStringLiteral("title"), QStringLiteral("subtitle"), QStringLiteral("body")}},
                                           {"text", false, 2000, {}}, {"points", false, 300, {}, 'l', 0, 12}}},
        {QStringLiteral("selection.select"), {{"find", false, 200, {}}, {"paragraph", false, 0, {}, 'i', 1, 100000},
                                              table(false), {"row", false, 0, {}, 'i', 1, 500},
                                              {"column", false, 0, {}, 'i', 1, 100}}},
    };
    return arguments;
}

// Formatting commands must say what to change; selection.select needs exactly one target.
bool editorArgumentsComplete(const QString& name, const QJsonObject& arguments)
{
    static const QStringList formats{QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("underline"),
                                     QStringLiteral("fontSize"), QStringLiteral("color"), QStringLiteral("fill"),
                                     QStringLiteral("align"), QStringLiteral("highlight"), QStringLiteral("style")};
    if (name.startsWith(QStringLiteral("format.")) && name != QStringLiteral("format.read"))
        return std::any_of(formats.begin(), formats.end(), [&arguments](const QString& key) { return arguments.contains(key); });
    if (name == QStringLiteral("cells.format")) {
        static const QStringList cellFormats{QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("fontSize"),
                                             QStringLiteral("color"), QStringLiteral("fill"), QStringLiteral("align"),
                                             QStringLiteral("numberFormat"), QStringLiteral("borders")};
        return std::any_of(cellFormats.begin(), cellFormats.end(), [&arguments](const QString& key) { return arguments.contains(key); });
    }
    if (name == QStringLiteral("slide.setText"))
        return arguments.contains(QStringLiteral("text")) != arguments.contains(QStringLiteral("points"));
    if (name == QStringLiteral("cells.filter")) {
        // One way to say which rows stay: values, a comparison, or the largest or smallest few.
        static const QRegularExpression comparison(QStringLiteral("^(>=|<=|<>|>|<|=)\\s*\\S.*$"));
        const int ways = (arguments.contains(QStringLiteral("values")) ? 1 : 0) + (arguments.contains(QStringLiteral("condition")) ? 1 : 0)
            + (arguments.contains(QStringLiteral("top")) ? 1 : 0) + (arguments.contains(QStringLiteral("bottom")) ? 1 : 0);
        return ways == 1 && (!arguments.contains(QStringLiteral("condition"))
                             || comparison.match(arguments.value(QStringLiteral("condition")).toString().trimmed()).hasMatch());
    }
    if (name == QStringLiteral("selection.select")) {
        const int ways = (arguments.contains(QStringLiteral("find")) ? 1 : 0)
            + (arguments.contains(QStringLiteral("paragraph")) ? 1 : 0) + (arguments.contains(QStringLiteral("table")) ? 1 : 0);
        return ways == 1;
    }
    return true;
}

bool editorArgumentsValid(const QString& name, const QJsonObject& arguments)
{
    const QList<EngineArgument> allowed = editorCommandArguments().value(name);
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        const auto known = std::find_if(allowed.begin(), allowed.end(),
                                        [&it](const EngineArgument& argument) { return it.key() == QLatin1String(argument.name); });
        if (known == allowed.end() || !engineValueValid(*known, it.value())) return false;
    }
    for (const EngineArgument& argument : allowed) {
        const QJsonValue value = arguments.value(QLatin1String(argument.name));
        if (argument.required && (value.isUndefined() || (value.isString() && value.toString().isEmpty()))) return false;
    }
    if (!editorArgumentsComplete(name, arguments)) return false;
    if (name == QStringLiteral("text.insert") && arguments.value(QStringLiteral("format")).toString() == QStringLiteral("markdown")
        && arguments.value(QStringLiteral("html")).toString().isEmpty())
        return false; // Markdown is converted by the host before it reaches the editor
    return true;
}

QString jsValue(const QJsonObject& arguments, const char* name)
{
    const QJsonValue value = arguments.value(QLatin1String(name));
    return value.isString() ? jsString(value.toString()) : QStringLiteral("null");
}

// Helpers for the structure and style commands. Built on the editor's document
// builder API (AscBuilder: the API of its macros and plugins). Changes run the way
// the editor runs a plugin command: runBuilderScript opens one undoable action,
// _afterEvalCommand recalculates and redraws, onEndBuilderScript closes it.
const char* const kStructureHelpers = R"JS(
var blocks=function(){var d=B.GetDocument(),n=d.GetElementsCount(),out=[];for(var i=0;i<n;i++)out.push(d.GetElement(i));return out;};
var kinds=function(kind){var all=blocks(),out=[];for(var i=0;i<all.length;i++)if(all[i].GetClassType()===kind)out.push(all[i]);return out;};
var span=function(spec,count){var s=String(spec===undefined||spec===null?'all':spec).replace(/\s+/g,'');
  var out=[];if(s===''||s==='all'){for(var i=0;i<count;i++)out.push(i);return out;}
  var parts=s.split(',');for(var j=0;j<parts.length;j++){var m=/^(\d+)(?:-(\d+))?$/.exec(parts[j]);if(!m)return null;
    var a=+m[1],b=m[2]?+m[2]:a;if(a<1||b<a||b>count)return null;for(var k=a;k<=b;k++)if(out.indexOf(k-1)<0)out.push(k-1);}
  return out;};
var cellText=function(cell){return String(cell.GetContent().GetText({})||'').replace(/\r\n?/g,'\n').replace(/[\t\n]+$/,'');};
var hexOf=function(c){var x=function(v){v=(v|0).toString(16).toUpperCase();return v.length<2?'0'+v:v;};return '#'+x(c.r)+x(c.g)+x(c.b);};
var looks=function(range){var o={};if(!range)return o;try{var t=range.private_GetTextPr();
  if(t.Bold)o.bold=true;if(t.Italic)o.italic=true;if(t.Underline)o.underline=true;if(t.FontSize!==undefined)o.size=t.FontSize;
  if(t.Color&&!t.Color.Auto){var h=hexOf(t.Color);if(h!=='#000000')o.color=h;}if(t.HighLight&&t.HighLight!==-1)o.highlight=true;}catch(x){}return o;};
var textFormat=function(range,a){if(!range)return 0;var n=0;
  if(has(a.bold)){range.SetBold(bool(a.bold));n++;}
  if(has(a.italic)){range.SetItalic(bool(a.italic));n++;}
  if(has(a.underline)){range.SetUnderline(bool(a.underline));n++;}
  if(has(a.fontSize)){range.SetFontSize(2*(+a.fontSize));n++;}
  if(has(a.color)){var c=rgb(a.color);range.SetColor(c[0],c[1],c[2]);n++;}
  if(has(a.highlight)){range.SetHighlight(a.highlight);n++;}
  return n;};
var alignOf=function(v){return v==='justify'?'both':v;};
if(!B){reply(false,'editor_not_ready',null);return;}
)JS";

const char* const kOutlineScript = R"JS(
var all=blocks(),out=[],tp=0,tt=0,cursor=null,curPara=null,curCell=null;
try{if(logic){var cp=logic.GetCurrentParagraph();var ap=cp&&B.GetByInternalId(cp.GetId());
  if(ap){var cc=ap.GetParentTableCell&&ap.GetParentTableCell();if(cc)curCell=cc;else curPara=ap.GetInternalId();}}}catch(x){}
for(var i=0;i<all.length;i++){var e=all[i],k=e.GetClassType();
  if(k==='paragraph'){tp++;if(out.length<400){var st=e.GetStyle(),t=String(e.GetText({Numbering:true})||'').replace(/[\r\n]+$/,'');
      var o={id:'P'+tp,style:st?st.GetName():'',text:t.slice(0,160),length:t.length},lk=t.length?looks(e.GetRange()):{};
      for(var k in lk)o[k]=lk[k];try{var jc=e.GetJc();if(jc&&jc!=='left')o.align=jc==='both'?'justify':jc;}catch(x){}
      out.push(o);}
    if(curPara&&e.GetInternalId()===curPara)cursor={paragraph:tp};}
  else if(k==='table'){tt++;var rows=e.GetRowsCount(),cols=0,first=[];for(var r=0;r<rows;r++)cols=Math.max(cols,e.GetRow(r).GetCellsCount());
    var r0=e.GetRow(0);for(var c=0;c<r0.GetCellsCount();c++)first.push(cellText(r0.GetCell(c)).replace(/\n/g,' ').slice(0,40));
    var to={id:'T'+tt,rows:rows,columns:cols,firstRow:first};
    try{var s=B.GetDocument().GetFinalSection().Section,textMm=s.PageSize.W-s.PageMargins.Left-s.PageMargins.Right,
      grid=e.Table.TableGrid||[],sum=0;for(var g=0;g<grid.length;g++)sum+=+grid[g]||0;
      var tw=e.Table.Pr&&e.Table.Pr.TableW;
      if(sum>0&&textMm>0)to.width=Math.round(100*sum/textMm);else if(tw&&tw.Type===3)to.width=Math.round(tw.W);}catch(x){}
    if(out.length<400)out.push(to);
    if(curCell){var pt=curCell.GetParentTable();if(pt&&pt.GetInternalId()===e.GetInternalId())
      cursor={table:tt,row:curCell.GetRowIndex()+1,column:curCell.GetIndex()+1};}}}
reply(true,'outline',{blocks:out,paragraphs:tp,tables:tt,cursor:cursor,truncated:out.length>=400});
)JS";

const char* const kTableReadScript = R"JS(
var tables=kinds('table'),e=tables[+args.table-1];
if(!e){reply(false,'no_table',{table:+args.table,tables:tables.length});return;}
var rows=[],cells=0,columns=0;
for(var r=0;r<e.GetRowsCount();r++){var row=e.GetRow(r),list=[];columns=Math.max(columns,row.GetCellsCount());
  for(var c=0;c<row.GetCellsCount();c++){var cell=row.GetCell(c),p=cell.GetContent().GetElement(0),info={text:cellText(cell).slice(0,300)};
    if(++cells>2000){info.text='';}
    try{var run=p&&p.GetElement(0),cp=run&&run.Run&&run.Run.Get_CompiledPr&&run.Run.Get_CompiledPr(false);
      if(cp){if(cp.Bold)info.bold=true;if(cp.Italic)info.italic=true;}
      else if(run&&run.GetTextPr){var tp=run.GetTextPr();if(tp.GetBold())info.bold=true;if(tp.GetItalic())info.italic=true;}}catch(x){}
    try{var jc=p&&p.GetJc&&p.GetJc();if(jc&&jc!=='left')info.align=jc==='both'?'justify':jc;}catch(x){}
    try{var sh=cell.Cell&&cell.Cell.Get_Shd&&cell.Cell.Get_Shd();
      if(sh&&sh.Value===Asc.c_oAscShdClear&&sh.Color&&!sh.Color.Auto){var f=hexOf(sh.Color);if(f!=='#FFFFFF')info.fill=f;}}catch(x){}
    list.push(info);}
  rows.push(list);}
reply(true,'table',{table:+args.table,rowsCount:rows.length,columns:columns,rows:rows,truncated:cells>2000});
)JS";

const char* const kSetCellScript = R"JS(
var t=+args.table,r=+args.row,c=+args.column,text=String(args.text);
edit(function(){var tables=kinds('table'),e=tables[t-1];
  if(!e)return{refused:'no_table',table:t,tables:tables.length};
  if(r>e.GetRowsCount()||c>e.GetRow(r-1).GetCellsCount())return{refused:'no_cell',table:t,row:r,column:c};
  var cell=e.GetRow(r-1).GetCell(c-1),content=cell.GetContent(),first=content.GetElement(0),before=cellText(cell);
  if(before===text.replace(/\r\n?/g,'\n'))return{code:'unchanged',table:t,row:r,column:c};
  var pr=null;try{var run=first.GetElement(0);if(run&&run.GetTextPr)pr=run.GetTextPr();}catch(x){}
  while(content.GetElementsCount()>1)content.RemoveElement(1);
  first.RemoveAllElements();
  var put=function(p,line){var run=B.CreateRun();run.AddText(line);if(pr)run.SetTextPr(pr);p.AddElement(run);};
  var lines=text.replace(/\r\n?/g,'\n').split('\n');put(first,lines[0]);
  for(var i=1;i<lines.length;i++){var np=B.CreateParagraph();put(np,lines[i]);content.Push(np);}
  return{table:t,row:r,column:c,before:before.slice(0,200)};},'cell_set');
)JS";

const char* const kFormatCellsScript = R"JS(
var a=args,t=+a.table,tables=kinds('table'),e=tables[t-1];
if(!e){reply(false,'no_table',{table:t,tables:tables.length});return;}
var rows=span(a.rows,e.GetRowsCount());if(!rows||!rows.length){reply(false,'bad_range',{what:'rows'});return;}
var targets=[];for(var i=0;i<rows.length;i++){var row=e.GetRow(rows[i]),cols=span(a.columns,row.GetCellsCount());
  if(!cols){reply(false,'bad_range',{what:'columns'});return;}for(var j=0;j<cols.length;j++)targets.push(row.GetCell(cols[j]));}
if(!targets.length){reply(false,'bad_range',{what:'columns'});return;}
edit(function(){for(var i=0;i<targets.length;i++){var cell=targets[i],content=cell.GetContent();
    textFormat(content.GetRange(),a);
    if(has(a.align))for(var q=0;q<content.GetElementsCount();q++){var el=content.GetElement(q);if(el.GetClassType()==='paragraph')el.SetJc(alignOf(a.align));}
    if(has(a.fill)){if(a.fill==='none')cell.SetShd('nil',255,255,255,true);else{var f=rgb(a.fill);cell.SetShd('clear',f[0],f[1],f[2],false);}}}
  return{table:t,cells:targets.length};},'formatted');
)JS";

const char* const kFormatParagraphsScript = R"JS(
var a=args,ps=kinds('paragraph'),idx=span(a.paragraphs,ps.length);
if(!idx||!idx.length){reply(false,'bad_range',{what:'paragraphs',paragraphs:ps.length});return;}
var style=null;if(has(a.style)){style=B.GetDocument().GetStyle({heading1:'Heading 1',heading2:'Heading 2',heading3:'Heading 3',normal:'Normal'}[a.style]);
  if(!style){reply(false,'no_style',{style:a.style});return;}}
edit(function(){for(var i=0;i<idx.length;i++){var p=ps[idx[i]];if(style)p.SetStyle(style);if(has(a.align))p.SetJc(alignOf(a.align));textFormat(p.GetRange(),a);}
  return{paragraphs:idx.length};},'formatted');
)JS";

const char* const kFormatReadScript = R"JS(
var found=B.GetDocument().Search(String(args.find),true)||[],list=[];
for(var i=0;i<found.length&&i<20;i++)list.push(looks(found[i]));
reply(found.length>0,found.length?'format_read':'not_found',{find:args.find,count:found.length,occurrences:list});
)JS";

const char* const kFormatTextScript = R"JS(
var a=args,found=B.GetDocument().Search(String(a.find),true)||[];
if(!found.length){reply(false,'not_found',{find:a.find});return;}
edit(function(){var n=Math.min(found.length,100);for(var i=0;i<n;i++)textFormat(found[i],a);return{count:n,total:found.length};},'formatted');
)JS";

const char* const kFormatSelectionScript = R"JS(
var a=args,r=B.GetDocument().GetRangeBySelect();
if(!r||!String(r.GetText()||'').replace(/\s+/g,'')){reply(false,'no_selection',null);return;}
var style=null;if(has(a.style)){style=B.GetDocument().GetStyle({heading1:'Heading 1',heading2:'Heading 2',heading3:'Heading 3',normal:'Normal'}[a.style]);
  if(!style){reply(false,'no_style',{style:a.style});return;}}
edit(function(){textFormat(r,a);var ps=(style||has(a.align))?(r.GetAllParagraphs()||[]):[];
  for(var i=0;i<ps.length;i++){if(style)ps[i].SetStyle(style);if(has(a.align))ps[i].SetJc(alignOf(a.align));}
  return{selection:true};},'formatted');
)JS";

const char* const kSelectScript = R"JS(
var a=args;
if(has(a.find)){var f=B.GetDocument().Search(String(a.find),true);if(!f||!f.length){reply(false,'not_found',{find:a.find});return;}
  f[0].Select();reply(true,'selected',{what:'text'});return;}
if(has(a.paragraph)){var ps=kinds('paragraph'),p=ps[+a.paragraph-1];if(!p){reply(false,'no_paragraph',{paragraph:+a.paragraph,paragraphs:ps.length});return;}
  p.Select();reply(true,'selected',{what:'paragraph',paragraph:+a.paragraph});return;}
var tables=kinds('table'),e=tables[+a.table-1];if(!e){reply(false,'no_table',{table:+a.table,tables:tables.length});return;}
var r=+(a.row||1),c=+(a.column||1);
if(r>e.GetRowsCount()||c>e.GetRow(r-1).GetCellsCount()){reply(false,'no_cell',{table:+a.table,row:r,column:c});return;}
var range=e.GetRow(r-1).GetCell(c-1).GetContent().GetRange();if(range)range.Select();
reply(true,'selected',{what:'cell',table:+a.table,row:r,column:c});
)JS";

// The one undoable step every change runs in (the way the editor runs a plugin command:
// runBuilderScript opens it, _afterEvalCommand recalculates and redraws, onEndBuilderScript
// closes it). Shared by the text, spreadsheet and presentation commands.
const char* const kEditHelpers = R"JS(
var rgb=function(hex){var h=String(hex).replace('#','');return [parseInt(h.slice(0,2),16),parseInt(h.slice(2,4),16),parseInt(h.slice(4,6),16)];};
var bool=function(v){return v===true||v==='true';};
var has=function(v){return v!==undefined&&v!==null&&v!=='';};
var edit=function(change,code){var settled=false,finish=function(ok,c,v){if(settled)return;settled=true;reply(ok,c,v);};
  var guard=setTimeout(function(){finish(false,'editor_locked',null);},10000);
  api.runBuilderScript(function(){var value=null,error=null;api._beforeEvalCommand();
    try{value=change();}catch(e){error=String((e&&e.message)||e).slice(0,200);}
    var end=function(){api.onEndBuilderScript(function(){clearTimeout(guard);
      if(error)finish(false,'edit_failed',error);
      else if(value&&value.refused)finish(false,value.refused,value);
      else finish(true,value&&value.code?value.code:code,value);});};
    if(!AscCommon.History.Is_LastPointEmpty())api._afterEvalCommand(end);else end();},'');};
)JS";

// Spreadsheets, through the editor's spreadsheet builder API (AscBuilder.Cell.Api) and,
// for reading, its workbook model. Addresses are A1 notation.
const char* const kSheetHelpers = R"JS(
var C=window.AscBuilder&&AscBuilder.Cell&&AscBuilder.Cell.Api;
if(!C||!api.wbModel){reply(false,'editor_not_ready',null);return;}
var colOf=function(letters){var n=0;letters=letters.toUpperCase();for(var i=0;i<letters.length;i++)n=n*26+(letters.charCodeAt(i)-64);return n-1;};
var letterOf=function(c){var s='';c++;while(c>0){var m=(c-1)%26;s=String.fromCharCode(65+m)+s;c=Math.floor((c-1)/26);}return s;};
var cellAt=function(text){var m=/^\$?([A-Za-z]{1,3})\$?([0-9]{1,7})$/.exec(String(text||''));return m?{r:+m[2]-1,c:colOf(m[1])}:null;};
var area=function(text){var parts=String(text||'').split(':'),a=cellAt(parts[0]),b=parts.length>1?cellAt(parts[1]):a;
  if(!a||!b)return null;return {r1:Math.min(a.r,b.r),c1:Math.min(a.c,b.c),r2:Math.max(a.r,b.r),c2:Math.max(a.c,b.c)};};
var name=function(r,c){return letterOf(c)+(r+1);};
var hexOfRgb=function(v){var h=(v&0xFFFFFF).toString(16).toUpperCase();while(h.length<6)h='0'+h;return '#'+h;};
// The sheet a command is for: the one named (made active by the change, so the person sees it), or the active one.
var sheetOf=function(text){if(!has(text))return C.GetActiveSheet();return C.GetSheet(String(text))||null;};
var usedOf=function(ws){return {r1:0,c1:0,r2:Math.max(ws.getRowsCount()-1,0),c2:Math.max(ws.getColsCount()-1,0)};};
var boxName=function(b){return name(b.r1,b.c1)+':'+name(b.r2,b.c2);};
var columnOf=function(text){var m=/^\$?([A-Za-z]{1,3})(\$?[0-9]{1,7})?$/.exec(String(text||'').trim());return m?colOf(m[1]):-1;};
var valueAt=function(ws,r,c){return String(ws.getCell3(r,c).getValue());};
var numeric=function(v){return v!==''&&!isNaN(Number(v));};
// A header row when it is not said: every cell of the first row is a text that is not a number.
var headerOf=function(ws,box,given){if(has(given))return bool(given);
  for(var c=box.c1;c<=box.c2;c++){var v=valueAt(ws,box.r1,c);if(v===''||numeric(v))return false;}return box.r2>box.r1;};
// A column named by its letter (C, C1) or by its header in the first row of the range ("销售额").
var columnIn=function(ws,box,text){var t=String(text||'').trim();if(!t)return -1;
  for(var c=box.c1;c<=box.c2;c++)if(valueAt(ws,box.r1,c).trim()===t)return c;return columnOf(t);};
// The range a sort, filter or clean-up works on: the one given, or what the sheet uses; one row
// is never meant (small models write "A1:Z1"), so that is taken as not given.
var rangeOf=function(ws,text){if(!has(text))return usedOf(ws);var b=area(text);if(b&&b.r1===b.r2)return usedOf(ws);return b;};
var sheetName=function(t){return t.length>0&&t.length<=31&&!/[\[\]:*?\/\\]/.test(t)&&t.charAt(0)!=="'"&&t.charAt(t.length-1)!=="'";};
)JS";

const char* const kSheetReadScript = R"JS(
var ws=has(args.sheet)?api.wbModel.getWorksheetByName(String(args.sheet)):api.wbModel.getActiveWs(),sheets=[];
if(!ws){reply(false,'no_sheet',{sheet:args.sheet});return;}
for(var i=0;i<api.wbModel.getWorksheetCount();i++)sheets.push(api.wbModel.getWorksheet(i).getName());
var used=usedOf(ws);
var box=has(args.range)?area(args.range):used;if(!box){reply(false,'bad_address',{range:args.range});return;}
var r2=Math.min(box.r2,box.r1+59),c2=Math.min(box.c2,box.c1+25),rows=[],formulas={},styles={};
for(var r=box.r1;r<=r2;r++){var row=[];for(var c=box.c1;c<=c2;c++){var cell=ws.getCell3(r,c),v=cell.getValue(),f=cell.getFormula();
    row.push(v);if(f)formulas[name(r,c)]='='+f;
    try{var g=ws.getRange3(r,c,r,c),font=g.getFont(),fill=g.getFill(),o={};
      if(font&&font.getBold&&font.getBold())o.bold=true;var bg=fill&&fill.bg?fill.bg():null;if(bg&&bg.rgb!==undefined&&(bg.rgb&0xFFFFFF)!==0xFFFFFF)o.fill=hexOfRgb(bg.rgb);
      if(o.bold||o.fill)styles[name(r,c)]=o;}catch(x){}}
  rows.push(row);}
var sel=null;try{var last=ws.selectionRange.getLast();sel=name(last.r1,last.c1)+(last.r2!==last.r1||last.c2!==last.c1?':'+name(last.r2,last.c2):'');}catch(x){}
var hidden=[];for(var h=box.r1;h<=r2;h++)if(ws.getRowHidden(h))hidden.push(h+1);
var charts=[];try{var all=C.GetSheet(ws.getName()).GetAllCharts();for(var k=0;k<all.length&&k<20;k++){var t=null;try{t=all[k].GetTitle();}catch(x){}
  charts.push({title:t?String(t).trim().slice(0,100):'',type:String(all[k].GetChartType?all[k].GetChartType():'')});}}catch(x){}
var filter=null;if(ws.AutoFilter&&ws.AutoFilter.Ref){var fr=ws.AutoFilter.Ref,off=0;for(var q=fr.r1+1;q<=fr.r2;q++)if(ws.getRowHidden(q))off++;
  filter={range:boxName(fr),hiddenRows:off};}
reply(true,'sheet',{sheet:ws.getName(),active:api.wbModel.getActiveWs().getName(),sheets:sheets,range:name(box.r1,box.c1)+':'+name(r2,c2),
  used:name(used.r1,used.c1)+':'+name(used.r2,used.c2),rows:rows,formulas:formulas,styles:styles,selection:sel,hiddenRows:hidden,
  charts:charts,filter:filter,truncated:r2<box.r2||c2<box.c2});
)JS";

const char* const kCellsSetScript = R"JS(
var start=cellAt(args.start);if(!start){reply(false,'bad_address',{start:args.start});return;}
var sh=sheetOf(args.sheet);if(!sh){reply(false,'no_sheet',{sheet:args.sheet});return;}
var why={'-309':'reference','-300':'parentheses','-308':'parentheses','-301':'operator','-307':'operand','-302':'arguments','-303':'arguments'};
for(var fr=0;fr<args.rows.length;fr++){var cellsOf=args.rows[fr]||[];for(var fc=0;fc<cellsOf.length;fc++){var text=cellsOf[fc];
  if(typeof text!=='string'||text.charAt(0)!=='='||text.length<2)continue;
  var at=name(start.r+fr,start.c+fc),parsed=null,result=null;
  try{parsed=new AscCommonExcel.parserFormula(text.slice(1),null,sh.worksheet);result=new AscCommonExcel.ParseResult([]);}catch(x){parsed=null;}
  if(!parsed)continue;
  if(!parsed.parse(false,null,result)){reply(false,'bad_formula',{cell:at,formula:text.slice(0,200),why:why[String(result.error)]||'syntax'});return;}
  var unknown=(parsed.outStack||[]).filter(function(e){return e instanceof AscCommonExcel.cUnknownFunction;});
  if(unknown.length){reply(false,'bad_formula',{cell:at,formula:text.slice(0,200),why:'function',function:String(unknown[0].name)});return;}}}
edit(function(){if(has(args.sheet))sh.SetActive();var n=0,f=0,width=0;
  for(var r=0;r<args.rows.length;r++){var row=args.rows[r]||[];width=Math.max(width,row.length);
    for(var c=0;c<row.length;c++){var v=row[c];if(v===null||v===undefined)continue;v=String(v);
      sh.GetRangeByNumber(start.r+r,start.c+c).SetValue(v);n++;if(v.charAt(0)==='=')f++;}}
  return {sheet:sh.GetName(),cells:n,formulas:f,start:name(start.r,start.c),end:name(start.r+args.rows.length-1,start.c+Math.max(width,1)-1)};},'cells_set');
)JS";

const char* const kCellsFormatScript = R"JS(
var box=area(args.range);if(!box){reply(false,'bad_address',{range:args.range});return;}
var sh=sheetOf(args.sheet);if(!sh){reply(false,'no_sheet',{sheet:args.sheet});return;}
edit(function(){if(has(args.sheet))sh.SetActive();var g=sh.GetRange(boxName(box));
  if(has(args.bold))g.SetBold(bool(args.bold));
  if(has(args.italic))g.SetItalic(bool(args.italic));
  if(has(args.fontSize))g.SetFontSize(+args.fontSize);
  if(has(args.color)){var c=rgb(args.color);g.SetFontColor(C.CreateColorFromRGB(c[0],c[1],c[2]));}
  if(has(args.fill)){if(args.fill==='none')g.SetFillColor('No Fill');else{var b=rgb(args.fill);g.SetFillColor(C.CreateColorFromRGB(b[0],b[1],b[2]));}}
  if(has(args.align))g.SetAlignHorizontal(args.align);
  if(has(args.numberFormat))g.SetNumberFormat(String(args.numberFormat));
  if(has(args.borders)){var on=bool(args.borders),black=C.CreateColorFromRGB(0,0,0);
    ['Top','Bottom','Left','Right','InsideHorizontal','InsideVertical'].forEach(function(side){g.SetBorders(side,on?'Thin':'None',black);});}
  return {cells:(box.r2-box.r1+1)*(box.c2-box.c1+1),range:name(box.r1,box.c1)+':'+name(box.r2,box.c2)};},'formatted');
)JS";

// Sorting rows by one column (ApiRange.SetSort, the way 数据 > 排序 does); a header row stays on top.
const char* const kCellsSortScript = R"JS(
var sh=sheetOf(args.sheet);if(!sh){reply(false,'no_sheet',{sheet:args.sheet});return;}
var ws=sh.worksheet,box=rangeOf(ws,args.range);if(!box){reply(false,'bad_address',{range:args.range});return;}
var kc=columnIn(ws,box,args.by);if(kc<0){reply(false,'no_column',{by:args.by});return;}
if(kc<box.c1||kc>box.c2){reply(false,'outside_range',{by:args.by,range:boxName(box)});return;}
var header=headerOf(ws,box,args.header),top=box.r1+(header?1:0),desc=args.order==='desc';
if(box.r2-top<1){reply(false,'too_few_rows',{range:boxName(box)});return;}
edit(function(){if(has(args.sheet))sh.SetActive();
  sh.GetRange(boxName(box)).SetSort(name(box.r1,kc),desc?'xlDescending':'xlAscending',null,null,null,null,header?'xlYes':'xlNo');
  var keys=[];for(var r=top;r<=box.r2;r++)keys.push(valueAt(ws,r,kc));
  if(keys.every(numeric))for(var i=1;i<keys.length;i++)if(desc?Number(keys[i])>Number(keys[i-1]):Number(keys[i])<Number(keys[i-1]))return {refused:'not_sorted',range:boxName(box)};
  return {sheet:ws.getName(),range:boxName(box),by:letterOf(kc),order:desc?'desc':'asc',header:header,rows:box.r2-top+1,first:keys.slice(0,5)};},'sorted');
)JS";

// Filtering (ApiRange.SetAutoFilter): the rows that do not fit are hidden, not deleted.
const char* const kCellsFilterScript = R"JS(
var sh=sheetOf(args.sheet);if(!sh){reply(false,'no_sheet',{sheet:args.sheet});return;}
var ws=sh.worksheet,ref=ws.AutoFilter&&ws.AutoFilter.Ref,box=ref?{r1:ref.r1,c1:ref.c1,r2:ref.r2,c2:ref.c2}:rangeOf(ws,args.range);
if(!box){reply(false,'bad_address',{range:args.range});return;}
var kc=columnIn(ws,box,args.column);if(kc<0){reply(false,'no_column',{column:args.column});return;}
if(kc<box.c1||kc>box.c2){reply(false,'outside_range',{column:args.column,range:boxName(box)});return;}
if(box.r2<=box.r1){reply(false,'too_few_rows',{range:boxName(box)});return;}
var field=kc-box.c1+1,values=args.values||[];
edit(function(){if(has(args.sheet))sh.SetActive();var g=sh.GetRange(boxName(box));
  if(values.length)g.SetAutoFilter(field,values.map(String),'xlFilterValues');
  else if(has(args.top))g.SetAutoFilter(field,String(args.top),'xlTop10Items');
  else if(has(args.bottom))g.SetAutoFilter(field,String(args.bottom),'xlBottom10Items');
  else g.SetAutoFilter(field,String(args.condition).trim());
  var now=ws.AutoFilter&&ws.AutoFilter.Ref,hidden=0,rows=0;if(!now)return {refused:'not_filtered'};
  for(var r=now.r1+1;r<=now.r2;r++){rows++;if(ws.getRowHidden(r))hidden++;}
  return {sheet:ws.getName(),range:boxName(now),column:letterOf(kc),rows:rows,shown:rows-hidden,hidden:hidden};},'filtered');
)JS";

const char* const kCellsClearFilterScript = R"JS(
var sh=sheetOf(args.sheet);if(!sh){reply(false,'no_sheet',{sheet:args.sheet});return;}
var ws=sh.worksheet,ref=ws.AutoFilter&&ws.AutoFilter.Ref;if(!ref){reply(false,'no_filter',{sheet:ws.getName()});return;}
var box={r1:ref.r1,c1:ref.c1,r2:ref.r2,c2:ref.c2};
edit(function(){if(has(args.sheet))sh.SetActive();sh.GetRange(boxName(box)).SetAutoFilter();
  var hidden=0;for(var r=box.r1+1;r<=box.r2;r++)if(ws.getRowHidden(r))hidden++;
  return {sheet:ws.getName(),range:boxName(box),hidden:hidden};},'unfiltered');
)JS";

// Removing repeated rows (ApiRange.RemoveDuplicates, as 数据 > 删除重复项 does): the first stays.
const char* const kCellsRemoveDuplicatesScript = R"JS(
var sh=sheetOf(args.sheet);if(!sh){reply(false,'no_sheet',{sheet:args.sheet});return;}
var ws=sh.worksheet,box=rangeOf(ws,args.range);if(!box){reply(false,'bad_address',{range:args.range});return;}
if(box.r2<=box.r1){reply(false,'too_few_rows',{range:boxName(box)});return;}
var columns=null;if(args.columns&&args.columns.length){columns=[];for(var i=0;i<args.columns.length;i++){var c=columnIn(ws,box,args.columns[i]);
  if(c<box.c1||c>box.c2){reply(false,'outside_range',{column:args.columns[i],range:boxName(box)});return;}columns.push(c-box.c1+1);}}
var header=headerOf(ws,box,args.header);
edit(function(){if(has(args.sheet))sh.SetActive();var removed=sh.GetRange(boxName(box)).RemoveDuplicates(columns||undefined,header?'xlYes':'xlNo');
  if(removed===null||removed===undefined)return {refused:'cannot_dedupe',range:boxName(box)};
  if(removed===0)return {code:'no_duplicates',range:boxName(box)};
  return {sheet:ws.getName(),range:boxName(box),removed:removed,header:header};},'deduped');
)JS";

// A chart (ApiWorksheet.AddChart) to the right of what the sheet uses, below the charts already there.
// A single series needs no legend; a pie's legend names its slices.
const char* const kChartAddScript = R"JS(
var sh=sheetOf(args.sheet);if(!sh){reply(false,'no_sheet',{sheet:args.sheet});return;}
var ws=sh.worksheet,areas=String(args.range||'').split(',').map(function(t){return area(t.trim());});
if(!areas.length||areas.some(function(b){return !b;})){reply(false,'bad_address',{range:args.range});return;}
var types={column:'ColumnClustered',bar:'BarClustered',line:'LineMarkers',pie:'Pie',doughnut:'Doughnut',area:'Area',scatter:'XYScatter'};
var kind=has(args.type)?args.type:'column',type=types[kind];if(!type){reply(false,'bad_chart_type',{type:args.type});return;}
var all={r1:1e9,c1:1e9,r2:-1,c2:-1};areas.forEach(function(b){all.r1=Math.min(all.r1,b.r1);all.c1=Math.min(all.c1,b.c1);all.r2=Math.max(all.r2,b.r2);all.c2=Math.max(all.c2,b.c2);});
if(all.r2===all.r1&&all.c2===all.c1){reply(false,'too_few_cells',{range:args.range});return;}
var quote="'"+ws.getName().replace(/'/g,"''")+"'!",abs=function(b){return '$'+letterOf(b.c1)+'$'+(b.r1+1)+':$'+letterOf(b.c2)+'$'+(b.r2+1);};
var ref=areas.map(function(b){return quote+abs(b);}).join(',');
var inRows=areas.length===1&&(all.c2-all.c1)>(all.r2-all.r1);
var used=usedOf(ws),col=used.c2+2,row=all.r1;
for(var i=0;i<ws.Drawings.length;i++){var d=ws.Drawings[i];if(d&&d.from&&d.to&&d.to.col>=col)row=Math.max(row,d.to.row+2);}
edit(function(){if(has(args.sheet))sh.SetActive();
  var chart=sh.AddChart(ref,inRows,type,2,5400000,2880000,col,0,row,0);if(!chart)return {refused:'no_chart'};
  var series=chart.GetAllSeries?chart.GetAllSeries().length:0,round=kind==='pie'||kind==='doughnut';
  // No title given: one series is named by its header ("销售额"), the way a person would title it.
  var title=has(args.title)?String(args.title):'';
  if(!title&&series===1&&!inRows){var last=areas[areas.length-1],head=valueAt(ws,last.r1,last.c2).trim();if(head&&!numeric(head))title=head;}
  if(title)chart.SetTitle(title,28,false);
  chart.SetLegendPos(round?'right':series<=1?'none':'bottom');
  return {sheet:ws.getName(),type:kind,range:areas.map(boxName).join(','),at:name(row,col),series:series,title:title,charts:sh.GetAllCharts().length};},'chart_added');
)JS";

const char* const kSheetAddScript = R"JS(
var title=String(args.name||'').trim();
if(!sheetName(title)){reply(false,'bad_sheet_name',{name:args.name});return;}
if(C.GetSheet(title)){reply(false,'sheet_exists',{name:title});return;}
edit(function(){var list=C.GetSheets();if(list.length)list[list.length-1].SetActive(); // a new sheet goes last
  C.AddSheet(title);var made=C.GetSheet(title);if(!made)return {refused:'not_added',name:title};
  made.SetActive();return {name:title,sheets:C.GetSheets().map(function(s){return s.GetName();})};},'sheet_added');
)JS";

const char* const kSheetRenameScript = R"JS(
var sh=sheetOf(args.sheet);if(!sh){reply(false,'no_sheet',{sheet:args.sheet});return;}
var title=String(args.name||'').trim(),before=sh.GetName();
if(!sheetName(title)){reply(false,'bad_sheet_name',{name:args.name});return;}
if(title===before){reply(false,'unchanged_name',{name:title});return;}
if(C.GetSheet(title)){reply(false,'sheet_exists',{name:title});return;}
edit(function(){sh.SetName(title);if(sh.GetName()!==title)return {refused:'not_renamed',name:title};return {before:before,name:title};},'sheet_renamed');
)JS";

// Presentations, through the editor's presentation builder API (AscBuilder.Slide.Api).
const char* const kSlideHelpers = R"JS(
var S=window.AscBuilder&&AscBuilder.Slide&&AscBuilder.Slide.Api;
if(!S){reply(false,'editor_not_ready',null);return;}
var kindOf=function(shape){try{var ph=shape.GetPlaceholder&&shape.GetPlaceholder();return ph?String(ph.GetType()):'';}catch(x){return '';}};
var isTitle=function(k){return k==='title'||k==='ctrTitle';};
var isBody=function(k){return k==='body'||k==='unknown'||k==='object';};
var linesOf=function(shape){var dc=shape.GetDocContent&&shape.GetDocContent();if(!dc)return [];var out=[];
  for(var i=0;i<dc.GetElementsCount();i++){var e=dc.GetElement(i);if(e&&e.GetText){
    var t=String(e.GetText({Numbering:false})).replace(/[\r\n]+$/,'').replace(/^[\u2022\u00B7\u25AA\u25E6\u25CF\u25A0\u2013\u2014]\s*/,'');
    if(t.trim()!=='')out.push(t);}}return out;};
var setLines=function(shape,lines){var dc=shape.GetDocContent();dc.RemoveAllElements();
  var first=dc.GetElementsCount()>0?dc.GetElement(0):null;
  for(var i=0;i<lines.length;i++){if(i===0&&first){first.AddText(String(lines[0]));continue;}var p=S.CreateParagraph();p.AddText(String(lines[i]));dc.Push(p);}};
var current=function(){try{return api.WordControl.m_oLogicDocument.CurPage;}catch(x){return 0;}};
// A slide's parts: its placeholders; on a slide made of plain text boxes (no title placeholder),
// the first box with text is the title and the second the body, the way people read them.
var partsOf=function(slide){var shapes=slide.GetAllShapes(),parts={title:null,subtitle:null,body:null,others:[]},boxes=[];
  shapes.forEach(function(shape){var k=kindOf(shape);var dc=shape.GetDocContent&&shape.GetDocContent();if(!dc)return;
    if(isTitle(k)&&!parts.title)parts.title=shape;else if(k==='subTitle'&&!parts.subtitle)parts.subtitle=shape;
    else if(isBody(k)&&!parts.body)parts.body=shape;else if(!k)boxes.push(shape);else parts.others.push(shape);});
  if(!parts.title&&!parts.subtitle&&!parts.body){parts.title=boxes.shift()||null;parts.body=boxes.shift()||null;}
  parts.others=parts.others.concat(boxes);return parts;};
)JS";

const char* const kSlidesReadScript = R"JS(
var p=S.GetPresentation(),n=p.GetSlidesCount(),out=[];
for(var i=0;i<n&&i<50;i++){var parts=partsOf(p.GetSlideByIndex(i)),o={slide:i+1,title:'',body:[]},other=[];
  if(parts.title)o.title=linesOf(parts.title).join(' ');
  if(parts.subtitle){var st=linesOf(parts.subtitle);if(st.length)o.subtitle=st.join(' ');}
  if(parts.body)o.body=linesOf(parts.body).slice(0,20);
  parts.others.forEach(function(shape){other=other.concat(linesOf(shape));});
  if(other.length)o.other=other.slice(0,20);out.push(o);}
reply(true,'slides',{count:n,current:current()+1,slides:out,truncated:n>50});
)JS";

const char* const kSlideAddScript = R"JS(
edit(function(){var p=S.GetPresentation(),m=p.GetMaster(0),points=args.points||[],want=points.length?'content':has(args.subtitle)?'cover':'title';
  var layout=null,layoutName='';
  for(var i=0;m&&i<m.GetLayoutsCount()&&!layout;i++){var l=m.GetLayout(i),kinds=l.GetAllShapes().map(kindOf);
    var titles=kinds.filter(isTitle).length,bodies=kinds.filter(isBody).length,subs=kinds.filter(function(k){return k==='subTitle';}).length,pics=kinds.filter(function(k){return k==='picture';}).length;
    if((want==='content'&&kinds.indexOf('title')>=0&&bodies===1&&!pics)||(want==='cover'&&kinds.indexOf('ctrTitle')>=0&&subs===1)
       ||(want==='title'&&kinds.indexOf('title')>=0&&!bodies&&!subs&&!pics)){layout=l;layoutName=l.GetName?l.GetName():'';}}
  var slide=S.CreateSlide();p.AddSlide(slide);if(layout)slide.ApplyLayout(layout);
  var index=p.GetSlidesCount()-1;
  if(args.position!=='end'){var at=Math.min(current()+1,index);if(at<index&&slide.MoveTo)slide.MoveTo(at);index=at;}
  var shapes=slide.GetAllShapes(),title=null,body=null,sub=null;
  shapes.forEach(function(shape){var k=kindOf(shape);if(isTitle(k)&&!title)title=shape;else if(isBody(k)&&!body)body=shape;else if(k==='subTitle'&&!sub)sub=shape;});
  var W=p.GetWidth(),H=p.GetHeight();
  var box=function(lines,y,h,size,bold){var s=S.CreateShape('rect',W-W/6,h,S.CreateNoFill(),S.CreateStroke(0,S.CreateNoFill()));
    s.SetPosition(W/12,y);var dc=s.GetDocContent();dc.RemoveAllElements();var first=dc.GetElementsCount()>0?dc.GetElement(0):null;
    for(var i=0;i<lines.length;i++){var run=S.CreateRun();run.AddText(String(lines[i]));run.SetFontSize(size*2);if(bold)run.SetBold(true);
      if(i===0&&first){first.AddElement(run);continue;}var para=S.CreateParagraph();para.AddElement(run);dc.Push(para);}
    slide.AddObject(s);return s;};
  if(title)setLines(title,[args.title]);else box([args.title],H/12,H/6,32,true);
  if(points.length){if(body)setLines(body,points);else box(points,H/4,H*2/3,20,false);}
  if(has(args.subtitle)){if(sub)setLines(sub,[args.subtitle]);else if(body&&!points.length)setLines(body,[args.subtitle]);else box([args.subtitle],H/2,H/6,20,false);}
  return {slide:index+1,layout:layoutName,points:points.length,count:p.GetSlidesCount()};},'slide_added');
)JS";

const char* const kSlideSetTextScript = R"JS(
edit(function(){var p=S.GetPresentation(),slide=p.GetSlideByIndex(+args.slide-1);if(!slide)return {refused:'no_slide',slide:+args.slide};
  var target=partsOf(slide)[args.part];
  if(!target)return {refused:'no_placeholder',slide:+args.slide,part:args.part};
  var lines=args.points?args.points:String(args.text).replace(/\r\n?/g,'\n').split('\n');setLines(target,lines);
  return {slide:+args.slide,part:args.part,lines:lines.length};},'slide_text_set');
)JS";

QString officeScript(const char* helpers, const char* script, const QJsonObject& arguments)
{
    return QStringLiteral("var args=") + QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))
        + QLatin1Char(';') + QString::fromUtf8(kEditHelpers) + QString::fromUtf8(helpers) + QString::fromUtf8(script);
}

QString structureScript(const char* script, const QJsonObject& arguments)
{
    return officeScript(kStructureHelpers, script, arguments);
}

// The editor answers through AscDesktopEditor.execCommand("openoffice:command"),
// which reaches the host as an EXECUTE_COMMAND event of the same view. The body
// uses the editor's own exported API (paste, selected text, undo/redo).
QString editorCommandScript(quint64 id, const QString& name, const QJsonObject& arguments)
{
    QString body;
    if (name == QStringLiteral("text.insert")) {
        // Plain text at the cursor uses the editor's text paste. HTML (converted
        // Markdown) and "after" use the editor's HTML paste; "after" moves to the
        // end of the current paragraph (or of the selection's last paragraph)
        // and starts a new one, unless the cursor already sits in an empty one.
        body = QStringLiteral("var t=%1,h=%2,pos=%3;"
            "var done=function(){reply(true,'inserted',{length:t.length});};"
            "if(pos==='end'){if(!logic){reply(false,'editor_not_ready',null);return;}"
            "logic.RemoveSelection();logic.MoveCursorToEndPos(false);pos='after';}"
            "if(pos==='after'){if(!logic||!B){reply(false,'editor_not_ready',null);return;}"
            "var empty=false;if(!logic.IsSelectionUse()){var cp=logic.GetCurrentParagraph();var ap=cp&&B.GetByInternalId(cp.GetId());"
            "empty=!!ap&&ap.GetText().replace(/[\\r\\n]/g,'')==='';}"
            "if(!h)h=paras(t);if(!empty){if(!afterParagraph()){reply(false,'no_position',null);return;}h='<p></p>'+h;}}"
            "if(h){pasteHtml(h,{},done);return;}"
            "api.asc_PasteData(1,t,undefined,undefined,undefined,done,undefined,"
            "function(){reply(false,'selection_locked',null);});")
                   .arg(jsValue(arguments, "text"), jsValue(arguments, "html"), jsValue(arguments, "position"));
    } else if (name == QStringLiteral("selection.getText")) {
        body = QStringLiteral("var s=api.asc_GetSelectedText(false,{Numbering:true});"
                              "s=(s===undefined||s===null)?'':String(s);var n=0;"
                              "try{if(s&&logic&&B&&logic.IsSelectionUse()){var r=B.GetDocument().GetRangeBySelect();"
                              "var ps=r&&r.GetAllParagraphs();n=ps?ps.length:0;}}catch(e){}"
                              "reply(true,'ok',{text:s.slice(0,20000),length:s.length,truncated:s.length>20000,paragraphs:n});");
    } else if (name == QStringLiteral("selection.replace")) {
        // The editor's own "smart" replacement keeps each paragraph's and run's
        // formatting; it needs at least as many new paragraphs as are selected,
        // otherwise the HTML paste (merged into the last paragraph) is used.
        body = QStringLiteral("var t=%1,expected=%2;if(!logic||!B){reply(false,'editor_not_ready',null);return;}"
            "if(!logic.IsSelectionUse()){reply(false,'no_selection',null);return;}"
            "var norm=function(x){return String(x||'').replace(/\\s+/g,'');};"
            "if(expected!==null&&norm(api.asc_GetSelectedText(false,{}))!==norm(expected)){reply(false,'selection_changed',null);return;}"
            "var r=B.GetDocument().GetRangeBySelect();var ps=r&&r.GetAllParagraphs();var n=ps&&ps.length?ps.length:1;"
            "var parts=t.replace(/\\r\\n?/g,'\\n').split('\\n').filter(function(l){return l.trim()!=='';});"
            "if(!parts.length){reply(false,'invalid_argument',null);return;}"
            "var ok=function(){reply(true,'replaced_selection',{length:t.length,paragraphs:parts.length});};"
            "var plugins=window.g_asc_plugins;"
            "if(parts.length<n||!plugins||!api.pluginMethod_ReplaceTextSmart){pasteHtml(paras(parts.join('\\n')),{MergeLast:true},ok);return;}"
            "var old=plugins.onPluginMethodReturn,finished=false;"
            "var finish=function(good){if(finished)return;finished=true;plugins.onPluginMethodReturn=old;"
            "if(good)ok();else reply(false,'replace_timeout',null);};"
            "plugins.onPluginMethodReturn=function(){finish(true);};"
            "setTimeout(function(){finish(false);},12000);"
            "api.pluginMethod_ReplaceTextSmart(parts,'\\t','\\r\\n');")
                   .arg(jsValue(arguments, "text"), jsValue(arguments, "expected"));
    } else if (name == QStringLiteral("document.getText")) {
        // Top-level paragraphs and tables, without touching the selection:
        // headings keep a Markdown marker, list numbers stay, table rows become
        // "| a | b |" lines.
        body = QStringLiteral("if(!B){reply(false,'editor_not_ready',null);return;}"
            "var d=B.GetDocument(),out=[],n=d.GetElementsCount();"
            "for(var i=0;i<n;i++){var e=d.GetElement(i),k=e.GetClassType();"
            "if(k==='paragraph'){var st=e.GetStyle(),nm=st?st.GetName():'';var m=/^(?:heading|\\u6807\\u9898)\\s*(\\d)/i.exec(nm);"
            "var x=e.GetText({Numbering:true}).replace(/[\\r\\n]+$/,'');out.push((m?new Array(+m[1]+1).join('#')+' ':'')+x);}"
            "else if(k==='table'){for(var r=0;r<e.GetRowsCount();r++){var row=e.GetRow(r),cells=[];"
            "for(var c=0;c<row.GetCellsCount();c++)cells.push(row.GetCell(c).GetContent().GetText({}).replace(/[\\r\\n]+/g,' ').trim());"
            "out.push('| '+cells.join(' | ')+' |');}}"
            "else{try{out.push(e.GetContent().GetText({}).replace(/[\\r\\n]+$/,''));}catch(x){}}}"
            "var text=out.join('\\n');"
            "reply(true,'read',{text:text.slice(0,40000),length:text.length,truncated:text.length>40000,blocks:n});");
    } else if (name == QStringLiteral("document.replaceText")) {
        body = QStringLiteral("var f=%1,rp=%2;if(!logic){reply(false,'editor_not_ready',null);return;}"
            "var clear=function(){try{if(logic.ClearSearch)logic.ClearSearch();}catch(e){}};"
            "var props=new AscCommon.CSearchSettings();props.SetText(f);props.SetMatchCase(true);"
            "var engine=logic.Search(props);var n=engine?engine.Count:0;"
            "if(!n){clear();reply(false,'not_found',{find:f});return;}"
            "logic.ReplaceSearchElement(rp,true,null,false);clear();reply(true,'replaced',{count:n});")
                   .arg(jsValue(arguments, "find"), jsValue(arguments, "replace"));
    } else if (name == QStringLiteral("document.outline")) {
        body = structureScript(kOutlineScript, arguments);
    } else if (name == QStringLiteral("sheet.read")) {
        body = officeScript(kSheetHelpers, kSheetReadScript, arguments);
    } else if (name == QStringLiteral("cells.set")) {
        body = officeScript(kSheetHelpers, kCellsSetScript, arguments);
    } else if (name == QStringLiteral("cells.format")) {
        body = officeScript(kSheetHelpers, kCellsFormatScript, arguments);
    } else if (name == QStringLiteral("cells.sort")) {
        body = officeScript(kSheetHelpers, kCellsSortScript, arguments);
    } else if (name == QStringLiteral("cells.filter")) {
        body = officeScript(kSheetHelpers, kCellsFilterScript, arguments);
    } else if (name == QStringLiteral("cells.clearFilter")) {
        body = officeScript(kSheetHelpers, kCellsClearFilterScript, arguments);
    } else if (name == QStringLiteral("cells.removeDuplicates")) {
        body = officeScript(kSheetHelpers, kCellsRemoveDuplicatesScript, arguments);
    } else if (name == QStringLiteral("chart.add")) {
        body = officeScript(kSheetHelpers, kChartAddScript, arguments);
    } else if (name == QStringLiteral("sheet.add")) {
        body = officeScript(kSheetHelpers, kSheetAddScript, arguments);
    } else if (name == QStringLiteral("sheet.rename")) {
        body = officeScript(kSheetHelpers, kSheetRenameScript, arguments);
    } else if (name == QStringLiteral("slides.read")) {
        body = officeScript(kSlideHelpers, kSlidesReadScript, arguments);
    } else if (name == QStringLiteral("slide.add")) {
        body = officeScript(kSlideHelpers, kSlideAddScript, arguments);
    } else if (name == QStringLiteral("slide.setText")) {
        body = officeScript(kSlideHelpers, kSlideSetTextScript, arguments);
    } else if (name == QStringLiteral("table.read")) {
        body = structureScript(kTableReadScript, arguments);
    } else if (name == QStringLiteral("table.setCell")) {
        body = structureScript(kSetCellScript, arguments);
    } else if (name == QStringLiteral("format.cells")) {
        body = structureScript(kFormatCellsScript, arguments);
    } else if (name == QStringLiteral("format.paragraphs")) {
        body = structureScript(kFormatParagraphsScript, arguments);
    } else if (name == QStringLiteral("format.text")) {
        body = structureScript(kFormatTextScript, arguments);
    } else if (name == QStringLiteral("format.read")) {
        body = structureScript(kFormatReadScript, arguments);
    } else if (name == QStringLiteral("format.selection")) {
        body = structureScript(kFormatSelectionScript, arguments);
    } else if (name == QStringLiteral("selection.select")) {
        body = structureScript(kSelectScript, arguments);
    } else if (name == QStringLiteral("edit.undo")) {
        body = QStringLiteral("if(api.asc_getCanUndo&&!api.asc_getCanUndo()){reply(false,'nothing_to_undo',null);return;}"
                              "api.Undo();reply(true,'undone',null);");
    } else if (name == QStringLiteral("edit.redo")) {
        body = QStringLiteral("if(api.asc_getCanRedo&&!api.asc_getCanRedo()){reply(false,'nothing_to_redo',null);return;}"
                              "api.Redo();reply(true,'redone',null);");
    }
    return QStringLiteral("(function(){var id=%1;"
        "function reply(ok,code,value){try{window.AscDesktopEditor.execCommand('openoffice:command',"
        "JSON.stringify({id:id,ok:ok,code:code,value:value}));}catch(e){}}"
        "try{var api=(window.Asc&&window.Asc.editor)||window.editor;"
        "if(!api){reply(false,'editor_not_ready',null);return;}"
        "var B=window.AscBuilder&&AscBuilder.Word&&AscBuilder.Word.Api;"
        "var logic=api.WordControl&&api.WordControl.m_oLogicDocument;"
        "var esc=function(x){return x.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');};"
        "var paras=function(x){return x.replace(/\\r\\n?/g,'\\n').split('\\n').filter(function(l){return l.trim()!=='';})"
        ".map(function(l){return '<p>'+esc(l)+'</p>';}).join('');};"
        "var pasteHtml=function(h,o,cb){api._pluginMethod_PasteHtml(h,cb,o);};"
        "var afterParagraph=function(){var p=null;"
        "if(logic.IsSelectionUse()){var r=B.GetDocument().GetRangeBySelect();var ps=r&&r.GetAllParagraphs();if(ps&&ps.length)p=ps[ps.length-1];}"
        "else{var c=logic.GetCurrentParagraph();if(c)p=B.GetByInternalId(c.GetId());}"
        "if(!p)return false;p.Select();logic.MoveCursorRight(false,false);return true;};"
        "%2"
        "}catch(e){reply(false,'script_error',String((e&&e.message)||e).slice(0,300));}})();")
        .arg(jsString(QString::number(id)), body);
}

// The editor's own theme machinery (web-apps Common.UI.Themes): going dark
// remembers the light theme in use and picks its dark partner (the modern
// "White" theme pairs with "Night", the others with "Dark"), then turns on the
// editor's dark document mode; going light restores the remembered theme.
// Display only; the document file is unaffected.
QString appearanceScript(bool dark)
{
    if (dark)
        return QStringLiteral("(function(){try{var t=window.Common&&Common.UI&&Common.UI.Themes;if(!t)return;"
            "var current=t.currentThemeId();if(!t.isDarkTheme(current))window.__openOfficeLightTheme=current;"
            "var light=window.__openOfficeLightTheme||current;"
            "t.setTheme(/white/.test(light)?'theme-night':'theme-dark','native');"
            "t.setContentTheme('dark',true);}catch(e){}})();");
    return QStringLiteral("(function(){try{var t=window.Common&&Common.UI&&Common.UI.Themes;if(!t)return;"
        "if(t.isDarkTheme(t.currentThemeId()))t.setTheme(window.__openOfficeLightTheme||'theme-light','native');"
        "}catch(e){}})();");
}

QString commandMessage(const QString& code, const QJsonObject& value, const QString& detail)
{
    if (code == QStringLiteral("inserted"))
        return QStringLiteral("已插入 %1 个字符。").arg(value.value(QStringLiteral("length")).toInt());
    if (code == QStringLiteral("ok")) {
        const int length = value.value(QStringLiteral("length")).toInt();
        return length ? QStringLiteral("已读取选中文字（%1 个字符）。").arg(length)
                      : QStringLiteral("当前没有选中文字。");
    }
    if (code == QStringLiteral("undone")) return QStringLiteral("已撤销上一步。");
    if (code == QStringLiteral("redone")) return QStringLiteral("已重做上一步。");
    if (code == QStringLiteral("nothing_to_undo")) return QStringLiteral("没有可撤销的操作。");
    if (code == QStringLiteral("nothing_to_redo")) return QStringLiteral("没有可重做的操作。");
    if (code == QStringLiteral("selection_locked")) return QStringLiteral("当前位置不能插入文字。");
    if (code == QStringLiteral("read")) {
        const int length = value.value(QStringLiteral("length")).toInt();
        return value.value(QStringLiteral("truncated")).toBool()
            ? QStringLiteral("已读取全文（共 %1 个字符，只取了前 40000 个）。").arg(length)
            : QStringLiteral("已读取全文（%1 个字符）。").arg(length);
    }
    if (code == QStringLiteral("replaced_selection")) return QStringLiteral("已替换选中的文字，可以撤销。");
    if (code == QStringLiteral("replaced"))
        return QStringLiteral("已替换 %1 处，可以撤销。").arg(value.value(QStringLiteral("count")).toInt());
    if (code == QStringLiteral("not_found")) return QStringLiteral("文档里没有找到这段文字，可能已经改过了。");
    if (code == QStringLiteral("no_selection")) return QStringLiteral("请先在文档里选中要修改的文字。");
    if (code == QStringLiteral("selection_changed"))
        return QStringLiteral("选中的内容和原来不一样了。请重新选中原文再试，或用“插到后面”。");
    if (code == QStringLiteral("replace_timeout")) return QStringLiteral("编辑器没有及时完成替换，请检查文档后再试。");
    if (code == QStringLiteral("no_position")) return QStringLiteral("找不到可以插入的位置。");
    if (code == QStringLiteral("invalid_argument")) return QStringLiteral("命令参数无效。");
    if (code == QStringLiteral("format_read"))
        return QStringLiteral("找到 %1 处，已读取格式。").arg(value.value(QStringLiteral("count")).toInt());
    if (code == QStringLiteral("outline"))
        return QStringLiteral("已读取文档结构：%1 段、%2 张表格。")
            .arg(value.value(QStringLiteral("paragraphs")).toInt()).arg(value.value(QStringLiteral("tables")).toInt());
    if (code == QStringLiteral("table"))
        return QStringLiteral("已读取第 %1 张表格（%2 行 %3 列）。").arg(value.value(QStringLiteral("table")).toInt())
            .arg(value.value(QStringLiteral("rowsCount")).toInt()).arg(value.value(QStringLiteral("columns")).toInt());
    if (code == QStringLiteral("cell_set"))
        return QStringLiteral("已修改第 %1 张表格第 %2 行第 %3 列，可以撤销。").arg(value.value(QStringLiteral("table")).toInt())
            .arg(value.value(QStringLiteral("row")).toInt()).arg(value.value(QStringLiteral("column")).toInt());
    if (code == QStringLiteral("unchanged")) return QStringLiteral("这一格本来就是这些文字，没有改动。");
    if (code == QStringLiteral("formatted")) {
        if (value.contains(QStringLiteral("cells")))
            return QStringLiteral("已设置 %1 个单元格的格式，可以撤销。").arg(value.value(QStringLiteral("cells")).toInt());
        if (value.contains(QStringLiteral("paragraphs")))
            return QStringLiteral("已设置 %1 段的格式，可以撤销。").arg(value.value(QStringLiteral("paragraphs")).toInt());
        if (value.contains(QStringLiteral("selection"))) return QStringLiteral("已设置选中文字的格式，可以撤销。");
        return QStringLiteral("已设置 %1 处文字的格式，可以撤销。").arg(value.value(QStringLiteral("count")).toInt());
    }
    if (code == QStringLiteral("selected")) return QStringLiteral("已选中。");
    if (code == QStringLiteral("sheet"))
        return QStringLiteral("已读取工作表“%1”（%2）。").arg(value.value(QStringLiteral("sheet")).toString(),
                                                         value.value(QStringLiteral("range")).toString());
    if (code == QStringLiteral("cells_set")) {
        const int formulas = value.value(QStringLiteral("formulas")).toInt();
        return QStringLiteral("已写入 %1–%2 的 %3 格%4，可以撤销。").arg(value.value(QStringLiteral("start")).toString(),
                                                                    value.value(QStringLiteral("end")).toString())
            .arg(value.value(QStringLiteral("cells")).toInt())
            .arg(formulas ? QStringLiteral("（含 %1 个公式）").arg(formulas) : QString());
    }
    if (code == QStringLiteral("slides"))
        return QStringLiteral("已读取 %1 页幻灯片（现在在第 %2 页）。").arg(value.value(QStringLiteral("count")).toInt())
            .arg(value.value(QStringLiteral("current")).toInt());
    if (code == QStringLiteral("slide_added"))
        return QStringLiteral("已加了一页幻灯片（第 %1 页%2），可以撤销。").arg(value.value(QStringLiteral("slide")).toInt())
            .arg(value.value(QStringLiteral("layout")).toString().isEmpty() ? QString()
                 : QStringLiteral("，版式“%1”").arg(value.value(QStringLiteral("layout")).toString()));
    if (code == QStringLiteral("slide_text_set"))
        return QStringLiteral("已改第 %1 页的文字，可以撤销。").arg(value.value(QStringLiteral("slide")).toInt());
    if (code == QStringLiteral("sorted"))
        return QStringLiteral("已把 %1 按 %2 列%3排好（%4 行），可以撤销。").arg(value.value(QStringLiteral("range")).toString(),
                                                                      value.value(QStringLiteral("by")).toString(),
                                                                      value.value(QStringLiteral("order")).toString() == QStringLiteral("desc")
                                                                          ? QStringLiteral("从大到小") : QStringLiteral("从小到大"))
            .arg(value.value(QStringLiteral("rows")).toInt());
    if (code == QStringLiteral("filtered"))
        return QStringLiteral("已筛选：显示 %1 行，隐藏 %2 行（没有删除），可以撤销。").arg(value.value(QStringLiteral("shown")).toInt())
            .arg(value.value(QStringLiteral("hidden")).toInt());
    if (code == QStringLiteral("unfiltered")) return QStringLiteral("已取消筛选，所有行都显示出来了，可以撤销。");
    if (code == QStringLiteral("no_filter")) return QStringLiteral("这个工作表没有筛选。");
    if (code == QStringLiteral("deduped"))
        return QStringLiteral("已删除 %1 行重复的（每组留第一行），可以撤销。").arg(value.value(QStringLiteral("removed")).toInt());
    if (code == QStringLiteral("no_duplicates")) return QStringLiteral("没有重复的行，没有改动。");
    if (code == QStringLiteral("chart_added")) {
        static const QHash<QString, QString> kinds{{QStringLiteral("column"), QStringLiteral("柱形图")}, {QStringLiteral("bar"), QStringLiteral("条形图")},
                                                   {QStringLiteral("line"), QStringLiteral("折线图")}, {QStringLiteral("pie"), QStringLiteral("饼图")},
                                                   {QStringLiteral("doughnut"), QStringLiteral("圆环图")}, {QStringLiteral("area"), QStringLiteral("面积图")},
                                                   {QStringLiteral("scatter"), QStringLiteral("散点图")}};
        return QStringLiteral("已在 %1 插入一张%2，可以撤销。").arg(value.value(QStringLiteral("at")).toString(),
                                                              kinds.value(value.value(QStringLiteral("type")).toString(), QStringLiteral("图表")));
    }
    if (code == QStringLiteral("sheet_added"))
        return QStringLiteral("已新建工作表“%1”，可以撤销。").arg(value.value(QStringLiteral("name")).toString());
    if (code == QStringLiteral("sheet_renamed"))
        return QStringLiteral("已把工作表“%1”改名为“%2”，可以撤销。").arg(value.value(QStringLiteral("before")).toString(),
                                                                    value.value(QStringLiteral("name")).toString());
    if (code == QStringLiteral("bad_formula")) {
        static const QHash<QString, QString> reasons{
            {QStringLiteral("reference"), QStringLiteral("引用了不存在的工作表或单元格")},
            {QStringLiteral("parentheses"), QStringLiteral("括号不配对")},
            {QStringLiteral("operator"), QStringLiteral("运算符写错了")},
            {QStringLiteral("operand"), QStringLiteral("运算符后面缺少数或单元格")},
            {QStringLiteral("arguments"), QStringLiteral("函数的参数个数不对")}};
        const QString why = value.value(QStringLiteral("why")).toString();
        const QString reason = why == QStringLiteral("function")
            ? QStringLiteral("没有“%1”这个函数，函数名要用英文，例如 SUM、AVERAGE").arg(value.value(QStringLiteral("function")).toString())
            : reasons.value(why, QStringLiteral("写法不对"));
        return QStringLiteral("公式写得不对，一格也没有写：%1 的 %2（%3）。").arg(value.value(QStringLiteral("cell")).toString(),
                                                                         value.value(QStringLiteral("formula")).toString(), reason);
    }
    if (code == QStringLiteral("no_sheet")) return QStringLiteral("没有这个工作表。");
    if (code == QStringLiteral("bad_sheet_name"))
        return QStringLiteral("工作表名称要有 1 到 31 个字，不能有 [ ] : * ? / \\ 这些符号。");
    if (code == QStringLiteral("sheet_exists")) return QStringLiteral("已经有这个名称的工作表了。");
    if (code == QStringLiteral("unchanged_name")) return QStringLiteral("工作表本来就叫这个名称，没有改动。");
    if (code == QStringLiteral("outside_range")) return QStringLiteral("这一列不在要处理的范围里。");
    if (code == QStringLiteral("no_column")) return QStringLiteral("表头里没有这一列，请写列字母（例如 C）或表头上的名字。");
    if (code == QStringLiteral("too_few_rows")) return QStringLiteral("范围里的行太少，没法这样做。");
    if (code == QStringLiteral("not_sorted")) return QStringLiteral("没能排序（范围里可能有合并的单元格）。");
    if (code == QStringLiteral("not_filtered")) return QStringLiteral("没能筛选。");
    if (code == QStringLiteral("cannot_dedupe")) return QStringLiteral("没能删除重复的行（范围里可能有合并的单元格或表格）。");
    if (code == QStringLiteral("bad_chart_type")) return QStringLiteral("不认识这种图表。");
    if (code == QStringLiteral("too_few_cells")) return QStringLiteral("做图表至少要两格以上的数据。");
    if (code == QStringLiteral("no_chart")) return QStringLiteral("没能插入图表（数据可能不合适）。");
    if (code == QStringLiteral("not_added")) return QStringLiteral("没能新建工作表。");
    if (code == QStringLiteral("not_renamed")) return QStringLiteral("没能给工作表改名。");
    if (code == QStringLiteral("bad_address")) return QStringLiteral("单元格地址写得不对。");
    if (code == QStringLiteral("no_slide")) return QStringLiteral("没有这一页幻灯片。");
    if (code == QStringLiteral("no_placeholder")) return QStringLiteral("这一页没有这个位置（标题、副标题或正文）。");
    if (code == QStringLiteral("no_table")) return QStringLiteral("文档里没有这张表格。");
    if (code == QStringLiteral("no_cell")) return QStringLiteral("表格里没有这一格。");
    if (code == QStringLiteral("no_paragraph")) return QStringLiteral("文档里没有这一段。");
    if (code == QStringLiteral("bad_range")) return QStringLiteral("行、列或段落的范围写得不对。");
    if (code == QStringLiteral("no_style")) return QStringLiteral("文档里没有这个样式。");
    if (code == QStringLiteral("editor_locked")) return QStringLiteral("文档现在不能修改（可能正被锁定），请稍后再试。");
    if (code == QStringLiteral("edit_failed")) return QStringLiteral("编辑器修改时出错：") + detail.left(200);
    if (code == QStringLiteral("editor_not_ready")) return QStringLiteral("编辑器尚未就绪。");
    return QStringLiteral("编辑器执行命令时出错：") + detail.left(300);
}

// QCefView does not clear its protected native pointer in _closedSlot().
// Detach on the Qt thread; OnRelease/_closed happens before browser destruction.
class EditorHost final : public QCefView {
public:
    explicit EditorHost(QWidget* parent) : QCefView(parent)
    {
        QObject::connect(this, &QCefView::_closed, this,
                         [this] { detachNativePointer(); }, Qt::QueuedConnection);
        QObject::connect(this, &QCefView::_loaded, this,
                         [this] { UpdateSize(); }, Qt::QueuedConnection);
    }
    void detachNativePointer()
    {
        m_pCefView = nullptr;
        setAcceptDrops(false);
        setEnabled(false);
    }
    void hideNativeBrowser()
    {
#ifdef Q_OS_WIN
        // Upstream DoClose() reparents the browser window to the desktop before
        // closing it. Hide it first so a retired document cannot flash on top.
        if (!cef_handle) return;
        if (const HWND child = ::GetWindow(reinterpret_cast<HWND>(cef_handle), GW_CHILD))
            ::ShowWindow(child, SW_HIDE);
#endif
    }
    void UpdateSize() override
    {
#ifdef Q_OS_WIN
        if (!m_pCefView || !cef_handle) return;
        const HWND parent = reinterpret_cast<HWND>(cef_handle);
        RECT client = {};
        if (!::GetClientRect(parent, &client)) return;
        // Qt6 widget sizes are logical pixels. The upstream Windows wrapper
        // passes them straight to SetWindowPos, making the CEF child 2/3 size
        // at 150% scaling. Use the native parent's client pixels directly;
        // do not multiply an already scaled native size by Qt's DPR again.
        cef_width = static_cast<unsigned int>(client.right - client.left);
        cef_height = static_cast<unsigned int>(client.bottom - client.top);
        if (const HWND child = ::GetWindow(parent, GW_CHILD))
            ::SetWindowPos(child, nullptr, 0, 0, static_cast<int>(cef_width),
                           static_cast<int>(cef_height), SWP_NOZORDER | SWP_NOACTIVATE);
#else
        QCefView::UpdateSize();
#endif
    }
protected:
    bool event(QEvent* event) override
    {
        const bool handled = QCefView::event(event);
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        if (event->type() == QEvent::DevicePixelRatioChange) UpdateSize();
#endif
        return handled;
    }
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (!m_pCefView) { event->ignore(); return; }
        QCefView::dragEnterEvent(event);
    }
    void dropEvent(QDropEvent* event) override
    {
        if (!m_pCefView) { event->ignore(); return; }
        QCefView::dropEvent(event);
    }
};

class ApplicationManager final : public QAscApplicationManager {
public:
    bool externalLoopInitialized = false;
    bool IsExternalEventLoop() override
    {
        // Upstream's method creates/binds the pump; it is NOT a read-only
        // getter. Record the one call made by Init_CEF instead of calling it
        // again after initialization and accidentally replacing the pump.
        externalLoopInitialized = QAscApplicationManager::IsExternalEventLoop();
        return externalLoopInitialized;
    }
};

struct EventSnapshot {
    int type = -1;
    int viewId = -1;
    bool flag = false;
    QString command;
    QString detail;
    QString path;
    int messageType = 0;
    std::vector<std::wstring> arguments;
    std::vector<int> formats;
};

// Used only if teardown is requested while a browser is still alive. It has
// process lifetime and cannot call back into a destroyed adapter.
CAscCefMenuEventListener& droppedEventSink()
{
    static CAscCefMenuEventListener sink;
    return sink;
}
} // namespace

struct OnlyOfficeEngineAdapter::Native final : CAscCefMenuEventListener {
    explicit Native(OnlyOfficeEngineAdapter* adapter) : owner(adapter)
    {
        commandTimer = new QTimer(owner);
        commandTimer->setSingleShot(true);
        QObject::connect(commandTimer, &QTimer::timeout, owner, [this] {
            // Not a session fault: commands are tagged, a late reply is ignored.
            finishCommand(refused("editor_command_timeout",
                QStringLiteral("编辑器没有在 15 秒内回报结果，无法确认命令是否已执行。")));
        });
        timer = new QTimer(owner);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, owner, [this] {
            log("operation_timeout");
            if (pending.active && incomingViewId >= 0) {
                // The replacement view is closed by its own ID; late events
                // from it are ignored and the current document stays usable.
                abandonIncoming(refused("native_timeout", QStringLiteral("文档加载超时；已保留当前文档。")));
                return;
            }
            faulted = true; // Late untagged native callbacks cannot service a new request.
            finish(refused("native_timeout", QStringLiteral("引擎操作超时；会话已停止接受新的打开和保存请求。")));
            emit owner->capabilitiesChanged();
        });
    }

    OnlyOfficeEngineAdapter* owner;
    std::mutex callbackMutex;
    bool acceptsCallbacks = true;
    std::unique_ptr<ApplicationManager> manager;
    std::unique_ptr<CApplicationCEF> application;
    // The UI receives one container. Each document has its own CEF view: a
    // replacement loads beside the current view, and the old view is destroyed
    // only after the new document is confirmed. A replacement therefore never
    // closes the last browser, whose closure ends the native message loop.
    QPointer<QWidget> container;
    QStackedLayout* stack = nullptr; // Owned by container.
    QPointer<EditorHost> host;       // Current document view.
    int viewId = -1;
    QPointer<EditorHost> incomingHost; // Replacement view while Open/Create loads.
    int incomingViewId = -1;
    bool incomingModified = false;
    QHash<int, QPointer<EditorHost>> retiring; // Destroyed views awaiting DESTROYWINDOW.
    QTimer* timer = nullptr;
    QFile eventLog;
    std::vector<QByteArray> argumentStorage;
    std::vector<char*> arguments;
    QSet<quint64> acceptedIds;
    bool initialized = false;
    bool subprocess = false;
    int subprocessExitCode = 0;
    bool loopRunning = false;
    bool loopReturned = false;
    bool faulted = false;
    bool ready = false;
    bool modified = false;
    bool loadRequested = false; // The current view was asked to load a document.
    bool destroyRequested = false;
    bool closed = false;
    QString currentPath; // Empty for a new document that was never saved.
    DocumentKind currentKind = DocumentKind::Unknown;
    QString lastNativeError;
    int dialogViewId = -1; // Editor-initiated save dialog waiting for the UI.
    bool darkAppearance = false;  // the window's appearance, applied to each loaded view
    bool appearanceChanged = false; // light views need a script only after a dark phase
    struct CommandPending {
        bool active = false;
        quint64 id = 0;
        QString name;
    } command; // One editor command at a time, separate from document operations.
    QTimer* commandTimer = nullptr;
    struct Pending {
        bool active = false;
        quint64 id = 0;
        EngineOperation operation = EngineOperation::Open;
        QString path;
        QString confirmedSavePath;
        bool discard = false;
        DocumentKind kind = DocumentKind::Unknown;
    } pending;

    bool onOwnerThread() const { return QThread::currentThread() == owner->thread(); }
    void stopCallbacks()
    {
        // Synchronize with an already-entered callback before QObject teardown.
        std::lock_guard<std::mutex> lock(callbackMutex);
        acceptsCallbacks = false;
    }
    static CCefViewEditor* editorOf(EditorHost* view)
    {
        return view && view->GetCefView()
            ? static_cast<CCefViewEditor*>(view->GetCefView()) : nullptr;
    }
    CCefViewEditor* editor() const { return editorOf(host); }
    bool currentModified() const { return modified || (editor() && editor()->GetModified()); }

    void log(const char* action, int eventType = -1, int eventView = -1)
    {
        if (!eventLog.isOpen()) return;
        // Event metadata only: no document text, renderer parameters or file paths.
        const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
            + " action=" + action + " event=" + QByteArray::number(eventType)
            + " view=" + QByteArray::number(eventView)
            + " request=" + QByteArray::number(pending.active ? pending.id : 0) + '\n';
        eventLog.write(line);
        eventLog.flush();
    }

    void logCommand(const char* action, quint64 id)
    {
        if (!eventLog.isOpen()) return;
        // Command name and id only: never the inserted or selected text.
        eventLog.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
            + " action=" + action + " command=" + command.name.toLatin1()
            + " command_request=" + QByteArray::number(id) + '\n');
        eventLog.flush();
    }

    void finishCommand(EngineResult result, QJsonObject value = {})
    {
        if (!command.active) return;
        commandTimer->stop();
        const CommandPending completed = command;
        logCommand(result.ok ? "editor_command_completed" : "editor_command_failed", completed.id);
        command = {};
        emit owner->editorCommandFinished(completed.id, std::move(result), std::move(value));
    }

    void handleCommandReply(const QString& detail)
    {
        const QJsonObject reply = QJsonDocument::fromJson(detail.toUtf8()).object();
        if (!command.active || reply.value(QStringLiteral("id")).toString() != QString::number(command.id)) {
            log("stale_editor_command_reply");
            return;
        }
        const QString code = reply.value(QStringLiteral("code")).toString();
        const QJsonObject value = reply.value(QStringLiteral("value")).toObject();
        const QString message = commandMessage(code, value, reply.value(QStringLiteral("value")).toString());
        if (reply.value(QStringLiteral("ok")).toBool()) finishCommand({true, code, message}, value);
        else finishCommand({false, code.isEmpty() ? QStringLiteral("editor_command_failed") : code, message});
    }

    EngineResult admissible(quint64 id) const
    {
        if (!onOwnerThread()) return refused("wrong_thread", QStringLiteral("必须在界面线程请求引擎操作。"));
        if (!id || acceptedIds.contains(id)) return refused("invalid_request_id", QStringLiteral("请求编号无效或已使用。"));
        if (pending.active) return refused("engine_busy", QStringLiteral("上一个引擎操作尚未结束。"));
        return {true, {}, {}};
    }

    EngineResult begin(quint64 id, EngineOperation operation, const QString& path = {}, bool discard = false,
                       DocumentKind kind = DocumentKind::Unknown)
    {
        const EngineResult allowed = admissible(id);
        if (!allowed.ok) return allowed;
        acceptedIds.insert(id);
        pending = {true, id, operation, path, {}, discard, kind};
        timer->start(operation == EngineOperation::Close ? 30000 : 180000);
        log("request_accepted");
        return {true, QStringLiteral("accepted"), QStringLiteral("请求已交给原生引擎，等待完成事件。")};
    }

    void finish(EngineResult result, const QString& actualPath = {})
    {
        if (!pending.active) return;
        timer->stop();
        const Pending completed = pending;
        log(result.ok ? "operation_completed" : "operation_failed");
        pending = {};
        emit owner->operationFinished(completed.id, completed.operation, std::move(result), actualPath,
            completed.operation == EngineOperation::Close ? DocumentKind::Unknown : completed.kind);
    }

    void replySavePath(int id, const QString& path, int fileType)
    {
        auto* response = new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE_PATH);
        auto* data = new CAscLocalSaveFileDialog;
        data->put_Id(id);
        data->put_Path(path.toStdWString());
        data->put_FileType(fileType ? fileType : AVS_OFFICESTUDIO_FILE_DOCUMENT_DOCX);
        response->m_pData = data;
        manager->Apply(response); // Ownership is transferred; manager/view release it.
    }

    void cancelDialog()
    {
        if (dialogViewId < 0) return;
        const int view = dialogViewId;
        dialogViewId = -1;
        replySavePath(view, QString(), editableFormat(currentKind)); // Empty path = cancelled.
        log("save_destination_cancelled", -1, view);
    }

    void answerSaveQuestion(int id)
    {
        // Renderer actions such as some encryption operations ask the host for
        // permission to save. This host does not support those actions:
        // return an explicit negative response.
        if (CCefView* view = manager->GetViewById(id)) {
            auto* response = new CAscMenuEvent(ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO);
            auto* data = new CAscEditorSaveQuestion;
            data->put_Id(id);
            data->put_Value(false);
            response->m_pData = data;
            view->Apply(response);
        }
    }

    // The same route the upstream desktop app uses for its menu commands:
    // web-apps' Desktop controller maps file:saveas to the editor's Save As.
    static void sendEditorCommand(CCefViewEditor* view, const std::wstring& command)
    {
        auto* data = new CAscExecCommandJS;
        data->put_Command(command);
        data->put_FrameName(L"frameEditor");
        auto* event = new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND_JS);
        event->m_pData = data;
        view->Apply(event);
    }

    EditorHost* createHost()
    {
        auto* view = new EditorHost(container);
        stack->addWidget(view);
        view->Create(manager.get(), cvwtEditor);
        if (!view->GetCefView()) {
            delete view;
            return nullptr;
        }
        log("host_created", -1, view->GetCefView()->GetId());
        return view;
    }

    void show(EditorHost* view)
    {
        if (!view || !stack || !container) return;
        stack->setCurrentWidget(view);
        view->setGeometry(container->contentsRect());
        view->UpdateSize(); // Supply native client pixels before browser creation.
    }

    void retire(EditorHost* view, int id)
    {
        if (id < 0) return;
        retiring.insert(id, view);
        if (view) view->hideNativeBrowser();
        log("view_retired", -1, id);
        manager->DestroyCefView(id, true);
        // DESTROY_SAFE has released the widget link; never touch the view again.
        if (view) view->detachNativePointer();
    }

    void abandonIncoming(EngineResult result)
    {
        EditorHost* view = incomingHost;
        const int id = incomingViewId;
        incomingHost = nullptr;
        incomingViewId = -1;
        incomingModified = false;
        retire(view, id);
        show(host);
        finish(std::move(result));
        emit owner->capabilitiesChanged();
    }

    void fault(EngineResult result)
    {
        faulted = true;
        ready = false;
        if (incomingViewId >= 0) {
            EditorHost* view = incomingHost;
            const int id = incomingViewId;
            incomingHost = nullptr;
            incomingViewId = -1;
            retire(view, id);
            show(host);
        }
        if (pending.active) finish(std::move(result));
        finishCommand(refused("native_session_faulted", QStringLiteral("编辑器会话已停止，无法确认命令是否已执行。")));
        emit owner->capabilitiesChanged();
    }

    void applyAppearance()
    {
        if (!ready || !editor() || (!darkAppearance && !appearanceChanged)) return;
        editor()->ExecuteEditorScript(appearanceScript(darkAppearance).toStdString());
        log(darkAppearance ? "editor_appearance_dark" : "editor_appearance_light", -1, viewId);
    }

    void completeLoad(bool replacing)
    {
        CCefViewEditor* loaded = editorOf(replacing ? incomingHost : host);
        QString actual;
        if (pending.operation == EngineOperation::Open) {
            actual = loaded ? canonicalFile(QString::fromStdWString(loaded->GetLocalFilePath())) : QString();
            if (!samePath(actual, pending.path)) {
                const EngineResult failed = refused("open_path_unconfirmed", QStringLiteral("引擎就绪事件未确认请求的文档路径。"));
                if (replacing) abandonIncoming(failed);
                else fault(failed);
                return;
            }
        }
        const bool wasModified = modified;
        if (replacing) {
            EditorHost* previous = host;
            const int previousId = viewId;
            host = incomingHost;
            viewId = incomingViewId;
            incomingHost = nullptr;
            incomingViewId = -1;
            modified = incomingModified;
            incomingModified = false;
            show(host);
            retire(previous, previousId); // Only now: the new document is confirmed.
            loadRequested = true;
        }
        currentPath = actual;
        currentKind = pending.kind;
        ready = true;
        applyAppearance(); // a fresh view starts in the editor's default theme
        if (wasModified != modified) emit owner->modifiedChanged(modified);
        if (pending.operation == EngineOperation::Open)
            finish({true, QStringLiteral("opened"), QStringLiteral("文档已由 ONLYOFFICE 打开。")}, currentPath);
        else
            finish({true, QStringLiteral("created"), QStringLiteral("新文档已由 ONLYOFFICE 创建。")});
        emit owner->capabilitiesChanged();
    }

    void OnEvent(CAscCefMenuEvent* event) override
    {
        if (!event) return;
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (!acceptsCallbacks) { event->Release(); return; }
        EventSnapshot snapshot;
        snapshot.type = event->m_nType;
        snapshot.viewId = event->get_SenderId();
        if (event->m_pData) {
            switch (snapshot.type) {
            case ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND: {
                auto* data = static_cast<CAscExecCommand*>(event->m_pData);
                snapshot.command = QString::fromStdWString(data->get_Command());
                if (snapshot.command == QStringLiteral("error:page")
                    || snapshot.command == QStringLiteral("openoffice:command"))
                    snapshot.detail = QString::fromStdWString(data->get_Param());
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_DOCUMENT_NAME: {
                auto* data = static_cast<CAscDocumentName*>(event->m_pData);
                snapshot.viewId = data->get_Id();
                snapshot.path = QString::fromStdWString(data->get_Path());
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_MODIFY_CHANGED: {
                auto* data = static_cast<CAscDocumentModifyChanged*>(event->m_pData);
                snapshot.viewId = data->get_Id(); snapshot.flag = data->get_Changed();
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_ONSAVE: {
                auto* data = static_cast<CAscDocumentOnSaveData*>(event->m_pData);
                snapshot.viewId = data->get_Id(); snapshot.flag = data->get_IsCancel();
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE: {
                auto* data = static_cast<CAscLocalSaveFileDialog*>(event->m_pData);
                snapshot.viewId = data->get_Id();
                snapshot.path = QString::fromStdWString(data->get_Path());
                snapshot.formats = data->get_SupportFormats();
                break;
            }
            case ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO:
                snapshot.viewId = static_cast<CAscEditorSaveQuestion*>(event->m_pData)->get_Id();
                break;
            case ASC_MENU_EVENT_TYPE_UI_THREAD_MESSAGE: {
                auto* data = static_cast<CAscUIThreadMessage*>(event->m_pData);
                snapshot.messageType = data->get_Type();
                snapshot.arguments = data->GetArgs();
                break;
            }
            case ASC_MENU_EVENT_TYPE_CEF_ONCLOSE:
            case ASC_MENU_EVENT_TYPE_CEF_ONBEFORECLOSE:
                snapshot.viewId = static_cast<CAscTypeId*>(event->m_pData)->get_Id();
                break;
            default: break;
            }
        }
        event->Release(); // Exactly one release; queued work owns only value copies.
        QMetaObject::invokeMethod(owner, [this, snapshot] { handle(snapshot); }, Qt::QueuedConnection);
    }

    void handleRetiring(const EventSnapshot& event)
    {
        switch (event.type) {
        case ASC_MENU_EVENT_TYPE_CEF_DESTROYWINDOW:
            if (QPointer<EditorHost> view = retiring.take(event.viewId)) view->deleteLater();
            log("retired_view_destroyed", event.type, event.viewId);
            break;
        case ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE:
            replySavePath(event.viewId, QString(), 0); // Never leave a closing view waiting.
            break;
        case ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO:
            answerSaveQuestion(event.viewId);
            break;
        default: break;
        }
    }

    void handleIncoming(const EventSnapshot& event)
    {
        const bool loading = pending.active
            && (pending.operation == EngineOperation::Open || pending.operation == EngineOperation::Create);
        switch (event.type) {
        case ASC_MENU_EVENT_TYPE_CEF_MODIFY_CHANGED:
            incomingModified = event.flag;
            break;
        case ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE:
            replySavePath(event.viewId, QString(), 0); // A loading document cannot be saved yet.
            break;
        case ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO:
            answerSaveQuestion(event.viewId);
            break;
        case ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND:
            if (event.command == QStringLiteral("error:page")) {
                lastNativeError = event.detail;
                abandonIncoming(refused("native_page_error", QStringLiteral("编辑器加载失败：")
                    + event.detail.left(2048) + QStringLiteral("；已保留当前文档。")));
            } else if (event.command.contains(QStringLiteral("doc:onready")) && loading) {
                completeLoad(true);
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_DESTROYWINDOW:
            if (incomingHost) {
                incomingHost->detachNativePointer();
                incomingHost->deleteLater();
            }
            incomingHost = nullptr;
            incomingViewId = -1;
            incomingModified = false;
            show(host);
            finish(refused("native_view_closed", QStringLiteral("新文档视图在加载完成前关闭；已保留当前文档。")));
            emit owner->capabilitiesChanged();
            break;
        case ASC_MENU_EVENT_TYPE_PAGE_CRASH:
            abandonIncoming(refused("native_renderer_crashed", QStringLiteral("新文档的编辑器进程已停止；已保留当前文档。")));
            break;
        default: break;
        }
    }

    void handleSaveDialog(const EventSnapshot& event)
    {
        const int format = editableFormat(currentKind);
        const bool editable = format && (event.formats.empty()
            || std::find(event.formats.begin(), event.formats.end(), format) != event.formats.end());
        const bool current = event.viewId == viewId && viewId >= 0 && !faulted;
        if (current && pending.active && pending.operation == EngineOperation::Save) {
            // Host-requested first save or Save As: hand over the destination
            // the user chose; the native side converts and reports back.
            replySavePath(event.viewId, editable ? pending.path : QString(), format);
            if (!editable)
                finish(refused("save_format_unsupported", QStringLiteral("编辑器没有提供与文档相同的保存格式，已取消保存。")));
            return;
        }
        if (current && !pending.active && ready && editable && dialogViewId < 0) {
            // The editor's own Save As command, or saving a new document from
            // inside the editor. The UI answers through requestSave/cancel.
            dialogViewId = event.viewId;
            log("save_destination_requested", event.type, event.viewId);
            emit owner->saveDestinationRequested(event.path, currentKind);
            return;
        }
        // Exports to other formats and requests during other work are not
        // supported yet: never leave the native dialog waiting.
        replySavePath(event.viewId, QString(), format);
    }

    void handle(const EventSnapshot& event)
    {
        if (!manager) return;
        if (event.type == ASC_MENU_EVENT_TYPE_CEF_CHECK_KEYBOARD) {
            // Keep the native polling work, but do not write/flush a log line
            // every 100 ms. Document, error and lifecycle events remain logged.
            manager->CheckKeyboard();
            return;
        }
        log("native_event", event.type, event.viewId);
        if (event.type == ASC_MENU_EVENT_TYPE_UI_THREAD_MESSAGE) {
            // Preserve the upstream host's UI-thread routing without carrying
            // a borrowed event/payload pointer across the thread boundary.
            auto* routed = new CAscCefMenuEvent(ASC_MENU_EVENT_TYPE_UI_THREAD_MESSAGE);
            routed->put_SenderId(event.viewId);
            auto* data = new CAscUIThreadMessage;
            data->put_Type(event.messageType);
            data->GetArgs() = event.arguments;
            routed->m_pData = data;
            manager->Apply(routed);
            return;
        }
        if (retiring.contains(event.viewId)) { handleRetiring(event); return; }
        if (incomingViewId >= 0 && event.viewId == incomingViewId) { handleIncoming(event); return; }
        if (event.type == ASC_MENU_EVENT_TYPE_CEF_LOCALFILE_SAVE) { handleSaveDialog(event); return; }
        if (event.type == ASC_MENU_EVENT_TYPE_DOCUMENTEDITORS_SAVE_YES_NO) {
            answerSaveQuestion(event.viewId);
            if (event.viewId == viewId && pending.active && pending.operation == EngineOperation::Save)
                finish(refused("save_confirmation_unsupported", QStringLiteral("此操作需要额外的保存确认，当前原生阶段已取消该操作。")));
            return;
        }
        if (event.viewId != viewId || viewId < 0) return;
        switch (event.type) {
        case ASC_MENU_EVENT_TYPE_CEF_MODIFY_CHANGED:
            if (modified != event.flag) {
                modified = event.flag;
                emit owner->modifiedChanged(modified);
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND:
            if (event.command == QStringLiteral("openoffice:command")) {
                handleCommandReply(event.detail);
            } else if (event.command == QStringLiteral("error:page")) {
                lastNativeError = event.detail;
                fault(refused("native_page_error", QStringLiteral("编辑器加载失败：") + event.detail.left(2048)));
            } else if (event.command.contains(QStringLiteral("doc:onready")) && pending.active && !faulted
                && (pending.operation == EngineOperation::Open || pending.operation == EngineOperation::Create)) {
                completeLoad(false);
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_DOCUMENT_NAME:
            if (pending.active && pending.operation == EngineOperation::Save && !faulted)
                pending.confirmedSavePath = canonicalFile(event.path);
            break;
        case ASC_MENU_EVENT_TYPE_CEF_ONSAVE:
            if (pending.active && pending.operation == EngineOperation::Save && !faulted) {
                const QString actual = pending.confirmedSavePath;
                if (event.flag) {
                    finish(refused("native_save_cancelled_or_failed", QStringLiteral("引擎未确认保存；可能已取消或转换失败。")));
                } else if (!samePath(actual, canonicalFile(pending.path)) || !QFileInfo(actual).isFile()) {
                    fault(refused("save_path_unconfirmed", QStringLiteral("引擎未确认保存到所选路径，会话已停止新的保存请求。")));
                } else {
                    currentPath = actual;
                    finish({true, QStringLiteral("saved"), QStringLiteral("引擎已确认保存。")}, actual);
                    emit owner->capabilitiesChanged();
                }
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_ONCLOSE:
        case ASC_MENU_EVENT_TYPE_CEF_ONBEFORECLOSE:
            if (pending.active && pending.operation == EngineOperation::Close && !destroyRequested) {
                if (event.type == ASC_MENU_EVENT_TYPE_CEF_ONBEFORECLOSE && !pending.discard) {
                    finish(refused("unsaved_changes", QStringLiteral("引擎仍有未保存修改，已保留当前文档。")));
                } else {
                    destroyRequested = true;
                    manager->DestroyCefView(viewId, true);
                }
            }
            break;
        case ASC_MENU_EVENT_TYPE_CEF_DESTROYWINDOW:
            if (host) host->detachNativePointer();
            closed = true;
            ready = false;
            viewId = -1;
            dialogViewId = -1;
            finishCommand(refused("native_view_closed", QStringLiteral("编辑器已关闭，无法确认命令是否已执行。")));
            if (pending.active && pending.operation == EngineOperation::Close)
                finish({true, QStringLiteral("closed"), QStringLiteral("原生编辑器已关闭。")});
            else if (pending.active)
                finish(refused("native_view_closed", QStringLiteral("编辑器在操作完成前关闭。")));
            emit owner->capabilitiesChanged();
            break;
        case ASC_MENU_EVENT_TYPE_PAGE_CRASH:
            fault(refused("native_renderer_crashed", QStringLiteral("编辑器进程已停止；未确认操作成功。")));
            break;
        default: break;
        }
    }
};

OnlyOfficeEngineAdapter::OnlyOfficeEngineAdapter(QObject* parent) : OfficeEngine(parent) {}

OnlyOfficeEngineAdapter::~OnlyOfficeEngineAdapter()
{
    if (native_) native_->stopCallbacks();
    shutdownNative();
    if (native_ && native_->viewId >= 0 && !native_->closed) {
        // A caller violated the close handshake. Avoid destroying a manager or
        // host that CEF can still reference; retain native state until process
        // exit, detach the QObject and widget, and explicitly log the failure.
        native_->log("unsafe_teardown_retained_until_process_exit");
        // Keep this listener itself alive: a native callback may already hold
        // its raw address. stopCallbacks() prevents any further QObject use.
        if (native_->editor()) native_->editor()->OnDestroyWidgetImpl();
        if (native_->host) native_->host->detachNativePointer();
        native_.release();
    }
}

EngineResult OnlyOfficeEngineAdapter::initializeNative(const QString& runtimeRoot,
    const QString& profileRoot, int argc, char** argv)
{
    if (QThread::currentThread() != thread() || !qobject_cast<QApplication*>(QCoreApplication::instance()))
        return refused("wrong_thread", QStringLiteral("原生编辑器必须在 QApplication 的界面线程初始化。"));
    if (native_) return refused("already_initialized", QStringLiteral("原生初始化已尝试；请重新启动程序。"));
    const QString runtime = QFileInfo(runtimeRoot).canonicalFilePath();
    if (runtime.isEmpty() || !samePath(runtime, QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath()))
        return refused("runtime_location_mismatch", QStringLiteral("运行依赖必须与本程序入口位于同一目录。"));
    const QDir directory(runtime);
    // converter/empty/<lang>/new.docx is the native blank-document seed used by
    // CreateLocalFile; the optional converter/templates gallery is not needed.
    for (const QString& relative : {QStringLiteral("editors_helper.exe"), QStringLiteral("libcef.dll"),
             QStringLiteral("ascdocumentscore.dll"), QStringLiteral("converter/x2t.exe"),
             QStringLiteral("converter/empty/zh-CN/new.docx"),
             QStringLiteral("editors/web-apps/apps/api/documents/index.html")}) {
        const QFileInfo file(directory.filePath(relative));
        if (!file.isFile() || !file.isReadable())
            return refused("runtime_incomplete", QStringLiteral("缺少必要的运行依赖：") + relative);
    }
    for (const QString& relative : {QStringLiteral("dictionaries")})
        if (!QFileInfo(directory.filePath(relative)).isDir())
            return refused("runtime_incomplete", QStringLiteral("缺少必要的运行目录：") + relative);
    if (profileRoot.isEmpty() || !QDir::isAbsolutePath(profileRoot) || samePath(QDir::cleanPath(profileRoot), runtime))
        return refused("invalid_profile", QStringLiteral("请提供独立的绝对路径作为引擎用户目录。"));
    if (!QDir().mkpath(profileRoot)) return refused("profile_unwritable", QStringLiteral("无法创建引擎用户目录。"));

    native_ = std::make_unique<Native>(this);
    Native& state = *native_;
    const QString profile = QFileInfo(profileRoot).canonicalFilePath();
    state.eventLog.setFileName(QDir(profile).filePath(QStringLiteral("native-events.log")));
    if (!state.eventLog.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return refused("profile_unwritable", QStringLiteral("无法写入引擎开发日志。"));
    for (const QString& relative : {QStringLiteral("data/fonts"), QStringLiteral("data/cache"),
             QStringLiteral("data/recover"), QStringLiteral("templates"), QStringLiteral("disabled-plugins")})
        if (!QDir(profile).mkpath(relative))
            return refused("profile_unwritable", QStringLiteral("无法创建引擎缓存目录。"));

    state.manager = std::make_unique<ApplicationManager>();
    state.manager->SetEventListener(&state);
    auto& settings = state.manager->m_oSettings;
    settings.SetUserDataPath(profile.toStdWString());
    settings.user_templates_path = QDir(profile).filePath(QStringLiteral("templates")).toStdWString();
    settings.spell_dictionaries_path = directory.filePath(QStringLiteral("dictionaries")).toStdWString();
    settings.file_converter_path = directory.filePath(QStringLiteral("converter")).toStdWString();
    settings.local_editors_path = directory.filePath(QStringLiteral("editors/web-apps/apps/api/documents/index.html")).toStdWString();
    // Preserve the upstream gallery location; converter/empty is a distinct
    // native resource used for blank documents, not a substitute gallery.
    settings.system_templates_path = directory.filePath(QStringLiteral("converter/templates")).toStdWString();
    settings.connection_error_path = directory.filePath(QStringLiteral("editors/webext/noconnect.html")).toStdWString();
    settings.additional_fonts_folder.push_back(directory.filePath(QStringLiteral("fonts")).toStdWString());
    settings.system_plugins_path = QDir(profile).filePath(QStringLiteral("disabled-plugins")).toStdWString();
    settings.macroses_support = false;
    settings.plugins_support = false;
    settings.country = "CN";
    // Editor URL parameters for every view, as the upstream host sets them.
    // CreateLocalFile uses them for the UI language and the zh-CN blank seed;
    // the bundled editor maps zh-CN to its Simplified Chinese "zh" locale.
    std::wstring editorParameters = L"lang=zh-CN";
    state.manager->InitAdditionalEditorParams(editorParameters);

    for (int i = 0; i < argc; ++i) {
        const QByteArray argument = argv && argv[i] ? QByteArray(argv[i]) : QByteArray();
        if (!argument.startsWith("--external-message-loop")) state.argumentStorage.push_back(argument);
    }
    if (state.argumentStorage.empty()) state.argumentStorage.push_back(QFile::encodeName(QCoreApplication::applicationFilePath()));
    state.argumentStorage.push_back(QByteArray("--external-message-loop=1"));
    for (auto& argument : state.argumentStorage) state.arguments.push_back(argument.data());
    state.arguments.push_back(nullptr);
    const int nativeArgc = static_cast<int>(state.arguments.size()) - 1;
    CApplicationCEF::Prepare(nativeArgc, state.arguments.data());
    state.application = std::make_unique<CApplicationCEF>();
    const int result = state.application->Init_CEF(state.manager.get(), nativeArgc, state.arguments.data());
    if (state.application->IsChromiumSubprocess()) {
        state.subprocess = true;
        state.subprocessExitCode = result;
        state.log("subprocess_completed");
        return refused("cef_subprocess_completed", QStringLiteral("CEF 子进程已结束。"));
    }
    if (result != 0 || !state.manager->externalLoopInitialized) {
        state.faulted = true;
        state.log("initialization_failed");
        return refused("native_initialization_failed", QStringLiteral("原生引擎初始化或 Qt 事件循环配置失败。"));
    }
    state.manager->CheckFonts(false);
    state.manager->StartSpellChecker();
    state.manager->StartKeyboardChecker();
    state.initialized = true;
    state.log("initialized_not_roundtrip_verified");
    emit capabilitiesChanged();
    return {true, QStringLiteral("initialized"), QStringLiteral("原生引擎已初始化，可以打开或新建 DOCX、XLSX、PPTX。")};
}

EngineCapabilities OnlyOfficeEngineAdapter::capabilities() const noexcept
{
    if (!native_ || !native_->initialized || native_->faulted || native_->closed) return {};
    return {true, true, native_->ready, native_->ready, true, native_->ready};
}

bool OnlyOfficeEngineAdapter::canCreate(DocumentKind kind) const noexcept
{
    // Text, spreadsheets and presentations, each from its blank seed in the runtime.
    if (!capabilities().create || !editableFormat(kind)) return false;
    const QString seed = blankSeed(kind);
    return !seed.isEmpty() && QFileInfo(QDir(QCoreApplication::applicationDirPath()).filePath(seed)).isFile();
}

bool OnlyOfficeEngineAdapter::documentModified() const noexcept
{
    return native_ && native_->modified;
}

bool OnlyOfficeEngineAdapter::needsCloseHandshake() const noexcept
{
    return native_ && native_->viewId >= 0 && !native_->closed;
}

QWidget* OnlyOfficeEngineAdapter::createEditorWidget(QWidget* parent)
{
    if (!capabilities().available || !native_->onOwnerThread()) return nullptr;
    Native& state = *native_;
    if (state.container) return state.container;
    auto* box = new QWidget(parent);
    box->setObjectName(QStringLiteral("nativeEditorContainer"));
    auto* stack = new QStackedLayout(box);
    stack->setContentsMargins(0, 0, 0, 0);
    state.container = box;
    state.stack = stack;
    EditorHost* view = state.createHost();
    if (!view) {
        state.faulted = true;
        state.container = nullptr;
        state.stack = nullptr;
        delete box;
        emit capabilitiesChanged();
        return nullptr;
    }
    state.host = view;
    state.viewId = view->GetCefView()->GetId();
    view->UpdateSize(); // Supply native client pixels before browser creation.
    return box;
}

EngineResult OnlyOfficeEngineAdapter::requestOpen(quint64 requestId, const QString& path,
                                                  bool discardCurrentChanges)
{
    if (!capabilities().available) return unavailable();
    Native& state = *native_;
    const EngineResult allowed = state.admissible(requestId);
    if (!allowed.ok) return allowed;
    if (!state.editor()) return refused("editor_host_missing", QStringLiteral("请先创建原生编辑视图。"));
    if (state.dialogViewId >= 0) return refused("engine_busy", QStringLiteral("编辑器正在等待选择保存位置。"));
    const bool replacing = state.loadRequested;
    if (replacing && !discardCurrentChanges && state.currentModified())
        return refused("unsaved_changes", QStringLiteral("当前文档仍有未保存修改。"));
    const DocumentKind kind = classifyDocument(path);
    if (!editableFormat(kind))
        return refused("unsupported_document", QStringLiteral("只能打开已有、可读的 DOCX、XLSX、PPTX 文件。"));
    const QString canonical = canonicalFile(path);
    const int format = CCefViewEditor::GetFileFormat(canonical.toStdWString());
    if (format != editableFormat(kind))
        return refused("unsupported_document", QStringLiteral("文件内容和扩展名对不上（引擎没有把它识别为 .%1）。")
                                                   .arg(QFileInfo(canonical).suffix().toLower()));
    if (replacing && samePath(canonical, state.currentPath))
        return refused("already_open", QStringLiteral("该文档已在当前窗口打开。"));
    EditorHost* target = state.host;
    if (replacing) {
        // OpenLocalFile mutates the native source/recovery paths of its view:
        // load into a fresh view so a failed open cannot damage the current one.
        target = state.createHost();
        if (!target) return refused("editor_host_failed", QStringLiteral("无法为新文档创建编辑视图；已保留当前文档。"));
        state.incomingHost = target;
        state.incomingViewId = target->GetCefView()->GetId();
        state.incomingModified = false;
    }
    EngineResult accepted = state.begin(requestId, EngineOperation::Open, canonical, discardCurrentChanges, kind);
    if (!accepted.ok) return accepted; // admissible() above makes this unreachable.
    if (!replacing) {
        state.ready = false;
        state.loadRequested = true;
    }
    state.show(target); // CEF needs a visible, sized parent while the document loads.
    Native::editorOf(target)->OpenLocalFile(canonical.toStdWString(), format, L"lang=zh-CN");
    emit capabilitiesChanged();
    return accepted;
}

EngineResult OnlyOfficeEngineAdapter::requestSave(quint64 requestId, const QString& path)
{
    if (!native_) return unavailable();
    if (!native_->onOwnerThread())
        return refused("wrong_thread", QStringLiteral("必须在界面线程请求引擎操作。"));
    Native& state = *native_;
    const bool answeringDialog = state.dialogViewId >= 0;
    const auto refuse = [&state](EngineResult result) {
        state.cancelDialog(); // The editor must not keep waiting for an answer.
        return result;
    };
    if (!capabilities().save || !state.editor()) return refuse(unavailable());
    const EngineResult allowed = state.admissible(requestId);
    if (!allowed.ok) return refuse(allowed);
    if (!answeringDialog && (state.editor()->IsBuilding() || state.editor()->IsSaveLocked()))
        return refused("engine_busy", QStringLiteral("引擎仍在转换或保存文档。"));
    const QString target = saveTarget(path);
    const int format = editableFormat(state.currentKind);
    if (target.isEmpty())
        return refuse(refused("invalid_save_path", QStringLiteral("请选择已有文件夹中的有效保存位置。")));
    if (!format || classifyFileName(target) != state.currentKind)
        return refuse(refused("save_format_unsupported", QStringLiteral("当前只支持保存为与文档相同的格式。")));
    const QFileInfo targetInfo(target);
    if (targetInfo.exists() && !targetInfo.isWritable())
        return refuse(refused("document_read_only", QStringLiteral("所选文件不可写。")));
    const bool sameFile = samePath(target, state.currentPath);
    if (answeringDialog) {
        EngineResult accepted = state.begin(requestId, EngineOperation::Save, target, false, state.currentKind);
        if (!accepted.ok) return refuse(accepted);
        const int view = state.dialogViewId;
        state.dialogViewId = -1;
        state.replySavePath(view, target, format);
        return accepted;
    }
    EngineResult accepted = state.begin(requestId, EngineOperation::Save, target, false, state.currentKind);
    if (!accepted.ok) return accepted;
    if (sameFile && !state.currentModified()) {
        // The renderer ignores a save request without changes and reports
        // nothing; the file on disk already is this document.
        QMetaObject::invokeMethod(this, [this, requestId] {
            if (native_ && native_->pending.active && native_->pending.id == requestId)
                native_->finish({true, QStringLiteral("unchanged"), QStringLiteral("文档没有新的修改，无需再次写入。")},
                                native_->currentPath);
        }, Qt::QueuedConnection);
    } else if (sameFile) {
        state.editor()->Apply(new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_SAVE));
    } else {
        // First save of a new document, or Save As: the editor's own Save As
        // route also works without changes; it asks for the destination with
        // LOCALFILE_SAVE, answered in handleSaveDialog().
        Native::sendEditorCommand(state.editor(), L"file:saveas");
    }
    return accepted;
}

void OnlyOfficeEngineAdapter::cancelSaveDestination()
{
    if (native_ && native_->onOwnerThread()) native_->cancelDialog();
}

void OnlyOfficeEngineAdapter::setEditorAppearance(bool dark)
{
    if (!native_ || !native_->onOwnerThread()) return;
    if (native_->darkAppearance == dark && !native_->appearanceChanged) {
        if (!dark) return; // nothing to undo in a view that never went dark
    }
    native_->appearanceChanged = native_->appearanceChanged || dark != native_->darkAppearance;
    native_->darkAppearance = dark;
    native_->applyAppearance();
}

bool OnlyOfficeEngineAdapter::supportsEditorCommand(const QString& name) const noexcept
{
    // The commands are written for the text editor's API; spreadsheets and presentations
    // have their own, not wired yet.
    return capabilities().edit && editorCommandNames().contains(name) && commandFitsKind(name, native_->currentKind);
}

EngineResult OnlyOfficeEngineAdapter::requestEditorCommand(quint64 requestId, const QString& name,
                                                           const QJsonObject& arguments)
{
    if (!capabilities().edit || !native_->editor()) return unavailable();
    Native& state = *native_;
    if (!state.onOwnerThread()) return refused("wrong_thread", QStringLiteral("必须在界面线程请求引擎操作。"));
    if (!supportsEditorCommand(name)) return refused("unsupported_command", QStringLiteral("编辑器不支持这个命令。"));
    if (!requestId || state.acceptedIds.contains(requestId))
        return refused("invalid_request_id", QStringLiteral("请求编号无效或已使用。"));
    if (state.pending.active || state.incomingViewId >= 0 || state.dialogViewId >= 0)
        return refused("engine_busy", QStringLiteral("文档操作进行中，请稍后再执行命令。"));
    if (state.command.active) return refused("engine_busy", QStringLiteral("上一条编辑器命令尚未完成。"));
    if (state.editor()->IsBuilding() || state.editor()->IsSaveLocked())
        return refused("engine_busy", QStringLiteral("引擎仍在转换或保存文档。"));
    // The engine re-checks what it executes; it does not rely on the caller.
    if (!editorArgumentsValid(name, arguments))
        return refused("invalid_argument", QStringLiteral("命令参数无效。"));
    state.acceptedIds.insert(requestId);
    state.command = {true, requestId, name};
    state.commandTimer->start(15000);
    state.logCommand("editor_command_accepted", requestId);
    state.editor()->ExecuteEditorScript(editorCommandScript(requestId, name, arguments).toStdString());
    return {true, QStringLiteral("accepted"), QStringLiteral("命令已交给编辑器，等待它回报结果。")};
}

EngineResult OnlyOfficeEngineAdapter::requestCreate(quint64 requestId, DocumentKind kind, const QString& title,
                                                    bool discardCurrentChanges)
{
    if (!capabilities().available) return unavailable();
    Native& state = *native_;
    const EngineResult allowed = state.admissible(requestId);
    if (!allowed.ok) return allowed;
    if (!canCreate(kind))
        return refused("create_unsupported", QStringLiteral("这个版本不能新建这类文档。"));
    if (!validTitle(title, kind))
        return refused("invalid_title", QStringLiteral("新文档名称无效。"));
    if (!state.editor()) return refused("editor_host_missing", QStringLiteral("请先创建原生编辑视图。"));
    if (state.dialogViewId >= 0) return refused("engine_busy", QStringLiteral("编辑器正在等待选择保存位置。"));
    const bool replacing = state.loadRequested;
    if (replacing && !discardCurrentChanges && state.currentModified())
        return refused("unsaved_changes", QStringLiteral("当前文档仍有未保存修改。"));
    EditorHost* target = state.host;
    if (replacing) {
        target = state.createHost();
        if (!target) return refused("editor_host_failed", QStringLiteral("无法为新文档创建编辑视图；已保留当前文档。"));
        state.incomingHost = target;
        state.incomingViewId = target->GetCefView()->GetId();
        state.incomingModified = false;
    }
    EngineResult accepted = state.begin(requestId, EngineOperation::Create, {}, discardCurrentChanges, kind);
    if (!accepted.ok) return accepted;
    if (!replacing) {
        state.ready = false;
        state.loadRequested = true;
    }
    state.show(target);
    // Blank seed: converter/empty/zh-CN/new.<ext> (selected by the lang editor
    // parameter). The title names the document until its first save.
    Native::editorOf(target)->CreateLocalFile(editorType(kind), title.toStdWString());
    emit capabilitiesChanged();
    return accepted;
}

EngineResult OnlyOfficeEngineAdapter::requestClose(quint64 requestId, bool discardChanges)
{
    if (!native_ || !native_->initialized) return unavailable();
    Native& state = *native_;
    if (!state.onOwnerThread()) return refused("wrong_thread", QStringLiteral("必须在界面线程请求引擎操作。"));
    if (!needsCloseHandshake()) return refused("no_native_view", QStringLiteral("当前没有待关闭的原生视图。"));
    if (state.dialogViewId >= 0) return refused("engine_busy", QStringLiteral("编辑器正在等待选择保存位置。"));
    if (state.currentModified() && !discardChanges)
        return refused("unsaved_changes", QStringLiteral("当前文档仍有未保存修改。"));
    const EngineResult accepted = state.begin(requestId, EngineOperation::Close, {}, discardChanges);
    if (!accepted.ok) return accepted;
    if (!state.loadRequested) {
        state.destroyRequested = true;
        state.manager->DestroyCefView(state.viewId, true);
    } else if (state.editor()) {
        state.editor()->Apply(new CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_CLOSE));
    } else {
        // Destruction may already be in progress after a timeout. Wait for the
        // authoritative DESTROYWINDOW callback; never synthesize success.
        state.log("awaiting_native_destruction");
    }
    return accepted;
}

int OnlyOfficeEngineAdapter::runNativeEventLoop(bool& used)
{
    used = false;
    if (!native_) return 0;
    Native& state = *native_;
    if (state.subprocess) { used = true; return state.subprocessExitCode; }
    if (!state.initialized || !state.application || !state.onOwnerThread()) return 0;
    state.loopRunning = true;
    const int result = state.application->RunMessageLoop(used);
    state.loopRunning = false;
    state.loopReturned = true;
    // Upstream keeps m_bIsMessageLoopRunned set after Run() returns. Clear it
    // before CloseApplication so Close() can perform actual context shutdown.
    state.application->ExitMessageLoop();
    // The final browser can quit Qt before the queued DESTROYWINDOW snapshot
    // and UI completion reach their receivers. Drain those queued metacalls.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    state.log("message_loop_returned");
    return result;
}

void OnlyOfficeEngineAdapter::shutdownNative()
{
    if (!native_) return;
    Native& state = *native_;
    if (!state.onOwnerThread()) return;
    if (state.loopRunning || needsCloseHandshake()) {
        state.log("shutdown_waits_for_close_handshake");
        return;
    }
    state.timer->stop();
    state.stopCallbacks();
    if (state.manager && !state.subprocess) state.manager->CloseApplication();
    state.application.reset();
    if (state.manager) state.manager->SetEventListener(&droppedEventSink());
    state.manager.reset();
    state.initialized = false;
    state.log("shutdown_completed");
    emit capabilitiesChanged();
}
} // namespace openoffice
