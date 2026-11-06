#include "app/CloudDrive.h"

#include "app/AppSettings.h"
#include "app/Background.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>

#include <utility>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <csignal>
#endif

namespace {

constexpr int pollInterval = 250;

#ifndef Q_OS_WIN
bool listedAsMount(const QString &path)
{
#ifdef Q_OS_LINUX
    // The mount table itself: QStorageInfo stats the path, which fails on the dead mount this must find.
    QFile table(QStringLiteral("/proc/self/mountinfo"));
    if (!table.open(QIODevice::ReadOnly))
        return false;
    const QString target = QDir::cleanPath(path);
    const QStringList lines = QString::fromUtf8(table.readAll()).split(u'\n');
    for (const QString &line : lines) {
        // "36 35 98:0 / /mount/point rw,noatime - fuse.rclone remote: rw"; spaces in the path are \\040.
        const QStringList before = line.section(QStringLiteral(" - "), 0, 0).split(u' ');
        const QString type = line.section(QStringLiteral(" - "), 1, 1).section(u' ', 0, 0);
        if (before.size() > 4 && type.contains(QStringLiteral("rclone"))
            && QString(before[4]).replace(QStringLiteral("\\040"), QStringLiteral(" ")) == target) {
            return true;
        }
    }
    return false;
#else
    const QStorageInfo storage(path);
    return storage.isValid() && storage.fileSystemType().contains("rclone")
        && QDir::cleanPath(storage.rootPath()) == QDir::cleanPath(path);
#endif
}
#endif

bool isRcloneMount(const QString &path)
{
#ifdef Q_OS_WIN
    return QFileInfo::exists(path); // rclone creates the mount folder itself there, and needs it absent
#else
    // A mount whose rclone died stays in the mount table, but the folder can't be read.
    return listedAsMount(path) && QFileInfo(path).isDir();
#endif
}

#ifndef Q_OS_WIN
void clearDeadMount(const QString &path)
{
    if (!listedAsMount(path) || QFileInfo(path).isDir())
        return;
#ifdef Q_OS_MACOS
    QProcess::execute(QStringLiteral("umount"), {QStringLiteral("-f"), path});
#else
    QString unmount = QStandardPaths::findExecutable(QStringLiteral("fusermount3"));
    if (unmount.isEmpty())
        unmount = QStringLiteral("fusermount");
    QProcess::execute(unmount, {QStringLiteral("-uz"), path});
#endif
}
#endif

bool processGone(qint64 pid)
{
#ifdef Q_OS_UNIX
    return pid > 0 && ::kill(pid_t(pid), 0) != 0 && errno == ESRCH;
#else
    Q_UNUSED(pid);
    return false; // no cheap check: the timeout reports the failure
#endif
}

} // namespace

CloudDrive::CloudDrive(QObject *parent)
    : QObject(parent)
    , m_probe(isRcloneMount)
    , m_poll(new QTimer(this))
{
    m_poll->setInterval(pollInterval);
    connect(m_poll, &QTimer::timeout, this, &CloudDrive::pollMount);
}

QString CloudDrive::remoteFolder() const
{
    return AppSettings::instance().value(AppSettings::driveFolderKey).toString();
}

QString CloudDrive::mountPoint() const
{
    return AppSettings::instance()
        .value(AppSettings::driveMountPointKey, QDir::home().filePath(QStringLiteral("GoogleDrive")))
        .toString();
}

bool CloudDrive::connectOnStart() const
{
    return AppSettings::instance().value(AppSettings::driveConnectOnStartKey, true).toBool();
}

void CloudDrive::setRemoteFolder(const QString &folder)
{
    QString clean = folder.trimmed();
    clean.replace(u'\\', u'/');
    while (clean.startsWith(u'/'))
        clean.remove(0, 1);
    while (clean.endsWith(u'/'))
        clean.chop(1);
    AppSettings::instance().setValue(AppSettings::driveFolderKey, clean);
}

void CloudDrive::setMountPoint(const QString &path)
{
    AppSettings::instance().setValue(AppSettings::driveMountPointKey, QDir::cleanPath(path.trimmed()));
}

void CloudDrive::setConnectOnStart(bool enabled)
{
    AppSettings::instance().setValue(AppSettings::driveConnectOnStartKey, enabled);
}

bool CloudDrive::contains(const QString &path) const
{
    const QString root = QDir::cleanPath(mountPoint());
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return clean == root || clean.startsWith(root + u'/');
}

void CloudDrive::setUnmountCommand(const QString &program, const QStringList &arguments)
{
    m_unmountProgram = program;
    m_unmountArguments = arguments;
}

