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
};

const QList<TaskSpec>& taskCatalog();
const TaskSpec* findTask(const QString& id);
QList<const TaskSpec*> tasksFor(TaskScope scope);

struct TaskInput {
    QString text;         // the selection, or the document text
    QString detail;       // Writing: what the person typed
    QString documentName; // for context only
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
