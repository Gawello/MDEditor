#include "TestHarness.h"
#include "editor/MarkdownEditor.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>

namespace {

const QString tableAndEnd = QStringLiteral("intro\n\n| a | long header |\n|---|---|\n| wide cell | 1 |\n\nend");
const QString alignedRow = QStringLiteral("| wide cell | 1           |");

struct Fixture {
    MarkdownEditor editor;
    QStringList actions; // texts of the context menu

    explicit Fixture(const QString &text)
    {
        editor.setAttribute(Qt::WA_DontShowOnScreen);
        editor.resize(800, 500);
        editor.setMarkdown(text);
        moveToEnd();
        editor.show();
        QApplication::processEvents();
    }
    void moveToEnd()
    {
        QTextCursor cursor = editor.textCursor();
        cursor.movePosition(QTextCursor::End);
        editor.setTextCursor(cursor);
    }
    QPoint pointInBlock(int number) const
    {
        QTextCursor cursor(editor.document()->findBlockByNumber(number));
        cursor.movePosition(QTextCursor::Right);
        return editor.cursorRect(cursor).center();
    }
    // Opens the real menu, then triggers the action whose text starts with `prefix` (if any).
    void openMenu(const QPoint &pos, QContextMenuEvent::Reason reason, const QString &prefix)
    {
        actions.clear();
        QTimer::singleShot(0, &editor, [this, prefix] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            QAction *chosen = nullptr;
            for (QAction *action : menu->actions()) {
                actions << action->text();
                if (!prefix.isEmpty() && action->text().remove(u'&').startsWith(prefix))
                    chosen = action;
            }
            menu->close();
            if (chosen)
                chosen->trigger();
        });
        QContextMenuEvent event(reason, pos, editor.viewport()->mapToGlobal(pos));
        QApplication::sendEvent(editor.viewport(), &event);
    }
};

} // namespace

TEST_CASE(context_menu_offers_prettify_with_shortcuts)
{
    Fixture f(QStringLiteral("text"));
    f.openMenu(f.pointInBlock(0), QContextMenuEvent::Mouse, QString());
    const QStringList prettify = f.actions.filter(QStringLiteral("Prettify"));
    CHECK(prettify.size() == 2);
    CHECK(prettify.value(0).contains(MarkdownEditor::prettifyShortcut().toString(QKeySequence::NativeText)));
    CHECK(prettify.value(1).contains(MarkdownEditor::prettifyDocumentShortcut().toString(QKeySequence::NativeText)));
    CHECK(f.actions.size() > 2); // the standard Undo/Copy/Paste entries are still there
}

TEST_CASE(right_click_prettifies_the_clicked_table_not_the_cursor_position)
{
    Fixture f(tableAndEnd);
    CHECK(!f.editor.markdown().contains(alignedRow));
    f.openMenu(f.pointInBlock(4), QContextMenuEvent::Mouse, QStringLiteral("Prettify\t"));
    CHECK(f.editor.markdown().contains(alignedRow));
}

TEST_CASE(prettify_document_from_context_menu_needs_no_target_under_the_click)
{
    Fixture f(tableAndEnd);
    f.openMenu(f.pointInBlock(0), QContextMenuEvent::Mouse, QStringLiteral("Prettify Document"));
    CHECK(f.editor.markdown().contains(alignedRow));
}

TEST_CASE(right_click_on_the_selection_keeps_it)
{
    Fixture f(QStringLiteral("first line\nsecond line"));
    QTextCursor cursor(f.editor.document()->firstBlock());
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    f.editor.setTextCursor(cursor);
    f.openMenu(f.pointInBlock(0), QContextMenuEvent::Mouse, QString());
    CHECK(f.editor.textCursor().selectedText() == QStringLiteral("first line"));
}

TEST_CASE(menu_key_leaves_the_cursor_alone)
{
    Fixture f(tableAndEnd);
    const int position = f.editor.textCursor().position();
    f.openMenu(f.pointInBlock(0), QContextMenuEvent::Keyboard, QString());
    CHECK(f.editor.textCursor().position() == position);
}

namespace {

// Puts the cursor at the "^" in `text`, prettifies, and returns the text with "^" at the cursor.
QString afterPrettify(const QString &text, bool wholeDocument = false)
{
    Fixture f(QString(text).remove(u'^'));
    QTextCursor cursor = f.editor.textCursor();
    cursor.setPosition(int(text.indexOf(u'^')));
    f.editor.setTextCursor(cursor);
    if (wholeDocument)
        f.editor.formatDocument(true);
    else
        f.editor.formatAtCursor();
    return f.editor.markdown().insert(f.editor.textCursor().position(), u'^');
}

} // namespace

