#include "TestHarness.h"
#include "markdown/InlineParser.h"

#include <string>

using namespace md;

// True when every character in [from, to) has all bits of `style` set.
static bool all(const InlineResult &r, int from, int to, std::uint16_t style)
{
    for (int i = from; i < to; ++i) {
        if ((r.styles[i] & style) != style)
            return false;
    }
    return true;
}

// True when no character in [from, to) has any bit of `style` set.
static bool none(const InlineResult &r, int from, int to, std::uint16_t style)
{
    for (int i = from; i < to; ++i) {
        if (r.styles[i] & style)
            return false;
    }
    return true;
}

static std::u16string url(std::u16string_view text, const LinkRange &link)
{
    return std::u16string(text.substr(link.urlStart, link.urlEnd - link.urlStart));
}

TEST_CASE(bold)
{
    const auto r = parseInline(u"a **b** c");
    CHECK(none(r, 0, 2, 0xFFFF));
    CHECK(all(r, 2, 4, Marker));
    CHECK(all(r, 4, 5, Bold));
    CHECK(none(r, 4, 5, Marker));
    CHECK(all(r, 5, 7, Marker));
}

TEST_CASE(italic)
{
    const auto r = parseInline(u"*x* _y_");
    CHECK(all(r, 0, 1, Marker));
    CHECK(all(r, 1, 2, Italic));
    CHECK(all(r, 2, 3, Marker));
    CHECK(all(r, 4, 5, Marker));
    CHECK(all(r, 5, 6, Italic));
    CHECK(all(r, 6, 7, Marker));
}

TEST_CASE(bold_italic)
{
    const auto r = parseInline(u"***bi***");
    CHECK(all(r, 0, 3, Marker));
    CHECK(all(r, 3, 5, Bold | Italic));
    CHECK(all(r, 5, 8, Marker));
}

TEST_CASE(bold_containing_asterisk)
{
    // CommonMark: **a*b** -> <strong>a*b</strong>
    const auto r = parseInline(u"**a*b**");
    CHECK(all(r, 0, 2, Marker));
    CHECK(all(r, 2, 5, Bold));
    CHECK(none(r, 3, 4, Marker | Italic));
    CHECK(all(r, 5, 7, Marker));
}

TEST_CASE(unmatched_delimiters_stay_literal)
{
    CHECK(none(parseInline(u"**open"), 0, 6, 0xFFFF));
    CHECK(none(parseInline(u"2 * 3 * 4"), 0, 9, 0xFFFF));
    CHECK(none(parseInline(u"snake_case_name"), 0, 15, 0xFFFF));
}

TEST_CASE(code_span_wins)
{
    const auto r = parseInline(u"`**no**`");
    CHECK(all(r, 0, 8, Code));
    CHECK(all(r, 0, 1, Marker));
    CHECK(none(r, 1, 7, Marker | Bold));
    CHECK(all(r, 7, 8, Marker));

    const auto two = parseInline(u"``a ` b``");
    CHECK(all(two, 0, 2, Marker));
    CHECK(none(two, 2, 7, Marker));
    CHECK(all(two, 2, 7, Code));
}

TEST_CASE(escapes)
{
    const auto r = parseInline(u"\\*not\\*");
    CHECK(all(r, 0, 1, Marker));
    CHECK(none(r, 0, 7, Italic));
}

TEST_CASE(strike_and_highlight)
{
    const auto s = parseInline(u"~~s~~ ==h==");
    CHECK(all(s, 0, 2, Marker));
    CHECK(all(s, 2, 3, Strike));
    CHECK(all(s, 3, 5, Marker));
    CHECK(all(s, 6, 8, Marker));
    CHECK(all(s, 8, 9, Highlight));
    CHECK(all(s, 9, 11, Marker));

    CHECK(none(parseInline(u"~s~"), 0, 3, 0xFFFF));
}

TEST_CASE(inline_links)
{
    const std::u16string_view text = u"see [t*x*t](http://x.y) end";
    const auto r = parseInline(text);
    CHECK(r.links.size() == 1);
    CHECK(url(text, r.links[0]) == u"http://x.y");
    CHECK(all(r, 4, 5, Marker));
    CHECK(all(r, 5, 10, Link));
    CHECK(all(r, 7, 8, Italic));
    CHECK(all(r, 10, 12, Marker));
    CHECK(all(r, 12, 22, Marker | LinkUrl));
    CHECK(all(r, 22, 23, Marker));
    CHECK(linkAt(r, 6) != nullptr);
    CHECK(linkAt(r, 2) == nullptr);

    const std::u16string_view image = u"![alt](img.png \"title\")";
    const auto ri = parseInline(image);
    CHECK(ri.links.size() == 1);
    CHECK(url(image, ri.links[0]) == u"img.png");
    CHECK(all(ri, 0, 2, Marker));
}

TEST_CASE(wiki_links)
{
    const std::u16string_view plain = u"[[Note]]";
    const auto r = parseInline(plain);
    CHECK(r.links.size() == 1);
    CHECK(url(plain, r.links[0]) == u"Note");
    CHECK(all(r, 2, 6, Link));
    CHECK(none(r, 2, 6, Marker));

    const std::u16string_view alias = u"[[dir/Note|Alias]]";
    const auto a = parseInline(alias);
    CHECK(a.links.size() == 1);
    CHECK(url(alias, a.links[0]) == u"dir/Note");
    CHECK(all(a, 2, 11, Marker | LinkUrl));
    CHECK(all(a, 11, 16, Link));
}

TEST_CASE(autolinks)
{
    const std::u16string_view text = u"go https://ex.com/a_b_c. now";
    const auto r = parseInline(text);
    CHECK(r.links.size() == 1);
    CHECK(url(text, r.links[0]) == u"https://ex.com/a_b_c");
    CHECK(none(r, 0, int(text.size()), Italic));
    CHECK(none(r, 23, 24, Link));
}
