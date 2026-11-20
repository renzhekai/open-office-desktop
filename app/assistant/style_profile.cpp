#include "style_profile.h"

#include "model_catalog.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace openoffice {
namespace {

constexpr char kKey[] = "style/profile";
constexpr int kKeep = 300;
constexpr double kHalfLifeDays = 60;
constexpr int kMinSignals = 3;
constexpr int kMinDocuments = 2;
constexpr double kSceneLean = 3; // signals in a scene before it outweighs the overall tendency

double fade(qint64 age)
{
    return std::pow(0.5, double(std::max<qint64>(age, 0)) / 86400.0 / kHalfLifeDays);
}

struct Tally {
    double sum = 0;
    int count = 0;
    QSet<QString> documents;
};

Tally tally(const StyleProfile& profile, const QString& pack, const QString& scene, qint64 now)
{
    Tally result;
    for (const StyleSignal& signal : profile.entries) {
        if (signal.pack != pack || (!scene.isEmpty() && signal.scene != scene)) continue;
        result.sum += signalWeight(signal.action) * fade(now - signal.time);
        ++result.count;
        result.documents.insert(signal.document);
    }
    return result;
}

bool settled(const Tally& tally)
{
    return tally.count >= kMinSignals && tally.documents.size() >= kMinDocuments;
}

} // namespace

double signalWeight(const QString& action)
{
    if (action == QStringLiteral("chosen")) return 1.0;
    if (action == QStringLiteral("kept")) return 0.3;
    if (action == QStringLiteral("rejected")) return -0.5;
    return 0;
}

QString documentHash(const QString& documentKey)
{
    return QString::fromLatin1(QCryptographicHash::hash(documentKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

StyleProfile loadStyleProfile()
{
    StyleProfile profile;
    const QJsonObject root = QJsonDocument::fromJson(aiSettings().value(QLatin1String(kKey)).toString().toUtf8()).object();
    profile.paused = root.value(QStringLiteral("paused")).toBool();
    const QJsonObject manual = root.value(QStringLiteral("manual")).toObject();
    for (auto it = manual.begin(); it != manual.end(); ++it) {
        const int value = it.value().toInt();
        if (value == 1 || value == -1) profile.manual.insert(it.key(), value);
    }
    for (const QJsonValue& value : root.value(QStringLiteral("signals")).toArray()) {
        const QJsonObject item = value.toObject();
        const StyleSignal signal{item.value(QStringLiteral("pack")).toString(), item.value(QStringLiteral("scene")).toString(),
                                 item.value(QStringLiteral("document")).toString(), item.value(QStringLiteral("action")).toString(),
                                 qint64(item.value(QStringLiteral("time")).toDouble())};
        if (!signal.pack.isEmpty() && signalWeight(signal.action) != 0) profile.entries.append(signal);
    }
    return profile;
}

void saveStyleProfile(const StyleProfile& profile)
{
    QJsonObject manual;
    for (auto it = profile.manual.begin(); it != profile.manual.end(); ++it) manual.insert(it.key(), it.value());
    QJsonArray list;
    for (const StyleSignal& signal : profile.entries)
        list.append(QJsonObject{{QStringLiteral("pack"), signal.pack}, {QStringLiteral("scene"), signal.scene},
                                {QStringLiteral("document"), signal.document}, {QStringLiteral("action"), signal.action},
                                {QStringLiteral("time"), double(signal.time)}});
    const QJsonObject root{{QStringLiteral("paused"), profile.paused}, {QStringLiteral("manual"), manual},
                           {QStringLiteral("signals"), list}};
    aiSettings().setValue(QLatin1String(kKey), QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

void recordStyleSignal(StyleProfile* profile, const QString& pack, const QString& scene, const QString& documentKey,
                       const QString& action, qint64 now)
{
    if (!profile || profile->paused || pack.isEmpty() || signalWeight(action) == 0) return;
    profile->entries.append({pack, scene.isEmpty() ? QStringLiteral("general") : scene, documentHash(documentKey), action, now});
    while (profile->entries.size() > kKeep) profile->entries.removeFirst();
}

QString mainScene(const QMap<QString, double>& scenes)
{
    QString best = QStringLiteral("general");
    double confidence = 0;
    for (auto it = scenes.begin(); it != scenes.end(); ++it)
        if (it.value() > confidence) {
            best = it.key();
            confidence = it.value();
        }
    return best;
}

QMap<QString, double> stylePreferences(const StyleProfile& profile, const QMap<QString, double>& scenes, qint64 now)
{
    QSet<QString> packs;
    for (const StyleSignal& signal : profile.entries) packs.insert(signal.pack);
    for (auto it = profile.manual.begin(); it != profile.manual.end(); ++it) packs.insert(it.key());
    const QString scene = mainScene(scenes);
    QMap<QString, double> result;
    for (const QString& pack : packs) {
        double preference = 0;
        if (const int manual = profile.manual.value(pack); manual != 0) {
            preference = manual; // set by hand: before anything learned
        } else {
            const Tally overall = tally(profile, pack, QString(), now);
            if (settled(overall)) {
                // In this scene, as far as there are signals for it; otherwise the overall tendency.
                const Tally here = tally(profile, pack, scene, now);
                const double general = std::tanh(overall.sum / 3.0);
                const double local = here.count > 0 ? std::tanh(here.sum / 3.0) : general;
                preference = (here.count * local + kSceneLean * general) / (here.count + kSceneLean);
            }
        }
        if (preference != 0) result.insert(pack, std::clamp(preference, -1.0, 1.0));
    }
    return result;
}

StyleStanding styleStanding(const StyleProfile& profile, const QString& pack, qint64 now)
{
    const Tally overall = tally(profile, pack, QString(), now);
    StyleStanding standing;
    standing.count = overall.count;
    standing.documents = int(overall.documents.size());
    standing.settled = settled(overall);
    standing.manual = profile.manual.value(pack);
    standing.preference = standing.manual != 0 ? standing.manual : standing.settled ? std::tanh(overall.sum / 3.0) : 0.0;
    return standing;
}

} // namespace openoffice
