#include "app/RecentFiles.h"

#include <QFileInfo>
#include <QSettings>

namespace RecentFiles {

namespace {

const QString key = QStringLiteral("recentFiles");

void store(const QStringList &files)
{
    QSettings().setValue(key, files);
}

} // namespace

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

QStringList list()
{
    return QSettings().value(key).toStringList();
}

void add(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QStringList files = list();
    files.removeIf([&](const QString &file) { return samePath(file, absolute); });
    files.prepend(absolute);
    store(files.mid(0, maxCount));
}

void remove(const QString &path)
{
    QStringList files = list();
    files.removeIf([&](const QString &file) { return samePath(file, path); });
    store(files);
}

void clear()
{
    store({});
}

} // namespace RecentFiles
