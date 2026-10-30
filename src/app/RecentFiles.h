#pragma once

#include <QStringList>

// Most recently opened or saved files, newest first, persisted in QSettings.
namespace RecentFiles {

constexpr int maxCount = 10;

QStringList list();
void add(const QString &path);
void remove(const QString &path);
void clear();

// Path comparison that follows the file system's case sensitivity.
bool samePath(const QString &a, const QString &b);

} // namespace RecentFiles
