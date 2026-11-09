#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>

#include <optional>

namespace openoffice {

// One bubble of a conversation as the assistant ball showed it.
struct ConversationMessage {
    QString role;  // "user" or "assistant"
    QString title; // the assistant bubble's title
    QString text;
    bool ok = true;
    bool markdown = false;
};

// A conversation with the assistant, kept on this computer in the program's
// profile: what was shown, what the local AI remembers, and the document it
// was about (the work file).
struct Conversation {
    QString id;
    QString title;            // the person's name for it, else taken from the first message
    bool titleChosen = false; // renamed by the person: never replaced
    QString documentPath;     // empty for a new document not saved yet
    QString documentName;
    QDateTime created;
    QDateTime updated;
    QList<ConversationMessage> messages;
    QJsonArray modelHistory; // LocalAssistant's context, so a reopened conversation continues
};

// One JSON file per conversation in a folder (the profile's "conversations");
// without a folder they are kept in memory only (tests, no profile).
class ConversationStore {
public:
    explicit ConversationStore(const QString& folder = {});

    QString folder() const { return m_folder; }
    static Conversation create(const QString& documentPath, const QString& documentName);
    // A title from the first thing said: its first line, at most 24 characters.
    static QString titleFrom(const QString& text);

    bool save(const Conversation& conversation);
    QList<Conversation> list() const; // newest first
    std::optional<Conversation> load(const QString& id) const;
    bool remove(const QString& id);
    int removeAllExcept(const QString& keepId); // how many were removed
    bool rename(const QString& id, const QString& title);

private:
    QString pathFor(const QString& id) const;

    QString m_folder;
    QHash<QString, Conversation> m_memory;
};

} // namespace openoffice
