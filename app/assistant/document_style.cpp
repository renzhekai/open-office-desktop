#include "document_style.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace openoffice {
namespace {

constexpr double kPriorWeight = 0.30;
constexpr double kSceneWeight = 0.55;
constexpr double kPreferenceWeight = 0.15;
constexpr double kExploration = 0.12;
constexpr double kFloor = 0.05;

const QStringList& knownScenes()
{
    static const QStringList scenes{QStringLiteral("official"), QStringLiteral("meeting"), QStringLiteral("report"),
                                    QStringLiteral("event"), QStringLiteral("festive"), QStringLiteral("finance"),
                                    QStringLiteral("education"), QStringLiteral("tech"), QStringLiteral("personal"),
                                    QStringLiteral("letter"), QStringLiteral("general")};
    return scenes;
}

// A colour as 0-255 channels rounded to whole values, as it is written ("#RRGGBB").
struct Rgb {
    int red = 0;
    int green = 0;
    int blue = 0;
};

Rgb fromHex(const QString& text)
{
    const QString digits = text.startsWith(QLatin1Char('#')) ? text.mid(1) : text;
    bool ok = false;
    const uint value = digits.toUInt(&ok, 16);
    if (!ok || digits.size() != 6) return {};
    return {int((value >> 16) & 0xFF), int((value >> 8) & 0xFF), int(value & 0xFF)};
}

QString hex(const Rgb& color)
{
    return QStringLiteral("#%1%2%3").arg(color.red, 2, 16, QLatin1Char('0')).arg(color.green, 2, 16, QLatin1Char('0'))
        .arg(color.blue, 2, 16, QLatin1Char('0')).toUpper();
}

double luminance(const Rgb& color)
{
    const auto channel = [](int value) {
        const double v = value / 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red) + 0.7152 * channel(color.green) + 0.0722 * channel(color.blue);
}

double contrast(const Rgb& first, const Rgb& second)
{
    const double a = luminance(first);
    const double b = luminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

Rgb hsl(double hue, double saturation, double lightness)
{
    const double h = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0) / 360.0;
    const double s = std::clamp(saturation, 0.0, 1.0);
    const double l = std::clamp(lightness, 0.0, 1.0);
    const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    const double p = 2 * l - q;
    const auto channel = [p, q](double t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0 / 6) return p + (q - p) * 6 * t;
        if (t < 0.5) return q;
        if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
        return p;
    };
    const auto byte = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
    return {byte(channel(h + 1.0 / 3)), byte(channel(h)), byte(channel(h - 1.0 / 3))};
}

// Darker until it stands `ratio` against `against`.
Rgb darkUntil(double hue, double saturation, double lightness, const Rgb& against, double ratio)
{
    Rgb color = hsl(hue, saturation, lightness);
    while (contrast(color, against) < ratio && lightness > 0.02) {
        lightness -= 0.01;
        color = hsl(hue, saturation, lightness);
    }
    return color;
}

// Lighter until `on` stands `ratio` against it.
Rgb lightUntil(double hue, double saturation, double lightness, const Rgb& on, double ratio)
{
    Rgb color = hsl(hue, saturation, lightness);
    while (contrast(on, color) < ratio && lightness < 0.995) {
        lightness += 0.005;
        color = hsl(hue, saturation, lightness);
    }
    return color;
}

bool number(const QJsonValue& value, double low, double high, double* out)
{
    if (!value.isDouble()) return false;
    const double v = value.toDouble();
    if (v < low || v > high) return false;
    *out = v;
    return true;
}

bool oneOf(const QJsonValue& value, const QStringList& choices, QString* out)
{
    if (!value.isString() || !choices.contains(value.toString())) return false;
    *out = value.toString();
    return true;
}

bool boolean(const QJsonValue& value, bool* out)
{
    if (!value.isBool()) return false;
    *out = value.toBool();
    return true;
}

// A font name goes into a style attribute: letters, digits, spaces and hyphens only.
bool fontName(const QJsonValue& value, QString* out)
{
    static const QRegularExpression name(QStringLiteral("^[\\p{L}\\p{N} \\-]{1,40}$"));
    if (!value.isString() || !name.match(value.toString()).hasMatch()) return false;
    *out = value.toString().trimmed();
    return !out->isEmpty();
}

