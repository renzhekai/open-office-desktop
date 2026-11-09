#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace openoffice {

// Everyday office jobs the assistant does in one step, without the person
// having to write a prompt: rework the selected text, read the whole document,
// or draft a common kind of document. Each task is one model call with a
// fixed instruction; the result is shown first and only goes into the
// document when the person picks what to do with it.
enum class TaskScope {
    Selection, // works on the selected text: the result can replace it or follow it
    Document,  // reads the whole document
    Writing    // drafts something new from a few words the person types
};

enum class TaskOutput {
    Rewrite,    // plain text that stands in for the selection
    Markdown,   // structured text (headings, lists, tables) to insert
    Corrections // a list of exact fixes the person can apply one by one
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
};

// Characters of document text a task sends to the model at most.
constexpr int kTaskTextLimit = 12000;

// The chat messages (system, user) for one task.
QJsonArray taskMessages(const TaskSpec& spec, const TaskInput& input);
// Ollama "format" for tasks with structured output (corrections); empty otherwise.
QJsonObject taskFormat(const TaskSpec& spec);
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

} // namespace openoffice
