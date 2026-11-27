#include "notes/Templates.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

namespace Templates {

QString qtDateFormat(const QString &momentFormat)
{
    // Longest tokens first. Qt quotes literal text with '...'.
    static const std::pair<const char *, const char *> tokens[] = {
        {"YYYY", "yyyy"}, {"YY", "yy"}, {"MMMM", "MMMM"}, {"MMM", "MMM"}, {"MM", "MM"}, {"M", "M"},
        {"DDDD", ""},     {"DD", "dd"}, {"Do", "d."},     {"D", "d"},     {"dddd", "dddd"}, {"ddd", "ddd"},
        {"HH", "HH"},     {"H", "H"},   {"hh", "hh"},     {"h", "h"},     {"mm", "mm"},     {"m", "m"},
        {"ss", "ss"},     {"s", "s"},   {"A", "AP"},      {"a", "ap"},
    };
    QString result;
    QString literal;
    const auto flushLiteral = [&] {
        if (literal.isEmpty())
            return;
        result += u'\'' + QString(literal).replace(u'\'', QStringLiteral("''")) + u'\'';
        literal.clear();
    };
    for (int i = 0; i < momentFormat.size();) {
        if (momentFormat[i] == u'[') { // [literal]
            const int close = int(momentFormat.indexOf(u']', i));
            if (close > i) {
                literal += momentFormat.mid(i + 1, close - i - 1);
                i = close + 1;
                continue;
            }
        }
        bool matched = false;
        for (const auto &[moment, qt] : tokens) {
            const QString token = QString::fromLatin1(moment);
            if (QStringView(momentFormat).mid(i).startsWith(token)) {
                flushLiteral();
                result += QString::fromLatin1(qt);
                i += int(token.size());
                matched = true;
                break;
            }
        }
        if (matched)
            continue;
        const QChar c = momentFormat[i++];
        if (c.isLetter())
            literal += c; // letters Qt would read as a format
        else {
            flushLiteral();
            result += c;
        }
    }
    flushLiteral();
    return result;
}

QString formatDate(const QDateTime &when, const QString &momentFormat)
{
    return QLocale().toString(when, qtDateFormat(momentFormat));
}

QString expand(const QString &text, const QString &title, const QDateTime &now)
{
    static const QRegularExpression variable(QStringLiteral("\\{\\{\\s*(title|date|time)\\s*(?::([^}]*))?\\}\\}"),
                                             QRegularExpression::CaseInsensitiveOption);
    QString result;
    qsizetype last = 0;
    auto it = variable.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += QStringView(text).mid(last, match.capturedStart() - last);
        const QString name = match.captured(1).toLower();
        const QString format = match.captured(2).trimmed();
        if (name == u"title")
            result += title;
        else if (name == u"date")
            result += formatDate(now, format.isEmpty() ? QStringLiteral("YYYY-MM-DD") : format);
        else
            result += formatDate(now, format.isEmpty() ? QStringLiteral("HH:mm") : format);
        last = match.capturedEnd();
    }
    result += QStringView(text).mid(last);
    return result;
}

QString dailyNotePath(const QString &root, const QString &folder, const QString &format, const QDate &date)
{
    const QString name = formatDate(QDateTime(date, QTime(12, 0)), format.isEmpty() ? QStringLiteral("YYYY-MM-DD") : format);
    QDir dir(root);
    if (!folder.trimmed().isEmpty())
        dir.setPath(QDir::isAbsolutePath(folder) ? folder : dir.filePath(folder.trimmed()));
    return QDir::cleanPath(dir.filePath(name + QStringLiteral(".md")));
}

QStringList templateFiles(const QString &folder)
{
    QStringList files;
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return files;
    QDirIterator it(folder, {QStringLiteral("*.md")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && files.size() < 500)
        files << it.next();
    files.sort(Qt::CaseInsensitive);
    return files;
}

} // namespace Templates
