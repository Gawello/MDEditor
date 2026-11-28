#include "TestHarness.h"
#include "app/AppSettings.h"
#include "app/EditorTab.h"
#include "app/MainWindow.h"
#include "editor/MarkdownEditor.h"
#include "notes/NoteIndex.h"
#include "panels/FolderPanel.h"
#include "panels/SearchPanel.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemModel>
#include <QKeyEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeView>
#include <QTreeWidget>

namespace {

void writeNote(const QString &path, const QByteArray &text, int ageSeconds = 0)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(text);
    file.close();
    if (ageSeconds) {
        CHECK(file.open(QIODevice::ReadWrite));
        file.setFileTime(QDateTime::currentDateTime().addSecs(-ageSeconds), QFileDevice::FileModificationTime);
    }
}

bool waitFor(const std::function<bool()> &done, int ms = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QStringList shownNames(FolderPanel &panel)
{
    QTreeView *tree = panel.tree();
    QStringList names;
    const QModelIndex root = tree->rootIndex();
    for (int row = 0; row < tree->model()->rowCount(root); ++row)
        names << tree->model()->index(row, 0, root).data().toString();
    return names;
}

} // namespace

TEST_CASE(explorer_sorts_and_selects_several)
{
    QTemporaryDir dir;
    writeNote(dir.filePath(QStringLiteral("a.md")), "a", 3600);
    writeNote(dir.filePath(QStringLiteral("b.md")), "b", 60);
    writeNote(dir.filePath(QStringLiteral("c.md")), "c", 7200);
    writeNote(dir.filePath(QStringLiteral("sub/x.md")), "x");
    FolderPanel panel;
    panel.setRootPath(dir.path());
    CHECK(waitFor([&panel] { return shownNames(panel).size() == 4; }));
    panel.setSort(FolderPanel::Sort::NameAscending);
    CHECK(waitFor([&panel] { return shownNames(panel) == QStringList{"sub", "a", "b", "c"}; }));
    panel.setSort(FolderPanel::Sort::ModifiedNewest);
    CHECK(waitFor([&panel] { return shownNames(panel).mid(1) == QStringList{"b", "a", "c"}; }));
    writeNote(dir.filePath(QStringLiteral("n10.md")), "", 9000);
    writeNote(dir.filePath(QStringLiteral("n2.md")), "", 9000);
    panel.setSort(FolderPanel::Sort::NameAscending);
    CHECK(waitFor([&panel] { return shownNames(panel) == QStringList{"sub", "a", "b", "c", "n2", "n10"}; })); // natural order
    QFile::remove(dir.filePath(QStringLiteral("n10.md")));
    QFile::remove(dir.filePath(QStringLiteral("n2.md")));
    CHECK(waitFor([&panel] { return shownNames(panel).size() == 4; }));
    panel.setSort(FolderPanel::Sort::NameDescending);
    CHECK(waitFor([&panel] { return shownNames(panel) == QStringList{"sub", "c", "b", "a"}; })); // folders stay first
    CHECK(QSettings().value(QStringLiteral("explorer/sort")).toInt() == int(FolderPanel::Sort::NameDescending));
    panel.setSort(FolderPanel::Sort::NameAscending);

    // Ctrl+click selections; Delete asks about all of them at once.
    QTreeView *tree = panel.tree();
    for (int row = 0; row < tree->model()->rowCount(tree->rootIndex()); ++row) {
        const QModelIndex index = tree->model()->index(row, 0, tree->rootIndex());
        if (index.data().toString() == u"a" || index.data().toString() == u"c")
            tree->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    CHECK(panel.selectedPaths().size() == 2);
    QStringList deleting;
    QObject::connect(&panel, &FolderPanel::deleteManyRequested, [&deleting](const QStringList &paths) { deleting = paths; });
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(tree, &del);
    CHECK(deleting.size() == 2);
}

TEST_CASE(moving_several_notes_asks_once_and_updates_links)
{
    QSettings().remove(QStringLiteral("session"));
    QSettings().remove(QStringLiteral("window"));
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::updateLinksKey), QStringLiteral("always"));
    QTemporaryDir dir;
    writeNote(dir.filePath(QStringLiteral("Home.md")), "[a](A.md) [b](B.md) [[A]]\n");
    writeNote(dir.filePath(QStringLiteral("A.md")), "[home](Home.md)\n");
    writeNote(dir.filePath(QStringLiteral("B.md")), "b\n");
    QDir().mkpath(dir.filePath(QStringLiteral("archive")));
    {
        MainWindow window;
        window.show();
        CHECK(window.openFile(dir.filePath(QStringLiteral("Home.md"))));
        auto *index = window.findChild<NoteIndex *>();
        CHECK(waitFor([index] { return !index->scanning() && index->notes().size() == 3; }));
        auto *panel = window.findChild<FolderPanel *>();
        emit panel->moveManyRequested({dir.filePath(QStringLiteral("A.md")), dir.filePath(QStringLiteral("B.md"))},
                                      dir.filePath(QStringLiteral("archive")));
        CHECK(QFile::exists(dir.filePath(QStringLiteral("archive/A.md"))) && QFile::exists(dir.filePath(QStringLiteral("archive/B.md"))));
        CHECK(readText(dir.filePath(QStringLiteral("Home.md"))) == "[a](archive/A.md) [b](archive/B.md) [[A]]\n");
        CHECK(readText(dir.filePath(QStringLiteral("archive/A.md"))) == "[home](../Home.md)\n");

        // Search in a folder: only its files.
        auto *search = window.findChild<SearchPanel *>();
        CHECK(waitFor([index] { return !index->scanning() && index->notes().size() == 3; }));
        emit panel->searchInFolderRequested(dir.filePath(QStringLiteral("archive")));
        CHECK(search->scope() == QDir::cleanPath(dir.filePath(QStringLiteral("archive"))));
        search->focusSearch(QStringLiteral("home"));
        CHECK(waitFor([search] { return !search->searching() && search->results()->topLevelItemCount() > 0; }));
        for (int i = 0; i < search->results()->topLevelItemCount(); ++i) // Home.md itself isn't in the folder
            CHECK(search->results()->topLevelItem(i)->text(0).startsWith("archive/A.md"));
        search->setScope(QString());
        for (EditorTab *tab : window.findChildren<EditorTab *>())
            tab->editor()->document()->setModified(false);
        window.close();
    }
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::updateLinksKey), QStringLiteral("ask"));
}
