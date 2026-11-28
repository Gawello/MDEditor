#include "TestHarness.h"
#include "app/AppSettings.h"
#include "editor/CompletionPopup.h"
#include "editor/MarkdownEditor.h"
#include "notes/Completions.h"
#include "notes/NoteIndex.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QListWidget>
#include <QScopeGuard>
#include <QTemporaryDir>

namespace {

void writeNote(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(text.toUtf8());
}

struct Folder {
    QTemporaryDir dir;
    NoteIndex index;
    QString path(const QString &relative) const { return QDir::cleanPath(dir.filePath(relative)); }
    Folder()
    {
        writeNote(path("Home.md"), "# Home\n## Plans\ntext ^h1\n#idea\n");
        writeNote(path("Alpha.md"), "---\naliases: [Start]\ntags: [proj/x]\n---\n# Alpha\n## Goals\n- ship ^g1\n#idea\n");
        writeNote(path("sub/Alpha.md"), "# Other alpha\n#project\n");
        writeNote(path("sub/Beta.md"), "# Beta\n");
        writeNote(path("img/pic.png"), "png");
        index.setRoot(dir.path());
        QElapsedTimer timer;
        timer.start();
        while ((index.scanning() || index.notes().size() < 4) && timer.elapsed() < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
};

md::CompletionContext context(md::CompletionKind kind, const std::u16string &query, const std::u16string &note = {})
{
    md::CompletionContext result;
    result.kind = kind;
    result.query = query;
    result.note = note;
    return result;
}

QStringList labels(const QList<CompletionItem> &items)
{
    QStringList result;
    for (const CompletionItem &item : items)
        result << item.label;
    return result;
}

void type(MarkdownEditor &editor, const QString &text)
{
    for (const QChar c : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        QApplication::sendEvent(&editor, &press);
    }
}

void key(MarkdownEditor &editor, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(&editor, &press);
}

bool popupShown(const MarkdownEditor &editor)
{
    return editor.completionPopup() && editor.completionPopup()->isVisible();
}

} // namespace

TEST_CASE(suggestions_for_notes_headings_blocks_tags)
{
    Folder folder;
    const QString home = folder.path("Home.md");
    using K = md::CompletionKind;

    // Notes: the best match first; two notes named Alpha are told apart by their path; aliases too.
    QList<CompletionItem> items = Completions::suggest(folder.index, context(K::Note, u"alp"), home, {});
    CHECK(items.size() >= 2);
    CHECK(labels(items).count("Alpha") == 2);
    QStringList inserts;
    for (const CompletionItem &item : items)
        inserts << item.insert;
    CHECK(inserts.contains("Alpha") && inserts.contains("sub/Alpha"));
    items = Completions::suggest(folder.index, context(K::Note, u"sta"), home, {});
    CHECK(!items.isEmpty() && items[0].label == "Start" && items[0].insert == "Alpha|Start");
    // Other files by their full name, after the notes; the note being edited isn't offered.
    items = Completions::suggest(folder.index, context(K::Note, u""), home, {});
    CHECK(labels(items).contains("pic.png") && !labels(items).contains("Home"));
    CHECK(labels(items).indexOf("pic.png") > labels(items).indexOf("Beta"));
    CHECK(Completions::suggest(folder.index, context(K::Note, u"zzz"), home, {}).isEmpty());

    // Headings of another note, or of this one (from its current text).
    items = Completions::suggest(folder.index, context(K::Heading, u"", u"Alpha"), home, {});
    CHECK((labels(items) == QStringList{"Alpha", "Goals"}));
    CHECK(items.size() == 2 && items[1].detail == "H2");
    items = Completions::suggest(folder.index, context(K::Heading, u"pl"), home, QStringLiteral("# Home\n## Plans\n"));
    CHECK((labels(items) == QStringList{"Plans"}));

    // Blocks with ids.
    items = Completions::suggest(folder.index, context(K::Block, u"", u"Alpha"), home, {});
    CHECK(items.size() == 1 && items[0].insert == "g1" && items[0].label == "ship");

    // Tags, most used first.
    items = Completions::suggest(folder.index, context(K::Tag, u""), home, {});
    CHECK(!items.isEmpty() && items[0].label == "#idea" && items[0].insert == "idea");
    items = Completions::suggest(folder.index, context(K::Tag, u"proj/"), home, {});
    CHECK(labels(items).contains("#proj/x"));
}

TEST_CASE(typing_brackets_shows_and_inserts_suggestions)
{
    // Without auto-pairing here ("[[" stays open); typing_with_pairs_and_suggestions has both.
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::autoPairBracketsKey), false);
    const auto restore = qScopeGuard(
        [] { AppSettings::instance().setValue(QString::fromLatin1(AppSettings::autoPairBracketsKey), true); });
    Folder folder;
    NoteIndex *index = &folder.index;
    MarkdownEditor::setCompletionProvider([index](const md::CompletionContext &c, const QString &from, const QString &text) {
        return Completions::suggest(*index, c, from, text);
    });
    MarkdownEditor editor;
    editor.setDocumentPath(folder.path("Home.md"));
    editor.setMarkdown(QStringLiteral("# Home\n## Plans\n"));
    editor.moveCursor(QTextCursor::End);

