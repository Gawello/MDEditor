#include "TestHarness.h"
#include "app/TablePicker.h"
#include "editor/MarkdownEditor.h"

#include <QApplication>
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace {

void pressKey(QWidget *widget, int key)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

} // namespace

// While the popup is open it holds an X11 grab, so slots must only run once it is gone.
TEST_CASE(table_picker_closes_before_reporting_the_size)
{
    auto *picker = new TablePicker; // deletes itself on close
    int rows = 0;
    int columns = 0;
    bool visibleInSlot = true;
    QObject::connect(picker, &TablePicker::picked, picker, [&](int r, int c) {
        rows = r;
        columns = c;
        visibleInSlot = picker->isVisible();
    });
    picker->popup(QPoint(0, 0));
    pressKey(picker, Qt::Key_Right);
    pressKey(picker, Qt::Key_Right);
    pressKey(picker, Qt::Key_Down);
    pressKey(picker, Qt::Key_Return);

    CHECK(rows == 2);
    CHECK(columns == 2);
    CHECK(!visibleInSlot);
}

// Font merging at 0.1pt searches every system font for the delimiter row's characters.
TEST_CASE(collapsed_table_delimiter_disables_font_merging)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("| a |\n| --- |\n| b |\n\nlast"));
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End); // the table is rendered only with the cursor outside it
    editor.setTextCursor(cursor);

    const QTextBlock delimiter = editor.document()->findBlockByNumber(1);
    const auto formats = delimiter.layout()->formats();
    CHECK(!formats.isEmpty());
    for (const QTextLayout::FormatRange &range : formats)
        CHECK(range.format.fontStyleStrategy() & QFont::NoFontMerging);
}
