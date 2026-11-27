#include "TestHarness.h"
#include "markdown/TableEdit.h"

#include <string>
#include <vector>

using namespace md;

using Lines = std::vector<std::u16string>;

static TableGrid grid(const Lines &lines)
{
    const auto parsed = TableGrid::parse(lines);
    CHECK(parsed.has_value());
    return parsed.value_or(TableGrid{});
}

TEST_CASE(table_grid_round_trips_and_pads_short_rows)
{
    const TableGrid t = grid({u"| a | b |", u"| :-- | --: |", u"| 1 |"});
    CHECK(t.rowCount() == 2);
    CHECK(t.columnCount() == 2);
    CHECK(t.render() == (Lines{u"| a | b |", u"| :--- | ---: |", u"| 1 |  |"}));
}

TEST_CASE(table_grid_rows)
{
    TableGrid t = grid({u"| h |", u"| - |", u"| 1 |", u"| 2 |"});
    t.insertRow(2);
    CHECK(t.render() == (Lines{u"| h |", u"| --- |", u"| 1 |", u"|  |", u"| 2 |"}));
    CHECK(!t.removeRow(0)); // the header stays
    CHECK(t.removeRow(2));
    CHECK(t.moveRow(2, 1));
    CHECK(!t.moveRow(1, 0));
    CHECK(t.render() == (Lines{u"| h |", u"| --- |", u"| 2 |", u"| 1 |"}));
}

TEST_CASE(table_grid_columns_and_alignment)
{
    TableGrid t = grid({u"| a | b |", u"| - | - |", u"| 1 | 2 |"});
    t.insertColumn(1);
    t.setAlign(1, Align::Center);
    CHECK(t.render() == (Lines{u"| a |  | b |", u"| --- | :---: | --- |", u"| 1 |  | 2 |"}));
    CHECK(t.moveColumn(0, 2));
    CHECK(t.render() == (Lines{u"|  | b | a |", u"| :---: | --- | --- |", u"|  | 2 | 1 |"}));
    CHECK(t.removeColumn(0));
    CHECK(t.removeColumn(0));
    CHECK(!t.removeColumn(0)); // one column is left
}

TEST_CASE(table_grid_keeps_escaped_pipes)
{
    const TableGrid t = grid({u"| a \\| b |", u"| - |"});
    CHECK(t.rows[0][0] == u"a \\| b");
}

TEST_CASE(table_grid_sorts_numbers_by_value)
{
    TableGrid t = grid({u"| n |", u"| - |", u"| 10 |", u"| b |", u"| 9 |", u"| A |"});
    t.sortRows(0, false);
    CHECK(t.rows[1][0] == u"9");
    CHECK(t.rows[2][0] == u"10");
    CHECK(t.rows[3][0] == u"A");
    CHECK(t.rows[4][0] == u"b");
    t.sortRows(0, true);
    CHECK(t.rows[1][0] == u"b");
}
