#pragma once

#include <QString>

class QMenu;
class QWidget;

// File management behind the right-click menus of the folder panel and the tabs. The disk work runs off
// the GUI thread and waits like loading a document does (see Background.h), so a hung network drive
// can't freeze the window.
namespace FileActions {

struct Result {
    bool ok = false;
    QString error;
    QString path; // the file created, renamed to or copied to
};

Result rename(const QString &from, const QString &to, QWidget *parent);
// Like rename, into another folder; a file on another drive is copied, then deleted.
Result move(const QString &from, const QString &to, QWidget *parent);
Result moveToTrash(const QString &path, QWidget *parent);
// For drives without a trash (USB sticks, network mounts): deletes a file, or a folder with its contents.
Result removePermanently(const QString &path, QWidget *parent);
Result createFile(const QString &path, QWidget *parent);
Result createFolder(const QString &path, QWidget *parent);
// Copies a file next to itself as "name 1.ext" (or the first free number).
Result duplicate(const QString &path, QWidget *parent);

// A name typed for a new or renamed file: empty if acceptable, otherwise why not.
QString checkName(const QString &name);

void openInDefaultApp(const QString &path);
// Opens the system file manager with `path` selected (just its folder where that isn't possible).
void showInFileManager(const QString &path);
QString showInFileManagerLabel();

// Adds Copy Path entries: full path, path relative to `root` (if inside it) and file name.
void addCopyPathMenu(QMenu *menu, const QString &path, const QString &root);

// `path` itself or something inside the folder `path`.
bool isSameOrInside(const QString &candidate, const QString &path);

} // namespace FileActions
