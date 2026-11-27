#include "TestHarness.h"
#include "app/EditorTab.h"
#include "app/MainWindow.h"
#include "editor/MarkdownEditor.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(data);
}

void type(MarkdownEditor *editor, int position, const QString &text, int remove = 0)
{
    QTextCursor cursor(editor->document());
    cursor.setPosition(position);
    cursor.setPosition(position + remove, QTextCursor::KeepAnchor);
    cursor.insertText(text);
}

} // namespace

TEST_CASE(twin_tabs_mirror_every_edit)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("note.md"));
    writeFile(path, "# Title\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n\nend\n");
    EditorTab a;
    EditorTab b;
    QString error;
    CHECK(a.load(path, error));
    a.openTwin(&b);
    CHECK(b.filePath() == a.filePath());
    CHECK(b.editor()->markdown() == a.editor()->markdown());
    CHECK(!b.isModified());

    type(a.editor(), 2, QStringLiteral("New "));                // inside a line
    type(a.editor(), 0, QStringLiteral("first\nsecond\n"));     // new lines
    type(b.editor(), 3, QString(), 12);                         // a deletion across lines, from the other side
    type(b.editor(), b.editor()->document()->characterCount() - 1, QStringLiteral("\nlast")); // at the very end
    CHECK(a.editor()->markdown() == b.editor()->markdown());
    CHECK(a.isModified() && b.isModified());

    a.editor()->undo();
    a.editor()->undo();
    CHECK(a.editor()->markdown() == b.editor()->markdown());

    // A save in one counts for both.
    CHECK(a.save(path, error) == EditorTab::SaveResult::Saved);
    CHECK(!b.isModified());
    type(b.editor(), 0, QStringLiteral("x"));
    CHECK(b.save(path, error) == EditorTab::SaveResult::Saved); // no conflict: b knows the version a wrote
    CHECK(!a.isModified());

    // Formatting changes (live preview, cursor moves) aren't edits.
    a.editor()->setMarkdownMode(false);
    a.editor()->setMarkdownMode(true);
    CHECK(!a.isModified() && !b.isModified());

    b.unlinkTwin();
    type(a.editor(), 0, QStringLiteral("only a"));
    CHECK(a.editor()->markdown() != b.editor()->markdown());
}

namespace {

QAction *action(MainWindow &window, const QString &text)
{
    for (QAction *candidate : window.findChildren<QAction *>()) {
        if (QString(candidate->text()).remove(u'&') == text)
            return candidate;
    }
    return nullptr;
}

QList<QTabWidget *> panes(MainWindow &window)
{
    QList<QTabWidget *> result;
    for (QTabWidget *pane : window.findChildren<QTabWidget *>()) {
        if (pane->isVisibleTo(&window))
            result << pane;
    }
    return result;
}

} // namespace

TEST_CASE(split_view_shows_a_file_in_two_panes)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("note.md"));
    writeFile(path, "hello\n");
    QSettings().remove(QStringLiteral("session"));
    {
        MainWindow window;
        window.show();
        CHECK(window.openFile(path));
        CHECK(panes(window).size() == 1);

        action(window, QStringLiteral("Split Right"))->trigger();
        CHECK(panes(window).size() == 2);
        const QList<EditorTab *> tabs = window.findChildren<EditorTab *>();
        CHECK(tabs.size() == 2);
        CHECK(tabs.size() == 2 && tabs[0]->twin() == tabs[1]);

        // An edit in one pane shows in the other; closing one view of the file asks nothing.
        tabs[1]->editor()->insertPlainText(QStringLiteral("both "));
        CHECK(tabs[0]->editor()->markdown() == tabs[1]->editor()->markdown());
        action(window, QStringLiteral("Close Tab"))->trigger();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(window.findChildren<EditorTab *>().size() == 1);
        CHECK(panes(window).size() == 1); // the emptied pane went
        for (EditorTab *tab : window.findChildren<EditorTab *>()) {
            CHECK(tab->isModified() && !tab->twin());
            tab->editor()->document()->setModified(false); // no "unsaved changes" question on close
        }
        window.close();
    }
    QSettings().remove(QStringLiteral("session"));
}