    type(editor, "See [[Bet");
    CHECK(popupShown(editor));
    CHECK(editor.completionPopup()->items().value(0).label == "Beta");
    key(editor, Qt::Key_Return);
    CHECK(!popupShown(editor));
    CHECK(editor.document()->lastBlock().text() == "See [[Beta]]");
    CHECK(editor.textCursor().atBlockEnd());
    // One undo step takes the completion back.
    editor.undo();
    CHECK(editor.document()->lastBlock().text() == "See [[Bet");

    // Escape closes the list; it stays closed while typing on, until a new link starts.
    editor.moveCursor(QTextCursor::End);
    type(editor, "a");
    CHECK(popupShown(editor));
    key(editor, Qt::Key_Escape);
    CHECK(!popupShown(editor));
    type(editor, "x");
    CHECK(!popupShown(editor));

    // Headings with '#', inside brackets that are already closed: the cursor ends up after them.
    editor.setPlainText(QStringLiteral("## Plans\n"));
    editor.moveCursor(QTextCursor::End);
    type(editor, "[[#");
    QTextCursor cursor = editor.textCursor();
    cursor.insertText(QStringLiteral("]]"));
    cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 2);
    editor.setTextCursor(cursor);
    type(editor, "P");
    CHECK(popupShown(editor) && editor.completionPopup()->items().value(0).label == "Plans");
    key(editor, Qt::Key_Tab);
    CHECK(editor.document()->toPlainText() == "## Plans\n[[#Plans]]");
    CHECK(editor.textCursor().atEnd());

    // Arrows move through the list; Enter takes the selected entry.
    editor.selectAll();
    editor.textCursor().removeSelectedText();
    type(editor, "[[alp");
    CHECK(popupShown(editor) && editor.completionPopup()->items().size() >= 2);
    key(editor, Qt::Key_Down);
    CHECK(editor.completionPopup()->currentRow() == 1);
    key(editor, Qt::Key_Up);
    key(editor, Qt::Key_Up); // wraps to the last
    CHECK(editor.completionPopup()->currentRow() == editor.completionPopup()->items().size() - 1);
    key(editor, Qt::Key_Escape);

    // Tags.
    editor.selectAll();
    editor.textCursor().removeSelectedText();
    type(editor, "text #id");
    CHECK(popupShown(editor) && editor.completionPopup()->items().value(0).label == "#idea");
    key(editor, Qt::Key_Return);
    CHECK(editor.document()->toPlainText() == "text #idea");

    // Moving the cursor away closes it; clicking a row inserts it.
    type(editor, " [[Bet");
    CHECK(popupShown(editor));
    editor.moveCursor(QTextCursor::Start);
    CHECK(!popupShown(editor));
    editor.moveCursor(QTextCursor::End);
    type(editor, "a");
    CHECK(popupShown(editor));
    emit editor.completionPopup()->activated(0);
    CHECK(editor.document()->toPlainText() == "text #idea [[Beta]]");

    // Not in code blocks or plain-text mode.
    editor.setPlainText(QStringLiteral("```\n"));
    editor.moveCursor(QTextCursor::End);
    type(editor, "[[Bet");
    CHECK(!popupShown(editor));
    editor.setPlainText(QString());
    editor.setMarkdownMode(false);
    type(editor, "[[Bet");
    CHECK(!popupShown(editor));
    MarkdownEditor::setCompletionProvider({});
}

TEST_CASE(typing_with_pairs_and_suggestions)
{
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::autoPairBracketsKey), true);
    Folder folder;
    NoteIndex *index = &folder.index;
    MarkdownEditor::setCompletionProvider([index](const md::CompletionContext &c, const QString &from, const QString &text) {
        return Completions::suggest(*index, c, from, text);
    });
    MarkdownEditor editor;
    editor.setDocumentPath(folder.path("Home.md"));
    type(editor, "See [[Bet");
    CHECK(editor.markdown() == "See [[Bet]]"); // the brackets closed themselves
    CHECK(popupShown(editor));
    key(editor, Qt::Key_Return);
    CHECK(editor.markdown() == "See [[Beta]]" && editor.textCursor().atEnd());
    editor.undo();
    CHECK(editor.markdown() == "See [[Bet]]");
    MarkdownEditor::setCompletionProvider({});
}
