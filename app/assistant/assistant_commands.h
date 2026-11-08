#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

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

enum class ArgumentType { String, DocumentKind };

struct CommandArgument {
    QString name;
    ArgumentType type = ArgumentType::String;
    bool required = false;
    int maxLength = 0; // String only; 0 = no limit
    QString description;
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
};

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

// The catalog as tool descriptions (name, title, description, inputSchema),
// the shape an MCP tools/list response uses. Not an MCP server by itself.
QJsonArray commandCatalogJson();

// Parses the panel's slash syntax, e.g. "/插入 你好" or "/undo". Returns
// nullopt for text that is not a slash command; *error is set when the text
// is a slash command that cannot be parsed. This is not language understanding.
std::optional<AssistantCommand> parseSlashCommand(const QString& text, QString* error);

} // namespace openoffice
