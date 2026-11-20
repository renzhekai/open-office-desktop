#include "markdown_html.h"

#include <QSet>

#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

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

bool tableLine(const QString& line)
{
    const QString trimmed = line.trimmed();
    return !trimmed.isEmpty() && (trimmed.startsWith(QLatin1Char('|')) || isTableSeparator(trimmed)
                                  || (trimmed.count(QLatin1Char('|')) >= 1 && !trimmed.startsWith(QLatin1Char('#'))));
}

// From line `start`, the lines of one table and the table made regular; false when they are not one.
bool readTable(const QStringList& lines, int start, int* end, QList<QStringList>* rows)
{
    int i = start;
    bool separator = false;
    bool piped = true; // every line starts with "|"
    QList<QStringList> found;
    for (; i < lines.size() && tableLine(lines[i]); ++i) {
        const QString trimmed = lines[i].trimmed();
        piped = piped && (trimmed.startsWith(QLatin1Char('|')) || isTableSeparator(trimmed));
        if (isTableSeparator(trimmed)) {
            separator = true;
            continue;
        }
        QStringList cells = tableCells(trimmed);
        // A line of nothing but pipes and dashes ("|", "| | |", "| - | |") is no row: small
        // models write one before the header.
        static const QRegularExpression blank(QStringLiteral("^[\\s:\\-\u2013\u2014_]*$"));
        if (std::all_of(cells.begin(), cells.end(), [](const QString& cell) { return blank.match(cell).hasMatch(); })) continue;
        found.append(cells);
    }
    // A table has a separator line, or (without one) at least two rows written with pipes.
    if (found.isEmpty() || (!separator && (!piped || found.size() < 2))) return false;
    // As many columns as the header has (a row with a stray "|" does not widen the table);
    // a header of one cell takes the widest row's.
    int columns = int(found.first().size());
    if (columns < 2)
        for (const QStringList& row : found) columns = qMax(columns, int(row.size()));
    if (columns < 2 && found.size() < 2) return false;
    columns = qMin(columns, 20);
    for (QStringList& row : found) {
        while (row.size() < columns) row.append(QString());
        while (row.size() > columns) {
            // Extra cells (a "|" inside a cell) go back into the last one.
            const QString extra = row.takeLast();
            if (!extra.isEmpty()) row.last() += QStringLiteral(" | ") + extra;
        }
    }
    *end = i;
    *rows = found;
    return true;
}

constexpr auto kHtmlOptions = QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption;

QString withoutEntities(QString text)
{
    static const QRegularExpression number(QStringLiteral("&#(x?)([0-9a-fA-F]{1,6});"));
    QString result;
    int last = 0;
    for (auto it = number.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        result += text.mid(last, match.capturedStart() - last);
        bool ok = false;
        const uint code = match.captured(2).toUInt(&ok, match.captured(1).isEmpty() ? 10 : 16);
        if (ok && code > 0 && code <= 0x10FFFF) {
            const char32_t character = code;
            result += QString::fromUcs4(&character, 1);
        }
        last = match.capturedEnd();
    }
    result += text.mid(last);
    result.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    result.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    result.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    result.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    result.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    result.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return result;
}

// A cell's text: no tags, entities read, one line, and no "|" that would split it.
QString htmlCellText(QString cell)
{
    static const QRegularExpression br(QStringLiteral("<br\\s*/?>"), kHtmlOptions);
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    cell.replace(br, QStringLiteral(" "));
    cell.remove(tag);
    cell = withoutEntities(cell);
    cell.replace(QLatin1Char('|'), QChar(0xFF5C));
    return cell.simplified();
}

// HTML outside tables as Markdown lines: headings, bold, list items and paragraphs.
QString htmlBlocksAsMarkdown(QString html)
{
    static const QRegularExpression heading(QStringLiteral("<h([1-6])\\b[^>]*>(.*?)</h\\1\\s*>"), kHtmlOptions);
    static const QRegularExpression strong(QStringLiteral("<(b|strong)\\b[^>]*>(.*?)</\\1\\s*>"), kHtmlOptions);
    static const QRegularExpression item(QStringLiteral("<li\\b[^>]*>"), kHtmlOptions);
    static const QRegularExpression lineEnd(QStringLiteral("<br\\s*/?>|</(p|div|li|h[1-6])\\s*>"), kHtmlOptions);
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    QString text;
    int last = 0;
    for (auto it = heading.globalMatch(html); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        text += html.mid(last, match.capturedStart() - last);
        text += QStringLiteral("\n") + QString(match.captured(1).toInt(), QLatin1Char('#')) + QLatin1Char(' ')
              + match.captured(2) + QLatin1Char('\n');
        last = match.capturedEnd();
    }
    text += html.mid(last);
    text.replace(strong, QStringLiteral("**\\2**"));
    text.replace(item, QStringLiteral("\n- "));
    text.replace(lineEnd, QStringLiteral("\n"));
    text.remove(tag);
    text = withoutEntities(text);
    QStringList lines;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) lines.append(trimmed);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

