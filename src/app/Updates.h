#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVersionNumber>

#include <optional>

// What an update check needs to know, without the network or any UI: published releases, which package fits
// this installation, and how an AppImage replaces itself.
namespace Updates {

// How this copy of MDEditor was installed, which decides how it can update itself.
enum class InstallKind {
    WindowsInstaller, // NSIS setup: run the new setup, which uninstalls the old version first
    WindowsPortable,  // unpacked .zip: only the download page
    AppImage,         // the file in $APPIMAGE: replaced by the new one in place
    Deb,              // /opt/mdeditor from the .deb: installing needs root, so only the download page
    Other,            // a build directory or a local install
};

struct Asset {
    QString name;
    QUrl url;
    qint64 size = -1;
    QByteArray sha256; // raw bytes; empty if the server didn't publish a digest
};

struct Release {
    QString error; // the check failed: everything else is empty
    QVersionNumber version;
    QString tag;
    QString notes; // Markdown
    QUrl page;
    QList<Asset> assets;
};

// The GitHub API's latest release, or MDEDITOR_UPDATE_URL (any URL Qt can fetch, including file://).
QUrl releaseUrl();
QVersionNumber currentVersion(); // from QCoreApplication::applicationVersion()

InstallKind installKind();
// The rules behind installKind(), for tests: `appDir` holds the executable, `appImage` is $APPIMAGE.
InstallKind installKind(const QString &appDir, const QString &appImage, bool windows);

Release parseRelease(const QByteArray &json);
// The package for `kind`, if the release has one.
std::optional<Asset> assetFor(const Release &release, InstallKind kind);
// Whether this installation can install `release` by itself (the others get a link to the release page).
bool canInstall(const Release &release, InstallKind kind);

// Makes the downloaded file `downloaded` executable and moves it over `target` (both in one folder, so the
// rename is atomic and the running AppImage stays intact until the app restarts).
bool replaceAppImage(const QString &downloaded, const QString &target, QString *error);

} // namespace Updates
