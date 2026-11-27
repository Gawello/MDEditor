#include "TestHarness.h"
#include "app/EditorTab.h"
#include "editor/MarkdownEditor.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>

namespace {

// The foreground color at `column` of block `block`, invalid if the text has the default color.
QColor colorAt(const MarkdownEditor &editor, int block, int column)
{
    const QTextBlock b = editor.document()->findBlockByNumber(block);
    for (const QTextLayout::FormatRange &range : b.layout()->formats()) {
        if (column >= range.start && column < range.start + range.length && range.format.hasProperty(QTextFormat::ForegroundBrush))
            return range.format.foreground().color();
    }
    return {};
}

} // namespace

TEST_CASE(code_files_get_syntax_colors)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("tool.py"));
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("def f():\n    return \"x\"  # done\n");
    file.close();

    EditorTab tab;
    QString error;
    CHECK(tab.load(path, error));
    const MarkdownEditor &editor = *tab.editor();
    CHECK(colorAt(editor, 0, 0).isValid());            // def
    CHECK(colorAt(editor, 1, 11).isValid());           // "x"
    CHECK(colorAt(editor, 1, 11) != colorAt(editor, 0, 0));
    CHECK(!tab.isModified());

    // Live preview on: the whole file is Markdown now, without code colors.
    tab.editor()->setMarkdownMode(true);
    CHECK(!colorAt(editor, 0, 0).isValid() || colorAt(editor, 0, 0) != colorAt(editor, 1, 11));
    CHECK(!tab.isModified());
}

TEST_CASE(fenced_code_is_colored_by_its_language)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("```python\nreturn 1\n```\n\n```\nreturn 1\n```\n\ntext"));
    QTextCursor cursor(editor.document());
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    CHECK(colorAt(editor, 1, 0).isValid());  // keyword in a python block
    CHECK(!colorAt(editor, 5, 0).isValid()); // no language: plain code

    // A block comment left open carries over to the next line of the block, and closing the fence ends it.
    editor.setMarkdown(QStringLiteral("```c\n/* open\nstill\n```\nafter"));
    const QColor comment = colorAt(editor, 1, 0);
    CHECK(comment.isValid());
    CHECK(colorAt(editor, 2, 0) == comment);
    CHECK(colorAt(editor, 4, 0) != comment);

    // Changing the language re-colors the block below.
    editor.setMarkdown(QStringLiteral("```text\nreturn 1\n```"));
    CHECK(!colorAt(editor, 1, 0).isValid());
    QTextCursor edit(editor.document()->firstBlock());
    edit.movePosition(QTextCursor::EndOfBlock);
    edit.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    edit.insertText(QStringLiteral("```js"));
    CHECK(colorAt(editor, 1, 0).isValid());
}
