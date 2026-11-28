#include "TestHarness.h"
#include "markdown/LinkRewrite.h"

#include <map>

using namespace md;

namespace {

// Rewrites by a table: (target, wiki) -> new target; records what it was asked about.
struct Table {
    std::map<std::pair<std::u16string, bool>, std::u16string> moves;
    std::vector<LinkToRewrite> asked;
    std::u16string apply(std::u16string_view text)
    {
        return rewriteLinks(text, [this](const LinkToRewrite &link) -> std::optional<std::u16string> {
            asked.push_back(link);
            const auto it = moves.find({link.target, link.wiki});
            if (it == moves.end())
                return std::nullopt;
            return it->second;
        });
    }
};

} // namespace

TEST_CASE(rewrite_wiki_links_keeping_anchor_and_alias)
{
    Table t;
    t.moves[{u"Alpha", true}] = u"Omega";
    CHECK(t.apply(u"See [[Alpha]].") == u"See [[Omega]].");
    CHECK(t.apply(u"[[Alpha#Goals]] [[Alpha|the A]] [[Alpha#^b1|x]]") == u"[[Omega#Goals]] [[Omega|the A]] [[Omega#^b1|x]]");
    CHECK(t.apply(u"![[Alpha]] and ![[Alpha#Part]]") == u"![[Omega]] and ![[Omega#Part]]");
    CHECK(t.apply(u"- [ ] task [[Alpha]]\n> quote [[Alpha]]\n# [[Alpha]]") == u"- [ ] task [[Omega]]\n> quote [[Omega]]\n# [[Omega]]");
    CHECK(t.apply(u"[[Alphabet]] [[alpha]] [[#Alpha]]") == u"[[Alphabet]] [[alpha]] [[#Alpha]]"); // other targets
}

TEST_CASE(rewrite_markdown_links)
{
    Table t;
    t.moves[{u"sub/Beta.md", false}] = u"other/Beta%20two.md";
    t.moves[{u"pic.png", false}] = u"img/pic.png";
    CHECK(t.apply(u"[b](sub/Beta.md) [b](sub/Beta.md#part \"title\")") ==
          u"[b](other/Beta%20two.md) [b](other/Beta%20two.md#part \"title\")");
    CHECK(t.apply(u"![p](pic.png) ![p](pic.png =100x)") == u"![p](img/pic.png) ![p](img/pic.png =100x)");
    // <angle brackets> are reported, so the caller can write spaces as they are.
    t.asked.clear();
    t.apply(u"[x](<a b.md>)");
    CHECK(t.asked.size() == 1 && t.asked[0].angle && t.asked[0].target == u"a b.md" && !t.asked[0].wiki);
    // URLs, autolinks and this note's headings aren't offered.
    t.asked.clear();
    t.apply(u"[w](https://x.org/a.md) <https://y.org> https://z.org [h](#part) mailto:a@b.c");
    CHECK(t.asked.empty());
}

TEST_CASE(rewrite_leaves_code_math_comments_and_the_rest)
{
    Table t;
    t.moves[{u"Alpha", true}] = u"Omega";
    const std::u16string_view text = u"```\n[[Alpha]]\n```\n`[[Alpha]]` $[[Alpha]]$\n$$\n[[Alpha]]\n$$\n%%\n[[Alpha]]\n%%\n"
                                     u"    indented [[Alpha]]\n";
    const std::u16string result = t.apply(text);
    // The indented line continues a paragraph (it isn't after a blank line): its link is a link.
    CHECK(result == u"```\n[[Alpha]]\n```\n`[[Alpha]]` $[[Alpha]]$\n$$\n[[Alpha]]\n$$\n%%\n[[Alpha]]\n%%\n"
                    u"    indented [[Omega]]\n");
    // Front matter properties are links too; CRLF and the rest are kept byte for byte.
    CHECK(t.apply(u"---\r\nup: \"[[Alpha]]\"\r\n---\r\n[[Alpha]]  \r\nend") == u"---\r\nup: \"[[Omega]]\"\r\n---\r\n[[Omega]]  \r\nend");
    CHECK(t.apply(u"").empty());
    CHECK(t.apply(u"no links\n") == u"no links\n");
}

TEST_CASE(rewrite_several_links_on_a_line)
{
    Table t;
    t.moves[{u"A", true}] = u"Longer name";
    t.moves[{u"b.md", false}] = u"c.md";
    CHECK(t.apply(u"[[A]] x [b](b.md) y [[A|z]] [[B]]") == u"[[Longer name]] x [b](c.md) y [[Longer name|z]] [[B]]");
}
