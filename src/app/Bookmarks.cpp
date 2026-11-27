#include "app/Bookmarks.h"

#include "app/AppSettings.h"
#include "app/FileActions.h"
#include "app/RecentFiles.h"

#include <QDir>
#include <QFileInfo>

namespace Bookmarks {

namespace {

void store(const QStringList &paths)
{
    AppSettings::instance().setValue(QLatin1String(AppSettings::bookmarksKey), paths);
}

} // namespace

QStringList list()
{
    return AppSettings::instance().value(QLatin1String(AppSettings::bookmarksKey)).toStringList();
}

bool contains(const QString &path)
{
    const QStringList paths = list();
    return std::any_of(paths.begin(), paths.end(), [&](const QString &p) { return RecentFiles::samePath(p, path); });
}

void add(const QString &path)
{
    if (!contains(path))
        store(list() << QFileInfo(path).absoluteFilePath());
}

void remove(const QString &path)
{
    QStringList paths = list();
    paths.removeIf([&](const QString &p) { return RecentFiles::samePath(p, path); });
    store(paths);
}

void toggle(const QString &path)
{
    if (contains(path))
        remove(path);
    else
        add(path);
}

void setOrder(const QStringList &paths)
{
    store(paths);
}

void moved(const QString &from, const QString &to)
{
    QStringList paths = list();
    bool changed = false;
    const int length = int(QDir::cleanPath(from).size());
    for (QString &path : paths) {
        if (FileActions::isSameOrInside(path, from)) {
            path = to + path.mid(length);
            changed = true;
        }
    }
    if (changed)
        store(paths);
}

void removed(const QString &path)
{
    QStringList paths = list();
    if (paths.removeIf([&](const QString &p) { return FileActions::isSameOrInside(p, path); }) > 0)
        store(paths);
}

} // namespace Bookmarks
