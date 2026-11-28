#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

// Obsidian's file recovery: copies of notes as they were opened and saved, kept for some days in the app's
// data folder (one folder per note, named by a hash of its path), so a note can be brought back after a bad
// edit or a sync accident. Everything here touches the disk: call it off the GUI thread.
namespace Snapshots {

struct Entry {
    QString file;   // the snapshot's own path
    QDateTime time; // when it was taken (local time)
    qint64 size = 0;
};

QString root();                          // <app data>/snapshots (tests point it elsewhere with setRoot)
void setRoot(const QString &folder);
// `base` (every call below takes it last): the snapshots folder, read with root() on the GUI thread and passed
// to background work, which then touches no shared state (a detached thread may outlive the app's statics).
// Empty: root().
QString folderFor(const QString &notePath, const QString &base = {});

// Newest first.
QList<Entry> list(const QString &notePath, const QString &base = {});
QString read(const Entry &entry);
// Keeps `text` as a snapshot of `notePath` unless it's what the newest one holds, or (without `force`) the
// newest is less than `minMinutes` old. Returns whether one was written.
bool record(const QString &notePath, const QString &text, int minMinutes, bool force, const QDateTime &now = {},
            const QString &base = {});
// Removes snapshots older than `days` (and folders left empty).
void prune(int days, const QDateTime &now = {}, const QString &base = {});

} // namespace Snapshots
