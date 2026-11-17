#include "cloud_models.h"

#include "model_catalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QUrl>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace openoffice {
namespace {

const char* const kProviders = "ai/cloudProviders";

QString keyName(const QString& id)
{
    return QStringLiteral("ai/cloudKey/") + id;
}

void saveAll(const QList<CloudProvider>& providers)
{
    QJsonArray list;
    for (const CloudProvider& provider : providers)
        list.append(QJsonObject{{QStringLiteral("id"), provider.id},
                                {QStringLiteral("name"), provider.name},
                                {QStringLiteral("baseUrl"), provider.baseUrl},
                                {QStringLiteral("models"), QJsonArray::fromStringList(provider.models)}});
    aiSettings().setValue(QLatin1String(kProviders), QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
}

#ifdef Q_OS_WIN
// Ties the stored text to this program as well as to the account.
const char kEntropy[] = "Open Office cloud API key";
#endif

} // namespace

QList<CloudProvider> cloudProviders()
{
    QList<CloudProvider> providers;
    const QJsonArray list = QJsonDocument::fromJson(aiSettings().value(QLatin1String(kProviders)).toString().toUtf8()).array();
    for (const QJsonValue& value : list) {
        const QJsonObject object = value.toObject();
        CloudProvider provider{object.value(QStringLiteral("id")).toString(), object.value(QStringLiteral("name")).toString(),
                               object.value(QStringLiteral("baseUrl")).toString(), {}};
        for (const QJsonValue& model : object.value(QStringLiteral("models")).toArray())
            if (!model.toString().isEmpty()) provider.models.append(model.toString());
        if (!provider.id.isEmpty() && !provider.baseUrl.isEmpty()) providers.append(provider);
    }
    return providers;
}

std::optional<CloudProvider> findCloudProvider(const QString& id)
{
    for (const CloudProvider& provider : cloudProviders())
        if (provider.id == id) return provider;
    return std::nullopt;
}

bool saveCloudProvider(const CloudProvider& provider, const QString& apiKey, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (provider.id.isEmpty() || provider.name.trimmed().isEmpty()) return fail(QStringLiteral("请填写名称。"));
    const QString address = checkCloudBaseUrl(provider.baseUrl);
    if (!address.isEmpty()) return fail(address);
    if (provider.models.isEmpty()) return fail(QStringLiteral("请至少填写一个模型名称，或点“获取模型列表”。"));
    if (!apiKey.isEmpty()) {
        const QString stored = protectSecret(apiKey);
        if (stored.isEmpty()) return fail(QStringLiteral("没能加密保存 API Key。"));
        aiSettings().setValue(keyName(provider.id), stored);
    }
    QList<CloudProvider> providers = cloudProviders();
    bool replaced = false;
    for (CloudProvider& existing : providers)
        if (existing.id == provider.id) {
            existing = provider;
            replaced = true;
        }
    if (!replaced) providers.append(provider);
    saveAll(providers);
    return true;
}

void removeCloudProvider(const QString& id)
{
    QList<CloudProvider> providers = cloudProviders();
    providers.erase(std::remove_if(providers.begin(), providers.end(), [&id](const CloudProvider& provider) { return provider.id == id; }),
                    providers.end());
    saveAll(providers);
    QSettings settings = aiSettings();
    settings.remove(keyName(id));
    settings.remove(QStringLiteral("ai/cloudConfirmed/") + id);
}

QString cloudApiKey(const QString& id)
{
    return unprotectSecret(aiSettings().value(keyName(id)).toString());
}

bool hasCloudApiKey(const QString& id)
{
    return !aiSettings().value(keyName(id)).toString().isEmpty();
}

QString newCloudProviderId(const QString& name)
{
    QString base = name.trimmed().toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    base.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (base.isEmpty()) base = QStringLiteral("cloud");
    QSet<QString> taken;
    for (const CloudProvider& provider : cloudProviders()) taken.insert(provider.id);
    QString id = base;
    for (int n = 2; taken.contains(id); ++n) id = QStringLiteral("%1-%2").arg(base).arg(n);
    return id;
}

QString checkCloudBaseUrl(const QString& text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    if (text.trimmed().isEmpty()) return QStringLiteral("请填写接口地址（Base URL），例如 https://api.deepseek.com/v1。");
    if (!url.isValid() || url.host().isEmpty()) return QStringLiteral("接口地址的格式不对，例如 https://api.deepseek.com/v1。");
    const QString host = url.host().toLower();
    const bool here = host == QStringLiteral("127.0.0.1") || host == QStringLiteral("localhost") || host == QStringLiteral("::1");
    if (url.scheme() == QStringLiteral("https")) return {};
    if (url.scheme() == QStringLiteral("http") && here) return {};
    return QStringLiteral("接口地址要用 https://（只有这台电脑上的服务可以用 http://127.0.0.1）。API Key 不能明文发出去。");
}

QStringList splitModelNames(const QString& text)
{
    QStringList names;
    for (const QString& part : text.split(QRegularExpression(QStringLiteral("[,，;；\\s]+")), Qt::SkipEmptyParts))
        if (!names.contains(part.trimmed())) names.append(part.trimmed());
    return names;
}

bool cloudConfirmed(const QString& id)
{
    return aiSettings().value(QStringLiteral("ai/cloudConfirmed/") + id, false).toBool();
}

void setCloudConfirmed(const QString& id)
{
    aiSettings().setValue(QStringLiteral("ai/cloudConfirmed/") + id, true);
}

QString protectSecret(const QString& secret)
{
#ifdef Q_OS_WIN
    const QByteArray plain = secret.toUtf8();
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Open Office", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    const QByteArray sealed(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return QStringLiteral("dpapi:") + QString::fromLatin1(sealed.toBase64());
#else
    Q_UNUSED(secret);
    return {}; // no safe store: keys are not kept
#endif
}

QString unprotectSecret(const QString& stored)
{
#ifdef Q_OS_WIN
    if (!stored.startsWith(QStringLiteral("dpapi:"))) return {};
    const QByteArray sealed = QByteArray::fromBase64(stored.mid(6).toLatin1());
    DATA_BLOB in{static_cast<DWORD>(sealed.size()), reinterpret_cast<BYTE*>(const_cast<char*>(sealed.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    const QString plain = QString::fromUtf8(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
#else
    Q_UNUSED(stored);
    return {};
#endif
}

} // namespace openoffice
