#include "TestHarness.h"
#include "app/AppSettings.h"
#include "editor/MarkdownEditor.h"

#include <QApplication>
#include <QKeyEvent>
#include <QTextBlock>

namespace {

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

QString text(const MarkdownEditor &editor)
{
    return editor.markdown();
}

// The text with '|' where the cursor is (and [ ] around a selection).
QString shown(const MarkdownEditor &editor)
{
    QString result = editor.markdown();
    const QTextCursor cursor = editor.textCursor();
    if (!cursor.hasSelection())
        return result.insert(cursor.position(), u'|');
    result.insert(cursor.selectionEnd(), u']');
    return result.insert(cursor.selectionStart(), u'[');
}

void select(MarkdownEditor &editor, int from, int to)
{
    QTextCursor cursor(editor.document());
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
}

void setPairing(bool brackets, bool markdown)
{
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::autoPairBracketsKey), brackets);
    AppSettings::instance().setValue(QString::fromLatin1(AppSettings::autoPairMarkdownKey), markdown);
}

} // namespace

TEST_CASE(typing_closes_brackets_and_quotes)
{
    setPairing(true, true);
    MarkdownEditor editor;
    type(editor, "see (");
    CHECK(shown(editor) == "see (|)");
    type(editor, "x)");
    CHECK(shown(editor) == "see (x)|");
    type(editor, " \"hi\" don't [[");
    CHECK(shown(editor) == "see (x) \"hi\" don't [[|]]");
    type(editor, "Note]]");
    CHECK(shown(editor) == "see (x) \"hi\" don't [[Note]]|");

    // One undo step takes a pair back; Backspace in an empty pair removes both.
    editor.setMarkdown(QString());
    type(editor, "(");
    editor.undo();
    CHECK(text(editor).isEmpty());
    type(editor, "a `");
    CHECK(shown(editor) == "a `|`");
    key(editor, Qt::Key_Backspace);
    CHECK(shown(editor) == "a |");
    type(editor, "(word");
    key(editor, Qt::Key_Backspace);
    CHECK(shown(editor) == "a (wor|)"); // not an empty pair: one character
}

TEST_CASE(typing_wraps_a_selection)
{
    setPairing(true, true);
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("make this bold"));
    select(editor, 5, 9);
    type(editor, "*");
    CHECK(shown(editor) == "make *[this]* bold");
    type(editor, "*");
    CHECK(shown(editor) == "make **[this]** bold");
    editor.undo();
    CHECK(shown(editor) == "make *this* bold" || text(editor) == "make *this* bold");
    select(editor, 0, 4);
    type(editor, "(");
    CHECK(shown(editor) == "([make]) *this* bold");
    // Across lines, typing replaces the selection as usual.
    editor.setMarkdown(QStringLiteral("a\nb"));
    select(editor, 0, 3);
    type(editor, "(");
    CHECK(text(editor) == "()" || text(editor) == "(");
}

TEST_CASE(pairing_follows_the_settings_and_the_mode)
{
    MarkdownEditor editor;
    setPairing(false, false);
    type(editor, "(\"[");
    CHECK(shown(editor) == "(\"[|");

    // Plain-text files: brackets yes, Markdown markers no.
    setPairing(true, true);
    editor.setMarkdown(QString());
    editor.setMarkdownMode(false);
    type(editor, "f(");
    CHECK(shown(editor) == "f(|)");
    editor.setMarkdown(QStringLiteral("x = y"));
    select(editor, 0, 1);
    type(editor, "*");
    CHECK(text(editor) == "* = y");

    // Shortcuts aren't typing.
    editor.setMarkdown(QString());
    QKeyEvent ctrl(QEvent::KeyPress, Qt::Key_ParenLeft, Qt::ControlModifier, QStringLiteral("("));
    QApplication::sendEvent(&editor, &ctrl);
    CHECK(text(editor) != "()");
    setPairing(true, true);
}
