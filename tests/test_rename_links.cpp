#include "TestHarness.h"
#include "app/AppSettings.h"
#include "app/EditorTab.h"
#include "app/MainWindow.h"
#include "editor/MarkdownEditor.h"
#include "notes/NoteIndex.h"
#include "panels/FolderPanel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>

namespace {

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool waitFor(const std::function<bool()> &done, int ms = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

struct Folder {
    QTemporaryDir dir;
    QString path(const QString &relative) const { return QDir::cleanPath(dir.filePath(relative)); }
    Folder()
    {
        writeBytes(path("Home.md"), "# Home\n[[Alpha]] [[Alpha#Goals|g]] ![[Alpha]] [m](Alpha.md) [s](sub/Beta.md#part)\n"
                                    "[[Beta]] [[Start]] `[[Alpha]]` [[Alpha.md]]\n```\n[[Alpha]]\n```\n");
        writeBytes(path("Alpha.md"), "---\naliases: [Start]\n---\n# Alpha\n## Goals\n[up](Home.md) [[Home]]\n");
        writeBytes(path("sub/Beta.md"), "\xEF\xBB\xBF# Beta\r\n[h](../Home.md) [[Alpha]] [a](../Alpha.md)\r\n");
        writeBytes(path("deep/Gamma.md"), "# Gamma\n");
    }
};

struct Window {
    MainWindow window;
    FolderPanel *panel;
    NoteIndex *index;
    explicit Window(const Folder &folder, int notes = 4)
    {
        window.show();
        CHECK(window.openFile(folder.path("Home.md")));
        panel = window.findChild<FolderPanel *>();
        index = window.findChild<NoteIndex *>();
        CHECK(waitFor([this, notes] { return !index->scanning() && index->notes().size() == notes; }));
    }
    ~Window()
    {
        for (EditorTab *tab : window.findChildren<EditorTab *>())
            tab->editor()->document()->setModified(false);
        window.close();
    }
    void settle(int notes)
    {
        CHECK(waitFor([this, notes] { return !index->scanning() && index->notes().size() == notes; }));
    }
};

void prepare(const QString &mode)
{
    QSettings().remove(QStringLiteral("session"));
    QSettings().remove(QStringLiteral("window"));
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::updateLinksKey), mode);
}

} // namespace

TEST_CASE(renaming_a_note_updates_links_to_it)
{
    prepare(QStringLiteral("always"));
    Folder folder;
    Window w(folder);

    emit w.panel->renameRequested(folder.path("Alpha.md"), QStringLiteral("Omega.md"));
    CHECK(QFile::exists(folder.path("Omega.md")) && !QFile::exists(folder.path("Alpha.md")));

    // Home is open in a tab: changed there (and saved, it had no other changes).
    EditorTab *home = nullptr;
    for (EditorTab *tab : w.window.findChildren<EditorTab *>()) {
        if (tab->filePath() == folder.path("Home.md"))
            home = tab;
    }
    CHECK(home && !home->isModified());
    const QString homeText = readText(folder.path("Home.md"));
    CHECK(homeText == "# Home\n[[Omega]] [[Omega#Goals|g]] ![[Omega]] [m](Omega.md) [s](sub/Beta.md#part)\n"
                      "[[Beta]] [[Start]] `[[Alpha]]` [[Omega.md]]\n```\n[[Alpha]]\n```\n");
    CHECK(home && home->editor()->markdown() == homeText);
    // Beta was closed: changed on disk, BOM and CRLF kept.
    CHECK(readText(folder.path("sub/Beta.md")) == QString::fromUtf8("\xEF\xBB\xBF# Beta\r\n[h](../Home.md) [[Omega]] [a](../Omega.md)\r\n"));
    // The renamed note's own links didn't need to change.
    CHECK(readText(folder.path("Omega.md")).endsWith("[up](Home.md) [[Home]]\n"));
    w.settle(4);
    CHECK(w.index->backlinks(folder.path("Omega.md")).size() >= 6);
}

