#include "TestHarness.h"
#include "markdown/BlockParser.h"
#include "markdown/InlineParser.h"
#include "markdown/NoteScan.h"

#include <string>
#include <vector>

using namespace md;

namespace {

std::u16string at(std::u16string_view text, int start, int end)
{
    return std::u16string(text.substr(start, end - start));
}

} // namespace

TEST_CASE(tags_follow_obsidian_rules)
{
    const std::u16string_view text = u"#start, mid #a/b-c_d end #zażółć #1 #12a x#no `#code` [#l](u) #trail/";
    const InlineResult r = parseInline(text);
    std::vector<std::u16string> tags;
    for (const TagRange &tag : r.tags)
        tags.push_back(at(text, tag.start, tag.end));
    CHECK(tags.size() == 5);
    CHECK(tags.size() == 5 && tags[0] == u"#start" && tags[1] == u"#a/b-c_d" && tags[2] == u"#zażółć"
          && tags[3] == u"#12a" && tags[4] == u"#trail");
    CHECK(r.styles[0] & Tag);
    CHECK(!(r.styles[text.find(u"_d")] & Italic)); // no emphasis inside a tag
    CHECK(parseInline(u"# heading-like").tags.empty());
    CHECK(parseInline(u"https://x.com/#anchor").tags.empty());
}

TEST_CASE(footnotes_are_parsed)
{
    const std::u16string_view text = u"Text[^1] and [^note].";
    const InlineResult r = parseInline(text);
    CHECK(r.footnotes.size() == 2);
    CHECK(at(text, r.footnotes[0].labelStart, r.footnotes[0].labelEnd) == u"1");
    CHECK(!r.footnotes[0].definition);
    CHECK((r.styles[4] & Marker) && (r.styles[6] & Footnote));
    CHECK(r.links.empty());

    const InlineResult def = parseInline(u"[^1]: The note.");
    CHECK(def.footnotes.size() == 1 && def.footnotes[0].definition);
    CHECK(parseInline(u"[^ x]").footnotes.empty());
}

TEST_CASE(frontmatter_only_on_the_first_line)
{
    BlockState state;
    LineResult r = parseLine(u"---", state, std::nullopt, true);
    CHECK(r.info.type == BlockType::FrontmatterOpen);
    CHECK(BlockState::decode(r.next.encode()).block == BlockState::Block::Frontmatter);
    r = parseLine(u"title: X", r.next);
    CHECK(r.info.type == BlockType::FrontmatterLine);
    r = parseLine(u"---", r.next);
    CHECK(r.info.type == BlockType::FrontmatterClose);
    CHECK(r.next.block == BlockState::Block::None);
    CHECK(parseLine(u"---", {}, std::nullopt, false).info.type == BlockType::HorizontalRule);
}

TEST_CASE(callouts_are_recognized)
{
    LineResult r = parseLine(u"> [!warning]- Careful", {});
    CHECK(r.info.type == BlockType::Quote);
    CHECK(r.info.calloutStart == 2 && r.info.calloutEnd == 13 && r.info.calloutFold == u'-');
    r = parseLine(u"> [!NOTE]", {});
    CHECK(r.info.calloutStart == 2 && r.info.calloutEnd == 9 && r.info.calloutFold == 0);
    CHECK(parseLine(u"> [!] no", {}).info.calloutStart < 0);
    CHECK(parseLine(u"> plain", {}).info.calloutStart < 0);
}

TEST_CASE(notes_are_scanned)
{
    const NoteScan scan = scanNote(u"---\r\n"
                                   u"title: \"My note\"\r\n"
                                   u"tags: [alpha, \"#beta\"]\r\n"
                                   u"aliases:\r\n"
                                   u"  - one\r\n"
                                   u"  - two\r\n"
                                   u"---\r\n"
                                   u"# Intro *here*\r\n"
                                   u"See [[Other note#Part|alias]] and [x](sub/file.md) and https://x.com #gamma\r\n"
                                   u"```\r\n"
                                   u"# not a heading #nottag [[nolink]]\r\n"
                                   u"```\r\n"
                                   u"## Second\r\n"
                                   u"![[pic.png]] #alpha [[#Local]]\r\n");
    CHECK(scan.frontmatterEnd == 6);
    CHECK(scan.properties.size() == 3);
    CHECK(scan.properties.size() == 3 && scan.properties[0].key == u"title" && scan.properties[0].value == u"My note");
    CHECK(scan.properties.size() == 3 && scan.properties[2].key == u"aliases" && scan.properties[2].value == u"one, two");
    CHECK(scan.tags == std::vector<std::u16string>({u"alpha", u"beta", u"gamma"}));
    CHECK(scan.headings.size() == 2);
    CHECK(scan.headings.size() == 2 && scan.headings[0].text == u"Intro here" && scan.headings[0].line == 7
          && scan.headings[1].level == 2);
    CHECK(scan.links.size() == 3);
    CHECK(scan.links.size() == 3 && scan.links[0].target == u"Other note" && scan.links[0].anchor == u"Part"
          && scan.links[0].wiki);
    CHECK(scan.links.size() == 3 && scan.links[1].target == u"sub/file.md" && !scan.links[1].wiki);
    CHECK(scan.links.size() == 3 && scan.links[2].target == u"pic.png" && scan.links[2].embed);

    const NoteScan listTags = scanNote(u"---\ntags:\n  - x\n  - y\n---\n");
    CHECK(listTags.tags == std::vector<std::u16string>({u"x", u"y"}));
}

TEST_CASE(fold_ranges)
{
    const std::u16string text = u"# A\n"       // 0
                                u"text\n"      // 1
                                u"## B\n"      // 2
                                u"- item\n"    // 3
                                u"  - child\n" // 4
                                u"    more\n"  // 5
                                u"- next\n"    // 6
                                u"\n"          // 7
                                u"> [!tip]- T\n" // 8
                                u"> body\n"    // 9
                                u"> more\n"    // 10
                                u"\n"          // 11
                                u"# C\n"       // 12
                                u"end";        // 13
    const std::vector<BlockInfo> blocks = parseBlocks(splitLines(text));
    CHECK(foldEnd(blocks, 0) == 10); // up to # C, trailing blank kept
    CHECK(foldEnd(blocks, 2) == 10);
    CHECK(foldEnd(blocks, 3) == 5);
    CHECK(foldEnd(blocks, 4) == 5);
    CHECK(foldEnd(blocks, 6) == 6);
    CHECK(foldEnd(blocks, 8) == 10);
    CHECK(foldEnd(blocks, 1) == 1);
    CHECK(foldEnd(blocks, 12) == 13);
}
