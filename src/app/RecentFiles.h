#pragma once

#include <QStringList>

// Most recently opened or saved files, newest first, persisted in QSettings.
namespace RecentFiles {

constexpr int maxCount = 10;

QStringList list();
void add(const QString &path);
void remove(const QString &path);
void clear();

// Folders opened in the folder panel, newest first (File → Open Recent).
QStringList folders();
void addFolder(const QString &path);
void clearFolders();

// Path comparison that follows the file system's case sensitivity.
bool samePath(const QString &a, const QString &b);

} // namespace RecentFiles