TEST_CASE(moving_notes_and_folders_updates_relative_links)
{
    prepare(QStringLiteral("always"));
    Folder folder;
    Window w(folder);

    // A note into another folder: Markdown links to it and from it get new relative paths; [[Alpha]] stays.
    emit w.panel->moveRequested(folder.path("Alpha.md"), folder.path("deep"));
    CHECK(QFile::exists(folder.path("deep/Alpha.md")));
    CHECK(readText(folder.path("Home.md")).contains("[[Alpha]] [[Alpha#Goals|g]] ![[Alpha]] [m](deep/Alpha.md)"));
    CHECK(readText(folder.path("deep/Alpha.md")).endsWith("[up](../Home.md) [[Home]]\n"));
    CHECK(readText(folder.path("sub/Beta.md")).contains("[a](../deep/Alpha.md)"));
    w.settle(4);

    // A folder: the notes inside move with it.
    emit w.panel->moveRequested(folder.path("sub"), folder.path("deep"));
    CHECK(QFile::exists(folder.path("deep/sub/Beta.md")));
    CHECK(readText(folder.path("Home.md")).contains("[s](deep/sub/Beta.md#part)"));
    CHECK(readText(folder.path("deep/sub/Beta.md")).contains("[h](../../Home.md) [[Alpha]] [a](../Alpha.md)"));
    w.settle(4);

    // A second note named Alpha: [[Alpha]] must now say which.
    writeBytes(folder.path("Alpha2/x.md"), "");
    emit w.panel->renameRequested(folder.path("Alpha2/x.md"), QStringLiteral("Other.md"));
    w.settle(5);
    emit w.panel->renameRequested(folder.path("Home.md"), QStringLiteral("Start page.md"));
    CHECK(readText(folder.path("deep/Alpha.md")).contains("[up](../Start%20page.md) [[Start page]]"));
}

TEST_CASE(link_updates_can_be_turned_off)
{
    prepare(QStringLiteral("never"));
    Folder folder;
    Window w(folder);
    const QString before = readText(folder.path("sub/Beta.md"));
    emit w.panel->renameRequested(folder.path("Alpha.md"), QStringLiteral("Omega.md"));
    CHECK(QFile::exists(folder.path("Omega.md")));
    CHECK(readText(folder.path("sub/Beta.md")) == before);
    CHECK(readText(folder.path("Home.md")).contains("[[Alpha]]"));
    prepare(QStringLiteral("ask"));
}

TEST_CASE(same_name_notes_are_linked_by_path)
{
    prepare(QStringLiteral("always"));
    Folder folder;
    writeBytes(folder.path("Links.md"), "[[Gamma]] and [[Gamma#Part|p]]\n");
    Window w(folder, 5);
    w.settle(5);
    // deep/Gamma becomes deep/Home, but there is a Home at the top already: the link needs the path.
    emit w.panel->renameRequested(folder.path("deep/Gamma.md"), QStringLiteral("Home.md"));
    CHECK(readText(folder.path("Links.md")) == "[[deep/Home]] and [[deep/Home#Part|p]]\n");
    w.settle(5);
    CHECK(w.index->resolve("deep/Home", true, folder.path("Links.md")) == folder.path("deep/Home.md"));
    // Links to the other Home are left alone.
    CHECK(readText(folder.path("deep/Home.md")) == "# Gamma\n");
    CHECK(readText(folder.path("Alpha.md")).endsWith("[up](Home.md) [[Home]]\n"));
    prepare(QStringLiteral("ask"));
}

TEST_CASE(renaming_from_the_inline_title)
{
    prepare(QStringLiteral("always"));
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::inlineTitleKey), true);
    Folder folder;
    Window w(folder);
    EditorTab *home = nullptr;
    for (EditorTab *tab : w.window.findChildren<EditorTab *>()) {
        if (tab->filePath() == folder.path("Home.md"))
            home = tab;
    }
    CHECK(home != nullptr);
    if (!home)
        return;
    QLineEdit *title = home->findChild<QLineEdit *>();
    CHECK(title && title->isVisible() && title->text() == "Home");
    if (!title)
        return;
    title->setText(QStringLiteral("Start page"));
    emit title->editingFinished();
    QCoreApplication::processEvents();
    CHECK(QFile::exists(folder.path("Start page.md")) && !QFile::exists(folder.path("Home.md")));
    CHECK(home->filePath() == folder.path("Start page.md") && title->text() == "Start page");
    CHECK(readText(folder.path("Alpha.md")).contains("[[Start page]]")); // links followed
    // A name that can't be a file name is refused, and the title shows the real name again.
    title->setText(QStringLiteral("bad/name"));
    emit title->editingFinished();
    QCoreApplication::processEvents();
    CHECK(home->filePath() == folder.path("Start page.md"));
    CHECK(title->text() == "Start page");
    // Turned off: hidden.
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::inlineTitleKey), false);
    CHECK(!title->isVisible());
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::inlineTitleKey), true);
    prepare(QStringLiteral("ask"));
}
