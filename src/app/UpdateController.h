#pragma once

#include "app/Updates.h"

#include <QObject>
#include <QPointer>

class QProgressDialog;
class QWidget;
class Updater;

// Help → Check for Updates and the optional check at startup: asks, shows what's new, downloads the package
// and installs it the way this copy was installed (see Updates::InstallKind).
class UpdateController : public QObject {
    Q_OBJECT

public:
    // `window` is the main window: it's closed (asking about unsaved changes) before an update restarts the app.
    explicit UpdateController(QWidget *window);

    void checkNow();     // says what it finds, including "up to date" and errors
    void checkOnStart(); // if enabled in Settings and not done in the last day; speaks only about a new version

    static constexpr int checkInterval = 20 * 60 * 60; // seconds between checks at startup

private:
    void handleRelease(const Updates::Release &release);
    void offer(const Updates::Release &release);
    void download(const Updates::Release &release);
    void handleDownload(const QString &path, const QString &error);
    void showError(const QString &message, const QUrl &page = {});

    QWidget *m_window;
    Updater *m_updater;
    QPointer<QProgressDialog> m_progress;
    Updates::Release m_release; // the one being downloaded
    QString m_target;           // where the download goes when it's done (the AppImage itself)
    bool m_interactive = false;
};
