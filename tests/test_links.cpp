#include "TestHarness.h"
#include "app/EditorTab.h"
#include "app/MainWindow.h"
#include "editor/MarkdownEditor.h"
#include "notes/NoteIndex.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QMenu>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>

namespace {

void writeNote(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(text.toUtf8());
}

QAction *action(QWidget &window, const QString &text)
{
    for (QAction *candidate : window.findChildren<QAction *>()) {
        if (QString(candidate->text()).remove(u'&').section(u'\t', 0, 0) == text)
            return candidate;
    }
    return nullptr;
}

QList<QTabWidget *> panes(MainWindow &window)
{
    QList<QTabWidget *> result;
    for (QTabWidget *candidate : window.findChildren<QTabWidget *>()) {
        if (candidate->isVisibleTo(&window))
            result << candidate;
    }
    return result;
}

EditorTab *current(MainWindow &window)
{
    for (QTabWidget *pane : panes(window)) {
        auto *tab = qobject_cast<EditorTab *>(pane->currentWidget());
        if (tab && tab->editor()->hasFocus())
            return tab;
    }
    return qobject_cast<EditorTab *>(panes(window).value(0)->currentWidget());
}

// Puts the cursor `offset` characters into the first occurrence of `text` and presses `key` with `modifiers`.
void pressAt(MarkdownEditor *editor, const QString &text, int offset, Qt::KeyboardModifiers modifiers)
{
    const int at = int(editor->document()->toRawText().indexOf(text));
    CHECK(at >= 0);
    QTextCursor cursor(editor->document());
    cursor.setPosition(at + offset);
    editor->setTextCursor(cursor);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, modifiers);
    QApplication::sendEvent(editor, &press);
}

int cursorLine(MarkdownEditor *editor)
{
    return editor->textCursor().block().blockNumber();
}

struct Folder {
    QTemporaryDir dir;
    QString path(const QString &relative) const { return QDir::cleanPath(dir.filePath(relative)); }
    Folder()
    {
        writeNote(path("Home.md"), "# Home\nSee [[Alpha#Goals]], [[Start]], [[New Note]] and [[#Later]].\n"
                                   "[md](sub/Beta.md#second-part) and [[Beta#^b1]]\n\n## Later\nend\n");
        writeNote(path("Alpha.md"), "---\naliases: [Start, \"Kick off\"]\n---\n# Alpha\n## Intro\ntext\n## Goals\n- ship\n");
        writeNote(path("sub/Beta.md"), "# Beta\n## Second part\nline\nblock text ^b1\nmore\n");
    }
};

void waitForIndex(MainWindow &window)
{
    // The index scans the folder in the background; aliases need it.
    auto *index = window.findChild<NoteIndex *>();
    CHECK(index != nullptr);
    QElapsedTimer timer;
    timer.start();
    while (index && (index->scanning() || index->notes().size() < 3) && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

void closeQuietly(MainWindow &window)
{
    for (EditorTab *tab : window.findChildren<EditorTab *>())
        tab->editor()->document()->setModified(false);
    window.close();
}

} // namespace

TEST_CASE(link_anchors_find_headings_and_blocks)
{
    const QString text = QStringLiteral("---\ntitle: x\n---\n# Top\n## My Heading\ntext\n### Zażółć gęślą\n"
                                        "- item ^list1\n\n| a |\n|---|\n\n^table1\n## Second part\n");
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("My Heading")) == 4);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("my heading")) == 4);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("My%20Heading")) == 4);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("my-heading")) == 4);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("Top#My Heading")) == 4);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("ZAŻÓŁĆ gęślą")) == 6);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("zażółć-gęślą")) == 6);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("^list1")) == 7);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("^table1")) == 10);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("second-part")) == 13);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("Nowhere")) == -1);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("^nope")) == -1);
    CHECK(NoteIndex::findAnchor(text, QString()) == -1);
    CHECK(NoteIndex::findAnchor(text, QStringLiteral("title: x")) == -1); // frontmatter isn't a heading
}

