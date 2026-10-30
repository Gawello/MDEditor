#pragma once

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
};

// State carried from one line to the next. The editor stores it in
// QTextBlock::userState, so it has to round-trip through a single int.
struct FenceState {
    char16_t fenceChar = 0; // 0 = outside of a fenced code block
    int fenceLength = 0;

    bool inside() const { return fenceChar != 0; }
    int encode() const; // -1 when outside (QSyntaxHighlighter's default state)
    static FenceState decode(int state);
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
    FenceState next;
};

// Classifies a single line of Markdown. `state` is the state left by the previous line.
LineResult parseLine(std::u16string_view line, FenceState state);

} // namespace md
