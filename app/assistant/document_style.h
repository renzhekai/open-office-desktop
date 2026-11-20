#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>

namespace openoffice {

// A style pack (style_packs.json): one whole look for a drafted document — palette, fonts,
// sizes, title, headings, body spacing, emphasis and tables — chosen together so that the
// parts go with each other. Packs carry weights: a general one (prior) and one per scene.
struct StylePack {
    QString id;
    QString name;        // 稳重蓝
    QString description; // what it looks like, for the person and for the model choosing
    double prior = 0.5;  // 0-1
    QMap<QString, double> scenes; // scene -> 0-1 (official, meeting, report, event, festive, ...)
    double hue = 0;      // 0-360: the palette is made from it, not listed
    double chroma = 0.5; // 0 (black and grey) - 1 (vivid)
    QString headingFont;
    QString bodyFont;
    double titleSize = 22;
    double headingSize = 16;
    double subheadingSize = 14;
    double bodySize = 12;
    QString titleAlign = QStringLiteral("center"); // left | center
    bool headingRule = false;   // a line under the section headings
    double lineHeight = 1.5;
    bool firstLineIndent = false; // two characters, as Chinese body text
    double spaceAfter = 0;      // points after a body paragraph
    QString emphasis = QStringLiteral("bold"); // accent (bold in the accent colour) | bold
    QString tableHeader = QStringLiteral("solid"); // solid (filled, white text) | tint (light fill) | line (three-line table)
    bool banded = false;        // every other row lightly filled
};

// The packs in a style-pack file; packs that break a rule are left out and said in `errors`.
QList<StylePack> parseStylePacks(const QByteArray& json, QStringList* errors);
// The packs that come with the program (style_packs.json, compiled in).
const QList<StylePack>& builtinStylePacks();
const StylePack* findStylePack(const QString& id);

// Colours made from a hue and chroma, each held to a contrast (WCAG ratio):
struct StylePalette {
    QString primary;    // titles, headings, filled header: at least 7:1 on white (white text on it too)
    QString accent;     // emphasised words, heading rules: at least 4.5:1 on white
    QString tint;       // banded rows: body text on it at least 12:1
    QString tintStrong; // a light header: primary text on it at least 4.5:1, body text 7:1
    QString border;     // table lines
    QString text;       // body text
};
StylePalette paletteFor(double hue, double chroma);
double contrastRatio(const QString& first, const QString& second);

// What the draft is for, from the kind picked (notice, minutes, plan...) and the words
// of the request and title: scene -> confidence 0-1. Empty when nothing says.
QMap<QString, double> detectScenes(const QString& taskId, const QString& text);

struct ScoredStyle {
    const StylePack* pack = nullptr;
    double score = 0;
};
// score = 0.30 prior + 0.55 scene match + 0.15 the person's preference (-1..1)
//       + exploration (0..0.12), never below 0.05: a pack that is rarely right still can be
// chosen when it fits, and now and then when it is not the obvious one.
QList<ScoredStyle> scoreStylePacks(const QList<StylePack>& packs, const QMap<QString, double>& scenes,
                                   const QMap<QString, double>& preferences, const std::function<double()>& random);
// The candidates the model chooses from: the `best` highest scores and one more drawn from
// the rest by weight.
QList<ScoredStyle> styleCandidates(QList<ScoredStyle> scored, int best, const std::function<double()>& random);
// One pack drawn by weight, not one of `skip` (unless every pack is in it).
const StylePack* drawStyle(const QList<ScoredStyle>& scored, const QStringList& skip, const std::function<double()>& random);

// A pack with its colours: what markdownToHtml applies.
struct DocumentStyle {
    StylePack pack;
    StylePalette palette;
};
DocumentStyle documentStyle(const StylePack& pack);

} // namespace openoffice
