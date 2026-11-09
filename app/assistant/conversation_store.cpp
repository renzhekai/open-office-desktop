#include "conversation_store.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace openoffice {
namespace {

bool validId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]{8}-[0-9]{6}-[0-9a-f]{6}$"));
    return pattern.match(id).hasMatch(); // never a path
}

QJsonObject toJson(const Conversation& conversation)
{
    QJsonArray messages;
    for (const ConversationMessage& message : conversation.messages)
        messages.append(QJsonObject{{QStringLiteral("role"), message.role}, {QStringLiteral("title"), message.title},
                                    {QStringLiteral("text"), message.text}, {QStringLiteral("ok"), message.ok},
                                    {QStringLiteral("markdown"), message.markdown}});
    return {{QStringLiteral("version"), 1},
            {QStringLiteral("id"), conversation.id},
            {QStringLiteral("title"), conversation.title},
            {QStringLiteral("titleChosen"), conversation.titleChosen},
            {QStringLiteral("documentPath"), conversation.documentPath},
            {QStringLiteral("documentName"), conversation.documentName},
            {QStringLiteral("created"), conversation.created.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("updated"), conversation.updated.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("messages"), messages},
            {QStringLiteral("modelHistory"), conversation.modelHistory}};
}

std::optional<Conversation> fromJson(const QJsonObject& object)
{
    Conversation conversation;
    conversation.id = object.value(QStringLiteral("id")).toString();
    if (!validId(conversation.id)) return std::nullopt;
    conversation.title = object.value(QStringLiteral("title")).toString();
    conversation.titleChosen = object.value(QStringLiteral("titleChosen")).toBool();
    conversation.documentPath = object.value(QStringLiteral("documentPath")).toString();
    conversation.documentName = object.value(QStringLiteral("documentName")).toString();
    conversation.created = QDateTime::fromString(object.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
    conversation.updated = QDateTime::fromString(object.value(QStringLiteral("updated")).toString(), Qt::ISODateWithMs);
    for (const QJsonValue& value : object.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject message = value.toObject();
        conversation.messages.append({message.value(QStringLiteral("role")).toString(),
                                      message.value(QStringLiteral("title")).toString(),
                                      message.value(QStringLiteral("text")).toString(),
                                      message.value(QStringLiteral("ok")).toBool(true),
                                      message.value(QStringLiteral("markdown")).toBool()});
    }
    conversation.modelHistory = object.value(QStringLiteral("modelHistory")).toArray();
    return conversation;
}

} // namespace

ConversationStore::ConversationStore(const QString& folder) : m_folder(folder) {}

Conversation ConversationStore::create(const QString& documentPath, const QString& documentName)
{
    Conversation conversation;
    const QDateTime now = QDateTime::currentDateTime();
    conversation.id = now.toString(QStringLiteral("yyyyMMdd-HHmmss-"))
        + QString::number(QRandomGenerator::global()->bounded(0x1000000), 16).rightJustified(6, QLatin1Char('0'));
    conversation.documentPath = documentPath;
    conversation.documentName = documentName;
    conversation.created = now;
    conversation.updated = now;
    return conversation;
}

QString ConversationStore::titleFrom(const QString& text)
{
    QString line = text.trimmed().section(QLatin1Char('\n'), 0, 0).simplified();
    if (line.startsWith(QLatin1Char('/')) || line.startsWith(QChar(0xFF0F))) line = line.mid(1); // "/插入 …"
    return line.size() > 24 ? line.left(24) + QStringLiteral("…") : line;
}

QString ConversationStore::pathFor(const QString& id) const
{
    return QDir(m_folder).filePath(id + QStringLiteral(".json"));
}

bool ConversationStore::save(const Conversation& conversation)
{
    if (!validId(conversation.id)) return false;
    if (m_folder.isEmpty()) {
        m_memory.insert(conversation.id, conversation);
        return true;
    }
    if (!QDir().mkpath(m_folder)) return false;
    QSaveFile file(pathFor(conversation.id)); // whole or not at all
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(toJson(conversation)).toJson(QJsonDocument::Compact));
    return file.commit();
}

QList<Conversation> ConversationStore::list() const
{
    QList<Conversation> result;
    if (m_folder.isEmpty()) {
        result = m_memory.values();
    } else {
        const QStringList files = QDir(m_folder).entryList({QStringLiteral("*.json")}, QDir::Files);
        for (const QString& name : files)
            if (const auto conversation = load(name.chopped(5))) result.append(*conversation);
    }
    std::sort(result.begin(), result.end(), [](const Conversation& a, const Conversation& b) {
        return a.updated != b.updated ? a.updated > b.updated : a.id > b.id;
    });
    return result;
}

std::optional<Conversation> ConversationStore::load(const QString& id) const
{
    if (!validId(id)) return std::nullopt;
    if (m_folder.isEmpty()) {
        const auto found = m_memory.constFind(id);
        return found == m_memory.constEnd() ? std::nullopt : std::optional<Conversation>(*found);
    }
    QFile file(pathFor(id));
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    auto conversation = fromJson(document.object());
    if (!conversation || conversation->id != id) return std::nullopt;
    return conversation;
}

bool ConversationStore::remove(const QString& id)
{
    if (!validId(id)) return false;
    if (m_folder.isEmpty()) return m_memory.remove(id) > 0;
    return QFile::remove(pathFor(id));
}

int ConversationStore::removeAllExcept(const QString& keepId)
{
    int removed = 0;
    for (const Conversation& conversation : list())
        if (conversation.id != keepId && remove(conversation.id)) ++removed;
    return removed;
}

bool ConversationStore::rename(const QString& id, const QString& title)
{
    auto conversation = load(id);
    if (!conversation) return false;
    conversation->title = title.simplified();
    conversation->titleChosen = true;
    return save(*conversation);
}

} // namespace openoffice
