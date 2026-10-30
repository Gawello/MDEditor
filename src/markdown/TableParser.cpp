#include "markdown/TableParser.h"

#include <algorithm>

namespace md {

namespace {

bool isWs(char16_t c) { return c == u' ' || c == u'\t'; }

bool isBlank(std::u16string_view s, int from, int to)
{
    for (int i = from; i < to; ++i) {
        if (!isWs(s[i]))
            return false;
    }
    return true;
}

} // namespace

TableRow splitTableRow(std::u16string_view line)
{
    TableRow row;
    const int n = int(line.size());
    for (int i = 0; i < n; ++i) {
        if (line[i] == u'|' && !(i > 0 && line[i - 1] == u'\\'))
            row.pipes.push_back(i);
    }

    struct Segment {
        int from;
        int to;
    };
    std::vector<Segment> segments;
    int previous = -1;
    for (const int pipe : row.pipes) {
        segments.push_back({previous + 1, pipe});
        previous = pipe;
    }
    segments.push_back({previous + 1, n});

    // A leading or trailing pipe doesn't open an extra (empty) cell.
    if (!row.pipes.empty() && isBlank(line, segments.front().from, segments.front().to))
        segments.erase(segments.begin());
    if (!row.pipes.empty() && !segments.empty() && segments.back().to == n
        && isBlank(line, segments.back().from, segments.back().to))
        segments.pop_back();

    for (const Segment &segment : segments) {
        TableCell cell{segment.from, segment.to, segment.from, segment.to};
        while (cell.start < cell.end && isWs(line[cell.start]))
            ++cell.start;
        while (cell.end > cell.start && isWs(line[cell.end - 1]))
            --cell.end;
        if (cell.start == cell.end) // empty cell: put the caret after the first space
            cell.start = cell.end = std::min(segment.from + 1, segment.to);
        row.cells.push_back(cell);
    }
    return row;
}

std::optional<std::vector<Align>> parseDelimiterRow(std::u16string_view line)
{
    if (line.find(u'|') == std::u16string_view::npos)
        return std::nullopt;

    const TableRow row = splitTableRow(line);
    if (row.cells.empty())
        return std::nullopt;

    std::vector<Align> aligns;
    for (const TableCell &cell : row.cells) {
        std::u16string_view content = line.substr(cell.start, cell.end - cell.start);
        if (content.empty())
            return std::nullopt;
        const bool left = content.front() == u':';
        const bool right = content.size() > 1 && content.back() == u':';
        if (left)
            content.remove_prefix(1);
        if (right)
            content.remove_suffix(1);
        if (content.empty() || content.find_first_not_of(u'-') != std::u16string_view::npos)
            return std::nullopt;
        aligns.push_back(left && right ? Align::Center : right ? Align::Right : left ? Align::Left : Align::None);
    }
    return aligns;
}

} // namespace md
