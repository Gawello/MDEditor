#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

// Daily notes and templates, as in Obsidian's core plugins. Dates use Obsidian's (moment.js) format tokens.
namespace Templates {

// "YYYY-MM-DD dddd HH:mm" -> Qt's "yyyy-MM-dd dddd HH:mm". Text in [brackets] stays literal.
QString qtDateFormat(const QString &momentFormat);
QString formatDate(const QDateTime &when, const QString &momentFormat);

// Replaces {{title}}, {{date}}, {{time}} and {{date:FORMAT}} / {{time:FORMAT}}.
QString expand(const QString &text, const QString &title, const QDateTime &now);

// The daily note for `date`: <root>/<folder>/<date in format>.md.
QString dailyNotePath(const QString &root, const QString &folder, const QString &format, const QDate &date);

// The Markdown files in `folder` and below, sorted.
QStringList templateFiles(const QString &folder);

} // namespace Templates
