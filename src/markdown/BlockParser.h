#pragma once

#include <optional>
#include <string_view>

namespace md {

enum class BlockType {
    Paragraph,
    Blank,
    Heading,
    BulletItem,
    OrderedItem,
    TaskItem,
    Quote,
    FenceOpen,
    FenceClose,
    CodeLine,
    HorizontalRule,
    TableHeader,
    TableDelimiter,
    TableRow,
    MathOpen,    // "$$" starting a display formula over several lines
    MathLine,
    MathClose,   // the line ending with "$$"
    MathSingle,  // "$$ x $$" alone on a line
    CommentLine, // in a %% comment over several lines (both delimiter lines included)
};

inline bool isMathType(BlockType type)
{
    return type == BlockType::MathOpen || type == BlockType::MathLine || type == BlockType::MathClose
        || type == BlockType::MathSingle;
}

inline bool isTableType(BlockType type)
{
    return type == BlockType::TableHeader || type == BlockType::TableDelimiter || type == BlockType::TableRow;
}

// State carried from one line to the next. The editor stores it in
// QTextBlock::userState, so it has to round-trip through a single int.
struct BlockState {
    enum class Table { None, AfterHeader, Body };

    char16_t fenceChar = 0; // 0 = outside of a fenced code block
    int fenceLength = 0;
    Table table = Table::None;
    // Inside a fence: what the code's syntax highlighter carries from line to line (language and open
    // comment or string), 15 bits. The parser only passes it on; it's set by whoever highlights the code.
    int code = 0;
    // Inside a block that continues on the next line: $$ math $$ or a %% comment %%.
    enum class Block { None, Math, Comment };
    Block block = Block::None;

    bool inFence() const { return fenceChar != 0; }
    int encode() const; // -1 for the default state (QSyntaxHighlighter's initial value)
    static BlockState decode(int state);
};

struct BlockInfo {
    BlockType type = BlockType::Paragraph;
    int headingLevel = 0;
    int indent = 0;          // leading whitespace before the marker
    int markerStart = 0;     // syntax prefix such as "## ", "> ", "- ", "1. "
    int markerLength = 0;
    int contentStart = 0;    // first character of inline content
    int checkboxStart = -1;  // task items: position of "[ ]"
    bool checked = false;
    int orderedNumber = 0;
    char16_t listMarker = 0; // '-', '*', '+' for bullets, '.' or ')' for ordered items

    bool isList() const
    {
        return type == BlockType::BulletItem || type == BlockType::OrderedItem
            || type == BlockType::TaskItem;
    }
};

struct LineResult {
    BlockInfo info;
    BlockState next;
};

// Classifies a single line of Markdown. `state` is the state left by the previous line;
// `nextLine` is needed to recognize table headers (which must be followed by a delimiter row).
LineResult parseLine(std::u16string_view line, BlockState state,
                     std::optional<std::u16string_view> nextLine = std::nullopt);

} // namespace md
