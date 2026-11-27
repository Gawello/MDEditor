#pragma once

#include "markdown/BlockParser.h"

#include <string>
#include <string_view>
#include <vector>

// What a note contains, for the outline, the tag list, the graph and the quick switcher: one pass over the
// text with the same parsers the editor uses, so code blocks, math and comments don't count.
namespace md {

struct Heading {
    int line = 0;
    int level = 1;
    std::u16string text; // without the markers and inline formatting
};

struct NoteLink {
    std::u16string target; // as written: "Note", "folder/Note.md", "../x.md"; without "#heading" or "|alias"
    std::u16string anchor; // the part after '#', if any
    int line = 0;
    bool wiki = false;
    bool embed = false; // ![[...]] or ![...](...)
};

struct Property {
    std::u16string key;
    std::u16string value; // a list joined with ", "
    int line = 0;
};

struct NoteScan {
    std::vector<Heading> headings;
    std::vector<std::u16string> tags; // without '#', each once, in order of appearance
    std::vector<NoteLink> links;      // local targets only, not URLs
    std::vector<Property> properties; // frontmatter
    int frontmatterEnd = -1;          // line of the closing "---", -1 without frontmatter
};

NoteScan scanNote(std::u16string_view text);

// Splits on '\n', dropping a '\r' before it.
std::vector<std::u16string_view> splitLines(std::u16string_view text);

// The values of a frontmatter list: "[a, b]", "a, b", or the "- item" lines below the key (already joined).
std::vector<std::u16string> propertyList(std::u16string_view value);

// Every line classified, with the state carried from line to line (the first line may open frontmatter).
std::vector<BlockInfo> parseBlocks(const std::vector<std::u16string_view> &lines);

// Folding: the last line of what the line `line` can hide. A heading hides its section (up to the next heading
// of the same or a higher level), a list item its more indented lines, a callout's first line the rest of
// the callout; trailing blank lines stay visible. Returns `line` when there's nothing to fold.
int foldEnd(const std::vector<BlockInfo> &blocks, int line);

} // namespace md
