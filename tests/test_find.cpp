#include "TestHarness.h"
#include "app/FindBar.h"
#include "editor/MarkdownEditor.h"

namespace {

struct Fixture {
    MarkdownEditor editor;
    FindBar bar{&editor};

    explicit Fixture(const QString &text)
    {
        editor.setMarkdown(text);
        bar.show(); // highlights and counting work while visible
    }
    QString selected() const { return editor.textCursor().selectedText(); }
    int position() const { return editor.textCursor().selectionStart(); }
};

} // namespace

TEST_CASE(find_next_previous_and_wrap)
{
    Fixture f("cat dog Cat\ncat");
    f.bar.setOptions(false, false, false);
    f.bar.setSearchText("cat"); // searches as you type: selects the first match
    CHECK(f.bar.matchCount() == 3);
    CHECK(f.position() == 0);

    CHECK(f.bar.findNext());
    CHECK(f.position() == 8);
    CHECK(f.selected() == "Cat");
    CHECK(f.bar.findNext());
    CHECK(f.position() == 12);
    CHECK(f.bar.findNext()); // wraps around
    CHECK(f.position() == 0);
    CHECK(f.bar.findPrevious()); // and backwards
    CHECK(f.position() == 12);
}

TEST_CASE(find_options)
{
    Fixture f("cat Cat concat cat_ żółw Żółw");
    f.bar.setSearchText("cat");
    f.bar.setOptions(true, false, false);
    CHECK(f.bar.matchCount() == 3); // cat, concat, cat_
    f.bar.setOptions(true, true, false);
    CHECK(f.bar.matchCount() == 1);
    f.bar.setOptions(false, true, false);
    CHECK(f.bar.matchCount() == 2);

    f.bar.setSearchText("żółw"); // whole word and case folding with Polish letters
    CHECK(f.bar.matchCount() == 2);

    f.bar.setOptions(false, false, true);
    f.bar.setSearchText("c[at");
    CHECK(f.bar.matchCount() == 0); // invalid regex: no matches, no crash
    CHECK(!f.bar.findNext());
}

TEST_CASE(replace_one_and_all)
{
    Fixture f("one two one two one");
    f.bar.setOptions(true, false, false);
    f.bar.setSearchText("one");
    f.bar.setReplaceText("1");
    CHECK(f.bar.replaceCurrent()); // the first match is selected, so this replaces it
    CHECK(f.editor.markdown() == "1 two one two one");
    CHECK(f.selected() == "one"); // and moves on to the next one

    CHECK(f.bar.replaceAll() == 2);
    CHECK(f.editor.markdown() == "1 two 1 two 1");
    f.editor.undo(); // Replace All is a single undo step
    CHECK(f.editor.markdown() == "1 two one two one");
}

TEST_CASE(regex_replace_groups)
{
    Fixture f("2024-01-31 and 1999-12-01\nend");
    f.bar.setOptions(false, false, true);
    f.bar.setSearchText("(\\d{4})-(\\d{2})-(\\d{2})");
    f.bar.setReplaceText("$3.$2.\\1");
    CHECK(f.bar.replaceAll() == 2);
    CHECK(f.editor.markdown() == "31.01.2024 and 01.12.1999\nend");

    f.bar.setSearchText("^");
    f.bar.setReplaceText("> ");
    CHECK(f.bar.replaceAll() == 0); // empty matches are skipped, no endless loop
}
