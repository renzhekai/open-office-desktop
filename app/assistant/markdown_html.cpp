#include "markdown_html.h"

#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QStringList>

namespace openoffice {
namespace {

QString inlineHtml(const QString& text)
{
    QString html = text.toHtmlEscaped();
    static const QRegularExpression code(QStringLiteral("`([^`]+)`"));
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]+)\\]\\(([^)\\s]+)\\)"));
    static const QRegularExpression bold(QStringLiteral("\\*\\*(.+?)\\*\\*|__(.+?)__"));
    static const QRegularExpression italic(QStringLiteral("(?<![*\\w])\\*(?![\\s*])(.+?)(?<![\\s*])\\*(?![*\\w])"));
    static const QRegularExpression strike(QStringLiteral("~~(.+?)~~"));
    html.replace(code, QStringLiteral("<span style=\"font-family:Consolas\">\\1</span>"));
    html.replace(link, QStringLiteral("\\1"));
    html.replace(bold, QStringLiteral("<b>\\1\\2</b>"));
    html.replace(italic, QStringLiteral("<i>\\1</i>"));
    html.replace(strike, QStringLiteral("<s>\\1</s>"));
    return html;
}

bool isTableSeparator(const QString& line)
{
    static const QRegularExpression separator(QStringLiteral("^\\s*\\|?\\s*:?-{2,}:?\\s*(\\|\\s*:?-{2,}:?\\s*)*\\|?\\s*$"));
    return separator.match(line).hasMatch();
}

QStringList tableCells(QString line)
{
    line = line.trimmed();
    if (line.startsWith(QLatin1Char('|'))) line.remove(0, 1);
    if (line.endsWith(QLatin1Char('|'))) line.chop(1);
    QStringList cells = line.split(QLatin1Char('|'));
    for (QString& cell : cells) cell = cell.trimmed();
    return cells;
}

} // namespace

QString stripThinking(const QString& text)
{
    static const QRegularExpression think(QStringLiteral("<think>.*?</think>"),
                                          QRegularExpression::DotMatchesEverythingOption);
    QString result = text;
    result.remove(think);
    // An unfinished block (the stream stopped mid-thought) is not an answer either.
    const int open = result.indexOf(QStringLiteral("<think>"));
    if (open >= 0) result.truncate(open);
    return result;
}

