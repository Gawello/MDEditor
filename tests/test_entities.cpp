#include "TestHarness.h"
#include "markdown/Entities.h"
#include "markdown/InlineParser.h"

using namespace md;

namespace {

std::u16string decoded(std::u16string_view text)
{
    const auto match = matchEntity(text, 0);
    return match ? match->text : std::u16string(u"<none>");
}

} // namespace

TEST_CASE(entity_named)
{
    CHECK(decoded(u"&rarr;") == u"→");
    CHECK(decoded(u"&amp;") == u"&");
    CHECK(decoded(u"&quot;") == u"\"");
    CHECK(decoded(u"&nbsp;") == u" ");
    CHECK(decoded(u"&Afr;") == u"\U0001D504");            // outside the BMP
    CHECK(decoded(u"&NotEqualTilde;") == u"≂̸"); // two code points
    CHECK(matchEntity(u"&rarr; rest", 0)->length == 6);
}

TEST_CASE(entity_names_are_exact)
{
    CHECK(!matchEntity(u"&RARR;", 0));  // case matters
    CHECK(decoded(u"&Rarr;") == u"↠");
    CHECK(!matchEntity(u"&rarr", 0));   // the semicolon is required
    CHECK(!matchEntity(u"&rarr ;", 0));
    CHECK(!matchEntity(u"&nosuchname;", 0));
    CHECK(!matchEntity(u"&;", 0));
    CHECK(!matchEntity(u"&", 0));
    CHECK(!matchEntity(u"&abcdefghijklmnopqrstuvwxyzabcdefghijklmn;", 0));
}

TEST_CASE(entity_numeric)
{
    CHECK(decoded(u"&#8594;") == u"→");
    CHECK(decoded(u"&#x2192;") == u"→");
    CHECK(decoded(u"&#X2192;") == u"→");
    CHECK(decoded(u"&#xabCD;") == u"ꯍ");
    CHECK(decoded(u"&#x1F680;") == u"\U0001F680");
    CHECK(decoded(u"&#9999999;") == u"�"); // seven digits are allowed, the code point isn't
}

TEST_CASE(entity_numeric_invalid_code_points_give_the_replacement_character)
{
    CHECK(decoded(u"&#0;") == u"�");
    CHECK(decoded(u"&#xD800;") == u"�");
    CHECK(decoded(u"&#x110000;") == u"�");
}

TEST_CASE(entity_numeric_malformed)
{
    CHECK(!matchEntity(u"&#;", 0));
    CHECK(!matchEntity(u"&#x;", 0));
    CHECK(!matchEntity(u"&#12", 0));
    CHECK(!matchEntity(u"&#abc;", 0));
    CHECK(!matchEntity(u"&#12345678;", 0)); // eight decimal digits
    CHECK(!matchEntity(u"&#x1234567;", 0)); // seven hex digits
}

TEST_CASE(inline_finds_entities_in_text)
{
    const InlineResult r = parseInline(u"a &rarr; b &#8594;");
    CHECK(r.entities.size() == 2);
    CHECK(r.entities[0].start == 2);
    CHECK(r.entities[0].end == 8);
    CHECK(r.entities[0].text == u"→");
    CHECK(r.entities[1].start == 11);
    CHECK((r.styles[2] & Entity) != 0);
    CHECK((r.styles[8] & Entity) == 0);
}

TEST_CASE(inline_keeps_entities_literal_where_markdown_does)
{
    CHECK(parseInline(u"`&rarr;`").entities.empty());         // code span
    CHECK(parseInline(u"\\&rarr;").entities.empty());         // escaped
    CHECK(parseInline(u"[a](http://x?a=1&rarr;)").entities.empty()); // link target
    CHECK(parseInline(u"http://x.io/?a&rarr;b").entities.empty());   // bare URL
    CHECK(parseInline(u"AT&T; &foo;").entities.empty());
}

TEST_CASE(inline_finds_entities_in_emphasis_and_link_text)
{
    InlineResult r = parseInline(u"**a&rarr;b**");
    CHECK(r.entities.size() == 1);
    CHECK((r.styles[3] & Bold) != 0);

    r = parseInline(u"[a&rarr;b](http://x.io)");
    CHECK(r.entities.size() == 1);
    CHECK(r.links.size() == 1);
}

TEST_CASE(inline_entity_does_not_act_as_markdown)
{
    // &ast; is "*", but as text: it must not open emphasis.
    const InlineResult r = parseInline(u"&ast;a&ast;");
    CHECK(r.entities.size() == 2);
    CHECK((r.styles[5] & Italic) == 0);
}

TEST_CASE(inline_escaped_ampersand_entity_shows_the_rest_literally)
{
    // "&amp;rarr;" reads as the text "&rarr;", not an arrow.
    const InlineResult r = parseInline(u"&amp;rarr;");
    CHECK(r.entities.size() == 1);
    CHECK(r.entities[0].text == u"&");
    CHECK(r.entities[0].end == 5);
}
