#include "TestHarness.h"
#include "markdown/Completion.h"
#include "markdown/NoteScan.h"

using namespace md;

namespace {

// The context with the cursor at the last '|' in `text` (which is removed).
CompletionContext at(std::u16string text)
{
    const size_t cursor = text.rfind(u'|');
    text.erase(cursor, 1);
    return completionAt(text, int(cursor));
}

} // namespace

TEST_CASE(completion_finds_note_links)
{
    CompletionContext c = at(u"See [[Alp|");
    CHECK(c.kind == CompletionKind::Note && c.query == u"Alp" && c.start == 6 && !c.closed);
    c = at(u"See [[|]] more");
    CHECK(c.kind == CompletionKind::Note && c.query.empty() && c.closed);
    c = at(u"![[pic|");
    CHECK(c.kind == CompletionKind::Note && c.query == u"pic" && c.start == 3);
    c = at(u"[[folder/No|");
    CHECK(c.kind == CompletionKind::Note && c.query == u"folder/No");
    CHECK(at(u"[[Done]] then |").kind == CompletionKind::None);
    CHECK(at(u"[[Done]] then [[Ne|").query == u"Ne");
    CHECK(at(u"[[Note|Disp|").kind == CompletionKind::None); // the display text
    CHECK(at(u"`[[code|`").kind == CompletionKind::None);
    CHECK(at(u"|[[x").kind == CompletionKind::None);
}

TEST_CASE(completion_finds_headings_and_blocks)
{
    CompletionContext c = at(u"[[Alpha#Go|");
    CHECK(c.kind == CompletionKind::Heading && c.note == u"Alpha" && c.query == u"Go" && c.start == 8);
    c = at(u"[[#|]]");
    CHECK(c.kind == CompletionKind::Heading && c.note.empty() && c.query.empty() && c.closed);
    c = at(u"x [[Alpha#^ab|");
    CHECK(c.kind == CompletionKind::Block && c.note == u"Alpha" && c.query == u"ab" && c.start == 11);
}

TEST_CASE(completion_finds_tags)
{
    CompletionContext c = at(u"#pro|");
    CHECK(c.kind == CompletionKind::Tag && c.query == u"pro" && c.start == 1);
    c = at(u"text #a/b|");
    CHECK(c.kind == CompletionKind::Tag && c.query == u"a/b");
    CHECK(at(u"text #zaż|").query == u"zaż");
    CHECK(at(u"#|").kind == CompletionKind::None);      // a heading being typed
    CHECK(at(u"# Head|").kind == CompletionKind::None);
    CHECK(at(u"x#no|").kind == CompletionKind::None);   // not after a space
    CHECK(at(u"`#code|`").kind == CompletionKind::None);
    CHECK(at(u"#tag |").kind == CompletionKind::None);   // finished
    CHECK(at(u"#ta|g").query == u"ta");                 // only what's before the cursor
}

TEST_CASE(note_scan_finds_block_ids)
{
    const NoteScan scan = scanNote(u"# T\nfirst para ^p1\n- item ^li-2\nnot^id\nsee x ^bad!\n| a |\n|---|\n\n^tbl\n```\ncode ^no\n```\n");
    CHECK(scan.blocks.size() == 3);
    if (scan.blocks.size() == 3) {
        CHECK(scan.blocks[0].id == u"p1" && scan.blocks[0].text == u"first para" && scan.blocks[0].line == 1);
        CHECK(scan.blocks[1].id == u"li-2" && scan.blocks[1].text == u"item" && scan.blocks[1].line == 2);
        CHECK(scan.blocks[2].id == u"tbl" && scan.blocks[2].text == u"|---|" && scan.blocks[2].line == 6);
    }
}
