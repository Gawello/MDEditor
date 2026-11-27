#pragma once

#include "markdown/TableParser.h"

#include <optional>
#include <string>
#include <vector>

namespace md {

// A GFM table as a grid of cell texts, for structural edits (rows, columns, alignment). Cells keep their
// Markdown source, escaped pipes included. Row 0 is the header; the delimiter row is `aligns`.
struct TableGrid {
    std::vector<std::vector<std::u16string>> rows;
    std::vector<Align> aligns;

    int columnCount() const { return int(aligns.size()); }
    int rowCount() const { return int(rows.size()); }

    // `lines` is the whole table: header, delimiter row, body rows. Short rows are padded with empty cells.
    static std::optional<TableGrid> parse(const std::vector<std::u16string> &lines);
    // Header, delimiter and body rows, unpadded ("| a | b |"); Prettify lines them up.
    std::vector<std::u16string> render() const;

    // Rows are indexed as in `rows`: body rows start at 1, and the header can't be removed or moved.
    void insertRow(int at);
    bool removeRow(int row);
    bool moveRow(int from, int to);
    void insertColumn(int at);
    bool removeColumn(int column); // the last column can't be removed
    bool moveColumn(int from, int to);
    void setAlign(int column, Align align);
    // Sorts the body rows by one column's text, numbers by value, ignoring case.
    void sortRows(int column, bool descending);
};

// The table row index (as in TableGrid::rows) of line `line` of a table, or -1 for the delimiter row.
inline int tableRowOfLine(int line)
{
    return line == 0 ? 0 : line == 1 ? -1 : line - 1;
}

} // namespace md
