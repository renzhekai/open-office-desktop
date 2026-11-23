#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace openoffice {

// The assistant's command interface: every operation an assistant (a future
// local model, an MCP client, or the panel's own slash commands) may perform
// is a named, schema-checked command. Nothing here talks to a model or the
// network; it only describes and validates what may be executed.
enum class CommandSource {
    User,      // typed by the person in the assistant panel
    Assistant, // proposed by a model (not connected yet)
    External,  // an external client such as a future MCP server
    Unknown    // never guessed: callers that cannot tell must say Unknown
};

enum class ArgumentType {
    String,
    DocumentKind,
    Choice,
    Integer, // a JSON number (or a string of digits) between minimum and maximum
    Boolean, // true or false (or the strings "true"/"false")
    Color,   // "#RRGGBB" (the # may be left out); "none" too where the description says so
    Address, // a cell or a range in A1 notation: D2, A1:C10
    Grid,    // rows of cell texts: [["名称","数量"],["笔","3"]]; at most maximum rows, maxLength per cell
    Lines    // lines of text: ["第一点","第二点"]; at most maximum lines, maxLength per line
};

struct CommandArgument {
    QString name;
    ArgumentType type = ArgumentType::String;
    bool required = false;
    int maxLength = 0; // String only; 0 = no limit
    QString description;
    QStringList choices; // Choice only: the allowed values
    int minimum = 0;     // Integer only
    int maximum = 0;     // Integer only
    bool allowNone = false; // Color only: "none" removes the color
};

// Where a command runs: the host window (document lifecycle) or the embedded
// editor (document content, through OfficeEngine::requestEditorCommand).
enum class CommandTarget { Host, Editor };

struct CommandSpec {
    QString name;
    QString title;       // short Chinese label for the panel
    QString description; // what it does, for people and models
    QList<CommandArgument> arguments;
    CommandTarget target = CommandTarget::Host;
    bool needsDocument = false;
    bool changesDocument = false;
    // Editor commands work in one kind of document: text, spreadsheet, presentation, or any.
    QString documents = QStringLiteral("text");
};

// Whether a command can run in a document of this kind ("text", "spreadsheet",
// "presentation"; empty: no document yet, as text). Host commands always can.
bool commandFitsDocument(const CommandSpec& spec, const QString& kind);

struct AssistantCommand {
    QString name;
    QJsonObject arguments;
    CommandSource source = CommandSource::Unknown;
};

struct CommandOutcome {
    bool ok = false;
    QString code;
    QString message;
    QJsonObject value;
};

const QList<CommandSpec>& commandCatalog();
const CommandSpec* findCommand(const QString& name);
QString sourceName(CommandSource source);

// ok=true when the command names a catalog entry and its arguments match the
// schema exactly (no unknown, missing or mistyped arguments, length limits).
CommandOutcome validateCommand(const AssistantCommand& command);
// "加粗、居中、底色 #D9E2F3": the formats a format command sets, for people.
QString formatWords(const QJsonObject& arguments);
// What a command will change, for the person to confirm ("第 1 张表格第 1 行、所有列：加粗、居中");
// empty for commands that have their own preview (insert, replace).
QString describeCommand(const AssistantCommand& command);

// The catalog as tool descriptions (name, title, description, inputSchema),
// the shape an MCP tools/list response uses. Not an MCP server by itself.
QJsonArray commandCatalogJson();

// Parses the panel's slash syntax, e.g. "/插入 你好" or "/undo". Returns
// nullopt for text that is not a slash command; *error is set when the text
// is a slash command that cannot be parsed. This is not language understanding.
std::optional<AssistantCommand> parseSlashCommand(const QString& text, QString* error);

// What the panel's "/" list offers for the catalog commands people type: the
// word, how to use it and what it does. (The panel adds its own, such as /新对话.)
struct SlashHint {
    QString word;    // "插入"
    QString command; // "text.insert"
    QString usage;   // "/插入 文字"
    QString description;
    bool takesText = false; // something follows the word
};
QList<SlashHint> slashHints();

} // namespace openoffice
