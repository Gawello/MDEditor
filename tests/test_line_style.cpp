#include "TestHarness.h"
#include "markdown/LineStyle.h"

#include <string>
#include <vector>

using namespace md;

using Lines = std::vector<std::u16string>;

TEST_CASE(heading_replaces_list_marker_and_toggles_off)
{
    CHECK(toggleLineStyle({u"- item"}, LineStyle::Heading, 2) == Lines{u"## item"});
    CHECK(toggleLineStyle({u"# title"}, LineStyle::Heading, 3) == Lines{u"### title"});
    CHECK(toggleLineStyle({u"## title"}, LineStyle::Heading, 2) == Lines{u"title"});
}

TEST_CASE(lists_number_lines_and_skip_blank_ones)
{
    CHECK(toggleLineStyle({u"a", u"", u"b"}, LineStyle::Numbered) == (Lines{u"1. a", u"", u"2. b"}));
    CHECK(toggleLineStyle({u"1. a", u"2. b"}, LineStyle::Numbered) == (Lines{u"a", u"b"}));
    CHECK(toggleLineStyle({u"- [x] done"}, LineStyle::Bullet) == Lines{u"- done"});
    CHECK(toggleLineStyle({u"    - nested"}, LineStyle::Task) == Lines{u"    - [ ] nested"});
    CHECK(toggleLineStyle({u"- [ ] a", u"b"}, LineStyle::Task) == (Lines{u"- [ ] a", u"- [ ] b"}));
    CHECK(toggleLineStyle({u""}, LineStyle::Bullet) == Lines{u"- "});
}

TEST_CASE(quote_wraps_whole_line_and_other_styles_go_inside)
{
    CHECK(toggleLineStyle({u"- a", u"b"}, LineStyle::Quote) == (Lines{u"> - a", u"> b"}));
    CHECK(toggleLineStyle({u"> - a", u"> b"}, LineStyle::Quote) == (Lines{u"- a", u"b"}));
    CHECK(toggleLineStyle({u"> b"}, LineStyle::Heading, 1) == Lines{u"> # b"});
    CHECK(toggleLineStyle({u"> # b"}, LineStyle::Body) == Lines{u"> b"});
}

TEST_CASE(code_tables_and_rules_keep_their_lines)
{
    const Lines lines{u"text", u"```", u"code", u"```", u"---", u"| a |", u"| - |"};
    const Lines styled = toggleLineStyle(lines, LineStyle::Bullet);
    CHECK(styled[0] == u"- text");
    for (size_t i = 1; i < lines.size(); ++i)
        CHECK(styled[i] == lines[i]);
}

TEST_CASE(clear_formatting_keeps_links_and_escapes)
{
    CHECK(stripInlineFormatting(u"**bold** and *it* ~~x~~ ==y== `c`") == u"bold and it x y c");
    CHECK(stripInlineFormatting(u"a [**l**](u) \\*b\\*") == u"a [**l**](u) \\*b\\*");
    CHECK(stripInlineFormatting(u"plain") == u"plain");
}
