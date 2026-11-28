#include "notes/Snapshots.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>

namespace Snapshots {

namespace {

const QString timeFormat = QStringLiteral("yyyyMMdd-HHmmss");

QMutex &rootLock()
{
    static QMutex mutex;
    return mutex;
}

QString &rootFolder()
{
    static QString folder;
    return folder;
}

} // namespace

QString root()
{
    QMutexLocker lock(&rootLock());
    if (rootFolder().isEmpty())
        rootFolder() = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/snapshots");
    return rootFolder();
}

void setRoot(const QString &folder)
{
    QMutexLocker lock(&rootLock());
    rootFolder() = folder;
}

QString folderFor(const QString &notePath, const QString &base)
{
    const QByteArray hash =
        QCryptographicHash::hash(QDir::cleanPath(notePath).toUtf8(), QCryptographicHash::Sha1).toHex().left(20);
    return (base.isEmpty() ? root() : base) + u'/' + QString::fromLatin1(hash);
}

QList<Entry> list(const QString &notePath, const QString &base)
{
    QList<Entry> entries;
    const QDir folder(folderFor(notePath, base));
    for (const QFileInfo &info : folder.entryInfoList({QStringLiteral("*.md")}, QDir::Files, QDir::Name | QDir::Reversed)) {
        const QDateTime time = QDateTime::fromString(info.completeBaseName().left(15), timeFormat);
        if (time.isValid())
            entries.append({info.absoluteFilePath(), time, info.size()});
    }
    return entries;
}

QString read(const Entry &entry)
{
    QFile file(entry.file);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool record(const QString &notePath, const QString &text, int minMinutes, bool force, const QDateTime &now,
            const QString &base)
{
    const QDateTime when = now.isValid() ? now : QDateTime::currentDateTime();
    const QList<Entry> entries = list(notePath, base);
    if (!entries.isEmpty()) {
        if (read(entries.front()) == text)
            return false; // nothing new
        if (!force && entries.front().time.secsTo(when) < minMinutes * 60)
            return false;
    }
    QDir folder(folderFor(notePath, base));
    if (!folder.mkpath(QStringLiteral(".")))
        return false;
    // Which note it was, for people looking in the folder.
    QFile name(folder.filePath(QStringLiteral("path.txt")));
    if (!name.exists() && name.open(QIODevice::WriteOnly))
        name.write(QDir::cleanPath(notePath).toUtf8());
    QString file = folder.filePath(when.toString(timeFormat) + QStringLiteral(".md"));
    for (int n = 2; QFile::exists(file); ++n)
        file = folder.filePath(when.toString(timeFormat) + QStringLiteral("-%1.md").arg(n));
    QFile out(file);
    return out.open(QIODevice::WriteOnly) && out.write(text.toUtf8()) >= 0;
}

void prune(int days, const QDateTime &now, const QString &base)
{
    const QDateTime limit = (now.isValid() ? now : QDateTime::currentDateTime()).addDays(-days);
    const QDir top(base.isEmpty() ? root() : base);
    for (const QFileInfo &folder : top.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir dir(folder.absoluteFilePath());
        bool any = false;
        for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.md")}, QDir::Files)) {
            const QDateTime time = QDateTime::fromString(info.completeBaseName().left(15), timeFormat);
            if (time.isValid() && time < limit)
                QFile::remove(info.absoluteFilePath());
            else
                any = true;
        }
        if (!any)
            dir.removeRecursively();
    }
}

} // namespace Snapshots
