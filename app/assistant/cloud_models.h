#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace openoffice {

// Cloud models the person adds in the AI settings: any service with an
// OpenAI-compatible chat API (DeepSeek, Qwen on DashScope, Moonshot, OpenAI,
// a server on this computer...), given by its base URL, an API key and the
// model names to offer in the model list.
//
// What goes to such a service leaves this computer: the person's questions and
// the document text the assistant sends along. The settings page says so, and the
// window asks once per service before switching to it.
struct CloudProvider {
    QString id;      // stable, made from the name when added
    QString name;    // shown: "DeepSeek"
    QString baseUrl; // "https://api.deepseek.com/v1"; requests go to <base>/chat/completions
    QStringList models;
};

QList<CloudProvider> cloudProviders();
std::optional<CloudProvider> findCloudProvider(const QString& id);
// Adds or replaces (same id). An empty apiKey keeps the saved one. The key is kept
// encrypted for this Windows account (DPAPI), never as plain text.
bool saveCloudProvider(const CloudProvider& provider, const QString& apiKey, QString* error);
void removeCloudProvider(const QString& id);
QString cloudApiKey(const QString& id);
bool hasCloudApiKey(const QString& id);
// A new id for a name: "deepseek", "deepseek-2"...
QString newCloudProviderId(const QString& name);

// https only, except a server on this computer (http://127.0.0.1, localhost).
// Returns what is wrong, in Chinese; empty when the address is fine.
QString checkCloudBaseUrl(const QString& url);
// "a, b，c" or one per line -> the names, without duplicates.
QStringList splitModelNames(const QString& text);

// The person agreed once that this service may receive what they ask.
bool cloudConfirmed(const QString& id);
void setCloudConfirmed(const QString& id);

// Windows data protection for this account (CryptProtectData); base64 text.
QString protectSecret(const QString& secret);
QString unprotectSecret(const QString& stored);

} // namespace openoffice
