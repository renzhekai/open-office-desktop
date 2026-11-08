#pragma once

#include <QString>

namespace openoffice {

// Where converted Markdown goes: the document (real heading styles, bordered
// tables) or a reply bubble (headings become bold lines at the bubble's size).
enum class MarkdownTarget { Document, Bubble };

// Converts the Markdown a model writes into the small HTML subset the editor's
// paste understands: headings, paragraphs, bold/italic/strike/code, ordered and
// unordered lists, pipe tables and fenced code. Every line that is not part of
// a list or table becomes its own paragraph, as office text expects. All text
// is HTML-escaped; links keep their text only; <think> blocks are dropped.
QString markdownToHtml(const QString& markdown, MarkdownTarget target = MarkdownTarget::Document);

// Text a rewrite should put back into the document: no Markdown emphasis or
// heading markers, no code fences, no quotes wrapped around the whole answer
// and no introductory line such as "以下是润色后的文字：".
QString cleanRewrite(const QString& text);

// Drops <think>…</think> reasoning that some models put into the answer.
QString stripThinking(const QString& text);

} // namespace openoffice
