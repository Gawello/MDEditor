#include "TestHarness.h"

#include <QtGlobal>

#ifdef Q_OS_UNIX // the fake rclone is a shell script

#include "app/AppSettings.h"
#include "app/CloudDrive.h"
#include "app/CloudDrivePage.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

using State = CloudDrive::State;

namespace {

const char fakeRclone[] = R"SH(#!/bin/sh
dir="$(dirname "$0")"
echo "$*" >> "$dir/calls"
case "$1 $2" in
"config dump")
    if [ -f "$dir/token" ]; then echo '{"mdeditor-gdrive":{"type":"drive","token":"{\"access_token\":\"x\"}"}}'
    elif [ -f "$dir/remote" ]; then echo '{"mdeditor-gdrive":{"type":"drive"}}'
    else echo '{}'; fi ;;
"config create")
    touch "$dir/remote"
    echo "NOTICE: go to the following link: http://127.0.0.1:53682/auth?state=abc" >&2
    if [ -f "$dir/login-hangs" ]; then exec sleep 30; fi
    if [ -f "$dir/login-fails" ]; then exit 1; fi
    touch "$dir/token" ;;
"config delete")
    rm -f "$dir/remote" "$dir/token" ;;
mount*)
    if [ -f "$dir/mount-fails" ]; then echo "Fatal error: no such thing" >&2; exit 1; fi
    if [ -f "$dir/mount-hangs" ]; then exec sleep 2; fi
    touch "$dir/mounted"
    exec sleep 2 ;;
esac
)SH";

const char fakeUnmount[] = R"SH(#!/bin/sh
dir="$(dirname "$0")"
if [ -f "$dir/unmount-fails" ]; then echo "device is busy" >&2; exit 1; fi
rm -f "$dir/mounted"
)SH";

struct Fixture {
    QTemporaryDir dir;
    QString mountPoint = dir.filePath(QStringLiteral("drive"));

    Fixture()
    {
        installScript(QStringLiteral("rclone"), fakeRclone);
        installScript(QStringLiteral("unmount"), fakeUnmount);
        AppSettings &settings = AppSettings::instance();
        settings.setValue(AppSettings::driveRclonePathKey, dir.filePath(QStringLiteral("rclone")));
        settings.setValue(AppSettings::driveMountPointKey, mountPoint);
        settings.setValue(AppSettings::driveFolderKey, QString());
        settings.setValue(AppSettings::driveConnectOnStartKey, true);
    }
    ~Fixture() { AppSettings::instance().setValue(AppSettings::driveRclonePathKey, QString()); }

    void installScript(const QString &name, const char *text)
    {
        QFile file(dir.filePath(name));
        file.open(QIODevice::WriteOnly);
        file.write(text);
        file.setPermissions(file.permissions() | QFile::ExeOwner);
    }
    void touch(const QString &name)
    {
        QFile file(dir.filePath(name));
        file.open(QIODevice::WriteOnly);
    }
    bool has(const QString &name) const { return QFile::exists(dir.filePath(name)); }
    QString calls() const
    {
        QFile file(dir.filePath(QStringLiteral("calls")));
        file.open(QIODevice::ReadOnly);
        return QString::fromUtf8(file.readAll());
    }
    // A new CloudDrive is what the application gets after a restart or an update.
    void prepare(CloudDrive &drive)
    {
        const QString marker = dir.filePath(QStringLiteral("mounted"));
        drive.setMountProbe([marker](const QString &) { return QFile::exists(marker); });
        drive.setUnmountCommand(dir.filePath(QStringLiteral("unmount")), {});
        drive.setConnectTimeout(800);
    }
};

template <typename Condition>
bool waitUntil(Condition condition, int milliseconds = 4000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    return condition();
}

// What a CloudDrive signalled, in order.
struct Signals {
    QStringList links;
    QStringList failures;
    QStringList connected;
    bool automatic = false;

    explicit Signals(CloudDrive &drive)
    {
        QObject::connect(&drive, &CloudDrive::signInLinkReady, &drive, [this](const QString &url) { links << url; });
        QObject::connect(&drive, &CloudDrive::failed, &drive, [this](const QString &text) { failures << text; });
        QObject::connect(&drive, &CloudDrive::connected, &drive, [this](const QString &path, bool isAutomatic) {
            connected << path;
            automatic = isAutomatic;
        });
    }
};

} // namespace

TEST_CASE(drive_reports_missing_rclone)
{
    Fixture f;
    AppSettings::instance().setValue(AppSettings::driveRclonePathKey, f.dir.filePath(QStringLiteral("nothing-here")));
    CloudDrive drive;
    f.prepare(drive);
    drive.refresh();
    CHECK(waitUntil([&] { return drive.state() == State::NoRclone; }));
}

