#include "app/UpdateController.h"

#include "app/AppSettings.h"
#include "app/Updater.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

using Updates::InstallKind;

UpdateController::UpdateController(QWidget *window)
    : QObject(window)
    , m_window(window)
    , m_updater(new Updater(this))
{
    connect(m_updater, &Updater::checked, this, &UpdateController::handleRelease);
    connect(m_updater, &Updater::downloaded, this, &UpdateController::handleDownload);
    connect(m_updater, &Updater::progress, this, [this](qint64 received, qint64 total) {
        if (!m_progress)
            return;
        // In KiB: QProgressDialog takes ints.
        m_progress->setMaximum(total > 0 ? int(total / 1024) : 0);
        m_progress->setValue(int(received / 1024));
    });
}

void UpdateController::checkNow()
{
    if (m_updater->busy() && m_progress)
        return; // a download is running and showing its progress
    m_interactive = true;
    m_updater->check();
    m_progress = new QProgressDialog(tr("Checking for updates…"), tr("Cancel"), 0, 0, m_window);
    m_progress->setWindowTitle(tr("Check for Updates"));
    m_progress->setAttribute(Qt::WA_DeleteOnClose);
    m_progress->setMinimumDuration(400);
    m_progress->setWindowModality(Qt::WindowModal);
    connect(m_progress, &QProgressDialog::canceled, m_updater, &Updater::cancel);
}

void UpdateController::checkOnStart()
{
    AppSettings &settings = AppSettings::instance();
    if (!settings.checkUpdatesOnStart() || m_updater->busy())
        return;
    const QDateTime last = settings.value(AppSettings::lastUpdateCheckKey).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < checkInterval)
        return;
    m_interactive = false;
    m_updater->check();
}

void UpdateController::handleRelease(const Updates::Release &release)
{
    if (m_progress)
        m_progress->close();
    if (!release.error.isEmpty()) {
        if (m_interactive)
            showError(release.error);
        return; // at startup: try again next time
    }
    AppSettings &settings = AppSettings::instance();
    settings.setValue(AppSettings::lastUpdateCheckKey, QDateTime::currentDateTimeUtc());

    if (release.version <= Updates::currentVersion()) {
        if (m_interactive)
            QMessageBox::information(m_window, tr("Check for Updates"),
                                     tr("MDEditor %1 is the latest version.").arg(QApplication::applicationVersion()));
        return;
    }
    if (!m_interactive && settings.value(AppSettings::skippedUpdateKey).toString() == release.version.toString())
        return;
    offer(release);
}

