#pragma once

#include "assistant_commands.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace openoffice {

// Everyday office jobs the assistant does in one step, without the person
// having to write a prompt: rework the selected text, read the whole document,
// or draft a common kind of document. Each task is one model call with a
// fixed instruction; the result is shown first and only goes into the
// document when the person picks what to do with it.
enum class TaskScope {
    Selection, // works on the selected text: the result can replace it or follow it
    Document,  // reads the whole document
    Writing,   // drafts something new from a few words the person types
    Table      // plans changes to one table, applied one cell or range at a time
};

enum class TaskOutput {
    Rewrite,    // plain text that stands in for the selection
    Markdown,   // structured text (headings, lists, tables) to insert
    Corrections, // a list of exact fixes the person can apply one by one
    TablePlan,   // a list of cell changes (text or format), checked and applied one by one
    Choice       // one of the options the program offers (TaskInput::options), as {"style": id}
};

struct TaskSpec {
    QString id;
    QString title;       // the quick-action label, also the reply title
    TaskScope scope = TaskScope::Selection;
    TaskOutput output = TaskOutput::Rewrite;
    QString instruction; // what the model is asked to do
    QString placeholder; // Writing only: what the person should type
    bool offered = true; // a quick action; false: only reached from what the person typed
};

const QList<TaskSpec>& taskCatalog();
const TaskSpec* findTask(const QString& id);
QList<const TaskSpec*> tasksFor(TaskScope scope); // the quick actions of a scope

// What something the person typed (not a quick action) asks for. Decided by
// code, not by the model: a small model given tools tends to insert at the
// cursor when it should replace the selection, and to split a piece of writing
// into several inserts.
enum class RequestKind {
    Chat,            // a question or anything else: the conversation with tools
    ReworkSelection, // text is selected and this is an instruction for it ("详细点"): task "rework"
    Write            // "写一篇…", "帮我起草…": task "write", shown before it goes in
};
RequestKind classifyRequest(const QString& text, bool hasSelection);
bool isQuestion(const QString& request);

// A request about a table: it names one (表格、表头、单元格…) or the cursor is in one,
// and asks for changes or a check ("看看有没有算错" counts). Then the table is read
// and the "table" task plans the changes, instead of rewriting text.
bool tableRequest(const QString& request, bool cursorInTable);
// "第 2 张表格", "表2", "T2": that table's number; 0 when the request does not say.
int tableNumberIn(const QString& request);

// One table as table.read reports it: the texts and the formats the plan can change.
struct TableCell {
    QString text;
    bool bold = false;
    bool italic = false;
    QString align; // empty: left
    QString fill;  // "#RRGGBB"; empty: none
};
using TableGrid = QList<QList<TableCell>>;
TableGrid tableGridFrom(const QJsonObject& tableRead);
// The table for the model: one line per cell, "R2C3：文字 [加粗、居中、底色 #D9E2F3]".
QString tableForPrompt(const TableGrid& grid, int table, int maxCells = 300);
// "第 2 行第 3 列", "第 1 行第 1–4 列", "第 2–5 行第 3 列".
QString tablePlace(int firstRow, int firstColumn, int lastRow, int lastColumn);

struct PlannedEdit {
    QString place;  // where, for people
    QString before; // the text now, or what the format change is about
    QString after;  // the new text, or the format
    QString reason;
    AssistantCommand command; // table.setCell or format.cells
};
// The model's plan checked against the table: cells must exist; the change must be one
// of text, bold, italic, align, fill, color or fontSize with a valid value; a text change
// is one cell; nothing that is already so; no duplicates; at most 20.
// With the request, a fill or color that names no color takes the request's, or is
// read as text to write in when the request names no color at all.
QList<PlannedEdit> parseTablePlan(const QString& answer, const TableGrid& grid, int table, const QString& request = QString());
// Formats the program reads from the request itself, clause by clause: where (表头、
// 第 N 行/列、“小计那一列”、“已取消的那几行”、整张表) and how (加粗、斜体、对齐、底色、文字颜色、
// 字号). A clause without a place goes on the last place named. allUnderstood is false when
// a clause asks for a format the program could not place or could not read.
QList<PlannedEdit> requestedFormats(const QString& request, const TableGrid& grid, int table, bool* allUnderstood);
// The program's formats first; the model's format changes go when the program read every
// format asked for (or only those of the kinds it covered); the model's text changes stay.
QList<PlannedEdit> withRequestedFormats(const QList<PlannedEdit>& plan, const TableGrid& grid, int table, const QString& request);

