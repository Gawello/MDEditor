#include "TestHarness.h"
#include "app/AppSettings.h"
#include "editor/MarkdownEditor.h"
#include "markdown/LineStyle.h"

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

TEST_CASE(moving_and_deleting_lines)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("a\nbb\nc"));
    QTextCursor cursor(editor.document()->findBlockByNumber(1));
    cursor.movePosition(QTextCursor::Right);
    editor.setTextCursor(cursor);
    CHECK(editor.moveLines(-1));
    CHECK(shown(editor) == "b|b\na\nc");
    CHECK(!editor.moveLines(-1)); // already first
    CHECK(editor.moveLines(1) && editor.moveLines(1));
    CHECK(shown(editor) == "a\nc\nb|b");
    CHECK(!editor.moveLines(1));
    editor.undo();
    CHECK(text(editor) == "a\nbb\nc");

    // A selection over two lines moves both and stays selected.
    editor.setMarkdown(QStringLiteral("1\n2\n3\n4"));
    select(editor, 2, 5); // "2\n3"
    CHECK(editor.moveLines(1));
    CHECK(shown(editor) == "1\n4\n[2\n3]");

    // A folded section moves past as a whole.
    editor.setMarkdown(QStringLiteral("# A\nx\n# B\ny"));
    QCoreApplication::processEvents();
    CHECK(editor.toggleFold(editor.document()->findBlockByNumber(0)));
    editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(2)));
    CHECK(editor.moveLines(-1));
    CHECK(text(editor) == "# B\n# A\nx\ny");

    // Deleting: a middle line, the last line, the only line.
    editor.setMarkdown(QStringLiteral("a\nb\nc"));
    editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(1)));
    editor.deleteLines();
    CHECK(shown(editor) == "a\n|c");
    editor.deleteLines();
    CHECK(text(editor) == "a");
    editor.deleteLines();
    CHECK(text(editor).isEmpty());
    editor.undo();
    CHECK(text(editor) == "a");
}

TEST_CASE(toggling_checkboxes)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("buy milk"));
    editor.moveCursor(QTextCursor::End);
    editor.transformLines(md::toggleCheckbox);
    CHECK(shown(editor) == "- [ ] buy milk|");
    editor.transformLines(md::toggleCheckbox);
    CHECK(shown(editor) == "- [x] buy milk|");
    editor.setMarkdown(QStringLiteral("- a\n- [x] b\nc"));
    editor.selectAll();
    editor.transformLines(md::toggleCheckbox);
    CHECK(text(editor) == "- [ ] a\n- [ ] b\n- [ ] c");
    editor.undo();
    CHECK(text(editor) == "- a\n- [x] b\nc");
}

TEST_CASE(line_shortcuts_reach_the_window)
{
    MarkdownEditor editor;
    for (const QKeyCombination combination : {QKeyCombination(Qt::CTRL, Qt::Key_D), QKeyCombination(Qt::CTRL, Qt::Key_L),
                                              QKeyCombination(Qt::ALT, Qt::Key_Up), QKeyCombination(Qt::ALT, Qt::Key_Down)}) {
        QKeyEvent override(QEvent::ShortcutOverride, combination.key(), combination.keyboardModifiers());
        QApplication::sendEvent(&editor, &override);
        CHECK(!override.isAccepted());
    }
}