TEST_CASE(drive_without_a_token_is_signed_out)
{
    Fixture f;
    f.touch(QStringLiteral("remote")); // what an abandoned login leaves behind
    CloudDrive drive;
    f.prepare(drive);
    drive.refresh();
    CHECK(waitUntil([&] { return drive.state() == State::SignedOut; }));
}

TEST_CASE(drive_sign_in_offers_the_link_and_stores_the_login)
{
    Fixture f;
    CloudDrive drive;
    f.prepare(drive);
    Signals seen(drive);
    drive.signIn();
    CHECK(waitUntil([&] { return drive.state() == State::SignedIn; }));
    CHECK(seen.links.size() == 1);
    CHECK(seen.links.value(0) == QStringLiteral("http://127.0.0.1:53682/auth?state=abc"));
    CHECK(f.calls().contains(QStringLiteral("config create mdeditor-gdrive drive scope=drive")));
}

TEST_CASE(drive_cancelled_sign_in_leaves_nothing_behind)
{
    Fixture f;
    f.touch(QStringLiteral("login-hangs"));
    CloudDrive drive;
    f.prepare(drive);
    drive.signIn();
    CHECK(waitUntil([&] { return f.has(QStringLiteral("remote")); }));
    drive.cancelSignIn();
    CHECK(waitUntil([&] { return drive.state() == State::SignedOut; }));
    CHECK(!f.has(QStringLiteral("remote")));
}

TEST_CASE(drive_failed_sign_in_says_so)
{
    Fixture f;
    f.touch(QStringLiteral("login-fails"));
    CloudDrive drive;
    f.prepare(drive);
    Signals seen(drive);
    drive.signIn();
    CHECK(waitUntil([&] { return drive.state() == State::SignedOut && !f.has(QStringLiteral("remote")); }));
    CHECK(seen.failures.size() == 1);
}

TEST_CASE(drive_connect_mounts_the_chosen_folder)
{
    Fixture f;
    f.touch(QStringLiteral("token"));
    CloudDrive drive;
    f.prepare(drive);
    drive.setRemoteFolder(QStringLiteral(" /Notes/Work/ "));
    CHECK(drive.remoteFolder() == QStringLiteral("Notes/Work"));
    drive.refresh();
    CHECK(waitUntil([&] { return drive.state() == State::SignedIn; }));

    Signals seen(drive);
    drive.connectDrive();
    CHECK(waitUntil([&] { return drive.state() == State::Connected; }));
    CHECK(seen.connected.size() == 1);
    CHECK(seen.connected.value(0) == f.mountPoint);
    CHECK(!seen.automatic); // the user asked: the window opens the folder
    const QString expected = QStringLiteral("mount mdeditor-gdrive:Notes/Work ") + f.mountPoint;
    CHECK(f.calls().contains(expected));
    CHECK(f.calls().contains(QStringLiteral("--vfs-cache-mode writes")));
}

TEST_CASE(drive_refuses_a_folder_that_is_not_empty)
{
    Fixture f;
    f.touch(QStringLiteral("token"));
    CloudDrive drive;
    f.prepare(drive);
    drive.setMountPoint(f.dir.path()); // holds the scripts
    drive.refresh();
    CHECK(waitUntil([&] { return drive.state() == State::SignedIn; }));
    Signals seen(drive);
    drive.connectDrive();
    CHECK(seen.failures.size() == 1);
    CHECK(drive.state() == State::SignedIn);
    CHECK(!f.calls().contains(QStringLiteral("mount ")));
}

TEST_CASE(drive_reports_why_a_mount_failed_without_waiting_for_the_timeout)
{
    Fixture f;
    f.touch(QStringLiteral("token"));
    f.touch(QStringLiteral("mount-fails"));
    CloudDrive drive;
    f.prepare(drive);
    drive.setConnectTimeout(60000);
    drive.refresh();
    CHECK(waitUntil([&] { return drive.state() == State::SignedIn; }));
    Signals seen(drive);
    QElapsedTimer timer;
    timer.start();
    drive.connectDrive();
    CHECK(waitUntil([&] { return seen.failures.size() == 1; }, 5000));
    CHECK(timer.elapsed() < 5000);
    CHECK(seen.failures.value(0).contains(QStringLiteral("no such thing")));
    CHECK(drive.state() == State::SignedIn);
}

TEST_CASE(drive_connect_gives_up_after_the_timeout)
{
    Fixture f;
    f.touch(QStringLiteral("token"));
    f.touch(QStringLiteral("mount-hangs"));
    CloudDrive drive;
    f.prepare(drive);
    drive.refresh();
    CHECK(waitUntil([&] { return drive.state() == State::SignedIn; }));
    Signals seen(drive);
    drive.connectDrive();
    CHECK(waitUntil([&] { return seen.failures.size() == 1; }));
    CHECK(drive.state() == State::SignedIn);
}

