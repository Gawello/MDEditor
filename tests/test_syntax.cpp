#include "TestHarness.h"
#include "syntax/Syntax.h"

#include <string>
#include <vector>

using namespace syntax;

namespace {

struct Line {
    std::vector<Span> spans;
    int state = 0;
};

Line run(std::u16string_view language, std::u16string_view text, int state = 0)
{
    Line line;
    line.state = highlightLine(languageForName(language), text, state, line.spans);
    return line;
}

// The token of the character at `at`.
Token tokenAt(const Line &line, int at)
{
    for (const Span &span : line.spans) {
        if (at >= span.start && at < span.start + span.length)
            return span.token;
    }
    return Token::Plain;
}

} // namespace

TEST_CASE(syntax_languages_by_name_suffix_and_file_name)
{
    CHECK(languageForName(u"c++") == languageForName(u"cpp"));
    CHECK(languageForName(u"Python") == languageForSuffix(u"py"));
    CHECK(languageForName(u"{.json}") == languageForSuffix(u"json"));
    CHECK(languageForName(u"python title=x.py") == languageForSuffix(u"py"));
    CHECK(languageForName(u"nonsense") == 0);
    CHECK(languageForName(u"") == 0);
    CHECK(languageForFileName(u"CMakeLists.txt") == languageForName(u"cmake"));
    CHECK(languageForFileName(u"Makefile") == languageForName(u"make"));
    CHECK(languageForFileName(u"notes.txt") == 0);
    CHECK(languageName(languageForSuffix(u"rs")) == u"Rust");
}

TEST_CASE(syntax_cpp_tokens)
{
    const std::u16string_view text = u"int main() { return 42; } // done";
    const Line line = run(u"cpp", text);
    CHECK(tokenAt(line, 0) == Token::Type);
    CHECK(tokenAt(line, 4) == Token::Function);
    CHECK(tokenAt(line, 13) == Token::Keyword);
    CHECK(tokenAt(line, 20) == Token::Number);
    CHECK(tokenAt(line, 27) == Token::Comment);
    CHECK(line.state == 0);

    const Line directive = run(u"cpp", u"#include <vector> // x");
    CHECK(tokenAt(directive, 0) == Token::Meta);
    CHECK(tokenAt(directive, 19) == Token::Comment);
}

TEST_CASE(syntax_block_comments_and_strings_span_lines)
{
    const Line open = run(u"cpp", u"x = 1; /* start");
    CHECK(open.state != 0);
    const Line inside = run(u"cpp", u"still comment", open.state);
    CHECK(tokenAt(inside, 3) == Token::Comment);
    CHECK(inside.state == open.state);
    const Line close = run(u"cpp", u"end */ y", inside.state);
    CHECK(tokenAt(close, 0) == Token::Comment);
    CHECK(tokenAt(close, 7) == Token::Plain);
    CHECK(close.state == 0);

    const Line doc = run(u"python", u"s = \"\"\"first");
    CHECK(doc.state != 0);
    CHECK(tokenAt(run(u"python", u"last\"\"\" + x", doc.state), 0) == Token::String);
    CHECK(run(u"python", u"last\"\"\" + x", doc.state).state == 0);

    // An ordinary string doesn't continue on the next line.
    CHECK(run(u"cpp", u"s = \"open").state == 0);
    const Line escaped = run(u"cpp", u"s = \"a\\\"b\"; x");
    CHECK(tokenAt(escaped, 9) == Token::String);
    CHECK(tokenAt(escaped, 12) == Token::Plain);
}

TEST_CASE(syntax_data_formats)
{
    const Line json = run(u"json", u"{\"key\": \"value\", \"n\": 3, \"ok\": true}");
    CHECK(tokenAt(json, 1) == Token::Property);
    CHECK(tokenAt(json, 9) == Token::String);
    CHECK(tokenAt(json, 22) == Token::Number);
    CHECK(tokenAt(json, 31) == Token::Constant);

    const Line yaml = run(u"yaml", u"name: demo # comment");
    CHECK(tokenAt(yaml, 0) == Token::Property);
    CHECK(tokenAt(yaml, 13) == Token::Comment);

    const Line ini = run(u"ini", u"[section]");
    CHECK(tokenAt(ini, 0) == Token::Tag);
    const Line pair = run(u"toml", u"port = 8080");
    CHECK(tokenAt(pair, 0) == Token::Property);
    CHECK(tokenAt(pair, 7) == Token::Number);

    const Line css = run(u"css", u"a { color: #fff; margin: 0 }");
    CHECK(tokenAt(css, 4) == Token::Property);
    CHECK(tokenAt(css, 11) == Token::Number);
}

TEST_CASE(syntax_markup)
{
    const Line tag = run(u"html", u"<a href=\"x\">link &amp; more</a>");
    CHECK(tokenAt(tag, 1) == Token::Tag);
    CHECK(tokenAt(tag, 3) == Token::Attribute);
    CHECK(tokenAt(tag, 9) == Token::String);
    CHECK(tokenAt(tag, 13) == Token::Plain);
    CHECK(tokenAt(tag, 18) == Token::Constant);
    CHECK(tag.state == 0);

    const Line open = run(u"xml", u"<item name=\"a\"");
    CHECK(open.state != 0); // still inside the tag
    const Line rest = run(u"xml", u"   value=\"b\">text", open.state);
    CHECK(tokenAt(rest, 3) == Token::Attribute);
    CHECK(tokenAt(rest, 14) == Token::Plain);

    const Line comment = run(u"xml", u"<!-- a");
    CHECK(tokenAt(run(u"xml", u"b --> <x>", comment.state), 0) == Token::Comment);
}

TEST_CASE(syntax_shell_and_diff)
{
    const Line sh = run(u"bash", u"echo $HOME ${X} # note");
    CHECK(tokenAt(sh, 0) == Token::Keyword);
    CHECK(tokenAt(sh, 5) == Token::Variable);
    CHECK(tokenAt(sh, 11) == Token::Variable);
    CHECK(tokenAt(sh, 18) == Token::Comment);
    CHECK(tokenAt(run(u"bash", u"echo a#b"), 6) == Token::Plain); // not a comment inside a word

    CHECK(tokenAt(run(u"diff", u"+added"), 0) == Token::Inserted);
    CHECK(tokenAt(run(u"diff", u"-removed"), 0) == Token::Deleted);
    CHECK(tokenAt(run(u"diff", u"@@ -1 +1 @@"), 0) == Token::Meta);

    const Line sql = run(u"sql", u"SELECT name FROM t -- all");
    CHECK(tokenAt(sql, 0) == Token::Keyword);
    CHECK(tokenAt(sql, 19) == Token::Comment);
}

TEST_CASE(syntax_states_fit_their_bits)
{
    for (const auto *language : {u"cpp", u"python", u"js", u"xml", u"go", u"powershell", u"lua", u"java"}) {
        const Line line = run(language, u"/* \"\"\" ` <!-- <# --[[ <x a=\"");
        CHECK(line.state >= 0 && line.state < (1 << stateBits));
    }
}
