#include "app/FileActions.h"

#include "app/Background.h"
#include "app/RecentFiles.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QMenu>
#include <QProcess>
#include <QUrl>

namespace FileActions {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("FileActions", text);
}

QString label(const QString &path)
{
    return tr("Waiting for %1…").arg(QFileInfo(path).fileName());
}

Result waited(const std::optional<Result> &result)
{
    if (result)
        return *result;
    return {false, tr("Stopped waiting for the drive. The change may still happen later."), {}};
}

} // namespace

Result rename(const QString &from, const QString &to, QWidget *parent)
{
    return waited(runAndWait(
        [from, to]() -> Result {
            // A change of case only is the same file on Windows: let that through.
            if (QFileInfo::exists(to) && !RecentFiles::samePath(from, to))
                return {false, tr("%1 already exists.").arg(QFileInfo(to).fileName()), {}};
            if (!QDir().rename(from, to))
                return {false, tr("The file system refused the change."), {}};
            return {true, {}, QFileInfo(to).absoluteFilePath()};
        },
        label(from), parent));
}

Result move(const QString &from, const QString &to, QWidget *parent)
{
    return waited(runAndWait(
        [from, to]() -> Result {
            if (QFileInfo::exists(to) && !RecentFiles::samePath(from, to))
                return {false, tr("%1 already exists there.").arg(QFileInfo(to).fileName()), {}};
            if (QDir().rename(from, to))
                return {true, {}, QFileInfo(to).absoluteFilePath()};
            // Renaming doesn't cross drives: copy a file, then delete the original.
            if (QFileInfo(from).isDir())
                return {false, tr("Folders can't be moved to another drive."), {}};
            QFile file(from);
            if (!file.copy(to))
                return {false, file.errorString(), {}};
            if (!QFile::remove(from)) {
                QFile::remove(to);
                return {false, tr("The original couldn't be deleted, so nothing was moved."), {}};
            }
            return {true, {}, QFileInfo(to).absoluteFilePath()};
        },
        label(from), parent));
}

Result moveToTrash(const QString &path, QWidget *parent)
{
    return waited(runAndWait(
        [path]() -> Result {
            // Qt's error text here is often unrelated (e.g. "Bad file descriptor"): the usual cause is a
            // drive without a trash folder.
            if (!QFile::moveToTrash(path))
                return {false, tr("This drive has no trash, or the trash can't be used."), {}};
            return {true, {}, path};
        },
        label(path), parent));
}

Result removePermanently(const QString &path, QWidget *parent)
{
    return waited(runAndWait(
        [path]() -> Result {
            const QFileInfo info(path);
            const bool ok = info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively() : QFile::remove(path);
            if (!ok)
                return {false, tr("The file system refused to delete it."), {}};
            return {true, {}, path};
        },
        label(path), parent));
}

Result createFile(const QString &path, QWidget *parent)
{
    return waited(runAndWait(
        [path]() -> Result {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                return {false, QFileInfo::exists(path) ? tr("%1 already exists.").arg(QFileInfo(path).fileName())
                                                       : file.errorString(), {}};
            }
            return {true, {}, QFileInfo(path).absoluteFilePath()};
        },
        label(path), parent));
}

Result createFolder(const QString &path, QWidget *parent)
{
    return waited(runAndWait(
        [path]() -> Result {
            if (QFileInfo::exists(path))
                return {false, tr("%1 already exists.").arg(QFileInfo(path).fileName()), {}};
            if (!QDir().mkdir(path))
                return {false, tr("The folder could not be created."), {}};
            return {true, {}, QFileInfo(path).absoluteFilePath()};
        },
        label(path), parent));
}

Result duplicate(const QString &path, QWidget *parent)
{
    return waited(runAndWait(
        [path]() -> Result {
            const QFileInfo info(path);
            const QString suffix = info.suffix().isEmpty() ? QString() : u'.' + info.suffix();
            const QString base = info.dir().filePath(info.completeBaseName());
            for (int n = 1; n < 1000; ++n) {
                const QString copy = QStringLiteral("%1 %2%3").arg(base).arg(n).arg(suffix);
                if (QFileInfo::exists(copy))
                    continue;
                QFile file(path);
                if (!file.copy(copy))
                    return {false, file.errorString(), {}};
                return {true, {}, copy};
            }
            return {false, tr("Too many copies already."), {}};
        },
        label(path), parent));
}

QString checkName(const QString &name)
{
    if (name.trimmed().isEmpty())
        return tr("The name is empty.");
    if (name == u"." || name == u"..")
        return tr("That name is reserved.");
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : name) {
        // Windows forbids all of them; elsewhere they would at least make the note hard to sync or link.
        if (forbidden.contains(c) || c.unicode() < 32)
            return tr("A name can't contain any of these characters: %1").arg(forbidden);
    }
    return {};
}

void openInDefaultApp(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString showInFileManagerLabel()
{
#if defined(Q_OS_WIN)
    return tr("Show in E&xplorer");
#elif defined(Q_OS_MACOS)
    return tr("Sho&w in Finder");
#else
    return tr("Sho&w in File Manager");
#endif
}

void showInFileManager(const QString &path)
{
    const QString folder = QFileInfo(path).absolutePath();
#if defined(Q_OS_WIN)
    if (!QProcess::startDetached(QStringLiteral("explorer.exe"), {u"/select," + QDir::toNativeSeparators(path)}))
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
#elif defined(Q_OS_MACOS)
    if (!QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path}))
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
#else
    // The freedesktop file manager interface selects the item (Nautilus, Dolphin, Nemo, Thunar...). Without
    // a file manager that implements it, open the folder instead.
    auto *process = new QProcess;
    const auto fallback = [process, folder](bool failed) {
        if (failed)
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        process->deleteLater();
    };
    QObject::connect(process, &QProcess::finished, process,
                     [fallback](int code, QProcess::ExitStatus status) { fallback(code != 0 || status != QProcess::NormalExit); });
    QObject::connect(process, &QProcess::errorOccurred, process, [fallback](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fallback(true);
    });
    process->start(QStringLiteral("dbus-send"),
                   {QStringLiteral("--session"), QStringLiteral("--print-reply"),
                    QStringLiteral("--dest=org.freedesktop.FileManager1"), QStringLiteral("--type=method_call"),
                    QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
                    u"array:string:" + QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded), QStringLiteral("string:")});
#endif
}

void addCopyPathMenu(QMenu *menu, const QString &path, const QString &root)
{
    const auto copy = [](const QString &text) { QGuiApplication::clipboard()->setText(text); };
    const QString native = QDir::toNativeSeparators(path);
    menu->addAction(tr("&Full Path"), menu, [copy, native] { copy(native); });
    if (!root.isEmpty() && isSameOrInside(path, root) && !RecentFiles::samePath(path, root)) {
        const QString relative = QDir(root).relativeFilePath(path); // forward slashes, as Markdown links use
        menu->addAction(tr("&Relative to Folder"), menu, [copy, relative] { copy(relative); });
    }
    const QString name = QFileInfo(path).fileName();
    menu->addAction(tr("File &Name"), menu, [copy, name] { copy(name); });
}

bool isSameOrInside(const QString &candidate, const QString &path)
{
    if (candidate.isEmpty() || path.isEmpty())
        return false;
    if (RecentFiles::samePath(candidate, path))
        return true;
    const QString prefix = QDir::cleanPath(path) + u'/';
    const QString cleaned = QDir::cleanPath(candidate);
    return RecentFiles::samePath(cleaned.left(prefix.size()), prefix);
}

} // namespace FileActions
