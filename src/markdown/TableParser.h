#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace md {

enum class Align { None, Left, Center, Right };

struct TableCell {
    int segmentStart = 0; // text between the surrounding pipes, [segmentStart, segmentEnd)
    int segmentEnd = 0;
    int start = 0;        // trimmed content, [start, end); start == end for an empty cell
    int end = 0;
};

struct TableRow {
    std::vector<TableCell> cells;
    std::vector<int> pipes; // positions of unescaped '|'
};

// Splits a GitHub-style table row ("| a | b |", leading/trailing pipes optional).
TableRow splitTableRow(std::u16string_view line);

// Parses a delimiter row such as "| --- | :-: | --: |". Returns the column alignments.
std::optional<std::vector<Align>> parseDelimiterRow(std::u16string_view line);

} // namespace md
