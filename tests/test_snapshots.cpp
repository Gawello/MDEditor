#include "TestHarness.h"
#include "app/AppSettings.h"
#include "app/EditorTab.h"
#include "app/MainWindow.h"
#include "app/VersionHistoryDialog.h"
#include "editor/MarkdownEditor.h"
#include "notes/Snapshots.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QSettings>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>

TEST_CASE(snapshots_keep_versions)
{
    QTemporaryDir store;
    const auto restore = qScopeGuard([previous = Snapshots::root()] { Snapshots::setRoot(previous); });
    Snapshots::setRoot(store.path());
    const QString note = QStringLiteral("/notes/a.md");
    const QDateTime t0(QDate(2026, 3, 1), QTime(10, 0));
    CHECK(Snapshots::record(note, QStringLiteral("one"), 5, true, t0));
    CHECK(!Snapshots::record(note, QStringLiteral("one"), 5, true, t0.addSecs(600)));      // the same text
    CHECK(!Snapshots::record(note, QStringLiteral("two"), 5, false, t0.addSecs(120)));     // too soon
    CHECK(Snapshots::record(note, QStringLiteral("two"), 5, true, t0.addSecs(120)));       // forced
    CHECK(Snapshots::record(note, QStringLiteral("three"), 5, false, t0.addSecs(120 + 360)));
    const QList<Snapshots::Entry> entries = Snapshots::list(note);
    CHECK(entries.size() == 3);
    CHECK(entries.size() == 3 && Snapshots::read(entries[0]) == "three" && Snapshots::read(entries[2]) == "one");
    CHECK(entries.size() == 3 && entries[0].time == t0.addSecs(480));
    CHECK(Snapshots::list(QStringLiteral("/notes/b.md")).isEmpty());
    // Unicode survives; old ones go after the given days, and an empty folder with them.
    CHECK(Snapshots::record(QStringLiteral("/notes/ż.md"), QStringLiteral("zażółć"), 0, true, t0));
    CHECK(Snapshots::read(Snapshots::list(QStringLiteral("/notes/ż.md")).value(0)) == "zażółć");
    Snapshots::prune(7, t0.addDays(7).addSecs(200));
    CHECK(Snapshots::list(note).size() == 1); // only "three" (10:08) is younger than 7 days
    CHECK(Snapshots::list(QStringLiteral("/notes/ż.md")).isEmpty());
    CHECK(!QDir(Snapshots::folderFor(QStringLiteral("/notes/ż.md"))).exists());
}

TEST_CASE(version_history_in_the_window)
{
    QTemporaryDir store;
    const auto restore = qScopeGuard([previous = Snapshots::root()] { Snapshots::setRoot(previous); });
    Snapshots::setRoot(store.path());
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("note.md"));
    {
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly));
        file.write("first version\n");
    }
    QSettings().remove(QStringLiteral("session"));
    QSettings().remove(QStringLiteral("window"));
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::snapshotMinutesKey), 0);
    MainWindow window;
    window.show();
    CHECK(window.openFile(path));
    // Opening keeps the note as it was (in the background).
    QElapsedTimer timer;
    timer.start();
    while (Snapshots::list(path).isEmpty() && timer.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(Snapshots::list(path).size() == 1);

    EditorTab *tab = window.findChild<EditorTab *>();
    tab->editor()->selectAll();
    tab->editor()->insertPlainText(QStringLiteral("second version\n"));
    QElapsedTimer pause;
    pause.start();
    while (pause.elapsed() < 1100)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().remove(u'&') == QStringLiteral("Save"))
            action->trigger();
    }
    timer.restart();
    while (Snapshots::list(path).size() < 2 && timer.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const QList<Snapshots::Entry> entries = Snapshots::list(path);
    CHECK(entries.size() == 2 && Snapshots::read(entries[0]) == "second version\n");

    // The dialog: the newest first; Restore brings the chosen one back.
    VersionHistoryDialog dialog(path, entries);
    QString restored;
    QObject::connect(&dialog, &VersionHistoryDialog::restoreRequested, [&restored](const QString &text) { restored = text; });
    dialog.list()->setCurrentRow(1);
    CHECK(dialog.selectedText() == "first version\n");
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text().remove(u'&') == QStringLiteral("Restore This Version"))
            button->click();
    }
    CHECK(restored == "first version\n");
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::snapshotMinutesKey), 5);
    tab->editor()->document()->setModified(false);
    window.close();
}
