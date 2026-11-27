#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Syntax highlighting for code: a small, rule-based tokenizer per language, one line at a time. Pure C++,
// shared by plain-text files and fenced code blocks in Markdown.
namespace syntax {

enum class Token : std::uint8_t {
    Plain,
    Keyword,
    Type,
    Constant, // true, null, None, character references
    String,
    Number,
    Comment,
    Function, // a name followed by "("
    Property, // JSON/YAML keys, CSS properties, INI keys
    Tag,      // XML/HTML tags, INI sections
    Attribute,
    Variable, // $name in shells and PHP
    Meta,     // preprocessor lines, decorators, @rules
    Inserted, // diff
    Deleted,
};
constexpr int tokenCount = 15;

struct Span {
    int start = 0;
    int length = 0;
    Token token = Token::Plain;
};

// What a line leaves open for the next one (a block comment, a multi-line string, a tag); 0 = nothing.
// Always fits in `stateBits` bits.
constexpr int stateBits = 9;

// Languages are numbered from 1; 0 means no highlighting. Ids fit in 6 bits.
int languageForName(std::u16string_view name);     // "cpp", "c++", "py", "JSON"... (fence info strings)
int languageForSuffix(std::u16string_view suffix); // "cpp", "py", "json" (file names, without the dot)
int languageForFileName(std::u16string_view name); // "CMakeLists.txt", "Makefile", ".bashrc"
std::u16string languageName(int language);

// Appends the spans of `line` (in order, Plain text omitted) and returns the state for the next line.
int highlightLine(int language, std::u16string_view line, int state, std::vector<Span> &spans);

} // namespace syntax