QList<QList<QStringList>> markdownTables(const QString& markdown)
{
    QString source = stripThinking(markdown);
    source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList lines = source.split(QLatin1Char('\n'));
    QList<QList<QStringList>> tables;
    for (int i = 0; i < lines.size(); ++i) {
        int end = i;
        QList<QStringList> rows;
        if (tableLine(lines[i]) && readTable(lines, i, &end, &rows)) {
            tables.append(rows);
            i = end - 1;
        }
    }
    return tables;
}

QString htmlAsMarkdown(const QString& text)
{
    static const QRegularExpression html(QStringLiteral("<(table|tr|td|th|p|br|h[1-6]|li|ul|ol|div|strong|b)\\b[^>]*>"),
                                         QRegularExpression::CaseInsensitiveOption);
    if (!html.match(text).hasMatch()) return text;
    // A <tr> or cell may be left open (the next one starts it again); the table may end with the text.
    static const QRegularExpression table(QStringLiteral("<table\\b[^>]*>(.*?)(?:</table\\s*>|$)"), kHtmlOptions);
    static const QRegularExpression row(QStringLiteral("<tr\\b[^>]*>(.*?)(?=<tr\\b|</table|$)"), kHtmlOptions);
    static const QRegularExpression cell(QStringLiteral("<t[hd]\\b[^>]*>(.*?)(?=<t[hd]\\b|</tr|$)"), kHtmlOptions);
    const auto line = [](const QStringList& cells) {
        return QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |");
    };
    QStringList parts;
    int last = 0;
    for (auto it = table.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        QList<QStringList> rows;
        for (auto r = row.globalMatch(match.captured(1)); r.hasNext();) {
            const QString rowHtml = r.next().captured(1);
            QStringList cells;
            for (auto c = cell.globalMatch(rowHtml); c.hasNext();) cells.append(htmlCellText(c.next().captured(1)));
            if (!cells.isEmpty()) rows.append(cells);
        }
        if (rows.isEmpty()) continue; // no rows: left as it is
        parts.append(htmlBlocksAsMarkdown(text.mid(last, match.capturedStart() - last)));
        QStringList lines{line(rows.first()), line(QStringList(rows.first().size(), QStringLiteral("---")))};
        for (int i = 1; i < rows.size(); ++i) lines.append(line(rows[i]));
        parts.append(lines.join(QLatin1Char('\n')));
        last = match.capturedEnd();
    }
    parts.append(htmlBlocksAsMarkdown(text.mid(last)));
    parts.removeAll(QString());
    return parts.join(QStringLiteral("\n\n"));
}

QString withTableHeader(const QString& markdown, const QStringList& header)
{
    if (header.size() < 2) return markdown;
    QString source = markdown;
    source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QStringList lines = source.split(QLatin1Char('\n'));
    static const QRegularExpression digit(QStringLiteral("\\d"));
    const auto line = [](const QStringList& cells) {
        return QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |");
    };
    for (int i = 0; i < lines.size(); ++i) {
        int end = i;
        QList<QStringList> rows;
        if (!tableLine(lines[i]) || !readTable(lines, i, &end, &rows)) continue;
        // The first table only, as wide as the names, with a first row that is data: none of
        // the names in it, and a column with numbers in it and in the next row (张三 | 138…).
        if (rows.size() < 2 || rows.first().size() != header.size()) return markdown;
        for (const QString& cell : rows.first())
            if (header.contains(plainCellText(cell))) return markdown;
        bool data = false;
        for (int c = 0; c < header.size(); ++c)
            data = data || (rows[0][c].contains(digit) && rows[1][c].contains(digit));
        if (!data) return markdown;
        QStringList table{line(header), line(QStringList(header.size(), QStringLiteral("---")))};
        for (const QStringList& row : rows) table.append(line(row));
        lines.remove(i, end - i);
        for (int k = 0; k < table.size(); ++k) lines.insert(i + k, table[k]);
        return lines.join(QLatin1Char('\n'));
    }
    return markdown;
}