QString readPack(const QJsonObject& object, StylePack* pack)
{
    static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9-]{0,39}$"));
    pack->id = object.value(QStringLiteral("id")).toString();
    if (!idPattern.match(pack->id).hasMatch()) return QStringLiteral("id 只能用小写字母、数字和连字符");
    pack->name = object.value(QStringLiteral("name")).toString().trimmed();
    if (pack->name.isEmpty() || pack->name.size() > 12) return QStringLiteral("name 要有，最多 12 个字");
    pack->description = object.value(QStringLiteral("description")).toString().trimmed();
    if (pack->description.isEmpty() || pack->description.size() > 60) return QStringLiteral("description 要有，最多 60 个字");
    if (!number(object.value(QStringLiteral("prior")), 0, 1, &pack->prior)) return QStringLiteral("prior 要是 0 到 1 的数");
    const QJsonObject scenes = object.value(QStringLiteral("scenes")).toObject();
    if (scenes.isEmpty()) return QStringLiteral("scenes 要有");
    for (auto it = scenes.begin(); it != scenes.end(); ++it) {
        double weight = 0;
        if (!knownScenes().contains(it.key())) return QStringLiteral("不认识的场景 ") + it.key();
        if (!number(it.value(), 0, 1, &weight)) return QStringLiteral("场景 ") + it.key() + QStringLiteral(" 的权重要是 0 到 1 的数");
        pack->scenes.insert(it.key(), weight);
    }
    const QJsonObject palette = object.value(QStringLiteral("palette")).toObject();
    if (!number(palette.value(QStringLiteral("hue")), 0, 360, &pack->hue)) return QStringLiteral("palette.hue 要是 0 到 360 的数");
    if (!number(palette.value(QStringLiteral("chroma")), 0, 1, &pack->chroma)) return QStringLiteral("palette.chroma 要是 0 到 1 的数");
    const QJsonObject fonts = object.value(QStringLiteral("fonts")).toObject();
    if (!fontName(fonts.value(QStringLiteral("heading")), &pack->headingFont) || !fontName(fonts.value(QStringLiteral("body")), &pack->bodyFont))
        return QStringLiteral("fonts.heading 和 fonts.body 要是字体名（字母、数字、空格）");
    const QJsonObject sizes = object.value(QStringLiteral("sizes")).toObject();
    if (!number(sizes.value(QStringLiteral("title")), 8, 48, &pack->titleSize)
        || !number(sizes.value(QStringLiteral("heading")), 8, 48, &pack->headingSize)
        || !number(sizes.value(QStringLiteral("subheading")), 8, 48, &pack->subheadingSize)
        || !number(sizes.value(QStringLiteral("body")), 8, 48, &pack->bodySize))
        return QStringLiteral("sizes 的 title、heading、subheading、body 要是 8 到 48 的磅值");
    if (!oneOf(object.value(QStringLiteral("title")).toObject().value(QStringLiteral("align")),
               {QStringLiteral("left"), QStringLiteral("center")}, &pack->titleAlign))
        return QStringLiteral("title.align 要是 left 或 center");
    if (!boolean(object.value(QStringLiteral("headings")).toObject().value(QStringLiteral("rule")), &pack->headingRule))
        return QStringLiteral("headings.rule 要是 true 或 false");
    const QJsonObject body = object.value(QStringLiteral("body")).toObject();
    if (!number(body.value(QStringLiteral("lineHeight")), 1, 3, &pack->lineHeight)
        || !boolean(body.value(QStringLiteral("firstLineIndent")), &pack->firstLineIndent)
        || !number(body.value(QStringLiteral("spaceAfter")), 0, 24, &pack->spaceAfter))
        return QStringLiteral("body 要有 lineHeight（1 到 3）、firstLineIndent（true/false）、spaceAfter（0 到 24 磅）");
    if (!oneOf(object.value(QStringLiteral("emphasis")), {QStringLiteral("accent"), QStringLiteral("bold")}, &pack->emphasis))
        return QStringLiteral("emphasis 要是 accent 或 bold");
    const QJsonObject table = object.value(QStringLiteral("table")).toObject();
    if (!oneOf(table.value(QStringLiteral("header")), {QStringLiteral("solid"), QStringLiteral("tint"), QStringLiteral("line")},
               &pack->tableHeader)
        || !boolean(table.value(QStringLiteral("banded")), &pack->banded))
        return QStringLiteral("table.header 要是 solid、tint 或 line，table.banded 要是 true 或 false");
    return {};
}

} // namespace

