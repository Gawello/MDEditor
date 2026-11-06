#include "TestHarness.h"
#include "editor/LivePreviewHighlighter.h"
#include "editor/MarkdownEditor.h"

#include <QApplication>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <cmath>

namespace {

struct Fixture {
    MarkdownEditor editor;

    explicit Fixture(const QString &text)
    {
        editor.resize(800, 400);
        editor.setMarkdown(text + QStringLiteral("\n\nlast"));
        QTextCursor cursor = editor.textCursor();
        cursor.movePosition(QTextCursor::End); // block 0 is rendered with the cursor elsewhere
        editor.setTextCursor(cursor);
        QApplication::processEvents();
    }
    QTextBlock block(int number = 0) const { return editor.document()->findBlockByNumber(number); }
    const BlockData *data(int number = 0) const { return static_cast<BlockData *>(block(number).userData()); }
    qreal width(int number = 0) const
    {
        editor.document()->documentLayout()->blockBoundingRect(block(number)); // lays it out
        return block(number).layout()->lineAt(0).naturalTextWidth();
    }
};

} // namespace

TEST_CASE(entity_is_shown_as_its_character_and_the_text_is_kept)
{
    Fixture f(QStringLiteral("a &rarr; b"));
    CHECK(f.data() != nullptr);
    CHECK(f.data()->entities.size() == 1);
    CHECK(f.data()->entities[0].text == QStringLiteral("→"));
    CHECK(f.data()->entities[0].position == 2);
    CHECK(f.editor.markdown().startsWith(QStringLiteral("a &rarr; b")));
}

TEST_CASE(entity_takes_the_width_of_its_character)
{
    Fixture entity(QStringLiteral("before &rarr; after"));
    Fixture literal(QStringLiteral("before → after"));
    CHECK(std::abs(entity.width() - literal.width()) < 2.0);
}

TEST_CASE(entity_shows_raw_on_the_line_being_edited)
{
    Fixture f(QStringLiteral("a &rarr; b"));
    QTextCursor cursor(f.block());
    f.editor.setTextCursor(cursor);
    QApplication::processEvents();
    CHECK(f.data()->entities.empty());

    cursor.movePosition(QTextCursor::End);
    f.editor.setTextCursor(cursor);
    QApplication::processEvents();
    CHECK(f.data()->entities.size() == 1);
}

TEST_CASE(entity_keeps_the_style_around_it)
{
    Fixture f(QStringLiteral("**a&rarr;b**"));
    CHECK(f.data()->entities.size() == 1);
    CHECK(f.data()->entities[0].font.bold());

    Fixture heading(QStringLiteral("# a&rarr;b"));
    Fixture body(QStringLiteral("a&rarr;b"));
    CHECK(heading.data()->entities[0].font.pointSizeF() > body.data()->entities[0].font.pointSizeF());
}

TEST_CASE(entity_stays_raw_in_code_blocks)
{
    Fixture f(QStringLiteral("```\n&rarr;\n```"));
    CHECK(f.data(1)->entities.empty());
}