// Arithmetic is the program's job, not the model's. A column whose numbers follow from
// other columns of the same row, found in the table itself: a product of two columns
// (金额 = 单价 × 数量) or the sum of the columns just before it (总分 = 语文 + 数学 + 英语).
// It counts when it holds in most rows (at least 2, and at least 60% of the rows that
// have all the numbers); the rows where it does not hold are the ones to fix.
struct TableFormula {
    int column = 0;     // 1-based
    QList<int> from;    // 1-based; two columns for a product, two or more for a sum
    bool product = false;
    // A sum may also subtract (期末 = 期初 + 入库 − 出库): +1 or -1 per column of from,
    // the first always +1. Empty for a product.
    QList<int> signs;
    struct Fix {
        int row = 0;    // 1-based
        QString before; // the cell's text
        QString after;  // the same text with the right number
    };
    QList<Fix> wrong;
};
// "12", "¥8.50", "1,200", "85 g", "120元": the number; false for text, dates and ranges.
bool cellNumber(const QString& text, double* value);
QList<TableFormula> tableFormulas(const TableGrid& grid);
// "第 5 列（总分）= 第 2–4 列之和" and, per wrong row, what it should be; for the prompt.
QString formulaFacts(const TableGrid& grid, const QList<TableFormula>& formulas);
// The request asks to check or fix numbers, or the table as a whole ("检查一下", "算错").
bool checkRequest(const QString& request);
// The model's plan held to the arithmetic: a text change in a formula column stays only
// when it writes the right number; with addMissing, every wrong row the model left out
// is added. The result keeps the plan's order, the program's fixes first.
QList<PlannedEdit> withFormulaFixes(const QList<PlannedEdit>& plan, const TableGrid& grid, int table,
                                    const QList<TableFormula>& formulas, bool addMissing);
// When the request asks to make a column uniform (统一、一致、规范、整理) and every text
// change the model made in a column only takes out separators (spaces, dashes, dots,
// slashes, brackets), the other cells of that column that become the same shape by
// taking out the same characters get the same change. The model often stops early.
QList<PlannedEdit> withSameCleanup(const QList<PlannedEdit>& plan, const TableGrid& grid, int table, const QString& request);
// The table (1-based) whose header row has a cell the request names ("总分有没有算错"
// names 总分), from document.outline's blocks; 0 when none does.
int tableMentionedIn(const QString& request, const QJsonArray& outlineBlocks);

// The quick action an instruction for the selection amounts to ("详细一点" is
// 扩写), so a small model gets that task's plain instruction, with a length
// target, besides the person's own words. Empty when none fits.
QString presetForInstruction(const QString& request);
// True when a rewrite did not really change the text: the same words (spacing,
// punctuation and list marks aside), or, for 扩写, hardly any longer.
bool rewriteUnchanged(const QString& original, const QString& result, const QString& preset);

struct TaskInput {
    QString text;         // the selection, or the document text
    QString detail;       // Writing: what the person typed
    QString documentName; // for context only
    bool insist = false;  // a second try: the first result was the original again
    QStringList options;  // Choice: the ids the answer must be one of
};

// Characters of document text a task sends to the model at most.
constexpr int kTaskTextLimit = 12000;

// The chat messages (system, user) for one task.
QJsonArray taskMessages(const TaskSpec& spec, const TaskInput& input);
// Ollama "format" for tasks with structured output (corrections, plans, a choice among
// input.options); empty otherwise.
QJsonObject taskFormat(const TaskSpec& spec, const TaskInput& input = {});
// A context window that fits the messages and the expected answer.
int contextLengthFor(const QJsonArray& messages, bool withTools);

bool mostlyChinese(const QString& text);

struct Correction {
    QString original;
    QString corrected;
    QString reason;
};
// Fixes from a corrections answer that can really be applied: the original
// must occur in the document, differ from the correction and be short.
// Duplicates are dropped; at most 30 are kept.
QList<Correction> parseCorrections(const QString& answer, const QString& documentText);

