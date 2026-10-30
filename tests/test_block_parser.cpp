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
    CHECK(open.next.inFence());

    auto inner = md::parseLine(u"# not a heading", open.next);
    CHECK(inner.info.type == BlockType::CodeLine);
    CHECK(inner.next.inFence());

    CHECK(md::parseLine(u"~~~", open.next).info.type == BlockType::CodeLine);
    CHECK(md::parseLine(u"``", open.next).info.type == BlockType::CodeLine);

    auto close = md::parseLine(u"````", open.next);
    CHECK(close.info.type == BlockType::FenceClose);
    CHECK(!close.next.inFence());

    CHECK(md::parseLine(u"``` a`b", {}).info.type == BlockType::Paragraph);
    CHECK(md::parseLine(u"~~~ a`b", {}).info.type == BlockType::FenceOpen);
}

TEST_CASE(block_state_roundtrip)
{
    CHECK(md::BlockState{}.encode() == -1);
    CHECK(!md::BlockState::decode(-1).inFence());

    const md::BlockState tilde{u'~', 5};
    const auto decoded = md::BlockState::decode(tilde.encode());
    CHECK(decoded.fenceChar == u'~');
    CHECK(decoded.fenceLength == 5);

    const auto backtick = md::BlockState::decode(md::BlockState{u'`', 3}.encode());
    CHECK(backtick.fenceChar == u'`');
    CHECK(backtick.fenceLength == 3);

    using Table = md::BlockState::Table;
    for (const Table table : {Table::AfterHeader, Table::Body}) {
        const auto t = md::BlockState::decode(md::BlockState{0, 0, table}.encode());
        CHECK(t.table == table);
        CHECK(!t.inFence());
    }
}

TEST_CASE(tables)
{
    const auto header = md::parseLine(u"| a | b |", {}, u"|---|:-:|");
    CHECK(header.info.type == BlockType::TableHeader);

    const auto delimiter = md::parseLine(u"|---|:-:|", header.next);
    CHECK(delimiter.info.type == BlockType::TableDelimiter);

    const auto row = md::parseLine(u"| 1 | 2 |", delimiter.next);
    CHECK(row.info.type == BlockType::TableRow);
    CHECK(md::parseLine(u"x | y", row.next).info.type == BlockType::TableRow);

    const auto end = md::parseLine(u"", row.next);
    CHECK(end.info.type == BlockType::Blank);
    CHECK(end.next.table == md::BlockState::Table::None);
    CHECK(md::parseLine(u"no pipes", row.next).info.type == BlockType::Paragraph);

    // Column count must match, and the header needs a delimiter row below it.
    CHECK(md::parseLine(u"| a | b |", {}, u"|---|").info.type == BlockType::Paragraph);
    CHECK(md::parseLine(u"| a | b |", {}, u"| x | y |").info.type == BlockType::Paragraph);
    CHECK(md::parseLine(u"| a | b |", {}).info.type == BlockType::Paragraph);
    CHECK(md::parseLine(u"| --- |", {}).info.type == BlockType::Paragraph);

    // Code fences win over tables.
    md::BlockState fence{u'`', 3};
    CHECK(md::parseLine(u"| a | b |", fence, u"|---|---|").info.type == BlockType::CodeLine);
}
