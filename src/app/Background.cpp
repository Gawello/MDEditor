#include "app/Background.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProgressDialog>
#include <QTimer>

namespace {

constexpr int pollInterval = 5;
constexpr int dialogDelay = 300;

} // namespace

bool waitWithProgress(const std::function<bool()> &finished, const QString &label, QWidget *parent)
{
    if (finished())
        return true;

    QElapsedTimer elapsed;
    elapsed.start();
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(pollInterval);
    bool dialogShown = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (finished() || (!dialogShown && elapsed.elapsed() >= dialogDelay))
            loop.quit();
    });
    poll.start();
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (finished())
        return true;

    dialogShown = true;
    QProgressDialog dialog(label, QCoreApplication::translate("Background", "Stop Waiting"), 0, 0, parent);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setMinimumDuration(0);
    bool stopped = false;
    const auto stop = [&] {
        stopped = true;
        loop.quit();
    };
    QObject::connect(&dialog, &QProgressDialog::canceled, &loop, stop);
    QObject::connect(&dialog, &QDialog::rejected, &loop, stop); // Esc hides the dialog without `canceled`
    dialog.show();
    loop.exec();
    return !stopped;
}