void UpdateController::offer(const Updates::Release &release)
{
    const InstallKind kind = Updates::installKind();
    const bool installable = Updates::canInstall(release, kind);

    QDialog dialog(m_window);
    dialog.setWindowTitle(tr("Update Available"));
    dialog.resize(560, 440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(tr("<b>MDEditor %1 is available.</b> You have %2.")
                                 .arg(release.version.toString(), QApplication::applicationVersion()),
                             &dialog);
    layout->addWidget(title);
    auto *notes = new QTextBrowser(&dialog);
    notes->setOpenExternalLinks(true);
    notes->setMarkdown(release.notes.isEmpty() ? tr("No release notes.") : release.notes);
    layout->addWidget(notes, 1);

    QString how;
    switch (kind) {
    case InstallKind::WindowsInstaller:
        how = tr("Install downloads the setup, closes MDEditor and starts the setup.");
        break;
    case InstallKind::AppImage:
        how = tr("Install replaces this AppImage (%1) with the new version.")
                  .arg(QDir::toNativeSeparators(qEnvironmentVariable("APPIMAGE")));
        break;
    case InstallKind::Deb:
        how = tr("Download the .deb package from the release page and install it with "
                 "<code>sudo apt install ./%1</code>.")
                  .arg(Updates::assetFor(release, kind) ? Updates::assetFor(release, kind)->name.toHtmlEscaped()
                                                        : QStringLiteral("mdeditor_*.deb"));
        break;
    case InstallKind::WindowsPortable:
        how = tr("Download the new .zip from the release page and unpack it over this folder.");
        break;
    case InstallKind::Other:
        how = tr("This copy wasn't installed from a package: get the new version from the release page.");
        break;
    }
    if ((kind == InstallKind::WindowsInstaller || kind == InstallKind::AppImage) && !installable)
        how = tr("This release has no package for this installation: get it from the release page.");
    auto *hint = new QLabel(how, &dialog);
    hint->setWordWrap(true);
    hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(&dialog);
    QPushButton *install = nullptr;
    if (installable) {
        install = buttons->addButton(tr("&Install"), QDialogButtonBox::AcceptRole);
        install->setDefault(true);
    }
    QPushButton *page = buttons->addButton(tr("Open &Release Page"), QDialogButtonBox::ActionRole);
    QPushButton *skip = buttons->addButton(tr("&Skip This Version"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(tr("&Later"), QDialogButtonBox::RejectRole);
    QAbstractButton *clicked = nullptr;
    connect(buttons, &QDialogButtonBox::clicked, &dialog, [&](QAbstractButton *button) {
        clicked = button;
        dialog.done(button == install ? QDialog::Accepted : QDialog::Rejected);
    });
    layout->addWidget(buttons);
    dialog.exec();

    if (clicked && clicked == install) {
        download(release);
    } else if (clicked == page) {
        QDesktopServices::openUrl(release.page);
    } else if (clicked == skip) {
        AppSettings::instance().setValue(AppSettings::skippedUpdateKey, release.version.toString());
    }
}

void UpdateController::download(const Updates::Release &release)
{
    const InstallKind kind = Updates::installKind();
    const std::optional<Updates::Asset> asset = Updates::assetFor(release, kind);
    if (!asset)
        return;

    QString partial;
    if (kind == InstallKind::AppImage) {
        // Next to the AppImage, so the final rename stays on one file system.
        m_target = qEnvironmentVariable("APPIMAGE");
        const QFileInfo folder(QFileInfo(m_target).absolutePath());
        if (!folder.isWritable()) {
            showError(tr("Can't write to %1, where this AppImage is. Download the new version from the release "
                         "page instead.")
                          .arg(QDir::toNativeSeparators(folder.absoluteFilePath())),
                      release.page);
            return;
        }
        partial = m_target + QStringLiteral(".download");
    } else {
        m_target.clear();
        partial = QDir::temp().filePath(asset->name);
    }

    m_release = release;
    m_progress = new QProgressDialog(tr("Downloading %1…").arg(asset->name), tr("Cancel"), 0, 0, m_window);
    m_progress->setWindowTitle(tr("Update"));
    m_progress->setAttribute(Qt::WA_DeleteOnClose);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    m_progress->setWindowModality(Qt::WindowModal);
    connect(m_progress, &QProgressDialog::canceled, m_updater, &Updater::cancel);
    m_updater->download(*asset, partial);
}

void UpdateController::handleDownload(const QString &path, const QString &error)
{
    if (m_progress) {
        m_progress->disconnect(m_updater); // closing a progress dialog emits canceled
        m_progress->close();
    }
    if (!error.isEmpty()) {
        showError(error, m_release.page);
        return;
    }

    const QString version = m_release.version.toString();
    if (Updates::installKind() == InstallKind::AppImage) {
        QString failure;
        if (!Updates::replaceAppImage(path, m_target, &failure)) {
            QFile::remove(path);
            showError(tr("Can't replace %1: %2").arg(QDir::toNativeSeparators(m_target), failure), m_release.page);
            return;
        }
        const auto answer = QMessageBox::question(
            m_window, tr("Update Installed"),
            tr("MDEditor %1 is installed. Restart now to use it?").arg(version),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes && m_window->close()) {
            QProcess::startDetached(m_target, {});
            QApplication::quit();
        }
        return;
    }

    // The Windows setup: it removes the old version first, so MDEditor has to be closed by then.
    const auto answer = QMessageBox::question(
        m_window, tr("Install Update"),
        tr("MDEditor %1 is downloaded. Close MDEditor and start the setup now?").arg(version),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes || !m_window->close())
        return;
    // Through the shell, which asks for administrator rights when the setup needs them.
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(nullptr, tr("Install Update"),
                             tr("Can't start the setup. Run it yourself: %1").arg(QDir::toNativeSeparators(path)));
    }
    QApplication::quit();
}

void UpdateController::showError(const QString &message, const QUrl &page)
{
    QMessageBox box(QMessageBox::Warning, tr("Check for Updates"), message, QMessageBox::Close, m_window);
    QPushButton *open = page.isValid() ? box.addButton(tr("Open &Release Page"), QMessageBox::ActionRole) : nullptr;
    box.exec();
    if (open && box.clickedButton() == open)
        QDesktopServices::openUrl(page);
}