QString plainCellText(const QString& cell)
{
    QString text = cell;
    static const QRegularExpression marks(QStringLiteral("\\*\\*|__|`|~~"));
    text.remove(marks);
    return text.simplified();
}

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
        int tableEnd = i;
        QList<QStringList> rows;
        if (tableLine(line) && readTable(lines, i, &tableEnd, &rows)) {
            // Made regular first (a stray "|" line, the header after the separator, uneven rows),
            // then written as a page-wide table with a shaded header: without a width the
            // editor squeezes every column to its narrowest (measured in the real editor).
            closeList();
            html += target == MarkdownTarget::Document
                ? QStringLiteral("<table border=\"1\" cellpadding=\"5\" style=\"border-collapse:collapse;width:100%\">")
                : QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">");
            for (int r = 0; r < rows.size(); ++r) {
                html += QStringLiteral("<tr>");
                for (const QString& cell : rows[r]) {
                    if (r == 0 && target == MarkdownTarget::Document)
                        html += QStringLiteral("<th style=\"background-color:#D9E2F3;font-weight:bold;width:%1%\">")
                                    .arg(100 / qMax(1, int(rows.first().size()))) + inlineHtml(cell) + QStringLiteral("</th>");
                    else if (r == 0)
                        html += QStringLiteral("<th>") + inlineHtml(cell) + QStringLiteral("</th>");
                    else
                        html += QStringLiteral("<td>") + inlineHtml(cell) + QStringLiteral("</td>");
                }
                html += QStringLiteral("</tr>");
            }
            html += QStringLiteral("</table>");
            i = tableEnd - 1;
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
    // A closing note about the answer ("（注：以上内容……）") is not part of the text.
    static const QRegularExpression note(QStringLiteral("^[（(]?\\s*(注|备注|说明|提示)\\s*[：:]"));
    while (lines.size() > 1 && (lines.last().trimmed().isEmpty() || note.match(lines.last().trimmed()).hasMatch()))
        lines.removeLast();
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

namespace {

// A line without Markdown marks, list numbers or leading symbols, for comparing.
QString plainLine(const QString& line)
{
    static const QRegularExpression marks(QStringLiteral("^[\\s#>*+\\-]*(\\d+[.、)）]\\s*)?"));
    QString text = line;
    text.remove(marks);
    text.remove(QStringLiteral("**"));
    text.remove(QStringLiteral("__"));
    while (!text.isEmpty() && (text.front().isPunct() || text.front().isSymbol() || text.front().isSpace()))
        text.remove(0, 1);
    return text.simplified();
}

bool markdownBlockLine(const QString& line)
{
    static const QRegularExpression block(QStringLiteral("^\\s*(#{1,6}\\s|[-*+]\\s|\\d+[.、)]\\s|\\|)"));
    return block.match(line).hasMatch();
}

} // namespace

bool looksLikeMarkdown(const QString& text)
{
    static const QRegularExpression heading(QStringLiteral("(^|\\n)\\s*#{1,6}\\s+\\S"));
    static const QRegularExpression list(QStringLiteral("(^|\\n)\\s*([-*+]|\\d+[.、)])\\s+\\S"));
    static const QRegularExpression strong(QStringLiteral("\\*\\*[^*\\n]+\\*\\*"));
    static const QRegularExpression table(QStringLiteral("(^|\\n)\\s*\\|?\\s*:?-{3,}"));
    if (heading.match(text).hasMatch() || strong.match(text).hasMatch() || table.match(text).hasMatch()) return true;
    int items = 0;
    for (auto it = list.globalMatch(text); it.hasNext(); it.next()) ++items;
    return items >= 2;
}

QString withoutEchoedText(const QString& reply, const QStringList& written)
{
    QSet<QString> writtenLines;
    for (const QString& text : written)
        for (const QString& line : text.split(QLatin1Char('\n')))
            if (const QString plain = plainLine(line); plain.size() >= 6) writtenLines.insert(plain);
    if (writtenLines.isEmpty()) return reply;
    const QStringList lines = reply.split(QLatin1Char('\n'));
    int firstEcho = -1;
    int echoed = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const QString plain = plainLine(lines[i]);
        if (plain.size() < 6 || !writtenLines.contains(plain)) continue;
        ++echoed;
        if (firstEcho < 0) firstEcho = i;
    }
    if (echoed < 2 && !(echoed == 1 && writtenLines.size() == 1)) return reply;
    // Keep what comes before the repeated text and before any heading or list leading into it.
    QStringList kept;
    for (int i = 0; i < firstEcho && !markdownBlockLine(lines[i]); ++i) kept << lines[i];
    while (!kept.isEmpty() && kept.last().trimmed().isEmpty()) kept.removeLast();
    if (!kept.isEmpty() && (kept.last().trimmed().endsWith(QChar(0xFF1A)) || kept.last().trimmed().endsWith(QLatin1Char(':'))))
        kept.removeLast(); // "内容如下：" announced the part that is gone
    const QString head = kept.join(QLatin1Char('\n')).trimmed();
    return head.isEmpty() ? QStringLiteral("已经写进文档了，可以在文档里查看。需要调整的话告诉我。") : head;
}

} // namespace openoffice
