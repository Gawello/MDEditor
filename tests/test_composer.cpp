#include "TestHarness.h"
#include "app/AppSettings.h"
#include "app/EditorTab.h"
#include "app/MainWindow.h"
#include "editor/CompletionPopup.h"
#include "editor/MarkdownEditor.h"
#include "markdown/NoteScan.h"
#include "notes/NoteIndex.h"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QTextBrowser>
#include <QUrl>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBlock>

namespace {

void writeNote(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(text);
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool waitFor(const std::function<bool()> &done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

EditorTab *tabOf(MainWindow &window, const QString &path)
{
    for (EditorTab *tab : window.findChildren<EditorTab *>()) {
        if (tab->filePath() == QDir::cleanPath(path))
            return tab;
    }
    return nullptr;
}

} // namespace

TEST_CASE(note_composer_extracts_and_merges)
{
    QSettings().remove(QStringLiteral("session"));
    QSettings().remove(QStringLiteral("window"));
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::updateLinksKey), QStringLiteral("always"));
    QTemporaryDir dir;
    const QString home = dir.filePath(QStringLiteral("Home.md"));
    writeNote(home, "# Home\nintro text\n## Plans\n- one\n- two\n## Later\nend\n");
    writeNote(dir.filePath(QStringLiteral("Draft.md")), "---\ntags: [x]\n---\nDraft body\n");
    writeNote(dir.filePath(QStringLiteral("Refs.md")), "See [[Draft]] and [[Home]]\n");
    MainWindow window;
    window.show();
    CHECK(window.openFile(home));
    auto *index = window.findChild<NoteIndex *>();
    CHECK(waitFor([index] { return !index->scanning() && index->notes().size() == 3; }));
    MarkdownEditor *editor = tabOf(window, home)->editor();

    // A heading's section becomes a note; a link stays in its place.
    editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(3)));
    CHECK(window.extractToNote(true, QStringLiteral("Plans")));
    CHECK(readText(dir.filePath(QStringLiteral("Plans.md"))) == "## Plans\n- one\n- two\n");
    CHECK(editor->markdown() == "# Home\nintro text\n[[Plans]]\n## Later\nend\n");

    // A selection too.
    QTextCursor cursor(editor->document()->findBlockByNumber(1));
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    CHECK(window.extractToNote(false, QStringLiteral("Intro")));
    CHECK(readText(dir.filePath(QStringLiteral("Intro.md"))) == "intro text\n");
    CHECK(editor->markdown().startsWith("# Home\n[[Intro]]\n[[Plans]]"));
    editor->undo(); // one step back
    CHECK(editor->markdown().startsWith("# Home\nintro text\n[[Plans]]"));
    CHECK(!window.extractToNote(false, QStringLiteral("Plans"))); // exists already

    // Draft merged into Home: its text at the end, links to it point to Home, the file goes.
    CHECK(window.openFile(dir.filePath(QStringLiteral("Draft.md"))));
    CHECK(window.mergeInto(QDir::cleanPath(home), false));
    CHECK(!QFile::exists(dir.filePath(QStringLiteral("Draft.md"))));
    CHECK(tabOf(window, dir.filePath(QStringLiteral("Draft.md"))) == nullptr);
    CHECK(tabOf(window, home)->editor()->markdown().endsWith("end\n\nDraft body\n"));
    CHECK(readText(dir.filePath(QStringLiteral("Refs.md"))) == "See [[Home]] and [[Home]]\n");
    for (EditorTab *tab : window.findChildren<EditorTab *>())
        tab->editor()->document()->setModified(false);
    window.close();
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::updateLinksKey), QStringLiteral("ask"));
}

TEST_CASE(block_links_give_blocks_their_id)
{
    QSettings().remove(QStringLiteral("session"));
    QSettings().remove(QStringLiteral("window"));
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::autoPairBracketsKey), false);
    QTemporaryDir dir;
    const QString home = dir.filePath(QStringLiteral("Home.md"));
    writeNote(home, "# Home\n\n");
    writeNote(dir.filePath(QStringLiteral("Other.md")), "# Other\nFirst thought here.\n- a list item\nKept ^old1\n");
    MainWindow window;
    window.show();
    CHECK(window.openFile(home));
    auto *index = window.findChild<NoteIndex *>();
    CHECK(waitFor([index] { return !index->scanning() && index->notes().size() == 2; }));
    MarkdownEditor *editor = tabOf(window, home)->editor();
    editor->setFocus();
    editor->moveCursor(QTextCursor::End);
    for (const QChar c : QStringLiteral("[[Other#^thought")) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        QApplication::sendEvent(editor, &press);
    }
    CHECK(editor->completionPopup() && editor->completionPopup()->isVisible());
    CHECK(editor->completionPopup() && editor->completionPopup()->items().value(0).label == "First thought here.");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(editor, &enter);
    QCoreApplication::processEvents();
    const QString line = editor->document()->lastBlock().text();
    CHECK(line.startsWith("[[Other#^") && line.endsWith("]]") && line.size() == 17);
    const QString id = line.mid(9, 6);
    CHECK(waitFor([&dir, &id] { return readText(dir.filePath(QStringLiteral("Other.md"))).contains("First thought here. ^" + id + "\n"); }));
    // Blocks that have an id are offered with it.
    const md::NoteScan scan = md::scanNote(u"text ^aa\nplain\n- item\n# Head\n");
    CHECK(scan.blocks.size() == 1 && scan.paragraphs.size() == 2 && scan.paragraphs[1].text == u"item");
    for (EditorTab *tab : window.findChildren<EditorTab *>())
        tab->editor()->document()->setModified(false);
    window.close();
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::autoPairBracketsKey), true);
}

TEST_CASE(reading_mode)
{
    CHECK(MarkdownEditor::excerpt(QStringLiteral("See [[Other#Part|the part]] and [[My Note]]"), {}, true)
          == "See [the part](Other#Part) and [My Note](My%20Note)");
    QSettings().remove(QStringLiteral("session"));
    QSettings().remove(QStringLiteral("window"));
    QTemporaryDir dir;
    const QString home = dir.filePath(QStringLiteral("Home.md"));
    writeNote(home, "---\ntags: [x]\n---\n# Home\n> [!tip] Handy\n> body\n\nGo to [[Other]].\n");
    writeNote(dir.filePath(QStringLiteral("Other.md")), "# Other\n");
    MainWindow window;
    window.show();
    CHECK(window.openFile(home));
    EditorTab *tab = tabOf(window, home);
    QAction *reading = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text().remove(u'&') == QStringLiteral("Reading Mode"))
            reading = action;
    }
    CHECK(reading != nullptr);
    if (!reading)
        return;
    reading->trigger();
    CHECK(tab->isReading() && !tab->editor()->isVisible());
    const QString shown = tab->reader()->toPlainText();
    CHECK(shown.contains("Handy") && shown.contains("Go to Other.") && !shown.contains("tags") && !shown.contains("[["));
    // Its links follow like the editor's.
    emit tab->reader()->anchorClicked(QUrl(QStringLiteral("Other")));
    EditorTab *other = tabOf(window, dir.filePath(QStringLiteral("Other.md")));
    CHECK(other != nullptr);
    window.openFile(home);
    reading->trigger();
    CHECK(!tab->isReading() && tab->editor()->isVisible());
    for (EditorTab *each : window.findChildren<EditorTab *>())
        each->editor()->document()->setModified(false);
    window.close();
}
