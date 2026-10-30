#include "TestHarness.h"
#include "markdown/BlockParser.h"

using md::BlockType;

static md::BlockInfo parse(std::u16string_view line)
{
    return md::parseLine(line, {}).info;
}

TEST_CASE(headings)
{
    auto h = parse(u"## Title");
    CHECK(h.type == BlockType::Heading);
    CHECK(h.headingLevel == 2);
    CHECK(h.markerLength == 3);
    CHECK(h.contentStart == 3);

    CHECK(parse(u"#").type == BlockType::Heading);
    CHECK(parse(u"#abc").type == BlockType::Paragraph);
    CHECK(parse(u"####### seven").type == BlockType::Paragraph);
    CHECK(parse(u"    # indented code").type == BlockType::Paragraph);
}

TEST_CASE(bullets_and_tasks)
{
    auto b = parse(u"- item");
    CHECK(b.type == BlockType::BulletItem);
    CHECK(b.listMarker == u'-');
    CHECK(b.contentStart == 2);

    auto nested = parse(u"    * nested");
    CHECK(nested.type == BlockType::BulletItem);
    CHECK(nested.indent == 4);
    CHECK(nested.markerStart == 4);
    CHECK(nested.contentStart == 6);

    auto t = parse(u"- [ ] task");
    CHECK(t.type == BlockType::TaskItem);
    CHECK(t.checkboxStart == 2);
    CHECK(!t.checked);
    CHECK(t.contentStart == 6);

    CHECK(parse(u"- [x] done").checked);
    CHECK(parse(u"- [X] done").checked);
    CHECK(parse(u"- [link](x)").type == BlockType::BulletItem);
    CHECK(parse(u"*emphasis*").type == BlockType::Paragraph);
    CHECK(parse(u"-").type == BlockType::BulletItem);
}

TEST_CASE(ordered_items)
{
    auto o = parse(u"12. item");
    CHECK(o.type == BlockType::OrderedItem);
    CHECK(o.orderedNumber == 12);
    CHECK(o.listMarker == u'.');
    CHECK(o.contentStart == 4);

    CHECK(parse(u"1) x").listMarker == u')');
    CHECK(parse(u"2024.").type == BlockType::OrderedItem);
    CHECK(parse(u"3.14 is pi").type == BlockType::Paragraph);
}

TEST_CASE(quotes_and_rules)
{
    auto q = parse(u"> quote");
    CHECK(q.type == BlockType::Quote);
    CHECK(q.contentStart == 2);
    CHECK(parse(u">> nested").contentStart == 3);

    CHECK(parse(u"---").type == BlockType::HorizontalRule);
    CHECK(parse(u"* * *").type == BlockType::HorizontalRule);
    CHECK(parse(u"- - -").type == BlockType::HorizontalRule);
    CHECK(parse(u"___").type == BlockType::HorizontalRule);
    CHECK(parse(u"--").type == BlockType::Paragraph);
    CHECK(parse(u"   ").type == BlockType::Blank);
}

TEST_CASE(fenced_code)
{
    auto open = md::parseLine(u"```cpp", {});
    CHECK(open.info.type == BlockType::FenceOpen);
    CHECK(open.info.contentStart == 3);
    CHECK(open.next.inside());

    auto inner = md::parseLine(u"# not a heading", open.next);
    CHECK(inner.info.type == BlockType::CodeLine);
    CHECK(inner.next.inside());

    CHECK(md::parseLine(u"~~~", open.next).info.type == BlockType::CodeLine);
    CHECK(md::parseLine(u"``", open.next).info.type == BlockType::CodeLine);

    auto close = md::parseLine(u"````", open.next);
    CHECK(close.info.type == BlockType::FenceClose);
    CHECK(!close.next.inside());

    CHECK(md::parseLine(u"``` a`b", {}).info.type == BlockType::Paragraph);
    CHECK(md::parseLine(u"~~~ a`b", {}).info.type == BlockType::FenceOpen);
}

TEST_CASE(fence_state_roundtrip)
{
    CHECK(md::FenceState{}.encode() == -1);
    CHECK(!md::FenceState::decode(-1).inside());

    const md::FenceState tilde{u'~', 5};
    const auto decoded = md::FenceState::decode(tilde.encode());
    CHECK(decoded.fenceChar == u'~');
    CHECK(decoded.fenceLength == 5);

    const auto backtick = md::FenceState::decode(md::FenceState{u'`', 3}.encode());
    CHECK(backtick.fenceChar == u'`');
    CHECK(backtick.fenceLength == 3);
}
