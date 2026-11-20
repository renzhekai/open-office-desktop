#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>

namespace openoffice {

// 排版风格档案: what the program learns, quietly and only on this computer, about the looks
// the person keeps for drafts — and what the person sets by hand. Only style pack ids,
// scenes, a hashed document key and times are kept, never text from the documents.
//
// Signals (requirements.md, 学习信号表): a look inserted as first shown counts a little
// (+0.3, "kept"); a look inserted after switching counts a lot (+1.0, "chosen"); a look
// switched away from counts against it (-0.5, "rejected"). A tendency counts only once it
// has shown at least 3 times in at least 2 documents; older signals fade (half of their
// weight after 60 days); per scene, with few signals there, it leans on the overall one.
// What the person sets by hand (常用 / 不用) comes before anything learned.
struct StyleSignal {
    QString pack;
    QString scene;     // the draft's main scene, or "general"
    QString document;  // a hash of the document's key, so "two documents" can be told apart
    QString action;    // kept | chosen | rejected
    qint64 time = 0;   // seconds since the epoch, UTC
};

struct StyleProfile {
    QList<StyleSignal> entries; // the signals, oldest first
    QMap<QString, int> manual; // pack -> +1 常用, -1 不用
    bool paused = false;       // nothing new is learned (what was learned still counts)
};

// Where a pack stands, for the 风格档案 window.
struct StyleStanding {
    double preference = 0; // -1..1, overall (manual setting included)
    int count = 0;         // signals for it, counted or not yet
    int documents = 0;     // documents they came from
    bool settled = false;  // enough to count
    int manual = 0;
};

StyleProfile loadStyleProfile();
void saveStyleProfile(const StyleProfile& profile);

double signalWeight(const QString& action);
QString documentHash(const QString& documentKey);
// Adds one signal (nothing while paused); keeps the latest 300.
void recordStyleSignal(StyleProfile* profile, const QString& pack, const QString& scene, const QString& documentKey,
                       const QString& action, qint64 now = QDateTime::currentSecsSinceEpoch());
// The person's preference per pack for a draft in these scenes (as scoreStylePacks takes it).
QMap<QString, double> stylePreferences(const StyleProfile& profile, const QMap<QString, double>& scenes,
                                       qint64 now = QDateTime::currentSecsSinceEpoch());
StyleStanding styleStanding(const StyleProfile& profile, const QString& pack, qint64 now = QDateTime::currentSecsSinceEpoch());
// The scene a signal is filed under: the most confident one, or "general".
QString mainScene(const QMap<QString, double>& scenes);

} // namespace openoffice
