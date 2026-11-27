#include "app/Updates.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>
#include <system_error>

namespace Updates {

QUrl releaseUrl()
{
    const QString custom = qEnvironmentVariable("MDEDITOR_UPDATE_URL");
    if (!custom.isEmpty())
        return QUrl::fromUserInput(custom);
    return QUrl(QStringLiteral("https://api.github.com/repos/Gawello/MDEditor/releases/latest"));
}

QVersionNumber currentVersion()
{
    return QVersionNumber::fromString(QCoreApplication::applicationVersion());
}

InstallKind installKind()
{
#ifdef Q_OS_WIN
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    return installKind(QCoreApplication::applicationDirPath(), qEnvironmentVariable("APPIMAGE"), windows);
}

InstallKind installKind(const QString &appDir, const QString &appImage, bool windows)
{
    if (windows) {
        // The setup puts MDEditor.exe in <install>/bin and its uninstaller in <install>.
        QDir dir(appDir);
        if (dir.cdUp() && QFile::exists(dir.filePath(QStringLiteral("Uninstall.exe"))))
            return InstallKind::WindowsInstaller;
        // A .zip unpacks to the same bin folder, without the uninstaller.
        return QFileInfo(appDir).fileName().compare(u"bin", Qt::CaseInsensitive) == 0 ? InstallKind::WindowsPortable
                                                                                       : InstallKind::Other;
    }
    if (!appImage.isEmpty())
        return InstallKind::AppImage;
    if (QDir::cleanPath(appDir).startsWith(u"/opt/mdeditor/"))
        return InstallKind::Deb;
    return InstallKind::Other;
}

Release parseRelease(const QByteArray &json)
{
    Release release;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    const QJsonObject object = document.object();
    const QString tag = object.value(u"tag_name").toString();
    // Tags are "v1.2.3" (see the release workflow).
    const QVersionNumber version = QVersionNumber::fromString(tag.startsWith(u'v') ? tag.mid(1) : tag);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || version.isNull()) {
        release.error = QCoreApplication::translate("Updates", "The server's answer isn't a release.");
        return release;
    }

    release.version = version;
    release.tag = tag;
    release.notes = object.value(u"body").toString();
    release.page = QUrl(object.value(u"html_url").toString());
    for (const QJsonValue &value : object.value(u"assets").toArray()) {
        const QJsonObject asset = value.toObject();
        Asset item;
        item.name = asset.value(u"name").toString();
        item.url = QUrl(asset.value(u"browser_download_url").toString());
        item.size = asset.value(u"size").toInteger(-1);
        const QString digest = asset.value(u"digest").toString(); // "sha256:<hex>"
        if (digest.startsWith(u"sha256:"))
            item.sha256 = QByteArray::fromHex(digest.mid(7).toLatin1());
        if (!item.name.isEmpty() && item.url.isValid())
            release.assets << item;
    }
    return release;
}

std::optional<Asset> assetFor(const Release &release, InstallKind kind)
{
    // The names CPack and the AppImage step give the packages.
    QString suffix;
    switch (kind) {
    case InstallKind::WindowsInstaller: suffix = QStringLiteral("-win64.exe"); break;
    case InstallKind::WindowsPortable: suffix = QStringLiteral("-win64.zip"); break;
    case InstallKind::AppImage: suffix = QStringLiteral("-x86_64.AppImage"); break;
    case InstallKind::Deb: suffix = QStringLiteral("_amd64.deb"); break;
    case InstallKind::Other: return std::nullopt;
    }
    for (const Asset &asset : release.assets) {
        if (asset.name.endsWith(suffix, Qt::CaseInsensitive))
            return asset;
    }
    return std::nullopt;
}

bool canInstall(const Release &release, InstallKind kind)
{
    return (kind == InstallKind::WindowsInstaller || kind == InstallKind::AppImage)
        && assetFor(release, kind).has_value();
}

bool replaceAppImage(const QString &downloaded, const QString &target, QString *error)
{
    QFile file(downloaded);
    if (!file.setPermissions(file.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther
                             | QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    // QFile::rename won't replace a file; rename(2) does, atomically.
    std::error_code code;
    std::filesystem::rename(std::filesystem::path(downloaded.toStdU16String()),
                            std::filesystem::path(target.toStdU16String()), code);
    if (code) {
        if (error)
            *error = QString::fromStdString(code.message());
        return false;
    }
    return true;
}

} // namespace Updates