QString markdownToHtml(const QString& markdown, MarkdownTarget target)
{
    QString source = stripThinking(markdown);
    source.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = source.split(QLatin1Char('\n'));
    static const QRegularExpression heading(QStringLiteral("^(#{1,6})\\s+(.*?)\\s*#*\\s*$"));
    static const QRegularExpression rule(QStringLiteral("^([-*_])(\\s*\\1){2,}$"));
    static const QRegularExpression bullet(QStringLiteral("^\\s*[-*+•·]\\s+(.*)$"));
    static const QRegularExpression numbered(QStringLiteral("^\\s*\\d{1,3}[.)．、]\\s*(.+)$"));
    static const QRegularExpression quote(QStringLiteral("^>\\s?(.*)$"));

    QString html;
    QString openList;
    const auto closeList = [&] {
        if (openList.isEmpty()) return;
        html += QStringLiteral("</") + openList + QLatin1Char('>');
        openList.clear();
    };
    const auto listItem = [&](const QString& tag, const QString& text) {
        if (openList != tag) {
            closeList();
            html += QLatin1Char('<') + tag + QLatin1Char('>');
            openList = tag;
        }
        html += QStringLiteral("<li>") + inlineHtml(text.trimmed()) + QStringLiteral("</li>");
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i];
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("```"))) {
            closeList();
            for (++i; i < lines.size() && !lines[i].trimmed().startsWith(QStringLiteral("```")); ++i)
                html += QStringLiteral("<p><span style=\"font-family:Consolas\">") + lines[i].toHtmlEscaped()
                    + QStringLiteral("</span></p>");
            continue;
        }
        if (trimmed.isEmpty()) {
            closeList();
            continue;
        }
        if (const auto match = heading.match(trimmed); match.hasMatch()) {
            closeList();
            const int level = match.captured(1).size();
            const QString text = inlineHtml(match.captured(2));
            if (target == MarkdownTarget::Document)
                html += QStringLiteral("<h%1>%2</h%1>").arg(level).arg(text);
            else
                html += QStringLiteral("<p><b>%1</b></p>").arg(text);
            continue;
        }
        if (rule.match(trimmed).hasMatch()) {
            closeList();
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('|')) && i + 1 < lines.size() && isTableSeparator(lines[i + 1])) {
            closeList();
            html += target == MarkdownTarget::Document
                ? QStringLiteral("<table border=\"1\" cellpadding=\"4\" style=\"border-collapse:collapse\">")
                : QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">");
            html += QStringLiteral("<tr>");
            for (const QString& cell : tableCells(trimmed)) html += QStringLiteral("<th>") + inlineHtml(cell) + QStringLiteral("</th>");
            html += QStringLiteral("</tr>");
            for (i += 2; i < lines.size() && lines[i].trimmed().startsWith(QLatin1Char('|')); ++i) {
                html += QStringLiteral("<tr>");
                for (const QString& cell : tableCells(lines[i])) html += QStringLiteral("<td>") + inlineHtml(cell) + QStringLiteral("</td>");
                html += QStringLiteral("</tr>");
            }
            --i;
            html += QStringLiteral("</table>");
            continue;
        }
        if (const auto match = bullet.match(line); match.hasMatch()) {
            listItem(QStringLiteral("ul"), match.captured(1));
            continue;
        }
        if (const auto match = numbered.match(line); match.hasMatch()) {
            listItem(QStringLiteral("ol"), match.captured(1));
            continue;
        }
        closeList();
        if (const auto match = quote.match(trimmed); match.hasMatch()) {
            html += QStringLiteral("<p>") + inlineHtml(match.captured(1)) + QStringLiteral("</p>");
            continue;
        }
        html += QStringLiteral("<p>") + inlineHtml(trimmed) + QStringLiteral("</p>");
    }
    closeList();
    return html;
}

QString cleanRewrite(const QString& text)
{
    QString result = stripThinking(text);
    result.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList lines = result.split(QLatin1Char('\n'));
    for (int i = lines.size() - 1; i >= 0; --i)
        if (lines[i].trimmed().startsWith(QStringLiteral("```"))) lines.removeAt(i);
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty()) lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
    // "以下是润色后的文字：" and the like, when more text follows.
    static const QRegularExpression intro(QStringLiteral(
        "^.{0,30}(以下|如下|润色|改写|修改|纠正|改正|翻译|精简|扩写|结果|版本|译文|Here)[^\\n]{0,30}[：:]\\s*$"));
    if (lines.size() > 1 && intro.match(lines.first().trimmed()).hasMatch()) {
        lines.removeFirst();
        while (!lines.isEmpty() && lines.first().trimmed().isEmpty()) lines.removeFirst();
    }
    static const QRegularExpression headingMark(QStringLiteral("^\\s*#{1,6}\\s+"));
    static const QRegularExpression emphasis(QStringLiteral("\\*\\*(.+?)\\*\\*|__(.+?)__"));
    // Markdown ends lines with two spaces to force a break; in the document they are stray spaces.
    static const QRegularExpression trailingSpace(QStringLiteral("[ \\t\\x{00A0}]+$"));
    for (QString& line : lines) {
        line.remove(headingMark);
        line.replace(emphasis, QStringLiteral("\\1\\2"));
        line.remove(trailingSpace);
    }
    result = lines.join(QLatin1Char('\n')).trimmed();
    // Quotes around the whole answer, when the answer itself contains no others.
    const QList<QPair<QChar, QChar>> quotes{{QChar(0x201C), QChar(0x201D)}, {QLatin1Char('"'), QLatin1Char('"')},
                                            {QChar(0x300C), QChar(0x300D)}};
    for (const auto& pair : quotes) {
        if (result.size() > 2 && result.startsWith(pair.first) && result.endsWith(pair.second)
            && result.count(pair.first) == (pair.first == pair.second ? 2 : 1)) {
            result = result.mid(1, result.size() - 2).trimmed();
            break;
        }
    }
    return result;
}

} // namespace openoffice
