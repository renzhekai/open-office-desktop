#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace openoffice {

// Where converted Markdown goes: the document (real heading styles, bordered
// tables) or a reply bubble (headings become bold lines at the bubble's size).
enum class MarkdownTarget { Document, Bubble };

struct DocumentStyle; // document_style.h

// Converts the Markdown a model writes into the small HTML subset the editor's
// paste understands: headings, paragraphs, bold/italic/strike/code, ordered and
// unordered lists, pipe tables and fenced code. Every line that is not part of
// a list or table becomes its own paragraph, as office text expects. All text
// is HTML-escaped; links keep their text only; <think> blocks are dropped.
// With a style (a chosen style pack) the look goes with it as inline styles: the first line
// as the title, coloured headings, body font, line height and indent, emphasis, tables in the
// pack's way (filled, light or three-line; banded; numbers on the right), and a closing sender
// and date on the right. Without one, the plain look (shaded header, default fonts).
QString markdownToHtml(const QString& markdown, MarkdownTarget target = MarkdownTarget::Document,
                       const DocumentStyle* style = nullptr);

// Text a rewrite should put back into the document: no Markdown emphasis or
// heading markers, no code fences, no quotes wrapped around the whole answer
// and no introductory line such as "以下是润色后的文字：".
QString cleanRewrite(const QString& text);

// Drops <think>…</think> reasoning that some models put into the answer.
QString stripThinking(const QString& text);
// The tables in a Markdown text, each made regular as markdownToHtml writes it: rows of
// cells (the first row is the header), every row as wide as the widest; lines of nothing
// but pipes are dropped, and the separator may come before or after the header.
QList<QList<QStringList>> markdownTables(const QString& markdown);
// HTML a model wrote where Markdown was meant (small models write <table><tr><td>…): its
// tables become Markdown tables, headings, bold, list items and paragraphs Markdown lines,
// other tags are dropped. Text without such tags comes back as it is.
QString htmlAsMarkdown(const QString& text);
// The first table in a Markdown text with `header` (the column names the person gave) as
// its header row, when the model left the header out: as many columns, none of the names in
// the first row, and that row clearly data (numbers in a column, as in the next row).
// Otherwise the text as it is.
QString withTableHeader(const QString& markdown, const QStringList& header);
// A drafted piece made ready to be laid out: its first line is the title ("# ", whatever
// level the model gave it, or a short line on its own such as "关于召开…的通知"), and the
// sections below start at "##" (a model that wrote "## 标题" and "### 一、…" gets "# " and "## ").
// Text with no such first line comes back as it is.
QString draftMarkdown(const QString& markdown);
// A cell's text as it reads in the document (no ** __ ` ~~).
QString plainCellText(const QString& cell);
// Headings, lists, bold or tables: text a model meant as Markdown even when it did not say so.
bool looksLikeMarkdown(const QString& text);
// After the model wrote text into the document, its answer often repeats it all.
// Drops the repeated part (and a "内容如下：" line before it); if nothing is left,
// one sentence saying it is in the document.
QString withoutEchoedText(const QString& reply, const QStringList& written);

} // namespace openoffice
