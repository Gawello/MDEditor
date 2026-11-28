#include "TestHarness.h"
#include "app/AppSettings.h"
#include "app/EditorTab.h"
#include "app/MainWindow.h"
#include "editor/MarkdownEditor.h"
#include "notes/NoteIndex.h"

#include <QDir>
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
