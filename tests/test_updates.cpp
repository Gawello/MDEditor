#include "TestHarness.h"

#include "app/AppSettings.h"
#include "app/UpdateController.h"
#include "app/Updater.h"
#include "app/Updates.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QWidget>

using Updates::InstallKind;

namespace {

QByteArray releaseJson(const QString &tag, const QString &assetUrl = {}, qint64 size = -1,
                       const QByteArray &sha256 = {})
{
    QString assets;
    if (!assetUrl.isEmpty()) {
        assets = QStringLiteral(R"({"name": "MDEditor-%1-x86_64.AppImage", "browser_download_url": "%2", "size": %3%4},)"
                                R"({"name": "mdeditor_%1_amd64.deb", "browser_download_url": "%2", "size": 1},)"
                                R"({"name": "MDEditor-%1-win64.exe", "browser_download_url": "%2", "size": 1},)"
                                R"({"name": "MDEditor-%1-win64.zip", "browser_download_url": "%2", "size": 1})")
                     .arg(tag.mid(1), assetUrl)
                     .arg(size)
                     .arg(sha256.isEmpty() ? QString() : QStringLiteral(R"(, "digest": "sha256:%1")")
                                                             .arg(QString::fromLatin1(sha256.toHex())));
    }
    return QStringLiteral(R"({"tag_name": "%1", "body": "## Changes\n- faster", )"
                          R"("html_url": "https://example.com/releases/%1", "assets": [%2]})")
        .arg(tag, assets)
        .toUtf8();
}

bool waitFor(const std::function<bool()> &done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

void write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(data);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

TEST_CASE(updates_parse_github_release)
{
    const Updates::Release release =
        Updates::parseRelease(releaseJson(QStringLiteral("v0.3.1"), QStringLiteral("https://example.com/a"), 42,
                                          QByteArray::fromHex("00ff")));
    CHECK(release.error.isEmpty());
    CHECK(release.version == QVersionNumber(0, 3, 1));
    CHECK(release.notes.startsWith(u"## Changes"));
    CHECK(release.page == QUrl(QStringLiteral("https://example.com/releases/v0.3.1")));
    CHECK(release.assets.size() == 4);
    CHECK(release.assets[0].size == 42);
    CHECK(release.assets[0].sha256 == QByteArray::fromHex("00ff"));
    CHECK(release.assets[1].sha256.isEmpty());

    CHECK(!Updates::parseRelease("{\"message\": \"Not Found\"}").error.isEmpty());
    CHECK(!Updates::parseRelease("<html>").error.isEmpty());
    CHECK(QVersionNumber(0, 10, 0) > QVersionNumber::fromString(QStringLiteral("0.9.5")));
}

TEST_CASE(updates_pick_the_package_of_this_installation)
{
    const Updates::Release release =
        Updates::parseRelease(releaseJson(QStringLiteral("v1.0.0"), QStringLiteral("https://example.com/a")));
    CHECK(Updates::assetFor(release, InstallKind::AppImage)->name == u"MDEditor-1.0.0-x86_64.AppImage");
    CHECK(Updates::assetFor(release, InstallKind::Deb)->name == u"mdeditor_1.0.0_amd64.deb");
    CHECK(Updates::assetFor(release, InstallKind::WindowsInstaller)->name == u"MDEditor-1.0.0-win64.exe");
    CHECK(Updates::assetFor(release, InstallKind::WindowsPortable)->name == u"MDEditor-1.0.0-win64.zip");
    CHECK(!Updates::assetFor(release, InstallKind::Other));

    CHECK(Updates::canInstall(release, InstallKind::AppImage));
    CHECK(Updates::canInstall(release, InstallKind::WindowsInstaller));
    CHECK(!Updates::canInstall(release, InstallKind::Deb)); // needs root: only the release page
    CHECK(!Updates::canInstall(release, InstallKind::WindowsPortable));
    const Updates::Release bare = Updates::parseRelease(releaseJson(QStringLiteral("v1.0.0")));
    CHECK(!Updates::canInstall(bare, InstallKind::AppImage));
}

TEST_CASE(updates_detect_how_the_app_was_installed)
{
    QTemporaryDir dir;
    CHECK(QDir(dir.path()).mkpath(QStringLiteral("MDEditor/bin")));
    const QString bin = dir.filePath(QStringLiteral("MDEditor/bin"));
    CHECK(Updates::installKind(bin, {}, true) == InstallKind::WindowsPortable);
    write(dir.filePath(QStringLiteral("MDEditor/Uninstall.exe")), "MZ");
    CHECK(Updates::installKind(bin, {}, true) == InstallKind::WindowsInstaller);
    CHECK(Updates::installKind(dir.path(), {}, true) == InstallKind::Other); // a build directory

    CHECK(Updates::installKind(QStringLiteral("/tmp/.mount_MDEdit/usr/bin"), QStringLiteral("/home/u/MDEditor.AppImage"),
                               false)
          == InstallKind::AppImage);
    CHECK(Updates::installKind(QStringLiteral("/opt/mdeditor/bin"), {}, false) == InstallKind::Deb);
    CHECK(Updates::installKind(QStringLiteral("/home/u/MDEditor/build/debug"), {}, false) == InstallKind::Other);
}

TEST_CASE(updates_check_reads_the_release)
{
    QTemporaryDir dir;
    const QString json = dir.filePath(QStringLiteral("latest.json"));
    write(json, releaseJson(QStringLiteral("v2.0.0")));

    Updater updater;
    QList<Updates::Release> checked;
    QObject::connect(&updater, &Updater::checked, [&](const Updates::Release &release) { checked << release; });
    updater.check(QUrl::fromLocalFile(json));
    CHECK(waitFor([&] { return checked.size() == 1; }));
    const Updates::Release release = checked.value(0);
    CHECK(release.error.isEmpty());
    CHECK(release.version == QVersionNumber(2, 0, 0));
    CHECK(!updater.busy());

    updater.check(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.json"))));
    CHECK(waitFor([&] { return checked.size() == 2; }));
    CHECK(!checked.value(1).error.isEmpty());
}

TEST_CASE(updates_download_is_verified)
{
    QTemporaryDir dir;
    const QByteArray payload(300000, 'x');
    const QString source = dir.filePath(QStringLiteral("package"));
    write(source, payload);
    const QByteArray sha = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);

    Updater updater;
    QStringList done; // the errors
    QObject::connect(&updater, &Updater::downloaded, [&](const QString &, const QString &error) { done << error; });
    Updates::Asset asset{QStringLiteral("package"), QUrl::fromLocalFile(source), payload.size(), sha};
    const QString target = dir.filePath(QStringLiteral("download"));
    updater.download(asset, target);
    CHECK(waitFor([&] { return done.size() == 1; }));
    CHECK(done.value(0).isEmpty());
    CHECK(read(target) == payload);

    asset.sha256 = QCryptographicHash::hash("other", QCryptographicHash::Sha256);
    updater.download(asset, target);
    CHECK(waitFor([&] { return done.size() == 2; }));
    CHECK(done.value(1).contains(u"checksum"));
    CHECK(!QFile::exists(target)); // a damaged download isn't left behind

    asset.sha256.clear();
    asset.size = payload.size() + 1;
    updater.download(asset, target);
    CHECK(waitFor([&] { return done.size() == 3; }));
    CHECK(done.value(2).contains(u"incomplete"));
    CHECK(!QFile::exists(target));
}

#ifdef Q_OS_UNIX
TEST_CASE(updates_replace_the_appimage)
{
    QTemporaryDir dir;
    const QString appImage = dir.filePath(QStringLiteral("MDEditor.AppImage"));
    const QString downloaded = appImage + QStringLiteral(".download");
    write(appImage, "old");
    write(downloaded, "new");
    QString error;
    CHECK(Updates::replaceAppImage(downloaded, appImage, &error));
    CHECK(error.isEmpty());
    CHECK(read(appImage) == "new");
    CHECK(!QFile::exists(downloaded));
    CHECK(QFileInfo(appImage).isExecutable());

    CHECK(!Updates::replaceAppImage(dir.filePath(QStringLiteral("nothing")), appImage, &error));
    CHECK(!error.isEmpty());
    CHECK(read(appImage) == "new");
}
#endif

TEST_CASE(updates_startup_check_is_opt_in_and_daily)
{
    AppSettings &settings = AppSettings::instance();
    QTemporaryDir dir;
    const QString json = dir.filePath(QStringLiteral("latest.json"));
    write(json, releaseJson(QStringLiteral("v0.0.1"))); // older than anything: nothing to offer
    qputenv("MDEDITOR_UPDATE_URL", QUrl::fromLocalFile(json).toString().toUtf8());
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QWidget window;
    UpdateController controller(&window);
    controller.checkOnStart();
    QCoreApplication::processEvents();
    CHECK(!settings.value(AppSettings::lastUpdateCheckKey).isValid()); // off by default

    settings.setValue(AppSettings::checkUpdatesOnStartKey, true);
    controller.checkOnStart();
    CHECK(waitFor([&] { return settings.value(AppSettings::lastUpdateCheckKey).isValid(); }));
    const QDateTime first = settings.value(AppSettings::lastUpdateCheckKey).toDateTime();

    // Within a day: no new check.
    controller.checkOnStart();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 200)
        QCoreApplication::processEvents();
    CHECK(settings.value(AppSettings::lastUpdateCheckKey).toDateTime() == first);

    settings.setValue(AppSettings::lastUpdateCheckKey, first.addSecs(-UpdateController::checkInterval - 1));
    controller.checkOnStart();
    CHECK(waitFor([&] { return settings.value(AppSettings::lastUpdateCheckKey).toDateTime() > first.addSecs(-10); }));

    qunsetenv("MDEDITOR_UPDATE_URL");
    QCoreApplication::setApplicationVersion(QString());
    settings.setValue(AppSettings::checkUpdatesOnStartKey, QVariant());
    settings.setValue(AppSettings::lastUpdateCheckKey, QVariant());
}
