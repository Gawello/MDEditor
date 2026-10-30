#include "TestHarness.h"
#include "markdown/TableParser.h"

#include <string>

using namespace md;

static std::u16string cellText(std::u16string_view line, const TableCell &cell)
{
    return std::u16string(line.substr(cell.start, cell.end - cell.start));
}

TEST_CASE(split_with_outer_pipes)
{
    const std::u16string_view line = u"| a | bb |";
    const TableRow row = splitTableRow(line);
    CHECK(row.cells.size() == 2);
    CHECK(row.pipes.size() == 3);
    CHECK(cellText(line, row.cells[0]) == u"a");
    CHECK(cellText(line, row.cells[1]) == u"bb");
    CHECK(row.cells[0].segmentStart == 1);
    CHECK(row.cells[0].segmentEnd == 4);
}

TEST_CASE(split_without_outer_pipes)
{
    const std::u16string_view line = u"a | b";
    const TableRow row = splitTableRow(line);
    CHECK(row.cells.size() == 2);
    CHECK(cellText(line, row.cells[0]) == u"a");
    CHECK(cellText(line, row.cells[1]) == u"b");
}

TEST_CASE(split_empty_and_escaped_cells)
{
    const std::u16string_view line = u"|     | x \\| y |  |";
    const TableRow row = splitTableRow(line);
    CHECK(row.cells.size() == 3);
    CHECK(row.cells[0].start == row.cells[0].end);
    CHECK(row.cells[0].start == 2); // caret goes after the first space
    CHECK(cellText(line, row.cells[1]) == u"x \\| y");
    CHECK(row.cells[2].start == row.cells[2].end);

    CHECK(splitTableRow(u"|").cells.empty());
}

TEST_CASE(delimiter_rows)
{
    const auto aligns = parseDelimiterRow(u"| --- | :-- | :-: | --: |");
    CHECK(aligns.has_value());
    CHECK(aligns->size() == 4);
    CHECK((*aligns)[0] == Align::None);
    CHECK((*aligns)[1] == Align::Left);
    CHECK((*aligns)[2] == Align::Center);
    CHECK((*aligns)[3] == Align::Right);

    CHECK(parseDelimiterRow(u"-|-").has_value());
    CHECK(!parseDelimiterRow(u"---").has_value()); // a rule, not a table
    CHECK(!parseDelimiterRow(u"| --- | abc |").has_value());
    CHECK(!parseDelimiterRow(u"| : |").has_value());
    CHECK(!parseDelimiterRow(u"|  |").has_value());
}
