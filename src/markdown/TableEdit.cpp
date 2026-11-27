#include "markdown/TableEdit.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace md {

namespace {

std::u16string cellText(std::u16string_view line, const TableCell &cell)
{
    return std::u16string(line.substr(cell.start, cell.end - cell.start));
}

std::u16string delimiterFor(Align align)
{
    switch (align) {
    case Align::Left:
        return u":---";
    case Align::Center:
        return u":---:";
    case Align::Right:
        return u"---:";
    case Align::None:
        break;
    }
    return u"---";
}

std::optional<double> numberIn(const std::u16string &text)
{
    if (text.empty() || text.size() > 40)
        return std::nullopt;
    std::string ascii;
    for (const char16_t c : text) {
        if (c > 0x7f)
            return std::nullopt;
        ascii += char(c);
    }
    char *end = nullptr;
    const double value = std::strtod(ascii.c_str(), &end);
    if (end == ascii.c_str() || *end != '\0')
        return std::nullopt;
    return value;
}

std::u16string folded(const std::u16string &text)
{
    std::u16string result = text;
    for (char16_t &c : result) {
        if (c >= u'A' && c <= u'Z')
            c = char16_t(c - u'A' + u'a');
    }
    return result;
}

template <typename T>
bool moveElement(std::vector<T> &items, int from, int to)
{
    const int n = int(items.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return false;
    T item = std::move(items[from]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(item));
    return true;
}

} // namespace

std::optional<TableGrid> TableGrid::parse(const std::vector<std::u16string> &lines)
{
    if (lines.size() < 2)
        return std::nullopt;
    const auto aligns = parseDelimiterRow(lines[1]);
    if (!aligns || aligns->empty())
        return std::nullopt;

    TableGrid grid;
    grid.aligns = *aligns;
    size_t columns = grid.aligns.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == 1)
            continue;
        const TableRow row = splitTableRow(lines[i]);
        std::vector<std::u16string> cells;
        for (const TableCell &cell : row.cells)
            cells.push_back(cellText(lines[i], cell));
        columns = std::max(columns, cells.size()); // extra cells are kept rather than lost
        grid.rows.push_back(std::move(cells));
    }
    grid.aligns.resize(columns, Align::None);
    for (auto &row : grid.rows)
        row.resize(columns);
    return grid;
}

std::vector<std::u16string> TableGrid::render() const
{
    const auto line = [](const std::vector<std::u16string> &cells) {
        std::u16string text = u"|";
        for (const std::u16string &cell : cells)
            text += u' ' + cell + u" |";
        return text;
    };
    std::vector<std::u16string> lines;
    lines.push_back(line(rows.front()));
    std::vector<std::u16string> delimiter;
    for (const Align align : aligns)
        delimiter.push_back(delimiterFor(align));
    lines.push_back(line(delimiter));
    for (size_t r = 1; r < rows.size(); ++r)
        lines.push_back(line(rows[r]));
    return lines;
}

void TableGrid::insertRow(int at)
{
    at = std::clamp(at, 1, rowCount());
    rows.insert(rows.begin() + at, std::vector<std::u16string>(aligns.size()));
}

bool TableGrid::removeRow(int row)
{
    if (row < 1 || row >= rowCount())
        return false;
    rows.erase(rows.begin() + row);
    return true;
}

bool TableGrid::moveRow(int from, int to)
{
    if (from < 1 || to < 1)
        return false;
    return moveElement(rows, from, to);
}

void TableGrid::insertColumn(int at)
{
    at = std::clamp(at, 0, columnCount());
    aligns.insert(aligns.begin() + at, Align::None);
    for (auto &row : rows)
        row.insert(row.begin() + at, std::u16string());
}

bool TableGrid::removeColumn(int column)
{
    if (column < 0 || column >= columnCount() || columnCount() == 1)
        return false;
    aligns.erase(aligns.begin() + column);
    for (auto &row : rows)
        row.erase(row.begin() + column);
    return true;
}

bool TableGrid::moveColumn(int from, int to)
{
    if (!moveElement(aligns, from, to))
        return false;
    for (auto &row : rows)
        moveElement(row, from, to);
    return true;
}

void TableGrid::setAlign(int column, Align align)
{
    if (column >= 0 && column < columnCount())
        aligns[column] = align;
}

void TableGrid::sortRows(int column, bool descending)
{
    if (column < 0 || column >= columnCount() || rows.size() < 3)
        return;
    const auto less = [column](const std::vector<std::u16string> &a, const std::vector<std::u16string> &b) {
        const auto x = numberIn(a[column]);
        const auto y = numberIn(b[column]);
        if (x && y)
            return *x < *y;
        if (x || y)
            return x.has_value(); // numbers before text
        return folded(a[column]) < folded(b[column]);
    };
    if (descending)
        std::stable_sort(rows.begin() + 1, rows.end(), [&](const auto &a, const auto &b) { return less(b, a); });
    else
        std::stable_sort(rows.begin() + 1, rows.end(), less);
}

} // namespace md