TEST_CASE(aliases_resolve_links)
{
    Folder folder;
    NoteIndex index;
    index.setRoot(folder.dir.path());
    QElapsedTimer timer;
    timer.start();
    while ((index.scanning() || index.notes().size() < 3) && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK((index.aliases(folder.path("Alpha.md")) == QStringList{"Start", "Kick off"}));
    CHECK(index.resolve("Start", true, folder.path("Home.md")) == folder.path("Alpha.md"));
    CHECK(index.resolve("kick off", true, folder.path("Home.md")) == folder.path("Alpha.md"));
    CHECK(index.resolve("Start", false, folder.path("Home.md")).isEmpty()); // Markdown links are paths
    // A note named like another note's alias wins.
    index.update(folder.path("Start.md"), QStringLiteral("# Start\n"));
    CHECK(index.resolve("Start", true, folder.path("Home.md")) == folder.path("Start.md"));
    // Saving a note with new aliases updates them.
    index.update(folder.path("sub/Beta.md"), QStringLiteral("---\nalias: Second\n---\n# Beta\n"));
    CHECK(index.resolve("Second", true, folder.path("Home.md")) == folder.path("sub/Beta.md"));
}

TEST_CASE(following_links_opens_headings_creates_notes)
{
    Folder folder;
    QSettings().remove(QStringLiteral("session"));
    MainWindow window;
    window.show();
    CHECK(window.openFile(folder.path("Home.md")));
    waitForIndex(window);
    MarkdownEditor *home = current(window)->editor();

    // Alt+Enter on [[Alpha#Goals]]: Alpha, at its Goals heading.
    pressAt(home, QStringLiteral("[[Alpha#Goals]]"), 4, Qt::AltModifier);
    CHECK(current(window)->filePath() == folder.path("Alpha.md"));
    CHECK(cursorLine(current(window)->editor()) == 6);
    action(window, QStringLiteral("Back"))->trigger();
    CHECK(current(window)->filePath() == folder.path("Home.md"));

    // An alias, by Ctrl+Enter.
    pressAt(home, QStringLiteral("[[Start]]"), 3, Qt::ControlModifier);
    CHECK(current(window)->filePath() == folder.path("Alpha.md"));
    action(window, QStringLiteral("Back"))->trigger();

    // A Markdown link with a slug, and a block reference.
    pressAt(home, QStringLiteral("[md](sub/Beta.md#second-part)"), 2, Qt::AltModifier);
    CHECK(current(window)->filePath() == folder.path("sub/Beta.md"));
    CHECK(cursorLine(current(window)->editor()) == 1);
    action(window, QStringLiteral("Back"))->trigger();
    pressAt(home, QStringLiteral("[[Beta#^b1]]"), 3, Qt::AltModifier);
    CHECK(current(window)->filePath() == folder.path("sub/Beta.md"));
    CHECK(cursorLine(current(window)->editor()) == 3);
    action(window, QStringLiteral("Back"))->trigger();
    CHECK(current(window)->filePath() == folder.path("Home.md"));

    // A heading of this note: a jump inside it, undone by Back.
    pressAt(home, QStringLiteral("[[#Later]]"), 3, Qt::AltModifier);
    CHECK(current(window)->filePath() == folder.path("Home.md"));
    CHECK(cursorLine(home) == 4);
    action(window, QStringLiteral("Back"))->trigger();
    CHECK(cursorLine(home) == 1);

    // A link to a note that doesn't exist creates it, next to this note.
    CHECK(!QFile::exists(folder.path("New Note.md")));
    pressAt(home, QStringLiteral("[[New Note]]"), 3, Qt::AltModifier);
    CHECK(QFile::exists(folder.path("New Note.md")));
    CHECK(current(window)->filePath() == folder.path("New Note.md"));
    action(window, QStringLiteral("Back"))->trigger();

    // Plain Enter still edits; Alt+Enter away from a link does nothing special.
    const int lines = home->document()->blockCount();
    pressAt(home, QStringLiteral("end"), 3, Qt::NoModifier);
    CHECK(home->document()->blockCount() == lines + 1);
    home->undo();
    CHECK(home->document()->blockCount() == lines);
    closeQuietly(window);
}

TEST_CASE(links_open_to_the_right)
{
    Folder folder;
    QSettings().remove(QStringLiteral("session"));
    MainWindow window;
    window.show();
    CHECK(window.openFile(folder.path("Home.md")));
    MarkdownEditor *home = current(window)->editor();
    pressAt(home, QStringLiteral("[[Alpha#Goals]]"), 4, Qt::ControlModifier | Qt::AltModifier);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QList<QTabWidget *> both = panes(window);
    CHECK(both.size() == 2);
    // One pane has Home only, the other Alpha only (the copy of Home that made the pane went away).
    QStringList contents;
    for (QTabWidget *pane : both) {
        QStringList names;
        for (int i = 0; i < pane->count(); ++i)
            names << QFileInfo(qobject_cast<EditorTab *>(pane->widget(i))->filePath()).fileName();
        contents << names.join(u',');
    }
    contents.sort();
    CHECK((contents == QStringList{"Alpha.md", "Home.md"}));
    CHECK(current(window)->filePath() == folder.path("Alpha.md"));
    CHECK(cursorLine(current(window)->editor()) == 6);

    // Again from Home: Alpha is already on the other side, so no new tab.
    pressAt(home, QStringLiteral("[[Alpha#Goals]]"), 4, Qt::ControlModifier | Qt::AltModifier);
    CHECK(window.findChildren<EditorTab *>().size() == 2);
    closeQuietly(window);
}

TEST_CASE(link_context_menu)
{
    Folder folder;
    MarkdownEditor editor;
    editor.setDocumentPath(folder.path("Home.md"));
    QFile file(folder.path("Home.md"));
    CHECK(file.open(QIODevice::ReadOnly));
    editor.setMarkdown(QString::fromUtf8(file.readAll()));
    const int at = int(editor.document()->toRawText().indexOf(QStringLiteral("[[Alpha")));
    QTextCursor cursor(editor.document());
    cursor.setPosition(at + 3);
    editor.setTextCursor(cursor);
    std::unique_ptr<QMenu> menu = editor.createContextMenu(editor.cursorRect().center());
    QStringList texts;
    for (QAction *entry : menu->actions())
        texts << entry->text().remove(u'&').section(u'\t', 0, 0);
    CHECK(texts.contains("Open Link") && texts.contains("Open to the Right"));
    CHECK(texts.contains("Reveal in Folder Panel") && texts.contains("Rename Linked File…"));

    // The reveal entry reports the linked file.
    QString revealed;
    QObject::connect(&editor, &MarkdownEditor::revealRequested, [&revealed](const QString &path) { revealed = path; });
    for (QAction *entry : menu->actions()) {
        if (entry->text().remove(u'&') == QStringLiteral("Reveal in Folder Panel"))
            entry->trigger();
    }
    CHECK(revealed == folder.path("Alpha.md"));

    // A missing note: no file entries.
    cursor.setPosition(int(editor.document()->toRawText().indexOf(QStringLiteral("[[New"))) + 3);
    editor.setTextCursor(cursor);
    menu = editor.createContextMenu(editor.cursorRect().center());
    texts.clear();
    for (QAction *entry : menu->actions())
        texts << entry->text().remove(u'&').section(u'\t', 0, 0);
    CHECK(texts.contains("Open to the Right") && !texts.contains("Reveal in Folder Panel"));
}
