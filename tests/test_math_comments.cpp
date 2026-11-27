#include "TestHarness.h"
#include "markdown/BlockParser.h"
#include "markdown/InlineParser.h"

#include <string>
#include <vector>

using namespace md;

TEST_CASE(inline_math_follows_pandoc_rules)
{
    const InlineResult r = parseInline(u"a $x^2$ b $$\\frac{1}{2}$$ c");
    CHECK(r.math.size() == 2);
    CHECK(!r.math[0].display);
    CHECK(r.math[0].start == 2 && r.math[0].end == 7 && r.math[0].contentStart == 3 && r.math[0].contentEnd == 6);
    CHECK(r.math[1].display);
    CHECK((r.styles[2] & Marker) && (r.styles[2] & Math));
    CHECK(!(r.styles[3] & Marker) && (r.styles[3] & Math));

    CHECK(parseInline(u"costs $5 and $6").math.empty());     // closing $ followed by a digit
    CHECK(parseInline(u"from $ 5 to 6 $").math.empty());     // spaces inside
    CHECK(parseInline(u"price \\$x$ here").math.empty());    // escaped dollar
    CHECK(parseInline(u"`$x$` in code").math.empty());
    CHECK(parseInline(u"$$ unclosed").math.empty());
}

TEST_CASE(math_content_is_not_markdown)
{
    const InlineResult r = parseInline(u"$a*b*c$ and *it*");
    CHECK(r.math.size() == 1);
    CHECK(!(r.styles[2] & Italic));
    CHECK(r.styles[13] & Italic);
}

TEST_CASE(inline_comments)
{
    const InlineResult r = parseInline(u"seen %%hidden **x**%% seen");
    CHECK((r.styles[5] & Comment) && (r.styles[5] & Marker));
    CHECK((r.styles[7] & Comment) && !(r.styles[7] & Marker));
    CHECK(!(r.styles[16] & Bold)); // no Markdown inside
    CHECK(!(r.styles[22] & Comment));
    CHECK(!(parseInline(u"50%% off").styles[2] & Comment)); // no closing pair
}

TEST_CASE(math_and_comment_blocks)
{
    const std::vector<std::u16string> lines{u"$$", u"x = 1 \\\\", u"y$$", u"after", u"%%", u"note", u"%%", u"text",
                                            u"$$ E = mc^2 $$", u"$$x"};
    std::vector<BlockType> types;
    BlockState state;
    for (const std::u16string &line : lines) {
        const LineResult r = parseLine(line, state);
        types.push_back(r.info.type);
        state = r.next;
        CHECK(BlockState::decode(state.encode()).block == state.block); // survives the int
    }
    CHECK(types[0] == BlockType::MathOpen);
    CHECK(types[1] == BlockType::MathLine);
    CHECK(types[2] == BlockType::MathClose);
    CHECK(types[3] == BlockType::Paragraph);
    CHECK(types[4] == BlockType::CommentLine);
    CHECK(types[5] == BlockType::CommentLine);
    CHECK(types[6] == BlockType::CommentLine);
    CHECK(types[7] == BlockType::Paragraph);
    CHECK(types[8] == BlockType::MathSingle);
    CHECK(types[9] == BlockType::MathOpen); // "$$x" with nothing closing it yet
    CHECK(parseLine(u"%% one line %%", {}).info.type == BlockType::Paragraph);
}