QList<StylePack> parseStylePacks(const QByteArray& json, QStringList* errors)
{
    QList<StylePack> packs;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) {
        if (errors) errors->append(QStringLiteral("不是 JSON 对象：") + parseError.errorString());
        return packs;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("open-office-style-packs")
        || root.value(QStringLiteral("version")).toInt() != 1) {
        if (errors) errors->append(QStringLiteral("format 要是 open-office-style-packs，version 要是 1"));
        return packs;
    }
    QSet<QString> ids;
    const QJsonArray list = root.value(QStringLiteral("packs")).toArray();
    for (int i = 0; i < list.size(); ++i) {
        StylePack pack;
        QString problem = list[i].isObject() ? readPack(list[i].toObject(), &pack) : QStringLiteral("不是对象");
        if (problem.isEmpty() && ids.contains(pack.id)) problem = QStringLiteral("id 重复");
        if (!problem.isEmpty()) {
            if (errors) errors->append(QStringLiteral("第 %1 个风格包（%2）：%3").arg(i + 1).arg(pack.id, problem));
            continue;
        }
        ids.insert(pack.id);
        packs.append(pack);
    }
    return packs;
}

const QList<StylePack>& builtinStylePacks()
{
    static const QList<StylePack> packs = [] {
        QFile file(QStringLiteral(":/style_packs.json"));
        if (!file.open(QIODevice::ReadOnly)) return QList<StylePack>();
        return parseStylePacks(file.readAll(), nullptr);
    }();
    return packs;
}

const StylePack* findStylePack(const QString& id)
{
    for (const StylePack& pack : builtinStylePacks())
        if (pack.id == id) return &pack;
    return nullptr;
}

double contrastRatio(const QString& first, const QString& second)
{
    return contrast(fromHex(first), fromHex(second));
}

StylePalette paletteFor(double hue, double chroma)
{
    const Rgb white{255, 255, 255};
    const Rgb text{0x26, 0x26, 0x26};
    const bool grey = chroma <= 0.001;
    const auto saturation = [grey, chroma](double base, double scale) { return grey ? 0.0 : std::clamp(base + scale * chroma, 0.0, 1.0); };
    StylePalette palette;
    const Rgb primary = darkUntil(hue, saturation(0.15, 0.7), 0.32, white, 7.0);
    palette.primary = hex(primary);
    palette.accent = hex(darkUntil(hue, saturation(0.2, 0.75), 0.45, white, 4.5));
    palette.tint = hex(lightUntil(hue, saturation(0.25, 0.5), 0.95, text, 12.0));
    Rgb strong = hsl(hue, saturation(0.3, 0.5), 0.86);
    double lightness = 0.86;
    while ((contrast(primary, strong) < 4.5 || contrast(text, strong) < 7.0) && lightness < 0.995) {
        lightness += 0.005;
        strong = hsl(hue, saturation(0.3, 0.5), lightness);
    }
    palette.tintStrong = hex(strong);
    palette.border = hex(hsl(hue, saturation(0.2, 0.4), grey ? 0.65 : 0.72));
    palette.text = hex(text);
    return palette;
}