TEST_CASE(prettify_keeps_the_cursor_in_its_table_cell)
{
    CHECK(afterPrettify("| a | long header |\n|---|---|\n| wi^de | 1 |\n| wide cell | 2 |\n\nend")
          == "| a         | long header |\n| --------- | ----------- |\n| wi^de      | 1           |\n"
             "| wide cell | 2           |\n\nend");
}

TEST_CASE(prettify_keeps_the_cursor_in_right_aligned_and_empty_cells)
{
    CHECK(afterPrettify("| a |\n|--:|\n| x^y |\n| long |")
          == "|    a |\n| ---: |\n|   x^y |\n| long |");
    CHECK(afterPrettify("| a | b |\n|---|---|\n| wide |  ^ |\n| 1 | long |")
          == "| a    | b    |\n| ---- | ---- |\n| wide | ^     |\n| 1    | long |");
}

TEST_CASE(prettify_keeps_the_cursor_on_the_delimiter_row)
{
    CHECK(afterPrettify("| wide | b |\n|-^--|---|\n| 1 | 2 |")
          == "| wide | b   |\n| -^--- | --- |\n| 1    | 2   |");
}

TEST_CASE(prettify_keeps_the_cursor_on_its_token_in_code)
{
    CHECK(afterPrettify("```json\n{\"first\":1,\"se^cond\":2}\n```")
          == "```json\n{\n    \"first\": 1,\n    \"se^cond\": 2\n}\n```");
    CHECK(afterPrettify("```json\n{\"a\":^1}\n```") == "```json\n{\n    \"a\": ^1\n}\n```");
    CHECK(afterPrettify("```cpp\nint f() {\nreturn^ 0;\n}\n```") == "```cpp\nint f() {\n    return^ 0;\n}\n```");
    CHECK(afterPrettify("```cpp\nint f() {\n^return 0;\n}\n```") == "```cpp\nint f() {\n    ^return 0;\n}\n```");
}

TEST_CASE(prettify_leaves_the_cursor_when_nothing_changes)
{
    const QString aligned = QStringLiteral("| a   |\n| --- |\n| ^b   |");
    CHECK(afterPrettify(aligned) == aligned);
}

TEST_CASE(prettify_selection_stays_selected)
{
    Fixture f(QStringLiteral("```json\n{\"a\":1}\n```"));
    QTextCursor cursor(f.editor.document()->findBlockByNumber(1));
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    f.editor.setTextCursor(cursor);
    f.editor.formatAtCursor();
    CHECK(f.editor.textCursor().selectedText().replace(QChar::ParagraphSeparator, u'\n')
          == QStringLiteral("{\n    \"a\": 1\n}"));
}

TEST_CASE(prettify_document_keeps_the_cursor_inside_a_table)
{
    // Expands above the cursor, aligns around it and changes a block below it.
    const QString result = afterPrettify("```json\n{\"a\":1}\n```\n\n| a | b |\n|---|---|\n| x^y | long text |\n\n"
                                         "| c |\n|---|\n| 1 |\n",
                                         true);
    CHECK(result.contains(QStringLiteral("| x^y  | long text |")));
    CHECK(result.contains(QStringLiteral("    \"a\": 1")));
    CHECK(result.contains(QStringLiteral("| --- |")));
}

TEST_CASE(prettify_document_keeps_the_cursor_inside_code_and_in_text)
{
    CHECK(afterPrettify("| a | b |\n|---|---|\n\n```json\n{\"k^ey\":1}\n```", true).contains(QStringLiteral("\"k^ey\": 1")));
    CHECK(afterPrettify("```json\n{\"a\":1}\n```\n\nsome te^xt\n\n| a |\n|-|\n", true).contains(QStringLiteral("some te^xt")));
}

TEST_CASE(prettify_document_is_one_undo_step_and_undo_restores_the_text)
{
    const QString text = QStringLiteral("```json\n{\"a\":1}\n```\n\n| a | long |\n|---|---|\n| wide cell | 2 |");
    Fixture f(text);
    QTextCursor cursor(f.editor.document()->findBlockByNumber(6));
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 3);
    f.editor.setTextCursor(cursor);
    f.editor.formatDocument(true);
    CHECK(f.editor.markdown() != text);
    f.editor.undo();
    CHECK(f.editor.markdown() == text);
}
