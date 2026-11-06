#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QProcess;
class QTimer;

// Google Drive as a local folder, through rclone. rclone does the login and keeps the token in its own
// configuration; MDEditor only remembers which folder goes where (AppSettings). Both live in the user's
// profile, so they survive updates of the application.
//
// The mount is a detached process: it keeps running after MDEditor exits, because rclone uploads a saved
// file a moment later and closing the window must not cut that off. The next start finds it still mounted.
class CloudDrive : public QObject {
    Q_OBJECT

public:
    enum class State {
        Checking,
        NoRclone,
        SignedOut,
        SigningIn,
        SignedIn, // has a token, not mounted
        Connecting,
        Connected,
        Disconnecting
    };
    using MountProbe = std::function<bool(const QString &path)>;

    static constexpr auto remoteName = "mdeditor-gdrive";

    explicit CloudDrive(QObject *parent = nullptr);

    State state() const { return m_state; }
    QString remoteFolder() const;     // on the drive, e.g. "Notes"; empty for the whole drive
    QString mountPoint() const;
    bool connectOnStart() const;
    void setRemoteFolder(const QString &folder);
    void setMountPoint(const QString &path);
    void setConnectOnStart(bool enabled);
    // Whether `path` is inside the mount point, connected or not.
    bool contains(const QString &path) const;

    void refresh();
    // At application start: refresh, and connect if the user asked for that.
    void start();
    void signIn();
    void cancelSignIn();
    void connectDrive();
    void disconnectDrive();
    void signOut();

    // For tests.
    void setMountProbe(MountProbe probe) { m_probe = std::move(probe); }
    void setConnectTimeout(int milliseconds) { m_connectTimeout = milliseconds; }
    void setUnmountCommand(const QString &program, const QStringList &arguments);

signals:
    void stateChanged();
    void connected(const QString &path, bool automatic);
    void failed(const QString &message);
    void signInLinkReady(const QString &url);

private:
    QString program() const;
    QString remote() const;
    QString logPath() const;
    QString logTail() const;
    void setState(State state);
    void run(const QStringList &arguments, std::function<void(int exitCode, const QByteArray &output)> done);
    void afterSignInCheck(bool signedIn);
    void pollMount();
    void finishConnecting(bool mounted, const QString &error = {});
    void probeMount(std::function<void(bool)> done);
    void deleteRemote();
    void handleUnmounted(bool ok, const QString &error);

    State m_state = State::Checking;
    MountProbe m_probe;
    QProcess *m_signIn = nullptr;
    QTimer *m_poll;
    qint64 m_mountPid = 0;
    qint64 m_connectStarted = 0;
    int m_connectTimeout = 20000;
    bool m_probing = false;
    bool m_automatic = false;         // the current connection attempt came from start()
    bool m_connectWhenReady = false;
    bool m_signOutAfterDisconnect = false;
    QString m_unmountProgram;
    QStringList m_unmountArguments;
};