// What the request says about where and how, held against the model's table plan:
// - "已完成的那几行": a cell format the model put on a cell whose text the request names
//   goes to that cell's whole row; "价格列右对齐": one put on the header of a column the
//   request names as a column goes to the whole column;
// - formats the request names (加粗、斜体、居中/左/右对齐、底色 with a color) that the plan
//   left out are added where the plan formats most, when the request is about one place.
QList<PlannedEdit> withRequestedScope(const QList<PlannedEdit>& plan, const TableGrid& grid, int table, const QString& request);

// A paragraph as document.outline reports it.
struct OutlineParagraph {
    int number = 0; // the P number (every top-level paragraph counts, empty ones too)
    QString text;
    QString style;  // "Heading 1", "Normal", or empty
    bool bold = false;
    bool italic = false;
    QString align;  // empty: left
    double size = 0; // points; 0: mixed or not known
    QString color;  // "#RRGGBB"; empty: automatic or mixed
};
QList<OutlineParagraph> outlineParagraphs(const QJsonArray& blocks);
// Formats the program reads from a request about a document's paragraphs or words, clause
// by clause, with no model: where (标题、第 N 段、最后 N 段、“X”开头的段落、含有“X”的段落、
// 文中所有“X”、全文, or the selection when nothing else is named) and how (加粗、斜体、下划线、
// 对齐、字号、文字颜色、突出显示、一/二/三级标题、正文样式). Paragraphs are counted as people
// count them: those with text. understood is false when a clause asks for a format the
// program cannot place or apply there; then the request goes to the model.
QList<PlannedEdit> requestedDocumentFormats(const QString& request, const QList<OutlineParagraph>& paragraphs,
                                            bool hasSelection, bool* understood);

// "帮我优化一下样式", "美化表格", "排版好看点": a request about style that names no format.
bool vagueStyleRequest(const QString& request);
// A request that names a format (加粗、居中、底色、字号…): then the readers above decide.
// The column names a request gives a new table ("两列：姓名、电话", "列为项目、数值、单位",
// "表头是日期、事项"); empty when it gives none.
QStringList requestedColumns(const QString& request);
bool namesAFormat(const QString& request);
// The request asks for wording to change (改成、统一、填上、替换、检查…), not only formats.
// A request that names formats and no such thing gets no text changes from the model.
bool asksForTextChanges(const QString& request);
// The program's default style, offered change by change (each only where it is not so yet):
// a table gets a bold, centred header with a light fill and right-aligned number columns;
// a document gets its title centred, bold and larger, its numbered subheadings (一、 （一）
// 1.) bold, and a closing department name or date right-aligned.
QList<PlannedEdit> defaultTableStyle(const TableGrid& grid, int table);
QList<PlannedEdit> defaultDocumentStyle(const QList<OutlineParagraph>& paragraphs);

// After inserting text with tables: each table meant (as markdownTables made it regular)
// looked for in document.outline's blocks by its size and header; what is wrong, in words
// (empty when every table is there as meant and spans at least half the page).
QString insertedTableProblems(const QList<QList<QStringList>>& meant, const QJsonArray& outlineBlocks);

// Dates are the program's job too. "2026-11-04", "2026/11/4", "2026年11月4日", "11月4日", "11/4".
struct CellDate {
    int year = 0; // 0: not written
    int month = 0;
    int day = 0;
};
bool cellDate(const QString& text, CellDate* date);
// When the request asks to write dates one way and shows how ("统一写成“11月3日”"), every
// date in a column of dates is rewritten that way by the program (a date without a year
// takes the column's usual year), and the model's own edits of those cells are dropped.
QList<PlannedEdit> withDateStyle(const QList<PlannedEdit>& plan, const TableGrid& grid, int table, const QString& request);

// The table's words for proofreading: each distinct cell text that is not a number or a
// date, one per line.
QString tableTexts(const TableGrid& grid);
// Proofreading fixes as cell edits: each cell that contains the wrong text and that the
// plan does not already change.
QList<PlannedEdit> withTypoFixes(const QList<PlannedEdit>& plan, const QList<Correction>& corrections, const TableGrid& grid, int table);

} // namespace openoffice
