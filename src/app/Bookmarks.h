#pragma once

#include <QStringList>

// Files and folders the user bookmarked, in the user's order, persisted in AppSettings (`bookmarks`), whose
// changed() signal tells the folder panel to refresh.
namespace Bookmarks {

QStringList list();
bool contains(const QString &path);
void add(const QString &path);
void remove(const QString &path);
void toggle(const QString &path);
void setOrder(const QStringList &paths);
// `from` (a file, or a folder with everything in it) is now at `to`.
void moved(const QString &from, const QString &to);
// `path` and everything in it is gone.
void removed(const QString &path);

} // namespace Bookmarks