QMap<QString, double> detectScenes(const QString& taskId, const QString& text)
{
    QMap<QString, double> scenes;
    static const QMap<QString, QString> byTask{
        {QStringLiteral("notice"), QStringLiteral("official")}, {QStringLiteral("minutes"), QStringLiteral("meeting")},
        {QStringLiteral("report"), QStringLiteral("report")},   {QStringLiteral("email"), QStringLiteral("letter")},
        {QStringLiteral("leave"), QStringLiteral("letter")},    {QStringLiteral("plan"), QStringLiteral("event")}};
    if (byTask.contains(taskId)) scenes.insert(byTask.value(taskId), 1.0);
    static const QList<QPair<QString, QRegularExpression>> words{
        {QStringLiteral("official"), QRegularExpression(QStringLiteral("通知|公告|通报|请示|批复|规定|办法|制度|决定|公文|红头"))},
        {QStringLiteral("meeting"), QRegularExpression(QStringLiteral("会议|纪要|例会|座谈|研讨会"))},
        {QStringLiteral("report"), QRegularExpression(QStringLiteral("总结|汇报|报告|述职|周报|月报|年报|季报|复盘|工作计划"))},
        {QStringLiteral("event"), QRegularExpression(QStringLiteral("活动|策划|晚会|联欢|庆典|团建|运动会|比赛|大赛|开幕|发布会|展会|年会|聚会|生日|游园"))},
        {QStringLiteral("festive"), QRegularExpression(QStringLiteral("春节|新年|元旦|中秋|国庆|端午|元宵|重阳|七夕|婚礼|婚宴|喜报|贺信|贺词|庆祝|周年庆|年会|新春"))},
        {QStringLiteral("finance"), QRegularExpression(QStringLiteral("财务|预算|决算|报销|费用|成本|收入|营收|利润|报价|采购|账目|资金"))},
        {QStringLiteral("education"), QRegularExpression(QStringLiteral("教学|课程|教案|学习|培训|学生|班级|考试|作业|讲座|论文|研究|课题|读书"))},
        {QStringLiteral("tech"), QRegularExpression(QStringLiteral("技术|产品|需求|系统|软件|开发|数据|架构|测试|上线|版本|接口|算法"))},
        {QStringLiteral("personal"), QRegularExpression(QStringLiteral("简历|求职|自我介绍|自荐|履历|应聘|个人陈述"))},
        {QStringLiteral("letter"), QRegularExpression(QStringLiteral("邮件|书信|感谢信|邀请函|请假|申请书|倡议书|慰问|家长信"))}};
    for (const auto& [scene, pattern] : words)
        if (pattern.match(text).hasMatch()) scenes.insert(scene, std::max(scenes.value(scene, 0.0), 0.8));
    return scenes;
}

QList<ScoredStyle> scoreStylePacks(const QList<StylePack>& packs, const QMap<QString, double>& scenes,
                                   const QMap<QString, double>& preferences, const std::function<double()>& random)
{
    QList<ScoredStyle> scored;
    for (const StylePack& pack : packs) {
        double match = 0;
        if (scenes.isEmpty()) {
            match = pack.scenes.value(QStringLiteral("general"), 0.5);
        } else {
            for (auto it = scenes.begin(); it != scenes.end(); ++it)
                match = std::max(match, it.value() * pack.scenes.value(it.key(), 0.0));
        }
        const double preference = std::clamp(preferences.value(pack.id, 0.0), -1.0, 1.0);
        const double explore = random ? std::clamp(random(), 0.0, 1.0) * kExploration : 0.0;
        const double score = kPriorWeight * pack.prior + kSceneWeight * match + kPreferenceWeight * preference + explore;
        scored.append({&pack, std::max(score, kFloor)});
    }
    return scored;
}

QList<ScoredStyle> styleCandidates(QList<ScoredStyle> scored, int best, const std::function<double()>& random)
{
    std::stable_sort(scored.begin(), scored.end(), [](const ScoredStyle& a, const ScoredStyle& b) { return a.score > b.score; });
    QList<ScoredStyle> chosen = scored.mid(0, best);
    const QList<ScoredStyle> rest = scored.mid(best);
    QStringList skip;
    for (const ScoredStyle& item : chosen) skip.append(item.pack->id);
    if (!rest.isEmpty()) {
        if (const StylePack* extra = drawStyle(rest, skip, random))
            for (const ScoredStyle& item : rest)
                if (item.pack == extra) chosen.append(item);
    }
    return chosen;
}

const StylePack* drawStyle(const QList<ScoredStyle>& scored, const QStringList& skip, const std::function<double()>& random)
{
    QList<ScoredStyle> open;
    for (const ScoredStyle& item : scored)
        if (!skip.contains(item.pack->id)) open.append(item);
    if (open.isEmpty()) open = scored;
    if (open.isEmpty()) return nullptr;
    double total = 0;
    for (const ScoredStyle& item : open) total += item.score;
    double point = (random ? std::clamp(random(), 0.0, 0.999999) : 0.0) * total;
    for (const ScoredStyle& item : open) {
        if (point < item.score) return item.pack;
        point -= item.score;
    }
    return open.last().pack;
}

DocumentStyle documentStyle(const StylePack& pack)
{
    return {pack, paletteFor(pack.hue, pack.chroma)};
}

} // namespace openoffice