QString CloudDrive::program() const
{
    const QString configured = AppSettings::instance().value(AppSettings::driveRclonePathKey).toString();
    return configured.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("rclone")) : configured;
}

QString CloudDrive::remote() const
{
    return QLatin1String(remoteName) + u':' + remoteFolder();
}

QString CloudDrive::logPath() const
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("rclone-mount.log"));
}

QString CloudDrive::logTail() const
{
    QFile file(logPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QStringList lines = QString::fromUtf8(file.readAll()).trimmed().split(u'\n');
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

void CloudDrive::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void CloudDrive::run(const QStringList &arguments, std::function<void(int, const QByteArray &)> done)
{
    auto *process = new QProcess(this);
    process->setStandardErrorFile(QProcess::nullDevice());
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            done(-1, {});
            process->deleteLater();
        }
    });
    connect(process, &QProcess::finished, this, [process, done](int exitCode) {
        done(exitCode, process->readAllStandardOutput());
        process->deleteLater();
    });
    process->start(program(), arguments);
    process->closeWriteChannel();
}

void CloudDrive::start()
{
    m_connectWhenReady = connectOnStart();
    refresh();
}

void CloudDrive::refresh()
{
    if (m_state == State::SigningIn || m_state == State::Connecting || m_state == State::Disconnecting)
        return;
    if (program().isEmpty()) {
        setState(State::NoRclone);
        return;
    }
    // Not `listremotes`: an abandoned login leaves the remote there, without a token.
    run({QStringLiteral("config"), QStringLiteral("dump")}, [this](int exitCode, const QByteArray &output) {
        if (exitCode < 0) {
            setState(State::NoRclone);
            return;
        }
        const QJsonObject remote = QJsonDocument::fromJson(output).object().value(QLatin1String(remoteName)).toObject();
        afterSignInCheck(!remote.value(QStringLiteral("token")).toString().isEmpty());
    });
}

void CloudDrive::afterSignInCheck(bool signedIn)
{
    if (!signedIn) {
        m_connectWhenReady = false;
        setState(State::SignedOut);
        return;
    }
    probeMount([this](bool mounted) {
        const bool automatic = std::exchange(m_connectWhenReady, false);
        if (mounted) {
            setState(State::Connected);
            if (automatic)
                emit connected(mountPoint(), true);
            return;
        }
        setState(State::SignedIn);
        if (automatic) {
            m_automatic = true;
            connectDrive();
        }
    });
}

void CloudDrive::probeMount(std::function<void(bool)> done)
{
    // A dead mount can hang any call that touches it.
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [watcher, done] {
        done(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(runInBackground([probe = m_probe, path = mountPoint()] { return probe(path); }));
}

void CloudDrive::signIn()
{
    if (m_signIn || program().isEmpty())
        return;
    setState(State::SigningIn);

    m_signIn = new QProcess(this);
    m_signIn->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_signIn, &QProcess::readyRead, this, [this] {
        static const QRegularExpression link(QStringLiteral(R"(https?://127\.0\.0\.1:\d+/\S+)"));
        const QRegularExpressionMatch match = link.match(QString::fromUtf8(m_signIn->readAll()));
        if (match.hasMatch())
            emit signInLinkReady(match.captured());
    });
    connect(m_signIn, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_signIn->deleteLater();
        m_signIn = nullptr;
        setState(State::NoRclone);
    });
    connect(m_signIn, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        const bool cancelled = status != QProcess::NormalExit;
        m_signIn->deleteLater();
        m_signIn = nullptr;
        if (cancelled) {
            deleteRemote(); // don't leave the empty remote of an abandoned login behind
            return;
        }
        setState(State::Checking);
        run({QStringLiteral("config"), QStringLiteral("dump")}, [this, exitCode](int, const QByteArray &output) {
            const QJsonObject remote = QJsonDocument::fromJson(output).object().value(QLatin1String(remoteName)).toObject();
            const bool signedIn = exitCode == 0 && !remote.value(QStringLiteral("token")).toString().isEmpty();
            if (!signedIn) {
                emit failed(tr("Signing in to Google didn't finish."));
                deleteRemote();
                return;
            }
            setState(State::SignedIn);
        });
    });
    // The whole drive, not just files MDEditor created: the notes are already there.
    m_signIn->start(program(), {QStringLiteral("config"), QStringLiteral("create"), QLatin1String(remoteName),
                                QStringLiteral("drive"), QStringLiteral("scope=drive")});
    m_signIn->closeWriteChannel();
}

void CloudDrive::cancelSignIn()
{
    if (m_signIn)
        m_signIn->kill();
}

void CloudDrive::deleteRemote()
{
    run({QStringLiteral("config"), QStringLiteral("delete"), QLatin1String(remoteName)},
        [this](int, const QByteArray &) { setState(State::SignedOut); });
}