TEST_CASE(drive_connection_is_remembered_by_a_new_instance)
{
    Fixture f;
    f.touch(QStringLiteral("token"));
    AppSettings::instance().setValue(AppSettings::driveFolderKey, QStringLiteral("Notes"));

    CloudDrive afterUpdate; // nothing of the old process survives but the settings and rclone's configuration
    f.prepare(afterUpdate);
    Signals seen(afterUpdate);
    afterUpdate.start();
    CHECK(waitUntil([&] { return seen.connected.size() == 1; }));
    CHECK(seen.automatic); // automatic: the session decides what to open
    CHECK(f.calls().contains(QStringLiteral("mount mdeditor-gdrive:Notes")));
}

TEST_CASE(drive_still_mounted_from_the_last_run_is_reused)
{
    Fixture f;
    f.touch(QStringLiteral("token"));
    f.touch(QStringLiteral("mounted"));
    CloudDrive drive;
    f.prepare(drive);
    Signals seen(drive);
    drive.start();
    CHECK(waitUntil([&] { return seen.connected.size() == 1; }));
    CHECK(drive.state() == State::Connected);
    CHECK(!f.calls().contains(QStringLiteral("mount ")));
}

TEST_CASE(drive_stays_off_when_auto_connect_is_disabled)
{
    Fixture f;
    f.touch(QStringLiteral("token"));
    CloudDrive drive;
    f.prepare(drive);
    drive.setConnectOnStart(false);
    drive.start();
    CHECK(waitUntil([&] { return drive.state() == State::SignedIn; }));
    CHECK(!f.calls().contains(QStringLiteral("mount ")));
}

TEST_CASE(drive_disconnect_and_sign_out)
{
    Fixture f;
    f.touch(QStringLiteral("token"));
    f.touch(QStringLiteral("mounted"));
    CloudDrive drive;
    f.prepare(drive);
    drive.refresh();
    CHECK(waitUntil([&] { return drive.state() == State::Connected; }));

    drive.signOut(); // from Connected: unmounts first
    CHECK(waitUntil([&] { return drive.state() == State::SignedOut; }));
    CHECK(!f.has(QStringLiteral("mounted")));
    CHECK(!f.has(QStringLiteral("token")));
}

TEST_CASE(drive_busy_unmount_stays_connected_and_says_so)
{
    Fixture f;
    f.touch(QStringLiteral("token"));
    f.touch(QStringLiteral("mounted"));
    f.touch(QStringLiteral("unmount-fails"));
    CloudDrive drive;
    f.prepare(drive);
    drive.refresh();
    CHECK(waitUntil([&] { return drive.state() == State::Connected; }));
    Signals seen(drive);
    drive.disconnectDrive();
    CHECK(waitUntil([&] { return seen.failures.size() == 1; }));
    CHECK(seen.failures.value(0).contains(QStringLiteral("busy")));
    CHECK(drive.state() == State::Connected);
}

TEST_CASE(drive_contains_only_paths_inside_the_mount_point)
{
    Fixture f;
    CloudDrive drive;
    CHECK(drive.contains(f.mountPoint));
    CHECK(drive.contains(f.mountPoint + QStringLiteral("/a/b.md")));
    CHECK(!drive.contains(f.mountPoint + QStringLiteral("-other/b.md")));
    CHECK(!drive.contains(QStringLiteral("/somewhere/else.md")));
}

TEST_CASE(drive_page_walks_through_sign_in_and_connect)
{
    Fixture f;
    CloudDrive drive;
    f.prepare(drive);
    CloudDrivePage page(&drive);
    const auto button = [&](const QString &text) -> QPushButton * {
        for (QPushButton *candidate : page.findChildren<QPushButton *>()) {
            if (candidate->text() == text && candidate->isEnabled())
                return candidate;
        }
        return nullptr;
    };
    drive.refresh();
    CHECK(waitUntil([&] { return button(QStringLiteral("Sign In with Google…")) != nullptr; }));
    button(QStringLiteral("Sign In with Google…"))->click();
    CHECK(waitUntil([&] { return button(QStringLiteral("Connect")) != nullptr; }));
    button(QStringLiteral("Connect"))->click();
    CHECK(waitUntil([&] { return button(QStringLiteral("Disconnect")) != nullptr; }));
    bool shown = false;
    for (QLabel *label : page.findChildren<QLabel *>())
        shown = shown || label->text().contains(f.mountPoint);
    CHECK(shown);
}

#endif // Q_OS_UNIX
