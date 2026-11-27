#include "TestHarness.h"
#include "editor/LivePreviewHighlighter.h"
#include "editor/MarkdownEditor.h"
#include "editor/MathRenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>

namespace {

const BlockData *dataOf(const MarkdownEditor &editor, int block)
{
    return static_cast<const BlockData *>(editor.document()->findBlockByNumber(block).userData());
}

void cursorToEnd(MarkdownEditor &editor)
{
    QTextCursor cursor(editor.document());
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
}

} // namespace

TEST_CASE(math_renderer_draws_and_reports_errors)
{
    const MathRenderer::Formula &ok = MathRenderer::instance().render(QStringLiteral("\\frac{a}{b}"), 20, Qt::black, true, 1);
    CHECK(!ok.pixmap.isNull());
    CHECK(!ok.error);
    CHECK(ok.size().height() > 20); // a fraction is taller than a line
    CHECK(ok.baseline > 0 && ok.baseline < ok.size().height());

    const MathRenderer::Formula &bad = MathRenderer::instance().render(QStringLiteral("\\frac{1}{"), 20, Qt::black, false, 1);
    CHECK(bad.error);
    CHECK(!bad.pixmap.isNull());
}

TEST_CASE(math_is_rendered_outside_the_cursor_line)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("a $x^2$ b\n\n$$\n\\sum_i i\n$$\n\n$$ y $$\n\nend"));
    cursorToEnd(editor);

    CHECK(dataOf(editor, 0)->math.size() == 1);
    CHECK(dataOf(editor, 0)->math.front().tex == QStringLiteral("x^2"));
    CHECK(dataOf(editor, 2)->mathShown);
    CHECK(dataOf(editor, 2)->mathBlock.contains(QStringLiteral("\\sum_i i")));
    CHECK(!dataOf(editor, 3)->mathShown);
    CHECK(dataOf(editor, 6)->mathShown);
    // The formula's line is as tall as the formula.
    const auto height = [&](int block) {
        return editor.document()->documentLayout()->blockBoundingRect(editor.document()->findBlockByNumber(block)).height();
    };
    // Block 1 is an ordinary (blank) line; the last block would include the document's bottom margin.
    CHECK(height(2) > 1.5 * height(1));
    CHECK(height(3) < 0.5 * height(1)); // collapsed

    // With the cursor in the block, the whole block shows its source.
    QTextCursor cursor(editor.document()->findBlockByNumber(3));
    editor.setTextCursor(cursor);
    CHECK(!dataOf(editor, 2)->mathShown);
    cursor.setPosition(0);
    editor.setTextCursor(cursor);
    CHECK(dataOf(editor, 0)->math.empty()); // the cursor's line shows source
    CHECK(dataOf(editor, 2)->mathShown);
}

TEST_CASE(math_menu_entries)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("x"));
    QTextCursor cursor = editor.textCursor();
    cursor.select(QTextCursor::Document);
    editor.setTextCursor(cursor);
    editor.toggleWrap(QStringLiteral("$"));
    CHECK(editor.markdown() == QStringLiteral("$x$"));

    editor.setMarkdown(QString());
    editor.insertSnippet(MarkdownEditor::Snippet::MathBlock);
    CHECK(editor.markdown() == QStringLiteral("$$\n\n$$"));
    CHECK(editor.textCursor().blockNumber() == 1);
}