void CloudDrive::connectDrive()
{
    if (m_state != State::SignedIn)
        return;
    const QString path = mountPoint();
    const auto fail = [this](const QString &message) {
        m_automatic = false;
        emit failed(message);
    };
    if (path.isEmpty()) {
        fail(tr("Choose a local folder for Google Drive first."));
        return;
    }
#ifdef Q_OS_WIN
    if (QFileInfo::exists(path)) {
        fail(tr("%1 already exists. rclone creates this folder itself, so choose one that doesn't exist yet.")
                 .arg(QDir::toNativeSeparators(path)));
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
#else
    clearDeadMount(path); // left by a crash, and it would block both of the checks below
    if (!QDir().mkpath(path)) {
        fail(tr("Cannot create the folder %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (!QDir(path).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        fail(tr("%1 isn't empty. Choose an empty folder: the drive would hide what's in it.")
                 .arg(QDir::toNativeSeparators(path)));
        return;
    }
#endif

    QProcess process;
    process.setProgram(program());
    process.setArguments({QStringLiteral("mount"), remote(), path,
                          QStringLiteral("--vfs-cache-mode"), QStringLiteral("writes"), // safe saves need it
                          QStringLiteral("--vfs-write-back"), QStringLiteral("1s"),
                          QStringLiteral("--dir-cache-time"), QStringLiteral("30s")}); // see other computers' changes
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(logPath());
    if (!process.startDetached(&m_mountPid)) {
        fail(tr("Cannot start rclone: %1").arg(process.errorString()));
        return;
    }
    AppSettings::instance().setValue(AppSettings::driveMountPidKey, m_mountPid);

    setState(State::Connecting);
    m_connectStarted = QDateTime::currentMSecsSinceEpoch();
    m_poll->start();
}

void CloudDrive::pollMount()
{
    if (m_probing)
        return;
    m_probing = true;
    probeMount([this](bool mounted) {
        m_probing = false;
        if (m_state != State::Connecting)
            return;
        if (mounted) {
            finishConnecting(true);
        } else if (processGone(m_mountPid)) {
            finishConnecting(false, logTail());
        } else if (QDateTime::currentMSecsSinceEpoch() - m_connectStarted > m_connectTimeout) {
            finishConnecting(false, tr("rclone didn't connect in time."));
        }
    });
}

void CloudDrive::finishConnecting(bool mounted, const QString &error)
{
    m_poll->stop();
    const bool automatic = std::exchange(m_automatic, false);
    if (mounted) {
        setState(State::Connected);
        emit connected(mountPoint(), automatic);
        return;
    }
    setState(State::SignedIn);
    emit failed(error.isEmpty() ? tr("Cannot connect to Google Drive.") : tr("Cannot connect to Google Drive: %1").arg(error));
}

void CloudDrive::disconnectDrive()
{
    if (m_state != State::Connected)
        return;
    setState(State::Disconnecting);

    QString unmount = m_unmountProgram;
    QStringList arguments = m_unmountArguments;
    if (unmount.isEmpty()) {
#if defined(Q_OS_WIN)
        unmount = QStringLiteral("taskkill");
        arguments = {QStringLiteral("/PID"), AppSettings::instance().value(AppSettings::driveMountPidKey).toString()};
#elif defined(Q_OS_MACOS)
        unmount = QStringLiteral("umount");
        arguments = {mountPoint()};
#else
        unmount = QStandardPaths::findExecutable(QStringLiteral("fusermount3"));
        if (unmount.isEmpty())
            unmount = QStringLiteral("fusermount");
        arguments = {QStringLiteral("-u"), mountPoint()};
#endif
    }

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            handleUnmounted(false, process->errorString());
            process->deleteLater();
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int exitCode) {
        handleUnmounted(exitCode == 0, QString::fromUtf8(process->readAll()).trimmed());
        process->deleteLater();
    });
    process->start(unmount, arguments);
}

void CloudDrive::handleUnmounted(bool ok, const QString &error)
{
    const bool signOutNext = std::exchange(m_signOutAfterDisconnect, false);
    if (!ok) {
        setState(State::Connected);
        emit failed(tr("Cannot disconnect Google Drive, it may be in use: %1").arg(error));
        return;
    }
    setState(State::SignedIn);
    if (signOutNext)
        signOut();
}

void CloudDrive::signOut()
{
    if (m_state == State::Connected) {
        m_signOutAfterDisconnect = true;
        disconnectDrive();
        return;
    }
    if (m_state == State::SignedIn) {
        setState(State::Checking);
        deleteRemote();
    }
}
